#ifndef LQCOMPARE_FORMATDETECTOR_H
#define LQCOMPARE_FORMATDETECTOR_H

#include "formatdefinition.h"
#include "mask.h"

namespace LqCompare {
class SessionTypeRegistry;
namespace Format {

enum class ContentKind { Unknown, Text, Binary };
enum class DetectionSource { None, AssociationOverride, FileMask, ContentSignature, UnknownFallback, Directory };
enum class UnknownFallback { Automatic, Text, Hex, Picture, Ask };

struct FileProbe
{
    // detect() also accepts unnamed memory samples when contentAvailable is true.
    // A default FileProbe means absent; detectFiles() maps empty paths to absent.
    QString path;
    QByteArray prefix;
    bool contentAvailable = false;
    bool complete = true;
    bool directory = false;
    QString error;
};

struct ContentAssessment
{
    ContentKind kind = ContentKind::Unknown;
    QString encoding;
    QString explanation;
};

// Reads at most limit bytes (+ one byte to detect truncation), never whole files.
FileProbe probeFile(const QString &path, qint64 limit = 64 * 1024);
ContentAssessment assessContent(const FileProbe &probe);

struct AssociationOverride
{
    QString mask;
    QString formatId;
    bool enabled = true;
};

struct DetectionOptions
{
    UnknownFallback unknownFallback = UnknownFallback::Automatic;
    Qt::CaseSensitivity maskCaseSensitivity = Qt::CaseInsensitive;
    QVector<AssociationOverride> overrides;
    qint64 probeLimit = 64 * 1024;
};

struct DetectionResult
{
    QString formatId;
    QString formatName;
    QString requestedSessionTypeId;
    QString sessionTypeId;
    DetectionSource source = DetectionSource::None;
    QString matchedRule;
    int matchedSide = -1; // 0 left, 1 right; rule order wins over side order.
    bool usedAvailabilityFallback = false;
    QString explanation;
    QStringList diagnostics;
    ContentAssessment leftContent;
    ContentAssessment rightContent;
    bool canOpen() const { return !sessionTypeId.isEmpty(); }
};

class FormatDetector
{
public:
    FormatDetector();
    explicit FormatDetector(const QVector<FormatDefinition> &definitions);
    // Atomic replacement: invalid definitions leave the previous table intact.
    bool setDefinitions(const QVector<FormatDefinition> &definitions, QString *error = nullptr);
    const QVector<FormatDefinition> &definitions() const { return m_definitions; }

    DetectionResult detect(const FileProbe &left, const FileProbe &right,
                           const SessionTypeRegistry &types,
                           const DetectionOptions &options = {}) const;
    DetectionResult detectFiles(const QString &leftPath, const QString &rightPath,
                                const SessionTypeRegistry &types,
                                const DetectionOptions &options = {}) const;

private:
    QVector<FormatDefinition> m_definitions;
    QVector<QVector<Filter::Mask>> m_masks;
};

QString detectionSourceLabel(DetectionSource source);

} // namespace Format
} // namespace LqCompare
#endif
