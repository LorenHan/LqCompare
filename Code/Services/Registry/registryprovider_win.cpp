#include "registrycompare.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace LqCompare { namespace Registry {
namespace {

// Keep the registry view explicit for the project's 32-bit Windows build.
constexpr REGSAM ReadAccess = KEY_READ | KEY_WOW64_64KEY;
constexpr int MaxNameCharacters = 32768;
constexpr int MaxBufferRetries = 16;

struct ScopedKey {
    HKEY handle = nullptr;
    ~ScopedKey()
    {
        // Opening the empty HKCU path returns the predefined handle itself.
        if (handle && handle != HKEY_CURRENT_USER)
            RegCloseKey(handle);
    }
};

QString windowsError(LSTATUS code)
{
    wchar_t message[1024] = {};
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, DWORD(code), 0, message, 1024, nullptr);
    const QString detail = count ? QString::fromWCharArray(message, int(count)).trimmed()
                                 : QStringLiteral("Registry operation failed");
    return QStringLiteral("%1 (Windows error %2)").arg(detail).arg(quint32(code));
}

void addError(Key &key, const QString &message, quint32 code = 0)
{
    if (key.error.isEmpty())
        key.error = message;
    else if (!key.error.contains(message))
        key.error += QStringLiteral("; ") + message;
    if (!key.nativeError)
        key.nativeError = code;
}

void nativeError(Key &key, const QString &operation, LSTATUS code)
{
    addError(key, QStringLiteral("%1: %2").arg(operation, windowsError(code)), quint32(code));
}

struct KeyInfo {
    DWORD subkeys = 0;
    DWORD longestSubkey = 0;
    DWORD values = 0;
    DWORD longestValueName = 0;
    DWORD largestValue = 0;
    FILETIME modified = {};
};

LSTATUS keyInfo(HKEY handle, KeyInfo &info)
{
    return RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &info.subkeys,
                           &info.longestSubkey, nullptr, &info.values,
                           &info.longestValueName, &info.largestValue,
                           nullptr, &info.modified);
}

struct PendingKey {
    QString path;
    int depth = 0;
};

class LocalReader {
public:
    LocalReader(const QString &root, const ReadOptions &limits)
        : rootPath(root), rootId(identity(root)), options(limits)
    {
        result.ok = true;
        result.snapshot.source = QStringLiteral("Local registry: %1 (64-bit view)").arg(root);
        Key key;
        key.path = root;
        result.snapshot.keys.insert(rootId, key);
    }

    ReadResult run()
    {
        bytes = qint64(rootPath.size()) * 2;
        if (bytes > options.maxBytes) {
            limit(QStringLiteral("Registry read byte limit reached before the root could be read."));
            return result;
        }
        pending.append({rootPath, 0});
        while (!pending.isEmpty() && !stopped) {
            const PendingKey current = pending.takeLast();
            readKey(current);
        }
        if (stopped) {
            for (const PendingKey &key : pending)
                addError(result.snapshot.keys[identity(key.path)],
                         QStringLiteral("Subtree not read because the total registry read limit was reached."));
        }
        return result;
    }

private:
    void limit(const QString &message)
    {
        stopped = true;
        result.error = message;
        addError(result.snapshot.keys[rootId], message);
    }

    qint64 remainingBytes() const { return options.maxBytes - bytes; }

    bool growName(std::vector<wchar_t> &buffer, DWORD reported)
    {
        const qint64 wanted = std::max(qint64(buffer.size()) * 2, qint64(reported) + 1);
        const int next = int(std::min(wanted, qint64(MaxNameCharacters)));
        if (next <= int(buffer.size()))
            return false;
        buffer.resize(size_t(next));
        return true;
    }

