#include "sessionarea.h"
#include "comparesession.h"
#include "homepage.h"

#include <QMenu>
#include <QMessageBox>
#include <QTabBar>

namespace LqCompare {
SessionArea::SessionArea(QWidget *parent) : QTabWidget(parent)
{
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(true);
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
