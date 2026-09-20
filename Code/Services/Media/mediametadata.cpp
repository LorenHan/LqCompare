#include "mediametadata.h"
#include "flacmetadata.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextCodec>

namespace LqCompare { namespace Media {
namespace {
quint8 byte(char value) { return static_cast<quint8>(value); }
quint32 big(const QByteArray &data, int offset, int count)
{
    quint32 value = 0;
    for (int i = 0; i < count; ++i) value = (value << 8) | byte(data[offset + i]);
    return value;
}
bool syncsafe(const QByteArray &data, int offset, quint32 *value)
{
    *value = 0;
    for (int i = 0; i < 4; ++i) {
        const quint8 b = byte(data[offset + i]);
        if (b & 0x80) return false;
        *value = (*value << 7) | b;
    }
    return true;
}
bool fail(Document *doc, ReadStatus status, const QString &message)
{
    doc->status = status;
    doc->message = message;
    return false;
}
void partial(Document *doc, const QString &warning)
{
    doc->status = ReadStatus::Partial;
    // One entry per reason, not per frame: malformed files cannot fill the UI with warnings.
    if (!doc->warnings.contains(warning)) doc->warnings.append(warning);
}
bool allZero(const QByteArray &data, int offset)
{
    for (int i = offset; i < data.size(); ++i) if (data[i] != '\0') return false;
    return true;
}
QByteArray deUnsynchronise(const QByteArray &data)
{
    QByteArray result;
    result.reserve(data.size());
    for (int i = 0; i < data.size(); ++i) {
        result.append(data[i]);
        if (byte(data[i]) == 0xff && i + 1 < data.size() && data[i + 1] == '\0') ++i;
    }
    return result;
}

// Decode without replacement characters: silently replacing damaged bytes would turn two
// different corrupt values into an apparently equal tag. UTF-16 BOM and surrogate checks
// are explicit so behavior does not depend on the machine's locale or byte order.
bool decode(const QByteArray &data, int encoding, QString *text)
{
    if (encoding == 0) { *text = QString::fromLatin1(data); return true; }
    if (encoding == 3) {
        QTextCodec::ConverterState state;
        *text = QTextCodec::codecForName("UTF-8")->toUnicode(data.constData(), data.size(), &state);
        return state.invalidChars == 0 && state.remainingChars == 0;
    }
    bool little = false;
    int offset = 0;
    if (encoding == 1) {
        if (data.size() < 2) return false;
        if (byte(data[0]) == 0xff && byte(data[1]) == 0xfe) little = true;
        else if (byte(data[0]) != 0xfe || byte(data[1]) != 0xff) return false;
        offset = 2;
    } else if (encoding != 2) return false;
    if ((data.size() - offset) % 2) return false;
    QString result;
    result.reserve((data.size() - offset) / 2);
    const auto unit = [&](int pos) -> ushort {
        return little ? ushort(byte(data[pos]) | (byte(data[pos + 1]) << 8))
                      : ushort((byte(data[pos]) << 8) | byte(data[pos + 1]));
    };
    for (int i = offset; i < data.size(); i += 2) {
        const ushort code = unit(i);
        if (QChar::isHighSurrogate(code)) {
            if (i + 3 >= data.size() || !QChar::isLowSurrogate(unit(i + 2))) return false;
            result.append(QChar(code)); result.append(QChar(unit(i + 2))); i += 2;
        } else {
            if (QChar::isLowSurrogate(code) || code == 0xfffe) return false;
            result.append(QChar(code));
        }
    }
    *text = result;
    return true;
}
QStringList values(QString text)
{
    while (text.endsWith(QChar(0))) text.chop(1);
    QStringList result = text.split(QChar(0), Qt::KeepEmptyParts);
    for (QString &value : result) if (value.startsWith(QChar(0xfeff))) value.remove(0, 1);
    return result;
}
QString canonical(const QString &frame)
{
    static const QMap<QString, QString> keys = {
        {QStringLiteral("TT2"), QStringLiteral("title")}, {QStringLiteral("TIT2"), QStringLiteral("title")},
        {QStringLiteral("TP1"), QStringLiteral("artist")}, {QStringLiteral("TPE1"), QStringLiteral("artist")},
        {QStringLiteral("TAL"), QStringLiteral("album")}, {QStringLiteral("TALB"), QStringLiteral("album")},
        {QStringLiteral("TP2"), QStringLiteral("album_artist")}, {QStringLiteral("TPE2"), QStringLiteral("album_artist")},
        {QStringLiteral("TRK"), QStringLiteral("track")}, {QStringLiteral("TRCK"), QStringLiteral("track")},
        {QStringLiteral("TYE"), QStringLiteral("date")}, {QStringLiteral("TYER"), QStringLiteral("date")},
        {QStringLiteral("TDRC"), QStringLiteral("date")},
        {QStringLiteral("TCO"), QStringLiteral("genre")}, {QStringLiteral("TCON"), QStringLiteral("genre")},
        {QStringLiteral("TCM"), QStringLiteral("composer")}, {QStringLiteral("TCOM"), QStringLiteral("composer")},
        {QStringLiteral("TCR"), QStringLiteral("copyright")}, {QStringLiteral("TCOP"), QStringLiteral("copyright")},
        {QStringLiteral("TEN"), QStringLiteral("encoder")}, {QStringLiteral("TENC"), QStringLiteral("encoder")}
    };
    return keys.value(frame, QStringLiteral("id3:") + frame);
}
bool parseTextFrame(const QString &id, const QByteArray &payload, int version, Document *doc,
                    const ReadLimits &limits)
{
    const bool comment = id == QStringLiteral("COMM") || id == QStringLiteral("COM");
    const bool userText = id == QStringLiteral("TXXX") || id == QStringLiteral("TXX");
    if (!id.startsWith(QLatin1Char('T')) && !comment) {
        partial(doc, QStringLiteral("Non-text ID3 frames (including artwork/lyrics) were not compared."));
        return true;
    }
    if (payload.size() > limits.maxFieldBytes)
        return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("ID3 text frame exceeds the field byte limit."));
    if (payload.isEmpty()) return fail(doc, ReadStatus::Malformed, QStringLiteral("Empty ID3 text frame."));
    const int encoding = byte(payload[0]);
    if (encoding > (version == 4 ? 3 : 1))
        return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid text encoding for this ID3 version."));
    QString language;
    int offset = 1;
    if (comment) {
        if (payload.size() < 4) return fail(doc, ReadStatus::Malformed, QStringLiteral("Truncated ID3 comment frame."));
        language = QString::fromLatin1(payload.mid(1, 3));
        for (int i = 1; i < 4; ++i) {
            if (byte(payload[i]) < 'a' || byte(payload[i]) > 'z')
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 comment language."));
        }
        offset = 4;
    }
    const QByteArray encoded = payload.mid(offset);
    QString text;
    QString key = comment ? QStringLiteral("comment") : canonical(id);
    if (comment || userText) {
        const int width = encoding == 1 || encoding == 2 ? 2 : 1;
        int separator = -1;
        for (int i = 0; i + width <= encoded.size(); i += width) {
            if (encoded[i] == '\0' && (width == 1 || encoded[i + 1] == '\0')) {
                separator = i;
                break;
            }
        }
        if (separator < 0)
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Missing ID3 text description terminator."));
        const QByteArray descriptionBytes = encoded.left(separator);
        QByteArray textBytes = encoded.mid(separator + width);
        QString description;
        // Empty UTF-16 descriptions may consist solely of their two-byte terminator.
        // Nonempty strings still need a BOM, or inherit the byte order declared by
        // the description in this same frame (used by common ID3v2 writers).
        if (!descriptionBytes.isEmpty() && !decode(descriptionBytes, encoding, &description))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Malformed ID3 text description encoding."));
        if (encoding == 1 && descriptionBytes.size() >= 2 && textBytes.size() >= 2
            && (textBytes.startsWith(QByteArray::fromHex("fffe")) || textBytes.startsWith(QByteArray::fromHex("feff")))
            && descriptionBytes.left(2) != textBytes.left(2))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Inconsistent UTF-16 byte order within an ID3 frame."));
        if (encoding == 1 && !textBytes.isEmpty()
            && !textBytes.startsWith(QByteArray::fromHex("fffe"))
            && !textBytes.startsWith(QByteArray::fromHex("feff"))
            && descriptionBytes.size() >= 2)
            textBytes.prepend(descriptionBytes.left(2));
        if (!textBytes.isEmpty() && !decode(textBytes, encoding, &text))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Malformed ID3 text encoding or missing UTF-16 BOM."));
        if (userText) key = QStringLiteral("id3:TXXX:") + description;
        else if (!description.isEmpty() || language != QStringLiteral("eng"))
            key += QLatin1Char(':') + language + QLatin1Char(':') + description;
    } else if (!decode(encoded, encoding, &text)) {
        return fail(doc, ReadStatus::Malformed, QStringLiteral("Malformed ID3 text encoding or missing UTF-16 BOM."));
    }
    while (text.endsWith(QChar(0))) text.chop(1);
    int existingValues = 0;
    for (auto it = doc->tags.constBegin(); it != doc->tags.constEnd(); ++it)
        existingValues += it.value().size();
    if (text.count(QChar(0)) + 1 > limits.maxFields - existingValues)
        return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("ID3 text value count exceeds the field limit."));
    doc->tags[key].append(values(text));
    return true;
}

bool extendedHeader(QByteArray *body, int version, Document *doc)
{
    if (body->size() < 4)
        return fail(doc, ReadStatus::Malformed, QStringLiteral("Truncated ID3 extended header."));
    quint32 size = 0;
    if (version == 3) {
        size = big(*body, 0, 4);
        if ((size != 6 && size != 10) || qint64(size) + 4 > body->size())
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3v2.3 extended header length."));
        const quint32 flags = big(*body, 4, 2);
        if ((flags & ~0x8000u) || bool(flags & 0x8000u) != (size == 10))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3v2.3 extended header flags."));
        const quint32 padding = big(*body, 6, 4);
        if (padding > quint32(body->size()) - size - 4)
            return fail(doc, ReadStatus::Malformed, QStringLiteral("ID3 padding length exceeds tag bounds."));
        if (flags) partial(doc, QStringLiteral("The optional ID3 CRC has not been verified."));
        body->remove(0, int(size) + 4);
    } else {
        if (!syncsafe(*body, 0, &size) || size < 6 || size > quint32(body->size())
            || byte((*body)[4]) != 1 || (byte((*body)[5]) & ~0x70))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3v2.4 extended header."));
        const quint8 flags = byte((*body)[5]);
        if (flags & 0x40)
            partial(doc, QStringLiteral("ID3v2.4 update-tag semantics require a preceding tag and are not resolved."));
        int pos = 6;
        for (const auto flag : {0x40, 0x20, 0x10}) {
            if (!(flags & flag)) continue;
            const int expected = flag == 0x40 ? 0 : flag == 0x20 ? 5 : 1;
            if (pos >= int(size) || byte((*body)[pos++]) != expected || expected > int(size) - pos)
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 extended header data length."));
            if (flag == 0x20) {
                for (int i = 0; i < 5; ++i) if (byte((*body)[pos + i]) & 0x80)
                    return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 CRC encoding."));
                if (byte((*body)[pos]) > 0x0f)
                    return fail(doc, ReadStatus::Malformed, QStringLiteral("ID3 CRC exceeds 32 bits."));
                partial(doc, QStringLiteral("The optional ID3 CRC has not been verified."));
            }
            pos += expected;
        }
        if (pos != int(size))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Unexpected ID3 extended header bytes."));
        body->remove(0, int(size));
    }
    return true;
}

bool readId3v2(QFile &file, Document *doc, const ReadLimits &limits, qint64 *tagEnd)
{
    const QByteArray header = file.read(10);
    if (header.size() != 10)
        return fail(doc, ReadStatus::Malformed, QStringLiteral("Truncated ID3 header."));
    const int version = byte(header[3]);
    if (version < 2 || version > 4)
        return fail(doc, ReadStatus::Unsupported, QStringLiteral("Only ID3v2.2, v2.3 and v2.4 are supported."));
    const quint8 flags = byte(header[5]);
    const quint8 allowed = version == 2 ? 0xc0 : version == 3 ? 0xe0 : 0xf0;
    if (byte(header[4]) == 0xff || (flags & ~allowed))
        return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 header version or flags."));
    if (version == 2 && (flags & 0x40))
        return fail(doc, ReadStatus::Unsupported, QStringLiteral("Compressed ID3v2.2 tags are not supported."));
    quint32 size = 0;
    if (!syncsafe(header, 6, &size))
        return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 synchsafe tag length."));
    if (qint64(size) > limits.maxTagBytes)
        return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("ID3 tag exceeds the metadata byte limit."));
    const bool footer = version == 4 && (flags & 0x10);
    *tagEnd = 10 + qint64(size) + (footer ? 10 : 0);
    if (*tagEnd > file.size())
        return fail(doc, ReadStatus::Malformed, QStringLiteral("ID3 tag length extends past the end of the file."));
    QByteArray body = file.read(size);
    if (body.size() != int(size)) return fail(doc, ReadStatus::IoError, QStringLiteral("Could not read ID3 tag bytes."));
    if (footer) {
        QByteArray expected = header;
        expected.replace(0, 3, "3DI");
        if (file.read(10) != expected)
            return fail(doc, ReadStatus::Malformed, QStringLiteral("ID3v2.4 footer does not match its header."));
    }
    if ((flags & 0x80) && version < 4) body = deUnsynchronise(body);
    if (version >= 3 && (flags & 0x40) && !extendedHeader(&body, version, doc)) return false;
    if (flags & 0x20) partial(doc, QStringLiteral("ID3 experimental tags may contain unsupported semantics."));
    doc->technical[QStringLiteral("id3_version")] = QStringList{QStringLiteral("2.%1.%2").arg(version).arg(byte(header[4]))};
    int pos = 0;
    int fields = 0;
    const int headerSize = version == 2 ? 6 : 10;
    while (pos < body.size()) {
        if (body[pos] == '\0') {
            if (footer)
                return fail(doc, ReadStatus::Malformed, QStringLiteral("An ID3v2.4 tag with a footer must not contain padding."));
            if (!allZero(body, pos))
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Nonzero bytes after ID3 padding."));
            break;
        }
        if (++fields > limits.maxFields)
            return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("ID3 frame count exceeds the limit."));
        if (body.size() - pos < headerSize)
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Truncated ID3 frame header."));
        const int idLength = version == 2 ? 3 : 4;
        for (int i = 0; i < idLength; ++i) {
            const quint8 c = byte(body[pos + i]);
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 frame identifier."));
        }
        const QString id = QString::fromLatin1(body.mid(pos, idLength));
        quint32 frameSize = 0;
        if (version == 4) {
            if (!syncsafe(body, pos + 4, &frameSize))
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3v2.4 frame length."));
        } else frameSize = big(body, pos + idLength, version == 2 ? 3 : 4);
        if (frameSize == 0 || frameSize > quint32(body.size() - pos - headerSize))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("ID3 frame length exceeds tag bounds or is zero."));
        const quint8 statusFlags = version == 2 ? 0 : byte(body[pos + 8]);
        const quint8 frameFlags = version == 2 ? 0 : byte(body[pos + 9]);
        if ((version == 3 && ((statusFlags & ~0xe0) || (frameFlags & ~0xe0)))
            || (version == 4 && ((statusFlags & ~0x70) || (frameFlags & ~0x4f))))
            return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 frame flags."));
        const int payloadOffset = pos + headerSize;
        pos += headerSize + int(frameSize);
        if ((version == 3 && (frameFlags & 0xc0)) || (version == 4 && (frameFlags & 0x0c))) {
            if (version == 4 && (frameFlags & 0x08) && !(frameFlags & 0x01))
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Compressed ID3v2.4 frame lacks a data length indicator."));
            partial(doc, QStringLiteral("Compressed or encrypted ID3 frames were not compared."));
            continue;
        }
        if (!id.startsWith(QLatin1Char('T')) && id != QStringLiteral("COMM") && id != QStringLiteral("COM")) {
            partial(doc, QStringLiteral("Non-text ID3 frames (including artwork/lyrics) were not compared."));
            continue;
        }
        if (frameSize > quint32(limits.maxFieldBytes))
            return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("ID3 text frame exceeds the field byte limit."));
        QByteArray payload = body.mid(payloadOffset, int(frameSize));
        if (version == 4 && ((flags & 0x80) || (frameFlags & 0x02))) payload = deUnsynchronise(payload);
        if ((version == 3 && (frameFlags & 0x20)) || (version == 4 && (frameFlags & 0x40))) {
            if (payload.isEmpty()) return fail(doc, ReadStatus::Malformed, QStringLiteral("Missing ID3 grouping identifier."));
            payload.remove(0, 1);
        }
        if (version == 4 && (frameFlags & 0x01)) {
            quint32 length = 0;
            if (payload.size() < 4 || !syncsafe(payload, 0, &length) || length != quint32(payload.size() - 4))
                return fail(doc, ReadStatus::Malformed, QStringLiteral("Invalid ID3 frame data length indicator."));
            payload.remove(0, 4);
        }
        if (!parseTextFrame(id, payload, version, doc, limits)) return false;
    }
    return true;
}

