#include "filterstack.h"

#include "session.h" // Services/Session —— 第 4 条的落点所依托的存储抽象

#include <QSet>

namespace LqCompare {
namespace Filter {

// -----------------------------------------------------------------------------
// 本文件里最容易踩坏的一处（先说清楚，免得下一个人「顺手修好」）
// -----------------------------------------------------------------------------
//
// 本仓库里同时存在两套「视图 / 会话 / 类型」的三层，它们的**方向相反**：
//
//   * `SettingScope`（SESS-007）：**覆盖**。读的时候按 视图 > 会话 > 类型 逐层问
//     「这一层有没有显式设过」，第一个命中的胜出，下面几层被**遮住**。
//   * `FilterLayer`（本条）：**叠加**。三层全部生效，一个条目必须被每一条生效的层
//     放行才最终可见；层与层之间取交集。
//
// 于是有一个看起来很自然、实际会静默丢掉一层过滤的写法：
//
//     ScopedSessionSettings settings;                        // 三层视图/会话/类型
//     stack.setDeclaration(FilterLayer::Session,
//                          settings.value("filter-declaration").toString());  // ← 错
//
// `settings.value()` 只会返回**优先级最高**的那一层。视图层里只要有值，
// 会话层的过滤声明就永远读不到——而界面上两条设置看起来都在，用户只会觉得
// 「会话级的过滤怎么不起作用」。正确做法是**按存储分别读**：
// 会话层从会话存储读、视图层从视图存储读，读出来的两份都装进同一个栈。
// `FilterLayerBinder::loadInto()` 就是这件事的唯一实现，不要绕过它。
//
// 反过来说，两边共用「三层」这个名字是刻意的吗？不是——是两条需求各自独立选了三层。
// 代价就是这一处同形不同义，所以写在这里，而不是指望下一个人从代码里看出来。

namespace {

///
/// \brief 把一组掩码拼成一个表达式原子序列（去重，必要时加引号）。
///
/// 去重只影响显示：`a || a` 与 `a` 是同一个集合，而重复显示会让面板看起来
/// 像坏了（用户会以为程序把同一条规则算了两遍）。
///
QString expressionAtom(const QString &text)
{
    // 掩码里的空格与 `& | ! ( ) '` 都可能是**字面量**（掩码语言里它们没有特殊含义），
    // 所以这样的原子必须加引号——不然一条叫 `a|b` 的掩码会与运算符分不开，
    // 面板上看起来就是「两个条件」而不是「一个名字里带竖线的文件」。
    const QString reserved = QStringLiteral(" &|!()'");
    bool needsQuotes = text.isEmpty();
    for (const QChar &character : text) {
        if (reserved.contains(character)) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes)
        return text;

    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString joinAtoms(const QStringList &atoms, const QString &separator)
{
    QStringList unique;
    QSet<QString> seen;
    for (const QString &atom : atoms) {
        if (seen.contains(atom))
            continue;
        seen.insert(atom);
        unique.append(expressionAtom(atom));
    }

    if (unique.isEmpty())
        return QString();
    if (unique.size() == 1)
        return unique.first();
    return QStringLiteral("(") + unique.join(separator) + QStringLiteral(")");
}

///
/// \brief 把一段已经拼好的表达式取反，且**不叠双括号**。
///
/// `joinAtoms` 在多于一个原子时已经加了括号（`(a || b)`），直接再包一层会得到
/// `!((a || b))`——语义没错，但面板上看起来像程序拼错了，而用户会怀疑整个表达式
/// 是不是也不可信。
///
QString negateExpression(const QString &text)
{
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')')))
        return QStringLiteral("!") + text;
    return QStringLiteral("!(") + text + QStringLiteral(")");
}

///
/// \brief 把「包含集合」与「排除集合」拼成一条表达式：`(包含) && !(排除)`。
///
/// 不把每条规则的位置信息编进去：表达式的用途是「一眼看懂现在到底在过滤什么」，
/// 而哪一条规则来自哪一层由面板逐层列出来。混在一起会变成一段谁都不读的长串。
///
QString composeExpression(const QStringList &includes, const QStringList &excludes)
{
    const QString includeText = joinAtoms(includes, QStringLiteral(" || "));
    const QString excludeText = joinAtoms(excludes, QStringLiteral(" || "));

    if (includeText.isEmpty() && excludeText.isEmpty())
        return QString();
    if (includeText.isEmpty())
        return negateExpression(excludeText);
    if (excludeText.isEmpty())
        return includeText;
    return includeText + QStringLiteral(" && ") + negateExpression(excludeText);
}

/// 两层结构体：把一份 `FilterLayerState` 里两类规则的文本分别抽出来。
void collectRuleTexts(const MaskFilter &filter, QStringList *includes, QStringList *excludes)
{
    for (const MaskRule &rule : filter.rules()) {
        if (rule.kind == MaskRuleKind::Include)
            includes->append(rule.text);
        else
            excludes->append(rule.text);
    }
}

/// 从某个存储里读过滤声明；存储没接上时当作「没有声明」。
QString declarationFromStore(const SessionSettings *store)
{
    if (!store)
        return QString();
    return store->value(filterDeclarationSettingKey()).toString();
}

/// 某一层的声明归哪个存储管。格式层没有存储（它来自文件格式定义）。
SessionSettings *storeForLayer(FilterLayer layer,
                               SessionSettings *viewStore,
                               SessionSettings *sessionStore)
{
    switch (layer) {
    case FilterLayer::Format:
        return nullptr;
    case FilterLayer::Session:
        return sessionStore;
    case FilterLayer::View:
        return viewStore;
    }
    return nullptr;
}

} // namespace

