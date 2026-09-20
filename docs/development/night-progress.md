# 夜间开发进度（2026-09-20）

用户授权：检查全部 issue 与产品设计，修改/取消/新增不合理需求，并持续完成剩余功能。顺序已确认：文本与文件夹比较的真实可用闭环优先，再推进其它模块。

## 当前分工

- 主协调：会话容器、主窗口、路径打开、会话保存/最近列表、命令行、整体验证。
- `issue_audit`：369 条规格与 GitHub issue 全量审查、修正 tools/spec 并生成 PRD、审查报告。独占规格文件。
- `text_compare`：Services/Text、Views/Text、Tests/Text*。
- `folder_compare`：Services/Folder、Views/Folder、Tests/Folder*。

共享构建已用 exists include 预留模块，代理不得改同一共享文件。

## 起始状态

- GitHub 369 issue 全部 open；SESS-006 挂“已完成”但仍 open；多数尚未实现。
- 用户已有未提交：Services/Platform/platform.pri、instanceprotocol.{h,cpp}、singleinstance.{h,cpp}、Tests/SingleInstance/。保留，不擅自归入新提交。
- 已有基础服务大量测试；主窗口大多数命令及全部比较视图仍为占位。
- 原始 issue 快照在本地 `.codex-work/issues-all.json`，不加入版本库。
- 自动续跑：当前任务 heartbeat `lqcompare`，每小时，在用户醒来或要求停止时停用；最迟北京时间 2026-09-21 10:00 汇总停用。

## 验收原则

服务层可用不等于完整 issue 完成；逐条记录实测证据。macOS 实测不能声称 Windows / Linux 真机验证。未完成的范围明确保留，不通过删验收标准冒充完成。

## 当前批次（2026-09-21 更新：夜间批次已收尾）

夜间这一批 24 个工作流**已整体落地**，逐条交付记录见同目录的 `team-*.md`。
两份收尾说明：

- **两条被额度中断的工作流已补完**（2026-09-21，由「接手收尾」的一轮完成）：
  - **报表与补丁**：`patch.pri` 当时已声明 `patchapply.cpp` 而该文件不存在，
    因此 `services.pri` 一 include 它、**整个应用工程就编译不过**；本轮补出
    `patchapply.cpp` 与 `Tests/PatchApply`。详见 `team-report-patch.md` 的「续做」小节。
  - **Git 差异集成**：`blameview.cpp` 是编译不过的半成品（写到一半就断了）且没接进
    `vcsview.pri`；本轮补齐并接通，`Tests/VcsBlameView` 跑绿。详见 `team-vcs.md`。
- **另外修掉两个中断残留的缺陷**：`homepage.cpp` / `hexcompareview.cpp` 各一条构建告警
  （本仓代码现在 0 warning）；5 个命令引用的 Ribbon 图标文件根本不存在
  （`ribbon_compare` / `merge` / `report` / `copy` / `shortcuts`），已按既有视觉语言补齐并登记进
  `Pictures.qrc`（27 → 32），`Tests/AppIntegration` 由「11 通过 / 1 失败」转为全通过。

**收尾状态**：全量测试 **3308 passed / 0 failed / 2 skipped（63 个套件）**，
五道护栏全过，主程序构建本仓代码 0 warning 且离屏启动无 `qt.svg` 报错。

**仍未覆盖的范围**：各 `team-*.md` 末尾的「尚未完成 / 不宣称验收的条目」仍然有效；
**Windows 与 Linux 的薄层仍未在目标平台编译或运行过**，对应完成标准一律不得勾选。
下一步的优先级见 `current-handoff.md` §4.0。
