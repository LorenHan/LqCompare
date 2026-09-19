# 当前进度与接手说明

> 更新时间：2026-09-20。**新开一个对话接手本项目时，先读这一页。**
> 详细的并行划分见 [parallel-workstreams.md](parallel-workstreams.md)。

## 1. 一句话现状

规格（369 条）与 GitHub issue 已全部铺好；Qt 工程骨架已在 macOS 上编译通过、
主程序可启动、测试全绿。**服务层开始有真实功能**：文件系统抽象层（PLAT-002）已落地，
界面上仍是 169 个按钮里 31 条带处理器，其余点击后提示对应 ACTION-ID。

## 1.1 已落地的服务层模块

| 模块 | 条目 | 状态 | 测试 |
| --- | --- | --- | --- |
| `Services/Command/` | UI-024 | 骨架 | `Tests/CommandRegistry`（14 用例） |
| `Services/Log/` | ENG-006 | 骨架 | — |
| `Services/Files/` | PLAT-002 | **部分完成**（Windows 实现未编译验证） | `Tests/FileSystem`（52 用例） |

PLAT-002 的详细说明与其「第 2 条完成标准为何不勾选」见
[issue #325](https://github.com/LorenHan/LqCompare/issues/325)。

## 2. 已验证的事实（不用再花时间确认）

| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试 | `CommandRegistryTests` 14 passed / 0 failed | `Code/Tests/run-tests.sh` |
| 测试（全量） | **66 passed / 0 failed**（FileSystem 52 + CommandRegistry 14） | `Code/Tests/run-tests.sh` |
| 文件系统抽象层 | 49 个纯逻辑用例 + 3 个真实文件系统用例全通过；其中 20 个覆盖 **Windows** 路径规则（盘符 / UNC / 长路径前缀 / 大小写），在 macOS 上真实执行 | `Code/Tests/run-tests.sh FileSystem` |
| 分层检查 | 通过（Services 未反向依赖界面） | `python3 tools/check_layering.py` |
| 图标检查 | 通过（27 个图标，声明/引用/文件三者一致） | `python3 tools/check_icons.py` |
| 规格自检 | 通过（369 条，P0 59 条，PRD 与数据同步） | `python3 tools/check_spec.py` |
| Shell 可移植性 | 通过（1 个脚本，无 bash 4 内建与 GNU 工具扩展） | `python3 tools/check_shell.py` |
| 测试套件 | `14 passed / 0 failed`，且「无套件匹配」被视为失败（exit 2） | `Code/Tests/run-tests.sh` |
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
│   ├── Files/                    filesystem（抽象层）、pathutils（路径规则）、
│   │                             filesystem_posix / filesystem_win（平台实现）
│   ├── command.pri / log.pri / files.pri / services.pri
├── Pictures/                     27 个 SVG 图标 + Pictures.qrc
├── Tests/
│   ├── Support/                  fakefilesystem（可注入故障的内存文件系统，多套件共用）
│   ├── CommandRegistry/          tst_commandregistry + .pro
│   ├── FileSystem/               tst_filesystem + .pro（52 用例）
│   └── run-tests.sh              统一测试运行器
└── ThirdParty/                   myclasspath.pri（定位 LqRibbon）、lqribbon.pri

tools/
├── spec/                         规格数据（唯一事实来源，8 个模块文件，369 条）
├── publish_issues.py             生成 PRD + issue 索引 + 创建 issue（幂等）
├── generate_icons.py             生成图标集
├── check_layering.py             ENG-001 依赖方向
├── check_icons.py                ENG-009 图标一致性
├── check_shell.py                ENG-003 脚本可移植性（bash 4 / GNU 扩展护栏）
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
| **A 平台底座** | 继续 | `PLAT-003`（回收站，**接口已就位、只差平台实现**）→ `PLAT-007`（Unicode 与长路径）→ `PLAT-008`（权限/只读/占用） | 在 `Services/Files/` 内新增文件；`files.pri` 已在 `services.pri` 里接好 |
| **B 会话框架** | 新开 | `SESS-001`（会话基类）→ `SESS-002`（类型注册表）→ `SESS-006`（设置框架） | `Services/Session/`、`Views/Session/`，含测试 |
| **H 过滤与格式** | 新开 | `FILT-001`（掩码解析器，纯算法、最容易写出完整测试） | `Services/Filter/`、`Services/Format/` |

三个工作流的目录互不重叠，`services.pri` 的 include 已一次加齐（`exists()` 保护），
因此三方都不需要改共享文件。详见 [parallel-workstreams.md](parallel-workstreams.md) §1。

**A 工作流的两件要紧事：**

1. `PLAT-003` 直接在 `Files/` 内落地即可——`deleteToTrash()` 接口已经定义好，
   基类当前返回 `NotSupported`，`Tests/FileSystem` 里有一条用例专门断言
   「此时文件必须还在」。接上真实实现时那条用例会失败，提醒你改成真实断言
   （这是刻意的，见 `tst_filesystem.cpp` 的说明）。
2. `filesystem_win.cpp` 需要在 Windows 上首次构建并修正。它是本轮唯一
   **从未被编译过**的代码，PLAT-002 的第 2 条完成标准因此未勾选。

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
