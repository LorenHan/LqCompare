# 全局选项仓库测试交付

日期：2026-09-20。范围：`Code/Tests/Options`；不修改服务层、界面、App、共享构建文件或全局规格。

## 已实现和验证

新增 `OptionsTests.pro` 与 `tst_options.cpp`。工程显式去掉 QtGui，只使用 QtCore、QtTest 和 Settings/Log 服务模块，证明全局选项仓库可以无界面测试。

当前结果：**45 passed，0 failed，0 skipped**（含 QtTest 初始化/结束两项），Qt 5.15.2 / C++17 / macOS 26.6，独立目录 `LqCompare-build-options-tests`，每次构建使用 `make -j2`。最终增量编译没有编译器警告或错误。

覆盖行为：

- 缺失配置回默认且不主动创建配置文件；Unicode、引号、反斜杠、独立界面/内容字体和绝对日志路径写入后重新读取一致。
- 未知键、错误类型、非法枚举、字号边界、相对日志路径：整个 apply 拒绝，内存、磁盘、变更信号均不部分提交。
- 损坏 JSON、数组根、错误格式、字符串版本号、错误 settings 类型、错误已知值：回退出厂默认、产生警告与日志、`.bak` 保留输入的精确字节。
- version 0 `values` 迁移并补默认，下一次写入变成 version 1；未来版本文件保留不动，普通 apply 和分类 reset 拒绝，完整 reset 先备份再恢复可写。
- 标准目录与便携 `config/` 分别存储、读取和重置；显式便携参数与 `lqcompare.portable` 标记检测。
- 未知存储键（含嵌套对象）随普通编辑保留，但不进入导出；未知导入键在 preview 的 `ignoredKeys` 列出。
- 默认导出排除 `logging.filePath`，显式包含机器信息时可导出；导入往返保持非机器设置并保留目标机器日志路径；外观子集只包含 display 分类。
- 导入预览标明相同/冲突及前后值；逐项选择只覆盖所选键，导入前精确备份并记录日志；无效输入和不存在的所选键不改变状态。
- 带 fingerprint 的导入在预览后源文件被删除、损坏或替换成另一份合法 JSON 时拒绝，不产生备份或 changed 信号；未变化文件可成功导入。
- 分类 reset 只恢复对应分类，全量 reset 回默认；未知分类拒绝；已有 `.bak` 与连续操作的备份互不覆盖，各自保存当时原文件的完整字节。
- 真实文件系统失败：配置目录被普通文件占据、配置文件路径被目录占据时拒绝写入，不提交内存，不发 changed；备份失败时 import/reset 停止。
- 日志目标指向当前配置、配置备份、目录或普通文件下的子路径时，apply 与 import 一致拒绝，原设置不变。
- `logging.filePath` 留空时，真实默认 `logs/lqcompare.log` 若通过符号链接指向配置文件或配置备份，load 与 apply 都拒绝，保留配置与原有备份字节，避免默认路径绕过别名校验。
- 导出目标为当前活动日志或其符号链接别名时拒绝；不追加错误日志、不替换链接，日志和设置都逐字保留。

## 构建与运行

```sh
mkdir -p /Users/loren/Desktop/Work/LqCompare-build-options-tests
cd /Users/loren/Desktop/Work/LqCompare-build-options-tests
/Users/loren/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/Options/OptionsTests.pro
make -j2
./bin/tst_options -o options-tests.txt,txt
```

测试输出位于独立构建目录的 `options-tests.txt`，未把构建产物放入源码目录。首次 qmake 对当前 macOS SDK 26.5 给出 Qt 5.15 支持范围提醒；实际编译与运行均通过。

## 本轮发现并复验的修复

目录占据配置文件路径时，macOS 的 `QFile::copy` 曾返回成功并产生伪 `.bak`，使 load 错误地声称已备份。服务负责人在 `backupCurrent()` 中加入普通文件检查，回归用例验证该情况返回失败、没有 backupPath、保留目录并阻止覆盖。

## 未验证和范围外

- 尚未在 Windows MinGW 8.1.0 或 Linux 运行本套件。
- `QFile::link` 在 Windows 创建快捷方式，默认日志符号链接专用数据行在 Windows 显式跳过；macOS 上两行均已实际运行通过。活动日志直接路径保护在所有平台都会测试。
- 不声称覆盖进程崩溃/掉电、磁盘写到一半耗尽等故障注入；这里验证校验失败、备份失败及真实路径 I/O 失败下的原子状态。
- 未测试选项对话框交互、主题/字体/日志运行时效果、App 单实例和关闭最后会话的接入。这些由 Options UI 与主协调完成。
- OPT-014 的视图 > 会话 > 类型默认 > 出厂默认解析属于既有 Session/SettingsScope 测试，不在全局仓库中新建比较设置层。
- 多语言、远程网络/凭据、未落地的其他选项分类不属于本套件覆盖范围。
