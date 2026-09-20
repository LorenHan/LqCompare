#include "tst_attributefilter.h"

#include "attributefilter.h"
#include "filterstack.h"
#include "mask.h"
#include "maskfilter.h"

#include <QFile>

using namespace LqCompare;
using namespace LqCompare::Filter;

namespace {

const quint64 kKilo = 1024ULL;
const quint64 kMega = 1024ULL * 1024ULL;
const quint64 kGiga = 1024ULL * 1024ULL * 1024ULL;
const quint64 kTera = 1024ULL * 1024ULL * 1024ULL * 1024ULL;

QDateTime moment(int year, int month, int day, int hour = 0, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute));
}

/// 只带名字的条目（没有大小/时间/属性/所有者）。
EntryMetadata named(const QString &name)
{
    return EntryMetadata::forName(name);
}

/// 源码级护栏用的禁用词：属性过滤一旦去读条目内容，这些名字必然出现。
/// 挑的都是**具体的 API / 数据名**而不是「content」这类词——后者在注释里
/// 完全可能正常出现，用它当护栏会变成一条动不动就红的噪声。
QStringList forbiddenContentTokens()
{
    return QStringList{QStringLiteral("readAll"), QStringLiteral("QFile"),
                       QStringLiteral("QTextStream"), QStringLiteral("QDataStream"),
                       QStringLiteral("readFileContents")};
}

/// 在一段源码里找出所有禁用词。抽成函数是为了让护栏能**反向验证**：
/// 拿一段故意写了 `readAll` 的文本跑同一个判定，必须报出来。
QStringList scanForContentAccess(const QString &source)
{
    QStringList found;
    for (const QString &token : forbiddenContentTokens()) {
        if (source.contains(token))
            found.append(token);
    }
    return found;
}

} // namespace

void TstAttributeFilter::initTestCase()
{
    QVERIFY2(!readSourceFile(QStringLiteral("/Services/Filter/attributefilter.h")).isEmpty(),
             "读不到 attributefilter.h：LQCOMPARE_CODE_ROOT 指向不对？");
    QVERIFY2(!readSourceFile(QStringLiteral("/Services/Filter/attributefilter.cpp")).isEmpty(),
             "读不到 attributefilter.cpp：LQCOMPARE_CODE_ROOT 指向不对？");
}

QString TstAttributeFilter::readSourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

// -----------------------------------------------------------------------------
// A 大小范围与单位（第 1 条）
// -----------------------------------------------------------------------------

void TstAttributeFilter::sizeMinimumRejectsSmallerEntries()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("10 MB"), QString());

    QVERIFY(filter.size.hasMinimum());
    QVERIFY(!filter.size.hasMaximum());
    QVERIFY2(filter.accepts(named(QStringLiteral("a.bin")).withSize(10 * kMega)),
             qPrintable(filter.describe()));
    QVERIFY(!filter.accepts(named(QStringLiteral("a.bin")).withSize(10 * kMega - 1)));
    QVERIFY(filter.accepts(named(QStringLiteral("a.bin")).withSize(500 * kMega)));
}

void TstAttributeFilter::sizeMaximumRejectsLargerEntries()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QString(), QStringLiteral("100 KB"));

    QVERIFY(filter.size.hasMaximum());
    QVERIFY(!filter.size.hasMinimum());
    QVERIFY(filter.accepts(named(QStringLiteral("a.bin")).withSize(100 * kKilo)));
    QVERIFY(!filter.accepts(named(QStringLiteral("a.bin")).withSize(100 * kKilo + 1)));
    QVERIFY(filter.accepts(named(QStringLiteral("a.bin")).withSize(1)));
}

void TstAttributeFilter::sizeRangeKeepsBothEndpoints()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("1 KB"), QStringLiteral("2 KB"));

    // 两个端点都含：「1 KB ～ 2 KB」若写成开区间，正好落在端点上的文件会
    // 莫名其妙地被过滤掉，而用户填的就是这两个数字。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(1 * kKilo)));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(2 * kKilo)));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(1536)));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withSize(1 * kKilo - 1)));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withSize(2 * kKilo + 1)));
}

void TstAttributeFilter::sizeUnitsAreBinary()
{
    // 单位按 1024 换算，与 Windows 资源管理器一致（理由见 attributefilter.h）。
    // 写成 1000 的话下面每一条都会红——这正是这条用例要钉住的。
    QCOMPARE(parseSizeText(QStringLiteral("1 KB")).bytes, kKilo);
    QCOMPARE(parseSizeText(QStringLiteral("1 kb")).bytes, kKilo);
    QCOMPARE(parseSizeText(QStringLiteral("1 MB")).bytes, kMega);
    QCOMPARE(parseSizeText(QStringLiteral("1 GB")).bytes, kGiga);
    QCOMPARE(parseSizeText(QStringLiteral("1 TB")).bytes, kTera);
    QCOMPARE(parseSizeText(QStringLiteral("1 KiB")).bytes, kKilo);
    QCOMPARE(parseSizeText(QStringLiteral("4 B")).bytes, 4ULL);
    // 单字母缩写也认（`nx` 之类的工具里 `10 k` 很常见），它们走的是表里
    // 单独的一行，所以必须各断言一次——只测 `KB` 的话那一行改成 1000 也没人发现。
    QCOMPARE(parseSizeText(QStringLiteral("1 k")).bytes, kKilo);
    QCOMPARE(parseSizeText(QStringLiteral("1 m")).bytes, kMega);
    QCOMPARE(parseSizeText(QStringLiteral("1 g")).bytes, kGiga);
    QCOMPARE(parseSizeText(QStringLiteral("1 t")).bytes, kTera);

    // 端到端：下限 1 KB 时 1023 字节必须被挡（按 1000 算的实现会放它过去）。
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("1 KB"), QString());
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withSize(1023)));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(1024)));
}

void TstAttributeFilter::sizeAcceptsFractionalValues()
{
    QCOMPARE(parseSizeText(QStringLiteral("1.5 MB")).bytes, 1536 * kKilo);
    QCOMPARE(parseSizeText(QStringLiteral("0.5 KB")).bytes, 512ULL);
    QCOMPARE(parseSizeText(QStringLiteral("2.25 GB")).bytes, 2304 * kMega);
    QCOMPARE(parseSizeText(QStringLiteral("1.5MB")).bytes, 1536 * kKilo);
    QCOMPARE(parseSizeText(QStringLiteral("  10  mb  ")).bytes, 10 * kMega);
}

void TstAttributeFilter::sizeBareNumberMeansBytes()
{
    QCOMPARE(parseSizeText(QStringLiteral("4096")).bytes, 4096ULL);
    QCOMPARE(parseSizeText(QStringLiteral("0")).bytes, 0ULL);
    QVERIFY(parseSizeText(QStringLiteral("4096")).ok);
}

void TstAttributeFilter::sizeMaximumZeroKeepsOnlyEmptyFiles()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QString(), QStringLiteral("0"));

    // 「上限 0 字节」是一个真实需求（只看空文件）。把它当成「没填」会让这条
    // 过滤静默失效，所以 hasMaximum 必须为真。
    QVERIFY(filter.size.hasMaximum());
    QVERIFY(filter.size.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("empty.txt")).withSize(0)));
    QVERIFY(!filter.accepts(named(QStringLiteral("one-byte.txt")).withSize(1)));
}

void TstAttributeFilter::sizeRejectsNegativeText()
{
    const SizeParseResult parsed = parseSizeText(QStringLiteral("-5 MB"));
    QVERIFY(!parsed.ok);
    QVERIFY2(parsed.problem.contains(QStringLiteral("负数")), qPrintable(parsed.problem));

    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("-5 MB"), QString());
    QCOMPARE(filter.size.problems().size(), 1);
    QCOMPARE(QString::fromLatin1(conditionFieldIdentifier(filter.size.problems().first().field)),
             QStringLiteral("size-min"));
    // 填错了 → 不参与收窄（否则「勾一下就什么都看不见」，用户只会以为程序坏了）。
    QVERIFY(!filter.size.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(1)));
}

void TstAttributeFilter::sizeRejectsUnknownUnit()
{
    const SizeParseResult unit = parseSizeText(QStringLiteral("10 XB"));
    QVERIFY(!unit.ok);
    QVERIFY2(unit.hint.contains(QStringLiteral("KB")), qPrintable(unit.hint));

    const SizeParseResult word = parseSizeText(QStringLiteral("一亿"));
    QVERIFY(!word.ok);
    QVERIFY(!word.hint.isEmpty());

    QVERIFY(!parseSizeText(QStringLiteral("   ")).ok);
}

void TstAttributeFilter::sizeRejectsOverflow()
{
    // 溢出必须在乘之前判：乘完再转 quint64 是未定义行为，而现象是
    // 「填了一个很大的数，过滤结果毫无规律」。
    const SizeParseResult parsed = parseSizeText(QStringLiteral("99999999 TB"));
    QVERIFY(!parsed.ok);
    QVERIFY2(parsed.problem.contains(QStringLiteral("超出")), qPrintable(parsed.problem));
}

