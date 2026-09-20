#include <QtTest>

#include "foldercompare.h"
#include "foldercomparesession.h"
#include "foldercompareview.h"
#include "sessiondocument.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeView>

using namespace LqCompare;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

const Folder::Entry *findEntry(const Folder::Result &result, const QString &path)
{
    for (const auto &entry : result.entries) {
        if (entry.relativePath == path)
            return &entry;
    }
    return nullptr;
}

struct Pair
{
    QTemporaryDir temp;
    QString left = temp.path() + QStringLiteral("/left");
    QString right = temp.path() + QStringLiteral("/right");
    Pair() { QDir().mkpath(left); QDir().mkpath(right); }
};

// Keep real files for content I/O while injecting metadata/enumeration failures.
class FaultFileSystem : public Files::FileSystem
{
public:
    std::unique_ptr<Files::FileSystem> native{Files::createNativeFileSystem()};
    QString unreadableDirectory;
    QString unreadableFile;
    QString changingFile;
    QHash<QString, QString> aliases;
    mutable int fileStats = 0;
    Qt::CaseSensitivity caseSensitivity() const override { return native->caseSensitivity(); }
    QChar separator() const override { return native->separator(); }
    QString pathNormalize(const QString &p, Files::ErrorCode *e) const override { return native->pathNormalize(p, e); }
    bool isAbsolutePath(const QString &p) const override { return native->isAbsolutePath(p); }
    QString toNativePath(const QString &p) const override { return native->toNativePath(p); }
    Files::FileInfo stat(const QString &p, Files::ErrorCode *e) const override
    {
        if (p == unreadableFile) {
            if (e) *e = Files::FileSystemError::PermissionDenied;
            return {};
        }
        if (p == changingFile && ++fileStats == 2)
            writeFile(p, QByteArray("changed while comparing"));
        auto info = native->stat(p, e);
        if (aliases.contains(p)) info.name = aliases.value(p);
        return info;
    }
    QString linkTarget(const QString &p, Files::ErrorCode *e) const override { return native->linkTarget(p, e); }
    bool exists(const QString &p, Files::ErrorCode *e) const override { return native->exists(p, e); }
    QVector<Files::FileInfo> enumerateDirectory(const QString &p, Files::ErrorCode *e) const override
    {
        if (p == unreadableDirectory) {
            if (e) *e = Files::FileSystemError::PermissionDenied;
            return {};
        }
        auto entries = native->enumerateDirectory(p, e);
        for (auto &entry : entries) {
            if (aliases.contains(entry.path)) entry.name = aliases.value(entry.path);
        }
        return entries;
    }
    bool setTimes(const QString &p, const Files::FileTime &m, const Files::FileTime &a,
                  Files::ErrorCode *e) const override { return native->setTimes(p, m, a, e); }
    bool setAttributes(const QString &p, Files::FileAttributes a, Files::ErrorCode *e) const override
    { return native->setAttributes(p, a, e); }
    QString platformName() const override { return native->platformName(); }
};

QModelIndex indexNamed(QAbstractItemModel *model, const QString &name, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data().toString() == name)
            return index;
        const auto child = indexNamed(model, name, index);
        if (child.isValid())
            return child;
    }
    return {};
}

QVariantMap savedSettings(SessionSettings *settings)
{
    QVariantMap result;
    for (const QString &key : settings->keys())
        result.insert(key, settings->value(key));
    return result;
}

bool sameOptions(const Folder::Options &a, const Folder::Options &b)
{
    return a.recursive == b.recursive && a.compareContent == b.compareContent
        && a.maximumDepth == b.maximumDepth && a.scanMaskDeclaration == b.scanMaskDeclaration
        && a.nameCaseSensitivity == b.nameCaseSensitivity;
}

} // namespace

