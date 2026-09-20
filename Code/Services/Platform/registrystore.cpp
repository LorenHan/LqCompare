/// \file
/// \brief 注册表键值存储的抽象层与非平台相关的实现（PRD: PLAT-005）。
///
/// 本文件里没有任何平台相关的系统调用，因此它在**任意平台**上都被真实编译与执行。
/// 只有真正读写注册表的那一段在 registrystore_win.cpp 里。
/// 分法与本仓库 `Files/`、`Platform/iconkey.cpp` 的做法一致：
/// 平台规则抽出来能真实跑，只把系统调用留在薄层。

#include "registrystore.h"

#include <QSet>

#include <algorithm>

namespace LqCompare {
namespace Platform {

const char *registryValueKindIdentifier(RegistryValueKind kind)
{
    switch (kind) {
    case RegistryValueKind::String:
        return "reg_sz";
    case RegistryValueKind::ExpandString:
        return "reg_expand_sz";
    case RegistryValueKind::DWord:
        return "reg_dword";
    case RegistryValueKind::Unsupported:
        return "unsupported";
    }
    // 刻意不写 default：新增枚举值而忘了在这里补一条时，
    // 编译器会以 -Wswitch 报警（本项目开着这个警告），
    // 而不是让新类型静默地显示成 "reg_sz"。
    return "reg_sz";
}

QString registryValueKindName(RegistryValueKind kind)
{
    switch (kind) {
    case RegistryValueKind::String:
        return QStringLiteral("REG_SZ");
    case RegistryValueKind::ExpandString:
        return QStringLiteral("REG_EXPAND_SZ");
    case RegistryValueKind::DWord:
        return QStringLiteral("REG_DWORD");
    case RegistryValueKind::Unsupported:
        return QStringLiteral("其它类型");
    }
    return QStringLiteral("REG_SZ");
}

// ---------------------------------------------------------------------------
// RegistryValue
// ---------------------------------------------------------------------------

RegistryValue RegistryValue::of(const QString &text)
{
    RegistryValue value;
    value.kind = RegistryValueKind::String;
    value.string = text;
    return value;
}

RegistryValue RegistryValue::expandable(const QString &text)
{
    RegistryValue value;
    value.kind = RegistryValueKind::ExpandString;
    value.string = text;
    return value;
}

RegistryValue RegistryValue::ofNumber(quint32 number)
{
    RegistryValue value;
    value.kind = RegistryValueKind::DWord;
    value.dword = number;
    return value;
}

RegistryValue RegistryValue::ofRaw(quint32 rawType, const QByteArray &bytes)
{
    RegistryValue value;
    value.kind = RegistryValueKind::Unsupported;
    value.rawType = rawType;
    value.raw = bytes;
    return value;
}

bool RegistryValue::operator==(const RegistryValue &other) const
{
    if (kind != other.kind)
        return false;
    switch (kind) {
    case RegistryValueKind::String:
    case RegistryValueKind::ExpandString:
        return string == other.string;
    case RegistryValueKind::DWord:
        return dword == other.dword;
    case RegistryValueKind::Unsupported:
        // 逐字节比较：这一档存在的全部意义就是「一个字节都不能变」。
        // 用 QString 往返比较会在遇到无效 UTF-8 时把差异抹平
        // （与 PLAT-007 那条「无效字节用未配对代理承载」的教训同源）。
        return rawType == other.rawType && raw == other.raw;
    }
    return false;
}

QString RegistryValue::display() const
{
    switch (kind) {
    case RegistryValueKind::String:
        return QStringLiteral("\"%1\"").arg(string);
    case RegistryValueKind::ExpandString:
        return QStringLiteral("(展开)\"%1\"").arg(string);
    case RegistryValueKind::DWord:
        return QStringLiteral("0x%1 (%2)")
                .arg(dword, 8, 16, QLatin1Char('0'))
                .arg(dword);
    case RegistryValueKind::Unsupported:
        // 不假装认识它：说清楚类型号与字节数，用户拿着这个能去查。
        return QStringLiteral("<类型 %1 的 %2 字节原始数据>")
                .arg(rawType)
                .arg(raw.size());
    }
    return QString();
}

// ---------------------------------------------------------------------------
// RegistryStore 的共用规则
// ---------------------------------------------------------------------------

RegistryStore::~RegistryStore() = default;

QString RegistryStore::normalizeKey(const QString &key)
{
    // 统一分隔符、去掉首尾分隔符、折叠重复分隔符，再转小写。
    //
    // 为什么把 `\` 和 `/` 都当分隔符：Windows 上写路径时两种都常见
    // （`RegCreateKeyExW` 只认 `\`，但人会写成 `/`）。若在这里不统一，
    // 就会出现「用 `/` 写的键存在、用 `\` 查不到」这种只在手工调试时出现的怪事。
    QString text = key;
    text.replace(QLatin1Char('/'), QLatin1Char('\\'));

    QStringList parts;
    // 用 `Qt::SkipEmptyParts` 而不是 `QString::SkipEmptyParts`：后者在 Qt 5.15 起
    // 已弃用（编译期报 -Wdeprecated-declarations），Qt 6 里被删掉。
    // 两者行为相同，但前者在 Qt 5.15 / 6 上都不报警告。
    const QStringList raw = text.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    for (const QString &part : raw) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            parts << trimmed.toLower();
    }
    return parts.join(QLatin1Char('\\'));
}

QString RegistryStore::normalizeName(const QString &name)
{
    // `(Default)` 这个名字在注册表里不是一个真实名称，而是「没有名称」。
    // 不同地方会把它写成 `(Default)`、`(默认)`、`@` 或空串，这里统一成空串——
    // 否则「卸载时删掉我们写的默认值」会因为名称对不上而静默失败，
    // 留下一项「右键菜单里显示我们的 ProID」。
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String("(Default)")
            || trimmed == QLatin1String("(默认)") || trimmed == QLatin1String("@")) {
        return QString();
    }
    return trimmed.toLower();
}

