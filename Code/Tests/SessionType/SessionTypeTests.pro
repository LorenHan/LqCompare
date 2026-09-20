# -----------------------------------------------------------------------------
# 会话类型描述子与注册表测试（SESS-002）
#
# 本工程是**纯 QtCore** 的，`QT -= gui` 是刻意的：注册表要能被完全没有界面的
# 环境直接覆盖，而「创建工厂」只前向声明了 Views 层的 CompareSession、
# 没有 include 它——这一条正是 SESS-001 把设置接口放进服务层、SESS-002 把
# 工厂做成服务层可声明的形状之后才成立的。
#
# 一旦哪天有人往 sessiontype.cpp 里塞进界面依赖（比如为了显示去引一个 QIcon），
# 本工程会立刻构建失败，而不是等到某台没有图形环境的机器上才发现。
# 与 Tests/Logging、Tests/Filter 是同一条纪律。
# -----------------------------------------------------------------------------
QT += core testlib
# qmake 给 app 模板的默认 QT 里带 gui，这里显式去掉。留着「反正能编过」的
# 默认值，上面那句「一旦有人加了 QtGui 依赖就会失败」就永远不成立。
QT -= gui

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_sessiontype

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services

# Filter 在前、Session 在后，两个 .pri 都得显式 include：
#
# session.pri 只把 Filter 目录加进搜索路径（让 #include "mask.h" 能找到），
# 刻意**不** include filter.pri —— services.pri 会把两者都 include 一遍，
# 再嵌一层就会让 mask.cpp 以两份 SOURCES 进到同一个 Makefile。代价是单独
# 构建本工程时必须自己补上这一行，否则链接报「Mask::compile 未定义」。
include($$CODE_ROOT/Services/Filter/filter.pri)
include($$CODE_ROOT/Services/Session/session.pri)

SOURCES += $$PWD/tst_sessiontype.cpp
HEADERS += $$PWD/tst_sessiontype.h

# 供两道源码级护栏定位源码文件（homepage.cpp 与 Pictures.qrc）。
# 刻意不用相对路径去猜：从构建目录往上数几层，换个构建目录就失效，
# 而那种失效表现为「护栏静默通过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