class FolderTests : public QObject
{
    Q_OBJECT
private slots:
    void recursivePairsAndStates()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/same.txt", "same\n"));
        QVERIFY(writeFile(pair.right + "/sub/same.txt", "same\n"));
        QVERIFY(writeFile(pair.left + "/sub/different.txt", "ABC"));
        QVERIFY(writeFile(pair.right + "/sub/different.txt", "ABD"));
        QVERIFY(writeFile(pair.left + "/left.txt", "left"));
        QVERIFY(writeFile(pair.right + "/right.txt", "right"));
        QVERIFY(writeFile(pair.left + "/conflict", "file"));
        QVERIFY(QDir().mkpath(pair.right + "/conflict"));
        const auto result = Folder::compare(pair.left, pair.right);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.complete);
        QCOMPARE(result.entries.size(), 6);
        QCOMPARE(findEntry(result, "sub/same.txt")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "sub/different.txt")->status, Folder::Status::Different);
        QCOMPARE(findEntry(result, "sub/different.txt")->firstDifference, qint64(2));
        QCOMPARE(findEntry(result, "sub")->status, Folder::Status::Different);
        QCOMPARE(findEntry(result, "left.txt")->status, Folder::Status::LeftOnly);
        QCOMPARE(findEntry(result, "right.txt")->status, Folder::Status::RightOnly);
        QCOMPARE(findEntry(result, "conflict")->status, Folder::Status::TypeConflict);
        QVERIFY(!findEntry(result, "conflict")->canCompareAsText());
        QVERIFY(findEntry(result, "left.txt")->canCompareAsText());
    }

    void exactBytesAcrossChunkBoundary()
    {
        Pair pair;
        QByteArray a(600000, 'a');
        QByteArray b = a;
        b[500000] = 'b';
        QVERIFY(writeFile(pair.left + "/large.bin", a));
        QVERIFY(writeFile(pair.right + "/large.bin", b));
        auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().firstDifference, qint64(500000));
        QVERIFY(writeFile(pair.right + "/large.bin", a));
        result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().status, Folder::Status::Same);
    }

    void emptyDirectoriesAndEmptyFiles()
    {
        Pair pair;
        QVERIFY(QDir().mkpath(pair.left + "/empty"));
        QVERIFY(QDir().mkpath(pair.right + "/empty"));
        QVERIFY(writeFile(pair.left + "/zero", {}));
        QVERIFY(writeFile(pair.right + "/zero", {}));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.size(), 2);
        QCOMPARE(findEntry(result, "empty")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "zero")->status, Folder::Status::Same);
    }

    void quickModeCannotProveEquality()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/equal-size", "aaa"));
        QVERIFY(writeFile(pair.right + "/equal-size", "bbb"));
        QVERIFY(writeFile(pair.left + "/different-size", "aaa"));
        QVERIFY(writeFile(pair.right + "/different-size", "longer"));
        Folder::Options options;
        options.compareContent = false;
        const auto result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(findEntry(result, "equal-size")->status, Folder::Status::Unknown);
        QCOMPARE(findEntry(result, "different-size")->status, Folder::Status::Different);
        QVERIFY(!result.complete);
    }

    void timestampsDoNotOverrideContent()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same", "same"));
        QVERIFY(writeFile(pair.right + "/same", "same"));
        std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
        Files::ErrorCode error;
        QVERIFY(fs->setTimes(pair.left + "/same", Files::FileTime::fromUnixTime(1234567890, 0), {}, &error));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().status, Folder::Status::Same);
        QVERIFY(result.entries.first().left.info.lastModified != result.entries.first().right.info.lastModified);
    }

    void recursionLimitIsExplicitUnknown()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/a/b/deep", "a"));
        QVERIFY(writeFile(pair.right + "/a/b/deep", "b"));
        Folder::Options options;
        options.recursive = false;
        auto result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.size(), 1);
        QCOMPARE(result.entries.first().status, Folder::Status::Unknown);
        options.recursive = true;
        options.maximumDepth = 1;
        result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.size(), 2);
        QCOMPARE(findEntry(result, "a/b")->status, Folder::Status::Unknown);
        QCOMPARE(findEntry(result, "a")->status, Folder::Status::Unknown);
        QVERIFY(!result.complete);
    }

    void linksAreComparedWithoutFollowing()
    {
#ifdef Q_OS_WIN
        QSKIP("Creating symbolic links on Windows requires a privileged test account.");
#else
        Pair pair;
        QVERIFY(QFile::link(pair.left, pair.left + "/cycle"));
        QVERIFY(QFile::link(pair.right, pair.right + "/cycle"));
        QVERIFY(QFile::link(pair.temp.path() + "/missing", pair.left + "/dangling"));
        QVERIFY(QFile::link(pair.temp.path() + "/missing", pair.right + "/dangling"));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.size(), 2);
        QCOMPARE(findEntry(result, "cycle")->left.kind, Folder::Kind::SymbolicLink);
        QCOMPARE(findEntry(result, "cycle")->status, Folder::Status::Different);
        QCOMPARE(findEntry(result, "dangling")->status, Folder::Status::Same);
        QVERIFY(!findEntry(result, "cycle")->canCompareAsText());
