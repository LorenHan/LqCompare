# 会话视图基类（SESS-001）与会话设置对话框（SESS-006）。
#
# 本模块只放「会话基类」这一件事：具体会话类型（文本比对、文件夹比对……）在各自的
# 工作流里实现 `createView()` / `doOpen()` 等实现点，不在本目录。
#
# 依赖方向：本模块依赖 Services/Session 的设置接口与声明（Views -> Services 是允许
# 的方向），但**不得**依赖同层的具体视图（Views/Shell、Views/Page 等）——SESS-001
# 第 4 条要求基类不依赖任何具体视图头文件。
#
# 这条约束有**两层**校验，强弱不同，改本目录时要记住区别：
#   1. `Tests/Session` 的 INCLUDEPATH 里只有 Views/Session 与 Services/Session，
#      因此基类 include 界面头文件会**构建失败**。但它只覆盖「本目录之外」的
#      头文件——SESS-006 把 settingsdialog 也放进了本目录，于是
#      `comparesession.h` 里多写一行 `#include "settingsdialog.h"` 不再会让
#      那个工程失败（同目录总是找得到）。
#   2. 真正守住这条约束的是 `Tests/Session` 的 F 组用例：它按**文件名的白名单**
#      扫 `comparesession.{h,cpp}` 的 include 列表，多一个头文件就报红，
#      并对一段故意写坏的源码做反向验证。
# 也就是说：加文件进本目录时，护栏靠的是第 2 层。
INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/comparesession.h \
    $$PWD/settingsdialog.h

SOURCES += \
    $$PWD/comparesession.cpp \
    $$PWD/settingsdialog.cpp
