# 命令状态、统一快捷键与 Ribbon 交付记录

负责范围：`Code/Services/Command`、`Code/Views/Page`、`Code/Tests/Command*`。
后续主协调授权补充 `Code/Tests/SessionType/tst_sessiontype.cpp` 中的 Home 旧护栏，以及独立 `Code/Tests/HomeRegistry` 行为测试。
本模块没有修改 App、RibbonWindow、Views/Shell 或全局规格，没有提交或推送共享工作区。

## 已实现

- 保留 Command 原有八字段 aggregate initializer；新成员只追加，默认不限制已实现命令。
- 运行期 `enabled / visible / checkable / checked`，禁用原因，会话类型限制与可选状态查询回调。
- `trigger()` 执行前后统一刷新所有状态。未知、无 handler、禁用命令返回 false；布局隐藏不禁止其他入口执行。
- 状态改变只对有效状态确实变化的命令发送 `commandChanged(id)`；整轮刷新先更新所有状态再发通知。
- 默认单快捷键加 `additionalShortcuts`，自定义多绑定、明确解绑、单条及全部恢复、整批交换。
- 快捷键保存前对全表验证：未知命令、非法序列、相同绑定、同一命令重复绑定及短序列与长序列前缀冲突均拒绝；失败不修改运行期绑定。
- QSettings 中的 `commands/shortcuts` 保存 version=1 JSON，序列采用 PortableText。加载损坏/未知版本/未知 ID/冲突配置会失败并保留运行期；缺键恢复默认。写失败报告错误并恢复 QSettings 中原键的缓存值，未宣称跨进程磁盘事务。
- 每顶层窗口一个 `CommandActionBinder`，为所有命令注册独立 QShortcut；Ribbon、菜单、QAT 可各自创建动作但不重复安装快捷键。
- Ribbon 未注册/无 handler 的占位动作禁用并显示原因、ACTION-ID；动态注册后动作自动接通。
- 快捷键设置对话框提供按键捕获、添加/替换/删除多绑定、冲突双方高亮、阻止冲突保存、单条/全部恢复、取消草稿和保存后生效。

## 主协调集成点

### 动态启用和会话范围

```cpp
auto &commands = LqCompare::CommandRegistry::instance();
commands.setCurrentSessionType(currentSessionType); // Home 用空串
commands.setEnabled("file.save", canSave, tr("当前会话没有可保存的内容"));
commands.setVisible("merge.resolve", showInRibbon);
commands.setChecked("view.output", outputDock->isVisible());
commands.updateEnabled(); // 会话选择/只读/脏状态变化后统一刷新回调
```

`Command::sessionTypes` 为空表示跨会话，否则须匹配 `currentSessionType()`。
基础 enabled 与 `enabledWhen()`、会话限制共同生效。基础 visible 与 `visibleWhen()` 共同生效。
`checkedWhen()` 或 `setChecked()` 提供真实业务勾选状态；执行器不自动假定一次触发一定完成切换。
`setChecked()` 仅接受声明 `checkable=true` 的命令。
状态回调在 `add()` 时就会求值，必须允许构造期间的空会话，并应只查询、不改注册表。

### QAT、菜单与信号

```cpp
#include "commandactionbinder.h"

auto *binder = LqCompare::CommandActionBinder::forWindow(this);
QAction *save = binder->createAction("file.save", qat);
QAction *special = binder->createAction(
    "merge.resolve", qat, QString(), QString(),
    LqCompare::CommandActionBinder::Toolbar, false);
QAction *menuItem = binder->createAction(
    "file.save", menu, QString(), QString(), LqCompare::CommandActionBinder::Menu);
```

末尾 `followVisibility=false` 让用户主动加入 QAT 的命令不随 Ribbon 布局隐藏；启用条件仍完全一致。
绑定器订阅 `commandChanged(id)`、`commandAdded(id)`、`shortcutsChanged(id)`、`registryReset()`，所有动作刷新文本、图标、提示、可见性、启用与勾选。
无需再给动作连接业务槽或调用 `setShortcut()`。菜单显示首个绑定，tooltip 和 `commandShortcuts` 属性包含全部有效绑定。
搜索结果应通过 `find(id)->enabled` 显示可用性，并用 `trigger(id)` 执行；显示快捷键应读 `effectiveShortcuts(id)`，不要读声明中的默认 `shortcut`。