void TstAttributeFilter::sizeInvertedRangeDoesNotApplyAndIsReported()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("100 MB"), QStringLiteral("10 MB"));

    QCOMPARE(filter.size.problems().size(), 1);
    QVERIFY2(filter.size.problems().first().message.contains(QStringLiteral("大于")),
             qPrintable(filter.size.problems().first().message));
    QVERIFY(filter.size.isConstrained());
    // 自相矛盾 → 两个界一起失效，而不是「永远为空列表」。
    QVERIFY(!filter.size.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(50 * kMega)));

    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")).withSize(50 * kMega));
    QVERIFY(decision.accepted);
    QVERIFY(decision.hasProblems());
    QVERIFY(decision.blocking.isEmpty());
}

void TstAttributeFilter::sizePartialRangeStillAppliesTheGoodBound()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    // 上限写了一个认不出的单位：下限必须照常生效（「一行写错只丢那一行」）。
    filter.size.setRangeText(QStringLiteral("10 MB"), QStringLiteral("一亿"));

    QCOMPARE(filter.size.problems().size(), 1);
    QVERIFY(filter.size.hasMinimum());
    QVERIFY(!filter.size.hasMaximum());
    QVERIFY(filter.size.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(20 * kMega)));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withSize(5 * kMega)));
}

void TstAttributeFilter::sizeIsUndecidedWhenSizeIsUnknown()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("10 MB"), QString());

    const EntryMetadata entry = named(QStringLiteral("a.bin"));
    QVERIFY(!entry.hasSize);

    const AttributeDecision decision = filter.decide(entry);
    const ConditionOutcome *outcome = decision.outcomeFor(AttributeConditionKind::Size);
    QVERIFY(outcome);
    QVERIFY2(outcome->undecided(), qPrintable(outcome->describe()));
    QVERIFY2(outcome->reason.contains(QStringLiteral("未知")), qPrintable(outcome->reason));
    // 判不出来 → 放行，并且**如实列进 undecided**（界面要显示「这一项没生效」）。
    QVERIFY(decision.accepted);
    QCOMPARE(decision.undecided.size(), 1);
}

void TstAttributeFilter::sizeDoesNothingWhileDisabled()
{
    AttributeFilter filter;
    filter.size.setEnabled(false);
    filter.size.setRangeText(QStringLiteral("10 MB"), QString());

    QVERIFY(!filter.size.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withSize(1)));
    QVERIFY(filter.decide(named(QStringLiteral("a")).withSize(1)).undecided.isEmpty());
}

void TstAttributeFilter::sizeFormattingIsReadableAndExact()
{
    QCOMPARE(formatSizeText(0), QStringLiteral("0 字节"));
    QCOMPARE(formatSizeText(1), QStringLiteral("1 字节"));
    QCOMPARE(formatSizeText(1023), QStringLiteral("1023 字节"));
    QCOMPARE(formatSizeText(kKilo), QStringLiteral("1 KB"));
    QCOMPARE(formatSizeText(1536), QStringLiteral("1.5 KB"));
    QCOMPARE(formatSizeText(10 * kMega), QStringLiteral("10 MB"));
    QCOMPARE(formatSizeText(kGiga), QStringLiteral("1 GB"));
    QCOMPARE(formatSizeText(kTera), QStringLiteral("1 TB"));
    QCOMPARE(formatSizeExactText(1536), QStringLiteral("1.5 KB（1536 字节）"));

    // 往返：显示出来的东西能被解析回同一个字节数（取可精确表示的取值）。
    const QVector<quint64> samples = {0ULL, 1ULL, 1023ULL, kKilo, 1536ULL, 10 * kMega, kGiga};
    for (quint64 bytes : samples) {
        const SizeParseResult parsed = parseSizeText(formatSizeText(bytes));
        QVERIFY2(parsed.ok, qPrintable(formatSizeText(bytes)));
        QCOMPARE(parsed.bytes, bytes);
    }
}

// -----------------------------------------------------------------------------
// B 修改时间范围（第 2 条）
// -----------------------------------------------------------------------------

void TstAttributeFilter::timeAbsoluteRangeKeepsBothEndpoints()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-01"), QStringLiteral("2026-09-20"));

    QVERIFY(filter.timeRange.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 1))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 20))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 10, 12, 30))));
    // 只填日期时，结束日当天整日都在区间内（上限补到 23:59:59）。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 20, 23, 59))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 21))));
}

void TstAttributeFilter::timeAbsoluteRangeRejectsOutside()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-01 08:00"), QStringLiteral("2026-09-20 18:00"));

    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 1, 8, 0))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 20, 18, 0))));

    const AttributeDecision early =
        filter.decide(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 1, 7, 59)));
    QVERIFY2(!early.accepted, qPrintable(early.describe()));
    QCOMPARE(QString::fromLatin1(attributeConditionIdentifier(early.deciding)), QStringLiteral("time"));
    QVERIFY2(early.reason.contains(QStringLiteral("早于")), qPrintable(early.reason));

    const AttributeDecision late =
        filter.decide(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 20, 18, 1)));
    QVERIFY(!late.accepted);
    QVERIFY2(late.reason.contains(QStringLiteral("晚于")), qPrintable(late.reason));
}

void TstAttributeFilter::timeAbsoluteLowerBoundOnly()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-10"), QString());

    QVERIFY(filter.timeRange.hasLowerBound());
    QVERIFY(!filter.timeRange.hasUpperBound());
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 9, 23, 59))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 10))));
    // 绝对区间没有「现在」的概念，只填下限时未来的时间戳照样在区间内。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2030, 1, 1))));
}

void TstAttributeFilter::timeAbsoluteUpperBoundOnly()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setAbsoluteText(QString(), QStringLiteral("2026-09-10"));

    QVERIFY(!filter.timeRange.hasLowerBound());
    QVERIFY(filter.timeRange.hasUpperBound());
    // 只填日期的上限 = 含当天一整天（09-10 23:59:59）。取当天零点的话，
    // 09-10 当天修改过的文件一个都不会出现，而用户填的日期看起来完全正确。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 10, 0, 1))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 10, 23, 59))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 11))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(1999, 1, 1))));

    // 带上钟点则按那一刻精确比较（不再补到当天末尾）。
    filter.timeRange.setAbsoluteText(QString(), QStringLiteral("2026-09-10 08:00"));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 10, 7, 59))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 10, 8, 1))));
}

void TstAttributeFilter::timeRelativeWindowIsRolling()
{
    const QDateTime now = moment(2026, 9, 20, 19, 0);

    AttributeFilter filter;
    filter.setReferenceTime(now);
    filter.timeRange.setEnabled(true);
    filter.timeRange.setKind(TimeRangeKind::Relative);
    filter.timeRange.setRelativeDays(7);

    QVERIFY(filter.timeRange.isActive());

    // 滚动窗口 = [now - 7×24h, now]。若实现成「最近 7 个自然日」，
    // 起点会落在 00:00，下面正落在 now-7d（09-13 19:00）的那条就会红。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(now)));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(now.addDays(-7))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(now.addDays(-7).addSecs(1))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(now.addDays(-7).addSecs(-1))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(now.addDays(-1))));
}

void TstAttributeFilter::timeRelativeWindowExcludesFutureTimestamps()
{
    const QDateTime now = moment(2026, 9, 20, 19, 0);

    AttributeFilter filter;
    filter.setReferenceTime(now);
    filter.timeRange.setEnabled(true);
    filter.timeRange.setKind(TimeRangeKind::Relative);
    filter.timeRange.setRelativeDays(7);

    // 「最近 7 天」= 过去 7 天。时间戳在未来的条目（时钟不准、解压出来的归档）
    // 不在窗口内——把 2030 年的文件算进「最近」更难解释。
    const AttributeDecision future =
        filter.decide(named(QStringLiteral("a")).withLastModified(now.addSecs(60)));
    QVERIFY2(!future.accepted, qPrintable(future.describe()));
    QVERIFY2(future.reason.contains(QStringLiteral("未来")), qPrintable(future.reason));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2030, 1, 1))));
}

void TstAttributeFilter::timeRelativeWithoutReferenceTimeIsUndecided()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setKind(TimeRangeKind::Relative);
    filter.timeRange.setRelativeText(QStringLiteral("7 天"));

    // 没有参考时刻 → 算不出窗口。这**不是**配置问题（用户没写错），所以
    // problems() 必须为空，条件表现为「未生效」。
    QVERIFY(!filter.timeRange.isActive());
    QVERIFY(filter.timeRange.problems().isEmpty());

    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")).withLastModified(moment(2026, 9, 19)));
    QVERIFY(decision.accepted);
    const ConditionOutcome *outcome = decision.outcomeFor(AttributeConditionKind::TimeRange);
    QVERIFY(outcome && outcome->undecided());
    QVERIFY2(outcome->reason.contains(QStringLiteral("当前时刻")), qPrintable(outcome->reason));

    // 给上一个**与真实系统时钟相差很远**的时刻，结论必须跟着参考时刻走。
    // 实现里若直接调 QDateTime::currentDateTime()，下面第一条会红。
    filter.setReferenceTime(moment(2020, 1, 10, 12, 0));
    QVERIFY(filter.timeRange.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2020, 1, 5))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2020, 1, 1))));
}

