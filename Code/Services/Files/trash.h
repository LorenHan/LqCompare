#ifndef LQCOMPARE_TRASH_H
#define LQCOMPARE_TRASH_H

#include "filesystem.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Files {

///
/// \brief 回收站的可用性（PRD: PLAT-003）。
///
/// 为什么要在删除**之前**单独问一次
/// ------------------------------
/// PLAT-003 的边界写得很死：「回收站不可用时必须明确告知并让用户选择，
/// 绝不静默降级为永久删除」。这条约束决定了可用性不能靠「删除失败后再看错误码」
/// 来获得，原因有三：
///   1. 删除是批量操作。等失败时，同一批里靠前的条目可能已经进了回收站，
///      靠后的没进——用户面对的是一半成功一半失败的现场。
///   2. 「失败」的语义不唯一。NotSupported 与 NoSpace 都可能导致失败，
///      但前者该建议「换个位置」，后者该建议「清空回收站」。删除失败后
///      再从错误码反推原因，等于把判断推迟到信息已经丢失之后。
///   3. 有些平台在不可用时**不会失败**，而是默默永久删除。这正是要防的。
///
/// 因此这里把「能不能进回收站」做成独立的一等查询，界面上先问、再动手。
///
enum class TrashAvailability {
    Available = 0,          ///< 可以进回收站
    VolumeNotSupported,     ///< 该卷没有回收站：网络盘、多数可移动盘、内存盘
    QuotaExceeded,          ///< 回收站容量配额已满（Windows 每卷默认有上限）
    NoSpace,                ///< 回收站所在卷剩余空间不足
    PlatformNotSupported,   ///< 该平台或该构建没有回收站能力
    Unknown,                ///< 探测本身失败，无法判断
};

/// 稳定的机器可读标识（用于日志与测试断言，不用于界面显示）。
const char *trashAvailabilityIdentifier(TrashAvailability availability);

/// 该状态下是否可以直接执行删除。只有 Available 为真。
///
/// 单独抽成函数而不是让调用方写 `== Available`，是为了让「有哪些状态属于
/// 不可用」只定义一次：将来若新增一种可用状态（例如某个平台的回收站
/// 需要先挂载再使用），只需改这一处。
bool isTrashUsable(TrashAvailability availability);

///
/// \brief 用户对「回收站不可用」的处置选择（PRD: PLAT-003）。
///
/// 默认值是 Cancel，这是刻意的：这个枚举的默认值会被用作「用户还没回答」时
/// 的行为。把它定成 DeletePermanently 会让一次界面卡顿变成一批文件的永久消失。
///
enum class TrashFallback {
    Cancel = 0,         ///< 取消本次删除（默认）
    DeletePermanently,  ///< 用户已明确要求永久删除
};

///
/// \brief 删除前的决策结果，供界面直接渲染（PRD: PLAT-003）。
///
/// 把「要不要问用户」「问的时候说什么」一次算好，而不是把这个判断分散到
/// 每个调用点。分散的后果是不同入口（工具栏删除、拖拽删除、同步时的清理）
/// 给出的提示不一致，用户会遇到同一个问题有时被拦住、有时不被拦住。
///
struct TrashDecision
{
    /// 为真时**不得**直接删除，必须先让用户从 TrashFallback 里做选择。
    bool requiresUserChoice = false;

    TrashAvailability availability = TrashAvailability::Available;

    /// 「为什么不能进回收站」。可用时为空。
    QString reason;

    /// 「可以怎么办」。可用时为空。
    QString advice;
};

/// 根据可用性算出删除前的决策（纯函数，无平台依赖，可在任意平台测试）。
TrashDecision decideTrash(TrashAvailability availability);

///
/// \brief 单个条目的删除结果（PRD: PLAT-003）。
///
/// 逐条目记录，而不是只留一个整体的成功/失败。批量删除需要能告诉用户
/// 「这 8 个进了回收站、那 2 个因为网络盘没有回收站所以没动」——
/// 只给一个布尔值，用户只能自己去数。
///
struct TrashRecord
{
    QString originalPath;  ///< 用户给的原始路径
    QString trashedPath;   ///< 删除成功后条目在回收站中的实际路径；失败时为空

    /// 失败原因。类型是 ErrorCode 而不是 FileSystemError：回收站失败最常见的是
    /// 「无权限」（回收站目录属于别人）与「被占用」，这两种恰恰是用户要拿着
    /// 原始错误码去搜才能查清的情形（例如 macOS 的 513 与 257 都归无权限，
    /// 但一个是读、一个是写）。见 filesystem.h 里 ErrorCode 的说明。
    ErrorCode error;

    bool succeeded() const { return error.ok(); }
};

///
/// \brief 一次批量删除的完整报告（PRD: PLAT-003）。
///
struct TrashReport
{
    QVector<TrashRecord> records;

    /// 全部条目都进了回收站。空批次视为成功（没有失败项）。
    bool succeeded() const;

    /// 至少有一个条目进了回收站。
    bool anySucceeded() const;

