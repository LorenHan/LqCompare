#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>

#include "scriptengine.h"

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using namespace LqCompare;

namespace {
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Could not create script test fixture");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

Cli::ExecutionResult runScript(const QTemporaryDir &directory, const QString &source,
                               Cli::Request defaults = {})
{
    defaults.scriptFile = directory.filePath(QStringLiteral("task.lqs"));
    writeFile(defaults.scriptFile, source.toUtf8());
    return Script::executeFile(defaults, QStringLiteral("test-1.2.3"));
}

void writePair(const QTemporaryDir &directory, const QByteArray &left = "same\n",
               const QByteArray &right = "same\n")
{
    writeFile(directory.filePath(QStringLiteral("left.txt")), left);
    writeFile(directory.filePath(QStringLiteral("right.txt")), right);
}

QString diagnosticText(const Script::ParseResult &parsed)
{
    QStringList messages;
    for (const auto &diagnostic : parsed.diagnostics)
        messages.append(QStringLiteral("%1 %2: %3")
                        .arg(diagnostic.line).arg(diagnostic.command, diagnostic.message));
    return messages.join(QLatin1Char('\n'));
}
}

class ScriptTests : public QObject {
    Q_OBJECT

private slots:
    void helpListsEverySupportedCommand()
    {
        const auto help = Script::helpText();
        QVERIFY(!help.isEmpty());
        QVERIFY(!Script::commands().isEmpty());
        for (const auto &command : Script::commands()) {
            QVERIFY2(help.contains(command.name), qPrintable(command.name));
            QVERIFY(!command.usage.isEmpty());
            QVERIFY(!command.description.isEmpty());
        }
    }

