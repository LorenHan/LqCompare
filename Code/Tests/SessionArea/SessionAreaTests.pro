QT += core gui widgets testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_sessionarea
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Views/Session/sessionview.pri)
include($$CODE_ROOT/Views/Shell/shell.pri)
SOURCES += $$PWD/tst_sessionarea.cpp
DESTDIR = $$OUT_PWD/bin
