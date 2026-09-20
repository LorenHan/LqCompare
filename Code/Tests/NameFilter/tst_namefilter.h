#ifndef LQCOMPARE_TST_NAMEFILTER_H
#define LQCOMPARE_TST_NAMEFILTER_H

#include <QObject>
#include <QtTest>

///
/// \brief 名称过滤器的测试（PRD: FILT-002）。
///
/// 分组对齐规格的五条完成标准：
///   A 三种匹配模式   —— 第 1 条（精确名 / 通配符 / 正则，可切换）
///   B 组合语义       —— 第 3 条（包含任一 / 不包含任何 / 全部满足，且有显示文案）
///   C 实时校验       —— 第 4 条（非法正则就地报错且不生效）
///   D 超时保护的策略 —— 第 2 条（超时记错、继续、断路器、停用）
///   E 超时保护的机制 —— 第 2 条的机制本身（真线程、真截止时间），只用少量用例
///   F 静态回溯预检   —— 第 2 条边界条款（「必须做灾难性回溯防护」）的第一层
///   G 具名预设与导出 —— 第 5 条里**能独立验证**的那一半（命名 + 往返）
///   H 描述与边界     —— 状态文案、空过滤器、残缺条目
///   I 源码级护栏     —— 名称过滤不得读文件系统（它是名字判定，不是内容判定）
///
/// 本套件**刻意不链接 QtGui**（见 NameFilterTests.pro）：名称过滤全是纯逻辑，
/// 一旦有人把界面依赖（图标、字体、控件、对话框）塞进 namefilter.cpp，
/// 本工程会立刻构建失败，而不是等某台没有图形环境的机器上才发现。
///
/// 它**必须**链接 Services/Session：本工程要 include 整份 filter.pri，
/// 而其中的 filterstack.cpp 要用 `SessionSettings` 表达三层落点。
///
class TstNameFilter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 三种匹配模式（第 1 条） ----------
    void exactMatchesOnlyWholeName();
    void exactHonorsCaseSensitivity();
    void wildcardUsesMaskSyntaxAndMatchesWholeName();
    void regexMatchesWholeNameNotSubstring();
    void regexKeepsUserAnchorsWorking();
    void regexSubstringNeedsExplicitDotStar();
    void regexHonorsCaseSensitivity();
    void wildcardMetacharactersAreNotRegex();
    void modePrefixTableIsTheSingleSourceOfTruth();
    void modeKeysAndLabelsAreDistinctAndStable();
    void modeKeysRoundTripThroughFromKey();
    void declarationTextAlwaysWritesTheModeWhenItIsNotWildcard();
    void defaultMatchModeDoesNotReinterpretExistingLines();
    void prefixShadowingBoundaryHasADocumentedEscape();
    void modeSemanticsNoteSaysWholeNameForAllThree();

    // ---------- B 组合语义（第 3 条） ----------
    void anyOfKeepsWhenAnyExpressionMatches();
    void anyOfRejectsWhenNothingMatches();
    void noneOfRejectsWhenAnyExpressionMatches();
    void noneOfKeepsWhenNothingMatches();
    void allOfRequiresEveryExpression();
    void allOfReportsTheFirstMissAsDecisive();
    void noneOfReportsTheHitAsDecisive();
    void combineSummaryNamesTheCurrentSemantics();
    void combineSummaryForEmptyFilterSaysNothingIsFiltered();
    void combineTableIsCompleteAndExplanationsDiffer();
    void decisionDescribeReportsOutcomes();
    void everyOutcomeHasAnIdentifierAndLabel();

    // ---------- C 实时校验（第 4 条） ----------
    void invalidRegexIsReportedWithPosition();
    void invalidRegexDoesNotTakeEffect();
    void invalidMaskIsReportedWithPosition();
    void badLineDoesNotAffectOtherLines();
    void blankAndCommentLinesAreIgnored();
    void prefixWithoutExpressionIsReported();
    void validationReusesTheSameImplementationAsParsing();
    void validateOnAParsedFilterReportsOnlyStaticRisks();
    void unclosedBracketInWildcardLineIsReported();

    // ---------- D 超时保护的策略（第 2 条） ----------
    void defaultBudgetIsTwoHundredMsPerEntry();
    void budgetWithoutLimitIsDisabled();
    void timeoutMarksTheEntryUndecidedAndKeepsGoing();
    void timeoutIsReportedAsAnIssueWithPosition();
    void timeoutDoesNotShrinkTheResultSet();
    void circuitBreakerDisablesAfterConsecutiveTimeouts();
    void circuitBreakerResetsOnASuccessfulMatch();
    void disabledExpressionIsNoLongerEvaluated();
    void disabledExpressionYieldsUndecidedNotNotMatched();
    void circuitBreakerCanBeTurnedOff();
    void zeroBudgetBypassesTheRunnerEntirely();
    void nonRegexModesNeverUseTheRunner();
    void timeoutCounterIsResettable();

    // ---------- E 超时保护的机制（第 2 条，真线程） ----------
    void threadRunnerCompletesAFastTask();
    void threadRunnerTimesOutOnASlowTask();
    void threadRunnerRunsInlineWithoutABudget();
    void threadRunnerRecoversAfterATimeout();
    void threadRunnerCountsAbandonedTasks();
    void defaultRunnerIsShared();
    void timedOutTaskKeepsCopiesNotReferences();
    void slowRegexTimeoutDoesNotCrashTheProcess();

    // ---------- F 静态回溯预检 ----------
    void nestedQuantifierIsFlagged();
    void quantifierOnNestedGroupIsFlagged();
    void nonCapturingGroupQuantifierIsNotFlagged();
    void quantifiedAlternationIsNotFlagged();
    void escapedAndCharacterClassContentIsIgnored();
    void riskReportsColumnAndSnippet();
    void riskyExpressionStillTakesEffect();
    void patternWithLiteralBracesIsNotAQuantifier();

    // ---------- G 具名预设与导出（第 5 条） ----------
    void presetRoundTripsAllFields();
    void presetFormatHeaderIsStable();
    void presetWithoutNameIsReported();
    void unknownCombineKeyIsReported();
    void combineKeyAcceptsChineseLabel();
    void caseOverrideRoundTrips();
    void crlfPresetFileIsReadCorrectly();
    void presetBodyReusesTheDeclarationSyntax();
    void fileWithoutRecordsIsReported();
    void serializedPresetIsParseableByTheFilterItself();

    // ---------- H 描述与边界 ----------
    void emptyFilterKeepsEverything();
    void emptyNameIsRejectedEvenWithNoneOf();
    void declarationKeyIsStable();
    void describeMentionsCombineCountAndCase();
    void caseSensitivityOverrideIsReported();
    void caseInsensitiveWildcardMatchesWindowsStyleNames();
    void parseReportsTheModeOfEachExpression();

    // ---------- I 源码级护栏 ----------
    void nameFilterDoesNotReadTheFileSystem();
    void contentAccessScanDetectsAPlantedRead();

    // ---------- J 表自检（护栏本身要能被反向验证） ----------
    void tableSelfCheckIsClean();
    void tableSelfCheckCatchesADuplicatePrefix();
    void tableSelfCheckCatchesAMissingMode();
    void tableSelfCheckCatchesAnEmptyMeaning();
    void tableSelfCheckCatchesAMissingCombineMode();
    void tableSelfCheckCatchesASharedExplanation();
    void combineModeTableMatchesTheEnumeration();

private:
    QString readSourceFile(const QString &relativePath);
};

#endif // LQCOMPARE_TST_NAMEFILTER_H
