#include "processarguments.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <memory>

namespace LqCompare { namespace Cli {

ProcessArguments windowsCommandLineArguments(const QString &commandLine)
{
    ProcessArguments result;
    if (commandLine.isNull() || commandLine.contains(QChar(0))) {
        result.error = QStringLiteral("The native command line is unavailable or contains a null character.");
        return result;
    }
    int count = 0;
    wchar_t **arguments = CommandLineToArgvW(reinterpret_cast<LPCWSTR>(commandLine.utf16()), &count);
    if (!arguments) {
        result.error = QStringLiteral("Cannot decode the native command line (Windows error %1).")
                           .arg(qulonglong(GetLastError()));
        return result;
    }
    const auto release = [](wchar_t **memory) { LocalFree(memory); };
    const std::unique_ptr<wchar_t *, decltype(release)> allocated(arguments, release);
    result.values.reserve(count);
    for (int index = 0; index < count; ++index)
        result.values.append(QString::fromWCharArray(arguments[index]));
    return result;
}

ProcessArguments processArguments()
{
    // MinGW 的窄 argv 会把本机代码页外字符变成 '?' 并可能展开为多份文件。
    // Qt 5.15.2 再拿这个 argc 索引宽参数时会越界；直接按宽命令行解码，
    // 不依赖 CRT 的数量，也不修改全局 glob 行为或把 Unicode 路径降为窄字符串。
    const wchar_t *commandLine = GetCommandLineW();
    return windowsCommandLineArguments(commandLine ? QString::fromWCharArray(commandLine) : QString());
}

} }
