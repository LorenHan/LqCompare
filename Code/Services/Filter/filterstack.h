#ifndef LQCOMPARE_FILTERSTACK_H
#define LQCOMPARE_FILTERSTACK_H

#include "mask.h"
#include "maskfilter.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {

// Services/Session —— 只前向声明接口。第 4 条「视图临时过滤不写入会话」要靠
// 「一个键 + 多个存储」表达，而存储的抽象就是 `SessionSettings`。
// **只前向声明**：本头文件的绝大多数使用者（各测试套件、将来的视图）只需要指针，
// 把 `session.h`（连带它的 QObject 基类）拉进每一个 include 点毫无必要。
class SessionSettings;

namespace Filter {

// -----------------------------------------------------------------------------
// 层级（第 1 条）
// -----------------------------------------------------------------------------

///
/// \brief 过滤来源的层级（PRD: FILT-005）。
///
/// 规格给的三层，枚举顺序**就是**叠加顺序（从最外层到最内层）：
///
///   文件格式定义内建过滤 → 会话设置过滤 → 视图临时过滤
///
/// **⚠ 与本仓库另外两处「三层」不是一回事**，混用其中任何一个都会造成静默错误：
///
/// | 三处「三层」 | 关系 | 含义 |
/// | --- | --- | --- |
/// | `SettingScope`（SESS-007） | **覆盖** | 读的时候只有一个胜出（视图 > 会话 > 类型） |
/// | `FilterLayer`（本条） | **叠加** | 全部生效，逐层收窄 |
/// | `MaskPlatform` 的三档大小写 | 无关 | 只是同一件事的三个取值 |
///
/// 最危险的一处是：拿 `ScopedSessionSettings::value("filter-declaration")` 去取
/// 过滤声明。那会只返回**优先级最高**的那一层（视图层有值时，会话层的过滤
/// 就静默消失了），而过滤器的语义是「三层一起生效」。详见 filterstack.cpp 顶部。
///
enum class FilterLayer
{
    Format,  ///< 文件格式定义内建过滤（用户不可编辑，也不落盘）
    Session, ///< 会话设置过滤（跟着会话走）
    View,    ///< 视图临时过滤（关闭标签即丢弃）
};

/// 机器可读标识（写日志、测试失败信息、设置键都用它，不靠枚举序号）。
const char *filterLayerIdentifier(FilterLayer layer);

/// 界面用的中文标签，如「会话设置过滤」。
QString filterLayerLabel(FilterLayer layer);

/// 一句话说明（下拉项 / 面板行的 tooltip）。
QString filterLayerDescription(FilterLayer layer);

/// 全部层级，按**规格顺序**（Format → Session → View）。
///
/// 本条目里这个顺序同时是叠加顺序，但它与 SESS-007 的
/// `settingScopePriorityOrder()` 是同一种刻意的分离：哪天产品决定
/// 「格式层排到最后展示」，只有展示顺序跟着改，叠加逻辑一行都不用动。
QVector<FilterLayer> allFilterLayers();

/// 该层级在上面那张表里的序号（0 起）；用于查 `FilterStack` 内部的格子。
int filterLayerIndex(FilterLayer layer);

// -----------------------------------------------------------------------------
// 落点（第 4 条）
// -----------------------------------------------------------------------------

///
/// \brief 某一层的声明该落到哪儿（第 4 条：视图临时过滤不写入会话）。
///
/// 这是一条**路由**约束，不是一个枚举摆设：`FilterLayerBinder` 按它决定
/// 写进哪个存储，因此「临时过滤不小心存成永久过滤」在结构上就不可能发生。
///
enum class FilterLayerStorage
{
    Builtin,    ///< 文件格式定义里写死的；用户改不了，也不落盘
    SessionFile,///< 会话文件（SESS-008 落盘），跟着会话走
    ViewMemory, ///< 只在当前视图的内存里，关闭标签即丢弃
};

/// 某一层的落点。
FilterLayerStorage filterLayerStorage(FilterLayer layer);

const char *filterLayerStorageIdentifier(FilterLayerStorage storage);
QString filterLayerStorageLabel(FilterLayerStorage storage);

/// 该层是否是「关闭标签就丢」的那种（只有视图层是）。
bool filterLayerIsDiscardedOnClose(FilterLayer layer);

/// 该层的声明用户能不能编辑（格式层不能——它来自文件格式定义）。
bool filterLayerIsEditable(FilterLayer layer);

///
/// \brief 层级表的一行：一个层级，以及它的声明落在哪儿。
///
/// 表是**唯一的事实来源**：`allFilterLayers()` 的顺序、`filterLayerIndex()` 的下标
/// 与 `filterLayerStorage()` 的落点全部从它推导。分成三处各写一遍的话，
/// 往表里插一层就会出现「面板上排在第二、写进存储用的却是第三个落点」这类错位，
/// 而现象是「设的是会话层、存到了视图层」，很难归因。
///
struct FilterLayerDescriptor
{
    FilterLayer layer = FilterLayer::Format;
    FilterLayerStorage storage = FilterLayerStorage::Builtin;
};

/// 层级表本身（顺序即规格顺序，也即叠加顺序）。
const QVector<FilterLayerDescriptor> &filterLayerTable();

///
/// \brief 层级表的自检（启动时跑一次）。
///
/// 与 `SessionTypeRegistry::validate()` 同一个定位：查「手写那张表时容易写错、
/// 写错了也不影响别的」的几件事。返回**可直接写进日志**的问题清单，
/// 空列表表示干净。
///
/// 为什么要参数化：一条**永远不会红**的护栏比没有护栏更糟。把表当参数传进来之后，
/// 测试可以拿一份故意写坏的表跑同一个判定，证明它真的会报。
///
/// `expectedLayers` 是规格点名要求的层级清单。**新增一个层级时要改它**——
/// 它是唯一不依赖这张表自身的期望值，靠 `-Wswitch` 在别处的 switch 里提醒。
///
QStringList validateFilterLayerTable(
    const QVector<FilterLayerDescriptor> &table,
    const QVector<FilterLayer> &expectedLayers = {FilterLayer::Format,
                                                  FilterLayer::Session,
                                                  FilterLayer::View});


/// 过滤声明的设置键。
///
/// **三层共用同一个键名**，因为它们住在三个**不同的存储**里——与 SESS-007
/// 的「一个键 + 多个作用域」同一手法。这样 SESS-008 落盘时不必为每一层
/// 发明一个键名，读回来也不会出现「有的层换了键名、旧会话文件读不到」。
QString filterDeclarationSettingKey();

// -----------------------------------------------------------------------------
// 一层的状态（第 2 条）
// -----------------------------------------------------------------------------

///
/// \brief 一层过滤的当前状态：声明、解析结果、启用标志。
///
/// 把「声明文本」与「解析后的 MaskFilter」放在同一个结构里，是因为界面
/// 两者都要：输入框要回显用户敲的**原文**（包括写错的那一行），
/// 而叠加要用**解析结果**。分开存会让「改了原文忘了重新解析」成为一种可能，
/// 而现象是「界面上写的是新规则、过滤用的是旧的」。
///
struct FilterLayerState
{
    FilterLayer layer = FilterLayer::Format;

