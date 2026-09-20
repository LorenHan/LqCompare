#include "sessiondocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QRegularExpression>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace LqCompare {
namespace {
constexpr qint64 MaximumSessionBytes = 8 * 1024 * 1024;
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QString sourcePath(const QString &value, const QString &base) {
    if (value.isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(value).isAbsolute() ? value : QDir(base).absoluteFilePath(value));
}

// canonicalFilePath() is empty for a future merge output. Resolve its existing
// parent as well, otherwise a directory symlink can hide a source alias.
QString resolvedPath(const QString &path, int depth = 0)
{
    if (depth > 64) return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return QDir::cleanPath(canonical);
    if (info.isSymLink()) return resolvedPath(info.symLinkTarget(), depth + 1);
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    const QString parent = QFileInfo(absolute).absolutePath();
    if (parent == absolute) return absolute;
    const QString resolvedParent = resolvedPath(parent, depth + 1);
    return resolvedParent.isEmpty() ? QString() : QDir(resolvedParent).filePath(QFileInfo(absolute).fileName());
}

bool sameFileObject(const QString &left, const QString &right)
{
#ifdef Q_OS_UNIX
    struct stat a, b;
    return ::stat(QFile::encodeName(left).constData(), &a) == 0
        && ::stat(QFile::encodeName(right).constData(), &b) == 0
        && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#else
    Q_UNUSED(left); Q_UNUSED(right);
    return false;
#endif
}

bool protectSources(const QString &path, const QStringList &sources, QString *error)
{
    if (path.isEmpty() || path.contains(QChar::Null))
        return fail(error, QStringLiteral("Session output path is empty or invalid."));
    const QString target = resolvedPath(path);
    if (target.isEmpty()) return fail(error, QStringLiteral("Cannot resolve session output path: %1").arg(path));
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    for (const QString &source : sources) {
        if (source.isEmpty()) continue;
        const QString resolvedSource = resolvedPath(source);
        if (resolvedSource.isEmpty())
            return fail(error, QStringLiteral("Cannot resolve session source path: %1").arg(source));
        if (target.compare(resolvedSource, sensitivity) == 0 || sameFileObject(path, source))
            return fail(error, QStringLiteral("A session definition cannot overwrite a source or merge output: %1").arg(source));
    }
    // A definition must replace its own regular file, never follow an unrelated
    // link whose referent can change between validation and QSaveFile::commit().
    if (QFileInfo(path).isSymLink())
        return fail(error, QStringLiteral("A session definition cannot be saved through a symbolic link."));
    return true;
}
}

bool SessionDocument::fromJson(const QJsonObject &json, const QString &base,
                               SessionDocument *document, QString *error)
{
    if (!document) return fail(error, QStringLiteral("Missing session output."));
    const auto version = json.value(QStringLiteral("version"));
    if (!version.isUndefined() && (!version.isDouble() || version.toDouble() != version.toInt()
                                  || version.toInt() < 0 || version.toInt() > 1))
        return fail(error, QStringLiteral("Unsupported session field: version (supported: 0, 1)."));
    for (const QString &key : {QStringLiteral("type"), QStringLiteral("title"),
                               QStringLiteral("notes")}) {
        if (json.contains(key) && !json.value(key).isString())
            return fail(error, QStringLiteral("Session field %1 must be text.").arg(key));
    }
    SessionDocument result;
    result.typeId = json.value(QStringLiteral("type")).toString(QStringLiteral("text"));
    if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9-]*$")).match(result.typeId).hasMatch())
        return fail(error, QStringLiteral("Invalid session field: type."));
    if (json.contains(QStringLiteral("sources")) && !json.value(QStringLiteral("sources")).isObject())
        return fail(error, QStringLiteral("Session field sources must be an object."));
    const auto sources = json.value(QStringLiteral("sources")).toObject();
    for (const QString &key : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("base"), QStringLiteral("output")}) {
        if (sources.contains(key) && !sources.value(key).isString())
            return fail(error, QStringLiteral("Session field sources.%1 must be text.").arg(key));
        if (sources.value(key).toString().contains(QChar::Null))
            return fail(error, QStringLiteral("Session field sources.%1 contains a null character.").arg(key));
    }
    if (json.contains(QStringLiteral("settings")) && !json.value(QStringLiteral("settings")).isObject())
        return fail(error, QStringLiteral("Session field settings must be an object."));
    result.leftPath = sourcePath(sources.value(QStringLiteral("left")).toString(), base);
    result.rightPath = sourcePath(sources.value(QStringLiteral("right")).toString(), base);
    result.basePath = sourcePath(sources.value(QStringLiteral("base")).toString(), base);
    result.outputPath = sourcePath(sources.value(QStringLiteral("output")).toString(), base);
    result.title = json.value(QStringLiteral("title")).toString();
    result.notes = json.value(QStringLiteral("notes")).toString();
    result.settings = json.value(QStringLiteral("settings")).toObject().toVariantMap();
    result.original = json;
    *document = result;
    if (error) error->clear();
    return true;
}

QJsonObject SessionDocument::toJson(const QString &base) const
{
    QJsonObject result = original;
    result.insert(QStringLiteral("version"), 1);
    result.insert(QStringLiteral("type"), typeId);
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("notes"), notes);
    result.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings));
    auto sources = result.value(QStringLiteral("sources")).toObject();
    const auto portable = [&base](const QString &path) {
        return base.isEmpty() || path.isEmpty() ? path : QDir(base).relativeFilePath(path);
    };
    sources.insert(QStringLiteral("left"), portable(leftPath));
    sources.insert(QStringLiteral("right"), portable(rightPath));
    sources.insert(QStringLiteral("base"), portable(basePath));
    sources.insert(QStringLiteral("output"), portable(outputPath));
    result.insert(QStringLiteral("sources"), sources);
    return result;
}

bool SessionDocument::load(const QString &path, SessionDocument *document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, path + QStringLiteral(": ") + file.errorString());
    if (file.size() > MaximumSessionBytes) return fail(error, QStringLiteral("Session file exceeds 8 MiB."));
    const QByteArray bytes = file.read(MaximumSessionBytes + 1);
    if (file.error() != QFile::NoError) return fail(error, file.errorString());
    if (bytes.size() > MaximumSessionBytes) return fail(error, QStringLiteral("Session file exceeds 8 MiB."));
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError) {
        const int line = bytes.left(parse.offset).count('\n') + 1;
        return fail(error, QStringLiteral("%1, line %2: %3").arg(path).arg(line).arg(parse.errorString()));
    }
    if (!json.isObject()) return fail(error, QStringLiteral("Session root must be a JSON object."));
    return fromJson(json.object(), QFileInfo(path).absolutePath(), document, error);
}

bool SessionDocument::save(const QString &path, QString *error) const
{
    if (path.isEmpty() || path.contains(QChar::Null))
        return fail(error, QStringLiteral("Session output path is empty or invalid."));
    const auto json = toJson(QFileInfo(path).absolutePath());
    SessionDocument validated;
    if (!fromJson(json, QFileInfo(path).absolutePath(), &validated, error)) return false;
    if (!protectSources(path, {leftPath, rightPath, basePath, outputPath}, error)) return false;
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumSessionBytes) return fail(error, QStringLiteral("Session file exceeds 8 MiB."));
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    if (file.write(bytes) != bytes.size()) return fail(error, file.errorString());
    if (!file.commit()) return fail(error, file.errorString());
    if (error) error->clear();
    return true;
}
}
