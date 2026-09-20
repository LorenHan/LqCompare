#include "versioncompare.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
namespace LqCompare { namespace Version {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("VersionCompare", text); }
bool segments(QString text, bool allowV, QStringList *result)
{
    text = text.trimmed();
    if (allowV && (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V')))) text.remove(0, 1);
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+(?:\\.[0-9]+)*$"));
    if (text.size() > 4096 || !pattern.match(text).hasMatch()) return false;
    *result = text.split(QLatin1Char('.'));
    for (QString &part : *result) {
        int first = 0;
        while (first < part.size() - 1 && part.at(first) == QLatin1Char('0')) ++first;
        part.remove(0, first);
    }
    return true;
}
QString hex(quint64 value) { return QStringLiteral("0x%1").arg(value, 0, 16).toUpper(); }
struct Value { QString text; bool version = false; };
using Fields = QMap<QPair<QString, QString>, Value>;
Fields flatten(const FileInfo &info)
{
    Fields fields;
    const auto add = [&fields](const QString &group, const QString &key, const QString &value, bool version = false) {
        // Display names may themselves end in a duplicate suffix; never let
        // such a name overwrite an earlier symbol's row.
        QString uniqueKey = key;
        int suffix = 2;
        while (fields.contains(qMakePair(group, uniqueKey)))
            uniqueKey = key + QStringLiteral(" [%1]").arg(suffix++);
        fields.insert(qMakePair(group, uniqueKey), {value, version});
    };
    for (auto i = info.metadata.cbegin(); i != info.metadata.cend(); ++i) add(tr("File metadata"), i.key(), i.value());
    add(tr("File metadata"), tr("Format"), info.status == Status::Pe ? (info.pe32Plus ? QStringLiteral("PE32+") : QStringLiteral("PE32")) : tr("Non-PE"));
    add(tr("Version availability"), tr("Version resource"), !info.versions.isEmpty() ? tr("Present (%1)").arg(info.versions.size()) : tr("No version resource"));
    if (!info.message.isEmpty()) add(tr("Version availability"), tr("Details"), info.message);
    for (auto i = info.headers.cbegin(); i != info.headers.cend(); ++i) add(tr("PE headers"), i.key(), i.value());
    for (int n = 0; n < info.sections.size(); ++n) {
        const auto &s = info.sections.at(n);
        const QString key = QStringLiteral("[%1] %2 / ").arg(n).arg(s.name);
        add(tr("Sections"), key + QStringLiteral("VirtualAddress"), hex(s.virtualAddress));
        add(tr("Sections"), key + QStringLiteral("VirtualSize"), QString::number(s.virtualSize));
        add(tr("Sections"), key + QStringLiteral("RawOffset"), hex(s.rawOffset));
        add(tr("Sections"), key + QStringLiteral("RawSize"), QString::number(s.rawSize));
        add(tr("Sections"), key + QStringLiteral("Characteristics"), hex(s.characteristics));
    }
    QMap<QString, int> duplicates;
    for (const auto &s : info.imports) {
        QString key = s.dll + QStringLiteral(" / ") + (s.byOrdinal ? QStringLiteral("#%1").arg(s.ordinal) : s.name);
        const int count = duplicates[key]++;
        if (count) key += QStringLiteral(" [%1]").arg(count + 1);
        add(tr("Imports"), key, s.byOrdinal ? tr("Ordinal %1").arg(s.ordinal) : s.name.isEmpty() ? tr("DLL dependency; no symbol entries") : tr("Name: %1; hint: %2").arg(s.name).arg(s.hint));
    }
    duplicates.clear();
    for (const auto &s : info.exports) {
        QString key = QStringLiteral("#%1 / %2").arg(s.ordinal).arg(s.name.isEmpty() ? tr("Unnamed") : s.name);
        const int count = duplicates[key]++;
        if (count) key += QStringLiteral(" [%1]").arg(count + 1);
        add(tr("Exports"), key, tr("RVA %1%2").arg(hex(s.address), s.forwarder.isEmpty() ? QString() : tr("; forwards to %1").arg(s.forwarder)));
    }
    duplicates.clear();
    for (const auto &v : info.versions) {
        QString group = tr("Version — resource %1 / language %2").arg(v.resourceName, v.language);
        const int count = duplicates[group]++;
        if (count) group += QStringLiteral(" [%1]").arg(count + 1);
        for (auto i = v.fixed.cbegin(); i != v.fixed.cend(); ++i)
            add(group, QStringLiteral("Fixed/") + i.key(), i.value(), i.key() == QStringLiteral("FileVersion") || i.key() == QStringLiteral("ProductVersion"));
        for (auto i = v.strings.cbegin(); i != v.strings.cend(); ++i) {
            const QString key = i.key().section(QLatin1Char('/'), -1);
            add(group, QStringLiteral("Strings/") + i.key(), i.value(), key == QStringLiteral("FileVersion") || key == QStringLiteral("ProductVersion"));
        }
        QStringList translations;
        for (quint32 language : v.translations) translations << hex(language);
        if (!translations.isEmpty()) add(group, QStringLiteral("Translations"), translations.join(QStringLiteral(", ")));
    }
    return fields;
}
QString csv(QString value) { value.replace(QLatin1Char('"'), QStringLiteral("\"\"")); return QLatin1Char('"') + value + QLatin1Char('"'); }
}
Relation compareNumbers(const QString &left, const QString &right, const CompareOptions &options)
{
    QStringList a, b;
    if (!segments(left, options.allowLeadingV, &a) || !segments(right, options.allowLeadingV, &b)) return Relation::Incomparable;
    if (!options.padMissingVersionSegments && a.size() != b.size()) return Relation::Incomparable;
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        const QString x = i < a.size() ? a.at(i) : QStringLiteral("0"), y = i < b.size() ? b.at(i) : QStringLiteral("0");
        const int order = x.size() == y.size() ? QString::compare(x, y, Qt::CaseSensitive) : (x.size() < y.size() ? -1 : 1);
        if (order) return order > 0 ? Relation::LeftHigher : Relation::RightHigher;
    }
    return Relation::Equal;
}
QString relationText(Relation relation)
{
    switch (relation) {
    case Relation::Equal: return tr("Same numeric version");
    case Relation::LeftHigher: return tr("Left higher");
    case Relation::RightHigher: return tr("Right higher");
    case Relation::Incomparable: return tr("Incomparable");
    }
    return {};
}
QVector<Row> compare(const FileInfo &left, const FileInfo &right, const CompareOptions &options)
{
    const Fields a = flatten(left), b = flatten(right);
    QSet<QPair<QString, QString>> keys;
    for (auto i = a.cbegin(); i != a.cend(); ++i) keys.insert(i.key());
    for (auto i = b.cbegin(); i != b.cend(); ++i) keys.insert(i.key());
    auto ordered = keys.values(); std::sort(ordered.begin(), ordered.end());
    QVector<Row> rows; rows.reserve(ordered.size());
    for (const auto &key : ordered) {
        Row row; row.group = key.first; row.key = key.second;
        row.left = a.value(key).text; row.right = b.value(key).text;
        row.versionNumber = a.value(key).version || b.value(key).version;
        row.difference = !a.contains(key) ? Difference::RightOnly : !b.contains(key) ? Difference::LeftOnly :
                         row.left == row.right ? Difference::Equal : Difference::Changed;
        if (row.versionNumber) {
            row.relation = compareNumbers(row.left, row.right, options);
            if (options.ignoreVersionNumbers && row.difference != Difference::Equal) row.difference = Difference::Ignored;
        }
        rows.append(row);
    }
    return rows;
}
QString toCsv(const QVector<Row> &rows)
{
    QString result = QStringLiteral("\"Group\",\"Field\",\"Left\",\"Right\",\"Difference\",\"Numeric relation\"\r\n");
    for (const Row &row : rows) {
        QString difference;
        switch (row.difference) {
        case Difference::Equal: difference = QStringLiteral("equal"); break;
        case Difference::Changed: difference = QStringLiteral("changed"); break;
        case Difference::LeftOnly: difference = QStringLiteral("left-only"); break;
        case Difference::RightOnly: difference = QStringLiteral("right-only"); break;
        case Difference::Ignored: difference = QStringLiteral("ignored"); break;
        }
        result += csv(row.group) + QLatin1Char(',') + csv(row.key) + QLatin1Char(',') + csv(row.left) + QLatin1Char(',') + csv(row.right) + QLatin1Char(',') + csv(difference) + QLatin1Char(',') + csv(row.versionNumber ? relationText(row.relation) : QString()) + QStringLiteral("\r\n");
    }
    return result;
}
} }
