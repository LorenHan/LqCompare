#ifndef LQCOMPARE_TEST_MEDIA_FIXTURES_H
#define LQCOMPARE_TEST_MEDIA_FIXTURES_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// These are metadata fixtures with synthetic audio bytes. They deliberately do
// not claim to be playable recordings and require no audio device or codec.
namespace MediaFixtures {

inline QByteArray be(quint64 value, int width)
{
    QByteArray bytes(width, '\0');
    for (int i = width - 1; i >= 0; --i) {
        bytes[i] = char(value & 0xff);
        value >>= 8;
    }
    return bytes;
}

inline QByteArray le32(quint32 value)
{
    QByteArray bytes;
    for (int i = 0; i < 4; ++i)
        bytes.append(char((value >> (8 * i)) & 0xff));
    return bytes;
}

inline QByteArray syncsafe(quint32 value)
{
    QByteArray bytes;
    for (int shift : {21, 14, 7, 0})
        bytes.append(char((value >> shift) & 0x7f));
    return bytes;
}

inline QByteArray mpegPayload()
{
    // MPEG-1 Layer III, 128 kbit/s, 44.1 kHz; one 417-byte frame placeholder.
    return QByteArray::fromHex("fffb9064") + QByteArray(413, '\0');
}

inline QByteArray fixed(const QByteArray &text, int length)
{
    return text.left(length).leftJustified(length, '\0');
}

inline QByteArray id3v1(const QByteArray &title = "Title", const QByteArray &artist = "Artist",
                       const QByteArray &album = "Album", const QByteArray &date = "1999",
                       const QByteArray &comment = "Comment", int track = 7, int genre = 17)
{
    QByteArray bytes("TAG");
    bytes += fixed(title, 30) + fixed(artist, 30) + fixed(album, 30) + fixed(date, 4);
    if (track > 0)
        bytes += fixed(comment, 28) + QByteArray(1, '\0') + QByteArray(1, char(track));
    else
        bytes += fixed(comment, 30);
    return bytes + QByteArray(1, char(genre));
}

inline QByteArray textPayload(int encoding, const QString &text)
{
    QByteArray bytes(1, char(encoding));
    if (encoding == 0)
        return bytes + text.toLatin1();
    if (encoding == 3)
        return bytes + text.toUtf8();
    if (encoding == 1)
        bytes += QByteArray::fromHex("fffe");
    for (QChar c : text) {
        if (encoding == 1) {
            bytes.append(char(c.unicode() & 0xff));
            bytes.append(char(c.unicode() >> 8));
        } else {
            bytes.append(char(c.unicode() >> 8));
            bytes.append(char(c.unicode() & 0xff));
        }
    }
    return bytes;
}

inline QByteArray id3Frame(int version, const QByteArray &id, const QByteArray &payload, int flags = 0)
{
    if (version == 2)
        return id + be(payload.size(), 3) + payload;
    return id + (version == 4 ? syncsafe(payload.size()) : be(payload.size(), 4))
            + be(flags, 2) + payload;
}

inline QByteArray id3Tag(int version, const QByteArray &frames, int flags = 0)
{
    return QByteArray("ID3") + QByteArray(1, char(version)) + QByteArray(1, '\0')
            + QByteArray(1, char(flags)) + syncsafe(frames.size()) + frames;
}

inline QByteArray flacStreamInfo(int sampleRate = 44100, int channels = 2,
                                  int bits = 16, quint64 samples = 88200)
{
    const quint64 audioInfo = (quint64(sampleRate) << 44) | (quint64(channels - 1) << 41)
            | (quint64(bits - 1) << 36) | samples;
    return be(16, 2) + be(4096, 2) + QByteArray(6, '\0') + be(audioInfo, 8) + QByteArray(16, '\0');
}

inline QByteArray flacBlock(int type, const QByteArray &payload, bool last)
{
    return QByteArray(1, char(type | (last ? 0x80 : 0))) + be(payload.size(), 3) + payload;
}

inline QByteArray vorbisComments(const QStringList &entries, const QByteArray &vendor = "LqCompare fixture")
{
    QByteArray bytes = le32(vendor.size()) + vendor + le32(entries.size());
    for (const QString &entry : entries) {
        const QByteArray encoded = entry.toUtf8();
        bytes += le32(encoded.size()) + encoded;
    }
    return bytes;
}

inline QByteArray flac(const QStringList &comments)
{
    return QByteArray("fLaC") + flacBlock(0, flacStreamInfo(), false)
            + flacBlock(4, vorbisComments(comments), true);
}

}
#endif
