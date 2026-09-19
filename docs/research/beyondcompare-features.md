# Beyond Compare 5 全功能测绘清单

> **文档用途**：作为 LqCompare（Qt 5.15.2 / C++17 文件与文件夹比对工具）的功能对标基线，支持「一个功能点 = 一个可独立验收的 issue」的拆分粒度。
>
> **对标产品**：Beyond Compare 5（Scooter Software），含 Standard 与 Pro 两个版本。Pro 专有功能用 `[Pro]` 标注；仅 Windows 可用用 `[Win]` 标注。
>
> **资料来源**：
> - Scooter Software 官方帮助：`https://www.scootersoftware.com/v5help/`（Command Line Reference、Scripting Reference、What's New、Session Settings、File Formats、Reports、File Masks、Archives、Standard vs Pro 等）
> - Beyond Compare 5 Change Log（5.0 ~ 5.5.x 全部发布说明）
> - Beyond Compare 4 官方帮助文档全目录（作为 BC5 未展开细节的补充基线）
> - Scooter Software KB：Patch Files 等
> - 补充：Beyond Compare 官方论坛 / 第三方集成文档 / 产品特性页
>
> **版本基准**：BC 5.2.x 系列（含 5.2.0 的 Unix owner/group/type 比对、Windows 扩展属性、Admin Policies；5.2.1 的报告打印修复）
>
> **术语约定**：专有名词保留英文原名（与 BC 官方 UI 文案一致），中文为说明性翻译。列名 `BE` 表示「默认行为（Beyond Compare Default）」，列名 `陷阱` 表示「常见边界 / 易错点」。

---

## 目录

| 域编号 | 功能域 | 条目数 |
|---|---|---|
| 1 | 会话类型（Session Types） | 68 |
| 2 | 会话管理与 Home 视图（Session Management / Home View） | 42 |
| 3 | 工作区（Workspaces） | 11 |
| 4 | 文本比对核心：规则、重要性与对齐算法 | 62 |
| 5 | 文本比对 UI 与显示 | 52 |
| 6 | 文本编辑与文件操作 | 61 |
| 7 | 文本合并 / 三路合并（Text Merge） | 71 |
| 8 | 文件夹比对核心（Folder Compare Criteria） | 55 |
| 9 | 文件夹视图 UI、列与显示过滤器 | 74 |
| 10 | 文件操作：复制 / 移动 / 删除 / 重命名 / 属性 | 58 |
| 11 | 文件夹同步（Folder Sync） | 46 |
| 12 | 文件夹合并（Folder Merge） | 22 |
| 13 | 过滤与文件掩码（Filters & File Masks） | 57 |
| 14 | 文件格式定义（File Formats） | 71 |
| 15 | 替换规则（Replacements） | 19 |
| 16 | 报表与打印（Reports & Printing） | 68 |
| 17 | 补丁文件（Patch Files） | 14 |
| 18 | 十六进制比对（Hex Compare） | 41 |
| 19 | 表格比对（Table Compare） | 57 |
| 20 | 图片比对（Picture Compare） | 44 |
| 21 | 媒体比对（Media Compare） | 31 |
| 22 | 注册表比对（Registry Compare） | 37 |
| 23 | 版本比对（Version Compare） | 28 |
| 24 | 文本编辑视图（Text Edit） | 23 |
| 25 | 压缩包内比对（Archives） | 26 |
| 26 | 快照（Snapshots） | 19 |
| 27 | 远程与云服务配置（Profiles: FTP / SFTP / Cloud） | 52 |
| 28 | 版本控制集成（Source Control Integration） | 38 |
| 29 | 命令行接口（Command Line） | 63 |
| 30 | 脚本语言（Scripting） | 74 |
| 31 | 外部调用与自动化（Automation / Third-party） | 27 |
| 32 | 程序选项（Tools > Options） | 82 |
| 33 | 外观：颜色 / 字体 / 主题（Appearance） | 36 |
| 34 | 自定义命令与快捷键（Customize Commands） | 24 |
| 35 | 国际化与平台差异（i18n / Platform / Admin Policies） | 33 |
| 36 | 文件系统细节与性能（File System / Performance） | 46 |
| 37 | 设置存储、导入导出与迁移（Settings Storage） | 24 |
| 38 | 帮助、许可与诊断（Help / Licensing / Diagnostics） | 26 |
| **合计** | **38 个功能域** | **1682** |

---

## 1. 会话类型（Session Types）

> Beyond Compare 的顶层概念：每种比对任务对应一个「会话类型」，各自拥有独立的视图、菜单命令集与 Session Settings 对话框。新建会话时可选类型共 13 种（含 Text Edit / Text Patch 两个辅助视图）。

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SESS-001 | 文本比较会话 | Text Compare | 并排或上下比对两个文本文件，高亮差异并支持编辑。 | 默认视图类型，`.txt/.c/.cpp` 等由 File Format 关联 | 打开未知扩展名文件时的默认兜底视图 |
| SESS-002 | 文本合并会话 | Text Merge `[Pro]` | 三路合并：左上/右上为两个版本，中部为共同祖先，下部为可编辑输出。 | 需 3 或 4 个文件参数才自动进入 | Standard 版无此类型；未保存输出会丢失内容 |
| SESS-003 | 表格比较会话 | Table Compare | 按单元格比对 CSV/TSV/Excel/HTML 表格数据，支持多工作表。 | BC4 前称 Data Compare | 列按「比较列」映射而非文件原始列序 |
| SESS-004 | 十六进制比较会话 | Hex Compare | 逐字节比对二进制文件，十六进制 dump 布局。 | 二进制文件默认兜底视图之一 | 大文件内存占用；需 Byte Address 显示控制 |
| SESS-005 | 媒体比较会话 | Media Compare | 比对 MP3/FLAC/MP4(AAC) 的标签与元数据差异。 | BC5 由「MP3 Compare」更名而来 | 仅比对标签，不对比音频波形 |
| SESS-006 | 图片比较会话 | Picture Compare | 并排比对两张图片并高亮像素差异。 | `.jpg/.png` 等由 File Format 关联 | 大图缩放策略；容差模式判定 |
| SESS-007 | 注册表比较会话 | Registry Compare `[Pro] [Win]` | 比对本地/远程实时注册表或注册表导出文件。 | 仅 Pro + Windows | 需要管理员权限才能写 HKLM；非 Windows 无此类型 |
| SESS-008 | 版本比较会话 | Version Compare `[Win]` | 比对可执行文件（exe/dll/ocx）的版本信息资源。 | 仅 Windows | BC5 增强了 MUI 处理与更多头部字段 |
| SESS-009 | 文件夹比较会话 | Folder Compare | 以资源管理器风格并排比对两个文件夹树。 | 主入口会话类型 | 递归深度与过滤交互；内容比对可能很慢 |
| SESS-010 | 文件夹合并会话 | Folder Merge `[Pro]` | 三路文件夹合并：左/右为版本，中部为祖先，逐文件决定保留哪侧。 | 需显式新建；`/fv="Folder Merge"` | 合并不可撤销（无 Undo）；冲突文件需人工选边 |
| SESS-011 | 文件夹同步会话 | Folder Sync | 专用同步视图，预览将执行的复制/删除操作后一键执行。 | Windows/macOS 支持 `/sync` 开关 | 执行前务必核对预览；删除默认走回收站 |
| SESS-012 | 文本编辑视图 | Text Edit | 单窗格纯文本编辑器（无比对），可独立打开/保存文件。 | 通过 Tools > Edit Text Files 或 `/edit` 打开 | 无差异高亮；语法高亮取决于 File Format |
| SESS-013 | 文本补丁视图 | Text Patch | 打开 `.diff`/`.patch` 文件查看，并支持 Apply Patch 到原始文件。 | 双击 .diff/.patch 进入 | Apply Patch 仅支持单文件补丁，不支持多文件 |
| SESS-014 | 从文件对自动选视图 | File-pair auto view selection | 依据 File Format 的掩码匹配，自动决定用哪个视图打开文件对。 | 命中首个匹配的文件格式 | 掩码冲突时以 File Formats 列表顺序（优先级）决定 |
| SESS-015 | 从剪贴板新建比较 | Open Clipboard | 以剪贴板内容作为某一侧的文件内容参与比较。 | 需从 File 菜单主动触发 | 剪贴板内容无文件名/路径，保存时需另存 |
| SESS-016 | 用另一视图打开 | Compare Using | 用另一种视图类型重新打开当前已比对的文件对。 | 子菜单列出可用视图 | 部分视图组合无意义（如图片与十六进制） |
| SESS-017 | 会话类型与 Pro 判定 | Pro-only session gating | Text Merge / Folder Merge / Registry Compare 等仅 Pro 可用。 | 试用版同时解锁 Standard + Pro 功能 | 可在 Help > About 中关闭 Pro 模式以模拟 Standard |
| SESS-018 | 会话设置总入口 | Session > Session Settings | 打开当前会话类型专属的设置对话框（分多个 Tab）。 | 工具栏 Rules 按钮等效入口 | 底部下拉决定改动作用域，误选会污染默认值 |
| SESS-019 | 设置作用域：仅当前视图 | Use for this view only | 设置只影响当前打开的视图实例，不写入会话。 | 默认即为该作用域 | 关闭标签后设置丢失 |
| SESS-020 | 设置作用域：更新会话默认值 | Update session defaults | 把设置写回该会话类型的默认值，影响后续所有新会话。 | 需显式选择 | 影响面大，恢复需 Factory Defaults |
| SESS-021 | 子会话设置作用域 | Use for these files within parent session | 文件夹会话派生的子文件比较，仅对该文件对记住设置。 | 仅子会话可见 | 仅存在于从文件夹会话派生的文件视图 |
| SESS-022 | 父会话全局子设置 | Use for all files within parent session | 文件夹会话派生的所有同类型子会话都使用该设置。 | 仅子会话可见 | 与上一项容易混淆 |
| SESS-023 | 会话规格页 | Specs tab | 每个会话设置对话框的首个 Tab，定义参与比对的两侧路径（Specs）。 | 全部会话类型均有 | 远程/压缩包路径的写法与本地路径不同 |
| SESS-024 | 文件夹会话比较页 | Folder: Comparison tab | 定义「什么算差异」：大小、时间戳、属性、内容、版本。 | 大小 + 时间戳 + 内容规则化 | 勾选内容比对会显著变慢 |
| SESS-025 | 文件夹会话处理页 | Folder: Handling tab | 定义后台扫描、展开、压缩包处理、符号链接、权限复制、自动刷新等。 | 后台扫描开启 | 跟随符号链接可能造成循环目录 |
| SESS-026 | 文件夹会话名称过滤器页 | Folder: Name Filters tab | 按文件掩码包含/排除文件与文件夹。 | 空（不过滤） | 掩码大小写敏感性依赖平台 |
| SESS-027 | 文件夹会话其它过滤器页 | Folder: Other Filters tab | 按日期、大小、DOS 属性、源代码控制状态、文本内容过滤。 | 默认排除受保护的系统文件 | 多个过滤器同时生效时底部出现过滤器提示条 |
| SESS-028 | 文件夹会话杂项页 | Folder: Misc tab | 对齐覆盖（Alignment Override）与文件格式关联覆盖。 | 空 | 对齐覆盖支持通配符与正则 `[Pro]` |
| SESS-029 | 文件夹同步同步页 | Folder Sync: Sync tab | 定义同步预设（更新/镜像、方向）与操作规则。 | 无预设 | 预设与手动操作混用时以 Session Settings 为准 |
| SESS-030 | 文本会话格式页 | Text: Format tab | 指定左右侧文件使用的 File Format 与编码覆盖。 | Encoding Override = 无（使用文件格式编码） | 编码覆盖错误会导致整文件全差异 |
| SESS-031 | 文本会话重要性页 | Text: Importance tab | 定义哪些语法元素/空白差异算「重要」。 | 语法元素按文件格式；大小写敏感关闭 | 与 Replacements 的作用范围容易重叠 |
| SESS-032 | 文本会话对齐页 | Text: Alignment tab | 选择对齐算法与偏斜容差、最近匹配等参数。 | Standard 方法 + 使用最近匹配 | Replacement（LCS）算法不支持相似度匹配 |
| SESS-033 | 文本会话替换页 | Text: Replacements tab `[Pro]` | 定义「左侧 X ↔ 右侧 Y」视为不重要的替换规则。 | 空 | 规则可限定仅搜索某一侧 |
| SESS-034 | 文本合并设置页 | Text Merge tabs | 与 Text Compare 类似：Specs / Format / Importance / Alignment。 | 与文本比较同构 | 缺少 Replacements 页 |
| SESS-035 | 表格会话列页 | Table: Columns tab | 定义比较列与左右文件列的映射、键列、列处理方式。 | 第一列作为唯一键列 | 未定义键列时才允许「未排序对齐」 |
| SESS-036 | 表格会话格式页 | Table: Format tab | 指定表格文件的 File Format（Data Format）与类型设置。 | 由掩码自动匹配 | 单侧比对时「隐藏相同列」曾出现全隐藏缺陷（5.0.1 修复） |
| SESS-037 | 十六进制会话设置 | Hex: Session Settings | 十六进制比对的相关设置。 | 无额外关键项 | 字节序、地址基准在 View 菜单而非设置 |
| SESS-038 | 图片会话设置 | Picture: Specs / Format / Comparison | 路径规范、文件格式、比较模式（容差/不匹配范围/二进制/混合）。 | 容差模式 | 四种模式的判定语义完全不同 |
| SESS-039 | 媒体会话设置 | Media: Session Settings | 定义哪些标签字段视为重要（Importance 树）。 | 常见标签重要 | MP4/FLAC 标签映射差异 |
| SESS-040 | 注册表会话设置 | Registry: Session Settings | 注册表比对范围设置（键、值、类型）。 | 比对键与值 | 实时注册表与导出文件的键路径基准不同 |
| SESS-041 | 版本会话设置 | Version: Specs / Importance | 定义参与比对的文件与哪些版本字段算重要。 | 全部字段重要 | 字段名本地化（MUI）时需注意 |
| SESS-042 | 会话类型图标与图例 | Session type icon / Legend | 每种会话类型有独立图标；文件夹视图有颜色图例窗口。 | 图例默认关闭 | 颜色语义随显示过滤器变化 |
| SESS-043 | 新建会话对话框 | New Session dialog | 从 Home 视图或 Session > New Session 选择类型并填 Specs。 | Home 视图 New 分支双击即新建 | 新建后未保存则只出现在「Recent」 |
| SESS-044 | 会话最近使用列表 | Recent sessions (MRU) | 自动记住最近使用的会话以便重新调用。 | 自动保存 | MRU 存储于 BCState.xml |
| SESS-045 | 命名会话 | Named Session | 用描述性名称保存会话，可通过命令行按名打开。 | 用户显式保存 | 名称冲突需避免；重名会话树中不可辨 |
| SESS-046 | 会话分组（文件夹） | Session folders / groups | 在会话树中以文件夹组织命名会话。 | 扁平 | 分组不参与命令行按名匹配的优先级判定 |
| SESS-047 | 锁定命名会话 | Lock session | 防止命名会话被误改写。 | 未锁定 | 锁定后修改设置需先解锁 |
| SESS-048 | 编辑会话默认值 | Edit Session Defaults | 在 Home 视图树中选择「Edit Session Defaults」编辑各类型默认设置。 | 出厂默认 | 默认值改动影响所有新会话 |
| SESS-049 | 保存会话 | Session > Save Session | 保存当前会话（含 Specs 与所有 Session Settings）。 | 命名会话可保存 | 未命名会话会转为「另存为」流程 |
| SESS-050 | 会话另存为 | Session > Save Session As | 以新名称保存当前会话，支持新建子文件夹。 | — | macOS 深色模式下「新建子文件夹」按钮曾出现黑字黑底（已修） |
| SESS-051 | 清除会话 | Session > Clear Session | 用同类型的空白会话替换当前视图。 | — | 会丢失当前会话的临时设置；需确认保存 |
| SESS-052 | 交换两侧 | Session > Swap Sides | 交换左右两侧的基准路径/文件。 | — | 图片与媒体视图中存在隐藏项时曾崩溃（已修） |
| SESS-053 | 后退 / 前进 | Session > Back / Forward | 在文件夹会话中回退/前进到之前比较过的基准文件夹。 | 有历史才可用 | 只记录文件夹会话的导航历史 |
| SESS-054 | 向上浏览 | Session > Browse Folder > Up One Level | 将一侧或两侧基准文件夹上移到父文件夹。 | 支持左/右/两侧全选 | 到盘根后不可再上移 |
| SESS-055 | 比较父文件夹 | Session > Compare Parent Folder | 基于当前文件的父文件夹打开新的 Folder Compare。 | — | 从单文件比对「上升」到目录比对的主要路径 |
| SESS-056 | 在子会话间导航 | Next / Previous Difference Files | 在父文件夹会话中跳到下一对有差异的文件。 | 仅子会话可用 | 父会话窗口关闭后子会话无法导航 |
| SESS-057 | 复制并跳转下一差异 | Copy File to Right/Left and Open Next Difference | 复制当前文件到指定侧并自动打开父会话中的下一对差异文件。 | 仅子会话可用 | 复制失败时应中断导航 |
| SESS-058 | 会话信息统计 | Session > Compare Info | 弹出当前比较的统计结果（数量汇总）。 | — | 统计口径随显示过滤器变化 |
| SESS-059 | 会话保存提示 | Save modified session prompt | 命名会话的参数被改动后，退出时提示是否保存。 | 弹出提示 | 选择「不保存」后改动仅存于本次运行 |
| SESS-060 | 会话路径占位与标题 | /title1..4, /vcs1..4 | 可在路径编辑框中显示自定义标题或 VCS 路径。 | 显示真实路径 | VCS 路径还会用于挑选文件格式 |
| SESS-061 | stdout / stdin 会话 | `-` 参数 | 从标准输入读取一侧内容参与比较。 | 需管道输入 | 无法从 stdin 推断文件格式 |
| SESS-062 | 设置包导入会话 | `.bcpkg` 参数 | 命令行传入设置包即导入其中全部设置。 | — | `/silent` 时全部设置静默导入 |
| SESS-063 | 会话内嵌文件格式覆盖 | Session file-format override | 文件夹会话可覆盖该会话内各文件格式的启用状态。 | 继承全局 | 会话内启用而全局禁用的格式会以粗体列出 |
| SESS-064 | 会话中的规则与格式分层 | Rules vs. File Formats | 会话设置定义「抽象重要性」，File Format 定义「语法细节」。 | 分层解耦 | 改语法需进 File Formats 对话框，而非会话设置 |
| SESS-065 | Tab 式多会话 | Tabs | 一个窗口内以标签页承载多个会话。 | 新会话开新标签 | Tab 顺序在 Home 批量启动时曾不一致（5.0.2 修复） |
| SESS-066 | 多窗口 | Session > New Window | 在新窗口中打开 Home 视图或会话。 | 单窗口多标签 | `BCompare.exe` 同时只允许一个进程实例 |
| SESS-067 | 会话内只读模式 | Read-only session | 通过 `/ro`、`/ro1`、`/ro2` 禁用全部或单侧编辑。 | 可编辑 | `/readonly` 与 `/leftreadonly` 可组合 |
| SESS-068 | 会话类型命令行枚举 | `/fv=<type>` | 命令行显式指定视图类型，支持 13 种取值（见 29 章）。 | 由文件类型推断 | 类型名必须与官方字符串完全一致 |

---

## 2. 会话管理与 Home 视图（Session Management / Home View）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MGMT-001 | 主视图 | Home View | 程序默认落地页，集中管理新建会话、已保存会话与最近会话。 | 启动即显示 | 可配置启动行为（Options > Startup） |
| MGMT-002 | 主视图-新建分支 | Home > New branch | 树形列出全部会话类型，双击即以该类型新建会话。 | 展开 | 类型顺序固定 |
| MGMT-003 | 主视图-已保存会话树 | Home > Sessions tree | 列出用户保存的全部命名会话，可右键重命名/删除/锁定。 | — | 会话树即 BCSessions.xml 的可视化 |
| MGMT-004 | 主视图-编辑默认会话 | Home > Edit Session Defaults | 选择会话类型后编辑其全局默认设置。 | 出厂默认 | 与 Update session defaults 是同一份数据 |
| MGMT-005 | 主视图-搜索框 | Home View search | 过滤会话树中的会话名称。 | 空 | 部分平台曾出现 +/- 按钮绘制异常（已修） |
| MGMT-006 | 主视图-右侧设置面板 | Home > Tabbed settings page | 在右侧以标签控件编辑选中会话的各项设置。 | 默认第一个 Tab | 编辑自带「保存」按钮 |
| MGMT-007 | 主视图-最近会话列表 | Recent sessions | 列出最近打开过的会话与比较。 | 自动维护 | 数量上限可配置 |
| MGMT-008 | 主视图-快速比较入口 | Compare Files / Compare Folders | 直接选择两个文件或两个文件夹发起比较。 | 工具栏入口 | 与「新建会话」的区别是不保存 Specs |
| MGMT-009 | 会话菜单 | Session menu | 当前会话操作命令的集合（保存/设置/清除/交换/报表等）。 | 随视图类型变化 | 不同视图类型下菜单项差异很大 |
| MGMT-010 | 新建选项卡 | Session > New Tab | 在当前窗口新开标签并回到主视图。 | — | — |
| MGMT-011 | 新建窗口 | Session > New Window | 打开新窗口并显示主视图。 | — | — |
| MGMT-012 | 打开新会话 | Session > New Session | 新开视图并显示主视图以选择会话类型。 | — | — |
| MGMT-013 | 加载已命名会话 | Load named session | 从菜单/命令行按名称加载已保存会话。 | — | 名称含空格时命令行须加引号 |
| MGMT-014 | 关闭选项卡 | Session > Close Tab | 关闭当前标签，必要时提示保存。 | — | 有未保存编辑时会弹确认 |
| MGMT-015 | 退出 | Session > Exit | 退出程序；退出前处理各标签的未保存内容。 | — | 脚本运行中退出会中断脚本 |
| MGMT-016 | 会话设置对话框 | Session Settings dialog | 各会话类型的设置集中入口（多 Tab）。 | Rules 按钮等效 | 底部作用域下拉是最大陷阱点 |
| MGMT-017 | 会话默认值保存 | Session defaults persistence | 会话默认值持久化到 BCSessions.xml / BCPreferences.xml。 | 自动 | 跨版本升级需导入 v4 设置（5.0.1 修过崩溃） |
| MGMT-018 | 导入设置 | Tools > Import Settings | 从 `.bcpkg` 或旧版本设置导入会话/选项/文件格式。 | 可选择导入范围 | Linux/macOS 导入 v4 设置曾崩溃（5.0.1 修复） |
| MGMT-019 | 导出设置 | Tools > Export Settings | 将选定范围的设置导出为 `.bcpkg` 包。 | 全选 | macOS 上列表复选框在无选中项时切换曾崩溃（已修） |
| MGMT-020 | 设置包命令行导入 | `.bcpkg` on command line | 命令行直接导入设置包。 | `/silent` 时静默全量导入 | 导入会覆盖同名设置，无撤销 |
| MGMT-021 | 会话重命名 | Rename session | 在会话树右键重命名命名会话。 | — | 重命名后命令行旧名失效 |
| MGMT-022 | 会话删除 | Delete session | 在会话树右键删除命名会话。 | — | 已有配置文件仍留在磁盘（需删除对应 xml 条目） |
| MGMT-023 | 会话锁定 | Lock session | 防止命名会话被误改写。 | 未锁定 | 锁定状态存储于会话定义中 |
| MGMT-024 | 会话共享 | Sharing Sessions | 通过导出 `.bcpkg` 或直接分发会话定义共享会话。 | — | 会话中若含密码，导出后密码按新策略处理 |
| MGMT-025 | 会话保存状态指示 | Modified marker | 标题/标签以标记指示当前会话有未保存修改。 | — | 未命名的临时会话无标记 |
| MGMT-026 | 关闭时保存提示 | Save changes on close | 关闭标签或退出时提示保存会话改动。 | 提示 | — |
| MGMT-027 | 启动时行为 | Startup action | 启动时显示主视图 / 恢复上次会话 / 显示空白比较等。 | 显示主视图 | 见 OPT 章 Startup 选项 |
| MGMT-028 | 任务栏脚本入口 | Scripting Task Bar | 脚本执行期间在任务栏显示进度窗口与错误信息。 | 显示 | `/silent` 抑制任务栏与状态窗口 |
| MGMT-029 | 脚本状态窗口 | Scripting Status Window | 展示脚本逐行执行进度、错误与耗时。 | 显示 | 可用 Tweaks 中的 Beep/Close when finished 控制 |
| MGMT-030 | 会话树拖放 | Drag and drop in session tree | 在会话树中拖动调整分组归属。 | — | — |
| MGMT-031 | 从资源管理器拖入 | Drag-and-drop from Explorer/Finder | 拖入文件/文件夹以创建对应比较会话。 | — | 左右顺序由拖放位置决定 |
| MGMT-032 | 外框标题提示 | Titlebar status | 以管理员/root 运行时标题栏显示「Administrator:」或「(Root Session)」。 | 普通用户无提示 | 提权运行时注意文件权限副作用 |
| MGMT-033 | 最近使用文件列表 | MRU lists | 记录最近使用的文件、文件夹、FTP 站点等列表。 | 自动 | 存于 BCState.xml |
| MGMT-034 | 窗体位置记忆 | Form position persistence | 记住窗口/对话框位置与尺寸。 | 自动 | 显示器配置变化后可能落在屏幕外 |
| MGMT-035 | 会话之间的设置继承 | Session setting inheritance | 新会话继承该类型的会话默认值。 | 继承默认值 | 命令行加载的命名会话用其自身设置 |
| MGMT-036 | 会话中的相对路径 | Relative paths in sessions | 会话可保存相对路径以便跨机器复用。 | 绝对路径 | 相对路径基于会话文件位置解析 |
| MGMT-037 | 会话中保存的密码 | Saved passwords | 远程配置中的密码可保存，受 Admin Policy 控制。 | 可保存（5.2 起加密方式变更） | 降级安装时已保存密码视为空 |
| MGMT-038 | 会话启动参数替换 | Session + command-line override | 命令行可以覆盖会话的基准路径、过滤器、标题等。 | — | 覆盖项不写回会话 |
| MGMT-039 | 多会话并行 | Multiple concurrent sessions | 同一进程内多个标签同时运行比较任务。 | 支持 | 大文件夹并行扫描会争抢 IO |
| MGMT-040 | 会话树排序 | Session tree sort | 会话与分组按名称排序展示。 | 字母序 | — |
| MGMT-041 | 会话视图切换（树/列表） | Sessions view modes | 在 Home 视图切换会话呈现方式。 | 树 | — |
| MGMT-042 | 会话设置的免保存试探 | Trial / non-persistent settings | 未保存的会话设置仅作用于本次运行。 | 临时生效 | 与「Update session defaults」作用域互斥 |

---

## 3. 工作区（Workspaces）

> 工作区 = 一组「窗口 + 标签」的命名快照，用于一键恢复整套比较工作环境。

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| WS-001 | 保存工作区为 | Session > Save Workspace As | 把当前所有窗口与标签的配置保存为命名工作区。 | — | 工作区记录的是会话引用而非会话内容 |
| WS-002 | 加载工作区 | Session > Load Workspace | 恢复命名工作区的窗口/标签布局。 | — | 加载前会关闭当前所有视图 |
| WS-003 | 工作区管理器 | Manage Workspaces | 重命名或删除已保存的工作区。 | — | — |
| WS-004 | 工作区命令行打开 | Named Workspace parameter | `BCompare.exe "My Workspace"` 直接加载工作区。 | — | 与会话同处一个命名空间，可能重名 |
| WS-005 | 工作区含文件视图 | Workspace with file views | 工作区可包含文件比较、文件夹比较等任意视图类型。 | — | 数据源不可用时加载失败 |
| WS-006 | 工作区含远程路径 | Workspace with remote paths | 工作区可保存 FTP/云路径。 | — | 需凭据可用才能恢复 |
| WS-007 | 工作区恢复顺序 | Restore order | 按保存顺序恢复标签。 | 保存顺序 | — |
| WS-008 | 工作区与会话的差异 | Workspace vs Session | 会话 = 单个比较任务；工作区 = 多个视图的集合。 | — | 混用会导致「加载后视图不是我想要的」 |
| WS-009 | 工作区列表菜单 | Load Workspace submenu | 在会话菜单中列出全部已保存工作区。 | 列表 | — |
| WS-010 | 工作区删除确认 | Workspace delete confirm | 删除工作区前弹出确认。 | 确认 | — |
| WS-011 | 工作区与选项卡布局 | Workspace tab layout | 记录每个标签的类型与 Specs，而非像素布局。 | — | 窗口尺寸不随工作区恢复 |

---

## 4. 文本比对核心：规则、重要性与对齐算法

### 4.1 对齐算法（Alignment）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXT-001 | 标准对齐方法 | Standard alignment method | 通过比较每个文件的相邻小片段来对齐，可在比较完成前显示部分结果。 | **默认方法** | 大段插入时需调大偏斜容差 |
| TXT-002 | 偏斜容差 | Skew tolerance | 控制算法如何寻找匹配的间距；文件有大量或大段插入时调大更佳。 | 中等 | 过大易产生错误对齐 |
| TXT-003 | 使用最近匹配 | Use closeness matching | 基于相似度对齐剩余的不匹配行。 | 开启 | 可能把语义上不同的行配在一起 |
| TXT-004 | 替补对齐方法（LCS） | Replacement / LCS alignment | 使用最长公共子序列算法，对重复文本多的文件匹配更佳。 | 未选中 | 需整文件读完后才显示；**不支持相似度匹配** |
| TXT-005 | 不对齐 | Unaligned | 不按内容对齐行，左右严格按行号对应。 | 未选中 | 仅适合行一一对应的数据 |
| TXT-006 | 从不对齐差异项 | Never align differences | 有重要差异的行显示为「新增/删除的整段」而非「修改」。 | 未选中 | 影响差异类型判定与报表语义 |
| TXT-007 | 手工对齐 | Align With… | 选中一行/多行后强制与另一侧选中行并排对齐。 | 右键菜单 | 手工对齐后重新比较会丢失 |
| TXT-008 | 手工对齐多行选择 | Align multiline selections | BC5 新增：手工对齐支持多行选区。 | 支持 | 行数不等时报错 |
| TXT-009 | 隔离 | Isolate | 重新对齐比较，使选中的行被独立显示。 | — | 与 Align With 相反方向的操作 |
| TXT-010 | 对齐细节面板 | Alignment Details | 视图底部显示可编辑的字符级对齐视图。 | 默认关闭 | 与 Text Details / Hex Details 三选一布局 |
| TXT-011 | 行重（Line Weights） | Line weights | File Format 中定义行重，对齐算法优先排列权重更高的匹配行。 | 由文件格式定义 | 配置不当反而降低对齐质量 |
| TXT-012 | 对齐结果不可预期时的补救 | Manual realignment | 算法产生非期望结果时用右键 Align With 手工修正。 | 需人工介入 | 无「一键重排」 |

### 4.2 重要性规则（Importance）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXT-013 | 语法元素重要性 | Importance: Syntax Elements | 在列表中勾选哪些语法元素（注释、字符串、关键字等）对比较重要。 | 依 File Format 语法定义 | 元素类型由语法定义，会话只决定重要性 |
| TXT-014 | 编辑语法入口 | Edit Grammar button | 从重要性页直接跳转编辑选中文件格式的语法定义。 | — | 修改的是全局 File Format，不是会话 |
| TXT-015 | 默认文本重要性 | Importance: Default text | 对未被语法定义的普通文本定义重要性规则。 | 全部重要 | 与语法元素规则叠加 |
| TXT-016 | 前导空白重要性 | Leading whitespace | 行首空格/Tab 是否重要。 | 不重要 | 缩进风格差异常被忽略 |
| TXT-017 | 嵌入空白重要性 | Embedded whitespace | 行中间空白是否重要。 | 不重要 | —— |
| TXT-018 | 尾随空白重要性 | Trailing whitespace | 行尾空白是否重要。 | 不重要 | 与「Trim Trailing Whitespace」转换配合 |
| TXT-019 | 其它文本重要性 | Other text | 非空白的普通文本是否重要（即是否整行重要）。 | 重要 | —— |
| TXT-020 | 字符大小写敏感 | Character case | 「其它文本」是否区分大小写。 | 不区分（大小写差异不重要） | 语法元素的字母大小写由语法控制，不受此项影响 |
| TXT-021 | 孤立行重要 | Orphan lines are important | 纯空白行或只含不重要文本的插入行是否算重要差异。 | 不算重要 | 勾选后大量「空行差异」会变成红色 |
| TXT-022 | 比较行尾（PC/Mac/Unix） | Compare line endings (PC/Mac/Unix) | 逐行比对 CR/LF 与 LF 的差异。 | **关闭**（通常忽略行尾风格差异） | 需配合 View > Visible Whitespace 才看得见 |
| TXT-023 | 重要差异着色 | Important difference coloring | 重要差异以红色文字 + 浅红行背景标示。 | 红色 | 浅红背景优先级高于浅蓝 |
| TXT-024 | 不重要差异着色 | Unimportant difference coloring | 不重要差异以蓝色文字 + 浅蓝行背景标示。 | 蓝色 | 开启 Ignore Unimportant 后不再高亮 |
| TXT-025 | 忽略不重要差异 | Ignore Unimportant Differences | 把所有不重要差异当作相同处理。 | 关闭 | 文件夹层的「内容规则化比对」会继承该语义 |
| TXT-026 | 忽略（局部） | Ignore / Unignore | 对选中行或当前段抑制/恢复差异显示。 | 未忽略 | 局部忽略不改变文件内容，仅影响判定 |
| TXT-027 | 重要性分层来源 | Rules vs File Formats layering | 会话设置管「抽象重要性」，File Format 管「语法细节」。 | 分层 | 两层都改容易互相覆盖 |
| TXT-028 | 单元格级颜色细化 | Text background color | 除整行背景外，可再对差异文本片段着色。 | 浅色 | —— |

