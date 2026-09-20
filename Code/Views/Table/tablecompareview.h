#ifndef LQCOMPARE_TABLECOMPAREVIEW_H
#define LQCOMPARE_TABLECOMPAREVIEW_H

#include <QAbstractTableModel>
#include <QWidget>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;

namespace LqCompare {
class TableCompareSession;

class TableGridModel : public QAbstractTableModel {
    Q_OBJECT
public:
    TableGridModel(TableCompareSession *session, bool left, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void refresh(bool differencesOnly);
    int visibleRow(int resultRow) const;
    int resultRow(int visibleRow) const;

private:
    TableCompareSession *m_session;
    bool m_left;
    QVector<int> m_rows;
};

class TableCompareView : public QWidget {
    Q_OBJECT
public:
    explicit TableCompareView(TableCompareSession *session, QWidget *parent = nullptr);

private:
    void refresh();
    void choosePath(bool left);
    void loadPaths();
    void applyFormats();
    void editColumns();
    void showCell(bool left, const QModelIndex &index);
    void selectDifference(int index);
    void showError(const QString &error);
    TableCompareSession *m_session;
    QLineEdit *m_paths[2];
    QComboBox *m_delimiters[2];
    QComboBox *m_encodings[2];
    QCheckBox *m_headers[2];
    QTableView *m_tables[2];
    TableGridModel *m_models[2];
    QCheckBox *m_differencesOnly;
    QCheckBox *m_differenceColumns;
    QComboBox *m_alignment;
    QComboBox *m_mapping;
    QLabel *m_summary;
    QLabel *m_warnings;
    QLabel *m_error;
};
}
#endif
