# Script 服务交付记录

本轮独占 `Code/Services/Script`、`Code/Tests/Script*`。实现不依赖 Views、App、Report 或 Sync，也不调用 Shell。

## 接入

- `Code/Services/Script/script.pri` 注册源码；依赖 Cli、Text、Folder、Files、Session/Filter。
- `#include "scriptengine.h"` 后调用 `LqCompare::Script::executeFile(cliRequest, applicationVersion)`，返回 `Cli::ExecutionResult`，由调用方把 `standardOutput`、`standardError` 写到对应通道并返回 `exitCode`。
- `Script::parse(source, scriptFile, defaults, version)` 提供完整预检结果：`diagnostics` 含行号、命令、说明；`program.protectedPaths` 收集所有已声明输入和脚本自身。
- `Script::commands()` 是命令定义表；`Script::helpText()` 从该表生成语法帮助。
- 本服务未修改 `App/main.cpp`、共享 .pri、规格或主测试调度文件。主协调负责入口接入。

## 已实现行为

脚本为 UTF-8 文本，接受 BOM、LF/CRLF、空行、单双引号；`#` 在引号外且位于参数开头时引入注释，文件名内的 `#` 保留。读取拒绝无效 UTF-8（包括文件尾被截断的多字节序列），文件上限 16 MiB。打开前要求脚本为普通文件，允许符号链接指向普通文件；目录、FIFO、设备等返回数据错误 3，避免等待特殊文件输入。

反斜杠仅在紧邻可启闭的引号时承担转义：连续 `2n` 个反斜杠产生 `n` 个字面反斜杠并启闭引号，连续 `2n+1` 个产生 `n` 个字面反斜杠和一个字面引号。单双引号采用同一规则；引号内部的另一种引号原样保留。其他反斜杠均不转义，因此 UNC 前缀的两个反斜杠、普通 Windows 路径和 `\n` 等文本保持原样。带空格且以反斜杠结尾的目录，在闭引号前把尾部反斜杠写成双份：

```text
load "C:\space dir\\" "\\server\space share\\"
# 上面两侧实际值分别以一个反斜杠结尾；UNC 前缀仍有两个反斜杠。
print "a \"quoted\" value"
print 'a \'quoted\' value'
```

命令支持：

| 命令 | 用法与结果 |
| --- | --- |
| load | `load [CLI 比较选项] left right` 或完整 `--left/--right`；加载两侧声明，不写输入。 |
| compare | `compare [CLI 比较选项] [left right]`；无路径时使用最近 load，选项延续并允许后续覆盖。 |
| report | `report json result.json`（txt/csv/json/html）；也接受 `--report=json --report-file=result.json --report-on-diff-only`。仅导出当前输入最近一次成功比较的摘要。 |
| set | `set name=value` 或 `set name "value"`；供后续 `${name}` 引用。 |
| print | 输出 `PRINT` 记录；JSON 模式保存在 `messages`。 |
| log | 输出 `LOG` 记录，并进入调用者显式指定的 `--log` 文件。 |

所有比较选项由 `Cli::parse()` 校验，选项身份和参数数量复用 `Cli::options()`。支持类型 text/folder、忽略大小写/空白/行尾/最终换行、exact-eol、编码、非递归目录比较、只读声明与静默/快速比较。脚本命令内不支持 GUI、嵌套脚本和其他 runner 选项，均明确报错。

相对输入、脚本内报告路径按照脚本所在目录解析；CLI 传入的 `--log` 仍按调用者当前目录解释。`${name}` 支持 `--script-arg` 传值和前面 set 定义值；内置 `${script-dir}`、`${cwd}`、`${version}`、`${timestamp}`。`$$` 和 `%%` 产生字面量 `$` 和 `%`。变量值展开后始终保持在原参数内，不会产生额外命令或参数。

