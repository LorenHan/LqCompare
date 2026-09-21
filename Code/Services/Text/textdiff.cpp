#include "textdiff.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <algorithm>

namespace LqCompare { namespace Text {
namespace {
QVector<QString> keys(const QVector<Line> &lines, const QVector<Line> &other,
                      const CompareOptions &options)
{
    QVector<QString> result;
    result.reserve(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        Eol eol = lines[i].eol;
        if (options.ignoreFinalNewline && i + 1 == lines.size() && eol == Eol::None
            && !other.isEmpty()) eol = other.last().eol;
        // Absence of a final newline is independent of the LF/CRLF/CR rule.
        int ending = static_cast<int>(eol);
        if (options.ignoreEol && ending) ending = 1;
        result.append(normalizedLine(lines[i].text, options) + QChar(0) + QChar(ending));
    }
    return result;
}

struct Range { int a; int n; int b; int m; bool equal; };

// 对齐区间的收集器。Myers 与 Patience **共用**它，一起把结论追加到同一份区间表里。
//
// 共用不是为了少写几行，而是为了让「切换算法不改变视图契约」在结构上成立：
// 两者产出的区间在同一个容器里按同一条规则合并，然后由同一个收尾循环翻译成
// blocks / rows。若两种算法各攒各的区间再拼起来，块边界会在拼接处出现两套口径。
//
// 预算与「受限」标记也放在这里。Patience 的锚点之间会嵌 Myers 回退，
// 若两边各记一份预算，同一份输入在两种算法下的 `alignmentLimited` 就会不一致——
// 而那是界面判断要不要提示「只做了粗略对齐」的唯一依据。
class AlignmentBuilder
{
public:
    QVector<Range> ranges;
    qint64 budget = 12000000;
    bool limited = false;

    void append(int ai, int n, int bi, int m, bool equal)
    {
        if (!n && !m) return;
        if (!ranges.isEmpty() && ranges.last().equal == equal) {
            ranges.last().n += n;
            ranges.last().m += m;
        } else ranges.append({ai, n, bi, m, equal});
    }
};

class Myers
{
public:
    const QVector<QString> &a;
    const QVector<QString> &b;
    AlignmentBuilder *out;

    // Return the meeting point of forward/reverse furthest-reaching paths.
    // The vectors die before recursive calls, keeping peak memory linear.
    QPair<int, int> bisect(int ai, int n, int bi, int m)
    {
        const int maxD = (n + m + 1) / 2;
        const int offset = maxD + 1;
        QVector<int> forward(2 * maxD + 3, -1), reverse(2 * maxD + 3, -1);
        forward[offset + 1] = reverse[offset + 1] = 0;
        const int delta = n - m;
        const bool odd = (delta % 2 != 0);
        int fStart = 0, fEnd = 0, rStart = 0, rEnd = 0;
        for (int d = 0; d <= maxD && out->budget > 0; ++d) {
            for (int k = -d + fStart; k <= d - fEnd; k += 2) {
                if (--out->budget <= 0) break;
                const int index = offset + k;
                int x = k == -d || (k != d && forward[index - 1] < forward[index + 1])
                    ? forward[index + 1] : forward[index - 1] + 1;
                int y = x - k;
                while (x < n && y < m && x >= 0 && y >= 0 && a[ai + x] == b[bi + y]) {
                    ++x; ++y; --out->budget;
                }
                forward[index] = x;
                if (x > n) fEnd += 2;
                else if (y > m) fStart += 2;
                else if (odd) {
                    const int reverseIndex = offset + delta - k;
                    if (reverseIndex >= 0 && reverseIndex < reverse.size()
                        && reverse[reverseIndex] != -1 && x >= n - reverse[reverseIndex])
                        return {x, y};
                }
            }
            for (int k = -d + rStart; k <= d - rEnd; k += 2) {
                if (--out->budget <= 0) break;
                const int index = offset + k;
                int x = k == -d || (k != d && reverse[index - 1] < reverse[index + 1])
                    ? reverse[index + 1] : reverse[index - 1] + 1;
                int y = x - k;
                while (x < n && y < m && x >= 0 && y >= 0
                       && a[ai + n - x - 1] == b[bi + m - y - 1]) {
                    ++x; ++y; --out->budget;
                }
                reverse[index] = x;
                if (x > n) rEnd += 2;
                else if (y > m) rStart += 2;
                else if (!odd) {
                    const int forwardIndex = offset + delta - k;
                    if (forwardIndex >= 0 && forwardIndex < forward.size()
                        && forward[forwardIndex] != -1) {
                        const int fx = forward[forwardIndex];
                        const int fy = fx - (delta - k);
                        if (fx >= n - x) return {fx, fy};
                    }
                }
            }
        }
        return {-1, -1};
    }

