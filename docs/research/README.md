# 竞品功能测绘

本目录保存 LqCompare 的历史功能测绘与可追溯审计。请先读
[2026-10-02 官方手册与修改记录审计](feature-parity-audit-2026-10-02.md)：
旧测绘存在版本、工具提示及功能归属误判，不能把原始条目数量当作已认证覆盖率。
规格与实现继续逐项核验，历史 ID 保留用于追溯。

| 文档 | 对标对象 | 规模 | 用途 |
| --- | --- | --- | --- |
| [feature-parity-audit-2026-10-02.md](feature-parity-audit-2026-10-02.md) | 官方 BC5 / TortoiseGit 手册及修改记录 | 92 组行为映射到现有 369 条规格 | **最新勘误、缺口与验收入口**，不是功能已完成声明 |
| [beyondcompare-features.md](beyondcompare-features.md) | Beyond Compare 5（Scooter Software） | 38 个功能域 / 1682 个功能点 | **功能面的主要依据** |
| [tortoisegit-diff-features.md](tortoisegit-diff-features.md) | TortoiseGit 比对/合并界面与客户端 | 1166 个功能点 | Ribbon 布局与交互参考 |
| [reference-projects.md](reference-projects.md) | kdiff3 / WinMerge / meld 等开源实现 | 4 个实现 | 算法、数据结构与测试方法的借鉴（**含许可证红线**） |

前两份回答「要做什么」，第三份回答「别人怎么做的」。注意第三份里的一条硬约束：
参考实现全部是 GPL，**只能读不能抄**，细节见该文档第 1 节。

## 两者的关系与裁决规则

- **功能面以 Beyond Compare 为准**。两个软件行为冲突时（例如「文件夹比对谁作为主视图」、
  「同步的删除策略默认值」），采用 Beyond Compare 的行为。
- **沿用当前 Ribbon 架构，按任务上下文呈现紧凑控件**。多页面是本项目的设计选择，
  不是竞品功能事实；优先保证差异区空间、工具提示、键盘操作和可访问性。

## 已知的重要结论（决策依据）

1. **TortoiseGit 内置工具不支持文件夹比对**（官方原文明示）。因此信息架构必须反过来：
   **以文件夹比对为主视图，文件比对是它的子视图**。
2. **TortoiseGitMerge 有工具提示**，见[官方按钮说明](https://tortoisegit.org/docs/tortoisegitmerge/tmerge-dug-toolbar.html)。
   XML 未出现某属性不能证明运行时没有提示。LqCompare 的两段式 tooltip 是自身标准（UI-023）。
3. **TortoiseGit 的 Log / Commit / Revision Graph 都不是 Ribbon**，是经典 Win32 对话框。
   它有很强的比对资产（日志图列 + 分支徽标语义色、修订图可导出 SVG/PNG、Blame 按龄连续着色、
   图像 XOR 差异），但没 Ribbon 化——这是 LqCompare 的差异化空间。
4. **TortoiseGitMerge 把 File 菜单整体挪进 Application Menu**，导致 Open/Save 藏在圆形按钮后。
   LqCompare 把这些高频命令留在 Home 页第一组，Application Menu 只放低频项。

## 一份功能点 ≠ 一个 issue

测绘文档追求「不漏」，因此粒度很细（1682 + 1166 个点）。规格书把它们收敛为
**369 个可独立实现与验收的条目**：一个条目内部可以包含若干紧密相关的功能点
（例如「编码探测与手动指定编码」），否则 issue 会碎到无法评审。

收敛过程保留在每个条目的「竞品对标出处」字段里，可逐条回溯到测绘文档的原始条目。
