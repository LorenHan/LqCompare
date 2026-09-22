#include "cliexecution.h"
#include "foldercompare.h"
#include "entrystatus.h"
#include "textdocument.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <exception>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace LqCompare { namespace Cli {
namespace {
QString escapedField(QString value)
{
    return value.replace('\\', QStringLiteral("\\\\")).replace('\t', QStringLiteral("\\t"))
            .replace('\r', QStringLiteral("\\r")).replace('\n', QStringLiteral("\\n"));
}
QString csvField(QString value)
{
    // Prefix formula-looking spreadsheet cells so a summary opened in Excel
    // cannot execute a path supplied by an untrusted data source.
    if (!value.isEmpty() && QStringLiteral("=+-@\t\r").contains(value.at(0))) value.prepend('\'');
    return '"' + value.replace('"', QStringLiteral("\"\"")) + '"';
}
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
    if (resolvedParent.isEmpty()) return {};
    return QDir(resolvedParent).filePath(QFileInfo(absolute).fileName());
}
bool sameFileObject(const QString &a, const QString &b)
{
#ifdef Q_OS_UNIX
    struct stat first, second;
    return ::stat(QFile::encodeName(a).constData(), &first) == 0
            && ::stat(QFile::encodeName(b).constData(), &second) == 0
            && first.st_dev == second.st_dev && first.st_ino == second.st_ino;
#else
    Q_UNUSED(a); Q_UNUSED(b);
    return false;
#endif
}
bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    if (file.write(bytes) != bytes.size()) { if (error) *error = file.errorString(); file.cancelWriting(); return false; }
    if (!file.commit()) { if (error) *error = file.errorString(); return false; }
    if (error) error->clear();
    return true;
}
QString summaryText(const QJsonObject &summary)
{
    return QStringLiteral("result\t%1\t%2\t%3\t%4\t%5\n")
            .arg(summary.value("status").toString(), summary.value("type").toString(),
                 escapedField(summary.value("left").toString()), escapedField(summary.value("right").toString()),
                 QString::number(summary.value("differences").toInt()));
}
ExecutionResult compareInputs(const Request &r)
{
    if (!r.scriptFile.isEmpty()) return errorResult(UsageError, QStringLiteral("Use Script::executeFile to run a script."), r.json);
    if (r.left.isEmpty() || r.right.isEmpty() || !r.base.isEmpty() || !r.output.isEmpty())
        return errorResult(UsageError, QStringLiteral("Headless comparison requires exactly two input paths; merge output is not supported."), r.json);
    if (!r.sessionType.isEmpty() && r.sessionType != "text" && r.sessionType != "folder")
        return errorResult(UsageError, QStringLiteral("Headless session type '%1' is not implemented; supported types are text and folder.").arg(r.sessionType), r.json);
    const QFileInfo left(r.left), right(r.right);
    if (!left.exists() || !right.exists())
        return errorResult(DataError, QStringLiteral("Input does not exist: %1").arg(!left.exists() ? r.left : r.right), r.json);
    if (left.isDir() != right.isDir())
        return errorResult(UsageError, QStringLiteral("Both inputs must be files or both must be directories."), r.json);
    if (r.sessionType == "folder" && !left.isDir())
        return errorResult(UsageError, QStringLiteral("The folder session type requires directories."), r.json);
    if (r.sessionType == "text" && left.isDir())
        return errorResult(UsageError, QStringLiteral("The text session type requires files."), r.json);
    if (!left.isDir() && (!left.isFile() || !right.isFile()))
        return errorResult(DataError, QStringLiteral("Only regular files and directories can be compared."), r.json);

    ExecutionResult result;
    result.summary = {{"type", left.isDir() ? "folder" : "text"},
                      {"left", left.absoluteFilePath()}, {"right", right.absoluteFilePath()}};
    int differences = 0, ignored = 0;
    if (left.isDir()) {
        if (r.textOptions.ignoreCase || r.textOptions.whitespace != Text::Whitespace::Exact
            || !r.textOptions.ignoreEol || r.textOptions.ignoreFinalNewline || !r.encoding.isEmpty())
            return errorResult(UsageError, QStringLiteral("Text comparison rules and encodings are not supported by headless folder comparison."), r.json);
        Folder::Options options;
        options.recursive = r.recursive;
        const Folder::Result compared = Folder::compare(r.left, r.right, options);
        if (!compared.error.isEmpty()) return errorResult(DataError, compared.error, r.json);
        if (compared.cancelled)
            return errorResult(DataError, QStringLiteral("Folder comparison was cancelled."), r.json);
        QJsonArray entries;
        for (const Folder::Entry &entry : compared.entries) {
            const bool excludedSubdirectory = !r.recursive && entry.isDirectory()
                    && entry.status == Folder::Status::Unknown;
            if (entry.status == Folder::Status::Error
                || (entry.status == Folder::Status::Unknown && !excludedSubdirectory))
                return errorResult(DataError, QStringLiteral("Unable to compare '%1': %2").arg(entry.relativePath, entry.explanation), r.json);
            if (entry.status != Folder::Status::Same && !excludedSubdirectory) ++differences;
            // Machine status is deliberately independent of translated labels.
            // 标识符取自主状态表（唯一的事实来源）：此前这里是一个自带
            // `default: break;` 的 switch，新增状态会静默输出空串，
            // 而那正是「机器契约里最贵的一种错」——调用方看到的是合法 JSON。
            const QString status = Folder::statusIdentifier(entry.status);
            if (status.isEmpty())
                return errorResult(DataError, QStringLiteral("Unknown folder status for '%1'.").arg(entry.relativePath), r.json);
            entries.append(QJsonObject{{"path", entry.relativePath}, {"status", status}, {"directory", entry.isDirectory()}});
        }
        if (!compared.complete && r.recursive)
            return errorResult(DataError, QStringLiteral("Folder comparison did not complete: %1").arg(compared.warnings.join(QStringLiteral("; "))), r.json);
        result.summary.insert(QStringLiteral("entries"), entries);
        result.summary.insert(QStringLiteral("recursive"), r.recursive);
    } else {
        if (!r.recursive) return errorResult(UsageError, QStringLiteral("--no-recursive applies only to folder comparisons."), r.json);
        Text::Document a, b; QString error;
        if (!Text::Document::load(r.left, &a, &error, r.encoding))
            return errorResult(DataError, QStringLiteral("Cannot read '%1': %2").arg(r.left, error), r.json);
        if (!Text::Document::load(r.right, &b, &error, r.encoding))
            return errorResult(DataError, QStringLiteral("Cannot read '%1': %2").arg(r.right, error), r.json);
        if (!a.canEdit() || !b.canEdit())
            return errorResult(DataError, QStringLiteral("Input is binary or could not be decoded losslessly; specify a valid --encoding for text."), r.json);
        const Text::Result compared = Text::compare(a.lines(), b.lines(), r.textOptions);
        // 数「一处改动」而不是「差异块」：TXT-005 起一处改动可能由相邻的若干块拼成，
        // 数块会让摘要里的数字比用户数得出来的多。与界面状态栏同源（Text::differenceRuns）。
        differences = Text::differenceRuns(compared).size();
        ignored = compared.ignoredBlocks;
        result.summary.insert(QStringLiteral("alignmentLimited"), compared.alignmentLimited);
        result.summary.insert(QStringLiteral("leftLines"), a.lines().size());
        result.summary.insert(QStringLiteral("rightLines"), b.lines().size());
    }
    result.exitCode = differences > 0 ? Different : Equal;
    result.summary.insert(QStringLiteral("status"), differences > 0 ? "different" : "equal");
    result.summary.insert(QStringLiteral("exitCode"), result.exitCode);
    result.summary.insert(QStringLiteral("differences"), differences);
    result.summary.insert(QStringLiteral("ignored"), ignored);
    result.standardOutput = r.json ? QString::fromUtf8(QJsonDocument(result.summary).toJson(QJsonDocument::Compact)) + '\n'
                                   : summaryText(result.summary);
    return result;
}
}

