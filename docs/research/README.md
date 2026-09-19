# 竞品功能测绘

本目录是 LqCompare 的**对标基线**：两份文档把两个竞品的功能面穷尽式测绘成
「一个可独立验收的功能点」，供规格书（`docs/PRD-actions.md`）与 issue 拆分使用。

| 文档 | 对标对象 | 规模 | 用途 |
| --- | --- | --- | --- |
| [beyondcompare-features.md](beyondcompare-features.md) | Beyond Compare 5（Scooter Software） | 38 个功能域 / 1682 个功能点 | **功能面的主要依据** |
| [tortoisegit-diff-features.md](tortoisegit-diff-features.md) | TortoiseGit 比对/合并界面与客户端 | 1166 个功能点 | Ribbon 布局与交互参考 |

## 两者的关系与裁决规则

- **功能面以 Beyond Compare 为准**。两个软件行为冲突时（例如「文件夹比对谁作为主视图」、
  「同步的删除策略默认值」），采用 Beyond Compare 的行为。
- **界面形态以 Ribbon 为准**。TortoiseGitMerge 确实是 Ribbon 实现，但它只有 **1 个 Tab**，
  靠 `ScalingPolicy` 依次收缩塞下 30 多个控件，社区还专门开过 issue 反对；
  因此 LqCompare 采用**多页面**方案（10 页 / 45 组），只借鉴它的分组素材。

## 已知的重要结论（决策依据）

1. **TortoiseGit 内置工具不支持文件夹比对**（官方原文明示）。因此信息架构必须反过来：
   **以文件夹比对为主视图，文件比对是它的子视图**。
2. **TortoiseGitMerge 的 Ribbon 没有任何 tooltip**（`TortoiseGitMergeRibbon.xml` 里
   `Command.TooltipTitle/Description` 全缺）。LqCompare 把「两段式 tooltip」做成启动自检项（UI-023）。
3. **TortoiseGit 的 Log / Commit / Revision Graph 都不是 Ribbon**，是经典 Win32 对话框。
   它有很强的比对资产（日志图列 + 分支徽标语义色、修订图可导出 SVG/PNG、Blame 按龄连续着色、
   图像 XOR 差异），但没 Ribbon 化——这是 LqCompare 的差异化空间。
4. **TortoiseGitMerge 把 File 菜单整体挪进 Application Menu**，导致 Open/Save 藏在圆形按钮后。
   LqCompare 把这些高频命令留在 Home 页第一组，Application Menu 只放低频项。

## 一份功能点 ≠ 一个 issue

测绘文档追求「不漏」，因此粒度很细（1682 + 1166 个点）。规格书把它们收敛为
**367 个可独立实现与验收的条目**：一个条目内部可以包含若干紧密相关的功能点
（例如「编码探测与手动指定编码」），否则 issue 会碎到无法评审。

收敛过程保留在每个条目的「竞品对标出处」字段里，可逐条回溯到测绘文档的原始条目。
