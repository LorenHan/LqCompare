#ifndef LQCOMPARE_CONTENTFILTER_H
#define LQCOMPARE_CONTENTFILTER_H

#include "mask.h"
#include "maskfilter.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Filter {

///
/// \brief 内容过滤器：行过滤与关键字节（PRD: FILT-004）。
///
/// ## 它在这套过滤器里的位置
///
/// 本仓的过滤分成四条**正交**的轴，各自回答一个问题：
///
/// | 模块 | 看什么 | 代价 |
/// | --- | --- | --- |
/// | `maskfilter.h` | 名字 / 相对路径 | 字符串匹配 |
/// | `namefilter.h` | 名字（高级表达式） | 正则可退化 |
/// | `attributefilter.h` | 元数据（大小 / 时间 / 属性位 / 所有者） | 一次 `stat` |
/// | **本文件** | **文件内容** | **把整个文件读进来** |
///
/// 前三条都能在「只看名字与目录项」的阶段出结论，本文件不行。这正是规格边界条款
/// 写「必须显式启用且给出性能提示」的原因，也是本模块把「启用」做成一等状态
/// （而不是「配了规则就等于启用」）的原因：一份别人分享过来的会话文件里带着
/// 上百条行过滤规则时，用户不该因为「我什么都没点」就被拖进逐文件读取。
///
/// ## 两条与既有模块同源的纪律
///
/// 1. **写错的一行只丢那一行，其余照常生效**（`MaskFilter::parse` /
///    `AttributeFilter::parseDeclaration` / `NameFilter::parse` 都是这样）。
///    用户改到一半时整份声明失效，他会以为是自己把别的地方敲坏了。
/// 2. **说不清楚的结论一律放行，且必须被报出来**。内容过滤这里对应的是
///    「二进制里一字节都不含关键序列 → 不纳入比较」——听起来像「不满足即排除」，
///    但**空规则集**必须解释成「没有约束」而不是「谁都不含」：把「没有配置」
///    当成「全部排除」，用户的目录会直接变成空的，而界面上只多了一行没人在意的提示。
///
/// ## 第 3 条：为什么「先过滤行、再应用忽略规则」不是一句废话
///
/// 规格第 3 条要求「内容过滤与忽略规则的作用顺序明确（先过滤行再应用忽略规则）
/// 且有测试」。它要防的是一个很自然、但会悄悄改变结果的实现选择：
/// **把行过滤接在「按忽略规则规范化之后」的文本上**。
///
/// 一旦那样做，「哪些行参与比较」就取决于会话里开了哪些忽略规则：
/// 打开「忽略行尾空白」之后，只写了空格的 `"   "` 行会先被规范化成 `""`，
/// 于是 `re:^$` 开始命中它——用户改的是**忽略规则**，被改变的却是**参与比较的行集**。
/// 反方向同样成立：`re:^$` 在「忽略空白」关着时命中不了 `"   "`，开着时就命中了，
/// 而两次的文件一模一样。
///
/// 因此本模块把这条顺序做成**数据**（`contentFilterStageOrder()`，
/// 见 `ContentFilterStage`）并把行过滤的输入钉死成**原始行**
/// （`LineFilter::excludes()` 与 `LineFilter::filterLines()` 都只吃原始行，
/// 且各有一条用例把这件事钉住）。`contentFilterStageOrder()` 由
/// `validateContentFilterStageTable()` 在启动/测试时核对。
///
/// ## 刻意不做的事
///
/// * **不读文件**。本模块只接受调用方已经读进来的 `QByteArray` / 行列表。
///   与 `attributefilter.h` 的「模块里没有任何读文件的代码」同一条纪律——
///   由 `Tests/ContentFilter` 的读源码护栏钉住（出现 `QFile` / `readAll` /
///   `QTextStream` / `QDataStream` 即红）。理由也一样：一处「顺手读一下」
///   会让这条约束在没有任何征兆的情况下消失。
/// * **不做编码转换**。行过滤吃的是已经解码好的 `QString` 行，
///   关键字节过滤吃的是**原始字节**。两者的输入是两样东西，混在一个入口里
///   迟早会出现「拿解码后的文本去搜字节序列」这种看起来能跑、实际永远搜不到的写法。
///

// -----------------------------------------------------------------------------
// 内容过滤的两个阶段（第 3 条完成标准）
// -----------------------------------------------------------------------------

