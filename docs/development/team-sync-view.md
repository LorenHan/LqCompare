# 目录同步预演窗口交付记录

日期：2026-09-20。独占修改范围：`Code/Views/Sync`、`Code/Tests/SyncView` 与本文。没有修改 App、共享规格、顶层 .pri 或服务层。

## 集成接口

- 头文件：`Code/Views/Sync/syncpreviewdialog.h`
- qmake：包含 `Code/Views/Sync/syncview.pri`，增加 Qt Widgets / Concurrent。服务侧同时包含 Files、Folder、Filter、Sync、Snapshot 各自 `.pri`，由主协调装配。
- 类型：`LqCompare::SyncPreviewDialog(QWidget *parent = nullptr)`，继承 `QDialog`。
- 最短调用：创建窗口，`setDirectories(left, right)`，然后 `show()` 或 `exec()`。路径设置后 350 ms 自动生成预演；调用 `startPreview()` 可立即开始。
- `setOptions(options)`、`setBaseline(baseline)`、`clearBaseline()` 更新配置并使旧计划立即失效。
- `loadBaseline(path)`、`saveBaseline(path)` 异步操作；完成信号分别为 `baselineLoaded(bool)` / `baselineSaved(bool)`。
- `previewReady()`、`executionFinished()`、`recoveryFinished(bool)`、`busyChanged(bool)` 提供轻量集成出口。`currentPlan()`、`lastReport()`、`statusText()` 返回可审阅状态。
- `setExecutor(shared_ptr<Sync::Executor>)` 允许测试注入隔离的备份与回收站。窗口保有恢复记录后拒绝替换 Executor，避免丢失回收站撤销对象。
- `setConfirmationHandler` 用于宿主/测试提供显式确认。默认使用取消为默认项的确认框；超过删除阈值时要求输入“确认删除”。每次执行绑定 `confirmationDigest(plan)`，确认期间目录或勾选变化会拒绝执行。

## 实现行为

窗口以左目录树、按动作分组的操作清单、右目录树展示预演。更新、镜像、双向三种模式与单向方向可切换；排除子树、回收站/不删除选项、删除数量与大小阈值可调整。镜像与“不删除”的矛盾由服务返回不可执行计划。

预演、执行、备份恢复、回收站撤销、基线加载/保存通过 QtConcurrent 在后台运行，UI 用定时器读取进度；工作闭包不捕获窗口。预演/执行使用 atomic 取消。改变路径或规则立即清空旧计划；正在运行的旧预演被取消，其结果按代次丢弃。复制/删除期间配置控件锁定。关闭正在运行的操作会先请求取消并保留窗口供查看结果。

每条可执行项与整个方向组均可取消勾选，冲突和跳过项没有可执行复选框。冲突单独分组并以警告图标和颜色突出；无基线提示不推断“两侧均改”。执行前显示两个绝对根目录及复制/新建/删除影响摘要，超过删除阈值另行确认。执行完成后保留报告并重新预演，不重用确认。

报告展示每项结果、失败原因、覆盖备份与回收站实际位置、持久恢复日志路径。服务整体拒绝执行且没有逐项结果时，清单仍显示红色“整个计划 / 执行被拒绝”行。窗口保留本窗口历次覆盖备份选择入口；恢复确认显示实际目标绝对路径；回收站撤销提示最近实际删除路径与回收站位置，能力范围按服务/平台最近一批删除处理。

计划文本使用 QSaveFile 导出，并拒绝写入任一同步树。基线保存同样拒绝同步树内路径，只接受当前完整、未过滤且两侧一致的 `commonBaseline(plan)`。加载失败清除旧基线并保留可见降级说明，根/规则不匹配由后续预演继续保守处理。不会自动将失败、取消、冲突或部分预演写成基线。

## 已验证

Qt 5.15.2 / C++17，macOS 本机，`QT_QPA_PLATFORM=offscreen`。独立目录 `/Users/loren/Desktop/Work/LqCompare-build-sync-view`，`make -j2`。

```sh
cd /Users/loren/Desktop/Work/LqCompare-build-sync-view
/Users/loren/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/SyncView/SyncViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./bin/tst_syncview -v1
```

10 个行为测试 + init/cleanup，共 12 项通过、0 失败。全部文件写入隔离临时目录；删除使用会真实搬移和还原文件的临时 TrashService，不触碰系统回收站。

- 无预演执行禁用；预演只读；取消确认不写入；计划导出保护同步范围。
- 单条/方向组勾选否决，日志保留跳过项；真实复制后自动重新预演。
- 配置改变立即失效；确认期间路径改变拒绝旧摘要；运行中的旧预演结果被丢弃。
- 无基线双向内容差异产生不可执行冲突，方向控件禁用。
- 删除超阈值必须二次确认；真实临时回收站删除与撤销。
- 覆盖前备份真实内容与窗口恢复入口往返。
- 预演后外部修改导致整体拒绝，目标原样保留并可见失败报告。
- 一致目录基线原子保存/加载，禁止树内保存；损坏基线明确降级；差异目录禁止保存为共同状态。
- 取消预演不产生可执行计划。

已查看离屏窗口截图 `/Users/loren/Desktop/Work/LqCompare-build-sync-view/sync-preview.png`，确认三栏、规则、基线和执行入口可见。Qt 提示当前 macOS SDK 新于 Qt 5.15.2 验证范围；编译与本套行为测试成功。

## 尚未完成 / 交由主协调

- App/Ribbon/会话命令装配由主协调负责，本模块不修改共享入口。
- 尚未实现逐项反转方向、手工冲突策略、失败项专门重试按钮、CSV/HTML 导出、同步预设或后台自动同步。
- 基线为显式保存/加载，没有自动基线更新或 Snapshot 文件选择桥接 UI。
- 当前窗口可恢复已保留的覆盖记录；持久日志导入并恢复的独立 UI 尚未提供。
- 恢复和基线加载期间不提供中断底层文件操作的按钮；窗口会等待安全收尾。预演和同步执行可取消。
- 未在 Windows / MinGW 编译运行，也不宣称完成全部 SYNC 规格或三方 Folder Merge。
