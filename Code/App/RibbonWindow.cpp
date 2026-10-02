#include "RibbonWindow.h"

#include "commandregistry.h"
#include "commandactionbinder.h"
#include "logging.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <QTabBar>

#include <cmath>

namespace LqCompare {
namespace {
QColor tabHoverBackground(const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    const auto luminance = [](const QColor &color) {
        const auto linear = [](double value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF())
                + 0.0722 * linear(color.blueF());
    };
    // Midlight 并不保证与 WindowText 配对。轻染 Window，必要时减弱色量，
    // 保住原文字的对比度；不修改用户调色板或引入固定的黑/白文字。
    for (const QColor &tint : {palette.color(QPalette::Highlight), palette.color(QPalette::Base)}) {
        for (int weight = 16; weight > 0; weight /= 2) {
            const auto blend = [weight](int base, int color) {
                return ((100 - weight) * base + weight * color) / 100;
            };
            const QColor hover(blend(window.red(), tint.red()), blend(window.green(), tint.green()),
                               blend(window.blue(), tint.blue()));
            const double a = luminance(text), b = luminance(hover);
            if (hover.rgba() != window.rgba()
                && (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) >= 4.5) return hover;
        }
    }
    return window;
}
}

RibbonWindow::RibbonWindow(QWidget *parent) : LqRibbon::RibbonMainWindow(parent)
{
    // 默认外观：Office 2016 Blue。样式可在选项中切换（UI-002）。
    ribbonBar()->setRibbonStyle(RibbonBar::Office2016Blue);
    ribbonBar()->setFont(QApplication::font());
    ribbonBar()->setTitleGroupsVisible(true);
    ribbonBar()->setSimplifiedModeEnabled(false);
    ribbonBar()->setMinimizationEnabled(true);

    // 原生窗口框架下没有蓝色标题背景，不能沿用 Office 标题栏的白色标签字。
    // 只覆盖已有标签栏的颜色和直角状态，不替换控件、库样式或页面布局。
    updateTabAppearance();
    connect(qApp, &QApplication::paletteChanged, this, &RibbonWindow::updateTabAppearance);
    connect(ribbonBar(), &RibbonBar::ribbonStyleChanged,
            this, &RibbonWindow::updateTabAppearance);

    setupSearchBar();

    connect(ribbonBar(), &RibbonBar::showRibbonContextMenu, this,
            &RibbonWindow::showRibbonContextMenu);

    switchLanguage();
}

void RibbonWindow::updateTabAppearance()
{
    auto *tabs = ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar"));
    if (!tabs) return;
    // 从应用取色，避免读回被 Ribbon 样式表改写过的控件调色板。
    const QPalette palette = QApplication::palette();
    const auto color = [&palette](QPalette::ColorRole role) { return palette.color(role).name(); };
    tabs->setStyleSheet(QStringLiteral(
        "QTabBar#lqRibbonTabBar { background: %1; }"
        "QTabBar#lqRibbonTabBar::tab { color: %2; background: %1; border-radius: 0px; }"
        "QTabBar#lqRibbonTabBar::tab:selected {"
        " color: %3; background: %4; border-radius: 0px;"
        " border-left-color: %5; border-right-color: %5; border-top-color: %5;"
        " border-bottom-color: %6; }"
        "QTabBar#lqRibbonTabBar::tab:hover:!selected { background: %7; border-radius: 0px; }"
        "QTabBar#lqRibbonTabBar::tab:selected:focus {"
        " border-left: 1px dotted %3; border-right: 1px dotted %3; border-top: 1px dotted %3; }"
        "QTabBar#lqRibbonTabBar::tab:disabled { color: %8; }")
        .arg(color(QPalette::Window), color(QPalette::WindowText), color(QPalette::Text),
             color(QPalette::Base), color(QPalette::Mid), color(QPalette::Highlight),
             tabHoverBackground(palette).name(), palette.color(QPalette::Disabled, QPalette::WindowText).name()));
}

void RibbonWindow::setupQuickAccessBar()
{
    RibbonQuickAccessBar *bar = ribbonBar()->quickAccessBar();
    if (!bar) {
        return;
    }
    // UI-003：QAT 只放跨会话通用命令，不放会话专属命令。
    static const char *const kAlwaysVisible[] = {
        "file.open", "file.save", "edit.undo", "edit.redo", "nav.prevdiff", "nav.nextdiff",
    };
    for (const char *id : kAlwaysVisible) {
        auto *binder = CommandActionBinder::forWindow(this);
        auto *action = binder->createAction(QString::fromLatin1(id), bar, {}, {},
                                           CommandActionBinder::Presentation::Toolbar, false);
        bar->addAction(action);
    }
}

void RibbonWindow::setupSearchBar()
{
    ribbonBar()->setSearchVisible(true);
    ribbonBar()->setSearchBarAppearance(RibbonBar::SearchBarCentral);
    if (RibbonSearchBar *search = ribbonBar()->searchBar()) {
        connect(search, &RibbonSearchBar::showHelp, this, &RibbonWindow::showHelp);
    }
}

void RibbonWindow::showHelp(const QString &text)
{
    const QString query = text.trimmed();
    if (query.isEmpty()) {
        return;
    }

    // UI-004：搜索范围是命令注册表全量条目，命中后可直接执行。
    QStringList matches;
    QStringList candidates;
    for (const Command &command : CommandRegistry::instance().all()) {
        if (!command.text.contains(query, Qt::CaseInsensitive)
            && !command.id.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        const QString state = command.isImplemented() ? tr("ready") : tr("not implemented");
        matches.append(QStringLiteral("%1  [%2]  (%3)").arg(command.text, command.actionId, state));
        if (command.isImplemented()) {
            candidates.append(command.id);
        }
    }

    if (matches.isEmpty()) {
        QMessageBox::information(this, tr("Command Search"),
                                 tr("No command matches \"%1\".").arg(query));
        return;
    }

    const QString body = matches.join(QLatin1Char('\n'));
    if (candidates.size() == 1) {
        const QString commandId = candidates.first();
        const QString question = tr("Command: %1\n\nRun it now?").arg(body);
        QMessageBox box(this);
        box.setWindowTitle(tr("Command Search"));
        box.setText(question);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        if (box.exec() == QMessageBox::Yes) {
            CommandRegistry::instance().trigger(commandId);
        }
        return;
    }

    QMessageBox::information(this, tr("Command Search"),
                             tr("Matching commands:\n\n%1").arg(body));
}

void RibbonWindow::showRibbonContextMenu(QMenu *menu, QContextMenuEvent *event)
{
    // UI-006：屏蔽 LqRibbon 的默认上下文菜单，只保留我们自己的两项。
    Q_UNUSED(event)
    if (!menu) {
        return;
    }
    menu->clear();
    auto *customize = menu->addAction(tr("Customize the Ribbon..."));
    connect(customize, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, tr("Customize the Ribbon"),
                                 tr("Ribbon customization is specified in PRD entry UI-006."));
    });
    menu->addSeparator();
    auto *about = menu->addAction(tr("About LqCompare"));
    connect(about, &QAction::triggered, this,
            []() { CommandRegistry::instance().trigger(QStringLiteral("help.about")); });
}

void RibbonWindow::switchLanguage()
{
    if (RibbonSearchBar *search = ribbonBar()->searchBar()) {
        search->setPlaceholderText(tr("Search commands"));
    }
}

} // namespace LqCompare
