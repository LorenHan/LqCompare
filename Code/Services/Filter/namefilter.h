#ifndef LQCOMPARE_NAMEFILTER_H
#define LQCOMPARE_NAMEFILTER_H

#include "mask.h"
#include "maskfilter.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace LqCompare {
namespace Filter {

///
/// \brief 名称过滤器：按条目名字做高级匹配，支持精确名 / 通配符 / 正则三种模式
/// （PRD: FILT-002）。
///
/// ## 它与「掩码过滤」（maskfilter.h）不是同一件东西
///
/// 两者都看名字，但回答的问题不同：
///   * `MaskFilter` 回答「一份多行**声明**叠加之后，这个条目留不留」——它带
///     包含/排除前缀，带「排除优先」的叠加规则，输出是 `MaskVerdict`。
///   * 本文件回答「一组**表达式**在三种组合语义下，这个条目留不留」——
///     它没有 `-` 排除前缀（排除语义由 `NameCombineMode::NoneOf` 承担），
///     输出是三态结论 + 超时问题清单。
///
/// 刻意**不**把两者合成一个类：掩码是「写一堆路径规则」，名称过滤器是
/// 「写几条高级匹配再选一种组合语义」，界面上是两个不同的输入框，
/// 需求也分属两个条目。合成一个类之后，任何一个界面的改动都会波及另一个。
/// 两者与属性过滤（`AttributeFilter`）之间是**与**的关系，最终合成入口仍然
/// 是 `decideEntry()`；本条目不参与那个合成（FILT-002 的完成标准里没有这一条），
/// 因此不擅自改动 `decideEntry()` 的签名。
///
/// ## 四条贯穿全文件的约定（改之前先读）
///
/// 1. **模式是每条表达式自己的，不是整份过滤器的一个开关**。声明文本里用行首
///    前缀表达（`=` 精确名 / 无前缀 通配符 / `re:` 正则），
///    `defaultMatchMode()` 只决定「界面上下一次新增的表达式用哪种模式」。
///    为什么不把模式做成整份过滤器的一个字段：那样一来用户在下拉框里换一下模式，
///    **所有已有表达式的含义会被静默重解**（`README.md` 从「精确名」变成
///    「正则 `README.md`」再变成「通配符」），而界面上看起来什么都没变。
///    这类「改一个开关，悄悄改了别处的语义」正是本仓一直在拒绝的东西。
///    `toDeclarationText()` 因此**总是**把超出通配符的部分写成显式前缀，
///    于是「读回来的过滤器」与「写出去的文本」语义永远一致。
///
/// 2. **写坏的表达式不参与收窄，但一定会被报出来**。与 `MaskFilter::parse`、
///    `AttributeFilter::parseDeclaration` 是同一条纪律：一行写错只丢那一行，
///    其余行照常生效。理由也一样——用户改到一半时整份声明失效，
///    他会以为是自己把别的地方敲坏了。
///
/// 3. **不确定的结论一律放行，且必须被报出来**。正则超时（第 2 条完成标准）
///    与「表达式因连续超时被停用」都不产生「不匹配」这个结论，而是产生
///    `NameMatchOutcome::Undecided`：条目照样进入结果集，同时有一条
///    `NameFilterIssue`。这与 `attributefilter.h` 顶部第 1 条取舍**逐字同源**：
///    把「判不出来」当成「不符合」，用户在界面上只会看到一个少了一批文件的结果，
///    而他唯一能想到的解释是「这软件坏了」。
///
/// 4. **只有正则模式需要超时保护**。通配符走本仓自己的 NFA 模拟
///    （复杂度 O(名字长度 × 掩码段数)，见 mask.cpp），精确名是一次字符串比较，
///    两者都不存在回溯爆炸。把三个模式都塞进工作线程只会让每一次判定多付
///    一次线程同步的代价，换不到任何安全性。所以 `decide()` 只在正则那一支
///    经过 `NameMatchRunner`。
///
/// ## 灾难性回溯的三层防护
///
/// 规格的边界条款写着「正则表达式必须做灾难性回溯防护（超时保护），否则一个
/// 恶意正则会冻结整个扫描」。本文件给了三层，各自只管一段：
///
///   1. **静态预检**（`analyzeRegexPatternRisk()`）：认出嵌套量词
///      （`(a+)+`、`(.*)*`）这类**结构上必然爆炸**的写法，随解析一起报成
///      `NameFilterIssueKind::Risky`。它只提示、不阻断——用户可能真的知道自己在
///      写什么（例如对着很短的样本写）。提示的价值在于「不用等到真卡住才知道」。
///   2. **运行期截止时间**（`NameMatchRunner`）：默认 200ms/条，超时就把这条
///      表达式记成 `Undecided` + 一条 `Timeout` 问题，**继续判定下一个条目**。
///   3. **断路器**（`NameMatchBudget::consecutiveTimeoutLimit`）：同一条表达式
///      连续超时到上限后**停用该表达式**，并记一条 `Disabled` 问题。
///      它不是「锦上添花」：Qt 5.15 无法中断一个正在跑的正则匹配，
///      超时被放弃的那次匹配会**一直占着一个工作线程**直到它自己跑完。
///      没有断路器的话，一条恶意表达式会让后面每一次匹配都排队等它、
///      于是**每一个条目都报超时**——用户看到的是「整个过滤器全红了」，
///      而不是「这一条表达式有问题」。断路器把这件事重新变回可归因的。
///
/// ## Qt 5.15 的现实（不要试图「顺手加个参数」）
///
/// `QRegularExpression::setMatchTimeout()` / `matchTimeout()` 是 **Qt 6.0** 才有的
/// （已在本机 `~/Qt/5.15.2/clang_64/lib/QtCore.framework/.../qregularexpression.h`
/// 里逐字核对过：没有这两个成员）。Qt 5.15 里 PCRE2 自带一个 match limit，
/// 但它既不可配、也不通过 Qt 暴露，Qt 只会把「超出 limit」与「不匹配」报成
/// 同一个结果（一个无效的 `QRegularExpressionMatch`），因此**拿不到**
/// 「这次是被放弃的」这个事实——而那正是第 2 条要求「记录为错误条目」的东西。
/// 于是只能自己做截止时间：把匹配放到工作线程上等一个时间预算（`NameMatchRunner`）。
///
/// 线程**无法**被真正杀掉（`QThread::terminate()` 会留下锁与栈上的对象，
/// 后果比慢一次更糟，本仓不用）。这一点是公开的代价，写在
/// `ThreadNameMatchRunner` 的注释里，并由断路器兜住上界。
///

// -----------------------------------------------------------------------------
// 匹配模式
// -----------------------------------------------------------------------------

/// 一条表达式用哪种方式匹配名字。顺序即 `nameMatchModePrefixTable()` 的顺序。
enum class NameMatchMode
{
    Exact,    ///< 精确名：名字与该字符串全等
    Wildcard, ///< 通配符：掩码语法（mask.h），整名匹配
    Regex,    ///< 正则：PCRE2，**整名匹配**（见 nameMatchModeSemanticsNote）
};

const char *nameMatchModeIdentifier(NameMatchMode mode);
QString nameMatchModeLabel(NameMatchMode mode);

/// 稳定键（进预设文件与命令行，因此一经发布不得改动）。
QString nameMatchModeKey(NameMatchMode mode);

/// 从稳定键反查；不认识时返回 `Exact` 并把 `ok` 置假。
NameMatchMode nameMatchModeFromKey(const QString &key, bool *ok = nullptr);

///
/// \brief 三种模式的匹配范围各是什么（界面要显示，测试要断言）。
///
/// **三种模式都是「整名匹配」**，这是刻意统一的：切换模式只应当改变
/// **表达力**，不应当改变**匹配范围**。如果正则模式按「子串搜索」实现，
/// 用户把一条写好的 `abc` 从通配符切到正则，匹配结果会从
/// 「名字恰好是 abc」变成「名字里含 abc」——而界面上只动了一个下拉框。
/// 想搜子串的写法是 `.*abc.*`，代价是多敲几个字符，换来的是「切换模式不会
/// 悄悄多放进来一批条目」。
///
QString nameMatchModeSemanticsNote(NameMatchMode mode);

/// 全部模式（界面下拉用）。
QVector<NameMatchMode> allNameMatchModes();

///
/// \brief 声明文本里表示模式的行首前缀。
struct NameMatchModePrefix
{
    NameMatchMode mode = NameMatchMode::Wildcard;
    QString prefix; ///< 空串表示「无前缀」（通配符）
    QString meaning;
};

///
/// \brief 前缀表（唯一的事实来源：解析、拼文本、界面提示都从这里取）。
///
/// 为什么通配符是「无前缀」：`*.cpp` 是这类输入框里最常见的写法，
/// 给它加前缀等于让最常见的用法最难写。代价是**行首出现 `=` 或 `re:` 时
/// 会被当成前缀**——这个边界是有意的，绕开方式是改用显式模式：
/// 精确名 `=foo` 写成 `= =foo`（前缀后允许空白），
/// 通配符 `re:*.cpp` 写成 `re:^re:.*\.cpp$`。`Tests/NameFilter` 里有一条
/// 用例把这个边界与两种绕法一起钉住，免得下一个人「顺手」把前缀放宽。
///
QVector<NameMatchModePrefix> nameMatchModePrefixTable();

/// 某个模式的前缀（查表结果，不做兜底猜测）。
QString nameMatchModePrefix(NameMatchMode mode);

/// 解析结果：模式与去掉前缀后的文本（保留原样，不 trim）。前缀不匹配时返回 false。
struct NameMatchModePrefixHit
{
    bool found = false;
    NameMatchMode mode = NameMatchMode::Wildcard;
    int prefixLength = 0; ///< 前缀占用的字符数（用于算列号）
};

/// 认出某一行的模式前缀。`line` 允许有前导空白（前缀识别在跳过空白之后）。
NameMatchModePrefixHit matchNameMatchModePrefix(const QString &line);

// -----------------------------------------------------------------------------
// 组合语义
// -----------------------------------------------------------------------------

/// 一组表达式怎么合成一个结论。顺序即 `allNameCombineModes()` 的顺序。
enum class NameCombineMode
{
    AnyOf,  ///< 包含任一：任意一条命中即保留
    NoneOf, ///< 不包含任何：一条都不命中才保留
    AllOf,  ///< 全部满足：所有表达式都命中才保留
};

const char *nameCombineModeIdentifier(NameCombineMode mode);
QString nameCombineModeLabel(NameCombineMode mode);

/// 稳定键（进预设文件）。
QString nameCombineModeKey(NameCombineMode mode);
NameCombineMode nameCombineModeFromKey(const QString &key, bool *ok = nullptr);

/// 这个语义对用户到底意味着什么（界面必须显示，见第 3 条完成标准）。
QString nameCombineModeExplanation(NameCombineMode mode);

QVector<NameCombineMode> allNameCombineModes();

///
/// \brief 组合语义表的一行。
///
/// 与 `attributeConditionTable()` 同一个手法：**表是数据、是唯一的事实来源**，
/// `nameCombineModeLabel()` / `nameCombineModeExplanation()` 都从它取。
/// 之所以把标签与解释也放进这张表（而不是散在几个 switch 里），是因为
/// 「界面显示的语义」与「判定用的语义」必须是同一份数据推出来的——
/// 两份必然分家，而分家的表现是「提示里写着『任意一条命中就保留』，
/// 程序做的却是『一条都不命中才保留』」，用户按提示去改、越改越不对。
///
/// 它同时是**可注入**的：`validateNameFilterTables()` 收一份表当参数，
/// 于是用例能拿一份故意写坏的表（两种语义共用同一句解释）证明自检真的会报。
///
struct NameCombineModeRow
{
    NameCombineMode mode = NameCombineMode::AnyOf;
    QString label;
    QString explanation;
};

/// 组合语义表（返回副本，调用方改不到内部那份）。
QVector<NameCombineModeRow> nameCombineModeTable();

///
/// \brief 模式表与组合语义表的自检（启动自检用）。
///
/// **表是参数**，不是从内部取固定表。理由与 `validateFilterLayerTable()` /
/// `validateAttributeConditionTable()` 完全一样：这两张表查的是「手写表时容易
/// 写错、写错了也不影响别的」的几件事（前缀重复、漏掉一个模式、两种语义共用
/// 一句解释），而这类问题如果自检只吃固定表，用例就拿不到「故意写坏的表」，
/// 这条护栏永远不会红——**一条永远不会红的护栏比没有护栏更糟**，
/// 它会让人以为这块已经被守住了。
///
QVector<QString> validateNameFilterTables(const QVector<NameMatchModePrefix> &modeTable,
                                          const QVector<NameCombineModeRow> &combineTable);

// -----------------------------------------------------------------------------
// 表达式
// -----------------------------------------------------------------------------

///
/// \brief 一条已解析成功的表达式。
struct NameFilterExpression
{
    NameMatchMode mode = NameMatchMode::Wildcard;
    QString text; ///< 用户写的原文（不含模式前缀，已去掉首尾空白）
    int line = -1;
    int column = -1; ///< 文本（含前缀）在该行里的起始列