### 4.3 文本格式与编码（Format & Encoding）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXT-029 | 左右侧文件格式指定 | Left/Right File Format | 显式指定左右侧使用的 File Format，而非自动检测。 | 默认（按掩码自动检测） | 左右可用不同格式，属性文件对比场景常用 |
| TXT-030 | 编码覆盖 | Encoding Override | 覆盖该会话的字符编码设置。 | 无（使用文件格式编码） | 设错会让整个文件全红 |
| TXT-031 | 编码自动检测 | Encoding auto-detection | 自动识别文件编码。 | 开启；BC5 起扫描**整个文件**而非前 64KB | 大文件检测开销上升 |
| TXT-032 | 默认编码假设 | Default encoding assumption | Windows 下文件视图默认假定 UTF-8（除非检测到扩展字符）。 | UTF-8（BC5 起，BC4 为 ANSI） | 升级后旧 ANSI 文件可能显示异常 |
| TXT-033 | BOM 处理 | Byte Order Mark handling | 识别并处理 UTF-8/UTF-16 BOM。 | 自动识别 | 仅含 BOM 的空文件保存时报「文件被占用」曾出现（5.0.1 修复） |
| TXT-034 | 混合编码处理 | Mixed encoding | 同一比较中左右两侧可用不同编码。 | 支持 | 需显式在 Format 页指定 |
| TXT-035 | 行尾风格 | Line ending style | DOS(CR+LF) / Unix(LF) / Mac(CR)。 | 由文件内容决定 | 「比较行尾」选项默认关闭 |
| TXT-036 | 行尾转换 | Transform File > Compare Line Endings | 批量转换整个文件的行尾风格。 | — | 会直接修改文件内容 |
| TXT-037 | 每行字符限制 | Max characters per line | 超过指定长度自动断行显示。 | 关闭 | 保存时会清除人工断行 |
| TXT-038 | Ctrl+Z 作为 EOF | Ctrl+Z marks EOF | 把十六进制 1A 当作文件结尾标记。 | 关闭 | DOS 时代遗留文本需要开启 |
| TXT-039 | 制表位设置 | Tab stops | 定义 Tab 停止间隔，影响显示与缩进换算。 | 4（依文件格式） | 与「前导空格转 Tab」联动 |
| TXT-040 | 插入空格而非 Tab | Insert spaces instead of tabs | 按 Tab 键插入空格而非 Tab 字符。 | 关闭（插入 Tab 字符） | —— |
| TXT-041 | 行是独立记录 | Lines are independent | 每行是独立记录，连续差异行作为一个段/节处理。 | 关闭 | 影响 Next/Prev Section 的段划分 |
| TXT-042 | 基于列的数据 | Column-based data | 每行字符位置重要，可逐列比较行。 | 关闭 | BC 对「列模式/块选择」的支持即源于此设置，并非通用块选择编辑 |
| TXT-043 | 剪裁尾随空格（保存时） | Trim trailing whitespace on save | 保存文件前自动去掉行尾空格与 Tab。 | 关闭 | 静默修改内容 |
| TXT-044 | 前导空格转 Tab（保存时） | Leading spaces to tabs on save | 保存前把行首空格按制表位换算为 Tab。 | 关闭 | 换算依赖制表位设置 |
| TXT-045 | HTML 网页内容比较 | Compare webpage content | 直接把 URL 作为文件 Spec 比对网页文本内容。 | 支持 | 动态页面结果依赖抓取时刻 |
| TXT-046 | 剪贴板作为一侧 | Open Clipboard | 以剪贴板内容作为某一侧参与比较。 | — | 无编码提示，依赖自动检测 |
| TXT-047 | 比较选中内容与剪贴板 | Compare Selection to Clipboard | 用当前选中文本与剪贴板内容新建比较视图。 | — | 临时视图不会保存 |

### 4.4 文本差异导航所需的数据模型

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXT-048 | 差异（Difference） | Difference | 一对不对应的行区间构成一个差异项。 | — | 与「段（Section）」不是同一概念 |
| TXT-049 | 差异段（Section） | Difference Section | 一组相邻的差异行构成的段。 | — | 导航粒度比差异项粗 |
| TXT-050 | 编辑行 | Edit line | 用户手工编辑过的行，可单独导航。 | — | Reload/Recompare 会清除编辑标记 |
| TXT-051 | 替换项（Replacement） | Replacement match | Replacements 规则命中的差异，可单独导航。 | 需 Pro | —— |
| TXT-052 | 差异类型 | Difference kind | 新增 / 删除 / 修改 / 不重要差异。 | — | 受「从不对齐差异项」影响 |
| TXT-053 | 行状态缓存 | Line status cache | 每行的比较状态用于缩略图与背景着色。 | 内存缓存 | 超大文件内存占用显著 |
| TXT-054 | 差异汇总统计 | Comparison statistics | 统计差异行数、差异项数量、相似度百分比等。 | 由 Compare Info 提供 | 相似度参与文件夹会话的「内容相似」判定 |
| TXT-055 | 相似度阈值 | Similarity threshold | 用于判断文件内容「相似」（非相同）的阈值。 | 内置阈值 | 影响 `/qc` 返回码 12（Similar） |
| TXT-056 | 规则化比较的判定链 | Rules-based comparison chain | 依次应用 File Format 转换 → 语法 → 重要性 → 忽略项。 | — | 任一环节配置错误都会误判 |
| TXT-057 | 忽略注释 | Ignore comments | 通过语法定义把注释标记为不重要，从而忽略注释差异。 | 依语法 | 需要语法能正确识别注释边界 |
| TXT-058 | 忽略行号 | Ignore line numbers | 通过替换规则忽略行号变化（如 `^\s*\d+:` → 空）。 | 需自定义规则 | 规则过宽会掩蔽真实差异 |
| TXT-059 | 忽略大小写 | Ignore character case | 通过 Importance 的 Character case 实现。 | 忽略 | 与「匹配字符大小写」的 Replacements 选项不同 |
| TXT-060 | 忽略空白差异 | Ignore whitespace differences | 前导/嵌入/尾随空白均设为不重要。 | 默认已忽略 | —— |
| TXT-061 | 忽略换行符差异 | Ignore line-ending differences | 不把 CR/LF 差异视为差异。 | 默认忽略 | 勾选 Compare Line Endings 可反转 |
| TXT-062 | 忽略不重要差异的传播 | Ignore-unimportant propagation | 文本子会话的忽略设置可传播到父文件夹会话的内容比对。 | 需显式选择作用域 | 作用域选错会导致文件夹层判定不符预期 |

---

## 5. 文本比对 UI 与显示

### 5.1 布局

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTUI-001 | 并排布局 | Side-by-side Layout | 左右两栏编辑器并排，同步滚动。 | **默认布局** | 长行需横向滚动（除非开启自动换行） |
| TXTUI-002 | 上下布局 | Over-under Layout | 两个编辑器上下排列。 | 未选中 | 适合窄屏 |
| TXTUI-003 | 自动换行 | Word Wrap | 按显示宽度折行显示，无需横向滚动。 | BC5 新增，默认关闭 | 与 Increase Ident 同时使用曾崩溃（已修） |
| TXTUI-004 | 空白字符可见 | Visible Whitespace | 显示空格与 Tab 的可视标记。 | 关闭 | 检查行尾差异的前提 |
| TXTUI-005 | 行号 | Line Numbers | 显示/隐藏行号栏。 | 关闭 | —— |
| TXTUI-006 | 语法高亮 | Syntax Highlighting | 按 File Format 语法着色。 | 开启 | 大文件高性能开销 |
| TXTUI-007 | 显示字体选择 | Display Font | 选择编辑器使用的字体。 | 系统等宽字体 | 见 LOOK 章 |
| TXTUI-008 | 增大显示字体 | Increase Display Font Size | 快捷键 `Ctrl+/⌘+` 放大编辑器字体。 | 默认快捷键 | BC5 新增快捷键绑定 |
| TXTUI-009 | 减小显示字体 | Decrease Display Font Size | 快捷键 `Ctrl-/⌘-` 缩小编辑器字体。 | 默认快捷键 | —— |
| TXTUI-010 | 重置显示字体 | Reset Display Font Size | 恢复默认字号。 | — | —— |
| TXTUI-011 | 文件信息面板 | File Info panel | 编辑器顶部显示文件名、编码、行尾等文件信息。 | 可开关 | —— |
| TXTUI-012 | 工具栏显隐 | Toolbar show/hide | 显示或隐藏工具栏。 | 显示 | —— |
| TXTUI-013 | 缩略图 | Thumbnail | 左侧一像素一行的差异总览条，可点击定位。 | 开启 | 白框代表当前视区，小三角代表当前行 |
| TXTUI-014 | 文本细节面板 | Text Details | 底部以整窗宽度显示两侧当前行文本，可编辑。 | 可开关 | 三选一（Text/Hex/Alignment） |
| TXTUI-015 | 十六进制细节面板 | Hex Details | 底部以只读十六进制显示当前行。 | 关闭 | 编码问题时定位利器 |
| TXTUI-016 | 规则标尺 | Ruler | 显示行细节规则的标尺。 | 关闭 | 与语法元素的边界标记相关 |
| TXTUI-017 | 网页面板 | Webpage pane | 以浏览器形式渲染当前文件。 | 关闭 | HTML 文件的辅助显示 |
| TXTUI-018 | 滚动同步 | Synchronized scrolling | 两个编辑器窗格同步滚动。 | 开启 | 对齐错乱时同步滚动会放大困惑 |
| TXTUI-019 | 鼠标滚轮作用于光标所在控件 | Mouse wheel under cursor | 滚轮滚动鼠标下的控件而非焦点控件。 | BC5 行为 | BC4 行为不同 |
| TXTUI-020 | 差异行背景着色 | Line background coloring | 整行浅红/浅蓝背景标示差异类型。 | 开启 | 横向滚动出屏也能看到 |
| TXTUI-021 | 差异文字着色 | Difference text coloring | 红色/蓝色文字标示重要/不重要差异。 | 开启 | 受 Selection 颜色影响 |
| TXTUI-022 | 条纹背景 | Use stripes | 隔行着色以提升可读性。 | 关闭 | 与差异背景叠加 |
| TXTUI-023 | 选区颜色 | Selection color | 默认使用中蓝色以确保差异着色不被遮蔽。 | 中蓝 | 可切换为系统高亮（会丢失差异/语法着色） |
| TXTUI-024 | 当前差异项高亮 | Current difference highlight | 当前差异项以更醒目的边框/背景标示。 | 有色 | —— |
| TXTUI-025 | 当前行指示 | Current line indicator | 缩略图中的小三角指向当前行。 | 开启 | —— |

### 5.2 显示过滤器（View > Display Filters）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTUI-026 | 显示所有 | Show All | 显示所有行，无论有无差异。 | — | —— |
| TXTUI-027 | 显示差异项 | Show Differences | 只显示有差异的行。 | — | 上下文行由 Show Context 控制 |
| TXTUI-028 | 显示相同项 | Show Same | 只显示没有差异的行。 | — | —— |
| TXTUI-029 | 不显示 | Show None | 隐藏所有行。 | 隐藏 | 常与文件夹会话的「不显示」配合重建目录结构 |
| TXTUI-030 | 显示上下文 | Show Context | 显示差异周围的上下文行。 | 3 行（可在 Options > File Views > Text 配置） | 上下文行数改错会以为「差异变少了」 |
| TXTUI-031 | 忽略不重要差异项 | Ignore Unimportant Differences | 视图中把不重要差异当作相同。 | 关闭 | 与重要性设置联动 |
| TXTUI-032 | 显示过滤器预设 | Display filter presets | 自定义命令中可把常用过滤器组合成工具栏下拉预设。 | 默认下拉 | 预设仅在支持的会话类型中出现 |

### 5.3 搜索与导航

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTUI-033 | 下一个差异项 | Next Difference | 定位到下一处差异文本。 | 快捷键 `Alt+↓`（可改） | BC5 修复了跳过缺失行的缺陷 |
| TXTUI-034 | 上一个差异项 | Previous Difference | 定位到上一处差异文本。 | 快捷键 | —— |
| TXTUI-035 | 下一个差异段 | Next Difference Section | 定位到下一个差异段（相邻差异行的集合）。 | — | 与差异项粒度不同 |
| TXTUI-036 | 上一个差异段 | Previous Difference Section | 定位到上一个差异段。 | — | —— |
| TXTUI-037 | 下一个替换 | Next Replacement `[Pro]` | 定位到下一个 Replacements 规则命中的差异。 | — | 需存在替换规则 |
| TXTUI-038 | 上一个替换 | Previous Replacement `[Pro]` | 定位到上一个替换命中。 | — | —— |
| TXTUI-039 | 下一个编辑项 | Next Edit | 定位到光标之后的下一个编辑行。 | — | 编辑标记在重新比较后清除 |
| TXTUI-040 | 上一个编辑项 | Previous Edit | 定位到光标之前的上一个编辑行。 | — | —— |
| TXTUI-041 | 查找 | Find | 搜索匹配文本。 | 快捷键 `Ctrl+F` | —— |
| TXTUI-042 | 查找下一个 / 上一个 | Find Next / Find Previous | 循环查找匹配项。 | 快捷键 `F3` / `Shift+F3` | 全部选中两次时曾崩溃（已修） |
| TXTUI-043 | 替换 | Replace | 替换匹配文本（可正则）。 | — | PCRE 语法 |
| TXTUI-044 | 转到 | Go To | 定位到指定行或行列位置。 | — | 列号在「基于列的数据」模式下更有意义 |
| TXTUI-045 | 切换书签 | Toggle Bookmark | 在当前行放置 0–9 编号书签。 | — | 同时只有一个固定数字槽 |
| TXTUI-046 | 转到书签 | Go To Bookmark | 跳转到指定编号书签。 | — | —— |
| TXTUI-047 | 清除书签 | Clear Bookmarks | 删除比较中所有书签。 | — | —— |
| TXTUI-048 | 匹配项高亮 | Search highlight | 查找结果在视图中高亮显示。 | 开启 | 可用 Esc 清除 |

### 5.4 剪贴板与复制文本

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTUI-049 | 复制到剪贴板（纯文本） | Copy as text | 把选中内容以纯文本复制。 | — | —— |
| TXTUI-050 | 复制为 HTML | Copy as HTML | 以带差异着色的 HTML 复制到剪贴板。 | — | 粘贴到不支持 HTML 的目标会退化为纯文本 |
| TXTUI-051 | 复制差异为 diff 文本 | Copy diff as unified text | 以统一 diff 形式复制差异。 | — | 见报表布局 Patch |
| TXTUI-052 | 复制整行 | Copy line | 复制当前行。 | — | —— |

---

## 6. 文本编辑与文件操作

### 6.1 文件级命令（File 菜单）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTED-001 | 打开文件 | File > Open File | 在选定窗格中打开现有文件。 | — | 会替换当前一侧内容 |
| TXTED-002 | 打开本地文件 | Open Local File | 限定在本地文件系统中选择。 | — | 与「打开文件」的区别是显式限定本地 |
| TXTED-003 | 打开 FTP 文件 | Open FTP File | 从 FTP/远程配置中选择文件。 | — | 需已配置 Profiles |
| TXTED-004 | 打开剪贴板 | Open Clipboard | 用剪贴板内容替换选定窗格。 | — | 无文件名信息 |
| TXTED-005 | 用文本编辑打开 | Open With > Text Edit | 用内置 Text Edit 视图打开选定窗格的文件。 | — | 丢失比对上下文 |
| TXTED-006 | 用关联程序打开 | Open With > Associated Application | 用系统关联程序打开文件。 | — | 外部修改后需手动 Reload |
| TXTED-007 | 保存文件 | File > Save File | 保存选定窗格中修改过的文件。 | — | 受 `/savetarget=` 影响可重定向目标 |
| TXTED-008 | 另存文件为 | File > Save File As | 以新文件名保存选定窗格文件。 | — | —— |
| TXTED-009 | 另存至文件系统 | Save File to File System | 把远程/压缩包内文件另存到本地。 | — | —— |
| TXTED-010 | 另存至 FTP 站点 | Save File to FTP Site | 把文件另存到远程站点。 | — | 需凭据 |
| TXTED-011 | 在资源管理器中显示 | Explorer / Reveal in Finder | 打开系统文件管理器并定位到文件。 | — | 仅本地路径可用 |
| TXTED-012 | 重新加载文件 | Session > Reload Files | 重新从磁盘读取，若需保存会先提示。 | — | 丢弃未保存编辑 |
| TXTED-013 | 重新比较文件 | Session > Recompare Files | 保留当前编辑重新运行比较，仍可还原。 | — | 与 Reload 的区别是不重读磁盘 |
| TXTED-014 | 交换两侧 | Session > Swap Sides | 对调左右文件。 | — | 编辑状态如何处理需注意 |
| TXTED-015 | 用于比较/合并的入口 | Compare Using / Merge Files | 以另一视图或合并视图打开当前文件对。 | Merge 需 Pro | —— |
| TXTED-016 | 备份文件生成 | Backup files | 保存时是否生成 `.bak` 备份。 | 依 Options > Backup | `/nobackups` 可禁用 |
| TXTED-017 | 保存前确认 | Confirm before save | 保存未保存修改时是否二次确认。 | 依 Options | —— |
| TXTED-018 | 只读提示 | Read-only side indicator | 某一侧被设为只读时提示不可编辑。 | — | 由 `/ro1` / `/ro2` 设置 |

### 6.2 编辑命令（Edit 菜单）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTED-019 | 撤销 / 恢复 | Undo / Redo | 标准撤销与重做。 | 多级 | 跨会话不保留 |
| TXTED-020 | 完整编辑模式 | Full Edit | 切换字符级完整编辑（开）与 BC2 风格行级编辑（关）。 | 开启 | 关闭后只能整行操作 |
| TXTED-021 | 剪切 / 复制 / 粘贴 / 删除 | Cut / Copy / Paste / Delete | 标准编辑器命令。 | — | 跨窗格粘贴不改变对齐 |
| TXTED-022 | 插入行（前 / 后） | Insert Line Before/After | 在当前行之前/之后插入空白行。 | — | 会制造新差异 |
| TXTED-023 | 删除行 | Delete Line | 删除当前行。 | — | —— |
| TXTED-024 | 删除到行首 / 行尾 | Delete to Beginning/End | 删除光标至行首/行尾的文本。 | — | —— |
| TXTED-025 | 删除单词 | Delete Word | 删除光标处的单词。 | — | —— |
| TXTED-026 | 删除到词首 / 词尾 | Delete to Word Start/End | 删除光标至单词起始/结尾。 | — | —— |
| TXTED-027 | 增加 / 减小缩进 | Increase / Decrease Indent | 调整选中行的缩进。 | — | BC5 修复了与 Word Wrap 同用时的崩溃 |
| TXTED-028 | 选中所有 | Select All | 选中当前窗格所有可见行。 | — | 「可见行」受显示过滤器影响 |
| TXTED-029 | 选择段 | Select Section | 选中当前差异段内的所有行。 | — | —— |
| TXTED-030 | 隔离 | Isolate | 使选中行在对齐中独立。 | — | —— |
| TXTED-031 | 和对齐 | Align With | 把选中行与另一侧选中行强制对齐。 | — | 需两侧都有选中 |

### 6.3 内容搬运（Copy 系列）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTED-032 | 复制到右侧 / 左侧 | Copy to Right / Copy to Left | 把当前段或选中行复制到指定侧。 | 默认隐藏命令 | 会直接修改目标文件内容 |
| TXTED-033 | 复制到另一侧 | Copy to Other Side | 依据当前侧自动决定复制方向。 | 动态标题 | —— |
| TXTED-034 | 复制行到右侧 / 左侧 | Copy Line to Right / Left | 只复制当前行。 | 默认隐藏 | —— |
| TXTED-035 | 复制行至另一侧 | Copy Line to Other Side | 动态方向的单行复制。 | 动态标题 | —— |
| TXTED-036 | 沟槽按钮 | Gutter buttons | 编辑器左右沟槽中的箭头按钮，一键把该段搬向另一侧。 | 开启 | 大段误点会批量覆盖 |
| TXTED-037 | 复制后自动前进 | Auto-advance after copy | 复制完成后自动跳到下一个差异。 | 可配置 | —— |

### 6.4 文本转换（Transform File）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTED-038 | 剪裁尾随空格 | Trim Trailing Whitespace | 删除行尾空格与 Tab。 | — | 直接改文件 |
| TXTED-039 | 前导空格转 Tab | Leading Spaces to Tabs | 按制表位把行首空格换成 Tab。 | — | 依赖制表位设置 |
| TXTED-040 | Tab 转空格 | Tabs to Spaces | 把行首 Tab 换成等宽空格。 | — | 同上 |
| TXTED-041 | 转换行尾 | Compare Line Endings | 把全文行尾统一为指定风格（DOS/Unix/Mac）。 | — | 直接改文件 |
| TXTED-042 | 转换的撤销 | Undo transform | 转换后可撤销。 | 支持 | 保存后不可撤销 |

### 6.5 文件操作与外部交互

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TXTED-043 | 检入 / 检出 | Check In / Check Out | 对受源代码控制的文件执行检入检出。 | 需 Pro + Win + 配置 | 未配置 VCS 时不显示 |
| TXTED-044 | 撤销检出 | Undo Check Out | 撤销本地改动并恢复只读。 | 同上 | —— |
| TXTED-045 | 比较父文件夹 | Compare Parent Folder | 从文件比对跳到其所在目录的文件夹比对。 | — | —— |
| TXTED-046 | 文本比较报表 | Text Compare Report | 生成文本比较报告。 | — | 见 REP 章 |
| TXTED-047 | 文本比较信息 | Text Compare Info | 显示统计信息。 | — | —— |
| TXTED-048 | 在资源管理器显示 | Explorer | 打开系统文件管理器上下文菜单。 | 仅 Windows | —— |
| TXTED-049 | 文件被外部修改 | External modification detection | 文件被外部程序改动后文件夹视图不自动更新。 | 需手动刷新 | 容易覆盖他人修改 |
| TXTED-050 | 文件锁 / 占用提示 | File in use error | 保存被其他进程占用的文件时报错。 | 报错 | 曾对「仅含 BOM」的文件误报（5.0.1 修复） |
| TXTED-051 | 保存时行尾规范化 | Save line-ending normalization | 保存时按文件格式的行尾设置写出。 | 依文件格式 | 可能造成整文件 diff |
| TXTED-052 | 大文件编辑性能 | Large file editing | 超大文本文件的编辑与渲染策略。 | — | 内存映射相关见 FS 章 |
| TXTED-053 | 多级撤销栈 | Undo stack depth | 撤销栈深度上限。 | 有限 | —— |
| TXTED-054 | 编辑标记与导航联动 | Edit markers | 编辑过的行可在搜索菜单中逐个导航。 | — | —— |
| TXTED-055 | 保存时同步父会话 | Save sync to parent | 在子会话中保存后父文件夹会话的状态更新。 | 需刷新 | 不自动刷新曾导致状态陈旧 |
| TXTED-056 | 只读侧的保护 | Read-only enforcement | 只读侧禁止任何编辑与复制入。 | — | —— |
| TXTED-057 | 剪贴板比较 | Compare Selection to Clipboard | 选中文本与剪贴板内容新建比较。 | — | —— |
| TXTED-058 | 打开压缩包内文件 | Open file inside archive | 直接以 `C:\Arc.zip\inner.txt` 形式打开。 | 支持 | 保存时回写压缩包 |
| TXTED-059 | 打开远程文件 | Open remote file | 以 `ftp://user@host/file.txt` 形式作为 Spec。 | 支持 | —— |
| TXTED-060 | 保存到原始压缩包 | Save back into archive | 保存编辑结果回写进压缩包。 | 支持 | 压缩包需可写 |
| TXTED-061 | 文件比较的 Specs 编辑 | Edit specs inline | 直接在路径编辑框中输入/修改文件 Spec。 | — | 路径含空格时需引号 |

---

## 7. 文本合并 / 三路合并（Text Merge）`[Pro]`

### 7.1 会话结构

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MRG-001 | 三路源窗格 | Left / Right / Center source panes | 上方为左右两个版本 + 可选中部的共同祖先，均**不可编辑**。 | 三个源窗格只读 | 源窗格禁止输入，只能通过 Take 箭头操作 |
| MRG-002 | 输出窗格 | Output pane | 下方可编辑的合并结果窗格，自动构建初值。 | 自动生成 | **已有输出文件内容会被忽略并覆盖**，保存即丢失 |
| MRG-003 | 中间窗格显隐 | Show Center Pane | 临时隐藏中间窗格以给左右更多空间。 | 显示 | 隐藏后冲突判定仍依据中部内容 |
| MRG-004 | 分离输出窗格 | Detached Output Pane | 把输出窗格移到独立窗口（可放第二显示器）。 | 关闭 | 多显示器缩放不一致时图片尺寸曾出错（已修） |
| MRG-005 | 自动换行 | Word Wrap | 合并视图支持自动换行。 | 关闭 | BC5 新增 |
| MRG-006 | 独立于文本比较的设置 | Separate merge settings | Text Merge 有独立的 Session Settings（Specs/Format/Importance/Alignment）。 | 独立 | 无 Replacements 页 |

### 7.2 合并决策命令

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MRG-007 | 冲突标记切换 | Conflict | 为选中行或当前段设置/清除冲突标记。 | 自动判定 | 手工改动后自动判定不再可靠 |
| MRG-008 | 选用左侧 | Take Left | 从左侧取选中行或当前段到输出。 | — | 覆盖已有输出内容 |
| MRG-009 | 选用中间 | Take Center | 从中间（祖先）取内容到输出。 | — | —— |
| MRG-010 | 选用右侧 | Take Right | 从右侧取内容到输出。 | — | —— |
| MRG-011 | 先左后右 | Take Left Then Right | 按左→右顺序选用当前段中选中的行。 | — | 用于两侧都有新增的行 |
| MRG-012 | 先右后左 | Take Right Then Left | 按右→左顺序选用。 | — | —— |
| MRG-013 | 选用左侧行 | Take Left Line | 只取左侧的当前行。 | — | 粒度比 Take Left 细 |
| MRG-014 | 选用中间行 | Take Center Line | 只取中间的当前行。 | — | —— |
| MRG-015 | 选用右侧行 | Take Right Line | 只取右侧的当前行。 | — | —— |
| MRG-016 | 一键接受全部非冲突 | Take all non-conflicting | 自动把所有非冲突变更应用到输出，只留冲突人工处理。 | 可脚本化（`/automerge`） | 容易误以为「已全部合并」 |
| MRG-017 | 沟槽箭头预测 | Gutter prediction arrows | 沟槽箭头预示自动合并会把哪一侧内容纳入输出。 | 显示 | 冲突处无箭头，需人工选边 |
| MRG-018 | 冲突自动检测 | Automatic conflict detection | 左右两侧对同一祖先区域都有改动即判为冲突。 | 自动 | 判定规则较保守，可能报「伪冲突」 |
| MRG-019 | 自动合并 | /automerge | 无冲突时无人工交互完成合并。 | 需显式指定 | 发现冲突时仍会停下 |
| MRG-020 | 强制写冲突标记 | /force | 配合 `/automerge` 时把冲突以 CSV 风格标记写入输出。 | 关闭 | 输出文件会含标记文本 |
| MRG-021 | 忽略不重要差异 | /ignoreunimportant | 自动合并时忽略不重要差异。 | 关闭 | 依赖 Importance 设置 |
| MRG-022 | 偏好侧设置 | /favorleft /favorright | 输出的非冲突变更不加色/不加分隔线；被忽略的不重要冲突自动取偏好侧。 | 无偏好 | 会掩盖「确实是差异」的事实 |
| MRG-023 | 合并输出指定 | /mergeoutput=<file\|path> | 显式指定合并输出文件或目录。 | 由第 4 个参数决定 | 目录型输出用于文件夹合并 |
| MRG-024 | 合并中心指定 | /center=<file> | 显式指定合并的祖先文件。 | 由第 3 个参数决定 | 参数顺序错会把手/脚当祖先 |
| MRG-025 | 冲突复核 | /reviewconflicts | 配合 `/automerge`，发现冲突时打开 Text Merge 视图。 | 关闭 | —— |

### 7.3 合并视图的显示过滤器

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MRG-026 | 显示所有 | Show All | 显示未修改、相同修改、单侧修改、差异修改与冲突行。 | — | —— |
| MRG-027 | 显示修改项 | Show Changes | 显示所有类型的修改与冲突行。 | — | —— |
| MRG-028 | 显示冲突项 | Show Conflicts | 只显示冲突行。 | — | 合并工作最常用的过滤 |
| MRG-029 | 显示左侧修改项 | Show Left Changes | 显示相同修改、左侧修改、差异修改与冲突行。 | — | —— |
| MRG-030 | 显示右侧修改项 | Show Right Changes | 显示相同修改、右侧修改、差异修改与冲突行。 | — | —— |
| MRG-031 | 显示可合并项 | Show Mergeable | 显示相同修改、单侧修改与差异修改行。 | — | —— |
| MRG-032 | 显示未修改项 | Show Unchanged | 只显示未修改行。 | — | —— |
| MRG-033 | 不显示 | Show None | 隐藏所有行。 | 隐藏 | —— |
| MRG-034 | 显示上下文 | Show Context | 显示差异周围的上下文行。 | 3 行 | —— |
| MRG-035 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要差异视为相同。 | 关闭 | —— |
| MRG-036 | 忽略相同变化 | Ignore Same Changes | 把左右两侧做出的相同改动视为相同。 | 关闭 | 与冲突判定相关 |
| MRG-037 | 偏好左侧修改 | Favor Left Changes | 输出中不额外高亮仅左侧的修改。 | 关闭 | 视觉压制，非内容变更 |
| MRG-038 | 偏好右侧修改 | Favor Right Changes | 输出中不额外高亮仅右侧的修改。 | 关闭 | —— |
| MRG-039 | 忽略（局部） | Ignore / Unignore | 对选中行或段忽略差异。 | 关闭 | —— |

### 7.4 合并视图导航与输出比对

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MRG-040 | 清除冲突段并前进 | Clear Conflict Section, Next | 清除当前段冲突标记并跳到下一个冲突段。 | — | 合并流水线的核心快捷键 |
| MRG-041 | 下一个 / 上一个冲突段 | Next / Previous Conflict Section | 仅在冲突段间跳转。 | — | —— |
| MRG-042 | 下一个 / 上一个差异项 | Next / Previous Difference | 按差异项粒度跳转。 | — | —— |
| MRG-043 | 下一个 / 上一个差异段 | Next / Previous Difference Section | 按差异段粒度跳转。 | — | —— |
| MRG-044 | 下一个左侧选用 / 右侧选用 | Next Left Take / Next Right Take | 定位到下一段被指定侧选用的行。 | — | —— |
| MRG-045 | 上一个左侧选用 / 右侧选用 | Previous Left Take / Next Right Take | 反向导航。 | — | —— |
| MRG-046 | 下一个 / 上一个编辑项 | Next / Previous Edit | 在用户手工编辑行间导航。 | — | —— |
| MRG-047 | 查找 / 替换 / 转到 / 书签 | Find / Replace / Go To / Bookmarks | 与文本比较相同的搜索工具集。 | — | —— |
| MRG-048 | 比较输出到左侧 | Compare Output > Left | 在新文本比较中打开输出与左侧。 | — | 用于合并后复检 |
| MRG-049 | 比较输出到中间 | Compare Output > Center | 输出与祖先比较。 | — | —— |
| MRG-050 | 比较输出到右侧 | Compare Output > Right | 输出与右侧比较。 | — | —— |
| MRG-051 | 合并信息统计 | Merge statistics | 统计冲突数、已解决数等。 | — | —— |
| MRG-052 | 输出保存 | Save output | 保存输出文件；未保存关闭会提示。 | 提示 | 选择不保存会导致 Git 认为合并未完成 |
| MRG-053 | 输出作为第 4 参数 | 4-file invocation | 4 个文件参数时第 4 个作为输出窗格文件。 | 支持 | 顺序：左 右 中 输出 |

### 7.5 合并的自动化与集成

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MRG-054 | Git 作为 mergetool | Git mergetool integration | 配置为 `git mergetool` 后由 Git 调用做三路合并。 | 需用户配置 | 退出码 14 / 101 表示冲突 |
| MRG-055 | 合并返回码 | Merge return codes | 冲突检测返回 14；冲突且未写输出返回 101。 | — | 脚本须据此判定合并成败 |
| MRG-056 | 合并脚本化 | Script-driven merge | 通过脚本调用 `file-report`/`text-report` 与 `load`。 | — | 脚本层没有直接的 Take 命令，需借 `merge` 机制 |
| MRG-057 | 文件夹级三路合并 | Folder Merge | 在目录层对多文件批量执行三路合并。 | 需 Pro | 见域 12 |
| MRG-058 | 合并父级 | Merge parent | 合并输出的派生关系（输出源于哪个父会话）。 | — | 无显式 UI 概念，主要用于作用域继承 |
| MRG-059 | 取消合并 | Abort merge | 直接关闭视图放弃合并结果。 | — | 无 Undo，输出内容不写盘 |
| MRG-060 | 合并输出只读保护 | Output read-only | 可把输出设为只读以避免误改。 | 可写 | —— |
| MRG-061 | 冲突拒绝标记 | Conflict markers | `/force` 时输出中以 `<<<<<<<`/`=======`/`>>>>>>>` 风格标记冲突。 | 无标记 | 会把标记写入文件 |
| MRG-062 | 三方合并的颜色语义 | Merge coloring | 未修改/单侧修改/相同修改/差异修改/冲突各有配色。 | 预设 | 见 LOOK 章可自定义 |
| MRG-063 | 合并中的可见空白与语法高亮 | Visible Whitespace / Syntax Highlighting | 与文本比较相同。 | 高亮开启 | —— |
| MRG-064 | 合并中的行号与缩略图 | Line Numbers / Thumbnail | 与文本比较相同。 | 缩略图开、行号关 | 缩略图对长合并文件尤其有用 |
| MRG-065 | 合并自定义命令与快捷键 | Customize Commands for merge | 可为 Take Left/Right、Next Conflict 等绑定快捷键。 | 默认绑定 | —— |
| MRG-066 | 合并的会话保存 | Save merge session | 保存 Text Merge 会话以便复用。 | — | —— |
| MRG-067 | 清空会话 | Clear Session | 用空白合并视图替换当前视图。 | — | —— |
| MRG-068 | 合并视图中的行细节面板 | Text Details / Hex Details | 输出窗格底部的细节面板。 | 关闭 | —— |
| MRG-069 | 合并视图的工具栏 | Merge toolbar | Take 系列按钮、冲突导航按钮、输出窗格按钮。 | 显示 | 可自定义 |
| MRG-070 | 合并的只读源保护 | Source panes read-only | 三个源窗格禁止直接输入，只能通过 Take 按钮或菜单。 | 强制只读 | 习惯直接编辑的用户需适应 |
| MRG-071 | 合并输出初值构造 | Auto-built output | 输出内容在比较加载时自动构建，把非冲突变更并入。 | 自动 | 冲突区域留空或保留一侧 |

---

## 8. 文件夹比对核心（Folder Compare Criteria）

> 「比较页（Comparison）」决定**什么算差异**。「快速测试」只读元数据，速度远快于内容比对。

