# 文件夹合并独立模块交付（2026-09-20）

## 交付边界

本模块实现 `folder-merge` 会话的**只读逻辑合并计划**、人工决策和文本冲突请求。它不实现文件系统应用器，不能声称 FMG-001～010 全部验收完成。`Plan::canExecute()` 永远为 `false`，界面应用按钮禁用，并解释事务写入、备份和恢复尚未实现。

仅修改 `Code/Services/FolderMerge`、`Code/Views/FolderMerge`、`Code/Tests/FolderMerge*` 和本交付文档；未修改 Folder 服务、Session 基类、共享 pri、规格或 GitHub。实际应用装配由主协调负责。

## 集成契约

主协调在总工程已有 Files、Filter、Session、Folder 服务和 CompareSession 基类的基础上增加：

```qmake
include(Services/FolderMerge/foldermerge.pri)
include(Views/FolderMerge/foldermergeview.pri)
```

两个 pri 只列本模块源码及 include 路径，不重复 include 依赖 pri。

`LqCompare::FolderMergeSession` 构造器：

```cpp
FolderMergeSession(QObject *parent = nullptr);
FolderMergeSession(const QString &base, const QString &left,
                   const QString &right, const QString &output,
                   QObject *parent = nullptr);
```

`typeId()` 为 `folder-merge`。会话提供四路径 getter、`plan()`、`isScanning()`、`setPaths()`、`rescan()`、`cancelScan()`、`setDecision()`、`resetDecision()` 和 `requestTextMerge()`。扫描通过独立工作线程运行；关闭取消扫描并阻止迟到结果更新会话。

关闭集成注意：人工计划 `isDirty()==true`，但 `canSave()==false`。已向主协调提出 `SessionArea::mayClose` 对该组合提供“丢弃/取消”而非不可用的 Save；本模块不改共享 Shell。完整关闭对话框的应用级验收由主协调负责。

信号：

```cpp
void textMergeRequested(const QString &basePath, const QString &leftPath,
                        const QString &rightPath, const QString &outputPath);
```

主协调将该信号连接到 `TextMergeSession(base,left,right,output,parent)` 的装配入口。只有三侧普通文件的未解决内容冲突可请求；符号链接、目录、删除修改、无祖先均不能冒充文本三方冲突。请求前解析输出的现存祖先与目标叶子，拒绝与输入重叠、经符号链接指向输入及无法解析的悬空链接。文本文件的编码/二进制识别由文本模块负责。请求本身不读写合并结果，也不将父计划标记为已解决；文本编辑结果的安全导入与目录应用链路尚未实现。

## 已实现行为

- `FolderMerge::buildPlan(Paths{base,left,right,output})` 复用 Folder 服务，使用三侧独立目录清单以及祖先/左、祖先/右、左/右逐字节比较。独立清单保留类型冲突目录下的子项，避免两路比较在类型边界停止后遗漏子树。工作量高于一次目录扫描，尚未做大目录性能验收。
- 基线存在时自动接受仅左改、仅右改、左右相同改动、左右相同新增、单侧新增，以及另一侧未变的删除；不同内容、删除对修改、类型变化均显式保留冲突。目录删除对后代修改属于删除修改冲突。
- 空祖先目录是有效基线；未填写祖先是两路模式。两路模式仅共同相同内容自动接受，单侧条目和不同内容保持“无祖先，无法推断”，不根据修改时间猜测历史。
- 普通自动目录行是合并容器，子项分别决策，不是把整个左/右目录复制。人工目录决策才按选择传播到子树。
- 可取左、取右、取祖先、忽略、恢复自动决策。选择缺失侧会明确归一为“删除/不输出”；删除只表示逻辑结果，当前没有文件系统删除实现。
- 目录人工决策默认遇到不同人工子项即原子拒绝；可显式选择保留或覆盖。保留导致父文件与子目录等结构不兼容时也会原子拒绝，不产生无法成立的输出树。恢复目录自动决策会恢复整个子树。
- 递归深度限制、取消、读取错误、被过滤条目、不同扫描阶段观察到状态变化均使计划不完整。不完整计划不自动推导任何结果，且人工选择删除或缺失侧会被拒绝。现阶段仅支持精确、区分大小写的相对路径配对，忽略大小写选项被显式拒绝。
- 四侧对齐的可展开目录树、相对路径、条目类型、来源与判定原因、子项冲突计数、只看未解决冲突过滤、目录子树作用范围确认，以及只读文本预演/复制。
- 人工决策置脏标记；换路径和重扫默认拒绝静默丢弃，界面重扫有明确丢弃确认。未实现计划持久化；会话保存能力保持禁用。

## 输出与只读保证

