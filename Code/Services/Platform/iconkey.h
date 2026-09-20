#ifndef LQCOMPARE_ICONKEY_H
#define LQCOMPARE_ICONKEY_H

#include "Files/pathutils.h"

#include <QString>
#include <QtGlobal>

namespace LqCompare {
namespace Platform {

///
/// \brief 图标缓存键的构造规则（PRD: PLAT-004 完成标准第 2 条）。
///
/// 为什么缓存必须按「类型」而不是按「文件」
/// ------------------------------------
/// 一个文件夹里 10 万个文件，扩展名通常只有几十种。按文件缓存意味着
/// 一条目录项一份图标位图（在 256×256 下每份 256 KB），内存直接失控；
/// 按扩展名缓存则把条目数压到「有多少种类型」，与文件数无关。
///
/// PLAT-004 把这一条写成了完成标准（「缓存命中率高」），因此这里刻意把
/// 「怎么算一个键」做成纯函数而不是埋在平台实现里——它是本条目里唯一
/// 能脱离平台被完整验证的部分，也是最容易写错的部分：
/// 键算错的表现是「图标不对」，而不是「崩溃」，很难靠手工点到。
///
namespace IconKey {

///
/// \brief 没有扩展名的条目共用的键。
///
/// 用 "?" 而不是空串：空串会让「键为空」同时表示「真的没有扩展名」与
/// 「计算失败/未知」两件事，日志里 `key=` 之后什么也看不出来。
///
constexpr const char *NoExtension = "?";

/// 目录（且自身没有扩展名）共用的键。
///
/// 目录一律共用一个键，不去区分具体是哪个目录。代价是**自定义文件夹图标
/// 拿不到**（Windows 的 `desktop.ini`、macOS 的文件夹图标属性都需要额外
/// 读文件，属后续工作）；收益是不为每个目录建一个键。有扩展名的目录
/// （如 macOS 的 `Foo.app`）不走这个键，会按扩展名解析。
constexpr const char *Directory = "<dir>";

///
/// \brief 由路径算出扩展名键。
///
/// 规则与 Windows Shell 的 `PathFindExtension` 对齐，而不是「凭直觉」：
///
///   - 只看**最后一段**里的最后一个点。`/a.d/b` 里的点属于目录名，
///     若在全路径上找最后一个点，整个目录的条目会共用一个错误的键。
///   - 结尾的点按「没有扩展名」处理：`name.` → `?`。
///   - 开头的点**算作扩展名**：`.gitignore` → `gitignore`。
///     直觉上会觉得它是「隐藏文件、无扩展名」，但 `PathFindExtension`
///     给的确实是 `gitignore`，而我们要的是「和系统给的答案一致」——
///     我们用这个键去问系统图标的，键不一致就会拿到另一套图标。
///   - 结果折叠为小写。这一步本身就是命中率的一部分：同一个目录里
///     `A.TXT` 与 `a.txt` 若各成一个键，命中率会凭空掉一半。
///     文件类型关联本身也不区分大小写（Windows 的关联表、macOS 的
///     LaunchServices、Linux 的 shared-mime-info 都是如此）。
///   - 去掉首尾空白。`a.txt ` 这种名字（PLAT-007 会在重命名时拦下，
///     但它已经存在于磁盘上时仍要能显示）不该为多出来的空格单独占一个键。
///
QString fromPath(const QString &path, const Files::PathUtils::Style &style);

///
/// \brief 把扩展名键与「是不是目录」合成真正的缓存键。
///
/// 为什么必须把 isDirectory 也编进键里
/// --------------------------------
/// 否则一个名为 `notes.txt` 的**目录**会把所有 `.txt` 文件的缓存项覆盖成
/// 文件夹图标，而且这个错误会一直留在缓存里（命中率越高，错得越久）。
/// 这类污染不会崩、不会报错，只会让用户看到一堆莫名其妙的文件夹图标。
///
/// 格式：`f|txt` / `d|txt` / `f|?` / `d|<dir>`。
/// 用 `|` 分隔是因为扩展名里不可能出现它（文件名允许的字符集里没有）。
///
QString cacheKey(const QString &extensionKey, bool isDirectory);

///
/// \brief 把缓存键拆回「扩展名键 + 是否目录」。解析失败时返回 false。
/// 存在的意义是让日志与排查能说出「这个键对应的是哪种文件」。
///
bool parseCacheKey(const QString &cacheKey, QString *extensionKey, bool *isDirectory);

} // namespace IconKey

///
/// \brief Windows 系统图标固定的几档尺寸（PRD: PLAT-004 完成标准第 5 条）。
///
/// 为什么必须知道这几档，而不是「要多大给多大」
/// ----------------------------------------
/// Windows 的图标来自 Shell 映像列表（image list），它只有离散的几档：
/// `SHGFI_SMALLICON`(16)、`SHGFI_LARGEICON`(32)、`SHIL_EXTRALARGE`(48)、
/// `SHIL_JUMBO`(256)。拿不到就退而求其次用别的档位再拉伸，结果是**模糊**，
/// 而用户在 150% 缩放下最容易看出来。
///
/// 因此取值规则是「宁可取大再缩，不取小再放大」：放大是插值出来的模糊，
/// 缩小是清晰的。代价是 96 这类请求会取到 256（约 256 KB 的位图），
/// 但一次会话里真正出现的扩展名只有几十种，可接受。
///
/// 这里没有 `static_assert` 可以写：`SHIL_*` 是**索引**（0x0/0x1/0x2/0x4）
/// 而不是尺寸，尺寸只写在微软的文档里，头文件里没有对应常量。
/// 与 Win32Error 那一套不同，这一点写在这里免得下一个人以为漏了。
///
namespace Win32IconSize {
constexpr int Small = 16;      ///< SHGFI_SMALLICON / SHIL_SMALL
constexpr int Large = 32;      ///< SHGFI_LARGEICON / SHIL_LARGE
constexpr int ExtraLarge = 48; ///< SHIL_EXTRALARGE
constexpr int Jumbo = 256;     ///< SHIL_JUMBO

/// 把任意请求尺寸吸附到可用的一档。
/// 请求 <= 16 一律给 16；请求 > 256 一律给 256（不再往上，Shell 没有更大的）。
int nearest(int requested);

} // namespace Win32IconSize

///
/// \brief 基准尺寸 + 设备像素比 -> 实际取图尺寸（PLAT-004 完成标准第 5 条）。
///
/// 为什么不能直接画的时候缩放
/// ------------------------
/// 在 200% 缩放的屏幕上，一张 16×16 的位图被拉成 32×32 显示就是模糊的。
/// 正确做法是**按缩放后的尺寸去取图**，取到清晰的 32×32 原始位图。
/// 这一步算错了不会报错，只会让高分屏上的图标看起来「有点糊」——
/// 而「有点糊」很容易被归咎于「这个软件做得不精细」。
///
/// 边界处理：
///   - baseSize <= 0 返回 0（调用方没要图，不要自作主张给一个默认值）；
///   - devicePixelRatio 非法（<= 0 或 NaN）时按 1.0 处理，而不是原样乘。
///     乘出来会是 0 或负数，界面上表现为「图标不见了」，
///     而真正的原因是调用方传了一个没初始化的缩放比——很难查；
///   - 结果下限 1（0 像素画不出东西，观感等同于「图标缺失」）；
///   - 上限 1024。没有上限的话，一个错传的缩放比会让我们对目录里每一行
///     都从 Shell 取一张 1600×1600 的位图。
///
int iconPixelSize(int baseSize, qreal devicePixelRatio);

} // namespace Platform
} // namespace LqCompare

#endif // LQCOMPARE_ICONKEY_H
