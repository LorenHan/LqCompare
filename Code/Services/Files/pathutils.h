#ifndef LQCOMPARE_PATHUTILS_H
#define LQCOMPARE_PATHUTILS_H

#include <QChar>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace LqCompare {
namespace Files {

///
/// \brief 路径字符串的工具函数（PRD: PLAT-002、PLAT-007）。
///
/// 为什么单独抽出来，而不是直接写在平台实现里
/// ------------------------------------------------
/// Windows 的路径规则（反斜杠、盘符、UNC、`\\?\` 长路径前缀）与 POSIX 的规则
/// （正斜杠、单一根、无盘符）差别很大。如果把它们埋在 `#ifdef Q_OS_WIN` 里，
/// 在 macOS 上开发时这段代码**一次都不会被执行**，等拿到 Windows 上才发现错误。
///
/// 这里把它们写成「纯字符串变换」：输入一个路径和该平台的风格（分隔符等），
/// 输出处理结果，不碰真实文件系统。于是可以在 macOS 上完整测试 Windows 的
/// 路径逻辑——这正是 PRD PLAT-010 要的「平台差异有专门用例覆盖」。
///
/// 所有函数都不访问文件系统，也不依赖当前工作目录。
///
namespace PathUtils {

/// 路径风格：把「平台相关的几个开关」显式作为参数传进来，而不是在函数内部
/// 查 Q_OS_* 宏。这样测试可以同时构造 Windows 风格与 POSIX 风格。
struct Style
{
    QChar separator = QLatin1Char('/'); ///< 主要分隔符
    bool backslashIsSeparator = false;  ///< Windows 上 `\` 也是分隔符
    bool allowDriveLetter = false;      ///< Windows 上 `C:` 是合法前缀
    bool allowUnc = false;              ///< Windows 上 `\\server\share` 是合法前缀

    /// POSIX 风格：只用 `/`，无盘符、无 UNC。
    static Style posix();

