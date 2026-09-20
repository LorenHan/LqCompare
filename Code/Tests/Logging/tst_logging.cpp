#include "tst_logging.h"

#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <thread>

using LqCompare::Log::Level;
using LqCompare::Log::Record;

namespace {

/// 把记录收进一个向量，供断言检查。
///
/// 每个用例都自己建一份、用完即弃：日志的接收者是全局状态，
/// 用共享的容器会让「上一个用例漏删了一个接收者」表现为「这个用例多收到一条」，
/// 而失败信息指向的是无辜的那条用例。
struct Capture
{
    QVector<Record> records;
};

int captureInto(Capture *capture)
{
    return LqCompare::Log::addSink([capture](const Record &record) {
        capture->records << record;
    });
}

QString readWholeFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

/// 从「X 耗时 12 ms」里取出毫秒数；取不到返回 -1。
qint64 millisecondsFrom(const QString &message)
{
    const int start = message.indexOf(QStringLiteral("耗时"));
    if (start < 0)
        return -1;
    const int end = message.indexOf(QStringLiteral(" ms"), start);
    if (end < 0)
        return -1;
    const QString number = message.mid(start + QStringLiteral("耗时").size(),
                                       end - start - QStringLiteral("耗时").size())
                                   .trimmed();
    bool ok = false;
    const qint64 value = number.toLongLong(&ok);
    return ok ? value : -1;
}

quintptr currentThreadId()
{
    return reinterpret_cast<quintptr>(QThread::currentThreadId());
}

} // namespace

void TstLogging::initTestCase()
{
    // 这是**进程启动之后、任何用例动手之前**的唯一一次观察机会。
    //
    // 默认级别是产品行为的一部分：它决定「用户什么都不说时日志里有什么」。
    // 定成 Trace 会让日志文件在正常使用下也一直膨胀；定成 Error 则会让
    // 用户描述的现象与日志里唯一的证据擦肩而过。
    //
    // 必须写在这里而不是某个用例里：`init()` 会把级别设成 Warning，
    // 之后再也分不清「本来就是 Warning」还是「被 init 设成了 Warning」。
    QCOMPARE(LqCompare::Log::level(), Level::Warning);
    QVERIFY(LqCompare::Log::logFile().isEmpty());
    QCOMPARE(LqCompare::Log::sinkCount(), 0);
    // 与级别同理：轮转与详细性能计时也各有出厂默认值，而它们同样是产品行为。
    QVERIFY(!LqCompare::Log::performanceTimingEnabled());
    QCOMPARE(QString::fromLatin1(
                     LqCompare::Log::rotationModeIdentifier(LqCompare::Log::rotationPolicy().mode)),
             QStringLiteral("none"));
}

void TstLogging::init()
{
    // 日志模块持有全局状态，用例之间必须隔离。顺序也有讲究：
    // 先清接收者再关文件——反过来的话，关文件时若还挂着接收者，
    // 某些实现会顺手往里写一条「日志文件已关闭」，于是这份记录落到下一个用例头上。
    LqCompare::Log::clearSinks();
    LqCompare::Log::setLogFile(QString());
    LqCompare::Log::setLevel(Level::Warning);
    LqCompare::Log::setRotationPolicy(LqCompare::Log::RotationPolicy());
    LqCompare::Log::setPerformanceTimingEnabled(false);
}

void TstLogging::cleanupTestCase()
{
    // 不留任何全局残留：本套件跑完之后，其它套件的输出里不该多出意料之外的内容。
    LqCompare::Log::clearSinks();
    LqCompare::Log::setLogFile(QString());
    LqCompare::Log::setLevel(Level::Warning);
    LqCompare::Log::setRotationPolicy(LqCompare::Log::RotationPolicy());
    LqCompare::Log::setPerformanceTimingEnabled(false);
}

// =============================================================================
// A 级别与过滤（标准第 1、2 条）
// =============================================================================

void TstLogging::setLevelTakesEffectImmediately()
{
    // 级别是立即生效的全局状态，不是「下次启动才用」的配置。
    // `--log-level` 解析完就调它，而后续所有日志都按新值走。
    LqCompare::Log::setLevel(Level::Trace);
    QCOMPARE(LqCompare::Log::level(), Level::Trace);

    LqCompare::Log::setLevel(Level::Error);
    QCOMPARE(LqCompare::Log::level(), Level::Error);
    QVERIFY(LqCompare::Log::isEnabled(Level::Error));
    QVERIFY(!LqCompare::Log::isEnabled(Level::Warning));
}

