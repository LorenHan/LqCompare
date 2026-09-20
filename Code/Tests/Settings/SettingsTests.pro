# -----------------------------------------------------------------------------
# 会话设置声明、草稿与询问策略的测试（SESS-006）
#
# 本工程是**纯 QtCore** 的，`QT -= gui` 是刻意的：第 4 条完成标准要求
# 「任一会话设置 Tab 均可在无界面测试中被单独构造与读写」——这句话只有在一个
# 真的没有图形的工程里跑过才算数。设置对话框本身（`Views/Session/settingsdialog`）
# 另有 Tests/SettingsDialog 覆盖，那一个是链接 QtWidgets 的。
#
# 一旦哪天有人往 settingschema.cpp 里塞进界面依赖（比如为了显示去引一个 QIcon），
# 本工程会立刻构建失败，而不是等到某台没有图形环境的机器上才发现。
# 与 Tests/Logging、Tests/Filter、Tests/SessionType 是同一条纪律。
# -----------------------------------------------------------------------------
QT += core testlib
# qmake 给 app 模板的默认 QT 里带 gui，这里显式去掉。留着「反正能编过」的
# 默认值，上面那句「一旦有人加了 QtGui 依赖就会失败」就永远不成立。
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_settings

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

# Filter 在前、Session 在后，两个 .pri 都得显式 include：
#
# session.pri 只把 Filter 目录加进搜索路径（让 #include "mask.h" /
# "maskfilter.h" 能找到），刻意**不** include filter.pri —— services.pri 会把
# 两者都 include 一遍，再嵌一层就会让 mask.cpp 以两份 SOURCES 进到同一个
# Makefile。代价是单独构建本工程时必须自己补上这一行，否则链接报
# 「Mask::compile 未定义」。
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_settings.cpp
HEADERS += $$PWD/tst_settings.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