### 8.1 快速测试（Quick Tests）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIR-001 | 比较文件大小 | Compare file size | 大小不同即标记为差异。 | **开启** | 关闭后大小差异需靠内容比对发现 |
| DIR-002 | 比较时间戳 | Compare timestamps | 比较最后修改时间，差异需大于容差才判定。 | **开启** | 复制文件会刷新时间戳，导致「全都不同」 |
| DIR-003 | 时间戳容差 | Timestamp tolerance | 定义允许的时间差（秒）。 | 2 秒 | FAT/exFAT 精度为 2 秒，跨文件系统需放宽 |
| DIR-004 | 忽略夏令时差异 | Ignore daylight saving difference | 忽略精确 1 小时的时间差。 | 关闭 | 跨 DST 边界的备份场景必开 |
| DIR-005 | 忽略时区差异 | Ignore timezone difference | 忽略恰好为整小时数的时间戳差异。 | 关闭 | 会连带忽略真实的一小时级修改 |
| DIR-006 | 比较文件名大小写 | Compare filename case | 文件名大小写不同即标记为差异。 | 依平台 | Windows 不区分、Linux 区分 |
| DIR-007 | 比较文件属性 | Compare file attributes `[Win]` | 比较 DOS 属性。 | 关闭 | 仅复制文件就会改变 Archive 位，建议关闭 |
| DIR-008 | 属性-存档 | Archive attribute | 比较 Archive 位。 | 关闭 | 最易误报 |
| DIR-009 | 属性-系统 | System attribute | 比较 System 位。 | 关闭 | —— |
| DIR-010 | 属性-隐藏 | Hidden attribute | 比较 Hidden 位。 | 关闭 | —— |
| DIR-011 | 属性-只读 | Read-only attribute | 比较 Read-only 位。 | 关闭 | —— |
| DIR-012 | 扩展 Windows 属性 | Extended Windows attributes | BC5.2 新增：比较临时、离线等扩展属性。 | 关闭 | 需 NTFS |
| DIR-013 | Unix owner/group/type | Owner / Group / permissions | BC5.2 新增：比较并过滤 Unix 属主、属组、文件类型与权限。 | 关闭 | 仅 Unix；见 DIR-020~024 |
| DIR-014 | 需读取文件的测试标记 | Tests that require reading contents | 标记哪些测试需要真正读取文件内容。 | 提示 | 影响「快速测试是否足够」的判断 |

### 8.2 内容比对（Content Comparison）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIR-015 | 比较内容 | Compare contents | 启用基于大小或内容的更严格比对。 | 关闭 | 开启后速度显著下降 |
| DIR-016 | 内容-规则化比对 | Rules-based comparison | 按 File Format 关联比较，可忽略行尾、空白、注释差异。 | 内容比对的默认方式 | 依赖 File Format 与 Text 会话的重要性设置 |
| DIR-017 | 内容-CRC 比对 | CRC comparison | 比对 CRC 值。 | 可选 | CRC 相同不代表内容一定相同（概率性） |
| DIR-018 | 内容-二进制比对 | Binary comparison | 逐字节比对。 | 可选 | 最慢最准 |
| DIR-019 | 快速测试相同则跳过 | Skip if quick test says same | 只有时间戳/大小不同时才做慢速内容比对。 | 开启 | 关掉会显著变慢 |
| DIR-020 | 比较版本 | Compare version `[Win]` | 比较 exe/dll/ocx 中的版本信息资源。 | 关闭 | 仅 Windows 可执行文件 |
| DIR-021 | 覆盖快速测试结果 | Override quick test results | 内容比对判定相同即显示为匹配，即使时间戳/属性不同。 | 关闭 | 开启后可消除大量「伪差异」 |
| DIR-022 | 内容比对的相似判定 | Similar content | 内容不完全相同但相似度超过阈值时标记为「相似」。 | — | 返回码 12 = Similar |
| DIR-023 | 二进制比对的读取方式 | Binary read mode | 是否绕过系统磁盘缓存直接从介质读取。 | 依 Handling 设置 | 用于校验可疑介质复制结果 |
| DIR-024 | CRC 作为文件夹列 | CRC column | 在文件夹视图中显示并排序 CRC 列。 | 需先计算 | 未计算前该列可能为空 |

