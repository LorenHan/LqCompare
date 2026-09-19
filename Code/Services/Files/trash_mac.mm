// macOS 的回收站实现（PRD: PLAT-003）。
//
// 为什么这个文件是 .mm 而不是 .cpp
// ------------------------------
// macOS 上能正确使用废纸篓的 API 只有一个：NSFileManager 的
// trashItemAtURL:resultingItemURL:error:。它属于 Foundation，必须用
// Objective-C++ 才能调用。
//
// 不用别的手段绕开的理由：
//   - 自己把文件 rename 进 ~/.Trash 看起来能用，但会丢掉「放回原处」所需的元数据，
//     而且重名时不会自动改名，会直接覆盖废纸篓里已有的同名文件。
//   - 调 osascript 让 Finder 去删，会启动另一个进程，慢、有窗口焦点问题、
//     并且在测试环境（无 Finder 会话）下不可靠。
//   - 这两个替代方案都会让「撤销上一次删除」变得不可靠，而那是 PLAT-003 的
//     完成标准之一。
//
// 所以老老实实用 .mm。好在这个文件只有真正搬文件的那几十行是 ObjC，
// 其余（可用性判定、错误分类、撤销）都是普通 C++，可以在 macOS 上直接跑测试。

#include "trash.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <sys/stat.h>
#include <unistd.h>

#import <Foundation/Foundation.h>

namespace LqCompare {
namespace Files {

// -----------------------------------------------------------------------------
// 编译期核对手写常量
// -----------------------------------------------------------------------------

// 与 filesystem.cpp 里核对 Win32 常量的做法完全对称：这里能同时看到
// Foundation 的真实常量与 filesystem.h 里的手写常量，写错任何一个都是
// **编译期**失败。这是把手写常量放在平台无关层换来的可测性所必须付出的补偿。
static_assert(CocoaError::NoSuchFile == NSFileNoSuchFileError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::FileLocking == NSFileLockingError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::ReadNoPermission == NSFileReadNoPermissionError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::WriteNoPermission == NSFileWriteNoPermissionError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::WriteInvalidFileName == NSFileWriteInvalidFileNameError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::WriteFileExists == NSFileWriteFileExistsError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::WriteOutOfSpace == NSFileWriteOutOfSpaceError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::WriteVolumeReadOnly == NSFileWriteVolumeReadOnlyError, "Cocoa 错误码常量不匹配");
static_assert(CocoaError::ManagerUnmountBusy == NSFileManagerUnmountBusyError, "Cocoa 错误码常量不匹配");

namespace {

/// 把 NSError 归类成本项目的错误分类。
///
/// 先看 domain 再看 code：160 与 22 这类数字在两个域里含义完全不同，
/// 不分域直接比数字会把「文件正在被写入」误判成「无效参数」。
FileSystemError classifyNSError(NSError *error)
{
    if (error == nil)
        return FileSystemError::Unknown;

    if ([error.domain isEqualToString:NSCocoaErrorDomain])
        return classifyCocoaError(static_cast<long>([error code]));

    // POSIX 域里放的就是 errno，直接交给已有的分类函数。
    // `NSPOSIXErrorDomain` 与 `NSOSStatusErrorDomain` 是另外两个常见的域，
    // 后者（Carbon 时代的 OSStatus）在文件系统操作里极少出现，落到 Unknown。
    if ([error.domain isEqualToString:NSPOSIXErrorDomain])
        return classifySystemError(static_cast<int>([error code]));

    return FileSystemError::Unknown;
}

/// 路径所在的卷根 URL（例如 / 或 /Volumes/USB）。
/// 拿不到时返回 nil，调用方按 Unknown 处理。
NSURL *volumeURLForPath(NSURL *url)
{
    NSURL *volumeURL = nil;
    NSError *error = nil;
    if (![url getResourceValue:&volumeURL forKey:NSURLVolumeURLKey error:&error])
        return nil;
    return volumeURL;
}

} // namespace

///
/// \brief macOS 回收站后端。
///
class MacTrashService : public TrashService
{
public:
    QString platformName() const override { return QStringLiteral("macos"); }

    TrashAvailability availabilityFor(const QString &path) const override;

    QString displayLocation() const override
    {
        // macOS 的废纸篓固定在家目录下。返回绝对路径而不是 "~/.Trash"：
        // 界面显示「已移到 /Users/xxx/.Trash」时用户可以直接拿去用，
        // 而 "~" 在图形界面里没法直接粘贴到文件对话框。
        return QDir::homePath() + QStringLiteral("/.Trash");
    }

