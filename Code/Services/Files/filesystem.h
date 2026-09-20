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
/// \brief 原始系统错误码所属的「域」（PRD: PLAT-008 完成标准第 5 条）。
///
/// 为什么分类之外还要留着原始码
/// --------------------------
/// PLAT-008 要求「错误信息包含原始系统错误码便于排查」。这件事不能只靠分类，
/// 因为分类是**有损**的：
///   - errno 与 Win32 错误码之间是多对一的。Windows 会把「文件被占用」映射成
///     EACCES，按 errno 归类就会给出「请提权」这种完全错误的建议。
///   - 同一个分类里的原因可能完全不同。1（EPERM，操作本身被禁止）与
///     13（EACCES，权限位不允许）都归 PermissionDenied，但排查方向不同：
///     前者常常是文件带不可变标志或 SELinux 拦截，后者才是 chmod 能解决的。
///   - 用户拿着原始码能直接搜到系统的官方解释，也能贴进工单——而「操作失败」
///     这三个字搜不出任何东西。
///
/// 所以结论是：分类用于**决定怎么办**，原始码用于**查清为什么**，两者都要留。
///
enum class ErrorDomain {
    None = 0,   ///< 没有原始码（例如成功，或本地产生的分类）
    Posix,      ///< errno 风格的值
    Win32,      ///< GetLastError() 的返回值
    Cocoa,      ///< NSCocoaErrorDomain 的值
};

/// 稳定的机器可读标识（"none" / "posix" / "win32" / "cocoa"）。
const char *errorDomainIdentifier(ErrorDomain domain);

///
/// \brief 一个错误的完整表示：分类 + 原始系统码（PRD: PLAT-008）。
///
/// 为什么把出参从 FileSystemError 换成这个结构体，而不是加一个新函数
/// ------------------------------------------------------------------
/// 「错误信息包含原始系统错误码」听起来像是个显示层需求，加个函数就够了。
/// 但真正做到需要原始码**在系统调用出错的那一刻**被记下来，然后一路传到界面。
/// 如果接口上只有 FileSystemError，那个码在读出来的下一行就被丢掉了，
/// 后面任何地方都补不回来——这正是改造前 filesystem_posix.cpp 里每处
/// `*error = classifySystemError(errno)` 的状态。
///
/// 于是把出参本身换成携带原始码的类型。为了让这个改动不至于把调用方全部推倒：
///   - 提供了从 FileSystemError 的隐式构造，因此 `*error = FileSystemError::None`
///     这类既有写法仍然有效；
///   - 提供了到 FileSystemError 的隐式转换，因此 `if (error == FileSystemError::Busy)`
///     这类既有比较也仍然有效。
///
/// 但**只有**真正拿到系统错误码的地方必须改用 fromSystemError()（而不是
/// classifySystemError()），否则原始码仍然是丢的——这一点没法靠类型系统强制，
/// 因此写在这里：`from*Error()` 是唯一正确的写法，`classify*()` 只用于分类本身。
///
struct ErrorCode
{
    /// 「该怎么办」。界面文案与建议由它决定。
    FileSystemError category = FileSystemError::None;

    /// 原始码属于哪个域。没有原始码时为 None。
    ErrorDomain domain = ErrorDomain::None;

    /// 原始码的字面值。含义由 domain 决定。
    qint64 raw = 0;

    ErrorCode() = default;

    /// 只带分类、不带原始码。**不是** explicit：让既有的
    /// `*error = FileSystemError::X` 写法继续可用。
    ErrorCode(FileSystemError onlyCategory) : category(onlyCategory) {}

    /// 是否表示成功。
    bool ok() const { return category == FileSystemError::None; }

    /// 是否带着一个可以拿去排查的原始码。
    bool hasRawCode() const { return domain != ErrorDomain::None; }

    /// 隐式转回分类。让既有比较（`error == FileSystemError::Busy`）继续可用。
    operator FileSystemError() const { return category; }
};

///
/// \brief 原始码的可读名字，例如 "EACCES" / "ERROR_SHARING_VIOLATION" /
///        "NSFileWriteFileExistsError"。不认识的取值返回 nullptr。
///
/// 刻意返回 nullptr 而不是拼一个 "UNKNOWN_13" 出来：拿到 nullptr 的调用方
/// 会退化成「只显示数字」，而一个看起来像名字的假名字会让人以为是官方叫法。
///
const char *rawErrorName(ErrorDomain domain, qint64 raw);

///
/// \brief 原始码的一行描述，例如 `errno 13（EACCES）`。
///
/// 没有原始码（domain == None）时返回空串——界面据此决定要不要显示这一段，
/// 而不是显示一句「原始错误码：（无）」。
///
QString errorDetail(const ErrorCode &code);

