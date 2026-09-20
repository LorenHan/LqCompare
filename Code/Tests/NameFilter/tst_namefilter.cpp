#include "tst_namefilter.h"

#include "namefilter.h"

#include <QElapsedTimer>
#include <QFile>
#include <QThread>

using namespace LqCompare;
using namespace LqCompare::Filter;

namespace {

/// 供 D 组使用的可编排运行器：按脚本依次报「超时」或「真跑一遍」。
///
/// 为什么要替身而不是真等 200ms：真等一次就是 200ms × 用例数，而且机器一忙
/// 结论就飘。策略（超时之后发生什么）必须能被**确定性**地断言；真实机制另在
/// E 组用少量用例单独测。
class ScriptedRunner : public NameMatchRunner
{
public:
    /// 依次消费：`true` 表示这一次报超时。脚本用完了就正常执行。
    QVector<bool> script;
    int calls = 0;
    int lastBudgetMs = -1;

    Outcome run(const std::function<bool()> &task, int budgetMs) override
    {
        ++calls;
        lastBudgetMs = budgetMs;
        if (!script.isEmpty()) {
            const bool shouldTimeOut = script.takeFirst();
            if (shouldTimeOut) {
                return Outcome{Status::TimedOut, false};
            }
        }
        return Outcome{Status::Completed, task()};
    }
};

std::shared_ptr<ScriptedRunner> scriptedRunner(const QVector<bool> &script = QVector<bool>())
{
    auto runner = std::make_shared<ScriptedRunner>();
    runner->script = script;
    return runner;
}

NameFilter buildFilter(const QString &declaration, NameCombineMode combine,
                       MaskPlatform platform = currentMaskPlatform())
{
    NameFilterParseResult parsed = NameFilter::parse(declaration, platform);
    parsed.filter.setCombineMode(combine);
    return parsed.filter;
}

QString modeId(NameMatchMode mode)
{
    return QString::fromLatin1(nameMatchModeIdentifier(mode));
}

QString combineId(NameCombineMode mode)
{
    return QString::fromLatin1(nameCombineModeIdentifier(mode));
}

QString kindId(NameFilterIssueKind kind)
{
    return QString::fromLatin1(nameFilterIssueKindIdentifier(kind));
}

QString outcomeId(NameMatchOutcome outcome)
{
    return QString::fromLatin1(nameMatchOutcomeIdentifier(outcome));
}

QString runnerStatusId(NameMatchRunner::Status status)
{
    return status == NameMatchRunner::Status::Completed ? QStringLiteral("completed")
                                                        : QStringLiteral("timed-out");
}

/// 源码级护栏用的禁用词：名称过滤一旦去读文件系统，这些名字必然出现。
QString joinProblems(const QVector<QString> &problems)
{
    // `QVector` 没有 `join`（那是 `QStringList` 的）。拼成 `QStringList` 再 join，
    // 免得在断言里写一长串循环。
    QStringList list;
    for (const QString &problem : problems) {
        list.append(problem);
    }
    return list.join(QStringLiteral("；"));
}

QStringList forbiddenFileSystemTokens()
{
    return QStringList{QStringLiteral("QFile"), QStringLiteral("QFileInfo"),
                       QStringLiteral("QDir"), QStringLiteral("readAll"),
                       QStringLiteral("QTextStream"), QStringLiteral("QDataStream")};
}

QStringList scanForFileSystemAccess(const QString &source)
{
    QStringList found;
    for (const QString &token : forbiddenFileSystemTokens()) {
        if (source.contains(token)) {
            found.append(token);
        }
    }
    return found;
}

} // namespace

void TstNameFilter::initTestCase()
{
    QVERIFY2(!readSourceFile(QStringLiteral("/Services/Filter/namefilter.h")).isEmpty(),
             "读不到 namefilter.h：LQCOMPARE_CODE_ROOT 指向不对？");
    QVERIFY2(!readSourceFile(QStringLiteral("/Services/Filter/namefilter.cpp")).isEmpty(),
             "读不到 namefilter.cpp：LQCOMPARE_CODE_ROOT 指向不对？");
}

QString TstNameFilter::readSourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// -----------------------------------------------------------------------------
// A 三种匹配模式（第 1 条）
// -----------------------------------------------------------------------------

void TstNameFilter::exactMatchesOnlyWholeName()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("= README.md"));
    QCOMPARE(parsed.filter.expressionCount(), 1);
    QCOMPARE(modeId(parsed.filter.expressions().at(0).mode), QStringLiteral("exact"));
    QCOMPARE(parsed.filter.expressions().at(0).text, QStringLiteral("README.md"));

    QVERIFY(parsed.filter.accepts(QStringLiteral("README.md")));
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("README.md.bak")),
             "精确名不应当命中更长的名字");
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("readme.md")),
             "posix 默认大小写敏感");
    QVERIFY(!parsed.filter.accepts(QStringLiteral("README.md ")));
}

void TstNameFilter::exactHonorsCaseSensitivity()
{
    NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("= README.md"));
    parsed.filter.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(parsed.filter.isCaseSensitivityOverridden());
    QVERIFY(parsed.filter.accepts(QStringLiteral("readme.md")));
    // 不敏感只放宽**大小写**，不是「什么都匹配」——少了这一条断言，
    // 一个「不敏感分支恒返回 true」的实现照样能过（本轮的变异 M02 正是这样漏掉的）。
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("other.md")),
             "大小写不敏感不等于命中一切");
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("README.md.bak")),
             "大小写不敏感只放宽大小写，不放宽整名匹配");

    // 平台默认也要能被真实执行：Windows 默认不敏感。
    const NameFilterParseResult windows =
        NameFilter::parse(QStringLiteral("= README.md"), MaskPlatform::Windows);
    QVERIFY2(windows.filter.accepts(QStringLiteral("readme.md")),
             "Windows 平台默认应当大小写不敏感");
    QVERIFY(windows.filter.accepts(QStringLiteral("README.md")));
}

void TstNameFilter::wildcardUsesMaskSyntaxAndMatchesWholeName()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("*.cpp\nsrc/*.cpp"));
    QCOMPARE(parsed.filter.expressionCount(), 2);
    QCOMPARE(modeId(parsed.filter.expressions().at(0).mode), QStringLiteral("wildcard"));

    // 不含 `/` 的掩码按名字匹配（掩码自己的规则），含 `/` 的按相对路径匹配。
    QVERIFY(parsed.filter.accepts(QStringLiteral("main.cpp")));
    QVERIFY(parsed.filter.accepts(QStringLiteral("src/main.cpp")));
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("main.cpp.bak")),
             "掩码是整名匹配，`*.cpp` 不该命中 `.cpp.bak`");
    QVERIFY(!parsed.filter.accepts(QStringLiteral("main.h")));
}

void TstNameFilter::regexMatchesWholeNameNotSubstring()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:README"));
    QCOMPARE(modeId(parsed.filter.expressions().at(0).mode), QStringLiteral("regex"));
    QCOMPARE(parsed.filter.expressions().at(0).text, QStringLiteral("README"));

    QVERIFY(parsed.filter.accepts(QStringLiteral("README")));
    // 这条是三种模式「范围一致」的核心断言：切到正则不该悄悄变成子串搜索。
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("xREADMEx")),
             "正则模式是整名匹配，不是子串搜索");
}

void TstNameFilter::regexKeepsUserAnchorsWorking()
{
    // 用户自己写的锚点必须按 PCRE2 的规则解释——因此实现用的是
    // 「跑一次普通匹配 + 检查它是否盖住整个名字」，而不是把表达式包进 `^(?:…)$`。
    const NameFilterParseResult anchored = NameFilter::parse(QStringLiteral("re:^a.*z$"));
    QVERIFY(anchored.filter.accepts(QStringLiteral("abcz")));
    QVERIFY(!anchored.filter.accepts(QStringLiteral("zabc")));

    const NameFilterParseResult alternation = NameFilter::parse(QStringLiteral("re:a|b"));
    QVERIFY(alternation.filter.accepts(QStringLiteral("a")));
    QVERIFY(alternation.filter.accepts(QStringLiteral("b")));
    QVERIFY2(!alternation.filter.accepts(QStringLiteral("ab")),
             "`a|b` 的整名匹配不应当命中 `ab`");
}

void TstNameFilter::regexSubstringNeedsExplicitDotStar()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:.*READ.*"));
    QVERIFY(parsed.filter.accepts(QStringLiteral("xREADMEx")));
    QVERIFY(parsed.filter.accepts(QStringLiteral("READ")));
    QVERIFY(!parsed.filter.accepts(QStringLiteral("readme")));
}