void TstAttributeFilter::timeRelativeTextAcceptsCommonForms()
{
    QCOMPARE(parseRelativeDaysText(QStringLiteral("7")).days, 7);
    QCOMPARE(parseRelativeDaysText(QStringLiteral("7天")).days, 7);
    QCOMPARE(parseRelativeDaysText(QStringLiteral("最近 7 天")).days, 7);
    QCOMPARE(parseRelativeDaysText(QStringLiteral("近7日")).days, 7);
    QCOMPARE(parseRelativeDaysText(QStringLiteral("7days")).days, 7);
    QCOMPARE(parseRelativeDaysText(QStringLiteral("7 days")).days, 7);
    QCOMPARE(parseRelativeDaysText(QStringLiteral("30天内")).days, 30);
    QCOMPARE(parseRelativeDaysText(QStringLiteral(" 3 天 ")).days, 3);

    QVERIFY(!parseRelativeDaysText(QStringLiteral("abc")).ok);
    QVERIFY(!parseRelativeDaysText(QStringLiteral("7天半")).ok);
    QVERIFY(!parseRelativeDaysText(QString()).ok);
}

void TstAttributeFilter::timeRelativeRejectsZeroDays()
{
    const RelativeDaysParseResult parsed = parseRelativeDaysText(QStringLiteral("0"));
    QVERIFY(!parsed.ok);
    QVERIFY2(parsed.problem.contains(QStringLiteral("大于 0")), qPrintable(parsed.problem));

    // 文本入口（声明文件）：写 0 是明确的笔误 → 报配置问题。
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setKind(TimeRangeKind::Relative);
    filter.timeRange.setRelativeText(QStringLiteral("0"));
    QCOMPARE(filter.timeRange.problems().size(), 1);
    QVERIFY(!filter.timeRange.isActive());

    // 整数入口（界面上的数字框）：0 的自然含义是「没填」，不该报错。
    // 两个入口的区别是刻意的，所以两条断言写在一起。
    filter.timeRange.setRelativeDays(0);
    QVERIFY(filter.timeRange.problems().isEmpty());
    QVERIFY(filter.timeRange.relativeText().isEmpty());
    QVERIFY(!filter.timeRange.isActive());
}

void TstAttributeFilter::timeInvertedRangeDoesNotApplyAndIsReported()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-20"), QStringLiteral("2026-09-01"));

    QCOMPARE(filter.timeRange.problems().size(), 1);
    QCOMPARE(QString::fromLatin1(conditionFieldIdentifier(filter.timeRange.problems().first().field)),
             QStringLiteral("time-to"));
    QVERIFY(!filter.timeRange.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withLastModified(moment(2026, 5, 1))));
}

void TstAttributeFilter::timeTextAcceptsChineseDateForm()
{
    QCOMPARE(parseDateTimeText(QStringLiteral("2026-09-01")).value, moment(2026, 9, 1));
    QCOMPARE(parseDateTimeText(QStringLiteral("2026-9-1")).value, moment(2026, 9, 1));
    QCOMPARE(parseDateTimeText(QStringLiteral("2026/09/01")).value, moment(2026, 9, 1));
    // 中文写法：本产品的界面是中文的，用户写「2026年9月1日」是自然的。
    QCOMPARE(parseDateTimeText(QStringLiteral("2026年9月1日")).value, moment(2026, 9, 1));
    QCOMPARE(parseDateTimeText(QStringLiteral(" 2026-09-01 ")).value, moment(2026, 9, 1));
    QCOMPARE(parseDateTimeText(QStringLiteral("2026-09-01 18:30")).value, moment(2026, 9, 1, 18, 30));
    QCOMPARE(parseDateTimeText(QStringLiteral("2026-09-01 18:30:45")).value.time(), QTime(18, 30, 45));
}

void TstAttributeFilter::timeTextRejectsGarbage()
{
    const DateTimeParseResult parsed = parseDateTimeText(QStringLiteral("下周三"));
    QVERIFY(!parsed.ok);
    QVERIFY2(parsed.hint.contains(QStringLiteral("2026-09-01")), qPrintable(parsed.hint));

    QVERIFY(!parseDateTimeText(QString()).ok);
    QVERIFY(!parseDateTimeText(QStringLiteral("2026-13-45")).ok);
    QVERIFY(!parseDateTimeText(QStringLiteral("20260901")).ok);
}

void TstAttributeFilter::timeWindowIsDescribedForDisplay()
{
    AttributeFilter filter;
    filter.setReferenceTime(moment(2026, 9, 20, 19, 0));
    filter.timeRange.setEnabled(true);
    filter.timeRange.setKind(TimeRangeKind::Relative);
    filter.timeRange.setRelativeDays(7);

    // 窗口算出来就要能显示：用户不必去猜程序怎么理解「最近 7 天」。
    const QString relative = filter.timeRange.describeWindow();
    QVERIFY2(relative.contains(QStringLiteral("最近 7 天")), qPrintable(relative));
    QVERIFY2(relative.contains(QStringLiteral("2026-09-13 19:00")), qPrintable(relative));
    QVERIFY2(relative.contains(QStringLiteral("2026-09-20 19:00")), qPrintable(relative));

    filter.timeRange.setKind(TimeRangeKind::Absolute);
    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-01"), QStringLiteral("2026-09-20"));
    // 上限那一边补到当天末尾，所以窗口的右端显示成 23:59——这是**刻意的**，
    // 让用户一眼看出「含 09-20 一整天」。
    QCOMPARE(filter.timeRange.describeWindow(), QStringLiteral("2026-09-01 ～ 2026-09-20 23:59"));

    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-01"), QString());
    QCOMPARE(filter.timeRange.describeWindow(), QStringLiteral("不早于 2026-09-01"));

    filter.timeRange.setAbsoluteText(QString(), QStringLiteral("2026-09-20 18:00"));
    QCOMPARE(filter.timeRange.describeWindow(), QStringLiteral("不晚于 2026-09-20 18:00"));

    filter.timeRange.setAbsoluteText(QString(), QString());
    QVERIFY(filter.timeRange.describeWindow().isEmpty());
}

void TstAttributeFilter::timeIsUndecidedWhenLastModifiedIsUnknown()
{
    AttributeFilter filter;
    filter.timeRange.setEnabled(true);
    filter.timeRange.setAbsoluteText(QStringLiteral("2026-09-01"), QString());

    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")));
    const ConditionOutcome *outcome = decision.outcomeFor(AttributeConditionKind::TimeRange);
    QVERIFY(outcome && outcome->undecided());
    QVERIFY2(outcome->reason.contains(QStringLiteral("未知")), qPrintable(outcome->reason));
    QVERIFY(decision.accepted);

    // 「给了但构造失败」的时刻也按「不知道」处理，而不是当成 1970 年。
    const AttributeDecision invalid =
        filter.decide(named(QStringLiteral("a")).withLastModified(QDateTime()));
    QVERIFY(invalid.accepted);
    QVERIFY(invalid.outcomeFor(AttributeConditionKind::TimeRange)->undecided());
}

// -----------------------------------------------------------------------------
// C 属性位（第 3 条前半）
// -----------------------------------------------------------------------------

void TstAttributeFilter::requiredAttributeMustBeSet()
{
    AttributeFilter filter;
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::ReadOnly, Requirement::Required);

    QVERIFY(filter.attributeBits.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withAttribute(EntryAttribute::ReadOnly, true)));

    const AttributeDecision decision =
        filter.decide(named(QStringLiteral("a")).withoutAttribute(EntryAttribute::ReadOnly));
    QVERIFY2(!decision.accepted, qPrintable(decision.describe()));
    QCOMPARE(QString::fromLatin1(attributeConditionIdentifier(decision.deciding)),
             QStringLiteral("attributes"));
    QVERIFY2(decision.reason.contains(QStringLiteral("只读")), qPrintable(decision.reason));
}

void TstAttributeFilter::forbiddenAttributeMustBeUnset()
{
    AttributeFilter filter;
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::Hidden, Requirement::Forbidden);

    // 「别给我看隐藏文件」是最常见的用法之一，所以三态里必须有「必须未置位」。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withoutAttribute(EntryAttribute::Hidden)));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withAttribute(EntryAttribute::Hidden, true)));

    const AttributeDecision decision =
        filter.decide(named(QStringLiteral("a")).withAttribute(EntryAttribute::Hidden, true));
    QVERIFY2(decision.reason.contains(QStringLiteral("未置位")), qPrintable(decision.reason));
}

