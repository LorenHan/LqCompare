# -----------------------------------------------------------------------------
# 文件系统服务抽象层测试（PRD: PLAT-002）
#
# 本工程只依赖 QtCore + QtTest：不引入任何界面代码，也不需要创建窗口。
# 这本身就是一条证据——文件系统抽象层与界面完全无关，可以脱离界面测试。
#
# 特别说明：本工程在 macOS 上跑，但其中的 Windows 路径规则用例（盘符、UNC、
# 长路径前缀、大小写不敏感比较）同样是**真实执行**的。这是把平台规则写成
# 纯字符串逻辑（pathutils.h）而不是埋进 #ifdef 的直接收益：Windows 的行为
# 不必等到 Windows 机器上才第一次被验证。
# -----------------------------------------------------------------------------
QT += core testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_filesystem

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services
INCLUDEPATH += $$CODE_ROOT/Tests/Support

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Tests/Support/support.pri)

SOURCES += $$PWD/tst_filesystem.cpp
HEADERS += $$PWD/tst_filesystem.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