### 快捷键对话框与启动恢复

```cpp
#include "shortcutsettingsdialog.h"

QSettings settings;
QStringList errors;
commands.loadShortcuts(settings, &errors); // 全部命令注册完成后；错误交给应用统一报告

// tools.shortcuts 的 handler：
LqCompare::ShortcutSettingsDialog dialog(settings, commands, this);
dialog.exec();
```

对话框及 QSettings 引用应保持覆盖 `exec()` 的生命周期。应用可选择现有用户配置目录中的 QSettings；模块不会偷偷创建另一份全局配置。
`applyShortcutOverrides()` 接收完整的覆盖字典；缺失条目表示默认，存在但空列表表示明确解绑。
恢复默认也走全表冲突验证；若另一个自定义命令占用默认键，应先一并解决冲突。

## 验证

- Qt 5.15.2 / C++17 / macOS，独立构建目录，`make -j2`。
- CommandRegistry：25 passed，0 failed，0 skipped，含原有全部测试。
- CommandShortcuts：45 passed，0 failed，0 skipped。
- CommandActions：18 passed，0 failed，0 skipped。包含真实键盘事件的单次派发、隐藏可用/禁用不可用、动态重绑旧键失效、动态注册、真实按键捕获、多绑定配置往返、冲突双方高亮/阻止保存、取消与恢复默认、存储失败完整回退。
- 本模块合计 88 passed，0 failed，0 skipped。三个构建目录依次为 `/tmp/lqcompare-command-registry-20260920`、`/tmp/lqcompare-command-shortcuts-20260920`、`/tmp/lqcompare-command-actions-20260920`。
- 其余 16 个 git tracked 既有测试工程已全量重新构建并执行，首轮唯一失败是 HomePage 已改用注册表派生类型、SessionType 仍解析旧硬编码表。随后依主协调追加授权更新此护栏并复跑 SessionType：59 passed，0 failed，0 skipped；16 套合计修正为 964 passed，0 failed，1 skipped（PathName 既有平台限定项）。首轮日志和复验 `SessionType/recheck.txt` 位于 `/tmp/lqcompare-command-regression-20260920/`。
- `python3 tools/check_layering.py` 通过，Services 没有 Widgets、Views 或 App 依赖。

## Home 注册目录补充验证

主协调授权处理 Home 页改造造成的旧测试失配后，已将 SessionType 中“提取视图硬编码类型 ID 再比较”的校验改为窄范围的共享目录来源检查，保留原有测试槽名以兼容过滤器。反向用例会检测删除目录调用、把 ID/显示名改回字面量，以及只在注释或字符串中提到目录等失效情形。SessionType 仍只链接 QtCore。

新增 `Code/Tests/HomeRegistry` 直接编译真实 HomePage、会话类型目录和掩码依赖，不引入 App 或整个 Shell。它验证所有注册类型在真实布局中恰有一个按钮、顺序和分组一致、显示名一致；逐类型反复启用/禁用后，只有启用的点击发送一次正确稳定 ID。另安装专用翻译器改变目录文案，证明新建 Home 的标题来自当前目录，移除翻译器后恢复原文案。没有宣称已存在的 Home 支持即时换语言。

SessionType 复验 **59 passed**；HomeRegistry **5 passed**，均无失败、无跳过。HomeRegistry 构建目录为 `/tmp/lqcompare-home-registry-20260920`。此次未改 MainWindow、HomePage 或其他共享实现。

## 范围外与未验证

- App 的 tools.shortcuts 注册、启动读取用户配置、菜单/搜索使用绑定器以及会话能力刷新由主协调接入；本模块已发送调用方式。主协调已确认 QAT binder/createAction 接入。
- 主协调已确认所有 Text 会话关闭本地快捷键并统一走 binder。其他视图若自行注册同键仍须避免窗口级歧义。
- 原生输入框通过 Qt ShortcutOverride 保留复制、撤销等局部编辑键；统一窗口快捷键不会强制抢占这些编辑操作。
- 注册表无法辨别一个非空 handler 是真实业务还是只打印“规格待实现”；主协调应移除占位 handler 或显式禁用其命令。
- Windows MinGW 编译与原生键盘/菜单行为尚未在 Windows 上运行。
