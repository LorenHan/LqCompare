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

    const QString levelName = parser.value(logLevelOption).toLower();
    LqCompare::Log::Level level = LqCompare::Log::Level::Warning;
    if (levelName == QLatin1String("error")) {
        level = LqCompare::Log::Level::Error;
    } else if (levelName == QLatin1String("info")) {
        level = LqCompare::Log::Level::Info;
    } else if (levelName == QLatin1String("debug")) {
        level = LqCompare::Log::Level::Debug;
    } else if (levelName == QLatin1String("trace")) {
        level = LqCompare::Log::Level::Trace;
    }
    LqCompare::Log::setLevel(level);
    if (!parser.isSet(noLogFileOption) && !LqCompare::Log::setLogFile(logPath)) {
        LqCompare::Log::write(LqCompare::Log::Level::Warning, QStringLiteral("app"),
                              QStringLiteral("无法写入日志文件：%1").arg(logPath));
    }

    LqCompare::Log::write(LqCompare::Log::Level::Info, QStringLiteral("app"),
                          QStringLiteral("启动 LqCompare %1（Qt %2，%3）")
                              .arg(QStringLiteral(LQCOMPARE_VERSION),
                                   QString::fromLatin1(qVersion()), QSysInfo::prettyProductName()));

    LqCompare::MainWindow window;

    // 命令注册表自检（UI-023 / UI-025）：缺失说明、缺失图标、快捷键冲突都会在这里暴露。
    const QStringList problems = LqCompare::CommandRegistry::instance().validate();
    for (const QString &problem : problems) {
        LqCompare::Log::write(LqCompare::Log::Level::Error, QStringLiteral("command"), problem);
    }
    if (!problems.isEmpty()) {
        LqCompare::Log::write(
            LqCompare::Log::Level::Warning, QStringLiteral("app"),
            QStringLiteral("命令注册表有 %1 项问题，详见上方日志（这些是尚未补齐的规格条目）")
                .arg(problems.size()));
    }

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        // 位置参数的处理（会话类型判定、语言选择、返回码）按 CLI-001 ~ CLI-012 实现。
        LqCompare::Log::write(
            LqCompare::Log::Level::Warning, QStringLiteral("cli"),
            QStringLiteral("命令行数据源尚未接入（CLI-001）：%1").arg(positional.join(QLatin1Char(' '))));
    }

    window.resize(1280, 820);
    window.show();
    return app.exec();
}
