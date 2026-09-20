#include "picturediff.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QObject>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <vector>

namespace LqCompare {
namespace Picture {
namespace {
bool reject(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool validateSize(const QSize &size, const Limits &limits, QString *error)
{
    if (size.width() <= 0 || size.height() <= 0)
        return reject(error, QObject::tr("The decoder did not provide valid image dimensions; decoding was refused to keep memory bounded."));
    if (size.width() > limits.maximumDimension || size.height() > limits.maximumDimension
        || qint64(size.width()) * size.height() > limits.maximumPixels)
        return reject(error, QObject::tr("Image dimensions %1 × %2 exceed the limit of %3 pixels and %4 pixels per side. Original-resolution comparison was not performed.")
                      .arg(size.width()).arg(size.height()).arg(limits.maximumPixels).arg(limits.maximumDimension));
    return true;
}

bool animatedPng(QFile &file)
{
    // Qt 5's PNG handler can decode an APNG as a static PNG without advertising
    // animation. Detect its animation-control chunk before handing it off.
    const qint64 previous = file.pos();
    file.seek(0);
    bool animated = false;
    if (file.read(8) == QByteArray::fromHex("89504e470d0a1a0a")) {
        while (file.pos() + 12 <= file.size()) {
            const QByteArray header = file.read(8);
            if (header.size() != 8) break;
            const quint32 length = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(header.constData()));
            const QByteArray type = header.mid(4, 4);
            if (type == "acTL") { animated = true; break; }
            if (type == "IDAT" || type == "IEND") break;
            if (qint64(length) + 4 > file.size() - file.pos()) break;
            if (!file.seek(file.pos() + qint64(length) + 4)) break;
        }
    }
    file.seek(previous);
    return animated;
}

bool multiPictureJpeg(QFile &file)
{
    // The JPEG plugin exposes an MPO's representative JPEG as a single image.
    // An MPF APP2 index means that first JPEG is not a complete comparison.
    const qint64 previous = file.pos();
    file.seek(0);
    bool multiPicture = false;
    if (file.read(2) == QByteArray::fromHex("ffd8")) {
        while (file.pos() + 4 <= file.size()) {
            char prefix = 0, marker = 0;
            if (!file.getChar(&prefix) || uchar(prefix) != 0xff || !file.getChar(&marker)) break;
            while (uchar(marker) == 0xff && file.getChar(&marker)) {}
            const uchar type = uchar(marker);
            if (type == 0xda || type == 0xd9 || type == 0x00) break;
            if (type == 0x01 || (type >= 0xd0 && type <= 0xd8)) continue;
            const QByteArray lengthBytes = file.read(2);
            if (lengthBytes.size() != 2) break;
            const quint16 length = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(lengthBytes.constData()));
            if (length < 2 || qint64(length - 2) > file.size() - file.pos()) break;
            const qint64 end = file.pos() + length - 2;
            if (type == 0xe2 && length >= 6 && file.read(4) == QByteArray("MPF\0", 4)) {
                multiPicture = true;
                break;
            }
            if (!file.seek(end)) break;
        }
    }
    file.seek(previous);
    return multiPicture;
}

bool positionLess(const QPoint &a, const QPoint &b)
{
    return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x());
}

bool regionLess(const Region &a, const Region &b)
{
    if (a.bounds.topLeft() != b.bounds.topLeft())
        return positionLess(a.bounds.topLeft(), b.bounds.topLeft());
    return positionLess(a.firstPixel, b.firstPixel);
}

void uniteRegion(Region &target, const Region &source)
{
    target.bounds = target.bounds.united(source.bounds);
    target.pixelCount += source.pixelCount;
    target.maximumChannelDifference = qMax(target.maximumChannelDifference, source.maximumChannelDifference);
    if (positionLess(source.firstPixel, target.firstPixel)) target.firstPixel = source.firstPixel;
}

// A scanline union/find keeps only components still touching the current row.
// Even a four-million-component checkerboard uses O(width + maximumRegions)
// indexing memory, with no per-pixel labels, flood-fill queue, or giant result.
void findRegions(Result *result, Connectivity connectivity, int maximumRegions)
{
    struct Run { int first; int last; int label; };
    struct Node { Region region; int parent; };
    QVector<Run> previous;
    QVector<Node> nodes;
    std::vector<Region> retained;
    retained.reserve(size_t(maximumRegions));
    const int margin = connectivity == Connectivity::Eight ? 1 : 0;
    auto root = [&nodes](int label) {
        int current = label;
        while (nodes[current].parent != current) current = nodes[current].parent;
        while (nodes[label].parent != label) {
            const int next = nodes[label].parent;
            nodes[label].parent = current;
            label = next;
        }
        return current;
    };
    auto finish = [result, &retained, maximumRegions](const Region &region) {
        ++result->regionCount;
        if (maximumRegions == 0) return;
        // A max-heap keeps the earliest regions, regardless of when a long
        // component finishes. Stopping after N completed components is wrong.
        if (int(retained.size()) < maximumRegions) {
            retained.push_back(region);
            std::push_heap(retained.begin(), retained.end(), regionLess);
        } else if (regionLess(region, retained.front())) {
            std::pop_heap(retained.begin(), retained.end(), regionLess);
            retained.back() = region;
            std::push_heap(retained.begin(), retained.end(), regionLess);
        }
    };
    const QImage &mask = result->differenceImage;
    for (int y = 0; y < mask.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        QVector<Run> current;
        int previousStart = 0;
        for (int x = 0; x < mask.width();) {
            if ((row[x] & 0x00ffffffU) == 0) { ++x; continue; }
            const int first = x;
            int maximumDelta = 0;
            while (x < mask.width() && (row[x] & 0x00ffffffU) != 0) {
                maximumDelta = qMax(maximumDelta, qMax(qMax(qRed(row[x]), qGreen(row[x])), qBlue(row[x])));
                ++x;
            }
            const int last = x - 1;
            Region runRegion{QRect(first, y, x - first, 1), x - first, maximumDelta, QPoint(first, y)};
            while (previousStart < previous.size() && previous[previousStart].last + margin < first) ++previousStart;
            int label = -1;
            for (int p = previousStart; p < previous.size() && previous[p].first <= last + margin; ++p) {
                const int other = root(previous[p].label);
                if (label < 0) label = other;
                else if (label != other) {
                    uniteRegion(nodes[label].region, nodes[other].region);
                    nodes[other].parent = label;
                }
            }
            if (label < 0) {
                label = nodes.size();
                nodes.append(Node{runRegion, label});
            } else uniteRegion(nodes[label].region, runRegion);
            current.append(Run{first, last, label});
        }
        QVector<int> active(nodes.size(), -1);
        for (Run &run : current) { run.label = root(run.label); active[run.label] = 0; }
        for (int n = 0; n < nodes.size(); ++n)
            if (nodes[n].parent == n && active[n] < 0) finish(nodes[n].region);
        QVector<Node> compact;
        compact.reserve(current.size());
        for (Run &run : current) {
            if (active[run.label] == 0) {
                compact.append(Node{nodes[run.label].region, compact.size()});
                active[run.label] = compact.size(); // one-based so zero means unassigned
            }
            run.label = active[run.label] - 1;
        }
        nodes = std::move(compact);
        previous = std::move(current);
    }
    for (const Node &node : nodes) finish(node.region);
    std::sort(retained.begin(), retained.end(), regionLess);
    result->regions.reserve(int(retained.size()));
    for (const Region &region : retained) result->regions.append(region);
    result->regionsTruncated = result->regionCount > result->regions.size();
}
}

