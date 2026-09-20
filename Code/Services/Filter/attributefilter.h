#ifndef LQCOMPARE_ATTRIBUTEFILTER_H
#define LQCOMPARE_ATTRIBUTEFILTER_H

#include "filterstack.h"
#include "mask.h"
#include "maskfilter.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Filter {

///
/// \brief 属性过滤：按条目的元数据（大小 / 修改时间 / 属性位 / 所有者）收窄结果
/// （PRD: FILT-003）。
///
/// ## 与名称过滤是两条正交的轴
///
/// 名称过滤（`MaskFilter` / `FilterStack`）只看**名字与路径**，属性过滤只看
/// **元数据**。规格第 4 条要求「多个属性条件之间为逻辑与，并与名称过滤构成整体的
/// 与关系」，因此两者不是二选一，而是同一个条目上互相独立的两个判定，
/// 最终结论是两者的**与**。合成入口在本文件末尾（`decideEntry`），
/// 不要在每个调用点各写一遍 `if (names.accepts(x) && attributes.accepts(x))`——
/// 那种写法迟早会出现一处写成 `||`，而现象是「加了过滤反而看到更多文件」。
///
/// ## 三条贯穿全文件的约定（改之前先读）
///
/// 1. **元数据缺失一律放行，但必须被报出来**。每个条件都返回
///    `ConditionOutcome`，用 `accepted` + `evaluated` 两位表达三态：
///    通过 / 拒绝 / 不知道。不知道时 `accepted = true`、`evaluated = false`，
///    并把这条条件列进 `AttributeDecision::undecided`，界面据此显示
///    「本平台上这一项没有生效」。
///    为什么是放行而不是拒绝：属性位（只读/系统/归档）与所有者/组在 Unix 与
///    Windows 上不是同一组概念——在 macOS 上「只看隐藏文件」可以判定（点开头），
///    而「只看归档位」永远判不出来。若把「判不出来」当成「不符合」，用户在
///    macOS 上勾一下「归档」就会得到**一个空列表**，而他唯一能想到的解释是
///    「这软件坏了」。放行 + 显式提示把「没生效」变成了可见的事实。
///    反过来说，本条规定**不适用于配置写错**（见第 2 条）——那是用户自己填错了，
///    必须报错并标红。
///
/// 2. **写坏的条件不参与收窄，但一定会被报出来**。与 `MaskFilter::parse`
///    「只有那一行被丢弃、其余照常生效」是同一条纪律：用户改到一半时整份声明
///    失效，会让人以为是自己把别的地方敲坏了。于是：
///    `size-min 100MB` + `size-max 10MB`（下限大于上限）不会让结果变成空列表，
///    而是被记成一条 `ConditionProblem`，界面职责是把它显示出来。
///    这条例外只有一处：**语法完全不认识的行**（未知键、空值）同样只是被丢掉并报错，
///    不牵连其它行。
///
/// 3. **「现在」由调用方给进来**（`setReferenceTime()`），本模块从不自己取
///    系统时钟。相对时间（「最近 7 天」）的判定依赖一个时刻，
///    在实现里直接 `QDateTime::currentDateTime()` 会让用例随运行时刻漂移
///    （跨午夜、闰秒、CI 上时区不同），而失败方式是「偶发红」，最难查。
///    做成显式输入之后，相对窗口在任意时刻都能被精确断言，
///    同一条纪律见 `MaskPlatform`（平台也做成参数而不是 `#ifdef`）。
///
/// ## 声明文本
///
/// 沿用 FILT-005「一个键 + 多个存储」的手法：属性条件也能写成一份多行声明
/// （`toDeclarationText()` / `parseDeclaration()`），因此三层叠加的存储机制
/// 不必改动就能带上属性条件。声明长这样：
///
/// ```
/// # 大小与时间
/// size-min 10MB
/// size-max 100 MB
/// time-relative 最近 7 天          # 或 time-from / time-to
/// attr readonly                    # 要求「只读」置位
/// -attr hidden                     # 要求「隐藏」未置位（前导 - 与掩码语法同义）
/// owner alice, bob
/// -owner root
/// group staff
/// ```
///
/// 键 → 输入框的对应关系由 `attributeConditionTable()` 给出，且**只此一处**：
/// 「未知的键」这条错误提示里的可用键清单是从那张表生成的，
/// 免得提示里写的键与实际认的键分家（那种分家用户完全无法自查）。
///

