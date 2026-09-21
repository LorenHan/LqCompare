#include "mergeengine.h"

#include "textdiff.h"

#include <algorithm>

namespace LqCompare { namespace Merge {
namespace {

struct Edit {
    int start;
    int count;
    int replacementCount;
    bool left;
    int end() const { return start + count; }
};

Text::CompareOptions exactOptions()
{
    Text::CompareOptions options;
    options.ignoreEol = false;
    return options;
}

// 「一处改动」的长度：从 `blocks[index]` 起，连续的、**基点相接**的非 Equal 块
// 属于同一段改写，返回它们一共几个块。
//
// 为什么合并引擎需要这一层：TXT-005 起，差异把一次改写呈现为几个块取决于相似度
// 配对（相似 → 一块 `Replace`；不相似 → 「删除 + 新增」；配对只成立一部分 →
// 三者混排），而三方合并真正需要的语义只有一个——「这段基点被这一侧改写了」。
// 把块粒度直接当语义用，会得到两种错误：
//   ① 「两侧删掉同一行、各插一句不同的话」被读成「双方都删除 + 两处基点为空的新增」，
//      基线行从结果里消失（`corpus(different-replacement)` 守的就是这一条）；
//   ② 一段改写被拆成两块之后，`protectUnterminatedBoundaries()` 会以为这是
//      「EOF 处两侧重叠编辑」而把它们并成一个假的冲突（穷举用例守的是这一条）。
// 判据是「基点相接」而不是块类型：`addBlock` 已经保证删除排在新增之前、且
// 相邻同类块会合并，所以相接就意味着中间没有别的行。
//
// 两个纯插入（`leftCount == 0`）即使基点相同也必须分开：那是两侧各自插入的
// 落点相同，不是同一段改写——它们各自属于不同的一侧，本来也不会进同一个 run，
// 这个判断是留给「同一侧的两个独立插入点」的兜底。
int changeRunLength(const QVector<Text::Block> &blocks, int index)
{
    int end = index + 1;
    while (end < blocks.size()) {
        const Text::Block &previous = blocks[end - 1];
        const Text::Block &next = blocks[end];
        if (next.change == Text::Change::Equal) break;
        if (next.leftStart != previous.leftStart + previous.leftCount) break;
        if (previous.leftCount == 0 && next.leftCount == 0) break;
        ++end;
    }
    return end - index;
}

void appendEdits(const Text::Result &diff, bool left, QVector<Edit> *edits)
{
    for (int index = 0; index < diff.blocks.size(); ) {
        if (diff.blocks[index].change == Text::Change::Equal) { ++index; continue; }
        const int length = changeRunLength(diff.blocks, index);
        int leftCount = 0, rightCount = 0;
        for (int i = 0; i < length; ++i) {
            leftCount += diff.blocks[index + i].leftCount;
            rightCount += diff.blocks[index + i].rightCount;
        }
        edits->append({diff.blocks[index].leftStart, leftCount, rightCount, left});
        index += length;
    }
}

Block sourceBlock(const QVector<Text::Line> &base, int baseStart, int baseCount,
                  const QVector<Text::Line> &left, int leftStart, int leftCount,
                  const QVector<Text::Line> &right, int rightStart, int rightCount)
{
    Block block;
    block.baseStart = baseStart;
    block.baseCount = baseCount;
    block.leftStart = leftStart;
    block.leftCount = leftCount;
    block.rightStart = rightStart;
    block.rightCount = rightCount;
    block.base = base.mid(baseStart, baseCount);
    block.left = left.mid(leftStart, leftCount);
    block.right = right.mid(rightStart, rightCount);
    return block;
}

void protectUnterminatedBoundaries(Result *result)
{
    auto unterminated = [](const Block &block) {
        auto endsWithoutNewline = [](const QVector<Text::Line> &lines) {
            return !lines.isEmpty() && lines.last().eol == Text::Eol::None;
        };
        return endsWithoutNewline(block.base) || endsWithoutNewline(block.left)
            || endsWithoutNewline(block.right) || endsWithoutNewline(block.output);
    };
    QVector<Block> safe;
    for (const auto &block : result->blocks) {
        // A no-newline line can only end the output. Treat a later nonempty
        // block as an overlapping EOF edit: silently adding a separator loses
        // one side's newline decision, while concatenation loses line identity.
        // Check every candidate too, so choosing a side later remains safe.
        // Keep intervening empty-output blocks inside the conflict as well.
        if (!block.output.isEmpty()) {
            int previous = safe.size() - 1;
            while (previous >= 0 && safe[previous].output.isEmpty()
                   && !unterminated(safe[previous])) --previous;
            if (previous >= 0 && unterminated(safe[previous])) {
                Block combined = safe[previous];
                auto appendSources = [&](const Block &part) {
                    combined.baseCount += part.baseCount;
                    combined.leftCount += part.leftCount;
                    combined.rightCount += part.rightCount;
                    combined.base += part.base;
                    combined.left += part.left;
                    combined.right += part.right;
                };
                for (int i = previous + 1; i < safe.size(); ++i) appendSources(safe[i]);
                appendSources(block);
                combined.kind = Kind::Conflict;
                combined.output = combined.base;
                safe.resize(previous);
                safe.append(combined);
                continue;
            }
        }
        safe.append(block);
    }
    result->blocks = safe;
}

} // namespace

int Result::conflictCount() const
{
    return std::count_if(blocks.cbegin(), blocks.cend(), [](const Block &block) {
        return block.kind == Kind::Conflict;
    });
}

QVector<Text::Line> Result::outputLines() const
{
    QVector<Text::Line> lines;
    for (const auto &block : blocks) lines += block.output;
    return lines;
}

Result merge(const QVector<Text::Line> &base, const QVector<Text::Line> &left,
             const QVector<Text::Line> &right)
{
    const auto leftDiff = Text::compare(base, left, exactOptions());
    const auto rightDiff = Text::compare(base, right, exactOptions());
    Result result;
    result.alignmentLimited = leftDiff.alignmentLimited || rightDiff.alignmentLimited;
    QVector<Edit> edits;
    appendEdits(leftDiff, true, &edits);
    appendEdits(rightDiff, false, &edits);
    // An insertion at the start boundary belongs before the replaced lines.
    // Processing it first keeps both source cursors aligned for the next block.
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) {
        if (a.start != b.start) return a.start < b.start;
        return a.count < b.count;
    });

