#ifndef LQCOMPARE_ICONCACHE_H
#define LQCOMPARE_ICONCACHE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace LqCompare {
namespace Platform {

///
/// \brief 一个图标的来源（PRD: PLAT-004 完成标准第 3、4 条）。
///
/// 把「来源」显式记下来，而不是「拿到图就是拿到了」：界面需要据此决定
/// 要不要动手上的回退逻辑，测试也需要据此断言「无桌面环境时确实回退了」
/// ——只断言「没有崩」是测不出问题的。
///
enum class IconSource {
    Missing = 0, ///< 什么都没拿到（连回退也没做，通常意味着调用方还该再问一次）
    System,      ///< 系统关联图标
    Builtin,     ///< 内置回退图标（系统图标不可用时）
};

const char *iconSourceIdentifier(IconSource source);

///
/// \brief 内置回退图标的种类（PRD: PLAT-004 完成标准第 4 条）。
///
/// 为什么这里只给「种类」，不给具体的资源名
/// ------------------------------------
/// 具体资源属于界面层（`Code/Pictures/`）。若在服务层里写
/// `"ribbon_generic_file.svg"`，会出现两个问题：
///   1. 服务层凭空空引用一个界面层的资源名，「服务层不得依赖界面」这条
///      铁律就变味了——它虽然没有 include 界面头文件，却已经知道了界面的实现细节；
///   2. `tools/check_icons.py` 的声明表在 `Pictures/` 里，服务层引用一个表外的
///      名字会被它判为「引用了不存在的图标」。
/// 所以这里给出「这是什么类型的条目」，由视图层决定画哪张图。
///
enum class BuiltinIcon {
    File = 0, ///< 普通文件
    Folder,   ///< 目录
    Unknown,  ///< 连条目类型都没确定（调用方没告诉我们是文件还是目录）
};

const char *builtinIconIdentifier(BuiltinIcon icon);

///
/// \brief 一个已解析的图标（PRD: PLAT-004）。
///
/// payload 是不透明的 QVariant 而不是 QIcon，理由是分层与可测性：
/// 真实实现把 `QIcon` 放进去，测试把 `QString` 放进去，于是**缓存与调度逻辑
/// 可以只链接 QtCore 就被完整覆盖**，不必为了测一个哈希表去起一个 QGuiApplication。
/// 代价是类型不那么紧——但真正用得上 payload 的只有视图层一处，
/// 而那处本来就要做 QVariant -> QIcon 的转换（多平台图标源的返回值本就不是同一种东西：
/// Windows 给的是映像列表里的一个索引，Linux 给的是主题里的一个名字，
/// macOS 给的是现成的 NSImage）。
///
struct IconEntry
{
    QString cacheKey;  ///< 见 IconKey::cacheKey()
    IconSource source = IconSource::Missing;

    /// source 为 Builtin 时有意义。source 为 System 时是 Unknown。
    BuiltinIcon builtin = BuiltinIcon::Unknown;

    /// 真实实现里是 QIcon。source 为 Missing 时无效。
    QVariant payload;

    /// 实际拿到的图标边长（像素）。
    ///
    /// 与请求尺寸不一致时说明是平台能力所限：Windows 的 Shell 只有
    /// 16 / 32 / 48 / 256 这几档（见 Win32IconSize），要 24 就会拿到 16 或 32。
    /// 把这个数记下来，视图才能决定「要不要自己缩一下」以及「要不要提示
    /// 这张图其实比请求的小」。只写「拿到了」而不写尺寸，
    /// 那点模糊就永远只能靠肉眼发现。
    int actualPixelSize = 0;

    /// 拿到的图有没有用：既要是系统给的，也要真的带载荷。
    /// 单独抽成函数，是为了让「什么叫可用」只定义一次——
    /// 界面上出错最常见的一类就是两处判断标准不一致。
    bool usable() const { return source == IconSource::System && payload.isValid(); }

    /// 系统图标没拿到时，能不能退回内置图标。
    bool hasFallback() const { return builtin != BuiltinIcon::Unknown; }
};

///
/// \brief 按「类型」缓存图标（PRD: PLAT-004 完成标准第 2 条）。
///
/// 有界 + LRU：键的数量受「出现过多少种扩展名」限制，但仍要有上限——
/// 一个扫描了几十万文件的会话里，每个键后面都可能挂着 256×256 的位图
/// （256 KB），无上限就会随会话时长一路涨上去。
/// 默认容量 128：按最坏情况算约 32 MB 的位图，而真实场景里一个目录树
/// 出现的扩展名通常只有几十种，128 已经足够装下并且不会被淘汰打断。
///
/// 淘汰顺序是确定性的（最久未被查过的先出），因此可以被测试断言。
/// 这里刻意不用 `QCache`：它的淘汰顺序没有契约，测不了「先淘汰哪个」，
/// 而「先淘汰哪个」恰恰是命中率的关键。
///
class IconCache
{
public:
    struct Stats
    {
        int hits = 0;       ///< 查到过的次数
        int misses = 0;     ///< 没查到的次数
        int evictions = 0;  ///< 因容量上限被淘汰掉的条目数
        int size = 0;       ///< 当前条目数
        int capacity = 0;   ///< 容量上限