    /// 用户（或文件格式定义）给的原文。
    QString declaration;

    /// 这一层从哪儿来的一句话说明，供面板显示（如「文件格式：C++ 源文件」）。
    /// 格式层以外通常为空。它**不参与逻辑**，只是诊断信息。
    QString source;

    /// 用户勾选的启用状态（第 2 条）。与「有没有规则」是两件事。
    bool enabled = true;

    /// 解析结果。写错的行被丢掉，错误收在下面。
    MaskFilter filter;

    /// 逐行的语法错误（第 2 条要显示「这一层当前是否生效」，
    /// 而「有错」也是「没生效」的一种原因，且用户必须看得到）。
    QVector<MaskRuleError> errors;

    ///
    /// \brief 这一层当前是否**参与叠加**。
    ///
    /// 判据是「启用 **且** 至少有一条有效规则」。空的启用层不参与——
    /// 否则「某一层生效中」会恒为真（三层默认都是启用的），
    /// 用户就无从判断到底是谁在过滤。这一条是第 2 条「显示该层是否当前生效」
    /// 的可测落点，也是本条目最容易写成「只看 enabled」的地方。
    ///
    bool active() const { return enabled && !filter.isEmpty(); }

    /// 界面要显示的一句话状态：生效 / 已禁用 / 已启用但没有规则 / 有语法错误。
    QString statusText() const;

