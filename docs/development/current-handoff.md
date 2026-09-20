# 当前进度与接手说明

> 更新时间：2026-09-20。**新开一个对话接手本项目时，先读这一页。**
> 详细的并行划分见 [parallel-workstreams.md](parallel-workstreams.md)。

## 1. 一句话现状

规格（369 条）与 GitHub issue 已全部铺好；Qt 工程骨架已在 macOS 上编译通过、
主程序可启动、测试全绿。**服务层开始有真实功能**：文件系统抽象层（PLAT-002）、
回收站服务（PLAT-003）、名称处理（PLAT-007）、批量操作的失败处置（PLAT-008）、
系统图标服务（PLAT-004）与 Shell 集成（PLAT-005）已落地。其中回收站在本机是
**真的能删进废纸篓再还原回来**的；名称处理连「无效 UTF-8 的文件名」这种只在 Linux 上
出现的输入都写好了测试（CI 上会真实执行）；错误路径现在携带**原始系统错误码**
（errno / Win32 / Cocoa），批量操作会给出按原因分组的失败清单并支持只重试失败项；
系统图标是按**类型**缓存 + 后台解析 + 去重的，在本机能真的拿到 Finder 那一套图标；
Shell 集成的注册表计划、安装回滚、卸载还原与残留检查全部在**内存注册表**上真实执行，
因此这台 macOS 上跑的是完整流程，而不只是编译过；分级日志（ENG-006）现在
级别过滤对宏与直接调用一视同仁、输出行带线程 id、并支持挂任意接收者与 RAII 计时；
掩码语法与过滤声明（FILT-001）也落地了——21 条语法速查条目连同它们的样本
都是**可执行**的，因而「帮助里写的行为」和「程序的行为」不可能分家；
**会话这把钥匙（SESS-001）也插进去了**：会话抽象基类给出了统一契约
（视图 / 打开 / 关闭 / 重载 / 保存 / 脏标记 / 设置）与三个公共出口
（状态栏文本、错误上报、进度上报），并且「基类不依赖任何具体视图」是一条
**编译期**护栏——基类一旦 include 具体视图头文件，`Tests/Session` 会直接构建失败。
它是本仓库第一个链接 QtWidgets 的模块，也是第一个跑在 offscreen 上的测试套件。
会话的设置接口单独落在 `Services/Session/`：作用域链与落盘还没做，但接口先定下来了，
因此后续的过滤器与选项页不必各自发明一套「会话设置」。
**会话类型的登记处（SESS-002）也补上了**：14 种内置类型（ID / 显示名 / 英文原名 /
图标键 / 默认文件掩码 / 分组 / Pro 归属 / 平台限定）落成一张**纯数据**的表，注册表按
「注册顺序 = 优先级」回答「这个文件该用哪个视图打开」，并按分组枚举出 Home 页与新建
向导需要的入口。它的默认文件掩码**复用** FILT-001 那套掩码语言，而不是再写一份
「看扩展名」的匹配；「创建工厂」的返回类型只前向声明了 `CompareSession`，
因此这个模块是纯 QtCore 的——本仓库第一次有「服务层的会话框架测试」（`QT -= gui`）。
界面尚未接它：那批代码目前的生产调用方只有启动自检，把条目喂给 Home 页与新建向导
属于 SESS-003 / SESS-005。
界面上仍是 169 个按钮里 31 条带处理器，其余点击后提示对应 ACTION-ID；
会话基类与类型注册表都还没有被容器拿去建标签。

## 1.1 已落地的服务层模块

| 模块 | 条目 | 状态 | 测试 |
| --- | --- | --- | --- |
| `Services/Command/` | UI-024 | 骨架 | `Tests/CommandRegistry`（14 用例） |
| `Services/Log/` | ENG-006 | **部分完成**（界面输出面板尚未接线） | `Tests/Logging`（32 个用例函数） |
| `Services/Filter/` | FILT-001 | **部分完成**（界面上的速查与实时预览尚未接线） | `Tests/Filter`（87 个用例函数） |
| `Services/Files/`（文件系统） | PLAT-002 | **部分完成**（Windows 实现未编译验证） | `Tests/FileSystem`（50 用例） |
| `Services/Files/`（回收站） | PLAT-003 | **部分完成**（Windows 实现未编译验证） | `Tests/Trash`（35 用例） |
| `Services/Files/`（名称与 Unicode） | PLAT-007 | **部分完成**（长路径只写在 Windows 侧，未编译验证） | `Tests/PathName`（40 用例 + 1 个仅 Linux 执行） |
| `Services/Files/`（错误携带与批量处置） | PLAT-008 | **部分完成**（界面动作尚未接上） | `Tests/Batch`（36 用例） |
| `Services/Platform/`（系统图标） | PLAT-004 | **部分完成**（Windows / Linux 实现未在目标平台验证；界面尚未取用） | `Tests/PlatformIcon`（46 个用例函数，含 3 条走真实图标源） |
| `Services/Platform/`（Shell 集成） | PLAT-005 | **部分完成**（Windows 注册表薄层未编译过；界面尚未接入；本机平台能力置灰说明已落地） | `Tests/ShellIntegration`（101 个用例函数） |
| `Services/Session/`（会话设置接口） | SESS-001 | **部分完成**（只有接口与内存实现；作用域链与落盘留给 SESS-006 / SESS-007） | `Tests/Session`（42 个用例函数，与下一行同一套件） |
| `Views/Session/`（会话基类） | SESS-001 | **部分完成**（第 2 条里「并注册」那半句依赖 SESS-002，已落地但基类尚未接上注册表；界面尚未取用） | `Tests/Session`（42 个用例函数） |
| `Services/Session/`（类型注册表） | SESS-002 | **部分完成**（第 2 条里「并注册」的前半句——按类型 ID 造会话——要等各会话类型实现出来；界面尚未取用） | `Tests/SessionType`（57 个用例函数，**纯 QtCore**） |