// -----------------------------------------------------------------------------
// 条件的种类
// -----------------------------------------------------------------------------

/// 属性条件有哪几种。顺序即 `attributeConditionTable()` 的顺序。
enum class AttributeConditionKind
{
    Size,
    TimeRange,
    Attributes,
    Owner,
};

const char *attributeConditionIdentifier(AttributeConditionKind kind);
QString attributeConditionLabel(AttributeConditionKind kind);
QVector<AttributeConditionKind> allAttributeConditions();

/// 某一类条件在表里的下标（0 起）。
int attributeConditionIndex(AttributeConditionKind kind);

// -----------------------------------------------------------------------------
// 配置问题（写错的条件）
// -----------------------------------------------------------------------------

///
/// \brief 出错的是哪个输入控件。
///
/// 与 `MaskRuleError` 的 `column` / `length` 是同一个定位思路：界面要能**就地**
/// 标红。属性条件是一组表单控件而不是一行文本，所以定位落在「哪个控件」上；
/// 从声明文本解析出来的问题另外带上 `line`，两种来源各自能定位。
///
enum class ConditionField
{
    None,
    SizeMin,
    SizeMax,
    TimeFrom,
    TimeTo,
    TimeRelative,
    Attributes,
    Owner,
    Group,
};

const char *conditionFieldIdentifier(ConditionField field);
QString conditionFieldLabel(ConditionField field);

///
/// \brief 一条「用户填错了」的记录。
///
/// 它不是 `MaskRuleError` 那个结构：属性条件的输入是表单，没有「第几列」，
/// 但要回答三个别的问题——哪一类条件、哪个控件、以及**怎么改**。
///
struct ConditionProblem
{
    /// 这条问题属于哪一类条件。`hasCondition` 为假时无意义
    /// （整行级别的问题——「未知的键」——归不到任何一类条件上）。
    AttributeConditionKind condition = AttributeConditionKind::Size;
    bool hasCondition = true;

    /// 声明文本里的原始键（如 `size-min`）。界面可以用它定位到那一行。
    QString key;

    ConditionField field = ConditionField::None;
    int line = -1;   ///< 声明文本里的行号（0 起）；-1 表示不是从文本来的
    int column = -1; ///< 该行里值部分的起始列；-1 表示与位置无关
    QString message;
    QString hint;    ///< 怎么改（可为空）

    /// 「大小 / 第 2 行 · 大小下限：下限 100 MB 大于上限 10 MB（把两者对调，或清空其中一个）」
    QString describe() const;
};

///
/// \brief 把一个声明键映射到它所属的条件。
///
/// 认不出来的键返回 false——「未知的键」这条错误提示里的**可用键清单**也由
/// 同一张表生成，因此提示里写的键与实际认的键不可能分家。
///
bool attributeConditionKindForKey(const QString &key, AttributeConditionKind *kind);

/// 把一组问题拼成多行文本（界面一次性提示、诊断包都用它）。
QString describeConditionProblems(const QVector<ConditionProblem> &problems);

// -----------------------------------------------------------------------------
// 被过滤的条目：元数据
// -----------------------------------------------------------------------------

///
/// \brief 条目属性位。四个取值都是 Windows 的 DOS 属性，在 Unix 上另有来源。
///
/// 每一条在 Unix / Windows 上的**可得性**都不一样，这一点是本模块的元数据
/// 必须区分「值为假」与「不知道」的根本原因：
///
/// | 属性 | Windows | macOS / Linux |
/// | --- | --- | --- |
/// | 只读 | 文件属性位 | 只能近似（属主写权限位），调用方自己决定要不要给 |
/// | 隐藏 | 文件属性位 | 名字以 `.` 开头（约定） |
/// | 系统 | 文件属性位 | **不存在这个概念** |
/// | 归档 | 文件属性位 | **不存在这个概念**（备份位是另一回事） |
///
enum class EntryAttribute
{
    ReadOnly,
    Hidden,
    System,
    Archive,
};

QVector<EntryAttribute> allEntryAttributes();
const char *entryAttributeIdentifier(EntryAttribute attribute);
QString entryAttributeLabel(EntryAttribute attribute);