### 8.3 文件夹处理（Handling）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIR-025 | 后台自动扫描子文件夹 | Automatically scan subfolders in background | 加载比较时后台读取子文件夹，以便打开前即着色。 | **开启** | 大目录会长时间占用 IO |
| DIR-026 | 自动扫描顶层孤立文件夹 | Scan top-level orphan subfolders | 自动扫描顶层孤立子文件夹以准确报告大小。 | 开启 | 顶层孤立文件夹通常无需扫描即可着色 |
| DIR-027 | 加载时展开子文件夹 | Expand subfolders when loading session | 加载比较时自动展开所有文件夹。 | 关闭 | 配合 `/expandall` 使用 |
| DIR-028 | 仅展开有差异的子文件夹 | Expand only differing subfolders | 自动展开限定在有差异的文件夹。 | 关闭 | —— |
| DIR-029 | 压缩包-始终作为文件 | Archives: Always as files | 把压缩包当普通文件处理，显示文件图标。 | **默认** | 内容比对需先解压 |
| DIR-030 | 压缩包-打开后作为文件夹 | Archives: As folders once opened | 以压缩包图标显示，双击打开后其比较状态即为内容状态。 | 可选 | 打开后才计算状态 |
| DIR-031 | 压缩包-始终作为文件夹 | Archives: Always as folders | 把压缩包当普通文件夹处理。 | 可选 | 大压缩包首次展开很慢 |
| DIR-032 | 复制到 FTP 时接触本地文件 | Touch local files when copying to FTP | 复制到 FTP 后把本地文件时间戳改为与远端一致。 | 关闭 | 很多 FTP 站点不支持设置远端时间戳 |
| DIR-033 | 绕过磁盘缓存做二进制比对 | Bypass disk cache for binary comparison | 直接从介质读取文件。 | 关闭 | 校验可疑介质时开启 |
| DIR-034 | 跟随符号链接 | Follow symbolic links | 把符号链接与 NTFS 联接点按其目标显示（含大小、时间、属性）。 | 关闭 | 可能造成目录环，导致无限递归 |
| DIR-035 | 保持 DOS 8.3 别名 | Maintain DOS 8.3 aliases `[Win]` | 复制时尽量保持同名的 8.3 短文件名。 | 关闭 | 仅 Windows |
| DIR-036 | 复制 NTFS 权限 | Copy NTFS permissions `[Win]` | 复制文件的安全描述符（ACL）。 | 关闭 | 曾出现复制 NTFS 权限崩溃（5.0.1 修复） |
| DIR-037 | 自动刷新 | Auto-refresh | 周期性更新文件夹比较。 | 关闭 | 刷新会重置当前展开/选中状态 |
| DIR-038 | 刷新 vs 完整刷新 | Refresh vs Full Refresh | Refresh 刷新已打开文件夹；Full Refresh 重新扫描整个比较。 | — | Full Refresh 会重新计算所有内容比对 |
| DIR-039 | 递归子目录 | Recursive subfolders | 递归比对所有层次子文件夹。 | 开启 | 深度过大影响性能 |
| DIR-040 | 忽略文件夹结构 | Ignore folder structure | 跨层级按文件名比较（效果等同「平展文件夹」）。 | 关闭 | 会产生大量「伪对齐」 |
| DIR-041 | 比较文件与文件夹结构 | Compare files and folder structure | 显示内部文件/文件夹匹配当前显示过滤器的文件夹。 | 开启 | 孤立文件夹的过滤规则与孤立文件相同 |
| DIR-042 | 只比较文件 | Compare Files Only | 只显示包含文件的文件夹。 | 关闭 | —— |
| DIR-043 | 始终显示文件夹 | Always Show Folders | 显示被文件过滤器排除的所有文件夹。 | 关闭 | 常与「不显示」配合重建目录结构 |
| DIR-044 | 禁用过滤器 | Disable Filters | 暂时禁用文件过滤器和显示过滤器。 | 关闭 | 被过滤项以特定颜色（默认蓝绿）显示且状态未知 |
| DIR-045 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要差异视为相同。 | 关闭 | 与文本重要性设置联动 |
| DIR-046 | 自动展开的状态一致性 | Expand state consistency | 刷新后展开状态与选中状态的保留策略。 | 部分保留 | 大目录刷新后定位丢失 |
| DIR-047 | 大小写敏感文件系统处理 | Case-sensitive FS handling | 在区分大小写的文件系统上正确处理同名不同大小写的文件。 | 依平台 | 跨平台同步时的经典问题 |
| DIR-048 | 长路径处理 | Long path support | 处理超过 260 字符的 Windows 路径。 | — | 需长路径支持或 `\\?\` 前缀 |
| DIR-049 | 孤立项（Orphan） | Orphan | 只在一侧存在的文件/文件夹。 | — | 孤立文件夹的显示受「比较文件与文件夹结构」影响 |
| DIR-050 | 较新 / 较旧 | Newer / Older | 两侧都存在但时间戳不同时的新旧判定。 | — | 判定依赖时间戳容差与 DST/时区设置 |
| DIR-051 | 匹配（相同） | Match | 两侧内容与属性均相同。 | — | 「匹配」的严格程度由比较设置决定 |
| DIR-052 | 差异（不同） | Difference | 两侧都存在但内容/属性不同。 | — | —— |
| DIR-053 | 文件夹状态汇总 | Folder status rollup | 父文件夹的状态由其子项状态汇总（含数量统计）。 | 自动 | 未扫描的子文件夹状态为未知 |
| DIR-054 | 文件夹数量统计 | Folder count statistics | 报出各状态的文件/文件夹数量（相同/差异/孤立/较新/较旧）。 | Compare Info 提供 | 统计口径受显示过滤器影响 |
| DIR-055 | 快速测试与内容比对的优先级 | Criterion precedence | 快速测试先跑，未通过才跑内容比对（除非覆盖）。 | 顺序执行 | 顺序理解错会误判性能瓶颈 |

---

## 9. 文件夹视图 UI、列与显示过滤器

### 9.1 列与排序

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIRUI-001 | 文件名列 | Name column | 显示文件/文件夹名，两侧名字不同时分别显示。 | 始终显示 | —— |
| DIRUI-002 | 大小列 | Size column | 显示文件大小（文件夹显示聚合大小）。 | 显示 | 文件夹聚合大小需扫描完成后才准确 |
| DIRUI-003 | 修改时间列 | Modified column | 显示最后修改时间。 | 显示 | 时区/夏令时设置影响显示 |
| DIRUI-004 | CRC 列 | CRC column | 显示内容 CRC 值。 | 隐藏 | 需先执行内容比对才计算 |
| DIRUI-005 | 版本列 | Version column | 显示可执行文件的版本号。 | 隐藏 | 仅 Windows 可执行文件 |
| DIRUI-006 | 属性列 | Attributes column `[Win]` | 显示 DOS 文件属性。 | 隐藏 | —— |
| DIRUI-007 | 同步动作列 | Sync Action column | 文件夹同步视图中显示将要执行的动作（复制/删除/无）。 | 同步视图显示 | 只在 Folder Sync 有意义 |
| DIRUI-008 | 同步方向列 | Sync Direction column | 同步视图中显示动作方向（← / →）。 | 同步视图显示 | —— |
| DIRUI-009 | 列显隐控制 | View > Columns | 打开对话框勾选显示哪些列。 | — | 关闭列不改变比较，只影响显示 |
| DIRUI-010 | 按列排序 | Sort by column | 点击列头按该列排序。 | 按名称 | 排序改变行顺序，影响手动选择 |
| DIRUI-011 | 列宽调整 | Column width | 拖动列边界调整列宽。 | — | 不跨会话持久化（取决于版本） |
| DIRUI-012 | 行内状态色 | Row status color | 用不同背景色表示文件夹/文件的状态。 | 预设配色 | 见 LOOK 章 |
| DIRUI-013 | 沟槽状态点 | Gutter status dot | 行首色点表示该项的比较状态。 | 显示 | 颜色含义见图例 |
| DIRUI-014 | 图例窗口 | Legend | 显示文件夹视图配色含义的列表窗口。 | 关闭 | 颜色含义按显示过滤器动态变化 |
| DIRUI-015 | 日志面板 | Log panel | 视图底部显示操作日志（复制/删除记录）。 | 关闭 | 同步报告的重要来源 |
| DIRUI-016 | 日志保留策略 | Log retention | Options > Folder Views > Log 中配置日志大小与保留。 | 有限保留 | 日志过大时截断 |
| DIRUI-017 | 过滤提示条 | Filter notification bar | 有其它过滤器生效时视图底部出现提示条，可点击编辑。 | 自动出现 | 容易忽略导致「文件怎么不见了」 |

### 9.2 展开 / 折叠与导航

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIRUI-018 | 打开文件夹 | Actions > Open Folder | 在比较中展开选定文件夹。 | — | 展开触发内容比对（若启用） |
| DIRUI-019 | 关闭文件夹 | Actions > Close Folder | 折叠选定文件夹。 | — | —— |
| DIRUI-020 | 打开子文件夹 | Open Subfolders | 展开选定文件夹及其所有子文件夹。 | — | 大目录耗时 |
| DIRUI-021 | 关闭子文件夹 | Close Subfolders | 折叠选定文件夹及其子文件夹。 | 默认隐藏 | —— |
| DIRUI-022 | 展开所有 | Edit > Expand All | 展开视图中所有文件夹。 | — | 脚本中需先 `expand all` 才能出完整报表 |
| DIRUI-023 | 折叠所有 | Edit > Collapse All | 折叠视图中所有文件夹。 | — | —— |
| DIRUI-024 | 设为基准文件夹 | Set as Base Folder | 把选定文件夹设为本侧的基准文件夹。 | — | —— |
| DIRUI-025 | 在另一侧设为基准 | Set Base on Other Side | 把选定文件夹设为另一侧的基准文件夹。 | — | —— |
| DIRUI-026 | 两侧设为基准 | Set as Base Folders | 把选中的两个文件夹分别设为左右基准（或在新视图打开）。 | — | 需恰好选中两个 |
| DIRUI-027 | 向上浏览 | Go Up One Level | 一层层上移基准文件夹。 | — | —— |
| DIRUI-028 | 双击打开 | Double-click to open | 双击文件/文件夹以关联视图打开或展开。 | 开启 | 双击文件夹在「压缩包作为文件」模式下会打开压缩包 |
| DIRUI-029 | 在新视图打开 | Open | 在新比较视图中打开选定项，恰选两项则互为比较对象。 | — | 选中奇数项时行为不同 |
| DIRUI-030 | 仅比较选定 | Compare Selected | 只对选定项执行内容比较。 | — | 与全局内容比对设置并行 |
| DIRUI-031 | 会话内比较 | Compare In New View | 在会话内新开子会话比较选中文件对。 | — | 子会话可继承父会话设置 |
| DIRUI-032 | 键盘导航 | Keyboard navigation | 方向键、Home/End、PageUp/PageDown 在列表与窗格间导航。 | 支持 | —— |
| DIRUI-033 | 窗格间切换 | Pane focus switch | 左右侧焦点切换（Tab / 快捷键）。 | 支持 | 操作命令作用于「当前侧」 |
| DIRUI-034 | 同步滚动 | Synchronized scroll | 左右两栏同步滚动。 | 开启 | 取消同步后会失去行对应关系 |

### 9.3 显示过滤器（View 菜单）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIRUI-035 | 显示所有 | Show All | 不使用任何显示过滤器。 | — | —— |
| DIRUI-036 | 显示差异项 | Show Differences | 过滤掉两侧状态为匹配的项（含较新/较旧/孤立）。 | — | 语义是「非匹配」而非仅「内容不同」 |
| DIRUI-037 | 显示相同项 | Show Same | 过滤掉较旧、较新与孤立项，只留匹配项。 | — | —— |
| DIRUI-038 | 不显示孤立项 | Show Orphans | 过滤掉孤立项。 | — | 命名与语义相反，注意文案 |
| DIRUI-039 | 显示差异项但不包括孤立项 | Show Differences but no Orphans | 过滤掉匹配项与孤立项。 | — | —— |
| DIRUI-040 | 显示孤立项 | Show Orphans Only | 过滤掉匹配、差异、较旧、较新项，只留孤立项。 | — | —— |
| DIRUI-041 | 显示左侧较新项 | Show Left Newer | 过滤掉左侧匹配/孤立/较旧项（保留右侧较新或差异项）。 | — | —— |
| DIRUI-042 | 显示右侧较新项 | Show Right Newer | 对称操作。 | — | —— |
| DIRUI-043 | 显示左侧较新项和左侧孤立项 | Show Left Newer and Left Orphans | 过滤掉左侧匹配与较旧项，以及右侧孤立项。 | — | —— |
| DIRUI-044 | 显示右侧较新项和右侧孤立项 | Show Right Newer and Right Orphans | 对称操作。 | — | —— |
| DIRUI-045 | 显示左侧孤立项 | Show Left Orphans | 只保留左侧孤立项。 | — | —— |
| DIRUI-046 | 显示右侧孤立项 | Show Right Orphans | 只保留右侧孤立项。 | — | —— |
| DIRUI-047 | 不显示 | Show None | 隐藏所有文件，配合「始终显示文件夹」重建目录结构。 | 默认隐藏命令 | 经典用法：只复制目录骨架 |
| DIRUI-048 | 始终显示文件夹 | Always Show Folders | 显示被文件过滤器排除的所有文件夹。 | 关闭 | —— |
| DIRUI-049 | 比较文件和文件夹结构 | Compare Files and Folder Structure | 显示内部匹配当前过滤器的文件夹。 | 开启 | —— |
| DIRUI-050 | 只比较文件 | Compare Files Only | 只显示包含文件的文件夹。 | 关闭 | —— |
| DIRUI-051 | 忽略文件夹结构 | Ignore Folder Structure | 比较所有层级下的文件名（平展）。 | 关闭 | —— |
| DIRUI-052 | 禁用过滤器 | Disable Filters | 临时禁用文件/显示过滤器，被过滤项以蓝绿色显示。 | 关闭 | 被禁用后状态未知 |
| DIRUI-053 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要差异视为相同。 | 关闭 | —— |

### 9.4 选择与批量操作

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIRUI-054 | 选中所有 | Select All | 选中所有可见项。 | — | 「可见」受过滤器影响 |
| DIRUI-055 | 选中所有文件 | Select All Files | 只选中所有可见文件（不含文件夹）。 | — | 常用于批量生成报表/补丁 |
| DIRUI-056 | 选择较新项（左/右/两侧） | Select Newer > Left/Right/All | 选中指定侧的较新可见文件。 | — | —— |
| DIRUI-057 | 选择孤立项（左/右/两侧） | Select Orphans > Left/Right/All | 选中指定侧的孤立文件。 | — | —— |
| DIRUI-058 | 反转选择 | Invert Selection | 反选。 | — | —— |
| DIRUI-059 | 刷新选中部分 | Refresh Selection | 只刷新选中项的状态。 | — | —— |
| DIRUI-060 | 选择掩码（脚本） | select 命令的掩码 | 脚本中按 `left.diff.files` 等掩码精确定位可操作项。 | — | 见 SCRIPT 章 |
| DIRUI-061 | 文件夹选中与状态继承 | Folder selection semantics | 选中文件夹时其子项的操作语义。 | 递归 | 复制/删除文件夹会递归作用 |
| DIRUI-062 | 在另一侧显示对应项 | Reveal counterpart | 定位到另一侧对应的项。 | — | 孤立项无对应 |
| DIRUI-063 | 复制文件名/路径 | Copy File Names | 把选中项的路径复制到剪贴板。 | — | 可含单侧或双侧路径 |
| DIRUI-064 | 多选与操作确认 | Multi-select confirmation | 批量操作前的确认对话。 | 依 Options > Folder Views > Confirmations | 谨慎关闭确认 |
| DIRUI-065 | 展开状态与过滤器交互 | Expand vs filter interaction | 被文件过滤器排除的子文件夹不会被 `expand all` 展开。 | — | 脚本生成报表时常见坑 |
| DIRUI-066 | 文件夹颜色语义 | Folder status colors | 相同/差异/孤立/较新/较旧/被过滤 各有配色。 | 预设 | 可自定义 |
| DIRUI-067 | 状态栏汇总 | Status bar summary | 底部状态栏显示各项计数与当前操作提示。 | 显示 | —— |
| DIRUI-068 | 列头右键菜单 | Column header context menu | 快速切换列显隐。 | — | —— |
| DIRUI-069 | 视图工具栏 | Folder toolbar | 打开/关闭/展开/复制/移动/删除/新建文件夹/过滤等快捷按钮。 | 显示 | 可自定义 |
| DIRUI-070 | 上下文菜单 | Context menu | 右键菜单按会话类型与选中项动态变化。 | — | —— |
| DIRUI-071 | 大文件夹懒加载 | Lazy loading | 未展开的文件夹不显示子项，逐步加载。 | 开启 | 状态汇总依赖后台扫描完成 |
| DIRUI-072 | 未知状态显示 | Unknown status | 尚未扫描的项显示为未知状态。 | — | 常被误解为「相同」 |
| DIRUI-073 | 结果稳定排序 | Stable sort | 排序后同状态项的相对顺序。 | 名称序 | —— |
| DIRUI-074 | 视图内搜索文件名 | Find Filename | 按名称搜索并定位到文件。 | 支持 | 与「查找下一个文件名」配对 |

---

## 10. 文件操作：复制 / 移动 / 删除 / 重命名 / 属性

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FILEOP-001 | 复制到另一侧 | Copy to Other Side | 把选中文件/文件夹复制到相对侧。 | 动态标题（→右/→左） | 会递归复制整个文件夹 |
| FILEOP-002 | 复制到右侧 | Copy to Right | 显式指定复制到右侧。 | 默认隐藏，`Ctrl+R` | —— |
| FILEOP-003 | 复制到左侧 | Copy to Left | 显式指定复制到左侧。 | 默认隐藏，`Ctrl+L` | —— |
| FILEOP-004 | 复制到一侧（提示） | Copy to Side (prompt) | 弹出对话框询问复制到哪一侧。 | 默认隐藏 | —— |
| FILEOP-005 | 复制到文件夹 | Copy to Folder | 把选中项复制到指定文件夹。 | — | 支持保留相对结构（脚本 `copyto`） |
| FILEOP-006 | 移动到另一侧 | Move to Other Side | 移动而非复制。 | 动态标题 | 移动跨卷时等价于复制+删除 |
| FILEOP-007 | 移动到右侧 / 左侧 | Move to Right / Left | 显式方向的移动。 | 默认隐藏 | —— |
| FILEOP-008 | 移动到一侧（提示） | Move to Side (prompt) | 询问移动到哪一侧。 | 默认隐藏 | —— |
| FILEOP-009 | 移动到文件夹 | Move to Folder | 移动到指定文件夹。 | — | —— |
| FILEOP-010 | 复制方向随选择变化 | Dynamic copy command | 依据当前选中侧动态决定命令标题、图标与方向。 | 自动 | 只选中左侧时标题为「复制到右侧」 |
| FILEOP-011 | 删除 | Delete | 删除选中文件/文件夹。 | 走回收站 | BC5 起默认使用回收站 |
| FILEOP-012 | 删除到回收站 | Delete to Recycle Bin | 删除时送入回收站。 | 开启 | 网络驱动器、远程服务、压缩包内不支持回收站 |
| FILEOP-013 | 永久删除 | Permanent delete | 绕过回收站直接删除。 | 关闭 | 不可恢复 |
| FILEOP-014 | 重命名 | Rename | 重命名单个选中项。 | — | 会改变对齐关系 |
| FILEOP-015 | 批量重命名 | Multi-file rename | 对多个选中项按掩码批量重命名。 | 脚本 `rename` | 使用 DOS 风格掩码或 `regexpr` 正则 |
| FILEOP-016 | 正则重命名 | Regex rename | 用正则表达式做查找替换式重命名。 | — | 语法为 PCRE |
| FILEOP-017 | 新建文件夹 | New Folder | 在指定侧创建新文件夹。 | — | —— |
| FILEOP-018 | 修改属性 | Attributes | 修改选中文件/文件夹的属性。 | 仅 Windows | 其他平台无 DOS 属性概念 |
| FILEOP-019 | 接触（时间戳） | Touch | 把一侧的时间戳复制到另一侧，或把某侧全部设为指定/当前时间。 | 需先选中文件 | 影响后续时间戳比对结果 |
| FILEOP-020 | 复制时间戳 | Touch Left→Right / Right→Left | 单向复制时间戳。 | — | —— |
| FILEOP-021 | 设为当前时间 | Touch : now | 把某侧全部文件时间戳设为当前时间。 | — | 破坏性较强 |
| FILEOP-022 | 排除（Exclude） | Exclude | 把选定文件或文件类型从当前会话排除。 | — | 会写入会话的名称过滤器 |
| FILEOP-023 | 忽略（Ignore） | Ignore | 抑制所选条目的差异（视为相同）。 | — | 仅本次会话生效 |
| FILEOP-024 | 刷新选择 | Refresh Selection | 刷新选中项的状态。 | — | —— |
| FILEOP-025 | 在资源管理器中显示 | Explorer | 打开系统文件管理器上下文菜单。 | 仅 Windows | 其他平台用 Reveal in Finder / 文件管理器 |
| FILEOP-026 | 用关联程序打开 | Open With > Associated Application | 用系统默认程序打开。 | — | 外部修改后文件夹视图不自动刷新 |
| FILEOP-027 | 用文本编辑打开 | Open With > Text Edit | 用内置文本编辑器打开。 | — | —— |
| FILEOP-028 | 快速比较对话框 | Quick Compare | 为选中文件弹出快速比较对话框。 | — | 恰选两项则直接互为比较 |
| FILEOP-029 | 和比较（选两项） | Compare With | 先选一项再点另一项，在新窗口中比较它们。 | — | 用于手工对齐不同名文件 |
| FILEOP-030 | 对齐（强制并排） | Align | 强制选中项并排排列（父文件夹必须已并排）。 | 需 Pro | 对目录对齐的强制覆盖 |
| FILEOP-031 | 手工对齐两项 | Align (pick two) | 选一项再点第二项使其并排。 | 需 Pro | 结果保存在会话中 |
| FILEOP-032 | 比较内容 | Compare Contents | 用 CRC / 二进制 / 规则化 三种方式之一比较选中项内容。 | 弹出对话框选择 | 只影响本次，不改变会话设置 |
| FILEOP-033 | 内容比较的即时性 | One-shot content compare | 内容比较是「一次性的」，改变方式需重新执行。 | — | 脚本中需用 `criteria` 改变比较方式 |
| FILEOP-034 | 覆盖确认 | Overwrite confirmation | 复制/移动覆盖已有文件前的确认。 | 提示 | 可配置 Always/Never/Ask |
| FILEOP-035 | 保留时间戳 | Preserve timestamps | 复制时保留源文件时间戳。 | 开启 | 关闭后时间戳比对会全部报差异 |
| FILEOP-036 | 属性同步 | Preserve attributes | 复制时保留文件属性。 | 依设置 | 见 DIR-007~012 |
| FILEOP-037 | 权限同步 | Preserve permissions | 复制时保留权限/ACL。 | 见 DIR-013/DIR-036 | 跨平台时可能失败 |
| FILEOP-038 | 只复制较新文件 | Copy newer only | 只复制比目标侧更新的文件（更新式同步）。 | 由同步预设控制 | 时间戳设置不准时会漏复制 |
| FILEOP-039 | 复制时的目录创建 | Create missing folders | 目标侧缺少中间目录时自动创建。 | 自动 | —— |
| FILEOP-040 | 复制进度与取消 | Progress and cancel | 长时复制显示进度并可取消。 | 支持 | 取消后可能留下部分文件 |
| FILEOP-041 | 复制失败的错误处理 | Copy error handling | 单文件失败时是否继续处理其它文件。 | 继续并记日志 | 日志面板/文件记录失败项 |
| FILEOP-042 | 跨文件系统复制 | Cross-filesystem copy | 不同文件系统间的复制（时间戳精度、大小写敏感差异）。 | — | FAT 精度 2 秒；大小写敏感差异 |
| FILEOP-043 | 复制到压缩包内 | Copy into archive | 把文件复制进压缩包（压缩包需支持写）。 | 支持 | 部分格式只读 |
| FILEOP-044 | 复制到远程站点 | Copy to remote | 复制到 FTP/SFTP/云。 | 需 Profiles | 远端时间戳支持度不一 |
| FILEOP-045 | 循环复制检测 | Recursive copy guard | 复制文件夹进自身子目录的检测。 | — | 符号链接场景下风险更高 |
| FILEOP-046 | 复制链接与联接点 | Symlink copy semantics | 复制符号链接时是复制链接还是目标内容。 | 依 Follow symbolic links | 设置不同结果完全不同 |
| FILEOP-047 | 稀疏文件与备用数据流 | Sparse files / ADS `[Win]` | 稀疏文件与 NTFS 备用数据流的复制行为。 | — | 一般工具常丢失 ADS |
| FILEOP-048 | 复制中的长路径 | Long path copy | 超长路径下的复制。 | — | 可能需要长路径支持 |
| FILEOP-049 | 复制日志 | Copy log | 记录所有复制/移动/删除操作。 | 记录 | 是「同步报告」的底层来源 |
| FILEOP-050 | 同步报告 | Sync report / log | 把本次同步执行的操作汇总输出。 | 需主动查看日志 | 脚本中 `log verbose <file>` 可落盘 |
| FILEOP-051 | 操作撤销 | Undo file operations | BC 不提供文件级撤销，需靠备份/回收站。 | 无 Undo | 大批量操作前建议先预览 |
| FILEOP-052 | 备份文件的创建 | Backup on overwrite | 覆盖时创建 `.bak` 备份（Options > Backup）。 | 依设置 | `/nobackups` 禁用 |
| FILEOP-053 | 只读文件的覆盖 | Read-only overwrite | 目标只读时是否强制覆盖。 | 询问 | —— |
| FILEOP-054 | 在另一侧创建对应目录 | Mirror folder structure | 把目录骨架镜像到另一侧（配合 Show None）。 | — | 经典运维用法 |
| FILEOP-055 | 复制文件名列表 | Copy File Names | 复制选中项路径到剪贴板。 | — | 可只复制有差异项 |
| FILEOP-056 | 批量选择与状态筛选联动 | Selection × status | 先按状态过滤再全选，可批量操作特定状态项。 | — | 误操作风险最高的组合 |
| FILEOP-057 | 删除确认 | Delete confirmation | 删除操作的二次确认。 | 确认 | 可在 Options 中调整 |
| FILEOP-058 | 移动的原子性 | Move atomicity | 同卷移动为原子重命名，跨卷为复制+删除。 | 依卷 | 跨卷移动中断会留下两份 |

---

## 11. 文件夹同步（Folder Sync）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SYNC-001 | 文件夹同步会话 | Folder Sync session | 专用同步视图，先预览再执行。 | — | 与文件夹比较的区别是「以执行为目的」 |
| SYNC-002 | 同步预览 | Sync preview | 以动作列/方向列明确展示将要执行的操作。 | 开启 | 执行前必须核对 |
| SYNC-003 | 运行同步 | Session > Run Sync | 执行全部同步操作。 | — | 执行后部分操作不可逆 |
| SYNC-004 | 同步信息 | Folder Sync Info | 显示同步统计数据（新增/更新/删除数量）。 | — | —— |
| SYNC-005 | 查看（Peek） | View > Peek / Show non-sync items | 临时显示不被同步影响的项。 | 关闭 | 临时视图状态，不改设置 |
| SYNC-006 | 更新预设（左→右） | Update preset: left→right | 从左侧复制较新与孤立项到右侧。 | — | 参数：`update:left->right` |
| SYNC-007 | 更新预设（右→左） | Update preset: right→left | 反向更新。 | — | —— |
| SYNC-008 | 更新预设（双向） | Update preset: two-way | 双向复制较新与孤立项。 | — | 双向更新不是合并，冲突时以较新为准 |
| SYNC-009 | 镜像预设（左→右） | Mirror preset: left→right | 使右侧与左侧完全一致：替换不同、删除右侧孤立、补齐左侧孤立。 | — | **会删除目标侧文件** |
| SYNC-010 | 镜像预设（右→左） | Mirror preset: right→left | 对称操作。 | — | —— |
| SYNC-011 | 同步预设选择 | Sync preset selector | 在 Session Settings > Sync 中选择预设或自定义。 | 无预设 | 预设与手动改动不一致时以设置页为准 |
| SYNC-012 | 自定义同步规则 | Custom sync rules | 自定义「较新/较旧/孤立/不同」各自对应什么操作。 | — | 规则组合语义需仔细核对 |
| SYNC-013 | 创建空文件夹 | Create empty folders | 同步时把空目录结构也复制过去。 | 可选（`create-empty`） | 不加此项会丢失目录骨架 |
| SYNC-014 | 同步脚本命令 | `sync` 命令 | `sync [visible] [create-empty] (update\|mirror):(left->right\|right->left\|all)`。 | — | `visible` 只操作当前可见（过滤后）项 |
| SYNC-015 | 同步的可见项限定 | `sync visible` | 只对当前显示过滤器下可见的项执行同步。 | 需显式指定 | 过滤条件写错会漏同步 |
| SYNC-016 | 镜像的删除行为 | Mirror delete | 镜像会删除目标侧的孤立项。 | 执行 | 回收站不可用时为永久删除 |
| SYNC-017 | 同步中的删除 | Sync delete | 同步过程删除项时的确认与日志。 | 记日志 | —— |
| SYNC-018 | 同步冲突/双向歧义 | Bi-directional ambiguity | 双向更新时两侧都改动的项判定。 | 以较新为准 | 可能丢失一侧的修改 |
| SYNC-019 | 同步的比较依据 | Sync comparison criteria | 同步使用与文件夹比较相同的比较页设置。 | 同 Folder Compare | 时间戳冗余设置会让每次同步都重传 |
| SYNC-020 | 同步的时间戳保留 | Preserve timestamps on sync | 保留源时间戳以避免重复同步。 | 开启 | 关闭会导致「同步永不收敛」 |
| SYNC-021 | 同步到远端的本地接触 | Touch local after remote copy | 复制到不支持设置时间戳的 FTP 后修正本地时间戳。 | 关闭 | 见 DIR-032 |
| SYNC-022 | 同步日志 | Sync log | 记录同步执行明细，可输出为文件。 | 日志面板 | 脚本 `log verbose` 落盘 |
| SYNC-023 | 同步报告输出 | Sync report | 生成同步结果报告。 | — | 可用 folder-report |
| SYNC-024 | 同步前的备份 | Backup before sync | 覆盖/删除前生成备份。 | 依 Options > Backup | 镜像场景强烈建议开启 |
| SYNC-025 | 同步方向图标 | Direction indicators | 行内箭头指示该项将向哪一侧同步。 | 显示 | —— |
| SYNC-026 | 同步动作计数 | Pending action count | 状态栏显示待执行动作数量。 | 显示 | —— |
| SYNC-027 | 同步的自动刷新 | Auto-refresh during sync | 同步执行中视图的刷新策略。 | — | 大规模同步时刷新开销大 |
| SYNC-028 | 从文件夹比较发起同步 | Compare > Sync Base Folders | 基于当前基准文件夹打开同步会话。 | — | 会话设置独立，不继承比较会话 |
| SYNC-029 | 从同步发起比较 | Sync > Compare Base Folders | 基于当前基准文件夹打开比较会话。 | — | —— |
| SYNC-030 | 同步的数据源类型 | Sync source types | 本地、网络、FTP/SFTP、云存储、压缩包、快照均可作为同步源。 | — | 快照为只读，只能作为来源 |
| SYNC-031 | 同步的取消与中断 | Abort sync | 中途取消同步。 | 支持 | 可能留下不一致状态 |
| SYNC-032 | 同步的属性/权限保留 | Attribute/permission preservation | 同步时是否保留属性与权限。 | 依设置 | 跨平台失败需记日志 |
| SYNC-033 | 同步的目录深度控制 | Recursion depth | 是否递归子目录。 | 递归 | 非递归时会漏掉深层文件 |
| SYNC-034 | 同步过滤器 | Sync filters | 名称过滤器/其它过滤器同样作用于同步。 | 生效 | 过滤导致「看似同步完成但文件缺失」 |
| SYNC-035 | 同步中的孤立文件夹处理 | Orphan folder handling | 孤立文件夹的复制/删除逻辑。 | 与孤立文件相同规则 | 见 DIR-041 |
| SYNC-036 | 同步日志的导出 | Export sync log | 把同步日志另存为文件。 | — | —— |
| SYNC-037 | 计划任务型同步 | Scheduled sync | 用系统计划任务 + 脚本实现定时同步。 | 需自行配置 | 见 SCRIPT / AUTO 章 |
| SYNC-038 | 同步的返回码 | Sync exit codes | 脚本/命令行退出码表示成功/冲突/错误。 | — | 见 CLI 章返回码表 |
| SYNC-039 | 同步视图的列 | Sync-specific columns | Action / Direction 列。 | 显示 | 只在同步会话中出现 |
| SYNC-040 | 同步的只读源 | Read-only sync source | 源为只读（如快照）时的处理。 | 允许 | —— |
| SYNC-041 | 同步中的大文件 | Large file sync | 断点/进度/内存策略。 | 流式 | 大文件同步耗时且占带宽 |
| SYNC-042 | 同步的相对路径保持 | Relative path preservation | 复制时保留相对目录结构。 | 保留 | `copyto` 的 `path:base/relative/none` 三种模式 |
| SYNC-043 | 同步的命令行发起 | `/sync` switch | Windows/macOS 命令行直接打开 Folder Sync。 | — | Linux 无此开关 |
| SYNC-044 | 同步会话的保存与复用 | Save sync session | 保存同步会话（含预设与过滤）。 | — | 定时任务的常用形式 |
| SYNC-045 | 同步的 dry-run 能力 | Preview-only | 仅预览不执行（不按 Run Sync 即为 dry-run）。 | 默认不执行 | 无独立「dry-run 开关」 |
| SYNC-046 | 双向同步的不可判定项 | Undecidable items | 两侧同时修改且时间戳无法区分时的处理。 | 保守不处理或按较新 | 容易漏同步 |

---

## 12. 文件夹合并（Folder Merge）`[Pro]`

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| DIRM-001 | 文件夹合并会话 | Folder Merge session | 三路合并目录：左/右为两个版本，中部为共同祖先。 | 需显式新建 | 与「文件夹比较」只支持两路不同 |
| DIRM-002 | 中部祖先文件夹 | Ancestor (center) folder | 中部被假定为「旧的、权威的祖辈」。 | 必需 | 把新版本放中间会导致合并方向反了 |
| DIRM-003 | 合并目标选择 | Merge target | 选择合并到左侧或右侧（而非「其它」）。 | 需选择 | 合并到中间祖先在语义上无意义 |
| DIRM-004 | 执行合并按钮 | Merge! button | 一键执行文件夹级合并。 | — | **不可撤销（无 Undo）** |
| DIRM-005 | 逐文件冲突决策 | Per-file take decisions | 对每个冲突文件选择采用哪一侧。 | 需人工 | 文件数量大时耗时 |
| DIRM-006 | 冲突预测箭头 | Prediction arrows | 边缘箭头预示该文件合并后会采用哪一侧。 | 显示 | 冲突项无箭头 |
| DIRM-007 | 自动合并无冲突文件 | Auto-merge non-conflicting | 只有一侧改动的文件自动合并。 | 自动 | —— |
| DIRM-008 | 双侧同一文件均改动 | Both-sides-modified conflict | 两侧都改动的文件需人工选边。 | 冲突 | —— |
| DIRM-009 | 文件级三路合并的嵌套 | Nested text merge | 冲突文本文件可打开 Text Merge 做行级合并。 | 支持 | 需 Pro 的 Text Merge |
| DIRM-010 | 目录级孤立项处理 | Orphan handling | 只在一侧存在的文件/目录的合并策略。 | 保留 | —— |
| DIRM-011 | 删除传播 | Deletion propagation | 一侧删除的文件在合并中的处理。 | 依祖先判定 | 祖先中存在而某侧删除 → 需判断是否传播删除 |
| DIRM-012 | 文件夹合并的比较依据 | Merge comparison criteria | 基于文件夹比较的比较页设置判定「改了没有」。 | 同 Folder Compare | 大小/时间戳设置影响冲突判定 |
| DIRM-013 | 文件夹合并的预览列 | Merge preview | 每行显示合并后的取舍结果。 | — | 执行前核对 |
| DIRM-014 | 文件夹合并的日志 | Merge log | 记录合并执行明细。 | 记日志 | —— |
| DIRM-015 | 命令行发起文件夹合并 | `/fv="Folder Merge"` | 命令行指定视图类型新建文件夹合并会话。 | — | 需要三个路径参数 |
| DIRM-016 | 合并输出的目录 | `/mergeoutput=<path>` | 文件夹合并的输出目录指定。 | 由参数决定 | —— |
| DIRM-017 | 文件夹合并与 Git | Git merge of directories | 从 Git mergetool 请求发起目录级合并。 | 需配置 | —— |
| DIRM-018 | 嵌套目录的递归合并 | Recursive directory merge | 子目录同样按三路逻辑递归处理。 | 递归 | 大型仓库耗时 |
| DIRM-019 | 合并前的备份 | Backup before folder merge | 合并前备份原文件。 | 依 Options > Backup | 强烈建议开启（无 Undo） |
| DIRM-020 | 文件夹合并信息 | Folder Merge Info | 统计冲突文件数、自动合并文件数。 | — | —— |
| DIRM-021 | 文件夹合并的会话保存 | Save folder merge session | 保存会话（含三个基准路径与设置）。 | — | —— |
| DIRM-022 | 文件夹合并的只读限制 | Read-only constraint | 三个源窗格不可直接编辑，只能通过 Take 决策。 | 强制 | 需适应操作范式 |

---

## 13. 过滤与文件掩码（Filters & File Masks）

### 13.1 文件掩码语法（File Masks）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FILT-001 | 掩码通配符 `*` | Asterisk wildcard | 匹配零个或多个任意字符。 | — | `*.*` 历史上也能匹配无扩展名文件 |
| FILT-002 | 掩码通配符 `?` | Question mark | 匹配任意单个字符。 | — | —— |
| FILT-003 | 字符集合 `[az]` | Character set | 匹配集合中的任意单字符。 | — | —— |
| FILT-004 | 字符范围 `[a-z]` | Character range | 匹配范围内的任意单字符。 | — | —— |
| FILT-005 | 反向集合 `[!az]` | Negated set | 匹配不在集合中的任意单字符。 | — | —— |
| FILT-006 | 简单掩码 | Simple mask | 影响任何父文件夹被包含的文件/文件夹。 | — | 基准文件夹始终被影响 |
| FILT-007 | 文件夹掩码 | Folder mask (`p\`) | 以反斜杠结尾表示匹配文件夹。 | — | 平台路径分隔符差异需注意 |
| FILT-008 | 排除前缀 `-` | Exclusion prefix | 掩码前加减号表示排除。 | — | 排除列表中使用时不需要减号 |
| FILT-009 | 相关掩码 `p\f` | Related mask | 指定「文件必须位于文件夹 p 中」。 | — | —— |
| FILT-010 | 基准文件夹掩码 `.\f` | Base-relative mask | 文件必须在基准文件夹中。 | — | —— |
| FILT-011 | 任意层级掩码 `...\f` | Any-depth mask | 文件可有任意层级父文件夹（含无父）。 | — | 覆盖范围大，易误配 |
| FILT-012 | 掩码大小写敏感 | Mask case sensitivity | 掩码匹配是否区分大小写。 | 依平台/会话 | 跨平台脚本需显式声明 |
| FILT-013 | 多掩码分号分隔 | Semicolon-separated masks | 命令行 `/filters=` 用分号分隔多个掩码。 | — | 含空格需引号包裹 |
| FILT-014 | 掩码的正则模式 | Regex masks | 在对齐覆盖等场景可启用正则解释掩码。 | 关闭 | 需显式启用 |
| FILT-015 | 掩码的祖先文件夹传播 | Ancestor folder propagation | 若某文件夹的子孙中有被包含的文件，则该文件夹也被包含。 | 自动 | 会产生「空但显示」的文件夹 |

### 13.2 名称过滤器（Name Filters）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FILT-016 | 包含文件 | Include Files | 列出本会话要包含的文件掩码。 | 空 | —— |
| FILT-017 | 排除文件 | Exclude Files | 列出要排除的文件掩码。 | 空 | 排除优先于包含 |
| FILT-018 | 包含文件夹 | Include Folders | 列出要包含的文件夹掩码。 | 空 | —— |
| FILT-019 | 排除文件夹 | Exclude Folders | 列出要排除的文件夹掩码。 | 空 | `.git`、`node_modules`、`target` 等经典排除项 |
| FILT-020 | 每个掩码独占一行 | One mask per line | 掩码列表的输入格式要求。 | — | 混用分隔符会被当作字面量 |
| FILT-021 | 清除所有名称过滤器 | Clear name filters | 一键清空当前标签页所有过滤器设置。 | — | —— |
| FILT-022 | 名称过滤器另存为预设 | Add to current (preset) | 把当前掩码组合保存为可复用预设。 | — | —— |
| FILT-023 | 预设管理 | Filter presets | 命名、编辑、删除过滤器预设。 | — | 预设为全局，跨会话共享 |
| FILT-024 | 过滤器预设的下拉调用 | Preset dropdown | 从工具栏下拉快速套用预置过滤组合。 | — | —— |
| FILT-025 | 过滤器应用范围 | Filter scope | 名称过滤器对文件夹会话与同步会话均生效。 | 生效 | 同步时过滤会导致漏同步 |

### 13.3 其它过滤器（Other Filters）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FILT-026 | 按日期过滤 | Date filter | 按文件日期包含/排除。 | 关闭 | —— |
| FILT-027 | 天数相对时间 | Days before | 用「N 天之前」的相对时间计算过滤基准（零点之后计时）。 | 关闭 | 关闭时为绝对日期时间 |
| FILT-028 | 排除更旧的文件 | Exclude files older than | 排除早于基准时间的文件。 | 关闭 | —— |
| FILT-029 | 排除更新的文件 | Exclude files newer than | 排除晚于基准时间的文件。 | 关闭 | 两者同开可表达时间区间 |
| FILT-030 | 按大小过滤 | Size filter | 按文件大小包含/排除。 | 关闭 | 支持 KB/MB/GB/TB 单位 |
| FILT-031 | 排除更小的文件 | Exclude files smaller than | 排除小于阈值的文件。 | 关闭 | —— |
| FILT-032 | 排除更大的文件 | Exclude files larger than | 排除大于阈值的文件。 | 关闭 | 两者同开可表达大小区间 |
| FILT-033 | 大小单位下拉 | Size unit combobox | KB/MB/GB/TB 选择。 | KB | 曾出现单位被忽略缺陷（5.0.1 修复于 macOS/Linux） |
| FILT-034 | 按 DOS 属性过滤 | Attribute filter `[Win]` | 按受保护/隐藏等属性排除。 | 默认排除受保护系统文件 | 仅 Windows |
| FILT-035 | 排除受保护的操作系统文件 | Exclude protected OS files | 默认排除系统保护文件。 | **开启** | 关闭后系统目录会大量出现 |
| FILT-036 | 按源代码控制状态过滤 | Source control status filter `[Win]` | 按 VCS 状态（已修改/未版本化等）过滤。 | 关闭 | 需配置 VCS |
| FILT-037 | 按文本内容过滤 | Text content filter | 文件必须包含指定文本才被包含，否则排除。 | 关闭 | 需逐个读取文件，性能影响大 |
| FILT-038 | 过滤器提示条 | Filter notification bar | 有其它过滤器生效时视图底部显示提示条与编辑图标。 | 自动 | 容易忽略 |
| FILT-039 | 过滤器单侧冲突显示 | Single-side filter conflict | 若过滤器判定一侧隐藏另一侧显示，则两者都显示，被隐藏者以蓝绿色（凫蓝色）标示。 | 自动 | 颜色语义是「本应隐藏」 |
| FILT-040 | 清除所有其它过滤器 | Clear other filters | 一键清空其它过滤器设置。 | — | —— |
| FILT-041 | Unix 类型过滤（脚本） | `filter unixtype:` | 脚本中按 Unix 文件类型（块/字符/目录/链接/管道/普通）过滤。 | — | 仅 Unix |
| FILT-042 | 属性掩码（脚本） | `filter attrib:` | 脚本中按 `[a][c][e][h][i][l][o][p][r][s][t][u][z]` 属性集合过滤。 | — | 仅 Windows |
| FILT-043 | 受保护文件过滤（脚本） | `filter exclude-protected` / `include-protected` | 脚本中控制是否排除受保护文件。 | exclude | 仅 Windows |
| FILT-044 | 日期过滤（脚本） | `filter cutoff:` | 脚本按时间戳或「N 天」过滤。 | — | —— |
| FILT-045 | 大小过滤（脚本） | `filter size:` | 脚本按带单位的大小过滤。 | — | —— |

### 13.4 对齐覆盖与格式关联覆盖

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FILT-046 | 对齐覆盖 | Alignment Override `[Pro]` | 覆盖文件夹会话中文件/文件夹的对齐方式。 | 空 | 未覆盖时按名称左对齐右对齐 |
| FILT-047 | 左/右文件掩码 | Align Left file / Right file | 定义左侧掩码与右侧掩码的配对关系。 | — | 支持 `*` `?` 通配符 |
| FILT-048 | 对齐覆盖示例：不同扩展名 | x.txt ↔ x.csv | 把同名不同扩展名的文件并排。 | — | 典型用法：`.gif` ↔ `.png` |
| FILT-049 | 对齐覆盖示例：带模式的前缀 | x*.jpg ↔ y*.jpg | 把 `x1.jpg/x2.jpg` 与 `y1.jpg/y2.jpg` 配对。 | — | —— |
| FILT-050 | 对齐覆盖的正则模式 | Regex alignment override | 掩码按 PCRE 正则解释。 | 关闭 | 需显式启用 |
| FILT-051 | 对齐覆盖的作用范围限定 | Limit to this folder | 指定该覆盖规则生效的相对路径。 | 全会话 | 限定写错则规则不生效 |
| FILT-052 | 手工对齐覆盖 | Align pick-two | 在文件夹会话中手工选定两项对齐，最简便的对齐修正方式。 | — | 结果保存在会话设置中 |
| FILT-053 | 文件格式关联覆盖 | File format association override | 覆盖该文件夹会话使用哪些文件格式。 | 继承全局 | 会话内启用而全局禁用的格式以粗体显示 |
| FILT-054 | 覆盖状态的粗体标识 | Bold format listing | 会话激活状态与全局不同的文件格式以粗体列出。 | — | 视觉提示易被忽略 |
| FILT-055 | 文件名对齐规则化 | Rules-based name alignment | 按规则忽略数字/日期等差异进行文件名对齐。 | 通过对齐覆盖/正则实现 | BC 无内置的「忽略数字对齐」开关，需自行配正则 |
| FILT-056 | 文件名不匹配仍比对 | Compare despite name mismatch | 通过对齐覆盖把不同名文件配对后仍做内容比对。 | — | 需 Pro |
| FILT-057 | 排除项的会话写入 | Exclude writes to session | Actions > Exclude 会把掩码写入会话的名称过滤器。 | 写入 | 下次打开该会话仍生效 |

---

## 14. 文件格式定义（File Formats）

> File Format 决定：使用哪种视图、比对/保存前做何种转换、文件的语法（grammar）、语法元素的大小写敏感性、制表位、以及用于提升对齐效果的行重。

### 14.1 格式管理器

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FMT-001 | 文件格式管理器 | File Formats dialog | 管理全部内置与自定义文件格式（Tools > File Formats）。 | — | 修改内置格式会影响所有会话 |
| FMT-002 | 格式列表 | Format list | 列出全部文件格式，可启用/禁用。 | 内置格式默认启用 | 禁用后该类型文件回退到兜底视图 |
| FMT-003 | 格式优先级 | Format order/priority | 列表顺序决定掩码冲突时的优先级。 | 内置顺序 | 与多个掩码匹配时取第一个匹配项 |
| FMT-004 | 掩码冲突提示 | Conflicting mask indicator | macOS 下对「因更高优先级格式匹配同掩码而不会生效」的格式标红。 | 标红 | 仅 macOS 有此视觉提示 |
| FMT-005 | 新建格式 | New format | 新建自定义文件格式。 | — | —— |
| FMT-006 | 复制格式 | Clone format | 基于现有格式创建副本后修改。 | — | 推荐做法，避免误改内置 |
| FMT-007 | 删除格式 | Delete format | 删除自定义格式。 | — | 内置格式通常不可删除 |
| FMT-008 | 格式启用/禁用 | Enable/Disable format | 全局启用开关。 | 启用 | —— |
| FMT-009 | 格式类型 | Format type | 文本格式 / 数据格式 / 图片格式 三类编辑界面。 | 依类型 | 三是不同的对话框结构 |
| FMT-010 | 格式掩码 | Masks | 与该格式关联的文件类型模式集合。 | 内置如 `*.c;*.cpp` | —— |
| FMT-011 | 格式描述 | Description | 任意描述文本；内置格式的描述包含限制与要求。 | 内置描述 | —— |

### 14.2 文本格式：转换（Conversion）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FMT-012 | 转换方法 | Conversion method | 定义文件如何被加载与保存。 | 无转换（纯文本直读） | 结构化/专有格式需转换 |
| FMT-013 | 外部程序转换 | External program conversion | 调用外部程序把文件转成纯文本后再比较。 | — | 需外部程序可用 |
| FMT-014 | 加载命令行 | Load command line | 转换程序路径与参数。 | — | 变量见 FMT-015 |
| FMT-015 | 转换程序变量 | `%s %t %n %x` | 源文件 / 目标文件 / 原始文件 / 原始扩展名（含点）。 | — | 变量名大小写敏感 |
| FMT-016 | 转换成功判定 | Conversion success criteria | 退出码为 0 且生成非空文件才算成功。 | — | 生成空文件会被判失败 |
| FMT-017 | 保存前转换 | Save conversion | 保存前调用转换程序回写原格式。 | 需取消「禁用编辑」 | 变量同上 |
| FMT-018 | 转换程序的禁用编辑 | Disable editing checkbox | 勾选后该格式文件在 BC 中不可编辑。 | — | 需取消勾选才能配置保存转换与就地编辑 |
| FMT-019 | 文件名编码 | Filename encoding | 转换程序参数中文件名的编码（Unicode/ANSI）。 | 自动 | 文件名含扩展字符时需显式指定 |
| FMT-020 | 内置 Word 文档转换 | `.docx` conversion | 提取 Word 文档文本进行比对。 | 内置 | Windows 系统 ANSI 代码页为 DBCS 时曾转换失败（5.2.1 修复） |
| FMT-021 | 内置 RTF 转换 | `.rtf` conversion | 提取 RTF 文本。 | 内置 | 曾出现比较 RTF 崩溃（5.0.1 修复） |
| FMT-022 | 编码 | Encoding / Code page | 大多数文本文件编码可自动检测，也可指定代码页。 | 自动检测 | BC5 默认假定 UTF-8 |
| FMT-023 | 每行字符限制 | Max characters per line | 超过长度自动断行；保存时清除人工断行。 | 关闭 | —— |
| FMT-024 | Ctrl+Z 结尾标记 | Ctrl+Z marks EOF | 把 0x1A 当作 EOF。 | 关闭 | 遗留 DOS 文本需开启 |
| FMT-025 | 保存时剪裁尾随空格 | Trim trailing whitespace on save | 保存前删除行尾空白。 | 关闭 | 静默改内容 |
| FMT-026 | 保存时前导空格转 Tab | Leading spaces to tabs on save | 保存前转换行首空白。 | 关闭 | —— |

### 14.3 文本格式：语法（Grammar）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FMT-027 | 语法元素 | Grammar element | 注释、关键字、字符串、数字等语法元素定义。 | 内置格式已定义 | 修改需理解元素优先级 |
| FMT-028 | 语法元素由语法项描述 | Grammar items | 每个语法元素由一到多个语法项（grammar item）描述。 | — | —— |
| FMT-029 | 语法项优先级 | Grammar item order | 列表中位置越高的语法项优先级越高。 | 列表顺序 | 顺序调错会让注释吃掉关键字 |
| FMT-030 | 语法项类型-分隔符 | Delimited item | 由前后分隔符界定的语法项（如字符串、注释块）。 | 常见 | 转义字符需配置 |
| FMT-031 | 语法项类型-行首关键字 | Line-start keyword | 行首关键字（如预处理器指令）。 | — | —— |
| FMT-032 | 语法项类型-关键字列表 | Keyword list | 一组关键字。 | — | —— |
| FMT-033 | 语法项类型-正则 | Regular expression item | 用正则匹配的语法项。 | — | 性能敏感 |
| FMT-034 | 语法项类型-数字 | Number item | 数字字面量。 | — | —— |
| FMT-035 | 语法项大小写敏感性 | Grammar case sensitivity | 每个语法元素可独立指定是否区分大小写。 | 依语法 | 影响 Importance 中的「字符大小写」语义 |
| FMT-036 | 制表位（格式级） | Tab stops (format) | 该格式的制表位设置。 | 4 | 与 Misc 页设置交互 |
| FMT-037 | 行重 | Line weights | 为语法元素定义权重，对齐算法优先匹配高权重行。 | 依格式 | 配置不当降低对齐质量 |
| FMT-038 | 语法高亮的语言覆盖 | Syntax highlighting coverage | 内置支持大量编程语言的语法高亮。 | 内置 | 官方宣传支持 200+ 语言 |
| FMT-039 | 语法元素的显示颜色 | Grammar element colors | 每个语法元素的着色可在 File View Colors 中配置。 | 预设 | 见 LOOK 章 |

### 14.4 文本格式：杂项（Misc）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FMT-040 | 插入空格而非 Tab | Insert spaces instead of tabs | Tab 键插入空格。 | 关闭 | —— |
| FMT-041 | 制表位设置 | Tab stop settings | Tab 停止间隔。 | 4 | —— |
| FMT-042 | 行是独立记录 | Lines are independent | 每行独立成记录，连续差异行视为一个段。 | 关闭 | 影响段导航粒度 |
| FMT-043 | 基于列的数据 | Column-based data | 每行字符位置重要，可逐列比较。 | 关闭 | 这是 BC 在「列/块」方向上的主要能力 |
| FMT-044 | 行结束标记 | Line ending handling | 该格式的行尾处理方式。 | 自动 | —— |

### 14.5 数据格式与图片格式

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FMT-045 | 数据格式编辑 | Edit data format | 数据（表格）格式的定义界面。 | 内置 CSV/TSV/Excel/HTML | —— |
| FMT-046 | 数据格式-通用 | Data format: General | 掩码与描述。 | — | —— |
| FMT-047 | 数据格式-转换 | Data format: Conversion | 加载/保存时的转换（含外部程序）。 | — | —— |
| FMT-048 | 数据格式-类型 | Data format: Type | 分隔符、引号、编码等类型参数（Delimited/Excel/HTML 等）。 | Delimited | 分隔符识别错误会导致整表错列 |
| FMT-049 | 数据格式的分隔符 | Delimiter | CSV/TSV 的字段分隔符。 | 逗号 | —— |
| FMT-050 | 数据格式的引号字符 | Quote character | 字段引号规则。 | 双引号 | —— |
| FMT-051 | 数据格式的首行作列名 | First row is column names | 是否把首行当作列名。 | 是 | 影响列对齐的列名推断 |
| FMT-052 | Excel 多工作表 | Excel multiple sheets | BC5 起 Table Compare 支持多 Excel 工作表。 | 支持 | 单表文件时无工作表标签 |
| FMT-053 | HTML 多表格 | HTML multiple tables | BC5 起支持比较 HTML 中的多个表格。 | 支持 | 需文件格式正确识别表格边界 |
| FMT-054 | 图片格式编辑 | Edit picture format | 图片格式定义界面。 | 内置常见图片格式 | —— |
| FMT-055 | 图片格式-通用 | Picture format: General | 掩码与描述。 | — | —— |
| FMT-056 | 图片格式-转换 | Picture format: Conversion | 图片加载/保存转换（含外部程序）。 | 无 | —— |
| FMT-057 | 文件格式与视图类型的绑定 | Format determines view type | 格式决定视觉比较使用哪种视图。 | — | 修改格式即可改变文件的默认打开视图 |
| FMT-058 | 文件格式影响文件夹内容比对 | Format affects folder rules-based comparison | 规则化内容比对依赖文件格式。 | 生效 | 格式配错会让文件夹层误判「相同」 |
| FMT-059 | 文件格式的导入导出 | Format import/export | 通过 `.bcpkg` 导出/导入文件格式定义。 | 支持 | 跨平台导入外部转换格式曾崩溃（已修） |
| FMT-060 | 文件格式的 XML 存储 | Format stored in BCFileFormats.xml | 全部格式定义持久化位置。 | — | 手工编辑需谨慎（XML 结构复杂） |
| FMT-061 | 关联格式（File Format Association） | File format association | 文件夹会话中某类文件与格式的关联。 | 按掩码 | 可在会话中覆盖（见 FILT-053） |
| FMT-062 | 转换的外部依赖检测 | External converter availability | 转换程序不可用时的降级行为。 | 报错/跳过 | 跨机器迁移时容易断链 |
| FMT-063 | 自定义格式的调试 | Format debugging | 验证掩码、转换、语法是否按预期生效。 | — | 需逐项排查 |
| FMT-064 | 格式的只读来源标记 | Built-in vs user format | 内置格式与用户格式的区分与保护。 | 内置受保护 | —— |
| FMT-065 | 二进制兜底格式 | Binary fallback | 未匹配任何格式时回退到二进制/十六进制视图。 | Hex Compare | —— |
| FMT-066 | 纯文本兜底格式 | Text fallback | 未匹配格式但内容像文本时回退到 Text Compare。 | Text Compare | —— |
| FMT-067 | 格式级忽略项与 Importance 的关系 | Format ↔ Importance | 格式定义语法，会话决定该语法元素是否重要。 | 分层 | 改错层会白费功夫 |
| FMT-068 | 表格列的列处理定义 | Column processing definition | 每列可定义处理方式（忽略大小写/忽略空白/类型转换等）。 | 默认逐字符比较 | 见 TBL 章 |
| FMT-069 | 表格列名推断 | Column name inference | 从左右文件自动推断列名，可用左文件填充或强制用比较列名。 | 自动 | 单侧文件时列名推断会退化 |
| FMT-070 | 表格列重命名 | Rename columns | 手工给比较列命名，或重置为自动命名。 | 自动 | —— |
| FMT-071 | 格式与文件掩码的匹配时机 | Mask matching timing | 打开文件/文件夹比较开始时按掩码选格式。 | 打开时 | 会话中途改格式需 Recompare |

---

## 15. 替换规则（Replacements）`[Pro]`

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REPL-001 | 替换规则列表 | Replacements | 定义「被视作不重要的重复性变化」的文本替换对。 | 空 | 是会话级设置，不是 File Format |
| REPL-002 | 新建替换规则 | New replacement | 添加一条替换规则。 | — | —— |
| REPL-003 | 查找文本 | Find text | 要匹配的文本。 | — | —— |
| REPL-004 | 替换文本 | Replace text | 匹配后等价看待的文本（留空表示视为删除）。 | — | 留空 ≠ 忽略该行 |
| REPL-005 | 匹配字符大小写 | Match character case | 仅匹配大小写完全一致的文本。 | 关闭 | 与 Importance 的 Character case 是两套机制 |
| REPL-006 | 仅完整词 | Whole words only | 防止只匹配单词的一部分。 | 关闭 | 关闭时 `cat` 会命中 `category` |
| REPL-007 | 正则表达式 | Regular expression | 把查找字符串按 PCRE 处理。 | 关闭 | 右侧下拉可插入常用正则元素 |
| REPL-008 | 正则元素插入下拉 | Regex element dropdown | 快速插入常用正则片段。 | — | —— |
| REPL-009 | 正则示例入口 | Regular Expression Examples | 跳转到正则示例帮助。 | — | 见 i18n/参考章 |
| REPL-010 | 应用侧选择 | Which side | 指定在哪个编辑器窗格中执行搜索。 | 双侧 | 配错侧规则不生效 |
| REPL-011 | 替换与导航 | Next/Previous Replacement | 命中替换的差异可单独导航。 | — | 需 Pro |
| REPL-012 | 替换的显示语义 | Unimportant replacement | 命中替换的差异被标记为不重要（蓝色）。 | 蓝色 | 开启 Ignore Unimportant 后不再显示 |
| REPL-013 | 行过滤器（Line Filters） | Line filters | 通过替换规则形式过滤整行（如忽略行号）。 | 需自定义 | 规则过宽会掩蔽真实差异 |
| REPL-014 | 忽略行号的典型规则 | Ignore line numbers | 用正则去掉行号前缀后再比较。 | 需自定义 | —— |
| REPL-015 | 忽略时间戳的典型规则 | Ignore timestamps | 用正则去掉生成时间戳。 | 需自定义 | 代码生成文件的常见需求 |
| REPL-016 | 替换规则的持久化 | Replacement persistence | 替换规则随会话保存。 | 保存 | —— |
| REPL-017 | 替换与 Importance 的边界 | Replacement vs Importance | Replacement 处理「值 A ↔ 值 B」；Importance 处理「类别是否重要」。 | 分层 | 概念重叠时优先用 Replacement |
| REPL-018 | 替换规则的正则替换字符串转义 | Regex replacement escapes | macOS/Linux 支持 `\n` `\r` `\t` 转义（BC 5.0.1 新增）。 | 支持 | 旧版本不支持 |
| REPL-019 | 替换规则与文件夹内容比对 | Replacement in folder rules-based compare | 文件夹会话的子文本比对会应用替换规则。 | 生效 | 作用域需选「对父会话内所有文件」 |

---

## 16. 报表与打印（Reports & Printing）

### 16.1 报表入口与输出目标

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REP-001 | 报表菜单入口 | Session > … Report | 各会话类型的会话菜单中提供对应报表命令。 | — | 命令名随会话类型不同 |
| REP-002 | 报表输出到打印机 | Output to printer | 直接打印报表。 | — | 打印色彩默认单色 |
| REP-003 | 报表输出到剪贴板 | Output to clipboard | 把报表文本/HTML 放入剪贴板。 | — | 大报表剪贴板可能失败 |
| REP-004 | 报表输出到文件 | Output to file | 保存为文件（文本或 HTML）。 | — | —— |
| REP-005 | 打印预览 | Print Preview | 预览打印效果。 | — | Windows 上曾出现缩放错误（5.2.1 修复） |
| REP-006 | 打印到 PDF | Print to PDF | 通过系统 PDF 打印机输出。 | — | 同上缩放问题 |
| REP-007 | 在浏览器中查看 | View in Browser | 预览 HTML 报表。 | — | —— |
| REP-008 | 报表标题 | Report title | 报表顶部显示的标题文本。 | 会话/文件相关 | —— |
| REP-009 | 报表样式中的颜色 | html-color / print-color | 报表使用彩色高亮差异。 | 打印默认单色 | HTML 默认彩色 |
| REP-010 | 报表样式中的单色 | html-mono / print-mono | 报表使用单色。 | 打印单色 | —— |
| REP-011 | 自定义 HTML 样式表 | html-custom | 使用外部 CSS 文件的 HTML 报表。 | — | 需提供样式表文件名或 URL |
| REP-012 | 打印方向 | print-portrait / print-landscape | 纵向 / 横向。 | 纵向 | 并排报表横向更合适 |
| REP-013 | 换行方式 | wrap-none / wrap-character / wrap-word | 报表长行的换行策略。 | wrap-none | 打印可用全部三种；HTML 只支持 none/word |

### 16.2 报表布局（Layouts）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REP-014 | 并排布局 | side-by-side | 左右两栏并列显示差异。 | 常用 | 长行需横向空间 |
| REP-015 | 摘要布局 | summary | 只输出差异摘要（不展开全文）。 | — | 信息量少 |
| REP-016 | 交错布局 | interleaved | 差异交错排列（BC4 起取代 over-under 与 composite）。 | — | 旧参数 `over-under`/`composite` 已弃用 |
| REP-017 | 补丁布局 | patch | 生成 Unix 风格补丁。 | — | 见域 17 |
| REP-018 | 统计布局 | statistics | 只输出统计数字。 | — | —— |
| REP-019 | XML 布局 | xml | 以 XML 结构化输出。 | — | 便于二次处理 |
| REP-020 | 十六进制交错布局 | interleaved (hex) | 十六进制报表的交错布局。 | — | —— |
| REP-021 | 表格交错布局 | interleaved (table) | 表格报表的交错布局。 | — | —— |
| REP-022 | 文件夹 XML 报表 | folder-report layout:xml | 文件夹比较的 XML 报表。 | — | —— |

### 16.3 报表选项（Options）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REP-023 | 显示差异项 | options:display-mismatches | 报表只输出有差异的部分。 | 常用 | —— |
| REP-024 | 显示所有 | options:display-all | 报表输出全部内容。 | — | 大文件报表巨大 |
| REP-025 | 显示上下文 | options:display-context | 输出差异周围的上下文行。 | — | 上下文行数由文件视图选项决定 |
| REP-026 | 忽略不重要差异 | options:ignore-unimportant | 报表忽略不重要差异。 | — | —— |
| REP-027 | 显示行号 | options:line-numbers | 报表输出行号。 | — | —— |
| REP-028 | 文件夹报表-版本列 | options:column-version | 文件夹报表包含版本列。 | 仅选中列 | —— |
| REP-029 | 文件夹报表-大小列 | options:column-size | 包含大小列。 | — | —— |
| REP-030 | 文件夹报表-时间戳列 | options:column-timestamp | 包含修改时间列。 | — | —— |
| REP-031 | 文件夹报表-CRC 列 | options:column-crc | 包含 CRC 列。 | — | 需先计算 CRC |
| REP-032 | 文件夹报表-属性列 | options:column-attributes | 包含属性列。 | — | Windows |
| REP-033 | 文件夹报表-无列 | options:column-none | 不输出任何列（只输出名称）。 | — | —— |
| REP-034 | 文件夹报表-只列当前工作表 | Just current sheet | BC5.0.1 新增：表格报表只输出当前工作表。 | — | 多表 Excel 的常见需求 |
| REP-035 | 表格报表的 sheet 表头 | Report sheet headers | 报表头部标明工作表名称。 | 输出 | 曾出现首个表名格式与其余不一致（已修） |
| REP-036 | 只输出选中项 | Just selection | 报表只包含当前选中项。 | — | 曾在第一个工作表之后崩溃（5.0.1 修复） |
| REP-037 | 报表的会话来源 | Comparison parameter | 报表命令可指定会话名或一对文件名。 | 当前会话 | —— |
| REP-038 | 报表统计与 Compare Info 的关系 | Report vs Compare Info | Compare Info 是即时统计；报表是落到文件/纸面的输出。 | 独立 | —— |

### 16.4 各类报表命令

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REP-039 | 文本比较报表 | text-report | 生成文本比较报表，支持 side-by-side / summary / interleaved / patch / statistics / xml。 | — | 布局参数必填 |
| REP-040 | 文件比较报表 | file-report | 基于当前选中文件生成报表，支持 side-by-side / summary。 | — | —— |
| REP-041 | 文件夹比较报表 | folder-report | 文件夹比较报表，支持 side-by-side / summary / xml。 | — | 包含子文件夹需先 `expand all` |
| REP-042 | 十六进制报表 | hex-report | 支持 side-by-side / summary / interleaved。 | — | —— |
| REP-043 | 表格报表 | data-report | 支持 side-by-side / summary / interleaved。 | — | —— |
| REP-044 | 图片报表 | picture-report | 支持 side-by-side / summary。 | — | —— |
| REP-045 | 媒体报表 | media-report | 支持 side-by-side / summary。 | — | —— |
| REP-046 | 注册表报表 | registry-report | 仅 Pro + Windows。 | — | —— |
| REP-047 | 版本报表 | version-report | 仅 Windows。 | — | —— |
| REP-048 | 报表输出到打印机的脚本语法 | `output-to:printer` | 脚本中直接送打印机。 | — | 静默运行时注意无纸化环境 |
| REP-049 | 报表输出到剪贴板的脚本语法 | `output-to:clipboard` | 输出到剪贴板。 | — | —— |
| REP-050 | 报表输出到文件 | `output-to:"<file>"` | 输出到指定文件。 | — | 路径含空格需引号 |
| REP-051 | 报表命令必须指定输出目标 | output-to required | 所有报表命令必须包含输出位置信息。 | 强制 | 漏写会报脚本语法错误 |

### 16.5 打印与页面设置

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REP-052 | 打印机选择 | Printer selection | 通过系统打印对话框选择打印机。 | 系统默认 | —— |
| REP-053 | 页眉页脚 | Header / Footer | 打印报表的页眉页脚设置。 | 系统/报表默认 | —— |
| REP-054 | 字体缩放 | Font scaling in print | 打印时的字体缩放。 | 自动 | Windows 上曾出现缩放错误（5.2.1 修复） |
| REP-055 | 多显示器打印预览 | Print preview multi-monitor | 在副显示器上预览的缩放。 | 自动 | Windows 副屏曾缩放错误（5.2.1 修复） |
| REP-056 | 打印色彩 | Color / Mono print | 彩色或单色打印。 | 单色 | —— |
| REP-057 | 打印的页面方向 | Portrait / Landscape | 纵向或横向。 | 纵向 | —— |
| REP-058 | 打印长行换行 | Wrap on print | 打印时对长行换行。 | 不换行 | 建议改为按字符/按词换行 |

### 16.6 报表的其它输出形式

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REP-059 | 文件夹清单导出 | Folder listing export | 通过文件夹报表导出文件清单（含大小/时间/CRC）。 | — | 常用 CSV/HTML 形式 |
| REP-060 | CSV 形式输出 | CSV-style output | 以逗号分隔形式导出（`/force` 冲突标记亦为 CSV 风格）。 | — | —— |
| REP-061 | 差异摘要文本复制 | Copy diff text | 把差异以纯文本形式复制（可直接贴入邮件/工单）。 | — | 无颜色信息 |
| REP-062 | 差异 HTML 复制 | Copy diff HTML | 保留差异着色的 HTML 复制。 | — | 部分目标不支持 |
| REP-063 | 报表与脚本的日志 | Report vs log | 报表是结果输出；日志是执行过程记录。 | 独立 | 两者常被混用 |
| REP-064 | 报表中的文件信息 | File info in report | 报表可包含文件名、编码、大小、时间等元信息。 | 部分 | —— |
| REP-065 | 报表的 XML 结构 | Report XML schema | XML 布局的字段结构。 | — | 结构随版本演进 |
| REP-066 | 报表的国际化 | Localized report headers | 报表表头随界面语言本地化。 | 本地化 | 机器解析时注意语言依赖 |
| REP-067 | 报表生成失败处理 | Report generation failure | 报表生成失败时报错并终止脚本。 | 报错 | Linux 曾出现生成报表崩溃/挂起（已修） |
| REP-068 | 报表的分页 | Report pagination | 打印时的分页策略。 | 自动 | —— |

---

## 17. 补丁文件（Patch Files）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| PATCH-001 | 生成补丁 | Generate patch | 生成内容差异的 Unix 风格补丁文件。 | — | 通过 Text Compare Report 的 patch 布局 |
| PATCH-002 | 补丁样式-普通 diff | Patch Style: Normal diff | 经典 `diff` 输出格式。 | 需选择 | —— |
| PATCH-003 | 补丁样式-统一 diff | Patch Style: Unified diff | `---/+++/@@` 形式（Git 常用）。 | — | VCS 集成的首选 |
| PATCH-004 | 补丁样式-上下文 diff | Patch Style: Context diff | 带上下文的 diff。 | — | —— |
| PATCH-005 | 补丁输出为纯文本 | Output Options: Plain text | 补丁必须保存为纯文本。 | — | HTML 输出无意义 |
| PATCH-006 | 多文件补丁生成 | Multi-file patch | 文件夹比较中 Expand All → Select All Files → File Compare Report → Patch。 | — | BC 生成多文件补丁但不支持应用多文件补丁 |
| PATCH-007 | 打开补丁文件 | Open .diff / .patch | 在 Text Patch 视图中打开补丁文件。 | 双击文件或 Tools > View Patch | —— |
| PATCH-008 | 应用补丁 | File > Apply Patch | 把补丁应用到指定原始文件。 | — | **仅支持单文件补丁**，多文件补丁不支持 |
| PATCH-009 | 补丁视图的导航 | Patch view navigation | 补丁视图中导航差异/差异段/书签。 | 同文本比较 | —— |
| PATCH-010 | 补丁计算的其他工具 | Third-party patch utility | 也可用 GNU patch 等工具应用。 | — | —— |
| PATCH-011 | 补丁的命令行打开 | Patch file parameter | 命令行直接传入 `.diff`/`.patch` 打开。 | — | —— |
| PATCH-012 | 补丁视图的显示选项 | Patch view display | 可见空白、行号、语法高亮、字体、并排/上下布局、缩略图、细节面板、标尺、工具栏。 | 同文本视图 | —— |
| PATCH-013 | 补丁视图的属性 | Patch view is read-only-ish | 补丁视图用于查看，编辑能力有限。 | — | —— |
| PATCH-014 | 补丁与版本控制的协作 | Patch in VCS workflow | 生成补丁分发后再用 BC 或 patch 应用。 | — | 跨平台行尾差异会破坏补丁 |

---

## 18. 十六进制比对（Hex Compare）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| HEX-001 | 十六进制转储布局 | Hex dump layout | 以典型 hex dump 形式逐字节展示并比对。 | 默认布局 | —— |
| HEX-002 | ASCII 对照区 | ASCII pane | 右侧显示对应可打印字符。 | 显示 | 非 ASCII 显示为点 |
| HEX-003 | 字节地址列 | Byte Address | 显示左侧字节偏移地址。 | 显示 | 可关闭 |
| HEX-004 | 当前字节地址 | Current byte address | 高亮当前字节的地址。 | — | —— |
| HEX-005 | 下一个差异字节 | Next Difference Byte | 定位到后一个差异字节。 | — | 粒度是字节而非行 |
| HEX-006 | 前一个差异字节 | Previous Difference Byte | 定位到前一个差异字节。 | — | —— |
| HEX-007 | 转到（字节偏移） | Go To | 定位到指定字节偏移。 | — | 支持十六进制输入 |
| HEX-008 | 显示所有 | Show All | 显示所有字节。 | — | —— |
| HEX-009 | 显示差异项 | Show Differences | 只显示有差异的项（按行块）。 | — | —— |
| HEX-010 | 显示相同项 | Show Same | 只显示没有差异的项。 | — | —— |
| HEX-011 | 不显示 | Show None | 隐藏所有项。 | 隐藏 | —— |
| HEX-012 | Little Endian 编码值 | Little Endian value | 按小端解释多字节数值，首字节放最后。 | 可切换 | —— |
| HEX-013 | Big Endian 编码值 | Big Endian value | 按大端解释，首字节放最前。 | 可切换 | 与上一项互斥 |
| HEX-014 | 并排布局 | Side-by-side Layout | 左右排列。 | 默认 | —— |
| HEX-015 | 上下布局 | Over-under Layout | 上下排列。 | — | —— |
| HEX-016 | 缩略图 | Thumbnail | 差异总览条。 | 显示 | —— |
| HEX-017 | 文件信息面板 | File info panel | 顶部显示文件信息。 | 可开关 | —— |
| HEX-018 | 显示字体 | Display Font | Hex 字体设置。 | 等宽 | 与 Editor Font / Listing Font 区分 |
| HEX-019 | 显示/隐藏空格 Tab | Visible Whitespace | 可见空白显示。 | — | 十六进制视图意义有限 |
| HEX-020 | 重新加载文件 | Reload Files | 重新读取文件。 | 提示保存 | —— |
| HEX-021 | 重新比较文件 | Recompare Files | 不重载直接重新比较。 | — | —— |
| HEX-022 | 交换两侧 | Swap Sides | 对调左右文件。 | — | —— |
| HEX-023 | 十六进制报表 | Hex Report | 生成十六进制比较报告。 | — | 支持 interleaved 布局 |
| HEX-024 | 十六进制比较信息 | Hex Compare Info | 统计信息。 | — | —— |
| HEX-025 | 用另一视图打开 | Compare Using | 用其它视图重新打开该文件对。 | — | —— |
| HEX-026 | 比较父文件夹 | Compare Parent Folder | 跳到父目录的文件夹比较。 | — | —— |
| HEX-027 | 打开文件 / 本地文件 / FTP / 剪贴板 | Open File variants | 多种文件来源打开方式。 | — | —— |
| HEX-028 | 用文本编辑打开 | Open With > Text Edit | 用内置文本编辑器打开。 | — | 二进制内容显示混乱 |
| HEX-029 | 用关联程序打开 | Open With > Associated App | 系统关联程序打开。 | — | —— |
| HEX-030 | 在资源管理器中显示 | Explorer `[Win]` | 系统文件管理器上下文菜单。 | — | —— |
| HEX-031 | 打印 vs 二进制 double-click | Double-click on tags | 从媒体/图片/版本视图双击标签会启动 Hex 或 Picture Compare（BC5 新增）。 | 支持 | —— |
| HEX-032 | 十六进制视图的沟槽 | Hex gutter | 行首显示差异指示。 | 显示 | —— |
| HEX-033 | 字节级差异着色 | Byte-level coloring | 差异字节以红/蓝着色。 | 显示 | 不重要差异是否着色取决于设置 |
| HEX-034 | 十六进制编辑器按键语义 | Hex editor key handling | `Shift+Tab` 保持选区、`Shift+←` 更新选区、`Delete` 在无选区时作用于下一个字节（BC5 修复）。 | — | 主要影响可编辑模式 |
| HEX-035 | 十六进制视图的编辑能力 | Hex editing | 在十六进制视图中直接编辑字节。 | 视版本/模式 | 需谨慎（直接改二进制） |
| HEX-036 | 十六进制大文件 | Large binary files | 大文件的加载与比较策略。 | 内存映射 | 见 FS 章 |
| HEX-037 | 十六进制视图的子会话导航 | Next/Previous Difference Files | 在父文件夹会话中跳转。 | 仅子会话 | —— |
| HEX-038 | 复制并跳转 | Copy file to side and open next difference | 复制当前文件并前进。 | 仅子会话 | —— |
| HEX-039 | 查找/查找下一个/上一个 | Find / Find Next / Previous | 在十六进制数据中查找文本。 | — | —— |
| HEX-040 | 十六进制会话设置 | Hex Compare Session Settings | 十六进制视图的设置对话框。 | — | 关键项较少 |
| HEX-041 | 十六进制视图的重比较 | Recompare without reload | 保留现状重新比较。 | — | —— |

---

## 19. 表格比对（Table Compare）

> BC4 之前称 **Data Compare**，BC5 全面重构：支持多个 Excel 工作表、多个 HTML 表格。文件内容按单元格比对，展示的是「比较列」而非文件原始列。

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TBL-001 | 单元格级比对 | Cell-by-cell comparison | 两个网格同步滚动，逐单元格比对。 | 核心行为 | —— |
| TBL-002 | 比较列（列映射） | Comparison columns | 显示的是比较列，可与文件原始列顺序不同（如左第 3 列 vs 右第 5 列）。 | — | 列映射配错会导致全表差异 |
| TBL-003 | 键列 | Key columns | 定义一或多个比较列作为键，只有键值匹配的行才对齐。 | 第一列为唯一键 | 键选错会大量「伪孤立行」 |
| TBL-004 | 多键列优先级 | Multiple key priority | 多键时按比较列顺序确定优先级。 | 列序 | —— |
| TBL-005 | 无键列时用全部列 | All-columns fallback | 未定义键列时可用所有列唯一定义一行。 | 兜底 | —— |
| TBL-006 | 未排序对齐 | Unsorted alignment | 强制保持文件中行的原始顺序，不按键排序。 | 关闭 | 相同键值的行不会被对齐 |
| TBL-007 | 行排序 | Row sorting | 默认在对齐前行按键排序。 | 开启 | 关闭排序需接受「同键不对齐」 |
| TBL-008 | 左列排序调整 | Left file column order | 用上移/下移按钮调整左文件中的比较顺序。 | — | —— |
| TBL-009 | 移除左侧某列 | Remove left column | 定位到左文件下移除按钮，防止该列参与比较。 | — | 移除后需用「插入」恢复 |
| TBL-010 | 恢复被移除的列 | Insert column | 右键选择 Insert 勾选要恢复的列。 | — | —— |
| TBL-011 | 整理列表 | Tidy list | 移除比较列列表中的空白行。 | — | —— |
| TBL-012 | 列名自动推断 | Automatic column naming | 默认从左侧或右侧文件自动确定列名。 | 自动 | 单侧文件时无法推断 |
| TBL-013 | 列名自定义 | Custom column name | 手工给比较列命名。 | — | —— |
| TBL-014 | 重设列名 | Reset names | 把所有列恢复为自动命名。 | — | —— |
| TBL-015 | 用左侧文件填充列名 | Fill names with left file | 用左侧文件内容填充列名。 | — | 需左侧数据可用 |
| TBL-016 | 用比较列填充列名 | Fill names with comparison | 强制用左侧名称作为比较列名。 | — | —— |
| TBL-017 | 列处理 | Column processing | 每列可定义处理方式（忽略大小写、忽略空白、类型转换等）。 | 逐字符比较 | 需通过列处理对话框配置 |
| TBL-018 | 列处理对话框 | Column processing dialog | 编辑某列处理设置的对话框。 | — | —— |
| TBL-019 | 列映射的交换（Swap Sides） | Swap sides column mapping | 交换两侧后列映射同步更新。 | 同步 | 表头标签在交换后曾不更新（已修） |
| TBL-020 | 工作表标签 | Sheet tabs | BC5 支持多个 Excel 工作表 / 多个 HTML 表格，以标签切换。 | 支持 | 需文件包含多个表 |
| TBL-021 | 工作表标签宽度自适应 | Sheet tab auto width | 标签宽度随标题或差异点变化重算。 | 自动 | 曾出现宽度不刷新（5.0.1 修复） |
| TBL-022 | 当前工作表 | Current sheet | 视图显示当前选中的工作表比较。 | — | 报表需指定「仅当前工作表」 |
| TBL-023 | 隐藏相同列 | Hide Same Columns | 隐藏两侧完全相同的列。 | 关闭 | 单侧比对时曾出现全部隐藏（5.0.1 修复） |
| TBL-024 | 列宽自适应 | Adjust column widths | 按内容调整列宽以完整显示数据。 | 手动触发 | —— |
| TBL-025 | 列显隐 | View > Columns | 显示或隐藏列。 | — | —— |
| TBL-026 | 行号 | Line Numbers | 显示/隐藏行号。 | 关闭 | —— |
| TBL-027 | 可见空白 | Visible Whitespace | 显示/隐藏空格与 Tab。 | 关闭 | —— |
| TBL-028 | 缩略图 | Thumbnail | 差异总览条（按行）。 | 显示 | —— |
| TBL-029 | 行细节 | Row Details | 底部显示当前行的细节面板。 | 可开关 | —— |
| TBL-030 | 文件信息面板 | File Info panel | 顶部显示文件信息。 | 可开关 | 曾出现信息栏显示错误/截断（5.0.1 修复） |
| TBL-031 | 显示字体 | Display Font | 表格显示字体。 | 等宽 | —— |
| TBL-032 | 并排布局 | Side-by-side Layout | 左右排列。 | 默认 | —— |
| TBL-033 | 上下布局 | Over-under Layout | 上下排列。 | — | —— |
| TBL-034 | 显示所有 | Show All | 显示所有行。 | — | —— |
| TBL-035 | 显示差异项 | Show Differences | 只显示有差异的行。 | — | —— |
| TBL-036 | 显示相同项 | Show Same | 只显示无差异的行。 | — | —— |
| TBL-037 | 不显示 | Show None | 隐藏所有行。 | 隐藏 | —— |
| TBL-038 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要差异视为相同。 | 关闭 | —— |
| TBL-039 | 下一个 / 上一个不同的行 | Next / Previous Difference Row | 在差异行间跳转。 | — | —— |
| TBL-040 | 下一个 / 上一个编辑项 | Next / Previous Edit | 在手工编辑行间跳转。 | — | —— |
| TBL-041 | 复制到另一侧 | Copy to Other Side | 把选中行复制到相反侧。 | 动态标题 | —— |
| TBL-042 | 复制到右侧 / 左侧 | Copy to Right / Copy to Left | 显式方向复制。 | 默认隐藏 | —— |
| TBL-043 | 新建文件 | File > New File | 在选定窗格中新建空白表格文件。 | — | —— |
| TBL-044 | 打开文件 / 本地 / FTP / 剪贴板 | Open variants | 多种数据来源。 | — | 剪贴板来源无文件格式信息 |
| TBL-045 | 保存 / 另存 / 另存到文件系统 / FTP | Save variants | 表格结果的保存方式。 | — | —— |
| TBL-046 | 编辑单元格 | Edit cell | 在单元格内切换编辑模式并修改。 | — | 空编辑器中按 Enter 曾崩溃（已修） |
| TBL-047 | 插入行 | Insert row | 插入新行。 | — | —— |
| TBL-048 | 剪切 / 复制 / 粘贴 / 删除 | Standard edit | 标准编辑命令。 | — | 粘贴多行时对齐关系会变 |
| TBL-049 | 撤销 / 恢复 | Undo / Redo | 标准撤销重做。 | 多级 | —— |
| TBL-050 | 选中所有 | Select All | 选中当前窗格所有可见行。 | — | —— |
| TBL-051 | 表格报表 | data-report | 支持 side-by-side / summary / interleaved。 | — | —— |
| TBL-052 | 表格比较信息 | Table Compare Info | 统计信息（支持多表）。 | — | 多表信息曾有缺陷（5.0.1 修复） |
| TBL-053 | 比较父文件夹 / 用另一视图打开 | Compare Parent Folder / Compare Using | 导航与视图切换。 | — | —— |
| TBL-054 | 重新加载 / 重新比较 | Reload / Recompare | 重载或原地重比较。 | — | —— |
| TBL-055 | 表格颜色语义 | Table coloring | 重要差异红色、不重要差异蓝色，沟槽色点表示行状态。 | 预设 | 可自定义 |
| TBL-056 | 表格单元格限制 | Sheet/row/column limits | 单表行列数量上限与性能表现。 | — | 超大表性能敏感 |
| TBL-057 | 表格列对齐的大小写/空白忽略 | Case/whitespace-insensitive column matching | BC5.2 起，按列名对齐时忽略大小写与空白。 | 开启 | 旧版本行为不同 |

---

## 20. 图片比对（Picture Compare）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| PIC-001 | 并排图片显示 | Side-by-side picture view | 左右并排显示两张图片并高亮差异。 | 默认 | —— |
| PIC-002 | 容差模式 | Tolerance Mode | 检测像素差异是否大于容差。 | **默认模式** | 容差设大会漏掉细微差异 |
| PIC-003 | 不匹配范围模式 | Mismatch Range Mode | 测量像素差异的程度（差异幅度）。 | 可选 | 与容差模式的输出语义不同 |
| PIC-004 | 二进制操作模式 | Binary Operations Mode | 对两张图执行位运算后显示结果。 | 可选 | 结果是运算图，非「差异图」 |
| PIC-005 | 混合模式 | Blended Mode | 按可配置百分比混合两张图。 | 可选 | —— |
| PIC-006 | 混合百分比 | Blend percentage | 混合模式下的混合比例。 | 50% | —— |
| PIC-007 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要差异视为相同。 | 关闭 | —— |
| PIC-008 | 拉伸到相同大小 | Stretch to Same Size | 强制两图采用相同宽高后再比对。 | 关闭 | 会引入因缩放产生的伪差异 |
| PIC-009 | 顺时针旋转 | Rotate Clockwise | 当前图片顺时针旋转 90°。 | — | 只影响显示/一侧 |
| PIC-010 | 逆时针旋转 | Rotate Counter-Clockwise | 当前图片逆时针旋转 90°。 | — | —— |
| PIC-011 | 水平翻转 | Flip Horizontal | 以竖直中线翻转像素。 | — | —— |
| PIC-012 | 竖直翻转 | Flip Vertical | 以水平中线翻转像素。 | — | —— |
| PIC-013 | 重设差异偏移 | Reset Difference Offset | 重新对齐两侧图片的左上角。 | — | 手动画布偏移后需重置 |
| PIC-014 | 差异偏移/手动平移 | Difference offset | 手工平移一侧图片以消除错位。 | — | 偏移后需 Reset 恢复 |
| PIC-015 | 并排布局差异 | Side-by-side with difference | 左右排列，差异窗格位于中间。 | 可选 | —— |
| PIC-016 | 上下布局差异 | Over-under with difference | 上下排列，差异窗格位于中间。 | 可选 | —— |
| PIC-017 | 下方布局差异 | Difference below | 差异窗格排在左右窗格下面。 | 可选 | —— |
| PIC-018 | 只显示差异布局 | Difference only | 只显示差异窗格，隐藏左右窗格。 | 可选 | —— |
| PIC-019 | 并排布局 | Side-by-side Layout | 仅左右两栏。 | 默认 | —— |
| PIC-020 | 上下布局 | Over-under Layout | 仅上下两栏。 | — | —— |
| PIC-021 | 放大 | Zoom In | 放大显示。快捷键 `Ctrl+/⌘+`。 | 支持 | —— |
| PIC-022 | 缩小 | Zoom Out | 缩小显示。快捷键 `Ctrl-/⌘-`。 | 支持 | —— |
| PIC-023 | 自动缩放 | Auto Zoom | 使图片正好适合面板。 | 可选 | —— |
| PIC-024 | 图片文件信息面板 | File Info panel | 顶部显示图片信息（尺寸、格式等）。 | 可开关 | —— |
| PIC-025 | 图片比较报表 | Picture Report | 支持 side-by-side / summary。 | — | —— |
| PIC-026 | 图片比较信息 | Picture Compare Info | 统计信息（差异像素数等）。 | — | —— |
| PIC-027 | 图片双击标签启动视图 | Double-click on tags | 双击标签启动 Hex 或 Picture Compare（BC5）。 | 支持 | —— |
| PIC-028 | 图片打开（本地/FTP/剪贴板） | Open variants | 多种来源。 | — | 剪贴板图片无文件名 |
| PIC-029 | 图片交换两侧 | Swap Sides | 对调左右图片。 | — | 隐藏项时曾崩溃（已修） |
| PIC-030 | 图片重新加载 / 重新比较 | Reload / Recompare | 重载或原地重比较。 | — | —— |
| PIC-031 | 图片比较设置 | Picture session settings | Specs / Format / Comparison 三部分。 | — | —— |
| PIC-032 | 图片比较模式设置 | Picture comparison settings | 各模式的具体参数（容差阈值、位运算类型等）。 | — | 很多参数也可在工具栏切换 |
| PIC-033 | 图片视图缩放与拖动 | Pan and zoom | 拖动平移、缩放查看。 | — | —— |
| PIC-034 | 图片比较的子会话导航 | Next/Previous Difference Files | 父文件夹会话中跳转。 | 仅子会话 | —— |
| PIC-035 | 图片复制到另一侧 | Copy file to side | 复制图片文件到另一侧。 | 仅子会话 | —— |
| PIC-036 | 图片比较的颜色 | Picture diff coloring | 差异像素的着色方式。 | 预设 | 可在 File View Colors 中定义 |
| PIC-037 | 支持的图片格式 | Supported image formats | PNG/JPG/GIF/BMP/TIFF/SVG 等。 | 由 File Format 决定 | macOS 上加载 SVG 曾崩溃（5.2.1 修复） |
| PIC-038 | 图片尺寸差异处理 | Different dimensions | 尺寸不同时的差异判定。 | 判为差异 | 需 Stretch 才能像素级比对 |
| PIC-039 | 图片元数据比对 | Picture metadata | 是否比对 EXIF 等元数据。 | 不比对 | 元数据差异需靠其它视图 |
| PIC-040 | 图片视图的复制选中内容 | Copy | 复制选中内容到剪贴板。 | — | —— |
| PIC-041 | 图片视图用关联程序打开 | Open With | 用系统程序打开图片。 | — | —— |
| PIC-042 | 图片视图在资源管理器显示 | Explorer `[Win]` | 系统文件管理器。 | — | —— |
| PIC-043 | 图片比较的像素差异阈值 | Pixel tolerance threshold | 容差模式下的像素比较阈值。 | 内置 | 需按用途调整 |
| PIC-044 | 图标/小图比对性能 | Small image performance | 图标类小图的大量比对。 | — | 文件夹会话中批量图片比对的性能 |

---

## 21. 媒体比对（Media Compare）

> BC5 由 **MP3 Compare** 更名而来，扩展支持 FLAC、MP3、MP4/AAC。比对对象是**标签与元数据**，不是音频波形。

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MED-001 | 媒体标签比对 | Media tag comparison | 比对媒体文件的标签字段并高亮差异区域。 | 核心行为 | 不比对音频数据 |
| MED-002 | 支持的格式-MP3 | MP3 (ID3v2) | 支持 MP3 及其 ID3 标签。 | 支持 | 依赖 ID3v2 库（5.5.x 更新至 2.1.95） |
| MED-003 | 支持的格式-FLAC | FLAC | BC5 新增支持。 | 支持 | —— |
| MED-004 | 支持的格式-MP4/AAC | MP4 / AAC | BC5 新增支持。 | 支持 | 依赖 MP4Tag 库（更新至 v1.0.68） |
| MED-005 | 标签重要性设置 | Media Importance | 定义哪些标签字段算重要差异。 | 常见字段重要 | 高 DPI 下树形缩进宽度曾异常（5.2.1 修复） |
| MED-006 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要标签差异视为相同。 | 关闭 | —— |
| MED-007 | 该项为重要 | Mark item important | 标记选中项为重要或不重要。 | — | 会话级临时覆盖 |
| MED-008 | 播放 | Play | 播放当前侧音频。 | — | 需系统解码器 |
| MED-009 | 播放两侧 | Play Both Sides | 同时播放两侧音轨。 | — | —— |
| MED-010 | 标签字段列表 | Tag field list | 展示文件中的所有标签字段与值。 | 按文件实际标签 | 不同文件标签集合可能不同 |
| MED-011 | 媒体比较报表 | Media Report | 支持 side-by-side / summary。 | — | —— |
| MED-012 | 媒体比较信息 | Media Compare Info | 统计信息。 | — | —— |
| MED-013 | 双击标签启动其它视图 | Double-click on tags | 双击标签启动 Hex 或 Picture Compare（BC5）。 | 支持 | —— |
| MED-014 | 打开文件 / 本地 / FTP | Open variants | 多种来源。 | — | 远程媒体文件播放需先缓冲 |
| MED-015 | 用关联程序打开 | Open With | 系统关联播放器打开。 | — | —— |
| MED-016 | 在资源管理器中显示 | Explorer `[Win]` | 系统文件管理器。 | — | —— |
| MED-017 | 下一个 / 上一个差异项 | Next / Previous Difference | 在差异标签间跳转。 | — | —— |
| MED-018 | 查找 / 查找下一个 / 上一个 | Find | 在标签文本中查找。 | — | —— |
| MED-019 | 复制选中内容 | Copy | 复制标签文本到剪贴板。 | — | —— |
| MED-020 | 显示所有 / 差异项 / 相同项 / 不显示 | Display filters | 标准显示过滤器。 | — | —— |
| MED-021 | 可见空白 | Visible Whitespace | 显示/隐藏空白。 | 关闭 | —— |
| MED-022 | 显示字体 | Display Font | 标签列表字体。 | — | —— |
| MED-023 | 并排 / 上下布局 | Layouts | 左右或上下排列。 | 并排 | —— |
| MED-024 | 项目细节 | Item details | 视图底部显示项的详细信息。 | 可开关 | —— |
| MED-025 | 文件信息面板 | File info panel | 顶部显示文件信息。 | 可开关 | —— |
| MED-026 | 媒体交换两侧 | Swap Sides | 对调左右媒体文件。 | — | 隐藏项时曾崩溃（已修） |
| MED-027 | 媒体重新加载 / 重新比较 | Reload / Recompare | 重载或原地重比较。 | — | —— |
| MED-028 | 媒体会话设置 | Media session settings | 标签重要性与其它设置。 | — | —— |
| MED-029 | 媒体比较的子会话导航 | Next/Previous Difference Files | 父文件夹会话中跳转。 | 仅子会话 | —— |
| MED-030 | 标签值的编码 | Tag value encoding | 不同 ID3 版本的文本编码处理。 | 自动 | ID3v1/v2 编码混乱是常见问题 |
| MED-031 | 媒体文件在文件夹会话中的比对 | Media in folder rules-based comparison | 文件夹会话中媒体文件的规则化比对。 | 依设置 | 需对应 File Format 关联 |

---

## 22. 注册表比对（Registry Compare）`[Pro] [Win]`

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REG-001 | 实时注册表比对 | Live registry comparison | 比对本地或远程的实时注册表。 | Pro + Win | 需相应权限 |
| REG-002 | 注册表文件比对 | Registry file comparison | 比对 `.reg` 导出文件。 | 支持 | 文件与实时注册表的键路径基准不同 |
| REG-003 | 打开注册表 | Open Registry | 在选定窗格打开实时注册表。 | — | —— |
| REG-004 | 在文件系统打开注册表文件 | Open Registry File in File System | 打开本地注册表文件。 | — | —— |
| REG-005 | 在 FTP 站点打开注册表文件 | Open Registry File on FTP Site | 打开远程注册表文件。 | — | —— |
| REG-006 | 用文本编辑打开 | Open With > Text Edit | 用文本编辑器查看。 | — | .reg 文本与树形视图语义不同 |
| REG-007 | 用关联程序打开 | Open With > Associated App | 系统关联程序打开。 | — | —— |
| REG-008 | 设为基准键 | Set as Base Key | 把选定键设为本侧基准键。 | — | —— |
| REG-009 | 在另一侧设置基准键 | Set Base Key on Other Side | 把选定键设为另一侧基准。 | — | —— |
| REG-010 | 设为两个基准键 | Set as Two Base Keys | 把选中的两个键分别设为左右基准。 | — | —— |
| REG-011 | 向上浏览（左/右/两侧） | Up One Level | 基准键上移到父键。 | — | 根键不可再上移 |
| REG-012 | 新建键 | New Key | 添加新键。 | — | 直接修改注册表，风险高 |
| REG-013 | 新建值 | New Value | 添加新值。 | — | 同上 |
| REG-014 | 修改值 | Modify | 修改当前值。 | — | 同上 |
| REG-015 | 删除 | Delete | 删除选中的键/值。 | — | 删除注册表键不可逆 |
| REG-016 | 重命名 | Rename | 重命名当前项。 | — | —— |
| REG-017 | 复制键名 | Copy Key Name | 把当前键名复制到剪贴板。 | — | —— |
| REG-018 | 导出 | Export | 把当前键值导出为注册表文件。 | — | —— |
| REG-019 | 导出全部 | Export All | 把所有键值导出为注册表文件。 | — | —— |
| REG-020 | 复制到右侧 / 左侧 | Copy to Right / Left | 把选中项复制到指定侧。 | 默认隐藏 | 会写入目标注册表 |
| REG-021 | 复制到另一侧 | Copy to Other Side | 动态方向复制。 | 动态标题 | —— |
| REG-022 | 撤销 / 恢复 | Undo / Redo | 撤销重做编辑。 | 支持 | —— |
| REG-023 | 展开所有 / 折叠所有 | Expand All / Collapse All | 树形展开折叠。 | — | —— |
| REG-024 | 下一个 / 上一个差异项 | Next / Previous Difference | 在差异键值间跳转。 | — | —— |
| REG-025 | 查找 / 查找下一个 / 上一个 | Find | 查找键或值文本。 | — | —— |
| REG-026 | 显示所有 / 差异项 / 相同项 / 不显示 | Display filters | 标准显示过滤器。 | — | —— |
| REG-027 | 可见空白 | Visible Whitespace | 显示/隐藏空白。 | 关闭 | —— |
| REG-028 | 显示字体 | Display Font | 列表字体。 | — | —— |
| REG-029 | 并排 / 上下布局 | Layouts | 左右或上下排列。 | 并排 | —— |
| REG-030 | 文本细节面板 | Text Details | 底部显示值的文本细节。 | 可开关 | —— |
| REG-031 | 十六进制细节面板 | Hex Details | 底部显示值的十六进制（二进制值类型常用）。 | 可开关 | —— |
| REG-032 | 文件信息面板 | File Info panel | 顶部文件信息。 | 可开关 | —— |
| REG-033 | 注册表比较报表 | Registry Report | 支持 side-by-side / summary，仅 Pro + Windows。 | — | —— |
| REG-034 | 注册表比较信息 | Registry Compare Info | 统计信息。 | — | —— |
| REG-035 | 深入探索 | Deep Zoom / Drill down | 展开查看键的完整路径。 | — | —— |
| REG-036 | 注册表值的类型比对 | Registry value types | REG_SZ / REG_DWORD / REG_BINARY / REG_MULTI_SZ 等类型比对。 | 类型不同即差异 | 二进制值用 Hex Details 查看 |
| REG-037 | 在资源管理器中显示 | Explorer `[Win]` | 系统文件管理器。 | — | 对实时注册表无意义 |

---

## 23. 版本比对（Version Compare）`[Win]`

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| VER-001 | 可执行文件版本比对 | Executable version comparison | 比对 exe/dll/ocx 的版本信息资源。 | Win | 仅 Windows |
| VER-002 | 版本字段列表 | Version field list | 展示所有版本字段（FileVersion、ProductVersion、CompanyName 等）。 | 全部字段 | —— |
| VER-003 | 更多头部字段（BC5） | More header fields | BC5 支持更多头部字段。 | 增强 | 旧版本字段少 |
| VER-004 | MUI 处理（BC5） | MUI handling | BC5 改进的多语言资源处理。 | 增强 | 多语言 exe 的字段名本地化 |
| VER-005 | 错误处理（BC5） | Error handling | BC5 改进的解析错误处理。 | 增强 | —— |
| VER-006 | 该项为重要 | Mark item important | 标记选中项为重要/不重要。 | — | —— |
| VER-007 | 忽略不重要差异项 | Ignore Unimportant Differences | 把不重要差异视为相同。 | 关闭 | —— |
| VER-008 | 项目细节 | Item details | 底部显示项细节。 | 可开关 | —— |
| VER-009 | 版本比较信息 | Version Compare Info | 统计信息。 | — | —— |
| VER-010 | 版本比较报表 | Version Report | 支持 side-by-side / summary。 | — | —— |
| VER-011 | 下一个 / 上一个差异项 | Next / Previous Difference | 跳转差异字段。 | — | —— |
| VER-012 | 查找 / 查找下一个 / 上一个 | Find | 在字段文本中查找。 | — | —— |
| VER-013 | 复制选中内容 | Copy | 复制字段文本。 | — | —— |
| VER-014 | 显示所有 / 差异项 / 相同项 / 不显示 | Display filters | 标准过滤器。 | — | —— |
| VER-015 | 可见空白 / 显示字体 | Display options | 显示选项。 | — | —— |
| VER-016 | 并排 / 上下布局 | Layouts | 排列方式。 | 并排 | —— |
| VER-017 | 文件信息面板 | File info panel | 顶部文件信息。 | 可开关 | —— |
| VER-018 | 打开文件变体 | Open variants | 本地/FTP。 | — | —— |
| VER-019 | 用关联程序打开 | Open With | 系统程序。 | — | 运行不可信可执行文件有风险 |
| VER-020 | 在资源管理器中显示 | Explorer | 系统文件管理器。 | — | —— |
| VER-021 | 重新加载 / 重新比较 | Reload / Recompare | 重载或原地重比较。 | — | —— |
| VER-022 | 交换两侧 | Swap Sides | 对调左右。 | — | 消息表隐藏项时曾崩溃（已修） |
| VER-023 | 版本会话设置 | Version: Specs / Importance | 路径规范与重要性设置。 | — | —— |
| VER-024 | 双击标签启动其它视图 | Double-click on tags | 启动 Hex 或 Picture Compare（BC5）。 | 支持 | —— |
| VER-025 | 比较父文件夹 | Compare Parent Folder | 跳到父目录比较。 | — | —— |
| VER-026 | 版本比较的子会话导航 | Next/Previous Difference Files | 父文件夹会话中跳转。 | 仅子会话 | —— |
| VER-027 | 复制的文件到另一侧 | Copy file to side | 复制可执行文件到另一侧。 | 仅子会话 | —— |
| VER-028 | 版本信息的编码 | Version info encoding | 版本字符串的编码/代码页处理。 | 自动 | 非拉丁字符需注意 |

---

## 24. 文本编辑视图（Text Edit）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| TE-001 | 文本编辑视图 | Text Edit view | 单窗格文本编辑器，可独立打开/编辑/保存文件。 | 通过 Tools > Edit Text Files 打开 | 无差异比对能力 |
| TE-002 | 打开文件 | File > Open File | 打开本地或远程文件。 | — | —— |
| TE-003 | 打开剪贴板 | Open Clipboard | 把剪贴板内容作为编辑内容（BC5 新增到 Text Edit）。 | 支持 | —— |
| TE-004 | 保存 / 另存为 | Save / Save As | 保存编辑结果。 | — | —— |
| TE-005 | 用关联程序打开 | Open With | 系统程序打开。 | — | —— |
| TE-006 | 撤销 / 恢复 / 剪切 / 复制 / 粘贴 / 删除 | Standard edit | 标准编辑命令。 | 多级撤销 | —— |
| TE-007 | 查找 / 替换 / 转到 | Find / Replace / Go To | 搜索与定位。 | — | —— |
| TE-008 | 书签 | Bookmarks | 设置/跳转/清除书签（0–9）。 | — | —— |
| TE-009 | 可见空白 | Visible Whitespace | 显示空格与 Tab 标记。 | 关闭 | —— |
| TE-010 | 行号 | Line Numbers | 显示/隐藏行号。 | 关闭 | —— |
| TE-011 | 语法高亮 | Syntax Highlighting | 按 File Format 语法着色。 | 开启 | —— |
| TE-012 | 显示字体 / 字号增减 / 重置 | Display Font controls | 字体与字号控制。 | — | —— |
| TE-013 | 完整编辑 | Full Edit | 字符级完整编辑与行级编辑切换。 | 开启 | —— |
| TE-014 | 增加 / 减小缩进 | Increase / Decrease Indent | 缩进调整。 | — | —— |
| TE-015 | 行操作 | Line operations | 删除行/到行首/到行尾/单词等，插入行前后。 | — | —— |
| TE-016 | 选中所有 | Select All | 全选。 | — | —— |
| TE-017 | 文本转换 | Transform File | 剪裁尾随空格、前导空格转 Tab、Tab 转空格、转换行尾。 | — | 直接改内容 |
| TE-018 | 自动换行 | Word Wrap | 折行显示。 | 可能默认关 | —— |
| TE-019 | 工具栏显隐 | Toolbar | 显示/隐藏工具栏。 | 显示 | —— |
| TE-020 | 与比较视图的互跳 | Open With / Compare | 从 Text Edit 转到比较视图。 | — | 转过去后不再是单文件编辑 |
| TE-021 | 大文件编辑 | Large file editing | 超大文本的编辑与渲染。 | — | 性能敏感 |
| TE-022 | 文件编码与行尾 | Encoding and line endings | 编辑时的编码与行尾处理。 | 依 File Format | 保存时可能改变编码 |
| TE-023 | 文本编辑的会话保存 | Save Text Edit session | 保存该编辑视图的会话（如启用）。 | — | —— |

---

## 25. 压缩包内比对（Archives）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| ARC-001 | 压缩包作为文件 | Archive as file | 压缩包默认显示为带压缩图标的文件夹，但在复制/比较时按普通文件处理。 | **默认** | 内容比对需先展开 |
| ARC-002 | 压缩包作为文件夹 | Archive as folder | 双击展开压缩包，与其中文件比较。 | 双击进入 | 大压缩包首次展开慢 |
| ARC-003 | 压缩包内文件直接打开 | Open file inside archive | 以 `C:\MyArchive.zip\MyFile.txt` 形式作为文件 Spec。 | 支持 | 保存时回写压缩包 |
| ARC-004 | 压缩包内容的读写 | Read/write archives | 很多格式支持读写，普通文件操作（内容比较、复制、重命名）同样适用。 | 视格式 | 部分格式只读 |
| ARC-005 | 压缩包比较状态 | Archive status | 「打开后作为文件夹」模式下，打开后压缩包的比较状态变为其内容的状态。 | 自动 | 未打开时为未知 |
| ARC-006 | 支持-7-Zip | 7-Zip (`*.7z;*.7z.001`) `[Win]` | 支持 7z 及其分卷。 | 支持 | Windows DLL |
| ARC-007 | 支持-BZip | BZip (`*.bz;*.bz2`) | 支持 bzip2。 | 支持 | —— |
| ARC-008 | 支持-BZipped Tar | BZipped Tar (`*.tbz;*.tbz2;*.tar.bz2`) | 支持。 | 支持 | —— |
| ARC-009 | 支持-Snapshot | Beyond Compare Snapshot (`*.bcss;*.bcs`) | 支持 BC 快照文件。 | 支持 | 只含元数据，无文件内容 |
| ARC-010 | 支持-Compiled HTML Help | CHM (`*.chm`) `[Win]` | 支持 CHM。 | 支持 | Windows |
| ARC-011 | 支持-Debian 包 | Debian Packages (`*.deb`) `[Linux]` | 支持 deb。 | 支持 | Linux |
| ARC-012 | 支持-GZip | GZip (`*.gz`) | 支持。 | 支持 | —— |
| ARC-013 | 支持-GZipped Tar | GZipped Tar (`*.tgz;*.tar.gz`) | 支持。 | 支持 | —— |
| ARC-014 | 支持-Microsoft Cabinet | CAB (`*.cab`) `[Win]` | 支持。 | 支持 | Windows |
| ARC-015 | 支持-RAR | RAR (`*.rar`) `[Win]` | 支持（UnRAR v7.x）。 | 支持 | 5.0.1 更新 UnRAR；macOS 读 rar 曾修复 |
| ARC-016 | 支持-Red Hat 包 | RPM (`*.rpm`) `[Linux]` | 支持。 | 支持 | Linux |
| ARC-017 | 支持-Tar | Tar (`*.tar`) | 支持。 | 支持 | —— |
| ARC-018 | 支持-Zip | Zip (`*.zip;*.jar;*.ear;*.war;*.bcpkg`) | 支持多种 Zip 变体。 | 支持 | `.bcpkg` 同时是设置包 |
| ARC-019 | 压缩包处理策略设置 | Archive handling option | Handling 页中设置「始终作为文件 / 打开后作为文件夹 / 始终作为文件夹」。 | 始终作为文件 | 大压缩包选「始终作为文件夹」会很慢 |
| ARC-020 | Total Commander Packer 插件 | TC "packer" plugins `[Win]` | 支持 Total Commander packer 插件以扩展压缩格式（如 ISO、MSI）。 | 需安装 | 通过 Tools > Options > Folder Views > 添加插件 |
| ARC-021 | 归档格式的添加插件入口 | Add plugin | 在 Options > Folder Views 中点击添加插件并按提示操作。 | — | 插件兼容性风险 |
| ARC-022 | 压缩包内的内容比对 | Compare contents within archive | 对压缩包内文件执行 CRC/二进制/规则化比对。 | 支持 | 需先解压到临时目录 |
| ARC-023 | 压缩包内的复制/移动/删除/重命名 | Archive file operations | 在压缩包内执行文件操作（回写压缩包）。 | 视格式 | 只读格式会失败 |
| ARC-024 | 压缩包与普通文件夹混比 | Compare archive with folder | 一侧为压缩包、另一侧为文件夹的比较。 | 支持 | 需把压缩包作为文件夹处理 |
| ARC-025 | 压缩包内文件的编码/时间戳 | Archive entry metadata | 压缩包内条目保留的时间戳与属性。 | 依格式 | Zip 时间戳精度 2 秒 |
| ARC-026 | 压缩包密码 | Password-protected archives | 受密码保护的压缩包处理。 | 提示输入 | 无法保存到会话 |

---

## 26. 快照（Snapshots）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SNAP-001 | 保存快照 | Tools > Save Snapshot | 创建文件夹结构的只读快照。 | — | —— |
| SNAP-002 | 快照内容 | Snapshot contents | 只包含文件名、日期、大小（不含文件内容本身）。 | 元数据 | 不能用于内容恢复 |
| SNAP-003 | 快照体积 | Snapshot size | 由于只存元数据，快照体积极小。 | 很小 | 可把整个硬盘信息存进一个快照 |
| SNAP-004 | 快照用途 | Snapshot purpose | 用于日后与当前状态比较，判断哪些文件变化/消失。 | — | 是审计与备份校验的轻量方案 |
| SNAP-005 | 快照文件扩展名 | `.bcss` / `.bcs` | 默认扩展名；无扩展名时自动用 `.bcss`。 | `.bcss` | —— |
| SNAP-006 | 快照作为存档加载 | Snapshot as archive | 快照可作为文件夹会话的一侧加载。 | 支持 | 与压缩包走同一套机制 |
| SNAP-007 | 快照 cwd 默认位置 | Default save location | 默认保存到当前文件夹。 | 当前文件夹 | 脚本中注意工作目录 |
| SNAP-008 | 快照保存 CRC | `save-crc` | 快照中保存 CRC 值。 | 可选 | 无 CRC 时无法做内容级快速比对 |
| SNAP-009 | 快照保存版本信息 | `save-version` | 保存可执行文件版本信息。 | 可选 | 仅 Windows |
| SNAP-010 | 快照展开压缩包 | `expand-archives` | 递归展开压缩包内容。 | 可选 | 显著增大快照并变慢 |
| SNAP-011 | 快照跟随符号链接 | `follow-symlinks` | 跟随符号链接。 | 可选 | 可能形成环 |
| SNAP-012 | 快照包含空文件夹 | `include-empty` | 记录空文件夹。 | 可选 | 不记录会丢目录结构信息 |
| SNAP-013 | 快照忽略过滤器 | `no-filters` | 保存快照时不应用过滤器。 | 可选 | 不加此项快照可能不完整 |
| SNAP-014 | 快照的侧选择 | `left\|right\|path:<path>` | 指定要快照的侧或路径。 | 必需 | —— |
| SNAP-015 | 快照输出目标 | `output:<target>` | 指定快照输出文件。 | 默认当前目录 | —— |
| SNAP-016 | 快照的只读性 | Read-only snapshot | 快照为只读，只能作为比较的来源。 | 只读 | 不能作为复制/同步目标 |
| SNAP-017 | 受控源（Reference snapshot） | Controlled/Reference source | 以快照作为「基线」长期保存，后续定期比对。 | — | 是「变更审计」的推荐用法 |
| SNAP-018 | 快照的命令行/脚本生成 | snapshot command | 通过脚本批量生成快照（配合计划任务）。 | — | 见 SCRIPT / AUTO 章 |
| SNAP-019 | 快照与文件夹会话的比较 | Snapshot vs folder | 快照与实时文件夹比对，识别新增/删除/修改。 | 支持 | 内容比对不可用（无内容），只能靠大小/CRC/时间戳 |

---

## 27. 远程与云服务配置（Profiles: FTP / SFTP / Cloud）

### 27.1 远程配置总览

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REM-001 | 远程配置管理器 | Profiles dialog (Tools > Profiles) | 管理 FTP、SFTP、云存储等远程连接配置。 | — | BC5 中入口为 Tools > Profiles |
| REM-002 | FTP | FTP | 明文 FTP 协议支持。 | 支持 | 明文传输不适合敏感数据 |
| REM-003 | FTPS（FTP over SSL） | FTPS | 通过 SSL 的 FTP。 | Pro | —— |
| REM-004 | SFTP | SFTP `[Pro]` | 内置安全 FTP（SSH）支持，无需第三方。 | Pro | 端口注意（默认 22） |
| REM-005 | WebDAV | WebDAV `[Pro]` | WebDAV 协议支持。 | Pro | —— |
| REM-006 | Amazon S3 | Amazon S3 `[Pro]` | 直接访问 S3 API。 | Pro | 需凭据/区域配置 |
| REM-007 | Dropbox | Dropbox `[Pro]` | 直接访问 Dropbox API，OAuth2 认证。 | Pro | 曾出现「Invalid key material type」错误（5.2.0 修复） |
| REM-008 | Google Drive | Google Drive `[Pro]` | 直接访问 Google Drive。 | Pro | BC5 支持 |
| REM-009 | OneDrive | OneDrive `[Pro]` | 直接访问 OneDrive（仅 Windows 与 macOS）。 | Pro | Linux 不支持 |
| REM-010 | Subversion 资源 | Subversion repository `[Pro]` | 直接浏览 SVN 仓库资源。 | Pro | 只读访问在禁用远程配置策略下仍允许 |
| REM-011 | 远程路径语法 | Remote path syntax | 以 `ftp://user@host/path/file.txt` 形式作为 Spec。 | — | 用户名/端口写在路径中易出错 |
| REM-012 | 使用已配置的配置文件 | Use configured profile | 在路径编辑框点击文件夹图标选择已配置的远程配置。 | — | 配置名含空格时命令行需引号 |
| REM-013 | 添加新配置 | Add profile (`[+]`) | 在 Profiles 对话框中新建配置。 | — | —— |
| REM-014 | 复制/删除配置 | Clone / Delete profile | 复制或删除远程配置。 | — | —— |
| REM-015 | 远程配置的导入导出 | Profile import/export | 通过 `.bcpkg` 导入导出远程配置。 | 支持 | 5.2 起导出配置中的密码在降级安装时视为空 |

