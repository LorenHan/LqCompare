#include "tst_contentfilter.h"

#include "contentfilter.h"
#include "namefilter.h" // 只为断言「新键不与既有过滤器的键撞车」
#include "textdiff.h"
#include "textdocument.h"

#include <QFile>

using namespace LqCompare;
using namespace LqCompare::Filter;

namespace {

/// `QCOMPARE(a, QStringList{"x","y"})` 报 too many arguments：宏参数里只有圆括号
/// 能保护逗号，花括号不算。外面包一层（本仓既有套件的同一手法，见 §6）。
QStringList asList(const QStringList &items)
{
    return items;
}

/// 按内容过滤真正的输入形态切行：**原始行**，不做任何规范化。
QStringList rawLines(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

/// 解析一份声明并只取过滤器。写错的行会被丢掉，
/// 「解析报了什么错」由 C 组单独断言。
LineFilter filterOf(const QString &declaration)
{
    return LineFilter::parseDeclaration(declaration).filter;
}

KeyByteFilter keyFilterOf(const QString &declaration, KeyByteCombineMode mode)
{
    KeyByteFilter filter = KeyByteFilter::parseDeclaration(declaration).filter;
    filter.setCombineMode(mode);
    return filter;
}

Text::Result compareText(const QStringList &left, const QStringList &right,
                         const Text::CompareOptions &options)
{
    return Text::compare(Text::Document::splitLines(left.join(QLatin1Char('\n'))),
                         Text::Document::splitLines(right.join(QLatin1Char('\n'))), options);
}

// 三张表各取一份**可改的副本**：自检必须是「表当参数」才守得住东西，
// 而验证它真会报的唯一办法就是传一份故意写坏的表进去。内置表是函数内静态常量，
// 所以这里 copy 一份再改。
QVector<ContentFilterStageRow> stageRows()
{
    return contentFilterStageTable();
}

QVector<LineFilterModeRow> lineModeRows()
{
    return lineFilterModeTable();
}

QVector<KeyByteCombineModeRow> combineRows()
{
    return keyByteCombineModeTable();
}

QString modeId(LineFilterMode mode)
{
    return QString::fromLatin1(lineFilterModeIdentifier(mode));
}

QString stageId(ContentFilterStage stage)
{
    return QString::fromLatin1(contentFilterStageIdentifier(stage));
}

QString combineId(KeyByteCombineMode mode)
{
    return QString::fromLatin1(keyByteCombineModeIdentifier(mode));
}

QString outcomeId(KeyByteOutcome outcome)
{
    return QString::fromLatin1(keyByteOutcomeIdentifier(outcome));
}

int indexOfMode(const QVector<LineFilterModeRow> &rows, LineFilterMode mode)
{
    for (int index = 0; index < rows.size(); ++index) {
        if (rows.at(index).mode == mode) {
            return index;
        }
    }
    return -1;
}

/// 去掉 C++ 注释（`//` 行注释与 `/* */` 块注释）。
///
/// **为什么护栏必须做这一步**：`contentfilter.h` 顶部那段说明里逐字写着
/// 「出现 `QFile` / `readAll` / … 即红」——护栏一上线就把自己的说明文档
/// 当成了违规，报出一个假错。本仓已经踩过一次同源的坑（见 §6：
/// `check_icons.py` 的引用正则**会扫注释**，我在注释里写下一个完整的资源路径
/// 字面量，护栏立刻报「代码引用了它，但 qrc 未声明」）。
///
/// 这里的做法与 `check_icons.py` 的「注释里用尖括号占位」不同：那条要求是
/// **写注释的人**记住一个隐式约定，而这里把约定变成代码。代价是字符串字面量
/// 里的 `//`（例如 URL）会把该行后面一起吞掉——对本护栏要抓的那些标识符
/// （`QFile` / `readAll` / `QDir` …）来说这个漏检面可以忽略，而且它只会
/// 导致**漏报**不会导致误报。
QString stripComments(const QString &text)
{
    QString out;
    int index = 0;
    while (index < text.size()) {
        const QChar character = text.at(index);

        if (character == QLatin1Char('/') && index + 1 < text.size()
            && text.at(index + 1) == QLatin1Char('/')) {
            while (index < text.size() && text.at(index) != QLatin1Char('\n')) {
                ++index;
            }
            continue;
        }
        if (character == QLatin1Char('/') && index + 1 < text.size()
            && text.at(index + 1) == QLatin1Char('*')) {
            index += 2;
            while (index + 1 < text.size()
                   && !(text.at(index) == QLatin1Char('*')
                        && text.at(index + 1) == QLatin1Char('/'))) {
                ++index;
            }
            index = qMin(index + 2, text.size());
            continue;
        }
        out.append(character);
        ++index;
    }
    return out;
}

/// 内容过滤器不得自己读文件的那份禁用清单，只此一份。
QStringList bannedFileTokens()
{
    return QStringList{QStringLiteral("QFile"), QStringLiteral("readAll"),
                       QStringLiteral("QTextStream"), QStringLiteral("QDataStream"),
                       QStringLiteral("QDir"), QStringLiteral("readFileContents")};
}

/// 「这段（已去掉注释的）源码里有没有读文件的痕迹」。
bool touchesFileSystem(const QString &source)
{
    const QString code = stripComments(source);
    for (const QString &token : bannedFileTokens()) {
        if (code.contains(token)) {
            return true;
        }
    }
    return false;
}

/// 「这段源码有没有引用某个头文件」——同样只在**代码**里找，
/// 说明文字里提到一个头文件名不算依赖。
bool touchesInclude(const QString &source, const QString &headerName)
{
    return stripComments(source).contains(headerName);
}

QString joined(const QVector<QString> &problems)
{
    // `join` 是 `QStringList` 的成员，`QVector<QString>` 没有它
    // （§6 有一条同源的坑记录）。先转再 join。
    QStringList lines;
    for (const QString &problem : problems) {
        lines.append(problem);
    }
    return lines.join(QStringLiteral("；"));
}

} // namespace

void TstContentFilter::initTestCase()
{
    QVERIFY2(!readSourceFile(QStringLiteral("/Services/Filter/contentfilter.h")).isEmpty(),
             "读不到 contentfilter.h：LQCOMPARE_CODE_ROOT 指向不对？");
    QVERIFY2(!readSourceFile(QStringLiteral("/Services/Filter/contentfilter.cpp")).isEmpty(),
             "读不到 contentfilter.cpp：LQCOMPARE_CODE_ROOT 指向不对？");
}

QString TstContentFilter::readSourceFile(const QString &relativePath) const
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// -----------------------------------------------------------------------------
// A 阶段顺序与「行过滤在前」（第 3 条）
// -----------------------------------------------------------------------------

void TstContentFilter::stageOrderPutsLineFilterBeforeIgnoreRules()
{
    const QVector<ContentFilterStage> order = contentFilterStageOrder();

    QCOMPARE(order.size(), 2);
    QCOMPARE(stageId(order.at(0)), QStringLiteral("line-filter"));
    QCOMPARE(stageId(order.at(1)), QStringLiteral("ignore-rules"));

    // 顺序这件事本身也要从**序号**这一侧断言：只比标识符的话，
    // 一个把 index 硬编码成 0 的实现照样能过。
    QVERIFY(contentFilterStageIndex(ContentFilterStage::LineFilter)
            < contentFilterStageIndex(ContentFilterStage::IgnoreRules));
    QCOMPARE(contentFilterStageIndex(ContentFilterStage::LineFilter), 0);
    QCOMPARE(contentFilterStageIndex(ContentFilterStage::IgnoreRules), 1);
}

void TstContentFilter::stageTableIsTheSingleSourceOfTruth()
{
    const QVector<ContentFilterStageRow> rows = stageRows();
    const QVector<ContentFilterStage> order = contentFilterStageOrder();

    QCOMPARE(rows.size(), order.size());
    for (int index = 0; index < rows.size(); ++index) {
        QCOMPARE(rows.at(index).stage, order.at(index));
        // 取标签/说明的函数必须与表逐字一致，否则「表是事实来源」是假的。
        QCOMPARE(rows.at(index).label, contentFilterStageLabel(order.at(index)));
        QCOMPARE(rows.at(index).description, contentFilterStageDescription(order.at(index)));
    }

    // 忽略规则那一段的说明必须点明「它不改变参与比较的行集」——
    // 一改动就说明取舍被改了，读这段话的人会以为改行集也是它的职责。
    QVERIFY2(contentFilterStageDescription(ContentFilterStage::IgnoreRules)
                 .contains(QStringLiteral("不改变参与比较的行集")),
             "忽略规则的说明里必须写明它不改变参与比较的行集");
}

void TstContentFilter::stageIndexMatchesTheTableOrder()
{
    QCOMPARE(contentFilterStageIndex(ContentFilterStage::LineFilter), 0);
    QCOMPARE(contentFilterStageIndex(ContentFilterStage::IgnoreRules), 1);

    // 每个阶段都必须能在表里找到（否则 index 返回 -1，界面会静默地少一行）。
    for (ContentFilterStage stage : contentFilterStageOrder()) {
        QVERIFY(contentFilterStageIndex(stage) >= 0);
        QVERIFY(!contentFilterStageLabel(stage).isEmpty());
        QVERIFY(!contentFilterStageDescription(stage).isEmpty());
    }
}

void TstContentFilter::stageSelfCheckIsCleanForBuiltinTable()
{
    QVERIFY2(validateContentFilterStageTable(stageRows()).isEmpty(),
             qPrintable(joined(validateContentFilterStageTable(stageRows()))));

    // 汇总入口同样必须是干净的。
    QVERIFY2(validateBuiltinContentFilterTables().isEmpty(),
             qPrintable(joined(validateBuiltinContentFilterTables())));
}

void TstContentFilter::stageSelfCheckReportsWrongOrder()
{
    // **这一条是这张表存在的全部理由。** 把两行对调，自检必须报出来。
    QVector<ContentFilterStageRow> rows = stageRows();
    std::swap(rows[0], rows[1]);

    // 先确认「对调之后表确实变了」——锚点不生效的写法会静默变成「什么都没改」，
    // 于是用例报漏检，看起来像自检没写到位（本仓踩过这个坑）。
    QCOMPARE(stageId(rows.at(0).stage), QStringLiteral("ignore-rules"));

    const QVector<QString> problems = validateContentFilterStageTable(rows);
    QVERIFY2(!problems.isEmpty(), "把两个阶段对调之后自检必须报错");
    QVERIFY2(joined(problems).contains(QStringLiteral("顺序")), qPrintable(joined(problems)));
}

void TstContentFilter::stageSelfCheckReportsDuplicateAndMissingStages()
{
    // 重复：两个 `line-filter`。
    QVector<ContentFilterStageRow> duplicated = stageRows();
    duplicated.append(duplicated.at(0));
    QVector<QString> problems = validateContentFilterStageTable(duplicated);
    QVERIFY2(!problems.isEmpty(), "重复的阶段必须被报出来");

    // 缺失：只剩一个阶段。
    QVector<ContentFilterStageRow> missing = stageRows();
    missing.removeLast();
    problems = validateContentFilterStageTable(missing);
    QVERIFY2(!problems.isEmpty(), "缺少阶段必须被报出来");
    QVERIFY2(joined(problems).contains(QStringLiteral("ignore-rules")), qPrintable(joined(problems)));

    // 空表。这是最要紧的一种：一份空表若能通过，自检就是个摆设。
    problems = validateContentFilterStageTable(QVector<ContentFilterStageRow>());
    QVERIFY2(problems.size() >= 2, "空表必须至少报出「条数不对」与「缺少某阶段」");
}

void TstContentFilter::stageSelfCheckReportsEmptyLabelAndIdentifier()
{
    QVector<ContentFilterStageRow> rows = stageRows();
    rows[0].label.clear();
    QVERIFY2(!validateContentFilterStageTable(rows).isEmpty(), "空标签必须被报出来");

    rows = stageRows();
    rows[1].identifier = "";
    QVERIFY2(!validateContentFilterStageTable(rows).isEmpty(), "空标识符必须被报出来");

    // 两个阶段共用一个标识符（日志与设置键会撞车）。
    rows = stageRows();
    rows[1].identifier = rows[0].identifier;
    QVERIFY2(joined(validateContentFilterStageTable(rows)).contains(QStringLiteral("重复")),
             qPrintable(joined(validateContentFilterStageTable(rows))));
}

void TstContentFilter::lineFilterReadsRawLinesNotNormalizedOnes()
{
    // **第 3 条在接口上的那半句。**
    //
    // 行过滤的输入是原始行，不经过任何规范化。这一条若被改坏（比如为了
    // 「顺手统一一下」先在内部 trim 一遍），「哪些行参与比较」就会开始随会话里
    // 开着的忽略规则变化——用户改的是忽略规则，被改变的却是参与比较的行集。
    const QStringList lines = rawLines(QStringLiteral("alpha\n   \nbeta"));
    QCOMPARE(lines.size(), 3);

    // `^$` 只认**真正为空**的行。整批里没有一行是空的，所以一行都不能少。
    const LineFilter strictEmpty = filterOf(QStringLiteral("re:^$"));
    const LineFilterResult strictResult = strictEmpty.filterLines(lines);
    QCOMPARE(strictResult.droppedLineCount(), 0);
    QCOMPARE(strictResult.lines, asList(lines));

    // 反过来：用户想丢掉那一行「看起来是空的」时怎么写，也要钉住。
    // 没有这一条的话，下一个人会以为 `^$` 行为不对而去放宽它。
    const LineFilter whitespaceOnly = filterOf(QStringLiteral("re:^\\s*$"));
    QCOMPARE(whitespaceOnly.filterLines(lines).droppedLineCount(), 1);
}

void TstContentFilter::lineFilterDecisionDoesNotDependOnIgnoreRules()
{
    // 行过滤与忽略规则是**两个不同的机制**，证据是它们在同一个输入上给出
    // 不同的可观测结果。这一条把两者区分开，免得将来有人把「丢行」实现成
    // 「一种忽略规则」——那样一来「忽略不重要差异」的开关会顺带改变参与比较的行集。
    const QStringList left = rawLines(QStringLiteral("alpha\n   \nbeta"));
    const QStringList right = rawLines(QStringLiteral("alpha\nbeta"));

    // 机制一：行过滤。丢掉那一行之后，**连 Exact 都能判成相同**——
    // 说明那一行是真的不在比较输入里了，不是被「忽略」掉的。
    const LineFilter filter = filterOf(QStringLiteral("re:^\\s*$"));
    const LineFilterResult filtered = filter.filterLines(left);
    QCOMPARE(filtered.droppedLineCount(), 1);

    Text::CompareOptions exact;
    exact.whitespace = Text::Whitespace::Exact;
    const Text::Result viaFilter = compareText(filtered.lines, right, exact);
    QVERIFY2(viaFilter.differences.isEmpty(), "丢掉那一行之后不该还有差异");
    QCOMPARE(viaFilter.ignoredBlocks, 0); // 相等不是靠「忽略」换来的

    // 机制二：只在**行内部**生效的忽略规则。整行多出来/少掉一条，
    // 它一条都救不了——所以「丢掉整行」只有行过滤这一个手段，
    // 「先过滤行」也就不是一个可以靠忽略规则补救的顺序问题。
    Text::CompareOptions ignoreAll;
    ignoreAll.whitespace = Text::Whitespace::IgnoreAll;
    const Text::Result viaIgnoreRules = compareText(left, right, ignoreAll);
    QVERIFY2(!viaIgnoreRules.differences.isEmpty(),
             "忽略空白救不了「多了一行」——它只作用于同位置行内部的差异");
}

void TstContentFilter::lineFilterThenIgnoreRulesComposesWithTheRealComparer()
{
    // 规格第 3 条要的「先过滤行再应用忽略规则」，用**真的**忽略规则跑一遍。
    const QStringList left =
        rawLines(QStringLiteral("2024-01-01 10:00:00 INFO start\nvalue\t1"));
    const QStringList right =
        rawLines(QStringLiteral("2025-06-06 23:59:59 WARN stop\nvalue 1"));

    // 什么都不做：两处差异（时间戳行不同、`\t` 与空格的空白差异）。
    QVERIFY(!compareText(left, right, Text::CompareOptions()).differences.isEmpty());

    // 阶段一：行过滤把两侧的时间戳行都去掉。
    const LineFilter filter = filterOf(QStringLiteral("re:^\\d{4}-\\d{2}-\\d{2} "));
    const LineFilterResult filteredLeft = filter.filterLines(left);
    const LineFilterResult filteredRight = filter.filterLines(right);
    QCOMPARE(filteredLeft.droppedLineCount(), 1);
    QCOMPARE(filteredRight.droppedLineCount(), 1);
    QCOMPARE(filteredLeft.lines, asList(rawLines(QStringLiteral("value\t1"))));
    QCOMPARE(filteredRight.lines, asList(rawLines(QStringLiteral("value 1"))));

    // 阶段二：忽略规则处理剩下的空白差异。
    Text::CompareOptions options;
    options.whitespace = Text::Whitespace::IgnoreAll;
    const Text::Result result = compareText(filteredLeft.lines, filteredRight.lines, options);
    QVERIFY2(result.differences.isEmpty(), "过滤掉时间戳行之后，空白差异应被忽略");
    QVERIFY2(result.ignoredBlocks > 0, "差异确实存在过，只是被忽略了——不是本来就没有");
}

// -----------------------------------------------------------------------------
// B 行过滤的三种模式（第 1 条）
// -----------------------------------------------------------------------------

void TstContentFilter::modeTableIsTheSingleSourceOfTruth()
{
    const QVector<LineFilterModeRow> rows = lineModeRows();
    const QVector<LineFilterMode> modes = allLineFilterModes();

    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.size(), modes.size());
    for (int index = 0; index < rows.size(); ++index) {
        QCOMPARE(rows.at(index).mode, modes.at(index));
        QCOMPARE(rows.at(index).label, lineFilterModeLabel(modes.at(index)));
        QCOMPARE(rows.at(index).explanation, lineFilterModeExplanation(modes.at(index)));
        QCOMPARE(rows.at(index).wholeLine, lineFilterModeMatchesWholeLine(modes.at(index)));
    }

