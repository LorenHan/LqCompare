#include "MainWindow.h"
#include "archivecomparesession.h"
#include "clioptions.h"
#include "commandregistry.h"
#include "foldercomparesession.h"
#include "hexcomparesession.h"
#include "homepage.h"
#include "optionsrepository.h"
#include "optionsruntime.h"
#include "sessionarea.h"
#include "sessiondocument.h"
#include "tablecomparesession.h"
#include "textcomparesession.h"
#include "textcompareview.h"
#include "vcsavailability.h"
#include "vcsbackend.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QShortcut>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>

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

// 只在需要遍历所有控件的测试内模拟完整键盘导航，离开作用域即恢复平台偏好。
struct FullKeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    FullKeyboardNavigation() { QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls); }
    ~FullKeyboardNavigation() { QGuiApplication::styleHints()->setTabFocusBehavior(previous); }
};

enum class SearchConfirmation { Button, Return, RepeatedReturn, KeyboardYes };
struct SearchResult { int dialogs = 0; QString text; Qt::TextFormat format = Qt::AutoText; };

SearchResult submitSearch(QLineEdit *search, const QString &query, QMessageBox::StandardButton answer,
                          bool escape = false, bool reenter = false,
                          const std::function<void()> &beforeAnswer = {},
                          SearchConfirmation confirmation = SearchConfirmation::Button,
                          Qt::Key searchKey = Qt::Key_Return, bool mouseSubmit = false)
{
    SearchResult result;
    QTimer responder;
    responder.setInterval(1);
    QObject::connect(&responder, &QTimer::timeout, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        // macOS 按平台规范忽略 QMessageBox 标题，仍严格校验拥有者、按钮与正文。
        // https://doc.qt.io/archives/qt-5.15/qmessagebox.html#setWindowTitle
        const auto buttons = answer == QMessageBox::Ok ? QMessageBox::StandardButtons(QMessageBox::Ok)
            : QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No);
        const bool expectedBody = buttons == QMessageBox::Ok
            ? (box->text() == QStringLiteral("No command matches \"%1\".").arg(query.trimmed())
               || box->text().startsWith(QStringLiteral("Matching commands:\n\n")))
            : (box->text().startsWith(QStringLiteral("Command: "))
               && box->text().endsWith(QStringLiteral("\n\nRun it now?")));
        if (box->parentWidget() != search->window() || box->standardButtons() != buttons || !expectedBody)
            qFatal("Unexpected command-search dialog: title=%s buttons=%x text=%s",
                   qPrintable(box->windowTitle()), int(box->standardButtons()), qPrintable(box->text()));
#ifndef Q_OS_MACOS
        if (box->windowTitle() != QStringLiteral("Command Search"))
            qFatal("Unexpected command-search title: %s", qPrintable(box->windowTitle()));
#endif
        ++result.dialogs; result.text = box->text(); result.format = box->textFormat();
        if (reenter) {
            // 直接向后方控件发送嵌套输入，验证模态期间也不会重复打开确认框。
            QTest::keyClick(search, Qt::Key_Return);
        }
        if (beforeAnswer) beforeAnswer();
        if (confirmation == SearchConfirmation::KeyboardYes) {
            FullKeyboardNavigation navigation;
            auto *no = box->button(QMessageBox::No), *yes = box->button(QMessageBox::Yes);
            box->activateWindow(); QCoreApplication::processEvents(); no->setFocus();
            for (int i = 0; i < 8 && !yes->hasFocus(); ++i)
                QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : box, Qt::Key_Tab);
            if (!yes->hasFocus()) qFatal("Keyboard focus did not reach Yes");
            QTest::keyClick(yes, Qt::Key_Space);
        } else if (confirmation != SearchConfirmation::Button) {
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(),
                            confirmation == SearchConfirmation::RepeatedReturn, 1);
            QApplication::sendEvent(box, &enter);
        }
        else if (escape) QTest::keyClick(box, Qt::Key_Escape);
        else box->button(answer)->click();
    });
    // 嵌套模态循环也有明确超时，失败不能把整个测试套件挂住。
    QTimer::singleShot(2000, &responder, [] { qFatal("Command search did not return"); });
    responder.start();
    search->setFocus(); search->clear(); QTest::keyClicks(search, query);
    if (mouseSubmit) {
        const auto buttons = search->findChildren<QAbstractButton *>();
        if (buttons.size() != 1 || !buttons.first()->isVisible()) qFatal("Expected visible search button");
        QTest::mouseClick(buttons.first(), Qt::LeftButton);
    } else QTest::keyClick(search, searchKey);
    QCoreApplication::processEvents();
    responder.stop();
    return result;
}

double contrast(const QColor &a, const QColor &b)
{
    const auto luminance = [](const QColor &color) {
        const auto linear = [](double value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF())
                + 0.0722 * linear(color.blueF());
    };
    const double x = luminance(a), y = luminance(b);
    return (qMax(x, y) + 0.05) / (qMin(x, y) + 0.05);
}

QColor tabBackground(QTabBar *tabs, int index)
{
    const QPixmap image = tabs->window()->grab();
    const QPoint origin = tabs->mapTo(tabs->window(), tabs->tabRect(index).topLeft() + QPoint(4, 4));
    return image.toImage().pixelColor(origin * image.devicePixelRatio());
}

// QStyleSheetStyle 将实际文字及解析后的颜色交给原生样式 drawItemText。
// 只记录并原样转发；继续使用真实栅格引擎，支持 macOS 的原生焦点绘制。
class TabTextPaintStyle : public QProxyStyle
{
public:
    explicit TabTextPaintStyle(QStyle *nativeStyle) : QProxyStyle(nativeStyle) {}
    QString label;
    mutable QList<QColor> colors;
    void drawItemText(QPainter *painter, const QRect &rect, int flags,
                      const QPalette &palette, bool enabled, const QString &text,
                      QPalette::ColorRole role = QPalette::NoRole) const override
    {
        if (text == label)
            colors.append(role == QPalette::NoRole ? painter->pen().color()
                          : palette.color(enabled ? QPalette::Normal : QPalette::Disabled, role));
        QProxyStyle::drawItemText(painter, rect, flags, palette, enabled, text, role);
    }
};

