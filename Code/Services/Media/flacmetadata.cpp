#include "flacmetadata.h"

#include <QFile>

namespace LqCompare { namespace Media { namespace Internal {
namespace {

quint32 little32(const QByteArray &bytes)
{
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData());
    return quint32(p[0]) | (quint32(p[1]) << 8)
            | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// Reject overlong encodings, surrogate code points, invalid continuations,
// values above U+10FFFF, and truncated sequences before Qt can replace bytes.
bool isUtf8(const QByteArray &bytes)
{
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData());
    const int size = bytes.size();
    for (int i = 0; i < size;) {
        const unsigned char first = p[i++];
        if (first < 0x80)
            continue;
        int continuations = 0;
        unsigned char secondMin = 0x80;
        unsigned char secondMax = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            continuations = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuations = 2;
            if (first == 0xe0)
                secondMin = 0xa0;
            if (first == 0xed)
                secondMax = 0x9f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuations = 3;
            if (first == 0xf0)
                secondMin = 0x90;
            if (first == 0xf4)
                secondMax = 0x8f;
        } else {
            return false;
        }
        if (continuations > size - i || p[i] < secondMin || p[i] > secondMax)
            return false;
        ++i;
        for (int j = 1; j < continuations; ++j, ++i) {
            if (p[i] < 0x80 || p[i] > 0xbf)
                return false;
        }
    }
    return true;
}

QString tagKey(const QByteArray &key)
{
    const QString upper = QString::fromLatin1(key).toUpper();
    static const QMap<QString, QString> known {
        { QStringLiteral("TITLE"), QStringLiteral("title") },
        { QStringLiteral("ARTIST"), QStringLiteral("artist") },
        { QStringLiteral("ALBUM"), QStringLiteral("album") },
        { QStringLiteral("ALBUMARTIST"), QStringLiteral("album_artist") },
        { QStringLiteral("ALBUM_ARTIST"), QStringLiteral("album_artist") },
        { QStringLiteral("TRACKNUMBER"), QStringLiteral("track") },
        { QStringLiteral("DATE"), QStringLiteral("date") },
        { QStringLiteral("GENRE"), QStringLiteral("genre") },
        { QStringLiteral("COMMENT"), QStringLiteral("comment") },
        { QStringLiteral("DESCRIPTION"), QStringLiteral("comment") },
        { QStringLiteral("COMPOSER"), QStringLiteral("composer") },
        { QStringLiteral("COPYRIGHT"), QStringLiteral("copyright") },
        { QStringLiteral("ENCODER"), QStringLiteral("encoder") }
    };
    const auto it = known.constFind(upper);
    return it == known.constEnd() ? QStringLiteral("vorbis:") + upper : it.value();
}

class FlacReader
{
public:
    FlacReader(QFile &file, Document &document, const ReadLimits &limits)
        : m_file(file), m_document(document), m_limits(limits) {}

