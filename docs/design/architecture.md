# LqCompare 代码架构

> 状态：2026-09-20 基线。本文档描述 `Code/` 的分层结构、依赖规则与装配方式。
> 结构上刻意与 Ailecium 保持一致，便于两个项目共享经验与工具。

## 1. 定位与目标

LqCompare 是 Qt 5.15.2 / C++17、qmake 构建的文件与文件夹比对工具。
交付目标是 Windows MinGW 8.1.0 32 位；macOS 与 Linux 用于开发与测试。

架构目标只有三条：

1. **分层清晰**：UI 与业务逻辑分离，业务逻辑可脱离界面单独测试。
2. **目录即职责**：看到目录名就知道里面放什么。
3. **改动可控**：任何一次改动都能独立编译验证，并能通过 git 单独回退。

## 2. 分层总览

```
┌─────────────────────────────────────────────────────────────┐
│  App/           应用装配层                                   │
│  main.cpp · RibbonWindow · MainWindow                        │
│  职责：进程入口、顶层窗口外壳、把各模块组装成应用             │
└───────────────────────────┬─────────────────────────────────┘
                            │ 允许依赖（向下）
┌───────────────────────────▼─────────────────────────────────┐
│  Views/         视图层                                       │
│  Session（会话基类）· Shell（会话容器 · Home 页）              │
│  Page（Ribbon 页面装配）                                      │
│  职责：界面呈现、用户交互、把用户意图翻译成对服务层的调用      │
└───────────────────────────┬─────────────────────────────────┘
                            │ 允许依赖（向下）
┌───────────────────────────▼─────────────────────────────────┐
│  Services/      服务层（不含任何 UI 依赖）                     │
│  Command（命令注册中心）· Log（分级日志）                      │
│  Session（设置接口 · 类型注册表）· Filter（掩码与过滤）        │
│  Files（文件系统 · 回收站）· Platform（系统图标 · Shell 集成） │
│  职责：文件/进程/比对/压缩包等业务能力，可被无界面测试         │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  ThirdParty/    第三方源码                                    │
│  LqRibbon（位于 MyClass 仓库，经 .pri 引入）                   │
└─────────────────────────────────────────────────────────────┘
```

**依赖方向铁律**：`App → Views → Services`。Services 层**不得**反向 include 任何
`Views/` 或 `App/` 的头文件。

这条规则由 `tools/check_layering.py` 在 CI 中强制，而不是靠代码审查：
这类违规是「加一行 include」级别的改动，评审时极易漏掉，而后果（服务层不再可测）
要过很久才暴露。

两条关于「可测性从哪来」的结构事实，写在这里免得下一个人重新踩：

1. `Views/` 与 `Services/` 下每个模块各自一个 `.pri`，且 `.pri` 自带
   `INCLUDEPATH += $$PWD`，所以测试工程只 include 自己真正用到的那个模块的 `.pri`
   （ENG-002），不必把整层拉进来。这是「服务层套件能在没有图形环境、甚至在
   Windows 上编译的 CI 里跑」的结构前提。
2. **但「测试套件都不依赖界面」是错的**。`Tests/AppIntegration` 与
   `Tests/CommandActions` 的 `.pro` 里 include 了 `ThirdParty/lqribbon.pri`——
   它们要真的驱动 Ribbon 布局。实测（其余 64 个套件 + 这 2 个，共 66 个工程）
   只有这 2 个依赖 LqRibbon，而 LqRibbon 在私有仓里。**这个清单必须靠跑 qmake
   数出来，不能靠 grep 推断**：`grep` 没搜到不等于不存在，本仓就在这件事上错过一次
   （详见 `docs/development/current-handoff.md` §6）。

## 3. 目录清单

### 3.1 App/（应用装配层）

| 文件 | 职责 |
| --- | --- |
| `main.cpp` | 进程入口、`QApplication` 装配、日志初始化、命令行解析、注册表自检 |
| `RibbonWindow.{h,cpp}` | 应用级 Ribbon 外壳：样式、快速访问栏、居中搜索栏、右键菜单（UI-001 ~ UI-006） |
| `MainWindow.{h,cpp}` | 主窗口：构建 Ribbon、注册命令、装配会话容器与输出面板、维护状态栏 |
| `app.pri` | 本模块构建描述 |

### 3.2 Views/（视图层）

| 模块 | 文件 | 职责 | 构建描述 |
| --- | --- | --- | --- |
| `Session/` | `comparesession.{h,cpp}` | 会话抽象基类：统一契约（视图 / 打开 / 关闭 / 重载 / 保存 / 脏标记 / 设置）与三个公共出口（状态栏文本、错误上报、进度上报）（SESS-001）。状态栏那一路是**两条通道**：文本（`statusTextChanged`）与严重程度（`statusSeverityChanged`，取值 `Normal` / `Warning`）。分开的理由是「图标不是文字」——容器要从一句话里推可靠地推不出「该不该亮警告图标」，让容器去嗅自己刚拼好的那句话就是反向解析，任何一次文案调整都会静默让图标失灵（TXT-010） | `sessionview.pri` |
| `Session/` | `settingsdialog.{h,cpp}` | 会话设置对话框：左侧 Tab 列表（按会话类型变化）、右侧由声明逐项生成的表单、底部作用域下拉与「恢复默认 / 应用 / 确定 / 取消」，以及未保存改动时的确认（SESS-006）。**框架本身不含任何具体设置项** | `sessionview.pri` |
| `Shell/` | `homepage.{h,cpp}`、`sessionarea.{h,cpp}` | Home 入口页（SESS-003）、会话标签容器（SESS-010）。容器把**当前**会话的两条状态通道转发出去：原地变化时转发，切标签时**两者都重播一次**（与既有的 `statusTextChanged` 约定对称，TXT-010） | `shell.pri` |
| `Page/` | `ribbonlayout.{h,cpp}` | 声明表驱动的 Ribbon 页面/分组/按钮构建（UI-007 ~ UI-024） | `page.pri` |
| `Vcs/` | `vcsview.{h,cpp}`、`blameview.{h,cpp}` | 只读的版本控制视图：HEAD / 索引 / 修订 / 提交日志四种模式（VCS-002 ~ VCS-010）；逐行追溯 `BlameView`——按作者 / 按日期龄 / 按提交块三种着色、同提交的连续行合并为块、只看某作者、悬停显示提交信息（VCS-012 / VCS-013） | `vcsview.pri` |
| `Text/` | `textcomparesession.{h,cpp}`、`textcompareview.{h,cpp}` | 文本比对的**会话与双窗格视图骨架**（TXT-001）：`TextCompareSession` 承载左右两份 `Text::Document` 与一份 `Text::Result`，视图只负责渲染与转发交互、**不自行计算差异**；`TextCompareView` 用 `QSplitter(Qt::Horizontal)` 铺**并排**两栏，两栏共用**同一个** `Result`——每一行由 `Result::rows[i]` 的两半决定，某一侧缺行时铺 **填充行**（`lineNumbers()` 为 -1、正文为空），因此两侧视觉行数与模型行数恒等、行号与行高天然对齐。`TextPane::lineNumbers()` 是**故意公开**的：号码槽与正文是两条链（`setPlainText()` 与 `setLineNumbers()` 各走各的路），只断言正文会让「号码没铺下去 / 铺成另一侧那份」零覆盖（TXT-001）。状态栏文本里除了差异处数、两侧编码与行尾描述，还会按开关追加「已忽略行尾差异」「已忽略末尾换行」两句，并把**行尾混合**这一情形通过 `CompareSession::StatusSeverity` 抬成 `Warning`（TXT-010） | `textview.pri` |
| （夜间新增的其余比较视图） | `Folder/`、`Merge/`、`Special/`、`Table/`、`Sync/`、`Filter/`、`Version/`、`Media/`、`Registry/`、`FolderMerge/`、`Archive/`、`Options/` | 各比较类型的会话与视图：文件夹、三方合并、十六进制与图片、表格、目录同步、过滤、版本与 PE、媒体标签、注册表、三方文件夹合并、压缩包、选项页。**逐条的公开接口、已验证范围与「尚未覆盖的规格」见各自的 `docs/development/team-*.md`** | 各目录下的 `*view.pri` |

### 3.3 Services/（服务层）

| 模块 | 文件 | 职责 | 构建描述 |
| --- | --- | --- | --- |
| `Command/` | `commandregistry.{h,cpp}` | 全量命令的注册、查询、执行与自检（UI-023 ~ UI-025） | `command.pri` |
| `Log/` | `logging.{h,cpp}`、`logfiles.{h,cpp}`、`diagnostics.{h,cpp}` | 分级日志：级别过滤（宏与直接调用同一套判断）、带线程 id 的定宽文本格式、三个输出目标（控制台 / 文件 / 任意接收者如界面输出面板）、记录经过的结构化载体 `Record`、RAII 的耗时辅助 `Stopwatch`（ENG-006）；**按大小 / 按日滚动与清空**（`logfiles`：`RotationPolicy` 纯数据策略对象 + 自检、纯函数 `rotationDecision()` 判「该不该转」（判据用注入的 `now` 与文件日期，因此可用假时间测跨天）、`applyLogRotation()` 落盘、`rotatedLogPath()` / `logHistoryFiles()` 历史枚举，`clearLogFile()` **截断而不删除**）（OPT-010）；**诊断包导出**（`diagnostics`：环境报告与 `manifest.json` 的内容与文件名、前缀表驱动的路径脱敏 `sanitizeDiagnosticText()`、导出前告知文案 `diagnosticNoticeText()`，以及把当前日志 + 滚动历史 + 环境报告 + 清单打进 `lqcompare-diagnostics-<时间戳>/` 的 `buildDiagnosticBundle()`；失败时整目录回滚不留半个包）（OPT-010） | `log.pri` |
| `Session/` | `session.{h,cpp}`、`sessiontype.{h,cpp}`、`settingschema.{h,cpp}`、`settingscope.{h,cpp}` | 会话设置的抽象接口 `SessionSettings` 与一个内存实现 `MemorySessionSettings`（SESS-001 契约里的 `sessionSettings`）；会话类型描述子 `SessionType` 与注册表 `SessionTypeRegistry`——内置 14 种类型的 ID、显示名、英文原名、图标键、默认文件掩码、分组、版本归属（Pro / Standard）、平台限定，以及按掩码的注册顺序优先查询（SESS-002）；**设置项的声明模型**（`SettingItem` / `SettingGroup` / `SettingsTab` / `SettingsSchema`，含标题、说明、控件类型、默认值、校验规则）、**设置草稿**（`SettingsDraft`：读写、脏判定、全有或全无的应用、恢复默认）、**未保存改动的询问策略**（`inquiryForUnsavedChanges`）与**声明目录**（`SessionSettingsCatalog`）（SESS-006）；**三层作用域的覆盖链与写入路由**（`ScopedSessionSettings`：视图 → 会话 → 类型 → 出厂默认的解析、只落目标层的写入、切换作用域与关闭标签的提示文案）（SESS-007）。设置落盘留给 SESS-008 | `session.pri` |
| `Filter/` | `mask.{h,cpp}`、`maskfilter.{h,cpp}`、`attributefilter.{h,cpp}`、`filterstack.{h,cpp}`、`namefilter.{h,cpp}`、`contentfilter.{h,cpp}` | 掩码语言（`*` / `?` / `[...]` / `**` 的解析与匹配、转义、语法速查）与过滤声明（包含/排除的叠加、排除优先、大小写策略、预览计数）（FILT-001）；**属性条件**（大小范围 / 修改时间范围 / 属性位 / 所有者与组、条目元数据快照 `EntryMetadata`、条件表自检、`decideEntry()` 把名称侧与属性侧合成**与**）（FILT-003）；**名称过滤**（精确名 / 通配符 / 正则三种模式、包含任一 / 不包含任何 / 全部满足三种组合语义、实时校验、正则的静态回溯预检 + 超时保护 + 断路器、具名预设的可移植文本往返）（FILT-002）；**内容过滤**（行过滤器：按整行 / 通配 / 正则排除行，三态结论与「说不清的放行」；关键字节：`\xHH` 解码、任一/全部两种组合方式、文本输入判「不适用」；两条与忽略规则的先后顺序；显式启用与性能提示文案）（FILT-004） | `filter.pri` |
| `Files/` | `filesystem.{h,cpp}`、`pathutils.{h,cpp}`、`pathname.{h,cpp}`、`trash.{h,cpp}`、`batch.{h,cpp}`、`fileopsoptions.{h,cpp}`、`filesystem_<平台>.cpp`、`trash_<平台>.<ext>` | 文件系统服务抽象层（路径、名称、元数据、枚举、时间戳、属性）、错误携带（分类 + 原始系统码）、回收站服务（可逆删除、撤销）、批量操作的失败清单与重试（PLAT-002 ~ PLAT-003、PLAT-007 ~ PLAT-008）；**文件操作的默认行为**（`FileOperationPolicy`：删除方式 / 覆盖策略 / 保留的元数据项 / 两个确认阈值 / 校验方式，以及「默认值必须保守」这条契约的机器可读形式）（OPT-005） | `files.pri` |
| `Platform/` | `iconkey.{h,cpp}`、`iconcache.{h,cpp}`、`iconservice.{h,cpp}`、`registrystore.{h,cpp}`、`registrystore_<平台>.<ext>`、`shellintegration.{h,cpp}`、`iconservice_<平台>.<ext>`、`instanceprotocol.{h,cpp}`、`singleinstance.{h,cpp}` | 平台集成：缓存键与尺寸规则、按类型的图标缓存与请求去重、系统图标解析与回退（PLAT-004）；注册表存储抽象与 Shell 集成（右键菜单、文件关联、安装/卸载/校验/残留检查）（PLAT-005）；**单实例**：标识符规则（端点名 / 共享内存键的合成与净化）、线协议的编解码与分帧、转发退出码表、命令行开关表、窗口置前判定（`instanceprotocol`），以及共享内存占标识 + `QLocalServer` 收参数的实做与超时降级（`singleinstance`）（PLAT-006） | `platform.pri` |
| `Report/` | `report.{h,cpp}` | 比较结果出报表：HTML（离线单文件、全部非信任字段 HTML 转义、内嵌 CSP、分组与折叠、固定脚本只用于搜索/状态过滤/分组）与纯文本两种格式，并排 / 交错 / 摘要 / 统计四种布局；`writeFile` 走 `QSaveFile` 原子保存、默认拒绝覆盖并额外拒绝覆盖比较源；`write(QIODevice*)` 分块渲染（单次设备写入 ≤ 64 KiB，支持短写重试） | `report.pri` |
| `Patch/` | `patch.{h,cpp}`、`patchapply.{h,cpp}` | unified 补丁的生成（多文件对、可选上下文、路径前缀、无末尾换行标记）、解析（Git/SVN/Hg 前导元数据、引号与八进制路径、CRLF、一基错误行号）与**只读预演**（反向、逐 hunk、唯一偏移匹配、路径安全校验）；以及**补丁应用**——预演→备份→暂存→提交→校验的事务，失败自动回滚、`RecoveryRequired` 如实上报、`ApplicationFailureInjector` 做测试缝。**边界是一个已存在普通文件的替换**：创建 / 删除 / 多目标在写盘前拒绝 | `patch.pri` |
| `Vcs/` | `vcsbackend.{h,cpp}`、`vcsavailability.{h,cpp}` | 只读版本控制后端：从文件或目录向上识别所属仓库（含嵌套仓库、worktree 的独立 git dir 与 shared common dir）、工作副本 / HEAD / 任意修订 / 索引 stage 0 / 冲突三阶段（base / ours / theirs）读取、变更列表（含重命名与未跟踪）、日志分页与过滤、引用、blame、修订图数据。`GitBackend` 通过用户安装的 Git 执行，安全独立参数 + `GIT_*` 环境清洗 + 超时与输出上限，比较两侧落在 `QTemporaryDir` 的只读快照里。`vcsavailability.{h,cpp}` 把「这条命令现在到底能不能用」与「同一个路径不要反复去问后端」这两件同一问题的两侧收在一处（VCS-001 的第 3、4 条）：`isVcsActionId()` 是「哪些命令属于版本控制」的唯一定义（主程序与用例都从这里取清单，而不是各自抄一遍 `vcs.` 前缀），`probeAvailability()` 把后端的 `availability()` 折成 `{是否可用, 不可用原因}` 并把「没装 git」翻译成用户看得懂的那一句 `未检测到 git…`（`unavailableReason()` 只做这一层翻译，判据不在调用方）；`RepositoryCache` 是**按规范化绝对路径分桶**的识别结果缓存——空路径**不探后端**直接报无效、同路径重复查询只探一次、换了路径一定重新探（分桶出错就会拿 A 仓库的结论回答 B）、只缓存**定论**（成功或「不是仓库」；超时 / 被取消这类说不清的结果不入缓），并用一个自增的 `generation` 记录「后端被换掉」：探到一半后端被替换，回来的结果**不写进缓存**。探测一律在锁外做（锁里只取后端与代次），否则慢后端会把界面线程堵在锁上 | `vcs.pri` |
| `Format/` | `formatdefinition.{h,cpp}`、`formatdetector.{h,cpp}` | **文件格式定义与识别**（FMT-001 及其后续）：`FormatDefinition`（稳定 `id`、`name`、`sessionTypeId`、`masks`、`signatures`、`settings` 不透明袋、`builtIn`）；version=1 的 JSON 解析 / 导出 / 原子保存 / 合并（无效文档整份拒绝、无效或重复条目逐个跳过并报诊断、`baseId` 与同 ID 继承、`mergeDefinitions()` 让用户定义覆盖内置而不修改传入表、8 MiB 上限、QSaveFile 原子写）；`FormatDetector` 按「显式覆盖 → 掩码 → 内容签名 → 未知兜底」的固定优先级判定该用哪种会话类型打开，判定前一律先问 registry 有没有工厂、当前平台可不可用（**全程不调用工厂、不创建视图**）；内容探测只读前缀（默认 64 KiB、上限 1 MiB），做 UTF-8 / UTF-16 / UTF-32 BOM 与截断校验 | `format.pri` |
| `Text/` | `textdocument.{h,cpp}`、`textdiff.{h,cpp}`、`linesimilarity.{h,cpp}`、`linereplacements.{h,cpp}`、`compareconclusion.{h,cpp}` | 文本文档模型（无损解码、按行编辑、行尾保持、防覆盖的原子保存）与**行对齐引擎**。引擎有两种算法，产出**同一套** `Result`（块类型 / 行覆盖 / `differences` / `ignoredBlocks` / `alignmentLimited` 语义一致），因此切换算法不改变视图契约：`Myers` 是确定性二分 Myers（线性辅助空间、预算与深度双上限，用尽的那一段是**显式替换**绝不静默当成相同）；`Patience` 先按「两侧都只出现一次的行」计数出候选，再用**严格递增最长子序列**挑锚点，锚点之间递归，**没有唯一行的一段平滑回退到 Myers**。两者共用一个区间收集器（预算与 `alignmentLimited` 因此只有一个来源）。另有算法表 `AlignmentDescriptor` 与自检 `validateAlignmentTable()`：`availableAlignments()` 只返回已实现的算法，**未实现的绝不出现在可选清单里**（TXT-002 / TXT-003）；**行内容规范化链** `normalizedLine()`——大小写折叠（Unicode simple folding，不是小写化）与空白处理的唯一实现，两侧都过完整条链之后才判等，按选项等价而原文不同的行产出 `Change::Ignored` 而不是 `Equal`（TXT-008）。空白那一侧另有模式表 `WhitespaceDescriptor` / `whitespaceTable()` / `validateWhitespaceTable()`：三个模式（`Exact` / `IgnoreChanges` / `IgnoreAll`）**语义严格分层**（数量 → 有无），`availableWhitespaces()` 只返回已实现项，界面按这张表铺下拉、按这张表读回，`whitespaceIdentifier()` 给出日志与（将来的）设置键要用的机器可读标识（TXT-009）。**相似行对齐**（`linesimilarity`，TXT-005）是对齐**之后**的一步后处理：把 `Change::Replace` 里「够像」的行配成一对（修改），不够像的行各自成删除块 / 新增块。分值是 `2·LCS/(len左+len右)` 的百分点，两侧都过 `normalizedLine()`；阈值判定含等号（`分值 >= 阈值`）；配对是「先最大化配对数、再最大化总分值」的单调 DP，配对数优先是刻意的（「能配就配」才是这个功能的目的）。两个工作量上限（单元格 512×512、字符预算 10^8）在开跑前就判，超限**退回按位配对**并把 `limited` 交给调用方，绝不静默。因为一段改动会被铺成**若干相邻块**，「差异块个数」不再等于「用户在界面上数得出的改动处数」，所以另加一层「一处改动」的归并 `DifferenceRun` / `differenceRuns()`——凡按处计数或按处操作的地方（状态栏、上一处/下一处、复制这一处、命令行摘要）都必须过它。**行尾（EOL）规则**（TXT-010）是同一份键函数里的一层：`CompareOptions::ignoreEol` 把 LF / CRLF / CR 折叠成同一档（因此只差行尾风格的行不判为差异），`ignoreFinalNewline` 则**独立地**处理「文件末尾有没有换行」——后者靠「借用另一侧末行的行尾」实现，所以两侧都空时没有可借的东西、差异照旧报出来（判据里有 `!other.isEmpty()`）。`Document` 那一侧另给出两个只读谓词：`eolDescription()` 报 LF / CRLF / CR / `Mixed (LF a / CRLF b / CR c)` / `No line ending`，`hasMixedEndings()` 报「同时存在两种及以上行尾风格」并**刻意不把末尾无换行算作一种风格**（算了的话，任何不以换行收尾的文件都会被报成混合，警告图标会一直亮着）。三个谓词共用同一个 `countEndings()`，口径只有一个来源。**替换规则链**（TXT-012）是链上的**最前一环**：`linereplacements.{h,cpp}` 用一张表 `ReplacementRuleDescriptor` 承载四条内置规则（行首编号 / 日期时间 / GUID / 十六进制地址），表里的 `pattern` **同时**是引擎编译的正则与界面上给用户看的那一串——分成两份就立刻出现第二份事实来源，改了规则忘了改说明时界面上仍写着旧行为而没有东西会红；`availableReplacementRules()` / `validateReplacementRuleTable()` 与另外两张表同一范式，`ReplacementSet` 按**表顺序**（不是用户点选的先后）应用，命中片段换成固定占位符而不是删除（删除会让「整行原本就是一个地址」的行塌成空行，与文件里真的空行不可区分），出厂是**空集**，因此不启用任何规则时键函数与引入本条之前逐字节相同。**BOM 处理策略**（TXT-015）新开 `compareconclusion.{h,cpp}`，它同时承载**比较侧**的「BOM 差异算不算差异」、**保存侧**的「BOM 怎么写」，以及三档结论 `Conclusion`（`Identical` / `RuleIdentical` / `Different`）的合成。两张描述子表 `bomPolicyTable()`（`Automatic` / `Ignore` / `TreatAsDifference`）与 `bomSavePolicyTable()`（`Preserve` / `AlwaysWrite` / `NeverWrite`），`available*()` / `default*()` 全部**从表推导**，`validate*Table(table, expected)` 把期望值当参数传入——表里漏登记一档时，「重复」「标识符为空」那几条一条都不会响，只有拿规格去比才发现少了一项。三处刻意写死的边界：①`Automatic` 必须排在策略表**第一行**，因为 `defaultBomPolicy()` 取的是「第一条已实现的」，而它同时要等于 `CompareOptions().bomPolicy` 的初值——两个**独立来源**必须相等，否则界面一打开显示的档就不是引擎在用的档；②`bomIsEncodingCritical()` 是**白名单**（只有 UTF-16/32 的 LE/BE 四个），不是 `codecName != "UTF-8"`，否则一个错拼的编码名会被当成「BOM 是关键信息」，`Automatic` 会在这个本不该有 BOM 的编码上把差异报出来；③`Automatic` 判的是「**任一侧**的编码把 BOM 当字节序声明」（`left || right`），只看一侧不够——一侧 UTF-16LE、另一侧 UTF-8 时，少的那个 BOM 让「这份文件按什么读」从确定变成了猜测。保存侧另有两条反直觉边界：`bomSavePolicyApplies()` 对「`NeverWrite` × 字节序关键编码」与「`AlwaysWrite` × 没有可写 BOM 的编码（GBK 等）」都返回「不适用」，此时 `bomShouldBeWritten()` **静默降级成保留**而不是弹窗拒绝（用户要的是「别让这个文件带 BOM」，而那对 UTF-16 本来就不成立）；而「编码本身没有可写的 BOM」这一句必须留在函数**最后一行**——`Preserve` 那一支完全可能想要 true（文件原来带 UTF-8 BOM，用户把解码编码改成了 GBK）。用户可见的文案只有 `bomConclusionSummary()` 一份、界面层不另写：`Ignored` 那一句必须同时说出「已忽略」与「两侧的原始字节并不相同」，因为规格第 2 条要的正是「**不得标成字节完全一致**」 | `text.pri` |
| `Folder/` | `foldercompare.{h,cpp}`、`entrystatus.{h,cpp}`、`recursionstrategy.{h,cpp}`、`statuspalette.{h,cpp}` | **目录比对的引擎与扫描器**（DIR-002 ~ DIR-012 的宿主）：递归枚举两侧（符号链接一律不跟随、父目录先于子项、顺序确定）、名称大小写策略、扫描掩码与属性 / 名称 / 内容三级过滤的落点、逐条状态分类（相同 / 不同 / 仅左 / 仅右 / 类型冲突 / 错误 / 未知）、**二进制逐字节比对**（大小不等即短路；否则分块读取，遇首个不同字节立刻判不同并记下偏移量；`kMaximumCompareBlockSize`（256 KiB）是**公开常量**，因为「分块读取且块大小有上界」这条验收标准若只能靠匹配源码里的字面量来守，那就是第二份事实来源）、**「只比较前 N 字节」的快速模式**（`Options::compareFirstBytes`，出厂关闭；限内全同而文件没读完时状态降级为 `Unknown` 并把「部分比较」放进 `Entry::partialComparison` 这一维，理由见 §4 的两条相邻决策），以及**比对期间文件被改动**的检测（比对前后各取一次元数据，变了就报 `Error` 而不是给出一个对旧内容的结论）。§4 里「按位置初始化的结构体新字段只能加末尾」那条纪律的代价就落在 `Options::compareFirstBytes` 上。**条目状态的类型模型与判据**拆在 `entrystatus.{h,cpp}` 里（DIR-011）：主状态表（9 档，末尾两档「两侧均改 / 冲突」**只可能由有效基线推导**）与三张自检表（主状态 / 内容证据 / 时间关系，都带一个「表当参数」的校验函数，避免自检恒真）、与主状态**正交**的两维（内容证据：未比较 / 部分比较 / 字节相同 / 字节不同 / 规则… / CRC…；时间关系：左新 / 右新 / 同 / 未知）、存在性（`Existence`，**派生视图**而不是另存一个字段）、基线视图的校验与「只在**叶子**上把相同 / 不同细化为两侧均改 / 冲突」、父子一致的商品汇总结论 `aggregateChildren()`（引擎与用例调用**同一份**，否则「父子视图结论一致」会退化成「两套口径碰巧今天相同」）、「为什么是这个状态」的三节理由 `statusReasonLines()` 与不可能组合自检 `statusModelViolations()`。`Entry::partialComparison` 也从布尔字段改成了 `contentEvidence == Partial` 的视图——同一件事不再有两份说法。**递归子目录策略**拆在 `recursionstrategy.{h,cpp}` 里（DIR-003）：三档表 `recursionTierTable()`（仅根目录直属条目 / 递归深度 1 / 完全递归）与自检 `validateRecursionTierTable(table)`，档位 ↔ `Options` 的双向映射 `applyRecursionTier()` / `recursionTierOf()`（后者按**行为**归类，`kDefaultFullDepth`、`kMaximumRecursionDepth`、`kDirectChildrenDepth` 三个常量与 `Options` 的初值同源），深度边界上唯一一份解释文案 `recursionBoundaryExplanation(options)`（引擎与用例共用，印的是**实际生效**的上限），以及循环符号链接的判据 `linkTargetReentersAncestor()` 与文案 `linkCycleExplanation()`。**注意这一档没有新增 `Options` 字段**：`(recursive, maximumDepth)` 的组合已经能表达全部三档，理由见 §4。**条目状态的配色与图标**拆在 `statuspalette.{h,cpp}` 里（DIR-012）：三套配色（`default` / `high-contrast` / `color-blind-safe`）各带**深浅两套**色值、两个参考背景与一个**自报的对比度门槛**（出厂方案与 `default` 都是 AA = 4.5，高对比是 AAA = 7.0），一张表 `colorSchemeTable()` 加一个**把表当参数**的校验函数 `validateColorSchemeTable(table)`（标识符可机器读、深浅背景必须**明暗相反**、每一档状态**恰好出现一次**、弱化色要可读且不得等于「相同」、方案自报的门槛必须真的达标、跨方案的「至少三套 / 第一套是出厂方案 / 标识符唯一 / 必须有一套装自报色盲友好」也要查）；`relativeLuminance()` / `contrastRatio()` 按 WCAG 相对亮度算，无效输入返回 `-1`（`-1` 与「对比度很低」是两件事，混起来会把「颜色写错」读成「颜色不好看」）；`colorBlindSeparation()` 走 Viénot/Brettel 的线性 RGB → LMS → 替换缺失锥细胞 → 回 RGB，返回方案内两两之间的**最小模拟距离**，而 `colorBlindFriendly` 是**要被守的判据**而不是装饰标签——自称色盲友好的方案过不了 `kMinimumColorBlindSeparation` 会让校验报错（实测：出厂方案那对「看起来差很远」的 `#2055a0` / `#5a3fa0` 在绿色盲模拟之后只差 **2.8/255**，色盲友好方案是 **26.2 / 23.2**）；色值一律用 `#rrggbb` **字符串**而不是 `QColor`（服务层因此不依赖 QtGui，`Tests/StatusPalette` 是 `QT -= gui` 的）；标识符查表**取不到就回退到出厂方案**，绝不返回一个空方案；配色可以导出 / 导入（`.lqcolors`），导入**复用同一个校验函数**并在失败时**一个字段都不改** | `folder.pri` |
| （夜间新增的其余服务模块） | `Merge/`、`Special/`、`Table/`、`Sync/`、`Snapshot/`、`Version/`、`Media/`、`Registry/`、`FolderMerge/`、`Archive/`、`Cli/`、`Script/`、`Settings/` | 各专用类型后端、目录同步与快照、命令行与脚本、设置存储等。**逐条的公开接口与未覆盖范围见各自的 `docs/development/team-*.md`** | 各目录下的 `.pri` |

