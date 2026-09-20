#ifndef LQCOMPARE_VERSIONCOMPARE_H
#define LQCOMPARE_VERSIONCOMPARE_H
#include "versioninfo.h"
namespace LqCompare { namespace Version {
enum class Relation { Equal, LeftHigher, RightHigher, Incomparable };
struct CompareOptions {
    bool ignoreVersionNumbers = false;
    bool padMissingVersionSegments = true;
    bool allowLeadingV = true;
};
// Decimal segments only; optional v/V prefix; suffixes are explicitly incomparable.
// Segments are compared without integer conversion, so large segments do not overflow.
Relation compareNumbers(const QString &left, const QString &right,
                        const CompareOptions &options = CompareOptions());
QString relationText(Relation relation);
enum class Difference { Equal, Changed, LeftOnly, RightOnly, Ignored };
struct Row {
    QString group, key, left, right;
    Difference difference = Difference::Equal;
    Relation relation = Relation::Incomparable;
    bool versionNumber = false;
};
QVector<Row> compare(const FileInfo &left, const FileInfo &right,
                     const CompareOptions &options = CompareOptions());
QString toCsv(const QVector<Row> &rows);
} }
#endif
