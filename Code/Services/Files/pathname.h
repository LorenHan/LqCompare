#ifndef LQCOMPARE_PATHNAME_H
#define LQCOMPARE_PATHNAME_H

#include "pathutils.h"

#include <QByteArray>
#include <QString>

namespace LqCompare {
namespace Files {

///
/// \brief 路径名称的文本处理：字节保真、Unicode 规范化、安全显示（PRD: PLAT-007）。
///
/// 为什么需要单独一层
/// ----------------
/// 界面层用 QString（UTF-16），而 POSIX 系统调用层的路径是一串**字节**。
/// 两者之间不是无损的：Linux 上文件名只要是「不含 `/` 与 NUL 的字节序列」就合法，
/// 完全可以是无效 UTF-8。而 `QString::fromUtf8` 遇到无效字节会替换成 U+FFFD，
/// 原始字节**当场丢失**——之后这个文件既打不开也删不掉，用户看到的是一个
/// 「看起来正常但什么操作都失败」的条目，而且失败原因永远查不出来。
///
/// 因此这一层要解决三件事：
///   1. **保真**：无效字节不丢，原样带在 QString 里（见下面的私存码位方案）。
///   2. **规范化**：同一个视觉名字的不同 Unicode 组合形式要能判为同一个名字，
///      否则 macOS（文件名用 NFD）与用户输入（多为 NFC）之间会大量误判「不同」。
///   3. **安全显示**：换行、制表符、首尾空格在列表里看不见，用户会在
///      「看起来一样」的两个名字里挑错。
///
/// 私存码位方案（与 Python 的 surrogateescape 同源）
/// -----------------------------------------------
/// 无效字节 b（0x80..0xFF）在 QString 里用一个**未配对低代理**承载：
/// 码位 = 0xDC00 + b。选择这个区间的理由：
///   - 它永远不会由合法的 UTF-8 解码产生（合法解码不会产生未配对代理）；
///   - ASCII 字节（0x00..0x7F）在 UTF-8 里恒定合法，不可能成为「无效字节」，
///     所以只用 0xDC80..0xDCFF 就够，不需要低半段；
///   - Qt 允许 QString 保存未配对代理（已实测：`at()` 能原样取回），
///     `normalized()` 也会原样保留它们（同样已实测）。
///
/// **唯一必须自己做的**：`QString::toUtf8()` 会把未配对代理替换成 '?'（实测
/// `DC80 0061 DCFF` → `3f 61 3f`）。所以编码回字节必须走 toNativeBytes()，
/// 绝不能用 toUtf8()。这条是这一层最容易被破坏的契约。
///
namespace PathName {

/// 承载原始字节的私存码位范围（未配对低代理）。
constexpr ushort RawByteEscapeFirst = 0xDC80;
constexpr ushort RawByteEscapeLast = 0xDCFF;

/// 把系统调用返回的原始字节解码成界面文本。**保真**：无效字节不丢弃。
///
/// 只用于 POSIX。Windows 在系统调用层就是 UTF-16，不存在这一层转换
/// （这正是 PLAT-007 第 1 条要求「不经过 ANSI 转换」的意思）。
QString fromNativeBytes(const QByteArray &bytes);

/// 把界面文本编码回系统调用的原始字节。fromNativeBytes() 的逆运算。
///
/// **不要用 QString::toUtf8() 代替它**：未配对代理会被替换成 '?'，
/// 于是原始字节永久丢失，而且从返回值上看不出发生过什么。
QByteArray toNativeBytes(const QString &text);

/// 文本里是否承载了无法用 UTF-8 表示的原始字节。
bool hasRawBytes(const QString &text);

///
/// \brief 把名字转成「能看清到底有什么字符」的显示形式。
///
/// 处理三类在界面上看不见的东西：
///   - 换行 / 制表 / 回车 → `\n` `\t` `\r`（它们会直接破坏列表的行结构）；
///   - 其它控制字符 → `\xNN`；
///   - 承载的原始字节 → `\xNN`（用真实的字节值，用户拿去写脚本能对上）；
///   - 首尾空格 → `·`（U+00B7）。这是最要紧的一类：「a.txt」与「a.txt 」
///     在列表里长得一模一样，而它们是两个不同的文件。
///
/// **只用于显示**，返回值不能拿去操作文件。
QString forDisplay(const QString &name);

/// forDisplay() 是否改动了名字。供界面决定要不要给这个名字加引号或提示。
bool displayDiffersFromActual(const QString &name);

///
/// \brief 统一 Unicode 组合形式到 NFC。
///
/// 为什么需要：`é` 有两种表示——单个码位 U+00E9（NFC），或 `e` + U+0301 组合
/// 抑音符（NFD）。两者视觉完全相同，字节完全不同。macOS 的文件系统会把名字
/// 存成 NFD，而用户在输入框里敲出来的是 NFC。不做规范化，界面会显示「未找到」
/// 而文件明明就在那里；文件夹比对会把同一个文件判成「两侧都有但不同」。
///
/// 只用于**比较与显示**，不要用它改写用户给出的路径——那会让「按名字查找」
/// 与文件系统里的实际字节对不上。
QString normalizedName(const QString &name);

/// 比较两个名字，忽略 Unicode 组合形式的差异。大小写语义由调用方传入。
bool equalNames(const QString &left, const QString &right, Qt::CaseSensitivity caseSensitivity);

} // namespace PathName
} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_PATHNAME_H
