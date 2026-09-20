QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_report
include(../../Services/Text/text.pri)
include(../../Services/Report/report.pri)
SOURCES += tst_report.cpp
DISTFILES += fixtures/text-left.txt fixtures/text-right.txt
DESTDIR = $$OUT_PWD/bin