/// 从标识（`readonly` / `read-only` / `ro`）或中文标签（`只读`）解析属性。
/// 认不出来返回 false，调用方据此报「未知的属性名」并列出全部可用名。
bool parseEntryAttribute(const QString &text, EntryAttribute *attribute);

/// 该属性的位。用于 `EntryMetadata` 的「已置位」与「可信」两个掩码。
quint8 entryAttributeBit(EntryAttribute attribute);

///
/// \brief 一个条目的元数据：属性过滤的全部输入。
///
/// **里面没有任何内容字段**，这是刻意的：规格的边界条款要求属性过滤与内容比对
/// 解耦（被属性过滤掉的条目不应被读取内容）。把「元数据」做成一个小结构、
/// 让判定函数只吃它，于是「属性过滤需要读文件内容」在类型上就写不出来——
/// 而不是靠注释约束。`Tests/AttributeFilter` 另有一条源码级护栏盯住这一点。
///
/// 每个字段都带一个「知不知道」的标志位，理由见文件顶部第 1 条。
/// `knownAttributeBits` 是**逐属性**的：macOS 上「是否隐藏」可判定而
/// 「是否系统文件」不可判定，用一个「整体可信」标志表达不了这件事。
///
struct EntryMetadata
{
    MaskSubject subject; ///< 名字与路径：第 4 条要与名称过滤合成

    bool hasSize = false;
    quint64 size = 0;

    bool hasLastModified = false;
    QDateTime lastModified;

    /// 已置位的属性。
    quint8 attributeBits = 0;

    /// 其中**可信**的属性。不在这一位里的属性按「不知道」处理，
    /// 而不是按「未置位」——这两者的差别正是第 1 条约定的由来。
    quint8 knownAttributeBits = 0;

    bool hasOwner = false;
    QString owner;

    bool hasGroup = false;
    QString group;

    // --- 构造辅助（可链式调用，读起来就是「这个条目长这样」） ------------------

    static EntryMetadata forName(const QString &name);
    static EntryMetadata forPath(const QString &path);

    EntryMetadata &withName(const QString &name);
    EntryMetadata &withSubject(const MaskSubject &value);
    EntryMetadata &withSize(quint64 bytes);
    EntryMetadata &withLastModified(const QDateTime &lastModified);
    EntryMetadata &withOwner(const QString &owner);
    EntryMetadata &withGroup(const QString &group);

    /// 标记某个属性可信，并按 `set` 决定它是否置位。
    EntryMetadata &withAttribute(EntryAttribute attribute, bool set = true);

    /// 标记某个属性可信但**未置位**（等价于 `withAttribute(a, false)`，读起来更清楚）。
    EntryMetadata &withoutAttribute(EntryAttribute attribute);

    bool knowsAttribute(EntryAttribute attribute) const;
    bool hasAttribute(EntryAttribute attribute) const;

    QString describe() const;
};

// -----------------------------------------------------------------------------
// 判定结果
// -----------------------------------------------------------------------------

///
/// \brief 单个条件的判定：三态（通过 / 拒绝 / 不知道）+ 一句依据。
///
/// `evaluated == false` 的两种来源刻意合成同一个出口，因为界面上的处置完全相同
/// （显示「这一项没有生效」）：
///   * 元数据缺失（本平台判不出来、或这一条根本没有这个信息）；
///   * 条件本身没生效（未启用、没填内容、或配置写错已报错）。
///
struct ConditionOutcome
{
    bool accepted = true;
    bool evaluated = false;
    QString reason;

    /// 判定通过且确实判过。
    bool passed() const { return accepted && evaluated; }

    /// 因为判不了而放行。
    bool undecided() const { return accepted && !evaluated; }

    QString describe() const;
};

///
/// \brief 属性过滤对一个条目的总结论。
///
/// 结构与 `LayeredFilterDecision` 对齐（结论 + 依据 + 明细），因为界面要回答的是
/// 同一个问题：「我为什么看不到这个文件」。只给一个 `bool` 的话，
/// 用户只会去改一个本来就没错的条件。
///
struct AttributeDecision
{
    bool accepted = true;

    /// 有没有任何一条条件真的参与了判定（判过，或因为判不了而放行都算）。
    /// 全都没参与时结论恒为「保留」，且 `deciding` 无意义。
    bool anyConditionActive = false;

