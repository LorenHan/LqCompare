#include "pathutils.h"

#include <QStringList>

namespace LqCompare {
namespace Files {
namespace PathUtils {

// -----------------------------------------------------------------------------
// 风格
// -----------------------------------------------------------------------------

Style Style::posix()
{
    Style style;
    style.separator = QLatin1Char('/');
    style.backslashIsSeparator = false;
    style.allowDriveLetter = false;
    style.allowUnc = false;
    return style;
}

Style Style::windows()
{
    Style style;
    style.separator = QLatin1Char('\\');
    style.backslashIsSeparator = true;
    style.allowDriveLetter = true;
    style.allowUnc = true;
    return style;
}

int extendedPathThreshold()
{
    // 260 是经典 MAX_PATH；扣掉 "\\?\" 的 4 个字符再留一点余量，取 248。
    // 这是「从这里开始就该处理长路径」的提示线，不是「超过就一定失败」的硬边界。
    return 248;
}

// -----------------------------------------------------------------------------
// 分隔符与切分
// -----------------------------------------------------------------------------

QString unifySeparators(const QString &path, const Style &style)
{
    QString result = path;

    if (style.separator == QLatin1Char('/')) {
        // 目标分隔符是 '/'。只有 Windows 风格需要把 '\' 也换掉；
        // POSIX 上的 '\' 是合法文件名字符，动了就是改文件名，绝对不能碰。
        if (style.backslashIsSeparator)
            result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    } else {
        result.replace(QLatin1Char('/'), style.separator);
        if (!style.backslashIsSeparator)
            result.replace(QLatin1Char('\\'), style.separator);
    }

    return result;
}

QStringList split(const QString &path, const Style &style)
{
    QStringList parts;

    // 先统一分隔符，避免 Windows 风格下 '/' 与 '\' 混写时漏掉某一段。
    const QString unified = unifySeparators(path, style);
    const int length = unified.length();

    int start = 0;
    for (int i = 0; i <= length; ++i) {
        if (i < length && unified.at(i) != style.separator)
            continue;
        // 落到这里：i 处是分隔符，或已到末尾。
        if (i > start)
            parts.append(unified.mid(start, i - start));
        // 空段（连续分隔符、或以分隔符开头）在这里被自然丢弃，
        // 所以 UNC 的 "\\server\share" 会得到 ["server", "share"]，没有空串。
        start = i + 1;
    }

    return parts;
}

// -----------------------------------------------------------------------------
// 前缀识别
//
// 这三个函数都假定输入已经被 unifySeparators 处理过。
// -----------------------------------------------------------------------------

bool isUnc(const QString &path, const Style &style)
{
    if (!style.allowUnc)
        return false;
    if (path.length() < 2)
        return false;
    return path.at(0) == style.separator && path.at(1) == style.separator;
}

QString uncPrefix(const QString &path, const Style &style)
{
    if (!isUnc(path, style))
        return QString();

    const QStringList parts = split(path, style);
    const QString heads = QString(2, style.separator); // "\\"

    if (parts.isEmpty())
        return heads;                                  // "\\"
    if (parts.size() == 1)
        return heads + parts.at(0);                    // "\\server"
    return heads + parts.at(0) + style.separator + parts.at(1); // "\\server\share"
}

QString drivePrefix(const QString &path, const Style &style)
{
    if (!style.allowDriveLetter)
        return QString();
    // 必须是「字母 + 冒号」且长度足够。注意只认单字母盘符：
    // 长度为 1 的 "C:" 也接受（表示 C 盘的当前目录，属驱动器相对路径）。
    if (path.length() >= 2 && path.at(0).isLetter() && path.at(1) == QLatin1Char(':'))
        return path.left(2);
    return QString();
}

bool isAbsolute(const QString &path, const Style &style)
{
    if (path.isEmpty())
        return false;

    const QString unified = unifySeparators(path, style);

    // 以分隔符开头即绝对：POSIX 的 "/x"、Windows 的 "\x" 与 UNC 的 "\\server\share"。
    if (unified.startsWith(style.separator))
        return true;

    // Windows 的 "C:\x" 是绝对；但 "C:x" 是「C 盘当前目录下的 x」，属相对路径。
    if (style.allowDriveLetter && unified.length() >= 3
        && unified.at(1) == QLatin1Char(':') && unified.at(2) == style.separator) {
        return true;
    }

    return false;
}

bool isRootPath(const QString &path, const Style &style)
{
    if (path.isEmpty())
        return false;

    const QString normalized = normalize(path, style);

    if (style.allowUnc && isUnc(normalized, style)) {
        const QString prefix = uncPrefix(normalized, style);
        if (prefix.isEmpty())
            return false;
        // 归一化后的 UNC 根是 "\\server\share\"，比前缀多一个分隔符。
        return normalized.length() == prefix.length() + 1;
    }

    // Windows 盘符根："C:\"
    if (style.allowDriveLetter && normalized.length() == 3
        && normalized.at(1) == QLatin1Char(':') && normalized.at(2) == style.separator) {
        return true;
    }

    // POSIX 根："/"
    return normalized == QString(style.separator);
}

// -----------------------------------------------------------------------------
// 规范化
// -----------------------------------------------------------------------------

QString normalize(const QString &path, const Style &style)
{
    if (path.isEmpty())
        return path;

    const QString unified = unifySeparators(path, style);
    const QChar separator = style.separator;

    // 第一步：切出「不可再消解的头部」。
    // 头部一旦确定就不会再变，后续的 "." / ".." 只在剩余部分里消解，
    // 因此 "/.." 会得到 "/" 而不是 "../"。
    QString prefix;
    QString remainder = unified;

    if (style.allowUnc && isUnc(unified, style)) {
        prefix = uncPrefix(unified, style) + separator; // "\\server\share\"
        remainder = unified.mid(prefix.length() - 1);   // 保留分隔符，交给 split 丢弃空段
        while (remainder.startsWith(separator))
            remainder.remove(0, 1);
    } else if (style.allowDriveLetter && !drivePrefix(unified, style).isEmpty()) {
        const QString drive = drivePrefix(unified, style); // "C:"
        remainder = unified.mid(drive.length());
        if (remainder.startsWith(separator)) {
            prefix = drive + separator;                    // "C:\"
            while (remainder.startsWith(separator))
                remainder.remove(0, 1);
        } else {
            // 驱动器相对路径，如 "C:work\a"。前缀保持 "C:"，
            // 拼接时不会再插分隔符（见 join 的同类处理）。
            prefix = drive;
        }
    } else if (unified.startsWith(separator)) {
        prefix = QString(separator);                        // "/"
        while (remainder.startsWith(separator))
            remainder.remove(0, 1);
    }

    // 第二步：逐段消解 "." 与 ".."。
    QStringList resolved;
    const QStringList segments = split(remainder, style);
    for (const QString &segment : segments) {
        if (segment == QLatin1String("."))
            continue; // "." 是「当前目录」，直接丢掉

        if (segment == QLatin1String("..")) {
            if (!resolved.isEmpty() && resolved.last() != QLatin1String("..")) {
                resolved.removeLast(); // 能退就退
            } else if (prefix.isEmpty()) {
                // 相对路径且已退无可退，如 "../a"：必须保留 ".."，
                // 否则 "../a" 会被错算成 "a"，指向完全不同的位置。
                resolved.append(segment);
            }
            // 有头部前缀时（绝对路径）越界即丢弃："/.." → "/"
            continue;
        }

        resolved.append(segment);
    }

    QString result = prefix + resolved.join(separator);

    // 全部消解掉的结果：相对路径 → "."（当前目录）；绝对路径保留前缀。
    if (result.isEmpty())
        return QStringLiteral(".");

    return result;
}

// -----------------------------------------------------------------------------
// 拼接与拆分
// -----------------------------------------------------------------------------

QString join(const QString &base, const QString &name, const Style &style)
{
    if (base.isEmpty())
        return name;
    if (name.isEmpty())
        return base;

    // 驱动器相对前缀之后不能插分隔符："C:" + "a" 是 "C:a"（C 盘当前目录下的 a），
    // 写成 "C:\a" 就变成另一个位置了。
    if (style.allowDriveLetter && base.length() == 2 && base.at(1) == QLatin1Char(':'))
        return base + name;

    if (base.endsWith(style.separator))
        return base + name;

    return base + style.separator + name;
}

QString fileName(const QString &path, const Style &style)
{
    const QStringList parts = split(path, style);
    return parts.isEmpty() ? QString() : parts.last();
}

QString parentPath(const QString &path, const Style &style)
{
    const QString normalized = normalize(path, style);

    // 根没有父目录，返回自身——调用方（如向上递归删除）靠这个停下来。
    if (isRootPath(normalized, style))
        return normalized;

    const QChar separator = style.separator;
    const int index = normalized.lastIndexOf(separator);

    // 相对路径只有一段："a" → "."（当前目录）
    if (index < 0)
        return QStringLiteral(".");

    // "/a" → "/"
    if (index == 0)
        return normalize(QString(separator), style);

    // "C:\a" → "C:\"（而不是 "C:"，后者是驱动器相对路径，含义不同）
    if (style.allowDriveLetter && index == 2 && normalized.at(1) == QLatin1Char(':'))
        return normalize(normalized.left(3), style);

    // 再走一遍 normalize 让返回值保持规范形式，
    // 例如 "\\server\share\a" → "\\server\share\"（补上结尾分隔符）
    return normalize(normalized.left(index), style);
}

// -----------------------------------------------------------------------------
// 长路径
// -----------------------------------------------------------------------------

QString toExtendedPath(const QString &path, const Style &style)
{
    // 只有 Windows 有这个概念。其他平台原样返回，让调用方不必到处判断平台。
    if (!style.allowUnc)
        return path;

    static const QString extendedPrefix = QStringLiteral("\\\\?\\");
    if (path.startsWith(extendedPrefix))
        return path; // 已带前缀，重复添加会得到非法路径

    if (path.length() < extendedPathThreshold())
        return path;

    // 前缀语法要求绝对路径。相对路径加了没有意义，反而会被系统拒绝。
    if (!isAbsolute(path, style))
        return path;

    // UNC 必须写成 \\?\UNC\server\share\...，
    // 直接拼成 \\?\\\server\share 是错的。
    if (isUnc(path, style))
        return QStringLiteral("\\\\?\\UNC\\") + path.mid(2);

    return extendedPrefix + path;
}

// -----------------------------------------------------------------------------
// 比较与名称校验
// -----------------------------------------------------------------------------

bool comparePaths(const QString &left, const QString &right, const Style &style,
                  Qt::CaseSensitivity caseSensitivity)
{
    // 两边都先规范化，这样 "/a/b" 与 "/a//b/" 视为同一个位置。
    const QString normalizedLeft = normalize(left, style);
    const QString normalizedRight = normalize(right, style);
    return QString::compare(normalizedLeft, normalizedRight, caseSensitivity) == 0;
}

int findInvalidFileNameCharacter(const QString &name)
{
    if (name.isEmpty())
        return 0;

    // 取「所有平台里最严格的那一套」：Windows 的保留字符集合。
    // 这样做是有意的——本工具经常在 Windows 与 Linux 之间比同一份文件树，
    // 在这里放行一个 Linux 合法但 Windows 非法的名字（如 "a:b.txt"），
    // 等到那份文件树被同步到 Windows 上才失败，那时已经很难追查到源头。
    static const QString forbidden = QStringLiteral("<>:\"/\\|?*");

    for (int i = 0; i < name.length(); ++i) {
        const QChar character = name.at(i);
        // 控制字符（含换行、制表符）一律非法：它们在列表里无法正确显示，
        // 也会让日志与报表的输出错乱。
        if (character.unicode() < 0x20 || character.unicode() == 0x7F)
            return i;
        if (forbidden.contains(character))
            return i;
    }

    // "." 与 ".." 是目录项而非文件名，长度合法但语义非法。
    // 返回 0 表示「从第一个字符起就不合法」，便于界面把光标定位到开头。
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return 0;

    return -1;
}

bool isValidFileName(const QString &name)
{
    return !name.isEmpty() && findInvalidFileNameCharacter(name) < 0
           && !isReservedName(name);
}

bool isReservedName(const QString &name)
{
    // Windows 的保留设备名。它们不是「含非法字符」，而是「整名保留」，
    // 因此只能在名称整体比较时判定，不能靠字符检查发现。
    // 例如 "con.txt" 在 Windows 上也是非法的——保留名匹配不带扩展名。
    static const QStringList reserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9"),
    };

    const int dot = name.indexOf(QLatin1Char('.'));
    const QString stem = dot < 0 ? name : name.left(dot);
    return reserved.contains(stem, Qt::CaseInsensitive);
}

} // namespace PathUtils
} // namespace Files
} // namespace LqCompare
