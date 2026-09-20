#ifndef LQCOMPARE_TST_SETTINGS_SCOPE_H
#define LQCOMPARE_TST_SETTINGS_SCOPE_H

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QtTest>

#include "settingscope.h"

///
/// \brief 三层作用域的覆盖链与写入路由（PRD: SESS-007）。
///
/// 分五组：
///   A 优先级顺序（第 1 条：视图 > 会话 > 类型）
///   B 覆盖链与出厂默认（第 4 条：视图 → 会话 → 类型 → 出厂默认）
///   C 写入路由与「三层互不覆盖」（第 1 条的边界条款）
///   D 写入去向的文案与切换作用域的提示（第 2 条）
///   E 关闭标签时丢弃视图级设置（第 3 条）
///   F 解析诊断与反向验证
///
/// **这套件刻意不链接 QtGui**：覆盖链是纯数据合成，一旦哪天有人往
/// `settingscope.cpp` 里塞进界面依赖，本工程会直接构建失败。
/// 与 Tests/SessionType、Tests/Settings 是同一条纪律。
///
class TstSettingsScope : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // --- A 优先级顺序（第 1 条） ----------------------------------------------
    void priorityOrderIsViewSessionType();
    void ranksFollowThePriorityOrder();
    void everyScopeAppearsExactlyOnceInBothOrders();
    void higherPriorityScopeWins();
    void aLayerWithoutTheKeyIsSkipped();

    // --- B 覆盖链与出厂默认（第 4 条） ----------------------------------------
    void chainEndsAtTheFactoryDefault();
    void factoryDefaultComesBeforeTheCallersFallback();
    void aKeyWithoutADeclarationUsesTheCallersFallback();
    void anExplicitEmptyValueIsNotAMissingValue();
    void withoutASchemaTheChainStopsAtTheTypeLayer();
    void theFactoryDefaultIsHandedOutNormalised();

    // --- C 写入路由与「三层互不覆盖」（第 1 条边界） ---------------------------
    void writeLandsOnlyInTheTargetLayer();
    void writingToViewDoesNotPolluteSessionOrTypeDefaults();
    void writingToSessionDoesNotTouchTheViewLayer();
    void writingToTypeDoesNotTouchTheOtherTwoLayers();
    void writeFailsAndStoresNothingWhenTheTargetLayerIsMissing();
    void emptyKeyIsRejected();
    void aShadowedWriteIsStillStoredWhereItWasAskedToGo();
    void removeOnlyAffectsTheTargetLayer();
    void clearOnlyClearsTheTargetLayer();
    void keysAreTheUnionOfTheThreeLayers();
    void containsIgnoresTheFactoryDefault();

    // --- D 写入去向的文案与切换提示（第 2 条） --------------------------------
    void destinationTextMatchesTheSpecWording();
    void eachDestinationTextIsDistinct();
    void noPendingChangesMeansNoNotice();
    void switchingToTheSameScopeMeansNoNotice();
    void noticeNamesTheNewDestinationAndTheCount();
    void noticeDoesNotClaimThatAppliedChangesMove();
    void theDestinationTextFollowsTheWriteScope();

    // --- E 关闭标签时丢弃视图级设置（第 3 条） --------------------------------
    void noViewScopeSettingsMeansNoQuestion();
    void closePlanNamesTheCountAndSaysDiscarded();
    void discardRestoresTheLowerLayersValues();
    void discardSignalsOnlyKeysWhoseEffectiveValueChanged();
    void discardReturnsTheNumberOfDroppedKeys();
    void viewScopeKeysAreSortedAndDeduped();
    void discardWithoutAViewLayerIsHarmless();

    // --- F 解析诊断与反向验证 -------------------------------------------------
    void resolvedScopeReportsWhichLayerWon();
    void describeResolutionNamesTheLayerAndTheFactoryDefault();
    void changedFiresOnlyWhenTheEffectiveValueChanges();

private:
    /// 合成的设置声明。形状照着真实会话设置（文本比对那一套）来，内容不属于
    /// 任何具体类型——内置的 14 种会话类型至今一个声明都没有，用合成数据
    /// 既能证明机制成立，又不会与 TEXT-* / FOLD-* 的落地撞车。
    LqCompare::SettingsSchema syntheticSchema() const;
};

#endif // LQCOMPARE_TST_SETTINGS_SCOPE_H
