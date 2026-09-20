#include <QtTest>
#include <QAbstractItemModel>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedPointer>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cmath>
#include "blameview.h"

using namespace LqCompare;
using namespace LqCompare::Vcs;

namespace {
const QString Snapshot = QString(40, 'f');
const QString Origin = QString(40, 'a');
const QString Parent = QString(40, 'b');
const QString Other = QString(40, 'c');
const QString Topic = QString(40, 'd');

struct BlameCall { QString path, revision; };
struct ContentCall { QString path; Source source; };
struct DiffCall { Source left, right; };

Commit commit(const QString &id, const QStringList &parents = {})
{
    Commit value;
    value.id = id;
    value.parents = parents;
    value.author = QStringLiteral("Alice");
    value.email = QStringLiteral("alice@example.invalid");
    value.subject = QStringLiteral("Fixture subject");
    value.message = QStringLiteral("Fixture subject\n\nFull fixture message.");
    value.date = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);
    return value;
}

BlameLine line(const QString &id, const QString &author, int finalLine, const QString &path = QStringLiteral("src/original.cpp"))
{
    BlameLine value;
    value.commit = id;
    value.author = author;
    value.text = QStringLiteral("text at line %1").arg(finalLine);
    value.originalLine = finalLine + 10;
    value.finalLine = finalLine;
    value.date = QDateTime::fromSecsSinceEpoch(1700000000 - finalLine * 86400, Qt::UTC);
    value.originalPath = path;
    value.email = author.toLower() + QStringLiteral("@example.invalid");
    value.summary = QStringLiteral("Summary for line %1").arg(finalLine);
    return value;
}

QVector<BlameLine> exampleLines()
{
    return {line(Origin, "Alice", 1), line(Origin, "Alice", 2),
            line(Origin, "Alice", 3, "src/other.cpp"),
            line(Other, "Bob", 4, "src/current.cpp"), line(Other, "Bob", 5, "src/current.cpp"),
            line(Origin, "Alice", 6), line(Parent, "Carol", 7, "src/current.cpp")};
}

// Each backend call is recorded at its actual boundary, including availability.
// A deliberately uncooperative blocked blame returns obsolete rows after cancel.
class FakeBackend final : public Backend {
public:
    explicit FakeBackend(QString root) : root(std::move(root)), guiThread(QCoreApplication::instance()->thread())
    {
        revisionCommits.insert("HEAD", commit(Snapshot, {Parent}));
        revisionCommits.insert("topic", commit(Topic, {Parent}));
        revisionCommits.insert(Origin, commit(Origin, {Parent}));
        revisionCommits.insert(Other, commit(Other, {Parent}));
        revisionCommits.insert(Parent, commit(Parent));
        currentContent.exists = true;
        currentContent.bytes = "one\ntwo\nthree\nfour\nfive\nsix\nseven\n";
        lines = exampleLines();
        changes.append({QStringLiteral("src/original.cpp"), {}, QStringLiteral("M"), false});
    }

    Error availability() const override
    { recordThread(); QMutexLocker lock(&mutex); return unavailable; }

    Result<Repository> detectRepo(const QString &path, const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        detectedPaths.append(path);
        Repository repository;
        repository.root = root;
        repository.gitDirectory = root + "/.git";
        repository.commonDirectory = repository.gitDirectory;
        repository.hasHead = hasHead;
        repository.head = hasHead ? Snapshot : QString();
        repository.branch = "main";
        return {repository, detectionError};
    }

    Result<QVector<Change>> status(const Repository &, const std::atomic_bool *) const override
    { recordThread(); return {}; }

    Result<QVector<Commit>> log(const Repository &, const LogQuery &query, const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        logQueries.append(query);
        if (logError.isError()) return {{}, logError};
        if (!hasHead && query.revision == QStringLiteral("HEAD"))
            return {{}, {ErrorCode::NoHead, QStringLiteral("仓库尚无 HEAD"), {}}};
        if (revisionCommits.contains(query.revision)) return {{revisionCommits.value(query.revision)}, {}};
        if (query.revision == Snapshot || query.revision == Topic) return {{commit(query.revision, {Parent})}, {}};
        return {{}, {ErrorCode::InvalidRevision, QStringLiteral("修订不存在"), query.revision}};
    }

