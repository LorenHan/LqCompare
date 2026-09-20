#include "tst_logdiagnostics.h"

#include "diagnostics.h"
#include "logfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using LqCompare::Log::DiagnosticBundleRequest;
using LqCompare::Log::DiagnosticBundleResult;
using LqCompare::Log::DiagnosticEnvironment;
using LqCompare::Log::RedactionRule;
using LqCompare::Log::RotationDecision;
using LqCompare::Log::RotationMode;
using LqCompare::Log::RotationPolicy;
using LqCompare::Log::RotationTrigger;

namespace {

/// `QCOMPARE(a, QStringList{"x","y"})` 会因为宏参数里的逗号被预处理器劈成两半
/// （花括号不算括号，只有圆括号算）。所有列表比较都套一层它。
QStringList asList(const QStringList &values)
{
    return values;
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool ok = file.write(content) == content.size();
    file.close();
    return ok;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

QString readText(const QString &path)
{
    return QString::fromUtf8(readFile(path));
}

/// 造一个正好 `bytes` 字节的文件。
bool writeFileOfSize(const QString &path, qint64 bytes)
{
    return writeFile(path, QByteArray(static_cast<int>(bytes), 'x'));
}

/// 把文件的最后写入时间改成某个时刻。按天轮转是靠它判定的，因此这条路径
/// 只能真实地走一次——把「文件是昨天的」这件事伪造出来。
bool setFileModified(const QString &path, const QDateTime &moment)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    const bool ok = file.setFileTime(moment, QFileDevice::FileModificationTime);
    file.close();
    return ok;
}

QStringList entryNames(const QDir &dir)
{
    QStringList names = dir.entryList(QDir::Files);
    names.sort();
    return names;
}

DiagnosticEnvironment sampleEnvironment()
{
    DiagnosticEnvironment environment;
    environment.applicationName = QStringLiteral("LqCompare");
    environment.applicationVersion = QStringLiteral("0.1.0");
    environment.qtVersion = QStringLiteral("5.15.2");
    environment.osDescription = QStringLiteral("macOS 26.0");
    environment.architecture = QStringLiteral("x86_64");
    environment.storageMode = QStringLiteral("标准模式（用户配置目录）");
    environment.storageDirectory = QStringLiteral("/Users/loren/.config/LqCompare");
    environment.logLevel = QStringLiteral("warning");
    environment.rotationSummary = QStringLiteral("轮转已关闭：日志会一直追加到同一个文件。");
    return environment;
}

} // namespace

void TstLogDiagnostics::initTestCase()
{
    // 本套件全程只碰临时目录，不依赖日志模块的全局状态（级别 / 日志文件 / 接收者），
    // 因此没有 init()。唯一需要先确认的是源码级护栏能定位到源码。
    QVERIFY2(!codeRoot().isEmpty(), "缺少 LQCOMPARE_CODE_ROOT 定义，源码级护栏无法定位源码。");
    QVERIFY(QFileInfo(codeRoot()).isDir());
}

// =============================================================================
// A 轮转策略与自检（第 2 条）
// =============================================================================

void TstLogDiagnostics::defaultsMatchTheDocumentedFactoryValues()
{
    // 出厂值是产品行为的一部分：用户不碰设置时的行为全靠它。这三个数字
    // 同时出现在 `OptionsRepository::definitions()` 里（Tests/Options 有一条
    // 用例核对两边一致），因此任何一边被改都会红。
    const RotationPolicy policy;
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(policy.mode)),
             QStringLiteral("none"));
    QCOMPARE(policy.maximumMegabytes(), 5LL);
    QCOMPARE(policy.keepFiles, 5);
    QVERIFY(!policy.active());
}

void TstLogDiagnostics::fromValuesReadsAllThreeKeys()
{
    QVariantMap values;
    values.insert(LqCompare::Log::rotationModeKey(), QStringLiteral("size"));
    values.insert(LqCompare::Log::rotationMaximumMegabytesKey(), 12);
    values.insert(LqCompare::Log::rotationKeepFilesKey(), 3);

    const RotationPolicy policy = RotationPolicy::fromValues(values);
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(policy.mode)),
             QStringLiteral("size"));
    QCOMPARE(policy.maximumBytes, 12LL * 1024 * 1024);
    QCOMPARE(policy.maximumMegabytes(), 12LL);
    QCOMPARE(policy.keepFiles, 3);
    QVERIFY(policy.active());
}

void TstLogDiagnostics::fromValuesKeepsUnusedFieldsAcrossModeSwitch()
{
    // 用户先按大小配好、再切到「按天」，切回来时上界不该悄悄回到 5 MB。
    // 这条是「未使用字段也照读」这个决定的守门用例：把它改成「只在需要时读」，
    // 本用例会红。
    QVariantMap values;
    values.insert(LqCompare::Log::rotationModeKey(), QStringLiteral("daily"));
    values.insert(LqCompare::Log::rotationMaximumMegabytesKey(), 42);
    values.insert(LqCompare::Log::rotationKeepFilesKey(), 7);

    const RotationPolicy asDaily = RotationPolicy::fromValues(values);
    QCOMPARE(asDaily.maximumMegabytes(), 42LL);
    QCOMPARE(asDaily.keepFiles, 7);

    values.insert(LqCompare::Log::rotationModeKey(), QStringLiteral("size"));
    const RotationPolicy backToSize = RotationPolicy::fromValues(values);
    QCOMPARE(backToSize.maximumMegabytes(), 42LL);
    QCOMPARE(backToSize.keepFiles, 7);
}

void TstLogDiagnostics::unknownRotationModeFallsBackToNoneWithoutError()
{
    // 设置文件里的非法值由 `OptionsRepository::validate()` 负责拦；
    // 这里再报一次就会变成第二份校验，而且报错的地方是日志模块——用户
    // 修不掉，因为界面上根本没有那个字段。
    QVariantMap values;
    values.insert(LqCompare::Log::rotationModeKey(), QStringLiteral("hourly"));
    const RotationPolicy policy = RotationPolicy::fromValues(values);
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(policy.mode)),
             QStringLiteral("none"));
    QCOMPARE(policy.validate(), QString());
}

void TstLogDiagnostics::toValuesRoundTripsThroughFromValues()
{
    RotationPolicy original;
    original.mode = RotationMode::Daily;
    original.setMaximumMegabytes(64);
    original.keepFiles = 9;

    const RotationPolicy restored = RotationPolicy::fromValues(original.toValues());
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(restored.mode)),
             QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(original.mode)));
    QCOMPARE(restored.maximumBytes, original.maximumBytes);
    QCOMPARE(restored.keepFiles, original.keepFiles);
    // 键名也要往返：写出去的键读不回来，现象是「设置项存了但不生效」。
    const QVariantMap values = original.toValues();
    QVERIFY(values.contains(LqCompare::Log::rotationModeKey()));
    QVERIFY(values.contains(LqCompare::Log::rotationMaximumMegabytesKey()));
    QVERIFY(values.contains(LqCompare::Log::rotationKeepFilesKey()));
}

