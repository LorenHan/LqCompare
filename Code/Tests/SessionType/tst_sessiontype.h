#ifndef LQCOMPARE_TST_SESSIONTYPE_H
#define LQCOMPARE_TST_SESSIONTYPE_H

#include <QObject>
#include <QStringList>
#include <QtTest>
#include <QVector>

#include "sessiontype.h"

///
/// \brief 会话类型描述子与注册表的用例（PRD: SESS-002）。
///
/// 分六组：
///   A 条目字段齐备（第 1 条）
///   B 类型 ID 的稳定性（第 2 条）
///   C 按掩码查询与注册顺序优先（第 3 条）
///   D 可枚举、供 Home 页生成入口（第 4 条）
///   E 按名字查（为 CLI-002 备好，本轮不主张 CLI-002 已完成）
///   F 自检、诊断与两道「能自证会报错」的源码级护栏
///
class TstSessionType : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // --- A 条目字段齐备 -------------------------------------------------------
    void builtInTableHasFourteenEntries();
    void everyBuiltInEntryHasAllRequiredFields();
    void builtInTypesCarryNoFactory();
    void entryFieldsSurviveRegistration();
    void factoryIsKeptAndCallable();
    void absentFactoryIsDistinguishableFromUnknownType();
    void proFlagMatchesTheResearchBaseline();
    void windowsOnlyFlagMatchesTheResearchBaseline();
    void platformScopeLabelSaysWindowsOnly();
    void unavailableReasonExplainsWhyAndIsEmptyWhenAvailable();
    void directoryTypesCarryNoFileMask();
    void fallbackAndAuxiliaryTypesCarryNoFileMask();
    void registrationRejectsAnEmptyMaskEntry();

    // --- B 类型 ID 的稳定性 ---------------------------------------------------
    void publishedIdsMatchSnapshot();
    void publishedIdsAreUniqueAndWellFormed();
    void idValidityFollowsTheDocumentedRule();
    void registryRejectsInvalidIdentifiers();
    void registryRejectsDuplicateIdentifiers();
    void registryRejectsEmptyDisplayName();
    void registryRejectsUnparseableMask();
    void addClearsTheErrorOnSuccess();
    void builtInTableValidatesClean();
    void idsAreStableAcrossRebuilds();
    void englishNamesAreStableAndNotTranslated();

    // --- C 按掩码查询与注册顺序 -----------------------------------------------
    void findByMaskReturnsTheFirstRegisteredMatch();
    void findByMaskFollowsRegistrationOrderNotTableOrder();
    void findByMaskReturnsNullWhenNothingMatches();
    void findByMaskReturnsNullForAnEmptyName();
    void findByMaskSkipsTypesUnavailableOnThisPlatform();
    void allByMaskKeepsRegistrationOrder();
    void allByMaskCountsEachTypeOnceWhenTwoMasksHit();
    void allByMaskKeepsTheOnlyRealOverlap();
    void onlyHtmlOverlapsInTheBuiltInTable();
    void maskMatchingHonoursTheExplicitCaseSensitivity();
    void caseSensitivityOverrideAffectsTheConvenienceOverload();
    void clearCaseSensitivityOverrideReturnsToThePlatformDefault();
    void masksMatchTheNameNotTheWholeRelativePath();
    void crossDirectoryMaskWorksBecauseTheFullMaskLanguageIsReused();

    // --- D 可枚举 -------------------------------------------------------------
    void groupsAreReturnedInAFixedOrder();
    void byGroupKeepsRegistrationOrderWithinTheGroup();
    void byGroupSkipsUnavailableTypesByDefault();
    void byGroupCanIncludeUnavailableTypes();
    void enumerationCoversEveryIdExactlyOnce();
    void homePageHardcodedIdsMatchTheRegistry();
    void homePageIdCheckCanFailOnBrokenSource();

    // --- E 按名字查 -----------------------------------------------------------
    void findByNameMatchesIdCaseInsensitively();
    void findByNameMatchesEnglishNameCaseInsensitively();
    void findByNameMatchesDisplayName();
    void findByNamePrefersIdOverDisplayName();
    void findByNameReturnsNullForUnknownOrEmptyName();

    // --- F 自检、诊断与源码级护栏 ---------------------------------------------
    void validateReportsMissingEnglishName();
    void validateDetectsTheAuthoringConventions();
    void registryStartsEmptyAndClearRemovesEverything();
    void describeListsEveryTypeAndItsFlags();
    void typeDescribeExplainsTheMissingMask();
    void iconKeysPointAtDeclaredResources();
    void iconKeyCheckCanFailOnAFabricatedKey();

private:
    /// 构造一个只含合成类型的注册表，用于验证「注册顺序 = 优先级」这类
    /// 与内置表内容无关的规则。
    static LqCompare::SessionType makeType(const QString &id, LqCompare::SessionGroup group = LqCompare::SessionGroup::Text);

    /// 从一个完整的 `Views/Shell/homepage.cpp` 文本里取出 `sections()` 的函数体。
    ///
    /// 抽成独立函数是为了能对**故意写坏的源码文本**跑一次反向验证：
    /// 只对真实文件跑一遍的检查，无法证明失败时它真的会失败。
    static QString sectionsBodyOf(const QString &source);
};

#endif // LQCOMPARE_TST_SESSIONTYPE_H