void TstAttributeFilter::unknownAttributeBitIsUndecidedNotRejected()
{
    AttributeFilter filter;
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::Archive, Requirement::Required);

    // Unix 上根本没有「归档位」这个概念。若把「不知道」当成「不符合」，
    // 用户在 macOS 上勾一下就会得到空列表。
    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")));
    QVERIFY2(decision.accepted, qPrintable(decision.describe()));
    const ConditionOutcome *outcome = decision.outcomeFor(AttributeConditionKind::Attributes);
    QVERIFY(outcome && outcome->undecided());
    QVERIFY2(outcome->reason.contains(QStringLiteral("归档")), qPrintable(outcome->reason));
    QCOMPARE(decision.undecided.size(), 1);
}

void TstAttributeFilter::partiallyKnownAttributeBitsAreUndecided()
{
    AttributeFilter filter;
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::Hidden, Requirement::Required);
    filter.attributeBits.setRequirement(EntryAttribute::System, Requirement::Required);

    // 隐藏位可信且置位、系统位不可信 → 判不了，但也不是拒绝。
    EntryMetadata partial = named(QStringLiteral("a"));
    partial.withAttribute(EntryAttribute::Hidden, true);
    const AttributeDecision decision = filter.decide(partial);
    QVERIFY(decision.accepted);
    QVERIFY(decision.outcomeFor(AttributeConditionKind::Attributes)->undecided());

    // 而「已知的那一位不满足」必须先于「另一位不知道」生效：拒绝优先。
    EntryMetadata wrong = named(QStringLiteral("a"));
    wrong.withoutAttribute(EntryAttribute::Hidden);
    const AttributeDecision rejected = filter.decide(wrong);
    QVERIFY(!rejected.accepted);
    QVERIFY2(rejected.reason.contains(QStringLiteral("隐藏")), qPrintable(rejected.reason));
}

void TstAttributeFilter::multipleAttributeRequirementsAreAnded()
{
    AttributeFilter filter;
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::ReadOnly, Requirement::Required);
    filter.attributeBits.setRequirement(EntryAttribute::Hidden, Requirement::Required);

    EntryMetadata both = named(QStringLiteral("a"));
    both.withAttribute(EntryAttribute::ReadOnly, true);
    both.withAttribute(EntryAttribute::Hidden, true);
    QVERIFY(filter.accepts(both));

    // 只满足一个 → 挡住（条件之间是**与**，不是或）。
    EntryMetadata one = named(QStringLiteral("a"));
    one.withAttribute(EntryAttribute::ReadOnly, true);
    one.withAttribute(EntryAttribute::Hidden, false);
    QVERIFY(!filter.accepts(one));
    QCOMPARE(filter.attributeBits.requiredAttributes().size(), 2);
}

void TstAttributeFilter::attributeRequirementDefaultsToIgnore()
{
    AttributeBitsCondition condition;
    QVERIFY(!condition.isConstrained());
    for (EntryAttribute attribute : allEntryAttributes()) {
        QCOMPARE(QString::fromLatin1(requirementIdentifier(condition.requirement(attribute))),
                 QStringLiteral("ignore"));
    }

    // 「启用了但什么都没约束」不算生效——否则「某一项生效中」会恒为真。
    condition.setEnabled(true);
    QVERIFY(!condition.isActive());
    QVERIFY(condition.requiredAttributes().isEmpty());
    QVERIFY(condition.forbiddenAttributes().isEmpty());
}

void TstAttributeFilter::attributeIdentifiersAndLabelsAreDistinctAndStable()
{
    const QVector<EntryAttribute> attributes = allEntryAttributes();
    QCOMPARE(attributes.size(), 4);

    // 标识是**对外事实**：它会进声明文件与错误提示，所以逐个钉住。
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(EntryAttribute::ReadOnly)),
             QStringLiteral("readonly"));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(EntryAttribute::Hidden)),
             QStringLiteral("hidden"));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(EntryAttribute::System)),
             QStringLiteral("system"));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(EntryAttribute::Archive)),
             QStringLiteral("archive"));

    // 位必须互不相同：掩码撞了会让「只读」与「隐藏」互相冒充，
    // 而现象是「勾一个条件把另一类文件也带上了」。
    QSet<quint8> bits;
    for (EntryAttribute attribute : attributes) {
        QVERIFY(!entryAttributeLabel(attribute).isEmpty());
        QVERIFY2(!bits.contains(entryAttributeBit(attribute)), "两个属性位撞了");
        bits.insert(entryAttributeBit(attribute));
    }
    QCOMPARE(bits.size(), 4);
}

void TstAttributeFilter::parseEntryAttributeAcceptsNamesAndAliases()
{
    EntryAttribute attribute = EntryAttribute::Archive;

    QVERIFY(parseEntryAttribute(QStringLiteral("readonly"), &attribute));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(attribute)), QStringLiteral("readonly"));
    QVERIFY(parseEntryAttribute(QStringLiteral("READ-ONLY"), &attribute));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(attribute)), QStringLiteral("readonly"));
    QVERIFY(parseEntryAttribute(QStringLiteral("ro"), &attribute));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(attribute)), QStringLiteral("readonly"));
    QVERIFY(parseEntryAttribute(QStringLiteral("只读"), &attribute));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(attribute)), QStringLiteral("readonly"));
    QVERIFY(parseEntryAttribute(QStringLiteral("  隐藏 "), &attribute));
    QCOMPARE(QString::fromLatin1(entryAttributeIdentifier(attribute)), QStringLiteral("hidden"));

    QVERIFY(!parseEntryAttribute(QStringLiteral("nope"), &attribute));
    QVERIFY(!parseEntryAttribute(QString(), &attribute));
}

void TstAttributeFilter::attributeConditionDescribesSetAndUnsetBits()
{
    AttributeBitsCondition condition;
    condition.setRequirement(EntryAttribute::ReadOnly, Requirement::Required);
    condition.setRequirement(EntryAttribute::Hidden, Requirement::Forbidden);

    const QString text = condition.describe();
    QVERIFY2(text.contains(QStringLiteral("只读=置位")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("隐藏=未置位")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("系统")), qPrintable(text));

    AttributeBitsCondition fresh;
    QVERIFY(fresh.describe().contains(QStringLiteral("未设置")));
}

void TstAttributeFilter::everyEntryAttributeIsAddressableFromTheTable()
{
    // 「属性位」这一类条件在声明里靠 `attr` / `-attr` 两个键，而属性名本身
    // 由 `parseEntryAttribute` 认。两处必须对得上，否则声明里写不出这一类条件。
    AttributeConditionKind kind = AttributeConditionKind::Size;
    QVERIFY(attributeConditionKindForKey(QStringLiteral("attr"), &kind));
    QCOMPARE(QString::fromLatin1(attributeConditionIdentifier(kind)),
             QStringLiteral("attributes"));
    QVERIFY(attributeConditionKindForKey(QStringLiteral("-attr"), &kind));

    for (EntryAttribute attribute : allEntryAttributes()) {
        const QString identifier = QString::fromLatin1(entryAttributeIdentifier(attribute));
        EntryAttribute parsed = EntryAttribute::Archive;
        QVERIFY2(parseEntryAttribute(identifier, &parsed), qPrintable(identifier));
        QVERIFY(parseEntryAttribute(entryAttributeLabel(attribute), &parsed));
    }
}

// -----------------------------------------------------------------------------
// D 所有者与组（第 3 条后半）
// -----------------------------------------------------------------------------

void TstAttributeFilter::ownerAnyOfKeepsListedOwners()
{
    AttributeFilter filter;
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice"), QStringLiteral("bob")});

    QVERIFY(filter.owner.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("alice"))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("bob"))));
}

void TstAttributeFilter::ownerAnyOfRejectsOtherOwners()
{
    AttributeFilter filter;
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice"), QStringLiteral("bob")});

    const AttributeDecision decision =
        filter.decide(named(QStringLiteral("a")).withOwner(QStringLiteral("carol")));
    QVERIFY2(!decision.accepted, qPrintable(decision.describe()));
    QCOMPARE(QString::fromLatin1(attributeConditionIdentifier(decision.deciding)),
             QStringLiteral("owner"));
    QVERIFY2(decision.reason.contains(QStringLiteral("不在名单")), qPrintable(decision.reason));
}

void TstAttributeFilter::ownerNoneOfExcludesListedOwners()
{
    AttributeFilter filter;
    filter.owner.setEnabled(true);
    filter.owner.setOwnerMode(ListMatchMode::NoneOf);
    filter.owner.setOwners(QStringList{QStringLiteral("root")});

    QVERIFY(filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("alice"))));

    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")).withOwner(QStringLiteral("root")));
    QVERIFY(!decision.accepted);
    QVERIFY2(decision.reason.contains(QStringLiteral("排除名单")), qPrintable(decision.reason));
}

void TstAttributeFilter::ownerListIsCaseSensitiveOnPosix()
{
    AttributeFilter filter;
    filter.setPlatform(MaskPlatform::Posix);
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    // Unix 的账号名区分大小写：把 `Alice` 当成 `alice` 会让「只看某个人的文件」
    // 静默多出另一个用户的文件。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("alice"))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("Alice"))));
}

