# -----------------------------------------------------------------------------
# 日志轮转与诊断包测试（PRD: OPT-010 第 2、3、5 条）
#
# 本工程**刻意**写 `QT -= gui`：轮转与诊断包全是「文件 + 字符串」，
# 「打开日志目录」那种界面动作留在设置页里。一旦有人把 QMessageBox / QFileDialog
# 塞进这两个模块，本工程会立刻构建失败，而不是等某台没有图形环境的机器上才发现。
# 与 Tests/Logging、Tests/FileOpsOptions、Tests/ContentFilter 同一条纪律。
#
# 只 include 服务层里日志模块的 .pri：本套件不碰界面、不碰设置仓库，
# 「默认值两边一致」那类断言住在 Tests/Options（那边才同时看得到定义表）。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_logdiagnostics

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Log/log.pri)

SOURCES += $$PWD/tst_logdiagnostics.cpp
HEADERS += $$PWD/tst_logdiagnostics.h

# 供 I 组的源码级护栏定位源码文件。刻意不靠相对路径去猜：从构建目录往上数几层，
# 换个构建目录就失效，而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
