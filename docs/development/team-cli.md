# CLI 服务交付记录

工作目录：`/Users/loren/Desktop/Work/LqCompare`。所有变更限定在 `Code/Services/Cli`、`Code/Tests/Cli*` 及本记录；脚本另见 `team-script.md`。未修改 App/main.cpp、顶层 .pri、规格数据或其他团队模块，未提交或推送。

## 主入口集成

`clioptions.h` 与 `cliexecution.h` 是 QtCore 服务接口，`services.pri` 已预留的 exists include 会自动发现 `Cli/cli.pri`。

```cpp
const auto parsed = LqCompare::Cli::parse(argumentsWithoutExecutable);
if (!parsed.ok()) {
    // 按 UTF-8 打印 Cli::errorResult(Cli::UsageError, parsed.error) 的输出。
    return LqCompare::Cli::UsageError;
}
const auto &request = parsed.request;
if (LqCompare::Cli::requiresHeadless(request)) {
    const auto result = request.scriptFile.isEmpty()
        ? LqCompare::Cli::execute(request, applicationVersion)
        : LqCompare::Script::executeFile(request, applicationVersion);
    // standardOutput -> stdout，standardError -> stderr，均固定 UTF-8。
    return result.exitCode;
}
// GUI 装配 request.left/right/base/output/sessionType、只读位、textOptions 等。
```

无界面分支应使用 `QCoreApplication`，不要先构造 `QApplication` 再解析，否则 Linux 会提前加载显示平台插件。`parse()` 接收去掉可执行文件名的 argv，shell 已处理外层引号，解析器不再次剥掉文件名中真实的引号。它不检查文件存在性、不读写配置。

`--new-instance` 与 `--wait` 仅在 Request 中承载，实例转发及等待由 App/Platform 装配。`--no-log-file` 保留旧入口兼容性；未显式传 `--log` 时本服务不写日志，应用可按既有日志策略提供默认日志，但须保证不写入比较源。

## 已实现行为

- 原生 0/1/2/3/4 路径解析，3 个按左/右/祖先，4 个再加输出；命名 `--left/right/base/output` 明确绑定，不与位置参数混用，不猜 Git 参数顺序。
- `--` 终止开关与 `@script` 解析；POSIX `/silent` 等当作路径，Windows 接受已知 `/fv`、`/silent`、`/qc`、`/leftreadonly`、`/rightreadonly`、`/readonly`。`/?` 在各平台显示帮助。
- 自动生成分组帮助、单参数帮助、版本、退出码表，以及从会话注册表生成的类型清单。显式类型可使用 ID、英文名、中文名或常用别名，并校验路径个数及平台限制。未指定的 GUI 类型留给会话装配自动判定。
- 重复参数、空值、缺值、互斥规则、未知类型和未知参数明确报错；未知开关给出编辑距离最近的候选。
- 无界面比较限两个文本文件或两个目录。文本调用 Text::Document/Text::compare，支持忽略大小写/全部空白/行尾/最后换行和显式编码；二进制或有损解码拒绝比较，避免乱码被判为相同。
- 目录调用 Folder::compare，默认递归与逐内容比较。错误、取消、深度受限或不确定结果不能返回 0。`--no-recursive` 只比较直接子项，未深入的同名目录在 JSON 标为 `not-compared`。目录模式收到尚未实现的文本规则时明确返回 2，不静默忽略规则。
- 静默服务只读取输入，永远不会把第 4 路径当作无提示保存目的地。合并和其他专用类型的无界面执行明确返回 2，GUI 仍可使用解析得到的全部路径和类型。
- UTF-8 稳定 TSV 摘要及 JSON 输出。TSV 路径中的反斜线、制表符和换行有转义。JSON 成功字段：`status/type/left/right/differences/ignored/exitCode`，文本附加行数和 `alignmentLimited`，目录附加 `entries` 与 `recursive`。`differences` 在文本中计差异块，在目录中计有差异条目（含父目录汇总节点），不是统一的文件数量。错误字段 `status=error/exitCode/error`。
- `--report=txt|csv|json|html --report-file=...` 生成比较摘要，支持仅有差异时输出。HTML 转义来源路径且无远程资源，CSV 引号/换行和公式开头安全处理。这是摘要报表子集，未冒充完整 Report 引擎。
- 显式日志和报告原子写入，拒绝覆盖任一输入、向输入目录内部写入、通过符号链接别名写输入，以及 POSIX 硬链接别名。日志与报告目标冲突也拒绝。`execute(..., protectedPaths)` 和 `writeReport(..., protectedPaths)` 支持脚本保护本体及所有先后声明的输入。
- `--script`、`@file`、重复但变量名各异的 `--script-arg name=value`、`--continue-on-error` 解析；执行入口位于 Script 模块。

## 稳定返回码

| 数值 | 比较含义 |
| --- | --- |
| 0 | 比较完成，无差异 |
| 1 | 比较完成，有差异 |
| 2 | 参数、用途或当前尚未实现的执行模式错误 |
| 3 | 无法读取/写入源或输出，或无法可靠完成比较 |
| 4 | 捕获到内部执行异常 |

比较返回 1 是正常结果，`ExecutionResult::ok()` 对 0 和 1 都为真。合并工具返回码文案遵循修订后的契约：0 已解决且成功保存，1 未解决或用户取消；本服务不执行静默合并。

## 验证

- `python3 tools/check_layering.py` 已通过：Services 没有依赖 Views/App。
- Qt 5.15.2 / C++17：全新独立构建目录中的 CLI QtTest 已通过 **108 passed / 0 failed / 0 skipped**。包含 QtCore 真实子进程的比较返回码、parser→Script::executeFile→比较→JSON 报告闭环，以及脚本运行错误行号。Script 最后的 FIFO/反斜杠边界修补后增量重编译复测仍为 108/0/0（457 ms）。结果保存于 `.build-cli-selfcontained/test-results.txt`。
- `Code/Tests/Cli/CliTests.pro` 自足构建：测试二进制以独立环境标志启动自身，子进程只运行 CLI 入口，不进入 QtTest。干净环境无需预先构建另一个程序。可选独立 probe 位于 `Code/Tests/CliProbe/Standalone/CliProbe.pro`，第三层目录使现有测试调度器不会将其误认为 QtTest 套件。

```sh
mkdir -p build-cli-tests
cd build-cli-tests
~/Qt/5.15.2/clang_64/bin/qmake ../Code/Tests/Cli/CliTests.pro
make -j2
./bin/tst_cli
```

## 尚未完成

- App/main.cpp 的正式分支接线、GUI 中类型/规则/只读的端到端装配由主协调负责；本记录不宣称它们已经集成。
- GUI 只读模型的所有写入路径、实例转发/等待、自动默认日志、Windows 系统命令行实际进程均未由此模块验收。
- Windows wildcard 展开尚未实现；不能认为 CLI-010 全部完成。解析器的 Windows 斜线开关语义已跨平台可测，但 Windows 实机仍需测试。
- 完整比较过滤掩码/排除、忽略注释、对齐/容差、语言切换与本地化帮助未实现；未在帮助表中承诺支持这些参数。
- 完整布局/范围报表、报表路径模板、Report 服务适配及静默合并未实现；当前报告只提供明确标识的摘要。
