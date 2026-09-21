#include "cliexecution.h"
#include "linesimilarity.h"
#include "textdocument.h"
#include "../CliProbe/cliprobe.h"

#include <QtTest>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace LqCompare;

namespace {
bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QMap<QString, QByteArray> fileSnapshot(const QString &root)
{
    QMap<QString, QByteArray> snapshot;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        snapshot.insert(QDir(root).relativeFilePath(path), readBytes(path));
    }
    return snapshot;
}

struct ProcessResult {
    bool finished = false;
    int exitCode = -1;
    QProcess::ExitStatus status = QProcess::CrashExit;
    QByteArray out, err;
};

ProcessResult runProbe(const QStringList &arguments, const QString &configRoot = {})
{
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("DISPLAY"));
    environment.remove(QStringLiteral("WAYLAND_DISPLAY"));
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("LQCOMPARE_CLI_PROBE_MODE"), QStringLiteral("1"));
    if (!configRoot.isEmpty()) {
        environment.insert(QStringLiteral("LQCOMPARE_CLI_TEST_CONFIG_HOME"), configRoot);
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), configRoot);
    }
    process.setProcessEnvironment(environment);
    const QString executable = qEnvironmentVariableIsSet("LQCOMPARE_CLI_PROBE")
        ? qEnvironmentVariable("LQCOMPARE_CLI_PROBE") : QCoreApplication::applicationFilePath();
    process.start(executable, arguments);
    ProcessResult result;
    result.finished = process.waitForStarted(10000) && process.waitForFinished(30000);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(5000);
    }
    result.exitCode = process.exitCode();
    result.status = process.exitStatus();
    result.out = process.readAllStandardOutput();
    result.err = process.readAllStandardError();
    if (!result.finished) result.err += process.errorString().toUtf8();
    return result;
}

Cli::ExecutionResult comparePaths(const QString &left, const QString &right,
                                  const QStringList &flags = {})
{
    const auto parsed = Cli::parse(flags + QStringList{QStringLiteral("--silent"), left, right});
    if (!parsed.ok()) return Cli::errorResult(Cli::UsageError, parsed.error);
    return Cli::execute(parsed.request);
}
}

class CliTests : public QObject
{
    Q_OBJECT
private slots:
    void exitCodeContract();
    void nativePaths_data();
    void nativePaths();
    void pathSpellingAndTerminator();
    void windowsAliasesAndPosixPaths();
    void longAliasesAndNoLog();
    void namedInputs();
    void sessionTypes_data();
    void sessionTypes();
    void optionsMapToRequest();
    void similarityOptionsMapToTextOptions();
    void similarityOptionsAreUsableFromTheCommandLine();
    void scriptArguments();
    void invalidArguments_data();
    void invalidArguments();
    void unknownOptionSuggestsClosest();
    void generatedHelpAndDiscovery();
    void parseDoesNotReadFiles();
    void textComparison_data();
    void textComparison();
    void unreadableOrUnsupportedInputs_data();
    void unreadableOrUnsupportedInputs();
    void folderComparison();
    void folderRecursion();
    void reports_data();
    void reports();
    void reportOnlyOnDifference();
    void outputFailures();
    void outputCannotReplaceInputs_data();
    void outputCannotReplaceInputs();
    void outputCannotWriteInsideInputDirectories();
    void symlinkOutputAliasesAreRejected();
    void hardLinkOutputAliasesAreRejected();
    void safeOutputSiblingsAndProtectedPaths();
    void reportAndLogCannotCollide();
    void machineOutputEscapesFields();
    void htmlReportEscapesPaths();
    void explicitLogs();
    void realProcessExitCodes_data();
    void realProcessExitCodes();
    void realProcessJsonAndUtf8();
    void realProcessHelpAndVersion();
    void realProcessSettingsAndInputsUnchanged();
    void realProcessScriptCompareAndReport();
    void realProcessScriptFailureHasLineNumber();
};

void CliTests::exitCodeContract()
{
    QCOMPARE(int(Cli::Equal), 0);
    QCOMPARE(int(Cli::Different), 1);
    QCOMPARE(int(Cli::UsageError), 2);
    QCOMPARE(int(Cli::DataError), 3);
    QCOMPARE(int(Cli::InternalError), 4);
    QCOMPARE(Cli::errorResult(Cli::InternalError, QStringLiteral("injected failure")).exitCode, 4);
}

void CliTests::nativePaths_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::newRow("home") << QStringList{};
    QTest::newRow("single") << QStringList{"left"};
    QTest::newRow("two") << QStringList{"left", "right"};
    QTest::newRow("three") << QStringList{"left", "right", "base"};
    QTest::newRow("four") << QStringList{"left", "right", "base", "output"};
}

void CliTests::nativePaths()
{
    QFETCH(QStringList, arguments);
    const auto result = Cli::parse(arguments);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.left, arguments.value(0));
    QCOMPARE(result.request.right, arguments.value(1));
    QCOMPARE(result.request.base, arguments.value(2));
    QCOMPARE(result.request.output, arguments.value(3));
}

void CliTests::pathSpellingAndTerminator()
{
    const QString left = QString::fromUtf8("/tmp/左侧 文件 \"quoted\".txt");
    auto result = Cli::parse({left, QStringLiteral("right's file.txt")}, Cli::Platform::Posix);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.left, left);
    QCOMPARE(result.request.right, QStringLiteral("right's file.txt"));
    result = Cli::parse({"--", "--silent", "@literal-script.txt"}, Cli::Platform::Posix);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.left, QStringLiteral("--silent"));
    QCOMPARE(result.request.right, QStringLiteral("@literal-script.txt"));
    QVERIFY(result.request.scriptFile.isEmpty());
    QVERIFY(!result.request.silent);
}

