#ifndef LQCOMPARE_MEDIAMETADATA_H
#define LQCOMPARE_MEDIAMETADATA_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare { namespace Media {

enum class ReadStatus { Ready, Partial, Unsupported, Malformed, IoError, LimitExceeded };

// Caps apply before allocation or iteration. Media payload is never decoded or read in full.
struct ReadLimits {
    qint64 maxTagBytes = 8 * 1024 * 1024;
    int maxFieldBytes = 256 * 1024;
    int maxFields = 4096;
    int maxMetadataBlocks = 256;
};

struct Document {
    QString path;
    qint64 fileSize = -1;
    QString format;
    ReadStatus status = ReadStatus::Unsupported;
    QString message;
    QStringList warnings;
    QMap<QString, QStringList> tags;
    QMap<QString, QStringList> technical;
    bool usable() const { return status == ReadStatus::Ready || status == ReadStatus::Partial; }
    bool complete() const { return status == ReadStatus::Ready; }
};

// Always fills document (including diagnostics) when non-null. False means parsing failed;
// Partial is successful but may not be described as a complete tag equality result.
bool load(const QString &path, Document *document, QString *error = nullptr,
          const ReadLimits &limits = ReadLimits());
QString statusName(ReadStatus status);
QString supportDescription();

enum class Difference { Equal, Changed, LeftOnly, RightOnly };
struct FieldDifference {
    QString key;
    bool technical = false;
    QStringList left;
    QStringList right;
    Difference difference = Difference::Equal;
};
struct Comparison {
    QVector<FieldDifference> fields;
    int differenceCount = 0;
    bool complete = false;
};
// Exact Unicode values, ordered multivalues; never asserts audio or file identity.
Comparison compare(const Document &left, const Document &right, bool ignoreTechnical = false);

} }
#endif
