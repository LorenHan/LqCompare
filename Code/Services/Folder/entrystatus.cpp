#include "entrystatus.h"

#include <QObject>

namespace LqCompare {
namespace Folder {

namespace {

// 主状态的全部取值。校验函数拿它当参照，被校验的表是参数——
// 「从模块内部的表读数据」会让一份故意写坏的表喂进去也永远绿。
QVector<Status> allStatusValues()
{
    return {Status::Same, Status::Different, Status::LeftOnly, Status::RightOnly,
            Status::TypeConflict, Status::Error, Status::Unknown,
            Status::BothChanged, Status::Conflict};
}

QVector<ContentEvidence> allContentEvidenceValues()
{
    return {ContentEvidence::NotCompared, ContentEvidence::Partial,
            ContentEvidence::ByteIdentical, ContentEvidence::ByteDifferent,
            ContentEvidence::RuleIdentical, ContentEvidence::RuleDifferent,
            ContentEvidence::CrcIdentical, ContentEvidence::CrcDifferent};
}

QVector<TimeRelation> allTimeRelationValues()
{
    return {TimeRelation::Unknown, TimeRelation::LeftNewer, TimeRelation::RightNewer,
            TimeRelation::Same};
}

QString kindText(Kind kind)
{
    switch (kind) {
    case Kind::Missing: return QObject::tr("不存在");
    case Kind::File: return QObject::tr("文件");
    case Kind::Directory: return QObject::tr("文件夹");
    case Kind::SymbolicLink: return QObject::tr("符号链接");
    case Kind::Other: return QObject::tr("特殊文件");
    }
    return {};
}

QString bytesText(quint64 bytes)
{
    return QObject::tr("%1 字节").arg(bytes);
}

// 一侧相对共同祖先有没有变。判据只用（大小，修改时间）：它们是比对侧
// 唯一握有的事实，而这两项任一变化都足以说明「这一侧被动过」。
bool sideDiffersFromAncestor(const Side &side, const AncestorRecord &ancestor)
{
    if (!side.exists())
        return false;
    if (side.kind == Kind::Directory)
        return false; // 目录自身的时间戳由子条目变动带出来，不是内容证据
    return side.info.size != ancestor.size || side.info.lastModified != ancestor.modified;
}

} // namespace

// ---------------------------------------------------------------------------
// 主状态表
// ---------------------------------------------------------------------------

const QVector<MainStatusDescriptor> &mainStatusTable()
{
    // 顺序 = 规格里点名的顺序（相同 / 不同 / 仅左 / 仅右 / 错误 / 未知）+
    // 两个必须有基线的取值。界面下拉、报表映射、命令行映射都按这张表的顺序走，
    // 因此这里的顺序本身就是一处事实来源。
    //
    // 标识符沿用**命令行早就在输出**的那套词（equal / different / left-only /
    // type-conflict / not-compared），而不是新造一套：机器标识一旦有两份，
    // 报表与命令行迟早会对同一个状态给出两个名字。
    // `not-compared` 这个名字同时覆盖「未知」与「未完整比较」两件事，
    // 而它是命令行 JSON 的既有契约，改名的收益是零。
    static const QVector<MainStatusDescriptor> table = {
        {Status::Same, "equal", false, ":/Pictures/status_same.svg"},
        {Status::Different, "different", false, ":/Pictures/status_different.svg"},
        {Status::LeftOnly, "left-only", false, ":/Pictures/status_left_only.svg"},
        {Status::RightOnly, "right-only", false, ":/Pictures/status_right_only.svg"},
        {Status::Error, "error", false, ":/Pictures/status_error.svg"},
        {Status::Unknown, "not-compared", false, ":/Pictures/status_unknown.svg"},
        {Status::TypeConflict, "type-conflict", false, ":/Pictures/status_type_conflict.svg"},
        {Status::BothChanged, "both-changed", true, ":/Pictures/status_both_changed.svg"},
        {Status::Conflict, "conflict", true, ":/Pictures/status_conflict.svg"},
    };
    return table;
}

QString statusIdentifier(Status status)
{
    for (const auto &row : mainStatusTable()) {
        if (row.value == status)
            return QString::fromUtf8(row.identifier);
    }
    return {};
}

bool statusRequiresBaseline(Status status)
{
    for (const auto &row : mainStatusTable()) {
        if (row.value == status)
            return row.requiresBaseline;
    }
    return false;
}

QString statusIconKey(Status status)
{
    for (const auto &row : mainStatusTable()) {
        if (row.value == status)
            return QString::fromUtf8(row.iconKey);
    }
    return {};
}

bool isKnownStatus(int rawValue)
{
    for (Status value : allStatusValues()) {
        if (static_cast<int>(value) == rawValue)
            return true;
    }
    return false;
}

QStringList validateMainStatusTable(const QVector<MainStatusDescriptor> &table)
{
    QStringList problems;
    const auto values = allStatusValues();
    if (table.size() != values.size())
        problems << QObject::tr("主状态表应有 %1 行，实际 %2 行。")
                        .arg(values.size()).arg(table.size());
    for (Status value : values) {
        int found = 0;
        for (const auto &row : table)
            found += row.value == value ? 1 : 0;
        if (found == 0)
            problems << QObject::tr("主状态表缺少取值 %1。").arg(int(value));
        else if (found > 1)
            problems << QObject::tr("主状态表里取值 %1 出现 %2 次（互斥集合不得重复）。")
                            .arg(int(value)).arg(found);
    }
    QVector<QString> identifiers;
    QVector<QString> iconKeys;
    for (int i = 0; i < table.size(); ++i) {
        const auto &row = table.at(i);
        const QString identifier = QString::fromUtf8(row.identifier);
        const QString iconKey = QString::fromUtf8(row.iconKey);
        if (identifier.isEmpty())
            problems << QObject::tr("主状态表第 %1 行的标识符为空。").arg(i + 1);
        else if (identifiers.contains(identifier))
            problems << QObject::tr("主状态表里标识符「%1」重复。").arg(identifier);
        else
            identifiers.append(identifier);
        // 颜色之外必须同时有图标和文本（第 4 条），所以图标键不得为空；
        // 图标键重复则两个状态长得一样，等于把「不靠颜色」的承诺作废。
        if (iconKey.isEmpty())
            problems << QObject::tr("主状态表第 %1 行没有图标。").arg(i + 1);
        else if (iconKeys.contains(iconKey))
            problems << QObject::tr("主状态表里图标「%1」被两个状态共用。").arg(iconKey);
        else
            iconKeys.append(iconKey);
        // 「两侧均改 / 冲突」是唯一两档必须有基线的取值；反过来，其他取值
        // 一旦被标成「需要基线」，比较引擎在没有基线时就再也不敢产出它们了。
        const bool baselineOnly = row.value == Status::BothChanged || row.value == Status::Conflict;
        if (row.requiresBaseline != baselineOnly)
            problems << QObject::tr("主状态表里取值 %1 的 requiresBaseline 应为 %2。")
                            .arg(int(row.value)).arg(baselineOnly ? "true" : "false");
    }
    return problems;
}

// ---------------------------------------------------------------------------
// 内容证据表
// ---------------------------------------------------------------------------

const QVector<ContentEvidenceDescriptor> &contentEvidenceTable()
{
    static const QVector<ContentEvidenceDescriptor> table = {
        {ContentEvidence::NotCompared, "not-compared", false, false},
        {ContentEvidence::Partial, "partial", false, false},
        {ContentEvidence::ByteIdentical, "byte-identical", true, true},
        {ContentEvidence::ByteDifferent, "byte-different", false, true},
        {ContentEvidence::RuleIdentical, "rule-identical", true, true},
        {ContentEvidence::RuleDifferent, "rule-different", false, true},
        {ContentEvidence::CrcIdentical, "crc-identical", true, true},
        {ContentEvidence::CrcDifferent, "crc-different", false, true},
    };
    return table;
}

QString contentEvidenceIdentifier(ContentEvidence evidence)
{
    for (const auto &row : contentEvidenceTable()) {
        if (row.value == evidence)
            return QString::fromUtf8(row.identifier);
    }
    return {};
}

bool contentEvidenceProvesIdentity(ContentEvidence evidence)
{
    for (const auto &row : contentEvidenceTable()) {
        if (row.value == evidence)
            return row.provesContentIdentity;
    }
    return false;
}

bool contentEvidenceCoversWholeContent(ContentEvidence evidence)
{
    for (const auto &row : contentEvidenceTable()) {
        if (row.value == evidence)
            return row.coversWholeContent;
    }
    return false;
}

QStringList validateContentEvidenceTable(const QVector<ContentEvidenceDescriptor> &table)
{
    QStringList problems;
    const auto values = allContentEvidenceValues();
    if (table.size() != values.size())
        problems << QObject::tr("内容证据表应有 %1 行，实际 %2 行。")
                        .arg(values.size()).arg(table.size());
    for (ContentEvidence value : values) {
        int found = 0;
        for (const auto &row : table)
            found += row.value == value ? 1 : 0;
        if (found == 0)
            problems << QObject::tr("内容证据表缺少取值 %1。").arg(int(value));
        else if (found > 1)
            problems << QObject::tr("内容证据表里取值 %1 重复。").arg(int(value));
    }
    QVector<QString> identifiers;
    for (int i = 0; i < table.size(); ++i) {
        const auto &row = table.at(i);
        const QString identifier = QString::fromUtf8(row.identifier);
        if (identifier.isEmpty())
            problems << QObject::tr("内容证据表第 %1 行的标识符为空。").arg(i + 1);
        else if (identifiers.contains(identifier))
            problems << QObject::tr("内容证据表里标识符「%1」重复。").arg(identifier);
        else
            identifiers.append(identifier);
        // 「证明了相同」蕴含「看完了全部内容」。`Partial` 是最容易写反的那一格：
        // 限内全同**不**证明相同，这正是 DIR-008 三条边界的前两条。
        if (row.provesContentIdentity && !row.coversWholeContent)
            problems << QObject::tr("内容证据表第 %1 行声称证明相同，却没有覆盖全部内容。")
                            .arg(i + 1);
    }
    return problems;
}

// ---------------------------------------------------------------------------
// 时间关系表
// ---------------------------------------------------------------------------

const QVector<TimeRelationDescriptor> &timeRelationTable()
{
    static const QVector<TimeRelationDescriptor> table = {
        {TimeRelation::Unknown, "unknown"},
        {TimeRelation::LeftNewer, "left-newer"},
        {TimeRelation::RightNewer, "right-newer"},
        {TimeRelation::Same, "same"},
    };
    return table;
}

QString timeRelationIdentifier(TimeRelation relation)
{
    for (const auto &row : timeRelationTable()) {
        if (row.value == relation)
            return QString::fromUtf8(row.identifier);
    }
    return {};
}

QString timeRelationLabel(TimeRelation relation)
{
    switch (relation) {
    case TimeRelation::Unknown: return QObject::tr("时间不可比");
    case TimeRelation::LeftNewer: return QObject::tr("左侧较新");
    case TimeRelation::RightNewer: return QObject::tr("右侧较新");
    case TimeRelation::Same: return QObject::tr("两侧时间相同");
    }
    return {};
}

QStringList validateTimeRelationTable(const QVector<TimeRelationDescriptor> &table)
{
    QStringList problems;
    const auto values = allTimeRelationValues();
    if (table.size() != values.size())
        problems << QObject::tr("时间关系表应有 %1 行，实际 %2 行。")
                        .arg(values.size()).arg(table.size());
    QVector<QString> identifiers;
    for (int i = 0; i < table.size(); ++i) {
        const auto &row = table.at(i);
        const QString identifier = QString::fromUtf8(row.identifier);
        if (identifier.isEmpty())
            problems << QObject::tr("时间关系表第 %1 行的标识符为空。").arg(i + 1);
        else if (identifiers.contains(identifier))
            problems << QObject::tr("时间关系表里标识符「%1」重复。").arg(identifier);
        else
            identifiers.append(identifier);
    }
    for (TimeRelation value : values) {
        int found = 0;
        for (const auto &row : table)
            found += row.value == value ? 1 : 0;
        if (found != 1)
            problems << QObject::tr("时间关系表里取值 %1 应恰好出现一次，实际 %2 次。")
                            .arg(int(value)).arg(found);
    }
    return problems;
}

TimeRelation compareTimes(const Files::FileTime &left, const Files::FileTime &right,
                          qint64 toleranceMs)
{
    if (!left.isValid() || !right.isValid())
        return TimeRelation::Unknown;
    if (toleranceMs > 0) {
        const qint64 delta = left.nanosecondsSinceEpoch() - right.nanosecondsSinceEpoch();
        const qint64 tolerance = toleranceMs * 1000000;
        if (delta <= tolerance && delta >= -tolerance)
            return TimeRelation::Same;
    }
    if (left == right)
        return TimeRelation::Same;
    return left > right ? TimeRelation::LeftNewer : TimeRelation::RightNewer;
}

TimeRelation timeRelationFor(const Entry &entry, qint64 toleranceMs)
{
    // 「两侧都真实存在」这条前置只在这里说一次。孤儿项没有时间关系可言：
    // 把「缺失」当成「很旧」会凭空造出一个左右较新的结论。
    if (!entry.left.exists() || !entry.right.exists())
        return TimeRelation::Unknown;
    return compareTimes(entry.left.info.lastModified, entry.right.info.lastModified, toleranceMs);
}

// ---------------------------------------------------------------------------
// 孤儿项与存在性
// ---------------------------------------------------------------------------

bool isOrphan(Status status)
{
    return status == Status::LeftOnly || status == Status::RightOnly;
}

QVector<Status> orphanStatuses()
{
    return {Status::LeftOnly, Status::RightOnly};
}

Existence existenceOf(const Entry &entry)
{
    const bool left = entry.left.exists();
    const bool right = entry.right.exists();
    if (left && right)
        return Existence::Both;
    if (left)
        return Existence::LeftOnly;
    if (right)
        return Existence::RightOnly;
    return Existence::Neither;
}

QString existenceIdentifier(Existence existence)
{
    switch (existence) {
    case Existence::Both: return QStringLiteral("both");
    case Existence::LeftOnly: return QStringLiteral("left-only");
    case Existence::RightOnly: return QStringLiteral("right-only");
    case Existence::Neither: return QStringLiteral("neither");
    case Existence::Unknown: return QStringLiteral("unknown");
    }
    return {};
}

QString existenceLabel(Existence existence)
{
    switch (existence) {
    case Existence::Both: return QObject::tr("两侧都有");
    case Existence::LeftOnly: return QObject::tr("仅左侧存在");
    case Existence::RightOnly: return QObject::tr("仅右侧存在");
    case Existence::Neither: return QObject::tr("两侧都没有");
    case Existence::Unknown: return QObject::tr("存在性未知");
    }
    return {};
}

// ---------------------------------------------------------------------------
// 基线
// ---------------------------------------------------------------------------

QStringList validateBaselineView(const BaselineView &baseline)
{
    QStringList problems;
    if (!baseline.valid) {
        problems << QObject::tr("基线未标记为有效。");
        return problems;
    }
    if (baseline.leftRoot.trimmed().isEmpty())
        problems << QObject::tr("基线没有绑定左侧根目录。");
    if (baseline.rightRoot.trimmed().isEmpty())
        problems << QObject::tr("基线没有绑定右侧根目录。");
    if (baseline.ancestors.isEmpty())
        problems << QObject::tr("基线里一条祖先记录都没有。");
    QStringList bad;
    for (auto it = baseline.ancestors.cbegin(); it != baseline.ancestors.cend(); ++it) {
        // 绝对路径或反斜杠开头的键说明这份基线不是「相对两侧根目录」的，
        // 拿它去查条目必然一条都对不上——那会让整条基线静默失效。
        if (it.key().isEmpty() || it.key().startsWith(QLatin1Char('/'))
            || it.key().startsWith(QLatin1Char('\\')) || it.key().contains(QLatin1String(":")))
            bad << it.key();
        if (!it.value().isValid())
            bad << it.key();
    }
    if (!bad.isEmpty()) {
        bad.removeDuplicates();
        problems << QObject::tr("基线里有 %1 条记录不可用（键必须是相对路径，且带修改时间）：%2。")
                        .arg(bad.size()).arg(bad.join(QStringLiteral("、")));
    }
    return problems;
}

bool applyBaselineStatus(Entry &entry, const BaselineView &baseline)
{
    if (!validateBaselineView(baseline).isEmpty())
        return false;
    // 目录的结论由子条目汇总（第 5 条），基线细化只作用在叶子上；
    // 否则一个目录的时间戳一动就会被判成「两侧均改」，与它的子条目矛盾。
    if (entry.isDirectory() || !entry.left.exists() || !entry.right.exists())
        return false;
    if (entry.status != Status::Same && entry.status != Status::Different)
        return false;
    if (!entry.inComparison())
        return false;
    const AncestorRecord ancestor = baseline.ancestors.value(entry.relativePath);
    if (!ancestor.isValid())
        return false;
    if (!sideDiffersFromAncestor(entry.left, ancestor)
        || !sideDiffersFromAncestor(entry.right, ancestor))
        return false;
    // 两侧都相对祖先变了：彼此仍然相同说明改法一致（同步无害），
    // 彼此不同就必须由人决定。
    if (entry.status == Status::Same) {
        entry.status = Status::BothChanged;
        entry.explanation = QObject::tr("两侧都相对基线的共同祖先改动过，且两侧当前内容一致（改法相同）。");
        return true;
    }
    entry.status = Status::Conflict;
    entry.explanation = QObject::tr("两侧都相对基线的共同祖先改动过，且两侧当前内容不同，需要人工决定保留哪一侧。");
    return true;
}

// ---------------------------------------------------------------------------
// 父子一致
// ---------------------------------------------------------------------------

ParentAggregate aggregateChildren(const QVector<ChildStatus> &children, bool scanStopped)
{
    ParentAggregate aggregate;
    for (const auto &child : children) {
        if (!child.inComparison) {
            aggregate.hasExcludedDescendants = true;
            continue;
        }
        aggregate.hasIncludedDescendants = true;
        if (child.status == Status::Error) {
            aggregate.status = Status::Error;
            break;
        }
        if (child.status != Status::Same && child.status != Status::Unknown)
            aggregate.status = Status::Different;
        else if (child.status == Status::Unknown && aggregate.status == Status::Same)
            aggregate.status = Status::Unknown;
    }
    // 「没有子条目」不等于「里面什么都没有」：取消之后剩下的子条目根本没看，
    // 把这种空洞的目录说成「相同」就会在同步场景里给出相反的动作依据。
    // 没读过的目录（递归被关闭、达到递归上限）不走这里——它们在自己的行上
    // 已经落成「未知」，汇总时看到的就是「未知」。
    if (scanStopped && aggregate.status == Status::Same)
        aggregate.status = Status::Unknown;
    return aggregate;
}

// ---------------------------------------------------------------------------
// 「为什么是这个状态」
// ---------------------------------------------------------------------------

QString reasonKindLabel(ReasonKind kind)
{
    switch (kind) {
    case ReasonKind::Criterion: return QObject::tr("准则");
    case ReasonKind::Override: return QObject::tr("覆盖策略");
    case ReasonKind::Conclusion: return QObject::tr("最终结论");
    }
    return {};
}

QVector<ReasonLine> statusReasonLines(const Entry &entry, const Options &options,
                                      bool baselineApplied)
{
    QVector<ReasonLine> lines;
    const auto criterion = [&lines](const QString &text) {
        lines.append({ReasonKind::Criterion, text});
    };
    const auto override = [&lines](const QString &text) {
        lines.append({ReasonKind::Override, text});
    };

    // ---- 各准则 ----
    criterion(QObject::tr("存在性：%1 / %2（%3）")
                  .arg(entry.left.exists() ? entry.left.info.name : QObject::tr("不存在"),
                       entry.right.exists() ? entry.right.info.name : QObject::tr("不存在"),
                       existenceLabel(existenceOf(entry))));
    criterion(QObject::tr("类型：左侧 %1 / 右侧 %2")
                  .arg(kindText(entry.left.kind), kindText(entry.right.kind)));
    if (entry.left.exists() && entry.right.exists() && !entry.isDirectory()) {
        criterion(QObject::tr("大小：左侧 %1 / 右侧 %2")
                      .arg(bytesText(entry.left.info.size), bytesText(entry.right.info.size)));
    }
    criterion(QObject::tr("内容证据：%1（%2）")
                  .arg(contentEvidenceIdentifier(entry.contentEvidence),
                       contentEvidenceProvesIdentity(entry.contentEvidence)
                           ? QObject::tr("已证明内容相同")
                           : contentEvidenceCoversWholeContent(entry.contentEvidence)
                           ? QObject::tr("已比较完，结论不是相同")
                           : QObject::tr("尚未看完内容")));
    criterion(QObject::tr("时间关系：%1").arg(timeRelationLabel(entry.timeRelation)));
    if (entry.excludedByMask)
        criterion(QObject::tr("扫描掩码：未命中包含规则。%1").arg(entry.filterReason));
    else
        criterion(QObject::tr("扫描掩码：在比较范围内。"));
    if (baselineApplied)
        criterion(QObject::tr("基线：本次比较提供了有效基线。"));
    else
        criterion(QObject::tr("基线：本次比较没有提供有效基线。"));

    // ---- 覆盖策略 ----
    if (!options.compareContent)
        override(QObject::tr("内容比较已关闭：大小相同的条目最多只能到「未知」。"));
    else if (entry.left.exists() && entry.right.exists() && !entry.isDirectory()
             && entry.left.info.size != entry.right.info.size)
        // 只在**真的**短路过时才说这句话。大小相同时把「短路」也印出来，
        // 会让「为什么是这个状态」这一栏解释一件没发生过的事。
        override(QObject::tr("大小不同时短路内容比较：字节内容必然不同，无需读任何字节。"));
    if (entry.left.kind == Kind::SymbolicLink || entry.right.kind == Kind::SymbolicLink)
        override(QObject::tr("符号链接只比较链接目标本身，不跟随链接。"));
    if (options.compareFirstBytes > 0)
        override(QObject::tr("只比较前 %1 字节：限内全同只记「部分比较」，不足以判定相同。")
                     .arg(options.compareFirstBytes));
    if (!options.recursive)
        override(QObject::tr("递归已关闭：子目录不展开比较。"));
    if (entry.excludedByMask)
        override(QObject::tr("扫描掩码的排除优先于其余全部准则。"));
    if (entry.nameCaseDifference)
        override(QObject::tr("名称仅大小写不同：已按忽略大小写配成一对，路径仍是各自的真实写法。"));
    if (statusRequiresBaseline(entry.status) && !baselineApplied)
        override(QObject::tr("没有有效基线：不推断「两侧均改」与「冲突」。"));
    else if (baselineApplied && !statusRequiresBaseline(entry.status)
             && entry.status != Status::Same && entry.status != Status::Different)
        override(QObject::tr("基线只把「相同 / 不同」细化成「两侧均改 / 冲突」，本条目落在别的档上。"));

    // ---- 最终结论 ----
    lines.append({ReasonKind::Conclusion,
                  QObject::tr("%1（%2）").arg(statusLabel(entry.status), statusIdentifier(entry.status))});
    if (!entry.explanation.isEmpty())
        lines.append({ReasonKind::Conclusion, entry.explanation});
    return lines;
}

// ---------------------------------------------------------------------------
// 模型自检
// ---------------------------------------------------------------------------

QStringList statusModelViolations(const Entry &entry, bool baselineWasValid)
{
    QStringList problems;
    const auto existence = existenceOf(entry);
    const bool leftError = !entry.left.error.isEmpty();
    const bool rightError = !entry.right.error.isEmpty();

    if (statusRequiresBaseline(entry.status) && !baselineWasValid) {
        problems << QObject::tr("没有有效基线却给出「%1」：两侧均改不能由两份当前文件推断。")
                        .arg(statusLabel(entry.status));
    }

    // 非目录条目判成「相同」时必须拿得出证明内容相同的证据。
    // 这一条同时挡住两种情形：`Partial`（只比了前 N 字节）与 `NotCompared`。
    if (entry.status == Status::Same && !entry.isDirectory()
        && !contentEvidenceProvesIdentity(entry.contentEvidence)) {
        problems << QObject::tr("内容证据是「%1」，不足以判成相同。")
                        .arg(contentEvidenceIdentifier(entry.contentEvidence));
    }

    if (entry.status == Status::LeftOnly && existence != Existence::LeftOnly)
        problems << QObject::tr("判成仅左存在，但左侧%1。")
                        .arg(entry.left.exists() ? QObject::tr("存在") : QObject::tr("存在性未知"));
    if (entry.status == Status::RightOnly && existence != Existence::RightOnly)
        problems << QObject::tr("判成仅右存在，但右侧%1。")
                        .arg(entry.right.exists() ? QObject::tr("存在") : QObject::tr("存在性未知"));
    const bool needsBoth = entry.status == Status::Same || entry.status == Status::Different
        || entry.status == Status::TypeConflict || entry.status == Status::BothChanged
        || entry.status == Status::Conflict;
    if (needsBoth && existence != Existence::Both)
        problems << QObject::tr("判成「%1」，但两侧并非都存在。").arg(statusLabel(entry.status));

    if (leftError && entry.status == Status::Same)
        problems << QObject::tr("左侧报错，不得判成相同。");
    if (rightError && entry.status == Status::Same)
        problems << QObject::tr("右侧报错，不得判成相同。");
    if (leftError && entry.status == Status::RightOnly)
        problems << QObject::tr("左侧报错时不得断定「仅右存在」。");
    if (rightError && entry.status == Status::LeftOnly)
        problems << QObject::tr("右侧报错时不得断定「仅左存在」。");

    if (entry.status == Status::Error && contentEvidenceCoversWholeContent(entry.contentEvidence)) {
        problems << QObject::tr("读到错误却声称内容证据是「%1」（已看完内容）。")
                        .arg(contentEvidenceIdentifier(entry.contentEvidence));
    }

    // 时间维度不能凭空长出结论：两侧缺一侧就没有「谁更新」。
    if (entry.timeRelation != TimeRelation::Unknown && existence != Existence::Both)
        problems << QObject::tr("两侧并非都存在，却给出了时间关系「%1」。")
                        .arg(timeRelationIdentifier(entry.timeRelation));
    return problems;
}

} // namespace Folder
} // namespace LqCompare