    void commentsQuotesBomAndCrLf()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto leftName = QString::fromUtf8("左 # file.txt");
        const auto rightName = QString::fromUtf8("右 file.txt");
        writeFile(directory.filePath(leftName), "unchanged\n");
        writeFile(directory.filePath(rightName), "unchanged\n");
        Cli::Request request;
        request.scriptFile = directory.filePath(QStringLiteral("task.lqs"));
        const QString source = QStringLiteral("# header\r\n\r\nload '%1' \"%2\" # comment\r\n"
                                              "compare\r\nprint 'text # stays'\r\n")
                                   .arg(leftName, rightName);
        writeFile(request.scriptFile, QByteArray::fromHex("efbbbf") + source.toUtf8());
        const auto result = Script::executeFile(request);
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QVERIFY2(result.standardError.isEmpty(), qPrintable(result.standardError));
        QVERIFY(result.standardOutput.contains(QStringLiteral("text # stays")));
        QCOMPARE(readFile(directory.filePath(leftName)), QByteArray("unchanged\n"));
        QCOMPARE(readFile(directory.filePath(rightName)), QByteArray("unchanged\n"));
    }

    void backslashesAndQuotesPreserveArguments_data()
    {
        QTest::addColumn<QString>("arguments");
        QTest::addColumn<QString>("expected");
        QTest::newRow("double-quoted-trailing-slash")
            << QString::fromLatin1("\"C:\\space dir\\\\\"")
            << QString::fromLatin1("C:\\space dir\\");
        QTest::newRow("single-quoted-trailing-slash")
            << QString::fromLatin1("'C:\\space dir\\\\'")
            << QString::fromLatin1("C:\\space dir\\");
        QTest::newRow("quoted-unc-leading-and-trailing-slashes")
            << QString::fromLatin1("\"\\\\server\\space share\\\\\"")
            << QString::fromLatin1("\\\\server\\space share\\");
        QTest::newRow("unquoted-unc")
            << QString::fromLatin1("\\\\server\\share\\file.txt")
            << QString::fromLatin1("\\\\server\\share\\file.txt");
        QTest::newRow("raw-backslash-path")
            << QString::fromLatin1("C:\\raw\\path\\")
            << QString::fromLatin1("C:\\raw\\path\\");
        QTest::newRow("ordinary-backslash-runs-unchanged")
            << QString::fromLatin1("\"C:\\raw\\\\middle\\\\\\end\"")
            << QString::fromLatin1("C:\\raw\\\\middle\\\\\\end");
        QTest::newRow("single-quotes-within-double-quotes")
            << QString::fromLatin1("\"single 'quoted' text\"")
            << QString::fromLatin1("single 'quoted' text");
        QTest::newRow("double-quotes-within-single-quotes")
            << QString::fromLatin1("'double \"quoted\" text'")
            << QString::fromLatin1("double \"quoted\" text");
        QTest::newRow("backslash-before-inactive-single-quote")
            << QString::fromLatin1("\"single \\'quoted\\' text\"")
            << QString::fromLatin1("single \\'quoted\\' text");
        QTest::newRow("backslash-before-inactive-double-quote")
            << QString::fromLatin1("'double \\\"quoted\\\" text'")
            << QString::fromLatin1("double \\\"quoted\\\" text");
        QTest::newRow("escaped-active-double-quotes")
            << QString::fromLatin1("\"escaped \\\"name\\\"\"")
            << QString::fromLatin1("escaped \"name\"");
        QTest::newRow("escaped-active-single-quotes")
            << QString::fromLatin1("'escaped \\'name\\''")
            << QString::fromLatin1("escaped 'name'");

        // Exercise both quote state transitions for every parity from one to
        // six slashes. Runs beside inactive quote types are covered above.
        for (const QChar quote : {QLatin1Char('"'), QLatin1Char('\'')}) {
            const QByteArray quoteName = quote == QLatin1Char('"') ? "double" : "single";
            for (int count = 1; count <= 6; ++count) {
                const QString slashes(count, QLatin1Char('\\'));
                const QString surviving(count / 2, QLatin1Char('\\'));
                const bool literalQuote = count % 2 != 0;
                const auto suffix = QByteArray::number(count);
                const QString inside = QString(quote) + QStringLiteral("left") + slashes + quote
                    + (literalQuote ? QStringLiteral("right") + quote : QString());
                const QString expectedInside = QStringLiteral("left") + surviving
                    + (literalQuote ? QString(quote) + QStringLiteral("right") : QString());
                QTest::newRow((quoteName + "-inside-" + suffix).constData()) << inside << expectedInside;

                const QString outside = QStringLiteral("left") + slashes + quote
                    + (literalQuote ? QStringLiteral("right") : QStringLiteral(" right") + quote);
                const QString expectedOutside = QStringLiteral("left") + surviving
                    + (literalQuote ? QString(quote) + QStringLiteral("right") : QStringLiteral(" right"));
                QTest::newRow((quoteName + "-outside-" + suffix).constData()) << outside << expectedOutside;
            }
        }
    }

    void backslashesAndQuotesPreserveArguments()
    {
        QFETCH(QString, arguments);
        QFETCH(QString, expected);
        const auto parsed = Script::parse(QStringLiteral("print ") + arguments + QLatin1Char('\n'),
                                          QStringLiteral("task.lqs"));
        QVERIFY2(parsed.ok(), qPrintable(diagnosticText(parsed)));
        QCOMPARE(parsed.program.commands.size(), 1);
        QCOMPARE(parsed.program.commands.first().text, expected);
    }

    void loadPreservesWindowsPathArguments_data()
    {
        QTest::addColumn<QString>("arguments");
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::newRow("double-quotes")
            << QString::fromLatin1("\"C:\\space dir\\\\\" \"\\\\server\\space share\\\\\"")
            << QString::fromLatin1("C:\\space dir\\")
            << QString::fromLatin1("\\\\server\\space share\\");
        QTest::newRow("single-quotes")
            << QString::fromLatin1("'C:\\space dir\\\\' '\\\\server\\space share\\\\'")
            << QString::fromLatin1("C:\\space dir\\")
            << QString::fromLatin1("\\\\server\\space share\\");
        QTest::newRow("unquoted-paths")
            << QString::fromLatin1("C:\\raw\\left\\ \\\\server\\share\\right\\")
            << QString::fromLatin1("C:\\raw\\left\\")
            << QString::fromLatin1("\\\\server\\share\\right\\");
    }

    void loadPreservesWindowsPathArguments()
    {
        QFETCH(QString, arguments);
        QFETCH(QString, left);
        QFETCH(QString, right);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto parsed = Script::parse(QStringLiteral("load ") + arguments + QStringLiteral("\ncompare\n"),
                                          directory.filePath(QStringLiteral("task.lqs")));
        QVERIFY2(parsed.ok(), qPrintable(diagnosticText(parsed)));
        QCOMPARE(parsed.program.commands.size(), 2);
        const auto expectedLeft = QDir::cleanPath(QDir(directory.path()).absoluteFilePath(left));
        const auto expectedRight = QDir::cleanPath(QDir(directory.path()).absoluteFilePath(right));
        for (const auto &command : parsed.program.commands) {
            QCOMPARE(command.request.left, expectedLeft);
            QCOMPARE(command.request.right, expectedRight);
        }
    }

    void variableValuesAreNotTokenizedAgain_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("windows-trailing-slash") << QString::fromLatin1("C:\\space dir\\");
        QTest::newRow("unc-leading-and-trailing-slashes") << QString::fromLatin1("\\\\server\\space share\\");
        QTest::newRow("literal-quotes-and-backslashes") << QString::fromLatin1("C:\\space 'single' \"double\" dir\\");
        QTest::newRow("hash-and-option-shaped-text") << QString::fromLatin1("C:\\space # --ignore-case \"quoted\"\\");
    }

    void variableValuesAreNotTokenizedAgain()
    {
        QFETCH(QString, value);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto scriptFile = directory.filePath(QStringLiteral("task.lqs"));
        const auto arguments = Cli::parse({QStringLiteral("--script"), scriptFile,
                                           QStringLiteral("--script-arg"), QStringLiteral("input=") + value});
        QVERIFY2(arguments.ok(), qPrintable(arguments.error));
        const auto parsed = Script::parse(QStringLiteral("print \"${input}\"\nload \"${input}\" right.txt\n"),
                                          scriptFile, arguments.request);
        QVERIFY2(parsed.ok(), qPrintable(diagnosticText(parsed)));
        QCOMPARE(parsed.program.commands.size(), 2);
        QCOMPARE(parsed.program.commands.first().text, value);
        QCOMPARE(parsed.program.commands.last().request.left,
                 QDir::cleanPath(QDir(directory.path()).absoluteFilePath(value)));
        QCOMPARE(parsed.program.commands.last().request.right, directory.filePath(QStringLiteral("right.txt")));
        QVERIFY(!parsed.program.commands.last().request.textOptions.ignoreCase);
    }

    void reportsAllStaticErrorsWithLines()
    {
        const auto parsed = Script::parse(QStringLiteral(
            "unknown-command\n"
            "print ${missing}\n"
            "print \"unterminated\n"
            "report json result.json\n"), QStringLiteral("/tmp/task.lqs"));
        QVERIFY(!parsed.ok());
        QSet<int> lines;
        for (const auto &diagnostic : parsed.diagnostics) {
            lines.insert(diagnostic.line);
            QVERIFY(!diagnostic.command.isEmpty());
            QVERIFY(!diagnostic.message.isEmpty());
        }
        for (int line = 1; line <= 4; ++line)
            QVERIFY2(lines.contains(line), qPrintable(diagnosticText(parsed)));
    }

    void invalidProgramHasNoPartialExecution()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "before\n", "after\n");
        const auto result = runScript(directory, QStringLiteral(
            "load left.txt right.txt\ncompare\nreport json result.json\n"
            "print EXECUTED_SENTINEL\nunknown-command\n"));
        QCOMPARE(result.exitCode, int(Cli::UsageError));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("result.json"))));
        QVERIFY(!result.standardOutput.contains(QStringLiteral("EXECUTED_SENTINEL")));
        QVERIFY(result.standardError.contains(QStringLiteral("unknown-command")));
        QVERIFY(result.standardError.contains(QLatin1Char('5')));
    }

    void unsupportedCommandsAreExplicit_data()
    {
        QTest::addColumn<QString>("command");
        for (const char *command : {"select", "copy", "move", "delete", "rename", "mkdir",
                                    "sync", "mirror", "applypatch", "if", "goto", "foreach"})
            QTest::newRow(command) << QString::fromLatin1(command);
    }

    void unsupportedCommandsAreExplicit()
    {
        QFETCH(QString, command);
        const auto parsed = Script::parse(command + QStringLiteral(" arg\n"),
                                          QStringLiteral("/tmp/task.lqs"));
        QVERIFY(!parsed.ok());
        QVERIFY(!parsed.diagnostics.isEmpty());
        QCOMPARE(parsed.diagnostics.first().line, 1);
        QCOMPARE(parsed.diagnostics.first().command, command);
        QVERIFY(!parsed.diagnostics.first().message.isEmpty());
    }

    void compareAndReportRequireAnInputComparison()
    {
        QVERIFY(!Script::parse(QStringLiteral("compare\n"), QStringLiteral("task.lqs")).ok());
        QVERIFY(!Script::parse(QStringLiteral("load left.txt right.txt\nreport json result.json\n"),
                               QStringLiteral("task.lqs")).ok());
    }

    void variablesArgumentsAndEscapes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        Cli::Request request;
        request.scriptArguments.insert(QStringLiteral("source"), QStringLiteral("left.txt"));
        request.scriptArguments.insert(QStringLiteral("message"), QString::fromUtf8("参数有空格"));
        const auto result = runScript(directory, QStringLiteral(
            "set right=right.txt\n"
            "set label 'local value'\n"
            "load --left=\"${source}\" --right=\"${right}\"\n"
            "compare\n"
            "print \"${message}|${label}|$${literal}|%%|${version}\"\n"
            "print \"DIR=${script-dir}\"\n"
            "print \"CWD=${cwd}\"\n"
            "print \"TIME=${timestamp}\"\n"), request);
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QVERIFY2(result.standardError.isEmpty(), qPrintable(result.standardError));
        QVERIFY(result.standardOutput.contains(QString::fromUtf8("参数有空格|local value|${literal}|%|test-1.2.3")));
        QVERIFY(result.standardOutput.contains(QStringLiteral("DIR=") + directory.path()));
        QVERIFY(result.standardOutput.contains(QStringLiteral("CWD=") + QDir::currentPath()));
        const int timestampStart = result.standardOutput.indexOf(QStringLiteral("TIME="));
        QVERIFY(timestampStart >= 0);
        QVERIFY(result.standardOutput.mid(timestampStart + 5).section(QLatin1Char('\n'), 0, 0).trimmed().size() > 0);
    }

    void emptyVariableValuesCanPrintAndConcatenate_data()
    {
        QTest::addColumn<QString>("definition");
        QTest::addColumn<bool>("fromCli");
        QTest::newRow("assignment") << QStringLiteral("set empty=\n") << false;
        QTest::newRow("quoted-value") << QStringLiteral("set empty \"\"\n") << false;
        QTest::newRow("script-argument") << QString() << true;
    }

    void emptyVariableValuesCanPrintAndConcatenate()
    {
        QFETCH(QString, definition);
        QFETCH(bool, fromCli);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Cli::Request request;
        if (fromCli) {
            const auto arguments = Cli::parse({QStringLiteral("--script"), directory.filePath(QStringLiteral("task.lqs")),
                                               QStringLiteral("--script-arg"), QStringLiteral("empty=")});
            QVERIFY2(arguments.ok(), qPrintable(arguments.error));
            request = arguments.request;
        }
        const auto source = definition + QStringLiteral("print \"${empty}\"\nprint \"before${empty}after\"\n");
        const auto parsed = Script::parse(source, directory.filePath(QStringLiteral("task.lqs")), request);
        QVERIFY2(parsed.ok(), qPrintable(diagnosticText(parsed)));
        QVERIFY(parsed.program.commands.size() >= 2);
        const auto count = parsed.program.commands.size();
        QCOMPARE(parsed.program.commands.at(count - 2).text, QString());
        QCOMPARE(parsed.program.commands.at(count - 1).text, QStringLiteral("beforeafter"));
        const auto result = runScript(directory, source, request);
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QVERIFY(result.standardOutput.contains(QStringLiteral("PRINT\t\n")));
        QVERIFY(result.standardOutput.contains(QStringLiteral("PRINT\tbeforeafter\n")));
    }

    void emptyVariablePathsAreRejectedBeforeExecution_data()
    {
        QTest::addColumn<QString>("definition");
        QTest::addColumn<bool>("fromCli");
        QTest::addColumn<QString>("comparison");
        for (const QString &comparison : {QStringLiteral("load \"${empty}\" right.txt\ncompare\n"),
                                           QStringLiteral("compare --left=\"${empty}\" --right=right.txt\n")}) {
            const auto form = comparison.startsWith(QStringLiteral("load")) ? "positional" : "named";
            QTest::newRow(qPrintable(QStringLiteral("assignment-%1").arg(form)))
                << QStringLiteral("set empty=\n") << false << comparison;
            QTest::newRow(qPrintable(QStringLiteral("quoted-value-%1").arg(form)))
                << QStringLiteral("set empty \"\"\n") << false << comparison;
            QTest::newRow(qPrintable(QStringLiteral("script-argument-%1").arg(form)))
                << QString() << true << comparison;
        }
    }

    void emptyVariablePathsAreRejectedBeforeExecution()
    {
        QFETCH(QString, definition);
        QFETCH(bool, fromCli);
        QFETCH(QString, comparison);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        Cli::Request request;
        if (fromCli) {
            const auto arguments = Cli::parse({QStringLiteral("--script"), directory.filePath(QStringLiteral("task.lqs")),
                                               QStringLiteral("--script-arg"), QStringLiteral("empty=")});
            QVERIFY2(arguments.ok(), qPrintable(arguments.error));
            request = arguments.request;
        }
        // A prior valid load must not hide an explicitly supplied empty path.
        const auto source = definition + QStringLiteral("load left.txt right.txt\nprint SHOULD_NOT_RUN\n") + comparison;
        const auto result = runScript(directory, source, request);
        QCOMPARE(result.exitCode, int(Cli::UsageError));
        QVERIFY(!result.standardError.isEmpty());
        QVERIFY(!result.standardOutput.contains(QStringLiteral("SHOULD_NOT_RUN")));
    }

    void relativePathsUseScriptDirectory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("inputs")));
        writeFile(directory.filePath(QStringLiteral("inputs/left file.txt")), "left\n");
        writeFile(directory.filePath(QStringLiteral("inputs/right file.txt")), "right\n");
        const auto result = runScript(directory, QStringLiteral(
            "compare 'inputs/left file.txt' 'inputs/right file.txt'\n"
            "report --report=json --report-file=result.json\n"));
        QCOMPARE(result.exitCode, int(Cli::Different));
        const auto bytes = readFile(directory.filePath(QStringLiteral("result.json")));
        QVERIFY(!bytes.isEmpty());
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(bytes, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(json.isObject());
    }

    void loadRulesAndCallerDefaultsAreInherited()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "Hello\n", "hello\n");
        auto result = runScript(directory, QStringLiteral("load --ignore-case left.txt right.txt\ncompare\n"));
        QCOMPARE(result.exitCode, int(Cli::Equal));

        Cli::Request request;
        request.textOptions.ignoreCase = true;
        result = runScript(directory, QStringLiteral("load left.txt right.txt\ncompare\n"), request);
        QCOMPARE(result.exitCode, int(Cli::Equal));

        result = runScript(directory, QStringLiteral("load left.txt right.txt\ncompare --ignore-case\n"));
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("left.txt"))), QByteArray("Hello\n"));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("right.txt"))), QByteArray("hello\n"));
    }

    void exactEolOverridesInheritedIgnoreEol()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "one\r\ntwo\r\n", "one\ntwo\n");
        const auto result = runScript(directory, QStringLiteral(
            "load --ignore-eol left.txt right.txt\ncompare --exact-eol\n"));
        QCOMPARE(result.exitCode, int(Cli::Different));
    }

    void nestedScriptInvocationIsRejectedBeforeExecution()
    {
        const auto parsed = Script::parse(QStringLiteral(
            "load left.txt right.txt\ncompare @nested.lqs\n"), QStringLiteral("/tmp/task.lqs"));
        QVERIFY(!parsed.ok());
        bool hasNestedError = false;
        for (const auto &diagnostic : parsed.diagnostics)
            hasNestedError = hasNestedError || (diagnostic.line == 2 && diagnostic.command == QStringLiteral("compare"));
        QVERIFY2(hasNestedError, qPrintable(diagnosticText(parsed)));
    }

    void absolutePosixPathsAreNotOptions()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX option and path disambiguation runs on POSIX hosts.");
