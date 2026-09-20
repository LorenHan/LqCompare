#include "logfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMap>

namespace LqCompare {
namespace Log {

namespace {

/// 本模块占用的设置键。
///
/// 写成函数而不是在调用点各写一遍字面量：这四个键同时被设置仓库（登记定义）、
/// 运行期（OptionsRuntime 读值）与设置页（按定义表自动生成控件）使用，
/// 三处各自手抄一遍的话，改名的代价是「有一处漏改」——而漏改那一处的
/// 现象是「设置项存在但不生效」，界面上完全看不出来。
const QString kRotationModeKey = QStringLiteral("logging.rotationMode");
const QString kRotationMaximumMegabytesKey = QStringLiteral("logging.rotationMaximumMegabytes");
const QString kRotationKeepFilesKey = QStringLiteral("logging.rotationKeepFiles");

constexpr qint64 kBytesPerMegabyte = 1024LL * 1024LL;
constexpr qint64 kMinimumBytes = 1LL * kBytesPerMegabyte;
constexpr qint64 kMaximumBytes = 1024LL * kBytesPerMegabyte;
constexpr int kMaximumKeepFiles = 100;

/// 人可读的体积。四舍五入到两位小数，与设置页上「单份日志体积上限」的单位一致。
QString formatByteSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 字节").arg(bytes);
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
        return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 2));
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 2));
    return QStringLiteral("%1 GB").arg(QString::number(mb / 1024.0, 'f', 2));
}

/// 保留份数那一句的共用说法（`rotationSummary()` 与 `rotationDecision()` 都要用）。
QString keepFilesPhrase(int keepFiles)
{
    if (keepFiles <= 0)
        return QStringLiteral("不保留历史（轮转时旧日志被删除）");
    return QStringLiteral("保留最近 %1 份历史").arg(keepFiles);
}

/// 收集 `baseName.N` 形式的历史文件，`N` 从 1 开始。
///
/// **刻意不用 `QDir` 的名字过滤器（glob）做粗筛。** 日志名是用户在设置里写的
/// 任意字符串，带 `[` / `*` / `?` 时 glob 会同时出两种错：
///   * 漏掉真历史——`a[1].log.*` 要求第二格匹配字符 `1`，而真历史 `a[1].log.1`
///     那一格是 `[`，于是「历史永不清理、`.1` 被静默覆盖」；
///   * 捞进无关文件——同一个模式会匹配 `a1.log.12345`，而轮转的最后一步是删除。
/// 两种错的代价都不小，因此这里退化成「列举目录 + 精确核对前缀 + 后缀必须是
/// 正整数」。代价是每个检查周期一次目录列举；写入路径已经把它节流到每秒最多
/// 一次，而日志目录本来就只有几个文件。
QMap<int, QString> existingHistory(const QFileInfo &info)
{
    QMap<int, QString> history;
    const QString baseName = info.fileName();
    const QDir directory = info.absoluteDir();
    const QString prefix = baseName + QLatin1Char('.');
    const QStringList names = directory.entryList(QDir::Files);
    for (const QString &name : names) {
        if (!name.startsWith(prefix)) continue;
        bool ok = false;
        const int index = name.mid(prefix.size()).toInt(&ok);
        if (ok && index > 0) history.insert(index, directory.absoluteFilePath(name));
    }
    return history;
}

} // namespace

QString rotationModeKey()
{
    return kRotationModeKey;
}

QString rotationMaximumMegabytesKey()
{
    return kRotationMaximumMegabytesKey;
}

QString rotationKeepFilesKey()
{
    return kRotationKeepFilesKey;
}

const char *rotationModeIdentifier(RotationMode mode)
{
    switch (mode) {
    case RotationMode::None:
        return "none";
    case RotationMode::Size:
        return "size";
    case RotationMode::Daily:
        return "daily";
    }
    return "none";
}

bool rotationModeFromName(const QString &name, RotationMode *out)
{
    const QString trimmed = name.trimmed().toLower();
    if (trimmed.isEmpty())
        return false;

    RotationMode parsed = RotationMode::None;
    if (trimmed == QLatin1String("none")) {
        parsed = RotationMode::None;
    } else if (trimmed == QLatin1String("size")) {
        parsed = RotationMode::Size;
    } else if (trimmed == QLatin1String("daily")) {
        parsed = RotationMode::Daily;
    } else {
        // 认不出来时不动 out：调用点能把「用户写错了」与「用户没写」分开处理。
        return false;
    }

    if (out != nullptr)
        *out = parsed;
    return true;
}

