# -----------------------------------------------------------------------------
# 会话抽象基类测试（SESS-001）
#
# 本工程是**本仓库第一个链接 QtWidgets 的测试套件**：会话基类的 createWidget()
# 返回 QWidget*，因此测试必须能看到 QtWidgets。运行时用 offscreen 平台，
# 由 run-tests.sh 统一导出 QT_QPA_PLATFORM=offscreen。
#
# 本工程同时承担一条**编译期护栏**（SESS-001 第 4 条：基类不依赖任何具体视图头文件）：
# INCLUDEPATH 里只有两个目录——Views/Session（会话基类自己）与 Services/Session
# （设置接口）。因此 comparesession.h 里一旦出现 `#include "homepage.h"`、
# `#include "sessionarea.h"` 这类具体视图头文件，本工程会直接**构建失败**，
# 而不是等某个人翻代码时发现。
#
# 为什么这条护栏要放在测试工程而不是主工程：主构建的 INCLUDEPATH 里有
# Views/Shell、Views/Page 等目录，同样的 include 在主工程里编得过。
# 基类对具体视图的依赖只有在「只剩自己」的环境里才暴露得出来。
#
# 第二层是 tst_session.cpp 的 F 组用例：把两个源文件的 include 列表与白名单比对。
# 两层同时存在是刻意的——编译期那层守住测试工程，源码级那层在主构建的 CI 里也会跑
# （run-tests.sh 会遍历到本套件）。
# -----------------------------------------------------------------------------
QT += core gui widgets testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_session

# 源码根目录：Code
CODE_ROOT = $$clean_path($$PWD/../..)

# 这两个 .pri 各自 `INCLUDEPATH += $$PWD`，因此搜索路径恰好是
# Views/Session 与 Services/Session —— 见文件头对护栏的说明。
include($$CODE_ROOT/Views/Session/sessionview.pri)
include($$CODE_ROOT/Services/Session/session.pri)

# 供 F 组用例定位源码文件。刻意不用相对路径去猜：从构建目录往上数几层，
# 换个构建目录就失效，而那种失效表现为「用例静默跳过」——最糟的一种绿。
DEFINES += LQCOMPARE_CODE_ROOT=\\\"$$CODE_ROOT\\\"

SOURCES += \
    $$PWD/tst_session.cpp \
    $$PWD/probesession.cpp

HEADERS += \
    $$PWD/tst_session.h \
    $$PWD/probesession.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
