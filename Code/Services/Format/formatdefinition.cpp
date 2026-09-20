#include "formatdefinition.h"
#include "mask.h"
#include "sessiontype.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace LqCompare {
namespace Format {
namespace {
FormatDefinition definition(const char *id, const QString &name, const char *view,
                            const QStringList &masks,
                            const QVector<MagicSignature> &signatures = {})
{
    FormatDefinition value;
    value.id = QString::fromLatin1(id);
    value.name = name;
    value.sessionTypeId = QString::fromLatin1(view);
    value.masks = masks;
    value.signatures = signatures;
    value.builtIn = true;
    return value;
}

MagicSignature magic(const char *hex, int offset = 0)
{
    return {offset, QByteArray::fromHex(hex)};
}

QJsonObject serialize(const FormatDefinition &d)
{
    QJsonArray masks;
    for (const auto &mask : d.masks)
        masks.append(mask);
    QJsonArray signatures;
    for (const auto &signature : d.signatures)
        signatures.append(QJsonObject{{QStringLiteral("offset"), signature.offset},
                                      {QStringLiteral("hex"), QString::fromLatin1(signature.bytes.toHex())}});
    return {{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name},
            {QStringLiteral("sessionTypeId"), d.sessionTypeId},
            {QStringLiteral("masks"), masks}, {QStringLiteral("signatures"), signatures},
            {QStringLiteral("settings"), d.settings}};
}

const FormatDefinition *findDefinition(const QVector<FormatDefinition> &definitions, const QString &id)
{
    for (const auto &definition : definitions)
        if (definition.id == id)
            return &definition;
    return nullptr;
}
} // namespace

QVector<FormatDefinition> builtInFormatDefinitions()
{
    // No catch-all mask: otherwise unknown content would never reach detection.
    // Masks are the sole extension authority; no extension branching in the opener.
    return {
        definition("patch", QStringLiteral("补丁"), "text-patch", {"*.patch", "*.diff"}),
        definition("csv", QStringLiteral("CSV 表格"), "table", {"*.csv"}),
        definition("tsv", QStringLiteral("TSV 表格"), "table", {"*.tsv", "*.tab"}),
        definition("office-table", QStringLiteral("电子表格"), "table", {"*.xls", "*.xlsx", "*.xlsm", "*.ods"}),
        definition("png", QStringLiteral("PNG 图片"), "picture", {"*.png"}, {magic("89504e470d0a1a0a")}),
        definition("jpeg", QStringLiteral("JPEG 图片"), "picture", {"*.jpg", "*.jpeg"}, {magic("ffd8ff")}),
        definition("gif", QStringLiteral("GIF 图片"), "picture", {"*.gif"}, {magic("474946383761"), magic("474946383961")}),
        definition("bmp", QStringLiteral("BMP 图片"), "picture", {"*.bmp"}, {magic("424d")}),
        definition("webp", QStringLiteral("WebP 图片"), "picture", {"*.webp"}, {magic("57454250", 8)}),
        definition("tiff", QStringLiteral("TIFF 图片"), "picture", {"*.tif", "*.tiff"}, {magic("49492a00"), magic("4d4d002a")}),
        definition("icon", QStringLiteral("图标"), "picture", {"*.ico"}, {magic("00000100")}),
        definition("zip", QStringLiteral("ZIP 归档"), "archive", {"*.zip", "*.jar", "*.bcpkg"},
                   {magic("504b0304"), magic("504b0506"), magic("504b0708")}),
        definition("gzip", QStringLiteral("GZip 归档"), "archive", {"*.gz", "*.tgz"}, {magic("1f8b")}),
        definition("7zip", QStringLiteral("7-Zip 归档"), "archive", {"*.7z"}, {magic("377abcaf271c")}),
        definition("rar", QStringLiteral("RAR 归档"), "archive", {"*.rar"}, {magic("526172211a0700"), magic("526172211a070100")}),
        definition("bzip2", QStringLiteral("BZip2 归档"), "archive", {"*.bz2", "*.tbz2"}, {magic("425a68")}),
        definition("tar", QStringLiteral("TAR 归档"), "archive", {"*.tar"}, {magic("7573746172", 257)}),
        definition("other-archive", QStringLiteral("其它归档"), "archive", {"*.cab", "*.chm", "*.deb", "*.rpm", "*.xz"}),
        definition("flac", QStringLiteral("FLAC 媒体"), "media", {"*.flac"}, {magic("664c6143")}),
        definition("ogg", QStringLiteral("Ogg 媒体"), "media", {"*.ogg"}, {magic("4f676753")}),
        definition("mp3", QStringLiteral("MP3 媒体"), "media", {"*.mp3"}, {magic("494433")}),
        definition("mp4", QStringLiteral("MP4 媒体"), "media", {"*.mp4", "*.m4a"}, {magic("66747970", 4)}),
        definition("other-media", QStringLiteral("其它媒体"), "media", {"*.aac", "*.wav", "*.wma"}),
        definition("registry", QStringLiteral("注册表导出"), "registry", {"*.reg"}),
        definition("version", QStringLiteral("可执行文件版本"), "version", {"*.exe", "*.dll", "*.ocx"}, {magic("4d5a")}),
        definition("cpp", QStringLiteral("C / C++"), "text", {"*.c", "*.cc", "*.cpp", "*.cxx"}),
        definition("headers", QStringLiteral("C / C++ 头文件"), "text", {"*.h", "*.hh", "*.hpp", "*.hxx"}),
        definition("python", QStringLiteral("Python"), "text", {"*.py", "*.pyw"}),
        definition("javascript", QStringLiteral("JavaScript / TypeScript"), "text", {"*.js", "*.jsx", "*.mjs", "*.ts", "*.tsx"}),
        definition("json", QStringLiteral("JSON"), "text", {"*.json", "*.jsonc"}),
        definition("xml", QStringLiteral("XML"), "text", {"*.xml", "*.xsl", "*.svg"}),
        definition("html", QStringLiteral("HTML"), "text", {"*.html", "*.htm"}),
        definition("css", QStringLiteral("CSS"), "text", {"*.css", "*.scss", "*.less"}),
        definition("markdown", QStringLiteral("Markdown"), "text", {"*.md", "*.markdown"}),
        definition("ini", QStringLiteral("INI / 配置文件"), "text", {"*.ini", "*.conf", "*.cfg", "*.toml", "*.yml", "*.yaml"}),
        definition("shell", QStringLiteral("Shell"), "text", {"*.sh", "*.bash", "*.zsh", "*.ps1", ".bashrc", ".zshrc"}),
        definition("batch", QStringLiteral("批处理"), "text", {"*.bat", "*.cmd"}),
        definition("sql", QStringLiteral("SQL"), "text", {"*.sql"}),
        definition("log", QStringLiteral("日志"), "text", {"*.log"}),
        definition("makefile", QStringLiteral("Makefile"), "text", {"Makefile", "GNUmakefile", "*.mk", "CMakeLists.txt"}),
        definition("plain-text", QStringLiteral("纯文本"), "text", {"*.txt", "README", "LICENSE", "COPYING"})
    };
}

QStringList validateDefinition(const FormatDefinition &d)
{
    QStringList errors;
    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    if (!idPattern.match(d.id).hasMatch())
        errors << QStringLiteral("格式 ID 无效：%1").arg(d.id);
    if (d.name.trimmed().isEmpty())
        errors << QStringLiteral("格式名称不能为空。");
    if (!isValidSessionTypeId(d.sessionTypeId))
        errors << QStringLiteral("视图类型 ID 无效：%1").arg(d.sessionTypeId);
    for (const auto &mask : d.masks) {
        const auto compiled = Filter::Mask::compile(mask);
        if (mask.trimmed().isEmpty() || !compiled.ok())
            errors << QStringLiteral("掩码「%1」无效：%2").arg(mask, compiled.error.describe());
    }
    for (const auto &signature : d.signatures) {
        if (signature.offset < 0 || signature.offset > 1024 * 1024 - signature.bytes.size() ||
            signature.bytes.isEmpty() || signature.bytes.size() > 4096)
            errors << QStringLiteral("内容签名必须有 1–4096 个字节，且完整位于文件前 1048576 字节内。");
    }
    return errors;
}

DefinitionLoadResult parseDefinitions(const QByteArray &json, const QVector<FormatDefinition> &base)
{
    DefinitionLoadResult result;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.diagnostics << QStringLiteral("格式定义文件不是有效 JSON 对象：%1").arg(error.errorString());
        return result;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("version")).toDouble(-1) != 1 ||
        !root.value(QStringLiteral("definitions")).isArray()) {
        result.diagnostics << QStringLiteral("格式定义版本不支持，或缺少 definitions 数组。");
        return result;
    }
    result.documentValid = true;
    QSet<QString> ids;
    int index = 0;
    for (const auto &value : root.value(QStringLiteral("definitions")).toArray()) {
        ++index;
        QStringList errors;
        const auto object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString baseId = object.value(QStringLiteral("baseId")).toString(id);
        const auto *parent = findDefinition(base, baseId);
        FormatDefinition d = parent ? *parent : FormatDefinition();
        d.builtIn = false;
        d.id = id;
        if (!value.isObject())
            errors << QStringLiteral("条目须是对象。");
        if (object.contains(QStringLiteral("baseId")) && !object.value(QStringLiteral("baseId")).isString())
            errors << QStringLiteral("baseId 须是字符串。");
        if (object.contains(QStringLiteral("baseId")) && !parent)
            errors << QStringLiteral("找不到继承来源：%1").arg(baseId);
        if (object.contains(QStringLiteral("name")))
            d.name = object.value(QStringLiteral("name")).toString();
        if (object.contains(QStringLiteral("sessionTypeId")))
            d.sessionTypeId = object.value(QStringLiteral("sessionTypeId")).toString();
        if (object.contains(QStringLiteral("masks"))) {
            d.masks.clear();
            if (!object.value(QStringLiteral("masks")).isArray())
                errors << QStringLiteral("masks 须是字符串数组。");
            for (const auto &mask : object.value(QStringLiteral("masks")).toArray()) {
                if (!mask.isString())
                    errors << QStringLiteral("掩码须是字符串。");
                d.masks << mask.toString();
            }
        }
        if (object.contains(QStringLiteral("signatures"))) {
            d.signatures.clear();
            if (!object.value(QStringLiteral("signatures")).isArray())
                errors << QStringLiteral("signatures 须是数组。");
            for (const auto &signature : object.value(QStringLiteral("signatures")).toArray()) {
                const auto s = signature.toObject();
                const auto hex = s.value(QStringLiteral("hex")).toString();
                const auto offset = s.value(QStringLiteral("offset"));
                static const QRegularExpression hexPattern(QStringLiteral("^(?:[0-9a-fA-F]{2})+$"));
                if (!signature.isObject() || !hexPattern.match(hex).hasMatch() ||
                    !offset.isDouble() || offset.toDouble() != offset.toInt(-1))
                    errors << QStringLiteral("内容签名须有整数 offset 和成对十六进制 hex。");
                d.signatures.append({offset.toInt(-1), QByteArray::fromHex(hex.toLatin1())});
            }
        }
        if (object.contains(QStringLiteral("settings"))) {
            if (!object.value(QStringLiteral("settings")).isObject())
                errors << QStringLiteral("settings 须是对象。");
            const auto settings = object.value(QStringLiteral("settings")).toObject();
            for (auto it = settings.begin(); it != settings.end(); ++it)
                d.settings.insert(it.key(), it.value());
        }
        errors.append(validateDefinition(d));
        if (ids.contains(id))
            errors << QStringLiteral("格式 ID 重复：%1").arg(id);
        if (!errors.isEmpty()) {
            result.diagnostics << QStringLiteral("已跳过第 %1 个格式（%2）：%3").arg(index).arg(id, errors.join(QStringLiteral("；")));
            continue;
        }
        ids.insert(id);
        result.definitions.append(d);
    }
    return result;
}

