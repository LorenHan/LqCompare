# VCS 交付记录（2026-09-20）

## 范围与集成接口

仅新增/修改 `Code/Services/Vcs`、`Code/Views/Vcs`、`Code/Tests/Vcs`、`Code/Tests/VcsView` 和本文件。未修改共享规格、App、Session 或其他团队文件；未对开发仓库执行暂存、提交、推送、重置或变基。

- 服务入口：`vcsbackend.h`，命名空间 `LqCompare::Vcs`。`Backend` 可注入假实现；`GitBackend(Options)` 通过用户安装的 Git 执行只读查询。
- 视图入口：`vcsview.h`，`LqCompare::VcsView(const QString &path, QWidget *parent = nullptr)`；另有接受 `QSharedPointer<Vcs::Backend>` 的构造函数。
- `VcsView::Mode` 为 `Head`、`Index`、`Revisions`、`History`。公开 `setMode()`、`setPath()`、`refresh()`、`cancel()`、`isBusy()`。
- 视图信号：`compareRequested(const Vcs::Comparison &)`、`errorOccurred(const QString &)`、`statusChanged(const QString &)`。
- `vcs.pri` / `vcsview.pri` 已由顶层预留的 exists/include 接入；视图 `.pri` 增加 QtConcurrent。服务不依赖 GUI。

**打开比较时必须保留 `Comparison` 值或其 `lifetime`，直到新的比较会话关闭。** 两侧路径均为私有临时目录中的只读快照，左、右原始来源见 `leftLabel` / `rightLabel`。接收者应同时禁用两侧编辑和保存；`binary` 为真时路由十六进制视图。VCS 视图不创建文本会话，不依赖其实现。物理只读权限不能替代接收视图的只读编辑约束。

## 已实现

### 只读后端

- 从文件、目录、已删除文件的现存祖先识别所属仓库；通过 Git 自身识别嵌套仓库、worktree 的独立 git directory 和 shared common directory。
- 区分没有 HEAD 的新仓库，提供可读错误；使用完整提交哈希固定历史来源。
- 一次 porcelain `git status -z` 批量查询整个仓库，保留两列 index/worktree 状态、未跟踪、忽略、重命名旧路径和冲突。
- 读取工作副本、HEAD/任意提交、暂存区 stage 0，以及冲突 stage 1（base）、2（ours）、3（theirs）。冲突 stage 0 明确报错。
- 两个修订、修订与工作副本、修订与索引、索引与工作副本的变更列表；反向比较；根提交相对空版本。
- 文件新增/删除形成空边；重命名使用各自路径；未跟踪文件纳入工作副本列表。冲突 U/M 重复记录合为一个冲突条目。
- 已从索引删除但仍在磁盘上的同名文件不会被当作空右侧；标为 M 以保留物理文件的比较入口（内容相同也可能在清单中显示此条目）。
- `log()` 每页最多 1000 条，默认 100，支持 skip、路径、作者、固定字符串消息关键词和日期过滤；返回作者、邮箱、日期、完整消息、父提交及引用信息。
- `references()` 返回本地分支、远程引用和标签；`blame()` 返回逐行提交/作者/日期/内容。`revisionGraph()` 返回含父提交的分页日志数据，没有图形布局。
- 安全独立参数 `QProcess`，使用 `--literal-pathspecs` 和修订 `--end-of-options`；保留空格、tab、换行、Unicode、前导破折号和冒号的文件名。
- 清除继承的 `GIT_*` 环境干扰，禁用 optional locks、fsmonitor、pager、交互认证、外部 diff/textconv；记录查询类别、耗时和退出码，不记录文件正文。
- 默认单进程 15 秒超时、32 MiB 输出限制；可通过 `Options` 配置 Git 路径、超时、大小上限；轮询取消会终止进程。
- Git 查询可能执行配置的 clean/process 内容过滤器。后端先以 config、ls-files、check-attr 检查实际受影响的已跟踪路径；命中外部过滤器时明确拒绝工作区查询，保留历史读取/比较。没有实际使用的全局 LFS/filter 定义不阻断查询。
- 拒绝仓库外路径、指向外部目录的链接和工作树符号链接读取；子模块/目录内容不伪装成文本文件。历史 blob 原样读取。
- 比较快照位于 `QTemporaryDir`，通过共享所有权延长到接收会话生命周期；写完改为只读；最后一个持有者释放后删除。UTF-16 BOM 文件不因 NUL 字节误判为二进制。

