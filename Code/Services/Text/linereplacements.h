#ifndef LQCOMPARE_LINEREPLACEMENTS_H
#define LQCOMPARE_LINEREPLACEMENTS_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare { namespace Text {

///
/// \brief 内置替换规则（PRD: TXT-012）。
///
/// 「替换规则」与「忽略大小写 / 忽略空白」不是同一层的两件事，**不要混在一起**：
/// 前者改写**内容**（把命中的一段换成占位符），后者放宽**判等**（文本还是原来那份，
/// 只是比较时换了一把尺子）。区分的现实意义是：替换规则会改变差异视图里显示的那一行，
/// 空白与大小写开关不会。也正因为如此，替换规则默认**一条都不开**——出厂就开着
/// 等于替用户做了内容层面的决定。
///
/// 枚举取值落在表外时的处置与 `Whitespace` 那处**刻意一致**：不兜底、不改写，
/// 当作「这条规则不存在」跳过。退化的方向因此只会**多**报差异（规则少了，能忽略的
/// 差异自然少了），绝不会把差异藏掉。反过来，`Alignment` 那处的兜底是必须的，
/// 因为一个「什么都不跑」的对齐算法会返回零个块，也就是「两份文件完全一样」——
/// 那份输入会静默地骗人。
///
enum class ReplacementRule
{
    /// 行首编号：`1. ` / `2) ` / `12: `。
    ///
    /// **要求分隔符与分隔符后的空白**是刻意的，不是写漏了：只要「行首是数字」就吞掉
    /// 会把 `42 apples` 这种正文当成编号（那份文件里的每个数量词都会消失），
    /// 而这类误伤在差异视图里表现为「一堆本该不同的行变成了相同」——最难被发现的一种错。
    /// 代价是 `1 引言` 这种没有分隔符的编号不吃，这个取舍写在 `Tests/TextRules`
    /// 的 `leadingNumberRequiresSeparatorAndSpace()` 里。
    LeadingNumber,
    /// 日期与日期时间：`2024-01-01` / `2024/1/1` / `2024-01-01T10:20:30Z`。
    DateTime,
    /// GUID / UUID：8-4-4-4-12，允许 `{...}` 包裹，大小写都认。
    Guid,
    /// 十六进制地址：`0x7FFE0000`，**要求 `0x` / `0X` 前缀**。
    ///
    /// 不接受裸十六进制串（`DEADBEEF`）：`add` / `beef` / `face` / `cafe` / `fade`
    /// 都是普通英文单词，裸串规则会在散文中大开杀戒。
    HexAddress,
};

///
/// \brief 替换规则表的一行。
///
/// 与 `AlignmentDescriptor` / `WhitespaceDescriptor` 同一个定位：**表是唯一的事实来源**。
/// `availableReplacementRules()` 的集合、`ReplacementSet` 的顺序、以及
/// `validateReplacementRuleTable()` 的判定全部从它推导。
///
/// `pattern` 同时承担两个角色，这是刻意的：它既是引擎编译的正则，**也是**界面上
/// 给用户看的那一串（完成标准第 2 条要求「显示其正则或匹配说明」）。
/// 分开写一份「给人看的说明」就会立刻出现第二份事实来源——改了规则忘了改说明，
/// 界面上仍然写着旧的行为，而没有任何测试会红。
///
struct ReplacementRuleDescriptor
{
    ReplacementRule rule = ReplacementRule::LeadingNumber;
    /// 机器可读标识（日志、快照、将来的设置键都用它，不靠枚举序号）。
    const char *identifier = "";
    /// 命中片段替换成什么。用**固定占位符**而不是空串，理由见 `linereplacements.cpp`
    /// 顶部那段「为什么是占位符而不是删除」。
    const char *placeholder = "";
    /// 规则的正则，UTF-8。既用于匹配也用于显示。
    const char *pattern = "";
    /// 一句话说明（UTF-8），与 `pattern` 配套给界面显示。
    const char *description = "";
    /// 引擎是否真的会算它。`false` 的条目不会出现在 `availableReplacementRules()` 里。
    bool implemented = false;
};

/// 规则表本身（顺序**即**声明顺序，也是 `ReplacementSet::apply()` 的应用顺序）。
const QVector<ReplacementRuleDescriptor> &replacementRuleTable();

/// 表里查这一行；查不到返回 `nullptr`，**不编一个默认行**。
const ReplacementRuleDescriptor *replacementRuleDescriptor(ReplacementRule rule);

