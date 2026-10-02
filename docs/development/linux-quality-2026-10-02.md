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

## PLAT-001 / #322：合并输出的 Windows 链接测试夹具

MergeOutput 的真实符号链接夹具也在 Qt/CRT 头文件之前设置可覆盖的 Vista
API 缺省目标，解决旧 MinGW 隐藏 `CreateSymbolicLinkW` 声明的编译错误。
不改生产保存逻辑，不改变构建方显式指定的目标版本。

该完整测试源码使用官方 Windows Qt 5.15.2 头和 i686 编译器生成 i386 COFF，
`-Wall -Wextra -Werror` 通过；Linux MergeOutput 全套 19 passed / 0 failed / 0 skipped。
Windows 真正的链接权限与别名保护断言仍待原生 CI。

## PLAT-010 / #332：过滤器的平台策略测试矩阵

Windows 的 8 条名称过滤/过滤栈失败来自测试隐含 POSIX 前提，生产匹配代码
不需要修改。测试显式覆盖 POSIX 与 Windows 两套策略、覆盖/清除大小写规则、
三层逐层匹配和平台往返重解析；本机默认另用编译目标独立断言。

原生 Linux：NameFilter 101、FilterStack 90 全部通过且无跳过。隔离副本只把
默认策略改为 Windows，原测试精确复现 5+3 条失败；修订后的跨平台用例全过。
新增本机默认护栏在该故意错误的 Linux 默认变异中另报 3+1 失败，证明不是
拿被测函数生成预期值的假绿。生产默认行为未改：过滤器 Windows 策略不敏感，
POSIX 策略敏感；此策略模拟不是原生 Windows 运行证据。

## PAT-006 / #265：补丁的真实跨平台夹具

补丁测试改用真正的 POSIX / Windows 符号链接，明确核对链接类型与拒绝诊断，
只有 Windows 明确缺少符号链接权限才跳过；目录拒绝用例独立执行。磁盘夹具
使用平台合法文件名，所有平台仍保留内存中的引号/Unicode/JSON 往返，POSIX
继续创建含双引号的真实文件。

真实 Git 的 stderr 警告不再混进补丁 stdout。旧源码在 Linux 通过进程级
core.autocrlf=true 精确复现 Windows 的首行解析失败，修订后全套再验通过。
生产补丁解析器与安全检查未改，没有通过忽略警告文本来放宽补丁格式。

PatchApply 61、PatchRegression 77 项全部通过且无跳过，强制 autocrlf 的
第二轮仍为 77 通过；两个完整测试翻译单元通过官方 Windows Qt 5.15.2 头与
i686 的 -Werror 交叉编译。原生 Windows 权限及真实 Git/patch 执行仍以 CI 为准。

## VCS-012 / #283：Git 历史路径与协议字节保真

blame 历史文件名属于 Git 树命名空间，不再经本机 QDir::cleanPath 改写：
Windows 也能保留历史名称中的字面反斜杠。仅元数据解析使用斜杠分量校验，
拒绝空分量、绝对路径、点/父级分量及 NUL；实际文件访问仍保留原生路径和
工作副本边界校验，没有放宽文件读取范围。

伪 Git 子进程在 Windows 使用二进制 stdout，任意协议字节以 Base64 通过环境
传递；覆盖 LF/CRLF、孤立 CR、NUL、Ctrl-Z、UTF-8，继续拒绝被整体转成 CRLF
的错误协议。VcsView 改用真实本机临时根，覆盖中文/空格路径。

Linux Vcs 79、VcsView 18、VcsBlameView 23 项全部通过且无跳过；C locale
协议子集 36 项通过。旧 CRT 转换模拟精确复现 3 条失败，旧主机分隔符模拟
复现 quoted-octal 路径失败。三个完整翻译单元用官方 Windows Qt 5.15.2 头
与 i686 编译器通过 -Werror；原生 Windows 运行结果仍等待正式 CI。

## PLAT-006 / #327：单实例测试的进程等待与 Unicode 夹具

修复测试子进程等待循环：先读取 QProcess 已缓冲输出，再判断进程退出；
截止时最后收尾，继续分派父进程事件。READY 不再因已被事件循环缓冲而丢失。
环境字段通过 Unicode API 读取，JSON 明确转 UTF-8，覆盖中文、非 BMP 字符、
空列表、空参数、换行/制表符以及真实 Unicode 输出路径。

