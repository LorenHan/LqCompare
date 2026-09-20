#include <QtTest>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include "versioncomparesession.h"
#include "pefixtures.h"
using namespace LqCompare;
namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}
class VersionViewTests : public QObject {
    Q_OBJECT
private slots:
    void emptySessionAndReadOnly();
    void asynchronousPeComparisonAndView();
    void nonPeMetadataAndExport();
    void reloadFailureRetainsComparison();
    void pathFailureRestoresDisplayedSources();
    void closedWorkerCannotPublish();
    void destroyedSessionWithWorker();
};
void VersionViewTests::emptySessionAndReadOnly()
{
    VersionCompareSession session;
    QCOMPARE(session.typeId(), QStringLiteral("version"));
    QString error; QVERIFY(session.open(&error)); QVERIFY(!session.isBusy()); QVERIFY(!session.isLoaded());
    QScopedPointer<QWidget> view(session.createWidget()); QVERIFY(view);
    QCOMPARE(session.createWidget(), view.data());
    QVERIFY(view->findChild<QLineEdit *>(QStringLiteral("versionLeftPath")));
    session.setDirty(true); QVERIFY(!session.canSave()); QVERIFY(!session.save(&error)); QVERIFY(!error.isEmpty());
    QVERIFY(!session.setPaths(QString(), QStringLiteral("x"), &error));
    QVERIFY(!session.exportCsv(QStringLiteral("unused.csv"), &error));
}
void VersionViewTests::asynchronousPeComparisonAndView()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString left = dir.filePath(QStringLiteral("left.dll")), right = dir.filePath(QStringLiteral("right.dll"));
    const QByteArray leftBytes = PeFixtures::image(), rightBytes = PeFixtures::image(true, true, true);
    QVERIFY(write(left, leftBytes)); QVERIFY(write(right, rightBytes));
    VersionCompareSession session;
    QVERIFY(session.open());
    QScopedPointer<QWidget> view(session.createWidget());
    view->findChild<QLineEdit *>(QStringLiteral("versionLeftPath"))->setText(left);
    view->findChild<QLineEdit *>(QStringLiteral("versionRightPath"))->setText(right);
    QSignalSpy finished(&session, &VersionCompareSession::loadFinished);
    auto *button = view->findChild<QPushButton *>(QStringLiteral("versionCompareButton"));
    button->click(); QVERIFY(session.isBusy()); QVERIFY(!button->isEnabled());
    QString error; QVERIFY(!session.setPaths(left, right, &error)); QVERIFY(!error.isEmpty());
    QTRY_COMPARE(finished.size(), 1); QVERIFY(finished.at(0).at(0).toBool());
    QVERIFY(session.isLoaded()); QVERIFY(!session.isBusy()); QVERIFY(button->isEnabled());
    QCOMPARE(session.leftInfo().versions.size(), 2); QVERIFY(session.rightInfo().pe32Plus);
    auto *tree = view->findChild<QTreeWidget *>(QStringLiteral("versionDifferenceTable")); QVERIFY(tree);
    QCOMPARE(tree->columnCount(), 5); QVERIFY(tree->topLevelItemCount() >= 7);
    QVERIFY(!session.rows().isEmpty());
    bool hasLeftOnly = false;
    for (const auto &row : session.rows()) if (row.difference == Version::Difference::LeftOnly) hasLeftOnly = true;
    QVERIFY(hasLeftOnly);
    tree->topLevelItem(0)->setExpanded(false);
    view->findChild<QCheckBox *>(QStringLiteral("versionIgnoreNumbers"))->setChecked(true);
    QVERIFY(session.options().ignoreVersionNumbers); QVERIFY(!tree->topLevelItem(0)->isExpanded());
    QVERIFY(!session.canSave());
    const QString screenshot = qEnvironmentVariable("LQ_VERSION_TEST_CAPTURE");
    if (!screenshot.isEmpty()) {
        view->resize(1440, 900); view->show();
        QTest::qWait(30);
        QVERIFY(view->grab().save(screenshot));
    }
    QCOMPARE(read(left), leftBytes); QCOMPARE(read(right), rightBytes);
}
void VersionViewTests::nonPeMetadataAndExport()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString left = dir.filePath(QStringLiteral("plain.txt")), right = dir.filePath(QStringLiteral("plain2.txt"));
    QVERIFY(write(left, QByteArrayLiteral("plain left"))); QVERIFY(write(right, QByteArrayLiteral("plain right")));
    VersionCompareSession session(left, right); QSignalSpy finished(&session, &VersionCompareSession::loadFinished);
    QVERIFY(session.open()); QTRY_COMPARE(finished.size(), 1); QVERIFY(finished.at(0).at(0).toBool());
    QCOMPARE(session.leftInfo().status, Version::Status::NonPe); QVERIFY(session.leftInfo().versions.isEmpty());
    QVERIFY(session.statusText().contains(QStringLiteral("no version resource")));
    QString error; QVERIFY(!session.exportCsv(left, &error)); QVERIFY(error.contains(QStringLiteral("input")));
    const QString csv = dir.filePath(QStringLiteral("report.csv")); QVERIFY(session.exportCsv(csv, &error));
    QVERIFY(read(csv).contains("\"File metadata\"")); QVERIFY(read(csv).contains("No version resource"));
    QCOMPARE(read(left), QByteArrayLiteral("plain left")); QCOMPARE(read(right), QByteArrayLiteral("plain right"));
}
void VersionViewTests::reloadFailureRetainsComparison()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString left = dir.filePath(QStringLiteral("left.exe")), right = dir.filePath(QStringLiteral("right.exe"));
    QVERIFY(write(left, PeFixtures::image())); QVERIFY(write(right, PeFixtures::image()));
    VersionCompareSession session(left, right); QSignalSpy finished(&session, &VersionCompareSession::loadFinished);
    QSignalSpy errors(&session, &CompareSession::errorReported);
    QVERIFY(session.open()); QTRY_COMPARE(finished.size(), 1); QVERIFY(session.isLoaded());
    const int oldRows = session.rows().size();
    QVERIFY(write(right, QByteArrayLiteral("MZbroken")));
    QVERIFY(session.reload()); QTRY_COMPARE(finished.size(), 2); QVERIFY(!finished.at(1).at(0).toBool());
    QVERIFY(!finished.at(1).at(1).toString().isEmpty()); QCOMPARE(errors.size(), 1);
    QCOMPARE(session.state(), CompareSession::State::Open); QVERIFY(session.isLoaded()); QCOMPARE(session.rows().size(), oldRows);
    QCOMPARE(session.rightInfo().status, Version::Status::Pe);
    QVERIFY(session.statusText().contains(QStringLiteral("previous comparison retained")));
    QVERIFY(write(right, PeFixtures::image(true))); QVERIFY(session.reload());
    QTRY_COMPARE(finished.size(), 3); QVERIFY(finished.at(2).at(0).toBool()); QVERIFY(session.rightInfo().pe32Plus);
}
void VersionViewTests::pathFailureRestoresDisplayedSources()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString left = dir.filePath(QStringLiteral("left.dll")), right = dir.filePath(QStringLiteral("right.dll"));
    QVERIFY(write(left, PeFixtures::image())); QVERIFY(write(right, PeFixtures::image()));
    VersionCompareSession session(left, right); QSignalSpy finished(&session, &VersionCompareSession::loadFinished);
    QVERIFY(session.open()); QTRY_COMPARE(finished.size(), 1); QVERIFY(session.isLoaded());
    QScopedPointer<QWidget> view(session.createWidget());
    auto *rightEdit = view->findChild<QLineEdit *>(QStringLiteral("versionRightPath"));
    rightEdit->setText(dir.filePath(QStringLiteral("missing.dll")));
    view->findChild<QPushButton *>(QStringLiteral("versionCompareButton"))->click();
    QTRY_COMPARE(finished.size(), 2); QVERIFY(!finished.at(1).at(0).toBool());
    QCOMPARE(rightEdit->text(), right); QCOMPARE(session.rightPath(), right);
    QVERIFY(session.isLoaded()); QVERIFY(!session.rows().isEmpty());
    QVERIFY(!view->findChild<QLabel *>(QStringLiteral("versionError"))->text().isEmpty());
}
void VersionViewTests::closedWorkerCannotPublish()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath(QStringLiteral("pe.dll"));
    QVERIFY(write(path, PeFixtures::image()));
    VersionCompareSession session(path, path); QSignalSpy finished(&session, &VersionCompareSession::loadFinished);
    QVERIFY(session.open()); session.close();
    QCOMPARE(session.state(), CompareSession::State::Closed); QVERIFY(!session.isLoaded()); QVERIFY(!session.isBusy());
    QTest::qWait(100); QCOMPARE(finished.size(), 0); QVERIFY(session.rows().isEmpty()); QVERIFY(!session.setPaths(path, path));
}
void VersionViewTests::destroyedSessionWithWorker()
{
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath(QStringLiteral("pe.dll"));
    QVERIFY(write(path, PeFixtures::image()));
    auto *session = new VersionCompareSession(path, path); QVERIFY(session->open()); delete session;
    QTest::qWait(100); // Worker owns paths/results, never the deleted QObject.
}
QTEST_MAIN(VersionViewTests)
#include "tst_versionview.moc"
