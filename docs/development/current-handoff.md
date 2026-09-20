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
级别过滤对宏与直接调用一视同仁、输出行带线程 id、并支持挂任意接收者与 RAII 计时。
界面上仍是 169 个按钮里 31 条带处理器，其余点击后提示对应 ACTION-ID。

## 1.1 已落地的服务层模块

| 模块 | 条目 | 状态 | 测试 |
| --- | --- | --- | --- |
| `Services/Command/` | UI-024 | 骨架 | `Tests/CommandRegistry`（14 用例） |
| `Services/Log/` | ENG-006 | **部分完成**（界面输出面板尚未接线） | `Tests/Logging`（32 个用例函数） |
| `Services/Files/`（文件系统） | PLAT-002 | **部分完成**（Windows 实现未编译验证） | `Tests/FileSystem`（50 用例） |
| `Services/Files/`（回收站） | PLAT-003 | **部分完成**（Windows 实现未编译验证） | `Tests/Trash`（35 用例） |
| `Services/Files/`（名称与 Unicode） | PLAT-007 | **部分完成**（长路径只写在 Windows 侧，未编译验证） | `Tests/PathName`（40 用例 + 1 个仅 Linux 执行） |
| `Services/Files/`（错误携带与批量处置） | PLAT-008 | **部分完成**（界面动作尚未接上） | `Tests/Batch`（36 用例） |
| `Services/Platform/`（系统图标） | PLAT-004 | **部分完成**（Windows / Linux 实现未在目标平台验证；界面尚未取用） | `Tests/PlatformIcon`（46 个用例函数，含 3 条走真实图标源） |
| `Services/Platform/`（Shell 集成） | PLAT-005 | **部分完成**（Windows 注册表薄层未编译过；界面尚未接入；本机平台能力置灰说明已落地） | `Tests/ShellIntegration`（101 个用例函数） |

