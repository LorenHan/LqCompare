# 全局选项交付记录

本轮独占 `Code/Services/Settings`、`Code/Views/Options`、`Code/Tests/Options*`。
未修改 App、Ribbon、Services/Session、其他会话模块、全局规格和总测试脚本；未提交或推送。

## 已实现

- **OPT-001 部分**：可使用的 `OptionsDialog`，含常规、显示、日志、存储分类树；搜索设置名称、说明、key并定位高亮；每项有作用与默认值 tooltip；确定、取消、应用；切换分类或导入导出前处理未应用草稿。
- **OPT-002 部分**：`general.singleInstance`（默认 true，明确下次启动生效）和 `general.lastSessionAction`（home/exit）。两项的存储与 UI 已完成，启动及关闭行为由主协调在 App 接线，本文不将未验证的 App 集成算成完成。
- **OPT-004 部分**：系统默认/浅色/深色 Qt 调色板、独立界面字体与内容字体、外观主题导入导出。`OptionsRuntime` 实际应用 QApplication 的字体与调色板，并通过 `contentFontChanged` 向内容视图传递等宽字体设置；Ribbon 显式字体/配色与 TextPane 字体由 App 集成。
- **OPT-010 部分**：真实日志级别、文件输出开关、日志文件路径与打开当前日志目录。空路径使用当前配置目录的 `logs/lqcompare.log`；日志文件启用默认 true，默认级别 warning。文件路径失败可被仓库预检/运行时错误信号报告。
- **OPT-012 核心闭环**：可读 JSON、标准/便携目录隔离、`lqcompare.portable` 标记探测与 forcePortable API、分类/全部重置确认与自动备份、坏文件回默认并记录告警。CLI `--portable` 的参数注册由 App 负责。
- **OPT-013 部分**：全部已实现全局设置或外观子集导出，默认排除本机日志路径；导入显示当前/传入值及冲突，逐项勾选，自动备份并原子落盘。尚无格式定义、快捷键、自定义命令或报表预设迁移。

## 集成 API

```cpp
#include "optionsrepository.h"
#include "optionsruntime.h"
#include "optionsdialog.h"

// QApplication 和应用名称/组织名设置完成后创建。三者需保持足够生命周期。
LqCompare::Settings::OptionsRepository options(
    LqCompare::Settings::StorageLocation::detect(portableFromCommandLine));
const auto loaded = options.load(); // !ok 时将 loaded.error 显示给用户；仓库已回默认
LqCompare::Options::OptionsRuntime runtime(&options, &app);
// 构造时已应用；检查 runtime.lastError()，并连接后续 runtimeError。

// 命令入口：
LqCompare::Options::OptionsDialog dialog(&options, mainWindow);
dialog.exec();
```

`Services/services.pri` 和 `Views/views.pri` 已预留 exists include；本模块提供 `settings.pri` / `options.pri`，无需再修改共享入口。独立测试须同时包含 `Services/Log/log.pri`。

- 单实例启动前读 `options.value("general.singleInstance").toBool()`。
- 关闭最后一个会话时读 `options.value("general.lastSessionAction").toString()`，home 保留 Home 页，exit 走已有可取消的窗口关闭流程。
- 内容视图创建时用 `runtime.contentFont()`；已打开视图连接 `OptionsRuntime::contentFontChanged`，更新相应 TextPane 的字体和依赖字体的行号栏/制表符宽度。
- Ribbon 构造时设置了显式字体，所以需要在 `display.uiFont*` 变化后 `ribbonBar()->setFont(QApplication::font())`；Ribbon 主题也需要映射其自有样式枚举。
- `OptionsRuntime::runtimeError` 报告实际应用失败；初始化失败读 `lastError()`。CLI 的本次日志覆盖放在 runtime 构造之后应用。
- 仓库 `changed(QStringList)` 仅在成功保存后按实际变化发出。UI 只提交自身改动的 key，并合并仓库外部更新的未编辑字段。

## 存储约定

当前文件为 `options.json`，格式标识 `LqCompare.Options`，整数版本 1，`settings` 对象采用扁平 key。版本 0 兼容 `values` 对象；缺少的新项补出厂默认。已知字段使用严格 JSON 类型校验，错误整份回默认，避免部分加载导致难以发现的行为差异。

未知字段在读取与普通保存中原样保留，但不进入 UI、不导出、不导入。未来版本不覆盖；必须由用户完整重置并备份后才可写入，分类重置不允许绕过保护。全局仓库不保存比较规则/会话类型默认值，未增加第五层设置覆盖。

写入使用 `QSaveFile` 且禁用直接写回退。导入/重置先保留现有文件原字节；首个备份为 `options.json.bak`，再次备份使用唯一后缀，不覆盖旧备份。备份失败则操作失败；导入或保存失败不更改仓库内存值与 changed 信号。导入预览的 SHA-256 指纹用于防止确认期间源文件被替换，UI 提交时会校验。

默认导出只遍历允许的定义，不包含未知字段、凭据、最近路径、窗口位置；只有显式包含本机设置才导出日志路径。日志目标不得指向配置或配置备份，导出不得覆盖活动仓库和当前日志。

标准路径使用 Windows/macOS 的 `AppDataLocation`（漫游数据/Application Support）和 Linux 的 `AppConfigLocation`（XDG）；便携始终使用程序目录 `config/`。Windows 真机路径行为尚未验收。

## 验证与未完成

独立测试工程：`Code/Tests/Options/OptionsTests.pro` 与 `Code/Tests/OptionsDialog/OptionsDialogTests.pro`。仓库 **45 passed**、UI/runtime **16 passed**，均为 **0 failed、0 skipped**，含各套件 QtTest 初始化/结束；命令与执行边界见 `team-options-tests.md`、`team-options-ui.md`。仓库 suite 仅链接 QtCore/Test，UI suite 使用 QtWidgets offscreen；构建并行度为 2。`python3 tools/check_layering.py` 已通过，Services 不依赖 Views/App。

主负责人已通过 `view_image` 检查 `/tmp/lqcompare-options-ui-tests/options-general.png`、`options-display.png`、`options-storage.png`：分类、搜索、编辑器和底部按钮可读无遮挡，长内容保留滚动条。运行时测试实际读取日志文件确认内容已写入，确认系统调色板/字体恢复、内容字体通知、日志失败回退原位置，以及仅修改显示项时保留 CLI 日志覆盖。截图不等同于完整 App 或 Windows 真机验收。

未完成：其余选项分类；启动恢复/指定工作区/自启动/Windows关联开关；差异和语法颜色、图标方案；运行中跟随操作系统主题；日志轮转、清空日志、诊断包与单独性能开关；格式/快捷键/命令/报表预设迁移；多语言、网络和远程连接。没有将这些能力呈现为可用开关。完整 App 集成及 Windows 真机 UI 由主协调后续验收。
