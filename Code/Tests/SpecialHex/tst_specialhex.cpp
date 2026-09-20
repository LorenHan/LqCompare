#include <QtTest>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QTemporaryDir>
#include <algorithm>
#include <limits>

#include "hexdiff.h"
#include "hexcomparesession.h"

using LqCompare::Hex::Comparison;
using LqCompare::Hex::Difference;

namespace {
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(bytes) != bytes.size() || !file.flush())
        qFatal("Cannot create binary test fixture: %s", qPrintable(file.errorString()));
}

void createSparseFile(const QString &path, qint64 size)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.resize(size))
        qFatal("Cannot create sparse binary test fixture: %s", qPrintable(file.errorString()));
}

void patchByte(const QString &path, qint64 offset, char byte)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite) || !file.seek(offset) || file.write(&byte, 1) != 1)
        qFatal("Cannot patch binary test fixture: %s", qPrintable(file.errorString()));
}

QVector<Difference> expectedRegions(const QByteArray &mask)
{
    QVector<Difference> result;
    for (int offset = 0; offset < mask.size(); ++offset) {
        if (mask.at(offset) != '1') continue;
        if (!result.isEmpty() && result.last().offset + result.last().length == offset)
            ++result.last().length;
        else result.append({offset, 1});
    }
    return result;
}
}

