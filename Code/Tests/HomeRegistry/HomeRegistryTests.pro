# Exercise the real HomePage without importing App or the rest of Shell.
QT += core gui widgets testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_homeregistry

CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += \
    $$CODE_ROOT/Views/Shell \
    $$CODE_ROOT/Services/Session \
    $$CODE_ROOT/Services/Filter

HEADERS += \
    $$CODE_ROOT/Views/Shell/homepage.h \
    $$CODE_ROOT/Services/Session/sessiontype.h \
    $$CODE_ROOT/Services/Filter/mask.h
SOURCES += \
    $$CODE_ROOT/Views/Shell/homepage.cpp \
    $$CODE_ROOT/Services/Session/sessiontype.cpp \
    $$CODE_ROOT/Services/Filter/mask.cpp \
    $$PWD/tst_homeregistry.cpp

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
