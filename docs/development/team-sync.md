# 同步交付记录（2026-09-20 夜间）

独占代码：`Code/Services/Sync`、`Code/Views/Sync`、`Code/Tests/Sync*`。快照独立记录见 `team-snapshot.md`，界面记录见 `team-sync-view.md`。未修改其他团队代码、共享规格或提交 Git。

## 集成

- 服务：`Services/Sync/sync.pri`，依赖 Files、Folder、Snapshot；Folder 独立测试还需要 Filter 的 mask/maskfilter 实现。服务层仅 QtCore，无 Views/App 引用。
- 窗口：`Views/Sync/syncview.pri`（Qt Widgets + Concurrent）；`LqCompare::SyncPreviewDialog(QWidget*)`，`setDirectories(left,right)` 后自动预演，可以 `show()` 或 `exec()`。
- 主协调已接 Home 的 folder-sync 与 Tools 镜像入口；总 App 编译由主协调执行。
- `Sync::preview` 自行扫描；`makePlan` 可复用未过滤的完整 `Folder::Result`。已被 Folder 扫描掩码排除的结果保守拒绝直接转换，须重新预演；同步自己的 `excludedPaths` 是两侧共同保护的相对子树。
- `Sync::Executor::execute` 需要 `Confirmation{confirmationDigest(plan),true,largeDeleteConfirmed}`，勾选、规则或任何计划内容变化都使旧确认失效。
- `commonBaseline` 只从完整、无范围排除、全部内容相同的最新预演生成共同基线；`syncbaseline.h` 提供从两份 SHA-256 Snapshot 构建、原子保存与严格加载。基线绑定有序左右路径和规则/范围摘要。

## 已实现的安全闭环

更新模式复制源侧独有与较新文件；镜像把目标多余条目明确列为回收站删除；双向没有基线时，两侧不同始终冲突，不用时间戳猜测哪侧改过。有效基线区分单侧修改、单侧删除和两侧变化（包括删除对修改冲突）。冲突默认不执行。镜像与“不删除”组合明确报错。

执行前确认所勾选的复制字节、目录数和删除数/字节；删除超过阈值额外确认。扫描不完整、路径穿越、大小写/Unicode 名称歧义、相同或嵌套根目录一律拒绝；链接和类型冲突不自动处理。执行前整体复验选中条目的 SHA-256、元数据和父目录身份，写入前再次校验路径与目标，避免旧预演覆盖变化。

覆盖先把原目标复制到同步树外的独立备份目录；原子 `QSaveFile` 替换，流式计算 SHA-256 并复制后校验、保留修改时间。只读目标保留并列为失败，其余操作继续。`journal.json` 在写入前保存计划与恢复映射，执行后逐项更新；日志失败停止后续操作。备份路径与日志不进入同步数据源。恢复覆盖备份时再次检查结果和备份，拒绝覆盖同步后的用户修改。

删除只调用现有 `TrashService`，不可用时失败且绝不降级永久删除。目录按子项优先，只把空目录移到回收站；被排除、未勾选、新增或失败子项会保住父目录。报告列出实际回收位置和备份路径。回收站撤销能力如实受底层平台限制：`undoLastTrash()` 只针对最近一次成功删除批次，其他条目仍可由系统回收站按位置恢复，不宣称可批量事务回滚。

取消保留已完成操作，逐项列出未执行项；单条失败继续汇总。预演/执行/恢复由视图后台工作线程调用，可取消，完成后重新预演。所有确认由窗口明确触发，预演不会写入同步目录。

## 验证

- Qt 5.15.2、C++17、qmake，独立构建目录 `/tmp/lqcompare-sync-build`，`make -j2`。
- 服务 24 项通过、0 失败、0 跳过（含 init/cleanup），已包含祖先目录替换回归，并链接 Snapshot 与基线持久化实现。
- 临时目录真实验证：更新较新规则、镜像实际目录结果、SHA-256旧计划拒绝、覆盖备份还原、还原拒绝后续用户改动、临时回收区删除/撤销、阈值无确认不写、排除子树保护、无基线冲突、基线删除对修改、错误基线降级、只读失败继续、取消清单、链接替换、备份路径树内/别名拒绝。
- `python3 tools/check_layering.py` 通过。
- 四个独立套件合计 **266 通过、0 失败、0 跳过**：Sync 24、Snapshot 114、SyncBaseline 116、SyncView 12（均包含各自 init/cleanup）。基线套件验证真实 capture → 共同状态 → 原子 JSON 保存/加载 → 双向修改方向推断，并验证坏基线直接传入 preview 时保守降级。详细结果见各子模块记录。
- 已目检三栏预演窗口截图 `/Users/loren/Desktop/Work/LqCompare-build-sync-view/sync-preview.png`。macOS 原生系统回收站在本模块测试中未操作真实用户目录，使用实现真实文件移动的临时 TrashService。

## 尚未完成的规格范围与限制

尚无自动取新/取大/保留两份的冲突策略、逐条反转方向、失败项直接重试、任意过滤表达式/属性过滤、永久删除选项、脚本静默授权入口、会话预设/基线绑定自动持久化、自动更新基线、HTML/CSV 报告、日志轮转、每文件字节进度。界面支持保守冲突展示与重新预演处理，不把这些后续能力标为已完成。

同步以普通文件内容一致为目标，不镜像全部权限/扩展属性/创建时间；空目录支持创建，特殊文件和符号链接需手动处理。SHA-256复验会增加读取成本。QSaveFile避免半文件写入，但没有跨进程目录锁或跨多文件原子事务；并发修改检测会拒绝已观察到的变化，不能宣称文件系统层面的事务隔离。Files 当前不暴露 inode/volume identity；尤其 Linux 无创建时间时，目录身份校验会退化，不能声称覆盖所有目录替换竞态。基线绑定有序路径与规则摘要，尚未保存可跨重启核验的物理卷/目录标识。Windows/MinGW 与 Linux 尚未在本轮机器上编译运行。
