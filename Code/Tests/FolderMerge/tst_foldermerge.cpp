#include <QtTest>

#include "foldermergeplan.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace LqCompare;
using namespace LqCompare::FolderMerge;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

struct Roots
{
    QTemporaryDir temp;
    Paths paths{temp.path() + "/base", temp.path() + "/left",
                temp.path() + "/right", temp.path() + "/output"};
    Roots()
    {
        QDir().mkpath(paths.base);
        QDir().mkpath(paths.left);
        QDir().mkpath(paths.right);
    }
};

// Observe every real directory name and byte sequence, including an already
// populated output. This is deliberately independent of the plan's decisions.
QMap<QString, QByteArray> treeContents(const QString &root)
{
    QMap<QString, QByteArray> result;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System |
                          QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString name = QDir(root).relativeFilePath(path);
        if (info.isSymLink()) {
            result.insert(name, QByteArray("link:") + info.symLinkTarget().toUtf8());
        } else if (info.isDir()) {
            result.insert(name, QByteArray("directory"));
        } else {
            QFile file(path);
            result.insert(name, file.open(QIODevice::ReadOnly)
                                   ? QByteArray("file:") + file.readAll()
                                   : QByteArray("unreadable"));
        }
    }
    return result;
}

bool putVersions(const Paths &paths, const QString &name, const QString &base,
                 const QString &left, const QString &right)
{
    return (base.isNull() || writeFile(paths.base + '/' + name, base.toUtf8())) &&
           (left.isNull() || writeFile(paths.left + '/' + name, left.toUtf8())) &&
           (right.isNull() || writeFile(paths.right + '/' + name, right.toUtf8()));
}

} // namespace

