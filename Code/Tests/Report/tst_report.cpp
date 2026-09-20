#include <QtTest>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>
#include "report.h"

using namespace LqCompare;
namespace R = LqCompare::Report;

namespace {
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Unable to read test fixture");
    return file.readAll();
}
void putBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Unable to create temporary fixture");
}
R::Metadata metadata()
{
    R::Metadata value;
    value.title = QStringLiteral("中文差异 😀");
    value.leftSource = QStringLiteral("left/中文源.txt");
    value.rightSource = QStringLiteral("right/中文源.txt");
    value.toolVersion = QStringLiteral("LqCompare test-version-42");
    value.generatedAt = QDateTime::fromString(QStringLiteral("2026-09-20T15:04:05Z"), Qt::ISODate);
    value.settings = QStringList{QStringLiteral("用户设置：保留中文")};
    return value;
}
QString decodedHtmlText(QString html)
{
    // Inspect visible payload independently from the exact choice of HTML entities.
    html.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    html.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    html.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    html.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    html.replace(QStringLiteral("&#34;"), QStringLiteral("\""));
    html.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    html.replace(QStringLiteral("&#x27;"), QStringLiteral("'"), Qt::CaseInsensitive);
    html.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    html.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return html;
}
R::Model fixtureModel()
{
    Text::Document left, right;
    const QString leftPath = QFINDTESTDATA("fixtures/text-left.txt");
    const QString rightPath = QFINDTESTDATA("fixtures/text-right.txt");
    if (leftPath.isEmpty() || rightPath.isEmpty()
        || !Text::Document::decode(readBytes(leftPath), &left)
        || !Text::Document::decode(readBytes(rightPath), &right))
        qFatal("Unable to decode real UTF-8 text fixtures");
    Text::CompareOptions options;
    options.ignoreCase = true;
    options.whitespace = Text::Whitespace::IgnoreChanges;
    return R::fromText(left, right, Text::compare(left.lines(), right.lines(), options), options, metadata());
}
R::Model manyRows(int count)
{
    R::Model model;
    model.metadata = metadata();
    model.rows.reserve(count);
    for (int index = 0; index < count; ++index) {
        R::Row row;
        row.state = R::State::Changed;
        row.leftLine = row.rightLine = index + 1;
        row.left = QStringLiteral("旧行 %1 中文😀").arg(index);
        row.right = QStringLiteral("新行 %1 中文😀").arg(index);
        model.rows.append(row);
    }
    return model;
}
Folder::Entry folderEntry(const QString &path, Folder::Status status,
                          Folder::Kind leftKind = Folder::Kind::File,
                          Folder::Kind rightKind = Folder::Kind::File)
{
    Folder::Entry entry;
    entry.relativePath = path;
    entry.status = status;
    entry.left.kind = leftKind;
    entry.right.kind = rightKind;
    entry.left.info.exists = leftKind != Folder::Kind::Missing;
    entry.right.info.exists = rightKind != Folder::Kind::Missing;
    entry.left.info.isDirectory = leftKind == Folder::Kind::Directory;
    entry.right.info.isDirectory = rightKind == Folder::Kind::Directory;
    entry.left.info.size = 123;
    entry.right.info.size = 456;
    return entry;
}

// A real sequential QIODevice: short writes are legal, -1/0 must stop promptly.
class ControlledDevice final : public QIODevice
{
public:
    explicit ControlledDevice(qint64 chunk = 17, qint64 failAfter = -1, bool returnZero = false)
        : m_chunk(chunk), m_failAfter(failAfter), m_returnZero(returnZero)
    { open(QIODevice::WriteOnly); }
    QByteArray bytes;
    int calls = 0;
    qint64 largestRequest = 0;
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 size) override
    {
        ++calls;
        largestRequest = qMax(largestRequest, size);
        if (m_failAfter >= 0 && bytes.size() >= m_failAfter) {
            setErrorString(QStringLiteral("Injected write failure"));
            return m_returnZero ? 0 : -1;
        }
        qint64 count = qMin(size, m_chunk);
        if (m_failAfter >= 0) count = qMin(count, m_failAfter - bytes.size());
        bytes.append(data, int(count));
        return count;
    }