void TstLogDiagnostics::rotationModeChoicesCoverEveryModeExactlyOnce()
{
    const QStringList choices = LqCompare::Log::rotationModeChoices();
    QCOMPARE(asList(choices), asList(QStringList{QStringLiteral("none"), QStringLiteral("size"),
                                                 QStringLiteral("daily")}));
    // 下拉框的取值集合必须与「能解析的名字集合」一致：多一个选不中的选项
    // 或者少一个能存的值，都会表现为「选了这个模式却像没选」。
    for (const QString &choice : choices) {
        RotationMode parsed = RotationMode::None;
        QVERIFY2(LqCompare::Log::rotationModeFromName(choice, &parsed),
                 qPrintable(choice));
    }
    QCOMPARE(choices.size(), 3);
}

void TstLogDiagnostics::rotationModeIdentifiersAreDistinct()
{
    const QSet<QString> identifiers {
        QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(RotationMode::None)),
        QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(RotationMode::Size)),
        QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(RotationMode::Daily))};
    QCOMPARE(identifiers.size(), 3);
}

void TstLogDiagnostics::rotationModeLabelsAreDistinctAndNonEmpty()
{
    const QSet<QString> labels {
        LqCompare::Log::rotationModeLabel(RotationMode::None),
        LqCompare::Log::rotationModeLabel(RotationMode::Size),
        LqCompare::Log::rotationModeLabel(RotationMode::Daily)};
    QCOMPARE(labels.size(), 3);
    for (const QString &label : labels)
        QVERIFY(!label.isEmpty());
}

void TstLogDiagnostics::rotationModeNameParsingIsCaseInsensitiveAndTrimmed()
{
    RotationMode mode = RotationMode::None;
    QVERIFY(LqCompare::Log::rotationModeFromName(QStringLiteral("  SIZE  "), &mode));
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(mode)),
             QStringLiteral("size"));
    QVERIFY(LqCompare::Log::rotationModeFromName(QStringLiteral("Daily"), &mode));
    QCOMPARE(QString::fromLatin1(LqCompare::Log::rotationModeIdentifier(mode)),
             QStringLiteral("daily"));
}

void TstLogDiagnostics::unknownRotationModeNameLeavesOutputUntouched()
{
    RotationMode mode = RotationMode::Daily;
    QVERIFY(!LqCompare::Log::rotationModeFromName(QStringLiteral("weekly"), &mode));
    // 「认不出来时不动 out」是调用点能把「用户写错了」与「用户没写」分开的前提。
    QCOMPARE(mode, RotationMode::Daily);
    QVERIFY(!LqCompare::Log::rotationModeFromName(QString(), &mode));
    QCOMPARE(mode, RotationMode::Daily);
}

void TstLogDiagnostics::validateRejectsKeepFilesOutOfRange()
{
    RotationPolicy policy;
    policy.keepFiles = -1;
    QVERIFY(!policy.validate().isEmpty());
    policy.keepFiles = LqCompare::Log::maximumRotationKeepFiles() + 1;
    QVERIFY(!policy.validate().isEmpty());

    // 边界值必须收得进来：0 是「不留历史」这个真实用法。
    policy.keepFiles = 0;
    QCOMPARE(policy.validate(), QString());
    policy.keepFiles = LqCompare::Log::maximumRotationKeepFiles();
    QCOMPARE(policy.validate(), QString());
}

void TstLogDiagnostics::validateRejectsSizeLimitOutsideBounds()
{
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.setMaximumMegabytes(LqCompare::Log::minimumRotationMaximumMegabytes() - 1);
    QVERIFY(!policy.validate().isEmpty());
    policy.setMaximumMegabytes(LqCompare::Log::maximumRotationMaximumMegabytes() + 1);
    QVERIFY(!policy.validate().isEmpty());

    policy.setMaximumMegabytes(LqCompare::Log::minimumRotationMaximumMegabytes());
    QCOMPARE(policy.validate(), QString());
    policy.setMaximumMegabytes(LqCompare::Log::maximumRotationMaximumMegabytes());
    QCOMPARE(policy.validate(), QString());
}

void TstLogDiagnostics::validateIgnoresSizeLimitWhenModeIsNotSize()
{
    // 这是一个**决定**，不是遗漏：界面上跟模式无关的那个数字框仍然可编辑，
    // 但用户此刻的意图与它无关。为「以后可能用到」拦住一次保存，只会让人
    // 以为设置页坏了。
    RotationPolicy policy;
    policy.mode = RotationMode::None;
    policy.setMaximumMegabytes(0);
    QCOMPARE(policy.validate(), QString());

    policy.mode = RotationMode::Daily;
    QCOMPARE(policy.validate(), QString());

    // 但保留份数与模式无关，任何模式下都要守住。
    policy.mode = RotationMode::None;
    policy.keepFiles = 999;
    QVERIFY(!policy.validate().isEmpty());
}

void TstLogDiagnostics::validateAcceptsEveryDefaultPolicy()
{
    for (RotationMode mode : {RotationMode::None, RotationMode::Size, RotationMode::Daily}) {
        RotationPolicy policy;
        policy.mode = mode;
        QCOMPARE(policy.validate(), QString());
    }
}

// =============================================================================
// B 轮转判定（纯函数，第 2 条）
// =============================================================================

void TstLogDiagnostics::noneModeNeverRotates()
{
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::None;
    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 100LL * 1024 * 1024, QDate(2020, 1, 1), now);
    QVERIFY(!decision.rotate);
    QCOMPARE(decision.trigger, RotationTrigger::None);
    QVERIFY(decision.reason.contains(QStringLiteral("关闭")));
}

void TstLogDiagnostics::sizeModeDoesNotRotateBelowTheLimit()
{
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.setMaximumMegabytes(1);

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 1024 * 1024 - 1, QDate(2026, 9, 21), now);
    QVERIFY(!decision.rotate);
    QVERIFY(decision.reason.contains(QStringLiteral("未达到上限")));
}

void TstLogDiagnostics::sizeModeRotatesAtExactlyTheLimit()
{
    // 边界取 `>=` 而不是 `>`：写成 `>` 的话「上限 5 MB」实际允许长到 5 MB
    // 之后再多写一条，而用例作者会分不清是哪一种。
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1024;

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 1024, QDate(2026, 9, 21), now);
    QVERIFY(decision.rotate);
    QCOMPARE(decision.trigger, RotationTrigger::SizeExceeded);
    QVERIFY(decision.reason.contains(QStringLiteral("达到上限")));
}

void TstLogDiagnostics::sizeModeRotatesAboveTheLimit()
{
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1024;

    QVERIFY(LqCompare::Log::rotationDecision(policy, 4096, QDate(2026, 9, 21), now).rotate);
}

