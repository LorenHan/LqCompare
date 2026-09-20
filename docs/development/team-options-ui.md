# 全局选项界面与运行时交付（2026-09-20）

本子任务仅修改 `Code/Views/Options`、`Code/Tests/OptionsDialog` 和本文档。设置仓库由选项主协调实现；未改 App、Ribbon、Services/Session、全局规格或其它团队文件。

## 已实现

- `OptionsDialog`：常规、显示与外观、日志与诊断、存储与迁移四个真实分类；关键字搜索定位和高亮、默认值 tooltip、脏分类加粗、确定/取消/应用。切换分类、关闭、导入、导出和重置遇到未应用改动时提供应用/放弃/返回三种选择。校验或保存失败时保留草稿，显示可读错误。无改动的确定不写文件。
- 当前表单来自仓库声明，只提供已实现的全局选项。单实例明确标注下次启动生效；关闭最后会话后的 Home/退出行为通过仓库 API 交付主 App 接入。
- 导入预览列出当前值、传入值及冲突，逐项勾选；冲突项默认不勾选。使用预览指纹执行原子导入，预览后文件变化则拒绝导入。空选择不写配置。重置分类/全部均先确认并由仓库自动备份，成功显示备份路径。
- 导出支持全部全局选项或显示主题，本机日志路径默认排除，可显式选择包含。全部设置文件读写调用 `OptionsRepository`，界面不自行读写配置。
- 两个同时打开的选项窗口共享仓库时，只提交相对于基线的实际编辑项；外部已保存的未编辑项同步刷新，避免旧表单覆盖外部改动。
- `OptionsRuntime`：构造时应用持久化设置，监听仓库变化立即更新 `QApplication` 的调色板、界面字体及字号；内容字体独立保存为 `QFont` 并发信号。日志级别、文件启停、路径均调用真实日志服务。空日志路径使用当前配置目录 `logs/lqcompare.log`。打开失败尝试恢复旧日志输出位置并提供 `runtimeError` 与 `lastError()`。
- 运行时按变更分类更新：仅改显示选项不会覆盖 App 在启动时设置的 `--log-level`、`--no-log-file`；修改日志组选项时应用完整日志组。

## App 集成接口

构建包含 `Services/Settings/settings.pri`、既有 `Services/Log/log.pri`、`Views/Options/options.pri`。

```cpp
using namespace LqCompare;
Settings::OptionsRepository repository(storageLocation);
repository.load();
Options::OptionsRuntime runtime(&repository, app);
// 构造时应用可能已经失败，因此同时读取 runtime.lastError() 并连接后续信号。
QObject::connect(&runtime, &Options::OptionsRuntime::runtimeError, errorReceiver);
// 比较视图初始化使用 runtime.contentFont()；存续期间连接 contentFontChanged。
Options::OptionsDialog dialog(&repository, mainWindow);
dialog.exec();
```

`OptionsRuntime::applyCurrent()` 可显式重新应用全部组；启动时的 CLI 日志覆盖应放在 Runtime 构造之后。一个 QApplication 只建立一个长生命周期 Runtime，在创建时采样系统默认字体和调色板。

App 仍需连接内容字体至实际内容视图、连接日志错误展示、读取 `general.singleInstance` 决定实例策略、把 `general.lastSessionAction` 接入会话关闭流程；这些文件不属于本子任务独占范围。Runtime 不依赖具体比较视图。

## 已验证

使用 Qt 5.15.2、C++17、macOS 的独立构建目录 `/tmp/lqcompare-options-ui-tests`，qmake 后 `make -j2`，运行 `QT_QPA_PLATFORM=offscreen ./tst_optionsdialog -o test-results.txt,txt`。

结果：**16 passed，0 failed，0 skipped**（含 QtTest 初始化/清理和截图用例）。覆盖真实控件改值与持久化、字号系统值跳过非法范围、切分类应用/放弃/返回、取消关闭、搜索定位/高亮、校验失败保留表单、多窗口合并、重置确认/精确备份/分类边界、逐项导入/冲突/主题导出、预览后源文件变化拒绝、导出前草稿处理、实际 QApplication 字体和调色板更新、实际日志文件写入/禁用、CLI 覆盖不受外观修改影响、无改动确定不写文件、日志运行时打开失败恢复旧位置。

真实对话框截图保存在上述构建目录 `options-general.png`、`options-display.png`、`options-storage.png`；已经视觉检查，布局可读、控件无重叠。Qt offscreen 的 `propagateSizeHints` 提示和 Qt 5 对新 macOS SDK 的兼容提示存在；未出现编译错误或测试失败。

## 未完成与边界

没有伪装支持：启动恢复会话/指定工作区、系统自启动、文件关联、多语言、远程连接及凭据、差异与语法颜色自定义、图标样式、日志轮转/清空/诊断包/独立性能计时、格式/快捷键/自定义命令/报表预设迁移均尚未实现，界面明确说明。

“系统默认”恢复启动时采样的系统调色板；未监听运行中的系统主题变化。主题通过 Qt palette 实施，App 或具体控件的硬编码 stylesheet 仍需其所有者适配。Windows 原生主题、真实文件对话框、系统打开日志目录尚未在 Windows 验证。

当前分类仅列出真正可配置的三组及存储工具，不声称完整覆盖 OPT-001/002/004/010/012/013 全部验收项。未提交、未推送，也未创建定时任务。
