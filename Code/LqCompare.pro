# -----------------------------------------------------------------------------
# LqCompare —— 文件与文件夹比对工具
#
# 构建基线（与 Ailecium 对齐，便于两个项目共享经验）：
#   Qt 5.15.2 / C++17 / qmake
#   Windows：MinGW 8.1.0 32 位为交付目标
#   macOS  ：clang_64，用于开发与测试
#
# 界面使用 LqRibbon（MyClass/3rd-party/LqRibbon）。若 MyClass 不在同级目录，
# 请设置环境变量 LQCOMPARE_MYCLASS_ROOT，详见 Code/ThirdParty/myclasspath.pri。
# -----------------------------------------------------------------------------
QT += core gui widgets

TEMPLATE = app
CONFIG += c++17
CONFIG += highdpi

# 与 Ailecium 保持同一套 Qt 基线，避免两套代码路径。
!equals(QT_MAJOR_VERSION, 5): error("LqCompare currently requires Qt 5.15.")
lessThan(QT_MINOR_VERSION, 15): error("LqCompare currently requires Qt 5.15.")

TARGET = LqCompare
VERSION = 0.1.0

# 构建信息注入（ENG-012）：版本号不手工维护在代码里。
DEFINES += LQCOMPARE_VERSION=\\\"$$VERSION\\\"

# -----------------------------------------------------------------------------
# 输出目录
#
# 交付目录刻意不放进源码树，也不被 git 跟踪：任何 checkout 都不应该把用户手上的
# 可执行文件打回旧构建（参考 Ailecium docs/PRD-actions.md #338）。
# -----------------------------------------------------------------------------
isEmpty(DESTDIR) {
    win32: DESTDIR = $$clean_path($$PWD/../dist/windows)
    macx: DESTDIR = $$clean_path($$PWD/../dist/macos)
    else: DESTDIR = $$clean_path($$PWD/../dist/linux)
}

win32: RC_ICONS = $$PWD/Pictures/ribbon_about.svg

macx {
    CONFIG += app_bundle
    QMAKE_TARGET_BUNDLE_PREFIX = org.lqcompare
}

unix:!macx {
    target.path = /usr/local/bin
    INSTALLS += target
}

# -----------------------------------------------------------------------------
# 模块构建描述
#
# 依赖方向：App -> Views -> Services。Services 不反向依赖 UI（ENG-001 强制检查）。
# 各 .pri 自带 INCLUDEPATH += $$PWD，可被测试工程单独 include 后自足编译（ENG-002）。
# -----------------------------------------------------------------------------

# --- 应用装配层 --------------------------------------------------------------
include($$PWD/App/app.pri)

# --- 视图层 ------------------------------------------------------------------
include($$PWD/Views/views.pri)

# --- 服务层 ------------------------------------------------------------------
include($$PWD/Services/services.pri)

# --- 资源 --------------------------------------------------------------------
RESOURCES += $$PWD/Pictures/Pictures.qrc

# --- 第三方 ------------------------------------------------------------------
include($$PWD/ThirdParty/lqribbon.pri)

# -----------------------------------------------------------------------------
# 翻译（ENG-008）
#
# 构建时用 lrelease 生成 .qm，由 UI-030 负责运行时切换语言。
# -----------------------------------------------------------------------------
TRANSLATIONS += \
    $$PWD/LqCompare_CN.ts \
    $$PWD/LqCompare_EN.ts