### 3.4 Files/ 的六段式结构

`Services/Files/` 刻意拆成六段，对应三种「可验证程度」不同的代码：

| 这一段 | 内容 | 在哪能被验证 |
| --- | --- | --- |
| `filesystem.h/.cpp` | 接口、`FileTime`、错误分类与映射（含 Win32 与 Cocoa 两套常量）、`ErrorCode`（分类 + 原始系统码）、平台工厂 | 任意平台（纯逻辑） |
| `pathutils.h/.cpp` | 路径规则（分隔符、`.与..`、盘符、UNC、长路径前缀）与文件名校验（问题分类 + 原因文案） | **任意平台**——Windows 规则也在这里被真实执行 |
| `pathname.h/.cpp` | 名称的字节保真（无效 UTF-8 字节不丢）、Unicode 组合形式规范化、安全显示 | **任意平台**——包括只在 Linux 上才会出现的输入 |
| `trash.h/.cpp` | 回收站：可用性状态、删除前的决策与文案、删除报告、XDG 路径与 `.trashinfo` 格式 | **任意平台**——Linux 的回收站规则也在这里被真实执行 |
| `batch.h/.cpp` | 批量操作：失败清单按错误分类归并、逐条建议与原始码、「重试失败项」与「跳过并继续」两条出路、进度回调 | **任意平台**（纯逻辑 + 可注入的假文件系统） |
| `fileopsoptions.h/.cpp` | 文件操作的**默认行为**：删除方式 / 覆盖策略 / 保留的三项元数据 / 大文件与批量删除两个确认阈值 / 覆盖后的校验方式；键表（唯一事实来源）、标识符与枚举的互转、「目标较新」的专门提示、「默认保守」契约的自检 | **任意平台**（纯数据；`Tests/FileOpsOptions` 因此可以 `QT -= gui`，且有一条源码级护栏钉住它不读文件、不认识设置仓库） |
| `filesystem_<平台>.cpp`、`trash_<平台>.<ext>` | 真正调用 `lstat`/`FindFirstFileW`/`NSFileManager`/`SHFileOperation` 的薄层 | 只有对应平台 |

这样分的原因是一条踩过的教训：写在 `#ifdef Q_OS_WIN` 里的逻辑在开发机（macOS）
上一次都不会执行，等拿到 Windows 上才第一次运行。把规则抽成接受 `Style` 参数的
纯函数之后，Windows 的路径规则可以在 macOS 上被单元测试覆盖；同理，
把 XDG 的「该用哪个废纸篓目录」与 `.trashinfo` 的编码格式抽成纯函数之后，
Linux 的回收站规则也能在 macOS 上被覆盖；把 UTF-8 校验与字节保真编码抽出来之后，
**「无效 UTF-8 文件名」这种只在 Linux 上出现的输入也能在任意平台被测试**。

平台错误码常量则在对应平台编译时用 `static_assert` 与系统头文件比对
（Win32 对 `<windows.h>`、Cocoa 对 `<Foundation/Foundation.h>`）。
这条护栏在 PLAT-003 里立刻见效了：`NSFileManagerUnmountBusyError` 一度被写成
768，而真实值是 769，768 是含义完全不同的 `NSFileManagerUnmountUnknownError`——
错误在编译期就被拦下，没有留到运行期变成一次误判。

剩下的系统调用薄层无法用这个办法规避，只能靠 PLAT-010 的双平台测试矩阵。
但 `trash_mac.mm` 是个例外：macOS 就是本机，因此它的往返行为
（真的移进废纸篓、再真的还原回来）是被真实执行过的，不是「写了但没跑过」。

### 3.5 Platform/ 的两段式结构

`Services/Platform/` 与 `Files/` 是同一套思路，只是规模小一档，因此是**两段**：

| 这一段 | 内容 | 在哪能被验证 |
| --- | --- | --- |
| `iconkey.{h,cpp}` | 从路径推出「缓存键」（扩展名 + 是否目录）、键的合成与解析、DPI 缩放后的像素尺寸、Windows 的四个图标档位常量 | **任意平台**（纯逻辑） |
| `iconcache.{h,cpp}` | 有界 LRU 图标缓存、请求去重队列、命中统计 | **任意平台**（纯逻辑 + 可注入的假 payload） |
| `iconservice.{h,cpp}` | 服务：同步取图 / 异步去重解析 / 缓存回查 / 回退、后台线程池、提供者接口 | **任意平台**（提供者是可替换的） |
| `registrystore.{h,cpp}` | 注册表抽象：键/值模型、值类型（含认不出的类型原样保留）、键名归一、内嵌的内存实现与故障注入 | **任意平台**（纯逻辑 + 内存实现） |
| `shellintegration.{h,cpp}` | Shell 集成：菜单项与文件关联的注册表**计划**、命令行引号与拼装、安装（含回滚）、卸载、校验、残留检查、命令行解析 | **任意平台**（计划与流程都在内存存储上真实执行） |
| `instanceprotocol.{h,cpp}` | 单实例的**数据与判定**：端点名与共享内存键的合成与净化（含长度上界、按用户分隔）、线协议（魔数 / 类型 / 版本 / 长度前缀）的编解码与分帧判定、转发结局到退出码的表、命令行开关表与优先级、窗口置前判定 | **任意平台**（纯逻辑，只依赖 QtCore） |
| `singleinstance.{h,cpp}` | 单实例的**动作**：进程锁选主（锁文件 + GUI 唯一性标识）、共享内存占标识、`QLocalServer`/`QLocalSocket` 收参数、总截止时间内的转交与超时降级、崩溃遗留标识的回收、资源释放顺序 | 需要 QtNetwork；**当前执行环境里共享内存这一条腿不可用，实际走的是进程锁 + 本地套接字**，详见下面那段 |
| `iconservice_<平台>.<ext>`、`registrystore_<平台>.<ext>` | 真正调用 `NSWorkspace` / `SHGetFileInfoW` / `RegCreateKeyExW` 的薄层 | 只有对应平台 |

**单实例这一块（PLAT-006）把「抽规则」用到了另一个方向**：抽出来的不是平台差异，
而是**跨进程的标识与线协议**。理由与 `Files/` 那次一样——四件事都写错了不会崩，
只会静默地不工作：名字推错的表现是「第二个实例连不上第一个」或者「同一台机器上
第二个登录的用户被吞掉」；分帧写错的表现是「本地套接字上大多数时候正常，
换一台机器才暴露」；退出码越界的表现是「脚本以为比对跑过了且没有差异」；
置前判错的表现是用户正在打字、焦点被抢走。抽进 `instanceprotocol` 之后，
它们连同畸形输入一起在单进程里被穷举，`singleinstance` 只剩动作。

**但标识这件事有两条腿，而本机只验证得动其中一条。** `QSharedMemory` 用来做
「我是唯一那个」的标识；它在系统资源耗尽或不可用时返回失败，此时退回到
**进程锁（`QLockFile`）+ 本地套接字**：选主交给锁、转交照旧走套接字，
启动报告里如实写出「共享内存不可用（…），已由进程锁保证单实例」。
本机（含本次执行环境）的实测结论是**共享内存这条腿跑不起来**：裸 `shmget()`
返回 `ENOMEM`、Qt 报 `OutOfResources`，所以 `recoverStaleIdentifier()`
（只在「标识已存在」分支里被调用）在本机**不可达**，它的用例在本机是
「跳过式通过」。这是「写了但本机跑不到」，不是「测过了」——接手时不要把它
当成已验证。

**这个模块是本仓库第一次出现「服务层依赖 QtGui」**。`Services/` 其余部分都只依赖
QtCore，`platform.pri` 里那句 `QT += gui` 是唯一的例外，理由写在文件头：
图标本身就是 `QIcon`/`QImage`，绕开 QtGui 只能自己实现一套像素缓冲与绘制，
那是把 Qt 已经做好并且测过的事重做一遍。
护栏 `check_layering.py` 查的是**依赖方向**（Services 不得反向依赖 Views/App），
不查「用了哪个 Qt 模块」，所以这条例外不需要改动护栏——
但它是刻意留下的一条记录：后续往 `Services/` 里加模块时不要顺手也 `QT += widgets`。

**payload 是不透明的 `QVariant`**，这是让上面两行「任意平台」成立的关键：
生产环境往里放 `QIcon`，测试里放一个 `QString`。于是缓存淘汰顺序、去重、
命中统计这些**最容易写错**的逻辑，可以在只链接 QtCore 的测试套件里完整覆盖，
不需要一个能跑图形栈的环境，也不需要真的拿到系统图标。
代价是 `IconEntry::usable()` 只能看「这一格有没有东西」，看不出那东西是不是
一张能用的图——所以 `Tests/PlatformIcon` 里另有三条**走真实系统图标源**的用例
（在 macOS 上真实执行，不跳过），把「拿到的是不是像素」这件事钉住。

#### 3.5.1 抽「存储」而不是抽「能力」

`registrystore.{h,cpp}` 是这一层里唯一不遵循「把平台规则抽成纯函数」套路的部分，
因为注册表这边值得抽的东西不是规则，而是**一个可替换的键值存储**。

理由：Shell 集成里最容易写错的三件事——
「写哪些键才能出现菜单项」「卸载怎么把用户原本的关联还原回去」「装了一半怎么收拾」——
与 `RegCreateKeyExW` 怎么调**完全无关**。它们只依赖「能读、能写、能列举子键」这三件事。
把这三件事抽象成一个接口之后，`shellintegration.cpp` 连同它的 101 条用例
可以在 macOS 上被真实执行，包括安装中途失败时的回滚。

于是 `Tests/ShellIntegration` 里跑的不是「假装成注册表的一个桩」，
而是 `MemoryRegistryStore`——一个**功能完整**的实现（键的创建/删除、
值的读写与列举、键名大小写不敏感、可以按需注入某次写入失败）。
它自己也参与生产代码：`previewInstall()` 用它做预演，
在真正动注册表之前算出「这次安装会覆盖你现有的 `.patch` 关联」这类结论。

对应的代价与它换来的一致性写在 §4 的两条决策里（值类型认不出就原样保留、
归一仅用于比较而存储保留原拼法）。

### 3.6 Filter/ 的六段式结构

`Services/Filter/` 与 `Platform/` 同一套思路，是**分段**的，每一段回答一个独立的问题。
其中**五段**只依赖 QtCore（输入输出都是字符串或字节，不碰文件系统、不碰图形、不碰平台 API；
`namefilter` 另用 QtCore 的线程原语做超时保护，`contentfilter` 只处理**已经被读进来**的
行与字节），只有 `filterstack` 多依赖一个 `Services/Session` 的**存储抽象**（一个纯 QtCore 的接口）：

| 这一段 | 内容 | 在哪能被验证 |
| --- | --- | --- |
| `mask.{h,cpp}` | 掩码语言：`*` / `?` / `[...]`（含取反与区间）/ `**`（整段时跨目录）的解析与匹配、`\` 转义、按「名字」还是「按相对路径」匹配的判定、语法速查表（数据 + 纯文本） | **任意平台**（纯逻辑） |
| `maskfilter.{h,cpp}` | 过滤声明：多行掩码的切分、`-` 排除与 `#` 注释、包含/排除的叠加（排除优先）、大小写策略（平台默认 + 显式覆盖）、预览计数 | **任意平台**（纯逻辑） |
| `attributefilter.{h,cpp}` | 属性条件：大小范围（二进制单位）、修改时间范围（绝对 / 相对「最近 N 天」）、属性位（只读 / 隐藏 / 系统 / 归档）、所有者与组；条目的**元数据快照** `EntryMetadata`；条件表自检；`decideEntry()` 把名称侧与属性侧的结论合成为**与** | **任意平台**（纯逻辑；`Tests/AttributeFilter` 因此可以 `QT -= gui`） |
| `filterstack.{h,cpp}` | 三层叠加（格式 / 会话 / 视图）与各层的落点：层间**取交集**、每层的启用/禁用与生效状态、合并表达式、面板数据、`FilterLayerBinder` 的存储路由、层级表自检 | **任意平台**（纯逻辑 + `Services/Session` 的 `SessionSettings` 接口） |
| `namefilter.{h,cpp}` | 名称过滤：精确名 / 通配符 / 正则三种模式（模式前缀是行首的 `=` / 无 / `re:`）、包含任一 / 不包含任何 / 全部满足三种组合语义、带位置信息的实时校验、正则的**静态回溯预检 + 运行期截止时间 + 断路器**三层防护、具名预设的导出/导入（`NameFilter` / `NameMatchRunner`） | **任意平台**（纯逻辑 + QtCore 的线程原语；`Tests/NameFilter` 因此可以 `QT -= gui`） |
| `contentfilter.{h,cpp}` | 内容过滤：**行过滤器**（整行 / 通配 / 正则三种模式、缩进无关的行首前缀、逐行诊断与打回、命中计数、`LineFilterResult` 把留下的行与被丢掉的行一起交回）与**关键字节过滤**（`\xHH` 转义解码 / 编码、任一 / 全部两种组合方式、文本输入判「不适用」、空规则集不构成约束）两条轴；两条轴与忽略规则的先后顺序表；显式启用与性能提示的文案。**输入永远是「已经读进来的行或字节」**，模块自己不碰文件系统 | **任意平台**（纯逻辑；`Tests/ContentFilter` 因此可以 `QT -= gui`） |

**六段刻意分开**，因为它们回答的是六个问题、失败也是六类：`mask.h` 回答「这一段
掩码命中这个条目吗」（失败是「掩码写错了」，所以错误带列号与长度）；`maskfilter.h`
回答「整个声明叠加之后这个条目留不留」（失败是「规则组合得不对」，界面要在状态栏
说明排除优先）；`attributefilter.h` 回答「这个条目的**元数据**满足全部属性条件吗」
（失败是「条件本身写错了」或「元数据还没读到」——两者处置不同：前者要标红输入框，
后者要**放行**并在状态栏说明「结论不确定」）；`filterstack.h` 回答「三个来源一起看
一个条目，结论是什么」（失败是**路由**问题——哪一层的声明存到了哪儿）；
`namefilter.h` 回答「一组高级表达式在某种组合语义下放不放行」（失败是**正则**
的问题——语法错、可能回溯爆炸、或已经跑超时，三者的处置各不相同）；
`contentfilter.h` 回答「这段**内容**里有哪些行/这一份字节该不该留下」（失败是
「代价」问题——它必须把文件读进来，因此它比前五段多一份「值不值得读」的判断，
而那个判断不能藏在实现里，必须是一个显式的启用开关）。
合成一个返回值，界面就没法分别显示。

**`contentfilter` 为什么不能与前五段压缩在一起**（这一段是本模块唯一**读文件内容**的，
它的取舍与其余五段相反）：`maskfilter` / `attributefilter` / `namefilter` 都只看
**元数据与名字**，因此在目录扫描阶段就能把条目丢掉，代价接近零；`contentfilter`
必须先把内容读进来，代价与文件大小同阶。这个差别决定了两件事，都需要有自己的
位置来表达：一是它**默认不启用**（`ContentFilterEnablement` 的 `active()` 是
「启用**且**有规则」，与 FILT-005 的 `FilterLayerState::active()` 同形），二是
「说不清的结论」在这里的含义不同——名字对不上就是不留，而**字节读不出来/文件是
文本而配的是关键字节规则**这种事必须报出来并放行（`KeyByteOutcome::NotApplicable`）。
把它并进 `namefilter` 会把这两条都变成隐藏行为。

**`namefilter` 与 `maskfilter` 都看名字，为什么不合成一个类**：需求分属两个条目
（FILT-002 / FILT-001），界面上是两个不同的输入框——`maskfilter` 是「写一堆路径
规则再靠 `-` 排除」，`namefilter` 是「写几条高级匹配再选一种组合语义」。合成的
代价是任何一处界面的改动都会波及另一处，而收益只是少一个类。两者与属性过滤之间
仍然是**与**的关系，最终合成入口依旧在 `decideEntry()`；`namefilter` 目前不参与
那个合成（FILT-002 的完成标准里没有这一条），因此没有擅自改动 `decideEntry()` 的
签名。

**⚠ 两套「三层」是同形不同义的，这是本模块最容易踩坏的一处。** 架构里同时存在：

| 三层 | 关系 | 含义 | 出处 |
| --- | --- | --- | --- |
| `SettingScope`：视图 / 会话 / 类型 | **覆盖** | 读的时候只有一个胜出（视图 > 会话 > 类型），下面几层被遮住 | SESS-007 |
| `FilterLayer`：格式 / 会话 / 视图 | **叠加** | 全部生效，一个条目必须被每一条生效的层放行 | FILT-005 |

于是有一个看起来非常自然、实际会**静默丢掉一层过滤**的写法：

```cpp
// 错：value() 只会返回优先级最高（视图层）那一条，会话层的过滤从此消失
stack.setDeclaration(FilterLayer::Session,
                     sessionWideSettings.value("filter-declaration").toString());
```

正确做法是**按存储分别读**（`FilterLayerBinder::loadInto()` 是唯一实现）：
会话层从会话存储读、视图层从视图存储读，两份都装进同一个栈。`Tests/FilterStack`
里有一条用例同时断言「作用域链确实只给一条」与「绑定器把三层都装进了栈」，
把这个差别钉死——理由与后果写在 `filterstack.cpp` 顶部。

**层与层之间取交集**（FILT-005 的边界条款要求明确并集还是交集）。取并集的意思是
「任一层放行即可见」，于是新增一层过滤会让结果**变多**——用户加一个「只看 `.cpp`」，
反而看到了原本被排除的文件，而他只会把这件事理解成「过滤器坏了」。交集保证
「加一层只会更窄」，与所有带多级过滤的软件的共同直觉一致。

**一层的落点是数据，不是散落的调用点约定**（FILT-005 第 4 条）。`filterLayerStorage()`
给出每一层的落点（格式层只读、会话层随会话保存、**视图层仅当前视图**），
`FilterLayerBinder::saveLayer()` 按它取存储，因此 `saveLayer(View, …)`
**只可能**写到视图存储——「临时过滤不小心存成永久过滤」在结构上不可能发生。
视图存储由视图持有并随之销毁，所以「关闭标签即丢弃」是所有权带来的结论，
而不是一段需要记得执行的清理代码。

**层级表是唯一的事实来源，且带一条启动自检。** 顺序、下标与落点全部由
`filterLayerTable()` 推导，`validateFilterLayerTable()` 在启动时跑一遍并写日志。
自检**参数化**（表当参数传进来），因此测试可以拿一份故意写坏的表跑同一个判定，
证明它真的会报——一条永远不会红的护栏比没有护栏更糟。其中「视图层的落点是否仍然是
仅当前视图」单独占一条，因为它是本条目里最贵的一处错误：改成随会话保存之后，
过滤照样工作，只是关掉标签它还在，没有任何运行期现象会提示这件事。

**掩码里的分隔符恒为 `/`，与平台无关。** 这是本模块最容易被误改的一条约定，
理由写在 `mask.cpp` 顶部：掩码是**用户写下来的意图**，而预设库（FILT-007）
要能导出给团队共享——让分隔符跟着平台走，同一份预设在两台机器上就不是同一套
语法了。附带的好处是 Windows 用户把 `build\out` 写进掩码时，我们能把
「反斜杠后面跟了一个不能转义的字符」当成明确的错误报出来并建议改写成 `/`，
而不是静默解释成 `buildout`。也正因为 `/` 永远先被切走，`*` / `?` / `[...]`
都不可能把分隔符吃掉——这是结构性保证，不靠实现里的小心。

**「按名字匹配」还是「按相对路径匹配」由掩码自己决定**（含不含 `/`）。
于是 `*.txt` 在任意目录下都命中，而 `src/*.txt` 只从起点匹配——后者不这样做的
话，废弃目录里的同名子目录会被一起过滤掉，用户看不出差别在哪。

**语法速查表是数据，不是文档。** FILT-011 明确要求「文档中的示例掩码与测试语料
中的用例一致（用同一个数据源生成）」。手写一份 markdown 表格做不到这一点，
所以 `maskSyntaxReference()` 返回带可执行样本的条目，`Tests/Filter` 会把每条样本
真的跑一遍——实现改了而速查表没改会立刻红。帮助页与本文档的表格都从
`maskSyntaxReferenceText()` 生成。

