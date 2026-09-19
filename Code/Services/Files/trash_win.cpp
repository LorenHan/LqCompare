// Windows 的回收站实现（PRD: PLAT-003 完成标准第 1 条）。
//
// 注意：本文件在编写时**没有在 Windows 上编译验证过**（本机只有 macOS 版 Qt）。
//   与 filesystem_win.cpp 一样，它属于「已写出但未在目标平台验证」的代码。
//   PLAT-003 的对应完成标准因此标为部分完成，issue 上写明了这一点。
//
// 为什么用 SHFileOperation 而不是 IFileOperation
// -------------------------------------------
// 规格写的是「SHFileOperation / IFileOperation」，两者都算数。选前者的原因：
// IFileOperation 是 COM 接口，要用它得先 CoInitializeEx、再实现一个
// IFileOperationProgressSink 来接收结果，代码量是后者的十倍；而它相对
// SHFileOperation 的优势（更好的进度回报、支持 IShellItem）在本项目里
// 用不上——进度由我们自己的界面画，任务粒度是「一批文件」而不是单文件。
//
// 关键的一条平台特性：FOF_ALLOWUNDO 在网络盘上会被**静默忽略**
// ---------------------------------------------------------
// Windows 对网络位置与部分可移动盘的删除没有回收站。此时 SHFileOperation
// 即使带了 FOF_ALLOWUNDO 也会直接永久删除，并且**返回成功**。
//
// 这正是 PLAT-003 强调「回收站不可用时必须明确告知并让用户选择」的原因：
// 在这类位置上，「删除成功」这四个字背后可能是一次不可逆的永久删除。
// 因此 availabilityFor() 的探测不是可有可无的优化，而是唯一的安全闸门。

#include "trash.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <string>

// windows.h 必须在 shellapi.h 之前：后者依赖前者里的基础类型与宏。
#include <windows.h>
#include <shellapi.h>

namespace LqCompare {
namespace Files {

namespace {

/// 路径所在的卷根（GetDriveTypeW / SHQueryRecycleBinW 都需要一个根路径）。
/// 拿不到时返回空串。
QString volumeRootOf(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);

    // UNC：\\server\share\... -> \\server\share
    if (native.startsWith(QStringLiteral("\\\\"))) {
        const QStringList parts = native.mid(2).split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            return QString();
        return QStringLiteral("\\\\") + parts.at(0) + QLatin1Char('\\') + parts.at(1);
    }

    // 盘符：C:\... -> C:\
    if (native.size() >= 2 && native.at(1) == QLatin1Char(':'))
        return native.left(2) + QLatin1Char('\\');

    // 相对路径、设备路径（\\?\...）等：不猜，交给调用方按 Unknown 处理。
    return QString();
}

LPCWSTR wide(const QString &text)
{
    // Qt 在 Windows 上内部就是 UTF-16，utf16() 得到的就是宽字符。
    // 用 reinterpret_cast 而不是 toStdWString()：后者会多复制一份，
    // 而下面几个调用都只在语句内使用返回的指针，生命周期够。
    return reinterpret_cast<LPCWSTR>(text.utf16());
}

/// 该卷是否可能没有回收站。
///
/// 判据是驱动类型而不是「路径里有没有 \\」：映射的网络驱动器（Z:\）
/// 看起来和本地盘一样，但删除时没有回收站——只按路径形状判断会漏掉它，
/// 而漏掉的后果是静默永久删除。
bool volumeHasNoRecycleBin(const QString &volumeRoot)
{
    if (volumeRoot.isEmpty())
        return true; // 判不出来时保守处理：宁可让用户多确认一次

    const UINT driveType = ::GetDriveTypeW(wide(volumeRoot));

    switch (driveType) {
    case DRIVE_REMOTE:      // 网络驱动器与 UNC（GetDriveTypeW 对 UNC 返回它）
    case DRIVE_CDROM:
    case DRIVE_NO_ROOT_DIR: // 路径无效
        return true;
    case DRIVE_REMOVABLE:
        // 可移动盘**可以**有回收站（$Recycle.Bin 就在盘上），因此不一律判死。
        // 到底有没有要实际去问回收站，见下面的 SHQueryRecycleBinW。
        break;
    default:
        break;
    }

    return false;
}

} // namespace

///
/// \brief Windows 回收站后端。
///
class WindowsTrashService : public TrashService
{
public:
    QString platformName() const override { return QStringLiteral("windows"); }

    TrashAvailability availabilityFor(const QString &path) const override;

    QString displayLocation() const override
    {
        // Windows 的回收站没有普通的文件系统路径——它是每个卷根的
        // $Recycle.Bin 下一组按用户 SID 分目录的 $R/$I 文件。
        // 界面上应该显示的是这个东西的命名空间位置，点它才会打开回收站。
        return QStringLiteral("shell:RecycleBinFolder");
    }

