QT += core gui widgets testlib
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tst_folder
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Views/Session/sessionview.pri)
include($$CODE_ROOT/Views/Folder/folderview.pri)
SOURCES += $$PWD/tst_folder.cpp
# 状态图标（DIR-011 第 4 条）必须能**真的加载**才谈得上「颜色之外还有图标」。
# 只断言 `statusIconKey()` 返回一个非空字符串的话，qrc 里少一行、路径写错一格，
# 用例照样绿，界面照样是空图标。与 CommandActions/AppIntegration 两个套件同一做法。
RESOURCES += $$CODE_ROOT/Pictures/Pictures.qrc
isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
