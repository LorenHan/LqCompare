#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

#include <cstdio>
#include <utility>

namespace LqCompare {
namespace Log {

namespace {

QMutex &mutex()
{
    static QMutex instance;
    return instance;
}

Level &currentLevel()
{
    static Level level = Level::Warning;
    return level;
}

QString &logFilePath()
{
    static QString path;
    return path;
}

/// 轮转策略。**只能在持有互斥量时访问**（写入路径自己就在临界区里，
/// 再进去一次会自锁死——`QMutex` 默认不可重入）。
RotationPolicy &rotationPolicyRef()
{
    static RotationPolicy policy;
    return policy;
}

/// 详细性能计时开关。同样只能在持有互斥量时访问。
bool &performanceTimingEnabledRef()
{
    static bool enabled = false;
    return enabled;
}

/// 轮转检查的节流时钟。
///
/// 为什么需要节流：`write()` 是逐文件比对时最热的路径之一（每条调试日志都会进来），
/// 而「日志有没有超过上限」要问一次文件系统。每条日志都 `stat` 一次，
/// 代价与日志条数成正比，而这个代价是**为了给日志瘦身**才付的，很荒唐。
/// 1 秒一次的粒度足够：超过上限之后最多多写 1 秒的日志。
QElapsedTimer &rotationClock()
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    return clock;
}

/// 上一次轮转检查的时刻（毫秒）。-1 表示「还没查过」，即下一次写入立刻检查。
qint64 &lastRotationCheckMs()
{
    static qint64 value = -1;
    return value;
}

constexpr qint64 kRotationCheckIntervalMs = 1000;

/// 句柄 → 接收者。用 `QMap` 而不是 `QHash` 是为了让调用顺序按句柄（也就是
/// 注册顺序）确定：面板与诊断包导出同时挂着时，「谁先看到这条日志」不该是随机的。
QMap<int, Sink> &sinks()
{
    static QMap<int, Sink> instance;
    return instance;
}

int &lastSinkHandle()
{
    static int handle = 0;
    return handle;
}

/// 输出行里那一段定宽的大写短名。
///
/// 定宽（`"INFO "` 与 `"WARN "` 都占 5 格）是为了让 `[级别]` 之后的分类对齐：
/// 日志是给人竖着扫的，级别列宽不固定时分类会左右跳。
const char *levelTag(Level level)
{
    switch (level) {
    case Level::Error:
        return "ERROR";
    case Level::Warning:
        return "WARN ";
    case Level::Info:
        return "INFO ";
    case Level::Debug:
        return "DEBUG";
    case Level::Trace:
        return "TRACE";
    }
    return "?????";
}

/// 当前线程名（`QThread` 对象名）。主线程之外大多为空。
///
/// `QThread::currentThread()` 对不是由 Qt 创建的线程也会返回一个对象
/// （Qt 会为它建一个 adopted QThread），因此这里不需要额外判断。
QString currentThreadName()
{
    const QThread *thread = QThread::currentThread();
    return thread != nullptr ? thread->objectName() : QString();
}

///
/// \brief 真正落盘与投递的那一段。**级别过滤不在这里。**
///
/// 两个入口（`write()` 与 `writeTiming()`）的过滤条件不同，把过滤挪进来
/// 迟早会有人按其中一个条件去改它，另一个入口的语义就跟着变了。
///
void appendRecord(Level levelValue, const QString &category, const QString &message)
{
    // 结构和文本先在这里定下来，再进临界区。
    // 时钟读取与 `QThread::currentThread()` 都不需要持锁，放进去只会
    // 让别的线程多等一会儿。
    Record record;
    record.time = QDateTime::currentDateTime();
    record.level = levelValue;
    record.category = category;
    record.threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    record.threadName = currentThreadName();
    record.message = message;

    const QString text = record.line();

    // 接收者清单在锁里**拷贝**一份，出了临界区再逐个调用。两个理由：
    //   1. 接收者里写日志（输出面板顺手记一条调试日志）不会自锁死；
    //   2. 接收者在回调里 addSink/removeSink 不会让容器在遍历中被改动。
    // 代价是并发移除时那个接收者可能还会收到这一条——比死锁好得多，
    // 而且「移除之后不再收到」本来也不是日志接收者需要保证的事。
    QMap<int, Sink> currentSinks;

    {
        QMutexLocker locker(&mutex());
        currentSinks = sinks();

        // 轮转检查放在**写这一行之前**：先轮转再写，这一条日志才会落到新文件里。
        // 反过来做会让「压垮上限的那一条」留在旧文件里，而旧文件的体积已经达标，
        // 下一次检查又要为它轮转一次。
        //
        // 失败在这里**不记日志**：本函数正持着互斥量，再调 write() 会自锁死
        // （`QMutex` 默认不可重入）。显式入口 `rotateIfNeeded()` 拿得到 error，
        // 由调用方报到界面上——那也是更该看到它的地方。
        if (!logFilePath().isEmpty() && rotationPolicyRef().active()) {
            const qint64 nowMs = rotationClock().elapsed();
            if (lastRotationCheckMs() < 0
                || nowMs - lastRotationCheckMs() >= kRotationCheckIntervalMs) {
                lastRotationCheckMs() = nowMs;
                applyLogRotation(logFilePath(), rotationPolicyRef(),
                                 QDateTime::currentDateTime(), nullptr, nullptr);
            }
        }

        std::fputs(qPrintable(text), stderr);
        std::fputc('\n', stderr);

        if (!logFilePath().isEmpty()) {
            QFile file(logFilePath());
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream stream(&file);
                stream << text << '\n';
            }
        }
    }