最终相同测试套在旧 helper 上为 79 passed / 3 failed，修复后连续 20 轮各
82 passed / 0 failed / 0 skipped；完整测试翻译单元以官方 Windows Qt 5.15.2
头和 i686 编译器通过 -Werror。未改生产守卫、超时默认值和跳过策略。

本轮选择 A-H、H2、L 的无 socket 子集；原有 I-K 共 30 个真实 IPC/生命周期
测试函数未在受限云主机运行。不能把聚焦通过当成 SingleInstance 全套通过。
macOS / Windows 原生完整 IPC 及 Unicode 修复由此次正式 CI 验证。

## PLAT-002 / #325：Windows FILETIME 单位与整数边界

原生读取先在 100ns 单位下减去 1601/1970 纪元差，再有界转纳秒；写入正确
除以 100，负数统一向过去取整。旧读取已由 UBSan 抓到现代日期的有符号溢出；
旧写入黄金值比正确 FILETIME 大 100 倍。越界读取返回无效，最低 8ns 因向下
舍入后超出内部范围拒绝写入；不回绕、不钳位、不部分更新指定时间。

无效 FileTime 继续表示保留该字段，创建时间不修改；SetFileTime 失败码在
CloseHandle 前保存。纯 helper 是实际 Windows 实现使用的同一份整数换算。

Linux 回归：WindowsFileTime 31、FileSystem 69、Folder 62、Sync 24、
SyncBaseline 116，共 302 passed / 0 failed / 1 Windows 专属 skipped。
纯换算启用 UBSan 和 -Werror；三时区重跑结果一致。生产 Win32、helper、纯
测试和 FileSystem 测试四个翻译单元通过官方 Qt 5.15.2 头/i686 -Werror，
旧的两处 FILETIME 溢出编译警告已消除。原生 Windows MinGW8 仍待正式 CI。

## PLAT-008 / #330：Windows 目录类型错误的诊断

ERROR_DIRECTORY（267）现在归为 NotDirectory，并保留 Win32 原码与符号名称，
避免把对普通文件的目录枚举报成 Unknown。官方值由 Windows SDK static_assert
校验；目录非空仍保持原分类，未混成可重试的 Busy。

新增纯分类/诊断断言在旧源码上明确失败，修复后 FileSystem 69 passed /
0 failed / 1 Windows 专属 skipped；真实 Linux 枚举检查仍在全套执行。
完整 filesystem.cpp 以官方 Windows Qt 5.15.2 头和 i686 -Werror 编译通过。
Windows 实际文件枚举行为仍由正式 CI 对原用例验证。

错误码依据：https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-

## ENG-003 / #335：按宿主选择 Qt 测试平台

Windows 默认使用原生 windows 插件，Linux/macOS 保留 offscreen；显式
QT_QPA_PLATFORM 原值（含插件参数/候选列表）优先，不在失败后自动降级。
移除 CI 作业级的统一 offscreen，所有平台都执行不依赖 Qt 的选择器自测。

Windows CI 的 Folder/TextView 崩溃栈均为 qt_getWindowsSystemMenu →
QMessageBox::showEvent。Qt v5.15.2 源码确认 offscreen 未提供 nativeInterface，
基类返回 nullptr，而 QMessageBox 的 Windows 菜单访问没有空指针检查。
这是测试运行平台配置问题，未删弹窗测试或更改业务提示逻辑。

14 条选择/导出/覆盖断言、四类变异、完整 Linux 运行器自测通过。
经更新后的运行器，Linux TextView 31、Folder 68 全套通过，均无跳过，日志
确认选择 offscreen。Folder 过滤运行排除了 FolderMerge/FolderMergeView，
不能算这些套件通过。原生 Windows windows 插件仍待该提交正式 CI。

Qt 官方源码：
- https://github.com/qt/qtbase/blob/v5.15.2/src/plugins/platforms/offscreen/qoffscreenintegration.h
- https://github.com/qt/qtbase/blob/v5.15.2/src/gui/kernel/qplatformintegration.cpp
- https://github.com/qt/qtbase/blob/v5.15.2/src/widgets/dialogs/qmessagebox.cpp

## PLAT-010 / #332：目录比较的原生路径夹具