QString fixedText(const QByteArray &bytes)
{
    int size = bytes.size();
    while (size > 0 && (bytes[size - 1] == '\0' || bytes[size - 1] == ' ')) --size;
    return QString::fromLatin1(bytes.constData(), size);
}
bool readId3v1(const QByteArray &tag, Document *doc, const ReadLimits &limits)
{
    const bool version11 = tag[125] == '\0' && tag[126] != '\0';
    const QMap<QString, QString> fallback = {
        {QStringLiteral("title"), fixedText(tag.mid(3, 30))},
        {QStringLiteral("artist"), fixedText(tag.mid(33, 30))},
        {QStringLiteral("album"), fixedText(tag.mid(63, 30))},
        {QStringLiteral("date"), fixedText(tag.mid(93, 4))},
        {QStringLiteral("comment"), fixedText(tag.mid(97, version11 ? 28 : 30))}
    };
    bool conflicting = false;
    for (auto it = fallback.constBegin(); it != fallback.constEnd(); ++it) {
        if (it.value().isEmpty()) continue;
        if (it.value().size() > limits.maxFieldBytes)
            return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("ID3v1 text exceeds the field byte limit."));
        if (!doc->tags.contains(it.key())) doc->tags[it.key()] = QStringList{it.value()};
        else if (doc->tags.value(it.key()) != QStringList{it.value()}) {
            // Preserve the older value too; hiding a contradictory v1 tag loses information.
            doc->tags[QStringLiteral("id3v1:") + it.key()] = QStringList{it.value()};
            conflicting = true;
        }
    }
    const auto appendFallback = [&](const QString &key, const QString &value) {
        if (!doc->tags.contains(key)) doc->tags[key] = QStringList{value};
        else if (doc->tags.value(key) != QStringList{value}) {
            doc->tags[QStringLiteral("id3v1:") + key] = QStringList{value}; conflicting = true;
        }
    };
    if (version11) appendFallback(QStringLiteral("track"), QString::number(byte(tag[126])));
    if (byte(tag[127]) != 255) appendFallback(QStringLiteral("genre"), QString::number(byte(tag[127])));
    int fieldValues = 0;
    for (auto it = doc->tags.constBegin(); it != doc->tags.constEnd(); ++it)
        fieldValues += it.value().size();
    if (fieldValues > limits.maxFields)
        return fail(doc, ReadStatus::LimitExceeded, QStringLiteral("Combined ID3 field count exceeds the limit."));
    doc->technical[QStringLiteral("id3v1_version")] = QStringList{version11 ? QStringLiteral("1.1") : QStringLiteral("1.0")};
    if (conflicting) doc->warnings.append(QStringLiteral("Conflicting ID3v1 values are preserved under id3v1: fields; ID3v2 has display priority."));
    doc->warnings.append(QStringLiteral("ID3v1 text is interpreted as Latin-1; legacy local encodings are not auto-detected. Genre is its numeric ID."));
    return true;
}
bool mpegHeader(const QByteArray &data)
{
    if (data.size() < 4 || byte(data[0]) != 0xff || (byte(data[1]) & 0xe0) != 0xe0) return false;
    const int version = (byte(data[1]) >> 3) & 3;
    const int layer = (byte(data[1]) >> 1) & 3;
    const int bitrate = (byte(data[2]) >> 4) & 15;
    const int rate = (byte(data[2]) >> 2) & 3;
    return version != 1 && layer == 1 && bitrate != 0 && bitrate != 15 && rate != 3
        && (byte(data[3]) & 3) != 2;
}
}

