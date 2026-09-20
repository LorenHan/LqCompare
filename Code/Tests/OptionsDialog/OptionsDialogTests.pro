QT += core gui widgets testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_optionsdialog
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Log/log.pri)
include($$CODE_ROOT/Services/Settings/settings.pri)
include($$CODE_ROOT/Views/Options/options.pri)
SOURCES += $$PWD/tst_optionsdialog.cpp