    /// 第一条拒绝它的条件的机器标识；`accepted` 为真时无意义。
    AttributeConditionKind deciding = AttributeConditionKind::Size;
    QString decidingIdentifier;
    QString reason;

    /// 全部拒绝了它的条件（`deciding` 只报第一条）。
    QVector<AttributeConditionKind> blocking;

    /// 判不了因而放行的条件。界面必须把这一项显示出来——否则用户会以为
    /// 自己设的条件生效了，而实际上它们什么也没做。
    QVector<AttributeConditionKind> undecided;

    /// 配置问题（写错的条件）。**不参与判定**，但必须显示。
    QVector<ConditionProblem> problems;

    /// 逐条件的判定明细，与 `attributeConditionTable()` 同序。
    QVector<ConditionOutcome> outcomes;

    /// 取某一类条件的明细；找不到返回 nullptr。
    const ConditionOutcome *outcomeFor(AttributeConditionKind kind) const;

    bool hasProblems() const { return !problems.isEmpty(); }

    QString describe() const;
};

// -----------------------------------------------------------------------------
// 大小条件
// -----------------------------------------------------------------------------

///
/// \brief 大小文本的解析结果。
///
/// 单位换算**固定按 1024**，与本条目的完成标准（「支持 KB/MB/GB 单位」）以及
/// Windows 资源管理器一致。理由与掩码「分隔符不随平台走」同源：
/// 同一份声明（预设库要导出给团队共享）在不同平台上必须解释成同一个字节数。
/// macOS Finder 显示的是 1000 进制，这是已知的平台差异——将来若要在
/// OPT-* 设置页里提供「按 1000 计算」的开关，那是**设置**，不是平台分支。
///
struct SizeParseResult
{
    quint64 bytes = 0;
    bool ok = false;
    QString problem;
    QString hint;

    QString describe() const;
};

/// 解析一段大小文本：`123`（字节）、`10 MB`、`1.5GB`、`4 kb`、`2 TiB`。
/// 空文本不是错误（表示「这个界不用」），由调用方判断要不要解析。
SizeParseResult parseSizeText(const QString &text);

/// 人能读的大小，如 `1.5 KB`、`10 MB`、`0 字节`（单位 1024 进制）。
QString formatSizeText(quint64 bytes);

/// 人能读的大小 + 精确字节数，如 `1.5 KB（1536 字节）`。用于判定依据与面板。
QString formatSizeExactText(quint64 bytes);

///
/// \brief 大小范围条件（完成标准第 1 条）。
///
/// 上下界**各自独立**可以缺省：只填下限是「不小于」，只填上限是「不大于」，
/// 两个都不填就是不生效。上下界也各自独立解析失败——`10MB` 写对而
/// `一亿` 写错时，下限照常生效、上限被丢掉并报错（同顶部约定第 2 条）。
///
class SizeCondition
{
public:
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    ///
    /// \brief 设置上下界的**原文**并立即重新解析。
    ///
    /// 原文与解析结果一起留着：输入框要回显用户敲的东西（包括写错的那一段），
    /// 而判定要用解析出来的字节数。setter 里直接解析，因此不存在
    /// 「改了原文忘了重新解析」——那种错误的表现是「界面上写的是新规则、
    /// 过滤用的是旧的」，而且两边看起来都对。
    ///
    void setRangeText(const QString &minText, const QString &maxText);

    QString minText() const { return m_minText; }
    QString maxText() const { return m_maxText; }

    bool hasMinimum() const { return m_hasMin; }
    bool hasMaximum() const { return m_hasMax; }
    quint64 minimumBytes() const { return m_min; }
    quint64 maximumBytes() const { return m_max; }

    /// 用户填了内容（哪怕写错了）。用于界面显示「这一项设置过」。
    bool isConstrained() const { return !m_minText.trimmed().isEmpty() || !m_maxText.trimmed().isEmpty(); }

    ///
    /// \brief 真正参与判定：启用，且至少有一个**可用的**界。
    ///
    /// 一界写错不牵连另一界（`size-min 10MB` 配一个认不出的上限时，
    /// 下限照常收窄），与「一行写错只丢那一行」是同一条纪律。
    /// 唯一牵连两边的是「下限大于上限」——那是自相矛盾，此时两个界一起失效，
    /// 并报一条问题（见 `reparse()`）。
    ///
    bool isActive() const;