// WCAG 1.4.3 比较指定的前景/背景色，不把抗锯齿造成的混色当作前景色：
// https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html
// 同时保留真实栅格的文字证据，排除边框及选中/焦点标记。
void verifyTabPixels(QTabBar *tabs, int index, const QColor &paletteForeground, const QColor &background)
{
    // 产品样式表使用 QColor::name() 的默认 HexRgb，指定色因此是不透明 RGB；
    // macOS 原生 Text 常含 alpha，不能把输入调色板 alpha 当作 CSS 绘制契约。
    const QColor foreground = QColor::fromRgb(paletteForeground.rgb());
    const QImage before = tabs->grab().toImage();
    const QFont font = tabs->font();
    QList<QRect> rectangles;
    for (int i = 0; i < tabs->count(); ++i) rectangles << tabs->tabRect(i);
    QVERIFY(!tabs->testAttribute(Qt::WA_SetStyle));
    QStyle *native = QStyleFactory::create(QApplication::style()->objectName());
    QVERIFY2(native, "The current native style must be available; no substitute style is allowed");
    TabTextPaintStyle probe(native);
    probe.label = tabs->tabText(index);
    struct RestoreStyle {
        QTabBar *tabs;
        ~RestoreStyle() { if (tabs) tabs->setStyle(nullptr); }
    } restore{tabs};
    tabs->setStyle(&probe);
    const QImage observed = tabs->grab().toImage();
    QCOMPARE(observed, before);
    QCOMPARE(tabs->font(), font);
    for (int i = 0; i < tabs->count(); ++i) QCOMPARE(tabs->tabRect(i), rectangles[i]);
    tabs->setStyle(nullptr);
    restore.tabs = nullptr;
    QCOMPARE(tabs->grab().toImage(), before);
    QVERIFY(!tabs->testAttribute(Qt::WA_SetStyle));
    QVERIFY2(!probe.colors.isEmpty(), qPrintable("No full text paint for " + tabs->tabText(index)));
    for (const QColor &painted : probe.colors) {
        QCOMPARE(painted.rgba(), foreground.rgba());
        QVERIFY2(contrast(painted, background) >= 4.5,
                 qPrintable(QStringLiteral("%1: foreground/background contrast %2:1 is below 4.5:1")
                            .arg(tabs->tabText(index)).arg(contrast(painted, background))));
    }
    // 抓取完整窗口，以用户实际看到的背景测量透明标签，
    // 避免把空的透明位图误当作标签背景。
    const QPixmap pixmap = tabs->window()->grab();
    const QImage image = pixmap.toImage();
    const qreal scale = pixmap.devicePixelRatio();
    const QRect tab = tabs->tabRect(index).translated(tabs->mapTo(tabs->window(), QPoint()));
    QCOMPARE(image.pixelColor((tab.left() + 4) * scale, (tab.top() + 4) * scale).rgba(), background.rgba());
    const QRect text = tab.adjusted(10, 5, -10, -5);
    int glyphPixels = 0;
    double bestContrast = 1;
    for (int y = text.top() * scale; y <= text.bottom() * scale; ++y) {
        for (int x = text.left() * scale; x <= text.right() * scale; ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const double ratio = contrast(pixel, background);
            bestContrast = qMax(bestContrast, ratio);
            if (qAbs(pixel.red() - foreground.red()) < 32
                && qAbs(pixel.green() - foreground.green()) < 32
                && qAbs(pixel.blue() - foreground.blue()) < 32)
                ++glyphPixels;
        }
    }
    QVERIFY2(glyphPixels >= 3,
             qPrintable(QStringLiteral("%1: only %2 foreground glyph pixels, best raster contrast %3:1")
                        .arg(tabs->tabText(index)).arg(glyphPixels).arg(bestContrast)));
}
}

class AppIntegrationTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_settings;
    QString m_oldOrganization;
    QString m_oldApplication;
    Qt::TabFocusBehavior m_tabFocusBehavior;

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
        m_tabFocusBehavior = QGuiApplication::styleHints()->tabFocusBehavior();
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
        QCOMPARE(QGuiApplication::styleHints()->tabFocusBehavior(), m_tabFocusBehavior);
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
        // 注册表和版本资源有真实工厂，但目录契约限定为 Windows 专属。
        for (const QString &id : {QStringLiteral("registry"), QStringLiteral("version")}) {
            auto *button = area->homePage()->findChild<QPushButton *>(QStringLiteral("newSession-") + id);
            QVERIFY2(button, qPrintable(id));
#ifdef Q_OS_WIN
            QVERIFY2(button->isEnabled(), qPrintable(id));
#else
            QVERIFY2(!button->isEnabled(), qPrintable(id));
#endif
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

    void ribbonTabsFollowPaletteWithoutChangingNavigation()
    {
        struct RestoreApplication {
            QPalette palette = QApplication::palette();
            QFont font = QApplication::font();
            ~RestoreApplication() { QApplication::setPalette(palette); QApplication::setFont(font); }
        } restore;
        QTemporaryDir directory;
        Settings::OptionsRepository options({directory.path(), false});
        Options::OptionsRuntime runtime(&options);
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *ribbon = window.ribbonBar();
        auto *tabs = ribbon->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
        QVERIFY(tabs);
        QCOMPARE(ribbon->currentPage()->objectName(), QStringLiteral("ribbonHomePage"));
        const Qt::WindowFlags flags = window.windowFlags();
        const bool frameTheme = ribbon->isFrameThemeEnabled();
        const QRect contentGeometry = sessions(window)->geometry();
        const QString libraryStyle = ribbon->styleSheet();
        const Qt::FocusPolicy focusPolicy = tabs->focusPolicy();
        const QString accessibleName = tabs->accessibleName();
        QList<QWidget *> pages;
        QList<bool> enabled;
        QStringList labels, tooltips;
        for (int i = 0; i < tabs->count(); ++i) {
            pages << ribbon->widget(i);
            enabled << tabs->isTabEnabled(i);
            labels << tabs->tabText(i);
            tooltips << tabs->tabToolTip(i);
        }
        const QString evidence = qEnvironmentVariable("LQCOMPARE_RIBBON_SCREENSHOT_DIR");
        if (!evidence.isEmpty()) QVERIFY(QDir().mkpath(evidence));
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (const QString &theme : {QStringLiteral("system"), QStringLiteral("light"),
                                         QStringLiteral("dark"), QStringLiteral("system")}) {
                QVERIFY(options.apply({{QStringLiteral("display.theme"), theme}}).ok);
                QCoreApplication::processEvents();
                const QPalette palette = QApplication::palette();
                tabs->setCurrentIndex(0);
                tabs->clearFocus();
                QTest::mouseMove(&window, QPoint(window.width() - 10, window.height() - 10));
                QTRY_VERIFY(!tabs->underMouse());
                QTRY_COMPARE(tabBackground(tabs, 1).rgba(), palette.color(QPalette::Window).rgba());
                const QRect normalFirst = tabs->tabRect(0), normalSecond = tabs->tabRect(1);
                const int tabHeight = tabs->height();
                verifyTabPixels(tabs, 0, palette.color(QPalette::Text), palette.color(QPalette::Base));
                verifyTabPixels(tabs, 1, palette.color(QPalette::WindowText), palette.color(QPalette::Window));
                if (cycle == 0 && !evidence.isEmpty()) {
                    QVERIFY(window.grab().save(evidence + "/" + theme + "-home.png"));
                    QVERIFY(tabs->grab().save(evidence + "/" + theme + "-tabs.png"));
                }
                QTest::mouseMove(tabs, tabs->tabRect(1).center());
                QTRY_VERIFY(tabs->underMouse());
                QTRY_VERIFY(tabBackground(tabs, 1).rgba() != palette.color(QPalette::Window).rgba());
                const QColor hover = tabBackground(tabs, 1);
                QVERIFY(hover.rgba() != palette.color(QPalette::Window).rgba());
                verifyTabPixels(tabs, 1, palette.color(QPalette::WindowText), hover);
                QCOMPARE(tabs->tabRect(0), normalFirst);
                QCOMPARE(tabs->tabRect(1), normalSecond);
                QCOMPARE(tabs->height(), tabHeight);
                if (cycle == 0 && !evidence.isEmpty())
                    QVERIFY(tabs->grab().save(evidence + "/" + theme + "-hover.png"));

                tabs->setFocus(Qt::TabFocusReason);
                QTRY_VERIFY(tabs->hasFocus());
                QTest::keyClick(tabs, Qt::Key_Right);
                QCOMPARE(tabs->currentIndex(), 1);
                QCOMPARE(ribbon->currentPage()->objectName(), QStringLiteral("ribbonComparePage"));
                const QRect focusedFirst = tabs->tabRect(0), focusedSecond = tabs->tabRect(1);
                QCOMPARE(focusedFirst, normalFirst);
                QCOMPARE(focusedSecond, normalSecond);
                QCOMPARE(tabs->height(), tabHeight);
                verifyTabPixels(tabs, 1, palette.color(QPalette::Text), palette.color(QPalette::Base));
                if (cycle == 0 && !evidence.isEmpty())
                    QVERIFY(tabs->grab().save(evidence + "/" + theme + "-focus.png"));
                tabs->clearFocus();
                QCOMPARE(tabs->tabRect(0), focusedFirst);
                QCOMPARE(tabs->tabRect(1), focusedSecond);
                QCOMPARE(tabs->height(), tabHeight);
                if (cycle == 0 && !evidence.isEmpty())
                    qInfo() << "Ribbon tab geometry" << theme << "height" << tabHeight
                            << "Home selected / Compare hovered" << normalFirst << normalSecond
                            << "Compare selected, with and without focus" << focusedFirst << focusedSecond;
                tabs->setFocus(Qt::TabFocusReason);
                QTest::keyClick(tabs, Qt::Key_Left);
                QCOMPARE(tabs->currentIndex(), 0);
                for (int i = 0; i < tabs->count(); ++i) tabs->setCurrentIndex(i);
                tabs->setCurrentIndex(1);
                CommandRegistry::instance().updateEnabled();
                QVERIFY(runtime.applyCurrent());
                QCoreApplication::processEvents();
                QCOMPARE(tabs->currentIndex(), 1);
                QCOMPARE(sessions(window)->geometry(), contentGeometry);
                QCOMPARE(window.windowFlags(), flags);
                QCOMPARE(ribbon->isFrameThemeEnabled(), frameTheme);
                QCOMPARE(ribbon->styleSheet(), libraryStyle);
                QCOMPARE(tabs->focusPolicy(), focusPolicy);
                QCOMPARE(tabs->accessibleName(), accessibleName);
                QCOMPARE(tabs->count(), pages.size());
                for (int i = 0; i < tabs->count(); ++i) {
                    QCOMPARE(ribbon->widget(i), pages[i]);
                    QCOMPARE(tabs->isTabEnabled(i), enabled[i]);
                    QCOMPARE(tabs->tabText(i), labels[i]);
                    QCOMPARE(tabs->tabToolTip(i), tooltips[i]);
                }
            }
        }
        ribbon->setRibbonStyle(RibbonBar::Microsoft365Dark);
        ribbon->setRibbonStyle(RibbonBar::Office2016Blue);
        QCoreApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), 1);
        QCOMPARE(sessions(window)->geometry(), contentGeometry);
        tabs->clearFocus();
        const QPalette palette = QApplication::palette();
        verifyTabPixels(tabs, 0, palette.color(QPalette::WindowText), palette.color(QPalette::Window));
        verifyTabPixels(tabs, 1, palette.color(QPalette::Text), palette.color(QPalette::Base));
        QVERIFY(qatAction(window, "file.open")->isEnabled());
        QVERIFY(!qatAction(window, "file.save")->isEnabled());

        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "a\nleft\nend\n"); writeFile(right, "a\nright\nend\n");
        QVERIFY(window.openPaths({left, right}));
        QCoreApplication::processEvents();
        auto *session = qobject_cast<TextCompareSession *>(sessions(window)->currentSession());
        QVERIFY(session);
        auto *leftPane = session->widget()->findChild<TextPane *>(QStringLiteral("leftTextPane"));
        auto *rightPane = session->widget()->findChild<TextPane *>(QStringLiteral("rightTextPane"));
        QVERIFY(leftPane && rightPane);
        const QRect leftGeometry = leftPane->geometry(), rightGeometry = rightPane->geometry();
        const QString leftText = leftPane->toPlainText(), rightText = rightPane->toPlainText();
        tabs->setCurrentIndex(1);
        for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("system")}) {
            QVERIFY(options.apply({{QStringLiteral("display.theme"), theme}}).ok);
            QCoreApplication::processEvents();
            QCOMPARE(sessions(window)->currentSession(), session);
            QCOMPARE(tabs->currentIndex(), 1);
            QCOMPARE(leftPane->geometry(), leftGeometry);
            QCOMPARE(rightPane->geometry(), rightGeometry);
            QCOMPARE(leftPane->toPlainText(), leftText);
            QCOMPARE(rightPane->toPlainText(), rightText);
            if (!evidence.isEmpty())
                QVERIFY(window.grab().save(evidence + "/" + theme + "-comparison.png"));
        }
    }

    void ribbonTabsSerializePaletteRgb_data()
    {
        QTest::addColumn<int>("alpha");
        for (int alpha : {0, 128, 216, 255})
            QTest::newRow(qPrintable(QString::number(alpha))) << alpha;
    }

    void ribbonTabsSerializePaletteRgb()
    {
        QFETCH(int, alpha);
        struct RestorePalette {
            QPalette palette = QApplication::palette();
            ~RestorePalette() { QApplication::setPalette(palette); }
        } restore;
        QPalette palette = restore.palette;
        palette.setColor(QPalette::Window, QColor(240, 240, 240));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::WindowText, QColor(0, 0, 0, alpha));
        palette.setColor(QPalette::Text, QColor(0, 0, 0, alpha));
        QApplication::setPalette(palette);
        MainWindow window;
        window.resize(1440, 900); window.show(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tabs = window.ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
        QVERIFY(tabs);
        QTest::mouseMove(&window, QPoint(1400, 800));
        QTRY_VERIFY(!tabs->underMouse());
        tabs->clearFocus();
        verifyTabPixels(tabs, 0, palette.color(QPalette::Text), palette.color(QPalette::Base));
        verifyTabPixels(tabs, 1, palette.color(QPalette::WindowText), palette.color(QPalette::Window));
        QCOMPARE(QApplication::palette().color(QPalette::Text).alpha(), alpha);
    }

    void ribbonSelectionKeepsCompleteLabels()
    {
        MainWindow window;
        window.resize(1440, 900); window.show(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tabs = window.ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
        QVERIFY(tabs);
        // macOS 默认会省略超宽文本；在所有平台主动覆盖这条真实布局路径。
        tabs->setElideMode(Qt::ElideRight);
        tabs->setTabText(1, QStringLiteral("Comparison commands"));
        QTest::mouseMove(&window, QPoint(1400, 800));
        QTRY_VERIFY(!tabs->underMouse());
        tabs->clearFocus();
        const QPalette palette = QApplication::palette();
        for (int i = 0; i < tabs->count(); ++i) {
            const QRect before = tabs->tabRect(i);
            tabs->setCurrentIndex(i);
            QCOMPARE(tabs->tabRect(i), before);
            verifyTabPixels(tabs, i, palette.color(QPalette::Text), palette.color(QPalette::Base));
            tabs->setFocus(Qt::TabFocusReason);
            QTRY_VERIFY(tabs->hasFocus());
            QCOMPARE(tabs->tabRect(i), before);
            verifyTabPixels(tabs, i, palette.color(QPalette::Text), palette.color(QPalette::Base));
            tabs->clearFocus();
        }
    }

    void ribbonHoverRemainsReadableWithUnpairedPaletteRoles()
    {
        struct RestorePalette {
            QPalette palette = QApplication::palette();
            ~RestorePalette() { QApplication::setPalette(palette); }
        } restore;
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tabs = window.ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
        QVERIFY(tabs);
        // 首例检出不安全的 WindowText/Midlight 配对；第二例让完整强调色混色失效：
        // 原本可读的中灰背景必须减弱混色，或改用调色板中的安全颜色。
        for (const QColor &background : {QColor(Qt::black), QColor(118, 118, 118)}) {
            QPalette palette = restore.palette;
            palette.setColor(QPalette::Window, background);
            palette.setColor(QPalette::WindowText, Qt::white);
            palette.setColor(QPalette::Base, Qt::black);
            palette.setColor(QPalette::Text, Qt::white);
            palette.setColor(QPalette::Midlight, QColor(250, 250, 250));
            palette.setColor(QPalette::Highlight, Qt::white);
            QApplication::setPalette(palette);
            const QPalette applied = QApplication::palette();
            QTest::mouseMove(&window, QPoint(1400, 800));
            tabs->setCurrentIndex(0);
            // QWidget 版本的 mouseMove 经由窗口服务器异步返回；单次 processEvents
            // 不保证已收到移动。进入/离开都等待真实状态，不依赖固定睡眠时间。
            // https://wiki.qt.io/Writing_good_tests#Widgets_and_Windows
            QTRY_VERIFY(!tabs->underMouse());
            QTRY_COMPARE(tabBackground(tabs, 1).rgba(), background.rgba());
            const QRect normal = tabs->tabRect(1);
            const int height = tabs->height();
            verifyTabPixels(tabs, 1, palette.color(QPalette::WindowText), background);
            QTest::mouseMove(tabs, tabs->tabRect(1).center());
            QTRY_VERIFY(tabs->underMouse());
            QTRY_VERIFY(tabBackground(tabs, 1).rgba() != background.rgba());
            const QColor hover = tabBackground(tabs, 1);
            QVERIFY(hover.rgba() != background.rgba());
            verifyTabPixels(tabs, 1, palette.color(QPalette::WindowText), hover);
            QCOMPARE(tabs->tabRect(1), normal);
            QCOMPARE(tabs->height(), height);
            QCOMPARE(QApplication::palette(), applied);
            const QString evidence = qEnvironmentVariable("LQCOMPARE_RIBBON_SCREENSHOT_DIR");
            if (!evidence.isEmpty()) {
                QVERIFY(QDir().mkpath(evidence));
                QVERIFY(tabs->grab().save(evidence + "/adversarial-" + background.name().mid(1) + ".png"));
            }
        }
    }

    void commandSearchStaysAboveRibbonAcrossLayouts()
    {
        struct RestoreApplication {
            QPalette palette = QApplication::palette();
            QFont font = QApplication::font();
            ~RestoreApplication() { QApplication::setPalette(palette); QApplication::setFont(font); }
        } restore;
        QTemporaryDir directory;
        Settings::OptionsRepository options({directory.path(), false});
        Options::OptionsRuntime runtime(&options);
        // 宽画布验证布局，不要求 CI 的物理屏幕容纳 1920 像素窗口。
        // 原生鼠标/键盘验证由独立的屏幕内窗口完成，裁切部分不注入输入。
        QWidget host;
        host.resize(480, 600);
        MainWindow window(&host);
        // QMainWindow 构造时强制顶层标志；仅逻辑画布显式嵌入宿主。
        window.setParent(&host, Qt::Widget);
        QVERIFY(!window.isWindow());
        window.show(); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("commandSearch"));
        auto *tabs = window.ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
        QVERIFY(search && tabs);
        QCOMPARE(window.menuWidget(), search->parentWidget());
        QVERIFY(!search->placeholderText().isEmpty());
        QVERIFY(!search->accessibleName().isEmpty());
        QVERIFY(!search->accessibleDescription().isEmpty());
        auto *submit = search->findChild<QAction *>(QStringLiteral("commandSearchSubmit"));
        QVERIFY(submit); QVERIFY(!submit->icon().isNull());
        QVERIFY(!submit->text().isEmpty()); QVERIFY(!submit->toolTip().isEmpty());
        const QString searchLabel = search->placeholderText();
        search->setPlaceholderText(QStringLiteral("stale")); submit->setText(QStringLiteral("stale"));
        QVERIFY(QMetaObject::invokeMethod(&window, "switchLanguage", Qt::DirectConnection));
        QCOMPARE(search->placeholderText(), searchLabel); QCOMPARE(submit->text(), searchLabel);
        const auto flags = window.windowFlags();
        const bool frameTheme = window.ribbonBar()->isFrameThemeEnabled();
        const QString libraryStyle = window.ribbonBar()->styleSheet();
        const QString evidence = qEnvironmentVariable("LQCOMPARE_SEARCH_SCREENSHOT_DIR");
        if (!evidence.isEmpty()) QVERIFY(QDir().mkpath(evidence));
        for (const QString &theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            QVERIFY(options.apply({{QStringLiteral("display.theme"), theme}}).ok);
            for (int points : {8, 16}) {
                QFont font = restore.font; font.setPointSize(points);
                QApplication::setFont(font);
                for (int width : {800, 1024, 1440, 1920}) {
                    window.resize(width, 900);
                    for (bool collapsed : {false, true, false}) {
                        window.ribbonBar()->setMinimized(collapsed);
                        QCoreApplication::processEvents();
                        QCOMPARE(window.width(), width);
                        QVERIFY(search->isVisible());
                        QVERIFY(!window.ribbonBar()->searchBar()->isVisible());
                        const QRect searchRect(search->mapTo(&window, QPoint()), search->size());
                        const QRect tabsRect(tabs->mapTo(&window, QPoint()), tabs->size());
                        QVERIFY2(searchRect.bottom() < tabsRect.top(), "搜索框必须完全位于标签上方");
                        QVERIFY(window.rect().contains(searchRect));
                        QVERIFY(search->width() <= search->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 36);
                        QVERIFY(qAbs(searchRect.center().x() - window.rect().center().x()) <= 1);
                        QVERIFY(search->height() >= search->fontMetrics().height() + 4);
                        QCOMPARE(search->font().pointSize(), points);
                        const QPalette palette = QApplication::palette();
                        QVERIFY(contrast(search->palette().color(QPalette::Text),
                                         search->palette().color(QPalette::Base)) >= 4.5);
                        QCOMPARE(search->palette().color(QPalette::Text), palette.color(QPalette::Text));
                        QCOMPARE(search->palette().color(QPalette::Base), palette.color(QPalette::Base));
                        QCOMPARE(window.windowFlags(), flags);
                        QCOMPARE(window.ribbonBar()->isFrameThemeEnabled(), frameTheme);
                        QCOMPARE(window.ribbonBar()->styleSheet(), libraryStyle);
                    }
                    QVERIFY(window.width() > host.width());
                    QVERIFY(window.visibleRegion().boundingRect().width() < window.width());
                    // 这里只检查逻辑命中区域，不能把屏幕外坐标的直投事件当作用户操作。
                    for (int i = 0; i < tabs->count(); ++i) {
                        const QPoint center = tabs->mapTo(&window, tabs->tabRect(i).center());
                        if (window.rect().contains(center)) QVERIFY(window.childAt(center) != search);
                    }
                    qInfo() << "Search logical canvas layout" << theme << "font" << points << "width" << width
                            << "search" << QRect(search->mapTo(&window, QPoint()), search->size())
                            << "tabs" << QRect(tabs->mapTo(&window, QPoint()), tabs->size());
                    if (!evidence.isEmpty()) {
                        QVERIFY(window.grab().save(QStringLiteral("%1/canvas-%2-%3pt-%4.png")
                                                  .arg(evidence, theme).arg(points).arg(width)));
                    }
                }
            }
        }
    }

    void commandSearchAcceptsNativeInputWithinScreen()
    {
        FullKeyboardNavigation navigation;
        struct RestoreApplication {
            QPalette palette = QApplication::palette();
            QFont font = QApplication::font();
            ~RestoreApplication() { QApplication::setPalette(palette); QApplication::setFont(font); }
        } restore;
        QTemporaryDir directory;
        Settings::OptionsRepository options({directory.path(), false});
        Options::OptionsRuntime runtime(&options);
        MainWindow window;
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        window.resize(qMin(800, available.width() - 64), qMin(600, available.height() - 96));
        window.move(available.topLeft() + QPoint(24, 32));
        window.show(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("commandSearch"));
        auto *tabs = window.ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
        QVERIFY(search && tabs);
        const auto flags = window.windowFlags();
        const bool frameTheme = window.ribbonBar()->isFrameThemeEnabled();
        const QString libraryStyle = window.ribbonBar()->styleSheet();
        for (const QString &theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            QVERIFY(options.apply({{QStringLiteral("display.theme"), theme}}).ok);
            for (int points : {8, 16}) {
                QFont font = restore.font; font.setPointSize(points); QApplication::setFont(font);
                for (int requested : {800, 1024}) {
                    const int width = qMin(requested, available.width() - 64);
                    window.resize(width, qMin(600, available.height() - 96));
                    for (bool collapsed : {false, true, false}) {
                        window.ribbonBar()->setMinimized(collapsed);
                        QCoreApplication::processEvents();
                        QCOMPARE(window.width(), width);
                        QCOMPARE(window.windowFlags(), flags);
                        QCOMPARE(window.ribbonBar()->isFrameThemeEnabled(), frameTheme);
                        QCOMPARE(window.ribbonBar()->styleSheet(), libraryStyle);
                        QVERIFY(available.contains(window.frameGeometry()));
                        QVERIFY(QRegion(search->rect()).subtracted(search->visibleRegion()).isEmpty());
                        const QRect searchRect(search->mapToGlobal(QPoint()), search->size());
                        QVERIFY(available.contains(searchRect));
                        int visibleTabs = 0;
                        for (int i = 0; i < tabs->count(); ++i) {
                            const QRect rect = tabs->tabRect(i);
                            if (!QRegion(rect).subtracted(tabs->visibleRegion()).isEmpty()) continue;
                            const QRect global(tabs->mapToGlobal(rect.topLeft()), rect.size());
                            QVERIFY(available.contains(global));
                            const QRect inWindow(tabs->mapTo(&window, rect.topLeft()), rect.size());
                            QVERIFY(window.rect().contains(inWindow));
                            for (QWidget *ancestor = tabs->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
                                QVERIFY(ancestor->rect().contains(QRect(tabs->mapTo(ancestor, rect.topLeft()), rect.size())));
                            QVERIFY(window.childAt(inWindow.center()) != search);
                            QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, rect.center());
                            QCOMPARE(tabs->currentIndex(), i);
                            ++visibleTabs;
                        }
                        QVERIFY(visibleTabs >= 2);
                        tabs->setCurrentIndex(0);
                        search->setFocus(Qt::TabFocusReason);
                        QTRY_VERIFY(search->hasFocus());
                        QTest::keyClick(search, Qt::Key_Tab);
                        QTRY_VERIFY(tabs->hasFocus());
                        QTest::keyClick(tabs, Qt::Key_Backtab);
                        QTRY_VERIFY(search->hasFocus());
                        search->setText(QStringLiteral("draft command"));
                        QTest::keyClick(search, Qt::Key_Escape);
                        QVERIFY(search->text().isEmpty()); QVERIFY(!search->hasFocus());
                        QVERIFY(!QApplication::activeModalWidget());
                    }
                    qInfo() << "Search screen-bounded native input" << theme << "font" << points
                            << "width" << width << "screen" << available;
                    const QString evidence = qEnvironmentVariable("LQCOMPARE_SEARCH_SCREENSHOT_DIR");
                    if (!evidence.isEmpty()) {
                        QVERIFY(QDir().mkpath(evidence));
                        QVERIFY(window.grab().save(QStringLiteral("%1/native-%2-%3pt-%4.png")
                                                  .arg(evidence, theme).arg(points).arg(width)));
                    }
                }
            }
        }
    }

    void commandSearchUsesRegistryConfirmation_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("answer");
        QTest::addColumn<bool>("escape");
        QTest::addColumn<int>("dialogs");
        QTest::addColumn<int>("calls");
        QTest::newRow("empty") << QString() << int(QMessageBox::Ok) << false << 0 << 0;
        QTest::newRow("whitespace") << QStringLiteral("   ") << int(QMessageBox::Ok) << false << 0 << 0;
        QTest::newRow("missing") << QStringLiteral("no-command-47291") << int(QMessageBox::Ok) << false << 1 << 0;
        QTest::newRow("literal-markup") << QStringLiteral("<b>&command-not-present</b>") << int(QMessageBox::Ok) << false << 1 << 0;
        QTest::newRow("id-confirm") << QStringLiteral("test.search-one") << int(QMessageBox::Yes) << false << 1 << 1;
        QTest::newRow("name-confirm") << QStringLiteral("Unique Search Probe") << int(QMessageBox::Yes) << false << 1 << 1;
        QTest::newRow("case-trim-confirm") << QStringLiteral("  TEST.SEARCH-ONE  ") << int(QMessageBox::Yes) << false << 1 << 1;
        QTest::newRow("cancel") << QStringLiteral("test.search-one") << int(QMessageBox::No) << false << 1 << 0;
        QTest::newRow("escape-dialog") << QStringLiteral("test.search-one") << int(QMessageBox::No) << true << 1 << 0;
        QTest::newRow("return-default-no") << QStringLiteral("test.search-one") << int(QMessageBox::No) << false << 1 << 0;
        QTest::newRow("repeat-return-default-no") << QStringLiteral("test.search-one") << int(QMessageBox::No) << false << 1 << 0;
        QTest::newRow("keyboard-yes") << QStringLiteral("test.search-one") << int(QMessageBox::Yes) << false << 1 << 1;
        QTest::newRow("keypad-enter") << QStringLiteral("test.search-one") << int(QMessageBox::Yes) << false << 1 << 1;
        QTest::newRow("mouse-confirm") << QStringLiteral("test.search-one") << int(QMessageBox::Yes) << false << 1 << 1;
        QTest::newRow("mouse-cancel") << QStringLiteral("test.search-one") << int(QMessageBox::No) << false << 1 << 0;
        QTest::newRow("mouse-disabled") << QStringLiteral("test.search-disabled") << int(QMessageBox::Yes) << false << 1 << 0;
        QTest::newRow("multiple") << QStringLiteral("test.search-many") << int(QMessageBox::Ok) << false << 1 << 0;
        QTest::newRow("disabled") << QStringLiteral("test.search-disabled") << int(QMessageBox::Yes) << false << 1 << 0;
        QTest::newRow("unimplemented") << QStringLiteral("test.search-stub") << int(QMessageBox::Ok) << false << 1 << 0;
    }

    void commandSearchUsesRegistryConfirmation()
    {
        QFETCH(QString, query); QFETCH(int, answer); QFETCH(bool, escape);
        QFETCH(int, dialogs); QFETCH(int, calls);
        MainWindow window; window.resize(1024, 800); window.show(); window.activateWindow();
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("commandSearch"));
        QVERIFY(search);
        int executed = 0;
        for (const QString &suffix : {QStringLiteral("one"), QStringLiteral("many-first"),
                                      QStringLiteral("many-second"), QStringLiteral("disabled"), QStringLiteral("stub")}) {
            Command command;
            command.id = QStringLiteral("test.search-") + suffix;
            command.text = suffix == QLatin1String("one") ? QStringLiteral("Unique Search Probe") : suffix;
            command.actionId = QStringLiteral("UI-004"); command.module = QStringLiteral("界面");
            command.description = QStringLiteral("仅用于测试的无副作用命令");
            if (suffix != QLatin1String("stub")) command.handler = [&] { ++executed; };
            command.enabled = suffix != QLatin1String("disabled");
            QVERIFY(CommandRegistry::instance().add(command));
        }
        const QByteArray tag = QTest::currentDataTag();
        const SearchConfirmation confirmation = tag == "return-default-no" ? SearchConfirmation::Return
            : tag == "repeat-return-default-no" ? SearchConfirmation::RepeatedReturn
            : tag == "keyboard-yes" ? SearchConfirmation::KeyboardYes : SearchConfirmation::Button;
        const SearchResult result = submitSearch(search, query, QMessageBox::StandardButton(answer), escape,
                                                true, [&] { QCOMPARE(executed, 0); }, confirmation,
                                                tag == "keypad-enter" ? Qt::Key_Enter : Qt::Key_Return, tag.startsWith("mouse-"));
        QCOMPARE(result.dialogs, dialogs); QCOMPARE(executed, calls);
        if (dialogs) QCOMPARE(result.format, Qt::PlainText);
        if (query.startsWith(QLatin1Char('<'))) QVERIFY(result.text.contains(query));
        if (query == QLatin1String("no-command-47291")) QVERIFY(result.text.contains(QStringLiteral("No command matches")));
        if (query == QLatin1String("test.search-many")) {
            QVERIFY(result.text.contains(QStringLiteral("many-first")));
            QVERIFY(result.text.contains(QStringLiteral("many-second")));
        }
        QVERIFY(!QApplication::activeModalWidget());
        // 模拟长按产生的自动重复事件，不能再次确认或执行。
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true, 1);
        QApplication::sendEvent(search, &repeat);
        QCOMPARE(executed, calls); QVERIFY(!QApplication::activeModalWidget());
    }

    void commandSearchCancelsStaleQueuedInput()
    {
        QPointer<MainWindow> window = new MainWindow;
        window->show(); window->activateWindow();
        auto *search = window->findChild<QLineEdit *>(QStringLiteral("commandSearch"));
        QVERIFY(search);
        auto *submit = search->findChild<QAction *>(QStringLiteral("commandSearchSubmit"));
        QVERIFY(submit);
        QStringList calls;
        for (const QString &suffix : {QStringLiteral("first"), QStringLiteral("second")}) {
            Command command; command.id = QStringLiteral("test.queued-") + suffix; command.text = suffix;
            command.handler = [&, suffix] { calls << suffix; };
            QVERIFY(CommandRegistry::instance().add(command));
        }
        int prompts = 0;
        QString lastPrompt;
        QTimer responder; responder.setInterval(1);
        connect(&responder, &QTimer::timeout, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            ++prompts; lastPrompt = box->text();
            if (!box->button(QMessageBox::Yes)) qFatal("Expected queued command confirmation");
            box->button(QMessageBox::Yes)->click();
        });
        QTimer::singleShot(2000, &responder, [] { qFatal("Queued search did not return"); });
        responder.start();
        const auto enter = [&] {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(search, &press);
        };
        search->setText(QStringLiteral("test.queued-first")); enter();
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(search, &escape);
        QCoreApplication::processEvents();
        QCOMPARE(prompts, 0); QVERIFY(calls.isEmpty()); QVERIFY(search->text().isEmpty());
        search->setText(QStringLiteral("test.queued-first")); enter();
        search->setText(QStringLiteral("test.queued-second"));
        QCoreApplication::processEvents();
        QCOMPARE(prompts, 0); QVERIFY(calls.isEmpty());
        // 未处理事件前混合回车与鼠标入口，只保留新查询的一次请求。
        search->setText(QStringLiteral("test.queued-first")); enter();
        search->setText(QStringLiteral("test.queued-second")); enter(); enter(); submit->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(prompts, 1); QCOMPARE(calls, QStringList{QStringLiteral("second")});
        QVERIFY(lastPrompt.contains(QStringLiteral("second"))); QVERIFY(!lastPrompt.contains(QStringLiteral("first")));
        search->setText(QStringLiteral("test.queued-first")); enter();
        QCoreApplication::processEvents();
        QCOMPARE(prompts, 2); QCOMPARE(calls, (QStringList{QStringLiteral("second"), QStringLiteral("first")}));
        // 排队期间销毁窗口，带上下文的回调必须自动取消。
        enter(); delete window.data(); QCoreApplication::processEvents();
        QCOMPARE(prompts, 2); QCOMPARE(calls.size(), 2); QVERIFY(!QApplication::activeModalWidget());
    }

    void commandSearchAllowsOwnerDestruction_data()
    {
        QTest::addColumn<bool>("duringConfirmation");
        QTest::addColumn<QString>("query");
        QTest::newRow("command-deletes-owner") << false << QStringLiteral("test.destroy-owner");
        QTest::newRow("owner-deleted-during-modal") << true << QStringLiteral("test.destroy-owner");
        QTest::newRow("owner-deleted-during-empty-state") << true << QStringLiteral("no-command-47291");
        QTest::newRow("owner-deleted-during-multiple-results") << true << QStringLiteral("test.");
    }

    void commandSearchAllowsOwnerDestruction()
    {
        QFETCH(bool, duringConfirmation); QFETCH(QString, query);
        QPointer<MainWindow> window = new MainWindow;
        window->show(); window->activateWindow();
        auto *search = window->findChild<QLineEdit *>(QStringLiteral("commandSearch"));
        QVERIFY(search);
        int executed = 0;
        Command command;
        command.id = QStringLiteral("test.destroy-owner");
        command.text = QStringLiteral("Owner Lifetime Probe");
        command.handler = [&] { ++executed; delete window.data(); };
        QVERIFY(CommandRegistry::instance().add(command));
        Command other = command; other.id = QStringLiteral("test.another-owner");
        QVERIFY(CommandRegistry::instance().add(other));
        QTimer responder;
        responder.setInterval(1);
        connect(&responder, &QTimer::timeout, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            responder.stop();
            if (duringConfirmation) delete window.data();
            else {
                if (!box->button(QMessageBox::Yes)) qFatal("Expected command confirmation");
                box->button(QMessageBox::Yes)->click();
            }
        });
        responder.start();
        search->setText(query);
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 2000);
        QVERIFY(window.isNull()); QCOMPARE(executed, duringConfirmation ? 0 : 1);
        QVERIFY(!QApplication::activeModalWidget());
    }

    void commandSearchProtectsCurrentSessionAndOriginalFiles()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        const QByteArray originalLeft("left original\r\n"), originalRight("right original\r\n");
        writeFile(left, originalLeft); writeFile(right, originalRight);
        MainWindow window; window.resize(1024, 800); window.show(); window.activateWindow();
        auto *first = qobject_cast<TextCompareSession *>(window.openComparison("text", left, right));
        QVERIFY(first);
        auto *pane = first->widget()->findChild<TextPane *>(QStringLiteral("rightTextPane"));
        QVERIFY(pane);
        bool editorShown = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *editor = dialog ? dialog->findChild<QPlainTextEdit *>(QStringLiteral("textBufferEditor")) : nullptr;
            auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr;
            if (!editor || !buttons || !buttons->button(QDialogButtonBox::Apply)) qFatal("Expected text editor");
            editorShown = true; editor->setFocus();
            QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(editor, "edited fixture");
            buttons->button(QDialogButtonBox::Apply)->click();
        });
        auto *edit = first->widget()->findChild<QPushButton *>(QStringLiteral("editRight"));
        QVERIFY(edit && edit->isEnabled()); edit->click(); QVERIFY(editorShown);
        QVERIFY(first->isDirty());
        const QString edited = pane->toPlainText();
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("commandSearch"));
        QVERIFY(search);
        // 为有歧义的 Save/Save As 子串建立测试别名，执行体仍是应用注册的保存处理器。
        const Command *save = CommandRegistry::instance().find(QStringLiteral("file.save"));
        QVERIFY(save); Command alias = *save;
        alias.id = QStringLiteral("test.fixture-save"); alias.text = QStringLiteral("Fixture Save Probe");
        alias.sessionTypes = QStringList{QStringLiteral("text")};
        alias.enabledWhen = [&window] { auto *current = sessions(window)->currentSession(); return current && current->canSave(); };
        QVERIFY(CommandRegistry::instance().add(alias));
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::No).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), originalRight);
        QCOMPARE(pane->toPlainText(), edited); QVERIFY(first->isDirty());
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::No, true).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), originalRight);
        QVERIFY(first->isDirty());
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::No, false, false, {},
                              SearchConfirmation::RepeatedReturn).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), originalRight);
        QCOMPARE(pane->toPlainText(), edited); QVERIFY(first->isDirty());
        auto *second = window.openComparison("text", left, right); QVERIFY(second);
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::Yes).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), originalRight);
        QVERIFY(first->isDirty()); QVERIFY(!second->isDirty());
        sessions(window)->setCurrentWidget(first->widget());
        // 确认框打开后切换到主页，执行时仍必须重新校验当前上下文。
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::Yes, false, false, [&] {
            sessions(window)->setCurrentWidget(sessions(window)->homePage());
        }).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), originalRight);
        QVERIFY(first->isDirty());
        sessions(window)->setCurrentWidget(first->widget());
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::Yes).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), QByteArray("edited fixture"));
        QVERIFY(!first->isDirty()); QVERIFY(!second->isDirty());
        QCOMPARE(submitSearch(search, alias.id, QMessageBox::Yes).dialogs, 1);
        QCOMPARE(readFile(left), originalLeft); QCOMPARE(readFile(right), QByteArray("edited fixture"));
        QVERIFY(sessions(window)->closeAllSessions());
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

    void destroyingWindowBeforeDeferredSessionDeletion_data()
    {
        QTest::addColumn<int>("closedCount");
        QTest::newRow("open-tabs") << 0;
        QTest::newRow("one-closed-tab") << 1;
        QTest::newRow("all-tabs-closed") << 3;
    }

    void destroyingWindowBeforeDeferredSessionDeletion()
    {
        QFETCH(int, closedCount);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("text.txt");
        writeFile(path, "contents\n");
        QScopedPointer<MainWindow> window(new MainWindow);
        auto *area = sessions(*window);
        QVERIFY(area);
        QList<QPointer<CompareSession>> trackedSessions;
        QList<QPointer<QWidget>> trackedViews;
        for (int i = 0; i < 3; ++i) {
            auto *session = window->openComparison("text", path, path);
            QVERIFY(session);
            trackedSessions.append(session);
            trackedViews.append(session->widget());
        }
        for (int i = 0; i < closedCount; ++i) {
            QVERIFY(area->closeSession(area->indexOf(trackedViews.at(i))));
            // DeferredDelete 尚未执行，已关闭会话仍然是窗口的子对象。
            QVERIFY(!trackedSessions.at(i).isNull());
            QCOMPARE(trackedSessions.at(i)->state(), CompareSession::State::Closed);
        }
        QCOMPARE(area->sessionCount(), 3 - closedCount);
        // 故意不处理事件：关闭标签后立即退出，也必须先断开会话回调。
        window.reset();
        for (const auto &session : trackedSessions) QVERIFY(session.isNull());
        for (const auto &view : trackedViews) QVERIFY(view.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
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

    // VCS-001 第 3 条：主窗口把「这台机器上能不能做 VCS 查询」落到版本控制命令上。
    void vcsCommandsFollowTheInjectedBackendAvailability()
    {
        MainWindow window;
        auto &registry = CommandRegistry::instance();
        // 哪些命令属于版本控制，判据取自**服务层那一个**（不是在这里另写一遍前缀比较）：
        // 界面上「哪些命令要跟着置灰」与这里「哪些命令该被检查」必须是同一句话，
        // 否则两边各写一份，改了一处另一处会静默地不再覆盖新加的命令。
        const auto vcsIds = [&registry] {
            QStringList ids;
            for (const Command &command : registry.all())
                if (Vcs::isVcsActionId(command.actionId)) ids << command.id;
            return ids;
        };
        const QStringList ids = vcsIds();
        // 空清单会让下面每一条断言都平凡成立——而「一条都没覆盖到」正是降级没接上
        // 的样子（护栏自己也会变成绿灯）。所以先把清单本身钉住。
        QCOMPARE(ids.size(), 3);
        QVERIFY(ids.contains(QStringLiteral("vcs.diffhead")));
        QVERIFY(ids.contains(QStringLiteral("vcs.tworevisions")));
        QVERIFY(ids.contains(QStringLiteral("vcs.log")));

        // ① 没有 git：全部置灰，且原因里说得出「未检测到 git」。
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Vcs::Options missingOptions;
        missingOptions.gitExecutable = temporary.filePath(QStringLiteral("no-such-git/git"));
        Vcs::GitBackend missing(missingOptions);
        window.setVcsBackend(&missing);
        for (const QString &id : vcsIds()) {
            const Command *command = registry.find(id);
            QVERIFY2(command, qPrintable(id));
            QVERIFY2(!command->enabled, qPrintable(id));
            QVERIFY2(command->disabledReason.contains(QStringLiteral("未检测到 git")),
                     qPrintable(command->disabledReason));
        }

        // ② 传 nullptr 回到构造时自建的那个后端。期望值由**同一个后端重新算一遍**
        //    得到，因此这条断言在有没有 git 的机器上都是真的，而不是「本机恰好有」。
        window.setVcsBackend(nullptr);
        Vcs::GitBackend sameAsBuiltIn;
        const auto expected = Vcs::probeAvailability(&sameAsBuiltIn);
        for (const QString &id : vcsIds()) {
            const Command *command = registry.find(id);
            QVERIFY2(command, qPrintable(id));
            QCOMPARE(command->enabled, expected.available);
            QCOMPARE(command->disabledReason, expected.reason);
        }
        // 本机有 git 时必须真的是「可用」，否则「可用」这一支就只在假后端上成立。
        QCOMPARE(registry.find(QStringLiteral("vcs.diffhead"))->enabled,
                 !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty());
    }
};

QTEST_MAIN(AppIntegrationTests)
#include "tst_appintegration.moc"
