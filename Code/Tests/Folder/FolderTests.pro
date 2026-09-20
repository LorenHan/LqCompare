QT += core gui widgets testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_folder
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Views/Session/sessionview.pri)
include($$CODE_ROOT/Views/Folder/folderview.pri)
SOURCES += $$PWD/tst_folder.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