void TstLogDiagnostics::emptyFileNeverRotatesInSizeMode()
{
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 0; // 极端配置：任何非空文件都超限

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 0, QDate(2026, 9, 21), now);
    QVERIFY(!decision.rotate);
    QVERIFY(decision.reason.contains(QStringLiteral("为空")));
}

void TstLogDiagnostics::emptyFileNeverRotatesInDailyMode()
{
    // 按天轮转时这条最要紧：一个从来没写过日志的路径，每天都会产出一个
    // 0 字节的 `.1`，用户看到一串空文件只会以为程序坏了。
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Daily;

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 0, QDate(2020, 1, 1), now);
    QVERIFY(!decision.rotate);
    QVERIFY(decision.reason.contains(QStringLiteral("为空")));
}

void TstLogDiagnostics::dailyModeRotatesWhenTheFileIsFromYesterday()
{
    const QDateTime now(QDate(2026, 9, 21), QTime(0, 0, 30));
    RotationPolicy policy;
    policy.mode = RotationMode::Daily;

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 512, QDate(2026, 9, 20), now);
    QVERIFY(decision.rotate);
    QCOMPARE(decision.trigger, RotationTrigger::NewDay);
}

void TstLogDiagnostics::dailyModeDoesNotRotateOnTheSameDay()
{
    // 「现在」由调用方传入，跨秒、跨时区都不会影响这条判定。
    const QDateTime now(QDate(2026, 9, 21), QTime(23, 59, 59));
    RotationPolicy policy;
    policy.mode = RotationMode::Daily;

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 512, QDate(2026, 9, 21), now);
    QVERIFY(!decision.rotate);
    QVERIFY(decision.reason.contains(QStringLiteral("同一天")));
}

void TstLogDiagnostics::dailyModeRefusesWhenTheFileDateIsUnknown()
{
    // 「不知道」不等于「是旧文件」。判成旧文件会让一次元数据读取失败
    // 变成一次凭空多出来的轮转。
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Daily;

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 512, QDate(), now);
    QVERIFY(!decision.rotate);
    QVERIFY(decision.reason.contains(QStringLiteral("无法确定")));
}

void TstLogDiagnostics::dailyModeReasonNamesBothDates()
{
    // 理由要说得出「哪一天 → 哪一天」：日志里那条「已轮转」是排查时唯一
    // 能回答「为什么昨天那份不见了」的证据。
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Daily;

    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 512, QDate(2026, 9, 19), now);
    QVERIFY(decision.rotate);
    QVERIFY(decision.reason.contains(QStringLiteral("2026-09-19")));
    QVERIFY(decision.reason.contains(QStringLiteral("2026-09-21")));
}

void TstLogDiagnostics::decisionAlwaysCarriesAReason()
{
    // 无论结论是什么都要有理由：界面上的「为什么还没轮转」与日志里那条
    // 「已轮转」是同一句话的两个用法。
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    for (RotationMode mode : {RotationMode::None, RotationMode::Size, RotationMode::Daily}) {
        RotationPolicy policy;
        policy.mode = mode;
        for (qint64 size : {qint64(0), qint64(1024), qint64(64) * 1024 * 1024}) {
            const RotationDecision decision = LqCompare::Log::rotationDecision(
                    policy, size, QDate(2026, 9, 20), now);
            QVERIFY2(!decision.reason.isEmpty(),
                     qPrintable(QStringLiteral("%1 / %2").arg(int(mode)).arg(size)));
        }
    }
}

void TstLogDiagnostics::decisionCarriesCurrentSize()
{
    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    const RotationDecision decision = LqCompare::Log::rotationDecision(
            policy, 12345, QDate(2026, 9, 21), now);
    QCOMPARE(decision.currentSize, 12345LL);
}

void TstLogDiagnostics::rotationTriggerLabelsAreDistinct()
{
    const QSet<QString> labels {
        LqCompare::Log::rotationTriggerLabel(RotationTrigger::None),
        LqCompare::Log::rotationTriggerLabel(RotationTrigger::SizeExceeded),
        LqCompare::Log::rotationTriggerLabel(RotationTrigger::NewDay)};
    QCOMPARE(labels.size(), 3);
}

void TstLogDiagnostics::rotationSummaryDescribesEveryMode()
{
    RotationPolicy policy;
    policy.mode = RotationMode::None;
    QVERIFY(LqCompare::Log::rotationSummary(policy).contains(QStringLiteral("关闭")));

    policy.mode = RotationMode::Size;
    policy.setMaximumMegabytes(8);
    const QString bySize = LqCompare::Log::rotationSummary(policy);
    QVERIFY(bySize.contains(QStringLiteral("8.00 MB")));
    QVERIFY(bySize.contains(QStringLiteral("5")));

    policy.mode = RotationMode::Daily;
    const QString byDay = LqCompare::Log::rotationSummary(policy);
    QVERIFY(byDay.contains(QStringLiteral("按天")));

    policy.keepFiles = 0;
    QVERIFY(LqCompare::Log::rotationSummary(policy).contains(QStringLiteral("不保留历史")));
}

// =============================================================================
// C 真实文件轮转与清空（第 2、3 条）
// =============================================================================

void TstLogDiagnostics::applyRotationRenamesTheCurrentFileToDotOne()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("旧的一行\n")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 8; // 已经超限

    RotationDecision decision;
    QString error;
    QVERIFY2(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0)),
                                             &decision, &error),
             qPrintable(error));
    QVERIFY(decision.rotate);
    QCOMPARE(readText(logPath + QStringLiteral(".1")), QStringLiteral("旧的一行\n"));
}

void TstLogDiagnostics::applyRotationLeavesNoCurrentFileBehind()
{
    // 轮转**不创建**新的当前文件：下一次追写会自动创建它。这条语义要钉住，
    // 因为「顺手建一个空文件」看起来更完整，却会让「文件存在但为空」成为
    // 又一个需要判断的状态（而空文件恰好是轮转判定里那个特例）。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("x")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;

    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0))));
    QVERIFY(!QFile::exists(logPath));
    QVERIFY(QFile::exists(logPath + QStringLiteral(".1")));
}

void TstLogDiagnostics::applyRotationShiftsExistingHistoryUp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("one")));
    QVERIFY(writeFile(logPath + QStringLiteral(".2"), QByteArray("two")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;
    policy.keepFiles = 5;

    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0))));
    QCOMPARE(readText(logPath + QStringLiteral(".1")), QStringLiteral("current"));
    QCOMPARE(readText(logPath + QStringLiteral(".2")), QStringLiteral("one"));
    QCOMPARE(readText(logPath + QStringLiteral(".3")), QStringLiteral("two"));
    QCOMPARE(asList(entryNames(QDir(dir.path()))),
             asList(QStringList{QStringLiteral("lqcompare.log.1"), QStringLiteral("lqcompare.log.2"),
                                QStringLiteral("lqcompare.log.3")}));
}

