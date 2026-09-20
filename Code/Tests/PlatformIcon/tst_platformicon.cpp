#include "tst_platformicon.h"

#include "iconcache.h"
#include "iconkey.h"
#include "iconservice.h"

#include <QIcon>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>

#include <memory>

using namespace LqCompare::Platform;

// -----------------------------------------------------------------------------
// 失败信息可读化
// -----------------------------------------------------------------------------

namespace QTest {

template <>
char *toString(const IconSource &source)
{
    return qstrdup(iconSourceIdentifier(source));
}

template <>
char *toString(const BuiltinIcon &icon)
{
    return qstrdup(builtinIconIdentifier(icon));
}

} // namespace QTest

namespace {

const LqCompare::Files::PathUtils::Style kPosix = LqCompare::Files::PathUtils::Style::posix();
const LqCompare::Files::PathUtils::Style kWindows = LqCompare::Files::PathUtils::Style::windows();

///
/// \brief 只做记录的内存图标源。
///
/// 它把 payload 设成 QString 而不是 QIcon，这是刻意的：缓存与调度逻辑
/// 不该为了被测试就去起一个 QGuiApplication。真实实现往同一个
/// QVariant 里放 QIcon。
///
class RecordingIconProvider : public IconProvider
{
public:
    QString platformName() const override { return QStringLiteral("recording"); }

    /// 记录形式 "f:txt@32" / "d:txt@16"，既能看清问的是文件还是目录、
    /// 问了哪个扩展名，也能看清按什么尺寸问的。
    IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) override
    {
        QMutexLocker locker(&m_mutex);

        m_calls.append(QStringLiteral("%1:%2@%3")
                           .arg(isDirectory ? QStringLiteral("d") : QStringLiteral("f"))
                           .arg(extensionKey)
                           .arg(pixelSize));

        if (m_failEverything || (isDirectory && m_failDirectories))
            return IconEntry();

        IconEntry entry;
        entry.source = IconSource::System;
        entry.actualPixelSize = pixelSize;
        entry.payload = QStringLiteral("icon:%1").arg(extensionKey);
        return entry;
    }

    void setFailEverything(bool fail)
    {
        QMutexLocker locker(&m_mutex);
        m_failEverything = fail;
    }

    void setFailDirectories(bool fail)
    {
        QMutexLocker locker(&m_mutex);
        m_failDirectories = fail;
    }

    QStringList calls() const
    {
        QMutexLocker locker(&m_mutex);
        return m_calls;
    }

    int callCount() const
    {
        QMutexLocker locker(&m_mutex);
        return m_calls.size();
    }

private:
    mutable QMutex m_mutex;
    QStringList m_calls;
    bool m_failEverything = false;
    bool m_failDirectories = false;
};

} // namespace

// -----------------------------------------------------------------------------
// 1. 缓存键（完成标准第 2 条）
// -----------------------------------------------------------------------------

void TstPlatformIcon::extensionKeyTakesThePartAfterTheLastDot()
{
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/b.txt"), kPosix), QStringLiteral("txt"));
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/b.tar.gz"), kPosix), QStringLiteral("gz"));
    QCOMPARE(IconKey::fromPath(QStringLiteral("noext"), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
}

void TstPlatformIcon::extensionKeyIgnoresDotsInDirectoryNames()
{
    // 这一条是本组里最要紧的一个：目录名里的点不是扩展名。
    // 若在全路径上找最后一个点，`/a.d/b` 会得到 "b"（把文件名整段当扩展名），
    // 于是那个目录下的所有文件共用一个错误的键——表现是「图标不对」，
    // 而不会崩、不会报错，只能靠肉眼发现。
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a.d/b"), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a.d/b.txt"), kPosix), QStringLiteral("txt"));

    // Windows 风格下 `\` 也是分隔符，同样只看最后一段。
    QCOMPARE(IconKey::fromPath(QStringLiteral("C:\\a.d\\b"), kWindows),
             QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral("C:\\a.d\\b.txt"), kWindows), QStringLiteral("txt"));
}