class SpecialHexTests : public QObject {
    Q_OBJECT
private slots:
    void unloadedComparison()
    {
        Comparison comparison;
        QVERIFY(!comparison.isLoaded());
        QCOMPARE(comparison.extent(), qint64(0));
        QCOMPARE(comparison.bitmapBytes(), qint64(0));
        QCOMPARE(comparison.differentBytes(), qint64(0));
        QCOMPARE(comparison.differenceRegions(), qint64(0));
        QVERIFY(comparison.path(true).isEmpty());
        QVERIFY(!comparison.differs(0));
        QVERIFY(!comparison.firstDifference().isValid());
        QVERIFY(!comparison.lastDifference().isValid());
        QVERIFY(!comparison.nextDifference(-1).isValid());
        QVERIFY(!comparison.previousDifference(0).isValid());
        QString error;
        QVERIFY(comparison.read(true, 0, 0, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void preservesEveryByteValue()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray allBytes(256, '\0');
        for (int i = 0; i < allBytes.size(); ++i) allBytes[i] = char(i);
        const QString left = directory.filePath("all-values.bin");
        const QString right = directory.filePath("identical.bin");
        writeFile(left, allBytes);
        writeFile(right, allBytes);
        Comparison comparison;
        QString error = QStringLiteral("stale error");
        QVERIFY2(comparison.load(left, right, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(comparison.read(true, 0, 256, &error), allBytes);
        QCOMPARE(comparison.read(false, 0, 256, &error), allBytes);
        QCOMPARE(comparison.read(true, 127, 3, &error), allBytes.mid(127, 3));
        QCOMPARE(comparison.size(true), qint64(256));
        QCOMPARE(comparison.bitmapBytes(), qint64(32));
        QCOMPARE(comparison.differentBytes(), qint64(0));
        QCOMPARE(comparison.differenceRegions(), qint64(0));
        QVERIFY(!comparison.firstDifference().isValid());
        QCOMPARE(comparison.path(true), QFileInfo(left).absoluteFilePath());
        QFile source(left);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), allBytes);
    }

    void absoluteOffsetDifferences_data()
    {
        QTest::addColumn<QByteArray>("left");
        QTest::addColumn<QByteArray>("right");
        QTest::addColumn<QByteArray>("mask");
        QTest::newRow("both-empty") << QByteArray() << QByteArray() << QByteArray();
        QTest::newRow("left-empty") << QByteArray() << QByteArray("abc") << QByteArray("111");
        QTest::newRow("right-empty") << QByteArray("abc") << QByteArray() << QByteArray("111");
        QTest::newRow("equal") << QByteArray("abcdefghi") << QByteArray("abcdefghi") << QByteArray("000000000");
        QTest::newRow("first-byte") << QByteArray("abcdefghi") << QByteArray("Abcdefghi") << QByteArray("100000000");
        QTest::newRow("last-byte") << QByteArray("abcdefghi") << QByteArray("abcdefghI") << QByteArray("000000001");
        QTest::newRow("both-ends") << QByteArray("abcdefghi") << QByteArray("AbcdefghI") << QByteArray("100000001");
        QTest::newRow("all-different") << QByteArray("abcdefghi") << QByteArray("ABCDEFGHI") << QByteArray("111111111");
        QTest::newRow("contiguous") << QByteArray("abcdefghi") << QByteArray("abCDEFghi") << QByteArray("001111000");
        QTest::newRow("alternating") << QByteArray("abcdefghi") << QByteArray("AbCdEfGhI") << QByteArray("101010101");
        QTest::newRow("insert-absolute-offset") << QByteArray("abcdef") << QByteArray("abXcdef") << QByteArray("0011111");
        QTest::newRow("delete-absolute-offset") << QByteArray("abcdef") << QByteArray("abdef") << QByteArray("001111");
        QTest::newRow("longer-left") << QByteArray("abcdefghi") << QByteArray("abc") << QByteArray("000111111");
        QTest::newRow("longer-right") << QByteArray("abc") << QByteArray("abcdefghi") << QByteArray("000111111");
        QTest::newRow("partial-bitmap-byte") << QByteArray("abc") << QByteArray("ABC") << QByteArray("111");
        QTest::newRow("embedded-null") << QByteArray("a\0b\0", 4) << QByteArray("a\1b\0", 4) << QByteArray("0100");
    }

    void absoluteOffsetDifferences()
    {
        QFETCH(QByteArray, left);
        QFETCH(QByteArray, right);
        QFETCH(QByteArray, mask);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath("left.bin"), left);
        writeFile(directory.filePath("right.bin"), right);
        Comparison comparison;
        QString error;
        QVERIFY2(comparison.load(directory.filePath("left.bin"), directory.filePath("right.bin"), &error), qPrintable(error));
        QCOMPARE(comparison.size(true), qint64(left.size()));
        QCOMPARE(comparison.size(false), qint64(right.size()));
        QCOMPARE(comparison.extent(), qint64(mask.size()));
        QCOMPARE(comparison.differentBytes(), qint64(mask.count('1')));
        const auto regions = expectedRegions(mask);
        QCOMPARE(comparison.differenceRegions(), qint64(regions.size()));
        for (int i = 0; i < mask.size(); ++i) {
            QCOMPARE(comparison.differs(i), mask.at(i) == '1');
            const Difference actual = comparison.differenceAt(i);
            const auto expected = std::find_if(regions.cbegin(), regions.cend(), [i](Difference region) {
                return i >= region.offset && i < region.offset + region.length;
            });
            QCOMPARE(actual.isValid(), expected != regions.cend());
            if (actual.isValid()) {
                QCOMPARE(actual.offset, expected->offset);
                QCOMPARE(actual.length, expected->length);
            }
        }
        Difference cursor = comparison.firstDifference();
        for (const Difference &region : regions) {
            QCOMPARE(cursor.offset, region.offset);
            QCOMPARE(cursor.length, region.length);
            cursor = comparison.nextDifference(cursor.offset);
        }
        QVERIFY(!cursor.isValid());
        cursor = comparison.lastDifference();
        for (int i = regions.size() - 1; i >= 0; --i) {
            QCOMPARE(cursor.offset, regions.at(i).offset);
            QCOMPARE(cursor.length, regions.at(i).length);
            cursor = comparison.previousDifference(cursor.offset);
        }
        QVERIFY(!cursor.isValid());
        QVERIFY(!comparison.differs(-1));
        QVERIFY(!comparison.differs(mask.size()));
        QCOMPARE(comparison.read(true, 0, 1024), left);
        QCOMPARE(comparison.read(false, 0, 1024), right);
    }

    void randomizedNavigationMatchesByteOracle()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QRandomGenerator random(0x584548U);
        // Vary the final bitmap byte and seek from both equal bytes and inside blocks.
        for (int trial = 0; trial < 48; ++trial) {
            const int length = trial * 11 + 1;
            QByteArray left(length, '\0'), right(length + trial % 7, '\0');
            for (int i = 0; i < length; ++i) left[i] = right[i] = char(random.generate());
            for (int i = 0; i < right.size(); ++i)
                if (random.bounded(4) == 0) right[i] = char(uchar(right.at(i)) ^ 0xff);
            QByteArray mask(right.size(), '0');
            for (int i = 0; i < mask.size(); ++i)
                if (i >= left.size() || left.at(i) != right.at(i)) mask[i] = '1';
            const auto regions = expectedRegions(mask);
            writeFile(directory.filePath("left.bin"), left);
            writeFile(directory.filePath("right.bin"), right);
            Comparison comparison;
            QVERIFY(comparison.load(directory.filePath("left.bin"), directory.filePath("right.bin")));
            QCOMPARE(comparison.differentBytes(), qint64(mask.count('1')));
            QCOMPARE(comparison.differenceRegions(), qint64(regions.size()));
            for (int offset = -1; offset <= mask.size(); ++offset) {
                Difference expectedNext, expectedPrevious;
                for (const auto &region : regions) {
                    if (region.offset > offset) { expectedNext = region; break; }
                }
                for (const auto &region : regions)
                    if (region.offset + region.length <= offset) expectedPrevious = region;
                const Difference next = comparison.nextDifference(offset);
                const Difference previous = comparison.previousDifference(offset);
                QCOMPARE(next.offset, expectedNext.offset);
                QCOMPARE(next.length, expectedNext.length);
                QCOMPARE(previous.offset, expectedPrevious.offset);
                QCOMPARE(previous.length, expectedPrevious.length);
            }
        }
    }

