#ifndef LQCOMPARE_FOLDERCOMPAREVIEW_H
#define LQCOMPARE_FOLDERCOMPAREVIEW_H

#include "foldercompare.h"
#include "recursionstrategy.h"
#include "statuspalette.h"

#include <QIcon>
#include <QWidget>
#include <QModelIndex>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTreeView;

namespace LqCompare {

class FolderTreeModel;
class FolderFilterModel;

// 状态图标（DIR-012 第 1 条后半句：图标随主题切换自动适配深浅）。
//
// 资源里的九张状态图是**中性灰描边**（DIR-011 刻意的取舍：不同形状而不是不同颜色，
// 这样灰度打印与色觉障碍下信息不丢）。中性灰在深色主题上几乎看不见，
// 因此配色方案切换时必须连图标一起换——做法是用方案里该状态的颜色给图标**着色**
// （`CompositionMode_SourceIn` 只改 RGB、保留 alpha，于是描边的抗锯齿边缘不受影响）。
//
// 做成自由函数而不是模型私有实现，是为了让「深色主题下图标真的变亮了」这件事
// 可以被断言：只断言 `DecorationRole` 非空的话，一个永远返回原图的实现照样绿，
// 而它的现象是「深色主题里状态列一片空白」。
QIcon themedStatusIcon(Folder::Status status, const Folder::ColorScheme &scheme, bool dark);

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
    // 当前档位（DIR-003 第 1 条）。公开出来是为了让「控件选中的」与
    // 「实际生效的」这**两条链**都能被断言——只钉其中一条时，
    // 「把下拉铺法改成倒序」这种变异会全绿（handoff §6 里记过这条）。
    Folder::RecursionTier recursionTier() const;
    // 当前**实际生效**的配色方案标识符（DIR-012 第 2 条）。
    //
    // 与 `recursionTier()` 同一条纪律：让「下拉选中的」与「模型实际在用的」
    // 两条链都能被断言。只钉其中一条时，「切换只改了控件没改模型」
    // 或者反过来，都全绿，而它们在界面上表现为「选了没反应」。
    QString colorSchemeId() const;
    // 第 5 条：导出 / 导入配色文件。
    //
    // 抽成**不带对话框**的方法，好让「写出去的字节对不对、读回来的方案是不是那份」
    // 可以被断言；界面上的两个菜单项只是给它们各加一次 `QFileDialog`。
    // 导入失败时 `problems` 里是解析器给出的原因，方案与界面都**保持不变**。
    bool exportColorScheme(const QString &path, QStringList *problems = nullptr) const;
    bool importColorScheme(const QString &path, QStringList *problems = nullptr);
    // 「配色…」下拉按钮的菜单。公开的理由与 `createStatusMenu()` 相同：
    // 测试要在不 exec() 的前提下检查菜单内容。
    // **不要**在测试里 trigger 这两个菜单项——它们会开原生文件对话框，
    // 离屏环境下会一直等下去。被 trigger 的是下面那两个不带对话框的方法。
    QMenu *createColorSchemeMenu();
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
    // 切换配色方案（DIR-012 第 2 条）。
    //
    // **刻意不发出 `rescanRequested`**：配色只影响画出来的样子，不影响
    // 结果集。第 4 条「切换立即重绘列表，不需要重新扫描」的可执行形式就是
    // 「不发这个信号 + 模型逐行发 dataChanged」——少了前一半，一次换配色
    // 会把整棵目录树重扫一遍（大目录上要等好几秒），而用户只是换了个颜色。
    void setColorScheme(const QString &identifier);

signals:
    void compareRequested(const QString &leftPath, const QString &rightPath);
    void cancelRequested();
    void compareFilesRequested(const QString &leftPath, const QString &rightPath);
    void navigationStatus(const QString &status);
    // 递归档位或深度上限被用户改动时发出（DIR-003 第 3 条）。
    //
    // 为什么是「重新扫描」而不是「对已扫描结果重新过滤」：这两个控件改的是
    // **扫描范围**，不在结果里的条目根本没有行可以过滤。带路径的
    // `compareRequested` 不适合这里——会话才是路径的唯一来源，
    // 视图里还没提交的路径编辑不该因为这个信号被当成来源（见
    // `unsubmittedPathEditsDoNotChangeSessionSource`）。
    void rescanRequested();
    // 配色方案换了（DIR-012 第 2 条）。**不带路径也不带"要不要重扫"**：
    // 换配色不需要重扫，「不重扫」这件事由本信号里没有重扫语义来表达。
    // 容器可用它把选择写进会话设置（设置项本身属 View 页 OPT-*）。
    void colorSchemeChanged(const QString &identifier);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void activate(const QModelIndex &index);
    void updateCount();
    // 把档位写进两个控件，并决定深度控件是否可改。
    // `fromUser` 为真时（用户真的选了档位）才把档位确定的深度写回控件；
    // 为假时（恢复存档 / 深度控件自己把档位带下来）**一个值都不动**——
    // 「不递归 + 深度 7」这种存档必须原样活过一次往返。
    void applyTierToControls(Folder::RecursionTier tier);
    void navigateDifference(int direction, bool fromEdge = false);
    QVector<QModelIndex> differenceIndexes() const;
    void reveal(const QModelIndex &index);
    QTreeView *activeTree() const;
    const Folder::Entry *entryForIndex(const QModelIndex &index) const;
    void showStatusReason(const QModelIndex &index);
    // 标识符 → 方案。出厂三套与「认不出来回落默认」都在服务层
    // （`colorSchemeByIdentifier`），视图**不重写一遍**；这里多出来的只有
    // 「本次会话导入的自定义配色」这一支——它不在出厂表里。
    Folder::ColorScheme schemeForIdentifier(const QString &identifier) const;
    void rebuildColorSchemeItems();
    void promptExportColorScheme();
    void promptImportColorScheme();

    QLineEdit *m_leftPath;
    QLineEdit *m_rightPath;
    // 递归子目录策略（DIR-003）：档位下拉 + 深度上限。
    // 两者是**两个字段的两个写入者**，不是同一件事的两份说法：
    // `m_recursionTier` 决定 `Options::recursive`，`m_maximumDepth` 决定
    // `Options::maximumDepth`。档位只在**用户换档**时才把档位确定的深度写进
    // 后者，其余时刻原样保留用户设好的值。
    QComboBox *m_recursionTier;
    QSpinBox *m_maximumDepth;
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
    // 与 m_recursionTier / m_maximumDepth 同样的处理：视图目前没有对应的控件，
    // 但要**原样保留**「只比较前 N 字节」的值。若 options() 不把它带回来，
    // 任何一次「读视图选项 → 写回会话」都会把用户设好的预算静默重置成 0（关闭）。
    qint64 m_compareFirstBytes = 0;
    // 配色方案（DIR-012）。下拉里铺的是**出厂表 + 本次会话导入的那一套**，
    // 而「实际生效的方案」只在模型里有一份存储——视图不另存一份标识符，
    // 那是同一件事的第二份说法（改成保存标识符的话，界面切了颜色而模型没切
    // 时会「看起来成功了」）。
    QComboBox *m_colorScheme = nullptr;
    // 导入的自定义配色**只活在本次会话**：它不进 `colorSchemeTable()`
    // （那是出厂常量），也没有对应的设置键——把用户导入的配色持久化成
    // 第四个设置项是一次独立的产品决定，属 View 页（OPT-*）。
    Folder::ColorScheme m_customColorScheme;
    bool m_hasCustomColorScheme = false;
};

} // namespace LqCompare

#endif
