# PLAT-006: real cross-process lifecycle, election and relay tests.
# Keep this target independent of icon services / Files and the GUI modules.
QT += core network testlib
QT -= gui
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_singleinstance

CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$CODE_ROOT/Services/Platform
SOURCES += $$PWD/tst_singleinstance.cpp \
           $$CODE_ROOT/Services/Platform/instanceprotocol.cpp \
           $$CODE_ROOT/Services/Platform/singleinstance.cpp
HEADERS += $$PWD/tst_singleinstance.h \
           $$CODE_ROOT/Services/Platform/instanceprotocol.h \
           $$CODE_ROOT/Services/Platform/singleinstance.h

DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
