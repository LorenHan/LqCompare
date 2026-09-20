#include "trash.h"

#include <QDir>
#include <QUrl>

namespace LqCompare {
namespace Files {

// -----------------------------------------------------------------------------
// 可用性
// -----------------------------------------------------------------------------

const char *trashAvailabilityIdentifier(TrashAvailability availability)
{
    // 返回稳定的英文标识而不是可翻译文案：这个字符串会出现在日志与测试断言里，
    // 一旦跟着界面语言变，日志就没法跨版本比对了。给用户看的是 decideTrash() 的文案。
    switch (availability) {
    case TrashAvailability::Available:
        return "available";
    case TrashAvailability::VolumeNotSupported:
        return "volume-not-supported";
    case TrashAvailability::QuotaExceeded:
        return "quota-exceeded";
    case TrashAvailability::NoSpace:
        return "no-space";
    case TrashAvailability::PlatformNotSupported:
        return "platform-not-supported";
    case TrashAvailability::Unknown:
        return "unknown";
    }
    return "unknown";
}

bool isTrashUsable(TrashAvailability availability)
{
    // 只有 Available 算可用。其余一律视为「必须先问用户」——
    // 包括 Unknown。探测失败时冒的险是用户的文件，不值得赌。
    return availability == TrashAvailability::Available;
}

TrashDecision decideTrash(TrashAvailability availability)
{
    TrashDecision decision;
    decision.availability = availability;
    decision.requiresUserChoice = !isTrashUsable(availability);

    // reason 说明「为什么不行」，advice 说明「怎么办」。分开写是因为二者面向
    // 不同的问题：只说原因用户不知道该做什么，只说建议用户会怀疑为什么被拦住。
    switch (availability) {
    case TrashAvailability::Available:
        break;

    case TrashAvailability::VolumeNotSupported:
        decision.reason = QStringLiteral("这个位置所在的磁盘没有回收站（网络位置和可移动盘常见）");
        decision.advice = QStringLiteral("取消本次删除，或先把文件复制到本机磁盘，再从本机磁盘删除");
        break;

    case TrashAvailability::QuotaExceeded:
        decision.reason = QStringLiteral("回收站的容量配额已经用满");
        decision.advice = QStringLiteral("清空回收站后重试，或取消本次删除");
        break;

    case TrashAvailability::NoSpace:
        decision.reason = QStringLiteral("回收站所在的磁盘空间不足，删进去也放不下");
        decision.advice = QStringLiteral("清理磁盘空间后重试，或取消本次删除");
        break;

    case TrashAvailability::PlatformNotSupported:
        decision.reason = QStringLiteral("当前系统环境没有可用的回收站");
        // 这种情况没有任何替代方案，只能让用户在「不做」和「不可逆」之间选。
        // 建议里必须明说不可逆，否则用户会把「永久删除」当成和平时一样的删除。
        decision.advice = QStringLiteral("建议取消；确认不需要后可以选择永久删除，删除后无法还原");
        break;

    case TrashAvailability::Unknown:
        decision.reason = QStringLiteral("无法确认这个位置是否支持回收站");
        decision.advice = QStringLiteral("建议取消本次删除；确认不需要后可以选择永久删除");
        break;
    }

    return decision;
}

// -----------------------------------------------------------------------------
// 删除报告
// -----------------------------------------------------------------------------

bool TrashReport::succeeded() const
{
    // 空批次算成功：没有失败项。这一点要和「任何一个条目失败即整体失败」的
    // 批量语义一致，否则上层对空批次的判断会和单条删除不一致。
    for (const TrashRecord &record : records) {
        if (!record.succeeded())
            return false;
    }
    return true;
}

bool TrashReport::anySucceeded() const
{
    for (const TrashRecord &record : records) {
        if (record.succeeded())
            return true;
    }
    return false;
}

FileSystemError TrashReport::firstError() const
{
    for (const TrashRecord &record : records) {
        if (!record.succeeded())
            return record.error;
    }
    return FileSystemError::None;
}

ErrorCode TrashReport::firstErrorCode() const
{
    for (const TrashRecord &record : records) {
        if (!record.succeeded())
            return record.error;
    }
    // 全部成功时返回一个空的 ErrorCode（ok() 为真），而不是「最后一个错误」——
    // 后者会让调用方在成功路径上拿到一个过期的失败原因。
    return ErrorCode();
}

QStringList TrashReport::trashedPaths() const
{
    QStringList result;
    for (const TrashRecord &record : records) {
        if (record.succeeded() && !record.trashedPath.isEmpty())
            result.append(record.trashedPath);
    }
    return result;
}

QStringList TrashReport::failedPaths() const
{
    QStringList result;
    for (const TrashRecord &record : records) {
        if (!record.succeeded())
            result.append(record.originalPath);
    }
    return result;
}

// -----------------------------------------------------------------------------
// 服务基类
// -----------------------------------------------------------------------------

TrashService::~TrashService() = default;

TrashReport TrashService::deleteToTrash(const QStringList &paths) const
{
    TrashReport report;

    // 第一步：**先**逐个查可用性，全部通过才动手。
    //
    // 这里刻意不做「跳过不可用的、继续删其余的」。批量的语义是「要么都做、
    // 要么都不做」：部分执行会让用户看到「文件从列表里消失了」但「回收站里
    // 只有一部分」，而这恰恰是最难恢复的状态——用户不知道自己丢了什么。
    //
    // 也刻意不看「第一个不可用的就返回」，而是把所有条目都检查一遍，
    // 这样提示里能一次说清有几个条目不行，用户不必删一次被拦一次。
    QStringList unusable;
    for (const QString &path : paths) {
        if (!isTrashUsable(availabilityFor(path)))
            unusable.append(path);
    }

    if (!unusable.isEmpty()) {
        // 拒绝执行时也要返回逐条目记录：调用方拿到的是「每个条目都没动、
        // 原因是 NotSupported」，而不是一个空报告或一句笼统的失败。
        for (const QString &path : paths) {
            TrashRecord record;
            record.originalPath = path;
            record.error = FileSystemError::NotSupported;
            report.records.append(record);
        }
        m_lastDelete = report;
        return report;
    }

    // 第二步：交给平台实现真正搬移。
    report = trashPaths(paths);

    // 第三步：只有确实有条目进了回收站，才更新撤销点。
    //
    // 为什么失败时也要覆盖 m_lastDelete：撤销点表示的是「最近一次删除」。
    // 如果一次失败的空操作不更新它，用户点「撤销」还原的会是更早的那次删除——
    // 而界面上明明刚提示过一次失败，撤销的对象和用户以为的不一致。
    // 失败批次里成功的那些条目仍然是有效的撤销目标。
    m_lastDelete = report;

    return report;
}

TrashReport TrashService::lastDelete() const
{
    return m_lastDelete;
}

// -----------------------------------------------------------------------------
// XDG 回收站路径规则（Linux；放在这里以便在任意平台测试）
// -----------------------------------------------------------------------------

namespace {

/// 带结尾分隔符的路径，便于直接和子目录名拼接。
QString withTrailingSeparator(const QString &path)
{
    if (path.isEmpty())
        return path;
    if (path.endsWith(QLatin1Char('/')))
        return path;
    return path + QLatin1Char('/');
}

} // namespace

QString xdgHomeTrashDirectory(const QString &homeDirectory, const QString &dataHome)
{
    // 家目录为空说明拿不到 HOME。此时既不能凭空编一个路径，也不能退回卷内回收站
    // ——那是调用方（平台实现）该做的决定，因为只有它知道条目在哪个卷上。
    if (homeDirectory.isEmpty())
        return QString();

    const QString dataHomePath = dataHome.isEmpty()
            ? withTrailingSeparator(homeDirectory) + QStringLiteral(".local/share")
            : dataHome;

    return withTrailingSeparator(dataHomePath) + QStringLiteral("Trash");
}

QString xdgVolumeTrashDirectory(const QString &volumeMountPoint, quint32 userId, bool canCreateOnVolume)
{
    if (volumeMountPoint.isEmpty())
        return QString();

    // 该卷不可写（网络盘、只读挂载）时没有回收站可言。
    // 明确返回空串，让调用方在删除**之前**就能告诉用户，
    // 而不是先报「可以删」再在搬移时失败。
    if (!canCreateOnVolume)
        return QString();

    // <挂载点>/.Trash/<uid>
    return withTrailingSeparator(withTrailingSeparator(volumeMountPoint) + QStringLiteral(".Trash"))
            + QString::number(userId);
}

QString xdgVolumeTrashFallbackDirectory(const QString &volumeMountPoint, quint32 userId)
{
    if (volumeMountPoint.isEmpty())
        return QString();

    // <挂载点>/.Trash-<uid>：当 .Trash 目录已存在且属于别的用户、
    // 当前用户无权在其中建自己的子目录时使用。
    return withTrailingSeparator(volumeMountPoint) + QStringLiteral(".Trash-")
            + QString::number(userId);
}

QString xdgTrashInfoContents(const QString &originalPath, const QDateTime &deletionDateLocal)
{
    if (originalPath.isEmpty())
        return QString();

    // 路径要做百分号编码。保留 '/'，因为规范里 Path 是路径而不是单个名字，
    // 编码掉分隔符会让解析方得到一个无法还原的字符串。
    //
    // 必须编码的典型场景：文件名里有空格、'#'、'?'、'%'、或中文。
    // 尤其是 '%' —— 不编码的话解析方会把它当成转义序列的开头，
    // 导致还原到一个完全不同的路径。
    const QString encodedPath = QString::fromLatin1(
            QUrl::toPercentEncoding(originalPath, QByteArrayLiteral("/")));

    // 日期用本地时间、不带时区后缀。规范（XDG Trash spec 1.0）明确如此，
    // 写成 ISO 8601 的 "Z" 形式部分实现会解析失败。
    // 秒级精度足够：这个字段用于在回收站里显示「什么时候删的」，
    // 不参与任何比对逻辑，因此不需要文件系统时间戳那套纳秒精度。
    const QString stamp = deletionDateLocal.isValid()
            ? deletionDateLocal.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
            : QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));

