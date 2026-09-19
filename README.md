# LqCompare

Qt 5.15.2 / C++17 的**文件与文件夹比对工具**。功能面以 **Beyond Compare 5** 为主要依据，
比对与合并界面参考 **TortoiseGit**（Ribbon 布局），两者行为冲突处以 Beyond Compare 为准。

界面使用 **LqRibbon**（与 [Ailecium](https://github.com/LorenHan/Ailecium) 同一套 Ribbon 外壳），
代码分层为 `App → Views → Services`。

> **状态**：规格已冻结、工程骨架已跑通。功能实现按 issue 逐条推进。
> 当前界面上每个按钮都已就位，未实现者点击后会告诉你它对应哪条规格条目。

---

## 1. 文档入口

| 文档 | 内容 |
| --- | --- |
| [docs/PRD-actions.md](docs/PRD-actions.md) | **产品规格正文**。367 个条目，每条都有入口/作用对象/行为边界与可核对的完成标准 |
| [docs/github/issue-index.md](docs/github/issue-index.md) | 全部条目对应的 GitHub issue 索引（按功能域分组） |
| [docs/research/beyondcompare-features.md](docs/research/beyondcompare-features.md) | Beyond Compare 5 全功能测绘（38 个功能域、1682 个功能点） |
| [docs/research/tortoisegit-diff-features.md](docs/research/tortoisegit-diff-features.md) | TortoiseGit 比对/合并界面与客户端入口测绘（含 10 页 · 44 组 · 195 按钮的 Ribbon 建议） |
| [docs/design/architecture.md](docs/design/architecture.md) | 代码架构、分层规则与目录清单 |
| [docs/development/current-handoff.md](docs/development/current-handoff.md) | **当前进度与接手说明**（并行开发从这里开始） |
| [docs/development/parallel-workstreams.md](docs/development/parallel-workstreams.md) | 并行开发工作流划分：谁能改哪些文件、依赖顺序 |

## 2. 构建

构建基线与 Ailecium 一致，便于两个项目共享经验：

| 项目 | 取值 |
| --- | --- |
| Qt | 5.15.2（`error()` 强制，不接受其它版本） |
| C++ | C++17 |
| 构建 | qmake（按模块以 `.pri` 组织） |
| 交付目标 | Windows MinGW 8.1.0 32 位 |
| 开发/测试 | macOS clang_64、Linux |

**前置依赖**：LqRibbon 位于 MyClass 仓库。把两个仓库 clone 到同级目录即可自动找到；
否则设置环境变量 `LQCOMPARE_MYCLASS_ROOT` 指向 MyClass 仓库根目录。

```
Work/
├── LqCompare/     # 本仓库
└── MyClass/       # 提供 3rd-party/LqRibbon
```

```bash
# 构建
mkdir -p ../build && cd ../build
~/Qt/5.15.2/clang_64/bin/qmake ../LqCompare/Code/LqCompare.pro
make -j8
```

产物落在 `dist/<平台>/`，该目录**不被 git 跟踪**——任何 checkout 都不应该把用户手上的
可执行文件打回旧构建。

## 3. 测试

```bash
# 全部套件（默认用 offscreen 平台，无显示器也能跑）
Code/Tests/run-tests.sh

# 只跑名字匹配的套件
Code/Tests/run-tests.sh CommandRegistry
```

仓库级检查（CI 也跑这三条）：

```bash
python3 tools/check_layering.py   # App -> Views -> Services 依赖方向（ENG-001）
python3 tools/check_icons.py      # 图标声明、引用、文件三者一致（ENG-009）
python3 tools/check_spec.py       # 规格数据合法且 PRD 与数据同步（DOC-005）
```

## 4. 规格即 issue

`tools/spec/` 是唯一事实来源：`docs/PRD-actions.md` 与 GitHub issue 都由它生成。

```bash
python3 tools/publish_issues.py prd        # 只重新生成规格书
python3 tools/publish_issues.py labels     # 只同步标签
python3 tools/publish_issues.py issues     # 只创建缺失的 issue（幂等）
python3 tools/publish_issues.py all        # 全流程
```

**改规格请改 `tools/spec/` 里的数据，不要改 `docs/PRD-actions.md`**——它是生成物，
手改会在下次生成时被冲掉，`tools/check_spec.py` 会直接报错。

## 5. 工作方式

- **Issue-first**：先有规格条目，再实现，提交消息包含 issue 号。
- **一个条目一个 commit**：小步提交，便于单独回退。
- **破坏性操作必须可逆或明确告知不可逆**：删除默认走回收站，覆盖前先备份，
  批量操作前强制预演与确认。
- **每个提交都要能独立编译验证**。

## 6. 许可证与第三方

本仓库不含 GPL/AGPL 组件。Qt 以动态链接方式使用（LGPLv3），
完整第三方清单与许可全文见应用内 Help → About → Third-Party Licenses。

---

## 附录：功能域与条目数

| 功能域 | 条目数 | 前缀 |
| --- | --- | --- |
| 界面 | 35 | UI |
| 会话 | 20 | SESS |
| 文本比对 | 40 | TXT |
| 三方合并 | 18 | MRG |
| 文件夹比对 | 38 | DIR |
| 文件夹同步 | 12 | SYNC |
| 文件夹合并 | 10 | FMG |
| 十六进制 | 10 | HEX |
| 表格比对 | 12 | DATA |
| 图片比对 | 10 | IMG |
| 媒体比对 | 6 | MED |
| 注册表 | 6 | REG |
| 版本比对 | 5 | VER |
| 压缩包 | 8 | ARC |
| 编辑视图 | 5 | EDV |
| 过滤规则 | 12 | FILT |
| 文件格式 | 12 | FMT |
| 报表导出 | 12 | RPT |
| 补丁 | 6 | PAT |
| 快照 | 6 | SNAP |
| 版本控制 | 19 | VCS |
| 命令行 | 12 | CLI |
| 脚本自动化 | 10 | SCR |
| 选项外观 | 14 | OPT |
| 平台性能 | 10 | PLAT |
| 工程质量 | 13 | ENG |
| 文档 | 6 | DOC |
| **合计** | **367** | 27 个前缀 |

完整的逐条说明见 [docs/PRD-actions.md](docs/PRD-actions.md)。
