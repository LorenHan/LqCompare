#include "fileopsoptions.h"

#include <QSet>

namespace LqCompare {
namespace Files {
namespace {

constexpr qint64 BytesPerMegabyte = 1024LL * 1024LL;

/// 表里三处都要用的「取键」动作。写成函数而不是宏，
/// 是为了让拼错的短名变成空串（`fromValues` 会因此回退到默认值，
/// 而 `validateFileOperationKeyTable()` 的用例会在开发期就把它拦下来）。
QString keyFor(const QString &identifier)
{
    return fileOperationKey(identifier);
}

QString describeMegabytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("关闭");
    return QStringLiteral("%1 MB").arg(bytes / BytesPerMegabyte);
}

} // namespace

// ---------------------------------------------------------------------------
// 标识符
// ---------------------------------------------------------------------------

QString deleteModeIdentifier(DeleteMode mode)
{
    return mode == DeleteMode::Permanent ? QStringLiteral("permanent") : QStringLiteral("trash");
}

bool deleteModeFromIdentifier(const QString &identifier, DeleteMode *mode)
{
    if (identifier == QStringLiteral("trash")) {
        if (mode) *mode = DeleteMode::Trash;
        return true;
    }
    if (identifier == QStringLiteral("permanent")) {
        if (mode) *mode = DeleteMode::Permanent;
        return true;
    }
    return false;
}

QString overwritePolicyIdentifier(OverwritePolicy policy)
{
    switch (policy) {
    case OverwritePolicy::Overwrite: return QStringLiteral("overwrite");
    case OverwritePolicy::Skip:      return QStringLiteral("skip");
    case OverwritePolicy::Ask:       break;
    }
    return QStringLiteral("ask");
}

bool overwritePolicyFromIdentifier(const QString &identifier, OverwritePolicy *policy)
{
    if (identifier == QStringLiteral("ask")) {
        if (policy) *policy = OverwritePolicy::Ask;
        return true;
    }
    if (identifier == QStringLiteral("overwrite")) {
        if (policy) *policy = OverwritePolicy::Overwrite;
        return true;
    }
    if (identifier == QStringLiteral("skip")) {
        if (policy) *policy = OverwritePolicy::Skip;
        return true;
    }
    return false;
}

QString verifyModeIdentifier(VerifyMode mode)
{
    switch (mode) {
    case VerifyMode::Size: return QStringLiteral("size");
    case VerifyMode::Crc:  return QStringLiteral("crc");
    case VerifyMode::None: break;
    }
    return QStringLiteral("none");
}

bool verifyModeFromIdentifier(const QString &identifier, VerifyMode *mode)
{
    if (identifier == QStringLiteral("none")) {
        if (mode) *mode = VerifyMode::None;
        return true;
    }
    if (identifier == QStringLiteral("size")) {
        if (mode) *mode = VerifyMode::Size;
        return true;
    }
    if (identifier == QStringLiteral("crc")) {
        if (mode) *mode = VerifyMode::Crc;
        return true;
    }
    return false;
}

QString overwriteSituationIdentifier(OverwriteSituation situation)
{
    switch (situation) {
    case OverwriteSituation::TargetOlder:    return QStringLiteral("target-older");
    case OverwriteSituation::TargetSameTime: return QStringLiteral("target-same-time");
    case OverwriteSituation::TargetNewer:    return QStringLiteral("target-newer");
    case OverwriteSituation::TargetMissing:  break;
    }
    return QStringLiteral("target-missing");
}

// ---------------------------------------------------------------------------
// 中文标签
// ---------------------------------------------------------------------------

QString deleteModeLabel(DeleteMode mode)
{
    return mode == DeleteMode::Permanent ? QStringLiteral("永久删除") : QStringLiteral("移入回收站");
}

QString overwritePolicyLabel(OverwritePolicy policy)
{
    switch (policy) {
    case OverwritePolicy::Overwrite: return QStringLiteral("直接覆盖");
    case OverwritePolicy::Skip:      return QStringLiteral("跳过已存在的目标");
    case OverwritePolicy::Ask:       break;
    }
    return QStringLiteral("逐个询问");
}

QString verifyModeLabel(VerifyMode mode)
{
    switch (mode) {
    case VerifyMode::Size: return QStringLiteral("比对大小");
    case VerifyMode::Crc:  return QStringLiteral("比对 CRC");
    case VerifyMode::None: break;
    }
    return QStringLiteral("不校验");
}

QString metadataItemLabel(const QString &itemIdentifier)
{
    if (itemIdentifier == QStringLiteral("timestamps")) return QStringLiteral("修改时间");
    if (itemIdentifier == QStringLiteral("attributes")) return QStringLiteral("属性位");
    if (itemIdentifier == QStringLiteral("permissions")) return QStringLiteral("权限");
    // 认不出就返回空串：编一个假名字会让用户拿一个不存在的符号去搜。
    return QString();
}

QStringList MetadataPreservation::enabledIdentifiers() const
{
    QStringList result;
    if (timestamps) result.append(QStringLiteral("timestamps"));
    if (attributes) result.append(QStringLiteral("attributes"));
    if (permissions) result.append(QStringLiteral("permissions"));
    return result;
}

// ---------------------------------------------------------------------------
// 阈值单位
//
// 设置里存的是「兆字节」的整数：`fileops.largeFileConfirmMegabytes`。
// 为什么不直接存字节——设置文件是给人看的（`options.json` 是可读 JSON，
// 用户可以自己改），`104857600` 与 `100` 相比，后者一眼能对上界面上的数字，
// 而前者要拿计算器。代价是「设置里的值」与「策略里的值」有两个单位，
// 所以这一对换算函数是唯一的互通口，并且有往返用例钉住。
//
// 0 表示关闭确认。**不要**把 0 当成「0 字节以上都要确认」——
// 那样用户永远关不掉这个确认，只能把阈值设成一个天文数字。
// ---------------------------------------------------------------------------

qint64 largeFileConfirmMegabytesToBytes(int megabytes)
{
    if (megabytes <= 0)
        return 0;
    return qint64(megabytes) * BytesPerMegabyte;
}

int largeFileConfirmBytesToMegabytes(qint64 bytes)
{
    if (bytes <= 0)
        return 0;
    return int(bytes / BytesPerMegabyte);
}

// ---------------------------------------------------------------------------
// 策略
// ---------------------------------------------------------------------------

FileOperationPolicy FileOperationPolicy::fromValues(const QVariantMap &values)
{
    FileOperationPolicy policy;

    const QString deleteKey = keyFor(QStringLiteral("deleteMode"));
    const QVariant deleteValue = values.value(deleteKey);
    DeleteMode deleteMode = policy.deleteMode;
    if (!deleteKey.isEmpty() && deleteModeFromIdentifier(deleteValue.toString(), &deleteMode)) {
        policy.deleteMode = deleteMode;
    } else if (!deleteKey.isEmpty() && values.contains(deleteKey)) {
        policy.fallbacks.append(QStringLiteral("%1=%2 不是受支持的删除方式，已按「%3」处理。")
                                        .arg(deleteKey, deleteValue.toString(),
                                             deleteModeLabel(policy.deleteMode)));
    }

    const QString overwriteKey = keyFor(QStringLiteral("overwritePolicy"));
    const QVariant overwriteValue = values.value(overwriteKey);
    OverwritePolicy overwritePolicy = policy.overwritePolicy;
    if (!overwriteKey.isEmpty()
        && overwritePolicyFromIdentifier(overwriteValue.toString(), &overwritePolicy)) {
        policy.overwritePolicy = overwritePolicy;
    } else if (!overwriteKey.isEmpty() && values.contains(overwriteKey)) {
        policy.fallbacks.append(QStringLiteral("%1=%2 不是受支持的覆盖策略，已按「%3」处理。")
                                        .arg(overwriteKey, overwriteValue.toString(),
                                             overwritePolicyLabel(policy.overwritePolicy)));
    }

    const QString verifyKey = keyFor(QStringLiteral("verifyAfterCopy"));
    const QVariant verifyValue = values.value(verifyKey);
    VerifyMode verifyMode = policy.verifyMode;
    if (!verifyKey.isEmpty() && verifyModeFromIdentifier(verifyValue.toString(), &verifyMode)) {
        policy.verifyMode = verifyMode;
    } else if (!verifyKey.isEmpty() && values.contains(verifyKey)) {
        policy.fallbacks.append(QStringLiteral("%1=%2 不是受支持的校验方式，已按「%3」处理。")
                                        .arg(verifyKey, verifyValue.toString(),
                                             verifyModeLabel(policy.verifyMode)));
    }

    // 三项元数据只有「开关」这一个差别，写三遍会漏改其中一处。
    // 表里的两个标识符都是 ASCII，中文标签由 `metadataItemLabel()` 单独给，
    // 因此这里不需要把中文放进 `const char *` 字段（见 §6 那条坑）。
    struct BoolBinding {
        const char *keyIdentifier;
        const char *itemIdentifier;
        bool MetadataPreservation::*member;
    };
    const QVector<BoolBinding> bindings {
        { "preserveTimestamps", "timestamps", &MetadataPreservation::timestamps },
        { "preserveAttributes", "attributes", &MetadataPreservation::attributes },
        { "preservePermissions", "permissions", &MetadataPreservation::permissions }
    };
    for (const BoolBinding &binding : bindings) {
        const QString bindKey = keyFor(QString::fromLatin1(binding.keyIdentifier));
        if (bindKey.isEmpty() || !values.contains(bindKey))
            continue;
        const QVariant value = values.value(bindKey);
        if (value.type() != QVariant::Bool) {
            policy.fallbacks.append(QStringLiteral("%1=%2 不是布尔值，保留元数据「%3」仍为开启。")
                                            .arg(bindKey, value.toString(),
                                                 metadataItemLabel(QString::fromLatin1(binding.itemIdentifier))));
            continue;
        }
        policy.preserve.*(binding.member) = value.toBool();
    }

    const QString megabytesKey = keyFor(QStringLiteral("largeFileConfirmMegabytes"));
    if (!megabytesKey.isEmpty() && values.contains(megabytesKey)) {
        const QVariant value = values.value(megabytesKey);
        const bool isInteger = value.type() == QVariant::Int || value.type() == QVariant::LongLong;
        if (!isInteger || value.toLongLong() < 0) {
            policy.fallbacks.append(QStringLiteral("%1=%2 不是非负整数，已按默认值「%3」处理。")
                                            .arg(megabytesKey, value.toString(),
                                                 describeMegabytes(policy.largeFileConfirmBytes)));
        } else {
            policy.largeFileConfirmBytes = largeFileConfirmMegabytesToBytes(int(value.toLongLong()));
        }
    }

    const QString countKey = keyFor(QStringLiteral("batchDeleteConfirmCount"));
    if (!countKey.isEmpty() && values.contains(countKey)) {
        const QVariant value = values.value(countKey);
        const bool isInteger = value.type() == QVariant::Int || value.type() == QVariant::LongLong;
        if (!isInteger || value.toLongLong() < 0) {
            policy.fallbacks.append(QStringLiteral("%1=%2 不是非负整数，已按默认值「%3 个」处理。")
                                            .arg(countKey, value.toString())
                                            .arg(policy.batchDeleteConfirmCount));
        } else {
            policy.batchDeleteConfirmCount = int(value.toLongLong());
        }
    }

    return policy;
}

QStringList FileOperationPolicy::validate() const
{
    QStringList problems = fallbacks;

    // 阈值的单位是兆字节，因此策略里的字节数必须能被整兆表示。
    // 直接构造出 1 字节的策略在设置里存不下——那种值只可能来自代码，
    // 而它读回来会变成 0（=关闭确认），是「看起来一样的两个值」。
    if (largeFileConfirmBytes > 0 && largeFileConfirmBytes % BytesPerMegabyte != 0)
        problems.append(QStringLiteral("大文件确认阈值 %1 字节不是整兆字节，设置里存不下这个值。")
                                .arg(largeFileConfirmBytes));
    if (largeFileConfirmBytes < 0)
        problems.append(QStringLiteral("大文件确认阈值不得为负（%1）。").arg(largeFileConfirmBytes));
    if (batchDeleteConfirmCount < 0)
        problems.append(QStringLiteral("批量删除确认条数不得为负（%1）。").arg(batchDeleteConfirmCount));

    return problems;
}

QStringList FileOperationPolicy::safetyContractViolations() const
{
    QStringList violations;
    if (deleteMode != DeleteMode::Trash)
        violations.append(QStringLiteral("删除方式的默认值必须是「移入回收站」，当前是「%1」。")
                                  .arg(deleteModeLabel(deleteMode)));
    // 只有「直接覆盖」破坏这条契约。「跳过已存在的目标」虽然也不问，但它不
    // 覆盖任何东西——把它算成违规会让这条契约退化成「必须等于 Ask」这个
    // 与安全性无关的同义反复，于是「保守」这个词就失去了边界。
    if (overwritePolicy == OverwritePolicy::Overwrite)
        violations.append(QStringLiteral("覆盖策略的默认值不得是「直接覆盖」，当前正是它；"
                                         "出厂默认应为「逐个询问」。"));
    if (!preserve.all()) {
        QStringList missing;
        for (const QString &identifier : QStringList{QStringLiteral("timestamps"),
                                                     QStringLiteral("attributes"),
                                                     QStringLiteral("permissions")}) {
            const bool enabled = identifier == QStringLiteral("timestamps") ? preserve.timestamps
                                 : identifier == QStringLiteral("attributes") ? preserve.attributes
                                                                              : preserve.permissions;
            if (!enabled)
                missing.append(metadataItemLabel(identifier));
        }
        violations.append(QStringLiteral("复制默认必须保留元数据，当前未保留：%1。")
                                  .arg(missing.join(QStringLiteral("、"))));
    }
    return violations;
}

bool FileOperationPolicy::needsLargeFileConfirmation(qint64 bytes) const
{
    // 阈值 0 = 用户关掉了这个确认。
    if (largeFileConfirmBytes <= 0)
        return false;
    // 大小为负表示「不知道」。不知道就不拦：把未知当超大文件会让每一个
    // 拿不到 stat 的条目都弹一个框，而那种框用户只会闭着眼点掉。
    if (bytes < 0)
        return false;
    // 「达到阈值就该问」而不是「超过才问」——用户把阈值设成 100 MB 的意思是
    // 「碰 100 MB 的文件先让我看一眼」。
    return bytes >= largeFileConfirmBytes;
}

bool FileOperationPolicy::needsBatchDeleteConfirmation(int count) const
{
    if (batchDeleteConfirmCount <= 0)
        return false;
    if (count <= 0)
        return false;
    return count >= batchDeleteConfirmCount;
}

QString FileOperationPolicy::deleteWarning() const
{
    if (deleteMode == DeleteMode::Trash)
        return QString();
    return QStringLiteral("永久删除不可恢复：删除的文件不进入回收站，无法撤销。");
}

OverwriteDecision FileOperationPolicy::overwriteDecision(OverwriteSituation situation) const
{
    OverwriteDecision decision;
    decision.targetNewer = situation == OverwriteSituation::TargetNewer;

    // 目标不存在时没有冲突可谈：不管策略是「询问」「覆盖」还是「跳过」，
    // 结论都只能是「写进去」，且不该有任何提示。
    //
    // 这一条是刻意的：少了它，默认的「逐个询问」策略会在一个空目录里
    // 逐条追问用户「要覆盖吗」——而那里根本没有东西可被覆盖。
    if (situation == OverwriteSituation::TargetMissing) {
        decision.action = OverwriteAction::Overwrite;
        return decision;
    }

    switch (overwritePolicy) {
    case OverwritePolicy::Overwrite: decision.action = OverwriteAction::Overwrite; break;
    case OverwritePolicy::Skip:      decision.action = OverwriteAction::Skip;      break;
    case OverwritePolicy::Ask:       decision.action = OverwriteAction::Ask;       break;
    }

    const QString timeNote = situation == OverwriteSituation::TargetNewer
            ? QStringLiteral("目标文件比源文件新。")
            : situation == OverwriteSituation::TargetOlder
              ? QStringLiteral("目标文件比源文件旧。")
              : QStringLiteral("目标文件与源文件的修改时间相同。");

    switch (decision.action) {
    case OverwriteAction::Ask:
        if (decision.targetNewer) {
            // 「文件较新」要单独说清楚代价：覆盖会把**更新的**那份内容替掉。
            // 只说「目标已存在」时用户会顺手点「全部覆盖」，而这一批里
            // 恰恰混着几个比源文件更新的目标——那是真正的数据丢失。
            decision.notice = QStringLiteral("%1覆盖会丢掉目标文件里较新的内容。").arg(timeNote);
        } else {
            decision.notice = QStringLiteral("%1是否覆盖？").arg(timeNote);
        }
        break;
    case OverwriteAction::Overwrite:
        decision.notice = decision.targetNewer
                ? QStringLiteral("%1覆盖策略为「%2」，将直接覆盖并丢失较新的内容。")
                          .arg(timeNote, overwritePolicyLabel(overwritePolicy))
                : QStringLiteral("%1覆盖策略为「%2」，将直接覆盖。")
                          .arg(timeNote, overwritePolicyLabel(overwritePolicy));
        break;
    case OverwriteAction::Skip:
        decision.notice = decision.targetNewer
                ? QStringLiteral("%1覆盖策略为「%2」，已跳过（目标文件未被改动）。")
                          .arg(timeNote, overwritePolicyLabel(overwritePolicy))
                : QStringLiteral("%1覆盖策略为「%2」，已跳过。")
                          .arg(timeNote, overwritePolicyLabel(overwritePolicy));
        break;
    }
    return decision;
}

QString FileOperationPolicy::describe() const
{
    return QStringLiteral("删除=%1；覆盖=%2；保留元数据=%3；大文件确认=%4；批量删除确认=%5；复制后校验=%6")
            .arg(deleteModeLabel(deleteMode), overwritePolicyLabel(overwritePolicy),
                 preserve.all() ? QStringLiteral("全部")
                                : preserve.enabledIdentifiers().isEmpty()
                                  ? QStringLiteral("全部不保留")
                                  : preserve.enabledIdentifiers().join(QStringLiteral("/")),
                 describeMegabytes(largeFileConfirmBytes),
                 batchDeleteConfirmCount > 0 ? QStringLiteral("%1 个").arg(batchDeleteConfirmCount)
                                             : QStringLiteral("关闭"),
                 verifyModeLabel(verifyMode));
}

// ---------------------------------------------------------------------------
// 键表
// ---------------------------------------------------------------------------

const QVector<FileOperationKey> &fileOperationKeyTable()
{
    // 函数内静态并返回引用：按值返回临时容器再取元素会悬垂（见 §6）。
    static const QVector<FileOperationKey> table {
        {QStringLiteral("deleteMode"),
         QStringLiteral("fileops.deleteMode"),
         QStringLiteral("删除方式的默认值：回收站或永久删除")},
        {QStringLiteral("overwritePolicy"),
         QStringLiteral("fileops.overwritePolicy"),
         QStringLiteral("目标已存在时的默认处置：询问、覆盖或跳过")},
        {QStringLiteral("preserveTimestamps"),
         QStringLiteral("fileops.preserveTimestamps"),
         QStringLiteral("复制时保留修改时间")},
        {QStringLiteral("preserveAttributes"),
         QStringLiteral("fileops.preserveAttributes"),
         QStringLiteral("复制时保留属性位")},
        {QStringLiteral("preservePermissions"),
         QStringLiteral("fileops.preservePermissions"),
         QStringLiteral("复制时保留权限")},
        {QStringLiteral("largeFileConfirmMegabytes"),
         QStringLiteral("fileops.largeFileConfirmMegabytes"),
         QStringLiteral("大文件操作前的体积确认阈值（兆字节，0 表示关闭确认）")},
        {QStringLiteral("batchDeleteConfirmCount"),
         QStringLiteral("fileops.batchDeleteConfirmCount"),
         QStringLiteral("批量删除的条数确认阈值（0 表示关闭确认）")},
        {QStringLiteral("verifyAfterCopy"),
         QStringLiteral("fileops.verifyAfterCopy"),
         QStringLiteral("操作完成后的校验方式：不校验、比对大小或比对 CRC")}
    };
    return table;
}

QStringList fileOperationKeys()
{
    QStringList result;
    for (const FileOperationKey &entry : fileOperationKeyTable())
        result.append(entry.key);
    return result;
}

QString fileOperationKey(const QString &identifier)
{
    for (const FileOperationKey &entry : fileOperationKeyTable()) {
        if (entry.identifier == identifier)
            return entry.key;
    }
    return QString();
}

QStringList validateFileOperationKeyTable(const QVector<FileOperationKey> &table)
{
    QStringList problems;
    QSet<QString> identifiers;
    QSet<QString> keys;
    for (int index = 0; index < table.size(); ++index) {
        const FileOperationKey &entry = table.at(index);
        const QString position = QStringLiteral("第 %1 行").arg(index + 1);
        if (entry.identifier.isEmpty())
            problems.append(QStringLiteral("%1：短名不能为空。").arg(position));
        else if (identifiers.contains(entry.identifier))
            problems.append(QStringLiteral("%1：短名「%2」重复。").arg(position, entry.identifier));
        identifiers.insert(entry.identifier);

        if (entry.key.isEmpty()) {
            problems.append(QStringLiteral("%1：完整键名不能为空。").arg(position));
        } else {
            if (!entry.key.startsWith(QStringLiteral("fileops.")))
                problems.append(QStringLiteral("%1：键名「%2」缺少 fileops. 前缀。")
                                        .arg(position, entry.key));
            if (keys.contains(entry.key))
                problems.append(QStringLiteral("%1：键名「%2」重复。").arg(position, entry.key));
            keys.insert(entry.key);
        }

        if (entry.purpose.isEmpty())
            problems.append(QStringLiteral("%1：说明不能为空。").arg(position));
    }
    if (table.isEmpty())
        problems.append(QStringLiteral("键表为空：策略将读不到任何设置项。"));
    return problems;
}

} // namespace Files
} // namespace LqCompare
