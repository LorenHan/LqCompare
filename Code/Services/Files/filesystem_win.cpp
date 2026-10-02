#include "filesystem.h"
#include "pathutils.h"
#include "windowsreparse.h"
#include "windowsfiletime.h"

// 本文件是 Windows 实现。非 Windows 平台上整体不参与编译。
//
// 重解析点的字节解析在 windowsreparse.cpp 中，可在所有平台执行边界测试。
// 本文件的 Win32 调用仍需 Windows CI / 运行验证，纯解析测试不能代替它。
#ifndef Q_OS_WIN
#  error "filesystem_win.cpp 只能在 Windows 上编译"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <winioctl.h> // FSCTL_GET_REPARSE_POINT 不由所有版本的 windows.h 间接提供

namespace LqCompare {
namespace Files {

namespace {

// 纯解析器的公开常量与当前 Windows SDK 保持一致。
static_assert(WindowsReparse::SymbolicLinkTag == IO_REPARSE_TAG_SYMLINK,
              "Symbolic link tag must match the Windows SDK");
static_assert(WindowsReparse::MountPointTag == IO_REPARSE_TAG_MOUNT_POINT,
              "Mount point tag must match the Windows SDK");
static_assert(WindowsReparse::MaximumBufferSize == MAXIMUM_REPARSE_DATA_BUFFER_SIZE,
              "Reparse buffer limit must match the Windows SDK");

PathUtils::Style windowsStyle()
{
    return PathUtils::Style::windows();
}

/// FILETIME（100 纳秒为单位、UTC、1601 纪元）→ 内部 FileTime。
FileTime fileTimeFromWindows(const FILETIME &fileTime)
{
    ULARGE_INTEGER value;
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;

    qint64 nanoseconds;
    if (!WindowsFileTime::fromTicks(value.QuadPart, &nanoseconds))
        return FileTime();
    return FileTime::fromNanosecondsSinceEpoch(nanoseconds);
}

/// 内部 FileTime → FILETIME。不可表示的时间必须报告失败，不回绕或钳位。
bool fileTimeToWindows(const FileTime &time, FILETIME *result)
{
    quint64 ticks;
    if (!time.isValid() || !WindowsFileTime::toTicks(time.nanosecondsSinceEpoch(), &ticks))
        return false;
    ULARGE_INTEGER value;
    value.QuadPart = ticks;
    result->dwLowDateTime = value.LowPart;
    result->dwHighDateTime = value.HighPart;
    return true;
}

/// QString → 以 L'\0' 结尾的宽字符串。
///
/// 全程走 UTF-16 宽字符 API，不经过 ANSI 转换——一旦落到 ANSI 代码页，
/// 中文路径与含非当前代码页字符的路径就会乱码或直接失败（PRD: PLAT-007）。
LPCWSTR toWide(const QString &path)
{
    return reinterpret_cast<LPCWSTR>(path.utf16());
}

// 这里曾经有一个自己拼字符串的 lastErrorMessage()（"Win32 错误码 %1（%2）"）。
// PLAT-008 落地时把它去掉了，原因有两个：
//   1. 它和新的 errorDetail() 是同一件事的两份实现。两份实现的必然结果是
//      界面上同时出现「Win32 错误码 32（busy）」与
//      「Win32 32（ERROR_SHARING_VIOLATION）」两种写法——用户看到的是
//      两个系统在报错，排查时会以为是不同的故障。
//   2. 它当时没有任何调用点（grep 确认过）。留着一段没人用、又要跟着分类
//      一起改的重复实现，比删掉更容易出问题。
// 现在需要把 Win32 原始码变成可读文本时，用 errorDetail(fromWindowsError(code))。

/// 把 WIN32_FIND_DATAW 的属性位翻译成 FileAttributes。
FileAttributes attributesFromWin32(DWORD win32Attributes)
{
    FileAttributes attributes = FileAttribute::None;
    if (win32Attributes & FILE_ATTRIBUTE_READONLY)
        attributes |= FileAttribute::ReadOnly;
    if (win32Attributes & FILE_ATTRIBUTE_HIDDEN)
        attributes |= FileAttribute::Hidden;
    if (win32Attributes & FILE_ATTRIBUTE_SYSTEM)
        attributes |= FileAttribute::System;
    if (win32Attributes & FILE_ATTRIBUTE_ARCHIVE)
        attributes |= FileAttribute::Archive;
    // 重解析点涵盖符号链接、目录联接（junction）与挂载点。
    // 统一按「某种链接」处理：上层关心的是「这不是一个普通文件/目录」。
    if (win32Attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        attributes |= FileAttribute::SymLink;
    return attributes;
}

FileInfo infoFromWin32(const QString &path, const WIN32_FIND_DATAW &data)
{
    FileInfo info;
    info.path = path;
    info.name = PathUtils::fileName(path, windowsStyle());
    info.exists = true;

    info.attributes = attributesFromWin32(data.dwFileAttributes);
    info.isSymLink = info.attributes.testFlag(FileAttribute::SymLink);
    info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // 大小由高低两个 32 位拼成。目录也给了这个字段，但它不代表目录内容的
    // 总大小（Windows 一律低估），所以不要拿它当目录大小用。
    info.size = (static_cast<quint64>(data.nFileSizeHigh) << 32)
                | static_cast<quint64>(data.nFileSizeLow);

    info.lastModified = fileTimeFromWindows(data.ftLastWriteTime);
    info.lastAccessed = fileTimeFromWindows(data.ftLastAccessTime);
    info.created = fileTimeFromWindows(data.ftCreationTime);

    return info;
}

/// 用 FindFirstFileW 读单个条目的元数据。
///
/// 用 FindFirstFile 而不是 GetFileAttributesEx，是为了让 stat 与
/// enumerateDirectory 拿到**完全相同**的字段集合与语义——
/// 两条路径若用不同 API，就可能出现「列表里的时间和属性面板里的时间不一致」。
bool findFirst(const QString &path, WIN32_FIND_DATAW *out, ErrorCode *error)
{
    const HANDLE handle = ::FindFirstFileW(toWide(path), out);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error)
            *error = fromWindowsError(::GetLastError());
        return false;
    }
    ::FindClose(handle);
    if (error)
        *error = FileSystemError::None;
    return true;
}

///
/// Windows 文件系统实现。
///
class WindowsFileSystem : public FileSystem
{
public:
    Qt::CaseSensitivity caseSensitivity() const override
    {
        // NTFS 默认大小写不敏感。注意这是**默认**：NTFS 支持按目录开启
        // 大小写敏感（WSL 会用到），此时这里给出的答案就不准了。
        // 更准确的判断需要查询每个目录的标志位，属后续工作（PLAT-007）。
        return Qt::CaseInsensitive;
    }