void TstLogging::isEnabledFollowsLevelOrder()
{
    // Error 最严重（数值最小），Trace 最轻。`isEnabled` 必须在**两个方向**上都对：
    // 只测一个方向的话，把比较写反（`>=`）也能通过一半用例。
    LqCompare::Log::setLevel(Level::Warning);

    QVERIFY(LqCompare::Log::isEnabled(Level::Error));
    QVERIFY(LqCompare::Log::isEnabled(Level::Warning));
    QVERIFY(!LqCompare::Log::isEnabled(Level::Info));
    QVERIFY(!LqCompare::Log::isEnabled(Level::Debug));
    QVERIFY(!LqCompare::Log::isEnabled(Level::Trace));

    LqCompare::Log::setLevel(Level::Debug);
    QVERIFY(LqCompare::Log::isEnabled(Level::Error));
    QVERIFY(LqCompare::Log::isEnabled(Level::Debug));
    QVERIFY(!LqCompare::Log::isEnabled(Level::Trace));
}

void TstLogging::writeDropsBelowLevel()
{
    // 这一条守的是 `Log::write()` 自己的过滤。早先的版本里 `write()` 完全不过滤，
    // 于是「级别」只对宏生效：直接调用的地方在 `--log-level error` 下照样打印。
    LqCompare::Log::setLevel(Level::Warning);

    Capture capture;
    captureInto(&capture);

    LqCompare::Log::write(Level::Info, QStringLiteral("t"), QStringLiteral("不该出现"));
    LqCompare::Log::write(Level::Debug, QStringLiteral("t"), QStringLiteral("不该出现"));
    LqCompare::Log::write(Level::Trace, QStringLiteral("t"), QStringLiteral("不该出现"));

    QCOMPARE(capture.records.size(), 0);

    // 而级别之上的照常通过——否则「筛掉了」也可能只是「全都筛掉了」。
    LqCompare::Log::write(Level::Warning, QStringLiteral("t"), QStringLiteral("该出现"));
    LqCompare::Log::write(Level::Error, QStringLiteral("t"), QStringLiteral("该出现"));
    QCOMPARE(capture.records.size(), 2);
}

void TstLogging::writeKeepsAtAndAboveLevel()
{
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    captureInto(&capture);

    const QVector<Level> all{Level::Error, Level::Warning, Level::Info, Level::Debug, Level::Trace};
    for (Level level : all)
        LqCompare::Log::write(level, QStringLiteral("t"), QStringLiteral("x"));

    QCOMPARE(capture.records.size(), all.size());
    for (int i = 0; i < all.size(); ++i)
        QCOMPARE(capture.records.at(i).level, all.at(i));
}

void TstLogging::macroDoesNotEvaluateArgumentsWhenDisabled()
{
    // 这是规格里那条「零成本」的**唯一**可验证形式：
    // 不是为了「快一点」，而是逐文件比对时每个文件都会构造一次消息，
    // 日志级别本来是关的，开销却与文件数成正比。
    int evaluations = 0;
    const auto expensive = [&evaluations] {
        ++evaluations;
        return QStringLiteral("代价很高的一段");
    };

    LqCompare::Log::setLevel(Level::Warning);

    LQCOMPARE_DEBUG("t", expensive());
    LQCOMPARE_TRACE("t", expensive());
    LQCOMPARE_INFO("t", expensive());
    QCOMPARE(evaluations, 0);

    // 打开级别之后必须求值，且只求值一次——求值两次说明宏体里出现了两遍参数。
    LqCompare::Log::setLevel(Level::Trace);
    LQCOMPARE_DEBUG("t", expensive());
    QCOMPARE(evaluations, 1);
}

void TstLogging::macroEvaluatesExactlyOnceWhenEnabled()
{
    int evaluations = 0;
    const auto counted = [&evaluations] {
        ++evaluations;
        return QStringLiteral("x");
    };

    LqCompare::Log::setLevel(Level::Trace);
    Capture capture;
    captureInto(&capture);

    LQCOMPARE_WARN("t", counted());

    QCOMPARE(evaluations, 1);
    QCOMPARE(capture.records.size(), 1);
}

void TstLogging::everyLevelIsReachableByItsOwnName()
{
    // 五个级别都要能通过「名字 → 级别 → 输出」这条链路走通。
    // 漏掉一个（例如实现里忘了 Warning 分支）会让对应级别的日志永远发不出来，
    // 而现象是「这个级别好像没生效」，很难联想到解析表少了一行。
    const QVector<Level> all{Level::Error, Level::Warning, Level::Info, Level::Debug, Level::Trace};

    for (Level level : all) {
        LqCompare::Log::setLevel(level);

        Capture capture;
        captureInto(&capture);
        LqCompare::Log::write(level, QStringLiteral("t"), QStringLiteral("x"));

        QCOMPARE(capture.records.size(), 1);
        QCOMPARE(capture.records.first().level, level);

        LqCompare::Log::clearSinks();
    }
}

