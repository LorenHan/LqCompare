# VER 版本比较模块交付（2026-09-20）

本模块只修改 `Code/Services/Version`、`Code/Views/Version`、`Code/Tests/Version*` 及本交付文档。没有修改共享 `.pri`、工厂、规格、GitHub 或其他团队文件；没有运行全量 runner。

## 集成接口

- `Services/Version/version.pri`：QtCore 服务；`versioninfo.h` 提供 `LqCompare::Version::parse(QByteArray, Limits)`、`inspectFile(QString, Limits)`；`versioncompare.h` 提供 `compare`、`compareNumbers`、`toCsv`。
- `Views/Version/versionview.pri`：自行添加 `QT += widgets concurrent`，依赖已存在的 `Views/Session` 与 `Services/Session`。
- `versioncomparesession.h`：`LqCompare::VersionCompareSession(QObject *parent = nullptr)`、`VersionCompareSession(const QString &left, const QString &right, QObject *parent = nullptr)`；`typeId()` 为 `version`。
- 路径接口：`leftPath()`、`rightPath()`、`setPaths(left, right, error)`。空会话可先打开，再从界面选择两侧文件。
- **异步契约**：`open` / 已打开会话的 `setPaths` / `reload` 成功表示工作已调度；以 `isBusy()`、`isLoaded()`、`busyChanged(bool)`、`loadFinished(bool, QString)` 判断完成。解析失败经公共 `errorReported` 出口报告。失败保留前次成功的路径、数据和比较表；初次异步加载失败时基类状态仍为 Open、`isLoaded()==false`，允许选择新文件重试。
- `rows()`、`leftInfo()`、`rightInfo()` 为最近一次成功的结果。`setOptions(CompareOptions)` 重新比较已有数据；`exportCsv(path,error)` 导出当前字段清单。
- 主协调负责在共享 services/views `.pri` 引入上述文件，并注册对应会话工厂。Windows 产品入口的平台约束仍由主协调和 SessionTypeRegistry 保持；跨平台可解析不等于已承诺所有平台产品入口。

## 已实现

### 文件与 PE 解析

纯字节只读解析，不使用 Windows loader，不调用 DLL，不执行输入文件，不查找或加载依赖库。读取普通文件前后检查大小与修改时间，拒绝顺序设备、目录、缺失文件及读取中发现变化的文件。非 PE 文件仍显示文件名、大小、创建/修改/访问时间、权限、所有者/组、宿主平台信息，并明确没有 PE 版本信息。MZ 开头但 PE 结构损坏返回 Invalid，不伪装为成功的通用文件。

支持 PE32、PE32+，提取机器类型、时间戳、子系统、入口 RVA、镜像基址、对齐、大小、头部校验和字段等；校验和按文件内记录原值呈现，未声称重新计算或验证。节表包含名称、虚拟地址/大小、原始偏移/大小、特征。

标准导入表保留 DLL、符号名及 hint，或 ordinal；标准导出表保留名称、ordinal、RVA、转发目标，支持无名称导出和同 ordinal 的别名。无符号的 DLL 依赖明确显示“无符号项”。不从导入名推断 DLL 版本，不虚构函数签名。导出 RVA 仅显示文件所记数值，不执行或按代码解引用。

RT_VERSION 资源按资源名/ID、资源语言分别保留 VS_FIXEDFILEINFO 全部固定字段，以及每个 StringTable 的语言/代码页、任意字符串字段、VarFileInfo/Translation。缺失字段保持缺失，不补伪造值；没有版本资源的合法 PE 仍可比较结构信息。

默认上限：64 MiB/文件、96 节、16,384 项累计工作量、4,096 字节/字符串、8 层资源目录；还限制累计文本。偏移运算先提升至 64 位，映射范围、节范围、目录、表长度、资源子块长度、UTF-16、字符串终止、ordinal 索引、循环和溢出均检查。解析任何结构失败时清空全部部分 PE 结果，只返回状态、原因和通用元信息。

### 会话与视图

- 两侧路径输入与浏览、后台比较、只读差异表；版本资源分组置顶，并按语言分别呈现。
- 分组可折叠，独立差异计数；字段按相同、变化、左独有、右独有、已忽略标注，长值有完整工具提示。
- 数字版本比较与原文字段差异分别呈现。接受十进制段与可选 `v` / `V` 前缀，允许首尾空白；预发布/其他后缀不可比。数字段以规范化字符串比较，无整数溢出；前导零无影响。
- 默认缺失尾段补零（1.2 与 1.2.0 数字相同、文本仍不同）；可改为不同段数不可比。可忽略版本号差异，其他字段继续比较。
- CSV 包含分组、字段、两侧值、差异类别和数字关系，保留引号/逗号/换行；原子保存，拒绝选择任一输入路径及其符号链接别名作为导出目标。
- 加载期间禁止重复提交；关闭/销毁会话不会由后台结果回填已关闭视图；始终不可保存输入文件。