// -----------------------------------------------------------------------------
// 层级
// -----------------------------------------------------------------------------

const char *filterLayerIdentifier(FilterLayer layer)
{
    switch (layer) {
    case FilterLayer::Format:
        return "format";
    case FilterLayer::Session:
        return "session";
    case FilterLayer::View:
        return "view";
    }
    return "unknown";
}

QString filterLayerLabel(FilterLayer layer)
{
    switch (layer) {
    case FilterLayer::Format:
        return QStringLiteral("文件格式内建过滤");
    case FilterLayer::Session:
        return QStringLiteral("会话设置过滤");
    case FilterLayer::View:
        return QStringLiteral("视图临时过滤");
    }
    return QString();
}

QString filterLayerDescription(FilterLayer layer)
{
    switch (layer) {
    case FilterLayer::Format:
        return QStringLiteral("由文件格式定义给出的过滤，用户不能改，只能整层启用或禁用");
    case FilterLayer::Session:
        return QStringLiteral("保存在这个会话里，跟着会话走");
    case FilterLayer::View:
        return QStringLiteral("只对当前视图临时生效，关闭标签即丢弃，不会写进会话");
    }
    return QString();
}

const QVector<FilterLayerDescriptor> &filterLayerTable()
{
    // 顺序即规格顺序，也即叠加顺序（从最外层到最内层）。与
    // `settingScopePriorityOrder()` 同一种刻意分离：展示顺序将来若变，叠加逻辑不动。
    //
    // 落点逐层写死，理由见各自的注释：
    //   Format  —— 来自文件格式定义，用户改不了，也没有存储可写；
    //   Session —— 会话文件（SESS-008 落盘），跟着会话走；
    //   View    —— **第 4 条的唯一落点声明**：视图临时过滤不落盘。
    static const QVector<FilterLayerDescriptor> table = {
        {FilterLayer::Format, FilterLayerStorage::Builtin},
        {FilterLayer::Session, FilterLayerStorage::SessionFile},
        {FilterLayer::View, FilterLayerStorage::ViewMemory},
    };
    return table;
}

QVector<FilterLayer> allFilterLayers()
{
    QVector<FilterLayer> layers;
    const QVector<FilterLayerDescriptor> &table = filterLayerTable();
    layers.reserve(table.size());
    for (const FilterLayerDescriptor &row : table)
        layers.append(row.layer);
    return layers;
}

int filterLayerIndex(FilterLayer layer)
{
    const QVector<FilterLayerDescriptor> &table = filterLayerTable();
    for (int index = 0; index < table.size(); ++index) {
        if (table.at(index).layer == layer)
            return index;
    }
    // 表里没有这一层。启动自检（validateFilterLayerTable）会把这件事报出来，
    // 因此这里是「不该发生的兜底」而不是正常路径：夹到 0 而不是返回 -1，
    // 是因为所有调用点都按「合法下标」写，返回 -1 会让每一处都要判一次，
    // 而判了之后的「什么都不做」比夹紧更难被发现。
    return 0;
}

// -----------------------------------------------------------------------------
// 落点
// -----------------------------------------------------------------------------

FilterLayerStorage filterLayerStorage(FilterLayer layer)
{
    const QVector<FilterLayerDescriptor> &table = filterLayerTable();
    for (const FilterLayerDescriptor &row : table) {
        if (row.layer == layer)
            return row.storage;
    }
    return FilterLayerStorage::Builtin;
}