void CliTests::windowsAliasesAndPosixPaths()
{
    auto result = Cli::parse({"/silent", "/qc", "/leftreadonly", "/rightreadonly",
                              "/fv=Text Compare", "C:\\left file.txt", "C:\\right.txt"},
                             Cli::Platform::Windows);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.request.silent);
    QVERIFY(result.request.quickCompare);
    QVERIFY(result.request.leftReadOnly);
    QVERIFY(result.request.rightReadOnly);
    QCOMPARE(result.request.sessionType, QStringLiteral("text"));
    QCOMPARE(result.request.left, QStringLiteral("C:\\left file.txt"));
    result = Cli::parse({"/silent", "/qc"}, Cli::Platform::Posix);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.left, QStringLiteral("/silent"));
    QCOMPARE(result.request.right, QStringLiteral("/qc"));
    QVERIFY(!result.request.silent);
    QVERIFY(!result.request.quickCompare);
    result = Cli::parse({"/ordinary/path", "/another/path"}, Cli::Platform::Windows);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.left, QStringLiteral("/ordinary/path"));
    for (const auto platform : {Cli::Platform::Posix, Cli::Platform::Windows}) {
        const auto help = Cli::parse({"/?"}, platform);
        QVERIFY2(help.ok(), qPrintable(help.error));
        QVERIFY(help.request.help);
    }
}

void CliTests::namedInputs()
{
    const auto result = Cli::parse({"--base", "ancestor", "--right=theirs", "--output", "merged",
                                     "--left=ours", "--type=text-merge"});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.left, QStringLiteral("ours"));
    QCOMPARE(result.request.right, QStringLiteral("theirs"));
    QCOMPARE(result.request.base, QStringLiteral("ancestor"));
    QCOMPARE(result.request.output, QStringLiteral("merged"));
    QCOMPARE(Cli::inputPaths(result.request), QStringList({"ours", "theirs", "ancestor"}));
}

void CliTests::longAliasesAndNoLog()
{
    auto parsed = Cli::parse({"--qc", "--readonly", "--no-log-file", "left", "right"});
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QVERIFY(parsed.request.quickCompare);
    QVERIFY(parsed.request.leftReadOnly && parsed.request.rightReadOnly);
    QVERIFY(parsed.request.noLogFile);
    parsed = Cli::parse({"--leftreadonly", "--rightreadonly", "left", "right"});
    QVERIFY(parsed.ok());
    QVERIFY(parsed.request.leftReadOnly && parsed.request.rightReadOnly);
    parsed = Cli::parse({"--help-session-types"});
    QVERIFY(parsed.ok());
    QVERIFY(parsed.request.listSessionTypes);
    parsed = Cli::parse({"/readonly", "left", "right"}, Cli::Platform::Windows);
    QVERIFY(parsed.ok());
    QVERIFY(parsed.request.leftReadOnly && parsed.request.rightReadOnly);
    QVERIFY(!Cli::parse({"--quick-compare", "--qc", "left", "right"}).ok());
    QVERIFY(!Cli::parse({"--silent", "/silent", "left", "right"}, Cli::Platform::Windows).ok());
}

void CliTests::sessionTypes_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QStringList>("paths");
    QTest::addColumn<QString>("canonical");
    QTest::newRow("text-name-case") << "tExT cOmPaRe" << QStringList{"l", "r"} << "text";
    QTest::newRow("text-id") << "text" << QStringList{"l", "r"} << "text";
    QTest::newRow("merge") << "Text Merge" << QStringList{"l", "r", "b"} << "text-merge";
    QTest::newRow("edit") << "text-edit" << QStringList{"l"} << "text-edit";
    QTest::newRow("patch") << "text-patch" << QStringList{"l.patch"} << "text-patch";
    QTest::newRow("folder") << "Folder Compare" << QStringList{"l", "r"} << "folder";
}

void CliTests::sessionTypes()
{
    QFETCH(QString, type);
    QFETCH(QStringList, paths);
    QFETCH(QString, canonical);
    const auto result = Cli::parse(QStringList{"--type", type} + paths);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.request.sessionType, canonical);
}

void CliTests::optionsMapToRequest()
{
    const auto result = Cli::parse({"--silent", "--quick-compare", "--read-only", "--ignore-case",
        "--ignore-whitespace", "--exact-eol", "--ignore-final-newline", "--encoding=UTF-8",
        "--no-recursive", "--json", "--log=trace.log", "--log-level=debug", "--report=html",
        "--report-file=summary.html", "--report-on-diff-only", "--new-instance", "--wait", "l", "r"});
    QVERIFY2(result.ok(), qPrintable(result.error));
    const auto &r = result.request;
    QVERIFY(r.silent && r.quickCompare && r.leftReadOnly && r.rightReadOnly);
    QVERIFY(r.textOptions.ignoreCase);
    QVERIFY(r.textOptions.whitespace != Text::Whitespace::Exact);
    QVERIFY(!r.textOptions.ignoreEol);
    QVERIFY(r.textOptions.ignoreFinalNewline);
    QCOMPARE(r.encoding, QByteArray("UTF-8"));
    QVERIFY(!r.recursive && r.json && r.reportOnDiffOnly && r.newInstance && r.wait);
    QCOMPARE(r.logFile, QStringLiteral("trace.log"));
    QCOMPARE(r.logLevel, QStringLiteral("debug"));
    QCOMPARE(r.reportFormat, QStringLiteral("html"));
    QCOMPARE(r.reportFile, QStringLiteral("summary.html"));
    QVERIFY(Cli::requiresHeadless(r));
    const auto ignore = Cli::parse({"--ignore-eol", "l", "r"});
    QVERIFY(ignore.ok());
    QVERIFY(ignore.request.textOptions.ignoreEol);
    const auto readonly = Cli::parse({"--left-read-only", "--right-read-only", "l", "r"});
    QVERIFY(readonly.ok());
    QVERIFY(readonly.request.leftReadOnly && readonly.request.rightReadOnly);
}

