#include "batch.h"

#include <QHash>
#include <QSet>

namespace LqCompare {
namespace Files {

// -----------------------------------------------------------------------------
// 策略
// -----------------------------------------------------------------------------

const char *batchFailurePolicyIdentifier(BatchFailurePolicy policy)
{
    // 稳定的英文标识，理由同 trashAvailabilityIdentifier()：这个字符串会出现在
    // 日志与测试断言里，跟着界面语言变就没法跨版本比对了。
    switch (policy) {
    case BatchFailurePolicy::SkipAndContinue:
        return "skip-and-continue";
    case BatchFailurePolicy::StopOnFirstError:
        return "stop-on-first-error";
    }
    return "unknown-policy";
}

// -----------------------------------------------------------------------------
// 失败分组
// -----------------------------------------------------------------------------

QStringList FailureGroup::paths() const
{
    QStringList result;
    result.reserve(items.size());
    for (const BatchRecord &item : items)
        result.append(item.path);
    return result;
}

// -----------------------------------------------------------------------------
// 报告
// -----------------------------------------------------------------------------

int BatchReport::succeededCount() const
{
    int count = 0;
    for (const BatchRecord &record : records) {
        if (record.succeeded())
            ++count;
    }
    return count;
}

int BatchReport::failedCount() const
{
    // 用 totalCount() - succeededCount() 而不是自己再数一遍：
    // 两条独立的计数逻辑迟早在「重试后」这类状态上出现不一致，
    // 而那份不一致会表现为「界面说失败 2 个，清单里有 3 条」。
    return totalCount() - succeededCount();
}

bool BatchReport::allSucceeded() const
{
    return failedCount() == 0;
}

bool BatchReport::anySucceeded() const
{
    for (const BatchRecord &record : records) {
        if (record.succeeded())
            return true;
    }
    return false;
}

int BatchReport::totalAttempts() const
{
    int total = 0;
    for (const BatchRecord &record : records)
        total += record.attempts;
    return total;
}

QStringList BatchReport::succeededPaths() const
{
    QStringList result;
    for (const BatchRecord &record : records) {
        if (record.succeeded())
            result.append(record.path);
    }
    return result;
}

QStringList BatchReport::failedPaths() const
{
    QStringList result;
    for (const BatchRecord &record : records) {
        if (!record.succeeded())
            result.append(record.path);
    }
    return result;
}

ErrorCode BatchReport::firstErrorCode() const
{
    for (const BatchRecord &record : records) {
        if (!record.succeeded())
            return record.error;
    }
    // 全成功时返回空的 ErrorCode，而不是「最后一个条目」的错误——
    // 后者会让调用方在成功路径上读到一个过期的失败原因。
    return ErrorCode();
}

FileSystemError BatchReport::firstError() const
{
    return firstErrorCode().category;
}

QVector<FailureGroup> BatchReport::failureGroups() const
{
    QVector<FailureGroup> groups;

    for (const BatchRecord &record : records) {
        if (record.succeeded())
            continue;

        // 按「分类第一次出现」的顺序建组（顺序线性查找即可：分类一共十来个，
        // 组数在实际场景里通常只有两三个，用哈希表反而要额外维护一份顺序）。
        int index = -1;
        for (int i = 0; i < groups.size(); ++i) {
            if (groups.at(i).category == record.error.category) {
                index = i;
                break;
            }
        }

        if (index < 0) {
            FailureGroup group;
            group.category = record.error.category;
            // 建议与「能不能重试」都在建组时算一次，而不是让每个调用点各自判断——
            // 否则界面、日志、批处理脚本会给出三套不完全一样的建议。
            group.advice = errorAdvice(group.category);
            group.retryable = isRetryable(group.category);
            groups.append(group);
            index = groups.size() - 1;
        }

        groups[index].items.append(record);
    }

    return groups;
}

QStringList BatchReport::retryablePaths() const
{
    QStringList result;
    for (const BatchRecord &record : records) {
        if (!record.succeeded() && isRetryable(record.error.category))
            result.append(record.path);
    }
    return result;
}

QString BatchReport::failureSummary() const
{
    const QVector<FailureGroup> groups = failureGroups();
    if (groups.isEmpty())
        return QString(); // 没有失败就没有清单，不输出「失败 0 项」这种噪点

    const QString title = operationName.isEmpty() ? QStringLiteral("批量操作") : operationName;

    QStringList lines;
    lines.append(QStringLiteral("「%1」共 %2 个条目：完成 %3 个，失败 %4 个。")
                     .arg(title)
                     .arg(totalCount())
                     .arg(succeededCount())
                     .arg(failedCount()));

    if (stoppedEarly) {
        // 提前结束时必须说清楚，否则用户会把「失败 4 个」理解成「只有 4 个有问题」，
        // 而真相是「根本还没轮到剩下的那些」。
        lines.append(QStringLiteral("（遇到错误后已按设置停止，其余条目未执行。）"));
    }

    for (const FailureGroup &group : groups) {
        lines.append(QString());

        QString heading = QStringLiteral("· %1 × %2")
                              .arg(errorMessage(group.category))
                              .arg(group.count());
        if (group.retryable)
            heading += QStringLiteral("　— 可以重试");
        lines.append(heading);

        if (!group.advice.isEmpty())
            lines.append(QStringLiteral("  建议：%1").arg(group.advice));

        for (const BatchRecord &item : group.items) {
            QString line = QStringLiteral("  - %1").arg(item.path);

            // 第 5 条：每一条都带上原始系统错误码。放在条目行而不是组标题上，
            // 是因为同一组里不同条目可能有不同的原始码（写无权限 vs 读无权限），
            // 那正是排查时最需要区分的地方。
            const QString detail = errorDetail(item.error);
            if (!detail.isEmpty())
                line += QStringLiteral("（%1）").arg(detail);

            if (item.attempts > 1)
                line += QStringLiteral("　已尝试 %1 次").arg(item.attempts);

            lines.append(line);
        }
    }

    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 执行器
// -----------------------------------------------------------------------------

BatchOperation::~BatchOperation() = default;

BatchReport BatchOperation::run(const QStringList &paths)
{
    // 去重（保留首次出现的顺序）。
    //
    // 同一个路径在一次批量里做两遍没有意义——第二遍看到的是第一遍的结果。
    // 而去重不只是省一次调用：留着重复项会让失败清单里出现两行同样的路径，
    // 用户重试时会以为程序写错了，或者以为自己看漏了。
    QStringList unique;
    QSet<QString> seen;
    for (const QString &path : paths) {
        if (seen.contains(path))
            continue;
        seen.insert(path);
        unique.append(path);
    }

    m_report = execute(unique);
    return m_report;
}

BatchReport BatchOperation::retryFailed()
{
    QStringList failed;
    for (const BatchRecord &record : m_report.records) {
        if (!record.succeeded())
            failed.append(record.path);
    }

    if (failed.isEmpty()) {
        // 没有失败项：一次 performOne 都不调用。
        // 这既省掉了无谓的磁盘操作，也避免了一批「本来已经成功」的文件
        // 被再做一遍（对批量复制这类操作那可能是覆盖）。
        return m_report;
    }

    const BatchReport outcome = execute(failed);

    // 把重试结果**合并**回整批报告，而不是替换掉它。
    //
    // 这一步是「保持已完成的进度」在重试路径上的具体含义：成功过的条目必须
    // 留在报告里，否则界面上的计数器会从「完成 8/10」退回「完成 2/2」，
    // 用户会以为前面 8 个白做了（第 4 条）。
    QHash<QString, int> indexByPath;
    indexByPath.reserve(m_report.records.size());
    for (int i = 0; i < m_report.records.size(); ++i)
        indexByPath.insert(m_report.records.at(i).path, i);

    for (const BatchRecord &retryRecord : outcome.records) {
        const int index = indexByPath.value(retryRecord.path, -1);
        if (index < 0) {
            // 理论上到不了这里：重试的路径都是从 m_report 里挑出来的。
            // 仍然追加而不是丢弃——宁可多一条记录，也不能让一个做过的条目
            // 从报告里凭空消失。
            m_report.records.append(retryRecord);
            indexByPath.insert(retryRecord.path, m_report.records.size() - 1);
            continue;
        }

        BatchRecord &existing = m_report.records[index];
        existing.error = retryRecord.error;
        // 累加而不是覆盖 attempts：用户要看到的是「一共试了几次」，
        // 覆盖会让「试了三次还是失败」显示成「试了一次」。
        existing.attempts += retryRecord.attempts;
    }

    m_report.retried = true;
    if (outcome.stoppedEarly)
        m_report.stoppedEarly = true;

    return m_report;
}

BatchReport BatchOperation::execute(const QStringList &paths)
{
    BatchReport report;
    report.operationName = operationName();
    report.policy = m_policy;

    const int total = paths.size();

    for (int i = 0; i < total; ++i) {
        const QString &path = paths.at(i);

        BatchRecord record;
        record.path = path;
        record.error = performOne(path);
        record.attempts = 1;
        report.records.append(record);

        if (m_progress)
            m_progress(record, i + 1, total);

        // 第 4 条：默认策略下**绝不**因为一个条目失败就退出循环。
        // 只有调用方显式要求 StopOnFirstError 时才提前结束，
        // 而且已经完成的条目全部留在报告里。
        if (!record.succeeded() && m_policy == BatchFailurePolicy::StopOnFirstError) {
            report.stoppedEarly = true;
            break;
        }
    }

    return report;
}

// -----------------------------------------------------------------------------
// 批量设置属性（参考实现）
// -----------------------------------------------------------------------------

SetAttributesBatch::SetAttributesBatch(const FileSystem &fileSystem, FileAttributes attributes)
    : m_fileSystem(fileSystem), m_attributes(attributes)
{
}

QString SetAttributesBatch::operationName() const
{
    if (m_attributes.testFlag(FileAttribute::ReadOnly))
        return QStringLiteral("设为只读");
    return QStringLiteral("解除只读");
}

ErrorCode SetAttributesBatch::performOne(const QString &path)
{
    ErrorCode error;
    if (m_fileSystem.setAttributes(path, m_attributes, &error))
        return ErrorCode();

    // 契约要求失败时一定要写出原因。万一实现没写（那是实现的 bug），
    // 这里绝不能把一个 ok() 为真的 ErrorCode 返回给执行器——
    // 那会让这个条目从失败清单里凭空消失，用户以为它成功了。
    if (error.ok())
        error.category = FileSystemError::Unknown;
    return error;
}

} // namespace Files
} // namespace LqCompare
