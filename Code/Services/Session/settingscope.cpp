#include "settingscope.h"

#include <QPair>
#include <QSet>

namespace LqCompare {

// -----------------------------------------------------------------------------
// 作用域的优先级
// -----------------------------------------------------------------------------

QVector<SettingScope> settingScopePriorityOrder()
{
    // 与 `allSettingScopes()` 是两件事，理由见头文件。
    return {SettingScope::View, SettingScope::Session, SettingScope::Type};
}

int settingScopeRank(SettingScope scope)
{
    switch (scope) {
    case SettingScope::View:
        return 0;
    case SettingScope::Session:
        return 1;
    case SettingScope::Type:
        return 2;
    }
    return 2; // 认不出的作用域按最低优先级处理，不会盖住任何一层
}

// -----------------------------------------------------------------------------
// ScopedSessionSettings
// -----------------------------------------------------------------------------

ScopedSessionSettings::ScopedSessionSettings(QObject *parent)
    : SessionSettings(parent)
{
}

ScopedSessionSettings::~ScopedSessionSettings() = default;

void ScopedSessionSettings::setLayer(SettingScope scope, SessionSettings *layer)
{
    m_layers[settingScopeRank(scope)] = layer;
}

SessionSettings *ScopedSessionSettings::layer(SettingScope scope) const
{
    return m_layers[settingScopeRank(scope)];
}

bool ScopedSessionSettings::hasLayer(SettingScope scope) const
{
    return layer(scope) != nullptr;
}

void ScopedSessionSettings::setSchema(const SettingsSchema *schema)
{
    m_schema = schema;
}

const SettingsSchema *ScopedSessionSettings::schema() const
{
    return m_schema;
}

SettingScope ScopedSessionSettings::writeScope() const
{
    return m_writeScope;
}

void ScopedSessionSettings::setWriteScope(SettingScope scope)
{
    // 不迁移已有值，理由见头文件。这里刻意连信号都不发：写入目标不是会话的
    // 「设置」，把它塞进 changed 会让会话因为下拉动了一下就变脏。
    m_writeScope = scope;
}

// -----------------------------------------------------------------------------
// 读：视图 → 会话 → 类型 → 出厂默认
// -----------------------------------------------------------------------------

QVariant ScopedSessionSettings::factoryDefault(const QString &key) const
{
    if (m_schema == nullptr || key.isEmpty()) {
        return QVariant();
    }
    const SettingItem *item = m_schema->findItem(key);
    if (item == nullptr) {
        return QVariant();
    }
    // 归一之后再交出去：会话文件里存的多行掩码是**一段文本**，而声明里的默认值是
    // 一个 QStringList。不归一的话「这个值等于默认值吗」会永远为假，
    // 于是「恢复默认」看起来没生效、干净的表单被判定成有改动。
    return item->normalized(item->defaultValue);
}

QVariant ScopedSessionSettings::resolvedValue(const QString &key) const
{
    if (key.isEmpty()) {
        return QVariant();
    }
    // 按优先级逐层问。**用 contains 而不是「值是不是空的」判断命中**：
    // 一个显式设成空串的值是一次真实的设置（用户可能就想让它空），
    // 按值判断会越过它去取下面那一层，于是用户发现自己清不掉这一项。
    for (SettingScope scope : settingScopePriorityOrder()) {
        const SessionSettings *store = layer(scope);
        if (store != nullptr && store->contains(key)) {
            return store->value(key);
        }
    }
    return factoryDefault(key);
}

QVariant ScopedSessionSettings::value(const QString &key, const QVariant &fallback) const
{
    const QVariant found = resolvedValue(key);
    // 空 QVariant 是「这条链上没有任何一环给得出值」的标记。出厂默认本身
    // 有可能就是一个空 QVariant（声明里没写 defaultValue），那种情况下面
    // factoryDefault() 也返回空 —— 两者都落到 fallback，是此处唯一说得通的取舍。
    if (!found.isValid()) {
        return fallback;
    }
    return found;
}

bool ScopedSessionSettings::contains(const QString &key) const
{
    if (key.isEmpty()) {
        return false;
    }
    for (SettingScope scope : settingScopePriorityOrder()) {
        const SessionSettings *store = layer(scope);
        if (store != nullptr && store->contains(key)) {
            return true;
        }
    }
    return false;
}

QStringList ScopedSessionSettings::keys() const
{
    // 三层并集，按字典序（接口约定）。用 QSet 而不是三次 append + 排序两次：
    // 同一项在视图层与会话层都有是常态（视图覆盖会话就是这么工作的），
    // 不去重的话 keys() 会把同一个键报两遍，而调用方多半拿它去驱动列表。
    QSet<QString> all;
    for (SettingScope scope : settingScopePriorityOrder()) {
        const SessionSettings *store = layer(scope);
        if (store == nullptr) {
            continue;
        }
        const QStringList layerKeys = store->keys();
        for (const QString &key : layerKeys) {
            all.insert(key);
        }
    }
    QStringList result(all.constBegin(), all.constEnd());
    result.sort();
    return result;
}

bool ScopedSessionSettings::sameValue(const QVariant &a, const QVariant &b)
{
    if (!a.isValid() && !b.isValid()) {
        return true;
    }
    if (a.isValid() != b.isValid()) {
        return false;
    }
    return a == b;
}

// -----------------------------------------------------------------------------
// 写：只落到写入目标那一层
// -----------------------------------------------------------------------------

bool ScopedSessionSettings::setValue(const QString &key, const QVariant &value)
{
    // 空键在这里就无法与「没设置」区分，因此直接拒掉。与
    // `MemorySessionSettings` 同一条约定，测试里两边都断言了。
    if (key.isEmpty()) {
        return false;
    }

    SessionSettings *target = layer(m_writeScope);
    if (target == nullptr) {
        // 不退而写入别的层，理由见头文件。
        return false;
    }

    // 有效值要**在写入之前**取。写成「先写再比」的话，写入目标层比下面那层低时
    // 会得出「变了」的错误结论（因为写的瞬间下面那层已经看不见了）。
    const QVariant before = resolvedValue(key);

    if (!target->setValue(key, value)) {
        return false;
    }

    if (!sameValue(before, resolvedValue(key))) {
        emit changed(key);
    }
    return true;
}

bool ScopedSessionSettings::remove(const QString &key)
{
    if (key.isEmpty()) {
        return false;
    }
    SessionSettings *target = layer(m_writeScope);
    if (target == nullptr) {
        return false;
    }

    const QVariant before = resolvedValue(key);
    if (!target->remove(key)) {
        // 目标层里本来就没有这一条。**不能再往下层删**：用户的意思是
        // 「这个会话不要单独设这一项了」，而不是「把类型的默认值也删掉」。
        return false;
    }
    if (!sameValue(before, resolvedValue(key))) {
        emit changed(key);
    }
    return true;
}

void ScopedSessionSettings::clear()
{
    clearLayer(m_writeScope);
}

int ScopedSessionSettings::clearLayer(SettingScope scope)
{
    SessionSettings *target = layer(scope);
    if (target == nullptr) {
        return 0;
    }

    // 先把会被影响的键与它们的有效值记下来，再清空，逐个比对。
    // 直接遍历 target->keys() 之外还得多想一层「清空之后这个键由谁接手」，
    // 而有效值的前后对比不用关心是谁接了手，结论就是用户看到的东西。
    const QStringList affected = target->keys();
    QVector<QPair<QString, QVariant>> before;
    before.reserve(affected.size());
    for (const QString &key : affected) {
        before.append(qMakePair(key, resolvedValue(key)));
    }

    target->clear();

    int removed = 0;
    for (const QPair<QString, QVariant> &entry : before) {
        if (target->contains(entry.first)) {
            // 清空之后目标层居然还有这个键（实现没清干净），不当成已丢弃。
            continue;
        }
        ++removed;
        if (!sameValue(entry.second, resolvedValue(entry.first))) {
            emit changed(entry.first);
        }
    }
    return removed;
}

// -----------------------------------------------------------------------------
// 解析诊断
// -----------------------------------------------------------------------------

bool ScopedSessionSettings::resolvedFromLayer(const QString &key, SettingScope *scope) const
{
    if (key.isEmpty()) {
        return false;
    }
    for (SettingScope candidate : settingScopePriorityOrder()) {
        const SessionSettings *store = layer(candidate);
        if (store != nullptr && store->contains(key)) {
            if (scope != nullptr) {
                *scope = candidate;
            }
            return true;
        }
    }
    return false;
}

QString ScopedSessionSettings::describeResolution(const QString &key) const
{
    SettingScope found = SettingScope::Session;
    if (resolvedFromLayer(key, &found)) {
        return QStringLiteral("来自「%1」").arg(settingScopeLabel(found));
    }
    if (factoryDefault(key).isValid()) {
        return QStringLiteral("来自该类型的出厂默认");
    }
    return QStringLiteral("未设置");
}

// -----------------------------------------------------------------------------
// 视图级改动：关标签即丢弃
// -----------------------------------------------------------------------------

QStringList ScopedSessionSettings::viewScopeKeys() const
{
    const SessionSettings *store = layer(SettingScope::View);
    if (store == nullptr) {
        return QStringList();
    }
    QStringList keys = store->keys();
    keys.sort();
    return keys;
}

int ScopedSessionSettings::discardViewScope()
{
    return clearLayer(SettingScope::View);
}

// -----------------------------------------------------------------------------
// 第 2 条：写入去向的文案与切换提示
// -----------------------------------------------------------------------------

QString writeDestinationText(SettingScope scope)
{
    return QStringLiteral("本次修改将保存到「%1」").arg(settingScopeLabel(scope));
}

QString ScopedSessionSettings::writeDestinationText() const
{
    return LqCompare::writeDestinationText(m_writeScope);
}

QString ScopeSwitchNotice::describe() const
{
    if (!ask) {
        return QStringLiteral("作用域切换：无需提示");
    }
    return QStringLiteral("作用域切换：%1").arg(text);
}

ScopeSwitchNotice scopeSwitchNotice(SettingScope from,
                                    SettingScope to,
                                    const QStringList &pendingKeys)
{
    ScopeSwitchNotice notice;
    if (from == to || pendingKeys.isEmpty()) {
        // 没有待定的改动就没必要问：切换作用域不影响任何已经生效的东西
        // （已经应用过的值留在原来那一层，见头文件里 `setWriteScope` 的说明）。
        return notice;
    }

    notice.ask = true;
    notice.title = QStringLiteral("切换设置作用域");
    notice.text = QStringLiteral("已有 %1 项改动尚未应用；切换到「%2」后，"
                                 "这 %1 项将保存到那里。")
                      .arg(pendingKeys.size())
                      .arg(settingScopeLabel(to));
    return notice;
}

QString ViewScopeClosePlan::describe() const
{
    if (!ask) {
        return QStringLiteral("关闭标签：没有仅作用于当前视图的设置");
    }
    return QStringLiteral("关闭标签：将丢弃 %1 项视图级设置").arg(keys.size());
}

ViewScopeClosePlan planViewScopeClose(const QStringList &viewScopeKeys)
{
    ViewScopeClosePlan plan;
    if (viewScopeKeys.isEmpty()) {
        // 一条都没有时一个字都不问。视图级设置用得多的地方（每次打开都调一下
        // 再看）如果关标签时也弹一次，用户会学会闭着眼睛点「确定」，
        // 于是真正的提示也一起失效。
        return plan;
    }

    plan.ask = true;
    plan.keys = viewScopeKeys;
    plan.keys.sort();
    plan.title = QStringLiteral("关闭这个标签");
    plan.text = QStringLiteral("有 %1 项设置只作用于当前视图；关闭这个标签后"
                               "它们会被丢弃，该会话会回到自己的默认值。")
                    .arg(plan.keys.size());
    return plan;
}

} // namespace LqCompare
