# -----------------------------------------------------------------------------
# 视图层 Views
#
# 职责：界面呈现、用户交互、把用户意图翻译成对服务层的调用。
# 依赖方向：App -> Views -> Services。本层可以依赖 Services，反之不行。
# -----------------------------------------------------------------------------
INCLUDEPATH += $$PWD

# --- 已落地 -------------------------------------------------------------------
include($$PWD/Shell/shell.pri)       # 会话容器与 Home 页（SESS-003 / SESS-010）
include($$PWD/Page/page.pri)         # Ribbon 页面装配（UI-007 ~ UI-024）

# -----------------------------------------------------------------------------
# 以下 include 在模块目录出现之前会被 exists() 跳过。
# 预留方式与理由见 Code/Services/services.pri 顶部的说明。
# -----------------------------------------------------------------------------

# 工作流 B：会话视图
exists($$PWD/Session/sessionview.pri): include($$PWD/Session/sessionview.pri)

# 工作流 D：文本比对视图
exists($$PWD/Text/textview.pri): include($$PWD/Text/textview.pri)

# 工作流 E：文件夹比对视图
exists($$PWD/Folder/folderview.pri): include($$PWD/Folder/folderview.pri)

# 工作流 F：三方合并视图
exists($$PWD/Merge/mergeview.pri): include($$PWD/Merge/mergeview.pri)

# 工作流 G：专用视图（十六进制 / 表格 / 图片 / 媒体 / 注册表 / 版本 / 归档 / 编辑）
exists($$PWD/Special/specialviews.pri): include($$PWD/Special/specialviews.pri)

# 工作流 K：版本控制视图
exists($$PWD/Vcs/vcsview.pri): include($$PWD/Vcs/vcsview.pri)

# 工作流 M：选项与外观
exists($$PWD/Options/options.pri): include($$PWD/Options/options.pri)