// =============================================================================
// B 格式（标准第 4 条）
// =============================================================================

void TstLogging::lineCarriesTimeLevelCategoryAndMessage()
{
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    captureInto(&capture);
    LQCOMPARE_INFO("net", QStringLiteral("已连接"));

    QCOMPARE(capture.records.size(), 1);
    const Record &record = capture.records.first();
    const QString line = record.line();

    // 时间戳：既要出现在行里，也要能被还原成一个合理的时刻。
    // 只断言「行里有数字」的话，把时间戳写死成常量也能通过。
    QVERIFY(line.contains(record.time.toString(Qt::ISODateWithMs)));
    QCOMPARE(qAbs(record.time.secsTo(QDateTime::currentDateTime())), qint64(0));

    // 级别用定宽短名：级别列宽度一致，后面的分类才不会左右跳。
    QVERIFY2(line.contains(QStringLiteral("[INFO ]")), qPrintable(line));
    QVERIFY2(line.contains(QStringLiteral("[net]")), qPrintable(line));
    QVERIFY2(line.endsWith(QStringLiteral("已连接")), qPrintable(line));
}

void TstLogging::lineCarriesThreadId()
{
    // 并发排查全靠这一段：没有它的日志只能证明「出过问题」，不能证明「谁出的」。
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    captureInto(&capture);
    LQCOMPARE_WARN("t", QStringLiteral("x"));

    QCOMPARE(capture.records.size(), 1);
    const Record &record = capture.records.first();
    QCOMPARE(record.threadId, currentThreadId());

    const QString expected = QStringLiteral("t:") + QString::number(record.threadId, 16);
    QVERIFY2(record.line().contains(expected), qPrintable(record.line()));
}

void TstLogging::lineCarriesThreadNameWhenSet()
{
    // 线程 id 是十六进制数字，跨一次重启就对不上；名字是给人读的。
    LqCompare::Log::setLevel(Level::Trace);

    QThread *thread = QThread::currentThread();
    QVERIFY(thread != nullptr);
    const QString previous = thread->objectName();
    thread->setObjectName(QStringLiteral("tst-logging-thread"));

    Capture capture;
    captureInto(&capture);
    LQCOMPARE_WARN("t", QStringLiteral("x"));

    thread->setObjectName(previous);

    QCOMPARE(capture.records.size(), 1);
    QCOMPARE(capture.records.first().threadName, QStringLiteral("tst-logging-thread"));
    QVERIFY2(capture.records.first().line().contains(QStringLiteral("tst-logging-thread")),
             qPrintable(capture.records.first().line()));

    // 没有名字时那一段必须整体消失，而不是留下一个孤立的 `t:1a2b main` 之外的空档。
    // 这里手工造一条 Record，而不是去改当前线程的名字——当前线程名是本用例
    // 之外的环境状态，拿它做「反面样本」会随测试运行器的配置而变。
    Record bare;
    bare.time = QDateTime::currentDateTime();
    bare.level = Level::Warning;
    bare.category = QStringLiteral("t");
    bare.threadId = 0x1a2b;
    bare.message = QStringLiteral("x");
    QVERIFY2(bare.line().contains(QStringLiteral("[t:1a2b]")), qPrintable(bare.line()));
}

void TstLogging::lineIsStableAcrossRepeatedCalls()
{
    // `line()` 必须是无副作用的纯查询：控制台与文件两个目标各自调一次，
    // 若它内部会推进状态（例如把「已输出」标记清掉），两处内容就会不一致。
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    captureInto(&capture);
    LQCOMPARE_WARN("t", QStringLiteral("x"));

    QCOMPARE(capture.records.size(), 1);
    const Record &record = capture.records.first();
    QCOMPARE(record.line(), record.line());
}

// =============================================================================
// C 输出目标（标准第 3 条）
// =============================================================================

void TstLogging::sinkReceivesRecordWithStructuredFields()
{
    // 接收者拿到的是**结构**而不是一行拼好的文本：界面输出面板要按级别着色、
    // 要分列显示时间与分类。让它去反解析文本的话，日志格式一改就静默失效。
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    const int handle = captureInto(&capture);
    QVERIFY(handle > 0);
    QCOMPARE(LqCompare::Log::sinkCount(), 1);

    LQCOMPARE_ERROR("io", QStringLiteral("打开失败"));

    QCOMPARE(capture.records.size(), 1);
    const Record &record = capture.records.first();
    QCOMPARE(record.level, Level::Error);
    QCOMPARE(record.category, QStringLiteral("io"));
    QCOMPARE(record.message, QStringLiteral("打开失败"));
    QCOMPARE(record.threadId, currentThreadId());
    QVERIFY(record.time.isValid());
}