### 27.2 FTP 配置页

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REM-016 | FTP 配置-通用 | Profile: General | 配置名称、类型等基本信息。 | — | —— |
| REM-017 | FTP 配置-连接 | Profile: Connection | 主机、端口、初始目录、超时等。 | 端口 21 | SFTP 默认 22 |
| REM-018 | FTP 配置-清单 | Profile: Listings | 目录列表解析规则（Unix/Windows/DOS 格式）。 | 自动检测 | 列表格式识别错会导致文件名/时间戳错乱 |
| REM-019 | FTP 配置-传送 | Profile: Transfer | 传输模式（二进制/ASCII/Auto）、编码。 | **BC5 起默认二进制**（Auto 仍可选） | BC4 对部分类型使用 ASCII |
| REM-020 | FTP 配置-身份验证 | Profile: Authentication | 用户名、密码、保存密码等。 | 密码可保存 | Admin Policy 可禁用保存密码 |
| REM-021 | 服务器编码设置 | Server > Encoding | 命令/文件名编码。 | **BC5 起默认 UTF-8**（旧为 Detect） | 旧服务器可能需改为 Detect |
| REM-022 | 保留时间戳 | Preserve timestamps | 尝试保持远端文件时间戳。 | 依服务器支持 | 多数 FTP 不支持设置远端时间戳 |
| REM-023 | 复制到 FTP 时的本地接触 | Touch local files | 复制后修正本地时间戳以与远端一致。 | 关闭 | 见 DIR-032 |
| REM-024 | 被动/主动模式 | Passive / Active mode | FTP 数据连接模式。 | 依实现 | 防火墙下常见问题 |
| REM-025 | 连接超时与重试 | Timeout and retry | 连接/传输超时与重试策略。 | 内置 | —— |
| REM-026 | 远程符号链接 | Remote symlinks | 远端符号链接的显示与跟随。 | 依服务器 | 部分服务器不支持 |
| REM-027 | 远程文件权限 | Remote permissions | 远端 Unix 权限的显示与设置。 | 支持 | 需服务器支持 |
| REM-028 | 远程删除 | Remote delete | 删除远端文件/目录。 | 支持 | 不可用回收站 |