QString RegistryStore::parentKey(const QString &key)
{
    const QString normalized = normalizeKey(key);
    const int cut = normalized.lastIndexOf(QLatin1Char('\\'));
    if (cut < 0)
        return QString();
    return normalized.left(cut);
}

int RegistryStore::keyDepth(const QString &key)
{
    const QString normalized = normalizeKey(key);
    if (normalized.isEmpty())
        return 0;
    return normalized.count(QLatin1Char('\\')) + 1;
}

bool isKeyEmpty(const RegistryStore &store, const QString &key)
{
    return store.subKeys(key).isEmpty() && store.valueNames(key).isEmpty();
}

// ---------------------------------------------------------------------------
// MemoryRegistryStore
// ---------------------------------------------------------------------------

MemoryRegistryStore::MemoryRegistryStore() = default;

bool MemoryRegistryStore::isAvailable(QString *reason) const
{
    Q_UNUSED(reason);
    // 内存后端在任何平台都能用——它不是平台能力，是功能可用性。
    // 「这个平台没有注册表」由 platformHasRegistry() 回答，界面置灰绑那个。
    return true;
}

Files::ErrorCode MemoryRegistryStore::defaultInjectedError()
{
    return Files::ErrorCode(Files::FileSystemError::PermissionDenied);
}

bool MemoryRegistryStore::keyExists(const QString &key) const
{
    return m_keys.contains(RegistryStore::normalizeKey(key));
}

bool MemoryRegistryStore::value(const QString &key, const QString &name, RegistryValue *out) const
{
    const auto entry = m_keys.constFind(RegistryStore::normalizeKey(key));
    if (entry == m_keys.constEnd())
        return false;

    const auto found = entry->values.constFind(RegistryStore::normalizeName(name));
    if (found == entry->values.constEnd())
        return false;

    if (out != nullptr)
        *out = *found;
    return true;
}

QStringList MemoryRegistryStore::subKeys(const QString &key) const
{
    const QString prefix = RegistryStore::normalizeKey(key);
    QStringList result;

    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
        const QString &candidate = it.key();
        // 只看**直接**子键：去掉前缀之后不能再含分隔符。
        // 判前缀时必须带上分隔符，否则 `a\bc` 会被当成 `a\b` 的子键——
        // 这一条在 Files/ 的路径规则里也踩过（`/mnt/net` 与 `/mnt/network`）。
        QString remainder;
        if (prefix.isEmpty())
            remainder = candidate;
        else if (candidate.startsWith(prefix + QLatin1Char('\\')))
            remainder = candidate.mid(prefix.length() + 1);
        else
            continue;

        if (remainder.isEmpty() || remainder.contains(QLatin1Char('\\')))
            continue;

        if (!result.contains(remainder))
            result << remainder;
    }

    std::sort(result.begin(), result.end());
    return result;
}

