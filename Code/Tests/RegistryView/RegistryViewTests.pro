QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_registryview

CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Registry/registry.pri)
include($$CODE_ROOT/Views/Registry/registryview.pri)
INCLUDEPATH += $$CODE_ROOT/Services/Session $$CODE_ROOT/Views/Session
HEADERS += $$CODE_ROOT/Services/Session/session.h $$CODE_ROOT/Views/Session/comparesession.h
SOURCES += $$CODE_ROOT/Services/Session/session.cpp $$CODE_ROOT/Views/Session/comparesession.cpp \
    $$PWD/tst_registryview.cpp
DESTDIR = $$OUT_PWD/bin
