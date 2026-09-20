#include "contentfilter.h"

#include "namefilter.h" // analyzeRegexPatternRisk()：回溯预检只有那一份实现

#include <QHash>
#include <QSet>

namespace LqCompare {
namespace Filter {

namespace {

/// 与 namefilter.cpp 的 `ltrimAscii` / `trimBoth` 同一手法：只用 ASCII 空白
/// 参与「这一行算不算空行」的判断。
///
/// 为什么不用 `QString::trimmed()` 的判断结果去做**所有**空白处理：
/// `trimmed()` 会把 U+00A0（不换行空格）之类的 Unicode 空白也一起吃掉，
/// 而这类字符在文本里是**内容**（用户可能真的想把一个 U+00A0 写进行过滤器）。
/// 本仓既有过滤器都只认 ASCII 空白，这里保持一致。
QString ltrimAscii(const QString &text)
{
    int index = 0;
    while (index < text.size() && text.at(index).isSpace() && text.at(index).unicode() < 128) {
        ++index;
    }
    return text.mid(index);
}

QString trimBoth(const QString &text)
{
    const QString left = ltrimAscii(text);
    int end = left.size();
    while (end > 0 && left.at(end - 1).isSpace() && left.at(end - 1).unicode() < 128) {
        --end;
    }
    return left.left(end);
}

// -----------------------------------------------------------------------------
// 行过滤模式表
// -----------------------------------------------------------------------------

/// 理由同 `contentFilterStageTable()`：表会以引用形式交出去，必须是函数内静态。
///
/// `prefixes` 里同时列 `=` 与 `= ` 两种写法（空格可选）。**长串必须排在前**：
/// 前缀匹配按最长优先做，把 `=` 排在 `= ` 前面会让 `= foo` 的正文
/// 从 `" foo"` 变成 `"foo"`——本例里最终 trim 掉看不出差别，
/// 但列号会差一格，界面上标红的位置就偏了。
const QVector<LineFilterModeRow> &builtinLineModeTable()

{
    static const QVector<LineFilterModeRow> table = QVector<LineFilterModeRow>{
        LineFilterModeRow{LineFilterMode::Exact, "exact", QStringLiteral("精确行"),
                          QVector<QString>{QStringLiteral("= "), QStringLiteral("=")},
                          QStringLiteral("整行与该字符串全等（首尾空白会被裁掉，"
                                         "要保留请写 `\\x20`）"),
                          true},
        LineFilterModeRow{LineFilterMode::Wildcard, "wildcard", QStringLiteral("通配行"),
                          QVector<QString>{QString()},
                          QStringLiteral("整行匹配：掩码语法（`*` 任意字符、`?` 单字符、"
                                         "`[...]` 字符集）"),
                          true},
        LineFilterModeRow{LineFilterMode::Regex, "regex", QStringLiteral("正则行"),
                          QVector<QString>{QStringLiteral("re:"), QStringLiteral("re: ")},
                          QStringLiteral("搜子串（等价于 grep）：不自动加锚点，"
                                         "要整行请自己写 `^…$`"),
                          false},
    };
    return table;
}

// -----------------------------------------------------------------------------
// 字节序列转义
// -----------------------------------------------------------------------------

bool isHexDigit(QChar character)
{
    const ushort code = character.unicode();
    return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')
        || (code >= 'A' && code <= 'F');
}

int hexValue(QChar character)
{
    const ushort code = character.unicode();
    if (code >= '0' && code <= '9')
        return code - '0';
    if (code >= 'a' && code <= 'f')
        return code - 'a' + 10;
    return code - 'A' + 10;
}

QString hexEscape(char value)
{
    const QString digits = QString::number(static_cast<unsigned char>(value), 16)
                               .rightJustified(2, QLatin1Char('0'))
                               .toUpper();
    return QStringLiteral("\\x") + digits;
}

} // namespace

// -----------------------------------------------------------------------------
// 阶段表
// -----------------------------------------------------------------------------

const QVector<ContentFilterStageRow> &contentFilterStageTable()
{
    // **必须是函数内静态**：`contentFilterStageIdentifier()` 之类会把表里
    // `const char *` 的地址交出去，而任何「按值返回局部 `QVector`」的写法都会让
    // 那个地址在函数返回的一瞬间悬垂——现象是标识符偶尔变成 `unknown`、
    // 严重时直接 SIGSEGV，而崩溃位置离这张表很远（见 §6 那条坑记录）。
    //
    // 中文一律用 QStringLiteral 而不是 `const char *`：本仓有两条坑记录说明
    // `const char *` + `QLatin1String` 处理中文会永远不相等。
    static const QVector<ContentFilterStageRow> table = QVector<ContentFilterStageRow>{
        ContentFilterStageRow{ContentFilterStage::LineFilter, "line-filter",
                              QStringLiteral("行过滤"),
                              QStringLiteral("把匹配模式的行从比较输入里去掉了再比；"
                                             "输入是**原始行**，不经过任何规范化")},
        ContentFilterStageRow{ContentFilterStage::IgnoreRules, "ignore-rules",
                              QStringLiteral("忽略规则"),
                              QStringLiteral("比对时忽略空白 / 大小写 / 行尾差异；"
                                             "它不改变参与比较的行集，属于比对引擎")},
    };
    return table;
}

namespace {

const ContentFilterStageRow *stageRowOf(const QVector<ContentFilterStageRow> &rows,
                                        ContentFilterStage stage)
{
    for (const ContentFilterStageRow &row : rows) {
        if (row.stage == stage) {
            return &row;
        }
    }
    return nullptr;
}

} // namespace

const char *contentFilterStageIdentifier(ContentFilterStage stage)
{
    const ContentFilterStageRow *row = stageRowOf(contentFilterStageTable(), stage);
    return row ? row->identifier : "unknown";
}

QString contentFilterStageLabel(ContentFilterStage stage)
{
    const ContentFilterStageRow *row = stageRowOf(contentFilterStageTable(), stage);
    return row ? row->label : QString();
}

QString contentFilterStageDescription(ContentFilterStage stage)
{
    const ContentFilterStageRow *row = stageRowOf(contentFilterStageTable(), stage);
    return row ? row->description : QString();
}

QVector<ContentFilterStage> contentFilterStageOrder()
{
    QVector<ContentFilterStage> order;
    for (const ContentFilterStageRow &row : contentFilterStageTable()) {
        order.append(row.stage);
    }
    return order;
}

int contentFilterStageIndex(ContentFilterStage stage)
{
    const QVector<ContentFilterStage> order = contentFilterStageOrder();
    for (int index = 0; index < order.size(); ++index) {
        if (order.at(index) == stage) {
            return index;
        }
    }
    return -1;
}

QVector<QString> validateContentFilterStageTable(const QVector<ContentFilterStageRow> &rows)
{
    QVector<QString> problems;

    // 规格规定的阶段集合。它来自**内置表**而不是参数：参数只负责「顺序对不对」，
    // 「总共有几个阶段」是规格事实。若连这一步也读参数，
    // 一份空表就能让自检全绿——那正是这条自检要防的东西。
    const QVector<ContentFilterStage> expected = contentFilterStageOrder();

    // 1) 齐全且不重复。
    QSet<int> seen;
    for (const ContentFilterStageRow &row : rows) {
        const int key = static_cast<int>(row.stage);
        if (seen.contains(key)) {
            problems.append(QStringLiteral("阶段表里 `%1` 出现了不止一次")
                                .arg(QString::fromLatin1(contentFilterStageIdentifier(row.stage))));
        }
        seen.insert(key);
    }
    if (rows.size() != expected.size()) {
        problems.append(QStringLiteral("阶段表应当是 %1 个阶段，实际是 %2 个")
                            .arg(expected.size())
                            .arg(rows.size()));
    }
    for (ContentFilterStage stage : expected) {
        if (!seen.contains(static_cast<int>(stage))) {
            problems.append(QStringLiteral("阶段表里缺少 `%1`")
                                .arg(QString::fromLatin1(contentFilterStageIdentifier(stage))));
        }
    }

    // 2) 顺序。**这是本表唯一真正要守的东西**，所以它单独一对断言：
    //    「行过滤在忽略规则之前」。顺序错了没有任何运行期现象——
    //    用户只会觉得「我改了忽略规则，行过滤的结果怎么跟着变了」。
    int lineIndex = -1;
    int ignoreIndex = -1;
    for (int index = 0; index < rows.size(); ++index) {
        if (rows.at(index).stage == ContentFilterStage::LineFilter) {
            lineIndex = index;
        } else if (rows.at(index).stage == ContentFilterStage::IgnoreRules) {
            ignoreIndex = index;
        }
    }
    if (lineIndex >= 0 && ignoreIndex >= 0 && lineIndex > ignoreIndex) {
        problems.append(QStringLiteral("阶段顺序错了：行过滤（第 %1 位）必须排在"
                                       "忽略规则（第 %2 位）之前——否则「哪些行参与比较」"
                                       "会随会话里开着的忽略规则变化")
                            .arg(lineIndex + 1)
                            .arg(ignoreIndex + 1));
    }

    // 3) 标签与说明（**从参数取**，这样一份写坏的表才影响得到这里）。
    for (const ContentFilterStageRow &row : rows) {
        const QString identifier = QString::fromLatin1(row.identifier ? row.identifier : "");
        if (row.label.isEmpty()) {
            problems.append(QStringLiteral("阶段 `%1` 没有标签").arg(identifier));
        }
        if (row.description.isEmpty()) {
            problems.append(QStringLiteral("阶段 `%1` 没有说明").arg(identifier));
        }
    }

    // 4) 标识符唯一且非空。
    QSet<QString> identifiers;
    for (const ContentFilterStageRow &row : rows) {
        const QString identifier = QString::fromLatin1(row.identifier ? row.identifier : "");
        if (identifier.isEmpty()) {
            problems.append(QStringLiteral("阶段表里有一个空标识符"));
            continue;
        }
        if (identifiers.contains(identifier)) {
            problems.append(QStringLiteral("阶段标识符 `%1` 重复").arg(identifier));
        }
        identifiers.insert(identifier);
    }

    return problems;
}

// -----------------------------------------------------------------------------
// 行过滤模式表
// -----------------------------------------------------------------------------

const QVector<LineFilterModeRow> &lineFilterModeTable()
{
    return builtinLineModeTable();
}

QVector<LineFilterMode> allLineFilterModes()
{
    QVector<LineFilterMode> modes;
    for (const LineFilterModeRow &row : lineFilterModeTable()) {
        modes.append(row.mode);
    }
    return modes;
}

const LineFilterModeRow *lineFilterModeRow(LineFilterMode mode)
{
    for (const LineFilterModeRow &row : lineFilterModeTable()) {
        if (row.mode == mode) {
            return &row;
        }
    }
    return nullptr;
}

const char *lineFilterModeIdentifier(LineFilterMode mode)
{
    const LineFilterModeRow *row = lineFilterModeRow(mode);
    return row ? row->identifier : "unknown";
}

QString lineFilterModeLabel(LineFilterMode mode)
{
    const LineFilterModeRow *row = lineFilterModeRow(mode);
    return row ? row->label : QString();
}

QString lineFilterModeExplanation(LineFilterMode mode)
{
    const LineFilterModeRow *row = lineFilterModeRow(mode);
    return row ? row->explanation : QString();
}

bool lineFilterModeMatchesWholeLine(LineFilterMode mode)
{
    const LineFilterModeRow *row = lineFilterModeRow(mode);
    return row ? row->wholeLine : true;
}

QString lineFilterModePreferredPrefix(LineFilterMode mode)
{
    const LineFilterModeRow *row = lineFilterModeRow(mode);
    if (!row || row->prefixes.isEmpty()) {
        return QString();
    }
    // 取第一个：表里每个模式的**第一个**前缀就是它的标准写法
    // （精确模式是 `= `、正则是 `re:`、通配符是空串）。
    return row->prefixes.first();
}

LineFilterModePrefixHit matchLineFilterModePrefix(const QString &line)
{
    LineFilterModePrefixHit hit;

    // 先跳过行首空白，再认前缀。理由写在头文件里：声明里缩进对齐是常态，
    // 而前缀没被认出来的后果是整行退化成通配模式（`=` 变成普通字符），
    // 规则看起来正常却永远不命中。`analyzeLineFilterLine` 靠 `textOffset`
    // 把列号算回原始行上，因此跳过缩进不会让报错位置漂移。
    const QString withoutLeft = ltrimAscii(line);
    const int indent = line.size() - withoutLeft.size();
    hit.textOffset = indent;

    // **最长前缀优先**：`= ` 必须在 `=` 之前被认出来，`re: ` 必须在 `re:` 之前。
    // 不按「表里的顺序」直接返回第一个能匹配的，是因为表是给人读的
    // （按模式分组、便于对照），而前缀匹配需要的是「最长优先」这个引擎侧的事实。
    // 两者一旦耦合，哪天有人为了排版把表行换个位置，列号就会悄悄偏一格。
    for (const LineFilterModeRow &row : lineFilterModeTable()) {
        for (const QString &prefix : row.prefixes) {
            if (prefix.isEmpty() || !withoutLeft.startsWith(prefix)) {
                // 空串是「无前缀」的声明方式，不是「任何一行都命中这个模式」。
                continue;
            }
            if (prefix.size() > hit.prefixLength) {
                hit.found = true;
                hit.mode = row.mode;
                hit.prefixLength = prefix.size();
                hit.textOffset = indent + prefix.size();
            }
        }
    }
    return hit;
}

QVector<QString> validateLineFilterModeTable(const QVector<LineFilterModeRow> &table)
{
    QVector<QString> problems;

    // 1) 三种模式齐全、不重复，且与 allLineFilterModes() 同序。
    const QVector<LineFilterMode> expected = allLineFilterModes();
    if (table.size() != expected.size()) {
        problems.append(QStringLiteral("模式表应当是 %1 种模式，实际是 %2 种")
                            .arg(expected.size())
                            .arg(table.size()));
    }
    QSet<int> seen;
    for (const LineFilterModeRow &row : table) {
        if (seen.contains(static_cast<int>(row.mode))) {
            problems.append(QStringLiteral("模式 `%1` 出现了不止一次")
                                .arg(QString::fromLatin1(lineFilterModeIdentifier(row.mode))));
        }
        seen.insert(static_cast<int>(row.mode));
    }
    for (LineFilterMode mode : expected) {
        if (!seen.contains(static_cast<int>(mode))) {
            problems.append(QStringLiteral("模式表里缺少 `%1`")
                                .arg(QString::fromLatin1(lineFilterModeIdentifier(mode))));
        }
    }

    // 2) 标签、说明、标识符。
    QSet<QString> identifiers;
    for (const LineFilterModeRow &row : table) {
        const QString identifier = QString::fromLatin1(row.identifier ? row.identifier : "");
        if (identifier.isEmpty()) {
            problems.append(QStringLiteral("模式表里有一个空标识符"));
        } else if (identifiers.contains(identifier)) {
            problems.append(QStringLiteral("模式标识符 `%1` 重复").arg(identifier));
        }
        identifiers.insert(identifier);

        if (row.label.isEmpty()) {
            problems.append(QStringLiteral("模式 `%1` 没有标签").arg(identifier));
        }
        if (row.explanation.isEmpty()) {
            problems.append(QStringLiteral("模式 `%1` 没有说明").arg(identifier));
        }
        if (row.prefixes.isEmpty()) {
            problems.append(QStringLiteral("模式 `%1` 连一个前缀都没声明——"
                                           "界面上就没有任何办法表达它")
                                .arg(identifier));
        }
        QSet<QString> ownPrefixes;
        for (const QString &prefix : row.prefixes) {
            if (ownPrefixes.contains(prefix)) {
                problems.append(QStringLiteral("模式 `%1` 的前缀 `%2` 重复")
                                    .arg(identifier, prefix));
            }
            ownPrefixes.insert(prefix);
        }
    }

    // 3) 同一个前缀不能属于两个模式。空串（无前缀）尤其只允许一个模式声明：
    //    两个模式都声明空串的话，「这一行用哪种模式」就没有答案了。
    QHash<QString, QString> prefixOwner;
    for (const LineFilterModeRow &row : table) {
        const QString owner = QString::fromLatin1(lineFilterModeIdentifier(row.mode));
        for (const QString &prefix : row.prefixes) {
            if (prefixOwner.contains(prefix)) {
                problems.append(QStringLiteral("前缀 `%1` 同时属于 `%2` 与 `%3`")
                                    .arg(prefix, prefixOwner.value(prefix), owner));
                continue;
            }
            prefixOwner.insert(prefix, owner);
        }
    }

    // 4) 匹配对象：`Regex` 必须是子串匹配，`Exact` / `Wildcard` 必须是整行匹配。
    //    **这一条不是形式检查**：第 1 条往「整行锚定」方向改一行代码，
    //    用户写的 `re:INFO`（想丢掉含 INFO 的行）就会永远不命中，
    //    而界面上完全看不出为什么。反过来把通配符改成子串匹配，
    //    `*tmp*` 会开始误伤别的行。两个方向都必须有断言。
    //
    //    这一段的取行**必须走参数** `table`。走模块内置表的写法（最初那一版
    //    在 stageTable 上就是这么写的）会让一份故意写坏的表影响不到自检——
    //    一条永远不会红的护栏，见 §6 里 M27 那次漏检。
    for (LineFilterMode mode : {LineFilterMode::Exact, LineFilterMode::Wildcard}) {
        for (const LineFilterModeRow &row : table) {
            if (row.mode == mode && !row.wholeLine) {
                problems.append(QStringLiteral("`%1` 必须是整行匹配")
                                    .arg(lineFilterModeLabel(mode)));
            }
        }
    }
    for (const LineFilterModeRow &row : table) {
        if (row.mode == LineFilterMode::Regex && row.wholeLine) {
            problems.append(QStringLiteral("`正则行` 必须是**子串**匹配——"
                                           "整行锚定会让 `re:INFO` 这类写法永远不命中"));
        }
    }

    return problems;
}

// -----------------------------------------------------------------------------
// 行过滤问题
// -----------------------------------------------------------------------------

const char *lineFilterIssueKindIdentifier(LineFilterIssueKind kind)
{
    switch (kind) {
    case LineFilterIssueKind::Syntax:
        return "syntax";
    case LineFilterIssueKind::Empty:
        return "empty";
    case LineFilterIssueKind::Risky:
        return "risky";
    }
    return "unknown";
}

QString lineFilterIssueKindLabel(LineFilterIssueKind kind)
{
    switch (kind) {
    case LineFilterIssueKind::Syntax:
        return QStringLiteral("语法错");
    case LineFilterIssueKind::Empty:
        return QStringLiteral("空表达式");
    case LineFilterIssueKind::Risky:
        return QStringLiteral("可能很慢");
    }
    return QString();
}

QString LineFilterIssue::describe() const
{
    QString where;
    if (line > 0) {
        where = QStringLiteral("第 %1 行").arg(line);
        if (column >= 0) {
            where += QStringLiteral("第 %1 列").arg(column + 1);
        }
        where += QStringLiteral("：");
    }

    QString text = where + lineFilterIssueKindLabel(kind) + QStringLiteral("：") + message;
    if (!hint.isEmpty()) {
        text += QStringLiteral("（") + hint + QStringLiteral("）");
    }
    return text;
}

int LineFilterProblems::countOfKind(LineFilterIssueKind kind) const
{
    int count = 0;
    for (const LineFilterIssue &issue : issues) {
        if (issue.kind == kind) {
            ++count;
        }
    }
    return count;
}

QString LineFilterProblems::describe() const
{
    QStringList lines;
    for (const LineFilterIssue &issue : issues) {
        lines.append(issue.describe());
    }
    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 单行分析
// -----------------------------------------------------------------------------

QString LineFilterPattern::toDeclarationText() const
{
    const QString prefix = lineFilterModePreferredPrefix(mode);
    if (mode == LineFilterMode::Exact) {
        // 精确模式即使前缀是 `= `，写回时也保留那个空格：`=foo` 与 `= foo`
        // 读起来一样，但少一个空格会让声明文本「看起来像少打了什么」。
        return QStringLiteral("= ") + pattern;
    }
    if (prefix.isEmpty()) {
        return pattern;
    }
    return prefix + pattern;
}

namespace {

LineFilterIssue makeLineIssue(LineFilterIssueKind kind, int line, int column, int length,
                              const QString &text, const QString &message, const QString &hint)
{
    LineFilterIssue issue;
    issue.kind = kind;
    issue.line = line;
    issue.column = column;
    issue.length = length;
    issue.text = text;
    issue.message = message;
    issue.hint = hint;
    return issue;
}

} // namespace

LineFilterLineAnalysis analyzeLineFilterLine(const QString &line, int lineNumber)
{
    LineFilterLineAnalysis analysis;

    const QString withoutLeft = ltrimAscii(line);
    if (withoutLeft.isEmpty() || withoutLeft.startsWith(QLatin1Char('#'))) {
        return analysis; // 空行与注释：既不是表达式，也不是错误
    }

    const LineFilterModePrefixHit hit = matchLineFilterModePrefix(line);
    const LineFilterMode mode = hit.found ? hit.mode : LineFilterMode::Wildcard;

    // 列号指向**表达式正文的第一个字符**，而不是前缀末尾。报在空格上会让
    // 界面标红一个空白，用户看不出自己哪里错了。与 analyzeNameFilterLine
    // 的做法一致；前缀匹配已经把行首缩进算进 `textOffset`，这里再跨过前缀与
    // 正文之间的空白。
    int textColumn = hit.textOffset;
    while (textColumn < line.size() && line.at(textColumn).isSpace()) {
        ++textColumn;
    }
    const QString text = trimBoth(line.mid(textColumn));

    LineFilterPattern pattern;
    pattern.line = lineNumber;
    pattern.column = textColumn;
    pattern.mode = mode;
    pattern.source = line;
    pattern.pattern = text;

    if (text.isEmpty()) {
        // 空表达式是**错误**而不是「匹配空行的规则」。理由：一行只剩下 `=`
        // 看着像没写完，而它一旦被当成「去掉所有空行」的规则生效，
        // 用户会看到一批空行凭空消失，却没有任何地方提示过他生效了。
        // 想去掉空行有明确的写法 `re:^$`，报错里直接给出。
        //
        // 注意这里仍然把**模式与前缀信息**填进 `analysis.pattern`：
        // 界面要能说「你用的是正则模式，但后面没写东西」而不是一句笼统的
        // 「这一行有问题」。`hasPattern` 保持为假，因此它不会被拿去过滤。
        analysis.pattern = pattern;
        analysis.issues.append(makeLineIssue(
            LineFilterIssueKind::Empty, lineNumber, textColumn,
            qMax(1, line.size() - textColumn), line.mid(textColumn),
            QStringLiteral("这一行只有模式前缀、没有表达式"),
            QStringLiteral("想去掉空行请写 `re:^$`（含空白的行写 `re:^\\s*$`）")));
        return analysis;
    }

    switch (mode) {
    case LineFilterMode::Exact:
        // 精确行是字面量，没有「语法」可错——任何字符都是合法的一行内容。
        break;

    case LineFilterMode::Wildcard: {
        const MaskParseResult compiled = Mask::compile(text);
        if (!compiled.ok()) {
            const MaskParseError &error = compiled.error;
            analysis.issues.append(makeLineIssue(
                LineFilterIssueKind::Syntax, lineNumber,
                textColumn + qMax(0, error.column), qMax(1, error.length), text,
                error.message, error.hint));
            return analysis;
        }
        pattern.compiledMask = compiled.mask;
        break;
    }

    case LineFilterMode::Regex: {
        // 大小写不在这里定：行过滤器目前没有大小写开关（掩码侧才有），
        // 因此编译一次即可，不需要像 namefilter 那样留到判定期再应用。
        // 将来若要加开关，改法与 namefilter 相同——不要在这里编进去。
        QRegularExpression regex(text);
        if (!regex.isValid()) {
            analysis.issues.append(makeLineIssue(
                LineFilterIssueKind::Syntax, lineNumber,
                textColumn + qMax(0, regex.patternErrorOffset()),
                qMax(1, text.size()), text,
                QStringLiteral("正则表达式写错了：%1").arg(regex.errorString()),
                QStringLiteral("确认括号、方括号与量词是否配对")));
            return analysis;
        }
        pattern.compiledRegex = regex;
        break;
    }
    }

    // 回溯风险预检只对正则做，而且**只提示、不阻断**：用户可能真的知道自己在
    // 写什么（例如对着很短的样本写）。复用的是 namefilter 的那份实现，
    // 刻意不再写第二份——两份必然演化成「一个报一个不报」。
    if (mode == LineFilterMode::Regex) {
        const QVector<RegexRisk> risks = analyzeRegexPatternRisk(text);
        for (const RegexRisk &risk : risks) {
            analysis.issues.append(makeLineIssue(
                LineFilterIssueKind::Risky, lineNumber, textColumn + risk.column,
                qMax(1, risk.length), risk.snippet,
                QStringLiteral("这段正则可能灾难性回溯：%1").arg(risk.reason),
                QStringLiteral("把嵌套量词改成 `(a|b)*` 这类写法，或缩小匹配范围")));
        }
    }

    pattern.valid = true;
    analysis.hasPattern = true;
    analysis.pattern = pattern;
    return analysis;
}

// -----------------------------------------------------------------------------
// LineFilter
// -----------------------------------------------------------------------------

LineFilterParseResult LineFilter::parseDeclaration(const QString &declaration)
{
    LineFilterParseResult result;

    const QStringList lines = splitDeclarationLines(declaration);
    for (int index = 0; index < lines.size(); ++index) {
        const LineFilterLineAnalysis analysis =
            analyzeLineFilterLine(lines.at(index), index + 1);
        for (const LineFilterIssue &issue : analysis.issues) {
            result.issues.append(issue);
        }
        if (analysis.hasPattern) {
            result.filter.addPattern(analysis.pattern);
        }
    }
    return result;
}

void LineFilter::addPattern(const LineFilterPattern &pattern)
{
    m_patterns.append(pattern);
}

QString LineFilter::toDeclarationText() const
{
    QStringList lines;
    for (const LineFilterPattern &pattern : m_patterns) {
        lines.append(pattern.toDeclarationText());
    }
    return lines.join(QLatin1Char('\n'));
}

bool LineFilter::excludes(const QString &rawLine) const
{
    // **不**在这里对空行提前返回。曾经的写法是 `if (rawLine.isEmpty()) return false;`，
    // 理由是「掩码命中不了空行」——但那顺手把精确与正则两种模式也一起挡掉了，
    // 于是 `re:^$`（去掉空行的唯一明确写法，报错提示里就是这么教用户的）
    // 永远不生效。空行能不能命中是**每一种模式自己的事**：
    //   精确 = 字符串全等（`= ` 配空表达式在解析期就报 Empty，走不到这里）；
    //   通配 = 掩码语言把空名字视为无效，命中不了（公开代价，见头文件）；
    //   正则 = 照常匹配，`re:^$` 命中。
    for (const LineFilterPattern &pattern : m_patterns) {
        if (!pattern.valid) {
            continue;
        }
        switch (pattern.mode) {
        case LineFilterMode::Exact:
            if (rawLine == pattern.pattern) {
                return true;
            }
            break;

        case LineFilterMode::Wildcard: {
            // **整行同时当作「名字」与「路径」交给掩码。**
            //
            // 刻意**不用** `MaskSubject::forName(rawLine)`：那个函数会先按 `/`
            // 取最后一段当作名字（它假定调用方给的是一个路径），
            // 用在行过滤上会让 `*.cpp` 去匹配一行的最后一段，与注释里写的
            // 「整行匹配」不符。手工构造这两个字段之后，掩码语言那两条既有规则
            // 原样成立：不含 `/` 的掩码按名字（= 整行）匹配，
            // 含 `/` 的掩码按 `/` 分段匹配。
            MaskSubject subject;
            subject.name = rawLine;
            subject.path = rawLine;
            if (pattern.compiledMask.matches(subject, Qt::CaseSensitive)) {
                return true;
            }
            break;
        }

        case LineFilterMode::Regex: {
            // 子串匹配：不调用 anchoredPattern()（那会改变用户自己锚点的语义，
            // 见 §6 那条坑），也不需要——QRegularExpression::match() 默认就从
            // 任意位置开始找。
            if (pattern.compiledRegex.match(rawLine).hasMatch()) {
                return true;
            }
            break;
        }
        }
    }
    return false;
}

QVector<int> LineFilter::matchingPatternIndexes(const QString &rawLine) const
{
    QVector<int> indexes;
    for (int index = 0; index < m_patterns.size(); ++index) {
        const LineFilterPattern &pattern = m_patterns.at(index);
        if (!pattern.valid) {
            continue;
        }
        bool hit = false;
        switch (pattern.mode) {
        case LineFilterMode::Exact:
            hit = (rawLine == pattern.pattern);
            break;
        case LineFilterMode::Wildcard: {
            MaskSubject subject;
            subject.name = rawLine;
            subject.path = rawLine;
            hit = pattern.compiledMask.matches(subject, Qt::CaseSensitive);
            break;
        }
        case LineFilterMode::Regex:
            hit = pattern.compiledRegex.match(rawLine).hasMatch();
            break;
        }
        if (hit) {
            indexes.append(index);
        }
    }
    return indexes;
}

LineFilterResult LineFilter::filterLines(const QStringList &rawLines) const
{
    LineFilterResult result;
    result.inputLineCount = rawLines.size();
    result.hitsByPattern = QVector<int>(m_patterns.size(), 0);

    for (int index = 0; index < rawLines.size(); ++index) {
        const QString &line = rawLines.at(index);
        const QVector<int> hits = matchingPatternIndexes(line);
        if (hits.isEmpty()) {
            result.lines.append(line);
            continue;
        }
        result.droppedLines.append(line);
        result.droppedLineNumbers.append(index + 1);
        for (int patternIndex : hits) {
            result.hitsByPattern[patternIndex] += 1;
        }
    }
    return result;
}

QString LineFilter::describe() const
{
    return QStringLiteral("行过滤：%1 条表达式").arg(m_patterns.size());
}

QString LineFilterResult::summary() const
{
    return QStringLiteral("内容过滤：丢弃 %1 行 / 共 %2 行")
        .arg(droppedLineCount())
        .arg(inputLineCount);
}

QString LineFilterParseResult::describeErrors() const
{
    QStringList lines;
    for (const LineFilterIssue &issue : issues) {
        lines.append(issue.describe());
    }
    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 关键字节：转义
// -----------------------------------------------------------------------------

QByteArray decodeByteSequenceText(const QString &text, QString *error, int *errorColumn)
{
    QByteArray out;
    if (error) {
        error->clear();
    }
    if (errorColumn) {
        *errorColumn = -1;
    }

    const auto fail = [&](int column, const QString &message) {
        if (error) {
            *error = message;
        }
        if (errorColumn) {
            *errorColumn = column;
        }
        return QByteArray();
    };

    int index = 0;
    while (index < text.size()) {
        const QChar character = text.at(index);

        if (character != QLatin1Char('\\')) {
            // 非转义字符按 UTF-8 编码。理由见头文件：声明文本已经是
            // 「正确解码后的文本」，再按本机编码编回去会让同一份设置文件
            // 在两台机器上给出不同的字节序列，而两边都看不出差别。
            int end = index + 1;
            // 代理对要一起编，否则 UTF-8 编码出来是两个替换字符。
            if (character.isHighSurrogate() && end < text.size()
                && text.at(end).isLowSurrogate()) {
                ++end;
            }
            const QString chunk = text.mid(index, end - index);
            const QByteArray encoded = chunk.toUtf8();
            if (encoded.isEmpty()) {
                // toUtf8() 把未配对代理换成 U+FFFD 时会得到非空结果，
                // 真正为空只可能是空串——这里不可能，兜底报错而不是静默跳过。
                return fail(index, QStringLiteral("这个位置的字符无法编码为 UTF-8"));
            }
            out.append(encoded);
            index = end;
            continue;
        }

        // 转义序列。
        if (index + 1 >= text.size()) {
            return fail(index, QStringLiteral("反斜杠后面什么都没有——"
                                              "要写一个反斜杠本身请用 `\\\\`"));
        }

        const QChar next = text.at(index + 1);
        switch (next.unicode()) {
        case 'n':
            out.append('\n');
            index += 2;
            continue;
        case 'r':
            out.append('\r');
            index += 2;
            continue;
        case 't':
            out.append('\t');
            index += 2;
            continue;
        case '0':
            out.append('\0');
            index += 2;
            continue;
        case '\\':
            out.append('\\');
            index += 2;
            continue;
        case 'x': {
            // **必须**紧跟两位十六进制。不采用「能吃几位吃几位」的写法：
            // `\x0a1b` 在两种读法下分别是 `0x0A` + 字面 `1b` 与 `0x0A1B`
            // （后者根本不是字节），用户完全看不出自己写的是哪一种。
            if (index + 3 >= text.size() || !isHexDigit(text.at(index + 2))
                || !isHexDigit(text.at(index + 3))) {
                return fail(index, QStringLiteral("`\\x` 后面必须紧跟两位十六进制"
                                                  "（如 `\\x0D`）"));
            }
            const int value = hexValue(text.at(index + 2)) * 16 + hexValue(text.at(index + 3));
            out.append(static_cast<char>(value));
            index += 4;
            continue;
        }
        default:
            return fail(index, QStringLiteral("不认识的转义 `\\%1`——"
                                              "认识的只有 `\\xHH` 与 `\\n \\r \\t \\0 \\\\`")
                                     .arg(next));
        }
    }

    return out;
}

QString encodeByteSequenceText(const QByteArray &bytes)
{
    QString out;
    for (int index = 0; index < bytes.size(); ++index) {
        const char value = bytes.at(index);
        const unsigned char byte = static_cast<unsigned char>(value);
        // 可见 ASCII（含空格）原样写；其余一律 `\xHH`。
        //
        // 刻意**不**把 `0x0A` 原样吐成换行：往返回来的序列里会多一个真实换行，
        // 而它在编辑器里跟转义写法长得完全不一样——如果两段文本都原样输出，
        // 反而会需要一条「多出来的换行」的特殊处理。全转义最省事也最不容易错。
        if (byte >= 0x20 && byte <= 0x7E) {
            out.append(QChar(static_cast<ushort>(byte)));
        } else {
            out.append(hexEscape(value));
        }
    }
    return out;
}

// -----------------------------------------------------------------------------
// 关键字节：表
// -----------------------------------------------------------------------------

const QVector<KeyByteCombineModeRow> &keyByteCombineModeTable()
{
    // 理由同 `contentFilterStageTable()`：行地址会交出去，表必须是函数内静态。
    static const QVector<KeyByteCombineModeRow> table = QVector<KeyByteCombineModeRow>{
        KeyByteCombineModeRow{KeyByteCombineMode::AnyOf, "any-of", "any-of",
                              QStringLiteral("含任意一条"),
                              QStringLiteral("含任意一条字节序列即纳入比较")},
        KeyByteCombineModeRow{KeyByteCombineMode::AllOf, "all-of", "all-of",
                              QStringLiteral("全部含齐"),
                              QStringLiteral("必须把每一条字节序列都含齐才纳入比较")},
    };
    return table;
}

QVector<KeyByteCombineMode> allKeyByteCombineModes()
{
    QVector<KeyByteCombineMode> modes;
    for (const KeyByteCombineModeRow &row : keyByteCombineModeTable()) {
        modes.append(row.mode);
    }
    return modes;
}

const KeyByteCombineModeRow *keyByteCombineModeRow(KeyByteCombineMode mode)
{
    for (const KeyByteCombineModeRow &row : keyByteCombineModeTable()) {
        if (row.mode == mode) {
            return &row;
        }
    }
    return nullptr;
}

const char *keyByteCombineModeIdentifier(KeyByteCombineMode mode)
{
    const KeyByteCombineModeRow *row = keyByteCombineModeRow(mode);
    return row ? row->identifier : "unknown";
}

QString keyByteCombineModeLabel(KeyByteCombineMode mode)
{
    const KeyByteCombineModeRow *row = keyByteCombineModeRow(mode);
    return row ? row->label : QString();
}

QString keyByteCombineModeExplanation(KeyByteCombineMode mode)
{
    const KeyByteCombineModeRow *row = keyByteCombineModeRow(mode);
    return row ? row->explanation : QString();
}

QVector<QString> validateKeyByteCombineModeTable(const QVector<KeyByteCombineModeRow> &table)
{
    QVector<QString> problems;

    const QVector<KeyByteCombineMode> expected = allKeyByteCombineModes();
    if (table.size() != expected.size()) {
        problems.append(QStringLiteral("组合语义表应当是 %1 种，实际是 %2 种")
                            .arg(expected.size())
                            .arg(table.size()));
    }

    QSet<int> seen;
    QSet<QString> identifiers;
    QSet<QString> keys;
    for (const KeyByteCombineModeRow &row : table) {
        if (seen.contains(static_cast<int>(row.mode))) {
            problems.append(QStringLiteral("组合语义 `%1` 出现了不止一次")
                                .arg(QString::fromLatin1(keyByteCombineModeIdentifier(row.mode))));
        }
        seen.insert(static_cast<int>(row.mode));

        const QString identifier = QString::fromLatin1(row.identifier ? row.identifier : "");
        const QString key = QString::fromLatin1(row.key ? row.key : "");
        if (identifier.isEmpty()) {
            problems.append(QStringLiteral("组合语义表里有一个空标识符"));
        } else if (identifiers.contains(identifier)) {
            problems.append(QStringLiteral("组合语义标识符 `%1` 重复").arg(identifier));
        }
        identifiers.insert(identifier);

        if (key.isEmpty()) {
            problems.append(QStringLiteral("组合语义 `%1` 没有设置键名").arg(identifier));
        } else if (keys.contains(key)) {
            problems.append(QStringLiteral("组合语义键名 `%1` 重复").arg(key));
        }
        keys.insert(key);

        if (row.label.isEmpty()) {
            problems.append(QStringLiteral("组合语义 `%1` 没有标签").arg(identifier));
        }
        if (row.explanation.isEmpty()) {
            problems.append(QStringLiteral("组合语义 `%1` 没有说明").arg(identifier));
        }
    }
    for (KeyByteCombineMode mode : expected) {
        if (!seen.contains(static_cast<int>(mode))) {
            problems.append(QStringLiteral("组合语义表里缺少 `%1`")
                                .arg(QString::fromLatin1(keyByteCombineModeIdentifier(mode))));
        }
    }

    return problems;
}

// -----------------------------------------------------------------------------
// 关键字节：问题与结论
// -----------------------------------------------------------------------------

const char *keyByteIssueKindIdentifier(KeyByteIssueKind kind)
{
    switch (kind) {
    case KeyByteIssueKind::Syntax:
        return "syntax";
    case KeyByteIssueKind::Empty:
        return "empty";
    case KeyByteIssueKind::Duplicate:
        return "duplicate";
    }
    return "unknown";
}

QString keyByteIssueKindLabel(KeyByteIssueKind kind)
{
    switch (kind) {
    case KeyByteIssueKind::Syntax:
        return QStringLiteral("语法错");
    case KeyByteIssueKind::Empty:
        return QStringLiteral("空序列");
    case KeyByteIssueKind::Duplicate:
        return QStringLiteral("重复");
    }
    return QString();
}

QString KeyByteIssue::describe() const
{
    QString where;
    if (line > 0) {
        where = QStringLiteral("第 %1 行").arg(line);
        if (column >= 0) {
            where += QStringLiteral("第 %1 列").arg(column + 1);
        }
        where += QStringLiteral("：");
    }
    QString text = where + keyByteIssueKindLabel(kind) + QStringLiteral("：") + message;
    if (!hint.isEmpty()) {
        text += QStringLiteral("（") + hint + QStringLiteral("）");
    }
    return text;
}

const char *contentKindIdentifier(ContentKind kind)
{
    return kind == ContentKind::Binary ? "binary" : "text";
}

QString contentKindLabel(ContentKind kind)
{
    return kind == ContentKind::Binary ? QStringLiteral("二进制") : QStringLiteral("文本");
}

const char *keyByteOutcomeIdentifier(KeyByteOutcome outcome)
{
    switch (outcome) {
    case KeyByteOutcome::Accepted:
        return "accepted";
    case KeyByteOutcome::Rejected:
        return "rejected";
    case KeyByteOutcome::NotApplicable:
        return "not-applicable";
    }
    return "unknown";
}

QString keyByteOutcomeLabel(KeyByteOutcome outcome)
{
    switch (outcome) {
    case KeyByteOutcome::Accepted:
        return QStringLiteral("纳入比较");
    case KeyByteOutcome::Rejected:
        return QStringLiteral("不纳入比较");
    case KeyByteOutcome::NotApplicable:
        return QStringLiteral("不适用");
    }
    return QString();
}

QString KeyByteDecision::describe() const
{
    QString text = keyByteOutcomeLabel(outcome);
    if (!reason.isEmpty()) {
        text += QStringLiteral("：") + reason;
    }
    return text;
}

// -----------------------------------------------------------------------------
// 关键字节：过滤器
// -----------------------------------------------------------------------------

QString KeyByteSequence::toDeclarationText() const
{
    return encodeByteSequenceText(bytes);
}

KeyByteParseResult KeyByteFilter::parseDeclaration(const QString &declaration)
{
    KeyByteParseResult result;

    const QStringList lines = splitDeclarationLines(declaration);
    QSet<QByteArray> seen;

    for (int index = 0; index < lines.size(); ++index) {
        const int lineNumber = index + 1;
        const QString withoutLeft = ltrimAscii(lines.at(index));
        if (withoutLeft.isEmpty() || withoutLeft.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const int column = lines.at(index).size() - withoutLeft.size();
        const QString payload = trimBoth(withoutLeft);

        QString error;
        int errorColumn = -1;
        const QByteArray bytes = decodeByteSequenceText(payload, &error, &errorColumn);
        if (!error.isEmpty()) {
            KeyByteIssue issue;
            issue.kind = KeyByteIssueKind::Syntax;
            issue.line = lineNumber;
            issue.column = column + qMax(0, errorColumn);
            issue.length = qMax(1, payload.size() - qMax(0, errorColumn));
            issue.text = payload;
            issue.message = error;
            issue.hint = QStringLiteral("可见字符可以直接写，其余用 `\\xHH`");
            result.issues.append(issue);
            continue;
        }

        if (bytes.isEmpty()) {
            // 只有一种可能：这一行全是转义、但解出来是空（例如注释里那种）。
            // 真正「一行什么都没有」的情况在上面就被跳过了。
            KeyByteIssue issue;
            issue.kind = KeyByteIssueKind::Empty;
            issue.line = lineNumber;
            issue.column = column;
            issue.length = qMax(1, payload.size());
            issue.text = payload;
            issue.message = QStringLiteral("这一行没有解出任何字节");
            issue.hint = QStringLiteral("删掉这一行，或写上有意义的字节序列");
            result.issues.append(issue);
            continue;
        }

        if (seen.contains(bytes)) {
            // 重复不是错：多写一条一模一样的序列，结论不会变。
            // 但它是**唯一**能让「这一条从来没起过作用」被看出来的地方，
            // 所以报一条 Duplicate 而不是静静吃掉。
            KeyByteIssue issue;
            issue.kind = KeyByteIssueKind::Duplicate;
            issue.line = lineNumber;
            issue.column = column;
            issue.length = qMax(1, payload.size());
            issue.text = payload;
            issue.message = QStringLiteral("这一条与前面某一条完全相同，永远不会改变结论");
            issue.hint = QStringLiteral("删掉它，或改成别的序列");
            result.issues.append(issue);
        }
        seen.insert(bytes);

        KeyByteSequence sequence;
        sequence.line = lineNumber;
        sequence.column = column;
        sequence.source = lines.at(index);
        sequence.bytes = bytes;
        result.filter.addSequence(sequence);
    }

    return result;
}

void KeyByteFilter::addSequence(const KeyByteSequence &sequence)
{
    m_sequences.append(sequence);
}

QString KeyByteFilter::toDeclarationText() const
{
    QStringList lines;
    for (const KeyByteSequence &sequence : m_sequences) {
        lines.append(sequence.toDeclarationText());
    }
    return lines.join(QLatin1Char('\n'));
}

KeyByteDecision KeyByteFilter::decide(const QByteArray &data, ContentKind kind) const
{
    KeyByteDecision decision;

    // 第 2 条的原话是「仅当**二进制文件**包含指定字节序列时才纳入比较」。
    // 文本文件走这一支时结论是「不适用」而不是「逐字节搜一遍」：
    // 后者看起来也对，但一个中文文本里搜 `E4` 会命中某个汉字的第一个字节，
    // 于是「按文件类型过滤」会悄悄变成「按字节碰运气」。
    if (kind != ContentKind::Binary) {
        decision.outcome = KeyByteOutcome::NotApplicable;
        decision.reason = QStringLiteral("输入是文本文件，关键字节过滤只对二进制生效");
        return decision;
    }

    if (m_sequences.isEmpty()) {
        // 空规则集 = 没有约束。把它直译成「谁都不含 → 全部排除」会让用户的
        // 目录直接变空，而界面上只多了一行没人在意的提示。与 MaskFilter /
        // NameFilter 的「空过滤器保留条目」是同一条纪律。
        decision.outcome = KeyByteOutcome::Accepted;
        decision.reason.clear();
        return decision;
    }

    if (m_combineMode == KeyByteCombineMode::AnyOf) {
        for (int index = 0; index < m_sequences.size(); ++index) {
            if (data.contains(m_sequences.at(index).bytes)) {
                decision.outcome = KeyByteOutcome::Accepted;
                decision.matchedSequenceIndex = index;
                decision.reason = QStringLiteral("含 `%1`")
                                      .arg(encodeByteSequenceText(m_sequences.at(index).bytes));
                return decision;
            }
        }
        decision.outcome = KeyByteOutcome::Rejected;
        decision.reason = QStringLiteral("不含任何一条指定的字节序列（共 %1 条）")
                              .arg(m_sequences.size());
        return decision;
    }

    for (int index = 0; index < m_sequences.size(); ++index) {
        if (!data.contains(m_sequences.at(index).bytes)) {
            decision.outcome = KeyByteOutcome::Rejected;
            decision.reason = QStringLiteral("缺少第 %1 条 `%2`")
                                  .arg(index + 1)
                                  .arg(encodeByteSequenceText(m_sequences.at(index).bytes));
            return decision;
        }
        decision.matchedSequenceIndex = index;
    }
    decision.outcome = KeyByteOutcome::Accepted;
    decision.reason = QStringLiteral("含齐全部 %1 条字节序列").arg(m_sequences.size());
    return decision;
}

QString KeyByteFilter::describe() const
{
    return QStringLiteral("关键字节：%1（%2 条）")
        .arg(keyByteCombineModeLabel(m_combineMode))
        .arg(m_sequences.size());
}

QString KeyByteParseResult::describeErrors() const
{
    QStringList lines;
    for (const KeyByteIssue &issue : issues) {
        lines.append(issue.describe());
    }
    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 启用状态与提示
// -----------------------------------------------------------------------------

QString contentFilterPerformanceNotice()
{
    return QStringLiteral("内容过滤已启用，比较速度会降低");
}

QString contentFilterEnablementNotice(const ContentFilterEnablement &state)
{
    if (!state.enabled && !state.hasRules) {
        return QStringLiteral("内容过滤未启用，也没有配置规则。");
    }
    if (!state.enabled && state.hasRules) {
        // 这一条最要紧：用户配完规则以为生效了，结果那些行还在结果里，
        // 而他唯一的线索就是这句话没出现过。
        return QStringLiteral("已配置内容过滤规则，但未启用，比较时不会读取文件内容。");
    }
    if (state.enabled && !state.hasRules) {
        return QStringLiteral("内容过滤已启用，但没有配置任何规则，不会产生任何影响。");
    }
    return contentFilterPerformanceNotice();
}

QString lineFilterDeclarationKey()
{
    return QStringLiteral("line-filter");
}

QString keyByteFilterDeclarationKey()
{
    return QStringLiteral("key-byte-filter");
}

QString contentFilterEnabledKey()
{
    return QStringLiteral("content-filter-enabled");
}

// -----------------------------------------------------------------------------
// 表自检
// -----------------------------------------------------------------------------

QVector<QString> validateContentFilterTables(
    const QVector<ContentFilterStageRow> &stages,
    const QVector<LineFilterModeRow> &lineModes,
    const QVector<KeyByteCombineModeRow> &combineModes)
{
    QVector<QString> problems;
    problems += validateContentFilterStageTable(stages);
    problems += validateLineFilterModeTable(lineModes);
    problems += validateKeyByteCombineModeTable(combineModes);
    return problems;
}

QVector<QString> validateBuiltinContentFilterTables()
{
    return validateContentFilterTables(contentFilterStageTable(), lineFilterModeTable(),
                                       keyByteCombineModeTable());
}

} // namespace Filter
} // namespace LqCompare
