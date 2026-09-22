# -----------------------------------------------------------------------------
# 条目状态判定与语义的测试（PRD: DIR-011）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include `Services/Folder` （连同它需要的 Files / Filter），不引入界面；
#   * **刻意**写 `QT -= gui`：状态分类法、时间维度与内容证据全是纯数据
#     （一个条目进去、判据出来）。一旦有人把 QColor 之流塞进 entrystatus.cpp，
#     本工程会立刻构建失败，而不是等某台没有图形环境的机器上才发现。
#     与 Tests/AttributeFilter、Tests/NameFilter、Tests/ContentFilter 同一条纪律。
#
# 为什么单独开一个套件而不是往 Tests/Folder 里加：本条验收的是**另一件事**。
# Tests/Folder 守的是「引擎扫出来什么」，本工程守的是「这些结论各自落在哪一维、
# 哪些组合不可能出现」。分家的好处在变异测试上直接体现：改 `entrystatus.cpp`
# 只需重建这一个套件，36 条既有用例不必跟着重跑一遍。
#
# 界面那半（状态图标列、右键「为什么是这个状态」）**必须**链接 QtWidgets，
# 因此它的用例在 `Tests/Folder` 的 I 组里，不在本工程。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_entrystatus

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)

SOURCES += $$PWD/tst_entrystatus.cpp
HEADERS += $$PWD/tst_entrystatus.h

# 供「图标必须在 qrc 里登记」那条源码级护栏定位资源文件。
# 刻意不靠相对路径去猜：从构建目录往上数几层，换个构建目录就失效，
# 而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