错误注入的标量与映射在插入/查询两端使用目标平台的分隔符规则；POSIX
字面反斜杠仍是文件名，不误当目录。路径预期保留大小写与身份检查，只统一
本机分隔符。循环链接纯表使用本机真实绝对根，真实链接用例不再把 Windows
快捷方式当符号链接，也不再一概跳过 Windows。

Folder 三轮全套各 68 passed / 0 failed / 0 skipped；最终又经新运行器验证
68/0/0、平台 offscreen。此 68 项证据对应本节夹具与上节运行器的组合源码。
独立纯夹具验证 18/0，旧原始键、只规范化一端、无条件替换 POSIX 反斜杠等
变异都被断言抓住。官方 Windows Qt 5.15.2 头的 i686 编译成功；保留并单独
豁免原有三条 dangling-reference 警告，不能宣称该翻译单元全警告为零。
原生 Windows 链接权限、路径注入与完整弹窗回归仍以提交后的 CI 为准。

## CLI-012 / #303：Windows Unicode 子进程崩溃取证

将真实 JSON/UTF-8 用例拆成 ASCII、Unicode 路径、Unicode 内容、两者并存
四行，同时严格核对 NormalExit、JSON 状态/路径/差异数与 UTF-8 往返。
仅测试探针安装有界 Windows 崩溃栈记录（本地符号、最多 48 帧），保留
原异常退出码；独立故障注入自测要求仍是 CrashExit / 0xC0000005。
没有更改产品 CLI 行为，也未把原生崩溃标成解决。

Linux Cli 119 passed / 0 failed / 0 skipped；测试与 Windows 诊断翻译单元
通过官方 Qt 5.15.2 头/i686 -Werror 编译，独立 PE 探针成功链接 DbgHelp。
诊断自测、实际崩溃栈与 Windows 参数/内容四行结果需要原生 CI 确认。

## PAT-006 / #265：Windows GNU patch 二进制往返

ac47de0 原生 Windows 已确认 PatchApply 61/0/0，PatchRegression 的 Git apply
通过，但 GNU patch 在 CRLF/mixed 字节夹具失败。按 GNU 官方选项文档，仅在
Windows 的正向/反向 patch 调用添加 --binary；全部目标文件字节比较保留。
macOS 系统 patch 参数不变，没有修改生成器或把换行差异当作成功。

Linux PatchRegression 77/0/0；额外包装器真实执行 GNU patch --binary 的
往返子集 4/0/0。Windows 头/i686 -Werror 编译通过；原生 Windows 修复后
结果等待 CI。依据：https://www.gnu.org/software/diffutils/manual/html_node/patch-Options.html

## PLAT-007 / #328：特殊文件名的合法磁盘夹具

原 PathName 用例无条件在 Windows 创建双引号文件，原生 CI 因文件系统禁止
而失败。现在三平台真实创建 Unicode/空格/单引号名称并读回字节，POSIX
额外保留双引号磁盘用例；所有平台均执行双引号原样显示与禁止新建的纯断言。
没有放宽生产名称规则，没有跳过整套特殊名称测试。

Linux PathName 44 passed / 0 failed / 0 skipped；完整测试翻译单元使用官方
Windows Qt 5.15.2 头及 i686 -Werror 编译通过。原生 Windows 的真实创建与
显示结果仍待 CI；其它平台能力相关旧用例的跳过边界不变。

## PLAT-002 / #325：Windows 当前元数据与存在性一致

stat/exists 统一使用 access=0、共享读写删除、不跟随重解析点的句柄查询；
目录枚举仅用搜索 API 取名称，再逐项 stat 取得当前元数据。子项查询错误或
FindNext 中途错误明确报告不完整，不忽略 Snapshot/FolderMerge 的一致性条件。
官方 FindFirstFileW 文档说明 NTFS 搜索属性可能过时，不能替代当前属性查询。

b1e6786 原生 Windows 已确认 FILETIME 新用例全过，但 Snapshot/FolderMerge
的目录复核仍失败；旧产物未记录具体变化字段，缓存机制仍须新原生用例验证。
新增 FileMetadata 的卷根、打开写句柄 oracle、坏链/活链、属性、错误复位与
1000 项枚举计时；每项额外元数据句柄的 Windows 性能开销尚未测定。