    // 末尾必须有一个换行：规范给的是「行」的格式，且部分解析器按行读，
    // 最后一行没有换行会读不到。写上去比省一个字节更安全。
    return QStringLiteral("[Trash Info]\nPath=") + encodedPath
            + QStringLiteral("\nDeletionDate=") + stamp + QLatin1Char('\n');
}

QString parseXdgTrashInfoPath(const QString &contents)
{
    if (contents.isEmpty())
        return QString();

    bool inTrashInfoSection = false;
    QString encodedPath;

    const QStringList lines = contents.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        // 只认 [Trash Info] 段里的键。规范允许文件里存在其它段（[Trash Info]
        // 之外的内容实现可以自行扩展），不加段判断会把别的段里的 Path 当成目标。
        if (line.startsWith(QLatin1Char('['))) {
            inTrashInfoSection = (line == QLatin1String("[Trash Info]"));
            continue;
        }

        if (!inTrashInfoSection)
            continue;

        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0)
            continue;

        const QString key = line.left(separator).trimmed();
        if (key != QLatin1String("Path"))
            continue;

        // 值里可能带前导空格（"Path = /x"），去掉再解码。
        encodedPath = line.mid(separator + 1).trimmed();
        break;
    }

    if (encodedPath.isEmpty())
        return QString();

    // 相对路径（不以 '/' 开头）在规范里表示相对于家目录。本项目删除时一律写
    // 绝对路径，但读到别人写的相对路径时不能直接当成绝对路径用——
    // 交给调用方判断更清楚，这里原样返回。
    return QUrl::fromPercentEncoding(encodedPath.toUtf8());
}