    QVector<ConditionProblem> problems() const { return m_problems; }

    /// 判定一个条目。`sizeKnown` 为假时按「不知道」放行。
    ConditionOutcome accepts(quint64 size, bool sizeKnown) const;

    /// 「10 MB ～ 100 MB」/「≥ 10 MB」/「≤ 0 字节」/ 空串。
    QString describeRange() const;

    QString describe() const;

private:
    void reparse();

    bool m_enabled = false;
    QString m_minText;
    QString m_maxText;
    bool m_hasMin = false;
    bool m_hasMax = false;
    quint64 m_min = 0;
    quint64 m_max = 0;
    QVector<ConditionProblem> m_problems;
};

// -----------------------------------------------------------------------------
// 修改时间条件
// -----------------------------------------------------------------------------

/// 相对时间区间的基准。
enum class TimeRangeKind
{
    Absolute, ///< 绝对区间（两个时刻，各自可缺省）
    Relative, ///< 相对「现在」的滚动窗口，如「最近 7 天」
};

const char *timeRangeKindIdentifier(TimeRangeKind kind);
QString timeRangeKindLabel(TimeRangeKind kind);

struct DateTimeParseResult
{
    QDateTime value;
    bool ok = false;

    ///
    /// \brief 输入的文本里**只有日期**（没有钟点）。
    ///
    /// 这一个标志有一个具体的用处：它决定「日期」在**上限**上被解释成哪一刻。
    /// 用户把结束时间填成 `2026-09-10` 时，他要的是「含 09-10 这一整天」，
    /// 而不是「到 09-10 00:00 为止」——后者会让当天修改的文件一个都不出现，
    /// 而用户填的日期看起来完全正确（见 `TimeCondition::reparseAbsolute()`）。
    /// 下限不受影响：`2026-09-10` 作为起点就是当天 00:00。
    ///
    bool dateOnly = false;

    QString problem;
    QString hint;

    QString describe() const;
};

/// 解析一个时刻：`2026-09-01`、`2026-09-01 18:30`、`2026-09-01 18:30:45`、
/// `2026/09/01`、`2026年9月1日`。日期缺时间时按当天 00:00:00（本地时区）。
DateTimeParseResult parseDateTimeText(const QString &text);

struct RelativeDaysParseResult
{
    int days = 0;
    bool ok = false;
    QString problem;
    QString hint;

    QString describe() const;
};

/// 解析相对天数：`7`、`7天`、`最近 7 天`、`近7日`、`7d`、`7 days`。
RelativeDaysParseResult parseRelativeDaysText(const QString &text);

///
/// \brief 修改时间范围条件（完成标准第 2 条）。
///
/// ## 「最近 N 天」是滚动窗口，不是自然日
///
/// 窗口是 `[现在 - N×24 小时, 现在]`，端点都含。选滚动而不是「最近 N 个自然日」
/// 的理由：自然日要引入时区与「今天从几点开始」的概念，而它的表现方式是
/// 「昨天还好好的，今天早上一开就少了几个文件」——用户无法把这件事归因到
/// 任何一次操作上。滚动窗口与 `find -mtime` 的直觉也一致。
///
/// **上界是「现在」**，因此修改时间在未来的条目（时钟不准、或者刚解压出来的
/// 归档文件）不在窗口内。这是刻意选的：用户说「最近 7 天」时想的是「过去」，
/// 把时间戳错到 2030 年的文件算进「最近」才更难解释。
///
/// 窗口一旦算出来就能被显示（`describeWindow()`），所以上面这两条选择在界面上
/// 是**看得见**的——用户不必去猜程序怎么理解「最近 7 天」。
///
class TimeCondition
{
public:
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    TimeRangeKind kind() const { return m_kind; }
    void setKind(TimeRangeKind kind);

    void setAbsoluteText(const QString &fromText, const QString &toText);
    QString fromText() const { return m_fromText; }
    QString toText() const { return m_toText; }

    void setRelativeText(const QString &text);
    void setRelativeDays(int days);
    QString relativeText() const { return m_relativeText; }
    int relativeDays() const { return m_days; }

    /// 「现在」。本模块从不自己取系统时钟，见文件顶部约定第 3 条。
    /// 默认是**无效**的 `QDateTime`，因此相对条件在被显式设置之前不会生效
    /// ——默认取当前时刻的话，忘记设置的地方会静默用上一个「看起来对」的时间。
    QDateTime referenceTime() const { return m_reference; }
    void setReferenceTime(const QDateTime &now) { m_reference = now; }