### 可用视图

- HEAD、索引、两个修订和提交日志四种模式，路径可限定文件/目录。
- 所有后端调用在 QtConcurrent 工作线程；取消、更换模式、刷新会使旧结果失效；关闭视图不会等待后台任务，也不会让回调访问已销毁的视图。
- HEAD 和输入引用先固定完整提交哈希，界面标明两侧来源和方向。
- 提交日志每页 100 条，支持按钮及滚动触发加载更多；选择提交显示完整消息、父列表和相对第一个父提交的变更；根提交使用空边。
- 双击变更文件或按钮生成只读比较信号；无 Git 时相关控件置灰，错误、超时、取消及无 HEAD 均有明确提示。

## 实际验证

环境：macOS、Qt 5.15.2 clang_64、C++17；所有独立构建最多 `make -j2`。未重复跑全量测试，没有 Windows MinGW 执行结果。

- `Code/Tests/Vcs/VcsTests.pro`：真实临时仓库（init/commit/branch/tag/rename/merge-conflict/worktree/nested），不访问网络或用户仓库。覆盖 HEAD/index/worktree、root 空边、特殊路径、修订注入与歧义、索引冲突三阶段、日志分页/过滤/引用/blame、快照只读与共享释放、UTF-16/二进制、链接边界、clean/process helper 不执行。逐字节比较查询前后 index、HEAD、refs 和工作文件。
- 超时、运行中取消、输出限额通过测试程序自身的 helper 模式注入可执行文件，真实启动/终止 QProcess；不依赖 shell sleep。
- `Code/Tests/VcsView/VcsViewTests.pro`：**16 passed / 0 failed / 0 skipped**（包含 init/cleanup），验证无 Git、无 HEAD、后台线程、两侧映射、重命名、根/首父、分页、快照共享、取消和销毁。独立构建目录 `/tmp/lqcompare-vcsview-tests-20260920-1`，命令 `make -j2` 后 `QT_QPA_PLATFORM=offscreen ./bin/tst_vcsview -v1`。
- 视图截图 `/tmp/lqcompare-vcsview-history-20260920.png` 已实际打开检查；是注入假后端的日志视图截图，不是整应用集成截图。
- `python3 tools/check_layering.py` 通过。源码 `git diff --check` 无格式错误。

服务最终结果（Git 2.50.1）：**31 passed / 0 failed / 0 skipped**，25.583 秒；`PATH=/nonexistent` 的无 Git 轮为 **8 passed / 0 failed / 23 skipped**，0.374 秒。只跳过依赖真实 Git 的行；降级、快照和 helper 进程用例仍执行。构建目录 `/tmp/lqcompare-vcs-tests.fQ6tGT/build`；完整结果 `result.txt` 和 `no-git-result.txt`。

复现服务套件：在独立构建目录执行 `/Users/loren/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/Vcs/VcsTests.pro`，再 `make -j2`、`./bin/tst_vcs -v1`。qmake 针对当前较新 macOS SDK 发出兼容提醒，实际编译与测试通过。

## 尚未完成/不宣称验收的条目

这是 VCS 只读核心闭环，不是 VCS-001～019 全部验收完成：

- 无仓库探测缓存、Git 最低版本自动检测、全局配置页/启停/并行度设置。要求安装支持所用命令选项的 Git；本机真实 Git 已验证。
- 无 folder 状态列聚合/着色、子模块独立入口与完整状态解释；工作树符号链接明确拒绝内容比较。
- 无引用选择浏览器、merge-base 模式、无共同祖先/多共同祖先策略；两个修订视图为直接比较。
- 无日志正则搜索、高级过滤 UI、图形列、引用徽标样式、修订图布局/导出、Blame 视图。服务仅提供相应基础数据。
- 无变更增删行数、显式权限列、复制探测开关、变更列表批量打开/导出、任选 merge parent；日志固定首父并明确标示。
- 冲突三阶段读取已测；三方合并会话绑定、MERGED 写回、外部 Git 工具入口不在此交付内。
- 两个临时快照之间的生成不是仓库级事务，其他程序并发修改工作区/索引时可能读取不同时刻。刷新重新查询；不锁定、不改写用户仓库。
- 非 UTF-8 原始文件名尚未以 byte-preserving 路径类型支持。超出 32 MiB 的默认单文件读取明确拒绝。
- 主协调负责把比较信号接到文本/十六进制会话并实施视图只读；本任务没有越界修改 App 或 Session。完整应用 UI 与 Windows 交付验收仍由对应团队进行。

