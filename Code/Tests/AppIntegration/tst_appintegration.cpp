#include "MainWindow.h"
#include "archivecomparesession.h"
#include "clioptions.h"
#include "commandregistry.h"
#include "foldercomparesession.h"
#include "hexcomparesession.h"
#include "homepage.h"
#include "sessionarea.h"
#include "sessiondocument.h"
#include "tablecomparesession.h"
#include "textcomparesession.h"
#include "textcompareview.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace LqCompare;

namespace {
void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
        qFatal("Could not write the application fixture");
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
SessionArea *sessions(MainWindow &window) { return window.findChild<SessionArea *>(); }
QAction *qatAction(MainWindow &window, const QString &id)
{
    auto *bar = window.ribbonBar()->quickAccessBar();
    if (!bar) return nullptr;
    for (QAction *action : bar->actions())
        if (action->property("commandId").toString() == id) return action;
    return nullptr;
}
void clickNextPrompt(QMessageBox::StandardButton choice, bool *shown)
{
    QTimer::singleShot(0, [choice, shown] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box || !box->button(choice)) qFatal("Expected the application's save/discard/cancel prompt");
        *shown = true;
        box->button(choice)->click();
    });
}
QJsonArray recentSessions()
{
    return QJsonDocument::fromJson(QSettings().value(QStringLiteral("sessions/recent")).toByteArray()).array();
}
}

class AppIntegrationTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_settings;
    QString m_oldOrganization;
    QString m_oldApplication;

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        m_oldOrganization = QCoreApplication::organizationName();
        m_oldApplication = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("LqCompareIntegrationTests"));
        QCoreApplication::setApplicationName(QStringLiteral("IsolatedApplication"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings;
        QVERIFY(settings.fileName().startsWith(m_settings.path()));
    }
    void init()
    {
        CommandRegistry::instance().clear();
        QSettings settings;
        settings.clear();
        settings.sync();
    }
    void cleanup()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QVERIFY(!QApplication::activeModalWidget());
        CommandRegistry::instance().clear();
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_oldOrganization);
        QCoreApplication::setApplicationName(m_oldApplication);
    }

    void homeAvailabilityAndQuickAccessAreReal()
    {
        MainWindow window;
        auto *area = sessions(window);
        QVERIFY(area);
        QCOMPARE(area->sessionCount(), 0);
        QVERIFY(area->isHomeCurrent());
        for (const QString &id : {QStringLiteral("text"), QStringLiteral("folder"), QStringLiteral("hex"),
                                  QStringLiteral("table"), QStringLiteral("archive")}) {
            auto *button = area->homePage()->findChild<QPushButton *>(QStringLiteral("newSession-") + id);
            QVERIFY2(button, qPrintable(id));
            QVERIFY2(button->isEnabled(), qPrintable(id));
        }
        // Unsupported catalog entries remain visible as disabled choices.
        for (const QString &id : {QStringLiteral("registry"), QStringLiteral("version")}) {
            auto *button = area->homePage()->findChild<QPushButton *>(QStringLiteral("newSession-") + id);
            QVERIFY2(button, qPrintable(id));
            QVERIFY2(!button->isEnabled(), qPrintable(id));
        }
        const QStringList ids = {"file.open", "file.save", "edit.undo", "edit.redo", "nav.prevdiff", "nav.nextdiff"};
        for (const auto &id : ids) {
            QAction *action = qatAction(window, id);
            QVERIFY2(action, qPrintable(id));
            QVERIFY(action->shortcuts().isEmpty()); // Keyboard dispatch belongs to the shared binder.
        }
        QVERIFY(qatAction(window, "file.open")->isEnabled());
        QVERIFY(!qatAction(window, "file.save")->isEnabled());
        QVERIFY(!qatAction(window, "edit.undo")->isEnabled());
        QVERIFY(!qatAction(window, "nav.nextdiff")->isEnabled());
    }

    void opensTextFolderHexTableAndArchiveInActualTabs()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "a\nleft\nend\n"); writeFile(right, "a\nright\nend\n");
        MainWindow window;
        auto *area = sessions(window);
        QVERIFY(window.openPaths({left, right}));
        auto *text = qobject_cast<TextCompareSession *>(area->currentSession());
        QVERIFY(text); QCOMPARE(text->state(), CompareSession::State::Open);
        // `left` 与 `right` 的相似度只有 22（只有一个公共字符 `t`），低于出厂阈值 50，
        // 于是这一处是「删除 + 新增」两块——但仍然是**一处改动**（块下标连续）。
        // 两个数都断言：差异块数是差异引擎的口径，「一处改动」是导航/状态栏的口径。
        QCOMPARE(text->comparison().differences.size(), 2);
        QCOMPARE(text->differenceCount(), 1);
        QVERIFY(text->widget()->findChild<TextPane *>("leftTextPane"));
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_APP_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            window.resize(1440, 900); window.show(); QTest::qWait(100);
            QVERIFY(window.grab().save(screenshot));
        }

        const auto leftDir = directory.filePath("folder-left"), rightDir = directory.filePath("folder-right");
        QVERIFY(QDir().mkpath(leftDir)); QVERIFY(QDir().mkpath(rightDir));
        writeFile(leftDir + "/item.txt", "left"); writeFile(rightDir + "/item.txt", "right");
        QVERIFY(window.openPaths({leftDir, rightDir}));
        auto *folder = qobject_cast<FolderCompareSession *>(area->currentSession());
        QVERIFY(folder); QCOMPARE(folder->state(), CompareSession::State::Open);
        QTRY_VERIFY_WITH_TIMEOUT(!folder->isScanning(), 5000);
        QVERIFY(!folder->result().entries.isEmpty());

        const auto leftBin = directory.filePath("left.bin"), rightBin = directory.filePath("right.bin");
        writeFile(leftBin, QByteArray::fromHex("00010203")); writeFile(rightBin, QByteArray::fromHex("00010903"));
        QVERIFY(window.openPaths({leftBin, rightBin}));
        auto *hex = qobject_cast<HexCompareSession *>(area->currentSession());
        QVERIFY(hex); QCOMPARE(hex->state(), CompareSession::State::Open);

        const auto leftCsv = directory.filePath("left.csv"), rightCsv = directory.filePath("right.csv");
        writeFile(leftCsv, "id,value\n1,left\n"); writeFile(rightCsv, "id,value\n1,right\n");
        QVERIFY(window.openPaths({leftCsv, rightCsv}, QStringLiteral("table")));
        auto *table = qobject_cast<TableCompareSession *>(area->currentSession());
        QVERIFY(table); QCOMPARE(table->state(), CompareSession::State::Open);

        const QString archiveFixtures = QFINDTESTDATA("../Archive/fixtures");
        QVERIFY(!archiveFixtures.isEmpty());
        const auto leftZip = directory.filePath("left.zip"), rightZip = directory.filePath("right.zip");
        QVERIFY(QFile::copy(archiveFixtures + "/compare-left.zip", leftZip));
        QVERIFY(QFile::copy(archiveFixtures + "/compare-right.zip", rightZip));
        QVERIFY(window.openPaths({leftZip, rightZip}));
        auto *archive = qobject_cast<ArchiveCompareSession *>(area->currentSession());
        QVERIFY(archive); QCOMPARE(archive->state(), CompareSession::State::Open);
        QVERIFY(archive->hasComparison());
        QCOMPARE(area->sessionCount(), 5);
        QVERIFY(area->closeAllSessions());
        QCOMPARE(area->sessionCount(), 0);
    }

    void activeTabDirtyStateAndQatSaveTrackCurrentBuffer()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "left\n"); writeFile(right, "right\n");
        MainWindow window;
        auto *first = qobject_cast<TextCompareSession *>(window.openComparison("text", left, right));
        QVERIFY(first);
        QVERIFY(first->setText(false, "edited\n"));
        QVERIFY(qatAction(window, "file.save")->isEnabled());
        QVERIFY(qatAction(window, "edit.undo")->isEnabled());
        auto *second = window.openComparison("text", left, right);
        QVERIFY(second);
        QVERIFY(!qatAction(window, "file.save")->isEnabled());
        QVERIFY(!qatAction(window, "edit.undo")->isEnabled());
        auto *area = sessions(window);
        area->setCurrentWidget(first->widget());
        QCOMPARE(area->currentSession(), static_cast<CompareSession *>(first));
        QVERIFY(qatAction(window, "file.save")->isEnabled());
        qatAction(window, "file.save")->trigger();
        QCOMPARE(readFile(right), QByteArray("edited\n"));
        QVERIFY(!first->isDirty());
        QVERIFY(!qatAction(window, "file.save")->isEnabled());
        area->setCurrentWidget(area->homePage());
        QVERIFY(!qatAction(window, "nav.nextdiff")->isEnabled());
        QVERIFY(!qatAction(window, "edit.undo")->isEnabled());
    }

    void dirtyWindowCloseCancelThenTabSaveClosesSafely()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("editable.txt");
        writeFile(path, "original\n");
        MainWindow window;
        window.resize(1280, 800); window.show();
        auto *text = qobject_cast<TextCompareSession *>(window.openComparison("text", {}, path));
        QVERIFY(text); QVERIFY(text->setText(false, "saved at close\n"));
        QPointer<CompareSession> tracked(text);
        bool cancelPromptShown = false;
        clickNextPrompt(QMessageBox::Cancel, &cancelPromptShown);
        QVERIFY(!window.close());
        QVERIFY(cancelPromptShown);
        QVERIFY(window.isVisible());
        QCOMPARE(sessions(window)->sessionCount(), 1);
        QCOMPARE(readFile(path), QByteArray("original\n"));
        bool savePromptShown = false;
        clickNextPrompt(QMessageBox::Save, &savePromptShown);
        QVERIFY(CommandRegistry::instance().trigger(QStringLiteral("session.close")));
        QVERIFY(savePromptShown);
        QCOMPARE(readFile(path), QByteArray("saved at close\n"));
        QCOMPARE(sessions(window)->sessionCount(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(tracked.isNull());
    }

    void sessionDefinitionRestoresEncodingBeforeOpening()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, QByteArray::fromHex("636166e90d0a"));
        writeFile(right, QByteArray::fromHex("434146c90a"));
        SessionDocument document;
        document.typeId = "text"; document.leftPath = left; document.rightPath = right;
        document.settings.insert("text.leftEncoding", QStringLiteral("ISO-8859-1"));
        document.settings.insert("text.rightEncoding", QStringLiteral("ISO-8859-1"));
        document.settings.insert("text.ignoreCase", true);
        document.settings.insert("text.ignoreEol", true);
        const auto definition = directory.filePath("comparison.lqc");
        QString error;
        QVERIFY2(document.save(definition, &error), qPrintable(error));
        MainWindow window;
        QVERIFY(window.openSessionFile(definition));
        auto *text = qobject_cast<TextCompareSession *>(sessions(window)->currentSession());
        QVERIFY(text); QCOMPARE(text->state(), CompareSession::State::Open);
        QCOMPARE(text->leftDocument().codecName(), QByteArray("ISO-8859-1"));
        QCOMPARE(text->leftDocument().decodingErrors(), 0);
        QVERIFY(text->leftDocument().canEdit());
        QCOMPARE(text->leftDocument().normalizedText(), QString::fromUtf8(QByteArray::fromHex("636166c3a90a")));
        QCOMPARE(text->comparison().differences.size(), 0);
        QVERIFY(text->comparison().ignoredBlocks > 0);
        QCOMPARE(text->property("definitionFile").toString(), definition);
    }

    void commandLineReadOnlySurvivesUiAndPublicApi()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("left.txt"), "source\n");
        writeFile(directory.filePath("right.txt"), "destination\n");
        MainWindow window;
        Cli::Request request;
        request.sessionType = "text"; request.left = "left.txt"; request.right = "right.txt";
        request.leftReadOnly = true; request.rightReadOnly = true;
        QVERIFY(window.openRequest(request, directory.path()));
        auto *text = qobject_cast<TextCompareSession *>(sessions(window)->currentSession());
        QVERIFY(text); QVERIFY(text->isSideReadOnly(true)); QVERIFY(text->isSideReadOnly(false));
        QString error;
        QVERIFY(!text->setText(false, "must not write\n", &error));
        QVERIFY(!text->copyDifference(true, &error));
        QVERIFY(!text->saveSideAs(false, directory.filePath("forbidden.txt"), true, &error));
        QVERIFY(!qatAction(window, "file.save")->isEnabled());
        QVERIFY(!CommandRegistry::instance().find("file.editable")->enabled);
        QVERIFY(!CommandRegistry::instance().trigger("file.editable"));
        QVERIFY(!CommandRegistry::instance().find("file.saveas")->enabled);
        QVERIFY(!text->widget()->findChild<QPushButton *>("editRight")->isEnabled());
        QVERIFY(!QFile::exists(directory.filePath("forbidden.txt")));
        QCOMPARE(readFile(directory.filePath("right.txt")), QByteArray("destination\n"));
    }

    void unifiedDifferenceShortcutsHaveOneActiveBinding()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "a\nleft one\nb\nleft two\nc\n");
        writeFile(right, "a\nright one\nb\nright two\nc\n");
        MainWindow window;
        window.resize(1280, 800); window.show(); window.activateWindow();
        auto *text = qobject_cast<TextCompareSession *>(window.openComparison("text", left, right));
        QVERIFY(text); QCOMPARE(text->comparison().differences.size(), 2);
        QVERIFY(!text->usesLocalShortcuts());
        auto *pane = text->widget()->findChild<TextPane *>("rightTextPane");
        QVERIFY(pane); pane->setFocus();
        QTest::qWait(50);
        QShortcut *next = nullptr;
        int activeNext = 0, activePrevious = 0;
        for (QShortcut *shortcut : window.findChildren<QShortcut *>()) {
            if (!shortcut->isEnabled()) continue;
            if (shortcut->key() == QKeySequence(Qt::Key_F8)) { ++activeNext; next = shortcut; }
            if (shortcut->key() == QKeySequence(Qt::Key_F7)) ++activePrevious;
        }
        QCOMPARE(activeNext, 1); QCOMPARE(activePrevious, 1); QVERIFY(next);
        QSignalSpy ambiguous(next, &QShortcut::activatedAmbiguously);
        QSignalSpy activated(next, &QShortcut::activated);
        text->firstDifference();
        QTest::keyClick(pane, Qt::Key_F8);
        QTRY_COMPARE_WITH_TIMEOUT(text->currentDifference(), 1, 1000);
        QCOMPARE(activated.count(), 1); QCOMPARE(ambiguous.count(), 0);
        QTest::keyClick(pane, Qt::Key_F7);
        QTRY_COMPARE_WITH_TIMEOUT(text->currentDifference(), 0, 1000);
    }

    void transientComparisonsNeverEnterRecentHistory()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("snapshot-left.txt"), right = directory.filePath("snapshot-right.txt");
        writeFile(left, "old\n"); writeFile(right, "new\n");
        MainWindow window;
        QVERIFY(recentSessions().isEmpty());
        auto *snapshot = window.openComparison("text", left, right, true);
        QVERIFY(snapshot); QVERIFY(snapshot->property("transientSource").toBool());
        QVERIFY(recentSessions().isEmpty());
        QVERIFY(sessions(window)->closeAllSessions());
        QVERIFY(window.openComparison("text", left, right));
        QCOMPARE(recentSessions().size(), 1);
    }

    void implementedCommandIconsRenderFromPackagedResources()
    {
        MainWindow window;
        for (const auto &command : CommandRegistry::instance().all()) {
            if (!command.handler) continue;
            const QString detail = command.id + QStringLiteral(": ") + command.icon;
            QVERIFY2(QFile::exists(command.icon), qPrintable(detail));
            QVERIFY2(!QIcon(command.icon).pixmap(20, 20).isNull(), qPrintable(detail));
        }
    }

    void destroyingWindowWithFolderWorkDoesNotLeaveSessionsAlive()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left"), right = directory.filePath("right");
        QVERIFY(QDir().mkpath(left)); QVERIFY(QDir().mkpath(right));
        for (int i = 0; i < 200; ++i) {
            const QString name = QStringLiteral("/entry-%1.txt").arg(i);
            writeFile(left + name, "left contents\n");
            writeFile(right + name, "right contents\n");
        }
        QScopedPointer<MainWindow> window(new MainWindow);
        auto *folder = qobject_cast<FolderCompareSession *>(window->openComparison("folder", left, right));
        QVERIFY(folder);
        QPointer<FolderCompareSession> tracked(folder);
        // Do not wait for the scan: window destruction owns worker cancellation.
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QVERIFY(tracked.isNull());
    }

    // TXT-010 第 4 条的后半句：「（行尾）混合时给出警告图标」。
    // 图标住在状态栏里、由 `CompareSession::StatusSeverity` 驱动，所以这一条
    // 必须从**真窗口**上验：会话那一层的严重度在 `Tests/TextView` 已经钉住，
    // 但「严重度变了，图标到底亮没亮」只在这条链上（会话 -> SessionArea -> MainWindow）。
    void statusBarWarningIconFollowsMixedLineEndings()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        const auto mixed = directory.filePath("mixed.txt");
        writeFile(left, "one\ntwo\n");
        writeFile(right, "one\ntwo\n");
        writeFile(mixed, "one\r\ntwo\n"); // LF + CRLF → 混合

        MainWindow window;
        window.resize(1280, 800);
        window.show();
        auto *icon = window.findChild<QLabel *>(QStringLiteral("statusWarningIcon"));
        QVERIFY(icon);
        // 图标在装配时就设好了，之后切换的只是可见性。若改成「亮的时候才设 pixmap」，
        // 这里会红——而那个写法的代价是状态栏右侧每次刷新都重建一遍 pixmap。
        const QPixmap *pixmap = icon->pixmap();
        QVERIFY(pixmap && !pixmap->isNull());

        auto *text = qobject_cast<TextCompareSession *>(window.openComparison("text", left, right));
        QVERIFY(text);
        QCoreApplication::processEvents();
        // 两侧都是纯 LF → 不警告、图标不出现。
        QCOMPARE(text->statusSeverity(), CompareSession::StatusSeverity::Normal);
        QVERIFY(!icon->isVisible());

        // 把左侧换成混合行尾的文件 → 图标出现。
        QVERIFY(text->setPaths(mixed, right));
        QCOMPARE(text->statusSeverity(), CompareSession::StatusSeverity::Warning);
        QVERIFY(icon->isVisible());

        // 换回去 → 图标灭。只验「亮」不验「灭」，一个「一旦警告就回不去」的
        // 实现照样全绿，而用户看到的是一个永远亮着的警告。
        QVERIFY(text->setPaths(left, right));
        QCOMPARE(text->statusSeverity(), CompareSession::StatusSeverity::Normal);
        QVERIFY(!icon->isVisible());

        // **切标签**这条链单独走一遍：严重度是**当前**会话的属性，两个会话
        // 一混合一干净，来回切必须跟着变。`MainWindow::refreshStatusBar()` 与
        // `SessionArea` 的 `currentChanged` 转发各自都只负责一半，
        // 少任何一半，现象都是「切过去图标还留着上一个会话的状态」。
        auto *clean = qobject_cast<TextCompareSession *>(window.openComparison("text", left, right));
        QVERIFY(clean); // 新标签自动成为当前会话（两侧都干净 → 图标不该亮）
        QCoreApplication::processEvents();
        QVERIFY(!icon->isVisible());

        auto *mixedSession = qobject_cast<TextCompareSession *>(window.openComparison("text", mixed, right));
        QVERIFY(mixedSession);
        QCoreApplication::processEvents();
        QVERIFY(icon->isVisible());

        auto *area = sessions(window);
        area->setCurrentIndex(area->indexOf(clean->widget()));
        QCoreApplication::processEvents();
        QVERIFY2(!icon->isVisible(), "切到干净会话后图标必须灭");

        area->setCurrentIndex(area->indexOf(mixedSession->widget()));
        QCoreApplication::processEvents();
        QVERIFY2(icon->isVisible(), "切回混合会话后图标必须回来");

        // 关掉所有会话（回到 Home 页）时也不该留着上一个会话的图标。
        QVERIFY(area->closeAllSessions());
        QCoreApplication::processEvents();
        QVERIFY(!icon->isVisible());
    }
};

QTEST_MAIN(AppIntegrationTests)
#include "tst_appintegration.moc"
