#include <QtTest>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include "foldermergesession.h"
#include "foldermergeview.h"

using namespace LqCompare;
using FolderMerge::Decision;
using FolderMerge::DirectoryPolicy;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
struct Fixture {
    QTemporaryDir temporary;
    QString base = temporary.filePath("base"), left = temporary.filePath("left"), right = temporary.filePath("right"),
        output = temporary.filePath("output");
    bool init() {
        return writeFile(base + "/dir/conflict.txt", "original\n")
            && writeFile(left + "/dir/conflict.txt", "left side\n")
            && writeFile(right + "/dir/conflict.txt", "right side\n")
            && writeFile(base + "/same.txt", "same\n")
            && writeFile(left + "/same.txt", "same\n")
            && writeFile(right + "/same.txt", "same\n");
    }
};
QTreeWidgetItem *findItem(QTreeWidget *tree, const QString &path)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == path) return *it;
    return nullptr;
}
}

class FolderMergeViewTests : public QObject {
    Q_OBJECT
private slots:
    void fourSidesAndPreviewStayReadOnly();
    void buttonsChangePlanAndClearHiddenSelection();
    void manualChoicesProtectReloadAndPaths();
    void subtreePoliciesAndReset();
    void noBaseModeIsExplicit();
    void textRequestForConflictOnly();
    void textRequestRejectsInputOverlap_data();
    void textRequestRejectsInputOverlap();
    void textRequestRejectsSymlinkAncestors();
    void textRequestRejectsSymlinkLeaf();
    void textRequestRejectsDanglingLink();
    void cancelledScanRemainsIncomplete();
    void closeSuppressesQueuedCallbacks();
    void viewSurvivesSessionDestruction();
    void emptySessionCanChooseSources();
};

void FolderMergeViewTests::fourSidesAndPreviewStayReadOnly()
{
    Fixture f; QVERIFY(f.init());
    QVERIFY(writeFile(f.output + "/existing.txt", "keep this\n"));
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QWidget container;
    auto *view = qobject_cast<FolderMergeView *>(session.createWidget(&container));
    QVERIFY(view);
    QCOMPARE(session.typeId(), QString("folder-merge"));
    QCOMPARE(session.createWidget(), view);
    QSignalSpy finished(&session, &FolderMergeSession::scanFinished);
    QVERIFY(session.open());
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!session.isScanning());
    QVERIFY(session.plan().complete);
    auto *tree = view->findChild<QTreeWidget *>("folderMergeTree");
    QVERIFY(tree); QCOMPARE(tree->columnCount(), 5);
    auto *conflict = findItem(tree, "dir/conflict.txt");
    QVERIFY(conflict); QVERIFY(conflict->parent());
    QCOMPARE(conflict->parent()->data(0, Qt::UserRole).toString(), QString("dir"));
    QVERIFY(conflict->parent()->text(4).contains("子项分别决策"));
    auto *execute = view->findChild<QPushButton *>("folderMergeExecute");
    QVERIFY(execute); QVERIFY(!execute->isEnabled());
    QVERIFY(view->findChild<QLabel *>("folderMergeExecutionNotice")->text().contains("禁用"));
    QVERIFY(!session.canSave());
    QVERIFY(!session.plan().canExecute());
    QVERIFY(session.plan().previewText().contains("dir/conflict.txt"));
    tree->setCurrentItem(conflict);
    QVERIFY(view->findChild<QPushButton *>("folderMergeTextMerge")->isEnabled());
    auto *filter = view->findChild<QCheckBox *>("folderMergeConflictsOnly");
    filter->setChecked(true);
    QVERIFY(findItem(tree, "same.txt")->isHidden());
    QVERIFY(!conflict->isHidden()); QVERIFY(!conflict->parent()->isHidden());
    QCOMPARE(readFile(f.base + "/dir/conflict.txt"), QByteArray("original\n"));
    QCOMPARE(readFile(f.left + "/dir/conflict.txt"), QByteArray("left side\n"));
    QCOMPARE(readFile(f.right + "/dir/conflict.txt"), QByteArray("right side\n"));
    QCOMPARE(readFile(f.output + "/existing.txt"), QByteArray("keep this\n"));
    QVERIFY(!QFileInfo::exists(f.output + "/dir"));
    if (qEnvironmentVariableIsSet("FOLDERMERGE_SCREENSHOT")) {
        container.resize(1350, 850); view->setGeometry(container.rect()); container.show();
        QVERIFY(QTest::qWaitForWindowExposed(&container));
        QVERIFY(view->grab().save(qEnvironmentVariable("FOLDERMERGE_SCREENSHOT")));
    }
}

