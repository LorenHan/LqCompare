#include "textdocument.h"
#include "textdiff.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextCodec>
#include <QtEndian>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#endif

namespace LqCompare { namespace Text {
namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

#ifdef Q_OS_WIN
QByteArray fileIdentity(HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info)) return {};
    return QByteArray::number(info.dwVolumeSerialNumber) + ':'
        + QByteArray::number(info.nFileIndexHigh) + ':' + QByteArray::number(info.nFileIndexLow);
}
#else
QByteArray fileIdentity(const struct stat &info)
{
    return QByteArray::number(qulonglong(info.st_dev)) + ':' + QByteArray::number(qulonglong(info.st_ino));
}
#endif

QByteArray fileIdentity(const QFileDevice &file)
{
#ifdef Q_OS_WIN
    return fileIdentity(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())));
#else
    struct stat info;
    return ::fstat(file.handle(), &info) == 0 ? fileIdentity(info) : QByteArray();
#endif
}

QByteArray fileIdentity(const QString &path)
{
    if (path.isEmpty()) return {};
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const auto identity = fileIdentity(handle);
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    return identity;
#else
    struct stat info;
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? fileIdentity(info) : QByteArray();
#endif
}

bool samePath(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) return false;
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

bool checkSnapshot(const QString &path, const QByteArray &identity,
                   const QByteArray &bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("The original file is no longer readable: %1").arg(file.errorString()));
    if (identity.isEmpty() || fileIdentity(file) != identity)
        return fail(error, QStringLiteral("The file changed on disk after it was opened (file identity changed). Save As to a new file or reload it before saving."));
    const bool sameContent = file.size() == bytes.size() && file.read(Document::MaximumFileBytes + 1) == bytes;
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    if (!sameContent || fileIdentity(path) != identity)
        return fail(error, QStringLiteral("The file changed on disk after it was opened. Save As to a new file or reload it before saving."));
    if (error) error->clear();
    return true;
}
QString actualText(const QVector<Line> &lines)
{
    QString text;
    for (const Line &line : lines) text += line.text + eolText(line.eol);
    return text;
}

bool exceedsLineLimit(const QString &text)
{
    int lines = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QLatin1Char('\r') && text[i] != QLatin1Char('\n')) continue;
        if (++lines > Document::MaximumLines) return true;
        if (text[i] == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) ++i;
    }
    if (!text.isEmpty() && text.back() != QLatin1Char('\r') && text.back() != QLatin1Char('\n')) ++lines;
    return lines > Document::MaximumLines;
}

QString decodeUtf32(const QByteArray &bytes, bool littleEndian, int *errors)
{
    QString text;
    text.reserve(bytes.size() / 4);
    *errors = 0;
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    int offset = 0;
    for (; offset + 4 <= bytes.size(); offset += 4) {
        const quint32 scalar = littleEndian ? qFromLittleEndian<quint32>(data + offset)
                                           : qFromBigEndian<quint32>(data + offset);
        if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
            // Some codecs accept surrogate values as UTF-32. They are not
            // Unicode scalar values and must not enter an editable document.
            text += QChar::ReplacementCharacter;
            ++*errors;
        } else if (scalar <= 0xffff) {
            text += QChar(static_cast<ushort>(scalar));
        } else {
            text += QChar(QChar::highSurrogate(scalar));
            text += QChar(QChar::lowSurrogate(scalar));
        }
    }
    if (offset != bytes.size()) {
        text += QChar::ReplacementCharacter;
        ++*errors;
    }
    return text;
}

QByteArray encodeUtf32(const QString &text, bool littleEndian)
{
    QByteArray bytes;
    bytes.reserve(text.size() * 4);
    for (int i = 0; i < text.size(); ++i) {
        quint32 scalar = text[i].unicode();
        // The caller has already validated all UTF-16 surrogate pairs.
        if (text[i].isHighSurrogate()) {
            scalar = QChar::surrogateToUcs4(text[i], text[i + 1]);
            ++i;
        }
        uchar unit[4];
        if (littleEndian) qToLittleEndian<quint32>(scalar, unit);
        else qToBigEndian<quint32>(scalar, unit);
        bytes.append(reinterpret_cast<const char *>(unit), 4);
    }
    return bytes;
}
}

QString eolText(Eol eol)
{
    switch (eol) {
    case Eol::LF: return QStringLiteral("\n");
    case Eol::CRLF: return QStringLiteral("\r\n");
    case Eol::CR: return QStringLiteral("\r");
    case Eol::None: return {};
    }
    return {};
}

