#include "fakefilesystem.h"

namespace LqCompare {
namespace Files {
namespace Test {

const char *FakeFileSystem::operationIdentifier(Operation operation)
{
    switch (operation) {
    case Operation::PathNormalize:  return "path-normalize";
    case Operation::Stat:           return "stat";
    case Operation::LinkTarget:     return "link-target";
    case Operation::Exists:         return "exists";
    case Operation::Enumerate:      return "enumerate";
    case Operation::SetTimes:       return "set-times";
    case Operation::SetAttributes:  return "set-attributes";
    case Operation::DeleteToTrash:  return "delete-to-trash";
    }
    return "unknown";
}

FakeFileSystem::FakeFileSystem()
{
    // 默认按 POSIX 语义，与开发机一致。需要 Windows 语义时显式调用
    // useWindowsSemantics()——让「当前在模拟哪个平台」在测试里始终是写明的。
    usePosixSemantics();
}

FakeFileSystem::~FakeFileSystem() = default;

// -----------------------------------------------------------------------------
// 平台语义
// -----------------------------------------------------------------------------

void FakeFileSystem::useWindowsSemantics()
{
    m_style = PathUtils::Style::windows();
    m_caseSensitivity = Qt::CaseInsensitive;
    m_platformName = QStringLiteral("windows");
}

void FakeFileSystem::usePosixSemantics()
{
    m_style = PathUtils::Style::posix();
    m_caseSensitivity = Qt::CaseSensitive;
    m_platformName = QStringLiteral("fake");
}

// -----------------------------------------------------------------------------
// 构造文件树
// -----------------------------------------------------------------------------

void FakeFileSystem::addDirectory(const QString &path)
{
    const QString normalized = PathUtils::normalize(path, m_style);

    // 逐级补齐父目录，与真实文件系统一致：/a/b/c 能存在就说明 /a 与 /a/b 都在。
    //
    // 实现上用 parentPath() 反复上溯收集祖先，再自根向下插入。
    // 一开始想手工切分路径来拼，但那样要分别处理 POSIX 根、Windows 盘符、
    // UNC 三种前缀（例如 split("C:\\a") 会把 "C:" 也当成一段），
    // 等于在假实现里再写一套「什么算父目录」的规则——两套规则必然有一天不一致。
    // 复用 parentPath() 就只有一套规则。
    QStringList chain;
    QString current = normalized;
    for (;;) {
        chain.prepend(current);

        const QString parent = PathUtils::parentPath(current, m_style);
        // 到达根（parentPath 返回自身）或相对路径的顶端（返回 "."）时停止。
        if (parent == current || parent.isEmpty() || parent == QLatin1String("."))
            break;
        current = parent;
    }

    for (const QString &entryPath : chain) {
        if (m_entries.contains(entryPath))
            continue;

        FileInfo info;
        info.path = entryPath;
        info.name = PathUtils::fileName(entryPath, m_style);
        info.exists = true;
        info.isDirectory = true;
        m_entries.insert(entryPath, info);
    }
}

void FakeFileSystem::addFile(const QString &path, quint64 size)
{
    const QString normalized = PathUtils::normalize(path, m_style);

    // 父目录在真实系统里必须存在，这里也照做——否则测试会构造出
    // 「文件在但父目录不在」这种真实世界不可能出现的状态，
    // 而基于它的断言没有意义。
    const QString parent = PathUtils::parentPath(normalized, m_style);
    if (!parent.isEmpty() && !m_entries.contains(parent))
        addDirectory(parent);

    FileInfo info;
    info.path = normalized;
    info.name = PathUtils::fileName(normalized, m_style);
    info.exists = true;
    info.isDirectory = false;
    info.size = size;
    m_entries.insert(normalized, info);
}

void FakeFileSystem::addSymLink(const QString &path, const QString &target)
{
    addFile(path);
    const QString normalized = PathUtils::normalize(path, m_style);

    FileInfo *info = findEntry(normalized);
    if (info == nullptr)
        return;

    info->isSymLink = true;
    info->attributes |= FileAttribute::SymLink;
    m_linkTargets.insert(normalized, target);
}

void FakeFileSystem::remove(const QString &path)
{
    const QString normalized = PathUtils::normalize(path, m_style);
    m_entries.remove(normalized);
    m_linkTargets.remove(normalized);
}

void FakeFileSystem::setModifiedTime(const QString &path, const FileTime &time)
{
    FileInfo *info = findEntry(PathUtils::normalize(path, m_style));
    if (info)
        info->lastModified = time;
}

void FakeFileSystem::setCreatedTime(const QString &path, const FileTime &time)
{
    FileInfo *info = findEntry(PathUtils::normalize(path, m_style));
    if (info)
        info->created = time;
}

void FakeFileSystem::setSize(const QString &path, quint64 size)
{
    FileInfo *info = findEntry(PathUtils::normalize(path, m_style));
    if (info)
        info->size = size;
}

void FakeFileSystem::setAttributesFor(const QString &path, FileAttributes attributes)
{
    FileInfo *info = findEntry(PathUtils::normalize(path, m_style));
    if (info)
        info->attributes = attributes;
}

// -----------------------------------------------------------------------------
// 故障注入
// -----------------------------------------------------------------------------

QString FakeFileSystem::failureKey(Operation operation, const QString &path)
{
    // 用 '|' 分隔：路径里不会含 '|'（它在两个平台上都是非法文件名字符），
    // 因此这个键不会产生歧义，且在测试失败信息里可以直接读。
    return QString::fromLatin1(operationIdentifier(operation)) + QLatin1Char('|') + path;
}

void FakeFileSystem::fail(Operation operation, const QString &path, FileSystemError error)
{
    m_failures.insert(failureKey(operation, PathUtils::normalize(path, m_style)), error);
}

void FakeFileSystem::clearFailures()
{
    m_failures.clear();
}

bool FakeFileSystem::intercept(Operation operation, const QString &path, FileSystemError *error) const
{
    const QString normalized = PathUtils::normalize(path, m_style);

    // 先记录再判断故障：即使这次调用注定失败，也必须出现在调用日志里。
    // 否则「某路径从未被访问」与「访问了但出错」两种完全不同的情况
    // 在测试里看起来一模一样。
    m_callLog.append(QString::fromLatin1(operationIdentifier(operation)) + QLatin1Char(':')
                     + normalized);

    const auto it = m_failures.constFind(failureKey(operation, normalized));
    if (it == m_failures.constEnd())
        return false;

    if (error)
        *error = it.value();
    return true;
}

bool FakeFileSystem::wasCalled(Operation operation, const QString &path) const
{
    const QString normalized = PathUtils::normalize(path, m_style);
    return m_callLog.contains(QString::fromLatin1(operationIdentifier(operation)) + QLatin1Char(':')
                              + normalized);
}

int FakeFileSystem::callCount(Operation operation) const
{
    const QString prefix = QString::fromLatin1(operationIdentifier(operation)) + QLatin1Char(':');
    int count = 0;
    for (const QString &entry : m_callLog) {
        if (entry.startsWith(prefix))
            ++count;
    }
    return count;
}

void FakeFileSystem::clearCallLog()
{
    m_callLog.clear();
    m_trashedPaths.clear();
    m_permanentlyDeleted.clear();
}

// -----------------------------------------------------------------------------
// 内部查询
// -----------------------------------------------------------------------------

bool FakeFileSystem::hasEntry(const QString &path) const
{
    return m_entries.contains(PathUtils::normalize(path, m_style));
}

FileInfo *FakeFileSystem::findEntry(const QString &path) const
{
    // m_entries 是 mutable，因此这里在 const 方法中也能拿到可写迭代器。
    const auto it = m_entries.find(path);
    return it == m_entries.end() ? nullptr : &it.value();
}

// -----------------------------------------------------------------------------
// FileSystem 接口
// -----------------------------------------------------------------------------

Qt::CaseSensitivity FakeFileSystem::caseSensitivity() const
{
    return m_caseSensitivity;
}

QChar FakeFileSystem::separator() const
{
    return m_style.separator;
}

QString FakeFileSystem::pathNormalize(const QString &path, FileSystemError *error) const
{
    if (intercept(Operation::PathNormalize, path, error))
        return QString();
    if (error)
        *error = FileSystemError::None;
    return PathUtils::normalize(path, m_style);
}

bool FakeFileSystem::isAbsolutePath(const QString &path) const
{
    return PathUtils::isAbsolute(path, m_style);
}

QString FakeFileSystem::toNativePath(const QString &path) const
{
    return PathUtils::toExtendedPath(PathUtils::normalize(path, m_style), m_style);
}

FileInfo FakeFileSystem::stat(const QString &path, FileSystemError *error) const
{
    if (intercept(Operation::Stat, path, error))
        return FileInfo();

    const QString normalized = PathUtils::normalize(path, m_style);

    // 大小写语义也要在假实现里生效：Windows 语义下 stat("/A/B.TXT") 应当
    // 命中 /a/b.txt。少了这一步，用假实现写的测试就无法覆盖大小写相关的
    // 判定逻辑（而那是 DIR-005 的核心）。
    const FileInfo *info = findEntry(normalized);
    if (info == nullptr && m_caseSensitivity == Qt::CaseInsensitive) {
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
            if (PathUtils::comparePaths(it.key(), normalized, m_style, Qt::CaseInsensitive)) {
                info = &it.value();
                break;
            }
        }
    }

