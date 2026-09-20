QT += core gui widgets concurrent testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_vcsview
include(../../Services/Vcs/vcs.pri)
include(../../Views/Vcs/vcsview.pri)
SOURCES += tst_vcsview.cpp
DESTDIR = $$OUT_PWD/bin