void TstLogDiagnostics::applyRotationDropsFilesBeyondTheKeepCount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));
    for (int index = 1; index <= 4; ++index)
        QVERIFY(writeFile(logPath + QStringLiteral(".%1").arg(index), QByteArray("old")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;
    policy.keepFiles = 2;

    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0))));
    // 保留 2 份历史 + 当前文件刚变成的第 1 份 = 3 个文件。
    QCOMPARE(asList(entryNames(QDir(dir.path()))),
             asList(QStringList{QStringLiteral("lqcompare.log.1"), QStringLiteral("lqcompare.log.2"),
                                QStringLiteral("lqcompare.log.3")}));
    QCOMPARE(readText(logPath + QStringLiteral(".1")), QStringLiteral("current"));
    QCOMPARE(readText(logPath + QStringLiteral(".3")), QStringLiteral("old"));
}

void TstLogDiagnostics::applyRotationWithZeroKeepFilesDeletesTheOldFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("one")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;
    policy.keepFiles = 0;

    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0))));
    // 「保留 0 份」= 轮转后立刻丢弃旧日志，因此目录里什么都留不下。
    QCOMPARE(asList(entryNames(QDir(dir.path()))), asList(QStringList{}));
}

void TstLogDiagnostics::applyRotationDoesNothingWhenTheDecisionSaysNo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("keep me")));

    RotationPolicy policy; // 默认不轮转
    RotationDecision decision;
    QString error;
    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0)),
                                             &decision, &error));
    // 「不需要轮转」不是失败，但必须能看出来它确实没动过手。
    QVERIFY(!decision.rotate);
    QCOMPARE(error, QString());
    QCOMPARE(readText(logPath), QStringLiteral("keep me"));
    QVERIFY(!QFile::exists(logPath + QStringLiteral(".1")));
}

void TstLogDiagnostics::applyRotationReportsAnEmptyPathAsFailure()
{
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;
    QString error;
    QVERIFY(!LqCompare::Log::applyLogRotation(QString(), policy,
                                              QDateTime(QDate(2026, 9, 21), QTime(6, 0)),
                                              nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TstLogDiagnostics::applyRotationReportsFailureWhenItCannotFreeTheHistorySlot()
{
    // 让「腾出 `.1` 的位置」失败：把 `.1` 造成一个**非空目录**，`QFile::remove()`
    // 删不掉它。这条路径上不能静默跳过——静默的后果是当前日志被覆盖到别处
    // 或者干脆消失，而界面会说「轮转完成」。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));
    const QString blocked = logPath + QStringLiteral(".1");
    QVERIFY(QDir().mkpath(blocked));
    QVERIFY(writeFile(QDir(blocked).filePath(QStringLiteral("occupied")), QByteArray("x")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;
    policy.keepFiles = 2;

    RotationDecision decision;
    QString error;
    QVERIFY(!LqCompare::Log::applyLogRotation(logPath, policy,
                                              QDateTime(QDate(2026, 9, 21), QTime(6, 0)),
                                              &decision, &error));
    QVERIFY(decision.rotate);
    QVERIFY(error.contains(QStringLiteral("腾出")));
    // 当前日志必须原样留在原地：失败时不做「补偿性写入」。
    QCOMPARE(readText(logPath), QStringLiteral("current"));
}

void TstLogDiagnostics::applyRotationIsIdempotentForTheSameFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));

    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;

    const QDateTime now(QDate(2026, 9, 21), QTime(6, 0));
    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy, now));
    // 第二次调用时当前文件已经不存在，判定落在「日志文件为空」上：
    // 不会凭空再多出一份 `.1`。写入路径每次日志都可能走到这里，
    // 因此这条语义决定了「轮转会不会连发」。
    RotationDecision decision;
    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy, now, &decision));
    QVERIFY(!decision.rotate);
    QCOMPARE(asList(entryNames(QDir(dir.path()))), asList(QStringList{QStringLiteral("lqcompare.log.1")}));
}

void TstLogDiagnostics::applyRotationUsesTheFilesLastWriteDateForDailyPolicy()
{
    // 纯函数那一组把日期规则测透了，这里只证明**真的文件**的 mtime 被传了进去
    // （而不是传了「现在」或某个默认值）。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("yesterday")));
    QVERIFY(setFileModified(logPath, QDateTime(QDate(2026, 9, 20), QTime(23, 0))));

    RotationPolicy policy;
    policy.mode = RotationMode::Daily;

    RotationDecision decision;
    QVERIFY(LqCompare::Log::applyLogRotation(
            logPath, policy, QDateTime(QDate(2026, 9, 21), QTime(6, 0)), &decision));
    QVERIFY(decision.rotate);
    QCOMPARE(decision.trigger, RotationTrigger::NewDay);
    QVERIFY(QFile::exists(logPath + QStringLiteral(".1")));
}

void TstLogDiagnostics::rotatedLogPathIsEmptyForNonPositiveIndexes()
{
    const QString logPath = QStringLiteral("/tmp/lqcompare.log");
    QVERIFY(LqCompare::Log::rotatedLogPath(logPath, 0).isEmpty());
    QVERIFY(LqCompare::Log::rotatedLogPath(logPath, -3).isEmpty());
    QVERIFY(LqCompare::Log::rotatedLogPath(QString(), 1).isEmpty());
}

void TstLogDiagnostics::rotatedLogPathAppendsTheIndex()
{
    QCOMPARE(LqCompare::Log::rotatedLogPath(QStringLiteral("/tmp/lqcompare.log"), 1),
             QStringLiteral("/tmp/lqcompare.log.1"));
    QCOMPARE(LqCompare::Log::rotatedLogPath(QStringLiteral("/tmp/lqcompare.log"), 12),
             QStringLiteral("/tmp/lqcompare.log.12"));
}

void TstLogDiagnostics::clearLogFileTruncatesButKeepsTheFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("很多行\n")));

    QString error;
    QVERIFY2(LqCompare::Log::clearLogFile(logPath, &error), qPrintable(error));
    // 文件必须还在：删除会让「本进程仍以追加方式持有它」这件事落到一个
    // 已被删除的 inode 上，`ls` 看不到增长而调用方以为日志重新开始了。
    QVERIFY(QFile::exists(logPath));
    QCOMPARE(QFileInfo(logPath).size(), 0LL);
    QCOMPARE(error, QString());
}

void TstLogDiagnostics::clearLogFileSucceedsWhenTheFileDoesNotExist()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY(LqCompare::Log::clearLogFile(QDir(dir.path()).filePath(QStringLiteral("none.log")),
                                         &error));
    QCOMPARE(error, QString());
}

void TstLogDiagnostics::clearLogFileRejectsAnEmptyPath()
{
    QString error;
    QVERIFY(!LqCompare::Log::clearLogFile(QString(), &error));
    QVERIFY(!error.isEmpty());
}

// =============================================================================
// D 历史文件枚举（第 2 条）
// =============================================================================

