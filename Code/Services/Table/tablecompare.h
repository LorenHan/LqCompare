#ifndef LQCOMPARE_TABLE_TABLECOMPARE_H
#define LQCOMPARE_TABLE_TABLECOMPARE_H

#include "tabledocument.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare::Table {

enum class ColumnMappingMode { AutoName, Position, Explicit };
enum class RowAlignment { Content, Position, Key };
enum class ColumnRole { Compare, Key, Ignore, Display };
enum class CellStatus { Equal, Different, LeftOnly, RightOnly, Ignored, NotCompared };
enum class RowStatus { Equal, Different, LeftOnly, RightOnly };

struct ColumnRule
{
    int left = -1;
    int right = -1;
    ColumnRole role = ColumnRole::Compare;
    bool numeric = false;
    double absoluteTolerance = 0;
    double relativeTolerance = 0;
    bool compareFormatting = false;
};

struct CompareOptions
{
    ColumnMappingMode columnMode = ColumnMappingMode::AutoName;
    RowAlignment alignment = RowAlignment::Content;
    // Used only with Explicit mapping; omitted columns are appended as Display.
    QVector<ColumnRule> columns;
    double minimumSimilarity = 0.5;
};

struct Row
{
    int left = -1;
    int right = -1;
    RowStatus status = RowStatus::Equal;
    QVector<CellStatus> cells;
    bool duplicateKey = false;
};

struct Statistics
{
    int equalRows = 0;
    int differentRows = 0;
    int leftOnlyRows = 0;
    int rightOnlyRows = 0;
    // Counts Different/LeftOnly/RightOnly cells in participating columns.
    int differentCells = 0;
    // Counts Ignore column slots in result rows, including one-sided rows.
    int ignoredCells = 0;
};

struct Result
{
    QVector<ColumnRule> columns;
    QVector<Row> rows;
    QVector<int> differences;
    Statistics statistics;
    QStringList warnings;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// Every input row occurs exactly once in a successful result. Missing cells are
// distinct from present empty strings. Inputs and source order are not modified.
Result compare(const Document &left, const Document &right,
               const CompareOptions &options = {});
QString rowStatusLabel(RowStatus status);

} // namespace LqCompare::Table

#endif