    /// 诊断用的完整一行。
    QString describe() const;
};

/// 一层自己的表达式（`(包含) && !(排除)`）。该层没有规则时返回空串。
QString filterLayerExpression(const FilterLayerState &state);

// -----------------------------------------------------------------------------
// 叠加的结论（第 1、5 条）
// -----------------------------------------------------------------------------

///
/// \brief 三层叠加之后对一个条目的结论，含「是哪一层决定的」。
///
/// 与 `MaskDecision` 是同一个思路（结论 + 依据），只是把作用域从「一条规则」
/// 升到「一层过滤」。诊断面板要回答的「我为什么看不到这个文件」在分层之后
/// 必须先回答「是哪一层挡的」——只给结论的话，用户会去改一个本来就没错的层。
///
struct LayeredFilterDecision
{
    MaskVerdict verdict = MaskVerdict::Included;

    /// 起决定作用的层。没有任何层生效时无意义（见 `anyLayerActive`）。
    FilterLayer decidingLayer = FilterLayer::Format;

    /// 有没有任何一层在生效。全都没生效时结论恒为「保留」，
    /// 且**没有任何层**起决定作用——这一位就是为了把那种情形与
    /// 「格式层放行了它」区分开。
    bool anyLayerActive = false;

    /// 起决定作用的规则下标（对应那一层的 `MaskFilter::rules()`）；-1 表示没有规则参与。
    int ruleIndex = -1;
    MaskRuleKind ruleKind = MaskRuleKind::Include;
    QString ruleText;

    /// 有哪些生效层排除了它。`decidingLayer` 只报第一个，
    /// 而用户改掉第一个之后往往发现还是看不见——所以两者都要有。
    QVector<FilterLayer> excludingLayers;

    QString describe() const;
};

// -----------------------------------------------------------------------------
// 计数（第 3 条）
// -----------------------------------------------------------------------------

/// 一层在一批条目里的表现。
struct FilterLayerStats
{
    FilterLayer layer = FilterLayer::Format;
    bool enabled = true;
    bool active = false;
    int ruleCount = 0;

    /// 本层主动排除掉的条目数。
    int excluded = 0;

    /// 本层白名单没命中的条目数。
    int notMatched = 0;

    ///
    /// \brief 合并结论由本层起决定的条目数。
    ///
    /// 这才是「这一层实际影响了多少条目」，也是面板上唯一一个能回答
    /// 「我把这层关掉会有什么变化」的数字：`ruleCount` 只说它有几条规则，
    /// 而一条永不命中的规则有 0 条被影响。
    ///
    int decided = 0;
};

///
/// \brief 三层叠加的计数结果（第 3 条「合并后的匹配计数」）。
struct FilterStackPreview
{
    int total = 0;
    int included = 0;
    int excluded = 0;
    int notMatched = 0;

    /// 与 `allFilterLayers()` 同序。
    QVector<FilterLayerStats> layers;

    /// 不可见的条目数（被排除的 + 未命中的）。
    int hidden() const { return total - included; }

    /// 取某一层的统计；越界时返回 nullptr。
    const FilterLayerStats *statsFor(FilterLayer layer) const;

    ///
    /// \brief 转成 FILT-001 的计数结构，只为复用它的文案。
    ///
    /// 「匹配 N 项 / 共 M 项」这句话在 FILT-001 里已经被断言过形状。
    /// 在这里重写一遍，两处迟早会出现「共 M 项 / 匹配 N 项」这种截图比对时
    /// 没人会注意的顺序差异。`hitsByRule` 留空——单个规则的命中数在分层之后
    /// 属于某一层自己的统计，不属于合并结果。
    ///
    MaskFilterPreview asMaskPreview() const;