void TstPlatformIcon::extensionKeyIsCaseFolded()
{
    // 折叠小写本身就是命中率的一部分：同一个目录里 A.TXT 与 a.txt
    // 若各成一个键，命中率会凭空掉一半。文件类型关联也不区分大小写。
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/A.TXT"), kPosix),
             IconKey::fromPath(QStringLiteral("/a/a.txt"), kPosix));
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/A.TXT"), kPosix), QStringLiteral("txt"));
}

void TstPlatformIcon::extensionKeyOfDotFileKeepsTheNameAfterTheDot()
{
    // 直觉上 `.gitignore` 是「隐藏文件、无扩展名」，但 Windows Shell 的
    // PathFindExtension 给的是 "gitignore"。我们要的是「和系统给的答案一致」
    // ——这个键是要拿去问系统图标的，键不一致就会拿到另一套图标。
    QCOMPARE(IconKey::fromPath(QStringLiteral("/repo/.gitignore"), kPosix),
             QStringLiteral("gitignore"));
    QCOMPARE(IconKey::fromPath(QStringLiteral(".bashrc"), kPosix), QStringLiteral("bashrc"));
}

void TstPlatformIcon::trailingDotHasNoExtension()
{
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/name."), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral("."), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral(".."), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral("..."), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
}

