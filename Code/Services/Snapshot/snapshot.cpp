#include "snapshot.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <memory>

namespace LqCompare {
namespace Snapshot {
namespace {

constexpr qint64 MaximumDocumentBytes = 64 * 1024 * 1024;
constexpr int MaximumEntries = 1000000;
constexpr quint32 KnownAttributes = 0x3f;

bool stopped(const std::atomic_bool *cancelled)
{
    return cancelled && cancelled->load(std::memory_order_relaxed);
}

QString invalid(const QString &field)
{
    return QObject::tr("快照字段无效或缺失：%1").arg(field);
}

bool safePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/'))
        || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':')))
        return false;
    for (const QChar c : path) {
        if (c.unicode() < 32 || c.unicode() == 127)
            return false;
    }
    for (const auto &part : path.split(QLatin1Char('/'), Qt::KeepEmptyParts)) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

bool validHash(const QByteArray &value)
{
    if (value.size() != 64)
        return false;
    for (const char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

QString kindName(Folder::Kind kind)
{
    switch (kind) {
    case Folder::Kind::File: return QStringLiteral("file");
    case Folder::Kind::Directory: return QStringLiteral("directory");
    case Folder::Kind::SymbolicLink: return QStringLiteral("symlink");
    case Folder::Kind::Other: return QStringLiteral("other");
    case Folder::Kind::Missing: return {};
    }
    return {};
}

Folder::Kind kindFromName(const QString &name)
{
    if (name == QLatin1String("file")) return Folder::Kind::File;
    if (name == QLatin1String("directory")) return Folder::Kind::Directory;
    if (name == QLatin1String("symlink")) return Folder::Kind::SymbolicLink;
    if (name == QLatin1String("other")) return Folder::Kind::Other;
    return Folder::Kind::Missing;
}

Entry fromInfo(const Files::FileInfo &info, Folder::Kind kind)
{
    Entry entry;
    entry.kind = kind;
    entry.size = info.size;
    entry.modifiedNanoseconds = info.lastModified.nanosecondsSinceEpoch();
    entry.attributes = quint32(info.attributes);
    return entry;
}

bool sameInfo(const Files::FileInfo &before, const Files::FileInfo &after)
{
    return after.exists && before.size == after.size
        && before.lastModified == after.lastModified
        && before.attributes == after.attributes
        && before.isDirectory == after.isDirectory && before.isSymLink == after.isSymLink;
}

class Capture
{
public:
    Capture(const CaptureOptions &options, const std::atomic_bool *cancelled,
            const Progress &progress, const Files::FileSystem &fs)
        : options(options), cancelled(cancelled), progress(progress), fs(fs) {}

    DocumentResult run(const QString &root)
    {
        if (stopped(cancelled))
            return failure(QString(), true);
        if (root.isEmpty() || options.maximumDepth < 0 || options.maximumDepth > 256)
            return failure(QObject::tr("请选择有效的目录与递归深度（0–256）。"));
        document.sourceRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
        document.capturedAtUtc = QDateTime::currentDateTimeUtc();
        document.hashAlgorithm = options.includeHashes ? QStringLiteral("sha256") : QStringLiteral("none");
        if (!walk(document.sourceRoot, QString(), 0))
            return failure(error, stopped(cancelled));
        if (stopped(cancelled))
            return failure(QString(), true);
        document.complete = true;
        error = validate(document);
        if (!error.isEmpty())
            return failure(error);
        DocumentResult result;
        result.document = std::move(document);
        return result;
    }

private:
    DocumentResult failure(const QString &message, bool wasCancelled = false)
    {
        DocumentResult result;
        result.error = message;
        result.cancelled = wasCancelled;
        return result; // Do not expose a partially populated document as a baseline.
    }

    bool fail(const QString &message)
    {
        error = message;
        return false;
    }

    bool unchanged(const QString &path, const Files::FileInfo &before)
    {
        Files::ErrorCode code;
        const auto after = fs.stat(path, &code);
        return code.ok() && sameInfo(before, after);
    }

    bool hashFile(const QString &path, const Files::FileInfo &info, Entry &entry)
    {
        QFile file(fs.toNativePath(path));
        if (!file.open(QIODevice::ReadOnly))
            return fail(QObject::tr("无法计算摘要 %1：%2").arg(path, file.errorString()));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        quint64 bytes = 0;
        while (!file.atEnd()) {
            if (stopped(cancelled))
                return false;
            const auto chunk = file.read(256 * 1024);
            if (file.error() != QFileDevice::NoError)
                return fail(QObject::tr("读取摘要失败 %1：%2").arg(path, file.errorString()));
            bytes += quint64(chunk.size());
            hash.addData(chunk);
        }
        if (bytes != info.size || !unchanged(path, info))
            return fail(QObject::tr("生成快照期间文件发生变化，请重新生成：%1").arg(path));
        entry.sha256 = hash.result().toHex();
        return true;
    }

    bool walk(const QString &path, const QString &relative, int depth)
    {
        if (stopped(cancelled))
            return false;
        Files::ErrorCode code;
        const auto directory = fs.stat(path, &code);
        if (!code.ok())
            return fail(Files::errorReport(code, path));
        if (!directory.exists || !directory.isDirectory || directory.isSymLink)
            return fail(QObject::tr("快照源必须是普通目录，不能跟随目录链接：%1").arg(path));
        auto children = fs.enumerateDirectory(path, &code);
        if (!code.ok())
            return fail(Files::errorReport(code, path));
        std::sort(children.begin(), children.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
        for (const auto &listed : children) {
            if (stopped(cancelled))
                return false;
            if (document.entries.size() >= MaximumEntries)
                return fail(QObject::tr("快照条目数超过限制。"));
            const QString childRelative = relative.isEmpty() ? listed.name : relative + QLatin1Char('/') + listed.name;
            if (!safePath(childRelative) || listed.name.contains(QLatin1Char('/')))
                return fail(QObject::tr("快照不支持此相对路径：%1").arg(childRelative));
            const QString childPath = QDir(path).filePath(listed.name);
            const auto info = fs.stat(childPath, &code);
            if (!code.ok())
                return fail(Files::errorReport(code, childPath));
            if (!info.exists || !info.lastModified.isValid())
                return fail(QObject::tr("无法读取条目元数据：%1").arg(childPath));
            const auto kind = info.isSymLink ? Folder::Kind::SymbolicLink
                : info.isDirectory ? Folder::Kind::Directory
                : QFileInfo(fs.toNativePath(childPath)).isFile() ? Folder::Kind::File : Folder::Kind::Other;
            Entry entry = fromInfo(info, kind);
            if (kind == Folder::Kind::File && options.includeHashes && !hashFile(childPath, info, entry))
                return false;
            if (document.entries.contains(childRelative))
                return fail(QObject::tr("重复的快照路径：%1").arg(childRelative));
            document.entries.insert(childRelative, entry);
            if (progress)
                progress(document.entries.size(), childRelative);
            if (stopped(cancelled))
                return false;
            if (kind == Folder::Kind::Directory) {
                if (depth >= options.maximumDepth)
                    return fail(QObject::tr("达到递归深度上限，不能生成完整快照：%1").arg(childRelative));
                if (!walk(childPath, childRelative, depth + 1))
                    return false;
            }
        }
        if (!unchanged(path, directory))
            return fail(QObject::tr("生成快照期间目录发生变化，请重新生成：%1").arg(path));
        return true;
    }

    const CaptureOptions &options;
    const std::atomic_bool *cancelled;
    const Progress &progress;
    const Files::FileSystem &fs;
    Document document;
    QString error;
};

QJsonObject contentObject(const Document &document, const std::atomic_bool *cancelled)
{
    QJsonArray entries;
    for (auto it = document.entries.cbegin(); it != document.entries.cend(); ++it) {
        if (stopped(cancelled))
            return {};
        const auto &entry = it.value();
        QJsonObject object{{QStringLiteral("path"), it.key()},
                           {QStringLiteral("kind"), kindName(entry.kind)},
                           {QStringLiteral("size"), QString::number(entry.size)},
                           {QStringLiteral("modifiedNanoseconds"), QString::number(entry.modifiedNanoseconds)},
                           {QStringLiteral("attributes"), int(entry.attributes)},
                           {QStringLiteral("sha256"), QString::fromLatin1(entry.sha256)}};
        entries.append(object);
    }
    const QJsonObject payload{{QStringLiteral("sourceRoot"), document.sourceRoot},
                             {QStringLiteral("capturedAtUtc"), document.capturedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
                             {QStringLiteral("complete"), document.complete},
                             {QStringLiteral("hashAlgorithm"), document.hashAlgorithm},
                             {QStringLiteral("entries"), entries}};
    return {{QStringLiteral("format"), QStringLiteral("LqCompare.snapshot")},
            {QStringLiteral("version"), document.formatVersion},
            {QStringLiteral("payload"), payload}};
}

QByteArray digest(const QJsonObject &object)
{
    return QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256).toHex();
}

bool exactKeys(const QJsonObject &object, const QStringList &keys)
{
    if (object.size() != keys.size())
        return false;
    for (const auto &key : keys) {
        if (!object.contains(key))
            return false;
    }
    return true;
}

DocumentResult loadFailure(const QString &error)
{
    DocumentResult result;
    result.error = error;
    return result;
}

} // namespace

QString validate(const Document &document)
{
    if (document.formatVersion != CurrentFormatVersion)
        return QObject::tr("不支持的快照格式版本：%1（支持 %2）。")
            .arg(document.formatVersion).arg(CurrentFormatVersion);
    if (document.sourceRoot.isEmpty() || document.sourceRoot.contains(QChar::Null))
        return invalid(QStringLiteral("sourceRoot"));
    if (!document.capturedAtUtc.isValid() || document.capturedAtUtc.toUTC().toString(Qt::ISODateWithMs).isEmpty())
        return invalid(QStringLiteral("capturedAtUtc"));
    if (document.hashAlgorithm != QLatin1String("none") && document.hashAlgorithm != QLatin1String("sha256"))
        return invalid(QStringLiteral("hashAlgorithm"));
    if (document.entries.size() > MaximumEntries)
        return invalid(QStringLiteral("entries: too many"));
    for (auto it = document.entries.cbegin(); it != document.entries.cend(); ++it) {
        const auto &entry = it.value();
        if (!safePath(it.key()))
            return invalid(QStringLiteral("path: ") + it.key());
        if (kindName(entry.kind).isEmpty())
            return invalid(it.key() + QStringLiteral(".kind"));
        if (entry.attributes & ~KnownAttributes)
            return invalid(it.key() + QStringLiteral(".attributes"));
        if ((entry.kind == Folder::Kind::SymbolicLink) != bool(entry.attributes & quint32(Files::FileAttribute::SymLink)))
            return invalid(it.key() + QStringLiteral(".kind/attributes"));
        const bool requiresHash = document.hashAlgorithm == QLatin1String("sha256") && entry.kind == Folder::Kind::File;
        if (requiresHash ? !validHash(entry.sha256) : !entry.sha256.isEmpty())
            return invalid(it.key() + QStringLiteral(".sha256"));
        QString parent = it.key();
        while (parent.contains(QLatin1Char('/'))) {
            parent.truncate(parent.lastIndexOf(QLatin1Char('/')));
            const auto found = document.entries.constFind(parent);
            if (found == document.entries.cend()) {
                if (document.complete)
                    return invalid(it.key() + QStringLiteral(".missingParent"));
            } else if (found->kind != Folder::Kind::Directory) {
                return invalid(it.key() + QStringLiteral(".parentNotDirectory"));
            }
        }
    }
    return {};
}

DocumentResult capture(const QString &root, const CaptureOptions &options,
                       const std::atomic_bool *cancelled, const Progress &progress,
                       const Files::FileSystem *fileSystem)
{
    std::unique_ptr<Files::FileSystem> native;
    if (!fileSystem) {
        native.reset(Files::createNativeFileSystem());
        fileSystem = native.get();
    }
    return Capture(options, cancelled, progress, *fileSystem).run(root);
}

DocumentResult fromFolder(const Folder::Result &result, bool leftSide)
{
    if (!result.complete || result.cancelled || !result.error.isEmpty())
        return loadFailure(QObject::tr("目录扫描未完整完成，不能生成同步基线。"));
    if (!result.scanMaskDeclaration.trimmed().isEmpty() || result.excludedCount != 0)
        return loadFailure(QObject::tr("目录扫描使用了过滤范围，不能作为完整目录快照。"));
    Document document;
    document.sourceRoot = leftSide ? result.leftRoot : result.rightRoot;
    document.capturedAtUtc = QDateTime::currentDateTimeUtc();
    document.complete = true;
    for (const auto &item : result.entries) {
        const auto &side = leftSide ? item.left : item.right;
        if (item.excludedByMask)
            return loadFailure(QObject::tr("目录扫描包含被过滤条目，不能作为完整目录快照：%1").arg(item.relativePath));
        if (item.nameCaseDifference)
            return loadFailure(QObject::tr("两侧路径大小写不同，请直接采集目录以保留路径原名：%1").arg(item.relativePath));
        if (!item.left.error.isEmpty() || !item.right.error.isEmpty()
            || item.status == Folder::Status::Error || item.status == Folder::Status::Unknown)
            return loadFailure(QObject::tr("目录扫描存在错误或未比较条目：%1").arg(item.relativePath));
        // Folder does not recurse into a directory/file type conflict. Its
        // comparison can be complete while that directory's inventory is not.
        if (item.status == Folder::Status::TypeConflict && side.kind == Folder::Kind::Directory)
            return loadFailure(QObject::tr("类型冲突目录的子项未扫描，不能生成完整快照：%1").arg(item.relativePath));
        if (!side.exists())
            continue;
        if (!side.info.lastModified.isValid() || document.entries.contains(item.relativePath))
            return loadFailure(invalid(item.relativePath));
        document.entries.insert(item.relativePath, fromInfo(side.info, side.kind));
    }
    const QString error = validate(document);
    if (!error.isEmpty())
        return loadFailure(error);
    DocumentResult output;
    output.document = std::move(document);
    return output;
}

SaveResult save(const Document &document, const QString &path, const std::atomic_bool *cancelled)
{
    SaveResult result;
    result.cancelled = stopped(cancelled);
    if (result.cancelled)
        return result;
    result.error = validate(document);
    if (!result.error.isEmpty())
        return result;
    QJsonObject object = contentObject(document, cancelled);
    if (stopped(cancelled)) {
        result.cancelled = true;
        return result;
    }
    object.insert(QStringLiteral("integrity"), QJsonObject{
        {QStringLiteral("algorithm"), QStringLiteral("sha256")},
        {QStringLiteral("value"), QString::fromLatin1(digest(object))}});
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumDocumentBytes) {
        result.error = QObject::tr("快照文件超过 64 MiB 限制。");
        return result;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QObject::tr("无法写入快照 %1：%2").arg(path, file.errorString());
        return result;
    }
    for (qint64 offset = 0; offset < bytes.size();) {
        if (stopped(cancelled)) {
            file.cancelWriting();
            result.cancelled = true;
            return result;
        }
        const qint64 count = qMin(qint64(256 * 1024), qint64(bytes.size()) - offset);
        if (file.write(bytes.constData() + offset, count) != count) {
            result.error = QObject::tr("写入快照失败 %1：%2").arg(path, file.errorString());
            file.cancelWriting();
            return result;
        }
        offset += count;
    }
    if (stopped(cancelled)) {
        file.cancelWriting();
        result.cancelled = true;
        return result;
    }
    if (!file.commit())
        result.error = QObject::tr("原子保存快照失败 %1：%2").arg(path, file.errorString());
    return result;
}

DocumentResult load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return loadFailure(QObject::tr("无法打开快照 %1：%2").arg(path, file.errorString()));
    if (file.size() > MaximumDocumentBytes)
        return loadFailure(QObject::tr("快照文件超过 64 MiB 限制。"));
    const QByteArray bytes = file.read(MaximumDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return loadFailure(QObject::tr("读取快照失败：%1").arg(file.errorString()));
    if (bytes.size() > MaximumDocumentBytes)
        return loadFailure(QObject::tr("快照文件超过 64 MiB 限制。"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return loadFailure(QObject::tr("快照 JSON 损坏（偏移 %1）：%2").arg(parseError.offset).arg(parseError.errorString()));
    QJsonObject object = json.object();
    if (!exactKeys(object, {QStringLiteral("format"), QStringLiteral("version"), QStringLiteral("payload"), QStringLiteral("integrity")})
        || object.value(QStringLiteral("format")).toString() != QLatin1String("LqCompare.snapshot"))
        return loadFailure(invalid(QStringLiteral("format/version/payload/integrity")));
    const auto version = object.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != CurrentFormatVersion)
        return loadFailure(QObject::tr("不支持的快照格式版本（支持 %1）。").arg(CurrentFormatVersion));
    if (!object.value(QStringLiteral("integrity")).isObject())
        return loadFailure(invalid(QStringLiteral("integrity")));
    const auto integrity = object.take(QStringLiteral("integrity")).toObject();
    if (!exactKeys(integrity, {QStringLiteral("algorithm"), QStringLiteral("value")})
        || integrity.value(QStringLiteral("algorithm")).toString() != QLatin1String("sha256")
        || !validHash(integrity.value(QStringLiteral("value")).toString().toLatin1()))
        return loadFailure(invalid(QStringLiteral("integrity")));
    if (digest(object) != integrity.value(QStringLiteral("value")).toString().toLatin1())
        return loadFailure(QObject::tr("快照完整性校验失败，文件可能损坏或被修改。"));
    if (!object.value(QStringLiteral("payload")).isObject())
        return loadFailure(invalid(QStringLiteral("payload")));
    const auto payload = object.value(QStringLiteral("payload")).toObject();
    if (!exactKeys(payload, {QStringLiteral("sourceRoot"), QStringLiteral("capturedAtUtc"), QStringLiteral("complete"),
                            QStringLiteral("hashAlgorithm"), QStringLiteral("entries")}))
        return loadFailure(invalid(QStringLiteral("payload")));
    if (!payload.value(QStringLiteral("sourceRoot")).isString() || !payload.value(QStringLiteral("capturedAtUtc")).isString()
        || !payload.value(QStringLiteral("complete")).isBool() || !payload.value(QStringLiteral("hashAlgorithm")).isString()
        || !payload.value(QStringLiteral("entries")).isArray())
        return loadFailure(invalid(QStringLiteral("payload types")));
    Document document;
    document.sourceRoot = payload.value(QStringLiteral("sourceRoot")).toString();
    const QString capturedAt = payload.value(QStringLiteral("capturedAtUtc")).toString();
    document.capturedAtUtc = QDateTime::fromString(capturedAt, Qt::ISODateWithMs);
    if (!capturedAt.endsWith(QLatin1Char('Z')) || document.capturedAtUtc.toUTC().toString(Qt::ISODateWithMs) != capturedAt)
        return loadFailure(invalid(QStringLiteral("capturedAtUtc")));
    document.complete = payload.value(QStringLiteral("complete")).toBool();
    document.hashAlgorithm = payload.value(QStringLiteral("hashAlgorithm")).toString();
    const auto entries = payload.value(QStringLiteral("entries")).toArray();
    if (entries.size() > MaximumEntries)
        return loadFailure(invalid(QStringLiteral("entries: too many")));
    for (const auto &value : entries) {
        if (!value.isObject())
            return loadFailure(invalid(QStringLiteral("entry")));
        const auto entryObject = value.toObject();
        if (!exactKeys(entryObject, {QStringLiteral("path"), QStringLiteral("kind"), QStringLiteral("size"),
                                    QStringLiteral("modifiedNanoseconds"), QStringLiteral("attributes"), QStringLiteral("sha256")}))
            return loadFailure(invalid(QStringLiteral("entry fields")));
        for (const auto &key : {QStringLiteral("path"), QStringLiteral("kind"), QStringLiteral("size"),
                               QStringLiteral("modifiedNanoseconds"), QStringLiteral("sha256")}) {
            if (!entryObject.value(key).isString())
                return loadFailure(invalid(key));
        }
        const QString relative = entryObject.value(QStringLiteral("path")).toString();
        if (document.entries.contains(relative))
            return loadFailure(QObject::tr("快照包含重复路径：%1").arg(relative));
        Entry entry;
        entry.kind = kindFromName(entryObject.value(QStringLiteral("kind")).toString());
        bool sizeOk = false, timeOk = false;
        const auto size = entryObject.value(QStringLiteral("size")).toString();
        const auto time = entryObject.value(QStringLiteral("modifiedNanoseconds")).toString();
        entry.size = size.toULongLong(&sizeOk);
        entry.modifiedNanoseconds = time.toLongLong(&timeOk);
        if (!sizeOk || QString::number(entry.size) != size || !timeOk || QString::number(entry.modifiedNanoseconds) != time)
            return loadFailure(invalid(relative + QStringLiteral(".size/modifiedNanoseconds")));
        const auto attributes = entryObject.value(QStringLiteral("attributes"));
        if (!attributes.isDouble() || attributes.toDouble() < 0 || attributes.toDouble() > KnownAttributes
            || attributes.toDouble() != attributes.toInt())
            return loadFailure(invalid(relative + QStringLiteral(".attributes")));
        entry.attributes = quint32(attributes.toInt());
        const auto hash = entryObject.value(QStringLiteral("sha256")).toString();
        entry.sha256 = hash.toLatin1();
        if (QString::fromLatin1(entry.sha256) != hash)
            return loadFailure(invalid(relative + QStringLiteral(".sha256")));
        document.entries.insert(relative, entry);
    }
    const QString error = validate(document);
    if (!error.isEmpty())
        return loadFailure(error);
    DocumentResult result;
    result.document = std::move(document);
    return result;
}

Difference compare(const Document &before, const Document &after)
{
    Difference result;
    result.error = validate(before);
    if (result.error.isEmpty())
        result.error = validate(after);
    if (!result.error.isEmpty())
        return result;
    QSet<QString> paths;
    for (auto it = before.entries.cbegin(); it != before.entries.cend(); ++it) paths.insert(it.key());
    for (auto it = after.entries.cbegin(); it != after.entries.cend(); ++it) paths.insert(it.key());
    auto ordered = paths.values();
    std::sort(ordered.begin(), ordered.end());
    for (const auto &path : ordered) {
        Change change;
        change.relativePath = path;
        const auto a = before.entries.constFind(path), b = after.entries.constFind(path);
        if (a == before.entries.cend() || b == after.entries.cend()) {
            const bool added = a == before.entries.cend();
            if (!(added ? before.complete : after.complete)) {
                change.presenceUnknown = true;
                ++result.presenceUnknown;
            } else if (added) {
                change.flags |= ChangeFlag::Added;
                ++result.added;
            } else {
                change.flags |= ChangeFlag::Removed;
                ++result.removed;
            }
        } else {
            if (a->kind != b->kind) change.flags |= ChangeFlag::Type;
            if (a->size != b->size) change.flags |= ChangeFlag::Size;
            if (a->modifiedNanoseconds != b->modifiedNanoseconds) change.flags |= ChangeFlag::Time;
            if (a->attributes != b->attributes) change.flags |= ChangeFlag::Attributes;
            if (a->kind == Folder::Kind::File && b->kind == Folder::Kind::File) {
                if (a->size != b->size || (!a->sha256.isEmpty() && !b->sha256.isEmpty() && a->sha256 != b->sha256))
                    change.content = ContentState::Different;
                else if (!a->sha256.isEmpty() && !b->sha256.isEmpty())
                    change.content = ContentState::Same;
                else
                    change.content = ContentState::Unknown;
            } else if (a->kind == b->kind && a->kind != Folder::Kind::Directory) {
                change.content = ContentState::Unknown; // No link target or special-file bytes retained.
            }
            if (change.content == ContentState::Different) change.flags |= ChangeFlag::Content;
            if (change.flags != ChangeFlags()) ++result.modified;
            if (change.content == ContentState::Unknown) ++result.contentUnknown;
        }
        if (change.flags != ChangeFlags() || change.content == ContentState::Unknown || change.presenceUnknown)
            result.entries.append(change);
    }
    return result;
}

QString Difference::summary() const
{
    if (!error.isEmpty())
        return error;
    QString text = QObject::tr("新增 %1 个条目、删除 %2 个、修改 %3 个。")
                       .arg(added).arg(removed).arg(modified);
    if (contentUnknown)
        text += QObject::tr("%1 个条目的内容未知。").arg(contentUnknown);
    if (presenceUnknown)
        text += QObject::tr("范围不完整，%1 个条目的新增/删除状态未知。").arg(presenceUnknown);
    return text;
}

QString readOnlyNotice()
{
    return QObject::tr("快照（只读）仅包含元数据和可选摘要，不包含文件内容，不能从快照恢复或复制文件。");
}

bool allows(const Document &document, Operation operation, QString *reason)
{
    if (reason) reason->clear();
    const auto error = validate(document);
    if (!error.isEmpty()) {
        if (reason) *reason = error;
        return false;
    }
    if (operation == Operation::CompareMetadata)
        return true;
    if (operation == Operation::CompareHashes && document.hashAlgorithm == QLatin1String("sha256"))
        return true;
    if (reason) {
        *reason = operation == Operation::CompareHashes
            ? QObject::tr("此快照没有内容摘要，只能比较元数据；文件内容未知。") : readOnlyNotice();
    }
    return false;
}

} // namespace Snapshot
} // namespace LqCompare
