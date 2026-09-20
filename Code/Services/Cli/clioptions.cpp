#include "clioptions.h"
#include "sessiontype.h"
#include <QSet>
#include <QTextCodec>
#include <algorithm>

namespace LqCompare { namespace Cli {
namespace {
Platform actualPlatform(Platform platform)
{
    if (platform != Platform::Native) return platform;
#ifdef Q_OS_WIN
    return Platform::Windows;
#else
    return Platform::Posix;
#endif
}
const Option *findOption(const QString &name, Platform platform)
{
    for (const Option &option : options()) {
        if (name == QStringLiteral("--") + option.name) return &option;
        for (const QString &alias : option.aliases) {
            if (alias.startsWith('/') && alias != QStringLiteral("/?")
                && platform != Platform::Windows) continue;
            if (alias.startsWith('/') ? alias.compare(name, Qt::CaseInsensitive) == 0
                                      : alias == name) return &option;
        }
    }
    return nullptr;
}
int editDistance(const QString &a, const QString &b)
{
    QVector<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int previous = row[0]; row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int old = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1,
                               previous + (a[i - 1] == b[j - 1] ? 0 : 1)});
            previous = old;
        }
    }
    return row.last();
}
QString unknownOption(const QString &name)
{
    int best = 100000; QString suggestion;
    for (const Option &option : options()) {
        const QString candidate = QStringLiteral("--") + option.name;
        const int distance = editDistance(name, candidate);
        if (distance < best) { best = distance; suggestion = candidate; }
    }
    return QStringLiteral("Unknown option '%1'. Did you mean '%2'? Use -- before paths beginning with '-'.")
            .arg(name, suggestion);
}
QString normalizeType(QString value, Platform platform, QString *error)
{
    value = value.trimmed();
    static const QMap<QString, QString> aliases = {
        {QStringLiteral("merge"), QStringLiteral("text-merge")},
        {QStringLiteral("edit"), QStringLiteral("text-edit")},
        {QStringLiteral("patch"), QStringLiteral("text-patch")},
        {QStringLiteral("directory"), QStringLiteral("folder")},
        {QStringLiteral("image"), QStringLiteral("picture")},
        {QStringLiteral("csv"), QStringLiteral("table")}
    };
    value = aliases.value(value.toLower(), value);
    for (const SessionType &type : builtInSessionTypes()) {
        if (type.id.compare(value, Qt::CaseInsensitive) != 0
            && type.englishName.compare(value, Qt::CaseInsensitive) != 0
            && type.displayName.compare(value, Qt::CaseInsensitive) != 0) continue;
        if (type.platforms == SessionPlatformScope::WindowsOnly && platform != Platform::Windows) {
            *error = QStringLiteral("Session type '%1' is available only on Windows.").arg(type.id);
            return {};
        }
        return type.id;
    }
    *error = QStringLiteral("Unknown session type '%1'. Use --list-session-types.").arg(value);
    return {};
}
}

