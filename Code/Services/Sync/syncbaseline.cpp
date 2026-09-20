#include "syncbaseline.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

namespace LqCompare { namespace Sync {
namespace {

constexpr int BaselineVersion = 1;
constexpr qint64 MaximumBytes = 64 * 1024 * 1024;

bool stopped(const std::atomic_bool *cancelled) {
    return cancelled && cancelled->load(std::memory_order_relaxed);
}
BaselineResult failure(const QString &error, bool cancelled = false) {
    BaselineResult result; result.error = error; result.cancelled = cancelled; return result;
}
bool hashValid(const QByteArray &hash) {
    if (hash.size() != 64) return false;
    for (const char c : hash)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
bool plainText(const QString &text) {
    for (const QChar c : text)
        if (c.unicode() < 32 || c.unicode() == 127) return false;
    return true;
}
bool safeRelative(const QString &path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || !plainText(path)
        || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':'))) return false;
    for (const auto &part : path.split(QLatin1Char('/')))
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) return false;
    return true;
}
bool rootValid(const QString &root) {
    return !root.isEmpty() && QDir::isAbsolutePath(root) && plainText(root)
        && !root.contains(QLatin1Char('\\')) && QDir::cleanPath(root) == root;
}
bool beneath(const QString &child, const QString &parent) {
    const QString a = child.normalized(QString::NormalizationForm_C);
    const QString b = parent.normalized(QString::NormalizationForm_C);
    const QString prefix = b.endsWith(QLatin1Char('/')) ? b : b + QLatin1Char('/');
    return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(prefix, Qt::CaseInsensitive);
}
bool optionsValid(const Options &options) {
    if ((options.mode != Mode::Update && options.mode != Mode::Mirror && options.mode != Mode::TwoWay)
        || (options.direction != Direction::LeftToRight && options.direction != Direction::RightToLeft)
        || (options.deletion != Deletion::Keep && options.deletion != Deletion::Trash)
        || options.deleteCountThreshold < 0
        || (options.mode == Mode::Mirror && options.deletion == Deletion::Keep)) return false;
    for (const auto &path : options.excludedPaths) if (!safeRelative(path)) return false;
    return true;
}
QByteArray digest(const QJsonObject &object) {
    return QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256).toHex();
}
bool exactKeys(const QJsonObject &object, const QStringList &keys) {
    if (object.size() != keys.size()) return false;
    for (const auto &key : keys) if (!object.contains(key)) return false;
    return true;
}
QString fieldError(const QString &field) {
    return QStringLiteral("同步基线字段无效或缺失：%1").arg(field);
}
QJsonObject toObject(const Baseline &baseline, const std::atomic_bool *cancelled) {
    QJsonArray entries;
    for (auto it = baseline.entries.cbegin(); it != baseline.entries.cend(); ++it) {
        if (stopped(cancelled)) return {};
        const auto &f = it.value();
        entries.append(QJsonObject{
            {QStringLiteral("path"), it.key()},
            {QStringLiteral("kind"), f.kind == Folder::Kind::File ? QStringLiteral("file") : QStringLiteral("directory")},
            {QStringLiteral("size"), QString::number(f.size)},
            {QStringLiteral("modifiedNs"), QString::number(f.modifiedNs)},
            {QStringLiteral("createdNs"), QString::number(f.createdNs)},
            {QStringLiteral("attributes"), f.attributes},
            {QStringLiteral("sha256"), QString::fromLatin1(f.sha256)}});
    }
    const QJsonObject payload{{QStringLiteral("leftRoot"), baseline.leftRoot},
                             {QStringLiteral("rightRoot"), baseline.rightRoot},
                             {QStringLiteral("scopeKey"), QString::fromLatin1(baseline.scopeKey)},
                             {QStringLiteral("complete"), baseline.complete},
                             {QStringLiteral("entries"), entries}};
    return {{QStringLiteral("format"), QStringLiteral("LqCompare.sync-baseline")},
            {QStringLiteral("version"), BaselineVersion}, {QStringLiteral("payload"), payload}};
}

} // namespace