QVector<Line> Document::splitLines(const QString &text)
{
    QVector<Line> lines;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char('\r') && ch != QLatin1Char('\n')) continue;
        const int end = i;
        Eol eol = ch == QLatin1Char('\n') ? Eol::LF : Eol::CR;
        if (eol == Eol::CR && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
            ++i;
            eol = Eol::CRLF;
        }
        lines.append({text.mid(start, end - start), eol});
        start = i + 1;
    }
    if (start < text.size()) lines.append({text.mid(start), Eol::None});
    return lines;
}

bool Document::decode(const QByteArray &bytes, Document *result, QString *error,
                      const QByteArray &codecName)
{
    if (!result) return fail(error, QStringLiteral("No document destination was provided."));
    if (bytes.size() > MaximumFileBytes)
        return fail(error, QStringLiteral("Text comparison currently supports files up to 32 MiB."));
    Document next;
    next.m_originalBytes = bytes;
    QByteArray content = bytes;
    QByteArray detected = "UTF-8";
    // UTF-32 LE shares its first two bytes with UTF-16 LE; longest BOM first.
    if (bytes.startsWith(QByteArray::fromHex("fffe0000"))) {
        detected = "UTF-32LE";
        next.m_bom = true;
        content.remove(0, 4);
    } else if (bytes.startsWith(QByteArray::fromHex("0000feff"))) {
        detected = "UTF-32BE";
        next.m_bom = true;
        content.remove(0, 4);
    } else if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) {
        next.m_bom = true;
        content.remove(0, 3);
    } else if (bytes.startsWith(QByteArray::fromHex("fffe"))) {
        detected = "UTF-16LE";
        next.m_bom = true;
        content.remove(0, 2);
    } else if (bytes.startsWith(QByteArray::fromHex("feff"))) {
        detected = "UTF-16BE";
        next.m_bom = true;
        content.remove(0, 2);
    }
    // A BOM is definitive. The explicit codec handles legacy files without BOM.
    next.m_codec = next.m_bom || codecName.isEmpty() ? detected : codecName;
    QTextCodec *codec = QTextCodec::codecForName(next.m_codec);
    if (!codec) return fail(error, QStringLiteral("Unsupported text encoding: %1")
                            .arg(QString::fromLatin1(next.m_codec)));
    next.m_codec = codec->name();
    QString decoded;
    if (next.m_codec == "UTF-32LE" || next.m_codec == "UTF-32BE") {
        decoded = decodeUtf32(content, next.m_codec == "UTF-32LE", &next.m_decodeErrors);
    } else {
        QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
        decoded = codec->toUnicode(content.constData(), content.size(), &state);
        if (state.remainingChars > 0) decoded += QChar::ReplacementCharacter;
        next.m_decodeErrors = state.invalidChars + (state.remainingChars > 0 ? 1 : 0);
    }
    next.m_binary = decoded.contains(QChar(0));
    // Qt editors treat U+2029 as a paragraph break and cannot distinguish it
    // from a file newline after editing. Keep these uncommon files read-only.
    next.m_paragraphSeparators = decoded.contains(QChar::ParagraphSeparator);
    // Check before allocating one Line per terminator: a 32 MiB newline-only
    // file otherwise allocates tens of millions of Line objects before failing.
    if (exceedsLineLimit(decoded))
        return fail(error, QStringLiteral("Text comparison currently supports up to 500,000 lines per file."));
    next.m_lines = splitLines(decoded);
    *result = next;
    if (error) error->clear();
    return true;
}

