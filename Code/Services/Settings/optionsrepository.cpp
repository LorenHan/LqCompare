#include "optionsrepository.h"

#include "../Log/logfiles.h"
#include "../Log/logging.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#include <utility>

namespace LqCompare {
namespace Settings {
namespace {
constexpr int CurrentVersion = 1;
constexpr qint64 MaximumFileSize = 2 * 1024 * 1024;

OperationResult failure(const QString &message)
{
    OperationResult result;
    result.error = message;
    return result;
}

QByteArray encode(const QVariantMap &values, const QVariantMap &unknown = {})
{
    QVariantMap combined = unknown;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        combined.insert(it.key(), it.value());
    return QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("LqCompare.Options")},
        {QStringLiteral("version"), CurrentVersion},
        {QStringLiteral("settings"), QJsonObject::fromVariantMap(combined)}
    }).toJson(QJsonDocument::Indented);
}

struct Parsed {
    bool ok = false;
    bool futureVersion = false;
    QString error;
    QVariantMap values;
    QVariantMap unknown;
    QByteArray fingerprint;
};

Parsed parseFile(const QString &path)
{
    Parsed result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取设置文件 %1：%2").arg(path, file.errorString());
        return result;
    }
    if (file.size() > MaximumFileSize) {
        result.error = QStringLiteral("设置文件超过 2 MiB 上限：%1").arg(path);
        return result;
    }
    const QByteArray bytes = file.read(MaximumFileSize + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaximumFileSize) {
        result.error = QStringLiteral("读取设置文件失败或文件过大：%1").arg(path);
        return result;
    }
    result.fingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("设置 JSON 损坏（位置 %1）：%2")
                               .arg(jsonError.offset).arg(jsonError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    const double numericVersion = versionValue.toDouble(-1);
    if (!versionValue.isDouble() || !std::isfinite(numericVersion)
        || std::floor(numericVersion) != numericVersion || numericVersion < 0) {
        result.error = QStringLiteral("设置文件缺少合法的整数版本号。");
        return result;
    }
    if (numericVersion > CurrentVersion) {
        result.futureVersion = true;
        result.error = QStringLiteral("设置文件版本 %1 高于支持版本 %2，已保留原文件；请升级程序或先备份后重置。")
                               .arg(numericVersion).arg(CurrentVersion);
        return result;
    }
    const int version = int(numericVersion);
    const QJsonValue format = root.value(QStringLiteral("format"));
    if ((version == 1 || !format.isUndefined())
        && format.toString() != QStringLiteral("LqCompare.Options")) {
        result.error = QStringLiteral("该文件不是 LqCompare 全局设置文件。");
        return result;
    }
    const QJsonValue settings = root.value(version == 0 ? QStringLiteral("values")
                                                       : QStringLiteral("settings"));
    if (!settings.isObject() || settings.toObject().size() > 512) {
        result.error = QStringLiteral("设置项必须为对象，且不得超过 512 项。");
        return result;
    }
    const QJsonObject object = settings.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const OptionDefinition *def = OptionsRepository::definition(it.key());
        if (!def) {
            result.unknown.insert(it.key(), it.value().toVariant());
            continue;
        }
        // JSON has one numeric type; do not let QVariant silently coerce strings/bools.
        QVariant value;
        if (def->defaultValue.type() == QVariant::Bool && it.value().isBool())
            value = it.value().toBool();
        else if (def->defaultValue.type() == QVariant::Int && it.value().isDouble()) {
            const double number = it.value().toDouble();
            if (std::isfinite(number) && std::floor(number) == number
                && number >= def->minimum && number <= def->maximum)
                value = int(number);
        } else if (def->defaultValue.type() == QVariant::String && it.value().isString())
            value = it.value().toString();
        const QString validation = OptionsRepository::validate(it.key(), value);
        if (!validation.isEmpty()) {
            result.error = validation;
            return result;
        }
        result.values.insert(it.key(), value);
    }
    result.ok = true;
    return result;
}

QStringList difference(const QVariantMap &left, const QVariantMap &right)
{
    QStringList result;
    for (auto it = right.cbegin(); it != right.cend(); ++it) {
        if (left.value(it.key()) != it.value())
            result.append(it.key());
    }
    return result;
}

