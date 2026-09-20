QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_archiveview
include(../../Services/Archive/archive.pri)
include(../../Views/Archive/archiveview.pri)
INCLUDEPATH += ../../Services/Session ../../Views/Session
HEADERS += ../../Services/Session/session.h ../../Views/Session/comparesession.h
SOURCES += ../../Services/Session/session.cpp ../../Views/Session/comparesession.cpp tst_archiveview.cpp
DESTDIR = $$OUT_PWD/bin
