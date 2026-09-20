# -----------------------------------------------------------------------------
# 三层作用域的覆盖链与写入路由的测试（SESS-007）
#
# 本工程与 Tests/Settings 一样是**纯 QtCore** 的，`QT -= gui` 是刻意的：
# 覆盖链、写入路由、丢弃视图级设置全是数据合成，一旦哪天有人往
# settingscope.cpp 里塞进界面依赖（比如为了显示去引一个 QIcon），
# 本工程会立刻构建失败。与 Tests/Logging、Tests/Filter、Tests/SessionType
# 是同一条纪律。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_settingsscope

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

# Filter 在前、Session 在后。session.pri 只把 Filter 目录加进搜索路径
# （让 #include "mask.h" / "maskfilter.h" 能找到），刻意**不** include
# filter.pri —— services.pri 会把两者都 include 一遍，再嵌一层就会让
# mask.cpp 以两份 SOURCES 进到同一个 Makefile。代价是单独构建本工程时
# 必须自己补上这一行，否则链接报「MaskFilter::parse 未定义」。
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_settingsscope.cpp
HEADERS += $$PWD/tst_settingsscope.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