    QChar separator() const override { return QLatin1Char('\\'); }

    QString pathNormalize(const QString &path, ErrorCode *error) const override
    {
        if (error)
            *error = FileSystemError::None;
        return PathUtils::normalize(path, windowsStyle());
    }

    bool isAbsolutePath(const QString &path) const override
    {
        return PathUtils::isAbsolute(path, windowsStyle());
    }

    QString toNativePath(const QString &path) const override
    {
        // 顺序很重要：**先规范化，再加长路径前缀**。
        // 加前缀之后系统不再解析 "." / ".."，也不会做大小写折叠，
        // 所以必须把规范化做完再交给系统。
        const QString normalized = PathUtils::normalize(path, windowsStyle());
        return PathUtils::toExtendedPath(normalized, windowsStyle());
    }

    FileInfo stat(const QString &path, ErrorCode *error) const override
    {
        // FindFirstFileW 对符号链接返回的是链接自身的属性，
        // 与 POSIX 的 lstat 语义一致（不是 GetFileAttributesEx 的跟随语义）。
        WIN32_FIND_DATAW data;
        if (!findFirst(path, &data, error))
            return FileInfo();
        return infoFromWin32(path, data);
    }

    QString linkTarget(const QString &path, ErrorCode *error) const override
    {
        WIN32_FIND_DATAW data;
        if (!findFirst(path, &data, error))
            return QString();

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
            if (error)
                *error = FileSystemError::NotSupported;
            return QString();
        }