class FolderMergeTests : public QObject
{
    Q_OBJECT
private slots:
    void fileDecisions_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::addColumn<int>("decision");
        QTest::addColumn<int>("conflict");
        const auto add = [](const char *name, const QString &base,
                            const QString &left, const QString &right,
                            Decision decision, Conflict conflict = Conflict::None) {
            QTest::newRow(name) << base << left << right << int(decision) << int(conflict);
        };
        const QString absent;
        add("unchanged", "old", "old", "old", Decision::TakeLeft);
        add("left-edited", "old", "left", "old", Decision::TakeLeft);
        add("right-edited", "old", "old", "right", Decision::TakeRight);
        add("both-same-edit", "old", "new", "new", Decision::TakeLeft);
        add("both-different-edit", "old", "left", "right", Decision::Unresolved, Conflict::Content);
        add("empty-file-is-present", "", "", "new", Decision::TakeRight);
        add("left-added", absent, "left", absent, Decision::TakeLeft);
        add("right-added", absent, absent, "right", Decision::TakeRight);
        add("both-same-add", absent, "new", "new", Decision::TakeLeft);
        add("both-different-add", absent, "left", "right", Decision::Unresolved, Conflict::Content);
        add("left-deleted", "old", absent, "old", Decision::Delete);
        add("right-deleted", "old", "old", absent, Decision::Delete);
        add("both-deleted", "old", absent, absent, Decision::Delete);
        add("left-delete-right-modify", "old", absent, "right", Decision::Unresolved, Conflict::DeleteModify);
        add("right-delete-left-modify", "old", "left", absent, Decision::Unresolved, Conflict::DeleteModify);
    }

    void fileDecisions()
    {
        QFETCH(QString, base);
        QFETCH(QString, left);
        QFETCH(QString, right);
        QFETCH(int, decision);
        QFETCH(int, conflict);
        Roots roots;
        QVERIFY(roots.temp.isValid());
        QVERIFY(putVersions(roots.paths, "item.txt", base, left, right));
        const auto before = treeContents(roots.temp.path());
        const Plan plan = buildPlan(roots.paths);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.complete);
        QVERIFY(plan.hasBase); // Even an empty ancestor directory is a valid base.
        QVERIFY(!plan.cancelled);
        const Entry *entry = plan.find("item.txt");
        QVERIFY(entry);
        QCOMPARE(int(entry->decision), decision);
        QCOMPARE(int(entry->automaticDecision), decision);
        QCOMPARE(int(entry->conflict), conflict);
        QCOMPARE(entry->unresolved(), decision == int(Decision::Unresolved));
        QCOMPARE(entry->canRequestTextMerge(), conflict == int(Conflict::Content) && !base.isNull());
        QCOMPARE(plan.unresolvedCount(), decision == int(Decision::Unresolved) ? 1 : 0);
        QCOMPARE(plan.manualCount(), 0);
        QVERIFY(!entry->explanation.isEmpty());
        QVERIFY(!plan.canExecute());
        QVERIFY(!plan.executionDisabledReason().isEmpty());
        QVERIFY(plan.previewText().contains("item.txt"));
        QCOMPARE(treeContents(roots.temp.path()), before);
        QVERIFY(!QFileInfo::exists(roots.paths.output));
    }

    void withoutAncestorNeverGuessesChanges()
    {
        Roots roots;
        roots.paths.base.clear();
        QVERIFY(writeFile(roots.paths.left + "/same", "same bytes"));
        QVERIFY(writeFile(roots.paths.right + "/same", "same bytes"));
        QVERIFY(writeFile(roots.paths.left + "/different", "left"));
        QVERIFY(writeFile(roots.paths.right + "/different", "right"));
        QVERIFY(writeFile(roots.paths.left + "/only-left", "left"));
        QVERIFY(writeFile(roots.paths.right + "/only-right", "right"));
        const Plan plan = buildPlan(roots.paths);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.complete);
        QVERIFY(!plan.hasBase);
        QVERIFY(!plan.warnings.isEmpty());
        QVERIFY(plan.find("same"));
        QCOMPARE(plan.find("same")->decision, Decision::TakeLeft);
        QCOMPARE(plan.find("same")->conflict, Conflict::None);
        for (const QString &path : {QString("different"), QString("only-left"), QString("only-right")}) {
            const Entry *entry = plan.find(path);
            QVERIFY(entry);
            QCOMPARE(entry->decision, Decision::Unresolved);
            QCOMPARE(entry->conflict, Conflict::NoBase);
            QVERIFY(!entry->canRequestTextMerge());
        }
        QCOMPARE(plan.unresolvedCount(), 3);
    }

    void directoryConflictCountsComeFromChildren()
    {
        Roots roots;
        QVERIFY(putVersions(roots.paths, "nested/deeper/conflict", "old", "left", "right"));
        QVERIFY(putVersions(roots.paths, "nested/left-edit", "old", "left", "old"));
        QVERIFY(putVersions(roots.paths, "nested/right-edit", "old", "old", "right"));
        Plan plan = buildPlan(roots.paths);
        QVERIFY(plan.complete);
        for (const QString &path : {QString("nested"), QString("nested/deeper")}) {
            const Entry *entry = plan.find(path);
            QVERIFY(entry);
            QVERIFY(entry->isDirectory());
            QCOMPARE(entry->conflict, Conflict::None);
            QCOMPARE(entry->descendantConflicts, 1);
        }
        QCOMPARE(plan.unresolvedCount(), 1);
        QCOMPARE(plan.find("nested/left-edit")->decision, Decision::TakeLeft);
        QCOMPARE(plan.find("nested/right-edit")->decision, Decision::TakeRight);
        QVERIFY(setDecision(plan, "nested/deeper/conflict", Decision::TakeRight));
        QCOMPARE(plan.unresolvedCount(), 0);
        QCOMPARE(plan.find("nested")->descendantConflicts, 0);
        QCOMPARE(plan.find("nested/deeper")->descendantConflicts, 0);
    }

    void deletingDirectoryWithChangedDescendantConflicts()
    {
        Roots roots;
        QVERIFY(writeFile(roots.paths.base + "/removed/deep/item", "old"));
        QVERIFY(writeFile(roots.paths.right + "/removed/deep/item", "edited"));
        const Plan plan = buildPlan(roots.paths);
        QVERIFY(plan.complete);
        QVERIFY(plan.find("removed"));
        QCOMPARE(plan.find("removed")->conflict, Conflict::DeleteModify);
        QCOMPARE(plan.find("removed")->decision, Decision::Unresolved);
        QVERIFY(plan.find("removed/deep/item"));
        QVERIFY(plan.find("removed/deep/item")->blockedByAncestor);
        QVERIFY(!plan.find("removed")->canRequestTextMerge());
    }

    void deletingUnchangedDirectoryIsAutomatic()
    {
        Roots roots;
        QVERIFY(writeFile(roots.paths.base + "/removed/deep/item", "old"));
        QVERIFY(writeFile(roots.paths.right + "/removed/deep/item", "old"));
        const Plan plan = buildPlan(roots.paths);
        QVERIFY(plan.complete);
        QCOMPARE(plan.unresolvedCount(), 0);
        for (const QString &path : {QString("removed"), QString("removed/deep"), QString("removed/deep/item")}) {
            QVERIFY(plan.find(path));
            QCOMPARE(plan.find(path)->decision, Decision::Delete);
            QCOMPARE(plan.find(path)->conflict, Conflict::None);
        }
    }

    void typeChangesRemainExplicitAndKeepDescendants()
    {
        Roots roots;
        QVERIFY(writeFile(roots.paths.base + "/changed", "old"));
        QVERIFY(writeFile(roots.paths.right + "/changed", "old"));
        QVERIFY(writeFile(roots.paths.left + "/changed/deep/child", "new child"));
        Plan plan = buildPlan(roots.paths);
        QVERIFY(plan.complete);
        QVERIFY(plan.find("changed"));
        QCOMPARE(plan.find("changed")->conflict, Conflict::Type);
        QCOMPARE(plan.find("changed")->decision, Decision::Unresolved);
        QVERIFY(!plan.find("changed")->canRequestTextMerge());
        QVERIFY(plan.find("changed/deep/child"));
        QVERIFY(plan.find("changed/deep/child")->blockedByAncestor);
        QVERIFY(setDecision(plan, "changed", Decision::TakeLeft));
        QCOMPARE(plan.find("changed/deep/child")->decision, Decision::TakeLeft);
        QVERIFY(!plan.find("changed/deep/child")->blockedByAncestor);
        const QString beforeIncompatibleChoice = plan.previewText();
        const int manualCount = plan.manualCount();
        QString error;
        QVERIFY(!setDecision(plan, "changed", Decision::TakeRight, DirectoryPolicy::PreserveManual, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(plan.previewText(), beforeIncompatibleChoice);
        QCOMPARE(plan.manualCount(), manualCount);
        QCOMPARE(plan.find("changed")->decision, Decision::TakeLeft);
        QCOMPARE(plan.find("changed/deep/child")->decision, Decision::TakeLeft);
        QVERIFY(setDecision(plan, "changed", Decision::TakeRight, DirectoryPolicy::OverwriteManual));
        QCOMPARE(plan.find("changed/deep/child")->decision, Decision::Delete);
        QVERIFY(resetDecision(plan, "changed"));
        QCOMPARE(plan.find("changed")->decision, Decision::Unresolved);
        QVERIFY(plan.find("changed/deep/child")->blockedByAncestor);
    }

    void manualDecisionsAndResetAreReadOnly()
    {
        Roots roots;
        QVERIFY(putVersions(roots.paths, "conflict", "old", "left", "right"));
        QVERIFY(writeFile(roots.paths.base + "/deleted-on-left", "old"));
        QVERIFY(writeFile(roots.paths.right + "/deleted-on-left", "edited"));
        QVERIFY(writeFile(roots.paths.output + "/conflict", "existing output bytes"));
        QVERIFY(writeFile(roots.paths.output + "/unknown/unrelated", "keep me"));
        const auto before = treeContents(roots.temp.path());
        Plan plan = buildPlan(roots.paths);
        QString error;
        for (Decision decision : {Decision::TakeLeft, Decision::TakeRight,
                                  Decision::TakeBase, Decision::Ignore}) {
            QVERIFY2(setDecision(plan, "conflict", decision, DirectoryPolicy::RejectManualConflicts, &error), qPrintable(error));
            QCOMPARE(plan.find("conflict")->decision, decision);
            QVERIFY(plan.find("conflict")->manual);
            QVERIFY(!plan.find("conflict")->unresolved());
            QVERIFY(!plan.canExecute());
            QCOMPARE(treeContents(roots.temp.path()), before);
        }
        QVERIFY(resetDecision(plan, "conflict", &error));
        QCOMPARE(plan.find("conflict")->decision, Decision::Unresolved);
        QVERIFY(!plan.find("conflict")->manual);
        QVERIFY(setDecision(plan, "deleted-on-left", Decision::TakeLeft));
        QCOMPARE(plan.find("deleted-on-left")->decision, Decision::Delete);
        QCOMPARE(plan.manualCount(), 1);
        const QString preview = plan.previewText();
        QVERIFY(!setDecision(plan, "missing-path", Decision::Ignore, DirectoryPolicy::RejectManualConflicts, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(plan.previewText(), preview);
        QCOMPARE(treeContents(roots.temp.path()), before);
    }

    void directoryDecisionRejectsPreservesAndOverwritesManualChildren()
    {
        Roots roots;
        QVERIFY(putVersions(roots.paths, "dir/manual", "old", "left", "right"));
        QVERIFY(putVersions(roots.paths, "dir/automatic", "old", "left", "right"));
        const auto before = treeContents(roots.temp.path());
        Plan plan = buildPlan(roots.paths);
        QVERIFY(setDecision(plan, "dir/manual", Decision::TakeRight));
        const QString priorPreview = plan.previewText();
        const int priorManualCount = plan.manualCount();
        QString error;
        QVERIFY(!setDecision(plan, "dir", Decision::TakeLeft, DirectoryPolicy::RejectManualConflicts, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(plan.previewText(), priorPreview);
        QCOMPARE(plan.manualCount(), priorManualCount);
        QCOMPARE(plan.find("dir/manual")->decision, Decision::TakeRight);
        QCOMPARE(plan.find("dir/automatic")->decision, Decision::Unresolved);

        QVERIFY(setDecision(plan, "dir", Decision::TakeLeft, DirectoryPolicy::PreserveManual));
        QCOMPARE(plan.find("dir")->decision, Decision::TakeLeft);
        QCOMPARE(plan.find("dir/manual")->decision, Decision::TakeRight);
        QCOMPARE(plan.find("dir/automatic")->decision, Decision::TakeLeft);
        QCOMPARE(plan.unresolvedCount(), 0);

        QVERIFY(setDecision(plan, "dir", Decision::TakeLeft, DirectoryPolicy::OverwriteManual));
        QCOMPARE(plan.find("dir/manual")->decision, Decision::TakeLeft);
        QCOMPARE(plan.find("dir/automatic")->decision, Decision::TakeLeft);
        QVERIFY(resetDecision(plan, "dir"));
        QCOMPARE(plan.manualCount(), 0);
        QCOMPARE(plan.find("dir/manual")->decision, Decision::Unresolved);
        QCOMPARE(plan.find("dir/automatic")->decision, Decision::Unresolved);
        QCOMPARE(plan.unresolvedCount(), 2);
        QCOMPARE(treeContents(roots.temp.path()), before);
    }

    void incompleteScanNeverDeletesOrAcceptsMissingSide()
    {
        Roots roots;
        QVERIFY(putVersions(roots.paths, "visible-delete", "old", {}, "old"));
        QVERIFY(putVersions(roots.paths, "deep/more/item", "old", "old", "old"));
        QVERIFY(writeFile(roots.paths.left + "/left-only", "new"));
        Folder::Options options;
        options.maximumDepth = 0;
        Plan plan = buildPlan(roots.paths, options);
        QVERIFY(!plan.complete);
        QVERIFY(!plan.warnings.isEmpty() || !plan.error.isEmpty());
        QVERIFY(plan.find("visible-delete"));
        QVERIFY(plan.find("left-only"));
        for (const Entry &entry : plan.entries) {
            QVERIFY(entry.decision != Decision::Delete);
            QVERIFY(entry.automaticDecision != Decision::Delete);
        }
        const QString before = plan.previewText();
        QString error;
        QVERIFY(!setDecision(plan, "visible-delete", Decision::Delete, DirectoryPolicy::RejectManualConflicts, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!setDecision(plan, "visible-delete", Decision::TakeLeft));
        QVERIFY(!setDecision(plan, "left-only", Decision::TakeRight));
        QCOMPARE(plan.previewText(), before);
        QVERIFY(!plan.canExecute());
    }

    void cancelledScanCannotProduceExecutableOrDeletionPlan()
    {
        Roots roots;
        QVERIFY(putVersions(roots.paths, "item", "old", {}, "old"));
        const auto before = treeContents(roots.temp.path());
        std::atomic_bool cancelled{true};
        const Plan plan = buildPlan(roots.paths, {}, &cancelled);
        QVERIFY(plan.cancelled);
        QVERIFY(!plan.complete);
        QVERIFY(!plan.canExecute());
        for (const Entry &entry : plan.entries)
            QVERIFY(entry.decision != Decision::Delete);
        QCOMPARE(treeContents(roots.temp.path()), before);
    }

    void pathsChangedBetweenInventoriesAndComparisons_data()
    {
        QTest::addColumn<bool>("removePath");
        QTest::newRow("added-after-inventories") << false;
        QTest::newRow("removed-after-inventories") << true;
    }

    void pathsChangedBetweenInventoriesAndComparisons()
    {
        QFETCH(bool, removePath);
        Roots roots;
        QVERIFY(putVersions(roots.paths, "z-sentinel", "stable", "stable", "stable"));
        // This entry would be an automatic Delete in a stable, complete scan.
        QVERIFY(putVersions(roots.paths, "a-deletion-candidate", "old", {}, "old"));
        const QString changingPath = roots.paths.left + "/m-changing-path";
        if (removePath)
            QVERIFY(writeFile(changingPath, "present during inventory"));
        const auto before = treeContents(roots.temp.path());
        // There are three sentinel rows plus two deletion-candidate rows across
        // the left, right and ancestor self inventories. The removed path adds
        // one left row. Their final callback is the ancestor's z-sentinel,
        // before any left/right pair comparison starts.
        const int inventoryRows = removePath ? 6 : 5;
        int changes = 0;
        bool changedOnDisk = false;
        QString triggerPath;
        Plan plan = buildPlan(roots.paths, {}, nullptr,
            [&](int count, const QString &path) {
                if (count != inventoryRows || changes != 0)
                    return;
                ++changes;
                triggerPath = path;
                changedOnDisk = removePath ? QFile::remove(changingPath)
                                          : writeFile(changingPath, "created after inventory");
            });
        QCOMPARE(changes, 1);
        QCOMPARE(triggerPath, QString("z-sentinel"));
        QVERIFY(changedOnDisk);
        QCOMPARE(QFileInfo::exists(changingPath), !removePath);
        QVERIFY(!plan.complete);
        QVERIFY(!plan.cancelled);
        QVERIFY(!plan.warnings.isEmpty());
        QVERIFY(plan.find("m-changing-path"));
        QVERIFY(plan.find("a-deletion-candidate"));
        for (const Entry &entry : plan.entries) {
            QVERIFY(entry.decision != Decision::Delete);
            QVERIFY(entry.automaticDecision != Decision::Delete);
        }
        QVERIFY(!setDecision(plan, "a-deletion-candidate", Decision::Delete));
        QVERIFY(!setDecision(plan, "a-deletion-candidate", Decision::TakeLeft));
        QVERIFY(!plan.canExecute());
        auto expected = before;
        if (removePath)
            expected.remove("left/m-changing-path");
        else
            expected.insert("left/m-changing-path", "file:created after inventory");
        QCOMPARE(treeContents(roots.temp.path()), expected);
        QVERIFY(!QFileInfo::exists(roots.paths.output));
    }

    void fileChangedUnderTypeConflictInvalidatesPlan()
    {
        Roots roots;
        const QByteArray original("old");
        const QByteArray modified("different bytes after the inventory is complete");
        QVERIFY(putVersions(roots.paths, "z-sentinel", "stable", "stable", "stable"));
        QVERIFY(putVersions(roots.paths, "a-deletion-candidate", "old", {}, "old"));
        QVERIFY(writeFile(roots.paths.base + "/shape", "a file"));
        QVERIFY(writeFile(roots.paths.right + "/shape", "a file"));
        const QString child = roots.paths.left + "/shape/child";
        QVERIFY(writeFile(child, original));
        const QDateTime directoryModified = QFileInfo(roots.paths.left + "/shape").lastModified();
        const auto before = treeContents(roots.temp.path());
        int changes = 0;
        bool changedOnDisk = false;
        QString triggerPath;
        // Each self inventory has three rows. The left inventory sees shape,
        // its child and the sentinel. Base/right see the deletion candidate,
        // shape and the sentinel. Pair scans stop at shape's type conflict.
        Plan plan = buildPlan(roots.paths, {}, nullptr,
            [&](int count, const QString &path) {
                if (count != 9 || changes != 0)
                    return;
                ++changes;
                triggerPath = path;
                changedOnDisk = writeFile(child, modified);
            });
        QCOMPARE(changes, 1);
        QCOMPARE(triggerPath, QString("z-sentinel"));
        QVERIFY(changedOnDisk);
        QCOMPARE(QFileInfo(roots.paths.left + "/shape").lastModified(), directoryModified);
        const Entry *entry = plan.find("shape/child");
        QVERIFY(entry);
        QCOMPARE(entry->left.info.size, quint64(original.size()));
        QCOMPARE(QFileInfo(child).size(), qint64(modified.size()));
        QVERIFY(entry->left.info.size != quint64(QFileInfo(child).size()));
        QVERIFY(!plan.complete);
        QVERIFY(!plan.cancelled);
        for (const Entry &item : plan.entries) {
            QVERIFY(item.decision != Decision::Delete);
            QVERIFY(item.automaticDecision != Decision::Delete);
        }
        QVERIFY(!setDecision(plan, "a-deletion-candidate", Decision::Delete));
        QVERIFY(!setDecision(plan, "a-deletion-candidate", Decision::TakeLeft));
        QVERIFY(!plan.canExecute());
        auto expected = before;
        expected.insert("left/shape/child", QByteArray("file:") + modified);
        QCOMPARE(treeContents(roots.temp.path()), expected);
    }

    void cancellationDuringRealEnumeration()
    {
        Roots roots;
        for (int i = 0; i < 20; ++i)
            QVERIFY(putVersions(roots.paths, QString("file-%1").arg(i), "old", "left", "old"));
        std::atomic_bool cancelled{false};
        int progressCalls = 0;
        const Plan plan = buildPlan(roots.paths, {}, &cancelled,
            [&cancelled, &progressCalls](int, const QString &) {
                ++progressCalls;
                cancelled.store(true);
            });
        QVERIFY(progressCalls > 0);
        QVERIFY(plan.cancelled);
        QVERIFY(!plan.complete);
        QVERIFY(!plan.canExecute());
    }

    void scannerReturnsPlanWithoutWritingOutput()
    {
        Roots roots;
        QVERIFY(putVersions(roots.paths, "item", "old", "left", "old"));
        const auto before = treeContents(roots.temp.path());
        Scanner scanner;
        QSignalSpy finished(&scanner, &Scanner::finished);
        QVERIFY(finished.isValid());
        QVERIFY(scanner.start(roots.paths));
        QVERIFY(!scanner.start(roots.paths));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        const Plan plan = qvariant_cast<Plan>(finished.takeFirst().at(0));
        QVERIFY(plan.complete);
        QVERIFY(plan.find("item"));
        QCOMPARE(plan.find("item")->decision, Decision::TakeLeft);
        QTRY_VERIFY(!scanner.isRunning());
        QCOMPARE(treeContents(roots.temp.path()), before);
        QVERIFY(!QFileInfo::exists(roots.paths.output));
    }
};

QTEST_GUILESS_MAIN(FolderMergeTests)
#include "tst_foldermerge.moc"