    int baseCursor = 0, leftCursor = 0, rightCursor = 0;
    auto unchangedThrough = [&](int end) {
        const int count = end - baseCursor;
        if (!count) return;
        auto block = sourceBlock(base, baseCursor, count, left, leftCursor, count,
                                 right, rightCursor, count);
        block.output = block.base;
        result.blocks.append(block);
        baseCursor = end;
        leftCursor += count;
        rightCursor += count;
    };

    int next = 0;
    while (next < edits.size()) {
        const int begin = next;
        const int start = edits[next].start;
        int end = edits[next].end();
        ++next;
        // Grow the entire connected overlap component. Strict comparison keeps
        // adjacent replacements and end-boundary insertions independent.
        while (next < edits.size()
               && (edits[next].start < end
                   || (start == end && edits[next].start == start && edits[next].count == 0))) {
            end = std::max(end, edits[next].end());
            ++next;
        }

        unchangedThrough(start);
        int leftCount = end - start, rightCount = end - start;
        bool leftChanged = false, rightChanged = false;
        for (int i = begin; i < next; ++i) {
            const auto &edit = edits[i];
            if (edit.left) {
                leftChanged = true;
                leftCount += edit.replacementCount - edit.count;
            } else {
                rightChanged = true;
                rightCount += edit.replacementCount - edit.count;
            }
        }
        auto block = sourceBlock(base, start, end - start, left, leftCursor, leftCount,
                                 right, rightCursor, rightCount);
        if (!rightChanged) {
            block.kind = Kind::LeftOnly;
            block.output = block.left;
        } else if (!leftChanged) {
            block.kind = Kind::RightOnly;
            block.output = block.right;
        } else if (block.left == block.right) {
            block.kind = Kind::SameChange;
            block.output = block.left;
        } else {
            block.kind = Kind::Conflict;
            block.output = block.base;
        }
        result.blocks.append(block);
        baseCursor = end;
        leftCursor += leftCount;
        rightCursor += rightCount;
    }
    unchangedThrough(base.size());
    protectUnterminatedBoundaries(&result);
    return result;
}

Result mergeWithoutBase(const QVector<Text::Line> &left, const QVector<Text::Line> &right)
{
    const auto diff = Text::compare(left, right, exactOptions());
    Result result;
    result.alignmentLimited = diff.alignmentLimited;
    // 无基线时每一处改动就是一处冲突；这里同样按「基点相接 = 一处改动」归并，
    // 否则用户会在两方比对里看到同一处改写被报成两处冲突（理由见 `changeRunLength()`）。
    for (int index = 0; index < diff.blocks.size(); ) {
        const auto &region = diff.blocks[index];
        if (region.change == Text::Change::Equal) {
            auto block = sourceBlock({}, 0, 0, left, region.leftStart, region.leftCount,
                                     right, region.rightStart, region.rightCount);
            block.kind = Kind::Unchanged;
            block.output = block.left;
            result.blocks.append(block);
            ++index;
            continue;
        }
        const int length = changeRunLength(diff.blocks, index);
        int leftCount = 0, rightCount = 0;
        for (int i = 0; i < length; ++i) {
            leftCount += diff.blocks[index + i].leftCount;
            rightCount += diff.blocks[index + i].rightCount;
        }
        auto block = sourceBlock({}, 0, 0, left, region.leftStart, leftCount,
                                 right, region.rightStart, rightCount);
        block.kind = Kind::Conflict;
        result.blocks.append(block);
        index += length;
    }
    protectUnterminatedBoundaries(&result);
    return result;
}

} }
