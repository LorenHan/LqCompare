# Snapshot 服务交付记录

本模块改动 `Code/Services/Snapshot`、`Code/Tests/Snapshot` 和本文档；随后经 Sync 协调者追加授权，新增 `Code/Services/Sync/syncbaseline.h/.cpp` 与 `Code/Tests/SyncBaseline`。不修改 Folder、`syncengine.*`、Views 或共享构建文件。使用 Qt 5.15.2 / C++17，服务仅依赖 QtCore 与既有 Files / Folder 数据类型。

## 接入

在已接入 Files 的工程中包含 `Services/Snapshot/snapshot.pri`，公共头为 `snapshot.h`，命名空间 `LqCompare::Snapshot`。`snapshot.pri` 不重复加入 Files 或 Folder 的源码。

- `capture(root, CaptureOptions{true}, cancel, progress)`：递归读取元数据并计算普通文件 SHA-256。`CaptureOptions{false}` 只采集元数据。返回 `DocumentResult`，先检查 `ok()`。
- `fromFolder(result, leftSide)`：复用 `Folder::Result` 的元数据，拒绝取消、不完整、错误及未展开的冲突目录。普通 warnings（例如两侧是同目录）不影响完整性。结果不含摘要。Folder 的 `compareContent=false` 可能使其结果不完整，因此不能直接当成完整基线；需摘要的同步基线应调用 `capture`。
- `Document` 含 `sourceRoot`、`capturedAtUtc`、`complete`、`hashAlgorithm` 和 `QMap<QString,Entry> entries`。map key 是相对路径；`Entry::sha256` 是 64 字符小写十六进制，不含原始字节。
- `save(document,path,cancel)` / `load(path)`：原子保存、完整性与严格字段校验。调用方不得把失败或 `complete=false` 的 Document 用作完整同步基线。
- `compare(before,after)`：输出新增、删除、类型、大小、时间、属性、内容变化位及摘要；无摘要时保留内容未知。任一侧范围不完整时，该侧缺失的路径标为存在状态未知，不推断新增/删除。
- `allows(document,operation,reason)` / `readOnlyNotice()`：快照侧的命令能力与只读提示。逐字节、规则比较、文件内容复制、修改、删除均被拒绝。视图/命令装配需要调用该门禁。

`fromFolder` 还拒绝有扫描掩码、排除计数或被排除条目的结果，防止把过滤范围误当整棵目录；两侧路径大小写不同也会拒绝，提示直接 `capture` 来保留选中侧的原始名称。

## Sync 基线桥接

公共头 `Services/Sync/syncbaseline.h`，命名空间 `LqCompare::Sync`，由 Sync 协调者接入 `sync.pri`。

- `fromSnapshots(leftDoc,rightDoc,options,cancel)` 返回 `BaselineResult`，其中包含 `baseline/error/cancelled/ok()`。两份 Document 必须都是完整 SHA-256 清单、路径集合及类型相同，普通文件的大小与 SHA-256 相同；目录的时间/大小及文件元数据差异不妨碍确认共同内容。链接与特殊文件不能作为共同基线。
- `validateBaseline(baseline)` 校验完整范围、路径/父目录、摘要、属性、错误状态、大小写及 Unicode 名称碰撞，并要求两个规范化本地绝对根路径互不相同/包含，`scopeKey` 为有效摘要。桥接不读取原始文件或验证远端路径；根路径绑定在 `Sync::makePlan` 与当前选中左右根匹配时才生效。
- `saveBaseline(baseline,path,cancel)` 返回 `BaselineSaveResult`；`loadBaseline(path)` 返回 `BaselineResult`。文件是独立初版 JSON envelope（`LqCompare.sync-baseline`，version 1），持久化左右根、规则/排除范围的 `scopeKey`、完整性标志与共同条目。64 位大小/修改及创建时间均使用十进制字符串。完整性算法、严格字段校验、64 MiB 上限和原子保存取消规则与快照一致。
- 绑定保留左右次序；选中根或同步规则不匹配、文件损坏或缺失时，调用方应说明原因并传入无基线模式。合法范围排除规则可绑定，但建立该基线仍需要两份全量且内容一致的快照，不以过滤掉的条目推断删除。

