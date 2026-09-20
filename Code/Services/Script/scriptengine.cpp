#include "scriptengine.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextCodec>
#include <algorithm>
#include <exception>

namespace LqCompare { namespace Script {
namespace {

QString field(QString value)
{
    return value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
        .replace(QLatin1Char('\t'), QStringLiteral("\\t"))
        .replace(QLatin1Char('\r'), QStringLiteral("\\r"))
        .replace(QLatin1Char('\n'), QStringLiteral("\\n"));
}

QString absolutePath(const QString &path, const QDir &directory)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(directory.absoluteFilePath(path));
}

QString optionKey(QString name)
{
    while (name.startsWith(QLatin1Char('-')) || name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    return name.toLower();
}

// A token remains a single argument after variable expansion. A variable cannot
// inject an additional command or option through whitespace in its value.
bool tokenize(const QString &line, QStringList *tokens, QString *error)
{
    QString token;
    QChar quote;
    bool started = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch.isNull()) {
            *error = QStringLiteral("NUL is not allowed in a script.");
            return false;
        }
        if (ch == QLatin1Char('\\')) {
            int end = i;
            while (end < line.size() && line.at(end) == QLatin1Char('\\')) ++end;
            const int count = end - i;
            const QChar next = end < line.size() ? line.at(end) : QChar();
            const bool quotes = (!quote.isNull() && next == quote)
                || (quote.isNull() && (next == QLatin1Char('"') || next == QLatin1Char('\'')));
            // Only backslashes immediately before a syntactic quote escape.
            // 2n closes/opens the quote after n literal slashes; 2n+1 escapes
            // the quote. Other runs stay verbatim, preserving UNC prefixes.
            token += QString(quotes ? count / 2 : count, QLatin1Char('\\'));
            started = true;
            if (quotes) {
                if (count % 2) token += next;
                else quote = quote.isNull() ? next : QChar();
                i = end;
            } else {
                i = end - 1;
            }
            continue;
        }
        if (!quote.isNull()) {
            if (ch == quote)
                quote = QChar();
            else
                token += ch;
            continue;
        }
        if (ch == QLatin1Char('#') && !started)
            break;
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            started = true;
        } else if (ch.isSpace()) {
            if (started) {
                tokens->append(token);
                token.clear();
                started = false;
            }
        } else {
            token += ch;
            started = true;
        }
    }
    if (!quote.isNull()) {
        *error = QStringLiteral("Unterminated quoted argument.");
        return false;
    }
    if (started)
        tokens->append(token);
    return true;
}

bool expand(const QString &source, const QMap<QString, QString> &variables,
            QString *result, QStringList *errors)
{
    for (int i = 0; i < source.size(); ++i) {
        const QChar ch = source.at(i);
        if ((ch == QLatin1Char('$') || ch == QLatin1Char('%'))
            && i + 1 < source.size() && source.at(i + 1) == ch) {
            *result += ch;
            ++i;
        } else if (ch == QLatin1Char('$') && i + 1 < source.size()
                   && source.at(i + 1) == QLatin1Char('{')) {
            const int end = source.indexOf(QLatin1Char('}'), i + 2);
            if (end < 0) {
                errors->append(QStringLiteral("Unterminated variable reference."));
                return false;
            }
            const QString name = source.mid(i + 2, end - i - 2);
            const auto value = variables.constFind(name);
            if (value == variables.cend())
                errors->append(QStringLiteral("Undefined variable: %1").arg(name));
            else
                *result += value.value();
            i = end;
        } else {
            *result += ch;
        }
    }
    return errors->isEmpty();
}

// The CLI table determines option identity and arity. The interpreter only
// determines which options make sense inside its load/compare commands.
QSet<QString> explicitOptions(const QStringList &arguments)
{
    QSet<QString> result;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString token = arguments.at(i);
        if (token == QStringLiteral("--"))
            break;
        if (!token.startsWith(QLatin1Char('-')) && !token.startsWith(QLatin1Char('/')))
            continue;
#ifndef Q_OS_WIN
        if (token.startsWith(QLatin1Char('/')) && token != QStringLiteral("/?"))
            continue;
#endif
        const QString key = optionKey(token.section(QLatin1Char('='), 0, 0));
        for (const Cli::Option &option : Cli::options()) {
            QStringList names = option.aliases;
            names.prepend(option.name);
            bool matched = false;
            for (const QString &name : names)
                matched = matched || optionKey(name) == key;
            if (matched) {
                result.insert(optionKey(option.name));
                if (!option.valueName.isEmpty() && !token.contains(QLatin1Char('=')))
                    ++i;
                break;
            }
        }
    }
    return result;
}