void FolderMergeViewTests::manualChoicesProtectReloadAndPaths()
{
    Fixture f; QVERIFY(f.init());
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QSignalSpy finished(&session, &FolderMergeSession::scanFinished);
    QVERIFY(session.open()); QTRY_COMPARE(finished.count(), 1);
    QVERIFY(session.setDecision("dir/conflict.txt", Decision::TakeLeft));
    QVERIFY(session.isDirty()); QVERIFY(!session.canSave());
    QString error;
    QVERIFY(!session.reload(&error)); QVERIFY(!error.isEmpty());
    QVERIFY(!session.rescan(false, &error));
    QVERIFY(!session.setPaths(f.base, f.right, f.left, f.output, &error));
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::TakeLeft);
    // Clearing the generic dirty flag cannot silently discard a manual plan.
    session.setDirty(false);
    QVERIFY(!session.reload(&error));
    QVERIFY(session.resetDecision("dir/conflict.txt", &error));
    QVERIFY(!session.isDirty());
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::Unresolved);
    QVERIFY(session.setDecision("dir/conflict.txt", Decision::TakeRight));
    QVERIFY(session.rescan(true, &error));
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!session.isDirty()); QCOMPARE(session.plan().manualCount(), 0);
    QVERIFY(!QFileInfo::exists(f.output));
}

void FolderMergeViewTests::buttonsChangePlanAndClearHiddenSelection()
{
    Fixture f; QVERIFY(f.init());
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QWidget owner;
    auto *view = qobject_cast<FolderMergeView *>(session.createWidget(&owner));
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    auto *tree = view->findChild<QTreeWidget *>("folderMergeTree");
    auto *filter = view->findChild<QCheckBox *>("folderMergeConflictsOnly");
    filter->setChecked(true);
    tree->setCurrentItem(findItem(tree, "dir/conflict.txt"));
    view->findChild<QPushButton *>("folderMergeTakeLeft")->click();
    QVERIFY(session.isDirty());
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::TakeLeft);
    QVERIFY(findItem(tree, "dir/conflict.txt")->isHidden());
    QVERIFY(view->selectedPath().isEmpty());
    QVERIFY(!view->findChild<QPushButton *>("folderMergeTakeRight")->isEnabled());
    filter->setChecked(false);
    tree->setCurrentItem(findItem(tree, "dir/conflict.txt"));
    view->findChild<QPushButton *>("folderMergeReset")->click();
    QVERIFY(!session.isDirty());
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::Unresolved);
    QCOMPARE(readFile(f.left + "/dir/conflict.txt"), QByteArray("left side\n"));
    QVERIFY(!QFileInfo::exists(f.output));
}

void FolderMergeViewTests::subtreePoliciesAndReset()
{
    Fixture f; QVERIFY(f.init());
    QVERIFY(writeFile(f.base + "/dir/other.txt", "a"));
    QVERIFY(writeFile(f.left + "/dir/other.txt", "b"));
    QVERIFY(writeFile(f.right + "/dir/other.txt", "c"));
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QVERIFY(session.setDecision("dir/conflict.txt", Decision::TakeRight));
    QString error;
    QVERIFY(!session.setDecision("dir", Decision::TakeLeft, DirectoryPolicy::RejectManualConflicts, &error));
    QCOMPARE(session.plan().manualCount(), 1);
    QVERIFY(session.setDecision("dir", Decision::TakeLeft, DirectoryPolicy::PreserveManual, &error));
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::TakeRight);
    QCOMPARE(session.plan().find("dir/other.txt")->decision, Decision::TakeLeft);
    QVERIFY(session.setDecision("dir", Decision::TakeBase, DirectoryPolicy::OverwriteManual, &error));
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::TakeBase);
    QVERIFY(session.resetDecision("dir"));
    QCOMPARE(session.plan().manualCount(), 0); QVERIFY(!session.isDirty());
    QVERIFY(!QFileInfo::exists(f.output));
}

