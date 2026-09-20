# -----------------------------------------------------------------------------
# 路径名称处理测试（PRD: PLAT-007）
#
# 本工程只依赖 QtCore + QtTest。
#
# 全部用例都是纯逻辑（字节编解码、Unicode 规范化、文件名校验），因此**在任意
# 平台上都真实执行**——包括那些只在 Linux 上才会遇到的输入（无效 UTF-8 字节）。
# 这是把平台规则写成纯函数的又一次应用，与 pathutils 里的 Windows 路径规则同理。
#
# 另有两个真实文件系统用例：一个验证含首尾空格与引号的名字能真的建出来再读回来，
# 一个验证保真的字节编码能操作**无效 UTF-8 名字的文件**（只在 Linux 上成立，
# 在 macOS 上会跳过——HFS+/APFS 不接受无效 UTF-8 的文件名）。
# 后者在 CI（Ubuntu）上会真实执行。
# -----------------------------------------------------------------------------
QT += core testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_pathname

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services
INCLUDEPATH += $$CODE_ROOT/Tests/Support

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Tests/Support/support.pri)

SOURCES += $$PWD/tst_pathname.cpp
HEADERS += $$PWD/tst_pathname.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