void TstAttributeFilter::ownerListIsCaseInsensitiveOnWindows()
{
    AttributeFilter filter;
    filter.setPlatform(MaskPlatform::Windows);
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    // Windows 的账号名（`DOMAIN\user`）不区分大小写。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("Alice"))));
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withOwner(QStringLiteral("ALICE"))));
}

void TstAttributeFilter::groupRequirementIsIndependentOfOwner()
{
    AttributeFilter filter;
    filter.owner.setEnabled(true);
    filter.owner.setGroups(QStringList{QStringLiteral("staff")});

    QVERIFY(filter.owner.isConstrained());
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withGroup(QStringLiteral("staff"))));
    // 只设了组时，所有者是谁不该影响结论（两条名单各自独立）。
    QVERIFY(filter.accepts(named(QStringLiteral("a")).withGroup(QStringLiteral("staff"))
                               .withOwner(QStringLiteral("whoever"))));
    QVERIFY(!filter.accepts(named(QStringLiteral("a")).withGroup(QStringLiteral("dev"))));
}

void TstAttributeFilter::unknownOwnerIsUndecided()
{
    AttributeFilter filter;
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")));
    QVERIFY(decision.accepted);
    QVERIFY(decision.outcomeFor(AttributeConditionKind::Owner)->undecided());

    // 排除模式也一样：「不在名单里」这个结论需要先知道它**是**谁。
    // 在这里放行会造成「排除名单看起来生效了，实际上一个都没排除」。
    AttributeFilter excluding;
    excluding.owner.setEnabled(true);
    excluding.owner.setOwnerMode(ListMatchMode::NoneOf);
    excluding.owner.setOwners(QStringList{QStringLiteral("root")});
    const AttributeDecision excluded = excluding.decide(named(QStringLiteral("a")));
    QVERIFY(excluded.accepted);
    QVERIFY(excluded.outcomeFor(AttributeConditionKind::Owner)->undecided());
}

void TstAttributeFilter::nameListTextSplitsOnCommasAndSpaces()
{
    const QStringList names = splitNameListText(QStringLiteral(" alice, bob;carol，dave；eve\n frank "));
    QCOMPARE(names,
             QStringList({QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol"),
                          QStringLiteral("dave"), QStringLiteral("eve"), QStringLiteral("frank")}));
    QVERIFY(splitNameListText(QStringLiteral("   ")).isEmpty());
    QVERIFY(splitNameListText(QStringLiteral(",,")).isEmpty());
}

void TstAttributeFilter::ownerConditionDescribesBothModes()
{
    OwnerCondition condition;
    QVERIFY(condition.describe().contains(QStringLiteral("未设置")));

    condition.setOwners(QStringList{QStringLiteral("alice")});
    QVERIFY2(condition.describe().contains(QStringLiteral("只看名单内")), qPrintable(condition.describe()));
    QVERIFY2(condition.describe().contains(QStringLiteral("alice")), qPrintable(condition.describe()));

    condition.setOwnerMode(ListMatchMode::NoneOf);
    QVERIFY2(condition.describe().contains(QStringLiteral("排除名单内")), qPrintable(condition.describe()));

    condition.setGroups(QStringList{QStringLiteral("staff")});
    QVERIFY2(condition.describe().contains(QStringLiteral("staff")), qPrintable(condition.describe()));
}

// -----------------------------------------------------------------------------
// E 与名称过滤的与关系（第 4 条）
// -----------------------------------------------------------------------------

void TstAttributeFilter::entryIsKeptOnlyWhenBothSidesAgree()
{
    const MaskFilterParseResult names = MaskFilter::parse(QStringLiteral("*.txt"));

    AttributeFilter attributes;
    attributes.size.setEnabled(true);
    attributes.size.setRangeText(QStringLiteral("1 KB"), QString());

    QVERIFY(decideEntry(names.filter, attributes,
                        named(QStringLiteral("a.txt")).withSize(2048)).accepted);
    // 名称通过、属性不通过
    QVERIFY(!decideEntry(names.filter, attributes,
                         named(QStringLiteral("a.txt")).withSize(512)).accepted);
    // 名称不通过、属性通过
    QVERIFY(!decideEntry(names.filter, attributes,
                         named(QStringLiteral("a.bin")).withSize(2048)).accepted);
    // 两边都不通过
    QVERIFY(!decideEntry(names.filter, attributes,
                         named(QStringLiteral("a.bin")).withSize(512)).accepted);
}

void TstAttributeFilter::nameExclusionWinsEvenWhenAttributesPass()
{
    FilterStack names;
    names.setDeclaration(FilterLayer::Session, QStringLiteral("-*.tmp"));

    AttributeFilter attributes;
    attributes.size.setEnabled(true);
    attributes.size.setRangeText(QStringLiteral("1 KB"), QString());

    const EntryFilterDecision decision =
        decideEntry(names, attributes, named(QStringLiteral("a.tmp")).withSize(4096));
    QVERIFY2(!decision.accepted, qPrintable(decision.describe()));
    QVERIFY(decision.blockedByName());
    QVERIFY(!decision.blockedByAttributes());
    QCOMPARE(QString::fromLatin1(maskVerdictIdentifier(decision.nameVerdict)),
             QStringLiteral("excluded"));
}

void TstAttributeFilter::attributeRejectionWinsEvenWhenNamePasses()
{
    FilterStack names;
    names.setDeclaration(FilterLayer::Session, QStringLiteral("*.txt"));

    AttributeFilter attributes;
    attributes.size.setEnabled(true);
    attributes.size.setRangeText(QStringLiteral("1 KB"), QString());

    const EntryFilterDecision decision =
        decideEntry(names, attributes, named(QStringLiteral("a.txt")).withSize(512));
    QVERIFY(!decision.accepted);
    QVERIFY(!decision.blockedByName());
    QVERIFY(decision.blockedByAttributes());
    QCOMPARE(QString::fromLatin1(maskVerdictIdentifier(decision.nameVerdict)),
             QStringLiteral("included"));
}

void TstAttributeFilter::decideEntryReportsTheDecidingLayer()
{
    FilterStack names;
    names.setDeclaration(FilterLayer::Session, QStringLiteral("*.txt"));

    AttributeFilter attributes; // 没有任何条件 → 一律放行

    const EntryFilterDecision decision = decideEntry(names, attributes, named(QStringLiteral("b.bin")));
    QVERIFY(!decision.accepted);
    QVERIFY(decision.hasDecidingLayer);
    QCOMPARE(QString::fromLatin1(filterLayerIdentifier(decision.decidingLayer)),
             QStringLiteral("session"));
    QCOMPARE(QString::fromLatin1(maskVerdictIdentifier(decision.nameVerdict)),
             QStringLiteral("not-matched"));
    // 属性侧没有被问到什么，所以它不该被说成「挡下它的原因」。
    QVERIFY(!decision.blockedByAttributes());
}

void TstAttributeFilter::decideEntryWithASingleMaskFilterReportsTheRule()
{
    const MaskFilterParseResult names = MaskFilter::parse(QStringLiteral("*.txt\n-idle.txt"));

    AttributeFilter attributes;
    const EntryFilterDecision decision =
        decideEntry(names.filter, attributes, named(QStringLiteral("idle.txt")));

    QVERIFY(!decision.accepted);
    QCOMPARE(decision.ruleIndex, 1);
    QCOMPARE(decision.ruleText, QStringLiteral("idle.txt"));
    // 单个 MaskFilter 没有「层」的概念，因此这一位必须为假——
    // 否则界面会去显示一个不存在的层名。
    QVERIFY(!decision.hasDecidingLayer);
}

void TstAttributeFilter::entryDecisionTextMentionsBothSides()
{
    FilterStack names;
    names.setDeclaration(FilterLayer::Session, QStringLiteral("*.txt"));

    AttributeFilter attributes;
    attributes.size.setEnabled(true);
    attributes.size.setRangeText(QStringLiteral("1 KB"), QString());

    const QString text = decideEntry(names, attributes, named(QStringLiteral("a.txt")).withSize(512)).describe();
    QVERIFY2(text.contains(QStringLiteral("名称过滤")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("属性过滤")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("最终结论")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("过滤掉")), qPrintable(text));
}

void TstAttributeFilter::entryDecisionIsAConjunctionOverAllCombinations()
{
    // 四种组合各跑一遍，断言「整体结论 = 名称侧 && 属性侧」。
    // 只测其中一两种组合的话，把实现写成 `||` 仍然能全绿。
    const QStringList names = {QStringLiteral("keep.txt"), QStringLiteral("drop.bin")};
    const QVector<quint64> sizes = {4096, 512};

    const MaskFilterParseResult mask = MaskFilter::parse(QStringLiteral("*.txt"));
    AttributeFilter attributes;
    attributes.size.setEnabled(true);
    attributes.size.setRangeText(QStringLiteral("1 KB"), QString());

    for (const QString &name : names) {
        for (quint64 size : sizes) {
            const EntryFilterDecision decision =
                decideEntry(mask.filter, attributes, named(name).withSize(size));
            const bool nameOk = name.endsWith(QStringLiteral(".txt"));
            const bool sizeOk = size >= 1024;
            QVERIFY2(decision.accepted == (nameOk && sizeOk),
                     qPrintable(QStringLiteral("%1 / %2：%3").arg(name).arg(size).arg(decision.describe())));
        }
    }
}

