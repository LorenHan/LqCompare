# 当前进度与接手说明

> 更新时间：2026-09-20。**新开一个对话接手本项目时，先读这一页。**
> 详细的并行划分见 [parallel-workstreams.md](parallel-workstreams.md)。

## 1. 一句话现状

规格（369 条）与 GitHub issue 已全部铺好；Qt 工程骨架已在 macOS 上编译通过、
主程序可启动、测试全绿。**服务层开始有真实功能**：文件系统抽象层（PLAT-002）、
回收站服务（PLAT-003）、名称处理（PLAT-007）与批量操作的失败处置（PLAT-008）
已落地。其中回收站在本机是**真的能删进废纸篓再还原回来**的；名称处理连
「无效 UTF-8 的文件名」这种只在 Linux 上出现的输入都写好了测试（CI 上会真实执行）；
错误路径现在携带**原始系统错误码**（errno / Win32 / Cocoa），批量操作会给出
按原因分组的失败清单并支持只重试失败项。
界面上仍是 169 个按钮里 31 条带处理器，其余点击后提示对应 ACTION-ID。

## 1.1 已落地的服务层模块

| 模块 | 条目 | 状态 | 测试 |
| --- | --- | --- | --- |
| `Services/Command/` | UI-024 | 骨架 | `Tests/CommandRegistry`（14 用例） |
| `Services/Log/` | ENG-006 | 骨架 | — |
| `Services/Files/`（文件系统） | PLAT-002 | **部分完成**（Windows 实现未编译验证） | `Tests/FileSystem`（50 用例） |
| `Services/Files/`（回收站） | PLAT-003 | **部分完成**（Windows 实现未编译验证） | `Tests/Trash`（35 用例） |
| `Services/Files/`（名称与 Unicode） | PLAT-007 | **部分完成**（长路径只写在 Windows 侧，未编译验证） | `Tests/PathName`（40 用例 + 1 个仅 Linux 执行） |
| `Services/Files/`（错误携带与批量处置） | PLAT-008 | **部分完成**（界面动作尚未接上） | `Tests/Batch`（36 用例） |