void FolderMergeViewTests::noBaseModeIsExplicit()
{
    Fixture f; QVERIFY(f.init());
    FolderMergeSession session({}, f.left, f.right, f.output);
    QWidget owner;
    auto *view = session.createWidget(&owner);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QVERIFY(!session.plan().hasBase);
    QVERIFY(view->findChild<QLabel *>("folderMergeBaseNotice")->text().contains("无祖先"));
    QString error;
    QVERIFY(!session.setDecision("dir/conflict.txt", Decision::TakeBase, DirectoryPolicy::RejectManualConflicts, &error));
    QVERIFY(!session.requestTextMerge("dir/conflict.txt", &error));
    QVERIFY(!error.isEmpty());
}

void FolderMergeViewTests::textRequestForConflictOnly()
{
    Fixture f; QVERIFY(f.init());
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QSignalSpy request(&session, &FolderMergeSession::textMergeRequested);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QString error;
    QVERIFY2(session.requestTextMerge("dir/conflict.txt", &error), qPrintable(error));
    QCOMPARE(request.count(), 1);
    QCOMPARE(request.first().at(0).toString(), f.base + "/dir/conflict.txt");
    QCOMPARE(request.first().at(1).toString(), f.left + "/dir/conflict.txt");
    QCOMPARE(request.first().at(2).toString(), f.right + "/dir/conflict.txt");
    QCOMPARE(request.first().at(3).toString(), f.output + "/dir/conflict.txt");
    QVERIFY(!session.requestTextMerge("same.txt", &error));
    QCOMPARE(request.count(), 1);
    QVERIFY(!session.isDirty());
    QCOMPARE(session.plan().find("dir/conflict.txt")->decision, Decision::Unresolved);
    QVERIFY(!QFileInfo::exists(f.output));
}

void FolderMergeViewTests::textRequestRejectsInputOverlap_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("same-input") << 0;
    QTest::newRow("nested-missing-output") << 1;
    QTest::newRow("ancestor-output") << 2;
}
void FolderMergeViewTests::textRequestRejectsInputOverlap()
{
    QFETCH(int, kind);
    Fixture f; QVERIFY(f.init());
    const QString output = kind == 0 ? f.left : kind == 1 ? f.right + "/missing/nested" : f.temporary.path();
    FolderMergeSession session(f.base, f.left, f.right, output);
    QSignalSpy request(&session, &FolderMergeSession::textMergeRequested);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QString error;
    QVERIFY(!session.requestTextMerge("dir/conflict.txt", &error));
    QVERIFY(!error.isEmpty()); QCOMPARE(request.count(), 0);
    QCOMPARE(readFile(f.left + "/dir/conflict.txt"), QByteArray("left side\n"));
}

void FolderMergeViewTests::textRequestRejectsSymlinkAncestors()
{
#ifdef Q_OS_WIN
    QSKIP("Creating genuine symlinks on Windows requires an enabled privilege; run this case on macOS/Linux.");
#else
    Fixture f; QVERIFY(f.init());
    const QString alias = f.temporary.filePath("output-alias");
    QVERIFY(QFile::link(f.left, alias));
    FolderMergeSession session(f.base, f.left, f.right, alias + "/missing/nested");
    QSignalSpy request(&session, &FolderMergeSession::textMergeRequested);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QString error; QVERIFY(!session.requestTextMerge("dir/conflict.txt", &error));
    QVERIFY(!error.isEmpty()); QCOMPARE(request.count(), 0);
#endif
}

