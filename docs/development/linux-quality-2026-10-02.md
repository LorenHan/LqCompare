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

## UI-001 / #2：启动默认页面

截图暴露了一个可复现的装配问题：Ribbon 每添加一页就选择新页，启动后最终
停在 Help。装配结束改为选择本次创建的第一张 Home 页，之后命令状态刷新
不抢走用户选择的 Compare 等页面。新增回归先得到实际 Help / 期望 Home，
修复后 CommandActions 19 passed、AppIntegration 17 passed；离屏截图确认
默认页面已是 Home。布局拥挤与搜索栏遮挡不在这一小修复内。

## 已发布三项修复的冻结回归（0f89075）

- 独立冻结源码工作树执行 71 套：3716 passed / 0 failed / 2 skipped
- 另排除 SingleInstance 1 套：此前已实测 22 项因云环境禁止本地 socket 失败
- 仓库 73 个 `.pro` 含更深层辅助工程，主运行器实际发现 72 套，不能把工程数
  直接当作已执行套件数
- [Qt 5.15.2 Ubuntu CI 36951310090](https://github.com/LorenHan/LqCompare/actions/runs/36951310090)
  在前一提交 `ca66cde` 已成功：3786 passed / 0 failed / 2 skipped，原 Folder
  崩溃不再出现。该 CI 排除 AppIntegration、CommandActions，并跳过主程序构建/打包
- 上述两类验证互补；macOS、Windows 的本轮结果仍需单独读取，不据此推断通过

## PLAT-001 / #322：Windows 文本/合并文件标识构建

Windows 基线的 14 个套件在 `textdocument.cpp` 编译失败：`DWORD` 直接传给
`QByteArray::number` 有重载歧义，且 `QDir` 缺少直接 include。文本保存和合并
输出的同类标识/变更令牌统一显式转换为 `qulonglong`，保留无符号十进制值；
POSIX 分支不变。依据是 [Windows DWORD 定义](https://learn.microsoft.com/en-us/windows/win32/winprog/windows-data-types)
和 [Qt 的 number 重载](https://doc.qt.io/archives/qt-5.15/qbytearray.html#number)。

提交前验证：

- 从两份生产源码提取实际格式表达式，以 `unsigned long` 字段构建编译探针：
  原代码明确报重载歧义；修复后构建并运行通过
- 探针核对 0、2147483647、2147483648、4294967295，以及混合字段顺序与分隔符
- Linux Text / TextRules / TextView 合计 88 passed；MergeOutput 19 passed

上述探针验证的是 C++ 重载与格式，不是假扮 Windows 文件系统。实际 Win32
调用及完整 MinGW 8.1 构建仍由此次推送后的 Windows CI 判定；重解析点和注册表
的其它已知构建失败另行处理，不在此提交宣称全部修好。

## DIR-005 / #114：文件夹时间比较与容差入口

文件夹选项新增时间戳开关、默认 2 秒容差及「仅大小与内容」预设。时间差列
显示左减右的精确差值并支持排序；时间关系与内容结论仍独立。预设一次性关闭
时间比较并启用完整内容比较，不会扫描中间状态或保留「仅前 N 字节」的限制。

设置随会话保存，严格验证数值；结果保存当次扫描的开关和有效容差，尚未完成
的新一轮选项不会改写旧结果的解释。UTC 纳秒差采用独立符号与无符号幅度，
避免极端时间值相减溢出，2 秒边界包含在容差内，多 1 纳秒即在容差外。

提交前验证：

- Folder 62 passed / 0 failed / 0 skipped，EntryStatus 28 / 0 / 0
- UTC、America/New_York、Asia/Shanghai、Europe/Berlin 四个独立 TZ 进程
- 快速修改选项的回归重复 20 次通过；持久化往返后实际扫描仍采用恢复的容差
- 1380 像素离屏截图已检查，两侧时间差列均在视口内；精确长值可用 tooltip 查看
- 检查改动文件哈希与最终构建一致，静态护栏及差异空白检查通过

时区证据是固定偏移、夏令时重复时钟语料及 TZ 进程测试，不是实际网络盘挂载。
macOS / Windows 原生交互与正式工具链仍需各自验证。

## PLAT-005 / #326：Windows 注册表后端编译

值名排序的临时变量修正为 `QStringList`；在本编译单元任何头文件之前设置
可覆盖的 Windows 目标缺省值，使旧 MinGW 能看到 `RegDeleteTreeW` 声明。
显式目标版本保持不变，没有更改删除流程、错误处理或真实注册表数据。

已验证：原文件编译失败可重现；修复后使用官方 Windows Qt 5.15.2 头文件与
Debian i686 GCC 14 / MinGW-w64 v12 交叉编译为 i386 COFF，`-Werror` 通过；
缺省 0x0600 与显式 0x0A00 均通过。独立 PE32 链接探针解析到 ADVAPI32.dll，
未执行。Linux ShellIntegration 在独立构建目录 101 passed / 0 failed / 0 skipped。

此证据不是 MinGW 8.1、完整 Windows 应用链接、实际注册表写入/删除或 Explorer 验收。

## ENG-006 / #338：文件日志的 UTF-8 保真

Windows CI 中中文日志变成问号。文件端的 `QTextStream` 曾使用本机默认编码，
而诊断读取固定按 UTF-8 解码。文件写入现在明确选择 UTF-8，保留原来的追加与
换行方式，不改变控制台代码页或用户系统设置。

新增测试在同一 Linux 二进制内分别模拟 UTF-8、Windows-1252、GB18030 默认编码，
核对中文、繁体、重音字符与 emoji 的真实输出字节以及无 BOM。原代码两组失败，
修复后 Logging 全套 46 passed / 0 failed / 0 skipped，失败时也恢复测试进程编码。
真实 Windows CI 的对应回归仍按此次提交单独核对。

## PLAT-002 / #325：Windows 重解析点读取兼容

不再依赖旧 MinGW 缺失的 `REPARSE_DATA_BUFFER` 类型。Win32 薄层直接包含
`winioctl.h`，独立 QtCore 解析器核对实际返回长度、声明载荷、偏移/长度及
UTF-16LE 码元，支持符号链接与目录联接，并与 SDK 常量做编译期核对。
不跟随链接、不改写相对目标，未知或损坏记录保持 NotSupported。

替代名称内部的 NUL 被拒绝，避免显示的 QString 与 Win32 实际消费路径不一致；
声明长度外的可选终止符仍接受。系统调用失败码在 CloseHandle 前保存。

已验证：75 项解析用例在正常与 ASan/UBSan 构建均通过，原 FileSystem 59 项通过；
NUL 修复前 6 行回归确实失败。官方 Windows Qt 5.15.2 头与 i686 GCC14/MinGW12
成功编译完整 Win32 实现、解析器及测试为 i386 COFF。仓库护栏通过。
ASAN 使用 detect_leaks=0；原 FILETIME 的两处溢出警告尚待独立修复。
精确 MinGW8.1 构建、原生链接/运行及真实 Windows 链接类型仍需正式 CI/真机验证。

## OPT-010 / #365：诊断日志原文逐字节导出

关闭脱敏时不再把日志先按 UTF-8 解码后重编码；二进制写入也避免 Windows
Text 模式把 LF/CRLF 再次改写。开启脱敏仍走原来的 UTF-8 路径替换逻辑。
写入或刷新失败会回到已有的半成品清理路径，不把不完整包报成成功。

新增 LF、CRLF、非 UTF-8/内嵌 NUL、空日志四类真实字节回归。原代码在 Linux
非 UTF-8 行确实失败，修复后 LogDiagnostics 92 passed / 0 failed / 0 skipped。
Windows 的原文换行失败由对应正式 CI 继续验证。
