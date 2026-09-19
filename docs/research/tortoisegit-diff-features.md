# TortoiseGit 比对（Diff）功能测绘报告

> 竞品测绘对象：**TortoiseGit 2.18 / 2.19**、**TortoiseGitMerge 2.18**、**TortoiseGitBlame**、**TortoiseGitIDiff**、**TortoiseGitUDiff**
> 测绘目的：为 **LqCompare**（Qt 5.15.2 / C++17 文件与文件夹比对工具）拆分可独立验收的开发任务，并指导 **LqRibbon** 的 `RibbonPage / RibbonGroup / Button` 组织方式。
> 编制日期：2026-09-20

---

## 0. 文档约定

### 0.1 证据等级标记

每个功能点后面的「来源」列使用以下标记，便于区分「照抄实现」与「需要设计判断」：

| 标记 | 含义 |
|---|---|
| `官方文档` | 来自 tortoisegit.org/docs 官方手册的明确描述 |
| `源码资源` | 来自 TortoiseGit 源码里的 `.rc` / `Ribbon.xml` / `resource.h` 资源定义（最可靠） |
| `源码逻辑` | 来自 TortoiseGit 源码的 `.cpp` 业务逻辑 |
| `推断` | 文档与源码均未直接覆盖，由界面截图 / 行业惯例 / 代码结构推断，**需二次确认** |

### 0.2 三个程序的分工（必读）

TortoiseGit 的「比对」能力不是一个程序，而是 **四个可执行文件** 分工：

| 程序 | 角色 | 是否 Ribbon |
|---|---|---|
| `TortoiseGitProc.exe` | TortoiseGit 客户端主程序，承载 Log / Commit / Revision Graph / Repo-Browser / Merge 等**所有对话框** | **否**，经典菜单+工具栏 Win32 对话框 |
| `TortoiseGitMerge.exe` | 文本比对/合并编辑器（1/2/3 栏），即 TortoiseMerge 的 Git 分支 | **是**（原生 Windows Ribbon），可关闭降级为经典工具栏 |
| `TortoiseGitBlame.exe` | 逐行追溯查看器 | **否**，MFC 经典菜单+工具栏 |
| `TortoiseGitIDiff.exe` | 图像比对查看器 | **否**，经典菜单+工具栏 |
| `TortoiseGitUDiff.exe` | 统一差异（Unified Diff）查看器 | **否**，经典菜单+工具栏 |

### 0.3 最关键的三个结论（直接影响 LqCompare 架构）

1. **TortoiseGitMerge 确实已经是 Ribbon 界面**，但**只有一个 Tab**（"Edit"），6 个 Group（Edit / Navigate / Blocks / Whitespaces / Diff / View）。这是「把一个传统单窗口工具硬塞进 Ribbon」的**最简形态**——它把 File 菜单整体挪进了 Application Menu（左上角 Office 按钮），把 View 的开关项挪进了 Group 里的 ToggleButton。**这个组织方式对 LqCompare 参考价值有限，反而是反例**：功能多页面时这种单 Tab 会造成按钮拥挤。LqRibbon 应当用「多页面 + 每页 4~6 组」的 Office 标准形态。
2. **TortoiseGit 客户端的所有对话框都不是 Ribbon**。它的「文件夹比对 / 工作副本状态比对」能力真实存在（Log 对话框、Check for Modifications、Commit、Revision Graph、Blame），但分散在经典对话框里。LqCompare 把这些能力 Ribbon 化，本身就是差异化优势。
3. **TortoiseGit 内置工具不支持文件夹（目录层次）比对** —— 官方文档原文：*"The built-in tools supplied with TortoiseGit do not support viewing differences between directory hierarchies."* 这是 LqCompare 的主战场，**绝不能照搬 TortoiseGit 的信息架构**，必须是「文件夹比对为主视图 + 文件比对为子视图」。

### 0.4 测绘规模总览

| 功能域 | 内容 | 功能点 ID 范围 | 数量 |
|---|---|---|---|
| **A** | TortoiseGitMerge 比对/合并窗口（21 个小节） | A01–A385 | **385** |
| **B** | TortoiseGit 客户端其他比对入口（16 个小节） | B01–B505 | **505** |
| **C** | Ribbon 布局分析与 LqRibbon 落地建议 | C001–C204 | **204** |
| **D** | 与 Beyond Compare 的差异点 | D01–D60 | **60** |
| **F** | 未覆盖/待确认项（补测清单） | F01–F12 | **12** |
| | **合计** | | **1166** |

其中域 C 拆分为：**195 条 Ribbon 按钮定义**（C001–C195，10 个页面 / 44 个组）+ **9 条 LqRibbon 引擎实现要求**（C196–C204）。

按「可独立实现并可单独验收」的口径统计，**需开发/验收的功能点总数为 1166 条**（含 12 条待补测确认项，不含待确认项则为 1154 条）。

---

# 第一部分：功能域 → 功能点清单

---

## A. TortoiseGitMerge 比对/合并窗口

### A.1 窗口布局与框架

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A01 | 主窗口 | Main Window | 单文档（SDI）窗口，承载全部视图与控制条，支持最大化/最小化/还原 | `源码逻辑` |
| A02 | 窗口标题栏 | Title Bar | 显示当前比对上下文（文件名 / 视图标题），标题文本可由命令行 `/basename` `/minename` `/theirsname` 覆盖 | `官方文档` |
| A03 | 视图标题 | View Title | 每个窗格顶部的标题条，默认显示 `Theirs` / `Mine` / `Merged`；鼠标悬停弹出 tooltip 显示该窗格实际的完整文件路径 | `官方文档` |
| A04 | 菜单栏 | Menu Bar | 经典模式下显示 `File / Edit / Navigate / View / Help` 五级菜单；Ribbon 模式下由 Application Menu + Tab 取代 | `源码资源` |
| A05 | 工具栏 | Tool Bar | 经典模式下的 16×16 图标注解工具条，由 `IDR_MAINFRAME TOOLBAR` 定义，共 33 个 Button + 10 个 Separator | `源码资源` |
| A06 | Ribbon 界面 | Ribbon UI | 启用后使用 Office 2007 风格 Ribbon；设置项 `Use ribbons`，默认**开启** | `官方文档` |
| A07 | Ribbon 降落降级 | Ribbon Fallback | 可通过 `HKEY_CURRENT_USER\Software\TortoiseGitMerge\UseRibbons = 0` 强制作废 Ribbon 回到经典工具栏（用于 RDP / 旧系统） | `源码逻辑` |
| A08 | Ribbon 应用程序菜单 | Application Menu | Ribbon 左上角圆形按钮，含 Open / Save / Save as / Create patch file / Hide-Show patch file list / Settings / About / Exit | `源码资源` |
| A09 | 快速访问工具栏 | Quick Access Toolbar | Ribbon 左上角 QAT，默认可勾选加入 Save / Undo / Redo | `源码资源` |
| A10 | Ribbon 收缩策略 | Scaling Policy | 窗口变窄时按预设顺序收缩分组：Navigate→Medium、View→Small、Diff→Small、Edit→Medium、Whitespaces→Small | `源码资源` |
| A11 | 定位器栏 | Locator Bar | 窗口**左侧**的窄条，三列分别对应左窗格/右窗格/底部窗格，可视化标出差异位置，同时可当滚动条拖动同步滚动所有窗格 | `官方文档` |
| A12 | 行差异栏 | Line Diff Bar | 视图内的窄条，逐行标出该行状态（未变/新增/删除/冲突） | `源码资源` |
| A13 | 状态栏 | Status Bar | 底部状态栏，含统计信息 + 三个组合框（编码/行尾/Tab） | `官方文档` |
| A14 | 分割条 | Splitter | 窗格之间可拖动调整比例的水平/垂直分割条 | `官方文档` |
| A15 | 补丁文件列表面板 | Patch File List Pane | 仅补丁模式下出现的小窗口，列出补丁涉及的文件，可用 `Hide/Show the patch file list` 开关 | `官方文档` |
| A16 | 视图缩放/DPI 适配 | DPICHANGED Handling | 响应 `WM_DPICHANGED`，跨显示器移动时重新布局 | `源码逻辑` |
| A17 | 系统颜色变更响应 | SysColorChange / SettingChange | 响应系统主题/颜色变化，重绘界面 | `源码逻辑` |
| A18 | 任务栏进度/缩略图 | Taskbar Button Created | 响应 `TaskBarButtonCreated`，支持任务栏集成 | `源码逻辑` |
| A19 | 布局持久化 | Layout Persistence | 窗口位置、大小、分割条位置、各栏显隐持久化到注册表 `Software\TortoiseGitMerge\*` | `源码逻辑` |

### A.2 视图模式（Viewing Modes）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A20 | 单栏视图 | One Pane View | 只显示一个窗格，已内联标记增删行；用于只看差异不编辑 | `官方文档` |
| A21 | 双栏视图 | Two Pane View | 左右两栏并排显示两个文件，支持行内差异着色、手改标记、块操作 | `官方文档` |
| A22 | 三栏视图 | Three Pane View | 左=Theirs 对 Base 的差异，右=Mine 对 Base 的差异，下=Merged 合并结果；用于解决冲突 | `官方文档` |
| A23 | 合并结果栏 | Merged / Bottom Pane | 三栏视图的底部窗格，是唯一可编辑的窗格，保存的就是它 | `官方文档` |
| A24 | 单/双栏切换 | Switch between single and double pane view | 一键在单栏与双栏之间切换（`Ctrl-D` / `ID_VIEW_ONEWAYDIFF`） | `官方文档` |
| A25 | 默认单栏 | Use one-pane view as default for 2-file diff | 设置项：2 文件比对时默认使用单栏视图 | `官方文档` |
| A26 | 启动强制单栏 | /oneway | 命令行开关，忽略设置强制以单栏视图启动 | `官方文档` |
| A27 | 左右视图交换 | Switch left and right view | 交换左右窗格内容（`ID_VIEW_SWITCHLEFT`） | `源码资源` |
| A28 | 三栏窗格切换 | Switch between left/right/bottom view | `Ctrl-Tab` 在三个窗格间循环切换焦点 | `官方文档` |
| A29 | 补丁模式视图 | Patch View | 加载补丁文件后进入的专门模式，显示「补丁文件列表 + 单文件差异预览」 | `官方文档` |

### A.3 经典工具栏按钮清单（逐条，来源：`TortoiseMergeENG.rc` → `IDR_MAINFRAME TOOLBAR`）

> 顺序即资源文件中的物理顺序，`—` 表示分隔符。

| ID | 中文名 | 英文原名 | 命令 ID | 来源 |
|---|---|---|---|---|
| A30 | 工具栏-打开 | Open files | `ID_FILE_OPEN` | `源码资源` |
| A31 | 工具栏-保存 | Save file | `ID_FILE_SAVE` | `源码资源` |
| A32 | （分隔符 1） | Separator | — | `源码资源` |
| A33 | 工具栏-重新加载 | Reload | `ID_FILE_RELOAD` | `源码资源` |
| A34 | 工具栏-撤销 | Undo | `ID_EDIT_UNDO` | `源码资源` |
| A35 | （分隔符 2） | Separator | — | `源码资源` |
| A36 | 工具栏-上一差异 | Previous difference | `ID_NAVIGATE_PREVIOUSDIFFERENCE` | `源码资源` |
| A37 | 工具栏-下一差异 | Next difference | `ID_NAVIGATE_NEXTDIFFERENCE` | `源码资源` |
| A38 | 工具栏-上一冲突 | Previous conflict | `ID_NAVIGATE_PREVIOUSCONFLICT` | `源码资源` |
| A39 | 工具栏-下一冲突 | Next conflict | `ID_NAVIGATE_NEXTCONFLICT` | `源码资源` |
| A40 | （分隔符 3） | Separator | — | `源码资源` |
| A41 | 工具栏-使用左块 | Use left block | `ID_EDIT_USELEFTBLOCK` | `源码资源` |
| A42 | （分隔符 4） | Separator | — | `源码资源` |
| A43 | 工具栏-使用左文本块 | Use left file text block | `ID_EDIT_USETHEIRBLOCK` | `源码资源` |
| A44 | 工具栏-使用右文本块 | Use right file text block | `ID_EDIT_USEMYBLOCK` | `源码资源` |
| A45 | 工具栏-先左后右 | Use left file text block then right | `ID_EDIT_USETHEIRTHENMYBLOCK` | `源码资源` |
| A46 | 工具栏-先右后左 | Use right file text block then left | `ID_EDIT_USEMINETHENTHEIRBLOCK` | `源码资源` |
| A47 | （分隔符 5） | Separator | — | `源码资源` |
| A48 | 工具栏-标记已解决 | Mark as resolved | `ID_EDIT_MARKASRESOLVED` | `源码资源` |
| A49 | （分隔符 6） | Separator | — | `源码资源` |
| A50 | 工具栏-显示空白符 | Show Whitespaces | `ID_VIEW_WHITESPACES` | `源码资源` |
| A51 | 工具栏-长行折行 | Wrap long lines | `ID_VIEW_WRAPLONGLINES` | `源码资源` |
| A52 | （分隔符 7） | Separator | — | `源码资源` |
| A53 | 工具栏-行内差异 | Inline diff | `ID_VIEW_INLINEDIFF` | `源码资源` |
| A54 | 工具栏-按词行内差异 | Inline diff word-wise | `ID_VIEW_INLINEDIFFWORD` | `源码资源` |
| A55 | 工具栏-比较空白 | Compare whitespaces | `ID_VIEW_COMPAREWHITESPACES` | `源码资源` |
| A56 | 工具栏-忽略空白变更 | Ignore whitespace changes | `ID_VIEW_IGNOREWHITESPACECHANGES` | `源码资源` |
| A57 | 工具栏-忽略全部空白变更 | Ignore all whitespace changes | `ID_VIEW_IGNOREALLWHITESPACECHANGES` | `源码资源` |
| A58 | （分隔符 8） | Separator | — | `源码资源` |
| A59 | 工具栏-单/双栏切换 | Switch between single and double pane view | `ID_VIEW_ONEWAYDIFF` | `源码资源` |
| A60 | 工具栏-左右交换 | Switch left and right view | `ID_VIEW_SWITCHLEFT` | `源码资源` |
| A61 | 工具栏-折叠 | Collapse | `ID_VIEW_COLLAPSED` | `源码资源` |
| A62 | 工具栏-显示补丁文件列表 | Hide/Show the patch file list | `ID_VIEW_SHOWFILELIST` | `源码资源` |
| A63 | （分隔符 9） | Separator | — | `源码资源` |
| A64 | 工具栏-设置 | Settings | `ID_VIEW_OPTIONS` | `源码资源` |
| A65 | （分隔符 10） | Separator | — | `源码资源` |
| A66 | 工具栏-帮助 | Help | `ID_HELP` | `源码资源` |

### A.4 原生 Ribbon 实际结构（来源：`src/Resources/TortoiseGitMergeRibbon.xml`）

**1 个 Tab，6 个 Group。**

| ID | 中文名 | 英文原名 | Ribbon 元素类型 | CommandName (Id) | 来源 |
|---|---|---|---|---|---|
| A67 | Ribbon 页-Edit | Tab "Edit" | Tab（Keytip `E`，Id 10000） | `cmdTabEdit` | `源码资源` |
| A68 | Ribbon 组-Edit | Group "Edit" | Group（IdealSize = Large） | `cmdGroupEdit` | `源码资源` |
| A69 | Ribbon 组-Navigate | Group "Navigate" | Group（IdealSize = Large，SizeDefinition = SixButtons-TwoColumns） | `cmdGroupNavigate` | `源码资源` |
| A70 | Ribbon 组-Blocks | Group "Blocks" | Group（SizeDefinition = TwoButtons） | `cmdGroupBlocks` | `源码资源` |
| A71 | Ribbon 组-Whitespaces | Group "Whitespaces" | Group（IdealSize = Medium，SizeDefinition = FourButtons） | `cmdGroupWhitespaces` | `源码资源` |
| A72 | Ribbon 组-Diff | Group "Diff" | Group（IdealSize = Large，SizeDefinition = FiveButtons） | `cmdGroupDiff` | `源码资源` |
| A73 | Ribbon 组-View | Group "View" | Group（IdealSize = Large，SizeDefinition = FiveButtons） | `cmdGroupView` | `源码资源` |
| A74 | 组内-保存 | Save | Button | `cmdSave` (0xE103) | `源码资源` |
| A75 | 组内-重新加载 | Reload | Button | `cmdReload` (32794) | `源码资源` |
| A76 | 组内-撤销 | Undo | Button | `cmdUndo` (0xE12B) | `源码资源` |
| A77 | 组内-重做 | Redo | Button | `cmdRedo` (0xE12C) | `源码资源` |
| A78 | 组内-允许编辑 | Enable Edit | **ToggleButton** | `cmdEditEnabled` (32976) | `源码资源` |
| A79 | 组内-复制 | Copy | Button | `cmdCopy` (0xE122) | `源码资源` |
| A80 | 组内-粘贴 | Paste | Button | `cmdPaste` (0xE125) | `源码资源` |
| A81 | 组内-查找 | Find | Button | `cmdFind` (0xE124) | `源码资源` |
| A82 | 组内-查找上一个 | Find Previous | Button | `cmdFindPrev` (32892) | `源码资源` |
| A83 | 组内-查找下一个 | Find Next | Button | `cmdFindNext` (32891) | `源码资源` |
| A84 | 组内-跳转行 | Goto Line | Button | `cmdGotoLine` (32893) | `源码资源` |
| A85 | 组内-标记已解决 | Mark as resolved | Button | `cmdMarkResolved` (32808) | `源码资源` |
| A86 | 导航-上一差异 | Previous difference | Button（Keytip `PD`） | `cmdNavigatePreviousDifference` (32780) | `源码资源` |
| A87 | 导航-下一差异 | Next difference | Button（Keytip `ND`） | `cmdNavigateNextDifference` (32779) | `源码资源` |
| A88 | 导航-上一冲突 | Previous conflict | Button（Keytip `PC`） | `cmdNavigatePreviousConflict` (32802) | `源码资源` |
| A89 | 导航-下一冲突 | Next conflict | Button（Keytip `NC`） | `cmdNavigateNextConflict` (32804) | `源码资源` |
| A90 | 导航-上一行内差异 | Previous inline difference | Button（Keytip `PI`） | `cmdNavigatePreviousInlineDiff` (32876) | `源码资源` |
| A91 | 导航-下一行内差异 | Next inline difference | Button（Keytip `NI`） | `cmdNavigateNextInlineDiff` (32875) | `源码资源` |
| A92 | 块-使用块 | Use text blocks | **SplitButton**（主按钮 + 下拉 3 项） | `cmdUseBlocks` (32914) | `源码资源` |
| A93 | 块-下拉-使用左块 | Use left block | SplitButton 主按钮 | `cmdUseLeftBlock` (32855) | `源码资源` |
| A94 | 块-下拉-使用左文件 | Use left file | SplitButton 菜单项 | `cmdUseLeftFile` (32856) | `源码资源` |
| A95 | 块-下拉-左块先于右块 | Use block from left before right | SplitButton 菜单项 | `cmdUseBlockFromLeftBeforeRight` (32857) | `源码资源` |
| A96 | 块-下拉-右块先于左块 | Use block from right before left | SplitButton 菜单项 | `cmdUseBlockFromRightBeforeLeft` (32859) | `源码资源` |
| A97 | 三方块操作 | Three-way actions | **SplitButton**（主按钮 = 使用左文本块） | `cmdThreeWayActions` (33001) | `源码资源` |
| A98 | 三方-菜单项-使用右文本块 | Use right text block | SplitButton 菜单项 | `cmdUseMineBlock` (32820) | `源码资源` |
| A99 | 三方-菜单项-先左后右 | Use left text block then right | SplitButton 菜单项 | `cmdUseTheirsThenMineBlock` (32821) | `源码资源` |
| A100 | 三方-菜单项-先右后左 | Use right text block then left | SplitButton 菜单项 | `cmdUseMineThenTheirsBlock` (32822) | `源码资源` |
| A101 | 空白-显示空白符 | Show Whitespaces | ToggleButton（Keytip `W`） | `cmdShowWhitespaces` (32774) | `源码资源` |
| A102 | 空白-比较空白 | Compare whitespaces | ToggleButton | `cmdCompareWhitespaces` (32871) | `源码资源` |
| A103 | 空白-忽略空白变更 | Ignore whitespace changes | ToggleButton | `cmdIgnoreWhitespaceChanges` (32872) | `源码资源` |
| A104 | 空白-忽略全部空白变更 | Ignore all whitespace changes | ToggleButton | `cmdIgnoreAllWhitespaceChanges` (32873) | `源码资源` |
| A105 | Diff-行内差异 | Inline diff | ToggleButton | `cmdViewInlineDiff` (32889) | `源码资源` |
| A106 | Diff-按词行内差异 | Inline diff word-wise | ToggleButton | `cmdViewInlineDiffWord` (32825) | `源码资源` |
| A107 | Diff-正则过滤器 | Regex Filter | **SplitButtonGallery**（VerticalMenuLayout, Gripper=None） | `cmdRegexFilterConfig1` (500) | `源码资源` |
| A108 | Diff-配置过滤器正则 | Configure Filter Regex | Gallery 项 | `cmdRegexFilterConfig2` (501) | `源码资源` |
| A109 | Diff-忽略注释 | Ignore Comments | ToggleButton | `cmdViewIgnoreComments` (32896) | `源码资源` |
| A110 | Diff-忽略行尾 | Ignore line endings | ToggleButton | `cmdViewIgnoreEOL` (32899) | `源码资源` |
| A111 | View-视图栏 | View Bars | **DropDownButton**（含 3 个 CheckBox） | `cmdViewBars` (32898) | `源码资源` |
| A112 | View-下拉项-行差异栏 | Line diff bar | CheckBox | `cmdViewLineDiffBar` (32853) | `源码资源` |
| A113 | View-下拉项-定位器栏 | Locator bar | CheckBox | `cmdViewLocatorBar` (32854) | `源码资源` |
| A114 | View-下拉项-状态栏 | Status Bar | CheckBox | `cmdViewStatusBar` (0xE801) | `源码资源` |
| A115 | View-长行折行 | Wrap Lines | ToggleButton（Keytip `L`） | `cmdViewWrapLines` (32881) | `源码资源` |
| A116 | View-单/双栏切换 | Switch between single and double pane view | ToggleButton | `cmdViewOneWayDiff` (32775) | `源码资源` |
| A117 | View-左右交换 | Switch left and right view | Button | `cmdViewSwitchLeft` (32811) | `源码资源` |
| A118 | View-折叠 | Collapse | ToggleButton | `cmdViewCollapsed` (32870) | `源码资源` |
| A119 | 应用菜单-打开 | Open | Button（Keytip `o1`，MajorItems） | `cmdOpen` (0xE101) | `源码资源` |
| A120 | 应用菜单-保存 | Save | Button（Keytip `S`） | `cmdSave` (0xE103) | `源码资源` |
| A121 | 应用菜单-另存为 | Save as | Button | `cmdSaveAs` (0xE104) | `源码资源` |
| A122 | 应用菜单-创建补丁文件 | Create patch file | Button | `cmdCreateUnifiedDiff` (32828) | `源码资源` |
| A123 | 应用菜单-显示补丁文件列表 | Hide/Show the patch file list | Button | `cmdShowFileList` (32817) | `源码资源` |
| A124 | 应用菜单-设置 | Settings | Button | `cmdSettings` (32782) | `源码资源` |
| A125 | 应用菜单-关于 | About TortoiseGitMerge | Button | `cmdAbout` (0xE140) | `源码资源` |
| A126 | 应用菜单-退出 | Exit | Button（StandardItems） | `cmdExit` (0xE141) | `源码资源` |
| A127 | 帮助按钮 | Help Button | Ribbon 右上角 `?` | `cmdHelp` (0xE146) | `源码资源` |
| A128 | 缺失 Ribbon 项-上一行内差异的键盘可达性 | — | Ribbon 未暴露 `cmdViewMovedBlocks`（移动块），仅菜单/快捷键可达 | `源码资源` |
| A129 | Ribbon 未定义 Tooltip | Missing Tooltips | `TortoiseGitMergeRibbon.xml` 中**完全没有** `Command.TooltipTitle/Description` 定义 —— LqCompare 应补齐 | `源码资源` |