QString validateBaseline(const Baseline &baseline) {
    if (!baseline.complete) return QStringLiteral("同步基线范围不完整，必须降级为无基线模式。");
    if (!rootValid(baseline.leftRoot) || !rootValid(baseline.rightRoot))
        return QStringLiteral("同步基线必须绑定两个规范化的本地绝对目录路径。");
    if (beneath(baseline.leftRoot, baseline.rightRoot) || beneath(baseline.rightRoot, baseline.leftRoot))
        return QStringLiteral("同步基线不能绑定相同或互为父子的目录。");
    if (!hashValid(baseline.scopeKey)) return fieldError(QStringLiteral("scopeKey"));
    Snapshot::Document document;
    document.sourceRoot = baseline.leftRoot;
    document.capturedAtUtc = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
    document.hashAlgorithm = QStringLiteral("sha256");
    document.complete = true;
    QSet<QString> normalizedPaths;
    for (auto it = baseline.entries.cbegin(); it != baseline.entries.cend(); ++it) {
        const auto &f = it.value();
        if ((f.kind != Folder::Kind::File && f.kind != Folder::Kind::Directory) || !f.error.isEmpty())
            return QStringLiteral("同步基线不支持链接、特殊、缺失或读取错误条目：%1").arg(it.key());
        const QString normalized = it.key().normalized(QString::NormalizationForm_C).toCaseFolded();
        if (normalizedPaths.contains(normalized))
            return QStringLiteral("同步基线路径大小写或 Unicode 规范形式冲突：%1").arg(it.key());
        normalizedPaths.insert(normalized);
        Snapshot::Entry entry;
        entry.kind = f.kind; entry.size = f.size; entry.modifiedNanoseconds = f.modifiedNs;
        entry.attributes = quint32(f.attributes); entry.sha256 = f.sha256;
        document.entries.insert(it.key(), entry);
    }
    return Snapshot::validate(document);
}

BaselineResult fromSnapshots(const Snapshot::Document &left, const Snapshot::Document &right,
                             const Options &options, const std::atomic_bool *cancelled) {
    if (stopped(cancelled)) return failure({}, true);
    QString error = Snapshot::validate(left);
    if (error.isEmpty()) error = Snapshot::validate(right);
    if (!error.isEmpty()) return failure(error);
    if (!left.complete || !right.complete || left.hashAlgorithm != QLatin1String("sha256")
        || right.hashAlgorithm != QLatin1String("sha256"))
        return failure(QStringLiteral("建立共同基线需要两份完整、含 SHA-256 的目录快照。"));
    if (!optionsValid(options)) return failure(QStringLiteral("同步选项或排除路径无效，不能建立基线。"));
    if (left.entries.size() != right.entries.size())
        return failure(QStringLiteral("两份快照的路径集合不同，不能建立共同基线。"));
    Baseline baseline;
    baseline.leftRoot = left.sourceRoot; baseline.rightRoot = right.sourceRoot;
    baseline.scopeKey = scopeKey(options); baseline.complete = true;
    for (auto it = left.entries.cbegin(); it != left.entries.cend(); ++it) {
        if (stopped(cancelled)) return failure({}, true);
        const auto other = right.entries.constFind(it.key());
        if (other == right.entries.cend() || it->kind != other->kind)
            return failure(QStringLiteral("快照路径或类型不同，不能建立共同基线：%1").arg(it.key()));
        if (it->kind != Folder::Kind::File && it->kind != Folder::Kind::Directory)
            return failure(QStringLiteral("不能用链接或特殊文件推断共同内容：%1").arg(it.key()));
        if (it->kind == Folder::Kind::File && (it->size != other->size || it->sha256 != other->sha256))
            return failure(QStringLiteral("两侧快照内容不同，不能建立共同基线：%1").arg(it.key()));
        Fingerprint f;
        f.kind = it->kind; f.size = it->size; f.modifiedNs = it->modifiedNanoseconds;
        f.attributes = int(it->attributes); f.sha256 = it->sha256;
        baseline.entries.insert(it.key(), f);
    }
    error = validateBaseline(baseline);
    if (!error.isEmpty()) return failure(error);
    if (stopped(cancelled)) return failure({}, true);
    BaselineResult result; result.baseline = std::move(baseline); return result;
}