**属性过滤按元数据判定，与内容比对解耦**（FILT-003 第 5 条要的是「属性过滤不能变成
一种内容过滤」）。`attributefilter.{h,cpp}` 里没有任何读文件的代码——输入是
`EntryMetadata`（名字、相对路径、大小、修改时间、属性位、所有者/组），输出是一个
结论。这条约束不是靠自觉：`Tests/AttributeFilter` 会**读源码**，一旦
`attributefilter.{h,cpp}` 里出现 `QFile` / `readAll` / `QTextStream` / `QDataStream`
就报红。这样做的现实理由是性能——属性过滤要能在扫描阶段就**提前**丢弃条目
（不然「只看 10 MB 以上的文件」还得把每个小文件读一遍才能排除），而扫描器一旦
和内容读取纠缠在一起，这条捷径就再也加不回去了。

**元数据缺失一律放行，并且如实报「结论不确定」。** 这是一个三态结论而不是两态：
`ConditionOutcome` 同时带 `accepted`（放行/拦下）与 `evaluated`（到底判没判过）。
大小还不知道时不能说「不满足 10 MB 以上」，只能说「不知道」——把不知道当成不满足，
会让一个还没读完元数据的条目凭空消失，而用户看到的只是「文件不见了」。同理，
**写错的条件永远不缩小结果集**（大小写成 `abc` 时不拦任何东西），但一定被报出来，
否则「过滤器看起来生效了、只是过滤错了」比报错难查得多。`decideEntry()` 把名称侧
与属性侧的结论合成为**与**：任何一侧说「不过」就不留，两侧都说「不确定」才算不确定。

**「现在」由调用方给，模块自己从不取当前时间。** `TimeCondition::referenceTime()`
是显式输入。原因是「最近 7 天」这类相对窗口在测试里必须可复现——如果模块内部调
`QDateTime::currentDateTime()`，用例就只能靠「跑得足够快」来避免跨秒失败，
而那种失败是偶发的、且看起来像别的问题。日期为主的**上限**要算到当天最后一刻
（`2026-09-10` 是含 9 月 10 日一整天），并且用同一天的 `23:59:59` 构造而不是
加 86399 秒——后者在夏令时切换的那一天会跨错一个小时。

### 3.7 Session/ 的两层四块结构

会话框架是**唯一一处「一个功能域同时落在两个层」**的地方，四段的理由分开写：

| 这一段 | 内容 | 在哪能被验证 |
| --- | --- | --- |
| `Services/Session/session.{h,cpp}` | 会话设置的抽象接口 `SessionSettings`、一个内存实现 `MemorySessionSettings`，以及「值没变就不算改动」这三条入口约定 | **任意平台**（纯逻辑，只依赖 QtCore） |
| `Services/Session/sessiontype.{h,cpp}` | 会话类型描述子 `SessionType` 与注册表 `SessionTypeRegistry`：14 种内置类型的字段、ID 稳定性校验、按掩码的注册顺序优先查询、按分组的枚举 | **任意平台**（纯逻辑，只依赖 QtCore） |
| `Services/Session/settingschema.{h,cpp}` | 设置项的声明（`SettingItem` 的标题 / 说明 / 控件类型 / 默认值 / 校验规则）、草稿（`SettingsDraft`：读写、脏判定、校验、全有或全无的应用、恢复默认）、未保存改动的询问策略（`inquiryForUnsavedChanges`）、声明目录（`SessionSettingsCatalog`） | **任意平台**（纯逻辑，只依赖 QtCore 与 `Services/Filter` 的掩码解析器；`Tests/Settings` 因此可以 `QT -= gui`） |
| `Services/Session/settingscope.{h,cpp}` | 三层作用域的覆盖链与写入路由（`ScopedSessionSettings`）：按 视图 → 会话 → 类型 → 出厂默认 解析；写入只落 `writeScope()` 指定的那一层；`writeDestinationText()` / `scopeSwitchNotice()` / `planViewScopeClose()` 三条可测文案 | **任意平台**（纯逻辑，只依赖 QtCore；`Tests/SettingsScope` 同样 `QT -= gui`） |
| `Views/Session/comparesession.{h,cpp}` | 会话抽象基类：`createWidget()` 的模板方法、`open/reload/save/close` 的状态机、三个公共出口、`SessionError` / `SessionProgress` 两个结构化载体 | **任意平台**（跑在 QtWidgets + offscreen 上，本机与 CI 都真实执行） |
| `Views/Session/settingsdialog.{h,cpp}` | 会话设置对话框的外壳：把声明画成控件、把用户的操作翻译成对草稿的读写、把草稿的结论翻译成按钮的可用状态与错误行 | **任意平台**（`Tests/SettingsDialog`，offscreen 平台） |

**为什么设置接口和设置声明都在服务层、只有对话框在视图层**：基类要交出视图，所以只能在 `Views/`；
而「某一项设置当前是什么值」「这个类型有哪些设置项」都是纯数据问题——作用域覆盖链（SESS-007）、
预设库、以及**服务层**的 FILT-005（「视图临时过滤不写入会话」需要一个会话级的设置容器）都要读它。
接口如果长在 `Views/` 里，服务层要用就只能反向依赖界面，而那正是 §2 那条铁律禁止的方向。
两个模块之间只有两处连接：`comparesession.h` 与 `settingsdialog.h` 引用 `Services/Session/`，
方向都是 Views → Services。SESS-006 的这条划分在测试上立刻兑现：声明、草稿、校验与询问策略
（第 2、3、4 条）全部落在 `Tests/Settings` 里用**纯 QtCore** 覆盖，界面那一层只剩
「控件生成、按钮接线、询问的界面行为」，由 `Tests/SettingsDialog` 覆盖。

**「任一会话设置 Tab 均可在无界面测试中被单独构造与读写」因此不是一句口号**：`Tests/Settings`
刻意写 `QT -= gui`（`settingschema.cpp` 一旦引入 QtGui，该工程直接构建失败），并且有一条
逐控件的数据驱动用例，把六种控件类型各走一遍「造声明 → 造草稿 → 读默认 → 写新值 → 应用 →
目标里拿到归一后的值」。

**会话基类必须有测试能真的跑起来**。它是本仓库第一个链接 QtWidgets 的模块，
`Tests/Session` 也因此是第一个链接 QtWidgets 的测试套件——运行器本来就统一
导出 `QT_QPA_PLATFORM=offscreen`，所以这一点没有额外的机制，只是第一次用上。

**SESS-007 之后，`Services/Session/` 里多了一层「合成」的角色**：
`ScopedSessionSettings` 不自己存值，它把三层存储（视图 / 会话 / 类型）串成一条链，
再拿声明里的 `SettingItem::defaultValue` 当链尾。这条链的**读写方向是刻意不对称的**：

| 方向 | 行为 | 为什么 |
| --- | --- | --- |
| 读 | 视图 → 会话 → 类型 → 出厂默认，逐层问「这一层有没有显式设过」，首个命中即返回 | 第 4 条点名的覆盖链；用 `contains()` 而不是「值是不是空的」判断命中，否则用户清空一项之后会被下面那层的值悄悄顶回来 |
| 写 | 只落到 `writeScope()` 指定的那一层，**绝不碰**另外两层 | 边界条款「视图级改动不得污染会话默认值」。写入目标那一层缺失时返回 false，不退而写入别的层——那会让一次「保存成新会话默认值」活不过关标签 |

于是「写入成功」与「用户看得见变化」成了两件事：往会话层写、而视图层已经有一条时，
值确实存进了会话层（用户要的就是这个），但有效值仍是视图层那个。
`resolvedFromLayer()` / `describeResolution()` 就是为这一情形准备的——
第 2 条的「必须能一眼看出当前改动的去向」在代码上的落点就是它们。
代价是本类**不转发**三层的 `changed` 信号，自己按「**有效值**有没有变」发，
理由写在 `settingscope.h` 的类注释里。

**「基类不依赖具体视图」有编译期与源码级两层校验，SESS-006 之后两层的分工变了**：
`SessionTests.pro` 的 INCLUDEPATH 里只有 `Views/Session` 与 `Services/Session` 两个目录，
因此 `comparesession.h` 里多写一行 `#include "homepage.h"` 会让**测试工程直接构建失败**。
主构建发现不了这件事（它的 INCLUDEPATH 里有 `Views/Shell` 等目录），所以另一半
放在 `Tests/Session` 的 F 组用例里：把两个源文件的 `#include "…"` 与**文件名白名单**比对，
并另有一条用例对一段**故意写坏**的源码做反向验证。

SESS-006 把 `settingsdialog.{h,cpp}` 也放进了 `Views/Session/`，于是那条编译期护栏
**不再覆盖同目录内的相互 include**（同目录总是找得到 `settingsdialog.h`）。守住这条
约束的因此是 F 组那份白名单——它是按**文件名**比对的，多一个头文件就报红。
`sessionview.pri` 的注释里写明了这一点，改这个目录之前先读那一段。

**注册表的四处关键取舍**（SESS-002）：

1. **描述子与创建工厂分成两层类型**。`SessionType` 是纯数据，`SessionTypeEntry`
   才是「描述子 + 工厂」。理由不是洁癖：第 2 条完成标准要求「已发布 ID 的字符串值
   被快照断言锁定」，而 `std::function` **不可比较**、也不该出现在快照里——
   混在一起，那张表就没法直接照搬进快照用例。
2. **工厂的返回类型只前向声明，不 include**。`sessiontype` 在服务层，
   `CompareSession` 在界面层，`tools/check_layering.py` 禁止那条 include。
   `class CompareSession;` 加一个 `std::function<CompareSession *(QObject *)>`
   既守住了依赖方向，也保住了类型安全——写成 `QObject *` 再让每个调用点自己转，
   类型安全就丢在每一处创建会话的地方了。直接收益是 `Tests/SessionType`
   可以 `QT -= gui`（纯 QtCore），本仓库第一次有「服务层的会话框架测试」。
3. **注册顺序就是优先级，也同时是 Home 页的展示顺序**。一个顺序服务两个用途是
   刻意的：若是两份，用户会看到「表格比对明明在上面，双击 .html 却打开了文本比对」
   这种无法自洽的界面。代价是内置表里 `*.html` 被两个类型同时声明——
   那处重叠**有意留着**，它是「按注册顺序返回首个命中」这条规则在真实数据上的
   唯一用例（合成的小表只能证明实现自洽，证明不了那张表最终落到什么结论）。
4. **不可用的类型不参与自动选择，但可以枚举出来**。`findByFileMask` 跳过当前平台
   受限的类型（在 macOS 上把 `.exe` 判给「版本比较会话」，用户得到的是一句
   「仅 Windows 可用」，而他本来可以拿到一个十六进制比对）；而 `byGroup(…, false)`
   仍然把它们列出来，因为 REG-001 要求 Home 页把受限入口**置灰并说明原因**——
   只给「可用的那些」，Home 页就只能自己再维护一份「哪些是仅 Windows」的名单。

**「表里那 14 个 ID 与 Home 页硬编码的 ID 是否一致」由一条源码级用例守着**：
`Tests/SessionType` 会去读 `Views/Shell/homepage.cpp`，从 `sections()` 的函数体里
抠出全部 `QStringLiteral("…")` 与注册表比对。这是**过渡期**的护栏——注册表落地前
那张硬编码的表是事实上的第一批「已发布 ID」，而让 HomePage 改成读注册表属于
SESS-003 的范围，本轮改了会与它撞车。另有一条用例对一段**故意写错**的源码跑
同一个判定流程，用来证明这条护栏不是恒真的。

### 3.8 其它目录

| 目录 | 内容 |
| --- | --- |
| `Pictures/` | 图标资源（SVG）与 `Pictures.qrc`。图标由 `tools/generate_icons.py` 生成 |
| `Tests/` | 每个测试套件一个子目录 + 一个 `.pro`；`run-tests.sh` 是统一运行器（**72 个套件 / 3809 条**，2026-09-24），默认**并行 4 个套件同时跑**（`LQCOMPARE_TEST_JOBS`）、单套件**超时 600 秒**（`LQCOMPARE_TEST_TIMEOUT`）、并自带 `run-tests.sh --self-test`（在临时目录里现造**六个**探针套件 + 一个**假调试器**脚本验它自己），断言 **48 条**（其中 2 条专守「拿到调用栈时日志里也贴出帧」——只守产物的话，把那段日志删掉不会有东西变红）。自测会**按遍**（并行 4 / 串行 1）各留一份「证据块」：该遍的合计行、运行器自己报的异常清单、以及每个探针的 `summary.env` 与 `Totals:` 原文——**两遍共用同一个构建目录，不在当场快照就会只剩第二遍的**，而「首遍合计是 0」恰恰是唯一看不到的那一个。**崩溃（没产出 `Totals:` 行）的套件会被自动补跑一遍 `-v2`**，把「跑到哪一条用例才崩」直接写进日志（理由见 §4）。**运行器的产物一律落在磁盘上、不依赖 stdout**：每个套件在 `_test-build/<套件>/` 下留 `results.txt`（纯文本）、`results.xml`（**JUnit**，供 CI 消费）、`build.log`（构建的完整输出，构建失败时唯一的线索）、`stderr.log`（崩溃与告警）与 `suite.log`（该套件在控制台上的整块输出），崩溃的套件另留 `verbose.txt`（补跑的 `-v2` 全文）与 `verbose.stderr.log`；**崩溃的套件还会自动用调试器抓一份 `crash-trace.txt`**——`gdb -batch -ex run -ex bt 40` 或 `lldb --batch -o run -o bt -c 40`，把「哪一行 C++」也变成数据（三条纪律见 §4）。CI 把前四样、后两样与 `crash-trace.txt` 整体上传，并在测试步之前探一次「本平台有没有 gdb / lldb」——没有就只打 `::warning::`、不让那条腿失败，因为「拿不到调用栈」是诊断能力的降级，不是测试结果的降级（见 §4 的相关决策）。`Support/` 放多个套件共用的测试替身（如内存文件系统）。**刻意不链接 QtGui 的套件**（`Tests/Filter`、`Tests/AttributeFilter`、`Tests/FilterStack`、`Tests/NameFilter`、`Tests/ContentFilter`、`Tests/FileOpsOptions`、`Tests/Logging`、`Tests/SessionType`、`Tests/Settings`、`Tests/SettingsScope`、`Tests/PatchApply`、`Tests/Alignment`、`Tests/Similarity`、`Tests/TextRules`、`Tests/CompareConclusion`、`Tests/EntryStatus`、`Tests/StatusPalette`）兼作「服务层不依赖界面」的编译期护栏；**链接 QtWidgets 的套件**（`Tests/Session`、`Tests/SettingsDialog`、`Tests/VcsView`、`Tests/VcsBlameView` 等）跑在 offscreen 平台上。新套件必须自己 `QTEST_MAIN`，否则链接报 `_main` 未定义。**测试产物一律进临时目录**：`Tests/OptionsDialog` 的 `screenshotsNeverLandInTheWorkingDirectory()` 用 `QTemporaryDir` 存对话框截图，并额外断言「工作目录里没有多出任何 `options-*.png`」——只改落点而不盯着，下一个人把路径改回 `QDir::current()` 时不会有任何东西变红。`.gitignore` 里仍留一条 `/options-*.png`，兜住历史上真的落在仓库根的那几个文件。**行对齐（`Services/Text/textdiff.*`）的验收点是「差异块序列」，不是「差异数量」**：`Tests/Text` 除了断言 `differences` 为空之外，还必须断言「有几个块」（块边界就是界面上的分隔线位置）与「把 `Result` 摊成文本的逐字段快照」——这两条在 2026-09-21 之前**都没有**，而算法与套件一直都在：**「模块在不在」判不出「完成标准守没守住」**。**对齐算法那一条（TXT-003）另开 `Tests/Alignment`**，理由与手法：TXT-002 守的是 Myers 自己那几条标准，TXT-003 守的是「换一种算法之后同一个 `Result` 契约还成不成立」，分开之后变异测试只需重建这一个套件（TXT-002 的记录里已经踩过「头文件变异不重建就漏检」）。它的断言骨架是**手推的黄金串 + 逐条契约不变量**：黄金串由纸上枚举锚点与后缀剥离得出（第一次跑就全绿，实现与手推互不相干地得到同一个值），契约不变量则由一个返回「第一条被违反项」的纯函数承担——6 组夹具 × 2 种算法全跑同一个判定，失败时能一眼看出是哪一条在哪种算法上破了。**相似行对齐（TXT-005）再开 `Tests/Similarity`**，判据与手法同前：`Tests/Alignment` 守的是「切完之后块边界对不对」，新套件守的是「切完之后**哪些行配成一对**」，两件事的夹具不重叠，分开之后变异测试只需重建一个套件。它的断言同样是「独立来源的期望值 + 结构不变量」：分值由 `2·LCS/(len左+len右)` 的**定义**在用例里另算一遍（不抄实现里的数），配对结果则用「左右下标各自严格递增」「每一对都 `>= 阈值`」「配对数与总分值都不得被任何更优解压过」三条不变量来判——手推一个具体的最优解容易被「和实现同错」蒙混过关。**内置替换规则（TXT-012）再开 `Tests/TextRules`**（19 条用例函数，同样刻意 `QT -= gui`）：四条规则各自的命中与不命中、单独开关与表顺序、以及一条**刻意构造的重叠语料**（两条规则都想吃同一段文本）——没有它，「按声明顺序依次应用」这句话在结果上不可观察，一个把顺序写反的实现也能让全套用例变绿 |
| `ThirdParty/` | `myclasspath.pri`（定位 LqRibbon）与 `lqribbon.pri`（引入） |

## 4. 关键设计决策