bool load(const QString &path, Document *document, QString *error, const ReadLimits &limits)
{
    if (error) error->clear();
    if (!document) { if (error) *error = QStringLiteral("Missing metadata output document."); return false; }
    *document = {};
    document->path = QFileInfo(path).absoluteFilePath();
    document->status = ReadStatus::Ready;
    const auto finish = [&]() {
        const bool ok = document->usable();
        if (!ok) {
            // An error may follow many valid fields; expose no misleading partial equality.
            document->tags.clear(); document->technical.clear();
            if (error) *error = document->message;
        } else document->message = document->status == ReadStatus::Ready
            ? QStringLiteral("Supported tag metadata read successfully; audio content was not validated.")
            : QStringLiteral("Only supported tag fields were read; some metadata was not compared.");
        return ok;
    };
    if (limits.maxTagBytes <= 0 || limits.maxTagBytes > 64 * 1024 * 1024
        || limits.maxFieldBytes <= 0 || limits.maxFieldBytes > 16 * 1024 * 1024
        || limits.maxFields <= 0 || limits.maxFields > 100000
        || limits.maxMetadataBlocks <= 0 || limits.maxMetadataBlocks > 65536) {
        fail(document, ReadStatus::LimitExceeded, QStringLiteral("Invalid or unsafe metadata read limits."));
        return finish();
    }
    QFile file(path);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
        fail(document, ReadStatus::IoError, QStringLiteral("Cannot open a regular media file: %1").arg(path));
        return finish();
    }
    document->fileSize = file.size();
    const QByteArray prefix = file.peek(10);
    if (prefix.startsWith("fLaC")) {
        Internal::readFlac(file, document, limits);
        return finish();
    }
    QByteArray v1;
    if (file.size() >= 128 && file.seek(file.size() - 128)) {
        const QByteArray tail = file.read(128);
        if (tail.size() != 128) {
            fail(document, file.error() == QFileDevice::NoError ? ReadStatus::Malformed : ReadStatus::IoError,
                 QStringLiteral("Media file trailer was truncated or could not be read."));
            return finish();
        }
        if (tail.startsWith("TAG")) v1 = tail;
    }
    if (!file.seek(0)) {
        fail(document, ReadStatus::IoError, QStringLiteral("Could not seek to the media metadata header."));
        return finish();
    }
    const bool v2 = prefix.startsWith("ID3");
    if (!v2 && v1.isEmpty() && !mpegHeader(prefix)) {
        fail(document, ReadStatus::Unsupported,
             QStringLiteral("Unsupported media format. This reader supports MP3 ID3v1/v2 and native FLAC Vorbis comments; OGG, M4A/AAC, WAV, MP4 and MKV are not implemented."));
        return finish();
    }
    document->format = QStringLiteral("MP3");
    qint64 tagEnd = 0;
    if (v2 && !readId3v2(file, document, limits, &tagEnd)) return finish();
    if (!v1.isEmpty()) {
        if (tagEnd > file.size() - 128) {
            fail(document, ReadStatus::Malformed, QStringLiteral("ID3v2 overlaps the ID3v1 trailer."));
            return finish();
        }
        if (128 > limits.maxTagBytes || qint64(128) + qMax<qint64>(0, tagEnd - 10) > limits.maxTagBytes) {
            fail(document, ReadStatus::LimitExceeded, QStringLiteral("Combined ID3 metadata exceeds the byte limit."));
            return finish();
        }
        if (!readId3v1(v1, document, limits)) return finish();
    }
    if (!v2 && v1.isEmpty()) {
        // APEv2/Lyrics tags may still exist, so no-ID3 is not proof of absence of all tags.
        partial(document, QStringLiteral("No supported ID3 tag found. Other tag schemes and audio data were not inspected."));
    }
    return finish();
}

