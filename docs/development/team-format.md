# Format 交付记录（2026-09-20）

## 独占范围与构建

本路修改 `Code/Services/Format`、`Code/Tests/Format`。过滤控件由本路子代理负责，记录在 `team-filterview.md`。主协调后续明确授权修复新 UI 暴露的 `Services/Filter/filterstack.cpp::combinedExpression()`，并调整 `Tests/FilterStack` 最小回归（见下文）。没有修改 App、全局规格或其他团队的目录，没有提交或推送共享工作区。

`Services/Format/format.pri` 已由顶层 `services.pri` 的 exists include 自动接入。独立消费者需同时 include `Filter/filter.pri` 与 `Session/session.pri`；Format 只依赖 QtCore，不创建视图、不执行转换器。

```sh
mkdir -p build-format-night-test
cd build-format-night-test
~/Qt/5.15.2/clang_64/bin/qmake ../Code/Tests/Format/FormatTests.pro
make -j2
./bin/tst_format -o format-results.txt,txt
```

## 可用入口

```cpp
#include "formatdetector.h"

LqCompare::Format::FormatDetector detector;
LqCompare::Format::DetectionOptions options;
auto result = detector.detectFiles(leftPath, rightPath, sessionTypes, options);
if (result.canOpen()) {
    // 在 App / Views 层使用 result.sessionTypeId 调用已有工厂。
    // result.explanation 适合状态栏和日志；diagnostics 应呈现到错误/输出面板。
}
```

`detectFiles()` 的空路径代表没有提供该侧。一侧文件可识别；两侧都空不打开；目录使用 folder 类型；目录和文件混合不打开；任何已提供文件的读取失败都不会被掩码掩盖。

`detect(FileProbe, FileProbe, registry, options)` 可直接接受内存样本。`contentAvailable=true` 时路径可以为空，仍参与内容探测。默认构造的 FileProbe 不参与判定。

`setDefinitions()` 整体校验后替换规则，失败保留原表。实际优先级为：

1. `options.overrides` 中首个有效且匹配的会话关联覆盖。
2. 格式定义列表中首个掩码匹配项，任一侧命中即可；格式优先级先于左右侧顺序。
3. 没有掩码命中时，首个内容签名匹配项。
4. 未知格式兜底：Automatic 按内容选 text / hex；也可显式选择 Text / Hex / Picture / Ask。

返回值分别保留格式 ID、原请求类型、实际类型、命中来源、规则和左右侧。已命中掩码不会被内容静默改写；签名冲突和文本规则遇二进制会产生诊断。

实际类型必须同时具备 registry 工厂且当前平台可用。图片、表格、归档等类型缺少实现时，文本内容降到 text，二进制内容降到 hex；二进制没有 hex 可用时不改用文本。text 不可用而 hex 可用时可显示原始字节。全程不调用工厂。`canOpen()` 表示存在可用候选，后续完整解码和视图自己的文件大小限制仍需由打开流程处理。

## 内容探测与定义数据

- 默认只读前 64 KiB，可配置 1 字节至 1 MiB，多读 1 字节判断截断；普通文件之外不读取。
- UTF-8 完整校验，含过长编码、代理码点、非法高位范围；截断前缀的合法未完序列不误报。
- UTF-16LE/BE、UTF-32LE/BE BOM 和代理对检查；有效 Unicode 中的 NUL 仍按二进制提示。
- NUL、非法解码或高比例控制字符建议 hex；空文件和有效文本选 text。只看前缀时说明采样范围，无法保证文件余下内容有效。
- 41 个内置格式，覆盖 FMT-010 的 18 类常见文本/表格名称规则，以及常见图片、归档、媒体、补丁、注册表和可执行文件。声明顺序唯一，不放 `*` 抢走未知内容。
- Windows 原生路径在进入掩码服务前按当前平台转换分隔符；POSIX 合法反斜杠文件名不会被重写。

`formatdefinition.h` 提供 version=1 的 JSON 解析、导出、原子保存和合并：

- 每个定义含稳定 id、name、sessionTypeId、masks、signatures、settings。settings 保留编码、语法、转换、过滤、列等不透明数据，加载它们不执行任何程序。
- 用户与内置定义由调用方分开保存；相同 ID 覆盖内置，`baseId` 可继承传入 base 表，省略字段继承，settings 按键合并。导出保存解析后的完整值。
- `mergeDefinitions(builtIns, user, priorityIds)` 首先使用显式优先级，剩余用户项先于剩余内置项；从不修改传入内置表。
- 无效 JSON/版本整份拒绝；无效或重复条目逐个跳过并报诊断，其余正常加载。无效视图 ID、掩码、签名和非字符串 baseId 均拒绝。
- 文件读写限制 8 MiB；QSaveFile 原子替换，无效数据不触碰旧文件。内容签名最长 4096 字节且须完整位于最多可采样的前 1 MiB。

## 已验证与边界

最终 Qt 5.15.2 / macOS 纯 QtCore 行为测试：80 通过、0 失败、0 跳过。日志：`build-format-night-test/format-results.txt`。编译未产生 C++ warning/error。

覆盖规则/覆盖/签名的顺序，跨侧优先级，可用工厂和平台限制，未知兜底与拒绝打开，Unicode BOM/截断和非法字节，真实文件前缀采样与 I/O 错误，18 类内置格式具体 ID，JSON 继承/未知 settings/坏条目，合并不变性与原子保存。

`python3 tools/check_layering.py` 已通过。只读复查发现并修复：原生 Windows 分隔符、会话类型 ID 校验过宽、非法 baseId 类型、超过采样范围的签名。UTF-32 文本识别与文本解码器支持的差异已交主协调；主协调确认文本负责人完成 UTF-32LE/BE 自动与手动加载、保真保存和坏码点只读，文本 40 项通过。该跨模块验收来自主协调回报，本路未修改 Text。

### 主协调追加授权：三层解释式一致性修复

目检 FilterView 截图发现既有 `combinedExpression()` 将跨层白名单扁平合并成 OR，而 `decide()` 实际执行 AND。例如格式层 `*.cpp / *.h` 与视图层 `*.cpp` 的实际结果不包含头文件，原解释式却仍为 `(*.cpp || *.h)`。

修复只改变解释式生成：保留每层包含规则的 OR 组，再用 AND 连接各层；排除规则继续跨层取 OR 后取反。匹配、存储、状态、计数和错误处理逻辑均不改变。重复的完整组仍去重，含运算符/空格的字面掩码继续引用。

`Tests/FilterStack` 调整 3 个原先错误期待 OR 的断言，并新增同时核对表达式、真实接受/拒绝、启用开关的回归。`Tests/FilterView` 增加端到端回归，确认修复前表达式缺失 2 个 AND 时失败，修复后与实际过滤一致。

最终验收：FilterStack **85 通过 / 0 失败 / 0 跳过**，独立构建目录 `build-filterstack-format-fix`，日志 `filterstack-results.txt`；FilterView **15 通过 / 0 失败 / 0 跳过**，两页截图已在修复后重新生成并目检。三个套件合计 **180 通过**，均使用 Qt 5.15.2、独立构建目录和 `make -j2`。

未完成且不计为本轮实现：格式管理器 UI、语法高亮及忽略注释引擎、格式转换执行、归档解压器、图片/表格语义执行、关联覆盖单独规则文件导入界面、应用级日志/创建视图接线。名称与掩码过滤控件的宿主接线由主协调负责。本路未执行 Windows 构建；平台限定的判定在 macOS 服务测试中覆盖，但原生 Windows 文件访问需目标平台验收。
