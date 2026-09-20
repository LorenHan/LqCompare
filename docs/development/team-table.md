# 表格比较团队交付记录

工作目录：`/Users/loren/Desktop/Work/LqCompare`。本团队仅修改 `Code/Services/Table`、`Code/Views/Table`、`Code/Tests/Table*` 与本文；未修改 Special、App、共享规格或其他团队文件，未提交、推送或操作 GitHub。

## 接入

- 服务层：`Code/Services/Table/table.pri`，纯 QtCore，不依赖 Views。
- 视图层：`Code/Views/Table/tableview.pri`，QtWidgets，依赖现有 `Views/Session/comparesession` 与 `Services/Session/session`。
- 主协调在总 `.pri` 中 include 两份文件，在 `table` 工厂创建 `LqCompare::TableCompareSession`。
- 入口头文件：`tablecomparesession.h`；构造 `TableCompareSession(QObject*)` 或 `TableCompareSession(leftPath, rightPath, QObject*)`。基类 `typeId()` 固定为 `table`。
- `setPaths(left, right, error)`、`open()`、`reload()`；`leftPath()`、`rightPath()`、`leftDocument()`、`rightDocument()`、`comparison()` 提供实际数据。`setComparisonOptions()` 和 `setParseOptions()` 修改规则。`firstDifference()`、`lastDifference()`、`nextDifference()`、`previousDifference()` 可接导航命令。
- `canSaveNow()` 固定 false；没有写回源文件路径。基类 `canSave()` 不会把表格源文件的保存动作误标可用。

## 已实现行为

### 解析

- CSV、TSV 与自定义字面分隔符（含多字符）；自动候选为逗号、Tab、分号、竖线。
- 正确处理引号内分隔符、引号内 CR/LF/CRLF、双引号转义、空字段、尾分隔符与尾换行；数据内换行原样保留。
- UTF-8（含 BOM）、UTF-16LE/BE BOM、严格编码校验与显式 Qt 编码名（如 GB18030）。自动编码为 BOM 或严格 UTF-8，不猜测本地 ANSI 编码。
- 两侧独立分隔符、编码和首行表头选项；无表头时生成列名；不把缺失字段补成空字符串。
- 失败给出具体原因，CSV 语法错误含记录/字段/物理行位置。双侧读取全成功后才替换现有文档；失败保留之前两侧数据。
- XLSX/XLS/XLSM/XLSB、HTML 和二进制工作簿签名明确拒绝，并提示先导出 CSV/TSV。
- 初版资源边界：单文件 32 MiB、16,384 列、2,000,000 个解析单元格，超限明确失败。
- 成功加载后会话保存绝对源路径，避免启动目录改变导致重载/会话保存引用另一份文件。

### 比较

- 默认按内容匹配，优先完整比较列内容相等，再做有界相似度匹配；插入、删除与重新排序不会整体退化成按行号比较。按行号模式需显式选择。
- 表头唯一时自动按名称映射，任一侧无表头时按位置映射；重名表头要求用户改成位置/手工映射。
- 手工一对一映射；Compare、Key、Ignore、Display 角色。未映射列保留显示并明确标成 Not compared。
- 多键按原始字符串对齐。重复键所有记录保留，先按相同内容配对、剩余按组内出现次序配对；标记重复键并汇总警告，不用单值哈希覆盖数据。
- 数值列需显式选择；点号小数、科学计数，绝对/相对容差取 `max(abs, rel × max(|a|, |b|))`。零容差保留十进制精度，大于 2^53 的相邻整数不因 double 舍入变相同。可显式要求数值格式完全一致。非法数值按原始文本比较，不转换成零。
- 相同、内容不同、仅左、仅右为互斥行状态；按参与比较的差异单元格统计。`ignoredCells` 是 Ignore 列在结果中的格数，不是仅忽略掉的差异格数。
- 模糊对齐最多 200,000 个候选、2,000,000 个字段/采样字符工作；超限警告，并保留未配对记录，不静默按位置回退。最多生成 4,000,000 个结果格，超限明确报错。双侧有数据但没有任何参与比较的列时明确报错。

