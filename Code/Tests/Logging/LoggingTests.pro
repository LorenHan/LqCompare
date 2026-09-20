# -----------------------------------------------------------------------------
# 分级日志测试（ENG-006）
#
# 本工程只 include 服务层里日志模块的 .pri，不引入任何界面代码——这正是
# 「Services 不依赖 UI」这条铁律带来的好处：日志可以在没有窗口的环境下测（ENG-002）。
#
# 不需要 gui：日志只用 QtCore（QDateTime / QFile / QThread / QElapsedTimer）。
# 与别的套件不同，这里是**刻意**不写 `QT += gui` —— 一旦哪天有人往 logging.cpp
# 里加了 QtGui 的依赖，本工程会立刻构建失败，而不是等某台没有图形环境的机器上才发现。
# -----------------------------------------------------------------------------
QT += core testlib
# qmake 给 app 模板的默认 QT 里带 gui，这里显式去掉。留着「反正能编过」的
# 默认值，上面那句「一旦有人加了 QtGui 依赖就会失败」就永远不成立。
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_logging

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Log/log.pri)

SOURCES += $$PWD/tst_logging.cpp
HEADERS += $$PWD/tst_logging.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