    void run(int ai, int n, int bi, int m, int depth = 0)
    {
        int prefix = 0;
        while (prefix < n && prefix < m && a[ai + prefix] == b[bi + prefix]) ++prefix;
        out->append(ai, prefix, bi, prefix, true);
        ai += prefix; bi += prefix; n -= prefix; m -= prefix;
        int suffix = 0;
        while (suffix < n && suffix < m && a[ai + n - suffix - 1] == b[bi + m - suffix - 1]) ++suffix;
        const int nn = n - suffix, mm = m - suffix;
        if (!nn || !mm) out->append(ai, nn, bi, mm, false);
        else if (depth >= 96 || out->budget <= 0) {
            out->limited = true;
            out->append(ai, nn, bi, mm, false);
        } else {
            // Avoid quadratic work for completely unrelated files/ranges.
            QSet<QString> common;
            for (int i = 0; i < nn; ++i) common.insert(a[ai + i]);
            bool any = false;
            for (int i = 0; i < mm && !any; ++i) any = common.contains(b[bi + i]);
            common.clear();
            common.squeeze();
            if (!any) out->append(ai, nn, bi, mm, false);
            else {
                const auto split = bisect(ai, nn, bi, mm);
                if (split.first < 0 || (split.first == 0 && split.second == 0)
                    || (split.first == nn && split.second == mm)) {
                    out->limited = true;
                    out->append(ai, nn, bi, mm, false);
                } else {
                    run(ai, split.first, bi, split.second, depth + 1);
                    run(ai + split.first, nn - split.first, bi + split.second, mm - split.second, depth + 1);
                }
            }
        }
        out->append(ai + nn, suffix, bi + mm, suffix, true);
    }
};

// 严格递增的最长子序列，返回**下标**（Patience 挑选锚点的那一步）。
//
// 为什么不用更短的写法：
//  * 按值排序 / 非严格递增的比较会把同一个右侧位置用两次，于是「一个锚点」变成
//    「两条边指向同一行」，收尾时行覆盖会出现重叠；
//  * 用 `QSet`/`QHash` 的迭代顺序来打破并列，会让同一份输入两次跑出不同的块边界。
//    本仓已经吃过一次「两个不同的量在夹具上恰好重合」的亏——不确定性比错误更难查，
//    因为它只在某些样本上出现，而回归测试恰好不覆盖那些样本。
// 因此这里用标准的 patience sorting（`tails[k]` 记录长度为 k+1 的候选子序列里
// 结尾最小的那个下标），比较写成 `<` 以保证严格递增。
//
// 补一句「为什么这里的严格性**当前**推不出差别」：候选的右侧下标天然互不相同
// ——每个候选来自一个两侧都唯一的键，而唯一键在右侧只出现一次，两个不同的键
// 不可能落在同一行。所以把 `<` 改成 `<=` 在这份实现里是等价变异，跑不出来。
// 仍然写 `<`：这条不变量由**调用方**（`uniqueAnchors` 的计数判定）保证，
// 上层一旦放宽，非严格的最长「递增」子序列会把同一个右侧位置用两次，
// 而锚点循环是按「右下标严格递增」推进的，重复取值会让下一段的右侧长度算成负数。
// 反向验证时如果这一处报「漏检」，先怀疑是不是上层被改宽了，不要先改这里。
QVector<int> longestStrictlyIncreasing(const QVector<int> &values)
{
    QVector<int> tails;
    QVector<int> previous(values.size(), -1);
    for (int i = 0; i < values.size(); ++i) {
        int low = 0, high = tails.size();
        while (low < high) {
            const int mid = low + (high - low) / 2;
            if (values[tails[mid]] < values[i]) low = mid + 1;
            else high = mid;
        }
        previous[i] = low > 0 ? tails[low - 1] : -1;
        if (low == tails.size()) tails.append(i);
        else tails[low] = i;
    }
    QVector<int> result(tails.size());
    int cursor = tails.isEmpty() ? -1 : tails.last();
    for (int k = tails.size() - 1; k >= 0; --k) {
        result[k] = cursor;
        cursor = previous[cursor];
    }
    return result;
}

// ── Patience（TXT-003）─────────────────────────────────────────────────────
//
// 为什么值得单独一套：Myers 求的是「最短编辑脚本」，它对**重复行**没有偏好。
// 于是一份「把某个函数搬到文件另一处」的改动，Myers 会在几十个 `}` / `}` / 空行里
// 挑一组最省事的配对，界面上表现为「一大片毫不相干的代码被标成改了」。
// Patience 先把**两侧都只出现一次**的行配起来当锚点——唯一行不会配错，
// 它没有同一个候选的第二个副本——锚点之间再递归。
//
// 关键取舍一：**锚点之间仍然递归走 Patience，而那一段没有唯一行时就交给 Myers**。
// 回退不是异常路径而是常规路径：真实文件里「唯一行」可能只占少数（压缩过的 JSON、
// 日志、生成代码、大段重复的表格行），若把「没有唯一行」当成「没有对齐」，
// 这些文件反而比 Myers 更差——会退化成一堆零匹配的大替换块。完成标准第 2 条
// 「平滑回退到 Myers，而不是退化为零匹配」说的就是这件事。
//
// 关键取舍二：**回归深度到顶时也交给 Myers，而不是产出一个大替换块**。
// Myers 自己有深度上限（96）与预算兜底，它的最坏情况是有界的；直接产出替换块
// 不会更快，只会更不准。因此这里没有「patience 自己宣布受限」的分支，
// `alignmentLimited` 永远由 Myers 的预算或深度决定——只有一个来源，不会互相矛盾。
//
// 关键取舍三：**唯一行靠「计数 == 1」判定，不靠「文本出现过一次」**。
// 计数用规范化后的键（`keys()` 已经按 ignoreCase / whitespace / EOL 全部处理过），
// 因此「忽略大小写后变成唯一行」与「不忽略时是重复行」两种情形都能正确归位，
// 不需要在本文件里再解一遍选项。
class Patience
{
public:
    const QVector<QString> &a;
    const QVector<QString> &b;
    AlignmentBuilder *out;

