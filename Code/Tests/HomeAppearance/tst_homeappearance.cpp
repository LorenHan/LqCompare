#include "comparesession.h"
#include "homepage.h"
#include "optionsrepository.h"
#include "optionsruntime.h"
#include "sessionarea.h"

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace LqCompare;

namespace {
double contrast(const QColor &first, const QColor &second)
{
    const auto luminance = [](const QColor &color) {
        const auto linear = [](double value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF())
                + 0.0722 * linear(color.blueF());
    };
    const double a = luminance(first), b = luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

struct TextPaint {
    QString text;
    QColor color;
    QRect rect;
    QRect bounds;
};

class ObservedStyle final : public QProxyStyle {
public:
    explicit ObservedStyle(QStyle *nativeStyle, bool lightSurface = false)
        : QProxyStyle(nativeStyle), m_lightSurface(lightSurface) {}
    mutable QList<TextPaint> paints;

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override
    {
        // 在 Linux 上复现原生控件忽略深色背景的缺陷。除表面外，文字、尺寸与
        // 交互仍交给真实 Qt；正常验证行不启用此故障注入。
        if (m_lightSurface && (element == CE_PushButtonBevel || element == CE_TabBarTabShape)) {
            painter->fillRect(option->rect, QColor(245, 245, 245));
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void drawItemText(QPainter *painter, const QRect &rect, int flags,
                      const QPalette &palette, bool enabled, const QString &text,
                      QPalette::ColorRole role = QPalette::NoRole) const override
    {
        paints.append({text, role == QPalette::NoRole ? painter->pen().color()
                       : palette.color(enabled ? QPalette::Normal : QPalette::Disabled, role),
                       rect, painter->fontMetrics().boundingRect(rect, flags, text)});
        QProxyStyle::drawItemText(painter, rect, flags, palette, enabled, text, role);
    }
private:
    bool m_lightSurface;
};

class ApplicationState final {
public:
    ApplicationState()
        : palette(QApplication::palette()), font(QApplication::font()),
          styleName(QApplication::style()->objectName()) {}
    ~ApplicationState()
    {
        QApplication::setStyle(QStyleFactory::create(styleName));
        QApplication::setPalette(palette);
        QApplication::setFont(font);
    }
    const QPalette palette;
    const QFont font;
    const QString styleName;
};

class ExampleSession final : public CompareSession {
public:
    ExampleSession() : CompareSession(QStringLiteral("text"))
    { setTitle(QStringLiteral("Example comparison")); }
protected:
    QWidget *createView(QWidget *parent) override
    { return new QLabel(QStringLiteral("Comparison content"), parent); }
};

QColor composite(const QColor &foreground, const QColor &background)
{
    const double alpha = foreground.alphaF();
    return QColor(qRound(foreground.red() * alpha + background.red() * (1 - alpha)),
                  qRound(foreground.green() * alpha + background.green() * (1 - alpha)),
                  qRound(foreground.blue() * alpha + background.blue() * (1 - alpha)));
}

QColor pixelAt(const QPixmap &pixmap, const QPoint &position)
{
    return pixmap.toImage().pixelColor(position * pixmap.devicePixelRatio());
}

void verifyRenderedText(ObservedStyle *style, QWidget *widget, const QRect &surface,
                        const QString &label, const QColor &foreground,
                        const QColor &background, bool enabled = true)
{
    style->paints.clear();
    const QPixmap pixmap = widget->grab();
    const QColor actualBackground = pixelAt(pixmap, surface.topLeft() + QPoint(4, 4));
    QCOMPARE(actualBackground.rgba(), background.rgba());
    const QColor visibleForeground = composite(foreground, background);
    bool found = false;
    for (const TextPaint &paint : style->paints) {
        if (paint.text != label) continue;
        found = true;
        QCOMPARE(paint.color.rgba(), foreground.rgba());
        // 验证实际传给栅格引擎的完整文本与绘制区域，不能只看 button->text()。
        QVERIFY2(paint.rect.adjusted(-1, -1, 1, 1).contains(paint.bounds),
                 qPrintable(QStringLiteral("Clipped label: %1 rect=%2,%3 %4x%5 bounds=%6,%7 %8x%9")
                     .arg(label).arg(paint.rect.x()).arg(paint.rect.y())
                     .arg(paint.rect.width()).arg(paint.rect.height()).arg(paint.bounds.x())
                     .arg(paint.bounds.y()).arg(paint.bounds.width()).arg(paint.bounds.height())));
        if (enabled)
            QVERIFY2(contrast(composite(paint.color, actualBackground), actualBackground) >= 4.5,
                     qPrintable(QStringLiteral("%1 contrast=%2:1").arg(label)
                                .arg(contrast(composite(paint.color, actualBackground), actualBackground))));
    }
    QVERIFY2(found, qPrintable(QStringLiteral("No full text paint: %1").arg(label)));
    // 抗锯齿允许混色，但文字区域必须真的有字形像素；不把边框当作文字。
    const QImage image = pixmap.toImage();
    const qreal scale = pixmap.devicePixelRatio();
    const QRect interior = surface.adjusted(9, 5, -9, -5);
    int glyphPixels = 0;
    for (int y = qRound(interior.top() * scale); y <= qRound(interior.bottom() * scale); ++y)
        for (int x = qRound(interior.left() * scale); x <= qRound(interior.right() * scale); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - visibleForeground.red()) < 40
                && qAbs(pixel.green() - visibleForeground.green()) < 40
                && qAbs(pixel.blue() - visibleForeground.blue()) < 40
                && pixel != background) ++glyphPixels;
        }
    QVERIFY2(glyphPixels >= 5, qPrintable(QStringLiteral("No rasterized glyphs: %1").arg(label)));
}

void saveScreenshot(QWidget *widget, const QString &name)
{
    const QString directory = qEnvironmentVariable("LQCOMPARE_HOME_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget->grab().save(QDir(directory).filePath(name + QStringLiteral(".png"))));
}
}

class HomeAppearanceTests : public QObject {
    Q_OBJECT
private slots:
    void palettePairsStayReadable_data();
    void palettePairsStayReadable();
    void nativeLightSurfaceNegativeControl();
    void closeGlyphAndRoutingFollowThemeAndTabMoves();
};

void HomeAppearanceTests::palettePairsStayReadable_data()
{
    QTest::addColumn<bool>("lightSurface");
    QTest::addColumn<int>("fontSize");
    for (const int size : {10, 16, 20}) {
        QTest::newRow(qPrintable(QStringLiteral("native-%1pt").arg(size))) << false << size;
        QTest::newRow(qPrintable(QStringLiteral("native-light-surface-%1pt").arg(size))) << true << size;
    }
}

void HomeAppearanceTests::palettePairsStayReadable()
{
    QFETCH(bool, lightSurface);
    QFETCH(int, fontSize);
    ApplicationState restore;
    auto *native = QStyleFactory::create(restore.styleName);
    QVERIFY(native);
    auto *style = new ObservedStyle(native, lightSurface);
    QApplication::setStyle(style);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Settings::OptionsRepository repository({directory.path(), true});
    Options::OptionsRuntime runtime(&repository);
    SessionArea area;
    area.resize(800, 600);
    area.show();
    area.activateWindow();
    auto *home = area.homePage();
    auto *tabs = area.findChild<QTabBar *>();
    auto *scroll = home->findChild<QScrollArea *>();
    QVERIFY(tabs);
    QVERIFY(scroll);
    const QStringList available{QStringLiteral("text"), QStringLiteral("text-merge"),
        QStringLiteral("folder"), QStringLiteral("folder-sync"), QStringLiteral("folder-merge")};
    for (const QString &id : available) home->setTypeAvailable(id, true);
    QSignalSpy newSession(home, &HomePage::sessionTypeRequested);
    QSignalSpy recentSession(home, &HomePage::recentSessionRequested);
    const QStringList themes{QStringLiteral("system"), QStringLiteral("dark"),
        QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("system")};
    for (int round = 0; round < themes.size(); ++round) {
        const QString theme = themes.at(round);
        const auto applied = repository.apply({{QStringLiteral("display.theme"), theme},
            {QStringLiteral("display.uiFontSize"), fontSize}});
        QVERIFY2(applied.ok, qPrintable(applied.error));
        QVERIFY(runtime.lastError().isEmpty());
        QCoreApplication::processEvents();
        const QPalette palette = QApplication::palette("QPushButton");
        const QPalette tabPalette = QApplication::palette("QTabBar");
        const QColor tabForeground = tabPalette.color(QPalette::Active, QPalette::ButtonText);
        const QColor tabBackground = tabPalette.color(QPalette::Active, QPalette::Button);
        const QColor selectedTabBackground = tabPalette.color(QPalette::Active, QPalette::Base);
        const QColor background = palette.color(QPalette::Active, QPalette::Button);
        const QColor foreground = palette.color(QPalette::Active, QPalette::ButtonText);
        // 在原生「浅色表面」故障注入行，system 仍然使用系统调色板。
        const QColor hovered = palette.color(QPalette::Active, QPalette::Midlight);
        for (QPushButton *button : home->findChildren<QPushButton *>()) {
            if (!button->objectName().startsWith(QStringLiteral("newSession-"))) continue;
            QTest::mouseMove(&area, QPoint(area.width() - 2, area.height() - 2));
            button->clearFocus();
            QCoreApplication::processEvents();
            QCOMPARE(button->font().pointSize(), fontSize);
            const QColor text = button->isEnabled() ? foreground
                : palette.color(QPalette::Disabled, QPalette::ButtonText);
            verifyRenderedText(style, button, button->rect(), button->text(), text,
                               palette.color(button->isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Button), button->isEnabled());
        }
        auto *textButton = home->findChild<QPushButton *>(QStringLiteral("newSession-text"));
        QVERIFY(textButton);
        // 高 DPI 或小屏幕下，800×600 的渲染窗口可能高于逻辑屏幕。先把真实
        // 指针目标滚到视口顶部，避免 mouseMove 落到屏外却误报配色失败。
        const int previousScroll = scroll->verticalScrollBar()->value();
        scroll->verticalScrollBar()->setValue(textButton->mapTo(scroll->widget(), QPoint()).y());
        const QPoint hoverPoint = textButton->rect().center();
        const QPoint globalHoverPoint = textButton->mapToGlobal(hoverPoint);
        QVERIFY2(QGuiApplication::screenAt(globalHoverPoint), "Hover target is outside every screen");
        QVERIFY(textButton->visibleRegion().contains(hoverPoint));
        QVERIFY(scroll->viewport()->rect().contains(textButton->mapTo(scroll->viewport(), hoverPoint)));
        QTest::mouseMove(textButton, hoverPoint);
        QTRY_VERIFY(textButton->underMouse());
        verifyRenderedText(style, textButton, textButton->rect(), textButton->text(), foreground, hovered);
        textButton->setFocus(Qt::TabFocusReason);
        QVERIFY(textButton->hasFocus());
        QCOMPARE(pixelAt(textButton->grab(), QPoint(0, textButton->height() / 2)),
                 palette.color(QPalette::Active, QPalette::Highlight));
        QTest::keyPress(textButton, Qt::Key_Space);
        verifyRenderedText(style, textButton, textButton->rect(), textButton->text(),
                           foreground, palette.color(QPalette::Active, QPalette::Base));
        QTest::keyRelease(textButton, Qt::Key_Space);
        QCOMPARE(newSession.count(), 1);
        QCOMPARE(newSession.takeFirst().first().toString(), QStringLiteral("text"));
        scroll->verticalScrollBar()->setValue(previousScroll);
        auto *disabled = home->findChild<QPushButton *>(QStringLiteral("newSession-text-edit"));
        QVERIFY(disabled);
        QVERIFY(!disabled->isEnabled());
        disabled->click();
        QCOMPARE(newSession.count(), 0);
        // 最近会话在每次主题切换后动态替换，两个文本行都必须完整绘制。
        home->setRecentSessions({{QStringLiteral("Recent comparison"), QStringLiteral("/sample/session.lqcompare")}});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        auto *recent = home->findChild<QPushButton *>(QStringLiteral("recentSession-0"));
        QVERIFY(recent);
        QTRY_VERIFY(recent->height() >= recent->sizeHint().height());
        verifyRenderedText(style, recent, recent->rect(), recent->text(), foreground, background);
        recent->click();
        QCOMPARE(recentSession.count(), 1);
        QCOMPARE(recentSession.takeFirst().first().toInt(), 0);
        QTest::mouseMove(&area, QPoint(area.width() - 2, area.height() - 2));
        tabs->setFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        verifyRenderedText(style, tabs, tabs->tabRect(0), tabs->tabText(0), tabForeground, selectedTabBackground);
        auto *session = new ExampleSession;
        QVERIFY(session->open());
        QCOMPARE(area.addSession(session), 1);
        QCoreApplication::processEvents();
        verifyRenderedText(style, tabs, tabs->tabRect(1), tabs->tabText(1), tabForeground, selectedTabBackground);
        verifyRenderedText(style, tabs, tabs->tabRect(0), tabs->tabText(0), tabForeground, tabBackground);
        tabs->setFocus(Qt::TabFocusReason);
        QTest::keyClick(tabs, Qt::Key_Left);
        QVERIFY(area.isHomeCurrent());
        // 非选中标签的悬停、禁用状态与键盘跳过仍由原有 QTabBar 负责。
        QTest::mouseMove(tabs, tabs->tabRect(1).center());
        QCoreApplication::processEvents();
        verifyRenderedText(style, tabs, tabs->tabRect(1), tabs->tabText(1), tabForeground,
                           tabPalette.color(QPalette::Active, QPalette::Midlight));
        area.setTabEnabled(1, false);
        QCoreApplication::processEvents();
        verifyRenderedText(style, tabs, tabs->tabRect(1), tabs->tabText(1),
                           tabPalette.color(QPalette::Disabled, QPalette::ButtonText),
                           tabPalette.color(QPalette::Disabled, QPalette::Button), false);
        QTest::keyClick(tabs, Qt::Key_Right);
        QVERIFY(area.isHomeCurrent());
        area.setTabEnabled(1, true);
        QTest::mouseMove(&area, QPoint(area.width() - 2, area.height() - 2));
        QCoreApplication::processEvents();
        if (round < 3)
            saveScreenshot(&area, QStringLiteral("%1-%2pt-%3").arg(lightSurface ? "light-surface" : "native")
                           .arg(fontSize).arg(theme));
        QVERIFY(area.closeSession(1));
        QVERIFY(area.isHomeCurrent());
        QCOMPARE(area.sessionCount(), 0);
    }
}

void HomeAppearanceTests::nativeLightSurfaceNegativeControl()
{
    ApplicationState restore;
    auto *native = QStyleFactory::create(restore.styleName);
    QVERIFY(native);
    auto *style = new ObservedStyle(native, true);
    QApplication::setStyle(style);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Settings::OptionsRepository repository({directory.path(), true});
    Options::OptionsRuntime runtime(&repository);
    QVERIFY(repository.apply({{QStringLiteral("display.theme"), QStringLiteral("dark")}}).ok);
    SessionArea area;
    area.resize(800, 600);
    area.show();
    QCoreApplication::processEvents();
    auto *home = area.homePage();
    auto *tabs = area.findChild<QTabBar *>();
    QVERIFY(tabs);
    auto *button = home->findChild<QPushButton *>(QStringLiteral("newSession-text"));
    QVERIFY(button);
    // 只移除产品局部样式即可稳定重现旧问题，不能靠改配色或阈值制造失败。
    home->setStyleSheet(QString());
    tabs->setStyleSheet(QString());
    // Windows 风格的原生焦点框会反相 (4,4) 处像素；这里测量的是注入的
    // 表面而非焦点框，焦点行为已在正常用例中独立检查。
    tabs->clearFocus();
    button->clearFocus();
    QCoreApplication::processEvents();
    const QColor foreground = QApplication::palette().color(QPalette::ButtonText);
    const QColor buttonBackground = pixelAt(button->grab(), QPoint(4, 4));
    const QColor tabBackground = pixelAt(tabs->grab(), tabs->tabRect(0).topLeft() + QPoint(4, 4));
    QCOMPARE(buttonBackground, QColor(245, 245, 245));
    QCOMPARE(tabBackground, QColor(245, 245, 245));
    QVERIFY(contrast(foreground, buttonBackground) < 1.2);
    QVERIFY(contrast(foreground, tabBackground) < 1.2);
    saveScreenshot(&area, QStringLiteral("negative-control-dark"));
}

void HomeAppearanceTests::closeGlyphAndRoutingFollowThemeAndTabMoves()
{
    ApplicationState restore;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Settings::OptionsRepository repository({directory.path(), true});
    Options::OptionsRuntime runtime(&repository);
    SessionArea area;
    area.resize(800, 600);
    area.show();
    area.activateWindow();
    auto *tabs = area.findChild<QTabBar *>();
    QVERIFY(tabs);
    auto *first = new ExampleSession;
    auto *second = new ExampleSession;
    first->setTitle(QStringLiteral("First comparison"));
    second->setTitle(QStringLiteral("Second comparison"));
    QVERIFY(first->open());
    QVERIFY(second->open());
    area.addSession(first);
    area.addSession(second);
    QSignalSpy closeRequests(&area, &QTabWidget::tabCloseRequested);
    const auto closeButton = [tabs](int index) -> QAbstractButton * {
        for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide})
            if (auto *button = qobject_cast<QAbstractButton *>(tabs->tabButton(index, side)))
                return button;
        return nullptr;
    };
    QAbstractButton *firstClose = closeButton(area.indexOf(first->widget()));
    QAbstractButton *secondClose = closeButton(area.indexOf(second->widget()));
    QVERIFY(firstClose);
    QVERIFY(secondClose);
    QVERIFY(!closeButton(0));
    for (const QString &theme : {QStringLiteral("system"), QStringLiteral("dark"),
                                QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("system")}) {
        QVERIFY(repository.apply({{QStringLiteral("display.theme"), theme},
                                  {QStringLiteral("display.uiFontSize"), 16}}).ok);
        QCoreApplication::processEvents();
        QCOMPARE(closeButton(area.indexOf(first->widget())), firstClose);
        QCOMPARE(closeButton(area.indexOf(second->widget())), secondClose);
        const QColor foreground = QApplication::palette("QTabBar").color(QPalette::ButtonText);
        QTest::mouseMove(&area, QPoint(area.width() - 2, area.height() - 2));
        QCoreApplication::processEvents();
        const QPixmap pixmap = tabs->grab();
        const QPoint origin = secondClose->mapTo(tabs, QPoint());
        const QColor background = pixelAt(pixmap, origin + QPoint(1, 1));
        const QColor visibleForeground = composite(foreground, background);
        QVERIFY2(contrast(visibleForeground, background) >= 3.0, "Close icon lacks contrast");
        int glyphPixels = 0;
        for (int y = 3; y < secondClose->height() - 3; ++y)
            for (int x = 3; x < secondClose->width() - 3; ++x) {
                const QColor pixel = pixelAt(pixmap, origin + QPoint(x, y));
                if (qAbs(pixel.red() - visibleForeground.red()) < 32
                    && qAbs(pixel.green() - visibleForeground.green()) < 32
                    && qAbs(pixel.blue() - visibleForeground.blue()) < 32) ++glyphPixels;
            }
        QVERIFY2(glyphPixels >= 6, "Close icon has no visible foreground strokes");
        // 每次换主题后点击仍走 QTabWidget 自己的关闭请求及现有 dirty 确认。
        second->setDirty(true);
        int prompts = 0;
        area.setClosePrompt([&prompts, second](CompareSession *session) {
            if (session == second) ++prompts;
            return SessionArea::CloseChoice::Cancel;
        });
        QTest::mouseClick(secondClose, Qt::LeftButton);
        QCOMPARE(closeRequests.count(), 1);
        QCOMPARE(closeRequests.takeFirst().first().toInt(), area.indexOf(second->widget()));
        QCOMPARE(prompts, 1);
        QCOMPARE(area.sessionCount(), 2);
        if (theme == QStringLiteral("dark")) saveScreenshot(&area, QStringLiteral("native-close-dark"));
    }
    tabs->moveTab(area.indexOf(second->widget()), 1);
    QCOMPARE(area.indexOf(area.homePage()), 0);
    QCOMPARE(area.indexOf(second->widget()), 1);
    QCOMPARE(closeButton(1), secondClose);
    area.setClosePrompt([second](CompareSession *session) {
        return session == second ? SessionArea::CloseChoice::Discard : SessionArea::CloseChoice::Cancel;
    });
    QTest::mouseClick(secondClose, Qt::LeftButton);
    QCOMPARE(closeRequests.count(), 1);
    QCOMPARE(closeRequests.takeFirst().first().toInt(), 1);
    QCOMPARE(area.sessionCount(), 1);
    QCOMPARE(area.sessionAt(1), first);
    auto *third = new ExampleSession;
    QVERIFY(third->open());
    area.addSession(third);
    QCoreApplication::processEvents();
    QAbstractButton *thirdClose = closeButton(area.indexOf(third->widget()));
    QVERIFY(thirdClose);
    QTest::mouseClick(thirdClose, Qt::LeftButton);
    QCOMPARE(closeRequests.count(), 1);
    QCOMPARE(closeRequests.takeFirst().first().toInt(), 2);
    QCOMPARE(area.sessionCount(), 1);
    QCOMPARE(closeButton(1), firstClose);
    QTest::mouseClick(firstClose, Qt::LeftButton);
    QCOMPARE(closeRequests.count(), 1);
    QCOMPARE(closeRequests.takeFirst().first().toInt(), 1);
    QCOMPARE(area.sessionCount(), 0);
    QVERIFY(area.isHomeCurrent());
    QVERIFY(!closeButton(0));
}

QTEST_MAIN(HomeAppearanceTests)
#include "tst_homeappearance.moc"
