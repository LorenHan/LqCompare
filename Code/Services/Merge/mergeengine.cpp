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

void appendEdits(const Text::Result &diff, bool left, QVector<Edit> *edits)
{
    for (const auto &block : diff.blocks) {
        if (block.change != Text::Change::Equal)
            edits->append({block.leftStart, block.leftCount, block.rightCount, left});
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
    for (const auto &region : diff.blocks) {
        auto block = sourceBlock({}, 0, 0, left, region.leftStart, region.leftCount,
                                 right, region.rightStart, region.rightCount);
        if (region.change == Text::Change::Equal) {
            block.kind = Kind::Unchanged;
            block.output = block.left;
        } else {
            block.kind = Kind::Conflict;
        }
        result.blocks.append(block);
    }
    protectUnterminatedBoundaries(&result);
    return result;
}

} }