    /// 「第 2 行：正则 `^a.*b$`」
    QString describe() const;
};

// -----------------------------------------------------------------------------
// 问题（写坏的表达式、超时、被停用、可疑写法）
// -----------------------------------------------------------------------------

/// 问题的来源。界面按它决定「标红」还是「标黄」。
enum class NameFilterIssueKind
{
    Syntax,   ///< 语法错（正则编译不过、掩码解析失败）——标红，该行不生效
    Risky,    ///< 结构上可能灾难性回溯——标黄，该行**仍然生效**
    Timeout,  ///< 运行期某一条目超时——标黄，该条目结论「不确定」
    Disabled, ///< 连续超时后表达式被停用——标红，该表达式不再生效
};

const char *nameFilterIssueKindIdentifier(NameFilterIssueKind kind);
QString nameFilterIssueKindLabel(NameFilterIssueKind kind);

///
/// \brief 一条问题。
///
/// 位置按**整行**给出（与 `MaskRuleError` 同形），界面可以直接拿去选中。
struct NameFilterIssue
{
    NameFilterIssueKind kind = NameFilterIssueKind::Syntax;
    int line = -1;
    int column = -1;
    int length = 0;
    QString text;    ///< 出问题的那段文本（便于界面回显）
    QString message; ///< 出了什么问题
    QString hint;    ///< 怎么改（可为空）

