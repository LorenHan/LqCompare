QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_options

# Keep persistence tests independent of QApplication and every view module.
CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$CODE_ROOT/Services
include($$CODE_ROOT/Services/Settings/settings.pri)
include($$CODE_ROOT/Services/Log/log.pri)

SOURCES += $$PWD/tst_options.cpp
DESTDIR = $$OUT_PWD/bin
