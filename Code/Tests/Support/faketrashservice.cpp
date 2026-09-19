#include "faketrashservice.h"

#include <QFileInfo>

namespace LqCompare {
namespace Files {
namespace Test {

namespace {

/// 从路径向上依次取值，返回第一个被注入过的答案。
///
/// 为什么按「祖先」而不是精确匹配：真实世界里回收站可用性是**按卷**的，
/// 一整块盘要么有回收站要么没有，不会出现 /media/usb/a 可以、/media/usb/b 不行。
/// 精确匹配会逼着每个测试把路径逐个列出来，写的人一定会漏，
/// 而漏掉的那条会静默落到默认答案上（可用）——于是「不可用时不删」的测试
/// 反而在测试「可用时删除」，看起来还是绿的。
bool lookupByAncestor(const QHash<QString, TrashAvailability> &table,
                      const QString &path,
                      TrashAvailability *result)
{
    QString current = path;
    while (true) {
        const auto it = table.constFind(current);
        if (it != table.constEnd()) {
            *result = it.value();
            return true;
        }

        const int separator = current.lastIndexOf(QLatin1Char('/'));
        if (separator <= 0)
            break;
        current = current.left(separator);
    }

    // 还有一条容易漏的情况：路径本身没有前导分隔符（相对路径）时，
    // 上面的循环一次都不会进。此时直接用整个路径查一次。
    const auto it = table.constFind(path);
    if (it != table.constEnd()) {
        *result = it.value();
        return true;
    }

    return false;
}

} // namespace

FakeTrashService::FakeTrashService() = default;

FakeTrashService::~FakeTrashService() = default;

void FakeTrashService::setDefaultAvailability(TrashAvailability availability)
{
    m_defaultAvailability = availability;
}

void FakeTrashService::setAvailabilityFor(const QString &path, TrashAvailability availability)
{
    m_availabilityByPath.insert(normalize(path), availability);
}

void FakeTrashService::failTrashPath(const QString &path, FileSystemError error)
{
    m_trashFailures.insert(normalize(path), error);
}

void FakeTrashService::failUndo(FileSystemError error)
{
    m_undoFailure = error;
}

QString FakeTrashService::normalize(const QString &path) const
{
    // 只去尾部分隔符。刻意不做完整规范化（不解析 "." 与 ".."）：
    // 测试替身一旦开始「聪明地猜测用户意图」，注入的键和查询的键就可能
    // 悄悄对不上，而失效方式是静默的——注入没生效，测试却仍然是绿的。
    QString result = path;
    while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

TrashAvailability FakeTrashService::availabilityFor(const QString &path) const
{
    m_callLog.append(QStringLiteral("availability:") + path);

    TrashAvailability answer = TrashAvailability::Available;
    if (lookupByAncestor(m_availabilityByPath, normalize(path), &answer))
        return answer;

    return m_defaultAvailability;
}

QStringList FakeTrashService::trashedPaths() const
{
    QStringList result;
    for (const QString &original : m_trashedOriginals)
        result.append(original);
    return result;
}

QStringList FakeTrashService::trashContents() const
{
    // QHash 的遍历顺序不稳定，回收站内容按路径排序后再返回，
    // 否则同一份记录的两次断言可能给出不同顺序。
    QStringList result = m_trashContents.keys();
    result.sort();
    return result;
}

int FakeTrashService::callCount(Operation operation) const
{
    const QString prefix = QString::fromLatin1(operationIdentifier(operation)) + QLatin1Char(':');
    int count = 0;
    for (const QString &entry : m_callLog) {
        if (entry.startsWith(prefix))
            ++count;
    }
    return count;
}

QStringList FakeTrashService::callLog() const
{
    return m_callLog;
}

void FakeTrashService::clearCallLog()
{
    m_callLog.clear();
}

const char *FakeTrashService::operationIdentifier(Operation operation)
{
    switch (operation) {
    case Operation::Availability:
        return "availability";
    case Operation::TrashPaths:
        return "trashPaths";
    case Operation::UndoLastDelete:
        return "undoLastDelete";
    }
    return "unknown";
}

TrashReport FakeTrashService::trashPaths(const QStringList &paths) const
{
    TrashReport report;

    for (const QString &path : paths) {
        const QString normalized = normalize(path);
        m_callLog.append(QStringLiteral("trashPaths:") + normalized);

        TrashRecord record;
        record.originalPath = path;

        const auto failure = m_trashFailures.constFind(normalized);
        if (failure != m_trashFailures.constEnd()) {
            record.error = failure.value();
            report.records.append(record);
            continue;
        }

        // 模拟真实实现的「同名自动改名」：先在废纸篓里放一份，
        // 再放同名时改成 "<name> 2.ext"。
        //
        // 这一条必须模拟，否则测试会用「按原名拼回收站路径」这种错误做法通过，
        // 而那个做法在真机上一次重名就会让撤销失效——而且失效得很安静：
        // 撤销会报「找不到文件」，用户以为是自己手工清空过回收站。
        const QString fileName = QFileInfo(normalized).fileName();
        const int occurrence = m_nameCollisions.value(fileName, 0);
        m_nameCollisions.insert(fileName, occurrence + 1);

        QString trashedName = fileName;
        if (occurrence > 0) {
            const int dot = fileName.lastIndexOf(QLatin1Char('.'));
            if (dot > 0) {
                trashedName = fileName.left(dot) + QStringLiteral(" ")
                        + QString::number(occurrence + 1) + fileName.mid(dot);
            } else {
                trashedName = fileName + QLatin1Char(' ') + QString::number(occurrence + 1);
            }
        }

        record.error = FileSystemError::None;
        record.trashedPath = QStringLiteral("/fake/.Trash/") + trashedName;

        m_trashContents.insert(record.trashedPath, normalized);
        m_trashedOriginals.append(normalized);

        // 注意这里**没有**任何地方记录「永久删除」。m_permanentlyDeleted 恒空，
        // 一旦哪个实现需要往里写东西，就说明它绕过了回收站。
        report.records.append(record);
    }

    return report;
}

bool FakeTrashService::undoLastDelete(FileSystemError *error) const
{
    m_callLog.append(QStringLiteral("undoLastDelete:"));

    if (m_undoFailure != FileSystemError::None) {
        if (error)
            *error = m_undoFailure;
        return false;
    }

    const TrashReport report = lastDelete();
    if (report.records.isEmpty()) {
        if (error)
            *error = FileSystemError::NotFound;
        return false;
    }

    for (const TrashRecord &record : report.records) {
        if (!record.succeeded() || record.trashedPath.isEmpty())
            continue;

        // 用 trashedPath 而不是自己拼路径——这正是要验证的契约。
        // 若实现方按原名拼路径，这里会因为键对不上而取不到，
        // 下面的断言（回收站内容为空）就会失败。
        m_trashContents.remove(record.trashedPath);
    }

    // 与真实实现保持一致：还原成功后清空撤销点。
    m_lastDelete = TrashReport();

    if (error)
        *error = FileSystemError::None;
    return true;
}

} // namespace Test
} // namespace Files
} // namespace LqCompare