    bool hasLowerBound() const;
    bool hasUpperBound() const;

    /// 左端点；相对区间由 `referenceTime()` 推出来（未设置时返回无效值）。
    QDateTime lowerBound() const;
    QDateTime upperBound() const;

    bool isConstrained() const;
    bool isActive() const;

    QVector<ConditionProblem> problems() const;

    ConditionOutcome accepts(const QDateTime &lastModified, bool known) const;

    /// 「2026-09-01 00:00 ～ 2026-09-20 19:00」/ 「最近 7 天：…」/ 空串。
    QString describeWindow() const;

    QString describe() const;

private:
    void reparseAbsolute();
    void reparseRelative();

    bool m_enabled = false;
    TimeRangeKind m_kind = TimeRangeKind::Absolute;
    QString m_fromText;
    QString m_toText;
    QString m_relativeText;
    int m_days = 0;
    QDateTime m_from;
    QDateTime m_to;
    QDateTime m_reference;
    QVector<ConditionProblem> m_problems;
};

// -----------------------------------------------------------------------------
// 属性位条件
// -----------------------------------------------------------------------------

///
/// \brief 对某一个属性位的要求：**三态**，不是布尔。
///
/// 为什么不能只做「勾选 = 要求置位」的一态：`-attr hidden`（「别给我看隐藏文件」）
/// 是极常见的用法，一态列表表达不了它。做成三态之后，
/// 「不限制 / 必须置位 / 必须未置位」三种意图各有一个取值，
/// 界面上的三态复选框与它一一对应。
///
enum class Requirement
{
    Ignore,
    Required,
    Forbidden,
};

const char *requirementIdentifier(Requirement requirement);
QString requirementLabel(Requirement requirement);

///
/// \brief 属性位条件（完成标准第 3 条的前一半）。
///
/// 判定只吃 `EntryMetadata` 的**可信**属性位：不可信的属性让这一条进入
/// 「不知道」而不是「未置位」（见文件顶部约定第 1 条）。
///
class AttributeBitsCondition
{
public:
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    Requirement requirement(EntryAttribute attribute) const;
    void setRequirement(EntryAttribute attribute, Requirement requirement);

    /// 要求置位的属性，按 `allEntryAttributes()` 顺序。
    QVector<EntryAttribute> requiredAttributes() const;

    /// 要求未置位的属性。
    QVector<EntryAttribute> forbiddenAttributes() const;

    bool isConstrained() const;
    bool isActive() const;

    ConditionOutcome accepts(const EntryMetadata &entry) const;

    /// 「只读=置位、隐藏=未置位」/「（未设置）」。
    QString describe() const;

private:
    bool m_enabled = false;
    Requirement m_requirements[4] = {Requirement::Ignore, Requirement::Ignore,
                                     Requirement::Ignore, Requirement::Ignore};
};

// -----------------------------------------------------------------------------
// 所有者 / 组条件
// -----------------------------------------------------------------------------

/// 名单的语义：命中其中任意一个就通过，还是命中其中任意一个就排除。
enum class ListMatchMode
{
    AnyOf,
    NoneOf,
};

const char *listMatchModeIdentifier(ListMatchMode mode);
QString listMatchModeLabel(ListMatchMode mode);

/// 把一段用户输入切成名字清单（分隔符：逗号、分号、空白），去掉空项。
QStringList splitNameListText(const QString &text);

///
/// \brief 所有者 / 组条件（完成标准第 3 条的后一半，Unix 才有意义）。
///
/// 比较的**大小写敏感性跟随 `MaskPlatform`**：Unix 的账号名区分大小写，
/// Windows 的账号名（`DOMAIN\user`）不区分。做成显式参数而不是 `#ifdef`，
/// 与掩码那边同一条理由——写进 `#ifdef` 之后，另一种平台的分支在开发机上
/// 永远不被执行，也就永远测不到。
///
/// `NoneOf`（「排除 root 的文件」）在所有者未知时同样进「不知道」而不是通过：
/// 「不在名单里」这个结论需要先知道所有者是谁。
///
class OwnerCondition
{
public:
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    QStringList owners() const { return m_owners; }
    void setOwners(const QStringList &owners) { m_owners = owners; }

