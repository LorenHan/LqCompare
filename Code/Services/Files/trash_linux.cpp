// Linux 的回收站实现（PRD: PLAT-003 完成标准第 3 条）。
//
// 遵循 XDG Trash 规范，不调用 gio trash
// -----------------------------------
// 规格允许二选一（「遵循 XDG 规范或调用 gio trash」）。这里选前者，理由有三：
//   1. 调 gio 要启动另一个进程，删除几百个条目时会变成几百次 fork/exec；
//      XDG 的搬移是一次 rename，同一卷内是原子的、几乎零成本。
//   2. gio 不一定装了（精简发行版、容器镜像里常见），而 /proc 与 statvfs 一定在。
//   3. 「从回收站还原最近一次删除」需要知道条目在回收站里的实际路径。走 XDG
//      时这个路径是我们自己定的，走 gio 时只能靠事后去猜它放到了哪里。
//
// 规范里真正容易写错的那部分（该用哪个废纸篓目录、.trashinfo 的编码与日期格式）
// 已经放在平台无关层的 trash.cpp 里，因此在本机（macOS）上就是被真实测试过的。
// 这个文件里剩下的只有系统调用。

#include "trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

namespace LqCompare {
namespace Files {

namespace {

const PathUtils::Style kPosix = PathUtils::Style::posix();

QByteArray encoded(const QString &path)
{
    // QFile::encodeName 而不是 toLocal8Bit：前者在 Linux 上走的是文件名编码约定，
    // 与 QFile 自己的行为保持一致，不会出现「QFile 能打开、这里打不开」的分叉。
    return QFile::encodeName(path);
}

/// $XDG_DATA_HOME，未设置时按规范返回空串（由 xdgHomeTrashDirectory 兜底）。
QString xdgDataHome()
{
    const QByteArray value = qgetenv("XDG_DATA_HOME");
    if (value.isEmpty())
        return QString();
    return QFile::decodeName(value);
}

/// 路径（或它最近的已存在祖先）所在的挂载点。
///
/// 做法是从路径向上走，直到某一级目录的设备号与它的父目录不同——那一级就是挂载点。
/// 不用 /proc/mounts 的原因：
///   - /proc 在容器里可能被遮蔽或内容不完整；
///   - 字符串上的「最长匹配挂载点」在 bind mount 与符号链接路径上会给出错误答案，
///     而设备号比较是内核给出的、不需要文本解析的事实。
QString findMountPoint(const QString &path)
{
    QString current = path;
    if (current.isEmpty())
        return QString();

    // 条目本身可能已经不存在（同步场景），向上找到最近的已存在祖先。
    struct stat st;
    while (::stat(encoded(current).constData(), &st) != 0) {
        const QString parent = PathUtils::parentPath(current, kPosix);
        if (parent.isEmpty() || parent == current)
            return QString();
        current = parent;
    }

    const dev_t device = st.st_dev;

    while (true) {
        const QString parent = PathUtils::parentPath(current, kPosix);
        if (parent.isEmpty() || parent == current)
            return current; // 已经到根

        struct stat parentStat;
        if (::stat(encoded(parent).constData(), &parentStat) != 0)
            return current;

        if (parentStat.st_dev != device)
            return current; // current 就是挂载点

        current = parent;
    }
}

bool sameVolume(const QString &left, const QString &right)
{
    struct stat leftStat;
    struct stat rightStat;
    if (::stat(encoded(left).constData(), &leftStat) != 0)
        return false;
    if (::stat(encoded(right).constData(), &rightStat) != 0)
        return false;
    return leftStat.st_dev == rightStat.st_dev;
}

/// 目录是否存在且可写。不存在时尝试创建。
///
/// availabilityFor() 里带创建副作用是有意的：XDG 规范下「这个卷有没有回收站」
/// 等价于「能不能在那里建出 Trash 目录」，不存在一个不需要尝试就能得到答案的查询。
/// 反过来若只检查父目录的可写权限，会漏掉「父目录可写但 .Trash 已属于别人」的情况，
/// 于是删除会在中途失败——那正是探测要避免的。
bool ensureWritableDirectory(const QString &path)
{
    if (path.isEmpty())
        return false;

    // mkpath 对已存在的目录也返回 true，因此这里不需要先判断存在性。
    if (!QDir().mkpath(path))
        return false;

    return ::access(encoded(path).constData(), W_OK | X_OK) == 0;
}

bool pathExists(const QString &path)
{
    return ::access(encoded(path).constData(), F_OK) == 0;
}

/// 在回收站的 files 目录里挑一个不重名的名字。
///
/// 规范建议重名时用 "<名字>.N"。同时要检查 info 目录：.trashinfo 文件是按名字
/// 一一对应的，只看 files 目录会写出一个覆盖已有元数据的 .trashinfo，
/// 结果是**另一个**条目的还原信息被抹掉。
QString uniqueNameIn(const QString &filesDirectory, const QString &infoDirectory, const QString &fileName)
{
    if (!pathExists(filesDirectory + QLatin1Char('/') + fileName)
        && !pathExists(infoDirectory + QLatin1Char('/') + fileName
                       + QString::fromLatin1(XdgTrash::InfoSuffix))) {
        return fileName;
    }

    for (int index = 2; index < 10000; ++index) {
        const QString candidate = fileName + QLatin1Char('.') + QString::number(index);
        if (!pathExists(filesDirectory + QLatin1Char('/') + candidate)
            && !pathExists(infoDirectory + QLatin1Char('/') + candidate
                           + QString::fromLatin1(XdgTrash::InfoSuffix))) {
            return candidate;
        }
    }

    return QString();
}

/// 从回收站内的条目路径反推回收站根目录：.../Trash/files/名字 -> .../Trash
QString trashRootOf(const QString &trashedPath)
{
    const QFileInfo entry(trashedPath);
    const QString filesDirectory = entry.absolutePath();          // .../Trash/files
    return QFileInfo(filesDirectory).absolutePath();              // .../Trash
}

QString filesDirectoryOf(const QString &trashRoot)
{
    return trashRoot + QLatin1Char('/') + QString::fromLatin1(XdgTrash::FilesSubdirectory);
}

QString infoDirectoryOf(const QString &trashRoot)
{
    return trashRoot + QLatin1Char('/') + QString::fromLatin1(XdgTrash::InfoSubdirectory);
}

} // namespace

///
/// \brief Linux（XDG）回收站后端。
///
class LinuxTrashService : public TrashService
{
public:
    QString platformName() const override { return QStringLiteral("linux"); }