///
/// \brief 内容过滤管线的阶段，枚举顺序**就是**作用顺序（先行过滤，后忽略规则）。
///
/// 为什么要有这张表而不是在注释里写一句「先过滤行」：那句话没有任何东西守着。
/// 表是数据，`validateContentFilterStageTable()` 能拿一份故意调换顺序的表
/// 证明它会报——于是「顺序」这件事有了一条会红的用例（本仓对「启动自检」
/// 的一贯要求，见 `filterstack.cpp` / `namefilter.cpp` 的表自检）。
///
/// **注意最后一个阶段（忽略规则）不在本模块里**。它是比对引擎
/// （`Services/Text` 的 `CompareOptions`）的职责，本模块只声明顺序、
/// 并保证自己这一段的输入是**原始行**。把忽略规则搬进本模块是错的：
/// 它不是过滤，而是「差异怎么判定」。
///
enum class ContentFilterStage
{
    LineFilter,   ///< 行过滤：把匹配模式的行从比较输入里去掉（本模块）
    IgnoreRules,  ///< 忽略规则：比对时忽略空白 / 大小写 / 行尾（比对引擎）
};

/// 机器可读标识（写日志、测试失败信息、设置键都用它，不靠枚举序号）。
const char *contentFilterStageIdentifier(ContentFilterStage stage);

/// 界面用的中文标签，如「行过滤」。
QString contentFilterStageLabel(ContentFilterStage stage);

/// 一句话说明（面板行的 tooltip）。
QString contentFilterStageDescription(ContentFilterStage stage);

/// 阶段表的一行。
///
/// 为什么把标签与说明也放进**可传参**的表里（而不是留在 .cpp 的私有结构里）：
/// `validateContentFilterStageTable()` 要能被「一份故意写坏的表」验证它真会报，
/// 而如果表行结构不公开，自检就只能去读模块内部的常量——那正是本仓已经踩过的
/// 那条坑（见 §6：自检从内部表取数据 → 一条永远不会红的护栏，变异 M27 漏检）。
/// 三张表一律「结构公开 + 自检吃参数」。
struct ContentFilterStageRow
{
    ContentFilterStage stage = ContentFilterStage::LineFilter;
    const char *identifier = nullptr; ///< 机器可读标识（`line-filter` / `ignore-rules`）
    QString label;                    ///< 界面用标签
    QString description;              ///< 一句话说明
};

/// 阶段表（**函数内静态并返回引用**：表行地址会交出去）。
const QVector<ContentFilterStageRow> &contentFilterStageTable();

/// 全部阶段，按**作用顺序**（LineFilter → IgnoreRules）。
///
/// 这个顺序同时是「数据流的顺序」与「展示顺序」，但它仍然被单独暴露出来：
/// 哪天产品决定「面板上把忽略规则排在前面」，只有展示层跟着改，
/// `contentFilterStageIndex()` 与自检盯着的作用顺序一行都不用动。
QVector<ContentFilterStage> contentFilterStageOrder();

/// 该阶段在上面那张表里的序号（0 起）。
int contentFilterStageIndex(ContentFilterStage stage);

////
/// \brief 核对一份阶段表（**表当参数**）。
///
/// 与 `validateFilterLayerTable()` / `validateNameFilterTables()` 同一条纪律：
/// 数据来源必须是**参数**。最初那两条自检从模块内部的静态表取数据，
/// 于是测试把一份故意写坏的表传进去也影响不到它——一条永远不会红的护栏
/// （见 §6 里 M27 那次漏检）。这里从一开始就按参数写。
///
/// 查四件事：
///   * 阶段是否齐全且不重复；
///   * 顺序是否是规格顺序（**行过滤在忽略规则之前**）；
///   * 每个阶段是否有标签与说明；
///   * 阶段标识符是否唯一且非空。
///
QVector<QString> validateContentFilterStageTable(const QVector<ContentFilterStageRow> &rows);


// -----------------------------------------------------------------------------
// 行过滤器：模式表（第 1 条完成标准）
// -----------------------------------------------------------------------------