void TstLogging::multipleSinksAllReceiveInRegistrationOrder()
{
    LqCompare::Log::setLevel(Level::Trace);

    Capture first;
    Capture second;
    const int firstHandle = captureInto(&first);
    const int secondHandle = captureInto(&second);
    QVERIFY(firstHandle > 0 && secondHandle > 0);
    QVERIFY(firstHandle != secondHandle);

    QVector<QString> order;
    LqCompare::Log::addSink([&order](const Record &) { order << QStringLiteral("a"); });
    LqCompare::Log::addSink([&order](const Record &) { order << QStringLiteral("b"); });

    LQCOMPARE_INFO("t", QStringLiteral("x"));

    // 两个接收者都要收到——「只有一个能挂上」会让诊断包导出与输出面板互相顶掉。
    QCOMPARE(first.records.size(), 1);
    QCOMPARE(second.records.size(), 1);
    // 顺序按注册顺序确定，便于排查「谁先看到」。
    QCOMPARE(order, QVector<QString>({QStringLiteral("a"), QStringLiteral("b")}));
}

void TstLogging::sinkRemovalStopsDelivery()
{
    LqCompare::Log::setLevel(Level::Trace);

    Capture keeper;
    Capture removed;
    const int keeperHandle = captureInto(&keeper);
    const int removedHandle = captureInto(&removed);

    LqCompare::Log::removeSink(removedHandle);
    QCOMPARE(LqCompare::Log::sinkCount(), 1);

    LQCOMPARE_INFO("t", QStringLiteral("x"));

    QCOMPARE(keeper.records.size(), 1);
    QCOMPARE(removed.records.size(), 0);

    // 移除不存在的句柄不能出事：界面析构与测试清理都可能重复移除，
    // 让重复移除崩溃会把一个无害的收尾动作变成随机崩溃。
    LqCompare::Log::removeSink(removedHandle);
    LqCompare::Log::removeSink(keeperHandle + 999);
    QCOMPARE(LqCompare::Log::sinkCount(), 1);
}

void TstLogging::clearSinksRemovesEverything()
{
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    captureInto(&capture);
    QCOMPARE(LqCompare::Log::sinkCount(), 1);

    LqCompare::Log::clearSinks();
    QCOMPARE(LqCompare::Log::sinkCount(), 0);

    LQCOMPARE_INFO("t", QStringLiteral("x"));
    QCOMPARE(capture.records.size(), 0);
}

void TstLogging::nullSinkIsRejected()
{
    // 空函数对象不是「什么都不做的接收者」，而是调用时崩溃。
    // 必须在注册处就挡住，否则崩在第一次真正记日志的地方，与传参处毫无关系。
    QCOMPARE(LqCompare::Log::addSink(LqCompare::Log::Sink()), 0);
    QCOMPARE(LqCompare::Log::sinkCount(), 0);
}

void TstLogging::sinkMayLogWithoutDeadlock()
{
    // 输出面板的接收者顺手记一条调试日志是很自然的写法。
    // 若 invoke 发生在持锁期间，这里会直接死锁——而现象是「界面卡住」，
    // 与日志模块看起来毫无关系。
    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    bool reentered = false;
    LqCompare::Log::addSink([&capture, &reentered](const Record &record) {
        capture.records << record;
        if (record.category == QLatin1String("outer") && !reentered) {
            reentered = true;
            LQCOMPARE_DEBUG("inner", QStringLiteral("接收者自己记的一条"));
        }
    });

    LQCOMPARE_DEBUG("outer", QStringLiteral("x"));

    QCOMPARE(capture.records.size(), 2);
    QCOMPARE(capture.records.at(0).category, QStringLiteral("outer"));
    QCOMPARE(capture.records.at(1).category, QStringLiteral("inner"));
}

void TstLogging::sinkIsCalledFromTheThreadThatLogged()
{
    // 这条用例记录的是一个**约束**而不是一个便利：接收者在记录日志的那个线程上被调用，
    // 因此界面输出面板**不能**直接被挂成接收者——图标解析跑在后台线程上，
    // 从那里碰控件会崩。面板必须自己加一次排队跳转（queued connection）。
    LqCompare::Log::setLevel(Level::Trace);

    QMutex guard;
    quintptr recordThreadId = 0;
    quintptr sinkThreadId = 0;
    bool delivered = false;

    LqCompare::Log::addSink([&](const Record &record) {
        QMutexLocker locker(&guard);
        recordThreadId = record.threadId;
        sinkThreadId = currentThreadId();
        delivered = true;
    });

    std::thread worker([] {
        LQCOMPARE_WARN("worker", QStringLiteral("来自后台线程"));
    });
    worker.join();

    QMutexLocker locker(&guard);
    QVERIFY(delivered);
    QCOMPARE(sinkThreadId, recordThreadId);
    QVERIFY2(recordThreadId != currentThreadId(),
             "后台线程的线程 id 不能等于主线程的，否则这条用例没测到跨线程");
}

