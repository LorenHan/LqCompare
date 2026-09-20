#ifndef LQCOMPARE_CLIOPTIONS_H
#define LQCOMPARE_CLIOPTIONS_H

#include "textdiff.h"
#include <QMap>
#include <QStringList>
#include <QVector>

namespace LqCompare { namespace Cli {

enum class Platform { Native, Posix, Windows };
enum ExitCode { Equal = 0, Different = 1, UsageError = 2, DataError = 3, InternalError = 4 };

struct Option {
    QString name;
    QStringList aliases;
    QString valueName;
    QString group;
    QString description;
    bool repeatable = false;
};

struct Request {
    QString left, right, base, output;
    QString sessionType; // Empty means automatic; otherwise the stable registry ID.
    bool silent = false;
    bool quickCompare = false;
    bool leftReadOnly = false;
    bool rightReadOnly = false;
    bool newInstance = false;
    bool wait = false;
    bool json = false;
    bool help = false;
    QString helpTopic;
    bool version = false;
    bool listSessionTypes = false;
    bool printExitCodes = false;
    bool recursive = true;
    Text::CompareOptions textOptions;
    QByteArray encoding;
    QString logFile;
    bool noLogFile = false;
    QString logLevel = QStringLiteral("info");
    QString reportFormat;
    QString reportFile;
    bool reportOnDiffOnly = false;
    QString scriptFile;
    QMap<QString, QString> scriptArguments;
    bool continueOnError = false;
};

struct ParseResult {
    Request request;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// argv WITHOUT argv[0]. Shell quoting is already removed by the operating system.
// This function performs no filesystem access and does not touch settings.
ParseResult parse(const QStringList &arguments, Platform platform = Platform::Native);
const QVector<Option> &options();
QString helpText(const QString &topic = {});
QString exitCodesText();
QString sessionTypesText();
bool requiresHeadless(const Request &request);
QStringList inputPaths(const Request &request);

} }
#endif