void TstPlatformIcon::extensionKeyTrimsSurroundingBlanks()
{
    // `a.txt ` 这种名字在磁盘上确实存在（PLAT-007 会在**新建/重命名**时拦下它，
    // 但已经存在的文件仍要能显示）。留着空格会让它单独占一个缓存键，
    // 而系统里并没有「带空格的 txt 类型」这种东西。
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/b.txt "), kPosix), QStringLiteral("txt"));
    QCOMPARE(IconKey::fromPath(QStringLiteral("/a/b. "), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
}

void TstPlatformIcon::pathWithoutNameHasNoExtension()
{
    QCOMPARE(IconKey::fromPath(QString(), kPosix), QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral("/"), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
    QCOMPARE(IconKey::fromPath(QStringLiteral("///"), kPosix),
             QString::fromLatin1(IconKey::NoExtension));
}

void TstPlatformIcon::cacheKeySeparatesFilesFromDirectories()
{
    // 没有这一条，一个叫 `notes.txt` 的**目录**会把所有 .txt 文件的缓存项
    // 覆盖成文件夹图标，而且这个错误会一直留在缓存里（命中率越高，错得越久）。
    // 它不崩、不报错，只让用户看到一堆莫名其妙的文件夹图标。
    QVERIFY(IconKey::cacheKey(QStringLiteral("txt"), false)
            != IconKey::cacheKey(QStringLiteral("txt"), true));
    QCOMPARE(IconKey::cacheKey(QStringLiteral("txt"), false), QStringLiteral("f|txt"));
    QCOMPARE(IconKey::cacheKey(QStringLiteral("txt"), true), QStringLiteral("d|txt"));
}

void TstPlatformIcon::cacheKeyOfExtensionlessDirectoryUsesSentinel()
{
    QCOMPARE(IconKey::cacheKey(QString::fromLatin1(IconKey::NoExtension), true),
             QStringLiteral("d|<dir>"));
    QCOMPARE(IconKey::cacheKey(QString::fromLatin1(IconKey::NoExtension), false),
             QStringLiteral("f|?"));
}

void TstPlatformIcon::cacheKeyOfDirectoryWithExtensionKeepsIt()
{
    // 有扩展名的目录（macOS 的 `Foo.app`）要按扩展名解析，而不是走目录哨兵——
    // 否则所有 .app 会退化成通用的文件夹图标。
    QCOMPARE(IconKey::cacheKey(QStringLiteral("app"), true), QStringLiteral("d|app"));
}

void TstPlatformIcon::parseCacheKeyRoundTrips()
{
    const QStringList keys{QStringLiteral("f|txt"), QStringLiteral("d|txt"),
                           QStringLiteral("f|?"), QStringLiteral("d|<dir>"),
                           QStringLiteral("f|tar.gz")};

    for (const QString &key : keys) {
        QString extension;
        bool isDirectory = false;
        QVERIFY2(IconKey::parseCacheKey(key, &extension, &isDirectory),
                 qPrintable(key));
        QCOMPARE(IconKey::cacheKey(extension, isDirectory), key);
    }
}

void TstPlatformIcon::parseCacheKeyRejectsMalformedInput()
{
    // 非法输入必须明确返回 false，而不是解出一个半截的键——
    // 半截的键会拿去问系统，得到一个看似成功但毫无意义的图标。
    const QStringList bad{QString(), QStringLiteral("x"), QStringLiteral("f"),
                          QStringLiteral("f|"), QStringLiteral("?|a"),
                          QStringLiteral("F|a"), QStringLiteral("ffa")};
    for (const QString &key : bad)
        QVERIFY2(!IconKey::parseCacheKey(key, nullptr, nullptr), qPrintable(key));
}

// -----------------------------------------------------------------------------
// 2. 尺寸（完成标准第 5 条）
// -----------------------------------------------------------------------------

void TstPlatformIcon::iconPixelSizeScalesWithDeviceRatio()
{
    QCOMPARE(iconPixelSize(16, 1.0), 16);
    QCOMPARE(iconPixelSize(16, 2.0), 32);
    QCOMPARE(iconPixelSize(16, 1.5), 24);
    QCOMPARE(iconPixelSize(32, 2.0), 64);
}

void TstPlatformIcon::iconPixelSizeTreatsBogusRatioAsOne()
{
    // 非法缩放比按 1.0 处理，而不是原样相乘：乘出来是 0 或负数，
    // 界面上表现为「图标不见了」，而原因是一个没初始化的变量——
    // 这类 bug 的排查成本远高于在这里挡一下。
    QCOMPARE(iconPixelSize(16, 0.0), 16);
    QCOMPARE(iconPixelSize(16, -2.0), 16);
    QCOMPARE(iconPixelSize(16, qQNaN()), 16);
}

void TstPlatformIcon::iconPixelSizeIsZeroWhenNothingRequested()
{
    // 调用方没要图（0 或负值）时不要自作主张给一个默认尺寸：
    // 那会让「我不想显示图标」变成一个说不清的默认行为。
    QCOMPARE(iconPixelSize(0, 1.0), 0);
    QCOMPARE(iconPixelSize(-8, 2.0), 0);
    // 而合法请求的结果下限是 1：0 像素画不出东西，观感等同于「图标缺失」。
    QCOMPARE(iconPixelSize(1, 0.001), 1);
}

void TstPlatformIcon::iconPixelSizeClampsToUpperBound()
{
    // 没有上限的话，一个错传的缩放比会让我们对目录里每一行
    // 都从系统取一张巨大的位图。
    QCOMPARE(iconPixelSize(16, 1000.0), 1024);
    QCOMPARE(iconPixelSize(512, 2.0), 1024);
}

void TstPlatformIcon::windowsShellSizeSnapsToAvailableSizes()
{
    using namespace Win32IconSize;

    QCOMPARE(nearest(8), Small);
    QCOMPARE(nearest(16), Small);
    QCOMPARE(nearest(20), Large);
    QCOMPARE(nearest(32), Large);
    QCOMPARE(nearest(40), ExtraLarge);
    QCOMPARE(nearest(48), ExtraLarge);

    // 48 到 256 之间是大跳。宁可取 256 再缩，也不取 48 再放大——
    // 放大是插值出来的模糊，缩小是清晰的。
    QCOMPARE(nearest(64), Jumbo);
    QCOMPARE(nearest(96), Jumbo);
    QCOMPARE(nearest(192), Jumbo);
    QCOMPARE(nearest(256), Jumbo);

    // Shell 没有比 Jumbo 更大的档，请求再大也只能给 256。
    QCOMPARE(nearest(4096), Jumbo);
}

// -----------------------------------------------------------------------------
// 3. 缓存（完成标准第 2 条）
// -----------------------------------------------------------------------------

void TstPlatformIcon::cacheReportsHitAndMiss()
{
    IconCache cache(8);

    IconEntry entry;
    QVERIFY(!cache.lookup(QStringLiteral("f|txt"), &entry));
    QCOMPARE(cache.stats().misses, 1);
    QCOMPARE(cache.stats().hits, 0);

    IconEntry stored;
    stored.cacheKey = QStringLiteral("f|txt");
    stored.source = IconSource::System;
    stored.payload = QStringLiteral("x");
    cache.insert(QStringLiteral("f|txt"), stored);

    QVERIFY(cache.lookup(QStringLiteral("f|txt"), &entry));
    QCOMPARE(entry.cacheKey, QStringLiteral("f|txt"));
    QCOMPARE(cache.stats().hits, 1);
    QCOMPARE(cache.stats().size, 1);
}

void TstPlatformIcon::cacheHitRateIsZeroBeforeAnyLookup()
{
    // 一次都没查过时报 0，而不是 1.0，也不是 NaN。界面上显示
    // 「命中率 100%」而其实一次都没查过，是最坏的一种正确。
    const IconCache cache(4);
    QCOMPARE(cache.stats().hitRate(), 0.0);
}

void TstPlatformIcon::cacheEvictsLeastRecentlyUsed()
{
    IconCache cache(2);
    IconEntry entry;

    for (const QString &key : {QStringLiteral("f|a"), QStringLiteral("f|b")})
        cache.insert(key, entry);
    QCOMPARE(cache.keysByRecency(),
             (QStringList{QStringLiteral("f|a"), QStringLiteral("f|b")}));

    cache.insert(QStringLiteral("f|c"), entry);

    QCOMPARE(cache.stats().size, 2);
    QCOMPARE(cache.stats().evictions, 1);
    QVERIFY(!cache.contains(QStringLiteral("f|a")));
    QVERIFY(cache.contains(QStringLiteral("f|b")));
    QVERIFY(cache.contains(QStringLiteral("f|c")));
}

void TstPlatformIcon::cacheLookupRefreshesRecency()
{
    IconCache cache(2);
    IconEntry entry;

    cache.insert(QStringLiteral("f|a"), entry);
    cache.insert(QStringLiteral("f|b"), entry);

    // 查一次 a，它就成了「最近使用」的；下一个被淘汰的应是 b。
    QVERIFY(cache.lookup(QStringLiteral("f|a"), &entry));
    cache.insert(QStringLiteral("f|c"), entry);

    QVERIFY(cache.contains(QStringLiteral("f|a")));
    QVERIFY(!cache.contains(QStringLiteral("f|b")));
}

void TstPlatformIcon::cacheShrinkingCapacityEvictsImmediately()
{
    IconCache cache(4);
    IconEntry entry;
    for (const QString &key : {QStringLiteral("f|a"), QStringLiteral("f|b"),
                               QStringLiteral("f|c"), QStringLiteral("f|d")})
        cache.insert(key, entry);

    cache.setCapacity(2);

    // 立刻淘汰到只剩 2 个，而不是等下次插入才突然掉一批——
    // 否则「设置里把缓存调小」看起来毫无效果。
    QCOMPARE(cache.stats().size, 2);
    QCOMPARE(cache.stats().capacity, 2);
    QVERIFY(cache.contains(QStringLiteral("f|c")));
    QVERIFY(cache.contains(QStringLiteral("f|d")));
}

void TstPlatformIcon::cacheCapacityHasFloorOfOne()
{
    IconCache cache(0);
    QCOMPARE(cache.capacity(), 1);

    IconEntry entry;
    cache.insert(QStringLiteral("f|a"), entry);
    QCOMPARE(cache.stats().size, 1);
}

void TstPlatformIcon::cacheContainsDoesNotCountAsHit()
{
    // contains() 只用于诊断。把它算成命中会让「命中率」这个指标失去意义——
    // 而 PLAT-004 恰恰把命中率写进了完成标准，它是一个要能拿来做判断的数字。
    IconCache cache(4);
    IconEntry entry;
    cache.insert(QStringLiteral("f|a"), entry);

    QVERIFY(cache.contains(QStringLiteral("f|a")));
    QCOMPARE(cache.stats().hits, 0);
    QCOMPARE(cache.stats().misses, 0);
}

void TstPlatformIcon::cacheClearKeepsStatistics()
{
    IconCache cache(4);
    IconEntry entry;
    cache.lookup(QStringLiteral("f|missing"), &entry);
    cache.insert(QStringLiteral("f|a"), entry);

    cache.clear();

    QCOMPARE(cache.stats().size, 0);
    // 统计刻意不清零：清空缓存通常发生在「用户改了缩放比例」这类场景，
    // 而那正是最需要看到「命中率刚刚掉下去了」的时刻。
    QCOMPARE(cache.stats().misses, 1);
}

void TstPlatformIcon::cacheRefusesNothingOnInsertOfSameKey()
{
    // 同一个键重复插入是正常流程（重试、重新探测），不该被当成淘汰。
    IconCache cache(2);
    IconEntry entry;
    cache.insert(QStringLiteral("f|a"), entry);
    cache.insert(QStringLiteral("f|a"), entry);

    QCOMPARE(cache.stats().size, 1);
    QCOMPARE(cache.stats().evictions, 0);
}

// -----------------------------------------------------------------------------
// 4. 请求队列（完成标准第 3 条）
// -----------------------------------------------------------------------------

void TstPlatformIcon::queueDeduplicatesSameKey()
{
    // 这一条是 PLAT-004 第 3 条的全部要点：首次浏览一个 10 万文件的目录时，
    // 缓存里一个条目都没有，去重是唯一能把在途请求数压到「扩展名数」的办法。
    IconRequestQueue queue;

    int accepted = 0;
    for (int i = 0; i < 10000; ++i) {
        if (queue.enqueue(QStringLiteral("f|txt")))
            ++accepted;
        if (queue.enqueue(QStringLiteral("f|png")))
            ++accepted;
        if (queue.enqueue(QStringLiteral("d|<dir>")))
            ++accepted;
    }

    QCOMPARE(accepted, 3);
    QCOMPARE(queue.pendingCount(), 3);
    QCOMPARE(queue.readyCount(), 3);
}

void TstPlatformIcon::queueKeepsKeysPendingUntilFinished()
{
    IconRequestQueue queue;
    queue.enqueue(QStringLiteral("f|txt"));

    QVERIFY(queue.isPending(QStringLiteral("f|txt")));
    QVERIFY(!queue.isPending(QStringLiteral("f|png")));

    queue.finish(QStringLiteral("f|txt"));
    QVERIFY(!queue.isPending(QStringLiteral("f|txt")));
    QCOMPARE(queue.pendingCount(), 0);
}

void TstPlatformIcon::takeReadyDoesNotReleasePending()
{
    IconRequestQueue queue;
    queue.enqueue(QStringLiteral("f|a"));
    queue.enqueue(QStringLiteral("f|b"));

    const QStringList taken = queue.takeReady(1);
    QCOMPARE(taken, QStringList{QStringLiteral("f|a")});

    // 取走 ≠ 结束。取走后仍然算在途，于是解析完成之前到来的同类请求
    // 不会又排一次队——否则「去重」在「已经发给后台」这段时间里是失效的。
    QVERIFY(queue.isPending(QStringLiteral("f|a")));
    QCOMPARE(queue.pendingCount(), 2);
    QCOMPARE(queue.readyCount(), 1);

    // 已在途的键再次入队必须被拒。
    QVERIFY(!queue.enqueue(QStringLiteral("f|a")));

    QCOMPARE(queue.takeReady(10), QStringList{QStringLiteral("f|b")});
    QCOMPARE(queue.readyCount(), 0);
}

void TstPlatformIcon::finishAllowsSameKeyToBeEnqueuedAgain()
{
    // 解析失败（无桌面环境、Shell 被策略拦下）之后必须能重来。
    // 若 finish() 不清在途集合，那个扩展名会**永远**不再被重试，
    // 用户切回有桌面的环境也拿不到图标——而且不会有任何报错。
    IconRequestQueue queue;
    QVERIFY(queue.enqueue(QStringLiteral("f|txt")));
    queue.takeReady(1);
    queue.finish(QStringLiteral("f|txt"));

    QVERIFY(queue.enqueue(QStringLiteral("f|txt")));
    QCOMPARE(queue.pendingCount(), 1);
}

void TstPlatformIcon::queueDropsEmptyKeys()
{
    // 空键既不能入队也不能被 finish：让空键进队列会让后续的
    // isPending("") 一直为真，于是「所有算不出键的条目」互相挡住。
    IconRequestQueue queue;
    QVERIFY(!queue.enqueue(QString()));
    QCOMPARE(queue.pendingCount(), 0);
}

void TstPlatformIcon::takeReadyWithZeroCountReturnsNothing()
{
    IconRequestQueue queue;
    queue.enqueue(QStringLiteral("f|a"));

    QCOMPARE(queue.takeReady(0), QStringList());
    QCOMPARE(queue.takeReady(-3), QStringList());
    QCOMPARE(queue.readyCount(), 1); // 没被取走，仍等着
}

// -----------------------------------------------------------------------------
// 5. 服务（完成标准第 2~4 条）
// -----------------------------------------------------------------------------

void TstPlatformIcon::serviceFallsBackToBuiltinWhenProviderHasNothing()
{
    auto *provider = new RecordingIconProvider;
    provider->setFailEverything(true);
    IconService service(provider);

    const IconEntry entry = service.iconForPath(QStringLiteral("/a/b.txt"), false);

    QCOMPARE(entry.source, IconSource::Builtin);
    QCOMPARE(entry.builtin, BuiltinIcon::File);
    QVERIFY(!entry.usable()); // 没有系统图标，界面据此去拿内置资源
    QVERIFY(entry.hasFallback());
}

void TstPlatformIcon::serviceFallsBackToFolderKindForDirectories()
{
    auto *provider = new RecordingIconProvider;
    provider->setFailEverything(true);
    IconService service(provider);

    QCOMPARE(service.iconForPath(QStringLiteral("/a/b"), true).builtin, BuiltinIcon::Folder);
    QCOMPARE(service.iconForPath(QStringLiteral("/a/b.txt"), false).builtin, BuiltinIcon::File);
}

void TstPlatformIcon::serviceCachesByExtensionNotByFile()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);

    // 100 个文件、3 种扩展名：系统只该被问 3 次。
    for (int i = 0; i < 100; ++i) {
        service.iconForPath(QStringLiteral("/dir%1/file%2.txt").arg(i % 7).arg(i), false);
        service.iconForPath(QStringLiteral("/dir%1/file%2.png").arg(i % 7).arg(i), false);
        service.iconForPath(QStringLiteral("/dir%1/file%2").arg(i % 7).arg(i), true);
    }

    QCOMPARE(provider->callCount(), 3);
    QCOMPARE(service.cacheStats().misses, 3);
    QCOMPARE(service.cacheStats().hits, 297);

    // 命中率高是完成标准里明写的一条，因此这里直接断言它。
    QVERIFY(service.cacheStats().hitRate() > 0.98);
}

void TstPlatformIcon::serviceDoesNotLetDirectoryPoisonFileKey()
{
    auto *provider = new RecordingIconProvider;
    provider->setFailDirectories(true);   // 让目录解析失败，走内置回退
    IconService service(provider);

    // 先问一个叫 notes.txt 的**目录**：它拿不到系统图标，回退成文件夹图标。
    const IconEntry directory = service.iconForPath(QStringLiteral("/w/notes.txt"), true);
    QCOMPARE(directory.source, IconSource::Builtin);
    QCOMPARE(directory.builtin, BuiltinIcon::Folder);

    // 再问一个叫 a.txt 的**文件**：必须拿到系统图标。
    // 若缓存键没把「文件/目录」分开，这里会命中上面那条回退记录，
    // 于是所有 .txt 文件都显示成文件夹图标——而且会一直留着。
    const IconEntry file = service.iconForPath(QStringLiteral("/w/a.txt"), false);
    QCOMPARE(file.source, IconSource::System);
    QVERIFY(file.usable());

    QCOMPARE(provider->callCount(), 2);
    QVERIFY(provider->calls().contains(QStringLiteral("d:txt@16")));
    QVERIFY(provider->calls().contains(QStringLiteral("f:txt@16")));
}

void TstPlatformIcon::servicePassesScaledPixelSizeToProvider()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);

    service.setBaseSize(16);
    service.setDevicePixelRatio(2.0);
    QCOMPARE(service.pixelSize(), 32);

    service.iconForPath(QStringLiteral("/a/b.txt"), false);

    // 传给系统的必须是**缩放后**的尺寸。传 16 再自己放大，
    // 在高分屏上就是模糊的——而「有点糊」很容易被归咎于「这软件不精细」。
    QCOMPARE(provider->calls(), QStringList{QStringLiteral("f:txt@32")});
}

