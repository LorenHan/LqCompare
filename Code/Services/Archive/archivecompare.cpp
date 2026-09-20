#include "archivecompare.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace LqCompare { namespace Archive {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("Archive", text); }
quint16 u16(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}
quint32 u32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

bool pathFailure(QString *reason, const QString &message)
{
    if (reason) *reason = message;
    return false;
}

bool strictUtf8(const QByteArray &bytes, QString *text)
{
    *text = QString::fromUtf8(bytes);
    return text->toUtf8() == bytes;
}

QString cp437(const QByteArray &bytes)
{
    // ZIP's specified legacy encoding. No locale-dependent guessing.
    static const ushort upper[128] = {
        0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,
        0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
        0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,
        0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
        0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,
        0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
        0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
        0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
        0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,
        0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
        0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,
        0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
        0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,
        0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
        0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,
        0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
    };
    QString result;
    result.reserve(bytes.size());
    for (unsigned char c : bytes) result.append(QChar(c < 128 ? ushort(c) : upper[c - 128]));
    return result;
}

quint32 nameCrc32(const QByteArray &bytes)
{
    // Small, bounded filename checksum for the Info-ZIP Unicode name field.
    // This does not read or verify file payloads.
    quint32 crc = 0xffffffffu;
    for (unsigned char c : bytes) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

struct Span { quint64 begin; quint64 end; QString path; };

class ZipReader {
public:
    ZipReader(const QString &path, const Limits &limits, const std::function<bool()> &cancel)
        : file(path), budget(limits), cancelled(cancel)
    {
        result.sourcePath = QFileInfo(path).absoluteFilePath();
    }

    Directory run()
    {
        if (!read()) {
            result.entries.clear();
            result.totalUncompressedSize = 0;
            result.totalCompressedSize = 0;
            result.storedEntryCount = 0;
        }
        return result;
    }

private:
    bool fail(ErrorCode code, const QString &message, qint64 offset = -1,
              const QString &path = QString())
    {
        result.error = {code, message, path, offset};
        return false;
    }

    bool checkCancelled()
    {
        return !(cancelled && cancelled()) || fail(ErrorCode::Cancelled, tr("Archive reading was cancelled."));
    }

    bool readAt(quint64 offset, int size, QByteArray *data)
    {
        if (offset > fileSize || quint64(size) > fileSize - offset)
            return fail(ErrorCode::Corrupt, tr("A ZIP record extends beyond the end of the archive."), qint64(offset));
        if (!file.seek(qint64(offset)))
            return fail(ErrorCode::Io, tr("Cannot seek in the archive: %1").arg(file.errorString()), qint64(offset));
        *data = file.read(size);
        if (data->size() != size)
            return fail(ErrorCode::Io, tr("Cannot read the complete ZIP record: %1").arg(file.errorString()), qint64(offset));
        return true;
    }

    bool extraFields(const QByteArray &extra, const QByteArray &rawName, QString *unicodeName,
                     bool *hasUnicodeName, quint64 offset)
    {
        QSet<quint16> seen;
        for (int cursor = 0; cursor < extra.size();) {
            if (extra.size() - cursor < 4)
                return fail(ErrorCode::Corrupt, tr("A ZIP extra field header is truncated."), qint64(offset + cursor));
            const quint16 tag = u16(extra, cursor), size = u16(extra, cursor + 2);
            cursor += 4;
            if (size > extra.size() - cursor)
                return fail(ErrorCode::Corrupt, tr("A ZIP extra field body is truncated."), qint64(offset + cursor));
            if (tag == 0x0001)
                return fail(ErrorCode::Zip64, tr("ZIP64 archives are not supported by this read-only ZIP reader."), qint64(offset + cursor));
            if (tag == 0x9901 || tag == 0x0017)
                return fail(ErrorCode::Encrypted, tr("Encrypted ZIP archives are not supported; a password cannot be used in this version."), qint64(offset + cursor));
            if (tag == 0x7075) {
                if (seen.contains(tag))
                    return fail(ErrorCode::Corrupt, tr("A ZIP member has duplicate Unicode path fields."), qint64(offset + cursor));
                seen.insert(tag);
                if (size < 5)
                    return fail(ErrorCode::Corrupt, tr("The Unicode path field is truncated."), qint64(offset + cursor));
                // Per APPNOTE, stale fields and unknown versions must be ignored.
                if (uchar(extra[cursor]) == 1 && u32(extra, cursor + 1) == nameCrc32(rawName)) {
                    if (!strictUtf8(extra.mid(cursor + 5, size - 5), unicodeName))
                        return fail(ErrorCode::Corrupt, tr("The Unicode path field is not valid UTF-8."), qint64(offset + cursor));
                    *hasUnicodeName = true;
                }
            }
            cursor += size;
        }
        return true;
    }

    bool normalize(const QString &raw, QString *path, quint64 offset)
    {
        QString reason;
        ErrorCode code = ErrorCode::None;
        if (!normalizeEntryPath(raw, path, &reason, budget, &code))
            return fail(code, reason, qint64(offset), raw);
        return true;
    }

    bool localHeader(const Entry &entry, quint16 flags, quint16 version, quint16 time, quint16 date,
                     quint64 centralOffset, Span *span)
    {
        const quint64 offset = entry.localHeaderOffset;
        if (offset > centralOffset || centralOffset - offset < 30)
            return fail(ErrorCode::Corrupt, tr("A local file header overlaps the central directory."), qint64(offset), entry.path);
        QByteArray local;
        if (!readAt(offset, 30, &local)) return false;
        if (u32(local, 0) != 0x04034b50u)
            return fail(ErrorCode::Corrupt, tr("The local file header signature is missing."), qint64(offset), entry.path);
        if (u16(local, 6) & (1u | 64u | 8192u))
            return fail(ErrorCode::Encrypted, tr("An encrypted ZIP local header is not supported."), qint64(offset), entry.path);
        if (u32(local, 18) == 0xffffffffu || u32(local, 22) == 0xffffffffu)
            return fail(ErrorCode::Zip64, tr("ZIP64 local headers are not supported."), qint64(offset), entry.path);
        if (u16(local, 4) != version || u16(local, 6) != flags || u16(local, 8) != entry.method
            || u16(local, 10) != time || u16(local, 12) != date)
            return fail(ErrorCode::Corrupt, tr("Local and central ZIP headers disagree."), qint64(offset), entry.path);
        const quint16 nameLength = u16(local, 26), extraLength = u16(local, 28);
        const quint64 payloadOffset = offset + 30 + nameLength + extraLength;
        if (payloadOffset > centralOffset || entry.compressedSize > centralOffset - payloadOffset)
            return fail(ErrorCode::Corrupt, tr("A ZIP member extends into the central directory."), qint64(offset), entry.path);
        if (nameLength != entry.rawName.size())
            return fail(ErrorCode::Corrupt, tr("Local and central ZIP member names disagree."), qint64(offset), entry.path);
        QByteArray variable;
        if (!readAt(offset + 30, nameLength + extraLength, &variable)) return false;
        if (variable.left(nameLength) != entry.rawName)
            return fail(ErrorCode::Corrupt, tr("Local and central ZIP member names disagree."), qint64(offset), entry.path);
        QString unicode, normalized;
        bool hasUnicode = false;
        if (!extraFields(variable.mid(nameLength), entry.rawName, &unicode, &hasUnicode,
                         offset + 30 + nameLength)) return false;
        if (hasUnicode && (!normalize(unicode, &normalized, offset) || normalized != entry.path)) {
            if (result.error.isError()) return false;
            return fail(ErrorCode::Corrupt, tr("Local and central Unicode paths disagree."), qint64(offset), entry.path);
        }
        if (hasUnicode && ((entry.rawName.endsWith('/') || entry.rawName.endsWith('\\'))
                           != (unicode.endsWith('/') || unicode.endsWith('\\'))))
            return fail(ErrorCode::Corrupt, tr("The local Unicode path field changes the ZIP entry type."), qint64(offset), entry.path);
        quint64 end = payloadOffset + entry.compressedSize;
        const quint32 localCrc = u32(local, 14), localCompressed = u32(local, 18), localSize = u32(local, 22);
        if (!(flags & 8u)) {
            if (localCrc != entry.crc32 || localCompressed != entry.compressedSize || localSize != entry.uncompressedSize)
                return fail(ErrorCode::Corrupt, tr("Local and central ZIP sizes or CRC records disagree."), qint64(offset), entry.path);
        } else {
            // Some writers leave zero placeholders, others repeat actual values.
            if ((localCrc && localCrc != entry.crc32) || (localCompressed && localCompressed != entry.compressedSize)
                || (localSize && localSize != entry.uncompressedSize))
                return fail(ErrorCode::Corrupt, tr("The ZIP data descriptor placeholders disagree with the directory."), qint64(offset), entry.path);
            if (centralOffset - end < 12)
                return fail(ErrorCode::Corrupt, tr("The ZIP data descriptor is missing."), qint64(end), entry.path);
            QByteArray descriptor;
            if (!readAt(end, int(qMin<quint64>(16, centralOffset - end)), &descriptor)) return false;
            const auto matches = [&](int start) {
                return descriptor.size() >= start + 12 && u32(descriptor, start) == entry.crc32
                    && u32(descriptor, start + 4) == entry.compressedSize
                    && u32(descriptor, start + 8) == entry.uncompressedSize;
            };
            // CRC itself may equal the optional signature; check complete tuples.
            if (u32(descriptor, 0) == 0x08074b50u && matches(4)) end += 16;
            else if (matches(0)) end += 12;
            else return fail(ErrorCode::Corrupt, tr("The ZIP data descriptor disagrees with the directory."), qint64(end), entry.path);
        }
        *span = {offset, end, entry.path};
        return true;
    }

    bool insertEntry(const Entry &entry)
    {
        auto existing = paths.constFind(entry.path);
        if (existing != paths.cend()) {
            Entry &previous = result.entries[*existing];
            if (!previous.implicitDirectory || !entry.directory)
                return fail(ErrorCode::DuplicatePath, tr("Conflicting or duplicate normalized ZIP path: %1").arg(entry.path), -1, entry.path);
            previous = entry;
        } else {
            if (result.entries.size() >= budget.maxEntries)
                return fail(ErrorCode::LimitExceeded, tr("The archive exceeds the entry budget, including parent directories."), -1, entry.path);
            paths.insert(entry.path, result.entries.size());
            result.entries.append(entry);
        }
        for (int slash = entry.path.indexOf('/'); slash >= 0; slash = entry.path.indexOf('/', slash + 1)) {
            const QString parent = entry.path.left(slash);
            auto found = paths.constFind(parent);
            if (found != paths.cend()) {
                if (!result.entries[*found].directory)
                    return fail(ErrorCode::DuplicatePath, tr("A ZIP file is also used as a parent directory: %1").arg(parent), -1, parent);
                continue;
            }
            if (result.entries.size() >= budget.maxEntries)
                return fail(ErrorCode::LimitExceeded, tr("The archive exceeds the entry budget, including parent directories."), -1, parent);
            Entry implicit;
            implicit.path = parent;
            implicit.directory = true;
            implicit.implicitDirectory = true;
            paths.insert(parent, result.entries.size());
            result.entries.append(implicit);
        }
        return true;
    }

    bool read()
    {
        if (!checkCancelled()) return false;
        if (!budget.maxArchiveBytes || !budget.maxCentralDirectoryBytes || budget.maxEntries <= 0
            || budget.maxPathBytes <= 0 || budget.maxPathDepth <= 0 || !budget.maxCompressionRatio)
            return fail(ErrorCode::LimitExceeded, tr("The ZIP reader budgets must be positive."));
        const QFileInfo before(file.fileName());
        if (!before.isFile()) return fail(ErrorCode::Io, tr("The archive path is not a regular file: %1").arg(result.sourcePath));
        if (!file.open(QIODevice::ReadOnly)) return fail(ErrorCode::Io, tr("Cannot open archive: %1").arg(file.errorString()));
        fileSize = quint64(file.size());
        if (fileSize > budget.maxArchiveBytes)
            return fail(ErrorCode::LimitExceeded, tr("Archive size exceeds the configured limit (%1 bytes).").arg(budget.maxArchiveBytes));
        if (fileSize < 4) return fail(ErrorCode::UnknownFormat, tr("This file is not a ZIP archive."));
        QByteArray signature;
        if (!readAt(0, 4, &signature)) return false;
        const quint32 firstSignature = u32(signature, 0);
        if (firstSignature != 0x04034b50u && firstSignature != 0x06054b50u) {
            if (firstSignature == 0x06064b50u || firstSignature == 0x07064b50u)
                return fail(ErrorCode::Zip64, tr("ZIP64 archives are not supported."));
            return fail(ErrorCode::UnknownFormat, tr("Unsupported archive format: only ZIP/JAR with ZIP32 headers are supported; self-extracting and split archives are not supported."));
        }
        if (fileSize < 22) return fail(ErrorCode::Corrupt, tr("The ZIP end-of-directory record is truncated."));
        const int tailSize = int(qMin<quint64>(fileSize, 22u + 65535u));
        QByteArray tail;
        if (!readAt(fileSize - tailSize, tailSize, &tail)) return false;
        int eocd = -1, fallbackEocd = -1;
        for (int pos = tail.size() - 22; pos >= 0; --pos) {
            if (u32(tail, pos) == 0x06054b50u && pos + 22 + u16(tail, pos + 20) == tail.size()) {
                if (fallbackEocd < 0) fallbackEocd = pos;
                const quint64 location = fileSize - tailSize + pos;
                const quint64 start = u32(tail, pos + 16), size = u32(tail, pos + 12);
                const quint16 count = u16(tail, pos + 10);
                // An archive comment may itself contain a complete EOCD-shaped
                // record. Do not let it mask the real directory before the comment.
                if (start <= location && size == location - start && quint64(count) * 46 <= size
                    && (count || (!size && !start))) {
                    eocd = pos;
                    break;
                }
            }
        }
        if (eocd < 0) eocd = fallbackEocd;
        if (eocd < 0) return fail(ErrorCode::Corrupt, tr("The ZIP end-of-directory record is missing or has trailing/truncated data."));
        const quint64 eocdOffset = fileSize - tailSize + eocd;
        const quint16 disk = u16(tail, eocd + 4), centralDisk = u16(tail, eocd + 6);
        const quint16 diskCount = u16(tail, eocd + 8), entryCount = u16(tail, eocd + 10);
        const quint64 centralSize = u32(tail, eocd + 12), centralOffset = u32(tail, eocd + 16);
        QByteArray locator;
        // A locator is outside the central directory. Bytes in an entry comment
        // at this position are ordinary comment data, even if their signature matches.
        if (eocdOffset >= 20 && eocdOffset - 20 >= centralOffset + centralSize
            && !readAt(eocdOffset - 20, 4, &locator)) return false;
        if ((!locator.isEmpty() && u32(locator, 0) == 0x07064b50u) || disk == 0xffffu || centralDisk == 0xffffu
            || diskCount == 0xffffu || entryCount == 0xffffu || centralSize == 0xffffffffu || centralOffset == 0xffffffffu)
            return fail(ErrorCode::Zip64, tr("ZIP64 archives are not supported by this read-only ZIP reader."), qint64(eocdOffset));
        if (disk || centralDisk || diskCount != entryCount)
            return fail(ErrorCode::Unsupported, tr("Split or multi-disk ZIP archives are not supported."), qint64(eocdOffset));
        if (entryCount > budget.maxEntries || centralSize > budget.maxCentralDirectoryBytes
            || centralSize > quint64(std::numeric_limits<int>::max()))
            return fail(ErrorCode::LimitExceeded, tr("The ZIP central directory exceeds the entry or memory budget."), qint64(eocdOffset));
        if (centralOffset > eocdOffset || centralSize != eocdOffset - centralOffset
            || quint64(entryCount) * 46 > centralSize || (!entryCount && (centralSize || centralOffset)))
            return fail(ErrorCode::Corrupt, tr("ZIP directory offsets, sizes, or entry counts are inconsistent."), qint64(eocdOffset));
        QByteArray central;
        if (!readAt(centralOffset, int(centralSize), &central)) return false;
        QVector<Span> spans;
        spans.reserve(entryCount);
        int cursor = 0;
        for (int count = 0; count < entryCount; ++count) {
            if (!checkCancelled()) return false;
            const quint64 absolute = centralOffset + cursor;
            if (central.size() - cursor < 46 || u32(central, cursor) != 0x02014b50u)
                return fail(ErrorCode::Corrupt, tr("A ZIP central directory header is missing or truncated."), qint64(absolute));
            const quint16 madeBy = u16(central, cursor + 4), version = u16(central, cursor + 6);
            const quint16 flags = u16(central, cursor + 8), method = u16(central, cursor + 10);
            const quint16 time = u16(central, cursor + 12), date = u16(central, cursor + 14);
            const quint16 nameLength = u16(central, cursor + 28), extraLength = u16(central, cursor + 30);
            const quint16 commentLength = u16(central, cursor + 32), startDisk = u16(central, cursor + 34);
            const quint32 attributes = u32(central, cursor + 38);
            Entry entry;
            entry.method = method;
            entry.crc32 = u32(central, cursor + 16);
            entry.compressedSize = u32(central, cursor + 20);
            entry.uncompressedSize = u32(central, cursor + 24);
            entry.localHeaderOffset = u32(central, cursor + 42);
            if (flags & (1u | 64u | 8192u))
                return fail(ErrorCode::Encrypted, tr("Encrypted ZIP entries are not supported; a password cannot be used in this version."), qint64(absolute));
            if (entry.compressedSize == 0xffffffffu || entry.uncompressedSize == 0xffffffffu
                || entry.localHeaderOffset == 0xffffffffu || startDisk == 0xffffu)
                return fail(ErrorCode::Zip64, tr("ZIP64 entries are not supported."), qint64(absolute));
            const int recordLength = 46 + nameLength + extraLength + commentLength;
            if (recordLength > central.size() - cursor)
                return fail(ErrorCode::Corrupt, tr("A ZIP member name, extra field, or comment is truncated."), qint64(absolute));
            if (nameLength > budget.maxPathBytes)
                return fail(ErrorCode::LimitExceeded, tr("The ZIP member name exceeds the path budget."), qint64(absolute));
            entry.rawName = central.mid(cursor + 46, nameLength);
            QString decoded;
            if (flags & 2048u) {
                if (!strictUtf8(entry.rawName, &decoded))
                    return fail(ErrorCode::Corrupt, tr("A UTF-8 ZIP member name contains invalid UTF-8 bytes."), qint64(absolute));
            } else decoded = cp437(entry.rawName);
            QString unicode;
            bool hasUnicode = false;
            if (!extraFields(central.mid(cursor + 46 + nameLength, extraLength), entry.rawName,
                             &unicode, &hasUnicode, absolute + 46 + nameLength)) return false;
            if (method != 0 && method != 8)
                return fail(ErrorCode::Unsupported, tr("ZIP compression method %1 is not supported; only stored and deflate entries can be listed.").arg(method), qint64(absolute));
            if (version > 20 || startDisk || (flags & ~(2048u | 8u | 4u | 2u)) || (method == 0 && (flags & 6u)))
                return fail(ErrorCode::Unsupported, tr("This ZIP entry uses unsupported version, disk, or flag features."), qint64(absolute));
            // Validate both representations: a safe Unicode alias may not hide a dangerous raw name.
            if (!normalize(decoded, &entry.path, absolute)) return false;
            if (hasUnicode) {
                QString unicodePath;
                if (!normalize(unicode, &unicodePath, absolute)) return false;
                if ((flags & 2048u) && unicodePath != entry.path)
                    return fail(ErrorCode::Corrupt, tr("The UTF-8 name and Unicode path field disagree."), qint64(absolute), entry.path);
                if ((decoded.endsWith('/') || decoded.endsWith('\\')) != (unicode.endsWith('/') || unicode.endsWith('\\')))
                    return fail(ErrorCode::Corrupt, tr("The Unicode path field changes the ZIP entry type."), qint64(absolute), entry.path);
                entry.path = unicodePath;
            }
            const quint16 host = madeBy >> 8;
            const quint32 unixType = (attributes >> 16) & 0170000u;
            const bool unixAttributes = host == 3 || host == 19;
            if (unixAttributes && unixType && unixType != 0100000u && unixType != 0040000u)
                return fail(ErrorCode::UnsafePath, tr("Symbolic links and special-file ZIP entries are refused: %1").arg(entry.path), qint64(absolute), entry.path);
            const bool nameIsDirectory = decoded.endsWith('/') || decoded.endsWith('\\');
            entry.directory = nameIsDirectory || (attributes & 16u) || (unixAttributes && unixType == 0040000u);
            if (entry.directory && unixAttributes && unixType == 0100000u)
                return fail(ErrorCode::Corrupt, tr("ZIP file and directory attributes conflict."), qint64(absolute), entry.path);
            if (entry.directory && (entry.uncompressedSize || entry.crc32))
                return fail(ErrorCode::Corrupt, tr("A ZIP directory entry claims file data."), qint64(absolute), entry.path);
            if (method == 0 && entry.compressedSize != entry.uncompressedSize)
                return fail(ErrorCode::Corrupt, tr("A stored ZIP entry has inconsistent sizes."), qint64(absolute), entry.path);
            if (entry.uncompressedSize > budget.maxEntryUncompressedBytes
                || entry.uncompressedSize > budget.maxTotalUncompressedBytes - qMin(result.totalUncompressedSize, budget.maxTotalUncompressedBytes))
                return fail(ErrorCode::LimitExceeded, tr("Declared ZIP member sizes exceed the uncompressed-byte budget."), qint64(absolute), entry.path);
            const quint64 denominator = qMax<quint64>(1, entry.compressedSize);
            if (entry.uncompressedSize / denominator > budget.maxCompressionRatio
                || (entry.uncompressedSize / denominator == budget.maxCompressionRatio && entry.uncompressedSize % denominator))
                return fail(ErrorCode::LimitExceeded, tr("A ZIP member exceeds the declared compression-ratio budget."), qint64(absolute), entry.path);
            const QDate modifiedDate(1980 + (date >> 9), (date >> 5) & 15, date & 31);
            const QTime modifiedTime(time >> 11, (time >> 5) & 63, (time & 31) * 2);
            // Store floating wall-clock values consistently across machines.
            if (modifiedDate.isValid() && modifiedTime.isValid()) entry.modified = QDateTime(modifiedDate, modifiedTime, Qt::UTC);
            Span span;
            if (!localHeader(entry, flags, version, time, date, centralOffset, &span)) return false;
            spans.append(span);
            if (!insertEntry(entry)) return false;
            result.totalUncompressedSize += entry.uncompressedSize;
            result.totalCompressedSize += entry.compressedSize;
            ++result.storedEntryCount;
            cursor += recordLength;
        }
        if (cursor != central.size())
            return fail(ErrorCode::Corrupt, tr("The central directory contains uncounted records or unsupported trailing data."), qint64(centralOffset + cursor));
        std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.begin < b.begin; });
        quint64 previousEnd = 0;
        for (const Span &span : spans) {
            if (span.begin < previousEnd)
                return fail(ErrorCode::Corrupt, tr("ZIP local headers or member data overlap."), qint64(span.begin), span.path);
            // Gaps can conceal unlisted local entries, SFX code, or obsolete data.
            if (span.begin != previousEnd)
                return fail(ErrorCode::Unsupported, tr("ZIP archives with prepended or unlisted data are not supported."), qint64(previousEnd), span.path);
            previousEnd = span.end;
        }
        if (previousEnd != centralOffset)
            return fail(ErrorCode::Corrupt, tr("Unlisted data appears before the ZIP central directory."), qint64(previousEnd));
        if (!checkCancelled()) return false;
        if (file.size() != qint64(fileSize) || QFileInfo(file.fileName()).lastModified() != before.lastModified())
            return fail(ErrorCode::Io, tr("The archive changed while it was being read; reload it."));
        std::sort(result.entries.begin(), result.entries.end(), [](const Entry &a, const Entry &b) { return a.path < b.path; });
        return true;
    }

    QFile file;
    const Limits &budget;
    const std::function<bool()> &cancelled;
    Directory result;
    quint64 fileSize = 0;
    QHash<QString, int> paths;
};

} // namespace

