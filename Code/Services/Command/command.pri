# 命令注册中心：Ribbon / 菜单 / 快速访问栏 / 快捷键 / 搜索栏的唯一出口（UI-024）。
INCLUDEPATH += $$PWD

# QKeySequence is QtGui data; this service deliberately has no QtWidgets dependency.
QT += gui

HEADERS += $$PWD/commandregistry.h
SOURCES += $$PWD/commandregistry.cpp \
           $$PWD/commandshortcuts.cpp