ExecutionResult errorResult(int code, const QString &message, bool json)
{
    ExecutionResult result;
    result.exitCode = code;
    result.summary = {{"status", "error"}, {"exitCode", code}, {"error", message}};
    result.standardError = QStringLiteral("error\t%1\t%2\n").arg(code).arg(escapedField(message));
    if (json) result.standardOutput = QString::fromUtf8(QJsonDocument(result.summary).toJson(QJsonDocument::Compact)) + '\n';
    return result;
}

bool isSafeOutputPath(const QString &path, const QStringList &protectedPaths, QString *error)
{
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (path.isEmpty()) return fail(QStringLiteral("Output path is empty."));
    const QString destination = resolvedPath(path);
    if (destination.isEmpty()) return fail(QStringLiteral("Cannot resolve output path '%1'.").arg(path));
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    for (const QString &input : protectedPaths) {
        if (input.isEmpty()) continue;
        const QString source = resolvedPath(input);
        if (source.isEmpty()) return fail(QStringLiteral("Cannot resolve protected input '%1'.").arg(input));
        const QString prefix = source.endsWith('/') ? source : source + '/';
        if (destination.compare(source, sensitivity) == 0 || sameFileObject(path, input)
            || (QFileInfo(input).isDir() && destination.startsWith(prefix, sensitivity)))
            return fail(QStringLiteral("Refusing to write '%1': it overlaps protected input '%2'.").arg(path, input));
    }
    if (QFileInfo(path).exists() && !QFileInfo(path).isFile())
        return fail(QStringLiteral("Output is not a regular file: %1").arg(path));
    if (error) error->clear();
    return true;
}