bool Document::load(const QString &path, Document *result, QString *error,
                    const QByteArray &codecName)
{
    if (!result) return fail(error, QStringLiteral("No document destination was provided."));
    if (path.isEmpty()) return decode({}, result, error, codecName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    const auto identity = fileIdentity(file);
    if (identity.isEmpty()) return fail(error, QStringLiteral("Cannot inspect the source file identity."));
    if (file.size() > MaximumFileBytes)
        return fail(error, QStringLiteral("Text comparison currently supports files up to 32 MiB."));
    const QByteArray content = file.read(MaximumFileBytes + 1);
    if (file.error() != QFile::NoError) return fail(error, file.errorString());
    if (fileIdentity(path) != identity)
        return fail(error, QStringLiteral("The source file changed while it was being opened. Open it again."));
    Document next;
    if (!decode(content, &next, error, codecName)) return false;
    next.m_path = QFileInfo(path).absoluteFilePath();
    next.m_originalIdentity = identity;
    next.m_originalCanonicalPath = QFileInfo(path).canonicalFilePath();
    *result = next;
    return true;
}

QString Document::warning() const
{
    if (m_decodeErrors)
        return QStringLiteral("%1 decoding error(s). Choose the correct encoding before editing.")
            .arg(m_decodeErrors);
    if (m_binary) return QStringLiteral("Contains NUL characters; binary data is read-only in the text view.");
    if (m_paragraphSeparators)
        return QStringLiteral("Contains Unicode paragraph separators; this editor cannot preserve them safely. The file is read-only.");
    return {};
}

void Document::countEndings(int *counts) const
{
    for (int i = 0; i < 4; ++i) counts[i] = 0;
    for (const Line &line : m_lines) ++counts[static_cast<int>(line.eol)];
}

QString Document::eolDescription() const
{
    int counts[4] = {};
    countEndings(counts);
    QStringList types;
    if (counts[1]) types << QStringLiteral("LF");
    if (counts[2]) types << QStringLiteral("CRLF");
    if (counts[3]) types << QStringLiteral("CR");
    if (types.isEmpty()) return QStringLiteral("No line ending");
    if (types.size() == 1) return types.first();
    return QStringLiteral("Mixed (LF %1 / CRLF %2 / CR %3)").arg(counts[1]).arg(counts[2]).arg(counts[3]);
}

Eol Document::preferredEol() const
{
    int counts[4] = {};
    countEndings(counts);
    int best = 1;
    for (int i = 2; i < 4; ++i) if (counts[i] > counts[best]) best = i;
    return static_cast<Eol>(best);
}

bool Document::hasMixedEndings() const
{
    int counts[4] = {};
    countEndings(counts);
    // 与 `eolDescription()` 的 `Mixed` 判据逐字一致：只数 LF / CRLF / CR，
    // 不数 `None`（理由见头文件）。
    int present = 0;
    for (int i = 1; i < 4; ++i) if (counts[i]) ++present;
    return present > 1;
}

QString Document::normalizedText() const
{
    QString text;
    for (const Line &line : m_lines) {
        text += line.text;
        if (line.eol != Eol::None) text += QLatin1Char('\n');
    }
    return text;
}

QByteArray Document::bytes(QString *error) const
{
    if (error) error->clear();
    QTextCodec *codec = QTextCodec::codecForName(m_codec);
    if (!codec) { fail(error, QStringLiteral("Unsupported text encoding.")); return {}; }
    const QString text = actualText(m_lines);
    for (int i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate()) {
            if (i + 1 >= text.size() || !text[i + 1].isLowSurrogate()) {
                fail(error, QStringLiteral("The text contains an incomplete Unicode character. Save was cancelled."));
                return {};
            }
            ++i;
        } else if (text[i].isLowSurrogate()) {
            fail(error, QStringLiteral("The text contains an incomplete Unicode character. Save was cancelled."));
            return {};
        }
    }
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    // Qt 5's UTF-32 codec can append a NUL unit after supplementary characters;
    // writing explicit scalar values keeps emoji and trailing bytes lossless.
    QByteArray encoded = m_codec == "UTF-32LE" || m_codec == "UTF-32BE"
        ? encodeUtf32(text, m_codec == "UTF-32LE")
        : codec->fromUnicode(text.constData(), text.size(), &state);
    if (state.invalidChars || state.remainingChars) {
        fail(error, QStringLiteral("The selected encoding cannot represent %1 character(s). Save was cancelled.")
             .arg(state.invalidChars));
        return {};
    }
    if (m_bom) {
        if (m_codec == "UTF-8") encoded.prepend(QByteArray::fromHex("efbbbf"));
        else if (m_codec == "UTF-16LE") encoded.prepend(QByteArray::fromHex("fffe"));
        else if (m_codec == "UTF-16BE") encoded.prepend(QByteArray::fromHex("feff"));
        else if (m_codec == "UTF-32LE") encoded.prepend(QByteArray::fromHex("fffe0000"));
        else if (m_codec == "UTF-32BE") encoded.prepend(QByteArray::fromHex("0000feff"));
    }
    return encoded;
}

bool Document::isModified() const
{
    // Invalid decoded content must never replace the original bytes.
    if (!canEdit()) return false;
    QString error;
    const auto content = bytes(&error);
    return !error.isEmpty() || content != m_originalBytes;
}

bool Document::setNormalizedText(const QString &text, QString *error)
{
    if (!canEdit()) return fail(error, warning());
    if (text == normalizedText()) { if (error) error->clear(); return true; }
    if (exceedsLineLimit(text))
        return fail(error, QStringLiteral("Text comparison currently supports up to 500,000 lines per file."));
    QVector<Line> next = splitLines(text);
    CompareOptions options;
    options.ignoreEol = true;
    options.ignoreFinalNewline = true;
    const Result alignment = compare(m_lines, next, options);
    const Eol fallback = preferredEol();
    for (Line &line : next) if (line.eol != Eol::None) line.eol = fallback;
    for (const Row &row : alignment.rows) {
        if (row.leftLine < 0 || row.rightLine < 0) continue;
        // Preserve per-line endings for existing lines, even after insertions.
        if (next[row.rightLine].eol != Eol::None && m_lines[row.leftLine].eol != Eol::None)
            next[row.rightLine].eol = m_lines[row.leftLine].eol;
    }
    m_lines = next;
    if (error) error->clear();
    return true;
}

