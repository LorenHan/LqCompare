#ifndef LQCOMPARE_RIBBONLAYOUT_H
#define LQCOMPARE_RIBBONLAYOUT_H

#include <QStringList>

namespace LqRibbon {
class RibbonBar;
}

namespace LqCompare {

///
/// \brief The RibbonLayout class
/// 按声明表构建 LqCompare 的全部 Ribbon 页面（PRD: UI-007 ~ UI-018）。
///
/// 设计约束：
/// - 界面结构（页面 / 分组 / 按钮）与命令实现解耦。声明表只写「哪个位置放哪条命令」，
///   命令的行为由 CommandRegistry 提供（UI-024）。因此新增一个按钮不需要改本文件的逻辑。
/// - 声明表里带有 ACTION-ID，界面上未实现的按钮会明确告诉用户该功能对应哪条规格条目，
///   便于逐条推进（这也是本仓库「issue 即规格书」的落地方式）。
/// - 尚未注册或尚未实现的命令生成禁用的占位按钮，tooltip 明确说明原因及
///   ACTION-ID。运行期注册命令或更新状态后，所有入口由绑定器同步刷新。
///
class RibbonLayout
{
public:
    /// 构建全部页面。返回本次创建的按钮总数，用于启动自检。
    static int build(LqRibbon::RibbonBar *bar);

    /// 页面数量（10：Home / Compare / Merge / Edit / View / Filter / Session / Report / Tools / Help）。
    static int pageCount();

    /// 分组数量（44）。
    static int groupCount();

    /// 按钮数量。
    static int buttonCount();
};

} // namespace LqCompare

#endif // LQCOMPARE_RIBBONLAYOUT_H