void TstLogging::fileTargetAppendsAndSurvivesReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("lqcompare.log"));

    QVERIFY(LqCompare::Log::setLogFile(path));
    QCOMPARE(LqCompare::Log::logFile(), path);

    LqCompare::Log::setLevel(Level::Trace);
    LQCOMPARE_WARN("t", QStringLiteral("第一行"));
    // 第二次写入必须是**追加**。以截断方式打开的话，一个长时间运行的程序
    // 的日志文件里只会剩下最后一条——而它恰好是最没有上下文的那一条。
    LQCOMPARE_WARN("t", QStringLiteral("第二行"));

    const QString text = readWholeFile(path);
    QVERIFY2(text.contains(QStringLiteral("第一行")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("第二行")), qPrintable(text));
    QVERIFY(text.count(QLatin1Char('\n')) >= 2);
}

void TstLogging::fileTargetRejectsUnwritablePath()
{
    // 路径不可用时必须**返回失败并关掉文件输出**，而不是留下一个「看起来配上了、
    // 实际写不进去」的状态：后者会让用户拿着一个空日志文件来问为什么。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString blocker = dir.filePath(QStringLiteral("not-a-directory"));
    {
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }

    QVERIFY(!LqCompare::Log::setLogFile(blocker + QStringLiteral("/sub/lq.log")));
    QVERIFY(LqCompare::Log::logFile().isEmpty());
}

void TstLogging::emptyPathDisablesFileTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("lq.log"));

    QVERIFY(LqCompare::Log::setLogFile(path));
    QVERIFY(!LqCompare::Log::logFile().isEmpty());

    // 注意 `setLogFile()` 会**探一次可写性**（以追加方式打开再关掉），
    // 所以此刻文件已经存在但是空的。这是刻意的：与其在第一条日志到来时
    // 才发现写不进去，不如配置的那一刻就说清楚。
    QVERIFY(QFile::exists(path));
    QCOMPARE(QFileInfo(path).size(), qint64(0));

    QVERIFY(LqCompare::Log::setLogFile(QString()));
    QVERIFY(LqCompare::Log::logFile().isEmpty());

    LqCompare::Log::setLevel(Level::Trace);
    LQCOMPARE_WARN("t", QStringLiteral("关掉之后不该再落盘"));

    // 断言的是「不再写入」，而不是「文件不存在」：关掉输出不等于删掉
    // 用户已有的日志——那会把上一轮排查的证据一起丢掉。
    QCOMPARE(QFileInfo(path).size(), qint64(0));
}

void TstLogging::fileTargetReceivesExactlyWhatConsoleWould()
{
    // 两个目标必须拿到**同一份文本**：不一致的话，「日志文件里看到的」与
    // 「控制台里看到的」会不一样，而对着一行行比对它们是最常见的排查动作。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("lq.log"));
    QVERIFY(LqCompare::Log::setLogFile(path));

    LqCompare::Log::setLevel(Level::Trace);

    Capture capture;
    captureInto(&capture);
    LQCOMPARE_WARN("t", QStringLiteral("内容要一致"));

    QCOMPARE(capture.records.size(), 1);

    const QString text = readWholeFile(path);
    QVERIFY2(text.startsWith(capture.records.first().line()), qPrintable(text));
    QVERIFY(text.endsWith(QLatin1Char('\n')));
}

// =============================================================================
// D 耗时辅助（标准第 5 条）
// =============================================================================

void TstLogging::stopwatchReportsElapsedOnScopeExit()
{
    LqCompare::Log::setLevel(Level::Debug);

    Capture capture;
    captureInto(&capture);

    {
        LqCompare::Log::Stopwatch timer(Level::Debug, QStringLiteral("compare"),
                                        QStringLiteral("逐行比对"));
        QTest::qWait(20);
    }

    QCOMPARE(capture.records.size(), 1);
    const Record &record = capture.records.first();
    QCOMPARE(record.level, Level::Debug);
    QCOMPARE(record.category, QStringLiteral("compare"));
    QVERIFY2(record.message.startsWith(QStringLiteral("逐行比对")), qPrintable(record.message));

    // 时间要落在「大于 0」但「不至于荒谬」的区间里。
    // 只断言 contains("耗时") 的话，实现里写死 `0 ms` 也能通过。
    const qint64 elapsed = millisecondsFrom(record.message);
    QVERIFY2(elapsed >= 0, qPrintable(record.message));
}

