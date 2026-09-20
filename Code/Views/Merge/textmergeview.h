#ifndef LQCOMPARE_TEXTMERGEVIEW_H
#define LQCOMPARE_TEXTMERGEVIEW_H
#include <QPointer>
#include <QWidget>
class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
namespace LqCompare {
class TextMergeSession;
class TextMergeView : public QWidget {
    Q_OBJECT
public:
    explicit TextMergeView(TextMergeSession *session, QWidget *parent = nullptr);
private:
    void setUseLocalShortcuts(bool enabled);
    void refresh();
    void highlight(bool navigate);
    void saveOutput(bool choosePath);
    void showError(const QString &error);
    QPointer<TextMergeSession> m_session;
    QPlainTextEdit *m_left = nullptr, *m_base = nullptr, *m_right = nullptr, *m_output = nullptr;
    QWidget *m_baseContainer = nullptr;
    QLabel *m_status = nullptr, *m_outputLabel = nullptr;
    QListWidget *m_conflicts = nullptr;
    QAction *m_acceptLeft = nullptr, *m_acceptRight = nullptr, *m_acceptBase = nullptr;
    QAction *m_leftRight = nullptr, *m_rightLeft = nullptr, *m_resolve = nullptr, *m_reopen = nullptr;
    QAction *m_undo = nullptr, *m_redo = nullptr, *m_save = nullptr;
    bool m_refreshing = false;
};
}
#endif