QStringList validateFilterLayerTable(const QVector<FilterLayerDescriptor> &table,
                                     const QVector<FilterLayer> &expectedLayers)
{
    QStringList problems;

    if (table.size() != expectedLayers.size()) {
        problems.append(QStringLiteral("层级表应当有 %1 行，实际 %2 行")
                            .arg(expectedLayers.size())
                            .arg(table.size()));
    }

    for (int index = 0; index < expectedLayers.size(); ++index) {
        const FilterLayer layer = expectedLayers.at(index);

        int seen = 0;
        for (const FilterLayerDescriptor &row : table) {
            if (row.layer == layer)
                ++seen;
        }
        if (seen == 0)
            problems.append(QStringLiteral("层级表里缺少「%1」").arg(filterLayerLabel(layer)));
        else if (seen > 1)
            problems.append(QStringLiteral("层级表里「%1」出现了 %2 次，它的落点会不确定")
                                .arg(filterLayerLabel(layer))
                                .arg(seen));

        // 顺序：交集本身与顺序无关（交换律），但这个顺序同时是面板的展示顺序
        // 与「第一个排除它的层」的判定顺序。顺序错了，同一个文件会报出另一层
        // 作为「起决定作用的层」，而用户拿到的修改建议是照那一层给的。
        if (index < table.size() && table.at(index).layer != layer) {
            problems.append(QStringLiteral("层级表第 %1 行应当是「%2」，实际是「%3」")
                                .arg(index + 1)
                                .arg(filterLayerLabel(layer))
                                .arg(filterLayerLabel(table.at(index).layer)));
        }
    }

    // 第 4 条单独查一遍，因为它是本条目里**最贵的一处错误**：
    // 视图层的落点一旦被改成 SessionFile，过滤照样工作，只是关掉标签它还在——
    // 没有任何运行期现象会提示这件事，用户只会觉得「怎么我删掉的临时过滤又回来了」。
    for (const FilterLayerDescriptor &row : table) {
        if (row.layer == FilterLayer::View && row.storage != FilterLayerStorage::ViewMemory) {
            problems.append(QStringLiteral(
                                "视图层的落点必须是「仅当前视图」，实际是「%1」"
                                "（第 4 条：视图临时过滤不写入会话，关闭标签即丢弃）")
                                .arg(filterLayerStorageLabel(row.storage)));
        }
        if (row.layer == FilterLayer::Format && row.storage != FilterLayerStorage::Builtin) {
            problems.append(QStringLiteral(
                                "格式层的落点必须是「只读（来自格式定义）」，实际是「%1」——"
                                "格式定义不是用户设置，让它可写会让一次误操作改掉一种格式的语义")
                                .arg(filterLayerStorageLabel(row.storage)));
        }
    }

    return problems;
}

const char *filterLayerStorageIdentifier(FilterLayerStorage storage)
{
    switch (storage) {
    case FilterLayerStorage::Builtin:
        return "builtin";
    case FilterLayerStorage::SessionFile:
        return "session-file";
    case FilterLayerStorage::ViewMemory:
        return "view-memory";
    }
    return "unknown";
}

QString filterLayerStorageLabel(FilterLayerStorage storage)
{
    switch (storage) {
    case FilterLayerStorage::Builtin:
        return QStringLiteral("只读（来自格式定义）");
    case FilterLayerStorage::SessionFile:
        return QStringLiteral("随会话保存");
    case FilterLayerStorage::ViewMemory:
        return QStringLiteral("仅当前视图（关标签即丢弃）");
    }
    return QString();
}

bool filterLayerIsDiscardedOnClose(FilterLayer layer)
{
    return filterLayerStorage(layer) == FilterLayerStorage::ViewMemory;
}

bool filterLayerIsEditable(FilterLayer layer)
{
    return filterLayerStorage(layer) != FilterLayerStorage::Builtin;
}

QString filterDeclarationSettingKey()
{
    // 三层共用一个键名。它们住在三个不同的存储里，因此不会互相覆盖——
    // 与 SESS-007 的「一个键 + 多个作用域」同一手法。若给每层各发明一个键名，
    // SESS-008 落盘时就多了一份「键名 ↔ 层」的对应关系要维护，
    // 而漏掉一处的表现是「旧会话文件里那一层的过滤读不回来」。
    return QStringLiteral("filter-declaration");
}

// -----------------------------------------------------------------------------
// FilterLayerState
// -----------------------------------------------------------------------------

QString FilterLayerState::statusText() const
{
    // 先看启用标志：用户明确关掉了一层，界面就该说「已禁用」，
    // 而不是拿一条语法错误去解释一个他本来就没打算启用它的事实。
    if (!enabled) {
        if (!errors.isEmpty()) {
            return QStringLiteral("已禁用（另有 %1 行语法错误）").arg(errors.size());
        }
        if (filter.isEmpty())
            return QStringLiteral("已禁用（没有规则）");
        return QStringLiteral("已禁用（%1 条规则不参与叠加）").arg(filter.ruleCount());
    }

    // 有错但仍有规则生效：这正是 MaskFilter「一行写错只丢那一行」的意义，
    // 界面必须把「还剩几条在用」说清楚，否则用户会以为整层都失效了。
    if (!errors.isEmpty()) {
        return QStringLiteral("有 %1 行语法错误，其余 %2 条规则照常生效")
            .arg(errors.size())
            .arg(filter.ruleCount());
    }

    // 这一条是第 2 条的关键：「已启用但没有规则」与「生效中」必须分得开。
    // 把空的启用层也算成生效的话，「某一层生效中」会恒为真（三层默认都启用），
    // 用户就无从判断到底是谁在过滤。
    if (filter.isEmpty())
        return QStringLiteral("已启用，但没有规则");

    return QStringLiteral("生效中（包含 %1 条 / 排除 %2 条）")
        .arg(filter.includeCount())
        .arg(filter.excludeCount());
}

