# 报表与补丁交付记录

工作区：`/Users/loren/Desktop/Work/LqCompare`。本路只维护 `Code/Services/Report`、`Code/Services/Patch`、对应 `Tests/Report*` / `Tests/Patch*` 与本文。顶层 `.pri` 由已有 `exists()` 自动装配；未改 App、Views、其他服务、规格、GitHub 或共享构建脚本。

## 可直接调用的报表接口

公共头 `Code/Services/Report/report.h`，命名空间 `LqCompare::Report`。

```cpp
Report::Metadata metadata;
metadata.settings << QStringLiteral("当前显示过滤：仅差异");
auto model = Report::fromText(leftDocument, rightDocument, textResult,
                              textCompareOptions, metadata);
// 目录使用 Report::fromFolder(folderResult, folderOptions, metadata)。
Report::Options options;
options.format = Report::Format::Html; // 或 PlainText
options.layout = Report::Layout::SideBySide;
QString error;
const bool saved = Report::writeFile(outputPath, model, options, &error,
                                     cancelFlag, progressCallback, overwriteChosen);
// 剪贴板 / 小预览：Report::render(model, options, &error)。
```

`Options` 默认只含差异（不含 Equal/Ignored，包含单侧项），可改 `includeEqual`、`includeIgnored`、`includeOrphans`、`showLineNumbers`、`utf8Bom`。支持并排、交错、摘要、统计四种布局；摘要保留状态、位置与说明，不复制长篇正文；统计仅给汇总。`model.rows[i].visible` 由调用方按当前显示过滤设置；请同时向 `Metadata.settings` 写入过滤说明，保证报告可解释。

统计区明确以全部比较结果为口径，另外报告输出范围内的条目数；文本的计数单位是对齐行，目录的计数单位是条目，目录数量另列。Ignored 不计为差异；Error、Unknown、未完成或取消的比较绝不报告“无差异”。源路径、比较选项、编码/BOM/换行、时间、工具版本写入报告。默认版本来自应用 `QCoreApplication::applicationVersion()`；调用方可用 `Metadata.toolVersion` 明确覆盖。

HTML 是离线单文件，正文、来源、路径、标题、设置、警告和详情均做 HTML 转义，用户内容不进入脚本、资源 URL 或事件属性；内嵌 CSP 限制默认资源加载。固定脚本仅用于关键字搜索、状态过滤和分组折叠。每组默认 250 项，后续组初始折叠；打印时展开分组、使用白底黑字。中文和 Unicode 原样写为 UTF-8，默认带 BOM。

`write(QIODevice*)` 逐条渲染，单次设备写入不超过 64 KiB，支持短写重试；`writeFile` 使用 `QSaveFile` 且禁用直接写入回退，取消或失败不留下半成品、不破坏已有文件。默认拒绝覆盖已有目标，UI 完成目标覆盖选择后传 `overwrite=true`；即便如此仍拒绝覆盖比较源文件或符号链接。`render` 与文件内容（去除编码 BOM 后）一致。

服务是无 UI 的同步函数，可在工作线程运行；进度回调在调用线程触发，UI 需自行转发到主线程，模型应为此次任务的稳定快照。`render` 有意缓存整个输出，只用于剪贴板或小预览。文件输出不会先拼接完整报告；适配器构造的 Model 本身仍随比较结果规模增长。

## 可直接调用的补丁接口

公共头 `Code/Services/Patch/patch.h`，命名空间 `LqCompare::Patch`。

```cpp
Patch::FileInput input;
input.oldPath = input.newPath = QStringLiteral("src/example.txt");
input.oldBytes = originalBytes;
input.newBytes = editedBytes;
auto generated = Patch::generate({input});
if (generated.ok)
    Patch::writeFile(patchPath, generated.bytes, &error, overwriteChosen);

auto parsed = Patch::parse(patchBytes);
if (parsed.ok) {
    Patch::ApplyOptions options;
    options.reverse = false;
    options.stripComponents = 1; // git a/ b/；svn / 普通相对路径可设 0
    auto plan = Patch::preview(parsed.document, targetRoot, options);
    // 展示 plan.files、每个 hunk 的可应用状态、偏移和诊断。
    // 本服务只预演，没有修改源文件的动作。
}
```

