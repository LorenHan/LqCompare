#include "MainWindow.h"
#include "clioptions.h"
#include "processarguments.h"
#include "cliexecution.h"
#include "scriptengine.h"
#include "commandregistry.h"
#include "logging.h"
#include "optionsrepository.h"
#include "optionsruntime.h"
#include "singleinstance.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <memory>

#ifndef LQCOMPARE_VERSION
#define LQCOMPARE_VERSION "0.0.0-dev"
#endif

namespace {
int printResult(const LqCompare::Cli::ExecutionResult &result)
{
    QFile output, errors;
    output.open(stdout, QIODevice::WriteOnly);
    errors.open(stderr, QIODevice::WriteOnly);
    output.write(result.standardOutput.toUtf8());
    errors.write(result.standardError.toUtf8());
    output.flush(); errors.flush();
    return result.exitCode;
}
}

int main(int argc, char *argv[])
{
    using namespace LqCompare;
    // 提前分类与正式执行必须读取同一套原生参数，不能按窄 argv 的错误数量选应用类型。
    const auto initialArguments = Cli::initialProcessArguments(argc, argv);
    if (!initialArguments.ok())
        return printResult(Cli::errorResult(Cli::UsageError, initialArguments.error));
    const auto early = Cli::parse(initialArguments.values.mid(1));
    const bool headless = !early.ok() || Cli::requiresHeadless(early.request);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    std::unique_ptr<QCoreApplication> app;
    if (headless) app.reset(new QCoreApplication(argc, argv));
    else app.reset(new QApplication(argc, argv));
    QCoreApplication::setApplicationName(QStringLiteral("LqCompare"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LQCOMPARE_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Ailecium"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("ailecium.org"));
    const auto nativeArguments = Cli::processArguments();
    if (!nativeArguments.ok())
        return printResult(Cli::errorResult(Cli::UsageError, nativeArguments.error));
    const QStringList arguments = nativeArguments.values.mid(1);
    const auto parsed = Cli::parse(arguments);
    if (!parsed.ok()) return printResult(Cli::errorResult(Cli::UsageError, parsed.error));
    const auto &request = parsed.request;
    if (Cli::requiresHeadless(request)) {
        const auto result = request.scriptFile.isEmpty()
            ? Cli::execute(request, QStringLiteral(LQCOMPARE_VERSION))
            : Script::executeFile(request, QStringLiteral(LQCOMPARE_VERSION));
        return printResult(result);
    }

    Settings::OptionsRepository options;
    const auto loaded = options.load();
    Options::OptionsRuntime runtime(&options, app.get());
    if (!loaded.ok) LQCOMPARE_WARN("settings", loaded.error);
    if (!runtime.lastError().isEmpty()) LQCOMPARE_WARN("settings", runtime.lastError());
    if (arguments.contains(QStringLiteral("--log-level"))) {
        Log::Level level;
        if (Log::levelFromName(request.logLevel, &level)) Log::setLevel(level);
    }
    if (request.noLogFile) Log::setLogFile({});
    else if (!request.logFile.isEmpty()) {
        QString error;
        if (!Cli::isSafeOutputPath(request.logFile, Cli::inputPaths(request), &error))
            return printResult(Cli::errorResult(Cli::UsageError, error));
        if (!Log::setLogFile(request.logFile))
            return printResult(Cli::errorResult(Cli::DataError, QStringLiteral("Cannot open log file.")));
    }

    Platform::SingleInstanceGuard instance;
    instance.setEnabled(options.value(QStringLiteral("general.singleInstance")).toBool()
                        && !request.newInstance && !request.wait);
    Platform::RelayRequest relay;
    relay.workingDirectory = QDir::currentPath();
    relay.arguments = arguments;
    instance.setRelayRequest(relay);
    const auto started = instance.start(QStringLiteral("org.lqcompare.desktop.v1"));
    if (started.shouldExit()) return started.exitCode();
    LQCOMPARE_INFO("instance", started.detail);

    MainWindow window;
    window.setOptions(&options, &runtime);
    if (request.wait) window.configureWaitMode(request.sessionType == QLatin1String("text-merge") || !request.base.isEmpty() || !request.output.isEmpty());
    QObject::connect(&instance, &Platform::SingleInstanceGuard::relayReceived, &window,
                     [&window](const Platform::RelayRequest &received) {
        const auto parsedRelay = Cli::parse(received.arguments);
        if (!parsedRelay.ok() || Cli::requiresHeadless(parsedRelay.request)) return;
        window.openRequest(parsedRelay.request, received.workingDirectory);
    });
    QObject::connect(&instance, &Platform::SingleInstanceGuard::activationRequested, &window,
                     [&window](const QStringList &, bool raiseWindow) {
        if (raiseWindow) { window.showNormal(); window.raise(); window.activateWindow(); }
    });
    for (const auto &problem : CommandRegistry::instance().validate()) LQCOMPARE_ERROR("command", problem);
    window.show();
    if (!window.openRequest(request)) {
        if (request.wait) return Cli::DataError;
    }
    return app->exec();
}
