#ifndef LQCOMPARE_TST_CONTENTFILTER_H
#define LQCOMPARE_TST_CONTENTFILTER_H

#include <QObject>
#include <QtTest>

///
/// \brief 内容过滤器的测试（PRD: FILT-004）。
///
/// 分组对齐规格的完成标准：
///   A 阶段顺序与「行过滤在前」 —— 第 3 条（作用顺序明确且有测试）
///   B 行过滤的三种模式        —— 第 1 条（匹配模式与它们各自的作用对象）
///   C 行过滤声明解析          —— 第 1 条（写错的行只丢那一行、就地报错、往返）
///   D 行过滤判定与统计        —— 第 1 条（排除哪些行、丢了多少、谁丢的）
///   E 关键字节的转义编解码    —— 第 2 条（`\xHH` / C 转义 / UTF-8、逐字节往返）
///   F 关键字节声明与判定      —— 第 2 条（**仅对二进制生效**、空规则集放行）
///   G 启用状态与提示          —— 规格「边界」条款（必须显式启用且给出性能提示）
///   H 三张表的自检            —— 反向验证：写坏的表必须被报出来
///   I 源码级护栏             —— 内容过滤不得自己读文件（它只吃调用方给的数据）
///
/// 本套件**刻意不链接 QtGui**（见 ContentFilterTests.pro）：内容过滤全是纯逻辑
/// （行匹配、字节搜索、声明解析、表的自检），一旦有人把界面依赖塞进
/// `contentfilter.cpp`，本工程会立刻构建失败。
///
/// 它**必须**额外链接 `Services/Text`，因为第 3 条那句「先过滤行再应用忽略规则」
/// 要拿**真的**忽略规则（`Text::CompareOptions`）验证才有意义——
/// 用测试里自己造的「忽略规则替身」验证等于什么都没验证。
/// 注意 `contentfilter.cpp` 本身**不**依赖 `Services/Text`：那是测试的依赖，
/// 不是模块的依赖。
///
class TstContentFilter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 阶段顺序与「行过滤在前」（第 3 条） ----------
    void stageOrderPutsLineFilterBeforeIgnoreRules();
    void stageTableIsTheSingleSourceOfTruth();
    void stageIndexMatchesTheTableOrder();
    void stageSelfCheckIsCleanForBuiltinTable();
    void stageSelfCheckReportsWrongOrder();
    void stageSelfCheckReportsDuplicateAndMissingStages();
    void stageSelfCheckReportsEmptyLabelAndIdentifier();
    void lineFilterReadsRawLinesNotNormalizedOnes();
    void lineFilterDecisionDoesNotDependOnIgnoreRules();
    void lineFilterThenIgnoreRulesComposesWithTheRealComparer();

    // ---------- B 行过滤的三种模式（第 1 条） ----------
    void modeTableIsTheSingleSourceOfTruth();
    void modePrefixRecognisesEqualsAndRegex();
    void indentedPrefixIsStillRecognised();
    void modePrefixIsEmptyForWildcard();
    void prefixMatchingPrefersTheLongestPrefix();
    void exactMatchesWholeLineOnly();
    void wildcardUsesMaskSyntaxAndMatchesWholeLine();
    void wildcardUsesTheMasksOwnSlashRule();
    void regexSearchesSubstringNotWholeLine();
    void regexKeepsUserAnchorsWorking();
    void wildcardMetacharactersAreNotRegex();
    void emptyLineIsNotMatchedByWildcard();
    void modeTableSelfCheckRejectsWholeLineRegex();
    void modeTableSelfCheckRejectsSubstringWildcard();
    void modeTableSelfCheckRejectsDuplicatePrefix();
    void modeTableSelfCheckRejectsMissingPrefixList();
    void modeTableSelfCheckRejectsUnknownMode();

    // ---------- C 行过滤声明解析（第 1 条） ----------
    void parseSkipsBlankLinesAndComments();
    void parseReportsEmptyExpressionWithHint();
    void parseReportsMaskSyntaxErrorAtColumn();
    void parseReportsInvalidRegexWithColumn();
    void parseReportsRiskyRegexWithoutBlockingIt();
    void parseKeepsGoodLinesWhenOneIsBad();
    void parseAcceptsCrLfAndCrLineEndings();
    void parseReportsOneBasedLineNumbers();
    void declarationRoundTripIsTextuallyIdentical();
    void toDeclarationTextWritesExplicitPrefixes();

    // ---------- D 行过滤判定与统计（第 1 条） ----------
    void excludesMatchesExactLine();
    void excludesIsCaseSensitive();
    void matchingPatternIndexesReportsEveryHit();
    void filterLinesKeepsOrderAndReportsDroppedLines();
    void filterLinesReportsHitsByPattern();
    void filterLinesWithEmptyFilterKeepsEveryLine();
    void filterLinesHandlesEmptyInput();
    void invalidPatternNeverExcludes();
    void filterLinesSummaryTextNamesBothCounts();
    void describeStatesPatternCount();

    // ---------- E 关键字节的转义编解码（第 2 条） ----------
    void decodeReadsHexEscapes();
    void decodeReadsCstyleEscapes();
    void decodeUsesUtf8ForNonAscii();
    void decodeReadsSurrogatePairAsOneUtf8Sequence();
    void decodeRejectsShortHexEscape();
    void decodeRejectsUnknownEscape();
    void decodeRejectsTrailingBackslash();
    void encodeKeepsPrintableAsciiVerbatim();
    void encodeEscapesNonPrintableBytes();
    void byteRoundTripIsExact();
    void byteRoundTripEscapesNewlineInsteadOfEmittingIt();

    // ---------- F 关键字节声明与判定（第 2 条） ----------
    void keyByteParseSkipsCommentsAndBlankLines();
    void keyByteParseReportsSyntaxAtColumn();
    void keyByteParseReportsDuplicateWithoutDroppingIt();
    void keyByteParseKeepsGoodLinesWhenOneIsBad();
    void keyByteDeclarationRoundTripIsExact();
    void anyOfAcceptsWhenAnySequenceIsPresent();
    void anyOfRejectsWhenNoneIsPresent();
    void allOfRequiresEverySequence();
    void emptyRuleSetAcceptsEverything();
    void textInputIsNotApplicable();
    void decisionReportsMatchedIndexAndReason();
    void combineModeTableIsTheSingleSourceOfTruth();
    void combineModeTableSelfCheckRejectsDuplicateKey();

    // ---------- G 启用状态与提示（规格边界条款） ----------
    void enablementDefaultsToOffWithNoRules();
    void enablementIsActiveOnlyWithBothFlagAndRules();
    void noticeWhenRulesConfiguredButNotEnabled();
    void noticeWhenEnabledWithoutRules();
    void noticeWhenNothingConfigured();
    void noticeWhenActiveIsThePerformanceNotice();
    void settingsKeysAreStableAndDistinct();

    // ---------- H 三张表的自检与反向验证 ----------
    void builtinTablesPassTheCombinedSelfCheck();
    void combinedSelfCheckReportsEveryBrokenTable();
    void repeatedTableLookupsStayStable();

    // ---------- I 源码级护栏 ----------
    void moduleNeverReadsFiles();
    void sourceGuardWouldCatchAnInjectedRead();

private:
    QString readSourceFile(const QString &relativePath) const;
};

#endif // LQCOMPARE_TST_CONTENTFILTER_H
