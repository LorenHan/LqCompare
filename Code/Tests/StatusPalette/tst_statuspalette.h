#ifndef TST_STATUSPALETTE_H
#define TST_STATUSPALETTE_H

#include <QObject>
#include <QtTest>

// 状态着色与图标（DIR-012）。用例分组对齐五条完成标准的 A~E 组，
// 第 4 条（切换方案立即重绘）在 `Tests/Folder` 的 J 组里，因为它要真实视图。
class StatusPaletteTests : public QObject
{
    Q_OBJECT

private slots:
    // ---- A 组：第 1 条 每类状态有默认颜色与图标，图标随主题适配深浅 ----
    void everySchemeColoursEveryStatusInBothThemes();
    void everyStatusOwnsAnIconKeySoColourIsNeverTheOnlyCarrier();
    void lightAndDarkVariantsActuallyDiffer();
    void theTwoReferenceBackgroundsAreOppositeInLightness();
    // **这一条补的是五处「没有任何输入能走到」的校验分支**：展示名 / 说明为空、
    // 参考背景不是 #rrggbb、两档背景不呈一明一暗。接手被中断的那一轮时逐个扫过
    // `validateOneColorScheme()` 的每一条 `problems <<`，只有这四条（加上 C 组的
    // 「已排除」非色值那条）在全部用例里找不到任何断言——它们不是写错了，
    // 而是**没人喂过能触发它们的表**。判据：把分支整段删掉，谁变红？
    void aSchemeIsJudgedOnItsNameAndItsReferenceBackgroundsToo();
    void unknownSchemeIdentifierFallsBackToTheFactoryDefault();

    // ---- B 组：第 2 条 至少 3 套方案（默认/高对比/色盲友好）并可切换 ----
    void theTableShipsTheThreeNamedSchemes();
    void theFactoryTablePassesItsOwnValidator();
    void theHighContrastNameIsBackedByMeasuredAAA();
    void theColorBlindSchemeBeatsTheDefaultMeasurably();
    void claimingColorBlindFriendlyWithoutTheSeparationIsRejected();
    void crossSchemeChecksRejectAThinnedReorderedOrMislabelledTable();
    void theDefaultSchemeWouldFailTheColorBlindThresholdIfItClaimedIt();

    // ---- C 组：第 3 条 颜色仅辅助，图标与文字必须同时存在 ----
    void aStatusTableWithoutIconKeysIsRejected();
    void aSchemeMissingOrDuplicatingAStatusIsRejected();
    void aWashedOutColorIsRejected();
    void theExcludedColourMustBeReadableAndDistinctFromSame();
    void anUnparsableColourIsNotSilentlyTreatedAsBlack();
    // 标识符形状。**这条补的是一个曾经没人守的判据**：接手被中断的那一轮时，
    // 这段检查是 `if (false)` 包着的死代码，`isMachineReadableIdentifier()`
    // 全仓没有调用者。带空格 / 大写的标识符不重名，因此唯一性检查放它过去。
    void anIdentifierThatIsNotMachineReadableIsRejected();
    void anIdentifierIsJudgedOnItselfNotOnlyAgainstItsSiblings();

    // ---- D 组：第 5 条 自定义颜色可导出为配色文件并分享 ----
    void roundTripThroughAFilePreservesEveryField();
    void theSerializedFormIsKeyedByStatusIdentifierNotByIndex();
    void filesThatAreNotPalettesAreRejectedWithAUsefulReason();
    void structurallyBrokenEntriesAreRejected();
    void aHandEditedFileThatBreaksContrastIsRejected();
    void aFailedLoadLeavesTheCallersSchemeUntouched();
    void savingIntoAnUnwritablePathReportsWhy();
};

#endif
