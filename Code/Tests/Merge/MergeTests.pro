QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_merge
include(../../Services/Text/text.pri)
include(../../Services/Merge/merge.pri)
SOURCES += tst_merge.cpp
DESTDIR = $$OUT_PWD/bin
