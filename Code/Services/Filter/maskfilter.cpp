#include "maskfilter.h"

namespace LqCompare {
namespace Filter {

namespace {

bool isLineSpace(QChar c)
{
    return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

} // namespace

QStringList splitDeclarationLines(const QString &declaration)
{
    QStringList lines;
    QString current;

    const int length = declaration.length();
    for (int i = 0; i < length; ++i) {
        const QChar character = declaration.at(i);
        if (character != QLatin1Char('\n') && character != QLatin1Char('\r')) {
            current.append(character);
            continue;
        }

        lines.append(current);
        current.clear();

        // `\r\n` 是一个换行而不是两个，否则每个空行会被数成两行，
        // 报错时的行号整体偏大。
        if (character == QLatin1Char('\r') && i + 1 < length
            && declaration.at(i + 1) == QLatin1Char('\n')) {
            ++i;
        }
    }

    lines.append(current);
    return lines;
}

// -----------------------------------------------------------------------------
// 标签
// -----------------------------------------------------------------------------

const char *maskRuleKindIdentifier(MaskRuleKind kind)
{
    return kind == MaskRuleKind::Exclude ? "exclude" : "include";
}

QString maskRuleKindLabel(MaskRuleKind kind)
{
    return kind == MaskRuleKind::Exclude ? QStringLiteral("排除") : QStringLiteral("包含");
}

const char *maskVerdictIdentifier(MaskVerdict verdict)
{
    switch (verdict) {
    case MaskVerdict::Included:
        return "included";
    case MaskVerdict::Excluded:
        return "excluded";
    case MaskVerdict::NotMatched:
        break;
    }
    return "not-matched";
}

QString maskVerdictLabel(MaskVerdict verdict)
{
    switch (verdict) {
    case MaskVerdict::Included:
        return QStringLiteral("保留");
    case MaskVerdict::Excluded:
        return QStringLiteral("被排除");
    case MaskVerdict::NotMatched:
        break;
    }
    return QStringLiteral("未命中");
}

// -----------------------------------------------------------------------------
// 结果结构体
// -----------------------------------------------------------------------------

QString MaskRule::describe() const
{
    return QStringLiteral("第 %1 行：%2 `%3`")
            .arg(line + 1)
            .arg(maskRuleKindLabel(kind), text);
}

QString MaskRuleError::describe() const
{
    QString text;
    if (line >= 0 && column >= 0)
        text = QStringLiteral("第 %1 行第 %2 列：%3").arg(line + 1).arg(column + 1).arg(message);
    else if (line >= 0)
        text = QStringLiteral("第 %1 行：%2").arg(line + 1).arg(message);
    else
        text = message;

    if (!hint.isEmpty())
        text += QStringLiteral("（%1）").arg(hint);

    return text;
}

QString MaskDecision::describe() const
{
    switch (verdict) {
    case MaskVerdict::Excluded:
        return QStringLiteral("被排除规则 #%1（`%2`）命中").arg(ruleIndex + 1).arg(ruleText);

    case MaskVerdict::Included:
        if (ruleIndex < 0)
            return QStringLiteral("没有任何包含规则，默认保留");
        return QStringLiteral("被包含规则 #%1（`%2`）命中").arg(ruleIndex + 1).arg(ruleText);

    case MaskVerdict::NotMatched:
        break;
    }
    return QStringLiteral("未命中任何包含规则");
}

QString MaskFilterParseResult::describeErrors() const
{
    if (errors.isEmpty())
        return QString();

    QStringList lines;
    lines.reserve(errors.size());
    for (const MaskRuleError &error : errors)
        lines.append(error.describe());
    return lines.join(QLatin1Char('\n'));
}

QString MaskFilterPreview::summary() const
{
    // 规格第 4 条点名要这句文案。做成函数而不是让每个界面各拼一遍：
    // 一旦有两处拼，其中一处迟早会写成「共 M 项 / 匹配 N 项」，
    // 而截图对比时没人会注意到顺序变了。
    return QStringLiteral("匹配 %1 项 / 共 %2 项").arg(included).arg(total);
}

// -----------------------------------------------------------------------------
// MaskFilter
// -----------------------------------------------------------------------------

MaskFilter::MaskFilter() = default;

MaskFilterParseResult MaskFilter::parse(const QString &declaration, MaskPlatform platform)
{
    MaskFilterParseResult result;

    result.filter.m_platform = platform;
    result.filter.m_case = defaultCaseSensitivity(platform);
    result.filter.m_caseOverridden = false;

    const QStringList lines = splitDeclarationLines(declaration);

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &line = lines.at(lineIndex);

        int cursor = 0;
        while (cursor < line.length() && isLineSpace(line.at(cursor)))
            ++cursor;

        // 空行与空白行：不是错误，只是没内容。预设文件里大量存在。
        if (cursor >= line.length())
            continue;

        // 注释：只认「一行的第一个非空白字符」。因此 `-#foo` 里的 `#` 是普通字符
        // （见 maskSyntaxReference 里对应的一条），这样「排除一个名字以 # 开头的
        // 文件」不需要额外发明语法。转义成 `\#` 也能写，但那是给「掩码本身以 #
        // 开头」准备的。
        if (line.at(cursor) == QLatin1Char('#'))
            continue;

        MaskRuleKind kind = MaskRuleKind::Include;
        if (line.at(cursor) == QLatin1Char('-')) {
            kind = MaskRuleKind::Exclude;
            ++cursor;
        }

        // 从这里开始就是掩码文本。刻意**不**再把空白跳掉：Mask::compile 自己会裁，
        // 而且它报的列号是相对它收到的文本的，跳两次会让列号算不平。
        const QString maskText = line.mid(cursor);
        const MaskParseResult parsed = Mask::compile(maskText);

        if (!parsed.ok()) {
            MaskRuleError error;
            error.line = lineIndex;
            // 把 Mask::compile 的列号换算回整行。它可能给出 -1（与位置无关的
            // 错误），那时就落回掩码的起点。
            error.column = parsed.error.column >= 0 ? cursor + parsed.error.column : cursor;
            error.length = parsed.error.length;
            error.message = parsed.error.message;
            error.hint = parsed.error.hint;
            result.errors.append(error);

            // 只丢这一行。其余行照常生效——用户在界面上是一行一行改的，
            // 整段失效会让他以为自己把别处敲坏了。
            continue;
        }

        MaskRule rule;
        rule.kind = kind;
        rule.mask = parsed.mask;
        rule.text = parsed.mask.normalizedPattern();
        rule.line = lineIndex;
        rule.column = cursor;
        result.filter.m_rules.append(rule);
    }

