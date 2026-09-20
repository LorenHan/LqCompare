# Registry 服务测试

本工程仅链接 QtCore / QtTest 与 `Services/Registry/registry.pri`。文件解析与
MemoryProvider 测试跨平台执行；Windows 本地读取 smoke 在非 Windows 平台明确 skip。
测试不创建、写入、导入或删除系统注册表项。

## 独立执行

从本目录执行（`qmake` 须来自 Qt 5.15.2）：

```sh
mkdir -p .build
cd .build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../RegistryTests.pro
make -j2
./bin/tst_registry -o test-results.txt,txt -o -,txt
```

Windows 使用安装环境对应的 Qt 5.15.2 qmake / make，在单独构建目录运行同一工程。
Windows smoke 仅对带随机 UUID 的 `HKCU\Software\LqCompareReadOnlyTest_<UUID>`
路径执行只读打开，断言不可读取时携带原生错误、无法被比较为相等。

## 本次真实验证

2026-09-20 在 macOS 26.6、Qt 5.15.2 x86_64 上完成上述独立构建与执行：
**61 passed / 0 failed / 1 skipped**（包含 QtTest init/cleanup）。
唯一 skip 为 `windowsReadOnlyMissingKeySmoke`；输出原因明确指出本机无法执行
Windows 原生 Unicode 注册表枚举。

qmake 提示 Qt 5.15.2 仅验证过 macOS SDK 10.15，当前编译 SDK 为 26.5；本轮实际
编译、链接、测试成功。该结果不能替代 Windows MinGW 32 位构建或 Windows 运行验证。

覆盖内容：

- UTF-16LE/BE BOM、Unicode/补充字符、Windows-1252/GB18030 显式 ANSI 解码、严格 UTF-8 BOM。
- 默认值、转义、注释、含 `=`/`;` 的值名、多行 hex、空键、删除标记及未知类型保真。
- DWORD/QWORD、hex(type)、REG_MULTI_SZ、REGEDIT4 ANSI hex 字符串转换；格式畸形的原始值仍保留字节并标记显示问题。
- 语法/编码错误的原子失败与行号，文件读取错误，以及 bytes/keys/values/depth 限额。
- 类型变化与字节变化分类、二进制长度、数组顺序/元素变化、65,536 字节值尾部变化、大小写身份及 ß/ss 不合并。
- 删除操作差异；无法表示为快照的父键删除/子键声明和相反顺序均原子拒绝。
- 内存提供者子树边界、稀疏父键补全、不可读祖先传播、原生错误保留、限额不误判完整。
- 任一侧或双方不可读时都不误判相等；已知邻近子树仍能正常报告差异。
- 非 Windows 原生服务禁用并给出错误说明。

未验证：Windows 实际成功枚举、受保护键 ACL、管理员读取行为、32 位 MinGW 构建。
未实现且本测试不声称覆盖 REG-003 写入/备份/回滚、REG-004 报表导出、REG-006 系统
导入或写入往返验收；本轮用户范围为安全只读比较。

## 固定文件语料

`fixtures/*.reg` 是真实存储于磁盘的人工合成格式语料，使用固定文本编码得到，
并非从此 macOS 开发机或 Windows 注册表导出的用户配置。测试直接调用
`readRegFile` 读取这些文件。所有文件均含 CRLF，UTF-16 文件含相应 BOM。
语料中的删除指令仅用于解析断言，不执行。

| 文件 | 编码与用途 |
| --- | --- |
| `unicode-types-utf16le.reg` | UTF-16LE；中文键值、emoji、字符串转义、所有主要类型、延续行、删除标记 |
| `unicode-utf16be.reg` | UTF-16BE；中文与 emoji 的字节序检查 |
| `ansi-windows1252.reg` | REGEDIT4 / Windows-1252；é、€、£ 的确定性解码 |
| `ansi-gb18030.reg` | REGEDIT4 / GB18030；可选 ANSI 编码 |
| `compare-left.reg` | UTF-16LE；比较左侧 |
| `compare-right.reg` | UTF-16LE；大小写身份相同，预期 9 个类型/数据/存在性/操作差异 |

SHA-256：

```text
395d1d711117aa68bb5e116169848777ac2dc291227a8797f3c810fdfb4f29f2  ansi-gb18030.reg
dd396f5211ce176551e578093392a303360bb1dfda164c42b3ce68ade1d00194  ansi-windows1252.reg
56e7c9926ba78e63b33c72ed25c401e2cfb46b092e408957bc74d299acf56733  compare-left.reg
212292c42dd506cd9c11e1dfebf03def1f2a2807cf1573904eea0afe848afa72  compare-right.reg
5590b874cad8b0224091357b9bad1c7ccb2f1ba03ac5cde30c5308141652fd61  unicode-types-utf16le.reg
cd9d2f13d456a7fe6a84340cda96aefd3cb4d033d3d65f5f986808fb3b6ecf69  unicode-utf16be.reg
```
