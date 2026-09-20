# -----------------------------------------------------------------------------
# 内容过滤器的测试（FILT-004）
#
# 本工程按「服务层模块各自可无界面测试」这条铁律组织（ENG-002）：
#
#   * 只 include 服务层的 .pri，不引入任何界面代码；
#   * **刻意**写 `QT -= gui`：内容过滤全是纯逻辑（行匹配、字节搜索、声明解析、
#     三张表的自检）。一旦有人把 QIcon / QMessageBox 之类的界面依赖塞进
#     contentfilter.cpp，本工程会立刻构建失败，而不是等某台没有图形环境的机器
#     上才发现。与 Tests/Filter、Tests/FilterStack、Tests/AttributeFilter、
#     Tests/NameFilter、Tests/Settings 是同一条纪律。
#
# 三个 .pri 都要显式 include，原因有两条：
#
#   1. filter.pri 只给 `../Session` 的**搜索路径**、session.pri 只给 `../Filter`
#      的搜索路径，谁也不 include 谁（否则 mask.cpp 会以两份 SOURCES 进同一个
#      Makefile，qmake 不去重）。而本工程 include 整份 filter.pri，其中的
#      filterstack.cpp 要用 `SessionSettings` 表达三层落点，所以 Session 必须
#      在自己的 .pri 里再进来一遍。
#   2. `Services/Text` 是**测试侧**的依赖，不是模块的依赖：第 3 条那句
#      「先过滤行再应用忽略规则」要拿真的忽略规则（`Text::CompareOptions`）
#      验证才有意义。contentfilter.cpp 本身不 include 任何 Text 头文件
#      （I 组的源码级护栏盯着它），因此这里补上 text.pri 只影响本工程。
#      三份 .pri 之间没有同名源文件，不会出现重复符号。
# -----------------------------------------------------------------------------
QT += core testlib
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_contentfilter

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)
include($$CODE_ROOT/Services/Text/text.pri)

SOURCES += $$PWD/tst_contentfilter.cpp
HEADERS += $$PWD/tst_contentfilter.h

# 供 I 组的源码级护栏定位源码文件。刻意不靠相对路径去猜：从构建目录往上数几层，
# 换个构建目录就失效，而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