double Result::differencePercent() const
{
    return totalPixels ? 100.0 * double(differentPixels) / double(totalPixels) : 0.0;
}

bool load(const QString &path, Document *document, QString *error, const Limits &limits)
{
    if (!document) return reject(error, QObject::tr("No image output was supplied."));
    if (path.isEmpty()) return reject(error, QObject::tr("Choose an image file for both sides."));
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return reject(error, QObject::tr("Image is not a readable regular file: %1").arg(path));
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return reject(error, QObject::tr("Cannot open %1: %2").arg(path, file.errorString()));
    if (file.size() > limits.maximumFileBytes)
        return reject(error, QObject::tr("Image file %1 is larger than the %2 MiB input limit.")
                      .arg(path).arg(limits.maximumFileBytes / 1024 / 1024));
    if (animatedPng(file))
        return reject(error, QObject::tr("Animated PNG %1 is not supported by this view. No first-frame-only comparison was made.").arg(path));
    if (multiPictureJpeg(file))
        return reject(error, QObject::tr("Multi-picture JPEG/MPO %1 is not supported by this view. No first-picture-only comparison was made.").arg(path));

    QImageReader reader(&file);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(false);
    if (!reader.canRead())
        return reject(error, QObject::tr("Unsupported or damaged image %1: %2").arg(path, reader.errorString()));
    QString reason;
    if (!validateSize(reader.size(), limits, &reason))
        return reject(error, QObject::tr("%1: %2").arg(path, reason));
    const int count = reader.imageCount();
    if (count > 1 || (reader.supportsAnimation() && count != 1))
        return reject(error, QObject::tr("Multi-frame or animated image %1 is not supported by this view. No first-frame-only comparison was made.").arg(path));

    Document next;
    next.path = info.absoluteFilePath();
    next.format = reader.format().toUpper();
    QImage decoded = reader.read();
    if (decoded.isNull())
        return reject(error, QObject::tr("Cannot decode image %1: %2").arg(path, reader.errorString()));
    if (!validateSize(decoded.size(), limits, &reason))
        return reject(error, QObject::tr("%1: %2").arg(path, reason));
    // Some handlers report an unknown count until the first image has been read.
    if (reader.jumpToNextImage())
        return reject(error, QObject::tr("Multi-frame image %1 is not supported by this view. No first-frame-only comparison was made.").arg(path));
    next.originalSize = decoded.size();
    next.sourceDepth = decoded.depth();
    next.hasAlpha = decoded.hasAlphaChannel();
    next.horizontalDpi = decoded.dotsPerMeterX() * 0.0254;
    next.verticalDpi = decoded.dotsPerMeterY() * 0.0254;
    const QColorSpace space = decoded.colorSpace();
    if (!space.isValid()) next.colorSpace = QObject::tr("Unspecified");
    else if (space == QColorSpace(QColorSpace::SRgb)) next.colorSpace = QStringLiteral("sRGB");
    else if (space == QColorSpace(QColorSpace::SRgbLinear)) next.colorSpace = QStringLiteral("Linear sRGB");
    else if (space == QColorSpace(QColorSpace::AdobeRgb)) next.colorSpace = QStringLiteral("Adobe RGB");
    else if (space == QColorSpace(QColorSpace::DisplayP3)) next.colorSpace = QStringLiteral("Display P3");
    else next.colorSpace = QObject::tr("Embedded profile (not transformed)");
    next.image = decoded.convertToFormat(QImage::Format_ARGB32);
    next.image.setDevicePixelRatio(1.0);
    if (next.image.isNull()) return reject(error, QObject::tr("Insufficient memory to decode %1.").arg(path));
    *document = std::move(next);
    if (error) error->clear();
    return true;
}

