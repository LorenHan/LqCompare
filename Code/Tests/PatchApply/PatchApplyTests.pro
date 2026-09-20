# -----------------------------------------------------------------------------
# 补丁应用测试（PAT-002 / PAT-005）
#
# 只 include 服务层的 .pri，不引入任何界面代码——应用补丁是纯服务层动作，
# 因此这个套件可以在没有图形环境的机器上跑（QT -= gui 是刻意的）。
#
# 本工程要同时 include Text 与 Patch 两个 .pri：patch.cpp 依赖 Text 的
# 对齐比较（textdiff.h），而 patch.pri 只加搜索路径、不嵌套 include 兄弟模块
# （见 services.pri 里那条「一个 .pri 里有两份同样的源文件」的坑）。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle

TARGET = tst_patchapply

include(../../Services/Text/text.pri)
include(../../Services/Patch/patch.pri)

SOURCES += tst_patchapply.cpp

DESTDIR = $$OUT_PWD/bin
