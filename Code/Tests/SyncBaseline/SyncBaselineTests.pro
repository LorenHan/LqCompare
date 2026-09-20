QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_syncbaseline

CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/Snapshot/snapshot.pri)
INCLUDEPATH += $$CODE_ROOT/Services/Sync $$CODE_ROOT/Services/Filter
HEADERS += $$CODE_ROOT/Services/Sync/syncbaseline.h $$CODE_ROOT/Services/Sync/syncengine.h
SOURCES += $$CODE_ROOT/Services/Sync/syncbaseline.cpp \
           $$CODE_ROOT/Services/Sync/syncengine.cpp \
           $$CODE_ROOT/Services/Filter/mask.cpp \
           $$CODE_ROOT/Services/Filter/maskfilter.cpp \
           $$PWD/tst_syncbaseline.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