### A.5 经典菜单栏结构（来源：`IDR_MAINFRAME MENU`）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A130 | 文件菜单 | File Menu | Open / Save / Save as… / Mark as resolved / Create patch file / Reload / Enable edit / Exit | `源码资源` |
| A131 | 编辑菜单 | Edit Menu | Undo / Copy / Paste / Use left block / Use left file / Use block from left before right / Use block from right before left / Use left text block / Use right text block / Use left text block then right / Use right text block then left / Find / Goto Line / Regex Filter 子菜单 / Ignore Comments | `源码资源` |
| A132 | 导航菜单 | Navigate Menu | Next difference / Previous difference / Next conflict / Previous conflict / Next inline difference / Previous inline difference | `源码资源` |
| A133 | 视图菜单 | View Menu | Toolbar / Status Bar / Line diff bar / Locator Bar / Wrap long lines / Moved blocks / Inline diff / Inline diff word-wise / Compare whitespaces / Ignore whitespace changes / Ignore all whitespace changes / Ignore Comments / Show Whitespaces / 单双栏 / 左右交换 / Collapse / Settings / Hide-Show patch file list | `源码资源` |
| A134 | 帮助菜单 | Help Menu | Help Topics / About TortoiseGitMerge | `源码资源` |
| A135 | Regex Filter 子菜单 | Regex Filter Submenu | 编辑菜单下的二级子菜单，只有 `Configure Filter Regexes` 一项 | `源码资源` |

### A.6 查找 / 替换 / 跳转

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A136 | 查找对话框 | Find Dialog | `Ctrl-F` 打开，支持文本搜索与替换 | `官方文档` |
| A137 | 查找下一个 | Find Next | 向下查找下一个匹配 | `源码资源` |
| A138 | 查找上一个 | Find Previous | 向上查找上一个匹配 | `源码资源` |
| A139 | 匹配计数 | Match Count | 查找对话框显示 `Count: %u matches.` | `源码资源` |
| A140 | 未找到提示 | Can't find text | 弹窗 `Find: Can't find the text "%s"` | `源码资源` |
| A141 | 到达文件尾提示 | End of document reached | `Find: First occurrence from the top found. End of document reached.` | `源码资源` |
| A142 | 到达文件首提示 | Beginning of document reached | `Find: First occurrence from the bottom found. Beginning of document reached.` | `源码资源` |
| A143 | 替换全部计数 | Replaced N matches | 替换完成提示 `Replaced %d matches` | `源码资源` |
| A144 | 跳转到行 | Goto Line | `Ctrl-G`；对话框提示 `&Line number (%1!d! - %2!d!)` | `源码资源` |
| A145 | 跳转越界校验 | Line out of range | `The line number must be in between %1!d! and %2!d!` | `源码资源` |
| A146 | 选中即查下一处 | Search Next | `ID_EDIT_FINDNEXTSTART`：查找当前选中文本的下一次出现 | `源码资源` |
| A147 | 选中即查上一处 | Search Previous | `ID_EDIT_FINDPREVSTART`：查找当前选中文本的上一次出现 | `源码资源` |

### A.7 差异导航

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A148 | 下一差异 | Next difference | 跳到下一个有差异的块并在全部窗格同步定位 | `官方文档` |
| A149 | 上一差异 | Previous difference | 跳到上一个有差异的块 | `官方文档` |
| A150 | 下一冲突 | Next conflict | 跳到下一个未解决冲突块 | `官方文档` |
| A151 | 上一冲突 | Previous conflict | 跳到上一个未解决冲突块 | `官方文档` |
| A152 | 下一行内差异 | Next inline difference | 在同一行内跳到下一处行内差异片段 | `源码资源` |
| A153 | 上一行内差异 | Previous inline difference | 在同一行内跳到上一处行内差异片段 | `源码资源` |
| A154 | 加载后跳首个差异 | Jump to first difference when loading | 设置项：文件加载完成后自动跳到第一处差异 | `官方文档` |
| A155 | 加载后跳首个冲突 | Jump to first conflict when loading | 设置项：文件加载完成后自动跳到第一处冲突 | `官方文档` |
| A156 | 启动跳转到指定行 | /line | 命令行 `/line:NN` 指定加载后跳转的行号 | `官方文档` |
| A157 | 导航按钮可用性 | Navigation UI Update | 无差异/无冲突时对应按钮置灰（`OnUpdate MergeNextconflict` 等） | `源码逻辑` |
| A158 | 定位器栏拖拽跳转 | Locator Bar Drag | 拖动定位器栏的高亮框可同步滚动全部窗格到指定位置 | `官方文档` |
| A159 | 定位器栏点击跳转 | Locator Bar Click | 点击定位器栏直接跳到对应位置 | `推断` |

### A.8 行状态与视觉标记

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A160 | 行状态-新增 | Line Added | 该行是新增的（`lineadded.ico`） | `官方文档` |
| A161 | 行状态-删除 | Line Removed | 该行已被删除（`lineremoved.ico`） | `官方文档` |
| A162 | 行状态-相等 | Line Equal | 该行无变化（`lineequal.ico`） | `源码资源` |
| A163 | 行状态-已还原 | Reverted Line | 该行的改动已被还原为原始内容 | `官方文档` |
| A164 | 行状态-仅空白变更 | Whitespace-only Change | 该行只有空白差异；连续多行出现时提示段落被重排换行（`linewhitespace.ico`） | `官方文档` |
| A165 | 行状态-手工编辑 | Hand-edited Line | 该行被用户手工编辑过（铅笔图标，`lineedited.ico`） | `官方文档` |
| A166 | 行状态-冲突 | Conflicted Line | 该行处于冲突状态（`lineconflicted.ico`） | `官方文档` |
| A167 | 行状态-冲突被忽略 | Conflict-Ignored | 该行冲突但由于空白/行尾被忽略设置而**未显示**为冲突（`lineconflictedignored.ico`） | `官方文档` |
| A168 | 行状态-已标记 | Marked Line | 该行/块被标记（用于"只提交部分改动"场景，`linemarked.ico`） | `源码资源` |
| A169 | 行状态-移动 | Moved Line | 该行被检测为从别处移动而来/移动到别处（`moved.ico`） | `官方文档` |
| A170 | 移动来源提示 | Line moved from line N | 悬停移动行显示 `Line moved from line %ld` | `源码资源` |
| A171 | 移动目标提示 | Line moved to line N | 悬停移动行显示 `Line moved to line %ld` | `源码资源` |
| A172 | 内联新增着色 | Inline added text | 行内差异中的新增片段使用单独颜色 | `官方文档` |
| A173 | 内联删除着色 | Inline removed text | 行内差异中的删除片段使用单独颜色；在字符串中以深棕色竖线标示 | `官方文档` |
| A174 | 仅空白变更的白色圆圈 | Whitespace-only marker | 视图左侧的白色圆圈，提示该块无实质代码变化（双栏视图特有） | `官方文档` |
| A175 | 双击选中词高亮 | Double-click word highlight | 双击某词后，全文所有该词在主窗格与定位器栏中同时高亮；再次双击取消 | `官方文档` |
| A176 | 左边距/三击选中整行 | Margin click / Triple click | 在左边距点击或行内三击可选中整行 | `官方文档` |
| A177 | 文本相同但文件不同提示 | Text identical but files differ | 汇总提示 `The text is identical, but the files do not match!` + 差异原因清单（Whitespace changes / Encoding / Newlines） | `源码资源` |
| A178 | 光标位置状态条 | `IDS_VIEWSCROLLTIPTEXT` | 状态区显示 `Line: %*ld` | `源码资源` |
| A179 | 标记词计数 | Marked words | Ribbon 状态栏显示 `Marked words: l: XXXX | r: XXXX | b: XXXX` | `源码逻辑` |
| A180 | 行/列指示器 | Column Indicator | Ribbon 状态栏 `ID_INDICATOR_COLUMN` 显示当前行列位置 | `源码逻辑` |

### A.9 空白与内容忽略策略

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A181 | 比较空白 | Compare whitespaces | 把缩进与行内空白的差异**全部**当作增删行显示 | `官方文档` |
| A182 | 忽略空白变更 | Ignore whitespace changes | 忽略空白**数量/类型**差异（如缩进变化、Tab 转空格）；从无到有或从有到无的空白仍算变更 | `官方文档` |
| A183 | 忽略全部空白变更 | Ignore all whitespace changes | 排除所有仅涉及空白的变更 | `官方文档` |
| A184 | 忽略行尾 | Ignore line endings | 隐藏仅因 CRLF/LF 风格不同而产生的差异（强烈推荐） | `官方文档` |
| A185 | 忽略大小写变更 | Ignore case changes | 隐藏仅因大小写导致的差异（VB 等语言有用） | `官方文档` |
| A186 | 忽略注释 | Ignore Comments | 比对前先移除注释，使注释内的改动不算差异 | `源码资源` |
| A187 | 忽略注释配置文件 | ignorecomments.txt | 忽略注释所依据的注释语法规则文件（安装目录 `bin`） | `源码资源` |
| A188 | 四种空白策略互斥组 | Whitespace Mode Group | Compare / Ignore changes / Ignore all 三种状态互斥（ToggleButton 组） | `源码资源` |
| A189 | 差异含义始终保留 | Content change always included | 官方明确：任何内容真正变化的行，永远会被计入差异，不受空白设置影响 | `官方文档` |

### A.10 正则过滤器（Regex Filter）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A190 | 正则过滤器下拉 | Regex Filter Gallery | Ribbon 中的 SplitButtonGallery，下拉列出全部已配置的正则过滤器 | `源码资源` |
| A191 | 应用某个过滤器 | Apply Regex Filter | 选中一个过滤器后，按该正则对两个文件预处理后再比对 | `源码资源` |
| A192 | 无过滤器 | No filter | `ID_REGEX_NO_FILTER`：清除当前过滤器 | `源码资源` |
| A193 | 配置过滤器正则 | Configure Filter Regexes | 打开配置对话框管理正则过滤器集合（`IDD_REGEXFILTERS`） | `源码资源` |
| A194 | 过滤器配置持久化 | RegexFilters.ini | 配置存放于 `Resources/RegexFilter.ini` 与用户配置 | `源码资源` |
| A195 | 过滤器命令范围 | ID_REGEXFILTER+400 | 支持最多 400 个过滤器的动态命令 ID 范围 | `源码逻辑` |

### A.11 合并与冲突解决操作

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A196 | 使用左块 | Use left block | 用左视图的文本块替换当前位置 | `官方文档` |
| A197 | 使用左文件 | Use left file | 用整个左文件内容替换结果 | `官方文档` |
| A198 | 左块先于右块 | Use block from left before right | 两个块都要，左块排在前面 | `官方文档` |
| A199 | 右块先于左块 | Use block from right before left | 两个块都要，右块排在前面 | `官方文档` |
| A200 | 使用左文本块 | Use text block from 'theirs' / left | 双栏视图下把左文件（Theirs）的改动应用到右文件（Mine） | `官方文档` |
| A201 | 使用右文本块 | Use text block from 'mine' / right | 三方视图下采用右文件（Mine）的块 | `官方文档` |
| A202 | 先左后右 | Use left text block then right | 三方视图：`mine` 在 `theirs` 之前 | `官方文档` |
| A203 | 先右后左 | Use right text block then left | 三方视图：`theirs` 在 `mine` 之前 | `官方文档` |
| A204 | 使用两个文本块（此块优先） | Use both text blocks (this one first) | 右键菜单项：两块都保留，当前块在前 | `官方文档` |
| A205 | 使用两个文本块（此块最后） | Use both text blocks (this one last) | 右键菜单项：两块都保留，当前块在后 | `官方文档` |
| A206 | 标记冲突已解决 | Mark as resolved | 在 Git 中把文件标记为已解决（`ID_EDIT_MARKASRESOLVED`） | `官方文档` |
| A207 | 允许编辑 | Enable Edit | 允许在当前窗格手工编辑；双栏视图默认只允许编辑右栏，按此按钮后才允许删左栏 | `官方文档` |
| A208 | 可编辑性只读锁定 | /readonly | 命令行开关，禁用编辑能力（只读比对） | `官方文档` |
| A209 | 撤销 | Undo | `Ctrl-Z` / `Alt-Backspace` 撤销上一次编辑 | `官方文档` |
| A210 | 重做 | Redo | 重做被撤销的编辑 | `源码资源` |
| A211 | 重新加载 | Reload | `Ctrl-R` 重新加载文件并**丢弃全部改动** | `官方文档` |
| A212 | 保存 | Save | `Ctrl-S` 保存合并/编辑结果 | `官方文档` |
| A213 | 另存为 | Save as | `Ctrl-Shift-S`；三栏合并且未指定输出文件时必须走此路径 | `官方文档` |
| A214 | 自动备份原文件 | Backup original file | 保存前把工作副本中的原文件改名为 `filename.bak` | `官方文档` |
| A215 | 冲突解决提示 | Conflict resolution hint | 三方视图下右栏（Mine）与左栏（Theirs）分别代表"我方"与"对方" | `官方文档` |
| A216 | 冲突被空白掩盖的提醒 | Hidden conflict warning | 文件在 Git 中标记为冲突但视图无冲突时，提示是空白/行尾设置所致，仍需选择版本 | `官方文档` |
| A217 | 编辑后无法追踪的告警 | Editing breaks block tracking | 官方明确：一旦开始手工编辑，就无法再追踪与原文件的行块关系，建议先做完块操作再手改 | `官方文档` |
| A218 | 自动合并结果 | Merged auto result | 三方合并时底部 Merged 窗格自动生成初步结果，用户在其上调整 | `官方文档` |
| A219 | 退出前强制确认保存 | /saverequired | 命令行开关：即使未修改，退出前也询问是否保存 | `官方文档` |
| A220 | 有冲突时强制确认保存 | /saverequiredonconflicts | 命令行开关：有冲突时退出前询问是否保存 | `官方文档` |

### A.12 右键菜单（上下文菜单）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A221 | 使用此文本块 | Use th&is text block | 采用当前视角下的这个块 | `源码资源` |
| A222 | 使用来自左侧的文本块 | Use text block from &left | 采用左窗格对应块 | `源码资源` |
| A223 | 使用来自右侧的文本块 | Use text block from &right | 采用右窗格对应块 | `源码资源` |
| A224 | 左块先于右块 | Use text block from l&eft before right | 两块都取，左前右后 | `源码资源` |
| A225 | 右块先于左块 | Use text block from r&ight before left | 两块都取，右前左后 | `源码资源` |
| A226 | 使用整个文件 | Use this &whole file | 用当前窗格整个文件替换结果 | `源码资源` |
| A227 | 使用另一文本块 | Use &other text block | 采用对侧窗格的块 | `源码资源` |
| A228 | 使用整个另一文件 | Use whole other &file | 用对侧整个文件替换结果 | `源码资源` |
| A229 | 前置此块到左 | Prepend this block to left | 把当前块插到左侧对应位置之前 | `源码资源` |
| A230 | 在左侧使用此块 | Use this block on left | 把当前块放到左侧 | `源码资源` |
| A231 | 追加此块到左 | Append this block to left | 把当前块插到左侧对应位置之后 | `源码资源` |
| A232 | 前置右块 | Prepend right block | 把右块前置 | `源码资源` |
| A233 | 使用右块 | Use right block | 使用右窗格块 | `源码资源` |
| A234 | 追加右块 | Append right block | 把右块追加 | `源码资源` |
| A235 | 行尾样式 | End of Line Style | 右键菜单直接切换该文件的行尾风格 | `源码资源` |
| A236 | 文件编码 | File Encoding | 右键菜单直接切换该文件的编码 | `源码资源` |
| A237 | 标记此块 | Mark this block | 把该块标记为"要包含的改动" | `源码资源` |
| A238 | 取消标记此块 | Unmark this block | 取消该块的标记 | `源码资源` |
| A239 | 仅保留已标记块 | Leave only marked blocks | 反转改动——只保留已标记块（等价于官方文档里的 `Use left file except marked blocks`） | `源码资源` |
| A240 | 右键菜单-剪切/复制/粘贴 | Cut / Copy / Paste | 标准文本编辑项（`IDS_EDIT_CUT/COPY/PASTE`） | `源码资源` |
| A241 | 右键菜单-转换制表符 | Convert tabs to spaces | 选中文本的 Tab→空格转换 | `源码资源` |
| A242 | 右键菜单-转换空格为 Tab | Convert spaces to tabs | 选中文本的空格→Tab 转换 | `源码资源` |
| A243 | 右键菜单-去除右侧空白 | Trim right | 去除行尾空白 | `源码资源` |
| A244 | 右键菜单-定位到资源管理器 | Explore to | 在资源管理器中打开当前文件所在目录 | `源码资源` |
| A245 | 头部-单独显示差异 | Show diff separately | 三栏视图下点视图头部可切换为"该栏相对 Base 的独立差异"（`IDS_HEADER_DIFFLEFTTOBASE` / `RIGHTTOBASE`） | `源码资源` |

### A.13 编码 / 行尾 / Tab（状态栏组合框）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A246 | 编码选择 | Encoding | 每个窗格独立选择编码：ASCII(本地) / UTF8 / UTF16LE / UTF16BE / UTF32LE / UTF32BE，均可带或不带 BOM（每视图 20 个选项） | `官方文档` |
| A247 | 编码组合框 tooltip | `IDS_ENCODING_COMBO_TOOLTIP` | 悬停显示编码说明 | `源码资源` |
| A248 | 行尾选择 | Line Endings | 选择 CRLF / LF / CR 等；**注意**：改动会改写整个文件的所有行尾，即使加载时行尾不统一 | `官方文档` |
| A249 | Tab 模式选择 | Tabs | 组合框首项决定按 Tab 键插入 Tab 还是空格 | `官方文档` |
| A250 | 智能 Tab | Smart tab char | 根据相邻行的字符判断应插入 Tab 还是空格 | `官方文档` |
| A251 | Tab 宽度 | Tab size | 一个 Tab 展开为多少空格（编辑与显示均适用） | `官方文档` |
| A252 | 默认 UTF-8 | Default to UTF-8 encoding | 设置项：ANSI 文件按 UTF-8 加载并以 UTF-8 保存 | `官方文档` |
| A253 | EditorConfig 支持 | EditorConfig | 检测 `.editorconfig`，支持 `indent_style`（space/tab）与 `indent_size` | `官方文档` |
| A254 | 三个组合框的占位命令 | Dummy Command Handlers | 每个窗格各有编码/EOL/Tab 三个组合框命令占位（共 9 个） | `源码逻辑` |
| A255 | 状态栏左右视图标签 | Left View / Right View | 状态栏分区标题 `IDS_STATUSBAR_LEFTVIEW` / `RIGHTVIEW` / `BOTTOMVIEW` | `源码资源` |

### A.14 状态栏统计

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A256 | 新增行计数 | Lines added | 状态栏显示 `+ %d` | `源码资源` |
| A257 | 删除行计数 | Lines removed | 状态栏显示 `- %d` | `源码资源` |
| A258 | 冲突行计数 | Conflicted lines | 状态栏显示 `! %d` | `源码资源` |
| A259 | 未解决冲突总数 | Conflicts: N | 状态栏汇总未解决冲突数 `Conflicts: %d` | `官方文档` |
| A260 | 状态栏点击跳转 | Indicator Click | 点击状态栏的左/右/底视图指示器可切换焦点到对应窗格 | `源码逻辑` |

### A.15 补丁应用（Applying Patches）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A261 | 打开补丁对话框 | Apply Patch… | `File → Open` 中选择补丁模式，需指定补丁文件路径 + 应用目标文件夹 | `官方文档` |
| A262 | 补丁文件列表窗口 | Patch File List | 解析补丁后弹出的小窗口，列出补丁涉及的所有文件 | `官方文档` |
| A263 | 可应用判定-黑色 | Clean patch | 文件名显示为黑色 = 该文件可无冲突应用 | `官方文档` |
| A264 | 可应用判定-红色 | Outdated patch | 文件名显示为红色 = 该文件已被本地修改，无法直接应用 | `官方文档` |
| A265 | 预览补丁效果 | Preview patched file | 右键菜单：预览补丁对该文件的效果（应用但不保存）；**双击 = 预览** | `官方文档` |
| A266 | 应用选中文件 | Patch selected files | 右键菜单：应用并保存选中文件的改动 | `官方文档` |
| A267 | 应用全部文件 | Patch all files | 右键菜单：应用并保存列表中**全部**文件的改动 | `官方文档` |
| A268 | 从剪贴板打开补丁 | Open from clipboard | 从剪贴板读取补丁内容 | `源码资源` |
| A269 | 补丁状态列 | Path / State | 列表包含 `Path` 与 `State` 两列；状态值含 `patched` / `Conflicted` / `Error` | `源码资源` |
| A270 | 无法干净应用提示 | Could not be cleanly patched | tooltip 提示 `%s\nCould not be cleanly patched.` | `源码资源` |
| A271 | 补丁进度对话框 | Patching… | 显示 `Patching file '%s'` 进度与标题 `Patching` | `源码资源` |
| A272 | 补丁源/目标标题 | patchoriginal / patchpatched | 补丁视图的标题可显示原始文件名与打补丁后文件名 | `官方文档` |
| A273 | 格式限制 | Unified Diff only | **仅支持 Unified Diff 格式，且仅支持由 Git 工作副本生成的补丁**；CVS 等格式不支持 | `官方文档` |
| A274 | 反向补丁 | /reversedpatch | 命令行开关：交换左右两个文件（用于反向应用补丁） | `官方文档` |
| A275 | 由两文件生成补丁 | Create patch file | `ID_EDIT_CREATEUNIFIEDDIFFFILE`：把两个文件的差异导出为补丁 | `源码资源` |
| A276 | 补丁目标路径 | /patchpath | 命令行指定补丁应用的目标路径；不指定则自动推断（可能非常慢） | `官方文档` |

### A.16 打开文件对话框（Open Files）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A277 | Base 路径 | Base | 三方比对的共同祖先文件；两方比对时即左文件 | `官方文档` |
| A278 | Mine 路径 | Mine | 三方比对中的"我方"文件（右窗格）；两方比对时即右文件 | `官方文档` |
| A279 | Theirs 路径 | Theirs | 三方比对中的"对方"文件（左窗格） | `官方文档` |
| A280 | Merged 路径 | Merged | 合并结果输出文件路径（三栏模式）；未指定时需用 Save As | `官方文档` |
| A281 | 模式二选一 | Diff vs Patch Mode | 对话框顶部先选"比对/合并文件"还是"应用补丁"，据此启用对应输入框 | `官方文档` |
| A282 | 至少两路径校验 | Minimum two paths | 比对模式下 Base/Mine/Theirs 至少填两个 | `官方文档` |
| A283 | 浏览按钮 | Browse buttons | 每个路径框旁的 `...` 按钮 | `官方文档` |

### A.17 设置（Settings）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A284 | 设置对话框 | Settings Dialog | 左侧树 `General / Colors / Misc`，右侧页；由 `ID_VIEW_OPTIONS` 打开 | `源码资源` |
| A285 | 备份原文件 | Backup original file | 保存前把原文件改名为 `filename.bak` | `官方文档` |
| A286 | 加载后跳首个差异 | Jump to first difference when loading | 见 A154 | `官方文档` |
| A287 | 加载后跳首个冲突 | Jump to first conflict when loading | 见 A155 | `官方文档` |
| A288 | 默认 UTF-8 | Default to UTF-8 encoding | 见 A252 | `官方文档` |
| A289 | 默认单栏 | Use one-pane view as default for 2-file diff | 见 A25 | `官方文档` |
| A290 | 显示行号 | Show linenumbers | 显示行号列 | `官方文档` |
| A291 | 新文件自动加入 Git | Add new files automatically to Git | 保存新文件时自动 `git add` | `官方文档` |
| A292 | 使用 Ribbon | Use Ribbons | 启用 Ribbon 界面（Office 2007 风格），默认开启 | `官方文档` |
| A293 | 使用空格 | Use spaces | 按 Tab 插入空格而非 Tab 字符，默认关闭 | `官方文档` |
| A294 | 智能 Tab 字符 | Smart tab char | 见 A250，默认关闭 | `官方文档` |
| A295 | Tab 宽度设置 | Tab size | 默认 4 | `官方文档` |
| A296 | EditorConfig | Enable EditorConfig | 见 A253 | `官方文档` |
| A297 | 字体设置 | Font | 默认 Courier New 10 | `官方文档` |
| A298 | 行内差异最大行长 | Max line length for inline diffs | 超过该长度的行不做行内差异（性能保护），默认 3000 | `官方文档` |
| A299 | 忽略行尾（设置项） | Ignore line endings (recommended) | 见 A184 | `官方文档` |
| A300 | 忽略大小写（设置项） | Ignore case changes | 见 A185 | `官方文档` |
| A301 | 补丁上下文行数 | Context lines for patches | 生成补丁时的上下文行数；默认 `-1` 表示由 git 或 `diff.context` 决定 | `官方文档` |
| A302 | 设置持久化 | Registry Persistence | 全部设置存于 `HKEY_CURRENT_USER\Software\TortoiseGitMerge\` | `源码逻辑` |
| A303 | Ribbon 布局持久化 | Ribbon Settings File | Ribbon 定制存于 `TortoiseGitMerge-RibbonSettings` 文件 | `源码逻辑` |
| A304 | 设置入口在 Ribbon 下 | Settings via Application Menu | Ribbon 模式下只能从左上角圆形菜单进入 Settings | `源码逻辑` |

### A.18 颜色方案（Color Settings Page）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A305 | 颜色-普通 | Normal | 未变更行，或变更被隐藏的行 | `官方文档` |
| A306 | 颜色-新增 | Added | 被添加的行 | `官方文档` |
| A307 | 颜色-删除 | Removed | 被删除的行 | `官方文档` |
| A308 | 颜色-修改 | Modified | 改动较小的行（走行内差异）；该色用于行中**未变**部分；若未启用行内差异着色则此色不用，改动行一律显示为替换 | `官方文档` |
| A309 | 颜色-冲突 | Conflicted | 两个文件都改动的同一行 | `官方文档` |
| A310 | 颜色-冲突已解决 | Conflict resolved | 两文件都改但用户已选定版本的行 | `官方文档` |
| A311 | 颜色-空行 | Empty | 对侧窗格新增了行、本侧无对应行的位置 | `官方文档` |
| A312 | 颜色-行内新增文本 | Inline added text | 行内差异中新增的片段 | `官方文档` |
| A313 | 颜色-行内删除文本 | Inline removed text | 行内差异中被删除的片段 | `官方文档` |
| A314 | 颜色-杂项空白 | Misc whitespaces | 用于显示空白符的字符颜色（与正文不同） | `官方文档` |
| A315 | 前景/背景双色 | Foreground & Background | 上述每一项都同时可配前景色与背景色 | `官方文档` |
| A316 | 颜色分组 | Line differences / Inline differences / Misc | 颜色页按这三组组织色块 | `官方文档` |
| A317 | 恢复默认颜色 | Restore Default | 一键恢复全部颜色为默认值 | `官方文档` |
| A318 | 深色模式 | Use dark mode | 启用界面深色模式，需 Windows 10 1809+ 且系统已开启应用深色模式 | `官方文档` |

### A.19 快捷键总表

**通用（General）**

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A319 | 退出 | Quit the program | `Ctrl-Q` / `Ctrl-W` / `Escape` | `官方文档` |
| A320 | 复制 | Copy selected text | `Ctrl-C` | `官方文档` |
| A321 | 剪切 | Cut selected text | `Ctrl-X` / `Shift-Del` | `官方文档` |
| A322 | 粘贴 | Paste from clipboard | `Ctrl-V` / `Shift-Insert` | `官方文档` |
| A323 | 撤销 | Undo last edits | `Ctrl-Z` / `Alt-Backspace` | `官方文档` |
| A324 | 查找 | Open Find dialog | `Ctrl-F` | `官方文档` |
| A325 | 打开文件 | Open files to diff/merge | `Ctrl-O` | `官方文档` |
| A326 | 保存 | Save the changes | `Ctrl-S` | `官方文档` |
| A327 | 另存为 | Save as… | `Ctrl-Shift-S` | `官方文档` |
| A328 | 下一差异 | Go to next difference | `F7` / `Ctrl-Down` | `官方文档` |
| A329 | 上一差异 | Go to previous difference | `Shift-F7` / `Ctrl-Up` | `官方文档` |
| A330 | 重新加载 | Reload files, revert changes | `Ctrl-R` | `官方文档` |
| A331 | 切换显示空白符 | Toggle show whitespaces | `Ctrl-T` | `官方文档` |
| A332 | 切换折叠 | Toggle collapse unchanged | `Ctrl-L` | `官方文档` |
| A333 | 切换折行 | Toggle line wrapping | `Ctrl-P` | `官方文档` |
| A334 | 跳转行 | Go to line | `Ctrl-G` | `官方文档` |
| A335 | 全选 | Select all text | `Ctrl-A` | `官方文档` |
| A336 | 左右滚动 | Scroll display left/right | `Ctrl-鼠标滚轮` | `官方文档` |
| A337 | 窗格切换 | Switch left/right/bottom view | `Ctrl-Tab` | `官方文档` |
| A338 | 切换标记 | Toggle marking the selected change | `Ctrl-M` | `官方文档` |

**Diff 模式**

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A339 | 单/双栏切换 | Toggle one-pane / two-pane diff | `Ctrl-D` | `官方文档` |
| A340 | 切换标记（Diff） | Toggle marking the selected change | `Ctrl-M` | `官方文档` |
| A341 | 交换视图 | Switches views | `Ctrl-U` | `官方文档` |
| A342 | 使用左块（Diff） | Use left block | `F12` | `官方文档` |

**冲突解决模式**

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A343 | 下一冲突 | Go to next conflict | `F8` | `官方文档` |
| A344 | 上一冲突 | Go to previous conflict | `Shift-F8` | `官方文档` |
| A345 | 使用左块 | Use left block | `Ctrl-F9` | `官方文档` |
| A346 | 先左后右 | Use left block then right block | `Ctrl-Shift-F9` | `官方文档` |
| A347 | 使用右块 | Use right block | `Ctrl-F10` | `官方文档` |
| A348 | 先右后左 | Use right block then left block | `Ctrl-Shift-F10` | `官方文档` |

**Ribbon Keytip**

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A349 | Ribbon Keytip | Keytips | Tab=`E`；Open=`o1`；Save=`S`；Undo=`Z`；Redo=`Y`；Reload=`R`；Enable Edit=`E`；Copy=`C`；Paste=`V`；Find=`F`；Goto Line=`G`；Show Whitespaces=`W`；Wrap Lines=`L`；上一/下一差异=`PD`/`ND`；上一/下一冲突=`PC`/`NC`；行内差异=`PI`/`NI` | `源码资源` |

### A.20 命令行（Command Line Switches）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A350 | 显示帮助对话框 | /? | 弹出命令行开关说明对话框 | `官方文档` |
| A351 | 帮助 | /help | 同 `/?` | `官方文档` |
| A352 | Base 文件 | /base:path | 指定三方比对的基础（共同祖先）文件；两方比对时即左文件 | `官方文档` |
| A353 | Base 显示名 | /basename:name | 视图中代替完整路径显示的 Base 名称；三栏模式显示在某视图标题的 tooltip 中 | `官方文档` |
| A354 | Base EditorConfig 名 | /basereflectedname:name | 用于 EditorConfig 模板匹配的名字 | `官方文档` |
| A355 | Theirs 文件 | /theirs:path | 三方比对的对方文件，显示在左窗格 | `官方文档` |
| A356 | Theirs 显示名 | /theirsname:name | 视图中代替路径显示的名称 | `官方文档` |
| A357 | Theirs EditorConfig 名 | /theirsreflectedname:name | EditorConfig 模板名 | `官方文档` |
| A358 | Mine 文件 | /mine:path | 三方比对的我方文件，显示在右窗格 | `官方文档` |
| A359 | Mine 显示名 | /minename:name | 视图中代替路径显示的名称 | `官方文档` |
| A360 | Mine EditorConfig 名 | /minereflectedname:name | EditorConfig 模板名 | `官方文档` |
| A361 | Merged 文件 | /merged:path | 合并结果保存路径；未设置则退出时询问保存位置 | `官方文档` |
| A362 | Merged 显示名 | /mergedname:name | 视图中代替路径显示的名称 | `官方文档` |
| A363 | Merged EditorConfig 名 | /mergedreflectedname:name | EditorConfig 模板名 | `官方文档` |
| A364 | 补丁目标路径 | /patchpath:path | 指定补丁应用的目标路径；不设置则自动推断（可能非常慢） | `官方文档` |
| A365 | 强制询问保存 | /saverequired | 即使未修改文件，退出前也询问保存 | `官方文档` |
| A366 | 有冲突时强制询问 | /saverequiredonconflicts | 有冲突时，即使未修改也询问保存 | `官方文档` |
| A367 | 补丁原始文件名 | /patchoriginal:name | 用于视图标题 | `官方文档` |
| A368 | 补丁结果文件名 | /patchpatched:name | 用于视图标题 | `官方文档` |
| A369 | 补丁文件路径 | /diff:path | 指定要应用到目录的补丁/差异文件路径 | `官方文档` |
| A370 | 强制单栏 | /oneway | 强制以单栏视图启动，忽略用户设置 | `官方文档` |
| A371 | 反向补丁 | /reversedpatch | 交换待比对的两个文件的左右位置 | `官方文档` |
| A372 | 创建统一差异 | /createunifieddiff | 由 `/origfile` 与 `/modifiedfile` 生成补丁并写到 `/outfile`；**设置此项后其他参数全部忽略** | `官方文档` |
| A373 | 原始文件 | /origfile:path | 配合 `/createunifieddiff` | `官方文档` |
| A374 | 修改后文件 | /modifiedfile:path | 配合 `/createunifieddiff` | `官方文档` |
| A375 | 输出补丁路径 | /outfile:path | 未设置时弹出保存对话框 | `官方文档` |
| A376 | 跳转到行 | /line:N | 加载后跳转到第 N 行 | `官方文档` |
| A377 | 只读模式 | /readonly | 禁用编辑能力 | `官方文档` |
| A378 | 位置参数兼容形式 | BaseFilePath MyFilePath [TheirsFilePath] | 兼容其他 diff 工具的裸文件名写法：2 个文件=两方比对；3 个文件=以第一个为 BASE 的三方比对 | `官方文档` |
| A379 | 参数冒号语法 | /switch:value | 需要带路径/字符串的开关统一用 `开关:值` 形式（如 `/base:"c:\folder\file.txt"`） | `官方文档` |

### A.21 错误与边界提示

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| A380 | 差异引擎错误 | Diff engine aborted | `The diffing engine aborted because of an error: %s` | `源码资源` |
| A381 | 行尾不一致无法比对 | Inconsistent newlines | `Cannot show diff because of inconsistent newlines in the file.` | `源码资源` |
| A382 | 文件打开失败 | Could not open the file | `Could not open the file %s` | `源码资源` |
| A383 | 非文本文件 | Not a valid text file | `The file %s is not a valid text file!` | `源码资源` |
| A384 | 不支持目录比对 | Can't diff directories | `%s is a directory, not a file! TortoiseGitMerge can't diff directories.` | `源码资源` |
| A385 | 文件过大 | File too big | `The file is too big` | `源码资源` |

