#ifndef LQCOMPARE_TEXTDIFF_H
#define LQCOMPARE_TEXTDIFF_H

#include "textdocument.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare { namespace Text {

enum class Change { Equal, Insert, Delete, Replace, Ignored };
enum class Whitespace { Exact, IgnoreChanges, IgnoreAll };

///
/// \brief 对齐算法（PRD: TXT-003）。
///
/// 枚举顺序**既**不代表优劣，也不代表界面下拉的顺序——界面上怎么排、叫什么名字、
/// 存到哪个设置键，都是 TXT-004 的决定。这里只回答一个问题：引擎会算哪几种。
///
/// 为什么不复用 `Change`/`Whitespace` 那种「一个枚举配一张表」的老写法：
/// 这两种算法的**输出契约不同**（Myers 求最短编辑脚本，Patience 求「唯一行优先」），
/// 因此「哪个是默认」不能靠枚举下标猜，必须由 `defaultAlignment()` 显式回答。
///
enum class Alignment
{
    Myers,    ///< 最短编辑脚本。历史行为，也是 Patience 的回退底座
    Patience, ///< 唯一行优先。重复行多的文件上，块边界更贴合人对「搬动了几块」的直觉
};

struct CompareOptions {
    bool ignoreCase = false;
    Whitespace whitespace = Whitespace::Exact;
    bool ignoreEol = true;
    bool ignoreFinalNewline = false;
    // ⚠ 新字段一律加在**末尾**。
    // 这个结构体的使用方式主要是「先默认构造、再逐个字段赋值」（视图、命令行、
    // 脚本引擎、报表都是如此），但也确实被按位置初始化过。往中间插一个字段，
    // 被拉开的那个位置不会报错，只会悄悄换一个含义——史上最容易漏的一种错。
    Alignment alignment = Alignment::Myers;
};
struct Block {
    Change change = Change::Equal;
    int leftStart = 0;
    int leftCount = 0;
    int rightStart = 0;
    int rightCount = 0;
    int firstRow = 0;
    int rowCount = 0;
};
struct Row {
    int leftLine = -1;
    int rightLine = -1;
    int block = -1;
    Change change = Change::Equal;
};
struct Result {
    QVector<Block> blocks;
    QVector<Row> rows;
    QVector<int> differences; // indexes into blocks; ignored changes are excluded
    int ignoredBlocks = 0;
    bool alignmentLimited = false;
};

///
/// \brief 对齐两侧的行。
///
/// `options.alignment` 选算法。两种算法产出**同一套** `Result`
/// （相同的块类型、相同的行覆盖、相同的 `differences` / `ignoredBlocks` /
/// `alignmentLimited` 语义），因此切换算法不会改变视图契约——视图不该知道
/// 自己拿到的是哪一种算法的结果。
///
/// Myers 是确定性的二分 Myers，线性辅助空间，对抗性输入的工作量有界；用尽预算的
/// 那一段是**显式替换**，绝不静默当成相同。Patience 先按「两侧都只出现一次的行」
/// 找锚点，锚点之间递归，遇到没有唯一行的一段就回退到 Myers——回退是常规路径
/// 而不是异常路径（见 textdiff.cpp 顶部）。
///
Result compare(const QVector<Line> &left, const QVector<Line> &right,
               const CompareOptions &options = CompareOptions());

// -----------------------------------------------------------------------------
// 已实现的对齐算法清单（TXT-003 第 3 条）
// -----------------------------------------------------------------------------

///
/// \brief 算法表的一行。
///
/// 表是**唯一的事实来源**：`availableAlignments()` 的集合、`defaultAlignment()`
/// 的取值、以及 `validateAlignmentTable()` 的判定全部从它推导。分开各写一遍的话，
/// 加第三种算法时就会出现「下拉里有三项、默认值指向第二项、而第二项其实没实现」
/// 这类错位，而现象是「选了某个算法，结果和另一个一模一样」。
///
struct AlignmentDescriptor
{
    Alignment alignment = Alignment::Myers;
    /// 机器可读标识（日志、快照、将来的设置键都用它，不靠枚举序号）。
    const char *identifier = "";
    /// 引擎是否真的会算它。
    /// **`false` 的条目绝不会出现在 `availableAlignments()` 里**——这就是
    /// 完成标准第 3 条那句「未实现算法不得可选」的可执行形式。
    bool implemented = false;
};

/// 算法表本身（顺序即 `availableAlignments()` 的返回顺序）。
const QVector<AlignmentDescriptor> &alignmentTable();

/// 机器可读标识。表里查不到时返回空指针，**不编一个假名字**——
/// 编出来的标识符只会让日志里的人去搜一个不存在的符号。
const char *alignmentIdentifier(Alignment alignment);

/// 默认算法：表里**第一条已实现**的条目。一条都没有时退回 `Alignment::Myers`
/// （Myers 是唯一不依赖任何别的东西就能算完的算法，也是历史行为）。
Alignment defaultAlignment(const QVector<AlignmentDescriptor> &table = alignmentTable());

/// 可选算法：只含 `implemented == true` 的条目，顺序与表一致。
QVector<Alignment> availableAlignments(const QVector<AlignmentDescriptor> &table = alignmentTable());

///
/// \brief 算法表的自检。
///
/// 与 `validateFilterLayerTable()` 同一个定位：查「手写那张表时容易写错、
/// 写错了也不影响别的」的几件事。返回**可直接写进日志**的问题清单，
/// 空列表表示干净。调用方目前只有测试与手工诊断——本条（TXT-003）不加启动自检，
/// 理由是这张表只有两行、且它的错误全都表现为「清单里少了一项」，
/// 加一处启动日志的收益低于它的噪声（与 `architecture.md` §5 对另外两处自检的
/// 判断一致）。
///
/// 为什么要参数化：一条**永远不会红**的护栏比没有护栏更糟。表当参数传进来之后，
/// 测试可以拿一份故意写坏的表跑同一个判定，证明它真的会报。
///
/// `expected` 是规格点名要求的算法清单，也是唯一**不依赖这张表自身**的期望值——
/// 表里漏登记第二种算法时，别的规则一条都不会响。
///
QStringList validateAlignmentTable(const QVector<AlignmentDescriptor> &table,
                                   const QVector<Alignment> &expected = {Alignment::Myers,
                                                                        Alignment::Patience});

} }
#endif