---

# Blame 视图（续做，2026-09-21）

## 上一轮留下了什么

上一轮并行开发被额度中断，`Code/Views/Vcs/blameview.{h,cpp}` 与 `Code/Tests/VcsBlameView/*` 已经落盘，但：

- `Code/Views/Vcs/vcsview.pri` 里只列了 `vcsview.h/.cpp`，**没有 blameview** → 这份代码从来没有被编译过、也从来没跑过测试；
- `blameview.cpp` 本身也被截断：文件到 `BlameView::Private::compareParent()` 的右花括号就结束了，缺 `Private` 的类收尾、缺 `buildUi()`、缺两个构造函数与全部公开 API（连 `};` 都没有，是一份编译不过的半成品）。

本轮做的是「接线 + 补齐 + 修真缺陷 + 跑到全绿」，并逐条核对规格落地程度。

## 做了什么

1. `Code/Views/Vcs/vcsview.pri` 增加 `blameview.h` / `blameview.cpp`；`Code/Views/views.pri` 未动（它已经用 `exists()` include 了 `vcsview.pri`）。
2. `Code/Tests/VcsBlameView/VcsBlameViewTests.pro` 从「自己再列一遍 `blameview.h/.cpp`」改成 `include(../../Views/Vcs/vcsview.pri)`。**理由**：前者会让「`.pri` 里漏接 blameview」这件事永远测不出来（测试照样能编能跑），改成 include 之后，`.pri` 少写一行本套件立刻构建失败。代价是 `vcsview.cpp` 也被编一次，可接受。
3. 补齐 `blameview.cpp` 的尾部：`BlameView::Private` 收尾、`buildUi()`（文件/追至修订/刷新/取消、着色模式、只看作者、只看提交前缀、合并块、六列表格 + 首列窄栏委托、详情面板、跳转与父比较按钮、状态行 + 不定长进度条）、两个构造函数、析构、以及 `path()/revision()/isBusy()/colorMode()/setPath()/setRevision()/setColorMode()/refresh()/cancel()/showSelectedRevision()/compareSelectedWithParent()`。
4. 修掉两处真缺陷（下面「关键设计理由」里有理由）：
   - **缺非 UTF-8 校验**：`git blame` 的行号是按 blob 字节切出来的，若界面显示的是一份「坏字节被替换成 U+FFFD」的文本，行号会和用户看到的行错位，于是「跳到第 N 行的提交」会跳到别的行。现在用 `QTextCodec::ConverterState::invalidChars` 在入口拒绝并说明原因。
   - **父提交比较多读一次文件**：原先在 `compare()` 之前先 `catFile` 探一次右侧是否存在，多一次 Git 进程；而 `Backend::compare()` 本身就把「不存在的一侧」写成空快照并在标签里标 `[不存在]`。已删掉这次探测，改用比较结果本身表达。
5. 新增用例 `colorModesMatchAuthorsAndAgeDirection`，把 VCS-013 的「按作者着色」与「按日期龄越旧越深」钉住（见「验证结果」里的漏检说明）。

## 关键设计理由

- **首列窄栏按提交哈希取色（不跟着行底色模式走）**：三种着色模式换的是整行底色，窄栏始终是「提交条纹」——同一个提交在任何模式下都拿到同一种颜色，用户切模式时行身份不会跟着乱。窄栏文字只在提交块起始行显示 `▌`、其余显示 `│`，让「块从哪里开始」不只能靠颜色深浅去猜。
- **单击不跳转，按钮与双击才跳转**：连续按行读详情是追溯表最高频的动作，若每次点行都把应用切到日志视图，用户就没法顺序看几行。跳转保留 `blameOpenRevision` 按钮与表格双击两个显式入口。
- **「追至修订」不是「只看某提交」**：前者是 `git blame <rev>` 的上界语义，后者是下面的提交前缀过滤。两个概念分开命名、分开控件，避免用户把 `HEAD~5` 理解成「只看 HEAD~5 改过的行」。
- **进度是不定长的**：`git blame` 是一次进程调用，拿不到中间进度。用一个编出来的百分比比没有进度更误导，因此用不定长 `QProgressBar` 表达「在跑」，配合即时生效的取消。
- **前后台生命周期沿用 `VcsView` 的既有做法**：`QFutureWatcher` 以 `BlameView` 为连接上下文（销毁即断连），`Private` 析构先把取消位置真——**不等待** future。所以关掉视图既不阻塞，也不会让回调回到已销毁的视图。
- **过滤在本地做，不重查后端**：作者、提交前缀、折叠都只重建模型行集合，不重新调用 `blame()`（用例断言 `blameCalls().size()` 不因过滤增长）。过滤后行号仍是原来文件里的行号（`finalLine`），不是过滤后的序号。
- **块合并要同时满足三个条件**：同一提交 + 同一历史路径 + 行号连续。少任意一个都会把「重命名前后」或「被打散的行」错误地并成一个块。