BaselineSaveResult saveBaseline(const Baseline &baseline, const QString &path, const std::atomic_bool *cancelled) {
    BaselineSaveResult result;
    result.cancelled = stopped(cancelled);
    if (result.cancelled) return result;
    result.error = validateBaseline(baseline);
    if (!result.error.isEmpty()) return result;
    auto object = toObject(baseline, cancelled);
    if (stopped(cancelled)) { result.cancelled = true; return result; }
    object.insert(QStringLiteral("integrity"), QJsonObject{
        {QStringLiteral("algorithm"), QStringLiteral("sha256")},
        {QStringLiteral("value"), QString::fromLatin1(digest(object))}});
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumBytes) { result.error = QStringLiteral("同步基线超过 64 MiB 限制。"); return result; }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { result.error = file.errorString(); return result; }
    for (qint64 offset = 0; offset < bytes.size();) {
        if (stopped(cancelled)) { file.cancelWriting(); result.cancelled = true; return result; }
        const qint64 count = qMin(qint64(256 * 1024), qint64(bytes.size()) - offset);
        if (file.write(bytes.constData() + offset, count) != count) {
            result.error = file.errorString(); file.cancelWriting(); return result;
        }
        offset += count;
    }
    if (stopped(cancelled)) { file.cancelWriting(); result.cancelled = true; return result; }
    if (!file.commit()) result.error = file.errorString();
    return result;
}