    QCOMPARE(modeId(modes.at(0)), QStringLiteral("exact"));
    QCOMPARE(modeId(modes.at(1)), QStringLiteral("wildcard"));
    QCOMPARE(modeId(modes.at(2)), QStringLiteral("regex"));

    // 三种模式的标识符、标签、说明都不能撞车。
    QCOMPARE(QSet<QString>({modeId(modes.at(0)), modeId(modes.at(1)), modeId(modes.at(2))}).size(), 3);
}

void TstContentFilter::modePrefixRecognisesEqualsAndRegex()
{
    // `= info` 与 `=info` 都要认（空格可选）。
    LineFilterModePrefixHit hit = matchLineFilterModePrefix(QStringLiteral("= info"));
    QVERIFY(hit.found);
    QCOMPARE(hit.mode, LineFilterMode::Exact);
    QCOMPARE(hit.prefixLength, 2);
    QCOMPARE(hit.textOffset, 2);

    hit = matchLineFilterModePrefix(QStringLiteral("=info"));
    QVERIFY(hit.found);
    QCOMPARE(hit.mode, LineFilterMode::Exact);
    QCOMPARE(hit.prefixLength, 1);
    QCOMPARE(hit.textOffset, 1);

    hit = matchLineFilterModePrefix(QStringLiteral("re:\\d+"));
    QVERIFY(hit.found);
    QCOMPARE(hit.mode, LineFilterMode::Regex);
    QCOMPARE(hit.prefixLength, 3);
    QCOMPARE(hit.textOffset, 3);

    // 认不出来的一律按通配（无前缀即通配，与 namefilter 的约定一致）。
    hit = matchLineFilterModePrefix(QStringLiteral("*.tmp"));
    QVERIFY(!hit.found);
    QCOMPARE(hit.prefixLength, 0);
    QCOMPARE(hit.textOffset, 0);
}

void TstContentFilter::indentedPrefixIsStillRecognised()
{
    // **刻意与 namefilter 不一致的一处**（理由写在 contentfilter.h 上）：
    // 声明里缩进对齐是常态，而前缀没被认出来的后果是整行退化成通配模式——
    // `=` 变成掩码里的普通字符，于是 `= EXACT` 变成「匹配字面量 `= EXACT` 的行」，
    // 看起来完全正常却永远不命中。缩进一下就静默失效，不值得为
    // 「与另一个过滤器逐字一致」付这个代价。
    const LineFilterLineAnalysis indented = analyzeLineFilterLine(QStringLiteral("   = EXACT"));
    QVERIFY(indented.hasPattern);
    QCOMPARE(indented.pattern.mode, LineFilterMode::Exact);
    QCOMPARE(indented.pattern.pattern, QStringLiteral("EXACT"));
    QCOMPARE(indented.pattern.column, 5); // 缩进 3 + 前缀 2
    QVERIFY(indented.issues.isEmpty());

    // tab 缩进同理（`ltrimAscii` 认 tab）。
    const LineFilterLineAnalysis tabbed = analyzeLineFilterLine(QStringLiteral("\tre:^a"));
    QVERIFY(tabbed.hasPattern);
    QCOMPARE(tabbed.pattern.mode, LineFilterMode::Regex);
    QCOMPARE(tabbed.pattern.pattern, QStringLiteral("^a"));
    QCOMPARE(tabbed.pattern.column, 4);

    // `textOffset` 要跟着变：没认到前缀时它是缩进长度，
    // 于是通配模式的列号也不会落到行首空白上。
    const LineFilterModePrefixHit hit = matchLineFilterModePrefix(QStringLiteral("  *.tmp"));
    QVERIFY(!hit.found);
    QCOMPARE(hit.textOffset, 2);

    // 真的生效（不只是解析结果好看）。
    const LineFilter filter = filterOf(QStringLiteral("# 注释\n   = EXACT\n\tre:^TS "));
    QVERIFY(filter.excludes(QStringLiteral("EXACT")));
    QVERIFY(filter.excludes(QStringLiteral("TS one")));

    // 反向：缩进版与不缩进版必须得到**同一份**判定结果，
    // 否则「缩进不影响语义」这句话只在解析结果上成立。
    QCOMPARE(filterOf(QStringLiteral("   = EXACT")).toDeclarationText(),
             filterOf(QStringLiteral("= EXACT")).toDeclarationText());
    QCOMPARE(filterOf(QStringLiteral("\tre:^a")).toDeclarationText(),
             filterOf(QStringLiteral("re:^a")).toDeclarationText());
}