桥接只记录共同内容；快照没有创建时间，桥接生成的 Fingerprint 创建时间为 0。Sync 当前变化方向判定使用内容摘要，执行前身份检查仍使用实时扫描得到的 Fingerprint，不能用基线创建时间代替实时身份。

## 存储与安全边界

快照是只读清单，不是备份，不保存文件内容，不能还原或复制文件。SHA-256 只用于摘要比较，未实现 CRC。

格式版本为初版 `1`，UTF-8 JSON 可读、可 diff。大小与纳秒时间以十进制字符串保存，避免 JSON 双精度损失 64 位精度。完整性摘要覆盖去掉 `integrity` 后的整个规范化 JSON 对象；它能发现损坏，不是签名或可信来源认证。重新排版不使摘要失效。

加载拒绝不支持的版本、截断 JSON、额外或缺失字段、字段类型不符、重复/不安全相对路径、非法父目录、非法属性、无效摘要和完整性不符。初版以前没有已发布的快照格式，不虚构旧版迁移；将来升级仍需补迁移实现及用例。加载上限为 64 MiB，条目上限为 1,000,000。

生成不跟随符号链接，不读取特殊文件内容。链接只记其元数据，未记录链接目标，所以没有依据时内容仍为未知。根目录是符号链接时明确拒绝。采集只在整个递归范围成功后输出 Document；深度上限、权限/读取错误或取消都不会提供半成品基线。计算摘要前后检查文件元数据，并在遍历后复查目录元数据以发现读取期间变化；这不是文件系统原子时间点或系统级文件锁。

`QSaveFile` 禁用直接写入回退，取消/写入失败不覆盖原目标，成功才原子替换。服务无写源目录文件 API。取消令牌可由工作线程控制；同步接口由调用方安排到后台线程。

## 验证

- 已通过：Qt 5.15.2 / macOS 单独静态编译（`/tmp/lqcompare-snapshot-compile`，`make -j2`）。
- 行为测试：`Code/Tests/Snapshot/SnapshotTests.pro`，独立构建目录 `/tmp/lqcompare-snapshot-tests-agent`，**114 通过、0 失败、0 跳过**（Qt 5.15.2 / macOS，`make -j2`）。覆盖真实目录采集/摘要、往返无内容、64 位精度、重签后的 schema 畸形输入、完整性损坏、差异标记及未知状态、只读能力、链接、读取故障与扫描过滤范围。
- 取消已验证预取消、采集进度中取消、失败结果不含半成品、保存预取消保留旧目标且不留临时文件；尚未通过确定性故障注入验证保存写入途中取消。
- 基线桥接已通过 Qt 5.15.2 / C++17 静态编译；`Code/Tests/SyncBaseline/SyncBaselineTests.pro` **116 通过、0 失败、0 跳过**（`/tmp/lqcompare-syncbaseline-tests-agent/results.txt`，`make -j2`）。覆盖真实 `capture → fromSnapshots → save/load → TwoWay preview` 单侧修改方向识别、规则不匹配降级、坏摘要/错误状态/不安全条目直接传入预演仍降级、同内容异元数据、根与范围校验、47 条重签 schema 畸形输入、7 条完整性损坏、64 位边界及原子保存预取消。

## 未完成与范围

尚未提供快照库管理 UI、快照专用打开入口、Folder 比较视图/命令侧只读接线、TXT/HTML/CSV 差异导出、历史格式迁移。自动同步后的基线刷新和绑定由 Sync/应用装配负责。Windows / Linux 平台尚未编译运行。不能把这些边界表述为 SNAP-001 至 SNAP-006 全部闭环。
