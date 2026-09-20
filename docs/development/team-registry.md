# Registry 只读比较交付记录

日期：2026-09-20。独占范围：`Code/Services/Registry`、`Code/Views/Registry`、`Code/Tests/Registry*`。未修改共享 `.pri`、App、Services/Session 或 GitHub；主协调负责应用集成。

## 集成入口

- 在 `Services/services.pri` 接入：`include($$PWD/Registry/registry.pri)`，仅依赖 QtCore；Windows 分支单独加入 `registryprovider_win.cpp` 和 `-ladvapi32`。
- 在 `Views/views.pri` 接入：`include($$PWD/Registry/registryview.pri)`；依赖现有 Services/Session 和 Views/Session。
- `LqCompare::RegistryCompareSession(QObject*)` / `(const QString &left, const QString &right, QObject*)`，`typeId="registry"`；`setPaths`、`leftPath/rightPath`、`comparison`、`leftSnapshot/rightSnapshot`、`isLoaded`、`readOptions/setReadOptions`。
- `left/right` 是 `.reg` 文件路径或 `HKCU\...` / `HKEY_CURRENT_USER\...` 本地源。默认空会话不读取整个 HKCU。保存始终不可用；更换两侧、重载和更换编码按一组提交，任一侧失败保留原比较并提示旧结果仍在显示。
- 主协调应保留 SessionTypeRegistry 的 `WindowsOnly` 入口约束；独立解析服务和测试可跨平台执行，不代表承诺非 Windows 产品入口。视图内非 Windows 的 Local HKCU 按钮禁用并解释原因。

## 已实现

### 文件解析及差异

`LqCompare::Registry` 提供 `parseReg`、`readRegFile`、`compare`、`displayValue`、`Provider` 和 `MemoryProvider`。

- UTF-16LE/BE BOM、UTF-8 BOM、显式 ANSI 编码；默认 Windows-1252，可选 GB18030 等 Qt 支持的编解码器。拒绝奇数字节 UTF-16、不完整代理对、无效选定编码字节和文本 NUL。
- 识别 v5 与 REGEDIT4 标头、注释、空键、默认值、引号/反斜杠转义、DWORD/QWORD、`hex` / `hex(type)`、多行十六进制和删除指令。REGEDIT4 的 hex 字符串按 ANSI 转为 UTF-16LE；v5 保留原始 UTF-16LE 字节。
- 保留原始数值类型（包括未知类型）和数据字节。字符串/多字符串按内容显示，整数同时显示十六进制与十进制，二进制显示长度和十六进制。Windows 可存储未终止字符串，因此原始 hex 不强行修正；异常布局明确显示为 malformed/unterminated。大值只格式化有限预览，比较仍使用全部字节。
- 键和值名采用简单 UTF-16 大写身份映射，保留原拼法；数据比较大小写敏感。分类包括键/值仅左、仅右、类型不同、数据不同、删除操作不同与不可读。
- 删除指令只供显示与比较，绝不执行。为避免把顺序不同但执行效果不同的修复脚本误判相等，同一路径 create/delete 冲突、删除祖先与后代 section 共存均拒绝；此版本比较导出快照，不模拟通用 `.reg` 脚本执行。
- 解析任一错误返回空键集合及错误行；默认限制 32 MiB、100000 键、1000000 值、128 层深度。缺失 BOM 的非 ASCII 输入必须选择正确 ANSI 编码，不声称自动识别代码页。

### 只读提供者和安全边界

- Provider 接口没有写入、删除、导入或提权方法。MemoryProvider 真正筛选并遍历子树，保留权限错误、补足稀疏祖先、规范化身份并执行限额。
- Windows 使用 `RegOpenKeyExW`、`RegQueryInfoKeyW`、`RegEnumKeyExW`、`RegEnumValueW`，只请求 `KEY_READ | KEY_WOW64_64KEY`，限定本地 HKCU。显式 64 位视图避免 32 位产品构建意外读取重定向视图。
- 使用 `REG_OPTION_OPEN_LINK` 读取链接本身；缓冲区有上限及有限重试；读取前后检查键元数据变化。读取进程权限仅用 TOKEN_QUERY，不请求提升权限。管理员身份也不视为可绕过 ACL。
- 权限失败、枚举失败、资源上限及读取期间变化保留为 Key.error/nativeError；错误祖先下的条目均为 unknown，不能成为“相等”或“仅一侧存在”。差异总数不混入 unknown；单独显示不可读键数。
- 整棵实时注册表不是事务快照：局部变化检测不能保证跨键一致性。当前读取及树构建同步执行且有资源上限，未完成后台取消与大树性能验收。