    /// 「第 2 行第 3 列：语法错：…（…）」
    QString describe() const;
};

/// 一组问题（`describe()` 拼成多行文本）。
struct NameFilterProblems
{
    QVector<NameFilterIssue> issues;

    bool ok() const { return issues.isEmpty(); }
    int countOfKind(NameFilterIssueKind kind) const;
    QString describe() const;
};

// -----------------------------------------------------------------------------
// 单行分析（解析、实时校验、复验共用的唯一入口）
// -----------------------------------------------------------------------------

///
/// \brief 对**一行**声明做完整分析（认出模式、编译、报错、静态风险预检）。
///
/// 这是「什么叫一行合法的名称过滤表达式」的唯一实现：
/// `NameFilter::parse()` 对每一行调它，界面的实时校验（第 4 条完成标准）
/// 也调它，`NameFilter::validate()` 还是调它。做成一个公开函数而不是
/// 「解析器内部的一小段」有两个具体好处：
///   * 界面可以**只校验、不应用**——用户在输入框里敲到一半时，
///     要的是「这一行现在报什么错」，而不是一份半成品过滤器；
///   * 于是「非法正则就地报错且不生效」这条能写出**反向可验证**的用例：
///     把某一行写坏，`analyzeNameFilterLine()` 必须给出 `Syntax`，
///     且 `hasExpression == false`（即它不会被任何调用方拿去过滤）。
///
struct NameFilterLineAnalysis
{
    bool hasExpression = false;
    NameFilterExpression expression;
    QVector<NameFilterIssue> issues;

