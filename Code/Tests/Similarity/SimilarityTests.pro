# -----------------------------------------------------------------------------
# 相似度分值与「相似行对齐」的测试（PRD: TXT-005）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层文本模块的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：分值、阈值判定与单调配对全是纯逻辑
#     （两行文本进去、一个数出来；两段行进去、一串下标对出来），
#     一旦有人把 QSpinBox 之类的东西塞进 linesimilarity.cpp，本工程会立刻构建失败，
#     而不是等某台没有图形环境的机器上才发现。与 Tests/Text、Tests/Alignment、
#     Tests/Filter 是同一条纪律。
#
# 为什么单独开一个套件而不是往 Tests/Text 里加：本条与 TXT-002 / TXT-003 同属
# `Services/Text`，但验收的是**另一件事**——那两条守的是「对齐算法怎么切块」，
# 本条守的是「切完之后哪些行配成一对」。分开之后变异测试可以只重建这个套件
#（TXT-003 的记录里已经踩过「头文件变异不重建就漏检」的坑），
# 而 Tests/Text 那批已有的绿不必跟着一起被重新验证一遍。
#
# 标准第 4 条（阈值与开关写入会话设置并可用于命令行）的用例在别的套件里：
# `Tests/TextView` 守会话键的往返，`Tests/Cli` 守命令行解析——那两件事都需要
# 链接界面 / 命令行模块，与上面那句「本工程刻意不链接 QtGui」冲突。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_similarity

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Text/text.pri)

SOURCES += $$PWD/tst_similarity.cpp
HEADERS += $$PWD/tst_similarity.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
