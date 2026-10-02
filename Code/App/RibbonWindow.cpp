#include "RibbonWindow.h"

#include "commandregistry.h"
#include "commandactionbinder.h"
#include "logging.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QStyle>
#include <QTabBar>
#include <QTimer>

#include <cmath>

namespace LqCompare {
namespace {
class CommandSearchEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        // 长按回车不能连续弹窗；Esc 清空搜索并把键盘焦点交给后续控件。
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && event->isAutoRepeat()) {
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            clear();
            focusNextChild();
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

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
    // 原生框架没有独立标题区：搜索使用主窗口布局中的单独一行，
    // 避免依赖库按标题坐标定位时遮挡标签；保留原 Ribbon、页面与命令组。
    ribbonBar()->setSearchBarAppearance(RibbonBar::SearchBarHidden);
    auto *row = new QWidget(this);
    row->setObjectName(QStringLiteral("commandSearchRow"));
    auto *layout = new QHBoxLayout(row);
    const int horizontal = style()->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, row);
    const int vertical = qMax(2, style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, row));
    layout->setContentsMargins(horizontal, vertical, horizontal, vertical);
    m_commandSearch = new CommandSearchEdit(row);
    m_commandSearch->setObjectName(QStringLiteral("commandSearch"));
    const auto updateFont = [this](const QFont &font) {
        m_commandSearch->setFont(font);
        // 宽屏也保持紧凑，最大宽度随字体度量变化，不固定设备像素。
        m_commandSearch->setMaximumWidth(m_commandSearch->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 36);
    };
    updateFont(QApplication::font());
    connect(qApp, &QApplication::fontChanged, this, updateFont);
    m_commandSearch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateSearchAppearance();
    connect(qApp, &QApplication::paletteChanged, this, &RibbonWindow::updateSearchAppearance);
    layout->addStretch(1);
    layout->addWidget(m_commandSearch, 2);
    layout->addStretch(1);
    setMenuWidget(row);
    if (auto *tabs = ribbonBar()->findChild<QTabBar *>(QStringLiteral("lqRibbonTabBar")))
        setTabOrder(m_commandSearch, tabs);
    const auto submit = [this] {
        const QString query = m_commandSearch->text();
        if (m_searchRunning || m_searchQueued || query.trimmed().isEmpty()) return;
        m_searchQueued = true;
        const quint64 generation = ++m_searchGeneration;
        // 先退出输入控件自己的按键/点击事件；模态期间关闭窗口不能销毁尚在处理事件的输入控件。
        QTimer::singleShot(0, this, [this, query, generation] {
            if (!m_searchQueued || generation != m_searchGeneration) return;
            m_searchQueued = false;
            showHelp(query);
        });
    };
    connect(m_commandSearch, &QLineEdit::textChanged, this, [this] {
        // Esc 或继续输入会撤销尚未打开的旧查询，不能让排队回调覆盖新意图。
        m_searchQueued = false;
        ++m_searchGeneration;
    });
    connect(m_commandSearch, &QLineEdit::returnPressed, this, submit);
    auto *action = m_commandSearch->addAction(QIcon(QStringLiteral(":/Pictures/ribbon_search.svg")),
                                             QLineEdit::TrailingPosition);
    action->setObjectName(QStringLiteral("commandSearchSubmit"));
    connect(action, &QAction::triggered, this, submit);
}

void RibbonWindow::updateSearchAppearance()
{
    if (!m_commandSearch) return;
    // 局部样式表会缓存调色板；切换主题时必须从应用重新取色。
    const QPalette palette = QApplication::palette();
    m_commandSearch->setPalette(palette);
    m_commandSearch->setStyleSheet(QStringLiteral(
        "QLineEdit#commandSearch { border: 1px solid %1; border-radius: 0px;"
        " padding: 3px 6px; background: %2; color: %3; }"
        "QLineEdit#commandSearch:focus { border-color: %4; }")
        .arg(palette.color(QPalette::Mid).name(), palette.color(QPalette::Base).name(),
             palette.color(QPalette::Text).name(), palette.color(QPalette::Highlight).name()));
}

void RibbonWindow::showHelp(const QString &text)
{
    const QString query = text.trimmed();
    if (query.isEmpty() || m_searchRunning) {
        return;
    }

    // 模态确认期间忽略嵌套搜索事件，单次输入最多走一次注册表入口。
    m_searchRunning = true;

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

    // 模态循环与命令处理器都可能关闭窗口。使用有生命期检查的对话框，
    // 先销毁确认框并恢复重入状态，再进入注册表，避免关闭命令访问已析构成员。
    const QPointer<RibbonWindow> guard(this);
    QPointer<QMessageBox> box = new QMessageBox(this);
    box->setWindowTitle(tr("Command Search"));
    box->setTextFormat(Qt::PlainText);
    QString commandId;
    if (matches.isEmpty()) {
        box->setIcon(QMessageBox::Information);
        box->setText(tr("No command matches \"%1\".").arg(query));
        box->setStandardButtons(QMessageBox::Ok);
    } else if (candidates.size() == 1) {
        commandId = candidates.first();
        box->setText(tr("Command: %1\n\nRun it now?").arg(matches.join(QLatin1Char('\n'))));
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        // 搜索回车只负责打开确认；转入对话框的重复按键不能默认执行命令。
        box->setDefaultButton(QMessageBox::No);
        box->setEscapeButton(QMessageBox::No);
    } else {
        box->setIcon(QMessageBox::Information);
        box->setText(tr("Matching commands:\n\n%1").arg(matches.join(QLatin1Char('\n'))));
        box->setStandardButtons(QMessageBox::Ok);
    }
    const int answer = box->exec();
    delete box.data();
    if (!guard) return;
    m_searchRunning = false;
    if (!commandId.isEmpty() && answer == QMessageBox::Yes)
        CommandRegistry::instance().trigger(commandId);
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
    if (m_commandSearch) {
        m_commandSearch->setPlaceholderText(tr("Search commands"));
        m_commandSearch->setAccessibleName(tr("Search commands"));
        m_commandSearch->setAccessibleDescription(tr("Search by command name or ID. Press Enter to search, Escape to leave."));
        if (auto *action = m_commandSearch->findChild<QAction *>(QStringLiteral("commandSearchSubmit"))) {
            action->setText(tr("Search commands"));
            action->setToolTip(tr("Search commands"));
        }
    }
}

} // namespace LqCompare
