# Intentionally QtCore only: CLI must remain usable without a windowing system.
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Text/text.pri)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/Cli/cli.pri)
include($$CODE_ROOT/Services/Script/script.pri)
INCLUDEPATH += $$CODE_ROOT/Services/Session $$CODE_ROOT/Services/Filter
HEADERS += $$CODE_ROOT/Services/Session/sessiontype.h $$CODE_ROOT/Services/Filter/mask.h
SOURCES += $$CODE_ROOT/Services/Session/sessiontype.cpp $$CODE_ROOT/Services/Filter/mask.cpp \
    $$CODE_ROOT/Services/Filter/maskfilter.cpp