private:
    qint64 m_chunk;
    qint64 m_failAfter;
    bool m_returnZero;
};
}

class ReportTests : public QObject
{
    Q_OBJECT
private slots:
    void realTextMappingAndDefaultFilters()
    {
        const R::Model model = fixtureModel();
        QCOMPARE(model.kind, R::Kind::Text);
        QCOMPARE(model.rows.size(), 10);
        QCOMPARE(model.metadata.leftSource, metadata().leftSource);
        QCOMPARE(model.metadata.rightSource, metadata().rightSource);
        QVERIFY(model.metadata.settings.contains(metadata().settings.first()));
        QVERIFY(model.metadata.settings.size() > metadata().settings.size());
        const R::Statistics defaults = R::statistics(model);
        QCOMPARE(defaults.total, qint64(10));
        QCOMPARE(defaults.equal, qint64(5));
        QCOMPARE(defaults.ignored, qint64(1));
        QCOMPARE(defaults.changed, qint64(2));
        QCOMPARE(defaults.leftOnly, qint64(1));
        QCOMPARE(defaults.rightOnly, qint64(1));
        QCOMPARE(defaults.exported, qint64(4));
        QVERIFY(defaults.hasDifferences());
        QVERIFY(defaults.complete);
        QCOMPARE(model.rows.first().leftLine, 1);
        QCOMPARE(model.rows.first().rightLine, 1);
        QCOMPARE(model.rows.first().left, QStringLiteral("中文标题 😀"));
        QCOMPARE(model.rows[1].state, R::State::Ignored);
        QCOMPARE(model.rows[5].state, R::State::LeftOnly);
        QCOMPARE(model.rows[5].leftLine, 6);
        QCOMPARE(model.rows[5].rightLine, 0);
        QCOMPARE(model.rows[7].state, R::State::RightOnly);
        QCOMPARE(model.rows[7].leftLine, 0);
        QCOMPARE(model.rows[7].rightLine, 7);
        QCOMPARE(model.rows.last().left, QStringLiteral("末行旧"));
        QCOMPARE(model.rows.last().right, QStringLiteral("末行新"));

        R::Options options;
        options.includeEqual = options.includeIgnored = true;
        QCOMPARE(R::statistics(model, options).exported, qint64(10));
        options.includeOrphans = false;
        QCOMPARE(R::statistics(model, options).exported, qint64(8));
        options.includeEqual = options.includeIgnored = false;
        QCOMPARE(R::statistics(model, options).exported, qint64(2));
        R::Model filtered = model;
        filtered.rows.last().visible = false;
        const R::Statistics visible = R::statistics(filtered, options);
        QCOMPARE(visible.total, defaults.total);
        QCOMPARE(visible.changed, defaults.changed);
        QCOMPARE(visible.exported, qint64(1));
    }

    void exportedContentRespectsFilters()
    {
        const R::Model model = fixtureModel();
        R::Options options;
        options.format = R::Format::PlainText;
        QString output = R::render(model, options);
        QVERIFY(!output.contains(QStringLiteral("共同锚点一")));
        QVERIFY(!output.contains(QStringLiteral("Mixed CASE")));
        QVERIFY(output.contains(QStringLiteral("仅左内容<&>")));
        QVERIFY(output.contains(QStringLiteral("仅右内容<&>")));
        options.includeOrphans = false;
        output = R::render(model, options);
        QVERIFY(!output.contains(QStringLiteral("仅左内容<&>")));
        QVERIFY(!output.contains(QStringLiteral("仅右内容<&>")));
        QVERIFY(output.contains(QStringLiteral("末行旧")));
        options.includeEqual = options.includeIgnored = true;
        output = R::render(model, options);
        QVERIFY(output.contains(QStringLiteral("共同锚点一")));
        QVERIFY(output.contains(QStringLiteral("Mixed CASE")));
        R::Model filtered = model;
        filtered.rows.last().visible = false;
        QVERIFY(!R::render(filtered, options).contains(QStringLiteral("末行旧")));
    }

