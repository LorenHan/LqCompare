#ifndef LQCOMPARE_TST_TRASH_H
#define LQCOMPARE_TST_TRASH_H

#include <QObject>
#include <QtTest>

///
/// \brief 回收站与可逆删除服务的测试（PRD: PLAT-003）。
///
/// 分五组：
///   1. 可用性与决策——「不可用时绝不静默降级为永久删除」这条约束的判定依据。
///   2. XDG 路径规则——Linux 的回收站位置与 .trashinfo 格式，在 macOS 上真实执行。
///   3. 错误分类——Cocoa 域的错误码，避免回收站失败时只剩一句「未知错误」。
///   4. 模板方法契约——用假替身验证基类 deleteToTrash() 的三步流程不被绕过。
///   5. 真实实现——真的移进本机废纸篓再还原，用往返证明删除确实可逆。
///
class TstTrash : public QObject
{
    Q_OBJECT

private slots:
    // --- 可用性与决策 ------------------------------------------------------
    void onlyAvailableCountsAsUsable();
    void availableNeedsNoUserChoice();
    void unavailableAlwaysNeedsUserChoice();
    void eachUnavailableReasonHasItsOwnAdvice();
    void adviceWarnsThatPermanentDeleteIsIrreversible();

    // --- XDG 路径规则（Linux 规则，在 macOS 上真实执行）--------------------
    void xdgHomeTrashUsesDataHome();
    void xdgHomeTrashFallsBackToLocalShare();
    void xdgHomeTrashIsEmptyWithoutHome();
    void xdgVolumeTrashUsesUserId();
    void xdgVolumeTrashRefusesUnwritableVolume();
    void xdgVolumeTrashFallbackUsesSuffix();
    void xdgTrashInfoEncodesSpecialCharacters();
    void xdgTrashInfoRoundTripsThroughParsing();
    void xdgTrashInfoUsesLocalTimeWithoutZoneSuffix();
    void parseXdgTrashInfoIgnoresOtherSections();
    void parseXdgTrashInfoWithoutPathIsEmpty();

    // --- 错误分类（Cocoa）--------------------------------------------------
    void classifyCocoaErrors();
    void classifyCocoaUnmountBusyIsRetryable();
    void classifyCocoaUnknownFallsBackToUnknown();

    // --- 模板方法契约（假替身）--------------------------------------------
    void unavailableTrashNeverCallsTheMover();
    void unavailableTrashReportsEveryEntry();
    void batchWithOneUnusableEntryIsRejectedWholly();
    void batchKeepsPartlySucceededEntries();
    void trashNeverRecordsPermanentDeletion();
    void undoUsesTheActualTrashedPath();
    void undoWithoutPriorDeleteIsNotFound();
    void undoClearsTheUndoPoint();
    void lastDeleteKeepsFailedBatches();

    // --- 真实实现（本机平台）----------------------------------------------
    void nativeTrashServiceReportsItsPlatform();
    void nativeTrashServiceHasDisplayLocation();
    void nativeTrashAvailabilityNeverAssumesUsable();
    void nativeTrashRoundTripRestoresTheFile();
    void nativeTrashRestoreRefusesToOverwrite();
};

#endif // LQCOMPARE_TST_TRASH_H