    bool read()
    {
        m_document.format = QStringLiteral("FLAC");
        if (!m_file.isOpen() || !(m_file.openMode() & QIODevice::ReadOnly)
                || m_file.isSequential())
            return fail(ReadStatus::IoError, QStringLiteral("FLAC 文件未以可定位的只读输入打开。"));
        if (m_limits.maxTagBytes < 0 || m_limits.maxFieldBytes < 0
                || m_limits.maxFields < 0 || m_limits.maxMetadataBlocks < 0)
            return fail(ReadStatus::LimitExceeded, QStringLiteral("FLAC 读取限制不能为负数。"));
        if (!m_file.seek(0))
            return ioFailure(QStringLiteral("无法定位到 FLAC 文件开头"));
        m_fileSize = m_file.size();
        if (m_fileSize < 0)
            return ioFailure(QStringLiteral("无法读取 FLAC 文件大小"));

        QByteArray signature;
        if (!readExact(4, &signature, QStringLiteral("FLAC 文件标识")))
            return false;
        if (signature != QByteArrayLiteral("fLaC"))
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC 文件标识无效。"));

        bool lastBlock = false;
        int blockCount = 0;
        while (!lastBlock) {
            if (blockCount >= m_limits.maxMetadataBlocks)
                return fail(ReadStatus::LimitExceeded, QStringLiteral("FLAC 元数据块数量超过限制。"));
            if (m_tagBytes > m_limits.maxTagBytes - qint64(4))
                return fail(ReadStatus::LimitExceeded, QStringLiteral("FLAC 元数据总长度超过限制。"));
            QByteArray header;
            if (!readExact(4, &header, QStringLiteral("FLAC 元数据块头（缺少末块或文件被截断）")))
                return false;
            const auto *p = reinterpret_cast<const unsigned char *>(header.constData());
            lastBlock = (p[0] & 0x80) != 0;
            const int type = p[0] & 0x7f;
            const qint64 length = (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
            ++blockCount;
            m_tagBytes += 4;
            if (length > m_limits.maxTagBytes - m_tagBytes)
                return fail(ReadStatus::LimitExceeded, QStringLiteral("FLAC 元数据总长度超过限制。"));
            m_tagBytes += length;
            if (length > m_fileSize - m_file.pos())
                return fail(ReadStatus::Malformed, QStringLiteral("FLAC 元数据块超出文件边界，文件可能被截断。"));
            if (blockCount == 1 && type != 0)
                return fail(ReadStatus::Malformed, QStringLiteral("FLAC 首个元数据块必须是 STREAMINFO。"));
            if (type == 127)
                return fail(ReadStatus::Malformed, QStringLiteral("FLAC 元数据块类型 127 无效。"));

            m_blockRemaining = length;
            if (type == 0) {
                if (blockCount != 1 || length != 34)
                    return fail(ReadStatus::Malformed, QStringLiteral("FLAC STREAMINFO 必须仅出现一次且长度为 34 字节。"));
                if (!readStreamInfo())
                    return false;
            } else if (type == 4) {
                if (m_hasComments)
                    return fail(ReadStatus::Malformed, QStringLiteral("FLAC 含有重复的 VORBIS_COMMENT 元数据块。"));
                m_hasComments = true;
                if (!readComments())
                    return false;
            } else {
                if (type == 3 && length % 18 != 0)
                    return fail(ReadStatus::Malformed, QStringLiteral("FLAC SEEKTABLE 长度不是 18 字节的整数倍。"));
                if (type != 1 && type != 3) {
                    m_partial = true;
                    const QString warning = type == 6
                            ? QStringLiteral("FLAC 封面图片未解析；当前仅比较可读标签和基础参数。")
                            : QStringLiteral("FLAC 元数据块类型 %1 未解析，完整元数据相等性尚未验证。").arg(type);
                    if (!m_document.warnings.contains(warning))
                        m_document.warnings.append(warning);
                }
                if (!m_file.seek(m_file.pos() + length))
                    return ioFailure(QStringLiteral("无法跳过 FLAC 元数据块"));
                // QFile::seek may succeed beyond EOF if another process truncated the file.
                if (m_file.pos() > m_file.size())
                    return fail(ReadStatus::Malformed, QStringLiteral("读取过程中 FLAC 文件被截断。"));
            }
        }
        m_document.status = m_partial ? ReadStatus::Partial : ReadStatus::Ready;
        m_document.message = m_partial
                ? QStringLiteral("已读取 FLAC 标签；部分元数据块未解析。未验证音频内容。")
                : QStringLiteral("已读取 FLAC 标签和基础参数；未验证音频内容。");
        return true;
    }

private:
    bool fail(ReadStatus status, const QString &message)
    {
        m_document.status = status;
        m_document.message = message;
        return false;
    }

    bool ioFailure(const QString &context)
    {
        return fail(ReadStatus::IoError, context + QStringLiteral(": ") + m_file.errorString());
    }

    bool readExact(int length, QByteArray *bytes, const QString &context)
    {
        *bytes = m_file.read(length);
        if (bytes->size() == length)
            return true;
        if (m_file.error() != QFileDevice::NoError)
            return ioFailure(context);
        return fail(ReadStatus::Malformed, context + QStringLiteral("被截断。"));
    }

    bool readBlock(int length, QByteArray *bytes, const QString &context)
    {
        if (length < 0 || qint64(length) > m_blockRemaining)
            return fail(ReadStatus::Malformed, context + QStringLiteral("超出 FLAC 元数据块边界。"));
        if (!readExact(length, bytes, context))
            return false;
        m_blockRemaining -= length;
        return true;
    }

    bool readLength(quint32 *length, const QString &context)
    {
        QByteArray bytes;
        if (!readBlock(4, &bytes, context))
            return false;
        *length = little32(bytes);
        return true;
    }

    bool readField(quint32 length, QByteArray *bytes, const QString &context)
    {
        if (quint64(length) > quint64(m_limits.maxFieldBytes))
            return fail(ReadStatus::LimitExceeded, context + QStringLiteral("长度超过限制。"));
        return readBlock(int(length), bytes, context);
    }

    void technical(const QString &key, quint64 value)
    {
        m_document.technical.insert(key, QStringList { QString::number(value) });
    }

    bool readStreamInfo()
    {
        QByteArray bytes;
        if (!readBlock(34, &bytes, QStringLiteral("FLAC STREAMINFO")))
            return false;
        const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData());
        const quint32 minimumBlock = (quint32(p[0]) << 8) | p[1];
        const quint32 maximumBlock = (quint32(p[2]) << 8) | p[3];
        if (minimumBlock < 16 || maximumBlock < minimumBlock)
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC STREAMINFO 音频块大小范围无效。"));
        const quint32 minimumFrame = (quint32(p[4]) << 16) | (quint32(p[5]) << 8) | p[6];
        const quint32 maximumFrame = (quint32(p[7]) << 16) | (quint32(p[8]) << 8) | p[9];
        if (minimumFrame && maximumFrame && maximumFrame < minimumFrame)
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC STREAMINFO 音频帧大小范围无效。"));
        quint64 packed = 0;
        for (int i = 10; i < 18; ++i)
            packed = (packed << 8) | p[i];
        const quint64 sampleRate = packed >> 44;
        const quint64 channels = ((packed >> 41) & 7) + 1;
        const quint64 bits = ((packed >> 36) & 31) + 1;
        const quint64 totalSamples = packed & Q_UINT64_C(0xfffffffff);
        if (sampleRate == 0 || bits < 4)
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC STREAMINFO 采样率或位深无效。"));
        technical(QStringLiteral("sample_rate"), sampleRate);
        technical(QStringLiteral("channels"), channels);
        technical(QStringLiteral("bits_per_sample"), bits);
        technical(QStringLiteral("total_samples"), totalSamples);
        if (totalSamples != 0)
            technical(QStringLiteral("duration_ms"), totalSamples * 1000 / sampleRate);
        return true;
    }