void TstNameFilter::regexHonorsCaseSensitivity()
{
    // 三个模式都要遵守同一个大小写开关。正则这一支最容易漏：它的选项被编进了
    // 正则对象里，而不是每次判定现算的（漏掉的现象是「正则模式下开关不起作用」，
    // 另外两个模式却正常）。
    NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:^readme$"));
    QVERIFY(parsed.filter.accepts(QStringLiteral("readme")));
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("README")), "posix 默认大小写敏感");

    parsed.filter.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(parsed.filter.accepts(QStringLiteral("README")));
    QVERIFY(parsed.filter.accepts(QStringLiteral("ReadMe")));

    // 平台默认也要能真实执行到正则这一支。
    const NameFilterParseResult windows =
        NameFilter::parse(QStringLiteral("re:^readme$"), MaskPlatform::Windows);
    QVERIFY(windows.filter.accepts(QStringLiteral("README")));
}

void TstNameFilter::wildcardMetacharactersAreNotRegex()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("a.c"));
    QVERIFY(parsed.filter.accepts(QStringLiteral("a.c")));
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("abc")),
             "`.` 在掩码语法里是字面点，不是正则的任意字符");
}

void TstNameFilter::modePrefixTableIsTheSingleSourceOfTruth()
{
    const QVector<NameMatchModePrefix> table = nameMatchModePrefixTable();
    QCOMPARE(table.size(), allNameMatchModes().size());
    QCOMPARE(table.size(), 3);

    // 表里的模式集合与 allNameMatchModes() 必须一致（两份枚举迟早分家）。
    for (NameMatchMode mode : allNameMatchModes()) {
        bool found = false;
        for (const NameMatchModePrefix &entry : table) {
            if (entry.mode == mode) {
                found = true;
                QCOMPARE(entry.prefix, nameMatchModePrefix(mode));
            }
        }
        QVERIFY2(found, qPrintable(QStringLiteral("模式 %1 不在前缀表里").arg(modeId(mode))));
    }

    QCOMPARE(nameMatchModePrefix(NameMatchMode::Exact), QStringLiteral("= "));
    QCOMPARE(nameMatchModePrefix(NameMatchMode::Regex), QStringLiteral("re:"));
    QCOMPARE(nameMatchModePrefix(NameMatchMode::Wildcard), QString());
}

void TstNameFilter::modeKeysAndLabelsAreDistinctAndStable()
{
    QSet<QString> keys;
    QSet<QString> labels;
    QSet<QString> identifiers;
    for (NameMatchMode mode : allNameMatchModes()) {
        keys.insert(nameMatchModeKey(mode));
        labels.insert(nameMatchModeLabel(mode));
        identifiers.insert(modeId(mode));
        QVERIFY(!nameMatchModeKey(mode).isEmpty());
        QVERIFY(!nameMatchModeLabel(mode).isEmpty());
        // 稳定键必须全是 ASCII：它会进预设文件，中文键在不同行尾/编码下容易出事。
        for (const QChar c : nameMatchModeKey(mode)) {
            QVERIFY2(c.unicode() < 128, "稳定键里出现了非 ASCII 字符");
        }
    }
    QCOMPARE(keys.size(), 3);
    QCOMPARE(labels.size(), 3);
    QCOMPARE(identifiers.size(), 3);
    QVERIFY(keys.contains(QStringLiteral("exact")));
    QVERIFY(keys.contains(QStringLiteral("wildcard")));
    QVERIFY(keys.contains(QStringLiteral("regex")));
}

void TstNameFilter::modeKeysRoundTripThroughFromKey()
{
    for (NameMatchMode mode : allNameMatchModes()) {
        bool ok = false;
        const NameMatchMode back = nameMatchModeFromKey(nameMatchModeKey(mode), &ok);
        QVERIFY(ok);
        QCOMPARE(modeId(back), modeId(mode));
    }
    bool ok = true;
    nameMatchModeFromKey(QStringLiteral("nope"), &ok);
    QVERIFY2(!ok, "不认识的键必须把 ok 置假，不能悄悄退化成某个默认值");
}

void TstNameFilter::declarationTextAlwaysWritesTheModeWhenItIsNotWildcard()
{
    const QString canonical = QStringLiteral("*.cpp\nre:^a$\n= README.md");
    const NameFilterParseResult parsed = NameFilter::parse(canonical);
    QCOMPARE(parsed.filter.expressionCount(), 3);
    QCOMPARE(parsed.filter.toDeclarationText(), canonical);

    // 逆：把写出来的文本再读一遍，得到语义相同的过滤器。
    const NameFilterParseResult again = NameFilter::parse(parsed.filter.toDeclarationText());
    QCOMPARE(again.filter.expressionCount(), 3);
    QVERIFY(again.filter.accepts(QStringLiteral("a.cpp")));
    QVERIFY(again.filter.accepts(QStringLiteral("a")));
    QVERIFY(again.filter.accepts(QStringLiteral("README.md")));
}

void TstNameFilter::defaultMatchModeDoesNotReinterpretExistingLines()
{
    NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("README.md"));
    QCOMPARE(modeId(parsed.filter.expressions().at(0).mode), QStringLiteral("wildcard"));

    // 换一下「新表达式默认模式」，已有表达式的模式与语义都不许变。
    parsed.filter.setDefaultMatchMode(NameMatchMode::Regex);
    QCOMPARE(modeId(parsed.filter.expressions().at(0).mode), QStringLiteral("wildcard"));
    QVERIFY(parsed.filter.accepts(QStringLiteral("README.md")));
    QVERIFY2(!parsed.filter.accepts(QStringLiteral("xREADME.mdx")),
             "换下拉框不该把一条掩码表达式变成正则（那会静默改变它的含义）");
    QCOMPARE(modeId(parsed.filter.defaultMatchMode()), QStringLiteral("regex"));
}

void TstNameFilter::prefixShadowingBoundaryHasADocumentedEscape()
{
    // 边界：行首的 `=` / `re:` 永远被当成模式前缀——这是有意的。
    // 两种绕法各测一次：`= =foo`（前缀后允许空白）与 `re:` 模式自己表达。
    const NameFilterParseResult prefixed = NameFilter::parse(QStringLiteral("= =foo"));
    QCOMPARE(prefixed.filter.expressionCount(), 1);
    QCOMPARE(modeId(prefixed.filter.expressions().at(0).mode), QStringLiteral("exact"));
    QCOMPARE(prefixed.filter.expressions().at(0).text, QStringLiteral("=foo"));
    QVERIFY(prefixed.filter.accepts(QStringLiteral("=foo")));

    const NameFilterParseResult tight = NameFilter::parse(QStringLiteral("==foo"));
    QCOMPARE(tight.filter.expressions().at(0).text, QStringLiteral("=foo"));
    QVERIFY(tight.filter.accepts(QStringLiteral("=foo")));

    // 前缀优先的判别性证据：`*.cpp` 作为掩码是**完全合法**的，而作为正则是
    // 「量词前面没有可重复的项」。它被报成语法错，就说明这一行确实交给了正则
    // 编译器，而不是被当成掩码。
    const NameFilterParseResult shadowed = NameFilter::parse(QStringLiteral("re:*.cpp"));
    QCOMPARE(shadowed.filter.expressionCount(), 0);
    QCOMPARE(shadowed.issues.size(), 1);
    QCOMPARE(kindId(shadowed.issues.at(0).kind), QStringLiteral("syntax"));

    // 掩码侧照旧：同样一段文本、不加前缀，必须被接受。
    const NameFilterParseResult asMask = NameFilter::parse(QStringLiteral("*.cpp"));
    QCOMPARE(asMask.filter.expressionCount(), 1);
    QVERIFY(asMask.filter.accepts(QStringLiteral("a.cpp")));
}

void TstNameFilter::modeSemanticsNoteSaysWholeNameForAllThree()
{
    for (NameMatchMode mode : allNameMatchModes()) {
        const QString note = nameMatchModeSemanticsNote(mode);
        QVERIFY(!note.isEmpty());
        QVERIFY2(note.contains(QStringLiteral("整名匹配")),
                 qPrintable(QStringLiteral("模式 %1 的说明没有写明「整名匹配」").arg(modeId(mode))));
    }
}

// -----------------------------------------------------------------------------
// B 组合语义（第 3 条）
// -----------------------------------------------------------------------------

void TstNameFilter::anyOfKeepsWhenAnyExpressionMatches()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.cpp\n*.h"), NameCombineMode::AnyOf);
    QVERIFY(filter.accepts(QStringLiteral("a.cpp")));
    QVERIFY(filter.accepts(QStringLiteral("a.h")));

    const NameFilterDecision decision = filter.decide(QStringLiteral("a.h"));
    QCOMPARE(outcomeId(decision.outcomes.at(0)), QStringLiteral("not-matched"));
    QCOMPARE(outcomeId(decision.outcomes.at(1)), QStringLiteral("matched"));
    QCOMPARE(decision.decisiveIndex, 1);
    QVERIFY(decision.accepted);
}

