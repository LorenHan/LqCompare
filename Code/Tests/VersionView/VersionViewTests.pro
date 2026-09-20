QT += core gui widgets concurrent testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_versionview
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Version/version.pri)
include($$CODE_ROOT/Views/Version/versionview.pri)
INCLUDEPATH += $$CODE_ROOT/Services/Session $$CODE_ROOT/Views/Session $$PWD/../Version
HEADERS += $$CODE_ROOT/Services/Session/session.h $$CODE_ROOT/Views/Session/comparesession.h $$PWD/../Version/pefixtures.h
SOURCES += $$CODE_ROOT/Services/Session/session.cpp $$CODE_ROOT/Views/Session/comparesession.cpp $$PWD/tst_versionview.cpp
DESTDIR = $$OUT_PWD/bin
