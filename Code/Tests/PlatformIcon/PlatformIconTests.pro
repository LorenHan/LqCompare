# -----------------------------------------------------------------------------
# 系统图标服务测试（PRD: PLAT-004）
#
# 与其它套件的一个区别：本工程需要 QtGui（QIcon / QPixmap / QImage），
# 因为最后三条用例走的是**真实的系统图标源**。QT += gui 由 Platform/platform.pri
# 加进来（模块自描述），QTEST_MAIN 因此在 QT_GUI_LIB 下生成 QGuiApplication。
# 运行仍然用 offscreen 平台，不需要显示器（见 Code/Tests/run-tests.sh）。
#
# 注意：坑表里那条「测试工程写 QT -= gui 会导致 QKeySequence 找不到」
# 在这里是反面案例——本套件**必须**有 gui。
#
# 前五组（缓存键 / 尺寸 / 缓存 / 请求队列 / 服务的调度）都是平台无关的纯逻辑，
# 因此在 macOS 上就已经把三种平台的规则真实跑了一遍：扩展名的折叠、
# Windows 的固定档位、缓存键里「文件 vs 目录」这一维，都不必等到
# 拿到对应平台的机器才第一次被验证。
# -----------------------------------------------------------------------------
QT += core gui testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_platformicon

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services
INCLUDEPATH += $$CODE_ROOT/Tests/Support

include($$CODE_ROOT/Services/Platform/platform.pri)
include($$CODE_ROOT/Services/Files/files.pri)

SOURCES += $$PWD/tst_platformicon.cpp
HEADERS += $$PWD/tst_platformicon.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
