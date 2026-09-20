QT += core
INCLUDEPATH += $$PWD
HEADERS += $$PWD/registrycompare.h
SOURCES += $$PWD/registrycompare.cpp $$PWD/registryprovider.cpp
win32 {
    SOURCES += $$PWD/registryprovider_win.cpp
    LIBS += -ladvapi32
}