void TstNameFilter::anyOfRejectsWhenNothingMatches()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.cpp\n*.h"), NameCombineMode::AnyOf);
    QVERIFY(!filter.accepts(QStringLiteral("a.txt")));

    const NameFilterDecision decision = filter.decide(QStringLiteral("a.txt"));
    QCOMPARE(decision.decisiveIndex, -1);
    QVERIFY(!decision.accepted);
    QVERIFY(decision.matchedIndexes.isEmpty());
}

void TstNameFilter::noneOfRejectsWhenAnyExpressionMatches()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.tmp\n*.log"), NameCombineMode::NoneOf);
    QVERIFY(!filter.accepts(QStringLiteral("a.tmp")));
    QVERIFY(!filter.accepts(QStringLiteral("a.log")));

    const NameFilterDecision decision = filter.decide(QStringLiteral("a.tmp"));
    QCOMPARE(decision.decisiveIndex, 0);
    QVERIFY2(!decision.accepted, "「不包含任何」下命中即排除");
}

void TstNameFilter::noneOfKeepsWhenNothingMatches()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.tmp\n*.log"), NameCombineMode::NoneOf);
    QVERIFY(filter.accepts(QStringLiteral("a.cpp")));
}

void TstNameFilter::allOfRequiresEveryExpression()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.cpp\nmain*"), NameCombineMode::AllOf);
    QVERIFY(filter.accepts(QStringLiteral("main.cpp")));
    QVERIFY2(!filter.accepts(QStringLiteral("other.cpp")),
             "「全部满足」下少命中一条就该排除");
    QVERIFY2(!filter.accepts(QStringLiteral("main.h")), "「全部满足」下少命中一条就该排除");
}

void TstNameFilter::allOfReportsTheFirstMissAsDecisive()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("main*\n*.cpp"), NameCombineMode::AllOf);
    const NameFilterDecision decision = filter.decide(QStringLiteral("other.h"));
    QCOMPARE(outcomeId(decision.outcomes.at(0)), QStringLiteral("not-matched"));
    // 两条都不命中，起决定作用的是**第一条**（界面要能指出「从这里开始不满足」）。
    QCOMPARE(decision.decisiveIndex, 0);
    QVERIFY(!decision.accepted);
}

void TstNameFilter::noneOfReportsTheHitAsDecisive()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.cpp\n*.tmp"), NameCombineMode::NoneOf);
    const NameFilterDecision decision = filter.decide(QStringLiteral("a.tmp"));
    QCOMPARE(decision.decisiveIndex, 1);
    QVERIFY(decision.matchedIndexes.contains(1));
}

void TstNameFilter::combineSummaryNamesTheCurrentSemantics()
{
    QSet<QString> summaries;
    for (NameCombineMode mode : allNameCombineModes()) {
        NameFilter filter = buildFilter(QStringLiteral("*.cpp\n*.h"), mode);
        const QString summary = filter.combineSummary();
        summaries.insert(summary);
        QVERIFY2(summary.contains(nameCombineModeLabel(mode)),
                 "摘要必须写出当前语义的名字");
        QVERIFY2(summary.contains(nameCombineModeExplanation(mode)),
                 "摘要必须带上这一语义的完整解释（界面就是靠它说清「意味着什么」）");
        QVERIFY2(summary.contains(QStringLiteral("2 条表达式")),
                 "摘要必须带上表达式条数");
    }
    QCOMPARE(summaries.size(), 3);
}

void TstNameFilter::combineSummaryForEmptyFilterSaysNothingIsFiltered()
{
    NameFilter filter;
    filter.setCombineMode(NameCombineMode::NoneOf);
    const QString summary = filter.combineSummary();
    QVERIFY2(summary.contains(QStringLiteral("不过滤")),
             "空过滤器必须说清「什么都不过滤」，否则用户以为它在拦东西");
}

void TstNameFilter::combineTableIsCompleteAndExplanationsDiffer()
{
    const QVector<NameCombineMode> modes = allNameCombineModes();
    QCOMPARE(modes.size(), 3);
    QCOMPARE(combineId(NameCombineMode::AnyOf), QStringLiteral("any-of"));
    QCOMPARE(combineId(NameCombineMode::NoneOf), QStringLiteral("none-of"));
    QCOMPARE(combineId(NameCombineMode::AllOf), QStringLiteral("all-of"));

    QSet<QString> labels;
    QSet<QString> explanations;
    QSet<QString> keys;
    for (NameCombineMode mode : modes) {
        labels.insert(nameCombineModeLabel(mode));
        explanations.insert(nameCombineModeExplanation(mode));
        keys.insert(nameCombineModeKey(mode));
    }
    QCOMPARE(labels.size(), 3);
    QCOMPARE(explanations.size(), 3);
    QCOMPARE(keys.size(), 3);
}

void TstNameFilter::decisionDescribeReportsOutcomes()
{
    const NameFilter filter =
        buildFilter(QStringLiteral("*.cpp\n*.h"), NameCombineMode::AnyOf);
    const NameFilterDecision decision = filter.decide(QStringLiteral("a.h"));
    const QString text = decision.describe();
    QVERIFY(text.contains(QStringLiteral("包含任一")));
    QVERIFY(text.contains(QStringLiteral("保留")));
    QVERIFY2(text.contains(QStringLiteral("第 2 条")), "必须指出是哪一条决定的");
    QVERIFY(text.contains(QStringLiteral("命中")));
}

void TstNameFilter::everyOutcomeHasAnIdentifierAndLabel()
{
    const QVector<NameMatchOutcome> outcomes{NameMatchOutcome::NotMatched,
                                             NameMatchOutcome::Matched,
                                             NameMatchOutcome::Undecided};
    QSet<QString> identifiers;
    QSet<QString> labels;
    for (NameMatchOutcome outcome : outcomes) {
        identifiers.insert(outcomeId(outcome));
        labels.insert(nameMatchOutcomeLabel(outcome));
        QVERIFY(!nameMatchOutcomeLabel(outcome).isEmpty());
    }
    QCOMPARE(identifiers.size(), 3);
    QCOMPARE(labels.size(), 3);
    QVERIFY(identifiers.contains(QStringLiteral("undecided")));
}

// -----------------------------------------------------------------------------
// C 实时校验（第 4 条）
// -----------------------------------------------------------------------------

void TstNameFilter::invalidRegexIsReportedWithPosition()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:("));
    QCOMPARE(parsed.filter.expressionCount(), 0);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(kindId(parsed.issues.at(0).kind), QStringLiteral("syntax"));
    QCOMPARE(parsed.issues.at(0).line, 0);
    QVERIFY2(parsed.issues.at(0).column >= 3,
             "列号应当落在模式前缀之后（前缀占 3 个字符）");
    QVERIFY(!parsed.issues.at(0).hint.isEmpty());
    QVERIFY(!parsed.ok());
    QVERIFY(!parsed.describeErrors().isEmpty());
}

void TstNameFilter::invalidRegexDoesNotTakeEffect()
{
    // 判别性用例：如果写坏的那一行被「保留但永不匹配」地塞进表达式集合，
    // 「全部满足」会因此拒绝所有条目——测试立刻变红。
    NameFilterParseResult parsed =
        NameFilter::parse(QStringLiteral("re:(\n*.cpp"));
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.filter.expressionCount(), 1);

    parsed.filter.setCombineMode(NameCombineMode::AllOf);
    QVERIFY2(parsed.filter.accepts(QStringLiteral("a.cpp")),
             "写坏的那一行既不参与收窄、也不该把结果集清空");

    // 反过来：写坏的那一行**确实**没有被当成一条「命中一切」的表达式。
    QVERIFY(!parsed.filter.accepts(QStringLiteral("a.txt")));
}

void TstNameFilter::invalidMaskIsReportedWithPosition()
{
    // 掩码的转义白名单是封闭的：`\q` 不是合法转义。
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("a\\qb"));
    QCOMPARE(parsed.filter.expressionCount(), 0);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(kindId(parsed.issues.at(0).kind), QStringLiteral("syntax"));
    QVERIFY(!parsed.issues.at(0).message.isEmpty());
}