///
/// \brief 一条行过滤表达式的匹配方式。
///
/// 三种模式沿用 `namefilter.h` 的**行首前缀**约定（`= ` 精确 / 无前缀 通配 /
/// `re:` 正则），因为同一份设置文件里两个过滤器挨着写，两套前缀只会让人记错。
/// 但**匹配对象不同**，这一点是刻意的：
///
///   * `NameFilter` 三种模式一律**整名**匹配——它的输入是文件名，
///     「部分匹配文件名」这个需求由用户自己写 `.*x.*` 表达。
///   * 本文件的**正则**是**子串**匹配（等价于 grep）。理由是本条目的典型用例
///     就是「把日志里带时间戳的行去掉」，用户会写 `re:^\d{4}-\d{2}-\d{2}`
///     或者 `re:INFO`。整行锚定会让 `re:INFO` 变成「整行就是 INFO」，
///     于是它永远不命中，而界面上完全看不出为什么——这属于「实现改变了
///     用户表达的意思」，本仓一贯拒绝（见 `anchoredPattern()` 那条坑记录）。
///     需要整行的用户写 `re:^INFO$` 即可，那是他明确的表达。
///   * `=` 精确与无前缀通配则**整行**匹配：`=` 本来就是「整行等于」，
///     通配走掩码语言、掩码天然是整串匹配。
///
/// `.wholeLine` 字段把这件事写进表里，`LineFilter::excludes()` 按它分流，
/// 自检核对「正则必须**不是**整行匹配」——否则一次「顺手统一一下」
/// 就会把上面那条取舍静默改掉。
///
enum class LineFilterMode
{
    Exact,    ///< 精确行：整行与该字符串全等
    Wildcard, ///< 通配行：掩码语法（mask.h），整行匹配
    Regex,    ///< 正则行：搜子串（等价于 grep），不自动加锚点
};

/// 模式表的一行。字段顺序刻意与 `NameFilter` 的模式表一致，便于对照阅读。
struct LineFilterModeRow
{
    LineFilterMode mode = LineFilterMode::Exact;
    const char *identifier = nullptr; ///< 机器可读标识（`exact` / `wildcard` / `regex`）
    QString label;                    ///< 界面用标签
    QVector<QString> prefixes;        ///< 该模式接受的行首前缀；空串成员表示「无前缀」
    QString explanation;              ///< 一句话解释（tooltip）
    bool wholeLine = false;           ///< true = 整行匹配，false = 子串匹配
};

/// 模式表（**函数内静态并返回引用**，理由见 namefilter.cpp 里那三条：返回
/// 临时 `QVector` 会让交出去的指针立刻悬垂，现象是标签函数返回空串、再往下 SIGSEGV）。
const QVector<LineFilterModeRow> &lineFilterModeTable();

/// 全部模式，按表的顺序。
QVector<LineFilterMode> allLineFilterModes();

/// 表行的查表入口（找不到返回 `nullptr`）。
const LineFilterModeRow *lineFilterModeRow(LineFilterMode mode);

/// 表是唯一的事实来源：这三个函数都从表里取。
const char *lineFilterModeIdentifier(LineFilterMode mode);
QString lineFilterModeLabel(LineFilterMode mode);
QString lineFilterModeExplanation(LineFilterMode mode);
bool lineFilterModeMatchesWholeLine(LineFilterMode mode);

/// 该模式**推荐**的行首前缀（写回声明文本时用；通配符是空串）。
QString lineFilterModePreferredPrefix(LineFilterMode mode);

/// 行首前缀的匹配结果。
struct LineFilterModePrefixHit
{
    bool found = false;
    LineFilterMode mode = LineFilterMode::Wildcard;
    int prefixLength = 0; ///< 前缀自身的字符数（不含缩进）
    /// 表达式正文的起点列（0 起）。等于「缩进 + 前缀长度」，没认到前缀时等于缩进。
    /// 界面据此把光标与标红位置落在正文上，而不是落在空白或前缀上。
    int textOffset = 0;
};