    if (info == nullptr) {
        if (error)
            *error = FileSystemError::NotFound;
        return FileInfo();
    }

    if (error)
        *error = FileSystemError::None;
    return *info;
}

QString FakeFileSystem::linkTarget(const QString &path, FileSystemError *error) const
{
    if (intercept(Operation::LinkTarget, path, error))
        return QString();

    const QString normalized = PathUtils::normalize(path, m_style);
    const auto it = m_linkTargets.constFind(normalized);
    if (it == m_linkTargets.constEnd()) {
        // 不是链接时返回 NotSupported 而不是空字符串 + None：
        // 调用方需要能区分「这不是链接」与「是链接但读了空目标」。
        if (error)
            *error = FileSystemError::NotSupported;
        return QString();
    }

    if (error)
        *error = FileSystemError::None;
    return it.value();
}

bool FakeFileSystem::exists(const QString &path, FileSystemError *error) const
{
    if (intercept(Operation::Exists, path, error))
        return false;

    FileSystemError statError = FileSystemError::None;
    const FileInfo info = stat(path, &statError);
    if (error)
        *error = statError;
    return info.exists;
}

QVector<FileInfo> FakeFileSystem::enumerateDirectory(const QString &path, FileSystemError *error) const
{
    QVector<FileInfo> entries;
    if (intercept(Operation::Enumerate, path, error))
        return entries;

    const QString normalized = PathUtils::normalize(path, m_style);

    const FileInfo *directory = findEntry(normalized);
    if (directory == nullptr) {
        if (error)
            *error = FileSystemError::NotFound;
        return entries;
    }
    if (!directory->isDirectory) {
        // 对文件调用枚举是调用方的错误，必须明确报出来，
        // 而不是安静地返回空列表——否则「指向文件的会话」会被当成「空目录」。
        if (error)
            *error = FileSystemError::NotDirectory;
        return entries;
    }

    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (it.key() == normalized)
            continue; // 目录自身不是自己的子项
        const QString parent = PathUtils::parentPath(it.key(), m_style);
        if (PathUtils::comparePaths(parent, normalized, m_style, m_caseSensitivity))
            entries.append(it.value());
    }

