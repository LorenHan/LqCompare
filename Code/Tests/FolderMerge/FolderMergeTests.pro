QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_foldermerge
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/FolderMerge/foldermerge.pri)
SOURCES += $$PWD/tst_foldermerge.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
