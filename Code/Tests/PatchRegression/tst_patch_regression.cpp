#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "patch.h"

using namespace LqCompare::Patch;

namespace {
QByteArray diagnostics(const QVector<Diagnostic> &items)
{
    QByteArray out;
    for (const Diagnostic &item : items)
        out += QByteArray::number(item.line) + ": " + item.message.toUtf8() + '\n';
    return out;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read test fixture: %s", qPrintable(path));
    return file.readAll();
}

QByteArray fixture(const QString &name)
{
    return readBytes(QTest::qFindTestData(qPrintable(QStringLiteral("fixtures/") + name),
                                        __FILE__, __LINE__));
}

FileInput modified(const QByteArray &before, const QByteArray &after,
                   const QString &name = QStringLiteral("sample.txt"))
{
    FileInput file;
    file.oldPath = name;
    file.newPath = name;
    file.oldBytes = before;
    file.newBytes = after;
    return file;
}

Document documentWithPaths(const QString &oldPath, const QString &newPath)
{
    FilePatch file;
    file.oldPath = oldPath;
    file.newPath = newPath;
    file.patchLine = 1;
    Hunk hunk;
    hunk.oldStart = oldPath == QStringLiteral("/dev/null") ? 0 : 1;
    hunk.oldCount = oldPath == QStringLiteral("/dev/null") ? 0 : 1;
    hunk.newStart = newPath == QStringLiteral("/dev/null") ? 0 : 1;
    hunk.newCount = newPath == QStringLiteral("/dev/null") ? 0 : 1;
    hunk.patchLine = 3;
    if (hunk.oldCount) hunk.lines.push_back({'-', QByteArray("old\n"), 4});
    if (hunk.newCount) hunk.lines.push_back({'+', QByteArray("new\n"), 5});
    file.hunks.push_back(hunk);
    return {{file}};
}

struct ProcessResult {
    bool finished = false;
    int exitCode = -1;
    QByteArray output;
};

ProcessResult runProcess(const QString &program, const QStringList &arguments,
                         const QString &directory)
{
    QProcess process;
    process.setWorkingDirectory(directory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    ProcessResult result;
    if (!process.waitForStarted(5000)) {
        result.output = process.errorString().toUtf8();
        return result;
    }
    result.finished = process.waitForFinished(30000);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(5000);
    }
    result.exitCode = process.exitCode();
    result.output = process.readAll();
    return result;
}
}

