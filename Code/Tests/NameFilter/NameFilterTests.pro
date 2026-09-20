# -----------------------------------------------------------------------------
# 名称过滤器的测试（FILT-002）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：名称过滤全是纯逻辑（模式判定、组合语义、校验、
#     超时策略、预设文本）。一旦有人把 QIcon / QDialog 之类的界面依赖塞进
#     namefilter.cpp，本工程会立刻构建失败，而不是等某台没有图形环境的机器上
#     才发现。与 Tests/Filter、Tests/FilterStack、Tests/AttributeFilter、
#     Tests/Settings 是同一条纪律。
#     注意 `QThread` / `QSemaphore` / `QRegularExpression` 都属于 QtCore，
#     因此超时保护那条**不**需要 QtGui。
#
# 但本工程**必须**链接 Services/Session：filter.pri 里的 filterstack.cpp
# 要用 `SessionSettings` 表达三层落点。两个 .pri 都要显式 include——
# filter.pri 只给 `../Session` 的**搜索路径**、session.pri 只给 `../Filter`
# 的搜索路径，谁也不 include 谁（否则 mask.cpp 会以两份 SOURCES 进同一个
# Makefile）。Tests/FilterStack 与 Tests/AttributeFilter 都照做了。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_namefilter

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_namefilter.cpp
HEADERS += $$PWD/tst_namefilter.h

# 供 I 组的源码级护栏与 E 组的「按值捕获」护栏定位源码文件。刻意不靠相对路径
# 去猜：从构建目录往上数几层，换个构建目录就失效，而那种失效表现为
# 「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