    /// 「匹配 N 项 / 共 M 项（三层叠加）」。
    QString summary() const;
};

// -----------------------------------------------------------------------------
// 三层叠加本身
// -----------------------------------------------------------------------------

///
/// \brief 三层过滤叠加成的最终过滤器（PRD: FILT-005）。
///
/// ## 层与层之间是**交集**
///
/// 规格的边界条款要求「层与层之间的关系必须明确（并集还是交集）」，
/// 这里取**交集**：一个条目必须被每一条生效的层放行，才最终可见。
/// 层内仍然是 FILT-001 的老规矩——**排除优先**，白名单为空表示全部保留。
///
/// 为什么是交集而不是并集
/// ----------------------
/// 并集的意思是「任一层放行即可见」，于是**新增一层过滤会让结果变多**。
/// 用户加一个「只看 .cpp」的过滤，反而看到了原本被排除的文件——
/// 而他会把这件事理解成「过滤器坏了」，不会去怀疑层与层之间是并集。
/// 交集则保证「加一层只会更窄」，这是所有带多级过滤的软件的共同直觉，
/// 也是本条目唯一能自洽的选择。
///
/// 于是三层各自的角色是清楚的：格式层是「这种格式本来就只看这些」，
/// 会话层是「这个会话的用户偏好」，视图层是「我现在临时想再看窄一点」。
///
/// ## 本类不持有任何存储
///
/// 三层声明都在本对象里（赋值即持有），但**它们从哪儿读、写回哪儿**是
/// `FilterLayerBinder` 的事。理由与 SESS-006 把草稿与会话分开同源：
/// 身份与来源分开之后，「这一层是临时的」与「这一层要落盘」就不会
/// 写进叠加逻辑里。
///
class FilterStack
{
public:
    FilterStack();

    // --- 平台与大小写 ---------------------------------------------------------

    /// 掩码语义所在的平台（只影响**默认**大小写敏感性）。改它会重解析全部层。
    MaskPlatform platform() const;
    void setPlatform(MaskPlatform platform);

    bool isCaseSensitivityOverridden() const;

    /// 显式覆盖大小写策略，应用到全部层。
    void setCaseSensitivity(Qt::CaseSensitivity cs);

    /// 回到平台默认。
    void clearCaseSensitivityOverride();

    // --- 三层声明 -------------------------------------------------------------

    /// 设置某一层的声明并重新解析。解析错误**只丢那一行**，其余规则照常生效
    /// （与 `MaskFilter::parse` 同一约定），错误可以从 `layerErrors()` 取。
    void setDeclaration(FilterLayer layer, const QString &declaration);

    QString declaration(FilterLayer layer) const;

    /// 直接塞一整份状态（供 `FilterLayerBinder::loadInto` 用）。
    void setLayerState(FilterLayer layer, const FilterLayerState &state);

    const FilterLayerState &layerState(FilterLayer layer) const;
    const MaskFilter &filter(FilterLayer layer) const;
    QVector<MaskRuleError> layerErrors(FilterLayer layer) const;

    /// 任意一层有语法错误。
    bool hasErrors() const;

    /// 全部层的错误拼成多行文本（界面一次性提示用）。
    QString describeErrors() const;

    // --- 启用 / 禁用与生效状态（第 2 条） ------------------------------------

    void setLayerEnabled(FilterLayer layer, bool enabled);
    bool isLayerEnabled(FilterLayer layer) const;

    /// 这一层是否参与叠加（启用 + 至少一条有效规则）。
    bool isLayerActive(FilterLayer layer) const;

    /// 参与叠加的层，按规格顺序。
    QVector<FilterLayer> activeLayers() const;

    /// 界面要显示的一句话状态（转发给 `FilterLayerState::statusText`）。
    QString layerStatusText(FilterLayer layer) const;

    // --- 叠加（第 1、5 条） ---------------------------------------------------

    LayeredFilterDecision decide(const MaskSubject &subject) const;

    /// 是否最终保留。
    bool accepts(const MaskSubject &subject) const;

    /// 有哪些**生效的**层排除了这个条目（诊断用）。
    QVector<FilterLayer> excludingLayers(const MaskSubject &subject) const;