bool samePath(const QString &left, const QString &right)
{
    const QFileInfo a(left), b(right);
    if (!a.canonicalFilePath().isEmpty() && !b.canonicalFilePath().isEmpty())
        return a.canonicalFilePath() == b.canonicalFilePath();
    return a.absoluteFilePath() == b.absoluteFilePath();
}

OperationResult atomicWrite(const QString &path, const QByteArray &bytes)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return failure(QStringLiteral("无法创建设置目录：%1").arg(info.absolutePath()));
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
    if (file.write(bytes) != bytes.size() || !file.commit())
        return failure(QStringLiteral("无法原子保存 %1：%2").arg(path, file.errorString()));
    OperationResult result;
    result.ok = true;
    return result;
}

bool knownCategory(const QString &category)
{
    if (category.isEmpty())
        return true;
    for (const OptionDefinition &def : OptionsRepository::definitions()) {
        if (def.category == category)
            return true;
    }
    return false;
}
} // namespace

QString StorageLocation::filePath() const
{
    return QDir(directory).filePath(QStringLiteral("options.json"));
}

StorageLocation StorageLocation::detect(bool forcePortable,
                                        const QString &executableDirectory,
                                        const QString &standardDirectory)
{
    const QString executable = executableDirectory.isEmpty()
            ? QCoreApplication::applicationDirPath() : executableDirectory;
    StorageLocation result;
    result.portable = forcePortable || QFileInfo(QDir(executable).filePath(
            QStringLiteral("lqcompare.portable"))).isFile();
    if (result.portable) {
        result.directory = QDir(executable).absoluteFilePath(QStringLiteral("config"));
    } else if (!standardDirectory.isEmpty()) {
        result.directory = QDir(standardDirectory).absolutePath();
    } else {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        result.directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
        result.directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
    }
    return result;
}

