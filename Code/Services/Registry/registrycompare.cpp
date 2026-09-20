#include "registrycompare.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextCodec>
#include <algorithm>
#include <limits>

namespace LqCompare { namespace Registry {
namespace {
ReadResult failure(const QString &source, const QString &message, int line = 0)
{
    ReadResult result;
    result.snapshot.source = source;
    result.errorLine = line;
    result.error = line ? QStringLiteral("Line %1: %2").arg(line).arg(message) : message;
    return result;
}
bool hexDigits(const QString &s, int maximum)
{
    if (s.isEmpty() || s.size() > maximum) return false;
    for (QChar c : s) {
        const ushort u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')))
            return false;
    }
    return true;
}
bool decodeUtf16(const QByteArray &bytes, bool bigEndian, QString *out)
{
    if (bytes.size() % 2) return false;
    out->clear();
    out->reserve(bytes.size() / 2);
    bool high = false;
    for (int i = 0; i < bytes.size(); i += 2) {
        const auto a = static_cast<uchar>(bytes.at(i));
        const auto b = static_cast<uchar>(bytes.at(i + 1));
        const ushort u = bigEndian ? ushort((a << 8) | b) : ushort(a | (b << 8));
        const QChar c(u);
        if (high && !c.isLowSurrogate()) return false;
        if (!high && c.isLowSurrogate()) return false;
        high = c.isHighSurrogate();
        out->append(c);
    }
    return !high;
}
bool quoted(const QString &text, int *position, QString *out, QString *error)
{
    out->clear();
    if (*position >= text.size() || text.at(*position) != QLatin1Char('"')) {
        *error = QStringLiteral("Expected a quoted registry string."); return false;
    }
    ++*position;
    while (*position < text.size()) {
        QChar c = text.at((*position)++);
        if (c == QLatin1Char('"')) return true;
        if (c == QLatin1Char('\\')) {
            if (*position >= text.size()) break;
            c = text.at((*position)++);
            if (c != QLatin1Char('\\') && c != QLatin1Char('"')) {
                *error = QStringLiteral("Only \\\\ and \\\" are valid quoted-string escapes."); return false;
            }
        }
        if (c.unicode() < 0x20) {
            *error = QStringLiteral("Control characters are not valid inside quoted strings; use hex data.");
            return false;
        }
        out->append(c);
    }
    *error = QStringLiteral("Unterminated quoted registry string."); return false;
}
QString valuePayload(const QString &line)
{
    int position = 0;
    QString ignored, error;
    if (line.startsWith(QLatin1Char('@'))) ++position;
    else if (!quoted(line, &position, &ignored, &error)) return {};
    while (position < line.size() && line.at(position).isSpace()) ++position;
    if (position >= line.size() || line.at(position++) != QLatin1Char('=')) return {};
    return line.mid(position).trimmed();
}
bool parseValue(const QString &line, Value *value, QString *error)
{
    int position = 0;
    if (line.startsWith(QLatin1Char('@'))) { ++position; value->name.clear(); }
    else if (!quoted(line, &position, &value->name, error)) return false;
    while (position < line.size() && line.at(position).isSpace()) ++position;
    if (position >= line.size() || line.at(position++) != QLatin1Char('=')) {
        *error = QStringLiteral("Expected '=' after the value name."); return false;
    }
    QString data = line.mid(position).trimmed();
    // An inline comment is recognized only outside quoted strings.
    if (!data.startsWith(QLatin1Char('"'))) data = data.section(QLatin1Char(';'), 0, 0).trimmed();
    if (data == QStringLiteral("-")) { value->deleted = true; return true; }
    if (data.startsWith(QLatin1Char('"'))) {
        position = 0;
        QString string;
        if (!quoted(data, &position, &string, error)) return false;
        const QString tail = data.mid(position).trimmed();
        if (!tail.isEmpty() && !tail.startsWith(QLatin1Char(';'))) {
            *error = QStringLiteral("Unexpected text after quoted value."); return false;
        }
        value->type = 1; value->data = encodeString(string); return true;
    }
    const int colon = data.indexOf(QLatin1Char(':'));
    if (colon < 0) { *error = QStringLiteral("Expected a quoted string, deletion marker, or typed value."); return false; }
    const QString prefix = data.left(colon).trimmed().toLower();
    const QString payload = data.mid(colon + 1).trimmed();
    if (prefix == QStringLiteral("dword") || prefix == QStringLiteral("qword")) {
        const int width = prefix == QStringLiteral("dword") ? 4 : 8;
        if (!hexDigits(payload, width * 2)) {
            *error = QStringLiteral("Invalid or overflowing %1 hexadecimal value.").arg(prefix); return false;
        }
        const quint64 number = payload.toULongLong(nullptr, 16);
        value->type = width == 4 ? 4 : 11;
        for (int i = 0; i < width; ++i) value->data.append(char((number >> (i * 8)) & 0xff));
        return true;
    }
    if (prefix == QStringLiteral("hex")) value->type = 3;
    else if (prefix.startsWith(QStringLiteral("hex(")) && prefix.endsWith(QLatin1Char(')'))
             && hexDigits(prefix.mid(4, prefix.size() - 5), 8))
        value->type = prefix.mid(4, prefix.size() - 5).toUInt(nullptr, 16);
    else { *error = QStringLiteral("Unknown registry value encoding: %1.").arg(prefix); return false; }
    if (payload.isEmpty()) return true;
    const QStringList octets = payload.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString &octet : octets) {
        const QString token = octet.trimmed();
        if (!hexDigits(token, 2)) { *error = QStringLiteral("Invalid hex byte list (expected comma-separated bytes)."); return false; }
        value->data.append(char(token.toUInt(nullptr, 16)));
    }
    // Raw hex data intentionally remains byte-exact, including malformed strings
    // that Windows permits storing. Display flags malformed layouts separately.
    return true;
}
QString unreadableAncestor(const Snapshot &snapshot, const QString &path)
{
    QString current = path;
    while (!current.isEmpty()) {
        const auto found = snapshot.keys.constFind(current);
        if (found != snapshot.keys.cend() && !found->error.isEmpty())
            return QStringLiteral("%1: %2").arg(found->path, found->error);
        const int slash = current.lastIndexOf(QLatin1Char('\\'));
        current = slash < 0 ? QString() : current.left(slash);
    }
    return {};
}
QString escapedDisplay(QString text)
{
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    text.replace(QChar(0), QStringLiteral("\\0"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return text;
}
}

QString identity(const QString &name)
{
    // Simple UTF-16 case mapping avoids multi-character expansions such as ß/ss.
    // Preserve raw spelling in the data model; normalization is identity only.
    QString result;
    result.reserve(name.size());
    for (QChar c : name) result.append(c.toUpper());
    return result;
}

QString canonicalKeyPath(const QString &path, QString *error)
{
    if (error) error->clear();
    const auto reject = [error](const QString &reason) {
        if (error) *error = reason;
        return QString();
    };
    if (path.isEmpty() || path.contains(QChar(0)) || path.contains(QLatin1Char('\r'))
        || path.contains(QLatin1Char('\n')))
        return reject(QStringLiteral("Registry key path is empty or contains a control character."));
    const QStringList parts = path.split(QLatin1Char('\\'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        if (part.isEmpty()) return reject(QStringLiteral("Registry paths must not contain empty key names."));
    }
    static const QMap<QString, QString> roots = {
        {QStringLiteral("HKCU"), QStringLiteral("HKEY_CURRENT_USER")},
        {QStringLiteral("HKLM"), QStringLiteral("HKEY_LOCAL_MACHINE")},
        {QStringLiteral("HKCR"), QStringLiteral("HKEY_CLASSES_ROOT")},
        {QStringLiteral("HKU"), QStringLiteral("HKEY_USERS")},
        {QStringLiteral("HKCC"), QStringLiteral("HKEY_CURRENT_CONFIG")}
    };
    QString root = parts.first().toUpper();
    if (roots.contains(root)) root = roots.value(root);
    else if (!roots.values().contains(root)) return reject(QStringLiteral("Unsupported or unknown registry hive: %1.").arg(parts.first()));
    return root + (parts.size() > 1 ? QStringLiteral("\\") + parts.mid(1).join(QLatin1Char('\\')) : QString());
}

QByteArray encodeString(const QString &text)
{
    QByteArray data;
    data.reserve((text.size() + 1) * 2);
    for (QChar c : text) {
        data.append(char(c.unicode() & 0xff));
        data.append(char(c.unicode() >> 8));
    }
    data.append(char(0)); data.append(char(0));
    return data;
}

QStringList multiStrings(const QByteArray &data)
{
    QString decoded;
    if (!decodeUtf16(data, false, &decoded)) return {};
    if (decoded.endsWith(QChar(0))) decoded.chop(1); // Array terminator.
    if (decoded.endsWith(QChar(0))) decoded.chop(1); // Last item terminator.
    return decoded.isEmpty() ? QStringList() : decoded.split(QChar(0), Qt::KeepEmptyParts);
}

QString typeName(quint32 type)
{
    switch (type) {
    case 0: return QStringLiteral("REG_NONE");
    case 1: return QStringLiteral("REG_SZ");
    case 2: return QStringLiteral("REG_EXPAND_SZ");
    case 3: return QStringLiteral("REG_BINARY");
    case 4: return QStringLiteral("REG_DWORD");
    case 5: return QStringLiteral("REG_DWORD_BIG_ENDIAN");
    case 6: return QStringLiteral("REG_LINK");
    case 7: return QStringLiteral("REG_MULTI_SZ");
    case 8: return QStringLiteral("REG_RESOURCE_LIST");
    case 9: return QStringLiteral("REG_FULL_RESOURCE_DESCRIPTOR");
    case 10: return QStringLiteral("REG_RESOURCE_REQUIREMENTS_LIST");
    case 11: return QStringLiteral("REG_QWORD");
    default: return QStringLiteral("REG_TYPE_0x%1").arg(type, 0, 16);
    }
}

QString displayValue(const Value &value)
{
    if (value.deleted) return QStringLiteral("Delete value instruction (not executed)");
    if (value.data.size() > 4096)
        return QStringLiteral("%1 bytes, first 256 bytes: %2 …")
            .arg(value.data.size()).arg(QString::fromLatin1(value.data.left(256).toHex(' ')));
    if (((value.type == 4 || value.type == 5) && value.data.size() == 4)
        || (value.type == 11 && value.data.size() == 8)) {
        quint64 n = 0;
        for (int i = 0; i < value.data.size(); ++i) {
            const int at = value.type == 5 ? i : value.data.size() - 1 - i;
            n = (n << 8) | static_cast<uchar>(value.data.at(at));
        }
        return QStringLiteral("%1 (0x%2)").arg(n).arg(n, value.data.size() * 2, 16, QLatin1Char('0'));
    }
    if (value.type == 1 || value.type == 2 || value.type == 6 || value.type == 7) {
        QString decoded;
        if (decodeUtf16(value.data, false, &decoded)) {
            if (value.type == 7 && decoded.endsWith(QString(2, QChar(0)))) {
                QStringList parts = multiStrings(value.data);
                for (QString &part : parts) part = QStringLiteral("\"%1\"").arg(escapedDisplay(part));
                return QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
            }
            if (value.type != 7 && decoded.endsWith(QChar(0))) {
                decoded.chop(1);
                return QStringLiteral("\"%1\"").arg(escapedDisplay(decoded));
            }
        }
        return QStringLiteral("Malformed/unterminated %1 (%2 bytes): %3")
            .arg(typeName(value.type)).arg(value.data.size()).arg(QString::fromLatin1(value.data.toHex(' ')));
    }
    return QStringLiteral("%1 bytes: %2").arg(value.data.size()).arg(QString::fromLatin1(value.data.toHex(' ')));
}

ReadResult parseReg(const QByteArray &bytes, const QString &source, const ReadOptions &options)
{
    if (options.maxBytes <= 0 || options.maxKeys <= 0 || options.maxValues <= 0 || options.maxDepth < 0)
        return failure(source, QStringLiteral("Invalid registry parsing limits."));
    if (bytes.size() > options.maxBytes)
        return failure(source, QStringLiteral("Registry export exceeds the configured byte limit."));
    QString text;
    bool utf16 = false;
    if (bytes.startsWith(QByteArray::fromHex("fffe")) || bytes.startsWith(QByteArray::fromHex("feff"))) {
        utf16 = true;
        if (!decodeUtf16(bytes.mid(2), static_cast<uchar>(bytes.at(0)) == 0xfe, &text))
            return failure(source, QStringLiteral("Invalid UTF-16 registry export (odd byte count or unpaired surrogate)."));
    } else {
        const bool utf8 = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        QTextCodec *codec = QTextCodec::codecForName(utf8 ? QByteArray("UTF-8") : options.ansiCodec);
        if (!codec) return failure(source, QStringLiteral("Unknown ANSI codec: %1.").arg(QString::fromLatin1(options.ansiCodec)));
        QTextCodec::ConverterState state(QTextCodec::ConvertInvalidToNull);
        const QByteArray payload = utf8 ? bytes.mid(3) : bytes;
        text = codec->toUnicode(payload.constData(), payload.size(), &state);
        if (state.invalidChars || state.remainingChars)
            return failure(source, QStringLiteral("Registry export contains invalid bytes for the selected text encoding."));
    }
    if (text.contains(QChar(0))) return failure(source, QStringLiteral("NUL in registry export text; UTF-16 files require a BOM."));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    ReadResult result;
    result.snapshot.source = source;
    QString currentKey;
    bool header = false;
    bool legacy = false;
    int valueCount = 0;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#'))) continue;
        const int startLine = i + 1;
        if (!header) {
            if (line != QStringLiteral("Windows Registry Editor Version 5.00") && line != QStringLiteral("REGEDIT4"))
                return failure(source, QStringLiteral("Missing Windows Registry Editor Version 5.00 or REGEDIT4 header."), startLine);
            header = true;
            legacy = line == QStringLiteral("REGEDIT4") && !utf16;
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            int close = line.indexOf(QLatin1Char(']'), 1);
            while (close >= 0) {
                const QString tail = line.mid(close + 1).trimmed();
                if (tail.isEmpty() || tail.startsWith(QLatin1Char(';'))) break;
                close = line.indexOf(QLatin1Char(']'), close + 1);
            }
            if (close < 1)
                return failure(source, QStringLiteral("Invalid registry key section."), startLine);
            QString path = line.mid(1, close - 1);
            const bool deleted = path.startsWith(QLatin1Char('-'));
            if (deleted) path.remove(0, 1);
            QString error;
            path = canonicalKeyPath(path, &error);
            if (path.isEmpty()) return failure(source, error, startLine);
            if (path.count(QLatin1Char('\\')) > options.maxDepth)
                return failure(source, QStringLiteral("Registry key exceeds depth limit."), startLine);
            currentKey = identity(path);
            QString ancestor = currentKey;
            while (ancestor.contains(QLatin1Char('\\'))) {
                ancestor = ancestor.left(ancestor.lastIndexOf(QLatin1Char('\\')));
                const auto parent = result.snapshot.keys.constFind(ancestor);
                if (parent != result.snapshot.keys.cend() && parent->deleted)
                    return failure(source, QStringLiteral("A deleted ancestor and its descendant sections have order-dependent effects; compare a registry export instead."), startLine);
            }
            if (deleted) {
                const QString prefix = currentKey + QLatin1Char('\\');
                const auto child = result.snapshot.keys.lowerBound(prefix);
                if (child != result.snapshot.keys.cend() && child.key().startsWith(prefix))
                    return failure(source, QStringLiteral("A deleted ancestor and its descendant sections have order-dependent effects; compare a registry export instead."), startLine);
            }
            if (result.snapshot.keys.contains(currentKey)) {
                const Key &old = result.snapshot.keys.value(currentKey);
                if (old.deleted != deleted)
                    return failure(source, QStringLiteral("Conflicting key create/delete sections cannot be represented as a snapshot."), startLine);
            } else {
                if (result.snapshot.keys.size() >= options.maxKeys)
                    return failure(source, QStringLiteral("Registry export exceeds key limit."), startLine);
                Key key; key.path = path; key.deleted = deleted;
                result.snapshot.keys.insert(currentKey, key);
            }
            continue;
        }
        if (currentKey.isEmpty()) return failure(source, QStringLiteral("Value appears before any registry key section."), startLine);
        if (result.snapshot.keys.value(currentKey).deleted)
            return failure(source, QStringLiteral("A deleted key section cannot contain values."), startLine);
        const QString payload = valuePayload(line);
        const bool hexEncoded = payload.startsWith(QStringLiteral("hex"), Qt::CaseInsensitive);
        if (hexEncoded)
            line = line.left(line.size() - payload.size()) + payload.section(QLatin1Char(';'), 0, 0).trimmed();
        if (hexEncoded && line.endsWith(QLatin1Char('\\'))) {
            // Only hex byte lists use line continuation. Quoted strings never do.
            while (line.endsWith(QLatin1Char('\\'))) {
                line.chop(1); line = line.trimmed();
                if (!line.endsWith(QLatin1Char(',')) || i + 1 >= lines.size())
                    return failure(source, QStringLiteral("Invalid or unfinished hex continuation."), startLine);
                const QString next = lines.at(++i).section(QLatin1Char(';'), 0, 0).trimmed();
                if (next.isEmpty() || next.startsWith(QLatin1Char(';')) || next.startsWith(QLatin1Char('#')))
                    return failure(source, QStringLiteral("Expected bytes on the continued line."), i + 1);
                line += next;
            }
        }
        Value value;
        QString error;
        if (!parseValue(line, &value, &error)) return failure(source, error, startLine);
        // REGEDIT4 hex string types contain ANSI bytes; version 5 uses UTF-16LE.
        // Quoted values have already been converted from the file's text codec.
        if (legacy && !value.deleted && (value.type == 1 || value.type == 2 || value.type == 7)
            && hexEncoded) {
            QTextCodec *codec = QTextCodec::codecForName(options.ansiCodec);
            if (!codec) return failure(source, QStringLiteral("Unknown ANSI codec for REGEDIT4 hex data."), startLine);
            QTextCodec::ConverterState state(QTextCodec::ConvertInvalidToNull);
            const QString decoded = codec->toUnicode(value.data.constData(), value.data.size(), &state);
            if (state.invalidChars || state.remainingChars)
                return failure(source, QStringLiteral("Invalid ANSI string bytes in REGEDIT4 hex data."), startLine);
            value.data = encodeString(decoded);
            value.data.chop(2); // Keep exactly the supplied terminators, including absence.
        }
        if (++valueCount > options.maxValues) return failure(source, QStringLiteral("Registry export exceeds value limit."), startLine);
        result.snapshot.keys[currentKey].values.insert(identity(value.name), value);
    }
    if (!header) return failure(source, QStringLiteral("Registry export has no version header."));
    result.ok = true;
    return result;
}

ReadResult readRegFile(const QString &path, const ReadOptions &options)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(path, QStringLiteral("Cannot read registry export: %1").arg(file.errorString()));
    if (options.maxBytes <= 0 || options.maxBytes >= std::numeric_limits<int>::max() || file.size() > options.maxBytes)
        return failure(path, QStringLiteral("Registry export exceeds the byte limit or the limit is invalid."));
    const QByteArray bytes = file.read(options.maxBytes + 1);
    if (file.error() != QFile::NoError) return failure(path, file.errorString());
    return parseReg(bytes, QFileInfo(file).absoluteFilePath(), options);
}

Comparison compare(const Snapshot &left, const Snapshot &right)
{
    Comparison result;
    QSet<QString> pathSet;
    for (auto it = left.keys.cbegin(); it != left.keys.cend(); ++it) pathSet.insert(it.key());
    for (auto it = right.keys.cbegin(); it != right.keys.cend(); ++it) pathSet.insert(it.key());
    QStringList paths = pathSet.values();
    std::sort(paths.begin(), paths.end());
    const auto append = [&result](Difference entry) {
        if (entry.status == Status::Unreadable && entry.kind == EntryKind::Key) ++result.unreadableKeys;
        else if (entry.status != Status::Unreadable && entry.status != Status::Equal) ++result.differenceCount;
        result.entries.append(std::move(entry));
    };
    for (const QString &path : paths) {
        const auto lit = left.keys.constFind(path), rit = right.keys.constFind(path);
        const Key *l = lit == left.keys.cend() ? nullptr : &lit.value();
        const Key *r = rit == right.keys.cend() ? nullptr : &rit.value();
        Difference key;
        key.path = l ? l->path : r->path;
        key.hasLeft = l; key.hasRight = r;
        QStringList errors;
        const QString le = unreadableAncestor(left, path), re = unreadableAncestor(right, path);
        if (!le.isEmpty()) errors.append(QStringLiteral("Left unreadable: %1").arg(le));
        if (!re.isEmpty()) errors.append(QStringLiteral("Right unreadable: %1").arg(re));
        key.detail = errors.join(QStringLiteral("; "));
        if (!errors.isEmpty()) key.status = Status::Unreadable;
        else if (!l) key.status = Status::OnlyRight;
        else if (!r) key.status = Status::OnlyLeft;
        else if (l->deleted != r->deleted) key.status = Status::OperationChanged;
        if (l && l->deleted) key.detail += QStringLiteral(" Left: delete key instruction (not executed).");
        if (r && r->deleted) key.detail += QStringLiteral(" Right: delete key instruction (not executed).");
        append(key);
        QSet<QString> names;
        if (l) for (auto it = l->values.cbegin(); it != l->values.cend(); ++it) names.insert(it.key());
        if (r) for (auto it = r->values.cbegin(); it != r->values.cend(); ++it) names.insert(it.key());
        QStringList sortedNames = names.values();
        std::sort(sortedNames.begin(), sortedNames.end());
        for (const QString &name : sortedNames) {
            Difference value;
            value.kind = EntryKind::Value; value.path = key.path;
            value.hasLeft = l && l->values.contains(name);
            value.hasRight = r && r->values.contains(name);
            if (value.hasLeft) value.left = l->values.value(name);
            if (value.hasRight) value.right = r->values.value(name);
            value.valueName = value.hasLeft ? value.left.name : value.right.name;
            if (key.status == Status::Unreadable) { value.status = Status::Unreadable; value.detail = key.detail; }
            else if (!value.hasLeft) value.status = Status::OnlyRight;
            else if (!value.hasRight) value.status = Status::OnlyLeft;
            else if (value.left.deleted != value.right.deleted) value.status = Status::OperationChanged;
            else if (!value.left.deleted && value.left.type != value.right.type) value.status = Status::TypeChanged;
            else if (!value.left.deleted && value.left.data != value.right.data) value.status = Status::DataChanged;
            append(value);
        }
    }
    return result;
}

QString statusName(Status status)
{
    switch (status) {
    case Status::Equal: return QStringLiteral("Equal");
    case Status::OnlyLeft: return QStringLiteral("Only left");
    case Status::OnlyRight: return QStringLiteral("Only right");
    case Status::TypeChanged: return QStringLiteral("Value type differs");
    case Status::DataChanged: return QStringLiteral("Value data differs");
    case Status::OperationChanged: return QStringLiteral("Delete instruction differs");
    case Status::Unreadable: return QStringLiteral("Unreadable / unknown");
    }
    return {};
}

}} // namespace LqCompare::Registry
