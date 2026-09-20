QT += core gui widgets concurrent testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_vcsblameview
include(../../Services/Vcs/vcs.pri)
# 刻意 include vcsview.pri（而不是自己再列一遍 blameview.h/.cpp）：
# 这样「顶层把 blameview 接进构建」这件事本身就被本套件覆盖——.pri 里漏掉
# 任一行，本套件立刻构建失败，而不是安静地只编测试文件。vcsview.cpp 顺带
# 也被编一次，它已有自己的套件，重复编译的代价可以接受。
include(../../Views/Vcs/vcsview.pri)
SOURCES += tst_vcsblameview.cpp
DESTDIR = $$OUT_PWD/bin