### 27.3 云存储配置

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| REM-029 | OAuth2 认证 | OAuth2 authentication | 云存储使用原生 OAuth2 授权。 | 支持 | 需浏览器完成授权 |
| REM-030 | 云存储的令牌刷新 | Token refresh | 访问令牌过期后的刷新。 | 自动 | 需保持网络与时钟正确 |
| REM-031 | 云存储的根路径 | Cloud root path | 配置云盘中的根目录。 | 账户根 | —— |
| REM-032 | S3 的桶与区域 | S3 bucket and region | 桶名、区域、访问密钥配置。 | — | 区域配错会连不上 |
| REM-033 | S3 的兼容端点 | S3-compatible endpoint | 支持自定义端点（如 MinIO）。 | — | 需手填 endpoint |
| REM-034 | 云存储的时间戳 | Cloud timestamps | 云端时间戳的精度与保留。 | 依服务 | 同步不收敛的常见原因 |
| REM-035 | 云存储的删除 | Cloud delete | 云端删除（可能进入云端回收站）。 | 支持 | 行为依服务 |
| REM-036 | 云存储的限流 | Cloud rate limiting | API 限流与退避。 | 自动 | 大量小文件时易触发 |
| REM-037 | 云存储的文件大小限制 | Cloud object size limits | 单对象大小与分片上传。 | 自动 | —— |
| REM-038 | 远程配置的密码存储安全 | Saved password storage | BC5.2 改变密码存储方式。 | 加密 | 降级安装密码视为空 |
| REM-039 | 禁用远程配置的策略 | DisableRemoteProfiles policy | 通过管理策略禁用 FTP 与云配置。 | 默认允许 | SVN 只读访问仍启用 |
| REM-040 | 禁用保存密码的策略 | DisableSavedPasswords policy | 隐藏「保存密码」复选框。 | 默认允许 | —— |
| REM-041 | 远程配置的名称冲突 | Profile name conflicts | 多个配置同名时的选择。 | 提示 | 会话中记录的配置名可能失效 |
| REM-042 | 远程路径与本地路径的对照 | Remote vs local semantics | 远程文件无本地路径概念，部分能力（回收站、属性）不可用。 | — | 大量「功能不可用」提示 |
| REM-043 | 远程会话的性能 | Remote session performance | 远程目录扫描的延迟与缓存。 | 缓存 | 深目录远程扫描很慢 |
| REM-044 | 远程文件的部分读取 | Remote partial read | 内容比对时按需读取文件片段。 | 支持 | 影响远程内容比对速度 |
| REM-045 | 远程站点的地址簿 | Site bookmarks | 常用远程站点的快速访问。 | 配置列表 | —— |
| REM-046 | 远程配置的测试连接 | Test connection | 验证配置是否正确。 | — | —— |
| REM-047 | 远程配置的默认初始目录 | Initial remote directory | 打开配置时默认进入的远端目录。 | 账户根 | —— |
| REM-048 | 网络驱动器 | Network drives | 支持映射的网络驱动器作为路径。 | 支持 | 网络延迟与断线处理 |
| REM-049 | 远程的并发传输 | Concurrent transfers | 远程传输的并发策略。 | 依实现 | 服务器可能限制连接数 |
| REM-050 | 远程与压缩包的组合 | Remote archive | 远程上的压缩包文件展开。 | 支持 | 需先下载 |
| REM-051 | 远程配置在同步中的使用 | Remote sync | 本地与远程之间的同步。 | 支持 | 时间戳/touch 问题最突出 |
| REM-052 | 远程配置在计划任务脚本中的使用 | Remote in scripts | 脚本中直接 load 远程路径。 | 支持 | 凭据需已保存或无需交互 |

---

## 28. 版本控制集成（Source Control Integration）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| VCS-001 | 源代码管理集成配置 | Tools > Source Control Integration | 把文件夹与 SCC 兼容的版本控制程序关联。 | `[Pro] [Win]` | 未配置则不显示相关命令 |
| VCS-002 | SCC 兼容提供者 | SCC-compliant provider | 支持 SCC 接口的 VCS（如 Perforce、旧版 VSS 等）。 | 需安装 provider | —— |
| VCS-003 | 添加到源代码管理 | Add to Source Control | 把文件加入项目。 | 子菜单 | —— |
| VCS-004 | 从源代码管理中移除 | Remove from Source Control | 从项目中移除文件。 | 子菜单 | —— |
| VCS-005 | 获取最新版本 | Get Latest Version | 从版本库获取最新版本。 | 子菜单 | 可能覆盖本地修改 |
| VCS-006 | 获取特定版本 | Get | 获取指定版本。 | 子菜单 | —— |
| VCS-007 | 检入 | Check In | 接受改动并应用到项目。 | `Ctrl+R` 邻近 | 需先实际保存文件 |
| VCS-008 | 检出 | Check Out | 从版本库检出并使原文件可写。 | 子菜单 | —— |
| VCS-009 | 撤销检出 | Undo Check Out | 丢弃本地改动并把文件设回只读。 | 子菜单 | 不可恢复本地改动 |
| VCS-010 | 显示差异 | Show Differences | 通知 VCS 调用 BC 显示文件比较。 | 需 VCS 端配置 BC 为 diff 工具 | —— |
| VCS-011 | 显示历史 | Show History | 在 VCS 中显示文件历史。 | 子菜单 | —— |
| VCS-012 | 显示属性 | Show Properties | 显示 VCS 属性。 | 子菜单 | —— |
| VCS-013 | 运行源代码管理程序 | Run Source Control Program | 启动 VCS 客户端程序。 | 子菜单 | —— |
| VCS-014 | VCS 命令的可见条件 | VCS command visibility | 仅当配置了 provider 且所有选中项受控时才显示。 | 条件显示 | 看不到命令通常是未配置 provider |
| VCS-015 | 与 Git 的集成 | Git integration | 通过 `git difftool` / `git mergetool` 配置调用 BC。 | 需用户配置 | 需设置 `bcomp` 为 mergetool |
| VCS-016 | 与 SVN 的集成 | Subversion integration | 通过 `svn diff` 外部工具配置调用 BC。 | 需用户配置 | `--diff-cmd` 参数配置 |
| VCS-017 | 与 Mercurial 的集成 | Mercurial integration | 通过 `hg` 外部工具配置调用 BC。 | 需用户配置 | —— |
| VCS-018 | 与 Perforce 的集成 | Perforce integration | SCC 集成或外部工具配置。 | 需用户配置 | —— |
| VCS-019 | 与 TortoiseGit/TortoiseSVN 的集成 | Tortoise integration | 在 Tortoise 客户端中配置 BC 为 diff/merge 工具。 | 需用户配置 | —— |
| VCS-020 | 版本控制的差异查看命令 | Diff via VCS | 由 VCS 调用 BC 打开「工作副本 vs 版本库」比较。 | — | 需要 VCS 传入 `bcomp` 并等待 |
| VCS-021 | 三方合并由 VCS 发起 | Merge via VCS | VCS 调用 BC 做三路合并，BC 返回冲突码。 | — | 返回码 14/101 需 VCS 识别 |
| VCS-022 | 在文件比较中查看修改历史 | Revision history in file compare | 通过 VCS 插件查看文件的修订历史。 | 需集成 | —— |
| VCS-023 | 比较两个修订 | Compare two revisions | 选择两个修订版本进行比较。 | 通过 VCS 界面发起 | —— |
| VCS-024 | 接受修订 | Accept revision | 把某修订内容接受为当前内容。 | 通过复制/保存实现 | —— |
| VCS-025 | VCS 状态列 | VCS status column | 文件夹视图中显示版本控制状态列。 | 需配置 | 见 DIRUI-006 邻近项 |
| VCS-026 | 按 VCS 状态过滤 | Filter by VCS status | 其它过滤器中按源代码控制状态过滤。 | 关闭 | 需 Windows + provider |
| VCS-027 | VCS 路径作为资源 | `/vcs1..4=` | 命令行用 VCS 路径作为路径编辑框内容（也用于挑选文件格式）。 | — | 与 `/title` 同时提供时 title 优先 |
| VCS-028 | BC 作为外部 diff 工具的标准配置 | Standard external diff config | 官方提供 Git/SVN/Mercurial/Perforce 等配置指南。 | — | —— |
| VCS-029 | bcomp 用于 VCS 等待 | `bcomp` executable | 从 VCS 调用时等待比较结束再返回。 | — | 用 `BCompare.exe` 可能导致 VCS 提前返回 |
| VCS-030 | BComp.com 用于批处理等待 | `BComp.com` | 控制台程序，批处理会等待其结束。 | — | 需有控制台 |
| VCS-031 | VCS 集成的 Pro/平台限制 | Pro and Windows only | SCC 集成仅 Pro + Windows。 | 限制 | macOS/Linux 只能用外部工具方式 |
| VCS-032 | VCS 变更的批量检入 | Batch check-in | 文件夹视图中批量检入多个文件。 | 支持 | —— |
| VCS-033 | VCS 差异视图的调用等待 | VCS wait semantics | 交互式调用会显示控制台窗口等待。 | — | BComp.com 在交互式 VCS 中会显示控制台 |
| VCS-034 | 只读 SVN 访问 | Read-only Subversion access | 禁用了远程配置策略时，SVN 只读访问仍可用。 | BC5.2 行为 | —— |
| VCS-035 | VCS 集成的会话保存 | VCS settings persistence | VCS 关联配置持久化。 | BCSourceControl.xml | 迁移时需一并导出 |
| VCS-036 | VCS 属性的显示 | VCS properties display | 显示文件的版本控制属性（如 svn:keywords）。 | — | —— |
| VCS-037 | VCS 历史中的两侧比较 | Compare across history revisions | 跨修订的两文件比较。 | — | 大仓库需先导出到临时位置 |
| VCS-038 | VCS 集成的错误处理 | VCS error handling | provider 不可用或命令失败时的提示。 | 提示 | 静默脚本中需检查返回码 |

---

## 29. 命令行接口（Command Line）

### 29.1 可执行文件

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| CLI-001 | 主程序 `BCompare.exe` | BCompare.exe `[Win]` | 主应用程序；同时只运行一个副本，第二次启动会把比较请求转给已有实例并立即退出。 | 单实例 | 需要独立实例时用 `/solo` |
| CLI-002 | `BComp.exe` | BComp.exe `[Win]` | Win32 GUI 程序，从 VCS 调用正常；从控制台/批处理启动则控制台不等待。 | — | 批处理中不会等待 |
| CLI-003 | `BComp.com` | BComp.com `[Win]` | Win32 控制台程序，必须有控制台；从控制台/批处理启动会等待比较完成。 | — | 交互式 VCS 调用会弹出控制台 |
| CLI-004 | `bcompare` | bcompare `[macOS/Linux]` | macOS/Linux 主程序。 | — | 开关前缀用 `-` 而非 `/` |
| CLI-005 | `bcomp` | bcomp `[macOS]` | 从 VCS 调用并等待比较完成。 | — | —— |

### 29.2 命令行参数

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| CLI-006 | 命名会话参数 | Named Session | `BCompare.exe "My Session"` 打开指定会话。 | — | 名称含空格需引号 |
| CLI-007 | 命名工作区参数 | Named Workspace | 打开指定已保存工作区。 | — | —— |
| CLI-008 | 文件夹对参数 | Pair of folders | 打开新的 Folder Compare。 | — | 两个目录参数 |
| CLI-009 | 文件对参数 | Pair of files | 以关联文件视图打开两个文件。 | — | —— |
| CLI-010 | 三文件参数 | 3 files `[Pro]` | 打开 Text Merge（左/右/中）。 | — | 顺序敏感 |
| CLI-011 | 四文件参数 | 4 files `[Pro]` | 打开 Text Merge（左/右/中/输出）。 | — | 顺序敏感 |
| CLI-012 | 脚本文件参数 | Script file (`@`) | 以 `@路径` 自动执行脚本。 | — | 不显示常规界面 |
| CLI-013 | 设置包参数 | Settings package (`.bcpkg`) | 导入设置包中的全部设置。 | — | 覆盖现有设置 |
| CLI-014 | 补丁文件参数 | Patch file (`.diff`/`.patch`) | 在 Text Patch 视图中打开。 | — | —— |
| CLI-015 | 标准输入参数 | `-` | 以 stdin 作为一侧内容打开相应视图。 | — | 无法推断格式 |
| CLI-016 | 参数含空格的引号要求 | Quoting | 可能含空格的参数应加引号。 | — | 常见脚本失败原因 |

### 29.3 命令行开关

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| CLI-017 | 帮助 | `/?, /h, /help` | Windows 打开帮助页面；macOS/Linux 输出用法到控制台。 | — | —— |
| CLI-018 | 自动合并 | `/automerge` `[Pro]` | 无冲突时无人工交互完成合并。 | 关闭 | 有冲突仍停下 |
| CLI-019 | 指定合并祖先 | `/center=<filename>` | 显式指定合并中心文件。 | 由参数位置决定 | —— |
| CLI-020 | 关闭脚本窗口 | `/closescript` | 完成时关闭脚本窗口，覆盖 Tweaks 中设置。 | 关闭 | 排错时不建议开启 |
| CLI-021 | 文本编辑视图 | `/edit` | 打开 Text Edit 视图。 | — | —— |
| CLI-022 | 初始展开全部 | `/expandall` | 初始文件夹比较时展开所有子文件夹。 | 关闭 | 大目录变慢 |
| CLI-023 | 偏好左侧 | `/favorleft` | 非冲突变更从左侧绘制且不加色/分隔线；被忽略的不重要冲突自动取左侧。 | 无偏好 | `[Pro]` 限制见 `/favorright` |
| CLI-024 | 偏好右侧 | `/favorright` `[Pro]` | 同上，偏好右侧。 | 无偏好 | —— |
| CLI-025 | 名称过滤器 | `/filters=<masks>` | 指定初始文件夹比较的名称过滤器（分号分隔多掩码）。 | 无过滤 | 含空格需引号 |
| CLI-026 | 强制冲突标记 | `/force` `[Pro]` | 配合 `/automerge` 时以 CSV 风格标记把冲突写入输出。 | 关闭 | 标记会写进文件 |
| CLI-027 | 指定视图类型 | `/fv=<type>, /fileviewer=<type>` | 以指定类型打开新视图（13 种取值）。 | 依文件类型推断 | 类型串需与官方完全一致 |
| CLI-028 | 忽略不重要差异 | `/iu, /ignoreunimportant` | 配合 `/automerge` 时开启忽略不重要差异。 | 关闭 | —— |
| CLI-029 | 指定合并输出 | `/mergeoutput=<file\|path>` `[Pro]` | 显式指定合并输出文件或目录。 | 参数位置决定 | —— |
| CLI-030 | 禁止备份 | `/nobackups` | 阻止创建备份文件。 | 关闭 | 覆盖操作不可回退 |
| CLI-031 | 快速比较 | `/qc=<type>, /quickcompare=<type>` | 快速比较两文件并设置退出码；类型可为 `size`、`crc`、`binary`，缺省为规则化比较。 | — | 退出码是核心用法 |
| CLI-032 | 复核冲突 | `/reviewconflicts` `[Pro]` | 配合 `/automerge`，发现冲突时打开 Text Merge。 | 关闭 | —— |
| CLI-033 | 全部只读 | `/ro, /readonly` | 禁止所有侧编辑。 | 可编辑 | —— |
| CLI-034 | 左侧只读 | `/ro1, /lro, /leftreadonly` | 禁止左侧编辑。 | 可编辑 | —— |
| CLI-035 | 右侧只读 | `/ro2, /rro, /rightreadonly` | 禁止右侧编辑。 | 可编辑 | —— |
| CLI-036 | 保存目标重定向 | `/savetarget=<filename>` | 文件视图的 Save 命令改写指定文件而非原文件。 | 原文件 | 容易误以为已保存回原文件 |
| CLI-037 | 静默模式 | `/silent` | 抑制一切交互：不显示任务栏条目或窗口；设置包全量导入；脚本中本应弹窗的问题改为记录错误。 | 关闭 | 排错难度大 |
| CLI-038 | 独立实例 | `/solo` | 强制启动新实例。 | 单实例 | 多实例共享设置文件可能冲突 |
| CLI-039 | 打开同步视图 | `/sync` `[Win] [macOS]` | 打开 Folder Sync 视图。 | — | Linux 不支持 |
| CLI-040 | 路径编辑框标题 | `/title1..4=, /lefttitle=, /righttitle=, /centertitle=, /outputtitle=` | 在对应路径编辑框中显示指定描述。 | 显示真实路径 | 只是显示，不改变实际路径 |
| CLI-041 | VCS 路径 | `/vcs1..4=, /vcsleft=, /vcsright=, /vcscenter=, /vcsoutput=` | 在对应路径编辑框显示 VCS 路径；文件视图还会用它挑选文件格式。 | — | 与 `/title` 同时存在时 title 优先 |
| CLI-042 | 未文档化的历史开关 | `/bds` | 与 CodeGear RAD Studio/Borland Developer Studio 的 `__history` 修订比较（历史遗留）。 | — | 现代 IDE 已不常用 |