`set empty=`、`set empty ""` 或 CLI 的 `--script-arg empty=` 可以定义空字符串；空值可以输出或与其他文本拼接。把空值单独用作输入路径会得到参数错误，脚本不会执行。变量替换发生在引号解析后，传入的变量值中的引号和反斜杠不需要再做脚本转义。

执行前检查整份脚本的语法、命令名、变量和 report 顺序，一次性报告全部静态错误；存在任何静态错误时不执行命令，不写报告或日志。所有声明输入（包括未来的 load/compare）、输入目录子路径、脚本文件均受写入保护；规范化路径、符号链接和 CLI 提供的文件身份检查不能绕过保护。日志还保护所有报告目的地，防止结束日志覆盖刚产生的报表。

运行时错误默认停止；`continueOnError` 继续并累积错误。退出码沿用 CLI-004：0 无差异，1 有差异，2 参数/使用错误，3 读写错误，4 内部错误；最终取所有执行步骤中的最大值。差异不是执行错误。失败 compare 会使旧比较结果失效，之后 report 不会导出过时结果。

普通输出为制表符字段记录；错误带脚本路径、行号、命令与错误码；JSON 模式输出单一有效 JSON 对象，包括步骤、比较摘要、消息、执行计数和最终退出码。显式执行日志采用 UTF-8 原子写入，记录时间、行号、命令、状态与耗时；未指定 `--log` 不写文件，`noLogFile` 会禁用日志文件。审计记录不自动抄录变量值与原始命令参数；用户主动 `print/log` 的文本按原意输出。

## 实用示例

`job.lqs`：

```text
# 文件均相对本脚本所在目录
set reportName=result.json
load --ignore-case "left file.txt" "right file.txt"
compare
report json "${reportName}"
print "finished with ${version}"
```

调用：

```sh
LqCompare --script=job.lqs --log=execution.log
LqCompare @job.lqs --json --continue-on-error
```

## 已验证

macOS / Qt 5.15.2 / C++17，纯 QtCore 与 QtTest，无 GUI：

```sh
mkdir -p Code/Tests/Script/build-script-tests
cd Code/Tests/Script/build-script-tests
~/Qt/5.15.2/clang_64/bin/qmake ../ScriptTests.pro
make -j2
./bin/tst_script
```

结果：**112 passed, 0 failed, 0 skipped**（含 init/cleanup），真实临时目录和文件。覆盖：BOM/CRLF/Unicode/引号/注释、静态错误聚合且零执行、变量及转义、相对路径、比较规则与 caller defaults 继承、exact-eol 覆盖、四种报告、差异条件报告、目录输入不变、默认停止与继续错误、严重退出码合并、失败后旧结果失效、报表与日志输入保护、未来输入、符号链接别名、日志与报表冲突、JSON 单对象、无效与截断 UTF-8、嵌套脚本拒绝、POSIX 绝对路径消歧、noLogFile。

最终边界增补还覆盖单双引号内外 1～6 个连续反斜杠的奇偶规则、带空格与尾部反斜杠的 Windows/UNC 路径参数、原始反斜杠保真、变量传值中的引号和反斜杠不二次解析、三种空变量定义的输出/拼接及空路径静态拒绝、目录与 FIFO 立即返回 3、普通脚本符号链接可执行。Windows 风格参数的纯解析在本机验证；Windows 文件系统实际打开仍需该平台验收。

## 未完成

这次交付是 SCR 的实用子集，不等同于全部 SCR-001～010 完成。select、copy/move/delete/rename/mkdir、sync/mirror、applypatch 和 if/goto/loop/foreach 等控制流均明确拒绝；未实现会话文件加载、上次结果动态变量、命令级 on-error、重试、显式设置退出码、日志追加/轮转/细分级别、调度锁、脚本录制或操作计划。报表为 CLI 摘要，未接完整 Report 布局。Windows 平台未编译运行；App 入口及真实发布程序端到端验证由主协调完成。
