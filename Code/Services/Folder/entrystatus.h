#ifndef LQCOMPARE_ENTRYSTATUS_H
#define LQCOMPARE_ENTRYSTATUS_H

#include "foldercompare.h"

#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Folder {

// ---------------------------------------------------------------------------
// DIR-011 条目状态判定与语义
//
// 这一族条目反复出现的错法是「实现早就在、标准却是裸的」，所以本模块刻意把
// **每一个被规格点名的说法都做成一份可读的数据**（表 + 标识符 + 判据），
// 而不是散落在各处的 switch：散落的 switch 改一处漏一处的代价，
// 在 DIR-012（着色与图标）、DIR-014（排序）、报表与命令行上会各付一遍。
//
// 三个互不相同的维度在这里各有自己的表；**主状态是互斥的，另两维不是**。
// 把它们并进 `Status` 会立刻需要一套优先级规则，而规格的边界正好相反：
// 「存在性、内容结论、时间关系与扫描完整性分别存储」。
// ---------------------------------------------------------------------------

// 存在性：派生视图。**不另存一份**——真正的存储是两侧的 `Side::exists`，
// 再存一个字段就是第二份事实来源，两份迟早分叉。它独立成类型只是为了
// 让界面与报表能用同一套词说话。
enum class Existence { Both, LeftOnly, RightOnly, Neither, Unknown };

QString existenceIdentifier(Existence existence);
QString existenceLabel(Existence existence);
Existence existenceOf(const Entry &entry);

// ---------------------------------------------------------------------------
// 主状态表：互斥集合的唯一事实来源
// ---------------------------------------------------------------------------

struct MainStatusDescriptor
{
    Status value;
    const char *identifier; ///< 稳定机器标识（报表 / 命令行 / 会话文件）
    // 只有拿到**有效基线**才允许出现的取值。这是「两侧均改」不可凭两份当前
    // 文件推断这句话的可执行形式：不是靠注释提醒，而是靠一条会红的用例。
    bool requiresBaseline;
    // 与颜色无关的那一维（DIR-011 第 4 条）：状态图标。颜色之外必须同时
    // 有图标与文本，否则色觉障碍用户拿到的信息量为零。
    //
    // 刻意**不做**「这一档算不算差异」的字段：本仓现在有三套各自合理的口径
    // （导航跳过读取错误、筛选只排掉相同、命令行把未知也算差异），把它们
    // 统一成一份数据是一次独立的行为变更，塞进 DIR-011 会让本条目在没人
    // 要求的地方改变三个消费点的表现。
    const char *iconKey;
};

const QVector<MainStatusDescriptor> &mainStatusTable();
QString statusIdentifier(Status status);
bool statusRequiresBaseline(Status status);
QString statusIconKey(Status status);
// 取值是否落在主状态表里。整数来自模型角色 / 下拉 / 会话文件，越界值必须
// 被当成「未知」而不是让调用方去 switch 的 default 分支里随手兜底。
bool isKnownStatus(int rawValue);
// 表自身的自检。**表是参数**：从模块内部读表会让「故意写坏的表喂进去也永远绿」
// （NameFilter 那一轮踩过，见 handoff §6）。
QStringList validateMainStatusTable(const QVector<MainStatusDescriptor> &table);

// ---------------------------------------------------------------------------
// 内容证据表
// ---------------------------------------------------------------------------

struct ContentEvidenceDescriptor
{
    ContentEvidence value;
    const char *identifier;
    // 这一档是否**证明**了内容相同。`Partial` 明确不算：限内全同不等于相同。
    bool provesContentIdentity;
    // 这一档是否覆盖了全部内容。`NotCompared` 与 `Partial` 都不覆盖。
    bool coversWholeContent;
};

const QVector<ContentEvidenceDescriptor> &contentEvidenceTable();
QString contentEvidenceIdentifier(ContentEvidence evidence);
bool contentEvidenceProvesIdentity(ContentEvidence evidence);
bool contentEvidenceCoversWholeContent(ContentEvidence evidence);
QStringList validateContentEvidenceTable(const QVector<ContentEvidenceDescriptor> &table);

// ---------------------------------------------------------------------------
// 时间关系表
// ---------------------------------------------------------------------------

struct TimeRelationDescriptor
{
    TimeRelation value;
    const char *identifier;
};

const QVector<TimeRelationDescriptor> &timeRelationTable();
QString timeRelationIdentifier(TimeRelation relation);
QString timeRelationLabel(TimeRelation relation);
// 容差的存在理由：FAT 的时间精度是 2 秒，网络盘与虚拟机共享目录还会更差。
// 传 0 表示严格相等。比较的是 UTC 时刻本身（`FileTime` 内部就是 UTC 纳秒），
// 与本地时区、夏令时无关。
TimeRelation compareTimes(const Files::FileTime &left, const Files::FileTime &right,
                          qint64 toleranceMs = 0);
