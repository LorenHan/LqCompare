#include "textdiff.h"

#include <QHash>
#include <QSet>
#include <algorithm>

namespace LqCompare { namespace Text {
namespace {
QString normalized(QString text, const CompareOptions &options)
{
    if (options.ignoreCase) text = text.toCaseFolded();
    if (options.whitespace == Whitespace::IgnoreChanges) return text.simplified();
    if (options.whitespace == Whitespace::IgnoreAll) {
        QString compact;
        compact.reserve(text.size());
        for (const QChar ch : text) if (!ch.isSpace()) compact += ch;
        return compact;
    }
    return text;
}

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
        result.append(normalized(lines[i].text, options) + QChar(0) + QChar(ending));
    }
    return result;
}

struct Range { int a; int n; int b; int m; bool equal; };
class Myers {
public:
    const QVector<QString> &a;
    const QVector<QString> &b;
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
        for (int d = 0; d <= maxD && budget > 0; ++d) {
            for (int k = -d + fStart; k <= d - fEnd; k += 2) {
                if (--budget <= 0) break;
                const int index = offset + k;
                int x = k == -d || (k != d && forward[index - 1] < forward[index + 1])
                    ? forward[index + 1] : forward[index - 1] + 1;
                int y = x - k;
                while (x < n && y < m && x >= 0 && y >= 0 && a[ai + x] == b[bi + y]) {
                    ++x; ++y; --budget;
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
                if (--budget <= 0) break;
                const int index = offset + k;
                int x = k == -d || (k != d && reverse[index - 1] < reverse[index + 1])
                    ? reverse[index + 1] : reverse[index - 1] + 1;
                int y = x - k;
                while (x < n && y < m && x >= 0 && y >= 0
                       && a[ai + n - x - 1] == b[bi + m - y - 1]) {
                    ++x; ++y; --budget;
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
        append(ai, prefix, bi, prefix, true);
        ai += prefix; bi += prefix; n -= prefix; m -= prefix;
        int suffix = 0;
        while (suffix < n && suffix < m && a[ai + n - suffix - 1] == b[bi + m - suffix - 1]) ++suffix;
        const int nn = n - suffix, mm = m - suffix;
        if (!nn || !mm) append(ai, nn, bi, mm, false);
        else if (depth >= 96 || budget <= 0) {
            limited = true;
            append(ai, nn, bi, mm, false);
        } else {
            // Avoid quadratic work for completely unrelated files/ranges.
            QSet<QString> common;
            for (int i = 0; i < nn; ++i) common.insert(a[ai + i]);
            bool any = false;
            for (int i = 0; i < mm && !any; ++i) any = common.contains(b[bi + i]);
            common.clear();
            common.squeeze();
            if (!any) append(ai, nn, bi, mm, false);
            else {
                const auto split = bisect(ai, nn, bi, mm);
                if (split.first < 0 || (split.first == 0 && split.second == 0)
                    || (split.first == nn && split.second == mm)) {
                    limited = true;
                    append(ai, nn, bi, mm, false);
                } else {
                    run(ai, split.first, bi, split.second, depth + 1);
                    run(ai + split.first, nn - split.first, bi + split.second, mm - split.second, depth + 1);
                }
            }
        }
        append(ai + nn, suffix, bi + mm, suffix, true);
    }
};
}

Result compare(const QVector<Line> &left, const QVector<Line> &right, const CompareOptions &options)
{
    const auto a = keys(left, right, options);
    const auto b = keys(right, left, options);
    Myers myers{a, b, {}};
    myers.run(0, a.size(), 0, b.size());
    Result result;
    result.alignmentLimited = myers.limited;
    auto addBlock = [&](Change change, int ai, int n, int bi, int m) {
        if (!result.blocks.isEmpty() && result.blocks.last().change == change) {
            Block &last = result.blocks.last();
            last.leftCount += n;
            last.rightCount += m;
        } else result.blocks.append({change, ai, n, bi, m, 0, 0});
    };
    for (const Range &range : myers.ranges) {
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

} }
