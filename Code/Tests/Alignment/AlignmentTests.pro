# -----------------------------------------------------------------------------
# 对齐算法（Patience 与 Myers 回退）的测试（PRD: TXT-003）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：对齐算法全是纯逻辑（两侧若干行进去、若干块出来），
#     一旦有人把 QIcon / 控件之类的东西塞进 textdiff.cpp，本工程会立刻构建失败，
#     而不是等某台没有图形环境的机器上才发现。与 Tests/Text、Tests/Filter、
#     Tests/FilterStack 是同一条纪律。
#
# 为什么单独开一个套件而不是往 Tests/Text 里加：本条与 TXT-002 同属
# `Services/Text`，但验收的是**另一件事**——TXT-002 守的是 Myers 自己那四条标准，
# 本条守的是「换一种算法之后，同一个 `Result` 契约还成不成立」。分开之后
# 变异测试可以只重建这个套件（TXT-002 的记录里已经踩过「头文件变异不重建就漏检」
# 的坑），而 Tests/Text 那 25 条已有的绿不必跟着一起被重新验证一遍。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_alignment

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Text/text.pri)

SOURCES += $$PWD/tst_alignment.cpp

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
