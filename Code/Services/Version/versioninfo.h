#ifndef LQCOMPARE_VERSIONINFO_H
#define LQCOMPARE_VERSIONINFO_H
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>

namespace LqCompare { namespace Version {
struct Limits {
    qint64 maxFileBytes = 64 * 1024 * 1024;
    int maxSections = 96;
    int maxEntries = 16384;
    int maxStringBytes = 4096;
    int maxResourceDepth = 8;
};
struct Section {
    QString name;
    quint32 virtualAddress = 0, virtualSize = 0, rawOffset = 0, rawSize = 0, characteristics = 0;
};
struct Import {
    QString dll, name;
    bool byOrdinal = false;
    quint16 ordinal = 0, hint = 0;
};
struct Export {
    QString name, forwarder;
    quint32 ordinal = 0, address = 0;
};
struct VersionResource {
    QString resourceName, language;
    QMap<QString, QString> fixed;
    // Keys preserve the StringTable language/codepage, e.g. 040904B0/CompanyName.
    QMap<QString, QString> strings;
    QVector<quint32> translations; // language in low WORD, codepage in high WORD
};
enum class Status { Pe, NonPe, Invalid, IoError, LimitExceeded };
struct FileInfo {
    Status status = Status::Invalid;
    QString path, message;
    bool pe32Plus = false;
    QMap<QString, QString> metadata;
    QMap<QString, QString> headers;
    QVector<Section> sections;
    QVector<Import> imports;
    QVector<Export> exports;
    QVector<VersionResource> versions;
    bool usable() const { return status == Status::Pe || status == Status::NonPe; }
};
// Pure parser: never loads a module, invokes the OS loader or follows DLL paths.
FileInfo parse(const QByteArray &bytes, const Limits &limits = Limits());
// Reads regular files only, read-only, bounded; non-PE retains generic metadata.
FileInfo inspectFile(const QString &path, const Limits &limits = Limits());
} }
#endif