    return result;
}

int MaskFilter::includeCount() const
{
    int count = 0;
    for (const MaskRule &rule : m_rules) {
        if (rule.kind == MaskRuleKind::Include)
            ++count;
    }
    return count;
}

int MaskFilter::excludeCount() const
{
    int count = 0;
    for (const MaskRule &rule : m_rules) {
        if (rule.kind == MaskRuleKind::Exclude)
            ++count;
    }
    return count;
}

void MaskFilter::setCaseSensitivity(Qt::CaseSensitivity cs)
{
    m_case = cs;
    m_caseOverridden = true;
}

void MaskFilter::clearCaseSensitivityOverride()
{
    m_case = defaultCaseSensitivity(m_platform);
    m_caseOverridden = false;
}

MaskDecision MaskFilter::decide(const MaskSubject &subject) const
{
    MaskDecision decision;

    // 排除优先。这是本条目第 2 条完成标准的全部内容，也是唯一合理的次序：
    // `*.cpp` 与 `-*_test.cpp` 同时声明时，用户要的是「要 .cpp、但不要测试」。
    // 若改成包含优先，排除掩码就永远不起作用——而用户没有任何替代写法，
    // 因为「除测试以外的 cpp」用正向掩码是写不出来的。
    for (int index = 0; index < m_rules.size(); ++index) {
        const MaskRule &rule = m_rules.at(index);
        if (rule.kind != MaskRuleKind::Exclude)
            continue;
        if (!rule.mask.matches(subject, m_case))
            continue;

        decision.verdict = MaskVerdict::Excluded;
        decision.ruleIndex = index;
        decision.ruleKind = rule.kind;
        decision.ruleText = rule.text;
        return decision;
    }

    for (int index = 0; index < m_rules.size(); ++index) {
        const MaskRule &rule = m_rules.at(index);
        if (rule.kind != MaskRuleKind::Include)
            continue;
        if (!rule.mask.matches(subject, m_case))
            continue;

        decision.verdict = MaskVerdict::Included;
        decision.ruleIndex = index;
        decision.ruleKind = rule.kind;
        decision.ruleText = rule.text;
        return decision;
    }

    // 一条包含掩码都没有 → 全部保留。这是「只填了排除框」时的行为，
    // 也是所有过滤器的惯例：留空的包含列表不该把所有东西都挡在外面。
    // 反过来说，只要写了哪怕一条包含掩码，语义就变成白名单。
    if (includeCount() == 0) {
        decision.verdict = MaskVerdict::Included;
        decision.ruleIndex = -1;
        return decision;
    }

    decision.verdict = MaskVerdict::NotMatched;
    decision.ruleIndex = -1;
    return decision;
}

bool MaskFilter::accepts(const MaskSubject &subject) const
{
    return decide(subject).verdict == MaskVerdict::Included;
}

QVector<int> MaskFilter::matchingRuleIndexes(const MaskSubject &subject) const
{
    QVector<int> indexes;
    for (int index = 0; index < m_rules.size(); ++index) {
        if (m_rules.at(index).mask.matches(subject, m_case))
            indexes.append(index);
    }
    return indexes;
}

QString MaskFilter::describe() const
{
    if (m_rules.isEmpty())
        return QStringLiteral("没有任何规则（全部保留）");

    QString text = QStringLiteral("包含 %1 条、排除 %2 条，%3")
                           .arg(includeCount())
                           .arg(excludeCount())
                           .arg(m_case == Qt::CaseInsensitive ? QStringLiteral("大小写不敏感")
                                                              : QStringLiteral("大小写敏感"));

    if (!m_caseOverridden) {
        text += QStringLiteral("（平台默认：%1）")
                        .arg(QString::fromLatin1(maskPlatformIdentifier(m_platform)));
    }

    return text;
}

// -----------------------------------------------------------------------------
// 预览
// -----------------------------------------------------------------------------

MaskFilterPreview preview(const MaskFilter &filter, const QVector<MaskSubject> &subjects)
{
    MaskFilterPreview result;
    result.total = subjects.size();
    result.hitsByRule = QVector<int>(filter.ruleCount(), 0);

    for (const MaskSubject &subject : subjects) {
        const MaskDecision decision = filter.decide(subject);
        switch (decision.verdict) {
        case MaskVerdict::Included:
            ++result.included;
            break;
        case MaskVerdict::Excluded:
            ++result.excluded;
            break;
        case MaskVerdict::NotMatched:
            ++result.notMatched;
            break;
        }

        // 「默认保留」（没有任何包含规则）没有对应的规则下标，不计入任何一格。
        if (decision.ruleIndex >= 0 && decision.ruleIndex < result.hitsByRule.size())
            result.hitsByRule[decision.ruleIndex] += 1;
    }

    return result;
}

MaskFilterPreview previewNames(const MaskFilter &filter, const QStringList &names)
{
    QVector<MaskSubject> subjects;
    subjects.reserve(names.size());
    for (const QString &name : names)
        subjects.append(MaskSubject::forName(name));
    return preview(filter, subjects);
}

} // namespace Filter
} // namespace LqCompare