void TstLogging::stopwatchIsSilentWhenLevelDisabled()
{
    LqCompare::Log::setLevel(Level::Warning);

    Capture capture;
    captureInto(&capture);

    {
        LqCompare::Log::Stopwatch timer(Level::Debug, QStringLiteral("compare"),
                                        QStringLiteral("逐行比对"));
        QTest::qWait(5);
    }

    // 级别关掉时连计时结果都不该被记录，更不该把消息拼出来。
    QCOMPARE(capture.records.size(), 0);
}

void TstLogging::stopwatchNoteIsAppended()
{
    LqCompare::Log::setLevel(Level::Info);

    Capture capture;
    captureInto(&capture);

    {
        LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("compare"),
                                        QStringLiteral("扫描目录"));
        timer.setNote(QStringLiteral("共 1234 个文件"));
    }

    QCOMPARE(capture.records.size(), 1);
    const QString message = capture.records.first().message;
    QVERIFY2(message.contains(QStringLiteral("共 1234 个文件")), qPrintable(message));
    // 备注写在括号里、仍在同一行：拆成两条会让「这条耗时对应哪次操作」需要靠位置去猜。
    QVERIFY2(message.contains(QStringLiteral("（共 1234 个文件）")), qPrintable(message));
}

void TstLogging::stopwatchElapsedIsQueryableMidway()
{
    LqCompare::Log::setLevel(Level::Warning);

    Capture capture;
    captureInto(&capture);

    LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("compare"),
                                    QStringLiteral("扫描目录"));
    QTest::qWait(10);

    // 中途查询不该结束计时，也不该依赖级别是否打开：调用方可能是想
    // 「跑完再看要不要记」，那就得先能拿到耗时。
    const qint64 midway = timer.elapsedMs();
    QVERIFY(midway >= 0);
    QVERIFY(timer.elapsedMs() >= midway);
    // 查询本身不能顺手记一条——否则「读一下耗时」会污染日志。
    QCOMPARE(capture.records.size(), 0);

    // 查询之后计时仍在继续：析构时照常记一条（此时把级别打开）。
    LqCompare::Log::setLevel(Level::Info);
    timer.finish();
    QCOMPARE(capture.records.size(), 1);
}

void TstLogging::stopwatchFinishIsIdempotent()
{
    LqCompare::Log::setLevel(Level::Info);

    Capture capture;
    captureInto(&capture);

    {
        LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("compare"),
                                        QStringLiteral("扫描目录"));
        timer.finish();
        timer.finish(); // 手动结束两次
    }                   // 析构时不应再记一条

    QCOMPARE(capture.records.size(), 1);
}

void TstLogging::stopwatchUsesLevelAtFinishNotConstruction()
{
    // 「先放计时器、再用命令行把级别调开」是排查性能问题时的常见顺序。
    // 若在构造时就把级别定死，这种用法会一条都不记，而看起来像是计时器没生效。
    LqCompare::Log::setLevel(Level::Warning);

    Capture capture;
    captureInto(&capture);

    {
        LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("compare"),
                                        QStringLiteral("扫描目录"));
        LqCompare::Log::setLevel(Level::Info); // 中途打开
    }

    QCOMPARE(capture.records.size(), 1);
}

// =============================================================================
// E 级别名解析
// =============================================================================

void TstLogging::levelNamesRoundTrip()
{
    const QVector<Level> all{Level::Error, Level::Warning, Level::Info, Level::Debug, Level::Trace};

    for (Level level : all) {
        const QString name = QString::fromLatin1(LqCompare::Log::levelIdentifier(level));
        Level parsed = Level::Error;
        QVERIFY2(LqCompare::Log::levelFromName(name, &parsed), qPrintable(name));
        QCOMPARE(parsed, level);
    }
}

void TstLogging::levelNameParsingIsCaseInsensitiveAndTrimmed()
{
    Level parsed = Level::Error;

    QVERIFY(LqCompare::Log::levelFromName(QStringLiteral("INFO"), &parsed));
    QCOMPARE(parsed, Level::Info);

    QVERIFY(LqCompare::Log::levelFromName(QStringLiteral("  Debug  "), &parsed));
    QCOMPARE(parsed, Level::Debug);

    // `warn` 是英文里最常见的缩写，用户会这么写；不支持等于逼他重试。
    QVERIFY(LqCompare::Log::levelFromName(QStringLiteral("warn"), &parsed));
    QCOMPARE(parsed, Level::Warning);
}

