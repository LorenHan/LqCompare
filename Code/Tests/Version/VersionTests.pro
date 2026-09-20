QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_version

CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$CODE_ROOT/Services/Version
HEADERS += $$CODE_ROOT/Services/Version/versioninfo.h $$PWD/pefixtures.h
SOURCES += $$CODE_ROOT/Services/Version/versioninfo.cpp $$PWD/tst_version.cpp
DESTDIR = $$OUT_PWD/bin
