# MED 只读媒体标签模块交付

本轮完成独立的 MP3 / 原生 FLAC 标签读取、字段差异与 `CompareSession` 视图。
这是 MED-001、MED-006 的部分实现，不是 MED 全部验收完成。

## 接入

由主协调在共享工程中接入，媒体任务没有修改共享 `.pri`、App、Session 或 GitHub：

- 服务：`Code/Services/Media/media.pri`，只依赖 QtCore，无新第三方库。
- 视图：`Code/Views/Media/mediaview.pri`，依赖上项、现有 Services/Session、Views/Session、QtWidgets。
- 工厂：包含 `mediacomparesession.h`，创建 `new LqCompare::MediaCompareSession(parent)`，`typeId` 固定为 `media`。
- 路径：`MediaCompareSession(left, right, parent)`，或 `setPaths(left, right, &error)` 后 `open(&error)`。
- 只读：`canSaveNow()` 恒为 false；没有音频解码、写标签或外部进程路径。

`MediaCompareSession` 还提供 `leftPath/rightPath`、`leftDocument/rightDocument`、`comparison`、
`hasReadAttempt`、`ignoreTechnical` 和 `setIgnoreTechnical`；信号为 `pathsChanged`、`comparisonChanged`。
忽略技术参数保存在会话设置键 `media.ignoreTechnical`，默认 false。
工厂注册、全局路径路由、会话持久化与顶层构建由主协调独立集成验收。

## 服务与数据语义

`Media::load(path, Document*, error, ReadLimits)` 无论成功失败都会替换输出文档。
成功包括 Ready 和 Partial；失败保留路径、文件大小、格式（若可识别）与诊断，但清除已读字段，
避免“前半段读到了，所以看起来像完整结果”的误导。错误区分不支持、损坏、I/O 和资源上限。
两侧读取彼此独立；即使左侧损坏，右侧也会读取并显示。

`Media::compare(left, right, ignoreTechnical)` 按 Unicode 字符串精确比较，保留多值顺序，
区分相同、不同、仅左和仅右。空字符串字段与缺失字段不同。不会做大小写折叠、Unicode 归一化、
genre 语义换算或音频相同性判定。`Comparison.complete` 仅说明本次支持的读取没有已知遗漏，
绝不代表整份媒体文件、PCM 样本、所有可能存在的标签方案或音频内容一致。

界面提供两侧文件选择/路径、Compare、Reload、忽略技术参数开关、文件大小/支持性/诊断、
只读差异表及选中字段的完整值详情。表格预览有长度上限；缺失、空值、不可用分别呈现。
Partial 时明确完整标签相等性未知；读取失败时结果显示不可比较。常驻说明：
**标签相同不代表音频相同，音频没有被解码或比较。**

## 已实现范围

- MP3 ID3v1/v1.1：Latin-1、固定文本字段、track、原始 genre 数字 ID。
  不自动猜测 GBK 等历史本地编码；明确提示这一点。
- MP3 ID3v2.2、v2.3、v2.4：对应帧长度格式、常用文本帧、未知文本帧保留原 ID、
  COMM/TXXX、UTF-8、UTF-16 BOM/UTF-16BE（按版本限制）、中文与代理对、多值、
  全标签或逐帧去同步、扩展头、footer、grouping、data length indicator。
- UTF-16 描述与正文分别检查；空描述可仅含终止符，正文可自带 BOM 或继承同帧描述字节序。
  损坏字节、缺少必要 BOM、不一致字节序不使用替换字符掩盖。
- 注释语言保留：英文空描述用 `comment` 通用键；其它语言/非空描述写入键，避免语言差异丢失。
- 同时有 v2 与 v1 时 v2 作为通用显示值，冲突 v1 保留在 `id3v1:` 字段，缺失通用字段从 v1 补齐。
- 非文本帧（含图片/歌词）、压缩或加密帧会跳过并标记 Partial；CRC 未验证、更新标签语义未解析也为 Partial。
  不存在已支持 ID3 标签的可识别 MPEG Layer III 文件标记 Partial，避免把未知标签方案说成不存在。
