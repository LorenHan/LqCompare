QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_sync
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/Sync/sync.pri)
include($$CODE_ROOT/Services/Snapshot/snapshot.pri)
SOURCES += $$CODE_ROOT/Services/Filter/mask.cpp $$CODE_ROOT/Services/Filter/maskfilter.cpp
SOURCES += $$PWD/tst_sync.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