### 29.4 返回码

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| CLI-043 | 成功 | 0 Success | 操作成功。 | — | —— |
| CLI-044 | 二进制相同 | 1 Binary same | 二进制比较结果相同。 | — | 与 2 区分「怎么比出来的」 |
| CLI-045 | 规则化相同 | 2 Rules-based same | 规则化比较结果相同。 | — | —— |
| CLI-046 | 二进制不同 | 11 Binary differences | 二进制比较发现差异。 | — | —— |
| CLI-047 | 相似 | 12 Similar | 内容相似但不完全相同。 | — | 常用于「近似文件」判定 |
| CLI-048 | 规则化不同 | 13 Rules-based differences | 规则化比较发现差异。 | — | —— |
| CLI-049 | 检测到冲突 | 14 Conflicts detected | 合并检测到冲突。 | — | 脚本须据此判定合并失败 |
| CLI-050 | 未知错误 | 100 Unknown error | 未分类错误。 | — | —— |
| CLI-051 | 冲突且未写输出 | 101 Conflicts detected, merge output not written | 冲突导致输出未写出。 | — | 与 14 区分「有没有产出」 |
| CLI-052 | BComp 无法等待 | 102 BComp.exe unable to wait until BCompare.exe finishes | 等待失败。 | — | —— |
| CLI-053 | 找不到主程序 | 103 BComp.exe cannot find BCompare.exe | 找不到主程序。 | — | 安装路径问题 |
| CLI-054 | 试用期过期 | 104 Trial period expired | 试用到期。 | — | —— |
| CLI-055 | 脚本加载失败 | 105 Error loading script file | 脚本文件加载失败。 | — | —— |
| CLI-056 | 脚本语法错误 | 106 Script syntax error | 脚本语法错误。 | — | —— |
| CLI-057 | 脚本无法加载文件夹/文件 | 107 Script failed to load folders or files | 路径无效或不可访问。 | — | CI 中最常见的失败码 |

### 29.5 命令行的其它能力

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| CLI-058 | 命令行与脚本的组合 | CLI + script | 命令行参数可作为脚本的 `%1..%9` 变量。 | — | 脚本名与 `/` 开关不计入 1–9 |
| CLI-059 | 环境变量在脚本中的展开 | `%VAR%` | 脚本中可通过百分号引用环境变量。 | — | 变量名大小写必须正确 |
| CLI-060 | 动态变量 | `%date% %time% %fn_time%` | 脚本每行可填入当前日期/时间/文件名安全时间。 | — | `%time%` 格式依赖区域设置 |
| CLI-061 | 返回码在批处理中的使用 | Exit code in batch/CI | 通过 `%ERRORLEVEL%` 判定比较结果。 | — | 需注意 `.com` 与 `.exe` 的等待差异 |
| CLI-062 | 手动检查命令行用法 | Usage output | 不带参数或在终端运行以查看用法提示。 | — | 不同小版本参数略有差异 |
| CLI-063 | 命令行打开压缩包内文件 | Archive path on CLI | 直接以压缩包内路径作为参数。 | 支持 | `/fv="Hex Compare"` 对压缩包文件也可用（BC5 新增） |

---

## 30. 脚本语言（Scripting）

### 30.1 脚本机制

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SCR-001 | 脚本 | Script | 纯文本文件，包含一系列控制程序、自动执行文件操作或生成报告的命令。 | — | 逐行处理 |
| SCR-002 | 脚本运行方式 | Run a script | 命令行以 `@` 前缀指定脚本文件名。 | — | 不显示常规界面 |
| SCR-003 | 脚本的逐行处理 | Line-by-line processing | 每行一条命令。 | — | 一条命令跨行需用 `&` 续行 |
| SCR-004 | 大小写不敏感 | Case-insensitive | 脚本命令不区分大小写。 | — | 但环境变量名大小写敏感 |
| SCR-005 | 空行与注释 | Blank lines and comments | 空行与 `#` 之后的内容被忽略。 | — | —— |
| SCR-006 | 参数分隔 | Space-separated arguments | 参数以空格分隔。 | — | 含空格的参数用双引号包裹 |
| SCR-007 | 续行符 | Ampersand continuation | 在除末行外的每行末尾加 `&` 以续行。 | — | 漏加会报语法错误 |
| SCR-008 | 命令行参数引用 | `%1`–`%9` | 脚本中可引用命令行传入的参数。 | — | 脚本名与 `/` 开关不计入 |
| SCR-009 | 环境变量引用 | `%VAR%` | 以百分号包裹环境变量名。 | — | 大小写必须正确 |
| SCR-010 | 动态变量 | `%date%`, `%time%`, `%fn_time%` | 每行自动填入当前日期、时间、文件名安全时间。 | — | 格式依赖区域设置 |
| SCR-011 | 临时目录变量 | `%TMP%` | 可加载系统临时文件夹。 | — | —— |
| SCR-012 | 脚本本质 | Invisible folder session | 脚本本质上是操作一个不可见的文件夹会话：文件操作需要选择、显示可被过滤限制、文件夹可被展开以递归。 | — | 概念模型是理解脚本的关键 |
| SCR-013 | 脚本的文件夹参数范围 | Folder argument types | 文件夹名参数可为本地、网络、远程服务或压缩包（`.zip`、`.cab`、Snapshot）。 | — | 远程/压缩包能力受限 |
| SCR-014 | 脚本可能需要确认 | Script confirmations | 脚本运行中某些确认可能仍需输入。 | 需交互 | 用 `option confirm` 控制 |
| SCR-015 | 脚本任务栏 | Scripting Task Bar | 脚本处理时在任务栏显示条目。 | 显示 | `/silent` 抑制 |
| SCR-016 | 脚本状态窗口 | Scripting Status Window | 显示进度与错误。 | 显示 | Tweaks 可控制自动关闭与蜂鸣 |
| SCR-017 | 脚本完成蜂鸣 | Beep when finished | 脚本完成时发出提示音。 | 关闭 | Options > Tweaks > Scripts |
| SCR-018 | 脚本完成自动关闭 | Close when finished | 完成后自动关闭状态窗口。 | 关闭 | 与蜂鸣配合使用 |
| SCR-019 | 共享脚本文件夹 | Shared scripts folder | 命令行给出无路径脚本名时，先在当前目录找，找不到再到共享脚本文件夹找。 | 需配置 | —— |
| SCR-020 | 脚本的日志 | `log` 命令 | 控制日志详细程度与落盘位置，默认写当前文件夹的 `Log.txt`。 | 默认 normal | 需显式 `append:` 才追加 |
| SCR-021 | 脚本的停止策略 | `option stop-on-error` | 出错时是否停止。 | 默认继续 | CI 中通常需要 stop-on-error |
| SCR-022 | 脚本的确认策略 | `option confirm:` | `prompt` / `yes-to-all` / `no-to-all`。 | prompt | 静默脚本应设为 yes-to-all 或 no-to-all |
| SCR-023 | 脚本的编码 | Script file encoding | 支持 UTF-8（含无 BOM）；BC5 修复了无 BOM UTF-8 脚本的读取。 | — | 旧版本会读错 |

### 30.2 脚本命令（共 29 个）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SCR-024 | 设置/清除文件属性 | `attrib (+/-)<set>` | 设置或清除选中项的 DOS 文件属性；集合为 `[a][s][h][r]`。 | — | 仅 Windows |
| SCR-025 | 蜂鸣 | `beep` | 发出 PC 扬声器蜂鸣（即使 `/silent` 也会响）。 | — | —— |
| SCR-026 | 折叠 | `collapse all` / `collapse <path>` | 折叠所有或指定路径的文件夹；路径不支持通配符。 | — | —— |
| SCR-027 | 比较内容 | `compare [CRC\|binary\|rules-based]` | 用指定类型比较当前选中项内容。 | 用最近一次的类型 | 只比较一次；改变方式需用 `criteria` |
| SCR-028 | 复制 | `copy (left->right\|right->left)` | 按方向复制选中文件/文件夹。 | — | —— |
| SCR-029 | 复制到路径 | `copyto [left\|right\|all] [path:(relative\|base\|none)] <path>` | 复制选中项到指定路径。 | all + path:none | `base` 保留完整相对结构，`relative` 保留最小相对结构 |
| SCR-030 | 设置比较条件 | `criteria [...]` | 设置全部比较条件：attrib、version、timestamp、size/CRC/binary/rules-based、timezone、follow-symlinks、ignore-unimportant、owner、group、permissions。 | — | attrib 仅 Windows；owner/group/permissions 为 BC5.2 新增 |
| SCR-031 | 删除 | `delete [recyclebin=(yes\|no)] (left\|right\|all)` | 删除指定侧选中项。 | — | 回收站不支持网络驱动器/远程服务/压缩包 |
| SCR-032 | 展开 | `expand all` / `expand <path>` | 展开指定或全部子文件夹。 | — | `expand all` 不展开被文件过滤器排除的子文件夹 |
| SCR-033 | 文件报表 | `file-report layout:... [...]` | 为选中文件生成报告，布局 side-by-side 或 summary。 | — | —— |
| SCR-034 | 过滤器 | `filter <masks>` / `filter cutoff:` / `filter size:` / `filter attrib:` / `filter exclude-protected` / `filter include-protected` / `filter unixtype:` | 控制哪些文件夹与文件类型参与比较。 | — | attrib/受保护过滤仅 Windows |
| SCR-035 | 文件夹报表 | `folder-report layout:...` | 生成文件夹比较报告，布局 side-by-side / summary / xml。 | — | 含子文件夹需先 `expand all` |
| SCR-036 | 十六进制报表 | `hex-report layout:...` | 布局 side-by-side / summary / interleaved。 | — | —— |
| SCR-037 | 加载 | `load <session>` / `load [create:(all\|left\|right)] <left> [<right>]` / `load <default>` | 加载会话、基准文件夹，或用默认设置新建文件夹比较。 | — | 加载失败会终止脚本 |
| SCR-038 | 日志 | `log [none\|normal\|verbose] [[append:]<file>]` | 控制日志详细程度与存储位置。 | normal，写 `Log.txt` | —— |
| SCR-039 | 媒体报表 | `media-report layout:...` | 布局 side-by-side / summary。 | — | —— |
| SCR-040 | 移动 | `move (left->right\|right->left)` | 按方向移动选中项。 | — | —— |
| SCR-041 | 移动到路径 | `moveto [left\|right\|all] [path:...] <path>` | 移动到指定路径。 | all + path:none | —— |
| SCR-042 | 脚本选项 | `option stop-on-error` / `option confirm:(prompt\|yes-to-all\|no-to-all)` | 调整脚本处理选项。 | prompt | —— |
| SCR-043 | 图片报表 | `picture-report layout:...` | 布局 side-by-side / summary。 | — | —— |
| SCR-044 | 注册表报表 | `registry-report layout:...` | 仅 Pro + Windows。 | — | —— |
| SCR-045 | 重命名 | `rename [regexpr <old mask>] <new mask>` | 多文件重命名；`regexpr` 启用正则。 | DOS 风格重命名 | —— |
| SCR-046 | 选择 | `select <mask>...` | 选择掩码控制可操作项。 | — | 掩码见 SCR-047 |
| SCR-047 | 选择掩码语法 | Select masks | `all` / `[(left\|right\|all).][(exact\|diff\|newer\|older\|orphan\|all).][(files\|folders\|all)]` / `empty.folders`。 | — | 掩码写错会「什么都没操作」 |
| SCR-048 | 快照 | `snapshot [save-crc] [save-version] [expand-archives] [follow-symlinks] [include-empty] [no-filters] left\|right\|path:<path> [output:<target>]` | 保存只读快照。 | `.bcss` 扩展名 | —— |
| SCR-049 | 同步 | `sync [visible] [create-empty] (update\|mirror):(...)` | 通过复制与删除同步文件夹（不使用当前选中项）。 | — | `visible` 只操作当前可见项 |
| SCR-050 | 表格报表 | `data-report layout:...` | 布局 side-by-side / summary / interleaved。 | — | —— |
| SCR-051 | 文本报表 | `text-report layout:...` | 布局 side-by-side / summary / interleaved / patch / statistics / xml。 | — | `over-under`、`composite` 已弃用 |
| SCR-052 | 接触时间戳 | `touch (left->right\|right->left)` / `touch (left\|right\|all):(now\|<timestamp>)` | 复制时间戳或设置为当前/指定时间。 | — | 需先选中文件 |

### 30.3 报表命令的公共参数

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SCR-053 | 报表标题参数 | `title:<text>` | 报表顶部显示的标题。 | 默认标题 | —— |
| SCR-054 | 输出目标参数 | `output-to:(printer\|clipboard\|<filename>)` | 报表输出目标。 | 必填 | 漏写报语法错 |
| SCR-055 | 输出选项参数 | `output-options:<options>` | 依输出目标而异的附加选项。 | — | 详见 SCR-056~058 |
| SCR-056 | 打印色彩选项 | `print-color` / `print-mono` | 彩色或单色打印。 | `print-mono` | —— |
| SCR-057 | 打印方向选项 | `print-portrait` / `print-landscape` | 纵向/横向。 | `print-portrait` | —— |
| SCR-058 | 换行选项 | `wrap-none` / `wrap-character` / `wrap-word` | 换行策略（printer 支持三种；HTML 支持 none/word）。 | `wrap-none` | —— |
| SCR-059 | HTML 输出选项 | `html-color` / `html-mono` / `html-custom` | HTML 颜色模式；`html-custom` 需外部样式表文件名或 URL。 | 剪贴板/文件输出适用 | —— |
| SCR-060 | 报表布局-补丁 | `layout:patch` | 生成补丁报表。 | — | 需配合补丁样式选项 |
| SCR-061 | 报表布局-统计 | `layout:statistics` | 只输出统计数字。 | — | —— |
| SCR-062 | 报表布局-XML | `layout:xml` | 结构化 XML（文本/文件夹报表）。 | — | —— |
| SCR-063 | 比较对象参数 | `<comparison>` | 会话名或一对文件名。 | 当前会话 | —— |

### 30.4 脚本的自动化与调度

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| SCR-064 | 脚本示例 | Sample scripts | 官方提供脚本示例集合。 | — | —— |
| SCR-065 | 计划任务调度 | Scheduling a script | 用系统计划任务（Task Scheduler / cron / launchd）定时执行脚本。 | — | 需注意运行账户权限与网络凭据 |
| SCR-066 | 脚本在 CI 中的应用 | Script in CI | 用脚本 + 退出码在流水线中做目录/内容校验。 | — | `/silent` 避免卡住 |
| SCR-067 | 脚本的日志归档 | Log archiving | 用 `%date%` / `%fn_time%` 生成带日期的日志文件名。 | — | —— |
| SCR-068 | 脚本的脚本化报表 | Scripted reports | 批量生成报表并落盘（如每日变更清单）。 | — | 报表布局与选项需写全 |
| SCR-069 | 脚本的快照基线 | Scripted snapshot baseline | 定期生成快照作为基线，之后比对差异。 | — | —— |
| SCR-070 | 脚本的删除安全 | Scripted deletion safety | 脚本删除默认走回收站；远程/压缩包不走。 | 回收站 | 误删不可恢复 |
| SCR-071 | 脚本的执行账户 | Execution account | 计划任务运行账户决定可访问的路径与凭据。 | — | SYSTEM 账户看不到用户映射盘 |
| SCR-072 | 脚本的错误排查 | Script troubleshooting | 通过状态窗口/日志定位失败行。 | — | `/silent` 时错误只写日志 |
| SCR-073 | 脚本的路径基准 | Script working directory | 相对路径的基准。 | 当前工作目录 | 计划任务中工作目录常与预期不同 |
| SCR-074 | 脚本的版本兼容 | Script compatibility | 不同大版本间脚本命令的兼容性。 | 大体兼容 | 新增命令（如 owner/group/permissions）在旧版不可用 |

---

## 31. 外部调用与自动化（Automation / Third-party）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| AUTO-001 | 从其它程序调用 | Calling BC from other applications | 官方文档提供从 IDE/VCS/自研程序调用 BC 的指南。 | — | 需正确选择 .exe/.com/bcomp |
| AUTO-002 | 作为外部 diff 工具 | External diff tool | 被 Git/SVN/Mercurial/Perforce/TortoiseX 调用作为 diff 界面。 | — | 需配置参数模板 |
| AUTO-003 | 作为外部 merge 工具 | External merge tool | 作为三方合并工具被调用（需返回冲突码）。 | — | 返回码 14/101 必须被识别 |
| AUTO-004 | 等待语义 | Wait semantics | 调用方是否等待比较结束由可执行文件类型决定。 | 见 CLI-001~005 | 用错会导致调用方提前继续 |
| AUTO-005 | IDE 集成（历史） | IDE integration | 历史上有 Eclipse / JDeveloper / CodeGear 等插件方式。 | 部分废弃 | 现代 IDE 多用命令行 |
| AUTO-006 | Visual Studio 集成 | Visual Studio integration | 通过 Tools > External Tools 配置 BC 为比较工具。 | 需配置 | —— |
| AUTO-007 | Windows 资源管理器右键菜单 | Windows Explorer context menu | 在资源管理器右键菜单中提供「Compare」「Select for Compare」等。 | 安装时可选 | BC5 增强了 Windows 11 支持 |
| AUTO-008 | 右键菜单-Select for Compare | Select for Compare | 第一个文件/文件夹标记为「左」，右键第二个选 Compare 即比较。 | 支持 | —— |
| AUTO-009 | 右键菜单-与已选比较 | Compare with <selected> | 动态显示的第二个菜单项。 | 支持 | —— |
| AUTO-010 | Shell 扩展的稳定版问题 | Shell extension crashes | Windows 资源管理器 shell 扩展曾崩溃（已修）。 | — | 需保持版本更新 |
| AUTO-011 | macOS Finder 集成 | macOS Finder integration | Finder 中的服务/右键集成。 | 支持 | 需在系统设置中授权 |
| AUTO-012 | Linux 文件管理器集成 | Linux file manager integration | Nautilus / KDE（BC5 新增 KDE6）。 | 支持 | 依赖桌面环境版本 |
| AUTO-013 | 安装时的集成选择 | Installer integration options | 安装时选择注册哪些集成项。 | 全选 | —— |
| AUTO-014 | 上下文菜单的注册范围 | Register for current user / all users | 当前用户或所有用户注册。 | 当前用户 | BC5 起「All Users」写入 `%AllUsersProfile%` 避免 UAC |
| AUTO-015 | 便携式安装 | Portable installation | 便携模式下的集成与设置存储。 | 需选 | 便携模式不写注册表 |
| AUTO-016 | 计划任务 | Scheduled task | 用系统调度器定时运行 BC 脚本。 | 需自行配置 | 见 SCR-065 |
| AUTO-017 | 自动化 COM 接口 | Automation / COM | Windows 下的自动化接口（历史支持有限）。 | — | 官方主要以命令行/脚本为自动化路径 |
| AUTO-018 | 命令行作为自动化主线 | CLI as the automation path | 官方推荐的自动化方式为命令行 + 脚本。 | — | —— |
| AUTO-019 | 与备份/部署工具配合 | Integration with backup/deploy tools | 被备份、部署、发布工具调用做校验。 | — | `/qc` 是最常用的校验入口 |
| AUTO-020 | 退出码驱动的流水线判定 | Exit-code driven CI | 用 `/qc` + 退出码在流水线中做「内容是否一致」判定。 | — | 需区分 1/2（相同）与 11/13（不同） |
| AUTO-021 | 通过 stdin 管道使用 | Pipeline usage | `dir \| BCompare.exe -` 形式。 | 支持 | 交互式查看意义有限 |
| AUTO-022 | 通过 stdout 取报表 | Report to stdout | 脚本报表可输出到文件再由调用方读取。 | — | 无直接 stdout 报表参数 |
| AUTO-023 | 与文档/知识库工具集成 | Wiki / KB integration | 差异 HTML 复制粘贴到文档系统。 | — | 样式依赖目标系统 |
| AUTO-024 | 与工单系统集成 | Issue tracker integration | 差异文本/HTML 复制到工单。 | — | —— |
| AUTO-025 | 自动化中的凭据传递 | Credential passing in automation | 远程配置的凭据需预保存或免密。 | — | `DisableSavedPasswords` 策略会影响 |
| AUTO-026 | 无人值守运行的显示抑制 | Headless-ish operation | `/silent` 抑制窗口与任务栏。 | 关闭 | 并非真正无 GUI（仍依赖桌面会话） |
| AUTO-027 | 与自研工具的双向交互 | Custom tool integration | 自研工具调用 BC 并解析退出码/报表。 | — | 报表 XML 布局最适合解析 |

---

## 32. 程序选项（Tools > Options）

> 程序选项是**全局**偏好设置（区别于随会话保存的 Session Settings）。所有页面均有「出厂默认值」按钮可恢复 Scooter Software 默认。

### 32.1 选项页结构

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| OPT-001 | 选项对话框 | Options dialog (Tools > Options) | 全局首选项入口；左侧列表切换页面。 | — | macOS 为 Beyond Compare > Settings |
| OPT-002 | 出厂默认值 | Factory Defaults | 把当前页选项恢复默认。 | — | 逐页恢复，非全局恢复 |
| OPT-003 | 应用 / 确认 | Apply / OK | 应用或应用并关闭。 | — | —— |
| OPT-004 | 启动页 | Startup page | 启动行为相关选项。 | — | 见 OPT-005~010 |
| OPT-005 | 启动时显示主视图 | Show Home view on startup | 启动即显示主视图。 | 开启 | —— |
| OPT-006 | 启动时恢复上次会话 | Restore last session | 启动恢复上次的标签。 | 关闭 | 大目录会拖慢启动 |
| OPT-007 | 启动时检查更新 | Check for updates on startup | 启动时检查新版本。 | 依版本 | 可被管理策略禁用 |
| OPT-008 | 单实例 | Single instance | 只允许一个进程实例。 | 开启 | `/solo` 可绕过 |
| OPT-009 | 启动时的欢迎/提示 | Startup prompts | 首次运行与升级后的提示。 | 显示 | —— |
| OPT-010 | 命令行参数处理 | Command-line handling | 第二次启动时把参数转给已有实例。 | 开启 | 需要独立实例时用 `/solo` |
| OPT-011 | 通用页 | General page | 通用用户偏好。 | — | —— |
| OPT-012 | 确认消息总控 | Confirmations | 控制各类确认对话是否弹出。 | 多数开启 | 全部关闭会显著提升误操作风险 |
| OPT-013 | 最近列表长度 | MRU length | 最近文件/会话列表长度。 | 有限 | —— |
| OPT-014 | 打开方式集成 | Open With integration | 资源管理器「用 BC 打开」集成设置。 | 开启 | —— |
| OPT-015 | 选项卡页 | Tabs page | 标签行为设置。 | — | —— |
| OPT-016 | 新标签位置 | New tab position | 新标签插入位置。 | 末尾 | —— |
| OPT-017 | 关闭最后一个标签的行为 | Last tab behavior | 关闭所有标签后是否退出/显示主视图。 | 显示主视图 | —— |
| OPT-018 | 标签宽度与显示 | Tab width / display | 标签的显示方式（含标题截断）。 | 自动 | —— |
| OPT-019 | 标签的会话类型图标 | Tab icons | 标签是否显示会话类型图标。 | 显示 | —— |
| OPT-020 | 备份页 | Backup page | 备份文件策略。 | — | —— |
| OPT-021 | 覆盖前备份 | Backup on overwrite | 覆盖文件前创建 `.bak`。 | 依默认 | `/nobackups` 可禁用 |
| OPT-022 | 备份文件扩展名 | Backup extension | 备份文件命名规则。 | `.bak` | 大量备份会污染目录 |
| OPT-023 | 备份保留数量 | Backup retention | 备份文件保留策略。 | 单个 | —— |
| OPT-024 | 删除时备份 | Backup on delete | 删除前是否备份。 | 依默认 | —— |
| OPT-025 | 用打开页 | Open With page | 定义「用打开」列表中的应用。 | 空 | —— |
| OPT-026 | 添加应用 | Add application | 添加外部程序到「用打开」列表。 | — | 需指定命令行与变量 |
| OPT-027 | 应用的命令行变量 | App command-line variables | 外部程序的参数变量（如 `%f` 文件）。 | — | 变量名与转换程序的变量不同 |
| OPT-028 | 应用的作用范围 | App scope | 应用可用于哪类文件/会话。 | 全局 | —— |
| OPT-029 | 脚本页 | Scripts page | 脚本处理选项。 | — | 位于 Tweaks 中的 Scripts 区段 |
| OPT-030 | 脚本完成蜂鸣 | Beep when finished | 脚本完成提示音。 | 关闭 | —— |
| OPT-031 | 脚本完成自动关闭窗口 | Close when finished | 完成后自动关闭状态窗口。 | 关闭 | `/closescript` 可覆盖 |
| OPT-032 | 共享脚本文件夹 | Shared scripts folder | 共享脚本查找目录。 | 空 | —— |
| OPT-033 | 文件夹视图-确认 | Folder Views > Confirmations | 文件夹视图各类操作的确认项。 | 多数开启 | —— |
| OPT-034 | 确认-覆盖文件 | Confirm overwrite | 覆盖前确认。 | 询问 | —— |
| OPT-035 | 确认-删除 | Confirm delete | 删除前确认。 | 询问 | BC5 删除默认走回收站 |
| OPT-036 | 确认-复制大文件夹 | Confirm large folder copy | 大文件夹复制前确认。 | 开启 | —— |
| OPT-037 | 确认-移动 | Confirm move | 移动前确认。 | 询问 | —— |
| OPT-038 | 确认-关闭会话 | Confirm close session | 关闭有改动的会话前确认。 | 开启 | —— |
| OPT-039 | 确认-脚本 | Confirm script operations | 脚本操作确认。 | 依设置 | —— |
| OPT-040 | 文件夹视图-显示 | Folder Views > Display | 文件夹视图的字体、颜色、条纹等。 | — | —— |
| OPT-041 | 文件夹显示字体 | Folder display font | 文件夹列表字体。 | 系统字体 | 取消「采用系统值」才可自定义 |
| OPT-042 | 选中部分颜色 | Selection color | 选中项背景色。 | 浅绿 | 「采用系统值」可改系统高亮 |
| OPT-043 | 允许比较颜色显示 | Allow compare colors in selection | 选中时仍显示比较颜色。 | 开启 | —— |
| OPT-044 | 被过滤项颜色 | Filtered item color | 被过滤器排除项的颜色。 | 特定色 | 禁用过滤器时状态为未知 |
| OPT-045 | 条纹背景 | Use stripes | 隔行条纹着色。 | 关闭 | —— |
| OPT-046 | 文件夹视图-比较颜色 | Folder Views > Compare Colors | 文件夹各状态的颜色。 | 预设 | 可自定义每个状态 |
| OPT-047 | 文件夹视图-日志 | Folder Views > Log | 日志面板的大小与保留策略。 | 有限 | —— |
| OPT-048 | 文件视图-显示 | File Views > Display | 文件视图通用显示设置。 | — | —— |
| OPT-049 | 文件视图-比较颜色 | File Views > Compare Colors | 见域 33。 | 预设 | —— |
| OPT-050 | 文件视图-下一个差异项 | File Views > Next Difference | Next Difference 的跳转行为。 | 默认 | 影响「下一个差异」是否跳过某些项 |
| OPT-051 | 文件视图-文本 | File Views > Text | 文本视图的上下文行数、Tab 宽度等。 | 上下文 3 行 | —— |
| OPT-052 | 上下文行数 | Context lines | 显示上下文时包含的行数。 | 3 | —— |
| OPT-053 | Tab 宽度（全局） | Tab width | 文本视图的全局 Tab 宽度。 | 4 | 可被 File Format 覆盖 |
| OPT-054 | 自动换行默认 | Word wrap default | 文本视图默认是否换行。 | 关闭 | —— |
| OPT-055 | 文件视图-数据 | File Views > Data | 表格视图相关显示选项。 | — | —— |
| OPT-056 | 文件视图-图片 | File Views > Picture | 图片比较相关选项。 | — | —— |
| OPT-057 | Tweaks 页 | Tweaks page | 微调项集合（含脚本、比较、性能等）。 | — | 选项最多的一页 |
| OPT-058 | Tweaks-比较 | Tweaks > Comparison | 比较行为的微调。 | 默认 | —— |
| OPT-059 | Tweaks-脚本 | Tweaks > Scripts | 脚本相关（同上）。 | 默认 | —— |
| OPT-060 | Tweaks-性能 | Tweaks > Performance | 多线程、缓存等性能项。 | 默认 | 见域 36 |
| OPT-061 | 主题设置 | Theme / Appearance | 浅色/深色模式切换（BC5 新增）。 | 依系统 | —— |
| OPT-062 | 语言设置 | Language | 界面语言。 | 系统语言 | 需重启 |
| OPT-063 | 检查更新 | Check for Updates | 手动检查更新。 | — | Linux 安装 .deb/.rpm 曾挂起（5.2.1 修复） |
| OPT-064 | 注册/输入密钥 | Register / Enter Key | 输入注册码解锁正式版。 | — | —— |
| OPT-065 | 关闭 Pro 模式 | Disable Pro mode | 在 About 中关闭 Pro 以模拟 Standard。 | 开启 Pro | 仅影响试用期 |
| OPT-066 | 重置全部设置 | Reset all settings | 恢复全部默认（等价于删除设置文件）。 | — | 会丢失会话 |
| OPT-067 | 设置文件的直接编辑 | Manual settings editing | 直接编辑 XML 设置文件。 | — | 版本间结构变化易出错 |
| OPT-068 | 选项的跨平台差异 | Platform differences | macOS 为 Settings 菜单；Linux 部分项缺失。 | — | 快捷键与菜单位置不同 |
| OPT-069 | 选项的即时生效 | Immediate effect | 多数选项即时生效，少数需重启。 | 多数即时 | 语言与部分集成项需重启 |
| OPT-070 | 选项的搜索 | Option search | 部分版本提供选项搜索。 | — | —— |
| OPT-071 | 拖放行为 | Drag and drop options | 拖入文件的行为设置。 | 默认 | —— |
| OPT-072 | 外部编辑器的检测 | External editor | 指定外部文本编辑器。 | 系统默认 | —— |
| OPT-073 | 默认行尾与编码 | Default encoding/line endings | 新建文件时的默认编码与行尾。 | 系统默认 | —— |
| OPT-074 | 报告默认选项 | Default report options | 报表对话框的默认布局与输出选项。 | 内置默认 | —— |
| OPT-075 | 打印默认选项 | Default print options | 打印的默认色彩/方向。 | 单色/纵向 | —— |
| OPT-076 | 剪贴板格式偏好 | Clipboard format preference | 复制时优先的格式。 | 依上下文 | —— |
| OPT-077 | 鼠标滚轮行为 | Mouse wheel behavior | 滚轮作用于鼠标下控件（BC5 行为）。 | 新行为 | 旧版按焦点滚动 |
| OPT-078 | 高 DPI 缩放 | DPI scaling | 多显示器按显示器独立缩放（Windows，BC5 新增）。 | 开启 | 跨显示器时字体尺寸曾轻微变化（5.2.0 修复） |
| OPT-079 | 管理员策略的可见性 | Admin policy effect on options | 被策略禁用的项在界面隐藏或禁用。 | — | 见域 35 |
| OPT-080 | 便携模式选项 | Portable mode options | 便携安装下的行为。 | — | —— |
| OPT-081 | 卸载/清理 | Uninstall / cleanup | 卸载时的设置保留选择。 | 保留设置 | Windows 当前用户安装卸载曾崩溃（5.2.1 修复） |
| OPT-082 | 崩溃报告与诊断 | Crash reporting | 崩溃信息收集。 | 依版本 | —— |

---

## 33. 外观：颜色 / 字体 / 主题（Appearance）

### 33.1 文件视图颜色与字体

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| LOOK-001 | 文件视图颜色字体选项 | File View Color, Font Options | 自定义各类文件视图的视觉表现。 | — | 列表下方的元素覆盖上方的元素 |
| LOOK-002 | 元素的文字颜色与背景色 | Element text/background color | 每个元素可单独设置文字色与背景色。 | 预设 | —— |
| LOOK-003 | 元素的字体样式 | Font style | 部分元素可设置加粗/斜体。 | 常规 | —— |
| LOOK-004 | 继承与覆盖规则 | Lower elements override higher | 列表中位置越靠下的元素覆盖越靠上的元素。 | 覆盖 | 选择 Default 表示保留上层设置 |
| LOOK-005 | 默认元素 | Default element | 不做任何改动，使用上层元素的颜色。 | — | 常用作「取消自定义」 |
| LOOK-006 | 条纹背景 | Use stripes | 隔行着色。 | 关闭 | —— |
| LOOK-007 | 选区元素 | Selection element | 控制选中文本的处理。 | 中蓝色 | 「采用系统值」会丢失差异与语法着色 |
| LOOK-008 | 选中仍显示状态栏信息 | Status bar on selection | 使用系统高亮时状态栏仍描述当前位置的比较与语法类型。 | 是 | 唯一的补偿信息 |
| LOOK-009 | 重要差异元素 | Important difference | 重要差异的文字色与整行背景色。 | 红 / 浅红 | 整行背景便于横向滚动时定位 |
| LOOK-010 | 不重要差异元素 | Unimportant difference | 不重要差异的文字色与整行背景色。 | 蓝 / 浅蓝 | 浅红背景优先于浅蓝 |
| LOOK-011 | 差异文字背景色 | Text background color | 在整行背景之上再对差异文本着色。 | 有 | 用于进一步凸显 |
| LOOK-012 | 语法高亮元素颜色 | Syntax highlighting elements | 各类语法元素的颜色。 | 预设 | 与 File Format 语法一一对应 |
| LOOK-013 | 编辑器字体 | Editor Font | 用于 Text Compare / Text Merge / Table Compare。 | 默认等宽 | —— |
| LOOK-014 | 十六进制字体 | Hex Font | 用于 Hex Compare 以及其它视图底部的十六进制细节面板。 | 默认等宽 | —— |
| LOOK-015 | 列表字体 | Listing Font | 用于其它所有视图（文件夹、注册表、媒体、版本等）。 | 默认 | —— |
| LOOK-016 | 字体大小增减快捷键 | Font size shortcuts | `Ctrl+/⌘+` 增大、`Ctrl-/⌘-` 减小、重置。 | BC5 新增 | —— |
| LOOK-017 | 颜色语义的一致性 | Consistent color semantics | 红=重要、蓝=不重要贯穿所有文件视图。 | 统一 | —— |

