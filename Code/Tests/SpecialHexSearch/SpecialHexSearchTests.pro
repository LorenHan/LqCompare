QT += core gui widgets testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tst_specialhexsearch
CODE_ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$CODE_ROOT/Services/Special $$CODE_ROOT/Services/Session \
    $$CODE_ROOT/Views/Special $$CODE_ROOT/Views/Session
HEADERS += $$CODE_ROOT/Services/Special/hexdiff.h \
    $$CODE_ROOT/Services/Special/hexsearch.h \
    $$CODE_ROOT/Services/Session/session.h \
    $$CODE_ROOT/Views/Session/comparesession.h \
    $$CODE_ROOT/Views/Special/hexcomparesession.h \
    $$CODE_ROOT/Views/Special/hexcompareview.h
SOURCES += $$CODE_ROOT/Services/Special/hexdiff.cpp \
    $$CODE_ROOT/Services/Special/hexsearch.cpp \
    $$CODE_ROOT/Services/Session/session.cpp \
    $$CODE_ROOT/Views/Session/comparesession.cpp \
    $$CODE_ROOT/Views/Special/hexcomparesession.cpp \
    $$CODE_ROOT/Views/Special/hexcompareview.cpp $$PWD/tst_hexsearch.cpp
DESTDIR = $$OUT_PWD/bin
