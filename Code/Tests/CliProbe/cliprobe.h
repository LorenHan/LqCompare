#ifndef LQCOMPARE_TEST_CLIPROBE_H
#define LQCOMPARE_TEST_CLIPROBE_H

#include "cliexecution.h"
#include "scriptengine.h"
#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <cstdio>

// Shared by the self-contained QtTest child process and the optional standalone
// probe. QCoreApplication is the only application object in either path.
inline int runCliProbe(QCoreApplication &app)
{
    QCoreApplication::setApplicationName(QStringLiteral("LqCompareCliProbe"));
    QCoreApplication::setOrganizationName(QStringLiteral("LqCompareTests"));
    const QString settingsRoot = qEnvironmentVariable("LQCOMPARE_CLI_TEST_CONFIG_HOME");
    if (!settingsRoot.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);
    }
    const auto parsed = LqCompare::Cli::parse(app.arguments().mid(1));
    const auto result = parsed.ok()
        ? (parsed.request.scriptFile.isEmpty()
           ? LqCompare::Cli::execute(parsed.request, QStringLiteral("probe-1.0"))
           : LqCompare::Script::executeFile(parsed.request, QStringLiteral("probe-1.0")))
        : LqCompare::Cli::errorResult(LqCompare::Cli::UsageError, parsed.error,
                                      parsed.request.json);
    QFile output;
    if (output.open(stdout, QIODevice::WriteOnly)) {
        output.write(result.standardOutput.toUtf8());
        output.flush();
    }
    QFile error;
    if (error.open(stderr, QIODevice::WriteOnly)) {
        error.write(result.standardError.toUtf8());
        error.flush();
    }
    return result.exitCode;
}

#endif
