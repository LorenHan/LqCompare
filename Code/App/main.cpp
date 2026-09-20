#include "MainWindow.h"

#include "commandregistry.h"
#include "logging.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef LQCOMPARE_VERSION
#define LQCOMPARE_VERSION "0.0.0-dev"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LqCompare"));
    QApplication::setApplicationVersion(QStringLiteral(LQCOMPARE_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Ailecium"));
    QApplication::setOrganizationDomain(QStringLiteral("ailecium.org"));

    // --- 日志与诊断（ENG-006） ---------------------------------------------
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString logPath = dataDir + QStringLiteral("/lqcompare.log");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("LqCompare — file and folder comparison."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption logLevelOption(
        QStringList{QStringLiteral("log-level")},
        QStringLiteral("Log level: error, warning, info, debug, trace."),
        QStringLiteral("level"), QStringLiteral("warning"));
    parser.addOption(logLevelOption);
    const QCommandLineOption noLogFileOption(
        QStringList{QStringLiteral("no-log-file")}, QStringLiteral("Do not write a log file."));
    parser.addOption(noLogFileOption);
    parser.addPositionalArgument(
        QStringLiteral("paths"),
        QStringLiteral("Files or folders to compare. Command line handling is specified in CLI-001."),
        QStringLiteral("[paths...]"));
    parser.process(app);

    // 级别的解析只有一份实现（`Log::levelFromName`）：写在这里过的话，
    // 以后加一个级别就会出现「日志模块认、命令行不认」的分歧，
    // 而用户看到的只是「未知的日志级别」，看不出其实是没同步。
    LqCompare::Log::Level level = LqCompare::Log::Level::Warning;
    const QString levelText = parser.value(logLevelOption);
    if (!LqCompare::Log::levelFromName(levelText, &level)) {
        // 认不出来时用默认值继续，并**说清楚**：静默降级会让用户以为
        // `--log-level debgu` 生效了，然后奇怪为什么日志里什么都没有。
        level = LqCompare::Log::Level::Warning;
        LQCOMPARE_WARN("app", QStringLiteral("无法识别的日志级别「%1」，改用 %2")
                                      .arg(levelText, QString::fromLatin1(
                                                              LqCompare::Log::levelIdentifier(level))));
    }
    LqCompare::Log::setLevel(level);

    if (!parser.isSet(noLogFileOption) && !LqCompare::Log::setLogFile(logPath)) {
        LQCOMPARE_WARN("app", QStringLiteral("无法写入日志文件：%1").arg(logPath));
    }

    LQCOMPARE_INFO("app",
                   QStringLiteral("启动 LqCompare %1（Qt %2，%3，日志级别 %4）")
                           .arg(QStringLiteral(LQCOMPARE_VERSION),
                                QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(),
                                QString::fromLatin1(LqCompare::Log::levelIdentifier(level))));

    LqCompare::MainWindow window;

    // 命令注册表自检（UI-023 / UI-025）：缺失说明、缺失图标、快捷键冲突都会在这里暴露。
    const QStringList problems = LqCompare::CommandRegistry::instance().validate();
    for (const QString &problem : problems) {
        LQCOMPARE_ERROR("command", problem);
    }
    if (!problems.isEmpty()) {
        LQCOMPARE_WARN("app",
                       QStringLiteral("命令注册表有 %1 项问题，详见上方日志（这些是尚未补齐的规格条目）")
                               .arg(problems.size()));
    }

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        // 位置参数的处理（会话类型判定、语言选择、返回码）按 CLI-001 ~ CLI-012 实现。
        LQCOMPARE_WARN("cli", QStringLiteral("命令行数据源尚未接入（CLI-001）：%1")
                                      .arg(positional.join(QLatin1Char(' '))));
    }

    window.resize(1280, 820);
    window.show();
    return app.exec();
}