void TstNameFilter::badLineDoesNotAffectOtherLines()
{
    const NameFilterParseResult parsed =
        NameFilter::parse(QStringLiteral("*.cpp\n[abc\n*.h"));
    QCOMPARE(parsed.filter.expressionCount(), 2);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(parsed.issues.at(0).line, 1);
    QVERIFY(parsed.filter.accepts(QStringLiteral("a.cpp")));
    QVERIFY(parsed.filter.accepts(QStringLiteral("a.h")));
}

void TstNameFilter::blankAndCommentLinesAreIgnored()
{
    const NameFilterParseResult parsed =
        NameFilter::parse(QStringLiteral("# 说明\n\n   \n*.cpp"));
    QCOMPARE(parsed.filter.expressionCount(), 1);
    QCOMPARE(parsed.issues.size(), 0);
    QCOMPARE(parsed.filter.expressions().at(0).line, 3);
}

void TstNameFilter::prefixWithoutExpressionIsReported()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:"));
    QCOMPARE(parsed.filter.expressionCount(), 0);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(kindId(parsed.issues.at(0).kind), QStringLiteral("syntax"));
    QVERIFY2(parsed.issues.at(0).message.contains(QStringLiteral("没有表达式")),
             "只有前缀的行要明确说是「没有表达式」，而不是「正则写错了」");
}

void TstNameFilter::validationReusesTheSameImplementationAsParsing()
{
    // 「什么叫合法」只有一份实现：界面的实时校验与解析器走同一个函数。
    const NameFilterLineAnalysis analysis = analyzeNameFilterLine(QStringLiteral("re:("), 0);
    QVERIFY2(!analysis.hasExpression, "写坏的行不能被当成一条可用的表达式");
    QVERIFY(analysis.hasSyntaxError());
    QCOMPARE(analysis.issues.size(), 1);

    const NameFilterLineAnalysis good = analyzeNameFilterLine(QStringLiteral("*.cpp"), 0);
    QVERIFY(good.hasExpression);
    QVERIFY(!good.hasSyntaxError());
    QCOMPARE(modeId(good.expression.mode), QStringLiteral("wildcard"));

    // 同一条文本，两条路径给出的问题描述必须逐字一致。
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:("));
    QCOMPARE(parsed.issues.at(0).describe(), analysis.issues.at(0).describe());
}

void TstNameFilter::validateOnAParsedFilterReportsOnlyStaticRisks()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("*.cpp\nre:(a+)+"));
    QCOMPARE(parsed.filter.expressionCount(), 2);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(kindId(parsed.issues.at(0).kind), QStringLiteral("risky"));

    // 解析之后重跑一遍校验：只有静态风险，没有语法错（语法错在解析期就被丢掉了）。
    const NameFilterProblems problems = parsed.filter.validate();
    QCOMPARE(problems.countOfKind(NameFilterIssueKind::Syntax), 0);
    QCOMPARE(problems.countOfKind(NameFilterIssueKind::Risky), 1);
    QCOMPARE(problems.issues.at(0).line, 1);
}

void TstNameFilter::unclosedBracketInWildcardLineIsReported()
{
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("a[b"));
    QCOMPARE(parsed.filter.expressionCount(), 0);
    QCOMPARE(parsed.issues.size(), 1);
    QVERIFY2(parsed.issues.at(0).hint.contains(QStringLiteral("]")),
             "未闭合的字符集要给出「补上 ]」这类可执行的建议");
}

// -----------------------------------------------------------------------------
// D 超时保护的策略（第 2 条）
// -----------------------------------------------------------------------------

void TstNameFilter::defaultBudgetIsTwoHundredMsPerEntry()
{
    NameFilter filter;
    QCOMPARE(filter.matchBudget().perEntryMs, 200);
    QCOMPARE(filter.matchBudget().consecutiveTimeoutLimit, 2);
    QVERIFY(filter.matchBudget().isEnabled());
    QVERIFY(filter.matchBudget().hasCircuitBreaker());
    QCOMPARE(filter.timeoutCount(), 0);
    QCOMPARE(filter.disabledExpressionCount(), 0);
}

void TstNameFilter::budgetWithoutLimitIsDisabled()
{
    NameFilter filter;
    NameMatchBudget budget;
    budget.perEntryMs = 0;
    filter.setMatchBudget(budget);
    QVERIFY(!filter.matchBudget().isEnabled());
}

void TstNameFilter::timeoutMarksTheEntryUndecidedAndKeepsGoing()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    auto runner = scriptedRunner(QVector<bool>{true});
    filter.setMatchRunner(runner);

    const NameFilterDecision timedOut = filter.decide(QStringLiteral("a"));
    QCOMPARE(outcomeId(timedOut.outcomes.at(0)), QStringLiteral("undecided"));
    QVERIFY2(timedOut.accepted, "不确定的结论必须放行（否则会凭空少一批条目）");
    QCOMPARE(timedOut.issues.size(), 1);
    QCOMPARE(kindId(timedOut.issues.at(0).kind), QStringLiteral("timeout"));
    QCOMPARE(runner->calls, 1);

    // 「并继续」：下一个条目照常判定，而且结果是真的判定结果。
    const NameFilterDecision next = filter.decide(QStringLiteral("b"));
    QCOMPARE(outcomeId(next.outcomes.at(0)), QStringLiteral("not-matched"));
    QVERIFY(!next.accepted);
    QCOMPARE(runner->calls, 2);
    QCOMPARE(filter.timeoutCount(), 1);
}

void TstNameFilter::timeoutIsReportedAsAnIssueWithPosition()
{
    NameFilter filter = buildFilter(QStringLiteral("*.cpp\nre:^a"), NameCombineMode::AnyOf);
    filter.setMatchRunner(scriptedRunner(QVector<bool>{true}));

    const NameFilterDecision decision = filter.decide(QStringLiteral("abc"));
    QCOMPARE(decision.issues.size(), 1);
    const NameFilterIssue &issue = decision.issues.at(0);
    QCOMPARE(kindId(issue.kind), QStringLiteral("timeout"));
    QCOMPARE(issue.line, 1);
    QCOMPARE(issue.column, 3);
    QCOMPARE(issue.text, QStringLiteral("^a"));
    QVERIFY2(issue.message.contains(QStringLiteral("abc")),
             "超时问题里要带上被匹配的名字，否则用户不知道是哪个条目");
    QVERIFY(!issue.hint.isEmpty());
    QVERIFY(issue.describe().contains(QStringLiteral("第 2 行")));
}

void TstNameFilter::timeoutDoesNotShrinkTheResultSet()
{
    // 「全部满足」+ 一条超时：若超时被当成「不匹配」，这里会排除掉 `b`。
    NameFilter filter = buildFilter(QStringLiteral("re:a\nre:b"), NameCombineMode::AllOf);
    filter.setMatchRunner(scriptedRunner(QVector<bool>{true}));

    const NameFilterDecision decision = filter.decide(QStringLiteral("b"));
    QCOMPARE(outcomeId(decision.outcomes.at(0)), QStringLiteral("undecided"));
    QCOMPARE(outcomeId(decision.outcomes.at(1)), QStringLiteral("matched"));
    QVERIFY2(decision.accepted, "超时不能缩小结果集");
    QCOMPARE(decision.undecidedIndexes.size(), 1);
    QCOMPARE(decision.matchedIndexes.size(), 1);
    QCOMPARE(decision.matchedIndexes.at(0), 1);
}

void TstNameFilter::circuitBreakerDisablesAfterConsecutiveTimeouts()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.perEntryMs = 200;
    budget.consecutiveTimeoutLimit = 2;
    filter.setMatchBudget(budget);
    auto runner = scriptedRunner(QVector<bool>{true, true, true, true});
    filter.setMatchRunner(runner);

    const NameFilterDecision first = filter.decide(QStringLiteral("a"));
    QCOMPARE(first.issues.size(), 1);
    QVERIFY2(!filter.isExpressionDisabled(0), "只超时一次还不该停用");

    const NameFilterDecision second = filter.decide(QStringLiteral("a"));
    QCOMPARE(second.issues.size(), 2);
    QCOMPARE(kindId(second.issues.at(1).kind), QStringLiteral("disabled"));
    QVERIFY(filter.isExpressionDisabled(0));
    QCOMPARE(filter.disabledExpressionCount(), 1);
    QCOMPARE(filter.timeoutCount(), 2);
}