BaselineResult loadBaseline(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(QStringLiteral("无法读取同步基线：%1").arg(file.errorString()));
    if (file.size() > MaximumBytes) return failure(QStringLiteral("同步基线超过 64 MiB 限制。"));
    const auto bytes = file.read(MaximumBytes + 1);
    if (file.error() != QFileDevice::NoError) return failure(file.errorString());
    if (bytes.size() > MaximumBytes) return failure(QStringLiteral("同步基线超过 64 MiB 限制。"));
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return failure(QStringLiteral("同步基线 JSON 损坏（偏移 %1）：%2").arg(parseError.offset).arg(parseError.errorString()));
    auto object = json.object();
    if (!exactKeys(object, {QStringLiteral("format"), QStringLiteral("version"), QStringLiteral("payload"), QStringLiteral("integrity")})
        || object.value(QStringLiteral("format")).toString() != QLatin1String("LqCompare.sync-baseline"))
        return failure(fieldError(QStringLiteral("format/version/payload/integrity")));
    if (!object.value(QStringLiteral("version")).isDouble() || object.value(QStringLiteral("version")).toDouble() != BaselineVersion)
        return failure(QStringLiteral("不支持的同步基线格式版本（支持 1）。"));
    if (!object.value(QStringLiteral("integrity")).isObject()) return failure(fieldError(QStringLiteral("integrity")));
    const auto integrity = object.take(QStringLiteral("integrity")).toObject();
    if (!exactKeys(integrity, {QStringLiteral("algorithm"), QStringLiteral("value")})
        || integrity.value(QStringLiteral("algorithm")).toString() != QLatin1String("sha256")
        || !hashValid(integrity.value(QStringLiteral("value")).toString().toLatin1()))
        return failure(fieldError(QStringLiteral("integrity")));
    if (digest(object) != integrity.value(QStringLiteral("value")).toString().toLatin1())
        return failure(QStringLiteral("同步基线完整性校验失败，必须降级为无基线模式。"));
    if (!object.value(QStringLiteral("payload")).isObject()) return failure(fieldError(QStringLiteral("payload")));
    const auto payload = object.value(QStringLiteral("payload")).toObject();
    if (!exactKeys(payload, {QStringLiteral("leftRoot"), QStringLiteral("rightRoot"), QStringLiteral("scopeKey"),
                            QStringLiteral("complete"), QStringLiteral("entries")}))
        return failure(fieldError(QStringLiteral("payload")));
    for (const auto &key : {QStringLiteral("leftRoot"), QStringLiteral("rightRoot"), QStringLiteral("scopeKey")})
        if (!payload.value(key).isString()) return failure(fieldError(key));
    if (!payload.value(QStringLiteral("complete")).isBool() || !payload.value(QStringLiteral("entries")).isArray())
        return failure(fieldError(QStringLiteral("complete/entries")));
    Baseline baseline;
    baseline.leftRoot = payload.value(QStringLiteral("leftRoot")).toString();
    baseline.rightRoot = payload.value(QStringLiteral("rightRoot")).toString();
    const QString key = payload.value(QStringLiteral("scopeKey")).toString();
    baseline.scopeKey = key.toLatin1();
    if (QString::fromLatin1(baseline.scopeKey) != key) return failure(fieldError(QStringLiteral("scopeKey")));
    baseline.complete = payload.value(QStringLiteral("complete")).toBool();
    const auto entries = payload.value(QStringLiteral("entries")).toArray();
    if (entries.size() > 1000000) return failure(fieldError(QStringLiteral("entries: too many")));
    for (const auto &value : entries) {
        if (!value.isObject()) return failure(fieldError(QStringLiteral("entry")));
        const auto e = value.toObject();
        if (!exactKeys(e, {QStringLiteral("path"), QStringLiteral("kind"), QStringLiteral("size"),
                          QStringLiteral("modifiedNs"), QStringLiteral("createdNs"), QStringLiteral("attributes"), QStringLiteral("sha256")}))
            return failure(fieldError(QStringLiteral("entry fields")));
        for (const auto &field : {QStringLiteral("path"), QStringLiteral("kind"), QStringLiteral("size"),
                                 QStringLiteral("modifiedNs"), QStringLiteral("createdNs"), QStringLiteral("sha256")})
            if (!e.value(field).isString()) return failure(fieldError(field));
        const QString relative = e.value(QStringLiteral("path")).toString();
        if (baseline.entries.contains(relative)) return failure(QStringLiteral("同步基线存在重复路径：%1").arg(relative));
        Fingerprint f;
        const QString kind = e.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("file")) f.kind = Folder::Kind::File;
        else if (kind == QLatin1String("directory")) f.kind = Folder::Kind::Directory;
        else return failure(fieldError(relative + QStringLiteral(".kind")));
        bool sizeOk = false, modifiedOk = false, createdOk = false;
        const QString size = e.value(QStringLiteral("size")).toString();
        const QString modified = e.value(QStringLiteral("modifiedNs")).toString();
        const QString created = e.value(QStringLiteral("createdNs")).toString();
        f.size = size.toULongLong(&sizeOk); f.modifiedNs = modified.toLongLong(&modifiedOk); f.createdNs = created.toLongLong(&createdOk);
        if (!sizeOk || !modifiedOk || !createdOk || QString::number(f.size) != size
            || QString::number(f.modifiedNs) != modified || QString::number(f.createdNs) != created)
            return failure(fieldError(relative + QStringLiteral(".size/modifiedNs/createdNs")));
        const auto attributes = e.value(QStringLiteral("attributes"));
        if (!attributes.isDouble() || attributes.toDouble() < 0 || attributes.toDouble() > 0x3f
            || attributes.toDouble() != attributes.toInt())
            return failure(fieldError(relative + QStringLiteral(".attributes")));
        f.attributes = attributes.toInt();
        const QString hash = e.value(QStringLiteral("sha256")).toString();
        f.sha256 = hash.toLatin1();
        if (QString::fromLatin1(f.sha256) != hash) return failure(fieldError(relative + QStringLiteral(".sha256")));
        baseline.entries.insert(relative, f);
    }
    const auto error = validateBaseline(baseline);
    if (!error.isEmpty()) return failure(error);
    BaselineResult result; result.baseline = std::move(baseline); return result;
}

} }