    /// 第一个失败条目的错误分类；全成功时返回 None。
    ///
    /// 注意「第一个」而不是「最严重的一个」：报告的用途是提示用户，
    /// 而用户接下来要做的是从第一批没成功的条目继续处理，顺序上
    /// 更容易和界面上看到的一致。
    FileSystemError firstError() const;

    /// 第一个失败条目的完整错误（含原始系统码），供排查用。
    /// 全成功时返回一个 ok() 为真的 ErrorCode。
    ErrorCode firstErrorCode() const;

    /// 成功进入回收站的条目在回收站中的路径（按原顺序）。
    QStringList trashedPaths() const;

    /// 失败条目的原始路径（按原顺序）。
    QStringList failedPaths() const;
};

///
/// \brief 回收站服务（PRD: PLAT-003）。
///
/// 为什么是独立服务，而不是 FileSystem 上的一个方法
/// ----------------------------------------------
/// `deleteToTrash` 曾经定义在 FileSystem 接口上。PLAT-003 落地时把它移到这里，
/// 原因是回收站有两处语义超出了「文件系统元数据操作」的范围：
///
///   1. **它是有状态的**。规格要求「提供从回收站还原最近一次删除的能力」，
///      这需要记住上一次删了什么、删到哪儿去了。而 FileSystem 的方法
///      全部标 const，契约是「无状态，改的是文件系统这个外部状态」。
///      把状态塞进 FileSystem 会让那条契约失效。
///   2. **它的抽象层级更高**。它要跨卷判断、要处理配额、要知道「这个位置
///      有没有回收站」——这些不是路径与元数据，是平台策略。
///
/// 同时保留 FileSystem 上的同名方法才是真正的坑：两个删除入口意味着
/// 「删除该走哪条路」有了两个事实来源，某天有人调错一个就会绕过可逆性保证。
/// 因此 FileSystem 上那份已经删除（见 filesystem.h 的说明）。
///
/// 模板方法：把「绝不静默降级」冻结在基类
/// ------------------------------------
/// 派生类只需要实现 trashPaths()，也就是「真正把文件挪走」这一步。
/// 「先查可用性、不可用就一个字节都不动、成功后记录撤销点」这套流程
/// 写在基类的 deleteToTrash() 里。这样任何一个平台实现都不可能不小心
/// 跳过可用性检查——因为它的代码根本不在那条路径上。
///
class TrashService
{
public:
    virtual ~TrashService();

    /// 平台名称，用于日志与测试断言（"macos" / "linux" / "windows"）。
    virtual QString platformName() const = 0;

    /// 查询某个路径所在的卷是否支持进回收站。
    ///
    /// 传目录或文件都可以。路径不存在时也应能给出答案（删除前判断可用性时
    /// 文件通常还在，但同步场景下可能刚被别的进程处理掉）。
    virtual TrashAvailability availabilityFor(const QString &path) const = 0;

    /// 回收站的可读位置，用于提示「已移到 ~/.Trash」这类信息。
    /// 平台没有可读位置时返回空串。
    virtual QString displayLocation() const = 0;

    // --- 对外接口（不应被派生类覆写）-------------------------------------

    /// 删除到回收站。
    ///
    /// 流程固定为三步，不可跳过：
    ///   1. 逐个条目查可用性；**任意**条目不可用即整体拒绝执行
    ///      （不是跳过那一条继续删其它的——批量的语义是「要么都做，要么都不做」，
    ///      部分执行会让用户以为已删的还在、还在的已删）。
    ///   2. 调用派生类的 trashPaths() 真正搬移。
    ///   3. 把成功的条目记为撤销点，供 undoLastDelete() 使用。
    TrashReport deleteToTrash(const QStringList &paths) const;

    /// 还原「最近一次成功删除到回收站」的全部条目。
    ///
    /// 返回 false 时 error 给出原因。受平台能力限制无法还原时返回
    /// NotSupported，并在 errorMessage 里说明——而不是假装成功。
    /// 出参类型是 ErrorCode，便于把原始系统错误码一起交出来（PLAT-008）。
    virtual bool undoLastDelete(ErrorCode *error = nullptr) const = 0;

    /// 最近一次删除的记录（可能包含失败条目）。从未删除过时 records 为空。
    ///
    /// 不设为纯虚：撤销点在基类的 deleteToTrash() 里统一记录，
    /// 派生类不需要各自维护一份（那样会有两份记录，迟早不一致）。
    TrashReport lastDelete() const;

protected:
    /// 真正把条目搬进回收站。派生类实现这一步；可用性检查由基类负责。
    ///
    /// 约定：
    ///   - 每个条目都要在返回值里出现一条 TrashRecord，路径原样回填 originalPath。
    ///   - 成功的条目必须填 trashedPath（回收站内的实际路径），
    ///     否则撤销无法定位——macOS 与 Linux 都会在重名时改名，
    ///     想当然地按原名拼回收站路径是错的。
    ///   - 不允许在这里做永久删除。
    virtual TrashReport trashPaths(const QStringList &paths) const = 0;

