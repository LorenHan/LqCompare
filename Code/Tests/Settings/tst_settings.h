#ifndef LQCOMPARE_TST_SETTINGS_H
#define LQCOMPARE_TST_SETTINGS_H

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtTest>

#include "settingschema.h"

///
/// \brief 计数用的会话设置实现。
///
/// 用它而不是 `MemorySessionSettings` 是为了能断言**写入次数**：
/// 「没有改动时一个键都不写」与「校验不过时一个键都不写」这两条，
/// 只看「目标里有没有值」都验证不了——一个先把 8 项全写一遍、再把有问题的
/// 那几项改回去的实现同样能通过。调用次数是唯一能把它与正确的实现分开的证据。
///
/// 与 PLAT-008 的批量测试是同一条纪律（那里断言的是 `callsFor(路径)` 而不是
/// 「最后成功了」）。
///
class CountingSettings : public LqCompare::SessionSettings
{
    Q_OBJECT

public:
    explicit CountingSettings(QObject *parent = nullptr);

    QStringList keys() const override;
    bool contains(const QString &key) const override;
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const override;
    bool setValue(const QString &key, const QVariant &value) override;
    bool remove(const QString &key) override;
    void clear() override;

    /// 直接放一个值进去，不计入写入次数（用来构造「会话原本就是这些值」的场景）。
    void seed(const QString &key, const QVariant &value);

    int writeCount() const { return m_writes.size(); }
    QStringList writes() const { return m_writes; }
    void resetCounters() { m_writes.clear(); }

private:
    QVariantMap m_values;
    QStringList m_writes;
};

///
/// \brief 会话设置声明、草稿与询问策略的用例（PRD: SESS-006）。
///
/// 分六组：
///   A 声明与控件类型（第 2 条的「标题 / 说明 / 控件类型 / 默认值 / 校验规则」）
///   B 声明的自检
///   C 草稿的读写与脏判定（第 4 条）
///   D 应用、恢复默认与「全有或全无」
///   E 未保存改动的询问策略（第 3 条里可无界面验证的那一半）
///   F 声明目录与「界面由数据生成」的反向验证
///
class TstSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // --- A 声明与控件类型 -----------------------------------------------------
    void everyControlTypeHasAnIdentifierAndALabel();
    void anItemCarriesAllFiveDeclaredAspects();
    void schemaIsPlainDataAndCanBeBuiltHeadlessly();
    void itemsAreEnumeratedInDeclarationOrder();
    void tabAndItemLookupsAgreeWithTheDeclaration();
    void validationRulesSummariseInPlainWords();
    void requiredRuleIsEnforced();
    void lengthRulesAreEnforced();
    void integerBoundsAreEnforced();
    void choiceMustBeOneOfTheDeclaredValues();
    void choiceRejectsAValueOutsideTheTable();
    void maskListValidationReusesTheMaskLanguage();
    void maskListValidationReportsTheOffendingLine();
    void maskListLengthRulesCountEntriesNotCharacters();
    void emptyOptionalTextIsNotReportedAsTooShort();
    void validationFailureNamesTheItem();
    void everyScopeHasIdentifierLabelAndDescription();

    // --- B 声明的自检 ---------------------------------------------------------
    void syntheticSchemaValidatesClean();
    void duplicateKeyAcrossTabsIsReported();
    void duplicateGroupIdIsReported();
    void missingTitleOrDescriptionIsReported();
    void invalidSettingKeyIsReported();
    void reversedBoundsAreReported();
    void defaultValueViolatingItsOwnRuleIsReported();
    void defaultValueOfTheWrongTypeIsReported();
    void choiceWithoutOptionsIsReported();
    void choicesOnANonChoiceItemAreReported();
    void emptyGroupOrEmptySchemaIsReported();

    // --- C 草稿的读写与脏判定（第 4 条） --------------------------------------
    void controlRoundTripsHeadlessly_data();
    void controlRoundTripsHeadlessly();
    void draftStartsAtDefaultsAndIsClean();
    void loadFromTakesTheSessionsValues();
    void unknownAndEmptyKeysAreRejected();
    void settingTheSameValueIsNotAChange();
    void changesAreTrackedPerItemAndPerTab();
    void dirtyKeysFollowDeclarationOrder();
    void revertRestoresTheBaselineAndClearsDirty();
    void dirtySignalFiresOnlyOnTransitions();
    void maskListTextAndListAreTheSameValue();
    void trailingNewlineIsNotAChange();
    void isDefaultIsAboutTheFactoryDefaultNotTheBaseline();

    // --- D 应用与恢复默认 -----------------------------------------------------
    void applyWritesOnlyDirtyKeys();
    void applyDoesNothingWhenThereIsNoChange();
    void applyIsAllOrNothingWhenValidationFails();
    void applyReturnsTheAppliedKeysAndClearsDirty();
    void applyWithoutATargetFails();
    void applyNormalisesBeforeWriting();
    void resetToDefaultChangesTheDraftOnly();
    void resetTabCountsOnlyRealChanges();
    void resetTabLeavesOtherTabsAlone();
    void restoreDefaultsIsUndoableByRevert();

    // --- E 未保存改动的询问策略（第 3 条） ------------------------------------
    void noChangesMeansNoQuestion();
    void switchTabOffersApplyAndCancelButNotDiscard();
    void closeDialogOffersTheDiscardExit();
    void defaultActionIsAlwaysCancel();
    void inquiryTextNamesTheTargetTabAndTheCount();
    void inquiryDescribeMentionsTheOptions();

    // --- F 声明目录与反向验证 -------------------------------------------------
    void catalogRegistersFindsAndClears();
    void catalogRejectsIllegalAndDuplicateTypeIds();
    void catalogRejectsADeclarationThatFailsItsOwnSelfCheck();
    void catalogFindsTheCommonSchemaByEmptyTypeId();
    void catalogValidateAggregatesEveryProblem();
    void catalogDescribeCountsItems();
    void frameworkShipsNoHardcodedSettingItems();
    void theFrameworkIsNotSpecialCasedToTheSyntheticSchema();

private:
    // 合成声明：形状照着真实的会话设置（文本比对那一套）来，内容不属于任何具体
    // 类型——SESS-006 的框架里刻意不含具体设置项，内置的 14 种类型都还没有声明。
    // 用合成的声明能证明机制成立，又不会与 TEXT-* / FOLD-* 的落地撞车。
    LqCompare::SettingsSchema textLikeSchema() const;
    LqCompare::SettingsSchema minimalSchema() const;
};

#endif // LQCOMPARE_TST_SETTINGS_H
