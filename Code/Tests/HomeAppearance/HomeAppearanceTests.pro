# 使用真实首页、会话栏和主题运行时，不依赖可选 Ribbon。
QT += core gui widgets testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_homeappearance
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Settings/settings.pri)
include($$CODE_ROOT/Services/Log/log.pri)
include($$CODE_ROOT/Views/Session/sessionview.pri)
include($$CODE_ROOT/Views/Shell/shell.pri)
INCLUDEPATH += $$CODE_ROOT/Views/Options
HEADERS += $$CODE_ROOT/Views/Options/optionsruntime.h
SOURCES += $$CODE_ROOT/Views/Options/optionsruntime.cpp $$PWD/tst_homeappearance.cpp
DESTDIR = $$OUT_PWD/bin
