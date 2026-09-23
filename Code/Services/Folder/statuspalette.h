#ifndef LQCOMPARE_STATUSPALETTE_H
#define LQCOMPARE_STATUSPALETTE_H

#include "entrystatus.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Folder {

// ---------------------------------------------------------------------------
// DIR-012 状态着色与图标
//
// 这一条规格的原文是「每类状态有默认颜色与图标，且图标随主题切换自动适配深浅；
// 提供至少 3 套配色方案（默认/高对比/色盲友好）并可切换；颜色仅作为辅助，
// 状态图标与文字提示必须同时存在」。
//
// **为什么另开一个模块，而不是继续把颜色写在 FolderCompareView::data() 里**：
// 视图里那份 `switch (status) { case Different: return QColor(dark ? "#ffbc66" : …) }`
// 有三个改不动的毛病——
//   ① 它是**第二份**状态清单。新增一档状态时没人会记得回来补一行，
//      界面于是静默少一种颜色（DIR-011 的筛选下拉踩过同一个坑）；
//   ② 它只表达得了「一套配色」。「高对比」「色盲友好」要的是把**九档一起**换掉，
//      散落的 switch 做不到，只能复制九行再改九处；
//   ③ 色值本身没有判据。「色盲友好」四个字写在界面上，但没有任何东西会红——
//      而 handoff §6 的纪律是「把那个词反过来写，谁变红？」。
//
// 所以本模块的定位是**三张表 + 两个判据 + 一个往返**：
//   · 表：`colorSchemeTable()`（三套方案）与每套里的九档 `StatusHighlight`；
//   · 判据一：对比度（WCAG 相对亮度），「不得因着色影响可读性」的可执行形式；
//   · 判据二：`colorBlindSeparation()`——把颜色投影到红绿色盲可见的那条轴上
//     再量两两距离。`colorBlindFriendly` 这一位是真的被判据守着的，
//     而不是一个装饰性字段；
//   · 往返：配色文件的序列化与解析（第 5 条「可导出为配色文件并分享」）。
//
// **颜色一律存成 `#rrggbb` 字符串而不是 `QColor`**：`QColor` 属于 QtGui，
// 存它会让本模块连带把整个 `Services/Folder` 拖进图形依赖，
// `Tests/StatusPalette` 那个刻意 `QT -= gui` 的工程就再也建不起来。
// 而「解析十六进制 → 算亮度 → 算对比度」全是纯算术，本来不需要图形栈。
// 视图那一侧在 `QColor(hex)` 处一次性转换，代价为零。
//
// 图表见 `docs/design/architecture.md` §4 的设计决策表。
// ---------------------------------------------------------------------------

// 一套配色里**单个状态**的样式。
//
// 浅色与深色两档都必须给：规格第 1 条要的是「图标随主题切换自动适配深浅」，
// 而主题在这里的唯一体现就是背景明暗。只给一档时，深色主题下那套深色字
// 会直接消失在背景里——这不是「风格问题」，是信息不可读。
struct StatusHighlight
{
    Status status = Status::Unknown;
    // `#rrggbb`，小写十六进制。两级都必须是**完整 6 位**：
    // 3 位缩写（`#abc`）与 8 位带 alpha 的写法一律拒绝——它们与 6 位写法
    // 混在一张表里时，逐值比较会得出「看着一样其实不相等」的结论。
    QString light;
    QString dark;
    // 取「浅色主题」那一档的颜色（`dark == false`）还是深色那一档。
    QString color(bool dark) const { return dark ? this->dark : light; }
};

