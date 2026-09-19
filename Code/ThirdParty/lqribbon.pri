# -----------------------------------------------------------------------------
# 引入 LqRibbon
#
# 与 Ailecium 保持一致：关掉 LqRibbon 自带的示例窗口（lqribbon_no_demo），
# 只把控件库编进来。
# -----------------------------------------------------------------------------
include($$PWD/myclasspath.pri)

CONFIG += lqribbon_no_demo

include($$LQRIBBONDIR/LqRibbon.pri)
