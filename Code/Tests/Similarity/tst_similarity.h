#ifndef LQCOMPARE_TST_SIMILARITY_H
#define LQCOMPARE_TST_SIMILARITY_H

#include <QObject>
#include <QtTest>

///
/// \brief 相似度分值与「相似行对齐」的测试（PRD: TXT-005）。
///
/// 分组对齐规格的四条完成标准：
///   A 分值          —— 标准第 1 条（阈值可配置 0–100 的那把尺子本身）
///   B 阈值判定      —— 标准第 2 条前半（低于阈值不配对的边界）
///   C 单调配对      —— 标准第 2 条后半（固定语料上的对应关系与无丢行）
///   D 总开关        —— 标准第 3 条（关闭后相似行呈现为删除 + 新增两条独立块）
///   E 出厂默认值    —— 标准第 2 条「默认值必须经过固定语料验证」
///   F 一处改动的归并 —— TXT-005 的连带契约：引擎块数不再等于改动处数，
///                      凡是「按处」计数或操作的地方（这里验的是归并本身，
///                      界面与命令行两侧的接线分别在 Tests/TextView 与 Tests/Cli）
///
/// 标准第 4 条（阈值与开关写入会话设置并可用于命令行）的用例**不在这里**：
/// 会话键的往返落在 `Tests/TextView`（`text.*` 键那一组），命令行解析落在
/// `Tests/Cli`（`--similar-lines` / `--no-similar-lines` / `--similarity-threshold`）。
/// 分开是刻意的——本套件刻意**不链接 QtGui**（见 SimilarityTests.pro），
/// 一旦有人把这个纯逻辑模块接上界面依赖，本工程会立刻构建失败。
///
class TstSimilarity : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // A 分值
    void similarityIsBoundedSymmetricAndExactOnEqualInput();
    void similarityIsCaseSensitiveUnlessTheOptionSaysOtherwise();
    void similarityFollowsTheWhitespaceChain();

    // B 阈值判定
    void thresholdIsInclusiveAtItsOwnValue();
    void thresholdClampsInsteadOfFallingBack();

    // C 单调配对
    void pairingDropsCandidatePairsBelowTheThreshold();
    void pairingIsMonotoneAndKeepsEveryLine();
    void pairingPrefersMorePairsThenHigherScore();

    // D 总开关
    void disabledSwitchTurnsASimilarPairIntoDeleteAndInsert();
    void enabledSwitchKeepsASimilarPairAsOneModificationBlock();

    // E 出厂默认值
    void defaultThresholdSeparatesRewritesFromUnrelatedLines();

    // F 一处改动的归并
    void differenceRunsMergeAdjacentBlocksOnly();
    void oversizedChangeFallsBackToPositionalPairingAndSaysSo();
};

#endif
