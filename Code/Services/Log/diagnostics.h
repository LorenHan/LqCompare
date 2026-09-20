#ifndef LQCOMPARE_DIAGNOSTICS_H
#define LQCOMPARE_DIAGNOSTICS_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Log {

///
/// \brief 诊断能力（PRD: OPT-010 第 3、5 条）。
///
/// 规格的边界写着「诊断能力必须在用户机器上可用，不能要求用户自行配置调试器」。
/// 落到这里就是两件事：**把环境信息与日志一起打成一个包**，以及
/// **导出前如实告诉用户里面有什么**（尤其是有没有机器相关的路径）。
///
/// 本模块全是纯逻辑 + 文件读写，不碰任何界面：脱敏规则、环境报告、
/// 诊断包内容都能在临时目录里逐字节断言。`QDesktopServices`（打开日志目录）
/// 那条留在设置页里，因为「打开一个 URL」本来就是界面动作。
///

/// 一条脱敏规则：把文本里出现的 `prefix` 换成 `replacement`。
struct RedactionRule
{
    QString prefix;
    QString replacement;
};

///
/// \brief 默认的脱敏规则：家目录 → `~`，配置目录 → `<配置目录>`。
///
/// 为什么是「替换前缀」而不是「抹掉所有绝对路径」：日志的价值恰恰在于
/// 「哪个文件出错了」，把路径整段删掉会让诊断包一半的内容变成 `<路径>`，
/// 排查者只能再来问一次。替换掉用户名那一段就足以去掉个人身份信息。
///
QVector<RedactionRule> defaultRedactionRules(const QString &homeDirectory,
                                             const QString &storageDirectory);

///
/// \brief 按规则脱敏一段文本，返回替换后的内容。
///
/// `replacedCount` 交回总替换次数（可为 nullptr）。次数要交回去是因为
/// 「一个都没替换」很可能意味着规则写错了（比如家目录传了空串），
/// 调用方需要能看出一份「脱敏后」的诊断包其实什么都没脱。
///
QString sanitizeDiagnosticText(const QString &text, const QVector<RedactionRule> &rules,
                               int *replacedCount = nullptr);

///
/// \brief 诊断包里的环境信息。
///
/// 全部字段由调用方填：模块自己去问 `QSysInfo` / `qVersion()` 会让这一段
/// 无法在测试里断言（只能断言「非空」），也会把「程序版本从哪来」这件事
/// 藏进模块内部。
///
struct DiagnosticEnvironment
{
    QString applicationName;
    QString applicationVersion;
    QString qtVersion;
    QString osDescription;
    QString architecture;
    /// 「标准模式（用户配置目录）」这类人话 + 目录本身。
    QString storageMode;
    QString storageDirectory;
    /// 日志级别的规范标识（`error` / `warning` / …）。
    QString logLevel;
    /// `rotationSummary()` 的结果，保证设置页与诊断包说的是同一句话。
    QString rotationSummary;
    QString logFilePath;
    /// 导出时刻。由调用方传入（诊断包构建时用请求里的 `now`），
    /// 模块自己不取当前时间——否则「导出时间」这一行没法在测试里断言。
    QDateTime exportTime;
};

/// 人可读的环境报告（纯文本，一行一项）。
QString environmentReport(const DiagnosticEnvironment &environment);

/// 环境报告在诊断包里的文件名。
QString environmentReportFileName();

/// 诊断包清单的文件名。
QString manifestFileName();

/// 诊断包里放日志的目录名（相对诊断包根）。
QString logArchiveDirectoryName();

/// 导出前的提示文案。两种口径必须不同——若脱敏开关不影响这句话，
/// 用户就没有任何依据去判断要不要勾它。
QString diagnosticNoticeText(bool redactPaths);

struct DiagnosticBundleRequest
{
    /// 当前日志文件；为空表示本机没有启用文件日志（仍然会产出环境信息）。
    QString logFilePath;
    /// 诊断包写到哪个目录下（会在其中新建一个带时间戳的子目录）。
    QString outputDirectory;
    DiagnosticEnvironment environment;
    /// 是否脱敏。默认**脱敏**：没勾的时候风险由用户显式承担，
    /// 而默认不脱敏会让「只是想发给同事看一眼」的人顺手把用户名发出去。
    bool redactPaths = true;
    QString homeDirectory;
    QString storageDirectory;
    QDateTime now;
};

struct DiagnosticBundleResult
{
    bool ok = false;
    QString error;
    QString bundleDirectory;
    /// 诊断包内的相对路径，按写入顺序。
    QStringList files;
    /// 收进包里的日志文件（绝对路径），便于调用方在日志里记一条「收了哪几份」。
    QStringList archivedLogs;
    /// 被替换掉的路径出现次数（脱敏关闭时为 0）。
    int redactedOccurrences = 0;
    bool redacted = false;
    bool logIncluded = false;
};

///
/// \brief 生成一个诊断包目录。
///
/// 目录名形如 `lqcompare-diagnostics-20260921-061500`；同一秒内重复导出会退化成
/// `…-2` / `…-3`，**不合并进已有目录**——两份诊断包混在一起之后，
/// 没人说得清哪一行日志是哪次问题的。
///
/// 失败时 `ok` 为 false 且 `error` 写清是哪一步失败（建目录 / 读日志 / 写文件）。
/// 未启用文件日志不是失败：环境信息本身就有诊断价值，此时 `logIncluded` 为 false。
///
DiagnosticBundleResult buildDiagnosticBundle(const DiagnosticBundleRequest &request);

} // namespace Log
} // namespace LqCompare

#endif // LQCOMPARE_DIAGNOSTICS_H
