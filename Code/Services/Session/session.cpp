#include "session.h"

namespace LqCompare {

SessionSettings::SessionSettings(QObject *parent) : QObject(parent)
{
}

SessionSettings::~SessionSettings() = default;

// ---------------------------------------------------------------------------
// MemorySessionSettings
// ---------------------------------------------------------------------------

MemorySessionSettings::MemorySessionSettings(QObject *parent) : SessionSettings(parent)
{
}

QStringList MemorySessionSettings::keys() const
{
    // QVariantMap 底层是 QMap，keys() 已经是字典序，不必再 sort 一遍。
    return m_values.keys();
}

bool MemorySessionSettings::contains(const QString &key) const
{
    // 空键永远「不存在」：setValue 也拒绝空键，两边必须一致，
    // 否则会出现「contains 说没有、value 却能取到一个值」这种自相矛盾的状态。
    return !key.isEmpty() && m_values.contains(key);
}

QVariant MemorySessionSettings::value(const QString &key, const QVariant &fallback) const
{
    if (!contains(key)) {
        return fallback;
    }
    return m_values.value(key);
}

bool MemorySessionSettings::setValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty()) {
        return false;
    }
    // 「写入同一个值」返回 true 但不算改动：界面会把 changed 直接连到
    // 「会话变脏」，而用户点开设置看一眼再确定关掉不该让会话变脏。
    const bool existed = m_values.contains(key);
    if (existed && m_values.value(key) == value) {
        return true;
    }
    m_values.insert(key, value);
    emit changed(key);
    return true;
}

bool MemorySessionSettings::remove(const QString &key)
{
    if (!contains(key)) {
        return false;
    }
    m_values.remove(key);
    emit changed(key);
    return true;
}

void MemorySessionSettings::clear()
{
    if (m_values.isEmpty()) {
        return;
    }
    m_values.clear();
    // 空键承载「清空」这件事：调用方把 changed("") 理解成「别只看某一项了，全部重读」。
    // 逐项发 changed 也行，但那样接收方要处理 N 次通知，而它真正需要知道的是「全变了」。
    emit changed(QString());
}

} // namespace LqCompare