生成支持多个文件对、可选上下文（默认 3 行）、相对路径与前缀（默认 `a/` / `b/`）、创建/删除非空文件、无末尾换行标记；输入是原始字节，绝不使用显示忽略规则生成补丁。`GenerateResult` 提供文件、hunk、增删行数量与诊断，失败不返回部分补丁。`writeFile` 采用原子保存，默认拒绝覆盖与符号链接。

解析返回结构化文件/hunk/正文和一基错误行号。支持普通 unified、Git/SVN/Hg 的常见 unified 前导元数据、Git 路径引号/八进制转义、CRLF 源行及无末尾换行标记。`ParseOptions.stripTransportCr` 只用于明确知道补丁在传输中整体变为 CRLF 的情况，避免把源文件真正的 CRLF 静默改成 LF。

预演支持反向、逐 hunk 选择、完整上下文的唯一偏移匹配；不丢弃上下文、不猜测多个匹配。先校验原始路径，再剥离前缀，拒绝绝对路径、盘符、反斜线、`..`、危险系统设备名、符号链接路径组件、非普通文件和重复/别名目标。预演只读，失败不改变原文件；单文件失败不返回部分结果字节。输出计划内的 `resultBytes` 是供审查的结果，不能被上层自动提交。

## 验收

报表套件已在本机 Qt 5.15.2 / macOS 26.6 完成独立 qmake、`make -j2`、ad-hoc 签名后实际运行：**36 passed / 0 failed / 0 skipped**（最后一轮 54 ms）。

- 工程：`Code/Tests/Report/ReportTests.pro`；真实输入：`Code/Tests/Report/fixtures/text-left.txt` / `text-right.txt`。
- 验证 Text 真实解码/比较到报告的映射、所有非信任字段的 HTML 转义、无外链资源标签、中文/emoji、2 格式 × 4 布局、4 BOM/格式组合、Ignored/可见性/单侧项过滤、五种不确定目录结果。
- 注入短写与写入失败；验证四种生成前/中取消场景、旧文件保持、无临时残留、源文件覆盖保护、无效选项失败、分块输出与完整进度。审查新增 Equal/Ignored 交错呈现、单侧项无虚假空行、目录读取错误不误写“不存在”的回归。
- 日志：`_build-report-tests/report-test-results.txt`；样例：`_build-report-tests/artifacts/text-report.html`、`text-report.txt`。样例正文和元数据已静态核对。
- 浏览器目测未完成：电脑使用工具的浏览器 URL 安全策略拒绝本地 `file://` 页面；没有通过别的浏览器/本地服务器绕过。没有将此记为浏览器验收通过。

补丁套件已同样完成独立 qmake、`make -j2`、签名后实际运行：**75 passed / 0 failed / 0 skipped**（487 ms）。两套总计 **111 passed**。

- 工程：`Code/Tests/PatchRegression/PatchRegressionTests.pro`；独立构建目录：`Code/Tests/PatchRegression/build-local`；最终日志：该目录的 `final-run.txt`。
- 真实工具：`git version 2.50.1 (Apple Git-155)`、`patch 2.0-12u11-Apple`、`Apple diff (based on FreeBSD diff)`。真实 `git diff` 与系统 `diff -u` 产生的补丁已解析并逐字节预演；SVN/Hg 使用固定兼容 fixture，本机没有这两个命令，未声称真实执行。
- 自家生成的七文件补丁含中文/空格/引号路径、Unicode、CRLF、BOM、无末尾换行及混合行尾，分别通过 `git apply --check` / `git apply` 和 `patch -p1 --batch` 实际应用，再以反向命令恢复，每个文件均逐字节核对。
- 18 类字节往返、默认/零上下文、反向、逐 hunk 选择、多 hunk 大小变化、唯一偏移匹配、歧义拒绝、上下文不丢弃、创建删除、多文件失败不写盘、路径穿越/绝对路径/符号链接/重复目标、畸形与截断输入、原子导出覆盖拒绝和错误原文件保持，均有通过用例。
- 工具验收实际发现 macOS `patch` 不支持生成器原先使用的 Git 八进制中文路径，已改为标准 unified 的 UTF-8 原文路径 + TAB 终止后通过。解析仍支持 Git 外部的引号/八进制路径。最终路径以双引号开头且前缀为空时明确拒绝生成，提示使用非空相对前缀。
- 两套测试没有引用界面模块，没有运行全量测试脚本或总构建；本路没有在真实用户目录执行补丁应用，标准工具测试只在临时 fixture 目录操作。

