# 会话视图基类（SESS-001）。
#
# 本模块只放「会话基类」这一件事：具体会话类型（文本比对、文件夹比对……）在各自的
# 工作流里实现 `createView()` / `doOpen()` 等实现点，不在本目录。
#
# 依赖方向：本模块依赖 Services/Session 的设置接口（Views -> Services 是允许的方向），
# 但**不得**依赖同层的具体视图（Views/Shell、Views/Page 等）——SESS-001 第 4 条
# 要求基类不依赖任何具体视图头文件。这条由 Tests/Session 做编译期校验：
# 该测试工程只把 Views/Session 放进 INCLUDEPATH，因此这里多写一行
# `#include "homepage.h"` 会让测试工程直接构建失败。
INCLUDEPATH += $$PWD

HEADERS += $$PWD/comparesession.h
SOURCES += $$PWD/comparesession.cpp
