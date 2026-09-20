#ifndef LQCOMPARE_SESSIONAREA_H
#define LQCOMPARE_SESSIONAREA_H

#include <QHash>
#include <QTabWidget>
#include <functional>

namespace LqCompare {
class HomePage;
class CompareSession;

class SessionArea : public QTabWidget
{
    Q_OBJECT
public:
    enum class CloseChoice { Save, Discard, Cancel };
    using ClosePrompt = std::function<CloseChoice(CompareSession *)>;

    explicit SessionArea(QWidget *parent = nullptr);
    HomePage *homePage() const { return m_home; }
    // Takes ownership only when the session can build a view.
    int addSession(CompareSession *session);
    CompareSession *sessionAt(int index) const;
    CompareSession *currentSession() const;
    bool isHomeCurrent() const;
    int sessionCount() const;
    bool closeSession(int index);
    bool closeAllSessions();
    void setClosePrompt(ClosePrompt prompt);

public slots:
    void closeCurrentSession();

signals:
    void sessionCountChanged(int count);
    void sessionClosing(LqCompare::CompareSession *session);
    void activeSessionChanged(LqCompare::CompareSession *session);
    void activeSessionStateChanged();
    void statusTextChanged(const QString &text);
    void errorReported(const QString &message, const QString &detail);

private:
    bool mayClose(CompareSession *session);
    bool closePages(const QList<QWidget *> &pages);
    void updateLabel(CompareSession *session);
    HomePage *m_home = nullptr;
    QHash<QWidget *, CompareSession *> m_sessions;
    ClosePrompt m_closePrompt;
};
}
#endif
