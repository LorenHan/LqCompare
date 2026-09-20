QT += core gui testlib
QT -= widgets
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_commandshortcuts

CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Command/command.pri)
SOURCES += $$PWD/tst_commandshortcuts.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
