#include "attributefilter.h"

#include <QRegularExpression>
#include <QSet>

namespace LqCompare {
namespace Filter {

// -----------------------------------------------------------------------------
// 三条贯穿全文件的约定（完整理由写在 attributefilter.h 顶部）
//
//   1. 元数据缺失 → 放行 + 计入 undecided（不是拒绝）。
//   2. 写坏的条件 → 不参与收窄 + 报 ConditionProblem（不是让结果变空）。
//   3. 「现在」由调用方给进来，本文件从不调 QDateTime::currentDateTime()。
//
// 这三条各自都会在下一个人「顺手修一下」时变成 bug，所以每处的实现里
// 还各留了一句就地说明。
// -----------------------------------------------------------------------------

namespace {

// -----------------------------------------------------------------------------
// 条件表（唯一的事实来源）
// -----------------------------------------------------------------------------

///
/// \brief 四类条件的表。
///
/// 顺序即判定顺序、即面板顺序、即 `AttributeDecision::outcomes` 的下标顺序。
/// 声明解析认的键、错误提示里的可用键清单也从这里取——「提示里说的键」与
/// 「实际认的键」分家是用户完全无法自查的一类偏差。
///
/// `expectedConditions`（`validateAttributeConditionTable` 的第二个参数）是唯一
/// 不依赖这张表自身的期望值，因此往表里加一类条件时，两处都要改，`-Wswitch`
/// 会在别处的 switch 里提醒。
///
const QVector<AttributeConditionDescriptor> &shippedConditionTable()
{
    static const QVector<AttributeConditionDescriptor> table = [] {
        QVector<AttributeConditionDescriptor> rows;

        // 注意：这里必须写 `QStringList{...}` 而不是 `= {QStringLiteral(...), ...}`。
        // Qt 5.15 的 `QStringLiteral` 展开成一个**含 static 局部变量**的 lambda，
        // 它出现在 `= {…}` 的初始化列表里时，clang 会把整个列表判成 void，
        // 报一句与真实原因毫无关系的「use of overloaded operator '=' is ambiguous
        // (with operand types 'QStringList' and 'void')」。
        // 显式构造 QStringList 即可，两个平台都编得过。
        AttributeConditionDescriptor size;
        size.kind = AttributeConditionKind::Size;
        size.identifier = QStringLiteral("size");
        size.label = QStringLiteral("大小");
        size.declarationKeys = QStringList{QStringLiteral("size-min"), QStringLiteral("size-max")};
        size.unitHint = QStringLiteral("如 10 MB、1.5 GB、4096");
        rows.append(size);

        AttributeConditionDescriptor time;
        time.kind = AttributeConditionKind::TimeRange;
        time.identifier = QStringLiteral("time");
        time.label = QStringLiteral("修改时间");
        time.declarationKeys = QStringList{QStringLiteral("time-relative"), QStringLiteral("time-from"),
                                          QStringLiteral("time-to")};
        // 相对区间要拿「现在」减天数，是本表里唯一需要参考时刻的一类。
        time.needsReferenceTime = true;
        time.unitHint = QStringLiteral("如 最近 7 天、2026-09-01");
        rows.append(time);

        AttributeConditionDescriptor bits;
        bits.kind = AttributeConditionKind::Attributes;
        bits.identifier = QStringLiteral("attributes");
        bits.label = QStringLiteral("属性位");
        bits.declarationKeys = QStringList{QStringLiteral("attr"), QStringLiteral("-attr")};
        bits.unitHint = QStringLiteral("只读 / 隐藏 / 系统 / 归档");
        rows.append(bits);

        AttributeConditionDescriptor owner;
        owner.kind = AttributeConditionKind::Owner;
        owner.identifier = QStringLiteral("owner");
        owner.label = QStringLiteral("所有者与组");
        owner.declarationKeys = QStringList{QStringLiteral("owner"), QStringLiteral("-owner"),
                                           QStringLiteral("group"), QStringLiteral("-group")};
        // 账号名的大小写敏感性在 Unix 与 Windows 上不同。
        owner.needsPlatform = true;
        owner.unitHint = QStringLiteral("账号名或组名，逗号分隔");
        rows.append(owner);

        return rows;
    }();
    return table;
}

///
/// \brief 「不知道」时的统一出参。
///
/// 单独一个函数是为了让「放行 + evaluated=false」这件事只有一个写法：
/// 四处各写一遍的话，总有一处会写成 `accepted = false`，
/// 而现象是「某个平台上勾一下某个条件就什么都看不见了」。
///
ConditionOutcome undecidedOutcome(const QString &reason)
{
    ConditionOutcome outcome;
    outcome.accepted = true;
    outcome.evaluated = false;
    outcome.reason = reason;
    return outcome;
}

ConditionOutcome rejectedOutcome(const QString &reason)
{
    ConditionOutcome outcome;
    outcome.accepted = false;
    outcome.evaluated = true;
    outcome.reason = reason;
    return outcome;
}

ConditionOutcome passedOutcome(const QString &reason)
{
    ConditionOutcome outcome;
    outcome.accepted = true;
    outcome.evaluated = true;
    outcome.reason = reason;
    return outcome;
}

/// 条件没生效时的出参（未启用 / 没填内容 / 配置写错）。
ConditionOutcome inactiveOutcome(const QString &label)
{
    return undecidedOutcome(QStringLiteral("%1条件未生效").arg(label));
}

QString conditionLabelFor(AttributeConditionKind kind)
{
    const AttributeConditionDescriptor *descriptor = attributeConditionDescriptor(kind);
    return descriptor ? descriptor->label : QStringLiteral("条件");
}

/// 全部声明键（错误提示里「可用键」那一栏）。从表里取，因此提示里写的键
/// 与实际认的键不可能分家——分家是用户完全无法自查的一类偏差。
QString allDeclarationKeysHint()
{
    QStringList keys;
    for (const AttributeConditionDescriptor &row : attributeConditionTable())
        keys.append(row.declarationKeys);
    keys.sort();
    return keys.join(QStringLiteral("、"));
}

/// 名字比较。平台决定大小写敏感性（Unix 敏感、Windows 不敏感）。
bool nameEquals(const QString &left, const QString &right, MaskPlatform platform)
{
    return QString::compare(left, right, defaultCaseSensitivity(platform)) == 0;
}

/// 名字是否命中名单里的任意一项。
bool matchesAnyInList(const QString &name, const QStringList &list, MaskPlatform platform)
{
    for (const QString &candidate : list) {
        if (nameEquals(name, candidate, platform))
            return true;
    }
    return false;
}

/// 属性位条件的名字清单，如「只读、隐藏」。
QString attributeListText(const QVector<EntryAttribute> &attributes)
{
    QStringList names;
    for (EntryAttribute attribute : attributes)
        names.append(entryAttributeLabel(attribute));
    return names.join(QStringLiteral("、"));
}

/// 时刻的显示格式。刻意写死格式串而不是用系统的区域设置：
/// 判定依据会进日志与 issue，格式随区域变化会让它们不可比对。
QString formatMoment(const QDateTime &moment)
{
    if (!moment.isValid())
        return QStringLiteral("（无效时刻）");
    // 零点整只显示日期：大部分时间条件是整天（「2026-09-01 起」），
    // 后面缀一个 00:00 只会让文本变长而不增加信息。
    if (moment.time() == QTime(0, 0))
        return moment.toString(QStringLiteral("yyyy-MM-dd"));
    return moment.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/// 大小的两种写法拼在一起：`1.5 KB（1536 字节）`。
QString sizeAndExact(quint64 bytes)
{
    return formatSizeExactText(bytes);
}

} // namespace

// -----------------------------------------------------------------------------
// 条件的种类
// -----------------------------------------------------------------------------

const QVector<AttributeConditionDescriptor> &attributeConditionTable()
{
    return shippedConditionTable();
}

const AttributeConditionDescriptor *attributeConditionDescriptor(AttributeConditionKind kind)
{
    for (const AttributeConditionDescriptor &row : attributeConditionTable()) {
        if (row.kind == kind)
            return &row;
    }
    return nullptr;
}

const char *attributeConditionIdentifier(AttributeConditionKind kind)
{
    switch (kind) {
    case AttributeConditionKind::Size:
        return "size";
    case AttributeConditionKind::TimeRange:
        return "time";
    case AttributeConditionKind::Attributes:
        return "attributes";
    case AttributeConditionKind::Owner:
        return "owner";
    }
    return "unknown";
}

QString attributeConditionLabel(AttributeConditionKind kind)
{
    return conditionLabelFor(kind);
}

QVector<AttributeConditionKind> allAttributeConditions()
{
    QVector<AttributeConditionKind> kinds;
    kinds.reserve(attributeConditionTable().size());
    for (const AttributeConditionDescriptor &row : attributeConditionTable())
        kinds.append(row.kind);
    return kinds;
}

int attributeConditionIndex(AttributeConditionKind kind)
{
    const QVector<AttributeConditionKind> kinds = allAttributeConditions();
    for (int index = 0; index < kinds.size(); ++index) {
        if (kinds.at(index) == kind)
            return index;
    }
    return -1;
}

bool attributeConditionKindForKey(const QString &key, AttributeConditionKind *kind)
{
    const QString bare = key.startsWith(QLatin1Char('-')) ? key.mid(1) : key;
    for (const AttributeConditionDescriptor &row : attributeConditionTable()) {
        // 先认完整键（`-attr` / `-owner` / `-group` 就在表里），再认去掉前导 `-` 的形态。
        if (row.declarationKeys.contains(key) || row.declarationKeys.contains(bare)) {
            if (kind)
                *kind = row.kind;
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// 配置问题
// -----------------------------------------------------------------------------

const char *conditionFieldIdentifier(ConditionField field)
{
    switch (field) {
    case ConditionField::None:
        return "none";
    case ConditionField::SizeMin:
        return "size-min";
    case ConditionField::SizeMax:
        return "size-max";
    case ConditionField::TimeFrom:
        return "time-from";
    case ConditionField::TimeTo:
        return "time-to";
    case ConditionField::TimeRelative:
        return "time-relative";
    case ConditionField::Attributes:
        return "attributes";
    case ConditionField::Owner:
        return "owner";
    case ConditionField::Group:
        return "group";
    }
    return "unknown";
}

QString conditionFieldLabel(ConditionField field)
{
    switch (field) {
    case ConditionField::None:
        return QStringLiteral("（未定位）");
    case ConditionField::SizeMin:
        return QStringLiteral("大小下限");
    case ConditionField::SizeMax:
        return QStringLiteral("大小上限");
    case ConditionField::TimeFrom:
        return QStringLiteral("起始时间");
    case ConditionField::TimeTo:
        return QStringLiteral("结束时间");
    case ConditionField::TimeRelative:
        return QStringLiteral("相对时间");
    case ConditionField::Attributes:
        return QStringLiteral("属性位");
    case ConditionField::Owner:
        return QStringLiteral("所有者");
    case ConditionField::Group:
        return QStringLiteral("组");
    }
    return QString();
}

QString ConditionProblem::describe() const
{
    // 行号只在问题确实来自声明文本时才出现：表单里填错的位置用 field 定位，
    // 报一个「第 -1 行」只会让人以为程序算错了。
    QString location = conditionFieldLabel(field);
    if (line >= 0)
        location = QStringLiteral("第 %1 行 · %2").arg(line + 1).arg(location);

    // `hasCondition` 为假的是整行级别的问题（未知的键），那时说「哪一类条件」
    // 本身就是错的——把原始键写出来，用户才认得出自己写的是哪一行。
    const QString scope = hasCondition
                              ? attributeConditionLabel(condition)
                              : (key.isEmpty() ? QStringLiteral("声明") : QStringLiteral("声明键「%1」").arg(key));

    QString text = QStringLiteral("%1 / %2：%3").arg(scope, location, message);
    if (!hint.isEmpty())
        text += QStringLiteral("（%1）").arg(hint);
    return text;
}

QString describeConditionProblems(const QVector<ConditionProblem> &problems)
{
    QStringList lines;
    for (const ConditionProblem &problem : problems)
        lines.append(problem.describe());
    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 条目属性
// -----------------------------------------------------------------------------

QVector<EntryAttribute> allEntryAttributes()
{
    return {EntryAttribute::ReadOnly, EntryAttribute::Hidden,
            EntryAttribute::System, EntryAttribute::Archive};
}

const char *entryAttributeIdentifier(EntryAttribute attribute)
{
    switch (attribute) {
    case EntryAttribute::ReadOnly:
        return "readonly";
    case EntryAttribute::Hidden:
        return "hidden";
    case EntryAttribute::System:
        return "system";
    case EntryAttribute::Archive:
        return "archive";
    }
    return "unknown";
}

QString entryAttributeLabel(EntryAttribute attribute)
{
    switch (attribute) {
    case EntryAttribute::ReadOnly:
        return QStringLiteral("只读");
    case EntryAttribute::Hidden:
        return QStringLiteral("隐藏");
    case EntryAttribute::System:
        return QStringLiteral("系统");
    case EntryAttribute::Archive:
        return QStringLiteral("归档");
    }
    return QString();
}

quint8 entryAttributeBit(EntryAttribute attribute)
{
    return static_cast<quint8>(1u << static_cast<unsigned>(attribute));
}

bool parseEntryAttribute(const QString &text, EntryAttribute *attribute)
{
    const QString key = text.trimmed().toLower();
    if (key.isEmpty())
        return false;

    for (EntryAttribute candidate : allEntryAttributes()) {
        const QString identifier = QString::fromLatin1(entryAttributeIdentifier(candidate));
        // 中文标签也认：声明文件是给用户看的，要求他先把「只读」翻译成
        // `readonly` 再写进去，只会让人放弃用声明文件。
        const bool alias = candidate == EntryAttribute::ReadOnly
                           && (key == QLatin1String("read-only") || key == QLatin1String("ro"));
        if (key == identifier || key == entryAttributeLabel(candidate) || alias) {
            if (attribute)
                *attribute = candidate;
            return true;
        }
    }
    return false;
}

bool EntryMetadata::knowsAttribute(EntryAttribute attribute) const
{
    return (knownAttributeBits & entryAttributeBit(attribute)) != 0;
}

bool EntryMetadata::hasAttribute(EntryAttribute attribute) const
{
    return (attributeBits & entryAttributeBit(attribute)) != 0;
}

EntryMetadata EntryMetadata::forName(const QString &name)
{
    EntryMetadata metadata;
    metadata.subject = MaskSubject::forName(name);
    return metadata;
}

EntryMetadata EntryMetadata::forPath(const QString &path)
{
    EntryMetadata metadata;
    metadata.subject = MaskSubject::forPath(path);
    return metadata;
}

EntryMetadata &EntryMetadata::withName(const QString &name)
{
    subject = MaskSubject::forName(name);
    return *this;
}

EntryMetadata &EntryMetadata::withSubject(const MaskSubject &value)
{
    subject = value;
    return *this;
}

EntryMetadata &EntryMetadata::withSize(quint64 bytes)
{
    hasSize = true;
    size = bytes;
    return *this;
}

EntryMetadata &EntryMetadata::withLastModified(const QDateTime &value)
{
    hasLastModified = true;
    lastModified = value;
    return *this;
}

EntryMetadata &EntryMetadata::withOwner(const QString &value)
{
    hasOwner = true;
    owner = value;
    return *this;
}

EntryMetadata &EntryMetadata::withGroup(const QString &value)
{
    hasGroup = true;
    group = value;
    return *this;
}

EntryMetadata &EntryMetadata::withAttribute(EntryAttribute attribute, bool set)
{
    knownAttributeBits |= entryAttributeBit(attribute);
    if (set)
        attributeBits |= entryAttributeBit(attribute);
    else
        attributeBits = static_cast<quint8>(attributeBits & ~entryAttributeBit(attribute));
    return *this;
}

EntryMetadata &EntryMetadata::withoutAttribute(EntryAttribute attribute)
{
    return withAttribute(attribute, false);
}

QString EntryMetadata::describe() const
{
    QStringList parts;
    parts.append(QStringLiteral("名字=%1").arg(subject.name));

    if (hasSize)
        parts.append(QStringLiteral("大小=%1").arg(sizeAndExact(size)));
    else
        parts.append(QStringLiteral("大小=未知"));

    if (hasLastModified && lastModified.isValid())
        parts.append(QStringLiteral("修改时间=%1").arg(formatMoment(lastModified)));
    else
        parts.append(QStringLiteral("修改时间=未知"));

    // 属性位要同时说「置位了哪些」与「哪些可信」——只说前者的话，
    // 「一个可信属性都没有」与「一个属性都没置位」看起来完全一样。
    QStringList setBits;
    QStringList knownBits;
    for (EntryAttribute attribute : allEntryAttributes()) {
        if (hasAttribute(attribute))
            setBits.append(entryAttributeLabel(attribute));
        if (knowsAttribute(attribute))
            knownBits.append(entryAttributeLabel(attribute));
    }
    parts.append(QStringLiteral("属性位=%1（可信：%2）")
                     .arg(setBits.isEmpty() ? QStringLiteral("无") : setBits.join(QStringLiteral("、")),
                          knownBits.isEmpty() ? QStringLiteral("无")
                                              : knownBits.join(QStringLiteral("、"))));

    parts.append(QStringLiteral("所有者=%1")
                     .arg(hasOwner ? owner : QStringLiteral("未知")));
    parts.append(QStringLiteral("组=%1").arg(hasGroup ? group : QStringLiteral("未知")));

    return parts.join(QStringLiteral("；"));
}

// -----------------------------------------------------------------------------
// 判定结果
// -----------------------------------------------------------------------------

QString ConditionOutcome::describe() const
{
    QString state;
    if (!accepted)
        state = QStringLiteral("未通过");
    else if (!evaluated)
        state = QStringLiteral("未生效");
    else
        state = QStringLiteral("通过");

    return reason.isEmpty() ? state : QStringLiteral("%1：%2").arg(state, reason);
}

const ConditionOutcome *AttributeDecision::outcomeFor(AttributeConditionKind kind) const
{
    const int index = attributeConditionIndex(kind);
    if (index < 0 || index >= outcomes.size())
        return nullptr;
    return &outcomes.at(index);
}

QString AttributeDecision::describe() const
{
    QStringList lines;
    lines.append(QStringLiteral("属性过滤：%1")
                     .arg(accepted ? QStringLiteral("通过") : QStringLiteral("未通过")));
    if (!accepted && !reason.isEmpty())
        lines.append(QStringLiteral("  起决定作用的是【%1】：%2")
                         .arg(attributeConditionLabel(deciding), reason));

    const QVector<AttributeConditionKind> kinds = allAttributeConditions();
    for (int index = 0; index < outcomes.size() && index < kinds.size(); ++index) {
        lines.append(QStringLiteral("  %1 —— %2")
                         .arg(attributeConditionLabel(kinds.at(index)),
                              outcomes.at(index).describe()));
    }

    if (!undecided.isEmpty()) {
        QStringList names;
        for (AttributeConditionKind kind : undecided)
            names.append(attributeConditionLabel(kind));
        lines.append(QStringLiteral("  未生效：%1（元数据缺失或条件没配置，均已放行）")
                         .arg(names.join(QStringLiteral("、"))));
    }

    for (const ConditionProblem &problem : problems)
        lines.append(QStringLiteral("  配置问题：%1").arg(problem.describe()));

    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 大小文本
// -----------------------------------------------------------------------------

QString SizeParseResult::describe() const
{
    if (ok)
        return formatSizeExactText(bytes);
    QString text = problem;
    if (!hint.isEmpty())
        text += QStringLiteral("（%1）").arg(hint);
    return text;
}

SizeParseResult parseSizeText(const QString &text)
{
    SizeParseResult result;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        result.problem = QStringLiteral("大小不能为空");
        return result;
    }

    // 单位换算固定 1024 进制，理由见头文件（撇开平台差异，同一份声明
    // 在 Windows 与 macOS 上必须解释成同一个字节数）。
    struct UnitRow
    {
        // 名字用 QString 而不是 `const char*`：单位表里有中文（`字节`），
        // 用 `QLatin1String(row.name)` 去比会把 UTF-8 字节按 Latin-1 解释，
        // 于是「0 字节」永远认不出来——而它恰恰是本模块自己产出的写法。
        QString name;
        quint64 multiplier;
    };
    // 单位换算固定 1024 进制，理由见头文件（撇开平台差异，同一份声明
    // 在 Windows 与 macOS 上必须解释成同一个字节数）。
    // `字节` 在里面是因为 `formatSizeText()` 产出的正是这个写法——显示出来的
    // 东西必须能被解析回来（往返由用例钉住），否则用户照着提示敲一遍反而报错。
    static const UnitRow units[] = {
        {QStringLiteral("b"), 1ULL},
        {QStringLiteral("byte"), 1ULL},
        {QStringLiteral("bytes"), 1ULL},
        {QStringLiteral("字节"), 1ULL},
        {QStringLiteral("k"), 1024ULL},
        {QStringLiteral("kb"), 1024ULL},
        {QStringLiteral("kib"), 1024ULL},
        {QStringLiteral("m"), 1024ULL * 1024},
        {QStringLiteral("mb"), 1024ULL * 1024},
        {QStringLiteral("mib"), 1024ULL * 1024},
        {QStringLiteral("g"), 1024ULL * 1024 * 1024},
        {QStringLiteral("gb"), 1024ULL * 1024 * 1024},
        {QStringLiteral("gib"), 1024ULL * 1024 * 1024},
        {QStringLiteral("t"), 1024ULL * 1024 * 1024 * 1024},
        {QStringLiteral("tb"), 1024ULL * 1024 * 1024 * 1024},
        {QStringLiteral("tib"), 1024ULL * 1024 * 1024 * 1024},
    };

    // 先单独把「负数」挑出来：不挑的话 `-5 MB` 会掉进「无法识别」那条提示，
    // 而用户真正需要知道的是「大小不能是负数」。
    static const QRegularExpression negative(QStringLiteral(R"(^\s*-\s*[0-9])"));
    if (negative.match(trimmed).hasMatch()) {
        result.problem = QStringLiteral("大小不能是负数");
        result.hint = QStringLiteral("大小指的是字节数，请去掉负号");
        return result;
    }

    // 单位这一组用 `\S*`（任意非空白）而不是 `[A-Za-z]*`：中文单位（`字节`）
    // 也要能认，否则 `formatSizeText()` 的产出自己解析不了。认不出的单位
    // 由下面的表判定并给出提示。
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*(\S*)\s*$)"));
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch()) {
        result.problem = QStringLiteral("无法识别的大小「%1」").arg(trimmed);
        result.hint = QStringLiteral("写成数字加单位，如 10 MB、1.5 GB、4096（可用单位：B、KB、MB、GB、TB）");
        return result;
    }

    bool numberOk = false;
    const double number = match.captured(1).toDouble(&numberOk);
    if (!numberOk) {
        result.problem = QStringLiteral("无法识别的大小「%1」").arg(trimmed);
        result.hint = QStringLiteral("数值部分只接受数字与小数点");
        return result;
    }

    const QString unitText = match.captured(2).toLower();
    quint64 multiplier = 1;
    if (!unitText.isEmpty()) {
        bool found = false;
        for (const UnitRow &row : units) {
            if (unitText == row.name) {
                multiplier = row.multiplier;
                found = true;
                break;
            }
        }
        if (!found) {
            result.problem = QStringLiteral("无法识别的单位「%1」").arg(match.captured(2));
            result.hint = QStringLiteral("可用单位：B、KB、MB、GB、TB（按 1024 进制）");
            return result;
        }
    }

    // 溢出必须在乘法之前判：乘完再判的话，double 到 quint64 的转换已经
    // 是未定义行为，而现象是「填了一个很大的数字，过滤结果毫无规律」。
    constexpr double kMaxSize = 1.8e19;
    if (number > kMaxSize / static_cast<double>(multiplier)) {
        result.problem = QStringLiteral("大小超出可表示范围");
        result.hint = QStringLiteral("最大约 16 TB");
        return result;
    }

    result.bytes = static_cast<quint64>(qRound64(number * static_cast<double>(multiplier)));
    result.ok = true;
    return result;
}

QString formatSizeText(quint64 bytes)
{
    static const struct
    {
        quint64 unit;
        const char *suffix;
    } rows[] = {
        {1024ULL * 1024 * 1024 * 1024, "TB"},
        {1024ULL * 1024 * 1024, "GB"},
        {1024ULL * 1024, "MB"},
        {1024ULL, "KB"},
    };

    for (const auto &row : rows) {
        if (bytes >= row.unit) {
            double value = static_cast<double>(bytes) / static_cast<double>(row.unit);
            QString text = QString::number(value, 'f', 2);
            // 去掉无意义的尾随 0：`1.50 KB` 与 `1.5 KB` 是同一个数，
            // 但前者看起来像程序在凑精度。
            while (text.endsWith(QLatin1Char('0')))
                text.chop(1);
            if (text.endsWith(QLatin1Char('.')))
                text.chop(1);
            return text + QLatin1String(" ") + QLatin1String(row.suffix);
        }
    }

    // 1 KB 以下直接给字节数：`0.98 KB` 对用户没有任何帮助。
    return QStringLiteral("%1 字节").arg(bytes);
}

QString formatSizeExactText(quint64 bytes)
{
    const QString readable = formatSizeText(bytes);
    if (readable.endsWith(QStringLiteral("字节")))
        return readable;
    return QStringLiteral("%1（%2 字节）").arg(readable).arg(bytes);
}

// -----------------------------------------------------------------------------
// 大小条件
// -----------------------------------------------------------------------------

void SizeCondition::setRangeText(const QString &minText, const QString &maxText)
{
    m_minText = minText;
    m_maxText = maxText;
    reparse();
}

void SizeCondition::reparse()
{
    m_problems.clear();
    m_hasMin = false;
    m_hasMax = false;
    m_min = 0;
    m_max = 0;

    // 上下界各自独立解析：一界写错不该牵连另一界（顶部约定第 2 条）。
    if (!m_minText.trimmed().isEmpty()) {
        const SizeParseResult parsed = parseSizeText(m_minText);
        if (parsed.ok) {
            m_hasMin = true;
            m_min = parsed.bytes;
        } else {
            ConditionProblem problem;
            problem.condition = AttributeConditionKind::Size;
            problem.field = ConditionField::SizeMin;
            problem.message = parsed.problem;
            problem.hint = parsed.hint;
            m_problems.append(problem);
        }
    }

    if (!m_maxText.trimmed().isEmpty()) {
        const SizeParseResult parsed = parseSizeText(m_maxText);
        if (parsed.ok) {
            m_hasMax = true;
            m_max = parsed.bytes;
        } else {
            ConditionProblem problem;
            problem.condition = AttributeConditionKind::Size;
            problem.field = ConditionField::SizeMax;
            problem.message = parsed.problem;
            problem.hint = parsed.hint;
            m_problems.append(problem);
        }
    }

    if (m_hasMin && m_hasMax && m_min > m_max) {
        ConditionProblem problem;
        problem.condition = AttributeConditionKind::Size;
        problem.field = ConditionField::SizeMax;
        problem.message = QStringLiteral("下限 %1 大于上限 %2")
                              .arg(formatSizeText(m_min), formatSizeText(m_max));
        problem.hint = QStringLiteral("把两者对调，或清空其中一个");
        m_problems.append(problem);

        // 自相矛盾时**两个界一起失效**：只留其中一个的话，留下的是哪一个取决于
        // 用户先填了哪一栏，而结果是一个「看起来生效了的」过滤器。
        // 一起失效则表现为「这一项没有参与过滤」，配上这条问题就说得清了。
        m_hasMin = false;
        m_hasMax = false;
        m_min = 0;
        m_max = 0;
    }
}

bool SizeCondition::isActive() const
{
    if (!m_enabled)
        return false;

    // 判据是「有没有可用的界」，**不是**「有没有问题」：一界写错不牵连另一界，
    // 与「一行写错只丢那一行」（MaskFilter::parse）是同一条纪律。
    // 自相矛盾的情形已经在 reparse() 里把两个界都清掉了，所以走不到这里。
    return m_hasMin || m_hasMax;
}

ConditionOutcome SizeCondition::accepts(quint64 sizeValue, bool sizeKnown) const
{
    if (!isActive()) {
        if (!m_problems.isEmpty())
            return undecidedOutcome(QStringLiteral("大小条件有配置问题，未参与判定"));
        return inactiveOutcome(QStringLiteral("大小"));
    }

    if (!sizeKnown) {
        // 顶部约定第 1 条：判不出来就放行，并如实说明。
        return undecidedOutcome(QStringLiteral("大小未知，已放行"));
    }

    if (m_hasMin && sizeValue < m_min) {
        return rejectedOutcome(QStringLiteral("大小 %1 小于下限 %2")
                                   .arg(sizeAndExact(sizeValue), formatSizeText(m_min)));
    }
    if (m_hasMax && sizeValue > m_max) {
        return rejectedOutcome(QStringLiteral("大小 %1 大于上限 %2")
                                   .arg(sizeAndExact(sizeValue), formatSizeText(m_max)));
    }

    return passedOutcome(QStringLiteral("大小 %1 在 %2 内")
                             .arg(sizeAndExact(sizeValue), describeRange()));
}

QString SizeCondition::describeRange() const
{
    if (m_hasMin && m_hasMax)
        return QStringLiteral("%1 ～ %2").arg(formatSizeText(m_min), formatSizeText(m_max));
    if (m_hasMin)
        return QStringLiteral("不小于 %1").arg(formatSizeText(m_min));
    if (m_hasMax)
        return QStringLiteral("不大于 %1").arg(formatSizeText(m_max));
    return QString();
}

QString SizeCondition::describe() const
{
    QString text = QStringLiteral("大小：%1")
                       .arg(describeRange().isEmpty() ? QStringLiteral("（未设置）") : describeRange());
    if (!m_problems.isEmpty())
        text += QStringLiteral("，%1 项问题").arg(m_problems.size());
    return text;
}

// -----------------------------------------------------------------------------
// 修改时间
// -----------------------------------------------------------------------------

const char *timeRangeKindIdentifier(TimeRangeKind kind)
{
    return kind == TimeRangeKind::Relative ? "relative" : "absolute";
}

QString timeRangeKindLabel(TimeRangeKind kind)
{
    return kind == TimeRangeKind::Relative ? QStringLiteral("相对时间") : QStringLiteral("绝对区间");
}

QString DateTimeParseResult::describe() const
{
    if (ok)
        return formatMoment(value);
    QString text = problem;
    if (!hint.isEmpty())
        text += QStringLiteral("（%1）").arg(hint);
    return text;
}

DateTimeParseResult parseDateTimeText(const QString &text)
{
    DateTimeParseResult result;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        result.problem = QStringLiteral("时间不能为空");
        return result;
    }

    // 中文写法先归一成 `2026-09-01`：用户在本产品的中文界面里敲日期时
    // 写「2026年9月1日」是自然的，为这一个写法让所有人去学 ISO 格式不值得。
    QString normalized = trimmed;
    normalized.replace(QStringLiteral("年"), QStringLiteral("-"));
    normalized.replace(QStringLiteral("月"), QStringLiteral("-"));
    normalized.replace(QStringLiteral("日"), QStringLiteral(" "));
    normalized = normalized.simplified();
    // 「2026-9-1 」这种归一后带尾空格的形态在上面已经 simplified 掉了；
    // 结尾多余的 `-`（`2026-9-1-`）会被下面的格式串直接判为不匹配。
    while (normalized.endsWith(QLatin1Char('-')))
        normalized.chop(1);

    static const QStringList timeFormats = {
        QStringLiteral("yyyy-M-d HH:mm:ss"),
        QStringLiteral("yyyy-M-d HH:mm"),
        QStringLiteral("yyyy/M/d HH:mm:ss"),
        QStringLiteral("yyyy/M/d HH:mm"),
        QStringLiteral("yyyy-M-dTHH:mm:ss"),
        QStringLiteral("yyyy-M-dTHH:mm"),
    };
    static const QStringList dateFormats = {
        QStringLiteral("yyyy-M-d"),
        QStringLiteral("yyyy/M/d"),
    };

    // 带钟点的格式先试：`2026-09-01 18:30` 不能被当成本日零点。
    for (const QString &format : timeFormats) {
        const QDateTime value = QDateTime::fromString(normalized, format);
        if (value.isValid()) {
            result.value = value;
            result.ok = true;
            return result;
        }
    }

    for (const QString &format : dateFormats) {
        const QDateTime value = QDateTime::fromString(normalized, format);
        if (value.isValid()) {
            result.value = value;
            result.ok = true;
            // 「只有日期」这件事要留给调用方判断：它决定上限的含义
            // （见 DateTimeParseResult::dateOnly 与 TimeCondition）。
            result.dateOnly = true;
            return result;
        }
    }

    result.problem = QStringLiteral("无法识别的时间「%1」").arg(trimmed);
    result.hint = QStringLiteral("可用格式：2026-09-01、2026-09-01 18:30、2026/09/01、2026年9月1日");
    return result;
}

QString RelativeDaysParseResult::describe() const
{
    if (ok)
        return QStringLiteral("最近 %1 天").arg(days);
    QString text = problem;
    if (!hint.isEmpty())
        text += QStringLiteral("（%1）").arg(hint);
    return text;
}

RelativeDaysParseResult parseRelativeDaysText(const QString &text)
{
    RelativeDaysParseResult result;
    QString key = text.trimmed();
    if (key.isEmpty()) {
        result.problem = QStringLiteral("相对时间不能为空");
        return result;
    }

    // 前缀「最近 / 近 / 过去」与后缀单位都可以有可以没有：用户会写
    // 「7」「7天」「最近 7 天」，三种都是同一个意思，为其中一种报错
    // 只会让人以为是自己写错了。
    static const QStringList prefixes = {QStringLiteral("最近"), QStringLiteral("过去"),
                                         QStringLiteral("近")};
    for (const QString &prefix : prefixes) {
        if (key.startsWith(prefix)) {
            key = key.mid(prefix.size()).trimmed();
            break;
        }
    }

    // 后缀用**显式列举**而不是正则：正则找的是最左匹配，`7days` 会在位置 0
    // 命中「days」从而把数字部分切空，变成「无法识别」——而它是能认的。
    // 列举的顺次有讲究：长的在前（`天内` 先于 `天`，`days` 先于 `day` 先于 `d`）。
    static const QStringList suffixes = {QStringLiteral("天内"), QStringLiteral("日内"),
                                         QStringLiteral("天"), QStringLiteral("日"),
                                         QStringLiteral("days"), QStringLiteral("day"),
                                         QStringLiteral("d")};
    for (const QString &suffix : suffixes) {
        if (key.endsWith(suffix, Qt::CaseInsensitive)) {
            key.chop(suffix.size());
            key = key.trimmed();
            break;
        }
    }

    bool ok = false;
    const int days = key.toInt(&ok);
    if (!ok) {
        result.problem = QStringLiteral("无法识别的相对时间「%1」").arg(text.trimmed());
        result.hint = QStringLiteral("写成天数，如 7、7天、最近 7 天");
        return result;
    }

    if (days < 1) {
        result.problem = QStringLiteral("相对天数必须大于 0");
        result.hint = QStringLiteral("「最近 0 天」没有意义，请填 1 以上的整数");
        return result;
    }

    result.days = days;
    result.ok = true;
    return result;
}

void TimeCondition::setKind(TimeRangeKind kindValue)
{
    m_kind = kindValue;
    // 只报当前这一类的输入问题：另一类留在表单里的内容与当前选择无关，
    // 把它的错误一起标红会让用户在一个根本没在用的框上找原因。
    m_problems.clear();
    if (m_kind == TimeRangeKind::Absolute)
        reparseAbsolute();
    else
        reparseRelative();
}

void TimeCondition::setAbsoluteText(const QString &fromText, const QString &toText)
{
    m_fromText = fromText;
    m_toText = toText;
    reparseAbsolute();
}

void TimeCondition::setRelativeText(const QString &text)
{
    m_relativeText = text;
    reparseRelative();
}

void TimeCondition::setRelativeDays(int days)
{
    // 整数入口是给界面上的数字框用的，那里 `0` 的自然含义是「没填」而不是
    // 「填错了」——为它报一条配置问题会让面板一打开就带着一条红字。
    // 文本入口（声明文件）里的 `0` 则是明确的笔误，如实报错。
    if (days <= 0) {
        m_relativeText.clear();
        m_days = 0;
        reparseRelative();
        return;
    }
    m_relativeText = QString::number(days);
    reparseRelative();
}

void TimeCondition::reparseAbsolute()
{
    m_from = QDateTime();
    m_to = QDateTime();
    m_problems.clear();

    if (!m_fromText.trimmed().isEmpty()) {
        const DateTimeParseResult parsed = parseDateTimeText(m_fromText);
        if (parsed.ok) {
            m_from = parsed.value;
        } else {
            ConditionProblem problem;
            problem.condition = AttributeConditionKind::TimeRange;
            problem.field = ConditionField::TimeFrom;
            problem.message = parsed.problem;
            problem.hint = parsed.hint;
            m_problems.append(problem);
        }
    }

    if (!m_toText.trimmed().isEmpty()) {
        const DateTimeParseResult parsed = parseDateTimeText(m_toText);
        if (parsed.ok) {
            ///
            /// 只填了日期的**上限**要算到当天最后一刻。
            ///
            /// 用户填「2026-09-10」当结束时间时，他要的是「含 09-10 这一整天」。
            /// 直接取当天 00:00 的话，09-10 当天修改过的文件一个都不会出现，
            /// 而用户填的日期看起来完全正确——这类「填对了却少了一批文件」的
            /// 现象最难自查。下限不受影响：`2026-09-10` 作为起点就是当天 00:00。
            ///
            /// 直接用同一天的 23:59:59 构造而不是 `addSecs(86399)`：
            /// 后者在夏令时切换的那一天会跨错一个小时。
            ///
            m_to = parsed.dateOnly ? QDateTime(parsed.value.date(), QTime(23, 59, 59))
                                   : parsed.value;
        } else {
            ConditionProblem problem;
            problem.condition = AttributeConditionKind::TimeRange;
            problem.field = ConditionField::TimeTo;
            problem.message = parsed.problem;
            problem.hint = parsed.hint;
            m_problems.append(problem);
        }
    }

    if (m_from.isValid() && m_to.isValid() && m_from > m_to) {
        ConditionProblem problem;
        problem.condition = AttributeConditionKind::TimeRange;
        problem.field = ConditionField::TimeTo;
        problem.message = QStringLiteral("起始时间 %1 晚于结束时间 %2")
                              .arg(formatMoment(m_from), formatMoment(m_to));
        problem.hint = QStringLiteral("把两者对调，或清空其中一个");
        m_problems.append(problem);

        // 与大小条件同一处理：矛盾时两个端点一起失效（只留一个的话，
        // 留下的是哪一个取决于先填了哪一栏，而结果看起来是生效的）。
        m_from = QDateTime();
        m_to = QDateTime();
    }
}

void TimeCondition::reparseRelative()
{
    m_days = 0;
    m_problems.clear();

    if (m_relativeText.trimmed().isEmpty())
        return;

    const RelativeDaysParseResult parsed = parseRelativeDaysText(m_relativeText);
    if (parsed.ok) {
        m_days = parsed.days;
        return;
    }

    ConditionProblem problem;
    problem.condition = AttributeConditionKind::TimeRange;
    problem.field = ConditionField::TimeRelative;
    problem.message = parsed.problem;
    problem.hint = parsed.hint;
    m_problems.append(problem);
}

bool TimeCondition::hasLowerBound() const
{
    return m_kind == TimeRangeKind::Absolute ? m_from.isValid() : relativeDays() > 0 && m_reference.isValid();
}

bool TimeCondition::hasUpperBound() const
{
    return m_kind == TimeRangeKind::Absolute ? m_to.isValid() : relativeDays() > 0 && m_reference.isValid();
}

QDateTime TimeCondition::lowerBound() const
{
    if (m_kind == TimeRangeKind::Absolute)
        return m_from;
    if (m_days <= 0 || !m_reference.isValid())
        return QDateTime();
    return m_reference.addDays(-m_days);
}

QDateTime TimeCondition::upperBound() const
{
    if (m_kind == TimeRangeKind::Absolute)
        return m_to;
    if (m_days <= 0 || !m_reference.isValid())
        return QDateTime();
    return m_reference;
}

bool TimeCondition::isConstrained() const
{
    return m_kind == TimeRangeKind::Absolute
               ? (!m_fromText.trimmed().isEmpty() || !m_toText.trimmed().isEmpty())
               : !m_relativeText.trimmed().isEmpty();
}

bool TimeCondition::isActive() const
{
    if (!m_enabled)
        return false;
    if (m_kind == TimeRangeKind::Absolute) {
        // 同大小条件：一个端点写错不牵连另一个端点（自相矛盾时两个端点
        // 已经在 reparseAbsolute() 里一起清掉了）。
        return m_from.isValid() || m_to.isValid();
    }
    if (m_days <= 0)
        return false;
    // 相对区间没有「现在」就算不出来。这一条**不算**配置问题（用户没写错，
    // 是调用方还没把参考时刻给进来），所以只在判定里体现为「未生效」。
    return m_reference.isValid();
}

QVector<ConditionProblem> TimeCondition::problems() const
{
    return m_problems;
}

ConditionOutcome TimeCondition::accepts(const QDateTime &lastModified, bool known) const
{
    if (!isEnabled())
        return inactiveOutcome(QStringLiteral("修改时间"));

    // 「没有参考时刻」与「条件没填」是两件事，区别开写：
    // 前者是调用方的疏漏，报出来能立刻定位；后者是用户还没设置。
    if (m_kind == TimeRangeKind::Relative && m_days > 0 && !m_reference.isValid()) {
        return undecidedOutcome(
            QStringLiteral("相对时间需要「当前时刻」，调用方还没有提供，已放行"));
    }

    if (!isActive()) {
        if (!m_problems.isEmpty())
            return undecidedOutcome(QStringLiteral("修改时间条件有配置问题，未参与判定"));
        return inactiveOutcome(QStringLiteral("修改时间"));
    }

    if (!known || !lastModified.isValid())
        return undecidedOutcome(QStringLiteral("修改时间未知，已放行"));

    const QDateTime from = lowerBound();
    const QDateTime to = upperBound();

    // 相对区间的上界是「现在」，因此时间戳在未来的条目被排除（见头文件）。
    if (m_kind == TimeRangeKind::Relative) {
        if (lastModified < from) {
            return rejectedOutcome(QStringLiteral("修改时间 %1 早于最近 %2 天的起点 %3")
                                       .arg(formatMoment(lastModified))
                                       .arg(m_days)
                                       .arg(formatMoment(from)));
        }
        if (lastModified > to) {
            return rejectedOutcome(QStringLiteral("修改时间 %1 晚于当前时刻 %2（相对区间不含未来）")
                                       .arg(formatMoment(lastModified), formatMoment(to)));
        }
        return passedOutcome(QStringLiteral("修改时间 %1 在最近 %2 天内（%3 起）")
                                 .arg(formatMoment(lastModified))
                                 .arg(m_days)
                                 .arg(formatMoment(from)));
    }

    if (from.isValid() && lastModified < from) {
        return rejectedOutcome(QStringLiteral("修改时间 %1 早于起始时间 %2")
                                   .arg(formatMoment(lastModified), formatMoment(from)));
    }
    if (to.isValid() && lastModified > to) {
        return rejectedOutcome(QStringLiteral("修改时间 %1 晚于结束时间 %2")
                                   .arg(formatMoment(lastModified), formatMoment(to)));
    }

    return passedOutcome(QStringLiteral("修改时间 %1 在 %2 内")
                             .arg(formatMoment(lastModified), describeWindow()));
}

QString TimeCondition::describeWindow() const
{
    if (m_kind == TimeRangeKind::Relative) {
        if (m_days <= 0)
            return QString();
        if (!m_reference.isValid())
            return QStringLiteral("最近 %1 天（尚未提供当前时刻）").arg(m_days);
        return QStringLiteral("最近 %1 天：%2 ～ %3")
            .arg(m_days)
            .arg(formatMoment(lowerBound()), formatMoment(upperBound()));
    }

    if (m_from.isValid() && m_to.isValid())
        return QStringLiteral("%1 ～ %2").arg(formatMoment(m_from), formatMoment(m_to));
    if (m_from.isValid())
        return QStringLiteral("不早于 %1").arg(formatMoment(m_from));
    if (m_to.isValid())
        return QStringLiteral("不晚于 %1").arg(formatMoment(m_to));
    return QString();
}

QString TimeCondition::describe() const
{
    QString text = QStringLiteral("修改时间（%1）：%2")
                       .arg(timeRangeKindLabel(m_kind),
                            describeWindow().isEmpty() ? QStringLiteral("（未设置）")
                                                       : describeWindow());
    if (!m_problems.isEmpty())
        text += QStringLiteral("，%1 项问题").arg(m_problems.size());
    return text;
}

// -----------------------------------------------------------------------------
// 属性位
// -----------------------------------------------------------------------------

const char *requirementIdentifier(Requirement requirement)
{
    switch (requirement) {
    case Requirement::Ignore:
        return "ignore";
    case Requirement::Required:
        return "required";
    case Requirement::Forbidden:
        return "forbidden";
    }
    return "unknown";
}

QString requirementLabel(Requirement requirement)
{
    switch (requirement) {
    case Requirement::Ignore:
        return QStringLiteral("不限制");
    case Requirement::Required:
        return QStringLiteral("必须置位");
    case Requirement::Forbidden:
        return QStringLiteral("必须未置位");
    }
    return QString();
}

Requirement AttributeBitsCondition::requirement(EntryAttribute attribute) const
{
    return m_requirements[static_cast<int>(attribute)];
}

void AttributeBitsCondition::setRequirement(EntryAttribute attribute, Requirement requirement)
{
    m_requirements[static_cast<int>(attribute)] = requirement;
}

QVector<EntryAttribute> AttributeBitsCondition::requiredAttributes() const
{
    QVector<EntryAttribute> result;
    for (EntryAttribute attribute : allEntryAttributes()) {
        if (requirement(attribute) == Requirement::Required)
            result.append(attribute);
    }
    return result;
}

QVector<EntryAttribute> AttributeBitsCondition::forbiddenAttributes() const
{
    QVector<EntryAttribute> result;
    for (EntryAttribute attribute : allEntryAttributes()) {
        if (requirement(attribute) == Requirement::Forbidden)
            result.append(attribute);
    }
    return result;
}

bool AttributeBitsCondition::isConstrained() const
{
    for (EntryAttribute attribute : allEntryAttributes()) {
        if (requirement(attribute) != Requirement::Ignore)
            return true;
    }
    return false;
}

bool AttributeBitsCondition::isActive() const
{
    return m_enabled && isConstrained();
}

ConditionOutcome AttributeBitsCondition::accepts(const EntryMetadata &entry) const
{
    if (!isActive())
        return inactiveOutcome(QStringLiteral("属性位"));

    QStringList unknown;
    QStringList mismatched;
    QStringList satisfied;

    for (EntryAttribute attribute : allEntryAttributes()) {
        const Requirement wanted = requirement(attribute);
        if (wanted == Requirement::Ignore)
            continue;

        if (!entry.knowsAttribute(attribute)) {
            // 顶部约定第 1 条。这一条在 Unix 上几乎必然命中：系统位与归档位
            // 在那边根本没有对应的概念，若按「未置位」处理，
            // 用户在 macOS 上勾一下「归档」就会得到空列表。
            unknown.append(entryAttributeLabel(attribute));
            continue;
        }

        const bool set = entry.hasAttribute(attribute);
        if (wanted == Requirement::Required && !set)
            mismatched.append(QStringLiteral("%1 要求置位，实际未置位").arg(entryAttributeLabel(attribute)));
        else if (wanted == Requirement::Forbidden && set)
            mismatched.append(QStringLiteral("%1 要求未置位，实际已置位").arg(entryAttributeLabel(attribute)));
        else
            satisfied.append(QStringLiteral("%1=%2")
                                 .arg(entryAttributeLabel(attribute),
                                      set ? QStringLiteral("置位") : QStringLiteral("未置位")));
    }

    if (!mismatched.isEmpty())
        return rejectedOutcome(mismatched.first());

    if (!unknown.isEmpty()) {
        return undecidedOutcome(QStringLiteral("该条目看不到这些属性位：%1，已放行")
                                    .arg(unknown.join(QStringLiteral("、"))));
    }

    return passedOutcome(QStringLiteral("属性位符合：%1").arg(satisfied.join(QStringLiteral("、"))));
}

QString AttributeBitsCondition::describe() const
{
    QStringList parts;
    for (EntryAttribute attribute : allEntryAttributes()) {
        const Requirement wanted = requirement(attribute);
        if (wanted == Requirement::Ignore)
            continue;
        parts.append(QStringLiteral("%1=%2")
                         .arg(entryAttributeLabel(attribute),
                              wanted == Requirement::Required ? QStringLiteral("置位")
                                                              : QStringLiteral("未置位")));
    }
    if (parts.isEmpty())
        return QStringLiteral("属性位：（未设置）");
    return QStringLiteral("属性位：%1").arg(parts.join(QStringLiteral("、")));
}

// -----------------------------------------------------------------------------
// 所有者 / 组
// -----------------------------------------------------------------------------

const char *listMatchModeIdentifier(ListMatchMode mode)
{
    return mode == ListMatchMode::NoneOf ? "none-of" : "any-of";
}

QString listMatchModeLabel(ListMatchMode mode)
{
    return mode == ListMatchMode::NoneOf ? QStringLiteral("排除名单内的") : QStringLiteral("只看名单内的");
}

QStringList splitNameListText(const QString &text)
{
    QStringList names;
    // 三种分隔符都认（逗号、分号、空白）：用户从表格里粘一列名字过来时
    // 分隔符是逗号，从聊天窗口粘过来时是换行或空格，让他手工改一遍没有意义。
    static const QRegularExpression separators(QStringLiteral(R"([,;，；\s]+)"));
    const QStringList pieces = text.split(separators, Qt::SkipEmptyParts);
    for (const QString &piece : pieces) {
        const QString trimmed = piece.trimmed();
        if (!trimmed.isEmpty())
            names.append(trimmed);
    }
    return names;
}

ConditionOutcome OwnerCondition::accepts(const EntryMetadata &entry) const
{
    if (!isActive())
        return inactiveOutcome(QStringLiteral("所有者"));

    QStringList satisfied;

    if (!m_owners.isEmpty()) {
        if (!entry.hasOwner) {
            // 顶部约定第 1 条。`NoneOf` 也一样：要断言「不在名单里」，
            // 先得知道它是谁。
            return undecidedOutcome(QStringLiteral("条目的所有者未知，已放行"));
        }
        const bool hit = matchesAnyInList(entry.owner, m_owners, m_platform);
        const QString list = m_owners.join(QStringLiteral("、"));
        if (m_ownerMode == ListMatchMode::AnyOf) {
            if (!hit) {
                return rejectedOutcome(QStringLiteral("所有者「%1」不在名单内（%2）")
                                           .arg(entry.owner, list));
            }
        } else if (hit) {
            return rejectedOutcome(QStringLiteral("所有者「%1」在排除名单内（%2）")
                                       .arg(entry.owner, list));
        }
        satisfied.append(QStringLiteral("所有者=%1").arg(entry.owner));
    }

    if (!m_groups.isEmpty()) {
        if (!entry.hasGroup)
            return undecidedOutcome(QStringLiteral("条目的组未知，已放行"));

        const bool hit = matchesAnyInList(entry.group, m_groups, m_platform);
        const QString list = m_groups.join(QStringLiteral("、"));
        if (m_groupMode == ListMatchMode::AnyOf) {
            if (!hit) {
                return rejectedOutcome(QStringLiteral("组「%1」不在名单内（%2）")
                                           .arg(entry.group, list));
            }
        } else if (hit) {
            return rejectedOutcome(QStringLiteral("组「%1」在排除名单内（%2）")
                                       .arg(entry.group, list));
        }
        satisfied.append(QStringLiteral("组=%1").arg(entry.group));
    }

    if (satisfied.isEmpty())
        return inactiveOutcome(QStringLiteral("所有者"));

    return passedOutcome(satisfied.join(QStringLiteral("、")));
}

QString OwnerCondition::describe() const
{
    if (!isConstrained())
        return QStringLiteral("所有者与组：（未设置）");

    QStringList parts;
    if (!m_owners.isEmpty()) {
        parts.append(QStringLiteral("%1：%2")
                         .arg(listMatchModeLabel(m_ownerMode), m_owners.join(QStringLiteral("、"))));
    }
    if (!m_groups.isEmpty()) {
        parts.append(QStringLiteral("组 %1：%2")
                         .arg(listMatchModeLabel(m_groupMode), m_groups.join(QStringLiteral("、"))));
    }
    return QStringLiteral("所有者与组：%1").arg(parts.join(QStringLiteral("；")));
}

// -----------------------------------------------------------------------------
// 四类条件合起来
// -----------------------------------------------------------------------------

void AttributeFilter::setReferenceTime(const QDateTime &now)
{
    timeRange.setReferenceTime(now);
}

void AttributeFilter::setPlatform(MaskPlatform platform)
{
    owner.setPlatform(platform);
}

bool AttributeFilter::isActive() const
{
    return !activeConditions().isEmpty();
}

QVector<AttributeConditionKind> AttributeFilter::activeConditions() const
{
    QVector<AttributeConditionKind> result;
    // 顺序来自表，不在这里手写一遍：手写的话，往表里加一类条件就会出现
    // 「面板上有、明细里没有」这种只在部分界面出现的错位。
    for (AttributeConditionKind kind : allAttributeConditions()) {
        bool active = false;
        switch (kind) {
        case AttributeConditionKind::Size:
            active = size.isActive();
            break;
        case AttributeConditionKind::TimeRange:
            active = timeRange.isActive();
            break;
        case AttributeConditionKind::Attributes:
            active = attributeBits.isActive();
            break;
        case AttributeConditionKind::Owner:
            active = owner.isActive();
            break;
        }
        if (active)
            result.append(kind);
    }
    return result;
}

QVector<ConditionProblem> AttributeFilter::problems() const
{
    QVector<ConditionProblem> result;
    for (AttributeConditionKind kind : allAttributeConditions()) {
        switch (kind) {
        case AttributeConditionKind::Size:
            result += size.problems();
            break;
        case AttributeConditionKind::TimeRange:
            result += timeRange.problems();
            break;
        case AttributeConditionKind::Attributes:
            // 属性位与所有者这两条没有「填错」的可能：它们的输入是枚举与名单，
            // 界面上的取值都是合法的。声明文本里的错误在解析阶段就已经报出来了。
            break;
        case AttributeConditionKind::Owner:
            break;
        }
    }
    return result;
}

AttributeDecision AttributeFilter::decide(const EntryMetadata &entry) const
{
    AttributeDecision decision;
    decision.accepted = true;
    decision.problems = problems();

    // 生效清单只算一次、也只在一处算：`activeConditions()` 里的 switch 与
    // 判定循环里的 switch 各写一遍的话，往表里加一类条件就会出现
    // 「面板说这一条生效中、判定却根本没过问它」这种只在部分界面暴露的错位。
    const QVector<AttributeConditionKind> active = activeConditions();
    decision.anyConditionActive = !active.isEmpty();

    for (AttributeConditionKind kind : allAttributeConditions()) {
        ConditionOutcome outcome;

        switch (kind) {
        case AttributeConditionKind::Size:
            outcome = size.accepts(entry.size, entry.hasSize);
            break;
        case AttributeConditionKind::TimeRange:
            outcome = timeRange.accepts(entry.lastModified, entry.hasLastModified);
            break;
        case AttributeConditionKind::Attributes:
            outcome = attributeBits.accepts(entry);
            break;
        case AttributeConditionKind::Owner:
            outcome = owner.accepts(entry);
            break;
        }

        decision.outcomes.append(outcome);

        if (!outcome.accepted) {
            decision.blocking.append(kind);
            if (decision.blocking.size() == 1) {
                decision.deciding = kind;
                decision.decidingIdentifier = QString::fromLatin1(attributeConditionIdentifier(kind));
                decision.reason = outcome.reason;
            }
        } else if (!outcome.evaluated && active.contains(kind)) {
            // 只把**生效但判不了**的条件列进未生效清单。未启用/没内容的条件
            // 不该出现在这里——那会把面板变成一张「你什么都没配」的清单，
            // 用户反而看不出是哪一项在当前的平台上不起作用。
            decision.undecided.append(kind);
        }
    }

    decision.accepted = decision.blocking.isEmpty();
    return decision;
}

bool AttributeFilter::accepts(const EntryMetadata &entry) const
{
    return decide(entry).accepted;
}

QString AttributeFilter::describe() const
{
    QStringList parts;
    parts.append(size.describe());
    parts.append(timeRange.describe());
    parts.append(attributeBits.describe());
    parts.append(owner.describe());

    QString text = parts.join(QStringLiteral("；"));
    const QVector<AttributeConditionKind> active = activeConditions();
    if (active.isEmpty())
        text = QStringLiteral("属性过滤：未生效（%1）").arg(text);
    return text;
}

QString AttributeFilter::toDeclarationText() const
{
    QStringList lines;
    lines.append(QStringLiteral("# 属性过滤声明（FILT-003）"));
    lines.append(QStringLiteral("# 一行一条；`#` 开头是注释；`-` 前缀表示排除"));
    // 启用状态刻意不写进声明：与 FILT-005 把「每层是否启用」留给界面同一条理由——
    // 启用是**当前视图的状态**，写进可落盘的声明会让「暂时关掉一层」变成
    // 跟着会话走的东西，而用户在取消勾选时并不会预期这一点。

    if (!size.minText().trimmed().isEmpty())
        lines.append(QStringLiteral("size-min %1").arg(size.minText().trimmed()));
    if (!size.maxText().trimmed().isEmpty())
        lines.append(QStringLiteral("size-max %1").arg(size.maxText().trimmed()));

    if (timeRange.kind() == TimeRangeKind::Relative) {
        if (timeRange.relativeDays() > 0)
            lines.append(QStringLiteral("time-relative %1 天").arg(timeRange.relativeDays()));
    } else {
        if (!timeRange.fromText().trimmed().isEmpty())
            lines.append(QStringLiteral("time-from %1").arg(timeRange.fromText().trimmed()));
        if (!timeRange.toText().trimmed().isEmpty())
            lines.append(QStringLiteral("time-to %1").arg(timeRange.toText().trimmed()));
    }

    for (EntryAttribute attribute : attributeBits.requiredAttributes()) {
        lines.append(QStringLiteral("attr %1").arg(QString::fromLatin1(entryAttributeIdentifier(attribute))));
    }
    for (EntryAttribute attribute : attributeBits.forbiddenAttributes()) {
        lines.append(QStringLiteral("-attr %1").arg(QString::fromLatin1(entryAttributeIdentifier(attribute))));
    }

    if (!owner.owners().isEmpty()) {
        const QString prefix = owner.ownerMode() == ListMatchMode::NoneOf ? QStringLiteral("-owner")
                                                                         : QStringLiteral("owner");
        lines.append(QStringLiteral("%1 %2").arg(prefix, owner.owners().join(QStringLiteral(", "))));
    }
    if (!owner.groups().isEmpty()) {
        const QString prefix = owner.groupMode() == ListMatchMode::NoneOf ? QStringLiteral("-group")
                                                                         : QStringLiteral("group");
        lines.append(QStringLiteral("%1 %2").arg(prefix, owner.groups().join(QStringLiteral(", "))));
    }

    return lines.join(QLatin1Char('\n'));
}

QString attributeFilterDeclarationKey()
{
    return QStringLiteral("attribute-filter");
}

// -----------------------------------------------------------------------------
// 声明文本的解析
// -----------------------------------------------------------------------------

AttributeFilterParseResult AttributeFilter::parseDeclaration(const QString &text, MaskPlatform platform)
{
    AttributeFilterParseResult result;
    result.filter.setPlatform(platform);

    // 用与掩码声明同一个切行实现：两份实现迟早会在换行处理上分家，
    // 而分家的表现恰好是「同一个文件在掩码框里正常、在属性框里每行都报错」。
    const QStringList lines = splitDeclarationLines(text);

    // 记录每个**不可重复**的键第一次出现在哪一行。声明是累积的（`attr` 可以重复），
    // 因此对不可重复的键取「首次出现生效」：在文件末尾追加内容不会静默改变
    // 已有行为——若取后者生效，用户把一行往上挪一下就会得到另一种过滤结果，
    // 而文件里两条并存，谁也说不出哪条在起作用。
    QSet<QString> seenKeys;

    // 三族「正负二选一」的键各记一个「第一次出现的那个」，既用来查重复，
    // 也用来查语义冲突（见 claimFamilyKey）。
    QString claimedTimeKey;
    QString claimedOwnerKey;
    QString claimedGroupKey;

    for (int index = 0; index < lines.size(); ++index) {
        const QString raw = lines.at(index);
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int separator = [&line] {
            for (int i = 0; i < line.size(); ++i) {
                if (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t'))
                    return i;
            }
            return -1;
        }();

        const QString key = (separator < 0 ? line : line.left(separator)).trimmed();
        const QString value = separator < 0 ? QString() : line.mid(separator + 1).trimmed();
        const int valueColumn = separator < 0 ? -1 : separator + 1;

        auto addProblem = [&](ConditionField field, const QString &message, const QString &hint) {
            ConditionProblem problem;
            // 认不出这个键时 `hasCondition` 为假：这类问题是整行级别的，
            // 硬塞给某一类条件会让提示写成「大小 / 未知的键「sizemin」」——
            // 用户会去大小那一栏找一个根本不存在的毛病。
            AttributeConditionKind kind = AttributeConditionKind::Size;
            problem.hasCondition = attributeConditionKindForKey(key, &kind);
            problem.condition = kind;
            problem.key = key;
            problem.field = field;
            problem.line = index;
            problem.column = valueColumn;
            problem.message = message;
            problem.hint = hint;
            result.problems.append(problem);
        };

        const bool negated = key.startsWith(QLatin1Char('-'));
        const QString bare = negated ? key.mid(1) : key;

        ///
        /// \brief 「这一族键只认第一次出现」的记账，返回 false 表示这一行被拒。
        ///
        /// 三族键（时间 / 所有者 / 组）都是「正负两种写法二选一」：
        /// `time-relative` 与 `time-from` 是两种互斥的区间形态，
        /// `owner` 与 `-owner` 是互斥的语义（只看谁 / 不看谁）。
        /// 同时出现时取「首次出现生效」并报一条问题——取后者生效的话，
        /// 用户把一行往上挪一下就会得到另一种过滤结果，而文件里两条并存，
        /// 谁也说不出哪条在起作用。
        ///
        auto claimFamilyKey = [&](const QString &familyKey, QString *claimed,
                                  ConditionField field) -> bool {
            if (claimed->isEmpty()) {
                *claimed = familyKey;
                return true;
            }
            if (*claimed == familyKey) {
                addProblem(field, QStringLiteral("重复的键「%1」").arg(familyKey),
                           QStringLiteral("同一个键只认第一次出现的值，请删掉这一行"));
                return false;
            }
            addProblem(field, QStringLiteral("「%1」与「%2」不能同时出现").arg(*claimed, familyKey),
                       QStringLiteral("二者语义相反，只能留一个（首次出现的那一行生效）"));
            return false;
        };

        // --- 大小 -------------------------------------------------------------
        if (bare == QLatin1String("size-min") || bare == QLatin1String("size-max")) {
            if (value.isEmpty()) {
                addProblem(bare == QLatin1String("size-min") ? ConditionField::SizeMin : ConditionField::SizeMax,
                           QStringLiteral("缺少值"), QStringLiteral("如：%1 10 MB").arg(key));
                continue;
            }
            if (seenKeys.contains(key)) {
                addProblem(bare == QLatin1String("size-min") ? ConditionField::SizeMin : ConditionField::SizeMax,
                           QStringLiteral("重复的键「%1」").arg(key),
                           QStringLiteral("同一个键只认第一次出现的值，请删掉这一行"));
                continue;
            }
            seenKeys.insert(key);

            // 另一界保持原样：`size-min` 与 `size-max` 是两份独立的输入。
            if (bare == QLatin1String("size-min"))
                result.filter.size.setRangeText(value, result.filter.size.maxText());
            else
                result.filter.size.setRangeText(result.filter.size.minText(), value);

            // 解析错误在这里转成带行号的问题：条件自己的 problems() 不知道行号。
            const SizeParseResult parsed = parseSizeText(value);
            if (!parsed.ok) {
                addProblem(bare == QLatin1String("size-min") ? ConditionField::SizeMin
                                                             : ConditionField::SizeMax,
                           parsed.problem, parsed.hint);
            }
            // 无论文本对不对，只要填了内容就算「用户配置过这一项」。
            result.filter.size.setEnabled(true);
            continue;
        }

        // --- 修改时间 ---------------------------------------------------------
        if (bare == QLatin1String("time-relative") || bare == QLatin1String("time-from")
            || bare == QLatin1String("time-to")) {
            if (value.isEmpty()) {
                addProblem(ConditionField::TimeRelative, QStringLiteral("缺少值"),
                           QStringLiteral("如：%1 %2")
                               .arg(key, bare == QLatin1String("time-relative")
                                             ? QStringLiteral("7 天")
                                             : QStringLiteral("2026-09-01")));
                continue;
            }

            // 三者的关系不是「可以叠加」：`time-relative` 与 `time-from` 是两种
            // **互斥**的区间形态（前者靠「现在」推，后者是绝对时刻）。
            const ConditionField timeField =
                bare == QLatin1String("time-relative")
                    ? ConditionField::TimeRelative
                    : (bare == QLatin1String("time-from") ? ConditionField::TimeFrom
                                                          : ConditionField::TimeTo);
            if (!claimFamilyKey(key, &claimedTimeKey, timeField))
                continue;

            if (bare == QLatin1String("time-relative")) {
                result.filter.timeRange.setKind(TimeRangeKind::Relative);
                result.filter.timeRange.setRelativeText(value);
                const RelativeDaysParseResult parsed = parseRelativeDaysText(value);
                if (!parsed.ok)
                    addProblem(ConditionField::TimeRelative, parsed.problem, parsed.hint);
            } else {
                result.filter.timeRange.setKind(TimeRangeKind::Absolute);
                if (bare == QLatin1String("time-from"))
                    result.filter.timeRange.setAbsoluteText(value, result.filter.timeRange.toText());
                else
                    result.filter.timeRange.setAbsoluteText(result.filter.timeRange.fromText(), value);

                const DateTimeParseResult parsed = parseDateTimeText(value);
                if (!parsed.ok) {
                    addProblem(bare == QLatin1String("time-from") ? ConditionField::TimeFrom
                                                                  : ConditionField::TimeTo,
                               parsed.problem, parsed.hint);
                }
            }
            result.filter.timeRange.setEnabled(true);
            continue;
        }

        // --- 属性位 -----------------------------------------------------------
        if (bare == QLatin1String("attr")) {
            EntryAttribute attribute = EntryAttribute::ReadOnly;
            if (value.isEmpty()) {
                addProblem(ConditionField::Attributes, QStringLiteral("缺少属性名"),
                           QStringLiteral("可用属性：%1").arg(attributeListText(allEntryAttributes())));
                continue;
            }
            if (!parseEntryAttribute(value, &attribute)) {
                addProblem(ConditionField::Attributes,
                           QStringLiteral("未知的属性名「%1」").arg(value),
                           QStringLiteral("可用属性：%1").arg(attributeListText(allEntryAttributes())));
                continue;
            }
            // `attr` 可重复（一个条目有多个属性位），因此不进 seenKeys。
            result.filter.attributeBits.setRequirement(
                attribute, negated ? Requirement::Forbidden : Requirement::Required);
            result.filter.attributeBits.setEnabled(true);
            continue;
        }

        // --- 所有者 / 组 ------------------------------------------------------
        if (bare == QLatin1String("owner") || bare == QLatin1String("group")) {
            const QStringList names = splitNameListText(value);
            if (names.isEmpty()) {
                addProblem(bare == QLatin1String("owner") ? ConditionField::Owner : ConditionField::Group,
                           QStringLiteral("缺少值"),
                           QStringLiteral("如：%1 alice, bob").arg(key));
                continue;
            }

            // `owner` 与 `-owner` 语义相反（只看谁 / 不看谁），只能留一个；
            // `group` 与 `-group` 同理。
            const bool isOwner = bare == QLatin1String("owner");
            if (!claimFamilyKey(key, isOwner ? &claimedOwnerKey : &claimedGroupKey,
                                isOwner ? ConditionField::Owner : ConditionField::Group))
                continue;

            if (isOwner) {
                result.filter.owner.setOwners(names);
                result.filter.owner.setOwnerMode(negated ? ListMatchMode::NoneOf : ListMatchMode::AnyOf);
            } else {
                result.filter.owner.setGroups(names);
                result.filter.owner.setGroupMode(negated ? ListMatchMode::NoneOf : ListMatchMode::AnyOf);
            }
            result.filter.owner.setEnabled(true);
            continue;
        }

        // --- 不认识的键 -------------------------------------------------------
        addProblem(ConditionField::None, QStringLiteral("未知的键「%1」").arg(key),
                   QStringLiteral("可用键：%1").arg(allDeclarationKeysHint()));
    }

    return result;
}

QString AttributeFilterParseResult::describeProblems() const
{
    return describeConditionProblems(problems);
}

// -----------------------------------------------------------------------------
// 条件表自检
// -----------------------------------------------------------------------------

QStringList validateAttributeConditionTable(
    const QVector<AttributeConditionDescriptor> &table,
    const QVector<AttributeConditionKind> &expectedConditions)
{
    QStringList problems;

    if (table.size() != expectedConditions.size()) {
        problems.append(QStringLiteral("条件表的行数不对：期望 %1 行，实际 %2 行")
                            .arg(expectedConditions.size())
                            .arg(table.size()));
    }

    QSet<QString> identifiers;
    QSet<QString> keys;
    QSet<int> kinds;

    for (int index = 0; index < table.size(); ++index) {
        const AttributeConditionDescriptor &row = table.at(index);
        const QString position = QStringLiteral("第 %1 行").arg(index + 1);

        if (index < expectedConditions.size() && row.kind != expectedConditions.at(index)) {
            problems.append(QStringLiteral("%1 的条件种类不对：期望「%2」，实际「%3」")
                                .arg(position,
                                     attributeConditionLabel(expectedConditions.at(index)),
                                     attributeConditionLabel(row.kind)));
        }

        if (kinds.contains(static_cast<int>(row.kind)))
            problems.append(QStringLiteral("%1 的条件种类「%2」重复了").arg(position, attributeConditionLabel(row.kind)));
        kinds.insert(static_cast<int>(row.kind));

        if (row.identifier.isEmpty()) {
            problems.append(QStringLiteral("%1 缺少机器标识").arg(position));
        } else {
            if (row.identifier != row.identifier.toLower()
                || row.identifier.contains(QLatin1Char(' '))) {
                problems.append(QStringLiteral("%1 的机器标识「%2」必须是小写且不含空格")
                                    .arg(position, row.identifier));
            }
            if (identifiers.contains(row.identifier))
                problems.append(QStringLiteral("%1 的机器标识「%2」重复了").arg(position, row.identifier));
            identifiers.insert(row.identifier);
        }

        if (row.label.isEmpty())
            problems.append(QStringLiteral("%1 缺少中文标签").arg(position));

        if (row.declarationKeys.isEmpty())
            problems.append(QStringLiteral("%1 没有声明键——用户在声明文件里写不出这一类条件").arg(position));

        for (const QString &key : row.declarationKeys) {
            if (key.trimmed() != key || key.contains(QLatin1Char(' '))) {
                problems.append(QStringLiteral("%1 的声明键「%2」含空格或首尾空白").arg(position, key));
            }
            if (keys.contains(key))
                problems.append(QStringLiteral("声明键「%1」在两处出现——用户写一行会被解释两次").arg(key));
            keys.insert(key);
        }

        // 这一条是 FILT-003 的边界条款（属性过滤不得读取条目内容）。
        // 表里把它改成假，就说明有人打算让某一类属性条件去读文件内容,
        // 而那样一来「扫描阶段早期生效」这条性能要求会从根上落空。
        if (!row.metadataOnly) {
            problems.append(QStringLiteral("%1「%2」声明它会读取条目内容——"
                                           "属性过滤必须与内容比对解耦")
                                .arg(position, row.label));
        }
    }

    return problems;
}

// -----------------------------------------------------------------------------
// 与名称过滤的合成（第 4 条的后半句）
// -----------------------------------------------------------------------------

QString EntryFilterDecision::describe() const
{
    QStringList lines;

    if (blockedByName()) {
        lines.append(QStringLiteral("名称过滤未通过：%1").arg(maskVerdictLabel(nameVerdict)));
        if (hasDecidingLayer) {
            lines.append(QStringLiteral("  起决定作用的层：%1").arg(filterLayerLabel(decidingLayer)));
        }
        if (ruleIndex >= 0 && !ruleText.isEmpty()) {
            lines.append(QStringLiteral("  规则：%1").arg(ruleText));
        }
    } else {
        lines.append(QStringLiteral("名称过滤通过：%1").arg(maskVerdictLabel(nameVerdict)));
    }

    lines.append(attributes.describe());
    lines.append(QStringLiteral("最终结论：%1").arg(accepted ? QStringLiteral("保留")
                                                          : QStringLiteral("过滤掉")));
    return lines.join(QLatin1Char('\n'));
}

EntryFilterDecision decideEntry(const FilterStack &names, const AttributeFilter &attributes,
                                const EntryMetadata &entry)
{
    EntryFilterDecision decision;

    const LayeredFilterDecision layered = names.decide(entry.subject);
    decision.nameVerdict = layered.verdict;
    decision.nameAccepted = (layered.verdict == MaskVerdict::Included);
    decision.hasDecidingLayer = layered.anyLayerActive;
    decision.decidingLayer = layered.decidingLayer;
    decision.ruleIndex = layered.ruleIndex;
    decision.ruleText = layered.ruleText;

    decision.attributes = attributes.decide(entry);

    // 第 4 条的要点：两侧是**与**。写成 `||` 的话，一次「只看 10 MB 以上的文件」
    // 会连带把名称过滤排除掉的文件放回来，而用户只会觉得过滤器不可靠。
    decision.accepted = decision.nameAccepted && decision.attributes.accepted;
    return decision;
}

EntryFilterDecision decideEntry(const MaskFilter &names, const AttributeFilter &attributes,
                                const EntryMetadata &entry)
{
    EntryFilterDecision decision;

    const MaskDecision maskDecision = names.decide(entry.subject);
    decision.nameVerdict = maskDecision.verdict;
    decision.nameAccepted = (maskDecision.verdict == MaskVerdict::Included);
    decision.hasDecidingLayer = false;
    decision.ruleIndex = maskDecision.ruleIndex;
    decision.ruleText = maskDecision.ruleText;

    decision.attributes = attributes.decide(entry);
    decision.accepted = decision.nameAccepted && decision.attributes.accepted;
    return decision;
}

} // namespace Filter
} // namespace LqCompare
