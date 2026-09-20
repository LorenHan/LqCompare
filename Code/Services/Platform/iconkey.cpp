#include "iconkey.h"

#include <QChar>

namespace LqCompare {
namespace Platform {

namespace IconKey {

QString fromPath(const QString &path, const Files::PathUtils::Style &style)
{
    // 只看最后一段。这一步不能用「全路径里最后一个点」代替：
    // `/a.d/b` 里的点属于目录名。
    const QString name = Files::PathUtils::fileName(path, style);
    if (name.isEmpty())
        return QString::fromLatin1(NoExtension);

    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return QString::fromLatin1(NoExtension);

    // 结尾的点：`name.` 没有扩展名（PathFindExtension 同样给出空扩展名）。
    if (dot == name.size() - 1)
        return QString::fromLatin1(NoExtension);

    // 注意 dot == 0 时这里会取到「点之后的全部」，也就是 `.gitignore`
    // 得到 `gitignore`——这正是 Windows Shell 的答案，见头文件里的说明。
    const QString extension = name.mid(dot + 1).trimmed().toLower();
    if (extension.isEmpty())
        return QString::fromLatin1(NoExtension);
    return extension;
}

QString cacheKey(const QString &extensionKey, bool isDirectory)
{
    // 目录且没有扩展名时用专门的键，避免和「恰好叫 ? 的扩展名」撞在一起。
    // 实际上扩展名不可能是 "?"（问号在 Windows 上是非法文件名字符，
    // 在 POSIX 上虽然合法但极罕见），这里仍然显式区分——因为一旦撞上，
    // 表现是所有无扩展名的文件都显示成文件夹图标。
    const QString effective = (isDirectory && extensionKey == QLatin1String(NoExtension))
            ? QString::fromLatin1(Directory)
            : extensionKey;

    return QString(isDirectory ? QLatin1String("d|") : QLatin1String("f|")) + effective;
}

bool parseCacheKey(const QString &cacheKey, QString *extensionKey, bool *isDirectory)
{
    if (cacheKey.size() < 3)
        return false;

    const QChar kind = cacheKey.at(0);
    if (cacheKey.at(1) != QLatin1Char('|'))
        return false;
    if (kind != QLatin1Char('f') && kind != QLatin1Char('d'))
        return false;

    if (extensionKey)
        *extensionKey = cacheKey.mid(2);
    if (isDirectory)
        *isDirectory = (kind == QLatin1Char('d'));
    return true;
}

} // namespace IconKey

// -----------------------------------------------------------------------------
// 尺寸
// -----------------------------------------------------------------------------

namespace Win32IconSize {

int nearest(int requested)
{
    if (requested <= Small)
        return Small;
    if (requested <= Large)
        return Large;
    if (requested <= ExtraLarge)
        return ExtraLarge;
    // 48 到 256 之间是大跳（64/96/128/192 都取不到）。宁可给 256 再缩，
    // 也不给 48 再放大——放大是插值出来的模糊。
    return Jumbo;
}

} // namespace Win32IconSize

int iconPixelSize(int baseSize, qreal devicePixelRatio)
{
    if (baseSize <= 0)
        return 0;

    // 非法的缩放比按 1.0 处理：原样相乘会得到 0 或负数，
    // 界面上表现为「图标不见了」，而原因是一个没初始化的变量。
    qreal ratio = devicePixelRatio;
    if (!(ratio > 0.0)) // 这个写法同时挡住 <= 0 与 NaN
        ratio = 1.0;

    const int scaled = qRound(baseSize * ratio);
    constexpr int kMaximumIconPixels = 1024;

    // 下限 1：0 像素的图什么也画不出来，观感与「图标缺失」相同，
    // 而调用方会以为自己拿到了一张图。
    return qBound(1, scaled, kMaximumIconPixels);
}

} // namespace Platform
} // namespace LqCompare