    void continuousDifferenceAcrossScanBoundary()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const int boundary = Comparison::MaximumReadBytes;
        QByteArray left(boundary + 64, 'a'), right = left;
        right[0] = 'b';
        for (int offset = boundary - 9; offset < boundary + 11; ++offset) right[offset] = 'b';
        right[right.size() - 2] = right[right.size() - 1] = 'b';
        writeFile(directory.filePath("left.bin"), left);
        writeFile(directory.filePath("right.bin"), right);
        Comparison comparison;
        QVERIFY(comparison.load(directory.filePath("left.bin"), directory.filePath("right.bin")));
        QCOMPARE(comparison.differentBytes(), qint64(23));
        QCOMPARE(comparison.differenceRegions(), qint64(3));
        const Difference middle = comparison.nextDifference(0);
        QCOMPARE(middle.offset, qint64(boundary - 9));
        QCOMPARE(middle.length, qint64(20));
        QCOMPARE(comparison.differenceAt(boundary + 7).offset, middle.offset);
        QCOMPARE(comparison.previousDifference(left.size() - 1).offset, middle.offset);
        QCOMPARE(comparison.read(false, boundary - 9, 20), QByteArray(20, 'b'));
    }

    void navigationDoesNotWrapAtIntegerLimits()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath("left.bin"), "abcde");
        writeFile(directory.filePath("right.bin"), "AbcdE");
        Comparison comparison;
        QVERIFY(comparison.load(directory.filePath("left.bin"), directory.filePath("right.bin")));
        QVERIFY(!comparison.nextDifference(4).isValid());
        QVERIFY(!comparison.nextDifference(5).isValid());
        QVERIFY(!comparison.previousDifference(0).isValid());
        QVERIFY(!comparison.previousDifference(-1).isValid());
        QVERIFY(!comparison.nextDifference(std::numeric_limits<qint64>::max()).isValid());
        QVERIFY(!comparison.previousDifference(std::numeric_limits<qint64>::min()).isValid());
    }

    void pageRequestsAreBounded()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath("left.bin"), "abcd");
        writeFile(directory.filePath("right.bin"), "ab");
        Comparison comparison;
        QVERIFY(comparison.load(directory.filePath("left.bin"), directory.filePath("right.bin")));
        QString error = QStringLiteral("stale error");
        QCOMPARE(comparison.read(true, 2, 100, &error), QByteArray("cd"));
        QVERIFY(error.isEmpty());
        QVERIFY(comparison.read(true, 4, 100, &error).isEmpty());
        QVERIFY(error.isEmpty());
        QVERIFY(comparison.read(true, 0, 0, &error).isEmpty());
        QVERIFY(error.isEmpty());
        QVERIFY(comparison.read(true, -1, 1, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(comparison.read(false, 3, 1, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(comparison.read(true, 0, -1, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(comparison.read(true, 0, Comparison::MaximumReadBytes + 1, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(comparison.read(false, 0, 2, &error), QByteArray("ab"));
        QVERIFY(error.isEmpty());
    }

    void hundredMiBRemainsNavigable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const qint64 bytes = 100LL * 1024 * 1024;
        const QString left = directory.filePath("left-100MiB.bin");
        const QString right = directory.filePath("right-100MiB.bin");
        createSparseFile(left, bytes);
        createSparseFile(right, bytes);
        for (qint64 offset : {qint64(0), bytes / 2, bytes - 1}) patchByte(right, offset, char(0xff));
        LqCompare::HexCompareSession session(left, right);
        const Comparison &comparison = session.comparison();
        QElapsedTimer timer;
        timer.start();
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        const qint64 loadMilliseconds = timer.elapsed();
        QCOMPARE(comparison.extent(), bytes);
        QCOMPARE(comparison.bitmapBytes(), bytes / 8);
        QVERIFY(comparison.bitmapBytes() <= 125LL * 1024 * 1024 / 10);
        QCOMPARE(comparison.differenceRegions(), qint64(3));
        QCOMPARE(comparison.differentBytes(), qint64(3));
        timer.restart();
        QCOMPARE(comparison.firstDifference().offset, qint64(0));
        QCOMPARE(comparison.nextDifference(0).offset, bytes / 2);
        QCOMPARE(comparison.nextDifference(bytes / 2).offset, bytes - 1);
        QCOMPARE(comparison.previousDifference(bytes - 1).offset, bytes / 2);
        QCOMPARE(comparison.previousDifference(bytes / 2).offset, qint64(0));
        QCOMPARE(comparison.lastDifference().offset, bytes - 1);
        const qint64 navigationMilliseconds = timer.elapsed();
        QVERIFY2(navigationMilliseconds < 5000, "Sparse 100 MiB navigation exceeded five seconds.");
        QByteArray expected(16, '\0');
        expected[8] = char(0xff);
        QCOMPARE(comparison.read(false, bytes / 2 - 8, expected.size(), &error), expected);
        QCOMPARE(comparison.read(true, bytes - Comparison::MaximumReadBytes, Comparison::MaximumReadBytes, &error), QByteArray(Comparison::MaximumReadBytes, '\0'));
        QVERIFY(comparison.read(true, 0, Comparison::MaximumReadBytes + 1, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QWidget container;
        QWidget *view = session.createWidget(&container);
        QVERIFY(view);
        view->resize(1460, 790);
        container.resize(view->size());
        container.show();
        view->show();
        QVERIFY(session.jumpToOffset(bytes - 1));
        QCoreApplication::processEvents();
        auto *paneLeft = view->findChild<QAbstractScrollArea *>(QStringLiteral("hexLeftPane"));
        auto *paneRight = view->findChild<QAbstractScrollArea *>(QStringLiteral("hexRightPane"));
        QVERIFY(paneLeft && paneRight);
        QCOMPARE(session.currentOffset(), bytes - 1);
        QVERIFY(paneLeft->verticalScrollBar()->value() > 6000000);
        QCOMPARE(paneRight->verticalScrollBar()->value(), paneLeft->verticalScrollBar()->value());
        QVERIFY(!view->grab().isNull());
        qInfo("100 MiB comparison: load %lld ms; four sparse navigation traversals %lld ms; bitmap %lld bytes",
              static_cast<long long>(loadMilliseconds), static_cast<long long>(navigationMilliseconds),
              static_cast<long long>(comparison.bitmapBytes()));
    }

    void oversizedSparseFileRejected()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString huge = directory.filePath("513MiB.bin");
        const QString small = directory.filePath("small.bin");
        createSparseFile(huge, 513LL * 1024 * 1024);
        writeFile(small, "abc");
        Comparison comparison;
        QString error;
        QVERIFY(!comparison.load(huge, small, &error));
        QVERIFY(error.contains(QStringLiteral("512 MiB")));
        QVERIFY(!comparison.isLoaded());
        QCOMPARE(comparison.bitmapBytes(), qint64(0));
        QVERIFY(!comparison.load(small, huge, &error));
        QVERIFY(error.contains(QStringLiteral("512 MiB")));
        QVERIFY(!comparison.isLoaded());
    }

    void snapshotsSurviveSourceChanges()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString left = directory.filePath("left.bin"), right = directory.filePath("right.bin");
        writeFile(left, "left\n");
        writeFile(right, "right\n");
        Comparison comparison;
        QVERIFY(comparison.load(left, right));
        const qint64 differences = comparison.differentBytes();
        writeFile(left, "12345"); // Same-size overwrite must not leak into the snapshot.
        QVERIFY(QFile::remove(right));
        QCOMPARE(comparison.read(true, 0, 100), QByteArray("left\n"));
        QCOMPARE(comparison.read(false, 0, 100), QByteArray("right\n"));
        writeFile(left, "");
        QCOMPARE(comparison.size(true), qint64(5));
        QCOMPARE(comparison.size(false), qint64(6));
        QCOMPARE(comparison.read(true, 0, 100), QByteArray("left\n"));
        QCOMPARE(comparison.differentBytes(), differences);
    }

    void failedLoadPreservesPreviousSnapshot()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString left = directory.filePath("left.bin"), right = directory.filePath("right.bin");
        writeFile(left, "abc");
        writeFile(right, "aBc");
        Comparison comparison;
        QVERIFY(comparison.load(left, right));
        const QString previousPath = comparison.path(true);
        writeFile(left, "replacement");
        QString error;
        const QString missing = directory.filePath("missing.bin");
        const QVector<QPair<QString, QString>> invalid = {
            {left, missing}, {missing, right}, {QString(), right}, {left, directory.path()}
        };
        for (const auto &paths : invalid) {
            QVERIFY(!comparison.load(paths.first, paths.second, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(comparison.isLoaded());
            QCOMPARE(comparison.path(true), previousPath);
            QCOMPARE(comparison.read(true, 0, 100), QByteArray("abc"));
            QCOMPARE(comparison.read(false, 0, 100), QByteArray("aBc"));
            QCOMPARE(comparison.firstDifference().offset, qint64(1));
            QCOMPARE(comparison.differentBytes(), qint64(1));
        }
    }

    void parseOffset_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<qint64>("expected");
        QTest::newRow("zero") << QStringLiteral("0") << true << qint64(0);
        QTest::newRow("decimal") << QStringLiteral("12345") << true << qint64(12345);
        QTest::newRow("decimal-leading-zero") << QStringLiteral("010") << true << qint64(10);
        QTest::newRow("hex") << QStringLiteral("0xFf") << true << qint64(255);
        QTest::newRow("hex-uppercase-prefix") << QStringLiteral("0X10") << true << qint64(16);
        QTest::newRow("trimmed") << QStringLiteral("  0x20 \t") << true << qint64(32);
        QTest::newRow("decimal-max") << QStringLiteral("9223372036854775807") << true << std::numeric_limits<qint64>::max();
        QTest::newRow("hex-max") << QStringLiteral("0x7fffffffffffffff") << true << std::numeric_limits<qint64>::max();
        for (const char *bad : {"", " ", "-1", "+1", "0x-1", "0x", "1.0", "0xgg", "1 0", "FF", "9223372036854775808", "0x8000000000000000", "18446744073709551616"})
            QTest::newRow(bad[0] ? bad : "empty") << QString::fromLatin1(bad) << false << qint64(-47);
        QTest::newRow("unicode-digits") << QString::fromUtf8("１２") << false << qint64(-47);
    }

    void parseOffset()
    {
        QFETCH(QString, text);
        QFETCH(bool, valid);
        QFETCH(qint64, expected);
        qint64 result = -47;
        QString error = QStringLiteral("stale error");
        QCOMPARE(Comparison::parseOffset(text, &result, &error), valid);
        QCOMPARE(result, expected);
        QCOMPARE(error.isEmpty(), valid);
    }

    void parseOffsetRejectsNullOutput()
    {
        QString error;
        QVERIFY(!Comparison::parseOffset(QStringLiteral("42"), nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

    void sessionIsStrictlyReadOnly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString left = directory.filePath("left.bin"), right = directory.filePath("right.bin");
        writeFile(left, "abc");
        writeFile(right, "aBc");
        LqCompare::HexCompareSession session(left, right);
        QCOMPARE(session.typeId(), QStringLiteral("hex"));
        QVERIFY(!session.canSave());
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        QCOMPARE(session.state(), LqCompare::CompareSession::State::Open);
        QVERIFY(!session.isDirty());
        QVERIFY(!session.canSave());
        QVERIFY(!session.save(&error));
        QVERIFY(!error.isEmpty());
        // Even the public base-class dirty flag cannot enable byte writes.
        session.setDirty(true);
        QVERIFY(!session.canSave());
        QVERIFY(!session.save(&error));
        session.setDirty(false);
        QFile source(left);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("abc"));
        QVERIFY(session.statusText().contains(QStringLiteral("Read-only")));
        session.close();
        QVERIFY(!session.comparison().isLoaded());
        QVERIFY(!session.jumpToOffset(qint64(0), &error));
        QVERIFY(!session.setPaths(left, right, &error));
        QVERIFY(!session.open(&error));
    }

    void sessionFailedReplacementKeepsPathsDataAndPosition()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString left = directory.filePath("left.bin"), right = directory.filePath("right.bin");
        writeFile(left, "abcdef");
        writeFile(right, "aBcdeF");
        LqCompare::HexCompareSession session(left, right);
        QVERIFY(session.open());
        QVERIFY(session.jumpToOffset(qint64(4)));
        const QString title = session.title();
        QSignalSpy changed(&session, &LqCompare::HexCompareSession::comparisonChanged);
        QSignalSpy pathsChanged(&session, &LqCompare::HexCompareSession::pathsChanged);
        QString error;
        writeFile(left, "replacement");
        QVERIFY(!session.setPaths(left, directory.filePath("missing.bin"), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(session.state(), LqCompare::CompareSession::State::Open);
        QCOMPARE(session.leftPath(), left);
        QCOMPARE(session.rightPath(), right);
        QCOMPARE(session.title(), title);
        QCOMPARE(session.currentOffset(), qint64(4));
        QCOMPARE(session.comparison().read(true, 0, 100), QByteArray("abcdef"));
        QCOMPARE(session.comparison().read(false, 0, 100), QByteArray("aBcdeF"));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(pathsChanged.count(), 0);
        QVERIFY(QFile::remove(right));
        QVERIFY(!session.reload(&error));
        QCOMPARE(session.currentOffset(), qint64(4));
        QCOMPARE(session.comparison().read(true, 0, 100), QByteArray("abcdef"));
        writeFile(right, "replacement");
        QVERIFY2(session.setPaths(left, right, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(session.currentOffset(), qint64(0));
        QCOMPARE(session.comparison().read(true, 0, 100), QByteArray("replacement"));
        QCOMPARE(session.comparison().differentBytes(), qint64(0));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(pathsChanged.count(), 1);
    }

    void sessionOffsetBoundariesAndDifferenceNavigation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath("left.bin"), "abcdefghij");
        writeFile(directory.filePath("right.bin"), "aBCdefGhIJ");
        LqCompare::HexCompareSession session(directory.filePath("left.bin"), directory.filePath("right.bin"));
        QVERIFY(session.open());
        QString error;
        QVERIFY(session.jumpToOffset(QStringLiteral("0x4"), &error));
        QCOMPARE(session.currentOffset(), qint64(4));
        QSignalSpy offsets(&session, &LqCompare::HexCompareSession::currentOffsetChanged);
        for (qint64 invalid : {qint64(-1), qint64(10), std::numeric_limits<qint64>::max()}) {
            QVERIFY(!session.jumpToOffset(invalid, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(session.currentOffset(), qint64(4));
        }
        QVERIFY(!session.jumpToOffset(QStringLiteral("-1"), &error));
        QCOMPARE(session.currentOffset(), qint64(4));
        QCOMPARE(offsets.count(), 0);
        session.firstDifference(); QCOMPARE(session.currentOffset(), qint64(1));
        session.previousDifference(); QCOMPARE(session.currentOffset(), qint64(1));
        session.nextDifference(); QCOMPARE(session.currentOffset(), qint64(6));
        session.nextDifference(); QCOMPARE(session.currentOffset(), qint64(8));
        session.nextDifference(); QCOMPARE(session.currentOffset(), qint64(8));
        session.previousDifference(); QCOMPARE(session.currentOffset(), qint64(6));
        session.lastDifference(); QCOMPARE(session.currentOffset(), qint64(8));
        session.lastByte(); QCOMPARE(session.currentOffset(), qint64(9));
        session.nextByte(); QCOMPARE(session.currentOffset(), qint64(9));
        session.firstByte(); QCOMPARE(session.currentOffset(), qint64(0));
        session.previousByte(); QCOMPARE(session.currentOffset(), qint64(0));
        session.nextByte(); QCOMPARE(session.currentOffset(), qint64(1));
        QVERIFY(session.jumpToOffset(QStringLiteral("7"), &error));
        QCOMPARE(session.currentOffset(), qint64(7));
        QVERIFY(error.isEmpty());
        QVERIFY(session.statusText().contains(QStringLiteral("0x7 (7)")));
    }

    void emptySessionCanChooseFilesLater()
    {
        LqCompare::HexCompareSession session;
        QVERIFY(session.open());
        QCOMPARE(session.state(), LqCompare::CompareSession::State::Open);
        QVERIFY(!session.comparison().isLoaded());
        QString error;
        QVERIFY(!session.jumpToOffset(qint64(0), &error));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath("empty-left.bin"), {});
        writeFile(directory.filePath("empty-right.bin"), {});
        QVERIFY(session.setPaths(directory.filePath("empty-left.bin"), directory.filePath("empty-right.bin"), &error));
        QVERIFY(session.comparison().isLoaded());
        QVERIFY(!session.jumpToOffset(qint64(0), &error));
        session.firstDifference();
        session.lastDifference();
        session.firstByte();
        session.lastByte();
        QCOMPARE(session.currentOffset(), qint64(0));
    }

    void widgetNavigationSettingsAndRendering()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray left(2048, '\0');
        for (int i = 0; i < left.size(); ++i) left[i] = char(i);
        QByteArray right = left;
        for (int i = 18; i < 24; ++i) right[i] = char(0xff);
        right.replace(64, 8, QByteArray("LQ HEX!!"));
        right.append("tail");
        writeFile(directory.filePath("reference.bin"), left);
        writeFile(directory.filePath("modified.bin"), right);
        LqCompare::HexCompareSession session(directory.filePath("reference.bin"), directory.filePath("modified.bin"));
        QVERIFY(session.open());
        QWidget container;
        QWidget *view = session.createWidget(&container);
        QVERIFY(view);
        QCOMPARE(session.createWidget(&container), view);
        auto *paneLeft = view->findChild<QAbstractScrollArea *>(QStringLiteral("hexLeftPane"));
        auto *paneRight = view->findChild<QAbstractScrollArea *>(QStringLiteral("hexRightPane"));
        auto *rowWidth = view->findChild<QComboBox *>(QStringLiteral("hexBytesPerRow"));
        auto *offset = view->findChild<QLineEdit *>(QStringLiteral("hexOffsetInput"));
        auto *address = view->findChild<QLabel *>(QStringLiteral("hexAddress"));
        auto *error = view->findChild<QLabel *>(QStringLiteral("hexError"));
        QVERIFY(paneLeft && paneRight && rowWidth && offset && address && error);
        view->resize(1460, 790);
        container.resize(view->size());
        container.show();
        view->show();
        QCoreApplication::processEvents();
        QTest::keyClick(paneLeft, Qt::Key_Right);
        QCOMPARE(session.currentOffset(), qint64(1));
        QTest::keyClick(paneLeft, Qt::Key_Down);
        QCOMPARE(session.currentOffset(), qint64(17));
        QTest::keyClick(paneRight, Qt::Key_End);
        QCOMPARE(session.currentOffset(), qint64(31));
        offset->setText(QStringLiteral("0x30"));
        QTest::keyClick(offset, Qt::Key_Return);
        QCOMPARE(session.currentOffset(), qint64(48));
        QCOMPARE(address->text(), QStringLiteral("0x30 = 48 bytes"));
        offset->setText(QStringLiteral("99999999"));
        QTest::keyClick(offset, Qt::Key_Return);
        QCOMPARE(session.currentOffset(), qint64(48));
        QVERIFY(error->isVisible());
        QVERIFY(!error->text().isEmpty());
        for (int width : {8, 16, 32, 64}) {
            rowWidth->setCurrentIndex(rowWidth->findData(width));
            QCOMPARE(session.bytesPerRow(), width);
            QCOMPARE(session.sessionSettings()->value(QStringLiteral("hex.bytesPerRow")).toInt(), width);
        }
        session.setBytesPerRow(7);
        QCOMPARE(session.bytesPerRow(), 64);
        view->resize(1000, 790);
        container.resize(view->size());
        QCoreApplication::processEvents();
        QVERIFY(session.jumpToOffset(qint64(63)));
        QVERIFY(paneLeft->horizontalScrollBar()->value() > 0);
        QVERIFY(paneRight->horizontalScrollBar()->value() > 0);
        view->resize(1460, 790);
        container.resize(view->size());
        session.sessionSettings()->setValue(QStringLiteral("hex.bytesPerRow"), 16);
        QCOMPARE(session.bytesPerRow(), 16);
        QCOMPARE(rowWidth->currentData().toInt(), 16);
        QVERIFY(paneLeft->verticalScrollBar()->maximum() > 0);
        paneLeft->verticalScrollBar()->setValue(20);
        QCOMPARE(paneRight->verticalScrollBar()->value(), 20);
        paneRight->verticalScrollBar()->setValue(5);
        QCOMPARE(paneLeft->verticalScrollBar()->value(), 5);
        session.lastByte();
        QVERIFY(paneLeft->verticalScrollBar()->value() > 5);
        QCOMPARE(paneRight->verticalScrollBar()->value(), paneLeft->verticalScrollBar()->value());
        offset->setText(QStringLiteral("0x12"));
        QTest::keyClick(offset, Qt::Key_Return);
        QVERIFY(!error->isVisible());
        QCOMPARE(session.currentOffset(), qint64(18));
        // Capture the complete offset/hex/ASCII columns for visual review.
        view->resize(1760, 850);
        container.resize(view->size());
        paneLeft->verticalScrollBar()->setValue(0);
        paneLeft->horizontalScrollBar()->setValue(0);
        paneRight->horizontalScrollBar()->setValue(0);
        QCoreApplication::processEvents();
        const QPixmap rendered = view->grab();
        QVERIFY(!rendered.isNull());
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_HEX_SCREENSHOT",
            QDir(QDir::tempPath()).filePath(QStringLiteral("lqcompare-special-hex.png")));
        QVERIFY2(rendered.save(screenshot), qPrintable(screenshot));
        qInfo("HEX screenshot: %s", qPrintable(screenshot));
        QVERIFY(!session.isDirty());
        QVERIFY(!session.canSave());
    }

    void viewSurvivesSessionDestruction()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath("left.bin"), QByteArray(128, 'a'));
        writeFile(directory.filePath("right.bin"), QByteArray(128, 'b'));
        QWidget independentOwner;
        auto *session = new LqCompare::HexCompareSession(directory.filePath("left.bin"), directory.filePath("right.bin"));
        QVERIFY(session->open());
        QWidget *view = session->createWidget(&independentOwner);
        QVERIFY(view);
        delete view;
        QVERIFY(!session->widget());
        view = session->createWidget(&independentOwner);
        QVERIFY(view);
        view->resize(1460, 790);
        independentOwner.resize(view->size());
        independentOwner.show();
        view->show();
        QCoreApplication::processEvents();
        delete session;
        QVERIFY(!view->isEnabled());
        auto *width = view->findChild<QComboBox *>(QStringLiteral("hexBytesPerRow"));
        auto *offset = view->findChild<QLineEdit *>(QStringLiteral("hexOffsetInput"));
        auto *pane = view->findChild<QAbstractScrollArea *>(QStringLiteral("hexLeftPane"));
        QVERIFY(width && offset && pane);
        // Programmatic signals can still fire on disabled children; receivers
        // must not retain the deleted session through captured raw pointers.
        width->setCurrentIndex(width->findData(64));
        offset->setText(QStringLiteral("0x10"));
        QVERIFY(QMetaObject::invokeMethod(offset, "returnPressed", Qt::DirectConnection));
        view->resize(1200, 600);
        view->show();
        pane->viewport()->update();
        QCoreApplication::processEvents();
        QVERIFY(!view->grab().isNull());
        delete view;
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(SpecialHexTests)
#include "tst_specialhex.moc"
