#ifndef LQCOMPARE_TST_LOGGING_H
#define LQCOMPARE_TST_LOGGING_H

#include <QObject>
#include <QtTest>

///
/// \brief 分级日志的测试（PRD: ENG-006）。
///
/// 分组对齐规格的五条完成标准：
///   A 级别与过滤   —— 标准第 1、2 条（分级 + 未启用时参数不求值）
///   B 格式         —— 标准第 4 条（时间戳 / 级别 / 分类 / 线程 id / 消息）
///   C 输出目标     —— 标准第 3 条（控制台 / 文件 / 界面输出面板）
///   D 耗时辅助     —— 标准第 5 条
///   D2 详细性能计时开关 —— OPT-010 第 4 条（计时行与全局级别解耦）
///   D3 轮转策略与写入路径 —— OPT-010 第 2 条（策略住在日志模块里，写入时自动检查）
///   E 级别名解析   —— 支撑命令行与设置界面（去掉第二份事实来源）
///
/// 日志模块持有**全局状态**（级别、日志文件、接收者、轮转策略、性能计时开关），
/// 因此每个用例开始前都必须回到已知状态：`init()` 负责这件事，它在 QTest 里
/// 会在每个用例前自动执行。少了它，用例之间会互相影响，而且失败的那条往往
/// 不是真正出问题的那条——轮转策略尤其如此：上一个用例留下的策略会让下一个
/// 用例的日志文件在自己没察觉的情况下被改名。
///
class TstLogging : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    // ---------- A 级别与过滤（标准第 1、2 条） ----------
    void setLevelTakesEffectImmediately();
    void isEnabledFollowsLevelOrder();
    void writeDropsBelowLevel();
    void writeKeepsAtAndAboveLevel();
    void macroDoesNotEvaluateArgumentsWhenDisabled();
    void macroEvaluatesExactlyOnceWhenEnabled();
    void everyLevelIsReachableByItsOwnName();

    // ---------- B 格式（标准第 4 条） ----------
    void lineCarriesTimeLevelCategoryAndMessage();
    void lineCarriesThreadId();
    void lineCarriesThreadNameWhenSet();
    void lineIsStableAcrossRepeatedCalls();

    // ---------- C 输出目标（标准第 3 条） ----------
    void sinkReceivesRecordWithStructuredFields();
    void multipleSinksAllReceiveInRegistrationOrder();
    void sinkRemovalStopsDelivery();
    void clearSinksRemovesEverything();
    void nullSinkIsRejected();
    void sinkMayLogWithoutDeadlock();
    void sinkIsCalledFromTheThreadThatLogged();
    void fileTargetAppendsAndSurvivesReopen();
    void fileTargetRejectsUnwritablePath();
    void emptyPathDisablesFileTarget();
    void fileTargetReceivesExactlyWhatConsoleWould();

    // ---------- D 耗时辅助（标准第 5 条） ----------
    void stopwatchReportsElapsedOnScopeExit();
    void stopwatchIsSilentWhenLevelDisabled();
    void stopwatchNoteIsAppended();
    void stopwatchElapsedIsQueryableMidway();
    void stopwatchFinishIsIdempotent();
    void stopwatchUsesLevelAtFinishNotConstruction();

    // ---------- D2 详细性能计时开关（OPT-010 第 4 条） ----------
    void timingIsFilteredByLevelWhenTheSwitchIsOff();
    void timingBypassesTheLevelFilterWhenTheSwitchIsOn();
    void timingKeepsTheTimersOwnLevelWhenItBypasses();
    void stopwatchReadsTheSwitchAtFinishTimeNotConstruction();

    // ---------- D3 轮转策略与写入路径（OPT-010 第 2 条） ----------
    void rotationPolicyIsOffByDefaultAndSettable();
    void writeDoesNotRotateWhenThePolicyIsOff();
    void writeRotatesWhenTheSizeLimitIsExceeded();
    void rotateIfNeededReportsTheDecisionWithoutWritingAnything();
    void rotateIfNeededFailsWhenNoLogFileIsConfigured();

    // ---------- E 级别名解析 ----------
    void levelNamesRoundTrip();
    void levelNameParsingIsCaseInsensitiveAndTrimmed();
    void unknownLevelNameLeavesOutputUntouched();
    void levelNamesAreDistinct();
};

#endif // LQCOMPARE_TST_LOGGING_H
