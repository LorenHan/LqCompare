# -----------------------------------------------------------------------------
# 掩码语法与解析器测试（FILT-001）
#
# 本工程只 include 服务层里过滤模块的 .pri，不引入任何界面代码——这正是
# 「Services 不依赖 UI」这条铁律带来的好处：掩码可以在没有窗口的环境下测（ENG-002）。
#
# 不需要 gui：掩码只处理字符串（QChar / QString / QVector）。与 Logging 套件同样，
# 这里**刻意**写 `QT -= gui` —— 一旦哪天有人往 mask.cpp 里塞进界面依赖
# （比如为了显示而引一个 QIcon），本工程会立刻构建失败，而不是等某台
# 没有图形环境的机器上才发现。
# -----------------------------------------------------------------------------
QT += core testlib
# qmake 给 app 模板的默认 QT 里带 gui，这里显式去掉。留着「反正能编过」的
# 默认值，上面那句「一旦有人加了 QtGui 依赖就会失败」就永远不成立。
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_filter

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Filter/filter.pri)

# filter.pri 从 FILT-005 起多了一个 filterstack.cpp，它引用了 Services/Session 的
# `SessionSettings` 接口（第 4 条「视图临时过滤不写入会话」的落点靠它表达）。
# filter.pri 只给搜索路径不给源文件（两边互相 include 会让 .cpp 进两次 SOURCES），
# 所以**本工程要自己再 include 一次 Session 的 .pri**——不 include 的话，
# `SessionSettings` 的构造与虚表在这一侧找不到定义，链接报未定义符号。
#
# 之所以放心的一个前提：session.pri 里的四个 .cpp 都是纯 QtCore 的
#（Tests/Settings 与 Tests/SettingsScope 同样 `QT -= gui` 地编译它们），
# 因此上面那句「本工程刻意不链接 QtGui」仍然成立。
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_filter.cpp
HEADERS += $$PWD/tst_filter.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