/// 机器可读标识。查不到时返回空指针，**不编一个假名字**——编出来的标识符只会让
/// 日志里的人去搜一个不存在的符号。
const char *replacementRuleIdentifier(ReplacementRule rule);

/// 给人看的一句话说明。查不到时返回空串。
QString replacementRuleDescription(ReplacementRule rule);

/// 该取值在**给定表**里是否存在且已实现（表当参数传，便于测试拿坏表跑同一判定）。
bool hasImplementedReplacementRule(const QVector<ReplacementRuleDescriptor> &table,
                                   ReplacementRule rule);

/// 可选规则：只含 `implemented == true` 的条目，顺序与表一致。
QVector<ReplacementRule> availableReplacementRules(
    const QVector<ReplacementRuleDescriptor> &table = replacementRuleTable());

///
/// \brief 默认启用的规则集：**空**。
///
/// 这个函数存在只是为了把这个决定写成一个可断言的接口，而不是散在注释里：
/// `Whitespace` / `Alignment` 都有「表里第一条已实现」的默认值，替换规则**没有**。
/// 理由是它的作用对象不同——前两者只影响「怎么比」，本条的规则会改写用户在差异视图里
/// 看到的那一行；出厂就开着等于替用户做了内容层面的决定，而用户第一次打开两个文件时
/// 根本不知道有过这个决定。
///
QVector<ReplacementRule> defaultReplacementRules();

///
/// \brief 规则表的自检。
///
/// 查「手写这张表时容易写错、写错了也不影响别的」的几件事：漏登记规格点名的规则、
/// 两条规则共用一个标识符、已实现的条目正则编译不过、占位符为空（那会变成「删除」
/// 而不是「替换」，见 cpp 顶部的取舍）。
///
/// `expected` 是规格点名的四条规则，也是唯一**不依赖这张表自身**的期望值——
/// 表里漏登记一条时，别的规则一条都不会响。
///
QStringList validateReplacementRuleTable(const QVector<ReplacementRuleDescriptor> &table,
                                         const QVector<ReplacementRule> &expected = {
                                             ReplacementRule::LeadingNumber,
                                             ReplacementRule::DateTime,
                                             ReplacementRule::Guid,
                                             ReplacementRule::HexAddress});

///
/// \brief 按**给定顺序**逐条应用规则（自由函数）。
///
/// 单独放出来的用途有两条，缺了哪条都不行：
///
/// 1. **顺序是可测的**。`ReplacementSet::apply()` 是「取启用集合 → 按表顺序调本函数」，
///    而顺序对不对，只有能自己指定一个顺序、再拿它跟表顺序对比才验得了
///    （完成标准第 4 条要的就是这件事）。没有这个入口，测试只能间接地猜顺序。
/// 2. 表外的取值与未实现的条目在这里**跳过**，与 `normalizedLine()` 对未知枚举取值的
///    处置保持一致。
///
/// 顺序**确实**会改变结果，这不是理论上的担心：见 `Tests/TextRules` 里那条
/// 「两条规则都想吃同一段文本」的语料。
///
QString applyReplacementRules(const QString &line, const QVector<ReplacementRule> &rules);

///
/// \brief 「哪些替换规则开着」的集合。
///
/// 内部按**规则表顺序**存一个与表等长的开关序列，而不是存一个「用户勾选的顺序」：
/// 完成标准第 3 条要求「多个规则按声明顺序依次应用」，若把勾选顺序当成应用顺序，
/// 那么同样的三条规则会因为用户点选的先后而给出不同结果——同一份配置在两台机器上
/// 结论不同，且没有任何东西会报错。
///
class ReplacementSet
{
public:
    ReplacementSet();

    /// 从一串规则构造：重复的取值只算一次。
    static ReplacementSet fromRules(const QVector<ReplacementRule> &rules);

    /// 表外的取值一律视为未启用。
    bool isEnabled(ReplacementRule rule) const;

    /// 表外的取值与不存在的下标一律**忽略**（不改集合，也不报错）。
    void setEnabled(ReplacementRule rule, bool enabled);

    /// 一条都没开。`apply()` 据此短路。
    bool isEmpty() const;

    /// 已启用的规则，**按表顺序**（不是勾选顺序）。
    QVector<ReplacementRule> enabledRules() const;

    /// 把整条规则链作用到一行文本上。空集合时**原样返回**，连正则都不编译。
    QString apply(const QString &line) const;

private:
    // 与 `replacementRuleTable()` 等长，下标即表下标。
    QVector<bool> m_enabled;
};

} }
#endif
