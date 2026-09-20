# -----------------------------------------------------------------------------
# 会话设置对话框的测试（SESS-006）
#
# 本工程链接 QtWidgets：对话框是一个真的 QDialog，第 1 条完成标准（左侧 Tab 列表、
# 右侧内容、底部作用域下拉 + 四个按钮）只有把控件真的建出来、真的放进布局里
# 才能验。运行时用 offscreen 平台，由 run-tests.sh 统一导出
# QT_QPA_PLATFORM=offscreen —— 与 Tests/Session 一样。
#
# 与 Tests/Settings 的分工：那一个是纯 QtCore 的，覆盖声明、草稿、校验与询问策略
# （第 2、3、4 条里可无界面验证的部分）；本工程只覆盖「界面这一层」——
# 控件生成、按钮接线、询问的界面行为。两边刻意用**不同**的合成声明，
# 于是「界面由声明生成」这句话在两个方向上都受检验。
# -----------------------------------------------------------------------------
QT += core gui widgets testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_settingsdialog

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Views/Session/sessionview.pri)

# 供 E 组的源码级护栏定位 settingsdialog.cpp（它断言这个文件里没有写死任何
# 合成声明的键）。刻意不用相对路径去猜：从构建目录往上数几层，换个构建目录
# 就失效，而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

SOURCES += $$PWD/tst_settingsdialog.cpp
HEADERS += $$PWD/tst_settingsdialog.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
