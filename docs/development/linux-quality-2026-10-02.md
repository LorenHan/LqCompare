# Linux 接手与跨平台质量记录（2026-10-02）

本轮从 `01922ebd61340d7e6c8d42f274040ae9e78817c3` 接手，延续已有
369 条规格、`App → Views → Services` 分层和逐条 issue 的小步提交约定。
产品行为以 Beyond Compare 5 为基准，双窗格及合并操作参考 TortoiseGitMerge。
不把按钮存在、服务层存在或单平台通过当成完整产品验收。

## 本轮顺序

1. 先建立 Linux 真正可运行的主程序和全量测试基线，修复运行器与平台崩溃
2. 按既有 issue 补齐文件夹比较的高频入口，再改进文本差异阅读与合并体验
3. 每个小功能独立测试、提交、推送；保留 macOS / Windows 真机验收清单

新界面采用常见紧凑控件；图标操作保留提示和可访问名称。截图检查布局、
重复操作、取消和状态恢复，不能用编译成功代替界面验收。

## ENG-003 / #335：测试工程发现与空目录诊断

原运行器使用非法的 `${#PROJECTS[@]:-0}` 展开。真实 Linux 执行每次都会报
`bad substitution`，空测试目录继续进入调度，最后误报成「没有匹配过滤器」。

修复使用显式计数，避免依赖旧版 bash 的空数组行为；自测新增五条断言，
覆盖空目录退出码、准确原因、不启动调度、空目录与正常目录均无展开错误。

已验证：

- Linux Qt 5.15.15 / GCC 14.2.0：`run-tests.sh --self-test` 全部通过
- 原版本空目录复现：退出 2，但出现展开错误且错误归因到空过滤器
- `bash -n Code/Tests/run-tests.sh` 与 shell 可移植性护栏通过
- 主程序完整构建成功；原有两处编译警告尚未处理，不宣称零警告

本轮尚未在 macOS bash 3.2 和 Windows Git Bash 实机执行上述变更。
Qt 5.15.2 的正式矩阵由 CI 验证，不能由本机 Qt 5.15.15 替代。

## 基线远端证据

主分支 [CI 35930937962](https://github.com/LorenHan/LqCompare/actions/runs/35930937962)
对应本轮基线提交：macOS 套件作业成功；Ubuntu 的 Folder 套件崩溃；
Windows 存在构建和断言失败。该次 CI 因未取得私有 LqRibbon 依赖，
三个系统均未构建或打包主程序，不能称为三平台产品通过。

## PLAT-002 / #325：POSIX 符号链接读取

基线 Ubuntu 产物已取得真实调用栈：`__readlink_chk →
PosixFileSystem::linkTarget → Folder::compare`。这与旧 Qt 的
[QArrayData 对象大小计算问题及官方修复](https://github.com/qt/qtbase/commit/2778f020218a503235638be558d86a05c38302ef)
吻合。修复只把系统调用的接收缓冲改为 `std::vector<char>`，不关闭 FORTIFY，
不改变链接不跟随、最大 64 KiB、按实际返回长度解码与原始错误码契约。

已验证：

- Linux Qt 5.15.15 / GCC 14.2，显式 `-O2 -D_FORTIFY_SOURCE=3`
- FileSystem 全套 59 passed / 0 failed / 0 skipped
- 新增真实 POSIX 链接测试：1、255、256、257、512、513 字节、相对悬空及
  长 Unicode 目标；错误复位、空错误输出指针、缺失/空路径与普通文件错误
- 将完整读取条件故意改错的独立变异，被 4 条数据行检出；最终源码重新构建并通过
- `git diff --check` 通过，该独立构建无警告

正式 Qt 5.15.2 上的原始崩溃是否消失仍待 CI；本机新版 Qt 结果不能冒充该证据。
Windows 的链接 API 没有改动，新增 POSIX 专属用例在 Windows 明确跳过。

## SESS-018 / #52：关闭标签后立即退出

Linux 的真实 AppIntegration 基线在打开并关闭五种会话后崩溃。ASAN 确认：
已关闭标签的会话尚在等待 `DeferredDelete`；窗口成员 `m_documents` 已经析构，
随后 Qt 删除剩余子会话时，`destroyed` 回调仍访问该哈希表，形成 use-after-free。

窗口析构改为断开所有仍被跟踪的会话，而非只断开仍显示的标签。回归覆盖
仍打开、部分关闭、全部关闭三种状态，要求窗口退出后会话与视图均已销毁。
这只是会话关闭生命周期修复，不代表 #52 所有产品标准均已完成。

已验证：ASAN 基线在「关闭一个标签」行重现；修复后完整 AppIntegration
17 passed / 0 failed / 0 skipped，`detect_leaks=0`（不宣称检测了泄漏）。
离屏截图已人工检查；Ribbon 对比度、拥挤和语言一致性仍是独立待改进项。
