QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_text
include(../../Services/Text/text.pri)
SOURCES += tst_text.cpp
DESTDIR = $$OUT_PWD/bin
