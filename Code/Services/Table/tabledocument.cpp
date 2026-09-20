#include "tabledocument.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTextCodec>
#include <algorithm>

namespace LqCompare { namespace Table {
namespace {

constexpr int MaximumColumns = 16384;
constexpr int MaximumCells = 2000000;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool supportedSource(const QString &source, QString *error)
{
    const QString suffix = QFileInfo(source).suffix().toLower();
    if (suffix == QStringLiteral("xlsx") || suffix == QStringLiteral("xls")
        || suffix == QStringLiteral("xlsm") || suffix == QStringLiteral("xlsb"))
        return fail(error, QStringLiteral("Excel workbooks (XLSX/XLS/XLSM/XLSB) are not supported. Export the worksheet as CSV or TSV first."));
    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm")
        || suffix == QStringLiteral("xhtml"))
        return fail(error, QStringLiteral("HTML tables are not supported. Export the table as CSV or TSV first."));
    return true;
}

bool decode(const QByteArray &bytes, const QByteArray &requestedEncoding,
            QString *text, QByteArray *actualEncoding, QString *error)
{
    QByteArray bomEncoding;
    int bomSize = 0;
    if (bytes.startsWith(QByteArray::fromHex("0000feff"))
        || bytes.startsWith(QByteArray::fromHex("fffe0000")))
        return fail(error, QStringLiteral("UTF-32 input is not supported; convert it to UTF-8 or UTF-16."));
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) {
        bomEncoding = "UTF-8";
        bomSize = 3;
    } else if (bytes.startsWith(QByteArray::fromHex("fffe"))) {
        bomEncoding = "UTF-16LE";
        bomSize = 2;
    } else if (bytes.startsWith(QByteArray::fromHex("feff"))) {
        bomEncoding = "UTF-16BE";
        bomSize = 2;
    }

    QTextCodec *requested = nullptr;
    if (!requestedEncoding.isEmpty()) {
        requested = QTextCodec::codecForName(requestedEncoding);
        if (!requested)
            return fail(error, QStringLiteral("Unknown text encoding: %1.").arg(QString::fromLatin1(requestedEncoding)));
    }
    QTextCodec *codec = bomEncoding.isEmpty()
        ? (requested ? requested : QTextCodec::codecForName("UTF-8"))
        : QTextCodec::codecForName(bomEncoding);
    if (requested && !bomEncoding.isEmpty() && requested->mibEnum() != codec->mibEnum()
        && !(requested->name() == "UTF-16" && bomEncoding.startsWith("UTF-16")))
        return fail(error, QStringLiteral("The %1 byte-order mark conflicts with the selected %2 encoding.")
                    .arg(QString::fromLatin1(bomEncoding), QString::fromLatin1(requested->name())));
    *actualEncoding = codec->name();
    const char *data = bytes.constData() + bomSize;
    const int length = bytes.size() - bomSize;