void TstPlatformIcon::serviceChangingRatioClearsCache()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);

    service.setBaseSize(16);
    QVERIFY(service.iconForPath(QStringLiteral("/a/b.txt"), false).usable());
    QCOMPARE(service.cacheStats().size, 1);

    service.setDevicePixelRatio(2.0);

    // 缓存里的位图是按旧 DPI 取的，留着就是「在 200% 屏上显示 16 像素的图」。
    QCOMPARE(service.cacheStats().size, 0);

    QVERIFY(service.iconForPath(QStringLiteral("/a/b.txt"), false).usable());
    QCOMPARE(provider->callCount(), 2);
    QCOMPARE(provider->calls().last(), QStringLiteral("f:txt@32"));
}

void TstPlatformIcon::serviceChangingBaseSizeClearsCache()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);

    QVERIFY(service.iconForPath(QStringLiteral("/a/b.txt"), false).usable());
    QCOMPARE(service.cacheStats().size, 1);

    service.setBaseSize(32);
    QCOMPARE(service.cacheStats().size, 0);

    // 设成同一个值不该白清一次缓存。
    service.setBaseSize(32);
    QVERIFY(service.iconForPath(QStringLiteral("/a/b.txt"), false).usable());
    service.setBaseSize(32);
    QCOMPARE(service.cacheStats().size, 1);
}