void TstLogDiagnostics::logHistoryFilesReturnsAscendingIndexes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("one")));
    QVERIFY(writeFile(logPath + QStringLiteral(".3"), QByteArray("three")));
    QVERIFY(writeFile(logPath + QStringLiteral(".10"), QByteArray("ten")));

    // 升序 = 从最新到最旧：诊断包按这个顺序拼接时，读起来与时间一致。
    QCOMPARE(asList(LqCompare::Log::logHistoryFiles(logPath)),
             asList(QStringList{logPath + QStringLiteral(".1"), logPath + QStringLiteral(".3"),
                                logPath + QStringLiteral(".10")}));
}

void TstLogDiagnostics::logHistoryFilesIgnoresLookalikeFiles()
{
    // 这些名字都很像历史文件。判错的代价是诊断包里混进无关文件，
    // 更糟的是轮转时把它们删掉——那是数据丢失。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("one")));
    QVERIFY(writeFile(logPath + QStringLiteral(".bak"), QByteArray("bak")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1.bak"), QByteArray("bak")));
    QVERIFY(writeFile(logPath + QStringLiteral(".tally"), QByteArray("tally")));

    QCOMPARE(asList(LqCompare::Log::logHistoryFiles(logPath)),
             asList(QStringList{logPath + QStringLiteral(".1")}));
}

void TstLogDiagnostics::logHistoryFilesIgnoresNamesTheGlobMatchedByAccident()
{
    // 日志名里带 `[` 时（用户在设置里可以写任意文件名），`QDir` 的 glob
    // `a[1].log.*` 会把 `a1.log.<数字>` 这类**别的文件**一起捞进来。若只靠
    // glob 的粗筛，`a1.log.12345` 会被当成序号 12345 的历史文件——而轮转的最后
    // 一步是删除，于是删掉的是一个用户根本没碰过的文件。
    //
    // 这条用例专门构造「只有前缀核准那一行拦得住」的输入：`mid()` 之后确实是
    // 一串数字，因此任何只看「剩下的是不是数字」的实现都会中招。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("a[1].log"));
    QVERIFY(writeFile(logPath, QByteArray("current")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("real history")));
    const QString decoy = QDir(dir.path()).filePath(QStringLiteral("a1.log.12345"));
    QVERIFY(writeFile(decoy, QByteArray("unrelated file")));

    QCOMPARE(asList(LqCompare::Log::logHistoryFiles(logPath)),
             asList(QStringList{logPath + QStringLiteral(".1")}));

    // 再走一次真实轮转：无关文件必须原样留在原地。
    RotationPolicy policy;
    policy.mode = RotationMode::Size;
    policy.maximumBytes = 1;
    policy.keepFiles = 1;
    QVERIFY(LqCompare::Log::applyLogRotation(logPath, policy,
                                             QDateTime(QDate(2026, 9, 21), QTime(6, 0))));
    QVERIFY(QFile::exists(decoy));
    QCOMPARE(readText(decoy), QStringLiteral("unrelated file"));
}

void TstLogDiagnostics::logHistoryFilesIsEmptyForAnEmptyPath()
{
    QVERIFY(LqCompare::Log::logHistoryFiles(QString()).isEmpty());
}

void TstLogDiagnostics::logHistoryFilesIsEmptyWhenThereIsNoHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(LqCompare::Log::logHistoryFiles(
                    QDir(dir.path()).filePath(QStringLiteral("lqcompare.log")))
                    .isEmpty());
}

// =============================================================================
// E 脱敏（第 5 条）
// =============================================================================

void TstLogDiagnostics::defaultRulesReplaceHomeAndStorageDirectory()
{
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QStringLiteral("/Users/loren/.config/LqCompare"));
    QCOMPARE(rules.size(), 2);
    QSet<QString> replacements;
    for (const RedactionRule &rule : rules)
        replacements.insert(rule.replacement);
    QVERIFY(replacements.contains(QStringLiteral("~")));
    QVERIFY(replacements.contains(QStringLiteral("<配置目录>")));
}

void TstLogDiagnostics::defaultRulesAreSortedLongestPrefixFirst()
{
    // 配置目录通常就在家目录下。若家目录那条先匹配，配置目录那一条永远轮不到，
    // 同一段路径在不同文件里会被替换成两种写法。
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QStringLiteral("/Users/loren/.config/LqCompare"));
    QCOMPARE(rules.size(), 2);
    QVERIFY(rules.first().prefix.size() > rules.last().prefix.size());
    QCOMPARE(rules.first().replacement, QStringLiteral("<配置目录>"));
}

void TstLogDiagnostics::homeDirectoryIsReplacedByTilde()
{
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QString());
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(
                     QStringLiteral("打开 /Users/loren/work/a.txt 失败"), rules),
             QStringLiteral("打开 ~/work/a.txt 失败"));
}

void TstLogDiagnostics::storageDirectoryIsReplacedByPlaceholder()
{
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QStringLiteral("/Users/loren/.config/LqCompare"));
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(
                     QStringLiteral("配置目录 /Users/loren/.config/LqCompare 已创建"), rules),
             QStringLiteral("配置目录 <配置目录> 已创建"));
}

void TstLogDiagnostics::lookalikeDirectoryNameIsNotReplaced()
{
    // 不做边界判定的实现会把 `/Users/loren2/proj` 换成 `~2/proj`——那份「已脱敏」
    // 的诊断包于是把一个不存在的路径写成了家目录下的东西，排查者会照着它去问问题。
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QString());
    const QString text = QStringLiteral("/Users/loren2/proj /Users/lorentoo/x");
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(text, rules), text);
}

void TstLogDiagnostics::prefixAtTheEndOfTextIsReplaced()
{
    // 串尾也是合法的边界：整行就是一个路径的日志很常见（「打开 X」的下半句）。
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QString());
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(QStringLiteral("/Users/loren"), rules),
             QStringLiteral("~"));
}

void TstLogDiagnostics::replacementCountAddsUpAcrossRules()
{
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QStringLiteral("/Users/loren/.config/LqCompare"));
    int count = 0;
    const QString result = LqCompare::Log::sanitizeDiagnosticText(
            QStringLiteral("/Users/loren/.config/LqCompare\n/Users/loren/a\n/Users/loren/b"),
            rules, &count);
    // 「一个都没替换」很可能意味着规则写错了（家目录传了空串），因此次数
    // 必须交回去让调用方能看出这件事。
    QCOMPARE(count, 3);
    QVERIFY(result.startsWith(QStringLiteral("<配置目录>")));
}

void TstLogDiagnostics::emptyRuleListKeepsTheTextUnchanged()
{
    int count = -1;
    const QString text = QStringLiteral("/Users/loren/a");
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(text, {}, &count), text);
    QCOMPARE(count, 0);
}

void TstLogDiagnostics::blankDirectoriesProduceNoRules()
{
    QVERIFY(LqCompare::Log::defaultRedactionRules(QString(), QString()).isEmpty());
    QVERIFY(LqCompare::Log::defaultRedactionRules(QStringLiteral("   "), QString()).isEmpty());
    // 配置目录与家目录相同时不重复造规则（结果会变成两条互相覆盖的规则）。
    QCOMPARE(LqCompare::Log::defaultRedactionRules(QStringLiteral("/Users/loren"),
                                                   QStringLiteral("/Users/loren/")).size(), 1);
}

