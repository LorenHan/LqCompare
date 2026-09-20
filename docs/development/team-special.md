# Special 专用视图交付记录

本轮只修改 `Code/Services/Special`、`Code/Views/Special`、`Code/Tests/SpecialHex`、`Code/Tests/SpecialPicture` 和本文件。未修改 App/Session、规格、GitHub 或顶层构建。构建通过既有 exists/include 的两份 `specialviews.pri` 自动接入。

## 公共接口

- `LqCompare::HexCompareSession`，头文件 `Views/Special/hexcomparesession.h`，固定 `typeId=hex`。
- `LqCompare::PictureCompareSession`，头文件 `Views/Special/picturecomparesession.h`，固定 `typeId=picture`。
- 两者均支持 `Session(QObject *parent=nullptr)` 与 `Session(const QString &left, const QString &right, QObject *parent=nullptr)`，以及 `bool setPaths(left,right,QString *error=nullptr)`、`leftPath()`、`rightPath()`。
- 继承统一 CompareSession：`open/reload/createWidget/close`、状态/错误/进度出口。空路径的初始会话可以打开并选择文件；只提供一侧路径失败。已打开会话的换文件或重载失败保留原来成功的数据与路径。只读，`canSaveNow=false`，不会写回输入。
- HEX：`comparison()` 返回 `const Hex::Comparison &`；`currentOffset()`、`jumpToOffset(qint64,error)` 和 `jumpToOffset(QString,error)`；`first/previous/next/lastDifference`、`first/previous/next/lastByte`；`setBytesPerRow(8/16/32/64)`。
- IMG：`leftDocument()/rightDocument()/comparison()`；`setComparisonOptions(Picture::CompareOptions,error)`、`comparisonOptions()`；视图负责缩放、模式及叠加透明度。

## 本轮实际能力

HEX 服务以绝对偏移对齐原始字节，统计差异字节、连续差异区域和区域首尾；插入/删除导致后续偏移差异，不伪造结构对齐。左右双窗格按可见行绘制偏移、HEX、ASCII，双方差异字节和缺失字节均高亮，短侧标记 EOF；垂直滚动联动。文件头尾、差异块导航、F7/Shift+F7、字节方向键、十进制/0x跳转可用；越界保持原位并显示原因。定位同步使当前 HEX 字节进入水平与垂直可视区域。行宽设置保存在 `hex.bytesPerRow`。

HEX 每侧上限512MiB，1MiB分块流入私有临时文件；显示读取临时快照，因此比较成功后源文件修改或删除不会让字节与索引不同步。差异索引每字节1 bit，100MiB输入为12.5MiB，512MiB上限为64MiB。事务式重载保留旧数据，峰值为128MiB位图+约2MiB扫描缓冲；磁盘快照初次最多1GiB，重载最多2GiB。页面读取最多1MiB，无整文件HEX文本展开。加载期间文件大小/mtime变化会拒绝提交，但不是文件系统原子时点快照；保留mtime的并发写入不能保证检测。

IMG 按原始像素显示与比较，支持并排、洋葱皮叠加、差值图，保留原始尺寸，统一缩放和平移，透明区/缺失区棋盘格。解码后的8位直通RGBA逐通道比较，阈值明确；Alpha可参与、忽略或只比较Alpha，包括全透明像素的隐藏RGB。不同尺寸按左上对齐，缺失区域独立计数，不与真实透明像素混淆。元信息展示尺寸、解码深度、Alpha、DPI及色彩空间；不做EXIF方向或色彩空间自动转换，更不覆盖输入。图片格式由Qt实际解码器决定；未知/损坏文件、Qt解码器识别出的多帧/动画明确失败，APNG另检查动画控制块、MPO/MPF另检查JPEG APP2块后拒绝。未声称识别所有插件未报告的多图容器。

IMG 默认每文件64MiB、单图和联合画布8,388,608像素、单边32768像素硬限制；超限明确拒绝，不暗中缩图后声称原像素相同。因32位目标预算，此轮未完成8000×8000可比较验收。

## 验证

测试在macOS Qt 5.15.2 x86_64、C++17运行，独立目录构建、`make -j2`、`QT_QPA_PLATFORM=offscreen`，未运行全仓测试。

| 工程 | 最终实际结果 | 覆盖 |
| --- | --- | --- |
| `Code/Tests/SpecialHex/SpecialHexTests.pro` | 57 passed / 0 failed / 0 skipped，459ms（含init/cleanup与数据行） | 全256字节、空文件/长短两侧、绝对偏移插删、随机oracle、跨1MiB区域、极值导航、bounded read、100MiB真实视图、513MiB拒绝、快照隔离、失败保旧状态、只读、跳转/键盘/滚动/宽行可见、生命周期 |
| `Code/Tests/SpecialPicture/SpecialPictureTests.pro` | 18 passed / 0 failed / 0 skipped，211ms（含init/cleanup） | 相同/单像素/阈值边界、隐藏RGB/Alpha、尺寸联合区域、失败事务、PNG元信息/内容识别、损坏/超限、GIF/APNG/MPO拒绝、只读及重载、空会话、设置重算、视图模式/缩放/联动/错误 |

HEX最新100MiB运行记录：载入63ms，四次稀疏差异导航21ms，位图13,107,200字节（12.5MiB）；这是本机生成文件的缓存/磁盘环境结果，不是跨平台性能承诺。未测全进程RSS或精确512MiB边界。

已实际查看HEX和IMG的并排、叠加、差值图截图；修复了HEX宽行横向定位、已销毁会话残留视图、导航极值溢出。截图路径默认使用`QDir::tempPath()`，测试不依赖Windows上的`/tmp`目录。`python3 tools/check_layering.py`通过。

可复现构建（两个目录分别执行，不能覆盖其他团队构建目录）：

```sh
mkdir -p /tmp/lqcompare-special-hex-build
cd /tmp/lqcompare-special-hex-build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/SpecialHex/SpecialHexTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./bin/tst_specialhex

mkdir -p /tmp/lqcompare-special-picture-build
cd /tmp/lqcompare-special-picture-build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/SpecialPicture/SpecialPictureTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./tst_specialpicture
```

完整日志：`/tmp/lqcompare-special-hex-build/test-results.txt`、`/tmp/lqcompare-special-picture-build/test-results.txt`。

## 仍未完成的验收

- HEX-001～003、008～010仅本轮只读基础与相关测试覆盖；PE/VA地址、结构/手工对齐、差异列表、拆分单字节导航、查找、编辑/撤销、搬运、导出等未做。不能将HEX整组规格标为完成。
- HEX 大文件载入/扫描、IMG解码/比较在调用线程执行；当前100MiB实测很快，但慢盘/上限文件仍可能阻塞界面；后台进度/取消是后续工作。
- IMG 单个总差异边界框不等于连通区域聚合；差异区域列表/导航、闪烁、高亮覆盖、指定区域忽略、总通道容差、图像编辑/保存、完整元数据与导出、多帧均未完成。
- IMG 8000×8000原尺寸比较因内存限制拒绝，未声称满足大图规格；原始高位深色彩转换为8位比较，不支持高位深精度验收。
- 此轮只在macOS Qt5.15.2运行；Windows MinGW32及完整应用入口/菜单接线由协调任务做集成验收。
