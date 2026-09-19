#include "filesystem.h"
#include "pathutils.h"

// 本文件是 Windows 实现。非 Windows 平台上整体不参与编译。
//
// ⚠ 未在本机编译验证
// -------------------
// 当前开发机是 macOS，没有 Windows 下的 Qt，因此**本文件没有被编译器检查过**。
// 这一点必须说清楚，不能让它看起来和 filesystem_posix.cpp 一样可靠：
//   - POSIX 实现：本机可编译、可运行、有测试覆盖。
//   - Windows 实现：仅经人工检查，首次在 Windows 上构建时很可能需要修语法/类型问题。
//
// 为降低这个风险，两件事已经做在前面：
//   1. 路径规则（分隔符、盘符、UNC、长路径前缀）全部提取到 pathutils.cpp，
//      它是平台无关的纯字符串逻辑，已在 macOS 上被完整测试——包括 Windows 规则。
//   2. Win32 错误码常量在 Windows 下由 static_assert 与本头文件里的真实常量比对
//      （见 filesystem.cpp），写错会在编译期失败。
// 剩下的就是这层薄薄的 API 调用，只能靠首次在 Windows 上构建来验证。
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

namespace LqCompare {
namespace Files {

namespace {

PathUtils::Style windowsStyle()
{
    return PathUtils::Style::windows();
}

/// Windows 的 FILETIME 纪元是 1601-01-01，而我们的内部表示以 1970-01-01 为 0。
/// 两者相差 11644473600 秒，这个常量是固定的，不随时区变化。
constexpr qint64 kSecondsBetween1601And1970 = 11644473600LL;
constexpr qint64 kNanosecondsPerSecond = 1000000000LL;

/// FILETIME（100 纳秒为单位、UTC、1601 纪元）→ 内部 FileTime。
FileTime fileTimeFromWindows(const FILETIME &fileTime)
{
    ULARGE_INTEGER value;
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;

    // FILETIME 的单位是 100 纳秒，乘 100 得到纳秒。
    // 用 unsigned 组合完再转有符号，避免先转有符号时高位被当成符号位。
    const qint64 hundredsOfNanoseconds = static_cast<qint64>(value.QuadPart);
    const qint64 nanoseconds = hundredsOfNanoseconds * 100;
    return FileTime::fromNanosecondsSinceEpoch(
        nanoseconds - kSecondsBetween1601And1970 * kNanosecondsPerSecond);
}

/// 内部 FileTime → FILETIME。传回的 FileTime 必须有效，调用方负责判断。
FILETIME fileTimeToWindows(const FileTime &time)
{
    ULARGE_INTEGER value;
    value.QuadPart = static_cast<ULONGLONG>(
        time.nanosecondsSinceEpoch() + kSecondsBetween1601And1970 * kNanosecondsPerSecond);

    FILETIME result;
    result.dwLowDateTime = value.LowPart;
    result.dwHighDateTime = value.HighPart;
    return result;
}

/// QString → 以 L'\0' 结尾的宽字符串。
///
/// 全程走 UTF-16 宽字符 API，不经过 ANSI 转换——一旦落到 ANSI 代码页，
/// 中文路径与含非当前代码页字符的路径就会乱码或直接失败（PRD: PLAT-007）。
LPCWSTR toWide(const QString &path)
{
    return reinterpret_cast<LPCWSTR>(path.utf16());
}

QString lastErrorMessage()
{
    const DWORD code = ::GetLastError();
    return QStringLiteral("Win32 错误码 %1（%2）")
        .arg(static_cast<uint>(code))
        .arg(QString::fromLatin1(errorIdentifier(classifyWindowsErrorCode(code))));
}

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
bool findFirst(const QString &path, WIN32_FIND_DATAW *out, FileSystemError *error)
{
    const HANDLE handle = ::FindFirstFileW(toWide(path), out);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error)
            *error = classifyWindowsErrorCode(::GetLastError());
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

    QString pathNormalize(const QString &path, FileSystemError *error) const override
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

    FileInfo stat(const QString &path, FileSystemError *error) const override
    {
        // FindFirstFileW 对符号链接返回的是链接自身的属性，
        // 与 POSIX 的 lstat 语义一致（不是 GetFileAttributesEx 的跟随语义）。
        WIN32_FIND_DATAW data;
        if (!findFirst(path, &data, error))
            return FileInfo();
        return infoFromWin32(path, data);
    }

