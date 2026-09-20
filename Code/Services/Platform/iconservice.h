#ifndef LQCOMPARE_ICONSERVICE_H
#define LQCOMPARE_ICONSERVICE_H

#include "Files/pathutils.h"
#include "iconcache.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

namespace LqCompare {
namespace Platform {

///
/// \brief 平台图标提供者（PRD: PLAT-004 完成标准第 1 条）。
///
/// 约定：
///   - **不访问文件系统内容**，只按类型（扩展名）解析。Windows 用
///     `SHGFI_USEFILEATTRIBUTES`；Linux 用 MIME 类型查主题图标；
///     macOS 用 `NSWorkspace` 的类型图标接口。这样对不存在的路径也能给出答案
///     （列表里显示一个已被删除的条目时不该先去 stat 一次再报错）。
///   - 拿不到图标时返回 `source = Missing` 的条目，**不要抛异常、不要崩**。
///     无桌面环境的 Linux、图标主题缺失、Shell 调用被策略拦下都会走到这里。
///   - 实现可能被后台线程调用，因此要么自身线程安全，要么由 IconService 加锁
///     （当前是后者，见 IconService::m_providerMutex）。
///
class IconProvider
{
public:
    virtual ~IconProvider();

    /// 平台名称，用于日志与测试断言（"windows" / "macos" / "linux" / "unsupported"）。
    virtual QString platformName() const = 0;

    /// 解析一个类型图标。
    ///
    /// \param extensionKey 来自 IconKey::fromPath()，恒为小写或在无扩展名时是
    ///                     IconKey::NoExtension；目录共用一个键。
    /// \param isDirectory  条目是不是目录。
    /// \param pixelSize    需要的像素尺寸（已按 DPI 换算过）。
    virtual IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) = 0;
};

///
/// \brief 平台提供者的工厂（PRD: PLAT-004 完成标准第 1、4 条）。
///
/// 与 createNativeFileSystem() 的约定一致：永不返回 nullptr。
/// 平台不被支持、或该构建没有实现时返回一个所有解析都答 Missing 的实现，
/// 于是 IconService 会统一回退到内置图标——而不是让上层到处判空。
///
IconProvider *createNativeIconProvider();

///
/// \brief 图标服务：缓存 + 去重 + 异步（PRD: PLAT-004 完成标准第 2~5 条）。
///
/// 三个职责，各自对应一条完成标准：
///   1. **缓存**（第 2 条）。按缓存键（扩展名 + 是否目录）缓存，有界 + LRU。
///   2. **去重 + 异步**（第 3 条）。请求先入 `IconRequestQueue` 去重，
///      再由**一个**后台线程批量解析。一个 10 万文件的目录在首次浏览时
///      只会对「有几种扩展名」发系统调用，而不是 10 万次。
///   3. **回退**（第 4 条）。系统图标拿不到时给出带 `BuiltinIcon` 种类的条目，
///      由视图层从内置图标集里选一张。回退结果**也进缓存**——
///      「这台机器没有桌面环境」不会在会话中途变，反复重问只会拖慢列表。
///
/// 为什么会话中途改变缩放比要清空缓存
/// --------------------------------
/// 缓存的条目里带着按当时 DPI 取来的位图。缩放比变了之后那些位图就是错的尺寸：
/// 16 像素的图在 200% 下被拉成 32 像素显示，看起来「有点糊」——
/// 而这种模糊很容易被归咎于「这软件做得不精细」。因此 setBaseSize() 与
/// setDevicePixelRatio() 会清空缓存，让下一次查询按新尺寸重新取图。
///
class IconService : public QObject
{
    Q_OBJECT

public:
    /// provider 由本对象接管（析构时 delete）。
    explicit IconService(IconProvider *provider, QObject *parent = nullptr);
    ~IconService() override;

    // --- 配置 --------------------------------------------------------------

    /// 路径风格。默认按平台给（Windows 用 `Style::windows()`，其余用 `Style::posix()`）。
    ///
    /// 为什么按平台给默认值，而不是从 FileSystem 反推：图标键只关心
    /// 「怎么切出路径最后一段」，与卷、大小写语义无关。要给别的平台的路径
    /// 算图标键（例如在 macOS 上预览 Windows 的路径）时才需要显式设置。
    void setStyle(const Files::PathUtils::Style &style) { m_style = style; }
    Files::PathUtils::Style style() const { return m_style; }

    /// 逻辑尺寸（界面上的 16 或 32）。改变它会清空缓存。0 表示不取图。
    void setBaseSize(int baseSize);
    int baseSize() const { return m_baseSize; }

