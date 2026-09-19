# -----------------------------------------------------------------------------
# 命令注册中心测试
#
# 本工程只 include Services 层的 .pri，不引入任何界面代码——这正是
# 「Services 不依赖 UI」这条铁律带来的好处：核心逻辑可以脱离界面测试（ENG-002）。
# -----------------------------------------------------------------------------
QT += core gui testlib

# gui 是必需的：Command 用 QKeySequence 表达默认快捷键（QKeySequence 属于 QtGui）。
# 但本工程不创建任何窗口，因此在 offscreen 平台下也能直接跑。
TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_commandregistry

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Command/command.pri)

SOURCES += $$PWD/tst_commandregistry.cpp
HEADERS += $$PWD/tst_commandregistry.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
