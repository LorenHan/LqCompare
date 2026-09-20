#ifndef LQCOMPARE_REGISTRYCOMPARE_H
#define LQCOMPARE_REGISTRYCOMPARE_H

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace LqCompare { namespace Registry {

// Type numbers are Windows REG_* values, retained even for unknown types.
struct Value {
    QString name; // Empty is the default value.
    quint32 type = 1;
    QByteArray data; // Exact registry bytes; UTF-16LE for string types.
    bool deleted = false; // Export instruction, never executed.
};
struct Key {
    QString path;
    QMap<QString, Value> values; // Keys are identity(value.name).
    bool deleted = false;
    QString error; // An unreadable subtree is unknown, never absent/equal.
    quint32 nativeError = 0;
};
struct Snapshot {
    QString source;
    QMap<QString, Key> keys; // Keys are identity(key.path).
};
struct ReadResult {
    bool ok = false;
    Snapshot snapshot;
    QString error;
    int errorLine = 0;
};
struct ReadOptions {
    QByteArray ansiCodec = "Windows-1252"; // Explicit, deterministic ANSI fallback.
    qint64 maxBytes = 32 * 1024 * 1024;
    int maxKeys = 100000;
    int maxValues = 1000000;
    int maxDepth = 128;
};

QString identity(const QString &name);
QString canonicalKeyPath(const QString &path, QString *error = nullptr);
QString typeName(quint32 type);
QString displayValue(const Value &value);
QByteArray encodeString(const QString &text);
QStringList multiStrings(const QByteArray &data);
ReadResult parseReg(const QByteArray &bytes, const QString &source = {},
                    const ReadOptions &options = {});
ReadResult readRegFile(const QString &path, const ReadOptions &options = {});

// Read-only by construction: no write, elevation, remote or mutation API.
class Provider {
public:
    virtual ~Provider() = default;
    virtual ReadResult read(const QString &root = QStringLiteral("HKEY_CURRENT_USER"),
                            const ReadOptions &options = {}) const = 0;
};
class MemoryProvider final : public Provider {
public:
    explicit MemoryProvider(Snapshot snapshot);
    ReadResult read(const QString &root = QStringLiteral("HKEY_CURRENT_USER"),
                    const ReadOptions &options = {}) const override;
private:
    Snapshot m_snapshot;
};
std::unique_ptr<Provider> createLocalProvider();
bool localProviderAvailable();
QString localProviderDescription();

enum class EntryKind { Key, Value };
enum class Status { Equal, OnlyLeft, OnlyRight, TypeChanged, DataChanged,
                    OperationChanged, Unreadable };
struct Difference {
    EntryKind kind = EntryKind::Key;
    QString path;
    QString valueName;
    Status status = Status::Equal;
    bool hasLeft = false;
    bool hasRight = false;
    Value left;
    Value right;
    QString detail;
};
struct Comparison {
    QVector<Difference> entries;
    int differenceCount = 0; // Excludes Unreadable: unknown is not a difference.
    int unreadableKeys = 0;
    bool complete() const { return unreadableKeys == 0; }
    bool equal() const { return complete() && differenceCount == 0; }
};
Comparison compare(const Snapshot &left, const Snapshot &right);
QString statusName(Status status);

}} // namespace LqCompare::Registry
#endif