## 边界情况（均有用例覆盖）

- 未检测到 git、仓库尚无 HEAD：分别给出可读错误，相关控件置灰，且完全不调用 `blame()`。
- 所选修订中的文件：为空 / 不存在（还未提交或该修订里没这个路径）/ 二进制 / 非 UTF-8 / 后端返回 `Unsupported`，五种都有各自的说明文案，且都不调用 `blame()`。
- 归因提交的变更清单里找不到唯一匹配的历史路径（或是删除条目）：拒绝比较，**不猜重命名映射**。
- 行缺少历史路径：拒绝跳转与父比较，并说明原因。
- 取消后迟到的结果必须丢弃；刷新后晚到的旧结果不得覆盖新结果（代次 + 取消位双保险）。
- 删除视图不阻塞（实测 < 250ms），且后台线程仍能看到取消位。

## 还没做的：VCS-012 / VCS-013 剩余范围

逐条如实核对（宁可只报已实现的）：

**VCS-012「逐行追溯（Blame）」**

| 要求 | 状态 |
| --- | --- |
| 显示每行的修订号、作者、日期，行首以窄栏着色 | 已落地 |
| 「忽略空白改动」选项 | **未实现**（服务层 `Backend::blame()` 也没有对应参数，属剩余范围） |
| 「跨重命名追溯」选项 | **未实现**：`BlameLine::originalPath` 已经带回历史路径并被用于跳转与父比较，但 blame 本身没有 `-C` / `--follow` 开关与对应 UI，属剩余范围 |
| 指定修订范围（追到某个提交为止） | **部分**：只有「追至修订」这**一个上界**，没有起点 + 终点的区间 |
| 点击某行跳到该提交的日志视图 | **部分**：`revisionRequested(repositoryRoot, commit, relativePath, originalLine)` 已按该行的**原始**路径与**原始**行号发出，视图也提供了按钮与双击两个入口；单击只更新详情（设计理由见上）。把它接到应用日志视图属于 App 侧集成，本任务未越界修改 `Code/App/*` |
| 后台计算并显示进度，大文件可取消 | **部分**：查询确实在 `QtConcurrent` 工作线程、取消即时生效且已测；但进度是**不定长**的，没有百分比 |

**VCS-013「追溯视图的着色与交互」**

| 要求 | 状态 |
| --- | --- |
| 按作者 / 按日期龄（越旧越深）/ 按提交块，三种着色 | 已落地，且对比度与语义各有用例 |
| 悬停显示修订、作者、日期、消息首行 | 已落地 |
| 同一提交的连续行合并为块，块内可折叠 | 已落地（提交 + 历史路径 + 行号连续） |
| 「只看某作者」过滤 | 已落地 |
| 「只看向某提交的区间」过滤 | **部分**：只有「只看某个提交（哈希前缀）」，没有区间 |
| 着色模式下保持文本可读性 | 已落地：用例按 WCAG 相对亮度算对比度，要求 ≥ 4.5 |

## 验证结果

环境：macOS、Qt 5.15.2 clang_64、C++17；产物为 x86_64 Mach-O 并已 ad-hoc 签名（`codesign -dv` 确认）。

