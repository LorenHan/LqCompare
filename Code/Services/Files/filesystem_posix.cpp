#include "filesystem.h"
#include "pathutils.h"

// 本文件是 POSIX（macOS / Linux）实现。Windows 上整体不参与编译，
// 因为下面用到的 <dirent.h>、<sys/stat.h> 等头文件在 Windows 上不存在。
#ifndef Q_OS_WIN

#include <QFile>

#include <cerrno>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>      // AT_FDCWD、AT_SYMLINK_NOFOLLOW
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace LqCompare {
namespace Files {

namespace {

/// POSIX 路径风格：只用 '/'，无盘符、无 UNC。
PathUtils::Style posixStyle()
{
    return PathUtils::Style::posix();
}

/// 把 POSIX 路径转成可交给系统调用的字节串。
///
/// 用 QFile::encodeName() 而不是 QString::toUtf8()：前者是 Qt 官方为
/// 「路径转本地编码」提供的入口，在 macOS/Linux 上给出 UTF-8，且未来若某个
/// 平台换成别的本地编码也能自动跟上。
QByteArray toNative(const QString &path)
{
    return QFile::encodeName(path);
}

/// 取出「修改时间」。
///
/// 字段名在两个平台上不同，这是 POSIX 里少见的显式差异：
///   macOS：st_mtimespec（纳秒在 tv_nsec）
///   Linux：st_mtim
/// 用条件编译而不是统一抹平，是因为抹平需要自己定义一套映射，
/// 而那正是「看起来一样、实际上只有一边被测到」的来源。
FileTime modifiedTimeOf(const struct stat &status)
{
#ifdef Q_OS_MACOS
    return FileTime::fromUnixTime(static_cast<qint64>(status.st_mtimespec.tv_sec),
                                  static_cast<qint64>(status.st_mtimespec.tv_nsec));
#else
    return FileTime::fromUnixTime(static_cast<qint64>(status.st_mtim.tv_sec),
                                  static_cast<qint64>(status.st_mtim.tv_nsec));
#endif
}

/// 取出「访问时间」。字段名差异同 modifiedTimeOf。
FileTime accessedTimeOf(const struct stat &status)
{
#ifdef Q_OS_MACOS
    return FileTime::fromUnixTime(static_cast<qint64>(status.st_atimespec.tv_sec),
                                  static_cast<qint64>(status.st_atimespec.tv_nsec));
#else
    return FileTime::fromUnixTime(static_cast<qint64>(status.st_atim.tv_sec),
                                  static_cast<qint64>(status.st_atim.tv_nsec));
#endif
}

/// 由 struct stat 构造 FileInfo。
///
/// 注意 st_mode 的判断顺序：符号链接必须先判，否则一个指向目录的链接会被
/// S_ISDIR 先命中（因为 S_ISDIR 判的是 st_mode，而 lstat 给的是链接自身的
/// mode，不会是目录——但显式先判链接能让意图更清楚，也避免以后换成 stat 时出错）。
FileInfo infoFromStat(const QString &path, const struct stat &status)
{
    FileInfo info;
    info.path = path;
    info.name = PathUtils::fileName(path, posixStyle());
    info.exists = true;

    info.isSymLink = S_ISLNK(status.st_mode);
    info.isDirectory = S_ISDIR(status.st_mode);
    info.size = static_cast<quint64>(status.st_size);

    info.lastModified = modifiedTimeOf(status);
    info.lastAccessed = accessedTimeOf(status);

    // 创建时间只有部分平台提供：
    //   macOS 的 struct stat 有 st_birthtimespec；
    //   Linux 的 stat 没有创建时间，只有少数文件系统支持 statx 才拿得到。
    // 拿不到时留一个「无效」而不是填 0——0 表示 1970 年，会被当成真实时间参与比较，
    // 让「创建时间不同」这种判定凭空成立。
#ifdef Q_OS_MACOS
    info.created = FileTime::fromUnixTime(static_cast<qint64>(status.st_birthtimespec.tv_sec),
                                          static_cast<qint64>(status.st_birthtimespec.tv_nsec));
#else
    // Linux：留空。PLAT-002 的完成标准不要求创建时间，需要时由 PLAT-007 补 statx。
    info.created = FileTime();
#endif

    FileAttributes attributes = FileAttribute::None;

    // 「只读」在 POSIX 上的含义是「任何写权限位都没有」。
    // 只看属主位是不够的：文件可能属主不可写但同组可写。
    if ((status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
        attributes |= FileAttribute::ReadOnly;

    if (status.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        attributes |= FileAttribute::Executable;

    if (info.isSymLink)
        attributes |= FileAttribute::SymLink;

    // POSIX 上的「隐藏」就是「名字以点开头」，这是约定而非属性位。
    // 放在这里识别，是为了让上层不必为两个平台各写一套判断。
    if (info.name.startsWith(QLatin1Char('.')))
        attributes |= FileAttribute::Hidden;

    // System 与 Archive 是 Windows 概念，POSIX 上恒不置位（见头文件说明）。

    info.attributes = attributes;
    return info;
}

/// 执行 lstat 并把 errno 归类。
bool lstatPath(const QString &path, struct stat *out, ErrorCode *error)
{
    errno = 0;
    if (::lstat(toNative(path).constData(), out) == 0) {
        if (error)
            *error = FileSystemError::None;
        return true;
    }
    if (error)
        *error = fromSystemError(errno);
    return false;
}

///
/// POSIX 文件系统实现。
///
/// 用原始系统调用（lstat / opendir / utimensat / chmod）而不是 Qt 的
/// QFileInfo / QDir，原因有三条：
///
/// 1. **精度**：Qt 的时间戳只有毫秒，而 stat 给到纳秒。摘要与同步判定在
///    时间戳接近时依赖精度（见 PLAT-002 的纳秒要求）。
/// 2. **语义**：需要严格的 lstat 语义来区分「链接本身」与「链接目标」。
/// 3. **平台差异必须显式存在**：Windows 实现要写 Win32 API，如果 POSIX 侧
///    用 Qt 糊过去，两边看起来一样、实则一个走了抽象、一个走了系统调用，
///    以后出问题时无从对照。
///
class PosixFileSystem : public FileSystem
{
public:
    Qt::CaseSensitivity caseSensitivity() const override
    {
        // 两个平台都不敏感：
        //   macOS 默认的 APFS/HFS+ 是大小写不敏感（除非显式格式化成敏感卷）；
        //   Linux 的 ext4 默认敏感。
        // 因此这里按平台给默认值，而不是按「POSIX」一刀切。
#ifdef Q_OS_MACOS
        return Qt::CaseInsensitive;
#else
        return Qt::CaseSensitive;
#endif
    }

    QChar separator() const override { return QLatin1Char('/'); }

    QString pathNormalize(const QString &path, ErrorCode *error) const override
    {
        if (error)
            *error = FileSystemError::None;
        return PathUtils::normalize(path, posixStyle());
    }

    bool isAbsolutePath(const QString &path) const override
    {
        return PathUtils::isAbsolute(path, posixStyle());
    }

    QString toNativePath(const QString &path) const override
    {
        // POSIX 没有长路径前缀的概念（PATH_MAX 对单个组件是 255，
        // 整条路径由系统调用返回值决定，不需要程序员加前缀绕开）。
        return path;
    }

    FileInfo stat(const QString &path, ErrorCode *error) const override
    {
        struct stat status;
        if (!lstatPath(path, &status, error))
            return FileInfo();
        return infoFromStat(path, status);
    }

    QString linkTarget(const QString &path, ErrorCode *error) const override
    {
        // PATH_MAX 是系统对单次 readlink 缓冲的实际上限。不写死 4096：
        // 用动态增长的小缓冲循环读，避免在极长链接上截断。
        static constexpr int kInitialBufferSize = 256;
        static constexpr int kMaxBufferSize = 64 * 1024;

        const QByteArray native = toNative(path);
        QByteArray buffer(kInitialBufferSize, Qt::Uninitialized);

        for (;;) {
            errno = 0;
            const ssize_t length = ::readlink(native.constData(), buffer.data(), buffer.size());
            if (length < 0) {
                if (error)
                    *error = fromSystemError(errno);
                return QString();
            }
            if (length < buffer.size()) {
                // 没填满缓冲，说明读完了。
                // 注意必须按 length 截断后再解码：readlink 不写结尾的 '\0'，
                // 直接拿整个 buffer 解码会把后面的未初始化内容也读进来。
                if (error)
                    *error = FileSystemError::None;
                return QFile::decodeName(QByteArray(buffer.constData(), static_cast<int>(length)));
            }
            if (buffer.size() >= kMaxBufferSize) {
                // 不再无限增长：能到 64KB 的链接已经是异常输入了，
                // 与其让内存一直涨，不如明确报「名称超长」。
                if (error)
                    *error = FileSystemError::InvalidName;
                return QString();
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    bool exists(const QString &path, ErrorCode *error) const override
    {
        struct stat status;
        if (lstatPath(path, &status, error))
            return true;
        // 「不存在」是一种**确定的答案**，不是失败：调用方问「在不在」，
        // 「不在」就是有效回答。仍然保留 error 以便区分「不在」与「没权限查」。
        return false;
    }

    QVector<FileInfo> enumerateDirectory(const QString &path, ErrorCode *error) const override
    {
        QVector<FileInfo> entries;
        if (error) *error = FileSystemError::None;

        const QByteArray native = toNative(path);
        DIR *directory = ::opendir(native.constData());
        if (directory == nullptr) {
            if (error)
                *error = fromSystemError(errno);
            return entries;
        }

        const QChar separator = QLatin1Char('/');
        const bool pathEndsWithSeparator = path.endsWith(separator);

        for (;;) {
            // readdir 同时用于遍历与判断结束：返回 nullptr 表示读完或出错，
            // 因此必须用 errno 区分两者，否则一个中途出错的目录会被当成空目录——
            // 而「空目录」在文件夹比对里意味着「目标需要被清空」，后果很严重。
            errno = 0;
            struct dirent *entry = ::readdir(directory);
            if (entry == nullptr) {
                if (errno != 0 && error)
                    *error = fromSystemError(errno);
                break;
            }

            const QByteArray rawName(entry->d_name);
            // 跳过 "." 与 ".."：它们是目录项但不是内容，
            // 出现在列表里会让每一层都多出两个条目。
            if (rawName == "." || rawName == "..")
                continue;

            const QString name = QFile::decodeName(rawName);
            const QString childPath =
                pathEndsWithSeparator ? path + name : path + separator + name;

            struct stat status;
            ErrorCode childError;
            if (!lstatPath(childPath, &status, &childError)) {
                // Preserve an incomplete enumeration as an error, never as an empty tree.
                if (error && *error == FileSystemError::None) *error = childError;
                continue;
            }

            entries.append(infoFromStat(childPath, status));
        }

        ::closedir(directory);

        // 刻意不排序：见头文件说明。上层要稳定顺序就自己排，
        // 这样这里可以用系统最快的方式枚举，不必为了「看起来有序」多排一遍。
        return entries;
    }

    bool setTimes(const QString &path, const FileTime &lastModified,
                  const FileTime &lastAccessed, ErrorCode *error) const override
    {
        // utimensat 一次设置两个时间戳，且支持纳秒精度。
        // 不修改的那一项用 UTIME_OMIT 明确表示「保持原值」——
        // 不能填当前时间，那会把「只改修改时间」变成「顺手污染访问时间」。
        struct timespec times[2];
        times[0] = toTimespec(lastAccessed, UTIME_OMIT);
        times[1] = toTimespec(lastModified, UTIME_OMIT);

        // AT_SYMLINK_NOFOLLOW：改链接自身的时间，而不是它指向的目标。
        // 这正是「stat 不跟随链接」这一约定在写操作上的对应行为。
        errno = 0;
        if (::utimensat(AT_FDCWD, toNative(path).constData(), times, AT_SYMLINK_NOFOLLOW) == 0) {
            if (error)
                *error = FileSystemError::None;
            return true;
        }
        if (error)
            *error = fromSystemError(errno);
        return false;
    }

    bool setAttributes(const QString &path, FileAttributes attributes,
                       ErrorCode *error) const override
    {
        struct stat status;
        if (!lstatPath(path, &status, error))
            return false;

        // 只改被点名的属性，其余权限位原样保留。
        // 直接写一个固定 mode 会把用户设的权限组合（如 setgid、粘滞位）抹掉。
        mode_t mode = status.st_mode;
        if (attributes.testFlag(FileAttribute::ReadOnly)) {
            mode &= ~static_cast<mode_t>(S_IWUSR | S_IWGRP | S_IWOTH);
        } else {
            // 解除只读时只恢复属主写权限，不擅自给组和其他人开写。
            mode |= S_IWUSR;
        }

        errno = 0;
        if (::chmod(toNative(path).constData(), mode & 07777) == 0) {
            if (error)
                *error = FileSystemError::None;
            return true;
        }
        if (error)
            *error = fromSystemError(errno);
        return false;
    }

    QString platformName() const override
    {
#ifdef Q_OS_MACOS
        return QStringLiteral("macos");
#else
        return QStringLiteral("linux");
#endif
    }

    // deleteToTrash 刻意不在这里覆写：真实实现属 PLAT-003。
    // 基类会返回 NotSupported，因此在此之前调用方拿到的是一句明确的
    // 「不支持」，而不是静默的永久删除。

private:
    static struct timespec toTimespec(const FileTime &time, long omitValue)
    {
        struct timespec result;
        if (!time.isValid()) {
            result.tv_sec = 0;
            result.tv_nsec = omitValue;
            return result;
        }
        result.tv_sec = static_cast<time_t>(time.nanosecondsSinceEpoch() / 1000000000LL);
        result.tv_nsec = static_cast<long>(time.nanosecondsSinceEpoch() % 1000000000LL);
        return result;
    }
};

} // namespace

FileSystem *createPosixFileSystem()
{
    return new PosixFileSystem();
}

} // namespace Files
} // namespace LqCompare

#endif // !Q_OS_WIN
