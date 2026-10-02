#include "processarguments.h"

#include <QCoreApplication>

namespace LqCompare { namespace Cli {

ProcessArguments initialProcessArguments(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return processArguments();
#else
    ProcessArguments result;
    if (argc < 1 || !argv) {
        result.error = QStringLiteral("The process command line is unavailable.");
        return result;
    }
    for (int index = 0; index < argc; ++index) {
        if (!argv[index]) {
            result.values.clear();
            result.error = QStringLiteral("The process command line contains a missing argument.");
            return result;
        }
        result.values.append(QString::fromLocal8Bit(argv[index]));
    }
    return result;
#endif
}

#ifndef Q_OS_WIN
ProcessArguments processArguments()
{
    ProcessArguments result;
    if (!QCoreApplication::instance()) {
        result.error = QStringLiteral("The application has not been initialized.");
        return result;
    }
    result.values = QCoreApplication::arguments();
    return result;
}
#endif

} }