// TXT-005 标准 4 的前半段：开关与阈值要能被命令行指定。
//
// 这一条是新加的——变异测试（把「越界报错」改成「静默接受」）第一轮**漏检**，
// 因为 `Code/Tests/Cli/` 里根本没有 `--similarity-threshold` / `--similar-lines`
// 的任何用例。完成标准里写着「并可用于命令行」，而当时只有实现、没有断言。
void CliTests::similarityOptionsMapToTextOptions()
{
    const auto on = Cli::parse({"--similar-lines", "--similarity-threshold=70", "l", "r"});
    QVERIFY2(on.ok(), qPrintable(on.error));
    QVERIFY(on.request.textOptions.alignSimilarLines);
    QCOMPARE(on.request.textOptions.similarityThreshold, 70);

    const auto off = Cli::parse({"--no-similar-lines", "l", "r"});
    QVERIFY2(off.ok(), qPrintable(off.error));
    QVERIFY(!off.request.textOptions.alignSimilarLines);
    // 没写就保持出厂值，命令行不得悄悄改掉它。
    QCOMPARE(off.request.textOptions.similarityThreshold, Text::defaultSimilarityThreshold());

    // 两个边界值都必须接受（0 与 100 是合法值，不是「越界」）。
    for (const int percent : {0, 100}) {
        const auto edge = Cli::parse({"--similarity-threshold=" + QString::number(percent), "l", "r"});
        QVERIFY2(edge.ok(), qPrintable(edge.error));
        QCOMPARE(edge.request.textOptions.similarityThreshold, percent);
    }
}

// 同一条标准的后半段：这些参数要真的走到比对引擎，而不是只被记下来。
//
// 这里要讲清一件容易误解的事：**命令行摘要里的 `differences` 不随这两个开关变**。
// 那个数是「一处改动」的个数（与界面状态栏同源，见 `cliexecution.cpp`），
// 而阈值改变的是**一处改动内部的块粒度**——够像的行配成一块替换、不够像的拆成
// 删除 + 新增，改动站点数两者相同。所以下面同时钉住两件事：
//   ① 摘要字段三种跑法都一样（它一旦随开关变，就说明有人把命令行改成了按块计数，
//      那时状态栏说 2 处、命令行说 3 处，正是本仓列为坑的「同一件事两套口径」）；
//   ② 块粒度**确实**被改变了（直接在引擎上复算一遍，免得①被误读成「参数没生效」）。
void CliTests::similarityOptionsAreUsableFromTheCommandLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto l = dir.filePath(QStringLiteral("left.txt"));
    const auto r = dir.filePath(QStringLiteral("right.txt"));
    const QByteArray leftBytes("a\nb\nc\nd\n"), rightBytes("a\nnew\nb\nchanged\nd\n");
    QVERIFY(writeBytes(l, leftBytes));
    QVERIFY(writeBytes(r, rightBytes));
    const auto before = fileSnapshot(dir.path());

    const auto byDefault = comparePaths(l, r);
    const auto loose = comparePaths(l, r, {QStringLiteral("--similarity-threshold=0")});
    const auto off = comparePaths(l, r, {QStringLiteral("--no-similar-lines")});
    for (const auto *result : {&byDefault, &loose, &off}) {
        QCOMPARE(result->exitCode, int(Cli::Different));
        QVERIFY2(result->standardError.isEmpty(), qPrintable(result->standardError));
        QCOMPARE(result->summary.value(QStringLiteral("differences")).toInt(), 2);
    }
    // 比对不得改动输入。
    QCOMPARE(fileSnapshot(dir.path()), before);

    // 出厂阈值（50）下 `c` 与 `changed` 的相似度只有 25，不配对；
    // 阈值放到 0 就把这一处配成一块替换，于是块数 3 → 2。
    const auto leftLines = Text::Document::splitLines(QString::fromUtf8(leftBytes));
    const auto rightLines = Text::Document::splitLines(QString::fromUtf8(rightBytes));
    const auto strict = Text::compare(leftLines, rightLines, Text::CompareOptions());
    QCOMPARE(strict.differences.size(), 3);
    Text::CompareOptions looseOptions;
    looseOptions.similarityThreshold = 0;
    const auto paired = Text::compare(leftLines, rightLines, looseOptions);
    QCOMPARE(paired.differences.size(), 2);
    Text::CompareOptions positional;
    positional.alignSimilarLines = false;
    QCOMPARE(Text::compare(leftLines, rightLines, positional).differences.size(), 3);

    // 越界与非数字在**真实进程**里也是用法错误（退出码 2），不是静默钳制：
    // 命令行是脚本用的，把 250 悄悄变成 100 会让脚本作者以为参数生效了。
    for (const QStringList &bad : {QStringList{QStringLiteral("--similarity-threshold=101")},
                                   QStringList{QStringLiteral("--similarity-threshold=-1")},
                                   QStringList{QStringLiteral("--similarity-threshold=abc")},
                                   QStringList{QStringLiteral("--similarity-threshold=70.5")},
                                   QStringList{QStringLiteral("--similar-lines"),
                                               QStringLiteral("--no-similar-lines")}}) {
        const auto result = runProbe(bad + QStringList{QStringLiteral("--silent"), l, r});
        QVERIFY2(result.finished, result.err.constData());
        QCOMPARE(result.status, QProcess::NormalExit);
        QCOMPARE(result.exitCode, int(Cli::UsageError));
        QVERIFY2(!result.err.trimmed().isEmpty(), qPrintable(bad.join(QLatin1Char(' '))));
    }
}

void CliTests::scriptArguments()
{
    for (const auto &args : {QStringList{"@jobs/run.lqs"}, QStringList{"--script=jobs/run.lqs"}}) {
        const auto result = Cli::parse(args + QStringList{"--script-arg", "name=value=tail",
                                                        "--script-arg=empty=", "--continue-on-error"});
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.request.scriptFile, QStringLiteral("jobs/run.lqs"));
        QCOMPARE(result.request.scriptArguments.value("name"), QStringLiteral("value=tail"));
        QVERIFY(result.request.scriptArguments.contains("empty"));
        QVERIFY(result.request.scriptArguments.value("empty").isEmpty());
        QVERIFY(result.request.continueOnError);
        QVERIFY(Cli::requiresHeadless(result.request));
    }
}