PLAT-002 的详细说明与其「第 2 条完成标准为何不勾选」见
[issue #325](https://github.com/LorenHan/LqCompare/issues/325)；
PLAT-003 见 [issue #324](https://github.com/LorenHan/LqCompare/issues/324)；
PLAT-007 见 [issue #328](https://github.com/LorenHan/LqCompare/issues/328)；
PLAT-008 见 [issue #330](https://github.com/LorenHan/LqCompare/issues/330)；
PLAT-004 见 [issue #329](https://github.com/LorenHan/LqCompare/issues/329)；
PLAT-005 见 [issue #326](https://github.com/LorenHan/LqCompare/issues/326)；
ENG-006 见 [issue #338](https://github.com/LorenHan/LqCompare/issues/338)；
FILT-001 见 [issue #228](https://github.com/LorenHan/LqCompare/issues/228)；
SESS-001 见 [issue #36](https://github.com/LorenHan/LqCompare/issues/36)；
SESS-002 见 [issue #37](https://github.com/LorenHan/LqCompare/issues/37)。

### 1.2 回收站（PLAT-003）落地到了什么程度

| 平台 | 实现 | 在本机验证过 |
| --- | --- | --- |
| macOS | `trash_mac.mm`，`NSFileManager trashItemAtURL:` | **是**——真实往返（移进废纸篓 → 断言文件确实在废纸篓里 → 还原 → 断言回到原处） |
| Linux | `trash_linux.cpp`，XDG 规范（`~/.local/share/Trash` + `.trashinfo`） | 部分——路径与格式规则在 `trash.cpp` 里，**已在 macOS 上真实执行**；系统调用部分未在 Linux 上跑过 |
| Windows | `trash_win.cpp`，`SHFileOperationW` + `FOF_ALLOWUNDO` | **否**——从未编译过 |

删除的入口从 `FileSystem` 移到了 `TrashService`（`Code/Services/Files/trash.h`）。
**迁移的理由值得记住**：`FileSystem` 是无状态的，而「撤销最近一次删除」需要
一个长期存在的撤销点。若在 `FileSystem` 上留一个便捷转发，实现必然是
「每次调用现场 new 一个 TrashService」，于是撤销点随对象一起被丢掉，
用户点撤销永远报「没有可还原的删除」——而删除本身是成功的，只有撤销不工作。
这类缺陷很难查。所以删除只保留一条入口。

`undoLastDelete()` 在 Windows 上返回 `NotSupported`，这是规格明确允许的
「受平台能力限制时说明」：回收站里的条目是一对 `$R`/`$I` 文件，不是普通文件，
还原要走 Shell 命名空间扩展。`displayLocation()` 在 Windows 上返回
`shell:RecycleBinFolder`，界面可以用它提供「打开回收站」入口让用户手工还原。

### 1.3 PLAT-008 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 四类错误分别识别并给出不同建议 | `errorAdvice()`（PLAT-002 已就位），`Tests/FileSystem` 里有一条用例断言四条建议互不相同 | **已落** |
| 错误信息包含原始系统错误码 | `ErrorCode`（分类 + 域 + 原始值）、`errorDetail()`、`errorReport()`；`fromSystemError` / `fromWindowsError` / `fromCocoaError` 是唯一正确出口 | **已落** |
| 批量中失败的条目汇总为失败清单，可单独重试 | `BatchReport::failureGroups()`、`FailureGroup`、`BatchOperation::retryFailed()` | **已落** |
| 「重试失败项」与「跳过并继续」两条出路 | `retryFailed()` / 直接读取报告接受当前进度；`BatchFailurePolicy` 显式声明 | **已落** |
| 长任务中途错误不中断整体，保持已完成进度 | 默认策略下循环不停；`retryFailed()` 把结果**合并**回整批报告而不是替换 | **已落** |

**还没有做的**：界面上的动作还没接上。也就是说，`batch.h` 提供的失败清单、
两条出路与进度回调目前只有测试在用，Ribbon 上还没有一个按钮会走进去。
这一步要等 SESS（会话）与视图层就位，因为「失败清单」需要一个可停留的对话框，
而「重试失败项」需要一次批量操作作为上下文。

### 1.4 PLAT-004 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| 按扩展名获取系统关联图标（Win `SHGetFileInfoW` / mac `UTType`+`NSWorkspace` / Linux 主题图标） | `iconservice_mac.mm` / `iconservice_win.cpp` / `iconservice_linux.cpp` | **macOS 是**（三条真机用例拿到真实像素，且文字文件与文件夹的图不同）；Windows 侧**从未编译过**；Linux 侧未在 Linux 上跑过 |
| 图标缓存按扩展名而非按文件，缓存命中率高 | `IconKey::cacheKey()`（键 = `f|txt` / `d|<dir>`）、`IconCache`（有界 LRU，带 `Stats::hitRate()`） | **是**——`serviceCachesByExtensionNotByFile` 断言同一类型的多个文件只解析一次，`serviceDoesNotLetDirectoryPoisonFileKey` 断言带扩展名的目录不会污染同名类型的文件 |
| 图标获取在后台线程，缺失时回退到内置的通用图标 | `IconService::requestIcon()` + 专属单线程 `QThreadPool` + `IconRequestQueue` 去重；`IconSource::Builtin` 是回退 | **是**（用可替换的假提供者断言调用次数与去重；另有真机用例） |
| 系统图标不可用时（无桌面环境）回退到内置图标集，不崩溃 | `createNativeIconProvider()` 在无可用图标源时返回 `HeadlessIconProvider`（Linux）；macOS 老系统走 `@available` 之外的分支；提供者抛异常由 `WorkItem` 吞掉 | **部分**——回退路径（`serviceFallsBackToBuiltinWhenProviderHasNothing`）与「有在途请求时析构」（`serviceDestructsWithPendingWork`）有覆盖；「真的没有桌面环境」只能靠注入假提供者模拟，本机没法真跑 |
| 图标大小随 DPI 与界面缩放正确获取 | `iconPixelSize(baseSize, devicePixelRatio)`、`Win32IconSize::nearest()`、`IconEntry::actualPixelSize` 如实报出真实尺寸 | **部分**——缩放算法与档位收拢是纯逻辑、已覆盖；Windows 上真实拿到的尺寸未验证 |

**还没有做的**：界面还没取用。`IconService` 目前只有测试在用，文件夹树与列表
还没接上（会话与视图层就位后一起做）。另外 Windows 只接了 16/32 两档，
48/256 需要 `IImageList` COM——代码里已注明，`actualPixelSize` 会如实报出
「其实只拿到了 32」，不会假装请求的尺寸就是拿到的尺寸。

### 1.5 PLAT-005 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| Windows：右键菜单项（比较 / 与…比较 / 作为左右侧比较），通过注册表实现 | `buildShellIntegrationPlan()` 生成三类目标（`*` / `Directory` / `Directory\Background`）下的 `shell\LqCompare.<动作>` 动词键（`MUIVerb` / `MultiSelectModel` / `Position` / `Icon` + `shell\command`）；`install()` 写入 | **是（在内存注册表上）**——菜单项内容、加速键唯一性、单选/多选约束、位置与图标选项、命令行的引号与动作开关都有用例；但**没有**在真实 Windows 资源管理器里看过 |
| 「选择第二个文件后比较」的两步式交互（占位菜单项） | `ShellAction::CompareSecondStep` + `shellActionIsPlaceholder()`；菜单文字带省略号，命令行走 `%V`（第二个选中项） | **是**——`onlySecondStepIsPlaceholder` 断言只有它是占位；`includedByOptionsRespectsTwoStepSwitch` 断言关掉后「与…比较」与占位项一起消失 |
| 文件关联可注册 `.patch` / `.diff`，可单独关闭 | 计划里 `.patch` / `.diff` 写 `Software\Classes\.<ext>` 的默认值指向 `LqCompare.PatchFile` / `LqCompare.DiffFile`；扩展名键是 **Shared**（用户的既有值先备份），ProgID 是 **Owned** | **是**——`planWithoutPatchOmitsPatchEntries` 断言关掉 `.patch` 后 `.diff` 完好，反之亦然；`uninstallRestoresPreviousAssociation` 断言还原成用户原来的 ProgID 而不是删掉 |
| 一键安装与一键卸载；卸载后注册表无残留（有校验） | `install()`（含步骤 0「已安装则先按记录的选项拆掉」、失败整体回滚）、`uninstall()`、`verify()`、`findResidue()`；`InstalledState` 记录版本、路径、选项与备份数 | **是**——101 个用例覆盖安装/回滚（`failedInstallLeavesStoreEmpty` 断言回滚后存储**一条不剩**）、卸载还原（含 `Unsupported` 值类型逐字节还原）、重复安装沿用最初那份备份、外来内容保留并说明、校验能区分「缺失」与「值不对」、残留四类发现 |
| 非 Windows 平台该能力置灰并说明 | `registrystore_stub.cpp` 的 `UnsupportedRegistryStore` + `platformRegistryUnsupportedReason()` / `platformRegistryUnsupportedAdvice()`；macOS 建议走「自动操作 / 访达扩展」、Linux 建议走「Dolphin 服务菜单 / Nautilus 脚本」 | **是**——本机就是非 Windows，`capability().available` 为假、原因与建议都断言非空；读给出「什么都没有」（不报错）、写返回 `NotSupported` 而不是「假成功」 |

**还没有做的**：

1. `registrystore_win.cpp` **从未被编译过**（与 `trash_win.cpp` 等同一个道理）。
   它里面有一处必须留意的选择：所有访问都带 `KEY_WOW64_64KEY`。交付目标是
   32 位 MinGW 构建，不加这个标志时会写进 `WOW6432Node` 影子副本，
   而 64 位资源管理器看不见那里——安装、校验、卸载全都报成功，菜单里却什么都没有。
2. **界面还没接上**。`ShellIntegration` 目前只有测试在用，Ribbon 上还没有
   「安装 Shell 集成 / 卸载 / 检查残留」的入口，也没有一个对话框展示
   `ShellIntegrationReport::lines()`。这一步与 PLAT-004 的界面接入是同一件事的两半，
   一起做更省事（都需要 OPT 设置页）。
3. **命令行入口只解析、不执行**。`parseShellInvocation()` 已经能把
   `--shell-action=compare "a.txt" "b.txt"` 解成结构化的 `ShellInvocation`
   （含「同一个路径选两次」这类无效输入），但 `main.cpp` 还没有按它去开会话——
   那要等 SESS 会话框架就位（CLI-001）。
4. `Icon` 注册表值用的是可执行文件的图标索引 0。想要一个专用图标需要把图标
   编进 exe 的资源节（`.rc` 文件），本项目还没有加——索引指向不存在的资源时
   资源管理器显示**空白占位**而不是报错，所以这个值必须与打包方式一起改。

### 1.6 ENG-006 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| 分级日志（五级）与分类标签 | `Level`（Error/Warning/Info/Debug/Trace）、`LQCOMPARE_ERROR/WARN/INFO/DEBUG/TRACE`，分类是宏的第一个参数 | **是**——五个级别逐个走过「名字 → 级别 → 输出」，且断言级别标识互不相同、都不为空、都不等于兜底的 `unknown` |
| 级别未启用时参数不求值 | 宏先调 `isEnabled()` 再拼消息 | **是**——用带计数副作用的表达式验证：关掉时求值 0 次，打开时**恰好 1 次**（2 次说明宏体里出现了两遍参数） |
| 输出到控制台、文件、界面输出面板三个目标 | 控制台（`stderr`）与文件已接；界面输出面板通过 `addSink()` / `removeSink()` / `clearSinks()` 接入 | **两个半**——控制台与文件有覆盖（追加而非截断、不可写路径要返回失败、关掉之后不再写入）；接收者机制有覆盖（结构化字段、多接收者顺序、移除、重入不死锁、跨线程调用）；**输出面板本身尚未接线**，原因见下 |
| 格式含时间戳、级别、分类、线程 id、消息 | `Record`（结构化载体）+ `Record::line()`（规范文本行）。线程名非空时也带上 | **是**——时间戳可还原成合理时刻（不是「行里有数字」）、级别是定宽短名、分类、`[t:<十六进制>]`、消息；并断言两个目标拿到**逐字相同**的文本 |
| 「记录耗时」辅助（进入/退出自动计时） | `Log::Stopwatch`（RAII）+ `LQCOMPARE_SCOPE_TIMER` 宏 | **是**——析构时记一条「X 耗时 N ms」、级别关掉时静默、备注写在同一行、中途可查 `elapsedMs()`、`finish()` 可重复调用只记一条、**级别在析构时判断**（先放计时器再调级别也能出结果） |

**顺带修掉的一个真实缺陷**：`Log::write()` 原先**完全不做级别过滤**，
只有宏里那个 `if` 在过滤。于是 `main.cpp` 里 5 处直接调用（含启动横幅）
在 `--log-level error` 下照样打印，与 `logging.h` 写的「低于该级别的日志被丢弃」
相反，日志文件也没法靠调级别瘦身。现在 `isEnabled()` 是唯一的判断处，
宏、`write()`、`Stopwatch` 三处都走它——各自写一遍 `<=` 的话，把方向弄反
只会发生在其中一处，而现象是「某个级别偶发不输出」。

**行为上的一个可见变化**：默认级别是 `warning`，而启动横幅是 `info`，
所以**不带参数启动时日志里不再有启动横幅**（原先有，因为当时 `write()` 不过滤）。
交接文档里的验证命令本来就带 `--log-level info`，照常工作。
如果希望「用户什么都不说时日志里也有一条启动锚点」，那是 OPT-010 的默认值问题，
不是日志模块的问题——不要用「让 `write()` 不过滤」去解决它。

**还没有做的**：界面输出面板尚未接线。接收者机制（第三个目标）已经就位并有跨线程用例，
但把 `MainWindow` 的输出面板挂上去时要**加一次排队跳转**：
接收者在「记录日志的那个线程」上被调用，而图标解析跑在后台线程上，
从那里碰控件会崩。正确姿势是让接收者只 `emit` 一个信号，
再以 `Qt::QueuedConnection` 连到面板的槽。这一步与 PLAT-004 / PLAT-005 的
界面接入是同一批活（都需要 OPT 设置页），一起做更省事。

### 1.7 FILT-001 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| 支持 `*`（任意字符）、`?`（单字符）、`[...]`（字符集）、`**`（跨目录）等语义 | `Mask::compile()` / `Mask::matches()`；`*` 不跨 `/`、`?` 恰好一个字符、字符集含 `[!...]` / `[^...]` 取反与 `[a-z]` 区间、`**` **独占一段**时跨目录 | **是**——87 个用例函数里，A/B/C 三组共 33 个用例专测语义边界：`*` 吃零个字符、区间上下界含、`[a-]` 与 `[-a]` 里的 `-` 退化、`]` 写在最前面是字面量、`a?b` 不命中 `a/b`、`build/**` 也命中 `build` 本身、`a**b` 不跨目录 |
| 支持排除掩码（前导 `-`）与包含掩码同时声明，排除优先 | `MaskFilter::parse()` 逐行解析；`MaskFilter::decide()` 先扫排除再扫包含 | **是**——`excludeWinsOverInclude` 断言 `*.cpp` + `-*_test.cpp` 下 `main_test.cpp` 不被接受；另有「只写排除 → 其余全保留」「只写包含 → 白名单」「空声明 → 全保留」三条 |
| 掩码匹配在 Windows 上默认大小写不敏感、Unix 上默认大小写敏感，且可显式覆盖 | `defaultCaseSensitivity(MaskPlatform)`（**平台是显式参数**）+ `MaskPlatform` 枚举 + `setCaseSensitivity()` / `clearCaseSensitivityOverride()` | **是**——**两种平台的默认值都在本机被断言**（Windows 那条不靠 `#ifdef` 分支绕过去）；另有「显式覆盖生效」「清掉覆盖回到平台默认」「`[a-z]` 与 `[A-Z]` 在不敏感下双向成立」「不敏感不会让 `[A-_]` 的区间反转」 |
| 提供掩码语法速查与实时预览（输入掩码后显示「匹配 N 项 / 共 M 项」） | 速查：`maskSyntaxReference()` 给出 21 行、每行都带可执行样本，另有 `maskSyntaxReferenceText()` 生成纯文本表；预览：`preview()` / `previewNames()` / `MaskFilterPreview::summary()` | **速查是**（测试会遍历这 21 行、把 53 条样本**真的跑一遍**，并断言纯文本速查里含每一个掩码与样本）；**计数是**（`summaryUsesTheWordingFromTheSpec` 断言文案逐字等于「匹配 2 项 / 共 3 项」）；**界面上的速查面板与实时预览不是**，见下 |
| 解析器为纯函数并有完整单元测试（含恶意与畸形输入） | `Mask::compile()` 无全局状态、不碰文件系统；`Tests/Filter` 的 I 组专测恶意输入 | **是**——`compileIsPureAndRepeatable` 断言同一输入两次得到同一结果（含失败位置）；I 组 6 条覆盖 `**/**/…` 对上 40 段路径的 2^N 回溯、`*a*a*…` 的段内指数退化、400 段路径、5000 字符掩码、500 成员字符集 |

**顺带定下来的两条对外约定**（都不是规格要求的，但不定下来会各自乱长）：

1. **掩码里的分隔符恒为 `/`，`\` 是转义字符**，与平台无关。于是
   `build\out` 会得到一条带建议的错误（「`\` 只能用来转义 `* ? [ ] - # \`，
   如果这是 Windows 路径分隔符请改写成 `/`」），而不是静默变成 `buildout`。
2. **不含 `/` 的掩码按名字匹配**（`*.txt` 在任意目录下都命中），
   **含 `/` 的按相对路径从起点匹配**（`src/*.txt` 不命中 `x/src/a.txt`）。

**还没有做的**：界面上的速查面板与实时预览尚未接线。`preview()` 与
`maskSyntaxReferenceText()` 目前只有测试在用——「输入掩码后显示匹配 N / 共 M」
需要一个 Filters 页或会话设置页来承载，而那要等 SESS/OPT 的工作流；
与 PLAT-004 / PLAT-005 / ENG-006 的界面接入是同一批活。另外
FILT-002（正则与超时保护）、FILT-003（属性过滤）、FILT-005（三层叠加）
等都还没开始，它们都会**复用**这一份掩码实现。

### 1.8 SESS-001 落地到了什么程度

规格的四条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 基类定义统一契约：createWidget、open、close、reload、save、isDirty、canSave、sessionSettings | `CompareSession`（`Views/Session/comparesession.h`）。`sessionSettings` 的返回类型是 `Services/Session/session.h` 里的 `SessionSettings` 接口 | **已落** |
| 新增一种会话类型只需实现基类契约并注册，不需要改动已有会话代码 | 「实现基类契约」那半句：`Tests/Session` 里的 `MinimalSession` 只写了 `createView()` 一个实现点，就走通了「打开 → 拿视图 → 标脏 → 保存 → 重载 → 关闭」全部动作。「并注册」那半句在 SESS-002 落地后补上了：G 组把一个只实现基类契约的类型的工厂登记进 `SessionTypeRegistry`，再从注册表把会话**造出来**并走完同一个生命周期——全程没有改动 `CompareSession` | **已落** |
| 基类提供状态栏文本、错误上报、进度上报三个公共出口 | `setStatusText()` / `reportError()` / `reportProgress()` 三个 public 出口，配 `statusTextChanged` / `errorReported` / `progressChanged` 三个信号；载体是 `SessionError`（message + detail）与 `SessionProgress`（current + total + what + `percent()`） | **已落** |
| 基类不依赖任何具体视图头文件（编译期校验） | 编译期那一半：`SessionTests.pro` 的 INCLUDEPATH 里只有 `Views/Session` 与 `Services/Session`，基类一旦 include 具体视图头文件，**该测试工程直接构建失败**。源码级那一半：`Tests/Session` 的 F 组用例把两个源文件的 `#include "…"` 与白名单比对，并另有一条用例对**故意写坏的源码**做反向验证 | **已落** |

**第 2 条为什么分了两轮才算完成**：「新增一种会话类型」在这个仓库里是两步——
实现基类契约，然后在**类型注册表**里登记（类型 ID、显示名、图标、默认掩码、
创建工厂）。SESS-001 那一轮只有第一步可做：注册表是 SESS-002，还不存在时没有
第二个地方可以登记，而临时造一个「只登记类型 ID 的小表」等于把 SESS-002 的契约
提前定死一份，两份类型表必然分歧。SESS-002 落地后，G 组把第二步补上，
**因此这一条现在可以勾了**。

**诚实地说清这条的证据边界**：目前「已有会话代码」只有基类本身与两个测试替身，
因此这条标准证明的是「新增类型不需要改动基类与既有类型的代码」。
真正的第二种会话类型要等 `TEXT-*` / `FOLD-*` 落地，届时它只是「再加一个类型」——
如果那时发现还要改基类，说明这一条的结论当时下早了。SESS-002 本身是一次
正向数据点：它是新模块 + 新测试，**没有改动 `comparesession.h` 一行**。

**还没有做的**：

1. **界面还没取用**。`CompareSession` 与 `SessionTypeRegistry` 目前只有启动自检
   与测试在用，`SessionArea::addSession()` 仍然创建占位页。接上要等 SESS-003
   （Home 视图）——Home 页要按注册表的分组枚举生成入口，容器要按类型 ID 造会话。
2. **设置的落盘与作用域链还没有**。`MemorySessionSettings` 只保证「本次运行期间
   读得到、写得进」，重启即丢。它**不谎称已落盘**：既然是内存实现，就没有
   「保存失败」这种状态需要上报。作用域三层（视图 > 会话 > 类型）是 SESS-007。
3. **三个公共出口还没有接收方**。信号已经在了，但状态栏、错误对话框与进度条
   都还没有连上去——那与 PLAT-004 / PLAT-005 / ENG-006 / FILT-001 的界面接入
   是同一批活（都要等 OPT 设置页与真正的会话视图）。

**两条刻意的边界**（改之前先读这三段理由，否则很容易把它们「修」回去）：

- `close()` **不销毁视图**。视图的父子关系属于容器；基类无法知道这个视图有没有
  被别处引用（分离窗格会把同一个视图挂到另一个窗口下），替容器删就是越权。
- 「未保存改动时拒绝重载」与「没有改动时拒绝保存」都放在**基类**，不在各会话类型里。
  忘了判的后果分别是「几十处编辑无声消失」与「同步目录里一串无意义的版本」，
  两条都不该由每个新增的会话类型各自记得。
- `open()` 幂等且**第二次不调 `doOpen()`**。界面在恢复标签时会重复调用它，
  重跑一遍会把滚动位置与编辑状态全部重置。

### 1.9 SESS-002 落地到了什么程度

规格的四条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 注册表条目包含：类型 ID、显示名、图标、默认文件掩码、创建工厂、是否 Pro 特性、是否平台限定 | `SessionType`（七个字段齐备）+ `SessionTypeEntry`（描述子 + 工厂）。`add()` 逐条校验：ID 格式、ID 重复、显示名非空、每条掩码能编译，**任一条不过就整条拒绝** | **已落** |
| 类型 ID 有稳定性测试：已发布 ID 的字符串值被快照断言锁定 | `Tests/SessionType` 的 B 组：14 个 ID 的精确值**与顺序**被逐字比对；6 个非法 ID 形态被拒；`idsAreStableAcrossRebuilds` 盯住「表是每次重建的」这件事 | **已落** |
| 按掩码查询匹配类型时按注册顺序（优先级）返回首个命中 | `findByFileMask()` 顺序扫描、首个命中即返回；`findByMaskFollowsRegistrationOrderNotTableOrder` 用**反序登记**证明它按注册顺序而不是插入位置；内置表里 `*.html` 那处重叠是真实数据上的用例 | **已落** |
| 注册表可枚举，供 Home 页与新建向导直接生成入口 | `entries()` / `byGroup()` / `groups()`（`onlyAvailable` 可关，供界面置灰展示）+ `findByName()`。枚举顺序与注册顺序一致，因此「分组枚举的并集」可以整表比对 | **已落** |

**还没有做的**：

1. **界面还没取用**。`SessionArea::addSession()` 仍然创建占位页；Home 页仍在用
   自己那份硬编码的类型表。让 HomePage 改成读注册表是 SESS-003 的范围——
   本轮改了会与它撞车，因此先用一条**源码级用例**把两边钉在一起（见下）。
2. **创建工厂还没有真正的实现方**。内置 14 种类型的 `factory` 全为空——
   各会话类型（`TEXT-*` / `FOLD-*` / `HEX-*` …）还没实现，没有东西可注册进工厂。
   「类型已登记」与「这一版还没有这个视图」由 `hasFactory()` 分开，
   G 组用测试替身证明这条路是通的。
3. **默认文件掩码只是一组种子**，不是 Beyond Compare 那张完整的文件格式关联表。
   真正的权威在 `FORMAT-*`（文件格式定义）。现在如实只放无歧义的种子
   （`.csv` 给表格、`.png` 给图片、`.diff` 给补丁视图），好过抄一份假装完整的表。

**四条刻意的取舍**（改之前先读，否则很容易把它们「修」回去）：

- **`add()` 整条拒绝，不做部分接受**。一个「ID 进去了但掩码全丢了」的条目会让
  按掩码的自动选择**静默失效**——「掩码写错了」能在启动时发现，「某些文件双击
  没反应」不能。
- **内置表里 `*.html` 被文本比对与表格比对同时声明，这是有意留的**。合成的小表
  只能证明实现自洽，证明不了这张表落在什么结论上；留着它，`onlyHtmlOverlaps`
  与 `allByMaskKeepsTheOnlyRealOverlap` 就把「文本胜出」这个当前结论钉死了。
  要让表格胜出是一次产品决定，改的时候这两条会红，从而强制做那次决定。
- **`describe()` 与 `validate()` 是两件事**。前者给人看（`main.cpp` 与诊断输出用），
  后者是自检。`validate()` **只查登记时没有把住的**几件事（英文原名缺失、图标键
  写法、掩码写成大写）——把 `add()` 已经拦下的再查一遍，那些分支永远走不到，
  而一条永远不会红的护栏比没有护栏更糟。
- **注册表不做成单例**（与 `CommandRegistry::instance()` 不同）。表要被反复构造：
  合成的小表验证优先级、内置的大表验证快照。单例会让「这一次测试往表里加了什么」
  泄漏到下一处。

**过渡期的源码级护栏**：`homePageHardcodedIdsMatchTheRegistry` 会去读
`Views/Shell/homepage.cpp`，从 `sections()` 的函数体里抠出全部
`QStringLiteral("…")` 与注册表**逐个比对（含顺序）**。注册表落地前那张硬编码的表
是事实上的第一批「已发布 ID」，两边不一致会让 Home 页点出来的入口指向不存在的类型
（现象是「点卡片没反应」）。另有一条 `homePageIdCheckCanFailOnBrokenSource`
对一段**故意写错**的源码跑同一个判定流程，证明这条护栏不是恒真的。

## 2. 已验证的事实（不用再花时间确认）

| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试（全量） | **554 passed / 0 failed / 1 skipped**（Batch 36 + CommandRegistry 14 + FileSystem 50 + **Filter 89** + Logging 34 + PathName 40 + PlatformIcon 48 + **Session 48** + **SessionType 59** + ShellIntegration 101 + Trash 35） | `Code/Tests/run-tests.sh` |
| 文件系统抽象层 | 47 个纯逻辑用例 + 3 个真实文件系统用例全通过；其中 20 个覆盖 **Windows** 路径规则（盘符 / UNC / 长路径前缀 / 大小写），在 macOS 上真实执行 | `Code/Tests/run-tests.sh FileSystem` |
| 回收站 | 35 个用例全通过。其中 9 个验证 XDG（Linux）的路径与 `.trashinfo` 规则、2 个是**真实**的废纸篓往返与冲突拒绝、多个断言「不可用时搬移函数一次都没被调用」 | `Code/Tests/run-tests.sh Trash` |
| 名称与 Unicode | 40 个用例通过 + 1 个跳过（无效 UTF-8 名字的用例只在 Linux 上执行，CI 会跑）。覆盖字节保真往返、UTF-8 边界与过长编码、Unicode 组合形式、六类文件名问题的原因与位置 | `Code/Tests/run-tests.sh PathName` |
| 错误携带与批量处置 | 36 个用例全通过。其中 9 个验证错误码在三种域下的携带与显示（含「未识别的码只给数字」）、11 个验证失败清单分组、12 个验证执行流程（含「重试只跑失败项」与「停止不移除已完成进度」）、4 个走真实文件系统做一次「设为只读 → 解除只读」往返 | `Code/Tests/run-tests.sh Batch` |
| 系统图标 | 46 个用例函数（QTest 合计 48，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分四组：17 个验证缓存键与尺寸规则（扩展名折叠 7 + 键的合成与解析 5 + DPI 缩放 4 + Windows 档位收拢 1）、9 个验证有界 LRU 的淘汰与命中统计、6 个验证请求去重队列、11 个验证服务层（同步/异步/去重/回退/换比例清缓存）；另有 **3 个走真实系统图标源**（macOS 上真实执行：断言拿到非空像素、断言文字文件与文件夹的图确实不同） | `Code/Tests/run-tests.sh PlatformIcon` |
| 主程序构建 | 通过，`iconservice_mac.mm`、`mask.cpp` / `maskfilter.cpp`、`comparesession.cpp` / `session.cpp` 与本轮的 `sessiontype.cpp` 都编进主程序，**本仓库自己的代码 0 warning**。完整 `make clean` 之后重编，全部告警只有 1 条 `LqRibbon.cpp:569: unused function 'nativeWindowScaleFactor' [-Wunused-function]`——它属于 MyClass 那个仓库，不在本仓改动范围内 | `cd _build-lqcompare && ~/Qt/5.15.2/clang_64/bin/qmake ../Code/LqCompare.pro && make -j8` |
| 主程序运行 | 离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」「会话类型注册表：14 个类型，0 项问题」，注册表自检 0 问题 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| Shell 集成 | 99 个用例函数（QTest 合计 101，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分十一组：A 动作与目标 14、B 选项 8、C 命令行引号 8、D 计划 13、E 安装 10、F 卸载与还原 9、G 校验 5、H 残留 6、I 能力 4、J 预演 3、K 命令行解析 9。**全部跑在功能完整的内存注册表上**，因此安装回滚与卸载还原是在本机真实执行的流程，不是桩 | `Code/Tests/run-tests.sh ShellIntegration` |
| Shell 集成的命令行引号 | 用测试内置的 `CommandLineToArgvW` 参考实现做往返：`"C:\Program Files\…\LqCompare.exe" --shell-action=compare "%1"` 切回来必须还是两个原值，含「结尾反斜杠要翻倍」这条最容易写错的规则 | `Code/Tests/run-tests.sh ShellIntegration` |
| 分级日志 | 32 个用例函数（QTest 合计 34，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分五组：A 级别与过滤 6、B 格式 4、C 输出目标 11、D 耗时辅助 6、E 级别名解析 4、以及 `initTestCase`/`cleanupTestCase`。这套件**刻意不链接 QtGui**：哪天有人往 `logging.cpp` 里加图形依赖，本工程会立刻构建失败 | `Code/Tests/run-tests.sh Logging` |
| 掩码语法与过滤声明 | 87 个用例函数（QTest 合计 89，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分九组：A 掩码基本语义 13、B 字符集 12、C 跨目录 8、D 大小写策略 9、E 声明解析 17、F 叠加 9、G 预览 6、H 语法速查 7、I 恶意与畸形输入 6。这套件同样**刻意不链接 QtGui**（掩码只处理字符串） | `Code/Tests/run-tests.sh Filter` |
| 掩码语法速查与实现同源 | 21 条速查条目、53 条样本被逐条**真的跑一遍**（掩码类走 `Mask::compile` + `matches`，声明类走 `MaskFilter::parse` + `accepts`），因此「帮助里写的行为」与「程序的行为」不可能分家。另有断言：纯文本速查表里含每一个掩码与样本（它确实是生成物）、条目无重复、全表同时出现「匹配」与「不匹配」两种样本 | `Code/Tests/run-tests.sh Filter` |
| 掩码的恶意输入有界 | `**/**/…`（24 个）对上 40 段路径、`*a*a*…`（12 个）对上 200 个 `a`、400 段路径、5000 字符掩码、500 成员字符集——全部在毫秒内出结果。这几条盯的是**指数级退化**（朴素递归分别是 2^40 与 2^200 量级），不是性能基线 | `Code/Tests/run-tests.sh Filter` |
| 会话抽象基类 | 46 个用例函数（QTest 合计 48，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分七组：A 生命周期与状态迁移 19、B 视图契约 5、C 三个公共出口 7、D 设置接口 7、E 可扩展性 2、F 源码级护栏 2、G 与类型注册表的衔接 4。**这是本仓库第一个链接 QtWidgets 的测试套件**（基类的 `createWidget()` 返回 `QWidget*`），跑在 offscreen 平台上 | `Code/Tests/run-tests.sh Session` |
| 会话类型注册表 | 57 个用例函数（QTest 合计 59，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分六组：A 条目字段齐备 13、B ID 稳定性与登记校验 11、C 按掩码查询与注册顺序 14、D 可枚举 7、E 按名字查 5、F 自检与两道源码级护栏 7。这套件**刻意不链接 QtGui**（注册表是纯逻辑）——它是本仓库第一次有「服务层的会话框架测试」 | `Code/Tests/run-tests.sh SessionType` |
| 会话类型注册表能反向验证 | 六处变异逐一被拦下：对调 `text` 与 `table` 的登记顺序 → 快照、重叠结论与 Home 页一致性三条红；改掉 `homepage.cpp` 里一个类型 ID → Home 页一致性红；给表格类型多加一条与文本重叠的掩码 → `onlyHtmlOverlaps` 红；把一条内置掩码写成大写 → `validate` 两条红；让 `findByFileMask` 不跳过当前平台不可用的类型 → 该条红；`add()` 不再校验 ID 格式 / 不再拒绝重复 ID / 不保存传入的工厂 / `byGroup` 不按可用性过滤 → 各自对应的用例红。全部检出 | 变异测试（结论写在 issue #37 的落地说明里） |
| 会话类型 ID 与 Home 页硬编码的 ID 一致 | 源码级比对：`Tests/SessionType` 读 `Views/Shell/homepage.cpp`，从 `sections()` 里抠出 14 个 `QStringLiteral("…")`，与 `builtInSessionTypeIds()` **逐个比对（含顺序）**；另有一条用例对一段故意写错 ID 的源码跑同一流程，证明判定不是恒真 | `Code/Tests/run-tests.sh SessionType` |
| 掩码语言只有一份实现 | `SessionTypeRegistry` 的默认文件掩码直接 `#include "mask.h"` 编译与匹配，因此 `**`、字符集、转义这些语义**免费成立**。有一条用例注册自定义的 `src/*.txt` 掩码并断言 `src/a.txt` 命中、`other/a.txt` 与根下的 `a.txt` 不命中——这条在自制「看扩展名」的实现下必然失败 | `Code/Tests/run-tests.sh SessionType` |
| 会话基类的用例能反向验证 | 五处变异逐一被拦下：`createWidget()` 每次重建 → `createWidgetBuildsTheViewOnlyOnce` 与 `createWidgetPassesTheParentThrough` 红；`save()` 去掉 `canSave()` 守卫 → 4 条红；`reload()` 去掉脏守卫 → `reloadRefusesWhenSessionIsDirty` 红；`save()` 成功后不清脏 → 2 条红；给基类加一行 `#include "homepage.h"` → **测试工程构建失败**（编译期护栏生效）。五处全部检出 | 变异测试（结论写在 issue #36 的落地说明里） |
| 会话基类不依赖具体视图 | 编译期：`SessionTests.pro` 的 INCLUDEPATH 里只有 `Views/Session` 与 `Services/Session`，构造出的是**失败**而不是「用 include 白名单扫一遍心理上放心」。源码级：F 组两条用例，其中一条对故意写坏的源码做反向验证 | `Code/Tests/run-tests.sh Session` |
| 会话设置接口 | 接口与内存实现都在 `Services/Session/session.{h,cpp}`，只依赖 QtCore。用例覆盖「键不存在返回调用方给的回退值」「空键整条拒掉」「写入同一个值不算改动」「`clear()` 用空键承载全变」四条约定 | `Code/Tests/run-tests.sh Session` |
| 日志级别真的生效 | 不带参数启动**不产生任何日志输出**（默认 `warning`，启动横幅是 `info`）；`--log-level info` 打印带线程 id 的完整启动序列；`--log-level debgu`（拼错）打印「无法识别的日志级别「debgu」，改用 warning」 | `QT_QPA_PLATFORM=offscreen ./LqCompare [--log-level …]` |
| 分层检查 | 通过（Services 未反向依赖界面） | `python3 tools/check_layering.py` |
| 图标检查 | 通过（27 个图标，声明/引用/文件三者一致） | `python3 tools/check_icons.py` |
| 规格自检 | 通过（369 条，P0 59 条，PRD 与数据同步） | `python3 tools/check_spec.py` |
| Shell 可移植性 | 通过（1 个脚本，无 bash 4 内建与 GNU 工具扩展） | `python3 tools/check_shell.py` |
| Windows 宽字符 API | 通过（83 个源文件、清单内 43 个 API；自测 17 个样本） | `python3 tools/check_winapi.py [--self-test]` |
| 测试套件 | `554 passed / 0 failed`，且「无套件匹配」被视为失败（exit 2） | `Code/Tests/run-tests.sh` |
| 命令注册表自检 | 启动时 0 问题（说明不缺图标、不缺说明、无快捷键冲突） | 启动日志 |

**已知未验证**：Windows MinGW 32 位构建未在本机验证（无该环境）；
macOS 上只有 Qt 5.15.2 一套（用户机器上 6.11.0 已卸载）。

## 3. 当前代码结构（哪些文件已经存在）

```
Code/
├── LqCompare.pro                 构建入口，已预留各工作流的 include
├── App/
│   ├── main.cpp                  进程入口、日志、命令行、注册表自检
│   ├── RibbonWindow.{h,cpp}      Ribbon 外壳（样式 / QAT / 搜索栏 / 右键菜单）
│   ├── MainWindow.{h,cpp}        主窗口 + 31 条已实现命令 + 输出面板 + 状态栏
│   └── app.pri
├── Views/
│   ├── Session/                  comparesession（会话抽象基类：契约 / 状态机 / 三出口）
│   ├── Shell/                    homepage（Home 页）、sessionarea（会话标签容器）
│   ├── Page/                     ribbonlayout（声明表驱动的 Ribbon 构建）
│   ├── sessionview.pri / shell.pri / page.pri / views.pri
├── Services/
│   ├── Command/                  commandregistry（命令注册中心）
│   ├── Log/                      logging（分级日志 / 级别过滤 / 三目标 / 耗时辅助）
│   ├── Session/                  session（会话设置接口 + 内存实现）、
│   │                             sessiontype（会话类型描述子与注册表：14 种内置
│   │                             类型的字段 / ID 快照 / 按掩码的注册顺序优先）
│   ├── Filter/                   mask（掩码语法 / 匹配 / 语法速查表）、
│   │                             maskfilter（包含排除叠加 / 大小写策略 / 预览计数）
│   ├── Files/                    filesystem（抽象层 + 错误携带）、pathutils（路径与名称规则）、
│   │                             pathname（字节保真 / Unicode / 显示）、
│   │                             trash（回收站服务 + XDG 规则）、
│   │                             batch（失败清单 / 重试 / 进度）、
│   │                             filesystem_posix/_win、trash_mac.mm/_linux/_win
│   ├── Platform/                 iconkey（缓存键与尺寸）、iconcache（有界 LRU + 去重队列）、
│   │                             iconservice（同步/异步/回退 + 提供者接口）、
│   │                             registrystore（注册表抽象 + 内存实现 + 故障注入）、
│   │                             shellintegration（计划 / 安装回滚 / 卸载还原 / 校验 / 残留）、
│   │                             iconservice_mac.mm/_win/_linux、registrystore_win.cpp/_stub.cpp
│   ├── command.pri / log.pri / filter.pri / files.pri / platform.pri / services.pri
├── Pictures/                     27 个 SVG 图标 + Pictures.qrc
├── Tests/
│   ├── Support/                  fakefilesystem（内存文件系统）、faketrashservice
│   │                             （内存回收站），多套件共用
│   ├── CommandRegistry/          tst_commandregistry + .pro（14 用例）
│   ├── FileSystem/               tst_filesystem + .pro（50 用例）
│   ├── Filter/                   tst_filter + .pro（87 用例函数，刻意不链接 QtGui）
│   ├── Logging/                  tst_logging + .pro（32 用例函数，刻意不链接 QtGui）
│   ├── PathName/                 tst_pathname + .pro（40 用例 + 1 个仅 Linux）
│   ├── Trash/                    tst_trash + .pro（35 用例）
│   ├── Batch/                    tst_batch + .pro（36 用例）
│   ├── PlatformIcon/             tst_platformicon + .pro（48 用例）
│   ├── ShellIntegration/         tst_shellintegration + .pro（99 用例函数）
│   ├── Session/                  tst_session + probesession + .pro（46 用例函数，
│   │                             唯一链接 QtWidgets 的套件；INCLUDEPATH 只有
│   │                             Views/Session 与 Services/Session，兼作编译期护栏）
│   ├── SessionType/              tst_sessiontype + .pro（57 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui）；兼作两道源码级护栏：类型 ID
│   │                             与 Home 页硬编码的一致性、图标键必须在 qrc 里
│   └── run-tests.sh              统一测试运行器
└── ThirdParty/                   myclasspath.pri（定位 LqRibbon）、lqribbon.pri

tools/
├── spec/                         规格数据（唯一事实来源，8 个模块文件，369 条）
├── publish_issues.py             生成 PRD + issue 索引 + 创建 issue（幂等）
├── generate_icons.py             生成图标集
├── check_layering.py             ENG-001 依赖方向
├── check_icons.py                ENG-009 图标一致性
├── check_shell.py                ENG-003 脚本可移植性（bash 4 / GNU 扩展护栏）
├── check_winapi.py               PLAT-007 宽字符 API（禁止 ANSI 版与不带后缀的写法）
└── check_spec.py                 规格自检 + PRD 同步 + 优先级策略 + 文档计数

docs/
├── PRD-actions.md                规格正文（生成物，勿手改）
├── PRD.md                        产品定位与范围
├── design/architecture.md        架构与目录清单
├── development/                  交接、并行划分、GitHub 流程
├── github/                       issue 索引与发布记录（均为生成物）
└── research/                     两份竞品测绘 + 开源实现参考 + 裁决规则
```

## 3.1 基线提交与远端状态

基线导入已完成并推送到 `origin/main`（本地与远端一致，工作区干净）。五个提交按
「可编译的整体」划分，而不是按动作条目切碎——`Code/` 拆开任何一个提交都编不过：

| 提交 | 内容 | 关联条目 |
| --- | --- | --- |
| `451dbc2` | 工程骨架、四层结构、命令注册中心与 Ribbon 外壳 | PLAT-001、ENG-002/003/005/006/011、UI-007、UI-024 |
| `ae53e47` | 分层与图标两项构建期护栏 | ENG-001、ENG-009 |
| `d9e1b63` | 规格数据层与 issue 发布链路 | ENG-014 |
| `5034ae3` | 产品定位、架构、并行划分与竞品测绘 | DOC-003、DOC-005、DOC-006 |
| `6f33117` | issue 模板、状态标签工作流与 CI | ENG-004、ENG-015 |
| `a693346` | 参考项目克隆实测结果与受限网络下的取用方式 | DOC-006 |
| `e456c83` | 文件系统服务抽象层与可替换的假实现 | PLAT-002 |
| `de1ba13` | 回收站与可逆删除服务 | PLAT-003 |
| `ad7f004` | Unicode、特殊文件名与名称的字节保真 | PLAT-007 |
| `03a481c` | 错误携带（分类 + 原始系统码）与批量操作的失败处置 | PLAT-008 |
| `81a33a4` | 系统图标的缓存、去重与异步解析 | PLAT-004 |
| `3701144` | 推送流程里「租约永远过期」的成因与正确写法（纯文档） | ENG-003 |
| `01165d6` | Shell 集成的注册表计划、安装回滚与卸载残留校验 | PLAT-005 |
| `20602e6` | 补上 PLAT-005 的提交记录，并纠正「回填提交号」的做法（纯文档） | ENG-003 |
| `042f0f6` | 分级日志的级别过滤、结构化记录、接收者目标与耗时辅助 | ENG-006 |
| `9622527` | 掩码语法、过滤声明与语法速查表 | FILT-001 |
| `bab3f5b` | 会话抽象基类与统一的会话设置接口 | SESS-001 |

> 上面这张表里，PLAT-005、FILT-001 与 SESS-001 那三行由**单独的纯文档提交**补写，
> 原因见下一段——把一个提交的提交号写进它自己，会因为 `--amend` 每次都改变
> 提交号而永远对不上。

远端：369 个 issue 全部创建，标签为 `需求 / 待实现 / <模块> / <优先级>`，
其中 P0 59 条。反查入口是 `docs/github/prd-issues.json`。

**核对「本地与远端是否一致」时走 SSH**：`git fetch origin` 走的是 https，
在本机网络下偶发 `Error in the HTTP2 framing layer`（同一个仓库的 SSH 推送却正常）。
用下面这条更稳，且不依赖任何配置：

```bash
git ls-remote git@github.com:LorenHan/LqCompare.git refs/heads/main
```

**推送必须走 SSH。** `gh` 登录的 token 只有 `gist`、`read:org`、`repo` 三个 scope，
推 `.github/workflows/` 下的文件会被拒（`refusing to allow an OAuth App to create or
update workflow ... without workflow scope`）。走 SSH 不受这个限制：

```bash
git push git@github.com:LorenHan/LqCompare.git main
```

**配套的坑**：`origin` 配的是 **https** URL（见 `git remote -v`），而推送走的是
**SSH** URL，两者在 git 眼里是两个不同的远端。于是 `git push --force-with-lease`
会**永远**报 `stale info` 而被拒——它拿不到与推送目标 URL 对应的 remote-tracking
引用，无法核对租约，于是保守地拒绝。这不是「有人改过远端」，但报错信息看不出
这个区别，很容易被误读成「远端被别人推过」从而不敢继续。

需要强推（例如 amend 后要覆盖刚推上去的那个提交）时，把期望值显式写出来：

```bash
git fetch origin                                     # 先让本地知道远端在哪
git push --force-with-lease=main:<远端当前提交> \
    git@github.com:LorenHan/LqCompare.git main
```

**能不强推就不强推。** 这里有一条踩过的坑：**不要把某个提交的提交号写进这个提交自己**。
`git commit --amend` 每次都产生新的提交号，于是「回填 → amend → 提交号变了 → 再回填」
是个死循环，中途总会留下一个指不到任何东西的假提交号（PLAT-004 那行就一度是 `ffa093a`，
而它早已被 amend 掉了）。正确做法是**两步**：先把代码提交定稿，拿到提交号，
再用一个纯文档提交把这一行补上——本节的 PLAT-005 行就是这么来的。
未推送时本来就该这样做；已推送时先补写会需要强推，更不划算。

## 4. 下一步该做什么

> 本仓库有一个**每 2 小时运行一次的无人值守任务**，按本节与 §4.1 的指引推进 issue 队列，
> 每轮完整闭环 1～2 条（含测试、护栏、提交、推送、issue 更新与本目录下的工作日志）。
> 因此你会看到没有人在场的提交——它们不是手滑推上去的，判定标准与本文件写的完全一致。
> 想让它停下来，把那个自动化暂停即可；已经推上去的提交都是各自独立的闭环，可以单独回退。

| 对话 | 工作流 | 从哪条 issue 开始 | 交付什么 |
| --- | --- | --- | --- |
| **B 会话框架** | 继续（**优先——`SESS-001` 与 `SESS-002` 都已落地，接力棒在 `SESS-006`**） | `SESS-006`（会话设置对话框框架）→ 之后是 `SESS-007`（作用域语义）与界面接入 | 声明式的设置分组模型与脏状态判定仍在 `Services/Session/` 内，可无界面测试；对话框外壳属界面批次 |
| **H 过滤与格式** | 继续（同模块的自然延续） | `FILT-005`（过滤器的层级与作用域）——`FILT-001` 已落地，它直接复用 | 仍在 `Services/Filter/` 内，纯逻辑 |
| **A 平台底座** | 继续 | `PLAT-006`（单实例与进程间通信）——但只能做第 1、2、4 条，见 §4.1 | 仍在 `Services/Platform/` 内；`platform.pri` 已接好 QtGui 与注册表相关的 `LIBS` |

**为什么下一步是 `SESS-006` 而不是界面**：`SESS-001` 定下了「会话」这个概念
（契约、状态机、三个出口、设置接口），`SESS-002` 定下了「会话类型」的登记方式
（内置类型的字段、ID 稳定性、按掩码的注册顺序优先查询、创建工厂）。
**唯一还没有定义的是「一个会话类型有哪些设置项」**——具体会话类型（`TEXT-*` / `FOLD-*`）
要写设置项、`SESS-007` 的作用域下拉要落在对话框底部、界面上接 Rules 按钮要有东西可开，
都要先有这套声明。而声明本身是纯数据（标题、说明、控件类型、默认值、校验规则），
`SESS-006` 第 4 条还明确要求「任一会话设置 Tab 均可在**无界面**测试中被单独构造与读写」
——本机就能做完整闭环，与先动界面相比划算得多。

**`SESS-002` 留给下一轮的接口**：`SessionTypeRegistry::find(id)` 拿到条目之后再取
`factory`，就是「按类型 ID 造会话」的那一步（`Tests/Session` 的 G 组从注册表把会话
造出来并走完了整个生命周期，用的是合成类型）。要留意的是**内置的 14 种类型目前一个
工厂都没有**——`HomePage::sections()` 里那批硬编码 ID 现在与注册表逐字一致（有源码级
护栏钉住，见 §2），但点到它们还造不出会话。等第一个真正的会话类型落地时，那一行
`factory` 才有人填；在那之前「双击 Home 页卡片打开会话」在真实数据上仍走不通。

三个工作流的目录互不重叠，`services.pri` 的 include 已一次加齐（`exists()` 保护），
因此三方都不需要改共享文件。详见 [parallel-workstreams.md](parallel-workstreams.md) §1。

### 4.1 选下一步之前先看这一节：哪些条目被谁阻塞

**为什么单独写一节**：本项目的推进方式是「一次闭环一条 issue」，
而**被阻塞的条目照样能提交一堆漂亮的代码**——它们只是永远无法把完成标准勾上，
因为验收依赖的东西还不存在。等发现时已经写了几百行没人用的代码。
所以选条目前先在这里查一次。

| 条目 | 被谁阻塞 | 现在能做多少 |
| --- | --- | --- |
| ~~`SESS-001` 会话抽象基类~~ | **已落地**（提交见 §3.1，issue #36） | 四条完成标准**全部已勾**：第 2 条的「并注册」那一半在下一轮由 `Tests/Session` 的 G 组补上，理由见 §1.8。它现在**不再阻塞任何人** |
| ~~`SESS-002` 会话类型注册表~~ | **已落地**（提交见 §3.1，issue #37） | 四条完成标准全部已勾，见 §1.9。**它解除了三处的阻塞**：`SESS-003` 第 1 条、`SESS-005` 第 1 条，以及「在 `Views/` 里新增会话入口的条目」。但它**没有**解除 `PLAT-006` 第 3 条与 `CLI-001` 的执行部分——原因见下面那两行 |
| `SESS-006` 会话设置对话框框架 | **无**（它是下一把钥匙） | **可完整落地**。声明式的设置分组模型（标题、说明、控件类型、默认值、校验规则）、脏状态判定、无界面构造都是纯逻辑；对话框外壳在模型就绪后是薄薄一层。第 4 条要求「任一会话设置 Tab 均可在无界面测试中被单独构造与读写」——这一条只能靠服务层先做出来 |
| `PLAT-006` 第 3 条（收到参数后首个实例**创建会话**并把窗口置前） | 从「`SESS-002` 未落地」**降级为「缺第一个真正的会话类型」** | 机制已经就位（`SessionTypeRegistry::find(id)` 拿到条目后取 `factory`），但**内置的类型一个工厂都没有**，因此「按类型 ID 造会话」在真实数据上仍然造不出东西。可做的是「把已有窗口激活到前台」 |
| `SESS-003` Home 视图 | 部分解除：第 1 条不再被阻塞 | 第 1 条（按会话类型分组的「新建会话」入口卡片）现在**可做**——注册表能按分组枚举，且每条带显示名、英文名与一句话说明。第 2、3 条（最近会话 / 最近比较）等 `SESS-009`，第 4 条（会话树）等 `SESS-004` 与 `SESS-008`，第 5 条（关完会话回 Home）只依赖 `SessionArea` |
| `SESS-005` 新建会话向导 | 部分解除：第 1 条不再被阻塞 | 第 1 条（按「文本类 / 文件夹类 / 数据类 / 高级」分组列出全部可用类型）现在**可做**：四个分组枚举与「按分组枚举可用类型」正是为此准备的。第 2 条要等各类型的路径模型，第 3 条（剪贴板作数据源）要等 `SESS-012` |
| `SESS-007` 设置作用域语义 | 第 2、3 条涉及设置对话框底部的下拉，而那个下拉住在 `SESS-006` 的框架里 | 第 1、4 条可做：三层优先级（视图 > 会话 > 类型）与「视图 → 会话 → 类型 → 出厂默认」的覆盖链是纯逻辑，可无界面测试；第 2 条里的「本次修改将保存到 X」也可以是可测的**文本**，只有把它画进下拉要等界面 |
| `PLAT-006` 第 5 条（单实例行为可由选项关闭） | `OPT-002`（设置框架）未落地 | 可以做成显式接口 + 命令行开关；「选项界面里能关」做不了 |
| `PLAT-006` 第 1、2、4 条 | 无 | **可完整落地并测试**（跨平台单实例、参数转发与退出码、超时降级），因此这一条值得开，只勾这三条 |
| `PLAT-004` 第 1 条、`PLAT-005` 第 1 条 | 需要在真的 Windows 上编译 + 在资源管理器里看 | 只能等有 Windows 机器 |
| `PLAT-004` / `PLAT-005` / `ENG-006` / `FILT-001` 的界面接入 | `OPT-001` / `OPT-002`（设置页）未落地 | 服务层已就绪，界面接口留好即可（`ENG-006` 的接收者机制、`FILT-001` 的 `maskSyntaxReferenceText()` 与 `preview()` 都已备好） |
| 任何在 `Views/` 里新增真正会话界面的条目 | ~~`SESS-001`（会话基类）~~ **已解除** | 基类与类型注册表都已落地，会话界面现在可以真正开工；缺的只是各自具体的会话类型实现 |
| ~~`FILT-001` 掩码解析器~~ | **已落地**（提交见 §3.1，issue #228） | 已完成，其余 FILT 条目都复用它 |
| `FILT-005` 过滤器的层级与作用域 | 第 4 条（「视图临时过滤不写入会话」）要等 `SESS-001`；第 3 条里的**面板**要等设置页 | 前两条与第 5 条可做：三层叠加、每层可开关、合并后的表达式与匹配计数（做成数据 + 文本，面板留给界面批次） |
| `FILT-002` 名称过滤器（正则与超时保护） | **不是被别的模块阻塞，而是被 Qt 版本卡住**——见下面的专门说明 | 第 1、3、4 条可做（三种模式、组合语义、实时校验）；**第 2 条（200ms 超时）在 Qt 5.15 上需要绕道** |
| `FILT-003` 属性过滤 | 第 5 条（「扫描阶段早期生效」的性能断言）要等 `Folder/` 的扫描器 | 第 1~4 条可做：大小/时间/属性位的纯判定 |
| `CLI-001` 起的命令行条目 | `SESS-002` 已落地，但**内置类型都没有工厂** | 解析部分可做（`ShellIntegration::parseShellInvocation()` 已是例子）；`--list-session-types` 这类**列出**类型的子命令现在也可做（注册表可枚举）。「执行」（真造出会话）仍要等第一个真正的会话类型 |

**`FILT-002` 第 2 条的专门说明（省得下一轮白做半截）**：那条要求
「正则匹配有超时保护（默认 200ms/条），超时记录为错误条目并继续」。但
**Qt 5.15 的 `QRegularExpression` 没有匹配超时**——`setMatchTimeout()` /
`matchTimeout()` 是 Qt 6.0 才加进来的（已在本机 Qt 5.15.2 的头文件里核对过：
`qregularexpression.h` 里没有这两个成员）。所以在 Qt 5.15 上要满足这一条只有两条路：
一是把匹配放到工作线程上等一个截止时间（超时就放弃那一条并记错，线程本身无法真正
被杀掉，得让它自然结束），二是直接调 PCRE2 的 match limit。两条都不是小活。
**不要把这一条当成「顺手加个参数」**；开 FILT-002 时先决定走哪条路，再动手。

判断方法很简单：**看完成标准里有没有动词指向一个还不存在的模块**。
「创建会话」「在设置界面里」「显示在差异视图里」都指向别的工作流的产物；
「解析」「生成」「转发」「超时降级」「写注册表」都只依赖本模块。

**A 工作流的九件要紧事：**

1. `trash_linux.cpp`、`trash_win.cpp`、`filesystem_win.cpp`、`iconservice_win.cpp`、
   `registrystore_win.cpp` 需要在各自的平台上首次构建并修正。它们是当前唯一
   **从未被编译过**的代码，PLAT-002 / PLAT-003 / PLAT-004 / PLAT-005 / PLAT-007
   的各一条完成标准因此未勾选。拿到 Windows 机器时一次性过一遍——几份文件共用
   同一套手写常量 + `static_assert` 模式，错法也相似（先看
   `NSFileManagerUnmountBusyError` 写成 768 那件事就知道，这类错误编译期会直接报出来，
   不必等运行）。
   `iconservice_linux.cpp` 未在 Linux 上跑过，但它只依赖 QtGui 的 `QMimeDatabase`
   与 `QIcon::fromTheme`，风险低于 Windows 那几个。
2. **删除只有一条入口：`TrashService`**。`FileSystem::deleteToTrash` 已经移除，
   不要再加回来——理由写在 `filesystem.h` 的注释里（无状态的 `FileSystem` 留不住
   撤销点，用户点撤销会永远报「没有可还原的删除」）。
   调用方要自己持有 `TrashService` 的实例，生命周期要跨越「删除」与「撤销」两次操作。
3. 界面上接删除功能时，**先调 `availabilityFor()` 再决定是否动手**，不可用时用
   `decideTrash()` 拿到的 `reason` / `advice` 让用户选「取消 / 永久删除」。
   `TrashFallback` 的默认值是 `Cancel`——这个默认值会被当成「用户还没回答」时的行为，
   把它定成 `DeletePermanently` 会让一次界面卡顿变成一批文件的永久消失。
4. 界面上显示文件名时**统一走 `PathName::forDisplay()`**，不要直接把名字塞进控件。
   换行会撑破列表行、首尾空格完全看不见，用户会在「看起来一样」的两个名字里挑错。
   同时记住两条纪律：读回与写回一律用**原始名字**（转义只发生在显示层）；
   把文本编码回系统字节必须用 `PathName::toNativeBytes()`，
   **绝不能用 `QString::toUtf8()`**（它会把承载原始字节的未配对代理换成 `?`，
   而且从返回值上看不出发生过什么）。
5. **报错只用一条出口**：拿到系统错误码的地方用 `fromSystemError()` /
   `fromWindowsError()` / `fromCocoaError()`，**不要**用 `classify*()`——后者只回答
   「属于哪一类」，原始码会被丢掉，PLAT-008 第 5 条就落空了。
   显示时用 `errorReport(code, path)`（分类文案 + 原始码）配 `errorAdvice(category)`。
   反过来说，`ErrorCode` 与 `FileSystemError` 之间有双向隐式转换，
   所以 `*error = FileSystemError::None` 与 `error == FileSystemError::Busy`
   这类既有写法都仍然有效，不需要为了类型变化去改调用点。
6. **批量文件操作走 `BatchOperation`，不要自己写循环**。
   理由不是「少写几行」：长任务的重试必须知道「哪些条目已经成功」，
   自写的循环迟早会把这个状态算错，然后表现为「已经成功的文件被再做一遍」。
   默认策略是 `SkipAndContinue`（PLAT-008 第 4 条）；有序批次才用 `StopOnFirstError`。
   界面上给用户两条出路时对应的是：`retryFailed()`（重试失败项）与
   直接接受当前报告（跳过并继续）——**两条路都不要重跑整批**。
7. **界面取图标走 `IconService`，不要在视图里自己调 `QFileIconProvider`**。
   理由不是「统一风格」，而是三件会真实发生的事：一是按路径取图标没有缓存，
   滚动一个 5000 个文件的目录就是 5000 次系统调用；二是同步取会让界面
   卡在文件名上；三是取不到图标时视图要自己决定「显示什么」，
   于是每个用到图标的地方都会长出自己的一套回退逻辑。
   接线的正确姿势是：构造期给 `IconService` 一个 `setBaseSize()` / `setDevicePixelRatio()`，
   列表里调 `requestIcon(path)`，收到 `iconReady(cacheKey)` 后用**条目自己记住的 cacheKey**
   去 `iconForPath()`（命中即 O(1) 哈希查找）刷新那一行。
   **不要**把「哪个键对应哪些行」维护在服务里——那是第二份事实来源。
   另外记得 `IconService` 是从 `Platform::` 拿 `Files::PathUtils::Style` 的，
   与 `Files/` 的路径规则共用同一个事实来源，别在视图里自己拼扩展名。
8. **Shell 集成的三个入口要一起接，不要只接「安装」**。
   `installedState()`（现在是什么状态）、`previewInstall()`（这次会改什么）、
   `install()` / `uninstall()`（动手）——`previewInstall()` 用一个内存存储
   把「会覆盖你现有的 `.patch` 关联」这类结论在动手之前算出来，
   而 `uninstall()` 之后**必然**会跑一次残留检查并把结果写进同一个报告。
   三条容易踩的纪律：
   一是**不要**绕过 `buildShellIntegrationPlan()` 自己拼注册表路径，
   计划是唯一的事实来源，安装/卸载/校验/残留四件事都从它推导；
   二是安装要允许「重新配置」（用户改了选项再装一次），
   `install()` 的步骤 0 已经会先按**记录的**选项拆干净，
   否则旧选项留下的项卸载不掉，而报告会说「残留检查通过」；
   三是**非 Windows 平台不要试图给一个「能用的替代实现」**——
   `capability().available` 为假时照 `advice` 给用户可执行的替代路径就行，
   把「写不进去」做成「假成功」比直接说不行糟得多。
9. **输出面板接日志要走一次排队跳转**，不要直接把面板挂成日志接收者。
   接收者在**记录日志的那个线程**上被调用（`Tests/Logging` 里有一条跨线程用例
   把这个事实钉住），而图标解析跑在 `IconService` 的后台线程上——
   从那里碰控件会崩，且崩的位置与「我只是接了个日志」看起来毫无关系。
   正确姿势：接收者只 `emit` 一个信号，用 `Qt::QueuedConnection` 连到面板的槽。
   另外三个容易踩的点：`addSink()` 传空函数对象会返回 0（无效句柄），
   面板析构时要按句柄 `removeSink()`（`clearSinks()` 会把别人的接收者一起清掉，
   例如诊断包导出）；清理顺序是**先移除接收者再关日志文件**，
   反过来的话关文件时若还挂着接收者，某些实现会顺手写一条「日志文件已关闭」，
   而这条落到了下一个使用者头上。

三个并行对话不是硬性数量，也可以只开两个（A + B），或把 B 换成 **O 工程与文档**
（`ENG-002` 模块构建守卫、`DOC-001` 用户手册）。**H 建议早做**：掩码解析器是纯算法，
输入输出都是字符串，不需要任何平台能力就能写出完整测试，是性价比最高的一块。

## 5. 接手时必须遵守的约定

1. **先读 issue，再写代码**。issue 正文里的「入口、作用对象与行为边界」「完成标准」
   就是验收条件；不要按自己的理解扩大范围。
2. **不要改 `docs/PRD-actions.md`**。它是生成物，改 `tools/spec/` 里的数据后重新生成。
3. **不要手改各顶层 `.pri` 与 `LqCompare.pro`**。共享 include 已一次加齐（`exists()` 保护），
   新建模块只需要新建目录与自己的 `.pri`。
4. **提交消息必须带 ACTION-ID 与 issue 号**，格式见 parallel-workstreams.md §3。
5. **破坏性操作必须可逆**：删除走回收站、覆盖先备份、批量前预演。
6. **每个条目配测试**，且测试要能在 `-platform offscreen` 下跑。
7. **静态检查脚本必须能自证会报错**。加一个 `--self-test`，用「该报的」与
   「不该报的」两类样本各跑一遍。一个从不报错的护栏比没有护栏更糟——它会让人
   以为这块已经被守住了。`check_winapi.py --self-test` 是现成的例子。
8. **写平台代码时先问「这段逻辑能不能抽成纯函数」**。能抽就抽：Windows 的路径
   规则、Linux 的回收站规则、UTF-8 的字节校验都因此能在开发机上被真实执行。
   抽不出来的只剩真正的系统调用，那部分如实标注「未在目标平台验证」，
   并在 issue 上写清楚——**不要**把「写了」当成「做完了」。

## 6. 本仓库的「坑」记录（新增坑请追加到本节）

| 坑 | 现象 | 处理 |
| --- | --- | --- |
| 日志宏形参命名为 `level` | 宏体里的 `LqCompare::Log::level()` 被一起替换掉，编译报「called object type … is not a function」 | 形参改用 `lvl` |
| `Q_OBJECT` 在头文件里却又写 `#include "xxx.moc"` | `No rule to make target 'xxx.moc'` | 去掉 `.moc` include；确保有 `QTEST_MAIN`，否则链接报 `undefined _main` |
| 测试工程写 `QT -= gui` | `QKeySequence` 头文件找不到（它属于 QtGui） | 保留 `QT += gui` |
| 图标校验只认 `:/Pictures/x.svg` | 用 `icon("x.svg")` 辅助函数的地方被误判为未引用 | 校验同时认裸文件名 |
| Qt 5.15.2 在 macOS 26 SDK 上 | qmake 报 SDK 版本不支持的警告 | 只是警告；本项目已实测可编译运行 |
| 优先级有两个事实来源 | 规格条目自带 `prio` 字段（恒为 P1），策略却写在 `publish_issues.P0_RANGES`；索引读前者，会把 59 条 P0 全标成 P1，且不报任何错 | 删掉条目里的 `prio` 字段，优先级只由策略推导；`check_spec.py` 新增护栏核对策略前缀 |
| 单跑 `publish_issues.py prd` | 用空映射覆盖 `prd-issues.json`，把「哪条规格对应哪个 issue」这个唯一反查入口清空（issue 还在远端躺着，但本地查不到了） | `prd` 与 `--dry-run` 一律沿用已有映射（`load_existing_mapping`） |
| GitHub 二级限流（内容创建） | 403 `secondary rate limit`；脚本按 2/4/8/16/32 秒退避，5 次重试合计 62 秒必然再次撞墙，首轮白跑并丢掉 16 条 | 对二级限流单独等待 ≥60 秒；脚本幂等，直接重跑即补齐 |
| `gh` 的 token 缺 `workflow` scope | 推送 `.github/workflows/` 下的文件被拒（`refusing to allow an OAuth App to create or update workflow`） | **改走 SSH**：`git push git@github.com:LorenHan/LqCompare.git main` |
| 手写文档里的条目数会过期 | 加两条规格后，README/CHANGELOG/PRD/架构/交接 5 处仍写着旧的 367；没有任何机制会主动发现 | `check_spec.py` 新增 `check_doc_counts`，扫描手写 markdown 里的条目数并与实际比对 |
| 脚本用了 `mapfile` | macOS 自带 bash 3.2 没有这个内建，测试运行器直接崩（`mapfile: command not found`） | 改成可移植的 `while read` 循环；`run-tests.sh` 头部写明「不得用 bash 4 语法」 |
| 脚本用 GNU sed 的 `\+` | BSD sed 不支持，解析**静默失败**：每行显示「14 passed」而合计是 0，看起来还挺正常 | 改用 `[0-9][0-9]*`；这是最危险的一类——不报错，只给错数字 |
| 测试过滤器无匹配时仍报「全部套件通过」 | 套件改名或过滤器拼错 → CI 绿，但 0 个用例执行 | 无匹配视为失败并 `exit 2` |
| CI 只跑 Linux，却声称支持三平台 | bash 4 内建与 GNU 扩展在 Ubuntu 上全绿、到 macOS 崩，CI 发现不了 | 新增 `tools/check_shell.py` 静态护栏并接入 CI；`build.yml` 里写明这个覆盖盲区 |
| macOS 与 Linux 的 `stat` 时间字段名不同 | macOS 是 `st_mtimespec` / `st_atimespec`，Linux 是 `st_mtim` / `st_atim`。写 `st_mtim` 在 macOS 上编译失败 | 在 `filesystem_posix.cpp` 里用条件编译分别取名；**不要**用自定义映射抹平，那会变成「看起来一样、实际只有一边被测到」 |
| `AT_FDCWD` / `AT_SYMLINK_NOFOLLOW` 未声明 | 忘了 `#include <fcntl.h>`，只引 `<sys/stat.h>` 不够 | 补 `<fcntl.h>`；用 `utimensat` 时它和 `<sys/stat.h>` 都要引 |
| `QFile::decodeName` 没有 `(const char*, int)` 重载 | 从 `readlink` 的缓冲解码时报「no matching function」 | 先构造 `QByteArray(ptr, len)` 再传。**必须按实际长度截断**——`readlink` 不写结尾 `'\0'`，直接解整个缓冲会把未初始化内容读进来 |
| 把 `C:` 当成根目录 | `isRootPath("C:")` 写成期望 true，被用例纠正：`C:` 是「C 盘当前目录」，属**驱动器相对**路径；根是 `C:\` | 判根时必须要求盘符后跟分隔符。当成根会让「向上递归到根」提前停下，操作作用到完全不同的位置 |
| 接口把写操作标 `const`，实现却没标 | 派生类的非 const 方法与基类 const 声明不匹配，报「does not override」 | `setTimes` / `setAttributes` / `deleteToTrash` 在基类与所有实现里统一标 `const`——它们改的是**文件系统**这个外部状态，不是对象自身 |
| 测试替身要在 `const` 方法里改数据 | `const` 方法中 `QHash::find` 返回 const_iterator，赋值编译失败 | 把容器标为 `mutable`（与调用日志同理），并提供单个返回 `FileInfo*` 的 `findEntry() const` |
| 手写 Cocoa 错误码 `NSFileManagerUnmountBusyError` | 写成 768（从「Unmount 在 Busy 之前的直觉」排下来），真实值是 **769**；768 是 `NSFileManagerUnmountUnknownError`，含义完全不同 | `trash_mac.mm` 里的 `static_assert` 在编译期直接报出来。这是「把平台常量放在平台无关层」必须配套的补偿，与 Win32 那套做法一致 |
| 在 `FileSystem` 上留 `deleteToTrash` 便捷转发 | `FileSystem` 无状态，「撤销最近一次删除」的撤销点只能每次调用现场重建 → 撤销永远报「没有可还原的删除」。删除本身成功，只有撤销不工作，很难查 | 删掉接口，删除只在 `TrashService` 上。**不要**再加回来 |
| Windows 上 `FOF_ALLOWUNDO` 在网络盘被静默忽略 | 无回收站的位置上 `SHFileOperation` 会直接永久删除，**并且返回成功**——「删除成功」四个字背后是一次不可逆操作 | 可用性探测是唯一的安全闸门：先按驱动类型（`DRIVE_REMOTE` 等）判，再 `SHQueryRecycleBinW` 确认。绝不能靠失败反推 |
| 回收站搬移与写元数据的顺序 | 先写 `.trashinfo` 而搬移失败 → 回收站里留下指向不存在条目的幽灵记录；搬移成功而写元数据失败 → 文件在回收站里但还原不回去，用户以为删成功了 | 先 `rename` 再写 info；写失败必须把 rename 回滚 |
| 撤销时按原名拼回收站路径 | 回收站里已有同名条目时，macOS 与 XDG 都会自动改名（`a.txt` → `a 2.txt` / `a.txt.2`），拼出来的路径根本不存在 | 必须用实现返回的 `TrashedRecord.trashedPath`。测试替身刻意模拟了改名，让「按原名拼」的写法当场失败 |
| 撤销成功后没有清空撤销点 | 第二次点撤销会把已经回到原处的文件再挪进回收站，而用户以为自己只是在「取消上一次撤销」 | 全部还原成功后清空撤销点；部分失败时保留，让用户修掉障碍后重试 |
| 测试替身的可用性注入按精确路径匹配 | 注入 `/mnt/net` 后查询 `/mnt/net/a.txt` 查不到，静默落到默认答案「可用」→ 那些「不可用时不删」的测试其实在测「可用时删除」，且仍然是绿的 | 按祖先逐级查找（真实世界回收站可用性是按卷的）；`lookupByAncestor()` 里写明了这个失效方式 |
| XDG `.trashinfo` 的两处格式细节 | `Path` 必须百分号编码（含空格与中文的名字不编码会被解析方截断）；分隔符 `/` 要保留；`DeletionDate` 是**本地时间且不带时区后缀**（写成 UTC 或带 `Z` 部分实现解析失败） | 抽成 `xdgTrashInfoContents()` / `parseXdgTrashInfoPath()` 纯函数，在 macOS 上就往返测过了 |
| `shellapi.h` 必须在 `windows.h` 之后 | 反过来会因缺少基础类型而编译失败 | `trash_win.cpp` 里已按顺序 include 并注明 |
| 真实往返测试的清理守卫 | 断言失败会直接 `return`，后面的清理代码不执行 → 用户的废纸篓里留下测试造出来的垃圾文件。而**失败恰恰最容易发生** | 用 RAII 守卫（`TrashCleanupGuard` / `TrashFileRemover`）而不是顺序清理代码；守卫在两次危险操作**之间**声明，覆盖所有失败路径 |
| `QString::toUtf8()` 会把未配对代理换成 `?` | 承载原始字节的私存码位（U+DC80..DCFF）经 `toUtf8()` 变成 `3f`，原始字节**无声丢失**——返回值看起来完全正常，只是一段合法的 UTF-8 | 编码回字节一律用 `PathName::toNativeBytes()`。有一条专门的用例（`toUtf8WouldLoseRawBytes`）把这个差异钉死，有人换回 `toUtf8()` 会立刻失败 |
| `QString::fromUtf8` 无法区分「无效字节」与「真的是 U+FFFD」 | 它把无效字节替换成 U+FFFD，而 U+FFFD 本身是合法字符（`EF BF BD`）。于是「原文里有一个 U+FFFD」与「这里有个坏字节」从结果上完全一样，逆向编码时却要走不同分支 | UTF-8 校验必须自己写（`decodeUtf8Sequence`），按码位范围判定。几十行，换来判断能力 |
| UTF-8 **过长编码**不判会凭空产生 NUL | `C0 80` 是 U+0000 的过长编码。放行它会让解码结果里出现一个 NUL，而 NUL 在路径里是终止符语义——系统调用在那里截断 | 校验里必须有「码位 < 该长度的下界即拒绝」这一条；同理要拒绝代理区码位（与私存方案会撞车）与超过 U+10FFFF 的值 |
| `QString::normalized()` 保留未配对代理 | 这是**实测结论**（Qt 5.15.2：`DC80 0061 DCFF` 规范化后一个都不变），整套字节保真方案依赖它。属外部依赖，Qt 改了行为会导致原始字节静默变成 U+FFFD | 用 `normalizationKeepsRawBytes` 用例钉住，同时把实测过程写进注释——将来 Qt 升级后这条会失败，提醒重新评估 |
| 转义前缀被 `toUpper()` 一起大写 | 先 `.arg(...).toUpper()` 得到 `\XFF`。`\X` 不是任何语言认的转义写法，用户看到只会觉得这个界面输出的东西不能直接用 | 只大写十六进制部分：`QStringLiteral("\\x") + QString::number(v, 16).rightJustified(2, '0').toUpper()` |
| 静态护栏从不报错也没人发现 | 一个永远 `exit 0` 的检查脚本会让人以为这块已经被守住了，比没有护栏更糟 | 凡是静态检查脚本都要能自证会报错：`check_winapi.py --self-test` 用 17 个样本（含「注释里提到 ANSI API」这类**不该**报的）验证两边都对 |
| 护栏不认条件编译 | `filesystem.cpp` 在 `#ifdef Q_OS_WIN` 块里包含 `<windows.h>` 用 `static_assert` 核对手写常量——那正是刻意设计的护栏，却被「非 Windows 文件不得包含 Windows 头」这条判成违规 | 护栏里维护预处理条件栈，只对**不在 `Q_OS_WIN` 块里**的包含报错 |
| `SHFileOperationW` 的返回值不是 `GetLastError()` | 它返回 Shell 的 `DE_*` 系列（如 `0x7C` = `DE_INVALIDFILES`），拿它去查 Win32 错误码表会查出含义完全不同的东西 | 仍然把它当原始码留下来（能搜到 `DE_INVALIDFILES`），但在注释里写明来源；`rawErrorName()` 对 `DE_*` 返回 `nullptr`，界面显示成数字而不是编一个假名字 |
| `QFile::open` / `write` 失败后 `errno` 不可靠 | Qt 内部会调若干系统调用，失败时**不一定**把 `errno` 设成有意义的值，读到的是上一次调用残留的。一个「磁盘满」会被报成「没有权限」——比不给原始码更误导 | 进 `QFile` 之前先 `errno = 0`，之后只在 `errno != 0` 时才 `fromSystemError(errno)`，否则报一个明确不带原始码的 `Unknown`。PLAT-008 要的是「原始码**确实是这次失败的原因**」 |
| 两次失败分类相同就以为原因相同 | `EPERM`(1) 与 `EACCES`(13) 都归 `PermissionDenied`，但前者常是不可变标志或安全模块拦截、后者才是 `chmod` 能解决的。只留分类会让用户按错误的建议去改权限 | 出参改成 `ErrorCode`（分类 + 域 + 原始值），并在测试里专门用一条用例断言这两个码仍然可区分 |
| `QTest::toString` 的自定义版本必须写 `template <>` | 写成普通重载时，`QCOMPARE` 内部用的是 `toString<T>(x)` 这种带显式模板实参的调用，普通重载**不参与**重载决议 → 拿不到值，失败信息退回「Compared values are not the same」。而那个重载本身还能编译，看不出任何异常 | 一律写成 `template <> char *toString(const T &)`；每个套件里都有现成例子 |
| 派生类的重写声明不会继承基类的默认参数 | 默认实参只写在基类声明上（`ErrorCode *error = nullptr`），派生类重写时不再重复。于是拿**派生类的静态类型**调用 `fakeFileSystem.stat(path)` 会报「too few arguments」，而通过 `const FileSystem&` 调用却正常 | 测试里加一层接受基类引用的辅助函数（如 `isReadOnly(const FileSystem&, ...)`）——它顺带让同一段断言对真实实现与替身都适用 |
| 用「重试后全部成功」验证「只重试失败项」 | 一个「整批重跑」的实现同样会全部成功，断言照样通过——而这正是 PLAT-008 第 2 条要防的（对批量复制就是覆盖用户刚确认过的结果） | 断言必须落在**调用次数**上：`callsFor(成功路径) == 1`、`callsFor(失败路径) == 2`。替身记录调用日志就是为了这个 |
| 同一件事的两种错误文案实现 | `filesystem_win.cpp` 里原本自己拼 `"Win32 错误码 %1（%2）"`，与新的 `errorDetail()` 是同一件事的两份实现；两份必然演化成界面上同时出现「Win32 错误码 32（busy）」与「Win32 32（ERROR_SHARING_VIOLATION）」，用户以为是两个不同故障 | 删掉自拼的那份（它当时还没有任何调用点），统一用 `errorDetail(fromWindowsError(code))` |
| 给认不出的错误码编一个「名字」 | 拼出 `UNKNOWN_1234` 这类字符串会让用户拿一个根本不存在的符号去搜，比只看到数字更糟；同理「原始错误码：（无）」也只是噪声 | `rawErrorName()` 认不出就返回 `nullptr`；`errorDetail()` 在没有原始码时返回**空串**，由界面决定不显示这一段 |
| `QImage(uchar*, w, h, bpr, fmt)` 是**浅引用** | 它不接管那块内存。用完就 `release` 源缓冲（如 `NSBitmapImageRep`）之后，QImage 指向已释放的内存——表现为「图标偶尔是花的」，只在内存被复用时出现，且几乎不可复现 | 立刻 `.copy()` 一份再释放源；`iconservice_mac.mm` 里写明了这一条。**任何**用这个构造函数包外部缓冲的地方都要这样处理 |
| `NSBitmapImageRep` 的像素格式要配 `Format_RGBA8888_Premultiplied` | 配成 `Format_ARGB32_Premultiplied`（直觉上「更 Qt」）会在小端机器上把通道读反，图标变成「蓝脸」。而灰度图标上完全看不出来 | 用 `RGBA8888` 与 NSBitmapImageRep 的内存布局逐字节对应。这类错误要靠**彩色**测试样本才能发现 |
| 后台线程里用 AppKit 不开 `@autoreleasepool` | 图标解析跑在 `QThreadPool` 的线程上，Qt 不会为它建池。`NSWorkspace` 返回的自动释放对象一直不释放，控制台有抱怨，表现为「滚动大目录时内存一直涨」 | 解析函数体整体包在 `@autoreleasepool { }` 里 |
| `UTType` 要 macOS 11+，部署目标却是 10.13 | 编译器为函数体内每个 `UTType` 各报一条 `-Wunguarded-availability-new`（本次 7 条）。`@available` 检查明明写在调用点，编译器却看不到——因为用的是**独立函数**，警告报在函数体内 | 给该函数加 `API_AVAILABLE(macos(11.0))`，把契约写进签名：漏检查时报错落在**调用行**。**不要**用 `#pragma clang diagnostic ignored` 压掉——那会连同「调用点漏检查」一起静音，而后果是用户在 10.15 上点一下列表就崩 |
| `SHGetFileInfo` 默认会去**访问磁盘** | 对没有对应文件的名字（枚举一个还没访问过的目录）它会去实际查找，网络盘上一次卡几百毫秒、U 盘没插时直接失败。而这里问的只是「`.cpp` 该长什么样」 | 加 `SHGFI_USEFILEATTRIBUTES` 只按名字问；Linux 侧同理用 `QMimeDatabase::MatchExtension` 而不是 `MatchDefault` |
| `SHGetFileInfo` 只能给 16/32/48/256 几档 | 请求 20 会拿到 32。把「接近的尺寸」当成「就是这个尺寸」写进结果，调用方会以为拿到了精确尺寸，拼高 DPI 图集时用错比例 | 如实报出 `actualPixelSize`；需要精确尺寸的地方自己再缩一次。Windows 侧 48/256 还需要 `IImageList` COM，本次没做，代码里已注明 |
| 用 `QCache` 当图标缓存 | 它的淘汰策略**没有对外契约**（文档只说「某条策略」），于是「缓存上限 256 个类型」这个约束无法写测试，也无法预测谁被淘汰 | 自己写有界 LRU，带 `keysByRecency()` 与 `Stats`，淘汰行为可断言。图标缓存总量本来就不大（上界是扩展名数），几十行的代价换来确定性 |
| 图标缓存键只写扩展名 | `notes.txt` 这个**目录**会拿到文本文件图标——因为「是否目录」没进键。这类错误只在「目录名带扩展名」时出现，测试里如果只用 `foo.txt` 这种文件样本，永远发现不了 | 键形如 `f|txt` / `d|<dir>`，把两个事实都编进去；并专门写一条「目录与同名扩展名的文件拿到不同格子」的用例 |
| 缓存命中时也走异步发信号 | 命中率高的目录（全是 `.cpp`）里绝大多数条目本来能同步拿到图，一律异步会让滚动时可见闪烁：先放占位、几十毫秒后换真图 | 命中就同步返回并同步发信号；只有未命中才排后台 |
| 服务里维护「哪些路径引用了这个键」 | 一次解析产出一个**类型**的图标，可能 200 行在等它。按路径发信号就要维护这份映射，即第二份事实来源，且随视图增删要同步改——改漏了就是「部分行永远不刷新」 | 信号只带 `cacheKey`，视图按需重查。列表项自己记住 cacheKey，刷新是 O(1) 哈希查找 |
| 图标请求不去重 | 一次解析的代价与「多少文件引用这个类型」无关，却与「多少文件」成正比。滚动一个全 `.cpp` 的目录就是几百次系统调用换回同一张图 | `IconRequestQueue` 按缓存键去重，把在途请求数从文件数压到扩展名数 |
| 让缓存层直接吃 `QIcon` | 缓存淘汰顺序、去重、命中统计是最容易写错的部分，而一旦它们依赖 `QIcon`，测试就得有一个能跑图形栈的环境 | `IconEntry::payload` 用不透明的 `QVariant`：生产放 `QIcon`，测试放 `QString`。于是这批逻辑能在只链接 QtCore 的套件里完整覆盖。代价是 `usable()` 只回答「有没有」，所以真实图标源另有三条走真机的用例 |
| 图标解析与其他后台工作共用线程池 | 一个耗时任务（枚举大目录、读压缩包）会把所有图标请求排到它后面，界面上表现为「整个列表都不出图标」——用户会以为是图标功能坏了，而不是「有个任务在跑」 | `IconService` 用**专属**的单线程 `QThreadPool`；池容量设 1 也顺便免除「提供者实现各自考虑并发」的负担 |
| 对真实系统图标源做「一定不是回退图标」的断言 | macOS 对未知扩展名（`.zzzznope`）会给**通用文档图标**，来源是 `System` 而不是 `Builtin`——与 Finder 的行为一致，这是正确行为。断言 `hasFallback() == true` 会失败，而看起来像代码有 bug | 按来源分支断言：`System` 时断言 `usable()` 且 `actualPixelSize > 0`；`Builtin` 时才断言 `hasFallback()`。**教训**：对「外部系统会怎么回答」的断言，先确认外部系统的真实行为，不要按自己的直觉写期望值 |
| `check_spec.py` 的文档计数护栏会误报「N 个条目」这种口语 | 我在架构文档里用「几千文件的目录会占几千格」举例时，最初写成了「N 个条目」的形状（数字紧跟「个条目」），护栏直接报错；改完这行**引用它的坑表本身**又踩了第二次——本轮写 FILT-001 的分组说明时第三次踩到，因为「三组（N 条）」与「（N 条，每条带样本）」都是同样的形状 | 该护栏是刻意宽进严出的（宁可误报也不漏报过期数字）。**它的模式不止「个条目」与「条规格」，写文档前先看一眼 `check_spec.py` 的 `patterns`**：数字紧跟「个条目」、数字紧跟「条规格」、以及两种括号形状（数字 + 条 + 右括号、数字 + 条 + 逗号）；最后还有一条针对表格里的「合计」行。所以数量一律写成「33 个用例」「这 21 行」这种形状，**不要在括号里用「数字 + 条」**。反过来也不要把这些模式改窄：它们正是靠宽匹配才抓到了 5 处真正的过期数字 |
| `git push --force-with-lease` 在本仓永远报 `stale info` | `origin` 是 https URL，推送走的是 SSH URL，两者是不同远端；git 找不到对应的 remote-tracking 引用，无法核对租约，于是保守拒绝。**报错信息读起来像「远端被别人推过」**，会被误判成协作冲突而不敢继续 | 显式写期望值：`git push --force-with-lease=main:<远端当前提交> git@github.com:LorenHan/LqCompare.git main`，前置一次 `git fetch origin`。更根本的做法是能不强推就不强推——先回填提交号再推 |
| 32 位进程写注册表却不加 `KEY_WOW64_64KEY` | 交付目标是 32 位 MinGW 构建。默认视图下写入会落到 `WOW6432Node` 影子副本，而 64 位资源管理器**看不见**那里。现象是安装、校验、卸载全都报成功，只有右键菜单「没有变化」——用户唯一能看到的证据就是什么都没发生 | 全部注册表访问固定带 `KEY_WOW64_64KEY`。判定依据不是「我们的程序多宽」，而是「谁要读它」——读它的是 64 位资源管理器 |
| 用「某个值一直写不进去」的注入测回滚 | `failOnValue()` 把该值的**删除**也一起挡住，于是回滚必然也失败，用例只留下一句「回滚未完全成功」。而「回滚逻辑写错了」与「注册表真的删不掉」从结果上完全分不开，这个用例等于什么都没测 | 另加一次性注入 `failNextWriteOnValue()`，只挡这一次写入、不影响删除。真实世界的写入失败大多是瞬时的（被杀毒软件短暂锁住），这才是想模拟的那一类。断言才能落在 `rollbackClean` 上 |
| 失败记录只打印「错误分类」 | 报告写成「失败 ……：找不到 该路径」，用户看不出找不到的是**哪一项**、期望它是什么值。而「缺失」与「值不对」的处置不同（一个要补、一个要改），全部区分信息都在 `detail` 里，却被丢掉了 | `ShellChangeRecord::describe()` 失败分支必须带上 `detail`。**凡是「成功/失败」两类共用一条格式化路径的地方，都要检查失败那一路有没有把诊断信息丢掉** |
| 登记子树里的下标键不在静态计划里 | `Backup\1`、`Backup\2` 是运行时生成的，卸载时按静态计划「键下有别人的内容就保留」会把这些备份键误判成**外来内容**从而拒绝删除 —— 卸载报「残留」，而用户什么都没做错 | 登记子树是唯一允许递归删除的地方（`removeKey` 整棵删）。回滚与卸载共用同一份 `removeInstallation()`，两处都写明了这个例外 |
| 「重新配置」被实现成「再叠一层」 | 用户取消勾选某项后再点安装，旧选项留下的注册表项仍在，而卸载的依据（登记里的选项）已经变成新选项——那些项永不被删，**报告却说「残留检查通过」**。这类错误只在「改过选项」时才出现 | 安装的步骤 0：已安装则先按**记录的**选项 `removeInstallation()` 拆掉再装；拆不干净就中止。凡是「安装记录 + 当前配置」两处状态的系统，都要先想清楚改配置时旧记录谁来负责 |
| `QString::SkipEmptyParts` | Qt 5.15 起弃用（`-Wdeprecated-declarations`），Qt 6 里被删。它在**只在 Windows 编译**的文件里不会在本机报出来，于是主程序 0 warning 而 Windows 构建会多 4 条 | 一律写 `Qt::SkipEmptyParts`（行为相同，两个大版本都不报警告）。**没被编译过的平台文件也要跟着改**，否则「0 warning」这个结论只对本机成立 |
| 用 `key.contains("\\shell\\")` 判「这是不是右键菜单项」 | 文件关联侧的命令键（`Software\Classes\LqCompare.PatchFile\shell\open\command`）同样含 `\shell\`，但它挂在 **ProgID** 下，不是加在右键菜单里的项。用它判会导致「关掉右键菜单」这条断言必然失败，而失败原因与它想验证的事实无关 | 按**挂在哪个类键下**判：菜单项挂在 `*` / `Directory` / `Directory\Background` 三个目标类下，关联挂在 ProgID 下。测试里把 `menuPrefix(target)` 提成辅助函数，让「菜单项」这个概念只有一个定义 |
| 用动词键去查命令条目 | 菜单文字挂在 `...\shell\LqCompare.compare`，命令挂在它**下面一层**的 `...\shell\command`。用前者调 `entriesFor()` 一条命令都取不到，断言 `foundCommand` 永远为假 | 命令条目要按 `verbKey + "\\shell\\command"` 查。测试里配了 `commandKey()` 辅助函数，避免第二次写错 |
| 「值名/键名大小写不敏感」被顺手做成了「存储也转小写」 | 归一后直接拿去创建键，会把 `LqCompare.DiffFile` 写成 `lqcompare.difffile`。功能上没问题，但用 regedit 打开时看起来像随手敲的乱码，下一个人会以为这是 bug 而去「修」它 | **归一仅用于比较，存储保留原拼法**。真实实现与内存实现都按这条写，且各有一条断言拼法的用例 |
| 认不出的注册表值类型被当成「没有值」 | 只记「有个值」而不记类型与字节，备份就等于记成「本来没有值」；卸载时会把用户原本那个我们看不懂的值**删掉**——这是不可逆的数据丢失，而报告会说「已还原」 | `RegistryValueKind::Unsupported` 把原始类型码与字节一起带上，`operator==` 逐字节比较；用例断言往返后字节完全一致。**凡是「读旧值 → 覆盖 → 还原」的流程，都要先问「我看不懂的旧值会怎样」** |
| `--log-level` 原先**只对宏生效、对直接调用无效** | `logging.h` 的 `LQCOMPARE_*` 宏会先比级别再调 `write()`，但 `Log::write()` 自己完全不过滤。`main.cpp` 里有 5 处直接调 `Log::write(...)`（含启动横幅），于是 `--log-level error` 下它们照样打印，日志文件也没法靠调级别瘦身——与 `logging.h` 写的「低于该级别的日志被丢弃」相反 | 过滤收进 `isEnabled()`，宏、`write()`、`Stopwatch` 三处共用它；`main.cpp` 的 5 处改成宏。**注意副作用**：默认级别是 `warning` 而启动横幅是 `info`，所以不带参数启动时日志里不再有启动横幅。想改回去要调**默认级别**（OPT-010），不是让 `write()` 不过滤 |
| 级别过滤写在多处 | 宏、`write()`、`Stopwatch` 各自写一遍 `<=` 比较的话，把方向或级别顺序弄反只会发生在其中一处，而现象是「某个级别偶发不输出」——这种不一致最难查 | 比较只有 `isEnabled()` 一份实现。加新级别时也只改一处 |
| 日志接收者在记录日志的那个线程上被调用 | 界面输出面板若直接挂成接收者，图标解析（`IconService` 的后台线程）记一条日志时就会从非 GUI 线程碰控件——崩溃位置与「我只是接了个日志」看起来毫无关系 | 面板必须自己加一次排队跳转（接收者只 `emit` 信号，用 `Qt::QueuedConnection` 连槽）。`Tests/Logging` 里有一条跨线程用例把这个事实钉住 |
| 在持锁期间调用日志接收者 | 接收者里顺手记一条调试日志（很自然的写法）会**死锁**，现象是「界面卡住」，与日志模块毫不相干；接收者在回调里增删接收者还会让容器在遍历中变动 | 进临界区前把接收者清单**拷贝**一份，出临界区再调用。代价是并发移除时那个接收者可能还收到这一条——比死锁好得多 |
| 日志接收者的句柄用函数对象当键 | lambda 之间没有可靠的相等比较，按值 `remove` 会**静默失效**——接收者以为自己被摘掉了，其实还在收 | `addSink()` 返回整数句柄，`removeSink(handle)` 按句柄移除。传空函数对象时返回 **0**（无效句柄）：空 `std::function` 不是「什么都不做的接收者」，而是调用时崩溃 |
| `setLogFile()` 会创建一个空文件 | 它要探一次可写性（以追加方式打开再关掉），所以「只配了路径、还没写日志」时文件已经存在。用例若断言「文件不存在」会失败，而失败原因是自己理解错了契约 | 断言应该是「关掉输出之后**不再写入**」（文件大小仍为 0），而不是「文件被删掉」——关掉输出不等于丢掉用户已有的日志。这个代价是刻意付的：宁可配置的那一刻就知道写不进去 |
| 日志文本格式里线程名的可选段 | 线程名只有主线程之外少数情况才有。直接拼 `[t:<id> <name>]` 时，名字为空会在行里留一个孤立空格，按空格切分日志的工具会多切出一段空字段 | 名字为空时整段不输出（`[t:<id>]` 与 `[t:<id> <name>]` 两种形态）。定宽短名（`"INFO "` / `"WARN "` 都占 5 格）则是为了级别列之后的内容能对齐——日志是给人竖着扫的 |
| 耗时辅助用 `start()` / `stop()` 两个调用 | 中途 `return`、抛异常、或忘了写 `stop()` 的路径都会漏记——而漏记的那条恰恰最可能是「为什么这里有时很慢」的答案 | 改成 RAII：作用域开头构造一个 `Log::Stopwatch`，离开作用域自动记一条。`finish()` 可重复调用（手动结束过就只记一条）；**级别在析构时判断**，这样「先放计时器、再用命令行调级别」也能出结果 |
| 日志套件链接 QtGui | 别的套件都是 `QT += gui`（`QKeySequence` 属于 QtGui）。日志只用 QtCore，若也跟着加，等哪天有人往 `logging.cpp` 里加图形依赖就没人会发现 | `LoggingTests.pro` 写 `QT -= gui`，让「混进 QtGui 依赖」变成构建失败。这也是 `QTEST_MAIN` 在这里展开成 `QCoreApplication` 的原因（无需 offscreen 平台） |
| `\` 转义用「不认识的转义就原样保留」这种宽容处理 | Windows 用户把路径分隔符敲进掩码（`build\out`）时，它会被静默解释成 `buildout`——过滤看起来生效了、只是漏了一批文件，而这类偏差在界面上完全无法自查 | 转义白名单是**封闭**的（`* ? [ ] - # \`），其余一律报错，并且提示里明确写「如果这是 Windows 路径分隔符，请改写成 `/`」。宁可报错，也不要给出一个看起来生效的过滤器 |
| 掩码里的分隔符跟着平台走 | 会让「预设库导出给团队共享」（FILT-007）带上平台色彩：`build/out` 在一台机器上排除子目录、在另一台上排除一个名字里带 `/` 的条目 | 掩码里 `/` 恒为分隔符、`\` 恒为转义，与平台无关。顺带让「`*` / `?` / 字符集都不可能吃掉 `/`」成为结构性事实——段是按 `/` 切出来的，段里根本没有 `/` |
| 只按 `\n` 切过滤声明的行 | Windows 上编辑过的预设文件是 CRLF，于是每行末尾多一个 `\r`，`*.tmp` 悄悄变成 `*.tmp\r`——**静静地对不上任何文件**，而掩码本身看起来完美无缺 | 切行同时认 `\n`、`\r\n`、`\r`（`\r\n` 算一个换行，否则空行的行号会整体偏大）。注意修复层在**声明解析**而不是掩码解析：行尾是文本文件的属性，不是掩码语言的属性 |
| 大小写不敏感时把区间端点也折叠 | `[A-_]` 折叠后是 `a`..`_`，而 `a`(U+0061) 比 `_`(U+005F) 大——区间**反了**，这个字符集从此永远匹配不到任何东西，表面上却一切正常 | 不折端点，改成「拿反转大小写的字符再试一遍」。这样 `[a-z]` 命中 `A`、`[A-Z]` 命中 `a` 都自然成立，也不必为区间维护两套边界 |
| 段级匹配写成递归回溯 | `**` 每一步都有「吃零段」与「吃一段」两个选择，`**/**/**/…` 对上有 N 段的路径时有 2^N 条路径。一个手抖敲出来的掩码就足以让扫描停在那里不动，而现象是「程序卡死」而不是「结果不对」 | 用「可达掩码段」表做 NFA 模拟，复杂度 O(路径段数 × 掩码段数)。`**` 的 ε 闭包只朝后传播，所以一次顺序扫描就到不动点，不需要反复迭代 |
| `**` 的语义不定就开写 | 不定下来的话，`a**b` 与 `a*b` 的行为没有任何可预期的区别，用户只能靠试；而两种实现都能自圆其说，谁也不会发现自己在改别人的规则 | 明确「`**` 只有**独占一段**时才跨目录，段内等同于 `*`」（gitignore / ant / ripgrep 的共同规则），并把它写进速查表——文档与行为同源，改一头另一头会红 |
| 不含 `/` 的掩码直接拿整条相对路径匹配 | `*.txt` 对不上 `src/a.txt`（因为 `*` 不跨 `/`），而「文件掩码」在所有人心里都是「任意目录下的 .txt」——用户会认为掩码功能坏了 | 由**掩码自己**决定按哪一侧匹配：不含 `/` 按名字匹配，含 `/` 按相对路径从起点匹配。这样 `*.txt` 处处生效、`src/*.txt` 又不会误伤别处的同名子树 |
| 把「速查表」写成 markdown 表格 | 文档里的示例与实现必然在某次修改后分家，而错误方式是「帮助里说 `[!a]` 是取反、程序其实不认」这种用户完全无法自查的偏差 | 速查表做成**数据**（`maskSyntaxReference()`），每条带可执行样本，测试逐条跑一遍；纯文本速查由数据生成。FILT-011 的「文档与测试语料同源」由此变成一条会红的用例 |
| 未闭合的 `[` 当成字面量 | 用户把 `[abc` 漏掉一个 `]` 时，掩码会静静变成「匹配字符串 `[abc`」——过滤看起来还在工作，只是永远不命中，界面上完全看不出问题 | 报错并给出列号与建议（补 `]`，或写成 `\[`）。「永远匹配不到任何东西」的掩码要和语法错误一样被拦下来 |
| Qt 5.15 的 `QRegularExpression` **没有**匹配超时 | FILT-002 第 2 条要求「正则匹配 200ms/条超时保护」，而 `setMatchTimeout()` / `matchTimeout()` 是 **Qt 6.0** 才加的（已在本机 `qregularexpression.h` 里核对）。照直觉把它当成「顺手加个参数」会做到一半才发现做不到 | 开 FILT-002 之前先定路线：走工作线程 + 截止时间（超时就放弃那一条并记错，注意线程无法真正杀掉），或直接调 PCRE2 的 match limit。两条都不是小活 |
| 声明的槽/构造函数没有实现，失败发生在**链接**期 | `moc_tst_filter.o` 引用了 `TstFilter::hiddenIsTotalMinusIncluded()`，报错是 `Undefined symbols` 且指向 moc 生成的文件，看起来像 moc 出了问题而不是「少写了一个函数」；`Mask::Mask()` 只声明未定义同理 | 头里声明的槽必须在 .cpp 里有实现；构造函数声明了就要定义（或写 `= default`）。看到 `Undefined symbols ... referenced from ... moc_*.o` 时，第一个要查的就是「哪个声明漏了实现」 |
| 报告「0 warning」时不说范围 | `_build-lqcompare` 是增量构建，看到的是 0 条；一旦 `make clean` 重编，第三方 LqRibbon 会冒出 `LqRibbon.cpp: unused function 'nativeWindowScaleFactor'`。于是「0 warning」这个结论到底指什么就说不清了 | 说「本仓库自己的代码 0 warning」，并注明第三方那一条不属于本仓（它在 MyClass 仓库里）。清过构建目录之后要重新数一遍 |
| 在构造函数里调虚函数（`createSettings()` 这类工厂） | 构造函数期间虚函数只派发到**基类**版本，子类的覆写永远不生效。现象是「设置改了不生效」——看起来像设置没保存，其实是工厂根本没被换掉 | 惰性构造：第一次 `sessionSettings()` 时才调工厂。`Tests/Session` 有一条用例把这件事钉住：「构造之后工厂调用次数为 0，首次取用时恰好 1 次」 |
| 会话基类用裸指针记住视图 | 容器删标签时会连带析构视图，而裸指针仍然指着那块内存。下一次 `createWidget()` 把一个已析构的对象交给界面，崩溃位置与真正的错误毫不相干 | 用 `QPointer`：视图被析构时它自动变成 `nullptr`，于是「视图不在了」成为可判定的状态，可以就地重建。有一条用例专门删掉容器再断言重建（`widgetFollowsTheContainersLifetime`） |
| 「基类不得依赖具体视图」只靠代码评审 | 这类违规是「加一行 `include`」级别的改动，评审极易漏掉；而更糟的是**主构建编得过**（它的 INCLUDEPATH 里有 `Views/Shell`、`Views/Page`），于是没有任何东西会红 | 把**测试工程**的 INCLUDEPATH 收敛到只剩本模块需要的两个目录（`Views/Session` 与 `Services/Session`），违规立刻变成测试工程构建失败；再由一条源码级用例做第二层（主构建的 CI 也会跑到它）。**注意这个办法的前提是测试工程不共享主构建的 INCLUDEPATH** |
| 自定义结构体/枚举只用于同线程直连 | 忘了 `qRegisterMetaType` 时 `QObject::connect` 只在**运行期**抱怨一句，编译期什么都看不出来；而进度上报迟早会来自后台线程，那时的现象是「进度条一直不动」 | 在会话构造时用函数内静态做一次性注册，并写一条用例断言 `QMetaType::type("…") != QMetaType::UnknownType`。凡是进信号的结构体都按这条办 |
| 用脚本做变异测试时，复原源文件后 `make` 未必重建 | 观察到「复原之后那些变异用例仍然红」，看起来像代码没改回来；其实只是构建产物没更新（时间戳判定），于是很容易得出「用例不能反向验证」的错误结论 | 复原之后显式 `touch` 一次被测文件（或删掉对应 `.o`）再 `make`，并在宣布「全绿」之前确认那次构建**真的编译了**。变异测试的结论只在「每一轮都确认过构建确实发生」时才可信 |
| 交接文档里「扫描 N 个源文件」这类数字会随新增模块过期 | `check_winapi.py` 数的是 `Code/` 下全部源文件（含 `Tests/`），新加一个套件这个数字就变一次。本轮它从 71 变成了 79，而文档里还写着 63——没有任何机制会主动发现 | 每次跑护栏时把输出里的数字与 §2 的表格核对一遍；`check_spec.py` 的文档计数护栏只管规格条目数，管不到这些 |
| `setDirty(false)` / `setStatusText()` 这类「设成某个值」的入口不去重 | 界面常见的写法是「按当前状态重写一遍」，每次都发信号会让状态栏在批量过程中反复重排、标签上的「*」反复重绘，看起来在抖。而这类抖动很难归因到某一次赋值 | 「值没变就不发信号」写在这几个入口里（与 `MemorySessionSettings` 的后三个入口同一条纪律）；错误是例外——它是**事件**而不是状态，同一个原因连报两次要收到两条 |
| `check_icons.py` 的引用正则**会扫注释** | 我在注释里写下一个完整的资源路径字面量（`":/Pictures/xxx.svg"` 的形状），护栏立刻报「代码引用了它，但 `Pictures.qrc` 未声明」。它认的是**形状**，不区分这行是代码还是注释 | 注释里要举例就写成 `:Pictures/<名字>.svg`（尖括号占位，不构成一个真实引用）。同理，测试里要**故意造一个不存在的图标键**时，必须把它拆成三段拼接——否则那个「假」键会被护栏当成真的 |
| 测试头文件只 `#include <QObject>` | 首次构建一次性报出三个 `no function template matches function template specialization 'toString'` 加几十行 `use of undeclared identifier 'QVERIFY2'/'QCOMPARE'/'QFAIL'`，看起来像是整个 QtTest 没接上 | 测试头文件里写 `#include <QtTest>`（既有套件都是这么写的，照抄即可）。**报错条数与真实原因严重不成比例**，见到这种规模的「未声明」先怀疑头文件而不是语法 |
| 用脚本做变异测试时只构建/运行了**一个**套件 | 变异明明生效、目标用例也确实会红，脚本却报「没检出」——因为那条用例住在**另一个**套件里。第一版就因此把三处变异误判成「用例不能反向验证」 | 变异测试脚本必须显式列出**所有**可能覆盖该变异的套件并逐个跑。另外单独复验一次那一条变异（打印「变异生效：True 出现次数：1」并肉眼确认 FAIL），别让脚本的结论单独决定「用例没写到位」 |
| 把源文件改回原样后紧接着写下一轮变异 | 与上面那条**构建时序**坑叠加：`finally` 里刚 `copy` 回来，下一轮立刻写入并 `make`，时间戳粒度不够时 `make` 判定「没有变化」→ 这条变异其实没被编译进去 → 报「没检出」 | 每轮之间强制重建（`make clean`，或至少 `os.utime` 一下被测文件 + 确认那次构建**真的编译了**）。这两条一起看：**变异测试的结论只在「每一轮都确认过构建确实发生」时才可信** |
| 变异测试脚本累积删除文件触发沙箱护栏 | 脚本跑到一半被 `[safe-delete][SAFE_DELETE_BULK_CONFIRM_REQUIRED]` 打断（本轮累计 97 次、阈值 50），看起来像脚本写崩了 | 这不是脚本的问题，是每次 `os.remove(...bak)` 都在计数。**要确认的是「源文件是否已复原」**：把复原放在 `finally` 里**先 copy 再 remove**，这样中途被打断也是干净的；打断后手工核一遍 `find Code -name "*.bak"`、`git status` 与关键内容计数 |
| `session.pri` 里嵌套 `include(../Filter/filter.pri)` | `services.pri` 已经把 Session 与 Filter 两个 `.pri` 各 include 一遍（各自在 `exists()` 保护下），再嵌套一次会让 `mask.cpp` 以**两份 SOURCES** 进同一个 Makefile。qmake 不去重——现象是重复符号或同一份代码编两次 | 子模块的 `.pri` 只加**搜索路径**（`exists($$PWD/../Filter/mask.h): INCLUDEPATH += $$PWD/../Filter`），不嵌套 include 兄弟模块的 `.pri`。代价是单独构建该模块的工程要自己再 include 一次，把它写在 `.pri` 的注释里 |
| 服务层需要「创建界面层对象」的工厂 | 注册表在 `Services/Session/`，而工厂返回的 `CompareSession` 定义在 `Views/Session/`。直接 `#include` 界面头会撞上 `check_layering.py`；退一步写成 `QObject *` 又丢掉了类型安全，且让「制造一个会话」这件事在类型上无从检查 | **只前向声明** `class CompareSession;` + `std::function<CompareSession *(QObject *)>`。前向声明不会被分层检查的 include 正则匹配，于是既守住分层又保住类型安全。顺带的收益很大：`Tests/SessionType` 因此是**纯 QtCore** 的套件（`QT -= gui`），本仓库第一次有「服务层的会话框架测试」 |
| `validate()` 里重查 `add()` 已经把住的规则 | ID 格式、ID 重复、显示名为空、掩码编译失败都是 `add()` 就会拒绝的，于是 `validate()` 里那几条判断**永远走不到**。而「一条永远不会红的护栏比没有护栏更糟」——它会让人以为这块已经被守住了 | `validate()` 只查**登记时没把住**的几项（英文原名缺失、图标键不以 `.svg` 结尾、掩码含大写、已编译掩码数与声明数不一致）。测试相应改成「内置表 `validate()` 为空」+「用合成表逐条验证它**能**报出来」，而不是断言一个不可达的分支 |
