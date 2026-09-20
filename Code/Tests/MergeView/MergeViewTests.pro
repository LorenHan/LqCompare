QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_mergeview
include(../../Services/Text/text.pri)
include(../../Services/Merge/merge.pri)
include(../../Views/Merge/mergeview.pri)
include(../../Services/Command/command.pri)
INCLUDEPATH += ../../Views/Page
HEADERS += ../../Views/Page/commandactionbinder.h
SOURCES += ../../Views/Page/commandactionbinder.cpp
INCLUDEPATH += ../../Services/Session ../../Views/Session
HEADERS += ../../Services/Session/session.h ../../Views/Session/comparesession.h
SOURCES += ../../Services/Session/session.cpp ../../Views/Session/comparesession.cpp tst_mergeview.cpp
DESTDIR = $$OUT_PWD/bin