const QVector<OptionDefinition> &OptionsRepository::definitions()
{
    static const QVector<OptionDefinition> items = {
        {QStringLiteral("general.singleInstance"), QStringLiteral("general"),
         QStringLiteral("复用已有实例"), QStringLiteral("启用时，新启动的进程将文件交给已有窗口。下次启动生效。"),
         true, {}, 0, 0, false, true},
        {QStringLiteral("general.lastSessionAction"), QStringLiteral("general"),
         QStringLiteral("关闭最后会话后"), QStringLiteral("返回 Home 页，或在关闭最后一个会话后退出程序。"),
         QStringLiteral("home"), {QStringLiteral("home"), QStringLiteral("exit")}},
        {QStringLiteral("display.theme"), QStringLiteral("display"),
         QStringLiteral("主题"), QStringLiteral("系统主题、浅色或深色；应用后立即更新界面调色板。"),
         QStringLiteral("system"), {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")}},
        {QStringLiteral("display.uiFontFamily"), QStringLiteral("display"),
         QStringLiteral("界面字体"), QStringLiteral("影响菜单、按钮和列表。留空使用系统字体；内容字体单独设置。"), QString(), {}},
        {QStringLiteral("display.uiFontSize"), QStringLiteral("display"),
         QStringLiteral("界面字号"), QStringLiteral("单位为点；0 使用系统默认，或设置 6–48。"), 0, {}, 0, 48},
        {QStringLiteral("display.contentFontFamily"), QStringLiteral("display"),
         QStringLiteral("内容字体"), QStringLiteral("供文本比较视图使用。留空使用系统等宽字体。"), QString(), {}},
        {QStringLiteral("display.contentFontSize"), QStringLiteral("display"),
         QStringLiteral("内容字号"), QStringLiteral("文本内容的字号，单位为点，范围 6–72。"), 12, {}, 6, 72, false, false,
         QStringLiteral(" pt")},
        {QStringLiteral("logging.level"), QStringLiteral("logging"),
         QStringLiteral("日志级别"), QStringLiteral("仅记录所选严重性及以上的日志；调试会包含更多诊断信息。"),
         QStringLiteral("warning"), {QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("info"), QStringLiteral("debug")}},
        {QStringLiteral("logging.fileEnabled"), QStringLiteral("logging"),
         QStringLiteral("写入日志文件"), QStringLiteral("关闭时仍保留界面输出和控制台日志。"), true, {}},
        {QStringLiteral("logging.filePath"), QStringLiteral("logging"),
         QStringLiteral("日志文件位置"), QStringLiteral("绝对路径；留空自动使用配置目录下 logs/lqcompare.log。默认导出不包含此机器路径。"),
         QString(), {}, 0, 0, true},

        // 日志文件轮转与诊断（OPT-010 第 2、4 条）。
        //
        // 这一页的出厂值与 `Log::RotationPolicy` 的默认值必须一致：策略对象是
        // 「读出来的值是什么意思」的唯一实现，而这里是「默认是什么」的唯一实现。
        // 两边一旦分家，用户不碰设置时的行为与他在设置页看到的首个选项会对不上。
        // `Tests/LogDiagnostics` 有一条用例拿 `Log::RotationPolicy()` 的默认值
        // 与这几个定义比对，改任何一边都会红。
        //
        // `rotationMode` 的下拉选项直接取 `Log::rotationModeChoices()`，
        // 不在这里手抄一遍：抄一遍的代价是「加了一种模式之后设置页里选不到」，
        // 而那是没有任何报错的静默失效。
        {QStringLiteral("logging.rotationMode"), QStringLiteral("logging"),
         QStringLiteral("日志文件轮转"),
         QStringLiteral("按体积或按天把旧日志换出当前文件；不轮转时日志会一直追加到同一个文件。"),
         QStringLiteral("none"), Log::rotationModeChoices()},
        {QStringLiteral("logging.rotationMaximumMegabytes"), QStringLiteral("logging"),
         QStringLiteral("单份日志体积上限"),
         QStringLiteral("按体积轮转时，单份日志达到该值就换成新文件。单位为兆字节。"),
         5, {}, static_cast<int>(Log::minimumRotationMaximumMegabytes()),
         static_cast<int>(Log::maximumRotationMaximumMegabytes()), false, false, QStringLiteral(" MB")},
        {QStringLiteral("logging.rotationKeepFiles"), QStringLiteral("logging"),
         QStringLiteral("保留的历史日志份数"),
         QStringLiteral("轮转后保留的旧日志份数；0 表示不留历史，旧日志在轮转时被删除。"),
         5, {}, 0, Log::maximumRotationKeepFiles(), false, false, QStringLiteral(" 份")},
        {QStringLiteral("logging.performanceTiming"), QStringLiteral("logging"),
         QStringLiteral("记录详细性能计时"),
         QStringLiteral("打开后计时行不再受日志级别限制，可在保持常规日志量的同时临时排查慢操作。"),
         false, {}},

        // 文件操作的默认行为（OPT-005）。
        //
        // 这批键的「契约」不止是「存一个值」：规格的边界条款要求安全相关的
        // 默认值必须保守（默认走回收站、默认不覆盖）。把这条契约放在文档里
        // 会过期，因此它另有一份可执行的副本——`Files::FileOperationPolicy`
        // 的 `safetyContractViolations()`，`Tests/FileOpsOptions` 的 A 组
        // 拿它钉住下面这些出厂值。改任何一个默认值之前先看那条用例。
        {QStringLiteral("fileops.deleteMode"), QStringLiteral("fileops"),
         QStringLiteral("删除方式"), QStringLiteral("默认走回收站；选择永久删除时每次删除前都会提示不可恢复。"),
         QStringLiteral("trash"), {QStringLiteral("trash"), QStringLiteral("permanent")}},
        {QStringLiteral("fileops.overwritePolicy"), QStringLiteral("fileops"),
         QStringLiteral("覆盖策略"), QStringLiteral("目标已存在时逐个询问（默认）、直接覆盖或跳过；目标文件较新时另行提示。"),
         QStringLiteral("ask"), {QStringLiteral("ask"), QStringLiteral("overwrite"), QStringLiteral("skip")}},
        {QStringLiteral("fileops.preserveTimestamps"), QStringLiteral("fileops"),
         QStringLiteral("复制时保留修改时间"), QStringLiteral("关闭后复制出来的文件修改时间会变成复制的那一刻。"), true, {}},
        {QStringLiteral("fileops.preserveAttributes"), QStringLiteral("fileops"),
         QStringLiteral("复制时保留属性位"), QStringLiteral("如只读、隐藏等标记。"), true, {}},
        {QStringLiteral("fileops.preservePermissions"), QStringLiteral("fileops"),
         QStringLiteral("复制时保留权限"), QStringLiteral("Unix 权限位；Windows 上无对应项时自动忽略。"), true, {}},
        {QStringLiteral("fileops.largeFileConfirmMegabytes"), QStringLiteral("fileops"),
         QStringLiteral("大文件操作确认阈值"),
         QStringLiteral("体积达到该值的复制/移动/删除前先确认。单位为兆字节，0 表示关闭确认。"),
         100, {}, 0, 102400, false, false, QStringLiteral(" MB")},
        {QStringLiteral("fileops.batchDeleteConfirmCount"), QStringLiteral("fileops"),
         QStringLiteral("批量删除确认条数"),
         QStringLiteral("一次删除的条目数达到该值前先确认。0 表示关闭确认。"),
         20, {}, 0, 100000, false, false, QStringLiteral(" 个")},
        {QStringLiteral("fileops.verifyAfterCopy"), QStringLiteral("fileops"),
         QStringLiteral("操作后校验方式"),
         QStringLiteral("不校验（默认）、比对大小或比对 CRC；CRC 需要读回全部内容。"),
         QStringLiteral("none"), {QStringLiteral("none"), QStringLiteral("size"), QStringLiteral("crc")}}
    };
    return items;
}

QVariantMap OptionsRepository::defaults()
{
    QVariantMap result;
    for (const OptionDefinition &def : definitions())
        result.insert(def.key, def.defaultValue);
    return result;
}

const OptionDefinition *OptionsRepository::definition(const QString &key)
{
    for (const OptionDefinition &def : definitions()) {
        if (def.key == key)
            return &def;
    }
    return nullptr;
}

QString OptionsRepository::validate(const QString &key, const QVariant &value)
{
    const OptionDefinition *def = definition(key);
    if (!def)
        return QStringLiteral("未知的全局设置项：%1").arg(key);
    if (value.type() != def->defaultValue.type())
        return QStringLiteral("%1 的值类型不正确。").arg(def->title);
    if (value.type() == QVariant::Int
        && (value.toInt() < def->minimum || value.toInt() > def->maximum
            || (key == QStringLiteral("display.uiFontSize") && value.toInt() > 0 && value.toInt() < 6)))
        return QStringLiteral("%1 超出允许范围。").arg(def->title);
    if (!def->choices.isEmpty() && !def->choices.contains(value.toString()))
        return QStringLiteral("%1 包含不支持的选项：%2").arg(def->title, value.toString());
    if (value.type() == QVariant::String) {
        const QString text = value.toString();
        if (text.size() > 4096 || text.contains(QChar::Null) || text.contains(QLatin1Char('\n'))
            || text.contains(QLatin1Char('\r')))
            return QStringLiteral("%1 包含无效字符或过长。").arg(def->title);
        if (key == QStringLiteral("logging.filePath") && !text.isEmpty() && !QDir::isAbsolutePath(text))
            return QStringLiteral("日志文件位置必须为绝对路径，或留空使用默认值。");
    }
    return {};
}

OptionsRepository::OptionsRepository(StorageLocation location, QObject *parent)
    : QObject(parent), m_location(std::move(location)), m_values(defaults())
{
}

StorageLocation OptionsRepository::location() const { return m_location; }
QVariant OptionsRepository::value(const QString &key) const { return m_values.value(key); }
QVariantMap OptionsRepository::values() const { return m_values; }
QString OptionsRepository::lastLoadError() const { return m_loadError; }

OperationResult OptionsRepository::load()
{
    const QVariantMap previous = m_values;
    m_values = defaults();
    m_unknown.clear();
    m_loadError.clear();
    m_writeBlocked = false;
    OperationResult result;
    const QString path = m_location.filePath();
    if (!QFileInfo::exists(path)) {
        result.ok = true;
    } else {
        Parsed parsed = parseFile(path);
        if (parsed.ok) {
            QVariantMap candidate = defaults();
            for (auto it = parsed.values.cbegin(); it != parsed.values.cend(); ++it)
                candidate.insert(it.key(), it.value());
            parsed.error = validateDestination(candidate, false);
            parsed.ok = parsed.error.isEmpty();
        }
        if (parsed.ok) {
            for (auto it = parsed.values.cbegin(); it != parsed.values.cend(); ++it)
                m_values.insert(it.key(), it.value());
            m_unknown = parsed.unknown;
            result.ok = true;
        } else {
            m_loadError = parsed.error;
            if (!parsed.futureVersion) {
                const OperationResult backup = backupCurrent();
                result.backupPath = backup.backupPath;
                if (!backup.ok) {
                    m_writeBlocked = true;
                    m_loadError += QStringLiteral("\n备份失败，已禁止覆盖原文件：%1").arg(backup.error);
                } else {
                    m_loadError += QStringLiteral("\n已保留备份：%1").arg(backup.backupPath);
                }
            } else {
                m_writeBlocked = true;
            }
            result.error = m_loadError;
            LQCOMPARE_WARN("options", m_loadError);
            emit loadWarning(m_loadError);
        }
    }
    result.changedKeys = difference(previous, m_values);
    if (!result.changedKeys.isEmpty())
        emit changed(result.changedKeys);
    return result;
}

OperationResult OptionsRepository::backupCurrent() const
{
    const QString source = m_location.filePath();
    QString destination = source + QStringLiteral(".bak");
    if (QFileInfo::exists(destination)) {
        destination += QStringLiteral(".") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz"))
                + QStringLiteral(".") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    OperationResult result;
    if (QFileInfo::exists(source)) {
        if (!QFileInfo(source).isFile())
            return failure(QStringLiteral("设置路径不是普通文件，无法创建可靠备份：%1").arg(source));
        QFile file(source);
        if (!file.copy(destination))
            return failure(QStringLiteral("无法备份设置 %1：%2").arg(source, file.errorString()));
        result.ok = true;
    } else {
        result = atomicWrite(destination, encode(m_values, m_unknown));
    }
    if (result.ok)
        result.backupPath = destination;
    return result;
}

OperationResult OptionsRepository::writeValues(const QVariantMap &next, bool requireBackup,
                                              bool allowRecovery, bool discardUnknown)
{
    if (m_writeBlocked && !allowRecovery)
        return failure(QStringLiteral("原设置文件暂不可覆盖。请先解决读取问题，或明确重置并备份。\n%1").arg(m_loadError));
    const QString destinationError = validateDestination(next, true);
    if (!destinationError.isEmpty())
        return failure(destinationError);
    const QByteArray encoded = encode(next, discardUnknown ? QVariantMap() : m_unknown);
    if (encoded.size() > MaximumFileSize || next.size() + (discardUnknown ? 0 : m_unknown.size()) > 512)
        return failure(QStringLiteral("设置内容超过 2 MiB 或 512 项上限，未写入文件。"));
    OperationResult result;
    if (requireBackup) {
        result = backupCurrent();
        if (!result.ok)
            return result;
    }
    const OperationResult write = atomicWrite(m_location.filePath(), encoded);
    if (!write.ok) {
        result.ok = false;
        result.error = write.error;
        return result;
    }
    result.ok = true;
    result.changedKeys = difference(m_values, next);
    m_values = next;
    if (discardUnknown)
        m_unknown.clear();
    m_writeBlocked = false;
    m_loadError.clear();
    if (!result.changedKeys.isEmpty())
        emit changed(result.changedKeys);
    return result;
}

OperationResult OptionsRepository::apply(const QVariantMap &changes)
{
    QVariantMap next = m_values;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const QString error = validate(it.key(), it.value());
        if (!error.isEmpty())
            return failure(error);
        next.insert(it.key(), it.value());
    }
    return writeValues(next, false);
}

QString OptionsRepository::validateDestination(const QVariantMap &values, bool checkWritable) const
{
    // Prevent logging from appending into the repository or its recovery backups,
    // including imported paths. Missing writable directories are created at runtime.
    QString logPath = values.value(QStringLiteral("logging.filePath")).toString();
    if (logPath.isEmpty())
        logPath = QDir(m_location.directory).filePath(QStringLiteral("logs/lqcompare.log"));
    {
        const QFileInfo target(logPath);
        const QFileInfo settingsFile(m_location.filePath());
        const QString resolved = target.canonicalFilePath();
        const QString canonicalDirectory = settingsFile.absoluteDir().canonicalPath();
        const QString canonicalBackupPrefix = canonicalDirectory.isEmpty() ? QString()
                : QDir(canonicalDirectory).filePath(settingsFile.fileName() + QStringLiteral(".bak"));
        if (samePath(logPath, m_location.filePath())
            || target.absoluteFilePath().startsWith(settingsFile.absoluteFilePath() + QStringLiteral(".bak"))
            || (!canonicalBackupPrefix.isEmpty() && resolved.startsWith(canonicalBackupPrefix)))
            return QStringLiteral("日志文件不能与设置文件或设置备份相同。");
        if (!checkWritable || !values.value(QStringLiteral("logging.fileEnabled")).toBool())
            return {};
        QFileInfo ancestor = target;
        while (!ancestor.exists() && ancestor.absoluteFilePath() != ancestor.absolutePath())
            ancestor.setFile(ancestor.absolutePath());
        if ((target.exists() && (!target.isFile() || !target.isWritable()))
            || !ancestor.isWritable() || (!target.exists() && !ancestor.isDir()))
            return QStringLiteral("日志文件位置不可写：%1").arg(logPath);
    }
    return {};
}

OperationResult OptionsRepository::reset(const QString &category)
{
    if (!knownCategory(category))
        return failure(QStringLiteral("未知的设置分类：%1").arg(category));
    QVariantMap next = m_values;
    for (const OptionDefinition &def : definitions()) {
        if (category.isEmpty() || def.category == category)
            next.insert(def.key, def.defaultValue);
    }
    // Partial reset must not overwrite an unsupported-version file.
    if (m_writeBlocked && !category.isEmpty())
        return failure(QStringLiteral("无法读取的设置必须先完整备份并重置，不能仅重置分类。"));
    OperationResult result = writeValues(next, true, category.isEmpty(), category.isEmpty());
    if (result.ok)
        LQCOMPARE_INFO("options", QStringLiteral("已重置全局设置分类 %1；备份 %2").arg(category.isEmpty() ? QStringLiteral("全部") : category, result.backupPath));
    return result;
}

OperationResult OptionsRepository::exportFile(const QString &path, const QStringList &categories,
                                             bool includeMachineSpecific) const
{
    if (samePath(path, m_location.filePath()))
        return failure(QStringLiteral("导出文件不能覆盖正在使用的设置仓库。"));
    if (!Log::logFile().isEmpty() && samePath(path, Log::logFile()))
        return failure(QStringLiteral("导出文件不能覆盖当前日志文件。"));
    for (const QString &category : categories) {
        if (!knownCategory(category))
            return failure(QStringLiteral("未知的导出分类：%1").arg(category));
    }
    QVariantMap exported;
    for (const OptionDefinition &def : definitions()) {
        if ((categories.isEmpty() || categories.contains(def.category))
            && (!def.machineSpecific || includeMachineSpecific))
            exported.insert(def.key, m_values.value(def.key));
    }
    OperationResult result = atomicWrite(path, encode(exported));
    if (result.ok)
        LQCOMPARE_INFO("options", QStringLiteral("已导出 %1 项全局设置。机器路径%2包含。")
                               .arg(exported.size()).arg(includeMachineSpecific ? QStringLiteral("已") : QStringLiteral("未")));
    return result;
}

ImportPreview OptionsRepository::previewImport(const QString &path) const
{
    ImportPreview result;
    const Parsed parsed = parseFile(path);
    result.ok = parsed.ok;
    result.error = parsed.error;
    if (!parsed.ok)
        return result;
    result.ignoredKeys = parsed.unknown.keys();
    result.sourceFingerprint = parsed.fingerprint;
    for (auto it = parsed.values.cbegin(); it != parsed.values.cend(); ++it) {
        ImportEntry entry;
        entry.key = it.key();
        entry.currentValue = m_values.value(it.key());
        entry.incomingValue = it.value();
        entry.conflict = entry.currentValue != entry.incomingValue;
        result.entries.append(entry);
    }
    return result;
}

OperationResult OptionsRepository::importFile(const QString &path, const QStringList &selectedKeys,
                                             const QByteArray &expectedFingerprint)
{
    const ImportPreview preview = previewImport(path);
    if (!preview.ok)
        return failure(preview.error);
    if (!expectedFingerprint.isEmpty() && expectedFingerprint != preview.sourceFingerprint)
        return failure(QStringLiteral("导入文件在预览后发生变化，请重新预览并选择设置项。"));
    QVariantMap available;
    for (const ImportEntry &entry : preview.entries)
        available.insert(entry.key, entry.incomingValue);
    QVariantMap next = m_values;
    for (const QString &key : selectedKeys) {
        if (!available.contains(key))
            return failure(QStringLiteral("导入文件不包含所选设置项：%1").arg(key));
        next.insert(key, available.value(key));
    }
    OperationResult result = writeValues(next, true);
    if (result.ok)
        LQCOMPARE_INFO("options", QStringLiteral("已导入 %1 项全局设置，备份 %2。")
                               .arg(selectedKeys.size()).arg(result.backupPath));
    return result;
}

} // namespace Settings
} // namespace LqCompare