        // 打开句柄时带 FILE_FLAG_OPEN_REPARSE_POINT，让系统打开链接本身
        // 而不是它的目标——否则我们会读到目标的属性，而不是链接的指向。
        const HANDLE handle = ::CreateFileW(
            toWide(toNativePath(path)), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (handle == INVALID_HANDLE_VALUE) {
            if (error)
                *error = fromWindowsError(::GetLastError());
            return QString();
        }

        // 重解析点数据最多 16 KB；解析时仍必须遵守系统实际返回的字节数。
        QByteArray buffer(WindowsReparse::MaximumBufferSize, Qt::Uninitialized);
        DWORD returned = 0;
        const BOOL ok = ::DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                                          buffer.data(),
                                          static_cast<DWORD>(buffer.size()), &returned, nullptr);
        // CloseHandle 也可能改写线程错误码，必须先保存 DeviceIoControl 的失败原因。
        const DWORD code = ok ? ERROR_SUCCESS : ::GetLastError();
        ::CloseHandle(handle);

        if (!ok) {
            if (error)
                *error = fromWindowsError(code);
            return QString();
        }

        const QString raw = WindowsReparse::target(buffer, returned);
        if (raw.isEmpty()) {
            if (error)
                *error = FileSystemError::NotSupported;
            return QString();
        }

        if (error)
            *error = FileSystemError::None;
        return PathUtils::normalize(raw, windowsStyle());
    }

    bool exists(const QString &path, ErrorCode *error) const override
    {
        WIN32_FIND_DATAW data;
        return findFirst(path, &data, error);
    }

    QVector<FileInfo> enumerateDirectory(const QString &path, ErrorCode *error) const override
    {
        QVector<FileInfo> entries;

        // FindFirstFileW 的通配符要求路径以 "\*" 结尾；
        // 这里用规范化后的路径，避免 "C:\" 变成 "C:\\*"。
        const QString normalized = PathUtils::normalize(path, windowsStyle());
        QString pattern = normalized;
        if (!pattern.endsWith(QLatin1Char('\\')))
            pattern += QLatin1Char('\\');
        pattern += QLatin1Char('*');

        WIN32_FIND_DATAW data;
        const HANDLE handle = ::FindFirstFileW(toWide(pattern), &data);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD code = ::GetLastError();
            // ERROR_FILE_NOT_FOUND 表示目录确实存在但没有内容——
            // 这是「空目录」而不是「失败」。把它当失败会让文件夹比对
            // 把「目标为空」误判成「读不到目标」。
            if (error) {
                // ERROR_FILE_NOT_FOUND 时用空的 ErrorCode 表示成功（读到了「空目录」）。
                // 这里刻意不给它带原始码：带上的话，一个成功的路径上会残留
                // 一个看起来像故障的错误码，排查时反而误导。
                *error = (code == Win32Error::FileNotFound) ? ErrorCode()
                                                            : fromWindowsError(code);
            }
            return entries;
        }

        const QChar separator = QLatin1Char('\\');
        const bool pathEndsWithSeparator = normalized.endsWith(separator);

        for (;;) {
            const QString name = QString::fromWCharArray(data.cFileName);
            // 跳过 "." 与 ".."：它们是目录项而不是内容。
            if (name != QLatin1String(".") && name != QLatin1String("..")) {
                const QString childPath =
                    pathEndsWithSeparator ? normalized + name : normalized + separator + name;
                entries.append(infoFromWin32(childPath, data));
            }

            if (!::FindNextFileW(handle, &data))
                break;
        }

