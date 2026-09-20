# -----------------------------------------------------------------------------
# 批量操作的失败处置测试（PRD: PLAT-008）
#
# 本工程只依赖 QtCore + QtTest。
#
# 分四组，其中前三组都是**平台无关的纯逻辑**，因此在 macOS 上就已经真实执行了
# Windows 与 Linux 才会遇到的错误码分类与显示（fromWindowsError /
# fromCocoaError / fromSystemError）。这是「把平台规则写成接受显式参数的
# 纯函数」这一策略的又一次应用：错误映射是最容易写错、又最需要测试的地方，
# 不该等到目标平台的机器上才第一次被验证。
#
# 第四组用真实文件系统做一次端到端往返（设为只读 → 读回属性 → 解除只读），
# 并借它验证「一个条目失败不影响其余条目真的落地」。
# -----------------------------------------------------------------------------
QT += core testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_batch

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services
INCLUDEPATH += $$CODE_ROOT/Tests/Support

include($$CODE_ROOT/Services/Files/files.pri)
include($$CODE_ROOT/Tests/Support/support.pri)

SOURCES += $$PWD/tst_batch.cpp
HEADERS += $$PWD/tst_batch.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
