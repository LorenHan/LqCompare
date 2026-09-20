#ifndef LQCOMPARE_CLIEXECUTION_H
#define LQCOMPARE_CLIEXECUTION_H

#include "clioptions.h"
#include <QJsonObject>

namespace LqCompare { namespace Cli {

struct ExecutionResult {
    int exitCode = Equal;
    QString standardOutput;
    QString standardError;
    QJsonObject summary;
    bool ok() const { return exitCode <= Different; }
};

// Synchronous QtCore service. Only two-way text/folder comparisons are supported
// headlessly. Merge output is never written. Scripts use Script::executeFile.
// protectedPaths lets callers protect a script and all of its inputs from output.
ExecutionResult execute(const Request &request, const QString &version = QStringLiteral("dev"),
                        const QStringList &protectedPaths = {});
ExecutionResult errorResult(int exitCode, const QString &message, bool json = false);
// Writes a summary (txt/csv/json/html), atomically. Rejects replacing an input or
// writing inside an input directory, including through symbolic-link aliases.
bool writeReport(const QString &path, const QString &format, const ExecutionResult &result,
                 const QStringList &protectedPaths, QString *error = nullptr);
bool isSafeOutputPath(const QString &path, const QStringList &protectedPaths,
                      QString *error = nullptr);

} }
#endif
