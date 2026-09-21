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
// 行内容规范化链（TXT-008 第 3、4 条）
// -----------------------------------------------------------------------------

///
/// \brief 把一行原文规范化成「判等用的键」——整条规范化链的唯一实现。
///
/// `compare()` 的两侧键都从这里取。之所以把它从匿名命名空间提到公开接口上，
/// 不是为了多一个可调用点，而是因为「规范化链」这件事需要一个**可以被指着说的名字**：
/// 完成标准第 3 条要求「与忽略空白等规则按统一规范化链叠加」，第 4 条要求
/// 「土耳其语 i/İ 等特例有明确取舍并记录」——取舍记录在下面，而它是否真的生效，
/// 只有让测试能直接问这条链才验得了。
///
/// 三条必须记住的契约：
///
/// 1. **两侧都要过完整条链，然后才判等。** 不允许「先按大小写比一次、不相等再按空白比
///    一次」这种短路写法——它会让「同一行同时有大小写和空白变化」被判成不同，
///    而规格里它应当是相同的。链只吃单行文本、**不读另一侧**，所以对称性是结构性的。
///
/// 2. **大小写走 Unicode simple case folding，既不等于小写化、也不跟着进程的
///    locale 走。** 选 `QString::toCaseFolded()` 而不是 `QString::toLower()`
///    的理由不是风格：`toLower()` 会把希腊语词尾 sigma 留成 `ς`、却把 `Σ`
///    变成 `σ`（本机 Qt 5.15.2 实测 `toLower("ΟΔΟΣ")` = `οδοσ`、`toLower("οδος")`
///    = `οδος`），于是 `ΟΔΟΣ` 与 `οδος` 被报成不同——同一段希腊文只要大小写差异
///    落在词尾就永远忽略不掉。`toCaseFolded()` 把 `Σ`/`σ`/`ς` 一并折成 `σ`，
///    这才是「判等」要的语义。
///
/// 3. **折叠结果与 `QLocale` 无关**（实测：`QLocale::setDefault(Turkish)`
///    不改变 `toCaseFolded()` 的任何结果）。这是刻意的：同一对文件在两台 locale
///    不同的机器上必须给出同一个答案，否则 CI 与本机会各说各话。
///
/// **土耳其语 `i`/`İ` 的取舍（第 4 条要求记录的正是这一条）**：土耳其语正字法里
/// `I`↔`ı`(U+0131)、`İ`(U+0130)↔`i` 互为大小写。我们选与 locale 无关的 Unicode 折叠，
/// 它给出的结论**恰好与土耳其语相反**：
///
/// | 输入对 | 土耳其语规则 | 本实现 |
/// | --- | --- | --- |
/// | `I` / `i` | 不同 | **相同** |
/// | `İ`(U+0130) / `i` | 相同 | **不同** |
/// | `ı`(U+0131) / `i` | 不同 | 不同 |
/// | `ı`(U+0131) / `I` | 相同 | **不同** |
///
/// 代价因此是明确的：一份土耳其语文档里 `I` 与 `ı` 会被报成差异。接受它的理由是
/// 上面第 3 条——按土耳其语折叠，结果就取决于跑在哪台机器上，而 `QString` 没有任何
/// API 能把「这份文档是土耳其语」这件事带进来。一句话：**宁可对土耳其语多报几个
/// 差异，也不要让同一对文件的结论随机器变。**
///
/// **另一条已知限制**：Qt 5.15.2 的 `toCaseFolded()` 是 **simple** folding，
/// 不做多字符展开——`ß` 折成 `ß`（不是 `ss`）、`ﬁ`(U+FB01) 折成 `ﬁ`（不是 `ffi`）、
/// `İ`(U+0130) 折成它自己（不是 `i`+U+0307）。于是 `STRASSE` 与 `straße` 仍被判为不同。
/// 不做展开换来的是**长度守恒**：折叠前后 `QChar` 个数相同（实测：BMP 逐码位 1:1，
/// 1189 个码位会变、0 个长度变化；非 BMP 的代理对仍是两个 `QChar`，如
/// Deseret U+10400→U+10428）。于是规范化后的下标可以直接当原文下标用——
/// TXT-025 的行内字符级高亮正依赖这一点。**若哪天改成 full folding，
/// 字符级高亮必须先做偏移映射，否则整段高亮会错位**，不要只改这一处就收工。
///
/// **空白侧的三个模式（TXT-009）**，语义必须严格区分——这是规格的边界条款：
///
/// | 模式 | 判据 | `a  b` vs `a b` | `ab` vs `a b` | `a\tb` vs `a b` |
/// | --- | --- | --- | --- | --- |
/// | `Exact` | 逐字符比 | 不同 | 不同 | 不同 |
/// | `IgnoreChanges` | 内部连续空白折叠成一个空格、首尾去掉 | **相同** | 不同 | **相同** |
/// | `IgnoreAll` | 删掉全部空白字符 | **相同** | **相同** | **相同** |
///
/// 一句话记住区别：**`IgnoreChanges` 只管白空的「数量」，`IgnoreAll` 连「有没有」也不管**。
/// 两者有一处容易搞反：`IgnoreChanges` 下 `ab` 与 `a b` **仍然不同**
/// （因为折叠后的 `a b` 里那个空格还在），这正是第 1 条与第 2 条的分界。
///
/// 「空白」按 `QChar::isSpace()` 判定，因此**不止 ASCII 空格与 Tab**：实测
/// U+00A0（NBSP）、U+3000（表意空格）、U+202F（窄 NBSP）、U+2028、U+2029 都算，
/// 而 U+200B（零宽空格）**不算**。U+00A0 被当成可忽略的空白是刻意的取舍：
/// 它在排版上确实是空白，把它排除掉会让「从别的编辑器粘贴来的行」比出一堆
/// 看不见的差异。要保留 NBSP 的区分度得靠替换规则（TXT-012），不在这一层做。
///
/// **枚举取值落在表外时按 `Exact` 处理**（不做兜底、不改写成默认模式）。
/// 这与 `Alignment` 那处的兜底方向**刻意相反**，理由也对得上：那里的兜底是必须的，
/// 因为一个「什么都不跑」的对齐算法会返回零个块、也就是「两份文件完全一样」；
/// 而这里的退化只会**多**报差异，绝不会把差异藏掉——同样是坏输入，
/// 一边会静默地骗人，另一边只是啰嗦。**不要**为了「对称」把它改成兜底到默认模式。
///
QString normalizedLine(const QString &text, const CompareOptions &options);