void TstAttributeFilter::singleMaskFilterOverloadHasNoDecidingLayer()
{
    const MaskFilterParseResult names = MaskFilter::parse(QString());

    AttributeFilter attributes;
    const EntryFilterDecision decision = decideEntry(names.filter, attributes, named(QStringLiteral("any")));
    QVERIFY(decision.accepted);
    QVERIFY(!decision.hasDecidingLayer);
    QCOMPARE(QString::fromLatin1(maskVerdictIdentifier(decision.nameVerdict)),
             QStringLiteral("included"));
    QCOMPARE(decision.ruleIndex, -1);
}

// -----------------------------------------------------------------------------
// F 声明文本
// -----------------------------------------------------------------------------

void TstAttributeFilter::declarationRoundTripsEveryCondition()
{
    const QString text = QStringLiteral(
        "# 属性过滤声明\n"
        "size-min 10 MB\n"
        "size-max 2 GB\n"
        "time-relative 最近 7 天\n"
        "attr readonly\n"
        "-attr system\n"
        "owner alice, bob\n"
        "group staff\n");

    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(text);
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeProblems()));
    const AttributeFilter &filter = parsed.filter;
    QVERIFY(filter.size.isActive());
    QVERIFY(filter.timeRange.isActive() == false); // 还没有参考时刻
    QVERIFY(filter.attributeBits.isActive());
    QVERIFY(filter.owner.isActive());

    // 往返：写出去 → 读回来 → 对同一批条目的结论必须一致。
    AttributeFilter before = filter;
    before.setReferenceTime(moment(2026, 9, 20, 19, 0));
    AttributeFilterParseResult again = AttributeFilter::parseDeclaration(before.toDeclarationText());
    QVERIFY2(again.ok(), qPrintable(again.describeProblems()));
    again.filter.setReferenceTime(moment(2026, 9, 20, 19, 0));

    QCOMPARE(again.filter.size.minimumBytes(), before.size.minimumBytes());
    QCOMPARE(again.filter.size.maximumBytes(), before.size.maximumBytes());
    QCOMPARE(again.filter.timeRange.relativeDays(), 7);
    QCOMPARE(QString::fromLatin1(requirementIdentifier(
                 again.filter.attributeBits.requirement(EntryAttribute::ReadOnly))),
             QStringLiteral("required"));
    QCOMPARE(QString::fromLatin1(requirementIdentifier(
                 again.filter.attributeBits.requirement(EntryAttribute::System))),
             QStringLiteral("forbidden"));
    QCOMPARE(again.filter.owner.owners(), before.owner.owners());
    QCOMPARE(again.filter.owner.groups(), before.owner.groups());
    QCOMPARE(QString::fromLatin1(listMatchModeIdentifier(again.filter.owner.ownerMode())),
             QStringLiteral("any-of"));
}

void TstAttributeFilter::declarationIgnoresCommentsAndBlankLines()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("# 只有注释\n\n   \n\t\n# attr readonly\n"));
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeProblems()));
    QVERIFY(!parsed.filter.isActive());
    QVERIFY(!parsed.filter.attributeBits.isConstrained());
}

void TstAttributeFilter::declarationRejectsUnknownKeyAndListsAvailableOnes()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(QStringLiteral("sizemin 10MB"));
    QCOMPARE(parsed.problems.size(), 1);

    const ConditionProblem &problem = parsed.problems.first();
    QVERIFY2(!problem.hasCondition, "未知的键归不到任何一类条件上");
    QCOMPARE(problem.key, QStringLiteral("sizemin"));
    QVERIFY2(problem.message.contains(QStringLiteral("未知的键")), qPrintable(problem.message));
    // 提示里的可用键必须来自那张表（否则提示与实际认的键会分家）。
    QVERIFY2(problem.hint.contains(QStringLiteral("size-min")), qPrintable(problem.hint));
    QVERIFY2(problem.hint.contains(QStringLiteral("attr")), qPrintable(problem.hint));
    QVERIFY(!parsed.filter.isActive());
}

void TstAttributeFilter::declarationRejectsMissingValue()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("size-min\nattr\nowner\n"));
    QCOMPARE(parsed.problems.size(), 3);
    for (const ConditionProblem &problem : parsed.problems)
        QVERIFY2(problem.message.contains(QStringLiteral("缺少")), qPrintable(problem.message));
    QVERIFY(!parsed.filter.isActive());
}

void TstAttributeFilter::declarationRejectsDuplicateKey()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("size-min 1 MB\nsize-min 2 MB\n"));
    QCOMPARE(parsed.problems.size(), 1);
    QVERIFY2(parsed.problems.first().message.contains(QStringLiteral("重复")),
             qPrintable(parsed.problems.first().message));
    // **首次出现生效**：在文件末尾追加一行不会静默改变已有行为。
    QCOMPARE(parsed.filter.size.minimumBytes(), kMega);
}

void TstAttributeFilter::declarationRejectsConflictingTimeKeys()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("time-relative 7 天\ntime-from 2026-09-01\n"));
    QCOMPARE(parsed.problems.size(), 1);
    QVERIFY2(parsed.problems.first().message.contains(QStringLiteral("不能同时出现")),
             qPrintable(parsed.problems.first().message));
    // 第一行决定形态，被拒的那一行不改变结论。
    QCOMPARE(QString::fromLatin1(timeRangeKindIdentifier(parsed.filter.timeRange.kind())),
             QStringLiteral("relative"));
    QCOMPARE(parsed.filter.timeRange.relativeDays(), 7);
}

void TstAttributeFilter::declarationRejectsUnknownAttributeName()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(QStringLiteral("attr nope"));
    QCOMPARE(parsed.problems.size(), 1);
    const ConditionProblem &problem = parsed.problems.first();
    QCOMPARE(QString::fromLatin1(conditionFieldIdentifier(problem.field)), QStringLiteral("attributes"));
    QVERIFY2(problem.message.contains(QStringLiteral("未知的属性名")), qPrintable(problem.message));
    QVERIFY2(problem.hint.contains(QStringLiteral("只读")), qPrintable(problem.hint));
    QVERIFY(!parsed.filter.attributeBits.isConstrained());
}

void TstAttributeFilter::declarationAcceptsChineseAttributeLabels()
{
    const AttributeFilterParseResult parsed =
        AttributeFilter::parseDeclaration(QStringLiteral("attr 只读\n-attr 隐藏"));
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeProblems()));
    QCOMPARE(QString::fromLatin1(requirementIdentifier(
                 parsed.filter.attributeBits.requirement(EntryAttribute::ReadOnly))),
             QStringLiteral("required"));
    QCOMPARE(QString::fromLatin1(requirementIdentifier(
                 parsed.filter.attributeBits.requirement(EntryAttribute::Hidden))),
             QStringLiteral("forbidden"));
}

void TstAttributeFilter::declarationAcceptsCrlfLineEndings()
{
    // Windows 上编辑过的声明文件是 CRLF。只按 `\n` 切的话，最后一列会多出
    // 一个 `\r`——`hidden\r` 认不出来，于是每一条属性行都报「未知的属性名」。
    const AttributeFilterParseResult parsed =
        AttributeFilter::parseDeclaration(QStringLiteral("attr readonly\r\nattr hidden\r\n"));
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeProblems()));
    QCOMPARE(parsed.filter.attributeBits.requiredAttributes().size(), 2);

    // 单独一个 `\r`（老式 Mac 行尾）也当换行。这一条单独写出来是因为它**能**
    // 区分实现：只按 `\n` 切的话，下面整段会被当成一行，
    // 于是键是 `attr`、值是 `readonly\rattr hidden\r` —— 直接报未知属性名。
    const AttributeFilterParseResult lone =
        AttributeFilter::parseDeclaration(QStringLiteral("attr readonly\rattr hidden\r"));
    QVERIFY2(lone.ok(), qPrintable(lone.describeProblems()));
    QCOMPARE(lone.filter.attributeBits.requiredAttributes().size(), 2);

    // 行号也要跟着对：`\r\n` 是一个换行而不是两个，否则报错的行号会整体偏大。
    const AttributeFilterParseResult numbered =
        AttributeFilter::parseDeclaration(QStringLiteral("# 注释\r\nattr nope\r\n"));
    QCOMPARE(numbered.problems.size(), 1);
    QCOMPARE(numbered.problems.first().line, 1);
}

