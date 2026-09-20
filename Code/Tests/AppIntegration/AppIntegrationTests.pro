QT += core gui widgets testlib svg
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_appintegration
CODE_ROOT = $$clean_path($$PWD/../..)

# Compile the actual application assembly, excluding the production entry point.
include($$CODE_ROOT/Services/services.pri)
include($$CODE_ROOT/Views/views.pri)
include($$CODE_ROOT/ThirdParty/lqribbon.pri)
INCLUDEPATH += $$CODE_ROOT/App
HEADERS += $$CODE_ROOT/App/MainWindow.h $$CODE_ROOT/App/RibbonWindow.h
SOURCES += $$CODE_ROOT/App/MainWindow.cpp $$CODE_ROOT/App/RibbonWindow.cpp $$PWD/tst_appintegration.cpp
RESOURCES += $$CODE_ROOT/Pictures/Pictures.qrc
DEFINES += LQCOMPARE_VERSION=\\\"integration-test\\\"
DESTDIR = $$OUT_PWD/bin