    if (*actualEncoding == "UTF-16" && bomEncoding.isEmpty() && length > 0)
        return fail(error, QStringLiteral("UTF-16 without a byte-order mark requires an explicit UTF-16LE or UTF-16BE encoding."));
    if (actualEncoding->startsWith("UTF-32"))
        return fail(error, QStringLiteral("UTF-32 input is not supported; convert it to UTF-8 or UTF-16."));
    if (*actualEncoding == "UTF-16LE" || *actualEncoding == "UTF-16BE") {
        if (length % 2)
            return fail(error, QStringLiteral("Invalid %1 encoding: incomplete code unit at byte %2.")
                        .arg(QString::fromLatin1(*actualEncoding)).arg(bytes.size()));
        const bool little = *actualEncoding == "UTF-16LE";
        text->reserve(length / 2);
        bool pendingHigh = false;
        for (int i = 0; i < length; i += 2) {
            const auto a = static_cast<unsigned char>(data[i]);
            const auto b = static_cast<unsigned char>(data[i + 1]);
            const ushort value = little ? ushort(a | (b << 8)) : ushort((a << 8) | b);
            const QChar c(value);
            if ((pendingHigh && !c.isLowSurrogate()) || (!pendingHigh && c.isLowSurrogate()))
                return fail(error, QStringLiteral("Invalid %1 encoding: unmatched surrogate at byte %2.")
                            .arg(QString::fromLatin1(*actualEncoding)).arg(i + bomSize + 1));
            pendingHigh = c.isHighSurrogate();
            text->append(c);
        }
        if (pendingHigh)
            return fail(error, QStringLiteral("Invalid %1 encoding: incomplete surrogate pair at end of input.")
                        .arg(QString::fromLatin1(*actualEncoding)));
    } else {
        QTextCodec::ConverterState state;
        *text = codec->toUnicode(data, length, &state);
        if (state.invalidChars || state.remainingChars)
            return fail(error, QStringLiteral("Invalid or incomplete %1 encoding. Select the source encoding explicitly.")
                        .arg(QString::fromLatin1(*actualEncoding)));
    }
    for (int i = 0; i < text->size(); ++i) {
        const ushort c = text->at(i).unicode();
        if ((c < 0x20 && c != '\t' && c != '\r' && c != '\n') || c == 0x7f)
            return fail(error, QStringLiteral("Binary or unsupported control character U+%1 at character %2; only text CSV/TSV is supported.")
                        .arg(c, 4, 16, QLatin1Char('0')).arg(i + 1));
    }
    const QString start = text->trimmed().left(256).toLower();
    const auto beginsTag = [&start](const QString &tag) {
        return start.startsWith(tag) && (start.size() == tag.size()
            || start.at(tag.size()).isSpace() || start.at(tag.size()) == QLatin1Char('>'));
    };
    if (beginsTag(QStringLiteral("<html")) || beginsTag(QStringLiteral("<table"))
        || beginsTag(QStringLiteral("<!doctype html")))
        return fail(error, QStringLiteral("HTML tables are not supported. Export the table as CSV or TSV first."));
    return true;
}

QString detectDelimiter(const QString &text, const QString &source)
{
    const QString candidates = QStringLiteral(",\t;|");
    QVector<QVector<int>> counts(4);
    int current[4] = {};
    int recordCount = 0;
    bool quoted = false;
    bool hasContent = false;
    const auto finishRecord = [&] {
        if (hasContent) {
            for (int c = 0; c < 4; ++c) {
                counts[c].append(current[c]);
                current[c] = 0;
            }
            ++recordCount;
        }
        hasContent = false;
    };
    for (int i = 0; i < text.size() && recordCount < 64; ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) {
            hasContent = true;
            if (quoted && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"'))
                ++i;
            else
                quoted = !quoted;
        } else if (!quoted && (c == QLatin1Char('\r') || c == QLatin1Char('\n'))) {
            finishRecord();
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
        } else {
            hasContent = true;
            if (!quoted) {
                const int candidate = candidates.indexOf(c);
                if (candidate >= 0)
                    ++current[candidate];
            }
        }
    }
    if (hasContent)
        finishRecord();
    const int preferred = QFileInfo(source).suffix().compare(QStringLiteral("tsv"), Qt::CaseInsensitive) == 0 ? 1 : 0;
    int best = preferred;
    qint64 bestScore = -1;
    for (int c = 0; c < 4; ++c) {
        int nonzero = 0;
        int total = 0;
        int modeCount = 0;
        QHash<int, int> frequencies;
        for (int count : counts[c]) {
            if (count) {
                ++nonzero;
                total += count;
                modeCount = qMax(modeCount, ++frequencies[count]);
            }
        }
        // Prefer a delimiter present consistently across records, then field count.
        const qint64 score = qint64(nonzero) * 100000000 + qint64(modeCount) * 1000000
            + (c == preferred ? 10000 : 0) + qMin(total, 9999);
        if (score > bestScore) {
            bestScore = score;
            best = c;
        }
    }
    return QString(candidates.at(best));
}

bool parseRecords(const QString &text, const QString &delimiter,
                  QVector<QStringList> *records, QString *error)
{
    enum State { Start, Unquoted, Quoted, Closed };
    State state = Start;
    QStringList record;
    QString field;
    bool recordStarted = false;
    int line = 1;
    int physicalColumn = 1;
    int cellCount = 0;
    const auto syntaxError = [&](const QString &reason) {
        return fail(error, QStringLiteral("CSV row %1, column %2 (line %3, character %4): %5")
                    .arg(records->size() + 1).arg(record.size() + 1).arg(line).arg(physicalColumn).arg(reason));
    };
    const auto appendField = [&] {
        if (record.size() >= MaximumColumns)
            return syntaxError(QStringLiteral("the 16,384-column limit was exceeded."));
        if (++cellCount > MaximumCells)
            return syntaxError(QStringLiteral("the 2,000,000-cell limit was exceeded."));
        record.append(field);
        field.clear();
        state = Start;
        return true;
    };
    for (int i = 0; i < text.size();) {
        const QChar c = text.at(i);
        const bool newline = c == QLatin1Char('\r') || c == QLatin1Char('\n');
        const int newlineLength = c == QLatin1Char('\r') && i + 1 < text.size()
            && text.at(i + 1) == QLatin1Char('\n') ? 2 : 1;
        if (state == Quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field.append(c);
                    i += 2;
                    physicalColumn += 2;
                } else {
                    state = Closed;
                    ++i;
                    ++physicalColumn;
                }
            } else if (newline) {
                field.append(text.midRef(i, newlineLength));
                i += newlineLength;
                ++line;
                physicalColumn = 1;
            } else {
                field.append(c);
                ++i;
                ++physicalColumn;
            }
            continue;
        }
        if (text.midRef(i, delimiter.size()) == delimiter) {
            if (!appendField())
                return false;
            recordStarted = true;
            i += delimiter.size();
            physicalColumn += delimiter.size();
        } else if (newline) {
            if (!appendField())
                return false;
            records->append(record);
            record.clear();
            recordStarted = false;
            i += newlineLength;
            ++line;
            physicalColumn = 1;
        } else if (state == Closed) {
            return syntaxError(QStringLiteral("unexpected content after a closing quote; expected a delimiter or line ending."));
        } else if (c == QLatin1Char('"')) {
            if (state != Start)
                return syntaxError(QStringLiteral("a quote must appear at the beginning of a field; embedded quotes must be escaped inside a quoted field."));
            state = Quoted;
            recordStarted = true;
            ++i;
            ++physicalColumn;
        } else {
            state = Unquoted;
            recordStarted = true;
            field.append(c);
            ++i;
            ++physicalColumn;
        }
    }
    if (state == Quoted)
        return syntaxError(QStringLiteral("unterminated quoted field."));
    // A final record terminator does not manufacture a new empty row.
    if (recordStarted) {
        if (!appendField())
            return false;
        records->append(record);
    }
    return true;
}

} // namespace