    bool hasSyntaxError() const;
};

NameFilterLineAnalysis analyzeNameFilterLine(const QString &line, int lineNumber = -1,
                                             MaskPlatform platform = currentMaskPlatform());

// -----------------------------------------------------------------------------
// 超时保护（第 2 条完成标准）
// -----------------------------------------------------------------------------

///
/// \brief 匹配的时间预算。
///
/// `perEntryMs <= 0` 表示**不做超时保护**，正则直接在当前线程上跑。
/// 这不是「关掉一个不方便的功能」：命令行/批处理场景没有界面可冻结，
/// 而省下一次线程同步的代价是真实的。默认值 200ms 来自规格原文。
///
struct NameMatchBudget
{
    int perEntryMs = 200;
    int consecutiveTimeoutLimit = 2;

    bool isEnabled() const { return perEntryMs > 0; }
    bool hasCircuitBreaker() const { return consecutiveTimeoutLimit > 0; }
};

///
/// \brief 「在一个时间预算内跑一段可能跑不完的匹配」这件事的抽象。
///
/// 做成接口而不是直接写线程池，只有一个理由：**策略要能被确定性地测**。
/// 真实实现靠墙上时钟，用例要断言「超时之后发生什么」就只能真等 200ms，
/// 而且机器一忙就飘。把机制抽出来之后，用例注一个「永远报超时」的替身，
/// 就能精确断言超时路径上的每一条行为；真实机制另有几条只测机制本身的用例
/// （见 Tests/NameFilter 的 D 组）。
///
class NameMatchRunner
{
public:
    enum class Status
    {
        Completed, ///< 在预算内跑完了，`matched` 有效
        TimedOut,  ///< 超时放弃，`matched` 无意义（一定是 false）
    };