void TstPlatformIcon::serviceRequestEmitsSynchronouslyOnCacheHit()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);
    QSignalSpy spy(&service, &IconService::iconReady);

    // 先同步解析一次，把缓存填上。
    QVERIFY(service.iconForPath(QStringLiteral("/a/b.txt"), false).usable());
    QCOMPARE(spy.count(), 0);
    const int callsBefore = provider->callCount();

    service.requestIcon(QStringLiteral("/a/b.txt"), false);

    // 命中同步发：命中率高的目录里绝大多数条目会同步拿到图标，
    // 若一律异步，一次滚动会插入成百上千个事件轮次，列表会出现可见的闪烁。
    QCOMPARE(spy.count(), 1);
    QCOMPARE(service.pendingRequestCount(), 0);
    QCOMPARE(provider->callCount(), callsBefore);
}

void TstPlatformIcon::serviceRequestDeduplicatesInFlightWork()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);
    QSignalSpy spy(&service, &IconService::iconReady);

    // 300 个请求，3 种扩展名。全部在同一个事件循环轮次内发出，
    // 因此解析结果来不及回来——这正是「首次浏览大目录」的现场。
    for (int i = 0; i < 100; ++i) {
        service.requestIcon(QStringLiteral("/f%1.txt").arg(i), false);
        service.requestIcon(QStringLiteral("/f%1.png").arg(i), false);
        service.requestIcon(QStringLiteral("/d%1").arg(i), true);
    }

    QVERIFY(service.waitForPending());

    // 系统被问 3 次，信号发 3 次——而不是各 300 次。
    // 按路径逐个发信号等于把刚从「300 次系统调用」省下的工作量
    // 原样搬到信号系统上。
    QCOMPARE(provider->callCount(), 3);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(service.pendingRequestCount(), 0);

    QStringList emittedKeys;
    for (const QList<QVariant> &arguments : spy)
        emittedKeys.append(arguments.at(0).toString());
    emittedKeys.sort();
    QCOMPARE(emittedKeys, (QStringList{QStringLiteral("d|<dir>"), QStringLiteral("f|png"),
                                       QStringLiteral("f|txt")}));

    // 解析完成后缓存已填上，再请求就是命中。
    QCOMPARE(service.cacheStats().hits, 0);
    service.requestIcon(QStringLiteral("/f99.txt"), false);
    QCOMPARE(service.cacheStats().hits, 1);
}