QString FilterLayerState::describe() const
{
    QString text = QStringLiteral("%1：%2").arg(filterLayerLabel(layer), statusText());
    if (!errors.isEmpty())
        text += QStringLiteral("；首个错误 %1").arg(errors.first().describe());
    return text;
}

QString filterLayerExpression(const FilterLayerState &state)
{
    QStringList includes;
    QStringList excludes;
    collectRuleTexts(state.filter, &includes, &excludes);
    return composeExpression(includes, excludes);
}

// -----------------------------------------------------------------------------
// LayeredFilterDecision
// -----------------------------------------------------------------------------

QString LayeredFilterDecision::describe() const
{
    if (!anyLayerActive)
        return QStringLiteral("没有任何过滤层生效，条目全部保留");

    const QString layerName = filterLayerLabel(decidingLayer);
    switch (verdict) {
    case MaskVerdict::Excluded:
        return QStringLiteral("被「%1」的排除规则「%2」排除").arg(layerName, ruleText);
    case MaskVerdict::NotMatched:
        return QStringLiteral("「%1」的白名单里没有它（未被任何包含规则命中）").arg(layerName);
    case MaskVerdict::Included:
        return QStringLiteral("三层叠加全部放行（最后一道白名单来自「%1」）").arg(layerName);
    }
    return QString();
}

// -----------------------------------------------------------------------------
// FilterStackPreview
// -----------------------------------------------------------------------------

const FilterLayerStats *FilterStackPreview::statsFor(FilterLayer layer) const
{
    const int index = filterLayerIndex(layer);
    if (index < 0 || index >= layers.size())
        return nullptr;
    return &layers.at(index);
}

MaskFilterPreview FilterStackPreview::asMaskPreview() const
{
    MaskFilterPreview preview;
    preview.total = total;
    preview.included = included;
    preview.excluded = excluded;
    preview.notMatched = notMatched;
    // hitsByRule 刻意留空：单个规则的命中数在分层之后属于某一层自己的统计，
    // 不属于「三层合并」这个结果，硬填会让人以为它的下标跨层连续。
    return preview;
}

QString FilterStackPreview::summary() const
{
    return asMaskPreview().summary();
}

// -----------------------------------------------------------------------------
// FilterStack
// -----------------------------------------------------------------------------

FilterStack::FilterStack()
    : m_platform(currentMaskPlatform())
    , m_case(defaultCaseSensitivity(m_platform))
{
    for (FilterLayer layer : allFilterLayers())
        m_layers[filterLayerIndex(layer)].layer = layer;
}

MaskPlatform FilterStack::platform() const
{
    return m_platform;
}

void FilterStack::setPlatform(MaskPlatform platform)
{
    if (m_platform == platform)
        return;
    m_platform = platform;
    // 必须重解析：平台只影响**默认**大小写敏感性，而它是在解析时固化进 MaskFilter 的。
    // 只记下来而不重解析，会让「切到 Windows 语义」看起来生效了、实际一条都没变。
    for (FilterLayer layer : allFilterLayers())
        reparseLayer(layer);
}

bool FilterStack::isCaseSensitivityOverridden() const
{
    return m_caseOverridden;
}

void FilterStack::setCaseSensitivity(Qt::CaseSensitivity cs)
{
    m_case = cs;
    m_caseOverridden = true;
    for (FilterLayer layer : allFilterLayers())
        applyCaseSensitivity(layer);
}

void FilterStack::clearCaseSensitivityOverride()
{
    if (!m_caseOverridden)
        return;
    m_caseOverridden = false;
    m_case = defaultCaseSensitivity(m_platform);
    for (FilterLayer layer : allFilterLayers())
        m_layers[filterLayerIndex(layer)].filter.clearCaseSensitivityOverride();
}

void FilterStack::setDeclaration(FilterLayer layer, const QString &declaration)
{
    const int index = filterLayerIndex(layer);
    m_layers[index].layer = layer;
    m_layers[index].declaration = declaration;
    reparseLayer(layer);
}

QString FilterStack::declaration(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)].declaration;
}

void FilterStack::setLayerState(FilterLayer layer, const FilterLayerState &state)
{
    const int index = filterLayerIndex(layer);
    m_layers[index] = state;
    // layer 字段由参数说了算：调用点复制一份状态过来时很容易忘了改这个字段，
    // 而错了之后面板上会出现两层同名的行，很难归因。
    m_layers[index].layer = layer;
    // **不采用 state.filter / state.errors，一律按声明文本重新解析**：
    // 「解析结果来自声明文本」是一条必须成立的不变式，允许传进来一份不一致的
    // 解析结果，就等于给「界面上写的是新规则、过滤用的是旧的」开了一道门。
    reparseLayer(layer);
}

