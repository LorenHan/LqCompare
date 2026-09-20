# -----------------------------------------------------------------------------
# 属性过滤的测试（FILT-003）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：属性过滤全是纯逻辑（大小/时间/属性位/所有者），
#     一旦有人把 QIcon 之类的界面依赖塞进 attributefilter.cpp，本工程会立刻
#     构建失败，而不是等到某台没有图形环境的机器上才发现。
#     与 Tests/Filter、Tests/FilterStack、Tests/Settings 是同一条纪律。
#
# 但本工程**必须**链接 Services/Session：第 4 条要求「与名称过滤构成整体的与
# 关系」，而名称过滤在生产里就是 `FilterStack`（FILT-005 的三层叠加），
# 它用 `SessionSettings` 表达三层声明各落在哪个存储。
#
# 两个 .pri 都要显式 include：filter.pri 只给 `../Session` 的**搜索路径**、
# session.pri 只给 `../Filter` 的搜索路径，谁也不 include 谁（否则 mask.cpp 会
# 以两份 SOURCES 进同一个 Makefile）。Tests/FilterStack 与 Tests/Settings 都照做了。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_attributefilter

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_attributefilter.cpp
HEADERS += $$PWD/tst_attributefilter.h

# 供 I 组的源码级护栏定位源码文件。刻意不靠相对路径去猜：从构建目录往上数几层，
# 换个构建目录就失效，而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