void FolderMergeViewTests::textRequestRejectsSymlinkLeaf()
{
#ifdef Q_OS_WIN
    QSKIP("Creating genuine symlinks on Windows requires an enabled privilege; run this case on macOS/Linux.");
#else
    Fixture f; QVERIFY(f.init());
    QVERIFY(QDir().mkpath(f.output + "/dir"));
    QVERIFY(QFile::link(f.right + "/dir/conflict.txt", f.output + "/dir/conflict.txt"));
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QSignalSpy request(&session, &FolderMergeSession::textMergeRequested);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QString error; QVERIFY(!session.requestTextMerge("dir/conflict.txt", &error));
    QVERIFY(!error.isEmpty()); QCOMPARE(request.count(), 0);
#endif
}

void FolderMergeViewTests::textRequestRejectsDanglingLink()
{
#ifdef Q_OS_WIN
    QSKIP("Creating genuine symlinks on Windows requires an enabled privilege; run this case on macOS/Linux.");
#else
    Fixture f; QVERIFY(f.init());
    QVERIFY(QDir().mkpath(f.output));
    QVERIFY(QFile::link(f.temporary.filePath("does-not-exist"), f.output + "/dir"));
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QSignalSpy request(&session, &FolderMergeSession::textMergeRequested);
    QVERIFY(session.open()); QTRY_VERIFY(!session.isScanning());
    QString error; QVERIFY(!session.requestTextMerge("dir/conflict.txt", &error));
    QVERIFY(!error.isEmpty()); QCOMPARE(request.count(), 0);
#endif
}

void FolderMergeViewTests::cancelledScanRemainsIncomplete()
{
    Fixture f; QVERIFY(f.init());
    for (int i = 0; i < 400; ++i) QVERIFY(writeFile(f.left + QString("/many/%1").arg(i), "left"));
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QSignalSpy finished(&session, &FolderMergeSession::scanFinished);
    QVERIFY(session.open()); session.cancelScan();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(session.plan().cancelled); QVERIFY(!session.plan().complete);
    QVERIFY(!session.plan().canExecute());
    for (const auto &entry : session.plan().entries) QVERIFY(entry.decision != Decision::Delete);
    QVERIFY(!QFileInfo::exists(f.output));
}

void FolderMergeViewTests::closeSuppressesQueuedCallbacks()
{
    Fixture f; QVERIFY(f.init());
    FolderMergeSession session(f.base, f.left, f.right, f.output);
    QWidget owner; auto *view = session.createWidget(&owner);
    QSignalSpy finished(&session, &FolderMergeSession::scanFinished);
    QSignalSpy changed(&session, &FolderMergeSession::planChanged);
    QVERIFY(session.open());
    session.close();
    const int before = changed.count();
    QTest::qWait(100);
    QCOMPARE(session.state(), CompareSession::State::Closed);
    QVERIFY(!session.isScanning()); QVERIFY(!view->isEnabled());
    QCOMPARE(finished.count(), 0); QCOMPARE(changed.count(), before);
    QString error;
    QVERIFY(!session.setDecision("dir/conflict.txt", Decision::Ignore, DirectoryPolicy::RejectManualConflicts, &error));
    QVERIFY(!session.setPaths(f.base, f.left, f.right, f.output, &error));
}

void FolderMergeViewTests::viewSurvivesSessionDestruction()
{
    Fixture f; QVERIFY(f.init());
    QWidget owner;
    auto *session = new FolderMergeSession(f.base, f.left, f.right, f.output);
    QPointer<QWidget> view(session->createWidget(&owner));
    QVERIFY(session->open());
    delete session;
    QVERIFY(view); QVERIFY(!view->isEnabled());
    QCoreApplication::processEvents();
}

void FolderMergeViewTests::emptySessionCanChooseSources()
{
    FolderMergeSession session;
    QVERIFY(session.open()); QVERIFY(!session.isScanning());
    Fixture f; QVERIFY(f.init());
    QVERIFY(session.setPaths(f.base, f.left, f.right, f.output));
    QVERIFY(session.rescan()); QTRY_VERIFY(!session.isScanning());
    QCOMPARE(session.plan().unresolvedCount(), 1);
}

QTEST_MAIN(FolderMergeViewTests)
#include "tst_foldermergeview.moc"