    /// Windows 风格：`/` 与 `\` 都算分隔符，支持盘符与 UNC。
    static Style windows();
};

/// Windows 上用 `\\?\` 前缀绕开 260 字符限制的阈值。
///
/// 为什么是 248 而不是经典的 MAX_PATH(260)：`\\?\` 前缀本身占 4 个字符，
/// 而完整路径长度上限是 260。留一点余量给文件名，取 248 作为「从这里开始
/// 就该考虑加前缀」的判断线，而不是「超过就一定失败」的分界线。
int extendedPathThreshold();

/// 把路径里的分隔符统一成 style.separator。
///
/// 只做替换，**不合并连续分隔符**——因为 UNC 的 "\\server\share" 开头就是
/// 两个分隔符，合并会把 UNC 破坏成普通路径。连续的冗余分隔符由 normalize
/// （经由 split 丢弃空段）处理。
///
/// 例（windows 风格）：`C:/a\b//c` → `C:\a\b\\c`
/// 例（posix 风格）：`a/b` → `a/b`（不改动，POSIX 上 `\` 是合法文件名字符）
///
QString unifySeparators(const QString &path, const Style &style);

/// 规范化：统一分隔符 + 去掉冗余分隔符 + 解析 `.` 与 `..` + 去掉结尾分隔符
/// （根目录本身除外）。
///
/// 刻意不做的事：
/// - 不解析符号链接（那需要访问文件系统，属 FileSystem::realPath 的职责）。
/// - 不把相对路径转成绝对路径（那需要当前工作目录）。
/// - 不做大小写折叠（大小写语义由调用方按 caseSensitivity 决定，见 comparePaths）。
///
/// `..` 的处理只做纯字符串层面的消解：`/a/b/../c` → `/a/c`。
/// 当 `..` 出现在根或盘符之后无法再退时（如 `C:\..\x`），保留 `..`，
/// 因为纯字符串无法判断它是否真的越界。
QString normalize(const QString &path, const Style &style);

/// 按分隔符切分，丢弃空段。`/a//b/` → ["a", "b"]。
QStringList split(const QString &path, const Style &style);

/// 拼接。base 为根目录（如 `/` 或 `C:\`）时不会产生双分隔符。
QString join(const QString &base, const QString &name, const Style &style);

/// 最后一段名称。`C:\a\b.txt` → `b.txt`；`/` → 空串。
QString fileName(const QString &path, const Style &style);

/// 去掉最后一段。`/a/b` → `/a`；`/a` → `/`；`/` → `/`。
QString parentPath(const QString &path, const Style &style);

/// 是否是绝对路径。POSIX：以 `/` 开头。Windows：以分隔符开头（含 UNC）或以盘符开头。
bool isAbsolute(const QString &path, const Style &style);

/// 是否是 UNC 路径（`\\server\share\...` 或 `//server/share/...`）。
/// POSIX 风格下恒为 false。
bool isUnc(const QString &path, const Style &style);

/// 取出 UNC 的 `\\server\share` 前缀（不含后续路径）。非 UNC 返回空串。
/// 用途：UNC 根是「不能往上退」的边界，判断父目录是否已达根时需要它。
QString uncPrefix(const QString &path, const Style &style);

/// 取出盘符前缀（`C:`）。非盘符路径返回空串。
QString drivePrefix(const QString &path, const Style &style);

/// 是否是「根」：POSIX 的 `/`、Windows 的 `C:\` 或 `\\server\share`。
/// 根目录没有父目录，调用方在写入与删除时需要靠它停下来。
bool isRootPath(const QString &path, const Style &style);

/// 给长路径加上 Windows 扩展前缀。已经带前缀则原样返回；
/// 长度未达阈值也原样返回。非 Windows 风格恒原样返回。
///
/// 注意：扩展前缀路径**必须是绝对路径且不能再做规范化**——
/// 加前缀之后 Windows 不再解析 `.`/`..`，也不会做大小写折叠。
/// 因此调用顺序必须是「先规范化，再加前缀」，反过来会出错。
QString toExtendedPath(const QString &path, const Style &style);

/// 比较两个路径是否指向同一位置，按给定的分隔符与大小写语义。
///
/// 先按 style 规范化再比，因此 `/a/b` 与 `/a//b/` 视为相同。
/// 是否大小写敏感由调用方从 FileSystem::caseSensitivity() 传入——
/// 同一个程序在 Windows 与 Linux 上对「a.txt 与 A.TXT 是否同一个文件」的
/// 答案必须不同，否则文件夹比对会给出错误结论。
bool comparePaths(const QString &left, const QString &right, const Style &style,
                  Qt::CaseSensitivity caseSensitivity);

/// 判断一个名称是否合法（不含分隔符、不为 `.` / `..`、不为空、不含控制字符、
/// 不是保留设备名）。用于重命名与新建时的前置校验（PLAT-007 要求给出明确原因）。
bool isValidFileName(const QString &name);

/// 名称里第一个非法字符的位置；合法时返回 -1。
///
/// 单独返回位置（而不是只返回 bool）是为了界面能把光标直接定位到出错处，
/// 而不是只报一句「名称非法」让用户自己找。
int findInvalidFileNameCharacter(const QString &name);

///
/// \brief 名称不合法的原因（PRD: PLAT-007 第 4 条）。
///
/// 为什么是枚举而不是直接返回一句文案
/// -------------------------------
/// 界面要做的远不止「显示原因」。不同原因的处置完全不同：
///   - `TrailingSpaceOrDot` 可以给一个「去掉首尾空格」的一键修正按钮；
///   - `ReservedName` 只能让用户改名，没有可自动做的事；
///   - `TooLong` 可以提示截断到多少字符。
/// 一旦把结果做成字符串，这些能力就只能在界面层重新解析文案来恢复——
/// 那等于把「有哪些原因」这个知识抄了第二份，迟早不一致。
///
enum class FileNameProblem {
    None = 0,
    Empty,              ///< 名字为空
    DotOrDotDot,        ///< `.` 或 `..`：是目录项，不是文件或目录的名字
    ControlCharacter,   ///< 控制字符（含换行、制表、回车）
    ForbiddenCharacter, ///< 平台禁止的字符
    TrailingSpaceOrDot, ///< 以空格或点结尾
    ReservedName,       ///< Windows 保留设备名
    TooLong,            ///< 超过单个名字的长度上限
};

/// 稳定的机器可读标识（用于日志与测试断言，不用于界面显示）。
const char *fileNameProblemIdentifier(FileNameProblem problem);

/// 面向用户的原因说明与可执行的建议。两人分工：先说要改什么，再说为什么。
QString describeFileNameProblem(FileNameProblem problem);

/// 名称校验结果：问题分类 + 位置（合法时 position 为 -1）。
struct FileNameCheck
{
    FileNameProblem problem = FileNameProblem::None;
    int position = -1;

    bool isValid() const { return problem == FileNameProblem::None; }
};

///
/// \brief 一次查清名称的所有问题（PRD: PLAT-007 第 4 条「统一校验」）。
///
/// 这是唯一的实现，isValidFileName() 与 findInvalidFileNameCharacter() 都是它的
/// 薄封装。三个函数各写一遍校验逻辑的话，「什么算非法」就有了三个事实来源，
/// 而且分歧方式是静默的：`isValidFileName` 说不合法、`checkFileName` 说合法，
/// 调用方各取所需，用户遇到的是「有时能建、有时不能建」。
///
/// **校验标准取所有平台里最严格的一套**，理由见 pathutils.cpp 里的说明：
/// 本工具经常在 Windows 与 Linux 之间比同一份文件树，在这里放行一个 Linux 合法
/// 但 Windows 非法的名字，要等到那份文件树被同步到 Windows 上才失败。
///
FileNameCheck checkFileName(const QString &name);

/// 是否是系统保留设备名（Windows 的 CON / PRN / AUX / NUL / COM1-9 / LPT1-9）。
///
/// 这类名称不含任何非法字符，因此字符检查发现不了——只能在整名比较时判定。
/// 匹配时忽略扩展名且不区分大小写，因为 Windows 上 "con.txt" 同样非法。
bool isReservedName(const QString &name);

} // namespace PathUtils
} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_PATHUTILS_H
