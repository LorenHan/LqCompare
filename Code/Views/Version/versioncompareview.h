#ifndef LQCOMPARE_VERSIONCOMPAREVIEW_H
#define LQCOMPARE_VERSIONCOMPAREVIEW_H
#include <QWidget>
class QLineEdit;
class QLabel;
class QTreeWidget;
namespace LqCompare {
class VersionCompareSession;
class VersionCompareView : public QWidget {
    Q_OBJECT
public:
    explicit VersionCompareView(VersionCompareSession *session, QWidget *parent = nullptr);
private:
    void comparePaths();
    void refresh();
    VersionCompareSession *m_session;
    QLineEdit *m_leftPath, *m_rightPath;
    QLabel *m_error;
    QTreeWidget *m_table;
};
}
#endif