Linux Snapshot 122、FolderMerge 31、SyncBaseline 116 全部通过；FileMetadata
9 passed / 0 failed / 9 Windows 专属 skipped。八种目录变化必须拒绝的新增
回归全部能抓住故意放宽的变异。三个完整 Windows 翻译单元用官方 Qt 5.15.2
头/i686 -Werror 通过；原生 NTFS/权限/性能及跨平台 CI 仍待验收。
详细依据与局限见 windows-metadata-2026-10-02.md。

## PLAT-003 / #324：Windows 删除默认拒绝，先保留原件

重要安全边界：旧 SHFileOperation 的 ALLOWUNDO 只是尽力保留撤销信息；
SHQueryRecycleBin 查询成功不能证明配额/策略变化后仍不会永久删除。本轮
移除 best-effort 删除路径，Windows 后端在可恢复保证未完成前统一拒绝非空
删除批次，返回 NotSupported 和明确“尚不能保证删除可恢复，已阻止”状态。
不调用 Shell/WinAPI、不读写文件、不猜测回收站位置、不自动永久删除。

新状态追加到枚举末尾保留旧值；空批次仍是无操作。系统回收站位置可供自行
打开，但本应用的 Windows 回收站删除与自动恢复均未开放。Linux/macOS 实现
未按此关闭；原生恢复用例保持并加强清理，Windows两条恢复用例在删除前跳过。

Linux Trash 42 passed / 0 failed / 0 skipped，包括直接运行同一 Windows
生产拒绝类的文件、目录和空/不存在批次。只在临时目录的同步集成探针 3/0/0：
三个删除项失败、零成功、父目录/子目录/原字节不变、基线不可提交。伪造能力
与成功的无删除变异被四条新回归检出。官方 Windows Qt5.15.2 头/i686 -Werror
编译通过，符号表确认无 Shell/WinAPI/删除函数依赖；原生 Windows仍待 CI。

这是一项安全降级，不是“Windows可恢复删除已完成”。后续必须实现并验证
真正拒绝永久退化的后端、取消/配额/策略边界与恢复能力，才能重新开放。

同步执行器也显示目标路径、具体不可恢复原因及保留文件建议，并保留真正搬移前的
第二次能力校验。Sync全套25/0/0，新增预演/确认执行回归验证搬移调用0、成功0、
三项失败与原件完整。同步源/测试同样通过Windows头/i686 -Werror交叉编译。

## ENG-004 / #336：PR CI 只保留最新快照

经确认，同一工作流/同一 PR 的新快照可取消过期运行；不同 PR、不同工作流
分开分组，main push 与手工触发使用唯一 run_id，不相互取消。三平台矩阵、
测试步骤及失败结果不改。YAML 解析、六个分组边界样例及原触发/矩阵检查通过；
GitHub 原生调度效果由后续连续提交验证，不把本地表达式样例当作调度实测。

已按确认在云浏览器取消 PR370 的过期运行 36962060639（659359e）、
36962550187（0e2f775）、36962194209（f2b3075），API 已确认三者 cancelled。
保留 532c8d2 / 36962955982 的当前元数据原生验证与新的安全护栏快照；
此前 b1e6786 已自然完成，产物证据保留，未删除日志/产物或操作 main。

## PLAT-002 / #325：macOS 创建时间的原生契约回归

b1e6786 的 macOS 唯一时间用例失败在创建时间比较，mtime/atime 已符合预期。
Apple 文件系统允许回溯 mtime 时前移创建时间；不据此猜测旧产物未记录的实际值。
测试改用同目录独立 utimensat/stat 对照，按当前挂载文件系统的原生行为确定
回溯结果。Windows 仍严格保持最初创建时间，三平台均新增向未来设置 mtime
不改变创建时间的断言；atime-only、双省略和空错误指针验证保留并加强。

仅改测试，新增 FileTime UTC 纳秒诊断。Linux FileSystem -Werror 全套69/0/1；
官方 Windows Qt5.15.2头/i686 -Werror编译通过。macOS原生对照需要后续CI，
未把 Linux 通过替代 macOS 验证。

## PLAT-006 / #327：事件分派测试不依赖短周期定时器

macOS 原生单实例套件的新夹具曾在50ms等待内没有收到10ms周期tick；Windows
完整112项已通过。改为预先排队回调，断言等待前未派发、等待后已派发，预算仍
是50ms。只修测试调度假设，不降低子进程输出/事件循环保障，也不加长超时。

