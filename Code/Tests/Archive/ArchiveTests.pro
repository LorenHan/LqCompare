QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = tst_archive
include($$PWD/../../Services/Archive/archive.pri)
SOURCES += $$PWD/tst_archive.cpp
DISTFILES += $$PWD/generate_fixtures.py $$PWD/README.md
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
