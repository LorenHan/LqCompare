#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "textdiff.h"

using namespace LqCompare::Text;
namespace {
Document decoded(const QByteArray &bytes)
{
    Document document;
    if (!Document::decode(bytes, &document)) qFatal("Fixture could not decode");
    return document;
}
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Could not write fixture");
}
QByteArray readFile(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
}

class TextTests : public QObject {
    Q_OBJECT
private slots:
    void emptyAndEndings()
    {
        QCOMPARE(decoded({}).lines().size(), 0);
        QCOMPARE(decoded("\n").lines().size(), 1);
        QCOMPARE(decoded("\n").lines().first().text, QString());
        Document doc = decoded("one\r\ntwo\rthree\nfour");
        QCOMPARE(doc.lines().size(), 4);
        QCOMPARE(doc.bytes(), QByteArray("one\r\ntwo\rthree\nfour"));
        QVERIFY(doc.eolDescription().startsWith("Mixed"));
        QCOMPARE(doc.normalizedText(), QStringLiteral("one\ntwo\nthree\nfour"));
        QVERIFY(!doc.isModified());
    }
    void unicodeRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QByteArray>("codec");
        QTest::newRow("utf8") << QByteArray::fromHex("e4b8ade696870af09f9880") << QByteArray("UTF-8");
        QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbfe4b8ade696870a") << QByteArray("UTF-8");
        QTest::newRow("utf16le") << QByteArray::fromHex("fffe2d4e87650d000a003dd800de") << QByteArray("UTF-16LE");
        QTest::newRow("utf16be") << QByteArray::fromHex("feff4e2d6587000d000ad83dde00") << QByteArray("UTF-16BE");
        QTest::newRow("utf32le") << QByteArray::fromHex("fffe00002d4e0000876500000d0000000a00000000f60100") << QByteArray("UTF-32LE");
        QTest::newRow("utf32be") << QByteArray::fromHex("0000feff00004e2d000065870000000d0000000a0001f600") << QByteArray("UTF-32BE");
        QTest::newRow("utf32le-empty") << QByteArray::fromHex("fffe0000") << QByteArray("UTF-32LE");
        QTest::newRow("utf32be-empty") << QByteArray::fromHex("0000feff") << QByteArray("UTF-32BE");
    }
    void unicodeRoundTrip()
    {
        QFETCH(QByteArray, input); QFETCH(QByteArray, codec);
        auto doc = decoded(input);
        QCOMPARE(doc.codecName(), codec);
        QCOMPARE(doc.decodingErrors(), 0);
        QCOMPARE(doc.bytes(), input);
        QVERIFY(!doc.isModified());
    }
    void unsafeDecodingAndLegacy()
    {
        auto invalid = decoded(QByteArray::fromHex("6162ff63e4b8"));
        QVERIFY(invalid.decodingErrors() >= 1);
        QVERIFY(!invalid.canEdit());
        QVERIFY(!invalid.setNormalizedText("changed"));
        QVERIFY(!invalid.warning().isEmpty());
        auto binary = decoded(QByteArray("a\0b", 3));
        QVERIFY(!binary.canEdit());
        const auto paragraph = decoded(QByteArray::fromHex("61e280a962"));
        QVERIFY(!paragraph.canEdit());
        QCOMPARE(paragraph.bytes(), QByteArray::fromHex("61e280a962"));
        Document latin;
        QVERIFY(Document::decode(QByteArray::fromHex("636166e9"), &latin, nullptr, "ISO-8859-1"));
        QCOMPARE(latin.normalizedText(), QString::fromUtf8(QByteArray::fromHex("636166c3a9")));
        QVERIFY(latin.setNormalizedText(QString::fromUtf8(QByteArray::fromHex("e4b8ade69687"))));
        QString error;
        latin.bytes(&error);
        QVERIFY(!error.isEmpty());
        auto incomplete = decoded("valid");
        QVERIFY(incomplete.setNormalizedText(QString(QChar(0xd800))));
        incomplete.bytes(&error);
        QVERIFY(!error.isEmpty());
    }
    void utf32RejectsMalformedScalarsAndTruncatedUnits()
    {
        const QList<QByteArray> damaged = {
            QByteArray::fromHex("fffe000000001100"), // Above U+10FFFF.
            QByteArray::fromHex("fffe000000d80000"), // Surrogate scalar, LE.
            QByteArray::fromHex("0000feff0000dfff"), // Surrogate scalar, BE.
            QByteArray::fromHex("0000feffffffffff"), // Invalid scalar.
            QByteArray::fromHex("fffe0000610000"),   // Truncated LE unit.
            QByteArray::fromHex("0000feff000061")    // Truncated BE unit.
        };
        QTemporaryDir directory;
        for (const auto &input : damaged) {
            auto document = decoded(input);
            QCOMPARE(document.decodingErrors(), 1);
            QCOMPARE(document.normalizedText(), QString(QChar::ReplacementCharacter));
            QVERIFY(!document.canEdit());
            QString error;
            QVERIFY(!document.setNormalizedText("replacement", &error));
            QVERIFY(!document.saveAs(directory.filePath("must-not-exist"), false, &error));
            QVERIFY(!QFile::exists(directory.filePath("must-not-exist")));
        }
    }
    void utf32EditSavePreservesBomAndEndianness()
    {
        QTemporaryDir directory;
        const QList<QByteArray> inputs = {
            QByteArray::fromHex("fffe0000610000000d0000000a000000"),
            QByteArray::fromHex("0000feff000000610000000d0000000a")
        };
        const QList<QByteArray> expected = {
            QByteArray::fromHex("fffe000000f601000d0000000a000000"),
            QByteArray::fromHex("0000feff0001f6000000000d0000000a")
        };
        const QString edited = QString::fromUtf8(QByteArray::fromHex("f09f98800a"));
        for (int i = 0; i < inputs.size(); ++i) {
            const QString path = directory.filePath(QString::number(i));
            writeFile(path, inputs[i]);
            Document document;
            QVERIFY(Document::load(path, &document));
            QVERIFY(document.canEdit());
            QVERIFY(document.setNormalizedText(edited));
            QVERIFY(document.save());
            QCOMPARE(readFile(path), expected[i]);
            QVERIFY(!document.isModified());
            // Explicit decoding also supports BOM-less UTF-32 without adding a BOM.
            Document noBom;
            QVERIFY(Document::decode(expected[i].mid(4), &noBom, nullptr, document.codecName()));
            QVERIFY(!noBom.hasBom());
            QCOMPARE(noBom.normalizedText(), edited);
            QCOMPARE(noBom.bytes(), expected[i].mid(4));
        }
    }
    void excessiveLineCountsFailWithoutReplacingCurrentDocument()
    {
        Document document = decoded("keep me\n");
        QString error;
        QVERIFY(!Document::decode(QByteArray(Document::MaximumLines + 1, '\n'), &document, &error));
        QVERIFY(error.contains("500,000"));
        QCOMPARE(document.bytes(), QByteArray("keep me\n"));
        QVERIFY(!document.setNormalizedText(QString(Document::MaximumLines, QLatin1Char('\n')) + QLatin1Char('x'), &error));
        QCOMPARE(document.bytes(), QByteArray("keep me\n"));
    }
    void insertionDeletionReplacement()
    {
        auto left = decoded("a\nb\nc\nd\n");
        auto right = decoded("a\nnew\nb\nchanged\nd\n");
        const auto result = compare(left.lines(), right.lines());
        QCOMPARE(result.differences.size(), 2);
        QCOMPARE(result.blocks[result.differences[0]].change, Change::Insert);
        QCOMPARE(result.blocks[result.differences[1]].change, Change::Replace);
        QCOMPARE(result.rows[1].leftLine, -1);
        QCOMPARE(result.rows[1].rightLine, 1);
        QCOMPARE(result.rows[2].leftLine, 1);
        QCOMPARE(result.rows[2].rightLine, 2);
        const auto reversed = compare(right.lines(), left.lines());
        QCOMPARE(reversed.blocks[reversed.differences[0]].change, Change::Delete);
        const auto empty = compare({}, left.lines());
        QCOMPARE(empty.blocks.size(), 1);
        QCOMPARE(empty.blocks.first().change, Change::Insert);
        QVERIFY(compare({}, {}).blocks.isEmpty());
    }
    void eolRulesIndependent()
    {
        auto a = decoded("a\r\nb\r\n"), b = decoded("a\nb\n"), c = decoded("a\nb");
        CompareOptions options;
        QVERIFY(compare(a.lines(), b.lines(), options).differences.isEmpty());
        QVERIFY(!compare(a.lines(), c.lines(), options).differences.isEmpty());
        options.ignoreFinalNewline = true;
        QVERIFY(compare(a.lines(), c.lines(), options).differences.isEmpty());
        options.ignoreEol = false;
        QCOMPARE(compare(a.lines(), b.lines(), options).differences.size(), 1);
        QVERIFY(compare(b.lines(), c.lines(), options).differences.isEmpty());
    }
    void ignoreRulesKeepOriginals()
    {
        const auto a = decoded(" Foo  BAR \nfoo bar\n"), b = decoded("foo\tbar\nfoobar\n");
        CompareOptions options;
        options.ignoreCase = true;
        options.whitespace = Whitespace::IgnoreChanges;
        auto result = compare(a.lines(), b.lines(), options);
        QCOMPARE(result.ignoredBlocks, 1);
        QCOMPARE(result.differences.size(), 1);
        options.whitespace = Whitespace::IgnoreAll;
        result = compare(a.lines(), b.lines(), options);
        QVERIFY(result.differences.isEmpty());
        QCOMPARE(a.bytes(), QByteArray(" Foo  BAR \nfoo bar\n"));
    }
    void editPreservesMixedEndings()
    {
        auto doc = decoded("first\r\nsecond\rthird\nlast");
        QVERIFY(doc.setNormalizedText("first\nsecond\nthird\nlast"));
        QVERIFY(!doc.isModified());
        QVERIFY(doc.setNormalizedText("first\nsecond changed\nthird\nlast"));
        QCOMPARE(doc.bytes(), QByteArray("first\r\nsecond changed\rthird\nlast"));
        doc.setEol(Eol::CRLF);
        QCOMPARE(doc.bytes(), QByteArray("first\r\nsecond changed\r\nthird\r\nlast"));
    }
    void guardedAtomicSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("left.txt");
        writeFile(path, "original\r\n");
        Document document;
        QVERIFY(Document::load(path, &document));
        QVERIFY(document.setNormalizedText("edited\n"));
        writeFile(path, "external\r\n");
        QString error;
        QVERIFY(!document.save(&error));
        QVERIFY(error.contains("changed on disk"));
        QCOMPARE(readFile(path), QByteArray("external\r\n"));
        QVERIFY(document.isModified());
        const auto copy = directory.filePath("copy.txt");
        QVERIFY(document.saveAs(copy, false, &error));
        QCOMPARE(readFile(copy), QByteArray("edited\r\n"));
        QVERIFY(!document.isModified());
        QVERIFY(document.setNormalizedText("another\n"));
        QVERIFY(!document.saveAs(path, false, &error));
        QVERIFY(document.save(&error));
        QCOMPARE(readFile(copy), QByteArray("another\r\n"));
    }
    void randomAlignmentsAreMinimal()
    {
        quint32 seed = 17;
        auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed; };
        for (int sample = 0; sample < 1200; ++sample) {
            QVector<Line> a, b;
            const int n = next() % 24, m = next() % 24;
            for (int i = 0; i < n; ++i) a.append({QString::number(next() % 7), Eol::LF});
            for (int i = 0; i < m; ++i) b.append({QString::number(next() % 7), Eol::LF});
            QVector<int> dp((n + 1) * (m + 1));
            for (int i = 1; i <= n; ++i) for (int j = 1; j <= m; ++j)
                dp[i * (m + 1) + j] = a[i - 1] == b[j - 1]
                    ? 1 + dp[(i - 1) * (m + 1) + j - 1]
                    : qMax(dp[(i - 1) * (m + 1) + j], dp[i * (m + 1) + j - 1]);
            const auto result = compare(a, b);
            int leftCursor = 0, rightCursor = 0, equal = 0;
            for (const auto &block : result.blocks) {
                QCOMPARE(block.leftStart, leftCursor);
                QCOMPARE(block.rightStart, rightCursor);
                leftCursor += block.leftCount;
                rightCursor += block.rightCount;
                if (block.change == Change::Equal) {
                    QCOMPARE(block.leftCount, block.rightCount);
                    for (int i = 0; i < block.leftCount; ++i)
                        QVERIFY(a[block.leftStart + i] == b[block.rightStart + i]);
                    equal += block.leftCount;
                }
            }
            QCOMPARE(leftCursor, n); QCOMPARE(rightCursor, m);
            QCOMPARE(equal, dp[n * (m + 1) + m]);
            QVERIFY(!result.alignmentLimited);
            const auto again = compare(a, b);
            QCOMPARE(again.rows.size(), result.rows.size());
            for (int i = 0; i < result.rows.size(); ++i) {
                QCOMPARE(again.rows[i].leftLine, result.rows[i].leftLine);
                QCOMPARE(again.rows[i].rightLine, result.rows[i].rightLine);
            }
        }
    }
    void tenThousandUnrelatedLines()
    {
        QVector<Line> a, b;
        for (int i = 0; i < 10000; ++i) {
            a.append({QStringLiteral("left %1").arg(i), Eol::LF});
            b.append({QStringLiteral("right %1").arg(i), Eol::LF});
        }
        QElapsedTimer timer; timer.start();
        const auto result = compare(a, b);
        QVERIFY2(timer.elapsed() < 2000, "10k unrelated lines exceeded 2s");
        QCOMPARE(result.blocks.size(), 1);
        QCOMPARE(result.blocks.first().change, Change::Replace);
        QVERIFY(!result.alignmentLimited);
        for (int i = 0; i < 10000; i += 100) b[i] = a[i];
        timer.restart();
        const auto typical = compare(a, b);
        QVERIFY2(timer.elapsed() < 2000, "bounded adversarial alignment exceeded 2s");
        QVERIFY(!typical.differences.isEmpty());
    }
};
QTEST_APPLESS_MAIN(TextTests)
#include "tst_text.moc"
