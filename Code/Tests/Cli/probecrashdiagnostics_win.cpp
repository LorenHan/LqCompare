#include "probecrashdiagnostics.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace {
int probeArgumentCount = 0;

void printWide(const wchar_t *value)
{
    char bytes[4096] = {};
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, bytes, sizeof(bytes), nullptr, nullptr))
        std::fputs(bytes, stderr);
}

LONG WINAPI reportException(EXCEPTION_POINTERS *exception)
{
    // 不恢复执行、不改写退出码：父进程必须仍把它判为真实崩溃。
    std::fprintf(stderr, "CLI probe unhandled Windows exception 0x%08lx at %p\n",
                 exception->ExceptionRecord->ExceptionCode,
                 exception->ExceptionRecord->ExceptionAddress);
    std::fprintf(stderr, "CRT argc=%d; Unicode command line: ", probeArgumentCount);
    printWide(GetCommandLineW());
    std::fputc('\n', stderr);
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS
                  | SYMOPT_NO_PROMPTS);
    // 明确只搜索本地目录，不依赖符号服务器或运行器的用户配置。
    if (!SymInitializeW(process, L".", TRUE)) {
        std::fprintf(stderr, "SymInitializeW failed: %lu\n", GetLastError());
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame = {};
#if defined(_M_IX86) || defined(__i386__)
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#elif defined(_M_X64) || defined(__x86_64__)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#else
    SymCleanup(process);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
#endif
#if defined(_M_IX86) || defined(__i386__) || defined(_M_X64) || defined(__x86_64__)
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    for (int index = 0; index < 48 && frame.AddrPC.Offset; ++index) {
        const DWORD64 address = frame.AddrPC.Offset;
        std::fprintf(stderr, "#%d 0x%llx ", index, static_cast<unsigned long long>(address));
        const DWORD64 module = SymGetModuleBase64(process, address);
        wchar_t modulePath[1024] = {};
        if (module && GetModuleFileNameW(reinterpret_cast<HMODULE>(static_cast<ULONG_PTR>(module)),
                                         modulePath, sizeof(modulePath) / sizeof(wchar_t))) {
            printWide(modulePath);
            std::fprintf(stderr, "+0x%llx ", static_cast<unsigned long long>(address - module));
        }
        alignas(SYMBOL_INFOW) unsigned char storage[sizeof(SYMBOL_INFOW) + 1024 * sizeof(wchar_t)] = {};
        auto *symbol = reinterpret_cast<SYMBOL_INFOW *>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        symbol->MaxNameLen = 1024;
        DWORD64 displacement = 0;
        if (SymFromAddrW(process, address, &displacement, symbol)) {
            printWide(symbol->Name);
            std::fprintf(stderr, "+0x%llx", static_cast<unsigned long long>(displacement));
        }
        std::fputc('\n', stderr);
        if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
    }
#endif
    SymCleanup(process);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
}

void installProbeCrashDiagnostics(int argc)
{
    probeArgumentCount = argc;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(reportException);
}

void triggerProbeCrashForTest()
{
    // 有意在独立子进程抛出同一异常，验证诊断链路没有吞掉真实失败。
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}
