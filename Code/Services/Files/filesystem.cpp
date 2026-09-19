#include "filesystem.h"

#include <QDateTime>
#include <QtGlobal>

#include <cerrno>

#ifdef Q_OS_WIN
// 只在 Windows 上引入，用于 static_assert 校验下面手写的错误码常量。
#  include <windows.h>
#endif

namespace LqCompare {
namespace Files {

// -----------------------------------------------------------------------------
// FileTime
// -----------------------------------------------------------------------------

namespace {

/// 一秒的纳秒数。单独定义是为了让换算处的意图一眼可见，避免出现 1000000000
/// 这类看不出量级的字面量。
constexpr qint64 kNanosecondsPerSecond = 1000000000LL;
constexpr qint64 kNanosecondsPerMillisecond = 1000000LL;

} // namespace

FileTime FileTime::fromNanosecondsSinceEpoch(qint64 nanoseconds)
{
    FileTime time;
    time.m_nanoseconds = nanoseconds;
    time.m_valid = true;
    return time;
}

FileTime FileTime::fromMillisecondsSinceEpoch(qint64 milliseconds)
{
    return fromNanosecondsSinceEpoch(milliseconds * kNanosecondsPerMillisecond);
}

FileTime FileTime::fromSecondsSinceEpoch(qint64 seconds)
{
    return fromNanosecondsSinceEpoch(seconds * kNanosecondsPerSecond);
}

FileTime FileTime::fromUnixTime(qint64 seconds, qint64 nanoseconds)
{
    return fromNanosecondsSinceEpoch(seconds * kNanosecondsPerSecond + nanoseconds);
}

FileTime FileTime::fromDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return FileTime();

