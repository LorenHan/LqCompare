#ifndef LQCOMPARE_TST_TEXTRULES_H
#define LQCOMPARE_TST_TEXTRULES_H

#include <QObject>
#include <QtTest>

///
/// \brief 内置替换规则的测试（PRD: TXT-012）。
///
/// 分组对齐规格的四条完成标准：
///   A 规则表        —— 标准第 1 条（四条内置规则都登记了、标识符可用于设置键）
///                       与第 2 条里「可显示的正则与说明」那一半
///   B 单条规则行为  —— 标准第 1 条（每条规则真的吃得对，且不吃错）
///   C 开关集合      —— 标准第 2 条（每条规则可单独开关；默认全关）
///   D 顺序与叠加    —— 标准第 3、4 条（按声明顺序依次应用 + 固定语料）
///   E 与引擎的集成  —— 标准第 3 条（规则在**比对前**统一作用于行内容）
///
/// 标准第 2 条里「界面显示」那一半**刻意不在本套件**：界面接通属设置页（OPT-*），
/// 本套件只钉住「正则与说明是服务层的可测数据、不必在界面里再抄一份」。
///
class TstTextRules : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // A 规则表
    void tableDeclaresEverySpecifiedRuleWithMachineReadableIdentifiers();
    void validationRejectsBrokenTables();

    // B 单条规则行为
    void leadingNumberNeedsSeparatorAndSpace();
    void dateTimeCoversItsFormsWithoutEatingPlainNumbers();
    void guidIgnoresCaseAndBracesButNotPartialMatches();
    void hexAddressNeedsItsPrefix();

    // C 开关集合
    void rulesAreOffByDefaultAndAnEmptySetShortCircuits();
    void eachRuleTogglesIndependently();
    void enabledRulesFollowTableOrderNotToggleOrder();
    void unknownEnumValueIsSkippedInsteadOfFallingBack();
    void everyRuleExposesItsPatternAndDescription();

    // D 顺序与叠加
    void singleRuleReplacementMatchesTheFixedCorpus();
    void overlappingRulesRevealTheApplicationOrder();
    void stackedRulesRewriteOneLineInOnePass();
    void everyMatchOnALineIsReplaced();

    // E 与引擎的集成
    void compareIgnoresTheDifferencesTheRulesCover();
    void normalizedLineRunsReplacementsBeforeCaseFolding();
};

#endif