// 一套配色方案。九档主状态**一个都不许少**（第 1 条「每类状态有默认颜色」）。
//
// 刻意**不**把图标键放进这套结构：它已经由 DIR-011 的 `statusIconKey()`
// 单一提供。在这里再存一份就是同一件事的第二份说法，两份迟早分叉，
// 而分叉的表现是「换了配色之后某一档的图标不见了」——最难归因的一类失败。
// 配色方案只回答颜色；图标由表自身保证存在（见 `validateColorSchemeTable()`）。
struct ColorScheme
{
    // 机器可读标识（设置键、配色文件、命令行）。**纯 ASCII 小写 + 连字符**：
    // 含非 ASCII 的字面量用 `const char *` 承载会被当成 Latin-1 逐字符解释，
    // 比较永远不相等（中文单位那一轮踩过，见 handoff §6）。
    QString identifier;
    QString displayName;
    QString description;
    // 「这套方案是按色盲友好设计的」。为真时 `validateColorSchemeTable()`
    // 会强制 `colorBlindSeparation()` 过阈值——这一位不是注释，是判据的开关。
    bool colorBlindFriendly = false;
    // 本套方案要求的最低文字/背景对比度。默认与色盲友好用 WCAG AA 的 4.5，
    // 高对比方案用 AAA 的 7.0。做成字段而不是按标识符硬编码，
    // 是为了让「高对比」这个说法也能被度量，而不是只靠名字自证。
    double minimumContrast = 4.5;
    // 行背景的**假定色**。对比度必须对着一个确定的背景才谈得上，
    // 而实际背景由 Qt 调色板决定（`QPalette::Base`），这里存的是两个参考点。
    QString lightBackground;
    QString darkBackground;
    // 「不在本次比较范围内」的条目（被扫描掩码排除）用的弱化色。
    //
    // 它**不是**一个主状态，因此不参与第 1 条的「每类状态」计数。
    // 但它**照样受对比度约束**——「已排除（未比较）」也是用户要读的一句话，
    // 读不清就等于没有。刻意不留「弱化色可低于下限」的例外：例外本身
    // 需要有东西守得住「确实弱到该弱的程度」，而那件事没有客观判据。
    //
    // 唯一多出来的一条约束是它必须与 `Same` 不同色：两者同色时用户分不清
    // 「两边确实一样」与「根本没比」，而这两件事的处置完全相反。
    QString excludedLight;
    QString excludedDark;

    QVector<StatusHighlight> highlights;

    QString excludedColor(bool dark) const { return dark ? excludedDark : excludedLight; }
    // 找不到该档时返回空字符串，由调用方决定回退（视图回退到 Qt 默认前景色）。
    QString colorFor(Status status, bool dark) const;
    bool coversEveryStatus() const;
};

// 三套配色方案，顺序即界面下拉的顺序，**第一套是出厂默认**。
//
// 三套的浅色档颜色与 DIR-011 上线时视图里那份硬编码的 switch **逐值相同**
// （`TypeConflict` 一处除外，见下），因此本条目对默认外观是零回归的：
// 唯一有意改掉的是 `TypeConflict`——它在上线时与 `Error` 共用 `#aa2424`，
// 而第 1 条要求「每类状态有默认颜色」，共用等于其中一档没有自己的颜色。
const QVector<ColorScheme> &colorSchemeTable();

// 出厂默认方案的标识符。**三处（视图初始值、设置缺省值、配色文件缺省回退）
// 都从这里取**，各自写一个字符串字面量就有三份说法。
QString defaultColorSchemeIdentifier();

// 按标识符取方案。**认不出来时回落到出厂默认**，不返回空：
// 标识符有三个来源（设置键、配色文件、界面数据），任一处写错或来自旧版本，
// 界面就会拿到一套没有颜色的方案，表现为「整个列表变成一种颜色」——
// 那是最难归因的一种失败。回退是刻意的，与 `recursionTierOf()` 的
// 「反查总在三档里落一格」同一条纪律。
const ColorScheme &colorSchemeByIdentifier(const QString &identifier);

bool isKnownColorScheme(const QString &identifier);

