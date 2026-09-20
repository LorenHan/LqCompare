#include "RibbonWindow.h"

#include "commandregistry.h"
#include "commandactionbinder.h"
#include "logging.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>

namespace LqCompare {

RibbonWindow::RibbonWindow(QWidget *parent) : LqRibbon::RibbonMainWindow(parent)
{
    // 默认外观：Office 2016 Blue。样式可在选项中切换（UI-002）。
    ribbonBar()->setRibbonStyle(RibbonBar::Office2016Blue);
    ribbonBar()->setFont(QApplication::font());
    ribbonBar()->setTitleGroupsVisible(true);
    ribbonBar()->setSimplifiedModeEnabled(false);
    ribbonBar()->setMinimizationEnabled(true);

    setupSearchBar();

    connect(ribbonBar(), &RibbonBar::showRibbonContextMenu, this,
            &RibbonWindow::showRibbonContextMenu);

    switchLanguage();
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
