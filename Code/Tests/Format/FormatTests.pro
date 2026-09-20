QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_format

CODE_ROOT = $$clean_path($$PWD/../..)
# Keep detection usable without any Views or QtWidgets dependency.
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Format/format.pri)
SOURCES += $$PWD/tst_format.cpp
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