void TstNameFilter::circuitBreakerResetsOnASuccessfulMatch()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.perEntryMs = 200;
    budget.consecutiveTimeoutLimit = 2;
    filter.setMatchBudget(budget);
    // 超时 → 成功（计数归零）→ 再超时：因此**不**该被停用。
    filter.setMatchRunner(scriptedRunner(QVector<bool>{true, false, true}));

    QCOMPARE(outcomeId(filter.decide(QStringLiteral("a")).outcomes.at(0)),
             QStringLiteral("undecided"));
    QCOMPARE(outcomeId(filter.decide(QStringLiteral("a")).outcomes.at(0)),
             QStringLiteral("matched"));
    const NameFilterDecision third = filter.decide(QStringLiteral("a"));
    QCOMPARE(outcomeId(third.outcomes.at(0)), QStringLiteral("undecided"));
    QVERIFY2(!filter.isExpressionDisabled(0),
             "「连续超时」必须在成功一次之后归零——否则偶发超时会攒成停用");
    QCOMPARE(filter.timeoutCount(), 2);
}

void TstNameFilter::disabledExpressionIsNoLongerEvaluated()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.consecutiveTimeoutLimit = 1;
    filter.setMatchBudget(budget);
    auto runner = scriptedRunner(QVector<bool>{true, true, true, true});
    filter.setMatchRunner(runner);

    filter.decide(QStringLiteral("a"));
    QVERIFY(filter.isExpressionDisabled(0));
    const int callsAfterDisable = runner->calls;

    filter.decide(QStringLiteral("a"));
    filter.decide(QStringLiteral("a"));
    QCOMPARE(runner->calls, callsAfterDisable);
}

void TstNameFilter::disabledExpressionYieldsUndecidedNotNotMatched()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.consecutiveTimeoutLimit = 1;
    filter.setMatchBudget(budget);
    filter.setMatchRunner(scriptedRunner(QVector<bool>{true}));

    filter.decide(QStringLiteral("a"));
    QVERIFY(filter.isExpressionDisabled(0));

    const NameFilterDecision decision = filter.decide(QStringLiteral("a"));
    QCOMPARE(outcomeId(decision.outcomes.at(0)), QStringLiteral("undecided"));
    QVERIFY2(decision.accepted, "被停用的表达式既不是命中也不是不命中——结论要放行并报出");
    QVERIFY(decision.issues.isEmpty());
}

void TstNameFilter::circuitBreakerCanBeTurnedOff()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.perEntryMs = 200;
    budget.consecutiveTimeoutLimit = 0;
    filter.setMatchBudget(budget);
    QVERIFY(!filter.matchBudget().hasCircuitBreaker());
    filter.setMatchRunner(scriptedRunner(QVector<bool>{true, true, true, true, true}));

    for (int i = 0; i < 5; ++i) {
        const NameFilterDecision decision = filter.decide(QStringLiteral("a"));
        QCOMPARE(decision.issues.size(), 1);
        QCOMPARE(kindId(decision.issues.at(0).kind), QStringLiteral("timeout"));
    }
    QVERIFY(!filter.isExpressionDisabled(0));
    QCOMPARE(filter.timeoutCount(), 5);
}

void TstNameFilter::zeroBudgetBypassesTheRunnerEntirely()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.perEntryMs = 0;
    filter.setMatchBudget(budget);
    auto runner = scriptedRunner(QVector<bool>{true, true});
    filter.setMatchRunner(runner);

    const NameFilterDecision decision = filter.decide(QStringLiteral("a"));
    QCOMPARE(runner->calls, 0);
    QCOMPARE(outcomeId(decision.outcomes.at(0)), QStringLiteral("matched"));
    QVERIFY(decision.issues.isEmpty());
}

void TstNameFilter::nonRegexModesNeverUseTheRunner()
{
    NameFilter filter = buildFilter(QStringLiteral("= a.cpp\n*.cpp"), NameCombineMode::AnyOf);
    auto runner = scriptedRunner(QVector<bool>{true, true, true});
    filter.setMatchRunner(runner);

    QVERIFY(filter.accepts(QStringLiteral("a.cpp")));
    QCOMPARE(runner->calls, 0);
    QVERIFY(filter.accepts(QStringLiteral("b.cpp")));
    QCOMPARE(runner->calls, 0);
    QVERIFY(!filter.accepts(QStringLiteral("a.txt")));
    QCOMPARE(runner->calls, 0);
}

void TstNameFilter::timeoutCounterIsResettable()
{
    NameFilter filter = buildFilter(QStringLiteral("re:a"), NameCombineMode::AnyOf);
    NameMatchBudget budget;
    budget.consecutiveTimeoutLimit = 1;
    filter.setMatchBudget(budget);
    filter.setMatchRunner(scriptedRunner(QVector<bool>{true}));

    filter.decide(QStringLiteral("a"));
    QCOMPARE(filter.timeoutCount(), 1);
    QVERIFY(filter.isExpressionDisabled(0));

    filter.resetTimeoutState();
    QCOMPARE(filter.timeoutCount(), 0);
    QVERIFY(!filter.isExpressionDisabled(0));
    QCOMPARE(filter.disabledExpressionCount(), 0);
}

// -----------------------------------------------------------------------------
// E 超时保护的机制（第 2 条，真线程）
// -----------------------------------------------------------------------------

void TstNameFilter::threadRunnerCompletesAFastTask()
{
    ThreadNameMatchRunner runner;
    const NameMatchRunner::Outcome outcome = runner.run([]() { return true; }, 2000);
    QCOMPARE(runnerStatusId(outcome.status), QStringLiteral("completed"));
    QVERIFY(outcome.matched);

    const NameMatchRunner::Outcome miss = runner.run([]() { return false; }, 2000);
    QCOMPARE(runnerStatusId(miss.status), QStringLiteral("completed"));
    QVERIFY(!miss.matched);
    QCOMPARE(runner.abandonedCount(), 0);
}

void TstNameFilter::threadRunnerTimesOutOnASlowTask()
{
    ThreadNameMatchRunner runner;
    QElapsedTimer timer;
    timer.start();
    const NameMatchRunner::Outcome outcome =
        runner.run([]() { QThread::msleep(400); return true; }, 40);
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(runnerStatusId(outcome.status), QStringLiteral("timed-out"));
    QVERIFY2(elapsed < 300, qPrintable(QStringLiteral("等了 %1ms，截止时间没有生效")
                                           .arg(elapsed)));
    QVERIFY2(!outcome.matched, "超时的结果不能报成命中");
    QCOMPARE(runner.abandonedCount(), 1);
}

void TstNameFilter::threadRunnerRunsInlineWithoutABudget()
{
    ThreadNameMatchRunner runner;
    QThread *calling = QThread::currentThread();

    bool ranOnCaller = false;
    const NameMatchRunner::Outcome unguarded = runner.run(
        [&ranOnCaller, calling]() {
            ranOnCaller = (QThread::currentThread() == calling);
            return true;
        },
        0);
    QCOMPARE(runnerStatusId(unguarded.status), QStringLiteral("completed"));
    QVERIFY2(ranOnCaller, "不设预算时任务应当在当前线程上直接跑（省掉一次同步）");

    // 对照：给了预算就必须挪到工作线程上，否则「超时保护」无从谈起。
    bool ranOnWorker = false;
    const NameMatchRunner::Outcome guarded = runner.run(
        [&ranOnWorker, calling]() {
            ranOnWorker = (QThread::currentThread() != calling);
            return true;
        },
        2000);
    QCOMPARE(runnerStatusId(guarded.status), QStringLiteral("completed"));
    QVERIFY2(ranOnWorker, "给了预算时任务应当在工作线程上跑");
}

void TstNameFilter::threadRunnerRecoversAfterATimeout()
{
    ThreadNameMatchRunner runner;
    QCOMPARE(runnerStatusId(runner.run([]() { QThread::msleep(400); return true; }, 40).status),
             QStringLiteral("timed-out"));

    // 关键：超时之后**不能被那个跑不完的任务拖住**。丢掉旧工作线程是唯一的办法
    // （Qt 无法中断匹配）；如果没丢掉，这一次会排在它后面，耗时 ≈ 400ms。
    QElapsedTimer timer;
    timer.start();
    const NameMatchRunner::Outcome outcome = runner.run([]() { return true; }, 2000);
    const qint64 elapsed = timer.elapsed();
    QCOMPARE(runnerStatusId(outcome.status), QStringLiteral("completed"));
    QVERIFY2(elapsed < 250,
             qPrintable(QStringLiteral("超时之后仍然被拖住：等了 %1ms").arg(elapsed)));
}

void TstNameFilter::threadRunnerCountsAbandonedTasks()
{
    ThreadNameMatchRunner runner;
    QCOMPARE(runner.abandonedCount(), 0);
    runner.run([]() { return true; }, 2000);
    QCOMPARE(runner.abandonedCount(), 0);
    runner.run([]() { QThread::msleep(300); return true; }, 30);
    QCOMPARE(runner.abandonedCount(), 1);
    runner.run([]() { return true; }, 2000);
    QCOMPARE(runner.abandonedCount(), 1);
}

