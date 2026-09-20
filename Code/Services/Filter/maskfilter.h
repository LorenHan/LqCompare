#ifndef LQCOMPARE_MASKFILTER_H
#define LQCOMPARE_MASKFILTER_H

#include "mask.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Filter {

///
/// \brief 过滤声明：一组「包含 / 排除」掩码，以及它们叠加的结论（PRD: FILT-001）。
///
/// 为什么这一层要和 mask.h 分开
/// --------------------------------
/// `mask.h` 回答的是「这一段掩码命中这个条目吗」，答案只有是/否；
/// 而用户在界面上写的是一个**声明**——多行掩码，`-` 开头的是排除、`#` 开头的是
/// 注释，合起来决定一个条目最终是被保留、被隐藏，还是「本来就没打算保留它」。
///
/// 这两层混在一起会让「一条掩码的语法对不对」与「整个过滤器怎么叠加」两类错误
/// 出现在同一个返回值里，界面上就没法分别显示（前者要在掩码那一段标红，
/// 后者要在状态栏说明「排除优先」）。所以合成有两层：语法在 mask.h，
/// 叠加在这里。
///

/// 一条规则是包含还是排除。
enum class MaskRuleKind
{
    Include,
    Exclude,
};

/// 机器可读标识（写日志、测试失败信息用）。
const char *maskRuleKindIdentifier(MaskRuleKind kind);

/// 界面用的中文标签。
QString maskRuleKindLabel(MaskRuleKind kind);

///
/// \brief 一条已解析成功的规则。
struct MaskRule
{
    MaskRuleKind kind = MaskRuleKind::Include;
    Mask mask;
    QString text;    ///< 归一后的掩码文本（不含 `-` 前缀与首尾空白），便于界面回显
    int line = -1;   ///< 在声明文本里的行号（0 起）
    int column = -1; ///< 掩码在该行里的起始列（0 起），便于界面定位

    /// 「第 2 行：排除 `*.tmp`」
    QString describe() const;
};

///
/// \brief 一个条目被过滤之后的结论。
///
/// 刻意把「被排除」与「未命中」（不在任何包含掩码里）分成两个取值，而不是合成
/// 一个「不可见」：两者给用户的解释完全不同——前者是「你明确要求不要它」，
/// 后者是「你的包含掩码里没有它」。FILT-006 的批量操作安全提示、FILT-011 的
/// 「我为什么看不到这个文件」都要靠这个区分。合成一个之后，这两个功能只能靠
/// 重新跑一遍匹配去猜。
///
enum class MaskVerdict
{
    Included,   ///< 保留（可见）
    Excluded,   ///< 被排除掩码命中
    NotMatched, ///< 未被任何包含掩码命中
};

const char *maskVerdictIdentifier(MaskVerdict verdict);
QString maskVerdictLabel(MaskVerdict verdict);

///
/// \brief 对一个条目的判定结果，含「是哪条规则决定的」。
struct MaskDecision
{
    MaskVerdict verdict = MaskVerdict::NotMatched;

    /// 起决定作用的规则下标（对应 `MaskFilter::rules()`）；-1 表示没有规则参与
    /// （即「一条包含规则都没有，于是默认保留」）。
    int ruleIndex = -1;

    MaskRuleKind ruleKind = MaskRuleKind::Include;
    QString ruleText;

    /// 一句话说清这个结论是怎么来的（状态栏与诊断面板用）。
    QString describe() const;
};

///
/// \brief 声明里某一行的语法错误。
///
/// 位置按**整行**给出（不是相对掩码），界面可以直接把 `column` / `length`
/// 喂给输入框的选中范围。`line` 也是整段声明的行号。
///
struct MaskRuleError
{
    int line = -1;
    int column = -1;
    int length = 0;
    QString message;
    QString hint;

    /// 「第 2 行第 3 列：字符集没有闭合的 `]`（补上 `]`…）」
    QString describe() const;
};

// 结果结构体按值持有 MaskFilter，因此要先于它声明、后于它定义。
struct MaskFilterParseResult;

///
/// \brief 一组叠加起来的包含 / 排除掩码。
///
/// 叠加规则只有两条，而这两条都是有意的选择（见 maskfilter.cpp 里的注释）：
///   1. **排除优先**：只要有排除规则命中，结论就是排除，与包含规则无关。
///   2. **一条包含规则都没有时，默认全部保留**：只填了排除框是正常用法。
///
class MaskFilter
{
public:
    MaskFilter();