bool writeReport(const QString &path, const QString &format, const ExecutionResult &result,
                 const QStringList &protectedPaths, QString *error)
{
    if (!isSafeOutputPath(path, protectedPaths, error)) return false;
    const QJsonObject &summary = result.summary;
    QByteArray bytes;
    if (format == "json") bytes = QJsonDocument(summary).toJson(QJsonDocument::Indented);
    else if (format == "txt") bytes = (result.ok() ? summaryText(summary) : result.standardError).toUtf8();
    else if (format == "csv") {
        QStringList fields;
        for (const QString &key : QStringList{"status", "type", "left", "right", "differences", "exitCode"})
            fields.append(csvField(summary.value(key).toVariant().toString()));
        bytes = (QStringLiteral("status,type,left,right,differences,exitCode\r\n") + fields.join(',') + QStringLiteral("\r\n")).toUtf8();
    } else if (format == "html") {
        QString html = QStringLiteral("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>LqCompare summary</title>"
                                      "<style>body{font:16px sans-serif;max-width:70em;margin:2em auto}th,td{text-align:left;padding:.5em;border:1px solid #ddd}table{border-collapse:collapse}</style>"
                                      "<h1>LqCompare summary</h1><table>");
        for (const QString &key : QStringList{"status", "type", "left", "right", "differences", "ignored", "exitCode"})
            html += QStringLiteral("<tr><th>%1</th><td>%2</td></tr>").arg(key.toHtmlEscaped(), summary.value(key).toVariant().toString().toHtmlEscaped());
        html += QStringLiteral("</table></html>\n");
        bytes = html.toUtf8();
    } else {
        if (error) *error = QStringLiteral("Unsupported summary format '%1'.").arg(format);
        return false;
    }
    return writeBytes(path, bytes, error);
}

ExecutionResult execute(const Request &r, const QString &version, const QStringList &protectedPaths)
{
    try {
        if (r.help) {
            ExecutionResult result; result.standardOutput = helpText(r.helpTopic);
            if (result.standardOutput.isEmpty()) return errorResult(UsageError, QStringLiteral("Unknown help topic."), r.json);
            return result;
        }
        if (r.version) { ExecutionResult result; result.standardOutput = QStringLiteral("LqCompare %1\n").arg(version); return result; }
        if (r.listSessionTypes) { ExecutionResult result; result.standardOutput = sessionTypesText(); return result; }
        if (r.printExitCodes) { ExecutionResult result; result.standardOutput = exitCodesText(); return result; }
        QStringList protectedInputs = protectedPaths + inputPaths(r);
        if (!r.output.isEmpty()) protectedInputs.append(r.output);
        QString error;
        if (r.reportFormat.isEmpty() != r.reportFile.isEmpty())
            return errorResult(UsageError, QStringLiteral("--report and --report-file must be supplied together."), r.json);
        if (!r.reportFile.isEmpty() && !QStringList{"txt", "csv", "json", "html"}.contains(r.reportFormat))
            return errorResult(UsageError, QStringLiteral("Unsupported report format."), r.json);
        if (!r.reportFile.isEmpty() && !isSafeOutputPath(r.reportFile, protectedInputs, &error))
            return errorResult(DataError, error, r.json);
        if (!r.logFile.isEmpty() && !r.noLogFile) {
            QStringList logProtected = protectedInputs;
            if (!r.reportFile.isEmpty()) logProtected.append(r.reportFile);
            if (!isSafeOutputPath(r.logFile, logProtected, &error)) return errorResult(DataError, error, r.json);
        }
        ExecutionResult result = compareInputs(r);
        if (result.ok() && !r.reportFile.isEmpty() && (!r.reportOnDiffOnly || result.exitCode == Different)) {
            if (!writeReport(r.reportFile, r.reportFormat, result, protectedInputs, &error))
                result = errorResult(DataError, QStringLiteral("Cannot write report '%1': %2").arg(r.reportFile, error), r.json);
        }
        if (!r.logFile.isEmpty() && !r.noLogFile) {
            const QByteArray bytes = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + '\t'
                                      + QString::number(result.exitCode) + '\n'
                                      + result.standardOutput + result.standardError).toUtf8();
            if (!writeBytes(r.logFile, bytes, &error))
                return errorResult(DataError, QStringLiteral("Cannot write log '%1': %2").arg(r.logFile, error), r.json);
        }
        return result;
    } catch (const std::exception &error) {
        return errorResult(InternalError, QStringLiteral("Internal comparison failure: %1").arg(QString::fromUtf8(error.what())), r.json);
    } catch (...) {
        return errorResult(InternalError, QStringLiteral("Unknown internal comparison failure."), r.json);
    }
}

} }
