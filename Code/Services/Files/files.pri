# 文件系统服务抽象层（PLAT-002、PLAT-003、PLAT-007、PLAT-008）。
#
# 本模块刻意分成五部分，便于单独理解与替换：
#   filesystem.{h,cpp}       —— 接口、时间类型、错误分类与原始系统码（平台无关）
#   pathutils.{h,cpp}        —— 路径规则与文件名校验（平台无关的纯逻辑，可在任一平台测试）
#   pathname.{h,cpp}         —— 路径名称的字节保真、Unicode 规范化与安全显示（平台无关）
#   trash.{h,cpp}            —— 回收站服务接口 + XDG 路径规则（平台无关部分）
#   batch.{h,cpp}            —— 批量操作的失败清单、重试与进度（平台无关）
#   filesystem_<平台>.cpp    —— 真正调用系统 API 的薄层
#   trash_<平台>.<ext>       —— 回收站的平台搬移实现
#
# 为什么回收站单独一组文件而不是塞进 filesystem_<平台>.cpp：
#   macOS 的回收站必须用 Foundation（NSFileManager），也就是必须写成 .mm；
#   而 filesystem_posix.cpp 是 .cpp，macOS 与 Linux 共用。混在一起就只能把
#   整个 POSIX 实现都变成 .mm，让 Linux 也跟着背 ObjC++ 的编译代价。
INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/filesystem.h \
    $$PWD/pathutils.h \
    $$PWD/pathname.h \
    $$PWD/trash.h \
    $$PWD/batch.h

SOURCES += \
    $$PWD/filesystem.cpp \
    $$PWD/pathutils.cpp \
    $$PWD/pathname.cpp \
    $$PWD/trash.cpp \
    $$PWD/batch.cpp

# 平台实现按平台三选一：各自调用平台独有的头文件，无法同时编译。
#
# 注意 OBJECTIVE_SOURCES：qmake 只有把它放在 macx 作用域里才会交给
# Objective-C++ 编译器处理。写成 SOURCES 会被当成普通 C++ 编译，
# 在 #import 处直接失败。
win32 {
    SOURCES += $$PWD/filesystem_win.cpp
    SOURCES += $$PWD/trash_win.cpp
    # SHFileOperationW 与 SHQueryRecycleBinW 在 shell32 里。
    # MinGW 与 MSVC 都认 -lshell32 这个写法。
    LIBS += -lshell32

    # 让 <windows.h> 把不带后缀的名字展开成宽字符版本。
    #
    # 本模块自己一律显式写 W 后缀（见 tools/check_winapi.py 的护栏），
    # 所以这个宏不影响我们的调用。定义它的原因是：Qt 与 <shellapi.h> 内部
    # 还有一些会按这个宏展开的地方，不定义就会在中文路径上悄悄退化成 ANSI。
    # 一句话——我们自己已经安全了，但不想让别人替我们做错的默认选择。
    DEFINES += UNICODE _UNICODE
} else:macx {
    SOURCES += $$PWD/filesystem_posix.cpp
    OBJECTIVE_SOURCES += $$PWD/trash_mac.mm
    LIBS += -framework Foundation
} else {
    SOURCES += $$PWD/filesystem_posix.cpp
    SOURCES += $$PWD/trash_linux.cpp
}
