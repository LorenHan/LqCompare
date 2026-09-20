#ifndef LQCOMPARE_PICTUREDIFF_H
#define LQCOMPARE_PICTUREDIFF_H

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

namespace LqCompare {
namespace Picture {

// Conservative defaults also apply on the 32-bit target. No thumbnail is ever
// substituted for the original pixels when deciding equality.
struct Limits {
    qint64 maximumFileBytes = 64 * 1024 * 1024;
    qint64 maximumPixels = 8 * 1024 * 1024;
    int maximumDimension = 32768;
    // Retain only the first N regions in stable position order for navigation.
    // Pixel statistics and regionCount remain exact even when this is zero.
    // Values above 65536 are refused to keep the index allocation bounded.
    int maximumRegions = 4096;
};

enum class AlphaMode { Include, Ignore, Only };
enum class Connectivity { Four, Eight };

struct CompareOptions {
    // A pixel differs iff at least one selected channel exceeds this threshold.
    // The comparison uses decoded, straight 8-bit RGBA, including hidden RGB in
    // fully transparent pixels. No color-profile or EXIF transform is applied.
    int channelTolerance = 0;
    AlphaMode alphaMode = AlphaMode::Include;
    Connectivity connectivity = Connectivity::Four;
};

struct Document {
    QString path;
    QByteArray format;
    QImage image; // straight ARGB32; devicePixelRatio is always 1
    QSize originalSize;
    int sourceDepth = 0;
    bool hasAlpha = false;
    double horizontalDpi = 0;
    double verticalDpi = 0;
    QString colorSpace;
};

struct Region {
    QRect bounds;
    qint64 pixelCount = 0;
    // Maximum selected channel delta; missing pixels have delta 255.
    int maximumChannelDifference = 0;
    QPoint firstPixel; // first actual difference in row-major order; stable tie-break
};

struct Result {
    QSize canvasSize;
    qint64 totalPixels = 0; // union of the two extents; excludes missing on both
    qint64 differentPixels = 0;
    qint64 leftOnlyPixels = 0;
    qint64 rightOnlyPixels = 0;
    QRect differenceBounds;
    // Opaque RGB channel differences; alpha differences contribute to magenta.
    // Missing left pixels are cyan, missing right pixels orange. Outside both
    // image extents is transparent. Equal pixels are black.
    QImage differenceImage;
    // Sorted by bounding-box top, left, then firstPixel for an unambiguous tie.
    QVector<Region> regions;
    qint64 regionCount = 0;
    bool regionsTruncated = false;
    double differencePercent() const;
};

bool load(const QString &path, Document *document, QString *error = nullptr,
          const Limits &limits = Limits());
bool compare(const QImage &left, const QImage &right, Result *result,
             QString *error = nullptr, const CompareOptions &options = CompareOptions(),
             const Limits &limits = Limits());

} // namespace Picture
} // namespace LqCompare

#endif