Linux 4路压力100轮各82/0/0；旧helper配相同测试79/3，故意用阻塞sleep取代
事件循环的变异准确失败。完整翻译单元通过官方Windows Qt5.15.2头/i686
-Werror。30项真实IPC仍未在此云主机执行，新macOS/Windows完整结果待CI。

## VCS-001 / #272：缓存计数等待首轮完成

VcsView 的探测调用计数不能证明首轮 diff 已开始；Windows 实测基线曾取到0，
随后实际完成2次而预期1次。测试先等待首轮diff与完成回调，再验证切换模式的
精确调用数；显式刷新还增加“恰多一次diff”断言，不增加超时或放宽计数。

受控延迟夹具精确复现旧actual2/expected1；修订后全套18/0/0，聚焦30轮全过。
完整测试以官方Windows Qt5.15.2头/i686 -Werror编译通过；原生Windows待CI。

## CLI-001 / #290：Windows 直接读取宽命令行

659359e 原生诊断确认：两个 Unicode 路径使 MinGW 窄 CRT argc 变成7，而宽命令行
实际上只有5参数；Qt5.15.2旧 arguments() 路径发生访问异常。纯Unicode内容
通过，崩溃诊断自身通过。共享 processArguments 入口在 Windows 直接使用
GetCommandLineW / CommandLineToArgvW，保留可执行文件、空参数、引号/反斜杠、
Unicode及字面通配符；解析失败明确报错，不返回半份参数，不全局改CRT glob。

App早期GUI/无界面分类、正式解析与两种CLI探针共用该入口；POSIX维持Qt解码。
Linux CLI 131/0/0，ASan/UBSan同为131/0/0；完整Linux产品重新构建通过，真实
位置/命名UnicodeJSON退出1、help退出0、非法参数退出2，均可在无效QPA配置下
运行。Unicode GUI离屏启动存活到测试结束，输入字节不变。

新增服务、测试及真实App入口通过官方Windows Qt5.15.2头/i686 -Werror。
这不是Windows原生功能通过；引号/反斜杠规则、GUI分类与Unicode路径还需CI。
内存分配失败保留系统码并明确失败，但没有做OOM故障注入。

依据：https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw
及 https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getcommandlinew

## MRG-013 / #104：Windows 目录链接夹具的安全解链

原生测试的父目录重定向场景此前在 QFile::remove(directoryLink) 失败，未走到
生产保存保护断言。Windows 夹具改为先核对目录重解析点，再用 RemoveDirectoryW
仅解除链接；POSIX仍用unlink式行为。新增拒绝普通目录/普通文件的夹具护栏，
目标两侧字节前后严格保持，结束时显式解链便于临时目录清理。

没有改合并输出生产逻辑或放宽“父目录已更换必须拒绝保存”。Linux MergeOutput
20/0/0；完整测试通过官方Windows Qt5.15.2头/i686 -Werror编译。原生Windows
仍待CI；依据：https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-removedirectoryw

## OPT-004 / #316：原生主题恢复的逐画刷诊断

Windows原生Qt平台暴露 dark→system 后 palette 不完全相等。保留精确相等
断言，新增启动应用/深色/恢复三阶段逐group/role画刷诊断，连填充、渐变、纹理
和变换都记录；仅RGB相等不能证明palette恢复正确。当前原生产物缺少具体差异，
没有凭猜测修改主题逻辑，也没有放宽断言。

Linux OptionsDialog24/0/0（诊断护栏遍历63个group-role组合）；完整测试经
官方Windows Qt5.15.2头/i686 -Werror编译通过。需下一原生Windows结果确定
startup polish 与实际恢复逻辑的责任边界，然后再实施有证据的修复。

## PLAT-002 / #325：绝对链接目标的 NT/Win32 路径边界

Windows重解析点替代名称的 \??\C:\ / \??\UNC\server\share 前缀不能直接
与普通扫描根比较，532c8d2 的绝对循环链接因而漏标错误。新增纯转换，仅把
已知盘符/完整UNC名称映射为等价Win32路径，再使用原有路径规范化。相对目标、
已有扩展前缀、未知NT命名空间及原始UTF-16解析契约保持不变，不跟随链接。