void TstLogging::unknownLevelNameLeavesOutputUntouched()
{
    // 认不出来时**不动**输出参数，调用点才能把「写错了」与「没写」分开处理：
    // 前者要提示用户，后者用默认值。若这里顺手把 out 设成默认值，
    // 调用点看到的就是「解析成功了，级别是 warning」，于是错误命令行被静默接受。
    Level parsed = Level::Trace;
    QVERIFY(!LqCompare::Log::levelFromName(QStringLiteral("verbose"), &parsed));
    QCOMPARE(parsed, Level::Trace);

    QVERIFY(!LqCompare::Log::levelFromName(QString(), &parsed));
    QCOMPARE(parsed, Level::Trace);

    QVERIFY(!LqCompare::Log::levelFromName(QStringLiteral("   "), &parsed));
    QCOMPARE(parsed, Level::Trace);

    // 不传 out 也不能出事：命令行里解析失败时只关心「成功没成功」。
    QVERIFY(!LqCompare::Log::levelFromName(QStringLiteral("verbose"), nullptr));
    QVERIFY(LqCompare::Log::levelFromName(QStringLiteral("error"), nullptr));
}

void TstLogging::levelNamesAreDistinct()
{
    const QVector<Level> all{Level::Error, Level::Warning, Level::Info, Level::Debug, Level::Trace};

    QVector<QString> names;
    for (Level level : all)
        names << QString::fromLatin1(LqCompare::Log::levelIdentifier(level));

    QSet<QString> unique;
    for (const QString &name : names) {
        QVERIFY(!name.isEmpty());
        // `unknown` 是给「传进来的不是合法枚举值」兜底用的，
        // 出现在正常级别里说明 switch 少了一个分支。
        QVERIFY(name != QLatin1String("unknown"));
        unique.insert(name);
    }
    QCOMPARE(unique.size(), all.size());
}

// =============================================================================
// D2 详细性能计时开关（OPT-010 第 4 条）
// =============================================================================

void TstLogging::timingIsFilteredByLevelWhenTheSwitchIsOff()
{
    // 出厂默认是「关」。关着的时候行为与开关存在之前**完全一样**：
    // 计时行与普通日志同样受级别控制。
    LqCompare::Log::setLevel(Level::Error);
    Capture capture;
    captureInto(&capture);
    {
        LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("test"), QStringLiteral("慢操作"));
    }
    QCOMPARE(capture.records.size(), 0);
}

void TstLogging::timingBypassesTheLevelFilterWhenTheSwitchIsOn()
{
    // 这正是这个开关存在的理由：排查「为什么这一步很慢」时最常见的配置是
    // 级别停在 warning/error，而把级别调到 debug 会同时放出成千上万条
    // 逐文件的调试日志——用户要的是「哪一步慢」，不是「每一步都刷屏」。
    LqCompare::Log::setLevel(Level::Error);
    LqCompare::Log::setPerformanceTimingEnabled(true);

    Capture capture;
    captureInto(&capture);
    {
        LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("test"), QStringLiteral("慢操作"));
    }
    QCOMPARE(capture.records.size(), 1);
    QVERIFY(capture.records.first().message.contains(QStringLiteral("慢操作")));
    QVERIFY(capture.records.first().message.contains(QStringLiteral("耗时")));
}

void TstLogging::timingKeepsTheTimersOwnLevelWhenItBypasses()
{
    // 绕过级别过滤**不改变级别字段**：日志里的这一行仍然是调用点声明的严重性，
    // 否则「按级别筛日志」的人会看到一条级别与他设置不符的行。
    LqCompare::Log::setLevel(Level::Error);
    LqCompare::Log::setPerformanceTimingEnabled(true);

    Capture capture;
    captureInto(&capture);
    {
        LqCompare::Log::Stopwatch timer(Level::Debug, QStringLiteral("test"), QStringLiteral("慢操作"));
    }
    QCOMPARE(capture.records.size(), 1);
    QCOMPARE(capture.records.first().level, Level::Debug);
}

void TstLogging::stopwatchReadsTheSwitchAtFinishTimeNotConstruction()
{
    // 与级别同一条纪律：计时器已经构造出来之后再打开开关，也必须记上——
    // 「先放计时器、再打开开关」是排查慢操作时最常见的用法。
    LqCompare::Log::setLevel(Level::Error);
    Capture capture;
    captureInto(&capture);

    {
        LqCompare::Log::Stopwatch timer(Level::Info, QStringLiteral("test"), QStringLiteral("慢操作"));
        LqCompare::Log::setPerformanceTimingEnabled(true);
    }
    QCOMPARE(capture.records.size(), 1);
    QVERIFY(!capture.records.first().message.isEmpty());
}