#else
        for (const auto &source : {QStringLiteral("compare /help /left\n"),
                                   QStringLiteral("load /left /right\ncompare\n"),
                                   QStringLiteral("compare /version /script\n")}) {
            const auto parsed = Script::parse(source, QStringLiteral("/tmp/task.lqs"));
            QVERIFY2(parsed.ok(), qPrintable(diagnosticText(parsed)));
        }
#endif
    }

    void jsonRunnerProducesOneObject_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("exitCode");
        QTest::newRow("successful") << QStringLiteral("print visible-message\ncompare left.txt right.txt\nlog after-message\n")
                                    << int(Cli::Equal);
        QTest::newRow("runtime-error") << QStringLiteral("print visible-message\ncompare missing.txt right.txt\n")
                                       << int(Cli::DataError);
        QTest::newRow("syntax-error") << QStringLiteral("print visible-message\nunknown-command\n")
                                      << int(Cli::UsageError);
    }

    void jsonRunnerProducesOneObject()
    {
        QFETCH(QString, source);
        QFETCH(int, exitCode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        Cli::Request request;
        request.json = true;
        const auto result = runScript(directory, source, request);
        QCOMPARE(result.exitCode, exitCode);
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(result.standardOutput.toUtf8(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(json.isObject());
        QCOMPARE(json.object().value(QStringLiteral("exitCode")).toInt(-1), exitCode);
        QCOMPARE(json.object().value(QStringLiteral("kind")).toString(), QStringLiteral("script"));
    }

    void invalidUtf8PreventsAllExecution_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid-byte") << QByteArray("print SHOULD_NOT_RUN\nprint ") + QByteArray::fromHex("ff") + '\n';
        QTest::newRow("truncated-character") << QByteArray("print SHOULD_NOT_RUN\nprint ") + QByteArray::fromHex("e4b8");
    }

    void invalidUtf8PreventsAllExecution()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Cli::Request request;
        request.scriptFile = directory.filePath(QStringLiteral("invalid.lqs"));
        writeFile(request.scriptFile, bytes);
        const auto result = Script::executeFile(request);
        QCOMPARE(result.exitCode, int(Cli::UsageError));
        QVERIFY(!result.standardOutput.contains(QStringLiteral("SHOULD_NOT_RUN")));
        QVERIFY(result.standardError.contains(QStringLiteral("UTF-8"), Qt::CaseInsensitive));
    }

    void reportsSupportedFormats_data()
    {
        QTest::addColumn<QString>("format");
        for (const char *format : {"txt", "csv", "json", "html"})
            QTest::newRow(format) << QString::fromLatin1(format);
    }

    void reportsSupportedFormats()
    {
        QFETCH(QString, format);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "old\n", "new\n");
        const auto result = runScript(directory, QStringLiteral(
            "load left.txt right.txt\ncompare\nreport %1 'report.%1'\n").arg(format));
        QCOMPARE(result.exitCode, int(Cli::Different));
        const auto bytes = readFile(directory.filePath(QStringLiteral("report.") + format));
        QVERIFY2(!bytes.isEmpty(), qPrintable(result.standardError));
        if (format == QStringLiteral("json")) {
            QJsonParseError error;
            QVERIFY(QJsonDocument::fromJson(bytes, &error).isObject());
            QCOMPARE(error.error, QJsonParseError::NoError);
        } else if (format == QStringLiteral("html")) {
            QVERIFY(bytes.toLower().contains("<html"));
        }
    }

    void reportOnDiffOnlySkipsEqualComparison()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        const auto source = QStringLiteral(
            "compare left.txt right.txt\n"
            "report --report=json --report-file=result.json --report-on-diff-only\n");
        const auto equal = runScript(directory, source);
        QCOMPARE(equal.exitCode, int(Cli::Equal));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("result.json"))));

        writeFile(directory.filePath(QStringLiteral("right.txt")), "different\n");
        const auto different = runScript(directory, source);
        QCOMPARE(different.exitCode, int(Cli::Different));
        QVERIFY(!readFile(directory.filePath(QStringLiteral("result.json"))).isEmpty());
    }

    void folderComparisonDoesNotWriteInputs()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("left")));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("right")));
        writeFile(directory.filePath(QStringLiteral("left/only.txt")), "unchanged\n");
        const auto result = runScript(directory, QStringLiteral("load left right\ncompare\nreport json folder.json\n"));
        QCOMPARE(result.exitCode, int(Cli::Different));
        QVERIFY(QFile::exists(directory.filePath(QStringLiteral("folder.json"))));
        QCOMPARE(QDir(directory.filePath(QStringLiteral("left"))).entryList(QDir::Files),
                 QStringList{QStringLiteral("only.txt")});
        QVERIFY(QDir(directory.filePath(QStringLiteral("right"))).entryList(QDir::Files).isEmpty());
        QCOMPARE(readFile(directory.filePath(QStringLiteral("left/only.txt"))), QByteArray("unchanged\n"));
    }

    void errorsStopExecutionByDefault()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        const auto result = runScript(directory, QStringLiteral(
            "# line one\ncompare missing.txt right.txt\nprint SHOULD_NOT_RUN\n"
            "compare left.txt right.txt\nreport json later.json\n"));
        QCOMPARE(result.exitCode, int(Cli::DataError));
        QVERIFY(!result.standardOutput.contains(QStringLiteral("SHOULD_NOT_RUN")));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("later.json"))));
        QVERIFY(result.standardError.contains(QStringLiteral("compare")));
        QVERIFY(result.standardError.contains(QLatin1Char('2')));
    }

    void continueOnErrorKeepsMostSevereExitCode()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "old\n", "new\n");
        Cli::Request request;
        request.continueOnError = true;
        const auto result = runScript(directory, QStringLiteral(
            "compare missing.txt right.txt\nprint CONTINUED_SENTINEL\n"
            "compare left.txt right.txt\nreport json later.json\n"
            "compare left.txt left.txt\n"), request);
        QCOMPARE(result.exitCode, int(Cli::DataError));
        QVERIFY(result.standardOutput.contains(QStringLiteral("CONTINUED_SENTINEL")));
        QVERIFY(QFile::exists(directory.filePath(QStringLiteral("later.json"))));
        QVERIFY(result.standardError.contains(QStringLiteral("compare")));
    }

    void differencesAreNotExecutionErrors()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "old\n", "new\n");
        const auto result = runScript(directory, QStringLiteral(
            "compare left.txt right.txt\nprint STILL_RUNNING\ncompare left.txt left.txt\n"));
        QCOMPARE(result.exitCode, int(Cli::Different));
        QVERIFY(result.standardOutput.contains(QStringLiteral("STILL_RUNNING")));
    }

    void failedCompareCannotReportAnEarlierSuccess()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        Cli::Request request;
        request.continueOnError = true;
        const auto result = runScript(directory, QStringLiteral(
            "compare left.txt right.txt\ncompare missing.txt right.txt\nreport json stale.json\n"), request);
        QCOMPARE(result.exitCode, int(Cli::DataError));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("stale.json"))));
        QVERIFY(result.standardError.contains(QStringLiteral("compare")));
        QVERIFY(result.standardError.contains(QStringLiteral("report")));
    }

    void reportWriteFailureHasDataErrorExitCode()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        writeFile(directory.filePath(QStringLiteral("blocked")), "this is a file\n");
        const auto result = runScript(directory, QStringLiteral(
            "compare left.txt right.txt\nreport json blocked/report.json\n"));
        QCOMPARE(result.exitCode, int(Cli::DataError));
        QVERIFY(!result.standardError.isEmpty());
        QCOMPARE(readFile(directory.filePath(QStringLiteral("blocked"))), QByteArray("this is a file\n"));
    }

    void reportCannotOverwriteInputsOrScript_data()
    {
        QTest::addColumn<QString>("destination");
        QTest::newRow("left") << QStringLiteral("left.txt");
        QTest::newRow("right") << QStringLiteral("right.txt");
        QTest::newRow("script") << QStringLiteral("task.lqs");
        QTest::newRow("normalized-input") << QStringLiteral("./left.txt");
    }

    void reportCannotOverwriteInputsOrScript()
    {
        QFETCH(QString, destination);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory, "old\n", "new\n");
        const auto source = QStringLiteral("compare left.txt right.txt\nreport json '%1'\n").arg(destination);
        const auto result = runScript(directory, source);
        QVERIFY(result.exitCode >= Cli::UsageError);
        QVERIFY(!result.standardError.isEmpty());
        QCOMPARE(readFile(directory.filePath(QStringLiteral("left.txt"))), QByteArray("old\n"));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("right.txt"))), QByteArray("new\n"));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("task.lqs"))), source.toUtf8());
    }

    void reportCannotOverwriteFutureInput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        writeFile(directory.filePath(QStringLiteral("future.txt")), "must survive\n");
        const auto result = runScript(directory, QStringLiteral(
            "compare left.txt right.txt\nreport json future.txt\n"
            "load future.txt right.txt\ncompare\n"));
        QVERIFY(result.exitCode >= Cli::UsageError);
        QCOMPARE(readFile(directory.filePath(QStringLiteral("future.txt"))), QByteArray("must survive\n"));
    }

    void reportCannotWriteInsideInputDirectory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("left")));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("right")));
        const auto result = runScript(directory, QStringLiteral(
            "compare left right\nreport json left/new-report.json\n"));
        QVERIFY(result.exitCode >= Cli::UsageError);
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("left/new-report.json"))));
    }

    void aliasesCannotBypassInputProtection()
    {
#ifdef Q_OS_WIN
        QSKIP("QFile::link creates a shortcut on Windows; symlink protection runs on POSIX.");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        const auto alias = directory.filePath(QStringLiteral("alias.txt"));
        QVERIFY(QFile::link(directory.filePath(QStringLiteral("left.txt")), alias));
        const auto result = runScript(directory, QStringLiteral(
            "compare left.txt right.txt\nreport json alias.txt\n"));
        QVERIFY(result.exitCode >= Cli::UsageError);
        QCOMPARE(readFile(directory.filePath(QStringLiteral("left.txt"))), QByteArray("same\n"));
        QVERIFY(QFileInfo(alias).isSymLink());

        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("left")));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("right")));
        QVERIFY(QFile::link(directory.filePath(QStringLiteral("left")),
                            directory.filePath(QStringLiteral("alias-directory"))));
        const auto folderResult = runScript(directory, QStringLiteral(
            "compare left right\nreport json alias-directory/new-report.json\n"));
        QVERIFY(folderResult.exitCode >= Cli::UsageError);
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("left/new-report.json"))));
#endif
    }

    void logCannotOverwriteInputOrScript_data()
    {
        QTest::addColumn<QString>("destination");
        QTest::newRow("input") << QStringLiteral("left.txt");
        QTest::newRow("script") << QStringLiteral("task.lqs");
        QTest::newRow("future-input") << QStringLiteral("future.txt");
    }

    void logCannotOverwriteInputOrScript()
    {
        QFETCH(QString, destination);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        writeFile(directory.filePath(QStringLiteral("future.txt")), "future content\n");
        Cli::Request request;
        request.logFile = directory.filePath(destination);
        const auto source = QStringLiteral("compare left.txt right.txt\nload future.txt right.txt\ncompare\n");
        const auto result = runScript(directory, source, request);
        QVERIFY(result.exitCode >= Cli::UsageError);
        QCOMPARE(readFile(directory.filePath(QStringLiteral("left.txt"))), QByteArray("same\n"));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("future.txt"))), QByteArray("future content\n"));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("task.lqs"))), source.toUtf8());
    }

    void printLogAndExecutionLogAreAvailable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        Cli::Request request;
        request.logFile = directory.filePath(QStringLiteral("execution.log"));
        const auto result = runScript(directory, QStringLiteral(
            "print start-message\nload left.txt right.txt\ncompare\nlog end-message\n"), request);
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QVERIFY(result.standardOutput.contains(QStringLiteral("start-message")));
        QVERIFY(result.standardOutput.contains(QStringLiteral("end-message")));
        const auto log = readFile(request.logFile);
        QVERIFY(!log.isEmpty());
        QVERIFY(log.contains("compare"));
    }

    void noLogFileIgnoresResidualLogDestination_data()
    {
        QTest::addColumn<QString>("destination");
        QTest::newRow("new-log") << QStringLiteral("disabled.log");
        QTest::newRow("protected-input") << QStringLiteral("left.txt");
    }

    void noLogFileIgnoresResidualLogDestination()
    {
        QFETCH(QString, destination);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        Cli::Request request;
        request.noLogFile = true;
        request.logFile = directory.filePath(destination);
        const auto result = runScript(directory, QStringLiteral("compare left.txt right.txt\n"), request);
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("disabled.log"))));
        QCOMPARE(readFile(directory.filePath(QStringLiteral("left.txt"))), QByteArray("same\n"));
    }

    void logAndReportCannotShareDestination_data()
    {
        QTest::addColumn<bool>("alias");
        QTest::newRow("same-path") << false;
#ifndef Q_OS_WIN
        QTest::newRow("symlink-alias") << true;
#endif
    }

    void logAndReportCannotShareDestination()
    {
        QFETCH(bool, alias);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writePair(directory);
        const auto reportPath = directory.filePath(QStringLiteral("result.json"));
        writeFile(reportPath, "previous report\n");
        Cli::Request request;
        request.logFile = alias ? directory.filePath(QStringLiteral("alias.log")) : reportPath;
        if (alias) QVERIFY(QFile::link(reportPath, request.logFile));
        const auto result = runScript(directory, QStringLiteral(
            "print SHOULD_NOT_RUN\ncompare left.txt right.txt\nreport json result.json\n"), request);
        QCOMPARE(result.exitCode, int(Cli::UsageError));
        QVERIFY(!result.standardOutput.contains(QStringLiteral("SHOULD_NOT_RUN")));
        QCOMPARE(readFile(reportPath), QByteArray("previous report\n"));
        if (alias) QVERIFY(QFileInfo(request.logFile).isSymLink());
    }

    void unreadableScriptIsDataError()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Cli::Request request;
        request.scriptFile = directory.filePath(QStringLiteral("missing.lqs"));
        const auto result = Script::executeFile(request);
        QCOMPARE(result.exitCode, int(Cli::DataError));
        QVERIFY(!result.standardError.isEmpty());
    }

    void nonRegularScriptInputsFailPromptly_data()
    {
        QTest::addColumn<bool>("fifo");
        QTest::newRow("directory") << false;
#ifdef Q_OS_UNIX
        QTest::newRow("fifo") << true;
#endif
    }

    void nonRegularScriptInputsFailPromptly()
    {
        QFETCH(bool, fifo);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Cli::Request request;
        request.scriptFile = directory.filePath(QStringLiteral("non-regular.lqs"));
        if (fifo) {
#ifdef Q_OS_UNIX
            QCOMPARE(::mkfifo(QFile::encodeName(request.scriptFile).constData(), 0600), 0);
#endif
        } else {
            QVERIFY(QDir().mkdir(request.scriptFile));
        }
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = Script::executeFile(request);
        QVERIFY(elapsed.elapsed() < 2000);
        QCOMPARE(result.exitCode, int(Cli::DataError));
        QVERIFY(!result.standardError.isEmpty());
    }

    void symlinkToRegularScriptStillExecutes()
    {
#ifdef Q_OS_WIN
        QSKIP("QFile::link creates a shortcut on Windows; script symlinks run on POSIX.");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto target = directory.filePath(QStringLiteral("real.lqs"));
        writeFile(target, "print SYMLINK_SCRIPT_RAN\n");
        Cli::Request request;
        request.scriptFile = directory.filePath(QStringLiteral("linked.lqs"));
        QVERIFY(QFile::link(target, request.scriptFile));
        const auto result = Script::executeFile(request);
        QCOMPARE(result.exitCode, int(Cli::Equal));
        QVERIFY(result.standardOutput.contains(QStringLiteral("SYMLINK_SCRIPT_RAN")));
#endif
    }
};

QTEST_GUILESS_MAIN(ScriptTests)
#include "tst_script.moc"