QByteArray serializeDefinitions(const QVector<FormatDefinition> &definitions)
{
    QJsonArray array;
    for (const auto &definition : definitions)
        array.append(serialize(definition));
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                    {QStringLiteral("definitions"), array}}).toJson(QJsonDocument::Indented);
}

DefinitionLoadResult loadDefinitions(const QString &path, const QVector<FormatDefinition> &base)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        DefinitionLoadResult result;
        result.diagnostics << QStringLiteral("无法读取格式定义 %1：%2").arg(path, file.errorString());
        return result;
    }
    // Definition files are configuration, not comparison payloads.
    if (file.size() > 8 * 1024 * 1024) {
        DefinitionLoadResult result;
        result.diagnostics << QStringLiteral("格式定义文件超过 8 MiB 限制：%1").arg(path);
        return result;
    }
    const auto bytes = file.read(8 * 1024 * 1024 + 1);
    if (bytes.size() > 8 * 1024 * 1024 || file.error() != QFileDevice::NoError) {
        DefinitionLoadResult result;
        result.diagnostics << QStringLiteral("格式定义读取失败或超过 8 MiB 限制：%1").arg(path);
        return result;
    }
    return parseDefinitions(bytes, base);
}

bool saveDefinitions(const QString &path, const QVector<FormatDefinition> &definitions, QString *error)
{
    if (error)
        error->clear();
    const auto json = serializeDefinitions(definitions);
    if (json.size() > 8 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("格式定义超过 8 MiB 限制，未保存。");
        return false;
    }
    const auto verified = parseDefinitions(json);
    if (!verified.documentValid || !verified.diagnostics.isEmpty()) {
        if (error)
            *error = verified.diagnostics.join(QLatin1Char('\n'));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("无法保存格式定义 %1：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QVector<FormatDefinition> mergeDefinitions(const QVector<FormatDefinition> &builtIns,
                                         const QVector<FormatDefinition> &user,
                                         const QStringList &priorityIds)
{
    QVector<FormatDefinition> result;
    QSet<QString> added;
    auto append = [&](const QString &id) {
        if (added.contains(id))
            return;
        auto *d = findDefinition(user, id);
        const bool isUser = d != nullptr;
        if (!d)
            d = findDefinition(builtIns, id);
        if (!d)
            return;
        auto copy = *d;
        if (isUser)
            copy.builtIn = false;
        result.append(copy);
        added.insert(id);
    };
    for (const auto &id : priorityIds)
        append(id);
    for (const auto &d : user)
        append(d.id);
    for (const auto &d : builtIns)
        append(d.id);
    return result;
}

} // namespace Format
} // namespace LqCompare
