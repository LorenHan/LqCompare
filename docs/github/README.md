# GitHub 发布记录与流程

## 1. 发布结果

| 核对项 | 结果 |
| --- | --- |
| 规格条目 / 已发布 issue | 369 个条目，全部已发布（见 [prd-issues.json](prd-issues.json)） |
| 逐条链接索引 | [issue-index.md](issue-index.md) |
| 状态标签 | 全部条目为「需求 + 待实现」 |
| 模块标签 | 27 个功能域各一 |
| 优先级标签 | P0 59 条，其余 P1 |

发布映射保存在 [prd-issues.json](prd-issues.json)：每条规格记录其 issue 号、链接、
功能域与优先级。该文件是只读核对结果，也是并行开发时「哪个条目对应哪个 issue」的查询入口。

## 2. 发布方式

```bash
python3 tools/publish_issues.py prd        # 重新生成 docs/PRD-actions.md 与索引
python3 tools/publish_issues.py labels     # 创建/更新标签
python3 tools/publish_issues.py issues     # 创建缺失的 issue
python3 tools/publish_issues.py all        # 全流程
python3 tools/publish_issues.py all --dry-run   # 只打印，不改远端
```

三条设计约束：

1. **幂等**：按标题里的 `[ACTION-ID]` 匹配已有 issue，已存在的不会重复创建。
   因此命令可以反复运行，用于补发新增条目。
2. **单一出口**：issue 正文由 `tools/spec/` 的数据渲染，
   所以「改规格」和「改 issue」是同一个动作。
3. **保守并发**：默认 4 个并发，遇到限流（403/429）自动退避重试。

## 3. 标签体系

### 3.1 状态标签（需求状态机）

`需求` + 下列状态之一：

| 标签 | 含义 |
| --- | --- |
| `待实现` | 已进规格，尚未开工（默认） |
| `进行中` | 有人在做 |
| `部分完成` | 完成标准只满足了一部分 |
| `待审核` | 实现完成，等待验收 |
| `待定` | 方案或范围尚未确定 |
| `已完成` | 全部完成标准成立（由 workflow 在 issue 关闭为 completed 时写入） |
| `已取消` | 不做了（issue 以 not_planned 关闭时写入） |

状态标签的自动同步由 `.github/workflows/requirement-labels.yml` 完成，
它只改状态标签，保留模块、优先级等其它标签。

### 3.2 模块标签

与 `docs/PRD-actions.md` 的功能域一一对应，共 27 个：
界面、会话、文本比对、三方合并、文件夹比对、文件夹同步、文件夹合并、十六进制、
表格比对、图片比对、媒体比对、注册表、版本比对、压缩包、编辑视图、过滤规则、
文件格式、报表导出、补丁、快照、版本控制、命令行、脚本自动化、选项外观、
平台性能、工程质量、文档。

按模块筛选即可得到某个并行工作流的全部任务，例如：

```bash
gh issue list --repo LorenHan/LqCompare --label 文件夹比对 --state open
```

### 3.3 优先级标签

`P0`（跑通最小闭环所必需的地基，59 条）与 `P1`（其余）。
优先级策略集中定义在 `tools/publish_issues.py` 的 `P0_RANGES`，
便于整体调整而不是逐条修改。

## 4. 关闭与重开

- 完成：关闭为 completed，或提交/PR 中写 `Closes #号码`。
- 取消：关闭为 not_planned。
- 仅关联：写 `#号码`，不要用关闭关键词。

## 5. 提新需求

新需求必须先成为规格条目：

1. 在 `tools/spec/` 的对应模块里加一条（填齐 id / module / title / entry / criteria / ref）。
2. 运行 `python3 tools/publish_issues.py prd` 更新规格书与索引。
3. 运行 `python3 tools/publish_issues.py issues` 补发 issue。
4. `python3 tools/check_spec.py` 必须通过。

直接改 `docs/PRD-actions.md` 是无效的——它是生成物，`check_spec.py` 会让 CI 失败。
