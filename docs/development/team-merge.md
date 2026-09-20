# MRG 团队交付记录

日期：2026-09-20。范围：`Code/Services/Merge`、`Code/Views/Merge`、`Code/Tests/Merge*`。未改 App、Session、Text、规格或 GitHub 状态，未提交/推送。编译遵从团队更新：独立目录，`make -j2`，仅运行自己的套件。

## 集成接口

```cpp
#include "textmergesession.h"
using namespace LqCompare;
auto *session = new TextMergeSession(basePath, leftPath, rightPath, outputPath, parent);
QString error;
if (!session->open(&error)) { /* 显示 error */ }
QWidget *view = session->createWidget(parentWidget);
```

参数顺序严格为 **base、left、right、output**，与 CLI 的位置参数顺序不同，CLI 装配须显式转换。`typeId()` 为 `text-merge`。空 `basePath` 为明确的两路降级；已指定却打不开的祖先不会默默降级。左右输入必须存在且可读，输出可留空，保存时由视图选择路径。

- `resolveCurrent(Merge::Resolution::{Left,Right,LeftThenRight,RightThenLeft,Base})`、`resolveBlock(index, choice)`：块级决策。
- `markCurrentResolved(bool)`：保留内容，只改变当前冲突状态。
- `previousConflict()` / `nextConflict()`：跳过已解决冲突，到边界提示而不循环。
- `selectBlock(index)` / `currentBlock()`：选任意块；冲突列表也可定位已解决块。
- `setOutputText(text)`：编辑输出；`undo()` / `redo()`：统一历史，最多保留 200 次快照。
- `setOutputPath(path, overwrite, &error)`：显式另存为选择；同一已选路径不会借此忽略外部修改。
- `save(&error)`：普通保存严格阻止未解决冲突，不提供可能丢失候选内容的强行保存。
- `unresolvedCount()`、`outputDocument()`、`outputText()`、`mergeResult()`、`outputRanges()`：读取状态。
- `hasSavedOutput()`：实际成功写入后，且当前无脏改动/无未解决冲突，才返回 true；可供 mergetool 退出判断，CLI 退出码装配不在本模块。
- `setUseLocalShortcuts(bool)` / `usesLocalShortcuts()`：默认 true。主窗口使用 `CommandActionBinder` 时设为 false；创建视图前设置和创建后动态切换均有效。关闭时移除本地 Save 绑定、停用编辑器固定 Undo/Redo，并放过 Qt 的 `ShortcutOverride`，由全局自定义绑定统一派发；可见按钮继续可用。

`merge.pri` 仅引入本模块服务，依赖 Text 的 `.pri` 由顶层提供；`mergeview.pri` 引入本模块视图。顶层预留 exists include 自动接入，无需改共享工程。

## 已实现并验证

- 三方纯函数引擎按相对 base 的严格差异合并，复用 `Text::compare`，忽略空白/大小写/行尾设置不会削弱冲突判断。仅一侧变化自动采用，同改一份保留，不同重叠保留 base/left/right 三份并显式冲突。相邻替换、边界插入、同位置插入、传递重叠、重复行均有语料。
- 任何候选末行没有终止符而后面还有输出时，相关 EOF 块合成显式结构冲突，保留完整三源；既不悄悄加换行，也不把原来的两行拼成一个词。
- 缺祖先时，同文自动保留，不同区域一律等待决策；输出初值不放候选冲突标记，未解决时不能保存。
- 左/base/右/输出四窗格；三个来源只读，输出可编辑。可切换祖先显示，切换不丢决策。冲突/已解决/人工编辑着色，冲突列表和状态栏计数同步；导航同时定位来源与输出。
- 接受左、右、基线、先左后右、先右后左；双边选择显式排序。决策、手工编辑、标记解决/重新打开共享撤销/重做历史。
- 普通多行编辑通过行映射维护块归属，重新选择首块不会吞掉后面独立手工修改。若手工编辑把几个块压成一个区域，会保守保护该区域：需撤销跨块编辑再选源，或直接审阅并标记解决；无关块可继续决策。
- 输出继承 base 的编码/BOM（无 base 时继承左），各输入分别解码；选择的每行保留行尾。编码/行尾差异给状态提示。非法解码、二进制、超过 Text 限额的输入拒绝并说明原因。
- 输出仅写独立路径。输入路径、符号链接、硬链接别名均拒绝；原子写使用 `QSaveFile` 且禁止 direct-write fallback。记录内容 SHA256 与文件身份，检测同大小/时间外部修改、删除、替换、新建冲撞、目录软链接重定向。失败不更新保存快照，不修改输入。