## 尚未覆盖的规格

- 报表只交付 HTML/TXT；CSV/XML、模板、预设持久化、其他编码、本地化语言切换、自动分页文件、打印机/PDF/UI 异步装配、CLI 入口和路径模板由后续任务实现。
- HTML 已分组，但没有虚拟列表；未宣称超过十万行的浏览器滚动性能或 Chrome/Edge/Safari 全矩阵验收。当前分组是按输出条目分块，尚未实现按目录层级的树形折叠。
- 未实现统计 Top N、扩展名/目录深度聚合、CRC/属性/时间戳选项。当前报表忠实覆盖现有 Text / Folder 比较模型。
- 补丁没有真实应用、备份、原子多文件提交或失败回滚；PAT-002/005 的事务与确认 UI 尚未满足。当前只提供只读预演以及生成补丁文件的原子保存。
- 二进制/git binary patch、NUL/UTF-16 文本、传统 context diff、权限修改、重命名尚不支持，不能静默当作成功。纯 unified 对空文件创建/删除不可移植，当前明确拒绝生成。
- 未运行 Windows MinGW 32 位构建与跨平台 GUI 验收。本路不声明整个 RPT/PAT 规格完成。

## 补丁应用（续做）

上一节的「尚未覆盖的规格」里写着「补丁没有真实应用、备份、原子多文件提交或失败回滚；PAT-002/005 的事务与确认 UI 尚未满足」。本节把其中属于**服务层**的那部分做完：`Code/Services/Patch/patchapply.cpp` 提供真实的应用动作。界面侧（确认对话框、受影响文件清单的展示方式）仍不在本路范围内。

### 交付内容

- 新建 `Code/Services/Patch/patchapply.cpp`（约 690 行），实现 `patchapply.h` 声明的全部 API：`prepareApplication()`、`executeApplication()`、`ApplicationPlan` 的四个公开成员、`applicationAuditJson()`。
- 新建 `Code/Tests/PatchApply/PatchApplyTests.pro` 与 `tst_patchapply.cpp`：独立 QTest 套件，58 个用例函数（`Totals` 里另有 QTest 自动生成的 `initTestCase` / `cleanupTestCase`，所以显示 60 passed）。
- **未改** `patch.pri`、`patch.h`、`patch.cpp`、`patchapply.h`。`patch.pri` 原先就声明了 `patchapply.cpp`，本轮只是把这个文件补齐——这正是不补它就编不过整个应用工程的原因。

### 怎么用

```cpp
Patch::InputProtection protection;
protection.source = Patch::PatchSource::InMemory;   // 或 File + patchFilePath
// 从文件读的补丁还应把补丁文件本身与其它输入放进 readOnlyPaths。

const Patch::ApplicationPlan plan =
    Patch::prepareApplication(parsed.document, targetRoot, applyOptions, protection);
if (!plan.isReady()) {
    // 展示 plan.preview()（每个 hunk 的 applicable/offset/error）与 plan.diagnostics()
} else if (Patch::executeApplication(plan, userConfirmed).ok()) {
    // 结果里带 targetPath、backupPath 与结构化审计
}
const QByteArray report = Patch::applicationAuditJson(result);   // 落盘的报表
```

`prepareApplication` 是**只读**的：它只做预演、边界检查与快照，不写一个字节。写盘只发生在 `executeApplication`，且必须显式传 `confirmed=true`。

### 关键设计理由