    void readValues(HKEY handle, Key &key, const KeyInfo &info)
    {
        const int initialName = int(std::min<quint64>(quint64(info.longestValueName) + 1, 256));
        std::vector<wchar_t> name(size_t(std::max(initialName, 1)));
        // A non-null pointer with capacity zero preserves ERROR_MORE_DATA for
        // nonempty data. A null lpData only queries its size and can succeed.
        QByteArray data(int(std::min<qint64>({qint64(info.largestValue), remainingBytes(), 4096})), '\0');
        BYTE emptyData = 0;
        for (DWORD index = 0; ; ++index) {
            LSTATUS status = ERROR_MORE_DATA;
            DWORD nameCharacters = 0;
            DWORD dataBytes = 0;
            DWORD type = 0;
            for (int attempt = 0; attempt < MaxBufferRetries; ++attempt) {
                nameCharacters = DWORD(name.size());
                dataBytes = DWORD(data.size());
                status = RegEnumValueW(handle, index, name.data(), &nameCharacters, nullptr,
                                       &type, data.isEmpty() ? &emptyData
                                                            : reinterpret_cast<BYTE *>(data.data()),
                                       &dataBytes);
                if (status != ERROR_MORE_DATA)
                    break;
                if (qint64(dataBytes) > remainingBytes()
                        || quint64(dataBytes) > quint64(std::numeric_limits<int>::max())) {
                    limit(QStringLiteral("Registry read byte limit reached while reading %1; omitted content is unreadable.")
                          .arg(key.path));
                    addError(key, result.error);
                    return;
                }
                if (dataBytes > DWORD(data.size())) {
                    data.resize(int(dataBytes));
                } else if (!growName(name, nameCharacters)) {
                    break;
                }
            }
            if (status == ERROR_NO_MORE_ITEMS)
                return;
            if (status != ERROR_SUCCESS) {
                nativeError(key, QStringLiteral("Cannot enumerate values"), status);
                return;
            }
            if (nameCharacters >= name.size() || dataBytes > DWORD(data.size())) {
                nativeError(key, QStringLiteral("Invalid value size returned by registry"), ERROR_INVALID_DATA);
                return;
            }
            const qint64 valueBytes = qint64(nameCharacters) * 2 + dataBytes;
            if (values >= options.maxValues || valueBytes > remainingBytes()) {
                limit(QStringLiteral("Registry read value/byte limit reached while reading %1; omitted content is unreadable.")
                      .arg(key.path));
                addError(key, result.error);
                return;
            }
            Value value;
            value.name = QString::fromWCharArray(name.data(), int(nameCharacters));
            value.type = quint32(type);
            // Do not append terminators or decode string values here: the exact
            // bytes (including malformed strings and unknown types) matter.
            value.data = QByteArray(data.constData(), int(dataBytes));
            const QString valueId = identity(value.name);
            if (key.values.contains(valueId)) {
                nativeError(key, QStringLiteral("Registry changed during value enumeration; reload the source"), ERROR_RETRY);
                return;
            }
            key.values.insert(valueId, value);
            ++values;
            bytes += valueBytes;
            // The previous buffer can be larger than the remaining total
            // allowance. Reduce it before reading the next value.
            if (data.size() > remainingBytes())
                data.resize(int(remainingBytes()));
            if (index == std::numeric_limits<DWORD>::max()) {
                nativeError(key, QStringLiteral("Registry enumeration index exhausted"), ERROR_MORE_DATA);
                return;
            }
        }
    }

    void readChildren(HKEY handle, Key &key, const PendingKey &current, const KeyInfo &info)
    {
        const int initialName = int(std::min<quint64>(quint64(info.longestSubkey) + 1, 256));
        std::vector<wchar_t> name(size_t(std::max(initialName, 1)));
        for (DWORD index = 0; ; ++index) {
            LSTATUS status = ERROR_MORE_DATA;
            DWORD characters = 0;
            for (int attempt = 0; attempt < MaxBufferRetries; ++attempt) {
                characters = DWORD(name.size());
                status = RegEnumKeyExW(handle, index, name.data(), &characters,
                                      nullptr, nullptr, nullptr, nullptr);
                if (status != ERROR_MORE_DATA || !growName(name, characters))
                    break;
            }
            if (status == ERROR_NO_MORE_ITEMS)
                return;
            if (status != ERROR_SUCCESS) {
                nativeError(key, QStringLiteral("Cannot enumerate child keys"), status);
                return;
            }
            if (!characters || characters >= name.size()) {
                nativeError(key, QStringLiteral("Invalid child key name returned by registry"), ERROR_INVALID_DATA);
                return;
            }
            if (current.depth >= options.maxDepth) {
                addError(key, QStringLiteral("Registry read depth limit reached; omitted descendants are unreadable."));
                return;
            }
            const QString child = current.path + QLatin1Char('\\')
                    + QString::fromWCharArray(name.data(), int(characters));
            const QString childId = identity(child);
            if (result.snapshot.keys.contains(childId)) {
                nativeError(key, QStringLiteral("Registry changed during child enumeration; reload the source"), ERROR_RETRY);
                return;
            }
            const qint64 childBytes = qint64(child.size()) * 2;
            if (result.snapshot.keys.size() >= options.maxKeys || childBytes > remainingBytes()) {
                limit(QStringLiteral("Registry read key/byte limit reached while reading %1; omitted content is unreadable.")
                      .arg(key.path));
                addError(key, result.error);
                return;
            }
            Key childKey;
            childKey.path = child;
            result.snapshot.keys.insert(childId, childKey);
            bytes += childBytes;
            pending.append({child, current.depth + 1});
            if (index == std::numeric_limits<DWORD>::max()) {
                nativeError(key, QStringLiteral("Registry enumeration index exhausted"), ERROR_MORE_DATA);
                return;
            }
        }
    }