    // 锚点递归的深度上限。它不是防御性代码：恶意输入可以让每一层只剥离一两条唯一行，
    // 那时递归深度就与行数同阶、栈深也跟着同阶。到顶交给 Myers（见上面取舍二）。
    static constexpr int MaximumDepth = 64;

    // 返回本段里「两侧都只出现一次、且右侧下标递增」的锚点（左侧下标、右侧下标）。
    QVector<QPair<int, int>> uniqueAnchors(int ai, int n, int bi, int m) const
    {
        QHash<QString, int> countA, countB, indexB;
        countA.reserve(n);
        countB.reserve(m);
        indexB.reserve(m);
        for (int i = 0; i < n; ++i) ++countA[a[ai + i]];
        for (int j = 0; j < m; ++j) {
            ++countB[b[bi + j]];
            // 只用来给「唯一」的键定位，因此重复键留下的是最后一次出现的位置——
            // 但重复键根本不会被查（下面先判计数），所以这个值不会被用到。
            indexB.insert(b[bi + j], j);
        }
        // 按左侧顺序收集候选，于是「左侧下标递增」天然成立，LIS 只需要管右侧。
        QVector<int> candidateLeft, candidateRight;
        for (int i = 0; i < n; ++i) {
            const QString &key = a[ai + i];
            if (countA.value(key) != 1 || countB.value(key) != 1) continue;
            candidateLeft.append(i);
            candidateRight.append(indexB.value(key));
        }
        if (candidateRight.isEmpty()) return {};
        const QVector<int> picked = longestStrictlyIncreasing(candidateRight);
        QVector<QPair<int, int>> anchors;
        anchors.reserve(picked.size());
        for (int index : picked)
            anchors.append({ai + candidateLeft[index], bi + candidateRight[index]});
        return anchors;
    }