    // 撤销点是「回收站服务」这个对象的一部分状态，而 deleteToTrash() 在接口上
    // 标为 const（与 FileSystem 的写方法同理：改的是外部状态而不是对象自身）。
    // 因此这里标 mutable，与 FakeFileSystem 里调用日志的处理方式一致。
    mutable TrashReport m_lastDelete;
};

///
/// \brief 创建当前平台的原生实现。
///
/// 与 createNativeFileSystem 的约定一致：永不返回 nullptr，平台不被支持时
/// 返回一个所有查询都答 PlatformNotSupported 的实现，上层不必到处判空。
///
TrashService *createNativeTrashService();

// -----------------------------------------------------------------------------
// XDG 回收站规范的路径规则（Linux）
// -----------------------------------------------------------------------------

///
/// 为什么把 Linux 的回收站规则放在平台无关层
/// ----------------------------------------
/// 与 pathutils.h 里放 Windows 路径规则是同一个理由：XDG 规范里真正容易写错的
/// 是「该用哪个废纸篓目录」的判断（分同卷/跨卷/回退三种情况）和 .trashinfo
/// 的格式（路径要百分号编码、日期是本地时间且不带时区）。这两件事都是纯字符串
/// 处理，不需要 Linux 就能测。
///
/// 留在 trash_linux.cpp 里的只有真正的系统调用（mkdir、rename、写入 info 文件）。
/// 这样 macOS 上可以覆盖 Linux 回收站最容易出错的那部分逻辑。
///

/// XDG 规范里回收站内的两个子目录名。
namespace XdgTrash {
constexpr const char *FilesSubdirectory = "files";   ///< 被删除的条目本体
constexpr const char *InfoSubdirectory = "info";     ///< 每个条目的 .trashinfo 元数据
constexpr const char *InfoSuffix = ".trashinfo";
} // namespace XdgTrash

///
/// \brief 家目录回收站根目录（PRD: PLAT-003 完成标准第 3 条）。
///
/// 返回 `$XDG_DATA_HOME/Trash`，`$XDG_DATA_HOME` 为空时按规范取
/// `$HOME/.local/share`。家目录为空（例如以服务身份运行、环境里没有 HOME）时
/// 返回空串。
///
/// 这个位置是 XDG 规范里唯一一个「桌面环境的回收站会显示出来」的地方，
/// 因此只要条目与家目录同卷就一律用它。跨卷时用 xdgVolumeTrashDirectory()。
///
QString xdgHomeTrashDirectory(const QString &homeDirectory, const QString &dataHome);

///
/// \brief 某个卷内部的回收站根目录（跨卷时用）。
///
/// 按 XDG 规范依次尝试：
///   1. `<挂载点>/.Trash/<uid>` —— 首选。规范要求 `.Trash` 目录带 sticky 位，
///      目的是让同一台机器上的多个用户共用一个目录而互不干扰。
///   2. `<挂载点>/.Trash-<uid>` —— 第 1 步因权限失败时的回退（挂载点只读、
///      或 `.Trash` 已属于别的用户）。
///
/// \param canCreateOnVolume 该卷是否可写。传 false（网络盘、只读挂载）时返回空串，
///        也就是明确「这里没有回收站」——而不是硬凑一个建不出来的路径，
///        让调用方在删除中途才发现失败。
///
/// \return 首选的回收站根目录；该卷不支持时返回空串。
///         调用方在该路径建目录失败时，改用 xdgVolumeTrashFallbackDirectory()。
///
QString xdgVolumeTrashDirectory(const QString &volumeMountPoint, quint32 userId, bool canCreateOnVolume);

/// 卷内回收站的回退位置：`<挂载点>/.Trash-<uid>`。挂载点为空时返回空串。
QString xdgVolumeTrashFallbackDirectory(const QString &volumeMountPoint, quint32 userId);

///
/// \brief 生成一个 .trashinfo 文件的内容（PRD: PLAT-003）。
///
/// 规范格式（键名与大小写都是规范的一部分，写错会被桌面环境忽略）：
///
///     [Trash Info]
///     Path=/home/user/notes/todo.txt
///     DeletionDate=2026-03-01T12:34:56
///
/// 两个容易写错的点，都在这里处理掉：
///   - `Path` 必须做 URL 百分号编码。含空格或非 ASCII 的文件名（中文文件名很常见）
///     不编码会让解析方截断，还原时回到一个不存在的路径。
///   - `DeletionDate` 用**本地时间且不带时区后缀**。规范如此规定，
///     写成 UTC 或带 "Z" 会让部分实现解析失败——这也是为什么这里收 QDateTime
///     而不是收 FileTime（后者内部恒为 UTC，语义不符）。
///
QString xdgTrashInfoContents(const QString &originalPath, const QDateTime &deletionDateLocal);

///
/// \brief 从 .trashinfo 内容里解析出原始路径（用于还原）。
///
/// 解析失败时返回空串。未知的键会被忽略——规范允许实现追加自定义键，
/// 遇到不认识的键就当成错误会让别人的回收站里的条目标不出来。
///
QString parseXdgTrashInfoPath(const QString &contents);

} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_TRASH_H