## 实际验收

环境：macOS、Qt 5.15.2 clang_64、C++17。qmake 提示 Qt 5 对本机 macOS 26.5 SDK 未做官方兼容验证，但实际编译成功。

| 套件 | 独立构建目录 | 实际结果 |
| --- | --- | --- |
| `Code/Tests/Merge/MergeTests.pro` | `build/merge-engine-tests` | 45 passed，0 failed，0 skipped，619 ms；含 EOF 结构冲突、左右交换、24,389 组三文档穷举和 10,000 行预算语料 |
| `Code/Tests/MergeOutput/MergeOutputTests.pro` | `/tmp/lqcompare-mergeoutput-build` | 19 passed，0 failed，0 skipped |
| `Code/Tests/MergeView/MergeViewTests.pro` | `/tmp/lqcompare-mergeview-build` | offscreen 26 passed，0 failed，0 skipped，172 ms；包含 EOF 场景与本地/全局快捷键回归 |

三套最近验收合计 **90 passed，0 failed，0 skipped**。本次快捷键补丁只重编/运行 MergeView，服务层沿用上次已通过结果。MergeView 实际覆盖自动合并后无编辑保存、所有五种选择、未解决保存保护、手工解决、撤销/重做、导航、跨块编辑、UTF-8 BOM/混合行尾、外部覆盖拒绝、另存为输入拒绝、失败重载保留数据、无祖先、二进制、空文件编辑和真实 QWidget 键入。界面另已生成并目视核对 offscreen 四窗格截图（`LQ_MERGE_SCREENSHOT` 环境变量指定路径）。

快捷键补充验收直接编入真实 `CommandActionBinder`，用 `QTest::keySequence` 发送平台标准 Save/Undo/Redo，断言全局 handler、`mergeChanged` 次数及内容，验证每键仅执行一次。覆盖视图创建前/后禁用本地键、独立视图默认键、Undo/Redo 重绑、原键改分配给 Save、全局命令禁用后旧本地键不恢复。回归先复现了 Qt `ShortcutOverride` 吞 Undo/Redo（全局计数 0），修复后全部通过。

## 尚未验收或未实现

本轮交付是可用的 MRG 核心闭环，**不代表 MRG-001 至 MRG-018 的全部验收条件完成**。

- 未实现四窗格全篇虚拟空白行严格对齐、连续滚动映射、连接带；当前是各源原文显示和块导航同步定位。隐藏 base 为直接隐藏，尚无内嵌摘要。
- 未实现整文件/批量决策确认、只显示冲突折叠、独立 Base 差异报表、输出重新比较、决策日志导出、编辑查找替换。
- 未提供带冲突标记的未解决保存、可恢复合并会话、覆盖输入模式；当前一律拒绝未解决保存和输入覆盖。这比可选覆盖模式保守，不应将 MRG-013 全项标为完成。
- 输入编码可在 open 前用 `sessionSettings()` 的 `merge.baseEncoding` / `merge.leftEncoding` / `merge.rightEncoding` 指定，但尚无独立编码选择 UI 或输出格式控制。
- 引擎复用 Text 的有界工作量与 `alignmentLimited` 提示，尚无后台取消。打开时的阶段进度不是可取消的异步实时进度。
- App 入口、CLI 参数、Git mergetool 退出码与真实 Git 端到端验收由协调/VCS 团队集成；本模块没有改共享目录，不宣称 Git 契约已通过。
- Windows 分支（文件身份句柄与路径）未实机编译/运行，当前只做 macOS 验收。
- QSaveFile 的最终检查与原子重命名之间仍存在普通便携 API 无法消除的并发窗口；已做提交前再次核对，但不宣称操作系统级 compare-and-swap 或锁住外部写入者。
