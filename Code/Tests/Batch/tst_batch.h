#ifndef LQCOMPARE_TST_BATCH_H
#define LQCOMPARE_TST_BATCH_H

#include <QObject>
#include <QtTest>

///
/// \brief 批量操作的失败处置测试（PRD: PLAT-008）。
///
/// 分四组：
///   1. 错误携带——分类之外还要留住原始系统码（完成标准第 1、5 条）。
///   2. 失败清单——按分类汇总、给出建议、标出哪些可重试（第 2 条）。
///   3. 执行流程——中途失败不中断、重试只重试失败项、两条出路（第 3、4 条）。
///   4. 真实实现——批量改属性走真实文件系统，端到端验证一次。
///
class TstBatch : public QObject
{
    Q_OBJECT

private slots:
    // --- 1. 错误携带 --------------------------------------------------------
    void posixErrorKeepsCategoryAndRawCode();
    void successfulErrorHasNoRawCode();
    void windowsErrorKeepsRawCode();
    void cocoaErrorKeepsRawCode();
    void unknownCodeStillKeepsItsNumber();
    void errorDetailNamesKnownCodes();
    void errorDetailIsEmptyWithoutRawCode();
    void errorReportAppendsDetailToMessage();
    void sameCategoryStaysDistinguishableByRawCode();
    void errorCodeConvertsToCategoryForOldCallSites();

    // --- 2. 失败清单 --------------------------------------------------------
    void emptyReportIsAllSucceeded();
    void reportSplitsPathsInOrder();
    void failureGroupsMergeByCategory();
    void failureGroupsFollowFirstAppearanceOrder();
    void failureGroupsCarryAdviceAndRetryability();
    void retryablePathsOnlyPicksRetryableCategories();
    void failureSummaryMentionsPathAdviceAndRawCode();
    void failureSummaryIsEmptyWhenNothingFailed();
    void failureSummaryMentionsStopAndAttempts();

    // --- 3. 执行流程 --------------------------------------------------------
    void defaultPolicyKeepsGoingAfterFailure();
    void stopOnFirstErrorKeepsCompletedProgress();
    void skippedItemsAreNotReportedAsSuccessful();
    void retryFailedOnlyRunsFailedItems();
    void retryFailedWithNoFailuresDoesNothing();
    void retryFailedMergesIntoWholeReport();
    void attemptsAccumulateAcrossRetries();
    void duplicatePathsAreCollapsed();
    void progressCallbackSeesEveryItem();
    void reportCarriesRawCodeFromOperation();
    void runAgainStartsANewBatch();

    // --- 4. 真实实现 --------------------------------------------------------
    void setAttributesBatchReportsReadOnlyWithAdvice();
    void setAttributesBatchRetriesAfterFailureCleared();
    void setAttributesBatchKeepsCompletedFilesOnFailure();
    void setAttributesBatchOnRealFileSystem();
};

#endif // LQCOMPARE_TST_BATCH_H