bool containsAny(const QSet<QString> &keys, std::initializer_list<const char *> candidates)
{
    for (const char *candidate : candidates)
        if (keys.contains(QLatin1String(candidate)))
            return true;
    return false;
}

Cli::Request comparisonDefaults(Cli::Request request)
{
    request.help = false;
    request.version = false;
    request.listSessionTypes = false;
    request.printExitCodes = false;
    request.scriptFile.clear();
    request.scriptArguments.clear();
    request.logFile.clear();
    request.reportFile.clear();
    request.reportFormat.clear();
    request.reportOnDiffOnly = false;
    request.silent = true;
    request.continueOnError = false;
    request.wait = false;
    request.newInstance = false;
    request.json = false;
    return request;
}

bool prepareComparison(const QStringList &args, const Cli::Request &current,
                       const QDir &directory, Cli::Request *request, QString *error)
{
    const Cli::ParseResult parsed = Cli::parse(args);
    if (!parsed.ok()) {
        *error = parsed.error;
        return false;
    }
    const QSet<QString> keys = explicitOptions(args);
    if (!parsed.request.scriptFile.isEmpty()) {
        *error = QStringLiteral("Nested script execution is not supported in load/compare.");
        return false;
    }
    static const QSet<QString> allowed {
        QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("base"), QStringLiteral("output"),
        QStringLiteral("type"), QStringLiteral("session-type"), QStringLiteral("fv"),
        QStringLiteral("ignore-case"), QStringLiteral("ignore-whitespace"), QStringLiteral("ignore-space"),
        QStringLiteral("ignore-space-change"), QStringLiteral("ignore-all-space"), QStringLiteral("whitespace"),
        QStringLiteral("ignore-eol"), QStringLiteral("exact-eol"), QStringLiteral("compare-eol"), QStringLiteral("ignore-final-newline"),
        QStringLiteral("encoding"), QStringLiteral("recursive"), QStringLiteral("no-recursive"),
        QStringLiteral("left-readonly"), QStringLiteral("right-readonly"), QStringLiteral("readonly"),
        QStringLiteral("left-read-only"), QStringLiteral("right-read-only"), QStringLiteral("read-only"),
        QStringLiteral("leftreadonly"), QStringLiteral("rightreadonly"),
        QStringLiteral("silent"), QStringLiteral("qc"), QStringLiteral("quick-compare")
    };
    for (const QString &key : keys) {
        if (!allowed.contains(key)) {
            *error = QStringLiteral("Option --%1 is not supported in load/compare; use the script runner or report command.").arg(key);
            return false;
        }
    }
    *request = current;
    const Cli::Request &value = parsed.request;
    const bool hasPaths = !Cli::inputPaths(value).isEmpty() || !value.output.isEmpty();
    if (hasPaths) {
        const bool named = containsAny(keys, {"left", "right", "base", "output"});
        if (!named) {
            request->left.clear(); request->right.clear(); request->base.clear(); request->output.clear();
        }
        if (!value.left.isEmpty()) request->left = absolutePath(value.left, directory);
        if (!value.right.isEmpty()) request->right = absolutePath(value.right, directory);
        if (!value.base.isEmpty()) request->base = absolutePath(value.base, directory);
        if (!value.output.isEmpty()) request->output = absolutePath(value.output, directory);
    }
    if (containsAny(keys, {"type", "session-type", "fv"})) request->sessionType = value.sessionType;
    if (keys.contains(QStringLiteral("ignore-case"))) request->textOptions.ignoreCase = value.textOptions.ignoreCase;
    if (containsAny(keys, {"ignore-whitespace", "ignore-space", "ignore-space-change", "ignore-all-space", "whitespace"}))
        request->textOptions.whitespace = value.textOptions.whitespace;
    if (containsAny(keys, {"ignore-eol", "exact-eol", "compare-eol"})) request->textOptions.ignoreEol = value.textOptions.ignoreEol;
    if (keys.contains(QStringLiteral("ignore-final-newline"))) request->textOptions.ignoreFinalNewline = value.textOptions.ignoreFinalNewline;
    if (keys.contains(QStringLiteral("encoding"))) request->encoding = value.encoding;
    if (containsAny(keys, {"recursive", "no-recursive"})) request->recursive = value.recursive;
    if (containsAny(keys, {"left-readonly", "left-read-only", "leftreadonly", "readonly", "read-only"})) request->leftReadOnly = value.leftReadOnly;
    if (containsAny(keys, {"right-readonly", "right-read-only", "rightreadonly", "readonly", "read-only"})) request->rightReadOnly = value.rightReadOnly;
    if (containsAny(keys, {"qc", "quick-compare"})) request->quickCompare = value.quickCompare;
    if (request->left.isEmpty() || request->right.isEmpty() || !request->base.isEmpty() || !request->output.isEmpty()) {
        *error = QStringLiteral("Script load/compare requires exactly two input paths; merge/output is not supported.");
        return false;
    }
    if (!request->sessionType.isEmpty() && request->sessionType != QStringLiteral("text")
        && request->sessionType != QStringLiteral("folder")) {
        *error = QStringLiteral("Script comparison does not support session type '%1'; use text or folder.").arg(request->sessionType);
        return false;
    }
    return true;
}