---

## B. TortoiseGit 客户端里的其他比对入口

### B.1 资源管理器右键菜单（diff 相关项）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B01 | 差异比对 | TortoiseGit → Diff | 对单文件：与 BASE（上次检出/更新后的原始副本）比较；对两个选中文件：两文件互比（**最后点击/获得焦点的那个视为"较新"的一方**） | `官方文档` |
| B02 | 与上一版本比对 | Diff with previous version | 与上一次提交之前的修订比较（即"是什么改动把文件变成现在这样"）；对文件夹或无选择时弹出比较修订对话框 | `官方文档` |
| B03 | 显示日志 | Show log… | 打开日志对话框 | `官方文档` |
| B04 | 修订图 | Revision Graph… | 打开修订图窗口 | `官方文档` |
| B05 | 追溯 | Blame… | 打开 TortoiseGitBlame（别名 annotate） | `官方文档` |
| B06 | 检查修改 | Check for Modifications… | 打开工作副本状态对话框 | `官方文档` |
| B07 | 解决冲突 | Resolve… | 对文件夹打开冲突文件列表对话框；对单文件直接标记已解决 | `官方文档` |
| B08 | 编辑冲突 | Edit Conflicts | 生成 `.BASE/.LOCAL/.REMOTE` 临时文件并启动配置的冲突编辑器 | `官方文档` |
| B09 | 已解决 | Resolved | 用 `git add` 把文件标记为已解决，并删除三个临时文件 | `官方文档` |
| B10 | 用 'mine' 解决冲突 | Resolve conflict using 'mine' | 采用 HEAD 版本（二进制冲突场景） | `官方文档` |
| B11 | 用 'theirs' 解决冲突 | Resolve conflict using 'theirs' | 采用被合并修订/分支版本 | `官方文档` |
| B12 | 合并 | Merge… | 打开合并对话框 | `官方文档` |
| B13 | 中止合并 | Abort Merge | 中止合并（Reset 的 Mixed / Hard 模式，Hard 会**永久删除**本地改动，不走回收站） | `官方文档` |
| B14 | 变基 | Rebase… | 打开变基对话框 | `官方文档` |
| B15 | 拣选 | Cherry Pick… | 把某提交拣选到 HEAD | `官方文档` |
| B16 | 创建补丁序列 | Create Patch Serial… | 由提交生成补丁序列 | `官方文档` |
| B17 | 审阅/应用单个补丁 | Review/apply single patch… | 对 `.patch` / `.diff` 文件，在正确目录层级应用 | `官方文档` |
| B18 | 应用补丁序列 | Apply Patch Serial… | 逐个应用补丁 | `官方文档` |
| B19 | 发送邮件 | Send Mail… | 发送补丁邮件 | `官方文档` |
| B20 | 浏览引用 | Browse Reference… | 打开引用浏览器 | `官方文档` |
| B21 | 引用日志 | RefLog | **默认在扩展菜单中**，需按住 `Shift` 右键才可见 | `官方文档` |
| B22 | 仓库浏览器 | Repo-browser | 打开仓库浏览器 | `官方文档` |
| B23 | 提交 | Commit → "branch" | 打开提交对话框 | `官方文档` |
| B24 | 与工作树比较（引用浏览器内） | Compare to working tree | 在引用浏览器中选中一个分支后，比较该分支与当前工作树 | `官方文档` |
| B25 | 扩展右键菜单 | Extended Context Menu | 按住 `Shift` 右键展开包含低频命令的扩展菜单 | `官方文档` |
| B26 | 拖放右键菜单 | Drag Context Menu | 右键拖放文件/文件夹时的 TortoiseGit 操作菜单，可在设置中关闭 | `官方文档` |
| B27 | 次要差异工具 | Shift + Diff | 若配置了替代差异工具，`Shift`+`Diff` 使用次要工具，普通 `Diff` 用主工具 | `官方文档` |
| B28 | 多个 TortoiseGit 菜单项 | Multiple Menu Entries | 快捷方式场景下会出现两个 TortoiseGit 项（一个针对快捷方式本身、一个针对目标），图标右下角有类型指示 | `官方文档` |
| B29 | 图标叠加 | Icon Overlays | 绿勾=normal / 红叹号=modified / 黄叹号=conflict / 加号=已加入 / 删除标记 / 忽略 / 未版本 / assume-valid / skip-worktree | `官方文档` |
| B30 | 属性页 | Explorer Properties → Git 页 | 资源管理器中文件/文件夹属性的 Git 标签页 | `官方文档` |

### B.2 工作副本状态对话框（Check for Modifications）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B31 | 文件列表 | Changed files list | 列出工作树中所有发生变化的文件以及未版本文件 | `官方文档` |
| B32 | 着色-蓝色 | Blue | 本地已修改项 | `官方文档` |
| B33 | 着色-紫色 | Purple | 已添加项；带历史的添加在状态列有 `+`，tooltip 显示复制来源 | `官方文档` |
| B34 | 着色-暗红 | Dark red | 已删除 / 缺失项 | `官方文档` |
| B35 | 着色-绿色 | Green | 本地与仓库都修改，更新时会合并，**可能**冲突 | `官方文档` |
| B36 | 着色-亮红 | Bright red | 本地改+仓库删 或 仓库改+本地删，**一定**冲突 | `官方文档` |
| B37 | 着色-黑色 | Black | 未变更与未版本项 | `官方文档` |
| B38 | 与基线比较 | Compare with Base | 查看**自己**做的本地改动 | `官方文档` |
| B39 | 以统一差异显示 | Show Differences as Unified Diff | 查看**别人**在仓库中做的改动 | `官方文档` |
| B40 | 还原 | Revert | 单个文件还原；误删文件显示为 Missing 后可用 Revert 找回 | `官方文档` |
| B41 | 删除 | Delete（Shift=永久删除） | 未版本/忽略文件送入回收站，`Shift` 点击则永久删除 | `官方文档` |
| B42 | 列自定义 | Customizable Columns | 右键列头选择显示哪些列，拖拽列边界改宽度，设置持久化 | `官方文档` |
| B43 | 底部显示选项 | Show options | 选择显示哪些条目（忽略文件、未跟踪/未版本文件等） | `官方文档` |
| B44 | 显示本地改动标记文件 | Show ignore local changes flagged files | 查看所有被标为 Assume valid / Skip worktree 的文件，并可重置标记 | `官方文档` |
| B45 | 保存统一差异 | Save unified diff | 生成包含全部未提交改动的统一补丁并用配置的差异查看器打开 | `官方文档` |
| B46 | Stash 下拉 | Stash | 快速访问 Git stash 的下拉菜单 | `官方文档` |
| B47 | 提交按钮 | Commit | 以当前对话框的同一文件夹/文件打开提交对话框；选中文件后走右键 `Commit…` 可只提交子集 | `官方文档` |
| B48 | 拖拽导出 | Drag out | 把文件拖到文本编辑器/IDE 中 | `官方文档` |
| B49 | 变更列表分组 | Changelists | 用 `Move to changelist` 分组，分组标题可整体勾选 | `官方文档` |
| B50 | 子模块差异入口 | Compare with base（子模块） | 对子模块项右键进入子模块差异对话框 | `官方文档` |

### B.3 提交对话框（Commit Dialog）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B51 | 变更文件列表 | Changes made | 列出所有变更文件（含新增、删除、未版本文件） | `官方文档` |
| B52 | 勾选框 | Checkbox | 点击每个条目左侧的勾选框选择/取消选择要提交的文件 | `官方文档` |
| B53 | 整个项目 | Whole Project | 左侧复选框，覆盖文件/文件夹过滤，显示整个仓库的所有变更；该设置按仓库记忆 | `官方文档` |
| B54 | 双击比对 | Double-click to diff | 双击任何已修改文件启动外部差异工具显示改动 | `官方文档` |
| B55 | 右键菜单 | Context Menu | 提供更多选项 | `官方文档` |
| B56 | 提交后还原 | Restore after commit | 右键项：先复制文件，再让用户在 TortoiseGitMerge 中撤销不想提交的改动，提交后自动还原副本 | `官方文档` |
| B57 | 列自定义 | Customizable Columns | 右键列头选择列、拖拽改宽度，设置持久化 | `官方文档` |
| B58 | 日志消息框 | Message | 输入提交日志；支持 `*粗体*` / `_下划线_` / `^斜体^` 简化格式 | `官方文档` |
| B59 | 拼写检查 | Spellchecker | 高亮拼错的词，右键获取建议、加入个人词典 | `官方文档` |
| B60 | 自动补全 | Auto-completion | 文件内容与文件名自动补全；输入 3 字符或 `Ctrl+Space` 触发下拉 | `官方文档` |
| B61 | 片段 | Commit message snippets | 输入片段快捷码后在下拉中选择即插入全文；自定义文件 `%APPDATA%\TortoiseGit\snippet.txt`（`#` 为注释，支持 `\t\r\n\\` 转义） | `官方文档` |
| B62 | 粘贴最近消息 | Paste Recent messages | 复用最近输入过的日志消息；`Delete` 键可删除单条 | `官方文档` |
| B63 | 附加签名 | Add Signed-off-by | 在日志末尾追加姓名与邮箱 | `官方文档` |
| B64 | 粘贴文件名列表 | Paste filename list | 把已勾选路径插入日志消息 | `官方文档` |
| B65 | 拖拽插入路径 | Drag files into message | 从文件列表拖动文件到编辑框即可插入路径 | `官方文档` |
| B66 | 提交到新分支 | Commit to a new branch | 勾选新分支复选框并输入分支名 | `官方文档` |
| B67 | 提交主按钮下拉 | Commit drop-down | `ReCommit`（提交后保持对话框打开）/ `Commit & push`（提交并立即推送；无上游时打开推送对话框） | `官方文档` |
| B68 | 提交进度对话框 | Commit Progress | 显示提交进度；左下角菜单按钮提供 ReCommit / Push 快捷入口 | `官方文档` |
| B69 | 变更列表 | Change Lists | 按变更列表分组显示，点组标题可全选，勾选任一项即全组勾选 | `官方文档` |
| B70 | ignore-on-commit | ignore-on-commit changelist | 加入此特殊变更列表的文件在提交对话框中**自动取消勾选** | `官方文档` |
| B71 | Bug ID 输入框 | Bug-ID / Issue-No | 由 bug 跟踪集成启用，多个 issue 用逗号分隔 | `官方文档` |
| B72 | 键盘提交 | Ctrl+return | 从键盘访问 OK/提交按钮 | `官方文档` |
| B73 | 分隔条调整 | Adjust message box size | 拖动 Message 与 Changes made 之间的分隔条 | `官方文档` |
| B74 | 默认始终显示暂存文件 | Staged files always shown | 提交对话框**总是**显示已暂存文件，即使在其他文件夹启动（设计如此，避免 merge 场景漏提交） | `官方文档` |
| B75 | 拖放加入文件 | Drag and drop in | 可从其他窗口拖入文件；拖入未版本文件会自动 git add | `官方文档` |
| B76 | 自动勾选 | Select items automatically | 设置项：默认自动勾选所有已修改项；关闭后从零开始手动挑 | `官方文档` |

