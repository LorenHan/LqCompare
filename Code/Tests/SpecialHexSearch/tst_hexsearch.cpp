#include <QtTest>
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <limits>

#include "hexdiff.h"
#include "hexsearch.h"
#include "hexcomparesession.h"

using namespace LqCompare::Hex;
using LqCompare::HexCompareSession;

namespace {
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(bytes) != bytes.size()) qFatal("Cannot write search fixture");
}
struct Fixture {
    QTemporaryDir directory;
    QString left = directory.filePath(QStringLiteral("left.bin"));
    QString right = directory.filePath(QStringLiteral("right.bin"));
    Comparison comparison;
    Fixture(const QByteArray &a, const QByteArray &b)
    {
        if (!directory.isValid()) qFatal("Cannot create search fixture directory");
        writeFile(left, a);
        writeFile(right, b);
        QString error;
        if (!comparison.load(left, right, &error)) qFatal("%s", qPrintable(error));
    }
};

SearchResult complete(SearchCursor &cursor, int stepBytes = 31)
{
    QElapsedTimer timer;
    timer.start();
    int steps = 0;
    while (cursor.result().state == SearchState::Running && steps++ < 1000000 && timer.elapsed() < 10000)
        cursor.step(stepBytes, 20);
    return cursor.result();
}

SearchResult search(const Comparison &comparison, const SearchPattern &pattern,
                    SearchOptions options = {}, int stepBytes = 31)
{
    SearchCursor cursor;
    QString error;
    if (!cursor.start(&comparison, pattern, options, &error)) {
        SearchResult result;
        result.state = SearchState::Error;
        result.message = error;
        return result;
    }
    return complete(cursor, stepBytes);
}

uchar fold(uchar c, bool sensitive)
{
    return !sensitive && c >= 'A' && c <= 'Z' ? uchar(c + ('a' - 'A')) : c;
}
bool word(uchar c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
SearchResult oracle(const QByteArray &bytes, const SearchPattern &pattern, SearchOptions options)
{
    QVector<qint64> candidates;
    const qint64 last = bytes.size() - pattern.bytes.size();
    for (qint64 pos = 0; pos <= last; ++pos) {
        bool equal = true;
        for (int i = 0; i < pattern.bytes.size(); ++i)
            if (fold(uchar(bytes.at(int(pos) + i)), pattern.caseSensitive) != fold(uchar(pattern.bytes.at(i)), pattern.caseSensitive)) {
                equal = false;
                break;
            }
        if (pattern.wholeWords && ((pos > 0 && word(uchar(bytes.at(int(pos) - 1))))
            || (pos + pattern.bytes.size() < bytes.size() && word(uchar(bytes.at(int(pos) + pattern.bytes.size())))))) equal = false;
        if (equal) candidates.append(pos);
    }
    if (options.backward) std::reverse(candidates.begin(), candidates.end());
    SearchResult result;
    result.state = SearchState::NotFound;
    for (qint64 pos : candidates) {
        if (options.backward ? pos <= options.startOffset : pos >= options.startOffset) {
            result.state = SearchState::Found;
            result.offset = pos;
            result.length = pattern.bytes.size();
            return result;
        }
    }
    if (options.wrap && !candidates.isEmpty()) {
        result.state = SearchState::Found;
        result.offset = candidates.first();
        result.length = pattern.bytes.size();
        result.wrapped = true;
    }
    return result;
}
}