    for (auto it = currentSinks.constBegin(); it != currentSinks.constEnd(); ++it)
        it.value()(record);
}

} // namespace

void setLevel(Level levelValue)
{
    QMutexLocker locker(&mutex());
    currentLevel() = levelValue;
}

Level level()
{
    QMutexLocker locker(&mutex());
    return currentLevel();
}

bool isEnabled(Level levelValue)
{
    QMutexLocker locker(&mutex());
    return levelValue <= currentLevel();
}

const char *levelIdentifier(Level levelValue)
{
    switch (levelValue) {
    case Level::Error:
        return "error";
    case Level::Warning:
        return "warning";
    case Level::Info:
        return "info";
    case Level::Debug:
        return "debug";
    case Level::Trace:
        return "trace";
    }
    return "unknown";
}

bool levelFromName(const QString &name, Level *out)
{
    const QString trimmed = name.trimmed().toLower();
    if (trimmed.isEmpty())
        return false;

    Level parsed = Level::Warning;
    if (trimmed == QLatin1String("error")) {
        parsed = Level::Error;
    } else if (trimmed == QLatin1String("warning") || trimmed == QLatin1String("warn")) {
        parsed = Level::Warning;
    } else if (trimmed == QLatin1String("info")) {
        parsed = Level::Info;
    } else if (trimmed == QLatin1String("debug")) {
        parsed = Level::Debug;
    } else if (trimmed == QLatin1String("trace")) {
        parsed = Level::Trace;
    } else {
        // 认不出来时**不动** out：调用点于是能把「写错了」与「没写」分开处理。
        return false;
    }

    if (out != nullptr)
        *out = parsed;
    return true;
}

bool setLogFile(const QString &filePath)
{
    QMutexLocker locker(&mutex());
    logFilePath() = filePath;
    // 换了目标文件就要重新判断一次轮转：新路径上可能已经躺着一份超限的旧日志
    // （上一轮的行程留下的），而复用别的文件留下的节流时刻会让它一直到
    // 下一秒才被处理。
    lastRotationCheckMs() = -1;
    if (filePath.isEmpty()) {
        return true;
    }
    const QFileInfo info(filePath);
    QDir directory = info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        logFilePath().clear();
        return false;
    }
    QFile probe(filePath);
    if (!probe.open(QIODevice::Append | QIODevice::Text)) {
        logFilePath().clear();
        return false;
    }
    probe.close();
    return true;
}

QString logFile()
{
    QMutexLocker locker(&mutex());
    return logFilePath();
}

QString Record::line() const
{
    // `[t:7f9a1c]` 里的 `t:` 前缀不是为了好看：线程名是可选的，
    // 没有前缀时 `[7f9a1c main]` 与「分类里带了个空格」从文本上分不开。
    QString thread = QStringLiteral("t:") + QString::number(threadId, 16);
    if (!threadName.isEmpty())
        thread += QLatin1Char(' ') + threadName;

    return QStringLiteral("%1 [%2] [%3] [%4] %5")
            .arg(time.toString(Qt::ISODateWithMs),
                 QString::fromLatin1(levelTag(level)), category, thread, message);
}