    // --- 最终生效的表达式（第 3 条） -----------------------------------------

    /// 有没有任何一层在生效且带规则。为假时 `combinedExpression()` 返回空串。
    bool hasActiveRule() const;

    ///
    /// \brief 三层合并后的表达式，一行。
    ///
    /// 形状：`(各层白名单取交集) && !(各层黑名单取并集)`。
    /// 它与「逐层 `&&` 连接」完全等价（德摩根），但短得多——
    /// 三层各两条规则时，逐层写法有 6 个括号，而这一个只有 3 个。
    ///
    /// 每条掩码只要含有空格或 `&` `|` `!` `(` `)` `'` 就会被单引号包起来，
    /// 因此这个字符串**无歧义**：不然一条叫 `a|b` 的掩码会与运算符分不开。
    /// 引号只是显示用的，不是掩码语法的一部分——`describe()` 里写明了这一点。
    ///
    QString combinedExpression() const;

    // --- 计数（第 3 条） ------------------------------------------------------

    FilterStackPreview preview(const QVector<MaskSubject> &subjects) const;

    /// 便捷重载：只有名字。
    FilterStackPreview previewNames(const QStringList &names) const;

    QString describe() const;

private:
    /// 按当前的平台与大小写覆盖重解析某一层。
    void reparseLayer(FilterLayer layer);

    /// 把大小写覆盖应用到某一层的解析结果上。
    void applyCaseSensitivity(FilterLayer layer);

    FilterLayerState m_layers[3];
    MaskPlatform m_platform = MaskPlatform::Posix;
    bool m_caseOverridden = false;
    Qt::CaseSensitivity m_case = Qt::CaseSensitive;
};

// -----------------------------------------------------------------------------
// 落点的实现（第 4 条）
// -----------------------------------------------------------------------------

///
/// \brief 三层过滤声明的持久化出口：把「哪一层写进哪个存储」收在一处。
///
/// ## 为什么必须有这个类
///
/// 第 4 条要求「视图临时过滤不写入会话」。这是一条**路由**约束：同样的键名、
/// 同样的内容，写进视图存储是临时的（关标签即丢），写进会话存储就跟着会话走了。
/// 如果每个调用点各自决定往哪个存储写，那么一次「临时过滤」变成「永久过滤」
/// 只需要一处笔误——而现象是「关掉标签再打开，过滤还在」，
/// 用户会以为是设置没生效，不会想到是存错了地方。
///
/// 把两个存储收在这个类里之后，`saveLayer(View, …)` 只可能落到视图存储：
/// 「写错地方」在结构上不可能发生（`saveLayer` 按 `filterLayerStorage()`
/// 取存储，调用点根本拿不到「往另一个存储写」的入口）。
///
/// ## 存储是借用的
///
/// 视图存储归视图所有：视图销毁（关标签）它就不在了。
/// **「关闭标签即丢弃」因此是所有权带来的结论，而不是一段需要记得执行的清理代码**
/// ——后者总有一条路径会漏掉（崩溃、强杀、异常），而漏掉的表现是临时过滤
/// 悄悄变成了永久过滤。
///
/// ## 格式层没有存储
///
/// 它来自文件格式定义（`FMT-*` 尚未落地），只能由 `setBuiltinDeclaration()` 给进来，
/// 且永远不可写（`canSaveLayer(Format)` 恒为假）。
///
class FilterLayerBinder
{
public:
    FilterLayerBinder();

    /// 视图层的存储（关标签即丢弃的那一份）。借用，不接管生命周期。
    void setViewStore(SessionSettings *store);

    /// 会话层的存储（会话文件那一份，SESS-008 落盘）。借用。
    void setSessionStore(SessionSettings *store);

    SessionSettings *viewStore() const { return m_viewStore; }
    SessionSettings *sessionStore() const { return m_sessionStore; }

    /// 格式层的声明。它不是用户设置，没有存储。
    void setBuiltinDeclaration(const QString &declaration);
    QString builtinDeclaration() const { return m_builtin; }

    /// 格式层声明的来源说明（面板上显示「文件格式：…」）。
    void setBuiltinSource(const QString &source);
    QString builtinSource() const { return m_builtinSource; }