### 会话界面

- 左右只读 `QTableView`，共用对齐结果；原始记录号与行状态常显，差异格着色，缺失格为 `∅`，区别于空字符串。
- 路径输入/浏览、Compare paths、Reload、两侧解析设置、Columns / keys / tolerance 对话框。
- 只看差异行、只看包含差异的列、上一/下一差异行，左右同步滚动。列宽可调整。
- 单元格双击只读详情（可选择完整文本）；列头显示对应源列与角色，tooltip 给对应关系、差异数与容差。
- 行/格统计、重复键/解析警告/数值容差常显。映射错误允许在保留已解析源数据的情况下修正。
- 使用 `SessionSettings` 保存 `table.left/right.delimiter`、`encoding`、`header`，以及 `table.columnMode`、`alignment`、`minimumSimilarity`、`columns`（QVariantList 内含左右列索引、角色、数值类型/容差/格式规则）。主协调的通用会话文档可直接序列化这些键。

## 验证

独立 qmake/QtTest 工程：

- `Code/Tests/TableParser/TableParserTests.pro`
- `Code/Tests/TableCompare/TableCompareTests.pro`
- `Code/Tests/TableView/TableViewTests.pro`

使用 Qt 5.15.2 `~/Qt/5.15.2/clang_64/bin/qmake`、C++17、独立 `.codex-work/table-*-build` 构建目录、`make -j2`。界面测试使用 `QT_QPA_PLATFORM=offscreen`。

最终验收（2026-09-20 夜间）：

| 测试 | QtTest 通过数 | 失败/跳过 |
| --- | ---: | ---: |
| TableParser | 64 | 0 / 0 |
| TableCompare | 67 | 0 / 0 |
| TableView | 15 | 0 / 0 |

合计 146 项通过（含各套件 init/cleanup）。最终 TableView 构建包含最新比较引擎和会话代码。`Code/Tests/run-tests.sh` 自动扫描所有 `Tests/*/*.pro`，三份新工程无需更改共享运行器即可被纳入。

界面测试覆盖真实 CSV 打开、按表头重排列、逐格底色、行/列筛选、缺失与空格区别、重复键保留、数值容差与忽略列、重载及双侧回滚、会话设置恢复、相对源路径打开后改变工作目录仍能重载、实际按钮/解析设置、实际列规则模态对话框（重复目标列被拒绝；独立右列映射、键/数值/忽略角色生效；`1e-12` 容差再次打开不被舍入为零）、只读禁止写回。

比较测试额外覆盖大于 2^53 的相邻整数、正负极值、科学计数、严格容差边界（不增加任意 epsilon 放宽）、亚正常数、长数值文本的候选工作预算；配置中的 double 容差先转为最短 round-trip 十进制，再作精确十进制比较。

已实际生成并查看 1500×800 界面截图：`.codex-work/table-view-build/table-view.png`。发现并修复初始选中行蓝底遮盖差异色的问题，最终以蓝色选择边框保留橙色差异格。截图是本地验证产物，不纳入源码。

`python3 tools/check_layering.py` 已通过。

## 明确未完成

不能将 DATA-001～012 整体标为完成。本轮是 CSV/TSV 只读闭环，以下仍未支持：

- XLSX/XLS/HTML、多工作表、公式/样式、Excel 读写；不宣称通用 Excel 支持。
- 源表编辑/写回、撤销重做、差异复制合并、差异报表/CSV/XLSX 导出与带转义的整表剪贴板。
- 日期、布尔、自定义类型与数值舍入规则；自定义引号/转义、正则分隔符、跳过前 N 行/任意表头行。
- 字符级单元格差异、按单元格导航、冻结列、展开折叠、完整显示布局持久化。
- 后台取消/进度与超大文件流式比较。网格按模型按需显示，但解析/比较仍同步且有明确资源上限。
- 手工列规则对话框限 256 个左侧列；更宽表格可使用自动/位置映射，避免一次创建数万个编辑控件。服务 API 不受该对话框上限限制。
- Windows MinGW 目标机验证；本轮只在 macOS Qt 5.15.2 实际编译/运行。
