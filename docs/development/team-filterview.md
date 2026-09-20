# 过滤设置控件交付记录

日期：2026-09-20。独占范围：`Code/Views/Filter`、`Code/Tests/FilterView`、本文档。未修改 `Services/Filter`、App、总构建或规格。

## 可装配入口

主头文件 `Code/Views/Filter/filtersettingswidget.h`，类均位于 `LqCompare` 命名空间：

- `FilterSettingsWidget`：组合标签页，`maskWidget()` / `nameWidget()` 获取两个独立控件；`setPreviewSubjects()` 同时传入相对路径和名称样本；聚合 `filterChanged()` 信号。
- `MaskFilterWidget`：`setFilterStack()` / `filterStack()`；`setBinder(const FilterLayerBinder&)` 复制绑定关系并载入三层；`reloadFromStores()`；`setDeclaration(layer, text)`、`setLayerEnabled(layer, enabled)`；`setPreviewSubjects()` / `setPreviewNames()`；`effectivePanel()`、`diagnosePath()`、`errorText()`。
- `NameFilterWidget`：`setDeclaration()` / `declaration()`、`setCombineMode()`、`setPlatform()`、大小写覆盖；`nameFilter()` 返回含合法表达式的服务值。`setPreviewNames()` 启动后台预览，`previewChanged()` 到达后读 `previewResult()`；`isPreviewPending()` 标识当前统计尚未完成。

引入 `Code/Views/Filter/filterview.pri` 即加入控件及 `QT += widgets concurrent`。包含项目必须另外引入一次既有 `Services/Filter/filter.pri` 与 `Services/Session/session.pri`，本 .pri 不重复包含服务源文件，也不依赖 App。

```cpp
using namespace LqCompare;
auto *page = new FilterSettingsWidget(parent);
Filter::FilterLayerBinder binder;
binder.setSessionStore(sessionStore);
binder.setViewStore(viewStore); // 标签自己持有的内存设置
binder.setBuiltinDeclaration(formatMasks);
binder.setBuiltinSource(formatName);
page->maskWidget()->setBinder(binder);
page->setPreviewSubjects(subjects);
connect(page, &FilterSettingsWidget::filterChanged, consumer, [page, consumer] {
    // 从 getter 取草稿，按应用的刷新策略重建结果。
    consumer->setMaskStack(page->maskWidget()->filterStack());
    consumer->setNameFilter(page->nameWidget()->nameFilter());
});
```

示例中的 `consumer` 是装配方自己的对象与方法，不是本模块新增接口。两个服务的结论需由扫描/视图消费者显式取交集；面板分别标明两类统计，避免将名称过滤统计伪装成三层掩码统计。

## 已实现行为

掩码页提供当前层选择、可编辑的会话/视图声明、格式声明只读、三层独立启用开关、大小写策略、逐行错误与输入区波浪线高亮、排除优先与层间交集说明、合并表达式、三层状态表与总匹配/隐藏数。非法行按现有服务约定不生效，合法行持续参与预览。

“为什么看不到”输入相对路径后说明所有阻挡层，同时列出所有命中的排除规则，包括未命中包含规则的层。离线速查按钮打开本地非模态表格，完全由 `maskSyntaxReference()` 的写法、含义和可执行样本生成。

名称页使用明确前缀保存每条表达式的模式，新增栏可选精确名、通配符、正则；切换新增模式不会重解释旧规则。三种组合语义与服务文案同源，非法正则就地报错且不参与过滤。支持具名预设保存、应用、文本文件导入/导出；同名保存拒绝覆盖，非法导入保持原列表。导入操作的文件对话框明确提示替换当前预设列表。

名称预览经 120 ms 合并输入后进入后台任务，继续使用服务的默认 200 ms 超时与断路器。每批独立持有 `ThreadNameMatchRunner`，不让并发预览共享服务默认运行器；工作函数仅捕获值，不捕获控件。新输入取消旧批并以代次检查拒绝旧结果；销毁控件请求取消而不等待整批完成。运行期超时/停用问题显示于计数下方。

## 写入边界

编辑立即改变控件草稿并发 `filterChanged()`，只有“保存当前层声明”或显式 `saveLayer(layer)` 写存储。保存走原有 `FilterLayerBinder`，不能从缺失的视图存储回退写会话。格式层不可写，含语法错误的声明不可保存，清空声明会移除该层键。

现有 binder 只保存声明；启用状态与大小写策略保留在当前 `FilterStack`，并在保存成功文案中明确说明。binder 中两个存储指针是借用，必须活得比控件更久。会话落盘、标签关闭释放视图存储由装配方负责。

名称规则及预设当前由控件持有；装配方可取声明与 `presets()` 对接正式设置/预设库。导入导出使用既有 `serializeNamedNameFilters` / `parseNamedNameFilters` 文本格式，不增加另一种序列化实现。

## 已验证

独立构建目录 `build-filterview-team`：

```sh
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../Code/Tests/FilterView/FilterViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./bin/tst_filterview
```

最终 QtTest：**15 passed / 0 failed / 0 skipped**（13 个行为场景与初始化/清理）。覆盖三层交集/排除优先、最终表达式保留逐层 AND 约束、UI 开关、格式只读、实时错误且保留合法规则、借用存储的实际路由、错误草稿拒绝保存、清空键、所有阻挡层诊断、帮助与服务语料一致、三种名称模式、三种组合语义、大小写覆盖、预设文本往返/非法导入原子性、关闭活跃预览、运行中替换 50,000 项预览后仅展示最新结果，以及组合面板。

Qt 5.15.2 / C++17 / macOS x86_64 构建通过。qmake 提示宿主 SDK 26.5 超出其历史受测范围；未产生编译错误。offscreen 运行存在平台插件不支持 `propagateSizeHints()` 的提示，行为用例通过。

在 900×750 独立显示组合控件，两页均填入非法规则并截图，用 `view_image` 目检：三层表格、合并表达式、匹配数、错误说明和名称模式/预设操作均可见，无溢出或控件重叠。截图只保存在构建目录：`build-filterview-team/masks.png`、`build-filterview-team/masks.png.names.png`。可通过环境变量 `LQCOMPARE_FILTER_SCREENSHOT_PATH` 重新生成。

## 尚未验证或由装配方完成

- App/Ribbon/会话设置页最终接入及真实扫描结果联动未在本独占范围修改。
- 名称设置和预设的正式持久化仓库、启用状态与大小写策略的会话落盘由相应消费者对接。
- 掩码预览目前同步计算；超大样本/复杂掩码的耗时尚未专项测量。名称预览已做后台隔离。
- Windows 目标、真实文件对话框往返操作、系统字体/DPI 和屏幕阅读器未做平台验收。导入/导出使用的纯文本往返与错误边界已有行为测试。
- UI 没有另写正则超时机制；继续依赖已存在服务，其超时边界测试在 `Tests/NameFilter`。本轮未重跑或修改服务测试。

## 共享服务集成回归（补充）

布局复查发现既有 `FilterStack::combinedExpression()` 将不同层白名单拍平成单个 OR，和 `accepts()` 的跨层 AND 结论不一致。控件没有复制或绕过服务表达式逻辑。已新增 `effectiveExpressionPreservesEachLayerIntersection`：格式层 `*.cpp / *.h`、会话层 `main.* / helper.*`、视图层 `*.cpp`，要求保留三组 AND 约束，且 `main.cpp` 放行、`main.h` 和 `notes.cpp` 拒绝。修复前回归如预期失败；服务源由协调方在取得独占授权后修复，本套件随后全量重跑为 15/15，并重新生成截图。当前表达式与实际判定保持逐层交集一致。