### B.4 日志对话框（Log Dialog）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B77 | 三面板布局 | Three-pane layout | 上=修订列表，中=完整日志消息，下=该修订变更的文件/文件夹列表 | `官方文档` |
| B78 | 顶部-修订列表 | Top pane revision list | 每行含日期时间、提交人、日志消息开头 | `官方文档` |
| B79 | 顶部-HEAD 加粗 | HEAD commit bold | HEAD 提交所在行以粗体显示 | `官方文档` |
| B80 | 顶部-工作树更改 | "Working tree changes" | 代表工作树当前未提交状态的虚拟条目 | `官方文档` |
| B81 | 操作列-modified | Actions: *modified* | 第 1 列，表示该修订修改了文件/目录 | `官方文档` |
| B82 | 操作列-added | Actions: *added* | 第 2 列 | `官方文档` |
| B83 | 操作列-deleted | Actions: *deleted* | 第 3 列 | `官方文档` |
| B84 | 操作列-replaced | Actions: *replaced* | 第 4 列，表示重命名/替换 | `官方文档` |
| B85 | 图列 | Graph column | 用线条展示提交、合并、分支关系 | `官方文档` |
| B86 | 图-圆形节点 | Circles | 无分支的普通提交 | `官方文档` |
| B87 | 图-方形节点 | Squares | 合并提交，或"此处产生分支"的普通提交 | `官方文档` |
| B88 | 图-线条颜色 | Graph line colors | 线条与形状颜色用于区分分支，颜色可配置 | `官方文档` |
| B89 | 图-线条宽度/节点大小 | Graph line width / node size | 颜色设置第 3 页可调 | `源码资源` |
| B90 | 标签-普通 tag | Normal tag | 黄色矩形 | `官方文档` |
| B91 | 标签-注释 tag | Annotated tag | 黄色矩形 + 右侧尖角（apex） | `官方文档` |
| B92 | 标签-当前分支 | Active branch | 默认**暗红色**矩形 | `官方文档` |
| B93 | 标签-本地分支 | Local branches | **绿色**矩形；若有远程跟踪则用**圆角**框 | `官方文档` |
| B94 | 标签-远程分支 | Remote branches | **桃色**矩形；远程跟踪分支用圆角框标识 | `官方文档` |
| B95 | 标签-Stash | Stash | **暗灰色**矩形 | `官方文档` |
| B96 | 标签-Bisect bad | bad versions | **浅红色**矩形 | `官方文档` |
| B97 | 标签-Bisect good | known good | **蓝色**矩形 | `官方文档` |
| B98 | 标签-Bisect skip | skip | **灰色**矩形 | `官方文档` |
| B99 | 标签位置 | Label position | 可配置为画在提交消息右侧，或显示包含整条提交消息的文本 | `官方文档` |
| B100 | 标签符号化 | Symbolize ref names | 上箭头 `↑` 替代远程名前缀；等价符 `≡` 替代与本地分支同名的远程分支名部分 | `官方文档` |
| B101 | 上下文菜单按 ref 类型优化 | Ref-aware context menu | 在本地分支标签上右键直接提供 switch/push；远程分支则不提供 push | `官方文档` |
| B102 | 菜单-与工作树比较 | Compare with working tree | 所选修订 vs 工作树；对文件夹日志显示变更文件列表可逐个查看 | `官方文档` |
| B103 | 菜单-统一差异 | Show changes as unified diff | 以 GNU patch 格式查看该修订的全部改动 | `官方文档` |
| B104 | 菜单-与上一修订比较 | Compare with previous revision | 所选修订 vs 前一修订 | `官方文档` |
| B105 | 菜单-浏览仓库 | Browse repository | 打开仓库浏览器查看该修订时的文件/文件夹 | `官方文档` |
| B106 | 菜单-重置 | Reset (current branch) to this | 把 HEAD 重置到所选提交 | `官方文档` |
| B107 | 菜单-切换/检出 | Switch / Checkout to revision | 把工作树更新到所选修订 | `官方文档` |
| B108 | 菜单-建分支 | Create branch from revision | 基于所选修订创建分支 | `官方文档` |
| B109 | 菜单-建标签 | Create tag from revision | 在所选修订上创建标签 | `官方文档` |
| B110 | 菜单-变基 | Rebase (current branch) to this | 把当前分支变基到所选提交之上 | `官方文档` |
| B111 | 菜单-导出此版本 | Export this version… | 把所选修订导出为归档（zip 等） | `官方文档` |
| B112 | 菜单-撤销此提交 | Revert change by this commit | 把该提交的改动反向应用到工作树，可立即提交或稍后编辑 | `官方文档` |
| B113 | 菜单-编辑注释 | Edit notes | 编辑该提交的 notes | `官方文档` |
| B114 | 菜单-折叠修订 | Collapse revisions | 隐藏到合并/分支点或带标签提交之间的父提交；折叠项在图中显示为**空心圆与方** | `官方文档` |
| B115 | 菜单-展开隐藏修订 | Expand hidden revisions | 还原 Collapse revisions | `官方文档` |
| B116 | 菜单-拣选此提交 | Cherry Pick this commit | 在 HEAD 之上拣选 | `官方文档` |
| B117 | 菜单-开始二分 | Bisect start | 开始二分查找定位引入 bug 的改动 | `官方文档` |
| B118 | 菜单-格式化补丁 | Format Patch… | 从该提交创建补丁 | `官方文档` |
| B119 | 菜单-复制 SHA-1 | Copy SHA-1 to clipboard | 复制提交哈希 | `官方文档` |
| B120 | 菜单-复制到剪贴板 | Copy to clipboard | 复制修订号、作者、日期、日志消息及变更项列表 | `官方文档` |
| B121 | 菜单-复制日志消息 | Copy log message to clipboard | 只复制日志消息 | `官方文档` |
| B122 | 菜单-搜索日志消息 | Search log messages… | 搜索日志消息以及 Git 生成的操作摘要（含底部面板内容），也可用于快速搜 refs | `官方文档` |
| B123 | 菜单-该提交所在分支 | Shows branches this commit is on | 列出该提交所属的本地与远程分支 | `官方文档` |
| B124 | 双选-比较修订 | Compare revisions | 用可视化差异工具比较两个所选修订 | `官方文档` |
| B125 | 双选-统一差异 | Show differences as unified diff | 两个修订之间的统一差异（文件与文件夹均可） | `官方文档` |
| B126 | 双选-撤销这些提交 | Revert changes by these commits | 反向应用所选修订的改动 | `官方文档` |
| B127 | 双选-合并为一个提交 | Combine to one commit | 把连续提交合并成一个 | `官方文档` |
| B128 | 双选-拣选所选提交 | Cherry Pick selected commits | 把所选提交拣选到 HEAD | `官方文档` |
| B129 | 双选-格式化补丁 | Format Patch… | 在两个所选提交之间创建补丁 | `官方文档` |
| B130 | 双选-复制 SHA-1（多行） | Copy SHA-1 to clipboard | 多个哈希以 CRLF 分隔 | `官方文档` |
| B131 | 双选-复制日志消息（多行） | Copy log messages to clipboard | 便于准备 release notes | `官方文档` |
| B132 | Shift 扩展菜单 | Shift for extended menu | 按住 `Shift` 打开右键可看到更多选项（从每个提交推送、打开日志） | `官方文档` |
| B133 | 底部面板-与基线比较 | Compare with base | 所选文件 vs base 版本 | `官方文档` |
| B134 | 底部面板-统一差异 | Show as unified diff | **仅对 modified 文件可用** | `官方文档` |
| B135 | 底部面板-与工作树比较 | Compare with working tree | 所选文件 vs 工作树 | `官方文档` |
| B136 | 底部面板-还原到此修订 | Revert changes to this revision | 把所选文件还原到该修订状态 | `官方文档` |
| B137 | 底部面板-还原到父修订 | Revert changes to parent revision | 还原到该修订之前 | `官方文档` |
| B138 | 底部面板-显示日志 | Show log | 显示该单个文件的修订日志 | `官方文档` |
| B139 | 底部面板-追溯 | Blame… | 打开 Blame 对话框，可 blame 到所选修订 | `官方文档` |
| B140 | 底部面板-另存修订 | Save revision to… | 保存该修订的文件版本到本地 | `官方文档` |
| B141 | 底部面板-导出选择 | Export selection to… | 保存到目标目录，**保留目录结构** | `官方文档` |
| B142 | 底部面板-替代编辑器查看 | View revision in alternative editor | 用 notepad2 等替代编辑器以该提交状态查看 | `官方文档` |
| B143 | 底部面板-打开 | Open / Open with… | 用默认查看器或自选程序打开 | `官方文档` |
| B144 | 底部面板-定位 | Explore to | 用资源管理器打开所在目录 | `官方文档` |
| B145 | 底部面板-复制路径 | Copy paths to clipboard | 复制路径 | `官方文档` |
| B146 | 底部面板-复制全部信息 | Copy all information to clipboard | 含版本信息 | `官方文档` |
| B147 | 过滤器-分支/修订选择器 | Branch/revision filter | 点击打开引用浏览器，可单选或多选引用 | `官方文档` |
| B148 | 过滤器-A B 组合 | "A B" | 显示 A 与 B 两者 | `官方文档` |
| B149 | 过滤器-A...B 组合 | "A...B" | 显示两者的差异 commit（对称差） | `官方文档` |
| B150 | 过滤器-A..B 组合 | "A..B" | 显示 A 到 B 之间的所有 commit | `官方文档` |
| B151 | 过滤器-快捷项 | HEAD / FETCH_HEAD / All / All local branches | 分支过滤器的特殊右键菜单 | `官方文档` |
| B152 | 过滤器-最近使用 | Last manually selected filters | 过滤器菜单中列出最近手动选择过的过滤条件 | `官方文档` |
| B153 | 过滤器-起始日期 | Start date control | 限制输出的起始日期 | `官方文档` |
| B154 | 过滤器-结束日期 | End date control | 限制输出的结束日期 | `官方文档` |
| B155 | 过滤器-搜索框 | Search box | 只显示包含特定短语的消息 | `官方文档` |
| B156 | 过滤器-默认条数限制 | Default limitation of log messages | 在 Dialogs 1 设置页配置；激活时 `From` 标签有**偏蓝背景**；日期选择器右键可配置或禁用 | `官方文档` |
| B157 | 过滤器-搜索字段选择 | Search fields | 主题 / 消息 / 路径 / 作者 / 电子邮件 / 版本 / 引用名称 / 标签信息 / 注释 / Bug ID | `源码逻辑` |
| B158 | 过滤器-正则模式 | regex | 切换使用正则表达式 | `官方文档` |
| B159 | 过滤器-大小写敏感 | Case sensitive | 切换大小写敏感 | `官方文档` |
| B160 | 过滤器-禁用路径搜索加速 | Uncheck "Paths" | 取消勾选 Paths 可显著加快过滤 | `官方文档` |
| B161 | 搜索语法-子串 | Simple sub-string search | 空格分隔的多个子串必须全部匹配 | `官方文档` |
| B162 | 搜索语法-排除 | `-word` | 前导 `-` 表示该子串不应出现 | `官方文档` |
| B163 | 搜索语法-整体反转 | `!expr` | 表达式开头 `!` 反转整个表达式的匹配 | `官方文档` |
| B164 | 搜索语法-强制包含 | `+word` | 前导 `+` 表示应被包含，即使之前被 `-` 排除；**顺序有意义** | `官方文档` |
| B165 | 搜索语法-引号 | `"multi word"` | 用引号包围含空格的字符串；连写两个引号表示字面引号 | `官方文档` |
| B166 | 搜索语法-反斜杠无特殊含义 | `\` not an escape | 简单子串搜索中反斜杠无转义作用 | `官方文档` |
| B167 | 视图-隐藏无关路径 | Hide unrelated changed paths | 隐藏与该路径无关的变更路径 | `官方文档` |
| B168 | 视图-置灰无关路径 | Gray unrelated changed paths | 把无关路径置灰；两项都不勾则完全隐藏 | `官方文档` |
| B169 | 全部分支复选框 | All branches | 覆盖分支过滤显示全仓库日志 | `官方文档` |
| B170 | 显示整个项目复选框 | Show whole project | 覆盖文件/文件夹过滤显示整个仓库；设置按仓库记忆 | `官方文档` |
| B171 | 标签显隐-Tags | View+Labels → Tags | 控制标签是否显示在日志图中 | `官方文档` |
| B172 | 标签显隐-本地分支 | View+Labels → Local branches | 同上 | `官方文档` |
| B173 | 标签显隐-远程分支 | View+Labels → Remote branches | 同上 | `官方文档` |
| B174 | Gravatar 开关 | View → Gravatar | 为特定仓库启用/禁用 Gravatar 头像 | `官方文档` |
| B175 | 遍历-第一父提交 | Walk Behavior → First Parent | 只跟踪第一个父提交，便于理解整体历史 | `官方文档` |
| B176 | 遍历-跳过合并 | Walk Behavior → No merges | 跳过所有合并点 | `官方文档` |
| B177 | 遍历-跟随重命名 | Walk Behavior → Follow renames | 仅对单文件可用，跟踪重命名 | `官方文档` |
| B178 | 遍历-压缩图 | Walk Behavior → Compressed Graph | 简化日志图，只保留合并点、带引用提交等 | `官方文档` |
| B179 | 遍历-只显示带标签提交 | Walk Behavior → Show labeled commits only | 只显示带引用的提交 | `官方文档` |
| B180 | 导航-跳转类型下拉 | Navigation type | 10 项：Author Email / Committer Email / Merge point / Parent 1 / Parent 2 / Tag / Tag (follow) / Branch / Branch (follow) / Selection history | `源码逻辑` |
| B181 | 导航-上/下按钮 | Up / Down jump buttons | 绿色按钮，在当前选中项相对位置导航到匹配该类型的提交 | `官方文档` |
| B182 | 导航-快捷键 | ALT+UP / ALT+DOWN | 同上 | `官方文档` |
| B183 | 导航-选择历史 | Selection History | 记住选过的提交历史，方便回跳；`ALT+LEFT` / `ALT+RIGHT`、Browse Back/Forward、鼠标侧键均可 | `官方文档` |
| B184 | 导航-Shift 浏览 | Shift + selection history | 按住 `Shift` 只滚动并高亮而不改变选中，便于后续选中真正想选的提交 | `官方文档` |
| B185 | 导航-粘贴哈希跳转 | Paste hash to jump | 按 `Ctrl+V` 或 `Shift+Insert` 粘贴哈希到除搜索框外的任意元素即可跳转 | `官方文档` |
| B186 | 统计-统计页 | Statistics | 覆盖周期、修订数量、min/max/average 等 | `官方文档` |
| B187 | 统计-按作者 | Commits by Author | 简单柱状图 / 堆叠柱状图 / 饼图 | `官方文档` |
| B188 | 统计-饼图 Others 阈值 | Others threshold slider | 低于该百分比的活动归入 Others 类别 | `官方文档` |
| B189 | 统计-按日期 | Commits by date | normal 与 stacked 两种视图 | `官方文档` |
| B190 | 统计-作者大小写不敏感 | Authors case insensitive | 把 `DavidMorgan` 与 `davidmorgan` 视为同一人 | `官方文档` |
| B191 | 统计-遵循 mailmap | .mailmap support | 统计遵循 `.mailmap` 文件 | `官方文档` |
| B192 | 刷新 | Refresh (F5) | 重新联系仓库检查更新的日志 | `官方文档` |
| B193 | 列自定义 | Customizable columns | 日志列表与文件列表的列可配置（`GITSLC_COLEXT / COLSTATUS / COLADD / COLDEL` 等掩码） | `源码逻辑` |
| B194 | 日志缓存 | Enable log cache | 在 `.git` 中保存 `tortoisegit.data` / `tortoisegit.index` 提升后续加载性能，默认启用 | `官方文档` |
| B195 | Describe 显示 | Show describe in log | 在提交消息上方显示 `v0.21.0-589-gdeadc43` 形式 | `官方文档` |
| B196 | 分支修订号 | Display branch revision number | 显示 `git rev-list --count --first-parent` 的结果；**不保证唯一** | `官方文档` |
| B197 | 重命名缩写 | Abbreviate renamings | `long/path/{to => for}/file.txt` 形式 | `官方文档` |
| B198 | 双击比较开关 | Can double-click in log list to compare with previous revision | 默认关闭（避免意外触发耗时的 diff） | `官方文档` |
| B199 | 日志字体 | Font for log messages | 单独配置日志消息字体与字号 | `官方文档` |
| B200 | 短日期格式 | Short date/time format | 长格式占屏过多时使用短格式 | `官方文档` |
| B201 | 星号前缀 | Show asterisk log prefix | 日志消息前加星号 | `官方文档` |

### B.5 修订图（Revision Graph）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B202 | 图窗口 | Revision Graph Window | `TortoiseGit → Revision Graph…`；从仓库根获取全部日志，仅显示带引用点的提交 | `官方文档` |
| B203 | 节点 | Node | 每个节点代表一个"该路径下发生了变化"的修订 | `官方文档` |
| B204 | 节点颜色 | Node colors | 不同类型节点用不同颜色区分，颜色可配置 | `官方文档` |
| B205 | 概览窗口 | Overview window | 小窗口显示整图并高亮当前可视区域，拖动高亮区改变显示区域 | `官方文档` |
| B206 | 悬浮提示 | Hover hint box | 鼠标悬停显示修订日期、作者、注释 | `官方文档` |
| B207 | 双选比较 | Compare two revisions | `Ctrl` 左键选两个修订，右键菜单显示二者差异 | `官方文档` |
| B208 | 分支创建点比较 | Compare at branch creation points | 可选在分支**创建点**比较差异 | `官方文档` |
| B209 | 分支端点比较 | Compare at branch end points (HEAD) | 通常更想用此项，即在 HEAD 处比较 | `官方文档` |
| B210 | 统一差异 | View differences as Unified-Diff | 单文件显示全部差异，上下文最少 | `官方文档` |
| B211 | 比较修订 | Context Menu → Compare Revisions | 弹出变更文件列表 | `官方文档` |
| B212 | 双击文件比对 | Double-click a file name | 取出两修订的文件版本并用可视化差异工具比较 | `官方文档` |
| B213 | 显示日志 | Context Menu → Show Log | 右键某修订查看其历史 | `官方文档` |
| B214 | 放大 | ID_VIEW_ZOOMIN | 放大视图 | `源码资源` |
| B215 | 缩小 | ID_VIEW_ZOOMOUT | 缩小视图 | `源码资源` |
| B216 | 100% 缩放 | ID_VIEW_ZOOM100 | 恢复 100% | `源码资源` |
| B217 | 显示全部 | ID_VIEW_ZOOMALL | 缩放以显示全部节点 | `源码资源` |
| B218 | 适应高度 | ID_VIEW_ZOOMHEIGHT | 缩放以适应该高度 | `源码资源` |
| B219 | 适应宽度 | ID_VIEW_ZOOMWIDTH | 缩放以适应该宽度 | `源码资源` |
| B220 | 缩放组合框 | ID_REVGRAPH_ZOOMCOMBO | 工具栏上的缩放比例下拉 | `源码资源` |
| B221 | 另存图 | ID_FILE_SAVEGRAPHAS | 把图导出为文件 | `官方文档` |
| B222 | 导出格式 | Export formats | `.svg` / `.wmf` / `.gv` / `.png` / `.jpg` / `.bmp` / `.gif` | `官方文档` |
| B223 | 无界面导出 | /output:path | 命令行传 `/output:path` 可不显示窗口直接导出图 | `官方文档` |
| B224 | 分组分支 | ID_VIEW_GROUPBRANCHES | 把分支分组 | `源码资源` |
| B225 | 顶部对齐 | ID_VIEW_TOPALIGNTREES | 树状结构顶部对齐 | `源码资源` |
| B226 | 显示概览 | ID_VIEW_SHOWOVERVIEW | 开关概览窗口 | `源码资源` |
| B227 | 自上而下布局 | ID_VIEW_TOPDOWN | 图布局方向自上而下 | `源码资源` |
| B228 | 显示 HEAD | ID_VIEW_SHOWHEAD | 在图中标出 HEAD | `源码资源` |
| B229 | 显示差异路径 | ID_VIEW_SHOWDIFFPATHS | 显示差异路径 | `源码资源` |
| B230 | 显示所有修订 | ID_VIEW_SHOWALLREVISIONS | 显示全部修订（而非仅带引用点） | `源码资源` |
| B231 | 精确复制源 | ID_VIEW_EXACTCOPYSOURCE | 精确显示复制来源 | `源码资源` |
| B232 | 折叠标签 | ID_VIEW_FOLDTAGS | 折叠标签节点 | `源码资源` |
| B233 | 移除已删除项 | ID_VIEW_REMOVEDELETEDONES | 移除已删除的分支/标签 | `源码资源` |
| B234 | 移除未变更分支 | ID_VIEW_REMOVEUNCHANGEDBRANCHES | 隐藏内容未变化的分支 | `源码资源` |
| B235 | 移除标签 | ID_VIEW_REMOVETAGS | 图中不显示标签 | `源码资源` |
| B236 | 显示工作副本修订 | ID_VIEW_SHOWWCREV | 显示工作副本的修订 | `源码资源` |
| B237 | 显示工作副本修改 | ID_VIEW_SHOWWCMODIFICATION | 标出工作副本的本地修改 | `源码资源` |
| B238 | 显示树状条纹 | ID_VIEW_SHOWTREESTRIPES | 交替底色条纹便于横向读图 | `源码资源` |
| B239 | 显示分支与合并 | ID_VIEW_SHOWBRANCHINGSANDMERGES | 显示分支/合并节点 | `源码资源` |
| B240 | 显示所有标签 | ID_VIEW_SHOWALLTAGS | 显示全部标签 | `源码资源` |
| B241 | 箭头指向合并点 | ID_VIEW_ARROW_POINT_TO_MERGES | 箭头指向合并方向（社区推荐开启，否则箭头方向不符合直觉） | `源码资源` |
| B242 | 过滤器对话框 | IDD_REVGRAPHFILTER | 按路径/引用等条件过滤图中节点 | `源码资源` |
| B243 | 当前分支用本地色 | IDC_REVGRAPHUSELOCALFORCUR | 是否为本仓库当前分支使用本地分支颜色 | `官方文档` |
| B244 | 刷新 | Refresh (F5) | 重新从服务器获取信息 | `官方文档` |
| B245 | 图工具栏位图 | IDR_REVGRAPHBAR / IDR_REVGRAPHGLYPHS | 修订图专用工具条位图与节点字形位图 | `源码资源` |

### B.6 Blame（逐行追溯）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B246 | 启动追溯 | TortoiseGit → Blame… | 列出文件中每一行的作者与改动它的修订（别名 annotate） | `官方文档` |
| B247 | 默认查看器 | TortoiseGitBlame | 内置查看器，用高亮区分不同修订便于阅读 | `官方文档` |
| B248 | 悬浮高亮同修订 | Hover highlight | 鼠标悬停在 blame 信息列时，**同一修订的所有行**显示更深的底色 | `官方文档` |
| B249 | 悬浮高亮同作者 | Hover highlight by author | 同一作者改动的其他修订行显示**较浅**底色 | `官方文档` |
| B250 | 左键粘滞高亮 | Click sticky highlight | 左键点击行后高亮保持（不随鼠标移开消失），再次点击取消 | `官方文档` |
| B251 | 悬浮显示日志消息 | Hover shows log message | 悬停 blame 信息列时用提示框显示该修订的提交说明 | `官方文档` |
| B252 | 右键复制日志 | Copy log message | 右键 blame 信息列可复制该修订的日志消息 | `官方文档` |
| B253 | 按龄着色（连续） | View → Colorize by age, continuous | 背景色强度与行龄相关，用颜色渐变：新行偏黄、旧行偏白；端点色可配 | `官方文档` |
| B254 | 按龄着色颜色端点 | Colors (newest/oldest) | 设置项：指定最新与最旧修订的颜色，中间线性插值 | `官方文档` |
| B255 | 忽略空白 | View → Ignore whitespace | 比对父子版本查找行来源时忽略空白（`git blame -w`） | `官方文档` |
| B256 | 检测移动/复制行 | Detect moved or copied lines | 5 级：Disabled / Within file (`-M`) / From modified files (`-C`) / At file creation (`-C -C`) / From existing files (`-C -C -C`) | `官方文档` |
| B257 | 检测字符数-文件内 | Within a file (alnum chars) | 文件内移动检测所需的最小字母数字字符数（`-M<num>`） | `官方文档` |
| B258 | 检测字符数-文件间 | Between files (alnum chars) | 文件间移动/复制检测所需的最小字母数字字符数（`-C<num>`） | `官方文档` |
| B259 | 显示完整日志 | Show complete log | 日志是否包含文件全部改动（即使对注释修订的内容无影响） | `官方文档` |
| B260 | 跟随重命名 | Follow renames | 日志不因文件被重命名而停止 | `官方文档` |
| B261 | 只考虑第一父提交 | Only consider first parents on blame | `git blame --first-parent` | `源码资源` |
| B262 | 显示列-Log ID | Show log ID instead of SHA-1 | 用连续 log 号代替 SHA-1 显示 | `源码资源` |
| B263 | 显示列-作者 | Show author | 显示/隐藏作者列 | `源码资源` |
| B264 | 显示列-日期 | Show date | 显示/隐藏日期列 | `源码资源` |
| B265 | 显示列-文件名 | Show file name | 显示/隐藏文件名列 | `源码资源` |
| B266 | 显示列-原始行号 | Show original line number | 显示/隐藏原始行号列 | `源码资源` |
| B267 | 查找 | Edit → Find… | 搜索修订号、作者、文件内容（**不含日志消息**，日志消息要用日志对话框搜） | `官方文档` |
| B268 | 跳转行 | Edit → Go To Line… | 跳到指定行号 | `官方文档` |
| B269 | 追溯上一修订 | Context Menu → Blame previous revision | 以当前行所属修订的**上一修订为上限**重新生成 blame 报告 | `官方文档` |
| B270 | 显示改动 | Context Menu → Show changes | 启动差异查看器显示该行所属修订改了什么 | `官方文档` |
| B271 | 显示日志 | Context Menu → Show log | 以该行引用的修订为起点打开日志对话框 | `官方文档` |
| B272 | 可用性条件 | Availability condition | `Blame previous revision` 与 `Show changes` **仅当该行不是文件首次提交时引入**才可用 | `官方文档` |
| B273 | 父提交子菜单 | Parent N | 右键子菜单列出各父提交（`IDS_BLAME_POPUP_PARENT`） | `源码资源` |
| B274 | 复制 SHA-1 | Copy SHA-1 to clipboard | 复制该行修订的哈希 | `源码资源` |
| B275 | 编码子菜单 | Encode | 24 个语言分组、约 50 种编码（UTF-8/16LE/16BE、ISO 8859 系列、KOI8、Windows-125x、OEM、Big5、GB2312、Shift-JIS、EUC-KR、TIS-620…） | `源码资源` |
| B276 | 语法高亮 | Enable syntax highlighting | 开关文本语法高亮（`ID_VIEW_ENABLELEXER`） | `源码资源` |
| B277 | 长行折行 | Wrap long lines | 开关折行 | `源码资源` |
| B278 | 深色模式 | Dark Mode | Blame 窗口深色模式开关 | `源码资源` |
| B279 | 字体 | Font | 文件内容与左侧 blame 信息列共用字体与字号 | `官方文档` |
| B280 | Tab 宽度 | Tab size | Tab 展开空格数 | `官方文档` |
| B281 | 折叠面板 | Output window | 底部 `Git Revision List` 输出窗；属性窗 `Commit Info`；右键 `&Copy / &Clear / &Hide` | `源码资源` |
| B282 | 属性面板字段 | Commit Info panes | Basic Info / Subject / Body / Parent(s) / Filename | `源码资源` |
| B283 | 工具栏 | Blame Toolbar | 5 个按钮：Open / Previous / Next / Copy / About | `源码资源` |
| B284 | 设置页 | TortoiseGitBlame Settings | 设置从主上下文菜单进入（不是从 Blame 自身） | `官方文档` |
| B285 | 命令行 | /command:blame | `/path` 指定文件，`/endrev` 指定追溯终止修订，`/line:nnn` 打开即定位到指定行 | `官方文档` |

### B.7 比较修订对话框（Changed Files）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B286 | 变更文件列表 | Compare Revisions Dialog | 列出两修订间所有变化文件，逐个用右键比较 | `官方文档` |
| B287 | 触发方式 | Invocation | 日志中选两个提交 → `Compare revisions`；日志中选一个提交 → `Compare with previous version` / `Compare with working tree`；资源管理器选文件夹 → `Diff with previous version` | `官方文档` |
| B288 | 方向切换 | Swap comparison direction | 顶部按钮切换"从 A 到 B"还是"从 B 到 A" | `官方文档` |
| B289 | 修订范围按钮 | Revision number buttons | 带修订号的按钮可切换比较范围，列表自动更新 | `官方文档` |
| B290 | 搜索框 | Search box | 按子串过滤文件名；语法与日志过滤器类似；输入 `.c` 而非 `*.c` | `官方文档` |
| B291 | 还原到修订1 | Revert to revision xxxxxxx | 把所选文件还原到版本 1（短哈希命名） | `官方文档` |
| B292 | 还原到修订2 | Revert to revision yyyyyyy | 把所选文件还原到版本 2 | `官方文档` |
| B293 | 导出变更树 | Export selection to… | 只导出变更文件但**保留目录结构**，便于发给别人 | `官方文档` |
| B294 | 导出文件清单 | Save list of selected files to… | 把变更文件清单导出为文本 | `官方文档` |
| B295 | 复制选择到剪贴板 | Copy selection to clipboard | 导出文件名**及动作**（modified/added/deleted） | `官方文档` |

### B.8 子模块差异对话框（Submodule Diff Dialog）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B296 | 入口 | Compare with base | 仅能从提交对话框与工作副本状态对话框的子模块项右键进入 | `官方文档` |
| B297 | From 分组框 | 'From' group box | 显示原始修订信息 | `官方文档` |
| B298 | To 分组框 | 'To' group box | 显示变化后修订信息 | `官方文档` |
| B299 | 完整哈希 | Full commit hash | 可高亮并复制到剪贴板 | `官方文档` |
| B300 | 提交主题 | Subject | 提交消息首行，可复制 | `官方文档` |
| B301 | 显示日志按钮 | Show Log | 打开新日志对话框并跳到该修订 | `官方文档` |
| B302 | 变更类型-Fast-forward | Fast-forward | 基于拓扑：快进变更 | `官方文档` |
| B303 | 变更类型-Rewind | Rewind | 基于拓扑：快进的反方向 | `官方文档` |
| B304 | 变更类型-Newer commit time | Newer commit time | 基于时间：既非快进也非回退，比较提交时间 | `官方文档` |
| B305 | 变更类型-Older commit time | Older commit time | 上述的反向 | `官方文档` |
| B306 | 变更类型-Same commit time | Same commit time | 提交时间相同（自动生成提交或两人同时提交） | `官方文档` |
| B307 | 变更类型-New Submodule | New Submodule | 新增子模块 | `官方文档` |
| B308 | 变更类型-Delete Submodule | Delete Submodule | 删除子模块 | `官方文档` |
| B309 | 变更类型-Unknown | Unknown | 哈希未变、错误等 | `官方文档` |
| B310 | 脏工作区着色 | Dirty submodule highlight | 子模块当前工作区为脏时，提交哈希显示为**黄底红字** | `官方文档` |
| B311 | 错误着色 | Error highlight | 修订未 fetch、子模块未初始化等错误时，哈希显示为**红底** | `官方文档` |

### B.9 统一差异查看器（TortoiseGitUDiff）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B312 | 统一差异查看 | Unified-Diff Viewer | 打开 `.diff` / patch 文件时使用的内置查看器 | `官方文档` |
| B313 | 颜色配置 | Colors | TortoiseGitUDiff 设置页可配置默认颜色 | `官方文档` |
| B314 | 字体 | Font | 选择文本显示字体与字号 | `官方文档` |
| B315 | Tab 宽度 | Tabs | diff 中 Tab 展开的空格数 | `官方文档` |
| B316 | 外部替代查看器 | Alternative unified-diff tool | 可替换为 Notepad2 等；文件名自动追加，或用 `%1` 显式占位 | `官方文档` |
| B317 | 标题传递 | %title | 传给外部查看器的标题栏信息 | `官方文档` |
| B318 | 次要工具 | Shift + unified diff | 按住 `Shift` 打开右键启动次要统一差异工具 | `官方文档` |

### B.10 图像差异（TortoiseGitIDiff）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B319 | 图像差异查看器 | TortoiseGitIDiff | 对常见图片格式执行 `Diff` 时自动启动 | `官方文档` |
| B320 | 并排视图 | Side-by-side | 默认视图，两图左右并排 | `官方文档` |
| B321 | 上下视图 | Top-bottom view | 通过 View 菜单或工具栏切换 | `官方文档` |
| B322 | 叠加视图 | Overlay (lightbox) | 两图叠加显示 | `官方文档` |
| B323 | 混合滑块 | Alpha blend slider | 左侧滑块控制两图相对强度（alpha 混合）；点击可直接设值，拖动可交互调整 | `官方文档` |
| B324 | 混合预设切换按钮 | Blend toggle button | 滑块上方的按钮在两个预设混合值之间切换，预设值由滑块两侧标记决定 | `官方文档` |
| B325 | 混合快捷键 | Ctrl+Shift+Wheel | 滚轮配合 `Ctrl+Shift` 调整混合比例 | `官方文档` |
| B326 | XOR 差异模式 | XOR difference | 关闭 alpha 混合后，差异以像素值 XOR 显示：未变区域纯白，变化区域着色 | `官方文档` |
| B327 | 缩放 | Zoom in/out | 放大缩小图像 | `官方文档` |
| B328 | 平移 | Pan | 滚动条、鼠标滚轮、左键拖拽均可平移 | `官方文档` |
| B329 | 图像联动 | Link images together | 勾选后两图的平移控件（滚动条、滚轮）联动 | `官方文档` |
| B330 | 图像信息框 | Image info box | 显示像素尺寸、分辨率、颜色深度；`View → Image Info` 可隐藏 | `官方文档` |
| B331 | 标题栏悬浮信息 | Title bar tooltip | 悬停图像标题栏可得到同样的图像信息 | `官方文档` |
| B332 | 命令行-左右文件 | /left /right | 指定左右图像路径 | `官方文档` |
| B333 | 命令行-标题 | /lefttitle /righttitle | 用标题串替代完整路径 | `官方文档` |
| B334 | 命令行-叠加模式 | /overlay | 启动即进入叠加（alpha blend）模式 | `官方文档` |
| B335 | 命令行-适应 | /fit | 使两图适配窗口 | `官方文档` |
| B336 | 命令行-信息框 | /showinfo | 显示图像信息框 | `官方文档` |

### B.11 仓库浏览器 / 引用浏览器 / RefLog

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B337 | 仓库浏览器 | Repository Browser | 无需工作树（如裸仓库）即可浏览某修订的仓库内容 | `官方文档` |
| B338 | 左树右列表 | Tree + list panes | 左侧目录树，右侧所选目录的内容 | `官方文档` |
| B339 | 路径与修订栏 | Path and revision bar | 顶部显示仓库内路径与要浏览的修订 | `官方文档` |
| B340 | 列排序 | Column sort | 点击右侧列头改变排序 | `官方文档` |
| B341 | 文件右键-打开 | Open | 用文件类型默认查看器或自选程序打开 | `官方文档` |
| B342 | 文件右键-显示日志 | Show the revision log | 显示该文件历史 | `官方文档` |
| B343 | 文件右键-与工作树比较 | Compare with working tree | 该修订的文件 vs 工作树同名文件 | `官方文档` |
| B344 | 文件右键-追溯 | Blame | 查看谁改了哪一行、何时改的 | `官方文档` |
| B345 | 文件右键-另存修订 | Save revision to | 保存旧版本文件到硬盘 | `官方文档` |
| B346 | 文件右键-还原 | Revert this file | 把该文件还原到工作副本原路径 | `官方文档` |
| B347 | 文件右键-复制完整路径 | Copy filename with full path | 复制地址栏所示完整路径 | `官方文档` |
| B348 | 文件夹右键-显示日志 | Show the revision log | 显示该文件夹历史 | `官方文档` |
| B349 | 文件夹右键-复制完整路径 | Copy the full path | 复制完整路径 | `官方文档` |
| B350 | 拖拽导出 | Drag to Explorer | 把一个或多个文件拖到资源管理器窗口 | `官方文档` |
| B351 | 刷新 | F5 | 刷新当前显示的一切 | `官方文档` |
| B352 | 引用浏览器 | Browse References | 查看并操作所有 ref（tag / branch / remote branch / stash 等） | `官方文档` |
| B353 | 左面板-ref 类型树 | Left panel ref type tree | 显示 tags / heads（本地分支）等类型 | `官方文档` |
| B354 | 右面板-ref 列表 | Right panel ref list | 显示所选类型的全部 ref，含最新提交、描述、以及本地分支的远程跟踪分支 | `官方文档` |
| B355 | 显示嵌套引用 | Show nested refs | 关闭则递归展开，开启则只显示非嵌套 | `官方文档` |
| B356 | 过滤编辑栏 | Filter bar | 顶部编辑栏按文本过滤右侧 ref 列表，语法同日志过滤器 | `官方文档` |
| B357 | 双选比较 | Compare two refs | 恰好选两个 ref 时可比较它们 | `官方文档` |
| B358 | 双选日志 | Show log of branch1...branch2 | 查看两分支**都包含**的提交（对称差） | `官方文档` |
| B359 | 双选日志 | Show log of branch1..branch2 | 查看**只在一个分支上**的提交 | `官方文档` |
| B360 | 删除/重命名 ref | Delete / Rename refs | 两面板都有强大右键菜单 | `官方文档` |
| B361 | 配置远程跟踪分支 | Configure remote tracked branch | 对本地分支设置其跟踪的远程分支 | `官方文档` |
| B362 | 删除远程标签 | Delete remote tags… | 在左侧选一个远程后右键，可一次删多个远程标签 | `官方文档` |
| B363 | RefLog 对话框 | RefLog Dialog | 显示某个 ref 的历史（过去指向过哪些提交）；可恢复被删除的提交或 HEAD 位置 | `官方文档` |
| B364 | RefLog 入口在扩展菜单 | Extended menu only | 默认需 `Shift` 右键才能看到 | `官方文档` |
| B365 | 引用比较-隐藏未变更 | Hide unchanged refs in Ref Compare List | 设置项：隐藏未变更的 ref，聚焦有变化的 | `官方文档` |

### B.12 补丁 / 邮件 / Pull Request

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B366 | 创建补丁序列 | Create Patch Serial… | 由已提交的 commit 生成补丁序列（Git 与 SVN 的差异：需先提交再生成） | `官方文档` |
| B367 | 输出目录 | Directory | 补丁输出目录；补丁文件名由 commit 主题生成 | `官方文档` |
| B368 | 起始点 | Since create patch from point | 点 `...` 打开引用浏览器选分支/标签 | `官方文档` |
| B369 | 提交数量 | Number Commits | 限制生成多少个补丁 | `官方文档` |
| B370 | 范围 | Range | 选择从哪个 commit 到哪个 commit，点 `...` 打开日志选提交 | `官方文档` |
| B371 | 创建后发邮件 | Send Mail | 创建补丁后直接进入发邮件对话框 | `官方文档` |
| B372 | 保存未提交差异 | Save unified diff since HEAD | 生成包含**未提交但已暂存**改动的补丁 | `官方文档` |
| B373 | 补丁邮件对话框 | Send Patches Dialog | 输入 To / CC | `官方文档` |
| B374 | 邮件类型 | Patch as attachment | 把补丁作为附件而非内联 | `官方文档` |
| B375 | 邮件类型 | Combine One Mail | 把所有补丁合并成一封邮件（需填 Subject） | `官方文档` |
| B376 | 应用单个补丁 | Review/apply single patch… | 对 `.patch`/`.diff` 右键；层级选错时 TortoiseGit 会提示正确层级 | `官方文档` |
| B377 | 选择仓库对话框 | Choose Repository Dialog | 提示输入工作树位置 | `官方文档` |
| B378 | 应用补丁序列 | Apply Patch Serial… | 把补丁/mbox 放到工作树根，然后逐条应用 | `官方文档` |
| B379 | 添加补丁 | Add | 向待应用列表添加补丁 | `官方文档` |
| B380 | 上移 | Up | 把所选补丁上移 | `官方文档` |
| B381 | 下移 | Down | 把所选补丁下移 | `官方文档` |
| B382 | 移除 | Remove | 移除所选补丁 | `官方文档` |
| B383 | 应用 | Apply | 逐条开始应用补丁 | `官方文档` |
| B384 | 创建 Pull Request | Request pull | 推送后在进度对话框选择；格式化请求包含提交列表与变更文件统计 | `官方文档` |
| B385 | Request Pull 字段 | Start / URL / End | 起始修订 / 公开仓库 URL / 结束分支名或修订 id | `官方文档` |
| B386 | 补丁视图菜单 | View Patch | 查看 HEAD 与索引/工作树之间的补丁 | `源码资源` |

### B.13 合并 / 冲突解决 / 合并跟踪

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B387 | 合并对话框 | Merge Dialog | 必须先从目标分支的工作树发起合并 | `官方文档` |
| B388 | 合并来源-HEAD | HEAD | 当前检出的提交 | `官方文档` |
| B389 | 合并来源-Branch | Branch | 所选分支的最新提交 | `官方文档` |
| B390 | 合并来源-Tag | Tag | 所选标签的提交 | `官方文档` |
| B391 | 合并来源-Commit | Commit | 任意提交，点 `...` 打开日志选，也可直接输入哈希或 `HEAD~4` 等友好名 | `官方文档` |
| B392 | Squash | Squash | 只合并改动内容，**不记录合并信息**；新提交不会把被合并分支作为父提交，日志中不出现合并线 | `官方文档` |
| B393 | No Fast Forward | No Fast Forward | 即使可快进也生成合并提交 | `官方文档` |
| B394 | No Commit | No Commit | 合并后不自动创建提交 | `官方文档` |
| B395 | 合并消息 | Messages | 用被合并提交的单行描述填充日志消息，可指定包含多少条 | `官方文档` |
| B396 | 合并策略 | Merge strategy | 策略下拉 + 策略选项 + 策略参数输入框 | `源码资源` |
| B397 | 合并日志开关 | IDC_CHECK_MERGE_LOG / IDC_EDIT_MERGE_LOGNUM | 是否记录合并日志及条数 | `源码资源` |
| B398 | 中止合并 | Abort Merge | 三种模式：**Merge**（重置索引并尝试重构合并前状态）、**Mixed**（保持工作树、重置索引、报告未更新内容）、**Hard**（重置工作树与索引，**永久丢弃**所有本地改动，不走回收站） | `官方文档` |
| B399 | 冲突编辑 | Edit Conflicts | 生成 `filename.ext.BASE.ext`（共同祖先）/ `.LOCAL.ext`（我的 HEAD，即 mine）/ `.REMOTE.ext`（待合入版本，即 theirs）三个临时文件，并启动配置的冲突编辑器 | `官方文档` |
| B400 | 临时文件按需生成 | On-demand temp files | 与 SVN 不同，Git 不会自动生成这三个文件，只有执行 `Edit Conflicts` 时才由 TortoiseGit 生成 | `官方文档` |
| B401 | 解决冲突对话框 | Resolve Conflicts Dialog | 右键父文件夹 → `Resolve…`，列出该文件夹所有冲突文件，可勾选标记 | `官方文档` |
| B402 | 标记已解决 | Resolved | `git add` 标记为已解决 | `官方文档` |
| B403 | 解决后必须提交 | Must commit after resolve | 与 SVN 不同，Git 解决冲突后必须提交；**若是 rebase 或 cherry-pick 产生的冲突，必须用对应的 rebase/cherry-pick 对话框提交，不能用普通提交对话框** | `官方文档` |
| B404 | 删除-修改冲突对话框 | Resolve delete-modify conflict | 一方删除、另一方修改时，让用户决定保留修改版还是删除文件 | `官方文档` |
| B405 | 子模块冲突对话框 | Resolve submodule conflict | 显示冲突子模块的 base / local / remote 提交以及提交类型 | `官方文档` |
| B406 | 未初始化子模块 | Uninitialized submodule conflict | 未初始化的子模块只显示 SHA-1，且**无法自动解决**——必须先手工 clone 到正确目录 | `官方文档` |
| B407 | 合并跟踪 | Merge Tracking | 合并对话框与日志图都会反映合并关系；`Squash` 会破坏合并跟踪 | `官方文档` |
| B408 | 合并状态标识 | Merge active indicator | 图标 `IDI_MERGEACTIVE` / 控件 `IDC_MERGEACTIVE` 提示当前处于合并中 | `源码资源` |
| B409 | 冲突文件着色 | Conflict coloring | 冲突文件在状态列表中以"可能冲突"（绿）/ "一定冲突"（亮红）着色 | `官方文档` |
| B410 | 阻塞式外部合并工具 | Block TortoiseGit while executing the external merge tool | 同步执行外部合并工具，等待其关闭后询问是否标记已解决 | `官方文档` |
| B411 | 退出码自动标记 | Trust exit code | 外部合并工具返回 0 时自动把冲突文件标记为已解决 | `官方文档` |
| B412 | 临时文件清理 | Temp file cleanup | 用 TortoiseGit / TortoiseGitMerge / TortoiseGitIDiff 标记已解决时，临时文件自动删除；用外部工具则需手工标记（也会删除） | `官方文档` |

### B.14 外部工具集成

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B413 | 外部差异查看器 | External Diff Viewer | 配置任意外部差异程序 | `官方文档` |
| B414 | 差异参数-%base | %base | 不含更改的原始文件 | `官方文档` |
| B415 | 差异参数-%mine | %mine | 含更改的自有文件 | `官方文档` |
| B416 | 差异参数-%bname/%yname | %bname / %yname | 两个文件的窗口标题（如 `filename: revision 123` / `filename: working tree`） | `官方文档` |
| B417 | 差异参数-%bpath/%ypath | %bpath / %ypath | 两个文件的完整路径 | `官方文档` |
| B418 | 差异参数-%brev/%yrev | %brev / %yrev | 两个文件的修订号（如可用） | `官方文档` |
| B419 | 差异参数-%wtroot | %wtroot | 工作树路径 | `官方文档` |
| B420 | 合并参数-%theirs | %theirs | 仓库中的文件 | `官方文档` |
| B421 | 合并参数-%merged | %merged | 冲突文件（合并结果输出） | `官方文档` |
| B422 | 合并参数-%tname/%mname | %tname / %mname | 对应窗口标题 | `官方文档` |
| B423 | 按扩展名绑定 | Advanced Diff/Merge Settings | 为每种文件扩展名定义不同的 diff/merge 程序（如 `.jpg` 用 Photoshop） | `官方文档` |
| B424 | 替代编辑器 | Alternative editor | 用于替代记事本打开非标准 CRLF 的文件（2.18.0 起不再内置 Notepad2e） | `官方文档` |
| B425 | 内置工具与外部工具并存 | Both tools available | 配置了替代工具后，右键 `Diff` 用主工具，`Shift`+`Diff` 用次要工具 | `官方文档` |

### B.15 通用设置（与比对相关）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B426 | 语言 | Language | 选择界面语言（需安装语言包） | `官方文档` |
| B427 | 上下文菜单-主菜单条目 | Context Menu Settings | 指定哪些条目放在一级菜单、哪些放进 TortoiseGit 子菜单 | `官方文档` |
| B428 | 隐藏未版本路径菜单 | Hide menus for unversioned paths | 不为未版本控制文件夹添加菜单项（`Shift` 可恢复） | `官方文档` |
| B429 | 排除路径 | Excluded paths | 列出不显示菜单的路径；尾部 `*` 为通配符，尾部 `\` 匹配该文件夹及全部子项；`*` 不可用于路径中间 | `官方文档` |
| B430 | 拖放上下文菜单开关 | Enable drag context menu | 关闭后右键拖放不显示 TortoiseGit 菜单 | `官方文档` |
| B431 | 上下文菜单 2 | Context Menu 2 Settings | 指定默认隐藏（需 `Shift` 才显示）的条目 | `官方文档` |
| B432 | 默认日志条数限制 | Default limitation of log messages | 见 B156 | `官方文档` |
| B433 | 日志字体 | Font for log messages | 见 B199 | `官方文档` |
| B434 | Gravatar 与 URL | Enable Gravatar | 默认 URL `https://gravatar.com/avatar/%HASH%?d=identicon`，支持 `%HASH%`（邮箱 SHA256） | `官方文档` |
| B435 | 标签画在右侧 | Draw tag/branch labels on right side | 标签/分支徽标显示在提交消息之后 | `官方文档` |
| B436 | Describe 策略 | Describe strategy | Annotated tags / All tags / All refs；默认仅注释标签 | `官方文档` |
| B437 | Describe 缩写长度 | Describe Abbreviated size | 默认 7 | `官方文档` |
| B438 | Describe 强制长格式 | Describe Always show long format | 如 `v0.21.0-0-g28f087c` 而非 `v0.21.0` | `官方文档` |
| B439 | 进度对话框自动关闭 | Git.exe Progress Dialog | 默认手动关闭；可选"无后续选项时自动关闭"或"无错误时自动关闭" | `官方文档` |
| B440 | 还原用回收站 | Use recycle bin when reverting | 关闭后还原将直接丢弃修改，不走回收站 | `官方文档` |
| B441 | 杀进程前确认 | Confirm to kill running git process | 避免误关对话框导致终止运行中的 git 进程 | `官方文档` |
| B442 | 同步对话框随机位置 | Randomize Sync Dialog startup position | 避免多个同步对话框误操作 | `官方文档` |
| B443 | 隐藏未变更 ref | Hide unchanged refs in Ref Compare List | 见 B365 | `官方文档` |
| B444 | 显示执行耗时 | Show git.exe execution timings | 在进度消息末尾追加执行时间与时间戳 | `官方文档` |
| B445 | 标签列表倒序 | Sort tag list in reversed order | 较新版本在前 | `官方文档` |
| B446 | 自动补全开关 | Use auto-completion of file paths and keywords | 见 B60 | `官方文档` |
| B447 | 自动补全超时 | Timeout to stop the auto-completion parsing | 防止大文件导致提交对话框卡死 | `官方文档` |
| B448 | 日志消息历史条数 | Max. items to keep in the log message history | 默认每仓库 25 条 | `官方文档` |
| B449 | 自动勾选提交项 | Select items automatically | 见 B76 | `官方文档` |
| B450 | 日志最小长度 | Limit (`tgit.logminsize`) | 短于该值时提交按钮禁用；未设置也不允许空消息 | `官方文档` |
| B451 | 日志宽度标记 | Border (`tgit.logwidthmarker`) | 在指定最大宽度处放标记并折行；需等宽字体才正确 | `官方文档` |
| B452 | Signed-off-by 警告 | Warn on Signed-Off-By on commit (`tgit.warnnosignedoffby`) | 要求提交消息含 Signed-off-by 行 | `官方文档` |
| B453 | 拼写检查语言 | Language (`tgit.projectlanguage`) | 十进制语言码（英语美国=1033），`-1` 禁用 | `官方文档` |
| B454 | 每仓库任务栏图标 | Overlay Icon (`tgit.icon`) | 支持 `.ico/.png/.jpg/.gif/.bmp`，非 16×16 会自动缩放；需高级选项 `GroupTaskbarIconsPerRepo` 为 3 或 4 | `官方文档` |
| B455 | 颜色-可能/真实冲突 | Possible or real conflict / obstructed | 也用于进度对话框的错误消息 | `官方文档` |
| B456 | 颜色-已添加 | Added files | 添加到仓库的项 | `官方文档` |
| B457 | 颜色-缺失/删除/替换 | Missing / deleted / replaced | 删除/缺失/被同名文件替换的项 | `官方文档` |
| B458 | 颜色-已合并 | Merged | 成功合并且无冲突的更改 | `官方文档` |
| B459 | 颜色-已修改/已复制 | Modified / copied | 带历史添加或仓库内复制的路径；日志对话框中含复制项的条目也用此色 | `官方文档` |
| B460 | 颜色-Note 节点 | Note node | 指向 `refs/notes` 命名空间的 git notes 引用 | `官方文档` |
| B461 | 颜色-当前分支用本地色 | Use local branch color for current branch | 修订图中当前分支是否用本地分支颜色 | `官方文档` |
| B462 | 深色主题 | Dark theme | 对话框深色模式，需 Win10 1809+ 与系统深色模式；并非所有控件都支持 | `官方文档` |
| B463 | 颜色设置 2 | Color Settings 2 | 配置对话框中的文本颜色 | `官方文档` |
| B464 | 颜色设置 3 | Color Settings 3 | 配置**日志对话框图形列的线条颜色、线条宽度、节点大小** | `官方文档` |
| B465 | 设置导出/导入 | Exporting TortoiseGit Settings | 导出/导入设置项与已保存会话 | `官方文档` |
| B466 | 高级设置 | Advanced Settings | 直接编辑注册表级设置 | `官方文档` |
| B467 | Diff/Merge 页 | TortoiseGit Merge / Diff 页 | 在设置中切换内置/外部差异与合并工具 | `官方文档` |

