#ifndef LQCOMPARE_TEST_PROBECRASHDIAGNOSTICS_H
#define LQCOMPARE_TEST_PROBECRASHDIAGNOSTICS_H

// 只在真实子进程探针中安装；QtTest 的父进程仍使用自身的崩溃处理。
void installProbeCrashDiagnostics(int argc);
void triggerProbeCrashForTest();

#endif