int Document::columnCount() const
{
    int count = headers.size();
    for (const QStringList &row : rows)
        count = qMax(count, row.size());
    return count;
}

bool Document::parse(const QByteArray &bytes, Document *result, QString *error,
                     const ParseOptions &options, const QString &sourceName)
{
    if (!result)
        return fail(error, QStringLiteral("No output document was supplied."));
    if (!supportedSource(sourceName, error))
        return false;
    if (bytes.size() > MaximumFileBytes)
        return fail(error, QStringLiteral("Table input exceeds the 32 MiB limit for this read-only preview."));
    if (options.delimiter.contains(QLatin1Char('"')) || options.delimiter.contains(QLatin1Char('\r'))
        || options.delimiter.contains(QLatin1Char('\n')) || options.delimiter.contains(QChar(0)))
        return fail(error, QStringLiteral("A delimiter cannot contain a quote, line ending, or NUL character."));
    if (bytes.startsWith(QByteArray::fromHex("504b0304")) || bytes.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1")))
        return fail(error, QStringLiteral("Binary workbook/archive input is not supported. Export the worksheet as CSV or TSV first."));

    Document parsed;
    parsed.path = sourceName;
    parsed.hasHeader = options.firstRowHeader;
    QString text;
    if (!decode(bytes, options.encoding, &text, &parsed.encoding, error))
        return false;
    parsed.delimiter = options.delimiter.isEmpty() ? detectDelimiter(text, sourceName) : options.delimiter;
    if (!parseRecords(text, parsed.delimiter, &parsed.rows, error))
        return false;
    if (parsed.hasHeader && !parsed.rows.isEmpty()) {
        parsed.headers = parsed.rows.first();
        parsed.rows.removeFirst();
    }
    const int columnCount = parsed.columnCount();
    const int headerCount = parsed.headers.size();
    QSet<QString> usedHeaders;
    for (const QString &header : parsed.headers)
        usedHeaders.insert(header);
    for (int c = headerCount; c < columnCount; ++c) {
        QString header = QStringLiteral("Column %1").arg(c + 1);
        while (usedHeaders.contains(header))
            header.append(QLatin1Char('_'));
        usedHeaders.insert(header);
        parsed.headers.append(header);
    }
    if (parsed.hasHeader && headerCount < columnCount)
        parsed.warnings.append(QStringLiteral("Some data rows extend beyond the header; %1 column name(s) were generated.").arg(columnCount - headerCount));
    int raggedRows = 0;
    for (const QStringList &row : parsed.rows) {
        if (row.size() != columnCount)
            ++raggedRows;
    }
    if (raggedRows)
        parsed.warnings.append(QStringLiteral("%1 row(s) have missing trailing fields; missing and empty cells remain distinct.").arg(raggedRows));
    if (parsed.hasHeader && !parsed.headers.isEmpty()) {
        QSet<QString> seen;
        bool duplicate = false;
        bool blank = false;
        for (int c = 0; c < headerCount; ++c) {
            const QString &header = parsed.headers.at(c);
            duplicate = duplicate || seen.contains(header);
            blank = blank || header.isEmpty();
            seen.insert(header);
        }
        if (duplicate || blank)
            parsed.warnings.append(QStringLiteral("Header names are duplicated or empty; automatic column-name matching may be ambiguous."));
    }
    *result = std::move(parsed);
    if (error)
        error->clear();
    return true;
}

bool Document::load(const QString &path, Document *result, QString *error, const ParseOptions &options)
{
    if (!result)
        return fail(error, QStringLiteral("No output document was supplied."));
    if (!supportedSource(path, error))
        return false;
    if (path.isEmpty())
        return parse({}, result, error, options);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
    if (file.size() > MaximumFileBytes)
        return fail(error, QStringLiteral("Table input exceeds the 32 MiB limit for this read-only preview."));
    const QByteArray bytes = file.read(MaximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
    return parse(bytes, result, error, options, path);
}

} }