### B.16 命令行（TortoiseGitProc.exe）

| ID | 中文名 | 英文原名 | 行为描述 | 来源 |
|---|---|---|---|---|
| B468 | 命令-diff | /command:diff | 启动配置的外部差异程序。`/path` 为第一个文件；有 `/path2` 则两文件互比，无则与 BASE 比；`/startrev:xxx`（BASE）与 `/endrev:xxx` 显式指定修订；`/unified` 输出统一差异；`/line:NN` 滚动到指定行 | `官方文档` |
| B469 | 命令-showcompare | /command:showcompare | 依修订与路径，展示统一差异 / 变更文件列表对话框 / 直接启动文件差异查看器；`/revision1:xxx` 为比较基准，`/revision2:xxx` 为另一侧 | `官方文档` |
| B470 | 命令-conflicteditor | /command:conflicteditor | 用配置的冲突编辑器打开 `/path` 指定的冲突文件（自动带好三个版本文件） | `官方文档` |
| B471 | 命令-log | /command:log | 打开日志对话框；`/path` 指定文件或文件夹 | `官方文档` |
| B472 | 日志-定位修订 | /rev:"SHA1" | 高亮并自动滚动到指定修订 | `官方文档` |
| B473 | 日志-结束修订 | /endrev:"SHA1/branch" | 显示到该修订为止的日志 | `官方文档` |
| B474 | 日志-起始修订 | /startrev:"SHA1/branch" | 仅与 `/endrev` 组合使用，显示 `startrev..endrev` 范围 | `官方文档` |
| B475 | 日志-范围 | /range:"gitrevision" | 按输入的 git 修订表达式显示（如 `branch1...branch2`） | `官方文档` |
| B476 | 日志-条数限制 | /limit:"N SCALE" | SCALE 取 Commit/Year/Month/Week；`/limit:0` 禁用默认限制 | `官方文档` |
| B477 | 日志-过滤串 | /findstring:"filterstring" | 预填过滤器文本框 | `官方文档` |
| B478 | 日志-强制文本过滤 | /findtext | 强制用文本而非正则 | `官方文档` |
| B479 | 日志-强制正则过滤 | /findregex | 强制用正则 | `官方文档` |
| B480 | 日志-过滤类型位掩码 | /findtype:X | 0=all, 1=messages, 2=path, 4=authors, 8=revisions, 16=保留, 32=bug ID, 64=subject（可相加） | `官方文档` |
| B481 | 日志-输出到文件 | /outfile:path | 关闭日志对话框时把所选修订写入该文件 | `官方文档` |
| B482 | 命令-blame | /command:blame | 打开 TortoiseGitBlame；`/endrev` 指定追溯终止修订；`/line:nnn` 打开即定位到该行 | `官方文档` |
| B483 | 命令-revisiongraph | /command:revisiongraph | 打开修订图；`/output:path` 可无窗口直接导出图 | `官方文档` |
| B484 | 命令-repobrowser | /command:repobrowser | 打开仓库浏览器；`/rev:xxx` 指定显示修订，缺省 HEAD | `官方文档` |
| B485 | 命令-repostatus | /command:repostatus | 打开检查修改对话框 | `官方文档` |
| B486 | 命令-merge | /command:merge | 打开合并对话框；`/abort` 打开中止合并对话框 | `官方文档` |
| B487 | 命令-resolve | /command:resolve | 把 `/path` 的冲突文件标记为已解决；`/noquestion` 跳过确认 | `官方文档` |
| B488 | 命令-commit | /command:commit | 打开提交对话框；`/logmsg` 预填消息；`/logmsgfile:path` 从文件读；`/bugid:"…"` 预填 Bug ID | `官方文档` |
| B489 | 命令-refbrowse | /command:refbrowse | 打开引用浏览器 | `官方文档` |
| B490 | 命令-reflog | /command:reflog | 打开 RefLog 对话框 | `官方文档` |
| B491 | 命令-cat | /command:cat | 把 `/path`（URL 或工作树路径）在 `/revision:xxx` 处的内容存到 `/savepath:path` | `官方文档` |
| B492 | 命令-log 对应的变更文件 | /command:showcompare | 见 B469 | `官方文档` |
| B493 | 命令-自动补全测试 | /command:autotexttest | 打开补全正则测试对话框 | `官方文档` |
| B494 | 命令-settings | /command:settings | 打开设置对话框 | `官方文档` |
| B495 | 命令-bisect | /command:bisect | `/start` 开始二分（可带 `/good:REF` `/bad:REF`）；进行中可用 `/good` `/bad` `/skip` `/reset` | `官方文档` |
| B496 | 命令-cherrypick | /command:cherrypick | 打开拣选对话框 | `推断` |
| B497 | 命令-rebase | /command:rebase | 打开变基对话框；`/upstream` 选上游，`/force` 强制 | `官方文档` |
| B498 | 命令-patch 相关 | /command:repostatus 等 | 补丁应用走资源管理器右键与 `Apply Patch Serial` | `官方文档` |
| B499 | 关闭行为 | /closeonend:0/1/2 | 0=手动关闭；1=无后续选项时自动关闭；2=无错误时自动关闭 | `官方文档` |
| B500 | 多路径参数 | /path 用 `*` 分隔 | 一个 `/path` 可传多个路径（如提交若干具体文件） | `官方文档` |
| B501 | 参数语法变体 | /option argument 与 -option argument | 除 `/option:argument` 外，还支持 PowerShell 的 `/option argument` 与 MSYS 的 `-option argument` | `官方文档` |
| B502 | 临时文件传参已废弃 | /notempfile obsolete | 1.5.0 起不需要，传多参数自动用临时文件 | `官方文档` |
| B503 | 版本检查 | /command:updatecheck | `/visible` 即使无新版本也显示；`/force` 强制显示下载列表 | `官方文档` |
| B504 | 帮助/关于 | /command:help, /command:about | 打开帮助 / 关于对话框（无命令时显示关于） | `官方文档` |
| B505 | 首次启动向导 | /command:firststart | 显示首次启动向导 | `官方文档` |