void TstNameFilter::defaultRunnerIsShared()
{
    const std::shared_ptr<NameMatchRunner> first = defaultNameMatchRunner();
    const std::shared_ptr<NameMatchRunner> second = defaultNameMatchRunner();
    QVERIFY(first != nullptr);
    QVERIFY2(first == second,
             "默认运行器必须是进程内共享的，否则每个过滤器实例会各起一条线程");
    QVERIFY(std::dynamic_pointer_cast<ThreadNameMatchRunner>(first) != nullptr);
}

void TstNameFilter::timedOutTaskKeepsCopiesNotReferences()
{
    // 被放弃的那次匹配会在工作线程上**继续跑**，而它的持有者（本次 decide() 的
    // 调用栈、乃至整个 NameFilter）那时可能已经销毁了。用引用捕获就是悬垂引用，
    // 现象是「过滤器偶发崩在毫不相干的地方」。
    //
    // 这件事无法用行为断言稳定地证明（崩不崩取决于内存有没有被复用），
    // 因此这里钉住**写法本身**：任务按值捕获。
    const QString source = readSourceFile(QStringLiteral("/Services/Filter/namefilter.cpp"));
    QVERIFY(!source.isEmpty());
    QVERIFY2(source.contains(QStringLiteral("const std::function<bool()> task = [regex, subject]()")),
             "任务必须按值捕获正则与名字——改成引用捕获会让被放弃的匹配读到已释放的对象");
    QVERIFY2(!source.contains(QStringLiteral("[&regex")),
             "任务里出现了对正则的引用捕获");
    QVERIFY2(!source.contains(QStringLiteral("[&re,")),
             "任务里出现了对正则的引用捕获");
}

void TstNameFilter::slowRegexTimeoutDoesNotCrashTheProcess()
{
    // 真实路径的冒烟用例：灾难性回溯的正则 + 很紧的预算 + 极小断路器阈值。
    // 无论「谁先结束」（我们放弃 / PCRE2 自己撞上 match limit），结论都只有两种，
    // 而**被放弃的那次匹配在过滤器销毁之后仍然跑完**这一点由进程活着来证明。
    {
        NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:^(a+)+$"));
        QCOMPARE(parsed.filter.expressionCount(), 1);
        NameMatchBudget budget;
        budget.perEntryMs = 5;
        budget.consecutiveTimeoutLimit = 1;
        parsed.filter.setMatchBudget(budget);

        const QString subject = QString(40, QLatin1Char('a')) + QLatin1Char('b');
        const NameFilterDecision decision = parsed.filter.decide(subject);

        if (!decision.issues.isEmpty()) {
            QCOMPARE(kindId(decision.issues.at(0).kind), QStringLiteral("timeout"));
            QVERIFY2(decision.accepted, "超时必须放行");
            QCOMPARE(decision.issues.size(), 2); // 超时 + 被停用（阈值是 1）
            QVERIFY(parsed.filter.isExpressionDisabled(0));
        } else {
            QVERIFY2(!decision.accepted, "没有超时的话，这条正则不命中这个名字");
        }
    }

    // 给被放弃的工作线程留出跑完的时间。它引用的是**副本**，因此这段等待
    // 不依赖上面那个 NameFilter 还活着。
    QThread::msleep(600);
    QVERIFY(true);
}

// -----------------------------------------------------------------------------
// F 静态回溯预检
// -----------------------------------------------------------------------------

void TstNameFilter::nestedQuantifierIsFlagged()
{
    const QVector<RegexRisk> risks = analyzeRegexPatternRisk(QStringLiteral("(a+)+"));
    QCOMPARE(risks.size(), 1);
    QCOMPARE(risks.at(0).snippet, QStringLiteral("(a+)+"));
    QCOMPARE(risks.at(0).column, 0);
    QCOMPARE(risks.at(0).length, 5);

    QCOMPARE(analyzeRegexPatternRisk(QStringLiteral("(.*)*")).size(), 1);
    QCOMPARE(analyzeRegexPatternRisk(QStringLiteral("(a?)+")).size(), 1);
    QCOMPARE(analyzeRegexPatternRisk(QStringLiteral("(x+){2,}")).size(), 1);
}

void TstNameFilter::quantifierOnNestedGroupIsFlagged()
{
    QCOMPARE(analyzeRegexPatternRisk(QStringLiteral("((a+))+")).size(), 1);
    QCOMPARE(analyzeRegexPatternRisk(QStringLiteral("((a){2,})+")).size(), 1);
}

void TstNameFilter::nonCapturingGroupQuantifierIsNotFlagged()
{
    // `(?:a)+` 里那个 `?` 是分组修饰而不是量词。把它判成量词是纯粹的误报，
    // 而误报的代价是用户从此不看这个提示。
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(?:a)+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(?i)(a)+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(?<name>a)+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(?=a)(b)+")).isEmpty());
}

void TstNameFilter::quantifiedAlternationIsNotFlagged()
{
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(a|b)*")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("\\.(cpp|h)$")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("^test_[0-9]{2}\\.log$")).isEmpty());
    // 固定次数的量词不算可变：`(a{2})+` 每轮恰好吃两个字符，回溯是线性的。
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(a{2})+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(a+){2}")).isEmpty());
}

void TstNameFilter::escapedAndCharacterClassContentIsIgnored()
{
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("[+*](a)+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("\\+(a+)\\+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("\\((a+)\\)+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("[a-z]+(b+)+")).size() == 1);
}

void TstNameFilter::riskReportsColumnAndSnippet()
{
    const QVector<RegexRisk> risks = analyzeRegexPatternRisk(QStringLiteral("xy(a+)+z"));
    QCOMPARE(risks.size(), 1);
    QCOMPARE(risks.at(0).column, 2);
    QCOMPARE(risks.at(0).snippet, QStringLiteral("(a+)+"));
    QVERIFY(!risks.at(0).reason.isEmpty());
    QVERIFY(risks.at(0).describe().contains(QStringLiteral("第 3 列")));
}

void TstNameFilter::riskyExpressionStillTakesEffect()
{
    // 静态预检只提示、不阻断：用户可能真的知道自己在写什么。
    const NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("re:(a+)+"));
    QCOMPARE(parsed.filter.expressionCount(), 1);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(kindId(parsed.issues.at(0).kind), QStringLiteral("risky"));
    QVERIFY2(parsed.filter.accepts(QStringLiteral("aaa")),
             "可疑的表达式仍然生效（只是界面要给提示）");
    QVERIFY(!parsed.filter.accepts(QStringLiteral("aab")));
}

void TstNameFilter::patternWithLiteralBracesIsNotAQuantifier()
{
    // `{x}` 不是量词，PCRE 把它当字面量。识别器不能把它算成量词，
    // 否则 `(a{x})+` 这种写法会被误报。
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("(a{x})+")).isEmpty());
    QVERIFY(analyzeRegexPatternRisk(QStringLiteral("a{x}")).isEmpty());
}

// -----------------------------------------------------------------------------
// G 具名预设与导出（第 5 条）
// -----------------------------------------------------------------------------

void TstNameFilter::presetRoundTripsAllFields()
{
    QVector<NamedNameFilter> presets;
    NamedNameFilter first;
    first.name = QStringLiteral("源码目录");
    first.note = QStringLiteral("只看源码，忽略产物");
    first.combine = NameCombineMode::NoneOf;
    first.declaration = QStringLiteral("*.cpp\n*.h");
    presets.append(first);

    NamedNameFilter second;
    second.name = QStringLiteral("日志");
    second.combine = NameCombineMode::AllOf;
    second.caseOverridden = true;
    second.caseSensitivity = Qt::CaseInsensitive;
    second.declaration = QStringLiteral("re:^.*\\.log$");
    presets.append(second);

    const QString text = serializeNamedNameFilters(presets);
    const NamedNameFilterParseResult parsed = parseNamedNameFilters(text);
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeErrors()));
    QCOMPARE(parsed.presets.size(), 2);

    QCOMPARE(parsed.presets.at(0).name, first.name);
    QCOMPARE(parsed.presets.at(0).note, first.note);
    QCOMPARE(combineId(parsed.presets.at(0).combine), combineId(first.combine));
    QCOMPARE(parsed.presets.at(0).declaration, first.declaration);
    QVERIFY(!parsed.presets.at(0).caseOverridden);

    QCOMPARE(parsed.presets.at(1).name, second.name);
    QCOMPARE(combineId(parsed.presets.at(1).combine), combineId(second.combine));
    QVERIFY(parsed.presets.at(1).caseOverridden);
    QCOMPARE(parsed.presets.at(1).caseSensitivity, Qt::CaseInsensitive);
    QCOMPARE(parsed.presets.at(1).declaration, second.declaration);
    QVERIFY(parsed.presets.at(1).note.isEmpty());
}

