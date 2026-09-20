#ifndef LQCOMPARE_LOGGING_H
#define LQCOMPARE_LOGGING_H

#include "logfiles.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QString>

#include <functional>

namespace LqCompare {
namespace Log {

///
/// 分级日志（PRD: ENG-006）。
///
/// 设计约束：级别未启用时**不得求值参数**，因此调用点必须使用
/// LQCOMPARE_DEBUG() 之类的宏，而不是 Log::write(Level::Debug, expensiveToString())。
///
/// 这条约束是硬的：逐文件比对时会为每个文件记一条调试日志，
/// 而「先拼好字符串再丢掉」的代价与文件数成正比——日志级别本来是关的，
/// 开销却照样发生。
///
enum class Level {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
    Trace = 4,
};

/// 使 Level 可以按数值比较（Error 最严重，数值最小）。
inline int levelValue(Level level)
{
    return static_cast<int>(level);
}

/// 设置当前级别；低于该级别的日志被丢弃。
void setLevel(Level level);
Level level();

///
/// \brief 该级别当前是否会产生输出。
///
/// 用于在拼字符串**之前**做判断，也是 `write()` 自己的过滤依据。
/// 有了它，需要自己拼消息的调用点（不能直接用宏的地方）可以写成
/// `if (isEnabled(Level::Debug)) { … }`，而不是把整段计算做完再丢掉。
///
bool isEnabled(Level level);

///
/// \brief 级别的规范标识（小写）：`"error"` / `"warning"` / `"info"` / `"debug"` / `"trace"`。
///
/// 与 `Record::line()` 里那个带填充的大写短名（`"WARN "`）刻意分开：
/// 前者用于**配置与回显**（命令行参数、设置界面），后者用于**对齐输出**。
/// 同一个函数兼两个用途的话，命令行就得接受带空格的 `"WARN "`，没人会猜得到。
///
const char *levelIdentifier(Level level);

///
/// \brief 从名字解析级别，大小写不敏感。
///
/// 解析失败时返回 false 且**不修改** `out`——调用点于是可以给 `out` 一个默认值，
/// 然后把「用户写错了」与「用户没写」当成两件事处理（前者要提示，后者用默认值）。
/// 参数解析写在 main.cpp 里曾导致过第二份事实来源：加了新级别之后命令行不认，
/// 而错误提示还是「未知的日志级别」，看不出其实是没同步。
///
bool levelFromName(const QString &name, Level *out);

/// 把日志同时写入文件（追加）。传空字符串表示关闭文件输出。
bool setLogFile(const QString &filePath);
QString logFile();

///
/// \brief 日志文件的轮转策略（PRD: OPT-010 第 2 条）。
///
/// 策略住在日志模块里，而不是只住在设置页里：轮转必须**在写入路径上自动发生**。
/// 只在用户点「应用」的那一刻执行一次的「轮转」不是轮转，那只是启动时删了一次文件。
///
/// 代价是多了一份全局状态（与级别、日志文件路径同类），因此 `Tests/Logging`
/// 的 `init()` 也必须把它复位——否则上一个用例留下的策略会让下一个用例的
/// 日志文件在自己没察觉的情况下被改名。
///
void setRotationPolicy(const RotationPolicy &policy);
RotationPolicy rotationPolicy();

///
/// \brief 按策略立刻检查并执行一次轮转（`now` 可显式传入，便于测试跨天）。
///
/// 写入路径自己每满 1 秒最多检查一次（见 logging.cpp 里的理由），本函数是
/// 「马上查一次」的显式入口：把日志文件换到新路径之后调它，新路径上已有的旧文件
/// 会立刻按策略处理，而不必等到下一条日志。
///
/// 返回 false 表示**没能完成检查或执行**（未启用文件日志、磁盘操作失败），
/// 原因写进 `error`；「检查过了、不需要轮转」返回 true。
///
bool rotateIfNeeded(const QDateTime &now, RotationDecision *decision = nullptr,
                    QString *error = nullptr);

///
/// \brief 详细性能计时开关（PRD: OPT-010 第 4 条）。
///
/// 打开后，计时行**不再受全局级别限制**。这正是这个开关存在的理由：排查
/// 「为什么这一步很慢」时最常见的配置是级别停在 warning/error，而把级别调到
/// debug 会同时放出成千上万条逐文件的调试日志——用户要的是「哪一步慢」，
/// 不是「每一步都刷屏」。关闭时计时行与普通日志一样受级别控制（现状）。
///
void setPerformanceTimingEnabled(bool enabled);
bool performanceTimingEnabled();
/// 这个开关在设置仓库里占的键名。
QString performanceTimingKey();

///
/// \brief 记录一条**计时**日志。与 `write()` 只差一处：详细性能计时开关打开时无视全局级别。
///
/// 单独开一个入口，而不是让 `write()` 自己绕过级别：一旦 `write()` 能绕过级别，
/// 第一个这么用的一定不会是计时器，而是某条「反正很重要」的普通日志，
/// 从此 `--log-level` 就管不住日志量了。
///
void writeTiming(Level level, const QString &category, const QString &message);

///
/// \brief 一条日志的完整内容。
///
/// 先成结构、再成文本，是为了让界面输出面板这类消费者不必去**反向解析**
/// 一行已经拼好的字符串（按空格切、猜哪一段是分类、哪一段是线程 id）。
/// 一旦有人这么做，日志格式的任何调整都会静默地打断面板的着色或分列。
///
struct Record
{
    QDateTime time;
    Level level = Level::Info;
    QString category;
    /// 线程 id 的数值形式，对应 `QThread::currentThreadId()`。
    quintptr threadId = 0;
    /// 线程名（`QThread` 对象名）。主线程之外大多为空，空时不输出这一段。
    QString threadName;
    QString message;