void TstLogDiagnostics::textWithoutAnyMatchIsReturnedVerbatim()
{
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("/Users/loren"), QString());
    const QString text = QStringLiteral("没有路径的一段话：\n空洞测试\n");
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(text, rules), text);
}

void TstLogDiagnostics::windowsSeparatorCountsAsABoundary()
{
    // 反斜杠也是分隔符：日志里出现 `C:\Users\loren` 时，`C:\Users\loren2` 是
    // 另一个目录，而 `C:\Users\loren\a` 才是家目录下的东西。
    const QVector<RedactionRule> rules = LqCompare::Log::defaultRedactionRules(
            QStringLiteral("C:\\Users\\loren"), QString());
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(
                     QStringLiteral("C:\\Users\\loren\\a.txt"), rules),
             QStringLiteral("~\\a.txt"));
    QCOMPARE(LqCompare::Log::sanitizeDiagnosticText(
                     QStringLiteral("C:\\Users\\loren2\\a.txt"), rules),
             QStringLiteral("C:\\Users\\loren2\\a.txt"));
}

// =============================================================================
// F 环境报告与文件名（第 3 条）
// =============================================================================

void TstLogDiagnostics::environmentReportContainsEveryField()
{
    DiagnosticEnvironment environment = sampleEnvironment();
    environment.logFilePath = QStringLiteral("/tmp/lqcompare.log");
    environment.exportTime = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));
    const QString report = LqCompare::Log::environmentReport(environment);

    for (const QString &expectation : {QStringLiteral("0.1.0"), QStringLiteral("5.15.2"),
                                       QStringLiteral("macOS 26.0"), QStringLiteral("x86_64"),
                                       QStringLiteral("标准模式"), QStringLiteral("warning"),
                                       QStringLiteral("轮转已关闭"), QStringLiteral("/tmp/lqcompare.log")}) {
        QVERIFY2(report.contains(expectation), qPrintable(expectation));
    }
}

void TstLogDiagnostics::environmentReportSaysNoFileLoggingWhenThePathIsEmpty()
{
    DiagnosticEnvironment environment = sampleEnvironment();
    environment.logFilePath.clear();
    QVERIFY(LqCompare::Log::environmentReport(environment)
                    .contains(QStringLiteral("未启用文件日志")));
}

void TstLogDiagnostics::environmentReportFallsBackToAPlaceholder()
{
    // 缺字段时写「（未知）」而不是空：空值会让报告里出现 `程序版本：` 这样
    // 半截的行，读的人分不清「没填」与「填了空串」。
    const QString report = LqCompare::Log::environmentReport(DiagnosticEnvironment());
    QVERIFY(report.contains(QStringLiteral("（未知）")));
    QVERIFY(report.contains(QStringLiteral("程序版本：")));
}

void TstLogDiagnostics::environmentReportCarriesTheExportTime()
{
    DiagnosticEnvironment environment = sampleEnvironment();
    environment.exportTime = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));
    // 时间由调用方传入，因此这一行可以被逐字断言（模块自己不取当前时间）。
    QVERIFY(LqCompare::Log::environmentReport(environment)
                    .contains(QStringLiteral("2026-09-21T06:15:00")));
}

void TstLogDiagnostics::bundleFileNamesAreStableAndDistinct()
{
    const QString manifest = LqCompare::Log::manifestFileName();
    const QString environment = LqCompare::Log::environmentReportFileName();
    const QString logs = LqCompare::Log::logArchiveDirectoryName();
    QCOMPARE(manifest, QStringLiteral("manifest.json"));
    QCOMPARE(environment, QStringLiteral("environment.txt"));
    QCOMPARE(logs, QStringLiteral("logs"));
    // 三者在同一个目录里，重名会让环境报告把清单覆盖掉。
    const QSet<QString> names {manifest, environment, logs};
    QCOMPARE(names.size(), 3);
}

// =============================================================================
// G 导出前提示（第 5 条）
// =============================================================================

void TstLogDiagnostics::noticeAlwaysMentionsPaths()
{
    // 两种口径都必须说出「里面会有路径」——这是用户决定要不要发出去的唯一依据。
    for (bool redact : {true, false})
        QVERIFY(LqCompare::Log::diagnosticNoticeText(redact).contains(QStringLiteral("路径")));
}

void TstLogDiagnostics::redactedNoticeExplainsWhatGetsReplaced()
{
    const QString notice = LqCompare::Log::diagnosticNoticeText(true);
    QVERIFY(notice.contains(QStringLiteral("~")));
    QVERIFY(notice.contains(QStringLiteral("脱敏")));
    // 脱敏只替换目录前缀，不改日志内容本身——这一点必须说出来，否则用户
    // 会以为包里的东西已经被「处理过」了。
    QVERIFY(notice.contains(QStringLiteral("不做删改")));
}

void TstLogDiagnostics::rawNoticeWarnsAboutUserNames()
{
    const QString notice = LqCompare::Log::diagnosticNoticeText(false);
    QVERIFY(notice.contains(QStringLiteral("不脱敏")));
    QVERIFY(notice.contains(QStringLiteral("用户名")));
}

void TstLogDiagnostics::noticesDifferBetweenModes()
{
    // 这两种口径若写成同一句话，用户就没有任何依据去判断要不要勾脱敏。
    QVERIFY(LqCompare::Log::diagnosticNoticeText(true)
            != LqCompare::Log::diagnosticNoticeText(false));
}

// =============================================================================
// H 诊断包（第 3、5 条）
// =============================================================================

void TstLogDiagnostics::bundleCreatesATimestampedDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DiagnosticBundleRequest request;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(QFileInfo(result.bundleDirectory).fileName(),
             QStringLiteral("lqcompare-diagnostics-20260921-061500"));
    QCOMPARE(QFileInfo(result.bundleDirectory).absolutePath(), QDir(dir.path()).absolutePath());
    QVERIFY(QFileInfo(result.bundleDirectory).isDir());
}

void TstLogDiagnostics::bundleWritesManifestEnvironmentAndLog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("一条日志\n")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.logIncluded);
    QVERIFY(QFile::exists(result.bundleDirectory + QStringLiteral("/logs/lqcompare.log")));
    QVERIFY(QFile::exists(result.bundleDirectory + QStringLiteral("/environment.txt")));
    QVERIFY(QFile::exists(result.bundleDirectory + QStringLiteral("/manifest.json")));
    QVERIFY(readText(result.bundleDirectory + QStringLiteral("/logs/lqcompare.log"))
                    .contains(QStringLiteral("一条日志")));
}