void TstAttributeFilter::declarationEnablesConditionsThatHaveConstraints()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("size-min 10 MB\ntime-relative 7 天\nattr readonly\nowner alice\n"));
    QVERIFY(parsed.filter.size.isEnabled());
    QVERIFY(parsed.filter.timeRange.isEnabled());
    QVERIFY(parsed.filter.attributeBits.isEnabled());
    QVERIFY(parsed.filter.owner.isEnabled());

    // 没写到的条件保持禁用：否则界面上一打开就是四个勾好的空条件。
    const AttributeFilterParseResult empty = AttributeFilter::parseDeclaration(QString());
    QVERIFY(!empty.filter.size.isEnabled());
    QVERIFY(!empty.filter.timeRange.isEnabled());
    QVERIFY(!empty.filter.attributeBits.isEnabled());
    QVERIFY(!empty.filter.owner.isEnabled());
}

void TstAttributeFilter::declarationReportsLineAndColumn()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("# 注释\nattr nope\n"));
    QCOMPARE(parsed.problems.size(), 1);
    const ConditionProblem &problem = parsed.problems.first();
    QCOMPARE(problem.line, 1);   // 0 起，注释是第 0 行
    QCOMPARE(problem.column, 5); // `attr ` 之后
    QVERIFY2(problem.describe().contains(QStringLiteral("第 2 行")), qPrintable(problem.describe()));
}

void TstAttributeFilter::declarationKeyDiffersFromTheMaskDeclarationKey()
{
    // 两个键住在同一个存储（会话设置）里但是两份不同的数据。共用一个键会让
    // 属性声明的解析把掩码行当成「未知的键」报错，反过来也一样。
    QVERIFY(attributeFilterDeclarationKey() != filterDeclarationSettingKey());
    QCOMPARE(attributeFilterDeclarationKey(), QStringLiteral("attribute-filter"));
}

void TstAttributeFilter::declarationMinusPrefixMeansExclusion()
{
    const AttributeFilterParseResult parsed = AttributeFilter::parseDeclaration(
        QStringLiteral("-owner root\n-group nogroup\n"));
    QVERIFY2(parsed.ok(), qPrintable(parsed.describeProblems()));
    QCOMPARE(QString::fromLatin1(listMatchModeIdentifier(parsed.filter.owner.ownerMode())),
             QStringLiteral("none-of"));
    QCOMPARE(QString::fromLatin1(listMatchModeIdentifier(parsed.filter.owner.groupMode())),
             QStringLiteral("none-of"));
    QCOMPARE(parsed.filter.owner.owners(), QStringList({QStringLiteral("root")}));
    QCOMPARE(parsed.filter.owner.groups(), QStringList({QStringLiteral("nogroup")}));
}

// -----------------------------------------------------------------------------
// G 缺失与写错
// -----------------------------------------------------------------------------

void TstAttributeFilter::emptyFilterAcceptsEverything()
{
    const AttributeFilter filter;
    QVERIFY(!filter.isActive());
    QVERIFY(filter.activeConditions().isEmpty());
    QVERIFY(filter.problems().isEmpty());
    QVERIFY(filter.accepts(named(QStringLiteral("anything"))));

    const AttributeDecision decision = filter.decide(named(QStringLiteral("anything")));
    QVERIFY(decision.accepted);
    QVERIFY2(!decision.anyConditionActive, qPrintable(decision.describe()));
    // 四个条件都「没生效」，但它们**不该**进 undecided 清单：
    // 那是留给「设了却判不了」的条件的，混进来会让面板变成一句废话。
    QVERIFY(decision.undecided.isEmpty());
    QCOMPARE(decision.outcomes.size(), allAttributeConditions().size());
}

void TstAttributeFilter::filterWithNoActiveConditionAcceptsEverything()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.timeRange.setEnabled(true);
    filter.attributeBits.setEnabled(true);
    filter.owner.setEnabled(true);

    // 「启用了但没有任何约束」不算生效——否则「某一项生效中」会恒为真，
    // 用户就无从判断到底是谁在过滤（与 FILT-005 的 active() 同一条纪律）。
    QVERIFY(!filter.isActive());
    QVERIFY(filter.accepts(named(QStringLiteral("anything"))));
    QVERIFY(filter.decide(named(QStringLiteral("anything"))).undecided.isEmpty());
}

void TstAttributeFilter::decisionListsUndecidedConditionsOnly()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("10 MB"), QString());
    filter.timeRange.setEnabled(true);            // 启用但没有内容 → 不算生效
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    const AttributeDecision decision = filter.decide(named(QStringLiteral("a")).withSize(20 * kMega));
    // 大小判过了（evaluated），所有者判不了（undecided），时间没生效（两者都不是）。
    const QStringList undecidedIdentifiers = [&decision] {
        QStringList list;
        for (AttributeConditionKind kind : decision.undecided)
            list.append(QString::fromLatin1(attributeConditionIdentifier(kind)));
        return list;
    }();
    QCOMPARE(undecidedIdentifiers, QStringList({QStringLiteral("owner")}));
    QVERIFY(decision.outcomeFor(AttributeConditionKind::Size)->passed());
    QVERIFY(decision.outcomeFor(AttributeConditionKind::TimeRange)->undecided());
}

void TstAttributeFilter::decisionReportsProblemsWithoutBlocking()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("100 MB"), QStringLiteral("10 MB")); // 写反了
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    const AttributeDecision decision =
        filter.decide(named(QStringLiteral("a")).withSize(50 * kMega).withOwner(QStringLiteral("alice")));
    QVERIFY2(decision.accepted, qPrintable(decision.describe()));
    QVERIFY(decision.hasProblems());
    QCOMPARE(decision.problems.size(), 1);
    QVERIFY2(decision.describe().contains(QStringLiteral("配置问题")), qPrintable(decision.describe()));
}

void TstAttributeFilter::decisionNamesTheFirstBlockingCondition()
{
    AttributeFilter filter;
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("10 MB"), QString());
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    const AttributeDecision decision = filter.decide(
        named(QStringLiteral("a")).withSize(1 * kKilo).withOwner(QStringLiteral("carol")));
    QVERIFY(!decision.accepted);
    // 起决定作用的是表里排在前面那一条（大小在时间/属性位之前）。
    QCOMPARE(decision.decidingIdentifier, QStringLiteral("size"));
    QVERIFY2(decision.reason.contains(QStringLiteral("小于下限")), qPrintable(decision.reason));
}

void TstAttributeFilter::decisionListsEveryBlockingCondition()
{
    const QDateTime now = moment(2026, 9, 20, 19, 0);

    AttributeFilter filter;
    filter.setReferenceTime(now);
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("10 MB"), QString());
    filter.timeRange.setEnabled(true);
    filter.timeRange.setKind(TimeRangeKind::Relative);
    filter.timeRange.setRelativeDays(7);
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::ReadOnly, Requirement::Required);
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});

    EntryMetadata entry = named(QStringLiteral("a"));
    entry.withSize(1 * kKilo);
    entry.withLastModified(moment(2020, 1, 1));
    entry.withoutAttribute(EntryAttribute::ReadOnly);
    entry.withOwner(QStringLiteral("carol"));

    const AttributeDecision decision = filter.decide(entry);
    QVERIFY(!decision.accepted);
    QCOMPARE(decision.blocking.size(), 4);

    QStringList blocking;
    for (AttributeConditionKind kind : decision.blocking)
        blocking.append(QString::fromLatin1(attributeConditionIdentifier(kind)));
    QCOMPARE(blocking, QStringList({QStringLiteral("size"), QStringLiteral("time"),
                                    QStringLiteral("attributes"), QStringLiteral("owner")}));

    const QString text = decision.describe();
    QVERIFY2(text.contains(QStringLiteral("起决定作用")), qPrintable(text));
    QVERIFY2(!decision.hasProblems(), qPrintable(text));
}

void TstAttributeFilter::outcomeTriStateIsReadableInText()
{
    ConditionOutcome passed;
    passed.accepted = true;
    passed.evaluated = true;
    passed.reason = QStringLiteral("大小 20 MB 在 10 MB ～ 100 MB 内");
    QVERIFY(passed.passed());
    QVERIFY(!passed.undecided());
    QVERIFY2(passed.describe().contains(QStringLiteral("通过")), qPrintable(passed.describe()));
    QVERIFY2(passed.describe().contains(QStringLiteral("20 MB")), qPrintable(passed.describe()));

    ConditionOutcome undecided;
    undecided.accepted = true;
    undecided.evaluated = false;
    undecided.reason = QStringLiteral("大小未知，已放行");
    QVERIFY(!undecided.passed());
    QVERIFY(undecided.undecided());
    QVERIFY2(undecided.describe().contains(QStringLiteral("未生效")), qPrintable(undecided.describe()));

    ConditionOutcome rejected;
    rejected.accepted = false;
    rejected.evaluated = true;
    rejected.reason = QStringLiteral("大小 2 MB 小于下限 10 MB");
    QVERIFY(!rejected.passed());
    QVERIFY(!rejected.undecided());
    QVERIFY2(rejected.describe().contains(QStringLiteral("未通过")), qPrintable(rejected.describe()));
}