////
/// \brief 认出这一行用的是哪种模式（`= ` → 精确、`re:` → 正则、都不是 → 通配）。
///
/// **行首的空白会被跳过**：`   = EXACT` 与 `= EXACT` 是同一件事。
///
/// 这一点是本文件**刻意与 `namefilter.h` 不一致**的地方，理由值得写下来：
/// 名称过滤器的声明是一行一个文件名，用户几乎不会缩进；而内容过滤的声明是
/// 「几条规则按模式分块写」，缩进对齐是常态。更要紧的是失败的形态——
/// 前缀没被认出来时整行会退化成**通配模式**，而 `=` 在掩码语言里是普通字符，
/// 于是 `= EXACT` 变成一个「匹配字面量 `= EXACT` 的行」的规则：
/// 它看起来完全正常，却永远不命中任何一行，用户在界面上查不出原因。
/// 「缩进一下就静默失效」不值得为「与另一个过滤器逐字一致」付这个代价。
///
/// 这是「什么叫一个合法的模式前缀」的唯一实现：`analyzeLineFilterLine()` 用它，
/// 界面将来做实时校验也只用它——**不要再写第二份前缀解析**。
///
LineFilterModePrefixHit matchLineFilterModePrefix(const QString &line);

/// 核对一份模式表（**表当参数**）。查：三种模式齐全且不重复、前缀不为空且不冲突、
/// 解释不为空、`Regex` 必须是子串匹配而 `Exact`/`Wildcard` 必须是整行匹配。
QVector<QString> validateLineFilterModeTable(const QVector<LineFilterModeRow> &table);

// -----------------------------------------------------------------------------
// 行过滤器：问题清单（第 1 条完成标准）
// -----------------------------------------------------------------------------

/// 一行行过滤声明可能出现的问题种类。
enum class LineFilterIssueKind
{
    Syntax, ///< 表达式写错了（掩码语法 / 正则语法）
    Empty,  ///< 前缀后面没有内容
    Risky,  ///< 语法成立，但结构上可能灾难性回溯（只提示，不阻断）
};

const char *lineFilterIssueKindIdentifier(LineFilterIssueKind kind);
QString lineFilterIssueKindLabel(LineFilterIssueKind kind);

/// 一条问题。带「行号 + 列号 + 长度」的理由与 `NameFilterIssue` 逐字相同：
/// 界面要能在输入框里就地选中出错的那一段，只给一句话会让用户逐个字符猜。
struct LineFilterIssue
{
    LineFilterIssueKind kind = LineFilterIssueKind::Syntax;
    int line = -1;    ///< 1 起；-1 表示与位置无关
    int column = -1;  ///< 相对**整行文本**的列号（0 起，含前缀）
    int length = 0;   ///< 出错片段的长度（界面据此高亮）
    QString text;     ///< 出问题的那段文本（便于回显）
    QString message;  ///< 出了什么问题
    QString hint;     ///< 怎么改（可为空）

    /// 「第 2 行第 3 列：…」
    QString describe() const;
};

/// 一组问题。
struct LineFilterProblems
{
    QVector<LineFilterIssue> issues;

    bool ok() const { return issues.isEmpty(); }
    int countOfKind(LineFilterIssueKind kind) const;
    QString describe() const;
};

// -----------------------------------------------------------------------------
// 行过滤器：单行分析（解析、界面实时校验、复验的唯一入口）
// -----------------------------------------------------------------------------

/// 一条编译好的行过滤表达式。
struct LineFilterPattern
{
    int line = -1;                       ///< 声明文本里的行号（1 起）
    int column = -1;                     ///< 表达式内容起始列（0 起，已跳过前缀）
    LineFilterMode mode = LineFilterMode::Wildcard;
    QString source;                      ///< 原样的那一行（回显用）
    QString pattern;                     ///< 去掉前缀后的表达式正文
    bool valid = false;                  ///< 编译通过才为真
    Mask compiledMask;                   ///< 仅通配模式有意义
    QRegularExpression compiledRegex;    ///< 仅正则模式有意义

    /// 该模式是否是整行匹配（从表里取，不另存一份事实）。
    bool matchesWholeLine() const { return lineFilterModeMatchesWholeLine(mode); }

    /// 这条表达式在声明文本里该怎么写（含前缀）。
    QString toDeclarationText() const;
};

/// 「一行」的完整分析结果。
struct LineFilterLineAnalysis
{
    bool hasPattern = false; ///< false 表示这一行不产生表达式（空行 / 注释 / 写错）
    /// 这一行解析出来的表达式。
    ///
    /// **`hasPattern` 为假时它只带描述信息**（模式、行号、列号、原文），
    /// 不保证 `valid`。这样界面在「只有前缀、没有正文」时还能说清楚
    /// 「你用的是正则模式，但后面没写东西」，而不会退回一句笼统的
    /// 「这一行有问题」。`LineFilter::parseDeclaration()` 只在 `hasPattern`
    /// 为真时才把它收进过滤器。
    LineFilterPattern pattern;
    QVector<LineFilterIssue> issues;
};