#endif
    }

    void cancellationRetainsCompletedEntries()
    {
        Pair pair;
        for (int i = 0; i < 20; ++i) {
            QVERIFY(writeFile(pair.left + QString("/%1.txt").arg(i), "same"));
            QVERIFY(writeFile(pair.right + QString("/%1.txt").arg(i), "same"));
        }
        std::atomic_bool cancelled{false};
        const auto result = Folder::compare(pair.left, pair.right, {}, &cancelled,
            [&cancelled](int count, const QString &) { if (count == 3) cancelled = true; });
        QVERIFY(result.cancelled);
        QVERIFY(!result.complete);
        QCOMPARE(result.entries.size(), 3);
        QCOMPARE(result.entries.first().status, Folder::Status::Same);
    }

    void inaccessibleDirectoryDoesNotCreateFalseOrphans()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/file", "same"));
        QVERIFY(writeFile(pair.right + "/sub/file", "same"));
        FaultFileSystem fs;
        fs.unreadableDirectory = pair.right + "/sub";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "sub")->status, Folder::Status::Error);
        QCOMPARE(findEntry(result, "sub/file")->status, Folder::Status::Unknown);
        QVERIFY(!findEntry(result, "sub/file")->canCompareAsText());
        QVERIFY(!result.complete);
    }

    void inaccessibleMetadataIsError()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/file", "same"));
        QVERIFY(writeFile(pair.right + "/file", "same"));
        FaultFileSystem fs;
        fs.unreadableFile = pair.left + "/file";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(result.entries.first().status, Folder::Status::Error);
        QVERIFY(!result.entries.first().explanation.isEmpty());
    }

    void fileChangingDuringComparisonIsError()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/file", "same"));
        QVERIFY(writeFile(pair.right + "/file", "same"));
        FaultFileSystem fs;
        fs.changingFile = pair.left + "/file";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(result.entries.first().status, Folder::Status::Error);
        QVERIFY(result.entries.first().explanation.contains(QStringLiteral("变化")));
    }

    void deterministicOrderingAndHiddenFiles()
    {
        Pair pair;
        for (const QString &name : {QStringLiteral("z.txt"), QStringLiteral(".hidden"), QStringLiteral("中文.txt")}) {
            QVERIFY(writeFile(pair.left + "/" + name, "same"));
            QVERIFY(writeFile(pair.right + "/" + name, "same"));
        }
        const auto a = Folder::compare(pair.left, pair.right);
        const auto b = Folder::compare(pair.left, pair.right);
        QCOMPARE(a.entries.size(), 3);
        for (int i = 0; i < a.entries.size(); ++i)
            QCOMPARE(a.entries.at(i).relativePath, b.entries.at(i).relativePath);
        QCOMPARE(a.entries.first().relativePath, QStringLiteral(".hidden"));
    }

    void invalidAndOverlappingRoots()
    {
        Pair pair;
        auto result = Folder::compare(pair.left, pair.temp.path() + "/missing");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.complete);
        result = Folder::compare(pair.left, pair.left);
        QCOMPARE(result.warnings.size(), 1);
        QVERIFY(QDir().mkpath(pair.left + "/child"));
        result = Folder::compare(pair.left, pair.left + "/child");
        QCOMPARE(result.warnings.size(), 1);
        QVERIFY(result.entries.size() < 5);
    }

    void sessionViewFilteringRefreshAndActivation()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/same", "same"));
        QVERIFY(writeFile(pair.right + "/sub/same", "same"));
        QVERIFY(writeFile(pair.left + "/different", "aaa"));
        QVERIFY(writeFile(pair.right + "/different", "bbb"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QSignalSpy activated(&session, &FolderCompareSession::compareFilesRequested);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(session.state(), CompareSession::State::Open);
        QVERIFY(!session.canSave());
        auto *view = session.view();
        QVERIFY(view);
        widget->resize(1380, 650);
        widget->show();
        QTest::qWait(20);
        const QString capture = qEnvironmentVariable("LQCOMPARE_FOLDER_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(widget->grab().save(capture));
        auto *tree = view->leftTree();
        auto *model = tree->model();
        QModelIndex sub = indexNamed(model, "sub");
        QVERIFY(sub.isValid());
        tree->expand(sub);
        QVERIFY(view->rightTree()->isExpanded(sub));
        QModelIndex same = indexNamed(model, "same");
        QVERIFY(same.isValid());
        QVERIFY(QMetaObject::invokeMethod(tree, "activated", Q_ARG(QModelIndex, same)));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), pair.left + "/sub/same");
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        QVERIFY(filter);
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Different)));
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("different"));
        filter->setCurrentIndex(0);
        tree->expand(indexNamed(model, "sub"));
        QVERIFY(writeFile(pair.right + "/different", "aaa"));
        QVERIFY(session.reload());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QCOMPARE(findEntry(session.result(), "different")->status, Folder::Status::Same);
        QVERIFY(tree->isExpanded(indexNamed(model, "sub")));
        session.close();
        QVERIFY(!view->isEnabled());
    }

    void unsubmittedPathEditsDoNotChangeSessionSource()
    {
        Pair pair;
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        auto *edit = widget->findChild<QLineEdit *>(QStringLiteral("folderLeftPath"));
        edit->setText(pair.temp.path());
        QCOMPARE(session.leftPath(), pair.left);
        QSignalSpy paths(&session, &FolderCompareSession::pathsChanged);
        session.setPaths(pair.temp.path(), pair.right);
        QCOMPARE(paths.count(), 1);
        QCOMPARE(session.leftPath(), pair.temp.path());
    }

    void scannerRejectsConcurrentStartsAndClosesCleanly()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/file", "same"));
        QVERIFY(writeFile(pair.right + "/file", "same"));
        Folder::Scanner scanner;
        QSignalSpy finished(&scanner, &Folder::Scanner::finished);
        QVERIFY(scanner.start(pair.left, pair.right));
        QVERIFY(!scanner.start(pair.left, pair.right));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!scanner.isRunning());
        FolderCompareSession session(pair.left, pair.right);
        QSignalSpy sessionFinished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        session.close();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isScanning(), 5000);
        QCOMPARE(sessionFinished.count(), 0);
    }

    void scanMasksNeverPruneIncludedDescendants()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/src/nested/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/nested/keep.txt", "same"));
        QVERIFY(writeFile(pair.left + "/src/nested/ignored.bin", "aaa"));
        QVERIFY(writeFile(pair.right + "/src/nested/ignored.bin", "bbb"));
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("src/nested/keep.txt\n-src\n-src/nested");
        const auto result = Folder::compare(pair.left, pair.right, options);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.complete);
        QCOMPARE(result.entries.size(), 4);
        QCOMPARE(result.excludedCount, 1);
        const auto *src = findEntry(result, "src");
        QVERIFY(src->excludedByMask);
        QVERIFY(src->hasIncludedDescendants);
        QVERIFY(src->inComparison());
        QCOMPARE(src->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "src/nested/keep.txt")->status, Folder::Status::Same);
        const auto *ignored = findEntry(result, "src/nested/ignored.bin");
        QVERIFY(!ignored->inComparison());
        QCOMPARE(ignored->status, Folder::Status::Unknown);
        QCOMPARE(ignored->firstDifference, qint64(-1));
        QVERIFY(!ignored->canCompareAsText());
        QVERIFY(!ignored->filterReason.isEmpty());
        options.maximumDepth = 0;
        const auto limited = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(limited.entries.size(), 1);
        QVERIFY(findEntry(limited, "src")->inComparison());
        QCOMPARE(findEntry(limited, "src")->status, Folder::Status::Unknown);
        QVERIFY(!limited.complete);
    }

    void maskExclusionPriorityAndInvalidInput()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/keep.txt", "same"));
        QVERIFY(writeFile(pair.left + "/ignored.txt", "aaa"));
        QVERIFY(writeFile(pair.right + "/ignored.txt", "bbb"));
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("*.txt\n-ignored.txt");
        auto result = Folder::compare(pair.left, pair.right, options);
        QVERIFY(findEntry(result, "ignored.txt")->excludedByMask);
        QCOMPARE(findEntry(result, "keep.txt")->status, Folder::Status::Same);
        options.scanMaskDeclaration = QStringLiteral("[");
        result = Folder::compare(pair.left, pair.right, options);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.complete);
        QVERIFY(result.entries.isEmpty());
    }

    void maskedUnreadableDirectoryStaysVisibleAsError()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/src/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/keep.txt", "same"));
        FaultFileSystem fs;
        fs.unreadableDirectory = pair.right + "/src";
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("*.txt\n-src");
        const auto result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "src")->status, Folder::Status::Error);
        QVERIFY(findEntry(result, "src")->inComparison());
        QVERIFY(!result.complete);
        FolderCompareView view;
        view.setResult(result);
        QVERIFY(indexNamed(view.leftTree()->model(), "src").isValid());
        fs.unreadableDirectory.clear();
        fs.unreadableFile = pair.right + "/src";
        const auto metadataFailure = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(findEntry(metadataFailure, "src")->status, Folder::Status::Error);
        QVERIFY(findEntry(metadataFailure, "src")->inComparison());
        QVERIFY(!metadataFailure.complete);
    }

    void caseInsensitivePairsPreserveActualPaths()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/SRC/ReadMe.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/readme.txt", "same"));
        auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(findEntry(result, "SRC")->status, Folder::Status::LeftOnly);
        QCOMPARE(findEntry(result, "src")->status, Folder::Status::RightOnly);
        Folder::Options options;
        options.nameCaseSensitivity = Qt::CaseInsensitive;
        options.scanMaskDeclaration = QStringLiteral("SRC/README.TXT");
        result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.size(), 2);
        const auto *file = findEntry(result, "SRC/ReadMe.txt");
        QVERIFY(file);
        QCOMPARE(file->status, Folder::Status::Same);
        QVERIFY(file->nameCaseDifference);
        QCOMPARE(file->left.info.path, pair.left + "/SRC/ReadMe.txt");
        QCOMPARE(file->right.info.path, pair.right + "/src/readme.txt");
        QVERIFY(findEntry(result, "SRC")->hasIncludedDescendants);
    }

    void ambiguousCaseFoldNamesNeverGuessOrLoseRows()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/lower-fixture", "same"));
        QVERIFY(writeFile(pair.left + "/upper-fixture", "other"));
        QVERIFY(writeFile(pair.right + "/other-fixture", "same"));
        // Simulate names from a case-sensitive volume even when the machine's
        // temporary directory is on a case-insensitive filesystem.
        FaultFileSystem fs;
        fs.aliases.insert(pair.left + "/lower-fixture", "name.txt");
        fs.aliases.insert(pair.left + "/upper-fixture", "NAME.txt");
        fs.aliases.insert(pair.right + "/other-fixture", "name.txt");
        Folder::Options options;
        options.nameCaseSensitivity = Qt::CaseInsensitive;
        auto result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(result.entries.size(), 2);
        for (const auto &entry : result.entries) {
            QCOMPARE(entry.status, Folder::Status::Error);
            QVERIFY(entry.explanation.contains(QStringLiteral("歧义")));
            QVERIFY(!entry.canCompareAsText());
        }
        QVERIFY(!result.complete);
        options.nameCaseSensitivity = Qt::CaseSensitive;
        result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "NAME.txt")->status, Folder::Status::LeftOnly);
        QCOMPARE(findEntry(result, "name.txt")->status, Folder::Status::Same);
    }

    void displayFiltersDoNotChangeComparisonScope()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/src/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/keep.txt", "same"));
        QVERIFY(writeFile(pair.left + "/src/ignored.bin", "aaa"));
        QVERIFY(writeFile(pair.right + "/src/ignored.bin", "bbb"));
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("*.txt");
        FolderCompareSession session(pair.left, pair.right);
        QVERIFY(session.setComparisonOptions(options));
        std::unique_ptr<QWidget> widget(session.createWidget());
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        auto *view = session.view();
        auto *model = view->leftTree()->model();
        QVERIFY(indexNamed(model, "src").isValid());
        QVERIFY(indexNamed(model, "keep.txt").isValid());
        QVERIFY(!indexNamed(model, "ignored.bin").isValid());
        QCOMPARE(session.result().entries.size(), 3);
        view->resetDisplayFilters();
        QVERIFY(indexNamed(model, "ignored.bin").isValid());
        QCOMPARE(session.result().entries.size(), 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(view->options().scanMaskDeclaration, options.scanMaskDeclaration);
        QCOMPARE(view->visibleDifferenceCount(), 0); // Excluded content isn't a difference.
        QVERIFY(widget->findChild<QPushButton *>(QStringLiteral("folderRefresh"))->shortcut().isEmpty());
        view->findChild<QPlainTextEdit *>(QStringLiteral("folderScanMask"))->setPlainText(QStringLiteral("["));
        QString error;
        QVERIFY(!session.reload(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(session.result().entries.size(), 3);
    }

    void differenceNavigationAndSelectionRespectDisplayFilters()
    {
        Pair pair;
        for (const QString &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                   QStringLiteral("sub/c.txt"), QStringLiteral("sub/d.txt")}) {
            QVERIFY(writeFile(pair.left + "/" + name, "same"));
            QVERIFY(writeFile(pair.right + "/" + name,
                              name == "b.txt" || name == "sub/c.txt" ? "DIFF" : "same"));
        }
        QVERIFY(writeFile(pair.left + "/z.txt", "left only"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        widget->resize(1380, 650);
        widget->show();
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        auto *view = session.view();
        auto *tree = view->leftTree();
        tree->setFocus();
        QTest::qWait(10);
        QCOMPARE(view->visibleDifferenceCount(), 3);
        session.firstDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("b.txt"));
        session.nextDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("c.txt"));
        QVERIFY(tree->isExpanded(tree->currentIndex().parent()));
        session.nextDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("z.txt"));
        session.nextDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("z.txt"));
        session.previousDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("c.txt"));
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Different)));
        QCOMPARE(view->visibleDifferenceCount(), 2);
        view->rightTree()->setFocus();
        QTest::qWait(10);
        session.selectAllDifferences();
        QCOMPARE(view->rightTree()->selectionModel()->selectedRows().size(), 2);
        QCOMPARE(tree->selectionModel()->selectedRows().size(), 1); // Independent side selections.
        session.lastDifference();
        QCOMPARE(view->rightTree()->currentIndex().data().toString(), QStringLiteral("c.txt"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Same)));
        QCOMPARE(view->visibleDifferenceCount(), 0);
        session.nextDifference();
        QVERIFY(session.statusText().contains(QStringLiteral("没有差异")));
        QCOMPARE(finished.count(), 1);
    }

    void hideEmptyFoldersKeepsReadErrorsVisible()
    {
        Pair pair;
        for (const auto &root : {pair.left, pair.right}) {
            QVERIFY(QDir().mkpath(root + "/empty"));
            QVERIFY(QDir().mkpath(root + "/denied"));
        }
        FaultFileSystem fs;
        fs.unreadableDirectory = pair.right + "/denied";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        FolderCompareView view;
        view.setResult(result);
        view.findChild<QCheckBox *>(QStringLiteral("folderHideEmpty"))->setChecked(true);
        auto *model = view.leftTree()->model();
        QVERIFY(!indexNamed(model, "empty").isValid());
        QVERIFY(indexNamed(model, "denied").isValid());
    }

    void contextualErrorParentsAreNotNavigationStopsForSameFilter()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/error.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/error.txt", "same"));
        QVERIFY(writeFile(pair.left + "/sub/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/same.txt", "same"));
        FaultFileSystem fs;
        fs.unreadableFile = pair.right + "/sub/error.txt";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "sub")->status, Folder::Status::Error);
        FolderCompareView view;
        view.setResult(result);
        auto *filter = view.findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Same)));
        QVERIFY(indexNamed(view.leftTree()->model(), "sub").isValid());
        QVERIFY(indexNamed(view.leftTree()->model(), "same.txt").isValid());
        QCOMPARE(view.visibleDifferenceCount(), 0);
    }

    void savedFolderOptionsRoundTripThroughLqcAndActuallyScan()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/Report.TXT", "aaa"));
        QVERIFY(writeFile(pair.right + "/report.txt", "bbb"));
        QVERIFY(writeFile(pair.left + "/ignored.txt", "aaa"));
        QVERIFY(writeFile(pair.right + "/ignored.txt", "bbb"));
        QVERIFY(writeFile(pair.left + "/sub/deep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/deep.txt", "same"));
        Folder::Options chosen;
        chosen.scanMaskDeclaration = QStringLiteral("*.txt\n-ignored.txt");
        chosen.nameCaseSensitivity = Qt::CaseInsensitive;
        chosen.recursive = false;
        chosen.compareContent = false;
        chosen.maximumDepth = 7;

        FolderCompareSession original(pair.left, pair.right);
        std::unique_ptr<QWidget> originalWidget(original.createWidget());
        // Simulate the form being edited and applied through Compare/open.
        original.view()->setOptions(chosen);
        QSignalSpy originalFinished(&original, &FolderCompareSession::scanFinished);
        QVERIFY(original.open());
        QTRY_COMPARE_WITH_TIMEOUT(originalFinished.count(), 1, 5000);
        QVERIFY(sameOptions(original.comparisonOptions(), chosen));
        const QVariantMap values = savedSettings(original.sessionSettings());
        QCOMPARE(values.size(), 5);
        QCOMPARE(values.value(QStringLiteral("folder.scanMaskDeclaration")).toString(), chosen.scanMaskDeclaration);
        QCOMPARE(values.value(QStringLiteral("folder.nameCaseSensitivity")).toInt(), int(Qt::CaseInsensitive));

        original.view()->findChild<QCheckBox *>(QStringLiteral("folderHideEmpty"))->setChecked(true);
        original.view()->findChild<QCheckBox *>(QStringLiteral("folderHideExcluded"))->setChecked(false);
        auto *filter = original.view()->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Different)));
        QCOMPARE(savedSettings(original.sessionSettings()), values); // Display state is temporary.

        SessionDocument document;
        document.typeId = QStringLiteral("folder");
        document.leftPath = original.leftPath();
        document.rightPath = original.rightPath();
        document.settings = values;
        const QString filename = pair.temp.path() + QStringLiteral("/saved-folder.lqc");
        QString error;
        QVERIFY2(document.save(filename, &error), qPrintable(error));
        SessionDocument loaded;
        QVERIFY2(SessionDocument::load(filename, &loaded, &error), qPrintable(error));
        FolderCompareSession restored(loaded.leftPath, loaded.rightPath);
        std::unique_ptr<QWidget> restoredWidget(restored.createWidget());
        for (auto it = loaded.settings.cbegin(); it != loaded.settings.cend(); ++it)
            QVERIFY(restored.sessionSettings()->setValue(it.key(), it.value()));
        QVERIFY(!restored.isScanning());
        QVERIFY(sameOptions(restored.comparisonOptions(), chosen));
        QVERIFY(sameOptions(restored.view()->options(), chosen));
        QVERIFY(!restored.view()->findChild<QCheckBox *>(QStringLiteral("folderHideEmpty"))->isChecked());
        QVERIFY(restored.view()->findChild<QCheckBox *>(QStringLiteral("folderHideExcluded"))->isChecked());
        QCOMPARE(restored.view()->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"))->currentData().toInt(), -1);

        QSignalSpy finished(&restored, &FolderCompareSession::scanFinished);
        QVERIFY2(restored.open(&error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        const auto *report = findEntry(restored.result(), QStringLiteral("Report.TXT"));
        QVERIFY(report);
        QCOMPARE(report->right.info.path, pair.right + "/report.txt"); // Restored ignore-case pairing.
        QCOMPARE(report->status, Folder::Status::Unknown); // Restored content comparison disabled.
        QVERIFY(!findEntry(restored.result(), "ignored.txt")->inComparison()); // Restored mask.
        QVERIFY(!findEntry(restored.result(), "sub/deep.txt")); // Restored nonrecursive scope.

        // The opposite direction remains live after restore: settings changes
        // update the existing form and become effective at the next reload.
        QVERIFY(restored.sessionSettings()->setValue(QStringLiteral("folder.compareContent"), true));
        QVERIFY(restored.view()->options().compareContent);
        QVERIFY(restored.reload(&error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QCOMPARE(findEntry(restored.result(), "Report.TXT")->status, Folder::Status::Different);
    }

    void rejectedOptionsAtomicallyPreserveSettingsOptionsAndView()
    {
        FolderCompareSession session;
        Folder::Options accepted;
        accepted.scanMaskDeclaration = QStringLiteral("*.txt");
        accepted.nameCaseSensitivity = Qt::CaseInsensitive;
        accepted.maximumDepth = 9;
        QVERIFY(session.setComparisonOptions(accepted));
        std::unique_ptr<QWidget> widget(session.createWidget());
        const QVariantMap before = savedSettings(session.sessionSettings());
        QSignalSpy changed(session.sessionSettings(), &SessionSettings::changed);
        QVector<Folder::Options> invalid;
        Folder::Options next = accepted;
        next.recursive = false;
        next.compareContent = false;
        next.nameCaseSensitivity = Qt::CaseSensitive;
        next.scanMaskDeclaration = QStringLiteral("[");
        invalid.append(next);
        next = accepted;
        next.nameCaseSensitivity = static_cast<Qt::CaseSensitivity>(99);
        invalid.append(next);
        next = accepted;
        next.maximumDepth = -1;
        invalid.append(next);
        next.maximumDepth = 257;
        invalid.append(next);
        for (const auto &options : invalid) {
            QString error;
            QVERIFY(!session.setComparisonOptions(options, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(savedSettings(session.sessionSettings()), before);
            QVERIFY(sameOptions(session.comparisonOptions(), accepted));
            QVERIFY(sameOptions(session.view()->options(), accepted));
        }
        QCOMPARE(changed.count(), 0);
    }

    void invalidRestoredSettingsBlockScanUntilExplicitlyRepaired()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same", "same"));
        QVERIFY(writeFile(pair.right + "/same", "same"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        const auto previous = session.comparisonOptions();
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.nameCaseSensitivity"), 0.5));
        QVERIFY(sameOptions(session.comparisonOptions(), previous));
        QVERIFY(sameOptions(session.view()->options(), previous));
        QString error;
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(!session.open(&error));
        QVERIFY(error.contains(QStringLiteral("folder.nameCaseSensitivity")));
        QVERIFY(!session.isScanning());
        QCOMPARE(finished.count(), 0);
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.nameCaseSensitivity"), int(Qt::CaseSensitive)));
        QVERIFY2(session.open(&error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(session.result().entries.first().status, Folder::Status::Same);

        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.recursive"), QStringLiteral("false")));
        QVERIFY(!session.reload(&error));
        QVERIFY(error.contains(QStringLiteral("folder.recursive")));
        QVERIFY(sameOptions(session.comparisonOptions(), previous));
        QVERIFY(session.setComparisonOptions(previous, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(session.sessionSettings()->value(QStringLiteral("folder.recursive")).type(), QVariant::Bool);
        session.sessionSettings()->clear();
        QVERIFY(sameOptions(session.comparisonOptions(), Folder::Options()));
        QVERIFY(sameOptions(session.view()->options(), Folder::Options()));
    }
};

QTEST_MAIN(FolderTests)
#include "tst_folder.moc"