bool prepareReport(const QStringList &args, const QDir &directory, Command *command, QString *error)
{
    QString format;
    QString path;
    if (args.size() == 2 && !args.at(0).startsWith(QLatin1Char('-'))) {
        format = args.at(0).toLower();
        path = args.at(1);
    } else if (args.size() == 1 && !args.at(0).startsWith(QLatin1Char('-'))) {
        format = QStringLiteral("txt");
        path = args.at(0);
    } else {
        const Cli::ParseResult parsed = Cli::parse(args);
        if (!parsed.ok()) {
            *error = parsed.error;
            return false;
        }
        if (!parsed.request.scriptFile.isEmpty()) {
            *error = QStringLiteral("Nested scripts are not supported in report.");
            return false;
        }
        const QSet<QString> keys = explicitOptions(args);
        for (const QString &key : keys) {
            if (key != QStringLiteral("report") && key != QStringLiteral("report-file")
                && key != QStringLiteral("report-on-diff-only")) {
                *error = QStringLiteral("Option --%1 is not supported in report.").arg(key);
                return false;
            }
        }
        if (!Cli::inputPaths(parsed.request).isEmpty()) {
            *error = QStringLiteral("Use report <format> <path> or --report=<format> --report-file=<path>.");
            return false;
        }
        path = parsed.request.reportFile;
        format = parsed.request.reportFormat;
        command->reportOnDiffOnly = parsed.request.reportOnDiffOnly;
    }
    if (path.isEmpty() || !QStringList({QStringLiteral("txt"), QStringLiteral("csv"), QStringLiteral("json"), QStringLiteral("html")}).contains(format)) {
        *error = QStringLiteral("Report requires a path and a supported format: txt, csv, json, html.");
        return false;
    }
    command->reportPath = absolutePath(path, directory);
    command->reportFormat = format;
    return true;
}

bool writeLog(const QString &path, const QString &contents, const QStringList &protectedPaths, QString *error)
{
    if (!Cli::isSafeOutputPath(path, protectedPaths, error))
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    const QByteArray bytes = contents.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QString diagnosticText(const QString &script, const Diagnostic &diagnostic, int exitCode)
{
    return QStringLiteral("ERROR\t%1\t%2\t%3\t%4\t%5\n")
        .arg(exitCode).arg(field(script)).arg(diagnostic.line)
        .arg(field(diagnostic.command), field(diagnostic.message));
}

} // namespace

