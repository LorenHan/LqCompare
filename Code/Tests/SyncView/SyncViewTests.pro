QT += core gui widgets testlib concurrent
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = tst_syncview
include(../../Services/Files/files.pri)
include(../../Services/Folder/folder.pri)
INCLUDEPATH += $$PWD/../../Services/Filter
SOURCES += $$PWD/../../Services/Filter/mask.cpp $$PWD/../../Services/Filter/maskfilter.cpp
include(../../Services/Sync/sync.pri)
include(../../Services/Snapshot/snapshot.pri)
include(../../Views/Sync/syncview.pri)
SOURCES += $$PWD/tst_syncview.cpp
DESTDIR = $$OUT_PWD/bin
