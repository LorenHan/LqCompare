# -----------------------------------------------------------------------------
# 内置替换规则的测试（PRD: TXT-012）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层文本模块的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：规则表、开关集合与替换链全是纯逻辑
#     （一行文本进去、一行文本出来），一旦有人把 QCheckBox 之类的东西塞进
#     linereplacements.cpp，本工程会立刻构建失败，而不是等某台没有图形环境的
#     机器上才发现。与 Tests/Text、Tests/Similarity、Tests/Alignment 是同一条纪律。
#
# 为什么单独开一个套件而不是往 Tests/Text 里加：本条与 TXT-008 / TXT-009 同属
# `Services/Text`，但验收的是**另一件事**——那两条守的是「判等时用哪把尺子」，
# 本条守的是「判等之前先把文本改成什么」。分开之后变异测试可以只重建这个套件
# （「头文件变异不重建就漏检」那个坑，见 handoff §6），
# 而 Tests/Text 那批已有的绿不必跟着一起被重新验证一遍。
#
# 标准第 2 条里「界面显示其正则或匹配说明」的**界面**一半不在这里：
# 本套件只钉住「正则与说明是服务层的可测数据」，界面接通属于设置页（OPT-*）。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_textrules

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Text/text.pri)

SOURCES += $$PWD/tst_textrules.cpp
HEADERS += $$PWD/tst_textrules.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