void TstAttributeFilter::activeConditionsFollowTheTableOrder()
{
    AttributeFilter filter;
    // 故意按「所有者 → 属性位 → 大小」的逆序设置。
    filter.owner.setEnabled(true);
    filter.owner.setOwners(QStringList{QStringLiteral("alice")});
    filter.attributeBits.setEnabled(true);
    filter.attributeBits.setRequirement(EntryAttribute::Hidden, Requirement::Forbidden);
    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("1 KB"), QString());

    QStringList active;
    for (AttributeConditionKind kind : filter.activeConditions())
        active.append(QString::fromLatin1(attributeConditionIdentifier(kind)));
    // 顺序来自那张表，而不是设置的先后：否则面板上的顺序会随用户的操作历史变化。
    QCOMPARE(active, QStringList({QStringLiteral("size"), QStringLiteral("attributes"),
                                  QStringLiteral("owner")}));
}

void TstAttributeFilter::filterDescriptionListsEveryCondition()
{
    AttributeFilter filter;
    const QString empty = filter.describe();
    QVERIFY2(empty.contains(QStringLiteral("大小")), qPrintable(empty));
    QVERIFY2(empty.contains(QStringLiteral("修改时间")), qPrintable(empty));
    QVERIFY2(empty.contains(QStringLiteral("属性位")), qPrintable(empty));
    QVERIFY2(empty.contains(QStringLiteral("所有者与组")), qPrintable(empty));
    QVERIFY2(empty.contains(QStringLiteral("未生效")), qPrintable(empty));

    filter.size.setEnabled(true);
    filter.size.setRangeText(QStringLiteral("10 MB"), QStringLiteral("2 GB"));
    const QString active = filter.describe();
    QVERIFY2(active.contains(QStringLiteral("10 MB")), qPrintable(active));
    QVERIFY2(active.contains(QStringLiteral("2 GB")), qPrintable(active));
}

// -----------------------------------------------------------------------------
// H 条件表自检
// -----------------------------------------------------------------------------

void TstAttributeFilter::shippedConditionTableIsClean()
{
    const QVector<AttributeConditionDescriptor> &table = attributeConditionTable();
    QCOMPARE(table.size(), 4);
    QCOMPARE(allAttributeConditions().size(), 4);
    const QStringList problems = validateAttributeConditionTable(table);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("；"))));

    // 唯一需要「现在」的是相对时间；随平台变的是所有者/组。
    QCOMPARE(attributeConditionDescriptor(AttributeConditionKind::TimeRange)->needsReferenceTime, true);
    QCOMPARE(attributeConditionDescriptor(AttributeConditionKind::Size)->needsReferenceTime, false);
    QCOMPARE(attributeConditionDescriptor(AttributeConditionKind::Owner)->needsPlatform, true);
    QCOMPARE(attributeConditionDescriptor(AttributeConditionKind::Size)->needsPlatform, false);
}

void TstAttributeFilter::tableDrivesEveryCondition()
{
    int index = 0;
    for (AttributeConditionKind kind : allAttributeConditions()) {
        const AttributeConditionDescriptor *row = attributeConditionDescriptor(kind);
        QVERIFY2(row != nullptr, "表里少了一类条件");
        QCOMPARE(index, attributeConditionIndex(kind));
        QCOMPARE(row->kind, kind);
        QVERIFY(!row->identifier.isEmpty());
        QVERIFY(!row->label.isEmpty());
        QVERIFY(!row->declarationKeys.isEmpty());
        // 声明键必须都能反查回这一类条件（错误提示与定位都靠它）。
        AttributeConditionKind looked = AttributeConditionKind::Size;
        for (const QString &key : row->declarationKeys) {
            QVERIFY2(attributeConditionKindForKey(key, &looked), qPrintable(key));
            QCOMPARE(looked, kind);
        }
        ++index;
    }
    QVERIFY(attributeConditionDescriptor(AttributeConditionKind::Size) != nullptr);
    QVERIFY(attributeConditionIndex(AttributeConditionKind::Owner) >= 0);
}

void TstAttributeFilter::validatorCatchesAWrongRowCount()
{
    QVector<AttributeConditionDescriptor> broken = attributeConditionTable();
    broken.removeLast();

    const QStringList problems = validateAttributeConditionTable(broken);
    QVERIFY2(!problems.isEmpty(), "少一行时必须报出来");
    QVERIFY2(problems.first().contains(QStringLiteral("行数")), qPrintable(problems.join(QStringLiteral("；"))));
}

void TstAttributeFilter::validatorCatchesAReorderedTable()
{
    QVector<AttributeConditionDescriptor> broken = attributeConditionTable();
    broken.swapItemsAt(0, 1);

    const QStringList problems = validateAttributeConditionTable(broken);
    QVERIFY2(!problems.isEmpty(), "顺序变了必须报出来");
    QVERIFY2(problems.first().contains(QStringLiteral("条件种类不对")),
             qPrintable(problems.join(QStringLiteral("；"))));
}

void TstAttributeFilter::validatorCatchesADuplicatedIdentifier()
{
    QVector<AttributeConditionDescriptor> broken = attributeConditionTable();
    broken[1].identifier = broken[0].identifier;

    const QStringList problems = validateAttributeConditionTable(broken);
    QVERIFY2(!problems.isEmpty(), "标识重复必须报出来");
    QVERIFY2(problems.join(QStringLiteral("；")).contains(QStringLiteral("重复")),
             qPrintable(problems.join(QStringLiteral("；"))));
}

void TstAttributeFilter::validatorCatchesADuplicatedDeclarationKey()
{
    QVector<AttributeConditionDescriptor> broken = attributeConditionTable();
    broken[1].declarationKeys.append(broken[0].declarationKeys.first());

    const QStringList problems = validateAttributeConditionTable(broken);
    QVERIFY2(!problems.isEmpty(), "声明键重复必须报出来");
    QVERIFY2(problems.join(QStringLiteral("；")).contains(QStringLiteral("两处出现")),
             qPrintable(problems.join(QStringLiteral("；"))));
}

void TstAttributeFilter::validatorCatchesAConditionThatWouldReadContent()
{
    QVector<AttributeConditionDescriptor> broken = attributeConditionTable();
    broken[2].metadataOnly = false;

    const QStringList problems = validateAttributeConditionTable(broken);
    QVERIFY2(!problems.isEmpty(), "「这一条会去读条目内容」必须报出来");
    QVERIFY2(problems.join(QStringLiteral("；")).contains(QStringLiteral("内容")),
             qPrintable(problems.join(QStringLiteral("；"))));
}

void TstAttributeFilter::validatorCatchesAMissingDeclarationKey()
{
    QVector<AttributeConditionDescriptor> broken = attributeConditionTable();
    broken[3].declarationKeys.clear();

    const QStringList problems = validateAttributeConditionTable(broken);
    QVERIFY2(!problems.isEmpty(), "没有声明键必须报出来");
    QVERIFY2(problems.join(QStringLiteral("；")).contains(QStringLiteral("声明键")),
             qPrintable(problems.join(QStringLiteral("；"))));
}

// -----------------------------------------------------------------------------
// I 与内容解耦（第 5 条能独立验证的那一半）
// -----------------------------------------------------------------------------

void TstAttributeFilter::attributeFilterSourcesNeverTouchFileContents()
{
    // 规格的边界条款：属性过滤必须与内容比对解耦——被属性过滤掉的条目
    // 不应被读取内容。服务层能独立保证的是「判定只看元数据」这一半：
    // 判定函数的输入类型 `EntryMetadata` 里根本没有内容字段，
    // 代码里也不该出现任何读文件内容的 API。真正「扫描阶段早期生效」的
    // 性能断言要等扫描器（见 issue 的落地说明）。
    const QString header = readSourceFile(QStringLiteral("/Services/Filter/attributefilter.h"));
    const QString source = readSourceFile(QStringLiteral("/Services/Filter/attributefilter.cpp"));
    QVERIFY(!header.isEmpty());
    QVERIFY(!source.isEmpty());

    const QStringList found = scanForContentAccess(header + source);
    QVERIFY2(found.isEmpty(), qPrintable(QStringLiteral("属性过滤里出现了读内容的 API：%1")
                                            .arg(found.join(QStringLiteral("、")))));
}

void TstAttributeFilter::contentScanDetectsAPlantedRead()
{
    // 反向验证：护栏本身必须会报错。拿一段故意写了读内容的源码跑同一个判定，
    // 认不出来就说明上面那条「永远为绿」——比没有护栏更糟。
    const QString planted =
        QStringLiteral("QByteArray data = QFile(path).readAll();\nQTextStream stream;");
    const QStringList found = scanForContentAccess(planted);
    QVERIFY2(!found.isEmpty(), "护栏认不出读内容的写法，等于没有护栏");
    QVERIFY(found.contains(QStringLiteral("readAll")));
    QVERIFY(found.contains(QStringLiteral("QFile")));

    // 干净的样本不能被误判（否则这条护栏会因为噪声被关掉）。
    QVERIFY(scanForContentAccess(QStringLiteral("QString reason = QStringLiteral(\"只读=置位\");"))
                .isEmpty());
}

QTEST_MAIN(TstAttributeFilter)
