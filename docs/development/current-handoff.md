# 当前进度与接手说明

> 更新时间：2026-09-20。**新开一个对话接手本项目时，先读这一页。**
> 详细的并行划分见 [parallel-workstreams.md](parallel-workstreams.md)。

## 1. 一句话现状

规格（367 条）与 GitHub issue 已全部铺好；Qt 工程骨架已在 macOS 上编译通过、
主程序可启动、测试全绿。**功能实现尚未开始**——界面上 169 个按钮里只有 31 条命令带处理器
（文件/会话/导航/编辑/视图/工具/帮助的基础骨架动作），其余点击后会提示它对应的 ACTION-ID。

## 2. 已验证的事实（不用再花时间确认）

| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试 | `CommandRegistryTests` 14 passed / 0 failed | `Code/Tests/run-tests.sh CommandRegistry` |
| 分层检查 | 通过（Services 未反向依赖界面） | `python3 tools/check_layering.py` |
| 图标检查 | 通过（27 个图标，声明/引用/文件三者一致） | `python3 tools/check_icons.py` |
| 规格自检 | 通过（367 条，P0 59 条，PRD 与数据同步） | `python3 tools/check_spec.py` |
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
│   ├── command.pri / log.pri / services.pri
├── Pictures/                     27 个 SVG 图标 + Pictures.qrc
├── Tests/
│   ├── CommandRegistry/          tst_commandregistry + .pro
│   └── run-tests.sh              统一测试运行器
└── ThirdParty/                   myclasspath.pri（定位 LqRibbon）、lqribbon.pri

tools/
├── spec/                         规格数据（唯一事实来源，7 个模块）
├── publish_issues.py             生成 PRD + issue 索引 + 创建 issue（幂等）
├── generate_icons.py             生成图标集
├── check_layering.py             ENG-001 依赖方向
├── check_icons.py                ENG-009 图标一致性
└── check_spec.py                 规格自检 + PRD 同步

docs/
├── PRD-actions.md                规格正文（生成物，勿手改）
├── PRD.md                        产品定位与范围
├── design/architecture.md        架构与目录清单
├── development/                  交接、并行划分、GitHub 流程
├── github/                       issue 索引与发布记录
└── research/                     两份竞品测绘 + 裁决规则
```

## 4. 下一步该做什么

按 [parallel-workstreams.md](parallel-workstreams.md) 的第一波开工。**推荐同时开 3 个对话**：

| 对话 | 工作流 | 从哪条 issue 开始 | 交付什么 |
| --- | --- | --- | --- |
| 对话 1 | **A 平台底座** | `PLAT-001`（构建系统）→ `PLAT-002`（文件系统抽象）→ `PLAT-003`（回收站） | `Services/Platform/`、`Services/Files/`，含测试 |
| 对话 2 | **B 会话框架** | `SESS-001`（会话基类）→ `SESS-002`（类型注册表）→ `SESS-006`（设置对话框框架） | `Services/Session/`、`Views/Session/`，含测试 |
| 对话 3 | **N Ribbon 深化** | `UI-017`（页面可见性）→ `UI-018`（收缩策略）→ `UI-019`~`UI-021`（复合控件） | `Views/Page/`、`Services/Command/` 的深化 |

第三个对话也可换成 **H 过滤与格式**（`FILT-001` 掩码解析器起步，纯算法、最容易写出完整测试）
或 **O 工程与文档**（`ENG-002` 模块构建守卫、`DOC-001` 用户手册）。

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
