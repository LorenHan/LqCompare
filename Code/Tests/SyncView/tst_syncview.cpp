#include "syncpreviewdialog.h"
#include <QtTest>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>

using namespace LqCompare;
namespace {
void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) qFatal("fixture write failed");
}
QByteArray readFile(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
class TemporaryTrash final : public Files::TrashService {
public:
    explicit TemporaryTrash(QString root) : m_root(std::move(root)) { QDir().mkpath(m_root); }
    QString platformName() const override { return QStringLiteral("temporary-test-trash"); }
    Files::TrashAvailability availabilityFor(const QString &) const override { return Files::TrashAvailability::Available; }
    QString displayLocation() const override { return m_root; }
    bool undoLastDelete(Files::ErrorCode *error = nullptr) const override {
        for (const auto &record : m_lastDelete.records) {
            if (!record.succeeded()) continue;
            if (QFileInfo::exists(record.originalPath) || !QFile::rename(record.trashedPath, record.originalPath)) {
                if (error) *error = Files::FileSystemError::AlreadyExists;
                return false;
            }
        }
        if (error) *error = {};
        return true;
    }
protected:
    Files::TrashReport trashPaths(const QStringList &paths) const override {
        Files::TrashReport report;
        for (const auto &path : paths) {
            Files::TrashRecord record; record.originalPath = path;
            record.trashedPath = QDir(m_root).filePath(QString::number(++m_next) + '-' + QFileInfo(path).fileName());
            if (!QFile::rename(path, record.trashedPath)) record.error = Files::FileSystemError::Unknown;
            report.records.push_back(record);
        }
        return report;
    }
private:
    QString m_root;
    mutable int m_next = 0;
};
struct Fixture {
    QTemporaryDir directory;
    QString left = directory.filePath("left");
    QString right = directory.filePath("right");
    TemporaryTrash trash{directory.filePath("trash")};
    std::shared_ptr<Sync::Executor> executor{std::make_shared<Sync::Executor>(directory.filePath("backups"), &trash)};
    Fixture() { QDir().mkpath(left); QDir().mkpath(right); }
};
QTreeWidgetItem *rowFor(SyncPreviewDialog &dialog, const QString &relative)
{
    auto *tree = dialog.findChild<QTreeWidget *>("syncPlanTree");
    for (int group = 0; group < tree->topLevelItemCount(); ++group) {
        auto *parent = tree->topLevelItem(group);
        for (int i = 0; i < parent->childCount(); ++i)
            if (parent->child(i)->text(1) == relative) return parent->child(i);
    }
    return nullptr;
}
}
class SyncViewTests : public QObject {
    Q_OBJECT
private slots:
    void previewIsReadOnlyAndExportProtectsScope()
    {
        Fixture f; writeFile(f.left + "/new.txt", "new");
        SyncPreviewDialog dialog;
        QVERIFY(dialog.setExecutor(f.executor));
        QVERIFY(!dialog.executePlan());
        QVERIFY(!dialog.findChild<QPushButton *>("syncExecute")->isEnabled());
        dialog.setDirectories(f.left, f.right);
        QVERIFY(dialog.startPreview());
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.isBusy(), 10000);
        QVERIFY2(dialog.hasValidPlan(), qPrintable(dialog.statusText()));
        QVERIFY(!QFileInfo::exists(f.right + "/new.txt"));
        QVERIFY(dialog.findChild<QPushButton *>("syncExecute")->isEnabled());
        QString error;
        QVERIFY(!dialog.exportPlan(f.left + "/plan.txt", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(f.left + "/plan.txt"));
        const auto output = f.directory.filePath("plan.txt");
        QVERIFY2(dialog.exportPlan(output, &error), qPrintable(error));
        QVERIFY(readFile(output).contains("new.txt"));
        QVERIFY(readFile(output).contains(f.left.toUtf8()));
        dialog.setConfirmationHandler([](auto, const auto &) { return false; });
        QVERIFY(!dialog.executePlan());
        QVERIFY(!QFileInfo::exists(f.right + "/new.txt"));
        const auto screenshot = qEnvironmentVariable("LQCOMPARE_SYNC_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { dialog.show(); QTest::qWait(80); QVERIFY(dialog.grab().save(screenshot)); }
    }
    void checkboxVetoAndExecutionRefresh()
    {
        Fixture f; writeFile(f.left + "/accept.txt", "accept"); writeFile(f.left + "/reject.txt", "reject");
        SyncPreviewDialog dialog; QVERIFY(dialog.setExecutor(f.executor));
        dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        auto *veto = rowFor(dialog, "reject.txt"); QVERIFY(veto); veto->setCheckState(0, Qt::Unchecked);
        int confirmations = 0;
        dialog.setConfirmationHandler([&](auto kind, const auto &text) {
            if (kind != SyncPreviewDialog::ConfirmationKind::Execute) return false;
            if (!text.contains(f.left) || !text.contains(f.right)) return false;
            ++confirmations; return true;
        });
        QSignalSpy execution(&dialog, &SyncPreviewDialog::executionFinished);
        QSignalSpy preview(&dialog, &SyncPreviewDialog::previewReady);
        QVERIFY(dialog.executePlan());
        QTRY_COMPARE(execution.count(), 1); QTRY_VERIFY(!dialog.isBusy());
        QCOMPARE(confirmations, 1); QVERIFY(preview.count() > 0);
        QCOMPARE(readFile(f.right + "/accept.txt"), QByteArray("accept"));
        QVERIFY(!QFileInfo::exists(f.right + "/reject.txt"));
        QCOMPARE(dialog.lastReport().succeededCount(), 1);
        bool vetoLogged = false;
        for (const auto &result : dialog.lastReport().items)
            if (result.item.relativePath == "reject.txt") vetoLogged = result.outcome == Sync::Outcome::Skipped;
        QVERIFY(vetoLogged);
    }
    void changesInvalidateAndConfirmMutationIsRejected()
    {
        Fixture f; writeFile(f.left + "/new.txt", "new");
        SyncPreviewDialog dialog; dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        dialog.findChild<QComboBox *>("syncMode")->setCurrentIndex(1);
        QVERIFY(!dialog.hasValidPlan()); QVERIFY(!dialog.executePlan());
        QVERIFY(!dialog.findChild<QPushButton *>("syncExecute")->isEnabled());
        QVERIFY(dialog.startPreview()); QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        dialog.findChild<QLineEdit *>("syncLeftPath")->setText(f.directory.filePath("missing"));
        QVERIFY(!dialog.hasValidPlan()); QVERIFY(!dialog.executePlan());
        dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview()); QTRY_VERIFY(!dialog.isBusy());
        dialog.setConfirmationHandler([&](auto, const auto &) {
            dialog.findChild<QLineEdit *>("syncRightPath")->setText(f.directory.filePath("changed"));
            return true;
        });
        QVERIFY(!dialog.executePlan());
        QVERIFY(!QFileInfo::exists(f.right + "/new.txt"));
    }
    void twoWayConflictsCannotBeSelected()
    {
        Fixture f; writeFile(f.left + "/conflict.txt", "left"); writeFile(f.right + "/conflict.txt", "right");
        SyncPreviewDialog dialog; Sync::Options options; options.mode = Sync::Mode::TwoWay;
        dialog.setOptions(options); dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        auto *conflict = rowFor(dialog, "conflict.txt"); QVERIFY(conflict);
        QVERIFY(!(conflict->flags() & Qt::ItemIsUserCheckable));
        QCOMPARE(dialog.currentPlan().items.first().action, Sync::Action::Conflict);
        QVERIFY(!dialog.currentPlan().baselineUsed);
        QVERIFY(!dialog.executePlan());
        QVERIFY(!dialog.findChild<QComboBox *>("syncDirection")->isEnabled());
    }
    void deleteThresholdRequiresSecondConfirmationAndUndoWorks()
    {
        Fixture f; writeFile(f.right + "/extra.txt", "extra");
        SyncPreviewDialog dialog; QVERIFY(dialog.setExecutor(f.executor));
        Sync::Options options; options.mode = Sync::Mode::Mirror; options.deleteCountThreshold = 0;
        dialog.setOptions(options); dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        QVERIFY(Sync::summarize(dialog.currentPlan()).needsDeleteConfirmation);
        QVector<SyncPreviewDialog::ConfirmationKind> confirmations;
        dialog.setConfirmationHandler([&](auto kind, const auto &) { confirmations.push_back(kind); return kind == SyncPreviewDialog::ConfirmationKind::Execute; });
        QVERIFY(!dialog.executePlan()); QCOMPARE(confirmations.size(), 2);
        QCOMPARE(confirmations.at(1), SyncPreviewDialog::ConfirmationKind::LargeDelete);
        QVERIFY(QFileInfo::exists(f.right + "/extra.txt"));
        dialog.setConfirmationHandler([](auto, const auto &) { return true; });
        QVERIFY(dialog.executePlan()); QTRY_VERIFY(!dialog.isBusy());
        QCOMPARE(dialog.lastReport().succeededCount(), 1);
        QVERIFY(!QFileInfo::exists(f.right + "/extra.txt"));
        const auto result = dialog.lastReport().items.first();
        QVERIFY(result.trashedPath.startsWith(f.directory.filePath("trash")));
        QVERIFY(QFileInfo::exists(result.trashedPath));
        QSignalSpy recovered(&dialog, &SyncPreviewDialog::recoveryFinished);
        QVERIFY(dialog.undoLastTrash()); QTRY_COMPARE(recovered.count(), 1); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(recovered.first().first().toBool()); QCOMPARE(readFile(f.right + "/extra.txt"), QByteArray("extra"));
    }
    void overwrittenFileHasRecoverableBackup()
    {
        Fixture f; writeFile(f.left + "/file.txt", "new contents"); writeFile(f.right + "/file.txt", "original");
        SyncPreviewDialog dialog; QVERIFY(dialog.setExecutor(f.executor));
        Sync::Options options; options.mode = Sync::Mode::Mirror;
        dialog.setOptions(options); dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        dialog.setConfirmationHandler([](auto, const auto &) { return true; });
        QVERIFY(dialog.executePlan()); QTRY_VERIFY(!dialog.isBusy());
        QCOMPARE(dialog.lastReport().succeededCount(), 1);
        QCOMPARE(readFile(f.right + "/file.txt"), QByteArray("new contents"));
        const auto backup = dialog.lastReport().items.first().backupPath;
        QVERIFY(!backup.isEmpty()); QCOMPARE(readFile(backup), QByteArray("original"));
        QVERIFY(dialog.findChild<QPushButton *>("syncRestoreBackup")->isEnabled());
        QSignalSpy recovered(&dialog, &SyncPreviewDialog::recoveryFinished);
        QVERIFY(dialog.restoreBackup(0)); QTRY_COMPARE(recovered.count(), 1); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(recovered.first().first().toBool()); QCOMPARE(readFile(f.right + "/file.txt"), QByteArray("original"));
    }
    void externalChangeFailsSafelyAndShowsReport()
    {
        Fixture f; writeFile(f.left + "/file.txt", "new contents"); writeFile(f.right + "/file.txt", "original");
        SyncPreviewDialog dialog; QVERIFY(dialog.setExecutor(f.executor));
        Sync::Options options; options.mode = Sync::Mode::Mirror;
        dialog.setOptions(options); dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        writeFile(f.right + "/file.txt", "external change");
        dialog.setConfirmationHandler([](auto, const auto &) { return true; });
        QVERIFY(dialog.executePlan()); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(dialog.lastReport().failedCount() > 0 || !dialog.lastReport().error.isEmpty());
        QCOMPARE(readFile(f.right + "/file.txt"), QByteArray("external change"));
        QVERIFY(dialog.findChild<QTreeWidget *>("syncReportTree")->topLevelItemCount() > 0);
    }
    void groupVetoAndChangedPreviewDiscardOldResults()
    {
        Fixture f; writeFile(f.left + "/one.txt", "one"); writeFile(f.left + "/two.txt", "two");
        SyncPreviewDialog dialog; dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy());
        auto *tree = dialog.findChild<QTreeWidget *>("syncPlanTree");
        tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        for (const auto &item : dialog.currentPlan().items) QVERIFY(!item.selected);
        QVERIFY(!dialog.executePlan());
        tree->topLevelItem(0)->setCheckState(0, Qt::Checked);
        for (const auto &item : dialog.currentPlan().items) QVERIFY(item.selected);
        const auto another = f.directory.filePath("another"); QDir().mkpath(another);
        writeFile(another + "/fresh.txt", "fresh");
        QVERIFY(dialog.startPreview());
        dialog.setDirectories(another, f.right);
        QVERIFY(!dialog.hasValidPlan());
        QTRY_VERIFY_WITH_TIMEOUT(dialog.hasValidPlan() && !dialog.isBusy(), 10000);
        QCOMPARE(QFileInfo(dialog.currentPlan().leftRoot).canonicalFilePath(), QFileInfo(another).canonicalFilePath());
        QVERIFY(rowFor(dialog, "fresh.txt")); QVERIFY(!rowFor(dialog, "one.txt"));
    }
    void baselineSaveLoadAndCorruptionDowngrade()
    {
        Fixture f; writeFile(f.left + "/common.txt", "common"); writeFile(f.right + "/common.txt", "common");
        SyncPreviewDialog dialog; Sync::Options options; options.mode = Sync::Mode::TwoWay;
        dialog.setOptions(options); dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        QTRY_VERIFY(!dialog.isBusy()); QVERIFY(dialog.hasValidPlan());
        QVERIFY(dialog.findChild<QPushButton *>("syncSaveBaseline")->isEnabled());
        QVERIFY(!dialog.saveBaseline(f.left + "/baseline.json"));
        QVERIFY(!QFileInfo::exists(f.left + "/baseline.json"));
        const auto path = f.directory.filePath("baseline.json");
        QSignalSpy saved(&dialog, &SyncPreviewDialog::baselineSaved);
        QVERIFY(dialog.saveBaseline(path)); QTRY_COMPARE(saved.count(), 1); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY2(saved.first().first().toBool(), qPrintable(dialog.statusText()));
        QVERIFY(QFileInfo::exists(path));
        QSignalSpy loaded(&dialog, &SyncPreviewDialog::baselineLoaded);
        QVERIFY(dialog.loadBaseline(path)); QTRY_COMPARE(loaded.count(), 1);
        QVERIFY2(loaded.first().first().toBool(), qPrintable(dialog.statusText()));
        QVERIFY(dialog.startPreview()); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(dialog.currentPlan().baselineUsed);
        const auto broken = f.directory.filePath("broken.json"); writeFile(broken, "not valid JSON");
        QVERIFY(dialog.loadBaseline(broken)); QTRY_COMPARE(loaded.count(), 2);
        QVERIFY(!loaded.last().first().toBool());
        QVERIFY(dialog.findChild<QLabel *>("syncBaselineStatus")->text().contains(QStringLiteral("已清除旧基线")));
        QVERIFY(dialog.startPreview()); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(!dialog.currentPlan().baselineUsed);
        writeFile(f.right + "/common.txt", "changed");
        QVERIFY(dialog.startPreview()); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(!dialog.findChild<QPushButton *>("syncSaveBaseline")->isEnabled());
        QVERIFY(!dialog.saveBaseline(f.directory.filePath("invalid-baseline.json")));
        QVERIFY(!QFileInfo::exists(f.directory.filePath("invalid-baseline.json")));
    }
    void cancelledPreviewNeverBecomesExecutable()
    {
        Fixture f; writeFile(f.left + "/new.txt", "new");
        SyncPreviewDialog dialog; dialog.setDirectories(f.left, f.right); QVERIFY(dialog.startPreview());
        dialog.cancelOperation(); QTRY_VERIFY(!dialog.isBusy());
        QVERIFY(!dialog.hasValidPlan()); QVERIFY(!dialog.executePlan());
        QVERIFY(!QFileInfo::exists(f.right + "/new.txt"));
    }
};
QTEST_MAIN(SyncViewTests)
#include "tst_syncview.moc"