    void run(int ai, int n, int bi, int m, int depth = 0)
    {
        int prefix = 0;
        while (prefix < n && prefix < m && a[ai + prefix] == b[bi + prefix]) ++prefix;
        out->append(ai, prefix, bi, prefix, true);
        ai += prefix; bi += prefix; n -= prefix; m -= prefix;
        int suffix = 0;
        while (suffix < n && suffix < m && a[ai + n - suffix - 1] == b[bi + m - suffix - 1]) ++suffix;
        const int nn = n - suffix, mm = m - suffix;
        if (!nn || !mm) out->append(ai, nn, bi, mm, false);
        else if (depth >= MaximumDepth) {
            Myers{a, b, out}.run(ai, nn, bi, mm);
        } else {
            const QVector<QPair<int, int>> anchors = uniqueAnchors(ai, nn, bi, mm);
            if (anchors.isEmpty()) {
                Myers{a, b, out}.run(ai, nn, bi, mm);
            } else {
                int pa = ai, pb = bi;
                for (const QPair<int, int> &anchor : anchors) {
                    run(pa, anchor.first - pa, pb, anchor.second - pb, depth + 1);
                    out->append(anchor.first, 1, anchor.second, 1, true);
                    pa = anchor.first + 1;
                    pb = anchor.second + 1;
                }
                run(pa, ai + nn - pa, pb, bi + mm - pb, depth + 1);
            }
        }
        out->append(ai + nn, suffix, bi + mm, suffix, true);
    }
};

// 表里具名的那一条是不是「已实现」。`availableAlignments()` 与 `compare()` 的取值
// 兜底都用它，避免两处各写一遍过滤条件（写歪一处就会出现「下拉里没有它、
// 但传进来照样算」这种半可选状态）。
bool hasImplementedDescriptor(const QVector<AlignmentDescriptor> &table, Alignment alignment)
{
    for (const AlignmentDescriptor &descriptor : table)
        if (descriptor.alignment == alignment) return descriptor.implemented;
    return false;
}
}

// 规范化链本体。**只此一份**：`keys()` 走它，测试也走它，
// 因此「链里有几步、按什么顺序」这件事不存在第二个说法。
//
// 顺序上先做大小写折叠、再做空白处理。这两步**可交换**（折叠既不会造出空白、
// 也不会吃掉空白），所以这个顺序不是契约的一部分——把它调换不会、也不该让任何用例变红。
// 真正是契约的是「两步都做、两侧都做、做完才判等」，那三条各有用例守着。
QString normalizedLine(const QString &text, const CompareOptions &options)
{
    QString result = text;
    if (options.ignoreCase) result = result.toCaseFolded();
    if (options.whitespace == Whitespace::IgnoreChanges) return result.simplified();
    if (options.whitespace == Whitespace::IgnoreAll) {
        QString compact;
        compact.reserve(result.size());
        for (const QChar ch : result) if (!ch.isSpace()) compact += ch;
        return compact;
    }
    return result;
}

Result compare(const QVector<Line> &left, const QVector<Line> &right, const CompareOptions &options)
{
    const auto a = keys(left, right, options);
    const auto b = keys(right, left, options);
    // `Alignment` 是 enum class，但取值可能来自会话文件 / 命令行 / 设置仓库，
    // 那些地方存的是整数，读回来可能落在枚举之外。落下界的取值必须退到默认算法，
    // **不能**落进「什么都不跑」的分支——那会返回一个零块零行的 `Result`，
    // 在界面上表现为「两份文件完全一样」，是最危险的一种错。
    Alignment selected = options.alignment;
    if (!hasImplementedDescriptor(alignmentTable(), selected)) selected = defaultAlignment();
    AlignmentBuilder builder;
    switch (selected) {
    case Alignment::Myers:
        Myers{a, b, &builder}.run(0, a.size(), 0, b.size());
        break;
    case Alignment::Patience:
        Patience{a, b, &builder}.run(0, a.size(), 0, b.size());
        break;
    }
    Result result;
    result.alignmentLimited = builder.limited;
    auto addBlock = [&](Change change, int ai, int n, int bi, int m) {
        if (!result.blocks.isEmpty() && result.blocks.last().change == change) {
            Block &last = result.blocks.last();
            last.leftCount += n;
            last.rightCount += m;
        } else result.blocks.append({change, ai, n, bi, m, 0, 0});
    };
    for (const Range &range : builder.ranges) {
        if (range.equal) {
            for (int i = 0; i < range.n; ++i)
                addBlock(left[range.a + i] == right[range.b + i] ? Change::Equal : Change::Ignored,
                         range.a + i, 1, range.b + i, 1);
        } else {
            addBlock(!range.n ? Change::Insert : (!range.m ? Change::Delete : Change::Replace),
                     range.a, range.n, range.b, range.m);
        }
    }
    for (int index = 0; index < result.blocks.size(); ++index) {
        Block &block = result.blocks[index];
        block.firstRow = result.rows.size();
        block.rowCount = std::max(block.leftCount, block.rightCount);
        if (block.change == Change::Ignored) ++result.ignoredBlocks;
        else if (block.change != Change::Equal) result.differences.append(index);
        for (int i = 0; i < block.rowCount; ++i)
            result.rows.append({i < block.leftCount ? block.leftStart + i : -1,
                                i < block.rightCount ? block.rightStart + i : -1, index, block.change});
    }
    return result;
}

// -----------------------------------------------------------------------------
// 已实现的对齐算法清单
// -----------------------------------------------------------------------------

const QVector<AlignmentDescriptor> &alignmentTable()
{
    // 顺序即 `availableAlignments()` 的顺序。Myers 排第一位不是因为它更好，
    // 而是因为它是历史行为、也是 Patience 的回退底座——默认值取「第一条已实现的
    // 条目」，把底座排在后面会让默认值随表的顺序漂移。
    static const QVector<AlignmentDescriptor> table{
        {Alignment::Myers, "myers", true},
        {Alignment::Patience, "patience", true},
    };
    return table;
}

const char *alignmentIdentifier(Alignment alignment)
{
    for (const AlignmentDescriptor &descriptor : alignmentTable())
        if (descriptor.alignment == alignment) return descriptor.identifier;
    return nullptr;
}

Alignment defaultAlignment(const QVector<AlignmentDescriptor> &table)
{
    for (const AlignmentDescriptor &descriptor : table)
        if (descriptor.implemented) return descriptor.alignment;
    return Alignment::Myers;
}

QVector<Alignment> availableAlignments(const QVector<AlignmentDescriptor> &table)
{
    QVector<Alignment> result;
    result.reserve(table.size());
    for (const AlignmentDescriptor &descriptor : table)
        if (descriptor.implemented) result.append(descriptor.alignment);
    return result;
}

QStringList validateAlignmentTable(const QVector<AlignmentDescriptor> &table,
                                   const QVector<Alignment> &expected)
{
    QStringList problems;
    if (table.isEmpty()) problems << QStringLiteral("对齐算法表为空：一条可选算法都没有");
    QSet<QString> seen;
    for (const AlignmentDescriptor &descriptor : table) {
        const QString identity = QString::fromLatin1(descriptor.identifier);
        if (identity.isEmpty()) {
            problems << QStringLiteral("对齐算法（枚举值 %1）没有标识符，日志里无法区分")
                            .arg(static_cast<int>(descriptor.alignment));
            continue;
        }
        if (seen.contains(identity))
            problems << QStringLiteral("对齐算法标识符重复：%1").arg(identity);
        else seen.insert(identity);
    }
    bool anyImplemented = false;
    for (const AlignmentDescriptor &descriptor : table) anyImplemented |= descriptor.implemented;
    if (!anyImplemented)
        problems << QStringLiteral("没有任何已实现的对齐算法，默认算法将无处可取");
    for (Alignment wanted : expected) {
        const AlignmentDescriptor *found = nullptr;
        for (const AlignmentDescriptor &descriptor : table)
            if (descriptor.alignment == wanted) found = &descriptor;
        if (!found) {
            problems << QStringLiteral("对齐算法（枚举值 %1）没有登记在表里，因此不可选")
                            .arg(static_cast<int>(wanted));
        } else if (!found->implemented) {
            // 登记了却标成未实现，本身不违法（这正是那个标志位的用途：留给还没做的算法）。
            // 但**规格点名要求的**算法标成未实现，就意味着这条规格被悄悄降级——
            // 「清单里少一项」在界面上看不出来任何异常，所以必须被报出来。
            problems << QStringLiteral("规格要求的对齐算法 %1 登记为未实现，不会出现在可选清单里")
                            .arg(QString::fromLatin1(found->identifier));
        }
    }
    return problems;
}

} }
