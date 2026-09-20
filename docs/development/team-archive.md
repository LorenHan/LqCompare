# ARC 只读 ZIP 目录比较交付记录

## 范围与集成

本模块独占 `Code/Services/Archive`、`Code/Views/Archive`、`Code/Tests/Archive*`。
依据 `tools/spec/d_views.py` 的 ARC-001/003/006/008 提供一个可运行的只读 ZIP 子集；不宣称完成全部 ARC 规格。

主协调已在顶层构建中引入（本模块交付时只读核对）：

- `Code/Services/Archive/archive.pri`
- `Code/Views/Archive/archiveview.pri`

会话类为 `LqCompare::ArchiveCompareSession`，类型 ID 固定为 `archive`。构造可接受 `QObject *` 或左右路径及父对象；通过 `setPaths(left, right, error)` 更换数据源，通过标准 `open/reload/close` 契约操作。

已核对主协调的 `App/MainWindow.cpp` 接入归档 factory、路径设置、会话文档左右路径保存；整应用最终构建验收由主协调执行。

服务层只依赖 QtCore，不依赖 Views。公共接口在 `archivecompare.h`：

- `Archive::readZip(path, limits, isCancelled)` 返回 `Directory`；安全或结构检查失败会清空全部条目，保留带分类、原因、条目路径及记录偏移的 `Error`。
- `Archive::normalizeEntryPath` 提供与读取一致的路径校验。
- `Archive::compare(left, right)` 返回配对条目与证据；失败的数据源不会生成看似成功的比较行。
- `Archive::metadataNotice()` 是界面必须可见的比较边界说明。

## 已实现行为

- 依内容签名读取 ZIP/JAR 的 ZIP32 central directory；支持 stored 和 deflate 条目的目录元数据，不启动命令行程序。
- 有界读取 EOCD、central directory、local headers 和 data descriptors；不会解压或写入任何条目，也不会读取整个归档入内存。
- 核对中央与本地头的原始名称、标志、算法、大小、CRC 记录和 DOS 时间；核对有签名/无签名的数据描述符。
- 拒绝越界、截断、重叠、未计数/隐藏条目及存在歧义的数据区布局，整包失败关闭。
- UTF-8 严格解码、无 UTF-8 标志时使用 CP437；支持校验通过的 Info-ZIP Unicode path extra field，过期字段按格式规范忽略。
- NFC 与分隔符规范化，去除 `.` 和多余分隔符；任何 `..`、绝对/UNC/盘符路径、控制/格式字符、设备名、尾部点/空格及保留文件名字符均明确拒绝。大小写不同的名称保留为不同条目。
- 拒绝 Unix 符号链接和特殊文件；拒绝规范化重复路径、文件/目录占用冲突、把文件当父目录的冲突。自动补全缺失的父目录。
- 限制归档字节数、中央目录内存、总条目（包括补全父目录）、路径字节与深度、声明单项/总展开字节、声明压缩比；支持调用方取消回调。
- 配对结果区分仅左、仅右、类型冲突、大小记录不同、CRC 记录不同、其他元数据不同、元数据匹配。
- 会话视图提供左右 ZIP/JAR 路径选择、成对的大小/压缩字节/CRC/时间/类型/算法列、路径文本搜索、差异筛选与前后导航。使用表模型承载大目录；路径与推导目录以平面列表展示，尚不是可折叠树。
- 只读会话始终不可保存；替换数据源或重读失败保留上次成功结果并标明失败原因，关闭清空数据并禁用遗留视图。

## 必须保留的限制

**比较依据仅为归档头中的声明数据。没有解压、逐字节比较或负载 CRC 校验。** 即使大小、CRC 和时间全部相同，也只能称“元数据匹配，内容未验证”；真实 CRC 碰撞及负载损坏仍可能具有相同目录记录。结构完整不代表压缩数据完整。

加密（含 AES/strong flags）、ZIP64、分卷、自解压/前缀代码、非 stored/deflate 算法、其他归档格式均暂不支持并明确报错。嵌套归档只显示为普通文件；没有递归展开、文件内容子视图、GBK 自动识别/编码选择、写归档、导出、解压、报表或 CLI 接线。

视图当前同步执行有预算的目录读取，未接后台线程或取消按钮；服务 API 已有取消回调。局域网/慢盘读取仍可能阻塞视图，不能把本地枚举耗时当作所有磁盘的性能保证。

为了确定性和安全，遇到危险、冲突或结构损坏条目时拒绝整包，不提供部分结果；这与完整 ARC-003 要求的逐条错误继续策略仍有差距。扩展时间戳额外字段不参与首版比较，只显示 ZIP DOS 墙钟时间（2 秒精度、无源时区）；内部借用 UTC 容器，界面不得把它换算为真实 UTC 时间。

默认预算：归档 1 GiB、central directory 32 MiB、5 万条归档记录（含推导父目录）、路径 4096 UTF-8 字节/128 层、单项声明展开 512 MiB、总声明展开 2 GiB、声明压缩比 1000。它们只约束目录读取和元数据，不代表已实现受控解压。

## 后端与许可证

生产后端由本仓 C++/QtCore 代码实现；不新增第三方二进制或源码，不链接 libz，不携带 GPL 解压组件。文件名额外字段所需 CRC32 使用本仓短循环计算，仅针对有界原始文件名。

格式依据：[PKWARE APPNOTE 6.3.10](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)。仓库不复制该规范正文。测试 fixture 生成器使用 Python 标准库 `zipfile`/`zlib`，仅在开发测试时运行，不进入应用分发。

## 验证记录

2026-09-20，macOS 26.6、Qt 5.15.2、C++17、qmake；两套测试使用独立构建目录与 `make -j2`。

- `Code/Tests/Archive/ArchiveTests.pro`：**106 passed、0 failed、0 skipped**，全套约 1.15 秒。5 万条真实 ZIP32 目录记录枚举实测 **152 ms**，测试上限为保守的 15 秒。
- `Code/Tests/ArchiveView/ArchiveViewTests.pro`：**15 passed、0 failed、0 skipped**，最终重编译回归约 274 ms；覆盖会话生命周期、只读、错误恢复、搜索/差异导航、真实 CRC 碰撞、5 万行模型的排序与筛选。
- 91 份小型实际 ZIP/恶意样本及清单合计不足 25 KiB；生成器可复现全部样本，5 万条记录的大样本只生成到 `QTemporaryDir`，不放入仓库。
- 独立解析审查后补测空 DEFLATE 目录、注释伪 EOCD/ZIP64 签名、本地 Unicode 别名的目录标志冲突，以及补充平面的不可见格式字符；所有对应问题已修复。另核对 CP437 高位字符映射全部与 Python 标准 codec 相同。
- 视图截图已检查；路径、证据说明、数值列和错误提示均可见，宽表通过水平滚动展示剩余元数据列。
- `python3 tools/check_layering.py` 与 `python3 tools/check_spec.py` 均通过。

可复现命令（分别在两个构建目录运行）：

```sh
mkdir -p /tmp/lqcompare-archive-build
cd /tmp/lqcompare-archive-build
~/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/Archive/ArchiveTests.pro
make -j2
./bin/tst_archive

mkdir -p /tmp/lqcompare-archiveview-build
cd /tmp/lqcompare-archiveview-build
~/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/ArchiveView/ArchiveViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./bin/tst_archiveview
```

尚未执行 Windows MinGW 编译和运行；未执行整应用的最终构建，也不把同步本地扫描性能推广为网络盘性能。本模块未提交、未推送、未修改 GitHub 或全局规格。
