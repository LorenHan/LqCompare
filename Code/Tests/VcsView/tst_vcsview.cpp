#include <QtTest>
#include <QComboBox>
#include <QDir>
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
#include <QThread>
#include <QTreeWidget>
#include <atomic>
#include "vcsbackend.h"
#include "vcsview.h"

using namespace LqCompare;
using namespace LqCompare::Vcs;

namespace {
struct DiffCall { QString root; Source left, right; };
struct ContentCall { QString path; Source source; };

// Records the actual backend boundary. A blocked diff deliberately returns its
// original result even after cancellation, to exercise the view's stale-result
// protection independently of a cooperative Git process.
class FakeBackend final : public Backend {
public:
    FakeBackend() : guiThread(QCoreApplication::instance()->thread()) {}

    Error availability() const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        return unavailable;
    }
    Result<Repository> detectRepo(const QString &path, const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        ++detectCount;
        Repository repo;
        repo.root = fixedRoot.isEmpty() ? path : fixedRoot;
        repo.gitDirectory = repo.root + "/.git";
        repo.commonDirectory = repo.gitDirectory;
        repo.hasHead = hasHead;
        repo.head = hasHead ? QString(40, 'a') : QString();
        repo.branch = "main";
        return {repo, detectionError};
    }
    Result<QVector<Change>> status(const Repository &, const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        return {changes, {}};
    }
    Result<QVector<Commit>> log(const Repository &, const LogQuery &query,
                                const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        logs.append(query);
        if (query.limit == 1) {
            Commit resolved;
            resolved.id = "resolved:" + query.revision;
            return {{resolved}, {}};
        }
        return {commits.mid(query.skip, query.limit), {}};
    }
    Result<QVector<Change>> diff(const Repository &repo, const Source &left,
                                 const Source &right, const std::atomic_bool *cancel) const override
    {
        recordThread();
        QVector<Change> result;
        Error error;
        {
            QMutexLocker lock(&mutex);
            diffs.append({repo.root, left, right});
            result = changes;
            error = diffError;
        }
        if (blockNextDiff.exchange(false)) {
            blockedEntered.store(true);
            QElapsedTimer timer;
            timer.start();
            while (!releaseBlocked.load() && timer.elapsed() < 5000) {
                if (cancel && cancel->load()) cancellationObserved.store(true);
                QThread::msleep(2);
            }
            blockedExited.store(true);
        }
        return {result, error};
    }
    Result<FileContent> catFile(const Repository &, const QString &path, const Source &source,
                                const std::atomic_bool *) const override
    {
        recordThread();
        QMutexLocker lock(&mutex);
        contents.append({path, source});
        FileContent content;
        content.exists = source.kind != SourceKind::Empty;
        content.bytes = payload(path, source);
        content.label = sourceLabel(source) + ":" + path;
        content.objectId = "fake-object";
        return {content, {}};
    }
    Result<QVector<Reference>> references(const Repository &, const std::atomic_bool *) const override
    { recordThread(); return {}; }
    Result<QVector<BlameLine>> blame(const Repository &, const QString &, const QString &,
                                    const std::atomic_bool *) const override
    { recordThread(); return {}; }

    static QByteArray payload(const QString &path, const Source &source)
    {
        if (source.kind == SourceKind::Empty) return {};
        return QString("kind=%1;revision=%2;path=%3\n")
                .arg(int(source.kind)).arg(source.revision, path).toUtf8();
    }
    void setChanges(QVector<Change> value)
    { QMutexLocker lock(&mutex); changes = std::move(value); }
    void setCommits(QVector<Commit> value)
    { QMutexLocker lock(&mutex); commits = std::move(value); }
    void setHead(bool value)
    { QMutexLocker lock(&mutex); hasHead = value; }
    void setRoot(const QString &value)
    { QMutexLocker lock(&mutex); fixedRoot = value; }
    void setUnavailable(Error value)
    { QMutexLocker lock(&mutex); unavailable = std::move(value); }
    void setDetectionError(Error value)
    { QMutexLocker lock(&mutex); detectionError = std::move(value); }
    QVector<DiffCall> diffCalls() const
    { QMutexLocker lock(&mutex); return diffs; }
    QVector<LogQuery> logCalls() const
    { QMutexLocker lock(&mutex); return logs; }
    QVector<ContentCall> contentCalls() const
    { QMutexLocker lock(&mutex); return contents; }
    int detectCalls() const
    { QMutexLocker lock(&mutex); return detectCount; }