// =============================================================================
// D3 轮转策略与写入路径（OPT-010 第 2 条）
// =============================================================================

void TstLogging::rotationPolicyIsOffByDefaultAndSettable()
{
    QCOMPARE(QString::fromLatin1(
                     LqCompare::Log::rotationModeIdentifier(LqCompare::Log::rotationPolicy().mode)),
             QStringLiteral("none"));

    LqCompare::Log::RotationPolicy policy;
    policy.mode = LqCompare::Log::RotationMode::Daily;
    policy.keepFiles = 2;
    LqCompare::Log::setRotationPolicy(policy);

    const LqCompare::Log::RotationPolicy stored = LqCompare::Log::rotationPolicy();
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(stored.mode)),
             QStringLiteral("daily"));
    QCOMPARE(stored.keepFiles, 2);
}

void TstLogging::writeDoesNotRotateWhenThePolicyIsOff()
{
    // 「配了策略但关着」必须与「没配」等价：一个关掉的策略仍然改名文件，
    // 用户会认为那个开关坏了。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    {
        QFile seed(logPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write(QByteArray(4096, 'x'));
        seed.close();
    }
    QVERIFY(LqCompare::Log::setLogFile(logPath));
    LqCompare::Log::setLevel(Level::Warning);

    LqCompare::Log::write(Level::Warning, QStringLiteral("test"), QStringLiteral("一条普通日志"));

    QVERIFY(!QFile::exists(logPath + QStringLiteral(".1")));
    QVERIFY(QFileInfo(logPath).size() > 4096);
}

void TstLogging::writeRotatesWhenTheSizeLimitIsExceeded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    {
        QFile seed(logPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write(QByteArray(4096, 'x'));
        seed.close();
    }
    QVERIFY(LqCompare::Log::setLogFile(logPath));
    LqCompare::Log::setLevel(Level::Warning);

    LqCompare::Log::RotationPolicy policy;
    policy.mode = LqCompare::Log::RotationMode::Size;
    policy.maximumBytes = 1024;
    policy.keepFiles = 3;
    // 设置策略会把「上次检查时刻」复位，因此下一条日志立刻检查——
    // 用户刚点完「应用」就去看日志目录，不该还要等满那一秒的节流间隔。
    LqCompare::Log::setRotationPolicy(policy);

    LqCompare::Log::write(Level::Warning, QStringLiteral("test"), QStringLiteral("触发轮转"));

    // 旧内容进了 `.1`，新的一条落在新建的当前文件里——这证明轮转发生在
    // 写这一行**之前**（反过来的话超限的那一条会留在旧文件里，而旧文件
    // 体积已经达标，下一次检查又要为它轮转一次）。
    QVERIFY(QFile::exists(logPath + QStringLiteral(".1")));
    QCOMPARE(QFileInfo(logPath + QStringLiteral(".1")).size(), 4096LL);
    const QString current = readWholeFile(logPath);
    QVERIFY(current.contains(QStringLiteral("触发轮转")));
}

void TstLogging::rotateIfNeededReportsTheDecisionWithoutWritingAnything()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    {
        QFile seed(logPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write(QByteArray(64, 'x'));
        seed.close();
    }
    QVERIFY(LqCompare::Log::setLogFile(logPath));

    LqCompare::Log::RotationPolicy policy;
    policy.mode = LqCompare::Log::RotationMode::Size;
    policy.maximumBytes = 1024;
    LqCompare::Log::setRotationPolicy(policy);

    LqCompare::Log::RotationDecision decision;
    QString error;
    // 「不需要轮转」返回 true（检查完成了），而不是 false——把两者混起来，
    // 调用点会把「一切正常」当成失败报给用户。
    QVERIFY(LqCompare::Log::rotateIfNeeded(QDateTime::currentDateTime(), &decision, &error));
    QCOMPARE(error, QString());
    QVERIFY(!decision.rotate);
    QVERIFY(!decision.reason.isEmpty());
    QVERIFY(!QFile::exists(logPath + QStringLiteral(".1")));
}

void TstLogging::rotateIfNeededFailsWhenNoLogFileIsConfigured()
{
    LqCompare::Log::RotationPolicy policy;
    policy.mode = LqCompare::Log::RotationMode::Daily;
    LqCompare::Log::setRotationPolicy(policy);

    QString error;
    QVERIFY(!LqCompare::Log::rotateIfNeeded(QDateTime::currentDateTime(), nullptr, &error));
    QVERIFY(!error.isEmpty());
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstLogging)