void CliTests::invalidArguments_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::newRow("unknown") << QStringList{"--nonsense"};
    QTest::newRow("too-many") << QStringList{"a", "b", "c", "d", "e"};
    QTest::newRow("empty-path") << QStringList{"", "b"};
    QTest::newRow("missing-type") << QStringList{"--type"};
    QTest::newRow("missing-left") << QStringList{"--left"};
    QTest::newRow("empty-left") << QStringList{"--left=", "--right=b"};
    QTest::newRow("missing-report") << QStringList{"--report"};
    QTest::newRow("missing-encoding") << QStringList{"--encoding"};
    QTest::newRow("duplicate-silent") << QStringList{"--silent", "--silent", "a", "b"};
    QTest::newRow("duplicate-left") << QStringList{"--left=a", "--left=b", "--right=c"};
    QTest::newRow("duplicate-type") << QStringList{"--type=text", "--type=folder", "a", "b"};
    QTest::newRow("mixed-path-style") << QStringList{"--left=a", "b"};
    QTest::newRow("unknown-type") << QStringList{"--type=imaginary", "a", "b"};
    QTest::newRow("edit-two") << QStringList{"--type=text-edit", "a", "b"};
    QTest::newRow("text-three") << QStringList{"--type=text", "a", "b", "c"};
    QTest::newRow("merge-two") << QStringList{"--type=text-merge", "a", "b"};
    QTest::newRow("flag-value") << QStringList{"--silent=false", "a", "b"};
    QTest::newRow("unknown-report") << QStringList{"--report=pdf", "--report-file=out.pdf", "a", "b"};
    QTest::newRow("unknown-log-level") << QStringList{"--log-level=banana", "a", "b"};
    QTest::newRow("script-duplicate") << QStringList{"--script=a", "@b"};
    QTest::newRow("script-and-input") << QStringList{"@a", "left", "right"};
    QTest::newRow("script-bad-argument") << QStringList{"@a", "--script-arg=no-equals"};
    QTest::newRow("script-empty-name") << QStringList{"@a", "--script-arg==value"};
    QTest::newRow("script-duplicate-name") << QStringList{"@a", "--script-arg=x=1", "--script-arg=x=2"};
    QTest::newRow("log-conflict") << QStringList{"--log=out.log", "--no-log-file", "a", "b"};
    QTest::newRow("eol-conflict") << QStringList{"--ignore-eol", "--exact-eol", "a", "b"};
    // TXT-005：阈值越界在命令行是**报错**而不是钳制（会话文件那条路才钳制，
    // 因为那里是用户手改出来的，报错就没人能修）。
    QTest::newRow("similarity-too-large") << QStringList{"--similarity-threshold=101", "a", "b"};
    QTest::newRow("similarity-negative") << QStringList{"--similarity-threshold=-1", "a", "b"};
    QTest::newRow("similarity-not-numeric") << QStringList{"--similarity-threshold=abc", "a", "b"};
    QTest::newRow("similarity-fractional") << QStringList{"--similarity-threshold=70.5", "a", "b"};
    QTest::newRow("similarity-needs-value") << QStringList{"--similarity-threshold", "a", "b"};
    QTest::newRow("similar-lines-conflict")
        << QStringList{"--similar-lines", "--no-similar-lines", "a", "b"};
    QTest::newRow("report-needs-destination") << QStringList{"--report=txt", "a", "b"};
    QTest::newRow("report-needs-format") << QStringList{"--report-file=out.txt", "a", "b"};
    QTest::newRow("report-on-diff-needs-report") << QStringList{"--report-on-diff-only", "a", "b"};
    QTest::newRow("named-right-needs-left") << QStringList{"--right=b"};
    QTest::newRow("unknown-codec") << QStringList{"--encoding=Imaginary-Codec", "a", "b"};
    QTest::newRow("unknown-help-topic") << QStringList{"--help=imaginary-option"};
    QTest::newRow("script-arg-needs-script") << QStringList{"--script-arg=x=1"};
    QTest::newRow("continue-needs-script") << QStringList{"--continue-on-error"};
}

void CliTests::invalidArguments()
{
    QFETCH(QStringList, arguments);
    const auto result = Cli::parse(arguments);
    QVERIFY2(!result.ok(), qPrintable(arguments.join(QLatin1Char(' '))));
    QVERIFY(!result.error.trimmed().isEmpty());
}

void CliTests::unknownOptionSuggestsClosest()
{
    const auto result = Cli::parse({"--silen", "left", "right"});
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(QStringLiteral("silent")), qPrintable(result.error));
}

void CliTests::generatedHelpAndDiscovery()
{
    const QString help = Cli::helpText();
    QVERIFY(!Cli::options().isEmpty());
    for (const auto &option : Cli::options()) {
        QVERIFY2(help.contains(option.name), qPrintable(option.name));
        QVERIFY2(!option.description.isEmpty(), qPrintable(option.name));
    }
    for (const auto &args : {QStringList{"--help"}, QStringList{"-h"}, QStringList{"/?"}}) {
        const auto parsed = Cli::parse(args);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error));
        QVERIFY(parsed.request.help);
        const auto result = Cli::execute(parsed.request);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.standardOutput.contains(QStringLiteral("--silent")));
    }
    const auto topic = Cli::parse({"--help=silent"});
    QVERIFY2(topic.ok(), qPrintable(topic.error));
    QCOMPARE(topic.request.helpTopic, QStringLiteral("silent"));
    QVERIFY(Cli::execute(topic.request).standardOutput.contains(QStringLiteral("silent")));
    for (const QString &flag : {QStringLiteral("--version"), QStringLiteral("-v")}) {
        const auto version = Cli::parse({flag});
        QVERIFY(version.ok());
        QVERIFY(version.request.version);
        QVERIFY(Cli::execute(version.request, QStringLiteral("test-42")).standardOutput.contains("test-42"));
    }
    for (const QString &flag : {QStringLiteral("--list-session-types"), QStringLiteral("--print-exit-codes")}) {
        const auto parsed = Cli::parse({flag});
        QVERIFY(parsed.ok());
        QVERIFY(Cli::requiresHeadless(parsed.request));
        const auto result = Cli::execute(parsed.request);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(!result.standardOutput.isEmpty());
    }
    QVERIFY(Cli::sessionTypesText().contains(QStringLiteral("text-merge")));
    QVERIFY(Cli::exitCodesText().contains(QLatin1Char('4')));
}