### 界面

两侧源输入/文件选择、只读 HKCU 子树输入、ANSI 编码选择、键层级和默认值、两侧类型/数据、状态排序与筛选。读取失败显示明确错误并保留旧比较；不可读单列状态和总数；删除操作明确标注未执行。键自身相等仅指存在/操作相同，不等同整个子树相等。

## 验证记录

使用本机 macOS、Qt 5.15.2 clang_64、C++17；各套件独立 qmake 构建且 `make -j2`，未运行全量 runner。

- `Code/Tests/Registry/RegistryTests.pro`：**61 passed / 0 failed / 1 skipped**。六个磁盘 fixture 为人工合成的固定语料，覆盖 UTF-16LE/BE、Windows-1252、GB18030 和真实文件差异读取；没有声称它们来自 Windows 实际导出。覆盖严格编码、转义、类型/原始数据、删除冲突、失败原子性、不可读祖先、MemoryProvider 限额及非 Windows 平台边界。复现命令和语料说明见 `Code/Tests/Registry/README.md`。
- `Code/Tests/RegistryView/RegistryViewTests.pro`：**12 passed / 0 failed / 0 skipped**（含 init/cleanup）。使用磁盘临时 UTF-16/ANSI 文件和 MemoryProvider 验证空会话不枚举、从不写源文件、双侧加载/重载/编码失败回滚、HKCU 边界、只读保存状态、不可读计数、树层级、状态筛选排序、删除指令与会话销毁安全；最终显示宽度调整后复跑通过。已检查离屏截图 `Code/Tests/RegistryView/.build/registry-tree.png`，不以此代替 Windows 应用交互验收。
- 模块范围 WinAPI 静态护栏：8 个 `.h/.cpp`、0 违规；服务层依赖检查：0 个反向依赖。`git diff --check` 无错误。

原生 Windows 文件未在本机编译或运行，Windows smoke 在非 Windows 明确 skip；不能以 macOS 文件解析/内存测试代替 Windows 验证。目标平台 MinGW 8.1.0 32 位、实际 HKCU ACL、Unicode 枚举、动态缓冲区和 64 位视图仍未验证。

## 验收范围与未完成项

- REG-001/002：完成本任务要求的导出文件解析、只读提供者实现和树形比较；Windows 本地读取仍待目标平台验证，应用入口由主协调集成。
- REG-005：实现只读边界及不可读传播；无注册表写入操作，因此不实现写操作日志。
- REG-006：跨平台真实固定文件语料、内存提供者和会话测试可执行；Windows 系统枚举/ACL/Unicode/64 位视图仍待目标平台验收。没有为了满足原规格而写入测试注册表。
- REG-003 的复制、写入、备份、回滚及子树导出未实现；REG-004 的 TXT/HTML/CSV/修复 `.reg` 报表未实现。它们不属于本次安全只读模块，不应将 REG 整组标为完成。
- 尚无远程注册表、HKLM 实时入口、权限提升、32 位视图切换、后台取消或通用 `.reg` 脚本顺序解释器。

## 官方格式/API 参考

- [Microsoft：.reg 子键和值语法](https://support.microsoft.com/en-us/topic/how-to-add-modify-or-delete-registry-subkeys-and-values-by-using-a-reg-file-9c7f37cf-a5e9-e1cd-c4fa-2a26218a1a23)
- [Microsoft：Registry value types](https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types)
- [RegOpenKeyExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw)、[RegEnumValueW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew)、[RegEnumKeyExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw)、[RegQueryInfoKeyW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw)