    Result<QVector<Change>> diff(const Repository &, const Source &left, const Source &right,
                                const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        diffRequests.append({left, right});
        return {changes, diffError};
    }

    Result<FileContent> catFile(const Repository &, const QString &path, const Source &source,
                                const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        contentRequests.append({path, source});
        if (contentError.isError()) return {{}, contentError};
        if (source.kind == SourceKind::Empty) return {};
        FileContent content = currentContent;
        content.label = sourceLabel(source) + ':' + path;
        content.objectId = source.revision;
        if (source.revision != Snapshot && source.revision != Topic)
            content.bytes = payload(path, source);
        return {content, {}};
    }

    Result<QVector<Reference>> references(const Repository &, const std::atomic_bool *) const override
    { recordThread(); return {}; }

    Result<QVector<BlameLine>> blame(const Repository &, const QString &path, const QString &revision,
                                    const std::atomic_bool *cancel) const override
    {
        recordThread();
        Result<QVector<BlameLine>> result;
        {
            QMutexLocker lock(&mutex);
            blameRequests.append({path, revision});
            result = {lines, blameError};
        }
        if (blockNextBlame.exchange(false)) {
            blockedEntered.store(true);
            QElapsedTimer timer;
            timer.start();
            while (!releaseBlocked.load() && timer.elapsed() < 5000) {
                if (cancel && cancel->load()) cancellationObserved.store(true);
                QThread::msleep(2);
            }
            blockedExited.store(true);
        }
        return result;
    }

    static QByteArray payload(const QString &path, const Source &source)
    { return (path + QStringLiteral(" @ ") + source.revision + '\n').toUtf8(); }

    void setLines(QVector<BlameLine> value) { QMutexLocker lock(&mutex); lines = std::move(value); }
    void setChanges(QVector<Change> value) { QMutexLocker lock(&mutex); changes = std::move(value); }
    void setCommit(const Commit &value) { QMutexLocker lock(&mutex); revisionCommits.insert(value.id, value); }
    void setHead(bool value) { QMutexLocker lock(&mutex); hasHead = value; }
    void setUnavailable(const Error &value) { QMutexLocker lock(&mutex); unavailable = value; }
    void setDetectionError(const Error &value) { QMutexLocker lock(&mutex); detectionError = value; }
    void setLogError(const Error &value) { QMutexLocker lock(&mutex); logError = value; }
    void setBlameError(const Error &value) { QMutexLocker lock(&mutex); blameError = value; }
    void setContent(const FileContent &value) { QMutexLocker lock(&mutex); currentContent = value; }
    void setContentError(const Error &value) { QMutexLocker lock(&mutex); contentError = value; }
    QVector<BlameCall> blameCalls() const { QMutexLocker lock(&mutex); return blameRequests; }
    QVector<ContentCall> contentCalls() const { QMutexLocker lock(&mutex); return contentRequests; }
    QVector<DiffCall> diffCalls() const { QMutexLocker lock(&mutex); return diffRequests; }
    QVector<LogQuery> logCalls() const { QMutexLocker lock(&mutex); return logQueries; }
    QStringList detectCalls() const { QMutexLocker lock(&mutex); return detectedPaths; }

    mutable std::atomic_bool calledOnGui{false}, blockNextBlame{false}, releaseBlocked{false},
        blockedEntered{false}, blockedExited{false}, cancellationObserved{false};

private:
    void recordThread() const { if (QThread::currentThread() == guiThread) calledOnGui.store(true); }
    QString root;
    QThread *const guiThread;
    mutable QMutex mutex;
    bool hasHead = true;
    QVector<BlameLine> lines;
    QVector<Change> changes;
    FileContent currentContent;
    QHash<QString, Commit> revisionCommits;
    Error unavailable, detectionError, logError, contentError, blameError, diffError;
    mutable QStringList detectedPaths;
    mutable QVector<BlameCall> blameRequests;
    mutable QVector<ContentCall> contentRequests;
    mutable QVector<DiffCall> diffRequests;
    mutable QVector<LogQuery> logQueries;
};

