# 纯整数换算，不链接或模拟 Win32；三平台都执行同一份边界与纪元测试。
QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_windowsfiletime

CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$CODE_ROOT/Services/Files
HEADERS += $$CODE_ROOT/Services/Files/windowsfiletime.h
SOURCES += $$PWD/tst_windowsfiletime.cpp \
    $$CODE_ROOT/Services/Files/windowsfiletime.cpp

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