    /// 设备像素比（高分屏上是 2.0 或 1.5）。改变它会清空缓存。
    void setDevicePixelRatio(qreal ratio);
    qreal devicePixelRatio() const { return m_devicePixelRatio; }

    /// 缓存容量。见 IconCache 的说明。
    void setCacheCapacity(int capacity) { m_cache.setCapacity(capacity); }

    /// 实际取图的像素尺寸（= iconPixelSize(baseSize, devicePixelRatio)）。
    int pixelSize() const;

    // --- 查询 --------------------------------------------------------------

    ///
    /// \brief 同步解析。缓存命中即一次哈希查找。
    ///
    /// **不要在界面线程上对大目录调用它**：未命中时会直接问平台，
    /// 一个目录树上千个未见过的扩展名会让界面卡住。界面走 requestIcon()。
    /// 它存在的意义是「已知只有一两个条目要问」的场合（拖拽悬停、
    /// 属性对话框的图标）以及测试。
    ///
    IconEntry iconForPath(const QString &path, bool isDirectory);

    ///
    /// \brief 异步请求某个路径的图标。
    ///
    /// 两种时机，调用方必须都能接受：
    ///   - **命中缓存**：`iconReady()` 在本次调用内同步发出；
    ///   - **未命中**：排入去重队列，由后台线程解析完后发信号。
    /// 命中同步发是刻意的：命中率高的目录里绝大多数条目会同步拿到图标，
    /// 只有第一次遇到某个扩展名时才是异步的。若一律异步，一次滚动会插入
    /// 成百上千个事件循环轮次，列表会出现可见的空图标闪烁。
    ///
    /// 同一个缓存键在途时本次调用什么也不做——它的解析结果到达时会发出
    /// 同一个键的信号，视图重新查一次缓存即可（见 iconReady 的说明）。
    ///
    void requestIcon(const QString &path, bool isDirectory);

    /// 同步等待所有在途请求结束（测试、「截图前先等一下」这类场景用）。
    ///
    /// 内部会跑事件循环，因此**不要在会重入的上下文里调用**
    /// （例如从 iconReady 的处理函数里）。返回 false 表示超时。
    bool waitForPending(int timeoutMilliseconds = 5000);

    IconCache::Stats cacheStats() const { return m_cache.stats(); }

    /// 还没结束的解析数（含已交给后台、结果尚未回来的）。
    int pendingRequestCount() const { return m_queue.pendingCount(); }

    /// 还在队列里等着被取走的解析数。
    int readyRequestCount() const { return m_queue.readyCount(); }

    QString providerName() const;

    /// 清空缓存但保留统计（统计不清零的理由见 IconCache::clear()）。
    void clearCache() { m_cache.clear(); }

signals:
    ///
    /// 某个**缓存键**的图标就绪。
    ///
    /// 为什么按缓存键而不是按路径发
    /// --------------------------
    /// 一个键可能对应上万个文件（同一目录下所有 `.txt`）。按路径逐个发信号
    /// 等于把刚从「10 万次系统调用」省下的工作量原样搬到信号系统上：
    /// 一次解析要发 10 万个信号，每个信号还要被视图处理一遍。
    /// 按键发则一次解析只有一个信号，视图收到后按需重查——
    /// 而重查是一次 O(1) 的哈希查找。
    ///
    void iconReady(const QString &cacheKey);

private:
    IconEntry resolveNow(const QString &cacheKey);
    IconEntry withFallback(const IconEntry &entry, bool isDirectory) const;
    QString cacheKeyForPath(const QString &path, bool isDirectory) const;
    void scheduleBackgroundResolve();
    void resolveInBackground(const QStringList &cacheKeys);
    void applyResult(const QString &cacheKey, const IconEntry &entry);

    IconProvider *m_provider = nullptr;

    /// 提供者不一定线程安全（Windows 的 Shell 调用尤其），因此所有
    /// provider->resolve() 都经过这把锁。锁的粒度是「一次解析」——
    /// 不设成「整个批量」，否则同批里的键会被串行化在同一把锁里等待，
    /// 而真正的瓶颈是系统调用本身。
    mutable QMutex m_providerMutex;

    IconCache m_cache;
    IconRequestQueue m_queue;

    /// 专属线程池。刻意不用全局池：析构时要能只等**自己**的任务结束
    /// （见 waitForPending 与析构函数），等全局池会把别人的任务也等进来。
    QThreadPool m_pool;

    Files::PathUtils::Style m_style;
    int m_baseSize = 16;
    qreal m_devicePixelRatio = 1.0;
};

} // namespace Platform
} // namespace LqCompare

#endif // LQCOMPARE_ICONSERVICE_H
