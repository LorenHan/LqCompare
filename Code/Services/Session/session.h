#ifndef LQCOMPARE_SESSION_SETTINGS_H
#define LQCOMPARE_SESSION_SETTINGS_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace LqCompare {

///
/// \brief 会话设置的抽象接口（PRD: SESS-001 契约里的 `sessionSettings`）。
///
/// **为什么设置定义在服务层，而不是会话基类旁边**：会话基类在 `Views/Session/`，
/// 而「某一项设置当前是什么值」是纯数据问题——作用域覆盖链（视图 → 会话 → 类型，
/// SESS-007）、预设库、以及将来按会话设置改变行为的过滤器都要读它。
/// 接口如果长在 `Views/` 里，服务层要用就只能反向依赖界面，而那条依赖正是
/// `tools/check_layering.py` 明令禁止的方向。所以接口放这里，基类引用它。
///
/// 本文件**只有接口与一个内存实现**，刻意不包含：作用域链的三层优先级、
/// 落盘格式、设置项的声明式描述、设置对话框。那些是 SESS-006 / SESS-007 的事。
///
/// 契约要点（子类实现时必须逐条满足，测试按这几条断言）：
///   - `value()` 在键不存在时返回调用方给的**回退值**，而不是空 QVariant；
///   - `setValue()` 遇到**空键**返回 false（空键在作用域链里无法与「没设置」区分）；
///   - `setValue()` 写入与当前**相同的值**返回 true 但**不发** `changed`——
///     「值没变也算改动」会让会话立刻变成脏状态，用户什么都没改就看到「要保存吗」；
///   - `remove()` 删一个不存在的键返回 false，且不发 `changed`；
///   - `clear()` 在本来就是空的时候不发 `changed`（同理：空转不是改动）。
///
class SessionSettings : public QObject
{
    Q_OBJECT

public:
    explicit SessionSettings(QObject *parent = nullptr);
    ~SessionSettings() override;

    /// 当前已显式设置过的键，按字典序。
    virtual QStringList keys() const = 0;

    virtual bool contains(const QString &key) const = 0;

    /// 读一个值。键不存在时返回 `fallback`——刻意不返回空 QVariant，
    /// 因为「这个键没被设置过」与「这个键被设成了一个无效值」在界面上是两回事。
    virtual QVariant value(const QString &key, const QVariant &fallback = QVariant()) const = 0;

    virtual bool setValue(const QString &key, const QVariant &value) = 0;

    virtual bool remove(const QString &key) = 0;

    virtual void clear() = 0;

signals:
    /// 某一项设置**确实变了**（值相同时不发，见类注释）。空键表示清空操作。
    void changed(const QString &key);
};

///
/// \brief 内存实现：只保证「这一次会话运行期间读得到、写得进」。
///
/// **它不落盘，也不参与作用域链。** 那是有意的：SESS-001 只要求契约里有一个
/// 可用的 `sessionSettings()`。基类默认返回 nullptr 的话，每个调用点都要判空，
/// 而「判空之后什么都不做」正是最难发现的一类缺陷——设置看起来保存了、其实丢了。
/// 给一个行为确定的内存实现，至少「读不到就是默认值、写进去当次有效」是诚实且可测的。
///
/// SESS-006 / SESS-007 落地时会**换掉实现**（换成带作用域链与落盘的那个），
/// 接口不变，因此基类与调用点都不需要改。
///
class MemorySessionSettings : public SessionSettings
{
    Q_OBJECT

public:
    explicit MemorySessionSettings(QObject *parent = nullptr);

    QStringList keys() const override;
    bool contains(const QString &key) const override;
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const override;
    bool setValue(const QString &key, const QVariant &value) override;
    bool remove(const QString &key) override;
    void clear() override;

private:
    QVariantMap m_values;
};

} // namespace LqCompare

#endif // LQCOMPARE_SESSION_SETTINGS_H