    mutable std::atomic_bool calledOnGui{false};
    mutable std::atomic_bool blockNextDiff{false};
    mutable std::atomic_bool releaseBlocked{false};
    mutable std::atomic_bool blockedEntered{false};
    mutable std::atomic_bool blockedExited{false};
    mutable std::atomic_bool cancellationObserved{false};

private:
    void recordThread() const
    { if (QThread::currentThread() == guiThread) calledOnGui.store(true); }
    QThread *const guiThread;
    mutable QMutex mutex;
    bool hasHead = true;
    QString fixedRoot;
    Error unavailable, detectionError, diffError;
    QVector<Change> changes;
    QVector<Commit> commits;
    mutable int detectCount = 0;
    mutable QVector<DiffCall> diffs;
    mutable QVector<LogQuery> logs;
    mutable QVector<ContentCall> contents;
};

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return "<unreadable fixture>";
    return file.readAll();
}
Change change(const QString &status, const QString &path = "file.txt", const QString &oldPath = {})
{
    Change result;
    result.status = status;
    result.path = path;
    result.oldPath = oldPath;
    return result;
}
Commit commit(const QString &id, const QStringList &parents = {})
{
    Commit result;
    result.id = id;
    result.parents = parents;
    result.author = "Fixture Author";
    result.email = "fixture@example.invalid";
    result.subject = "Fixture subject " + id;
    result.message = result.subject + "\n\nFull commit message.";
    result.date = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);
    return result;
}
}

class VcsViewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    { qRegisterMetaType<LqCompare::Vcs::Comparison>(); }

    void headAndIndexUseCorrectSourcesOffGuiThread()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change("M")});
        VcsView view("/virtual/repository", backend);
        auto *changes = view.findChild<QTreeWidget *>("vcsChanges");
        QVERIFY(changes);
        QTRY_COMPARE(changes->topLevelItemCount(), 1);
        QTRY_VERIFY(!view.isBusy());
        const auto head = backend->diffCalls().last();
        QCOMPARE(int(head.left.kind), int(SourceKind::Revision));
        QCOMPARE(head.left.revision, QString(40, 'a'));
        QCOMPARE(int(head.right.kind), int(SourceKind::WorkingTree));
        QCOMPARE(changes->topLevelItem(0)->text(2), QString("file.txt"));
        const int before = backend->diffCalls().size();
        view.setMode(VcsView::Mode::Index);
        QTRY_VERIFY(backend->diffCalls().size() > before);
        QTRY_VERIFY(!view.isBusy());
        const auto index = backend->diffCalls().last();
        QCOMPARE(int(index.left.kind), int(SourceKind::Index));
        QCOMPARE(int(index.right.kind), int(SourceKind::WorkingTree));
        QVERIFY(!backend->calledOnGui.load());
    }

    void missingGitDisablesVcsControls()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setUnavailable({ErrorCode::Unavailable, "未检测到 git 可执行文件", {}});
        VcsView view("/virtual/repository", backend);
        auto *status = view.findChild<QLabel *>("vcsStatus");
        auto *mode = view.findChild<QComboBox *>("vcsMode");
        auto *compare = view.findChild<QPushButton *>("vcsCompare");
        auto *commits = view.findChild<QTreeWidget *>("vcsCommits");
        QVERIFY(status && mode && compare && commits);
        QTRY_VERIFY(status->text().contains("未检测到 git"));
        QVERIFY(!mode->isEnabled());
        QVERIFY(!compare->isEnabled());
        QVERIFY(!commits->isEnabled());
        QVERIFY(backend->diffCalls().isEmpty());
        QVERIFY(backend->logCalls().isEmpty());
        QCOMPARE(backend->detectCalls(), 0);
        QVERIFY(!backend->calledOnGui.load());
    }

    void unbornHeadReportsErrorButIndexRemainsUsable()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setHead(false);
        backend->setChanges({change("??")});
        VcsView view("/virtual/unborn", backend);
        QSignalSpy errors(&view, &VcsView::errorOccurred);
        QTRY_VERIFY(!errors.isEmpty());
        QVERIFY(errors.last().at(0).toString().contains("HEAD"));
        QVERIFY(backend->diffCalls().isEmpty());
        view.setMode(VcsView::Mode::Index);
        QTRY_VERIFY(!backend->diffCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(view.findChild<QTreeWidget *>("vcsChanges")->topLevelItemCount(), 1);
    }

    void detectionFailureIsVisible()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setDetectionError({ErrorCode::NotRepository, "路径不属于 Git 仓库", "fixture detail"});
        VcsView view("/virtual/not-a-repository", backend);
        QSignalSpy errors(&view, &VcsView::errorOccurred);
        QTRY_VERIFY(!errors.isEmpty());
        QVERIFY(errors.last().at(0).toString().contains("路径不属于 Git 仓库"));
        QVERIFY(!view.findChild<QPushButton *>("vcsCompare")->isEnabled());
        QVERIFY(backend->diffCalls().isEmpty());
    }

    // VCS-001 第 4 条在生产路径上的两半：切模式复用探测结论，显式刷新丢掉它。
    // 两半必须一起测：只测前半句的话，一个「永远不失效」的缓存同样能过，
    // 而它的后果是用户在刚 git init 的目录里点刷新也永远看不到仓库。
    void repositoryDetectionIsCachedAcrossModeSwitchesButNotAcrossExplicitRefresh()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change("M")});
        VcsView view("/virtual/repository", backend);
        // 等首轮加载**真的跑完**：`isBusy()` 在工作还没被排上之前就是 false，
        // 拿它当判据会在第一帧就通过，于是后面的计数断言全部落在 0 上（本轮实测踩到）。
        QTRY_COMPARE(backend->detectCalls(), 1);
        const int headDiffs = backend->diffCalls().size();

        view.setMode(VcsView::Mode::Index);
        QTRY_COMPARE(backend->diffCalls().size(), headDiffs + 1);
        view.setMode(VcsView::Mode::History);
        QTRY_COMPARE(backend->logCalls().size(), 1);
        view.setMode(VcsView::Mode::Head);
        QTRY_COMPARE(backend->diffCalls().size(), headDiffs + 2);
        // 同一个路径切了三次模式，探测仍然只有开头那一次。
        QCOMPARE(backend->detectCalls(), 1);

        // 显式刷新必须真的重新探测：这是用户唯一的「我不信上一次结论」的入口。
        view.refresh();
        QTRY_COMPARE(backend->detectCalls(), 2);
        QVERIFY(!backend->calledOnGui.load());
    }

    void pathScopeIncludesRenamesAndDeletedFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkdir("sub"));
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setRoot(directory.path());
        backend->setChanges({change("M", "sub/file.txt"), change("D", "sub/deleted.txt"),
                             change("M", "sub/trailing space.txt "),
                             change("R100", "elsewhere/moved.txt", "sub/old.txt"),
                             change("M", "sub-other/outside.txt")});
        VcsView view(directory.filePath("sub"), backend);
        auto *changes = view.findChild<QTreeWidget *>("vcsChanges");
        QTRY_COMPARE(changes->topLevelItemCount(), 4);
        QTRY_VERIFY(!view.isBusy());
        QStringList paths;
        for (int i = 0; i < changes->topLevelItemCount(); ++i)
            paths.append(changes->topLevelItem(i)->text(2));
        QVERIFY(paths.contains("sub/file.txt"));
        QVERIFY(paths.contains("sub/deleted.txt"));
        QVERIFY(paths.contains("elsewhere/moved.txt"));
        QVERIFY(!paths.contains("sub-other/outside.txt"));
        view.setPath(directory.filePath("sub/deleted.txt"));
        QTRY_COMPARE(changes->topLevelItemCount(), 1);
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(changes->topLevelItem(0)->text(2), QString("sub/deleted.txt"));
        QCOMPARE(backend->detectCalls(), 2);
        QCOMPARE(backend->diffCalls().last().root, directory.path());
        view.setPath(directory.filePath("sub/trailing space.txt "));
        QTRY_VERIFY(backend->detectCalls() == 3);
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(changes->topLevelItemCount(), 1);
        QCOMPARE(changes->topLevelItem(0)->text(2), QString("sub/trailing space.txt "));
        QCOMPARE(view.path(), directory.filePath("sub/trailing space.txt "));
    }

    void revisionsPreserveRenameAndSnapshotLifetime()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change("R100", "new name.txt", "old name.txt")});
        QScopedPointer<VcsView> view(new VcsView("/virtual/repository", backend));
        view->setMode(VcsView::Mode::Revisions);
        auto *left = view->findChild<QLineEdit *>("vcsLeftRevision");
        auto *right = view->findChild<QLineEdit *>("vcsRightRevision");
        auto *refresh = view->findChild<QPushButton *>("vcsRefresh");
        auto *compare = view->findChild<QPushButton *>("vcsCompare");
        auto *changes = view->findChild<QTreeWidget *>("vcsChanges");
        QVERIFY(left && right && refresh && compare && changes);
        QTRY_VERIFY(!backend->diffCalls().isEmpty());
        QTRY_VERIFY(!view->isBusy());
        left->setText("release/v1");
        right->setText("feature^2");
        const int before = backend->diffCalls().size();
        QTest::mouseClick(refresh, Qt::LeftButton);
        QTRY_VERIFY(backend->diffCalls().size() > before);
        QTRY_VERIFY(!view->isBusy());
        QCOMPARE(backend->diffCalls().last().left.revision, QString("resolved:release/v1"));
        QCOMPARE(backend->diffCalls().last().right.revision, QString("resolved:feature^2"));
        QCOMPARE(changes->topLevelItemCount(), 1);
        QCOMPARE(changes->topLevelItem(0)->text(1), QString("old name.txt"));
        QCOMPARE(changes->topLevelItem(0)->text(2), QString("new name.txt"));
        changes->setCurrentItem(changes->topLevelItem(0));
        QSignalSpy comparisons(view.data(), &VcsView::compareRequested);
        QTest::mouseClick(compare, Qt::LeftButton);
        QTRY_COMPARE(comparisons.size(), 1);
        Comparison snapshot = qvariant_cast<Comparison>(comparisons.takeFirst().at(0));
        const QString leftPath = snapshot.leftPath, rightPath = snapshot.rightPath;
        QVERIFY(snapshot.lifetime);
        QVERIFY(leftPath != rightPath);
        QCOMPARE(readFile(leftPath), FakeBackend::payload("old name.txt", Source::at("resolved:release/v1")));
        QCOMPARE(readFile(rightPath), FakeBackend::payload("new name.txt", Source::at("resolved:feature^2")));
        const auto calls = backend->contentCalls();
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.at(0).path, QString("old name.txt"));
        QCOMPARE(calls.at(1).path, QString("new name.txt"));
        const QFile::Permissions writeBits = QFile::WriteOwner | QFile::WriteGroup | QFile::WriteOther;
        QVERIFY(!(QFileInfo(leftPath).permissions() & writeBits));
        QVERIFY(!(QFileInfo(rightPath).permissions() & writeBits));
        view.reset();
        QVERIFY(QFileInfo::exists(leftPath));
        QVERIFY(QFileInfo::exists(rightPath));
        snapshot = {};
        QTRY_VERIFY(!QFileInfo::exists(leftPath));
        QTRY_VERIFY(!QFileInfo::exists(rightPath));
        QVERIFY(!backend->calledOnGui.load());
    }

    void additionsAndDeletionsUseEmptySide_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<bool>("emptyLeft");
        QTest::newRow("added") << QString("A") << true;
        QTest::newRow("untracked") << QString("??") << true;
        QTest::newRow("deleted") << QString("D") << false;
    }
    void additionsAndDeletionsUseEmptySide()
    {
        QFETCH(QString, status);
        QFETCH(bool, emptyLeft);
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change(status)});
        VcsView view("/virtual/repository", backend);
        auto *changes = view.findChild<QTreeWidget *>("vcsChanges");
        QTRY_COMPARE(changes->topLevelItemCount(), 1);
        QTRY_VERIFY(!view.isBusy());
        changes->setCurrentItem(changes->topLevelItem(0));
        QSignalSpy comparisons(&view, &VcsView::compareRequested);
        QTest::mouseClick(view.findChild<QPushButton *>("vcsCompare"), Qt::LeftButton);
        QTRY_COMPARE(comparisons.size(), 1);
        const Comparison comparison = qvariant_cast<Comparison>(comparisons.first().at(0));
        QCOMPARE(readFile(emptyLeft ? comparison.leftPath : comparison.rightPath), QByteArray());
        QCOMPARE(readFile(emptyLeft ? comparison.rightPath : comparison.leftPath),
                 FakeBackend::payload("file.txt", emptyLeft ? Source::workingTree() : Source::at(QString(40, 'a'))));
    }

    void historyUsesFirstParentOrEmptyRoot_data()
    {
        QTest::addColumn<QStringList>("parents");
        QTest::newRow("merge-commit") << QStringList({"first-parent", "second-parent"});
        QTest::newRow("root-commit") << QStringList();
    }
    void historyUsesFirstParentOrEmptyRoot()
    {
        QFETCH(QStringList, parents);
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change(parents.isEmpty() ? "A" : "M")});
        backend->setCommits({commit("selected-commit", parents)});
        VcsView view("/virtual/repository", backend);
        view.setMode(VcsView::Mode::History);
        QTRY_VERIFY(!backend->diffCalls().isEmpty());
        QTRY_VERIFY(!view.isBusy());
        const DiffCall diff = backend->diffCalls().last();
        QCOMPARE(int(diff.left.kind), int(parents.isEmpty() ? SourceKind::Empty : SourceKind::Revision));
        QCOMPARE(diff.left.revision, parents.value(0));
        QCOMPARE(diff.right.revision, QString("selected-commit"));
        QCOMPARE(int(diff.right.kind), int(SourceKind::Revision));
        auto *commits = view.findChild<QTreeWidget *>("vcsCommits");
        auto *details = view.findChild<QPlainTextEdit *>("vcsCommitDetails");
        QVERIFY(commits && details);
        QCOMPARE(commits->topLevelItemCount(), 1);
        QCOMPARE(commits->currentItem(), commits->topLevelItem(0));
        QVERIFY(details->isReadOnly());
        QVERIFY(details->toPlainText().contains("Full commit message."));
        QVERIFY(details->toPlainText().contains("fixture@example.invalid"));
        auto *changes = view.findChild<QTreeWidget *>("vcsChanges");
        changes->setCurrentItem(changes->topLevelItem(0));
        QSignalSpy comparisons(&view, &VcsView::compareRequested);
        QVERIFY(QMetaObject::invokeMethod(changes, "itemDoubleClicked", Qt::DirectConnection,
                                          Q_ARG(QTreeWidgetItem *, changes->topLevelItem(0)), Q_ARG(int, 0)));
        QTRY_COMPARE(comparisons.size(), 1);
        const Comparison comparison = qvariant_cast<Comparison>(comparisons.first().at(0));
        QCOMPARE(readFile(comparison.leftPath), FakeBackend::payload("file.txt", diff.left));
        QCOMPARE(readFile(comparison.rightPath), FakeBackend::payload("file.txt", diff.right));
        QVERIFY(!backend->calledOnGui.load());
    }

    void historyPagesWithoutChangingSelectedCommit()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        QVector<Commit> commits;
        for (int i = 0; i < 105; ++i)
            commits.append(commit(QString("commit-%1").arg(i), {QString("parent-%1").arg(i)}));
        backend->setCommits(commits);
        backend->setChanges({change("M")});
        VcsView view("/virtual/repository", backend);
        view.setMode(VcsView::Mode::History);
        auto *list = view.findChild<QTreeWidget *>("vcsCommits");
        auto *more = view.findChild<QPushButton *>("vcsLoadMore");
        QVERIFY(list && more);
        QTRY_COMPARE(list->topLevelItemCount(), 100);
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(backend->logCalls().size(), 1);
        QCOMPARE(backend->logCalls().first().skip, 0);
        QCOMPARE(backend->logCalls().first().limit, 100);
        QCOMPARE(backend->diffCalls().size(), 1);
        QTreeWidgetItem *selected = list->currentItem();
        QVERIFY(selected);
        QVERIFY(more->isEnabled());
        QTest::mouseClick(more, Qt::LeftButton);
        QTRY_COMPARE(list->topLevelItemCount(), 105);
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(backend->logCalls().size(), 2);
        QCOMPARE(backend->logCalls().last().skip, 100);
        QCOMPARE(backend->logCalls().last().limit, 100);
        QCOMPARE(list->currentItem(), selected);
        QCOMPARE(backend->diffCalls().size(), 1);
        QVERIFY(!more->isEnabled());
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_VCS_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            view.resize(1200, 760);
            view.show();
            QTest::qWait(50);
            QVERIFY(view.grab().save(screenshot));
        }
    }

    void cancelAndRefreshIgnoreLateResults()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change("M", "stale.txt")});
        backend->blockNextDiff.store(true);
        VcsView view("/virtual/old-repository", backend);
        QSignalSpy errors(&view, &VcsView::errorOccurred);
        QTRY_VERIFY(backend->blockedEntered.load());
        QVERIFY(view.isBusy());
        view.cancel();
        QVERIFY(!view.isBusy());
        QTRY_VERIFY(backend->cancellationObserved.load());
        backend->setChanges({change("M", "fresh.txt")});
        view.setPath("/virtual/new-repository");
        auto *changes = view.findChild<QTreeWidget *>("vcsChanges");
        QTRY_COMPARE(changes->topLevelItemCount(), 1);
        QTRY_COMPARE(changes->topLevelItem(0)->text(2), QString("fresh.txt"));
        QTRY_VERIFY(!view.isBusy());
        QCOMPARE(backend->diffCalls().last().root, QString("/virtual/new-repository"));
        backend->releaseBlocked.store(true);
        QTRY_VERIFY(backend->blockedExited.load());
        QTest::qWait(50); // Allow the old worker's queued completion to be delivered.
        QCOMPARE(changes->topLevelItemCount(), 1);
        QCOMPARE(changes->topLevelItem(0)->text(2), QString("fresh.txt"));
        QVERIFY(errors.isEmpty());
    }

    void destroyingViewCancelsOutstandingWorker()
    {
        auto backend = QSharedPointer<FakeBackend>::create();
        backend->setChanges({change("M")});
        backend->blockNextDiff.store(true);
        QScopedPointer<VcsView> view(new VcsView("/virtual/repository", backend));
        QTRY_VERIFY(backend->blockedEntered.load());
        view.reset();
        QTRY_VERIFY(backend->cancellationObserved.load());
        backend->releaseBlocked.store(true);
        QTRY_VERIFY(backend->blockedExited.load());
        QTest::qWait(50); // A queued completion must not dereference the deleted widget.
    }
};

QTEST_MAIN(VcsViewTests)
#include "tst_vcsview.moc"