void TstContentFilter::modePrefixIsEmptyForWildcard()
{
    // 通配符用**空串**表示「无前缀」。这不是「什么都能匹配」，而是「不写前缀」。
    // 自检里有一条专门盯这件事：前缀列表不能为空，而空串只能属于一个模式。
    QCOMPARE(lineFilterModePreferredPrefix(LineFilterMode::Wildcard), QString());
    QCOMPARE(lineFilterModePreferredPrefix(LineFilterMode::Exact), QStringLiteral("= "));
    QCOMPARE(lineFilterModePreferredPrefix(LineFilterMode::Regex), QStringLiteral("re:"));

    // 一行以 `re:` 开头但它其实是通配符想要的内容（`re:` 也是合法的掩码文本）——
    // 前缀优先，所以它被当成正则。这是明确定义的行为，不是巧合。
    const LineFilterLineAnalysis analysis = analyzeLineFilterLine(QStringLiteral("re:"));
    QCOMPARE(analysis.pattern.mode, LineFilterMode::Regex);
    QVERIFY(!analysis.hasPattern); // 前缀后面什么都没有 → 空表达式
}

void TstContentFilter::prefixMatchingPrefersTheLongestPrefix()
{
    // `re: ` 与 `re:` 都在表里。必须取长的那个，否则列号会偏一格，
    // 界面上标红的位置就落在空白上。
    const LineFilterLineAnalysis spaced = analyzeLineFilterLine(QStringLiteral("re: ^a"));
    QVERIFY(spaced.hasPattern);
    QCOMPARE(spaced.pattern.mode, LineFilterMode::Regex);
    // 正文从第 4 列起（`re: ` 共 4 个字符），而不是第 3 列。
    QCOMPARE(spaced.pattern.column, 4);
    QCOMPARE(spaced.pattern.pattern, QStringLiteral("^a"));
}

void TstContentFilter::exactMatchesWholeLineOnly()
{
    const LineFilter filter = filterOf(QStringLiteral("= INFO start"));

    QVERIFY(filter.excludes(QStringLiteral("INFO start")));
    // 前后多一个字符都不算——`=` 的语义就是整行全等。
    QVERIFY(!filter.excludes(QStringLiteral(" INFO start")));
    QVERIFY(!filter.excludes(QStringLiteral("INFO start ")));
    QVERIFY(!filter.excludes(QStringLiteral("x INFO start")));
}

void TstContentFilter::wildcardUsesMaskSyntaxAndMatchesWholeLine()
{
    const LineFilter filter = filterOf(QStringLiteral("*.tmp"));

    QVERIFY(filter.excludes(QStringLiteral("a.tmp")));
    QVERIFY(filter.excludes(QStringLiteral("build.log.tmp")));
    QVERIFY(!filter.excludes(QStringLiteral("a.txt")));

    // `?` 与 `[...]` 是掩码语义，不是正则语义。
    const LineFilter single = filterOf(QStringLiteral("log?.txt"));
    QVERIFY(single.excludes(QStringLiteral("log1.txt")));
    QVERIFY(!single.excludes(QStringLiteral("log12.txt")));

    const LineFilter set = filterOf(QStringLiteral("v[0-9].log"));
    QVERIFY(set.excludes(QStringLiteral("v3.log")));
    QVERIFY(!set.excludes(QStringLiteral("vx.log")));
}

void TstContentFilter::wildcardUsesTheMasksOwnSlashRule()
{
    // 整行被同时当作「名字」与「路径」交给掩码，于是掩码语言那两条既有规则原样成立：
    //   不含 `/` 的掩码按名字（= 整行）匹配 → `*.txt` 命中任意一层
    //   含 `/` 的掩码按 `/` 分段匹配        → `src/*.txt` 只命中该前缀
    //
    // 刻意**不**用 MaskSubject::forName()：它会先按 `/` 取最后一段当名字
    // （它假定调用方给的是路径），用在行过滤上会让 `*.txt` 只匹配一行的最后一段，
    // 与「整行匹配」不符。下面两条断言就是这件事的证据。
    const LineFilter byName = filterOf(QStringLiteral("*.txt"));
    QVERIFY(byName.excludes(QStringLiteral("src/a.txt")));

    const LineFilter byPath = filterOf(QStringLiteral("src/*.txt"));
    QVERIFY(byPath.excludes(QStringLiteral("src/a.txt")));
    QVERIFY(!byPath.excludes(QStringLiteral("other/a.txt")));
}

void TstContentFilter::regexSearchesSubstringNotWholeLine()
{
    // **这是本条目的主用例**：日志时间戳行是 FILT-004 第 1 条的典型场景，
    // 用户会写 `re:^\d{4}-` 或 `re:INFO`。若实现把正则整行锚定，
    // `re:INFO` 会永远不命中，而界面上完全看不出为什么。
    const LineFilter bySubstring = filterOf(QStringLiteral("re:INFO"));
    QVERIFY2(bySubstring.excludes(QStringLiteral("2024-01-01 10:00:00 INFO start")),
             "正则必须是子串匹配（等价于 grep），不能整行锚定");
    QVERIFY(!bySubstring.excludes(QStringLiteral("2024-01-01 10:00:00 DEBUG start")));

    // 用户自己写的锚点照常生效。
    const LineFilter anchored = filterOf(QStringLiteral("re:^INFO"));
    QVERIFY(anchored.excludes(QStringLiteral("INFO start")));
    QVERIFY(!anchored.excludes(QStringLiteral("2024 INFO start")));

    // 时间戳前缀（真实用例）。
    const LineFilter timestamp = filterOf(QStringLiteral("re:^\\d{4}-\\d{2}-\\d{2} \\d{2}:"));
    QVERIFY(timestamp.excludes(QStringLiteral("2024-01-01 10:00:00 INFO")));
    QVERIFY(!timestamp.excludes(QStringLiteral("INFO 2024-01-01 10:00:00")));
}

void TstContentFilter::regexKeepsUserAnchorsWorking()
{
    // 这条盯的是「不要用 anchoredPattern() 包装用户输入」。
    // 包装会把用户的 `^a` / `a$` 变成 `\A(?:^a)\z` 之类的叠加锚点，
    // 语义被静默改掉（本仓在 namefilter 上踩过）。
    const LineFilter endAnchored = filterOf(QStringLiteral("re:END$"));
    QVERIFY(endAnchored.excludes(QStringLiteral("INFO END")));
    QVERIFY(!endAnchored.excludes(QStringLiteral("END INFO")));

    // 子串匹配的另一个可观测后果：`dot` 能命中 `a.b` 之外的位置。
    const LineFilter middle = filterOf(QStringLiteral("re:INFO"));
    QVERIFY(middle.excludes(QStringLiteral("xINFOx")));
}

void TstContentFilter::wildcardMetacharactersAreNotRegex()
{
    // 通配模式走掩码语言。`.tmp` 里的 `.` 是字面量而不是「任意字符」，
    // `a.tmp` 不能靠 `.` 命中 `axtmp`。
    const LineFilter filter = filterOf(QStringLiteral("a.tmp"));
    QVERIFY(filter.excludes(QStringLiteral("a.tmp")));
    QVERIFY(!filter.excludes(QStringLiteral("axtmp")));

    // 反过来，正则模式下的 `a.tmp` 应当命中 `axtmp`（`.` 是任意字符）。
    const LineFilter regex = filterOf(QStringLiteral("re:^a.tmp$"));
    QVERIFY(regex.excludes(QStringLiteral("axtmp")));
}

void TstContentFilter::emptyLineIsNotMatchedByWildcard()
{
    // 一条**公开的代价**，刻意保留：通配模式复用的是掩码语言，而掩码把
    // 「空名字」视为无效（MaskSubject::isValid() 要求名字非空），
    // 因此 `*` 命中不了空行。
    //
    // 不在这里给空行开特例：那会让「掩码语言在空串上的行为」出现第二个事实来源，
    // 而它只影响一个几乎没人依赖的边角。用户想连空行一起去掉时写 `re:^$`。
    const LineFilter star = filterOf(QStringLiteral("*"));
    QVERIFY(!star.excludes(QString()));
    QVERIFY(star.excludes(QStringLiteral("x")));

    // 想丢空行有明确写法。
    QVERIFY(filterOf(QStringLiteral("re:^$")).excludes(QString()));
    QVERIFY(filterOf(QStringLiteral("re:^$")).excludes(QStringLiteral("")));

    // 空行的判定入口也不该崩。
    QVERIFY(star.matchingPatternIndexes(QString()).isEmpty());
    QCOMPARE(star.filterLines(QStringList{QString()}).lines.size(), 1);
}

void TstContentFilter::modeTableSelfCheckRejectsWholeLineRegex()
{
    QVector<LineFilterModeRow> rows = lineModeRows();
    const int regexIndex = indexOfMode(rows, LineFilterMode::Regex);
    QVERIFY(regexIndex >= 0);
    rows[regexIndex].wholeLine = true;

    const QVector<QString> problems = validateLineFilterModeTable(rows);
    QVERIFY2(!problems.isEmpty(), "把正则改成整行匹配必须被报出来");
    QVERIFY2(joined(problems).contains(QStringLiteral("子串")), qPrintable(joined(problems)));
}

void TstContentFilter::modeTableSelfCheckRejectsSubstringWildcard()
{
    QVector<LineFilterModeRow> rows = lineModeRows();
    const int wildcardIndex = indexOfMode(rows, LineFilterMode::Wildcard);
    QVERIFY(wildcardIndex >= 0);
    rows[wildcardIndex].wholeLine = false;

    const QVector<QString> problems = validateLineFilterModeTable(rows);
    QVERIFY2(!problems.isEmpty(), "把通配符改成子串匹配必须被报出来");
    QVERIFY2(joined(problems).contains(QStringLiteral("整行")), qPrintable(joined(problems)));
}

void TstContentFilter::modeTableSelfCheckRejectsDuplicatePrefix()
{
    // 两个模式都声明「无前缀」——「这一行用哪种模式」就没有答案了。
    QVector<LineFilterModeRow> rows = lineModeRows();
    const int wildcardIndex = indexOfMode(rows, LineFilterMode::Wildcard);
    const int regexIndex = indexOfMode(rows, LineFilterMode::Regex);
    rows[wildcardIndex].prefixes = QVector<QString>{QString()};
    rows[regexIndex].prefixes = QVector<QString>{QString()};

    const QVector<QString> problems = validateLineFilterModeTable(rows);
    QVERIFY2(!problems.isEmpty(), "两个模式共用无前缀必须被报出来");

    // 同一个模式内部重复也算。
    rows = lineModeRows();
    rows[indexOfMode(rows, LineFilterMode::Exact)].prefixes =
        QVector<QString>{QStringLiteral("= "), QStringLiteral("= ")};
    QVERIFY2(!validateLineFilterModeTable(rows).isEmpty(), "同一模式内前缀重复必须被报出来");
}

