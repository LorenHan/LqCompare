#ifndef LQCOMPARE_FOLDERCOMPAREVIEW_H
#define LQCOMPARE_FOLDERCOMPAREVIEW_H

#include "foldercompare.h"

#include <QWidget>
#include <QModelIndex>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QTreeView;

namespace LqCompare {

class FolderTreeModel;
class FolderFilterModel;

class FolderCompareView : public QWidget
{
    Q_OBJECT
public:
    explicit FolderCompareView(QWidget *parent = nullptr);
    void setPaths(const QString &leftPath, const QString &rightPath);
    void setResult(const Folder::Result &result);
    void setScanning(bool scanning);
    void setStatus(const QString &status);
    Folder::Options options() const;
    void setOptions(const Folder::Options &options);
    QTreeView *leftTree() const { return m_leftTree; }
    QTreeView *rightTree() const { return m_rightTree; }
    int visibleDifferenceCount() const;

public slots:
    void nextDifference();
    void previousDifference();
    void firstDifference();
    void lastDifference();
    void selectAllDifferences();
    void resetDisplayFilters();

signals:
    void compareRequested(const QString &leftPath, const QString &rightPath);
    void cancelRequested();
    void compareFilesRequested(const QString &leftPath, const QString &rightPath);
    void navigationStatus(const QString &status);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void activate(const QModelIndex &index);
    void updateCount();
    void navigateDifference(int direction, bool fromEdge = false);
    QVector<QModelIndex> differenceIndexes() const;
    void reveal(const QModelIndex &index);
    QTreeView *activeTree() const;

    QLineEdit *m_leftPath;
    QLineEdit *m_rightPath;
    QCheckBox *m_recursive;
    QCheckBox *m_content;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_hideExcluded;
    QCheckBox *m_hideEmpty;
    QPlainTextEdit *m_scanMask;
    QLabel *m_maskError;
    QComboBox *m_filter;
    QPushButton *m_cancel;
    QLabel *m_status;
    QLabel *m_count = nullptr;
    QTreeView *m_leftTree;
    QTreeView *m_rightTree;
    FolderTreeModel *m_model;
    FolderFilterModel *m_filterModel;
    bool m_hasResult = false;
    bool m_rightActive = false;
    int m_maximumDepth = 128;
};

} // namespace LqCompare

#endif
