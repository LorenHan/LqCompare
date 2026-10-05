# -----------------------------------------------------------------------------
# 路径名称处理测试（PRD: PLAT-007）
#
# 本工程只依赖 QtCore + QtTest。
#
# 纯逻辑用例（字节编解码、Unicode 规范化、文件名校验）**在任意
# 平台上都真实执行**——包括那些只在 Linux 上才会遇到的输入（无效 UTF-8 字节）。
# 这是把平台规则写成纯函数的又一次应用，与 pathutils 里的 Windows 路径规则同理。
#
# 真实文件系统用例按平台能力创建名称：三平台验证 Unicode、空格、单引号，
# POSIX 额外创建双引号名称；双引号的纯显示/校验仍在所有平台执行。
# 无效 UTF-8 名字由 Linux CI 真实操作；不接受该名称的平台保留明确跳过。
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
