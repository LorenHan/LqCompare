#include "diagnostics.h"

#include "logfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace LqCompare {
namespace Log {

namespace {

const QString kManifestFileName = QStringLiteral("manifest.json");
const QString kEnvironmentReportFileName = QStringLiteral("environment.txt");
const QString kLogArchiveDirectoryName = QStringLiteral("logs");
const QString kBundleFormat = QStringLiteral("LqCompare.Diagnostics");
constexpr int kBundleFormatVersion = 1;

bool isPathSeparator(QChar character)
{
    return character == QLatin1Char('/') || character == QLatin1Char('\\');
}

/// 这个字符会不会让「前缀」变成另一个**更长的名字**的一部分。
///
/// 只在**是**的时候才拒绝替换（而不是要求后面必须是分隔符）。方向是这样定的：
/// 漏掉一次替换等于把一个真实路径原样发出去（隐私泄漏），多替换一次只是少看了
/// 几个字符（外观问题）。于是把「继续构成名字」的字符收窄到真正会延续路径名的那些：
/// 字母、数字与 `_ - .`（`/Users/loren2`、`/Users/loren.bak`、`/Users/loren-x` 属此类）。
/// 空白、换行、中文标点、`：`、右括号等等都算边界——日志里路径后面跟的
/// 几乎全是这些。
bool continuesTheName(QChar character)
{
    if (character == QLatin1Char('_') || character == QLatin1Char('-') || character == QLatin1Char('.'))
        return true;
    const ushort code = character.unicode();
    const bool asciiLetter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
    const bool asciiDigit = code >= '0' && code <= '9';
    return asciiLetter || asciiDigit;
}

QString normalizedPrefix(const QString &prefix)
{
    QString value = prefix.trimmed();
    while (!value.isEmpty() && isPathSeparator(value.at(value.size() - 1)))
        value.chop(1);
    return value;
}

/// 套用一条规则，把命中的次数累加进 `hits`。
///
/// 这里没有用 `QString::replace()`：它会连**前缀恰好相同的另一个名字**一起换掉。
/// 家目录是 `/Users/loren` 时，日志里 `/Users/loren2/proj` 会被换成 `~2/proj`——
/// 那份「已脱敏」的诊断包于是把另一台机器/另一个人的路径写成了家目录下的东西，
/// 排查者会照着这个不存在的路径去问问题。所以后面那个字符必须**不能延续名字**
/// （见 `continuesTheName()`）。
QString applyRule(const QString &text, const RedactionRule &rule, int *hits)
{
    const QString prefix = rule.prefix;
    if (prefix.isEmpty())
        return text;

    QString result;
    result.reserve(text.size());
    int index = 0;
    while (index < text.size()) {
        const int found = text.indexOf(prefix, index);
        if (found < 0) {
            result += text.mid(index);
            break;
        }
        const int after = found + prefix.size();
        const bool boundary = after >= text.size() || !continuesTheName(text.at(after));
        if (!boundary) {
            result += text.mid(index, after - index);
            index = after;
            continue;
        }
        result += text.mid(index, found - index);
        result += rule.replacement;
        if (hits != nullptr)
            ++(*hits);
        index = after;
    }
    return result;
}

QString writeTextFile(const QString &path, const QString &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return QString();
    }
    file.write(content.toUtf8());
    file.close();
    return path;
}

} // namespace

QVector<RedactionRule> defaultRedactionRules(const QString &homeDirectory,
                                             const QString &storageDirectory)
{
    QVector<RedactionRule> rules;
    const QString home = normalizedPrefix(homeDirectory);
    if (!home.isEmpty())
        rules.append({home, QStringLiteral("~")});

    const QString storage = normalizedPrefix(storageDirectory);
    // 配置目录通常就在家目录下（`~/.config/…`）。它自己也要有一条规则，
    // 否则脱敏后那几行仍是 `~/.config/LqCompare`，等于把「这台机器上的谁在用」
    // 之外的东西也说清楚了——而配置目录恰恰是最常出现在日志里的那一串路径。
    if (!storage.isEmpty() && storage != home)
        rules.append({storage, QStringLiteral("<配置目录>")});

    // 长前缀优先：`/Users/loren/.config/LqCompare` 必须先于 `/Users/loren` 匹配，
    // 否则配置目录那一条永远轮不到（家目录那条已经把它吃掉了）。
    std::sort(rules.begin(), rules.end(), [](const RedactionRule &left, const RedactionRule &right) {
        return left.prefix.size() > right.prefix.size();
    });
    return rules;
}

QString sanitizeDiagnosticText(const QString &text, const QVector<RedactionRule> &rules,
                               int *replacedCount)
{
    QString result = text;
    int hits = 0;
    for (const RedactionRule &rule : rules)
        result = applyRule(result, rule, &hits);
    if (replacedCount != nullptr)
        *replacedCount = hits;
    return result;
}

