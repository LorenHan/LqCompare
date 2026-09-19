# -----------------------------------------------------------------------------
# 应用装配层 App
#
# 本层是进程入口与顶层窗口外壳，把各 View / Service 模块组装成应用：
#   - main.cpp      : 进程入口、QApplication 装配、日志初始化、命令行解析
#   - RibbonWindow  : 应用级 Ribbon 外壳（UI-001 ~ UI-006）
#   - MainWindow    : 主窗口，聚合 Ribbon 页面、会话容器、输出面板、状态栏
#
# 依赖方向：App -> Views -> Services。
# -----------------------------------------------------------------------------
INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/main.cpp \
    $$PWD/RibbonWindow.cpp \
    $$PWD/MainWindow.cpp

HEADERS += \
    $$PWD/RibbonWindow.h \
    $$PWD/MainWindow.h