    struct Outcome
    {
        Status status = Status::Completed;
        bool matched = false;
    };

    virtual ~NameMatchRunner();

    /// 跑 `task` 并在 `budgetMs` 之后放弃。`budgetMs <= 0` 表示不设上限。
    virtual Outcome run(const std::function<bool()> &task, int budgetMs) = 0;
};

///
/// \brief 生产实现：一条专属工作线程 + 截止时间。
///
/// 三条必须知道的代价（都写在这里，免得下一个人以为它是 bug）：
///
///   1. **被放弃的任务没有被杀掉，也无法被杀掉**。它继续在旧线程上跑到自己结束；
///      线程池的容量是 1，所以那个池在它结束之前是死的。本实现因此在**每次超时
///      之后就把整个池丢掉、换一个新的**，于是后续匹配不会被排在那条表达式后面。
///      泄漏的线程数是「每条被放弃的表达式 1 个」，由断路器给出上界。
///   2. **不用 `QThread::terminate()`**：它可能在任意指令处中断，留下持锁的
///      mutex、半构造的对象与不确定的栈——比慢一次糟得多。
///   3. **不用 `QThreadPool` 而用手写的单线程**是因为要「丢掉整个池」这个动作；
///      `QThreadPool` 不允许把它从某个跑不完的任务里解放出来。
///
class ThreadNameMatchRunner : public NameMatchRunner
{
public:
    ThreadNameMatchRunner();
    ~ThreadNameMatchRunner() override;

    Outcome run(const std::function<bool()> &task, int budgetMs) override;

    /// 至今为止被放弃（超时）的任务数，供自检与诊断使用。
    int abandonedCount() const;

private:
    class Worker;
    friend class Worker;