void TstPlatformIcon::serviceDestructsWithPendingWork()
{
    // 后台还排着任务时就销毁服务。这条用例的价值在于：它会在
    // AddressSanitizer / 调试运行下暴露「任务持有 this」这类问题——
    // 析构必须等自己的任务结束，因此用的是专属线程池而不是全局池。
    auto *service = new IconService(new RecordingIconProvider);
    for (int i = 0; i < 50; ++i)
        service->requestIcon(QStringLiteral("/f%1.ext%2").arg(i).arg(i % 7), false);

    delete service; // 刻意不等 waitForPending()
    QVERIFY(true);
}

void TstPlatformIcon::serviceUsesBuiltinIconForTypedEntry()
{
    auto *provider = new RecordingIconProvider;
    IconService service(provider);

    const IconEntry entry = service.iconForPath(QStringLiteral("C:\\dir\\report.DOCX"),
                                                false);
    // 键折叠成小写，并且带上了「文件」这个维度。
    QCOMPARE(entry.cacheKey, QStringLiteral("f|docx"));
    QCOMPARE(provider->calls(), QStringList{QStringLiteral("f:docx@16")});
}

// -----------------------------------------------------------------------------
// 6. 真实系统图标源
// -----------------------------------------------------------------------------

void TstPlatformIcon::realProviderIsCreatedAndNamed()
{
    // 工厂的契约与 createNativeFileSystem() 一致：永不返回 nullptr。
    // 判空代码一旦有一处漏掉，表现就是启动即崩，且只在那个平台上崩。
    std::unique_ptr<IconProvider> provider(createNativeIconProvider());
    QVERIFY(provider != nullptr);
    QVERIFY(!provider->platformName().isEmpty());
    QVERIFY(provider->platformName() != QStringLiteral("unsupported"));
}