const FilterLayerState &FilterStack::layerState(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)];
}

const MaskFilter &FilterStack::filter(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)].filter;
}

QVector<MaskRuleError> FilterStack::layerErrors(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)].errors;
}

bool FilterStack::hasErrors() const
{
    for (FilterLayer layer : allFilterLayers()) {
        if (!m_layers[filterLayerIndex(layer)].errors.isEmpty())
            return true;
    }
    return false;
}

QString FilterStack::describeErrors() const
{
    QStringList lines;
    for (FilterLayer layer : allFilterLayers()) {
        const QVector<MaskRuleError> errors = m_layers[filterLayerIndex(layer)].errors;
        for (const MaskRuleError &error : errors) {
            // 带上层名：三层的行号各自从 0 起，不带层名的话用户不知道该去哪一框改。
            lines.append(QStringLiteral("%1：%2").arg(filterLayerLabel(layer), error.describe()));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void FilterStack::setLayerEnabled(FilterLayer layer, bool enabled)
{
    m_layers[filterLayerIndex(layer)].enabled = enabled;
}

bool FilterStack::isLayerEnabled(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)].enabled;
}

bool FilterStack::isLayerActive(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)].active();
}

QVector<FilterLayer> FilterStack::activeLayers() const
{
    QVector<FilterLayer> layers;
    for (FilterLayer layer : allFilterLayers()) {
        if (m_layers[filterLayerIndex(layer)].active())
            layers.append(layer);
    }
    return layers;
}

QString FilterStack::layerStatusText(FilterLayer layer) const
{
    return m_layers[filterLayerIndex(layer)].statusText();
}

LayeredFilterDecision FilterStack::decide(const MaskSubject &subject) const
{
    LayeredFilterDecision decision;
    decision.anyLayerActive = !activeLayers().isEmpty();

    // 第 1 步：任一生效层排除它 → 排除。
    // 与层内的「排除优先」是同一条规则，只是升到了层间：用户说「不要这个」
    // 时，不管它被另外几层放行了多少次，结论都是不要。反过来（把层间定成
    // 「包含优先」）会让排除规则在多层场景下完全失效，而用户没有任何替代写法。
    for (FilterLayer layer : allFilterLayers()) {
        const FilterLayerState &state = m_layers[filterLayerIndex(layer)];
        if (!state.active())
            continue;

        const MaskDecision one = state.filter.decide(subject);
        if (one.verdict != MaskVerdict::Excluded)
            continue;

        decision.verdict = MaskVerdict::Excluded;
        decision.decidingLayer = layer;
        decision.ruleIndex = one.ruleIndex;
        decision.ruleKind = one.ruleKind;
        decision.ruleText = one.ruleText;
        // 起决定作用的是**第一个**排除它的层，但「有哪些层排除它」要一起给出：
        // 用户改掉第一个之后往往发现还是看不见，只报第一个会让他反复改错地方。
        decision.excludingLayers = excludingLayers(subject);
        return decision;
    }

    // 第 2 步：任一层的白名单没命中 → 未命中。
    // 「被排除」与「未命中」刻意不合成一个「不可见」：前者是「你明确要求不要它」，
    // 后者是「你的白名单里没有它」，界面给的解释与建议完全不同（FILT-001 同一约定）。
    for (FilterLayer layer : allFilterLayers()) {
        const FilterLayerState &state = m_layers[filterLayerIndex(layer)];
        if (!state.active())
            continue;

        const MaskDecision one = state.filter.decide(subject);
        if (one.verdict != MaskVerdict::NotMatched)
            continue;

        decision.verdict = MaskVerdict::NotMatched;
        decision.decidingLayer = layer;
        decision.ruleIndex = one.ruleIndex;
        decision.ruleKind = one.ruleKind;
        decision.ruleText = one.ruleText;
        return decision;
    }

    // 第 3 步：全部放行。
    decision.verdict = MaskVerdict::Included;

    // 「哪一层决定了它被保留」对「保留」这个结论没有唯一答案，取一个最有信息量的：
    // **最后一个有白名单的生效层**——白名单是收窄的那一半，最后一个白名单就是
    // 最后一道闸门。一层白名单都没有时（全是排除规则）取最后一个生效层，
    // 此时「全部保留」确实是各层共同给出的结论。
    FilterLayer lastActive = FilterLayer::Format;
    bool anyWhitelist = false;
    for (FilterLayer layer : allFilterLayers()) {
        const FilterLayerState &state = m_layers[filterLayerIndex(layer)];
        if (!state.active())
            continue;
        lastActive = layer;
        if (state.filter.includeCount() > 0) {
            anyWhitelist = true;
            decision.decidingLayer = layer;
        }
    }
    if (!anyWhitelist)
        decision.decidingLayer = lastActive;

    return decision;
}