    void htmlEscapesEveryUntrustedField()
    {
        R::Model model = fixtureModel();
        model.kind = R::Kind::Folder; // Exercise paths as well as both text cells.
        model.metadata.title = QStringLiteral("标题</title><script>alert('title')</script>\"&");
        model.metadata.leftSource = QStringLiteral("左<iframe src='https://bad.invalid/left'>\"&");
        model.metadata.rightSource = QStringLiteral("右<img src='https://bad.invalid/right'>\"&");
        model.metadata.toolVersion = QStringLiteral("版本<meta http-equiv='refresh' content='0'>\"&");
        model.metadata.settings.append(QStringLiteral("设置<style>@import 'https://bad.invalid/style';</style>\"&"));
        model.warnings.append(QStringLiteral("警告<svg onload='alert(1)'>\"&"));
        model.rows[3].path = QStringLiteral("目录/<a href='javascript:alert(1)'>路径</a>\"&.txt");
        model.rows[3].detail = QStringLiteral("详情<object data='https://bad.invalid/object'>\"&");
        R::Options options;
        options.includeEqual = options.includeIgnored = true;
        const QString html = R::render(model, options);
        QVERIFY(!html.isEmpty());
        const QStringList payloads{model.metadata.title, model.metadata.leftSource,
            model.metadata.rightSource, model.metadata.toolVersion,
            model.metadata.settings.last(), model.warnings.first(),
            model.rows[3].path, model.rows[3].left, model.rows[3].right, model.rows[3].detail};
        const QString decoded = decodedHtmlText(html);
        for (const QString &payload : payloads) {
            QVERIFY2(!html.contains(payload), qPrintable(QStringLiteral("Raw HTML payload: ") + payload));
            QVERIFY2(decoded.contains(payload), qPrintable(QStringLiteral("Lost escaped content: ") + payload));
        }
        const QRegularExpression resourceTag(
            QStringLiteral("<(?:img|iframe|object|embed|link|base|svg)\\b|<script\\b[^>]*\\bsrc\\s*="),
            QRegularExpression::CaseInsensitiveOption);
        QVERIFY2(!resourceTag.match(html).hasMatch(), qPrintable(resourceTag.match(html).captured()));
        const QRegularExpression styles(QStringLiteral("<style\\b[^>]*>([\\s\\S]*?)</style>"),
                                        QRegularExpression::CaseInsensitiveOption);
        auto styleMatches = styles.globalMatch(html);
        while (styleMatches.hasNext()) {
            const QString css = styleMatches.next().captured(1);
            QVERIFY(!css.contains(QStringLiteral("@import"), Qt::CaseInsensitive));
            QVERIFY(!css.contains(QStringLiteral("url("), Qt::CaseInsensitive));
        }
        QVERIFY(html.contains(QStringLiteral("https://attack.invalid/像素")));
        QVERIFY(decoded.contains(QStringLiteral("中文标题 😀")));
    }

    void allFormatsAndLayouts_data()
    {
        QTest::addColumn<int>("format");
        QTest::addColumn<int>("layout");
        for (int format = 0; format < 2; ++format)
            for (int layout = 0; layout < 4; ++layout)
                QTest::newRow(qPrintable(QStringLiteral("format-%1-layout-%2").arg(format).arg(layout)))
                    << format << layout;
    }
    void allFormatsAndLayouts()
    {
        QFETCH(int, format);
        QFETCH(int, layout);
        const R::Model model = fixtureModel();
        R::Options options;
        options.format = R::Format(format);
        options.layout = R::Layout(layout);
        QString error = QStringLiteral("stale error");
        const QString output = R::render(model, options, &error);
        QVERIFY2(!output.isEmpty(), qPrintable(error));
        QVERIFY(error.isEmpty());
        const QString visible = format == int(R::Format::Html) ? decodedHtmlText(output) : output;
        QVERIFY(visible.contains(model.metadata.title));
        QVERIFY(visible.contains(model.metadata.leftSource));
        QVERIFY(visible.contains(model.metadata.rightSource));
        QVERIFY(visible.contains(model.metadata.toolVersion));
        QVERIFY(visible.contains(model.metadata.settings.first()));
        QVERIFY(visible.contains(QStringLiteral("2026-09-20")));
        QVERIFY(visible.contains(R::stateLabel(R::State::Changed)));
        QVERIFY(visible.contains(R::stateLabel(R::State::Ignored)));
        if (layout == int(R::Layout::SideBySide) || layout == int(R::Layout::Interleaved)) {
            QVERIFY(visible.contains(QStringLiteral("末行旧")));
            QVERIFY(visible.contains(QStringLiteral("末行新")));
        }
        if (format == int(R::Format::Html)) {
            QVERIFY(output.contains(QStringLiteral("<html"), Qt::CaseInsensitive));
            QVERIFY(output.contains(QStringLiteral("charset=\"utf-8\""), Qt::CaseInsensitive)
                    || output.contains(QStringLiteral("charset=\"UTF-8\"")));
            QVERIFY(output.contains(QStringLiteral("</html>"), Qt::CaseInsensitive));
        }
    }