void TstContentFilter::modeTableSelfCheckRejectsMissingPrefixList()
{
    QVector<LineFilterModeRow> rows = lineModeRows();
    rows[indexOfMode(rows, LineFilterMode::Regex)].prefixes = QVector<QString>();

    const QVector<QString> problems = validateLineFilterModeTable(rows);
    QVERIFY2(!problems.isEmpty(), "一个模式连前缀都不声明必须被报出来");

    // 空标签、空说明、空标识符、重复标识符各报一条。
    rows = lineModeRows();
    rows[0].label.clear();
    QVERIFY(!validateLineFilterModeTable(rows).isEmpty());

    rows = lineModeRows();
    rows[0].explanation.clear();
    QVERIFY(!validateLineFilterModeTable(rows).isEmpty());

    rows = lineModeRows();
    rows[0].identifier = "";
    QVERIFY(!validateLineFilterModeTable(rows).isEmpty());

    rows = lineModeRows();
    rows[1].identifier = rows[0].identifier;
    QVERIFY(!validateLineFilterModeTable(rows).isEmpty());
}

void TstContentFilter::modeTableSelfCheckRejectsUnknownMode()
{
    // 表里少一种模式（界面会静默地少一个下拉项，而没有任何运行期现象）。
    QVector<LineFilterModeRow> rows = lineModeRows();
    rows.removeLast();
    const QVector<QString> problems = validateLineFilterModeTable(rows);
    QVERIFY2(!problems.isEmpty(), "少一种模式必须被报出来");
    QVERIFY2(joined(problems).contains(QStringLiteral("regex")), qPrintable(joined(problems)));

    // 多一种不认识的行（标识符为空）。
    rows = lineModeRows();
    rows.append(LineFilterModeRow{});
    QVERIFY(!validateLineFilterModeTable(rows).isEmpty());
}

// -----------------------------------------------------------------------------
// C 行过滤声明解析（第 1 条）
// -----------------------------------------------------------------------------

void TstContentFilter::parseSkipsBlankLinesAndComments()
{
    const LineFilterParseResult parsed = LineFilter::parseDeclaration(
        QStringLiteral("# 日志时间戳\n"
                       "\n"
                       "   \n"
                       "re:^\\d{4}-  \n"
                       "# 结尾注释\n"
                       "*.tmp"));

    QVERIFY2(parsed.ok(), qPrintable(parsed.describeErrors()));
    QCOMPARE(parsed.patternCount(), 2);
    QCOMPARE(parsed.filter.patterns().at(0).mode, LineFilterMode::Regex);
    QCOMPARE(parsed.filter.patterns().at(1).mode, LineFilterMode::Wildcard);
}

void TstContentFilter::parseReportsEmptyExpressionWithHint()
{
    // 只有前缀、没有正文。**报错**而不是「当成匹配空行的规则」：
    // 一行只剩下 `=` 看着像没写完，而它一旦静默生效，
    // 用户会看到一批空行凭空消失却没有任何提示。
    const LineFilterParseResult parsed = LineFilter::parseDeclaration(QStringLiteral("="));

    QVERIFY(!parsed.ok());
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.first().kind, LineFilterIssueKind::Empty);
    QCOMPARE(parsed.issues.first().line, 1);
    QVERIFY(!parsed.issues.first().hint.isEmpty());
    // 提示里必须给出「想去掉空行该怎么写」——这是报错的全部价值。
    QVERIFY2(parsed.issues.first().hint.contains(QStringLiteral("^$")),
             qPrintable(parsed.issues.first().hint));
    QCOMPARE(parsed.patternCount(), 0);

    // 空的 `re:` 同样。
    const LineFilterParseResult regexOnly =
        LineFilter::parseDeclaration(QStringLiteral("re:"));
    QCOMPARE(regexOnly.issues.size(), 1);
    QCOMPARE(regexOnly.issues.first().kind, LineFilterIssueKind::Empty);
}

void TstContentFilter::parseReportsMaskSyntaxErrorAtColumn()
{
    // 前面三个空格，掩码从第 4 列起（0 起算 = 3），掩码内部再偏 0 列。
    const LineFilterParseResult parsed =
        LineFilter::parseDeclaration(QStringLiteral("   [abc"));

    QVERIFY(!parsed.ok());
    QCOMPARE(parsed.issues.size(), 1);
    const LineFilterIssue issue = parsed.issues.first();
    QCOMPARE(issue.kind, LineFilterIssueKind::Syntax);
    QCOMPARE(issue.line, 1);
    QCOMPARE(issue.column, 3);
    QVERIFY(issue.length >= 1);
    QVERIFY(!issue.message.isEmpty());
    QCOMPARE(parsed.patternCount(), 0);

    // 列号必须真的换算过：整体前移一格，列号也要跟着减一。
    const LineFilterParseResult shifted =
        LineFilter::parseDeclaration(QStringLiteral("  [abc"));
    QCOMPARE(shifted.issues.first().column, 2);
}

void TstContentFilter::parseReportsInvalidRegexWithColumn()
{
    const LineFilterParseResult parsed =
        LineFilter::parseDeclaration(QStringLiteral("re:("));

    QVERIFY(!parsed.ok());
    QCOMPARE(parsed.issues.size(), 1);
    const LineFilterIssue issue = parsed.issues.first();
    QCOMPARE(issue.kind, LineFilterIssueKind::Syntax);
    QCOMPARE(issue.line, 1);
    // 列号落在正文区间内（`re:` 占 3 格，Qt 的 patternErrorOffset 指向出错点之后）。
    QVERIFY2(issue.column >= 3, qPrintable(QString::number(issue.column)));
    QVERIFY(!issue.message.isEmpty());
    QCOMPARE(parsed.patternCount(), 0);
}

void TstContentFilter::parseReportsRiskyRegexWithoutBlockingIt()
{
    const LineFilterParseResult parsed =
        LineFilter::parseDeclaration(QStringLiteral("re:(a+)+$"));

    // 风险预检**只提示、不阻断**：用户可能真的知道自己在写什么
    // （例如对着很短的样本写）。所以它报 Risky，但表达式照常生效。
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.first().kind, LineFilterIssueKind::Risky);
    QVERIFY(parsed.issues.first().column >= 3);
    QCOMPARE(parsed.patternCount(), 1);

    // 而且它真的生效。
    const LineFilter filter = parsed.filter;
    QVERIFY(filter.excludes(QStringLiteral("aaaa")));

    // 一个不会退化的正则不该被误报（复用 namefilter 的预检实现，
    // 它的取舍是「宁可漏报也不误报」）。
    const LineFilterParseResult safe =
        LineFilter::parseDeclaration(QStringLiteral("re:^test_[0-9]{2}\\.log$"));
    QVERIFY2(safe.ok(), qPrintable(safe.describeErrors()));

    // `(a|b)*` 的内层没有量词，不在该报之列。
    QVERIFY(LineFilter::parseDeclaration(QStringLiteral("re:(a|b)*c")).ok());
}

void TstContentFilter::parseKeepsGoodLinesWhenOneIsBad()
{
    // 一行写错只丢那一行，其余照常生效。用户改到一半时整份声明失效，
    // 他会以为是自己把别的地方敲坏了。
    const LineFilterParseResult parsed = LineFilter::parseDeclaration(
        QStringLiteral("re:^\\d{4}-\n"
                       "[broken\n"
                       "*.tmp\n"
                       "= EXACT\n"));

    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.first().line, 2);
    QCOMPARE(parsed.patternCount(), 3);

    const LineFilter filter = parsed.filter;
    QVERIFY(filter.excludes(QStringLiteral("2024-01-01")));
    QVERIFY(filter.excludes(QStringLiteral("a.tmp")));
    QVERIFY(filter.excludes(QStringLiteral("EXACT")));
}

void TstContentFilter::parseAcceptsCrLfAndCrLineEndings()
{
    // 只在 `\n` 上切会让 Windows 上编辑过的声明每行多一个 `\r`，
    // 于是 `= EXACT` 变成 `= EXACT\r`——**静静地对不上任何一行**，
    // 而声明本身看起来完美无缺。
    const LineFilterParseResult crlf =
        LineFilter::parseDeclaration(QStringLiteral("= EXACT\r\n*.tmp\r\n"));
    QVERIFY2(crlf.ok(), qPrintable(crlf.describeErrors()));
    QCOMPARE(crlf.patternCount(), 2);
    QCOMPARE(crlf.filter.patterns().at(0).pattern, QStringLiteral("EXACT"));
    QVERIFY(crlf.filter.excludes(QStringLiteral("EXACT")));

    const LineFilterParseResult cr =
        LineFilter::parseDeclaration(QStringLiteral("= EXACT\r*.tmp\r"));
    QCOMPARE(cr.patternCount(), 2);
    QVERIFY(cr.filter.excludes(QStringLiteral("EXACT")));

    // 行数也要对：`\r\n` 是一个换行而不是两个，否则空行的行号整体偏大。
    const LineFilterParseResult numbered =
        LineFilter::parseDeclaration(QStringLiteral("\r\n\r\n= EXACT"));
    QCOMPARE(numbered.patternCount(), 1);
    QCOMPARE(numbered.filter.patterns().first().line, 3);
}

void TstContentFilter::parseReportsOneBasedLineNumbers()
{
    const LineFilterParseResult parsed = LineFilter::parseDeclaration(
        QStringLiteral("# 注释\n"
                       "*\n"
                       "\n"
                       "=\n"
                       "re:^\n"));

    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.first().line, 4);
    QCOMPARE(parsed.patternCount(), 2);
}

void TstContentFilter::declarationRoundTripIsTextuallyIdentical()
{
    // **规范形式**下的往返必须逐字一致：差一个尾随空格或换行时，
    // 两个字符串在终端里长得一模一样，只有逐字比较才查得出来。
    const QString declaration = QStringLiteral("= EXACT LINE\n"
                                               "*.tmp\n"
                                               "re:^\\d{4}-\\d{2}-\\d{2}");

    const LineFilterParseResult parsed = LineFilter::parseDeclaration(declaration);
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeErrors()));
    QCOMPARE(parsed.filter.toDeclarationText(), declaration);

    // 再解析一次仍然一致（幂等）。
    QCOMPARE(LineFilter::parseDeclaration(parsed.filter.toDeclarationText())
                 .filter.toDeclarationText(),
             declaration);

    // 非规范形式会被**规范化**，而不是原样吐回去：表达式正文首尾的空白会被裁掉
    // （与 `NameFilter` 同一约定），前缀形式也统一成表里的标准写法。
    // 所以「往返一致」这条断言只对规范形式成立——把这一半也写下来，
    // 免得下一个人拿一份带尾随空格的声明来断言逐字一致，然后删掉裁剪逻辑。
    const LineFilter normalized =
        filterOf(QStringLiteral("  =   EXACT LINE  \n\n*.tmp\nre:^\\d{4}- "));
    QCOMPARE(normalized.toDeclarationText(),
             QStringLiteral("= EXACT LINE\n*.tmp\nre:^\\d{4}-"));

    // 而且规范化是幂等的：规范化之后的文本再走一遍不会继续变形。
    QCOMPARE(filterOf(normalized.toDeclarationText()).toDeclarationText(),
             normalized.toDeclarationText());
}

