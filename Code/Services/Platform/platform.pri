# 平台集成服务（PLAT-004 系统图标起，PLAT-005 Shell 集成）。
#
# 本模块刻意分成两部分，与 Files/ 是同一套思路：
#   iconkey.{h,cpp}        —— 缓存键与尺寸规则（平台无关的纯逻辑，可在任一平台测试）
#   iconcache.{h,cpp}      —— 按类型的图标缓存 + 请求去重队列（平台无关）
#   iconservice.{h,cpp}    —— 服务：缓存 / 去重 / 异步 / 回退 + 提供者接口
#   registrystore.{h,cpp}  —— 注册表式键值存储的抽象 + 内存后端（平台无关）
#   shellintegration.*     —— Shell 集成的计划 / 安装 / 卸载 / 残留校验（平台无关）
#   *_<平台>.*             —— 真正调用系统接口的薄层
#
# 为什么键与尺寸要抽出来：本条目里最容易写错的就是「同一个键代表什么」
# （一个叫 notes.txt 的目录会不会污染所有 .txt 文件）与「该取多大尺寸」
# （高分屏上取 16 再放大就是模糊）。这两件事都是纯逻辑，抽出来之后
# 在 macOS 上就能把三种平台的规则都真实跑一遍——Windows 的错误码常量
# 是同一个手法。
#
# 为什么注册表也要抽出来：真正只有 Windows 有的其实是「存储」，
# 而「写哪些键、卸载怎么还原、残留怎么查」才是最容易出错、错了最难查的部分
# （卸载漏一个键：菜单里多一项点下去报找不到程序；卸载删过头：
# 把用户原有的 .diff 关联一起删了）。抽出来之后那一整批规则在 macOS 上
# 被真实执行，Windows 后端只剩下「一次系统调用换一次」的直译。
INCLUDEPATH += $$PWD

# 本模块要用 QtGui（QIcon / QPixmap / QImage）。
# 写在 .pri 里而不是让每个使用者自己记得加：忘了加的报错（找不到 QIcon）
# 与「这个模块是纯逻辑的」这个错觉完全对不上，很难查。
QT += gui

HEADERS += \
    $$PWD/iconkey.h \
    $$PWD/iconcache.h \
    $$PWD/iconservice.h \
    $$PWD/registrystore.h \
    $$PWD/shellintegration.h

SOURCES += \
    $$PWD/iconkey.cpp \
    $$PWD/iconcache.cpp \
    $$PWD/iconservice.cpp \
    $$PWD/registrystore.cpp \
    $$PWD/shellintegration.cpp

# 平台实现三选一。注意 OBJECTIVE_SOURCES：qmake 只有把它放在 macx 作用域里
# 才会交给 Objective-C++ 编译器，写成 SOURCES 会在 #import 处直接失败
# （与 files.pri 里 trash_mac.mm 的同一条踩坑记录）。
win32 {
    SOURCES += $$PWD/iconservice_win.cpp
    SOURCES += $$PWD/registrystore_win.cpp

    # SHGetFileInfoW / DestroyIcon 在 shell32，GetDC 在 user32，
    # GetIconInfo / GetDIBits / DeleteObject 在 gdi32；
    # 注册表那一套（RegCreateKeyExW 等）在 advapi32。
    LIBS += -lshell32 -luser32 -lgdi32 -ladvapi32
} else:macx {
    OBJECTIVE_SOURCES += $$PWD/iconservice_mac.mm
    # 非 Windows 平台都要注册表桩：让 createNativeRegistryStore() 有
    # 「永不返回 nullptr、如实说自己不可用」的契约（PLAT-005 第 5 条）。
    SOURCES += $$PWD/registrystore_stub.cpp

    # NSWorkspace 在 AppKit；UTType 在 UniformTypeIdentifiers（macOS 11+）。
    # Foundation 由 AppKit 带进来，但显式写出便于一眼看出依赖面。
    LIBS += -framework AppKit -framework UniformTypeIdentifiers -framework Foundation
} else {
    SOURCES += $$PWD/iconservice_linux.cpp
    SOURCES += $$PWD/registrystore_stub.cpp
}
