QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_script
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Text/text.pri)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/Cli/cli.pri)
include($$CODE_ROOT/Services/Script/script.pri)
INCLUDEPATH += $$CODE_ROOT/Services/Session
SOURCES += $$CODE_ROOT/Services/Session/sessiontype.cpp \
    $$CODE_ROOT/Services/Filter/mask.cpp \
    $$CODE_ROOT/Services/Filter/maskfilter.cpp \
    $$PWD/tst_script.cpp
DESTDIR = $$OUT_PWD/bin