        ::FindClose(handle);

        if (error)
            *error = FileSystemError::None;
        return entries;
    }

    bool setTimes(const QString &path, const FileTime &lastModified,
                  const FileTime &lastAccessed, ErrorCode *error) const override
    {
        FILETIME modified;
        FILETIME accessed;
        // 所有指定字段先校验，再打开文件；失败时不能只写入一部分时间。
        if ((lastModified.isValid() && !fileTimeToWindows(lastModified, &modified))
            || (lastAccessed.isValid() && !fileTimeToWindows(lastAccessed, &accessed))) {
            if (error)
                *error = FileSystemError::NotSupported;
            return false;
        }
        // 传 nullptr 表示「该时间保持不变」。绝不能填当前时间——
        // 那会把「只改修改时间」变成「顺手改掉访问时间」。
        const FILETIME *pModified = lastModified.isValid() ? &modified : nullptr;
        const FILETIME *pAccessed = lastAccessed.isValid() ? &accessed : nullptr;

        // 需要 FILE_WRITE_ATTRIBUTES 才能改时间戳。用 BACKUP_SEMANTICS
        // 以便对目录也生效（否则目录会因缺少 FILE_FLAG_BACKUP_SEMANTICS 而无法打开）。
        const HANDLE handle = ::CreateFileW(
            toWide(toNativePath(path)), FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);

        if (handle == INVALID_HANDLE_VALUE) {
            if (error)
                *error = fromWindowsError(::GetLastError());
            return false;
        }

        // 第一个参数（创建时间）恒传 nullptr：本接口不提供修改创建时间的能力。
        const BOOL ok = ::SetFileTime(handle, nullptr, pAccessed, pModified);
        // 关闭句柄可能改写线程错误码，先保留实际设置失败的原因。
        const DWORD code = ok ? ERROR_SUCCESS : ::GetLastError();
        ::CloseHandle(handle);

        if (!ok) {
            if (error)
                *error = fromWindowsError(code);
            return false;
        }
        if (error)
            *error = FileSystemError::None;
        return true;
    }

    bool setAttributes(const QString &path, FileAttributes attributes,
                       ErrorCode *error) const override
    {
        WIN32_FIND_DATAW data;
        if (!findFirst(path, &data, error))
            return false;

        // 只改被点名的属性位，其余原样保留。
        // 注意 FILE_ATTRIBUTE_DIRECTORY 与 REPARSE_POINT 不能被误改，
        // 因此只在确有对应属性时才动它。
        DWORD win32Attributes = data.dwFileAttributes;
        auto apply = [&win32Attributes](DWORD flag, bool enabled) {
            if (enabled)
                win32Attributes |= flag;
            else
                win32Attributes &= ~flag;
        };

        apply(FILE_ATTRIBUTE_READONLY, attributes.testFlag(FileAttribute::ReadOnly));
        apply(FILE_ATTRIBUTE_HIDDEN, attributes.testFlag(FileAttribute::Hidden));
        apply(FILE_ATTRIBUTE_SYSTEM, attributes.testFlag(FileAttribute::System));
        apply(FILE_ATTRIBUTE_ARCHIVE, attributes.testFlag(FileAttribute::Archive));

        if (!::SetFileAttributesW(toWide(toNativePath(path)), win32Attributes)) {
            if (error)
                *error = fromWindowsError(::GetLastError());
            return false;
        }
        if (error)
            *error = FileSystemError::None;
        return true;
    }

    QString platformName() const override { return QStringLiteral("windows"); }

    // deleteToTrash 不在这里覆写：真实实现属 PLAT-003
    // （SHFileOperation / IFileOperation 带 FOF_ALLOWUNDO）。
    // 目前由基类返回 NotSupported，绝不会静默变成永久删除。
};

} // namespace

FileSystem *createWindowsFileSystem()
{
    return new WindowsFileSystem();
}

} // namespace Files
} // namespace LqCompare