        /// 命中率。一次都没查过时返回 0，而不是 1.0，也不是 NaN——
        /// 界面上显示「命中率 100%」而其实一次都没查过，是最坏的一种正确。
        qreal hitRate() const;
    };

    explicit IconCache(int capacity = 128);

    int capacity() const { return m_capacity; }

    /// 设置容量上限。调小到低于当前条目数时立刻淘汰到只剩 capacity 个。
    /// 不这么做的话「设置里把缓存调小」会看起来毫无效果，
    /// 直到下次插入才突然掉一批。
    void setCapacity(int capacity);

    /// 查一次。命中会更新「最近使用」并计入 hits；未命中计入 misses。
    /// 结果**拷贝**出来而不是返回内部指针：返回 QHash 内部指针会在
    /// 下一次插入引发 rehash 时变成悬空指针，而那是一类极难查的崩溃。
    bool lookup(const QString &cacheKey, IconEntry *entry);

    /// 是否在缓存里。**不计入命中统计**——它只用于诊断与测试，
    /// 把它算成命中会让命中率这个指标失去意义。
    bool contains(const QString &cacheKey) const;

    void insert(const QString &cacheKey, const IconEntry &entry);

    void clear();

    Stats stats() const;

    /// 按「最久未使用在前」列出所有键。仅供测试与诊断用：
    /// 淘汰顺序是本类的一条契约，必须能被断言。
    QStringList keysByRecency() const;

private:
    void touch(const QString &cacheKey);
    void evictIfNeeded();

    QHash<QString, IconEntry> m_entries;

    /// 最旧的在前，最近使用的在最后。
    /// 用 QStringList 而不是链表：容量有上限且很小（默认 128），
    /// 每次 `indexOf`/`removeAt` 的线性开销远小于维护一个链表结构的复杂度。
    QStringList m_recency;

    int m_capacity = 128;
    int m_hits = 0;
    int m_misses = 0;
    int m_evictions = 0;
};

///
/// \brief 图标解析请求的去重队列（PRD: PLAT-004 完成标准第 3 条）。
///
/// 为什么必须有去重
/// ---------------
/// 规格的边界写得很直接：「图标获取必须缓存并异步，否则在大目录下会因逐个
/// 系统调用而严重变慢」。缓存只解决了第二次以后的问题，**第一次浏览**一个
/// 10 万文件的目录时，缓存里一个条目都没有，于是两件事会同时发生：
///   - 10 万次图标请求全部入队；
///   - 其中绝大多数问的是同几十种扩展名。
/// 去重把「在途请求数」从文件数压到**扩展名数**，这是唯一能把它压下来的办法——
/// 缓存做不到，因为请求入队时缓存还是空的。
///
/// 去重的第二个好处是「不重复做注定重复的工作」：10 万个请求若不去重，
/// 后台线程会对着同一种扩展名问系统 10 万次，问出 10 万个相同答案。
///
class IconRequestQueue
{
public:
    /// 入队。
    /// 返回 true 表示这是**需要真正解析**的新键；
    /// 返回 false 表示同一个键已经在途，调用方不必再发一次。
    bool enqueue(const QString &cacheKey);

    /// 从队首取出最多 count 个待解析的键。
    ///
    /// 取出后这些键仍然算「在途」（`isPending()` 为真），只是离开了待办列表——
    /// 这样在解析完成之前到来的同类请求不会又排一次队。
    /// count <= 0 时返回空列表。
    QStringList takeReady(int count);

    /// 标记一个键的解析已结束（成功或失败都算）。
    ///
    /// 调用后同一个键可以再次入队——这一点很要紧：解析失败（例如无桌面环境）
    /// 时若不解除在途，那个扩展名会**永远**不再被重试，
    /// 用户切回有桌面的环境也拿不到图标。
    void finish(const QString &cacheKey);

    bool isPending(const QString &cacheKey) const { return m_inFlightSet.contains(cacheKey); }

    /// 还没结束的键数（含已经交给后台、结果尚未回来的）。
    int pendingCount() const { return m_inFlight.size(); }

    /// 还在队列里等待被取走的键数。
    int readyCount() const { return m_ready.size(); }

    /// 在途的键，按入队顺序。仅供测试与诊断——去重与顺序都是本类的契约，
    /// 必须能被断言。
    QStringList pending() const { return m_inFlight; }

    void clear();

private:
    /// 已入队但还没被 takeReady() 取走的（待办）。
    QStringList m_ready;

    /// 已入队且尚未 finish() 的全部键，按入队顺序；与 m_inFlightSet 同源。
    QStringList m_inFlight;
    QSet<QString> m_inFlightSet;
};

} // namespace Platform
} // namespace LqCompare

#endif // LQCOMPARE_ICONCACHE_H