///
/// \brief 面向用户的一句话说明，末尾附上原始系统错误码（PRD: PLAT-008 第 5 条）。
///
/// 与 errorMessage() 的分工：
///   - errorMessage() 只讲「出了什么事」，用于不希望出现技术细节的场合；
///   - errorReport() 在其后补上「系统到底说了什么」，用于排查与日志。
/// 处置建议不在这里拼接，因为它取决于上下文（单条 vs 批量，见 batch.h），
/// 由调用方用 errorAdvice() 决定怎么显示。
///
QString errorReport(const ErrorCode &code, const QString &path = QString());

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
/// \brief macOS 的 Cocoa 错误码（`NSCocoaErrorDomain` 的取值）。
///
/// 与 Win32Error 完全对称：常量与映射函数都放在平台无关层，因此**在 macOS 上
/// 就能被覆盖**——回收站删除（PLAT-003）最需要验证的恰恰是错误分类这一段。
/// 编译期校验放在 trash_mac.mm 里：那里能同时看到 Foundation 的真实常量与
/// 这些手写常量，写错就是编译失败，而不是运行期把「没有权限」误判成别的。
///
/// 为什么不能只靠 errno
/// -------------------
/// `NSError` 有两个域。POSIX 域的错误可以走 classifySystemError()，
/// 但 Cocoa 域的错误码与 errno 完全不同：「文件已存在」在 POSIX 是 EEXIST=17，
/// 在 Cocoa 是 516；「磁盘满」在 POSIX 是 ENOSPC=28，在 Cocoa 是 640。
/// 只认 errno 会让回收站删不进去时全部落到 Unknown，用户拿到一句
/// 「未知错误」——等于没提示。
///
namespace CocoaError {
constexpr long NoSuchFile = 4;              ///< NSFileNoSuchFileError
constexpr long FileLocking = 255;           ///< NSFileLockingError
constexpr long ReadNoPermission = 257;      ///< NSFileReadNoPermissionError
constexpr long WriteNoPermission = 513;     ///< NSFileWriteNoPermissionError
constexpr long WriteInvalidFileName = 514;  ///< NSFileWriteInvalidFileNameError
constexpr long WriteFileExists = 516;       ///< NSFileWriteFileExistsError
constexpr long WriteOutOfSpace = 640;       ///< NSFileWriteOutOfSpaceError
constexpr long WriteVolumeReadOnly = 642;   ///< NSFileWriteVolumeReadOnlyError
constexpr long ManagerUnmountBusy = 769;    ///< NSFileManagerUnmountBusyError
} // namespace CocoaError

/// 把 Cocoa 错误码归类。POSIX 域的错误码请走 classifySystemError()。
FileSystemError classifyCocoaError(long code);

///
/// \brief 构造一个「分类 + 原始码」都齐全的 ErrorCode。
///
/// 这三个函数是**唯一正确的错误出口**：凡是真正拿到了系统错误码的地方，
/// 都必须用它们，而不是用上面的 classify*()（后者只回答「属于哪一类」，
/// 原始码会被丢掉，第 5 条完成标准也就落空了）。
///
/// 传 0 表示成功：此时 category 与 domain 都为空，raw 为 0。
///
ErrorCode fromSystemError(int systemError);
ErrorCode fromWindowsError(unsigned long code);
ErrorCode fromCocoaError(long code);

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
/// 出参类型是 ErrorCode 而不是 FileSystemError：后者只能说明「属于哪一类」，
/// 而 PLAT-008 还要求把系统给出的原始错误码交给用户便于排查。分类与原始码
/// 必须一起从系统调用处传出来，所以它们在同一个结构体里（见 ErrorCode 的说明）。
/// 平台实现应使用 fromSystemError() / fromWindowsError() 填这个出参。
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
    virtual QString pathNormalize(const QString &path, ErrorCode *error = nullptr) const = 0;

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
    virtual FileInfo stat(const QString &path, ErrorCode *error = nullptr) const = 0;

    /// 读取符号链接指向的原始路径（不解析其中的相对部分，也不递归）。
    /// 路径不是符号链接、或读取失败时返回空串并写 error。
    virtual QString linkTarget(const QString &path, ErrorCode *error = nullptr) const = 0;

    /// 判断存在性。比 stat 便宜（某些平台可以只查目录项）。
    virtual bool exists(const QString &path, ErrorCode *error = nullptr) const = 0;

    /// 枚举目录内容。**不递归**，且不含 `.` 与 `..`。
    ///
    /// 返回顺序不做保证：上层若需要稳定顺序必须自己排序。这样实现可以按平台
    /// 最快的方式枚举，而不必为了「看起来有序」做一遍额外排序。
    virtual QVector<FileInfo> enumerateDirectory(const QString &path,
                                                 ErrorCode *error = nullptr) const = 0;

    // --- 写入 --------------------------------------------------------------
    //
    // 这些方法都标为 const：它们改变的是**文件系统**这个外部状态，
    // 而不是 FileSystem 对象自身。标 const 后，持有 const 引用的调用方
    // （例如只读的会话上下文）仍然可以执行写操作，不必为了改一个时间戳
    // 而把整条调用链的非 const 性传下去。

    /// 设置时间戳。不需要修改的那一项传 isValid() == false 的 FileTime。
    virtual bool setTimes(const QString &path, const FileTime &lastModified,
                          const FileTime &lastAccessed, ErrorCode *error = nullptr) const = 0;

    /// 设置属性。未包含在 attributes 里的属性**保持不变**（不是清零）。
    virtual bool setAttributes(const QString &path, FileAttributes attributes,
                               ErrorCode *error = nullptr) const = 0;

    // 说明：这里曾经有一个 deleteToTrash()。PLAT-003 落地时它被移到了
    // TrashService（见 trash.h），而不是留在这里转发。
    //
    // 移走的理由是它当时有个不被注意的缺陷：FileSystem 是无状态的，
    // 而「撤销上一次删除」需要一个长期存在的撤销点。若在这里保留便捷转发，
    // 实现必然是「每次调用现场 new 一个 TrashService」——于是撤销点随对象一起
    // 被丢掉，用户点撤销永远报「没有可还原的删除」。那种失败还很难查：
    // 删除本身是成功的，只有撤销不工作。
    //
    // 所以删除只有一条入口：TrashService。它由调用方持有，生命周期跨越
    // 「删除」与「撤销」两次操作。

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
