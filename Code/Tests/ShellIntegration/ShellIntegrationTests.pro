# -----------------------------------------------------------------------------
# Shell 集成测试（PRD: PLAT-005）
#
# 本套件的价值在于：Shell 集成在真正交付时才运行在 Windows 上，
# 而它**绝大部分代码不在注册表调用里**——是「写哪些键、每个键写什么值、
# 卸载时删哪些、哪些必须还原成原样、残留怎么查」。
# 这批规则最容易出错，错了又最难查（卸载漏一个键：菜单里多一项点下去报
# 找不到程序；卸载删过头：把用户原有的 .diff 关联一起删了）。
# 把存储抽成 RegistryStore 之后，这一整批规则在 macOS 上被真实执行。
#
# 窗口里那 14 个动作 × 3 类对象、共享键的备份与还原、安装失败的回滚、
# 卸载后的残留校验，全部有真实断言。
#
# win32 分支（registrystore_win.cpp）不参与本套件的构建：
# 那部分只能在 Windows 上编译，属于「已写但未验证」，见交接文档。
# 非 Windows 上 platform.pri 会编 registrystore_stub.cpp，
# 本套件有专门的用例验证它「如实说自己不可用」并给出原因与建议。
# -----------------------------------------------------------------------------
QT += core gui testlib

TEMPLATE = app
CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = tst_shellintegration

CODE_ROOT = $$clean_path($$PWD/../..)

INCLUDEPATH += $$CODE_ROOT/Services
INCLUDEPATH += $$CODE_ROOT/Tests/Support

include($$CODE_ROOT/Services/Platform/platform.pri)
include($$CODE_ROOT/Services/Files/files.pri)

SOURCES += $$PWD/tst_shellintegration.cpp
HEADERS += $$PWD/tst_shellintegration.h

isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)