const QVector<Option> &options()
{
    static const QVector<Option> table = {
        {"help", {"-h", "/?"}, "[option]", "Discovery", "Show this help; --help=option shows one option."},
        {"version", {"-v"}, {}, "Discovery", "Print the application version."},
        {"list-session-types", {"--help-session-types"}, {}, "Discovery", "List registry types and headless availability."},
        {"print-exit-codes", {}, {}, "Discovery", "Print the stable comparison and mergetool exit-code contract."},
        {"left", {}, "path", "Inputs", "Bind the left input explicitly."},
        {"right", {}, "path", "Inputs", "Bind the right input explicitly."},
        {"base", {}, "path", "Inputs", "Bind the common ancestor explicitly."},
        {"output", {}, "path", "Inputs", "Bind the merge destination; headless comparison never writes it."},
        {"type", {"/fv"}, "type", "Inputs", "Select a registry ID or name, for example 'Text Compare'."},
        {"silent", {"/silent"}, {}, "Execution", "Compare without opening a window."},
        {"quick-compare", {"--qc", "/qc"}, {}, "Execution", "Run a headless content comparison and return its result."},
        {"read-only", {"--readonly", "/readonly"}, {}, "Execution", "Mark both inputs read-only for interactive opening."},
        {"left-read-only", {"--leftreadonly", "/leftreadonly"}, {}, "Execution", "Mark the left input read-only."},
        {"right-read-only", {"--rightreadonly", "/rightreadonly"}, {}, "Execution", "Mark the right input read-only."},
        {"new-instance", {}, {}, "Execution", "Open an independent GUI instance (handled by the application)."},
        {"wait", {}, {}, "Execution", "Wait for GUI completion (handled by the application)."},
        {"ignore-case", {}, {}, "Comparison", "Ignore text case for this invocation."},
        {"ignore-whitespace", {}, {}, "Comparison", "Ignore all whitespace in text for this invocation."},
        {"ignore-eol", {}, {}, "Comparison", "Ignore CR/LF differences (the default)."},
        {"exact-eol", {}, {}, "Comparison", "Include CR/LF differences."},
        {"ignore-final-newline", {}, {}, "Comparison", "Ignore the presence of the final newline."},
        {"encoding", {}, "codec", "Comparison", "Decode BOM-less text with this Qt codec; default UTF-8."},
        {"no-recursive", {}, {}, "Comparison", "Compare immediate folder children only."},
        {"json", {}, {}, "Output", "Emit a UTF-8 JSON comparison summary."},
        {"log", {}, "file", "Output", "Write an invocation log atomically to an explicit file."},
        {"log-level", {}, "level", "Output", "Log level: error, warning, info, debug, trace."},
        {"no-log-file", {}, {}, "Output", "Disable file logging (including application default logging)."},
        {"report", {}, "format", "Output", "Generate a comparison summary: txt, csv, json or html."},
        {"report-file", {}, "file", "Output", "Summary destination, outside all input paths."},
        {"report-on-diff-only", {}, {}, "Output", "Write the summary only when differences exist."},
        {"script", {}, "file", "Script", "Execute a script; @file is an alias. Relative paths use its directory."},
        {"script-arg", {}, "name=value", "Script", "Pass a script variable; names must be unique.", true},
        {"continue-on-error", {}, {}, "Script", "Continue after script execution errors and return the most severe code."}
    };
    return table;
}