class PatchRegressionTests : public QObject {
    Q_OBJECT
private slots:
    void parseIndependentFixtures_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("oldPath");
        QTest::addColumn<QString>("newPath");
        QTest::addColumn<int>("strip");
        QTest::newRow("git-with-index") << QStringLiteral("git-index.diff")
            << QStringLiteral("a/sample.txt") << QStringLiteral("b/sample.txt") << 1;
        QTest::newRow("svn-with-Index") << QStringLiteral("svn-index.diff")
            << QStringLiteral("sample.txt") << QStringLiteral("sample.txt") << 0;
        QTest::newRow("GNU-with-timestamps") << QStringLiteral("gnu.diff")
            << QStringLiteral("old/sample.txt") << QStringLiteral("new/sample.txt") << 1;
        QTest::newRow("hg-with-revision") << QStringLiteral("hg.diff")
            << QStringLiteral("a/sample.txt") << QStringLiteral("b/sample.txt") << 1;
    }

    void parseIndependentFixtures()
    {
        QFETCH(QString, name); QFETCH(QString, oldPath); QFETCH(QString, newPath); QFETCH(int, strip);
        const auto parsed = parse(fixture(name));
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QCOMPARE(parsed.document.files.size(), 1);
        const FilePatch &file = parsed.document.files.first();
        QCOMPARE(file.oldPath, oldPath);
        QCOMPARE(file.newPath, newPath);
        QCOMPARE(file.hunks.size(), 1);
        QCOMPARE(file.hunks.first().oldCount, 3);
        QCOMPARE(file.hunks.first().newCount, 3);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath(QStringLiteral("sample.txt"));
        QVERIFY(writeBytes(path, "one\nold\nthree\n"));
        ApplyOptions options;
        options.stripComponents = strip;
        const auto plan = preview(parsed.document, root.path(), options);
        QVERIFY2(plan.ok, diagnostics(plan.diagnostics));
        QCOMPARE(plan.files.size(), 1);
        QVERIFY(plan.files.first().applicable);
        QCOMPARE(plan.files.first().resultBytes, QByteArray("one\nnew\nthree\n"));
        QCOMPARE(readBytes(path), QByteArray("one\nold\nthree\n"));
    }

    void parsesRealGitDiffOutput()
    {
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY2(!git.isEmpty(), "Real git is required for source compatibility verification");
        QTemporaryDir repository;
        QVERIFY(repository.isValid());
        const QString name = QStringLiteral("中文 source.txt");
        const QByteArray before = QStringLiteral("首行\n旧内容\n末行").toUtf8();
        const QByteArray after = QStringLiteral("首行\n新内容\n末行").toUtf8();
        const auto init = runProcess(git, {"init", "--quiet"}, repository.path());
        QVERIFY2(init.finished && init.exitCode == 0, init.output);
        QVERIFY(writeBytes(repository.filePath(name), before));
        const auto add = runProcess(git, {"add", "--", name}, repository.path());
        QVERIFY2(add.finished && add.exitCode == 0, add.output);
        QVERIFY(writeBytes(repository.filePath(name), after));
        const auto diff = runProcess(git, {"--no-pager", "-c", "core.quotePath=true", "diff", "--no-ext-diff",
                                          "--no-textconv", "--no-color", "--", name}, repository.path());
        QVERIFY2(diff.finished && diff.exitCode == 0, diff.output);
        QVERIFY(diff.output.contains("diff --git "));
        QVERIFY(diff.output.contains("index "));
        const auto parsed = parse(diff.output);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics) + diff.output);
        QCOMPARE(parsed.document.files.size(), 1);
        QCOMPARE(parsed.document.files.first().oldPath, QStringLiteral("a/") + name);
        const auto forward = previewBytes(parsed.document.files.first(), before);
        QVERIFY2(forward.ok, diagnostics(forward.diagnostics));
        QCOMPARE(forward.resultBytes, after);
        ApplyOptions reverse;
        reverse.reverse = true;
        const auto backward = previewBytes(parsed.document.files.first(), after, reverse);
        QVERIFY2(backward.ok, diagnostics(backward.diagnostics));
        QCOMPARE(backward.resultBytes, before);
        QCOMPARE(readBytes(repository.filePath(name)), after);
    }

    void parsesRealSystemDiffOutput()
    {
        const QString diffTool = QStandardPaths::findExecutable(QStringLiteral("diff"));
        QVERIFY2(!diffTool.isEmpty(), "Real diff -u is required for source compatibility verification");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray before("head\r\nold\r\ntail");
        const QByteArray after("head\r\nnew\r\ntail");
        QVERIFY(writeBytes(root.filePath("old/sample.txt"), before));
        QVERIFY(writeBytes(root.filePath("new/sample.txt"), after));
        const auto diff = runProcess(diffTool, {"-u", "old/sample.txt", "new/sample.txt"}, root.path());
        QVERIFY2(diff.finished && diff.exitCode == 1, diff.output);
        const auto parsed = parse(diff.output);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics) + diff.output);
        QCOMPARE(parsed.document.files.size(), 1);
        QCOMPARE(parsed.document.files.first().oldPath, QStringLiteral("old/sample.txt"));
        const auto forward = previewBytes(parsed.document.files.first(), before);
        QVERIFY2(forward.ok, diagnostics(forward.diagnostics));
        QCOMPARE(forward.resultBytes, after);
        QCOMPARE(readBytes(root.filePath("old/sample.txt")), before);
        QCOMPARE(readBytes(root.filePath("new/sample.txt")), after);
    }

    void malformedInput_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("expectedLine");
        QTest::newRow("invalid-hunk-header") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -bad +1 @@\n-old\n+new\n") << 3;
        QTest::newRow("truncated-hunk") << fixture(QStringLiteral("truncated.diff")) << 0;
        QTest::newRow("missing-new-header") << QByteArray("--- a/sample.txt\n@@ -1 +1 @@\n-old\n+new\n") << 0;
        QTest::newRow("unknown-body-prefix") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -1 +1 @@\n!old\n") << 4;
        QTest::newRow("invalid-count") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -1,-1 +1 @@\n-old\n+new\n") << 3;
        QTest::newRow("overflow-start") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -999999999999999999999 +1 @@\n-old\n+new\n") << 3;
        QTest::newRow("orphan-no-newline-marker") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -1 +1 @@\n\\ No newline at end of file\n-old\n+new\n") << 4;
        QTest::newRow("metadata-without-body") << QByteArray("diff --git a/empty.txt b/empty.txt\nnew file mode 100644\n") << 1;
        QTest::newRow("index-without-body") << QByteArray("index 1234567..7654321 100644\n") << 1;
        QTest::newRow("duplicate-no-newline-marker") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -1 +1 @@\n-old\n\\ No newline at end of file\n\\ No newline at end of file\n+new\n") << 6;
        QTest::newRow("inconsistent-unchanged-prefix") << QByteArray("--- a/sample.txt\n+++ b/sample.txt\n@@ -1 +2 @@\n-old\n+new\n") << 3;
    }

    void malformedInput()
    {
        QFETCH(QByteArray, bytes); QFETCH(int, expectedLine);
        const auto parsed = parse(bytes);
        QVERIFY(!parsed.ok);
        QVERIFY(!parsed.diagnostics.isEmpty());
        QVERIFY(!parsed.diagnostics.first().message.isEmpty());
        QVERIFY(parsed.diagnostics.first().line > 0);
        if (expectedLine) QCOMPARE(parsed.diagnostics.first().line, expectedLine);
    }

    void byteExactRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("before");
        QTest::addColumn<QByteArray>("after");
        QTest::newRow("LF") << QByteArray("one\nold\nthree\n") << QByteArray("one\nnew\nthree\n");
        QTest::newRow("Unicode") << QStringLiteral("第一行\n旧内容😀\n最后一行\n").toUtf8()
            << QStringLiteral("第一行\n新内容🧪\n最后一行\n").toUtf8();
        QTest::newRow("UTF8-BOM") << QByteArray::fromHex("efbbbf") + QByteArray("old\n")
            << QByteArray::fromHex("efbbbf") + QByteArray("new\n");
        QTest::newRow("add-BOM") << QByteArray("same\n") << QByteArray::fromHex("efbbbf") + QByteArray("same\n");
        QTest::newRow("remove-BOM") << QByteArray::fromHex("efbbbf") + QByteArray("same\n") << QByteArray("same\n");
        QTest::newRow("CRLF") << QByteArray("one\r\nold\r\nthree\r\n") << QByteArray("one\r\nnew\r\nthree\r\n");
        QTest::newRow("LF-to-CRLF") << QByteArray("one\nold\n") << QByteArray("one\r\nnew\r\n");
        QTest::newRow("mixed-EOL") << QByteArray("one\r\nold\ntail\r\nlast") << QByteArray("one\r\nnew\ntail\r\nlast");
        QTest::newRow("bare-CR-data") << QByteArray("old\rdata\nlast\r") << QByteArray("new\rdata\nlast\r");
        QTest::newRow("neither-final-newline") << QByteArray("one\nold") << QByteArray("one\nnew");
        QTest::newRow("add-final-newline") << QByteArray("one\nlast") << QByteArray("one\nlast\n");
        QTest::newRow("remove-final-newline") << QByteArray("one\nlast\n") << QByteArray("one\nlast");
        QTest::newRow("invalid-UTF8-preserved") << QByteArray::fromHex("6162ff630a78790a") << QByteArray::fromHex("6162fe630a78790a");
        QTest::newRow("empty-to-lines") << QByteArray() << QByteArray("first\nlast");
        QTest::newRow("lines-to-empty") << QByteArray("first\nlast") << QByteArray();
        QTest::newRow("only-newline-added") << QByteArray() << QByteArray("\n");
        QTest::newRow("only-newline-removed") << QByteArray("\n") << QByteArray();
        QTest::newRow("append-after-incomplete-line") << QByteArray("one") << QByteArray("one\ntwo\n");
    }

    void byteExactRoundTrip()
    {
        QFETCH(QByteArray, before); QFETCH(QByteArray, after);
        const auto generated = generate({modified(before, after)});
        QVERIFY2(generated.ok, diagnostics(generated.diagnostics));
        QVERIFY(!generated.bytes.isEmpty());
        const auto parsed = parse(generated.bytes);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QCOMPARE(parsed.document.files.size(), 1);
        const auto forward = previewBytes(parsed.document.files.first(), before);
        QVERIFY2(forward.ok, diagnostics(forward.diagnostics));
        QCOMPARE(forward.resultBytes, after);
        ApplyOptions reverse;
        reverse.reverse = true;
        const auto backward = previewBytes(parsed.document.files.first(), after, reverse);
        QVERIFY2(backward.ok, diagnostics(backward.diagnostics));
        QCOMPARE(backward.resultBytes, before);
    }

    void explicitTransportCrOption()
    {
        QByteArray transported = fixture(QStringLiteral("git-index.diff"));
        transported.replace("\n", "\r\n");
        const auto preserved = parse(transported);
        QVERIFY2(preserved.ok, diagnostics(preserved.diagnostics));
        QCOMPARE(preserved.document.files.first().hunks.first().lines.first().bytes, QByteArray("one\r\n"));
        QVERIFY(!previewBytes(preserved.document.files.first(), "one\nold\nthree\n").ok);
        ParseOptions options;
        options.stripTransportCr = true;
        const auto stripped = parse(transported, options);
        QVERIFY2(stripped.ok, diagnostics(stripped.diagnostics));
        const auto plan = previewBytes(stripped.document.files.first(), "one\nold\nthree\n");
        QVERIFY2(plan.ok, diagnostics(plan.diagnostics));
        QCOMPARE(plan.resultBytes, QByteArray("one\nnew\nthree\n"));
    }

    void noNewlineMarkerCannotBecomeFileData()
    {
        const QByteArray bytes("--- a/sample.txt\n+++ b/sample.txt\n@@ -1 +1 @@\n-old\n\\ No newline at end of file\n+new\n\\ No newline at end of file\n");
        const auto parsed = parse(bytes);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QCOMPARE(parsed.document.files.first().hunks.first().lines.size(), 2);
        QCOMPARE(parsed.document.files.first().hunks.first().lines.first().bytes, QByteArray("old"));
        const auto applied = previewBytes(parsed.document.files.first(), "old");
        QVERIFY2(applied.ok, diagnostics(applied.diagnostics));
        QCOMPARE(applied.resultBytes, QByteArray("new"));
    }

    void externalGitOctalQuotedPathsRemainReadable()
    {
        const QByteArray encodedPath("\\344\\270\\255\\346\\226\\207\\040file.txt");
        const QByteArray patch = "diff --git \"a/" + encodedPath + "\" \"b/" + encodedPath
            + "\"\nindex 1234567..7654321 100644\n--- \"a/" + encodedPath
            + "\"\n+++ \"b/" + encodedPath + "\"\n@@ -1 +1 @@\n-old\n+new\n";
        const auto parsed = parse(patch);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QCOMPARE(parsed.document.files.first().oldPath, QStringLiteral("a/中文 file.txt"));
        QCOMPARE(parsed.document.files.first().newPath, QStringLiteral("b/中文 file.txt"));
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString target = root.filePath(QStringLiteral("中文 file.txt"));
        QVERIFY(writeBytes(target, "old\n"));
        const auto plan = preview(parsed.document, root.path());
        QVERIFY2(plan.ok, diagnostics(plan.diagnostics));
        QCOMPARE(plan.files.first().resultBytes, QByteArray("new\n"));
        QCOMPARE(readBytes(target), QByteArray("old\n"));
    }

    void generationStatisticsAndContext()
    {
        const QByteArray before("1\n2\n3\n4\n5\n6\n7\n8\n9\n");
        const QByteArray after("1\nTWO\n3\n4\n5\n6\n7\nEIGHT\n9\n");
        GenerateOptions options;
        options.contextLines = 1;
        const auto result = generate({modified(before, after)}, options);
        QVERIFY2(result.ok, diagnostics(result.diagnostics));
        QCOMPARE(result.fileCount, 1);
        QCOMPARE(result.hunkCount, 2);
        QCOMPARE(result.addedLines, 2);
        QCOMPARE(result.removedLines, 2);
        const auto parsed = parse(result.bytes);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QCOMPARE(parsed.document.files.first().hunks.size(), 2);
        QCOMPARE(parsed.document.files.first().hunks.first().oldCount, 3);
        options.contextLines = 0;
        const auto noContext = generate({modified(before, after)}, options);
        QVERIFY2(noContext.ok, diagnostics(noContext.diagnostics));
        const auto noContextParsed = parse(noContext.bytes);
        QVERIFY(noContextParsed.ok);
        QCOMPARE(noContextParsed.document.files.first().hunks.first().oldCount, 1);
        QCOMPARE(previewBytes(noContextParsed.document.files.first(), before).resultBytes, after);
        options.contextLines = 20;
        const auto oneHunk = generate({modified(before, after)}, options);
        QVERIFY(oneHunk.ok);
        QCOMPARE(oneHunk.hunkCount, 1);
    }

    void unchangedInputsGenerateNoPatch()
    {
        const auto result = generate({modified("same\r\n", "same\r\n"), modified({}, {}, "empty.txt")});
        QVERIFY2(result.ok, diagnostics(result.diagnostics));
        QVERIFY(result.bytes.isEmpty());
        QCOMPARE(result.fileCount, 0);
        QCOMPARE(result.hunkCount, 0);
        QCOMPARE(result.addedLines, 0);
        QCOMPARE(result.removedLines, 0);
    }

    void binaryGenerationIsRejected()
    {
        const auto result = generate({modified(QByteArray("a\0b", 3), QByteArray("a\0c", 3))});
        QVERIFY(!result.ok);
        QVERIFY(result.bytes.isEmpty());
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void atomicPatchExportRequiresExplicitOverwrite()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath(QStringLiteral("导出补丁.diff"));
        const QByteArray initial("--- a/file\n+++ b/file\n");
        QString error = QStringLiteral("stale error");
        QVERIFY2(writeFile(path, initial, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(readBytes(path), initial);
        QVERIFY(!writeFile(path, "replacement\n", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(path), initial);
        QVERIFY2(writeFile(path, "replacement\n", &error, true), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(readBytes(path), QByteArray("replacement\n"));
        const QString missingParent = root.filePath(QStringLiteral("missing/file.diff"));
        QVERIFY(!writeFile(missingParent, initial, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("missing"))));
        QCOMPARE(QDir(root.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
    }

    void atomicExportRejectsSymlinkAndDirectory()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString target = root.filePath("original.diff");
        const QString link = root.filePath("linked.diff");
        QVERIFY(writeBytes(target, "original\n"));
        QVERIFY(QFile::link(target, link));
        QString error;
        QVERIFY(!writeFile(link, "replacement\n", &error, true));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(target), QByteArray("original\n"));
        QVERIFY(!writeFile(root.path(), "replacement\n", &error, true));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo(root.path()).isDir());
    }

    void exactContextOffset_data()
    {
        QTest::addColumn<QByteArray>("before");
        QTest::addColumn<QByteArray>("after");
        QTest::addColumn<int>("offset");
        QTest::newRow("shift-forward") << QByteArray("extra\nhead\nold\ntail\n")
            << QByteArray("extra\nhead\nnew\ntail\n") << 1;
        QTest::newRow("shift-backward") << QByteArray("head\nold\ntail\n")
            << QByteArray("head\nnew\ntail\n") << -1;
    }

    void exactContextOffset()
    {
        QFETCH(QByteArray, before); QFETCH(QByteArray, after); QFETCH(int, offset);
        const int start = offset > 0 ? 1 : 2;
        const QByteArray patch = "--- a/sample.txt\n+++ b/sample.txt\n@@ -" + QByteArray::number(start)
            + ",3 +" + QByteArray::number(start) + ",3 @@\n head\n-old\n+new\n tail\n";
        const auto parsed = parse(patch);
        QVERIFY(parsed.ok);
        ApplyOptions options;
        options.maximumOffset = 1;
        const auto result = previewBytes(parsed.document.files.first(), before, options);
        QVERIFY2(result.ok, diagnostics(result.diagnostics));
        QCOMPARE(result.resultBytes, after);
        QCOMPARE(result.hunks.size(), 1);
        QCOMPARE(result.hunks.first().offset, offset);
        options.maximumOffset = 0;
        const auto rejected = previewBytes(parsed.document.files.first(), before, options);
        QVERIFY(!rejected.ok);
        QVERIFY(rejected.resultBytes.isEmpty());
    }

    void contextIsNeverDiscarded_data()
    {
        QTest::addColumn<QByteArray>("before");
        QTest::newRow("leading-context-mismatch") << QByteArray("changed-head\nold\ntail\n");
        QTest::newRow("trailing-context-mismatch") << QByteArray("head\nold\nchanged-tail\n");
        QTest::newRow("only-change-line-matches") << QByteArray("old\n");
        QTest::newRow("newline-is-context") << QByteArray("head\nold\ntail");
    }

    void contextIsNeverDiscarded()
    {
        QFETCH(QByteArray, before);
        const auto parsed = parse("--- a/sample.txt\n+++ b/sample.txt\n@@ -1,3 +1,3 @@\n head\n-old\n+new\n tail\n");
        QVERIFY(parsed.ok);
        const auto result = previewBytes(parsed.document.files.first(), before);
        QVERIFY(!result.ok);
        QVERIFY(result.resultBytes.isEmpty());
        QVERIFY(!result.hunks.first().applicable);
        QVERIFY(!result.hunks.first().error.isEmpty());
    }

    void selectedHunksAndFailureAreWholeFileSafe()
    {
        const QByteArray before("one\nold-a\nthree\nfour\nfive\nsix\nold-b\neight\n");
        const QByteArray after("one\nnew-a\nthree\nfour\nfive\nsix\nnew-b\neight\n");
        GenerateOptions options;
        options.contextLines = 1;
        const auto generated = generate({modified(before, after)}, options);
        QVERIFY(generated.ok);
        const auto parsed = parse(generated.bytes);
        QVERIFY(parsed.ok);
        const FilePatch &file = parsed.document.files.first();
        QCOMPARE(file.hunks.size(), 2);
        ApplyOptions selected;
        selected.selectedHunks.insert(0, {1});
        const auto partial = previewBytes(file, before, selected);
        QVERIFY2(partial.ok, diagnostics(partial.diagnostics));
        QCOMPARE(partial.resultBytes, QByteArray("one\nold-a\nthree\nfour\nfive\nsix\nnew-b\neight\n"));
        QCOMPARE(partial.hunks.size(), 2);
        QVERIFY(!partial.hunks.at(0).selected);
        QVERIFY(partial.hunks.at(1).selected);
        selected.selectedHunks[0].clear();
        const auto none = previewBytes(file, before, selected);
        QVERIFY2(none.ok, diagnostics(none.diagnostics));
        QCOMPARE(none.resultBytes, before);
        QVERIFY(!none.hunks.at(0).selected);
        QVERIFY(!none.hunks.at(1).selected);
        const QByteArray broken("one\nold-a\nthree\nfour\nfive\nsix\nDIFFERENT\neight\n");
        const auto rejected = previewBytes(file, broken);
        QVERIFY(!rejected.ok);
        QVERIFY(rejected.resultBytes.isEmpty());
        QCOMPARE(rejected.hunks.size(), 2);
        QVERIFY(rejected.hunks.at(0).applicable);
        QVERIFY(!rejected.hunks.at(1).applicable);
        QVERIFY(!rejected.diagnostics.isEmpty());
    }

    void inconsistentFinalNewlineClearsWholeResult()
    {
        const auto parsed = parse("--- a/sample.txt\n+++ b/sample.txt\n@@ -2 +2 @@\n-old\n+new\n\\ No newline at end of file\n");
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        const auto result = previewBytes(parsed.document.files.first(), "head\nold\ntail\n");
        QVERIFY(!result.ok);
        QVERIFY(result.resultBytes.isEmpty());
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void invalidHunkSelectionCannotSilentlyApply()
    {
        const auto parsed = parse(fixture(QStringLiteral("git-index.diff")));
        QVERIFY(parsed.ok);
        ApplyOptions options;
        options.selectedHunks.insert(0, {1});
        const auto badIndex = previewBytes(parsed.document.files.first(), "one\nold\nthree\n", options);
        QVERIFY(!badIndex.ok);
        QVERIFY(badIndex.resultBytes.isEmpty());
        QVERIFY(!badIndex.diagnostics.isEmpty());
        options.selectedHunks.insert(0, {-1});
        const auto negativeIndex = previewBytes(parsed.document.files.first(), "one\nold\nthree\n", options);
        QVERIFY(!negativeIndex.ok);
        QVERIFY(negativeIndex.resultBytes.isEmpty());
    }

    void ambiguousOffsetIsRejected()
    {
        const auto parsed = parse("--- a/sample.txt\n+++ b/sample.txt\n@@ -2 +2 @@\n-old\n+new\n");
        QVERIFY(parsed.ok);
        const auto result = previewBytes(parsed.document.files.first(), "old\nseparator\nold\n");
        QVERIFY(!result.ok);
        QVERIFY(result.resultBytes.isEmpty());
        QVERIFY(!result.hunks.first().error.isEmpty());
    }

    void multipleHunkSizeChangesUseOriginalCoordinates()
    {
        const QByteArray before("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\n");
        const QByteArray after("a\nb\ninsert1\ninsert2\nc\nd\ne\nf\ng\nh\nj\nk\nl\n");
        GenerateOptions generateOptions;
        generateOptions.contextLines = 1;
        const auto generated = generate({modified(before, after)}, generateOptions);
        QVERIFY2(generated.ok, diagnostics(generated.diagnostics));
        QCOMPARE(generated.hunkCount, 2);
        const auto parsed = parse(generated.bytes);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        const auto &file = parsed.document.files.first();
        const auto forward = previewBytes(file, before);
        QVERIFY2(forward.ok, diagnostics(forward.diagnostics));
        QCOMPARE(forward.resultBytes, after);
        ApplyOptions reverse;
        reverse.reverse = true;
        const auto backward = previewBytes(file, after, reverse);
        QVERIFY2(backward.ok, diagnostics(backward.diagnostics));
        QCOMPARE(backward.resultBytes, before);
        ApplyOptions partial;
        partial.selectedHunks.insert(0, {1});
        const auto selected = previewBytes(file, before, partial);
        QVERIFY2(selected.ok, diagnostics(selected.diagnostics));
        QCOMPARE(selected.resultBytes, QByteArray("a\nb\nc\nd\ne\nf\ng\nh\nj\nk\nl\n"));
    }

    void manuallyConstructedEmbeddedLineBreakIsRejected()
    {
        Document document = documentWithPaths("a/sample.txt", "b/sample.txt");
        document.files.first().hunks.first().lines.last().bytes = "new\nextra\n";
        const auto result = previewBytes(document.files.first(), "old\n");
        QVERIFY(!result.ok);
        QVERIFY(result.resultBytes.isEmpty());
        QVERIFY(!result.diagnostics.isEmpty());
    }

    void creationDeletionAndMultipleFilesAreReadOnly()
    {
        FileInput create = modified({}, "created\n", "nested/new.txt");
        create.oldExists = false;
        FileInput remove = modified("removed\n", {}, "old.txt");
        remove.newExists = false;
        FileInput change = modified("old\n", "new\n", QStringLiteral("中文 文件.txt"));
        const auto generated = generate({create, remove, change});
        QVERIFY2(generated.ok, diagnostics(generated.diagnostics));
        QCOMPARE(generated.fileCount, 3);
        const auto parsed = parse(generated.bytes);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeBytes(root.filePath("old.txt"), remove.oldBytes));
        QVERIFY(writeBytes(root.filePath(change.oldPath), change.oldBytes));
        const auto plan = preview(parsed.document, root.path());
        QVERIFY2(plan.ok, diagnostics(plan.diagnostics));
        QCOMPARE(plan.files.size(), 3);
        QVERIFY(plan.files.at(0).createsFile);
        QCOMPARE(plan.files.at(0).resultBytes, create.newBytes);
        QVERIFY(plan.files.at(1).deletesFile);
        QVERIFY(plan.files.at(1).resultBytes.isEmpty());
        QCOMPARE(plan.files.at(2).resultBytes, change.newBytes);
        QVERIFY(!QFileInfo::exists(root.filePath("nested")));
        QCOMPARE(readBytes(root.filePath("old.txt")), remove.oldBytes);
        QCOMPARE(readBytes(root.filePath(change.oldPath)), change.oldBytes);
        QVERIFY(!QFileInfo::exists(root.filePath("old.txt.orig")));

        QTemporaryDir reverseRoot;
        QVERIFY(reverseRoot.isValid());
        QVERIFY(writeBytes(reverseRoot.filePath(create.newPath), create.newBytes));
        QVERIFY(writeBytes(reverseRoot.filePath(change.newPath), change.newBytes));
        ApplyOptions reverse;
        reverse.reverse = true;
        const auto reversed = preview(parsed.document, reverseRoot.path(), reverse);
        QVERIFY2(reversed.ok, diagnostics(reversed.diagnostics));
        QVERIFY(reversed.files.at(0).deletesFile);
        QVERIFY(reversed.files.at(1).createsFile);
        QCOMPARE(reversed.files.at(1).resultBytes, remove.oldBytes);
        QCOMPARE(reversed.files.at(2).resultBytes, change.oldBytes);
        QCOMPARE(readBytes(reverseRoot.filePath(create.newPath)), create.newBytes);
        QVERIFY(!QFileInfo::exists(reverseRoot.filePath(remove.oldPath)));
    }

    void unsafePaths_data()
    {
        QTest::addColumn<QString>("path");
        const QStringList paths = {"../escape.txt", "a/../../escape.txt", "a/../escape.txt",
            "/tmp/escape.txt", "C:/escape.txt", "C:\\escape.txt", "C:escape.txt",
            "\\\\server\\share\\escape.txt", "a\\..\\escape.txt", "a/./escape.txt"};
        for (const QString &path : paths) QTest::newRow(qPrintable(path)) << path;
    }

    void unsafePaths()
    {
        QFETCH(QString, path);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString sentinel = root.filePath("escape.txt");
        QVERIFY(writeBytes(sentinel, "old\n"));
        const auto plan = preview(documentWithPaths(path, path), root.path());
        QVERIFY2(!plan.ok, qPrintable(path));
        QVERIFY(!plan.diagnostics.isEmpty() || (!plan.files.isEmpty() && !plan.files.first().diagnostics.isEmpty()));
        QCOMPARE(readBytes(sentinel), QByteArray("old\n"));
    }

    void generationRejectsUnsafePaths()
    {
        for (const QString &path : QStringList{"../escape.txt", "/tmp/escape.txt", "C:/escape.txt"}) {
            const auto result = generate({modified("old\n", "new\n", path)});
            QVERIFY2(!result.ok, qPrintable(path));
            QVERIFY(result.bytes.isEmpty());
        }
    }

    void leadingQuoteWithoutPrefixIsRejected()
    {
        const FileInput input = modified("old\n", "new\n", QStringLiteral("\"quoted\".txt"));
        GenerateOptions noPrefix;
        noPrefix.oldPrefix.clear();
        noPrefix.newPrefix.clear();
        const auto rejected = generate({input}, noPrefix);
        QVERIFY(!rejected.ok);
        QVERIFY(rejected.bytes.isEmpty());
        QVERIFY(!rejected.diagnostics.isEmpty());
        const auto prefixed = generate({input});
        QVERIFY2(prefixed.ok, diagnostics(prefixed.diagnostics));
        const auto parsed = parse(prefixed.bytes);
        QVERIFY2(parsed.ok, diagnostics(parsed.diagnostics));
        QCOMPARE(parsed.document.files.first().oldPath, QStringLiteral("a/\"quoted\".txt"));
        const auto plan = previewBytes(parsed.document.files.first(), input.oldBytes);
        QVERIFY2(plan.ok, diagnostics(plan.diagnostics));
        QCOMPARE(plan.resultBytes, input.newBytes);
    }

    void symlinkTargetsAndComponentsAreRejected()
    {
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid()); QVERIFY(outside.isValid());
        const QString external = outside.filePath("sample.txt");
        QVERIFY(writeBytes(external, "old\n"));
        QVERIFY(QFile::link(external, root.filePath("sample.txt")));
        const auto target = preview(documentWithPaths("a/sample.txt", "b/sample.txt"), root.path());
        QVERIFY(!target.ok);
        QCOMPARE(readBytes(external), QByteArray("old\n"));
        QVERIFY(QFile::link(outside.path(), root.filePath("linked")));
        const auto component = preview(documentWithPaths("a/linked/sample.txt", "b/linked/sample.txt"), root.path());
        QVERIFY(!component.ok);
        QCOMPARE(readBytes(external), QByteArray("old\n"));
        const auto create = preview(documentWithPaths("/dev/null", "b/linked/created.txt"), root.path());
        QVERIFY(!create.ok);
        QVERIFY(!QFileInfo::exists(outside.filePath("created.txt")));
    }

    void duplicateRenamedAndNonregularTargetsAreRejected()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath("sample.txt");
        QVERIFY(writeBytes(path, "old\n"));
        Document duplicate = documentWithPaths("a/sample.txt", "b/sample.txt");
        duplicate.files.push_back(duplicate.files.first());
        const auto duplicated = preview(duplicate, root.path());
        QVERIFY(!duplicated.ok);
        QCOMPARE(readBytes(path), QByteArray("old\n"));
        const auto renamed = preview(documentWithPaths("a/sample.txt", "b/renamed.txt"), root.path());
        QVERIFY(!renamed.ok);
        QVERIFY(!QFileInfo::exists(root.filePath("renamed.txt")));
        QVERIFY(QDir(root.path()).mkdir("directory"));
        const auto directory = preview(documentWithPaths("a/directory", "b/directory"), root.path());
        QVERIFY(!directory.ok);
        QCOMPARE(readBytes(path), QByteArray("old\n"));
    }

    void failedMultiFilePreviewDoesNotWriteAnything()
    {
        const auto generated = generate({modified("old\n", "new\n", "first.txt"),
                                         modified("expected\n", "new\n", "second.txt")});
        QVERIFY(generated.ok);
        const auto parsed = parse(generated.bytes);
        QVERIFY(parsed.ok);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeBytes(root.filePath("first.txt"), "old\n"));
        QVERIFY(writeBytes(root.filePath("second.txt"), "different\n"));
        const auto plan = preview(parsed.document, root.path());
        QVERIFY(!plan.ok);
        QCOMPARE(plan.files.size(), 2);
        QVERIFY(plan.files.at(0).applicable);
        QVERIFY(!plan.files.at(1).applicable);
        QVERIFY(plan.files.at(1).resultBytes.isEmpty());
        QCOMPARE(readBytes(root.filePath("first.txt")), QByteArray("old\n"));
        QCOMPARE(readBytes(root.filePath("second.txt")), QByteArray("different\n"));
        QCOMPARE(QDir(root.path()).entryList(QDir::Files).size(), 2);
    }

    void generatedPatchWorksWithRealTools_data()
    {
        QTest::addColumn<QString>("tool");
        QTest::newRow("git-apply") << QStringLiteral("git");
        QTest::newRow("patch-p1") << QStringLiteral("patch");
    }

    void generatedPatchWorksWithRealTools()
    {
        QFETCH(QString, tool);
        const QString executable = QStandardPaths::findExecutable(tool);
        QVERIFY2(!executable.isEmpty(), qPrintable("Required end-to-end tool unavailable: " + tool));
        const QVector<FileInput> files = {
            modified(QStringLiteral("第一行\n旧内容\n末行\n").toUtf8(), QStringLiteral("第一行\n新内容\n末行\n").toUtf8(), QStringLiteral("中文 文件.txt")),
            modified("old\n", "new\n", QStringLiteral("directory with spaces/file name.txt")),
            modified("old\n", "new\n", QStringLiteral("\"quoted\".txt")),
            modified("one\r\nold\r\nlast\r\n", "one\r\nnew\r\nlast\r\n", "crlf.txt"),
            modified(QByteArray::fromHex("efbbbf") + "old\n", QByteArray::fromHex("efbbbf") + "new\n", "bom.txt"),
            modified("first\nold", "first\nnew", "no-final-newline.txt"),
            modified("one\nold\r\nlast", "one\nnew\r\nlast", "mixed.txt")
        };
        const auto generated = generate(files);
        QVERIFY2(generated.ok, diagnostics(generated.diagnostics));
        QTemporaryDir root;
        QTemporaryDir patchLocation;
        QVERIFY(root.isValid()); QVERIFY(patchLocation.isValid());
        for (const auto &file : files) QVERIFY(writeBytes(root.filePath(file.oldPath), file.oldBytes));
        const QString patchPath = patchLocation.filePath("generated.diff");
        QVERIFY(writeBytes(patchPath, generated.bytes));
        if (tool == QStringLiteral("git")) {
            const auto init = runProcess(executable, {"init", "--quiet"}, root.path());
            QVERIFY2(init.finished && init.exitCode == 0, init.output);
            const auto check = runProcess(executable, {"apply", "--check", patchPath}, root.path());
            QVERIFY2(check.finished && check.exitCode == 0, check.output + generated.bytes);
            const auto applied = runProcess(executable, {"apply", patchPath}, root.path());
            QVERIFY2(applied.finished && applied.exitCode == 0, applied.output + generated.bytes);
        } else {
            const auto applied = runProcess(executable, {"-p1", "--batch", "-i", patchPath}, root.path());
            QVERIFY2(applied.finished && applied.exitCode == 0, applied.output + generated.bytes);
        }
        for (const auto &file : files) QCOMPARE(readBytes(root.filePath(file.newPath)), file.newBytes);
        const QStringList reverseArguments = tool == QStringLiteral("git")
            ? QStringList{"apply", "--reverse", patchPath}
            : QStringList{"-p1", "--batch", "-R", "-i", patchPath};
        const auto reversed = runProcess(executable, reverseArguments, root.path());
        QVERIFY2(reversed.finished && reversed.exitCode == 0, reversed.output + generated.bytes);
        for (const auto &file : files) QCOMPARE(readBytes(root.filePath(file.oldPath)), file.oldBytes);
    }
};

QTEST_GUILESS_MAIN(PatchRegressionTests)
#include "tst_patch_regression.moc"