QStringList MemoryRegistryStore::valueNames(const QString &key) const
{
    const auto entry = m_keys.constFind(RegistryStore::normalizeKey(key));
    if (entry == m_keys.constEnd())
        return QStringList();

    QStringList named;
    bool hasDefault = false;

    for (auto it = entry->values.constBegin(); it != entry->values.constEnd(); ++it) {
        if (it.key().isEmpty()) {
            // (Default) 以**空串**出现在列表里，而不是 `(Default)` 这样的展示名：
            // 契约是「空串就是默认值」，调用方可以直接把它传回 value()。
            // 用展示名的话往返一次要靠 normalizeName 拆回来，多绕一圈且容易忘。
            hasDefault = true;
        } else {
            named << entry->valueSpellings.value(it.key());
        }
    }

    // 默认值排在最前，其余按拼法排序。清单必须稳定，否则校验报告每次的
    // diff 都不一样，看不出真正的变化。
    std::sort(named.begin(), named.end());

    QStringList ordered;
    if (hasDefault)
        ordered << QString();
    ordered << named;
    return ordered;
}

bool MemoryRegistryStore::isUnder(const QString &key, const QString &prefix)
{
    if (prefix.isEmpty())
        return true;
    return key == prefix || key.startsWith(prefix + QLatin1Char('\\'));
}

bool MemoryRegistryStore::shouldFail(const QString &key, const QString &name, bool writing,
                                     Files::ErrorCode *error)
{
    const QString normalizedKey = RegistryStore::normalizeKey(key);
    const QString normalizedName = RegistryStore::normalizeName(name);

    // 一次性注入排在最前：它比 failOnValue() 更具体（「就这一次」比「一直」更具体），
    // 而且它**取走即失效**，所以要在返回之前把自己从待办里摘掉。
    //
    // 只对写入生效：删除走的是「回滚」路径，而回滚能不能成功正是这个注入
    // 想让人看见的东西。
    if (writing) {
        for (int i = 0; i < m_nextWriteFailures.size(); ++i) {
            const auto &entry = m_nextWriteFailures.at(i);
            if (!isUnder(normalizedKey, entry.first.first))
                continue;
            if (normalizedName != entry.first.second)
                continue;

            // 先取出错误码，再移除——移除之后引用就悬了。
            const Files::ErrorCode injected = entry.second;
            m_nextWriteFailures.remove(i);

            if (error != nullptr)
                *error = injected;
            return true;
        }
    }

    // 先看更精确的「某个值」注入：两条都命中时，具体的那个说了算。
    for (const auto &entry : m_failingValues) {
        if (isUnder(normalizedKey, entry.first.first)
                && normalizedName == entry.first.second) {
            if (error != nullptr)
                *error = entry.second;
            return true;
        }
    }

    for (const auto &entry : m_failingKeys) {
        if (isUnder(normalizedKey, entry.first)) {
            if (error != nullptr)
                *error = entry.second;
            return true;
        }
    }

    return false;
}

bool MemoryRegistryStore::setValue(const QString &key, const QString &name,
                                   const RegistryValue &value, Files::ErrorCode *error)
{
    ++m_writeCalls;

    // 成功时要把出参清成「无错误」：调用方常常复用同一个 ErrorCode 变量，
    // 上一次的失败不清理就会在这一次被读成失败。
    if (error != nullptr)
        *error = Files::ErrorCode();

    if (shouldFail(key, name, true /*writing*/, error))
        return false;

    const QString normalizedKey = RegistryStore::normalizeKey(key);
    if (normalizedKey.isEmpty()) {
        if (error != nullptr) {
            *error = Files::ErrorCode(Files::FileSystemError::InvalidName);
        }
        return false;
    }

    Entry &entry = m_keys[normalizedKey];
    if (entry.spelling.isEmpty())
        entry.spelling = key;

    const QString normalizedName = RegistryStore::normalizeName(name);
    if (!entry.values.contains(normalizedName))
        entry.valueSpellings.insert(normalizedName, name);

    entry.values.insert(normalizedName, value);
    return true;
}

bool MemoryRegistryStore::removeValue(const QString &key, const QString &name,
                                      Files::ErrorCode *error)
{
    ++m_writeCalls;

    if (error != nullptr)
        *error = Files::ErrorCode();

    if (shouldFail(key, name, false /*删除，不是写入*/, error))
        return false;

    const auto entry = m_keys.find(RegistryStore::normalizeKey(key));
    if (entry == m_keys.end())
        return true; // 幂等：本来就没有

    const QString normalizedName = RegistryStore::normalizeName(name);
    entry->values.remove(normalizedName);
    entry->valueSpellings.remove(normalizedName);
    return true;
}

bool MemoryRegistryStore::removeKey(const QString &key, Files::ErrorCode *error)
{
    ++m_writeCalls;

    if (error != nullptr)
        *error = Files::ErrorCode();

    // 删键不是「写某个值」，所以一次性写入注入在这里不命中。
    // 想注入删键失败要用 failOnKey()。
    if (shouldFail(key, QString(), false, error))
        return false;

    const QString normalizedKey = RegistryStore::normalizeKey(key);
    if (normalizedKey.isEmpty())
        return true;

    for (auto it = m_keys.begin(); it != m_keys.end();) {
        if (isUnder(it.key(), normalizedKey))
            it = m_keys.erase(it);
        else
            ++it;
    }
    return true;
}