void TstPlatformIcon::realProviderResolvesTextAndFolderDifferently()
{
    std::unique_ptr<IconProvider> provider(createNativeIconProvider());

    const IconEntry text = provider->resolve(QStringLiteral("txt"), false, 32);
    if (!text.usable())
        QSKIP("本机没有可用的系统图标源（无桌面会话或图标主题缺失）");

    QCOMPARE(text.source, IconSource::System);
    QVERIFY(text.actualPixelSize > 0);
    QVERIFY(text.payload.canConvert<QIcon>());
    QVERIFY(!qvariant_cast<QIcon>(text.payload).isNull());

    const IconEntry folder =
            provider->resolve(QString::fromLatin1(IconKey::NoExtension), true, 32);
    QVERIFY(folder.usable());

    // 文字文件与文件夹必须拿到**不同**的图。
    // 只断言「两张图都不为空」是测不出问题的：一个「无论问什么都返回同一张
    // 通用图标」的实现同样满足，而那会让整个列表失去可辨识性。
    const QImage textImage = qvariant_cast<QIcon>(text.payload).pixmap(32, 32).toImage();
    const QImage folderImage = qvariant_cast<QIcon>(folder.payload).pixmap(32, 32).toImage();
    QVERIFY(!textImage.isNull());
    QVERIFY(!folderImage.isNull());
    QVERIFY(textImage != folderImage);
}

