#include "iconservice.h"

#include "iconkey.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>

#include <functional>

namespace LqCompare {
namespace Platform {

namespace {

///
/// \brief 把一段工作丢给线程池的最小壳子。
///
/// 用 `std::function` 而不是持有 `IconService*` 再直接调用私有方法：
/// 这样这个类不必是 IconService 的友元，也不必把「批量解析」这个内部方法
/// 放到公开接口上——一旦公开，下一个人就会在界面线程上直接调它，
/// 而那正是本条目要避免的。
///
class WorkItem : public QRunnable
{
public:
    explicit WorkItem(std::function<void()> work) : m_work(std::move(work)) {}

    void run() override
    {
        // 不允许异常穿出 run()：Qt 的线程池在异常逃出时会终止进程。
        // 图标解析走到这一步的异常只可能来自平台实现，
        // 而那属于「必须修」的 bug——但要让它表现为一个失败的图标请求
        // 而不是整个程序消失，用户至少还能继续用。
        try {
            m_work();
        } catch (...) {
            // 什么也不做：applyResult() 没被调用，键会留在在途集合里，
            // 下一次对该键的请求会重新排队（不会永久卡死）。
        }
    }

private:
    std::function<void()> m_work;
};

/// 按平台给默认的路径风格。理由见头文件里 setStyle() 的说明。
Files::PathUtils::Style defaultStyle()
{
#ifdef Q_OS_WIN
    return Files::PathUtils::Style::windows();
#else
    return Files::PathUtils::Style::posix();
#endif
}

///
/// \brief 没有原生实现时的兜底提供者。
///
/// 所有解析都答 Missing，于是 IconService 会统一回退到内置图标。
/// 它存在的意义与 UnsupportedTrashService 相同：让「永不返回 nullptr」这条
/// 契约成立，上层不必到处判空——而判空的代码一旦有一处漏掉，
/// 表现就是启动即崩，且只在那个平台上崩。
///
class UnsupportedIconProvider : public IconProvider
{
public:
    QString platformName() const override { return QStringLiteral("unsupported"); }

    IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) override
    {
        Q_UNUSED(extensionKey);
        Q_UNUSED(isDirectory);
        Q_UNUSED(pixelSize);
        return IconEntry();
    }
};

} // namespace

// -----------------------------------------------------------------------------
// 提供者接口
// -----------------------------------------------------------------------------

IconProvider::~IconProvider() = default;

// -----------------------------------------------------------------------------
// 服务
// -----------------------------------------------------------------------------

IconService::IconService(IconProvider *provider, QObject *parent)
    : QObject(parent),
      m_provider(provider != nullptr ? provider : new UnsupportedIconProvider()),
      m_style(defaultStyle())
{
    // 单线程：图标解析是「问系统要一张图」的 I/O 型工作，而且都经过
    // m_providerMutex。多开线程只会让它们在锁上排队，同时把位图内存
    // 峰值乘上线程数，换不到吞吐。
    m_pool.setMaxThreadCount(1);
}

IconService::~IconService()
{
    // 必须等自己的任务结束再析构：后台任务持有 this，
    // 不等待就会在任务里访问已经释放的对象。
    // 这也是用专属线程池而不是全局池的原因——等全局池会把别的模块的
    // 长任务也一并等进来。
    m_pool.waitForDone();
    delete m_provider;
    m_provider = nullptr;
}

void IconService::setBaseSize(int baseSize)
{
    if (m_baseSize == baseSize)
        return;
    m_baseSize = baseSize;
    // 缓存条目里的位图是按旧尺寸取的，留着就是「在 200% 屏上显示 16 像素的图」。
    m_cache.clear();
}

void IconService::setDevicePixelRatio(qreal ratio)
{
    // 用 qFuzzyCompare 而不是直接比较：调用方通常是从一个 qreal 属性抄过来的，
    // 值可能差一个浮点尾数。不比较的话每次都会白清一次缓存。
    if (qFuzzyCompare(m_devicePixelRatio, ratio))
        return;
    m_devicePixelRatio = ratio;
    m_cache.clear();
}

int IconService::pixelSize() const
{
    return iconPixelSize(m_baseSize, m_devicePixelRatio);
}

QString IconService::providerName() const
{
    return m_provider != nullptr ? m_provider->platformName() : QStringLiteral("none");
}

QString IconService::cacheKeyForPath(const QString &path, bool isDirectory) const
{
    return IconKey::cacheKey(IconKey::fromPath(path, m_style), isDirectory);
}

IconEntry IconService::withFallback(const IconEntry &entry, bool isDirectory) const
{
    if (entry.usable())
        return entry;

    // 回退到「普通文件 / 目录」两类。只有这两类是真的能从现有信息判断出来的：
    // 再细分（可执行、压缩包、图片……）需要知道 MIME 类型，而 MIME 类型恰恰是
    // 系统图标那条路才会告诉我们的东西——系统图标没拿到，我们并不知道它是什么。
    // 与其猜一个，不如给一个中性的通用图标。
    IconEntry fallback;
    fallback.source = IconSource::Builtin;
    fallback.builtin = isDirectory ? BuiltinIcon::Folder : BuiltinIcon::File;
    return fallback;
}

IconEntry IconService::resolveNow(const QString &cacheKey)
{
    // 从键里解出扩展名与目录标志，而不是让调用方再传一遍——
    // 传两遍就有两个可能不一致的来源，而不一致的表现是「缓存按 A 存、
    // 解析按 B 问」，查起来极其费劲。
    QString extension;
    bool directory = false;
    if (!IconKey::parseCacheKey(cacheKey, &extension, &directory)) {
        // 键不合法：宁可回退到内置图标，也不要拿一个半截的键去问系统。
        extension = QString::fromLatin1(IconKey::NoExtension);
        directory = false;
    }

    const int pixels = pixelSize();

    IconEntry entry;
    {
        QMutexLocker locker(&m_providerMutex);
        entry = m_provider->resolve(extension, directory, pixels);
    }

    entry = withFallback(entry, directory);
    entry.cacheKey = cacheKey;
    return entry;
}

IconEntry IconService::iconForPath(const QString &path, bool isDirectory)
{
    const QString key = cacheKeyForPath(path, isDirectory);

    IconEntry entry;
    if (m_cache.lookup(key, &entry))
        return entry;

    entry = resolveNow(key);
    m_cache.insert(key, entry);
    return entry;
}

void IconService::requestIcon(const QString &path, bool isDirectory)
{
    const QString key = cacheKeyForPath(path, isDirectory);

    // 已经在途：什么也不做。
    //
    // 刻意**不**为这个路径登记一个等待者。按路径登记意味着等待者列表的
    // 大小与文件数成正比（10 万条），而我们已经有「按键发信号 + 视图重查」
    // 这条更省的路。这里提前返回还有一个好处：不碰缓存统计，
    // 于是命中率反映的是「有多少次查询真的用上了缓存」，
    // 而不是被同一批重复请求稀释成一个看不出问题的数字。
    if (m_queue.isPending(key))
        return;

    IconEntry entry;
    if (m_cache.lookup(key, &entry)) {
        // 命中：同步发。理由见头文件。
        emit iconReady(key);
        return;
    }

    if (m_queue.enqueue(key))
        scheduleBackgroundResolve();
}

void IconService::scheduleBackgroundResolve()
{
    // 一次把所有待办都取走，交给一个后台任务顺序处理。
    //
    // 不按固定批量（例如每次 16 个）分批的理由：键的总数是「扩展名数」，
    // 本来就只有几十个量级，分批只会增加线程池任务数与事件轮次，
    // 换不到任何东西（而且每个任务都要重新加锁一次）。
    const QStringList keys = m_queue.takeReady(m_queue.readyCount());
    if (keys.isEmpty())
        return;

    m_pool.start(new WorkItem([this, keys]() { resolveInBackground(keys); }));
}

void IconService::resolveInBackground(const QStringList &cacheKeys)
{
    for (const QString &key : cacheKeys) {
        const IconEntry entry = resolveNow(key);

        // 结果必须回到本对象所在线程再落缓存、再发信号。
        // 直接在后台线程里 emit 会让槽函数在后台线程上执行（自动连接在
        // 跨线程时退化为队列连接，而这里连信号都不该从未注册类型的线程发）。
        QMetaObject::invokeMethod(
            this,
            [this, key, entry]() { applyResult(key, entry); },
            Qt::QueuedConnection);
    }
}

void IconService::applyResult(const QString &cacheKey, const IconEntry &entry)
{
    // 先解除在途，再落缓存。顺序不能反：若先落缓存而 finish() 抛错或
    // 中途返回，那个键会永远留在在途集合里，之后所有同类请求都被
    // 「已经在途」挡掉，表现为「某些扩展名永远没有图标」。
    m_queue.finish(cacheKey);

    // 回退结果也缓存。系统图标拿不到（无桌面环境、主题缺失）不会在
    // 会话中途变化，反复重问只会让列表一直慢。
    m_cache.insert(cacheKey, entry);

    emit iconReady(cacheKey);
}

bool IconService::waitForPending(int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();

    // 先跑一次事件循环让已经在池里排队的任务有机会开始，
    // 否则刚 requestIcon() 完立刻调用本函数时队列里还什么都没有，
    // 会「立刻返回 true」而其实结果还没回来。
    while (m_queue.pendingCount() > 0) {
        if (timer.elapsed() >= timeoutMilliseconds)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

// -----------------------------------------------------------------------------
// 平台实现的声明（定义在各自的平台文件里）
// -----------------------------------------------------------------------------

// 与 createNativeFileSystem() 同一套手法：三个平台各写一个工厂，
// 这里只声明，用条件编译选一个——平台实现要调用各自的系统 API，
// 那些头文件在别的平台上根本不存在，必须在编译期就排除掉。
IconProvider *createWindowsIconProvider();
IconProvider *createMacIconProvider();
IconProvider *createLinuxIconProvider();

IconProvider *createNativeIconProvider()
{
#ifdef Q_OS_WIN
    return createWindowsIconProvider();
#elif defined(Q_OS_MACOS)
    return createMacIconProvider();
#else
    return createLinuxIconProvider();
#endif
}

// 注：未被选中的那两个工厂只是声明，不会产生「未定义符号」——
// 只要它们没有被调用。这与 filesystem.cpp / trash.cpp 的做法一致。

} // namespace Platform
} // namespace LqCompare