    void folderMappingRetainsStatesSourcesAndDirectoryKind()
    {
        Folder::Result result;
        result.leftRoot = QStringLiteral("/左根/中文");
        result.rightRoot = QStringLiteral("/右根/中文");
        result.entries = {
            folderEntry(QStringLiteral("same.txt"), Folder::Status::Same),
            folderEntry(QStringLiteral("changed.txt"), Folder::Status::Different),
            folderEntry(QStringLiteral("left.txt"), Folder::Status::LeftOnly, Folder::Kind::File, Folder::Kind::Missing),
            folderEntry(QStringLiteral("right.txt"), Folder::Status::RightOnly, Folder::Kind::Missing, Folder::Kind::File),
            folderEntry(QStringLiteral("conflict"), Folder::Status::TypeConflict, Folder::Kind::Directory, Folder::Kind::File),
            folderEntry(QStringLiteral("error.txt"), Folder::Status::Error),
            folderEntry(QStringLiteral("unknown.txt"), Folder::Status::Unknown),
            folderEntry(QStringLiteral("目录"), Folder::Status::Same, Folder::Kind::Directory, Folder::Kind::Directory)};
        result.entries[5].left.error = QStringLiteral("左侧无权读取");
        result.entries[5].explanation = QStringLiteral("文件不可访问");
        result.warnings = QStringList{QStringLiteral("扫描告警<&>")};
        Folder::Options compareOptions;
        compareOptions.recursive = false;
        compareOptions.compareContent = false;
        compareOptions.maximumDepth = 7;
        const R::Model model = R::fromFolder(result, compareOptions);
        QCOMPARE(model.kind, R::Kind::Folder);
        QCOMPARE(model.metadata.leftSource, result.leftRoot);
        QCOMPARE(model.metadata.rightSource, result.rightRoot);
        QVERIFY(!model.metadata.settings.isEmpty());
        QCOMPARE(model.rows.size(), 8);
        QCOMPARE(model.rows[0].state, R::State::Equal);
        QCOMPARE(model.rows[1].state, R::State::Changed);
        QCOMPARE(model.rows[2].state, R::State::LeftOnly);
        QCOMPARE(model.rows[3].state, R::State::RightOnly);
        QCOMPARE(model.rows[4].state, R::State::Conflict);
        QCOMPARE(model.rows[5].state, R::State::Error);
        QCOMPARE(model.rows[6].state, R::State::Unknown);
        QVERIFY(model.rows[4].directory);
        QVERIFY(model.rows[7].directory);
        QCOMPARE(model.rows[7].path, QStringLiteral("目录"));
        QVERIFY(model.rows[5].detail.contains(result.entries[5].explanation));
        QVERIFY(model.warnings.contains(result.warnings.first()));
        const R::Statistics stats = R::statistics(model);
        QCOMPARE(stats.total, qint64(8));
        QCOMPARE(stats.equal, qint64(2));
        QCOMPARE(stats.changed, qint64(1));
        QCOMPARE(stats.conflicts, qint64(1));
        QCOMPARE(stats.errors, qint64(1));
        QCOMPARE(stats.unknown, qint64(1));
        QCOMPARE(stats.directories, qint64(2));
        QCOMPARE(stats.exported, qint64(6));
        QVERIFY(!stats.complete);
        QVERIFY(stats.hasDifferences());
    }