所有扫描和决策 API 均不创建、修改或删除输入或输出。输出路径只参与预演与受检查的文本子会话请求。本模块未枚举已有输出目录，预演明确注明它是逻辑归属清单，**不是已经检查过目标现状的新增/覆盖/删除操作清单**。已有输出的未知条目不会被本模块删除。

源目录比较是多轮读取而非文件系统快照；通过跨轮元数据和缺失条目复核检出常见扫描变化，但不承诺原子快照。执行能力禁用，因此不存在把过期计划直接用于写入的路径。

## 验证记录

独立 Qt 5.15.2 / C++17 / qmake 工程，构建并发 `make -j2`；未运行全量 runner。服务测试使用真实 `QTemporaryDir` 建立祖先、左、右与已有输出，比较执行前后的实际目录名和字节内容。

`Tests/FolderMerge/FolderMergeTests.pro` 已独立构建并运行：**31 passed / 0 failed / 0 skipped**（含 QtTest init/cleanup，含数据行）。日志：`Code/Tests/FolderMerge/build-local/test-results.txt`。覆盖完整三方决策表、人工子树策略、取消与深度限制、跨扫描轮次新增/移除，以及类型冲突下子文件变化而父目录时间不变的回归。

`Tests/FolderMergeView/FolderMergeViewTests.pro` 独立构建成功，使用 `QT_QPA_PLATFORM=offscreen` 运行：**18 passed / 0 failed / 0 skipped**（含 init/cleanup，16 个行为/数据行）。日志：`Code/Tests/FolderMergeView/build-local/results.txt`。覆盖四侧树/冲突过滤、真实按钮决策与恢复自动、只读输出、人工决策保护、子树策略、文本请求四路径、输出重叠与真实符号链接防护、取消、关闭后迟到回调及会话先于视图销毁。已解决行被冲突过滤隐藏时会清空当前决策目标，避免按钮继续作用于隐藏条目。

测试截图 `Code/Tests/FolderMergeView/build-local/foldermerge-view.png` 已渲染并视觉核查，目录容器、子项来源、冲突提示、执行禁用及判定原因显示完整。截图是独立会话视图，不是全应用入口已验收的证据。两套构建产物均在 `build-local/`，由现有 `build-*/` 忽略规则排除。

复现命令（在对应 `build-local` 目录分别执行）：

```sh
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../FolderMergeTests.pro
make -j2
./bin/tst_foldermerge
```

```sh
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../FolderMergeViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./bin/tst_foldermergeview
```

独立审查用临时 qmake harness 复现过最后一个竞态：补丁前快照1字节/实际27字节却 `complete=true`；补丁后同一条件 `complete=false`，明确报告变化并禁删。已将该行为加入正式服务测试。

本机为 macOS，使用 `/Users/loren/Qt/5.15.2/clang_64/bin/qmake`。Qt 输出 SDK 26.5 高于其已测试 SDK 的提示；这不改变实际构建与测试结果。Windows MinGW 8.1 32 位和 Linux 未编译、未运行，不标为已验收。视图测试的三个真实符号链接用例在 Windows 明确 QSKIP，须通过有符号链接权限的专项环境另行验证；本次 macOS 无跳过。文本请求的路径防护只保证发送时的状态，下游实际保存仍须复检外部变化。

## 尚未完成的规格验收

| 规格 | 本次覆盖 | 未完成部分 |
| --- | --- | --- |
| FMG-001 | 四侧树计划、无基线提示、人工决策脏状态 | 全应用入口由协调装配；输出持久化/外部修改状态 |
| FMG-002 | 左/右/祖先/忽略、恢复自动、说明 | 重命名保留两份、完整撤销栈、全局批量入口 |
| FMG-003 | 子树传播、保留/覆盖人工决策与冲突拒绝 | 更完整目录角标产品验收 |
| FMG-004 | 类型/内容/删除修改冲突、父级计数、过滤和统计 | 独立冲突清单面板 |
| FMG-005 | 明确禁用应用 | 全部真实落盘模式、确认/取消/失败报告 |
| FMG-006 | 逻辑输出树与只读文本清单 | 实际应用共用操作清单、目标变化检测与文件导出 |
| FMG-007 | 目录作用范围数量/隐藏项提示、保留人工计划 | 选中集合/过滤集合/全部冲突批量与原子撤销 |
| FMG-008 | 可复制文本逻辑预演 | 完整审计字段、时间、HTML/CSV/XML 报表 |
| FMG-009 | 执行彻底禁用 | 写入前检查、可验证备份、恢复、回收站撤销 |
| FMG-010 | 真实临时目录计划、决策与只读回归 | Windows 运行、预览与实际应用一致性、落盘恢复测试 |