- **一次只替换一个已存在的普通文件。** 创建、删除、以及同时改动多于一个目标都在写盘前拒绝，并把受影响文件列进诊断。这是 `patchapply.h` 写定的边界，因此删除类补丁在应用层是「拒绝 + 列出受影响文件」，而不是「确认后执行删除」——PAT-005 要求的那次额外确认在界面上表现为这条拒绝提示。
- **强制确认对所有应用生效，不止危险动作。** `confirmed=false` 一律拒绝并列出受影响文件；「大规模改写」（改动 ≥ 40 行且至少占原文一半）另外在计划的诊断里带一条点名文件与行数的提示，界面据此做更强的确认文案。`ApplicationPlan` 的公开成员是写定的，没有地方挂「危险标志」，诊断就是那个出口。
- **暂存与提交靠 `QSaveFile`，失败路径不碰目标文件。** 新内容写进同目录的临时文件，`commit()` 才做原子替换；`setDirectWriteFallback(false)` 关掉了「直接写原文件」的回退，否则目标目录不可写时原子性会静默消失。备份同样用 `QSaveFile` 写，宁可这里失败，也不能留下半份像备份的字节。
- **回滚只有一条通道，且回滚必须再校验一次。** 提交后校验不过就把源快照写回目标，写回后再读一遍比对。回滚写失败/提交失败/回滚后校验失败一律报 `RecoveryRequired`，并把备份路径放进结果——**绝不**报成 `RolledBack`：把「回滚失败」说成「已回滚」等于让用户以为文件是好的，而它其实不是。
- **回调之后才跑校验。** `ApplicationFailureInjector` 的契约是「回调可以模拟外部改写，校验在回调之后跑」。所以每个注入点之后都会重新读一次目标并比对源快照；这样「回调改了文件」这条路径才真的可测，而不是只在代码里写着。
- **`backupPath` 只指向我们自己刚写的那一份。** 备份名优先用 `<name>.orig`，被占用就退到 `<name>.orig.<n>`，绝不覆盖已存在的 `.orig`（那可能是别的工具或上一次会话留下的唯一副本）。目标没被改动而操作中止时，这个备份会被删掉——留着它只会在用户目录里堆积无用文件，而 `backupPath` 会指向一个多余的东西。
- **备份位置与保留策略走环境变量。** `ApplyOptions` 与 `InputProtection` 里没有备份字段，而本路不允许改 `patchapply.h`，所以 `LQCOMPARE_PATCH_BACKUP_DIR`（位置）与 `LQCOMPARE_PATCH_BACKUP_KEEP`（保留策略）是唯一既不动接口又能被测试真正配置到的地方。默认（不设变量）不裁剪任何历史备份：删用户目录里的东西必须是显式配置过的行为。裁剪只针对带序号的历史备份，无序号 `.orig` 与本次自己的备份永不删除；`KEEP=0` 与「未设置」是两个不同的含义，所以内部的「不裁剪」用 `-1` 表示。
- **协作锁是协作，不是安全边界。** 目标旁的 `.<name>.lqcompare-lock` 只挡得住同样遵守这个约定的调用方；恶意的并发改名/写入挡不住，断电持久性也不承诺。`patchapply.h` 已经写明这一点，实现里的注释重复一遍是为了别让它被后来者误当成防线。
- **`ApplicationStatus::Rejected` 与 `FailedUnchanged` 分开。** 前者是「计划/输入/并发状态不允许应用，什么都没写」，后者是「写了（备份或暂存）但目标文件一个字节都没动」。两者对用户的意义不同，因此不合并。

### 审计与日志

`ApplicationResult::audit` 是操作日志本体：每个阶段一条带 UTC 时间戳的记录，顺序固定为 验证 → 备份 → 暂存 → 提交 → 校验 →（回滚）→ 结束，未走到的阶段不出现；`Finished.success` 只在 `Applied` / `NoChanges` 时为真（`RolledBack` 不算成功，因为这次应用没有成功）。

`applicationAuditJson()` 输出报表：

```json
{
  "status": "rolledBack",
  "ok": false,
  "targetPath": "/tmp/…/sample.txt",
  "backupPath": "/tmp/…/sample.txt.orig",
  "diagnostics": [ { "line": 0, "message": "注入：提交后故障" } ],
  "audit": [
    { "stage": "validation", "success": true, "path": "…", "message": "…", "time": "2026-09-21T…Z" },
    { "stage": "rollback", "success": true, "path": "…", "message": "已从备份回滚，目标文件恢复原状", "time": "…" }
  ]
}
```

应用中成功的 hunk 数与偏移量写在 `commit` 那条审计的 `message` 里（形如「已应用 2 个 hunk（选中 3，文件共 3）；偏移调整：+0, +4」），检查 hunk 的 `offset` 则可以从 `plan.preview().files[].hunks[].offset` 结构化地拿到。**「fuzz」在这里指唯一的完整上下文偏移匹配，不是传统 patch 的行模糊**：`patch.h` 明确「Exact complete context only; never discard context」，本实现不丢弃任何上下文。

