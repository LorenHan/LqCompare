#ifndef LQCOMPARE_TST_FILTERSTACK_H
#define LQCOMPARE_TST_FILTERSTACK_H

#include <QObject>
#include <QtTest>

///
/// \brief 三层过滤的叠加、启用状态、最终表达式与落点的测试（PRD: FILT-005）。
///
/// 分组对齐规格的五条完成标准：
///   A 层级与落点     —— 第 1 条（三层顺序）与第 4 条（每一层存到哪儿）
///   B 三层叠加       —— 第 1、5 条（层间取交集、层内排除优先、结论可解释）
///   C 启用与生效     —— 第 2 条（每层可单独启用/禁用、显示是否当前生效）
///   D 表达式与面板   —— 第 3 条（合并后的表达式与匹配计数）
///   E 视图层不落盘   —— 第 4 条（视图临时过滤不写入会话，关标签即丢弃）
///   F 计数           —— 第 3、5 条（合并计数与逐层计数）
///
/// 本套件**刻意不链接 QtGui**（见 FilterStackTests.pro）：整个过滤模块都是纯逻辑，
/// 一旦哪天有人把界面依赖塞进 `filterstack.cpp`，本工程会直接构建失败。
/// 它**链接 QtCore 的 Services/Session**：第 4 条要用真正的 `SessionSettings`
/// 存储来断言「视图层的声明没有漏进会话层」，用一个测试替身替不掉这件事。
///
class TstFilterStack : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 层级与落点（第 1、4 条的结构面） ----------
    void layerOrderMatchesTheSpecification();
    void layerIdentifiersAreStableAndDistinct();
    void layerIndexesMatchTheSpecOrder();
    void storageRoutesEachLayerToItsOwnPlace();
    void onlyTheViewLayerIsDiscardedOnClose();
    void formatLayerIsNotEditableOthersAre();
    void allThreeLayersShareOneDeclarationKey();
    void freshStackHasNoActiveLayer();

    // ---------- B 三层叠加（第 1、5 条） ----------
    void everyActiveLayerMustAdmitTheSubject();
    void addingALayerOnlyNarrowsTheResult();
    void sessionWhitelistNarrowsTheFormatWhitelist();
    void viewWhitelistNarrowsFurther();
    void anyLayerExclusionWinsOverAllOtherAdmissions();
    void exclusionInTheOutermostLayerAlsoWins();
    void exclusionOutranksNotMatchedAcrossLayers();
    void noWhitelistAnywhereKeepsEverythingElse();
    void oneWhitelistMakesEveryLayerFollowIt();
    void decisionNamesTheFirstExcludingLayer();
    void decisionNamesTheFirstRejectingWhitelist();
    void decisionNamesTheLastWhitelistForIncluded();
    void decisionNamesTheLastActiveLayerWhenNoWhitelistExists();
    void allExcludingLayersAreReported();
    void disabledLayerDoesNotAffectTheVerdict();
    void enabledButEmptyLayerDoesNotAffectTheVerdict();
    void emptyStackAcceptsEverything();
    void decisionReportsThatNoLayerIsActive();
    void oneBadLineOnlyDropsThatLine();
    void errorsAreReportedWithTheirLayerName();

    // ---------- C 启用与生效（第 2 条） ----------
    void newStackHasEveryLayerEnabled();
    void enabledButEmptyLayerIsNotActive();
    void disablingAnActiveLayerMakesItInactive();
    void statusTextDistinguishesDisabledFromEmpty();
    void statusTextReportsRuleCounts();
    void statusTextMentionsErrorsWhileOtherRulesStayActive();
    void activeLayersListsOnlyTheActiveOnes();
    void setLayerStateReparsesFromTheDeclaration();
    void setLayerStateForcesTheLayerField();
    void caseSensitivityOverrideAppliesToEveryLayer();
    void caseOverrideSurvivesLaterDeclarations();
    void switchingPlatformReparsesEveryLayer();
    void clearingCaseOverrideReturnsToPlatformDefault();
    void settingTheSamePlatformIsANoOp();

    // ---------- D 表达式与面板（第 3 条） ----------
    void expressionCombinesWhitelistsWithAndBlacklistsWithOr();
    void expressionIsEmptyWhenNothingIsActive();
    void expressionOmitsDisabledAndEmptyLayers();
    void expressionQuotesAtomsThatContainOperators();
    void expressionQuotesAtomsThatContainSpaces();
    void expressionDeduplicatesRepeatedRules();
    void expressionOfPureExcludesIsANegation();
    void layerExpressionDescribesThatLayerAlone();
    void panelRowsFollowTheSpecOrder();
    void panelReportsStatusStorageAndSource();
    void panelSummaryReusesTheFilterWording();
    void panelDecidedCountsAttributeTheDecision();
    void panelExpressionMatchesTheStack();
    void panelIsEmptyWhenNothingIsActive();
    void panelTextListsEveryLayerAndTheExpression();

    // ---------- E 视图层不落盘（第 4 条） ----------
    void savingTheViewLayerWritesOnlyTheViewStore();
    void savingTheSessionLayerWritesOnlyTheSessionStore();
    void formatLayerCanNeverBeSaved();
    void savingWithoutAViewStoreFailsInsteadOfFallingBack();
    void clearingALayerIsIdempotent();
    void discardViewLayerRemovesExactlyOneKey();
    void loadIntoReadsEachLayerFromItsOwnStore();
    void loadIntoDoesNotUseTheScopePrecedenceChain();
    void loadIntoPreservesEnabledFlags();
    void loadIntoWithoutStoresClearsDeclarations();
    void binderReportsWhatIsConnected();

    // ---------- F 计数（第 3、5 条） ----------
    void previewCountsEachVerdict();
    void previewCountsEachLayerIndependently();
    void previewDecidedMatchesTheDecision();
    void previewSummaryUsesTheFilterWording();
    void previewNamesEqualsPreviewSubjects();
    void stackDescribeMentionsTheExpression();

    // ---------- G 层级表自检（第 1、4 条的启动护栏） ----------
    void shippedLayerTableIsClean();
    void layerTableIsTheSingleSourceOfTruth();
    void validatorCatchesAMissingLayer();
    void validatorCatchesADuplicatedLayer();
    void validatorCatchesAReorderedTable();
    void validatorCatchesAViewLayerThatWouldPersist();
    void validatorCatchesAFormatLayerThatWouldBeWritable();
    void validatorCatchesAWrongRowCount();
};

#endif // LQCOMPARE_TST_FILTERSTACK_H