    /// 规范文本行。控制台与文件两个目标都用它，保证两处内容逐字一致。
    QString line() const;
};

///
/// \brief 日志的第三个目标：任意消费者（界面输出面板、诊断包导出……）。
///
/// 刻意不做成「只能有一个界面回调」：诊断包导出与输出面板可以同时挂上，
/// 互不知晓。句柄用整数而不是 `std::function` 本身，是为了让移除能按句柄进行——
/// 用函数对象当键去 `remove` 依赖相等比较，而 lambda 之间没有可靠的相等。
///
using Sink = std::function<void(const Record &)>;

/// 注册一个接收者，返回句柄（> 0）。句柄用来移除。
int addSink(Sink sink);
/// 按句柄移除。句柄不存在时什么也不做。
void removeSink(int handle);
/// 移除全部接收者。测试之间必须调用，否则上一个用例留下的 lambda
/// 会捕获着已析构的对象继续被调用。
void clearSinks();
/// 当前接收者个数。
int sinkCount();

///
/// \brief 记录一条日志。
///
/// **本函数自己会按级别过滤**：低于当前级别的调用会被直接丢掉。
/// 于是宏里那个 `if` 不是「过滤」，而是「别求值参数」的优化——
/// 两处行为一致，只是宏省掉了字符串构造。
///
/// 过滤放在这里（而不是只靠宏）是因为宏挡不住直接调用：
/// `write(Level::Debug, …)` 这种写法看起来就该受级别控制，
/// 而早先的版本里它不受——`--log-level error` 下调试日志照样打印，
/// 日志文件也没法靠调级别瘦身，与 `setLevel()` 的文档说明相反。
///
void write(Level level, const QString &category, const QString &message);

///
/// \brief 记录耗时的 RAII 辅助，用于性能排查。
///
/// 用法：在作用域开头构造一个，离开作用域时自动记一条「X 耗时 N ms」。
/// 之所以是 RAII 而不是 `start()` / `stop()` 两个调用：中途 `return`、
/// 抛异常、或忘了写 `stop()` 的路径都不会漏记——而漏记的那条恰恰最可能是
/// 「为什么这里有时很慢」的答案。
///
/// 计时本身是**无条件**开启的（`QElapsedTimer::start()` 只是一次时钟读取）；
/// 「要不要记」在析构时判断（级别，或详细性能计时开关），因此中途调过
/// `setLevel()` / `setPerformanceTimingEnabled()` 也按新值走。
///
class Stopwatch
{
public:
    Stopwatch(Level level, QString category, QString what);
    ~Stopwatch();

    Stopwatch(const Stopwatch &) = delete;
    Stopwatch &operator=(const Stopwatch &) = delete;

    /// 已过去的毫秒数，可在中途查询（不结束计时）。
    qint64 elapsedMs() const;

    /// 附加说明，会出现在同一行末尾。可在中途多次设置，最后一次生效。
    void setNote(const QString &note);

    /// 提前结束并记一条。析构时若已结束就什么也不做，因此手动调用是安全的。
    void finish();

private:
    Level m_level;
    QString m_category;
    QString m_what;
    QString m_note;
    QElapsedTimer m_timer;
    bool m_finished = false;
};

} // namespace Log
} // namespace LqCompare

// 参数在级别未启用时不会被求值。
//
// 注意：宏形参刻意命名为 lvl 而不是 level —— 形参名会参与全宏体的令牌替换，
// 若叫 level，宏体里的 LqCompare::Log::level() 也会被替换掉，编译直接失败。
#define LQCOMPARE_LOG(lvl, category, message)                                                      \
    do {                                                                                          \
        if (LqCompare::Log::isEnabled(lvl)) {                                                      \
            LqCompare::Log::write(lvl, QStringLiteral(category), message);                         \
        }                                                                                         \
    } while (false)

#define LQCOMPARE_ERROR(category, message)                                                         \
    LQCOMPARE_LOG(LqCompare::Log::Level::Error, category, message)
#define LQCOMPARE_WARN(category, message)                                                          \
    LQCOMPARE_LOG(LqCompare::Log::Level::Warning, category, message)
#define LQCOMPARE_INFO(category, message)                                                          \
    LQCOMPARE_LOG(LqCompare::Log::Level::Info, category, message)
#define LQCOMPARE_DEBUG(category, message)                                                         \
    LQCOMPARE_LOG(LqCompare::Log::Level::Debug, category, message)
#define LQCOMPARE_TRACE(category, message)                                                         \
    LQCOMPARE_LOG(LqCompare::Log::Level::Trace, category, message)

///
/// \brief 作用域计时器。
///
/// 两段式展开是为了让 `__LINE__` 先展开成行号再拼接：
/// 直接写 `x##__LINE__` 得到的是字面量 `x__LINE__`，同一个作用域里写两个
/// 就会重定义——而「同一个函数里量两段时间」是最常见的用法。
///
#define LQCOMPARE_TIMER_JOIN_2(a, b) a##b
#define LQCOMPARE_TIMER_JOIN(a, b) LQCOMPARE_TIMER_JOIN_2(a, b)
#define LQCOMPARE_SCOPE_TIMER(lvl, category, what)                                                 \
    LqCompare::Log::Stopwatch LQCOMPARE_TIMER_JOIN(lqcompareScopeTimer_, __LINE__)(                \
            lvl, QStringLiteral(category), what)

#endif // LQCOMPARE_LOGGING_H
