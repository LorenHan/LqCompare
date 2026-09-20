# -----------------------------------------------------------------------------
# 三层过滤叠加的测试（FILT-005）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * FilterStackTests 只 include 服务层的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：整个过滤模块都是纯逻辑，一旦有人把 QIcon 之类的
#     界面依赖塞进 filterstack.cpp，本工程会立刻构建失败，而不是等某台没有
#     图形环境的机器上才发现。
#
# 与 Tests/Filter 不同的是，本工程还**必须**链接 Services/Session：
# FILT-005 第 4 条（「视图临时过滤不写入会话」）要拿真正的 `SessionSettings`
# 存储来断言，用一个测试替身替不掉这件事——替身会连「存到哪个存储」
# 一起替掉，于是断言退化成「替身照着我的期望返回」。
#
# 两个 .pri 都要 include：filter.pri 只给 `../Session` 的**搜索路径**不给源文件,
# session.pri 只给 `../Filter` 的搜索路径不给源文件，因此这里显式各 include 一次
# 才是完整的（这也是 tests 目录里既有的做法，见 Tests/Settings 的 .pro）。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_filterstack

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_filterstack.cpp
HEADERS += $$PWD/tst_filterstack.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