bool compare(const QImage &left, const QImage &right, Result *result, QString *error,
             const CompareOptions &options, const Limits &limits)
{
    if (!result) return reject(error, QObject::tr("No comparison output was supplied."));
    if (left.isNull() || right.isNull())
        return reject(error, QObject::tr("Two decoded images are required for comparison."));
    if (options.channelTolerance < 0 || options.channelTolerance > 255)
        return reject(error, QObject::tr("Channel tolerance must be between 0 and 255."));
    if (options.alphaMode != AlphaMode::Include && options.alphaMode != AlphaMode::Ignore
        && options.alphaMode != AlphaMode::Only)
        return reject(error, QObject::tr("Unknown alpha comparison mode."));
    if (options.connectivity != Connectivity::Four && options.connectivity != Connectivity::Eight)
        return reject(error, QObject::tr("Unknown region connectivity mode."));
    if (limits.maximumRegions < 0 || limits.maximumRegions > 65536)
        return reject(error, QObject::tr("The navigation index limit must be between 0 and 65536 regions."));
    const QSize canvas(qMax(left.width(), right.width()), qMax(left.height(), right.height()));
    if (!validateSize(canvas, limits, error)) return false;
    // Keeping caller-owned images shared is important for view refreshes.
    const QImage a = left.convertToFormat(QImage::Format_ARGB32);
    const QImage b = right.convertToFormat(QImage::Format_ARGB32);
    if (a.isNull() || b.isNull()) return reject(error, QObject::tr("Insufficient memory for the comparison."));
    Result next;
    next.canvasSize = canvas;
    const qint64 overlap = qint64(qMin(a.width(), b.width())) * qMin(a.height(), b.height());
    next.leftOnlyPixels = qint64(a.width()) * a.height() - overlap;
    next.rightOnlyPixels = qint64(b.width()) * b.height() - overlap;
    next.totalPixels = qint64(a.width()) * a.height() + qint64(b.width()) * b.height() - overlap;
    next.differenceImage = QImage(canvas, QImage::Format_ARGB32);
    if (next.differenceImage.isNull()) return reject(error, QObject::tr("Insufficient memory for the difference image."));
    int minX = canvas.width(), minY = canvas.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < canvas.height(); ++y) {
        const QRgb *rowA = y < a.height() ? reinterpret_cast<const QRgb *>(a.constScanLine(y)) : nullptr;
        const QRgb *rowB = y < b.height() ? reinterpret_cast<const QRgb *>(b.constScanLine(y)) : nullptr;
        QRgb *out = reinterpret_cast<QRgb *>(next.differenceImage.scanLine(y));
        for (int x = 0; x < canvas.width(); ++x) {
            const bool hasA = rowA && x < a.width(), hasB = rowB && x < b.width();
            bool different = false;
            if (!hasA && !hasB) out[x] = qRgba(0, 0, 0, 0);
            else if (!hasA || !hasB) {
                out[x] = hasA ? qRgb(255, 160, 0) : qRgb(0, 200, 255);
                different = true;
            } else {
                const QRgb pa = rowA[x], pb = rowB[x];
                const int dr = options.alphaMode == AlphaMode::Only ? 0 : std::abs(qRed(pa) - qRed(pb));
                const int dg = options.alphaMode == AlphaMode::Only ? 0 : std::abs(qGreen(pa) - qGreen(pb));
                const int db = options.alphaMode == AlphaMode::Only ? 0 : std::abs(qBlue(pa) - qBlue(pb));
                const int da = options.alphaMode == AlphaMode::Ignore ? 0 : std::abs(qAlpha(pa) - qAlpha(pb));
                different = qMax(qMax(dr, dg), qMax(db, da)) > options.channelTolerance;
                out[x] = different ? qRgb(qMax(dr, da), dg, qMax(db, da)) : qRgb(0, 0, 0);
            }
            if (different) {
                ++next.differentPixels;
                minX = qMin(minX, x); minY = qMin(minY, y);
                maxX = qMax(maxX, x); maxY = qMax(maxY, y);
            }
        }
    }
    if (maxX >= 0) next.differenceBounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    findRegions(&next, options.connectivity, limits.maximumRegions);
    *result = std::move(next);
    if (error) error->clear();
    return true;
}

} // namespace Picture
} // namespace LqCompare
