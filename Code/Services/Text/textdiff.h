#ifndef LQCOMPARE_TEXTDIFF_H
#define LQCOMPARE_TEXTDIFF_H

#include "textdocument.h"

namespace LqCompare { namespace Text {

enum class Change { Equal, Insert, Delete, Replace, Ignored };
enum class Whitespace { Exact, IgnoreChanges, IgnoreAll };
struct CompareOptions {
    bool ignoreCase = false;
    Whitespace whitespace = Whitespace::Exact;
    bool ignoreEol = true;
    bool ignoreFinalNewline = false;
};
struct Block {
    Change change = Change::Equal;
    int leftStart = 0;
    int leftCount = 0;
    int rightStart = 0;
    int rightCount = 0;
    int firstRow = 0;
    int rowCount = 0;
};
struct Row {
    int leftLine = -1;
    int rightLine = -1;
    int block = -1;
    Change change = Change::Equal;
};
struct Result {
    QVector<Block> blocks;
    QVector<Row> rows;
    QVector<int> differences; // indexes into blocks; ignored changes are excluded
    int ignoredBlocks = 0;
    bool alignmentLimited = false;
};

// Deterministic Myers bisect, linear auxiliary space. Adversarial work is bounded;
// the remaining range is an explicit replacement, never silently considered equal.
Result compare(const QVector<Line> &left, const QVector<Line> &right,
               const CompareOptions &options = CompareOptions());

} }
#endif
