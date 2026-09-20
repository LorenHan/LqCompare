# 掩码与过滤规则（FILT-001、FILT-005）。
#
# 本模块刻意分成三部分，一层一个文件：
#   mask.{h,cpp}        —— 掩码语言本身：语法、解析、匹配、语法速查（平台无关）
#   maskfilter.{h,cpp}  —— 过滤声明：包含/排除如何叠加、大小写策略、预览计数（平台无关）
#   filterstack.{h,cpp} —— 三层叠加（格式 / 会话 / 视图）与各层的落点（FILT-005）
#
# 为什么三层要分开：
#   * `mask.h` 回答「这一段掩码命中这个条目吗」——失败是「你的掩码写错了」，
#     界面要在输入框里标红那一段（所以错误带列号与长度）。
#   * `maskfilter.h` 回答「整个声明叠加之后这个条目留不留」——失败是
#     「你的规则组合得不对」，界面要在状态栏说明排除优先。
#   * `filterstack.h` 回答「三个来源一起看着一个条目，结论是什么」——失败是
#     路由问题（哪一层该存到哪儿）。合成一个返回值之后，界面就没法分别显示。
#
# 本模块依赖 QtCore，另加 **Services/Session 的存储抽象**（只有 filterstack 用到）。
# 前者保证「三种平台的大小写规则都能在 macOS 上被真实执行」——这正是 FILT-001
# 第 3 条完成标准要的东西。「Windows 默认不敏感」这条规则如果写成 `#ifdef Q_OS_WIN`，
# 在开发机上就一次都不会执行到。
INCLUDEPATH += $$PWD

# FILT-005 第 4 条（视图临时过滤不写入会话）要靠「一个键 + 多个存储」表达，而
# 「存储」这个概念只有 Services/Session 有（`SessionSettings` 接口）。
#
# **只加搜索路径，不 include session.pri**，理由与 session.pri 反向引用 Filter 时
# 完全一样：services.pri 会把 Session 与 Filter 两个 .pri 各 include 一次，两边若
# 互相 include，对方的 .cpp 就会以两份 SOURCES 进到同一个 Makefile 里 ——
# qmake 不会去重，现象是重复符号或同一份代码被编译两次。
#
# 代价是：单独构建本模块（不经过 services.pri）的工程必须自己再 include 一次
# Session 的 .pri，否则会链接报「SessionSettings 的虚表未定义」。
# Tests/Filter 与 Tests/FilterStack 都照做了，并在各自的 .pro 里写了原因。
exists($$PWD/../Session/session.h): INCLUDEPATH += $$PWD/../Session

HEADERS += \
    $$PWD/mask.h \
    $$PWD/maskfilter.h \
    $$PWD/filterstack.h

SOURCES += \
    $$PWD/mask.cpp \
    $$PWD/maskfilter.cpp \
    $$PWD/filterstack.cpp
