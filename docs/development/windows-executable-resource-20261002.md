# Windows 主程序图标资源的构建回归

对应 PLAT-001 / [#322](https://github.com/LorenHan/LqCompare/issues/322) 与
ENG-004 / [#336](https://github.com/LorenHan/LqCompare/issues/336)。

主程序原先把 `Pictures/ribbon_about.svg` 填入 `RC_ICONS`。Qt 的该字段要求 Windows
ICO 数据，不负责把 SVG 转换为 Windows 资源。使用实际 qmake 生成的原 `.rc` 文件，
MinGW windres 明确失败：`does not contain icon data`。这解释了一个过去被私有依赖
缺失掩盖的实际程序构建障碍，而不是通过调整 CI 忽略错误。

现在把同一未改动的 SVG 直接渲染为 16/24/32/48/64/128/256 像素七个 RGBA 图层，
检入标准 `Pictures/lqcompare.ico`，仅供 Windows 可执行资源引用。现有界面 SVG、
QRC 和图标注册表均不改变；不是重新设计应用视觉。维护工具
`tools/generate_windows_icon.py` 使用 Inkscape 1.4 与 Pillow 12.3.0，应用正常构建
不需要这些转换器。工具记录了再生成、逐字节复验及负对照入口。

已完成的提交前证据：

- 原 qmake 资源实际 windres 编译失败，新完整资源编译为 i386 COFF，并链接为 PE32
  探测程序；含七项 RT_ICON、一项 RT_GROUP_ICON 和版本元数据
- 七种尺寸分别解码验证透明边缘、RGBA、目录边界；重复生成逐字节一致；七项损坏
  输入对照正确拒绝；实际查看透明图标预览
- 原 SVG 和 Pictures.qrc 与 HEAD 字节一致；Linux qmake 输出排除自动编译器
  `.qmake.stash` 依赖路径后逐字节一致，现有图标检查通过
- 本资源提交的精确生产/测试快照在 Linux 全量得到 3987 / 0 / 12；SingleInstance
  因云端 socket 限制仍整套排除。候选调色板测试另行恢复，不混入本次结果
- Linux 实际主程序重新 qmake/make 无待编译改动，真实 CLI 与生成文档/GUI 冒烟
  继续通过；此 Windows 资源变更不改变 Linux 产品代码

这证明 Linux 上的 Windows 资源交叉编译/链接，不等于原生 Windows 完整程序或
安装验收。三系统实际构建、打包、运行结果按随后对应提交的 CI 单独记录。

官方依据：[Qt 5.15 应用图标说明](https://doc.qt.io/archives/qt-5.15/appicon.html)。
