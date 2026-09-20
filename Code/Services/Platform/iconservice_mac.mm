/// \file
/// \brief macOS 的系统图标提供者（PRD: PLAT-004 完成标准第 1、5 条）。
///
/// 必须写成 Objective-C++：`NSWorkspace` 只有 AppKit 才有。
/// 与 trash_mac.mm 同理，写成 .mm 而不是把整个 Platform 模块都变成 ObjC++。

#include "iconkey.h"
#include "iconservice.h"

// 本文件只在 macOS 上参与编译：AppKit 在别的平台上不存在。
// 这里不能照搬 filesystem_posix.cpp 的 `!Q_OS_WIN` 大包——Linux 的实现
// 要 QtGui 的主题图标接口，macOS 的实现要 AppKit，二者不能出现在同一个
// 编译单元里（否则 Linux 也要背 ObjC++ 的编译代价，而且引用不到 AppKit）。
#ifdef Q_OS_MACOS

#include <QIcon>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace LqCompare {
namespace Platform {

namespace {

/// 把扩展名映射到 UTType（macOS 11+）。
///
/// 用 UTType 而不是 `iconForFileType:`：后者从 macOS 12 起已废弃，而且它
/// 要的是一个旧的 HFS 类型码或扩展名，对「没有扩展名的数据文件」只能靠
/// 传 0 之类的魔法值。
///
/// `API_AVAILABLE` 不是装饰，是**契约**：它声明「本函数只能在 macOS 11+
/// 执行」，于是编译期就能核对调用点是否真的做了可用性检查。
/// 少了它，编译器会为函数体内的每个 `UTType` 各报一条
/// `-Wunguarded-availability-new`（7 条），而正确的反应不是用
/// `#pragma clang diagnostic ignored` 把警告压掉——那会让「调用点漏了检查」
/// 这件事也一起被静音，而漏检查的后果是用户在 macOS 10.15 上点一下列表就崩。
/// 加上它之后，若哪天 `@available` 判断被删掉，报错点会精确落在**调用行**。
API_AVAILABLE(macos(11.0))
UTType *typeForExtension(const QString &extensionKey, bool isDirectory)
{
    if (isDirectory)
        return UTTypeFolder;

    if (extensionKey.isEmpty() || extensionKey == QLatin1String(IconKey::NoExtension))
        return UTTypeData;

    UTType *type = [UTType typeWithFilenameExtension:extensionKey.toNSString()];
    // 对没注册过的扩展名，typeWithFilenameExtension: 返回 nil（不是异常）。
    // 退回「数据文件」而不是放弃：用户看到一个通用文档图标，
    // 远好过看到一个空位。
    return type != nil ? type : UTTypeData;
}

///
/// \brief 把 NSImage 渲染成 QImage。
///
/// 为什么要自己开 NSBitmapImageRep 画一遍，而不是找现成的转换函数：
/// NSImage 是「矢量 + 多分辨率表示」的容器，不是像素缓冲。直接去取它的
/// 位图表示，在 Retina 上很容易拿到 16×16 的那一份表示（图标本来就带多个
/// 表示），于是高分屏上又变回模糊。显式按目标像素尺寸画进一个自建的位图，
/// 才是「按请求的尺寸取图」。
///
/// 像素格式用 `Format_RGBA8888_Premultiplied`，与 NSBitmapImageRep 的默认
/// 内存布局逐字节对应（R,G,B,A）。用 `Format_ARGB32_Premultiplied` 会在
/// 小端机器上把通道顺序读反——图标会变成「蓝脸」，而这类错误在灰度图标上
/// 完全看不出来。
///
QImage imageFromNSImage(NSImage *image, int pixels)
{
    if (image == nil || pixels <= 0)
        return QImage();

    [image setSize:NSMakeSize(pixels, pixels)];

    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:pixels
                          pixelsHigh:pixels
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];
    if (rep == nil)
        return QImage();

    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    if (context == nil) {
        [rep release];
        return QImage();
    }

    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [image drawInRect:NSMakeRect(0, 0, pixels, pixels)
             fromRect:NSZeroRect
            operation:NSCompositingOperationSourceOver
             fraction:1.0];
    [NSGraphicsContext restoreGraphicsState];

    const QImage shallow(reinterpret_cast<const uchar *>([rep bitmapData]),
                         static_cast<int>([rep pixelsWide]),
                         static_cast<int>([rep pixelsHigh]),
                         static_cast<int>([rep bytesPerRow]),
                         QImage::Format_RGBA8888_Premultiplied);

    // QImage 的上面这个构造函数是「浅引用」——它不接管那块内存。
    // 因此必须先 copy() 一份再释放 rep，否则 QImage 会指向已释放的缓冲，
    // 而那表现为「图标偶尔是花的」，只在内存被复用的时候出现。
    const QImage copy = shallow.copy();

    [rep release];
    return copy;
}

class MacIconProvider : public IconProvider
{
public:
    QString platformName() const override { return QStringLiteral("macos"); }

    IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) override;

private:
    /// 与 IconService 那把锁的分工：IconService 的锁保护的是「provider 本身
    /// 不是线程安全的」，这把锁保护的是「同一时刻只进一次 NSWorkspace」。
    /// 两层看着冗余，但宁可多一层，也不要让一个偶发崩溃出现在「滚动列表」
    /// 这种完全看不出因果的操作上。
    QMutex m_workspaceMutex;
};

IconEntry MacIconProvider::resolve(const QString &extensionKey, bool isDirectory, int pixelSize)
{
    IconEntry entry;

    if (pixelSize <= 0)
        return entry;

    // 必须显式开 autoreleasepool：解析是在后台线程里跑的，
    // 而 Qt 不会为它创建一个。没有池的话，NSWorkspace 返回的自动释放对象
    // 会一直不释放（并且系统会在控制台里抱怨），
    // 表现是「滚动一个大目录时内存一直涨」。
    @autoreleasepool {
        if (@available(macOS 11.0, *)) {
            NSImage *image = nil;
            {
                QMutexLocker locker(&m_workspaceMutex);
                image = [[NSWorkspace sharedWorkspace]
                        iconForContentType:typeForExtension(extensionKey, isDirectory)];
                // 在这个池的作用域内不需要 retain/release：
                // 出池之前对象一直有效。
            }

            if (image != nil) {
                const QImage pixels = imageFromNSImage(image, pixelSize);
                if (!pixels.isNull()) {
                    entry.source = IconSource::System;
                    entry.actualPixelSize = pixels.width();
                    entry.payload = QVariant::fromValue(QIcon(QPixmap::fromImage(pixels)));
                }
            }
        }
        // macOS 11 以下没有 UTType，这里刻意不退回 iconForFileType:：
        // 那条 API 已废弃，且需要一个与 UTType 不同的类型码体系，
        // 为它维护第二套映射的收益（支持 2020 年以前的系统）不值得。
        // 结果是老系统上一律走内置回退图标——明确、可用，不崩。
        //
        // 注意部署目标是 10.13（Qt 5.15.2 clang_64 的默认值），
        // 所以「下面这条分支真的会在老机器上被执行」不是假设：
        // 在没有系统图标的老系统上，用户看到的会是内置的通用文档/文件夹图标，
        // 而不是空白或崩溃。这条分支无法在本机（macOS 26）被走到，
        // 属「已写但未验证」，与 Windows 的几个实现同类——
        // 区别是它的失败代价只是图标不好看，不是功能不可用。
    }

    return entry;
}

} // namespace

IconProvider *createMacIconProvider()
{
    return new MacIconProvider();
}

} // namespace Platform
} // namespace LqCompare

#endif // Q_OS_MACOS
