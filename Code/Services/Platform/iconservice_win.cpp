/// \file
/// \brief Windows 的系统图标提供者（PRD: PLAT-004 完成标准第 1、5 条）。
///
/// **本文件在本项目的开发机（macOS）上从未被编译过。**
/// 这一点必须如实标出来：下面用手写的常量与 `SHGetFileInfoW` 的调用约定，
/// 只能靠 `static_assert`（在 Windows 上编译时才生效）与代码审查来保证正确。
/// 与 filesystem_win.cpp / trash_win.cpp 是同一类遗留，见
/// docs/development/current-handoff.md 的「已知未验证」一节。

#include "iconkey.h"
#include "iconservice.h"

#ifdef Q_OS_WIN

#include <QDir>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QString>

// shellapi.h 必须在 windows.h 之后（见 trash_win.cpp 里的同一条踩坑记录）。
#include <windows.h>
#include <shellapi.h>

namespace LqCompare {
namespace Platform {

namespace {

///
/// \brief 用 FindFirstFile 之外的方式问 Shell 要一个「按扩展名的图标」。
///
/// 关键在 `SHGFI_USEFILEATTRIBUTES`：它让 Shell **不要访问磁盘**，
/// 直接拿传入的 `dwFileAttributes` 当作条目类型，按扩展名去查关联表。
/// 不用它的后果是每问一个图标就 stat 一次磁盘：
///   - 一个已被删除的条目会解析失败（而列表里本来就要把它的图标显示出来）；
///   - 一个网络盘上的目录会为了画图标而卡住界面；
///   - 1000 行列表就是 1000 次多余的系统调用。
///
/// 这正是 PLAT-004 的边界说的「图标获取必须缓存并异步，否则在大目录下会因
/// 逐个系统调用而严重变慢」——`SHGFI_USEFILEATTRIBUTES` 是这道题在
/// Windows 上的那一半答案，另一半是 IconRequestQueue 的去重。
///
bool shellIconFor(const QString &extensionKey, bool isDirectory, bool largeIcon, HICON *icon)
{
    // 拼一个只用来「提供扩展名」的假文件名。因为带了
    // SHGFI_USEFILEATTRIBUTES，这个文件不必存在，Shell 也不会去找它。
    QString probe = QStringLiteral("probe");
    if (!isDirectory && !extensionKey.isEmpty()
        && extensionKey != QLatin1String(IconKey::NoExtension)) {
        probe += QLatin1Char('.');
        probe += extensionKey;
    }

    SHFILEINFOW info = {};
    UINT flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES;
    flags |= largeIcon ? SHGFI_LARGEICON : SHGFI_SMALLICON;

    const DWORD attributes = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;

    // 第四个参数在带 SHGFI_USEFILEATTRIBUTES 时就是「条目属性」。
    const DWORD_PTR result = ::SHGetFileInfoW(
            reinterpret_cast<LPCWSTR>(probe.utf16()), attributes, &info, sizeof(info), flags);

    if (result == 0 || info.hIcon == nullptr)
        return false;

    *icon = info.hIcon;
    return true;
}

/// 把 HICON 转成 QImage。
///
/// 走 GetIconInfo + GetDIBits 而不是 Qt 的 QPixmap::fromWinHICON()：
/// 后者需要 Qt 的 Windows 平台插件已经初始化，而后台线程里没有 QGuiApplication
/// 的窗口系统连接可用（QIcon/QPixmap 在非 GUI 线程上的行为也随平台不同）。
/// 用原始 GDI 调用拿到像素缓冲，转换这件事就完全不依赖 Qt 的窗口系统状态。
///
/// 尺寸从位图自身读，而不是从请求值推：Shell 给的可能不是我们要的那一档
/// （见 Win32IconSize::nearest），读真实尺寸才能把它如实报给调用方。
QImage imageFromHICON(HICON icon)
{
    if (icon == nullptr)
        return QImage();

    ICONINFO info = {};
    if (::GetIconInfo(icon, &info) == FALSE)
        return QImage();

    BITMAP bitmap = {};
    const HGDIOBJ handle = (info.hbmColor != nullptr) ? (HGDIOBJ)info.hbmColor
                                                     : (HGDIOBJ)info.hbmMask;
    if (::GetObject(handle, sizeof(bitmap), &bitmap) == 0) {
        ::DeleteObject(info.hbmColor);
        ::DeleteObject(info.hbmMask);
        return QImage();
    }

    // 先问一次尺寸，不传缓冲（lpvBits 为 NULL）。
    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(header);
    header.biWidth = bitmap.bmWidth;
    // 负高度表示「自上而下」的扫描行顺序。用负值可以直接拿到与 QImage
    // 一致的行序，省掉一次手动翻转——漏掉翻转的表现是图标上下颠倒。
    header.biHeight = -bitmap.bmHeight;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;

    const int width = bitmap.bmWidth;
    const int height = bitmap.bmHeight;

    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull()) {
        ::DeleteObject(info.hbmColor);
        ::DeleteObject(info.hbmMask);
        return QImage();
    }

