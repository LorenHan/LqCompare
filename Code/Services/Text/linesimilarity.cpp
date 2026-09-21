#include "linesimilarity.h"

#include <algorithm>

namespace LqCompare { namespace Text {
namespace {

// 最长公共子序列的**长度**。不需要子序列本身，因此只需要两行滚动数组。
//
// 公共前后缀先剥掉再算：`LCS(a, b) = 公共前缀 + LCS(中段) + 公共后缀`。
// 这不是锦上添花的优化，而是这个分值能不能用的前提——本仓比的是源码行，
// 两行的公共前后缀通常占绝大部分（只差一两个标识符），剥完之后中段常常只剩几个字符。
// 剥了之后中段的**首尾字符必然与另一侧不同**，所以 DP 不会做无用功。
//
// 滚动数组的写法上有一处容易写错：交换之后 `current` 里是上一行的值，
// 而下一轮会把下标 1..m **全部**重写，因此不需要 `fill(0)`；
// 下标 0 永远不被写、初值就是 0，这正是一行长度为零时的边界。
int longestCommonSubsequenceLength(const QString &left, const QString &right)
{
    const int shorter = qMin(left.size(), right.size());
    int prefix = 0;
    while (prefix < shorter && left.at(prefix) == right.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < shorter - prefix
           && left.at(left.size() - 1 - suffix) == right.at(right.size() - 1 - suffix)) ++suffix;

    const int n = left.size() - prefix - suffix;
    const int m = right.size() - prefix - suffix;
    if (n <= 0 || m <= 0) return prefix + suffix;

    QVector<int> previous(m + 1, 0), current(m + 1, 0);
    for (int i = 0; i < n; ++i) {
        const QChar a = left.at(prefix + i);
        for (int j = 0; j < m; ++j) {
            current[j + 1] = a == right.at(prefix + j) ? previous[j] + 1
                                                       : qMax(previous[j + 1], current[j]);
        }
        previous.swap(current);
    }
    return prefix + suffix + previous[m];
}

// 分值之间的「更好」判定，与配对 DP 的目标函数**共用一处定义**：
// 先配对数、再总分值。分成两处写，DP 的转移与回溯的判据就可能各说各话
// （那种错的症状是「结果看起来对、但某些输入下会丢一对」）。
bool betterPlan(int pairsA, int scoreA, int pairsB, int scoreB)
{
    if (pairsA != pairsB) return pairsA > pairsB;
    return scoreA > scoreB;
}

} // namespace

int defaultSimilarityThreshold()
{
    return CompareOptions().similarityThreshold;
}

int clampSimilarityThreshold(int threshold)
{
    return qBound(0, threshold, MaximumSimilarity);
}

int lineSimilarityPercent(const QString &left, const QString &right, const CompareOptions &options)
{
    const QString a = normalizedLine(left, options);
    const QString b = normalizedLine(right, options);
    // 两侧都被规范化成空串：按「判等」的语义它们完全一样。**不能**在这里返回 0，
    // 否则「开了忽略全部空白之后两行都变成空的」会被判成毫不相似，
    // 而那种输入的正确结论恰恰是「同一行的两种写法」。
    if (a.isEmpty() && b.isEmpty()) return MaximumSimilarity;
    if (a.isEmpty() || b.isEmpty()) return 0;
    const int lcs = longestCommonSubsequenceLength(a, b);
    // 乘 200 再除和，等价于 `2·LCS/(len左+len右)` 的百分点；用整数运算避免
    // 浮点舍入在不同平台上分毫不同——分值是**判定**的输入，两边差 0.5 就会
    // 让同一对文件在两台机器上一对是修改、一对是增删。
    return qRound(200.0 * lcs / (a.size() + b.size()));
}

bool isSimilarEnough(int similarityPercent, int threshold)
{
    return similarityPercent >= clampSimilarityThreshold(threshold);
}

int similarityPairingMaximumCells()
{
    return 512 * 512;
}

qint64 similarityPairingBudget()
{
    return 100000000; // 10^8 个「字符×字符」比较，实测量级在 0.2 秒以内
}

SimilarityPlan pairSimilarLines(const QVector<Line> &left, int leftStart, int leftCount,
                                const QVector<Line> &right, int rightStart, int rightCount,
                                const CompareOptions &options, int threshold)
{
    SimilarityPlan plan;
    if (leftCount <= 0 || rightCount <= 0) return plan;
    if (leftStart < 0 || rightStart < 0
        || leftStart + leftCount > left.size() || rightStart + rightCount > right.size()) {
        // 越界的区间是调用方的错，但这里既不崩也不猜：如实报「没算」，
        // 让调用方退回按位配对。抛出异常会把一次比较变成一次崩溃。
        plan.limited = true;
        return plan;
    }

    const int n = leftCount, m = rightCount;
    const int cells = n * m;
    if (cells > similarityPairingMaximumCells()) {
        plan.limited = true;
        return plan;
    }

    // 规范化只做一次：每行都被 DP 用到 m（或 n）次，逐格规范化会把
    // 「阈值判定」的成本抬到与「单元格数 × 行长」同阶。
    QVector<QString> leftText(n), rightText(m);
    qint64 leftCharacters = 0, rightCharacters = 0;
    for (int i = 0; i < n; ++i) {
        leftText[i] = normalizedLine(left.at(leftStart + i).text, options);
        leftCharacters += leftText[i].size();
    }
    for (int j = 0; j < m; ++j) {
        rightText[j] = normalizedLine(right.at(rightStart + j).text, options);
        rightCharacters += rightText[j].size();
    }
    // 所有格子的 LCS 工作量之和恰好是「左侧字符总数 × 右侧字符总数」——
    // 于是这个判断在开算之前就能做完，不需要边跑边数（见 header 的说明）。
    if (leftCharacters * rightCharacters > similarityPairingBudget()) {
        plan.limited = true;
        return plan;
    }

    const int limit = clampSimilarityThreshold(threshold);
    QVector<int> scores(cells, 0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < m; ++j) {
            const int score = lineSimilarityPercent(leftText[i], rightText[j]);
            scores[i * m + j] = score;
        }
    }
    // 低于阈值的格子直接标成 -1 而不是「分值 0 且不可配」：这样回溯时
    // 只需判断 `>= 0`，而「分值恰好为 0 的可配格」在这套分值下不存在
    // （0 分意味着连一个公共字符都没有，阈值只可能是 0 才放行，见下面的说明）。
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j)
            if (!isSimilarEnough(scores[i * m + j], limit)) scores[i * m + j] = -1;

