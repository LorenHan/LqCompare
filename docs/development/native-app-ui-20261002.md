# 原生应用标签与平台回归

对应 UI-001 / [#2](https://github.com/LorenHan/LqCompare/issues/2)、UI-004 /
[#4](https://github.com/LorenHan/LqCompare/issues/4)、PLAT-001 /
[#322](https://github.com/LorenHan/LqCompare/issues/322)。

## 已证实问题与最小生产修复

完整依赖的首次 Cocoa 应用测试中，截图确认选中/聚焦的 Compare 被画成
`Comp...`。原因是只有选中状态添加边框，文字空间缩窄。生产代码只在正常规则
预留透明的左/右/上 1px 与下 2px 边框，各状态再变换颜色，避免选中改变几何。
不修改字体、内容窗口最小尺寸、原生窗口框架、搜索行为或比较数据。

Mac 原生 AppIntegration 的 34 条失败不是 34 个独立功能：三个测试函数中包含
20 条把系统调色板 alpha 216 与既有不透明 CSS 色直接比较的错误期望，12 条未
取得完整 Compare 字形，另有键盘系统策略和消息框标题假设。自定义 User 绘图
引擎在 Cocoa 还报告不支持，不能用屏蔽警告或伪装引擎类型解决。

Windows 同轮应用测试 43 / 2 / 0：注册表/版本比较本来按 WindowsOnly 契约提供，
却被测试一律判为不可用；另有 1440 顶层窗口被原生屏幕钳成 1028。两处不能靠
修改产品能力或最小宽度配合测试机。

## 保留严格性的观察与输入方法

- 在测试标签上使用同一个原生 factory 的独立 QProxyStyle，只记录并转发实际
  drawItemText；继续让真实 Qt 栅格引擎绘制。不能创建原生样式时明确失败，不换
  成 Fusion。每次观察前/中/后要求图像逐像素一致、字体及每个 tabRect 一致，
  退出时恢复继承样式，避免观察器自己制造期望结果
- 仍验证完整标签、精确实际指定 RGB、未取整的 4.5 对比度和真实字形像素。
  alpha 0/128/216/255 数据行说明既有 HexRgb 契约，原应用调色板保持原值。
  显式 ElideRight 路径覆盖每个标签选中/聚焦时不能截断
- 只有需要完整键盘遍历的测试作用域设置 AllControls，并用 RAII 与清理断言恢复
  原 Text/List/All 策略，不改产品的 TabFocus/点击焦点行为
- Mac 消息框按平台契约不依赖窗口标题；仍精确检查拥有者、标准按钮集合和正文，
  非 Mac 保留标题断言
- 800/1024/1440/1920 宽布局使用清楚标注的逻辑画布，不向宿主外投递事件冒充
  用户操作。另保留未改原生 flags/frame 的顶层窗口，在实际可用屏幕内验证完整
  鼠标命中、Tab/Backtab/Escape、主题/字体和折叠恢复；操作前检查祖先裁剪区域

## 本地冻结结果与边界

Linux Qt 5.15.15/offscreen：完整 AppIntegration 在 DPI96、DPI72、Windows 绘制
风格下各 51 / 0 / 0；相关绘制矩阵在 DPI120、125%/200% 下各 9 / 0 / 0，72DPI
重复三轮各 5 / 0 / 0。三种起始键盘策略的操作/恢复回归各 4 / 0 / 0。

独立反例全部被拒绝：去掉透明边框、错误的近白前景、4.47809 的低对比度背景，
以及保留成功文字调用却擦除截图字形。没有降低颜色阈值或像素证据要求。

实际程序单独重建，Unicode 文档 JSON 比较、帮助、错误参数，以及搜索回车、
默认 No 和 Cancel 通过，输入文件哈希未变；人工检查比较窗格和确认截图。
合并当前本地候选的全量回归为 **3994 / 0 / 12**，SingleInstance 仍因云端
socket 限制整套排除。另以全新构建完成完整 AppIntegration **ASan+UBSan 51 / 0 / 0**，
关闭当前环境不能可靠执行的泄漏检测，不宣称 LSan 通过。

这些不是新 macOS/Windows 原生运行结果；后续精确提交必须再跑原生 CI 才能确认。
也不代表完整竞品功能对齐、全桌面交互、输入法、多屏或交付安装验收。

官方依据：

- [QMessageBox 的 macOS 标题约定](https://doc.qt.io/archives/qt-5.15/qmessagebox.html#setWindowTitle)
- [Qt 5.15.2 样式表文字转发](https://github.com/qt/qtbase/blob/v5.15.2/src/widgets/styles/qstylesheetstyle.cpp)
- [QProxyStyle 所有权及转发](https://github.com/qt/qtbase/blob/v5.15.2/src/widgets/styles/qproxystyle.cpp)
- [Qt 平台键盘遍历](https://github.com/qt/qtbase/blob/v5.15.2/src/widgets/kernel/qapplication.cpp)
- [WCAG 指定颜色对比度](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)
