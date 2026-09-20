#ifndef LQCOMPARE_FORMATDEFINITION_H
#define LQCOMPARE_FORMATDEFINITION_H

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Format {

// Magic signatures are data. No converter, decoder or process is executed here.
struct MagicSignature
{
    int offset = 0;
    QByteArray bytes;
};

struct FormatDefinition
{
    QString id;
    QString name;
    QString sessionTypeId;
    QStringList masks;
    QVector<MagicSignature> signatures;
    // Encoding, syntax, conversions, filters and column settings remain opaque data.
    QJsonObject settings;
    bool builtIn = false;
};

QVector<FormatDefinition> builtInFormatDefinitions();
QStringList validateDefinition(const FormatDefinition &definition);

struct DefinitionLoadResult
{
    bool documentValid = false;
    QVector<FormatDefinition> definitions;
    QStringList diagnostics;
};

// Versioned JSON. Bad entries are skipped individually. Invalid document/version
// produces no entries. Missing fields inherit from baseId, or an existing same ID.
DefinitionLoadResult parseDefinitions(const QByteArray &json,
                                     const QVector<FormatDefinition> &base = {});
QByteArray serializeDefinitions(const QVector<FormatDefinition> &definitions);
DefinitionLoadResult loadDefinitions(const QString &path,
                                    const QVector<FormatDefinition> &base = {});
bool saveDefinitions(const QString &path, const QVector<FormatDefinition> &definitions,
                     QString *error = nullptr);

// User IDs replace matching built-ins without mutating either input. Explicit
// priority IDs come first; remaining user entries precede remaining built-ins.
QVector<FormatDefinition> mergeDefinitions(const QVector<FormatDefinition> &builtIns,
                                         const QVector<FormatDefinition> &user,
                                         const QStringList &priorityIds = {});

} // namespace Format
} // namespace LqCompare
#endif
