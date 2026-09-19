# 并行开发工作流划分

> 面向「多个对话/多个开发者同时推进」的场景。**开始之前先读 [current-handoff.md](current-handoff.md)。**
>
> 本文档解决三个具体问题：谁能改哪些文件、按什么顺序开工、共享文件怎么避免打架。

## 1. 核心约束：文件归属是硬边界

并行开发失败的常见原因不是任务划分不清，而是**两个工作流改了同一个文件**，
合并时冲突大到不如重做。因此每个工作流有自己独占的目录，跨目录只通过接口调用。

### 1.1 各工作流独占的目录

| 工作流 | 独占目录 | 允许新增的 `.pri` |
| --- | --- | --- |
| A 平台底座 | `Code/Services/Platform/`、`Code/Services/Files/` | `platform.pri`、`files.pri` |
| B 会话框架 | `Code/Services/Session/`、`Code/Views/Session/` | `session.pri`、`viewsession.pri` |
| C 文本比对引擎 | `Code/Services/Text/` | `text.pri` |
| D 文本比对视图 | `Code/Views/Text/` | `textview.pri` |
| E 文件夹比对 | `Code/Services/Folder/`、`Code/Views/Folder/` | `folder.pri`、`folderview.pri` |
| F 三方合并 | `Code/Services/Merge/`、`Code/Views/Merge/` | `merge.pri`、`mergeview.pri` |
| G 专用视图 | `Code/Services/Special/`、`Code/Views/Special/` | `specialviews.pri` |
| H 过滤与格式 | `Code/Services/Filter/`、`Code/Services/Format/` | `filter.pri`、`format.pri` |
| I 报表与补丁 | `Code/Services/Report/`、`Code/Services/Patch/` | `report.pri`、`patch.pri` |
| J 同步与快照 | `Code/Services/Sync/`、`Code/Services/Snapshot/` | `sync.pri`、`snapshot.pri` |
| K 版本控制 | `Code/Services/Vcs/`、`Code/Views/Vcs/` | `vcs.pri`、`vcsview.pri` |
| L 命令行与脚本 | `Code/Services/Cli/`、`Code/Services/Script/` | `cli.pri`、`script.pri` |
| M 选项与外观 | `Code/Services/Settings/`、`Code/Views/Options/` | `settings.pri`、`options.pri` |
| N Ribbon 深化 | `Code/Views/Page/`、`Code/Views/Shell/`、`Code/Services/Command/` | 已有的 `page.pri` 等 |
| O 工程与文档 | `tools/`、`.github/`、`docs/`（除 `docs/PRD-actions.md`） | — |

### 1.2 必须串行的共享文件

以下文件被多个工作流需要，**改动会互相冲突，因此必须串行**。规则是：
需要改动时在 issue 里注明，由协调方（或提交顺序上更晚的那个工作流）统一改一次。

| 共享文件 | 冲突点 | 处理方式 |
| --- | --- | --- |
| `Code/LqCompare.pro` | 每个工作流都要 `include()` 自己的 `.pri` | **一次加齐**：按本文档的工作流清单预留 `include` 行，用 `exists()` 保护 |
| `Code/Services/services.pri` | 同上 | 同上 |
| `Code/Views/views.pri` | 同上 | 同上 |
| `tools/spec/` 下的数据文件 | 改规格会重新生成 PRD 与 issue | 一个工作流只改自己功能域的文件（`b_text.py` 归 C，`c_folder.py` 归 E，依此类推） |
| `docs/PRD-actions.md` | 生成物 | **任何人都不要手改**；改数据后运行 `tools/publish_issues.py prd` |
| `README.md` | 功能域统计表 | 规格条目数变化时才需要改，由 O 工作流统一改 |
| `Code/Pictures/*.svg` + `Pictures.qrc` | 新增图标 | 图标由 `tools/generate_icons.py` 生成；新增图标时同时改生成脚本与 qrc，且在 PR 里说明 |

## 2. 依赖顺序与开工波次

依赖关系（箭头表示「被依赖」，必须先完成）：

```
A 平台底座 ─┬─→ B 会话框架 ─┬─→ C 文本引擎 ─→ D 文本视图 ─┐
            │               │                            ├─→ F 三方合并
            │               ├─→ E 文件夹比对 ─→ J 同步与快照
            │               ├─→ G 专用视图
            │               ├─→ I 报表与补丁 ─→ L 命令行与脚本
            │               ├─→ K 版本控制
            │               ├─→ M 选项与外观
            │               └─→ N Ribbon 深化
            └─→ H 过滤与格式
O 工程与文档（无依赖，可随时进行）
```

### 第一波（可同时开工，互不依赖）