### 验证结果

命令与结果（本机 Qt 5.15.2 / clang_64 / macOS 26.5 SDK，`offscreen`）：

```
LQCOMPARE_TEST_BUILD_ROOT=/tmp/lq-patchapply-build Code/Tests/run-tests.sh PatchApply
  Totals: 60 passed, 0 failed, 0 skipped, 0 blacklisted, 77ms
```

`python3 tools/check_layering.py` → 「分层检查通过：App -> Views -> Services，Services 未反向依赖界面」。新建的 `patchapply.cpp` 只 include Qt 与 `patchapply.h` / `patch.h`，不依赖 Text（`patch.h` 本身不带 Text 依赖）；测试工程也不引用任何界面模块。

用例分组（58 个）：预演与逐 hunk 选择 5 个；边界与路径安全（创建/删除/多目标/遍历/绝对路径/符号链接/重复目标/输入保护/二进制）12 个；确认与大规模改写 5 个；备份与保留策略 8 个；原子性、回滚与并发 14 个；审计与报表 JSON 8 个；hunk 统计 2 个；反向应用 2 个；二进制与权限变更的解析层拒绝 2 个。

补充验证：`cd _build-lqcompare && make patchapply.o` 用**主工程自己的**编译选项（`-Wall -Wextra`、App 的完整 INCLUDEPATH）成功编译该文件且 0 告警——这直接确认了「`patch.pri` 声明的文件缺失导致整个应用工程编不过」这个阻塞点已经解除。**未**因此跑整个应用或全量测试套件（按分工由主协调负责）。

### 定点变异（16 处，15 处检出、1 处预期漏检）

脚本逐处改 `patchapply.cpp`：改前与还原后都删掉 `patchapply.o` 再 `make`，并对每次替换断言「确实替换了」。每轮结束后重建并复跑套件确认基线仍全绿。

| 变异 | 结果 | 至少一处变红 |
| --- | --- | --- |
| M1 把 `QSaveFile` 原子替换改成直接写目标文件 | 检出（25 个用例红） | `failureDuringStageWriteLeavesTheTargetUnchanged`、`failureBeforeCommitLeavesTheTargetUnchanged` |
| M2 去掉回滚（校验失败后不做任何恢复） | 检出 | `verificationFailureRollsBackByteExactly`、`verificationMismatchRollsBack` |
| M3 去掉执行期的目标重新校验（符号链接/逃逸/规范化） | 检出 | `symlinkSwappedInAfterPrepareIsRejected` |
| M4 忽略 `confirmed` 参数 | 检出 | `executionWithoutConfirmationIsRejectedAndListsTargets`、`largeRewriteNeedsConfirmationAndNamesTheFile` |
| M5 把 `RecoveryRequired` 报成 `RolledBack` | 检出 | 三条 `rollback…ReportsRecoveryRequired`、`auditJsonReportsRecoveryRequiredWithItsBackup` |
| M6 报表 JSON 的 `stage` 字段名写错 | 检出 | `auditJsonCarriesStatusPathsAndStages` |
| M7 跳过暂存阶段的「回调后快照校验」 | 检出 | `externalChangeDuringStagingIsDetected` |
| M8 fuzz 偏移量恒置 0 | 检出 | `reportListsAppliedHunksAndFuzzOffsets` |
| M9 不创建备份 | 检出（25 个用例红） | `applyCreatesAnOrigBackupOfTheOriginalBytes`、`corruptedBackupIsDetectedBeforeApplying` |
| M10 不拿协作锁 | 检出 | `cooperativeLockIsHonoured` |
| M11 去掉「多于一个改动目标」的边界拒绝 | 检出 | `multipleTargetsAreRejectedWithAllAffectedFiles` |
| M12 去掉「补丁会删除文件」的边界拒绝 | 检出 | `deletionPatchIsRejectedWithAffectedFiles` |
| M13 跳过备份内容校验 | 检出 | `corruptedBackupIsDetectedBeforeApplying` |
| M14 去掉只读输入清单校验 | 检出 | `targetOutsideTheReadOnlyListIsRejected` |
| M15 去掉「目标就是补丁来源文件」的保护 | 检出 | `targetEqualToThePatchSourceIsRejected` |
| M16 去掉 `prepareApplication` 里的根目录包含判定 | **漏检**（预期如此） | —— |