const QVector<CommandDefinition> &commands()
{
    static const QVector<CommandDefinition> table {
        {QStringLiteral("load"), QStringLiteral("load [comparison options] <left> <right>"),
         QStringLiteral("Select two text files or directories. Options use the CLI parameter table.")},
        {QStringLiteral("compare"), QStringLiteral("compare [comparison options] [<left> <right>]"),
         QStringLiteral("Compare the loaded pair, or a new pair; retain options until overridden.")},
        {QStringLiteral("report"), QStringLiteral("report <txt|csv|json|html> <path>"),
         QStringLiteral("Write the latest successful comparison; also accepts --report/--report-file/--report-on-diff-only.")},
        {QStringLiteral("set"), QStringLiteral("set <name>=<value> | set <name> <value>"),
         QStringLiteral("Set a variable for later ${name} references. $$ and %% produce literal characters.")},
        {QStringLiteral("print"), QStringLiteral("print <text>..."),
         QStringLiteral("Write a PRINT record to standard output; JSON mode stores it in messages.")},
        {QStringLiteral("log"), QStringLiteral("log <text>..."),
         QStringLiteral("Write a LOG record to standard output and to the optional script log.")}
    };
    return table;
}

QString helpText()
{
    QString text = QStringLiteral("Script commands (one per line; # comments and single/double quotes):\n");
    for (const CommandDefinition &command : commands())
        text += command.usage + QLatin1Char('\n') + QStringLiteral("  ") + command.description + QLatin1Char('\n');
    text += QStringLiteral("Paths are relative to the script directory. Built-ins: ${script-dir}, ${cwd}, ${version}, ${timestamp}.\n"
                           "Before a matching quote, double trailing backslashes; an odd final backslash escapes the quote. Other backslashes (including UNC prefixes) stay literal.\n"
                           "All syntax and variable errors are rejected before execution. Runtime errors stop unless --continue-on-error is set.\n"
                           "copy/move/delete/rename/mkdir/sync/mirror/applypatch/select and control flow are not implemented.\n");
    return text;
}