    ///
    /// \brief 解析一段过滤声明。
    ///
    /// 声明语法（一行一条）：
    ///   * 空行、只有空白字符的行 —— 忽略
    ///   * 第一个非空白字符是 `#` —— 整行注释，忽略
    ///   * 其余情况：一个掩码；`-` 开头表示排除
    ///
    /// 某一行的掩码写错时，**只有那一行被丢弃**，其余行照常生效，错误收在
    /// `errors` 里。这样界面上可以一边显示「这一行有问题」一边让其他规则继续
    /// 工作，而不是整段失效——用户改到一半时整段失效会让人以为是自己把
    /// 别的地方敲坏了。
    ///
    /// `platform` 只影响**默认**的大小写敏感性，可以用 setCaseSensitivity 覆盖。
    ///
    static MaskFilterParseResult parse(const QString &declaration,
                                       MaskPlatform platform = currentMaskPlatform());

    const QVector<MaskRule> &rules() const { return m_rules; }
    int ruleCount() const { return m_rules.size(); }
    int includeCount() const;
    int excludeCount() const;

    /// 一条规则都没有（等价于「什么都不过滤」）。
    bool isEmpty() const { return m_rules.isEmpty(); }

    MaskPlatform platform() const { return m_platform; }

    Qt::CaseSensitivity caseSensitivity() const { return m_case; }

    /// 大小写策略是否被显式设置过（界面据此显示「已覆盖平台默认」）。
    bool isCaseSensitivityOverridden() const { return m_caseOverridden; }

    void setCaseSensitivity(Qt::CaseSensitivity cs);
    void clearCaseSensitivityOverride();

    /// 对单个条目下结论。
    ///
    /// 传入一个**不合法**的条目（名字为空，见 `MaskSubject::isValid()`）时，结论
    /// 是 `NotMatched` 而不是 `Included`——即当作不可见。方向是刻意选的：调用方
    /// 给了残缺的条目，保守的一侧是把它挡在外面；放它通过过滤器的话，一个本该
    /// 报错的输入会变成「界面上少了一个条目」这种无声无息的结果。
    MaskDecision decide(const MaskSubject &subject) const;

    /// 是否保留（`decide` 的便捷写法，界面绝大多数地方只关心这一个）。
    bool accepts(const MaskSubject &subject) const;

    /// 该条目被**哪些**规则命中（按声明顺序的下标）。
    ///
    /// decide() 只报「起决定作用的那一条」，而诊断面板要回答的是
    /// 「我为什么看不到这个文件，又是哪几条在管它」——两者不是一回事：
    /// 一个条目可能同时命中两条排除规则，只报第一条会让用户改掉一条之后
    /// 发现还是看不见。
    QVector<int> matchingRuleIndexes(const MaskSubject &subject) const;

    /// 「包含 2 条、排除 1 条，大小写敏感（平台默认：posix）」
    QString describe() const;

private:
    QVector<MaskRule> m_rules;
    MaskPlatform m_platform = MaskPlatform::Posix;
    bool m_caseOverridden = false;
    Qt::CaseSensitivity m_case = Qt::CaseSensitive;
};

///
/// \brief 解析结果：算出来的过滤器 + 逐行的错误。
///
/// 两者同时返回而不是「有错就整体失败」：见 MaskFilter::parse 的说明。
///
struct MaskFilterParseResult
{
    MaskFilter filter;
    QVector<MaskRuleError> errors;

    bool ok() const { return errors.isEmpty(); }

    /// 把所有错误拼成多行文本（界面的一次性提示用）。
    QString describeErrors() const;
};

///
/// \brief 预览结果：输入一批条目时的计数（FILT-001 第 4 条完成标准）。
struct MaskFilterPreview
{
    int total = 0;
    int included = 0;
    int excluded = 0;
    int notMatched = 0;

    /// 按「起决定作用的那条规则」统计命中次数，与 `MaskFilter::rules()` 同序。
    /// 用于「这条规则实际影响了多少条目」。
    QVector<int> hitsByRule;

    /// 不可见的条目数（被排除的 + 未命中的）。
    int hidden() const { return total - included; }

    /// 「匹配 N 项 / 共 M 项」——规格第 4 条要求的文案。
    QString summary() const;
};

/// 统计一批条目在过滤器下的分布。
MaskFilterPreview preview(const MaskFilter &filter, const QVector<MaskSubject> &subjects);

/// 便捷重载：只有名字（没有目录信息），等价于逐个走 MaskSubject::forName。
MaskFilterPreview previewNames(const MaskFilter &filter, const QStringList &names);

} // namespace Filter
} // namespace LqCompare

#endif // LQCOMPARE_MASKFILTER_H
