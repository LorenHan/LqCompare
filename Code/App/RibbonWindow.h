#ifndef LQCOMPARE_RIBBONWINDOW_H
#define LQCOMPARE_RIBBONWINDOW_H

#include "LqRibbon.h"

namespace LqCompare {

///
/// \brief The RibbonWindow class
/// 应用级 Ribbon 外壳（PRD: UI-001 ~ UI-006）。
///
/// 与 Ailecium 的 RibbonWindow 保持同一套做法，便于两个项目共享经验：
/// Office 2016 Blue 样式、居中命令搜索栏、可最小化、屏蔽 LqRibbon 默认上下文菜单，
/// 并把「添加到快速访问工具栏 / 自定义功能区」两项作为自有右键入口。
///
class RibbonWindow : public LqRibbon::RibbonMainWindow
{
    Q_OBJECT

public:
    explicit RibbonWindow(QWidget *parent = nullptr);

protected slots:
    /// 搜索栏请求帮助：把关键字交给命令注册中心检索并执行（UI-004）。
    void showHelp(const QString &text);

    /// Ribbon 上下文菜单（UI-006）。
    void showRibbonContextMenu(QMenu *menu, QContextMenuEvent *event);

    /// 语言切换后刷新 Ribbon 自有控件的文案（UI-030）。
    void switchLanguage();

protected:
    void setupQuickAccessBar();

private:
    void setupSearchBar();

    Q_DISABLE_COPY(RibbonWindow)
};

} // namespace LqCompare

#endif // LQCOMPARE_RIBBONWINDOW_H