void TstNameFilter::presetFormatHeaderIsStable()
{
    // 第一行是版本标识：将来格式变化时靠它区分，因此不能被悄悄改掉。
    QCOMPARE(nameFilterPresetFormatHeader(), QStringLiteral("# LqCompare 名称过滤预设 v1"));
    const QString text = serializeNamedNameFilters(QVector<NamedNameFilter>());
    QVERIFY(text.startsWith(nameFilterPresetFormatHeader()));
}

void TstNameFilter::presetWithoutNameIsReported()
{
    const NamedNameFilterParseResult parsed =
        parseNamedNameFilters(QStringLiteral("# 头\n[预设]\n*.cpp\n"));
    QVERIFY2(parsed.presets.isEmpty(), "没有名字的预设不能被收下");
    QCOMPARE(parsed.issues.size(), 1);
    QVERIFY(parsed.issues.at(0).message.contains(QStringLiteral("没有名字")));
}

void TstNameFilter::unknownCombineKeyIsReported()
{
    const NamedNameFilterParseResult parsed =
        parseNamedNameFilters(QStringLiteral("[预设] x\n组合: nope\n*.cpp\n"));
    QCOMPARE(parsed.presets.size(), 1);
    QCOMPARE(parsed.issues.size(), 1);
    QCOMPARE(kindId(parsed.issues.at(0).kind), QStringLiteral("syntax"));
    QVERIFY2(parsed.issues.at(0).hint.contains(QStringLiteral("any-of")),
             "提示里要列出可用取值，而且要能搜到稳定键");
}

void TstNameFilter::combineKeyAcceptsChineseLabel()
{
    const NamedNameFilterParseResult parsed =
        parseNamedNameFilters(QStringLiteral("[预设] x\n组合: 不包含任何\n*.tmp\n"));
    QCOMPARE(parsed.presets.size(), 1);
    QCOMPARE(combineId(parsed.presets.at(0).combine), QStringLiteral("none-of"));
}

void TstNameFilter::caseOverrideRoundTrips()
{
    const NamedNameFilterParseResult insensitive =
        parseNamedNameFilters(QStringLiteral("[预设] x\n大小写: insensitive\n*.cpp\n"));
    QVERIFY(insensitive.presets.at(0).caseOverridden);
    QCOMPARE(insensitive.presets.at(0).caseSensitivity, Qt::CaseInsensitive);

    const NamedNameFilterParseResult platformDefault =
        parseNamedNameFilters(QStringLiteral("[预设] x\n大小写: platform\n*.cpp\n"));
    QVERIFY(!platformDefault.presets.at(0).caseOverridden);

    const NamedNameFilterParseResult bogus =
        parseNamedNameFilters(QStringLiteral("[预设] x\n大小写: maybe\n*.cpp\n"));
    QCOMPARE(bogus.issues.size(), 1);
    QVERIFY(!bogus.presets.at(0).caseOverridden);
}

void TstNameFilter::crlfPresetFileIsReadCorrectly()
{
    // 预设是给人共享的文件，在 Windows 上编辑过就是 CRLF。只按 `\n` 切的话，
    // 每一行末尾会多一个 `\r`，`*.tmp` 会静静变成 `*.tmp\r`。
    const QString text = QStringLiteral("# 头\r\n[预设] 源码\r\n组合: all-of\r\n*.cpp\r\n= README.md\r\n");
    const NamedNameFilterParseResult parsed = parseNamedNameFilters(text);
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeErrors()));
    QCOMPARE(parsed.presets.size(), 1);
    QCOMPARE(parsed.presets.at(0).name, QStringLiteral("源码"));
    QCOMPARE(combineId(parsed.presets.at(0).combine), QStringLiteral("all-of"));
    QCOMPARE(parsed.presets.at(0).declaration, QStringLiteral("*.cpp\n= README.md"));
    QVERIFY2(!parsed.presets.at(0).declaration.contains(QLatin1Char('\r')),
             "声明体里不能留下 \\r");
}

void TstNameFilter::presetBodyReusesTheDeclarationSyntax()
{
    const NamedNameFilterParseResult parsed = parseNamedNameFilters(
        QStringLiteral("[预设] x\n*.cpp\nre:^test_\n= README.md\n"));
    QCOMPARE(parsed.presets.size(), 1);

    const NameFilterParseResult body = NameFilter::parse(parsed.presets.at(0).declaration);
    QCOMPARE(body.filter.expressionCount(), 3);
    QCOMPARE(modeId(body.filter.expressions().at(1).mode), QStringLiteral("regex"));
    QCOMPARE(modeId(body.filter.expressions().at(2).mode), QStringLiteral("exact"));
    QVERIFY(body.filter.accepts(QStringLiteral("test_a.cpp")));
}

void TstNameFilter::fileWithoutRecordsIsReported()
{
    const NamedNameFilterParseResult parsed =
        parseNamedNameFilters(QStringLiteral("这是一份说明\n*.cpp\n"));
    QVERIFY(parsed.presets.isEmpty());
    QCOMPARE(parsed.issues.size(), 1);
    QVERIFY2(parsed.issues.at(0).hint.contains(QStringLiteral("备注:")),
             "提示里要列出可用的元信息键（从元信息表生成）");
}

void TstNameFilter::serializedPresetIsParseableByTheFilterItself()
{
    // 导出与导入必须自洽：导出的声明体拿回过滤器里读，必须一条不落地认出来。
    NamedNameFilter preset;
    preset.name = QStringLiteral("回归");
    preset.combine = NameCombineMode::AllOf;
    preset.declaration = QStringLiteral("*.cpp\nre:^test_\n= README.md");

    const NamedNameFilterParseResult parsed =
        parseNamedNameFilters(serializeNamedNameFilters(QVector<NamedNameFilter>{preset}));
    QCOMPARE(parsed.presets.size(), 1);

    const NameFilterParseResult body = NameFilter::parse(parsed.presets.at(0).declaration);
    QVERIFY2(body.ok(), qPrintable(body.describeErrors()));
    QCOMPARE(body.filter.expressionCount(), 3);
    QCOMPARE(parsed.presets.at(0).declaration, preset.declaration);
}

// -----------------------------------------------------------------------------
// H 描述与边界
// -----------------------------------------------------------------------------

void TstNameFilter::emptyFilterKeepsEverything()
{
    // 启动自检盯着这一条：没有配置过滤时，用户什么都还没设就看不到文件了。
    NameFilter filter;
    QVERIFY(filter.isEmpty());
    QVERIFY(filter.accepts(QStringLiteral("anything.txt")));
    QVERIFY(filter.accepts(QStringLiteral("nothing")));

    const NameFilterParseResult parsed = NameFilter::parse(QString());
    QVERIFY(parsed.filter.isEmpty());
    QVERIFY(parsed.filter.accepts(QStringLiteral("anything.txt")));

    // 全是注释与空行也算空。
    const NameFilterParseResult comments = NameFilter::parse(QStringLiteral("# 只写了注释\n\n"));
    QVERIFY(comments.filter.isEmpty());
    QVERIFY(comments.filter.accepts(QStringLiteral("anything.txt")));
}

void TstNameFilter::emptyNameIsRejectedEvenWithNoneOf()
{
    // 「不包含任何」在空表达式集合上会顺理成章地算出「保留」，而这个名字是
    // 残缺输入。非空过滤器下必须挡在外面——三个组合语义全都一样。
    for (NameCombineMode combine : allNameCombineModes()) {
        const NameFilter filter = buildFilter(QStringLiteral("*.cpp"), combine);
        QVERIFY2(!filter.accepts(QString()),
                 qPrintable(QStringLiteral("%1 下空名字也必须被挡在外面")
                                .arg(nameCombineModeLabel(combine))));
        const NameFilterDecision decision = filter.decide(QString());
        QCOMPARE(decision.outcomes.size(), 1);
        QCOMPARE(outcomeId(decision.outcomes.at(0)), QStringLiteral("not-matched"));
    }
}

void TstNameFilter::declarationKeyIsStable()
{
    // 这个键会进会话文件与预设，一经发布不得改动。
    QCOMPARE(nameFilterDeclarationKey(), QStringLiteral("name-filter"));
}

void TstNameFilter::describeMentionsCombineCountAndCase()
{
    const NameFilter filter = buildFilter(QStringLiteral("*.cpp\n*.h"), NameCombineMode::NoneOf);
    const QString text = filter.describe();
    QVERIFY(text.contains(QStringLiteral("不包含任何")));
    QVERIFY(text.contains(QStringLiteral("2 条表达式")));
    QVERIFY(text.contains(QStringLiteral("敏感")));
    QVERIFY(text.contains(QStringLiteral("posix")));
}