PLAT-002 的详细说明与其「第 2 条完成标准为何不勾选」见
[issue #325](https://github.com/LorenHan/LqCompare/issues/325)；
PLAT-003 见 [issue #324](https://github.com/LorenHan/LqCompare/issues/324)；
PLAT-007 见 [issue #328](https://github.com/LorenHan/LqCompare/issues/328)；
PLAT-008 见 [issue #330](https://github.com/LorenHan/LqCompare/issues/330)。

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

## 2. 已验证的事实（不用再花时间确认）

| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试（全量） | **175 passed / 0 failed / 1 skipped**（Batch 36 + FileSystem 50 + PathName 40 + Trash 35 + CommandRegistry 14） | `Code/Tests/run-tests.sh` |
| 文件系统抽象层 | 47 个纯逻辑用例 + 3 个真实文件系统用例全通过；其中 20 个覆盖 **Windows** 路径规则（盘符 / UNC / 长路径前缀 / 大小写），在 macOS 上真实执行 | `Code/Tests/run-tests.sh FileSystem` |
| 回收站 | 35 个用例全通过。其中 9 个验证 XDG（Linux）的路径与 `.trashinfo` 规则、2 个是**真实**的废纸篓往返与冲突拒绝、多个断言「不可用时搬移函数一次都没被调用」 | `Code/Tests/run-tests.sh Trash` |
| 名称与 Unicode | 40 个用例通过 + 1 个跳过（无效 UTF-8 名字的用例只在 Linux 上执行，CI 会跑）。覆盖字节保真往返、UTF-8 边界与过长编码、Unicode 组合形式、六类文件名问题的原因与位置 | `Code/Tests/run-tests.sh PathName` |
| 错误携带与批量处置 | 36 个用例全通过。其中 9 个验证错误码在三种域下的携带与显示（含「未识别的码只给数字」）、11 个验证失败清单分组、12 个验证执行流程（含「重试只跑失败项」与「停止不移除已完成进度」）、4 个走真实文件系统做一次「设为只读 → 解除只读」往返 | `Code/Tests/run-tests.sh Batch` |
| 分层检查 | 通过（Services 未反向依赖界面） | `python3 tools/check_layering.py` |
| 图标检查 | 通过（27 个图标，声明/引用/文件三者一致） | `python3 tools/check_icons.py` |
| 规格自检 | 通过（369 条，P0 59 条，PRD 与数据同步） | `python3 tools/check_spec.py` |
| Shell 可移植性 | 通过（1 个脚本，无 bash 4 内建与 GNU 工具扩展） | `python3 tools/check_shell.py` |
| Windows 宽字符 API | 通过（40 个源文件、清单内 43 个 API；自测 17 个样本） | `python3 tools/check_winapi.py [--self-test]` |
| 测试套件 | `175 passed / 0 failed`，且「无套件匹配」被视为失败（exit 2） | `Code/Tests/run-tests.sh` |
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
│   ├── Log/                      logging（分级日志）
│   ├── Files/                    filesystem（抽象层 + 错误携带）、pathutils（路径与名称规则）、
│   │                             pathname（字节保真 / Unicode / 显示）、
│   │                             trash（回收站服务 + XDG 规则）、
│   │                             batch（失败清单 / 重试 / 进度）、
│   │                             filesystem_posix/_win、trash_mac.mm/_linux/_win
│   ├── command.pri / log.pri / files.pri / services.pri
├── Pictures/                     27 个 SVG 图标 + Pictures.qrc
├── Tests/
│   ├── Support/                  fakefilesystem（内存文件系统）、faketrashservice
│   │                             （内存回收站），多套件共用
│   ├── CommandRegistry/          tst_commandregistry + .pro（14 用例）
│   ├── FileSystem/               tst_filesystem + .pro（50 用例）
│   ├── PathName/                 tst_pathname + .pro（40 用例 + 1 个仅 Linux）
│   ├── Trash/                    tst_trash + .pro（35 用例）
│   ├── Batch/                    tst_batch + .pro（36 用例）
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
| 见 `git log` | 错误携带（分类 + 原始系统码）与批量操作的失败处置 | PLAT-008 |

远端：369 个 issue 全部创建，标签为 `需求 / 待实现 / <模块> / <优先级>`，
其中 P0 59 条。反查入口是 `docs/github/prd-issues.json`。

**推送必须走 SSH。** `gh` 登录的 token 只有 `gist`、`read:org`、`repo` 三个 scope，
推 `.github/workflows/` 下的文件会被拒（`refusing to allow an OAuth App to create or
update workflow ... without workflow scope`）。走 SSH 不受这个限制：

```bash
git push git@github.com:LorenHan/LqCompare.git main
```

## 4. 下一步该做什么

| 对话 | 工作流 | 从哪条 issue 开始 | 交付什么 |
| --- | --- | --- | --- |
| **A 平台底座** | 继续 | `PLAT-004`（系统图标）→ `PLAT-005`（Shell 集成） | 在 `Services/Platform/` 内新增文件；`.pri` 已在 `services.pri` 里接好 |
| **B 会话框架** | 新开 | `SESS-001`（会话基类）→ `SESS-002`（类型注册表）→ `SESS-006`（设置框架） | `Services/Session/`、`Views/Session/`，含测试 |
| **H 过滤与格式** | 新开 | `FILT-001`（掩码解析器，纯算法、最容易写出完整测试） | `Services/Filter/`、`Services/Format/` |

三个工作流的目录互不重叠，`services.pri` 的 include 已一次加齐（`exists()` 保护），
因此三方都不需要改共享文件。详见 [parallel-workstreams.md](parallel-workstreams.md) §1。

**A 工作流的四件要紧事：**

1. `trash_linux.cpp` 与 `trash_win.cpp` 需要在各自的平台上首次构建并修正。
   它们与 `filesystem_win.cpp` 是当前唯一**从未被编译过**的代码，
   PLAT-002 / PLAT-003 / PLAT-007 的各一条完成标准因此未勾选。拿到 Windows
   机器时一次性过一遍——几份文件共用同一套手写常量 + `static_assert` 模式，
   错法也相似（先看 `NSFileManagerUnmountBusyError` 写成 768 那件事就知道，
   这类错误编译期会直接报出来，不必等运行）。
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
