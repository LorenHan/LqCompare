#ifndef LQCOMPARE_RECURSIONSTRATEGY_H
#define LQCOMPARE_RECURSIONSTRATEGY_H

#include "foldercompare.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Folder {

// ---------------------------------------------------------------------------
// DIR-003 递归子目录策略
//
// 为什么另开一个「档位」概念而不给 `Options` 加第三个字段：三档在引擎里
// **已经全部可达**——`(recursive, maximumDepth)` 这两个既有字段的组合就能表达
// 「不进子目录」「只进一层」「一路递归」。缺的只是「档位」这个名字与它的控件。
// 再存一个 `RecursionTier` 字段就会让同一件事有两份说法，两份迟早分叉：
// 一份说「只进一层」、另一份的 depthLimit 却是 128，谁也没法判断哪个对。
//
// 所以本模块的定位是**映射表 + 唯一的落点**，而不是新的存储：
//   · `applyRecursionTier()` 是「档位 → 那两个字段」的唯一实现；
//   · `recursionTierOf()` 是反查，界面回填与会话恢复都走它；
//   · `Options` 仍然只有 `recursive` 与 `maximumDepth` 两个字段。
//
// 另一条纪律写在这里：深度上限是**用户另设**的（第 4 条），档位只回答
// 「recursive 这一位是什么」。「不递归 + 深度 7」这种组合是合法的存档值，
// 反查与展示都必须原样保留它——把 7 归一成 0 会让 `.lqc` 存档的往返断言变红
// （`Tests/Folder::savedFolderOptionsRoundTripThroughLqcAndActuallyScan`）。
// ---------------------------------------------------------------------------

enum class RecursionTier {
    DirectChildren, // 仅根目录直属条目（不进入子目录）
    OneLevel,       // 递归深度 1（进入直接子目录）
    Full,           // 完全递归（深度上限由用户另设，见第 4 条）
};

// 「完全递归」档**首次被选中**时写回的深度上限。
//
// 与 `Options::maximumDepth` 的初值**必须一致**，否则「缺省选项」反查出来的档位
// 就不是「完全递归」——那会让下拉在一打开时就显示错档，而这种错在运行期
// 没有任何别的现象。`Tests/Folder` 有一条用例专门钉这个等式（头文件之间
// 互相 include，写不成 `static_assert`，只能靠会红的用例兜住）。
inline constexpr int kDefaultFullDepth = 128;

// 深度上限的合法上界。引擎的 `qBound`、会话设置的校验与界面控件的取值范围
// 三处共用它：分别写死一个 256 就有三份说法，改一处漏两处。
inline constexpr int kMaximumRecursionDepth = 256;

// 「仅根目录直属条目」这一档在 `Options` 里的字段形态。写成具名常量是因为
// 引擎的判定（`depth < maximumDepth`）在 `maximumDepth == 0` 时与 `recursive == false`
// 完全同行为，而这两件事必须在**反查**里被认成同一档（见 `recursionTierOf`）。
inline constexpr int kDirectChildrenDepth = 0;

struct RecursionTierDescriptor
{
    RecursionTier tier;
    // 机器可读标识（设置 / 日志 / 命令行）。**纯 ASCII**：含非 ASCII 的字面量
    // 用 `const char *` 承载会被当成 Latin-1 逐字符解释，比较永远不相等
    // （中文单位那一轮踩过，见 handoff §6）。中文文案一律走 `tr()` 函数。
    const char *identifier;
    bool recursive;
    // 这一档**确定**的深度上限。`Full` 这一行给的是缺省值而不是硬上限：
    // 它只在「用户从别的档切过来」时写回，之后由用户另设（第 4 条）。
    int depthLimit;
};

const QVector<RecursionTierDescriptor> &recursionTierTable();
QString recursionTierIdentifier(RecursionTier tier);
QString recursionTierLabel(RecursionTier tier);
QString recursionTierDescription(RecursionTier tier);
bool recursionTierRecurses(RecursionTier tier);
int recursionTierDepthLimit(RecursionTier tier);

// 档位 → `Options`。**唯一**的映射落点。
//
// `Full` 只把 `recursive` 打开，**不动**用户已经设好的深度上限；但它会把与
// 档位自相矛盾的值（`<= 1`，那在引擎里就是「不递归」或「只进一层」）提到缺省值。
// 少了这一提，`applyRecursionTier(o, Full)` 之后 `recursionTierOf(o)` 可能不是
// `Full` —— 一次「设定为完全递归」的操作得到「不递归」，而两处都在说自己是
// 完全递归。
void applyRecursionTier(Options &options, RecursionTier tier);

