#ifndef LQCOMPARE_TST_PLATFORMICON_H
#define LQCOMPARE_TST_PLATFORMICON_H

#include <QObject>
#include <QtTest>

///
/// \brief 系统图标服务的测试（PRD: PLAT-004）。
///
/// 分五组：
///   1. 缓存键——「一个键代表什么」是本条目最容易写错的地方（第 2 条）。
///   2. 尺寸——基准尺寸与 DPI 怎么换算成取图尺寸（第 5 条）。
///   3. 缓存——有界、LRU、统计（第 2 条）。
///   4. 请求队列——去重是「大目录不变慢」的关键（第 3 条）。
///   5. 服务——缓存 / 去重 / 异步 / 回退串起来（第 2~4 条），
///      最后几条走真实的系统图标源。
///
class TstPlatformIcon : public QObject
{
    Q_OBJECT

private slots:
    // --- 1. 缓存键 ----------------------------------------------------------
    void extensionKeyTakesThePartAfterTheLastDot();
    void extensionKeyIgnoresDotsInDirectoryNames();
    void extensionKeyIsCaseFolded();
    void extensionKeyOfDotFileKeepsTheNameAfterTheDot();
    void trailingDotHasNoExtension();
    void extensionKeyTrimsSurroundingBlanks();
    void pathWithoutNameHasNoExtension();
    void cacheKeySeparatesFilesFromDirectories();
    void cacheKeyOfExtensionlessDirectoryUsesSentinel();
    void cacheKeyOfDirectoryWithExtensionKeepsIt();
    void parseCacheKeyRoundTrips();
    void parseCacheKeyRejectsMalformedInput();

    // --- 2. 尺寸 ------------------------------------------------------------
    void iconPixelSizeScalesWithDeviceRatio();
    void iconPixelSizeTreatsBogusRatioAsOne();
    void iconPixelSizeIsZeroWhenNothingRequested();
    void iconPixelSizeClampsToUpperBound();
    void windowsShellSizeSnapsToAvailableSizes();

    // --- 3. 缓存 ------------------------------------------------------------
    void cacheReportsHitAndMiss();
    void cacheHitRateIsZeroBeforeAnyLookup();
    void cacheEvictsLeastRecentlyUsed();
    void cacheLookupRefreshesRecency();
    void cacheShrinkingCapacityEvictsImmediately();
    void cacheCapacityHasFloorOfOne();
    void cacheContainsDoesNotCountAsHit();
    void cacheClearKeepsStatistics();
    void cacheRefusesNothingOnInsertOfSameKey();

    // --- 4. 请求队列 --------------------------------------------------------
    void queueDeduplicatesSameKey();
    void queueKeepsKeysPendingUntilFinished();
    void takeReadyDoesNotReleasePending();
    void finishAllowsSameKeyToBeEnqueuedAgain();
    void queueDropsEmptyKeys();
    void takeReadyWithZeroCountReturnsNothing();

    // --- 5. 服务 ------------------------------------------------------------
    void serviceFallsBackToBuiltinWhenProviderHasNothing();
    void serviceFallsBackToFolderKindForDirectories();
    void serviceCachesByExtensionNotByFile();
    void serviceDoesNotLetDirectoryPoisonFileKey();
    void servicePassesScaledPixelSizeToProvider();
    void serviceChangingRatioClearsCache();
    void serviceChangingBaseSizeClearsCache();
    void serviceRequestEmitsSynchronouslyOnCacheHit();
    void serviceRequestDeduplicatesInFlightWork();
    void serviceDestructsWithPendingWork();
    void serviceUsesBuiltinIconForTypedEntry();

    // --- 6. 真实系统图标源 --------------------------------------------------
    void realProviderIsCreatedAndNamed();
    void realProviderResolvesTextAndFolderDifferently();
    void serviceWithRealProviderAlwaysProducesSomething();
};

#endif // LQCOMPARE_TST_PLATFORMICON_H