void TstContentFilter::toDeclarationTextWritesExplicitPrefixes()
{
    // `=foo` 与 `= foo` 读起来一样，但写回时统一成 `= `：
    // 少了那个空格会让声明文本「看起来像少打了什么」。
    QCOMPARE(filterOf(QStringLiteral("=foo")).toDeclarationText(), QStringLiteral("= foo"));

    // 通配模式不加前缀（它的前缀就是空串）。
    QCOMPARE(filterOf(QStringLiteral("*.tmp")).toDeclarationText(), QStringLiteral("*.tmp"));

    // 空过滤器的声明文本是空串而不是一个换行。
    QCOMPARE(LineFilter().toDeclarationText(), QString());
    QCOMPARE(LineFilter().patternCount(), 0);
    QVERIFY(LineFilter().isEmpty());
}

// -----------------------------------------------------------------------------
// D 行过滤判定与统计（第 1 条）
// -----------------------------------------------------------------------------

void TstContentFilter::excludesMatchesExactLine()
{
    const LineFilter filter = filterOf(QStringLiteral("= INFO start\n*.tmp"));

    QVERIFY(filter.excludes(QStringLiteral("INFO start")));
    QVERIFY(filter.excludes(QStringLiteral("x.tmp")));
    QVERIFY(!filter.excludes(QStringLiteral("WARN start")));
    QVERIFY(!filter.excludes(QStringLiteral("other")));

    // 空过滤器一条都不排除（与 MaskFilter / NameFilter 的「空过滤器保留条目」同源）。
    QVERIFY(!LineFilter().excludes(QStringLiteral("anything")));
}

void TstContentFilter::excludesIsCaseSensitive()
{
    // 行过滤器**没有**大小写开关：行内容的比较一律区分大小写。
    // 这是一条明确的取舍（行过滤的典型对象是日志里的固定标记），
    // 想不区分就写 `re:(?i)info`——那是用户明确的表达。
    const LineFilter exact = filterOf(QStringLiteral("= INFO"));
    QVERIFY(exact.excludes(QStringLiteral("INFO")));
    QVERIFY(!exact.excludes(QStringLiteral("info")));

    const LineFilter regex = filterOf(QStringLiteral("re:INFO"));
    QVERIFY(regex.excludes(QStringLiteral("x INFO y")));
    QVERIFY(!regex.excludes(QStringLiteral("x info y")));

    // 正则内联标志照常生效。
    QVERIFY(filterOf(QStringLiteral("re:(?i)INFO")).excludes(QStringLiteral("x info y")));
}

void TstContentFilter::matchingPatternIndexesReportsEveryHit()
{
    // 一条行可能同时命中多条表达式。只报第一条的话，用户改掉一条之后
    // 会发现还是被排除，而界面上说不出为什么。
    const LineFilter filter = filterOf(QStringLiteral("*.tmp\nre:^build\n= build.log.tmp"));
    const QVector<int> hits = filter.matchingPatternIndexes(QStringLiteral("build.log.tmp"));

    QCOMPARE(hits, QVector<int>({0, 1, 2}));
    QVERIFY(filter.matchingPatternIndexes(QStringLiteral("other")).isEmpty());
}

void TstContentFilter::filterLinesKeepsOrderAndReportsDroppedLines()
{
    const QStringList input = rawLines(QStringLiteral("keep-a\nDROP b\nkeep-c\nDROP d"));
    const LineFilter filter = filterOf(QStringLiteral("re:DROP"));

    const LineFilterResult result = filter.filterLines(input);

    // 留下来的行顺序不变。
    QCOMPARE(result.lines, asList(rawLines(QStringLiteral("keep-a\nkeep-c"))));
    // 被丢掉的行与它们的**输入行号**（1 起）都留着——界面上要能回答
    // 「你刚才把哪几行吃掉了」，只给一个数字会让用户以为软件坏了。
    QCOMPARE(result.droppedLines, asList(rawLines(QStringLiteral("DROP b\nDROP d"))));
    QCOMPARE(result.droppedLineNumbers, QVector<int>({2, 4}));
    QCOMPARE(result.inputLineCount, 4);
    QCOMPARE(result.keptLineCount(), 2);
    QCOMPARE(result.droppedLineCount(), 2);
}

void TstContentFilter::filterLinesReportsHitsByPattern()
{
    const LineFilter filter = filterOf(QStringLiteral("re:^A\nre:B"));
    const LineFilterResult result =
        filter.filterLines(rawLines(QStringLiteral("A one\nA two\nxB three\nuntouched")));

    QCOMPARE(result.droppedLineCount(), 3);
    // 与 patterns() 同序：每一条实际影响了多少行。
    // `^A` 命中两条、`B` 只命中 `xB three`（`A one` / `A two` 里没有 B）。
    QCOMPARE(result.hitsByPattern, QVector<int>({2, 1}));
    // 计数必须真的来自判定，不能靠「总丢行数」平摊——下面这条把两者区分开：
    // 第一条命中的行同时也被第二条命中时，第二条的计数也要涨。
    const LineFilter overlapping = filterOf(QStringLiteral("re:A\nre:one"));
    QCOMPARE(overlapping.filterLines(rawLines(QStringLiteral("A one\nA two")))
                 .hitsByPattern,
             QVector<int>({2, 1}));
}

void TstContentFilter::filterLinesWithEmptyFilterKeepsEveryLine()
{
    const QStringList input = rawLines(QStringLiteral("a\nb\nc"));
    const LineFilterResult result = LineFilter().filterLines(input);

    QCOMPARE(result.lines, asList(input));
    QCOMPARE(result.droppedLineCount(), 0);
    QVERIFY(result.droppedLines.isEmpty());
    QVERIFY(result.hitsByPattern.isEmpty());
}

void TstContentFilter::filterLinesHandlesEmptyInput()
{
    const LineFilter filter = filterOf(QStringLiteral("*.tmp"));
    const LineFilterResult result = filter.filterLines(QStringList());

    QCOMPARE(result.inputLineCount, 0);
    QCOMPARE(result.lines.size(), 0);
    QCOMPARE(result.droppedLineCount(), 0);
    // 没有输入时 hitsByPattern 仍然按表达式条数给出全 0，而不是空表——
    // 否则界面按下标取值时会越界。
    QCOMPARE(result.hitsByPattern, QVector<int>({0}));

    // 全部命中也是一种边界：结果为空但计数正确。
    const LineFilterResult allDropped =
        filter.filterLines(rawLines(QStringLiteral("a.tmp\nb.tmp")));
    QCOMPARE(allDropped.droppedLineCount(), 2);
    QCOMPARE(allDropped.keptLineCount(), 0);
}

void TstContentFilter::invalidPatternNeverExcludes()
{
    // 手工塞一条 `valid == false` 的表达式（解析路径不会产生它，
    // 但 `addPattern()` 是公开的，调用方可以）。它必须**一条都不排除**——
    // 半成品表达式参与了收窄，用户会看到文件凭空消失却查不到原因。
    LineFilter filter;
    LineFilterPattern broken;
    broken.mode = LineFilterMode::Regex;
    broken.pattern = QStringLiteral(".*");
    broken.valid = false;
    filter.addPattern(broken);

    QCOMPARE(filter.patternCount(), 1);
    QVERIFY(!filter.excludes(QStringLiteral("anything")));
    QVERIFY(filter.matchingPatternIndexes(QStringLiteral("anything")).isEmpty());
    QCOMPARE(filter.filterLines(rawLines(QStringLiteral("a\nb"))).droppedLineCount(), 0);
}

void TstContentFilter::filterLinesSummaryTextNamesBothCounts()
{
    const LineFilter filter = filterOf(QStringLiteral("re:DROP"));
    const LineFilterResult result =
        filter.filterLines(rawLines(QStringLiteral("a\nDROP b\nc")));

    // 两个数字都要在：只说「丢弃 1 行」看不出原来有多少行，
    // 用户无法判断这次过滤影响面有多大。
    const QString summary = result.summary();
    QVERIFY2(summary.contains(QStringLiteral("1")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("3")), qPrintable(summary));

    QCOMPARE(filter.describe(), QStringLiteral("行过滤：1 条表达式"));
}

void TstContentFilter::describeStatesPatternCount()
{
    // 描述文案要说清「几条表达式」：它是设置页那一栏标题的内容，
    // 而用户排查「怎么有一行没生效」时第一个看的就是这个数字。
    QCOMPARE(LineFilter().describe(), QStringLiteral("行过滤：0 条表达式"));

    const LineFilter two = filterOf(QStringLiteral("*.tmp\nre:^#"));
    QCOMPARE(two.describe(), QStringLiteral("行过滤：2 条表达式"));

    // 写坏的那一行不算进去（它不是表达式）。
    const LineFilter oneGood = filterOf(QStringLiteral("*.tmp\n[broken\n= X\n="));
    QCOMPARE(oneGood.patternCount(), 2);
    QCOMPARE(oneGood.describe(), QStringLiteral("行过滤：2 条表达式"));
}

// -----------------------------------------------------------------------------
// E 关键字节的转义编解码（第 2 条）
// -----------------------------------------------------------------------------

void TstContentFilter::decodeReadsHexEscapes()
{
    QString error;
    int column = -1;
    const QByteArray bytes =
        decodeByteSequenceText(QStringLiteral("\\x1f\\x8b\\x08\\x00"), &error, &column);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(column, -1);
    QCOMPARE(bytes, QByteArray::fromHex("1f8b0800"));

    // 大小写都认。
    QCOMPARE(decodeByteSequenceText(QStringLiteral("\\xAB\\xab")), QByteArray::fromHex("abab"));
}