bool Document::replaceLines(int start, int count, const QVector<Line> &replacement, QString *error)
{
    if (!canEdit()) return fail(error, warning());
    if (start < 0 || count < 0 || start > m_lines.size() || count > m_lines.size() - start)
        return fail(error, QStringLiteral("The selected difference is no longer valid. Compare again."));
    if (qint64(m_lines.size()) - count + replacement.size() > MaximumLines)
        return fail(error, QStringLiteral("Text comparison currently supports up to 500,000 lines per file."));
    QVector<Line> next = m_lines.mid(0, start);
    next += replacement;
    next += m_lines.mid(start + count);
    // A non-final line requires a terminator when appending to a no-newline file.
    for (int i = 0; i + 1 < next.size(); ++i)
        if (next[i].eol == Eol::None) next[i].eol = preferredEol();
    m_lines = next;
    if (error) error->clear();
    return true;
}

void Document::setEol(Eol eol)
{
    if (!canEdit() || eol == Eol::None) return;
    for (Line &line : m_lines) if (line.eol != Eol::None) line.eol = eol;
}

bool Document::checkUnchangedOnDisk(QString *error) const
{
    if (m_path.isEmpty()) return fail(error, QStringLiteral("Choose Save As for the empty side before saving."));
    if (QFileInfo(m_path).isSymLink())
        return fail(error, QStringLiteral("Saving through a symbolic link is not supported. Use Save As."));
    return checkSnapshot(m_path, m_originalIdentity, m_originalBytes, error);
}

bool Document::refersToPath(const QString &path) const
{
    if (path.isEmpty() || m_path.isEmpty()) return false;
    const QFileInfo target(path), source(m_path);
    if (samePath(target.absoluteFilePath(), source.absoluteFilePath())) return true;
    const auto canonical = target.canonicalFilePath();
    if (samePath(canonical, source.canonicalFilePath()) || samePath(canonical, m_originalCanonicalPath)) return true;
    const auto identity = fileIdentity(path);
    return !identity.isEmpty() && (identity == m_originalIdentity || identity == fileIdentity(m_path));
}

bool Document::write(const QString &path, QString *error, bool protectOriginal)
{
    if (!canEdit()) return fail(error, warning());
    QString conversionError;
    const QByteArray content = bytes(&conversionError);
    if (!conversionError.isEmpty()) return fail(error, conversionError);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    if (file.write(content) != content.size()) return fail(error, file.errorString());
    const auto identity = fileIdentity(file);
    if (identity.isEmpty()) return fail(error, QStringLiteral("Cannot inspect the output file identity."));
    // Recheck after preparing the atomic replacement. This is not an OS-level
    // compare-and-swap, but avoids overwriting changes during serialization.
    if (protectOriginal && (!checkSnapshot(m_path, m_originalIdentity, m_originalBytes, error)
                            || !checkSnapshot(path, m_originalIdentity, m_originalBytes, error))) return false;
    if (!file.commit()) return fail(error, file.errorString());
    m_originalBytes = content;
    m_path = QFileInfo(path).absoluteFilePath();
    m_originalIdentity = identity;
    m_originalCanonicalPath = QFileInfo(path).canonicalFilePath();
    if (error) error->clear();
    return true;
}

bool Document::save(QString *error)
{
    if (!checkUnchangedOnDisk(error)) return false;
    return write(m_path, error, true);
}

bool Document::saveAs(const QString &path, bool overwrite, QString *error)
{
    if (path.isEmpty()) return fail(error, QStringLiteral("No output file was selected."));
    const QFileInfo target(path);
    if (target.isSymLink()) return fail(error, QStringLiteral("Choose a regular file, not a symbolic link."));
    if (refersToPath(path)) {
        // Save As to a real path may recover a document opened through a
        // symlink, but neither an alias nor overwrite=true acknowledges edits.
        if (!checkSnapshot(m_path, m_originalIdentity, m_originalBytes, error)
            || !checkSnapshot(path, m_originalIdentity, m_originalBytes, error)) return false;
        return write(path, error, true);
    }
    if (target.exists() && !overwrite) return fail(error, QStringLiteral("The destination already exists. Confirm overwrite first."));
    return write(path, error);
}

} }
