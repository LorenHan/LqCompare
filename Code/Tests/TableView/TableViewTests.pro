QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = tst_tableview
include(../../Services/Table/table.pri)
include(../../Views/Table/tableview.pri)
INCLUDEPATH += ../../Services/Session ../../Views/Session
HEADERS += ../../Services/Session/session.h ../../Views/Session/comparesession.h
SOURCES += ../../Services/Session/session.cpp ../../Views/Session/comparesession.cpp tst_tableview.cpp
DESTDIR = $$OUT_PWD/bin