////
/// \brief 对**一行**声明做完整分析（认前缀、编译、报错、回溯风险预检）。
///
/// 与 `analyzeNameFilterLine()` 同一条纪律：解析、实时校验、复验共用这一份实现。
/// 界面因此在用户敲到一半时就能只说「这一行现在报什么错」，
/// 而不必构造一份半成品过滤器。
///
LineFilterLineAnalysis analyzeLineFilterLine(const QString &line, int lineNumber = -1);

// -----------------------------------------------------------------------------
// 行过滤器（第 1 条完成标准）
// -----------------------------------------------------------------------------

/// 解析结果：算出来的过滤器 + 逐行的问题（两者同时返回，理由同 `NameFilter::parse`）。
struct LineFilterParseResult;

/// 过滤结果（定义在 `LineFilter` 之后）。
///
/// **必须先在这里声明**：`LineFilter::filterLines()` 把它当返回类型。
/// 这与 `mask.h` 里「结果结构体按值持有 `Mask`，因此要先于它声明、后于它定义」
/// 是同一个理由——只是这里按值持有的是 `LineFilter` 的那一份在末尾。
struct LineFilterResult;

////
/// \brief 行过滤器：把匹配模式的行从比较输入里去掉。
///
/// ## 只吃**原始行**
///
/// `excludes()` 与 `filterLines()` 的输入是「按物理换行切出来的行」，
/// **不带**任何规范化（不 trim、不折叠空白、不统一行尾）。这是第 3 条
/// 「先过滤行再应用忽略规则」在接口上的体现，理由写在文件顶部——
/// 一句话：让「参与比较的行集」不随「忽略规则」变化。
///
/// ## 空行：只有通配模式命中不了，而且这是刻意保留的
///
/// 通配符模式复用的是掩码语言，而掩码把「空名字」视为无效
/// （`MaskSubject::isValid()` 要求名字非空），所以 `*` 命中不了空行。
/// 精确与正则两种模式照常工作：`re:^$` 命中空行，`re:^\s*$` 连只含空白的行一起。
///
/// **不**在 `excludes()` 里给空行开特例：那会让「掩码语言在空串上的行为」
/// 出现第二个事实来源，而它只影响一个几乎没人依赖的边角。
/// `Tests/ContentFilter` 有两条用例把这个行为钉住（`*` 不命中空行、
/// `re:^$` 命中空行）——曾经有一版实现在入口处对空行提前返回，
/// 顺手把 `re:^$` 也一起挡掉了，而「去掉空行」恰恰是报错提示里教用户的写法。
/// 改了会让 `re:` 与通配在空行上的行为不再可预测。
///
class LineFilter
{
public:
    /// 一行一条表达式；`#` 开头的整行是注释；空行忽略。
    /// 切行同时认 `\n` / `\r\n` / `\r`（复用 `splitDeclarationLines()`）——
    /// 只在 `\n` 上切会让 Windows 上编辑过的声明每行多一个 `\r`，
    /// 于是表达式**静静地对不上任何行**而看起来完美无缺。
    static LineFilterParseResult parseDeclaration(const QString &declaration);

    bool isEmpty() const { return m_patterns.isEmpty(); }
    int patternCount() const { return m_patterns.size(); }
    const QVector<LineFilterPattern> &patterns() const { return m_patterns; }

    /// 追加一条已经编译好的表达式（调用方一般是 `parseDeclaration`）。
    void addPattern(const LineFilterPattern &pattern);

    /// 逆运算：写回设置项用的声明文本（往返逐字一致）。
    QString toDeclarationText() const;

    /// 这一**原始行**是否被排除。
    bool excludes(const QString &rawLine) const;

    /// 命中该行的所有表达式下标（一条行可能同时命中多条，只报第一条会让
    /// 用户改掉一条之后发现还是被排除）。
    QVector<int> matchingPatternIndexes(const QString &rawLine) const;

    /// 过滤一批**原始行**。
    LineFilterResult filterLines(const QStringList &rawLines) const;

    /// 「行过滤：2 条表达式」
    QString describe() const;

private:
    QVector<LineFilterPattern> m_patterns;
};

