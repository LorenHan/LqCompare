QT += core gui widgets testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_optionsdialog
CODE_ROOT = $$clean_path($$PWD/../..)
include($$CODE_ROOT/Services/Log/log.pri)
include($$CODE_ROOT/Services/Settings/settings.pri)
# 文件操作那几个选项的中文标签由 Services/Files 提供（见 optionsdialog.cpp 的
# fileOpsChoiceLabel）。只加搜索路径不够——链接期要符号，所以整份 .pri 都要进来。
include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Views/Options/options.pri)
SOURCES += $$PWD/tst_optionsdialog.cpp