    bool readComments()
    {
        quint32 vendorLength = 0;
        if (!readLength(&vendorLength, QStringLiteral("FLAC Vorbis vendor 长度")))
            return false;
        QByteArray vendor;
        if (!readField(vendorLength, &vendor, QStringLiteral("FLAC Vorbis vendor")))
            return false;
        if (!isUtf8(vendor))
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC Vorbis vendor 不是有效 UTF-8。"));
        m_document.technical.insert(QStringLiteral("vendor"), QStringList { QString::fromUtf8(vendor) });

        quint32 count = 0;
        if (!readLength(&count, QStringLiteral("FLAC Vorbis 标签数量")))
            return false;
        if (quint64(count) > quint64(m_limits.maxFields))
            return fail(ReadStatus::LimitExceeded, QStringLiteral("FLAC Vorbis 标签数量超过限制。"));
        if (quint64(count) > quint64(m_blockRemaining) / 4)
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC Vorbis 标签数量超出元数据块边界。"));
        for (quint32 index = 0; index < count; ++index) {
            quint32 length = 0;
            if (!readLength(&length, QStringLiteral("FLAC Vorbis 标签长度")))
                return false;
            QByteArray field;
            if (!readField(length, &field, QStringLiteral("FLAC Vorbis 标签")))
                return false;
            const int separator = field.indexOf('=');
            if (separator <= 0)
                return fail(ReadStatus::Malformed, QStringLiteral("FLAC Vorbis 标签缺少有效字段名或等号分隔符。"));
            for (int i = 0; i < separator; ++i) {
                const unsigned char c = static_cast<unsigned char>(field.at(i));
                if (c < 0x20 || c > 0x7d)
                    return fail(ReadStatus::Malformed, QStringLiteral("FLAC Vorbis 标签字段名含有非法字符。"));
            }
            const QByteArray value = field.mid(separator + 1);
            if (!isUtf8(value))
                return fail(ReadStatus::Malformed, QStringLiteral("FLAC Vorbis 标签不是有效 UTF-8。"));
            m_document.tags[tagKey(field.left(separator))].append(QString::fromUtf8(value));
        }
        if (m_blockRemaining != 0)
            return fail(ReadStatus::Malformed, QStringLiteral("FLAC Vorbis 标签块含有未声明的尾随字节。"));
        return true;
    }

    QFile &m_file;
    Document &m_document;
    const ReadLimits &m_limits;
    qint64 m_fileSize = 0;
    qint64 m_tagBytes = 0;
    qint64 m_blockRemaining = 0;
    bool m_hasComments = false;
    bool m_partial = false;
};

} // namespace

bool readFlac(QFile &file, Document *document, const ReadLimits &limits)
{
    if (!document)
        return false;
    return FlacReader(file, *document, limits).read();
}

} } }