/// 过滤结果：留下来的行 + 被丢掉的行 + 计数。
///
/// 为什么把「被丢掉的行」也留下：界面上要能回答「你刚才把哪几行吃掉了」。
/// 只给一个数字时，用户对内容过滤的唯一反馈渠道就是「结果少了一批」，
/// 而那正是本条目最容易让人误判成「软件坏了」的地方。
struct LineFilterResult
{
    QStringList lines;             ///< 留下来的原始行（顺序不变）
    QStringList droppedLines;      ///< 被丢掉的行（原始内容，便于回显）
    QVector<int> droppedLineNumbers; ///< 被丢掉的行号（1 起，指**输入**里的位置）
    QVector<int> hitsByPattern;    ///< 每条表达式实际丢掉了多少行，与 patterns() 同序
    int inputLineCount = 0;

    int droppedLineCount() const { return droppedLines.size(); }
    int keptLineCount() const { return lines.size(); }

    /// 「内容过滤：丢弃 3 行 / 共 120 行」
    QString summary() const;
};

/// 解析结果定义（放在 `LineFilter` 之后：它按值持有一个 `LineFilter`）。
struct LineFilterParseResult
{
    LineFilter filter;
    QVector<LineFilterIssue> issues;

    bool ok() const { return issues.isEmpty(); }
    int patternCount() const { return filter.patternCount(); }
    QString describeErrors() const;
};

// -----------------------------------------------------------------------------
// 关键字节过滤（第 2 条完成标准）
// -----------------------------------------------------------------------------

/// 多条字节序列之间的关系。
enum class KeyByteCombineMode
{
    AnyOf, ///< 含**任意**一条即纳入比较
    AllOf, ///< 必须**全部**含齐才纳入比较
};

/// 组合语义表的一行。
struct KeyByteCombineModeRow
{
    KeyByteCombineMode mode = KeyByteCombineMode::AnyOf;
    const char *identifier = nullptr;
    const char *key = nullptr;
    QString label;
    QString explanation;
};

/// 组合语义表（函数内静态 + 返回引用，理由同 `lineFilterModeTable()`）。
const QVector<KeyByteCombineModeRow> &keyByteCombineModeTable();

QVector<KeyByteCombineMode> allKeyByteCombineModes();
const KeyByteCombineModeRow *keyByteCombineModeRow(KeyByteCombineMode mode);
const char *keyByteCombineModeIdentifier(KeyByteCombineMode mode);
QString keyByteCombineModeLabel(KeyByteCombineMode mode);
QString keyByteCombineModeExplanation(KeyByteCombineMode mode);
QVector<QString> validateKeyByteCombineModeTable(const QVector<KeyByteCombineModeRow> &table);

/// 问题种类。
enum class KeyByteIssueKind
{
    Syntax,    ///< 转义写错（`\x` 后面不是两位十六进制、`\q` 这种不认识的转义）
    Empty,     ///< 这一行没有任何字节
    Duplicate, ///< 与前面某一条重复（不是错，但值得说一句：它永远不会改变结论）
};

const char *keyByteIssueKindIdentifier(KeyByteIssueKind kind);
QString keyByteIssueKindLabel(KeyByteIssueKind kind);

struct KeyByteIssue
{
    KeyByteIssueKind kind = KeyByteIssueKind::Syntax;
    int line = -1;
    int column = -1;
    int length = 0;
    QString text;
    QString message;
    QString hint;

    QString describe() const;
};

/// 一条字节序列。
struct KeyByteSequence
{
    int line = -1;
    int column = -1;
    QString source;    ///< 原样的那一行
    QByteArray bytes;  ///< 解出来的字节（**逐字节**，不是文本）

    /// 可读形式：可见 ASCII 原样，其余写成 `\xHH`。
    QString toDeclarationText() const;
};

/// 输入是文本还是二进制——**关键字节过滤只对二进制生效**。
///
/// 第 2 条的原话是「仅当**二进制文件**包含指定字节序列时才纳入比较」。
/// 把这条写成一个显式的输入参数而不是靠调用方自觉：文本文件走这一支时
/// 结论必须是 `NotApplicable`（不适用 → 放行），而不是「逐字节搜一遍」。
/// 后者的危害是它**看起来也对**——一个中文文本文件里搜 `E4` 会命中某个
/// 汉字的第一个字节，于是「按文件类型过滤」悄悄变成了「按字节碰运气」。
enum class ContentKind
{
    Text,
    Binary,
};