    void uncertainFolderDoesNotClaimNoDifferences_data()
    {
        QTest::addColumn<int>("condition");
        QTest::newRow("unknown-entry") << 0;
        QTest::newRow("error-entry") << 1;
        QTest::newRow("incomplete-scan") << 2;
        QTest::newRow("cancelled-scan") << 3;
        QTest::newRow("scan-error") << 4;
    }
    void uncertainFolderDoesNotClaimNoDifferences()
    {
        QFETCH(int, condition);
        Folder::Result result;
        if (condition < 2)
            result.entries.append(folderEntry(QStringLiteral("uncertain.txt"),
                condition == 0 ? Folder::Status::Unknown : Folder::Status::Error));
        if (condition == 2) result.complete = false;
        if (condition == 3) result.cancelled = true;
        if (condition == 4) result.error = QStringLiteral("Root cannot be read");
        const R::Model model = R::fromFolder(result);
        const R::Statistics stats = R::statistics(model);
        QVERIFY(!stats.complete);
        QVERIFY(!stats.hasDifferences()); // uncertainty is separate from a proved difference.
        R::Options options;
        options.format = R::Format::PlainText;
        options.layout = R::Layout::Summary;
        const QString text = R::render(model, options);
        QVERIFY(!text.contains(QStringLiteral("无差异")));
        QVERIFY(!text.contains(QStringLiteral("No differences"), Qt::CaseInsensitive));
        QVERIFY(text.contains(QStringLiteral("不完整"))
                || text.contains(QStringLiteral("incomplete"), Qt::CaseInsensitive)
                || text.contains(QStringLiteral("错误")));
    }

