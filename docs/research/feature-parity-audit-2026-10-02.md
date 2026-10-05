# LqCompare 官方功能对标与缺口审计

审计日期：2026-10-02 UTC。目的：把 Beyond Compare 和 TortoiseGit 的手册、发布变更转成可追溯的实现与验收计划，避免把规格数量、按钮数量、测试工程数量当成完整功能覆盖率。

结论：现有 369 条规格已覆盖多数功能域，但仍有明显的实现缺口和规格覆盖缺口。应复用现有 ACTION-ID 分批补齐，同时增加远程数据源等真正缺少的后端规格。本文只提供研究与源码审计，**不宣称全量功能已经实现，也不替代三平台 GUI 验收**。

## 1 基线和证据边界

- Beyond Compare：官方 BC5 帮助及 [5.x 完整变更页](https://www.scootersoftware.com/all/v5changelog)，截至本次检查最新条目为 **5.2.6.32774，2026-09-24**。不是 5.5.x。Standard/Pro 以[官方版本区分](https://www.scootersoftware.com/v5help/standard_vs_pro.html)及页面图标为准
- TortoiseGit：官网[发布说明](https://tortoisegit.org/docs/releasenotes/)最新条目 **2.19.1.0，2026-06-27**；[TortoiseGitMerge 手册入口](https://tortoisegit.org/docs/tortoisegitmerge/)标注 2.18.0。检索缓存中 `/index.html` 曾显示 2.16.0，不能用旧缓存覆盖当前发布版本
- 仓库：`tools/spec/*.py` 是规格真源；`docs/PRD-actions.md` 是生成物，不应手工修改。程序化读取结果为 **369 个唯一 ACTION-ID**；`Code/Tests` 内 **73 个 .pro**。两者都只是库存统计
- 旧研究：BC 表格有 1682 个唯一原始 ID；TG 有 1166 个唯一 ID，其中 F12 被重复展示。这些计数可以复核，但**真实性、独立性、与实现覆盖率均不能由计数推导**。本文是跨功能域核查与全部 369 条规格映射，尚未逐个重新认证旧研究的全部 2848 个原始 ID；旧表中没有具体官方出处的细节仍待逐项核验
- 源码审计基于本次共享工作树。基准主线为 `01922ebd61340d7e6c8d42f274040ae9e78817c3`，审计期间开发分支含后续修复及未提交目录比较改动；本文文件/符号证据可重新定位，不把活动工作树误报成已发布版本
- 本文以独立摘要和链接记录公开功能；不复制商业实现或大段手册。本文没有运行 BC/TortoiseGit 二进制，也没有重跑 LqCompare 全量测试。需复验的行为明确标为 U

### 状态码

| 码 | 本文含义 | 不能据此声称 |
| --- | --- | --- |
| C | 找到相关实现或明确 API/视图接线；范围由 E 证据说明 | 完成整个 ACTION-ID；UI/跨平台已验收 |
| G | 源码明示禁用、不支持，或当前算法/数据模型与官方功能存在具体缺口 | 整个模块不存在 |
| U | 本次尚未逐项核验运行行为 | 已实现或一定缺失 |
| S | 产品规格范围不足/后续阶段；需规格决策 | 已经有可交付后端 |
| X | LqCompare 自有扩展或本轮未找到竞品官方支持 | 必须复制的竞品功能 |

组合如 C/G 表示同一能力组部分已有代码、部分明确缺失。附录逐个 ACTION-ID 保留“未完成条目级验收”，避免组合状态被误读为整条完成。

## 2 应先纠正的对标结论

1. `beyondcompare-features.md` 中“5.0～5.5.x 全部发布说明”没有当前官网依据；应改成具体版本与检查日期。BC 5.0 的 Linux Qt5 记录也不能代表当前 5.2 的依赖版本
2. `research/README.md` 中“TortoiseGitMerge Ribbon 没有任何 tooltip”的绝对判断与[官方控制说明](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug-toolbar.html)矛盾。XML 中未发现某属性不能证明运行时没有提示；LqCompare 两段式提示仍可作为自身标准
3. [官方 diff 手册](https://tortoisegit.org/docs/tortoisegit/tgit-dug-diff.html)明确不提供目录层次比较。Git 修订的变更列表、子模块差异属于客户端能力，不能当作 TGM 目录同步/三路目录合并
4. BC Pro 专项包括文本/目录三路合并、文本替换、目录配对覆盖、SFTP/FTPS、Windows SCC 与 Registry Compare。外部 `git difftool` 调用与 SCC 插件不是同一能力；勿把所有 Git 调用都标 Pro
5. [BC 命令行](https://www.scootersoftware.com/v5help/command_line_reference.html)列出 13 种视图；[归档手册](https://www.scootersoftware.com/v5help/archive_files.html)把归档作为文件夹数据源。LqCompare 独立归档页可以保留，但应标为自身信息架构。`PRD.md`“13 种”表实际另加归档，需统一口径
6. BC [媒体命令](https://www.scootersoftware.com/v5help/commandsmedia.html)支持比较、播放、报告和复制文本，没有据此证明标签编辑/保存；[图片命令](https://www.scootersoftware.com/v5help/commandspix.html)的旋转/翻转用于视图比较，不能据此宣称通用图像写回。MED-002/004、IMG-007 等应保留为自有扩展或待证实项
7. [BC Patch 官方 KB](https://www.scootersoftware.com/kb/patch)区分“可生成多文件补丁”和“Apply 仅单文件”；TGM 的多文件补丁预览/应用是另一项能力。两者不能合并成无条件的全格式兼容
8. [BC 快照](https://www.scootersoftware.com/v5help/snapshots.html)保存清单，可附 CRC，不保存文件内容；它不是备份。基于历史快照的同步删除传播不能仅由 BC Update Both 推定
9. TGM 手册不同章节对可编辑源窗格描述存在差异：Viewing Modes 说明 Enable Edit 可打开源侧编辑，Conflict 章节仍有“只可编辑结果”的表述。相关行为须按目标版本实测，不从单页推导全局不变量
10. 闭源商用约束来自本仓 PRD；[TortoiseGitMerge 许可说明](https://tortoisegit.org/docs/tortoisegitmerge/tme-preface.html)说明 GPL。只复用行为需求与独立测试，不直接搬运其源代码/资源。BC 使用受[官方许可协议](https://www.scootersoftware.com/v5help/license_agreement.html)约束；Standard/Pro 是竞品归属标记，不是要求 LqCompare 复制收费体系

## 3 功能矩阵

每行是一组相关的可测试行为；ACTION-ID 已全部检查是否存在于当前真源。来源列链接到官方具体章节，E 编号链接到后面的本地代码证据。空白或 U 不代表竞品没有该功能。BC 默认指 Standard 已有、Pro 亦包含；平台/版本例外见各行。

### 会话与工作区

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p01"></a>P01 | 13 种 BC 视图类型及按文件格式路由；归档是数据源，不是第 14 种 BC 会话 | BC Standard；三路与 Registry 例外 | SESS-001..002,SESS-005,SESS-013..014 | C；[E01](#e01) | [S006](#s006), [S051](#s051) |
| <a id="p02"></a>P02 | Home 新建、拖放、搜索、已存与自动保存会话 | BC Standard | SESS-003,UI-033 | C；[E01](#e01) | [S075](#s075) |
| <a id="p03"></a>P03 | 命名会话、分组、重命名、锁定、最近历史、类型默认值 | BC Standard | SESS-004,SESS-008..009,SESS-015 | U；[E01](#e01) | [S035](#s035) |
| <a id="p04"></a>P04 | 当前视图、类型默认值、父会话中特定文件对或所有文件的设置作用域 | BC Standard | SESS-006..007 | C；[E01](#e01) | [S051](#s051) |
| <a id="p05"></a>P05 | 只读共享会话包、导入导出、工作区与标签恢复 | BC Standard | SESS-010..011,OPT-013 | U；[E01](#e01) | [S067](#s067), [S007](#s007) |
| <a id="p06"></a>P06 | 剪贴板比较、会话路径/标题、只读开关、从外部调用传参 | BC Standard | SESS-012,SESS-016..017,CLI-001..002,CLI-005 | C；[E14](#e14) | [S006](#s006), [S086](#s086) |
| <a id="p07"></a>P07 | 重载、保存提示、脏状态、比较统计、部分失败可见 | BC/TGM | SESS-018..020 | C；[E01](#e01) | [S016](#s016), [S086](#s086) |
### 文本算法

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p08"></a>P08 | Myers、Patience、Standard、Unaligned；相似行、禁止差异对齐、偏斜范围 | BC Standard | TXT-002..005 | C/G；[E02](#e02) | [S062](#s062) |
| <a id="p09"></a>P09 | 手动标记/对齐/断开；合并支持多行选择对齐 | BC Standard 文本；Merge Pro | TXT-006..007,MRG-002 | U；[E02](#e02) | [S016](#s016), [S065](#s065), [S083](#s083) |
| <a id="p10"></a>P10 | 字符大小写、前/中/尾空白、空行重要性、按行尾比较 | BC Standard；TGM 白空白模式 | TXT-008..010,TXT-013 | C；[E02](#e02) | [S064](#s064), [S090](#s090) |
| <a id="p11"></a>P11 | 语法元素与注释重要性、语法着色；显示忽略和真实保存字节分离 | BC Standard | TXT-011,TXT-013,TXT-018,FMT-003 | C/U；[E12](#e12) | [S033](#s033), [S031](#s031) |
| <a id="p12"></a>P12 | 左右不同文本的等价替换；不能把任意正则归一化直接当作完整等价 | BC Pro | TXT-012,TXT-013,FMT-003 | C/U；[E02](#e02) | [S066](#s066) |
### 文本文件

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p13"></a>P13 | 自动/显式编码、左右分别覆盖、BOM、混合 EOL、末行无换行 | BC Standard；TGM | TXT-014..016 | C/G；[E03](#e03) | [S063](#s063), [S030](#s030), [S089](#s089) |
| <a id="p14"></a>P14 | Tab 宽度、空格/制表符/行尾可见、插入空格、智能 Tab、EditorConfig 缩进 | BC Standard；智能 Tab/EditorConfig 见 TGM | TXT-017,TXT-031,TXT-033,OPT-007 | U；[E03](#e03) | [S025](#s025), [S032](#s032), [S097](#s097) |
### 文本视图

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p15"></a>P15 | 双栏或上下布局、联动滚动、行号、差异缩略图、行文本/Hex/对齐详情 | BC Standard | TXT-001,TXT-019..020,TXT-023..025 | C/U；[E02](#e02) | [S080](#s080) |
| <a id="p16"></a>P16 | 单窗格/双窗格/三窗格切换、行内增删标记、忽略变化图标、手改铅笔图标 | TGM | TXT-021,TXT-025,TXT-033,MRG-001,MRG-004 | C/U；[E02](#e02) | [S098](#s098), [S089](#s089) |
| <a id="p17"></a>P17 | 差异块、块内差异、父文件夹下一差异文件导航；末端行为可配置 | BC Standard | TXT-022,TXT-036,DIR-029 | C/U；[E02](#e02) | [S082](#s082) |
| <a id="p18"></a>P18 | 相同/差异/上下文筛选、折叠未变段、编号书签、字词匹配高亮 | BC Standard；TGM | TXT-026..028 | U；[E02](#e02) | [S016](#s016), [S092](#s092), [S089](#s089) |
| <a id="p19"></a>P19 | 查找替换、正则、全字/大小写、搜索方向/侧、环回与全部选择 | BC Standard；TGM | TXT-028,TXT-031 | C/U；[E02](#e02) | [S050](#s050), [S092](#s092) |
### 文本编辑

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p20"></a>P20 | 左右编辑后重比、行/块/整文件双向复制、剪切粘贴、撤销重做、保存/另存 | BC Standard；TGM 有编辑侧开关 | TXT-029..031,TXT-038 | C；[E03](#e03) | [S025](#s025), [S016](#s016), [S098](#s098) |
| <a id="p21"></a>P21 | 换行显示、超长行预算、列选择、保持视图位置、大文件不伪报相同 | BC Standard；TGM | TXT-032,TXT-034..035,TXT-039..040 | C/G；[E02](#e02), [E03](#e03) | [S083](#s083), [S016](#s016), [S097](#s097), [S092](#s092) |
### 文本合并

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p22"></a>P22 | 两/三输入到独立可编辑输出；共同祖先可隐藏；输出可独立窗口 | BC Pro；TGM 三窗格 | MRG-001..003,MRG-008,MRG-012 | C/U；[E04](#e04) | [S081](#s081), [S098](#s098) |
| <a id="p23"></a>P23 | 自动非冲突合并、同改动合并、冲突范围与相邻改动距离设置 | BC Pro；TGM 冲突 | MRG-003..004,MRG-014,MRG-017 | C/G；[E04](#e04) | [S072](#s072), [S065](#s065) |
| <a id="p24"></a>P24 | 取左/右/祖先、两块顺序、整文件决策、手工输出、撤销决策 | BC Pro；TGM 两种块顺序 | MRG-005..009,MRG-015 | C；[E04](#e04) | [S017](#s017), [S088](#s088) |
| <a id="p25"></a>P25 | 冲突筛选/导航、忽略差异仍须解决、保存前未解冲突提示 | BC Pro；TGM | MRG-010..011,MRG-013,MRG-018 | C/U；[E04](#e04) | [S072](#s072), [S088](#s088), [S086](#s086) |
| <a id="p26"></a>P26 | CLI 祖先/两侧/输出/标题、自动合并、强制冲突标记、保存要求和返回码 | BC Pro；TGM | MRG-016,CLI-004..005 | C/G；[E14](#e14) | [S006](#s006), [S086](#s086) |
### 目录比较

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p27"></a>P27 | 递归/后台扫描、孤儿扫描、展开策略、刷新、进度与取消 | BC Standard；TG 无目录层次比较 | DIR-001..003,DIR-015..016,DIR-034,DIR-038 | C；[E05](#e05) | [S053](#s053), [S008](#s008), [S090](#s090) |
| <a id="p28"></a>P28 | 大小、时间容差、DST/时区、文件名大小写/Unicode 规范化、不同扩展名对齐 | BC Standard | DIR-004..006,DIR-010 | C/U；[E05](#e05) | [S052](#s052) |
| <a id="p29"></a>P29 | CRC、逐字节、格式规则比较；快速测试与内容结论优先级必须可解释 | BC Standard | DIR-007..011 | C/G；[E05](#e05) | [S052](#s052) |
| <a id="p30"></a>P30 | Windows 扩展属性/版本；Unix 权限、owner/group/type；链接与特殊文件 | BC Standard；平台限定 | DIR-026,DIR-036..037,PLAT-002,PLAT-007..008 | C/U；[E05](#e05) | [S052](#s052), [S053](#s053) |
| <a id="p31"></a>P31 | 状态色/图标/统计列、排序、展开折叠、基准目录切换、历史导航 | BC Standard | DIR-012..016,SESS-019 | C/U；[E05](#e05) | [S008](#s008) |
| <a id="p32"></a>P32 | 显示状态与名称/属性过滤分离；空目录模式、忽略目录结构 | BC Standard | DIR-017..018,FILT-005..006,FILT-010 | C/U；[E05](#e05), [E12](#e12) | [S020](#s020) |
### 目录操作

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p33"></a>P33 | 复制/移动/删除/重命名/建目录/属性/Touch；方向、隐藏项范围和失败重试 | BC Standard | DIR-019..024,DIR-026..027,PLAT-003,PLAT-008 | C/U；[E05](#e05) | [S021](#s021), [S038](#s038), [S008](#s008) |
| <a id="p34"></a>P34 | 复制时间戳、创建时间、DOS 短名、NTFS 权限；覆盖前备份 | BC Standard；部分 Windows | DIR-020,DIR-027,OPT-005 | U；[E05](#e05) | [S053](#s053), [S036](#s036) |
| <a id="p35"></a>P35 | 手动名称配对、模式配对、不同名称比较、会话内格式关联覆盖 | BC Pro（Folder alignment overrides） | DIR-031..032,FMT-008 | U；[E05](#e05) | [S054](#s054) |
| <a id="p36"></a>P36 | 子文件查看器、任选两文件比较、父文件夹、外部工具、系统定位 | BC Standard；Windows Explorer 专用入口 | DIR-028..030,PLAT-005 | C/U；[E05](#e05) | [S008](#s008), [S039](#s039) |
### 目录同步

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p37"></a>P37 | Update 左/右/双向与 Mirror 左/右；预览、排除操作、详细文件检查 | BC Standard | DIR-025,SYNC-001..003,SYNC-009..010 | C；[E06](#e06) | [S022](#s022) |
| <a id="p38"></a>P38 | 执行复制/删除、进度、错误/取消、同步日志、后续核验与安全阈值 | BC 基础执行；阈值和核验是 Lq 强化 | SYNC-004..008,SYNC-012 | C/U；[E06](#e06) | [S010](#s010), [S038](#s038) |
### 目录合并

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p39"></a>P39 | 左右及可选祖先目录到独立输出；每项决策、目录传播、冲突筛选 | BC Pro | FMG-001..004,FMG-007 | C；[E07](#e07) | [S073](#s073), [S009](#s009) |
| <a id="p40"></a>P40 | 应用写回与结果复验；预演/备份/失败恢复/审计 | BC Pro 写回；恢复保证按 Lq 规范 | FMG-005..006,FMG-008..010 | G；[E07](#e07) | [S009](#s009) |
### 过滤与格式

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p41"></a>P41 | 文件/目录包含和排除掩码、规则预设、临时排除、快捷设置入口 | BC Standard | FILT-001..002,FILT-007..008,FILT-011..012 | C/U；[E12](#e12) | [S055](#s055), [S008](#s008) |
| <a id="p42"></a>P42 | 日期/大小/属性/内容筛选；排除保护文件；被一侧筛掉的配对项仍显式显示 | BC Standard | FILT-003..004,FILT-006,FILT-009 | C/U；[E12](#e12) | [S020](#s020), [S056](#s056) |
| <a id="p43"></a>P43 | 格式定义/优先级/识别/关联与自定义管理；会话覆盖不污染全局 | BC Standard | FMT-001..002,FMT-005..012 | C；[E12](#e12) | [S026](#s026), [S054](#s054) |
| <a id="p44"></a>P44 | 语法元素、行权重、列数据、独立行；并非完整语言解析器 | BC Standard | FMT-003,TXT-013,TXT-018 | C/G；[E12](#e12) | [S033](#s033), [S031](#s031), [S032](#s032) |
| <a id="p45"></a>P45 | Office/PDF 等先抽文本、外部转换器、读写转换；转换失败与只读边界 | BC Standard；可用转换依平台/组件 | FMT-004,SESS-017 | C/G；[E12](#e12) | [S030](#s030) |
### 表格

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p46"></a>P46 | CSV/TSV、固定宽度、分隔符/引号/连续分隔符、表头、编码与区域格式 | BC Standard | DATA-001,DATA-006,FMT-005 | C/G；[E08](#e08) | [S029](#s029), [S028](#s028) |
| <a id="p47"></a>P47 | Excel 多工作表、HTML 多表、工作表名称/顺序/手动映射 | BC Standard | DATA-007 | G；[E08](#e08) | [S071](#s071), [S061](#s061), [S027](#s027) |
| <a id="p48"></a>P48 | 列按位置/名称/自定义映射；复合键；无键内容对齐/预排序 | BC Standard | DATA-002..003 | C；[E08](#e08) | [S059](#s059), [S060](#s060) |
| <a id="p49"></a>P49 | 单元格状态、数值容差、日期时间容差、忽略列、区域解析 | BC Standard | DATA-004..005 | C/U；[E08](#e08) | [S079](#s079), [S028](#s028), [S005](#s005) |
| <a id="p50"></a>P50 | 差异行/列/工作表导航、隐藏相同列、列宽、自选多行、详情与双向滚动 | BC Standard | DATA-008,DATA-012 | C/U；[E08](#e08) | [S015](#s015) |
| <a id="p51"></a>P51 | 单元格/行复制、增删行列、编辑、撤销重做、保存、查找替换/导出报告 | BC Standard；转换器可限制保存 | DATA-009..011 | G；[E08](#e08) | [S015](#s015), [S027](#s027) |
### 十六进制

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p52"></a>P52 | 原始字节双栏/上下、地址、字符/数值解释、小端/大端、差异统计 | BC Standard | HEX-001..003,HEX-009..010 | C；[E09](#e09) | [S074](#s074), [S011](#s011) |
| <a id="p53"></a>P53 | Complete/Fast/None 字节对齐及文件锁阈值 | BC Standard | HEX-008 | C/G；[E09](#e09) | [S057](#s057) |
| <a id="p54"></a>P54 | 按地址跳转、查找替换、字节复制/插删/编辑、撤销重做和保存 | BC Standard | HEX-002,HEX-004..005,HEX-007 | C/G；[E09](#e09) | [S011](#s011) |
### 图片

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p55"></a>P55 | 常见图片与受 OS/codec 限制的格式；32bpp 比较，不保证 HDR/广色域 | BC Standard；TGitIDiff 独立程序 | IMG-001,IMG-008,IMG-010,FMT-006 | C/U；[E10](#e10) | [S077](#s077), [S090](#s090) |
| <a id="p56"></a>P56 | 容差、差异强度、混合；特定颜色替换；TGitIDiff alpha/XOR | BC Standard；TGitIDiff | IMG-002..003 | C/U；[E10](#e10) | [S041](#s041), [S058](#s058), [S090](#s090) |
| <a id="p57"></a>P57 | 自动比例、90°旋转、翻转、位置偏移/微调、缩放/平移/全屏/元数据 | BC Standard；TGitIDiff 联动平移 | IMG-004..006 | C/U；[E10](#e10) | [S041](#s041), [S013](#s013), [S090](#s090) |
| <a id="p58"></a>P58 | 报告与剪贴板；视图旋转并不证明有通用图像编辑/保存功能 | BC Standard；IMG-007 部分为 Lq 扩展 | IMG-007,IMG-009 | C/U；[E10](#e10) | [S013](#s013) |
### 媒体

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p59"></a>P59 | MP3/FLAC/MP4-AAC 标签比较；重要性、搜索、详情、单侧/双侧播放 | BC Standard | MED-001,MED-003,MED-005..006 | C/G；[E11](#e11) | [S076](#s076), [S012](#s012) |
| <a id="p60"></a>P60 | 标签写回与波形/频谱不应当计为已证实 BC 功能 | Lq 扩展或待证实 | MED-002,MED-004 | X/U；[E11](#e11) | [S012](#s012) |
### 注册表

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p61"></a>P61 | 本机/远程实时注册表及 .reg 对比；键/类型/值、孤儿项、读权限错误 | BC Pro Windows；Lq 可跨平台读导出文件 | REG-001..002,REG-005..006 | C/G；[E11](#e11) | [S078](#s078) |
| <a id="p62"></a>P62 | 复制/删除/改名/新增键值、改值类型、导出与报告 | BC Pro Windows；需安全写回设计 | REG-003..004 | G；[E11](#e11) | [S078](#s078), [S014](#s014) |
### 版本资源

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p63"></a>P63 | PE 版本字段比较与报告；MUI/更多头字段是 BC5 变化 | BC Standard Windows；Lq 解析器可跨平台 | VER-001..002,VER-004..005 | C/U；[E11](#e11) | [S076](#s076), [S019](#s019), [S083](#s083) |
### 归档

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p64"></a>P64 | 以文件/按需展开/始终目录处理；ZIP/7z/RAR/tar 等读取边界依格式 | BC Standard | ARC-001,ARC-003,DIR-033,FMT-007 | C/G；[E13](#e13) | [S002](#s002), [S053](#s053) |
| <a id="p65"></a>P65 | 成员真实内容比对、可写格式内部复制/改名/保存；只读格式禁用写 | BC Standard | ARC-002,ARC-004,ARC-007 | G；[E13](#e13) | [S002](#s002), [S025](#s025) |
| <a id="p66"></a>P66 | 路径逃逸/链接/加密/损坏/尺寸预算与依赖更新；Total Commander 插件限 Windows | BC Standard；安全用例为 Lq 验收要求 | ARC-006,ARC-008,ENG-013 | C/U；[E13](#e13) | [S002](#s002), [S004](#s004) |
### 快照

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p67"></a>P67 | 只读目录清单与可选哈希；不能恢复内容，不能执行字节/规则比对 | BC Standard；BC CRC，Lq SHA-256 | SNAP-001..004,SNAP-006 | C；[E13](#e13) | [S068](#s068) |
| <a id="p68"></a>P68 | 用于三方历史判断的同步基线；无基线不得猜删除传播 | Lq 扩展，不是已证实 BC Sync 模式 | SNAP-005,SYNC-011 | X/C；[E06](#e06), [E13](#e13) | [S022](#s022), [S068](#s068) |
### 远程

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p69"></a>P69 | FTP、FTP over SSL/FTPS、SFTP；连接/代理/主机身份/超时/重试 | FTP Standard；SFTP/FTPS Pro | OPT-009,DIR-036 | G/S；[E15](#e15) | [S047](#s047), [S043](#s043) |
| <a id="p70"></a>P70 | 传输 ASCII/Binary/自动、时间戳/Unix 权限、压缩、带宽、服务器兼容 | BC；安全 FTP 属 Pro | OPT-009,DIR-020 | G/S；[E15](#e15) | [S044](#s044) |
| <a id="p71"></a>P71 | Amazon S3、Dropbox、OneDrive、WebDAV 命名 profile 与 Subversion 修订只读源 | BC 官方远程 profile | OPT-009,VCS-018 | G/S；[E15](#e15) | [S047](#s047), [S042](#s042), [S045](#s045), [S046](#s046) |
| <a id="p72"></a>P72 | 凭据生命周期、系统凭据引用、授权失败与策略禁用；禁止把密码保存示例照搬为安全设计 | Lq 安全实现；BC 有管理员策略 | OPT-009,OPT-012,ENG-013 | G/S；[E15](#e15) | [S001](#s001), [S043](#s043) |
### 报告

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p73"></a>P73 | 文本/文件夹及专用视图报告；HTML/纯文本/剪贴板/打印与预览 | BC Standard；Registry Pro | RPT-001..004,TXT-037,DIR-035,DATA-011,IMG-009,MED-005,REG-004 | C/G；[E14](#e14) | [S023](#s023), [S015](#s015), [S013](#s013), [S012](#s012), [S014](#s014) |
| <a id="p74"></a>P74 | 范围/行号/忽略项、布局/摘要/统计、编码、流式/取消、自动化 | BC 报告；部分健壮性为 Lq 标准 | RPT-003,RPT-005..010,RPT-012 | C/U；[E14](#e14) | [S023](#s023), [S048](#s048) |
### 补丁

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p75"></a>P75 | 生成单文件/多文件差异；Normal/Context/Unified 能力须按格式验收 | BC 与 TGM；BC Apply 仅单文件 | PAT-001,PAT-003,EDV-002,EDV-004 | C/U；[E14](#e14) | [S040](#s040), [S086](#s086) |
| <a id="p76"></a>P76 | 查看/预览/反向、逐文件或全部应用、多文件状态；冲突与路径安全 | TGM 多文件；BC Apply 限单文件 | PAT-002,PAT-004..006,EDV-003,EDV-005 | C/U；[E14](#e14) | [S098](#s098), [S095](#s095), [S086](#s086), [S040](#s040) |
### 独立编辑

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p77"></a>P77 | 独立 Text Edit 与 Text Patch 导航、换行、搜索和书签 | BC Standard | EDV-001..002 | U；[E01](#e01) | [S006](#s006), [S018](#s018) |
### Git 比较

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p78"></a>P78 | 工作树/HEAD/任意两修订/分支标签；交换方向与变更文件列表筛选/导出 | TortoiseGitProc，不是 TGM 内核 | VCS-001..005,VCS-009..010 | C/U；[E16](#e16) | [S090](#s090) |
### Git 历史

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p79"></a>P79 | 日志搜索/引用/图形列；Blame 的行修订、颜色与上下文跳转 | TortoiseGitProc/Blame | VCS-006..008,VCS-011..013 | C/U；[E16](#e16) | [S094](#s094), [S085](#s085) |
### Git 集成

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p80"></a>P80 | 外部 diff/merge 配置、按扩展名选择、双/三文件与别名标题；Shift 临时回内置 | TortoiseGit 与 BC 外部调用 | VCS-014..016,VCS-018..019,CLI-001 | C/U；[E14](#e14), [E16](#e16) | [S087](#s087), [S086](#s086), [S006](#s006) |
| <a id="p81"></a>P81 | 子模块修订方向、工作树、多仓库、缺对象/未初始化状态 | TortoiseGit 客户端 | VCS-017 | U；[E16](#e16) | [S090](#s090) |
| <a id="p82"></a>P82 | SCC check-in/out 与 Git difftool 不同；不把 BC Pro SCC 标志扩展到所有 Git 调用 | BC Pro Windows SCC | VCS-001,VCS-016,PLAT-005 | S/U；[E16](#e16) | [S024](#s024), [S069](#s069) |
### CLI

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p83"></a>P83 | 文件/目录/命名会话/标准输入/补丁/设置包/脚本；平台开关风格、引号和路径 | BC；TGM 用各自独立契约 | CLI-001..003,CLI-006..007,CLI-010..012 | C/U；[E14](#e14) | [S006](#s006), [S086](#s086) |
| <a id="p84"></a>P84 | 无界面比较、报告输出、返回码、只读和运行实例转交 | BC 与 Lq 应记录兼容范围 | CLI-004..005,CLI-008..009,PLAT-006 | C/G；[E14](#e14) | [S006](#s006), [S049](#s049) |
### 脚本

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p85"></a>P85 | 加载、比较准则、过滤、选择、展开；拷贝/移动/删除/重命名/触时/同步 | BC Standard 脚本 | SCR-001..003,SCR-006 | C/G；[E14](#e14) | [S048](#s048) |
| <a id="p86"></a>P86 | 参数、日志、报告/快照、外部调度；录制与控制流不能假定已对齐 BC | BC Standard；部分 Lq 扩展 | SCR-004..010 | C/G；[E14](#e14) | [S049](#s049), [S048](#s048) |
### 设置与 UI

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p87"></a>P87 | 菜单/工具栏显示、快捷键冲突、自定义命令、工具提示与键盘导航 | BC Standard；TGM 可切经典工具栏 | UI-003..004,UI-023..027,UI-035,OPT-001,OPT-011 | C/U；[E17](#e17) | [S037](#s037), [S039](#s039), [S089](#s089), [S097](#s097) |
| <a id="p88"></a>P88 | 主题/字体/差异色、窗口/多显示器 DPI、Tab/状态栏、全局/会话默认 | BC 与 TGM；具体 Lq Ribbon 结构是自定 | UI-001..002,UI-005..022,UI-028..034,OPT-002..008,OPT-014 | C/U；[E17](#e17) | [S083](#s083), [S037](#s037), [S097](#s097) |
| <a id="p89"></a>P89 | 设置导入导出/便携模式/迁移；管理员禁止更新/远程/持久密码策略 | BC Standard；BC 便携模式 Windows | OPT-009..010,OPT-012..013 | C/G；[E15](#e15), [E17](#e17) | [S084](#s084), [S001](#s001) |
### 平台交付

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p90"></a>P90 | 跨 OS 构建/路径/符号链接/系统菜单/打包；每平台真实应用验收 | BC Windows/macOS/Linux；TG Windows | PLAT-001..010,ENG-001..013 | U；[E18](#e18) | [S003](#s003), [S083](#s083), [S091](#s091) |
### 规格与帮助

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p91"></a>P91 | 用户手册、CLI 参考、快捷键、变更日志、可追溯对标与规格发布校验 | Lq 交付工程，不是单一竞品按钮 | DOC-001..006,ENG-014..015 | X/U；[E18](#e18) | [S006](#s006), [S092](#s092) |
### 自有扩展

| 编号 | 行为覆盖 | 竞品归属 | LqCompare 规格 | 状态/证据 | 官方来源 |
| --- | --- | --- | --- | --- | --- |
| <a id="p92"></a>P92 | PE/ELF/PNG 结构解释、语义版本排序、归档冗余分析、报告模板 | 现有 Lq 规格；本轮未证实竞品同等功能 | HEX-006,VER-003,ARC-005,RPT-011 | X/U；[E18](#e18) | [S074](#s074), [S076](#s076), [S002](#s002), [S023](#s023) |

## 4 当前源码证据

以下仅是本次源码证据，不是测试通过记录。测试目录列为下一轮应运行的入口；目录存在本身不能证明某项已验收。路径均相对仓库根目录。

| 证据 | 已核对的事实和边界 | 代码入口 | 相关测试入口 |
| --- | --- | --- | --- |
| <a id="e01"></a>E01 | 会话注册/文档/作用域/容器已有实现；不据此确认所有注册类型均能完整打开、编辑、持久化、恢复 | `Code/Services/Session/sessiontype.cpp`、`sessiondocument.cpp`、`settingscope.cpp`；`Code/Views/Shell/sessionarea.cpp` | SessionType、SessionDocument、SettingsScope、HomeRegistry、SessionArea |
| <a id="e02"></a>E02 | `Alignment` 仅 Myers/Patience；相似行阈值与忽略规则有数据模型；文本视图两处固定 NoWrap。手动多行对齐/列选择/完整搜索等未逐项核验 | `Code/Services/Text/textdiff.h`；`Code/Views/Text/textcompareview.{h,cpp}`，`TextPane`/编辑器 `setLineWrapMode(NoWrap)` | Text、Alignment、Similarity、TextRules、TextView |
| <a id="e03"></a>E03 | Document 提供编码/BOM/EOL、外部变化检测、保存/另存与编辑限制；文本硬限制为 32 MiB/500000 行。BOM 注释把 UTF16/32 无 BOM 说成“不可能”，与 TGM 显式无 BOM 选项存在语义差异，需专项核验 | `Code/Services/Text/textdocument.h`、`textdocument.cpp`、`compareconclusion.cpp`；`Code/Views/Text/textcomparesession.cpp` | Text、CompareConclusion、TextView |
| <a id="e04"></a>E04 | Merge 区分左右单改/同改/冲突，支持两种块顺序和独立输出；引擎按精确文本/EOL、严格重叠判断。未见 BC 相邻 N 行冲突范围参数，不等于与 BC 默认完全一致 | `Code/Services/Merge/mergeengine.{h,cpp}`、`mergeoutput.cpp`；`Code/Views/Merge/textmergesession.cpp` | Merge、MergeOutput、MergeView |
| <a id="e05"></a>E05 | 本地扫描/状态/视图/复制等入口已有代码。2026-10-02 02:39 UTC 于 `641bbd5` 复核：目录内容比较仅有 `compareContent` 开关及 `compareFirstBytes` 预算，`compareFile()` 执行大小检查和逐字节读取；没有 CRC 或格式规则比较的模式选择与执行分支。`ContentEvidence` 的 CRC/Rule 枚举及状态表只定义结果语义，不证明算法已接入；DIR-007/009 为明确执行缺口，DIR-008 仅确认相关代码存在。未逐项验收 BC 全部元数据、刷新、属性写回、对齐、网络挂载和长路径组合；未据本次复核增加运行通过声明 | `Code/Services/Folder/foldercompare.h:133-154`、`foldercompare.cpp::compareFile()`；`entrystatus.cpp`；`Code/Services/Files/`；`Code/Views/Folder/` | Folder、EntryStatus、FileSystem、Batch、Trash、PathName |
| <a id="e06"></a>E06 | 同步计划、预览和执行有代码；符号链接/特殊文件/类型或名称冲突进入手工处理，不宣称对所有文件类型自动同步 | `Code/Services/Sync/syncengine.cpp`（`unsupported-type`）；`Code/Views/Sync/syncpreviewdialog.cpp` | Sync、SyncView、SyncBaseline |
| <a id="e07"></a>E07 | `Plan::canExecute()` 固定 false；禁用理由明示只有只读合并计划，事务写入/备份/恢复未实现 | `Code/Services/FolderMerge/foldermergeplan.h:44`、`.cpp` 的 `executionDisabledReason()`；`Code/Views/FolderMerge/` | FolderMerge、FolderMergeView |
| <a id="e08"></a>E08 | 表格支持文本解析、列角色/复合键/内容与位置对齐、数值容差；加载路径明确只接受 CSV/TSV。`canSaveNow()` 固定 false；没有多工作表数据模型 | `Code/Services/Table/tabledocument.{h,cpp}`、`tablecompare.{h,cpp}`；`Code/Views/Table/tablecomparesession.h:43` | TableParser、TableCompare、TableView |
| <a id="e09"></a>E09 | Hex 有按偏移读取/差异/搜索和视图；没有字节编辑保存，`canSaveNow()` 固定 false；当前比较按偏移，未形成 Complete/Fast 插入删除对齐三模式 | `Code/Services/Special/hexdiff.{h,cpp}`、`hexsearch.cpp`；`Code/Views/Special/hexcomparesession.h:57` | SpecialHex、SpecialHexSearch |
| <a id="e10"></a>E10 | 图片像素容差/alpha/区域分析及视图有代码；保存禁用。未逐项验证自动缩放、旋转、偏移、颜色替换、XOR、多帧/EXIF 与平台 codec 矩阵 | `Code/Services/Special/picturediff.{h,cpp}`；`Code/Views/Special/picturecompareview.cpp`、`picturecomparesession.h` | SpecialPicture |
| <a id="e11"></a>E11 | Media 明示 MP3/FLAC 可读、MP4/AAC 等未实现。Registry provider 为只读 API，保存禁用。PE 检查器有跨平台解析代码；这不说明 Windows live registry 已经实测 | `Code/Services/Media/mediametadata.cpp:452`；`Code/Views/Registry/registrycomparesession.h`；`Code/Services/Version/versioninfo.cpp` | Media、MediaView、Registry、RegistryView、Version、VersionCompare、VersionView |
| <a id="e12"></a>E12 | 过滤器与格式检测/存储实现已存在；`FormatDefinition::settings` 中语法、转换等是 opaque data；当前格式数据存在不能证明语法引擎/外部转换执行已接入 | `Code/Services/Filter/`；`Code/Services/Format/formatdefinition.h:27`、`formatdetector.cpp` | Filter、NameFilter、AttributeFilter、ContentFilter、FilterStack、FilterView、Format |
| <a id="e13"></a>E13 | Archive 明示只读 ZIP/JAR 元数据，不解压、不提取、不校验 payload CRC；ZIP64/加密有错误类型。Snapshot 是只读清单与可选 SHA256，不含恢复内容能力 | `Code/Services/Archive/archivecompare.h:25-26,62-65`、`.cpp`；`Code/Services/Snapshot/snapshot.{h,cpp}` | Archive、ArchiveView、Snapshot |
| <a id="e14"></a>E14 | headless 类型仅 text/folder；脚本命令为 load/compare/report/set/print/log，copy/move/delete/sync/control flow 等明示未实现；通用报告 Kind 只有 Text/Folder；补丁生成/解析/预览/安全应用有独立实现 | `Code/Services/Cli/cliexecution.cpp:79`；`Code/Services/Script/scriptengine.cpp:363`；`Code/Services/Report/report.h:20`；`Code/Services/Patch/patch.{h,cpp}`、`patchapply.cpp` | Cli、CliProbe、Script、Report、PatchApply、PatchRegression |
| <a id="e15"></a>E15 | 选项 UI 明示网络/远程连接/凭据管理未实现；PRD 明确远程是后续阶段。OPT-009 是设置项，不能覆盖全套数据源后端规格 | `Code/Views/Options/optionsdialog.cpp:331`；`docs/PRD.md` §3.2；`tools/spec/f_system.py` | OptionsDialog；尚需远程传输/凭据/故障注入测试 |
| <a id="e16"></a>E16 | Git 后端提供 status/log/diff/catFile/references/blame，源区分 WorkingTree/Index/Revision/Empty；历史内容用临时只读快照。没据此确认 Git 客户端全部工作流 | `Code/Services/Vcs/vcsbackend.{h,cpp}`；`Code/Views/Vcs/vcsview.cpp`、`blameview.cpp` | Vcs、VcsView、VcsBlameView |
| <a id="e17"></a>E17 | 命令注册/统一状态、快捷键、选项 UI 存在；未实现入口会给原因。选项明确若干颜色/迁移/启动行为未接入；多页 Ribbon 本身不构成需求完成 | `Code/Services/Command/commandregistry.cpp`；`Code/Views/Page/commandactionbinder.cpp`；`Code/Views/Options/optionsdialog.cpp:234-241,331` | CommandRegistry、CommandActions、CommandShortcuts、Options、OptionsDialog、AppIntegration |
| <a id="e18"></a>E18 | 369 条规格真源与 73 测试工程存在。本研究不附任何未执行的“全绿”结论；应采用同一最终 SHA 的 Windows/Linux/macOS 测试与 GUI 证据 | `tools/spec/__init__.py::load_actions()`；`Code/Tests/**/*.pro`；`.github/workflows/` | `tools/check_spec.py` 及正式测试运行器 |

## 5 可立即进入实现队列的缺口

优先级是本次建议，不改写现有 GitHub issue 的优先级或状态。所有新行为都应有服务测试、真实视图入口、异常路径和保存后的字节/目录结果校验，不能仅补按钮。

| 缺口 | 状态与对应规格 | 下一次最小完整交付 | 验收重点 |
| --- | --- | --- | --- |
| GAP-01 | G，FMG-005/009，E07 | 把只读目录合并计划接到安全写回，先覆盖普通文件/目录；不能支持的类型继续拒绝 | 输出不得覆盖输入；源在预览后变更、权限失败、中途取消、部分失败、备份恢复 |
| GAP-02 | G，ARC-001/002/004、DIR-033，E13 | 第一阶段 ZIP payload 解码与成员比对；随后分格式开放写回和其他归档 | 同 CRC/不同 payload、CRC 错误、ZIP64/加密、嵌套炸弹、符号链接逃逸、路径大小写冲突 |
| GAP-03 | G，DATA-007，E08 | Excel/HTML 读取与工作表模型、映射/导航；“支持 Excel”按 xls/xlsx 等具体格式分验收 | 多表同名/重名/缺表、单文件两表、空表、公式值/格式边界、转换失败 |
| GAP-04 | G，DATA-009/010，E08 | 文本表格可编辑、行/列/单元格复制、撤销重做、原格式保存 | 引号/多行字段/尾空格/EOL/编码往返；取消不得写盘；不可逆转换显式只读 |
| GAP-05 | G，HEX-004/007，E09 | 字节插删/覆盖、双向复制、Undo/Redo 与原子保存 | 插入造成偏移、空文件、>2 GiB 地址解析与实际文件预算、源在外部被修改 |
| GAP-06 | G，HEX-008，E09 | 将按偏移与插删对齐模式明确区分，提供复杂/快速策略与可见预算退化 | 首部插字节不能把余下全部误作修改；内存/取消边界；稀疏文件 |
| GAP-07 | G，MED-001，E11 | MP4/AAC 元数据读取接入，与 MP3/FLAC 相同的完整性状态 | 损坏/超长 atom、封面图预算、未知字段、部分读取不能报完整相同 |
| GAP-08 | G/S，OPT-009，E15 | 新增数据源能力接口与 provider 规格，再按 FTP→SFTP/FTPS→WebDAV/S3/云盘阶段实现 | 身份验证与主机校验、限权、只读、分页、断线、取消、重试幂等、无凭据日志 |
| GAP-09 | G，SCR-002/003，E14 | 明确选择集/过滤语义后接入文件操作与同步；先能预演，再允许显式执行 | 脚本全量校验、路径引用、危险输出覆盖输入、失败返回码、部分执行审计 |
| GAP-10 | G，CLI-002/004/008、MRG-016，E14 | 为已具备引擎的类型逐个增加 headless 执行，区分“不支持”和“有差异” | 每种类型相同/不同/错误/取消矩阵；stdout/stderr 分离；引号和 Unicode |
| GAP-11 | C/G，FMT-003/004，E12 | 格式识别数据接到真实语法/转换执行；先一个完整格式闭环 | 转换器非零/空输出/超时、双向可逆性、源别名保护、格式层/会话层优先级 |
| GAP-12 | G，TXT-034，E02/E03 | 文本真实换行显示及超长行策略；保留源行号与字符坐标 | 中文/emoji/组合字符/Tab/NUL、最后字符选择、改变窗宽、编辑后导航不跳错 |
| GAP-13 | C/G，TXT-004、MRG-004，E02/E04 | 公布支持的对齐模式；合并可配置邻近改动是否需人工复核 | 不把特定算法名字当唯一验收；用固定语料核对结果、冲突范围和性能退化 |
| GAP-14 | C/G，TXT-014/015，E03 | 重新检查无 BOM UTF16/32 的显式编码读写契约与 UI 提示 | 显式字节序应能表意；盲猜编码可拒绝；用字节金样验证，而非只测显示文字 |
| GAP-15 | G，REG-001/003/005，E11 | 先补 Windows live read 实机覆盖及远程只读；写回独立阶段 | HKCU/限权、32/64 位视图、备份、预演、取消；平台能力不可伪装 |
| GAP-16 | C/G，RPT-001/004/010、专用报告 ID，E14 | 专用视图适配通用报告，补打印/分页与相同数据的 CLI 输出 | HTML escaping、长字段、多表汇总、图片资源、DPI、空选择、取消与原子写 |
| GAP-17 | C/U，TXT-012/013、SESS-007，E01/E02 | 专项核对 BC 左右方向替换与父会话子文件作用域；不足则扩规格 | a→b 与 b→a 是否对称须明确；只忽略这对文件不能泄漏到另一个会话 |
| GAP-18 | S/U，OPT-012/013、ENG-013，E15/E17 | 增加管理员策略与配置迁移验收，明确后续版本是否支持 | 禁止远程/存密码/更新时 UI、CLI、脚本均执行同一策略；旧配置安全降级 |

### 当前规格不足以表达的后续子任务

以下是建议拆分名称，不是已经创建的 GitHub issue，也没有擅自分配正式 ACTION-ID：

- `Remote provider contract`：枚举/读取/能力标记/取消、远程路径身份与元数据、分页和缓存一致性
- `FTP transport`、`SFTP and FTPS transport`：分别验收协议，不能合在“网络设置页”下算完成
- `Cloud profiles`：S3、WebDAV、Dropbox、OneDrive 各自授权/只读/上传/分页失败用例；不假定一个 HTTP 客户端等于四种完整 provider
- `Read-only SVN source`：BC 的 Subversion 修订读取与 Windows SCC check-in/out 分开；按“所有 BC 功能”目标需要保留明确范围
- `Credential and admin policy`：系统凭据库引用、撤销/过期、导出不含秘密、按策略禁用、零日志泄露
- `BC child-session settings`：父会话中指定文件对设置与全部子视图设置，需要明确谁拥有/持久化这些配置
- `Compatibility declaration`：Lq 自有 CLI/脚本与 BC/TGM 兼容模式分表；不支持的开关应明确报错，不能静默丢弃

## 6 从修改记录提炼的回归验收

下表“记录线索”为简短摘要，具体版本内容以官方[BC 变更页](https://www.scootersoftware.com/all/v5changelog)和[TortoiseGit 发布说明](https://tortoisegit.org/docs/releasenotes/)为准。右列是针对 LqCompare 的独立测试设计，**不是声称 LqCompare 已复现同一缺陷，也不是复制竞品实现**。

| 版本线索 | 记录线索 | LqCompare 回归设计 |
| --- | --- | --- |
| BC 5.0 | wrap、多表、media | 将 TXT-034/DATA-007/MED-001 分别列成端到端样例；切表不丢筛选和选择 |
| BC 5.1.0 | 离散选行、差异表导航 | Ctrl/Shift 多选、隐藏相同列、首尾差异工作表；列表为空仍能恢复显示 |
| BC 5.2.0 | Unix 元数据、Qt6、策略 | 特殊文件不阻塞扫描；按 OS 能力标记列；不把 BC 依赖升级强套 Lq 的 Qt5 约束 |
| BC 5.2.1 | 打印/DPI/高缩放 | 100/125/150/200/250% 与跨屏移动；打开、关闭、预览、取消均测试 |
| BC 5.2.2 | patch 拖放、主题取消 | 拖放路由无隐式应用；颜色预览取消恢复原状态；不同会话不受污染 |
| BC 5.2.3 | SFTP CRC、正则/长路径 | 不信任远端宣称哈希；无效正则可恢复；已运行实例接收超长 Unicode 参数 |
| BC 5.2.4 | 未保存退出、归档更新 | 脏文档取消退出保留；关闭多个标签时只询问一次对应文档；依赖漏洞清单 |
| BC 5.2.5 | 日期单元格、大地址 | 同一时刻不同区域字符串；地址转换不截为 int；超支持大小给明确错误 |
| BC 5.2.6 | launcher/对齐/缩进 | 参数数组不经 shell 拼接；对齐拖动在 gutter 释放；反缩进后关闭和 Undo |
| TG 2.13 | BOM patch、换行/大文件 | 空文件/NUL/中文超长行/末尾 EOL；BOM 更改的补丁不能破坏编码 |
| TG 2.14 | Undo/Redo 卡死 | 最小一行文件上循环撤销重做；变更计数和 dirty 标志保持一致 |
| TG 2.15 | DPI、SVG | 图像畸形输入/小窗口；跨屏移动后工具栏、差异格、热点一致 |
| TG 2.16 | EOL 控件、Redo 循环 | 在忽略 EOL 后仍显示原字节状态；一行变更保存后继续重做不能无限循环 |
| TG 2.17 | 视图栏持久化、暗色 | 切主题/最大化/恢复/重启后布局仍完整；不得残留白闪或空 Ribbon |
| TG 2.18 | 结果路径定位 | 输出路径可复制/定位；路径为空或输出尚未保存时入口正确禁用 |
| TG 2.19.0 | 外部脚本/参数安全 | 不把历史文件名拼成命令；拒绝未授权网络来源；限制进程输出与环境变量 |
| TG 2.19.1 | base/yours 参数互换 | 以三个完全不同的金样文件验证 base/mine/theirs/output 参数，交换任意两侧必失败 |

这些用例应覆盖普通执行，也覆盖重复点击、取消、关闭、重开、外部改文件、源缺失与权限失败。历史发布中大量修复落在这些边界；只验证“打开能看到 diff”不足以证明对等。

## 7 推荐阶段和 UI 约束

1. **先守住数据与运行正确性**：所有平台编译/当前已实现入口、会话关闭与脏文档、对齐/编码/EOL、目录时间语义、操作范围。用同一最终 SHA 的日志和截图验证，不混用旧 CI 结果
2. **文本与目录的日常闭环**：紧凑常规控件、换行/导航/查找、手动对齐与显示过滤、文件操作/同步的预演和真实结果。完成一个功能时同时接入快捷键/菜单和错误提示
3. **专业编辑与格式**：目录合并写回、表格多表/编辑、Hex 编辑/插删对齐、格式转换、报告与 Patch 兼容性。先明确只读能力，再逐个开放可靠写回
4. **扩展数据源与自动化**：归档 payload/写回、远程 profile/凭据、专用 CLI/脚本、管理员策略。远程登录/外部写入不作为“一个 settings tab”处理
5. **专项平台与自有增强**：Registry/SCC、平台菜单、辅助波形/结构解析、报告模板等。继续保留 X 与真实竞品差距的区别

用户要求的界面方向是常规、紧凑控件。建议高频操作采用小图标按钮/标准菜单/少量明确下拉，差异区保留主要空间；低频高级参数进入设置。快捷键、工具提示、键盘焦点、禁用原因和无障碍不能因紧凑而省略。**“必须十页 Ribbon”“单 Tab 是反面教材”属于旧设计意见，不是官方功能事实，也不应压过当前用户方向。**

不建议为了“所有功能”一次性把待实现命令全塞入主工具条。完整性通过可检索命令目录与本矩阵核对，日常界面按任务上下文呈现；功能暂不可用应有清楚原因。

## 8 持续对标的完成定义

每项验收记录至少包含：ACTION-ID、矩阵编号/官方章节、目标版本、实现 commit、测试用例、三平台结果、GUI 入口和截图、取消/失败结果、已知不支持边界。只有规格、真实后端、UI、自动化契约和目标平台验收都符合该条的完成标准，才更新为已完成。

后续看到竞品新版本时：先记录版本/日期，再将**新功能、语义变更、回归修复、安全/依赖变化**分别映射到既有 ID；仅在现有条目无法表达时加规格。未检查的来源保留待核验，旧版限制不可直接当作新版限制。本文件不是自动监控任务，未设置定时抓取或发布操作。

## 9 全部 369 条规格的覆盖索引

这一索引证明现有规格都找到审计主题，不证明每条已经实现。所有条目本轮均未完成独立验收；明确缺口以 GAP 编号记录。题名与 issue 地址来自真源及 `docs/github/prd-issues.json`，未更改原状态。


### 界面

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [UI-001](https://github.com/LorenHan/LqCompare/issues/2) | Ribbon 主窗口外壳 | [P88](#p88) | 未逐项验收 |
| [UI-002](https://github.com/LorenHan/LqCompare/issues/1) | Ribbon 样式、最小化与分组标题 | [P88](#p88) | 未逐项验收 |
| [UI-003](https://github.com/LorenHan/LqCompare/issues/3) | 快速访问工具栏（QAT）策略 | [P87](#p87) | 未逐项验收 |
| [UI-004](https://github.com/LorenHan/LqCompare/issues/4) | 居中命令搜索栏 | [P87](#p87) | 未逐项验收 |
| [UI-005](https://github.com/LorenHan/LqCompare/issues/5) | Application Menu 后台视图 | [P88](#p88) | 未逐项验收 |
| [UI-006](https://github.com/LorenHan/LqCompare/issues/6) | Ribbon 上下文菜单抑制与页面右键菜单 | [P88](#p88) | 未逐项验收 |
| [UI-007](https://github.com/LorenHan/LqCompare/issues/7) | Ribbon 页面：Home 开始 | [P88](#p88) | 未逐项验收 |
| [UI-008](https://github.com/LorenHan/LqCompare/issues/8) | Ribbon 页面：Compare 比较 | [P88](#p88) | 未逐项验收 |
| [UI-009](https://github.com/LorenHan/LqCompare/issues/10) | Ribbon 页面：Merge 合并 | [P88](#p88) | 未逐项验收 |
| [UI-010](https://github.com/LorenHan/LqCompare/issues/9) | Ribbon 页面：Edit 编辑 | [P88](#p88) | 未逐项验收 |
| [UI-011](https://github.com/LorenHan/LqCompare/issues/11) | Ribbon 页面：View 视图 | [P88](#p88) | 未逐项验收 |
| [UI-012](https://github.com/LorenHan/LqCompare/issues/12) | Ribbon 页面：Filter 过滤 | [P88](#p88) | 未逐项验收 |
| [UI-013](https://github.com/LorenHan/LqCompare/issues/13) | Ribbon 页面：Session 会话 | [P88](#p88) | 未逐项验收 |
| [UI-014](https://github.com/LorenHan/LqCompare/issues/15) | Ribbon 页面：Report 报表 | [P88](#p88) | 未逐项验收 |
| [UI-015](https://github.com/LorenHan/LqCompare/issues/14) | Ribbon 页面：Tools 工具 | [P88](#p88) | 未逐项验收 |
| [UI-016](https://github.com/LorenHan/LqCompare/issues/16) | Ribbon 页面：Help 帮助 | [P88](#p88) | 未逐项验收 |
| [UI-017](https://github.com/LorenHan/LqCompare/issues/17) | Ribbon 页面与分组的上下文可见性 | [P88](#p88) | 未逐项验收 |
| [UI-018](https://github.com/LorenHan/LqCompare/issues/20) | Ribbon 分组收缩策略（ScalingPolicy） | [P88](#p88) | 未逐项验收 |
| [UI-019](https://github.com/LorenHan/LqCompare/issues/18) | 复合控件支持：SplitButton | [P88](#p88) | 未逐项验收 |
| [UI-020](https://github.com/LorenHan/LqCompare/issues/19) | 复合控件支持：DropDownButton 与复选列表 | [P88](#p88) | 未逐项验收 |
| [UI-021](https://github.com/LorenHan/LqCompare/issues/21) | 复合控件支持：Gallery | [P88](#p88) | 未逐项验收 |
| [UI-022](https://github.com/LorenHan/LqCompare/issues/22) | 复合控件支持：可编辑 ComboBox（路径下拉） | [P88](#p88) | 未逐项验收 |
| [UI-023](https://github.com/LorenHan/LqCompare/issues/23) | 命令 tooltip 两段式规范与强制校验 | [P87](#p87) | 未逐项验收 |
| [UI-024](https://github.com/LorenHan/LqCompare/issues/24) | 命令注册中心 | [P87](#p87) | 未逐项验收 |
| [UI-025](https://github.com/LorenHan/LqCompare/issues/25) | 图标体系与资源管理 | [P87](#p87) | 未逐项验收 |
| [UI-026](https://github.com/LorenHan/LqCompare/issues/26) | Ribbon 状态栏宿主 | [P87](#p87) | 未逐项验收 |
| [UI-027](https://github.com/LorenHan/LqCompare/issues/27) | 快捷键系统与自定义 | [P87](#p87) | 未逐项验收 |
| [UI-028](https://github.com/LorenHan/LqCompare/issues/28) | 主题与配色方案 | [P88](#p88) | 未逐项验收 |
| [UI-029](https://github.com/LorenHan/LqCompare/issues/30) | 窗口几何与布局持久化 | [P88](#p88) | 未逐项验收 |
| [UI-030](https://github.com/LorenHan/LqCompare/issues/29) | Ribbon 本地化与语言切换 | [P88](#p88) | 未逐项验收 |
| [UI-031](https://github.com/LorenHan/LqCompare/issues/33) | 主窗口标题与文档状态标记 | [P88](#p88) | 未逐项验收 |
| [UI-032](https://github.com/LorenHan/LqCompare/issues/32) | 对话框风格统一 | [P88](#p88) | 未逐项验收 |
| [UI-033](https://github.com/LorenHan/LqCompare/issues/31) | 拖放目标 | [P02](#p02), [P88](#p88) | 未逐项验收 |
| [UI-034](https://github.com/LorenHan/LqCompare/issues/34) | 高 DPI 与分数缩放适配 | [P88](#p88) | 未逐项验收 |
| [UI-035](https://github.com/LorenHan/LqCompare/issues/35) | 键盘全导航与无障碍 | [P87](#p87) | 未逐项验收 |

### 会话

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [SESS-001](https://github.com/LorenHan/LqCompare/issues/36) | 会话抽象基类 | [P01](#p01) | 未逐项验收 |
| [SESS-002](https://github.com/LorenHan/LqCompare/issues/37) | 会话类型注册表 | [P01](#p01) | 未逐项验收 |
| [SESS-003](https://github.com/LorenHan/LqCompare/issues/38) | Home 视图 | [P02](#p02) | 未逐项验收 |
| [SESS-004](https://github.com/LorenHan/LqCompare/issues/41) | 会话树与分组管理 | [P03](#p03) | 未逐项验收 |
| [SESS-005](https://github.com/LorenHan/LqCompare/issues/40) | 新建会话向导 | [P01](#p01) | 未逐项验收 |
| [SESS-006](https://github.com/LorenHan/LqCompare/issues/39) | 会话设置对话框框架 | [P04](#p04) | 未逐项验收 |
| [SESS-007](https://github.com/LorenHan/LqCompare/issues/42) | 设置作用域语义 | [P04](#p04) | GAP-17；未逐项验收 |
| [SESS-008](https://github.com/LorenHan/LqCompare/issues/43) | 会话文件保存与加载 | [P03](#p03) | 未逐项验收 |
| [SESS-009](https://github.com/LorenHan/LqCompare/issues/46) | 最近会话与最近比较列表 | [P03](#p03) | 未逐项验收 |
| [SESS-010](https://github.com/LorenHan/LqCompare/issues/44) | 会话标签页与多标签管理 | [P05](#p05) | 未逐项验收 |
| [SESS-011](https://github.com/LorenHan/LqCompare/issues/45) | 工作区（Workspaces） | [P05](#p05) | 未逐项验收 |
| [SESS-012](https://github.com/LorenHan/LqCompare/issues/48) | 剪贴板比对会话 | [P06](#p06) | 未逐项验收 |
| [SESS-013](https://github.com/LorenHan/LqCompare/issues/47) | 文件对自动选视图 | [P01](#p01) | 未逐项验收 |
| [SESS-014](https://github.com/LorenHan/LqCompare/issues/49) | 用其它视图打开（Compare Using） | [P01](#p01) | 未逐项验收 |
| [SESS-015](https://github.com/LorenHan/LqCompare/issues/50) | 会话默认值管理 | [P03](#p03) | 未逐项验收 |
| [SESS-016](https://github.com/LorenHan/LqCompare/issues/51) | 会话参数从命令行导入 | [P06](#p06) | 未逐项验收 |
| [SESS-017](https://github.com/LorenHan/LqCompare/issues/53) | 会话只读与可编辑状态管理 | [P06](#p06), [P45](#p45) | 未逐项验收 |
| [SESS-018](https://github.com/LorenHan/LqCompare/issues/52) | 会话生命周期与脏状态提示 | [P07](#p07) | 未逐项验收 |
| [SESS-019](https://github.com/LorenHan/LqCompare/issues/55) | 会话统计与汇总 | [P07](#p07), [P31](#p31) | 未逐项验收 |
| [SESS-020](https://github.com/LorenHan/LqCompare/issues/54) | 会话级错误处理与部分失败恢复 | [P07](#p07) | 未逐项验收 |

### 文本比对

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [TXT-001](https://github.com/LorenHan/LqCompare/issues/56) | 文本比对会话与双窗格视图骨架 | [P15](#p15) | 未逐项验收 |
| [TXT-002](https://github.com/LorenHan/LqCompare/issues/57) | 基础行对齐算法（Myers） | [P08](#p08) | 未逐项验收 |
| [TXT-003](https://github.com/LorenHan/LqCompare/issues/58) | 耐心对齐（Patience Diff） | [P08](#p08) | 未逐项验收 |
| [TXT-004](https://github.com/LorenHan/LqCompare/issues/59) | 对齐样式选择与持久化 | [P08](#p08) | GAP-13；未逐项验收 |
| [TXT-005](https://github.com/LorenHan/LqCompare/issues/60) | 相似度阈值与相似行对齐 | [P08](#p08) | 未逐项验收 |
| [TXT-006](https://github.com/LorenHan/LqCompare/issues/62) | 手动对齐与断开对齐 | [P09](#p09) | 未逐项验收 |
| [TXT-007](https://github.com/LorenHan/LqCompare/issues/61) | 对齐覆盖（Alignment Override） | [P09](#p09) | 未逐项验收 |
| [TXT-008](https://github.com/LorenHan/LqCompare/issues/63) | 忽略大小写差异 | [P10](#p10) | 未逐项验收 |
| [TXT-009](https://github.com/LorenHan/LqCompare/issues/64) | 忽略空白变化 | [P10](#p10) | 未逐项验收 |
| [TXT-010](https://github.com/LorenHan/LqCompare/issues/65) | 忽略行尾（EOL）差异 | [P10](#p10) | 未逐项验收 |
| [TXT-011](https://github.com/LorenHan/LqCompare/issues/66) | 忽略注释差异 | [P11](#p11) | 未逐项验收 |
| [TXT-012](https://github.com/LorenHan/LqCompare/issues/69) | 忽略行号、行内数字与日期等待定模式 | [P12](#p12) | GAP-17；未逐项验收 |
| [TXT-013](https://github.com/LorenHan/LqCompare/issues/67) | 行重要性规则（Importance） | [P10](#p10), [P11](#p11), [P12](#p12), [P44](#p44) | GAP-17；未逐项验收 |
| [TXT-014](https://github.com/LorenHan/LqCompare/issues/68) | 编码探测与手动指定编码 | [P13](#p13) | GAP-14；未逐项验收 |
| [TXT-015](https://github.com/LorenHan/LqCompare/issues/70) | BOM 处理策略 | [P13](#p13) | GAP-14；未逐项验收 |
| [TXT-016](https://github.com/LorenHan/LqCompare/issues/71) | 行尾规范化、显示与保存 | [P13](#p13) | 未逐项验收 |
| [TXT-017](https://github.com/LorenHan/LqCompare/issues/72) | 制表符宽度与不可见字符显示 | [P14](#p14) | 未逐项验收 |
| [TXT-018](https://github.com/LorenHan/LqCompare/issues/73) | 语法高亮 | [P11](#p11), [P44](#p44) | 未逐项验收 |
| [TXT-019](https://github.com/LorenHan/LqCompare/issues/75) | 并排双栏布局与同步滚动 | [P15](#p15) | 未逐项验收 |
| [TXT-020](https://github.com/LorenHan/LqCompare/issues/76) | 上下分栏布局 | [P15](#p15) | 未逐项验收 |
| [TXT-021](https://github.com/LorenHan/LqCompare/issues/74) | 单栏与全部内联布局 | [P16](#p16) | 未逐项验收 |
| [TXT-022](https://github.com/LorenHan/LqCompare/issues/77) | 差异导航 | [P17](#p17) | 未逐项验收 |
| [TXT-023](https://github.com/LorenHan/LqCompare/issues/78) | 概览栏与差异缩略图 | [P15](#p15) | 未逐项验收 |
| [TXT-024](https://github.com/LorenHan/LqCompare/issues/79) | 行号与当前定位指示 | [P15](#p15) | 未逐项验收 |
| [TXT-025](https://github.com/LorenHan/LqCompare/issues/80) | 行内差异高亮（字符级） | [P15](#p15), [P16](#p16) | 未逐项验收 |
| [TXT-026](https://github.com/LorenHan/LqCompare/issues/353) | 显示过滤器（差异/相同/上下文） | [P18](#p18) | 未逐项验收 |
| [TXT-027](https://github.com/LorenHan/LqCompare/issues/352) | 书签与标记 | [P18](#p18) | 未逐项验收 |
| [TXT-028](https://github.com/LorenHan/LqCompare/issues/354) | 匹配项高亮与文本搜索 | [P18](#p18), [P19](#p19) | 未逐项验收 |
| [TXT-029](https://github.com/LorenHan/LqCompare/issues/355) | 行内编辑与保存 | [P20](#p20) | 未逐项验收 |
| [TXT-030](https://github.com/LorenHan/LqCompare/issues/81) | 内容搬运（复制块与整体方向） | [P20](#p20) | 未逐项验收 |
| [TXT-031](https://github.com/LorenHan/LqCompare/issues/82) | 文本转换命令 | [P14](#p14), [P19](#p19), [P20](#p20) | 未逐项验收 |
| [TXT-032](https://github.com/LorenHan/LqCompare/issues/83) | 大文件策略与内存管理 | [P21](#p21) | 未逐项验收 |
| [TXT-033](https://github.com/LorenHan/LqCompare/issues/84) | 空白与不可见字符的语义显示 | [P14](#p14), [P16](#p16) | 未逐项验收 |
| [TXT-034](https://github.com/LorenHan/LqCompare/issues/85) | 自动换行与超长行处理 | [P21](#p21) | GAP-12；未逐项验收 |
| [TXT-035](https://github.com/LorenHan/LqCompare/issues/86) | 列选择模式与块编辑 | [P21](#p21) | 未逐项验收 |
| [TXT-036](https://github.com/LorenHan/LqCompare/issues/87) | 行列导航与跳转 | [P17](#p17) | 未逐项验收 |
| [TXT-037](https://github.com/LorenHan/LqCompare/issues/88) | 文本比对报表导出 | [P73](#p73) | 未逐项验收 |
| [TXT-038](https://github.com/LorenHan/LqCompare/issues/90) | 剪贴板与外部复制的文本格式 | [P20](#p20) | 未逐项验收 |
| [TXT-039](https://github.com/LorenHan/LqCompare/issues/89) | 文本比对的视图状态快照与恢复 | [P21](#p21) | 未逐项验收 |
| [TXT-040](https://github.com/LorenHan/LqCompare/issues/91) | 文本会话的自动化测试与固定语料 | [P21](#p21) | 未逐项验收 |

### 三方合并

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [MRG-001](https://github.com/LorenHan/LqCompare/issues/92) | 三方合并会话与多窗格布局 | [P16](#p16), [P22](#p22) | 未逐项验收 |
| [MRG-002](https://github.com/LorenHan/LqCompare/issues/93) | 共同祖先栏（Base）显示与定位 | [P09](#p09), [P22](#p22) | 未逐项验收 |
| [MRG-003](https://github.com/LorenHan/LqCompare/issues/94) | 自动合并引擎 | [P22](#p22), [P23](#p23) | 未逐项验收 |
| [MRG-004](https://github.com/LorenHan/LqCompare/issues/95) | 冲突识别与可视化标记 | [P16](#p16), [P23](#p23) | GAP-13；未逐项验收 |
| [MRG-005](https://github.com/LorenHan/LqCompare/issues/96) | 块级决策：使用左侧 / 使用右侧 | [P24](#p24) | 未逐项验收 |
| [MRG-006](https://github.com/LorenHan/LqCompare/issues/98) | 双向应用：先左后右 / 先右后左 | [P24](#p24) | 未逐项验收 |
| [MRG-007](https://github.com/LorenHan/LqCompare/issues/97) | 整文件决策 | [P24](#p24) | 未逐项验收 |
| [MRG-008](https://github.com/LorenHan/LqCompare/issues/99) | 手动编辑合并输出 | [P22](#p22), [P24](#p24) | 未逐项验收 |
| [MRG-009](https://github.com/LorenHan/LqCompare/issues/100) | 标记冲突已解决 / 撤销解决 | [P24](#p24) | 未逐项验收 |
| [MRG-010](https://github.com/LorenHan/LqCompare/issues/101) | 冲突导航 | [P25](#p25) | 未逐项验收 |
| [MRG-011](https://github.com/LorenHan/LqCompare/issues/102) | 只显示冲突 | [P25](#p25) | 未逐项验收 |
| [MRG-012](https://github.com/LorenHan/LqCompare/issues/103) | 独立显示差异（相对 Base 的左右差异） | [P22](#p22) | 未逐项验收 |
| [MRG-013](https://github.com/LorenHan/LqCompare/issues/104) | 保存合并结果与源文件语义 | [P25](#p25) | 未逐项验收 |
| [MRG-014](https://github.com/LorenHan/LqCompare/issues/105) | 合并输出重新比较与一致性校验 | [P23](#p23) | 未逐项验收 |
| [MRG-015](https://github.com/LorenHan/LqCompare/issues/106) | 合并决策的撤销、重做与决策日志 | [P24](#p24) | 未逐项验收 |
| [MRG-016](https://github.com/LorenHan/LqCompare/issues/107) | 三方合并的命令行与外部工具集成 | [P26](#p26) | GAP-10；未逐项验收 |
| [MRG-017](https://github.com/LorenHan/LqCompare/issues/108) | 合并会话的差异统计与前置校验 | [P23](#p23) | 未逐项验收 |
| [MRG-018](https://github.com/LorenHan/LqCompare/issues/109) | 合并场景的自动化测试与语料 | [P25](#p25) | 未逐项验收 |

### 文件夹比对

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [DIR-001](https://github.com/LorenHan/LqCompare/issues/110) | 文件夹比对会话与双树视图 | [P27](#p27) | 未逐项验收 |
| [DIR-002](https://github.com/LorenHan/LqCompare/issues/112) | 目录扫描引擎与并行扫描 | [P27](#p27) | 未逐项验收 |
| [DIR-003](https://github.com/LorenHan/LqCompare/issues/111) | 递归子目录策略 | [P27](#p27) | 未逐项验收 |
| [DIR-004](https://github.com/LorenHan/LqCompare/issues/113) | 快速测试：文件大小 | [P28](#p28) | 未逐项验收 |
| [DIR-005](https://github.com/LorenHan/LqCompare/issues/114) | 快速测试：修改时间 | [P28](#p28) | 未逐项验收 |
| [DIR-006](https://github.com/LorenHan/LqCompare/issues/115) | 快速测试：文件名大小写与规范化 | [P28](#p28) | 未逐项验收 |
| [DIR-007](https://github.com/LorenHan/LqCompare/issues/117) | 内容比对：CRC 校验和 | [P29](#p29) | G（E05：目录比较执行未接入）；未逐项验收 |
| [DIR-008](https://github.com/LorenHan/LqCompare/issues/116) | 内容比对：二进制逐字节 | [P29](#p29) | 未逐项验收 |
| [DIR-009](https://github.com/LorenHan/LqCompare/issues/118) | 内容比对：基于文件格式规则 | [P29](#p29) | G（E05：目录比较执行未接入）；未逐项验收 |
| [DIR-010](https://github.com/LorenHan/LqCompare/issues/119) | 比较准则的组合与优先级 | [P28](#p28), [P29](#p29) | 未逐项验收 |
| [DIR-011](https://github.com/LorenHan/LqCompare/issues/120) | 条目状态判定与语义 | [P29](#p29) | 未逐项验收 |
| [DIR-012](https://github.com/LorenHan/LqCompare/issues/122) | 状态着色与图标 | [P31](#p31) | 未逐项验收 |
| [DIR-013](https://github.com/LorenHan/LqCompare/issues/121) | 列定义与自定义列 | [P31](#p31) | 未逐项验收 |
| [DIR-014](https://github.com/LorenHan/LqCompare/issues/124) | 排序、分组与稳定顺序 | [P31](#p31) | 未逐项验收 |
| [DIR-015](https://github.com/LorenHan/LqCompare/issues/126) | 树展开、折叠与导航 | [P27](#p27), [P31](#p31) | 未逐项验收 |
| [DIR-016](https://github.com/LorenHan/LqCompare/issues/123) | 加载时的展开策略 | [P27](#p27), [P31](#p31) | 未逐项验收 |
| [DIR-017](https://github.com/LorenHan/LqCompare/issues/125) | 显示过滤器（文件夹视图） | [P32](#p32) | 未逐项验收 |
| [DIR-018](https://github.com/LorenHan/LqCompare/issues/127) | 选择模型与按状态选择 | [P32](#p32) | 未逐项验收 |
| [DIR-019](https://github.com/LorenHan/LqCompare/issues/128) | 复制到左 / 复制到右 | [P33](#p33) | 未逐项验收 |
| [DIR-020](https://github.com/LorenHan/LqCompare/issues/131) | 复制选项（时间戳 / 属性 / 权限） | [P33](#p33), [P34](#p34), [P70](#p70) | 未逐项验收 |
| [DIR-021](https://github.com/LorenHan/LqCompare/issues/129) | 移动条目 | [P33](#p33) | 未逐项验收 |
| [DIR-022](https://github.com/LorenHan/LqCompare/issues/130) | 删除条目（回收站优先） | [P33](#p33) | 未逐项验收 |
| [DIR-023](https://github.com/LorenHan/LqCompare/issues/132) | 重命名与批量重命名 | [P33](#p33) | 未逐项验收 |
| [DIR-024](https://github.com/LorenHan/LqCompare/issues/133) | 新建文件夹与文件 | [P33](#p33) | 未逐项验收 |
| [DIR-025](https://github.com/LorenHan/LqCompare/issues/134) | 镜像（单向覆盖整个目录） | [P37](#p37) | 未逐项验收 |
| [DIR-026](https://github.com/LorenHan/LqCompare/issues/136) | 文件属性与权限查看/编辑 | [P30](#p30), [P33](#p33) | 未逐项验收 |
| [DIR-027](https://github.com/LorenHan/LqCompare/issues/137) | 文件系统操作的可撤销与操作日志 | [P33](#p33), [P34](#p34) | 未逐项验收 |
| [DIR-028](https://github.com/LorenHan/LqCompare/issues/135) | 在系统中定位与打开 | [P36](#p36) | 未逐项验收 |
| [DIR-029](https://github.com/LorenHan/LqCompare/issues/138) | 双击打开与子视图路由 | [P17](#p17), [P36](#p36) | 未逐项验收 |
| [DIR-030](https://github.com/LorenHan/LqCompare/issues/139) | 外部工具与自定义命令 | [P36](#p36) | 未逐项验收 |
| [DIR-031](https://github.com/LorenHan/LqCompare/issues/142) | 文件名对齐规则（模糊配对） | [P35](#p35) | 未逐项验收 |
| [DIR-032](https://github.com/LorenHan/LqCompare/issues/140) | 名称不匹配仍比较内容（对齐覆盖） | [P35](#p35) | 未逐项验收 |
| [DIR-033](https://github.com/LorenHan/LqCompare/issues/141) | 压缩包作为文件夹参与比对 | [P64](#p64) | GAP-02；未逐项验收 |
| [DIR-034](https://github.com/LorenHan/LqCompare/issues/143) | 扫描进度、取消与增量刷新 | [P27](#p27) | 未逐项验收 |
| [DIR-035](https://github.com/LorenHan/LqCompare/issues/144) | 文件夹比对的报表导出 | [P73](#p73) | 未逐项验收 |
| [DIR-036](https://github.com/LorenHan/LqCompare/issues/145) | UNC、系统挂载网络位置与长路径支持 | [P30](#p30), [P69](#p69) | 未逐项验收 |
| [DIR-037](https://github.com/LorenHan/LqCompare/issues/146) | 符号链接、硬链接与特殊文件处理 | [P30](#p30) | 未逐项验收 |
| [DIR-038](https://github.com/LorenHan/LqCompare/issues/147) | 文件夹比对的自动化测试与语料 | [P27](#p27) | 未逐项验收 |

### 文件夹同步

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [SYNC-001](https://github.com/LorenHan/LqCompare/issues/148) | 双向同步会话与三栏预览布局 | [P37](#p37) | 未逐项验收 |
| [SYNC-002](https://github.com/LorenHan/LqCompare/issues/149) | 同步方向与同步规则 | [P37](#p37) | 未逐项验收 |
| [SYNC-003](https://github.com/LorenHan/LqCompare/issues/150) | 同步预览与操作清单交互 | [P37](#p37) | 未逐项验收 |
| [SYNC-004](https://github.com/LorenHan/LqCompare/issues/153) | 同步执行、进度与失败处理 | [P38](#p38) | 未逐项验收 |
| [SYNC-005](https://github.com/LorenHan/LqCompare/issues/151) | 同步删除策略与安全阈值 | [P38](#p38) | 未逐项验收 |
| [SYNC-006](https://github.com/LorenHan/LqCompare/issues/152) | 同步冲突处理 | [P38](#p38) | 未逐项验收 |
| [SYNC-007](https://github.com/LorenHan/LqCompare/issues/154) | 同步后的校验 | [P38](#p38) | 未逐项验收 |
| [SYNC-008](https://github.com/LorenHan/LqCompare/issues/155) | 同步日志与报告 | [P38](#p38) | 未逐项验收 |
| [SYNC-009](https://github.com/LorenHan/LqCompare/issues/156) | 同步会话预设与批量执行 | [P37](#p37) | 未逐项验收 |
| [SYNC-010](https://github.com/LorenHan/LqCompare/issues/157) | 同步的过滤与范围限定 | [P37](#p37) | 未逐项验收 |
| [SYNC-011](https://github.com/LorenHan/LqCompare/issues/159) | 同步的基线快照 | [P68](#p68) | 未逐项验收 |
| [SYNC-012](https://github.com/LorenHan/LqCompare/issues/357) | 同步会话的自动化测试 | [P38](#p38) | 未逐项验收 |

### 文件夹合并

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [FMG-001](https://github.com/LorenHan/LqCompare/issues/158) | 文件夹合并会话与三路布局 | [P39](#p39) | 未逐项验收 |
| [FMG-002](https://github.com/LorenHan/LqCompare/issues/160) | 逐条目合并决策 | [P39](#p39) | 未逐项验收 |
| [FMG-003](https://github.com/LorenHan/LqCompare/issues/356) | 目录级决策传播 | [P39](#p39) | 未逐项验收 |
| [FMG-004](https://github.com/LorenHan/LqCompare/issues/358) | 冲突文件夹与冲突标识 | [P39](#p39) | 未逐项验收 |
| [FMG-005](https://github.com/LorenHan/LqCompare/issues/359) | 合并输出的写回策略与应用 | [P40](#p40) | GAP-01；未逐项验收 |
| [FMG-006](https://github.com/LorenHan/LqCompare/issues/161) | 文件夹合并预览与模拟执行 | [P40](#p40) | 未逐项验收 |
| [FMG-007](https://github.com/LorenHan/LqCompare/issues/163) | 文件夹合并的批量决策与筛选联动 | [P39](#p39) | 未逐项验收 |
| [FMG-008](https://github.com/LorenHan/LqCompare/issues/162) | 文件夹合并报表与审计 | [P40](#p40) | 未逐项验收 |
| [FMG-009](https://github.com/LorenHan/LqCompare/issues/164) | 文件夹合并的撤销与应用前检查 | [P40](#p40) | GAP-01；未逐项验收 |
| [FMG-010](https://github.com/LorenHan/LqCompare/issues/166) | 文件夹合并的自动化测试 | [P40](#p40) | 未逐项验收 |

### 十六进制

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [HEX-001](https://github.com/LorenHan/LqCompare/issues/165) | 十六进制比对会话与双窗格字节视图 | [P52](#p52) | 未逐项验收 |
| [HEX-002](https://github.com/LorenHan/LqCompare/issues/167) | 字节地址导航与跳转 | [P52](#p52), [P54](#p54) | 未逐项验收 |
| [HEX-003](https://github.com/LorenHan/LqCompare/issues/168) | 差异导航与差异统计 | [P52](#p52) | 未逐项验收 |
| [HEX-004](https://github.com/LorenHan/LqCompare/issues/171) | 字节编辑与保存 | [P54](#p54) | GAP-05；未逐项验收 |
| [HEX-005](https://github.com/LorenHan/LqCompare/issues/170) | 十六进制视图的查找（模式与通配） | [P54](#p54) | 未逐项验收 |
| [HEX-006](https://github.com/LorenHan/LqCompare/issues/169) | 结构化解释面板（PE/ELF/PNG 等） | [P92](#p92) | 未逐项验收 |
| [HEX-007](https://github.com/LorenHan/LqCompare/issues/172) | 十六进制比对的内容搬运与导出 | [P54](#p54) | GAP-05；未逐项验收 |
| [HEX-008](https://github.com/LorenHan/LqCompare/issues/173) | 字节对齐与块大小对齐模式 | [P53](#p53) | GAP-06；未逐项验收 |
| [HEX-009](https://github.com/LorenHan/LqCompare/issues/174) | 十六进制视图的着色与显示选项 | [P52](#p52) | 未逐项验收 |
| [HEX-010](https://github.com/LorenHan/LqCompare/issues/175) | 十六进制比对的测试语料 | [P52](#p52) | 未逐项验收 |

### 表格比对

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [DATA-001](https://github.com/LorenHan/LqCompare/issues/178) | 表格比对会话与网格视图 | [P46](#p46) | 未逐项验收 |
| [DATA-002](https://github.com/LorenHan/LqCompare/issues/176) | 行对齐（按内容而非行号） | [P48](#p48) | 未逐项验收 |
| [DATA-003](https://github.com/LorenHan/LqCompare/issues/177) | 列映射与「比较列」定义 | [P48](#p48) | 未逐项验收 |
| [DATA-004](https://github.com/LorenHan/LqCompare/issues/179) | 单元格级差异与行级状态 | [P49](#p49) | 未逐项验收 |
| [DATA-005](https://github.com/LorenHan/LqCompare/issues/180) | 数值列的比较语义 | [P49](#p49) | 未逐项验收 |
| [DATA-006](https://github.com/LorenHan/LqCompare/issues/183) | 分隔符、编码与表头处理 | [P46](#p46) | 未逐项验收 |
| [DATA-007](https://github.com/LorenHan/LqCompare/issues/181) | 多工作表与多表格处理 | [P47](#p47) | GAP-03；未逐项验收 |
| [DATA-008](https://github.com/LorenHan/LqCompare/issues/182) | 表格视图的列宽、冻结与显示选项 | [P50](#p50) | 未逐项验收 |
| [DATA-009](https://github.com/LorenHan/LqCompare/issues/184) | 表格数据的复制与导出 | [P51](#p51) | GAP-04；未逐项验收 |
| [DATA-010](https://github.com/LorenHan/LqCompare/issues/185) | 表格的编辑与保存 | [P51](#p51) | GAP-04；未逐项验收 |
| [DATA-011](https://github.com/LorenHan/LqCompare/issues/186) | 表格比对的报表 | [P51](#p51), [P73](#p73) | GAP-16；未逐项验收 |
| [DATA-012](https://github.com/LorenHan/LqCompare/issues/187) | 表格比对的测试语料 | [P50](#p50) | 未逐项验收 |

### 图片比对

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [IMG-001](https://github.com/LorenHan/LqCompare/issues/188) | 图片比对会话与并排视图 | [P55](#p55) | 未逐项验收 |
| [IMG-002](https://github.com/LorenHan/LqCompare/issues/189) | 差异可视化方式 | [P56](#p56) | 未逐项验收 |
| [IMG-003](https://github.com/LorenHan/LqCompare/issues/190) | 像素级容差与颜色容差 | [P56](#p56) | 未逐项验收 |
| [IMG-004](https://github.com/LorenHan/LqCompare/issues/191) | 缩放、平移与对齐工具 | [P57](#p57) | 未逐项验收 |
| [IMG-005](https://github.com/LorenHan/LqCompare/issues/192) | 差异导航与差异列表 | [P57](#p57) | 未逐项验收 |
| [IMG-006](https://github.com/LorenHan/LqCompare/issues/193) | 图片元数据与 EXIF 比对 | [P57](#p57) | 未逐项验收 |
| [IMG-007](https://github.com/LorenHan/LqCompare/issues/194) | 图片比对的编辑与保存 | [P58](#p58) | 未逐项验收 |
| [IMG-008](https://github.com/LorenHan/LqCompare/issues/195) | 多帧与动画图片处理 | [P55](#p55) | 未逐项验收 |
| [IMG-009](https://github.com/LorenHan/LqCompare/issues/196) | 图片比对报表导出 | [P58](#p58), [P73](#p73) | GAP-16；未逐项验收 |
| [IMG-010](https://github.com/LorenHan/LqCompare/issues/197) | 图片比对的测试语料 | [P55](#p55) | 未逐项验收 |

### 媒体比对

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [MED-001](https://github.com/LorenHan/LqCompare/issues/198) | 媒体比对会话与标签视图 | [P59](#p59) | GAP-07；未逐项验收 |
| [MED-002](https://github.com/LorenHan/LqCompare/issues/202) | 标签字段的编辑与保存 | [P60](#p60) | 未逐项验收 |
| [MED-003](https://github.com/LorenHan/LqCompare/issues/199) | 封面图与附加资源比对 | [P59](#p59) | 未逐项验收 |
| [MED-004](https://github.com/LorenHan/LqCompare/issues/201) | 波形与频谱可视化（辅助） | [P60](#p60) | 未逐项验收 |
| [MED-005](https://github.com/LorenHan/LqCompare/issues/200) | 媒体比对报表与批量场景 | [P59](#p59), [P73](#p73) | GAP-16；未逐项验收 |
| [MED-006](https://github.com/LorenHan/LqCompare/issues/203) | 媒体比对的测试语料 | [P59](#p59) | 未逐项验收 |

### 注册表

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [REG-001](https://github.com/LorenHan/LqCompare/issues/204) | 注册表比对会话与树视图 | [P61](#p61) | GAP-15；未逐项验收 |
| [REG-002](https://github.com/LorenHan/LqCompare/issues/205) | 键值差异判定与呈现 | [P61](#p61) | 未逐项验收 |
| [REG-003](https://github.com/LorenHan/LqCompare/issues/206) | 注册表内容的复制与导出 | [P62](#p62) | GAP-15；未逐项验收 |
| [REG-004](https://github.com/LorenHan/LqCompare/issues/208) | 注册表比对报表 | [P62](#p62), [P73](#p73) | GAP-16；未逐项验收 |
| [REG-005](https://github.com/LorenHan/LqCompare/issues/209) | 注册表比对的权限与安全边界 | [P61](#p61) | GAP-15；未逐项验收 |
| [REG-006](https://github.com/LorenHan/LqCompare/issues/207) | 注册表比对的测试 | [P61](#p61) | 未逐项验收 |

### 版本比对

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [VER-001](https://github.com/LorenHan/LqCompare/issues/210) | 可执行文件版本资源比对 | [P63](#p63) | 未逐项验收 |
| [VER-002](https://github.com/LorenHan/LqCompare/issues/211) | PE 头部与依赖信息比对 | [P63](#p63) | 未逐项验收 |
| [VER-003](https://github.com/LorenHan/LqCompare/issues/212) | 版本号语义化比较 | [P92](#p92) | 未逐项验收 |
| [VER-004](https://github.com/LorenHan/LqCompare/issues/213) | 批量版本比对与清单导出 | [P63](#p63) | 未逐项验收 |
| [VER-005](https://github.com/LorenHan/LqCompare/issues/214) | 版本比对的测试 | [P63](#p63) | 未逐项验收 |

### 压缩包

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [ARC-001](https://github.com/LorenHan/LqCompare/issues/216) | 压缩包读取与条目枚举 | [P64](#p64) | GAP-02；未逐项验收 |
| [ARC-002](https://github.com/LorenHan/LqCompare/issues/215) | 归档内文件比对 | [P65](#p65) | GAP-02；未逐项验收 |
| [ARC-003](https://github.com/LorenHan/LqCompare/issues/217) | 归档结构差异比对 | [P64](#p64) | 未逐项验收 |
| [ARC-004](https://github.com/LorenHan/LqCompare/issues/218) | 归档的写入与修改 | [P65](#p65) | GAP-02；未逐项验收 |
| [ARC-005](https://github.com/LorenHan/LqCompare/issues/219) | 归档专项比对：压缩率与冗余分析 | [P92](#p92) | 未逐项验收 |
| [ARC-006](https://github.com/LorenHan/LqCompare/issues/220) | 归档路径安全与兼容性 | [P66](#p66) | 未逐项验收 |
| [ARC-007](https://github.com/LorenHan/LqCompare/issues/221) | 归档比对的命令行与自动化 | [P65](#p65) | 未逐项验收 |
| [ARC-008](https://github.com/LorenHan/LqCompare/issues/223) | 归档比对的测试与安全语料 | [P66](#p66) | 未逐项验收 |

### 编辑视图

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [EDV-001](https://github.com/LorenHan/LqCompare/issues/222) | 单窗格文本编辑视图 | [P77](#p77) | 未逐项验收 |
| [EDV-002](https://github.com/LorenHan/LqCompare/issues/224) | 补丁文件查看（Text Patch） | [P75](#p75), [P77](#p77) | 未逐项验收 |
| [EDV-003](https://github.com/LorenHan/LqCompare/issues/225) | 应用补丁 | [P76](#p76) | 未逐项验收 |
| [EDV-004](https://github.com/LorenHan/LqCompare/issues/226) | 生成补丁文件 | [P75](#p75) | 未逐项验收 |
| [EDV-005](https://github.com/LorenHan/LqCompare/issues/227) | 编辑视图与补丁视图的测试 | [P76](#p76) | 未逐项验收 |

### 过滤规则

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [FILT-001](https://github.com/LorenHan/LqCompare/issues/228) | 文件掩码语法与解析器 | [P41](#p41) | 未逐项验收 |
| [FILT-002](https://github.com/LorenHan/LqCompare/issues/229) | 名称过滤器（正则与实体名） | [P41](#p41) | 未逐项验收 |
| [FILT-003](https://github.com/LorenHan/LqCompare/issues/230) | 属性过滤（大小 / 时间 / 属性位） | [P42](#p42) | 未逐项验收 |
| [FILT-004](https://github.com/LorenHan/LqCompare/issues/231) | 内容过滤器（行过滤与关键字节） | [P42](#p42) | 未逐项验收 |
| [FILT-005](https://github.com/LorenHan/LqCompare/issues/233) | 过滤器的层级与作用域 | [P32](#p32) | 未逐项验收 |
| [FILT-006](https://github.com/LorenHan/LqCompare/issues/232) | 过滤结果的可见性与批量操作安全 | [P32](#p32), [P42](#p42) | 未逐项验收 |
| [FILT-007](https://github.com/LorenHan/LqCompare/issues/234) | 掩码与过滤器预设库 | [P41](#p41) | 未逐项验收 |
| [FILT-008](https://github.com/LorenHan/LqCompare/issues/235) | 临时过滤与快速排除 | [P41](#p41) | 未逐项验收 |
| [FILT-009](https://github.com/LorenHan/LqCompare/issues/236) | 过滤的性能与索引 | [P42](#p42) | 未逐项验收 |
| [FILT-010](https://github.com/LorenHan/LqCompare/issues/237) | 子目录级别的过滤与展开排除 | [P32](#p32) | 未逐项验收 |
| [FILT-011](https://github.com/LorenHan/LqCompare/issues/238) | 掩码语法与过滤行为的文档与帮助 | [P41](#p41) | 未逐项验收 |
| [FILT-012](https://github.com/LorenHan/LqCompare/issues/239) | 过滤规则的测试语料 | [P41](#p41) | 未逐项验收 |

### 文件格式

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [FMT-001](https://github.com/LorenHan/LqCompare/issues/240) | 文件格式定义模型与存储 | [P43](#p43) | 未逐项验收 |
| [FMT-002](https://github.com/LorenHan/LqCompare/issues/360) | 格式管理界面 | [P43](#p43) | 未逐项验收 |
| [FMT-003](https://github.com/LorenHan/LqCompare/issues/361) | 文本格式：语法定义 | [P11](#p11), [P12](#p12), [P44](#p44) | GAP-11；未逐项验收 |
| [FMT-004](https://github.com/LorenHan/LqCompare/issues/363) | 文本格式：转换（比较前的文本预处理） | [P45](#p45) | GAP-11；未逐项验收 |
| [FMT-005](https://github.com/LorenHan/LqCompare/issues/362) | 数据格式与表格列定义 | [P43](#p43), [P46](#p46) | 未逐项验收 |
| [FMT-006](https://github.com/LorenHan/LqCompare/issues/241) | 图片格式定义 | [P43](#p43), [P55](#p55) | 未逐项验收 |
| [FMT-007](https://github.com/LorenHan/LqCompare/issues/242) | 归档格式定义的识别与处理 | [P43](#p43), [P64](#p64) | 未逐项验收 |
| [FMT-008](https://github.com/LorenHan/LqCompare/issues/244) | 格式关联覆盖（Association Override） | [P35](#p35), [P43](#p43) | 未逐项验收 |
| [FMT-009](https://github.com/LorenHan/LqCompare/issues/243) | 未知扩展名的兜底策略 | [P43](#p43) | 未逐项验收 |
| [FMT-010](https://github.com/LorenHan/LqCompare/issues/245) | 格式定义的内置库 | [P43](#p43) | 未逐项验收 |
| [FMT-011](https://github.com/LorenHan/LqCompare/issues/246) | 格式定义的导入导出与共享 | [P43](#p43) | 未逐项验收 |
| [FMT-012](https://github.com/LorenHan/LqCompare/issues/247) | 格式定义引擎的测试 | [P43](#p43) | 未逐项验收 |

### 报表导出

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [RPT-001](https://github.com/LorenHan/LqCompare/issues/248) | 报表引擎与布局模型 | [P73](#p73) | GAP-16；未逐项验收 |
| [RPT-002](https://github.com/LorenHan/LqCompare/issues/249) | 报表输出目标 | [P73](#p73) | 未逐项验收 |
| [RPT-003](https://github.com/LorenHan/LqCompare/issues/250) | 报表选项（包含范围与显示项） | [P73](#p73), [P74](#p74) | 未逐项验收 |
| [RPT-004](https://github.com/LorenHan/LqCompare/issues/251) | 打印与页面设置 | [P73](#p73) | GAP-16；未逐项验收 |
| [RPT-005](https://github.com/LorenHan/LqCompare/issues/252) | 统计类报表 | [P74](#p74) | 未逐项验收 |
| [RPT-006](https://github.com/LorenHan/LqCompare/issues/253) | 差异摘要报表（可读性优先） | [P74](#p74) | 未逐项验收 |
| [RPT-007](https://github.com/LorenHan/LqCompare/issues/254) | HTML 报表的交互能力 | [P74](#p74) | 未逐项验收 |
| [RPT-008](https://github.com/LorenHan/LqCompare/issues/257) | 报表的编码与本地化 | [P74](#p74) | 未逐项验收 |
| [RPT-009](https://github.com/LorenHan/LqCompare/issues/255) | 报表的性能与流式生成 | [P74](#p74) | 未逐项验收 |
| [RPT-010](https://github.com/LorenHan/LqCompare/issues/256) | 报表的自动化调用 | [P74](#p74) | GAP-16；未逐项验收 |
| [RPT-011](https://github.com/LorenHan/LqCompare/issues/258) | 报表模板与自定义 | [P92](#p92) | 未逐项验收 |
| [RPT-012](https://github.com/LorenHan/LqCompare/issues/261) | 报表模块的测试 | [P74](#p74) | 未逐项验收 |

### 补丁

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [PAT-001](https://github.com/LorenHan/LqCompare/issues/259) | 补丁生成（diff 输出） | [P75](#p75) | 未逐项验收 |
| [PAT-002](https://github.com/LorenHan/LqCompare/issues/260) | 补丁应用（视角为「应用」而非「查看」） | [P76](#p76) | 未逐项验收 |
| [PAT-003](https://github.com/LorenHan/LqCompare/issues/262) | 补丁格式兼容性 | [P75](#p75) | 未逐项验收 |
| [PAT-004](https://github.com/LorenHan/LqCompare/issues/263) | 补丁与版本控制系统集成 | [P76](#p76) | 未逐项验收 |
| [PAT-005](https://github.com/LorenHan/LqCompare/issues/264) | 补丁相关的安全与确认 | [P76](#p76) | 未逐项验收 |
| [PAT-006](https://github.com/LorenHan/LqCompare/issues/265) | 补丁模块的测试 | [P76](#p76) | 未逐项验收 |

### 快照

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [SNAP-001](https://github.com/LorenHan/LqCompare/issues/266) | 快照生成与存储 | [P67](#p67) | 未逐项验收 |
| [SNAP-002](https://github.com/LorenHan/LqCompare/issues/268) | 快照作为比较数据源 | [P67](#p67) | 未逐项验收 |
| [SNAP-003](https://github.com/LorenHan/LqCompare/issues/267) | 快照对比与差异报告 | [P67](#p67) | 未逐项验收 |
| [SNAP-004](https://github.com/LorenHan/LqCompare/issues/269) | 快照管理 | [P67](#p67) | 未逐项验收 |
| [SNAP-005](https://github.com/LorenHan/LqCompare/issues/270) | 快照驱动的同步基线（与 SYNC 联动） | [P68](#p68) | 未逐项验收 |
| [SNAP-006](https://github.com/LorenHan/LqCompare/issues/271) | 快照模块的测试 | [P67](#p67) | 未逐项验收 |

### 版本控制

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [VCS-001](https://github.com/LorenHan/LqCompare/issues/272) | 版本控制后端抽象层 | [P78](#p78), [P82](#p82) | 未逐项验收 |
| [VCS-002](https://github.com/LorenHan/LqCompare/issues/274) | 与 HEAD 比对 | [P78](#p78) | 未逐项验收 |
| [VCS-003](https://github.com/LorenHan/LqCompare/issues/273) | 比较两个修订 | [P78](#p78) | 未逐项验收 |
| [VCS-004](https://github.com/LorenHan/LqCompare/issues/276) | 比较两个分支或标签 | [P78](#p78) | 未逐项验收 |
| [VCS-005](https://github.com/LorenHan/LqCompare/issues/275) | 工作副本状态列 | [P78](#p78) | 未逐项验收 |
| [VCS-006](https://github.com/LorenHan/LqCompare/issues/277) | 提交日志对话框 | [P79](#p79) | 未逐项验收 |
| [VCS-007](https://github.com/LorenHan/LqCompare/issues/278) | 日志的图形列与引用徽标 | [P79](#p79) | 未逐项验收 |
| [VCS-008](https://github.com/LorenHan/LqCompare/issues/279) | 日志过滤与搜索 | [P79](#p79) | 未逐项验收 |
| [VCS-009](https://github.com/LorenHan/LqCompare/issues/280) | 日志中的变更文件列表 | [P78](#p78) | 未逐项验收 |
| [VCS-010](https://github.com/LorenHan/LqCompare/issues/281) | 从日志打开比对 | [P78](#p78) | 未逐项验收 |
| [VCS-011](https://github.com/LorenHan/LqCompare/issues/282) | 修订图（Revision Graph） | [P79](#p79) | 未逐项验收 |
| [VCS-012](https://github.com/LorenHan/LqCompare/issues/283) | 逐行追溯（Blame） | [P79](#p79) | 未逐项验收 |
| [VCS-013](https://github.com/LorenHan/LqCompare/issues/284) | 追溯视图的着色与交互 | [P79](#p79) | 未逐项验收 |
| [VCS-014](https://github.com/LorenHan/LqCompare/issues/285) | 提交前变更审阅与外部提交工具入口 | [P80](#p80) | 未逐项验收 |
| [VCS-015](https://github.com/LorenHan/LqCompare/issues/286) | 冲突解决工作流 | [P80](#p80) | 未逐项验收 |
| [VCS-016](https://github.com/LorenHan/LqCompare/issues/287) | 外部版本控制工具的协作 | [P80](#p80), [P82](#p82) | 未逐项验收 |
| [VCS-017](https://github.com/LorenHan/LqCompare/issues/288) | 多仓库、子模块与工作树 | [P81](#p81) | 未逐项验收 |
| [VCS-018](https://github.com/LorenHan/LqCompare/issues/289) | 版本控制集成的配置与降级 | [P71](#p71), [P80](#p80) | 未逐项验收 |
| [VCS-019](https://github.com/LorenHan/LqCompare/issues/291) | 版本控制模块的测试 | [P80](#p80) | 未逐项验收 |

### 命令行

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [CLI-001](https://github.com/LorenHan/LqCompare/issues/290) | 命令行入口与参数解析 | [P06](#p06), [P80](#p80), [P83](#p83) | 未逐项验收 |
| [CLI-002](https://github.com/LorenHan/LqCompare/issues/292) | 会话类型与视图选择开关 | [P06](#p06), [P83](#p83) | GAP-10；未逐项验收 |
| [CLI-003](https://github.com/LorenHan/LqCompare/issues/293) | 比较规则与选项的命令行覆盖 | [P83](#p83) | 未逐项验收 |
| [CLI-004](https://github.com/LorenHan/LqCompare/issues/295) | 静默模式与返回码语义 | [P26](#p26), [P84](#p84) | GAP-10；未逐项验收 |
| [CLI-005](https://github.com/LorenHan/LqCompare/issues/294) | 只读与只写侧控制 | [P06](#p06), [P26](#p26), [P84](#p84) | 未逐项验收 |
| [CLI-006](https://github.com/LorenHan/LqCompare/issues/296) | 输出重定向与日志 | [P83](#p83) | 未逐项验收 |
| [CLI-007](https://github.com/LorenHan/LqCompare/issues/297) | 命令行调用已运行实例 | [P83](#p83) | 未逐项验收 |
| [CLI-008](https://github.com/LorenHan/LqCompare/issues/299) | 命令行报表生成 | [P84](#p84) | GAP-10；未逐项验收 |
| [CLI-009](https://github.com/LorenHan/LqCompare/issues/298) | 脚本文件引用 | [P84](#p84) | 未逐项验收 |
| [CLI-010](https://github.com/LorenHan/LqCompare/issues/300) | 路径参数与通配展开 | [P83](#p83) | 未逐项验收 |
| [CLI-011](https://github.com/LorenHan/LqCompare/issues/301) | 命令行的帮助与发现能力 | [P83](#p83) | 未逐项验收 |
| [CLI-012](https://github.com/LorenHan/LqCompare/issues/303) | 命令行模块的测试 | [P83](#p83) | 未逐项验收 |

### 脚本自动化

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [SCR-001](https://github.com/LorenHan/LqCompare/issues/302) | 脚本引擎与脚本语法 | [P85](#p85) | 未逐项验收 |
| [SCR-002](https://github.com/LorenHan/LqCompare/issues/304) | 脚本命令：数据源与比较 | [P85](#p85) | GAP-09；未逐项验收 |
| [SCR-003](https://github.com/LorenHan/LqCompare/issues/305) | 脚本命令：文件操作与同步 | [P85](#p85) | GAP-09；未逐项验收 |
| [SCR-004](https://github.com/LorenHan/LqCompare/issues/306) | 脚本变量与参数 | [P86](#p86) | 未逐项验收 |
| [SCR-005](https://github.com/LorenHan/LqCompare/issues/309) | 脚本控制流与条件 | [P86](#p86) | 未逐项验收 |
| [SCR-006](https://github.com/LorenHan/LqCompare/issues/307) | 脚本错误处理与返回码 | [P85](#p85), [P86](#p86) | 未逐项验收 |
| [SCR-007](https://github.com/LorenHan/LqCompare/issues/308) | 脚本的日志与审计 | [P86](#p86) | 未逐项验收 |
| [SCR-008](https://github.com/LorenHan/LqCompare/issues/310) | 脚本调度与定时执行 | [P86](#p86) | 未逐项验收 |
| [SCR-009](https://github.com/LorenHan/LqCompare/issues/311) | 脚本的录制与生成（辅助能力） | [P86](#p86) | 未逐项验收 |
| [SCR-010](https://github.com/LorenHan/LqCompare/issues/312) | 脚本模块的测试 | [P86](#p86) | 未逐项验收 |

### 选项外观

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [OPT-001](https://github.com/LorenHan/LqCompare/issues/313) | 程序选项对话框框架 | [P87](#p87) | 未逐项验收 |
| [OPT-002](https://github.com/LorenHan/LqCompare/issues/314) | 常规选项 | [P88](#p88) | 未逐项验收 |
| [OPT-003](https://github.com/LorenHan/LqCompare/issues/315) | 比较规则的默认值选项 | [P88](#p88) | 未逐项验收 |
| [OPT-004](https://github.com/LorenHan/LqCompare/issues/316) | 显示与外观选项 | [P88](#p88) | 未逐项验收 |
| [OPT-005](https://github.com/LorenHan/LqCompare/issues/317) | 文件操作选项 | [P34](#p34), [P88](#p88) | 未逐项验收 |
| [OPT-006](https://github.com/LorenHan/LqCompare/issues/318) | 文件夹视图选项 | [P88](#p88) | 未逐项验收 |
| [OPT-007](https://github.com/LorenHan/LqCompare/issues/319) | 文本编辑与视图选项 | [P14](#p14), [P88](#p88) | 未逐项验收 |
| [OPT-008](https://github.com/LorenHan/LqCompare/issues/320) | 报表与打印选项 | [P88](#p88) | 未逐项验收 |
| [OPT-009](https://github.com/LorenHan/LqCompare/issues/364) | 网络与连接配置选项 | [P69](#p69), [P70](#p70), [P71](#p71), [P72](#p72), [P89](#p89) | GAP-08；未逐项验收 |
| [OPT-010](https://github.com/LorenHan/LqCompare/issues/365) | 日志与诊断选项 | [P89](#p89) | 未逐项验收 |
| [OPT-011](https://github.com/LorenHan/LqCompare/issues/367) | 自定义命令与快捷键管理界面 | [P87](#p87) | 未逐项验收 |
| [OPT-012](https://github.com/LorenHan/LqCompare/issues/366) | 设置存储、便携模式与重置 | [P72](#p72), [P89](#p89) | GAP-18；未逐项验收 |
| [OPT-013](https://github.com/LorenHan/LqCompare/issues/321) | 全局设置的导入与导出 | [P05](#p05), [P89](#p89) | GAP-18；未逐项验收 |
| [OPT-014](https://github.com/LorenHan/LqCompare/issues/323) | 选项模块的测试 | [P88](#p88) | 未逐项验收 |

### 平台性能

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [PLAT-001](https://github.com/LorenHan/LqCompare/issues/322) | 构建系统与多平台编译 | [P90](#p90) | 未逐项验收 |
| [PLAT-002](https://github.com/LorenHan/LqCompare/issues/325) | 文件系统服务抽象层 | [P30](#p30), [P90](#p90) | 未逐项验收 |
| [PLAT-003](https://github.com/LorenHan/LqCompare/issues/324) | 回收站与可逆删除服务 | [P33](#p33), [P90](#p90) | 未逐项验收 |
| [PLAT-004](https://github.com/LorenHan/LqCompare/issues/329) | 系统图标与文件类型识别服务 | [P90](#p90) | 未逐项验收 |
| [PLAT-005](https://github.com/LorenHan/LqCompare/issues/326) | Shell 集成（右键菜单与文件关联） | [P36](#p36), [P82](#p82), [P90](#p90) | 未逐项验收 |
| [PLAT-006](https://github.com/LorenHan/LqCompare/issues/327) | 单实例与进程间通信 | [P84](#p84), [P90](#p90) | 未逐项验收 |
| [PLAT-007](https://github.com/LorenHan/LqCompare/issues/328) | Unicode、长路径与特殊文件名 | [P30](#p30), [P90](#p90) | 未逐项验收 |
| [PLAT-008](https://github.com/LorenHan/LqCompare/issues/330) | 权限、只读与文件占用处理 | [P30](#p30), [P33](#p33), [P90](#p90) | 未逐项验收 |
| [PLAT-009](https://github.com/LorenHan/LqCompare/issues/331) | 性能基准与回归护栏 | [P90](#p90) | 未逐项验收 |
| [PLAT-010](https://github.com/LorenHan/LqCompare/issues/332) | 平台差异的测试矩阵 | [P90](#p90) | 未逐项验收 |

### 工程质量

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [ENG-001](https://github.com/LorenHan/LqCompare/issues/333) | 工程骨架与分层依赖校验 | [P90](#p90) | 未逐项验收 |
| [ENG-002](https://github.com/LorenHan/LqCompare/issues/334) | 模块化构建与依赖守卫 | [P90](#p90) | 未逐项验收 |
| [ENG-003](https://github.com/LorenHan/LqCompare/issues/335) | 测试框架与测试运行器 | [P90](#p90) | 未逐项验收 |
| [ENG-004](https://github.com/LorenHan/LqCompare/issues/336) | 持续集成流水线 | [P90](#p90) | 未逐项验收 |
| [ENG-005](https://github.com/LorenHan/LqCompare/issues/337) | 代码风格与静态分析 | [P90](#p90) | 未逐项验收 |
| [ENG-006](https://github.com/LorenHan/LqCompare/issues/338) | 日志与诊断基础设施 | [P90](#p90) | 未逐项验收 |
| [ENG-007](https://github.com/LorenHan/LqCompare/issues/339) | 崩溃捕获与最小复现机制 | [P90](#p90) | 未逐项验收 |
| [ENG-008](https://github.com/LorenHan/LqCompare/issues/341) | 国际化提取与校验流水线 | [P90](#p90) | 未逐项验收 |
| [ENG-009](https://github.com/LorenHan/LqCompare/issues/340) | 图标与资源校验流水线 | [P90](#p90) | 未逐项验收 |
| [ENG-010](https://github.com/LorenHan/LqCompare/issues/342) | 测试覆盖率与关键路径护栏 | [P90](#p90) | 未逐项验收 |
| [ENG-011](https://github.com/LorenHan/LqCompare/issues/343) | 交付打包与分发 | [P90](#p90) | 未逐项验收 |
| [ENG-012](https://github.com/LorenHan/LqCompare/issues/344) | 版本号、构建信息与更新检查 | [P90](#p90) | 未逐项验收 |
| [ENG-013](https://github.com/LorenHan/LqCompare/issues/345) | 第三方依赖与许可证合规 | [P66](#p66), [P72](#p72), [P90](#p90) | GAP-18；未逐项验收 |
| [ENG-014](https://github.com/LorenHan/LqCompare/issues/368) | 规格数据层与 issue 发布链路 | [P91](#p91) | 未逐项验收 |
| [ENG-015](https://github.com/LorenHan/LqCompare/issues/369) | Issue 模板与规格状态标签工作流 | [P91](#p91) | 未逐项验收 |

### 文档

| ACTION-ID | 真源题名 | 审计矩阵 | 已记录缺口/本轮验收状态 |
| --- | --- | --- | --- |
| [DOC-001](https://github.com/LorenHan/LqCompare/issues/346) | 用户手册 | [P91](#p91) | 未逐项验收 |
| [DOC-002](https://github.com/LorenHan/LqCompare/issues/347) | 命令行与脚本参考 | [P91](#p91) | 未逐项验收 |
| [DOC-003](https://github.com/LorenHan/LqCompare/issues/348) | 架构与开发文档 | [P91](#p91) | 未逐项验收 |
| [DOC-004](https://github.com/LorenHan/LqCompare/issues/349) | 快捷键、掩码与语法速查表 | [P91](#p91) | 未逐项验收 |
| [DOC-005](https://github.com/LorenHan/LqCompare/issues/350) | 变更日志与发布说明 | [P91](#p91) | 未逐项验收 |
| [DOC-006](https://github.com/LorenHan/LqCompare/issues/351) | 与竞品的对标差异文档 | [P91](#p91) | 未逐项验收 |

## 10 官方来源索引

所有下列来源在 2026-10-02 UTC 实际获取并校验响应；链接属于官方站点。SHA-256 是当次响应的校验摘要前 16 位，方便后续判断页面是否变化，**不是代码版本或功能测试结果**。本文只附摘要和链接，未把商业手册原文打包进仓库。

| 来源 | 官方章节 | 响应摘要 |
| --- | --- | --- |
| <a id="s001"></a>S001 | [bc-admin_policies](https://www.scootersoftware.com/v5help/admin_policies.html) | `edfbf06851c3711b` |
| <a id="s002"></a>S002 | [bc-archive_files](https://www.scootersoftware.com/v5help/archive_files.html) | `cc997ccc98994d6e` |
| <a id="s003"></a>S003 | [bc-bcshellex](https://www.scootersoftware.com/v5help/bcshellex.html) | `f04f0988179d2018` |
| <a id="s004"></a>S004 | [bc-changelog](https://www.scootersoftware.com/all/v5changelog) | `c425400a62a3e52d` |
| <a id="s005"></a>S005 | [bc-columnhandling](https://www.scootersoftware.com/v5help/dlgtablecolhandling.html) | `faaf326e4b698636` |
| <a id="s006"></a>S006 | [bc-command_line_reference](https://www.scootersoftware.com/v5help/command_line_reference.html) | `518d7ebe900cbfa3` |
| <a id="s007"></a>S007 | [bc-commandsbc](https://www.scootersoftware.com/v5help/commandsbc.html) | `a1f5de2a3e02707a` |
| <a id="s008"></a>S008 | [bc-commandsdir](https://www.scootersoftware.com/v5help/commandsdir.html) | `b2b5871e6ad09a08` |
| <a id="s009"></a>S009 | [bc-commandsdirmerge](https://www.scootersoftware.com/v5help/commandsdirmerge.html) | `a3a3a991a21cd850` |
| <a id="s010"></a>S010 | [bc-commandsdirsync](https://www.scootersoftware.com/v5help/commandsdirsync.html) | `d22f398662103378` |
| <a id="s011"></a>S011 | [bc-commandshex](https://www.scootersoftware.com/v5help/commandshex.html) | `1a884a0b634d66ef` |
| <a id="s012"></a>S012 | [bc-commandsmedia](https://www.scootersoftware.com/v5help/commandsmedia.html) | `51d77b693edf712f` |
| <a id="s013"></a>S013 | [bc-commandspix](https://www.scootersoftware.com/v5help/commandspix.html) | `8c7ed2e1654f6d9f` |
| <a id="s014"></a>S014 | [bc-commandsreg](https://www.scootersoftware.com/v5help/commandsreg.html) | `5944ce59b403bfd3` |
| <a id="s015"></a>S015 | [bc-commandstable](https://www.scootersoftware.com/v5help/commandstable.html) | `092cbbd1026d9ea3` |
| <a id="s016"></a>S016 | [bc-commandstext](https://www.scootersoftware.com/v5help/commandstext.html) | `6a63c79de7b7302d` |
| <a id="s017"></a>S017 | [bc-commandstextmerge](https://www.scootersoftware.com/v5help/commandstextmerge.html) | `1209068a0bfe807f` |
| <a id="s018"></a>S018 | [bc-commandstextpatch](https://www.scootersoftware.com/v5help/commandstextpatch.html) | `c8a49f998fca1209` |
| <a id="s019"></a>S019 | [bc-commandsver](https://www.scootersoftware.com/v5help/commandsver.html) | `48e8640514f1bc01` |
| <a id="s020"></a>S020 | [bc-dir_filtering_the_view](https://www.scootersoftware.com/v5help/dir_filtering_the_view.html) | `3aa206b7e9c64784` |
| <a id="s021"></a>S021 | [bc-dir_reconciling_differences](https://www.scootersoftware.com/v5help/dir_reconciling_differences.html) | `c74e6e9906b229f4` |
| <a id="s022"></a>S022 | [bc-dir_sync_how_to_sync](https://www.scootersoftware.com/v5help/dir_sync_how_to_sync.html) | `478bb5cb4938fe96` |
| <a id="s023"></a>S023 | [bc-dlgreport](https://www.scootersoftware.com/v5help/dlgreport.html) | `3539d982c6f67d43` |
| <a id="s024"></a>S024 | [bc-dlgsourcecontrolmanager](https://www.scootersoftware.com/v5help/dlgsourcecontrolmanager.html) | `64de40a40dc9a688` |
| <a id="s025"></a>S025 | [bc-editing_text](https://www.scootersoftware.com/v5help/editing_text.html) | `9ad7f5ac21f3824e` |
| <a id="s026"></a>S026 | [bc-file_formats](https://www.scootersoftware.com/v5help/file_formats.html) | `598e5d05d5d2a6c8` |
| <a id="s027"></a>S027 | [bc-formattableconversion](https://www.scootersoftware.com/v5help/formattableconversion.html) | `cca63c0c6f0e335a` |
| <a id="s028"></a>S028 | [bc-formattableregional](https://www.scootersoftware.com/v5help/formattableregional.html) | `d90e60ade6876b99` |
| <a id="s029"></a>S029 | [bc-formattabletype](https://www.scootersoftware.com/v5help/formattabletype.html) | `a892ecbbefc5bc83` |
| <a id="s030"></a>S030 | [bc-formattextconversion](https://www.scootersoftware.com/v5help/formattextconversion.html) | `6218a76e5ff71d2d` |
| <a id="s031"></a>S031 | [bc-formattextgrammar](https://www.scootersoftware.com/v5help/formattextgrammar.html) | `6dac6cf03cba6f92` |
| <a id="s032"></a>S032 | [bc-formattextmisc](https://www.scootersoftware.com/v5help/formattextmisc.html) | `b8d9f4ede35e4e66` |
| <a id="s033"></a>S033 | [bc-grammars](https://www.scootersoftware.com/v5help/grammars.html) | `0ae06dbde3afd037` |
| <a id="s034"></a>S034 | [bc-license](https://www.scootersoftware.com/v5help/license_agreement.html) | `8d10ee179d683c00` |
| <a id="s035"></a>S035 | [bc-managing_sessions](https://www.scootersoftware.com/v5help/managing_sessions.html) | `04929eb7dd6f59c9` |
| <a id="s036"></a>S036 | [bc-optionsbackup](https://www.scootersoftware.com/v5help/optionsbackup.html) | `d5ccdef679e4b0cc` |
| <a id="s037"></a>S037 | [bc-optionscommand](https://www.scootersoftware.com/v5help/optionscommand.html) | `0e9e0a7a4bee91aa` |
| <a id="s038"></a>S038 | [bc-optionsop](https://www.scootersoftware.com/v5help/optionsop.html) | `0bb09f6b452f19a0` |
| <a id="s039"></a>S039 | [bc-optionsopenwith](https://www.scootersoftware.com/v5help/optionsopenwith.html) | `e6849ffa81f31bbc` |
| <a id="s040"></a>S040 | [bc-patch](https://www.scootersoftware.com/kb/patch) | `41feb44bfbd6f53d` |
| <a id="s041"></a>S041 | [bc-pix_how_to_compare](https://www.scootersoftware.com/v5help/pix_how_to_compare.html) | `5b02e89a6952a693` |
| <a id="s042"></a>S042 | [bc-profileamazons3](https://www.scootersoftware.com/v5help/profileamazons3.html) | `df48e7d2629cf340` |
| <a id="s043"></a>S043 | [bc-profileftplogin](https://www.scootersoftware.com/v5help/profileftplogin.html) | `d2a8bd4a28284a5b` |
| <a id="s044"></a>S044 | [bc-profileftptransfer](https://www.scootersoftware.com/v5help/profileftptransfer.html) | `09aa602acc771150` |
| <a id="s045"></a>S045 | [bc-profilesvn](https://www.scootersoftware.com/v5help/profilesvn.html) | `2b0f6e7ee54b169c` |
| <a id="s046"></a>S046 | [bc-profilewebdav](https://www.scootersoftware.com/v5help/profilewebdav.html) | `8bf313d269219950` |
| <a id="s047"></a>S047 | [bc-remote_services](https://www.scootersoftware.com/v5help/remote_services.html) | `331052b62cd602a7` |
| <a id="s048"></a>S048 | [bc-scripting_reference](https://www.scootersoftware.com/v5help/scripting_reference.html) | `de378f768f0a40eb` |
| <a id="s049"></a>S049 | [bc-scripts](https://www.scootersoftware.com/v5help/scripts.html) | `039a7d132114f194` |
| <a id="s050"></a>S050 | [bc-searching_for_text](https://www.scootersoftware.com/v5help/searching_for_text.html) | `07a1e8f723fa73c6` |
| <a id="s051"></a>S051 | [bc-session_settings](https://www.scootersoftware.com/v5help/session_settings.html) | `41596f0b59324285` |
| <a id="s052"></a>S052 | [bc-sessiondircomparison](https://www.scootersoftware.com/v5help/sessiondircomparison.html) | `74ce624eee522086` |
| <a id="s053"></a>S053 | [bc-sessiondirhandling](https://www.scootersoftware.com/v5help/sessiondirhandling.html) | `7f9de72c7c4c4a1c` |
| <a id="s054"></a>S054 | [bc-sessiondirmisc](https://www.scootersoftware.com/v5help/sessiondirmisc.html) | `ff9947c41315d1d6` |
| <a id="s055"></a>S055 | [bc-sessiondirnamefilter](https://www.scootersoftware.com/v5help/sessiondirnamefilter.html) | `570893617a112dc4` |
| <a id="s056"></a>S056 | [bc-sessiondirotherfilters](https://www.scootersoftware.com/v5help/sessiondirotherfilters.html) | `4b474e7795e57d04` |
| <a id="s057"></a>S057 | [bc-sessionhexcomparison](https://www.scootersoftware.com/v5help/sessionhexcomparison.html) | `3b3dc079baffcd2e` |
| <a id="s058"></a>S058 | [bc-sessionpixreplacements](https://www.scootersoftware.com/v5help/sessionpixreplacements.html) | `e42f353a4d92c181` |
| <a id="s059"></a>S059 | [bc-sessiontablecolumns](https://www.scootersoftware.com/v5help/sessiontablecolumns.html) | `bb86e4a376cec70a` |
| <a id="s060"></a>S060 | [bc-sessiontablerows](https://www.scootersoftware.com/v5help/sessiontablerows.html) | `7e5af15a03e0549f` |
| <a id="s061"></a>S061 | [bc-sessiontablesheets](https://www.scootersoftware.com/v5help/sessiontablesheets.html) | `05bca926f6bbc0b5` |
| <a id="s062"></a>S062 | [bc-sessiontextalignment](https://www.scootersoftware.com/v5help/sessiontextalignment.html) | `3c2465fdf5eb9e99` |
| <a id="s063"></a>S063 | [bc-sessiontextformats](https://www.scootersoftware.com/v5help/sessiontextformats.html) | `43042232e54be011` |
| <a id="s064"></a>S064 | [bc-sessiontextimportance](https://www.scootersoftware.com/v5help/sessiontextimportance.html) | `706d120927a4cc5a` |
| <a id="s065"></a>S065 | [bc-sessiontextmergealignment](https://www.scootersoftware.com/v5help/sessiontextmergealignment.html) | `279aa1e9e05858a7` |
| <a id="s066"></a>S066 | [bc-sessiontextreplacements](https://www.scootersoftware.com/v5help/sessiontextreplacements.html) | `3c803e26c93764cd` |
| <a id="s067"></a>S067 | [bc-sharing_sessions](https://www.scootersoftware.com/v5help/sharing_sessions.html) | `d2a8c5c6184865bd` |
| <a id="s068"></a>S068 | [bc-snapshots](https://www.scootersoftware.com/v5help/snapshots.html) | `d4294b9b341a2351` |
| <a id="s069"></a>S069 | [bc-standard_vs_pro](https://www.scootersoftware.com/v5help/standard_vs_pro.html) | `9a5b45f592a1ba56` |
| <a id="s070"></a>S070 | [bc-support_ordering_and_license](https://www.scootersoftware.com/v5help/support_ordering_and_license.html) | `c05004d27f5fbb82` |
| <a id="s071"></a>S071 | [bc-table_working_with_multiple](https://www.scootersoftware.com/v5help/table_working_with_multiple.html) | `f85506a6be4aebfe` |
| <a id="s072"></a>S072 | [bc-using_text_merge](https://www.scootersoftware.com/v5help/using_text_merge.html) | `d9e6d0069ed9b254` |
| <a id="s073"></a>S073 | [bc-viewdirmerge](https://www.scootersoftware.com/v5help/viewdirmerge.html) | `7525f69ba1c05ebc` |
| <a id="s074"></a>S074 | [bc-viewhex](https://www.scootersoftware.com/v5help/viewhex.html) | `f2aee04ef8024c36` |
| <a id="s075"></a>S075 | [bc-viewhome](https://www.scootersoftware.com/v5help/viewhome.html) | `dc1df052f9a78c7f` |
| <a id="s076"></a>S076 | [bc-viewother](https://www.scootersoftware.com/v5help/viewother.html) | `f7252ab66b937122` |
| <a id="s077"></a>S077 | [bc-viewpix](https://www.scootersoftware.com/v5help/viewpix.html) | `7667769f923dcf36` |
| <a id="s078"></a>S078 | [bc-viewreg](https://www.scootersoftware.com/v5help/viewreg.html) | `eb886d5c91230dfb` |
| <a id="s079"></a>S079 | [bc-viewtable](https://www.scootersoftware.com/v5help/viewtable.html) | `6dd86b9b9e55ea98` |
| <a id="s080"></a>S080 | [bc-viewtext](https://www.scootersoftware.com/v5help/viewtext.html) | `5e556ca61c9aa7d6` |
| <a id="s081"></a>S081 | [bc-viewtextmerge](https://www.scootersoftware.com/v5help/viewtextmerge.html) | `6a0729043e39d184` |
| <a id="s082"></a>S082 | [bc-walking_through_differences](https://www.scootersoftware.com/v5help/walking_through_differences.html) | `278882ca26f79206` |
| <a id="s083"></a>S083 | [bc-whats_new](https://www.scootersoftware.com/v5help/whats_new.html) | `f61f0e7c88d1c831` |
| <a id="s084"></a>S084 | [bc-where_settings_are_stored](https://www.scootersoftware.com/v5help/where_settings_are_stored.html) | `64e90b772795f961` |
| <a id="s085"></a>S085 | [tg-blame](https://tortoisegit.org/docs/tortoisegit/tgit-dug-blame.html) | `f698fce8e0093667` |
| <a id="s086"></a>S086 | [tg-cli](https://tortoisegit.org/docs/tortoisegitmerge/tme-automation.html) | `b0c73a440bc479c8` |
| <a id="s087"></a>S087 | [tg-clientsettings](https://tortoisegit.org/docs/tortoisegit/tgit-dug-settings.html) | `67e4a7a480bb4f2b` |
| <a id="s088"></a>S088 | [tg-conflicts](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug-conflicts.html) | `3cfcaf3e7f685780` |
| <a id="s089"></a>S089 | [tg-controls](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug-toolbar.html) | `9a0d2e735d076149` |
| <a id="s090"></a>S090 | [tg-diff](https://tortoisegit.org/docs/tortoisegit/tgit-dug-diff.html) | `74d22295f26d9b0f` |
| <a id="s091"></a>S091 | [tg-home](https://tortoisegit.org/docs/tortoisegitmerge/) | `8ed65536b62b1626` |
| <a id="s092"></a>S092 | [tg-keys](https://tortoisegit.org/docs/tortoisegitmerge/tme-keyboard.html) | `b34c66baaf5303ec` |
| <a id="s093"></a>S093 | [tg-license](https://tortoisegit.org/docs/tortoisegitmerge/tme-preface.html) | `6adb63eee6afaaba` |
| <a id="s094"></a>S094 | [tg-log](https://tortoisegit.org/docs/tortoisegit/tgit-dug-showlog.html) | `6fc706957a7d51ad` |
| <a id="s095"></a>S095 | [tg-open](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug-open.html) | `e9c7e1a09fad68ee` |
| <a id="s096"></a>S096 | [tg-release](https://tortoisegit.org/docs/releasenotes/) | `c23b36e202d4d746` |
| <a id="s097"></a>S097 | [tg-settings](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug-settings.html) | `54bfd42b352dfe08` |
| <a id="s098"></a>S098 | [tg-view](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug.html) | `e1a5a271cf42b45d` |

校验：92 组行为覆盖；369 个唯一 ACTION-ID 均有矩阵映射；98 个官方来源；18 个明确区分状态的缺口记录。未修改规格真源或 issue 状态，未提交/推送代码。
