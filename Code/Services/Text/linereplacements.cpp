#include "linereplacements.h"

#include <QRegularExpression>
#include <QSet>

namespace LqCompare { namespace Text {
namespace {

///
/// 为什么是「替换成占位符」而不是「删掉命中片段」：
///
/// 1. 删除会让「整行原本就是一个地址」的行塌成**空行**，与文件里真的空行不可区分。
///    在差异视图里，那表现为凭空多出／少掉一个空行段落，而用户对着自己的编辑历史
///    找不到这处变化——差异工具最糟糕的一种表现：报了一个用户没做过的改动。
/// 2. 占位符保留「这里原本有一处内容」这一信息，两侧是同一个占位符时仍然判等为相同，
///    「忽略」的意图达到了，行的形状也没有被抹掉。
/// 3. 占位符是**表里的数据**，所以「换成什么」可以改而不用碰应用逻辑。
///
/// 一个必须知道的后果：占位符里的大写字母会跟着 `ignoreCase` 一起被 `toCaseFolded()`
/// 折成小写（`<NUM>` → `<num>`）。判等只看两侧一致，所以这不影响结论；
/// 但**不要**依赖占位符的原始大小写去做任何判断，包括写测试。
///
///
/// 正则为什么在第一次用到时编译一次就缓存：
/// 替换规则跑在**每一行、每一侧**上（`keys()` 里两次），编译正则的代价远大于匹配。
/// 表是常量数据，缓存因此不会失效；测试要验「表里那条正则」时走的是
/// `applyReplacementRules`，与缓存同一份输入。
///
const QVector<QRegularExpression> &compiledPatterns()
{
    static const QVector<QRegularExpression> patterns = []() {
        QVector<QRegularExpression> list;
        const QVector<ReplacementRuleDescriptor> &table = replacementRuleTable();
        list.reserve(table.size());
        for (const ReplacementRuleDescriptor &descriptor : table)
            list.append(QRegularExpression(QString::fromUtf8(descriptor.pattern)));
        return list;
    }();
    return patterns;
}

// 表里的下标。表外取值返回 -1——调用方据此**跳过**，而不是兜底成某一条规则。
int tableIndexOf(ReplacementRule rule)
{
    const QVector<ReplacementRuleDescriptor> &table = replacementRuleTable();
    for (int i = 0; i < table.size(); ++i)
        if (table[i].rule == rule) return i;
    return -1;
}

} // namespace

const QVector<ReplacementRuleDescriptor> &replacementRuleTable()
{
    // 顺序即应用顺序（完成标准第 3 条）。调换任意两行都会改变结果——
    // 见 `Tests/TextRules` 里那条「两条规则都想吃同一段文本」的语料，
    // 它存在的唯一目的就是让这个顺序成为**可观察**的事实。
    static const QVector<ReplacementRuleDescriptor> table = {
        {ReplacementRule::LeadingNumber, "leading-number", "<NUM>",
         "^[ \\t]*[0-9]{1,9}[.):][ \\t]+",
         "行首编号（如「1. 」「2) 」「12: 」，要求分隔符后紧跟空白）", true},
        {ReplacementRule::DateTime, "date-time", "<DATE>",
         "\\b[0-9]{4}[-/][0-9]{1,2}[-/][0-9]{1,2}"
         "(?:[T ][0-9]{1,2}:[0-9]{2}(?::[0-9]{2})?(?:\\.[0-9]+)?"
         "(?:Z|[+-][0-9]{2}:?[0-9]{2})?)?\\b",
         "日期与日期时间（如 2024-01-01、2024/1/1、2024-01-01T10:20:30Z）", true},
        // `\\{?` / `\\}?` 让花括号一起被吃掉：只吃中间的 8-4-4-4-12 而把花括号留下，
        // 会让 `{...}` 与不带花括号的写法在判等上仍然不同——而它们表达的是同一个标识。
        // 两侧的 `(?<![0-9A-Fa-f])` / `(?![0-9A-Fa-f])` 防的是「嵌在更长十六进制串里」
        // 的误伤；注意 `x` **不在**十六进制字符集里，所以 `0x12345678-...` 中
        // `12345678` 前的那一位是允许的（这正是顺序语料要用的重叠点）。
        {ReplacementRule::Guid, "guid", "<GUID>",
         "(?<![0-9A-Fa-f])\\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}"
         "-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\\}?(?![0-9A-Fa-f])",
         "GUID / UUID（8-4-4-4-12，可带花括号）", true},
        // 前边界不能用 `\\b`：`\\b` 要求「一侧是非词字符」，而 `0` 前面若是 `_`
        // 也会被当成词内位置（`_0x1F` 不该被吃）。用显式的否定后顾把边界说清楚。
        {ReplacementRule::HexAddress, "hex-address", "<HEX>",
         "(?<![0-9A-Za-z_])0[xX][0-9A-Fa-f]+",
         "十六进制地址（如 0x7FFE0000，要求 0x 前缀）", true},
    };
    return table;
}

const ReplacementRuleDescriptor *replacementRuleDescriptor(ReplacementRule rule)
{
    const int index = tableIndexOf(rule);
    if (index < 0) return nullptr;
    return &replacementRuleTable()[index];
}

const char *replacementRuleIdentifier(ReplacementRule rule)
{
    const ReplacementRuleDescriptor *descriptor = replacementRuleDescriptor(rule);
    return descriptor ? descriptor->identifier : nullptr;
}

QString replacementRuleDescription(ReplacementRule rule)
{
    const ReplacementRuleDescriptor *descriptor = replacementRuleDescriptor(rule);
    return descriptor ? QString::fromUtf8(descriptor->description) : QString();
}

bool hasImplementedReplacementRule(const QVector<ReplacementRuleDescriptor> &table,
                                   ReplacementRule rule)
{
    for (const ReplacementRuleDescriptor &descriptor : table)
        if (descriptor.rule == rule && descriptor.implemented) return true;
    return false;
}

QVector<ReplacementRule> availableReplacementRules(const QVector<ReplacementRuleDescriptor> &table)
{
    QVector<ReplacementRule> rules;
    for (const ReplacementRuleDescriptor &descriptor : table)
        if (descriptor.implemented) rules.append(descriptor.rule);
    return rules;
}

QVector<ReplacementRule> defaultReplacementRules()
{
    return {};
}

QStringList validateReplacementRuleTable(const QVector<ReplacementRuleDescriptor> &table,
                                         const QVector<ReplacementRule> &expected)
{
    QStringList problems;
    QSet<QString> identifiers;
    QSet<int> seen;
    for (const ReplacementRuleDescriptor &descriptor : table) {
        const QString id = QString::fromUtf8(descriptor.identifier);
        if (id.isEmpty()) problems << QStringLiteral("替换规则缺少标识符。");
        else if (identifiers.contains(id)) problems << QStringLiteral("替换规则标识符重复：%1").arg(id);
        else identifiers.insert(id);

        if (seen.contains(static_cast<int>(descriptor.rule)))
            problems << QStringLiteral("替换规则在表里登记了两次：%1").arg(id);
        seen.insert(static_cast<int>(descriptor.rule));

        // 未实现的条目只查「有没有登记过」，不查它的正则：一个还没写好的规则
        // 带着半成品正则留在表里是正常的，报红只会逼人把它整条删掉。
        if (!descriptor.implemented) continue;

        const QString pattern = QString::fromUtf8(descriptor.pattern);
        if (pattern.isEmpty()) {
            problems << QStringLiteral("已实现的替换规则没有正则：%1").arg(id);
        } else {
            const QRegularExpression compiled(pattern);
            if (!compiled.isValid())
                problems << QStringLiteral("替换规则正则无法编译：%1（%2）")
                                .arg(id, compiled.errorString());
        }
        if (QString::fromUtf8(descriptor.placeholder).isEmpty())
            problems << QStringLiteral("已实现的替换规则缺少占位符：%1").arg(id);
    }

    for (const ReplacementRule rule : expected) {
        if (hasImplementedReplacementRule(table, rule)) continue;
        const char *identifier = replacementRuleIdentifier(rule);
        problems << QStringLiteral("规格点名的替换规则缺失或未实现：%1")
                        .arg(identifier ? QString::fromUtf8(identifier) : QStringLiteral("未知取值"));
    }
    return problems;
}

QString applyReplacementRules(const QString &line, const QVector<ReplacementRule> &rules)
{
    QString result = line;
    const QVector<ReplacementRuleDescriptor> &table = replacementRuleTable();
    const QVector<QRegularExpression> &patterns = compiledPatterns();
    for (const ReplacementRule rule : rules) {
        const int index = tableIndexOf(rule);
        // 表外的取值：跳过，不兜底。与 `Whitespace` 那处的处置一致——
        // 退化只会少忽略一些差异，绝不会把差异藏起来。
        if (index < 0 || index >= patterns.size()) continue;
        if (!table[index].implemented) continue;
        if (patterns[index].pattern().isEmpty()) continue;
        // `replace` 是**全局**替换：一行里出现两次同一个地址也要一起吃掉，
        // 否则「同一行两处地址里只有一处不同」仍会被报成差异。
        result.replace(patterns[index], QString::fromUtf8(table[index].placeholder));
    }
    return result;
}

ReplacementSet::ReplacementSet()
    : m_enabled(replacementRuleTable().size(), false)
{
}

ReplacementSet ReplacementSet::fromRules(const QVector<ReplacementRule> &rules)
{
    ReplacementSet set;
    for (const ReplacementRule rule : rules)
        set.setEnabled(rule, true);
    return set;
}

bool ReplacementSet::isEnabled(ReplacementRule rule) const
{
    const int index = tableIndexOf(rule);
    if (index < 0 || index >= m_enabled.size()) return false;
    return m_enabled[index];
}

void ReplacementSet::setEnabled(ReplacementRule rule, bool enabled)
{
    const int index = tableIndexOf(rule);
    if (index < 0) return; // 表外取值：忽略，不改集合也不报错
    if (m_enabled.size() != replacementRuleTable().size())
        m_enabled.resize(replacementRuleTable().size()); // 表变长了（新增了一条规则）时对齐
    m_enabled[index] = enabled;
}

bool ReplacementSet::isEmpty() const
{
    for (const bool enabled : m_enabled)
        if (enabled) return false;
    return true;
}

QVector<ReplacementRule> ReplacementSet::enabledRules() const
{
    QVector<ReplacementRule> rules;
    const QVector<ReplacementRuleDescriptor> &table = replacementRuleTable();
    for (int i = 0; i < table.size(); ++i)
        if (i < m_enabled.size() && m_enabled[i]) rules.append(table[i].rule);
    return rules;
}

QString ReplacementSet::apply(const QString &line) const
{
    // 空集合时原样返回：这不只是省一点时间，更是「不启用任何规则 == 与引入本条之前
    // 逐字节相同」这条不变量的实现形式——它必须是一次短路，不能靠「正则都不匹配」。
    if (isEmpty()) return line;
    return applyReplacementRules(line, enabledRules());
}

} }
