QT += core gui widgets testlib
CONFIG += testcase console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_mediaview
INCLUDEPATH += ../../Views/Session ../../Services/Session
include(../../Services/Media/media.pri)
include(../../Views/Media/mediaview.pri)
SOURCES += tst_mediaview.cpp \
    ../../Views/Session/comparesession.cpp \
    ../../Services/Session/session.cpp
HEADERS += ../../Views/Session/comparesession.h \
    ../../Services/Session/session.h
