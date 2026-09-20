# Requires Services/Filter and Services/Session once in the containing project.
QT += widgets concurrent
INCLUDEPATH += $$PWD $$PWD/../../Services/Filter $$PWD/../../Services/Session
HEADERS += $$PWD/filtersettingswidget.h
SOURCES += $$PWD/filtersettingswidget.cpp