---

## C. Ribbon 布局分析与 LqRibbon 落地建议

> **本节单独成节，可直接落地成 `RibbonPage` / `RibbonGroup` / `Button`。**

### C.1 TortoiseGit 的 Ribbon 现状（先厘清事实）

| 问题 | 结论 | 证据 |
|---|---|---|
| TortoiseGitMerge 是 Ribbon 吗？ | **是**。默认启用 Office 2007 风格 Ribbon，可在 Settings 里 `Use Ribbons` 关闭降级为经典菜单+工具栏 | `官方文档`：*"Use ribbons: when set, ribbons interface is used (looks like Office 2007). When unset, traditional toolbar interface is used. Default is set."* |
| TortoiseGitMerge 的 Ribbon 有几个页面？ | **只有 1 个 Tab**，命名为 `Edit`；另外有左上角 Application Menu（承载 File 菜单）与 Quick Access Toolbar | `源码资源`：`TortoiseGitMergeRibbon.xml` 中只有一个 `<Tab Id="cmdTabEdit">` |
| TortoiseGitMerge 的 Ribbon 用了什么技术？ | **Windows 原生 Ribbon Framework**（`UIRibbonFramework` + `IUIFramework::LoadUI`），而不是 MFC 的 `CMFCRibbonBar`；UI 定义在外部 XML 资源 `TORTOISEGITMERGERIBBON_RIBBON` | `源码逻辑`：`MainFrm.cpp::InitRibbon()` 中 `CoCreateInstance(__uuidof(UIRibbonFramework))` + `LoadUI(..., L"TORTOISEGITMERGERIBBON_RIBBON")` |
| TortoiseGit 客户端的 Log / Commit / Revision Graph 等对话框是 Ribbon 吗？ | **否**。全是经典 Win32 对话框 + 菜单栏 + 工具栏。官方从未给这些对话框做 Ribbon | `源码资源`：`TortoiseProcENG.rc` 中只有 `MENU` / `TOOLBAR` 资源，无 Ribbon XML |
| TortoiseGitBlame / TortoiseGitIDiff / TortoiseGitUDiff 是 Ribbon 吗？ | **否**。MFC 经典菜单 + 16×16 工具栏 | `源码资源`：`TortoiseGitBlameENG.rc` 中 `IDR_TORTOISE_GIT_BLAME_MAINFRAME MENU` + `TOOLBAR` |

**社区历史**：TortoiseGit 曾开过 issue #1619 *"TortoiseGitMerge should not use ribbon UI"*（反对 Ribbon），最终以"Ribbon 可关闭、保留经典工具栏"收场——这说明**单 Tab 的 Ribbon 在小工具上确有体验代价**，LqCompare 必须用**多页面**方案规避。

### C.2 从 TortoiseGitMerge 的 Ribbon 吸取的经验教训

| 观察 | 教训 |
|---|---|
| 只有 1 个 Tab `Edit`，却塞进 6 个 Group、30+ 控件，靠 `ScalingPolicy` 依次收缩 | 单页面承载不了比对工具的功能量；LqCompare 必须**分页**，让每页在 `Large` 尺寸下都不收缩 |
| File 菜单整体被挪进 Application Menu，导致 `Open/Save/Save As` 藏在圆形按钮后 | 高质量高频命令（打开/保存/另存）应放在 **Home 页第一组**，Application Menu 只放低频项（About/Exit/Import/Export） |
| 所有 Tooltip 都缺失（XML 里没有 `Command.TooltipTitle/Description`） | LqRibbon 的每个按钮**必须**有 Title + Description 两段式 tooltip |
| `cmdGroupBlocks` 在 Tab 的 ScalingPolicy 中未被列入 IdealSizes | 分组必须完整纳入收缩策略，否则窄窗口下会错乱 |
| 复选框被塞进 `DropDownButton`（`cmdViewBars`）与 `SplitButtonGallery`（Regex Filter） | `RibbonGroup` 需要原生支持三种复合控件：**SplitButton / DropDownButton(含 CheckBox 列表) / Gallery** |
| 两种界面（Ribbon / 经典工具栏）并存，靠注册表 `UseRibbons` 切换 | LqCompare 应**只做 Ribbon + 可折叠式 QAT**，不做两套 UI，减少维护面 |
| 状态栏里放了编码/EOL/Tab 三个下拉（Ribbon 状态栏 `CMFCRibbonButtonsGroup`） | 编码/行尾/Tab 属于"每窗格高频切换"项，放**状态栏**比放 Ribbon 更合理，LqRibbon 需要支持"Ribbon 状态栏宿主" |

### C.3 【可直接落地】建议的 Ribbon 页面 → 组 → 按钮

> 约定：
> - **L**=Large 大按钮（图标+文字，32×32），**M**=Medium 中按钮（图标在上文字在下），**S**=Small 小按钮（仅 16×16 图标，带 tooltip）
> - **T**=Toggle 切换按钮，**SP**=SplitButton（主按钮+下拉），**DD**=DropDownButton，**CB**=CheckBox，**CMB**=ComboBox/Gallery
> - 「对齐 TortoiseGit」列标注该按钮在 TortoiseGit 里的对应名称，便于溯源

**页面统计：10 个 RibbonPage / 44 个 RibbonGroup / 195 个按钮（C001–C195）。**

#### Page 1 — `Home` 开始（默认页）

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C001 | Group 1.1 **File 文件** | 打开 Open / **L** / Button | `cmdOpen`、`ID_FILE_OPEN` |
| C002 | | 保存 Save / **L** / Button | `cmdSave`、`ID_FILE_SAVE` |
| C003 | | 另存为 Save As / **S** / Button | `cmdSaveAs`、`ID_FILE_SAVE_AS` |
| C004 | | 重新加载 Reload / **S** / Button | `cmdReload`、`ID_FILE_RELOAD` |
| C005 | | 允许编辑 Enable Edit / **M** / **T** | `cmdEditEnabled`、`ID_EDIT_ENABLE` |
| C006 | Group 1.2 **Session 会话** | 新建会话 New Session / **L** / **SP**（下拉：文件比对 / 文件夹比对 / 三方合并 / 剪贴板比对） | 无（BC 的 Home 视图） |
| C007 | | 打开会话 Open Session / **M** / Button | BC `Open Session` |
| C008 | | 保存会话 Save Session / **M** / Button | BC `Save Session As` |
| C009 | | 最近会话 Recent Sessions / **S** / **DD** | BC 自动保存会话 |
| C010 | Group 1.3 **Navigate 导航** | 上一差异 Previous Difference / **L** / Button | `cmdNavigatePreviousDifference` |
| C011 | | 下一差异 Next Difference / **L** / Button | `cmdNavigateNextDifference` |
| C012 | | 上一冲突 Previous Conflict / **M** / Button | `cmdNavigatePreviousConflict` |
| C013 | | 下一冲突 Next Conflict / **M** / Button | `cmdNavigateNextConflict` |
| C014 | | 跳转行 Goto Line / **S** / Button | `cmdGotoLine` |
| C015 | Group 1.4 **Clipboard 剪贴板** | 剪切 Cut / **S** / Button | `IDS_EDIT_CUT` |
| C016 | | 复制 Copy / **S** / Button | `cmdCopy`、`ID_EDIT_COPY` |
| C017 | | 粘贴 Paste / **S** / Button | `cmdPaste`、`ID_EDIT_PASTE` |
| C018 | Group 1.5 **Source Control 版本控制** | 与 HEAD 比对 Diff vs HEAD / **L** / Button | `TortoiseGit → Diff` |
| C019 | | 比较两修订 Compare Two Revisions / **L** / Button | `Compare revisions` |
| C020 | | 比较两分支 Compare Two Branches / **S** / Button | 引用浏览器双选比较 |
| C021 | | 显示日志 Show Log / **S** / Button | `Show log…` |
| C022 | | 追溯 Blame / **S** / Button | `Blame…` |
| C023 | | 修订图 Revision Graph / **S** / Button | `Revision Graph…` |

