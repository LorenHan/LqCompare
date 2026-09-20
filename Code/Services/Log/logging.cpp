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

    // 级别判断放在析构/结束时，而不是构造时：中途调过 setLevel() 也按新级别走，
    // 否则「先构造计时器、再打开调试级别」这种用法会静默地一条都不记。
    if (!isEnabled(m_level))
        return;

    QString text = QStringLiteral("%1 耗时 %2 ms").arg(m_what).arg(m_timer.elapsed());
    if (!m_note.isEmpty())
        text += QStringLiteral("（%1）").arg(m_note);

    write(m_level, m_category, text);
}

} // namespace Log
} // namespace LqCompare