    bool undoLastDelete(FileSystemError *error) const override;

protected:
    TrashReport trashPaths(const QStringList &paths) const override;
};

TrashAvailability WindowsTrashService::availabilityFor(const QString &path) const
{
    const QString volumeRoot = volumeRootOf(path);
    if (volumeRoot.isEmpty())
        return TrashAvailability::Unknown;

    if (volumeHasNoRecycleBin(volumeRoot))
        return TrashAvailability::VolumeNotSupported;

    // 剩余空间。为 0 时删进去也放不下，而且建议（清理磁盘）与其他原因不同。
    ULARGE_INTEGER freeBytesAvailable = {};
    if (::GetDiskFreeSpaceExW(wide(volumeRoot), &freeBytesAvailable, nullptr, nullptr)) {
        if (freeBytesAvailable.QuadPart == 0)
            return TrashAvailability::NoSpace;
    }

    // 问一次这台机器上回收站的实际使用情况。
    //
    // 为什么只用它来确认「回收站是否启用」而不判配额：Windows 的**配额**
    // （MaxCapacity）存在注册表 HKCU\...\BitBucket\Volume\{GUID} 里，按卷的
    // GUID 分开存放，读它要绕过 Explorer 的私有结构；而 SHQueryRecycleBin
    // 只返回当前总大小与条目数，不返回上限。因此配额是否超限只有真正搬移时
    // 才知道，那时 SHFileOperation 会失败，由 trashPaths() 归类。
    //
    // 这里能确认的是「这个卷上回收站是活的」：查得成功说明该卷有回收站。
    SHQUERYRBINFO query = {};
    query.cbSize = sizeof(query);
    const HRESULT result = ::SHQueryRecycleBinW(wide(volumeRoot), &query);

    if (result != S_OK)
        return TrashAvailability::VolumeNotSupported;

    return TrashAvailability::Available;
}

TrashReport WindowsTrashService::trashPaths(const QStringList &paths) const
{
    TrashReport report;

    // SHFileOperationW 一次性接收整批路径，而不是逐个调用。
    //
    // 逐个调用会让「回收站配额刚好卡在中间」这种情况产生一半成功一半失败的现场，
    // 而且每个条目都会闪一下系统的进度窗口。整批一次交给 Shell，它自己会
    // 决定怎么排进度，失败时也会给出统一的结果。
    //
    // 注意：调用方（基类 deleteToTrash）已经保证过这一批的可用性全都通过，
    // 因此这里不会遇到「其中某个在无回收站的卷上」的情况。
    std::wstring batch;
    for (const QString &path : paths) {
        batch += QDir::toNativeSeparators(path).toStdWString();
        batch.push_back(L'\0'); // 路径之间用 NUL 分隔
    }
    batch.push_back(L'\0');     // 整体再补一个 NUL 结尾

    SHFILEOPSTRUCTW operation = {};
    operation.wFunc = FO_DELETE;
    operation.pFrom = batch.c_str();
    operation.fFlags = FOF_ALLOWUNDO        // 移到回收站而不是永久删除
            | FOF_NOCONFIRMATION            // 是否删除由我们自己的界面问，不要系统再问一次
            | FOF_NOERRORUI                 // 错误交给我们展示，不要弹系统对话框
            | FOF_SILENT;                   // 进度由我们自己的界面画

    const int shellResult = ::SHFileOperationW(&operation);

    const bool aborted = operation.fAnyOperationsAborted != FALSE;
    const bool failed = (shellResult != 0) || aborted;

    for (const QString &path : paths) {
        TrashRecord record;
        record.originalPath = path;

        if (!failed) {
            record.error = FileSystemError::None;
            // trashedPath 刻意留空：Windows 不通过任何公开 API 告诉我们
            // 条目在回收站里叫什么名字（那是 $Recycle.Bin\<SID>\$R<随机串>，
            // 由 Shell 生成并且不对外暴露）。填一个猜出来的路径比留空更糟——
            // 上层会以为它可以拿去还原，然后失败在一个莫名其妙的地方。
            //
            // 这也正是 undoLastDelete() 在 Windows 上直接返回 NotSupported 的
            // 原因（见下面的说明）。
        } else if (aborted) {
            // fAnyOperationsAborted 为真表示用户或系统中断了操作。
            // 此时**无法知道**哪些条目已经删掉、哪些没有——Shell 不提供这个粒度。
            // 报 Busy 而不是 PermissionDenied：Busy 是可重试的，
            // 而这里正确的处置正是「过一会儿再试一次」。
            record.error = FileSystemError::Busy;
        } else {
            record.error = classifyWindowsErrorCode(static_cast<unsigned long>(shellResult));
        }

        report.records.append(record);
    }

    return report;
}

bool WindowsTrashService::undoLastDelete(FileSystemError *error) const
{
    // Windows 上无法还原「最近一次删除」——这是平台能力限制，不是没实现。
    //
    // 原因：回收站里每个条目是一对文件（$R<id> 是内容、$I<id> 是元数据），
    // 它们不是普通文件，要靠 Shell 的命名空间扩展（枚举回收站文件夹、
    // 读取 I 文件里的原始路径、再调用「还原」动词）才能操作。
    // 这需要 COM 与 IShellFolder 的完整实现，而且要处理 SID 归属与多卷情况。
    //
    // PLAT-003 的完成标准对这一点写的是「受平台能力限制时说明」，因此这里
    // 明确返回 NotSupported 并给出说明，而不是假装成功或悄悄什么都不做。
    //
    // 用户在 Windows 上仍然可以通过资源管理器手工还原：打开回收站、
    // 右键「还原」。displayLocation() 给出的 shell:RecycleBinFolder 就是为了
    // 让界面能提供「打开回收站」这个入口。
    if (error)
        *error = FileSystemError::NotSupported;
    return false;
}

// 工厂函数，由 trash.cpp 声明并分发。
TrashService *createWindowsTrashService()
{
    return new WindowsTrashService();
}

} // namespace Files
} // namespace LqCompare