    /// 某一层现在能不能写。
    bool canSaveLayer(FilterLayer layer) const;

    ///
    /// \brief 把某一层写回**它该去的地方**。格式层永远返回 false。
    ///
    /// 返回 false 的另外两种情况：该层不可写；对应的存储没接上。
    /// **不因为视图存储没接上就退而写入会话存储**——那会把一次临时过滤
    /// 变成永久的，是本类存在的全部理由所反对的事。
    ///
    bool saveLayer(FilterLayer layer, const FilterStack &stack) const;

    ///
    /// \brief 按存储把三层读进栈。
    ///
    /// 三层**各自从自己的存储读**，不走优先级解析。这一点是本条目与 SESS-007
    /// 最容易混的地方：`ScopedSessionSettings::value()` 只会返回胜出的那一层，
    /// 用它来取过滤声明会让会话层过滤静默失效。
    ///
    /// 会清掉栈里已有的声明（先清空再读），因此本函数是「以存储为准」的完整载入。
    ///
    void loadInto(FilterStack *stack) const;

    /// 从视图存储里删掉过滤声明（关闭标签）。返回真正删掉的键数（0 或 1）。
    int discardViewLayer() const;

    QString describe() const;

private:
    SessionSettings *m_viewStore = nullptr;
    SessionSettings *m_sessionStore = nullptr;
    QString m_builtin;
    QString m_builtinSource;
};

// -----------------------------------------------------------------------------
// 「查看最终生效过滤」面板（第 3 条）
// -----------------------------------------------------------------------------

/// 面板上的一层。
struct FilterLayerPanelRow
{
    FilterLayer layer = FilterLayer::Format;
    QString label;
    QString storageLabel;   ///< 「只读（格式定义）」/「随会话保存」/「仅当前视图」
    QString source;         ///< 这一层来自哪里（可为空）
    bool editable = true;
    bool enabled = true;
    bool active = false;
    int ruleCount = 0;
    int includeCount = 0;
    int excludeCount = 0;
    int decided = 0;        ///< 合并结论由本层起决定的条目数

    QString statusText;
    QString expression;     ///< 这一层自己的表达式（没有规则时为空）
    QString errorText;      ///< 该层的语法错误摘要（没有错误时为空）

    QString describe() const;
};

///
/// \brief 「查看最终生效过滤」面板的内容（第 3 条）。
///
/// 做成**数据 + 文本**，而不是一个控件：这一条的面板要等设置页（`OPT-*`）
/// 才有地方住，而「合并后的表达式与匹配计数」本身是纯数据问题，
/// 现在就能被完整实现并逐条断言。与 FILT-001 把语法速查表做成数据同一个理由——
/// 手写在界面里的话，帮助页与面板会各演化出一种说法。
///
struct EffectiveFilterPanel
{
    QString title;
    QString semantics;  ///< 层级语义说明（交集 + 排除优先）
    QString expression; ///< 三层合并后的一行表达式（没有生效层时为空）
    QVector<FilterLayerPanelRow> layers;

    int total = 0;
    int included = 0;
    int excluded = 0;
    int notMatched = 0;

    /// 「匹配 N 项 / 共 M 项」——与 FILT-001 是**同一句文案**（由
    /// `MaskFilterPreview::summary()` 生成），不在这里重写一遍。
    QString summary;

    /// 不可见的条目数（被排除的 + 未命中的）。
    int hidden() const { return total - included; }

    /// 没有任何一层生效（表达式为空、也没什么可过滤的）。
    bool empty() const;

    /// 面板全文（纯文本，可直接塞进只读文本框，也可以进诊断包）。
    QString describe() const;
};

/// 为一个过滤器与一批条目生成面板内容。
EffectiveFilterPanel buildEffectiveFilterPanel(const FilterStack &stack,
                                               const QVector<MaskSubject> &subjects);

/// 便捷重载：只有名字。
EffectiveFilterPanel buildEffectiveFilterPanel(const FilterStack &stack,
                                               const QStringList &names);

} // namespace Filter
} // namespace LqCompare

#endif // LQCOMPARE_FILTERSTACK_H
