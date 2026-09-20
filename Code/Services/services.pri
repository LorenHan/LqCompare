# -----------------------------------------------------------------------------
# 服务层 Services
#
# 依赖方向铁律：App -> Views -> Services。本层**不得** include 任何 Views/ 或 App/
# 的头文件（由 tools/check_layering.py 强制）。
#
# 本层的每个模块都必须能在没有界面的情况下被单元测试直接编译与运行。
# -----------------------------------------------------------------------------
INCLUDEPATH += $$PWD

# --- 已落地 -------------------------------------------------------------------
include($$PWD/Command/command.pri)   # 命令注册中心（UI-024）
include($$PWD/Log/log.pri)           # 分级日志（ENG-006）

# -----------------------------------------------------------------------------
# 以下 include 在模块目录出现之前会被 exists() 跳过。
#
# 这是刻意预留的：并行开发时多个工作流都需要往本文件加一行 include，
# 那样必然冲突。因此按 docs/development/parallel-workstreams.md 的工作流清单
# 一次加齐，各工作流只需要新建自己的目录与 .pri，不再改本文件。
# -----------------------------------------------------------------------------

# 工作流 A：平台与文件系统底座
exists($$PWD/Platform/platform.pri): include($$PWD/Platform/platform.pri)
exists($$PWD/Files/files.pri): include($$PWD/Files/files.pri)

# 工作流 B：会话框架
exists($$PWD/Session/session.pri): include($$PWD/Session/session.pri)

# 工作流 C：文本比对引擎
exists($$PWD/Text/text.pri): include($$PWD/Text/text.pri)

# 工作流 E：文件夹比对
exists($$PWD/Folder/folder.pri): include($$PWD/Folder/folder.pri)

# 工作流 F：三方合并
exists($$PWD/Merge/merge.pri): include($$PWD/Merge/merge.pri)

# 工作流 G：专用视图的数据后端（十六进制 / 表格 / 图片 / 媒体 / 注册表 / 版本 / 归档 / 编辑）
exists($$PWD/Special/specialviews.pri): include($$PWD/Special/specialviews.pri)

# 工作流 H：过滤与文件格式
exists($$PWD/Filter/filter.pri): include($$PWD/Filter/filter.pri)
exists($$PWD/Format/format.pri): include($$PWD/Format/format.pri)

# 工作流 I：报表与补丁
exists($$PWD/Report/report.pri): include($$PWD/Report/report.pri)
exists($$PWD/Patch/patch.pri): include($$PWD/Patch/patch.pri)

# 工作流 J：文件夹同步与快照
exists($$PWD/Sync/sync.pri): include($$PWD/Sync/sync.pri)
exists($$PWD/Snapshot/snapshot.pri): include($$PWD/Snapshot/snapshot.pri)

# 工作流 K：版本控制
exists($$PWD/Vcs/vcs.pri): include($$PWD/Vcs/vcs.pri)

# 工作流 L：命令行与脚本
exists($$PWD/Cli/cli.pri): include($$PWD/Cli/cli.pri)
exists($$PWD/Script/script.pri): include($$PWD/Script/script.pri)

# 工作流 M：设置存储
exists($$PWD/Settings/settings.pri): include($$PWD/Settings/settings.pri)

exists($$PWD/Archive/archive.pri): include($$PWD/Archive/archive.pri)

exists($$PWD/Table/table.pri): include($$PWD/Table/table.pri)

exists($$PWD/Version/version.pri): include($$PWD/Version/version.pri)

exists($$PWD/Media/media.pri): include($$PWD/Media/media.pri)

exists($$PWD/Registry/registry.pri): include($$PWD/Registry/registry.pri)

exists($$PWD/FolderMerge/foldermerge.pri): include($$PWD/FolderMerge/foldermerge.pri)