QString environmentReportFileName()
{
    return kEnvironmentReportFileName;
}

QString manifestFileName()
{
    return kManifestFileName;
}

QString logArchiveDirectoryName()
{
    return kLogArchiveDirectoryName;
}

QString environmentReport(const DiagnosticEnvironment &environment)
{
    const QString unknown = QStringLiteral("（未知）");
    const auto orUnknown = [&unknown](const QString &value) {
        return value.isEmpty() ? unknown : value;
    };

    QStringList lines;
    lines << QStringLiteral("%1 诊断包").arg(orUnknown(environment.applicationName));
    lines << QStringLiteral("程序名称：%1").arg(orUnknown(environment.applicationName));
    lines << QStringLiteral("程序版本：%1").arg(orUnknown(environment.applicationVersion));
    lines << QStringLiteral("Qt 版本：%1").arg(orUnknown(environment.qtVersion));
    lines << QStringLiteral("操作系统：%1").arg(orUnknown(environment.osDescription));
    lines << QStringLiteral("处理器架构：%1").arg(orUnknown(environment.architecture));
    lines << QStringLiteral("存储模式：%1").arg(orUnknown(environment.storageMode));
    lines << QStringLiteral("配置目录：%1").arg(orUnknown(environment.storageDirectory));
    lines << QStringLiteral("日志级别：%1").arg(orUnknown(environment.logLevel));
    lines << QStringLiteral("日志轮转：%1").arg(orUnknown(environment.rotationSummary));
    lines << QStringLiteral("日志文件：%1").arg(environment.logFilePath.isEmpty()
                                                        ? QStringLiteral("未启用文件日志")
                                                        : environment.logFilePath);
    lines << QStringLiteral("导出时间：%1").arg(environment.exportTime.isValid()
                                                        ? environment.exportTime.toString(Qt::ISODate)
                                                        : unknown);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString diagnosticNoticeText(bool redactPaths)
{
    if (redactPaths) {
        return QStringLiteral(
                "诊断包包含日志原文、程序版本与运行环境信息。"
                "已选择脱敏：日志与报告里的路径中，家目录会被替换成 ~、配置目录会被替换成 "
                "<配置目录>，但路径之外的内容不做删改——导出后请自行确认没有别的不便公开的信息。");
    }
    return QStringLiteral(
            "诊断包包含日志原文、程序版本与运行环境信息，"
            "并且**不脱敏**：里面的路径带着用户名与项目目录名。"
            "只有确认接收方可以看这些路径时才这样导出。");
}

DiagnosticBundleResult buildDiagnosticBundle(const DiagnosticBundleRequest &request)
{
    DiagnosticBundleResult result;

    if (request.outputDirectory.trimmed().isEmpty()) {
        result.error = QStringLiteral("没有指定诊断包的输出目录。");
        return result;
    }

    const QDateTime now = request.now.isValid() ? request.now : QDateTime::currentDateTime();
    QDir root(request.outputDirectory);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("无法创建诊断包目录：%1").arg(request.outputDirectory);
        return result;
    }

    // 同一秒内重复导出退化成 `…-2` / `…-3`，绝不复用已有目录：两份诊断包
    // 混在一起之后，没人说得清哪一行日志是哪一次问题的。
    const QString baseName = QStringLiteral("lqcompare-diagnostics-%1")
                                     .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString bundlePath = root.filePath(baseName);
    for (int suffix = 2; QFileInfo::exists(bundlePath); ++suffix)
        bundlePath = root.filePath(QStringLiteral("%1-%2").arg(baseName).arg(suffix));

    if (!QDir().mkpath(bundlePath)) {
        result.error = QStringLiteral("无法创建诊断包目录：%1").arg(bundlePath);
        return result;
    }

    // 从这一刻起，任何失败都要把半成品目录删掉：一个内容不全、名字却与
    // 正常诊断包一模一样的目录，比「导出失败」糟得多——用户会把它发出去。
    const auto fail = [&bundlePath, &result](const QString &message) {
        QDir(bundlePath).removeRecursively();
        result.bundleDirectory.clear();
        result.files.clear();
        result.archivedLogs.clear();
        result.error = message;
        result.ok = false;
        return result;
    };

    DiagnosticEnvironment environment = request.environment;
    if (environment.logFilePath.isEmpty())
        environment.logFilePath = request.logFilePath;
    if (environment.storageDirectory.isEmpty())
        environment.storageDirectory = request.storageDirectory;
    environment.exportTime = now;

    const QVector<RedactionRule> rules = request.redactPaths
            ? defaultRedactionRules(request.homeDirectory, request.storageDirectory)
            : QVector<RedactionRule>();
    result.redacted = request.redactPaths;
    int redactedOccurrences = 0;

    // ---- 日志（当前文件 + 轮转历史）----
    QStringList logSources;
    if (!request.logFilePath.isEmpty() && QFileInfo(request.logFilePath).isFile()) {
        logSources << request.logFilePath;
        logSources += logHistoryFiles(request.logFilePath);
    }
    if (!logSources.isEmpty()) {
        QDir bundleDirectory(bundlePath);
        if (!bundleDirectory.mkpath(kLogArchiveDirectoryName))
            return fail(QStringLiteral("无法创建诊断包内的日志目录：%1")
                                .arg(bundleDirectory.filePath(kLogArchiveDirectoryName)));

        for (const QString &path : logSources) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return fail(QStringLiteral("无法读取日志 %1：%2").arg(path, file.errorString()));
            }
            const QString content = QString::fromUtf8(file.readAll());
            file.close();

            int hits = 0;
            const QString outgoing = request.redactPaths
                    ? sanitizeDiagnosticText(content, rules, &hits)
                    : content;
            redactedOccurrences += hits;

            const QString relative = QStringLiteral("%1/%2")
                                             .arg(kLogArchiveDirectoryName,
                                                  QFileInfo(path).fileName());
            QString writeError;
            if (writeTextFile(bundleDirectory.filePath(relative), outgoing, &writeError).isEmpty())
                return fail(writeError);
            result.files << relative;
            result.archivedLogs << path;
        }
        result.logIncluded = true;
    }

    // ---- 环境信息 ----
    // 环境信息里的配置目录与日志路径同样是机器相关路径，必须和日志一起走脱敏。
    // 「日志脱敏了、环境信息没脱」是最容易漏的一种：报告开头那几行正是
    // 一眼就能看到的地方。
    {
        int hits = 0;
        const QString report = environmentReport(environment);
        const QString outgoing = request.redactPaths
                ? sanitizeDiagnosticText(report, rules, &hits)
                : report;
        redactedOccurrences += hits;

        QString writeError;
        if (writeTextFile(QDir(bundlePath).filePath(kEnvironmentReportFileName), outgoing,
                          &writeError).isEmpty())
            return fail(writeError);
        result.files << kEnvironmentReportFileName;
    }

    // ---- 清单 ----
    {
        QJsonObject application;
        application.insert(QStringLiteral("name"), environment.applicationName);
        application.insert(QStringLiteral("version"), environment.applicationVersion);

        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), kBundleFormat);
        manifest.insert(QStringLiteral("formatVersion"), kBundleFormatVersion);
        manifest.insert(QStringLiteral("exportedAt"), now.toString(Qt::ISODate));
        manifest.insert(QStringLiteral("application"), application);
        manifest.insert(QStringLiteral("qtVersion"), environment.qtVersion);
        manifest.insert(QStringLiteral("os"), environment.osDescription);
        manifest.insert(QStringLiteral("architecture"), environment.architecture);
        manifest.insert(QStringLiteral("storageMode"), environment.storageMode);
        manifest.insert(QStringLiteral("logLevel"), environment.logLevel);
        manifest.insert(QStringLiteral("rotation"), environment.rotationSummary);
        manifest.insert(QStringLiteral("logIncluded"), result.logIncluded);
        // 脱敏状态与替换次数都写进清单：接收方据此判断这份包能不能公开，
        // 而不必逐个文件去找有没有漏掉的用户名。
        manifest.insert(QStringLiteral("redacted"), result.redacted);
        manifest.insert(QStringLiteral("redactedOccurrences"), redactedOccurrences);

        QJsonArray logFiles;
        for (const QString &path : result.archivedLogs)
            logFiles.append(QFileInfo(path).fileName());
        manifest.insert(QStringLiteral("logFiles"), logFiles);

        // 清单自己也算包内的一个文件，而且要**排在最后**——它是最后写出来的。
        // 少了这一行，清单与包内实际内容就对不上（用户按它核对时以为漏了东西）。
        QStringList listedFiles = result.files;
        listedFiles << kManifestFileName;
        QJsonArray files;
        for (const QString &path : listedFiles)
            files.append(path);
        manifest.insert(QStringLiteral("files"), files);

        QString writeError;
        const QString content = QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        if (writeTextFile(QDir(bundlePath).filePath(kManifestFileName), content, &writeError).isEmpty())
            return fail(writeError);
        result.files << kManifestFileName;
    }

    result.ok = true;
    result.error.clear();
    result.bundleDirectory = bundlePath;
    result.redactedOccurrences = redactedOccurrences;
    return result;
}

} // namespace Log
} // namespace LqCompare