void MemoryRegistryStore::seedFrom(const RegistryStore &source, const QString &prefix)
{
    const QString normalizedPrefix = RegistryStore::normalizeKey(prefix);

    // 先把「有哪些键、每个键有哪些值」全部收集出来，再开始写。
    //
    // 为什么不能边遍历边写：预演场景下调用方可能把内存后端喂给自己
    // （`memory.seedFrom(memory)`），边遍历边写会让容器在遍历中增长，
    // 表现是死循环或内存爆掉。先把清单落定就没有这个问题。
    //
    // 也不能只靠 allKeys()：那是内存后端专有的，而这个函数要接受任意后端。
    // 因此用一个显式待办栈按 subKeys() 往下走。
    QStringList keys;
    QVector<QPair<QString, QString>> values;

    QStringList pending;
    pending << normalizedPrefix;

    QSet<QString> visited;
    while (!pending.isEmpty()) {
        const QString current = pending.takeFirst();
        if (visited.contains(current))
            continue;
        visited.insert(current);

        if (source.keyExists(current)) {
            keys << current;
            const QStringList names = source.valueNames(current);
            for (const QString &name : names)
                values << qMakePair(current, name);
        }

        const QStringList children = source.subKeys(current);
        for (const QString &child : children)
            pending << (current.isEmpty() ? child : current + QLatin1Char('\\') + child);
    }

    std::sort(keys.begin(), keys.end());

    for (const QPair<QString, QString> &pair : values) {
        RegistryValue value;
        if (!source.value(pair.first, pair.second, &value))
            continue;
        // 用 source 里的原始拼法写进去，这样 allKeys()/allValues() 读出来
        // 与真实存储里看到的一致，报告不会因为大小写而前后不一。
        setValue(pair.first, pair.second, value);
    }

    // 把「存在但一个值都没有」的键也补上：`keyExists` 与「有值」不是一回事，
    // 而卸载时「这个键是不是我建的」恰恰取决于前者。
    for (const QString &key : keys) {
        if (!m_keys.contains(key)) {
            Entry entry;
            entry.spelling = key;
            m_keys.insert(key, entry);
        }
    }
}

void MemoryRegistryStore::touchKey(const QString &key)
{
    const QString normalized = RegistryStore::normalizeKey(key);
    if (normalized.isEmpty())
        return;

    Entry &entry = m_keys[normalized];
    if (entry.spelling.isEmpty())
        entry.spelling = key;
}

void MemoryRegistryStore::failOnKey(const QString &key, const Files::ErrorCode &error)
{
    m_failingKeys.append(qMakePair(RegistryStore::normalizeKey(key), error));
}

void MemoryRegistryStore::failOnValue(const QString &key, const QString &name,
                                      const Files::ErrorCode &error)
{
    m_failingValues.append(qMakePair(
            qMakePair(RegistryStore::normalizeKey(key), RegistryStore::normalizeName(name)),
            error));
}

void MemoryRegistryStore::failNextWriteOnValue(const QString &key, const QString &name,
                                               const Files::ErrorCode &error)
{
    m_nextWriteFailures.append(qMakePair(
            qMakePair(RegistryStore::normalizeKey(key), RegistryStore::normalizeName(name)),
            error));
}

void MemoryRegistryStore::clearInjectedFailures()
{
    m_failingKeys.clear();
    m_failingValues.clear();
    // 一次性注入也要清：漏掉它的后果是「上一个用例留下的定时炸弹在下个用例里响」，
    // 而且报告指向的是一个和它无关的用例，极难定位。
    m_nextWriteFailures.clear();
}

QStringList MemoryRegistryStore::allKeys() const
{
    QStringList result;
    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it)
        result << it->spelling;
    std::sort(result.begin(), result.end());
    return result;
}

QStringList MemoryRegistryStore::allValues() const
{
    QStringList result;
    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
        for (auto value = it->values.constBegin(); value != it->values.constEnd(); ++value) {
            // `键\0值名`：用 NUL 作分隔符是因为键与值名里都可能出现 `\`，
            // 用它拼出来的字符串没法可靠地再切回两部分（测试里要按它比对残留）。
            result << (it->spelling + QLatin1Char('\0')
                       + it->valueSpellings.value(value.key()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace Platform
} // namespace LqCompare
