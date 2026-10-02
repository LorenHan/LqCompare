# 变更日志

本文件按版本记录新增、变更、修复与破坏性变更。每条目附带 ACTION-ID 与 issue 号，
格式由 `tools/check_spec.py` 之外的评审检查项保证（见 DOC-005）。

## [Unreleased] — 2026-10-02

此节记录开发分支成果，不代表已合并、打包或完成三平台发布验收。

### 安全边界

- Windows 回收站后端在可恢复性未验证前拒绝删除，移除可能退化为永久删除的 Shell 调用；
  同步删除明确失败并保留原件，不自动永久删除。系统回收站入口仍可自行打开，
  本应用自动删除/恢复暂不可用（PLAT-003 / [#324](https://github.com/LorenHan/LqCompare/issues/324)）

### 新增

- 文件夹时间比较开关、默认 2 秒容差、可排序时间差及忽略时间预设
  （DIR-005 / [#114](https://github.com/LorenHan/LqCompare/issues/114)，提交 `641bbd5`）
- 官方手册和修改记录对标审计，明确已知缺口与后续验收
  （DOC-006 / [#351](https://github.com/LorenHan/LqCompare/issues/351)，提交 `877789c`）

### 变更

- 研究入口区分历史测绘库存和已核验来源，不以条目数推导覆盖率
  （DOC-006 / [#351](https://github.com/LorenHan/LqCompare/issues/351)）
- 审计提交 `877789c` 的标题误写 `DOC-005 / #357`，正确归属为 `DOC-006 / #351`；
  此处纠正关联，不改写已推送历史（DOC-005 / [#350](https://github.com/LorenHan/LqCompare/issues/350)）

### 修复

- Ribbon 标签改为跟随应用调色板的直角状态，修复原生窗口框架下白字不可读，
  保留选择、悬停及键盘焦点提示，不改变页面/命令与比较区域布局
  （UI-001 / [#2](https://github.com/LorenHan/LqCompare/issues/2)）

- 测试工程发现的非法 shell 展开和空目录错误归因
  （ENG-003 / [#335](https://github.com/LorenHan/LqCompare/issues/335)，提交 `af723cf`）
- 旧 Qt 与强化编译下的 POSIX 符号链接读取崩溃
  （PLAT-002 / [#325](https://github.com/LorenHan/LqCompare/issues/325)，提交 `ca66cde`）
- 关闭标签后立即退出的延迟删除回调 use-after-free
  （SESS-018 / [#52](https://github.com/LorenHan/LqCompare/issues/52)，提交 `0f89075`）
- Ribbon 启动默认页面错误停在 Help
  （UI-001 / [#2](https://github.com/LorenHan/LqCompare/issues/2)，提交 `9d97753`）
- MinGW 文件标识的 DWORD 数值重载和 QDir 直接依赖
  （PLAT-001 / [#322](https://github.com/LorenHan/LqCompare/issues/322)，提交 `23fb56c`）

### 破坏性变更

- 无。时间比较新增字段在会话中持久化，时间关系与内容是否相同仍独立。

### 验证边界

- `641bbd5` 冻结源码：Linux 71 套、3725 passed / 0 failed / 2 skipped；
  SingleInstance 另行排除，其本地 socket 路径受云环境限制
- 三平台 CI 按对应 SHA 单独核对。CI 未获得私有 LqRibbon 时，主程序构建、打包及
  AppIntegration / CommandActions 明确跳过，不能称为完整产品验收
- 完整测试、截图和平台边界见 [本轮质量记录](docs/development/linux-quality-2026-10-02.md)

## [0.1.0] — 2026-09-20

规格冻结与工程骨架落地。**本版本不含功能实现**：界面骨架完整，行为按 issue 逐条补齐。

### 新增

- **产品规格**：369 个条目，覆盖 27 个功能域，全部发布为 GitHub issue（DOC-005 / `docs/PRD-actions.md`）
- **竞品测绘**：Beyond Compare 5（38 个功能域 / 1682 个功能点）与 TortoiseGit（1166 个功能点）两份对标文档（DOC-006 / `docs/research/`）
- **工程骨架**：Qt 5.15.2 + C++17 + qmake，`App → Views → Services` 分层，首次 clone 即可编译运行（ENG-001、ENG-002、PLAT-001）
- **Ribbon 界面**：10 个页面 / 45 个分组 / 169 个按钮，由声明表驱动构建；未实现的命令点击后显示其 ACTION-ID（UI-007 ~ UI-024）
- **Ribbon 外壳**：Office 2016 Blue 样式、快速访问工具栏、居中命令搜索栏、可最小化、自有的 Ribbon 右键菜单（UI-001 ~ UI-006）
- **会话容器**：Home 入口页（按类别列出会话类型、最近会话）与会话标签容器（SESS-003、SESS-010）
- **命令注册中心**：全量命令的注册、查询、执行与启动自检，是 Ribbon/菜单/QAT/快捷键/搜索栏的唯一出口（UI-024）
- **分级日志**：级别未启用时参数不求值，支持控制台与文件两个目标（ENG-006）
- **测试基础设施**：Qt Test 套件 + 统一运行器（offscreen 平台可跑），首个套件 `CommandRegistryTests` 14 条用例（ENG-003）
- **仓库级护栏**：分层依赖检查、图标一致性检查、规格数据自检，CI 每次提交执行（ENG-001、ENG-009、DOC-005）
- **图标体系**：27 个 SVG，由 `tools/generate_icons.py` 生成，统一描边与配色（UI-025）
- **Issue 工作流**：`需求` 状态机标签、`requirement.yml` 模板、关闭时自动同步状态标签的 workflow
- **文档**：README、产品说明、架构文档、当前进度与接手说明、并行开发工作流划分、GitHub 发布与流程

### 变更

- 无（首个版本）

### 修复

- 无（首个版本）

### 破坏性变更

- 无（首个版本）

### 已验证

| 项目 | 结果 |
| --- | --- |
| macOS 构建 | qmake + make 通过，产出 `dist/macos/LqCompare.app` |
| 离屏启动 | 通过，日志「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 |
| 命令注册表自检 | 0 问题（不缺图标、不缺说明、无快捷键冲突） |
| 测试 | 14 passed / 0 failed |
| 分层 / 图标 / 规格检查 | 全部通过 |

### 未验证

- Windows MinGW 8.1.0 32 位构建（本机无该环境，由 CI 或 Windows 开发机验证）
- 国际化：`.ts` 尚未填充，运行时语言切换（UI-030）未实现
