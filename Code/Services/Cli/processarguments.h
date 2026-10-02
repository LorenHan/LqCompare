#ifndef LQCOMPARE_PROCESSARGUMENTS_H
#define LQCOMPARE_PROCESSARGUMENTS_H

#include <QStringList>

namespace LqCompare { namespace Cli {

struct ProcessArguments {
    // 与 Qt arguments() 一致，首项保留可执行文件名，包括原始拼写。
    QStringList values;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// 创建应用前决定 GUI / 无界面模式；Windows 与创建后的入口共用宽字符解码。
ProcessArguments initialProcessArguments(int argc, char *argv[]);
ProcessArguments processArguments();

#ifdef Q_OS_WIN
// 保留 CommandLineToArgvW 的引号、反斜杠与空参数语义；不展开通配符。
ProcessArguments windowsCommandLineArguments(const QString &commandLine);
#endif

} }
#endif
