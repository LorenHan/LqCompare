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
