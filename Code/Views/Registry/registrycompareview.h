#ifndef LQCOMPARE_REGISTRYCOMPAREVIEW_H
#define LQCOMPARE_REGISTRYCOMPAREVIEW_H

#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

namespace LqCompare {
class RegistryCompareSession;

class RegistryCompareView : public QWidget {
    Q_OBJECT
public:
    enum DataRole { StatusRole = Qt::UserRole, KindRole, KeyPathRole, ValueNameRole };
    explicit RegistryCompareView(RegistryCompareSession *session, QWidget *parent = nullptr);

private:
    void comparePaths();
    void rebuildTree();
    void applyFilter();
    void syncPaths();
    void syncOptions();
    QPointer<RegistryCompareSession> m_session;
    QLineEdit *m_leftPath = nullptr, *m_rightPath = nullptr;
    QComboBox *m_statusFilter = nullptr, *m_codec = nullptr;
    QLabel *m_error = nullptr, *m_summary = nullptr;
    QTreeWidget *m_tree = nullptr;
};

} // namespace LqCompare
#endif