Linux WindowsReparse106/0/0；关闭转换的变异精确触发8条失败，实际PathUtils
探针证明原drive/UNC目标均与扫描根不匹配，转换后身份相符。Linux Folder68/0/0。
完整Win32文件系统、helper与测试经官方Qt5.15.2头/i686 -Werror编译通过。
原生绝对循环检测、UNC/符号链接行为仍待此次Windows CI。

## DIR-005 / #114：固定画布验收适配小屏幕运行器

Windows原生桌面把1380像素顶层测试窗口限制为1028像素，时间差数据/排序断言
已通过，失败只在硬编码顶层宽度。夹具改为屏幕内可滚动宿主中的1380×640子
画布，仍严格验证指定宽度、实际可见性、列范围、数值排序和证据，不改产品UI、
不改系统分辨率、不跳过Windows。离屏截图已检查双侧时间差列与内容。

Linux完整Folder68/0/0，新运行器集成同过；Windows头/i686编译通过，原有三条
测试dangling-reference警告仍单独保留，未把它称作全警告清零。原生小屏幕
宿主中的完整回归由新CI验证。

## OPT-004 / #316：恢复系统主题的完整画刷与原生状态

20d5cdc 正式Windows诊断证明仅Active/Disabled/Inactive三组Link残留暗色
#7bb7ff，启动快照是#0000ff。Qt5.15.2 WindowsVista/XP的缺省palette从当前
应用取未解析Link，直接恢复resolve=0快照会再次继承暗色。这是产品缺陷，
不是相等断言过严。

恢复时先同步显式还原启动快照全部画刷，再回填原mask；不把palette永久
标成自定义，以保留Qt原生菜单/按钮类调色板行为。未换Style、未改颜色常量。
旧runtime搭配最终代理回归准确失败两行；修复后OptionsDialog26/0/0，
覆盖重复light/dark/system、三组纹理/渐变/变换、原mask/AA_SetPalette与
QMenu/QPushButton/QAbstractButton调色板。AppIntegration17与CommandActions19
在修改后的同一工作树通过，均使用真实LqRibbon依赖。

官方Windows Qt5.15.2头/i686两翻译单元-Werror通过；ASan/UBSan聚焦6/0/0。
LSan因容器ptrace限制失败，关闭泄漏检测重跑通过，不宣称做了泄漏验收。
最终修复的原生Windows仍需下一CI，不额外声称验证了运行中OS高对比度切换。

依据：
- https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/plugins/styles/windowsvista/qwindowsxpstyle.cpp
- https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/plugins/platforms/windows/qwindowstheme.cpp
- https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/kernel/qpalette.cpp
- https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/widgets/kernel/qapplication.cpp

### e859f6b：冻结快照与正式三平台结果

