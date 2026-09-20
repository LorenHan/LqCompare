#ifndef LQCOMPARE_ARCHIVECOMPAREVIEW_H
#define LQCOMPARE_ARCHIVECOMPAREVIEW_H

#include "archivecompare.h"
#include <QAbstractTableModel>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace LqCompare {

class ArchiveEntryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        Path, Status, LeftSize, RightSize, LeftPackedSize, RightPackedSize,
        LeftCrc, RightCrc, LeftModified, RightModified, LeftKind, RightKind,
        LeftMethod, RightMethod, ColumnCount
    };
    enum Role { DifferenceRole = Qt::UserRole + 1, EvidenceRole, SortRole };
    explicit ArchiveEntryModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    void setComparison(const Archive::Comparison &comparison);

private:
    Archive::Comparison m_comparison;
};

class ArchiveCompareView : public QWidget
{
    Q_OBJECT
public:
    explicit ArchiveCompareView(QWidget *parent = nullptr);
    void setPaths(const QString &left, const QString &right);
    void setComparison(const Archive::Comparison &comparison, bool available = true);
    void setStatus(const QString &text);

public slots:
    void previousDifference();
    void nextDifference();

signals:
    void compareRequested(const QString &left, const QString &right);
    void reloadRequested();

private:
    void refreshVisibleRows();
    void navigateDifference(bool forward);
    void updateEvidence();

    QLineEdit *m_left;
    QLineEdit *m_right;
    QLineEdit *m_search;
    QCheckBox *m_differencesOnly;
    QLabel *m_summary;
    QLabel *m_status;
    QLabel *m_evidence;
    QPushButton *m_previous;
    QPushButton *m_next;
    QPushButton *m_reload;
    ArchiveEntryModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    QVector<int> m_visibleDifferences;
};

} // namespace LqCompare

#endif
