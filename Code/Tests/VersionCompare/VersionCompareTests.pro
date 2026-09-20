QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = tst_versioncompare
SOURCES += tst_versioncompare.cpp \
           ../../Services/Version/versioncompare.cpp
HEADERS += ../../Services/Version/versioncompare.h \
           ../../Services/Version/versioninfo.h
DESTDIR = $$OUT_PWD/bin