    QStringList groups() const { return m_groups; }
    void setGroups(const QStringList &groups) { m_groups = groups; }

    ListMatchMode ownerMode() const { return m_ownerMode; }
    void setOwnerMode(ListMatchMode mode) { m_ownerMode = mode; }

    ListMatchMode groupMode() const { return m_groupMode; }
    void setGroupMode(ListMatchMode mode) { m_groupMode = mode; }

    MaskPlatform platform() const { return m_platform; }
    void setPlatform(MaskPlatform platform) { m_platform = platform; }

    bool isConstrained() const { return !m_owners.isEmpty() || !m_groups.isEmpty(); }
    bool isActive() const { return m_enabled && isConstrained(); }

    ConditionOutcome accepts(const EntryMetadata &entry) const;

    QString describe() const;

private:
    bool m_enabled = false;
    QStringList m_owners;
    QStringList m_groups;
    ListMatchMode m_ownerMode = ListMatchMode::AnyOf;
    ListMatchMode m_groupMode = ListMatchMode::AnyOf;
    MaskPlatform m_platform = MaskPlatform::Posix;
};

// -----------------------------------------------------------------------------
// 四类条件合起来
// -----------------------------------------------------------------------------

// 结果结构体按值持有 AttributeFilter，因此要先于它声明、后于它定义。
struct AttributeFilterParseResult;

///
/// \brief 属性过滤：四类条件的**与**（完成标准第 4 条的前半句）。
///
/// 条件顺序、驱动循环与逐条明细全部来自 `attributeConditionTable()`，
/// 因此「表里排第一的是哪一条」与「面板上第一条是哪一个」不可能是两件事。
///
/// 四个成员是公开的（它们只是一组输入），但**取值都走各自的 setter**：
/// 大小与时间的 setter 里立即解析，于是不存在「改了原文没重新解析」。
///
struct AttributeFilter
{
    SizeCondition size;
    TimeCondition timeRange;
    AttributeBitsCondition attributeBits;
    OwnerCondition owner;

    /// 「现在」统一从这里下发（相对时间条件要用）。见文件顶部约定第 3 条。
    void setReferenceTime(const QDateTime &now);
    QDateTime referenceTime() const { return timeRange.referenceTime(); }

    /// 平台只影响两处：所有者/组比较的大小写。下发给 `owner`。
    void setPlatform(MaskPlatform platform);
    MaskPlatform platform() const { return owner.platform(); }

    /// 至少一条条件真正生效（启用 + 有内容 + 不自相矛盾）。
    bool isActive() const;

    /// 生效的条件，按表序。
    QVector<AttributeConditionKind> activeConditions() const;

    /// 四类条件自己的配置问题汇总，按表序。
    QVector<ConditionProblem> problems() const;

    AttributeDecision decide(const EntryMetadata &entry) const;

    bool accepts(const EntryMetadata &entry) const;

    QString describe() const;

    // --- 声明文本 -------------------------------------------------------------

    /// 把当前条件写成声明文本（未设置的条件不出现；启用状态**不写进去**，
    /// 与 FILT-005 把「每层是否启用」留给界面同一条理由）。
    QString toDeclarationText() const;

    ///
    /// \brief 解析一份属性过滤声明。
    ///
    /// 语法错误的行被丢掉并报错，其余照常生效（同顶部约定第 2 条）。
    /// 解析成功后**带约束的条件会被置为启用**，否则用户在界面里敲完声明、
    /// 条件却因为复选框没勾而不生效，而界面上看不出任何异常。
    ///
    static AttributeFilterParseResult parseDeclaration(
        const QString &text, MaskPlatform platform = currentMaskPlatform());
};

struct AttributeFilterParseResult
{
    AttributeFilter filter;
    QVector<ConditionProblem> problems;

    bool ok() const { return problems.isEmpty(); }

    /// 多行文本（界面的一次性提示用）。
    QString describeProblems() const;
};

/// 属性条件的设置键。与会话设置里的名称声明（`filterDeclarationSettingKey()`）
/// 是**两个键**：它们住在同一个存储里但是两份不同的数据，
/// 共用一个键会让 `toDeclarationText()` 把对方的行当成「未知的键」报错。
QString attributeFilterDeclarationKey();