    // toMSecsSinceEpoch() 内部先转 UTC 再取毫秒，所以传本地时间也是正确的：
    // 调用方不必关心 dateTime 的时区，转换在这里一次完成。
    return fromMillisecondsSinceEpoch(dateTime.toMSecsSinceEpoch());
}

FileTime FileTime::now()
{
    return fromMillisecondsSinceEpoch(QDateTime::currentMSecsSinceEpoch());
}

QDateTime FileTime::toUtcDateTime() const
{
    if (!m_valid)
        return QDateTime();
    return QDateTime::fromMSecsSinceEpoch(millisecondsSinceEpoch(), Qt::UTC);
}

QDateTime FileTime::toLocalDateTime() const
{
    // 明确经由 UTC 转本地，而不是直接按本地构造：
    // 后者在跨时区与夏令时切换时会给出错误结果。
    return toUtcDateTime().toLocalTime();
}

// -----------------------------------------------------------------------------
// 错误分类的辅助函数
// -----------------------------------------------------------------------------

const char *errorIdentifier(FileSystemError error)
{
    switch (error) {
    case FileSystemError::None:               return "none";
    case FileSystemError::NotFound:           return "not-found";
    case FileSystemError::PermissionDenied:   return "permission-denied";
    case FileSystemError::ReadOnly:           return "read-only";
    case FileSystemError::Busy:               return "busy";
    case FileSystemError::ReadOnlyFileSystem: return "read-only-filesystem";
    case FileSystemError::NoSpace:            return "no-space";
    case FileSystemError::InvalidName:        return "invalid-name";
    case FileSystemError::NotDirectory:       return "not-directory";
    case FileSystemError::AlreadyExists:      return "already-exists";
    case FileSystemError::NotSupported:       return "not-supported";
    case FileSystemError::Unknown:            return "unknown";
    }
    // 枚举被扩展却忘了在这里补分支时，宁可显示一个明确的占位符，
    // 也不要让日志里出现空字符串而看不出发生过什么。
    return "unclassified";
}

QString errorMessage(FileSystemError error, const QString &path)
{
    const QString target = path.isEmpty() ? QString() : QStringLiteral("「%1」").arg(path);

    switch (error) {
    case FileSystemError::None:
        return QString();
    case FileSystemError::NotFound:
        return QStringLiteral("找不到 %1").arg(target.isEmpty() ? QStringLiteral("该路径") : target);
    case FileSystemError::PermissionDenied:
        return QStringLiteral("没有权限访问 %1").arg(target.isEmpty() ? QStringLiteral("该路径") : target);
    case FileSystemError::ReadOnly:
        return QStringLiteral("%1 是只读的").arg(target.isEmpty() ? QStringLiteral("该文件") : target);
    case FileSystemError::Busy:
        return QStringLiteral("%1 正被其它程序占用").arg(target.isEmpty() ? QStringLiteral("该文件") : target);
    case FileSystemError::ReadOnlyFileSystem:
        return QStringLiteral("%1 所在的文件系统是只读的").arg(target.isEmpty() ? QStringLiteral("该位置") : target);
    case FileSystemError::NoSpace:
        return QStringLiteral("空间不足，无法写入 %1").arg(target.isEmpty() ? QStringLiteral("该位置") : target);
    case FileSystemError::InvalidName:
        return QStringLiteral("%1 不是合法的名称").arg(target.isEmpty() ? QStringLiteral("该名称") : target);
    case FileSystemError::NotDirectory:
        return QStringLiteral("%1 的类型与操作不符（期望目录但实际是文件，或反之）")
            .arg(target.isEmpty() ? QStringLiteral("该路径") : target);
    case FileSystemError::AlreadyExists:
        return QStringLiteral("%1 已经存在").arg(target.isEmpty() ? QStringLiteral("该目标") : target);
    case FileSystemError::NotSupported:
        return QStringLiteral("当前平台或文件系统不支持该操作");
    case FileSystemError::Unknown:
        return QStringLiteral("操作失败（原因无法归类）");
    }
    return QStringLiteral("操作失败");
}

QString errorAdvice(FileSystemError error)
{
    // PLAT-008 要求「无权限 / 只读 / 被占用 / 只读文件系统」四类给出**不同**建议。
    // 统一写成「操作失败，请重试」等于没给建议——用户不知道下一步做什么。
    switch (error) {
    case FileSystemError::None:
        return QString();
    case FileSystemError::NotFound:
        return QStringLiteral("确认该路径是否已被移动或删除；若来自最近列表或历史会话，这条记录已经失效。");
    case FileSystemError::PermissionDenied:
        return QStringLiteral("以管理员身份运行本程序，或把目标改到你确定有写权限的目录。");
    case FileSystemError::ReadOnly:
        return QStringLiteral("先在文件属性里去掉只读标记（POSIX 上相当于 chmod +w）再重试。");
    case FileSystemError::Busy:
        return QStringLiteral("关闭正在打开该文件的程序后重试；不确定是哪个程序时，重启后再试。");
    case FileSystemError::ReadOnlyFileSystem:
        return QStringLiteral("该分区/设备是以只读方式挂载的，请换一个可写的目标目录。");
    case FileSystemError::NoSpace:
        return QStringLiteral("清理空间，或把目标改到另一块磁盘、另一个目录。");
    case FileSystemError::InvalidName:
        return QStringLiteral("改名后再试：去掉 < > : \" / \\ | ? * 和控制字符，"
                              "并避开保留设备名（CON / PRN / AUX / NUL / COM1-9 / LPT1-9）。");
    case FileSystemError::NotDirectory:
        return QStringLiteral("检查该路径到底是目录还是文件——当前操作对两者的要求不同。");
    case FileSystemError::AlreadyExists:
        return QStringLiteral("换一个名称，或先处理已存在的目标（覆盖前建议先备份）。");
    case FileSystemError::NotSupported:
        return QStringLiteral("换一种方式完成：改用本程序提供的其它入口，或在系统里手工操作。");
    case FileSystemError::Unknown:
        return QStringLiteral("查看日志里的原始系统错误码以定位原因。");
    }
    return QString();
}

bool isRetryable(FileSystemError error)
{
    // 只有「被占用」是重试可能自行消失的：用户关掉占用程序就好了。
    // 权限不足、只读、空间不足重试多少次都是一样的结果，界面不应该给出
    // 「重试」这个注定无效的选项。
    return error == FileSystemError::Busy;
}

// -----------------------------------------------------------------------------
// 系统错误码 -> 分类
// -----------------------------------------------------------------------------

FileSystemError classifySystemError(int systemError)
{
    // 这里用 errno 常量而不是数字，原因是 errno 的取值在不同平台上并不相同，
    // 而 <cerrno> 提供的是当前平台的正确取值。
    switch (systemError) {
    case 0:
        return FileSystemError::None;

    case ENOENT:
        return FileSystemError::NotFound;

    case EACCES:
    case EPERM:
        return FileSystemError::PermissionDenied;

    case EBUSY:
#ifdef ETXTBSY
    case ETXTBSY:
#endif
        return FileSystemError::Busy;

    case EROFS:
        return FileSystemError::ReadOnlyFileSystem;

    case EEXIST:
        return FileSystemError::AlreadyExists;

    case ENAMETOOLONG:
        return FileSystemError::InvalidName;

    case EISDIR:
    case ENOTDIR:
        return FileSystemError::NotDirectory;

#ifdef ENOSPC
    case ENOSPC:
        return FileSystemError::NoSpace;
#endif
#ifdef EDQUOT
    case EDQUOT:
        return FileSystemError::NoSpace;
#endif

#ifdef ENOTSUP
    case ENOTSUP:
        return FileSystemError::NotSupported;
#endif
#ifdef EOPNOTSUPP
#  if !defined(ENOTSUP) || (EOPNOTSUPP != ENOTSUP)
    // 有些平台上 ENOTSUP 与 EOPNOTSUPP 是同一个值，重复的 case 会编译失败，
    // 因此加了条件编译——这类平台差异只能靠条件编译处理。
    case EOPNOTSUPP:
        return FileSystemError::NotSupported;
#  endif
#endif

    default:
        // 刻意不把 EINVAL 归到 InvalidName：EINVAL 的成因太杂（参数错、偏移错、
        // 设备不支持），归进去会把「名称合法但操作参数有问题」误报成「名称非法」，
        // 让用户去改一个本来没问题的名字。
        return FileSystemError::Unknown;
    }
}

FileSystemError classifyWindowsErrorCode(unsigned long code)
{
    using namespace Win32Error;

    if (code == 0)
        return FileSystemError::None;
    if (code == FileNotFound || code == PathNotFound)
        return FileSystemError::NotFound;
    if (code == AccessDenied)
        return FileSystemError::PermissionDenied;
    if (code == WriteProtect)
        return FileSystemError::ReadOnly;
    if (code == SharingViolation || code == LockViolation)
        return FileSystemError::Busy;
    if (code == FileExists)
        return FileSystemError::AlreadyExists;
    if (code == InvalidName || code == FilenameExceededRange)
        return FileSystemError::InvalidName;
    if (code == DiskFull)
        return FileSystemError::NoSpace;
    if (code == NotSupported)
        return FileSystemError::NotSupported;
    // ERROR_DIR_NOT_EMPTY 在这里归为 Unknown 而不是 Busy：
    // 「目录非空」不会因为重试而改变，界面绝不能把它当成可重试的占用问题。
    return FileSystemError::Unknown;
}

#ifdef Q_OS_WIN
// 在 Windows 上编译时，把手写的常量值与 <windows.h> 的真实常量逐个核对。
// 写错任何一个都会在**编译期**失败，而不是运行期静默错判——
// 这正是把手写常量放在平台无关层所必须付出的补偿。
static_assert(Win32Error::FileNotFound == ERROR_FILE_NOT_FOUND, "Win32 错误码常量不匹配");
static_assert(Win32Error::PathNotFound == ERROR_PATH_NOT_FOUND, "Win32 错误码常量不匹配");
static_assert(Win32Error::AccessDenied == ERROR_ACCESS_DENIED, "Win32 错误码常量不匹配");
static_assert(Win32Error::WriteProtect == ERROR_WRITE_PROTECT, "Win32 错误码常量不匹配");
static_assert(Win32Error::SharingViolation == ERROR_SHARING_VIOLATION, "Win32 错误码常量不匹配");
static_assert(Win32Error::LockViolation == ERROR_LOCK_VIOLATION, "Win32 错误码常量不匹配");
static_assert(Win32Error::NotSupported == ERROR_NOT_SUPPORTED, "Win32 错误码常量不匹配");
static_assert(Win32Error::FileExists == ERROR_FILE_EXISTS, "Win32 错误码常量不匹配");
static_assert(Win32Error::DiskFull == ERROR_DISK_FULL, "Win32 错误码常量不匹配");
static_assert(Win32Error::InvalidName == ERROR_INVALID_NAME, "Win32 错误码常量不匹配");
static_assert(Win32Error::FilenameExceededRange == ERROR_FILENAME_EXCED_RANGE, "Win32 错误码常量不匹配");
static_assert(Win32Error::DirectoryNotEmpty == ERROR_DIR_NOT_EMPTY, "Win32 错误码常量不匹配");
#endif

// -----------------------------------------------------------------------------
// FileSystem 基类
// -----------------------------------------------------------------------------

FileSystem::~FileSystem() = default;

bool FileSystem::deleteToTrash(const QStringList &paths, FileSystemError *error) const
{
    Q_UNUSED(paths);

    // 基类刻意**不**提供任何实现，哪怕只是「直接删掉」这种看起来能用的兜底。
    //
    // 原因是「删除必须可逆」是本工具对用户的基本承诺。如果这里提供一个会真删的
    // 默认实现，某个平台实现忘记覆写时，用户的文件就被永久删除了，而且不会有
    // 任何迹象表明「回收站其实没生效」。
    //
    // 宁可让所有平台实现都必须显式覆写（真实实现见 PLAT-003）。
    if (error)
        *error = FileSystemError::NotSupported;
    return false;
}

// -----------------------------------------------------------------------------
// 平台实现的分发
// -----------------------------------------------------------------------------

// 两个工厂函数由各自的平台文件提供实现：
//   filesystem_posix.cpp —— macOS 与 Linux
//   filesystem_win.cpp   —— Windows
FileSystem *createPosixFileSystem();
FileSystem *createWindowsFileSystem();

FileSystem *createNativeFileSystem()
{
    // 用 #ifdef 而不是运行期判断：平台实现里要调用各自的系统 API，
    // 那些头文件在别的平台上根本不存在，必须在编译期就排除掉。
#ifdef Q_OS_WIN
    return createWindowsFileSystem();
#else
    return createPosixFileSystem();
#endif
}

} // namespace Files
} // namespace LqCompare
