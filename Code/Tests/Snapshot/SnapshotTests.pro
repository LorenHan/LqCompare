QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_snapshot

CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Snapshot/snapshot.pri)

SOURCES += $$PWD/tst_snapshot.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