void CliTests::parseDoesNotReadFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto before = fileSnapshot(dir.path());
    const auto result = Cli::parse({"--silent", "--type=folder", dir.filePath("missing-left"),
                                    dir.filePath("missing-right")});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(fileSnapshot(dir.path()), before);
}

void CliTests::textComparison_data()
{
    QTest::addColumn<QByteArray>("left");
    QTest::addColumn<QByteArray>("right");
    QTest::addColumn<QStringList>("flags");
    QTest::addColumn<int>("code");
    QTest::newRow("same") << QByteArray("hello\n") << QByteArray("hello\n") << QStringList{} << 0;
    QTest::newRow("different") << QByteArray("hello\n") << QByteArray("world\n") << QStringList{} << 1;
    QTest::newRow("empty") << QByteArray() << QByteArray() << QStringList{} << 0;
    QTest::newRow("case-sensitive") << QByteArray("Hello\n") << QByteArray("hello\n") << QStringList{} << 1;
    QTest::newRow("ignore-case") << QByteArray("Hello\n") << QByteArray("hello\n") << QStringList{"--ignore-case"} << 0;
    QTest::newRow("spaces-significant") << QByteArray("a b\n") << QByteArray("a  b\n") << QStringList{} << 1;
    QTest::newRow("ignore-spaces") << QByteArray("a b\n") << QByteArray("a  b\n") << QStringList{"--ignore-whitespace"} << 0;
    QTest::newRow("ignore-eol") << QByteArray("hello\r\n") << QByteArray("hello\n") << QStringList{"--ignore-eol"} << 0;
    QTest::newRow("exact-eol") << QByteArray("hello\r\n") << QByteArray("hello\n") << QStringList{"--exact-eol"} << 1;
    QTest::newRow("final-newline") << QByteArray("hello\n") << QByteArray("hello") << QStringList{} << 1;
    QTest::newRow("ignore-final-newline") << QByteArray("hello\n") << QByteArray("hello") << QStringList{"--ignore-final-newline"} << 0;
    QTest::newRow("utf8-bom") << (QByteArray::fromHex("efbbbf") + "hello\n") << QByteArray("hello\n") << QStringList{} << 0;
    QTest::newRow("explicit-encoding") << QByteArray::fromHex("636166e90a") << QByteArray::fromHex("636166e90a") << QStringList{"--encoding=ISO-8859-1"} << 0;
    QTest::newRow("read-only") << QByteArray("left\n") << QByteArray("right\n") << QStringList{"--read-only"} << 1;
}

void CliTests::textComparison()
{
    QFETCH(QByteArray, left);
    QFETCH(QByteArray, right);
    QFETCH(QStringList, flags);
    QFETCH(int, code);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto l = dir.filePath(QString::fromUtf8("左 side.txt"));
    const auto r = dir.filePath(QString::fromUtf8("右 side.txt"));
    QVERIFY(writeBytes(l, left));
    QVERIFY(writeBytes(r, right));
    const auto before = fileSnapshot(dir.path());
    const auto result = comparePaths(l, r, flags);
    QCOMPARE(result.exitCode, code);
    QVERIFY2(result.standardError.isEmpty(), qPrintable(result.standardError));
    QVERIFY2(result.standardOutput.startsWith(QStringLiteral("result\t")), qPrintable(result.standardOutput));
    QCOMPARE(fileSnapshot(dir.path()), before);
}

void CliTests::unreadableOrUnsupportedInputs_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("code");
    QTest::newRow("missing") << "missing" << 3;
    QTest::newRow("binary") << "binary" << 3;
    QTest::newRow("invalid-utf8") << "invalid-utf8" << 3;
    QTest::newRow("mixed-kinds") << "mixed-kinds" << 2;
    QTest::newRow("folder-files") << "folder-files" << 2;
    QTest::newRow("text-dirs") << "text-dirs" << 2;
    QTest::newRow("unsupported-merge") << "unsupported-merge" << 2;
}

void CliTests::unreadableOrUnsupportedInputs()
{
    QFETCH(QString, scenario);
    QFETCH(int, code);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString left = dir.filePath("left.txt"), right = dir.filePath("right.txt");
    QVERIFY(writeBytes(left, "hello\n"));
    QVERIFY(writeBytes(right, "hello\n"));
    QStringList flags;
    QString l = left, r = right;
    if (scenario == "missing") l = dir.filePath("missing.txt");
    if (scenario == "binary") QVERIFY(writeBytes(left, QByteArray("hello\0there", 11)));
    if (scenario == "invalid-utf8") QVERIFY(writeBytes(left, QByteArray::fromHex("c3280a")));
    if (scenario == "mixed-kinds" || scenario == "text-dirs") l = dir.path();
    if (scenario == "text-dirs") { r = dir.path(); flags << "--type=text"; }
    if (scenario == "folder-files") flags << "--type=folder";
    if (scenario == "unsupported-merge") flags << "--type=text-merge";
    const auto before = fileSnapshot(dir.path());
    auto parsed = Cli::parse(QStringList{"--silent"} + flags + QStringList{l, r}
                            + (scenario == "unsupported-merge" ? QStringList{left} : QStringList{}));
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    const auto result = Cli::execute(parsed.request);
    QCOMPARE(result.exitCode, code);
    QVERIFY(!result.standardError.trimmed().isEmpty());
    QCOMPARE(fileSnapshot(dir.path()), before);
}