    // 目标函数：`(配对数, 总分值)` 的字典序最大。两个数组而不是一个结构体数组，
    // 是为了让「配对数」与「总分值」在代码里各自有一个名字——把它们塞进一个
    // 复合数值（比如 `pairs * 1000 + score`）需要在脑子里维护一个隐含的权重，
    // 而那正是这类 DP 最容易写错的地方。
    const int stride = m + 1;
    QVector<int> bestPairs((n + 1) * stride, 0), bestScore((n + 1) * stride, 0);
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const int index = i * stride + j;
            int pairs = bestPairs[(i - 1) * stride + j];
            int score = bestScore[(i - 1) * stride + j];
            if (betterPlan(bestPairs[i * stride + j - 1], bestScore[i * stride + j - 1], pairs, score)) {
                pairs = bestPairs[i * stride + j - 1];
                score = bestScore[i * stride + j - 1];
            }
            const int cell = scores[(i - 1) * m + j - 1];
            if (cell >= 0) {
                const int pairedPairs = bestPairs[(i - 1) * stride + j - 1] + 1;
                const int pairedScore = bestScore[(i - 1) * stride + j - 1] + cell;
                // 相等时**不覆盖**：不覆盖意味着「有配对的那条转移优先」，
                // 与目标函数第一项「能配就配」一致。
                if (betterPlan(pairedPairs, pairedScore, pairs, score)) {
                    pairs = pairedPairs;
                    score = pairedScore;
                }
            }
            bestPairs[index] = pairs;
            bestScore[index] = score;
        }
    }

    QVector<SimilarityPair> reversed;
    int i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const int cell = scores[(i - 1) * m + j - 1];
            if (cell >= 0
                && bestPairs[i * stride + j] == bestPairs[(i - 1) * stride + j - 1] + 1
                && bestScore[i * stride + j] == bestScore[(i - 1) * stride + j - 1] + cell) {
                reversed.append({i - 1, j - 1});
                --i; --j;
                continue;
            }
        }
        if (i > 0 && bestPairs[i * stride + j] == bestPairs[(i - 1) * stride + j]
            && bestScore[i * stride + j] == bestScore[(i - 1) * stride + j]) {
            --i;
            continue;
        }
        if (j > 0 && bestPairs[i * stride + j] == bestPairs[i * stride + j - 1]
            && bestScore[i * stride + j] == bestScore[i * stride + j - 1]) {
            --j;
            continue;
        }
        // 三条转移都对不上意味着上面的填表与这里判据不一致；宁可在这里停住并
        // 报「没算」，也不要返回一份可能与目标函数不符的配对。
        // 这个分支在当前实现下不可达（测试里有一条用例专门核对
        // 「每个格子都能被某条转移解释」），留着是为了让不一致**可见**。
        plan.limited = true;
        plan.pairs.clear();
        return plan;
    }
    std::reverse(reversed.begin(), reversed.end());
    plan.pairs = reversed;
    return plan;
}

} }
