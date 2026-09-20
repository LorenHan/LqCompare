#ifndef LQCOMPARE_FOLDERMERGEVIEW_H
#define LQCOMPARE_FOLDERMERGEVIEW_H

#include "foldermergeplan.h"

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace LqCompare {
class FolderMergeSession;

class FolderMergeView : public QWidget {
    Q_OBJECT
public:
    explicit FolderMergeView(FolderMergeSession *session, QWidget *parent = nullptr);
    QString selectedPath() const;

private:
    void refreshPaths();
    void refreshPlan();
    void refreshActions();
    void scan();
    void choose(FolderMerge::Decision decision);
    void resetChoice();
    void showPreview();
    void applyFilter();
    void showFailure(const QString &message);
    QPointer<FolderMergeSession> m_session;
    QLineEdit *m_base, *m_left, *m_right, *m_output;
    QTreeWidget *m_tree;
    QLabel *m_summary, *m_baseNotice, *m_executionNotice, *m_details;
    QPushButton *m_scan, *m_cancel, *m_takeLeft, *m_takeRight, *m_takeBase, *m_ignore, *m_reset, *m_textMerge;
    QComboBox *m_directoryPolicy;
    QCheckBox *m_conflictsOnly;
};
}
#endif