### 33.2 文件夹视图颜色

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| LOOK-018 | 文件夹状态颜色 | Folder status colors | 相同/差异/孤立/较新/较旧各有背景色。 | 预设 | —— |
| LOOK-019 | 选中颜色 | Folder selection color | 选中项背景色。 | 浅绿 | —— |
| LOOK-020 | 被过滤项颜色 | Filtered color | 被过滤项的颜色（未知状态）。 | 特定色 | 单侧过滤冲突时用凫蓝色 |
| LOOK-021 | 条纹背景（文件夹） | Stripes in folder view | 隔行着色。 | 关闭 | —— |
| LOOK-022 | 文件夹字体 | Folder font | 文件夹列表字体。 | 系统字体 | 需取消「采用系统值」 |
| LOOK-023 | 文件夹视图图例 | Folder legend | 颜色含义图例窗口。 | 关闭 | 颜色含义随显示过滤器变化 |

### 33.3 主题与界面

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| LOOK-024 | 浅色模式 | Light mode | 浅色 UI 主题。 | 依系统 | —— |
| LOOK-025 | 深色模式 | Dark mode | 深色 UI 主题（BC5 新增）。 | 依系统 | 部分自定义控件的对比度问题在后续版本逐步修复 |
| LOOK-026 | 主题随系统 | Follow system theme | 跟随操作系统主题。 | 开启 | —— |
| LOOK-027 | 深色模式下的对比度修复 | Dark mode contrast fixes | 各项深色模式绘制缺陷的修复记录（如路径编辑框标签黑字黑底）。 | 已修复 | 需较新版本 |
| LOOK-028 | 高 DPI 下的菜单分隔线 | HiDPI menu separators | Linux 上过粗的菜单分隔线修复。 | 已修复 | —— |
| LOOK-029 | 光标绘制 | Cursor rendering | 深色模式下 I 型/忙碌光标可见性修复；Linux Wayland 分数缩放规避。 | 已修复 | —— |
| LOOK-030 | 按钮焦点绘制 | Button focus drawing | macOS 按钮获得焦点时形状变小的问题修复。 | 已修复 | —— |
| LOOK-031 | 状态栏比较结果图标 | Status bar result icon | 文件视图状态栏的比较结果图标（曾在 macOS 反向绘制）。 | 已修复 | —— |
| LOOK-032 | 标签页外观 | Tab appearance | 标签的绘制、标题截断、差异点提示。 | 默认 | —— |
| LOOK-033 | 状态栏内容 | Status bar | 显示比较状态、当前位置、语法类型、动作计数。 | 显示 | —— |
| LOOK-034 | 工具栏外观 | Toolbar appearance | 工具栏按钮与下拉。 | 默认 | 可自定义 |
| LOOK-035 | 沟槽按钮外观 | Gutter buttons | 文本视图沟槽的复制箭头按钮外观与显隐。 | 显示 | —— |
| LOOK-036 | 缩略图配色 | Thumbnail colors | 缩略图中的状态配色与视区框。 | 预设 | —— |

---

## 34. 自定义命令与快捷键（Customize Commands）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| CMD-001 | 自定义命令对话框 | Tools > Customize Commands | 控制哪些命令显示在菜单/工具栏，并设置快捷键。 | — | 需打开某类会话才能自定义该类型的命令 |
| CMD-002 | 按会话类型自定义 | Per-session-type customization | 菜单与工具栏随会话类型变化，需在该类型会话中打开对话框。 | — | 在错误类型下改不到目标命令 |
| CMD-003 | 命令搜索 | Command search box | 按命令名或描述搜索以缩小列表。 | — | —— |
| CMD-004 | 命令描述 | Command description | 选中命令时列表下方显示其描述。 | — | —— |
| CMD-005 | 菜单上显示命令 | Show on menu | 控制命令是否出现在菜单中。 | 依默认 | —— |
| CMD-006 | 工具栏上显示命令 | Show on toolbar | 控制命令是否出现在工具栏。 | 依默认 | —— |
| CMD-007 | 下拉列表上显示命令 | Show on toolbar dropdown | 部分命令可在工具栏按钮的下拉菜单中出现。 | 依默认 | —— |
| CMD-008 | 键盘快捷键设置 | Keyboard shortcut | 在 Shortcut 字段按下按键即设为快捷键。 | 默认绑定 | 冲突时弹出警告与重置选项 |
| CMD-009 | 清除快捷键 | Clear shortcut | 删除已有快捷键。 | — | —— |
| CMD-010 | 恢复默认快捷键 | Default shortcut | 恢复该命令的默认快捷键。 | — | —— |
| CMD-011 | 快捷键冲突处理 | Shortcut conflict | 冲突时提示并给出重新设置选项。 | 提示 | —— |
| CMD-012 | 显示过滤器预设 | Display filter presets | 有显示过滤器的会话类型可定义预设的工具栏布局（下拉/收藏夹/切换）。 | — | 右键工具栏可选择预设 |
| CMD-013 | 锁定工具栏位置 | Lock toolbar position | 禁止拖动调整工具栏位置。 | 关闭 | —— |
| CMD-014 | 工具栏显隐 | Show/hide toolbar | 显示或隐藏工具栏。 | 显示 | —— |
| CMD-015 | 命令参考文档 | Commands Reference | 每个视图类型的命令清单文档。 | — | —— |
| CMD-016 | 默认隐藏命令 | Hidden by default | 部分命令（如 Copy Line to Right、Delete Line、Insert Line 等）默认不在菜单中显示。 | 隐藏 | 需通过自定义命令显式显示 |
| CMD-017 | 动态命令 | Dynamic commands | 标题与图标随选中侧变化的命令（如 Copy to Other Side）。 | 动态 | 快捷键固定，标题变化 |
| CMD-018 | 命令的菜单归属 | Menu grouping | 命令按 File/Edit/Search/View/Session/Actions/Tools 分组。 | 固定分组 | —— |
| CMD-019 | 跨类型命令 | Common commands | Session 菜单中的通用命令在所有视图类型下相同。 | 相同 | —— |
| CMD-020 | 自定义命令的持久化 | Persistence | 自定义结果持久化到设置文件。 | 自动 | —— |
| CMD-021 | 会话内的临时工具栏调整 | Runtime toolbar changes | 拖动工具栏按钮的临时调整。 | 可撤销 | 与自定义命令对话框是两套入口 |
| CMD-022 | 上下文菜单 | Context menus | 右键菜单项（不受自定义命令完全控制）。 | 内置 | —— |
| CMD-023 | 快捷键与输入法的冲突 | IME conflicts | 中文/日文等输入法可能吞掉快捷键。 | — | 常见用户困惑 |
| CMD-024 | 快捷键的平台差异 | Platform key differences | Windows 用 `Ctrl`，macOS 用 `⌘`。 | 依平台 | 文档中的 `Ctrl+` 在 macOS 为 `⌘+` |

---

## 35. 国际化与平台差异（i18n / Platform / Admin Policies）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| I18N-001 | 多语言界面 | Localized UI | 界面支持多语言，随系统语言或设置切换。 | 系统语言 | 需重启生效 |
| I18N-002 | 翻译不完整的回退 | Translation fallback | 未翻译项回退为英文。 | 回退英文 | 曾出现翻译版复选框仍显示英文（5.0.1 修复） |
| I18N-003 | Unicode 全支持 | Full Unicode support | Standard 版即具备完整 Unicode 支持。 | 支持 | —— |
| I18N-004 | 扩展字符文件名的编码 | Extended filename encoding | 转换程序参数中文件名的 Unicode/ANSI 编码选择。 | 自动 | 非拉丁文件名易出错 |
| I18N-005 | 系统代码页影响 | System ANSI code page | DBCS 代码页下 .docx 转换曾失败（5.2.1 修复）。 | 自动 | DBCS 环境需较新版本 |
| I18N-006 | 平台-Windows | Windows | 支持 Windows 10/11；BC5 增强 Win11 右键菜单与外观。 | — | 部分视图类型独占 |
| I18N-007 | 平台-macOS | macOS | 原生 Apple Silicon 支持（BC5）。 | — | 部分功能缺失（Registry / Version / SSC） |
| I18N-008 | 平台-Linux | Linux | BC5 升到 Qt 5；BC5.2 起升到 Qt 6。 | — | 命令行开关前缀用 `-` |
| I18N-009 | macOS 的写时复制克隆 | Copy-on-write clones | 同一文件系统内复制使用 APFS 克隆以避免数据重复。 | 自动 | 行为与普通复制不同（延迟复制） |
| I18N-010 | Linux 桌面集成 | Linux desktop integration | Nautilus 集成；BC5 新增 KDE6 集成。 | 支持 | 依赖桌面环境 |
| I18N-011 | Linux 包管理 | .deb / .rpm | 提供 deb 与 rpm 包（5.2.0 更新 Deb822）。 | — | 通过 Check for Updates 安装曾挂起（5.2.1 修复） |
| I18N-012 | Windows 注册表位置 | Windows registry locations | 当前用户安装与所有用户安装的注册表位置差异。 | — | BC5「All Users」写入 `%AllUsersProfile%` 免 UAC |
| I18N-013 | 提权运行标识 | Elevated session indicator | 标题栏显示「Administrator:」或「(Root Session)」。 | 显示 | 提权下操作的危险性提示 |
| I18N-014 | 管理策略存储位置 | Admin policy storage | Windows 存于 `HKEY_LOCAL_MACHINE`；macOS/Linux 存于 `/etc/bcompare.conf`。 | — | 需管理员权限配置 |
| I18N-015 | 禁用检查更新策略 | DisableCheckForUpdates policy | 禁用更新检查。 | 允许 | 安装器开关 `/DisableCheckForUpdates`（别名 `/DisableUpdates`） |
| I18N-016 | 禁用远程配置策略 | DisableRemoteProfiles policy | 禁用 FTP 与云配置。 | 允许 | SVN 只读访问仍启用 |
| I18N-017 | 禁用保存密码策略 | DisableSavedPasswords policy | 隐藏「保存密码」复选框。 | 允许 | —— |
| I18N-018 | 安装器策略开关 | Installer switches | `/DisableCheckForUpdates`、`/DisableRemoteProfiles`、`/DisableSavedPasswords`（仅 Windows）。 | — | 可组合使用 |
| I18N-019 | 策略对功能可见性的影响 | Policy-driven feature hiding | 被策略禁用的功能在界面上被隐藏/禁用。 | — | 用户可能误判为「功能缺失」 |
| I18N-020 | 界面字体与 CJK 显示 | CJK font rendering | 中文/日文/韩文的显示与等宽字体选择。 | 系统字体 | 等宽 CJK 字体缺失时对齐困难 |
| I18N-021 | 区域设置对时间格式的影响 | Locale and time format | `%time%` 与时间戳显示受区域设置影响。 | 本地格式 | 跨机器日志比对困难 |
| I18N-022 | 区域设置对日期过滤的影响 | Locale and date filters | 日期过滤使用本地时间语义。 | 本地 | 跨时区脚本需注意 |
| I18N-023 | 跨平台行尾差异 | Cross-platform line endings | Windows CRLF 与 Unix LF 的差异处理。 | 默认忽略 | 参与比对需显式开启 |
| I18N-024 | 跨平台路径分隔符 | Path separator differences | `\` 与 `/` 的处理。 | 自动 | 掩码中的分隔符需注意 |
| I18N-025 | 跨平台大小写敏感差异 | Case sensitivity differences | Windows 不敏感、Linux 敏感导致的对齐差异。 | 依平台 | 经典跨平台同步问题 |
| I18N-026 | 跨平台符号链接差异 | Symlink differences | Windows 快捷方式/联接点与 Unix 符号链接的差异。 | — | 「target.lnk」曾导致自动重定向（BC5 已改为不自动跳转） |
| I18N-027 | Windows 快捷方式处理 | Windows shortcut handling | 基文件夹内的 `target.lnk` 不再自动重定向到其目标。 | 不重定向（BC5） | BC4 行为不同 |
| I18N-028 | 平台特有视图的隐藏 | Platform-specific views | Registry/Version/Media 的可用性随平台变化。 | 自动隐藏 | 命令行 `/fv=` 指定不可用类型会失败 |
| I18N-029 | 平台特有开关的隐藏 | Platform-specific switches | `/sync` 在 Linux 不可用。 | 自动 | —— |
| I18N-030 | 试用版与正式版的差异 | Trial vs licensed | 试用期同时解锁 Standard 与 Pro。 | 全功能 | 过期后返回码 104 |
| I18N-031 | 许可类型与升级路径 | License upgrade path | Standard 许可可后补差价升级 Pro。 | — | —— |
| I18N-032 | 多语言帮助文档 | Localized help | 帮助文档亦有本地化版本。 | 英文为主 | 本地化文档可能落后 |
| I18N-033 | 文本方向（RTL） | Right-to-left support | 阿拉伯语等从右到左语言的界面支持。 | 有限 | —— |

---

## 36. 文件系统细节与性能（File System / Performance）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| FS-001 | 符号链接处理 | Symbolic links | 是否跟随符号链接（含大小/时间/属性）。 | 不跟随 | 跟随可能造成目录环 |
| FS-002 | NTFS 联接点 | NTFS junctions `[Win]` | 联接点的显示与跟随。 | 不跟随 | —— |
| FS-003 | Windows 快捷方式 | Windows shortcuts (.lnk) | `.lnk` 文件的处理（BC5 不再自动重定向 `target.lnk`）。 | 不自动重定向 | BC4 行为不同 |
| FS-004 | Unix 文件类型 | Unix file types | 普通文件/目录/符号链接/块设备/字符设备/管道/FIFO 的识别。 | 识别 | BC5.2 支持按类型比较与过滤 |
| FS-005 | Unix 属主 | Unix owner | 比对文件的 Unix 属主（BC5.2 新增）。 | 不比对 | 需显式启用 |
| FS-006 | Unix 属组 | Unix group | 比对文件的 Unix 属组（BC5.2 新增）。 | 不比对 | 需显式启用 |
| FS-007 | Unix 权限 | Unix permissions | 比对文件的 Unix 权限位（BC5.2 新增）。 | 不比对 | 需显式启用 |
| FS-008 | ACL 复制 | ACL copy `[Win]` | 复制 NTFS 安全描述符。 | 关闭 | 曾崩溃（5.0.1 修复） |
| FS-009 | 所有者的复制 | Owner copy | 复制时保留属主/属组。 | 依设置 | 需特权 |
| FS-010 | 扩展属性 | Extended attributes | Windows 扩展属性（临时、离线等，BC5.2 新增）。 | 不比对 | 需 NTFS |
| FS-011 | 资源分支/备用数据流 | Resource forks / ADS | macOS 资源分支与 NTFS 备用数据流的处理。 | 有限 | 常被忽略的差异来源 |
| FS-012 | 稀疏文件 | Sparse files | 稀疏文件的识别与复制。 | 依实现 | 复制后可能变密集 |
| FS-013 | 硬链接 | Hard links | 硬链接的处理。 | 按普通文件 | 复制后失去链接关系 |
| FS-014 | 长路径 | Long paths | 超过 260 字符的路径支持。 | — | 需系统长路径支持 |
| FS-015 | 路径中的特殊字符 | Special characters in paths | 空格、引号、Unicode 字符等。 | 支持 | 命令行需正确引号 |
| FS-016 | 路径长度限制提示 | Path too long error | 路径超限时的错误提示。 | 报错 | —— |
| FS-017 | 网络路径与 UNC | UNC paths | `\\server\share` 形式。 | 支持 | 凭据与延迟问题 |
| FS-018 | 映射网络驱动器 | Mapped network drives | 盘符映射驱动器。 | 支持 | SYSTEM 账户看不到用户映射 |
| FS-019 | 可移动介质 | Removable media | U 盘/光盘等介质。 | 支持 | 直接读取避免缓存（见 DIR-033） |
| FS-020 | 文件锁定与占用 | File locking | 被其它进程占用的文件。 | 报错 | 保存时「文件被占用」错误 |
| FS-021 | 时间戳精度 | Timestamp granularity | 不同文件系统的时间戳精度（FAT 2 秒）。 | 容差 2 秒 | 跨文件系统复制后大量「较新/较旧」 |
| FS-022 | 时间戳的时区与夏令时 | Timezone and DST | 跨时区/夏令时的比较。 | 忽略 DST 关闭 | 需按场景开启 |
| FS-023 | 文件大小与实际占用 | File size vs size on disk | 逻辑大小与磁盘占用。 | 显示逻辑大小 | 稀疏/压缩文件差异 |
| FS-024 | 大文件支持 | Large file support | 超大文件（GB 级）的比对。 | 内存映射 | 32 位地址空间限制 |
| FS-025 | 内存映射比对 | Memory-mapped comparison | 大文件用内存映射读取。 | 开启 | 网络文件映射性能差 |
| FS-026 | 多线程扫描 | Multi-threaded scanning | 文件夹扫描与内容比对的并行度。 | 自动 | 机械盘上过度并发反而变慢 |
| FS-027 | 内容比对的并发 | Concurrent content comparison | 多个文件同时做内容比对。 | 自动 | 远程/网络路径下需限流 |
| FS-028 | CRC 计算性能 | CRC computation performance | CRC 计算是主要耗时点之一。 | — | 可用「快速测试跳过」缓解 |
| FS-029 | 大目录（十万级文件） | Very large directories | 十万级以上文件的目录处理。 | 后台扫描 | 内存与 UI 响应 |
| FS-030 | 深度递归目录 | Deep recursion | 极深的目录树。 | 递归 | 栈与路径长度问题 |
| FS-031 | 磁盘缓存旁路 | Bypass OS disk cache | 二进制比对时绕过 OS 缓存。 | 关闭 | 用于校验可疑介质 |
| FS-032 | 远程读取优化 | Remote read optimization | 远程/压缩包内的按需读取。 | 支持 | 反复读取会慢 |
| FS-033 | 打开压缩包的耗时 | Archive open cost | 首次打开压缩包的索引构建。 | — | 大压缩包明显 |
| FS-034 | 图片/媒体解析开销 | Media/image parsing cost | 图片与媒体文件的解析。 | — | 文件夹会话中批量比对的开销 |
| FS-035 | 编译/生成目录排除 | Build directory exclusion | 排除 `node_modules`、`target`、`.git` 等。 | 需手动配 | 未排除会显著变慢 |
| FS-036 | 自动刷新的代价 | Auto-refresh cost | 周期性刷新的开销。 | 关闭 | 大目录不建议开启 |
| FS-037 | 结果缓存 | Result caching | 比较结果与 CRC 的缓存策略。 | 内存缓存 | 关闭标签即丢 |
| FS-038 | 会话切换的开销 | Session switch cost | 在多个大目录会话间切换。 | — | 内存占用累积 |
| FS-039 | 磁盘空间占用（临时文件） | Temp space usage | 压缩包解压、转换程序产生的临时文件。 | 系统临时目录 | 空间不足导致失败 |
| FS-040 | 只读介质上的操作 | Read-only media | 只能读取的介质。 | 读操作可用 | 写操作失败需明确提示 |
| FS-041 | 文件正在被写入 | File being written | 比对过程中文件被写入。 | 读到的内容可能不一致 | 最好先快照 |
| FS-042 | 磁盘满 | Disk full | 复制/保存时磁盘满。 | 报错 | 可能留下不完整文件 |
| FS-043 | 权限不足 | Permission denied | 读写权限不足。 | 报错并记日志 | 提权运行可解但风险高 |
| FS-044 | 路径不存在 | Path not found | 基准路径不存在。 | 报错 | 脚本返回码 107 |
| FS-045 | 大小写不敏感文件系统上的重命名 | Case-only rename | 仅改变大小写的重命名在两阶段实现。 | 自动处理 | 需临时名中转 |
| FS-046 | 文件名中的非法字符 | Invalid filename characters | 非法字符的检测与提示。 | 报错 | 跨平台复制时的目标端非法字符 |

---

## 37. 设置存储、导入导出与迁移（Settings Storage）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| STORE-001 | 设置存储位置 | Where settings are stored | Windows 位于 `%APPDATA%\Scooter Software\Beyond Compare 5\`；macOS/Linux 有对应路径。 | — | 版本号出现在路径中 |
| STORE-002 | 状态文件 | BCState.xml | 保存 MRU 列表、窗体位置、窗口状态等。 | 自动 | 5.0.1 修复了 v5 State 设置导入 |
| STORE-003 | 会话文件 | BCSessions.xml | 保存命名会话与各类型会话默认值。 | 自动 | 手动编辑风险高 |
| STORE-004 | 首选项文件 | BCPreferences.xml | 保存程序选项。 | 自动 | —— |
| STORE-005 | 文件格式文件 | BCFileFormats.xml | 保存文件格式定义。 | 自动 | 结构复杂 |
| STORE-006 | 远程配置文件 | BCProfiles.xml | 保存 FTP/云配置。 | 自动 | 密码存储方式在 5.2 变更 |
| STORE-007 | 源代码控制配置 | BCSourceControl.xml | 保存 VCS 关联。 | 自动 | —— |
| STORE-008 | 设置文件的备份 | Settings backup | 建议在升级/迁移前导出 `.bcpkg`。 | — | —— |
| STORE-009 | 导出设置范围选择 | Export scope | 可选择导出会话、选项、文件格式、远程配置等。 | 全选 | 漏选会导致新机器缺配置 |
| STORE-010 | 导入设置的合并策略 | Import merge | 导入时对同名设置的处理。 | 覆盖 | 无撤销 |
| STORE-011 | 设置包的跨版本兼容 | Cross-version compatibility | v5 导出的配置可被旧版本读取，但密码视为空。 | — | 降级安装会丢密码 |
| STORE-012 | 旧版本设置的导入 | Import v4 settings | 从 BC4 导入设置。 | 支持 | 5.0.1 修复了 macOS/Linux 导入 v4 设置崩溃 |
| STORE-013 | 设置包的静默导入 | Silent import | `/silent` 时全量导入设置包。 | — | 无提示即覆盖 |
| STORE-014 | 便携安装的设置位置 | Portable settings location | 便携模式把设置放在程序目录下的数据文件夹。 | — | 移动目录即带走设置 |
| STORE-015 | 设置迁移到另一台机器 | Moving BC to another computer | 通过导出/导入 `.bcpkg` 迁移。 | — | 需一并迁移文件格式与远程配置 |
| STORE-016 | 设置文件损坏的处理 | Corrupted settings | 设置文件损坏时的降级/重置行为。 | 回到默认 | 建议定期备份 |
| STORE-017 | 多用户机器上的设置隔离 | Per-user settings | 设置按用户隔离。 | 按用户 | 所有用户安装也不共享设置 |
| STORE-018 | 版本升级后的设置兼容 | Upgrade compatibility | 大版本升级时旧设置的处理。 | 自动迁移 | 建议先导出备份 |
| STORE-019 | 命令行导入设置包 | `.bcpkg` parameter | 直接以参数传入设置包。 | — | 可与 `/silent` 组合 |
| STORE-020 | 设置文件的手工编辑 | Manual XML editing | 直接编辑 XML 实现界面未暴露的配置。 | — | 版本间结构变化易损坏 |
| STORE-021 | 会话定义的迁移 | Session portability | 会话中若含绝对路径，迁移后需调整。 | 绝对路径 | 跨机器使用建议相对路径 |
| STORE-022 | 文件格式定义的迁移 | Format portability | 自定义格式（尤其外部转换程序）迁移后可能断链。 | — | 外部程序路径需重配 |
| STORE-023 | 设置的重置 | Reset settings | 删除设置文件即恢复默认。 | — | 会丢失所有会话 |
| STORE-024 | 设置文件的版本标识 | Settings version marker | 设置文件中记录版本信息以支持迁移。 | — | —— |

---

## 38. 帮助、许可与诊断（Help / Licensing / Diagnostics）

| ID | 中文名 | 英文原名 | 行为描述 | BE | 陷阱 |
|---|---|---|---|---|---|
| MISC-001 | 帮助内容 | Help > Help Contents | 打开帮助文档。 | `F1` | —— |
| MISC-002 | 上下文敏感帮助 | Context-Sensitive Help | 针对当前对话框/视图打开对应帮助页。 | `F1` | —— |
| MISC-003 | 在线支持资源 | Scooter Software on the Web | 在浏览器中打开官网。 | — | —— |
| MISC-004 | 获得支持的资源 | Support resources | 打开支持与故障上报页面。 | — | —— |
| MISC-005 | 输入密钥 | Enter Key | 打开注册对话框输入注册码。 | — | —— |
| MISC-006 | 关于窗口 | About | 显示程序版本与许可证信息。 | — | 可在此关闭 Pro 模式 |
| MISC-007 | 关闭 Pro 模式（试用） | Disable Pro mode | 试用期限定到 Standard 功能集。 | — | 仅试用期有意义 |
| MISC-008 | 检查更新 | Check for Updates | 检查并下载新版本。 | 手动触发 | Linux 安装包曾挂起（5.2.1 修复） |
| MISC-009 | 更新检查的禁用 | Disable update check | 通过选项或管理策略禁用。 | 允许 | —— |
| MISC-010 | 试用期到期 | Trial expiration | 试用到期后返回码 104。 | — | 脚本需处理该返回码 |
| MISC-011 | 许可证类型 | License editions | Standard 与 Pro 两种许可。 | — | Pro 功能标记处需注意 |
| MISC-012 | 许可的解锁范围 | License unlocking | 注册后密钥永久解锁对应版本。 | — | Standard 可后补差价升级 |
| MISC-013 | 注册到所有用户 | Register for All Users `[Win]` | 把密钥存入 `%AllUsersProfile%` 避免 UAC 提示。 | 仅当前用户 | BC5 起行为变更 |
| MISC-014 | 安装器开关 | Installer switches | 安装时启用管理策略。 | — | 仅 Windows |
| MISC-015 | 卸载 | Uninstall | 卸载程序；可选择是否保留设置。 | 保留设置 | 当前用户安装卸载曾崩溃（5.2.1 修复） |
| MISC-016 | 错误报告与反馈 | Reporting problems | 官方问题上报渠道。 | — | —— |
| MISC-017 | 崩溃恢复 | Crash recovery | 崩溃后的会话与编辑内容恢复能力。 | 有限 | 建议及时保存 |
| MISC-018 | 日志与诊断 | Diagnostics | 脚本日志、复制日志、状态窗口等诊断手段。 | — | —— |
| MISC-019 | 常见问题定位方法 | Troubleshooting | 通过状态栏、日志、返回码定位问题。 | — | —— |
| MISC-020 | 版本号与构建号 | Version and build number | 帮助/关于中的版本与构建号（如 5.0.1.29877）。 | — | 报障时需提供 |
| MISC-021 | 变更日志 | Change log | 官方发布说明记录每版修复与增强。 | — | 排查「某问题是否已修」的关键 |
| MISC-022 | 标准版与专业版对照 | Standard vs Pro comparison | 官方提供功能对照表。 | — | 选型依据 |
| MISC-023 | 术语表 | Glossary | 官方术语定义。 | — | 对齐文档术语时有用 |
| MISC-024 | 正则表达式参考 | Regular Expressions reference | 官方提供的正则语法与示例。 | PCRE | —— |
| MISC-025 | 正则示例集 | Sample regular expressions | 常用正则示例（如匹配邮箱、行号）。 | — | —— |
| MISC-026 | 键盘快捷键参考 | Keyboard shortcuts | 各视图的默认快捷键清单。 | 见 CMD 章 | 可在自定义命令中查看 |

---

## 附录 A：会话类型 vs 视图/命令 快速对照

| 会话类型 | 命令行 `/fv=` 取值 | 会话设置 Tab | 报表命令 | 平台/版本限制 |
|---|---|---|---|---|
| Text Compare | `"Text Compare"` | Specs / Format / Importance / Alignment / Replacements | `text-report` | Replacements 需 Pro |
| Text Merge | `"Text Merge"` | Specs / Format / Importance / Alignment | `text-report` | Pro |
| Table Compare | `"Table Compare"` | Specs / Format / Columns | `data-report` | — |
| Hex Compare | `"Hex Compare"` | Session Settings | `hex-report` | — |
| Media Compare | `"Media Compare"` | Session Settings（Importance） | `media-report` | — |
| Picture Compare | `"Picture Compare"` | Specs / Format / Comparison | `picture-report` | — |
| Registry Compare | `"Registry Compare"` | Session Settings | `registry-report` | Pro + Windows |
| Version Compare | `"Version Compare"` | Specs / Importance | `version-report` | Windows |
| Folder Compare | `"Folder Compare"` | Specs / Comparison / Handling / Name Filters / Other Filters / Misc | `folder-report` / `file-report` | Alignment Override 需 Pro |
| Folder Merge | `"Folder Merge"` | Specs / Comparison / Handling / Name Filters / Other Filters / Misc | `folder-report` | Pro |
| Folder Sync | `"Folder Sync"` | Specs / Sync / Comparison / Handling / Name Filters / Other Filters / Misc | `folder-report` | `/sync` 仅 Win/macOS |
| Text Edit | `"Text Edit"` | — | — | — |
| Text Patch | `"Text Patch"` | — | `text-report` | Apply Patch 仅单文件 |

---

## 附录 B：命令行开关速查（BC5 官方清单）

| 开关 | 说明 |
|---|---|
| `/?` ` /h` `/help` | 帮助（Windows 打开帮助页；macOS/Linux 输出用法） |
| `/automerge` | 无冲突时自动合并 `[Pro]` |
| `/center=<file>` | 指定合并祖先文件 `[Pro]` |
| `/closescript` | 完成后关闭脚本窗口 |
| `/edit` | 打开 Text Edit 视图 |
| `/expandall` | 初始比较时展开所有子文件夹 |
| `/favorleft` / `/favorright` | 偏好某一侧绘制 | 
| `/filters=<masks>` | 指定初始名称过滤器 |
| `/force` | 冲突以 CSV 风格标记写入输出 `[Pro]` |
| `/fv=<type>` `/fileviewer=<type>` | 指定视图类型 |
| `/iu` `/ignoreunimportant` | 忽略不重要差异 |
| `/mergeoutput=<file\|path>` | 指定合并输出 `[Pro]` |
| `/nobackups` | 不创建备份文件 |
| `/qc=<type>` `/quickcompare=<type>` | 快速比较并设置退出码（size/crc/binary/缺省规则化） |
| `/reviewconflicts` | 有冲突时打开合并视图 `[Pro]` |
| `/ro` `/readonly` | 全部只读 |
| `/ro1` `/lro` `/leftreadonly` | 左侧只读 |
| `/ro2` `/rro` `/rightreadonly` | 右侧只读 |
| `/savetarget=<file>` | Save 命令改写指定文件 |
| `/silent` | 抑制一切交互 |
| `/solo` | 强制新实例 |
| `/sync` | 打开 Folder Sync 视图 `[Win] [macOS]` |
| `/title1..4=` `/lefttitle=` `/righttitle=` `/centertitle=` `/outputtitle=` | 路径框显示自定义标题 |
| `/vcs1..4=` `/vcsleft=` `/vcsright=` `/vcscenter=` `/vcsoutput=` | 路径框显示 VCS 路径（并用于挑选文件格式） |

> macOS/Linux 用户应使用 `-` 前缀而非 `/`。

---

## 附录 C：返回码速查

| 码 | 含义 |
|---|---|
| 0 | 成功（Success） |
| 1 | 二进制相同（Binary same） |
| 2 | 规则化相同（Rules-based same） |
| 11 | 二进制不同（Binary differences） |
| 12 | 相似（Similar） |
| 13 | 规则化不同（Rules-based differences） |
| 14 | 检测到冲突（Conflicts detected） |
| 100 | 未知错误（Unknown error） |
| 101 | 检测到冲突且未写输出（Conflicts detected, merge output not written） |
| 102 | BComp.exe 无法等待 BCompare.exe 结束 |
| 103 | BComp.exe 找不到 BCompare.exe |
| 104 | 试用期过期（Trial period expired） |
| 105 | 脚本文件加载失败 |
| 106 | 脚本语法错误 |
| 107 | 脚本未能加载文件夹或文件 |

---

## 附录 D：脚本命令速查（29 条）

`attrib`、`beep`、`collapse`、`compare`、`copy`、`copyto`、`criteria`、`delete`、`expand`、`file-report`、`filter`、`folder-report`、`hex-report`、`load`、`log`、`media-report`、`move`、`moveto`、`option`、`picture-report`、`registry-report`、`rename`、`select`、`snapshot`、`sync`、`data-report`、`text-report`、`touch`、`version-report`

---

## 附录 E：LqCompare 拆分建议（映射提示，非功能点）

| 建议 issue 簇 | 对应功能域 | 说明 |
|---|---|---|
| 核心引擎：对齐算法 | 4 | 先做 Standard 方法 + 偏斜容差 + 最近匹配，再补 LCS |
| 核心引擎：规则与重要性 | 4 / 14 | Importance 与 File Format 分层，是后续一切比对语义的基础 |
| 文本视图 UI | 5 / 6 | 缩略图、沟槽、行背景着色、导航、编辑器命令 |
| 合并引擎与合并视图 | 7 | Take 命令族 + 冲突判定 + 输出窗格 |
| 目录遍历与比较引擎 | 8 | 快速测试 → 内容比对的分层判定 |
| 目录视图 UI | 9 | 列、状态着色、显示过滤器、展开折叠 |
| 文件操作 | 10 | 复制/移动/删除/重命名/属性，含回收站与备份 |
| 同步与会话 | 11 / 12 | 预览 → 执行的两阶段模型 |
| 过滤器与掩码 | 13 | 掩码语法是脚本与 UI 的公共依赖 |
| 规则与格式系统 | 14 / 15 | File Format 管理器 + 替换规则 |
| 报表与补丁 | 16 / 17 | 布局 × 输出目标 的正交组合 |
| 专用视图 | 18~24 | Hex / Table / Picture / Media / Registry / Version / Text Edit |
| 容器与远程 | 25 / 26 / 27 | 压缩包抽象层、快照格式、远程抽象层 |
| 集成 | 28 | VCS 集成（先做「作为外部 diff 工具」） |
| 自动化 | 29 / 30 / 31 | 命令行 → 脚本 → 计划任务 |
| 全局配置与外观 | 32 / 33 / 34 | Options、颜色字体、自定义命令 |
| 工程化 | 35 / 36 / 37 / 38 | 平台差异、性能、设置存储、诊断 |

---

*文档版本：v1.0 ｜ 生成日期：2026-09-20 ｜ 对标基准：Beyond Compare 5.2.x*

