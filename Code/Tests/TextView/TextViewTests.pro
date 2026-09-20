QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_textview
include(../../Services/Text/text.pri)
include(../../Views/Text/textview.pri)
INCLUDEPATH += ../../Services/Session ../../Views/Session
HEADERS += ../../Services/Session/session.h ../../Views/Session/comparesession.h
SOURCES += ../../Services/Session/session.cpp ../../Views/Session/comparesession.cpp tst_textview.cpp
DESTDIR = $$OUT_PWD/bin