bool FilterStack::accepts(const MaskSubject &subject) const
{
    return decide(subject).verdict == MaskVerdict::Included;
}

QVector<FilterLayer> FilterStack::excludingLayers(const MaskSubject &subject) const
{
    QVector<FilterLayer> layers;
    for (FilterLayer layer : allFilterLayers()) {
        const FilterLayerState &state = m_layers[filterLayerIndex(layer)];
        if (!state.active())
            continue;
        if (state.filter.decide(subject).verdict == MaskVerdict::Excluded)
            layers.append(layer);
    }
    return layers;
}

bool FilterStack::hasActiveRule() const
{
    return !activeLayers().isEmpty();
}

QString FilterStack::combinedExpression() const
{
    // 形状是 `(各层白名单取交集) && !(各层黑名单取并集)`，与「逐层 `&&` 连接」
    // 完全等价——设第 i 层的表达式是 `INC_i && !EXC_i`，则
    //   AND_i (INC_i && !EXC_i) = (AND_i INC_i) && (AND_i !EXC_i)
    //                          = (AND_i INC_i) && !(OR_i EXC_i)      （德摩根）
    // 合并写法短得多：三层各两条规则时，逐层写法有 6 个括号，合并写法只有 3 个。
    //
    // 至于「白名单为空的层」：它的 INC_i 恒为真，于是对交集没有贡献——
    // 这正是「只填了排除框是正常用法」在分层场景下的表现。
    QStringList layerIncludes;
    QStringList excludes;
    for (FilterLayer layer : allFilterLayers()) {
        const FilterLayerState &state = m_layers[filterLayerIndex(layer)];
        if (!state.active())
            continue;
        QStringList includes;
        collectRuleTexts(state.filter, &includes, &excludes);
        // Keep each layer's OR group intact before joining layers with AND.
        // Flattening includes here incorrectly describes an intersection as a
        // union, although decide() continues to filter by intersection.
        const QString included = joinAtoms(includes, QStringLiteral(" || "));
        if (!included.isEmpty() && !layerIncludes.contains(included))
            layerIncludes.append(included);
    }
    const QString included = layerIncludes.join(QStringLiteral(" && "));
    const QString excluded = joinAtoms(excludes, QStringLiteral(" || "));
    if (excluded.isEmpty())
        return included;
    if (included.isEmpty())
        return negateExpression(excluded);
    return included + QStringLiteral(" && ") + negateExpression(excluded);
}

FilterStackPreview FilterStack::preview(const QVector<MaskSubject> &subjects) const
{
    FilterStackPreview result;
    result.total = subjects.size();
    result.layers.resize(allFilterLayers().size());
    for (FilterLayer layer : allFilterLayers()) {
        const int index = filterLayerIndex(layer);
        FilterLayerStats &stats = result.layers[index];
        stats.layer = layer;
        stats.enabled = m_layers[index].enabled;
        stats.active = m_layers[index].active();
        stats.ruleCount = m_layers[index].filter.ruleCount();
    }

    for (const MaskSubject &subject : subjects) {
        const LayeredFilterDecision decision = decide(subject);
        switch (decision.verdict) {
        case MaskVerdict::Included:
            ++result.included;
            break;
        case MaskVerdict::Excluded:
            ++result.excluded;
            break;
        case MaskVerdict::NotMatched:
            ++result.notMatched;
            break;
        }

        if (decision.anyLayerActive)
            result.layers[filterLayerIndex(decision.decidingLayer)].decided += 1;

        // 逐层单独统计。这些数字**可以重叠**（同一条目被两层同时排除），
        // 它们回答的是「这一层自己命中了多少」，而 `decided` 回答的是
        // 「最终结论由谁起决定作用」——两者不是一回事，面板上要分开写。
        for (FilterLayer layer : allFilterLayers()) {
            const int index = filterLayerIndex(layer);
            if (!result.layers[index].active)
                continue;
            const MaskDecision one = m_layers[index].filter.decide(subject);
            if (one.verdict == MaskVerdict::Excluded)
                ++result.layers[index].excluded;
            else if (one.verdict == MaskVerdict::NotMatched)
                ++result.layers[index].notMatched;
        }
    }

    return result;
}

FilterStackPreview FilterStack::previewNames(const QStringList &names) const
{
    QVector<MaskSubject> subjects;
    subjects.reserve(names.size());
    for (const QString &name : names)
        subjects.append(MaskSubject::forName(name));
    return preview(subjects);
}