- 命令：`LQCOMPARE_TEST_BUILD_ROOT=/tmp/lq-blame-build Code/Tests/run-tests.sh VcsBlameView`
- **`Totals: 23 passed, 0 failed, 0 skipped, 0 blacklisted`**（约 1.0 秒）。
- **`blameview.cpp` 确实进了产物**：`blameview.o` 与 `moc_blameview.o` 都在 qmake 生成的链接行里；`nm -C` 在二进制里数出 202 个 `LqCompare::BlameView*` 符号。
- `python3 tools/check_layering.py` 通过。
- 定点变异 **12 处，12 处命中**（每一处都确认过 `make` 真的重编了 `blameview.cpp`；改之前与还原之后都删掉目标 `.o`，避开「`shutil.copy2` 连旧 mtime 一起还原 → `make` 跳过重编 → 跑上一轮变异二进制」那个坑）：
  - M1 只看作者过滤失效 → 命中 `authorAndCommitFiltersRetainOriginalLineNumbers`
  - M2 只看提交前缀过滤失效 → 命中同一条
  - M3 提交块合并边界差一行（`+1` 改 `+2`）→ 命中 `foldingUsesContiguousCommitAndOriginalPathBlocks`
  - M4 `cancel()` 不置取消位 → 命中 `cancelAndRefreshDiscardLateRows`
  - M5 回调不判代次/取消 → 命中 `cancelAndRefreshDiscardLateRows`（陈旧结果覆盖了新结果）
  - M6 悬停信息缺「消息首行」→ 命中 `resolvesRevisionAndPreservesMetadataOffGuiThread`
  - M7 「追至修订」被写死成 HEAD → 命中同一条
  - M8 前景色改浅（对比度不合格）→ 命中 `allColorModesKeepReadableContrast`
  - M9 缺历史路径不再拒绝 → 命中 `missingOriginalPathIsNeverGuessed`
  - M10 二进制守卫删除 → 命中 `unsupportedFileContentIsExplained(binary)`
  - M11 日期龄渐变方向写反 → 命中 `colorModesMatchAuthorsAndAgeDirection`
  - M12 「按作者着色」实际按提交块取色 → 命中同一条
- **M11 / M12 原本是漏检**：只靠「有颜色 + 对比度合格」这两条断言，把渐变方向写反、或把作者色换成块色，界面看起来完全正常（颜色依然好看、对比度依然合格），没有任何用例会红。这正是本轮补 `colorModesMatchAuthorsAndAgeDirection` 的原因——它断言「同作者不同块必须同色」「不同作者必须不同色」「越旧必须越深」，M11 与 M12 随即变成命中。
- 离屏截图 `/tmp/lqcompare-blame-view-20260921.png` 已实际打开检查（注入假后端的追溯视图，不是整应用集成截图）：首列窄栏按提交显示成三条条纹（`aaaa…` 青、`cccc…` 紫、`bbbb…` 橙）；Age 模式下第 7 行（2023-11-08，最旧）底色明显比第 1 行深，与「越旧越深」一致；修订列完整显示 12 位哈希；作者/日期/内容各列与夹具一致；详情面板显示修订/作者/邮箱/日期/历史路径/原始行/消息首行；状态行是「共 7 行 · 当前显示 7 行」；筛选行里「合并同一提交的连续行」默认未勾选，与默认展开逐行的行为一致。
- **未在目标平台验证**：Windows（MinGW）与 Linux **都没有编译过、没有运行过**。本视图只用 QtWidgets / QtConcurrent 与 Qt 5.15 API、没有平台分支，但「只在 macOS 实测」这一点不向其他平台延展。
- **未做集成验证**：把 blame 视图挂到应用标签页、把 `revisionRequested` 接到真实日志视图，属 App 侧工作，不在本任务范围。

## 发现但未改的问题

1. `Backend::blame()` 的签名只有 `(repo, path, revision, cancel)`，没有承载「忽略空白改动」与「跨重命名追溯」这两个选项的位置。要落地 VCS-012 这两条，需要先扩服务层接口（例如加一个 `BlameOptions` 结构），属于跨层改动，本轮不擅自扩。
2. 「按提交块着色」时，行底色用的是**块的序号**取模调色板，与首列窄栏（提交哈希取色）是两套颜色。同一段提交在两种呈现里颜色不同，理论上会让用户困惑；保留的原因是窄栏要提供跨模式稳定的提交身份。若后续统一，建议都按提交哈希取色。
3. `BlameModel` 一次把全部 `BlameLine` 留在内存里（每行含 `text`）。大文件（十万行级）的内存与首次布局没有实测，服务层 32 MiB 单文件上限是唯一的硬边界。