[正式运行 36967820301](https://github.com/LorenHan/LqCompare/actions/runs/36967820301)
已于 2026-10-02 05:33 UTC 结束，精确源码为
`e859f6b8af37fe1c34ef0ff86a7d9089527c886a`：

- Ubuntu：4035 passed / 0 failed / 12 skipped。
- macOS：4035 passed / 0 failed / 12 skipped。
- Windows：4015 passed / 1 failed / 36 skipped；73 套都有结果。
  全局调色板恢复及新增两个 Link 代理回归通过，唯一失败移到新增的 QMenu 类
  调色板快照比较，仍在区分 Qt 原生样式再次 polish 与实际主题残留。
- 三系统 CI 的主程序构建、打包及 AppIntegration、CommandActions 均缺
  LqRibbon 而跳过；这些结果不代表可发布的三平台 GUI 已验收。

独立冻结 Linux 工作树在同一 SHA 运行 74 套，3959 / 0 / 12，含真实依赖的
AppIntegration 17 / 0 / 0 与 CommandActions 19 / 0 / 0。SingleInstance 整套因
本云环境的本地 socket 限制排除，没有把它计作通过；其不使用 IPC 的 82 项曾在
源码相同的 20d5cdc 通过。12 个跳过分别是 FileMetadata 的 9 个 Windows 专属
条目，以及 FileSystem 的 Windows 舍入、Registry 的原生 Windows、PlatformIcon
的桌面图标来源各 1 项。

实际 Linux 主程序已重建，SHA-256 为
`91f11cb68fb231d55a94bb5a134a4d47a5caaf3d82a890e2ae47966c42fd7852`。
Unicode 位置参数/命名参数 JSON、help、错误参数退出码和离屏 GUI 启动烟测通过，
输入夹具字节未变。主程序构建仍有既有 Filter 缩进与私有依赖未使用函数警告，
不宣称清零全部警告。

## UI-001 / #2：直角标签与可读对比度

此后先推进 Linux 完整应用体验；保留 Windows 已知 QMenu 测试基线问题和现有
跨平台防护，不将 Windows 收敛作为 Linux 界面改进的前置条件。

原生窗口框架下，Office 标题栏的白字实际落在浅灰标签背景上，实测对比度仅
1.15:1。仅为既有 QTabBar 增加应用自有样式，使用应用 Window/WindowText 与
Base/Text 配对；直角边缘、选中下划线、轻量悬停与键盘虚线焦点随主题更新。
悬停颜色从 Window 向 Highlight/Base 小幅混合并检查对比度，避免直接组合
没有配对保证的 Midlight/WindowText。未修改或复制私有依赖，未改窗口框架。

Linux Qt5.15.15 完整 AppIntegration 19 / 0 / 0、CommandActions 19 / 0 / 0，
主验收独立复跑同过。新增回归在 150% 缩放及 Qt Windows 绘制样式各 4 / 0 / 0。
系统、浅色、深色标签的正常/悬停/焦点像素实测对比度 10.58–21:1；两个对抗
调色板同过，恢复不安全 Midlight 方案后均以 1.04:1 失败。基线源码在最终测试下
准确复现白字问题。验证所有标签对象、文案、工具提示、可访问名称、键盘切换、
命令启用状态、会话内容与比较窗格几何保持稳定；每种状态标签栏仍高 28 像素。
三主题 Home 图像在前 28 像素以下与基线逐像素一致；已人工复核真实窗口截图。

官方 Windows Qt5.15.2 头文件交叉编译实现、头文件探针、moc 和完整应用测试
通过，仅保留原有测试 QLabel::pixmap 弃用警告。这不是 Windows 链接或原生
交互验收。搜索栏遮挡标签、命令区拥挤及暗主题命令区颜色仍是独立已知问题，
本提交不把整个界面称为已完成扁平化。

提交前对最终三文件哈希建立独立验收树，完整 Linux 74 套再次得到
3961 passed / 0 failed / 12 skipped；SingleInstance 仍因云端 socket 限制整套
排除并单列。覆盖实际窗口的生成文本编辑/QAT 保存逐字节核对、关闭取消不改原件、
只读保护、编码会话恢复，以及 MergeView/MergeOutput 的合并和保存边界。
最终实际程序重建后 Unicode 位置/命名 CLI、错误输入和离屏 GUI 启动再次通过，
生成源文件字节未变。该二进制 SHA-256：
`d890e043a4ffe7dbdbdc5a7809eb508a0c335fc94e09eb7cacb958a3f2b0d305`。
原生桌面合成器、打包后干净启动及 macOS 完整程序仍未由本地测试覆盖。

## UI-004 / #4：搜索布局与真实输入入口

真实控件基线复现：搜索 QRect(458,7 524×22) 与标签相交 524×21；普通回车
只发出库的 searchAccepted，而应用监听的是另一个 showHelp 信号，没有弹出
应用搜索结果。将旧标题搜索隐藏，在 QMainWindow 的菜单控件区域用布局管理
独立搜索行；保留原生窗口框架和现有标签/页面。输入框使用直角、配对调色板、
随字体变化的宽度上限及既有放大镜图标，键盘和鼠标共用注册表检索入口。

保留现有确认流程，默认/逃逸选择明确为 No；命令是否可执行仍由注册表检查。
输入提交延后一轮事件循环，合并重复操作并用代号撤销已被 Esc/继续输入替代
的查询，避免 QLineEdit 自己的事件尚未返回就被模态流程销毁。结果框用受
QPointer 保护的堆对象，先清理对话框再执行命令；无匹配、多匹配、执行关闭
命令和模态期间窗口销毁均有回归。查询与结果按纯文本显示。

最终源码的完整 AppIntegration 45 / 0 / 0、CommandActions 19 / 0 / 0；新增
相关测试在 150% 和 200% 缩放各 28 / 0 / 0。完整应用集成 ASan+UBSan 45 / 0 / 0，
未启用泄漏检测，不宣称 LSan 验收。Home 画布实际宽度 800/1024/1440/1920、
8/16pt、浅/深主题与 Ribbon 收起/展开均精确断言几何；验证 Tab/Shift+Tab、
Enter/小键盘回车/放大镜、默认 No、显式键盘/鼠标 Yes、Cancel、禁用命令、
重复事件、快速撤销和窗口生命周期。生成 CRLF 文件的编辑、取消、会话切换
有原件字节检查。

提交前独立验收树全量 74 套得到 **3987 passed / 0 failed / 12 skipped**，
SingleInstance 因本云环境 socket 限制仍单独排除。真实程序重建后，CLI Unicode
比较/帮助/错误码、未注入 GUI 启动以及用只读测试驱动注入 Qt 事件的实际窗口
搜索/确认/取消均通过，生成源文件哈希不变；人工查看比较窗格和确认截图。
事件驱动探针不随产品分发，不等于原生桌面操作验收。该二进制 SHA-256：
`8d5b326a1bcf6db38c8e6dbc62958b055a61d8b935ee03580a917bee78ef6c92`。

补充真实程序生成文档验收：94 项文档约定内的文本/编码/BOM/空白/换行/目录/
报告转义/输入保护/错误/容量边界通过，含生成 DOCX/PDF 的不支持语义导入边界；
不是 Office/PDF 解析支持。另两项保守组合别名期望仍未满足：比较目录子文件的
外部硬链接用作报告/日志目的地时，外部别名被原子替换，源字节与 inode 保持。
未找到针对该组合的明确规格条款，因此记录为拒绝规则一致性待核查项，不称为
已证实的数据损坏，也不把 96 项写成全绿；故意写错结果的负对照确实失败。
同一独立矩阵已对本提交候选二进制重跑，结果相同。

界限：UI-004 的完整下拉候选、所属页面/快捷键呈现等仍未完成；当前是原有
确认式检索入口变得可用。窄窗口命令组拥挤、部分翻译和暗主题命令区仍是
独立问题。文本比较区已有宽度下限，真实文本窗口可能比请求的 1440 更宽，
不能用 Home 的精确宽度矩阵声称文本工具栏已响应式。macOS 原生完整程序及
桌面合成器尚未验收；现有 CI 缺私有 LqRibbon 时明确跳过应用构建/打包两套。

## UI-001 / #2：原生绘制与悬停测试的可靠性

首次完整依赖的 Qt 5.15.2/xcb 应用验收中，同一个悬停用例出现两个断言失败：
中灰背景上的最亮栅格像素只有 4.45186 对比度，以及鼠标移动后立即读回了旧背景。
产品的指定白色前景与 `#767676` 背景实际为 4.54222；抗锯齿后像素不能代替
指定颜色作 WCAG 对比度判定。原测试在 72 DPI 的本地复现也失败，证明并非仅 CI
偶发现象。另一个前提问题是 QWidget 版本的 `QTest::mouseMove` 经窗口服务器
异步返回，单次 `processEvents()` 不保证已经收到事件。

本次只修改 AppIntegration 测试，不修改产品字体、样式、颜色或布局。小型测试
绘图设备记录真实控件提交给 QPainter 的文本前景色，要求与预期精确相等，并与
实际抓取的背景检查**未取整的 4.5 下限**。屏幕抓图仍必须包含真实前景字形。
进入和离开分别有界等待实际鼠标状态与背景变化，不使用固定延时掩盖时序。
参考 [W3C 对比度解释](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)
及 [Qt 鼠标测试时序](https://wiki.qt.io/Writing_good_tests#Widgets_and_Windows)。

提交前完整 AppIntegration 在默认 DPI 与 72 DPI 各 **45 / 0 / 0**；72/96/100/120
DPI、125%/200% 缩放的相关用例通过，72 DPI 连续重复五次通过。六个隔离负对照
全部被拒绝：原白字白底、错误 Midlight 配对、4.47809 的低对比度、近似但不正确
的前景色、悬停背景不变，以及颜色记录正确但屏幕字形被抹去。

实际程序另行重建，Unicode 相同/不同文件、帮助、非法选项和 GUI 启动通过；
事件驱动的实际搜索回车、默认 No、Cancel 通过，自建输入文件哈希不变。所有
本地执行仍为 Linux/offscreen；原生 xcb/Cocoa/Windows 结果需按对应 CI 提交
单独核对，不能以此测试修正声称完整三平台产品验收。
