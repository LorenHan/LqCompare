QT += core gui widgets concurrent testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_filterview
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Views/Filter/filterview.pri)
# Only the existing settings store contract is needed.
HEADERS += $$CODE_ROOT/Services/Session/session.h
SOURCES += $$CODE_ROOT/Services/Session/session.cpp $$PWD/tst_filterview.cpp
DESTDIR = $$OUT_PWD/bin