bool normalizeEntryPath(const QString &raw, QString *normalized, QString *reason, const Limits &limits, ErrorCode *errorCode)
{
    if (normalized) normalized->clear();
    if (reason) reason->clear();
    if (errorCode) *errorCode = ErrorCode::UnsafePath;
    const auto overBudget = [&](const QString &message) {
        if (errorCode) *errorCode = ErrorCode::LimitExceeded;
        return pathFailure(reason, message);
    };
    if (raw.isEmpty()) return pathFailure(reason, tr("The ZIP member path is empty."));
    if (raw.toUtf8().size() > limits.maxPathBytes)
        return overBudget(tr("The ZIP member path exceeds the path-byte budget."));
    QString path = raw.normalized(QString::NormalizationForm_C);
    path.replace('\\', '/');
    if (path.startsWith('/')) return pathFailure(reason, tr("Absolute or UNC ZIP paths are refused: %1").arg(raw));
    for (int i = 0; i < path.size(); ++i) {
        const QChar c = path[i];
        uint codePoint = c.unicode();
        if (c.isHighSurrogate()) {
            if (i + 1 >= path.size() || !path.at(i + 1).isLowSurrogate())
                return pathFailure(reason, tr("An invalid Unicode surrogate appears in a ZIP path."));
            codePoint = QChar::surrogateToUcs4(c, path.at(++i));
        } else if (c.isLowSurrogate()) return pathFailure(reason, tr("An invalid Unicode surrogate appears in a ZIP path."));
        const auto category = QChar::category(codePoint);
        if (category == QChar::Other_Control || category == QChar::Other_Format
            || category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
            return pathFailure(reason, tr("Control or invisible formatting characters are refused in ZIP paths."));
    }
    QStringList parts;
    const auto segments = path.split('/');
    for (const QString &part : segments) {
        if (part == QStringLiteral("..")) return pathFailure(reason, tr("Parent traversal '..' is refused in ZIP paths: %1").arg(raw));
        if (part.isEmpty() || part == QStringLiteral(".")) continue;
        for (const QChar c : part) {
            if (QStringLiteral(":<>\"|?*").contains(c))
                return pathFailure(reason, tr("Drive prefixes, streams, and reserved filename characters are refused in ZIP paths: %1").arg(raw));
        }
        if (part.endsWith(' ') || part.endsWith('.'))
            return pathFailure(reason, tr("Trailing spaces or dots are refused in ZIP path components: %1").arg(raw));
        const QString base = part.section('.', 0, 0).toUpper();
        if (base == QStringLiteral("CON") || base == QStringLiteral("PRN") || base == QStringLiteral("AUX")
            || base == QStringLiteral("NUL") || base == QStringLiteral("CLOCK$")
            || ((base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT")))
                && base.size() == 4 && QStringLiteral("123456789¹²³").contains(base[3])))
            return pathFailure(reason, tr("Reserved device names are refused in ZIP paths: %1").arg(raw));
        parts.append(part);
        if (parts.size() > limits.maxPathDepth) return overBudget(tr("The ZIP path exceeds the directory-depth budget."));
    }
    if (parts.isEmpty()) return pathFailure(reason, tr("The ZIP member path normalizes to an empty path."));
    path = parts.join('/');
    if (path.toUtf8().size() > limits.maxPathBytes) return overBudget(tr("The normalized ZIP path exceeds the path-byte budget."));
    if (normalized) *normalized = path;
    if (errorCode) *errorCode = ErrorCode::None;
    return true;
}

Directory readZip(const QString &path, const Limits &limits, const std::function<bool()> &isCancelled)
{
    return ZipReader(path, limits, isCancelled).run();
}

QString metadataNotice()
{
    return tr("Read-only ZIP directory comparison. Sizes, CRC and timestamps are archive metadata; matching records do not prove equal file contents. Payloads are not decompressed or CRC-verified.");
}

QString differenceLabel(Difference difference)
{
    switch (difference) {
    case Difference::LeftOnly: return tr("Left only");
    case Difference::RightOnly: return tr("Right only");
    case Difference::TypeMismatch: return tr("File/directory conflict");
    case Difference::SizeDifferent: return tr("Size record differs");
    case Difference::CrcDifferent: return tr("CRC record differs");
    case Difference::MetadataDifferent: return tr("Other metadata differs");
    case Difference::MatchingMetadata: return tr("Matching metadata (contents unverified)");
    }
    return QString();
}

Comparison compare(const Directory &left, const Directory &right)
{
    Comparison result;
    result.left = left;
    result.right = right;
    if (!result.ok()) return result;
    QHash<QString, int> leftByPath, rightByPath;
    QSet<QString> names;
    for (int i = 0; i < left.entries.size(); ++i) { leftByPath.insert(left.entries[i].path, i); names.insert(left.entries[i].path); }
    for (int i = 0; i < right.entries.size(); ++i) { rightByPath.insert(right.entries[i].path, i); names.insert(right.entries[i].path); }
    QStringList ordered = names.values();
    std::sort(ordered.begin(), ordered.end());
    for (const QString &path : ordered) {
        Row row;
        row.path = path;
        row.leftIndex = leftByPath.value(path, -1);
        row.rightIndex = rightByPath.value(path, -1);
        if (row.leftIndex < 0) row.difference = Difference::RightOnly;
        else if (row.rightIndex < 0) row.difference = Difference::LeftOnly;
        else {
            const Entry &a = left.entries[row.leftIndex], &b = right.entries[row.rightIndex];
            if (a.directory != b.directory) row.difference = Difference::TypeMismatch;
            else if (a.uncompressedSize != b.uncompressedSize) row.difference = Difference::SizeDifferent;
            else if (a.crc32 != b.crc32) row.difference = Difference::CrcDifferent;
            else if (!(a.directory && (a.implicitDirectory || b.implicitDirectory))
                     && (a.modified != b.modified || a.compressedSize != b.compressedSize || a.method != b.method))
                row.difference = Difference::MetadataDifferent;
        }
        row.evidence = differenceLabel(row.difference);
        if (row.difference == Difference::MatchingMetadata && row.leftIndex >= 0 && left.entries[row.leftIndex].directory)
            row.evidence = tr("Directory exists on both sides; child entries are compared separately.");
        else if (row.difference != Difference::LeftOnly && row.difference != Difference::RightOnly && row.difference != Difference::TypeMismatch)
            row.evidence += tr("; payload contents unverified");
        if (row.difference != Difference::MatchingMetadata) ++result.differenceCount;
        result.rows.append(row);
    }
    return result;
}

} } // namespace LqCompare::Archive