void CliTests::folderComparison()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left"), r = dir.filePath("right");
    QVERIFY(QDir().mkpath(l + "/sub"));
    QVERIFY(QDir().mkpath(r + "/sub"));
    QVERIFY(writeBytes(l + "/sub/item.txt", "same\n"));
    QVERIFY(writeBytes(r + "/sub/item.txt", "same\n"));
    QCOMPARE(comparePaths(l, r).exitCode, 0);
    QVERIFY(writeBytes(r + "/sub/item.txt", "different\n"));
    const auto before = fileSnapshot(dir.path());
    QCOMPARE(comparePaths(l, r).exitCode, 1);
    QCOMPARE(fileSnapshot(dir.path()), before);
    QVERIFY(QFile::remove(r + "/sub/item.txt"));
    QCOMPARE(comparePaths(l, r).exitCode, 1);
}

void CliTests::folderRecursion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left"), r = dir.filePath("right");
    QVERIFY(QDir().mkpath(l + "/sub"));
    QVERIFY(QDir().mkpath(r + "/sub"));
    QVERIFY(writeBytes(l + "/sub/item.txt", "left\n"));
    QVERIFY(writeBytes(r + "/sub/item.txt", "right\n"));
    QCOMPARE(comparePaths(l, r).exitCode, 1);
    QCOMPARE(comparePaths(l, r, {"--no-recursive"}).exitCode, 0);
}

void CliTests::reports_data()
{
    QTest::addColumn<QString>("format");
    for (const char *format : {"txt", "csv", "json", "html"}) QTest::newRow(format) << QString::fromLatin1(format);
}

void CliTests::reports()
{
    QFETCH(QString, format);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt");
    const QString output = dir.filePath("summary." + format);
    QVERIFY(writeBytes(l, "left\n"));
    QVERIFY(writeBytes(r, "right\n"));
    const auto result = comparePaths(l, r, {"--report=" + format, "--report-file=" + output});
    QCOMPARE(result.exitCode, 1);
    const auto bytes = readBytes(output);
    QVERIFY(!bytes.isEmpty());
    QCOMPARE(readBytes(l), QByteArray("left\n"));
    QCOMPARE(readBytes(r), QByteArray("right\n"));
    if (format == "json") QVERIFY(QJsonDocument::fromJson(bytes).isObject());
    if (format == "html") QVERIFY(bytes.contains("<html") || bytes.contains("<!DOCTYPE html"));
    if (format == "csv") QVERIFY(bytes.contains(','));
}

void CliTests::reportOnlyOnDifference()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt");
    const QString output = dir.filePath("summary.txt");
    QVERIFY(writeBytes(l, "same\n"));
    QVERIFY(writeBytes(r, "same\n"));
    const QStringList flags{"--report=txt", "--report-file=" + output, "--report-on-diff-only"};
    QCOMPARE(comparePaths(l, r, flags).exitCode, 0);
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(writeBytes(output, "old report sentinel"));
    QCOMPARE(comparePaths(l, r, flags).exitCode, 0);
    QCOMPARE(readBytes(output), QByteArray("old report sentinel"));
    QVERIFY(writeBytes(r, "changed\n"));
    QCOMPARE(comparePaths(l, r, flags).exitCode, 1);
    QVERIFY(readBytes(output) != QByteArray("old report sentinel"));
}

void CliTests::outputFailures()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left"), r = dir.filePath("right");
    QVERIFY(writeBytes(l, "same\n"));
    QVERIFY(writeBytes(r, "same\n"));
    const auto report = comparePaths(l, r, {"--report=txt", "--report-file=" + dir.path()});
    QCOMPARE(report.exitCode, 3);
    QVERIFY(!report.standardError.isEmpty());
    const auto log = comparePaths(l, r, {"--log=" + dir.path()});
    QCOMPARE(log.exitCode, 3);
    QVERIFY(!log.standardError.isEmpty());
}

void CliTests::outputCannotReplaceInputs_data()
{
    QTest::addColumn<QString>("outputKind");
    QTest::addColumn<bool>("rightSide");
    QTest::newRow("report-left") << "report" << false;
    QTest::newRow("report-right") << "report" << true;
    QTest::newRow("log-left") << "log" << false;
    QTest::newRow("log-right") << "log" << true;
}

void CliTests::outputCannotReplaceInputs()
{
    QFETCH(QString, outputKind);
    QFETCH(bool, rightSide);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt");
    QVERIFY(writeBytes(l, "left input\n"));
    QVERIFY(writeBytes(r, "right input\n"));
    const QString output = rightSide ? r : l;
    const QStringList flags = outputKind == "log" ? QStringList{"--log=" + output}
        : QStringList{"--report=txt", "--report-file=" + output};
    const auto before = fileSnapshot(dir.path());
    const auto result = comparePaths(l, r, flags + QStringList{"--read-only"});
    QCOMPARE(result.exitCode, 3);
    QVERIFY(!result.standardError.isEmpty());
    QCOMPARE(fileSnapshot(dir.path()), before);
}

void CliTests::outputCannotWriteInsideInputDirectories()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left"), r = dir.filePath("right");
    QVERIFY(QDir().mkpath(l + "/sub"));
    QVERIFY(QDir().mkpath(r));
    QVERIFY(writeBytes(l + "/item.txt", "left input\n"));
    const auto before = fileSnapshot(dir.path());
    for (const auto &target : {l + "/sub/report.txt", r + "/new.txt"}) {
        QCOMPARE(comparePaths(l, r, {"--report=txt", "--report-file=" + target}).exitCode, 3);
        QCOMPARE(comparePaths(l, r, {"--log=" + target}).exitCode, 3);
        QVERIFY(!QFileInfo::exists(target));
    }
    QCOMPARE(fileSnapshot(dir.path()), before);
}

