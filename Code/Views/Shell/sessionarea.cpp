#include "sessionarea.h"
#include "comparesession.h"
#include "homepage.h"

#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>
#include <QTabBar>

namespace LqCompare {
namespace {
// QTabBar 原有关闭按钮仍负责点击、索引和 tabCloseRequested；只替换它的
// 关闭图元。原生样式的缓存图标可能一直是黑色，不能靠文字调色板给它着色。
class SessionTabCloseStyle final : public QProxyStyle {
public:
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element != PE_IndicatorTabClose) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        const QPalette::ColorGroup group = option->state & State_Enabled
                ? QPalette::Active : QPalette::Disabled;
        const QPalette palette = QApplication::palette("QTabBar");
        painter->save();
        if (option->state & (State_MouseOver | State_Sunken))
            painter->fillRect(option->rect, palette.brush(group, QPalette::Midlight));
        if (option->state & State_HasFocus) {
            painter->setPen(palette.color(group, QPalette::Highlight));
            painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        }
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(palette.color(group, QPalette::ButtonText), 1.5));
        const qreal side = qMax(6, qMin(option->rect.width(), option->rect.height()) / 2);
        const QRectF cross(QPointF(option->rect.center()) - QPointF(side / 2, side / 2),
                           QSizeF(side, side));
        painter->drawLine(cross.topLeft(), cross.bottomRight());
        painter->drawLine(cross.topRight(), cross.bottomLeft());
        painter->restore();
    }
};
}

SessionArea::SessionArea(QWidget *parent) : QTabWidget(parent)
{
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(true);
    // 原生标签可能忽略背景色而保留浅色文字。范围限定到会话栏，不影响
    // Ribbon 或会话内容；和首页一样，主题/字体改变时更新局部样式缓存。
    const auto updateAppearance = [this] {
        const QPalette palette = QApplication::palette("QTabBar");
        const auto color = [&palette](QPalette::ColorRole role,
                                      QPalette::ColorGroup group = QPalette::Active) {
            const QColor value = palette.color(group, role);
            return QStringLiteral("rgba(%1,%2,%3,%4)")
                    .arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alpha());
        };
        tabBar()->setStyleSheet(QStringLiteral(
            "QTabBar::tab { background-color: %1; color: %2;"
            " border: 1px solid %3; border-bottom: 2px solid %3;"
            " border-radius: 0; padding: 4px 10px; }"
            "QTabBar::tab:hover:enabled { background-color: %4; }"
            "QTabBar::tab:selected { background-color: %5; border-bottom-color: %6; }"
            "QTabBar:focus::tab:selected { border-color: %6; }"
            "QTabBar::tab:disabled { background-color: %7; color: %8; }")
            .arg(color(QPalette::Button), color(QPalette::ButtonText), color(QPalette::Mid),
                 color(QPalette::Midlight), color(QPalette::Base), color(QPalette::Highlight),
                 color(QPalette::Button, QPalette::Disabled),
                 color(QPalette::ButtonText, QPalette::Disabled)));
    };
    updateAppearance();
    connect(qApp, &QApplication::paletteChanged, tabBar(), updateAppearance);
    connect(qApp, &QApplication::fontChanged, tabBar(), updateAppearance);
    m_home = new HomePage(this);
    addTab(m_home, tr("Home"));
    tabBar()->setTabButton(indexOf(m_home), QTabBar::RightSide, nullptr);
    tabBar()->setTabButton(indexOf(m_home), QTabBar::LeftSide, nullptr);
    connect(this, &QTabWidget::tabCloseRequested, this, &SessionArea::closeSession);
    connect(this, &QTabWidget::currentChanged, this, [this] {
        auto *session = currentSession();
        emit activeSessionChanged(session);
        emit activeSessionStateChanged();
        emit statusTextChanged(session ? session->statusText() : tr("Ready"));
        // 切标签时严重度要跟着一起播：新会话可能是「行尾混合」那一个，
        // 只播文本的话图标会留着上一个会话的状态。
        emit statusSeverityChanged(session ? session->statusSeverity()
                                           : CompareSession::StatusSeverity::Normal);
    });
    // A moved Home tab must never be mistaken for a closable session.
    connect(tabBar(), &QTabBar::tabMoved, this, [this] {
        const int homeIndex = indexOf(m_home);
        if (homeIndex > 0) tabBar()->moveTab(homeIndex, 0);
    });
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QTabBar::customContextMenuRequested, this, [this](const QPoint &point) {
        const int index = tabBar()->tabAt(point);
        if (!sessionAt(index)) return;
        QWidget *selected = widget(index);
        QMenu menu(this);
        QAction *close = menu.addAction(tr("Close"));
        QAction *others = menu.addAction(tr("Close Others"));
        QAction *right = menu.addAction(tr("Close to the Right"));
        QAction *all = menu.addAction(tr("Close All"));
        QAction *choice = menu.exec(tabBar()->mapToGlobal(point));
        if (!choice) return;
        QList<QWidget *> pages;
        for (int i = 0; i < count(); ++i) {
            QWidget *page = widget(i);
            if (!m_sessions.contains(page)) continue;
            if (choice == all || (choice == close && page == selected)
                || (choice == others && page != selected)
                || (choice == right && i > indexOf(selected))) pages.append(page);
        }
        closePages(pages);
    });
}