const char *contentKindIdentifier(ContentKind kind);
QString contentKindLabel(ContentKind kind);

/// 一条字节序列过滤的结论。三态而不是布尔。
enum class KeyByteOutcome
{
    Accepted,      ///< 该纳入比较（命中，或规则集为空）
    Rejected,      ///< 不纳入比较（没命中）
    NotApplicable, ///< 不适用（输入是文本文件）——**放行**
};

const char *keyByteOutcomeIdentifier(KeyByteOutcome outcome);
QString keyByteOutcomeLabel(KeyByteOutcome outcome);

/// 判定结果。
struct KeyByteDecision
{
    KeyByteOutcome outcome = KeyByteOutcome::Accepted;
    int matchedSequenceIndex = -1; ///< 命中的那一条（`AnyOf` 是第一处命中；`AllOf` 是最后一条）
    QString reason;                ///< 一句话说明（界面/日志直接用）

    bool accepted() const { return outcome != KeyByteOutcome::Rejected; }
    QString describe() const;
};

/// 解析结果（定义在 `KeyByteFilter` 之后：它按值持有一个 `KeyByteFilter`）。
struct KeyByteParseResult;

////
/// \brief 关键字节过滤器：只让真正含目标字节序列的二进制文件进入比较。
///
/// ## 空规则集 = 没有约束（以及为什么这条必须写下来）
///
/// 规格说「仅当二进制文件包含指定字节序列时才纳入比较」。若把这句话直译成
/// 「不含 → 排除」，那么**一条规则都没配**时结论会是「谁都不含 → 全部排除」，
/// 用户的目录直接变空。因此空规则集的结论是 `Accepted` + 空 `reason`
/// ——「没有约束」。这与 `MaskFilter` / `NameFilter` 的「空过滤器保留条目」
/// 是同一条纪律（本仓三条过滤器各有一句断言，措辞一致）。
///
/// ## 声明的写法
///
/// 一行一条字节序列，`#` 开头是注释。可见的 ASCII 原样写，其余写转义：
///   * `\xHH`：两位十六进制（大小写都认）；
///   * `\n` `\r` `\t` `\0` `\\`：C 风格转义；
///   * 其余字符按 **UTF-8** 编成字节。
///
/// 为什么非 ASCII 走 UTF-8 而不是「按本机编码」：声明文本本身是 `QString`，
/// 它在进入本模块之前就已经是「正确解码后的文本」了。再按本机编码编回去
/// 会让同一份设置文件在两台机器上给出不同的字节序列，而两边都看不出差别。
/// 需要精确控制字节的写法是 `\xHH`，它是平台无关的。
///
class KeyByteFilter
{
public:
    static KeyByteParseResult parseDeclaration(const QString &declaration);

    void setCombineMode(KeyByteCombineMode mode) { m_combineMode = mode; }
    KeyByteCombineMode combineMode() const { return m_combineMode; }

    bool isEmpty() const { return m_sequences.isEmpty(); }
    int sequenceCount() const { return m_sequences.size(); }
    const QVector<KeyByteSequence> &sequences() const { return m_sequences; }
    void addSequence(const KeyByteSequence &sequence);

    /// 逆运算：写回设置项用的声明文本（往返**逐字节**一致）。
    QString toDeclarationText() const;

    /// 判定一批原始字节。
    KeyByteDecision decide(const QByteArray &data,
                           ContentKind kind = ContentKind::Binary) const;

    /// 「关键字节：含任意一条（2 条）」
    QString describe() const;

private:
    QVector<KeyByteSequence> m_sequences;
    KeyByteCombineMode m_combineMode = KeyByteCombineMode::AnyOf;
};

/// 解析结果（放在 `KeyByteFilter` 之后：它按值持有一个 `KeyByteFilter`）。
struct KeyByteParseResult
{
    KeyByteFilter filter;
    QVector<KeyByteIssue> issues;

    bool ok() const { return issues.isEmpty(); }
    int sequenceCount() const { return filter.sequenceCount(); }
    QString describeErrors() const;
};

