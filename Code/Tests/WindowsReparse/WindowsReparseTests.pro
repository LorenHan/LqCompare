# 纯 QtCore 的 Windows 重解析点解析测试：三平台都真实执行，不需要链接权限。
QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_windowsreparse

CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$CODE_ROOT/Services/Files
HEADERS += $$CODE_ROOT/Services/Files/windowsreparse.h
SOURCES += $$PWD/tst_windowsreparse.cpp \
    $$CODE_ROOT/Services/Files/windowsreparse.cpp

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