M16 漏检的原因值得记下来：`preview()` 早就用同一套规则拒绝了遍历/绝对/符号链接路径，`prepareApplication` 里这条判定是**纵深防御**，去掉它没有任何用例会红。它保留的价值在于「Document 可能不经 `parse`/`preview` 而由别处组装」这一类未来变化，但它不是唯一防线，也不应被当成能反向验证的护栏。真正被用例钉住的那条路径逃逸防护是 M3（执行期重新校验）。

### 还没做的

- **界面**：确认对话框、受影响文件清单的展示、hunk 逐项勾选的控件、报表落地路径，都不在本路范围内。本路只提供可调用的服务接口与结构化结果。
- **操作日志服务未接通**：`Services/Log/logging.h` 是现成的，但 `patch.pri`（不可改）里没有 `../Log` 的 `INCLUDEPATH`，从 `patchapply.cpp` include 它会形成一条**声明不出来的隐式依赖**（单独构建 patch 模块就会失败）。因此本路用 `ApplicationResult::audit` 承担「操作日志」，没有把每个动作再写进全局日志。要接通需要先允许改 `patch.pri` 或 `patchapply.h`。
- **权限变更补丁**：`Document` 结构里没有权限位，应用层无法表示它；权限/模式行在 `parse()` 就被拒绝并单独提示（有用例）。二进制同理在解析层被拒，另有一条应用层的 NUL 检查兜住「手工构造的文档」。
- **多文件事务**：本模块的边界就是单文件替换，没有「多文件一起提交/一起回滚」。`preview()` 能预演多文件，但 `prepareApplication` 会拒绝多于一个改动目标。
- **断电持久性、恶意并发改名/写入**：`patchapply.h` 明确不承诺，实现也未做（没有 fsync 目录、没有防御性 `O_NOFOLLOW` 打开）。协作锁只对协作方有效。
- **未验证的平台**：全部结论只来自本机 macOS（Qt 5.15.2 clang_64，x86_64，`offscreen`）。**Windows / Linux 均未验证**，也没有跨平台 GUI 验收。

### 发现但没改的问题

1. `ApplicationResult` 没有承载 hunk 统计与偏移量的结构化字段，PAT-002 第 4 条要求「在报告中列出」，只能塞进 `ApplicationAudit.message`（文本）与 `diagnostics` 里。建议后续在 `patchapply.h` 给 `ApplicationResult` 加 `appliedHunks` / `selectedHunks` / `fuzzOffsets`，让报表消费方不必解析人读文本。
2. 备份位置与保留策略没有接口字段，只能走环境变量（见上）。这是同一类缺口：`ApplyOptions` 里加 `backupDirectory` / `backupKeep` 更合适。
3. `prepareApplication` 的阶段顺序无法表达「确认前」与「确认后」：`executeApplication(plan, confirmed)` 是唯一的分界。若界面希望在预演时就知道「这次需要几次确认」，目前只能靠读诊断文本判断。
4. `patch.cpp` 的 `preview()` 对**未选中**的文件也照样做路径校验（这本身是对的），但它同时把「有多文件记录」的补丁整体判成不可用——如果将来要做「多文件事务」，这条判定需要重做。
5. 环境变量名没有集中登记的地方（本仓没有统一的配置常量表），`LQCOMPARE_PATCH_BACKUP_DIR` / `LQCOMPARE_PATCH_BACKUP_KEEP` 目前只写在 `patchapply.cpp` 顶部与本节。
6. 新踩到的编译坑（建议补进 `current-handoff.md` §6，本路不改那个文件）：`QCOMPARE(a, QStringList{"x", "y"})` 会报 `too many arguments provided to function-like macro invocation`——宏参数里**只有圆括号**能保护逗号，`QStringList{…}` 的花括号不算。测试里统一用 `asList(QStringList{…})` 包一层函数调用解决；`patch.pri` 里那句「`QStringLiteral` 不能直接写进 `= {…}`」的坑与它是同一族问题的两个不同症状。
