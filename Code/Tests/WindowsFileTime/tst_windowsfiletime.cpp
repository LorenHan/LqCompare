#include "windowsfiletime.h"

#include <limits>
#include <QtTest>

using namespace LqCompare::Files;

class TstWindowsFileTime : public QObject
{
    Q_OBJECT

private slots:
    void goldenUtcValues_data();
    void goldenUtcValues();
    void rejectsOutOfRangeTicks_data();
    void rejectsOutOfRangeTicks();
    void floorsSubTickPrecision_data();
    void floorsSubTickPrecision();
    void rejectsNullOutput();
    void lowestNanosecondsNeverWrapOrClamp();
    void highestNanosecondsNeverWrapOrClamp();
    void tickRoundTripsAcrossTheRange();
};

void TstWindowsFileTime::goldenUtcValues_data()
{
    QTest::addColumn<quint64>("ticks");
    QTest::addColumn<qint64>("nanoseconds");

    // 固定黄金值，不用被测函数计算期望值。纪元与单位来自 FILETIME 文档：
    // https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime
    QTest::newRow("unix-epoch") << quint64(116444736000000000ULL) << qint64(0);
    QTest::newRow("1900-01-01-utc")
        << quint64(94354848000000000ULL) << qint64(-2208988800000000000LL);
    QTest::newRow("2000-01-01-utc")
        << quint64(125911584000000000ULL) << qint64(946684800000000000LL);
    QTest::newRow("2026-10-02-utc")
        << quint64(134353728000000000ULL) << qint64(1790899200000000000LL);
    // Microsoft KB 555936 的独立样例：2007-06-24 05:57:54.2968750 UTC。
    // https://learn.microsoft.com/en-us/troubleshoot/windows-server/active-directory/convert-datetime-attributes-to-standard-format
    QTest::newRow("microsoft-kb555936")
        << quint64(128271382742968750ULL) << qint64(1182664674296875000LL);
    QTest::newRow("one-tick-before-unix-epoch")
        << quint64(116444735999999999ULL) << qint64(-100);
    QTest::newRow("one-tick-after-unix-epoch")
        << quint64(116444736000000001ULL) << qint64(100);
    QTest::newRow("recent-sub-millisecond")
        << quint64(134353728001234567ULL) << qint64(1790899200123456700LL);
    QTest::newRow("lowest-representable-tick")
        << quint64(24211015631452242ULL) << qint64(-9223372036854775800LL);
    QTest::newRow("highest-representable-tick")
        << quint64(208678456368547758ULL) << qint64(9223372036854775800LL);
}

void TstWindowsFileTime::goldenUtcValues()
{
    QFETCH(quint64, ticks);
    QFETCH(qint64, nanoseconds);
    qint64 decoded = 42;
    QVERIFY(WindowsFileTime::fromTicks(ticks, &decoded));
    QCOMPARE(decoded, nanoseconds);
    quint64 encoded = 42;
    QVERIFY(WindowsFileTime::toTicks(nanoseconds, &encoded));
    QCOMPARE(encoded, ticks);
}

void TstWindowsFileTime::rejectsOutOfRangeTicks_data()
{
    QTest::addColumn<quint64>("ticks");
    // 1601 纪元本身早于内部 qint64 纳秒范围，必须无效，不能钳到最早日期。
    QTest::newRow("windows-epoch-1601") << quint64(0);
    QTest::newRow("windows-epoch-plus-one-tick") << quint64(1);
    QTest::newRow("just-before-lower-bound") << quint64(24211015631452241ULL);
    QTest::newRow("just-after-upper-bound") << quint64(208678456368547759ULL);
    QTest::newRow("signed-maximum") << quint64(0x7fffffffffffffffULL);
    QTest::newRow("high-bit-set") << quint64(0x8000000000000000ULL);
    QTest::newRow("unsigned-maximum-special-value") << quint64(0xffffffffffffffffULL);
}

void TstWindowsFileTime::rejectsOutOfRangeTicks()
{
    QFETCH(quint64, ticks);
    qint64 decoded = 42;
    QVERIFY(!WindowsFileTime::fromTicks(ticks, &decoded));
    QCOMPARE(decoded, qint64(42));
}