#### Page 2 — `Compare` 比较

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C024 | Group 2.1 **Sources 数据源** | 左侧路径 Left Path / **L** / **CMB**（可编辑下拉 + 浏览） | `/base` |
| C025 | | 右侧路径 Right Path / **L** / **CMB** | `/mine` |
| C026 | | 交换两侧 Swap Sides / **M** / Button | `cmdViewSwitchLeft`、`/reversedpatch` |
| C027 | | 重新加载两侧 Reload Both / **S** / Button | `cmdReload` |
| C028 | Group 2.2 **Rules 比较规则** | 比较规则 Settings for Comparison / **L** / Button | `Compare whitespaces` 等集合 |
| C029 | | 忽略空白变化 Ignore Whitespace Changes / **M** / **T** | `cmdIgnoreWhitespaceChanges` |
| C030 | | 忽略全部空白 Ignore All Whitespace Changes / **M** / **T** | `cmdIgnoreAllWhitespaceChanges` |
| C031 | | 比较空白 Compare Whitespaces / **S** / **T** | `cmdCompareWhitespaces` |
| C032 | | 忽略行尾 Ignore Line Endings / **S** / **T** | `cmdViewIgnoreEOL` |
| C033 | | 忽略大小写 Ignore Case Changes / **S** / **T** | `Ignore case changes` |
| C034 | | 忽略注释 Ignore Comments / **S** / **T** | `cmdViewIgnoreComments` |
| C035 | Group 2.3 **Alignment 对齐** | 对齐相似行 Align Similar Lines / **M** / **T** | BC `对齐相似行` |
| C036 | | 对齐样式 Alignment Style / **S** / **DD** | BC `多种对齐样式` |
| C037 | | 手动对齐 Manual Align / **S** / **T** | BC `手动对齐更改或断开对齐` |
| C038 | | 断开对齐 Disconnect Alignment / **S** / Button | BC 同上 |
| C039 | Group 2.4 **Sync Scroll 同步滚动** | 同步水平滚动 Sync Horizontal Scroll / **S** / **T** | BC 同步链接 |
| C040 | | 同步垂直滚动 Sync Vertical Scroll / **S** / **T** | BC 同步链接 |
| C041 | | 链接图像/窗格 Link Panes / **S** / **T** | TortoiseGitIDiff `Link images together` |
| C042 | | 隔离选择 Isolate Selection / **S** / **T** | BC `隔离` |
| C043 | Group 2.5 **Folder Options 文件夹选项**（仅文件夹会话可见） | 比较子文件夹 Compare Subfolders / **M** / **T**（含下拉：不比较/仅比较子层/递归） | BC `Folder handling` |
| C044 | | 自动后台扫描 Automatically Scan Subfolders in Background / **S** / **T** | BC `Automatically scan subfolders in background` |
| C045 | | 加载时展开子文件夹 Expand Subfolders when Loading / **S** / **DD**（不展开/仅展开有差异） | BC `Expand subfolders when loading session` |
| C046 | | 比较时间戳 Compare Timestamps / **S** / **T** | BC `比较时间戳` |
| C047 | | 比较大小 Compare Size / **S** / **T** | BC `比较文件大小` |
| C048 | | 比较内容 Compare Contents / **S** / **DD**（不比较/CRC/二进制/基于规则） | BC `比较内容` |
| C049 | | 压缩包处理 Archive Handling / **S** / **DD**（作为文件/打开时作为文件夹/始终作为文件夹） | BC `Archive handling` |
| C050 | | 比较文件名大小写 Compare Filename Case / **S** / **T** | BC `Compare filename case` |
| C051 | | 跟随符号链接 Follow Symbolic Links / **S** / **T** | BC `Follow symbolic links` |

#### Page 3 — `Merge` 合并

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C052 | Group 3.1 **Merge Output 合并输出** | 显示合并栏 Show Merged Pane / **L** / **T** | 三栏视图 `Merged` |
| C053 | | 自动合并 Auto Merge / **L** / Button | BC `自动合并更改` |
| C054 | | 保存合并结果 Save Merged / **L** / Button | `cmdSave` + `/merged` |
| C055 | | 另存为 Save As / **S** / Button | `cmdSaveAs` |
| C056 | | 重新比较输出 Recompare Output / **S** / Button | 推断（BC 有 `重新比较`） |
| C057 | Group 3.2 **Block Actions 块操作** | 使用左侧 Use Left Block / **L** / Button | `cmdUseLeftBlock` |
| C058 | | 使用右侧 Use Right Block / **L** / Button | `cmdUseMineBlock` |
| C059 | | 先左后右 Use Left Then Right / **M** / Button | `cmdUseTheirsThenMineBlock` |
| C060 | | 先右后左 Use Right Then Left / **M** / Button | `cmdUseMineThenTheirsBlock` |
| C061 | | 使用两侧 Use Both Blocks / **S** / **SP**（此块优先 / 此块最后） | `Use both text blocks (this one first/last)` |
| C062 | | 使用整个文件 Use Whole File / **S** / **SP**（用左文件 / 用右文件） | `cmdUseLeftFile`、`Use whole other file` |
| C063 | Group 3.3 **Conflict 冲突** | 标记已解决 Mark as Resolved / **L** / Button | `cmdMarkResolved` |
| C064 | | 上一冲突 Previous Conflict / **M** / Button | `cmdNavigatePreviousConflict` |
| C065 | | 下一冲突 Next Conflict / **M** / Button | `cmdNavigateNextConflict` |
| C066 | | 解决全部 Resolve All / **S** / Button | 推断（`Resolve…` 对话框） |
| C067 | | 冲突计数 Conflict Count / **S** / Label（只读显示 `Conflicts: N`） | `IDS_STATUSBAR_CONFLICTS` |
| C068 | Group 3.4 **Three-Way 三方** | 基线栏 Base Pane / **M** / **T** | `/base` |
| C069 | | 中间栏 Center Pane / **S** / **DD** | 推断 |
| C070 | | 只显示冲突 Show Conflicts Only / **S** / **T** | 推断 |
| C071 | | 独立显示差异 Show Diff Separately / **S** / **SP**（相对 Base 的左差异 / 右差异） | `IDS_HEADER_DIFFLEFTTOBASE` |
| C072 | Group 3.5 **Apply Change 应用变更** | 复制到左侧 Copy to Left / **L** / Button | 推断（BC 自适应边栏） |
| C073 | | 复制到右侧 Copy to Right / **L** / Button | 推断 |
| C074 | | 复制到合并栏 Copy to Merged / **M** / Button | 推断 |
| C075 | | 删除块 Delete Block / **S** / Button | 推断 |

#### Page 4 — `Edit` 编辑

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C076 | Group 4.1 **Undo 撤销** | 撤销 Undo / **L** / Button | `cmdUndo`、`ID_EDIT_UNDO` |
| C077 | | 重做 Redo / **M** / Button | `cmdRedo`、`ID_EDIT_REDO` |
| C078 | | 还原文件 Revert File / **S** / Button | `ID_FILE_RELOAD` |
| C079 | Group 4.2 **Find 查找** | 查找 Find / **L** / Button | `cmdFind`、`ID_EDIT_FIND` |
| C080 | | 查找下一个 Find Next / **S** / Button | `cmdFindNext` |
| C081 | | 查找上一个 Find Previous / **S** / Button | `cmdFindPrev` |
| C082 | | 替换 Replace / **S** / Button | `ID_EDIT_REPLACE` |
| C083 | | 在文件中查找 Find in Files / **S** / Button | 推断 |
| C084 | Group 4.3 **Mark 标记** | 标记块 Mark Block / **M** / **T** | `IDS_VIEWCONTEXTMENU_MARKBLOCK` |
| C085 | | 取消标记 Unmark Block / **S** / Button | `IDS_VIEWCONTEXTMENU_UNMARKBLOCK` |
| C086 | | 仅保留已标记 Leave Only Marked / **S** / Button | `IDS_VIEWCONTEXTMENU_LEAVEONLYMARKEDBLOCKS` |
| C087 | | 清除全部标记 Clear Marks / **S** / Button | 推断 |
| C088 | Group 4.4 **Text Tools 文本工具** | 制表符转空格 Convert Tabs to Spaces / **S** / Button | `IDS_EDIT_TAB2SPACE` |
| C089 | | 空格转制表符 Convert Spaces to Tabs / **S** / Button | `IDS_EDIT_SPACE2TAB` |
| C090 | | 去除行尾空白 Trim Trailing Whitespace / **S** / Button | `IDS_EDIT_TRIM` |
| C091 | | 行排序 Sort Lines / **S** / Button | 推断 |
| C092 | | 大小写转换 Change Case / **S** / **DD**（全大写/全小写/首字母大写） | 推断 |
| C093 | Group 4.5 **Line Endings 行尾** | 行尾样式 End of Line Style / **M** / **DD**（CRLF/LF/CR/Mixed） | `IDS_VIEWCONTEXTMENU_EOL` |
| C094 | | 文件编码 File Encoding / **M** / **DD**（ASCII/UTF-8/UTF-16LE/BE/UTF-32LE/BE ±BOM） | `IDS_VIEWCONTEXTMENU_ENCODING` |
| C095 | | Tab 模式 Tab Mode / **S** / **DD**（Tab/空格/智能） | `Tabs` / `Smart tab char` |

#### Page 5 — `View` 视图

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C096 | Group 5.1 **Panes 窗格** | 单栏视图 One Pane / **L** / **T** | `cmdViewOneWayDiff`、`ID_VIEW_ONEWAYDIFF` |
| C097 | | 双栏视图 Two Pane / **L** / **T** | 同上 |
| C098 | | 三栏视图 Three Pane / **L** / **T** | 三栏视图 |
| C099 | | 左右交换 Switch Left and Right / **M** / Button | `cmdViewSwitchLeft` |
| C100 | | 并排 / 上下 Side-by-Side / Top-Bottom / **S** / **DD** | TortoiseGitIDiff `View menu` |
| C101 | Group 5.2 **Bars 界面栏** | 行差异栏 Line Diff Bar / **S** / **T** | `cmdViewLineDiffBar` |
| C102 | | 定位器栏 Locator Bar / **S** / **T** | `cmdViewLocatorBar` |
| C103 | | 状态栏 Status Bar / **S** / **T** | `cmdViewStatusBar` |
| C104 | | 概览图 Overview / **S** / **T** | `ID_VIEW_SHOWOVERVIEW` |
| C105 | | 图像信息框 Image Info / **S** / **T** |  TortoiseGitIDiff `View → Image Info` |
| C106 | Group 5.3 **Display 显示** | 显示空白符 Show Whitespaces / **M** / **T** | `cmdShowWhitespaces` |
| C107 | | 长行折行 Wrap Long Lines / **M** / **T** | `cmdViewWrapLines` |
| C108 | | 显示行号 Show Line Numbers / **S** / **T** | `Show linenumbers` |
| C109 | | 行内差异 Inline Diff / **S** / **T** | `cmdViewInlineDiff` |
| C110 | | 按词行内差异 Inline Diff Word-wise / **S** / **T** | `cmdViewInlineDiffWord` |
| C111 | | 移动块 Moved Blocks / **S** / **T** | `ID_VIEW_MOVEDBLOCKS` |
| C112 | | 语法高亮 Syntax Highlighting / **S** / **DD** | TortoiseGitBlame `Enable syntax highlighting` |
| C113 | Group 5.4 **Collapse 折叠** | 折叠未变更 Collapse Unchanged Sections / **L** / **T** | `cmdViewCollapsed` |
| C114 | | 折叠层级 Collapse Level / **S** / **DD** | 推断 |
| C115 | | 全部展开 Expand All / **S** / Button | 推断 |
| C116 | | 全部折叠 Collapse All / **S** / Button | 推断 |
| C117 | Group 5.5 **Theme 主题** | 主题 Theme / **M** / **DD**（浅色/深色/跟随系统） | `Use dark mode` |
| C118 | | 字体 Font / **S** / Button | `Font` |
| C119 | | 缩放 Zoom / **S** / **DD**（放大/缩小/100%/适应宽度/适应高度/显示全部） | 修订图 `ZOOMIN` 系列 |
| C120 | | 全屏 Full Screen / **S** / **T** | 推断 |

#### Page 6 — `Filter` 过滤

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C121 | Group 6.1 **Display Filter 显示过滤** | 显示全部 Show All / **L** / **T** | BC `Show All` |
| C122 | | 只显示差异 Show Differences / **L** / **T** | BC `Show Differences` |
| C123 | | 只显示相同 Show Matches / **M** / **T** | BC `Show Matches` |
| C124 | | 显示孤立项 Show Orphans / **S** / **SP**（仅左 / 仅右 / 两侧） | BC `Show Orphans` |
| C125 | | 显示较新项 Show Newer / **S** / **SP**（左侧较新 / 右侧较新） | BC `Show Left Newer` |
| C126 | Group 6.2 **More Filters 更多过滤** | 临时停用过滤 Suppress Filters / **M** / **T** | BC `Suppress Filters` |
| C127 | | 忽略目录结构 Ignore Folder Structure / **S** / **T** | BC `Ignore Folder Structure` |
| C128 | | 只比较文件 Only Compare Files / **S** / **T** | BC `Only Compare Files` |
| C129 | | 始终显示文件夹 Always Show Folders / **S** / **T** | BC `Always Show Folders` |
| C130 | | 隐藏无关路径 Hide Unrelated Paths / **S** / **T** | 日志 `Hide unrelated changed paths` |
| C131 | Group 6.3 **File Filter 文件过滤** | 文件过滤器 Filters / **L** / **SP**（下拉 = 预设过滤器列表） | BC `Filters toolbar` |
| C132 | | 编辑过滤器 Edit Filters / **S** / Button | BC `Session Settings → Name Filters` |
| C133 | | 清除过滤器 Clear Filter / **S** / Button | 推断 |
| C134 | | 按属性过滤 Filter by Attributes / **S** / Button | BC `Other Filters` |
| C135 | Group 6.4 **Line Filter 行过滤** | 正则过滤器 Regex Filter / **L** / **SP**（下拉 = 已配置过滤器 Gallery） | `cmdRegexFilterConfig1` |
| C136 | | 配置正则 Configure Filter Regexes / **S** / Button | `cmdRegexFilterConfig2` |
| C137 | | 无过滤器 No Filter / **S** / **T** | `cmdRegexNoFilter` |
| C138 | | 忽略注释 Ignore Comments / **S** / **T** | `cmdViewIgnoreComments` |
| C139 | Group 6.5 **Log Filter 日志过滤**（版本控制会话可见） | 过滤字段 Filter Fields / **M** / **DD**（勾选 Subject/Messages/Paths/Authors/Emails/Revisions/Refname/Notes/Bug ID） | `LOGFILTER_*` 菜单 |
| C140 | | 正则模式 Use Regex / **S** / **T** | `LOGFILTER_REGEX` |
| C141 | | 大小写敏感 Case Sensitive / **S** / **T** | `LOGFILTER_CASE` |
| C142 | | 分支过滤 Branch Filter / **M** / **CMB**（`A B` / `A...B` / `A..B`） | 日志分支过滤器 |
| C143 | | 跟随重命名 Follow Renames / **S** / **T** | `Walk Behavior → Follow renames` |

#### Page 7 — `Session` 会话

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C144 | Group 7.1 **Session 会话** | 新建 New Session / **L** / **SP** | BC `New Session` |
| C145 | | 打开 Open Session / **L** / Button | BC `Open Session` |
| C146 | | 保存 Save Session / **M** / Button | BC `Save Session As` |
| C147 | | 另存为 Save Session As / **S** / Button | BC 同上 |
| C148 | | 锁定会话 Lock Session / **S** / **T** | BC `Lock` |
| C149 | Group 7.2 **Workspace 工作区** | 载入工作区 Load Workspace / **M** / **DD** | BC `Load Workspace` |
| C150 | | 管理工作区 Manage Workspaces / **S** / Button | BC `Manage Workspaces` |
| C151 | | 保存工作区 Save Workspace As / **S** / Button | BC `Save Workspace As` |
| C152 | Group 7.3 **Tabs 标签页** | 新建标签页 New Tab / **S** / Button | BC `New Tab` |
| C153 | | 新建窗口 New Window / **S** / Button | BC `New Window` |
| C154 | | 关闭标签页 Close Tab / **S** / Button | BC `Close Tab` |
| C155 | Group 7.4 **Recent 最近** | 最近会话 Recent / **L** / **DD** | BC 自动保存会话 |

#### Page 8 — `Report` 报告

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C156 | Group 8.1 **Export 导出** | 保存报告 Save Report / **L** / **SP**（HTML / 文本 / CSV / XML） | BC `打印和 HTML 比较报告` |
| C157 | | 打印 Print / **M** / Button | BC 打印报告 |
| C158 | | 打印预览 Print Preview / **S** / Button | 推断 |
| C159 | | 导出变更树 Export Change Tree / **S** / Button（保留目录结构） | `Export selection to…` |
| C160 | Group 8.2 **Copy 复制** | 复制全部 Copy All / **M** / Button | `Copy to clipboard` |
| C161 | | 只复制差异 Copy Differences Only / **M** / Button | 推断 |
| C162 | | 复制文件清单 Copy File List / **S** / Button（含 modified/added/deleted 动作） | `Copy selection to clipboard` |
| C163 | | 清单存为文件 Save List to File / **S** / Button | `Save list of selected files to…` |
| C164 | | 复制路径 Copy Paths / **S** / Button | `Copy paths to clipboard` |
| C165 | Group 8.3 **Patch 补丁** | 创建补丁 Create Patch / **L** / Button | `cmdCreateUnifiedDiff`、`Create Patch Serial` |
| C166 | | 应用补丁 Apply Patch / **L** / Button | `Review/apply single patch…` |
| C167 | | 查看补丁 View Patch / **S** / Button | `Save unified diff since HEAD` |
| C168 | | 反向补丁 Reverse Patch / **S** / **T** | `/reversedpatch` |
| C169 | Group 8.4 **Statistics 统计** | 统计信息 Statistics / **L** / Button | `Statistics` 按钮 |
| C170 | | 按作者提交 Commits by Author / **S** / Button（柱状/堆叠/饼图） | `Commits by Author` |
| C171 | | 按日期提交 Commits by Date / **S** / Button | `Commits by date` |
| C172 | | 行数统计 Line Count / **S** / Button | 推断 |

#### Page 9 — `Tools` 工具

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C173 | Group 9.1 **External Tools 外部工具** | 启动外部比对 Launch External Diff / **L** / Button | `%base %mine` 配置 |
| C174 | | 选择比对工具 Choose Diff Tool / **M** / **DD**（内置 / 次要工具 / 各外部工具） | `Shift + Diff` 次要工具 |
| C175 | | 配置外部工具 Configure External Tools / **S** / Button | `Diff Viewer` / `Merge Tool` 设置页 |
| C176 | | 按扩展名绑定 Extension Associations / **S** / Button | `Advanced Diff/Merge Settings` |
| C177 | Group 9.2 **Options 选项** | 设置 Settings / **L** / Button | `cmdSettings`、`ID_VIEW_OPTIONS` |
| C178 | | 颜色与字体 Colors & Fonts / **M** / Button | `Colors` 设置页 |
| C179 | | 文件格式 File Formats / **S** / Button | BC `File Formats` |
| C180 | | 配置 Profile Profiles / **S** / Button | BC `Profiles` |
| C181 | Group 9.3 **Integration 集成** | 注册右键菜单 Shell Integration / **M** / **T** | 资源管理器右键菜单 |
| C182 | | 安装命令行 CLI Install / **S** / Button | `TortoiseGitProc` |
| C183 | | 与版本控制集成 Source Control Integration / **S** / Button | BC `Source Control Integration` |
| C184 | Group 9.4 **Maintenance 维护** | 导出设置 Export Settings / **S** / Button | `Exporting TortoiseGit Settings` |
| C185 | | 导入设置 Import Settings / **S** / Button | 同上 |
| C186 | | 清除缓存 Clear Session Cache / **S** / Button | `Enable log cache` |
| C187 | | 重置界面布局 Reset Layout / **S** / Button | 布局持久化 |
| C188 | | 恢复出厂默认 Restore Factory Defaults / **S** / Button | BC `Restore Factory Defaults` |

#### Page 10 — `Help` 帮助

| # | Group | 按钮（中文名 / English / 尺寸 / 类型） | 对齐 TortoiseGit |
|---|---|---|---|
| C189 | Group 10.1 **Help 帮助** | 帮助 Contents / **L** / Button | `cmdHelp`、`ID_HELP` |
| C190 | | 上下文帮助 Context Sensitive Help / **S** / Button | `BC Context Sensitive Help` |
| C191 | | 快捷键一览 Keyboard Shortcuts / **S** / Button | 手册附录 A |
| C192 | | 检查更新 Check for Updates / **S** / Button | `updatecheck` |
| C193 | | 关于 About / **S** / Button | `cmdAbout`、`ID_APP_ABOUT` |
| C194 | Group 10.2 **Feedback 反馈** | 报告问题 Report a Bug / **S** / Button | TortoiseGit GitLab issue |
| C195 | | 打开日志目录 Open Log Folder / **S** / Button | 推断 |

#### 页面/组统计

| RibbonPage | Group 数 | 按钮数 |
|---|---|---|
| Home 开始 | 5 | 23 |
| Compare 比较 | 5 | 28 |
| Merge 合并 | 5 | 24 |
| Edit 编辑 | 5 | 20 |
| View 视图 | 5 | 25 |
| Filter 过滤 | 5 | 23 |
| Session 会话 | 4 | 12 |
| Report 报告 | 4 | 17 |
| Tools 工具 | 4 | 16 |
| Help 帮助 | 2 | 7 |
| **合计** | **44** | **195** |

> 另有 C196–C204 共 9 条「给 LqRibbon 的实现要求」，不属于按钮。
> 域 C 功能点合计 = 195（Ribbon 按钮）+ 9（Ribbon 引擎要求）= **204**。

### C.4 给 LqRibbon 的 6 条实现要求（从 TortoiseGit 反推）

| # | 要求 | 理由 |
|---|---|---|
| C196 | `RibbonPage` 支持**多页面 + 页面级可见性条件** | `Compare` 页的"文件夹选项"组只在文件夹会话显示；`Filter` 页的"日志过滤"组只在版本控制会话显示；TortoiseGitMerge 单 Tab 无法做到，是它的痛点 |
| C197 | `RibbonGroup` 必须支持 **SplitButton / DropDownButton（含 CheckBox 列表）/ Gallery** 三类复合控件 | TortoiseGitMerge 用了全部三类：`cmdUseBlocks`、`cmdViewBars`、`cmdRegexFilterConfig1` |
| C198 | `RibbonGroup` 的 `SizeDefinition` 与 **ScalingPolicy 收缩顺序**必须显式配置且完整覆盖 | TortoiseGitMerge 的 `cmdGroupBlocks` 漏配收缩策略，是已知不一致 |
| C199 | 每个按钮必须提供 **TooltipTitle + TooltipDescription** 两段式 tooltip，并支持 Keytip | TortoiseGitMerge 的 Ribbon XML 完全没写 tooltip，是明显缺陷 |
| C200 | Ribbon 需要支持**宿主状态栏**（在 Ribbon 底部条里放 ComboBox / 只读 Label），用于编码/行尾/Tab/冲突计数 | TortoiseGitMerge 用 `CMFCRibbonStatusBarPane` + `CMFCRibbonButtonsGroup` 实现；这三个下拉是"每窗格"语义，放 Ribbon 主体会语义错乱 |
| C201 | 支持 **Quick Access Toolbar（QAT）** 与 **Application Menu**，QAT 默认含 Save / Undo / Redo | TortoiseGitMerge 的 QAT 默认项就是这三个 |
| C202 | 支持**界面布局持久化**（页面选中项、QAT 内容、组收缩状态、Ribbon 最小化） | TortoiseGitMerge 用 `TortoiseGitMerge-RibbonSettings` 文件保存 |
| C203 | 提供 **Ribbon 最小化/折叠**（仅显示页面标签）以适配窄窗口与高 DPI | 参考 Office 惯例，TortoiseGit 缺失 |
| C204 | 页面顺序建议固定为 `Home → Compare → Merge → Edit → View → Filter → Session → Report → Tools → Help`，并允许用户隐藏不常用页 | 兼顾 Office 惯例与工具型软件的高频路径 |

---

## D. 与 Beyond Compare 的差异点

> 「以哪一方为准」的含义：`BC` = 以 Beyond Compare 为准（LqCompare 照抄）；`TG` = 以 TortoiseGit 为准；`Lq` = 两者都不采用，走 LqCompare 自己的设计。