// ---------------------------------------------------------------------------
// 判据一：对比度（WCAG 2.x 相对亮度）
// ---------------------------------------------------------------------------
//
// 规格第 3 条「不得因着色影响可读性（对比度要求）」是这段话里唯一带副词的
// 一句——「不得影响可读性」反过来写就是「得可以看不清」，而没有任何东西会红。
// 这里把它做成一对可度量的函数。
//
// 两个函数对**非法输入**返回 `-1`（而不是 0 或 1）：`0` 是「纯黑」的合法亮度，
// 拿它当错误值会让「颜色写错了」被读成「这个颜色很暗」。判据用负数当哨兵，
// 调用方一眼能看出这是「算不出来」而不是「算出来是 0」。

// 非法颜色返回 -1。
double relativeLuminance(const QString &color);
// WCAG 对比度，取值 1.0（同色）～ 21.0（黑白）。任一颜色非法或两者都非法时返回 -1。
double contrastRatio(const QString &first, const QString &second);

inline constexpr double kMinimumReadableContrastRatio = 4.5; // WCAG AA（正文）
inline constexpr double kStrongReadableContrastRatio = 7.0;  // WCAG AAA（正文）

// ---------------------------------------------------------------------------
// 判据二：色盲友好
// ---------------------------------------------------------------------------
//
// 「色盲友好」若只写成一个布尔字段，它就是一句没人验过的声明。红绿色盲
// （deuteranopia / protanopia）把整个色彩空间压到一条蓝—黄轴加明度上，
// 于是「橙 vs 绿」「红 vs 棕」这两组在正常视觉下差得很远的颜色会**并成一种**。
// 本仓既有那份硬编码配色里，`LeftOnly`(#2055a0) 与 `BothChanged`(#5a3fa0)
// 在 deuteranopia 投影后的欧氏距离只有 **2.8/255**——两个状态看起来一模一样，
// 而这恰恰是「色盲友好」这套方案存在的理由。
//
// 模拟采用 Viénot/Brettel 那一族在业界通行的矩阵（线性 sRGB → LMS，
// 令缺失的锥细胞响应由另两种线性组合补出，再变回来）。刻意**不用**简单的
// 「灰度化」近似：灰度已经由 DIR-011 的中性灰描边图标覆盖了，这里要回答的是
// 「色觉障碍用户能不能把这两个状态的颜色分开」，灰度化回答不了这个问题。

enum class ColorVisionDeficiency { Deuteranopia, Protanopia };

// 投影到该色觉障碍下所能看到的颜色。输入非法时返回空字符串。
QString simulateColorVisionDeficiency(const QString &color, ColorVisionDeficiency kind);

// 一套方案在某一主题下，**九档两两之间**投影后的最小 sRGB 欧氏距离
// （两种色觉障碍各算一遍，取更小的那个）。数值域是 0～约 441。
//
// 返回 -1 表示**算不出来**：方案没有覆盖全部九档，或有非法色值——
// 「缺一档」与「两档靠得太近」是两件事，前者由 `validateColorSchemeTable()`
// 单独报，混进这个数里会让「少了一档」被读成「颜色不够分」。
double colorBlindSeparation(const ColorScheme &scheme, bool dark);

// 一套方案要自称色盲友好，投影后的最小间距至少要有这么多。
//
// 20 不是从哪个标准抄来的整数，而是三个实测值的分界（写在这里免得下一轮
// 有人凭直觉改它）：本仓默认方案 = **2.8**、高对比方案 = 4.7、
// 色盲友好方案 = **26.2 / 23.2**（浅色 / 深色）。阈值取 20 时
// 「反例确实会红」与「真方案有余量」同时成立。
inline constexpr double kMinimumColorBlindSeparation = 20.0;