    /// 当前的工作线程；超时后置空并重建。
    Worker *m_worker = nullptr;
    int m_abandoned = 0;
};

///
/// \brief 进程内共享的默认运行器（惰性创建）。
///
/// 共享而不是「每个过滤器一个」：每个实例一个线程池意味着几百个线程。
/// 默认运行器对所有 `NameFilter` 实例是安全的——它自身只做「跑一段代码 +
/// 等一个预算」，不含任何过滤器状态。
///
std::shared_ptr<NameMatchRunner> defaultNameMatchRunner();

// -----------------------------------------------------------------------------
// 判定结论
// -----------------------------------------------------------------------------

/// 一条表达式对一个条目的结论。三态，理由见文件顶部第 3 条约定。
enum class NameMatchOutcome
{
    NotMatched,
    Matched,
    Undecided, ///< 超时 / 已停用——既不是命中也不是不命中
};

const char *nameMatchOutcomeIdentifier(NameMatchOutcome outcome);
QString nameMatchOutcomeLabel(NameMatchOutcome outcome);

///
/// \brief 一个条目在整份名称过滤器下的结论。
struct NameFilterDecision
{
    bool accepted = true;
    NameCombineMode combine = NameCombineMode::AnyOf;

    /// 与 `NameFilter::expressions()` 同序。
    QVector<NameMatchOutcome> outcomes;
    QVector<int> matchedIndexes;
    QVector<int> undecidedIndexes;

    /// 起决定作用的表达式下标；-1 表示没有表达式参与（空过滤器）。
    int decisiveIndex = -1;

    /// 本次判定产生的问题（超时、被停用）。写坏的表达式在解析期就报过了。
    QVector<NameFilterIssue> issues;

    /// 「包含任一：命中 1 条（`*.cpp`）」这类一句话说明。
    QString describe() const;
};

// -----------------------------------------------------------------------------
// 名称过滤器
// -----------------------------------------------------------------------------

// 解析结果按值持有 NameFilter，因此要先于它声明、后于它定义。
struct NameFilterParseResult;

class NameFilter
{
public:
    NameFilter();

    ///
    /// \brief 解析一份名称过滤声明。
    ///
    /// 声明语法（一行一条）：
    ///   * 空行、只有空白字符的行 —— 忽略
    ///   * 第一个非空白字符是 `#` —— 整行注释，忽略
    ///   * `=` 开头 —— 精确名
    ///   * `re:` 开头 —— 正则
    ///   * 其余 —— 通配符（掩码语法）
    ///
    /// 模式前缀之后**允许有空白**，它会被去掉；这也顺带给出「以 `=` 开头的
    /// 精确名」的写法（`= =foo`）。文本首尾空白一律去掉。
    ///
    /// 某一行的表达式写错（正则编译不过、掩码语法错）时**只有那一行被丢弃**，
    /// 错误收在 `issues` 里。`platform` 只影响**默认**的大小写敏感性。
    ///
    static NameFilterParseResult parse(const QString &declaration,
                                       MaskPlatform platform = currentMaskPlatform());

    // ---- 表达式 ----
    const QVector<NameFilterExpression> &expressions() const { return m_expressions; }
    int expressionCount() const { return m_expressions.size(); }

    /// 一条可生效的表达式都没有（等价于「什么都不过滤」）。
    bool isEmpty() const { return m_expressions.isEmpty(); }

    // ---- 组合语义 ----
    NameCombineMode combineMode() const { return m_combine; }
    void setCombineMode(NameCombineMode mode);

    /// 「包含任一：3 条表达式中任意一条命中即保留」——界面直接显示这一句。
    /// 空过滤器与一条表达式的措辞不同（「一条表达式」时说「任意一条」很怪）。
    QString combineSummary() const;

    // ---- 新增表达式时用的模式（界面下拉）----
    NameMatchMode defaultMatchMode() const { return m_defaultMode; }
    void setDefaultMatchMode(NameMatchMode mode);

    // ---- 大小写 ----
    MaskPlatform platform() const { return m_platform; }
    Qt::CaseSensitivity caseSensitivity() const { return m_case; }
    bool isCaseSensitivityOverridden() const { return m_caseOverridden; }
    void setCaseSensitivity(Qt::CaseSensitivity cs);
    void clearCaseSensitivityOverride();

    // ---- 超时保护 ----
    const NameMatchBudget &matchBudget() const { return m_budget; }
    void setMatchBudget(const NameMatchBudget &budget);

