# 媒体标签只读视图回归

本工程只构建 `Services/Media`、`Views/Media` 及会话基类，不运行其它团队的测试。
Qt 5.15.2、C++17、qmake；并行编译上限 `make -j2`。

```sh
cd /Users/loren/Desktop/Work/LqCompare/Code/Tests/MediaView
mkdir -p build
cd build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../MediaViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen MEDIA_VIEW_SCREENSHOT="$PWD/media-view.png" ./tst_mediaview -o media-view-tests.txt,txt
```

`MEDIA_VIEW_SCREENSHOT` 为可选的截图绝对路径。启用时，中文标签用例对实际创建的
`MediaCompareView` 调用 `show()`、处理事件，再通过 `grab()` 保存 1100×800 窗口截图。
它不是设计稿或模拟图片。所有测试文件均生成于 `QTemporaryDir`，无需外网或音频设备。

## 覆盖范围

- 会话类型 `media`、空会话、关闭后的拒绝行为、视图复用及只读保存能力。
- 中文文件名与中文 ID3v2 标签、字段改变、仅左/仅右、空值与缺失值的区分。
- 多值字段的顺序呈现、只读全值详情与不可编辑的表格模型。
- ID3v1 Latin-1 解释提示；尝试保存被拒绝且原文件字节保持不变。
- 技术参数开关与 `media.ignoreTechnical` 设置同步、清空设置后恢复默认行为。
- `file_size` / `format` 等比较服务合成字段的显示，开关不隐藏两侧文件大小摘要。
- 重载左侧损坏、右侧已更改的场景：两侧均重新读取、旧标签清除、失败侧明确不可用。
- 两侧均不支持时不会给出相同结论；读取失败分别给出左右原因。
- 无 ID3 标签的 MP3 `Partial` 状态不会被呈现为完整标签相等。
- FLAC Vorbis 中文/空值/多值标签以及采样率技术参数差异。
- 长值的表格预览有界，完整值可在下方只读详情中查看；预览转义换行、制表和格式控制字符。
- 路径输入与 Compare / Reload 控件、初次失败后的重试、会话销毁后视图禁用。

语料以最小合法标签结构为主，没有可播放音频。测试验证的是标签读取与视图契约，
不验证音频解码、可播放性或音频内容身份。界面常驻说明标签相同不意味着音频相同。

## 验证记录与边界

2026-09-20 在本机 macOS（QtTest 报告 osx 26.6）使用 Qt 5.15.2 x86_64 完成独立构建，
`QT_QPA_PLATFORM=offscreen` 测试汇总 **13 passed / 0 failed / 0 skipped**，
其中 11 项为业务用例，另外两项为 QtTest 初始化与清理。
服务层修复 UTF-16 comment / TXXX 描述分串及统一端序后，已对当前源码再次执行
`make -j2` 并重跑，结果仍为 **13 passed / 0 failed / 0 skipped**。

- 测试日志：`build/media-view-tests.txt`。
- 最后一次增量构建日志：`build/media-view-build.txt`，无编译 warning / error。
- 实际离屏截图：`build/media-view.png`；已人工检查，无正文重叠，中文及左右字段值可读。
- qmake 提示 Qt 5.15.2 只测试过 macOS 10.15 SDK，而本机构建使用 macOS 26.5 SDK；构建实际成功，未关闭该提示。
- 截图用例出现离屏插件 `This plugin does not support propagateSizeHints()` 警告；截图成功、测试通过，未屏蔽该警告。
- 未执行 Windows MinGW 32 位构建、Windows UI、真实 macOS 原生文件选择器与交互验收。
- 未执行总应用、共享 `.pri` 集成或其它测试套件；这些由主协调负责。

当前支持范围以 `Media::supportDescription()` 和每侧读取状态为准。OGG、M4A/AAC、WAV、
MP4、MKV、封面、播放、波形、标签编辑/写回和报表仍未实现，不可把本回归通过视为 MED 全量验收。

## 集成接口

协调任务接入 `Code/Services/Media/media.pri` 和 `Code/Views/Media/mediaview.pri`，并确保现有
`Views/Session` 与 `Services/Session` 已接入。此测试未改动共享构建文件。

`LqCompare::MediaCompareSession` 提供默认及左右路径构造函数，`setPaths`、`leftPath`、
`rightPath`、`leftDocument`、`rightDocument`、`comparison`、`hasReadAttempt`、
`ignoreTechnical` / `setIgnoreTechnical`，以及 `pathsChanged` / `comparisonChanged` 信号。
数据访问为 const 引用，只读会话始终禁止保存。初次读取失败通过 `open()` 重试，
已打开会话通过 `reload()` 重读当前路径。失败会替换两侧读取结果，不保留旧标签。