    void renderMatchesUtf8Files_data()
    {
        QTest::addColumn<bool>("bom");
        QTest::addColumn<int>("format");
        QTest::newRow("html-bom") << true << int(R::Format::Html);
        QTest::newRow("html-utf8") << false << int(R::Format::Html);
        QTest::newRow("text-bom") << true << int(R::Format::PlainText);
        QTest::newRow("text-utf8") << false << int(R::Format::PlainText);
    }
    void renderMatchesUtf8Files()
    {
        QFETCH(bool, bom);
        QFETCH(int, format);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const R::Model model = fixtureModel();
        R::Options options;
        options.utf8Bom = bom;
        options.format = R::Format(format);
        QString error;
        const QString rendered = R::render(model, options, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!rendered.startsWith(QChar(0xfeff)));
        const QString path = dir.filePath(QStringLiteral("中文😀报告.out"));
        QVERIFY2(R::writeFile(path, model, options, &error), qPrintable(error));
        const QByteArray expected = (bom ? QByteArray::fromHex("efbbbf") : QByteArray()) + rendered.toUtf8();
        QCOMPARE(readBytes(path), expected);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{QFileInfo(path).fileName()});
    }

    void legalShortWritesAndDeviceFailures()
    {
        const R::Model model = fixtureModel();
        R::Options options;
        options.format = R::Format::PlainText;
        options.utf8Bom = false;
        QString error;
        ControlledDevice shortWriter(3);
        QVERIFY2(R::write(&shortWriter, model, options, &error), qPrintable(error));
        QCOMPARE(shortWriter.bytes, R::render(model, options).toUtf8());
        QVERIFY(shortWriter.calls > 10);
        for (bool zero : {false, true}) {
            ControlledDevice failing(13, 100, zero);
            error.clear();
            QVERIFY(!R::write(&failing, model, options, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(failing.bytes.size(), 100);
            QVERIFY(failing.calls < 100);
        }
        QBuffer unopened;
        QVERIFY(!R::write(&unopened, model, options, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!R::write(nullptr, model, options, &error));
        QVERIFY(!error.isEmpty());
    }

    void fileFailurePreservesExistingDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("existing.html"));
        const QByteArray original("original bytes must survive\n");
        putBytes(path, original);
        QString error;
        const R::Model model = fixtureModel();
        QVERIFY(!R::writeFile(path, model, {}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(path), original);
        const QString invalid = dir.filePath(QStringLiteral("missing/report.html"));
        QVERIFY(!R::writeFile(invalid, model, {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(invalid));
        QVERIFY(!R::writeFile(dir.path(), model, {}, &error, nullptr, {}, true));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(path), original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("existing.html")});
        QVERIFY2(R::writeFile(path, model, {}, &error, nullptr, {}, true), qPrintable(error));
        QVERIFY(readBytes(path) != original);
    }

    void cancelledFileLeavesNoPartialOutput_data()
    {
        QTest::addColumn<bool>("existing");
        QTest::addColumn<bool>("alreadyCancelled");
        QTest::newRow("new-pre-cancelled") << false << true;
        QTest::newRow("existing-pre-cancelled") << true << true;
        QTest::newRow("new-during-write") << false << false;
        QTest::newRow("existing-during-write") << true << false;
    }
    void cancelledFileLeavesNoPartialOutput()
    {
        QFETCH(bool, existing);
        QFETCH(bool, alreadyCancelled);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("report.html"));
        const QByteArray original("keep this report exactly\0unchanged", 33);
        if (existing) putBytes(path, original);
        const R::Model model = manyRows(1200);
        std::atomic_bool cancelled(alreadyCancelled);
        qint64 highestProgress = -1;
        int callbacks = 0;
        const R::Progress progress = [&](qint64 processed, qint64 total) {
            ++callbacks;
            QVERIFY(processed >= highestProgress);
            QVERIFY(processed <= total);
            QCOMPARE(total, qint64(model.rows.size()));
            highestProgress = processed;
            if (processed > 0) cancelled = true;
        };
        QString error;
        QVERIFY(!R::writeFile(path, model, {}, &error, &cancelled, progress, existing));
        QVERIFY(!error.isEmpty());
        if (!alreadyCancelled) {
            QVERIFY(callbacks > 0);
            QVERIFY(highestProgress > 0);
            QVERIFY(highestProgress < model.rows.size());
        }
        if (existing) QCOMPARE(readBytes(path), original);
        else QVERIFY(!QFile::exists(path));
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 existing ? QStringList{QStringLiteral("report.html")} : QStringList{});
    }

    void interleavedEqualAndIgnoredAreNotShownAsAdditionsOrDeletions()
    {
        R::Options options;
        options.format = R::Format::PlainText;
        options.layout = R::Layout::Interleaved;
        options.includeEqual = options.includeIgnored = true;
        const QString output = R::render(fixtureModel(), options);
        QVERIFY(output.contains(QStringLiteral("\n  中文标题 😀\n")));
        QVERIFY(!output.contains(QStringLiteral("\n- 中文标题 😀")));
        QVERIFY(!output.contains(QStringLiteral("\n+ 中文标题 😀")));
        QCOMPARE(output.count(QStringLiteral("中文标题 😀")), 1);
        QVERIFY(output.contains(QStringLiteral("\n≈   Mixed CASE  \n")));
        QVERIFY(output.contains(QStringLiteral("\n≈ mixed   case\n")));
        QVERIFY(output.contains(QStringLiteral("\n- 末行旧\n+ 末行新\n")));
        QVERIFY(output.contains(QStringLiteral("\n- 仅左内容<&>\n")));
        QVERIFY(output.contains(QStringLiteral("\n+ 仅右内容<&>\n")));
        QVERIFY(!output.contains(QStringLiteral("\n+ \n")));
        QVERIFY(!output.contains(QStringLiteral("\n- \n")));
    }

    void folderMissingSideWithErrorIsNotReportedAsNonexistent()
    {
        Folder::Result result;
        auto entry = folderEntry(QStringLiteral("private.txt"), Folder::Status::Error,
                                 Folder::Kind::Missing, Folder::Kind::File);
        entry.left.error = QStringLiteral("无权读取 private.txt");
        result.entries.append(entry);
        const R::Model model = R::fromFolder(result);
        QCOMPARE(model.rows.size(), 1);
        QVERIFY(model.rows.first().left.contains(entry.left.error));
        QVERIFY(!model.rows.first().left.contains(QStringLiteral("不存在")));
        QVERIFY(!R::statistics(model).complete);
    }

    void invalidOptionsDoNotReplaceExistingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("existing.txt"));
        const QByteArray original("report from yesterday\n");
        putBytes(path, original);
        R::Options options;
        options.rowsPerGroup = 0;
        QString error;
        QVERIFY(!R::writeFile(path, fixtureModel(), options, &error, nullptr, {}, true));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(path), original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("existing.txt")});
    }

    void cannotExportOverComparisonSource()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("source.txt"));
        const QByteArray sourceBytes = readBytes(QFINDTESTDATA("fixtures/text-left.txt"));
        putBytes(path, sourceBytes);
        Text::Document left, right;
        QString error;
        QVERIFY2(Text::Document::load(path, &left, &error), qPrintable(error));
        QVERIFY(Text::Document::decode(QByteArray("changed\n"), &right));
        const auto model = R::fromText(left, right, Text::compare(left.lines(), right.lines()));
        QCOMPARE(model.metadata.leftSource, path);
        QVERIFY(!R::writeFile(path, model, {}, &error, nullptr, {}, true));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(path), sourceBytes);
    }

    void htmlGroupsAndLineNumberOption()
    {
        R::Options options;
        options.rowsPerGroup = 2;
        const R::Model model = fixtureModel();
        const QString html = R::render(model, options);
        QCOMPARE(html.count(QRegularExpression(QStringLiteral("<details(?: open)?>"))), 2);
        QCOMPARE(html.count(QStringLiteral("<details open>")), 1);
        QVERIFY(decodedHtmlText(html).contains(QStringLiteral("左 9 / 右 9")));
        options.showLineNumbers = false;
        QVERIFY(!decodedHtmlText(R::render(model, options)).contains(QStringLiteral("左 9 / 右 9")));
    }

    void createReviewArtifacts()
    {
        const QString directory = QString::fromLocal8Bit(QT_TESTCASE_BUILDDIR) + QStringLiteral("/artifacts");
        QVERIFY(QDir().mkpath(directory));
        const R::Model model = fixtureModel();
        R::Options options;
        options.includeEqual = options.includeIgnored = true;
        options.rowsPerGroup = 4;
        QString error;
        QVERIFY2(R::writeFile(directory + QStringLiteral("/text-report.html"), model, options,
                              &error, nullptr, {}, true), qPrintable(error));
        options.format = R::Format::PlainText;
        options.layout = R::Layout::Interleaved;
        QVERIFY2(R::writeFile(directory + QStringLiteral("/text-report.txt"), model, options,
                              &error, nullptr, {}, true), qPrintable(error));
    }

    void reportStreamsRowsAndCompletesProgress()
    {
        const R::Model model = manyRows(5000);
        R::Options options;
        options.format = R::Format::PlainText;
        options.utf8Bom = false;
        ControlledDevice device(65536);
        qint64 previous = -1;
        int callbacks = 0;
        const R::Progress progress = [&](qint64 processed, qint64 total) {
            ++callbacks;
            QVERIFY(processed >= previous);
            QVERIFY(processed <= total);
            QCOMPARE(total, qint64(model.rows.size()));
            previous = processed;
        };
        QString error;
        QVERIFY2(R::write(&device, model, options, &error, nullptr, progress), qPrintable(error));
        QCOMPARE(previous, qint64(model.rows.size()));
        QVERIFY(callbacks > 1);
        QVERIFY(device.bytes.size() > 100000);
        // A renderer that buffers the entire report before one write breaks this.
        QVERIFY2(device.largestRequest < device.bytes.size() / 4,
                 qPrintable(QStringLiteral("One write requested %1 of %2 bytes")
                            .arg(device.largestRequest).arg(device.bytes.size())));
        QCOMPARE(device.bytes, R::render(model, options).toUtf8());
    }
};

QTEST_GUILESS_MAIN(ReportTests)
#include "tst_report.moc"
