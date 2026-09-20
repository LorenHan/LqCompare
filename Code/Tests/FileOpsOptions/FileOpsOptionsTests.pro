# -----------------------------------------------------------------------------
# 文件操作的默认行为测试（PRD: OPT-005）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * **刻意**写 `QT -= gui`：`fileopsoptions.{h,cpp}` 全是纯数据（标识符与枚举
#     的互转、三张阈值/策略的判定、键表的自检）。一旦有人把 QMessageBox 之类的
#     界面依赖塞进去，本工程会立刻构建失败，而不是等某台没有图形环境的机器上
#     才发现。与 Tests/Settings、Tests/SettingsScope、Tests/ContentFilter 同一条纪律。
#
#   * `Services/Settings` 与 `Services/Log` 是**测试侧**的依赖，不是模块的依赖：
#     G 组要拿真的设置仓库与真的 `definitions()` 才能证明「策略读的键」与
#     「仓库登记的键」没有分家（少一处，界面上那个选项就永远改不动策略）。
#     `fileopsoptions.cpp` 本身不 include 任何一个，I 组的源码级护栏盯着它。
#
# `Services/Files` 整份 .pri 都要进来：fileopsoptions.cpp 与 filesystem / trash /
# batch 同属一个模块，而 qmake 不做去重，跨模块只能按「整份 .pri」或
# 「只有搜索路径」二选一——这里选整份，因为它同时提供了文件系统的类型定义。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_fileopsoptions

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Services/Settings/settings.pri)
include($$CODE_ROOT/Services/Log/log.pri)

SOURCES += $$PWD/tst_fileopsoptions.cpp
HEADERS += $$PWD/tst_fileopsoptions.h

# 供 I 组的源码级护栏定位源码文件。刻意不靠相对路径去猜：从构建目录往上数几层，
# 换个构建目录就失效，而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
