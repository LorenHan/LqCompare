#ifndef LQCOMPARE_SCRIPTENGINE_H
#define LQCOMPARE_SCRIPTENGINE_H

#include "cliexecution.h"
#include <QVector>

namespace LqCompare { namespace Script {

struct Diagnostic {
    int line = 0;
    QString command;
    QString message;
};

struct CommandDefinition {
    QString name;
    QString usage;
    QString description;
};

struct Command {
    enum class Kind { Load, Compare, Report, Set, Print, Log };
    Kind kind = Kind::Load;
    int line = 0;
    QString name;
    Cli::Request request;
    QString text;
    QString reportPath;
    QString reportFormat;
    bool reportOnDiffOnly = false;
};

struct Program {
    QString scriptFile;
    QVector<Command> commands;
    // Includes ALL declared sources, even those loaded after an output command.
    QStringList protectedPaths;
};

struct ParseResult {
    Program program;
    QVector<Diagnostic> diagnostics;
    bool ok() const { return diagnostics.isEmpty(); }
};

const QVector<CommandDefinition> &commands();
QString helpText();

// Parses and validates the whole script before execution; never writes files.
// Relative script paths resolve against scriptFile's containing directory.
// Comparison options are parsed by Cli::parse, using the same option table.
ParseResult parse(const QString &source, const QString &scriptFile,
                  const Cli::Request &defaults = {},
                  const QString &version = QStringLiteral("dev"));

// QtCore-only synchronous entry point for App/main.cpp. Input files are read-only.
// Runtime failures stop by default; continueOnError accumulates all failures.
// The final exit code is the most severe comparison/command result (CLI-004).
Cli::ExecutionResult executeFile(const Cli::Request &request,
                                 const QString &version = QStringLiteral("dev"));

} }
#endif