ParseResult parse(const QStringList &arguments, Platform platform)
{
    platform = actualPlatform(platform);
    ParseResult result;
    Request &r = result.request;
    QSet<QString> seen;
    QStringList paths;
    bool positional = false;
    auto fail = [&](const QString &error) { result.error = error; return result; };
    for (int i = 0; i < arguments.size(); ++i) {
        const QString argument = arguments[i];
        if (argument.isEmpty()) return fail(QStringLiteral("An empty path or argument is not allowed."));
        if (!positional && argument == QStringLiteral("--")) { positional = true; continue; }
        if (!positional && argument.startsWith('@')) {
            if (seen.contains(QStringLiteral("script"))) return fail(QStringLiteral("Duplicate option --script."));
            if (argument.size() == 1) return fail(QStringLiteral("Missing script path after '@'."));
            seen.insert(QStringLiteral("script")); r.scriptFile = argument.mid(1); continue;
        }
        const int equals = argument.indexOf('=');
        const QString name = equals < 0 ? argument : argument.left(equals);
        const Option *option = positional ? nullptr : findOption(name, platform);
        if (!option) {
            if (!positional && argument.startsWith('-') && argument != QStringLiteral("-"))
                return fail(unknownOption(name));
            paths.append(argument); continue;
        }
        const QString key = option->name;
        if (seen.contains(key) && !option->repeatable)
            return fail(QStringLiteral("Duplicate option --%1.").arg(key));
        seen.insert(key);
        QString value;
        if (equals >= 0) value = argument.mid(equals + 1);
        const bool takesValue = !option->valueName.isEmpty();
        if (takesValue && key != QStringLiteral("help")) {
            if (equals < 0) {
                if (i + 1 >= arguments.size() || arguments[i + 1].startsWith(QStringLiteral("--")))
                    return fail(QStringLiteral("Missing value for --%1.").arg(key));
                const QString next = arguments[i + 1];
                if (findOption(next.section('=', 0, 0), platform))
                    return fail(QStringLiteral("Missing value for --%1.").arg(key));
                value = arguments[++i];
            }
            if (value.isEmpty()) return fail(QStringLiteral("Empty value for --%1.").arg(key));
        } else if (!takesValue && equals >= 0) {
            return fail(QStringLiteral("Option --%1 does not accept a value.").arg(key));
        }
        if (key == "help") { r.help = true; r.helpTopic = value; }
        else if (key == "version") r.version = true;
        else if (key == "list-session-types") r.listSessionTypes = true;
        else if (key == "print-exit-codes") r.printExitCodes = true;
        else if (key == "left") r.left = value;
        else if (key == "right") r.right = value;
        else if (key == "base") r.base = value;
        else if (key == "output") r.output = value;
        else if (key == "type") {
            r.sessionType = normalizeType(value, platform, &result.error);
            if (!result.ok()) return result;
        }
        else if (key == "silent") r.silent = true;
        else if (key == "quick-compare") r.quickCompare = true;
        else if (key == "read-only") r.leftReadOnly = r.rightReadOnly = true;
        else if (key == "left-read-only") r.leftReadOnly = true;
        else if (key == "right-read-only") r.rightReadOnly = true;
        else if (key == "new-instance") r.newInstance = true;
        else if (key == "wait") r.wait = true;
        else if (key == "ignore-case") r.textOptions.ignoreCase = true;
        else if (key == "ignore-whitespace") r.textOptions.whitespace = Text::Whitespace::IgnoreAll;
        else if (key == "ignore-eol") r.textOptions.ignoreEol = true;
        else if (key == "exact-eol") r.textOptions.ignoreEol = false;
        else if (key == "ignore-final-newline") r.textOptions.ignoreFinalNewline = true;
        else if (key == "encoding") {
            r.encoding = value.toUtf8();
            if (!QTextCodec::codecForName(r.encoding)) return fail(QStringLiteral("Unknown encoding '%1'.").arg(value));
        }
        else if (key == "no-recursive") r.recursive = false;
        else if (key == "json") r.json = true;
        else if (key == "log") r.logFile = value;
        else if (key == "no-log-file") r.noLogFile = true;
        else if (key == "log-level") {
            if (!QStringList{"error", "warning", "info", "debug", "trace"}.contains(value))
                return fail(QStringLiteral("Unknown log level '%1'.").arg(value));
            r.logLevel = value;
        }
        else if (key == "report") {
            r.reportFormat = value.toLower();
            if (!QStringList{"txt", "csv", "json", "html"}.contains(r.reportFormat))
                return fail(QStringLiteral("Unsupported summary format '%1'; use txt, csv, json or html.").arg(value));
        }
        else if (key == "report-file") r.reportFile = value;
        else if (key == "report-on-diff-only") r.reportOnDiffOnly = true;
        else if (key == "script") r.scriptFile = value;
        else if (key == "continue-on-error") r.continueOnError = true;
        else if (key == "script-arg") {
            const int separator = value.indexOf('=');
            if (separator <= 0) return fail(QStringLiteral("--script-arg requires name=value."));
            const QString variable = value.left(separator);
            for (const QChar ch : variable) {
                if (!ch.isLetterOrNumber() && ch != '_') return fail(QStringLiteral("Invalid script variable name '%1'.").arg(variable));
            }
            if (variable[0].isDigit()) return fail(QStringLiteral("Script variable names must not start with a digit."));
            if (r.scriptArguments.contains(variable)) return fail(QStringLiteral("Duplicate script variable '%1'.").arg(variable));
            r.scriptArguments.insert(variable, value.mid(separator + 1));
        }
    }
    if (seen.contains("ignore-eol") && seen.contains("exact-eol"))
        return fail(QStringLiteral("--ignore-eol and --exact-eol conflict."));
    if (r.noLogFile && !r.logFile.isEmpty()) return fail(QStringLiteral("--log and --no-log-file conflict."));
    if (!paths.isEmpty() && (seen.contains("left") || seen.contains("right")
                            || seen.contains("base") || seen.contains("output")))
        return fail(QStringLiteral("Do not mix named input/output paths with positional paths."));
    if (paths.size() > 4) return fail(QStringLiteral("Expected at most four paths, received %1.").arg(paths.size()));
    if (!paths.isEmpty()) r.left = paths.value(0);
    if (paths.size() > 1) r.right = paths[1];
    if (paths.size() > 2) r.base = paths[2];
    if (paths.size() > 3) r.output = paths[3];
    if ((!r.right.isEmpty() && r.left.isEmpty()) || (!r.base.isEmpty() && r.right.isEmpty())
        || (!r.output.isEmpty() && r.base.isEmpty()))
        return fail(QStringLiteral("Named paths require --left, then --right, then --base before --output."));
    if (r.reportFormat.isEmpty() != r.reportFile.isEmpty())
        return fail(QStringLiteral("--report and --report-file must be supplied together."));
    if (r.reportOnDiffOnly && r.reportFile.isEmpty())
        return fail(QStringLiteral("--report-on-diff-only requires a report destination."));
    if (r.scriptFile.isEmpty() && (!r.scriptArguments.isEmpty() || r.continueOnError))
        return fail(QStringLiteral("Script arguments and --continue-on-error require --script."));
    if (!r.scriptFile.isEmpty() && (!r.left.isEmpty() || !r.reportFile.isEmpty()))
        return fail(QStringLiteral("A script cannot be combined with CLI input paths or report options; put load/report commands in the script."));
    const int count = inputPaths(r).size();
    if (!r.sessionType.isEmpty() && count > 0) {
        const bool merge = r.sessionType.endsWith(QStringLiteral("-merge"));
        const bool single = r.sessionType == "text-edit" || r.sessionType == "text-patch";
        if ((merge && count != 3) || (single && count != 1) || (!merge && !single && count > 2))
            return fail(QStringLiteral("Session type '%1' does not accept %2 input paths.").arg(r.sessionType).arg(count));
    }
    if (r.help && !r.helpTopic.isEmpty() && helpText(r.helpTopic).isEmpty())
        return fail(QStringLiteral("Unknown help topic '%1'.").arg(r.helpTopic));
    return result;
}

