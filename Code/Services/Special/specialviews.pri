# Image decoding and pixel math require QtGui, without QWidget dependencies.
QT += gui
INCLUDEPATH += $$PWD
HEADERS += $$PWD/hexdiff.h $$PWD/hexsearch.h $$PWD/picturediff.h
SOURCES += $$PWD/hexdiff.cpp $$PWD/hexsearch.cpp $$PWD/picturediff.cpp
