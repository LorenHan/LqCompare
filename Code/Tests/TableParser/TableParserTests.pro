QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = tst_tableparser
SOURCES += tst_tableparser.cpp \
           ../../Services/Table/tabledocument.cpp
HEADERS += ../../Services/Table/tabledocument.h
DESTDIR = $$OUT_PWD/bin