int addSink(Sink sink)
{
    if (!sink) {
        // 空函数对象不是「一个什么都不做的接收者」，而是调用时崩溃。
        // 返回 0（无效句柄）让调用点能看出自己传错了，而不是等到某条日志
        // 恰好触发时崩在一个与日志内容无关的地方。
        return 0;
    }

    QMutexLocker locker(&mutex());
    const int handle = ++lastSinkHandle();
    sinks().insert(handle, std::move(sink));
    return handle;
}

void removeSink(int handle)
{
    QMutexLocker locker(&mutex());
    sinks().remove(handle);
}

void clearSinks()
{
    QMutexLocker locker(&mutex());
    sinks().clear();
}

int sinkCount()
{
    QMutexLocker locker(&mutex());
    return sinks().size();
}

void write(Level levelValue, const QString &category, const QString &message)
{
    if (!isEnabled(levelValue))
        return;
    appendRecord(levelValue, category, message);
}

void writeTiming(Level levelValue, const QString &category, const QString &message)
{
    // 短路是刻意的：详细性能计时开关打开时，连级别判断都不做——
    // 级别恰好也被打开（比如正在跑 debug）时不该多一次加锁。
    if (!performanceTimingEnabled() && !isEnabled(levelValue))
        return;
    appendRecord(levelValue, category, message);
}

QString performanceTimingKey()
{
    return QStringLiteral("logging.performanceTiming");
}

void setPerformanceTimingEnabled(bool enabled)
{
    QMutexLocker locker(&mutex());
    performanceTimingEnabledRef() = enabled;
}

bool performanceTimingEnabled()
{
    QMutexLocker locker(&mutex());
    return performanceTimingEnabledRef();
}

void setRotationPolicy(const RotationPolicy &policy)
{
    QMutexLocker locker(&mutex());
    rotationPolicyRef() = policy;
    // 新策略立刻生效：把「上次检查时刻」复位，下一次写入就会检查，
    // 而不是还要等满那一秒的节流间隔。用户刚点完「应用」就去看日志文件，
    // 如果什么都没发生，他只会以为设置没生效。
    lastRotationCheckMs() = -1;
}

RotationPolicy rotationPolicy()
{
    QMutexLocker locker(&mutex());
    return rotationPolicyRef();
}

bool rotateIfNeeded(const QDateTime &now, RotationDecision *decision, QString *error)
{
    QMutexLocker locker(&mutex());
    if (decision != nullptr)
        *decision = RotationDecision();
    if (error != nullptr)
        error->clear();
    // 记下这次检查的时刻：否则紧随其后的第一条日志会再查一次同一个文件。
    lastRotationCheckMs() = rotationClock().elapsed();
    return applyLogRotation(logFilePath(), rotationPolicyRef(), now, decision, error);
}

Stopwatch::Stopwatch(Level levelValue, QString category, QString what)
    : m_level(levelValue), m_category(std::move(category)), m_what(std::move(what))
{
    // 计时无条件开始：`QElapsedTimer::start()` 只是一次时钟读取，
    // 而「到底慢不慢」不该取决于日志级别——排查性能问题时最常见的写法
    // 是先在代码里放计时器、再用命令行调级别看结果。
    m_timer.start();
}

Stopwatch::~Stopwatch()
{
    finish();
}

qint64 Stopwatch::elapsedMs() const
{
    return m_timer.elapsed();
}

void Stopwatch::setNote(const QString &note)
{
    m_note = note;
}

void Stopwatch::finish()
{
    if (m_finished)
        return;
    m_finished = true;

    QString text = QStringLiteral("%1 耗时 %2 ms").arg(m_what).arg(m_timer.elapsed());
    if (!m_note.isEmpty())
        text += QStringLiteral("（%1）").arg(m_note);

    // 「要不要记」放在这里、而不是构造时判断：中途调过 setLevel() 或
    // 详细性能计时开关也按新值走，否则「先构造计时器、再打开开关」这种写法
    // 会静默地一条都不记。过滤本身在 writeTiming() 里（那里也是唯一的实现）。
    writeTiming(m_level, m_category, text);
}

} // namespace Log
} // namespace LqCompare