    TrashAvailability availabilityFor(const QString &path) const override;

    QString displayLocation() const override
    {
        const QString directory = xdgHomeTrashDirectory(QDir::homePath(), xdgDataHome());
        return directory.isEmpty() ? QString() : directory;
    }

    bool undoLastDelete(FileSystemError *error) const override;

protected:
    TrashReport trashPaths(const QStringList &paths) const override;

private:
    /// 该条目的首选回收站根目录；不可用时返回空串。
    QString trashRootFor(const QString &path) const;
};

QString LinuxTrashService::trashRootFor(const QString &path) const
{
    const QString home = QDir::homePath();
    if (home.isEmpty())
        return QString();

    if (sameVolume(path, home)) {
        // 与家目录同卷 —— 用家目录里的回收站。这是唯一一个桌面环境的
        // 文件管理器会显示出来的位置。
        return xdgHomeTrashDirectory(home, xdgDataHome());
    }

    const QString mountPoint = findMountPoint(path);
    if (mountPoint.isEmpty())
        return QString();

    // 跨卷：先试 .Trash/<uid>。
    const QString volumeTrash = xdgVolumeTrashDirectory(mountPoint, ::getuid(), true);
    if (!volumeTrash.isEmpty() && ensureWritableDirectory(volumeTrash))
        return volumeTrash;

    // 回退到 .Trash-<uid>（.Trash 已存在且属于别的用户时走这条）。
    const QString fallback = xdgVolumeTrashFallbackDirectory(mountPoint, ::getuid());
    if (!fallback.isEmpty() && ensureWritableDirectory(fallback))
        return fallback;

    return QString();
}

TrashAvailability LinuxTrashService::availabilityFor(const QString &path) const
{
    const QString mountPoint = findMountPoint(path);
    if (mountPoint.isEmpty())
        return TrashAvailability::Unknown;

    // 先看空间。回收站满了的时候删不进去，而且处置建议（清理磁盘）与其他原因
    // 完全不同，所以必须单独报出来。
    struct statvfs fileSystemInfo;
    if (::statvfs(encoded(mountPoint).constData(), &fileSystemInfo) == 0) {
        if (fileSystemInfo.f_bavail == 0)
            return TrashAvailability::NoSpace;

        // 只读挂载点：不是「没有回收站」而是「整个卷都不能写」。
        // 分开报是因为建议不同——只读挂载要重新挂载，没回收站要换位置。
        if ((fileSystemInfo.f_flag & ST_RDONLY) != 0)
            return TrashAvailability::VolumeNotSupported;
    }

    if (trashRootFor(path).isEmpty())
        return TrashAvailability::VolumeNotSupported;

    // 注意这里不返回 QuotaExceeded：XDG 规范里没有「回收站配额」这个概念，
    // 回收站只受所属卷的剩余空间约束（已由上面的 NoSpace 覆盖）。
    // 保留该状态是为了让上层不必按平台分支——Windows 实现会用到它。
    return TrashAvailability::Available;
}

TrashReport LinuxTrashService::trashPaths(const QStringList &paths) const
{
    TrashReport report;

    for (const QString &path : paths) {
        TrashRecord record;
        record.originalPath = path;

        const QString trashRoot = trashRootFor(path);
        if (trashRoot.isEmpty()) {
            record.error = FileSystemError::NotSupported;
            report.records.append(record);
            continue;
        }

        const QString filesDirectory = filesDirectoryOf(trashRoot);
        const QString infoDirectory = infoDirectoryOf(trashRoot);
        if (!ensureWritableDirectory(filesDirectory) || !ensureWritableDirectory(infoDirectory)) {
            record.error = FileSystemError::PermissionDenied;
            report.records.append(record);
            continue;
        }

        const QString name = uniqueNameIn(filesDirectory, infoDirectory,
                                          QFileInfo(path).fileName());
        if (name.isEmpty()) {
            // 试了一万次都不重名可用，说明这个目录已经病得不轻了。
            record.error = FileSystemError::AlreadyExists;
            report.records.append(record);
            continue;
        }

        const QString target = filesDirectory + QLatin1Char('/') + name;

        // 先搬移，再写元数据。
        //
        // 顺序不能反：先写 .trashinfo 而搬移失败的话，回收站里会留下一条
        // 指向不存在条目的记录，桌面环境的回收站里就会出现一个点不开的幽灵条目。
        if (::rename(encoded(path).constData(), encoded(target).constData()) != 0) {
            record.error = classifySystemError(errno);
            report.records.append(record);
            continue;
        }

        // 搬移成功但元数据写失败时必须**回滚**。
        //
        // 没有 .trashinfo 的条目在回收站里是「有身体没身份证」：文件在，
        // 但谁也不知道它原本在哪，还原不回去。这种状态下用户看到的是
        // 「删除成功」，然后永远拿不回那个文件——比直接报删除失败糟得多。
        const QString infoPath = infoDirectory + QLatin1Char('/') + name
                + QString::fromLatin1(XdgTrash::InfoSuffix);

        QFile infoFile(infoPath);
        const QByteArray contents =
                xdgTrashInfoContents(path, QDateTime::currentDateTime()).toUtf8();

        bool infoWritten = false;
        if (infoFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            infoWritten = infoFile.write(contents) == contents.size();
            infoFile.close();
        }

        if (!infoWritten) {
            ::rename(encoded(target).constData(), encoded(path).constData());
            infoFile.remove();
            record.error = classifySystemError(errno);
            report.records.append(record);
            continue;
        }

        record.error = FileSystemError::None;
        record.trashedPath = target;
        report.records.append(record);
    }

    return report;
}

bool LinuxTrashService::undoLastDelete(FileSystemError *error) const
{
    const TrashReport report = lastDelete();

    if (report.records.isEmpty()) {
        if (error)
            *error = FileSystemError::NotFound;
        return false;
    }

    for (const TrashRecord &record : report.records) {
        if (!record.succeeded() || record.trashedPath.isEmpty())
            continue;

        const QString trashRoot = trashRootOf(record.trashedPath);
        const QString name = QFileInfo(record.trashedPath).fileName();
        const QString infoPath = infoDirectoryOf(trashRoot) + QLatin1Char('/') + name
                + QString::fromLatin1(XdgTrash::InfoSuffix);

        // 原始路径优先从 .trashinfo 里读，而不是直接用记录里的 originalPath。
        //
        // 为什么多这一步：.trashinfo 是**权威来源**，用户可能在这期间用桌面环境的
        // 回收站手工还原过、或者别的工具改过它。记录只是我们自己的备忘，
        // 与回收站的实际状态不一致时应该以后者为准。
        QString original = record.originalPath;

        QFile infoFile(infoPath);
        if (infoFile.open(QIODevice::ReadOnly)) {
            const QString fromInfo = parseXdgTrashInfoPath(QString::fromUtf8(infoFile.readAll()));
            infoFile.close();
            if (!fromInfo.isEmpty())
                original = fromInfo;
        }

        if (original.isEmpty()) {
            if (error)
                *error = FileSystemError::NotFound;
            return false;
        }

        // 原位置已被占用时不能覆盖：那会删掉一个用户没打算碰、而且不在回收站里的
        // 文件，删除后无法恢复。比「还原失败」严重得多。
        if (pathExists(original)) {
            if (error)
                *error = FileSystemError::AlreadyExists;
            return false;
        }

        if (::rename(encoded(record.trashedPath).constData(), encoded(original).constData()) != 0) {
            if (error)
                *error = classifySystemError(errno);
            return false;
        }

        // 条目回到原处之后 .trashinfo 就没有意义了，留着只会让回收站的
        // 元数据目录里堆积指向不存在条目的记录。
        QFile::remove(infoPath);
    }

    // 全部还原成功后才清掉撤销点。理由见 trash_mac.mm 里的同一处说明：
    // 不清的话第二次撤销会把已经回到原处的文件再挪走。
    m_lastDelete = TrashReport();

    if (error)
        *error = FileSystemError::None;
    return true;
}

// 工厂函数，由 trash.cpp 声明并分发。
TrashService *createLinuxTrashService()
{
    return new LinuxTrashService();
}

} // namespace Files
} // namespace LqCompare
