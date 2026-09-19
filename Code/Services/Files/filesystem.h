#ifndef LQCOMPARE_FILESYSTEM_H
#define LQCOMPARE_FILESYSTEM_H

#include <QChar>
#include <QDateTime>
#include <QFlags>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Files {

///
/// \brief 文件系统操作的错误分类（PRD: PLAT-002、PLAT-008）。
///
/// 刻意按「用户能做什么」分类，而不是照搬系统错误码。原因是 PLAT-008 要求
/// 「无权限 / 只读 / 被占用 / 只读文件系统」四类必须给出不同的处置建议——
/// 统一报「操作失败」等于没报。分类放在最底层，上层才能给出针对性提示。
///
enum class FileSystemError {
    None = 0,
    NotFound,           ///< 路径不存在
    PermissionDenied,   ///< 权限不足（可提权）
    ReadOnly,           ///< 文件是只读的（可解除只读）
    Busy,               ///< 被其它进程占用（需关闭占用程序）
    ReadOnlyFileSystem, ///< 所在文件系统只读（只能换目标目录）
    NoSpace,            ///< 磁盘或配额不足（需清理空间或换目标位置）
    InvalidName,        ///< 名称非法（含非法字符、保留名、超长）
    NotDirectory,       ///< 期望目录但实际是文件（或反之）
    AlreadyExists,      ///< 目标已存在且不允许覆盖
    NotSupported,       ///< 当前平台或文件系统不支持该操作
    Unknown,            ///< 无法归类的失败
};

/// 稳定的机器可读错误标识（用于日志与测试断言，不用于界面显示）。
const char *errorIdentifier(FileSystemError error);

/// 面向用户的一句话说明。
QString errorMessage(FileSystemError error, const QString &path = QString());

/// 面向用户的处置建议。四类可处置错误必须给出不同建议（PLAT-008）。
QString errorAdvice(FileSystemError error);

/// 重试是否有意义（例如「被占用」重试可能成功，「权限不足」重试不会）。
bool isRetryable(FileSystemError error);

///
/// \brief 把 errno 风格的系统错误码归类。
///
/// 放在平台无关层而不是各自的平台实现里，理由有两条：
/// 1. POSIX 与 Windows 的 errno 常量集合足够重合（ENOENT / EACCES / EBUSY /
///    EROFS / EEXIST / ENAMETOOLONG ...），归类逻辑可以共用。
/// 2. 放在共享层就可以在任意平台上被单元测试覆盖——包括在 macOS 上测试
///    Windows 会返回的那些错误码。这正是 PLAT-010 想要的「平台差异可测」。
///
FileSystemError classifySystemError(int systemError);

///
/// \brief Windows 错误码（Win32 `GetLastError()` 的取值）。
///
/// 为什么要自己写一遍常量，而不是直接用 `<windows.h>` 里的 `ERROR_ACCESS_DENIED`
/// --------------------------------------------------------------------------
/// 因为 Windows 的错误映射恰恰是最容易出错、又最需要测试的地方。例如
/// 「文件被占用」在 Windows 上返回的是 `ERROR_SHARING_VIOLATION`，而标准库会把它
/// 映射成 `EACCES`（看起来像「权限不足」）——如果按 errno 归类，用户会拿到
/// 「请提权」这种完全错误的建议。
///
/// 把常量与映射函数放在平台无关层，就能在 macOS 上覆盖 Windows 的错误分类逻辑。
/// 代价是常量值需要手写；为此在 Windows 编译时用 `static_assert` 与
/// `<windows.h>` 里的真实常量逐个比对（见 filesystem.cpp），一旦写错在
/// Windows 上是**编译期**失败，而不是运行期静默错判。
///
/// 取值来源：Win32 错误码官方定义（这些值自 NT 起稳定不变）。
///
namespace Win32Error {
constexpr unsigned long FileNotFound = 2;              ///< ERROR_FILE_NOT_FOUND
constexpr unsigned long PathNotFound = 3;              ///< ERROR_PATH_NOT_FOUND
constexpr unsigned long AccessDenied = 5;              ///< ERROR_ACCESS_DENIED
constexpr unsigned long WriteProtect = 19;             ///< ERROR_WRITE_PROTECT
constexpr unsigned long SharingViolation = 32;         ///< ERROR_SHARING_VIOLATION
constexpr unsigned long LockViolation = 33;            ///< ERROR_LOCK_VIOLATION
constexpr unsigned long NotSupported = 50;             ///< ERROR_NOT_SUPPORTED
constexpr unsigned long FileExists = 80;               ///< ERROR_FILE_EXISTS
constexpr unsigned long InvalidName = 123;             ///< ERROR_INVALID_NAME
constexpr unsigned long DiskFull = 112;                ///< ERROR_DISK_FULL
constexpr unsigned long FilenameExceededRange = 206;   ///< ERROR_FILENAME_EXCED_RANGE
constexpr unsigned long DirectoryNotEmpty = 145;       ///< ERROR_DIR_NOT_EMPTY
} // namespace Win32Error