| ID | 维度 | Beyond Compare 行为 | TortoiseGit 行为 | 差异性质 | 以谁为准 |
|---|---|---|---|---|---|
| D01 | **文件夹比对** | 核心能力：双栏文件夹树、按内容/CRC/规则比较、孤立项处理、压缩包当文件夹 | **内置工具完全不支持目录层次比对**（官方明确说明） | 能力有无 | **BC** |
| D02 | **会话模型** | 有完整 Session 概念：命名会话、会话文件夹、锁定会话、自动保存会话、Workspace（多窗口多标签快照）、Home 视图管理会话 | 无会话概念；只能靠命令行参数或右键菜单每次重新发起 | 架构级差异 | **BC** |
| D03 | **多标签/多窗口** | 同一窗口多标签页 + 多窗口 + Workspace 恢复 | 每次操作新开一个进程窗口，无标签页 | 架构级差异 | **BC** |
| D04 | **比对类型** | 文本 / 文件夹 / 表格（CSV/Excel/HTML 表）/ 二进制（十六进制）/ 图片 / 注册表 / 版本 / MP3 | 文本（TortoiseGitMerge）/ 图片（TortoiseGitIDiff）/ 统一差异（UDiff） | 能力广度 | **BC** |
| D05 | **表格比对** | 有专门的表格比较视图（网格、关键列对齐、数值容差、列重排、多工作表） | 无 | 能力有无 | **BC** |
| D06 | **十六进制比对** | 有二进制/十六进制比对视图，支持内联编辑与动态重比较、快速逐字节对齐 | 无（仅提示"不是合法文本文件"） | 能力有无 | **BC** |
| D07 | **界面框架** | **菜单栏 + 上下文工具栏 + Home 视图大按钮**，**不是** Office Ribbon | TortoiseGitMerge 是**原生 Ribbon 单 Tab**；客户端对话框是经典菜单+工具栏 | 界面框架 | **Lq**（采用多页面 Office Ribbon，两者都不是理想形态） |
| D08 | **三方合并** | 双路 + 三路合并（带输出面板），自动合并、标记冲突、取整块或选区 | 三栏 + 输出栏，结构等价，另有 TortoiseGit 特有的 Git 冲突语义（mine/theirs 随 rebase 改变含义） | 基本等价 | **BC**（交互） |
| D09 | **自动合并** | 有明确 `自动合并更改` 命令 | 无独立"自动合并"命令，合并结果由 Git merge 生成后再打开编辑器 | 交互差异 | **BC** |
| D10 | **版本控制语义** | 独立工具，不绑定任何 VCS；可通过 SCC 集成 | 深度绑定 Git：BASE/LOCAL/REMOTE、mine/theirs、merge tracking、rebase 语义 | 定位差异 | **TG**（保留 VCS 入口，但做成可选模块） |
| D11 | **日志 / 修订图 / Blame** | **没有**。BC 不提供版本历史可视化 | 有：日志对话框（图列 + 徽标 + 过滤器 + 统计）、修订图（可导出 SVG/PNG）、Blame（按龄着色） | 能力有无 | **TG**（这是 TortoiseGit 的独有强项，必须列入） |
| D12 | **行内差异粒度** | 支持行内差异、替换文本/重命名标识符忽略（Pro）、灵活对齐 | 支持行内差异、**按词**行内差异、忽略注释、正则过滤器 | TG 在"按词"与"正则预处理"上更细 | **TG** |
| D13 | **空白/忽略策略** | 注释 / 空白 / 分隔字符串 / 正则 / 大小写 / 页眉 / 列 / 行终止符，粒度极细 | 空白三档 + 行尾 + 大小写 + 注释 + 正则过滤器 | 粒度 | **BC** |
| D14 | **显示过滤器** | 三套：文件过滤器（名称/属性/日期/大小/内容）、显示过滤器（按比较状态）、文件夹显示过滤器（Always Show Folders / Compare Files and Folder Structure / Only Compare Files / Ignore Folder Structure） | 仅有日志对话框的文本过滤器与路径置灰；**文件比对窗口无显示过滤器** | 能力有无 | **BC** |
| D15 | **临时停用过滤** | `View → Suppress Filters`，被隐藏项以**青绿色**显示且可操作 | 无 | 能力有无 | **BC** |
| D16 | **文件夹同步** | 独立 Folder Sync 视图：5 种同步方法（Update Left/Right/Both、Mirror to Left/Right）、预览、Sync Now | 无 | 能力有无 | **BC** |
| D17 | **文件操作** | 文件夹视图内直接复制/移动/删除（默认尊重过滤器，可覆盖） | 仅右键菜单的 git 操作；比对窗口内只读 | 能力有无 | **BC** |
| D18 | **复制/粘贴与剪贴板比较** | 支持"比较文件或剪贴板内容" | 仅支持"从剪贴板打开补丁" | 能力有无 | **BC** |
| D19 | **比较前转换** | 支持比较前转换文件（如 Word/PDF/RTF 自动转文本、Delphi DFM 转文本） | 无 | 能力有无 | **BC** |
| D20 | **语法高亮** | 文本视图有语法高亮（BC4+）；BC5 有格式化 HTML 视图 | TortoiseGitMerge **无**语法高亮；TortoiseGitBlame 有（`Enable syntax highlighting`） | 能力有无 | **BC**（比对视图）/ **TG**（Blame） |
| D21 | **书签** | 有书签 | 无 | 能力有无 | **BC** |
| D22 | **报告** | 打印和 HTML 比较报告 | 无报告输出；但有补丁导出、变更树导出、清单导出 | 能力有无 | **BC** |
| D23 | **补丁支持** | 以并排方式查看 Unix 补丁文件（BC4+）；有 Text Patch 视图 | 补丁视图更强：补丁文件列表 + 逐文件预览/应用 + 状态着色（可/不可干净应用） | TG 在补丁工作流上更完整 | **TG** |
| D24 | **图像比对** | 图片比对视图（MP3、注册表也有专门视图） | TortoiseGitIDiff：并排/上下/叠加 + alpha 混合滑块 + **XOR 差异模式** + 图像联动 | TG 的 XOR 模式是 BC 没有的亮点 | **TG** |
| D25 | **深色模式** | BC4 无，**BC5 有** | TortoiseGit 有（Win10 1809+），TortoiseGitMerge/Blame 也有开关 | 均有 | **Lq**（跟随系统 + 手动覆盖） |
| D26 | **高 DPI** | BC4+ 支持 | TortoiseGitMerge 响应 `WM_DPICHANGED`，但历史上 RDP/高分屏曾出问题 | 均有 | **BC**（更成熟） |
| D27 | **命令行** | BC 有脚本处理器、完整 CLI | TortoiseGitProc 的 `/command:*` 命令族 + TortoiseGitMerge 的 `/base /mine /theirs /merged` 参数族 | 都有 | **BC**（CLI 更干净）+ **TG**（VCS 命令入口） |
| D28 | **外部工具替换** | 可配置为其他工具 | 可替换 Diff Viewer / Merge Tool / Unified-Diff Viewer / 按扩展名绑定；参数替换符 `%base %mine %theirs %merged` 等 | TG 的替换机制更明确 | **TG** |
| D29 | **文件夹孤立项** | 有 Orphans 概念与专门的显示过滤器 | 无（因为不支持文件夹比对） | 能力有无 | **BC** |
| D30 | **对齐控制** | 对齐相似行、多种对齐样式、可禁止差异对齐、手动对齐/断开对齐 | 无对齐控制（只做行块匹配 + 移动块检测） | 能力有无 | **BC** |
| D31 | **移动/复制行检测** | 无显式开关（靠对齐） | 有 `Moved blocks` 开关 + 左侧白色圆圈提示 + 悬停显示移动来源行号 | TG 更直观 | **TG** |
| D32 | **十六进制内联编辑** | 有 | 无 | 能力有无 | **BC** |
| D33 | **忽略不重要的差异** | 注释/空白/分隔字符串/正则/大小写/页眉/列/行终止符 | 空白三档/行尾/大小写/注释/正则 | 粒度 | **BC** |
| D34 | **注册表比对** | Pro 版有 | 无 | 能力有无 | **BC**（低优先） |
| D35 | **版本信息比较** | 比较 exe/dll 的版本资源 | 无 | 能力有无 | **BC**（低优先） |
| D36 | **冲突可视化** | 用颜色标记冲突部分，但语义是"文件级" | 明确映射 Git 三阶段：BASE / LOCAL(mine) / REMOTE(theirs)，且 `Mark as resolved` 直接写回 Git 索引 | TG 更贴合 VCS | **TG** |
| D37 | **保存时自动备份** | 保存时自动备份 | `Backup original file` 生成 `filename.bak` | 均有 | **BC** |
| D38 | **右键菜单 shell 集成** | Windows 资源管理器 + Windows 11 右键菜单 | 完整 Git 语境右键菜单（Diff / Log / Blame / Revision Graph / Merge / Rebase / Resolve…） | TG 场景化更强，但只在 Git 目录有效 | **Lq**（做通用文件/文件夹右键 + 可选 VCS 扩展） |
| D39 | **鼠标交互** | 拖拽启动比较、拖放列头 | 拖放启动比较、拖入提交对话框自动 git add、右键拖放菜单 | 均有 | **BC** |
| D40 | **内置文本编辑器** | 有独立文本编辑器 | TortoiseGitMerge 内置编辑能力（Enable Edit、撤销/重做、标记块） | 均有 | **BC**（独立编辑视图） |
| D41 | **撤销粒度** | 编辑撤销 + 文件级还原 | 编辑撤销（`Undo.cpp`） + `Reload` 丢弃全部改动 | 均有 | **BC** |
| D42 | **会话/比较的"方向"概念** | 左右对称，可任意交换 | 同理，但 Git 语义下左=theirs/右=mine 会随 rebase 反转 | TG 有额外语义陷阱，需在 UI 明确提示 | **TG**（明确标注 mine/theirs） |
| D43 | **比较缩略图概览** | 有比较缩略图概览 | TortoiseGitMerge 有定位器栏（Locator Bar）+ 行差异栏；修订图有概览窗口 | 语义不同但均有 | **BC**（比对缩略图）+ **TG**（定位器栏三列设计） |
| D44 | **批量/脚本** | 有脚本处理器（可无人值守） | 明确说明"GUI 客户端，自动化请用 git CLI" | 定位差异 | **BC** |
| D45 | **配置可移植性** | 导出/导入设置、快照 | 导出设置、快照（Save Snapshot）、高级设置（注册表） | 均有 | **BC** |
| D46 | **UI 语言与本地化** | 多语言 | 多语言 + 语言包独立安装 + 拼写检查语言按项目配置 | TG 的按项目语言/拼写更细 | **TG** |
| D47 | **图标方案** | 单一图标集 | 可选图标集（`iconset.ico` / Icon Set Selection）、按仓库自定义任务栏图标 | TG 更灵活 | **TG**（低优先） |
| D48 | **行的"标记"用途** | 书签 | 块标记 + `仅保留已标记块`，用于"只提交部分改动" | TG 场景更强 | **TG** |
| D49 | **正则预处理** | 文件格式/规则的关联 | 独立的正则过滤器集合，下拉选择，配置文件 `RegexFilter.ini` | TG 更易用 | **TG** |
| D50 | **日志/历史可视化** | 无 | 日志图列（圆/方 + 颜色）、分支标签徽标（10+ 种语义色）、修订图（可导出矢量图）、统计图 | 能力有无 | **TG**（差异化护城河） |
| D51 | **逐行追溯着色** | 无 Blame | Blame 按龄连续着色（黄→白渐变，端点可配）+ 悬停/点击高亮同修订/同作者 | 能力有无 | **TG** |
| D52 | **合并中止** | 无 | Abort Merge 三模式（Merge / Mixed / Hard） | 能力有无 | **TG** |
| D53 | **补丁序列应用** | 无 | Apply Patch Serial（Add/Up/Down/Remove/Apply）+ Send Mail + Request Pull | 能力有无 | **TG**（Patch 页） |
| D54 | **图像 XOR 差异** | 无 | TortoiseGitIDiff 关闭 alpha 后有 XOR 差异模式（未变纯白、变化着色） | TG 独有 | **TG** |
| D55 | **状态栏组合框** | 工具栏内下拉 | 编码/行尾/Tab **每窗格独立**的组合框放在状态栏 | TG 的"每窗格独立"更清晰 | **TG** |
| D56 | **比较规则的可视化配置入口** | `Session → Session Settings` 多页对话框 | TortoiseGitMerge 的 Settings 只有 General/Colors/Misc | BC 的规则面板更完整 | **BC** |
| D57 | **"文本相同但文件不同"诊断** | 有"忽略不重要的差异"的细粒度说明 | 明确弹出诊断：Whitespace changes / Encoding / Newlines | TG 的诊断更直白 | **TG** |
| D58 | **文件过大/非法文本的处理** | 自动切换到十六进制或提示转换 | 明确提示 `not a valid text file` / `The file is too big` / 无法比对目录 | TG 的边界提示更清楚 | **TG** |
| D59 | **首选项与文件格式解耦** | File Formats 是独立概念（按扩展名定义比较规则与转换） | 按扩展名只能绑定 diff/merge **程序**，不能绑定比较**规则** | BC 更强大 | **BC** |
| D60 | **会话参数在命令行暴露程度** | 可通过 CLI 指定会话与规则 | TortoiseGitMerge 只能指定文件路径与少量开关，**无法指定比较规则** | 差异 | **BC**（规则可 CLI 化，便于 CI） |

### D.1 差异点结论（给架构决策用）

1. **必须照抄 Beyond Compare 的**：会话模型、文件夹比对为核心、显示过滤器三件套、孤立项、表格/十六进制比对、对齐控制、报告、文件夹同步、比较前转换、脚本/CLI。这些是 TortoiseGit 的**结构性缺失**，也是 LqCompare 相对 TortoiseGit 的基本盘。
2. **必须吸收 TortoiseGit 的**：日志/修订图/Blame 三位一体的历史可视化、行块标记与"只保留已标记块"、按词行内差异、正则预过滤器、移动块检测、补丁文件列表与逐文件应用、图像 XOR 差异、每窗格独立的编码/行尾/Tab、mine/theirs 语义提示、冲突写回 VCS 索引。
3. **两者都不要照抄的**：
   - TortoiseGitMerge 的 **单 Tab Ribbon**（页面拥挤、tooltip 缺失、收缩策略不完整）
   - TortoiseGit 客户端对话框的**经典菜单+工具栏**（LqCompare 应当统一到 Ribbon）
   - Beyond Compare 的**无 Ribbon** 界面（用户明确要求参考 Ribbon）
   - Beyond Compare 把文件夹同步做成**独立视图**（LqCompare 可考虑做成文件夹会话里的一个"同步"页面/模式，减少概念数）

---

## E. 附录

### E.1 快捷键总表（可直接照抄实现）

| 快捷键 | 作用 | 适用模式 | 来源 |
|---|---|---|---|
| `Ctrl-Q` / `Ctrl-W` / `Esc` | 退出程序 | 通用 | `官方文档` |
| `Ctrl-C` | 复制选中文本 | 通用 | `官方文档` |
| `Ctrl-X` / `Shift-Del` | 剪切选中文本 | 通用 | `官方文档` |
| `Ctrl-V` / `Shift-Insert` | 粘贴 | 通用 | `官方文档` |
| `Ctrl-Z` / `Alt-Backspace` | 撤销 | 通用 | `官方文档` |
| `Ctrl-F` | 查找对话框 | 通用 | `官方文档` |
| `Ctrl-O` | 打开文件 | 通用 | `官方文档` |
| `Ctrl-S` | 保存 | 通用 | `官方文档` |
| `Ctrl-Shift-S` | 另存为 | 通用 | `官方文档` |
| `F7` / `Ctrl-Down` | 下一差异 | 通用 | `官方文档` |
| `Shift-F7` / `Ctrl-Up` | 上一差异 | 通用 | `官方文档` |
| `Ctrl-R` | 重新加载并丢弃改动 | 通用 | `官方文档` |
| `Ctrl-T` | 切换显示空白符 | 通用 | `官方文档` |
| `Ctrl-L` | 切换折叠未变更区域 | 通用 | `官方文档` |
| `Ctrl-P` | 切换折行 | 通用 | `官方文档` |
| `Ctrl-G` | 跳转行 | 通用 | `官方文档` |
| `Ctrl-A` | 全选 | 通用 | `官方文档` |
| `Ctrl-鼠标滚轮` | 左右滚动 | 通用 | `官方文档` |
| `Ctrl-Tab` | 切换左/右/底视图 | 通用 | `官方文档` |
| `Ctrl-M` | 切换标记当前变更 | 通用 / Diff | `官方文档` |
| `Ctrl-D` | 单栏/双栏切换 | Diff | `官方文档` |
| `Ctrl-U` | 交换视图 | Diff | `官方文档` |
| `F12` | 使用左块 | Diff | `官方文档` |
| `F8` | 下一冲突 | 冲突解决 | `官方文档` |
| `Shift-F8` | 上一冲突 | 冲突解决 | `官方文档` |
| `Ctrl-F9` | 使用左块 | 冲突解决 | `官方文档` |
| `Ctrl-Shift-F9` | 先左后右 | 冲突解决 | `官方文档` |
| `Ctrl-F10` | 使用右块 | 冲突解决 | `官方文档` |
| `Ctrl-Shift-F10` | 先右后左 | 冲突解决 | `官方文档` |
| `F1` | 帮助 | 全局 | `官方文档` |
| `F5` | 刷新当前视图（图标叠加 / 提交对话框重扫 / 日志重新拉取 / 修订图 / 仓库浏览器） | 全局 | `官方文档` |
| `Ctrl+Space` | 触发日志消息自动补全 | 提交对话框 | `官方文档` |
| `Ctrl+return` | 提交对话框 OK | 提交对话框 | `官方文档` |
| `Ctrl+V` / `Shift+Insert` | 粘贴哈希跳转到提交 | 日志对话框 | `官方文档` |
| `ALT+UP` / `ALT+DOWN` | 按跳转类型上/下导航 | 日志对话框 | `官方文档` |
| `ALT+LEFT` / `ALT+RIGHT` | 选择历史前进/后退 | 日志对话框 | `官方文档` |
| `Delete` | 删除最近消息历史条目 | 提交对话框 | `官方文档` |
| `Shift` + 右键 | 展开扩展上下文菜单 | 资源管理器 | `官方文档` |
| `Shift` + Diff | 使用次要差异工具 | 资源管理器 | `官方文档` |
| `Shift` + Delete | 永久删除（绕过回收站） | 状态对话框 | `官方文档` |
| 中键 / 右键点最大化 | 仅纵向 / 仅横向最大化窗口 | 对话框 | `官方文档` |

### E.2 TortoiseGitMerge 颜色项完整清单（11 项，每项含前景+背景）

`Normal` / `Added` / `Removed` / `Modified` / `Conflicted` / `Conflict resolved` / `Empty` / `Inline added text` / `Inline removed text` / `Misc whitespaces` / `Restore Default` 按钮

### E.3 TortoiseGit 状态着色完整清单（对话框用）

`Blue` 本地已修改 / `Purple` 已添加 / `Dark red` 已删除或缺失 / `Green` 本地与仓库都改（**可能**冲突）/ `Bright red` 本地改+仓库删 或 仓库改+本地删（**一定**冲突）/ `Black` 未变更与未版本

### E.4 日志图标签语义色完整清单

| 标签类型 | 颜色/形状 |
|---|---|
| Normal tag | 黄色矩形 |
| Annotated tag | 黄色矩形 + 右侧尖角 |
| Active branch | 暗红色矩形 |
| Local branch | 绿色矩形；有上游跟踪时圆角 |
| Remote branch | 桃色矩形；远程跟踪分支圆角 |
| Stash | 暗灰色矩形 |
| Bisect bad | 浅红色矩形 |
| Bisect good | 蓝色矩形 |
| Bisect skip | 灰色矩形 |
| Note node | 指向 `refs/notes` 的引用（颜色可配） |
| Collapsed revision | 空心圆 + 空心方 |
| 普通提交 | 圆形 |
| 合并提交 / 分支点 | 方形 |

### E.5 行状态图标完整清单（8 种）

`lineadded.ico` 新增 / `lineremoved.ico` 删除 / `lineequal.ico` 未变 / 已还原（reverted）/ `linewhitespace.ico` 仅空白变更 / `lineedited.ico` 手工编辑 / `lineconflicted.ico` 冲突 / `lineconflictedignored.ico` 冲突被忽略 / `linemarked.ico` 已标记 / `moved.ico` 移动

### E.6 外部工具替换符完整清单

**Diff Viewer**：`%base` `%bname` `%mine` `%yname` `%bpath` `%ypath` `%brev` `%yrev` `%wtroot`
**Merge Tool**：`%base` `%bname` `%mine` `%yname` `%theirs` `%tname` `%merged` `%mname` `%wtroot`
**Unified-Diff Viewer**：`%1` `%title`

### E.7 修订图导出格式

`.svg` / `.wmf` / `.gv` / `.png` / `.jpg` / `.bmp` / `.gif`

---

## F. 未覆盖 / 待确认项（后续补测建议）

| ID | 待确认项 | 建议补测方式 |
|---|---|---|
| F01 | 日志对话框**工具栏按钮的完整清单与顺序**（`SetButtons` 数组位于源文件被截断处或 `LogDlg.h`） | 抓 `src/TortoiseProc/LogDialog/LogDlg.h`，或实际安装 TortoiseGit 后截图/抓资源 |
| F02 | 日志对话框**修订列表的完整列头**（定义在 `CGitLogListBase`） | 抓 `src/TortoiseProc/GitLogListBase.cpp` 的 `InsertGitColumn()` |
| F03 | 文件变更列表的**完整列头**（定义在 `CGitStatusListCtrl`） | 抓 `src/TortoiseProc/GitStatusListCtrl.cpp` |
| F04 | 提交对话框中**右键菜单的完整项** | 抓 `src/TortoiseProc/CommitDlg.cpp` 的 `OnContextMenu` |
| F05 | 修订图**右键菜单的完整项**（文档只提到 Compare Revisions / Show Log） | 抓 `src/TortoiseProc/RevisionGraph/RevisionGraphWnd.cpp` |
| F06 | TortoiseGitIDiff 的**完整 View 菜单与工具栏按钮** | 抓 `src/TortoiseIDiff/TortoiseIDiff.rc` |
| F07 | 各对话框的**加速键表**（`IDR_ACC_LOGDLG` / `IDR_ACC_PATCHVIEW` 等） | 抓 `TortoiseProcENG.rc` 的 `ACCELERATORS` 段 |
| F08 | 修订图节点的**具体类型与默认颜色值** | 抓 `src/TortoiseProc/RevisionGraph/RevisionGraph.cpp` 的颜色初始化 |
| F09 | 日志图列的**默认线条颜色/宽度/节点大小具体数值** | 抓 `Color Settings 3` 默认值定义 |
| F10 | 各 View（LeftView/RightView/BottomView）的**右键菜单构建代码**（本报告 A.12 的条目来自字符串资源与文档，未逐条核对 View 代码） | 抓 `src/TortoiseMerge/LeftView.cpp` 的 `OnContextMenu` |
| F11 | TortoiseGitMerge 的**加速键表 `IDR_MAINFRAME ACCELERATORS`** 是否与手册附录 A 完全一致 | 抓 `TortoiseMergeENG.rc` 的 `ACCELERATORS` 段 |
| F12 | 「文件夹比对」在 TortoiseGit 侧是否真的**完全没有**（是否存在隐藏的目录 diff 入口，如外部工具默认配置） | 核对 `TortoiseGitMerge` 的 `/diff` 开关与 `TortoiseMerge` 的错误提示 `can't diff directories` |

---

**报告完**