QString FilterStack::describe() const
{
    QStringList parts;
    for (FilterLayer layer : allFilterLayers())
        parts.append(QStringLiteral("%1=%2").arg(filterLayerLabel(layer),
                                                 m_layers[filterLayerIndex(layer)].statusText()));

    QString text = QStringLiteral("三层过滤：") + parts.join(QStringLiteral("；"));
    const QString expression = combinedExpression();
    if (expression.isEmpty())
        text += QStringLiteral("；没有任何生效的过滤");
    else
        text += QStringLiteral("；最终表达式 %1").arg(expression);
    return text;
}

void FilterStack::reparseLayer(FilterLayer layer)
{
    const int index = filterLayerIndex(layer);
    const MaskFilterParseResult parsed = MaskFilter::parse(m_layers[index].declaration, m_platform);
    m_layers[index].filter = parsed.filter;
    m_layers[index].errors = parsed.errors;
    applyCaseSensitivity(layer);
}

void FilterStack::applyCaseSensitivity(FilterLayer layer)
{
    // 只在本栈显式覆盖过大小写时才动它：`MaskFilter::parse` 已经按平台填好了默认值，
    // 无条件覆盖会把「Windows 默认不敏感」这条规则又抹回敏感。
    if (!m_caseOverridden)
        return;
    m_layers[filterLayerIndex(layer)].filter.setCaseSensitivity(m_case);
}

// -----------------------------------------------------------------------------
// FilterLayerBinder
// -----------------------------------------------------------------------------

FilterLayerBinder::FilterLayerBinder() = default;

void FilterLayerBinder::setViewStore(SessionSettings *store)
{
    m_viewStore = store;
}

void FilterLayerBinder::setSessionStore(SessionSettings *store)
{
    m_sessionStore = store;
}

void FilterLayerBinder::setBuiltinDeclaration(const QString &declaration)
{
    m_builtin = declaration;
}

void FilterLayerBinder::setBuiltinSource(const QString &source)
{
    m_builtinSource = source;
}

bool FilterLayerBinder::canSaveLayer(FilterLayer layer) const
{
    if (!filterLayerIsEditable(layer))
        return false;
    return storeForLayer(layer, m_viewStore, m_sessionStore) != nullptr;
}

bool FilterLayerBinder::saveLayer(FilterLayer layer, const FilterStack &stack) const
{
    // 格式层先拦掉：它来自文件格式定义，用户改不了，也没有存储可写。
    if (!filterLayerIsEditable(layer))
        return false;

    SessionSettings *store = storeForLayer(layer, m_viewStore, m_sessionStore);
    if (!store) {
        // **不退而写入另一个存储**：视图存储没接上时写进会话存储，意味着用户的
        // 一次临时过滤变成了永久的，而他从没同意过。这与 SESS-007
        // 「写入目标层缺失时返回失败」是同一条纪律。
        return false;
    }

    const QString declaration = stack.declaration(layer);
    if (declaration.isEmpty()) {
        // 清空某一层是**正常操作**（用户把临时过滤删干净了）。删一个本来不存在的
        // 键会让 remove() 返回 false，但那不是失败——「这一层现在没有声明」这个
        // 目标已经成立。把它报成失败会让界面弹一个没有意义的错误。
        store->remove(filterDeclarationSettingKey());
        return true;
    }
    return store->setValue(filterDeclarationSettingKey(), declaration);
}

void FilterLayerBinder::loadInto(FilterStack *stack) const
{
    if (!stack)
        return;

    for (FilterLayer layer : allFilterLayers()) {
        // 先取旧状态再改声明：`enabled` 是界面上的启用开关（第 2 条），
        // 它不属于「存储里的声明」。整份替换会让每次重新载入都把用户的
        // 禁用选择抹掉，而现象是「我刚关掉的那一层又自己开起来了」。
        FilterLayerState state = stack->layerState(layer);

        switch (layer) {
        case FilterLayer::Format:
            state.declaration = m_builtin;
            state.source = m_builtinSource.isEmpty() ? QStringLiteral("文件格式定义")
                                                     : m_builtinSource;
            break;
        case FilterLayer::Session:
            // 从**会话存储**读，不从作用域链解析——见本文件顶部的说明。
            state.declaration = declarationFromStore(m_sessionStore);
            state.source = QStringLiteral("会话设置");
            break;
        case FilterLayer::View:
            state.declaration = declarationFromStore(m_viewStore);
            state.source = QStringLiteral("当前视图（临时）");
            break;
        }

        stack->setLayerState(layer, state);
    }
}

int FilterLayerBinder::discardViewLayer() const
{
    if (!m_viewStore)
        return 0;
    return m_viewStore->remove(filterDeclarationSettingKey()) ? 1 : 0;
}

QString FilterLayerBinder::describe() const
{
    QStringList parts;
    parts.append(QStringLiteral("格式层=%1").arg(
        m_builtin.isEmpty() ? QStringLiteral("（无内建声明）") : m_builtin));
    parts.append(QStringLiteral("会话存储=%1").arg(
        m_sessionStore ? QStringLiteral("已接上") : QStringLiteral("未接上")));
    parts.append(QStringLiteral("视图存储=%1").arg(
        m_viewStore ? QStringLiteral("已接上") : QStringLiteral("未接上")));
    return parts.join(QStringLiteral("；"));
}