void TstLogDiagnostics::bundleFilesComeInLogThenEnvironmentThenManifestOrder()
{
    // 清单里的顺序决定用户打开目录时看到的顺序（也是文件列表的展示顺序）：
    // 日志在最前、清单在最后。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("x")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(asList(result.files),
             asList(QStringList{QStringLiteral("logs/lqcompare.log"),
                                LqCompare::Log::environmentReportFileName(),
                                LqCompare::Log::manifestFileName()}));
}

void TstLogDiagnostics::bundleArchivesRotatedHistory()
{
    // 用户报告的问题很可能发生在轮转之前的那一份里，「只有当前文件」的诊断包
    // 会让排查者看不到出事那一刻的日志。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("current\n")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("older\n")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(QFile::exists(result.bundleDirectory + QStringLiteral("/logs/lqcompare.log.1")));
    QCOMPARE(asList(result.archivedLogs),
             asList(QStringList{logPath, logPath + QStringLiteral(".1")}));
    QVERIFY(readText(result.bundleDirectory + QStringLiteral("/logs/lqcompare.log.1"))
                    .contains(QStringLiteral("older")));
}

void TstLogDiagnostics::bundleRedactsLogContentByDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath,
                      QByteArray("开始：/Users/loren/.config/LqCompare\n"
                                 "错误：/Users/loren/work/a.txt\n"
                                 "别人：/Users/loren2/x\n")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    // 环境信息里放一个**不含规则前缀**的目录，把这一条用例的计数收窄到
    // 「归档日志里的替换次数」上：否则环境报告里那行「配置目录」也会计入，
    // 用例就变成「报告格式改了它就会红」。
    DiagnosticEnvironment environment = sampleEnvironment();
    environment.storageDirectory = QStringLiteral("/tmp/lqcompare-config");
    environment.logFilePath = QStringLiteral("/tmp/lqcompare.log");
    request.environment = environment;
    request.homeDirectory = QStringLiteral("/Users/loren");
    request.storageDirectory = QStringLiteral("/Users/loren/.config/LqCompare");
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QString archived = readText(result.bundleDirectory + QStringLiteral("/logs/lqcompare.log"));
    QVERIFY(archived.contains(QStringLiteral("<配置目录>")));
    QVERIFY(archived.contains(QStringLiteral("~/work/a.txt")));
    QVERIFY(!archived.contains(QStringLiteral("/Users/loren/")));
    // 前缀相同但是别的目录：必须原样保留。
    QVERIFY(archived.contains(QStringLiteral("/Users/loren2/x")));
    QVERIFY(result.redacted);
    // 配置目录那一处 + 家目录那一处 = 2 次；`/Users/loren2` 不算。
    QCOMPARE(result.redactedOccurrences, 2);
}

void TstLogDiagnostics::bundleKeepsRawContentWhenRedactionIsOff()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("错误：/Users/loren/work/a.txt\n")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.homeDirectory = QStringLiteral("/Users/loren");
    request.storageDirectory = QStringLiteral("/Users/loren/.config/LqCompare");
    request.redactPaths = false;
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(readText(result.bundleDirectory + QStringLiteral("/logs/lqcompare.log")),
             QStringLiteral("错误：/Users/loren/work/a.txt\n"));
    QVERIFY(!result.redacted);
    QCOMPARE(result.redactedOccurrences, 0);
}

void TstLogDiagnostics::bundleRedactsTheEnvironmentReport()
{
    // 「日志脱敏了、环境信息没脱」是最容易漏的一种：报告开头那几行正是
    // 一眼就能看到的地方。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DiagnosticBundleRequest request;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.homeDirectory = QStringLiteral("/Users/loren");
    request.storageDirectory = QStringLiteral("/Users/loren/.config/LqCompare");
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QString report = readText(result.bundleDirectory + QStringLiteral("/environment.txt"));
    QVERIFY(report.contains(QStringLiteral("<配置目录>")));
    QVERIFY(!report.contains(QStringLiteral("/Users/loren")));
}

void TstLogDiagnostics::manifestRecordsTheRedactionState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    for (bool redact : {true, false}) {
        DiagnosticBundleRequest request;
        request.outputDirectory = dir.path();
        request.environment = sampleEnvironment();
        request.homeDirectory = QStringLiteral("/Users/loren");
        request.redactPaths = redact;
        request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

        const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const QJsonObject manifest = QJsonDocument::fromJson(
                readFile(result.bundleDirectory + QStringLiteral("/manifest.json"))).object();
        // 接收方据此判断这份包能不能公开，而不必逐个文件去找有没有漏掉的用户名。
        QCOMPARE(manifest.value(QStringLiteral("redacted")).toBool(), redact);
    }
}

void TstLogDiagnostics::manifestCountsRedactedOccurrences()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("/Users/loren/a\n/Users/loren/b\n")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    // 同上：环境信息里放一个不含规则前缀的目录，让计数只反映归档日志。
    DiagnosticEnvironment environment = sampleEnvironment();
    environment.storageDirectory = QStringLiteral("/tmp/lqcompare-config");
    environment.logFilePath = QStringLiteral("/tmp/lqcompare.log");
    request.environment = environment;
    request.homeDirectory = QStringLiteral("/Users/loren");
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QJsonObject manifest = QJsonDocument::fromJson(
            readFile(result.bundleDirectory + QStringLiteral("/manifest.json"))).object();
    QCOMPARE(manifest.value(QStringLiteral("redactedOccurrences")).toInt(),
             result.redactedOccurrences);
    QCOMPARE(manifest.value(QStringLiteral("redactedOccurrences")).toInt(), 2);
}

void TstLogDiagnostics::manifestListsEveryFileInTheBundle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("x")));
    QVERIFY(writeFile(logPath + QStringLiteral(".1"), QByteArray("y")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QJsonObject manifest = QJsonDocument::fromJson(
            readFile(result.bundleDirectory + QStringLiteral("/manifest.json"))).object();

    QStringList listed;
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files)
        listed << value.toString();
    QCOMPARE(asList(listed), asList(result.files));

    // 清单里放的是日志的**文件名**而不是绝对路径：清单是最可能被单独贴出来的
    // 那一段，在里面写一串绝对路径等于把脱敏绕过去了。
    const QJsonArray logFiles = manifest.value(QStringLiteral("logFiles")).toArray();
    QCOMPARE(logFiles.size(), 2);
    QCOMPARE(logFiles.first().toString(), QStringLiteral("lqcompare.log"));
    for (const QJsonValue &value : logFiles)
        QVERIFY(!value.toString().contains(QLatin1Char('/')));
}

void TstLogDiagnostics::manifestIsParseableJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DiagnosticBundleRequest request;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
            readFile(result.bundleDirectory + QStringLiteral("/manifest.json")), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    const QJsonObject manifest = document.object();
    QCOMPARE(manifest.value(QStringLiteral("format")).toString(),
             QStringLiteral("LqCompare.Diagnostics"));
    QCOMPARE(manifest.value(QStringLiteral("logIncluded")).toBool(), false);
    QVERIFY(manifest.contains(QStringLiteral("qtVersion")));
}

