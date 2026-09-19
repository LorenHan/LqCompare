# -----------------------------------------------------------------------------
# 定位 LqRibbon 源码树
#
# LqRibbon 是 Ailecium 与 LqCompare 共用的 Ribbon 控件库，位于 MyClass 仓库的
# 3rd-party/LqRibbon/LqRibbonCPP。本文件只负责把它找出来；具体编译描述由
# ThirdParty/lqribbon.pri 引入。
#
# 覆盖方式（按优先级）：
#   1. qmake 变量 MYCLASS_ROOT（可用于 IDE 的构建配置）
#   2. 环境变量 LQCOMPARE_MYCLASS_ROOT
#   3. 默认相对路径 ../../../MyClass（LqCompare 与 MyClass 同级 clone 时成立）
# -----------------------------------------------------------------------------
!contains(CONFIG, lqcompare_ribbon_path_included) {
    CONFIG += lqcompare_ribbon_path_included

    isEmpty(MYCLASS_ROOT): MYCLASS_ROOT = $$(LQCOMPARE_MYCLASS_ROOT)

    isEmpty(MYCLASS_ROOT) {
        MYCLASS_ROOT = $$clean_path($$PWD/../../../MyClass)
        !exists($$MYCLASS_ROOT/3rd-party/LqRibbon/LqRibbonCPP/LqRibbon.pri) {
            MYCLASS_ROOT = $$clean_path($$PWD/../../../myclass)
        }
    }

    LQRIBBONDIR = $$clean_path($$MYCLASS_ROOT/3rd-party/LqRibbon/LqRibbonCPP)

    !exists($$LQRIBBONDIR/LqRibbon.pri) {
        error("未找到 LqRibbon 源码。请把 MyClass 与 LqCompare clone 到同级目录，或设置 LQCOMPARE_MYCLASS_ROOT 指向 MyClass 仓库根目录。当前查找路径：$$LQRIBBONDIR")
    }
}