    std::shared_ptr<NameMatchRunner> matchRunner() const { return m_runner; }

    /// 注入运行器（测试用；生产走 defaultNameMatchRunner()）。
    void setMatchRunner(const std::shared_ptr<NameMatchRunner> &runner);

    /// 至今为止的超时次数 / 被停用的表达式条数。
    int timeoutCount() const { return m_timeoutCount; }

    /// 被停用的表达式**条数**（不是「可能被停用的槽位数」）。
    int disabledExpressionCount() const;
    bool isExpressionDisabled(int index) const;

    /// 清掉超时计数与停用标记（界面改完表达式后重新开始统计）。
    void resetTimeoutState();

    // ---- 判定 ----
    ///
    /// 对一个名字下结论。
    ///
    /// 名字为空（`MaskSubject::isValid()` 为假）时结论是 `accepted = false`——
    /// 与 `MaskFilter::decide` 同一条方向：调用方给了残缺的条目，保守的一侧是
    /// 把它挡在外面，而不是让一个本该报错的输入变成「界面上多了一个条目」。
    /// 例外是**空过滤器**：那时什么都不过滤，结论恒为保留（启动自检盯住这一条）。
    ///
    NameFilterDecision decide(const QString &name) const;

    /// 是否保留（`decide` 的便捷写法）。
    bool accepts(const QString &name) const;

    ///
    /// \brief 重跑一遍全部静态检查（界面实时校验用）。
    ///
    /// 解析之后再调用它可以查出两类**解析期查不出或不完全**的东西：
    ///   * `Risky`：表达式的正则结构可能灾难性回溯（解析期也会报，这里重新查一次，
    ///     因为表达式可能已经被程序改过）；
    ///   * `Syntax`：正则/掩码在解析期就已编译过一次，这里重新编译，
    ///     用来证明「编译器与校验器是同一个实现」——两份实现必然分家，
    ///     而分家的表现是「界面上没报错、判定时却永远不匹配」。
    ///
    NameFilterProblems validate() const;

    /// 「包含任一，3 条表达式，大小写敏感（平台默认：posix）」
    QString describe() const;

    /// 把当前状态写回声明文本（`parse()` 的逆；模式超出通配符时才写前缀）。
    QString toDeclarationText() const;

private:
    QVector<NameFilterExpression> m_expressions;
    /// 与 `m_expressions` 同序的预编译结果：通配符的 `Mask` / 正则的
    /// `QRegularExpression`。用两个并行数组而不是把编译器塞进公开结构体
    /// （那会把 `QRegularExpression` 变成接口的一部分，测试替身就替不掉它）。
    QVector<Mask> m_masks;
    QVector<QRegularExpression> m_regexes;

    NameCombineMode m_combine = NameCombineMode::AnyOf;
    NameMatchMode m_defaultMode = NameMatchMode::Wildcard;
    MaskPlatform m_platform = MaskPlatform::Posix;
    bool m_caseOverridden = false;
    Qt::CaseSensitivity m_case = Qt::CaseSensitive;
    NameMatchBudget m_budget;
    std::shared_ptr<NameMatchRunner> m_runner;

    // 判定会更新这两个计数器，因此 `decide()` 是 const 而它们是 mutable。
    // 为什么不做成非 const：界面在绘制列表时就在判定（每个条目一次），
    // 把 `decide()` 变成非 const 会逼着所有调用点丢掉 const 正确性。
    mutable int m_timeoutCount = 0;
    mutable QVector<bool> m_disabled;
    mutable QVector<int> m_consecutiveTimeouts;

    /// 单条表达式对一个名字的结论（正则那一支走 `NameMatchRunner`）。
    NameMatchOutcome evaluate(int index, const QString &name, QVector<NameFilterIssue> *issues) const;