QString rotationModeLabel(RotationMode mode)
{
    switch (mode) {
    case RotationMode::None:
        return QStringLiteral("不轮转");
    case RotationMode::Size:
        return QStringLiteral("按大小轮转");
    case RotationMode::Daily:
        return QStringLiteral("按天轮转");
    }
    return QStringLiteral("不轮转");
}

QStringList rotationModeChoices()
{
    // 顺序即下拉框顺序，也是持久化的候选值顺序；`rotationModeIdentifier()`
    // 是唯一的事实来源，这里不重复写字面量。
    QStringList choices;
    for (RotationMode mode : {RotationMode::None, RotationMode::Size, RotationMode::Daily})
        choices << QString::fromLatin1(rotationModeIdentifier(mode));
    return choices;
}

qint64 minimumRotationMaximumMegabytes()
{
    return kMinimumBytes / kBytesPerMegabyte;
}

qint64 maximumRotationMaximumMegabytes()
{
    return kMaximumBytes / kBytesPerMegabyte;
}

int maximumRotationKeepFiles()
{
    return kMaximumKeepFiles;
}

RotationPolicy RotationPolicy::fromValues(const QVariantMap &values)
{
    RotationPolicy policy;
    RotationMode mode = RotationMode::None;
    if (rotationModeFromName(values.value(kRotationModeKey).toString(), &mode))
        policy.mode = mode;

    // 未使用的字段也照读：用户先按大小配好、再切到「按天」，切回来时
    // 上界不该悄悄回到 5 MB。
    bool ok = false;
    const qint64 megabytes = values.value(kRotationMaximumMegabytesKey).toLongLong(&ok);
    if (ok) policy.maximumBytes = megabytes * kBytesPerMegabyte;

    bool keepOk = false;
    const int keepFiles = values.value(kRotationKeepFilesKey).toInt(&keepOk);
    if (keepOk) policy.keepFiles = keepFiles;
    return policy;
}

QVariantMap RotationPolicy::toValues() const
{
    QVariantMap values;
    values.insert(kRotationModeKey, QString::fromLatin1(rotationModeIdentifier(mode)));
    values.insert(kRotationMaximumMegabytesKey, maximumMegabytes());
    values.insert(kRotationKeepFilesKey, keepFiles);
    return values;
}

QString RotationPolicy::validate() const
{
    // 只校验当前模式用得到的字段。理由是产品性的：界面上跟模式无关的那个
    // 数字框仍然是可编辑的，但用户此刻的意图与它无关——为了「以后可能用到」
    // 拦住一次保存，只会让人以为设置页坏了。
    if (keepFiles < 0 || keepFiles > kMaximumKeepFiles)
        return QStringLiteral("保留的历史日志份数需在 0–%1 之间。").arg(kMaximumKeepFiles);
    if (mode != RotationMode::Size)
        return QString();

    if (maximumBytes < kMinimumBytes)
        return QStringLiteral("单份日志体积上限至少 1 MB。");
    if (maximumBytes > kMaximumBytes)
        return QStringLiteral("单份日志体积上限最多 1024 MB。");
    return QString();
}

QString rotationTriggerLabel(RotationTrigger trigger)
{
    switch (trigger) {
    case RotationTrigger::None:
        return QStringLiteral("无需轮转");
    case RotationTrigger::SizeExceeded:
        return QStringLiteral("达到体积上限");
    case RotationTrigger::NewDay:
        return QStringLiteral("已跨天");
    }
    return QStringLiteral("无需轮转");
}

