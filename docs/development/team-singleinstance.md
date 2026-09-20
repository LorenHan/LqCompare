# SingleInstance 夜间交付（2026-09-20）

范围仅为 `Code/Services/Platform/instanceprotocol.{h,cpp}`、`singleinstance.{h,cpp}`、`Code/Tests/SingleInstance/` 和本记录。保留原有协议、退出码、激活策略及服务接口；没有修改其他平台文件、App、规格或 GitHub，没有提交共享工作区。

## 结论与复现证据

基线 SingleInstance 为 **73 passed / 20 failed**。独立构建后先重现 lone-primary 与 crash-recovery 都返回 Degraded。

本机为 macOS 26.6.2 / Apple Silicon，Qt 5.15.2 clang_64 经 Rosetta 运行。`ipcs -m`（原生与 x86_64）没有任何段，但原生 libc `shmget(IPC_PRIVATE, 1, 0600 | IPC_CREAT)` 也返回 **-1 / errno 12 (ENOMEM)**。因此当前故障是系统共享内存分配不可用，并非另一个 LqCompare 实例仍占键；也不能归结为简单的段数已满。没有提高 sysctl 限额、删除其他应用 IPC 或清理用户数据。

已确认的代码问题：

- `start()` 的恢复分支只有孤立分号，定义好的 `recoverStaleIdentifier()` 从未调用。
- 把所有共享内存创建失败都当作“别的实例存在”，导致系统分配失败时所有启动都退化为多实例。
- 测试子进程停止一律 `kill()`；模拟服务的共享内存/服务对象也未正常析构。崩溃模式即使未成为 Primary 仍宣布 READY，掩盖夹具初始化失败。
- 重复 `start()` 不释放/复用已有监听；切换种子或禁用时仍可能保留旧身份。
- 首实例持有身份但尚未监听时，第二实例立即降级，没有使用超时预算等待启动完成。
- 净化后的 seed 能与另一个原始合法 seed 同名；字段以 `-` 拼接也丢失边界。已对原始字段的长度编码计算摘要，并对短名称同样保留摘要。

Apple XNU 公开源码中，System V 段创建与释放分别涉及内核页和当前进程页的取整；Rosetta 的页大小差异是“一字节段反复创建后空系统仍 ENOMEM”的**可能解释**，不是本次已经证实的内核根因。标识段改为 16 KiB 对齐以避免该差异；已经耗尽的系统计账并未因此恢复。

参考：[Qt 5.15.2 System V 共享内存实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qsharedmemory_systemv.cpp)、[Apple XNU System V 共享内存实现](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/sysv_shm.c)。

## 实现与集成

所有守卫先争用同一个 `QLockFile`，设置 `staleLockTime(0)`，不按锁龄抢占活进程；成功后创建原有 `QSharedMemory` 标识和 `QLocalServer`。遗留共享段在持有进程锁时 attach/detach 回收。共享内存返回资源/权限等非 AlreadyExists 错误时，保留进程锁与本地套接字，仍只有一个 Primary；`report.detail` 明确包含共享内存不可用的原因。仍被旧实例持有的共享段不会被抢占。

正常释放顺序为连接 → 监听 → 共享内存 → 进程锁。崩溃锁由 QLockFile 的进程存活检查回收。重复启动同一首实例保留原监听；切换种子或禁用会先释放旧资源。连接、写入、应答共用一个截止时间，在首实例尚未监听时短暂重连。

现有公共调用方式不变，注意 `RelayRequest` 的字段顺序是工作目录、参数，建议显式赋值：

```cpp
RelayRequest request;
request.workingDirectory = QDir::currentPath();
request.arguments = argumentsToForward;
guard.setRelayRequest(request);
const InstanceStartReport report = guard.start(instanceSeed);
// guard 必须保持到应用退出；报告的 detail 交给现有日志系统。
if (report.shouldExit()) return report.exitCode();
```

首实例连接 `relayReceived(const RelayRequest&)` 创建会话，并使用发送方工作目录解析相对路径；`activationRequested(arguments, raiseWindow)` 给出窗口置前策略结论。新增只读查询 `usesSharedMemoryIdentifier()` 与 `identifierLockPath()` 用于诊断。`platform.pri` 原本已包含本模块与 QtNetwork，无需新增 include。

标识名称现在始终带原始字段摘要，旧版计算出的端点/键与新版不同；当前文件本来尚未提交，未设计已发布版本之间的 IPC 兼容迁移。

## 已验证

独立目录：`/Users/loren/Desktop/Work/build/lqcompare-singleinstance`。使用 Qt 5.15.2 qmake、C++17、`make -j2`、macOS ad-hoc 签名。测试工程现在只编译本模块，显式 `QT -= gui`；二进制不链接 QtGui/QtWidgets。

最终完整套件连续三轮均 **104 passed / 0 failed / 0 skipped**（约 5 秒/轮）。日志为上述构建目录下 `final-1.txt`、`final-2.txt`、`final-3.txt`。

覆盖原有 93 项，以及新增 11 项：

- 原始输入/净化结果身份区分，带分隔符字段边界区分。
- 同一守卫重复启动、换种子、禁用后的旧身份与监听释放。
- 128 次正常创建/销毁；子进程正常退出释放身份。
- 12 次真实 `_exit` 崩溃 → 接手 → 子进程成功转交。
- 4 个真实进程同时启动，只产生 1 个 Primary，其余成功转交并以对应退出码退出。
- 首实例延迟 120ms 开始监听，第二实例在 600ms 预算内成功转交。
- 实际跨进程传递空参数、含换行/制表符/空格/中文参数，保持参数与工作目录逐字一致。

测试身份使用 UUID 隔离并行运行与 PID 复用；普通子进程通过受控退出事件析构，仅崩溃用例使用 `_exit`。父进程异常终止时子进程有 30 秒退出保护。参数夹具从换行分隔改为 JSON。

连续三轮前后 Qt IPC 临时文件数均为 **86，新增 0**（历史文件未删除）；原生 `shmget` 仍返回 ENOMEM。该结果验证的是资源不可用时仍可工作的实际进程锁/套接字闭环，没有跳过失败用例。

## 尚未验证 / 交给协调集成

- 本机当前无法分配 System V 共享内存，因此正常共享段创建、attach/detach 回收分支需要在共享内存可用的 macOS/Linux 环境补跑；不能把本次 fallback 全绿表述为这些分支已经实测。
- Windows MinGW 8.1 32 位及 Linux 本轮未构建运行。实现和测试仍使用 Qt 跨平台 API；崩溃测试不再错误要求 Windows 保留共享段。
- Qt 5.15.2 对本机 26.5 SDK 给出版本兼容提示；模块编译没有新增编译器 warning/error。
- App 层实例生命周期、会话创建、窗口激活与日志落地由主协调接入；本工作未修改这些独占文件。
- CLI `--new-instance` / `--wait` 和持久化单实例选项不在本次故障修复范围。
