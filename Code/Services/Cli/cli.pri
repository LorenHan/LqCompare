# QtCore-only CLI parser and execution. Text, Folder, Files and Session/Filter
# are included by services.pri; standalone tests include these dependencies.
INCLUDEPATH += $$PWD $$PWD/../Text $$PWD/../Folder $$PWD/../Files $$PWD/../Session $$PWD/../Filter
HEADERS += $$PWD/clioptions.h $$PWD/cliexecution.h $$PWD/processarguments.h
SOURCES += $$PWD/clioptions.cpp $$PWD/cliexecution.cpp $$PWD/processarguments.cpp
win32 {
    SOURCES += $$PWD/processarguments_win.cpp
    LIBS += -lshell32
}
