# 会话设置接口（SESS-001 的契约条目 sessionSettings）。
#
# 这里只有接口与一个内存实现：作用域链（SESS-007）、落盘与设置对话框（SESS-006）
# 会在后续 issue 里补，接口不变。因此本模块只依赖 QtCore，能被只链接
# QtCore 的测试套件直接覆盖。
INCLUDEPATH += $$PWD

HEADERS += $$PWD/session.h
SOURCES += $$PWD/session.cpp
