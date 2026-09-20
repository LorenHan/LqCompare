#ifndef LQCOMPARE_TST_FILTER_H
#define LQCOMPARE_TST_FILTER_H

#include <QObject>
#include <QtTest>

///
/// \brief 掩码语法与解析器的测试（PRD: FILT-001）。
///
/// 分组对齐规格的五条完成标准：
///   A 掩码基本语义   —— 标准第 1 条（`*` / `?` 的语义与「名字 vs 路径」）
///   B 字符集         —— 标准第 1 条（`[...]`，含取反与区间）
///   C 跨目录         —— 标准第 1 条（`**`）
///   D 大小写策略     —— 标准第 3 条（平台默认 + 显式覆盖）
///   E 声明解析       —— 标准第 2 条（`-` 排除、注释、多行）
///   F 叠加           —— 标准第 2 条（排除优先、白名单语义、结论可解释）
///   G 预览           —— 标准第 4 条（「匹配 N 项 / 共 M 项」）
///   H 语法速查       —— 标准第 4 条（速查表与实现同源）
///   I 恶意与畸形输入 —— 标准第 5 条
///
/// 这个模块是纯函数，没有全局状态，因此不需要 init()/cleanup() 复位。
/// 反过来，本套件**刻意不链接 QtGui**（见 FilterTests.pro），
/// 这样哪天有人往掩码里塞进界面依赖会立刻构建失败。
///
class TstFilter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 掩码基本语义（标准第 1 条） ----------
    void starMatchesAnyRunInsideOneSegment();
    void starMatchesEmptyRun();
    void starDoesNotCrossSeparator();
    void questionMatchesExactlyOneCharacter();
    void questionDoesNotMatchSeparator();
    void literalMatchesItselfOnly();
    void maskWithoutSeparatorMatchesNameAtAnyDepth();
    void maskWithSeparatorMatchesRelativePathFromTheStart();
    void subjectWithEmptyNameNeverMatches();
    void defaultConstructedMaskIsInvalidAndMatchesNothing();
    void maskNormalizesSurroundingWhitespace();
    void maskIgnoresLeadingAndTrailingSeparators();
    void repeatedSeparatorsCollapse();

    // ---------- B 字符集（标准第 1 条） ----------
    void setMatchesAnyMember();
    void setRangeMatchesInclusiveBounds();
    void setNegatedMatchesEverythingElse();
    void caretIsAnAliasForNegation();
    void closingBracketAtStartIsALiteral();
    void leadingAndTrailingDashAreLiterals();
    void separatorCannotBeASetMemberBecauseItSplitsFirst();
    void backslashInsideSetIsALiteralMember();
    void invertedRangeIsRejected();
    void unterminatedSetIsRejected();
    void emptySetIsRejected();
    void multipleRangesInOneSetWork();

    // ---------- C 跨目录（标准第 1 条） ----------
    void doubleStarMatchesZeroSegments();
    void doubleStarMatchesMultipleSegments();
    void doubleStarInTheMiddleSpansArbitraryDepth();
    void doubleStarDoesNotMatchDifferentTail();
    void doubleStarInsideSegmentDegradesToStar();
    void doubleStarIsCountedPerSegment();
    void doubleStarSegmentIsRecordedStructurally();
    void absoluteLookingMaskIsTreatedAsRelative();

    // ---------- D 大小写策略（标准第 3 条） ----------
    void windowsDefaultsToCaseInsensitive();
    void posixDefaultsToCaseSensitive();
    void hostDefaultFollowsCurrentPlatform();
    void filterDefaultPlatformIsTheHostPlatform();
    void explicitCaseSensitivityOverridesPlatformDefault();
    void clearingOverrideReturnsToPlatformDefault();
    void caseInsensitiveSetMatchesInBothDirections();
    void caseInsensitiveRangeDoesNotInvert();
    void maskMatchingUsesTheGivenSensitivity();

    // ---------- E 声明解析（标准第 2 条） ----------
    void emptyDeclarationProducesNoRules();
    void blankLinesAreIgnored();
    void commentLinesAreIgnored();
    void leadingDashMarksExclude();
    void escapedLeadingDashIsALiteralName();
    void hashAfterExcludeMarkerIsLiteral();
    void crlfLineEndingsAreStripped();
    void loneCarriageReturnIsALineBreak();
    void lineNumbersAreReportedForRules();
    void lineNumbersAreReportedForErrors();
    void errorColumnIsInLineCoordinates();
    void errorColumnAccountsForLeadingWhitespace();
    void trailingBackslashIsRejected();
    void windowsStyleSeparatorInsideMaskIsRejectedWithHint();
    void oneBadLineDoesNotDisableTheOthers();
    void declarationWithoutTrailingNewlineStillParses();
    void ruleDescribeMentionsLineAndKind();

    // ---------- F 叠加（标准第 2 条） ----------
    void excludeWinsOverInclude();
    void includeOnlyActsAsWhitelist();
    void excludeOnlyKeepsEverythingElse();
    void emptyFilterAcceptsEverything();
    void excludedAndNotMatchedAreDistinct();
    void decisionNamesTheDecidingRule();
    void decisionExplainsDefaultInclusion();
    void matchingRuleIndexesReportsEveryHit();
    void filterDescribeCountsRulesAndCase();

    // ---------- G 预览（标准第 4 条） ----------
    void summaryUsesTheWordingFromTheSpec();
    void previewCountsEachVerdict();
    void previewCountsHitsPerRule();
    void previewOfEmptyFilterKeepsEverything();
    void hiddenIsTotalMinusIncluded();
    void previewNamesIsEquivalentToSubjects();

    // ---------- H 语法速查（标准第 4 条） ----------
    void everyReferenceEntryCompilesAndSamplesHold();
    void declarationEntriesAreExercisedThroughTheFilter();
    void referenceContainsBothOutcomes();
    void referenceEntriesAreUniqueAndNonEmpty();
    void referenceTextIsGeneratedFromTheSameData();
    void referenceTextExplainsTheDeclarationSyntax();
    void referenceIsPureAndRepeatable();

    // ---------- I 恶意与畸形输入（标准第 5 条） ----------
    void pathologicalCrossSegmentPatternCompletesQuickly();
    void pathologicalSegmentPatternCompletesQuickly();
    void manySegmentsDoNotBlowUp();
    void veryLongLiteralMaskIsHandled();
    void veryLongSetIsHandled();
    void compileIsPureAndRepeatable();
};

#endif // LQCOMPARE_TST_FILTER_H