int SessionArea::addSession(CompareSession *session)
{
    if (!session) return -1;
    QWidget *page = session->createWidget(this);
    if (!page) return -1;
    if (m_sessions.contains(page)) return indexOf(page);
    session->setParent(this);
    m_sessions.insert(page, session);
    const int index = addTab(page, session->title());
    // 关闭按钮所在侧由原生风格决定，保留 Qt 创建的同一个按钮及其连接。
    for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
        if (QWidget *closeButton = tabBar()->tabButton(index, side)) {
            auto *appearance = new SessionTabCloseStyle;
            appearance->setParent(closeButton);
            closeButton->setStyle(appearance);
        }
    }
    connect(session, &CompareSession::titleChanged, this, [this, session] { updateLabel(session); });
    connect(session, &CompareSession::dirtyChanged, this, [this, session] {
        updateLabel(session);
        if (session == currentSession()) emit activeSessionStateChanged();
    });
    connect(session, &CompareSession::stateChanged, this, [this, session] {
        if (session == currentSession()) emit activeSessionStateChanged();
    });
    connect(session, &CompareSession::statusTextChanged, this, [this, session](const QString &text) {
        if (session == currentSession()) emit statusTextChanged(text);
    });
    connect(session, &CompareSession::statusSeverityChanged, this,
            [this, session](CompareSession::StatusSeverity severity) {
                if (session == currentSession()) emit statusSeverityChanged(severity);
            });
    connect(session, &CompareSession::errorReported, this, [this](const SessionError &error) {
        emit errorReported(error.message, error.detail);
    });
    updateLabel(session);
    setCurrentIndex(index);
    emit sessionCountChanged(sessionCount());
    return index;
}

void SessionArea::updateLabel(CompareSession *session)
{
    const int index = indexOf(session->widget());
    if (index < 0) return;
    setTabText(index, session->title() + (session->isDirty() ? QStringLiteral(" *") : QString()));
    setTabToolTip(index, session->title());
    if (session == currentSession()) emit activeSessionStateChanged();
}

CompareSession *SessionArea::sessionAt(int index) const { return m_sessions.value(widget(index)); }
CompareSession *SessionArea::currentSession() const { return sessionAt(currentIndex()); }
bool SessionArea::isHomeCurrent() const { return currentWidget() == m_home; }
int SessionArea::sessionCount() const { return m_sessions.size(); }
void SessionArea::setClosePrompt(ClosePrompt prompt) { m_closePrompt = std::move(prompt); }

bool SessionArea::mayClose(CompareSession *session)
{
    if (!session || !session->isDirty()) return true;
    CloseChoice choice;
    if (m_closePrompt) {
        choice = m_closePrompt(session);
    } else {
        QMessageBox box(QMessageBox::Question, tr("Unsaved Changes"),
                        tr("Save changes to %1 before closing?").arg(session->title()),
                        (session->canSave() ? QMessageBox::Save : QMessageBox::NoButton) | QMessageBox::Discard | QMessageBox::Cancel, this);
        if (!session->canSave()) box.setText(tr("Discard the unsaved decisions in %1? This session cannot save them yet.").arg(session->title()));
        box.setDefaultButton(session->canSave() ? QMessageBox::Save : QMessageBox::Cancel);
        const int answer = box.exec();
        choice = answer == QMessageBox::Save ? CloseChoice::Save
               : answer == QMessageBox::Discard ? CloseChoice::Discard : CloseChoice::Cancel;
    }
    if (choice == CloseChoice::Cancel) return false;
    if (choice == CloseChoice::Discard) return true;
    QString error;
    if (!session->save(&error)) return false;
    return !session->isDirty();
}

bool SessionArea::closePages(const QList<QWidget *> &pages)
{
    // Complete all decisions before removing any tab. Cancel keeps the workspace intact.
    for (QWidget *page : pages) {
        if (!mayClose(m_sessions.value(page))) return false;
    }
    for (QWidget *page : pages) {
        auto *session = m_sessions.take(page);
        if (!session) continue;
        emit sessionClosing(session);
        session->close();
        removeTab(indexOf(page));
        page->deleteLater();
        session->deleteLater();
    }
    if (m_sessions.isEmpty()) setCurrentWidget(m_home);
    emit sessionCountChanged(sessionCount());
    emit activeSessionStateChanged();
    return true;
}

bool SessionArea::closeSession(int index)
{
    if (!sessionAt(index)) return false;
    return closePages({widget(index)});
}
bool SessionArea::closeAllSessions()
{
    QList<QWidget *> pages;
    for (int i = 0; i < count(); ++i) if (sessionAt(i)) pages.append(widget(i));
    return closePages(pages);
}
void SessionArea::closeCurrentSession() { closeSession(currentIndex()); }
}
