QT += core gui widgets testlib svg
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_commandactions
CODE_ROOT = $$clean_path($$PWD/../..)

include($$CODE_ROOT/ThirdParty/lqribbon.pri)
include($$CODE_ROOT/Services/Command/command.pri)
include($$CODE_ROOT/Services/Log/log.pri)
include($$CODE_ROOT/Views/Page/page.pri)

SOURCES += $$PWD/tst_commandactions.cpp
RESOURCES += $$CODE_ROOT/Pictures/Pictures.qrc
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