## 真实验证

环境：macOS 26.6，Qt 5.15.2 x86_64，C++17，qmake；每个独立构建使用 `make -j2`。qmake 提示 SDK 26.5 新于 Qt 5.15 的已测试 SDK；实际编译、链接和运行成功。界面测试使用 `QT_QPA_PLATFORM=offscreen`；截图时有平台不支持 propagateSizeHints 的提示。

| 独立套件 | 实跑结果 | 证据 |
| --- | --- | --- |
| `Tests/Version/VersionTests.pro` | 74 passed / 0 failed / 0 skipped | `build/VersionTests-night/version-tests.txt` |
| `Tests/VersionCompare/VersionCompareTests.pro` | 62 passed / 0 failed / 0 skipped | `build/VersionCompareTests-night/result.txt` |
| `Tests/VersionView/VersionViewTests.pro` | 9 passed / 0 failed / 0 skipped | `build/VersionViewTests-night/results.txt` |

解析 fixture 是 `Tests/Version/pefixtures.h` 运行时生成的真实 PE 字节结构：PE32 / PE32+、三节、两种资源语言、每叶双 StringTable、固定/自定义/缺失字段、按名称/序号导入、无名/具名/转发导出；它们不是可运行 Windows 应用，也未交给 loader。覆盖 48 组损坏语料、5 种限额、12,284 个截断输入、512 个确定性变异，以及实际临时普通文件读取和内容/时间/权限未被修改。

比较套件覆盖数字关系/选项/大数字、字段差异/忽略、跨语言隔离、重复 import/export、CSV 保真；发现并修复了真实符号名含 `[2]` 后缀时覆盖重复项的问题，两种符号回归均通过。

会话/视图套件真实调度后台工作，驱动路径与 Compare 按钮，检查表格、PE/非PE状态、CSV、输入不变、失败重载保留结果、失败换路径时恢复与旧结果相符的路径显示、关闭和销毁时的回调安全。生成并人工检查离屏截图 `build/VersionViewTests-night/version-preview.png`，版本字段、语言、数值关系和差异颜色可见。构建/报告/截图留在各自 build 目录，不属于产品源码。

复跑方式：进入对应独立 build 目录，使用 `/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../../Code/Tests/<套件>/<套件>Tests.pro`，再 `make -j2` 和 `./bin/tst_version` / `./bin/tst_versioncompare` / `QT_QPA_PLATFORM=offscreen ./bin/tst_versionview`。macOS 若要求签名可对测试可执行文件执行本地 ad-hoc codesign。

## 验收边界与未验证项

本轮完成授权的独立解析服务、版本会话与差异表，覆盖 VER-001/002/003/005 的相应实现与自动测试；**不把整个 VER 产品验收写成全部完成**。

- Windows MinGW 8.1 32 位与 Linux 未实际构建/运行；Windows loader、签名文件、真实供应商 exe/dll、MUI 外部伴随文件自动查找未验证。当前多语言能力是输入 PE 内嵌 RT_VERSION 的按语言解析，不自动解析外部 MUI 文件集合。
- 文件夹双击入口、会话工厂、总工程编译、产品平台可见性由主协调集成验收。
- VER-004 两目录批量版本清单、可取消批处理、升级筛选和 XLSX 未实现；当前 CSV 是单对文件的结构/版本字段清单。
- 不支持延迟导入表/绑定导入表扩展解析；缺少原始 lookup table 的已绑定导入明确拒绝，不把已解析地址误识别为名称。
- 采用保守的单段 RVA 映射；需要读取的结构跨越节边界、重叠节映射、未知版本根子块等输入会明确拒绝。导出普通地址是记录值，不要求可作为文件字节读取。
- 文件读取前后检查并非文件锁或原子快照，无法保证识别大小和时间完全未变的并发覆写。平台可能因读取更新文件访问时间；程序不会写入输入内容。
- 数字比较不是完整 SemVer 预发布排序；当前规则和不可比分支已在 UI 与测试中明确。比较选项属于本次会话内存状态，未接入跨会话持久化。

格式来源：[Microsoft PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)、[VS_VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo)、[VS_FIXEDFILEINFO](https://learn.microsoft.com/en-us/windows/win32/api/verrsrc/ns-verrsrc-vs_fixedfileinfo)、[String](https://learn.microsoft.com/en-us/windows/win32/menurc/string-str)、[Var](https://learn.microsoft.com/en-us/windows/win32/menurc/var-str)。
