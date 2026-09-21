#ifndef LQCOMPARE_TST_SESSION_H
#define LQCOMPARE_TST_SESSION_H

#include <QObject>
#include <QtTest>

///
/// \brief 会话抽象基类的测试（PRD: SESS-001）。
///
/// 分组对齐规格的四条完成标准：
///   A 生命周期与状态迁移 —— 标准第 1 条（open / close / reload / save / isDirty / canSave）
///   B 视图契约           —— 标准第 1 条（createWidget，以及「只建一次」的保证）
///   C 三个公共出口       —— 标准第 3 条（状态栏文本 / 错误上报 / 进度上报）
///   D 设置接口           —— 标准第 1 条（sessionSettings）
///   E 可扩展性           —— 标准第 2 条（新增一种会话类型只需实现基类契约）
///   F 源码级护栏         —— 标准第 4 条（基类不依赖任何具体视图头文件）
///   G 与类型注册表的衔接 —— 标准第 2 条里「并注册」那半句
///
/// **本套件同时承担一条编译期校验**：`SessionTests.pro` 只把
/// `Views/Session` 与 `Services/Session` 放进 INCLUDEPATH，因此
/// `comparesession.h` 里一旦出现 `#include "homepage.h"` 之类的具体视图头文件，
/// 本工程会直接构建失败。F 组的源码级用例是这条编译期校验的第二层
/// （主程序构建不会失败，因为主构建的 INCLUDEPATH 里有 Views/Shell 等目录）。
///
class TstSession : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---------- A 生命周期与状态迁移（标准第 1 条） ----------
    void baseClassIsAbstract();
    void typeIdAndTitleBehaveAsDocumented();
    void openRunsTheImplementationOnceAndIsIdempotent();
    void openFailureStopsInFailedStateAndCarriesTheReason();
    void openFailureWithoutAReasonGetsAFallbackMessage();
    void openAfterCloseIsRefused();
    void reentrantOpenIsRefused();
    void reloadRefusesWhenSessionIsNotOpen();
    void reloadRefusesWhenSessionIsDirty();
    void reloadFailureKeepsTheSessionOpen();
    void reloadClearsDirtyOnSuccess();
    void saveRefusesWhenSessionIsNotOpen();
    void saveRefusesWhenNothingChanged();
    void saveClearsDirtyOnSuccess();
    void saveFailureKeepsTheDirtyFlag();
    void canSaveCanBeOverriddenByTheSessionType();
    void closeIsIdempotent();
    void closeFromFailedSessionStillReleasesResources();
    void stateChangesAreReportedInOrder();

    // ---------- B 视图契约（标准第 1 条） ----------
    void createWidgetBuildsTheViewOnlyOnce();
    void createWidgetPassesTheParentThrough();
    void createWidgetReturnsNullAfterClose();
    void createWidgetReportsWhenTheViewIsMissing();
    void widgetFollowsTheContainersLifetime();

    // ---------- C 三个公共出口（标准第 3 条） ----------
    void statusTextIsReportedOnlyWhenItChanges();
    void statusTextIsReadableBeforeAnySignal();
    void statusSeverityIsASeparateChannelFromTheText();
    void statusSeverityDefaultsToNormalWhenOnlyTextIsSet();
    void errorReportCarriesMessageAndDetail();
    void repeatedErrorsAreNotDeduplicated();
    void progressIsReportedOnlyWhenItChanges();
    void progressPercentIsClampedWhileRawValuesStayHonest();
    void unknownTotalHasNoPercent();

    // ---------- D 设置接口（标准第 1 条） ----------
    void everySessionHasASettingsStore();
    void settingsStoreIsCreatedLazilyAndReused();
    void sessionCanReplaceItsSettingsStore();
    void memorySettingsRoundTripAndFallback();
    void memorySettingsRejectsBlankKey();
    void memorySettingsIgnoresUnchangedValue();
    void memorySettingsRemoveAndClear();

    // ---------- E 可扩展性（标准第 2 条） ----------
    void aNewTypeNeedsOnlyTheBaseContract();
    void differentTypesShareTheSameBaseBehaviour();

    // ---------- F 源码级护栏（标准第 4 条） ----------
    void baseModuleIncludesStayInTheirOwnModules();
    void theIncludeGuardWouldCatchAConcreteViewInclude();

    // ---------- G 与类型注册表的衔接（标准第 2 条「并注册」那半句） ----------
    void aTypeRegisteredWithAFactoryProducesARealSession();
    void aSessionCreatedThroughTheRegistryRunsItsWholeLifecycle();
    void theCreatedSessionAgreesWithItsRegistryEntry();
    void typesWithoutAFactoryCannotBeCreated();
};

#endif // LQCOMPARE_TST_SESSION_H
