# -----------------------------------------------------------------------------
# 状态着色与图标的测试（PRD: DIR-012）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include `Services/Folder` （连同它需要的 Files / Filter），不引入界面；
#   * **刻意**写 `QT -= gui`。配色表、对比度与色觉模拟全是纯算术
#     （十六进制进去、数字出来），一个 `QColor` 都不需要。一旦有人把 QColor
#     之流塞进 statuspalette.cpp，本工程会立刻构建失败，而不是等某台没有
#     图形环境的机器上才发现。与 Tests/EntryStatus、Tests/AttributeFilter 同一条纪律。
#
# 为什么单独开一个套件而不是往 Tests/Folder 里加：本条验收的是**另一件事**。
# Tests/Folder 守的是「引擎扫出来什么、界面把它画成什么」，本工程守的是
# 「这套配色表自身合不合规」——表自检、对比度判据、色盲判据与配色文件往返。
# 分家的好处在变异测试上直接体现：改 statuspalette.cpp 只需重建这一个套件。
#
# 第 4 条（切换方案立即重绘、不需重新扫描）**必须**链接 QtWidgets，
# 因此它的用例在 `Tests/Folder` 的 J 组里，不在本工程。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_statuspalette

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Folder/folder.pri)

SOURCES += $$PWD/tst_statuspalette.cpp
HEADERS += $$PWD/tst_statuspalette.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
