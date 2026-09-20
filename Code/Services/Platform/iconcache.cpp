#include "iconcache.h"

namespace LqCompare {
namespace Platform {

// -----------------------------------------------------------------------------
// 枚举的稳定标识
// -----------------------------------------------------------------------------

const char *iconSourceIdentifier(IconSource source)
{
    // 稳定的英文标识：会出现在日志与测试断言里，跟着界面语言变就没法跨版本比对。
    switch (source) {
    case IconSource::Missing: return "missing";
    case IconSource::System:  return "system";
    case IconSource::Builtin: return "builtin";
    }
    return "unknown-source";
}

const char *builtinIconIdentifier(BuiltinIcon icon)
{
    switch (icon) {
    case BuiltinIcon::File:    return "file";
    case BuiltinIcon::Folder:  return "folder";
    case BuiltinIcon::Unknown: return "unknown";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// 缓存
// -----------------------------------------------------------------------------

qreal IconCache::Stats::hitRate() const
{
    const int total = hits + misses;
    if (total <= 0)
        return 0.0;
    return static_cast<qreal>(hits) / static_cast<qreal>(total);
}

IconCache::IconCache(int capacity)
{
    setCapacity(capacity);
}

void IconCache::setCapacity(int capacity)
{
    // 容量至少留 1：0 容量的缓存会让每一次查询都是未命中，
    // 而「缓存开着但永远不命中」在界面上表现为「图标一直在闪」，
    // 排查者通常会先去怀疑图标源，很难想到是容量被设成了 0。
    m_capacity = qMax(1, capacity);

    while (m_entries.size() > m_capacity)
        evictIfNeeded();
}

bool IconCache::contains(const QString &cacheKey) const
{
    return m_entries.contains(cacheKey);
}

bool IconCache::lookup(const QString &cacheKey, IconEntry *entry)
{
    const auto it = m_entries.constFind(cacheKey);
    if (it == m_entries.constEnd()) {
        ++m_misses;
        return false;
    }

    ++m_hits;
    touch(cacheKey);
    if (entry)
        *entry = it.value();
    return true;
}

void IconCache::insert(const QString &cacheKey, const IconEntry &entry)
{
    m_entries.insert(cacheKey, entry);
    touch(cacheKey);
    evictIfNeeded();
}

void IconCache::clear()
{
    m_entries.clear();
    m_recency.clear();
    // 统计刻意**不**清零：清空缓存通常发生在「用户改了缩放比例」
    // 这类场景，而那正是最需要看到「命中率刚刚掉下去了」的时刻。
    // 要重置统计请新建一个实例。
}

IconCache::Stats IconCache::stats() const
{
    Stats result;
    result.hits = m_hits;
    result.misses = m_misses;
    result.evictions = m_evictions;
    result.size = m_entries.size();
    result.capacity = m_capacity;
    return result;
}

QStringList IconCache::keysByRecency() const
{
    return m_recency;
}

void IconCache::touch(const QString &cacheKey)
{
    // 已在末尾就不动：这一步是热路径（每画一行都会经过），
    // 而「刚查过的键就在末尾」是最常见的情况。
    if (!m_recency.isEmpty() && m_recency.last() == cacheKey)
        return;

    m_recency.removeAll(cacheKey);
    m_recency.append(cacheKey);
}

void IconCache::evictIfNeeded()
{
    while (m_entries.size() > m_capacity && !m_recency.isEmpty()) {
        const QString victim = m_recency.takeFirst();
        if (m_entries.remove(victim) > 0)
            ++m_evictions;
    }
}

// -----------------------------------------------------------------------------
// 请求队列
// -----------------------------------------------------------------------------

bool IconRequestQueue::enqueue(const QString &cacheKey)
{
    if (cacheKey.isEmpty())
        return false;

    // 已经在途（排队中或已交给后台）就不再排一次。
    // 这一行是 PLAT-004 第 3 条的全部要点：一个 10 万文件的目录里，
    // 它把在途请求数从 10 万压到「有几种扩展名」。
    if (m_inFlightSet.contains(cacheKey))
        return false;

    m_ready.append(cacheKey);
    m_inFlight.append(cacheKey);
    m_inFlightSet.insert(cacheKey);
    return true;
}

QStringList IconRequestQueue::takeReady(int count)
{
    if (count <= 0 || m_ready.isEmpty())
        return QStringList();

    const int taken = qMin(count, m_ready.size());
    QStringList result;
    result.reserve(taken);
    for (int i = 0; i < taken; ++i)
        result.append(m_ready.at(i));

    // 只从待办里摘掉，**不**从在途集合里摘——它们还没解析完。
    if (taken == m_ready.size())
        m_ready.clear();
    else
        m_ready = m_ready.mid(taken);

    return result;
}

void IconRequestQueue::finish(const QString &cacheKey)
{
    m_inFlightSet.remove(cacheKey);
    m_inFlight.removeAll(cacheKey);
    // 防御性地也清一下待办：正常流程下 takeReady() 已经把它摘走了，
    // 但若有人直接对没取过的键调用 finish()，留着会让它被重复解析一次。
    m_ready.removeAll(cacheKey);
}

void IconRequestQueue::clear()
{
    m_ready.clear();
    m_inFlight.clear();
    m_inFlightSet.clear();
}

} // namespace Platform
} // namespace LqCompare