void TstWindowsFileTime::floorsSubTickPrecision_data()
{
    QTest::addColumn<qint64>("input");
    QTest::addColumn<quint64>("ticks");
    QTest::addColumn<qint64>("rounded");
    QTest::newRow("positive-1") << qint64(1) << quint64(116444736000000000ULL) << qint64(0);
    QTest::newRow("positive-99") << qint64(99) << quint64(116444736000000000ULL) << qint64(0);
    QTest::newRow("positive-101") << qint64(101) << quint64(116444736000000001ULL) << qint64(100);
    QTest::newRow("negative-1") << qint64(-1) << quint64(116444735999999999ULL) << qint64(-100);
    QTest::newRow("negative-99") << qint64(-99) << quint64(116444735999999999ULL) << qint64(-100);
    QTest::newRow("negative-101") << qint64(-101) << quint64(116444735999999998ULL) << qint64(-200);
    QTest::newRow("negative-second-boundary")
        << qint64(-1000000001) << quint64(116444735989999999ULL) << qint64(-1000000100);
    QTest::newRow("recent") << qint64(1790899200123456789LL)
        << quint64(134353728001234567ULL) << qint64(1790899200123456700LL);
}

void TstWindowsFileTime::floorsSubTickPrecision()
{
    QFETCH(qint64, input);
    QFETCH(quint64, ticks);
    QFETCH(qint64, rounded);
    quint64 encoded = 42;
    QVERIFY(WindowsFileTime::toTicks(input, &encoded));
    QCOMPARE(encoded, ticks);
    qint64 decoded = 42;
    QVERIFY(WindowsFileTime::fromTicks(encoded, &decoded));
    QCOMPARE(decoded, rounded);
}

void TstWindowsFileTime::rejectsNullOutput()
{
    QVERIFY(!WindowsFileTime::fromTicks(116444736000000000ULL, nullptr));
    QVERIFY(!WindowsFileTime::toTicks(0, nullptr));
}

void TstWindowsFileTime::lowestNanosecondsNeverWrapOrClamp()
{
    const qint64 minimum = std::numeric_limits<qint64>::min();
    for (int offset = 0; offset < 208; ++offset) {
        quint64 encoded = 42;
        const bool ok = WindowsFileTime::toTicks(minimum + offset, &encoded);
        QCOMPARE(ok, offset >= 8);
        if (!ok) {
            QCOMPARE(encoded, quint64(42));
            continue;
        }
        QCOMPARE(encoded, quint64(24211015631452242ULL) + quint64((offset - 8) / 100));
        qint64 decoded = 42;
        QVERIFY(WindowsFileTime::fromTicks(encoded, &decoded));
        QCOMPARE(decoded, qint64(-9223372036854775800LL) + ((offset - 8) / 100) * qint64(100));
    }
}

void TstWindowsFileTime::highestNanosecondsNeverWrapOrClamp()
{
    const qint64 maximum = std::numeric_limits<qint64>::max();
    for (int offset = 0; offset < 208; ++offset) {
        quint64 encoded = 42;
        QVERIFY(WindowsFileTime::toTicks(maximum - offset, &encoded));
        qint64 decoded = 42;
        QVERIFY(WindowsFileTime::fromTicks(encoded, &decoded));
        QCOMPARE(decoded, ((maximum - offset) / 100) * 100);
    }
}

void TstWindowsFileTime::tickRoundTripsAcrossTheRange()
{
    const quint64 minimum = 24211015631452242ULL;
    const quint64 maximum = 208678456368547758ULL;
    const quint64 step = (maximum - minimum) / 10000;
    for (quint64 input = minimum; input <= maximum; input += step) {
        qint64 decoded = 42;
        QVERIFY(WindowsFileTime::fromTicks(input, &decoded));
        quint64 encoded = 42;
        QVERIFY(WindowsFileTime::toTicks(decoded, &encoded));
        QCOMPARE(encoded, input);
    }
}

QTEST_APPLESS_MAIN(TstWindowsFileTime)
#include "tst_windowsfiletime.moc"