/// 把 Win32 错误码归类（PRD: PLAT-002、PLAT-008）。
///
/// 与 classifySystemError 分开的原因：Windows 上 errno 的映射存在信息丢失
/// （多个不同的 Win32 错误会被折叠成同一个 errno），拿不到「被占用」这类
/// 关键区别。因此 Windows 实现应当优先用真实的 Win32 错误码。
///
FileSystemError classifyWindowsErrorCode(unsigned long code);

///
/// \brief 时间戳：内部统一为「UTC 纪元起的纳秒数」（PRD: PLAT-002）。
///
/// 内部一律 UTC，显示层再用 toLocalDateTime() 转本地时间。这样做的原因是
/// 比对逻辑与排序只关心「哪个更新」，与时区无关；一旦在存储层混入本地时区，
/// 夏令时切换与跨时区挂载目录会直接产生错误的「相同/不同」判定。
///
/// 精度说明：内部表示是纳秒，但实际精度取决于来源。QDateTime 只有毫秒精度，
/// 因此 fromDateTime() 会丢掉亚毫秒部分；各平台文件系统本身也常只有
/// 1 秒或 100 纳秒精度。文件比对比较时间戳时不应假设纳秒级可分辨。
///
class FileTime
{
public:
    FileTime() = default;

    /// 用「UTC 纪元起的纳秒数」构造。
    static FileTime fromNanosecondsSinceEpoch(qint64 nanoseconds);

    /// 用毫秒数构造（Qt 的 API 大多给毫秒）。
    static FileTime fromMillisecondsSinceEpoch(qint64 milliseconds);

    static FileTime fromSecondsSinceEpoch(qint64 seconds);

    /// 从 QDateTime 构造。会先转成 UTC 再取毫秒，因此传本地时间也是正确的。
    static FileTime fromDateTime(const QDateTime &dateTime);

    /// 当前时间。
    static FileTime now();

    /// 用系统调用返回的秒 + 纳秒构造（POSIX 的 stat 直接给这两个字段）。
    static FileTime fromUnixTime(qint64 seconds, qint64 nanoseconds);

    bool isValid() const { return m_valid; }

    qint64 nanosecondsSinceEpoch() const { return m_nanoseconds; }
    qint64 millisecondsSinceEpoch() const { return m_nanoseconds / 1000000; }

    /// 显示层用这个转本地时间；比对逻辑不要用它。
    QDateTime toLocalDateTime() const;

    /// 明确要求 UTC 时用这个。
    QDateTime toUtcDateTime() const;

