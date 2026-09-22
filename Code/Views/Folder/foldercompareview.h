#ifndef LQCOMPARE_FOLDERCOMPAREVIEW_H
#define LQCOMPARE_FOLDERCOMPAREVIEW_H

#include "foldercompare.h"

#include <QWidget>
#include <QModelIndex>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
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
    // 「为什么是这个状态」的正文（DIR-011 第 4 条）：按「准则 / 覆盖策略 /
    // 最终结论」分组的逐行说明。做成公开查询而不是只藏在弹出框里，是为了
    // 让「各准则与最终结论都被列出来了」这件事可以被断言，而不是靠眼睛看。
    QString statusExplanation(const QModelIndex &index) const;
    // 右键菜单的构造。同样公开：测试要在不 exec() 的前提下检查菜单内容。
    QMenu *createStatusMenu(const QModelIndex &index);

public slots:
    void nextDifference();
    void previousDifference();
    void firstDifference();
    void lastDifference();
    void selectAllDifferences();
    void resetDisplayFilters();
    // 显示筛选。`-1`（全部条目）与 `-2`（差异与未确认）是本类的哨兵值；
    // 其余只接受主状态表里的取值，越界值退到「全部条目」。
    void setStatusFilter(int status);

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
    const Folder::Entry *entryForIndex(const QModelIndex &index) const;
    void showStatusReason(const QModelIndex &index);

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
    // 与 m_maximumDepth 同样的处理：视图目前没有对应的控件，但要**原样保留**
    // 「只比较前 N 字节」的值。若 options() 不把它带回来，任何一次
    // 「读视图选项 → 写回会话」都会把用户设好的预算静默重置成 0（关闭）。
    qint64 m_compareFirstBytes = 0;
};

} // namespace LqCompare

#endif
