QT = core testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_tablecompare
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Table/table.pri)
SOURCES += $$PWD/tst_tablecompare.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
