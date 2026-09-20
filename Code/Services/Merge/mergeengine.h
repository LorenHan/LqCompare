#ifndef LQCOMPARE_MERGEENGINE_H
#define LQCOMPARE_MERGEENGINE_H

#include "textdocument.h"

namespace LqCompare { namespace Merge {

enum class Kind { Unchanged, LeftOnly, RightOnly, SameChange, Conflict };
enum class Resolution { Unresolved, Left, Right, LeftThenRight, RightThenLeft, Base, Manual };

struct Block {
    Kind kind = Kind::Unchanged;
    // Zero-based source ranges. Insertions and deletions may have zero counts.
    int baseStart = 0;
    int baseCount = 0;
    int leftStart = 0;
    int leftCount = 0;
    int rightStart = 0;
    int rightCount = 0;
    QVector<Text::Line> base;
    QVector<Text::Line> left;
    QVector<Text::Line> right;
    // A conflict initially contains base, while both candidates remain above.
    QVector<Text::Line> output;
};

struct Result {
    QVector<Block> blocks;
    // At least one diff exhausted its work limit; unresolved ranges remain
    // explicit changes and can therefore produce conservative conflicts.
    bool alignmentLimited = false;
    int conflictCount() const;
    QVector<Text::Line> outputLines() const;
};

// Exact text AND line-ending comparison, independent of display ignore rules.
// Disjoint edits merge; different same-point insertions or strictly overlapping
// edits conflict. Insertions at the boundary of a replacement remain disjoint.
// Removing the final terminator while the other side appends also conflicts:
// automatic output never invents a terminator or joins two separate lines.
Result merge(const QVector<Text::Line> &base, const QVector<Text::Line> &left,
             const QVector<Text::Line> &right);

// Without an ancestor, every unequal region needs an explicit decision.
// Its base range and initial conflict output are empty.
Result mergeWithoutBase(const QVector<Text::Line> &left, const QVector<Text::Line> &right);

} }
#endif