| 决策 | 理由 | 对应规格 |
| --- | --- | --- |
| **命令注册中心是唯一出口** | Ribbon 按钮、菜单、快速访问栏、快捷键、搜索栏全部由注册表驱动；同一命令在多个入口的启用状态必然一致，不会出现「菜单里能点、按钮是灰的」 | UI-024 |
| **Ribbon 布局是声明表，不是代码** | 页面/分组/按钮写成数据，新增按钮不改逻辑；未注册的命令生成占位按钮并标注 ACTION-ID，界面骨架可以先交付 | UI-007 ~ UI-018 |
| **按钮 tooltip 强制两段式** | 缺说明的命令在启动自检里直接报出来，避免 TortoiseGitMerge 那样「一条 tooltip 都没有」 | UI-023 |
| **图标由脚本生成并校验一致性** | 同一语义只用一个图标；孤立图标会导致下一个人「有现成的就直接用」，最终形成第二套视觉语言 | UI-025 / ENG-009 |
| **PRD 是生成物，`tools/spec/` 是数据** | 369 条规格与 369 个 issue 由同一份数据生成，不会漂移；`check_spec.py` 阻止手改生成物 | DOC-005 |
| **交付目录不进 git** | 任何 checkout 都不应该把用户手上的可执行文件打回旧构建 | ENG-011 |
| **日志宏不命名形参为 `level`** | 宏形参参与全宏体令牌替换，会连带替换掉 `LqCompare::Log::level()`，导致编译失败。踩过一次，写在这里避免重犯 | ENG-006 |
| **删除只有一条入口：`TrashService`** | 删除必须可逆。而「撤销最近一次删除」需要一个长期存在的撤销点，`FileSystem` 是无状态的，在那里留便捷转发必然导致撤销点随对象一起丢掉。两个入口还会让「该走哪条路」有第二个事实来源 | PLAT-003 |
| **回收站不可用的判定必须发生在删除之前** | 有些平台在无回收站的位置上不会失败，而是**静默永久删除**（Windows 的 `FOF_ALLOWUNDO` 在网络盘上被忽略、且返回成功）。因此「能不能进回收站」是一个独立的一等查询，而不是从失败里反推 | PLAT-003 |
| **「不可用就不动手」冻结在基类的模板方法里** | 平台实现只写「真正搬移」那一步，可用性检查与撤销点记录写在基类。这样任何平台实现都不可能不小心跳过检查——它的代码根本不在那条路径上。对应测试断言的也是「搬移函数一次都没被调用」，而不只是「返回了失败」 | PLAT-003 |
| **无效 UTF-8 字节用未配对低代理承载，不用 U+FFFD** | `QString::fromUtf8` 把无效字节替换成 U+FFFD 后原始字节**永久丢失**，那个文件从此打不开也删不掉，而失败现象是「找不到文件」，与真正的原因看不出关系。用私存码位（U+DC80..DCFF，与 Python 的 surrogateescape 同源）可以保真往返。代价是**绝不能**用 `toUtf8()` 编码回字节——它会把未配对代理换成 `?`，所以有一条专门的用例把这件事钉住 | PLAT-007 |
| **文件名校验一次算清「问题分类 + 位置」** | 只返回 bool 说不出「哪里错了」，只返回一句文案则界面无法按原因给不同处置（「去掉首尾空格」可以一键修正，「保留设备名」只能让用户改名）。`checkFileName()` 是唯一实现，另外两个函数是它的薄封装——各写一遍会形成两个事实来源，分歧还是静默的 | PLAT-007 |
| **新建时的名称校验取所有平台里最严格的一套** | 本工具经常在 Windows 与 Linux 之间比同一份文件树。放行一个 Linux 合法但 Windows 非法的名字（如 `a:b.txt`），要等那份文件树同步到 Windows 上才失败，那时已追不到源头。代价是 Linux 上少用几个字符，收益是跨平台行为一致 | PLAT-007 |
| **显示与操作用两套名字** | 换行、制表、首尾空格在列表里看不见，用户会在「看起来一样」的两个名字里挑错。`forDisplay()` 把它们标出来（`\n`、`\xNN`、首尾空格用 `·`），而读回与写回一律用原始名字——转义只发生在显示这一层 | PLAT-007 |
| **错误出参是「分类 + 原始系统码」，不是一个枚举** | PLAT-008 第 5 条要求错误信息带原始码便于排查。原始码必须在系统调用出错的那一刻被记下，之后任何地方都补不回来——所以它得和分类在同一个结构体里一起从底层传出来。为了让这次改造不必推倒上百处调用点，`ErrorCode` 提供了与 `FileSystemError` 双向的隐式转换（`*error = FileSystemError::None` 与 `error == FileSystemError::Busy` 都继续有效），但**只有** `from*Error()` 是正确出口，`classify*()` 只回答分类 | PLAT-008 |
| **分类用于「决定怎么办」，原始码用于「查清为什么」** | 分类是有损的：Windows 把「文件被占用」映射成 `EACCES`，只按 errno 归类会给用户「请提权」这种完全错误的建议。同一分类下原因也可能不同：`EPERM`(1) 常是不可变标志或安全模块拦截，`EACCES`(13) 才是 `chmod` 能解决的。原始码让用户能搜到系统官方解释、能贴进工单 | PLAT-008 |
| **认不出的原始码只给数字，绝不编名字** | `rawErrorName()` 对不认识的取值返回 `nullptr`，界面退化成 `Win32 1234`。拼一个 `UNKNOWN_1234` 那样的"名字"会让用户拿一个不存在的符号去搜——比只给数字更误导。同理，原始码为 0（成功）时 `errorDetail()` 返回空串，而不是显示「原始错误码：（无）」 | PLAT-008 |
| **批量操作的进度放在执行器对象上，不做成自由函数** | 长任务的重试必须知道「哪些条目已经成功」。自由函数不得不在每次重试时把上一次的报告传回来，于是这份状态有了第二个事实来源；一旦调用方算错，已经成功的文件会被再做一遍（对批量复制就是覆盖用户刚确认过的结果）。把 `m_report` 收在 `BatchOperation` 里，这类错误在结构上就不可能发生 | PLAT-008 |
| **默认「跳过并继续」，但策略是显式的** | 两种选择在真实场景里都成立且理由相反：批量改属性、同步一批文件就属于「第 7 个被占用不该影响第 8 个」；而有序批次（先建目录再放文件）里继续做只会产出 200 条失败清单、其中真正的原因只有第 1 条。写死任何一种都会让另一类场景拿到错误的现场，所以默认按 PLAT-008 第 4 条取 `SkipAndContinue`，依赖型批次由调用方主动声明 | PLAT-008 |
| **`retryFailed()` 返回整批的当前状态，而不是「只含被重试的那几条」** | 后者会让界面上的计数器从「完成 8/10」退回「完成 2/2」，用户以为前面 8 个白做了——这正是第 4 条「保持已完成的进度」在重试路径上的含义。对应地，`attempts` 是累加而不是覆盖，用户要看到的是「一共试了几次」 | PLAT-008 |
| **失败清单按错误分类归并，而不是平铺路径** | 同一类错误的处置动作只有一个。200 条失败里 180 条是「被占用」、20 条是「无权限」时，平铺清单要用户做 200 次决策；分组后只需两次。分组也让「重试失败项」有了明确范围——只重试 `isRetryable()` 为真的那一组，权限类重试一百次结果一样，只会让用户以为程序卡住 | PLAT-008 |
| **批量输入先去重** | 同一路径在一次批量里做两遍没有意义（第二遍看到的是第一遍的结果），而留着重复项会让失败清单出现两行相同的路径，用户会以为程序写错了 | PLAT-008 |
| **图标缓存键是「扩展名 + 是否目录」，不是文件路径** | 按路径缓存等于没缓存：一个几千文件的目录会占几千格，其中绝大多数指向同一张图，而系统的图标解析恰恰是这里最贵的一步。按类型缓存后格数上界是「出现过的扩展名数」。**「是否目录」必须进键**：`notes.txt` 这个**目录**与 `a.txt` 这个文件要拿到不同的图标，只按扩展名做键会让目录显示成文本文件图标——而这类错误只在「目录名带扩展名」时出现，很难复现 | PLAT-004 |
| **图标请求按缓存键去重** | 一次解析的代价与「多少个文件引用这个类型」无关，却与「多少个文件」成正比。滚动一个全是 `.cpp` 的目录时不去重就是几百次系统调用换回同一张图。去重把在途请求数从文件数压到扩展名数 | PLAT-004 |
| **缓存命中时同步发信号，未命中才走后台** | 一律异步会让滚动时可见闪烁（先在屏幕上放一个占位，几十毫秒后换成真图标）。命中率高的目录里绝大多数条目能同步拿到图，这时直接给结果，界面一次成型 | PLAT-004 |
| **信号按「缓存键」发，不按文件路径发** | 一次解析产出的是**一个类型**的图标，可能同时有 200 个列表项在等它。按路径发要在服务里维护「哪些路径引用了这个键」，即第二份事实来源，且随视图增删要同步维护。按发键、视图收到后自己按需重查（哈希查找，O(1)），服务侧的状态只有缓存本身 | PLAT-004 |
| **缓存与调度逻辑用不透明的 `QVariant` 传图标** | 让这批最容易写错的逻辑（淘汰顺序、去重、命中统计）能在只链接 QtCore 的测试套件里完整覆盖，不需要图形环境。代价是 `usable()` 只回答「有没有」；真实图标源因此另有三条走真机的用例 | PLAT-004 |
| **缓存不用 `QCache`，自己写有界 LRU** | `QCache` 的淘汰策略没有对外契约（文档只说「某条策略」），拿它做「缓存上限 256 个类型」时，淘汰顺序不可预测、也无法写测试。图标缓存的总量本来就不大（上界是扩展名数），自己写一份带 `keysByRecency()` 的 LRU 只多几十行，换来可断言的淘汰行为 | PLAT-004 |
| **Windows 侧用 `SHGFI_USEFILEATTRIBUTES`，Linux 侧用 `MatchExtension`** | 两者都是「只按名字问类型，**不碰磁盘**」。默认版本的 `SHGetFileInfo` 会为没有对应文件的名字去实际查找（网络盘上一次卡住几百毫秒、U 盘没插时直接失败），而这里问的本来就只是「`.cpp` 该长什么样」。用不读磁盘的版本之后，枚举一个从未访问过的目录也能立即出图标 | PLAT-004 |
| **Windows 只有离散几档尺寸，`actualPixelSize` 如实报出** | `SHGetFileInfo` 只能给 16/32/48/256 这几档，请求 20 会拿到 32。把一个「接近的」尺寸当成「就是这个尺寸」写进结果，会让调用方以为拿到了精确尺寸，进而在拼高 DPI 的图集时用错比例。如实报出真实尺寸，调用方才有机会自己再缩一次 | PLAT-004 |
| **高分屏按 `devicePixelRatio` 取图，不取完再放大** | 取 16×16 再放大到 32×32 就是模糊的，而图标放大是所有界面问题里最容易被当成「系统就这样」的一个。`iconPixelSize()` 在请求时就乘上比例并向 1..1024 收敛 | PLAT-004 |
| **`IconService` 用专属的单线程 `QThreadPool`** | 共用的全局线程池里，一个耗时任务（枚举大目录、读压缩包）会把所有图标请求排到它后面，界面上表现为「整个列表都不出图标」。专属池把图标解析与其它后台工作隔离，池容量设 1 也顺便保证了提供者实现不必各自考虑并发 | PLAT-004 |
| **`UTType` 的可用性写成 `API_AVAILABLE` 而不是压警告** | 部署目标是 10.13，`UTType` 要 11.0。把 7 条 `-Wunguarded-availability-new` 用 `#pragma` 静音，会连同「调用点漏了 `@available` 检查」一起静音，而那正是会崩溃的情形。`API_AVAILABLE` 把契约写进函数签名：漏检查时报错精确落在调用行 | PLAT-004 |
| **Shell 集成抽象的是「存储」而不是「注册表能力」** | 最容易写错的三件事（写哪些键才有菜单项、卸载怎么还原用户原有的关联、装了一半怎么收拾）都不依赖 `RegCreateKeyExW` 怎么调，只依赖「能读能写能列举」。抽象成 `RegistryStore` 之后，整个安装/卸载/校验流程连同它的用例可以在 macOS 上真实执行，而 Windows 侧只剩一层不含判断的转发 | PLAT-005 |
| **键名与值名只在比较时归一，存储一律保留原拼法** | 注册表大小写不敏感，但**保留拼法**是给「用 regedit 打开看一眼」的人看的：写成 `lqcompare.difffile` 会被当成随手敲的乱码。反之，比较时不归一又会出现「用 `/` 写的键存在、用 `\` 查不到」这类只在手工调试时发生的怪事。两件事分开做，各取所需 | PLAT-005 |
| **认不出的注册表值类型必须原样读回** | 只记「有个值」而不记它的类型与字节，等于把「本来有一个我不认识的值」记成「本来没有值」；卸载时就会**删掉用户的数据**。`RegistryValueKind::Unsupported` 把原始类型码与字节一起带上，`operator==` 逐字节比较——这条路径的测试断言的是往返之后字节完全一致 | PLAT-005 |
| **单实例不靠 `qHash()` 推名字，自己写 FNV-1a** | `qHash` 对每个进程使用随机种子，同一个输入在两个进程里得到不同的值；而这里的名字必须**跨进程一致**，否则第二个实例算出来的名字与第一个记下的永远对不上，表现是「单实例机制完全没生效」且**没有任何报错**。凡「跨进程要一致」的散列都不能用 `qHash` | PLAT-006 |
| **净化后的标识要补一段原始输入的特征值** | 共享内存键在 Unix 上会变成一个文件名（Qt 拿它去 `ftok`）、端点名会变成一个套接字路径，所以名字里的非安全字符一律换成 `_`。但中文用户名、带空格的用户名净化后都是「一串下划线」，两个不同的用户会撞进同一个标识——其中一个的程序会被另一个的实例吞掉，而他只能看到「点了没反应」。补一段稳定摘要只用来区分，不要求可逆 | PLAT-006 |
| **标识符附带「原始字段」的摘要，而不只是净化后的文本** | `a-b` + `c` 与 `a` + `b-c` 拼出来是同一个可读串，净化本身也可能把两个不同输入映射到同一输出。对原始字段做**长度前缀**编码后再摘要、并把摘要追加到所有标识（短标识也一样），字段边界与「净化前的区别」就都保住了 | PLAT-006 |
| **转发结局的退出码要错开 CLI-004 已占用的 0~4** | 撞进去的后果不是报错，而是「脚本以为比对跑过了、而且没有差异」——这是最难被发现的一类错误。取值区间由一个显式的 `relayExitCodeBand()` 划出，并有一条自检用例把 CLI-004 的上界钉在 4 上；CLI-004 落地时应当把它换成对那边的引用，那一刻自检会提醒 | PLAT-006 |
| **线协议以「长度前缀」分帧，且解码要求「正好一帧」** | 本地套接字是字节流，一次 `write` 与一次 `readyRead` 之间没有任何对应关系；没有长度前缀就只能靠「读到断开为止」，那意味着一个永不挂断的客户端能把内存撑满。而「正好一帧」比「至少一帧」严格：宽松时两帧粘在一起会被静默当成一帧，第二个请求从此排在队列里等一个不会到来的帧头。**这个「正好」的检查有两处**（分帧与载荷读尽），是刻意的纵深防御——变异测试证实去掉分帧那一处后，载荷读尽那一处仍然拦得住 | PLAT-006 |
| **交不出去时默认「照常启动新实例」，而不是报错退出** | PLAT-006 的边界条款是「通信失败时降级为启动新实例，绝不让用户看到失败」：用户双击一个文件，他想要的是看到文件。把「转发失败」做成一句错误对话框，等于把实现细节变成用户的障碍。无界面与脚本场景另给 `ExitWithRelayCode`——那里没有窗口可开，「悄悄多出一个进程」比直接报错更糟 | PLAT-006 |
| **「要不要抢焦点」抽成三值策略 + 注入「距上次输入多久」** | 抢错的表现是用户正在另一个窗口里打字、焦点被夺走；这一条是本条目里唯一与人的手感有关的规则，因此不能藏在一个 bool 里。而「用户刚才有没有在打字」只有界面层知道，所以做成可注入的函数对象，不注入时按「不知道」处理并照常置前——那条路径正是「用户双击了一个文件」的主路径 | PLAT-006 |
| **选主先争进程锁，再把共享内存当标识** | 两个作用不同：共享内存回答「有没有人在跑」，进程锁回答「谁在改这份标识」，后者在回收崩溃遗留标识时是必需的（否则两个实例的 create/attach 会交错）。共享内存不可用（资源耗尽）时只靠进程锁仍能选出唯一首实例，转交照旧走套接字——**这正是本机实际走的那条路** | PLAT-006 |
| **替换规则改写「内容」，忽略开关放宽「判等」——两者不合并** | 忽略大小写 / 忽略空白**不改变**用户看到的那一行，替换规则**会改**；所以前者可以出厂默认开着，后者必须默认**空集**——出厂就开着等于替用户做了内容层面的决定，而他第一次打开两个文件时根本不知道有过这个决定。同理，替换规则排在规范化链的**最前面**：规则的正则是对**原文**写的（用户看着原文写正则），挪到大小写折叠之后会出现「界面上的文本明明匹配、引擎却匹配不上」的错位 | TXT-012 |
| **替换规则命中后是 `Change::Ignored`，不是 `Equal`** | 两行的键相同、原文却不同，走的正是 TXT-008 留下的那条通道。视图层因此**不需要**为替换规则新增一种状态，而用户仍然能看到「这一行被规则吃掉了」；反过来，若把命中硬写成 `Equal`，界面上会显示成「原文完全一致」，那是把一次忽略伪装成一次巧合 | TXT-012 |
| **注册表访问固定用 `KEY_WOW64_64KEY`** | 交付目标是 32 位 MinGW 构建。不加这个标志时，32 位进程写到 `WOW6432Node` 影子副本，而 64 位资源管理器**看不见**那里。现象是安装、校验、卸载全都报成功，而右键菜单里什么都没有——用户唯一能看到的证据是「没有变化」 | PLAT-005 |
| **安装是全有或全无，与批量操作的默认策略相反** | 批量复制第 7 个失败可以继续做第 8 个；Shell 集成不行：装了一半的用户既不知道装了哪些、也无法自己卸掉，而且我们自己的登记此时是可疑的（它会说「装过」）。所以失败必须回滚，`rolledBack` 与 `rollbackClean` 是两个独立的结论 | PLAT-005 |
| **卸载与安装回滚共用同一份 `removeInstallation()`** | 两者要做的事逐条相同（按记录的选项倒序删值、键判空后按深度倒序删、最后删登记）。写两遍必然分歧，而分歧的表现是「卸载干净、回滚留渣」或反过来，两种都只在失败路径上出现 | PLAT-005 |
| **登记子树是唯一允许递归删除的地方** | 删单个键一律先判空，键下有别人的内容就保留并报 `AlreadyExists`（建议文案也不同：这里没有「占用者」，不能照抄「请关闭占用它的程序」）。只有登记子树例外——它的下标键（`Backup\1`、`Backup\2`）是运行时生成的，不在静态计划里，按静态清单判空会把它们误判成外来内容而拒绝删除，于是卸载报「残留」而用户什么都没做错 | PLAT-005 |
| **重新配置 = 先按记录的选项拆掉，再装** | 用户取消勾选某项后再点安装，若只是叠加，注册表会留下一批只属于旧选项的项，而登记（卸载的依据）写的是新选项——卸载不会删它们，**报告却会说「残留检查通过」**。所以安装的步骤 0 是「已安装则先拆」，拆不干净就中止 | PLAT-005 |
| **测试里的故障注入要区分「写」与「删」** | `failOnValue()` 把某个值的删除也一起挡住。用它测「写入失败后回滚能否清干净」时，回滚必然也失败，于是「回滚逻辑写错了」与「注册表真的删不掉」从结果上分不开。真实世界的写入失败大多是瞬时的，`failNextWriteOnValue()` 只挡这一次写入，才真的在断言回滚 | PLAT-005 |
| **失败记录必须把 `detail` 一起打印出来** | 只给「失败 + 错误分类」的报告会写「失败 ……：找不到 该路径」，而用户看不出找不到的是**哪一项**、期望它是什么值。分辨「缺失」与「值不对」的全部信息都在 `detail` 里，而这两种情况的处置不同（一个要补、一个要改） | PLAT-005 |
| **级别过滤同时写在宏里与 `write()` 里** | 宏里那个 `if` 的职责是「别求值参数」（逐文件比对时每个文件都会拼一条消息，级别本来是关的，开销却与文件数成正比）；`write()` 里的过滤的职责才是「按级别丢弃」。只做前者的话，直接调 `write(Level::Debug, …)` 的地方看起来受级别控制、实际不受——`--log-level error` 下调试日志照样打印，日志文件也没法靠调级别瘦身 | ENG-006 |
| **过滤判断放在 `isEnabled()` 里，不重复写比较** | 宏、`write()`、`Stopwatch` 三处都要判断。各自写一遍 `<=` 的话，把方向和级别顺序弄反只会在其中一处发生，而现象是「某个级别偶发不输出」——这种不一致最难查 | ENG-006 |
| **日志先成结构（`Record`）再成文本（`line()`）** | 界面输出面板要按级别着色、分列显示时间与分类。只给一行拼好的字符串，面板就得反解析（按空格切、猜哪段是分类）——日志格式的任何调整都会静默打断它。结构里同时带 `level` / `category` / `threadId` / `time`，面板不必猜 | ENG-006 |
| **接收者句柄用整数，返回 0 表示传入了空函数对象** | 空 `std::function` 不是「什么都不做的接收者」，而是调用时崩溃；在注册处挡住它，崩溃就落在传参那一行而不是第一次真正记日志的地方。句柄用整数而不是拿函数对象做键，是因为 lambda 之间没有可靠的相等，移除会静默失效 | ENG-006 |
| **调用接收者前先拷贝清单、出临界区再调用** | 接收者里写日志（输出面板顺手记一条调试日志）在持锁调用时会**死锁**，而现象是「界面卡住」，与日志模块看起来毫无关系；接收者在回调里增删接收者也会让容器在遍历中变动。代价是并发移除时那个接收者可能还收到这一条——比死锁好得多 | ENG-006 |
| **接收者在「记录日志的那个线程」上被调用** | 这既是实现事实，也是给使用方的约束：界面输出面板不能直接被挂成接收者——图标解析跑在后台线程上，从那里碰控件会崩。面板必须自己加一次排队跳转。测试用一条跨线程用例把这个约束钉住，改坏了会红 | ENG-006 |
| **耗时辅助是 RAII，且级别在析构时判断** | 用 `start()`/`stop()` 两个调用的话，中途 `return`、抛异常、或忘了 `stop()` 的路径都会漏记——而漏记的那条恰恰最可能是「为什么这里有时很慢」的答案。级别在析构时判断，是为了让「先放计时器、再用命令行把级别调开」这种常见排查顺序也能出结果 | ENG-006 |
| **级别的名字解析只有一份实现（`levelFromName`）** | 早先的解析写在 `main.cpp` 里，于是加一个级别就会出现「日志模块认、命令行不认」的分歧，而用户只看到「未知的日志级别」，看不出其实是没同步。解析失败时**不修改**出参，调用点才能把「写错了」（要提示）与「没写」（用默认值）分开处理 | ENG-006 |
| **滚动检查放在写路径上（每秒至多一次），而不是只在用户点「应用」时做** | 只在应用设置时检查，等于把「日志文件会到 500 MB」这件事交给用户记性——而用户正是被日志占满磁盘才来改这项设置的。检查放在 `appendRecord()` 里、且**写在写这一行之前**：先写再检查的话，那条把文件顶过上限的记录永远留在旧文件里，滚动之后立刻又超限。丢一帧的检查用 `QElapsedTimer` 限流，避免每次写日志都 `stat` | OPT-010 |
| **写路径上的滚动失败被吞掉，`rotateIfNeeded()` 才返回错误** | `appendRecord()` 持着非递归互斥量：在里面调 `rotationDecision`/`applyLogRotation` 失败后再记一条日志会**死锁**，而现象是「程序卡住」，与滚动毫无关系。所以那里只尽力而为，需要错误的地方（设置页、命令行）走显式的 `rotateIfNeeded()`。日志系统的失败不该把用户的工作一起带走 | OPT-010 |
| **清空日志是截断文件，不是删除文件** | 删除后日志系统手里那个 `QFile` 仍指向已被 unlink 的 inode，后续写入落在一个「谁也不认识的文件」上：磁盘不涨、文件也永远不出现，用户看到的是「日志功能坏了」。截断保住了 inode 与打开的文件句柄。同理，清空**不动**滚动历史——历史文件是用户为了排查才留下的，一并删掉会毁掉证据，界面上也据此分开说明 | OPT-010 |
| **脱敏的边界判定宁可多替换一次，也不能漏掉一次** | 判据从「后面必须跟分隔符」反转为「后面不能是名字的延续字符（`_ - .` 或字母数字）」。前者漏掉 `/Users/loren `（后面是空格）这类**真实路径**——那是一次隐私泄露，发出去的诊断包里躺着用户的家目录；后者多替换的顶多是 `/Users/lorenx` 这种恰好以家目录名开头的路径，只是看起来有点怪。两种错误的代价不对等，所以规则朝不泄露的一侧偏 | OPT-010 |
| **枚举滚动历史不用 `QDir` 的名字通配** | 日志文件名是用户可配的。用 `entryList(name + ".*")` 时，名字里带 `[` `*` `?` 会被当成通配符：`a[1].log` 既能**漏掉**真正的 `a[1].log.1`，又能**误收**无关的 `a1.log.12345`。改成列全部文件、按前缀 `startsWith` 加整数后缀解析来判断——诊断包里混进无关文件会把包变大还可能带出隐私，漏掉历史则让排查少一半材料 | OPT-010 |
| **`keepFiles == 0` 时先把已有历史全删掉** | 用户把保留份数从 5 调到 0，意图是「别给我留历史」。只做「删掉当前文件」的实现会让磁盘上此前攒下的 `.1 ~ .5` 原样留着——设置看起来生效了，磁盘占用却一点没降。所以滚动时先按目标份数清历史，再放新的那一份 | OPT-010 |
| **诊断包在任何一步失败时整个目录回滚** | 半个诊断包比没有更坏：用户会把 `lqcompare-diagnostics-*` 直接发给别人，包里少了环境报告或日志就会得出「你这日志里什么都没有」的结论，而真正的原因是我们中途失败了。失败时删掉已经建好的目录，返回的 `error` 才说明白「包没做出来」 | OPT-010 |
| **格式定义的「不透明袋」是刻意的：`settings` 只存不解释** | 完成标准点名了编码与行尾策略、语法定义、转换规则、行过滤器、重要性规则、列/字段定义、杂项选项这一长串字段。把它们逐个建模成结构体，就要在**没有任何消费者**的情况下先替每一类定死 schema——而第一个真正会来读它们的是语法高亮引擎与格式管理器界面，它们对同一份数据的要求还要互相妥协。所以模型层只保证「**稳定 id + 掩码 + 视图类型 + 内容签名 + 一个能原样往返的 JSON 袋**」，`settings` 里放什么由消费方决定。关键约束是**解析它不执行任何程序**（有 `externalConverter` 这类字段的往返用例钉住这一点）——配置文件不是代码 | FMT-001 |
| **格式 ID 是关联的唯一键，名字不是** | 覆盖内置、`baseId` 继承、按 ID 合并，三件事全靠 ID 把两份定义连起来。ID 因此必须是**稳定的小写短横线标识**（`^[a-z0-9][a-z0-9-]*$`），且同一份文件里重复 ID 必须**先到先得**并报诊断——静默接受两个同 ID 的定义，会让「按 ID 找定义」的结果取决于条目书写顺序，于是同一份定义文件在不同机器上解析出不同的规则表，而用户只看到「覆盖没生效」。改名走的是「同 ID + 新 name」，**永不**产生新 ID | FMT-001 |
| **坏条目逐个跳过，坏文档整份拒绝** | 两者的处置刻意不同。**条目级**错误（ID 非法、掩码编译不过、签名越界、继承来源不存在）只跳过那一条并报出第几条、哪个 ID、为什么——文件是用户手写的，一条写错不该让他丢掉整份定义。**文档级**错误（不是 JSON、版本不认识、没有 `definitions` 数组）则整份拒绝：这三件事说明我们读的根本不是这份格式，继续按「空表」处理会把用户的定义静默当没写 | FMT-001 |
| **内容签名必须落在可采样的前缀里** | 签名要求「1–4096 字节且完整位于前 1 MiB 内」。没有这条约束，用户写一个偏移 4 MiB 的签名照样能存进去，但探测永远看不到那么远——于是这条规则**永远不命中**，而它在编辑器里看起来完全正常。宁可拒绝，也不要留一条静默失效的规则 | FMT-001 |
| **掩码的语法只有一份实现** | 同一条掩码会出现在 Filters 页、会话设置、子目录排除、预设库与命令行上。各处各写一套通配逻辑，必然出现「界面上能匹配、命令行匹配不了」这类分歧，而用户只会得出「这个软件的过滤靠不住」的结论——他不会去区分是哪个入口的 bug | FILT-001 |
| **掩码里的分隔符恒为 `/`，`\` 是转义字符** | 掩码是**用户的意图**而不是本地路径。让分隔符跟着平台走，一份要导出给团队共享的预设库（FILT-007）就带上了平台色彩：`build/out` 在一台机器上排除子目录、在另一台上排除一个名字里带 `/` 的条目。定死之后还有一个好处：Windows 用户敲 `build\out` 时，`\` 后面跟的是不能转义的字符，于是这变成一条**带建议的明确错误**，而不是静默变成 `buildout` | FILT-001 |
| **不含 `/` 的掩码按名字匹配，含 `/` 的按相对路径匹配** | `*.txt` 必须能在任意目录下命中，否则用户得为每个目录写一条掩码；而 `src/*.txt` 必须**从起点**匹配，否则别的目录下的同名子树会被一起过滤掉，用户看不出差别在哪。让掩码自己决定按哪一侧匹配，调用点就不必各自记住「哪个参数给名字、哪个给路径」 | FILT-001 |
| **`**` 只有独占一段时才跨目录** | 这是 gitignore / ant / ripgrep 共同的规则，也是唯一能让「`a**b` 到底能不能跨目录」有确定答案的写法。不这样规定的话，`a**b` 与 `a*b` 的行为没有任何可预期的区别，用户只能靠试 | FILT-001 |
| **段级匹配用可达表做 NFA 模拟，不用递归回溯** | `**` 每一步都有「吃零段」与「吃一段」两个选择，于是 `**/**/**/…` 对上有 N 段的路径时有 2^N 条可能路径。一个手抖敲出来的掩码就足以让扫描停在那里不动——而「恶意与畸形输入」正是本条目第 5 条点名要求处理的输入。可达表把复杂度压到 O(路径段数 × 掩码段数)，且没有递归深度问题 | FILT-001 |
| **大小写不敏感时字符集两个方向都试，不折叠区间端点** | 「把区间两端也折成小写再比」会让 `[A-_]` 的区间**反转**（折叠后 `a` 比 `_` 大），这个字符集从此匹配不到任何东西，而表面上一切正常。反转输入字符再比一遍既没有这个问题，也不必为区间维护两套边界 | FILT-001 |
| **未闭合的 `[` 报错，不当字面量** | 当字面量的话，用户把 `[abc` 敲漏一个 `]`，掩码会静静地变成「匹配字符串 `[abc`」：过滤看起来还在工作，只是永远不命中，而界面上完全看不出问题。报错能让输入框就地标红 | FILT-001 |
| **`\` 的转义白名单是封闭的，不认识的转义就是错误** | 「不认识的转义原样保留」这种宽容处理会把 `build\out` 悄悄变成 `buildout`——过滤看起来生效了、只是漏了一批文件。宁可报错，也不要给出一个看起来生效的过滤器 | FILT-001 |
| **一行掩码写错只丢那一行，其余照常生效** | 用户在界面上是一行一行改的。一行写错就整段失效，他会以为是自己把别处敲坏了，于是去动本来正确的行 | FILT-001 |
| **换行同时认 `\n`、`\r\n`、`\r`** | 预设库要能导出给团队共享，而 Windows 上编辑过的文本文件是 CRLF 行尾。只按 `\n` 切行的话，每行末尾会多出一个 `\r`，于是 `*.tmp` 变成 `*.tmp\r`——**静静地对不上任何文件**。用户看到的现象是「导入的预设完全不起作用」，而掩码本身看起来完美无缺 | FILT-001 |
| **「被排除」与「未命中」是两个不同的结论** | 前者是「你明确要求不要它」，后者是「你的包含掩码里没有它」。FILT-006 的批量操作安全提示与 FILT-011 的「我为什么看不到这个文件」都要靠这个区分；合成一个之后，那两个功能只能重跑一遍匹配去猜 | FILT-001 |
| **「起决定作用的规则」与「命中了哪些规则」分成两个查询** | 决定只有一个（第一条命中的排除规则），而诊断要回答的是「哪几条在管它」。一个条目可能同时命中两条排除规则，只报第一条会让用户改掉一条之后发现还是看不见 | FILT-001 |
| **预览文案「匹配 N 项 / 共 M 项」由服务层生成** | 规格第 4 条点名要这句文案。让每个界面各拼一遍的话，其中一处迟早会写成「共 M 项 / 匹配 N 项」，而截图比对时没有人会注意顺序变了 | FILT-001 |
| **属性条件与名称过滤取「与」，不是「或」** | 取或的话，一次「只看 10 MB 以上的文件」会连带把名称过滤明确排除掉的文件放回来——而用户加过滤器的意图是「更窄」，他只会把这件事理解成过滤器不可靠。同一条理由在 FILT-005 的「层间取交集」上重复出现，两者是同一个直觉 | FILT-003 / FILT-005 |
| **属性过滤只吃元数据，模块里不留任何读文件的路** | 第 5 条要的是「属性过滤不能退化成内容过滤」。现实理由是性能：属性条件要能在**扫描阶段**就把条目丢掉，而扫描器一旦为了判断属性去读内容，这条提前退出的路就再也加不回去了。守住的方式是**读源码的护栏**（出现 `QFile`/`readAll`/`QTextStream` 即红），因为「以后有人顺手加一读」正是这类约束的典型死法 | FILT-003 |
| **元数据缺失走「放行 + 记不确定」，不当作不满足** | 这是一个三态结论：`accepted`（留不留）与 `evaluated`（判没判过）分开。把「还不知道大小」当成「不满足大小条件」，会让条目在元数据到达之前凭空消失，而用户看到的现象是「文件不见了」——他不可能会想到去查过滤器的判定时机 | FILT-003 |
| **条件写错永远不缩小结果集，但一定被报出来** | 大小写成 `abc` 时若按「不满足」处理，用户会得到「过滤器生效了但结果不对」，比直接空着更难查。报错带**位置与建议**（`ConditionProblem` 的 line/column/hint），界面才能在输入框就地标红并提示改法 | FILT-003 |
| **「现在」由调用方传入，模块自己从不取当前时间** | 「最近 7 天」这类相对窗口在测试里必须可复现。模块内部调 `QDateTime::currentDateTime()` 的话，用例只能靠「跑得足够快」避免跨秒失败——那是一种偶发、且看起来像别的问题的失败。`TimeCondition::referenceTime()` 因此是显式输入 | FILT-003 |
| **只填日期的上限算到当天最后一刻，且用同一天的 `23:59:59` 构造** | 用户写 `2026-09-10` 指的是「含这一天」。用当天 `00:00` 比较会把这一天改成下午的文件全排除掉。而不用 `addSecs(86399)` 是因为夏令时切换的那一天有 25 小时，加固定秒数会跨错一个小时 | FILT-003 |
| **大小单位按 1024 进制，且单位名里同时认中文「字节」** | 与资源管理器「属性」页显示的数字一致，用户拿它对照时才不会觉得我们算错了。中文单位进表是为了让 `formatSizeText()` 的输出能**原样解析回来**（往返一致），否则「复制到别处再粘回来」这条路径会凭空失败 | FILT-003 |
| **相对天数用显式前后缀表解析，不用正则** | 正则的最左匹配会把 `7days` 的 `days` 从位置 0 吃掉，只剩一个空数字；而前缀表（`最近`/`过去`/`近`）与后缀表（`天内`/`日内`/`天`/`日`/`days`/`d`，长串优先）是谁都看得懂的两张表，报错时也能说出「后缀不认识」 | FILT-003 |
| **属性的「已知/未知」按单个位记录，不合并成一个属性集** | `knownAttributeBits` 与 `attributeBits` 分开。平台能报告的信息不完整时（例如某个平台不给「归档」位），「未知的归档位」不能当成「没有归档」，否则 `Requirement::Forbidden` 会错误地放行、`Required` 会错误地拦下——两种错都错在「把没读到当读到了」 | FILT-003 |
| **条件表是唯一的事实来源，同样带启动自检** | 与 `filterLayerTable()` 同一套做法：顺序、标识符、声明键、是否只依赖元数据全部由 `attributeConditionTable()` 推导，`validateAttributeConditionTable()` 参数化（表当参数传）以便测试用一份故意写坏的表证明它真会报 | FILT-003 |
| **属性声明复用 `maskfilter.h` 的 `splitDeclarationLines()`** | 同一段声明文本将来要和掩码行混在一处（一行一个条件、`#` 注释、CRLF 行尾），各写一套切行必然分歧，而分歧的表现是「某一种行尾下条件静静失效」。把切行提成公共 API 比复制一份便宜 | FILT-003 |
| **语法速查是数据，不是手写的文档表格** | FILT-011 要求「文档中的示例掩码与测试语料中的用例一致（用同一个数据源生成）」。手写的 markdown 表格必然在某次修改后与实现分家，而错误方式是「帮助里说 `[!a]` 是取反、程序其实不认」这种用户完全无法自查的偏差。速查表的每条样本都有期望结果，测试会把它真的跑一遍 | FILT-001 |
| **三层过滤之间取交集，层内仍然是排除优先** | 并集的意思是「任一层放行即可见」，于是新增一层过滤会让结果**变多**——用户加一个「只看 `.cpp`」，反而看到了原本被排除的文件，而他只会认为过滤器坏了。交集保证「加一层只会更窄」。层内保留 FILT-001 的「排除优先」不变，于是三层的关系不引入第二套语义 | FILT-005 |
| **`FilterLayer` 与 `SettingScope` 是同形不同义的两套三层，必须按存储分别读** | 设置是**覆盖**（`ScopedSessionSettings::value()` 只返回胜出的那一层），过滤是**叠加**（三层全部生效）。用前者的读法取过滤声明，会静默丢掉会话层的过滤——界面上两条设置看起来都在，用户只会觉得「会话级的过滤怎么不起作用」。`FilterLayerBinder::loadInto()` 是唯一实现，`Tests/FilterStack` 有一条用例同时钉住两种读法的差别 | FILT-005 |
| **`FilterLayerState::active()` 是「启用**且**有规则」，只看 `enabled` 不算** | 三层默认都是启用的，把空的启用层也算成生效，「某一层生效中」就恒为真，用户无从判断到底是谁在过滤。第 2 条「显示该层是否当前生效」的全部价值都落在这一条上——面板上「已启用，但没有规则」与「已禁用」必须是两句不同的话 | FILT-005 |
| **合并表达式用「平铺」形状，不用「逐层相与」** | 设第 i 层是 `INC_i && !EXC_i`，则 `AND_i (…) = (AND_i INC_i) && !(OR_i EXC_i)`（德摩根）。两者等价，但平铺写法短得多：三层各两条规则时，逐层写法有 6 个括号，平铺只有 3 个。掩码本身含空格或 `& \| ! ( )` 时原子会被单引号包住——否则一条叫 `a\|b` 的掩码会与运算符分不开 | FILT-005 |
| **每一层的落点写成数据（`filterLayerStorage`），写入按它路由** | 第 4 条是一条**路由**约束：同样的键名与内容，写进视图存储是临时的，写进会话存储就跟着会话走了。每个调用点各自决定往哪写的话，一次「临时过滤」变成「永久过滤」只需要一处笔误，而现象是「关掉标签再打开，过滤还在」——用户会以为是设置没生效。把两个存储收进 `FilterLayerBinder` 之后，`saveLayer(View, …)` 只可能落到视图存储 | FILT-005 |
| **视图层的存储是借用的，不接管生命周期** | 「关闭标签即丢弃」因此是**所有权**带来的结论，而不是一段需要记得执行的清理代码——后者总有一条路径会漏掉（崩溃、强杀、异常），而漏掉的表现恰好是临时过滤悄悄变成永久过滤 | FILT-005 |
| **`FilterLayerBinder::saveLayer()` 在目标存储缺失时报失败，不退而写入另一个存储** | 视图存储没接上时写进会话存储，意味着用户的一次临时过滤变成了永久的，而他从没同意过。与 SESS-007「写入目标层缺失时返回失败」是同一条纪律 | FILT-005 |
| **层级表是唯一的事实来源，且带一条参数化的启动自检** | 顺序、下标与落点分成三处各写一遍，往表里插一层就会出现「面板上排在第二、写进存储用的却是第三个落点」这类错位。`validateFilterLayerTable()` **把表当参数**，因此测试能拿一份故意写坏的表证明它真的会报——一条永远不会红的护栏比没有护栏更糟。其中「视图层是否仍然仅当前视图」单独占一条：它是本条目最贵的一处错误，错了也没有任何运行期现象 | FILT-005 |
| **会话基类把「只建一次视图」冻结在模板方法里** | 会话容器在切换标签、恢复窗口、拖到另一块显示器时都会多次索取视图。让每个子类各自保证「只建一次」的话，漏掉的那个会在第二次切换时把界面重建一遍——滚动位置、展开的节点、正在编辑的文本全部回到初始状态，而这类缺陷只在特定操作序列下出现。基类公开 `createWidget()`、子类只实现 `createView()`，子类的代码根本不在「重复创建」那条路径上 | SESS-001 |
| **`open()` 幂等，且第二次不调 `doOpen()`** | 界面在恢复标签时会重复调用 `open()`；重跑一遍 `doOpen()` 会把用户滚动到的位置、展开的节点、正在编辑的文本全部重置，而他只是切了一下标签。想重新读取数据源就走 `reload()`——那是一个显式的动作 | SESS-001 |
| **视图指针用 `QPointer`** | 容器删标签时会连带析构视图。若基类存的是裸指针，下一次 `createWidget()` 会把一个已析构的对象交给界面，而崩溃点与真正的错误毫不相干。`QPointer` 让「视图不在了」变成一个可判定的 `nullptr`，于是可以就地重建 | SESS-001 |
| **`close()` 不销毁视图** | 视图的父子关系属于容器：基类无法知道这个视图有没有被别处引用（分离窗格会把同一个视图挂到另一个窗口下）。替容器删掉它就是越权，后果是容器手上留一个悬空指针 | SESS-001 |
| **打开失败停在 `Failed`，而不是退回 `Created`** | 「还没打开」要提示「请选择文件」，「上次打开失败」要显示失败原因，两者必须能区分。退回 `Created` 会让那次失败在界面上变成「什么都没发生」，用户只会再点一次同样的按钮 | SESS-001 |
| **未保存改动时拒绝重载，且这条判断在基类** | 忘了判的后果是用户几十处编辑在点一下「重新加载」之后无声消失，而那条路径不会有任何提示。确认丢弃之后走的是**显式**路径（先 `setDirty(false)` 再重载），因此不会因为某处漏判而被走错 | SESS-001 |
| **没有改动时 `save()` 返回失败，且一次都不调 `doSave()`** | 把没有改动的会话重写一遍，在同步目录里会产生一串毫无意义的版本，在只读介质上则会凭空失败。返回 false 让调用点能一致处理（界面本来就会把 Save 置灰），而关键断言落在「`doSave` 调用次数为 0」上——只看返回值的话，「整批重写一遍」的实现同样会『成功』 | SESS-001 |
| **`isDirty` / 状态栏文本 / 进度去重，错误不去重** | 前三个是**状态**：值没变就不发信号，否则状态栏会在批量过程中反复重排、标签上的「*」会反复重绘。错误是**事件**：同一个原因连续失败两次时，上层要能知道「它又试了一次并再次失败」，而不是以为信号漏了一个 | SESS-001 |
| **进度的原始数字如实保留，只有百分比夹紧** | 与 `actualPixelSize` 同一条思路：界面上出现 150% 是「程序坏了」的观感，但把 12/10 悄悄写成 10/10 会让一个真实的计数错误永远查不出来。夹紧只发生在 `percent()` 这一个显示用的查询里 | SESS-001 |
| **三个公共出口是 public，不是 protected** | 容器与命令行也要往同一个出口推（「正在打开命令行传入的文件……」），而信号只能由本类发出。入口保持一个，状态栏就不必判断这条消息是谁发的 | SESS-001 |
| **`sessionSettings()` 惰性构造，构造函数里不调 `createSettings()`** | 构造函数里调虚函数只会派发到基类版本（C++ 的经典陷阱），于是子类的覆写永远不生效，而现象是「设置改了不生效」。惰性构造也让工厂可以安全使用「子类构造期还没准备好的东西」 | SESS-001 |
| **设置接口默认给一个内存实现，而不是返回空指针** | 返回 nullptr 的话每个调用点都要判空，而「判空之后什么都不做」正是最难发现的一类缺陷：设置看起来保存了、其实丢了。内存实现的行为是确定的（读不到就是调用方给的回退值、写进去当次有效），并且**不谎称已落盘**；SESS-006 换实现时接口不变 | SESS-001 |
| **设置入口也是「值没变就不算改动」** | 界面把 `changed` 直接连到「会话变脏」，而用户点开设置看一眼再确定关掉不该让会话变脏。`clear()` 用空键承载「全部变了」——接收方真正需要知道的就是「别只看某一项了，全部重读」 | SESS-001 |
| **`SessionError` / `SessionProgress` 先成结构、再成文本** | 与 `Log::Record` 同一个理由：状态栏、错误对话框、输出面板对同一次失败需要不同的呈现（一行摘要 / 带详情 / 带原始错误码），而只给一行拼好的字符串的话，它们只能去反向解析那段文字——任何一次文案调整都会静默打断其中一处。`detail` 为空时界面整段不显示，而不是显示「详情：」加一片空白 | SESS-001 |
| **`createView()` 返回空视为错误并上报** | 一个不给出视图的会话类型在界面上表现为「打开了一个空窗格」，而用户看不出是还没实现、还是数据源为空、还是程序坏了。上报的 `detail` 直接写明「会话类型应当总是返回一个控件」，把责任方指出来 | SESS-001 |
| **类型 ID 是机器键，只允许小写字母、数字与连字符** | ID 会存进会话文件、命令行与最近会话列表，一经发布不可改名（改名让那些入口指向不存在的类型，现象是「双击会话没反应」）。允许大写会产生大小写变体、允许空格与中文会让命令行必须加引号规则。`add()` 在登记时校验并整条拒绝，另有一条用例对全部已发布 ID 做快照断言 | SESS-002 |
| **掩码复用 `Services/Filter` 的语言，不自制「看扩展名」的匹配** | FILT-001 的边界写着「不允许各处实现各自的通配逻辑」，注册表是它的第一个复用方。自制版本看着更简单，但它会在 FILT 系列落地时与真正的语法分家，而分歧的表现是「Filters 页能匹配、自动选视图不能」——用户只会认为过滤功能靠不住。复用之后连 `**` 与字符集都免费成立 | SESS-002 |
| **描述子（纯数据）与创建工厂分成两层类型** | 第 2 条要求「已发布 ID 被快照断言锁定」，而 `std::function` 不可比较、也不该进快照。混在一起，那张表就没法直接照搬进用例；分开之后 `builtInSessionTypes()` 返回的是一份可以逐字段比较的纯数据 | SESS-002 |
| **工厂的返回类型只前向声明 `CompareSession`，不 include** | 服务层 include 界面头文件是 `tools/check_layering.py` 明令禁止的方向（后果是服务层测试开始需要链接整个界面）。前向声明加 `std::function` 既守住方向又保住类型安全——写成 `QObject *` 再让调用点自己转，安全就丢在每一处创建会话的地方。直接收益：`Tests/SessionType` 能 `QT -= gui` | SESS-002 |
| **注册顺序同时是查询优先级与 Home 页展示顺序** | 若是两份顺序，用户会看到「表格比对明明在上面，双击 `.html` 却打开了文本比对」这种无法自洽的界面。代价是内置表里 `*.html` 被两个类型同时声明——那处重叠**有意留着**，因为合成的小表只能证明实现自洽，证明不了这张表落在什么结论上 | SESS-002 |
| **不可用的类型不参与自动选择，但要能枚举出来** | 在 macOS 上把 `.exe` 判给「版本比较会话」，用户得到的是一句「仅 Windows 可用」，而他本来可以拿到一个十六进制比对；反过来，只给「可用的那些」的话，Home 页要按 REG-001 的要求把受限入口**置灰并说明原因**，就只能自己再维护一份「哪些是仅 Windows」的名单 | SESS-002 |
| **大小写策略从 `Filter` 取，且可显式覆盖** | 与 FILT-001 同源：同一次比较里「按掩码过滤」与「按掩码选视图」必须对同一个扩展名给出相同结论。提供显式参数是为了让「Windows 上大小写不敏感」这条规则在 macOS 上也能被真实断言——与 `mask.h` 的 `defaultCaseSensitivity(MaskPlatform)` 同一手法 | SESS-002 |
| **注册表不做成单例** | 表要被反复构造：合成的小表验证优先级、内置的大表验证快照。单例会让「这一次测试往表里加了什么」泄漏到下一处。现有的 `CommandRegistry::instance()` 是单例，那是因为它的调用点遍布界面各处；注册表目前的调用点（`main.cpp` 的启动自检与测试）都能显式拿到它就是 | SESS-002 |
| **`validate()` 只查登记时没有把住的几件事** | ID 格式、ID 重复、显示名为空、掩码编译失败在 `add()` 时就被整条拒绝了，再查一遍得到的分支永远走不到——而一条永远不会红的护栏比没有护栏更糟，它会让人以为这块已经被守住了。剩下那几条（英文原名缺失、图标键写法、掩码写成大写）都是「手写表时容易写错、写错也不影响登记成功」的 | SESS-002 |
| **过渡期用源码级用例把注册表与 Home 页的硬编码 ID 钉在一起** | 注册表落地前，`HomePage::sections()` 那张硬编码的表是事实上的第一批「已发布 ID」，两边不一致会让 Home 页点出来的入口指向不存在的类型。让 HomePage 改成读注册表属于 SESS-003 的范围，本轮改了会与它撞车，因此先加一条读源码的用例，并对一段故意写错的源码做反向验证 | SESS-002 |
| **设置项的声明里，校验规则是枚举字段而不是一段正则** | 正则看着更通用，但它把校验交给一段不可分析的代码——界面没法回答「这一项在等什么」，测试也没法逐条覆盖边界。另外 Qt 5.15 的 `QRegularExpression` **没有匹配超时**（`setMatchTimeout()` 是 Qt 6.0 才加的，FILT-002 为此被卡住），把用户输入喂给一条不可中断的匹配等于留了一个不可控的卡顿入口。现用的几种规则（必填、长度、数值区间、取值必须在表内、掩码清单语法）都是 O(字符串长度) 的 | SESS-006 |
| **掩码清单项的校验复用 `MaskFilter::parse()`，不自制「看扩展名」的匹配** | 与 SESS-002 复用掩码语言同源。自制的写法会把 `[abc`（未闭合字符集）与 `build\out`（`\` 后面是不能转义的字符）静静放行，而它们正是 FILT-001 明确要求报错的形态——用户在设置页看到「合法」，扫描时那一行却被丢掉。复用之后连「第几行第几列」都是现成的 | SESS-006 |
| **用户改的是「草稿」，不是会话本身** | 「取消」= 丢掉草稿；「恢复默认」= 把草稿重置。若直接改会话，取消就得把每一项改回去（一百种做错的方式，错的后果是「点了取消、设置却变了」），而恢复默认会变成一个**不可撤销**的动作——本仓库的纪律是破坏性操作必须可逆 | SESS-006 |
| **「脏」是与「载入时读到的那份值」比，不是与出厂默认比** | 用户什么都没改就不该被问「要不要保存」；而「改回默认值」是一次**真实的**改动，必须被算作改动（否则按了恢复默认再关掉窗口，程序会认为什么都没发生，设置其实该变） | SESS-006 |
| **掩码清单归一化时只去掉末尾空行，中间的空行留着** | 末尾空行要掉：在编辑框里敲完最后一条顺手按一下回车，`toPlainText()` 以 `\n` 结尾，留着它会让「内容与默认值一样」变成「多了一条空掩码」，于是干净的设置被判定成有改动、关窗口时白问一句。中间的空行不能掉：校验报错带的是**行号**，掉了行号就与用户在编辑框里看到的不一致 | SESS-006 |
| **`applyTo()` 全有或全无，且只写有改动的键** | 半应用会让用户看到「一部分生效、一部分没有」，而被拦下的原因只覆盖其中一项；写全部键则会在会话文件里留下一堆没有意义的项（对 SESS-008 就是脏文件）。没有改动时返回失败——与 `CompareSession::save()` 一致，让「什么都没改也点应用」不会在同步目录里留下一串无意义的版本 | SESS-006 |
| **询问策略（该不该问、给哪几个出口、默认项）在服务层，界面只负责翻译成按钮** | 这是本条目里唯一一条「既有交互又必须能反向验证」的规则：「有改动才问」与「没改动也问」从代码上看只差一个判断，而用户体感是「这个对话框很烦」。放进服务层之后，「没有改动时询问次数为 0」是一条可以断言的结论，而不是一句承诺 | SESS-006 |
| **切换 Tab 的询问不给「放弃改动」这个出口，关闭才给** | 切换 Tab 时草稿原样带到下一张 Tab，没有任何东西会丢——给一个用不上的破坏性按钮，等于凭空造出一条丢工作的路径。关闭对话框必须给（否则用户没有别的出路）。默认项恒为「返回」：一个手快的回车不应该丢掉用户刚敲进去的东西 | SESS-006 |
| **询问处理器可替换（`setInquiryHandler`），默认真弹 `QMessageBox`** | 真实模态对话框在测试里要么挂住（`exec()` 不返回），要么得靠 `QTimer` 打补丁——后者让用例的成败依赖时序，是「偶发红」的经典来源。把「问一句、拿个答案」抽成可替换的函数之后，用例能精确断言「问了什么、给了哪几个选项、选了之后发生了什么」；`Tests/SettingsDialog` 里那个默认答案恒为「返回」的基类则保证「意外被问到」时用例是**变红**而不是卡住 | SESS-006 |
| **「应用」在校验不过或没有改动时置灰，「确定」不置灰** | 一个点了什么都不发生的按钮会让人以为程序卡了。而「确定」的语义在没改动时就是「关闭」，把它一起灰掉会让用户找不到关门的按钮。校验不通过时「确定」也不许关——用户会以为保存成功了，而改动其实一个字都没写 | SESS-006 |
| **「恢复默认」只作用于当前 Tab，且落在草稿上** | 按钮贴着当前页的内容，用户按它时的预期是「这一页恢复原样」；整表重置是更大范围的动作，该有一个说清范围的入口（那是 OPT 设置页的事）。落在草稿上意味着它可以用「取消」撤销 | SESS-006 |
| **脏标记用字重，不往 Tab 标题里塞星号** | 星号会迫使每一处读标题的代码都去处理它（漏掉一处就是「Tab 名字里带了一个 \*」），而「这一页有没有改动」本来就有 `isTabDirty()` 这个确定答案 | SESS-006 |
| **`QSpinBox` 的取值区间必须显式设置** | 它的默认范围是 0..99，用户想填 200 会被静静夹到 99——「看起来能用、实际改了用户的输入」是最难发现的一类缺陷。声明了上下界就用它（校验规则顺手变成界面约束），没声明就用整型全域 | SESS-006 |
| **框架里不含任何具体设置项** | 「文本比对该有哪些设置」是 TEXT-* / FOLD-* 的产品决定，不是框架的一部分。先编一份看起来完整的设置表，等各类型落地时会被逐条质疑；留白不妨碍任何人——登记一份声明，对话框立刻就有内容。`Tests/Settings` 里有一条用例把「目录当前为空」这个状态钉住，让下一个人看到它时必须做一次决定 | SESS-006 |
| **对话框先读会话现值、再建界面** | 反过来的话各项控件会先按出厂默认值建好，而那时的 `loadFrom()` 还没有人接信号——界面上显示的全是默认值，用户会以为自己的设置丢了，然后点「确定」把默认值真的写回去 | SESS-006 |
| **三层作用域的**读**走覆盖链，**写**只落目标层** | 读写方向刻意不对称。读按 视图 → 会话 → 类型 → 出厂默认 逐层问「这一层有没有显式设过」；写只落 `writeScope()` 那一层。若写入时「顺手把下面几层也写一遍」，用户一次临时调整（视图级）就会变成这个会话甚至这个类型的默认值——而他从没同意过。反过来若写入时「顺手把上面几层清掉让改动生效」，被清掉的那一层是另一个视图或另一个会话的东西 | SESS-007 |
| **判断「这一层有没有这一项」用 `contains()`，不看值是否为空** | 用户把一项清空（编码留空、掩码清空）是一次**真实的**设置。按「值是不是空的」判断命中会越过这一层去取下面那层的值，于是用户发现自己清不掉这一项——重开又回来了，且没有任何提示 | SESS-007 |
| **名称过滤的模式是**每条表达式自己的**，不是整份过滤器的一个开关** | 把模式做成一个字段的话，用户在下拉框里换一下模式会让**所有已有表达式**被静默重解（`README.md` 从「精确名」变成「正则」再变成「通配符」），而界面上看起来什么都没变。行首前缀（`=` / 无 / `re:`）让每条表达式的含义写在它自己那一行上，下拉框只决定「下一次新增的表达式用哪种模式」。`toDeclarationText()` 因此**总是**把前缀写出来，「读回来的过滤器」与「写出去的文本」永远同一语义 | FILT-002 |
| **通配符模式没有前缀，代价是行首出现 `=` / `re:` 时会被当成前缀** | `*.cpp` 是这类输入框里最常见的写法，给它加前缀等于让最常见的用法最难写。这个边界是有意的，两种绕法都留好了：精确名 `=foo` 写成 `= =foo`（前缀后允许空白），正则/掩码把 `re:` 当字面量就写 `re:^re:.*$`。测试里有一条用例把这个边界与两种绕法一起钉住，免得下一个人「顺手」把前缀放宽 | FILT-002 |
| **三种模式都是「整名匹配」，正则不是子串搜索** | 切换模式只应当改变**表达力**，不应当改变**匹配范围**。若正则按子串搜索实现，用户把写好的一条从通配符切到正则，结果会从「名字恰好是 abc」变成「名字里含 abc」——而界面上只动了一个下拉框。要搜子串就写 `.*abc.*`，多敲几个字符换来「切换模式不会悄悄多放进来一批条目」 | FILT-002 |
| **正则的整名匹配用「跑一次普通匹配 + 检查覆盖整名」实现，不用 `anchoredPattern()`** | 把用户的表达式包进 `^(?:…)$` 会连同他自己写的 `^` / `$` / `(?m)` 一起塞进括号里，行为随之改变（`(?m)` 出现在中间就失效了）。检查 `capturedStart() == 0 && capturedLength() == 名字长度` 既保住「整名」的语义，又让用户写的锚点按 PCRE2 的规则解释 | FILT-002 |
| **只有正则模式走超时保护，通配符与精确名直接在当前线程判定** | 通配符走的是本仓自己的 NFA 模拟（复杂度 O(名字长度 × 掩码段数)），精确名是一次字符串比较，两者都不存在回溯爆炸。把三个模式都塞进工作线程只会让每一次判定多付一次线程同步的代价，换不到任何安全性 | FILT-002 |
| **超时保护自己做（工作线程 + 截止时间），不用调 PCRE2 的 match limit** | Qt 5.15 的 `QRegularExpression` **没有**匹配超时（`setMatchTimeout()` 是 Qt 6.0 才加的，已在本机头文件里逐字核对）；Qt 内建的 PCRE2 match limit 既不可配、也不通过 Qt 暴露——Qt 把「超出 limit」与「不匹配」报成同一个结果，于是**拿不到**「这次是被放弃的」这个事实，而第 2 条要求的正是「记录为错误条目」。另一个选择（直调 PCRE2）要把第三方库带进交付物，与本仓「一个 exe 分发」的目标相冲 | FILT-002 |
| **超时的收尾是「记不确定 + 放行 + 报一条问题」，不是「记不匹配」** | 与 FILT-003「元数据缺失放行」逐字同源。把「判不出来」当成「不符合」，用户在界面上只会看到一个少了一批文件的结果，而他唯一能想到的解释是「这软件坏了」。`NameMatchOutcome` 因此是三态：命中 / 不命中 / 不确定 | FILT-002 |
| **超时之后丢掉整个工作线程、换一个新的** | 被放弃的那次匹配无法中断（`QThread::terminate()` 可能在任意指令处中断，留下持锁的 mutex 与半构造的对象，比慢一次糟得多）。线程池容量是 1，不换的话后续每一个条目都会排在它后面、于是**全部报超时**——用户看到的是「整个过滤器全红了」，而不是「这一条表达式有问题」 | FILT-002 |
| **断路器：同一条表达式连续超时到上限就停用它，并记一条问题** | 这是「换线程」的配套上界：每条被放弃的表达式最多泄漏一个线程。它同时把「全红」重新变回可归因的一件事——界面能明确说出「第 2 条表达式已停用」。「连续」二字也是刻意的：成功一次就归零，否则偶发超时会在长时间扫描里攒成一次停用 | FILT-002 |
| **静态回溯预检只报「被可变量词修饰的组，组内还有可变量词」，只提示不阻断** | `(a+)+` 在结构上必然爆炸，值得提前提示；而 `(a\|b)*`、`(a{2})+`、`(a+){2}` 是常见的**无害**写法——固定次数的量词每轮恰好吃同样多字符，回溯是线性的。误报的真正代价不是「多一条提示」，而是用户**从此不看这个提示**，于是它连本该拦下的那一类也拦不住。为什么是「提示」不是「拒绝」：用户可能真的知道自己在写什么（例如只对着很短的样本用），而拒绝会把一条能用的表达式变成不可用 | FILT-002 |
| **「什么叫一行合法的表达式」只有一份实现（`analyzeNameFilterLine`）** | 解析器、界面的实时校验、解析之后的复验都调它。两份实现必然分家，而分家的表现是「界面上没报错、判定时却永远不匹配」——用户完全无法自查。这个函数同时也是「非法正则不生效」能被**反向验证**的关键：写坏的行返回 `hasExpression == false`，因此它不会被任何调用方拿去过滤 | FILT-002 |
| **组合语义表是数据（`nameCombineModeTable()`），标签与解释都从它取** | 「界面显示的语义」与「判定用的语义」必须由同一份数据推出。两份必然分家，而分家的表现是「提示里写着『任意一条命中就保留』，程序做的却是『一条都不命中才保留』」——用户按提示去改，越改越不对。把它做成可注入的表还带来第二个好处：自检能拿一份**故意写坏**的表（两种语义共用同一句解释）证明它真的会报 | FILT-002 |
| **三张表都做成函数内静态，不返回临时容器** | `matchModeRow()` / `combineModeRow()` 会把表里某一行的地址交出去。返回临时 `QVector` 的写法让那个指针在函数返回的一瞬间变成悬垂指针——本轮的第一版就是这样，现象是 `nameMatchModeLabel()` 偶尔返回空串、严重时直接崩溃，而崩溃位置离这张表很远 | FILT-002 |
| **任务闭包按值捕获正则与名字** | 被放弃的匹配会在工作线程上继续跑，而它的持有者（本次 `decide()` 的调用栈、乃至整个 `NameFilter`）那时可能已经销毁。引用捕获就是悬垂引用，现象是「过滤器偶发崩在毫不相干的地方」。这条不变式无法用行为断言稳定地证明（崩不崩取决于内存有没有被复用），因此另有一条读源码的用例把写法本身钉住 | FILT-002 |
| **预设的导出格式只有一套，第四、五条要的落盘与内置预设留给 FILT-007** | FILT-007 要的是「预设库」（存储位置、内置预设、增删改、画廊），而 FILT-002 第 5 条只要「可命名并导出」。本轮给到「命名 + 可移植文本往返」这两半，把格式定义在 `namednamefilter` 这一处并写明 FILT-007 应当复用/扩展它——两份格式必然分家，而分家的表现是「我导出的预设导入不回来」 | FILT-002 |
| **预设正文的末尾空行在导入时清掉，记录开始前的空行不算「正文已开始」** | 记录之间那一个空行会被当成声明体的一部分（记录内部也可能用空行分组，不能一概丢），留着它会让「导出 → 导入 → 再导出」的文本每次多一个换行，往返不再稳定。而记录开始前的空行必须让元信息块**继续**：`[预设] x` 之后空一行再写 `组合:` 是很自然的写法，把它当成正文会让那行元信息被塞进声明体 | FILT-002 |
| **内容过滤必须显式启用，配了规则不等于启用** | 它的代价与其余五段不同阶：`maskfilter` / `attributefilter` / `namefilter` 只看名字与元数据，在扫描阶段就能丢掉条目；内容过滤要先把文件读进来。因此 `ContentFilterEnablement::active()` 是「启用**且**有规则」，与 `FilterLayerState::active()` 同形——一个「配过规则但关掉了」的过滤器必须是**不生效**的，而界面上「已配置，未启用」与「未配置」也要是两句不同的话。把一个代价这么高的过滤器做成「写了就生效」，用户会因为某一轮排查时随手试的一条规则而在之后每次比较里默默付读全部文件的代价 | FILT-004 |
| **行过滤器的输入是**原始行**，不是在忽略规则处理之后的行** | 第 3 条要求的顺序是「先过滤行、再应用忽略规则」。反过来做（先按忽略规则规范化、再按规则过滤）会让**参与比较的行集随忽略规则变化**：同一条「排除时间戳行」的规则在「忽略空白」开着时能命中，关掉之后就不命中——而用户认为这两件事毫不相干。`LineFilter::excludes()` 因此只接受原始行，测试里有一条用**真的** `Text::compare` 把这件事钉住：先过滤再忽略空白，差异为零；只忽略空白不过滤，差异仍在那一行 | FILT-004 |
| **内容过滤的正则是**子串**匹配，名称过滤器的正则是整名匹配——刻意不一致** | 两侧问的问题不同。名称侧问「这个名字是不是符合某条命名规则」，一个名字的片段不是一个名字（`.*abc.*` 写出来才等价）；内容侧问「这一行里有没有我要找的那一段」，这正是 grep 的语义，`2026-` 与 `error:` 这类写法在日志过滤里是绝大多数。把两侧统一成同一种，必然有一侧的常用写法变得不可用——这处不一致是有意的，理由写在 `contentfilter.h` 顶部 | FILT-004 |
| **行首前缀的匹配先跳过行首缩进，与 `namefilter.h` 刻意不一致** | 内容过滤的声明经常是**从被排除的样本上抄下来的**（用户从日志里复制一行，它带着缩进），而缩进对齐在声明文本里是常态。不跳缩进的话 `  =   EXACT LINE  ` 会被解析成通配模式——那条规则看起来完全正常（因为它照样解析成功、照样有个表达式），实际上变成「匹配字面量 `= EXACT LINE` 的行」，永远不命中。名称过滤器做不到这一点是因为它的表达式是用户从头写的、没有「抄一行样本」这个来源 | FILT-004 |
| **关键字节过滤对文本输入判「不适用」，空规则集不构成约束** | `KeyByteOutcome` 是三态：接受 / 拒绝 / **不适用**。被指定按关键字节过滤而条目其实是文本时，正确结论是「这条规则与它无关，放行并报出来」，而不是「它不含那个字节序列，所以拒绝」——后者会让一批文本文件凭空消失。同理，一条规则都没写时不构成约束（`Accepted` 且不给理由），因为「空的规则集」的直觉含义是「没有约束」，而把它读成「全部拒绝」会让一个还没配好的过滤器静默吃掉整次比较 | FILT-004 |
| **只有通配模式命中不了空行，这是刻意保留的** | 掩码的 `MaskSubject::isValid()` 要求名字非空（`*` 与 `?` 都不匹配空串），于是行过滤器里 `*` 命中不了空行，而 `re:^$` 是**唯一**能明确表达「去掉空行」的写法。曾经有一版实现为了「顺手」在入口处对空行提前返回 `false`，把精确与正则也一起挡掉了——那条报错提示里教用户写的 `re:^$` 因此永远不生效。空行在日志与文本过滤里是一个真实的类别，必须能被单独表达 | FILT-004 |
| **写入目标层缺失时返回失败，不退而写入别的层** | 用户的动作是「保存到当前会话默认值」。悄悄写到视图层会让他关标签后以为设置丢了、而它当时确实生效过——比直接报「这一层暂时不可用」糟得多。这类「看起来成功了」的降级正是最难查的一类 | SESS-007 |
| **`changed` 按**有效值**是否变化发，不按「有没有写这一层」发** | 往会话层写、而视图层已有同一条时，值确实存进去了（用户要的就是这个），但用户看到的值没变。这时发 `changed` 会让状态栏与标签上的脏标记白抖一次，而抖动的来源极难归因到某一次赋值。本类因此**不转发**三层的 `changed`，自己算有效值 | SESS-007 |
| **出厂默认来自声明（`SettingItem::defaultValue`），且排在调用方的 `fallback` 之前** | 三层记的都是「用户设过的值」，链的最后一环只能来自声明。把 `fallback` 排在声明之前，一个已声明默认值的设置项会因为某个调用点传了个临时值而读到它，且从结果上看不出异常。另外出厂默认必须**按 `normalized()` 归一后**再交出去：直接返回声明里那一份，会让「带一个末尾空行的掩码清单」变成 2 条，而界面显示 1 条 | SESS-007 |
| **`clear()` / `remove()` 只作用于写入目标层** | 类型层是所有新会话共用的默认值，被一个会话的「清空」带走等于一次跨会话、不可逆的破坏性操作。把范围写在函数名上（另有 `clearLayer(scope)`）比在文档里解释可靠 | SESS-007 |
| **「本次修改将保存到 X」与「切换作用域」的文案是服务层的可测数据，不写进对话框** | 第 2 条的要求是「一眼看出改动的去向」，而这句话会出现在下拉项 tooltip、状态提示两处。写在对话框里必然演化出两种说法。做成 `writeDestinationText()` / `scopeSwitchNotice()` 之后，三种作用域各有一句话被逐条断言，而且可以断言「切换作用域**不会**把已应用的改动搬走」——一个把它写成迁移命令的文案会让用户切换完去检查原来那一层，发现值还在，进而认为切换坏了 | SESS-007 |
| **「关闭标签会丢弃视图级设置」也做成纯函数（`planViewScopeClose`）** | 第 3 条里可无界面验证的那一半。视图层一条都没有时**一个字都不问**——视图级设置用得多的地方（每次打开都调一下再看）如果关标签时也弹一次，用户会学会闭着眼睛点「确定」，于是真正的提示也一起失效。这条「没有就不问」正是这条例最容易写过头的部分，所以它必须能被单独断言 | SESS-007 |
| **丢弃视图层时只对有效值真变了的键发 `changed`** | 视图层的值与会话层相同时，丢掉它用户看不出任何变化。为它发信号会让状态栏与脏标记白抖一次，理由与 `setDirty(false)` 去重那条同源 | SESS-007 |
| **补丁应用的事务边界是「一个已存在普通文件的替换」** | 创建、删除、多于一个改动目标在**写盘前**就拒绝。把「多文件事务」一起做进来的诱惑很大，但本模块唯一的原子原语是 `QSaveFile`（单文件字节替换）；用它去实现多文件事务，能得到的只有「前 3 个成功、第 4 个失败」——正是 PAT-002 明文禁止的中间状态 | PAT-002 |
| **回滚失败必须报 `RecoveryRequired`，绝不能报成回滚成功** | 备份文件是留给用户恢复的。把它说成「已回滚」会让用户以为目标文件已经回到原状、于是不再去检查，而现场其实处在「一半新一半旧」的状态。宁可多一个刺眼的错误，也不要一个看起来成功的假结论 | PAT-002 / PAT-005 |
| **应用计划的不可变性由 `shared_ptr<const Data>` + friend 保证** | 审查过的计划若能在用户确认之后被换掉 `resultBytes` / 路径 / hunk 选择 / 源快照，「用户确认的内容」与「实际写入的内容」就有了第二个事实来源，而分歧是静默的。做成不可变之后，确认与执行之间**不可能**出现内容漂移 | PAT-002 |
| **VCS 两侧一律是只读快照，快照寿命绑在接收会话上** | 直接拿工作副本当比较源，会被用户在比较过程中改掉，「看到的结果」于是不可复现。快照放进 `QTemporaryDir` 并把所有权延长到会话关闭，一次比较才有确定的输入。**物理只读权限不能替代接收视图的只读编辑约束**——用户仍然需要能选中、复制、滚动 | VCS-002 ~ VCS-010 |
| **Git 一律走安全独立参数，并清洗继承的 `GIT_*` 环境** | 用 shell 拼命令会让「文件名带空格 / 换行 / 前导破折号 / 冒号」直接变成命令注入；而继承来的 `GIT_DIR` / `GIT_INDEX_FILE` 会让查询读到**另一个仓库**，症状是「结果莫名其妙」，与原因完全看不出关系。因此 `--literal-pathspecs`、`--end-of-options` 与环境清洗是必需项，不是优化 | VCS-001 |
| **Blame 只读、后台算、关闭即取消；局部能力缺失要如实置灰** | 追溯一个几万行的文件必须离开 UI 线程，且关闭视图不能等后台任务（否则关标签卡住），所以回调进视图前要判代次与取消位。另外「忽略空白改动」「跨重命名追溯」在当前后端里**没有承载参数**——这种情况的正确做法是界面置灰并写明原因，而不是在视图层假装实现（那会让用户以为追溯结果包含了重命名历史） | VCS-012 |
| **「默认值必须保守」写成可执行的契约（`safetyContractViolations()`），不是文档里的一句话** | 这份模块的三类默认值有一个共同点：**错的方向是不可逆的**（永久删除不进回收站、直接覆盖会盖掉较新的内容、不保留时间戳会让一次复制把整棵目录树的修改时间刷成今天）。写进文档的那条要求会过期，而下一个人读到它时已经晚了；写成 `QStringList safetyContractViolations()` 之后，出厂默认值上调用它必须为空，任何一处被改成不保守的方向都会指出来——`Tests/FileOpsOptions` 的 A 组就是拿它反向验证的。**注意「跳过已存在的目标」不算违反**：它虽然也不问，但不覆盖任何东西，把它算成违规会让这条契约退化成「必须等于 `Ask`」这个与安全性无关的同义反复 | OPT-005 |
| **目标不存在时不问、也不给提示，与覆盖策略无关** | 少了这一条，默认的「逐个询问」策略会在一个**空目录**里逐条追问「要覆盖吗」——而那里根本没有东西可被覆盖。用户点几次就学会闭着眼睛确认，那时真正的「目标较新」提示也一起失效了。同一处还承担第二条职责：**「目标文件较新」必须单独说清代价**（「覆盖会丢掉目标文件里较新的内容」），因为只说「目标已存在」时用户会顺手点「全部覆盖」，而这一批里恰恰混着几个比源文件更新的目标——那是真正的数据丢失 | OPT-005 |
| **「保留哪些元数据」是三个独立布尔项，不是一个集合类型的设置值** | 设置仓库的值模型只支持 布尔 / 整数 / 单值字符串，界面控件也是按这个模型一一对应的（勾选框 / 数字框 / 下拉框）。要表达多选集合就得给模型加第四种值类型，而那会同时牵动校验、导入导出、控件生成与往返测试四处——为一条设置项不值得。三个独立键逐项可测、导入导出免费可用，而「集合」这个概念由服务层的 `MetadataPreservation` 恢复。三项**默认全开**：保留是无损方向，关掉才会丢信息 | OPT-005 |
| **阈值以「兆字节」存、以「字节」用，`0` 表示关闭确认** | 设置文件是可读 JSON、用户会自己打开看，`104857600` 与 `100` 相比后者一眼能对上界面上的数字。代价是两个单位并存，因此换算只留 `largeFileConfirmMegabytesToBytes()` / `largeFileConfirmBytesToMegabytes()` 一对函数，并有往返用例钉住。**`0` 必须是「关闭」而不是「0 字节以上都要确认」**：后者用户永远关不掉这个确认，只能把阈值设成一个天文数字。判定用 `>=` 而不是 `>`——用户设 100 MB 的意思是「碰到 100 MB 的先让我看一眼」 | OPT-005 |
| **认不出的设置值回退到默认值，并把回退本身报出来** | 设置文件可以被用户手改。悄悄回退到默认值会让用户以为自己的设置生效了；而悄悄**采用**一个认不出的值（比如把 `Permanent` 当成 `permanent`）更糟——那是按用户没表达过的意图执行一次不可逆操作。因此 `fromValues()` 走 `fallbacks` 通道把每一次回退记下来，`validate()` 把它连同内部一致性问题（阈值不是整兆字节、为负）一起交出去；展示与否由调用方决定，但**丢不掉**。同理，`metadataItemLabel()` 对认不出的标识符返回空串，不编名字 | OPT-005 |
| **数字项的单位后缀进定义表，不在界面按键名硬编码** | 界面曾经对所有整数项一律加 `" pt"`，于是新加一个「体积确认阈值（兆字节）」会显示成 **`100 pt`**——数字对、单位错，而没有任何断言会失败。把 `unit` 放进 `OptionDefinition` 之后，标签由数据推导，`Tests/OptionsDialog` 断言的是 `" MB"` / `" 个"` / `" pt"` 三种后缀各就各位。**新字段必须追加在结构体末尾**，理由见交接文档的坑表 | OPT-005 |
| **缺了私有可选依赖时，流水线降级成「更窄的验证」，不是整体红掉** | LqRibbon 在私有仓 MyClass 里，公开 CI 拿不到；匿名 clone 会以 128 退出，**于是整条流水线在第一步就红掉，它后面的构建与测试从来没有跑过**——而那才是流水线真正该给出的信息。因为 66 个测试工程里只有 2 个依赖它（见 §2），「拿不到依赖」的后果是**少验证 2 个套件、不构建主程序**，而不是「什么都验不了」。降级范围必须来自实测（对每个 `.pro` 跑一次 `LQCOMPARE_MYCLASS_ROOT=/nonexistent qmake`，失败的即依赖方），不能来自 grep | ENG-004 / ENG-002 |
| **降级必须被看见：被排除的套件要同时出现在开头与汇总里** | 「排除掉依赖缺失的套件」与「静默跳过」在日志上只差一句话，可信度却差一个数量级：前者报告「64 个套件通过、2 个未验证」，后者报告「全部套件通过」——后者会把「有 2 个套件从未在公开 CI 上编译过」读成「全都验证过了」。因此 `run-tests.sh` 在 `LQCOMPARE_TEST_SKIP` 生效时，开头打一行排除清单、末尾把 `全部套件通过` 换成 `通过（已排除 N 个套件、未验证）`；全部被排除时仍以退出码 2 报「一个测试都没跑」。同一处纪律还要求：**CI 里的 `run:` 在 Windows 上默认是 pwsh**，所以 bash 语法必须显式声明 `shell: bash`，否则三平台只剩两平台 | ENG-004 / ENG-003 |
| **测试结果的传递不走 stdout，只走文件** | 运行器原本用 Qt 的 `-o -,txt` 把结果同时打到屏幕上。这一路在本机（macOS）正常，在 **Windows（Git Bash）上一行都不输出**，而同一轮的文件产物写得好好的——于是 Windows 腿的日志里没有 `Totals:`、没有失败用例名，合计还被算成 `0 passed`。**「结果怎么被看见」不能依赖一个只在一个平台上成立的约定**：改成只写 `results.txt` / `results.xml`，运行器再把文件 `cat` 回来，路径由运行器自己拼、与上传的 CI 产物逐字一致。同一处还要求跑之前 `rm -f` 上一轮的产物——套件崩在 `initTestCase()` 时不写文件，留着旧的会把一次崩溃显示成一次通过 | ENG-004 / ENG-003 |
| **「汇总」与「明细」的口径差必须在同一句话里解释，不能靠读者自己推** | Windows 腿上一轮报出「34 个套件红了」而合计只有 `26 failed`。两个数字都对：Qt 的 `Totals:` 行只在套件正常跑完时才写，**崩在初始化阶段（或构建失败）的套件一条用例都不计入合计**。日志里没有一句话说明来源，于是它看起来像统计错误——而一个看起来像统计错误的日志，下一步就是被人忽略。运行器因此在合计之外单独打一行「另有 N 个套件没有产出统计行（其用例数不计入上面的合计）：…」。**凡是「总数」与「明细条数」并列出现的地方，都要能解释两者的差从哪来** | ENG-004 / ENG-003 |
| **失败路径的原始输出必须留痕，`>/dev/null` 只允许出现在结果已被别处记下的地方** | 运行器原本把 qmake / make 的输出丢进 `/dev/null`，于是 CI 上 ubuntu 17 个、Windows 26 个套件「构建失败」而**连一条编译器错误都看不到**——只有「哪个套件红了」，没有「为什么红」，而那正是这条流水线要回答的问题。改成写 `<套件>/build.log`、失败时打末尾 20/30 行、并把 `build.log` 加进上传产物之后，下一跑就有原因可读 | ENG-004 / ENG-003 |
| **运行器自己也要能自证：`run-tests.sh --self-test`** | 本文件是全仓唯一的验证入口，而**它自己的**行为错了——并行没生效、超时没打断、失败被报成通过、并发下计数丢失——没有任何东西会变红，恰恰是它本该报告的那类静默失败。所以在临时目录里现造五个探针套件（通过 / 失败 / 挂死 / 硬退出 / 构建期失败）跑两遍（并行 4 与串行 1），逐条断言它该说的话，并断言两遍的**合计逐字相同**。探针跑完即删、绝不进仓库（留在 `Code/Tests/` 下会让全量运行永远失败）；`LQCOMPARE_SELFTEST_KEEP=1` 可保留现场。CI 里 Windows 腿显式跳过并打 `::warning::`（超时那支靠 POSIX 信号，未在 Windows 上实测），**不做成静默跳过** | ENG-003 |
| **并行必须被证明是并行，而且不能改变结果** | 只看「跑得快了」会把「构建缓存命中」误读成并发生效。探针把 `epoch毫秒 名字 start/end` 写进一个共享轨迹文件，事后算最大并发数：并行跑必须 ≥2、串行跑必须恰为 1（实测 3 与 1）。**只统计有头有尾的探针**——挂死与硬退出那两条永远走不到 `end`，把它们的 `start` 记进去会让计数只增不减，于是串行也会报出并发 2，一个测量误差就这样冒充成「并行生效了」。另一半是「结果不变」：`合计` 一行两遍必须逐字相同，因为并发化最常见的错（计数写进子 shell 的局部变量）**只**体现在合计上，单套件输出一条都看不出异常 | ENG-003 |
| **超时失败必须与断言失败分开命名，且不计入合计** | 「构建不过」「用例断言失败」「跑不完被终止」三条路的排查方向完全不同（读编译错误 / 读用例 / 找挂死点），混在一句「套件失败」里会让人先去读代码。因此超时另起一行（`✗ 套件超时（超过 Ns 未结束，已终止）`）、另列一份末尾清单，并明确它同样不算进合计。**实现上刻意不依赖外部命令**：GNU `timeout` 在 macOS 上默认不存在，所以超时是自建的轮询（`kill -0` 判存活 + `wait` 取回真实退出码）；也刻意不用「`wait` + 看门狗子 shell」，那会在每个套件上留一个孤儿 `sleep` | ENG-003 |
| **快照测试要把 `Result` 的每个字段摊开，且期望值必须有独立来源** | 行对齐那条标准要的是「输出**逐字段**相同」，而逐字段 `QCOMPARE` 只覆盖你当时记得写的字段。改成把 `Result` 摊成文本（`fingerprint()`）之后，**给结构体加字段会在黄金串里留下痕迹**，而不是悄悄少守一半。另一半是黄金串的来源：五个夹具的期望值是**手推**出来的（纸上枚举 Myers 的 `ranges` 再算块与行），第一次跑就全绿——实现与手推互不相干地得到同一个值，这才算交叉验证；先跑一遍再粘回去的「黄金」只能证明「今天和昨天一样」，还会把当前的 bug 固化成期望。**逐字段覆盖 + 独立来源，两条缺一条快照就只是装饰** | TXT-002 |
| **夹具里两个不同的量恰好相等，会把真变异伪装成等价变异** | 「全相同 → 单个相同块 / 逐字段快照」那组夹具里每个块都只占一行，于是「前面已累积的行数」恰好等于「块序号」：把 `block.firstRow = result.rows.size()` 变异成 `= index`，9 处变异里唯一没被检出的就是它，而报告出来的是「断言没抓住」——看起来像测试有洞。补一个**中间块占两行**的夹具后立刻检出。**写夹具时要问「这里有没有两个量在当前数据上恰好相等」**：等价变异与漏检的修法完全相反（一个改数据、一个改断言），判错方向会白改一轮 | TXT-002 |
| **两种对齐算法共用一个区间收集器，而不是各攒各的** | 「Patience 与 Myers 必须产出同一接口的差异块，切换算法不改变视图契约」这句话，靠约定守不住：只要两边分别产出区间再拼接，拼接处迟早出现两套合并口径（一个合并相邻同类区间、一个不合并），而现象是「同一个文件用两种算法看，分隔线位置差一格」。把区间表、预算与「受限」标记收进同一个 `AlignmentBuilder` 之后，块边界只有一个来源，`Tests/Alignment` 的契约判定函数能对两种算法跑同一套不变量 | TXT-003 |
| **Patience 的回退是常规路径，不是异常路径** | 「找不到唯一行就回退 Myers」被写成了注释里的一句话很容易被下一个人优化掉（「没有唯一行 → 没有锚点 → 直接给一个大替换块更快」）。但真实文件里唯一行常常只占少数（压缩过的 JSON、日志、生成代码、大段重复的表格行），那样改会让这些文件比 Myers 更差：整份显示成「全删 + 全插」。完成标准「平滑回退到 Myers，而不是退化为零匹配」因此被断言成「与 Myers 的结果**逐字段相同**」——比「配对数大于 0」强得多，后者一个随便配前后缀的实现也能满足 | TXT-003 |
| **Patience 深度到顶也交给 Myers，自己从不宣布「受限」** | 锚点递归的深度与「唯一行」的层数同阶，恶意输入可以让每层只剥离一两条，于是栈深与行数同阶——所以上限是必需的。但到顶之后有两条路：自己产出一个大替换块，或交给 Myers。选后者，因为 Myers 自己的深度与预算上限已经保证了最坏情况有界，直接产出替换块不会更快、只会更不准。**顺带得到一个更重要的好处**：`alignmentLimited` 只有一个来源（Myers 的预算与深度），不会出现「两种算法对同一输入给出不同受限度」这种无法解释的分歧 | TXT-003 |
| **默认算法取「表里第一条已实现的条目」，不是一个写死的枚举值** | 写死 `Alignment::Myers` 时，「默认算法」与「可用算法清单」是两个各写一遍的事实来源；把清单里唯一已实现的那条改成别的算法，默认值会静默指着一条没实现的算法，而现象是「默认看起来还行，只是结果和另一个算法一模一样」。让默认值**从表推导**之后，这条不可能错。同时「未实现算法不得可选」被拆成两道互补的机制：`availableAlignments()` 过滤掉未实现的（保护界面），`validateAlignmentTable()` 报出「规格点名的算法被登记为未实现」（保护规格）——只有前者的话，把某条规格降级成未实现会是一次静默的合规操作 | TXT-003 |
| **切换算法的差异要用「LCS 唯一」的语料证明，不能只看块数** | 首选的区分语料是 `a b a c` 对 `c a b a`：`c` 两侧各一次（唯一），`a` 两次（重复），而两者的 LCS 恰好是 **3 且唯一**（只能是 `aba`）。于是 Myers 必然配 3 行、Patience 只配 1 行，且这个差异是**枚举得出**的，不是跑一遍看出来的。这一点很要紧：块数在两份语料里**都是 3**，只断言「块数变了」的实现会漏掉「位置没变」的情形，而标准 4 的原话是「数量与位置」 | TXT-003 |
| **判等用大小写折叠，不用小写化** | `toLower()` 会把希腊语词尾 sigma 留成 `ς`、却把 `Σ` 变成 `σ`（本机 Qt 5.15.2 实测：`toLower("ΟΔΟΣ")` = `οδοσ`、`toLower("οδος")` = `οδος`），于是 `ΟΔΟΣ` 与 `οδος` 被报成不同——同一段希腊文只要大小写差异落在词尾就永远忽略不掉。`toCaseFolded()` 把 `Σ`/`σ`/`ς` 一并折成 `σ`，这才是「判等」要的语义。这条属于**只有非 ASCII 才暴露**的错误：ASCII 上两种写法完全一样，而本工具的常见用法恰好是跨平台比同一份文件树 | TXT-008 |
| **大小写折叠不跟着 locale 走，土耳其语因此被明确放弃** | 土耳其语正字法里 `I`↔`ı`(U+0131)、`İ`(U+0130)↔`i` 互为大小写，而 `toCaseFolded()` 给出的结论**恰好相反**（`I`/`i` 相同，`İ`/`i` 与 `ı`/`I` 都不同）。接受这个代价的理由是确定性：同一对文件在两台 locale 不同的机器上必须给出同一个答案，否则 CI 与本机会各说各话；而 `QString` 没有任何 API 能把「这份文档是土耳其语」带进来（`QLocale::toLower()` 能吃 locale，但它同时踩中上一条那个词尾 sigma 的坑，而且结果随环境漂移）。取舍以**表格的形式**写在 `textdiff.h` 的 `normalizedLine()` 注释里，并由 `Tests/Text` 的土耳其语用例逐条钉住——写成用例是为了让「哪天有人觉得这是个 bug、顺手改成 locale-aware」当场变红 | TXT-008 |
| **规范化链提到公开接口（`normalizedLine`），而不是留在匿名命名空间** | 「统一规范化链」这件事需要一个**能被指着说的名字**：完成标准里既要求「与忽略空白等规则按统一规范化链叠加」，又要求「土耳其语等特例有明确取舍并记录」，而取舍是否真的生效只有让测试能直接问这条链才验得了。同一处还顺带保证了「只有一个实现」——链里几步、按什么顺序，不存在第二个说法 | TXT-008 |
| **simple folding 不做多字符展开，换来折叠前后长度守恒** | Qt 5.15.2 的 `toCaseFolded()` 是 **simple** folding：`ß` 折成 `ß`（不是 `ss`）、`ﬁ` 折成 `ﬁ`（不是 `ffi`）、`İ` 折成它自己（不是 `i`+U+0307），于是 `STRASSE` 与 `straße` 仍被判为不同。收益是长度守恒（实测：BMP 逐码位 1:1，1189 个码位会变、0 个长度变化；非 BMP 的代理对仍是两个 `QChar`，如 Deseret U+10400→U+10428），于是**规范化后的下标可以直接当原文下标用**——TXT-025 的行内字符级高亮正依赖这一点。**改成 full folding 之前必须先做偏移映射**，否则整段高亮会错位；`Tests/Text` 有一条长度守恒用例专门把这个前提钉住 | TXT-008 |
| **「被忽略的差异仍有弱化标记」必须对**画出来的东西**断言，且底色不另抄一份** | 标准的前半句（不再算差异）服务层就能守住，后半句只在渲染结果上成立：一个把 `Change::Ignored` 直接 `continue` 掉的 `highlight()` 能让服务层全部用例通过，界面上却是「明明有大小写差异，却干干净净什么都看不出来」，用户会以为文件完全一样。因此视图级用例直接读控件的 `extraSelections`——只有真正进了那里的行才会被填色——并把 Insert / Delete / Replace 三种真实差异色从**真实渲染**里采出来，断言忽略色与每一种都不同、且彩度更低。**不另抄一份调色板**：抄一份就有了第二份事实来源，改一边不会让另一边红 | TXT-008 |
| **「两级空白」按「数量 vs 有无」划界，`IgnoreChanges` 不是 `IgnoreAll` 的弱化版** | 这两个模式最容易做成一强一弱（都往「更宽松」的方向调），那样两级的差别只剩「手气」：任何一组语料要么两个都判等、要么两个都判不同，用户看不出为什么要有两个开关。规格给的分界线是**空格的「有没有」**：`IgnoreChanges` 下 `ab` 与 `a b` 仍然**不同**（折叠后那个空格还在），只有 `IgnoreAll` 才判等。`Tests/Text` 的固定语料表就是按这条边界搭的（表里同时有「只差数量」与「差有无」两类行），另加一条**结构不变量**断言判等关系必须嵌套（`Exact ⊆ IgnoreChanges ⊆ IgnoreAll`），加第四个模式时也要满足 | TXT-009 |
| **空白的判定范围按 `QChar::isSpace()`，NBSP 与表意空格都算空白** | 「空白」若只认 ASCII 空格与 Tab，从别的编辑器（Word、网页、macOS 文本）粘过来的行会带着 NBSP（U+00A0）或表意空格（U+3000），表现为「这两行看上去一模一样却怎么也不判相同」，而且**看不见**——归因极难。本机 Qt 5.15.2 实测 `isSpace()` 的范围：空格 / Tab / CR / LF / VT / FF / U+00A0 / U+202F / U+3000 / U+2028 / U+2029 为真，U+200B（零宽空格）为**假**。选「宽」的取舍是刻意接受「NBSP 与普通空格被当成同一种空白」；要保留区分度得靠替换规则（TXT-012），不在这一层做 | TXT-009 |
| **界面与模式之间不按序号对应，改按模式表** | 原写法是下拉里写死三行文案 + 读回时 `static_cast<Whitespace>(currentIndex())`。序号对应是一份**隐式的第二事实来源**：调换两条文案、或在枚举中间插一个取值，界面上的「忽略全部空白」会静默变成另一个模式，而构建、运行、既有用例**全都不红**。改法有两半——值那一半走 `availableWhitespaces()` 取第 i 项；**文案那一半也必须钉住**，否则改成倒序铺下拉同样不会红（本轮第一版就是这样漏检的）。为此界面文案提到 `TextCompareView::whitespaceLabel()` 这个公开静态接口上，让用例能断言「第 i 行显示的字 = 表第 i 项的文案」，同时不复制任何界面字符串 | TXT-009 |
| **落盘的模式值就是枚举序号，因此重排模式表是破坏性改动** | 会话设置与命令行传的都是整数（`text.whitespace` / `MainWindow` 的请求结构），读回时按序号解释。钉住这一点的用例是刻意的：表一旦重排，旧会话会被解释成另一个模式，这是必须配迁移的改动，让它当场变红好过让用户在半年后发现「我的会话选项自己变了」。**已知的遗留耦合**：`textcomparesession.cpp` 里还有一处 `static_cast<Whitespace>(qBound(0, value, 2))`，读回时的合法区间是从枚举写死的 `0..2` 而不是从模式表推出来的；本轮没动它（它不在 TXT-009 五条标准的射程内，且改动会牵到命令行那条路），已记入 handoff 的待办 | TXT-009 |
| **相似度分值取 `2·LCS/(len左+len右)`，不取编辑距离也不取公共前缀长度** | 分子用 LCS 而不是「相同字符个数」，于是**顺序**被计入：`abc` 与 `cba` 不该拿满分，而按字符集合算它们会。分母用两行长度之和（Dice 系数），于是「短行被长行包含」不会自动满分——`int x;` 与 `int x; // 一整段很长的注释` 只有 2·7/(7+30) ≈ 38% 而不是 90% 以上。这是刻意的：只差一个尾注的行按编辑距离几乎相同，但按「这是不是同一个修改」更像一改一加，而阈值本来就允许用户调宽。附带好处是值域天然落在 0–100（`2·LCS ≤ len左+len右`），不必再定一个与阈值不同的上界 | TXT-005 |
| **分值在 `normalizedLine()` 之后算，忽略规则在相似度这一层同样生效** | 两侧都过整条规范化链再算分，于是「忽略大小写」开着时 `HELLO world` 与 `hello world` 的分值必须是 **100**。不这么做就会出现一个自相矛盾的组合：用户开了「忽略大小写」，这两行被判定为「按选项等价」，却同时被判定为「不够相似、不配对」——而这两条结论都来自「自动判等」这一个意图。这一条把 TXT-008 的规范化链从「判等」扩展到「相似」，两条链因此只有一个实现 | TXT-005 / TXT-008 |
| **阈值判定含等号（`分值 >= 阈值`）** | 阈值的语义是「至少这么像」。取严格大于的话，阈值的**字面值**（50）与它实际放行的最低分值（51）不一致，而用户在界面上填的就是那个字面值——他会发现「填 50」与「填 51」放行的行集一样，然后开始怀疑这个设置有没有生效。用例因此专挑「分值恰好等于阈值」的那一对语料，钉住它必须配对 | TXT-005 |
| **配对目标先最大化配对数、再最大化总分值，顺序不可颠倒** | 规格里这个功能的目的是「把相似的行配成一对（修改块）而不是两条独立增删」，也就是**能配就配**；只在配对数相同时才比总分值，用来在「左 1 行可以配右 1 行或右 2 行、都够像」时挑更像的那一对。反过来（先比总分值）会让「一对 90 分」压过「两对 60 分」，于是用户看到两行改动被缩成一处——而规格要的正好相反。这个顺序是 DP 的状态设计，不是末了的排序：写成「先算完所有配对再挑」的版本会在这一步悄悄退化 | TXT-005 |
| **两个工作量上限在开跑之前就判，超限退回按位配对且必须被调用方看见** | 配对 DP 是 O(左行数 × 右行数)，而调用它的是「对齐刚判定出的一段非相同区间」——对抗性输入上那一段可以是整份文件（`Tests/Text` 里那份一万行互不相关的语料），于是 n·m 可达 10^8。单元格上限（512×512）管不住行本身很长的情况（512² 格 × 每行 500 字符 ≈ 6.5×10^10 次比较），所以另有一个字符工作量预算（10^8）——它恰好等于「把每一对行都算一遍 LCS」的 DP 单元格总数，因此「要不要做」是一个开跑前就能算出来的判断，不需要边跑边数。超限时**退回按位配对**（= 这个功能出现之前的旧行为，「少了一个新功能」而不是「结果变错」），并把 `limited` 交出去：静默退回会让「阈值调了没反应」无从归因，用户唯一能想到的解释是「这软件坏了」 | TXT-005 |
| **阈值越界在命令行上**报错**、在会话文件上**钳制**——两条路刻意不同** | 命令行是脚本用的：静默把 `--similarity-threshold 250` 变成 100 会让脚本作者以为参数生效了，而他的脚本从此跑在一个他没指定的阈值上（`--no-similar-lines` 与 `--similar-lines` 同时给也是用法错误）。会话文件是**被手改**的：把坏值退回默认值会改成一个用户没设过的数，钳到边界（「最小 0、最大 100」）是他能理解的东西。同一处还要求「越界 / 非数字 / 小数」三种都算用法错误（退出码 2），因为 `70.5` 被静默截成 70 同样是「改了用户的输入」 | TXT-005 |
| **「一处改动」（`DifferenceRun`）成为按处计数与按处操作的唯一口径** | TXT-005 之后引擎会把**一段**改动铺成若干相邻块（够像的行配成一个替换块、不够像的行各自成删除块 / 新增块），于是「差异块的个数」不再等于「用户在界面上数得出的改动处数」。状态栏、上一处/下一处、复制这一处、命令行摘要这四处若各自按块数算，同一件事就有两套口径，而现象是「状态栏说 3 处、按两次『下一处』就到头了」。归并判据只用「块下标连续」，不需要再看行号：两种对齐算法产出的区间本来就是「相同段 / 非相同段」交替的，两处独立改动之间必然隔着一个相同段。**`Result::differences` 数的是变更块不是行**这个既有事实因此第一次有了名字 | TXT-005 |
| **三方合并必须与块粒度无关：基点相接的同侧相邻非 Equal 块要归并成一处改写** | 这是 TXT-005 带出的**连带缺陷**，不是它的副产品：拆分「一处改写」之后，合并引擎按**块**粒度解读两侧编辑，于是「左改 B→C、右改 B→D」在 `A B C` / `A D C` / 基线 `A B C` 上被读成两组互不重叠的编辑，**基线行 `B` 从结果里消失**（输出 `A\nC\n` 而不是 `A\nB\nC\n`）——这是丢数据，比多报一个冲突严重得多。修法是在 `mergeengine` 里加 `changeRunLength()`，把「基点相接」的同侧相邻非 Equal 块归并成一处再交给合并判定；两方合并的输出行→块映射（`textmergesession`）同样要按「一处改动」口径算归属，否则纯新增行会挂到错误的旧行上。**判据是「基点相接」而不是块类型**：`addBlock` 已保证删除排在新增之前、相邻同类块会合并 | TXT-005 |
| **双窗格用「填充行」对齐，而不是让两侧各显示自己的文件** | 让两栏各自 `setPlainText(自己的文件)` 看起来更简单，但它把「行号与行高严格对齐」变成了**巧合**：某一侧多一行之后两栏就整体错开，滚动同步看起来是「越滚越错位」，而行号槽里的数字与屏幕上那一行也不再是同一件事。本项目的做法是两侧都按 `Result::rows` 铺：某侧缺行就铺一条**空行**并把号码置为 -1（号码槽不画），于是「两边视觉行数 == 模型行数」是结构性的，行号与行高不可能错开。代价是两侧的 `QPlainTextEdit` 里存在**并不对应原文**的行，所以任何按「控件行号 → 原文行号」的换算都必须过 `Result::rows`（查找与跳转已经这么做，见 `searchAndLineJumpUseOriginalLineNumbers`） | TXT-001 |
| **号码槽的数据要能被断言，因此 `TextPane::lineNumbers()` 是公开接口** | 正文与号码来自同一个 `numbers` 数组，却由 `setPlainText()` 与 `setLineNumbers()` 两条路分别铺开。只断言正文时，「号码根本没铺下去」（号码槽全空）与「号码铺成了另一侧那份」（号码与正文整体错位一格）都不会让任何用例变红，而这两种情况下用户按号码读出来的结论都与屏幕上显示的内容对不上。这与「UI 值与 UI 文案同源不同路，两条链都要有断言」是同一条纪律：**同源不同路的两条链，必须各钉一遍** | TXT-001 |
| **「单侧为空」按规格的动词选块类型（`Insert`/`Delete`），不选「能表示」的 `Replace`** | 左侧 0 行、右侧 N 行时，`Replace` 块在数学上同样表达了「这 N 行是新的」，界面上也能画对。但规格写的是「标记为**新增**/删除」，而 `Replace` 向用户表达的语义是「左边原来有内容、被换掉了」。选 `Insert` 还顺带让「整段是**一个**块、`leftCount == 0`」成为一个可断言的形状。**两个方向都要验**：只验「左空右有」时，把插入与删除写反（新增的文件显示成被删除）不会有人发现 | TXT-001 |
| **状态栏的「严重程度」另开一条通道，不塞进状态文本里** | 规格要求在行尾混合时给出**警告图标**，而图标不是文字。若让容器去嗅状态文本里有没有某个词、或让每个会话自己往字符串里贴符号，就等于**反向解析自己刚拼好的那句话**：任何一次文案调整或翻译都会静默让图标失灵，而失败的样子是「图标不见了」——没有任何东西会红。因此 `CompareSession` 提供 `StatusSeverity`（只有 `Normal` / `Warning` 两档，多留一档就会有人随手用错）与配套信号，两条通道**各自去重**：文本没变不发文本信号（状态栏刷新是高频路径），但严重度那一路不能被文本的去重一起吞掉（`setStatusText()` 里那两句的写法就是为此）。图标落在状态栏的**永久控件**上，可见性由窗口按当前会话的严重度切换 | TXT-010 |
| **「混合行尾」做成数据上的谓词，且末尾无换行不算一种风格** | 谓词（`Document::hasMixedEndings()`）与文案（`eolDescription()`）分开：文案是给用户看的，随时可能改词或加计数，判定不该跟着一起动。判据只数 LF / CRLF / CR 三档，**刻意不数 `Eol::None`**——`None` 说的是「文件没有以换行收尾」，它与行尾风格的选择是两条独立的事（第 2 条）。若把 `None` 也算一种风格，任何不以换行收尾的文件都会被报成「混合」，警告图标就一直亮着：**叫狼来了的提示等于没有提示**。两处判据共用同一个 `countEndings()`，并有「两者必须一致」的用例盯着（口径一旦分家，状态栏会出现「Left: LF」旁边亮着一个「混合」图标） | TXT-010 |
| **状态栏警告图标只有一个写入点；同一件事的第二条路要删掉** | 最初的写法是两条路都刷图标（会话的严重度信号 + `refreshStatusBar()`），理由是「刷新状态栏时顺手重算」。听起来无害，实际让两处变异**互相遮蔽**——删掉任何一条，图标的表现一点变化都没有，另一条把它兜住了，于是**两边都测不出来**（本轮实测：两处都报漏检）。保留的是「容器重播」那条：它与 `SessionArea::statusTextChanged` 在切标签时同样重播的既有约定对称。判据是**它必须能被变异测试单独打红**：一个「删掉之后没有任何用例变红」的分支，不是纵深防御，是没人知道的死代码 | TXT-010 |
| **「只比较前 N 字节」不新增主状态，而是复用 `Unknown` + 一维内容证据** | 主状态那一栏回答的是「结论是什么」（相同 / 不同 / 仅左 / 仅右 / 类型冲突 / 错误 / 未知），「只比较了前 N 字节」回答的是**结论覆盖了多少内容**——把后者塞进主状态，同一件事就会有两个说法：一个开了快速模式却真的发现差异的条目，主状态该写「不同」还是「部分比较」？两个都不对，于是必然长出一套优先级规则，而界面、报表与命令行各自都要再实现一遍。做法是让主状态继续只表示结论（限内全同且没读完 → `Unknown`，因为它**确实**是「尚未确定」），另加 `Entry::partialComparison` 这一维表达「结论只覆盖了前 N 字节」。收益是可组合：`Different` + `partialComparison == false`（限内已经证明不同）、`Unknown` + `true`（限内全同但没读完）两个组合自然成立，不需要任何特例。副作用是刻意的：`run()` 早已把 `Unknown` 映射成 `complete == false`，因此一开这个开关整体结果就必然标注为「不完整」——正是验收标准要的那句「明确标注为不完整比对」，而报表那侧的「目录扫描未完成」告警也是免费得来的 | DIR-008 |
| **字节预算卡在「大小不等」短路之后：已经证明的结论不降级成不确定** | 「只比较前 N 字节」最省事的实现是把它放在 `compareFile()` 最前面，反正只读前 N 字节。但那样一来，「两侧大小不同」这个**一个字节都不用读就能证明**的结论会被降级成「部分比较、无法确定」——用户开了快速模式之后，一个 1 KiB 与 1 MiB 的文件会被报成「不确定」，而它其实百分之百不同。这不是保守，是把已经拿到的证据丢掉。所以顺序是：大小不等 → 直接 `Different`（不看预算、不读字节）；大小相等 → 才进分块循环，循环里再用 `want = min(块上界, 预算 − 已读)` 夹逼，保证**不越过第 N 字节**（越界读会让「限 N 字节」变成一个只在日志里成立的承诺）。同一原则的另一面是：限内**发现**差异时也不降级——差异已经被证明，`partialComparison` 保持 `false`、`complete` 保持 `true`。这三条边界（限 == 文件长度算完整、差异在限内算完整、差异刚好在限外一个字节算不完整）各有专门语料钉住 | DIR-008 |
| **崩溃的套件自动补跑一遍 `-v2`，而不是去猜哪一行** | 套件被 abort 掉时**不写 `Totals:` 行**，日志里只剩「没有产出 Totals 行」——知道它死了，不知道死在**哪一条用例**上。猜具体哪一行是浪费：同一个缓冲区越界在 macOS 上大概率跑得好好的（libc 没有 `_FORTIFY_SOURCE` 这道检查），本机结构性看不到。`-v2` 把答案变成数据：每个用例先写一行 `INFO : Class::func() entering`、跑完再写 `PASS`，于是「最后一个 entering 的用例」就是崩之前正在执行的用例。**这条诊断成立的前提是日志要留得住尾巴**——实测 `abort()` 与连 `atexit` 都不跑的 `std::_Exit()` 之下，最后一行 `QDEBUG` 与 `entering` 都还在（Qt 的纯文本日志器逐条写盘），所以判定不依赖任何一次 flush | ENG-003 / ENG-004 |
| **补跑只对「没产出 `Totals:` 行的非超时失败」做，且不碰 `results.txt`** | 范围放宽会把两种排查方向完全不同的事混在一起：超时的套件上面已经单独点名，再补跑一次只会再白等一个超时；断言失败的套件用例名本来就在 `results.txt` 里。所以「**只**对崩溃补跑」里的那个「只」是被断言钉住的（通过 / 失败 / 超时三个探针都不该多出 `verbose.txt`）。同样地，补跑**不传 `-o results.txt`**：首轮那份「崩之前已经跑过的用例」记录是**证据**，让诊断顺手覆盖它，等于让诊断改写证据 | ENG-003 |
| **「启动二进制并带超时等它」只留一份实现** | 补跑要走的是与首轮**逐字相同**的启动路径（同样的看门狗、同样的超时标记、同样的退出码取法），于是把它抽成一个函数，而不是在旁边再写一段轮询。两处各写一份，迟早会在某一处漂移，而「超时」这条路的全部证据（标记文件、汇总里单独一行）正是自测断言的对象——漂移会静默地只影响其中一条路。判据同样落在断言上：把看门狗改失效、或把它的标志位从调用方读回来这一步去掉，自测必须变红（本轮各验一次） | ENG-003 |
| **「有没有产出 `Totals:` 行」不能单独当作「崩溃」的判据** | 这条判据在 Linux 上会把**超时的套件读成正常跑完**：Qt Test 接住 `SIGTERM` 后会自己补写一份统计行再以 `SIGABRT` 收尾（`status=134`、**有** `Totals:`），macOS 则被 `TERM` 直接带走、什么也不写。于是「崩溃 = 没统计行」与「超时 = 有统计行」这两句话**各自只在半个世界里成立**。判据因此写成「**没统计行 且 没超时**」（`hs -eq 1 && to -eq 0`），并且自测的挂死探针**自己先写一份统计行再挂死**——把这个边界在两个平台上变成同一种形状。否则它在 macOS 上恒不可达，**任何本地变异都 redden不了它**（实测：ubuntu 腿因此连红 13 次，且因为自测红了、后面的「运行测试套件」被 skip，整条腿一个套件都没跑过） | ENG-003 |
| **崩溃诊断再进一步：自动抓一份 `crash-trace.txt`，三条纪律缺一不可** | `-v2` 补跑回答的是「**哪一条用例**」，而 `*** buffer overflow detected ***` 这类 abort 要的是「**哪一行 C++**」，那只有调试器能给。因此崩溃判据命中时（同一条 `没统计行 且 没超时`，两者永远对同一个套件说话）自动跑一次 `gdb -batch -ex run -ex bt 40` 或 `lldb --batch -o run -o bt -c 40`。三条纪律：①**不传 `-o`**——首轮那份「崩之前跑过哪些用例」是证据，让诊断顺手改写它等于让诊断改写证据（与 `verbose.txt` 补跑同一条纪律）；②看门狗取下限 `max(超时, 60s)`——调试器冷启动加符号解析比程序本身慢得多，套件超时（600s）会白等，写死 60s 又可能在慢机器上误杀，取下限让「比套件超时更宽裕」在两边都成立；③**挑不到调试器也要留产物**，写明「本平台没有可用的调试器」并给出 `LQCOMPARE_TEST_DEBUGGER` 的指路——**别把「没有栈」读成「没有崩」**。本机 macOS 实测 lldb 在这台机器上根本停不下来（`bt` 报 `Command requires a process which is currently stopped`，或干脆挂住），所以这条能力**在目标平台（ubuntu 腿）才验证得了** | ENG-003 / ENG-004 |
| **「调用了外部工具」这种断言必须由被调用者自己记下来** | 自测里放的是一个**假调试器**脚本，它把收到的 argv 原样 `echo` 出来（`STUB-DEBUGGER-ARGV: …`），用例断言的是**那段回显**而不是「脚本里有没有那个字符串」。原因：gdb 与 lldb 的参数形状完全不同（`-ex run -ex bt 40` vs `-o run -o bt -c 40`），把两者写反、或把 lldb 的 `-o` 也塞进 gdb，grep 源码一样能过——只有让被调用者把实际收到的参数吐回来，「真的这样调用」才可断言。同一条纪律顺带守住「不传 `-o`」：假调试器不代跑进程自身输出的话，「没把结果搬走」在断言上完全不可观察。分帧上还有一处刻意的交叉验证：`stub-lldb` 走按名字分支、`fakebin/gdb` 走 PATH 探测分支，两条路各验一次 | ENG-003 |
| **CI 侧的调试器探测只告警、不失败；诊断产物必须进「跑之前 `rm -f`」的清单** | 「这个平台有没有 gdb / lldb」是环境事实，不是我们的测试结果。把它做成失败条件，会让一条本来只是「答不到那一行」的腿整条红掉，而它本该报告的套件结果一个都没报（ubuntu 腿曾因自测红而整条 skip，见上一条）。所以 CI 只在测试步之前探一次、没有就 `::warning::`。另一半是产物纪律：`crash-trace.txt` 必须和 `results.txt` 一样列进跑之前的 `rm -f`，否则上一轮留下的栈会在这一轮被当成这一轮的读——**一份诊断产物写错的代价，比没有这份产物更大** | ENG-003 |

| **条目状态是「一档主状态 + 三个正交维度」，不是一张越来越长的状态清单** | 主状态回答「结论是什么」（互斥，9 档）；内容证据回答「结论建立在什么上」（未比较 / 部分比较 / 已比完且同 / 已比完且不同…）；时间关系回答「哪一侧较新」；存在性由两侧 `exists` **派生**（不另存字段）。做成一张清单就必然长出「部分比较且发现差异」这类组合，再配一套优先级规则，界面 / 报表 / 命令行各实现一遍。反过来的证据同样重要：`Entry::partialComparison` 原本是**独立布尔字段**，与内容证据里的 `Partial` 是同一件事的两份说法——本轮把它改成 `contentEvidence == Partial` 的只读视图，两份说法只剩一份 | DIR-011 |
| **「两侧均改 / 冲突」只由**有效**基线推导，没有基线时一个都不出现** | 两份当前文件永远推不出「谁先动的手」。所以 `compare()` 的 `baseline` 形参默认为空、`Result::baselineApplied` 如实记录「这次到底用没用上有效基线」，而基线本身要走 `validateBaselineView()`：必须标记有效、必须同时绑定两侧根目录、至少一条祖先记录、键必须是相对路径（绝对路径的键永远查不中任何条目，整条基线会**静默**失效）。基线只**细化**不推翻：只把叶子上的「相同 / 不同」改成「两侧均改 / 冲突」，目录的结论仍由子条目汇总 | DIR-011 |
| **父目录的结论就是子条目汇总本身：`aggregateChildren()` 是引擎与用例共用的唯一实现** | 如果引擎自己写一份汇总、测试另写一份，「父子视图结论一致」就退化成「两套口径今天碰巧结果相同」。共用之后真值表可以脱离文件系统跑（15 行组合 + 掩码排除两行），而引擎那侧只需断言「把子条目喂回同一个函数，结论与写在父目录上的那一格逐字相同」。另外汇总**不许依赖遍历顺序**（目录的枚举顺序来自文件系统，不归我们决定），因此真值表要把子条目反过来再跑一遍 | DIR-011 |
| **「为什么是这个状态」——判据做成可读数据，且三节恒定出现** | 主状态 / 内容证据 / 时间关系各有一张 `…Descriptor` 表，表里带机器可读标识符与图标键，界面按表铺下拉、按表读回，`statusReasonLines()` 也按表产出「准则 / 覆盖策略 / 最终结论」三节。三节**恒定**出现（空小节写「（无）」）：按需省略会让读者分不清「这一节没有内容」和「这一节根本没实现」，而排查一个诡异状态时最要紧的恰是这一区分。状态图标另有 9 个中性灰描边 SVG（不靠颜色区分），因为灰度打印与色觉障碍下颜色是第一个失效的信息 | DIR-011 |
| **校验函数把「表」当参数，而不是读模块内部的那张表** | 若 `validateMainStatusTable()` 自己去读 `mainStatusTable()`，喂一份**故意写坏**的表进去也永远绿——自检变成恒真。因此校验函数收 `const QVector<…> &table`，用例可以把造出来的坏表递进去；同类的还有内容证据表的一条**蕴含关系**（`provesContentIdentity` 蕴含 `coversWholeContent`：声称证明了内容相同，就必须真的读完了全部内容） | DIR-011 |

| **递归三档不新增 `Options` 字段：`(recursive, maximumDepth)` 就是全部存储** | 三档（仅根目录直属条目 / 递归深度 1 / 完全递归）在引擎里**已经全部可达**——`recursive` 与 `maximumDepth` 的三种组合就表达了它们。再存一个 `RecursionTier` 字段，同一件事就有两份说法，而两份迟早分叉：`recursive == false` 配 `maximumDepth == 7` 是一个**合法存档值**（引擎里与「不递归」同行为），第三个字段一进来就必须规定它是哪一档，于是「反查」与「归一化」两个概念同时冒出来，`.lqc` 往返里那个 7 会被静默改写成 0。因此反查 `recursionTierOf()` **按行为归类**（`!recursive \|\| maximumDepth <= 0` 是第一档），而 `setOptions()` **不做任何归一化**。`Full` 档写回时也**只碰 `recursive`**，仅当上限自相矛盾（`<= 1`）才提到缺省——「又选了一次完全递归」不该把用户另设的上限重置 | DIR-003 |
| **档位与深度是两个字段的两个写入者；`fromUser` 那样的「遮羞参数」要删掉** | 深度控件在**用户换档**时由换档处理器写一次（写的是档位决定的深度，不是控件里那个旧值，否则切到「递归深度 1」后控件上还留着 128，显示与实际生效就成了两回事），在**程序性路径**上由 `setOptions()` 写一次。原先这两处合并在 `applyTierToControls(tier, bool fromUser)` 里，看起来是纵深防御，实测是**互相遮蔽**：程序性路径下那条写入永远被紧接着的覆写或一次恒等写入盖掉，于是把它删掉**没有任何用例变红**（本轮变异 M13 第一次跑就是 green）。判据与 TXT-010 那条一致——**它必须能被变异测试单独打红**。收成一处之后 M13 变红 | DIR-003 |
| **边界上的子目录是「未枚举」，既不是「相同」也不是「不存在」** | 深度边界上的子目录节点**仍在结果里**（两侧都在，只是没进去看），状态是 `Unknown` 而不是 `Same` / `LeftOnly`。这条是规格里那句「内部未枚举内容不得计为相同或不存在」的直接落地。解释文案只有一份实现 `recursionBoundaryExplanation(options)`，引擎写进 `Entry::explanation`、用例比对同一句，免得界面与报表各抄一句、三份迟早分叉。文案里印的必须是**实际生效**的深度上限：调用方直接传一个超界值（300）时引擎夹到 256，照着原值印会让提示里的数字与真正拦住扫描的那个数不是一回事 | DIR-003 |
| **换档触发重扫，而不是对已扫描结果重新过滤** | 档位与深度上限决定「哪些条目**进**结果」，不在结果里的条目根本没有行可以过滤——按显示过滤会得到一个自洽但错误的结果集。所以视图侧只提供 `rescanRequested()` 这一个信号，且**刻意不带路径参数**：路径的唯一来源是会话，视图再递一份路径等于给同一件事开第二个入口。会话收到后按当前设置 `reload()`；在还没开始比较的会话上该信号只是把档位记下来，不该弹一句「请选择文件夹」 | DIR-003 |
| **循环符号链接的判据是「跟随它会不会回到自己的上级（含自身）」** | 引擎里符号链接**一律不跟随**（`classify()` 只比较目标串、`walk()` 只在 `Kind::Directory` 上递归），所以无限递归在结构上不可能发生——但规格要的是「**检测并终止、记录为错误条目**」，因为用户看到的是「这棵子树没被走下去」。判据写成「解析出来的目标 == 链接自身，或是链接自身的**严格**上级」，一条同时覆盖 → `.`、→ `..`、越过扫描根、绝对目标恰好是扫描根 / 其上级、以及绕一圈回到自己（`self/../cycle`）；指向兄弟或下级子树的链接**刻意放过**（那里没有这个链接，跟下去不会回到起点）。两个实现细节有专门用例钉住：相对目标按**链接所在目录**解析（`..` 在链接自己的位置上的含义才是它真正的含义），以及根目录的拼接不能写成 `//`（写了的话「目标恰好是 `/`」这种真循环会被漏判）。两侧都查——循环只可能出现在一侧 | DIR-003 |

| **状态着色是一张表，不是视图里的一段 `switch`** | 视图里原来那九行 `switch (status)` 是状态的**第二份清单**：新增一档状态时没人会回来补一行，界面于是静默少一种颜色（DIR-011 的筛选下拉踩过同一个坑）；而「高对比 / 色盲友好」要的是把九档**一起**换掉，散落的 `switch` 做不到，只能复制九行再改九处。颜色因此收进 `colorSchemeTable()`，视图只负责查表 | DIR-012 |
| **图标跟着配色一起换，颜色永远只是辅助** | 九张状态图是中性灰描边（DIR-011 的取舍：靠形状不靠颜色，灰度打印与色觉障碍下信息不丢）。中性灰在深色主题上几乎看不见，所以着色走 `CompositionMode_SourceIn`——只改 RGB、保留 alpha，描边的抗锯齿边缘不被抹平。「深色主题的图标必须明显更亮」由用例正面钉住；只断言图标非空的话，一个永远返回原图的实现照样绿，而它的现象是「深色主题里状态列一片空白」 | DIR-012 |
| **「色盲友好」是被判据守着的，而不是一个标签** | 它若只是一个布尔字段，就是一句没人验过的声明。`colorBlindSeparation()` 走 Viénot/Brettel 的线性 RGB → LMS → 替换缺失锥细胞 → 回 RGB，返回方案内两两之间的**最小**模拟距离；自称色盲友好的方案过不了 `kMinimumColorBlindSeparation` 会让校验报错。实测：出厂方案那对「看着差很远」的 `#2055a0` / `#5a3fa0` 在绿色盲模拟之后只差 **2.8/255** | DIR-012 |
| **色值一律用 `#rrggbb` 字符串，不用 `QColor`** | `QColor` 属于 QtGui，存它会让 `Services/Folder` 连带把整个服务层拖进图形依赖，`Tests/StatusPalette` 那个刻意 `QT -= gui` 的工程就再也建不起来；而「解析十六进制 → 算相对亮度 → 算对比度」全是纯算术，本来不需要图形栈 | DIR-012 |
| **换配色只重绘，绝不重扫；而且不重置模型** | 配色只影响画出来的样子，不影响结果集。一次换配色去重扫整棵目录树，在大目录上要等好几秒，而用户只是换了个颜色。重绘走**逐行** `dataChanged` 而不是 `beginResetModel()` / `endResetModel()`——重置会把展开状态、选中项与滚动位置一起丢掉，换一个配色不该让用户重新展开一遍树、重新挑一遍要看的行。「没有重扫」是一句不可断言的话，因此由 `rescanRequested` 的 spy 计数为 0 正面钉住 | DIR-012 |
| **配色文件按状态标识符当键，不按数组下标** | `.lqcolors` 是**分享出去**的东西。用下标当键时，主状态表下一次新增一档，所有已经发出去的文件就会整体错位一格——而那些文件不会报错，只会把颜色配到错误的状态上 | DIR-012 |
| **导入失败时一个字段都不改** | 调用方手里那份方案是界面上**正在用**的那套。一次失败的导入把界面留在半套配色上，比直接说「没读进来」糟得多。因此解析器把结论写进独立的临时对象，结构与语义两层校验全过才交出去；结构层与语义层**都要做**——只做结构层的话，一个被人手改坏了对比度的文件会被当成合法的自定义配色装进界面，而用户看到的只是「字看不清」 | DIR-012 |
| **「弱化色可以低于对比度下限」这个例外刻意不留** | 例外本身需要有东西守得住「确实弱到该弱的程度」，而那件事没有客观判据，于是它会退化成一句没人验过的声明（§6 那条纪律）。唯一多出来的约束是弱化色必须与「相同」不同色：两者同色时用户分不清「两边确实一样」与「根本没比」，而这两件事的处置完全相反 | DIR-012 |
| **配色不进 `Options`，也不进会话快照** | 它改的是画出来的样子而不是结果集，因此与 `Options` 无关、不参与会话快照与命令行。界面上它铺在「显示」工具条而不是「文件夹选项」那一条——放在「子目录 / 逐字节比较内容」旁边会让人以为它影响比对。导入的自定义配色只活在本次会话：把它持久化成第四个设置项是一次独立的产品决定，属 View 页 `OPT-*` | DIR-012 |
| **「没装 git 时版本控制命令是灰的」只由 `updateCommandState()` 守，视图里不加第二道守卫** | 命令注册中心是唯一出口（UI-024），而 `VcsView` 的唯一入口是 `CommandRegistry::trigger`——命令被禁用时那条路径根本走不到，写在 `showVcs()` 开头的守卫因此是**不可达的死代码**（本轮自审时删掉，并在 `updateCommandState()` 里留注释说明为什么只有一处）。真正要守的不是「视图这次被调用了」，而是「命令在**任何**入口都是灰的且原因是同一句」：判据把假后端塞进主窗口，逐条查清单里每个 `Vcs::isVcsActionId` 命令的启用状态与禁用原因，再用 `probeAvailability()` 对同一个后端算出期望值——清单本身也只有一个来源，主程序与用例都不许自己拼 `vcs.` 前缀 | VCS-001 |
| **仓库识别的缓存按「规范化绝对路径」分桶，而且只缓存定论** | 缓存要回答的是「这个路径属于哪个仓库」，所以分桶键必须是路径本身：键错了会拿 A 仓库的结论回答 B，而这**不会报错**，只会让界面在正确的目录上显示错误的变更列表。规范化（相对路径按当前目录补全 + `cleanPath`）是为了让 `./sub` / `sub` / `a/../sub` 这类等价写法命中同一桶。只缓存**定论**（探到了仓库、或明确「不是仓库」）：后者也入缓，因为「反复去问一个非仓库目录」是纯浪费；超时 / 被取消这类**说不清**的结果不入缓，否则一次网络盘超时会把「暂时问不到」永久固化成本次会话的结论 | VCS-001 |
| **探测在锁外做，并用自增 `generation` 丢弃「后端被换掉那一刻在途」的结果** | 锁里只取「后端指针 + 代次」，`detectRepo()` 在锁外跑：慢后端（网络盘上的大仓库）会把界面线程堵在缓存锁上，而缓存的全部意义正是让界面不等。取到结果后要先比代次再写：探到一半后端被替换时，那个结果属于**上一个**后端，写进去会让用户看到一个已经不该被引用的结论——这类 bug 在单线程测试里永远复现不出来，所以用例用假后端的 `duringDetect` 钩子在探测中途换后端 | VCS-001 |
| **缓存活在视图里，跨模式切换保留、显式刷新时失效** | `VcsView` 的 diff / log / blame 三个模式共用一份工作副本与一个仓库，用户在它们之间来回切换只是在换视图，并没有表达「重新去问」——每次切换都重探一次会在慢仓库上反复卡几秒；而点「刷新」正是那个表达，所以只有它清掉当前路径的缓存。缓存的持有者是视图的 `Private`（`QSharedPointer`，工作线程只拿到一个值语义的副本），后台线程因此永远不会碰到已析构的视图 | VCS-001 |

| **BOM 差异算不算差异是「比较规则」，不是 `Document` 的属性** | 同一对文件在「忽略 / 视为差异 / 按编码自动」三个策略下必须给出三种结论，而 `Document` 描述的是**文件本身**（它有没有 BOM、按什么编码解码）。把策略放进 `Document` 会得到「同一个文件在两个策略下是两个不同的文件」这种怪事，也会让会话快照与命令行各自抄一份。因此 `CompareOptions::bomPolicy` 加在 `CompareOptions` 上（**新字段只能加在末尾**，见 §4 那条按位置聚合初始化的纪律） | TXT-015 |
| **BOM 策略表里 `Automatic` 必须排第一行，且与 `CompareOptions` 的初值相等** | `defaultBomPolicy()` 取的是「表里第一条已实现的」，而界面出厂选中项、设置键缺省值、`CompareOptions().bomPolicy` 是**三个独立来源**。表里换一次顺序，这三者就会彼此错开，而现象是「界面显示按编码自动判定、引擎其实在忽略」——两边都自洽，没有任何一条断言看得见。真正挡住它的是「拿表推出来的默认值 == 结构体初值」这条**跨来源等式**，它在本轮第一次跑就红了一次（当时 `Ignore` 排在首位） | TXT-015 |
| **`Automatic` 看「任一侧」，保存侧做不到时**静默**降级成保留** | `Automatic` 的判据是 `bomIsEncodingCritical(左) || bomIsEncodingCritical(右)`：只看一侧不够——一侧 UTF-16LE、另一侧 UTF-8 时，少的那个 BOM 让「这份文件按什么读」从确定变成猜测。保存侧反向的边界是：用户选「不写 BOM」而编码是 UTF-16（BOM 就是字节序）时，**降级成保留**并让状态栏说明，而不是弹一个「拒绝保存」——后者解决不了用户真正想要的事（「别让这个文件带 BOM」对 UTF-16 本来就不成立），只会把他卡在保存失败上 | TXT-015 |

## 5. 装配流程

```
main.cpp
  ├─ QApplication + 应用元信息（名称/版本/组织）
  ├─ 日志：级别解析（Log::levelFromName）→ 日志文件（AppDataLocation/lqcompare.log）
  ├─ 命令行解析（QCommandLineParser）
  ├─ MainWindow 构造
  │    ├─ registerCommands()   注册已实现的命令（含图标、说明、快捷键、ACTION-ID）
  │    ├─ buildRibbon()        RibbonLayout 按声明表建 10 页 / 45 组 / 169 个按钮
  │    ├─ setupDocks()         会话容器（中央）+ 输出面板（底部 dock）
  │    └─ setupStatusBar()     会话数 + 命令实现进度
  ├─ 命令注册表自检（CommandRegistry::validate()）→ 问题写日志
  ├─ MainWindow::setOptions()：设置仓库 + OptionsRuntime（主题 / 界面字体 / 内容字体 / 日志级别与目标）
  └─ show() + exec()
```

**⚠️ 这里曾经列着四条「启动时打印的表自检」，它们现在没有调用方了。** 2026-09-21 夜间的
一次 `main.cpp` 重写带走了它们，而文档没有跟着改，于是文档与行为悄悄分家了一段时间。
现状与决定如下（这条决定本身也是 §4.0 里那个待定事项的结论）：

| 自检 | 生产调用点 | 决定 |
| --- | --- | --- |
| `CommandRegistry::validate()` | `main.cpp` 里仍在调用 | 保留 |
| `validateSessionTypeTable()`（SESS-002）、`validateFilterLayerTable()`（FILT-005）、`validateAttributeConditionTable()`（FILT-003）、`validateNameFilterTables()`（FILT-002）、`validateContentFilterTables()`（FILT-004） | **没有**（`validateContentFilterTables()` 从落地起就没有） | **不把它们加回 `main.cpp`** |

不加回去的理由：这些表都是**编译期常量**，一张写坏的表只可能来自一次错误的改动，
而那次改动的产物在构建时就已经被单元测试逐条钉住了——每个套件都有一条拿**故意写坏的表**
跑同一个判定、要求它必须报出条数的用例（`Tests/FilterStack` G 组、`Tests/AttributeFilter` H 组、
`Tests/NameFilter` I 组、`Tests/ContentFilter` I 组、`Tests/FileOpsOptions` H 组）。
相比之下启动时打印一遍只是噪声：它能发现的，单测已经发现；单测发现不了的，它也发现不了。
四个函数与它们的用例都保留（它们**是**那道防线），只是不再冒充「启动时会跑」。

于是「`Tests/` 里有没有一条会红的用例」成为这类护栏的唯一判据——
这也是 §4 里「自检函数的数据来源必须是参数」那条决策存在的意义。

Ribbon 的 169 个按钮里，只有注册表里登记并带处理器的命令才是「已实现」；
其余点击后显示它对应的 ACTION-ID 与提示。状态栏常显「Commands N/M」，
避免「界面看着齐全、其实大部分没实现」的误判。

## 6. 后续结构演进

按 PRD 的功能域，服务层会需要以下模块（均在 `Services/` 下，各自一个 `.pri`）：

| 模块 | 承担的功能域 |
| --- | --- |
| `Session/` | 会话设置接口与会话类型注册表（**均已落地**，见 §3.7；注册表含 14 种内置类型的字段、ID 快照校验、按掩码的注册顺序优先查询、按分组枚举）、**设置项的声明模型与草稿**（**已落地**，见 §3.7；含逐控件的数据驱动往返用例）、设置作用域的三层覆盖链（SESS-007）、会话文件读写（SESS-008） |
| `Text/` | 行对齐算法（**已落地**：Myers / Patience，见 §3.3）、忽略规则（**已落地**：大小写折叠与两级空白，见 §3.3）、**行尾规则**（**已落地**：忽略行尾风格 + 独立的末尾换行开关，状态栏报 LF/CRLF/CR/混合，TXT-010）、相似行对齐与阈值（**已落地**：TXT-005，见 §3.3）、编码探测、字符级差异 |
| `Merge/` | 三方合并引擎与冲突判定 |
| `Folder/` | 目录扫描、快速测试、内容比对、状态判定、文件操作 |
| `Sync/` | 同步规则、预览生成、执行与校验 |
| `Filter/` | 掩码语法（**已落地**，含语法速查表与完整单元测试）、三层叠加与各层落点（FILT-005，**服务层已落地**，面板与设置页尚未接线）、属性条件（FILT-003，**服务层已落地**，扫描器尚未接线）、名称过滤（FILT-002，**服务层已落地**，界面尚未接线）、内容过滤的行过滤与关键字节（FILT-004，**服务层已落地**，比对引擎/状态栏/文件格式定义尚未接线）、预设 |
| `Format/` | 文件格式定义、语法、转换、关联覆盖 |
| `Report/` | 报表数据模型与 TXT/HTML/CSV/XML 渲染器 |
| `Patch/` | 补丁解析、生成、应用与回滚 |
| `Archive/` | 归档读取、写入与 Zip Slip 防护 |
| `Vcs/` | 版本控制后端抽象与 Git 实现 |
| `Platform/` | 系统图标（**已落地**）、Shell 集成（**已落地**，注册表计划与安装/卸载/校验在内存存储上可测，Windows 侧薄层尚未编译过）——文件系统抽象与回收站实际落在 `Files/`，见 §3.4 |
| `Script/` | 脚本解析与执行引擎 |

新增模块时同步更新本文件的目录清单。