RotationDecision rotationDecision(const RotationPolicy &policy, qint64 currentSize,
                                  const QDate &fileDate, const QDateTime &now)
{
    RotationDecision decision;
    decision.currentSize = currentSize;

    if (policy.mode == RotationMode::None) {
        decision.reason = QStringLiteral("轮转已关闭，日志会一直追加到同一个文件。");
        return decision;
    }

    // 空文件（含文件不存在）一律不动。按天轮转时这条尤其重要：一个从来没写过
    // 日志的路径，每天都会产出一个 0 字节的 `.1`，用户看到一串空文件只会
    // 认为程序坏了，而真正的原因是他把日志级别配得太高。
    if (currentSize <= 0) {
        decision.reason = QStringLiteral("日志文件为空，无需轮转。");
        return decision;
    }

    if (policy.mode == RotationMode::Size) {
        if (currentSize >= policy.maximumBytes) {
            decision.rotate = true;
            decision.trigger = RotationTrigger::SizeExceeded;
            decision.reason = QStringLiteral("日志已达 %1，达到上限 %2，需要轮转。")
                                      .arg(formatByteSize(currentSize),
                                           formatByteSize(policy.maximumBytes));
        } else {
            decision.reason = QStringLiteral("日志当前 %1，未达到上限 %2，无需轮转。")
                                      .arg(formatByteSize(currentSize),
                                           formatByteSize(policy.maximumBytes));
        }
        return decision;
    }

    // 按天轮转。日期取自文件的**最后写入时间**，而不是运行期是否跨过 0 点：
    // 程序关一整天再启动时，同一条策略也要生效。
    if (!fileDate.isValid()) {
        decision.reason = QStringLiteral("无法确定日志的最后写入日期，本次不轮转。");
        return decision;
    }
    const QDate today = now.date();
    if (fileDate < today) {
        decision.rotate = true;
        decision.trigger = RotationTrigger::NewDay;
        decision.reason = QStringLiteral("日志最后一次写入是 %1，今天已是 %2，需要轮转。")
                                  .arg(fileDate.toString(QStringLiteral("yyyy-MM-dd")),
                                       today.toString(QStringLiteral("yyyy-MM-dd")));
    } else {
        decision.reason = QStringLiteral("日志最后一次写入是 %1，与今天（%2）是同一天，无需轮转。")
                                  .arg(fileDate.toString(QStringLiteral("yyyy-MM-dd")),
                                       today.toString(QStringLiteral("yyyy-MM-dd")));
    }
    return decision;
}

QString rotatedLogPath(const QString &logFilePath, int index)
{
    if (index <= 0 || logFilePath.isEmpty())
        return QString();
    return QStringLiteral("%1.%2").arg(logFilePath).arg(index);
}

QString rotationSummary(const RotationPolicy &policy)
{
    switch (policy.mode) {
    case RotationMode::None:
        return QStringLiteral("轮转已关闭：日志会一直追加到同一个文件。");
    case RotationMode::Size:
        return QStringLiteral("按大小轮转：单份达到 %1 时换成新文件，%2。")
                .arg(formatByteSize(policy.maximumBytes), keepFilesPhrase(policy.keepFiles));
    case RotationMode::Daily:
        return QStringLiteral("按天轮转：跨天后第一次写入时换成新文件，%1。")
                .arg(keepFilesPhrase(policy.keepFiles));
    }
    return QStringLiteral("轮转已关闭：日志会一直追加到同一个文件。");
}

QStringList logHistoryFiles(const QString &logFilePath)
{
    if (logFilePath.isEmpty())
        return QStringList();
    // `QMap` 的键是升序，正好就是「从最新到最旧」的历史顺序。
    const QMap<int, QString> history = existingHistory(QFileInfo(logFilePath));
    QStringList paths;
    for (auto it = history.constBegin(); it != history.constEnd(); ++it)
        paths << it.value();
    return paths;
}