    QString linkTarget(const QString &path, FileSystemError *error) const override
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
                *error = classifyWindowsErrorCode(::GetLastError());
            return QString();
        }

        // 一个重解析点数据最多约 16 KB，用固定缓冲足够；
        // 返回的 size 会告诉我们实际长度。
        QByteArray buffer(MAXIMUM_REPARSE_DATA_BUFFER_SIZE, Qt::Uninitialized);
        DWORD returned = 0;
        const BOOL ok = ::DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                                          buffer.data(),
                                          static_cast<DWORD>(buffer.size()), &returned, nullptr);
        ::CloseHandle(handle);

        if (!ok) {
            if (error)
                *error = classifyWindowsErrorCode(::GetLastError());
            return QString();
        }

        const auto *reparse = reinterpret_cast<const REPARSE_DATA_BUFFER *>(buffer.constData());
        const QString raw = extractReparseTarget(reparse);
        if (raw.isEmpty()) {
            if (error)
                *error = FileSystemError::NotSupported;
            return QString();
        }

        if (error)
            *error = FileSystemError::None;
        return PathUtils::normalize(raw, windowsStyle());
    }

    bool exists(const QString &path, FileSystemError *error) const override
    {
        WIN32_FIND_DATAW data;
        return findFirst(path, &data, error);
    }

    QVector<FileInfo> enumerateDirectory(const QString &path, FileSystemError *error) const override
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
                *error = (code == Win32Error::FileNotFound)
                             ? FileSystemError::None
                             : classifyWindowsErrorCode(code);
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
                  const FileTime &lastAccessed, FileSystemError *error) const override
    {
        // 需要 FILE_WRITE_ATTRIBUTES 才能改时间戳。用 BACKUP_SEMANTICS
        // 以便对目录也生效（否则目录会因缺少 FILE_FLAG_BACKUP_SEMANTICS 而无法打开）。
        const HANDLE handle = ::CreateFileW(
            toWide(toNativePath(path)), FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);

        if (handle == INVALID_HANDLE_VALUE) {
            if (error)
                *error = classifyWindowsErrorCode(::GetLastError());
            return false;
        }

        FILETIME modified;
        FILETIME accessed;
        // 传 nullptr 表示「该时间保持不变」。绝不能填当前时间——
        // 那会把「只改修改时间」变成「顺手改掉访问时间」。
        LPFILETIME pModified = nullptr;
        LPFILETIME pAccessed = nullptr;
        if (lastModified.isValid()) {
            modified = fileTimeToWindows(lastModified);
            pModified = &modified;
        }
        if (lastAccessed.isValid()) {
            accessed = fileTimeToWindows(lastAccessed);
            pAccessed = &accessed;
        }

        // 第一个参数（创建时间）恒传 nullptr：本接口不提供修改创建时间的能力。
        const BOOL ok = ::SetFileTime(handle, nullptr, pAccessed, pModified);
        ::CloseHandle(handle);

        if (!ok) {
            if (error)
                *error = classifyWindowsErrorCode(::GetLastError());
            return false;
        }
        if (error)
            *error = FileSystemError::None;
        return true;
    }

    bool setAttributes(const QString &path, FileAttributes attributes,
                       FileSystemError *error) const override
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
                *error = classifyWindowsErrorCode(::GetLastError());
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

private:
    /// 从重解析点数据里取出链接目标。
    ///
    /// 符号链接与目录联接的目标分别存放在两个不同的字段里，且都是
    /// 「字节偏移 + 字节长度」的形式（因为它们可以包含不能直接当字符串
    /// 解释的内容）。这里按各自的布局取出来。
    static QString extractReparseTarget(const REPARSE_DATA_BUFFER *reparse)
    {
        if (reparse == nullptr)
            return QString();

        if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
            const USHORT offset =
                reparse->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
            const USHORT length =
                reparse->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
            return QString::fromWCharArray(
                reparse->SymbolicLinkReparseBuffer.PathBuffer + offset, length);
        }

        if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
            const USHORT offset =
                reparse->MountPointReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
            const USHORT length =
                reparse->MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
            return QString::fromWCharArray(
                reparse->MountPointReparseBuffer.PathBuffer + offset, length);
        }

        return QString();
    }
};

} // namespace

FileSystem *createWindowsFileSystem()
{
    return new WindowsFileSystem();
}

} // namespace Files
} // namespace LqCompare