void TstNameFilter::caseSensitivityOverrideIsReported()
{
    NameFilterParseResult parsed = NameFilter::parse(QStringLiteral("*.cpp"));
    QVERIFY2(!parsed.filter.isCaseSensitivityOverridden(), "刚解析出来时应当跟随平台默认");
    parsed.filter.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(parsed.filter.isCaseSensitivityOverridden());
    parsed.filter.clearCaseSensitivityOverride();
    QVERIFY(!parsed.filter.isCaseSensitivityOverridden());
    QCOMPARE(parsed.filter.caseSensitivity(), Qt::CaseSensitive);
}

void TstNameFilter::caseInsensitiveWildcardMatchesWindowsStyleNames()
{
    const NameFilterParseResult windows =
        NameFilter::parse(QStringLiteral("*.CPP"), MaskPlatform::Windows);
    QVERIFY(windows.filter.accepts(QStringLiteral("a.cpp")));

    const NameFilterParseResult posix =
        NameFilter::parse(QStringLiteral("*.CPP"), MaskPlatform::Posix);
    QVERIFY2(!posix.filter.accepts(QStringLiteral("a.cpp")),
             "posix 默认大小写敏感——两种平台的默认值都要能在本机被真实执行");
    QVERIFY(posix.filter.accepts(QStringLiteral("a.CPP")));
}

void TstNameFilter::parseReportsTheModeOfEachExpression()
{
    const NameFilterParseResult parsed =
        NameFilter::parse(QStringLiteral("= a\nb\nre:c"));
    QCOMPARE(parsed.filter.expressionCount(), 3);
    QCOMPARE(modeId(parsed.filter.expressions().at(0).mode), QStringLiteral("exact"));
    QCOMPARE(modeId(parsed.filter.expressions().at(1).mode), QStringLiteral("wildcard"));
    QCOMPARE(modeId(parsed.filter.expressions().at(2).mode), QStringLiteral("regex"));
    QCOMPARE(parsed.filter.expressions().at(0).text, QStringLiteral("a"));
    QCOMPARE(parsed.filter.expressions().at(1).text, QStringLiteral("b"));
    QCOMPARE(parsed.filter.expressions().at(2).text, QStringLiteral("c"));
    QCOMPARE(parsed.filter.expressions().at(0).column, 2);
    QCOMPARE(parsed.filter.expressions().at(1).column, 0);
    QCOMPARE(parsed.filter.expressions().at(2).column, 3);
    QCOMPARE(parsed.filter.expressions().at(2).line, 2);
    QVERIFY(parsed.filter.expressions().at(2).describe().contains(QStringLiteral("第 3 行")));
}

// -----------------------------------------------------------------------------
// I 源码级护栏
// -----------------------------------------------------------------------------

void TstNameFilter::nameFilterDoesNotReadTheFileSystem()
{
    // 名称过滤是**纯名字判定**：它不该碰文件系统。一旦有人为了「顺手查一下」
    // 把 QFile 引进来，判定就会出现在扫描的热路径上，而现象只是「打开大目录变慢」。
    const QString header = readSourceFile(QStringLiteral("/Services/Filter/namefilter.h"));
    const QString source = readSourceFile(QStringLiteral("/Services/Filter/namefilter.cpp"));
    QVERIFY(!header.isEmpty());
    QVERIFY(!source.isEmpty());

    const QStringList found = scanForFileSystemAccess(header + source);
    QVERIFY2(found.isEmpty(), qPrintable(QStringLiteral("名称过滤里出现了文件系统 API：%1")
                                             .arg(found.join(QStringLiteral("、")))));
}

void TstNameFilter::contentAccessScanDetectsAPlantedRead()
{
    // 反向验证：护栏本身必须会报错。
    const QString planted = QStringLiteral("QFile file(path);\nQByteArray data = file.readAll();");
    const QStringList found = scanForFileSystemAccess(planted);
    QVERIFY2(!found.isEmpty(), "护栏认不出读文件的写法，等于没有护栏");
    QVERIFY(found.contains(QStringLiteral("QFile")));
    QVERIFY(found.contains(QStringLiteral("readAll")));

    // 干净的样本不能被误判（否则这条护栏会因为噪声被关掉）。
    QVERIFY(scanForFileSystemAccess(QStringLiteral("QString name = subject;")).isEmpty());
}

// -----------------------------------------------------------------------------
// J 表自检（护栏本身要能被反向验证）
// -----------------------------------------------------------------------------

void TstNameFilter::tableSelfCheckIsClean()
{
    const QVector<QString> problems =
        validateNameFilterTables(nameMatchModePrefixTable(), nameCombineModeTable());
    QVERIFY2(problems.isEmpty(), qPrintable(joinProblems(problems)));
}

void TstNameFilter::tableSelfCheckCatchesADuplicatePrefix()
{
    // 反向验证：拿一份故意写坏的表跑同一个判定。两条模式共用前缀时，
    // 后一个模式在解析里永远轮不到——而界面上它照样能被选到。
    QVector<NameMatchModePrefix> broken = nameMatchModePrefixTable();
    broken[0].prefix = broken[2].prefix; // 精确名与正则共用 `re:`
    const QVector<QString> problems = validateNameFilterTables(broken, nameCombineModeTable());
    QVERIFY2(!problems.isEmpty(), "自检认不出重复的前缀，等于没有护栏");
    bool mentionsPrefix = false;
    for (const QString &problem : problems) {
        if (problem.contains(QStringLiteral("re:"))) {
            mentionsPrefix = true;
        }
    }
    QVERIFY2(mentionsPrefix, "问题描述里要指出是哪个前缀（否则用户不知道该改哪一条）");
}

void TstNameFilter::tableSelfCheckCatchesAMissingMode()
{
    QVector<NameMatchModePrefix> broken = nameMatchModePrefixTable();
    broken.removeLast();
    const QVector<QString> problems = validateNameFilterTables(broken, nameCombineModeTable());
    QVERIFY(!problems.isEmpty());
    QVERIFY2(joinProblems(problems).contains(QStringLiteral("不在表里")),
             "漏掉一个模式要说清是哪一个模式没登记");
}

void TstNameFilter::tableSelfCheckCatchesAnEmptyMeaning()
{
    QVector<NameMatchModePrefix> broken = nameMatchModePrefixTable();
    broken[1].meaning = QString();
    const QVector<QString> problems = validateNameFilterTables(broken, nameCombineModeTable());
    QVERIFY2(!problems.isEmpty(), "没有说明的模式必须被报出来（界面要靠它解释模式）");
}

void TstNameFilter::tableSelfCheckCatchesAMissingCombineMode()
{
    QVector<NameCombineModeRow> broken = nameCombineModeTable();
    broken.removeLast();
    const QVector<QString> problems =
        validateNameFilterTables(nameMatchModePrefixTable(), broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinProblems(problems).contains(QStringLiteral("不在表里")));
}

void TstNameFilter::tableSelfCheckCatchesASharedExplanation()
{
    // 两种语义共用一句解释时，界面照常显示，用户看到的却是另一条语义的说明——
    // 这类错没有任何运行期现象，只能靠自检。它也是本组存在的理由：
    // 自检必须吃**传进来的表**，否则这条永远不可能被验证（第一版就是内部取表，
    // 变异测试当场证明了它是一处「永远不会红的护栏」）。
    QVector<NameCombineModeRow> broken = nameCombineModeTable();
    broken[1].explanation = broken[0].explanation;
    const QVector<QString> problems =
        validateNameFilterTables(nameMatchModePrefixTable(), broken);
    QVERIFY2(!problems.isEmpty(), "自检认不出共用的解释，等于没有护栏");
    QVERIFY(joinProblems(problems).contains(QStringLiteral("共用")));
}

void TstNameFilter::combineModeTableMatchesTheEnumeration()
{
    const QVector<NameCombineModeRow> table = nameCombineModeTable();
    const QVector<NameCombineMode> modes = allNameCombineModes();
    QCOMPARE(table.size(), modes.size());
    for (int index = 0; index < modes.size(); ++index) {
        QCOMPARE(combineId(table.at(index).mode), combineId(modes.at(index)));
        // 表是唯一的事实来源：取标签/解释的函数必须与表逐字一致。
        QCOMPARE(table.at(index).label, nameCombineModeLabel(modes.at(index)));
        QCOMPARE(table.at(index).explanation, nameCombineModeExplanation(modes.at(index)));
    }
}

QTEST_MAIN(TstNameFilter)
