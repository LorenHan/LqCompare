#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "syncengine.h"

using namespace LqCompare;

namespace {
bool put(const QString &path, const QByteArray &bytes, qint64 millis = 1000000) {
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    file.close();
    std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
    return fs->setTimes(path, Files::FileTime::fromMillisecondsSinceEpoch(millis), {});
}
QByteArray get(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
struct Fixture {
    QTemporaryDir temp;
    QString left = temp.filePath(QStringLiteral("left"));
    QString right = temp.filePath(QStringLiteral("right"));
    QString backup = temp.filePath(QStringLiteral("backup"));
    Fixture() { QDir().mkpath(left); QDir().mkpath(right); }
    QString l(const QString &name) const { return QDir(left).filePath(name); }
    QString r(const QString &name) const { return QDir(right).filePath(name); }
};
class TemporaryTrash final : public Files::TrashService {
public:
    explicit TemporaryTrash(const QString &path) : root(path) { QDir().mkpath(root); }
    QString root;
    QString failName;
    bool unavailable = false;
    mutable int calls = 0;
    QString platformName() const override { return QStringLiteral("temporary-test-trash"); }
    Files::TrashAvailability availabilityFor(const QString &) const override {
        return unavailable ? Files::TrashAvailability::VolumeNotSupported : Files::TrashAvailability::Available;
    }
    QString displayLocation() const override { return root; }
    bool undoLastDelete(Files::ErrorCode *error) const override {
        for (const auto &record : m_lastDelete.records) {
            if (record.succeeded() && (QFileInfo::exists(record.originalPath)
                || !QDir().rename(record.trashedPath, record.originalPath))) {
                if (error) *error = Files::FileSystemError::AlreadyExists;
                return false;
            }
        }
        if (error) *error = Files::FileSystemError::None;
        return true;
    }
protected:
    Files::TrashReport trashPaths(const QStringList &paths) const override {
        Files::TrashReport report;
        for (const auto &path : paths) {
            ++calls;
            Files::TrashRecord record; record.originalPath = path;
            record.trashedPath = QDir(root).filePath(QString::number(calls));
            if (QFileInfo(path).fileName() == failName) record.error = Files::FileSystemError::PermissionDenied;
            else if (!QDir().rename(path, record.trashedPath)) record.error = Files::FileSystemError::Unknown;
            report.records << record;
        }
        return report;
    }
};
Sync::Confirmation confirm(const Sync::Plan &plan, bool threshold = false) {
    return {Sync::confirmationDigest(plan), true, threshold};
}
const Sync::Item *find(const Sync::Plan &plan, const QString &path) {
    for (const auto &item : plan.items) if (item.relativePath == path) return &item;
    return nullptr;
}
Sync::Options mirror() { Sync::Options options; options.mode = Sync::Mode::Mirror; return options; }
Sync::Options twoWay() { Sync::Options options; options.mode = Sync::Mode::TwoWay; return options; }
}

class SyncTests : public QObject {
    Q_OBJECT
private slots:
    void updateOnlyCopiesNewer();
    void mirrorCreatesBacksUpAndRestores();
    void mirrorUsesTrashAndDeletesChildrenFirst();
    void explicitConfirmationAndSelectionBinding();
    void thresholdRequiresSecondConfirmation();
    void staleSameSizeSameTimeContentIsRejected();
    void incompleteScanAndOverlappingRootsRefused();
    void excludedSubtreesAreDeletionProtected();
    void noBaselineNeverInfersBothChanged();
    void baselineDistinguishesEditDeleteAndConflict();
    void wrongBaselineFallsBack();
    void cancellationRetainsCompletedAndListsRemaining();
    void failureContinuesAndTrashNeverFallsBack();
    void symlinkReplacementRefused();
    void childDeselectionProtectsDirectory();
    void restoreRefusesNewUserChanges();
    void emptyDirectoryAndRightToLeft();
    void readonlyFailurePreservesOriginalAndContinues();
    void backupCannotPolluteSyncedTree();
    void invalidRuleAndFilteredScanAreRefused();
    void cancelledBeforeExecutionWritesNothing();
    void replacedAncestorIsRejected();
};

void SyncTests::updateOnlyCopiesNewer() {
    Fixture f;
    QVERIFY(put(f.l("newer"), "left-new", 2000000)); QVERIFY(put(f.r("newer"), "old", 1000000));
    QVERIFY(put(f.l("older"), "old", 1000000)); QVERIFY(put(f.r("older"), "right-new", 2000000));
    QVERIFY(put(f.r("extra"), "keep")); QVERIFY(put(f.l("only"), "add"));
    auto plan = Sync::preview(f.left, f.right);
    QVERIFY2(plan.executable(), qPrintable(plan.error));
    QCOMPARE(find(plan, "newer")->reasonCode, QStringLiteral("source-newer"));
    QCOMPARE(find(plan, "older")->action, Sync::Action::Skip);
    Sync::Executor executor(f.backup);
    auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.failedCount(), 0); QCOMPARE(report.succeededCount(), 2);
    QCOMPARE(get(f.r("newer")), QByteArray("left-new")); QCOMPARE(get(f.r("older")), QByteArray("right-new"));
    QCOMPARE(get(f.r("extra")), QByteArray("keep")); QCOMPARE(get(f.r("only")), QByteArray("add"));
    QVERIFY(!report.baselineEligible); // skipped differences cannot establish a common state.
}
void SyncTests::mirrorCreatesBacksUpAndRestores() {
    Fixture f;
    QVERIFY(put(f.l("a.txt"), "new")); QVERIFY(put(f.r("a.txt"), "old"));
    QVERIFY(put(f.l("nested/child"), "child"));
    auto plan = Sync::preview(f.left, f.right, mirror());
    QVERIFY2(plan.executable(), qPrintable(plan.error));
    Sync::Executor executor(f.backup);
    const auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.failedCount(), 0); QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QCOMPARE(get(f.r("a.txt")), QByteArray("new")); QCOMPARE(get(f.r("nested/child")), QByteArray("child"));
    const auto result = *std::find_if(report.items.begin(), report.items.end(), [](const Sync::ItemResult &r) { return r.item.relativePath == "a.txt"; });
    QCOMPARE(get(result.backupPath), QByteArray("old"));
    const auto journal = QJsonDocument::fromJson(get(report.journalPath));
    QVERIFY(journal.isObject()); QCOMPARE(journal.object().value("planId").toString(), plan.id);
    QVERIFY(get(report.journalPath).contains(result.backupPath.toUtf8()));
    QString error; QVERIFY2(executor.restoreBackup(result, &error), qPrintable(error));
    QCOMPARE(get(f.r("a.txt")), QByteArray("old"));
}
void SyncTests::mirrorUsesTrashAndDeletesChildrenFirst() {
    Fixture f; QVERIFY(put(f.r("extra/sub/file"), "recoverable"));
    TemporaryTrash trash(f.temp.filePath("trash"));
    auto plan = Sync::preview(f.left, f.right, mirror());
    QVERIFY2(plan.executable(), qPrintable(plan.error));
    QCOMPARE(Sync::summarize(plan).deletions, 3);
    Sync::Executor executor(f.backup, &trash);
    auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.failedCount(), 0); QCOMPARE(trash.calls, 3);
    QVERIFY(!QFileInfo::exists(f.r("extra")));
    QCOMPARE(get(report.items.first().trashedPath), QByteArray("recoverable"));
    QVERIFY(report.items.first().item.relativePath.endsWith("file"));
    QString error; QVERIFY2(executor.undoLastTrash(&error), qPrintable(error));
    QVERIFY(QFileInfo(f.r("extra")).isDir()); // truthful API: only latest batch restored.
}
void SyncTests::explicitConfirmationAndSelectionBinding() {
    Fixture f; QVERIFY(put(f.l("file"), "content"));
    auto plan = Sync::preview(f.left, f.right, mirror());
    Sync::Executor executor(f.backup);
    QVERIFY(!executor.execute(plan, {}).error.isEmpty()); QVERIFY(!QFileInfo::exists(f.r("file")));
    const auto authorization = confirm(plan);
    plan.items.first().selected = false;
    QVERIFY(!executor.execute(plan, authorization).error.isEmpty());
    const auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.succeededCount(), 0); QVERIFY(!QFileInfo::exists(f.r("file")));
}
void SyncTests::thresholdRequiresSecondConfirmation() {
    Fixture f; QVERIFY(put(f.r("file"), "content"));
    TemporaryTrash trash(f.temp.filePath("trash")); Sync::Executor executor(f.backup, &trash);
    auto options = mirror(); options.deleteCountThreshold = 0;
    auto plan = Sync::preview(f.left, f.right, options);
    QVERIFY(Sync::summarize(plan).needsDeleteConfirmation);
    QVERIFY(!executor.execute(plan, confirm(plan)).error.isEmpty()); QCOMPARE(trash.calls, 0);
    QCOMPARE(executor.execute(plan, confirm(plan, true)).succeededCount(), 1); QCOMPARE(trash.calls, 1);
}
void SyncTests::staleSameSizeSameTimeContentIsRejected() {
    Fixture f; QVERIFY(put(f.l("a"), "AAAA")); QVERIFY(put(f.r("a"), "BBBB")); QVERIFY(put(f.l("z"), "new"));
    auto plan = Sync::preview(f.left, f.right, mirror());
    QVERIFY(put(f.r("a"), "CCCC")); // same length and timestamp: hash must reject.
    Sync::Executor executor(f.backup); const auto report = executor.execute(plan, confirm(plan));
    QVERIFY(!report.error.isEmpty()); QCOMPARE(report.succeededCount(), 0);
    QCOMPARE(get(f.r("a")), QByteArray("CCCC")); QVERIFY(!QFileInfo::exists(f.r("z")));
}
void SyncTests::incompleteScanAndOverlappingRootsRefused() {
    Fixture f; QVERIFY(put(f.l("a"), "a"));
    auto scan = Folder::compare(f.left, f.right); scan.complete = false;
    QVERIFY(!Sync::makePlan(scan, mirror()).executable());
    QVERIFY(!Sync::preview(f.left, f.left).executable());
    QDir().mkpath(f.l("nested")); QVERIFY(!Sync::preview(f.left, f.l("nested")).executable());
}
void SyncTests::excludedSubtreesAreDeletionProtected() {
    Fixture f; QVERIFY(put(f.r("extra/keep"), "keep")); QVERIFY(put(f.r("extra/remove"), "remove"));
    auto options = mirror(); options.excludedPaths << "extra/keep";
    auto plan = Sync::preview(f.left, f.right, options);
    QVERIFY2(plan.executable(), qPrintable(plan.error));
    QCOMPARE(find(plan, "extra/keep")->reasonCode, QStringLiteral("excluded"));
    QCOMPARE(find(plan, "extra")->reasonCode, QStringLiteral("deletion-protected"));
    TemporaryTrash trash(f.temp.filePath("trash")); Sync::Executor executor(f.backup, &trash);
    QCOMPARE(executor.execute(plan, confirm(plan)).succeededCount(), 1);
    QCOMPARE(get(f.r("extra/keep")), QByteArray("keep")); QVERIFY(!QFileInfo::exists(f.r("extra/remove")));
}
void SyncTests::noBaselineNeverInfersBothChanged() {
    Fixture f; QVERIFY(put(f.l("a"), "left", 2000000)); QVERIFY(put(f.r("a"), "right", 1000000));
    QVERIFY(put(f.l("left-only"), "left")); QVERIFY(put(f.r("right-only"), "right"));
    auto plan = Sync::preview(f.left, f.right, twoWay());
    QVERIFY(!plan.baselineUsed); QVERIFY(!plan.warnings.isEmpty());
    QCOMPARE(find(plan, "a")->action, Sync::Action::Conflict);
    QCOMPARE(find(plan, "a")->reasonCode, QStringLiteral("different-without-baseline"));
    QCOMPARE(find(plan, "left-only")->action, Sync::Action::CopyLeftToRight);
    QCOMPARE(find(plan, "right-only")->action, Sync::Action::CopyRightToLeft);
    Sync::Executor executor(f.backup); auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.succeededCount(), 2); QVERIFY(!report.baselineEligible);
    QCOMPARE(get(f.l("a")), QByteArray("left")); QCOMPARE(get(f.r("a")), QByteArray("right"));
}
void SyncTests::baselineDistinguishesEditDeleteAndConflict() {
    Fixture f;
    for (const auto &name : {"edit", "deleted", "conflict", "delete-edit"}) {
        QVERIFY(put(f.l(name), "base")); QVERIFY(put(f.r(name), "base"));
    }
    auto options = twoWay(); auto base = Sync::commonBaseline(Sync::preview(f.left, f.right, options));
    QVERIFY(base.complete);
    QVERIFY(put(f.l("edit"), "left")); QVERIFY(QFile::remove(f.l("deleted")));
    QVERIFY(put(f.l("conflict"), "left")); QVERIFY(put(f.r("conflict"), "right"));
    QVERIFY(QFile::remove(f.l("delete-edit"))); QVERIFY(put(f.r("delete-edit"), "right"));
    auto plan = Sync::preview(f.left, f.right, options, &base);
    QVERIFY2(plan.executable(), qPrintable(plan.error)); QVERIFY(plan.baselineUsed);
    QCOMPARE(find(plan, "edit")->reasonCode, QStringLiteral("left-changed"));
    QCOMPARE(find(plan, "deleted")->action, Sync::Action::DeleteRight);
    QCOMPARE(find(plan, "conflict")->reasonCode, QStringLiteral("both-changed"));
    QCOMPARE(find(plan, "delete-edit")->action, Sync::Action::Conflict);
}
void SyncTests::wrongBaselineFallsBack() {
    Fixture f; QVERIFY(put(f.l("a"), "base")); QVERIFY(put(f.r("a"), "base"));
    auto options = twoWay(); auto base = Sync::commonBaseline(Sync::preview(f.left, f.right, options));
    base.rightRoot += "-other"; QVERIFY(put(f.l("a"), "left"));
    auto plan = Sync::preview(f.left, f.right, options, &base);
    QVERIFY(!plan.baselineUsed); QCOMPARE(find(plan, "a")->reasonCode, QStringLiteral("different-without-baseline"));
}
void SyncTests::cancellationRetainsCompletedAndListsRemaining() {
    Fixture f; QVERIFY(put(f.l("a"), "first")); QVERIFY(put(f.l("b"), "second"));
    auto plan = Sync::preview(f.left, f.right, mirror()); Sync::Executor executor(f.backup);
    std::atomic_bool cancelled{false};
    auto report = executor.execute(plan, confirm(plan), &cancelled, [&](int count, int, const QString &) { if (count == 1) cancelled = true; });
    QCOMPARE(report.succeededCount(), 1); QVERIFY(report.cancelled); QCOMPARE(report.items.size(), 2);
    QCOMPARE(get(f.r("a")), QByteArray("first")); QVERIFY(!QFileInfo::exists(f.r("b"))); QVERIFY(!report.baselineEligible);
}
void SyncTests::failureContinuesAndTrashNeverFallsBack() {
    Fixture f; QVERIFY(put(f.r("a"), "blocked")); QVERIFY(put(f.r("b"), "remove"));
    TemporaryTrash trash(f.temp.filePath("trash")); trash.failName = "a"; Sync::Executor executor(f.backup, &trash);
    auto plan = Sync::preview(f.left, f.right, mirror()); auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.failedCount(), 1); QCOMPARE(report.succeededCount(), 1);
    QCOMPARE(get(f.r("a")), QByteArray("blocked")); QVERIFY(!QFileInfo::exists(f.r("b")));
    trash.unavailable = true; plan = Sync::preview(f.left, f.right, mirror());
    report = executor.execute(plan, confirm(plan)); QCOMPARE(report.failedCount(), 1);
    QCOMPARE(get(f.r("a")), QByteArray("blocked")); QCOMPARE(trash.calls, 2);
}
void SyncTests::symlinkReplacementRefused() {
    Fixture f; QVERIFY(put(f.l("folder/file"), "inside")); QVERIFY(put(f.r("folder/file"), "target"));
    const QString outside = f.temp.filePath("outside"); QVERIFY(put(QDir(outside).filePath("file"), "outside"));
    auto plan = Sync::preview(f.left, f.right, mirror());
    QVERIFY(QDir(f.r("folder")).removeRecursively()); QVERIFY(QFile::link(outside, f.r("folder")));
    Sync::Executor executor(f.backup); QVERIFY(!executor.execute(plan, confirm(plan)).error.isEmpty());
    QCOMPARE(get(QDir(outside).filePath("file")), QByteArray("outside"));
}
void SyncTests::childDeselectionProtectsDirectory() {
    Fixture f; QVERIFY(put(f.r("folder/file"), "keep")); auto plan = Sync::preview(f.left, f.right, mirror());
    for (auto &item : plan.items) if (item.relativePath == "folder/file") item.selected = false;
    TemporaryTrash trash(f.temp.filePath("trash")); Sync::Executor executor(f.backup, &trash);
    auto report = executor.execute(plan, confirm(plan)); QCOMPARE(report.failedCount(), 1); QCOMPARE(trash.calls, 0);
    QCOMPARE(get(f.r("folder/file")), QByteArray("keep"));
}
void SyncTests::restoreRefusesNewUserChanges() {
    Fixture f; QVERIFY(put(f.l("a"), "new")); QVERIFY(put(f.r("a"), "old"));
    auto plan = Sync::preview(f.left, f.right, mirror()); Sync::Executor executor(f.backup);
    const auto report = executor.execute(plan, confirm(plan)); QCOMPARE(report.succeededCount(), 1);
    QVERIFY(put(f.r("a"), "user edit")); QString error;
    QVERIFY(!executor.restoreBackup(report.items.first(), &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(get(f.r("a")), QByteArray("user edit")); QCOMPARE(get(report.items.first().backupPath), QByteArray("old"));
}
void SyncTests::emptyDirectoryAndRightToLeft() {
    Fixture f; QDir().mkpath(f.r("empty")); QVERIFY(put(f.r("file"), "right"));
    auto options = mirror(); options.direction = Sync::Direction::RightToLeft;
    auto plan = Sync::preview(f.left, f.right, options); Sync::Executor executor(f.backup);
    auto report = executor.execute(plan, confirm(plan)); QCOMPARE(report.failedCount(), 0); QCOMPARE(report.succeededCount(), 2);
    QCOMPARE(get(f.l("file")), QByteArray("right")); QVERIFY(QFileInfo(f.l("empty")).isDir());
}
void SyncTests::readonlyFailurePreservesOriginalAndContinues() {
    Fixture f; QVERIFY(put(f.l("a"), "new")); QVERIFY(put(f.r("a"), "readonly")); QVERIFY(put(f.l("b"), "copy"));
    QVERIFY(QFile::setPermissions(f.r("a"), QFileDevice::ReadOwner | QFileDevice::ReadUser));
    auto plan = Sync::preview(f.left, f.right, mirror()); Sync::Executor executor(f.backup);
    auto report = executor.execute(plan, confirm(plan));
    QCOMPARE(report.failedCount(), 1); QCOMPARE(report.succeededCount(), 1);
    QCOMPARE(get(f.r("a")), QByteArray("readonly")); QCOMPARE(get(f.r("b")), QByteArray("copy"));
    QVERIFY(QFile::setPermissions(f.r("a"), QFileDevice::ReadOwner | QFileDevice::WriteOwner));
}
void SyncTests::backupCannotPolluteSyncedTree() {
    Fixture f; QVERIFY(put(f.l("a"), "new")); QVERIFY(put(f.r("a"), "old"));
    const auto plan = Sync::preview(f.left, f.right, mirror());
    Sync::Executor inside(f.r("backup"));
    QVERIFY(!inside.execute(plan, confirm(plan)).error.isEmpty()); QCOMPARE(get(f.r("a")), QByteArray("old"));
    const QString alias = f.temp.filePath("alias"); QVERIFY(QFile::link(f.right, alias));
    Sync::Executor aliased(QDir(alias).filePath("backup"));
    QVERIFY(!aliased.execute(plan, confirm(plan)).error.isEmpty()); QCOMPARE(get(f.r("a")), QByteArray("old"));
}
void SyncTests::invalidRuleAndFilteredScanAreRefused() {
    Fixture f; QVERIFY(put(f.l("a"), "content"));
    auto options = mirror(); options.deletion = Sync::Deletion::Keep;
    QVERIFY(!Sync::preview(f.left, f.right, options).executable());
    auto scan = Folder::compare(f.left, f.right); scan.scanMaskDeclaration = "*.txt";
    QVERIFY(!Sync::makePlan(scan).executable());
    scan.scanMaskDeclaration.clear(); scan.excludedCount = 1; QVERIFY(!Sync::makePlan(scan).executable());
    scan.excludedCount = 0; scan.entries.first().excludedByMask = true;
    QVERIFY(!Sync::makePlan(scan).executable());
    options = mirror(); options.mode = static_cast<Sync::Mode>(99);
    QVERIFY(!Sync::preview(f.left, f.right, options).executable());
    options = mirror(); options.direction = static_cast<Sync::Direction>(99);
    QVERIFY(!Sync::preview(f.left, f.right, options).executable());
    options = mirror(); options.deletion = static_cast<Sync::Deletion>(99);
    QVERIFY(!Sync::preview(f.left, f.right, options).executable());
}
void SyncTests::cancelledBeforeExecutionWritesNothing() {
    Fixture f; QVERIFY(put(f.l("a"), "content"));
    auto plan = Sync::preview(f.left, f.right); Sync::Executor executor(f.backup);
    std::atomic_bool cancelled{true}; const auto report = executor.execute(plan, confirm(plan), &cancelled);
    QVERIFY(report.cancelled); QCOMPARE(report.items.size(), 1); QCOMPARE(report.items.first().outcome, Sync::Outcome::Cancelled);
    QVERIFY(!QFileInfo::exists(f.r("a")));
}
void SyncTests::replacedAncestorIsRejected() {
    Fixture f; QVERIFY(put(f.l("a/b/file"), "new")); QVERIFY(put(f.r("a/b/file"), "old"));
    auto plan = Sync::preview(f.left, f.right, mirror());
    const QString moved = f.temp.filePath("moved-directory");
    QVERIFY(QDir().rename(f.r("a/b"), moved)); QVERIFY(QDir().mkpath(f.r("a/b")));
    QVERIFY(QFile::rename(QDir(moved).filePath("file"), f.r("a/b/file")));
    Sync::Executor executor(f.backup); const auto report = executor.execute(plan, confirm(plan));
    QVERIFY(!report.error.isEmpty()); QCOMPARE(get(f.r("a/b/file")), QByteArray("old"));
}

QTEST_GUILESS_MAIN(SyncTests)
#include "tst_sync.moc"