////
/// \brief 把一段声明文本解成字节（`\xHH` / C 风格转义 / 其余按 UTF-8）。
///
/// 出错时返回空数组并填 `error` 与 `errorColumn`（相对 `text` 的列号）。
/// 单独暴露出来是为了让界面的实时校验能只说「第几列那个转义不认识」，
/// 而不必先拼出一整条字节序列。
///
QByteArray decodeByteSequenceText(const QString &text, QString *error = nullptr,
                                 int *errorColumn = nullptr);

////
/// \brief `decodeByteSequenceText()` 的逆：可见 ASCII 原样，其余 `\xHH`。
///
/// 往返必须**逐字节**一致（`decode(encode(x)) == x`）。只对「能被编码回来的
/// 字节序列」成立显然是废话，这条约束真正拦住的是另一种写法：
/// 把 `0x0A`（换行）原样吐出去，于是往返回来的序列里多了一个换行——
/// 而在编辑器里看，两段文本长得一模一样。
///
QString encodeByteSequenceText(const QByteArray &bytes);

// -----------------------------------------------------------------------------
// 内容过滤的启用状态与提示（规格「边界」条款）
// -----------------------------------------------------------------------------

/// 内容过滤的启用状态。**默认关闭**。
///
/// 「配了规则」与「启用」是两件事：规格的边界条款要求「必须显式启用」，
/// 而一份从别人那里拿来的会话文件里可能带着满满的规则。把两者分开之后，
/// 「我什么都没点，为什么每个文件都被读了一遍」在结构上不可能发生。
struct ContentFilterEnablement
{
    bool enabled = false;
    bool hasRules = false; ///< 至少配了一条行过滤或关键字节规则

    bool active() const { return enabled && hasRules; }

    bool operator==(const ContentFilterEnablement &other) const
    {
        return enabled == other.enabled && hasRules == other.hasRules;
    }
    bool operator!=(const ContentFilterEnablement &other) const { return !(*this == other); }
};

////
/// \brief 关于当前启用状态的一句话提示（**可测数据，不是对话框里的字符串**）。
///
/// 四种组合各有自己的话，且都不说谎：
///   * 关闭 + 没规则 → 「内容过滤未启用，也没有配置规则。」
///   * 关闭 + 有规则 → 「已配置内容过滤规则，但未启用，比较时不会读取文件内容。」
///     （这一条最要紧：用户配完规则以为生效了，结果什么都没发生，
///      而他唯一的线索就是「结果里还留着那些行」。）
///   * 启用 + 没规则 → 「内容过滤已启用，但没有配置任何规则，不会产生任何影响。」
///   * 启用 + 有规则 → 规格第 4 条要求的性能提示
///     「内容过滤已启用，比较速度会降低」。
///
QString contentFilterEnablementNotice(const ContentFilterEnablement &state);

/// 规格第 4 条要求的那句性能提示（单独暴露，便于状态栏直接取用）。
QString contentFilterPerformanceNotice();

/// 内容过滤在设置里占的键名（与 `nameFilterDeclarationKey()` /
/// `attributeFilterDeclarationKey()` 同一手法）。
QString lineFilterDeclarationKey();
QString keyByteFilterDeclarationKey();
QString contentFilterEnabledKey();

// -----------------------------------------------------------------------------
// 表自检（参数化，见 validateContentFilterStageTable 的说明）
// -----------------------------------------------------------------------------

/// 一次核对阶段表、行过滤模式表、字节组合语义表三张表；返回问题清单。
QVector<QString> validateContentFilterTables(
    const QVector<ContentFilterStageRow> &stages,
    const QVector<LineFilterModeRow> &lineModes,
    const QVector<KeyByteCombineModeRow> &combineModes);

/// 用模块内置的三张表跑一次自检。
///
/// **刻意不在启动时调用**。夜间重写 `main.cpp` 之后，之前几轮加的四条
/// `validate*Tables()` 启动自检都没有调用方了（见 current-handoff §6 最后一条），
/// 「要不要加回启动自检」是一个尚未决定的问题。本条目不擅自替它做决定：
/// 自检函数留在模块里、由 `Tests/ContentFilter` 直接调用，
/// 于是新表从第一天起就被守住，也不会与那个悬而未决的问题打架。
QVector<QString> validateBuiltinContentFilterTables();

} // namespace Filter
} // namespace LqCompare

#endif // LQCOMPARE_CONTENTFILTER_H