    if (error)
        *error = FileSystemError::None;
    return entries;
}

bool FakeFileSystem::setTimes(const QString &path, const FileTime &lastModified,
                              const FileTime &lastAccessed, FileSystemError *error) const
{
    if (intercept(Operation::SetTimes, path, error))
        return false;

    const QString normalized = PathUtils::normalize(path, m_style);
    auto it = m_entries.find(normalized);
    if (it == m_entries.end()) {
        if (error)
            *error = FileSystemError::NotFound;
        return false;
    }

    // 与真实实现一致：只改传进来的那一项，未传的保持原值。
    if (lastModified.isValid())
        it->lastModified = lastModified;
    if (lastAccessed.isValid())
        it->lastAccessed = lastAccessed;

    if (error)
        *error = FileSystemError::None;
    return true;
}

bool FakeFileSystem::setAttributes(const QString &path, FileAttributes attributes,
                                   FileSystemError *error) const
{
    if (intercept(Operation::SetAttributes, path, error))
        return false;

    const QString normalized = PathUtils::normalize(path, m_style);
    auto it = m_entries.find(normalized);
    if (it == m_entries.end()) {
        if (error)
            *error = FileSystemError::NotFound;
        return false;
    }

    // 只改被点名的属性位，其余保留——与 POSIX/Windows 实现保持同一语义。
    // 顺序：先清掉这四个可控位，再按传入值设置。
    static const FileAttributes mutableAttributes = FileAttribute::ReadOnly
        | FileAttribute::Hidden | FileAttribute::System | FileAttribute::Archive;

    FileAttributes current = it->attributes;
    current &= ~mutableAttributes;
    current |= (attributes & mutableAttributes);
    it->attributes = current;

    if (error)
        *error = FileSystemError::None;
    return true;
}

bool FakeFileSystem::deleteToTrash(const QStringList &paths, FileSystemError *error) const
{
    // 逐个检查故障，语义与真实实现一致：任何一个条目失败都不算整体成功。
    for (const QString &path : paths) {
        if (intercept(Operation::DeleteToTrash, path, error))
            return false;
    }

    for (const QString &path : paths) {
        const QString normalized = PathUtils::normalize(path, m_style);
        if (!m_entries.contains(normalized)) {
            if (error)
                *error = FileSystemError::NotFound;
            return false;
        }
    }

    for (const QString &path : paths) {
        const QString normalized = PathUtils::normalize(path, m_style);
        // 记录到 trashed，并从内存树里移除——模拟「移入回收站」。
        // 关键点：**绝不写入 m_permanentlyDeleted**。这个列表永远是空的，
        // 一旦哪个实现往里写了东西，测试会立刻失败——那正是「静默永久删除」。
        m_trashedPaths.append(normalized);
        m_entries.remove(normalized);
        m_linkTargets.remove(normalized);
    }

    if (error)
        *error = FileSystemError::None;
    return true;
}

QString FakeFileSystem::platformName() const
{
    return m_platformName;
}

} // namespace Test
} // namespace Files
} // namespace LqCompare