void CliTests::symlinkOutputAliasesAreRejected()
{
#ifdef Q_OS_WIN
    QSKIP("QFile::link creates shortcuts on Windows; POSIX symbolic-link coverage only.");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt"), alias = dir.filePath("alias.txt");
    QVERIFY(writeBytes(l, "left input\n"));
    QVERIFY(writeBytes(r, "right input\n"));
    QVERIFY(QFile::link(l, alias));
    QCOMPARE(comparePaths(l, r, {"--log=" + alias}).exitCode, 3);
    QCOMPARE(comparePaths(l, r, {"--report=txt", "--report-file=" + alias}).exitCode, 3);
    QCOMPARE(readBytes(l), QByteArray("left input\n"));
    QVERIFY(QFileInfo(alias).isSymLink());
    const QString folder = dir.filePath("folder"), folderAlias = dir.filePath("folder-alias");
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(QFile::link(folder, folderAlias));
    QString error;
    QVERIFY(!Cli::isSafeOutputPath(folderAlias + "/new/deep/report.txt", {folder}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(folder + "/new"));
#endif
}

void CliTests::safeOutputSiblingsAndProtectedPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath("input"), sibling = dir.filePath("input-other/report.txt");
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(QDir().mkpath(dir.filePath("input-other")));
    QString error;
    QVERIFY2(Cli::isSafeOutputPath(sibling, {folder}, &error), qPrintable(error));
    const QString script = dir.filePath("job.lqs");
    QVERIFY(writeBytes(script, "load left right\ncompare\n"));
    QVERIFY(!Cli::isSafeOutputPath(script, {script}, &error));
    auto result = Cli::errorResult(Cli::Different, QStringLiteral("difference"));
    QVERIFY(!Cli::writeReport(script, "txt", result, {script}, &error));
    QCOMPARE(readBytes(script), QByteArray("load left right\ncompare\n"));
}

void CliTests::hardLinkOutputAliasesAreRejected()
{
#ifndef Q_OS_UNIX
    QSKIP("POSIX hard-link coverage only.");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt"), alias = dir.filePath("alias.txt");
    QVERIFY(writeBytes(l, "left input\n"));
    QVERIFY(writeBytes(r, "right input\n"));
    QCOMPARE(::link(QFile::encodeName(l).constData(), QFile::encodeName(alias).constData()), 0);
    QCOMPARE(comparePaths(l, r, {"--log=" + alias}).exitCode, 3);
    QCOMPARE(comparePaths(l, r, {"--report=txt", "--report-file=" + alias}).exitCode, 3);
    QCOMPARE(readBytes(l), QByteArray("left input\n"));
    QCOMPARE(readBytes(alias), QByteArray("left input\n"));
#endif
}

void CliTests::reportAndLogCannotCollide()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt"), target = dir.filePath("output.txt");
    QVERIFY(writeBytes(l, "left\n"));
    QVERIFY(writeBytes(r, "right\n"));
    const auto result = comparePaths(l, r, {"--report=txt", "--report-file=" + target, "--log=" + target});
    QCOMPARE(result.exitCode, 3);
    QVERIFY(!QFileInfo::exists(target));
    QCOMPARE(readBytes(l), QByteArray("left\n"));
    QCOMPARE(readBytes(r), QByteArray("right\n"));
}

void CliTests::machineOutputEscapesFields()
{
    const QString message = QStringLiteral("missing\tpath\nwith\\slashes\r");
    const auto error = Cli::errorResult(Cli::DataError, message, true);
    QCOMPARE(error.standardError.count(QLatin1Char('\n')), 1);
    QCOMPARE(error.standardError.count(QLatin1Char('\t')), 2);
    QVERIFY(error.standardError.contains(QStringLiteral("\\t")));
    QVERIFY(error.standardError.contains(QStringLiteral("\\n")));
    const auto json = QJsonDocument::fromJson(error.standardOutput.toUtf8());
    QVERIFY(json.isObject());
    QCOMPARE(json.object().value("error").toString(), message);
    QCOMPARE(json.object().value("exitCode").toInt(), 3);
#ifndef Q_OS_WIN
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath(QStringLiteral("left\tfile\n.txt")), r = dir.filePath("right.txt");
    QVERIFY(writeBytes(l, "same\n"));
    QVERIFY(writeBytes(r, "same\n"));
    const auto result = comparePaths(l, r);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.standardOutput.count(QLatin1Char('\n')), 1);
    QCOMPARE(result.standardOutput.count(QLatin1Char('\t')), 5);
    QVERIFY(result.standardOutput.contains(QStringLiteral("left\\tfile\\n.txt")));
#endif
}

void CliTests::htmlReportEscapesPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Cli::ExecutionResult result;
    result.summary = {{"status", "equal"}, {"left", "<script>alert('x')</script>"},
                      {"right", "a&b\"quoted\""}, {"type", "text"}, {"differences", 0}, {"exitCode", 0}};
    const QString output = dir.filePath("summary.html");
    QString error;
    QVERIFY2(Cli::writeReport(output, "html", result, {}, &error), qPrintable(error));
    const auto html = readBytes(output);
    QVERIFY(!html.contains("<script>"));
    QVERIFY(html.contains("&lt;script&gt;"));
    QVERIFY(html.contains("a&amp;b&quot;quoted&quot;"));
}

void CliTests::explicitLogs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left"), r = dir.filePath("right"), log = dir.filePath("result.log");
    QVERIFY(writeBytes(l, "left\n"));
    QVERIFY(writeBytes(r, "right\n"));
    const auto result = comparePaths(l, r, {"--log=" + log, "--log-level=debug"});
    QCOMPARE(result.exitCode, 1);
    QVERIFY(!readBytes(log).isEmpty());
    QCOMPARE(readBytes(l), QByteArray("left\n"));
    QCOMPARE(readBytes(r), QByteArray("right\n"));
}

