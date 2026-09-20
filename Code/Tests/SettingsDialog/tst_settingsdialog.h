#ifndef LQCOMPARE_TST_SETTINGSDIALOG_H
#define LQCOMPARE_TST_SETTINGSDIALOG_H

#include <QObject>
#include <QStringList>
#include <QtTest>

#include "settingschema.h"
#include "settingsdialog.h"

///
/// \brief 记录写入的会话设置实现。
///
/// 与 Tests/Settings 里的 `CountingSettings` 是同一个手法（那一份在另一个工程里，
/// 不能共用）：断言「应用只写有改动的键」时，只看目标里有没有值区分不了
/// 「只写了改动的」与「全写了一遍」。
///
class RecordingSettings : public LqCompare::SessionSettings
{
    Q_OBJECT

public:
    explicit RecordingSettings(QObject *parent = nullptr);

    QStringList keys() const override;
    bool contains(const QString &key) const override;
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const override;
    bool setValue(const QString &key, const QVariant &value) override;
    bool remove(const QString &key) override;
    void clear() override;

    void seed(const QString &key, const QVariant &value);
    int writeCount() const { return m_writes.size(); }
    QStringList writes() const { return m_writes; }
    void resetCounters() { m_writes.clear(); }

private:
    QVariantMap m_values;
    QStringList m_writes;
};

///
/// \brief 会话设置对话框的用例（PRD: SESS-006）。
///
/// 分组对齐规格的四条完成标准：
///   A 结构：左 Tab 列表 / 右内容 / 底部作用域下拉 + 四个按钮，界面由声明生成（第 1、2 条）
///   B 未保存改动时切换 Tab 或关闭的确认（第 3 条）
///   C 四个按钮各自的动作
///   D 校验不通过时的界面反馈
///   E 「界面由数据生成」的反向验证（含一道源码级护栏）
///
class TstSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // --- A 结构与「由声明生成」 ----------------------------------------------
    void tabListSitsOnTheLeftAndContentOnTheRight();
    void footerCarriesTheScopeComboAndFourButtons();
    void scopeComboCoversAllThreeScopesWithDescriptions();
    void tabListFollowsTheSchema();
    void everyItemGetsATitleEditorAndDescription();
    void descriptionLabelCarriesTheDeclaredText();
    void eachControlTypeMapsToTheMatchingWidget();
    void integerEditorUsesTheDeclaredBounds();
    void textEditorUsesTheDeclaredMaxLength();
    void choiceEditorIsPopulatedFromTheSchema();
    void editorsShowTheSessionsCurrentValuesNotTheDefaults();
    void dialogWithoutTabsIsStillUsable();
    void groupsBecomeGroupBoxesInTheOrderDeclared();
    void advancedAndProFlagsReachTheWidgets();

    // --- B 切 Tab 与关闭的确认（第 3 条） ------------------------------------
    void switchingTabWithoutChangesDoesNotAsk();
    void switchingTabWithChangesCanStayPut();
    void switchingTabWithChangesCanApplyFirst();
    void clickingTheTabListGoesThroughTheSameInquiry();
    void refusingToSwitchKeepsTheSelectionOnTheOldTab();
    void closingWithChangesAsksAndCanBeRefused();
    void closingWithChangesCanDiscardThem();
    void closingWithChangesCanApplyFirst();
    void escapeGoesThroughTheSameInquiry();
    void closingWithoutChangesDoesNotAsk();
    void aFailedApplyKeepsTheDialogOpen();

    // --- C 四个按钮 -----------------------------------------------------------
    void applyWritesTheChangedKeysAndClearsTheDirtyMarks();
    void applyIsDisabledWhenThereIsNothingToApply();
    void applyIsDisabledWhileTheValueIsInvalid();
    void cancelClosesWithoutWriting();
    void restoreDefaultsResetsOnlyTheCurrentTab();
    void restoreDefaultsCanBeUndoneByCancel();
    void restoreDefaultsIsDisabledWhenEverythingIsDefault();
    void okAppliesAndCloses();
    void okWithoutChangesJustCloses();
    void okStaysOpenWhenTheValueIsInvalid();

    // --- D 校验的界面反馈 -----------------------------------------------------
    void anInvalidValueShowsAProblemUnderTheItem();
    void fixingTheValueClearsTheProblem();
    void theProblemTextIsTheServiceLayersConclusion();
    void theDirtyTabIsMarkedByFontWeight();
    void theDirtyMarkIsClearedAfterApply();

    // --- E 反向验证 -----------------------------------------------------------
    void twoDifferentSchemasProduceTwoDifferentDialogs();
    void aKeyFromAnotherSchemaHasNoEditorHere();
    void theDialogSourceHardcodesNoSchemaKeys();

private:
    // 合成声明：形状照着真实的会话设置（文件夹比对那一套）来，内容不属于任何
    // 具体类型——SESS-006 的框架里刻意不含具体设置项。与 Tests/Settings 用的那份
    // **不同**，两边各自证明「界面由声明生成」。
    LqCompare::SettingsSchema folderLikeSchema() const;
    LqCompare::SettingsSchema tinySchema() const;
};

#endif // LQCOMPARE_TST_SETTINGSDIALOG_H
