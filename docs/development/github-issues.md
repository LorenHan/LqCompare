# Issue 工作流规则

> 面向开发者的操作说明。标签体系与发布流程见 [../github/README.md](../github/README.md)。

## 1. 基本循环

```
读规格条目 → 认领 issue（打「进行中」）→ 实现 + 测试 → 提交（带 ACTION-ID 与 issue 号）
→ 更新完成标准勾选 → 打「待审核」→ 验收通过 → 关闭 issue（自动置「已完成」）
```

## 2. 认领

```bash
# 找某个工作流的任务
gh issue list --repo LorenHan/LqCompare --label 平台性能 --state open

# 按优先级找地基任务
gh issue list --repo LorenHan/LqCompare --label P0 --state open

# 看某条规格条目对应哪个 issue
gh issue list --repo LorenHan/LqCompare --search "PLAT-002 in:title"
```

认领后把 `待实现` 换成 `进行中`：

```bash
gh issue edit <号> --repo LorenHan/LqCompare --remove-label 待实现 --add-label 进行中
```

## 3. 提交消息

格式固定，便于从 git 历史反查规格：

```
<动作>：<一句话说明>（<ACTION-ID> / #<issue 号>）
```

动作词汇表（与 `CHANGELOG.md` 的分节对应）：

| 动作 | 用途 |
| --- | --- |
| `新增` | 新功能 |
| `变更` | 行为调整 |
| `修复` | 缺陷修复 |
| `重构` | 结构改动，行为不变 |
| `文档` | 只改文档 |
| `测试` | 只改测试 |
| `构建` | 只改构建配置 |
| `工具` | 只改 `tools/` 下的脚本 |

示例：

```
新增：目录枚举的并行扫描与取消（PLAT-002 / #42）
修复：图标校验漏判 icon("x.svg") 写法导致护栏失效（ENG-009 / #118）
文档：补充并行工作流划分与共享文件约束（DOC-003 / #301）
```

**一个条目一个 commit。** 一个提交只做一件事，便于单独回退，也让 `git log` 可读。

唯一的例外是**基线导入**：工程骨架、规格数据与工具链在第一个可用版本之前一次性落地，
按交付物类型分成几个提交（构建 / 工具 / 文档 / CI），每条提交消息同时列出它覆盖的
ACTION-ID。基线之后，任何改动都必须回到「一个条目一个 commit」。

## 4. 完成标准的勾选

issue 正文里的完成标准是复选框。实现过程中逐条勾选，全部勾上才算完成：

```bash
gh issue edit <号> --repo LorenHan/LqCompare --body-file <(gh issue view <号> --json body -q .body | sed 's/- \[ \] 第 2 条/- [x] 第 2 条/')
```

更简单的做法是直接在网页上勾选。

**如果某条完成标准实际不成立**（例如平台限制），不要勾选，也不要删掉它：
在 issue 评论里说明原因，把它改写成「已明确不做，理由：…」并同步修改
`tools/spec/` 里的规格数据。规格与实现必须一致，否则下一个人会以为它是 bug。

## 5. 关闭

- 实现完成并验收通过：关闭（reason: completed），workflow 自动打「已完成」。
- 决定不做：关闭（reason: not_planned），workflow 自动打「已取消」。
  同时在 `tools/spec/` 里把该条目标注为不做，并重新生成 PRD。
- 提交/PR 里写 `Closes #号码` 可自动关闭；只想关联就只写 `#号码`。

## 6. 发现新问题

发现规格没覆盖的问题时：

1. 先确认不是既有条目的一部分（用 `gh issue list --search` 搜一下）。
2. 是缺陷 → 用 Bug 模板开 issue，打 `bug` + 模块标签。
3. 是新需求 → **先加规格条目**，再补 issue（见 [../github/README.md](../github/README.md) §5）。

## 7. 常见错误

| 错误 | 后果 |
| --- | --- |
| 手改 `docs/PRD-actions.md` | 下次生成时被冲掉，CI 报错 |
| 提交消息不带 ACTION-ID | 无法从 git 历史反查规格，变更日志无法自动汇总 |
| 一个提交做多个条目 | 无法单独回退；出问题时只能整体回滚 |
| 关闭 issue 时只写「做完了」 | 无法核对完成标准是否真的成立 |
| 完成标准不成立却直接删掉 | 规格与实现脱节，下一个人按规格实现时才发现对不上 |