// -----------------------------------------------------------------------------
// 空白模式表（TXT-009 第 3 条）
// -----------------------------------------------------------------------------

///
/// \brief 空白模式表的一行。
///
/// 完成标准第 3 条要的是「以**单一**枚举呈现三个模式、三模式互斥」。枚举本身
/// （`Whitespace`）已经满足「单一」，但「界面拿到的模式」与「枚举」之间原本靠
/// **序号**对应：界面写死三行文案，读回时 `static_cast<Whitespace>(currentIndex())`。
/// 序号对应是一份隐式的第二事实来源——往枚举中间插一个取值、或者调换两条文案，
/// 界面上的「忽略全部空白」会静默变成别的模式，而**没有任何东西会红**。
///
/// 这张表和 `AlignmentDescriptor` 一样是**唯一的事实来源**：界面按它铺下拉、按它读回，
/// 标识符用于日志与（将来的）设置键。缺一个模式、多一个模式、两个模式共用一个标识符，
/// `validateWhitespaceTable()` 都会报出来。
///
struct WhitespaceDescriptor
{
    Whitespace whitespace = Whitespace::Exact;
    /// 机器可读标识（日志、快照、将来的设置键都用它，不靠枚举序号）。
    const char *identifier = "";
    /// 引擎是否真的会算它。`false` 的条目不会出现在 `availableWhitespaces()` 里。
    bool implemented = false;
};

/// 空白模式表本身（顺序即 `availableWhitespaces()` 的返回顺序，也是界面上拉的顺序）。
const QVector<WhitespaceDescriptor> &whitespaceTable();

/// 机器可读标识。表里查不到时返回空指针，**不编一个假名字**。
const char *whitespaceIdentifier(Whitespace whitespace);

/// 默认模式：表里**第一条已实现**的条目；一条都没有时退回 `Whitespace::Exact`。
Whitespace defaultWhitespace(const QVector<WhitespaceDescriptor> &table = whitespaceTable());

/// 可选模式：只含 `implemented == true` 的条目，顺序与表一致。
QVector<Whitespace> availableWhitespaces(const QVector<WhitespaceDescriptor> &table = whitespaceTable());

///
/// \brief 空白模式表的自检。
///
/// 与 `validateAlignmentTable()` 同一个定位：查「手写那张表时容易写错、写错了也不影响
/// 别的」的几件事。返回可直接写进日志的问题清单，空列表表示干净。
///
/// `expected` 是规格点名的三个模式，也是唯一**不依赖这张表自身**的期望值——
/// 表里漏登记一个模式时，别的规则一条都不会响。
///
QStringList validateWhitespaceTable(const QVector<WhitespaceDescriptor> &table,
                                    const QVector<Whitespace> &expected = {Whitespace::Exact,
                                                                           Whitespace::IgnoreChanges,
                                                                           Whitespace::IgnoreAll});

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
