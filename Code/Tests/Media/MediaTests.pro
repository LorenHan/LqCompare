QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = tst_media
SOURCES += tst_media.cpp
HEADERS += fixtures.h
include(../../Services/Media/media.pri)
DESTDIR = $$OUT_PWD/bin
