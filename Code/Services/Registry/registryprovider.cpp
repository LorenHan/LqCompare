#include "registrycompare.h"

#include <utility>

namespace LqCompare { namespace Registry {
namespace {

bool containsPath(const QString &parent, const QString &path)
{
    return path == parent || path.startsWith(parent + QLatin1Char('\\'));
}

QString optionsError(const ReadOptions &options)
{
    if (options.maxBytes < 0 || options.maxKeys < 1 || options.maxValues < 0
            || options.maxDepth < 0) {
        return QStringLiteral("Registry read limits require maxBytes, maxValues and maxDepth >= 0, and maxKeys >= 1.");
    }
    return {};
}

void addError(Key &key, const QString &error, quint32 nativeError = 0)
{
    if (key.error.isEmpty())
        key.error = error;
    else if (!key.error.contains(error))
        key.error += QStringLiteral("; ") + error;
    if (!key.nativeError)
        key.nativeError = nativeError;
}

ReadResult failedRead(const QString &source, const QString &path, const QString &error)
{
    ReadResult result;
    result.snapshot.source = source;
    result.error = error;
    if (!path.isEmpty()) {
        Key key;
        key.path = path;
        key.error = error;
        result.snapshot.keys.insert(identity(path), key);
    }
    return result;
}

} // namespace

MemoryProvider::MemoryProvider(Snapshot snapshot) : m_snapshot(std::move(snapshot)) {}

ReadResult MemoryProvider::read(const QString &root, const ReadOptions &options) const
{
    QString error;
    const QString path = canonicalKeyPath(root, &error);
    if (path.isEmpty())
        return failedRead(m_snapshot.source, {}, error);
    error = optionsError(options);
    if (!error.isEmpty())
        return failedRead(m_snapshot.source, path, error);

    const QString rootId = identity(path);
    QMap<QString, Key> candidates;
    Key rootKey;
    rootKey.path = path;
    bool coveredByUnreadableAncestor = false;

    // Exports may omit ancestor headers. Canonicalize the actual stored paths,
    // rather than trusting the caller's map spelling or matching a raw prefix.
    for (auto it = m_snapshot.keys.cbegin(); it != m_snapshot.keys.cend(); ++it) {
        const QString keyPath = canonicalKeyPath(it->path, &error);
        if (keyPath.isEmpty())
            return failedRead(m_snapshot.source, path,
                              QStringLiteral("Invalid memory snapshot key: %1").arg(error));
        const QString keyId = identity(keyPath);
        if (keyId != rootId && containsPath(keyId, rootId) && !it->error.isEmpty()) {
            coveredByUnreadableAncestor = true;
            addError(rootKey, QStringLiteral("Unreadable ancestor %1: %2")
                     .arg(keyPath, it->error), it->nativeError);
        }
        if (!containsPath(rootId, keyId))
            continue;
        if (candidates.contains(keyId))
            return failedRead(m_snapshot.source, path,
                              QStringLiteral("Duplicate canonical key in memory snapshot: %1").arg(keyPath));
        Key key = it.value();
        key.path = keyPath;
        candidates.insert(keyId, key);
    }

    if (candidates.isEmpty() && !coveredByUnreadableAncestor)
        return failedRead(m_snapshot.source, path,
                          QStringLiteral("Registry root is not present in the memory snapshot: %1").arg(path));
    if (candidates.contains(rootId)) {
        Key explicitRoot = candidates.value(rootId);
        if (!rootKey.error.isEmpty())
            addError(explicitRoot, rootKey.error, rootKey.nativeError);
        rootKey = explicitRoot;
    }
    candidates.insert(rootId, rootKey);

    // Materialize only the ancestors inside the requested subtree. This also
    // makes maxDepth apply to a sparse export exactly as to a live tree.
    const QStringList originalIds = candidates.keys();
    for (const QString &id : originalIds) {
        QString ancestor = candidates.value(id).path;
        while (identity(ancestor) != rootId) {
            ancestor = ancestor.left(ancestor.lastIndexOf(QLatin1Char('\\')));
            const QString ancestorId = identity(ancestor);
            if (!candidates.contains(ancestorId)) {
                Key key;
                key.path = ancestor;
                candidates.insert(ancestorId, key);
            }
        }
    }

    ReadResult result;
    result.ok = true;
    result.snapshot.source = m_snapshot.source;
    qint64 bytes = 0;
    int values = 0;
    const int rootDepth = path.count(QLatin1Char('\\'));
    bool truncated = false;
    QString limitError;
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
        const Key &source = it.value();
        if (source.path.count(QLatin1Char('\\')) - rootDepth > options.maxDepth) {
            truncated = true;
            limitError = QStringLiteral("Registry read depth limit reached; omitted descendants are unreadable.");
            continue;
        }
        const qint64 keyBytes = qint64(source.path.size()) * 2 + qint64(source.error.size()) * 2;
        if (result.snapshot.keys.size() >= options.maxKeys || keyBytes > options.maxBytes - bytes) {
            truncated = true;
            limitError = QStringLiteral("Registry read key/byte limit reached; omitted content is unreadable.");
            break;
        }
        Key key;
        key.path = source.path;
        key.deleted = source.deleted;
        key.error = source.error;
        key.nativeError = source.nativeError;
        bytes += keyBytes;
        for (auto valueIt = source.values.cbegin(); valueIt != source.values.cend(); ++valueIt) {
            const Value &value = valueIt.value();
            const qint64 valueBytes = qint64(value.name.size()) * 2 + value.data.size();
            if (values >= options.maxValues || valueBytes > options.maxBytes - bytes) {
                truncated = true;
                limitError = QStringLiteral("Registry read value/byte limit reached; omitted content is unreadable.");
                addError(key, limitError);
                break;
            }
            const QString valueId = identity(value.name);
            if (key.values.contains(valueId)) {
                addError(key, QStringLiteral("Duplicate canonical value name in memory snapshot."));
                continue;
            }
            key.values.insert(valueId, value);
            bytes += valueBytes;
            ++values;
        }
        result.snapshot.keys.insert(it.key(), key);
        // Once total resources run out, mark the root as unknown so omitted
        // siblings can never be interpreted as missing by the comparison.
        if (truncated && !key.error.isEmpty() && key.error.contains(limitError))
            break;
    }
    if (truncated) {
        if (!result.snapshot.keys.contains(rootId)) {
            rootKey.values.clear();
            result.snapshot.keys.insert(rootId, rootKey);
        }
        addError(result.snapshot.keys[rootId], limitError);
        result.error = limitError;
    }
    return result;
}

#ifndef Q_OS_WIN
namespace {
class UnavailableProvider final : public Provider {
public:
    ReadResult read(const QString &root, const ReadOptions &) const override
    {
        const QString path = canonicalKeyPath(root);
        return failedRead(QStringLiteral("Local registry"), path,
                          QStringLiteral("Live registry reading is available only on Windows; .reg file parsing remains available."));
    }
};
} // namespace

std::unique_ptr<Provider> createLocalProvider()
{
    return std::unique_ptr<Provider>(new UnavailableProvider);
}

bool localProviderAvailable() { return false; }

QString localProviderDescription()
{
    return QStringLiteral("Live registry: Windows only. Exported .reg files can be parsed on this platform.");
}
#endif

}} // namespace LqCompare::Registry