bool applyLogRotation(const QString &logFilePath, const RotationPolicy &policy,
                      const QDateTime &now, RotationDecision *decision, QString *error)
{
    if (decision != nullptr)
        *decision = RotationDecision();
    if (error != nullptr)
        error->clear();

    if (logFilePath.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("未启用文件日志，无需轮转。");
        return false;
    }

    const QFileInfo info(logFilePath);
    const bool exists = info.exists() && info.isFile();
    const qint64 size = exists ? info.size() : 0;
    const QDate fileDate = exists ? info.lastModified().date() : QDate();

    const RotationDecision decided = rotationDecision(policy, size, fileDate, now);
    if (decision != nullptr)
        *decision = decided;
    if (!decided.rotate)
        return true;

    const int keepFiles = policy.keepFiles < 0 ? 0 : policy.keepFiles;

    // 保留 0 份等价于「轮转后立刻丢弃旧日志」。这里直接删当前文件，而不是
    // 先改名成 `.1` 再删：少一次无谓的磁盘往返，也不会在这中间留下一个
    // 用户从没要求过的历史文件（它恰好会被别的进程看到）。
    //
    // 已有的历史也要一并删掉——「保留 0 份」是用户的当前意图，而轮转正是
    // 执行这个意图的那一刻。只删当前文件会让上一套策略留下的 `.1` `.2`
    // 永远留在那里，用户把份数调成 0 之后发现磁盘还在涨。
    if (keepFiles == 0) {
        QMap<int, QString> stale = existingHistory(info);
        const QList<int> staleIndexes = stale.keys();
        for (int position = staleIndexes.size() - 1; position >= 0; --position) {
            const QString path = stale.value(staleIndexes.at(position));
            QFile old(path);
            if (!old.remove()) {
                if (error != nullptr)
                    *error = QStringLiteral("无法删除历史日志 %1：%2").arg(path, old.errorString());
                return false;
            }
        }
        if (QFile::remove(logFilePath))
            return true;
        QFile probe(logFilePath);
        if (error != nullptr)
            *error = QStringLiteral("无法删除已达上限的日志 %1：%2")
                             .arg(logFilePath, probe.errorString());
        return false;
    }

    QMap<int, QString> history = existingHistory(info);

    // 1) 先删掉超出保留份数的旧文件。`keys()` 是升序，倒着遍历即从最旧的开始，
    //    这样「删到一半失败」时留下的是连续的一段，而不是中间挖空的一段。
    const QList<int> existingIndexes = history.keys();
    for (int position = existingIndexes.size() - 1; position >= 0; --position) {
        const int index = existingIndexes.at(position);
        if (index <= keepFiles) continue;
        const QString path = history.value(index);
        QFile old(path);
        if (!old.remove()) {
            if (error != nullptr)
                *error = QStringLiteral("无法删除超出保留份数的日志 %1：%2")
                                 .arg(path, old.errorString());
            return false;
        }
        history.remove(index);
    }

    // 2) 序号整体后移一位，给当前文件腾出 `.1`。
    for (int index = keepFiles; index >= 1; --index) {
        auto found = history.constFind(index);
        if (found == history.constEnd()) continue;
        const QString source = found.value();
        const QString target = rotatedLogPath(logFilePath, index + 1);
        // 先删目标：`QFile::rename()` 在目标已存在时的行为随平台而异
        // （POSIX 上静默覆盖、Windows 上失败）。同一份代码在两种平台走两条路，
        // 而这里「覆盖」是正确行为——干脆显式做掉，让两个平台一致。
        if (QFile::exists(target) && !QFile::remove(target)) {
            QFile probe(target);
            if (error != nullptr)
                *error = QStringLiteral("无法腾出历史日志位置 %1：%2")
                                 .arg(target, probe.errorString());
            return false;
        }
        QFile file(source);
        if (!file.rename(target)) {
            if (error != nullptr)
                *error = QStringLiteral("无法重命名日志 %1 → %2：%3")
                                 .arg(source, target, file.errorString());
            return false;
        }
    }

    // 3) 当前文件变成 `.1`。此后下一次追加会自动创建新的当前文件。
    const QString firstTarget = rotatedLogPath(logFilePath, 1);
    if (QFile::exists(firstTarget) && !QFile::remove(firstTarget)) {
        QFile probe(firstTarget);
        if (error != nullptr)
            *error = QStringLiteral("无法腾出历史日志位置 %1：%2")
                             .arg(firstTarget, probe.errorString());
        return false;
    }
    QFile current(logFilePath);
    if (!current.rename(firstTarget)) {
        if (error != nullptr)
            *error = QStringLiteral("无法轮转日志 %1 → %2：%3")
                             .arg(logFilePath, firstTarget, current.errorString());
        return false;
    }
    return true;
}

bool clearLogFile(const QString &logFilePath, QString *error)
{
    if (error != nullptr)
        error->clear();

    if (logFilePath.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("未启用文件日志，无需清空。");
        return false;
    }

    QFile file(logFilePath);
    if (!file.exists())
        return true; // 「没有东西需要清空」不是失败。
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr)
            *error = QStringLiteral("无法清空日志 %1：%2").arg(logFilePath, file.errorString());
        return false;
    }
    file.close();
    return true;
}

} // namespace Log
} // namespace LqCompare