// `Options` → 档位（界面回填与会话恢复用）。
//
// 反查按**行为**归类，不按字段原值：`recursive == true` 配 `maximumDepth == 0`
// 在引擎里不展开任何一层，与「不递归」完全同行为，若照原值报成「完全递归」，
// 下拉一打开就显示错档。三条分支互斥且穷尽——反查**总是在**三档里落一格，
// 不允许「认不出来」：认不出来时界面就会出现一个选不中的下拉，用户看到的是
// 「当前档位」那一栏空白。
RecursionTier recursionTierOf(const Options &options);

// 表自检。**表是参数**：从模块内部读表会让「故意写坏的表喂进去也永远绿」
// （NameFilter 那一轮踩过，见 handoff §6）。查五件事：
//   ① 三档齐、且顺序就是规格里点名的顺序；
//   ② 标识符非空、唯一，且是机器可读的（小写字母 / 数字 / 连字符）；
//   ③ 「不递归」与「深度上限为 0」必须同时成立或同时不成立：只写一边时
//      档位与行为就是两份说法；
//   ④ 递归档的深度上限必须为正且不超过 `kMaximumRecursionDepth`；
//   ⑤ `Full` 行的深度上限必须等于 `kDefaultFullDepth`（否则缺省选项反查不回来）。
QStringList validateRecursionTierTable(const QVector<RecursionTierDescriptor> &table);

// ---------------------------------------------------------------------------
// 深度边界上的解释（第 2、4 条）
// ---------------------------------------------------------------------------

// 一个**未被展开的目录**该怎么说清自己。引擎、界面与用例共用这一份，
// 免得三处各写一句中文（三份迟早分叉，而这里分叉的后果是用户分不清
// 「这一层没内容」和「这一层没实现」）。
//
// 两种形态刻意分开：档位不递归时说清是**档位**决定的，达到深度上限时把
// **实际上限数值**印出来（第 4 条要的「明确提示」缺了这个数字就只是一句
// 「有个上限」，用户还得自己去翻设置）。
QString recursionBoundaryExplanation(const Options &options);

// ---------------------------------------------------------------------------
// 循环符号链接（第 5 条）
// ---------------------------------------------------------------------------

// 跟随这个符号链接会不会回到它自己的上级（含自身）？
//
// 引擎里符号链接**一律不跟随**（`classify()` 只比较链接目标字符串，
// `walk()` 只在 `Kind::Directory` 上递归），所以无限递归在结构上就不可能发生。
// 但那只是「没有坏事发生」，规格要的是**检测到并记下来**：静默不跟随会让一个
// 循环链接看起来只是一条普通条目，用户得不到任何「这棵树没走完」的信号。
//
// 判据（等比它们更弱的条件都放过）：
//   把 `linkTarget` 相对链接所在目录解析出来，得到 `resolved`；
//   若 `resolved == 链接自身`，或 `resolved` 是链接自身路径的**严格上级**，
//   则跟随它必然再次枚举到这个链接 —— 记成循环。
//
// 这一条同时覆盖三种表面不同、实质相同的情形，因此不需要第三个取值：
//   · `cycle -> .`（指向自己所在目录）、`link -> ..`（指向上级）、
//     `link -> ../../..`（越过扫描根，而扫描根的上级仍然含这个链接）；
//   · 绝对路径目标恰好等于扫描根（`cycle -> /path/to/root`）；
//   · 绝对路径目标是扫描根的上级（`link -> /tmp`，而根是 `/tmp/x/left`）。
// 反过来，指向兄弟子树的目标（`link -> ../sibling`）**不是**循环：跟随它
// 枚举的是另一棵子树，那里没有这个链接。判据把它放过是刻意的。
//
// 相对目标里出现 `..` 越过扫描根时，`resolved` 落在根之外，但 `resolved` 仍是
// 链接路径的字符串上级 ⇒ 照样判成循环；而完全无关的绝对目标（`/elsewhere`）
// 既不是自身也不是上级 ⇒ 放过。不需要为「越界」单独开一档。
//
// `caseSensitivity` 由调用方从 `FileSystem::caseSensitivity()` 传入：它回答的是
// 「这两条路径是不是同一条路径」，正是文件系统自己的大小写规则（DIR-006 的
// 那条边界：文件系统规则决定**路径身份**，不覆盖用户的比较设置）。
// 本判据是安全网，漏判的方向是安全的（不报而已），误判才会打扰用户。
bool linkTargetReentersAncestor(const QString &rootPath, const QString &relativePath,
                                const QString &linkTarget,
                                Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive);

// 记进 `Entry::explanation` 的那句话。两个参数都是原值，便于用户自己去核对。
QString linkCycleExplanation(const QString &relativePath, const QString &linkTarget);

} // namespace Folder
} // namespace LqCompare

#endif // LQCOMPARE_RECURSIONSTRATEGY_H