struct Fixture {
    QTemporaryDir directory;
    QSharedPointer<FakeBackend> backend;
    QString path;
    Fixture()
    {
        if (!directory.isValid()) qFatal("temporary fixture directory failed");
        QDir().mkpath(directory.filePath("src"));
        path = directory.filePath("src/current.cpp");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("one\ntwo\nthree\nfour\nfive\nsix\nseven\n") < 0)
            qFatal("fixture write failed");
        backend = QSharedPointer<FakeBackend>::create(QFileInfo(directory.path()).canonicalFilePath());
    }
};

QTableView *table(BlameView &view) { return view.findChild<QTableView *>("blameLines"); }
QLabel *status(BlameView &view) { return view.findChild<QLabel *>("blameStatus"); }
void unfold(BlameView &view) { view.findChild<QCheckBox *>("blameFoldBlocks")->setChecked(false); }
void selectRow(BlameView &view, int row)
{
    table(view)->setCurrentIndex(table(view)->model()->index(row, 0));
    table(view)->selectRow(row);
}
QVector<int> lineNumbers(BlameView &view)
{
    QVector<int> value;
    for (int row = 0; row < table(view)->model()->rowCount(); ++row)
        value.append(table(view)->model()->index(row, 0).data(Qt::UserRole).toInt());
    return value;
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return "<unreadable snapshot>";
    return file.readAll();
}
double luminance(const QColor &color)
{
    auto channel = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}
QColor roleColor(const QVariant &value)
{
    if (value.canConvert<QBrush>()) return qvariant_cast<QBrush>(value).color();
    if (value.canConvert<QColor>()) return qvariant_cast<QColor>(value);
    return {};
}
}

class VcsBlameViewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<Comparison>(); }

    void resolvesRevisionAndPreservesMetadataOffGuiThread()
    {
        Fixture fixture;
        BlameView view(fixture.path, fixture.backend);
        QVERIFY(table(view));
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        unfold(view);
        QCOMPARE(table(view)->model()->rowCount(), 7);
        QCOMPARE(table(view)->model()->columnCount(), 6);
        QCOMPARE(fixture.backend->blameCalls().last().path, QStringLiteral("src/current.cpp"));
        QCOMPARE(fixture.backend->blameCalls().last().revision, Snapshot);
        QCOMPARE(int(fixture.backend->contentCalls().first().source.kind), int(SourceKind::Revision));
        QCOMPARE(fixture.backend->contentCalls().first().source.revision, Snapshot);
        QCOMPARE(lineNumbers(view), QVector<int>({1, 2, 3, 4, 5, 6, 7}));
        QCOMPARE(table(view)->model()->index(0, 5).data().toString(), QStringLiteral("text at line 1"));
        selectRow(view, 0);
        const QString details = view.findChild<QPlainTextEdit *>("blameDetails")->toPlainText();
        QVERIFY(details.contains(Origin));
        QVERIFY(details.contains("alice@example.invalid"));
        QVERIFY(details.contains("src/original.cpp"));
        const QString tooltip = table(view)->model()->index(0, 2).data(Qt::ToolTipRole).toString();
        QVERIFY(tooltip.contains(Origin));
        QVERIFY(tooltip.contains("Summary for line 1"));
        QVERIFY(!fixture.backend->calledOnGui.load());

        view.setRevision("topic");
        QTRY_COMPARE(fixture.backend->blameCalls().size(), 2);
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(fixture.backend->blameCalls().last().revision, Topic);
        QCOMPARE(view.revision(), QStringLiteral("topic"));
    }

    void authorAndCommitFiltersRetainOriginalLineNumbers()
    {
        Fixture fixture;
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        unfold(view);
        auto *authors = view.findChild<QComboBox *>("blameAuthorFilter");
        auto *commits = view.findChild<QLineEdit *>("blameCommitFilter");
        QVERIFY(authors && commits);
        const int bob = authors->findText(QStringLiteral("Bob"));
        QVERIFY(bob > 0);
        authors->setCurrentIndex(bob);
        QTRY_COMPARE(lineNumbers(view), QVector<int>({4, 5}));
        commits->setText(Origin.left(8));
        QTRY_COMPARE(table(view)->model()->rowCount(), 0);
        QVERIFY(!view.findChild<QPushButton *>("blameOpenRevision")->isEnabled());
        authors->setCurrentIndex(0);
        QTRY_COMPARE(lineNumbers(view), QVector<int>({1, 2, 3, 6}));
        commits->clear();
        QTRY_COMPARE(table(view)->model()->rowCount(), 7);
        QCOMPARE(fixture.backend->blameCalls().size(), 1);
    }

    void foldingUsesContiguousCommitAndOriginalPathBlocks()
    {
        Fixture fixture;
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        auto *fold = view.findChild<QCheckBox *>("blameFoldBlocks");
        QVERIFY(fold);
        fold->setChecked(true);
        QTRY_COMPARE(lineNumbers(view), QVector<int>({1, 3, 4, 6, 7}));
        QVERIFY(!table(view)->model()->index(0, 0).data().toString().isEmpty());
        selectRow(view, 2);
        QSignalSpy revisions(&view, &BlameView::revisionRequested);
        view.showSelectedRevision();
        QCOMPARE(revisions.size(), 1);
        QCOMPARE(revisions.first().at(1).toString(), Other);
        QCOMPARE(revisions.first().at(3).toInt(), 14);
        fold->setChecked(false);
        QTRY_COMPARE(table(view)->model()->rowCount(), 7);
    }

    void allColorModesKeepReadableContrast()
    {
        Fixture fixture;
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        unfold(view);
        for (const auto mode : {BlameView::Author, BlameView::Age, BlameView::CommitBlock}) {
            view.setColorMode(mode);
            QCOMPARE(view.colorMode(), mode);
            int coloredCells = 0;
            for (int row = 0; row < table(view)->model()->rowCount(); ++row) {
                for (int column = 0; column < table(view)->model()->columnCount(); ++column) {
                    const auto index = table(view)->model()->index(row, column);
                    const QColor background = roleColor(index.data(Qt::BackgroundRole));
                    if (!background.isValid()) continue;
                    const QColor foreground = roleColor(index.data(Qt::ForegroundRole));
                    QVERIFY(foreground.isValid());
                    const double a = luminance(background), b = luminance(foreground);
                    const double contrast = (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
                    QVERIFY2(contrast >= 4.5, qPrintable(QStringLiteral("mode %1 row %2 column %3 contrast %4")
                        .arg(int(mode)).arg(row).arg(column).arg(contrast)));
                    ++coloredCells;
                }
            }
            QVERIFY(coloredCells >= table(view)->model()->rowCount());
        }
        auto *selector = view.findChild<QComboBox *>("blameColorMode");
        selector->setCurrentIndex(BlameView::Age);
        QCOMPARE(view.colorMode(), BlameView::Age);
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_BLAME_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            view.resize(1280, 760);
            view.show();
            QTest::qWait(70);
            QVERIFY(view.grab().save(screenshot));
        }
    }

    // 上面那条用例只保证「有颜色且读得清」。把渐变方向写反、或让「按作者」
    // 实际按提交块取色，界面看起来都完全正常（颜色依然好看、对比度依然合格），
    // 所以 VCS-013 的前两条要求必须单独钉住，否则没有任何用例会变红。
    void colorModesMatchAuthorsAndAgeDirection()
    {
        Fixture fixture;
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        unfold(view);
        QCOMPARE(table(view)->model()->rowCount(), 7);
        const auto backgroundAt = [&](int row) {
            return roleColor(table(view)->model()->index(row, 5).data(Qt::BackgroundRole));
        };
        view.setColorMode(BlameView::Author);
        // 第 1 行与第 6 行同属 Alice，但分属不同的提交块：
        // 「按作者着色」必须让它们同色，否则它其实是按提交块着色。
        QCOMPARE(backgroundAt(0).name(), backgroundAt(5).name());
        // 第 1 行（Alice）与第 4 行（Bob）必须不同色。
        QVERIFY(backgroundAt(0) != backgroundAt(3));
        view.setColorMode(BlameView::Age);
        // 夹具里 finalLine 越大日期越旧：越旧必须越深。
        QVERIFY2(luminance(backgroundAt(6)) < luminance(backgroundAt(0)), "越旧的行底色必须更深");
    }

    void revisionNavigationUsesAttributedPathAndLine()
    {
        Fixture fixture;
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        unfold(view);
        selectRow(view, 2);
        QSignalSpy revisions(&view, &BlameView::revisionRequested);
        auto *open = view.findChild<QPushButton *>("blameOpenRevision");
        QVERIFY(open && open->isEnabled());
        open->click();
        QCOMPARE(revisions.size(), 1);
        QCOMPARE(revisions.first().at(0).toString(), QFileInfo(fixture.directory.path()).canonicalFilePath());
        QCOMPARE(revisions.first().at(1).toString(), Origin);
        QCOMPARE(revisions.first().at(2).toString(), QStringLiteral("src/other.cpp"));
        QCOMPARE(revisions.first().at(3).toInt(), 13);
    }

    void parentComparisonUsesOriginalPathAndRetainsSnapshots_data()
    {
        QTest::addColumn<QString>("changeStatus");
        QTest::addColumn<QString>("oldPath");
        QTest::addColumn<bool>("rootCommit");
        QTest::addColumn<bool>("emptyLeft");
        QTest::newRow("rename-first-parent") << QString("R100") << QString("src/before.cpp") << false << false;
        QTest::newRow("new-file") << QString("A") << QString() << false << true;
        QTest::newRow("root-commit") << QString("A") << QString() << true << true;
    }

    void parentComparisonUsesOriginalPathAndRetainsSnapshots()
    {
        QFETCH(QString, changeStatus);
        QFETCH(QString, oldPath);
        QFETCH(bool, rootCommit);
        QFETCH(bool, emptyLeft);
        Fixture fixture;
        fixture.backend->setLines({line(Origin, "Alice", 1)});
        fixture.backend->setCommit(commit(Origin, rootCommit ? QStringList() : QStringList{Parent, Other}));
        fixture.backend->setChanges({{QStringLiteral("src/original.cpp"), oldPath, changeStatus, false}});
        QScopedPointer<BlameView> view(new BlameView(fixture.path, fixture.backend));
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view->isBusy());
        selectRow(*view, 0);
        const int initialReads = fixture.backend->contentCalls().size();
        QSignalSpy compares(view.data(), &BlameView::compareRequested);
        auto *compare = view->findChild<QPushButton *>("blameCompareParent");
        QVERIFY(compare && compare->isEnabled());
        compare->click();
        QTRY_COMPARE(compares.size(), 1);
        QTRY_VERIFY(!view->isBusy());
        const auto queries = fixture.backend->logCalls();
        QCOMPARE(queries.last().revision, Origin);
        QCOMPARE(queries.last().limit, 1);
        QVERIFY(!fixture.backend->diffCalls().isEmpty());
        const auto diff = fixture.backend->diffCalls().last();
        QCOMPARE(int(diff.left.kind), int(rootCommit ? SourceKind::Empty : SourceKind::Revision));
        if (!rootCommit) QCOMPARE(diff.left.revision, Parent);
        QCOMPARE(diff.right.revision, Origin);
        const auto reads = fixture.backend->contentCalls().mid(initialReads);
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reads[0].path, oldPath.isEmpty() ? QStringLiteral("src/original.cpp") : oldPath);
        QCOMPARE(reads[1].path, QStringLiteral("src/original.cpp"));
        QCOMPARE(int(reads[0].source.kind), int(emptyLeft ? SourceKind::Empty : SourceKind::Revision));
        QCOMPARE(reads[1].source.revision, Origin);
        Comparison comparison = qvariant_cast<Comparison>(compares.first().first());
        QVERIFY(comparison.lifetime && comparison.lifetime->isValid());
        const QString snapshotDirectory = comparison.lifetime->path();
        QCOMPARE(readFile(comparison.leftPath), emptyLeft ? QByteArray() : FakeBackend::payload(reads[0].path, reads[0].source));
        QCOMPARE(readFile(comparison.rightPath), FakeBackend::payload(reads[1].path, reads[1].source));
        QVERIFY(!(QFile::permissions(comparison.leftPath) & QFileDevice::WriteOwner));
        QVERIFY(!(QFile::permissions(comparison.rightPath) & QFileDevice::WriteOwner));
        QVERIFY(!fixture.backend->calledOnGui.load());
        compares.clear();
        view.reset();
        QVERIFY(QFileInfo::exists(comparison.leftPath));
        comparison = {};
        QTRY_VERIFY(!QFileInfo::exists(snapshotDirectory));
    }

    void missingOriginalPathIsNeverGuessed()
    {
        Fixture fixture;
        fixture.backend->setLines({line(Origin, "Alice", 1, {})});
        BlameView view(fixture.path, fixture.backend);
        QSignalSpy errors(&view, &BlameView::errorOccurred);
        QSignalSpy revisions(&view, &BlameView::revisionRequested);
        QSignalSpy compares(&view, &BlameView::compareRequested);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        if (table(view)->model()->rowCount()) selectRow(view, 0);
        view.showSelectedRevision();
        view.compareSelectedWithParent();
        QTRY_VERIFY(!errors.isEmpty() || status(view)->text().contains(QStringLiteral("路径")));
        QVERIFY(status(view)->text().contains(QStringLiteral("路径")));
        QCOMPARE(revisions.size(), 0);
        QCOMPARE(compares.size(), 0);
        QVERIFY(fixture.backend->diffCalls().isEmpty());
    }

    void unmatchedOriginalPathDoesNotCompareAnotherFile()
    {
        Fixture fixture;
        fixture.backend->setLines({line(Origin, "Alice", 1)});
        fixture.backend->setChanges({{QStringLiteral("unrelated.cpp"), {}, QStringLiteral("M"), false}});
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->blameCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        selectRow(view, 0);
        QSignalSpy errors(&view, &BlameView::errorOccurred);
        QSignalSpy compares(&view, &BlameView::compareRequested);
        const int initialReads = fixture.backend->contentCalls().size();
        view.compareSelectedWithParent();
        QTRY_VERIFY(!errors.isEmpty());
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(compares.size(), 0);
        QCOMPARE(fixture.backend->contentCalls().size(), initialReads);
    }

    void missingGitAndNoHeadAreExplicit_data()
    {
        QTest::addColumn<bool>("missingGit");
        QTest::newRow("missing-git") << true;
        QTest::newRow("unborn-head") << false;
    }

    void missingGitAndNoHeadAreExplicit()
    {
        QFETCH(bool, missingGit);
        Fixture fixture;
        if (missingGit) fixture.backend->setUnavailable({ErrorCode::Unavailable, QStringLiteral("未检测到 git"), {}});
        else fixture.backend->setHead(false);
        BlameView view(fixture.path, fixture.backend);
        QSignalSpy errors(&view, &BlameView::errorOccurred);
        QTRY_VERIFY(!errors.isEmpty());
        QTRY_VERIFY(!view.isBusy());
        QVERIFY(status(view)->text().contains(missingGit ? QStringLiteral("git") : QStringLiteral("HEAD"), Qt::CaseInsensitive));
        QCOMPARE(table(view)->model()->rowCount(), 0);
        QVERIFY(!view.findChild<QPushButton *>("blameOpenRevision")->isEnabled());
        QVERIFY(!view.findChild<QPushButton *>("blameCompareParent")->isEnabled());
        if (missingGit) QVERIFY(!view.findChild<QComboBox *>("blameColorMode")->isEnabled());
        QVERIFY(fixture.backend->blameCalls().isEmpty());
        QVERIFY(!fixture.backend->calledOnGui.load());
    }

    void unsupportedFileContentIsExplained_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("empty") << 0;
        QTest::newRow("untracked-or-absent-in-revision") << 1;
        QTest::newRow("binary") << 2;
        QTest::newRow("invalid-utf8") << 3;
        QTest::newRow("backend-unsupported") << 4;
    }

    void unsupportedFileContentIsExplained()
    {
        QFETCH(int, kind);
        Fixture fixture;
        FileContent content;
        content.exists = kind != 1;
        if (kind == 2) { content.bytes = QByteArray("binary\0file", 11); content.binary = true; }
        if (kind == 3) content.bytes = QByteArray::fromHex("c328");
        if (kind == 4) fixture.backend->setContentError({ErrorCode::Unsupported, QStringLiteral("不支持该文件编码"), {}});
        fixture.backend->setContent(content);
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(!fixture.backend->contentCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(table(view)->model()->rowCount(), 0);
        QVERIFY(!view.findChild<QPushButton *>("blameOpenRevision")->isEnabled());
        QVERIFY(!view.findChild<QPushButton *>("blameCompareParent")->isEnabled());
        const QString message = status(view)->text();
        if (kind == 0) QVERIFY2(message.contains(QStringLiteral("空")), qPrintable(message));
        if (kind == 1) QVERIFY2(message.contains(QStringLiteral("不存在")) || message.contains(QStringLiteral("未跟踪"))
                                || message.contains(QStringLiteral("所选修订")), qPrintable(message));
        if (kind == 2) QVERIFY2(message.contains(QStringLiteral("二进制")), qPrintable(message));
        if (kind >= 3) QVERIFY2(message.contains(QStringLiteral("编码")) || message.contains(QStringLiteral("UTF")), qPrintable(message));
        QVERIFY(fixture.backend->blameCalls().isEmpty());
    }

    void backendFailureIsVisibleAndRetryWorks()
    {
        Fixture fixture;
        fixture.backend->setBlameError({ErrorCode::Timeout, QStringLiteral("追溯查询超时"), QStringLiteral("fixture timeout")});
        BlameView view(fixture.path, fixture.backend);
        QSignalSpy errors(&view, &BlameView::errorOccurred);
        QTRY_VERIFY(!errors.isEmpty());
        QTRY_VERIFY(!view.isBusy());
        QVERIFY(status(view)->text().contains(QStringLiteral("超时")));
        QCOMPARE(table(view)->model()->rowCount(), 0);
        fixture.backend->setBlameError({});
        view.refresh();
        QTRY_VERIFY(!view.isBusy());
        unfold(view);
        QTRY_COMPARE(table(view)->model()->rowCount(), 7);
        QCOMPARE(fixture.backend->blameCalls().size(), 2);
    }

    void cancelAndRefreshDiscardLateRows()
    {
        Fixture fixture;
        fixture.backend->setLines({line(Origin, "Old", 1)});
        fixture.backend->blockNextBlame.store(true);
        BlameView view(fixture.path, fixture.backend);
        QTRY_VERIFY(fixture.backend->blockedEntered.load());
        QVERIFY(view.isBusy());
        QElapsedTimer timer;
        timer.start();
        view.findChild<QPushButton *>("blameCancel")->click();
        QVERIFY(timer.elapsed() < 250);
        QVERIFY(!view.isBusy());
        QTRY_VERIFY(fixture.backend->cancellationObserved.load());
        QVERIFY(status(view)->text().contains(QStringLiteral("取消")));
        fixture.backend->setLines({line(Other, "New", 9)});
        view.refresh();
        QTRY_COMPARE(fixture.backend->blameCalls().size(), 2);
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(lineNumbers(view), QVector<int>({9}));
        fixture.backend->releaseBlocked.store(true);
        QTRY_VERIFY(fixture.backend->blockedExited.load());
        QTest::qWait(30);
        QCOMPARE(lineNumbers(view), QVector<int>({9}));
        QVERIFY(!fixture.backend->calledOnGui.load());
    }

    void deletingViewCancelsWorkerWithoutBlocking()
    {
        Fixture fixture;
        fixture.backend->blockNextBlame.store(true);
        QScopedPointer<BlameView> view(new BlameView(fixture.path, fixture.backend));
        QTRY_VERIFY(fixture.backend->blockedEntered.load());
        QElapsedTimer timer;
        timer.start();
        view.reset();
        QVERIFY(timer.elapsed() < 250);
        QTRY_VERIFY(fixture.backend->cancellationObserved.load());
        fixture.backend->releaseBlocked.store(true);
        QTRY_VERIFY(fixture.backend->blockedExited.load());
        QTest::qWait(20);
        QVERIFY(!fixture.backend->calledOnGui.load());
    }
};

QTEST_MAIN(VcsBlameViewTests)
#include "tst_vcsblameview.moc"
