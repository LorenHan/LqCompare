#ifndef TST_ENTRYSTATUS_H
#define TST_ENTRYSTATUS_H

#include <QObject>
#include <QtTest>

class EntryStatusTests : public QObject
{
    Q_OBJECT
private slots:
    // A 主状态：互斥集合、稳定标识符、图标
    void mainStatusTableCoversTheSpecifiedSetExactlyOnce();
    void mainStatusIdentifiersAreTheEstablishedMachineWords();
    void everyStatusHasItsOwnIcon();
    void statusIconsAreDeclaredInTheResourceFile();
    void onlyBaselineDerivedStatusesRequireABaseline();
    void knownStatusRejectsOutOfRangeIntegers();

    // B 内容证据
    void contentEvidenceTableKeepsTheSpecifiedTiers();
    void claimingIdentityRequiresHavingReadEverything();
    void contentEvidenceIdentifiersAreStable();

    // C 时间关系
    void timeRelationIsIndependentOfStatus();
    void compareTimesHonoursToleranceAndInvalidInput();
    void orphanEntriesHaveNoTimeRelation();

    // D 存在性与孤儿项
    void existenceFollowsTheTwoSides();
    void orphanSetIsADisplaySetNotASecondStatus();

    // E 基线
    void baselineMustBeBoundAndPopulated();
    void baselineOnlyRefinesLeafEntries();
    void withoutAValidBaselineNothingIsRefined();
    void conflictNeedsBothSidesToHaveMoved();

    // F 父子一致
    void parentAggregateTruthTable();
    void cancelledScanNeverAggregatesToSame();

    // G 模型自检
    void modelSelfCheckCatchesEveryImpossibleCombination();
    void modelSelfCheckIsSilentOnWellFormedEntries();

    // H 为什么是这个状态
    void reasonLinesListCriteriaOverridesAndConclusion();
    void reasonLinesReportTheBaselineHonestly();
    void reasonLinesExplainShortCircuitsThatActuallyHappened();
};

#endif