// -----------------------------------------------------------------------------
// 与名称过滤的合成（完成标准第 4 条的后半句）
// -----------------------------------------------------------------------------

///
/// \brief 名称过滤 + 属性过滤的总结论。
///
/// 名称侧的三个字段把两种来源都装得下：三层栈（`hasDecidingLayer` 为真，
/// 带上层与规则）与单个 `MaskFilter`（只有规则）。界面要显示的东西完全一样。
///
struct EntryFilterDecision
{
    bool nameAccepted = true;
    MaskVerdict nameVerdict = MaskVerdict::Included;

    bool hasDecidingLayer = false;
    FilterLayer decidingLayer = FilterLayer::Format;
    int ruleIndex = -1;
    QString ruleText;

    AttributeDecision attributes;

    /// **整体结论 = 名称侧与属性侧的与**。这一位是第 4 条的全部要点。
    bool accepted = true;

    /// 哪一侧挡下的它（两侧都可能挡下，用两个标志分别记）。
    bool blockedByName() const { return !nameAccepted; }
    bool blockedByAttributes() const { return !attributes.accepted; }

    QString describe() const;
};

/// 三层名称过滤 × 属性过滤（生产路径）。
EntryFilterDecision decideEntry(const FilterStack &names, const AttributeFilter &attributes,
                                const EntryMetadata &entry);

/// 单个名称过滤 × 属性过滤（给不用三层的地方，如命令行与测试）。
EntryFilterDecision decideEntry(const MaskFilter &names, const AttributeFilter &attributes,
                                const EntryMetadata &entry);

// -----------------------------------------------------------------------------
// 条件表与启动自检
// -----------------------------------------------------------------------------

///
/// \brief 一类条件的描述：它叫什么、声明里认哪些键、需要什么输入。
///
/// 表是**唯一的事实来源**：`allAttributeConditions()` 的顺序、声明解析认的键、
/// 「未知的键」这条错误提示里的可用键清单、以及 `AttributeDecision::outcomes`
/// 的顺序全部从它推导。
///
struct AttributeConditionDescriptor
{
    AttributeConditionKind kind = AttributeConditionKind::Size;
    QString identifier;          ///< 机器可读，同时是声明文本里的键前缀
    QString label;               ///< 中文标签（面板与错误提示用）
    QStringList declarationKeys; ///< 声明里认的**全部**键（含 `-` 前缀的那些）
    bool needsReferenceTime = false; ///< 是否需要「现在」（相对时间）
    bool needsPlatform = false;      ///< 是否随平台变化（所有者/组的大小写）

    /// 只吃元数据、从不读条目内容。**这一条必须恒为真**：
    /// 规格的边界条款要求属性过滤与内容比对解耦，启动自检会盯住它。
    bool metadataOnly = true;

    /// 输入提示（界面的占位文本），如「如 10 MB、1.5 GB」。
    QString unitHint;
};

/// 条件表本身（顺序即判定与面板顺序）。
const QVector<AttributeConditionDescriptor> &attributeConditionTable();

/// 取某一类条件的描述；找不到返回 nullptr。
const AttributeConditionDescriptor *attributeConditionDescriptor(AttributeConditionKind kind);

////
/// \brief 条件表的自检（启动时跑一次）。
///
/// 与 `validateFilterLayerTable()` 同一个定位：查「手写那张表时容易写错、
/// 写错了也不影响别的」的几件事——标识重复、声明键重复、有条件的
/// `metadataOnly` 被改成假（那会破坏「属性过滤不读内容」这条边界）。
///
/// 表当参数传进来，因此测试能拿一份**故意写坏**的表跑同一个判定，
/// 证明它真的会报（一条永远不会红的护栏比没有护栏更糟）。
///
/// `expectedConditions` 是规格点名要求的条件清单。**新增一类条件时要改它**——
/// 它是唯一不依赖这张表自身的期望值。
///
QStringList validateAttributeConditionTable(
    const QVector<AttributeConditionDescriptor> &table,
    const QVector<AttributeConditionKind> &expectedConditions = {
        AttributeConditionKind::Size,
        AttributeConditionKind::TimeRange,
        AttributeConditionKind::Attributes,
        AttributeConditionKind::Owner});

} // namespace Filter
} // namespace LqCompare

#endif // LQCOMPARE_ATTRIBUTEFILTER_H
