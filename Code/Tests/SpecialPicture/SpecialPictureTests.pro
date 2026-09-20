QT += core gui widgets testlib
CONFIG += testcase console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_specialpicture
INCLUDEPATH += ../../Services/Special ../../Views/Special ../../Views/Session ../../Services/Session
SOURCES += tst_specialpicture.cpp \
    ../../Services/Special/picturediff.cpp \
    ../../Views/Special/picturecomparesession.cpp \
    ../../Views/Special/picturecompareview.cpp \
    ../../Views/Session/comparesession.cpp \
    ../../Services/Session/session.cpp
HEADERS += ../../Services/Special/picturediff.h \
    ../../Views/Special/picturecomparesession.h \
    ../../Views/Special/picturecompareview.h \
    ../../Views/Session/comparesession.h \
    ../../Services/Session/session.h
