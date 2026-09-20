QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_registry

CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Registry/registry.pri)
SOURCES += $$PWD/tst_registry.cpp
DEFINES += REGISTRY_FIXTURE_DIR=\\\"$$PWD/fixtures\\\"
DESTDIR = $$OUT_PWD/bin
