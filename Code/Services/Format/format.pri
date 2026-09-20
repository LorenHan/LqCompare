# Format definitions and bounded content detection; QtCore only.
# Standalone consumers also include Filter/filter.pri and Session/session.pri.
INCLUDEPATH += $$PWD $$PWD/../Filter $$PWD/../Session
HEADERS += $$PWD/formatdefinition.h $$PWD/formatdetector.h
SOURCES += $$PWD/formatdefinition.cpp $$PWD/formatdetector.cpp