QString statusName(ReadStatus status)
{
    switch (status) {
    case ReadStatus::Ready: return QStringLiteral("Supported");
    case ReadStatus::Partial: return QStringLiteral("Partially supported");
    case ReadStatus::Unsupported: return QStringLiteral("Unsupported");
    case ReadStatus::Malformed: return QStringLiteral("Malformed metadata");
    case ReadStatus::IoError: return QStringLiteral("File read error");
    case ReadStatus::LimitExceeded: return QStringLiteral("Metadata safety limit exceeded");
    }
    return {};
}
QString supportDescription()
{
    return QStringLiteral("Read-only MP3 ID3v1 / ID3v2.2–2.4 and native FLAC Vorbis comments. "
                          "Tag equality does not imply audio equality. Artwork, playback, waveform, tag editing, "
                          "OGG, M4A/AAC, WAV, MP4 and MKV are not implemented.");
}
Comparison compare(const Document &left, const Document &right, bool ignoreTechnical)
{
    Comparison result;
    result.complete = left.complete() && right.complete();
    const auto append = [&](const QMap<QString, QStringList> &a, const QMap<QString, QStringList> &b, bool technical) {
        QSet<QString> keys;
        for (auto it = a.constBegin(); it != a.constEnd(); ++it) keys.insert(it.key());
        for (auto it = b.constBegin(); it != b.constEnd(); ++it) keys.insert(it.key());
        QStringList ordered = keys.values(); ordered.sort(Qt::CaseSensitive);
        for (const QString &key : ordered) {
            FieldDifference row;
            row.key = key; row.technical = technical; row.left = a.value(key); row.right = b.value(key);
            row.difference = !a.contains(key) ? Difference::RightOnly : !b.contains(key) ? Difference::LeftOnly
                             : row.left == row.right ? Difference::Equal : Difference::Changed;
            if (row.difference != Difference::Equal) ++result.differenceCount;
            result.fields.append(row);
        }
    };
    append(left.tags, right.tags, false);
    if (!ignoreTechnical) {
        auto a = left.technical, b = right.technical;
        if (left.fileSize >= 0) a[QStringLiteral("file_size")] = QStringList{QString::number(left.fileSize)};
        if (right.fileSize >= 0) b[QStringLiteral("file_size")] = QStringList{QString::number(right.fileSize)};
        if (!left.format.isEmpty()) a[QStringLiteral("format")] = QStringList{left.format};
        if (!right.format.isEmpty()) b[QStringLiteral("format")] = QStringList{right.format};
        append(a, b, true);
    }
    return result;
}

} }
