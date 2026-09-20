QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = tst_vcs
include(../../Services/Vcs/vcs.pri)
SOURCES += tst_vcs.cpp
DESTDIR = $$OUT_PWD/bin
