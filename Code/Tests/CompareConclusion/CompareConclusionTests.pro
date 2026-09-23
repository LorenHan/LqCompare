# -----------------------------------------------------------------------------
# 文本比对结论与 BOM 处理的测试（PRD: TXT-015）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层文本模块的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：策略表、判定与 BOM 字节全是纯逻辑
#     （几个事实进去、一个结论出来），一旦有人把 QComboBox 之类的东西塞进
#     compareconclusion.cpp，本工程会立刻构建失败，而不是等某台没有图形环境的
#     机器上才发现。与 Tests/Text、Tests/Similarity、Tests/TextRules 是同一条纪律。
#
# 为什么单独开一个套件而不是往 Tests/Text 里加：两者同属 `Services/Text`，
# 但验收的是**另一件事**——Tests/Text 守的是「两份文件的字节与行怎么解出来」，
# 本套件守的是「这一对文件到底算什么结论，以及 BOM 这一维怎么参与判定」。
# 分开之后变异测试可以只重建这个套件（「头文件变异不重建就漏检」那个坑，
# 见 handoff §6），而 Tests/Text 那批已有的绿不必跟着一起被重新验证一遍。
#
# 标准第 2 条里「在状态栏说明」那一半与第 3 条的端到端字节断言不在本套件：
# 它们要真的会话与真的文件，落在 Tests/TextView。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_compareconclusion

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Text/text.pri)

SOURCES += $$PWD/tst_compareconclusion.cpp
HEADERS += $$PWD/tst_compareconclusion.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