QString helpText(const QString &topic)
{
    QString text;
    if (topic.isEmpty()) {
        text = QStringLiteral("LqCompare — file and folder comparison\nUsage: LqCompare [options] [left [right [base [output]]]]\n"
                              "Native order: left, right, ancestor, output. Use named paths for Git tools.\n"
                              "Use -- to stop option parsing. Shell quoting is handled by your shell.\n"
                              "Headless comparison supports two text files or two folders.\n"
                              "Reports currently contain a comparison summary. All input paths are protected from writes.\n"
                              "Windows accepts the listed / aliases; POSIX absolute paths remain paths (except /?).\n");
    }
    QString group;
    QString wanted = topic;
    if (wanted.startsWith("--")) wanted.remove(0, 2);
    for (const Option &option : options()) {
        if (!wanted.isEmpty() && option.name != wanted && !option.aliases.contains(topic)) continue;
        if (group != option.group) { group = option.group; text += QStringLiteral("\n%1:\n").arg(group); }
        text += QStringLiteral("  --%1").arg(option.name);
        if (!option.valueName.isEmpty()) text += QStringLiteral("=%1").arg(option.valueName);
        if (!option.aliases.isEmpty()) text += QStringLiteral(" (%1)").arg(option.aliases.join(", "));
        text += QStringLiteral("\n      %1\n").arg(option.description);
    }
    if (topic.isEmpty()) text += QStringLiteral("\nExamples:\n  LqCompare --silent left.txt right.txt\n"
        "  LqCompare --type=text-merge --left=local --right=remote --base=ancestor --output=merged\n"
        "  LqCompare --silent --json left-folder right-folder\n  LqCompare --script=compare.lqs\n");
    return text;
}
QString exitCodesText()
{
    return QStringLiteral("0\tNo differences; mergetool: resolved and saved\n"
                          "1\tDifferences; mergetool: unresolved or cancelled\n"
                          "2\tParameter or usage error\n3\tCannot read/write data source or output\n4\tInternal error\n");
}
QString sessionTypesText()
{
    QString text = QStringLiteral("id\tname\theadless\tplatform\n");
    for (const SessionType &type : builtInSessionTypes())
        text += type.id + '\t' + type.englishName + '\t'
                + (type.id == "text" || type.id == "folder" ? QStringLiteral("supported") : QStringLiteral("not implemented"))
                + '\t' + (currentPlatformAllows(type.platforms) ? QStringLiteral("available") : QStringLiteral("Windows only")) + '\n';
    return text;
}
bool requiresHeadless(const Request &r)
{
    return r.help || r.version || r.listSessionTypes || r.printExitCodes || r.silent
            || r.quickCompare || r.json || !r.reportFile.isEmpty() || !r.scriptFile.isEmpty();
}
QStringList inputPaths(const Request &r)
{
    QStringList paths;
    if (!r.left.isEmpty()) paths.append(r.left);
    if (!r.right.isEmpty()) paths.append(r.right);
    if (!r.base.isEmpty()) paths.append(r.base);
    return paths;
}

} }
