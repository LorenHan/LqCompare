#ifndef LQCOMPARE_TST_COMPARECONCLUSION_H
#define LQCOMPARE_TST_COMPARECONCLUSION_H

#include <QObject>
#include <QtTest>

///
/// \brief 文本比对结论档与 BOM 处理策略的测试（PRD: TXT-015）。
///
/// 分组对齐规格的四条完成标准：
///   A 结论档        —— 标准第 2 条（「相同 / 规则相同 / 不同」三档各自可达，
///                       且 BOM 被忽略时落在**规则相同**这一档）
///   B 比较侧策略表  —— 标准第 1 条（三档都登记了、可选集合、默认值、
///                       标识符可当设置键，以及自检真的会报）
///   C BOM 差异判定  —— 标准第 1、2 条（同一对事实在三个策略下的三种结论）
///   D 保存侧策略    —— 标准第 3 条（保留 / 强制写 / 强制不写，
///                       以及「做不到时必须降级」那条反直觉的边界）
///   E 编码判定      —— 标准第 4 条（UTF-8 BOM 绝不会被读成 UTF-16）
///
/// 端到端那两条（状态栏文案、保存后的磁盘字节）刻意不在本套件——
/// 它们要真的会话与真的文件，落在 `Tests/TextView`。
///
class TstCompareConclusion : public QObject
{
    Q_OBJECT

private slots:
    // A 结论档
    void everyConclusionTierHasItsOwnLabel();
    void conclusionTiersFollowTheEvidence();
    void ignoredDifferenceIsRuleIdenticalNotIdentical();

    // B 比较侧策略表
    void bomPolicyTableIsClean();
    void bomPolicyTableRejectsBrokenTables();
    void bomPolicyDefaultMatchesCompareOptions();
    void bomPolicyIdentifiersRoundTrip();

    // C BOM 差异判定
    void identicalBomStateYieldsNoVerdict();
    void incompleteObservationYieldsNoVerdict();
    void ignorePolicyNeverCountsABomDifference();
    void treatAsDifferenceAlwaysCountsABomDifference();
    void automaticIgnoresTheRedundantUtf8Bom();
    void automaticCountsTheByteOrderMarks();

    // D 保存侧策略
    void bomSavePolicyTableIsClean();
    void bomSavePolicyTableRejectsBrokenTables();
    void bomSavePolicyAppliesRejectsImpossibleCombinations();
    void bomShouldBeWrittenHonoursEveryPolicy();
    void documentWritesTheBomItsPolicyAsksFor();
    void removingTheBomOfAByteOrderMarkedEncodingIsRefused();

    // E 编码判定
    void utf8BomIsNeverReadAsUtf16_data();
    void utf8BomIsNeverReadAsUtf16();
};

#endif
