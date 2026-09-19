# 文件系统服务抽象层（PLAT-002、PLAT-003）。
#
# 本模块刻意分成三部分，便于单独理解与替换：
#   filesystem.{h,cpp}       —— 接口、时间类型、错误分类（平台无关）
#   pathutils.{h,cpp}        —— 路径规则（平台无关的纯字符串逻辑，可在任一平台测试）
#   filesystem_<平台>.cpp    —— 真正调用系统 API 的薄层
INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/filesystem.h \
    $$PWD/pathutils.h

SOURCES += \
    $$PWD/filesystem.cpp \
    $$PWD/pathutils.cpp

# 平台实现按平台二选一：两者都调用各自平台独有的头文件，无法同时编译。
win32: SOURCES += $$PWD/filesystem_win.cpp
else: SOURCES += $$PWD/filesystem_posix.cpp