// ---------------------------------------------------------------------------
// 表自检
// ---------------------------------------------------------------------------
//
// **表是参数**：从模块内部读表会让「故意写坏的表喂进去也永远绿」
// （NameFilter 那一轮踩过，见 handoff §6）。状态表同样当参数传进来，
// 因为「每类状态有图标」这件事横跨两张表——只喂一套坏配色进去，
// 验证不出「图标那一侧被删空了」。
//
// 查七件事：
//   ① 至少三套方案，且规格点名的三套（默认 / 高对比 / 色盲友好）都在；
//   ② 标识符非空、唯一、机器可读（小写字母 / 数字 / 连字符）；
//   ③ 展示名与说明非空；
//   ④ 每套方案恰好覆盖每档主状态**一次**（缺一档与重复一档都要报）；
//   ⑤ 每个色值都是合法 `#rrggbb`，且**浅深两档背景本身的明暗必须相反**
//      （两档背景都亮 = 没有深色主题那一档，第 1 条的后半句当场落空）；
//   ⑥ 每档状态在状态表里都有非空图标键（第 3 条「图标与文字必须同时存在」），
//      且方案的最低对比度不低于 AA、九档颜色**与「已排除」弱化色**都达到
//      该方案自己的阈值，弱化色还必须与「相同」不同色；
//   ⑦ 自称色盲友好的方案必须真的过 `kMinimumColorBlindSeparation`，
//      且 `color-blind-safe` 这个名字不许不使用这一位。
//      **刻意不做**「必须比默认方案更分得开」的相对比较：默认方案的实测
//      间距是 2.8，远低于阈值 20，那条分支在出厂表上永远走不到——
//      走不到的分支就是死代码。那句话改由 `Tests/StatusPalette` 直接断言
//      两张表的值来守。
QStringList validateColorSchemeTable(const QVector<ColorScheme> &schemes,
                                     const QVector<MainStatusDescriptor> &statuses = mainStatusTable());

// ---------------------------------------------------------------------------
// 配色文件（第 5 条：自定义颜色可导出为配色文件并分享）
// ---------------------------------------------------------------------------
//
// 格式选 JSON，与本仓其余落盘物（设置、快照、格式定义）一致，理由不是
// 「统一」而是三件具体的事：解析失败能给出**位置**、非 ASCII 的展示名
// 不必自己设计转义、以及换行/缩进这类差异不会让两份「内容相同」的文件
// 逐字节不等。
//
// 状态用 `statusIdentifier()` 的稳定标识符当键，**不按数组下标**：
// 下标会随主状态表增长而整体挪位，而 `.lqcolors` 是拿来分享的，
// 分享出去的文件必须在下一次新增状态档位之后仍然读得回来。

// 序列化成 JSON 文本。永远是合法 JSON：非法色值会被原样写出去，
// 由 `parseColorScheme()` 在**读回来的时候**报错——写出一个「看起来正常、
// 读回来才发现坏了」的文件，比当场报错更难查。
QString serializeColorScheme(const ColorScheme &scheme);

// 解析并校验。成功时把结论写进 `out`（此时 `out` 一定覆盖全部九档）。
// 失败时 `out` **保持原样**（不写入半成品）并把原因追加进 `problems`。
//
// 校验分两层且**都要做**：结构层（是不是 JSON、必填键在不在、色值合不合法）
// 与语义层（覆盖是否完整、对比度是否达标）。只做结构层的话，一个手改坏了
// 对比度的文件会被当成合法的自定义配色装进界面，而用户看到的只是「字看不清」。
bool parseColorScheme(const QString &text, ColorScheme &out, QStringList *problems = nullptr);

// 文件往返。`problems` 里的原因与解析器同源，界面可以直接显示。
QString colorSchemeFileExtension(); // "lqcolors"
QString colorSchemeFileFilter();    // 供 QFileDialog 用
bool saveColorSchemeFile(const QString &path, const ColorScheme &scheme,
                         QStringList *problems = nullptr);
bool loadColorSchemeFile(const QString &path, ColorScheme &scheme,
                         QStringList *problems = nullptr);

} // namespace Folder
} // namespace LqCompare

#endif // LQCOMPARE_STATUSPALETTE_H