void TstContentFilter::decodeReadsCstyleEscapes()
{
    QString error;
    const QByteArray bytes =
        decodeByteSequenceText(QStringLiteral("a\\nb\\rc\\td\\\\e\\0f"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // 字面量里 `\0` 后面还有 `f`，所以必须显式给长度（11 个字节）：
    // 让 QByteArray 自己数的话它会在 `\0` 处停住，得到 10 字节——
    // 而「长度对不对」正是这条用例要守的东西之一。
    QCOMPARE(bytes.size(), 11);
    QCOMPARE(bytes, QByteArray("a\nb\rc\td\\e\0f", 11));
    QCOMPARE(bytes.at(9), '\0');
    QCOMPARE(bytes.at(10), 'f');
}

void TstContentFilter::decodeUsesUtf8ForNonAscii()
{
    // 非 ASCII 按 UTF-8 编。**不**按本机编码：声明文本在进入本模块之前就已经是
    // 「正确解码后的文本」了，再按本机编码编回去会让同一份设置文件在两台机器上
    // 给出不同的字节序列，而两边都看不出差别。
    QString error;
    const QByteArray bytes = decodeByteSequenceText(QStringLiteral("中"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(bytes, QByteArray("中"));
    QCOMPARE(bytes, QByteArray::fromHex("e4b8ad"));
    QCOMPARE(bytes.size(), 3);

    // 混排也要对。
    QCOMPARE(decodeByteSequenceText(QStringLiteral("A\\x00中")),
             QByteArray("A\0中", 5));
}

void TstContentFilter::decodeReadsSurrogatePairAsOneUtf8Sequence()
{
    // 一个非 BMP 字符（U+1F600）在 QString 里是两个代理。逐个字符 toUtf8()
    // 会得到两个替换字符（6 字节），而整对一起编才是 4 字节。
    const QString emoji = QString::fromUcs4(U"😀");
    QCOMPARE(emoji.size(), 2); // 代理对：确实占了两个 QChar

    const QByteArray bytes = decodeByteSequenceText(emoji);
    QCOMPARE(bytes, QByteArray::fromHex("f09f9880"));
    QCOMPARE(bytes.size(), 4);
}

void TstContentFilter::decodeRejectsShortHexEscape()
{
    QString error;
    int column = -1;
    QCOMPARE(decodeByteSequenceText(QStringLiteral("\\x0"), &error, &column), QByteArray());
    QVERIFY(!error.isEmpty());
    QCOMPARE(column, 0);

    // `\x0g` 同样：两位里有一位不是十六进制。
    error.clear();
    column = -1;
    QCOMPARE(decodeByteSequenceText(QStringLiteral("\\x0g"), &error, &column), QByteArray());
    QVERIFY(!error.isEmpty());
    QCOMPARE(column, 0);

    // 报错时**整条序列**都不产出，而不是产出前半段：
    // 半个序列比没有序列更危险（它会去匹配一段根本没打算匹配的字节）。
    error.clear();
    QCOMPARE(decodeByteSequenceText(QStringLiteral("\\x41\\x0"), &error), QByteArray());
    QVERIFY(!error.isEmpty());
}

void TstContentFilter::decodeRejectsUnknownEscape()
{
    QString error;
    int column = -1;
    QCOMPARE(decodeByteSequenceText(QStringLiteral("\\q"), &error, &column), QByteArray());
    QVERIFY(!error.isEmpty());
    QCOMPARE(column, 0);

    // 位置要准：错在第三个转义上就报第三个转义的首字符列。
    // `\x41\x42` 各占 4 个字符，所以第三个转义从第 8 列起。
    error.clear();
    column = -1;
    QCOMPARE(decodeByteSequenceText(QStringLiteral("\\x41\\x42\\q"), &error, &column),
             QByteArray());
    QVERIFY(!error.isEmpty());
    QCOMPARE(column, 8);
}

void TstContentFilter::decodeRejectsTrailingBackslash()
{
    QString error;
    int column = -1;
    QCOMPARE(decodeByteSequenceText(QStringLiteral("ab\\"), &error, &column), QByteArray());
    QVERIFY(!error.isEmpty());
    QCOMPARE(column, 2);
}

void TstContentFilter::encodeKeepsPrintableAsciiVerbatim()
{
    QCOMPARE(encodeByteSequenceText(QByteArray("PNG")), QStringLiteral("PNG"));
    QCOMPARE(encodeByteSequenceText(QByteArray("a b")), QStringLiteral("a b"));
    // 反斜杠本身是可见 ASCII，原样输出；往返时它会被 `\\` 解回一个反斜杠。
    QCOMPARE(encodeByteSequenceText(QByteArray("\\")), QStringLiteral("\\"));
}

void TstContentFilter::encodeEscapesNonPrintableBytes()
{
    QCOMPARE(encodeByteSequenceText(QByteArray::fromHex("1f8b")), QStringLiteral("\\x1F\\x8B"));
    // 空格以外的控制字符全部转义。
    QCOMPARE(encodeByteSequenceText(QByteArray("\t")), QStringLiteral("\\x09"));
    // 高字节也转义（它们不是可见 ASCII）。
    QCOMPARE(encodeByteSequenceText(QByteArray::fromHex("e4b8ad")),
             QStringLiteral("\\xE4\\xB8\\xAD"));
}

void TstContentFilter::byteRoundTripIsExact()
{
    // 逐字节往返。**必须逐字节**：只比字符串长度或只 `contains` 会漏掉
    // 「某个字节变成了另一个」这种错。
    const QByteArray samples[] = {
        QByteArray::fromHex("1f8b08000000000000ff"),
        QByteArray("PNG\r\n\x1a\n"),
        QByteArray("中"),
        QByteArray("\0\0"),
        QByteArray(""),
    };

    for (const QByteArray &sample : samples) {
        QString error;
        int column = -1;
        const QByteArray back = decodeByteSequenceText(encodeByteSequenceText(sample),
                                                       &error, &column);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.size(), sample.size());
        QCOMPARE(back, sample);
    }
}

void TstContentFilter::byteRoundTripEscapesNewlineInsteadOfEmittingIt()
{
    // 这条盯的是那个具体取舍：`0x0A` **不**原样吐成换行。
    // 原样输出的话，往返回来的序列里会多一个真实换行，
    // 而两段文本在编辑器里长得完全不一样时反而更难解释「为什么不一样」。
    const QByteArray withNewline = QByteArray("a\nb");
    const QString encoded = encodeByteSequenceText(withNewline);

    QVERIFY2(!encoded.contains(QLatin1Char('\n')), qPrintable(encoded));
    QCOMPARE(encoded, QStringLiteral("a\\x0Ab"));
    QCOMPARE(decodeByteSequenceText(encoded), withNewline);
}

// -----------------------------------------------------------------------------
// F 关键字节声明与判定（第 2 条）
// -----------------------------------------------------------------------------

void TstContentFilter::keyByteParseSkipsCommentsAndBlankLines()
{
    const KeyByteParseResult parsed = KeyByteFilter::parseDeclaration(
        QStringLiteral("# 压缩包魔数\n"
                       "\n"
                       "   \n"
                       "\\x1f\\x8b"));

    QVERIFY2(parsed.ok(), qPrintable(parsed.describeErrors()));
    QCOMPARE(parsed.sequenceCount(), 1);
    QCOMPARE(parsed.filter.sequences().first().bytes, QByteArray::fromHex("1f8b"));

    // 首尾空白被裁掉（声明的缩进不该变成字节），要写空白用 `\x20`。
    const KeyByteParseResult indented =
        KeyByteFilter::parseDeclaration(QStringLiteral("   PNG   "));
    QCOMPARE(indented.filter.sequences().first().bytes, QByteArray("PNG"));
    QCOMPARE(KeyByteFilter::parseDeclaration(QStringLiteral("\\x20PNG\\x20"))
                 .filter.sequences().first().bytes,
             QByteArray(" PNG "));
}

void TstContentFilter::keyByteParseReportsSyntaxAtColumn()
{
    const KeyByteParseResult parsed =
        KeyByteFilter::parseDeclaration(QStringLiteral("PNG\n  \\q"));

    QVERIFY(!parsed.ok());
    QCOMPARE(parsed.issues.size(), 1);
    const KeyByteIssue issue = parsed.issues.first();
    QCOMPARE(issue.kind, KeyByteIssueKind::Syntax);
    QCOMPARE(issue.line, 2);
    QCOMPARE(issue.column, 2); // 前面两个空格 + 转义在第 0 列
    QVERIFY(!issue.message.isEmpty());
    QVERIFY(!issue.hint.isEmpty());

    // 写错的那一行不进过滤器（否则它会变成 `q` 这个字节）。
    QCOMPARE(parsed.sequenceCount(), 1);
}

void TstContentFilter::keyByteParseReportsDuplicateWithoutDroppingIt()
{
    // 重复不是错（结论不会变），但它是唯一能让「这一条从来没起过作用」
    // 被看出来的地方，所以报一条 Duplicate 而不是静静吃掉。
    const KeyByteParseResult parsed =
        KeyByteFilter::parseDeclaration(QStringLiteral("PNG\n\\x50\\x4E\\x47"));

    QVERIFY(!parsed.ok());
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.first().kind, KeyByteIssueKind::Duplicate);
    QCOMPARE(parsed.issues.first().line, 2);
    // 重复的那一条**仍然进过滤器**：丢掉它会改变 `AllOf` 下的条数语义，
    // 而用户并没有写错什么。
    QCOMPARE(parsed.sequenceCount(), 2);
}

void TstContentFilter::keyByteParseKeepsGoodLinesWhenOneIsBad()
{
    const KeyByteParseResult parsed = KeyByteFilter::parseDeclaration(
        QStringLiteral("\\x1f\\x8b\n"
                       "\\xzz\n"
                       "PNG"));

    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.first().line, 2);
    QCOMPARE(parsed.sequenceCount(), 2);

    KeyByteFilter filter = parsed.filter;
    filter.setCombineMode(KeyByteCombineMode::AllOf);
    QVERIFY(filter.decide(QByteArray::fromHex("1f8b504e47")).accepted());
}

void TstContentFilter::keyByteDeclarationRoundTripIsExact()
{
    const KeyByteFilter filter = keyFilterOf(QStringLiteral("\\x1f\\x8b\nPNG\r\n"), KeyByteCombineMode::AnyOf);
    // `\r\n` 是行尾而不是字节（见 parseAcceptsCrLfAndCrLineEndings 的同源约定）。
    QCOMPARE(filter.sequenceCount(), 2);

    const QString declaration = filter.toDeclarationText();
    const KeyByteFilter again = KeyByteFilter::parseDeclaration(declaration).filter;
    QCOMPARE(again.sequenceCount(), filter.sequenceCount());
    for (int index = 0; index < filter.sequenceCount(); ++index) {
        QCOMPARE(again.sequences().at(index).bytes, filter.sequences().at(index).bytes);
    }
    // 逐字幂等：两条序列用换行连起来（一行一条是声明的唯一形式）。
    QCOMPARE(again.toDeclarationText(), declaration);
    QCOMPARE(declaration, QStringLiteral("\\x1F\\x8B\nPNG"));

    // 单条序列时**没有**前后换行——`join("\n")` 而不是「每条后面加一个换行」。
    // 后者会让声明文本以换行结尾，往返回来的条数不变但文本不一致，
    // 而两者在终端里长得一模一样。
    QCOMPARE(keyFilterOf(QStringLiteral("PNG"), KeyByteCombineMode::AnyOf).toDeclarationText(),
             QStringLiteral("PNG"));
    QCOMPARE(KeyByteFilter().toDeclarationText(), QString());
}

void TstContentFilter::anyOfAcceptsWhenAnySequenceIsPresent()
{
    const KeyByteFilter filter =
        keyFilterOf(QStringLiteral("\\x1f\\x8b\nPNG\n%PDF"), KeyByteCombineMode::AnyOf);

    // 第一条命中就够。
    KeyByteDecision decision = filter.decide(QByteArray::fromHex("00001f8b0000"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Accepted);
    QVERIFY(decision.accepted());
    QCOMPARE(decision.matchedSequenceIndex, 0);

    // 第二条。
    decision = filter.decide(QByteArray("xxPNGxx"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Accepted);
    QCOMPARE(decision.matchedSequenceIndex, 1);

    // 第三条（多字节序列）。输入刻意**不**含前两条的字节，
    // 否则命中的会是先找到的那一条，而断言里那个下标就说明不了任何事。
    decision = filter.decide(QByteArray("%PDF-1.7"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Accepted);
    QCOMPARE(decision.matchedSequenceIndex, 2);

    // 命中顺序是「表里第一条命中的」，不是「最短的」也不是「最长的」。
    decision = filter.decide(QByteArray("%PDFandsomePNG"));
    QCOMPARE(decision.matchedSequenceIndex, 1);
}

void TstContentFilter::anyOfRejectsWhenNoneIsPresent()
{
    const KeyByteFilter filter =
        keyFilterOf(QStringLiteral("\\x1f\\x8b\nPNG"), KeyByteCombineMode::AnyOf);

    const KeyByteDecision decision = filter.decide(QByteArray("plain text __"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Rejected);
    QVERIFY(!decision.accepted());
    QCOMPARE(decision.matchedSequenceIndex, -1);
    QVERIFY(!decision.reason.isEmpty());

    // 空输入也不是「含任意一条」。
    QCOMPARE(filter.decide(QByteArray()).outcome, KeyByteOutcome::Rejected);

    // 子序列不算：只有头两个字节不构成 `\x1f\x8b\x08`。
    const KeyByteFilter threeBytes = keyFilterOf(QStringLiteral("\\x1f\\x8b\\x08"), KeyByteCombineMode::AnyOf);
    QCOMPARE(threeBytes.decide(QByteArray::fromHex("1f8b")).outcome, KeyByteOutcome::Rejected);
    QCOMPARE(threeBytes.decide(QByteArray::fromHex("1f8b08")).outcome, KeyByteOutcome::Accepted);
}

void TstContentFilter::allOfRequiresEverySequence()
{
    const KeyByteFilter filter =
        keyFilterOf(QStringLiteral("\\x1f\\x8b\n\\x08"), KeyByteCombineMode::AllOf);

    QCOMPARE(filter.decide(QByteArray::fromHex("1f8b08")).outcome, KeyByteOutcome::Accepted);
    // 少一条就排除，并且报出**缺的是哪一条**（否则用户只能自己逐个试）。
    KeyByteDecision decision = filter.decide(QByteArray::fromHex("1f8b00"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Rejected);
    QVERIFY2(decision.reason.contains(QStringLiteral("2")), qPrintable(decision.reason));

    decision = filter.decide(QByteArray::fromHex("0008"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Rejected);
    QVERIFY2(decision.reason.contains(QStringLiteral("1")), qPrintable(decision.reason));

    // 顺序无关：两条都在，出现次序倒过来也算含齐。
    QCOMPARE(filter.decide(QByteArray::fromHex("08001f8b")).outcome, KeyByteOutcome::Accepted);
}

void TstContentFilter::emptyRuleSetAcceptsEverything()
{
    // **本条目最容易写错的一处。** 规格说「仅当含指定序列时才纳入比较」，
    // 直译成「不含 → 排除」会让一条规则都没配时**全部排除**，用户的目录直接变空。
    // 空规则集的结论是「没有约束」。
    const KeyByteFilter empty;

    QVERIFY(empty.isEmpty());
    QCOMPARE(empty.sequenceCount(), 0);

    const KeyByteDecision decision = empty.decide(QByteArray("anything"));
    QCOMPARE(decision.outcome, KeyByteOutcome::Accepted);
    QVERIFY(decision.accepted());
    QCOMPARE(decision.matchedSequenceIndex, -1);
    // 空规则集不带「理由」——理由是给「为什么被排除」用的，
    // 这里没什么可解释的，硬编一句会让日志里出现无意义的一行。
    QVERIFY(decision.reason.isEmpty());

    // `AllOf` 下的空集同理（全称量化在空集上恒真，但要显式测）。
    KeyByteFilter emptyAll = empty;
    emptyAll.setCombineMode(KeyByteCombineMode::AllOf);
    QCOMPARE(emptyAll.decide(QByteArray("x")).outcome, KeyByteOutcome::Accepted);

    // 解析一份只有注释的声明同样得到空规则集，而不是报错。
    const KeyByteParseResult parsed =
        KeyByteFilter::parseDeclaration(QStringLiteral("# 什么都没有\n"));
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.sequenceCount(), 0);
}

void TstContentFilter::textInputIsNotApplicable()
{
    // 第 2 条的原话是「仅当**二进制文件**包含指定字节序列时才纳入比较」。
    // 文本文件走这一支时结论是「不适用」（放行），而不是「逐字节搜一遍」：
    // 后者看起来也对，但一个中文文本里搜 `E4` 会命中某个汉字的第一个字节，
    // 于是「按文件类型过滤」会悄悄变成「按字节碰运气」。
    const KeyByteFilter filter =
        keyFilterOf(QStringLiteral("\\xE4"), KeyByteCombineMode::AnyOf);

    const KeyByteDecision asBinary = filter.decide(QByteArray("中"), ContentKind::Binary);
    QCOMPARE(asBinary.outcome, KeyByteOutcome::Accepted);

    const KeyByteDecision asText = filter.decide(QByteArray("中"), ContentKind::Text);
    QCOMPARE(asText.outcome, KeyByteOutcome::NotApplicable);
    QVERIFY(asText.accepted()); // 不适用 = 放行
    QVERIFY(!asText.reason.isEmpty());

    // 默认参数是二进制（调用方要显式说「这是文本」才会走不适用那一支）。
    QCOMPARE(filter.decide(QByteArray("中")).outcome, KeyByteOutcome::Accepted);

    // 文本支下即使一条都不含也仍然放行。
    const KeyByteFilter unmatched = keyFilterOf(QStringLiteral("\\x1f\\x8b"), KeyByteCombineMode::AnyOf);
    QCOMPARE(unmatched.decide(QByteArray("plain"), ContentKind::Text).outcome,
             KeyByteOutcome::NotApplicable);
    QCOMPARE(unmatched.decide(QByteArray("plain"), ContentKind::Binary).outcome,
             KeyByteOutcome::Rejected);

    QCOMPARE(QString::fromLatin1(contentKindIdentifier(ContentKind::Binary)), QStringLiteral("binary"));
    QVERIFY(!contentKindLabel(ContentKind::Text).isEmpty());
    QCOMPARE(outcomeId(KeyByteOutcome::NotApplicable), QStringLiteral("not-applicable"));
}

void TstContentFilter::decisionReportsMatchedIndexAndReason()
{
    const KeyByteFilter filter =
        keyFilterOf(QStringLiteral("\\x1f\\x8b\nPNG"), KeyByteCombineMode::AnyOf);

    const KeyByteDecision decision = filter.decide(QByteArray("PNG"));
    QVERIFY2(decision.reason.contains(QStringLiteral("PNG")), qPrintable(decision.reason));
    QVERIFY2(decision.describe().contains(QStringLiteral("纳入比较")),
             qPrintable(decision.describe()));

    QCOMPARE(filter.describe(), QStringLiteral("关键字节：含任意一条（2 条）"));

    KeyByteFilter all = filter;
    all.setCombineMode(KeyByteCombineMode::AllOf);
    QCOMPARE(all.combineMode(), KeyByteCombineMode::AllOf);
    QVERIFY2(all.describe().contains(QStringLiteral("全部含齐")), qPrintable(all.describe()));
}

void TstContentFilter::combineModeTableIsTheSingleSourceOfTruth()
{
    const QVector<KeyByteCombineModeRow> rows = combineRows();
    const QVector<KeyByteCombineMode> modes = allKeyByteCombineModes();

    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.size(), modes.size());
    for (int index = 0; index < rows.size(); ++index) {
        QCOMPARE(rows.at(index).mode, modes.at(index));
        QCOMPARE(rows.at(index).label, keyByteCombineModeLabel(modes.at(index)));
        QCOMPARE(rows.at(index).explanation, keyByteCombineModeExplanation(modes.at(index)));
    }
    QCOMPARE(combineId(modes.at(0)), QStringLiteral("any-of"));
    QCOMPARE(combineId(modes.at(1)), QStringLiteral("all-of"));

    QVERIFY(validateKeyByteCombineModeTable(rows).isEmpty());
}

void TstContentFilter::combineModeTableSelfCheckRejectsDuplicateKey()
{
    QVector<KeyByteCombineModeRow> rows = combineRows();
    rows[1].key = rows[0].key;
    QVERIFY2(!validateKeyByteCombineModeTable(rows).isEmpty(), "设置键重复必须被报出来");

    rows = combineRows();
    rows[0].key = "";
    QVERIFY(!validateKeyByteCombineModeTable(rows).isEmpty());

    rows = combineRows();
    rows[0].identifier = "";
    QVERIFY(!validateKeyByteCombineModeTable(rows).isEmpty());

    rows = combineRows();
    rows[0].label.clear();
    QVERIFY(!validateKeyByteCombineModeTable(rows).isEmpty());

    rows = combineRows();
    rows[0].explanation.clear();
    QVERIFY(!validateKeyByteCombineModeTable(rows).isEmpty());

    rows = combineRows();
    rows.append(rows.at(0));
    QVERIFY(!validateKeyByteCombineModeTable(rows).isEmpty());

    rows = combineRows();
    rows.removeLast();
    QVERIFY2(joined(validateKeyByteCombineModeTable(rows)).contains(QStringLiteral("all-of")),
             qPrintable(joined(validateKeyByteCombineModeTable(rows))));
}

// -----------------------------------------------------------------------------
// G 启用状态与提示（规格边界条款）
// -----------------------------------------------------------------------------

void TstContentFilter::enablementDefaultsToOffWithNoRules()
{
    // 规格边界要求「必须显式启用」。默认值必须是**关**：
    // 一份从别人那里拿来的会话文件里可能带着满满的规则，
    // 而「我什么都没点，为什么每个文件都被读了一遍」不该发生。
    const ContentFilterEnablement state;

    QVERIFY(!state.enabled);
    QVERIFY(!state.hasRules);
    QVERIFY(!state.active());
    QVERIFY(!ContentFilterEnablement{}.active());
}

void TstContentFilter::enablementIsActiveOnlyWithBothFlagAndRules()
{
    ContentFilterEnablement state;
    state.hasRules = true;
    QVERIFY(!state.active()); // 配了规则但没启用 → 不生效

    state = ContentFilterEnablement{};
    state.enabled = true;
    QVERIFY(!state.active()); // 启用了但没有规则 → 不产生任何影响

    state.hasRules = true;
    QVERIFY(state.active());

    // 相等比较要按两个字段一起判，否则「切换启用」在界面上看不出脏。
    QVERIFY(state == (ContentFilterEnablement{true, true}));
    QVERIFY(state != (ContentFilterEnablement{true, false}));
    QVERIFY(state != (ContentFilterEnablement{false, true}));
}

void TstContentFilter::noticeWhenRulesConfiguredButNotEnabled()
{
    // 这一条最要紧：用户配完规则以为生效了，结果那些行还在结果里，
    // 而他唯一的线索就是这句话没出现过。
    const QString notice = contentFilterEnablementNotice(ContentFilterEnablement{false, true});
    QVERIFY2(notice.contains(QStringLiteral("未启用")), qPrintable(notice));
    QVERIFY2(!notice.contains(QStringLiteral("速度会降低")), qPrintable(notice));
}

void TstContentFilter::noticeWhenEnabledWithoutRules()
{
    const QString notice = contentFilterEnablementNotice(ContentFilterEnablement{true, false});
    QVERIFY2(notice.contains(QStringLiteral("没有配置任何规则")), qPrintable(notice));
    // 没有规则却说「速度会降低」是假警报。
    QVERIFY2(!notice.contains(QStringLiteral("速度会降低")), qPrintable(notice));
}

void TstContentFilter::noticeWhenNothingConfigured()
{
    const QString notice = contentFilterEnablementNotice(ContentFilterEnablement{});
    QVERIFY(!notice.isEmpty());
    QVERIFY2(!notice.contains(QStringLiteral("速度会降低")), qPrintable(notice));

    // 四种组合必须给出**四种不同**的话。两句一样就说明有一条分支白写了，
    // 而界面上表现为「两种情况提示相同」，用户分不清自己在哪一种里。
    const QString a = contentFilterEnablementNotice(ContentFilterEnablement{false, false});
    const QString b = contentFilterEnablementNotice(ContentFilterEnablement{false, true});
    const QString c = contentFilterEnablementNotice(ContentFilterEnablement{true, false});
    const QString d = contentFilterEnablementNotice(ContentFilterEnablement{true, true});
    QCOMPARE(QSet<QString>({a, b, c, d}).size(), 4);
}

void TstContentFilter::noticeWhenActiveIsThePerformanceNotice()
{
    // 规格第 4 条要求的那句性能提示。**这里只落数据**：
    // 状态栏的接线还没有（第 4 条因此保持未勾选），但文案本身是服务层的可测数据，
    // 先把「说法」定死，接界面时不该再发明第二句。
    const QString notice = contentFilterEnablementNotice(ContentFilterEnablement{true, true});
    QCOMPARE(notice, contentFilterPerformanceNotice());
    QVERIFY2(notice.contains(QStringLiteral("比较速度会降低")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("内容过滤已启用")), qPrintable(notice));
}

void TstContentFilter::settingsKeysAreStableAndDistinct()
{
    // 键名是**对外事实**：它会进会话文件，改名等于让所有用户已保存的声明失效。
    // 所以在用例里把它逐字钉住，而不是只断言「非空」。
    QCOMPARE(lineFilterDeclarationKey(), QStringLiteral("line-filter"));
    QCOMPARE(keyByteFilterDeclarationKey(), QStringLiteral("key-byte-filter"));
    QCOMPARE(contentFilterEnabledKey(), QStringLiteral("content-filter-enabled"));

    // 三个键必须互不相同：撞车会让两个声明互相覆盖，而现象是
    // 「我写的行过滤不见了」，与键名这件事看起来毫无关系。
    QCOMPARE(QSet<QString>({lineFilterDeclarationKey(), keyByteFilterDeclarationKey(),
                            contentFilterEnabledKey()})
                 .size(),
             3);

    // 也不能与既有过滤器的键撞车——它们住在同一个设置命名空间里。
    QVERIFY(lineFilterDeclarationKey() != nameFilterDeclarationKey());
}

// -----------------------------------------------------------------------------
// H 三张表的自检与反向验证
// -----------------------------------------------------------------------------

void TstContentFilter::builtinTablesPassTheCombinedSelfCheck()
{
    const QVector<QString> problems = validateBuiltinContentFilterTables();
    QVERIFY2(problems.isEmpty(), qPrintable(joined(problems)));

    // 逐张单独跑也要干净（汇总入口不能靠「少调一张表」来变干净）。
    QVERIFY(validateContentFilterStageTable(stageRows()).isEmpty());
    QVERIFY(validateLineFilterModeTable(lineModeRows()).isEmpty());
    QVERIFY(validateKeyByteCombineModeTable(combineRows()).isEmpty());
}

void TstContentFilter::combinedSelfCheckReportsEveryBrokenTable()
{
    // 三张表各写坏一张，汇总入口必须**都**报出来。
    // 只报第一张的实现在这里会红——那正是「汇总」这个词的意义。
    QVector<ContentFilterStageRow> stages = stageRows();
    std::swap(stages[0], stages[1]);
    QVector<LineFilterModeRow> modes = lineModeRows();
    modes[indexOfMode(modes, LineFilterMode::Regex)].wholeLine = true;
    QVector<KeyByteCombineModeRow> combines = combineRows();
    combines[1].key = combines[0].key;

    const QVector<QString> problems = validateContentFilterTables(stages, modes, combines);
    QVERIFY(problems.size() >= 3);

    const QString text = joined(problems);
    QVERIFY2(text.contains(QStringLiteral("顺序")), qPrintable(text));      // 阶段表那张
    QVERIFY2(text.contains(QStringLiteral("子串")), qPrintable(text));      // 模式表那张
    QVERIFY2(text.contains(QStringLiteral("any-of")) || text.contains(QStringLiteral("all-of")),
             qPrintable(text));                                            // 组合语义那张

    // 反向对照：三张表都完好时同一入口必须是干净的。
    // 没有这一半的话，一个「永远报错」的实现同样能过上面那些断言。
    QVERIFY2(validateContentFilterTables(stageRows(), lineModeRows(), combineRows()).isEmpty(),
             "完好的三张表不该被报出任何问题");
}

void TstContentFilter::repeatedTableLookupsStayStable()
{
    // 表是函数内静态、以引用交出去的。**返回临时容器的写法会在这里现形**：
    // 第一次取到的标签还正常，第二次就变成空串（或直接崩）。
    // 这条用例盯的就是 §6 那条「函数内构造的临时容器被返回，指针随即悬垂」。
    const QString firstStage = contentFilterStageLabel(ContentFilterStage::LineFilter);
    const QString firstMode = lineFilterModeLabel(LineFilterMode::Regex);
    const QVector<LineFilterModeRow> rows = lineModeRows();

    for (int round = 0; round < 8; ++round) {
        QCOMPARE(contentFilterStageLabel(ContentFilterStage::LineFilter), firstStage);
        QCOMPARE(lineFilterModeLabel(LineFilterMode::Regex), firstMode);
        // 取行地址的入口也要稳定（表行的地址不能每轮都不同，否则内部一致性
        // 比较会莫名其妙地失败）。
        QCOMPARE(lineFilterModeRow(LineFilterMode::Regex), &rows.at(indexOfMode(rows, LineFilterMode::Regex)));
    }

    QVERIFY(!firstStage.isEmpty());
    QVERIFY(!firstMode.isEmpty());
    QVERIFY(contentFilterStageIdentifier(ContentFilterStage::IgnoreRules)[0] != '\0');
}

// -----------------------------------------------------------------------------
// I 源码级护栏
// -----------------------------------------------------------------------------

void TstContentFilter::moduleNeverReadsFiles()
{
    // 与 attributefilter 同一条纪律：内容过滤器只接受调用方**已经读进来**的
    // 字节与行，自己不碰文件系统。理由不是洁癖——一处「顺手读一下」会让
    // 「什么代价都不付」这个前提消失，而本条目之所以要把启用做成显式状态，
    // 正是因为读文件内容是这里唯一的高代价动作。
    const QStringList sources = QStringList{QStringLiteral("/Services/Filter/contentfilter.h"),
                                            QStringLiteral("/Services/Filter/contentfilter.cpp")};

    for (const QString &source : sources) {
        const QString text = readSourceFile(source);
        QVERIFY2(!text.isEmpty(), qPrintable(source));
        QVERIFY2(!touchesFileSystem(text),
                 qPrintable(QStringLiteral("%1 里出现了读文件的东西——"
                                           "内容过滤器不得自己读文件").arg(source)));
    }

    // 去注释这一步本身也要能自证：本头文件的说明里**逐字**写着那几个标识符，
    // 所以「原文里有、去注释后没有」是可以直接断言的。没有这一条的话，
    // stripComments() 一旦写错（例如把 `//` 判成注释却没跳掉内容），
    // 上面那两条就会**静默通过**——而它们正是唯一在守这条约束的东西。
    const QString header = readSourceFile(QStringLiteral("/Services/Filter/contentfilter.h"));
    QVERIFY2(header.contains(QStringLiteral("QFile")), "头文件说明里确实提到了 QFile");
    QVERIFY2(!stripComments(header).contains(QStringLiteral("QFile")),
             "去注释之后不该还有 QFile");

    // 它也不该依赖比对引擎：忽略规则属于阶段二，住在 Services/Text。
    // 模块一旦 include 它，「先过滤行再应用忽略规则」这条边界就会开始模糊。
    QVERIFY2(!touchesInclude(header, QStringLiteral("textdiff.h")),
             "contentfilter.h 不得依赖 Services/Text");
    QVERIFY2(!touchesInclude(header, QStringLiteral("textdocument.h")),
             "contentfilter.h 不得依赖 Services/Text");

    // 沿用现成的回溯预检实现，不写第二份。
    const QString implementation = readSourceFile(QStringLiteral("/Services/Filter/contentfilter.cpp"));
    QVERIFY2(implementation.contains(QStringLiteral("analyzeRegexPatternRisk")),
             "回溯风险预检必须复用 namefilter 的那一份实现");
}

void TstContentFilter::sourceGuardWouldCatchAnInjectedRead()
{
    // 上面那条护栏不能是恒真的：拿一段**故意植入读文件代码**的源码跑同一个判定，
    // 它必须报出来。
    const QString clean = readSourceFile(QStringLiteral("/Services/Filter/contentfilter.h"));

    // 1) 代码里出现读文件的东西 → 报。
    const QString injected =
        clean + QStringLiteral("\ninline QByteArray peek(const QString &p) "
                               "{ return QFile(p).readAll(); }\n");
    QVERIFY2(injected.contains(QStringLiteral("QFile")), "注入必须真的生效");
    QVERIFY2(touchesFileSystem(injected), "植入了 QFile::readAll 的源码必须被拦下");

    // 2) **注释里**出现同样的字样 → 不报（这正是一开始那条假错的成因）。
    const QString commented =
        clean + QStringLiteral("\n// 这里顺手 QFile(p).readAll() 一下就行\n");
    QVERIFY2(!touchesFileSystem(commented), "注释里的字样不算违规");
    // 而它确实是被「去注释」去掉的，不是因为原文里没有——否则这条断言恒真。
    QVERIFY(commented.contains(QStringLiteral("readAll")));

    // 3) 去掉注释不会把代码一起吞掉：块注释之后紧跟的代码仍要被抓到。
    const QString blockCommentThenCode =
        QStringLiteral("/* QFile */ int x = 0;\nQDir d(QStringLiteral(\".\"));\n");
    QVERIFY(!stripComments(blockCommentThenCode).contains(QStringLiteral("QFile")));
    QVERIFY(touchesFileSystem(blockCommentThenCode));

    // 4) 只有注释、没有代码的一小段不该被误报。
    QVERIFY(!touchesFileSystem(QStringLiteral("// 只有一行注释\nint y = 0;\n")));
}

QTEST_MAIN(TstContentFilter)
