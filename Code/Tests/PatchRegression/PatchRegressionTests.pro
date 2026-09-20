QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_patch_regression
include(../../Services/Text/text.pri)
include(../../Services/Patch/patch.pri)
SOURCES += tst_patch_regression.cpp
DISTFILES += fixtures/git-index.diff fixtures/svn-index.diff \
             fixtures/gnu.diff fixtures/hg.diff fixtures/truncated.diff
DESTDIR = $$OUT_PWD/bin
