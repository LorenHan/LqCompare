# -----------------------------------------------------------------------------
# 回收站与可逆删除服务测试（PRD: PLAT-003）
#
# 本工程和 FileSystemTests 一样只依赖 QtCore + QtTest。
#
# 特别说明：这个套件在 macOS 上跑，但其中的 **XDG 回收站路径规则**用例
# （该用哪个废纸篓目录、.trashinfo 的编码与日期格式）同样是**真实执行**的。
# 这是把 XDG 规范里最容易写错的那部分（纯字符串逻辑）放进平台无关层的直接收益：
# Linux 的回收站规则不必等到 Linux 机器上才第一次被验证——与 pathutils.h
# 里放 Windows 路径规则是同一个策略。
#
# 另有一组是真实实现用例：会真的把文件移进本机废纸篓再还原回来。
# 它们比任何替身都更能说明「删除确实可逆」，但也在用户的废纸篓上动手，
# 因此每个用例都带清理守卫，失败路径同样会尽力把文件收回来。
# Windows 当前后端拒绝未验证可恢复性的删除：只跳过两条真实恢复用例，删除前即跳过。
# Shell 位置、无文件路径的成功报告、NotSupported 能力边界仍分别执行断言；
# 不会为了测试而向 Windows 回收站投放无法自动收回的文件。
# -----------------------------------------------------------------------------
QT += core testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_trash

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services
INCLUDEPATH += $$CODE_ROOT/Tests/Support

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Tests/Support/support.pri)

# Windows 保守后端不依赖 WinAPI；其他平台也执行同一生产实现的拒绝删除回归。
!win32: SOURCES += $$CODE_ROOT/Services/Files/trash_win.cpp

SOURCES += $$PWD/tst_trash.cpp
HEADERS += $$PWD/tst_trash.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