ParseResult parse(const QString &source, const QString &scriptFile, const Cli::Request &defaults, const QString &version)
{
    ParseResult result;
    result.program.scriptFile = QFileInfo(scriptFile).absoluteFilePath();
    result.program.protectedPaths.append(result.program.scriptFile);
    const QDir directory = QFileInfo(result.program.scriptFile).absoluteDir();
    Cli::Request current = comparisonDefaults(defaults);
    current.left = absolutePath(current.left, directory);
    current.right = absolutePath(current.right, directory);
    current.base = absolutePath(current.base, directory);
    current.output = absolutePath(current.output, directory);
    result.program.protectedPaths += Cli::inputPaths(current);
    QMap<QString, QString> variables = defaults.scriptArguments;
    const QMap<QString, QString> builtins {
        {QStringLiteral("script-dir"), directory.absolutePath()},
        {QStringLiteral("cwd"), QDir::currentPath()},
        {QStringLiteral("version"), version},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"))}
    };
    const QRegularExpression variableName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]*$"));
    auto error = [&](int line, const QString &command, const QString &message) {
        result.diagnostics.append({line, command, message});
    };
    for (auto it = variables.cbegin(); it != variables.cend(); ++it) {
        if (!variableName.match(it.key()).hasMatch())
            error(0, QStringLiteral("script-arg"), QStringLiteral("Invalid variable name: %1").arg(it.key()));
        if (builtins.contains(it.key()))
            error(0, QStringLiteral("script-arg"), QStringLiteral("Cannot replace built-in variable: %1").arg(it.key()));
    }
    for (auto it = builtins.cbegin(); it != builtins.cend(); ++it)
        variables.insert(it.key(), it.value());
    QString normalized = source;
    if (normalized.startsWith(QChar(0xfeff))) normalized.remove(0, 1);
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    bool hasComparison = false;
    bool anyComparison = false;
    for (int line = 0; line < lines.size(); ++line) {
        QStringList tokens;
        QString lexicalError;
        if (!tokenize(lines.at(line), &tokens, &lexicalError)) {
            error(line + 1, tokens.value(0), lexicalError);
            continue;
        }
        if (tokens.isEmpty()) continue;
        const QString name = tokens.takeFirst().toLower();
        auto definition = std::find_if(commands().cbegin(), commands().cend(), [&](const CommandDefinition &entry) { return entry.name == name; });
        if (definition == commands().cend()) {
            error(line + 1, name, QStringLiteral("Unsupported command '%1'. Supported commands: load, compare, report, set, print, log.").arg(name));
            continue;
        }
        QStringList args;
        QStringList expansionErrors;
        for (const QString &token : tokens) {
            QString value;
            expand(token, variables, &value, &expansionErrors);
            args.append(value);
        }
        for (const QString &message : expansionErrors)
            error(line + 1, name, message);
        if (!expansionErrors.isEmpty()) continue;
        Command command;
        command.line = line + 1;
        command.name = name;
        QString commandError;
        if (name == QStringLiteral("load") || name == QStringLiteral("compare")) {
            command.kind = name == QStringLiteral("load") ? Command::Kind::Load : Command::Kind::Compare;
            if (!prepareComparison(args, current, directory, &command.request, &commandError)) {
                error(line + 1, name, commandError);
                continue;
            }
            if (name == QStringLiteral("load") && args.isEmpty()) {
                error(line + 1, name, QStringLiteral("load requires input paths or comparison options."));
                continue;
            }
            current = command.request;
            result.program.protectedPaths += Cli::inputPaths(current);
            hasComparison = command.kind == Command::Kind::Compare;
            anyComparison = anyComparison || hasComparison;
        } else if (name == QStringLiteral("report")) {
            command.kind = Command::Kind::Report;
            if (!prepareReport(args, directory, &command, &commandError))
                error(line + 1, name, commandError);
            if (!hasComparison)
                error(line + 1, name, QStringLiteral("report requires a preceding compare for the currently loaded inputs."));
            if (!commandError.isEmpty() || !hasComparison) continue;
        } else if (name == QStringLiteral("set")) {
            command.kind = Command::Kind::Set;
            QString key, value;
            if (args.size() == 1 && args.first().contains(QLatin1Char('='))) {
                key = args.first().section(QLatin1Char('='), 0, 0);
                value = args.first().mid(key.size() + 1);
            } else if (args.size() == 2) {
                key = args.at(0); value = args.at(1);
            }
            if (!variableName.match(key).hasMatch()) {
                error(line + 1, name, QStringLiteral("Use set name=value or set name value with a valid variable name."));
                continue;
            }
            if (builtins.contains(key)) {
                error(line + 1, name, QStringLiteral("Cannot replace built-in variable: %1").arg(key));
                continue;
            }
            variables.insert(key, value);
        } else {
            command.kind = name == QStringLiteral("print") ? Command::Kind::Print : Command::Kind::Log;
            if (args.isEmpty()) {
                error(line + 1, name, QStringLiteral("%1 requires text.").arg(name));
                continue;
            }
            command.text = args.join(QLatin1Char(' '));
        }
        result.program.commands.append(command);
    }
    result.program.protectedPaths.removeDuplicates();
    for (const Command &command : result.program.commands) {
        if (command.kind != Command::Kind::Report) continue;
        QString outputError;
        if (!Cli::isSafeOutputPath(command.reportPath, result.program.protectedPaths, &outputError))
            error(command.line, command.name, outputError);
    }
    QStringList logProtectedPaths = result.program.protectedPaths;
    for (const Command &command : result.program.commands)
        if (command.kind == Command::Kind::Report)
            logProtectedPaths.append(command.reportPath);
    if (!defaults.reportFile.isEmpty()) logProtectedPaths.append(defaults.reportFile);
    if (!defaults.logFile.isEmpty() && !defaults.noLogFile) {
        QString outputError;
        if (!Cli::isSafeOutputPath(defaults.logFile, logProtectedPaths, &outputError))
            error(0, QStringLiteral("log"), outputError);
    }
    if (!defaults.reportFile.isEmpty()) {
        QString outputError;
        if (!Cli::isSafeOutputPath(defaults.reportFile, result.program.protectedPaths, &outputError))
            error(0, QStringLiteral("report"), outputError);
    }
    if (!defaults.reportFile.isEmpty() && !anyComparison)
        error(0, QStringLiteral("report"), QStringLiteral("A runner report requires at least one compare command."));
    return result;
}