// -----------------------------------------------------------------------------
// 平台实现的分发
// -----------------------------------------------------------------------------

// 三个工厂函数由各自的平台文件提供实现：
//   trash_mac.mm    —— macOS（Foundation，必须是 .mm）
//   trash_linux.cpp —— Linux（XDG 规范）
//   trash_win.cpp   —— Windows（SHFileOperation + FOF_ALLOWUNDO）
TrashService *createMacTrashService();
TrashService *createLinuxTrashService();
TrashService *createWindowsTrashService();

namespace {

///
/// \brief 没有任何回收站能力的兜底实现。
///
/// 什么时候会用到：编译到一个三个平台实现都没覆盖的目标上（例如 BSD）。
/// 它的存在是为了满足 createNativeTrashService 的契约——永不返回 nullptr，
/// 上层不必到处判空。
///
/// 它把所有可用性查询都答成 PlatformNotSupported，于是基类的 deleteToTrash()
/// 会自动拒绝执行。**这就是兜底实现唯一正确的行为**：不能因为「不知道怎么办」
/// 就退化成真删。用户会看到「当前系统环境没有可用的回收站」并可以选择永久删除，
/// 那是一个知情的选择；静默真删不是。
///
class UnsupportedTrashService : public TrashService
{
public:
    QString platformName() const override { return QStringLiteral("unsupported"); }

    TrashAvailability availabilityFor(const QString &path) const override
    {
        Q_UNUSED(path);
        return TrashAvailability::PlatformNotSupported;
    }

    QString displayLocation() const override { return QString(); }

    bool undoLastDelete(ErrorCode *error) const override
    {
        if (error)
            *error = FileSystemError::NotSupported;
        return false;
    }

protected:
    TrashReport trashPaths(const QStringList &paths) const override
    {
        // 走不到这里：基类在可用性检查阶段就会拦下所有条目。
        // 仍然逐条目返回记录而不是直接断言，是为了万一将来有人把可用性检查
        // 打开一个口子时，行为仍然是「明确失败」而不是崩溃。
        TrashReport report;
        for (const QString &path : paths) {
            TrashRecord record;
            record.originalPath = path;
            record.error = FileSystemError::NotSupported;
            report.records.append(record);
        }
        return report;
    }
};

} // namespace

TrashService *createNativeTrashService()
{
    // 用 #ifdef 而不是运行期判断：平台实现要调用各自的系统 API，
    // 那些头文件在别的平台上根本不存在，必须在编译期就排除掉。
#if defined(Q_OS_WIN)
    return createWindowsTrashService();
#elif defined(Q_OS_MACOS)
    return createMacTrashService();
#elif defined(Q_OS_LINUX)
    return createLinuxTrashService();
#else
    return new UnsupportedTrashService();
#endif
}

} // namespace Files
} // namespace LqCompare