class SpecialHexSearchTests : public QObject {
    Q_OBJECT
private slots:
    void hexPatternParser_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("contiguous") << QStringLiteral("00FFaB7f") << true << QByteArray::fromHex("00ffab7f");
        QTest::newRow("groups") << QStringLiteral("aabb CC ddEE") << true << QByteArray::fromHex("aabbccddee");
        QTest::newRow("whitespace") << QStringLiteral(" \t00\nff\rAB ") << true << QByteArray::fromHex("00ffab");
        QTest::newRow("maximum") << QString(8192, QLatin1Char('A')) << true << QByteArray(4096, char(0xaa));
        for (const char *bad : {"", " ", "A", "A B", "ABC DEF", "0xAA", "GG", "??", "AA-FF", "AA:FF", "+1"})
            QTest::newRow(bad[0] ? bad : "empty") << QString::fromLatin1(bad) << false << QByteArray();
        QTest::newRow("oversize") << QString(8194, QLatin1Char('A')) << false << QByteArray();
        QTest::newRow("unicode-digits") << QString::fromUtf8("１２") << false << QByteArray();
    }
    void hexPatternParser()
    {
        QFETCH(QString, input);
        QFETCH(bool, valid);
        QFETCH(QByteArray, expected);
        SearchPattern pattern;
        QString error = QStringLiteral("old error");
        QCOMPARE(parseHexPattern(input, &pattern, &error), valid);
        QCOMPARE(error.isEmpty(), valid);
        if (valid) {
            QCOMPARE(pattern.bytes, expected);
            QVERIFY(pattern.caseSensitive);
            QVERIFY(!pattern.wholeWords);
        }
    }
    void textPatternsUseUtf8AndByteLimit()
    {
        SearchPattern pattern;
        QString error;
        const QString text = QString::fromUtf8("AbCé中🙂");
        QVERIFY(textPattern(text, false, true, &pattern, &error));
        QCOMPARE(pattern.bytes, text.toUtf8());
        QVERIFY(!pattern.caseSensitive);
        QVERIFY(pattern.wholeWords);
        QVERIFY(textPattern(QString(4096, QLatin1Char('x')), true, false, &pattern, &error));
        QCOMPARE(pattern.bytes.size(), 4096);
        QVERIFY(!textPattern(QString(4097, QLatin1Char('x')), true, false, &pattern, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(textPattern(QString(1365, QChar(0x4e2d)), true, false, &pattern, &error));
        QCOMPARE(pattern.bytes.size(), 4095);
        QVERIFY(!textPattern(QString(1366, QChar(0x4e2d)), true, false, &pattern, &error));
        QVERIFY(!textPattern(QString(), true, false, &pattern, &error));
        QVERIFY(!textPattern(QStringLiteral("a"), true, false, nullptr, &error));
        QVERIFY(!parseHexPattern(QStringLiteral("ff"), nullptr, &error));
    }

    void matchingSemantics_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QByteArray>("needle");
        QTest::addColumn<bool>("sensitive");
        QTest::addColumn<bool>("wholeWords");
        QTest::addColumn<qint64>("expected");
        QTest::newRow("ascii-insensitive") << QByteArray("xxABCyy") << QByteArray("abc") << false << false << qint64(2);
        QTest::newRow("ascii-sensitive") << QByteArray("xxABCyy") << QByteArray("abc") << true << false << qint64(-1);
        QTest::newRow("utf8-exact") << QString::fromUtf8("a中🙂b").toUtf8() << QString::fromUtf8("中🙂").toUtf8() << true << false << qint64(1);
        QTest::newRow("nonascii-not-folded") << QString::fromUtf8("Ä").toUtf8() << QString::fromUtf8("ä").toUtf8() << false << false << qint64(-1);
        QTest::newRow("word-skip-underscore-digit") << QByteArray("a_foo foo1 foo!foo") << QByteArray("foo") << true << true << qint64(11);
        QTest::newRow("nonascii-is-word-boundary") << QString::fromUtf8("éfooé").toUtf8() << QByteArray("foo") << true << true << qint64(2);
        QTest::newRow("word-at-head") << QByteArray("foo!") << QByteArray("foo") << true << true << qint64(0);
        QTest::newRow("word-at-tail") << QByteArray("!foo") << QByteArray("foo") << true << true << qint64(1);
        QTest::newRow("complete-whole-file") << QByteArray("abc") << QByteArray("abc") << true << true << qint64(0);
        QTest::newRow("incomplete-tail") << QByteArray("ab") << QByteArray("abc") << true << false << qint64(-1);
        QTest::newRow("incomplete-head") << QByteArray("bc") << QByteArray("abc") << true << false << qint64(-1);
        QTest::newRow("never-match-across-eof") << QByteArray("cdab") << QByteArray("abcd") << true << false << qint64(-1);
        QTest::newRow("null-and-high-bit") << QByteArray::fromHex("10ff008020") << QByteArray::fromHex("ff0080") << false << false << qint64(1);
        QTest::newRow("empty-file") << QByteArray() << QByteArray("a") << true << false << qint64(-1);
    }
    void matchingSemantics()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QByteArray, needle);
        QFETCH(bool, sensitive);
        QFETCH(bool, wholeWords);
        QFETCH(qint64, expected);
        Fixture fixture(bytes, bytes);
        SearchPattern pattern{needle, sensitive, wholeWords};
        for (bool backward : {false, true}) {
            SearchOptions options;
            options.backward = backward;
            options.startOffset = backward ? bytes.size() : 0;
            const SearchResult actual = search(fixture.comparison, pattern, options, 1);
            // Backward may select a different occurrence; the independent oracle
            // defines that side while the table anchors the forward answer.
            const SearchResult reference = oracle(bytes, pattern, options);
            QCOMPARE(actual.state, expected < 0 ? SearchState::NotFound : SearchState::Found);
            QCOMPARE(actual.offset, backward ? reference.offset : expected);
        }
    }

    void inclusiveOffsetsOverlapsAndWrap()
    {
        const QByteArray bytes("ababa");
        Fixture fixture(bytes, QByteArray("right"));
        SearchPattern pattern{QByteArray("aba"), true, false};
        for (bool backward : {false, true}) for (bool wrap : {false, true}) {
            for (qint64 start = -1; start <= bytes.size(); ++start) {
                SearchOptions options;
                options.startOffset = start;
                options.backward = backward;
                options.wrap = wrap;
                const auto reference = oracle(bytes, pattern, options);
                const auto actual = search(fixture.comparison, pattern, options, 2);
                QCOMPARE(actual.state, reference.state);
                QCOMPARE(actual.offset, reference.offset);
                if (actual.state == SearchState::Found) {
                    QCOMPARE(actual.length, 3);
                    QCOMPARE(actual.wrapped, reference.wrapped);
                }
            }
        }
        SearchOptions right;
        right.left = false;
        QCOMPARE(search(fixture.comparison, {QByteArray("right"), true, false}, right).offset, qint64(0));
        QCOMPARE(search(fixture.comparison, pattern, right).state, SearchState::NotFound);
    }

    void scanAndStepBoundaryMatches()
    {
        const int boundary = Comparison::MaximumReadBytes;
        QByteArray bytes(boundary * 2 + 64, '.');
        const QByteArray needle("aBaBaCb");
        for (int pos : {0, 29, boundary - 3, bytes.size() - needle.size()}) bytes.replace(pos, needle.size(), needle);
        Fixture fixture(bytes, bytes);
        const SearchPattern pattern{needle, true, false};
        for (bool backward : {false, true}) {
            SearchOptions options;
            options.backward = backward;
            options.wrap = false;
            QVector<qint64> positions = {0, 29, boundary - 3, bytes.size() - needle.size()};
            if (backward) std::reverse(positions.begin(), positions.end());
            options.startOffset = backward ? bytes.size() : 0;
            for (qint64 pos : positions) {
                const auto result = search(fixture.comparison, pattern, options, 4093);
                QCOMPARE(result.state, SearchState::Found);
                QCOMPARE(result.offset, pos);
                options.startOffset = pos + (backward ? -1 : 1);
            }
            QCOMPARE(search(fixture.comparison, pattern, options, 4093).state, SearchState::NotFound);
        }
        // Whole-word context must survive both streaming boundaries and wraps.
        SearchPattern whole{needle, true, true};
        SearchOptions options;
        options.startOffset = boundary - 5;
        QCOMPARE(search(fixture.comparison, whole, options, 2).offset, qint64(boundary - 3));
    }

    void cancellationAndScanQuotaRemainDistinct()
    {
        Fixture fixture(QByteArray(10000, 'a'), QByteArray());
        SearchPattern pattern{QByteArray("z"), true, false};
        SearchOptions options;
        options.wrap = false;
        SearchCursor cursor;
        QVERIFY(cursor.start(&fixture.comparison, pattern, options));
        QCOMPARE(cursor.step(17, 20), SearchState::Running);
        QVERIFY(cursor.result().scannedBytes <= 17);
        cursor.cancel();
        QCOMPARE(cursor.result().state, SearchState::Cancelled);
        QVERIFY(!cursor.result().message.isEmpty());
        const qint64 cancelledAt = cursor.result().scannedBytes;
        QCOMPARE(cursor.step(1000, 20), SearchState::Cancelled);
        QCOMPARE(cursor.result().scannedBytes, cancelledAt);
        options.maximumScanBytes = 100;
        QVERIFY(cursor.start(&fixture.comparison, pattern, options));
        const auto limited = complete(cursor, 13);
        QCOMPARE(limited.state, SearchState::LimitReached);
        QVERIFY(limited.scannedBytes <= 100);
        QVERIFY(!limited.message.isEmpty());
        QCOMPARE(limited.offset, qint64(-1));
        QCOMPARE(cursor.step(), SearchState::LimitReached);
        options.maximumScanBytes = 10000;
        const auto completeResult = search(fixture.comparison, pattern, options, 101);
        QCOMPARE(completeResult.state, SearchState::NotFound);
        QCOMPARE(completeResult.scannedBytes, qint64(10000));
    }

    void elapsedQuotaIsNotNotFound()
    {
        Fixture fixture(QByteArray(1024 * 1024, 'a'), QByteArray());
        SearchCursor cursor;
        SearchOptions options;
        options.maximumElapsedMs = 1;
        QVERIFY(cursor.start(&fixture.comparison, {QByteArray("z"), true, false}, options));
        QTest::qWait(10);
        QCOMPARE(cursor.step(1, 1), SearchState::LimitReached);
        QVERIFY(!cursor.result().message.isEmpty());
        QCOMPARE(cursor.result().offset, qint64(-1));
    }

    void invalidCursorInputsReportErrors()
    {
        Fixture fixture(QByteArray("abc"), QByteArray("abc"));
        SearchPattern pattern{QByteArray("a"), true, false};
        SearchCursor cursor;
        SearchOptions options;
        QString error;
        Comparison unloaded;
        QVERIFY(!cursor.start(nullptr, pattern, options, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!cursor.start(&unloaded, pattern, options, &error));
        QVERIFY(!cursor.start(&fixture.comparison, {}, options, &error));
        QVERIFY(!cursor.start(&fixture.comparison, {QByteArray(4097, 'x'), true, false}, options, &error));
        for (qint64 value : {std::numeric_limits<qint64>::min(), qint64(-2), qint64(4), std::numeric_limits<qint64>::max()}) {
            options.startOffset = value;
            QVERIFY(!cursor.start(&fixture.comparison, pattern, options, &error));
            QVERIFY(!error.isEmpty());
        }
        options = {};
        options.maximumScanBytes = -1;
        QVERIFY(!cursor.start(&fixture.comparison, pattern, options, &error));
        options = {};
        options.maximumElapsedMs = -1;
        QVERIFY(!cursor.start(&fixture.comparison, pattern, options, &error));
    }

    void randomizedMatchesIndependentOracle()
    {
        QRandomGenerator random(0x51484558U);
        const QByteArray alphabet("aAbB_!.12\0\xff", 12);
        for (int trial = 0; trial < 80; ++trial) {
            QByteArray bytes(17 + trial % 37, '\0');
            for (char &byte : bytes) byte = alphabet.at(int(random.bounded(quint32(alphabet.size()))));
            QByteArray needle;
            if (trial % 3) {
                const int begin = int(random.bounded(quint32(bytes.size())));
                needle = bytes.mid(begin, 1 + int(random.bounded(5U)));
            } else needle = QByteArray("aBb");
            Fixture fixture(bytes, bytes);
            SearchPattern pattern{needle, bool(trial % 2), bool(trial % 4 < 2)};
            for (bool backward : {false, true}) for (bool wrap : {false, true}) {
                SearchOptions options;
                options.backward = backward;
                options.wrap = wrap;
                options.startOffset = qint64(random.bounded(quint32(bytes.size() + 2))) - 1;
                const auto reference = oracle(bytes, pattern, options);
                const auto actual = search(fixture.comparison, pattern, options, 1 + trial % 11);
                QCOMPARE(actual.state, reference.state);
                QCOMPARE(actual.offset, reference.offset);
                QCOMPARE(actual.length, reference.length);
                if (actual.state == SearchState::Found) QCOMPARE(actual.wrapped, reference.wrapped);
            }
        }
    }
};

QTEST_MAIN(SpecialHexSearchTests)
#include "tst_hexsearch.moc"
