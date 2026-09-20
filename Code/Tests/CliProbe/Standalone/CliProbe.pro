QT += core
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = lqcompare_cli_probe
include(../../Cli/cli-test-dependencies.pri)
SOURCES += $$PWD/../main.cpp
HEADERS += $$PWD/../cliprobe.h
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