    bool operator==(const FileTime &other) const
    {
        return m_valid == other.m_valid && m_nanoseconds == other.m_nanoseconds;
    }
    bool operator!=(const FileTime &other) const { return !(*this == other); }
    bool operator<(const FileTime &other) const { return m_nanoseconds < other.m_nanoseconds; }
    bool operator>(const FileTime &other) const { return other < *this; }
    bool operator<=(const FileTime &other) const { return !(other < *this); }
    bool operator>=(const FileTime &other) const { return !(*this < other); }

private:
    qint64 m_nanoseconds = 0;
    bool m_valid = false;
};

/// 文件属性。取值与「用户能看见的属性」对齐，不做系统位到处的映射。
enum class FileAttribute {
    None = 0x0,
    ReadOnly = 0x1,  ///< 只读
    Hidden = 0x2,    ///< 隐藏
    System = 0x4,    ///< 系统文件（Windows 有、POSIX 无，POSIX 实现恒不置位）
    Archive = 0x8,   ///< 存档位（Windows 有、POSIX 无）
    SymLink = 0x10,  ///< 符号链接（本身，而非它指向的目标）
    Executable = 0x20, ///< 可执行（POSIX 权限位；Windows 上按扩展名推断）
};

Q_DECLARE_FLAGS(FileAttributes, FileAttribute)
Q_DECLARE_OPERATORS_FOR_FLAGS(FileAttributes)

///
/// \brief 一个文件或目录的元数据快照（PRD: PLAT-002）。
///
struct FileInfo
{
    QString path;   ///< 完整路径（未做平台规范化，调用方负责）
    QString name;   ///< 最后一段名称；列表显示直接用这个，不必再切路径
    quint64 size = 0; ///< 字节数；目录的 size 由文件系统决定，不要当作内容大小用

    FileTime lastModified;
    FileTime lastAccessed;
    FileTime created; ///< 某些平台（如 Linux 的多数文件系统）拿不到，此时 isValid() 为 false

    FileAttributes attributes = FileAttribute::None;

    bool exists = false;            ///< 路径不存在时 false，其余字段无意义
    bool isDirectory = false;
    bool isSymLink = false;         ///< 与 FileAttribute::SymLink 一致，便于直接判断

    bool isReadOnly() const { return attributes.testFlag(FileAttribute::ReadOnly); }
};

///
/// \brief 文件系统服务抽象层（PRD: PLAT-002）。
///
/// **业务代码不允许直接调用平台 API**，也不允许硬编码路径分隔符——一律走这里。
/// 两个理由：
/// 1. 异常路径（权限、占用、只读、不存在）需要能被注入和测试。用真实文件系统
///    制造这些情形既难又不可靠（Windows 上「文件被占用」尤其难稳定复现）。
/// 2. 平台差异集中在一处，上层代码不必到处写 #ifdef。
///
/// 约定：所有方法都接受一个可选的 error 出参。传 nullptr 表示调用方不关心原因；
/// 传了指针时，成功必须写 FileSystemError::None，失败必须写具体分类——
/// 不允许出现「返回 false 但 error 仍是 None」的情况，否则调用方拿不到原因。
///
class FileSystem
{
public:
    virtual ~FileSystem();

    // --- 路径语义（平台相关的部分集中在这里）-------------------------------

    /// 平台的大小写语义。Windows/macOS 默认不敏感，Linux 敏感。
    ///
    /// 注意：这不是「文件系统一定不敏感」，而是「同一路径的两种写法可能指向
    /// 同一个文件」。文件夹比对是否把 a.txt 与 A.TXT 判为同一个文件，取决于这里。
    virtual Qt::CaseSensitivity caseSensitivity() const = 0;

    virtual QChar separator() const = 0;

    /// 规范化：统一分隔符、去掉冗余分隔符与结尾分隔符（根目录除外）、
    /// 解析 `.` 与 `..`。不做符号链接解析（那是 realPath 的事）。
    virtual QString pathNormalize(const QString &path, FileSystemError *error = nullptr) const = 0;

    virtual bool isAbsolutePath(const QString &path) const = 0;