- 原生 FLAC：STREAMINFO 与 Vorbis comment，严格 UTF-8、键大小写统一、重复键/多值保留，
  支持中文、未知字段；采样率、声道、位深、总采样数、可计算时长（毫秒）与 vendor。
  文件大小、格式也是可选择忽略的技术行。
- FLAC 图片、应用/未知元数据块未比较，标记 Partial；音频帧及声明 PCM MD5 未验证。
  常规 padding/seektable 仅跳过，不宣称比较了它们的内容。

默认上限：元数据 8 MiB、单个文本帧/字段 256 KiB、帧/标签或文本值总数 4096、FLAC 块 256。
读取前检查文件/块/帧边界，检查 synchsafe 位、字段计数与编码；大音频载荷不整文件读取。
解析同步执行，元数据大小有界；没有实现后台解析或大文件性能承诺。

## 真实验证

独立 qmake 工程：

```sh
mkdir -p Code/Tests/Media/build
cd Code/Tests/Media/build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../MediaTests.pro
make -j2
./bin/tst_media -o results.txt,txt
```

```sh
mkdir -p Code/Tests/MediaView/build
cd Code/Tests/MediaView/build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../MediaViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen MEDIA_VIEW_SCREENSHOT="$PWD/media-view.png" ./tst_mediaview -o media-view-tests.txt,txt
```

服务最终结果：**378 passed / 0 failed / 0 skipped**，其中含 256 个固定种子的截断/变异数据行；
并非 378 个独立验收项。日志为 `Code/Tests/Media/build/results.txt`。
视图最终结果：**13 passed / 0 failed / 0 skipped**（11 项业务用例，加 QtTest 初始化/清理）；
日志为 `Code/Tests/MediaView/build/media-view-tests.txt`。
已实际使用 Qt 5.15.2 / C++17、x86_64、macOS 26.6，服务无 GUI/音频依赖，视图 offscreen。
qmake 发出了 macOS 26.5 SDK 新于旧 Qt 已测试 SDK 的提示；没有删除该提示或将其视为平台认证。
截图测试有 offscreen `propagateSizeHints()` 警告。

服务使用程序生成的极小 fixture：v1/v2、无已支持标签、中文、超长/多值、格式边界、损坏与资源限制；
检查只读文件在成功、损坏和不支持输入下原字节、修改时间、owner 写权限不变。
另有 256 个可复现截断/变异数据行，检查不崩溃、失败清空旧字段、原文件保持；
它是有界损坏回归，不是覆盖任意输入的证明、专用模糊测试器结果或性能基准。
视图测试直接检查表格值与服务差异结果一致、可见状态、缺失与空值、参数开关、重载失败清旧、
两侧刷新、会话生命周期、只读属性，并生成实际 QWidget 截图。
截图 `Code/Tests/MediaView/build/media-view.png` 已视觉检查：双侧中文与差异行、完整值详情和常驻说明可读。
这不是播放、真实音频解码、整应用交互或 Windows 验证。

## 明确未完成的验收

| 规格 | 当前结论与剩余工作 |
| --- | --- |
| MED-001 | 部分：MP3/原生 FLAC 标签、字段表、有限 FLAC 技术参数、忽略技术差异、错误原因。M4A/AAC、OGG、WAV、MP4、MKV；MP3 时长/码率等完整参数未实现。 |
| MED-002 | 未实现：标签编辑/保存、备份、复制、写入后复读。只读不可当作写标签失败安全验收。 |
| MED-003 | 未实现：封面缩略图、歌词/章节与资源导出。遇到相关数据明确 Partial。 |
| MED-004 | 未实现：播放、波形、频谱、长音频可视化。 |
| MED-005 | 未实现：媒体报表、文件夹媒体内容策略、批量/同步写标签。 |
| MED-006 | 部分：格式/中文/多值/损坏/只读 fixture 与视图一致性；写标签往返、写入失败、更多容器/真实音频语料未验证。 |

Windows MinGW 8.1.0 32 位、Linux、真实音频设备、全量应用构建/运行、全量 runner、
音频载荷完整性及性能基准均未由本任务验证。没有外网、许可证未审依赖或 GitHub 变更。