// -----------------------------------------------------------------------------
// 面板
// -----------------------------------------------------------------------------

QString FilterLayerPanelRow::describe() const
{
    QString text = QStringLiteral("%1：%2（%3）").arg(label, statusText, storageLabel);
    if (!source.isEmpty())
        text += QStringLiteral("；来源 %1").arg(source);
    if (!expression.isEmpty())
        text += QStringLiteral("；表达式 %1").arg(expression);
    if (!errorText.isEmpty())
        text += QStringLiteral("；错误 %1").arg(errorText);
    if (decided > 0)
        text += QStringLiteral("；影响了 %1 个条目").arg(decided);
    return text;
}

bool EffectiveFilterPanel::empty() const
{
    return expression.isEmpty();
}

QString EffectiveFilterPanel::describe() const
{
    QStringList lines;
    lines.append(title);
    lines.append(semantics);
    lines.append(QString());

    lines.append(QStringLiteral("三层现状："));
    for (const FilterLayerPanelRow &row : layers) {
        lines.append(QStringLiteral("  · ") + row.describe());
    }

    lines.append(QString());
    if (expression.isEmpty()) {
        lines.append(QStringLiteral("最终生效表达式：（没有任何生效的过滤层，全部条目保留）"));
    } else {
        lines.append(QStringLiteral("最终生效表达式：%1").arg(expression));
        // 引号说明只在**真的出现引号**时才印：一个没有引号的表达式配上一段
        // 「引号只是显示用的」会让用户去找一个不存在的引号。
        if (expression.contains(QLatin1Char('\''))) {
            lines.append(QStringLiteral(
                "（表达式里每条掩码两侧的单引号只是显示用的引号，不是掩码语法的一部分；"
                "只有在掩码本身含空格或运算符字符时才会出现）"));
        }
    }

    lines.append(QString());
    lines.append(QStringLiteral("按 %1 个条目试算：%2（其中不可见 %3）")
                     .arg(total)
                     .arg(summary)
                     .arg(hidden()));

    lines.append(QStringLiteral("各层实际起决定作用的条目数："));
    for (const FilterLayerPanelRow &row : layers) {
        lines.append(QStringLiteral("  · %1：%2").arg(row.label).arg(row.decided));
    }

    return lines.join(QLatin1Char('\n'));
}

EffectiveFilterPanel buildEffectiveFilterPanel(const FilterStack &stack,
                                               const QVector<MaskSubject> &subjects)
{
    const FilterStackPreview preview = stack.preview(subjects);

    EffectiveFilterPanel panel;
    panel.title = QStringLiteral("查看最终生效的过滤");
    // 这一句就是规格边界要求的「层与层之间的关系必须明确（并集还是交集）」
    // 在界面上的落点。写成数据而不是写死在控件里，理由与语法速查表同源：
    // 手写在界面里的话，帮助页与面板会各演化出一种说法。
    panel.semantics = QStringLiteral(
        "层级语义：三层取交集——一个条目必须被每一条生效的层放行才可见；"
        "层内排除优先（任何一条排除规则命中即隐藏）。"
        "因此每多一条生效的过滤层，结果只会更少，不会更多。");
    panel.expression = stack.combinedExpression();
    panel.total = preview.total;
    panel.included = preview.included;
    panel.excluded = preview.excluded;
    panel.notMatched = preview.notMatched;
    panel.summary = preview.summary();

    for (FilterLayer layer : allFilterLayers()) {
        const FilterLayerState &state = stack.layerState(layer);
        const FilterLayerStats *stats = preview.statsFor(layer);

        FilterLayerPanelRow row;
        row.layer = layer;
        row.label = filterLayerLabel(layer);
        row.storageLabel = filterLayerStorageLabel(filterLayerStorage(layer));
        row.source = state.source;
        row.editable = filterLayerIsEditable(layer);
        row.enabled = state.enabled;
        row.active = state.active();
        row.ruleCount = state.filter.ruleCount();
        row.includeCount = state.filter.includeCount();
        row.excludeCount = state.filter.excludeCount();
        row.decided = stats ? stats->decided : 0;
        row.statusText = state.statusText();
        row.expression = filterLayerExpression(state);
        if (!state.errors.isEmpty())
            row.errorText = state.errors.first().describe();

        panel.layers.append(row);
    }

    return panel;
}

EffectiveFilterPanel buildEffectiveFilterPanel(const FilterStack &stack, const QStringList &names)
{
    QVector<MaskSubject> subjects;
    subjects.reserve(names.size());
    for (const QString &name : names)
        subjects.append(MaskSubject::forName(name));
    return buildEffectiveFilterPanel(stack, subjects);
}

} // namespace Filter
} // namespace LqCompare
