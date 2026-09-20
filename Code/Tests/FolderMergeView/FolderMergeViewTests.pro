QT += core gui widgets testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_foldermergeview
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/FolderMerge/foldermerge.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Views/Session/sessionview.pri)
include($$CODE_ROOT/Views/FolderMerge/foldermergeview.pri)
SOURCES += $$PWD/tst_foldermergeview.cpp
DESTDIR = $$OUT_PWD/bin