PLAT-002 的详细说明与其「第 2 条完成标准为何不勾选」见
[issue #325](https://github.com/LorenHan/LqCompare/issues/325)；
PLAT-003 见 [issue #324](https://github.com/LorenHan/LqCompare/issues/324)；
PLAT-007 见 [issue #328](https://github.com/LorenHan/LqCompare/issues/328)；
PLAT-008 见 [issue #330](https://github.com/LorenHan/LqCompare/issues/330)；
PLAT-004 见 [issue #329](https://github.com/LorenHan/LqCompare/issues/329)；
PLAT-005 见 [issue #326](https://github.com/LorenHan/LqCompare/issues/326)；
ENG-006 见 [issue #338](https://github.com/LorenHan/LqCompare/issues/338)。

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

## 2. 已验证的事实（不用再花时间确认）

| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试（全量） | **358 passed / 0 failed / 1 skipped**（Batch 36 + CommandRegistry 14 + FileSystem 50 + Logging 34 + PathName 40 + PlatformIcon 48 + ShellIntegration 101 + Trash 35） | `Code/Tests/run-tests.sh` |
| 文件系统抽象层 | 47 个纯逻辑用例 + 3 个真实文件系统用例全通过；其中 20 个覆盖 **Windows** 路径规则（盘符 / UNC / 长路径前缀 / 大小写），在 macOS 上真实执行 | `Code/Tests/run-tests.sh FileSystem` |
| 回收站 | 35 个用例全通过。其中 9 个验证 XDG（Linux）的路径与 `.trashinfo` 规则、2 个是**真实**的废纸篓往返与冲突拒绝、多个断言「不可用时搬移函数一次都没被调用」 | `Code/Tests/run-tests.sh Trash` |
| 名称与 Unicode | 40 个用例通过 + 1 个跳过（无效 UTF-8 名字的用例只在 Linux 上执行，CI 会跑）。覆盖字节保真往返、UTF-8 边界与过长编码、Unicode 组合形式、六类文件名问题的原因与位置 | `Code/Tests/run-tests.sh PathName` |
| 错误携带与批量处置 | 36 个用例全通过。其中 9 个验证错误码在三种域下的携带与显示（含「未识别的码只给数字」）、11 个验证失败清单分组、12 个验证执行流程（含「重试只跑失败项」与「停止不移除已完成进度」）、4 个走真实文件系统做一次「设为只读 → 解除只读」往返 | `Code/Tests/run-tests.sh Batch` |
| 系统图标 | 46 个用例函数（QTest 合计 48，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分四组：17 个验证缓存键与尺寸规则（扩展名折叠 7 + 键的合成与解析 5 + DPI 缩放 4 + Windows 档位收拢 1）、9 个验证有界 LRU 的淘汰与命中统计、6 个验证请求去重队列、11 个验证服务层（同步/异步/去重/回退/换比例清缓存）；另有 **3 个走真实系统图标源**（macOS 上真实执行：断言拿到非空像素、断言文字文件与文件夹的图确实不同） | `Code/Tests/run-tests.sh PlatformIcon` |
| 主程序构建 | 通过，`iconservice_mac.mm` 编进主程序，**0 warning**（原先 7 条 `-Wunguarded-availability-new` 已用 `API_AVAILABLE` 消掉，不是压掉） | `qmake && make -j8` |
| 主程序运行 | 离屏启动正常，日志 `Ribbon 构建完成：10 页 / 45 组 / 169 个按钮`，注册表自检 0 问题 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| Shell 集成 | 99 个用例函数（QTest 合计 101，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分十一组：A 动作与目标 14、B 选项 8、C 命令行引号 8、D 计划 13、E 安装 10、F 卸载与还原 9、G 校验 5、H 残留 6、I 能力 4、J 预演 3、K 命令行解析 9。**全部跑在功能完整的内存注册表上**，因此安装回滚与卸载还原是在本机真实执行的流程，不是桩 | `Code/Tests/run-tests.sh ShellIntegration` |
| Shell 集成的命令行引号 | 用测试内置的 `CommandLineToArgvW` 参考实现做往返：`"C:\Program Files\…\LqCompare.exe" --shell-action=compare "%1"` 切回来必须还是两个原值，含「结尾反斜杠要翻倍」这条最容易写错的规则 | `Code/Tests/run-tests.sh ShellIntegration` |
| 分级日志 | 32 个用例函数（QTest 合计 34，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分五组：A 级别与过滤 6、B 格式 4、C 输出目标 11、D 耗时辅助 6、E 级别名解析 4、以及 `initTestCase`/`cleanupTestCase`。这套件**刻意不链接 QtGui**：哪天有人往 `logging.cpp` 里加图形依赖，本工程会立刻构建失败 | `Code/Tests/run-tests.sh Logging` |
| 日志级别真的生效 | 不带参数启动**不产生任何日志输出**（默认 `warning`，启动横幅是 `info`）；`--log-level info` 打印带线程 id 的完整启动序列；`--log-level debgu`（拼错）打印「无法识别的日志级别「debgu」，改用 warning」 | `QT_QPA_PLATFORM=offscreen ./LqCompare [--log-level …]` |
| 分层检查 | 通过（Services 未反向依赖界面） | `python3 tools/check_layering.py` |
| 图标检查 | 通过（27 个图标，声明/引用/文件三者一致） | `python3 tools/check_icons.py` |
| 规格自检 | 通过（369 条，P0 59 条，PRD 与数据同步） | `python3 tools/check_spec.py` |
| Shell 可移植性 | 通过（1 个脚本，无 bash 4 内建与 GNU 工具扩展） | `python3 tools/check_shell.py` |
| Windows 宽字符 API | 通过（63 个源文件、清单内 43 个 API；自测 17 个样本） | `python3 tools/check_winapi.py [--self-test]` |
| 测试套件 | `358 passed / 0 failed`，且「无套件匹配」被视为失败（exit 2） | `Code/Tests/run-tests.sh` |
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
│   ├── Shell/                    homepage（Home 页）、sessionarea（会话标签容器）
│   ├── Page/                     ribbonlayout（声明表驱动的 Ribbon 构建）
│   ├── shell.pri / page.pri / views.pri
├── Services/
│   ├── Command/                  commandregistry（命令注册中心）
│   ├── Log/                      logging（分级日志 / 级别过滤 / 三目标 / 耗时辅助）
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
│   ├── command.pri / log.pri / files.pri / platform.pri / services.pri
├── Pictures/                     27 个 SVG 图标 + Pictures.qrc
├── Tests/
│   ├── Support/                  fakefilesystem（内存文件系统）、faketrashservice
│   │                             （内存回收站），多套件共用
│   ├── CommandRegistry/          tst_commandregistry + .pro（14 用例）
│   ├── FileSystem/               tst_filesystem + .pro（50 用例）
│   ├── Logging/                  tst_logging + .pro（32 用例函数，刻意不链接 QtGui）
│   ├── PathName/                 tst_pathname + .pro（40 用例 + 1 个仅 Linux）
│   ├── Trash/                    tst_trash + .pro（35 用例）
│   ├── Batch/                    tst_batch + .pro（36 用例）
│   ├── PlatformIcon/             tst_platformicon + .pro（48 用例）
│   ├── ShellIntegration/         tst_shellintegration + .pro（99 用例函数）
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

> 上面这一行由**单独的提交**补写，原因见下一段——把一个提交的提交号写进它自己，
> 会因为 `--amend` 每次都改变提交号而永远对不上。

远端：369 个 issue 全部创建，标签为 `需求 / 待实现 / <模块> / <优先级>`，
其中 P0 59 条。反查入口是 `docs/github/prd-issues.json`。

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

| 对话 | 工作流 | 从哪条 issue 开始 | 交付什么 |
| --- | --- | --- | --- |
| **A 平台底座** | 继续 | `PLAT-006`（单实例与进程间通信） | 仍在 `Services/Platform/` 内；`platform.pri` 已接好 QtGui 与注册表相关的 `LIBS` |
| **B 会话框架** | 新开 | `SESS-001`（会话基类）→ `SESS-002`（类型注册表）→ `SESS-006`（设置框架） | `Services/Session/`、`Views/Session/`，含测试 |
| **H 过滤与格式** | 新开 | `FILT-001`（掩码解析器，纯算法、最容易写出完整测试） | `Services/Filter/`、`Services/Format/` |

三个工作流的目录互不重叠，`services.pri` 的 include 已一次加齐（`exists()` 保护），
因此三方都不需要改共享文件。详见 [parallel-workstreams.md](parallel-workstreams.md) §1。

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
| `check_spec.py` 的文档计数护栏会误报「N 个条目」这种口语 | 我在架构文档里用「几千文件的目录会占几千格」举例时，最初写成了「N 个条目」的形状（数字紧跟「个条目」），护栏的 `(\d+)\s*个条目` 把它当成规格条目数，直接报错；改完这行**引用它的坑表本身**又踩了第二次 | 该护栏的模式是刻意宽进严出的（宁可误报也不漏报过期数字）。**写文档时避免让数字紧贴「个条目」「条规格」这类词**，表达缓存/列表数量时换成「格」「项」。反过来也不要把这个模式改窄——它正是靠宽匹配才抓到了 5 处过期数字 |
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