| 工作流 | 为什么现在能开 | 建议规模 |
| --- | --- | --- |
| **A 平台底座** | 不依赖任何其它模块；是后续一切的地基 | PLAT-001/002/003/007/008、DIR-022 ~ DIR-027、ENG-011 |
| **B 会话框架** | 只依赖 A 的接口声明（可先用接口 + 假实现起步） | SESS-001 ~ SESS-020、UI-005、UI-029 |
| **H 过滤与格式** | 只需要 A 的目录枚举接口 | FILT-001 ~ FILT-012、FMT-001 ~ FMT-012 |
| **N Ribbon 深化** | 骨架已跑通，深化项自成一体 | UI-003/004/006/017 ~ UI-026、UI-030 ~ UI-035 |
| **O 工程与文档** | 无依赖 | ENG-001 ~ ENG-013、DOC-001 ~ DOC-006 |

### 第二波（第一波完成后）

| 工作流 | 前置 |
| --- | --- |
| **C 文本比对引擎** | B 的会话契约 |
| **E 文件夹比对** | A 的文件系统抽象 + B 的会话契约 |
| **K 版本控制** | A + B（后端只调用外部 git，不依赖界面） |
| **M 选项与外观** | B 的设置作用域 |

### 第三波

| 工作流 | 前置 |
| --- | --- |
| **D 文本比对视图** | C |
| **G 专用视图** | B（各视图各自独立，可再拆成多个对话） |
| **I 报表与补丁** | B |
| **F 三方合并** | C + D |
| **J 同步与快照** | E |
| **L 命令行与脚本** | B + I |

**给多对话的建议**：第一波同时开 **3 个**最稳（A、B 必开，第三个在 H / N / O 里挑一个）。
一次开 5 个以上时，`LqCompare.pro`、`services.pri`、`views.pri` 的改动会明显互相等。

## 3. Git 工作方式

1. **一个工作流一个分支**，命名 `ws/<字母>-<主题>`，例如 `ws/a-platform`。
2. **一个规格条目一个 commit**，提交消息格式：
   ```
   <动作>：<一句话说明>（<ACTION-ID> / #<issue 号>）
   ```
   例：`新增：目录枚举的并行扫描（PLAT-002 / #42）`
3. **PR 标题带 ACTION-ID**，PR 描述里写清「完成了哪几条完成标准、加了哪些测试」。
4. **不要 rebase 别人的分支**，也不要 `push --force`。
5. **合并前必须本地跑过**：
   ```bash
   Code/Tests/run-tests.sh
   python3 tools/check_layering.py
   python3 tools/check_icons.py
   python3 tools/check_spec.py
   ```

## 4. 每个工作流的准入要求

一个条目要算「完成」，必须同时满足：

- [ ] `docs/PRD-actions.md` 里该条目的**每一条完成标准**都成立（不是部分成立）。
- [ ] 新增了对应测试，且 `Code/Tests/run-tests.sh` 全绿。
- [ ] 涉及界面行为时，测试能在 `-platform offscreen` 下运行。
- [ ] 涉及文件系统写操作时，破坏性路径有「预演 / 备份 / 回收站」其中之一。
- [ ] 没有破坏 `tools/check_layering.py`（Services 不得反向依赖界面）。
- [ ] 提交消息里带 ACTION-ID 与 issue 号。
- [ ] 若改了 `tools/spec/` 的数据，已重新生成 PRD 与 issue 索引，且 `check_spec.py` 通过。

## 5. 常见踩坑（前人已经踩过，别再踩）

| 坑 | 现象 | 正确做法 |
| --- | --- | --- |
| 日志宏形参命名为 `level` | 宏体里的 `LqCompare::Log::level()` 被一起替换，编译直接失败 | 形参用 `lvl`（见 `logging.h` 注释） |
| `#include "xxx.moc"` 与 `Q_OBJECT` 放错位置 | 链接报 `undefined symbol _main` 或 `No rule to make target xxx.moc` | `Q_OBJECT` 放头文件时不写 `.moc` include，并加 `QTEST_MAIN` |
| 测试工程 `QT -= gui` | `QKeySequence` 找不到头文件（它属于 QtGui） | 需要 `QT += gui`，但是否创建窗口与模块无关 |
| 手改 `docs/PRD-actions.md` | 下次生成时被冲掉 | 改 `tools/spec/` 的数据 |
| 把交付产物提交进 git | 别人 checkout 后手上的可执行文件被替换成旧构建 | `dist/` 已在 `.gitignore`，`check_spec.py` 会拦 |
| 只认 `:/Pictures/xxx.svg` 完整路径的图标校验 | 用 `icon("xxx.svg")` 的地方被误判为「未引用」，护栏变噪声被忽略 | 校验同时认裸文件名（见 `check_icons.py`） |