Cli::ExecutionResult executeFile(const Cli::Request &request, const QString &version)
{
    try {
        if (request.scriptFile.isEmpty())
            return Cli::errorResult(Cli::UsageError, QStringLiteral("A script file is required."), request.json);
        if (!QFileInfo(request.scriptFile).isFile())
            return Cli::errorResult(Cli::DataError, QStringLiteral("Script is not a regular file: %1").arg(request.scriptFile), request.json);
        QFile file(request.scriptFile);
        if (!file.open(QIODevice::ReadOnly))
            return Cli::errorResult(Cli::DataError, QStringLiteral("Cannot read script '%1': %2").arg(request.scriptFile, file.errorString()), request.json);
        constexpr qint64 maximumScriptBytes = 16 * 1024 * 1024;
        const QByteArray bytes = file.read(maximumScriptBytes + 1);
        if (file.error() != QFileDevice::NoError)
            return Cli::errorResult(Cli::DataError, file.errorString(), request.json);
        if (bytes.size() > maximumScriptBytes)
            return Cli::errorResult(Cli::UsageError, QStringLiteral("Script exceeds the 16 MiB size limit."), request.json);
        QTextCodec::ConverterState state;
        const QString source = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
        if (state.invalidChars != 0 || state.remainingChars != 0)
            return Cli::errorResult(Cli::UsageError, QStringLiteral("Script is not valid UTF-8."), request.json);
        const ParseResult parsed = parse(source, request.scriptFile, request, version);
        Cli::ExecutionResult result;
        if (!parsed.ok()) {
            result.exitCode = Cli::UsageError;
            QJsonArray diagnostics;
            for (const Diagnostic &diagnostic : parsed.diagnostics) {
                result.standardError += diagnosticText(parsed.program.scriptFile, diagnostic, result.exitCode);
                diagnostics.append(QJsonObject{{QStringLiteral("line"), diagnostic.line}, {QStringLiteral("command"), diagnostic.command}, {QStringLiteral("message"), diagnostic.message}});
            }
            result.summary = {{QStringLiteral("kind"), QStringLiteral("script")}, {QStringLiteral("exitCode"), result.exitCode},
                              {QStringLiteral("script"), parsed.program.scriptFile}, {QStringLiteral("diagnostics"), diagnostics}, {QStringLiteral("executed"), 0}};
            if (request.json)
                result.standardOutput = QString::fromUtf8(QJsonDocument(result.summary).toJson(QJsonDocument::Compact)) + QLatin1Char('\n');
            return result;
        }
        QElapsedTimer total;
        total.start();
        QJsonArray steps, messages;
        QString audit;
        Cli::ExecutionResult latest;
        bool hasLatest = false;
        int succeeded = 0, failed = 0;
        for (const Command &command : parsed.program.commands) {
            QElapsedTimer elapsed;
            elapsed.start();
            Cli::ExecutionResult step;
            if (command.kind == Command::Kind::Compare) {
                hasLatest = false;
                step = Cli::execute(command.request, version, parsed.program.protectedPaths);
                if (step.ok()) { latest = step; hasLatest = true; }
                if (!request.json) result.standardOutput += step.standardOutput;
            } else if (command.kind == Command::Kind::Load) {
                hasLatest = false;
            } else if (command.kind == Command::Kind::Report) {
                QString reportError;
                if (!hasLatest) {
                    step = Cli::errorResult(Cli::DataError, QStringLiteral("No successful comparison is available for report."));
                } else if ((!command.reportOnDiffOnly || latest.exitCode == Cli::Different)
                           && !Cli::writeReport(command.reportPath, command.reportFormat, latest, parsed.program.protectedPaths, &reportError)) {
                    step = Cli::errorResult(Cli::DataError, reportError);
                }
            } else if (command.kind == Command::Kind::Print || command.kind == Command::Kind::Log) {
                const QString record = command.name.toUpper() + QLatin1Char('\t') + field(command.text) + QLatin1Char('\n');
                if (!request.json) result.standardOutput += record;
                if (command.kind == Command::Kind::Log) audit += record;
                messages.append(QJsonObject{{QStringLiteral("line"), command.line}, {QStringLiteral("command"), command.name}, {QStringLiteral("text"), command.text}});
            }
            QJsonObject record {{QStringLiteral("line"), command.line}, {QStringLiteral("command"), command.name},
                                {QStringLiteral("exitCode"), step.exitCode}, {QStringLiteral("durationMs"), elapsed.elapsed()}};
            if (command.kind == Command::Kind::Compare) record.insert(QStringLiteral("comparison"), step.summary);
            steps.append(record);
            audit += QStringLiteral("STEP\t%1\t%2\t%3\t%4\t%5\n")
                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)).arg(command.line)
                .arg(command.name).arg(step.exitCode).arg(elapsed.elapsed());
            result.exitCode = std::max(result.exitCode, step.exitCode);
            if (step.ok()) {
                ++succeeded;
            } else {
                ++failed;
                const QString message = step.standardError.trimmed();
                const QString diagnostic = diagnosticText(parsed.program.scriptFile, {command.line, command.name, message}, step.exitCode);
                result.standardError += diagnostic;
                audit += diagnostic;
                if (!request.continueOnError) break;
            }
        }
        if (!request.reportFile.isEmpty() && (request.continueOnError || failed == 0)) {
            QString reportError;
            if (!hasLatest) reportError = QStringLiteral("No successful comparison is available for the runner report.");
            else if (!request.reportOnDiffOnly || latest.exitCode == Cli::Different)
                Cli::writeReport(request.reportFile, request.reportFormat.isEmpty() ? QStringLiteral("txt") : request.reportFormat,
                                 latest, parsed.program.protectedPaths, &reportError);
            if (!reportError.isEmpty()) {
                result.exitCode = std::max(result.exitCode, int(Cli::DataError));
                result.standardError += diagnosticText(parsed.program.scriptFile, {0, QStringLiteral("report"), reportError}, Cli::DataError);
                ++failed;
            }
        }
        QString summaryText = QStringLiteral("SCRIPT\t%1\t%2\t%3\t%4\n").arg(result.exitCode).arg(succeeded).arg(failed).arg(total.elapsed());
        audit += summaryText;
        if (!request.logFile.isEmpty() && !request.noLogFile) {
            QStringList logProtectedPaths = parsed.program.protectedPaths;
            for (const Command &command : parsed.program.commands)
                if (command.kind == Command::Kind::Report)
                    logProtectedPaths.append(command.reportPath);
            if (!request.reportFile.isEmpty()) logProtectedPaths.append(request.reportFile);
            QString logError;
            if (!writeLog(request.logFile, audit, logProtectedPaths, &logError)) {
                result.exitCode = std::max(result.exitCode, int(Cli::DataError));
                result.standardError += diagnosticText(parsed.program.scriptFile, {0, QStringLiteral("log"), logError}, Cli::DataError);
                ++failed;
            }
        }
        result.summary = {{QStringLiteral("kind"), QStringLiteral("script")}, {QStringLiteral("script"), parsed.program.scriptFile},
                          {QStringLiteral("exitCode"), result.exitCode}, {QStringLiteral("succeeded"), succeeded}, {QStringLiteral("failed"), failed},
                          {QStringLiteral("executed"), steps.size()}, {QStringLiteral("durationMs"), total.elapsed()},
                          {QStringLiteral("steps"), steps}, {QStringLiteral("messages"), messages}};
        if (request.json)
            result.standardOutput = QString::fromUtf8(QJsonDocument(result.summary).toJson(QJsonDocument::Compact)) + QLatin1Char('\n');
        else
            result.standardOutput += QStringLiteral("SCRIPT\t%1\t%2\t%3\t%4\n").arg(result.exitCode).arg(succeeded).arg(failed).arg(total.elapsed());
        return result;
    } catch (const std::exception &exception) {
        return Cli::errorResult(Cli::InternalError, QStringLiteral("Internal script error: %1").arg(QString::fromUtf8(exception.what())), request.json);
    } catch (...) {
        return Cli::errorResult(Cli::InternalError, QStringLiteral("Internal script error."), request.json);
    }
}

} }
