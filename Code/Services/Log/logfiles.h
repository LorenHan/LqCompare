#ifndef LQCOMPARE_LOGFILES_H
#define LQCOMPARE_LOGFILES_H

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace LqCompare {
namespace Log {

///
/// \brief 日志文件本身的维护（PRD: OPT-010 第 2、3 条）。
///
/// 这里只做两件事：**轮转**与**清空**。之所以单独成文件而不是塞进 `logging.cpp`：
/// 这两件事都只碰磁盘上的文件，与「谁在记日志、按什么格式记」无关，
/// 因此可以完全脱离日志模块的全局状态来测试（含真实文件操作的往返）。
///
/// 模块内的分工是刻意的：
///   * `rotationDecision()` 是**纯函数**——策略、当前大小、文件日期、以及「现在」
///     全由调用方传入。它自己不取当前时间，否则「跨天轮转」这类用例只能靠
///     「跑得足够快」避免跨秒失败（与 `attributefilter.h` 里 `referenceTime()` 同源）。
///   * `applyLogRotation()` 执行磁盘动作，并把**决策与失败原因如实交回**。
///     它在失败时不清空、不静默——「轮转没做成」与「没有需要轮转的」必须能区分。
///

enum class RotationMode {
    None = 0,
    Size = 1,
    Daily = 2,
};

/// 持久化与命令行用的规范标识（小写）：`"none"` / `"size"` / `"daily"`。
const char *rotationModeIdentifier(RotationMode mode);
/// 从标识解析模式，大小写不敏感。解析失败返回 false 且**不修改** `out`。
bool rotationModeFromName(const QString &name, RotationMode *out);
/// 界面上显示的中文名。
QString rotationModeLabel(RotationMode mode);
/// 设置项下拉框用的全部取值（顺序即持久化顺序）。
QStringList rotationModeChoices();

///
/// \brief 轮转策略在设置仓库里占用的三个键名。
///
/// 写成函数而不是让调用点各写一遍字面量：这三个键同时被设置仓库（登记定义）、
/// 运行期（读值）与设置页（按定义表生成控件）使用。三处各抄一遍的话，
/// 改名的代价是「有一处漏改」——而漏改那一处的现象是「设置项存在但不生效」，
/// 界面上完全看不出来。
///
QString rotationModeKey();                 // logging.rotationMode
QString rotationMaximumMegabytesKey();     // logging.rotationMaximumMegabytes
QString rotationKeepFilesKey();            // logging.rotationKeepFiles

/// 轮转策略。**纯数据**，不持有任何文件句柄，可以按值拷贝与比较。
struct RotationPolicy
{
    RotationMode mode = RotationMode::None;
    /// `Size` 模式的上界（字节）。
    qint64 maximumBytes = 5LL * 1024 * 1024;
    /// 轮转后保留的历史份数（不含当前文件）。0 表示不留历史。
    int keepFiles = 5;

    /// 从设置仓库的键值读出策略。缺失的键走默认值，未知标识退回 `None`
    /// 并**不报错**——设置文件里的非法值由 `OptionsRepository::validate()` 负责拦，
    /// 这里再报一次会变成第二份校验。
    static RotationPolicy fromValues(const QVariantMap &values);
    /// 写回设置仓库的键值（与 `fromValues()` 往返一致）。
    QVariantMap toValues() const;

    /// 自检：返回空串表示合法，否则是一句可直接显示给用户的原因。
    /// 只检查**当前生效模式所用到**的字段——「轮转关掉了却报上限非法」
    /// 是用户改不掉的报错（那个控件在他看到的这一页上就是灰的）。
    QString validate() const;

    bool active() const { return mode != RotationMode::None; }
    qint64 maximumMegabytes() const { return maximumBytes / (1024 * 1024); }
    void setMaximumMegabytes(qint64 megabytes) { maximumBytes = megabytes * 1024 * 1024; }
};

/// 触发轮转的原因。留成枚举而不是一句文案，是为了让界面与日志不必去解析中文字符串。
enum class RotationTrigger {
    None = 0,
    SizeExceeded = 1,
    NewDay = 2,
};

QString rotationTriggerLabel(RotationTrigger trigger);

///
/// \brief 一次轮转判定的完整结果。
///
/// `reason` 是**给人和给日志**的同一句话：界面上的「为什么还没有轮转」
/// 与日志里那条「已轮转」必须是同源文本，否则两边迟早会说出不同的话。
///
struct RotationDecision
{
    bool rotate = false;
    RotationTrigger trigger = RotationTrigger::None;
    QString reason;
    qint64 currentSize = 0;
};

///
/// \brief 判定是否需要轮转。**纯函数**：不读磁盘、不取当前时间。
///
/// `currentSize` 是日志文件的当前字节数；`fileDate` 是它最后一次写入的日期
/// （文件不存在时传无效 `QDate()`）；`now` 是「现在」。
///
/// 三条边界写在这里，因为它们都是「看起来该轮转、其实不该」的情形：
///   * 空文件（或文件不存在）一律不轮转——按天轮转时，一个空日志每天都会
///     产出一个空历史文件，用户看到一串 0 字节的 `.1` `.2` 只会以为程序坏了。
///   * 恰好等于上限即轮转（`>=` 而不是 `>`）——否则「上限 5 MB」实际允许长到
///     5 MB 之后再多一条日志，边界值上写测试的人会分不清是哪一种。
///   * `Daily` 用**文件最后写入日期**与今天比较，而不是「运行时跨过 0 点」：
///     程序退出一天后再启动，同一条策略也要生效。
///
RotationDecision rotationDecision(const RotationPolicy &policy, qint64 currentSize,
                                  const QDate &fileDate, const QDateTime &now);

/// 第 `index` 份历史日志的路径：`lqcompare.log` → `lqcompare.log.1`（`.1` 最新）。
/// 为什么用序号而不是时间戳：同一秒内连续两次轮转不会撞名，且「保留最近 N 份」
/// 这件事在序号上就是一行删除，不必去解析文件名里的时间。
/// `index <= 0` 时返回空串（调用点可以据此发现自己的下标算错了）。
QString rotatedLogPath(const QString &logFilePath, int index);

/// 把策略概括成一句人话，供设置页提示与诊断包的环境信息共用（同源文本）。
QString rotationSummary(const RotationPolicy &policy);

///
/// \brief 当前日志文件的全部历史份（`.1` / `.2` …），按序号升序返回绝对路径。
///
/// 诊断包要把它收进去：用户报告的问题很可能发生在轮转之前的那一份里，
/// 而「只有当前文件」的诊断包会让排查者看不到出事那一刻的日志。
///
QStringList logHistoryFiles(const QString &logFilePath);

///
/// \brief 按策略检查并执行一次轮转。
///
/// 返回值的语义只有一种，必须说清楚，否则调用点会把它当「是否发生了轮转」用：
///   * **true** —— 检查完成（无论是否真的轮转了）。
///   * **false** —— 没能完成（未启用文件日志、或磁盘操作失败），原因写进 `error`。
///
/// 失败时**不做任何补偿性写入**：日志系统自己出故障时把用户正在做的事一起弄挂，
/// 代价比少记几行日志大得多。调用点该做的是把 `error` 报到界面或日志里。
///
bool applyLogRotation(const QString &logFilePath, const RotationPolicy &policy,
                      const QDateTime &now, RotationDecision *decision = nullptr,
                      QString *error = nullptr);

///
/// \brief 清空当前日志文件（保留文件本身与它的权限）。
///
/// 为什么是「截断」而不是「删除」：日志文件此刻可能正被本进程以追加方式打开，
/// 删掉它之后写入会落到一个已被删除的 inode 上——`ls` 看不到增长，
/// 而调用方以为日志已经重新开始了。截断对追加写入是安全的。
///
/// 文件不存在时返回 true（「没有需要清空的东西」不是失败）。
///
bool clearLogFile(const QString &logFilePath, QString *error = nullptr);

///
/// \brief 轮转参数的上下界。
///
/// 这三个数字被设置页（`OptionDefinition` 的 `minimum` / `maximum`）与
/// `RotationPolicy::validate()` 同时使用。写成函数而不是两处各写一遍字面量：
/// 两处不一致时的现象是「界面上允许填 2000，保存时却被判定非法」，
/// 而用户只会觉得这个页面坏了。
///
qint64 minimumRotationMaximumMegabytes();
qint64 maximumRotationMaximumMegabytes();
int maximumRotationKeepFiles();

} // namespace Log
} // namespace LqCompare

#endif // LQCOMPARE_LOGFILES_H