void TstLogDiagnostics::bundleWithoutAFileLogStillSucceeds()
{
    // 没启用文件日志不是失败：环境信息本身就有诊断价值，包内如实说明即可。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DiagnosticBundleRequest request;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.logIncluded);
    QVERIFY(result.archivedLogs.isEmpty());
    QVERIFY(readText(result.bundleDirectory + QStringLiteral("/environment.txt"))
                    .contains(QStringLiteral("未启用文件日志")));
}

void TstLogDiagnostics::bundleFillsTheEnvironmentLogPathFromTheRequest()
{
    // 界面只填「日志文件在哪」这一件事，环境信息里的那一行由服务层补齐：
    // 两处都填的话必然会出现「报告里写的是 A、实际收的是 B」。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = QDir(dir.path()).filePath(QStringLiteral("lqcompare.log"));
    QVERIFY(writeFile(logPath, QByteArray("x")));

    DiagnosticBundleRequest request;
    request.logFilePath = logPath;
    request.outputDirectory = dir.path();
    DiagnosticEnvironment environment = sampleEnvironment();
    environment.logFilePath.clear();
    request.environment = environment;
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(readText(result.bundleDirectory + QStringLiteral("/environment.txt"))
                    .contains(QFileInfo(logPath).fileName()));
}

void TstLogDiagnostics::bundleReportsAnEmptyOutputDirectoryAsFailure()
{
    DiagnosticBundleRequest request;
    request.environment = sampleEnvironment();
    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.bundleDirectory.isEmpty());
    QVERIFY(result.files.isEmpty());
}

void TstLogDiagnostics::bundleReportsAnUnwritableOutputDirectoryAsFailure()
{
    // 「输出目录建不出来」必须是一次**明确的失败**：一个内容不全、名字却与正常
    // 诊断包一模一样的目录，比导出失败糟得多——用户会把它发出去。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString blocker = QDir(dir.path()).filePath(QStringLiteral("blocker"));
    QVERIFY(writeFile(blocker, QByteArray("not a directory")));

    DiagnosticBundleRequest request;
    request.outputDirectory = blocker + QStringLiteral("/nested");
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult result = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("无法创建诊断包目录")));
    QVERIFY(result.bundleDirectory.isEmpty());
    QVERIFY(result.files.isEmpty());
}

void TstLogDiagnostics::secondBundleInTheSameSecondGetsASuffix()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DiagnosticBundleRequest request;
    request.outputDirectory = dir.path();
    request.environment = sampleEnvironment();
    request.now = QDateTime(QDate(2026, 9, 21), QTime(6, 15, 0));

    const DiagnosticBundleResult first = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(first.ok, qPrintable(first.error));
    const DiagnosticBundleResult second = LqCompare::Log::buildDiagnosticBundle(request);
    QVERIFY2(second.ok, qPrintable(second.error));

    // 绝不复用已有目录：两份诊断包混在一起之后，没人说得清哪一行日志是哪次问题的。
    QVERIFY(first.bundleDirectory != second.bundleDirectory);
    QCOMPARE(QFileInfo(second.bundleDirectory).fileName(),
             QStringLiteral("lqcompare-diagnostics-20260921-061500-2"));
}

// =============================================================================
// I 源码级护栏
// =============================================================================

QString TstLogDiagnostics::codeRoot()
{
    return QString::fromUtf8(LQCOMPARE_CODE_ROOT);
}

QString TstLogDiagnostics::readSource(const QString &relativePath)
{
    QFile file(codeRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString TstLogDiagnostics::stripComments(const QString &source)
{
    QString result;
    result.reserve(source.size());
    int index = 0;
    while (index < source.size()) {
        if (source.at(index) == QLatin1Char('/') && index + 1 < source.size()
            && source.at(index + 1) == QLatin1Char('*')) {
            index += 2;
            while (index < source.size()
                   && !(source.at(index) == QLatin1Char('*') && index + 1 < source.size()
                        && source.at(index + 1) == QLatin1Char('/')))
                ++index;
            index += 2;
            continue;
        }
        if (source.at(index) == QLatin1Char('/') && index + 1 < source.size()
            && source.at(index + 1) == QLatin1Char('/')) {
            while (index < source.size() && source.at(index) != QLatin1Char('\n')) ++index;
            continue;
        }
        result.append(source.at(index));
        ++index;
    }
    return result;
}

void TstLogDiagnostics::moduleNeverIncludesAnyViewHeader()
{
    for (const QString &path : QStringList{QStringLiteral("Services/Log/logfiles.h"),
                                           QStringLiteral("Services/Log/logfiles.cpp"),
                                           QStringLiteral("Services/Log/diagnostics.h"),
                                           QStringLiteral("Services/Log/diagnostics.cpp")}) {
        const QString code = stripComments(readSource(path));
        QVERIFY2(!code.isEmpty(), qPrintable(path));
        QVERIFY2(!code.contains(QStringLiteral("\"Views/")), qPrintable(path));
    }
}

void TstLogDiagnostics::moduleNeverUsesUiOnlyApi()
{
    // 「打开日志目录」这种动作留在设置页里，正是为了这两个模块保持纯 QtCore、
    // 可以在没有图形环境的机器上跑。一旦有人把 QMessageBox 塞进来，
    // 本工程（`QT -= gui`）会先构建失败；这条护栏是第二层，给主构建用。
    const QStringList forbidden {QStringLiteral("QMessageBox"), QStringLiteral("QDesktopServices"),
                                 QStringLiteral("QFileDialog"), QStringLiteral("QDialog")};
    for (const QString &path : QStringList{QStringLiteral("Services/Log/logfiles.h"),
                                           QStringLiteral("Services/Log/logfiles.cpp"),
                                           QStringLiteral("Services/Log/diagnostics.h"),
                                           QStringLiteral("Services/Log/diagnostics.cpp")}) {
        const QString code = stripComments(readSource(path));
        for (const QString &token : forbidden)
            QVERIFY2(!code.contains(token), qPrintable(path + QStringLiteral(" 使用了 ") + token));
    }
}

void TstLogDiagnostics::sourceGuardWouldCatchAnInjectedUiCall()
{
    // 成对断言：既证明 stripComments 不会把代码里的调用一起去掉（否则护栏永远绿），
    // 也证明它确实会把注释里的词去掉（否则护栏永远红——本模块的头文件里
    // 逐字写着这些词）。
    const QString injected = QStringLiteral(
            "// QMessageBox is only mentioned in this comment\n"
            "QMessageBox::warning(parent, title, text);\n");
    QCOMPARE(injected.count(QStringLiteral("QMessageBox")), 2);
    const QString code = stripComments(injected);
    QCOMPARE(code.count(QStringLiteral("QMessageBox")), 1);
    QVERIFY(code.contains(QStringLiteral("QMessageBox::warning")));
    QVERIFY(!code.contains(QStringLiteral("only mentioned")));
}

QTEST_MAIN(TstLogDiagnostics)
