# 会话框架的服务层部分。
#
#   session.{h,cpp}      —— 会话设置的抽象接口 + 内存实现（SESS-001 契约条目）
#   sessiontype.{h,cpp}  —— 会话类型描述子与注册表（SESS-002）
#
# 后续会往这里补：类型注册表之外的设置作用域链（SESS-007）、设置落盘（SESS-006）、
# 最近会话列表（SESS-009 的数据部分）。接口不变。
#
# 本模块只依赖 QtCore 与 Services/Filter 的掩码语言，因此能被只链接 QtCore 的
# 测试套件（Tests/SessionType）直接覆盖——这是 SESS-002 第 1 条的「创建工厂」
# 只前向声明 Views 层类型、不 include 它的直接收益。
INCLUDEPATH += $$PWD

# 类型掩码复用 Services/Filter 的掩码语言（mask.h / FILT-001），因此要能看到它。
#
# 这里只加**搜索路径**，不 include filter.pri：services.pri 会把 Session 与 Filter
# 两个 .pri 都 include 一遍（各自在 exists() 保护下），若 session.pri 再 include
# 一次 filter.pri，mask.cpp 就会以两份 SOURCES 进到同一个 Makefile 里。
# qmake 不会去重 —— 现象是重复符号或同一份代码被编译两次。
#
# 代价是：单独构建本模块（不经过 services.pri）的工程，必须自己再 include 一次
# Filter 的 .pri，否则会链接报「Mask::compile 未定义」。Tests/Session 与
# Tests/SessionType 都照做了，并在各自的 .pro 里写了原因。
exists($$PWD/../Filter/mask.h): INCLUDEPATH += $$PWD/../Filter

HEADERS += \
    $$PWD/session.h \
    $$PWD/sessiontype.h

SOURCES += \
    $$PWD/session.cpp \
    $$PWD/sessiontype.cpp