    /// 转成可以直接交给系统调用的形式，必要时加长路径前缀（Windows `\\?\`）。
    virtual QString toNativePath(const QString &path) const = 0;

    // --- 读取 --------------------------------------------------------------

    /// 读取元数据。路径不存在时返回 exists == false 且 error 为 NotFound。
    ///
    /// **不跟随符号链接**（等价于 POSIX 的 lstat）：返回的是链接本身——
    /// isSymLink 为 true、isDirectory 为 false、size 是链接自身的大小、
    /// lastModified 是链接自身的修改时间。
    ///
    /// 为什么选不跟随，而不是像多数封装那样跟随
    /// ----------------------------------------
    /// 文件夹比对必须能区分三种情况：链接本身不同、链接目标不同、
    /// 链接已损坏。一旦这里跟随了链接，损坏的链接会直接变成「不存在」，
    /// 用户看到的是「源里有、目标里没有」——而真相是两个不同的坏链接。
    /// 跟随会把这个区别永久抹掉，无法在更上层恢复。
    ///
    /// 需要目标的元数据时，先 linkTarget() 取出指向，再对它调用 stat()。
    virtual FileInfo stat(const QString &path, FileSystemError *error = nullptr) const = 0;

    /// 读取符号链接指向的原始路径（不解析其中的相对部分，也不递归）。
    /// 路径不是符号链接、或读取失败时返回空串并写 error。
    virtual QString linkTarget(const QString &path, FileSystemError *error = nullptr) const = 0;

    /// 判断存在性。比 stat 便宜（某些平台可以只查目录项）。
    virtual bool exists(const QString &path, FileSystemError *error = nullptr) const = 0;

    /// 枚举目录内容。**不递归**，且不含 `.` 与 `..`。
    ///
    /// 返回顺序不做保证：上层若需要稳定顺序必须自己排序。这样实现可以按平台
    /// 最快的方式枚举，而不必为了「看起来有序」做一遍额外排序。
    virtual QVector<FileInfo> enumerateDirectory(const QString &path,
                                                 FileSystemError *error = nullptr) const = 0;

    // --- 写入 --------------------------------------------------------------
    //
    // 这些方法都标为 const：它们改变的是**文件系统**这个外部状态，
    // 而不是 FileSystem 对象自身。标 const 后，持有 const 引用的调用方
    // （例如只读的会话上下文）仍然可以执行写操作，不必为了改一个时间戳
    // 而把整条调用链的非 const 性传下去。

    /// 设置时间戳。不需要修改的那一项传 isValid() == false 的 FileTime。
    virtual bool setTimes(const QString &path, const FileTime &lastModified,
                          const FileTime &lastAccessed, FileSystemError *error = nullptr) const = 0;

    /// 设置属性。未包含在 attributes 里的属性**保持不变**（不是清零）。
    virtual bool setAttributes(const QString &path, FileAttributes attributes,
                               FileSystemError *error = nullptr) const = 0;

    /// 删除到回收站（PRD: PLAT-003 提供真实实现）。
    ///
    /// 接口在这里定义是因为「所有删除都必须可逆」是上层的基本假设；
    /// 但真正的回收站行为（各平台机制、配额、不可用时的用户选择）属 PLAT-003。
    /// 基类实现返回 NotSupported，PLAT-003 之前调用方得到的是一句明确的
    /// 「不支持」而不是静默的永久删除。
    virtual bool deleteToTrash(const QStringList &paths, FileSystemError *error = nullptr) const;

    /// 平台名称，用于日志与测试断言（"windows" / "macos" / "linux"）。
    virtual QString platformName() const = 0;
};

///
/// \brief 创建当前平台的原生实现。
///
/// 返回的指针由调用方持有（用 std::unique_ptr 接管）。永不返回 nullptr：
/// 平台不被支持时返回一个「所有操作都失败并给出 NotSupported」的实现，
/// 这样上层不必到处判空。
///
FileSystem *createNativeFileSystem();

} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_FILESYSTEM_H
