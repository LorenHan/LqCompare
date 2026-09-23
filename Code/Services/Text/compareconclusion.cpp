#include "compareconclusion.h"

#include <QObject>

namespace LqCompare { namespace Text {

// ---------------------------------------------------------------------------
// 结论档
// ---------------------------------------------------------------------------

QString conclusionLabel(Conclusion conclusion)
{
    switch (conclusion) {
    case Conclusion::Identical: return QObject::tr("相同");
    case Conclusion::RuleIdentical: return QObject::tr("规则相同");
    case Conclusion::Different: return QObject::tr("不同");
    }
    return QString();
}

QString conclusionDescription(Conclusion conclusion)
{
    switch (conclusion) {
    case Conclusion::Identical:
        return QObject::tr("按当前规则未发现差异。");
    case Conclusion::RuleIdentical:
        // 这句话必须把「不是那一档」说出来：用户看到「相同」两个字时，
        // 最可能的假设就是「两份文件一模一样」，而这里恰恰不是。
        return QObject::tr("按当前规则未发现差异，但两侧在原始层面并不相同"
                           "（差异被规则忽略，例如此开关下的 BOM 差异）。");
    case Conclusion::Different:
        return QObject::tr("存在未被规则忽略的差异。");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// BOM 处理策略（比较侧）
// ---------------------------------------------------------------------------

namespace {

// 表是唯一的事实来源；顺序即 `availableBomPolicies()` 的返回顺序，
// 也是界面上拉的顺序——因此**默认档排在第一**：下拉一打开就停在默认档上，
// 不必先去第三行找它。`defaultBomPolicy()` 的判据正是「第一条已实现的条目」，
// 而它对不对得上 `CompareOptions()` 的出厂值由 `Tests/CompareConclusion`
// 的 `bomPolicyDefaultMatchesCompareOptions()` 钉住（两者不是同一个来源，
// 所以必须有一处等式断言，否则界面显示与引擎实际在用的会是两个档）。
//
// 为什么默认选 `Automatic` 而不是 `Ignore`，理由不是「新功能默认弱一点」：
// `Ignore` 对 UTF-16 / UTF-32 是**错的**——那两种编码的 BOM 就是字节序本身，
// 把「有没有 BOM」一律当成不计较，等于把「一份 UTF-16LE 文件与一份没有 BOM
// 的、按别的字节序读的同一段文本」报成相同。反过来 `TreatAsDifference`
// 会把 UTF-8 那个完全冗余的 `EF BB BF` 提升成真差异，
// 于是从别的编辑器存出来的同内容文件永远比不平——那是最容易把用户逼回
// 「干脆不开这个功能」的一种默认值。`Automatic` 按上面那条分界线决定，
// 两种误判都不会发生。
const QVector<BomPolicyDescriptor> kPolicyTable = {
    {BomPolicy::Automatic, "automatic", true},
    {BomPolicy::Ignore, "ignore", true},
    {BomPolicy::TreatAsDifference, "difference", true},
};

const QVector<BomSavePolicyDescriptor> kSavePolicyTable = {
    {BomSavePolicy::Preserve, "preserve", true},
    {BomSavePolicy::AlwaysWrite, "always", true},
    {BomSavePolicy::NeverWrite, "never", true},
};

} // namespace

const QVector<BomPolicyDescriptor> &bomPolicyTable() { return kPolicyTable; }

const char *bomPolicyIdentifier(BomPolicy policy)
{
    for (const auto &descriptor : kPolicyTable)
        if (descriptor.policy == policy) return descriptor.identifier;
    return nullptr;
}

BomPolicy defaultBomPolicy(const QVector<BomPolicyDescriptor> &table)
{
    for (const auto &descriptor : table)
        if (descriptor.implemented) return descriptor.policy;
    // 表里一条都没有时退回 `Automatic`：它是那张表里唯一不依赖别的档先成立的档，
    // 也是引入本条之前的实际行为（UTF-8 的 BOM 本来就没参与过判定）。
    return BomPolicy::Automatic;
}

bool bomPolicyFromIdentifier(const QString &identifier, BomPolicy *result)
{
    for (const auto &descriptor : kPolicyTable) {
        if (identifier == QLatin1String(descriptor.identifier)) {
            if (result) *result = descriptor.policy;
            return true;
        }
    }
    if (result) *result = defaultBomPolicy();
    return false;
}

QVector<BomPolicy> availableBomPolicies(const QVector<BomPolicyDescriptor> &table)
{
    QVector<BomPolicy> result;
    for (const auto &descriptor : table)
        if (descriptor.implemented) result.append(descriptor.policy);
    return result;
}

QStringList validateBomPolicyTable(const QVector<BomPolicyDescriptor> &table,
                                   const QVector<BomPolicy> &expected)
{
    QStringList problems;
    if (table.isEmpty()) problems << QStringLiteral("BOM 策略表为空，下拉会一个选项都没有。");

    QVector<BomPolicy> seen;
    QVector<QString> identifiers;
    for (const auto &descriptor : table) {
        if (seen.contains(descriptor.policy))
            problems << QStringLiteral("BOM 策略表里重复登记了同一个取值（下标 %1）。")
                            .arg(static_cast<int>(descriptor.policy));
        seen.append(descriptor.policy);
        const QString identifier = QString::fromLatin1(descriptor.identifier);
        if (identifier.isEmpty())
            problems << QStringLiteral("BOM 策略表里有一条没有标识符，日志与设置键会无处可写。");
        else if (identifiers.contains(identifier))
            // 两个取值共用一个标识符时，设置键与日志都会把两件事写成一件，
            // 而现象是「改了设置看不出变化」。
            problems << QStringLiteral("BOM 策略标识符重复：%1。").arg(identifier);
        else
            identifiers.append(identifier);
    }
    // 期望值来自规格，**不来自这张表自身**：表里漏登记一档时，
    // 上面那几条一条都不会响（没有重复、标识符也不空），
    // 只有拿规格去比才发现少了一项。
    for (const BomPolicy policy : expected)
        if (!seen.contains(policy))
            problems << QStringLiteral("BOM 策略表漏登记了取值 %1。")
                            .arg(static_cast<int>(policy));

    const auto available = availableBomPolicies(table);
    if (available.isEmpty())
        problems << QStringLiteral("BOM 策略表里一条已实现的都没有，下拉会空着。");
    if (!available.contains(defaultBomPolicy(table)))
        problems << QStringLiteral("BOM 策略的默认值不在可选集合里，界面一打开就会显示到一个没有的档。");
    return problems;
}

QString bomPolicyLabel(BomPolicy policy)
{
    switch (policy) {
    case BomPolicy::Ignore: return QObject::tr("忽略 BOM 差异");
    case BomPolicy::TreatAsDifference: return QObject::tr("BOM 差异视为差异");
    case BomPolicy::Automatic: return QObject::tr("按编码自动判定");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// BOM 与编码
// ---------------------------------------------------------------------------

bool bomIsEncodingCritical(const QByteArray &codecName)
{
    // 只认这四个。UTF-8 没有字节序问题，它的 BOM 是一个**内容无关**的标记
    // （Unicode 标准自己就建议不要写它）；其余单字节编码（GBK / Big5 /
    // ISO-8859-1）连 BOM 概念都没有。写上这个白名单而不是
    // 「`codecName != "UTF-8"`」，是因为后者会把一个错拼的编码名也算成
    // 「BOM 是关键信息」——那会让 `Automatic` 在一个本不该有 BOM 的编码上
    // 把差异报出来。
    return codecName == "UTF-16LE" || codecName == "UTF-16BE"
        || codecName == "UTF-32LE" || codecName == "UTF-32BE";
}

QByteArray bomBytesForCodec(const QByteArray &codecName)
{
    if (codecName == "UTF-8") return QByteArray::fromHex("efbbbf");
    if (codecName == "UTF-16LE") return QByteArray::fromHex("fffe");
    if (codecName == "UTF-16BE") return QByteArray::fromHex("feff");
    if (codecName == "UTF-32LE") return QByteArray::fromHex("fffe0000");
    if (codecName == "UTF-32BE") return QByteArray::fromHex("0000feff");
    return {};
}

// ---------------------------------------------------------------------------
// 判定
// ---------------------------------------------------------------------------

BomObservation observeBom(const Document &left, const Document &right, bool available)
{
    BomObservation observation;
    observation.leftAvailable = available;
    observation.rightAvailable = available;
    observation.leftHasBom = left.hasBom();
    observation.rightHasBom = right.hasBom();
    observation.leftCodec = left.codecName();
    observation.rightCodec = right.codecName();
    return observation;
}

BomVerdict evaluateBom(BomPolicy policy, const BomObservation &observation)
{
    BomVerdict verdict;
    // 观测不全时不下结论。这里**不**退回某个默认档：把「还没打开」当成
    // 「两侧一样」会让状态栏在打开之前就印出一句关于 BOM 的话。
    if (!observation.complete()) return verdict;
    // BOM 状态相同 ⇒ 这一维没有差异可谈。注意这与「有差异但被忽略」不同。
    if (observation.leftHasBom == observation.rightHasBom) return verdict;

    switch (policy) {
    case BomPolicy::Ignore:
        verdict.conclusion = BomConclusion::Ignored;
        break;
    case BomPolicy::TreatAsDifference:
        verdict.conclusion = BomConclusion::Difference;
        break;
    case BomPolicy::Automatic:
        // 两侧**任一侧**的编码把 BOM 当成字节序声明，这个差异就真的改变了
        // 字节怎么读，因此算差异。只看一侧不够：一侧是 UTF-16LE（BOM 必需）、
        // 另一侧是 UTF-8（BOM 冗余）时，少的那个 BOM 让「这份文件按什么读」
        // 从确定变成了猜测。
        verdict.conclusion =
            (bomIsEncodingCritical(observation.leftCodec)
             || bomIsEncodingCritical(observation.rightCodec))
            ? BomConclusion::Difference
            : BomConclusion::Ignored;
        break;
    }
    return verdict;
}

QString bomConclusionSummary(BomConclusion conclusion)
{
    switch (conclusion) {
    case BomConclusion::None:
        return QString();
    case BomConclusion::Ignored:
        // 「并非字节完全一致」这半句是规格点名的：只写「已忽略 BOM 差异」的话，
        // 用户会把它读成「两份文件完全一样」，而这里恰恰承认了两侧原始字节不同。
        return QObject::tr("BOM 差异已忽略（结论为规则相同，两侧的原始字节并不相同）");
    case BomConclusion::Difference:
        return QObject::tr("BOM 差异计为不同（行内容未发现差异）");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// BOM 处理策略（保存侧）
// ---------------------------------------------------------------------------

const QVector<BomSavePolicyDescriptor> &bomSavePolicyTable() { return kSavePolicyTable; }

const char *bomSavePolicyIdentifier(BomSavePolicy policy)
{
    for (const auto &descriptor : kSavePolicyTable)
        if (descriptor.policy == policy) return descriptor.identifier;
    return nullptr;
}

BomSavePolicy defaultBomSavePolicy(const QVector<BomSavePolicyDescriptor> &table)
{
    for (const auto &descriptor : table)
        if (descriptor.implemented) return descriptor.policy;
    return BomSavePolicy::Preserve;
}

QVector<BomSavePolicy> availableBomSavePolicies(const QVector<BomSavePolicyDescriptor> &table)
{
    QVector<BomSavePolicy> result;
    for (const auto &descriptor : table)
        if (descriptor.implemented) result.append(descriptor.policy);
    return result;
}

QStringList validateBomSavePolicyTable(const QVector<BomSavePolicyDescriptor> &table,
                                       const QVector<BomSavePolicy> &expected)
{
    QStringList problems;
    if (table.isEmpty()) problems << QStringLiteral("BOM 保存策略表为空，下拉会一个选项都没有。");

    QVector<BomSavePolicy> seen;
    QVector<QString> identifiers;
    for (const auto &descriptor : table) {
        if (seen.contains(descriptor.policy))
            problems << QStringLiteral("BOM 保存策略表里重复登记了同一个取值（下标 %1）。")
                            .arg(static_cast<int>(descriptor.policy));
        seen.append(descriptor.policy);
        const QString identifier = QString::fromLatin1(descriptor.identifier);
        if (identifier.isEmpty())
            problems << QStringLiteral("BOM 保存策略表里有一条没有标识符。");
        else if (identifiers.contains(identifier))
            problems << QStringLiteral("BOM 保存策略标识符重复：%1。").arg(identifier);
        else
            identifiers.append(identifier);
    }
    for (const BomSavePolicy policy : expected)
        if (!seen.contains(policy))
            problems << QStringLiteral("BOM 保存策略表漏登记了取值 %1。")
                            .arg(static_cast<int>(policy));

    const auto available = availableBomSavePolicies(table);
    if (available.isEmpty())
        problems << QStringLiteral("BOM 保存策略表里一条已实现的都没有，下拉会空着。");
    if (!available.contains(defaultBomSavePolicy(table)))
        problems << QStringLiteral("BOM 保存策略的默认值不在可选集合里。");
    return problems;
}

QString bomSavePolicyLabel(BomSavePolicy policy)
{
    switch (policy) {
    case BomSavePolicy::Preserve: return QObject::tr("保存时保留原 BOM 状态");
    case BomSavePolicy::AlwaysWrite: return QObject::tr("保存时强制写入 BOM");
    case BomSavePolicy::NeverWrite: return QObject::tr("保存时不写入 BOM");
    }
    return QString();
}

bool bomSavePolicyApplies(BomSavePolicy policy, const QByteArray &codecName)
{
    if (policy == BomSavePolicy::NeverWrite && bomIsEncodingCritical(codecName)) return false;
    // `AlwaysWrite` 也不是到处都成立：对 GBK 这类编码写一段 BOM 字节，
    // 结果是文件头多出几个乱码字符——BOM 在那里根本不是标记，是内容。
    if (policy == BomSavePolicy::AlwaysWrite && bomBytesForCodec(codecName).isEmpty()) return false;
    return true;
}

bool bomShouldBeWritten(BomSavePolicy policy, bool originalHasBom, const QByteArray &codecName)
{
    bool wanted = originalHasBom;
    if (!bomSavePolicyApplies(policy, codecName)) {
        // 做不到时按 `Preserve` 走。**不报错**是有意的：用户按的是
        // 「不写 BOM」，而两侧的差别只在「文件还能不能读」上，
        // 把一个拒绝保存弹窗塞给他，解决不了他真正想要的事
        // （他想要的是「别让这个文件带 BOM」，而那对 UTF-16 本来就不成立）。
        wanted = originalHasBom;
    } else if (policy == BomSavePolicy::AlwaysWrite) {
        wanted = true;
    } else if (policy == BomSavePolicy::NeverWrite) {
        wanted = false;
    }
    // 编码本身没有可写的 BOM 时，无论前面想要什么，最后都是一段字节都不写。
    // 这一句必须留在**最后**：`Preserve` 那一支完全可能想要 true——
    // 文件原来带 UTF-8 BOM，而用户把解码编码改成了 GBK，
    // 此时「保留」在物理上做不到（GBK 文件头那几个字节是内容，不是标记）。
    return wanted && !bomBytesForCodec(codecName).isEmpty();
}

// ---------------------------------------------------------------------------
// 合成
// ---------------------------------------------------------------------------

Conclusion conclude(const Result &result, const BomVerdict &bom)
{
    if (!result.differences.isEmpty() || bom.countedAsDifference())
        return Conclusion::Different;
    if (result.ignoredBlocks > 0 || bom.ignored())
        return Conclusion::RuleIdentical;
    return Conclusion::Identical;
}

} } // namespace LqCompare::Text