// 「两侧都必须是真实存在的条目」这条前置只在这里说一次。
TimeRelation timeRelationFor(const Entry &entry, qint64 toleranceMs = 0);
QStringList validateTimeRelationTable(const QVector<TimeRelationDescriptor> &table);

// ---------------------------------------------------------------------------
// 孤儿项：仅左 / 仅右的**显示集合**
// ---------------------------------------------------------------------------

bool isOrphan(Status status);
// 供「只显示孤儿项」这类过滤器与报表计数使用。刻意没有 `Status::Orphan`：
// 那会让「仅左」同时属于两个互斥取值。
QVector<Status> orphanStatuses();

// ---------------------------------------------------------------------------
// 基线：两侧均改与冲突的唯一来源（DIR-011 第 3 条）
// ---------------------------------------------------------------------------

// 有效基线的判据。返回空列表即有效。三条缺一不可：
// ① 两侧根目录都绑定了；② 至少有一条祖先记录；③ 祖先记录本身自洽。
QStringList validateBaselineView(const BaselineView &baseline);

// 有基线时把条目细化为「两侧均改」或「冲突」；无有效基线时**一个字段都不动**。
// 返回值表示是否发生了细化。
//
// 判定用的是「两侧相对共同祖先有没有变」：
//   两侧都变、且两侧彼此仍然相同 → `BothChanged`（改法一致，同步无害）
//   两侧都变、且两侧彼此不同   → `Conflict`（必须由人决定）
// 只有一侧变时主状态保持原来的「不同」——那是「右新左旧」这类信息，
// 已经由时间维度与内容证据表达，再抬成主状态就成了第四套说法。
bool applyBaselineStatus(Entry &entry, const BaselineView &baseline);

// ---------------------------------------------------------------------------
// 父子一致（DIR-011 第 5 条）
// ---------------------------------------------------------------------------

struct ChildStatus
{
    Status status = Status::Unknown;
    bool inComparison = true;
};

struct ParentAggregate
{
    Status status = Status::Same;
    bool hasIncludedDescendants = false;
    bool hasExcludedDescendants = false;
};

// 目录状态由已扫描的子条目汇总。抽成独立函数有三个目的：
// ① 它成为「固定数据源与准则下父子视图结论一致」这条标准的**唯一**实现，
//    引擎与用例调用同一份代码，不存在两套口径；
// ② 真值表可以脱离文件系统直接跑；
// ③ 「扫描被取消」在这里收口，不必在每个调用点各写一次。
//
// 形参只有「扫描是否已被取消」这一个「没看完」的开关，这是查证后的结论而不是
// 遗漏：另外两种「没看完」——这一层的内容根本没读（达到递归上限、或递归被关闭）、
// 以及目录根本没权限列——在引擎里都**先在条目自己的行上落成 `Unknown` / `Error`**，
// 再作为子条目进入汇总。汇总看到的已经是「未知」或「错误」，再要一个开关就会
// 得到一个永远为假的参数。规格里写了、代码里却没有出口的字段比没有更坏。
//
// 「空目录」不在此列：空目录的内容真的读过（列表成功且结果为空），
// 所以它就该汇总成「相同」；没读过的目录不会以「相同」的身份进来。
ParentAggregate aggregateChildren(const QVector<ChildStatus> &children,
                                  bool scanStopped = false);

// ---------------------------------------------------------------------------
// 「为什么是这个状态」（DIR-011 第 4 条）
// ---------------------------------------------------------------------------

enum class ReasonKind { Criterion, Override, Conclusion };

struct ReasonLine
{
    ReasonKind kind = ReasonKind::Criterion;
    QString text;
};

// 逐条列出各准则、覆盖策略与最终结论。刻意返回结构化行而不是一段文本：
// 界面要分组显示、报表要按类遍历、命令行要能只印结论那一行。
// `baselineApplied` 来自 `Result::baselineApplied`——本次比较到底有没有用上
// 有效基线；缺了这一句，界面会在一条「两侧均改」的条目上印出「没有提供基线」。
QVector<ReasonLine> statusReasonLines(const Entry &entry, const Options &options,
                                      bool baselineApplied = false);
QString reasonKindLabel(ReasonKind kind);

// ---------------------------------------------------------------------------
// 模型自检：把「不可能出现的组合」写成可执行的判据
// ---------------------------------------------------------------------------

// 返回被违反的不变量清单（空即合规）。**基线是否有效是参数**：从条目本身
// 推不出这件事，而「无基线时不得出现左右均改」恰恰是本条目最要紧的一条。
//
// 覆盖的不变量：
//   ① 需要有基线的取值在没有基线时出现；
//   ② 非目录条目判成「相同」却拿不出证明内容相同的证据（含 `Partial`）；
//   ③ 少了哪一侧却判成「两侧都有」，或两侧都在却判成「仅左 / 仅右」；
//   ④ 报读取错误却同时声称内容已比较完毕；或声称「仅左/仅右」而那一侧报错。
QStringList statusModelViolations(const Entry &entry, bool baselineWasValid);

} // namespace Folder
} // namespace LqCompare

#endif