    bool undoLastDelete(FileSystemError *error) const override;

protected:
    TrashReport trashPaths(const QStringList &paths) const override;
};

TrashAvailability MacTrashService::availabilityFor(const QString &path) const
{
    // 注意：探测 MUST NOT 因为路径不存在就返回 Unknown。
    // 删除前判断可用性时文件通常还在，但同步场景下它可能刚被别的进程处理掉，
    // 那种时候用户要看到的是「回收站可用、但文件已经没了」，
    // 而不是「无法判断回收站是否可用」——后者会让人以为系统出了问题。
    // getResourceValue: 对不存在的路径也能取到卷信息，正合此用。
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
        if (url == nil)
            return TrashAvailability::Unknown;

        NSURL *volumeURL = volumeURLForPath(url);

        // 先看空间。空间为 0 时无论回收站在哪都放不下，不必再往下判断——
        // 而且这个原因的处置建议（清理磁盘）和「这个卷没有回收站」
        // （换位置）完全不同，所以必须区分开。
        if (volumeURL != nil) {
            NSNumber *capacity = nil;
            NSError *capacityError = nil;
            if ([volumeURL getResourceValue:&capacity
                                      forKey:NSURLVolumeAvailableCapacityKey
                                       error:&capacityError]
                && capacity != nil && [capacity longLongValue] <= 0) {
                return TrashAvailability::NoSpace;
            }
        }

        // 是否本地卷。非本地卷（网络共享、部分云端文件提供器）能不能用废纸篓
        // 取决于服务器和协议，macOS 会退而把条目放进本地的 .Trashes 目录，
        // 但也常常直接失败。这里做一次「卷根有没有可写的 .Trashes」的判断，
        // 比一刀切「非本地卷一律不支持」准确——后者会把本来能删的拦下来。
        NSNumber *isLocal = nil;
        NSError *error = nil;
        if (![url getResourceValue:&isLocal forKey:NSURLVolumeIsLocalKey error:&error])
            return TrashAvailability::Unknown;

        if ([isLocal boolValue])
            return TrashAvailability::Available;

        if (volumeURL == nil)
            return TrashAvailability::VolumeNotSupported;

        NSString *perVolumeTrash = [[volumeURL path] stringByAppendingPathComponent:@".Trashes"];
        NSFileManager *manager = [NSFileManager defaultManager];
        if ([manager isWritableFileAtPath:perVolumeTrash])
            return TrashAvailability::Available;

        return TrashAvailability::VolumeNotSupported;
    }

    // 这里永远不会返回 QuotaExceeded：macOS 的废纸篓没有「按卷配额」这个概念，
    // 它只受卷剩余空间约束（已由上面的 NoSpace 覆盖）。保留该状态是为了让
    // 上层不用按平台分支——Windows 实现会用到它。
}

TrashReport MacTrashService::trashPaths(const QStringList &paths) const
{
    TrashReport report;

    @autoreleasepool {
        NSFileManager *manager = [NSFileManager defaultManager];

        for (const QString &path : paths) {
            TrashRecord record;
            record.originalPath = path;

            NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
            NSURL *resultingURL = nil;
            NSError *error = nil;

            if ([manager trashItemAtURL:url resultingItemURL:&resultingURL error:&error]) {
                // resultingItemURL 是**回收站里的实际路径**，必须记下来。
                // 废纸篓里已有同名文件时 macOS 会自动改名（"a.txt" -> "a 2.txt"），
                // 想当然按原名拼路径是错的，撤销会因此找不到文件。
                record.error = FileSystemError::None;
                record.trashedPath = QString::fromNSString([resultingURL path]);
            } else {
                record.error = classifyNSError(error);
            }

            report.records.append(record);
        }
    }

    return report;
}

bool MacTrashService::undoLastDelete(FileSystemError *error) const
{
    const TrashReport report = lastDelete();

    if (report.records.isEmpty()) {
        // 「没有可还原的删除」和「还原失败」是两回事，用 NotFound 区分于
        // 权限之类的失败，界面才能给出「还没有删除过」而不是「还原出错」。
        if (error)
            *error = FileSystemError::NotFound;
        return false;
    }

    @autoreleasepool {
        NSFileManager *manager = [NSFileManager defaultManager];

        for (const TrashRecord &record : report.records) {
            // 上次删除里失败的那些条目本来就没进废纸篓，没有可还原的东西。
            // 跳过而不是报错：撤销的语义是「把刚才那次删除撤掉」，
            // 而那些条目本来就还在原处，撤销的目的已经达成了。
            if (!record.succeeded() || record.trashedPath.isEmpty())
                continue;

            // 原位置已被别的文件占用时**不能**覆盖。覆盖会删掉一个用户没打算
            // 碰的文件，而且那个文件不在废纸篓里、无法恢复——这是比「还原失败」
            // 严重得多的后果，所以宁可直接失败。
            if ([manager fileExistsAtPath:record.originalPath.toNSString()]) {
                if (error)
                    *error = FileSystemError::AlreadyExists;
                return false;
            }

            NSURL *fromURL = [NSURL fileURLWithPath:record.trashedPath.toNSString()];
            NSURL *toURL = [NSURL fileURLWithPath:record.originalPath.toNSString()];
            NSError *moveError = nil;

            if (![manager moveItemAtURL:fromURL toURL:toURL error:&moveError]) {
                if (error)
                    *error = classifyNSError(moveError);
                return false;
            }
        }
    }

    // 全部还原成功后才清掉撤销点。为什么清：撤销的语义是「还原最近一次删除」，
    // 还原过之后就没有「待还原的那一次」了。不清的话，用户会以为还能再撤销一次，
    // 而第二次撤销会把已经回到原处的文件再挪走——那才是真正的数据丢失。
    //
    // 部分失败时保持原记录不动，让用户可以修掉障碍（关掉占用文件的程序）后重试；
    // 已还原的条目会因为「原位置已存在」而在下一次进入上面那个 AlreadyExists 分支，
    // 提示用户手工处理，而不是被静默跳过。
    m_lastDelete = TrashReport();

    if (error)
        *error = FileSystemError::None;
    return true;
}

// 工厂函数，由 trash.cpp 声明并分发。
TrashService *createMacTrashService()
{
    return new MacTrashService();
}

} // namespace Files
} // namespace LqCompare