void TstPlatformIcon::serviceWithRealProviderAlwaysProducesSomething()
{
    IconService service(createNativeIconProvider());

    // 不存在的扩展名 + 不存在的路径。无论本机有没有图标源，
    // 都必须给出 System 或 Builtin，**不能**是 Missing——
    // 「缺失」漏到界面上就是一个空图标位。
    const IconEntry entry =
            service.iconForPath(QStringLiteral("/definitely/not/here.zzzznope"), false);

    QVERIFY(entry.source == IconSource::System || entry.source == IconSource::Builtin);
    QVERIFY(entry.source != IconSource::Missing);
    QCOMPARE(entry.cacheKey, QStringLiteral("f|zzzznope"));

    if (entry.source == IconSource::System) {
        // 拿到了系统图标（macOS 对未知扩展名给通用文档图标，
        // 与 Finder 的行为一致），那就必须是可用的。
        QVERIFY(entry.usable());
        QVERIFY(entry.actualPixelSize > 0);
    } else {
        // 回退到内置图标时必须说清是「文件」还是「目录」，
        // 否则视图层不知道该画哪一张。
        QVERIFY(entry.hasFallback());
        QCOMPARE(entry.builtin, BuiltinIcon::File);
    }
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "tst_platformicon.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstPlatformIcon)