    void readKey(const PendingKey &current)
    {
        Key &key = result.snapshot.keys[identity(current.path)];
        const QString subkey = current.path.mid(QStringLiteral("HKEY_CURRENT_USER").size() + 1);
        ScopedKey opened;
        const LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                            reinterpret_cast<LPCWSTR>(subkey.utf16()),
                                            REG_OPTION_OPEN_LINK, ReadAccess, &opened.handle);
        if (status != ERROR_SUCCESS) {
            nativeError(key, QStringLiteral("Cannot open key for reading"), status);
            return;
        }
        KeyInfo before;
        LSTATUS queryStatus = keyInfo(opened.handle, before);
        if (queryStatus != ERROR_SUCCESS) {
            nativeError(key, QStringLiteral("Cannot query key metadata"), queryStatus);
            return;
        }
        readValues(opened.handle, key, before);
        if (!stopped)
            readChildren(opened.handle, key, current, before);
        KeyInfo after;
        queryStatus = keyInfo(opened.handle, after);
        if (queryStatus != ERROR_SUCCESS) {
            nativeError(key, QStringLiteral("Cannot recheck key metadata"), queryStatus);
        } else if (before.subkeys != after.subkeys || before.values != after.values
                   || before.modified.dwLowDateTime != after.modified.dwLowDateTime
                   || before.modified.dwHighDateTime != after.modified.dwHighDateTime) {
            nativeError(key, QStringLiteral("Registry changed during enumeration; reload the source"), ERROR_RETRY);
        }
    }

    QString rootPath;
    QString rootId;
    const ReadOptions &options;
    ReadResult result;
    QVector<PendingKey> pending;
    qint64 bytes = 0;
    int values = 0;
    bool stopped = false;
};

class LocalProvider final : public Provider {
public:
    ReadResult read(const QString &root, const ReadOptions &options) const override
    {
        ReadResult result;
        QString error;
        const QString path = canonicalKeyPath(root, &error);
        result.snapshot.source = QStringLiteral("Local registry: %1 (64-bit view)").arg(root);
        if (path.isEmpty()) {
            result.error = error;
        } else if (path.section(QLatin1Char('\\'), 0, 0) != QStringLiteral("HKEY_CURRENT_USER")) {
            result.error = QStringLiteral("Live registry reading currently supports only local HKEY_CURRENT_USER. No elevation or remote registry access is performed.");
        } else if (options.maxBytes < 0 || options.maxKeys < 1 || options.maxValues < 0 || options.maxDepth < 0) {
            result.error = QStringLiteral("Registry read limits require maxBytes, maxValues and maxDepth >= 0, and maxKeys >= 1.");
        } else {
            return LocalReader(path, options).run();
        }
        if (!path.isEmpty()) {
            Key key;
            key.path = path;
            key.error = result.error;
            result.snapshot.keys.insert(identity(path), key);
        }
        return result;
    }
};

QString permissionDescription()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return QStringLiteral("Current process permission level unavailable (Windows error %1)").arg(GetLastError());
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const BOOL read = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    const DWORD error = read ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!read)
        return QStringLiteral("Current process permission level unavailable (Windows error %1)").arg(error);
    return elevation.TokenIsElevated ? QStringLiteral("Current process: elevated token")
                                     : QStringLiteral("Current process: standard token (not elevated)");
}

} // namespace

std::unique_ptr<Provider> createLocalProvider()
{
    return std::unique_ptr<Provider>(new LocalProvider);
}

bool localProviderAvailable() { return true; }

QString localProviderDescription()
{
    return QStringLiteral("%1. Read-only local HKCU, 64-bit registry view. ACLs and protected keys can deny reads even when elevated; failures remain unreadable. Live enumeration is not an atomic snapshot. No registry writes or elevation requests.")
            .arg(permissionDescription());
}

}} // namespace LqCompare::Registry