void CliTests::realProcessExitCodes_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("code");
    QTest::newRow("equal") << "equal" << 0;
    QTest::newRow("different") << "different" << 1;
    QTest::newRow("usage") << "usage" << 2;
    QTest::newRow("missing-input") << "missing" << 3;
    QTest::newRow("output-failure") << "output" << 3;
}

void CliTests::realProcessExitCodes()
{
    QFETCH(QString, scenario);
    QFETCH(int, code);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt");
    QVERIFY(writeBytes(l, "same\n"));
    QVERIFY(writeBytes(r, scenario == "different" ? "different\n" : "same\n"));
    QStringList args{"--silent", l, r};
    if (scenario == "usage") args = QStringList{"--silen"};
    if (scenario == "missing") args = QStringList{"--silent", l, dir.filePath("missing")};
    if (scenario == "output") args << "--report=txt" << "--report-file=" + dir.path();
    const auto result = runProbe(args);
    QVERIFY2(result.finished, result.err.constData());
    QCOMPARE(result.status, QProcess::NormalExit);
    QCOMPARE(result.exitCode, code);
    if (code < 2) {
        QVERIFY(result.err.isEmpty());
        QVERIFY2(result.out.startsWith("result\t"), result.out.constData());
    } else {
        QVERIFY(!result.err.isEmpty());
    }
}

void CliTests::realProcessJsonAndUtf8()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath(QString::fromUtf8("左 文件.txt")), r = dir.filePath(QString::fromUtf8("右 文件.txt"));
    QVERIFY(writeBytes(l, QString::fromUtf8("你好\n").toUtf8()));
    QVERIFY(writeBytes(r, QString::fromUtf8("世界\n").toUtf8()));
    const auto result = runProbe({"--silent", "--json", l, r});
    QVERIFY2(result.finished, result.err.constData());
    QCOMPARE(result.exitCode, 1);
    QVERIFY(result.err.isEmpty());
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(result.out, &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(json.isObject());
    QVERIFY(!json.object().isEmpty());
    QCOMPARE(QString::fromUtf8(result.out).toUtf8(), result.out);
}

void CliTests::realProcessHelpAndVersion()
{
    for (const auto &args : {QStringList{"--help"}, QStringList{"/?"}, QStringList{"--version"},
                            QStringList{"--list-session-types"}, QStringList{"--print-exit-codes"}}) {
        const auto result = runProbe(args);
        QVERIFY2(result.finished, result.err.constData());
        QCOMPARE(result.status, QProcess::NormalExit);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.err.isEmpty());
        QVERIFY(!result.out.isEmpty());
        if (args.first() == "--version") QVERIFY(result.out.contains("probe-1.0"));
    }
}

void CliTests::realProcessSettingsAndInputsUnchanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString config = dir.filePath("configuration");
    QVERIFY(QDir().mkpath(config + "/LqCompareTests"));
    const QString settings = config + "/LqCompareTests/LqCompareCliProbe.ini";
    QVERIFY(writeBytes(settings, "[comparison]\nignoreCase=false\ncustom=sentinel\n"));
    const QString l = dir.filePath("left.txt"), r = dir.filePath("right.txt");
    QVERIFY(writeBytes(l, "Hello  World\n"));
    QVERIFY(writeBytes(r, "hello world\n"));
    const auto before = fileSnapshot(dir.path());
    const auto result = runProbe({"--silent", "--ignore-case", "--ignore-whitespace", "--read-only", l, r}, config);
    QVERIFY2(result.finished, result.err.constData());
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(fileSnapshot(dir.path()), before);
}

void CliTests::realProcessScriptCompareAndReport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString l = dir.filePath("left file.txt"), r = dir.filePath("right file.txt");
    const QString script = dir.filePath("job.lqs"), report = dir.filePath("result.json");
    QVERIFY(writeBytes(l, "left\n"));
    QVERIFY(writeBytes(r, "right\n"));
    const QByteArray source("load 'left file.txt' 'right file.txt'\ncompare\nreport json result.json\n");
    QVERIFY(writeBytes(script, source));
    // QProcess inherits the build-directory CWD, so these relative paths can
    // succeed only if they are resolved from the script's own directory.
    const auto result = runProbe({"--script=" + script});
    QVERIFY2(result.finished, result.err.constData());
    QCOMPARE(result.status, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 1);
    QVERIFY2(result.err.isEmpty(), result.err.constData());
    const auto json = QJsonDocument::fromJson(readBytes(report));
    QVERIFY(json.isObject());
    QCOMPARE(json.object().value("status").toString(), QStringLiteral("different"));
    QCOMPARE(json.object().value("exitCode").toInt(), 1);
    QCOMPARE(readBytes(l), QByteArray("left\n"));
    QCOMPARE(readBytes(r), QByteArray("right\n"));
    QCOMPARE(readBytes(script), source);
}

void CliTests::realProcessScriptFailureHasLineNumber()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString right = dir.filePath("right.txt"), script = dir.filePath("job.lqs");
    QVERIFY(writeBytes(right, "right\n"));
    QVERIFY(writeBytes(script, "# line one\ncompare missing.txt right.txt\nprint SHOULD_NOT_RUN\n"));
    const auto result = runProbe({"@" + script});
    QVERIFY2(result.finished, result.err.constData());
    QCOMPARE(result.status, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);
    QVERIFY(result.err.contains("\t2\tcompare\t"));
    QVERIFY(!result.out.contains("SHOULD_NOT_RUN"));
    QVERIFY(!QFileInfo::exists(dir.filePath("missing.txt")));
    QCOMPARE(readBytes(right), QByteArray("right\n"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("LQCOMPARE_CLI_PROBE_MODE")) return runCliProbe(app);
    CliTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_cli.moc"