    /// 超时计数 +1，返回「是否因此停用了该表达式」。
    bool noteTimeout(int index, const NameFilterExpression &expression) const;
};

///
/// \brief 解析结果：算出来的过滤器 + 逐行的问题。
///
/// 两者同时返回而不是「有错就整体失败」：见 `NameFilter::parse` 的说明。
///
struct NameFilterParseResult
{
    NameFilter filter;
    QVector<NameFilterIssue> issues;

    bool ok() const { return issues.isEmpty(); }
    QString describeErrors() const;
};

// -----------------------------------------------------------------------------
// 静态风险预检
// -----------------------------------------------------------------------------

///
/// \brief 一段正则里可能灾难性回溯的片段。
struct RegexRisk
{
    int column = 0; ///< 在正则文本里的起始列（0 起）
    int length = 0;
    QString snippet; ///< 出问题的那个片段（如 `(a+)+`）
    QString reason;  ///< 为什么可疑

    QString describe() const;
};

///
/// \brief 找出结构上可能灾难性回溯的片段。
///
/// 报的是「一个被**可变**量词修饰的组，它内部还有**可变**量词」。
/// `(a+)+`、`(.*)*`、`((a+))+` 都在此列；
/// `(a|b)*`、`\.(cpp|h)$`、`^test_[0-9]{2}\.log$` **不报**——它们的内层没有量词，
/// 而「交替式里放量词」与本案不是一回事（PCRE 的自动占有化会处理掉大多数）。
/// **固定次数**的量词也不算可变：`(a{2})+` 每一轮恰好吃两个字符，回溯是线性的；
/// `(a+){2}` 外层只有两轮，也不是指数。把这两类报出来是纯粹的误报。
///
/// 宁可漏报也不误报：一条「凡是有括号就报可疑」的实现会让用户学会忽略这个提示，
/// 于是它连本该拦下的那一类也拦不住了。
///
QVector<RegexRisk> analyzeRegexPatternRisk(const QString &pattern);

// -----------------------------------------------------------------------------
// 声明键与预设（第 5 条完成标准）
// -----------------------------------------------------------------------------

/// 名称过滤声明在设置里占的键名（与 `attributeFilterDeclarationKey()` 同一手法）。
QString nameFilterDeclarationKey();

///
/// \brief 一份具名预设。
///
/// 第 5 条要的是「表达式可命名保存为预设并导出」。本文件给到「命名 + 可移植文本
/// 的往返」这两半；**落盘位置、内置预设、预设画廊与导入界面属 FILT-007**，
/// 因此第 5 条只勾得动其中一半，理由写在 handoff 文档与 issue 里。
///
/// 这个文本格式是**唯一的**导出格式，FILT-007 应当复用/扩展它，
/// 不要另起一套——两份格式必然分家，而分家的表现是「我导出的预设导入不回来」。
///
struct NamedNameFilter
{
    QString name;
    QString note;
    NameCombineMode combine = NameCombineMode::AnyOf;

    /// 大小写策略是否被这份预设显式覆盖过（没覆盖就跟随平台默认）。
    bool caseOverridden = false;
    Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;

    QString declaration;

    bool isValid() const { return !name.isEmpty(); }
};

/// 预设文件的第一行（版本标识，解析时核对；不认识的行被忽略）。
QString nameFilterPresetFormatHeader();

/// 导出为可移植文本。
QString serializeNamedNameFilters(const QVector<NamedNameFilter> &presets);

struct NamedNameFilterParseResult
{
    QVector<NamedNameFilter> presets;
    QVector<NameFilterIssue> issues;

    bool ok() const { return issues.isEmpty(); }
    QString describeErrors() const;
};

///
/// \brief 从导出文本读回预设。
///
/// 行尾认 `\n` / `\r\n` / `\r`（复用 `splitDeclarationLines`），
/// 理由与 `MaskFilter::parse` 完全一样：预设是给人共享的文件，
/// 在 Windows 上编辑过就是 CRLF。
///
NamedNameFilterParseResult parseNamedNameFilters(const QString &text,
                                                 MaskPlatform platform = currentMaskPlatform());

} // namespace Filter
} // namespace LqCompare

#endif // LQCOMPARE_NAMEFILTER_H
