QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_mergeoutput
INCLUDEPATH += ../../Services/Merge
HEADERS += ../../Services/Merge/mergeoutput.h
SOURCES += tst_mergeoutput.cpp \
           ../../Services/Merge/mergeoutput.cpp
DESTDIR = $$OUT_PWD/bin
