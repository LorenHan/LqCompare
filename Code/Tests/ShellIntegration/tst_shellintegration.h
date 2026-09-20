#ifndef TST_SHELLINTEGRATION_H
#define TST_SHELLINTEGRATION_H

#include "shellintegration.h"

#include <QObject>
#include <QString>

///
/// \brief 注册表存储与 Shell 集成的测试（PRD: PLAT-005）。
///
/// 分组刻意与规格的五条完成标准对齐，这样「哪条标准被守住了」可以一眼看出：
///   A 动作与目标       —— 完成标准第 1、2 条（菜单项内容与两步式）
///   B 选项             —— 完成标准第 3 条（可单独关闭）
///   C 命令行           —— 完成标准第 1 条（引号规则）
///   D 计划             —— 把上面几项拼成「要写什么」
///   E 安装             —— 完成标准第 4 条
///   F 卸载与还原       —— 完成标准第 3、4 条（还原，而不是删掉）
///   G 校验 / H 残留     —— 完成标准第 4 条（有校验）
///   I 能力             —— 完成标准第 5 条（置灰并说明）
///   J 预演 / K 解析     —— 支撑上面各条
///
class TstShellIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---------- A 动作与目标（标准第 1、2 条） ----------
    void everyActionHasUniqueIdentifier();
    void actionIdentifierRoundTrips();
    void unknownActionIdentifierIsRejected();
    void everyActionHasMenuTextAndDescription();
    void menuTextsUseDistinctAcceleratorsPerTarget();
    void requiredPathCountMatchesTheAction();
    void onlySecondStepIsPlaceholder();
    void onlyOpenActionIsAssociationOnly();
    void compareAcceptsManyPathsOthersAcceptOne();
    void targetClassKeysAreDistinctAndExpected();
    void backgroundTargetHasNoPlainCompare();
    void everyTargetOffersTheSameFourCompareActions();
    void targetsCoverFilesFoldersAndBackground();
    void includedByOptionsRespectsContextMenuSwitch();
    void includedByOptionsRespectsTwoStepSwitch();
    void includedByOptionsNeverExposesAssociationAction();

    // ---------- B 选项（标准第 3 条） ----------
    void optionsRoundTripThroughString();
    void optionsStringIsReadableInRegedit();
    void optionsParsingIsCaseInsensitive();
    void unknownOptionKeysAreReported();
    void optionsSummaryMentionsWhatIsTurnedOff();
    void anyEnabledDetectsAllOff();

    // ---------- C 命令行（标准第 1 条） ----------
    void quotingAlwaysAddsQuotesEvenWithoutSpaces();
    void quotingDoublesTrailingBackslash();
    void quotingEscapesEmbeddedQuote();
    void quotingEmptyStringGivesEmptyQuoted();
    void quotedArgumentsSurviveWindowsSplitting();
    void invocationLineQuotesPlaceholderExactlyOnce();
    void invocationLineCarriesTheActionSwitch();
    void singleItemPlaceholderIsAlreadyQuoted();
    void associationCommandUsesOpenAction();

    // ---------- D 计划 ----------
    void planIsPureFunctionOfItsInputs();
    void planWithoutContextMenuHasNoMenuEntries();
    void planWithoutTwoStepOmitsBothTwoStepItems();
    void planWithoutPatchOmitsPatchEntries();
    void planWithoutDiffOmitsDiffEntries();
    void planWithNothingEnabledIsEmpty();
    void planMenuTextMatchesAction();
    void planRestrictsSinglePathActionsToOneSelection();
    void planPositionOnlyWhenAtTop();
    void planIconOnlyWhenMenuIconEnabled();
    void planIconPointsAtExecutableIndexZero();
    void planCommandUsesQuotedExecutableAndPlaceholder();
    void extensionDefaultIsSharedWhileProgIdIsOwned();
    void planKeysAreAllRelativeToStoreRoot();
    void planSummaryMentionsBackupCount();
    void planDetailLinesListTargetsAndExtensions();
    void planKeysAreSortedShallowestFirst();

    // ---------- E 安装（标准第 4 条） ----------
    void installWritesEveryPlannedValue();
    void installMarksCompletionWithVersionOne();
    void installRecordsExecutablePathAndOptions();
    void installRefusesWhenBackendUnavailable();
    void installWithNothingEnabledDoesNotTouchStore();
    void installBacksUpSharedValueBeforeOverwriting();
    void installIsAtomicWhenOneEntryFails();
    void failedInstallLeavesStoreEmpty();
    void failedDuringBookkeepingLeavesStoreEmpty();
    void installTwiceKeepsTheOriginalBackup();
    void previewInstallDoesNotTouchRealStore();

    // ---------- F 卸载与还原（标准第 3、4 条） ----------
    void uninstallRestoresPreviousAssociation();
    void uninstallDeletesOurValuesAndKeys();
    void uninstallRemovesValueThatNeverExisted();
    void uninstallDeletesKeyWeCreated();
    void uninstallKeepsKeyWeDidNotCreate();
    void uninstallIsIdempotent();
    void uninstallWithoutInstallReportsNotInstalled();
    void reconfigureRemovesEntriesOfRemovedOptions();
    void uninstallRestoresUnsupportedValueKindByteForByte();
    void uninstallCleansInterruptedInstall();
    void uninstallAfterPartialWriteLeavesNoResidue();
    void uninstallKeepsForeignContentAndSaysSo();
    void uninstallReportsResidueWhenDeletionFails();
    void secondInstallReusesTheOriginalBackup();

    // ---------- G 校验（标准第 4 条） ----------
    void verifyPassesAfterInstall();
    void verifyReportsMissingEntry();
    void verifyReportsChangedValue();
    void verifyReportsMissingBookkeeping();
    void verifyReportsInterruptedInstall();

    // ---------- H 残留（标准第 4 条） ----------
    void residueIsCleanAfterCorrectUninstall();
    void residueFindsLeftoverValue();
    void residueFindsLeftoverKey();
    void residueFindsUnrestoredShare();
    void residueFindsForeignEntry();
    void residueWithoutPlanSaysItIsApproximate();

    // ---------- I 能力（标准第 5 条） ----------
    void memoryBackendIsAvailableButPlatformFollowsReality();
    void nativeBackendAvailabilityMatchesThePlatform();
    void unavailableCapabilityAlwaysExplainsWhy();
    void capabilitySummaryNeverEmpty();

    // ---------- J 预演 ----------
    void previewRefusesWithoutAScratchStore();
    void previewDetectsOverwriteOfExistingAssociation();

    // ---------- K 命令行解析 ----------
    void parseWithoutSwitchIsNotShellInvocation();
    void parseCompareWithTwoPathsIsValid();
    void parseCompareWithOnePathIsInvalid();
    void parseCompareWithSamePathTwiceIsInvalid();
    void parseUnknownActionIsInvalid();
    void parseDoubleSwitchIsInvalid();
    void parseBareFormIsAccepted();
    void parseTooManyPathsForSingleActionKeepsFirst();
    void parseOpenActionNeedsExactlyOnePath();
};

#endif // TST_SHELLINTEGRATION_H