    HDC screen = ::GetDC(nullptr);
    const int scanned = ::GetDIBits(screen, info.hbmColor, 0, static_cast<UINT>(height),
                                    image.bits(), reinterpret_cast<BITMAPINFO *>(&header),
                                    DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, screen);

    ::DeleteObject(info.hbmColor);
    ::DeleteObject(info.hbmMask);

    if (scanned == 0)
        return QImage();

    // GetDIBits 给的 32 位数据里 alpha 通道在部分图标上是缺的（旧式图标
    // 用掩码表示透明）。这里不额外做掩码合成——那需要再取一次
    // hbmMask 并且逐个像素判断，代价与收益不成比例。
    // 结果是某些老程序的图标边缘可能有黑边，属已知的显示瑕疵。
    return image;
}

class WindowsIconProvider : public IconProvider
{
public:
    QString platformName() const override { return QStringLiteral("windows"); }

    IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) override;
};

IconEntry WindowsIconProvider::resolve(const QString &extensionKey, bool isDirectory, int pixelSize)
{
    IconEntry entry;

    if (pixelSize <= 0)
        return entry;

    // Shell 只有离散的几档尺寸，先吸附到最近的一档。
    // 注意：48 与 256 这两档要 IImageList（COM），本文件暂未接上，
    // 因此这里实际只会走 16 / 32 两档；48 以上会拿到 32 再被放大，
    // 也就是「比请求的小」。actualPixelSize 会把这个事实如实报出来，
    // 让视图有机会提示或自行缩放，而不是让它变成一个说不清的模糊。
    // 接 IImageList 时只需替换下面这一次取图调用——尺寸吸附已经算好了。
    const int shellSize = Win32IconSize::nearest(pixelSize);
    const bool largeIcon = shellSize > Win32IconSize::Small;

    HICON icon = nullptr;
    if (!shellIconFor(extensionKey, isDirectory, largeIcon, &icon))
        return entry;

    const QImage image = imageFromHICON(icon);
    // 拿到之后必须 DestroyIcon：SHGetFileInfo 返回的 HICON 由调用方负责释放，
    // 漏掉的话每画一行泄漏一个图标句柄，GDI 对象数会在用户滚动列表时涨到上限
    // 然后整个程序画不出任何东西。
    ::DestroyIcon(icon);

    if (image.isNull())
        return entry;

    entry.source = IconSource::System;
    entry.actualPixelSize = image.width();
    entry.payload = QVariant::fromValue(QIcon(QPixmap::fromImage(image)));
    return entry;
}

} // namespace

IconProvider *createWindowsIconProvider()
{
    return new WindowsIconProvider();
}

} // namespace Platform
} // namespace LqCompare

#endif // Q_OS_WIN
