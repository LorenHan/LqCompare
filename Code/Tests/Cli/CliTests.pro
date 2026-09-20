QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_cli
include(cli-test-dependencies.pri)
SOURCES += $$PWD/tst_cli.cpp
HEADERS += $$PWD/../CliProbe/cliprobe.h
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
