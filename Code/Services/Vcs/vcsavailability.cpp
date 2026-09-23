#include "vcsavailability.h"

#include <QDir>
#include <QObject>
#include <QMutexLocker>

namespace LqCompare {
namespace Vcs {

bool isVcsActionId(const QString &actionId)
{
    // 只认前缀 `VCS-`，不认裸的 `VCS`：规格条目号是「域-编号」两段式
    // （`tools/spec/` 的口径），裸 `VCS` 不是一个条目号。把裸前缀也算进来的话，
    // 将来某个域名为 `VCSX` 的条目号会被一起置灰——而那种错误只有在真加了
    // 那条规格之后才会显形。
    return actionId.startsWith(QLatin1String("VCS-"));
}

QString unavailableReason(const Error &error)
{
    if (error.code == ErrorCode::Unavailable) {
        return QObject::tr("未检测到 git：版本控制命令需要本机已安装 git，且能在 PATH 上执行。");
    }
    // 其余错误码照样置灰，但**不要**都说成「没有 git」：用户会去装一个已经装好的 git，
    // 而真正的原因（进程起不来、输出超限、路径非法）一个字都没被说出来。
    const QString detail = error.message.isEmpty() ? error.detail : error.message;
    if (detail.isEmpty())
        return QObject::tr("版本控制后端当前不可用。");
    return QObject::tr("版本控制后端当前不可用：%1").arg(detail);
}

CommandAvailability probeAvailability(const Backend *backend)
{
    if (!backend) {
        // 没有后端与「后端说没有 git」是两件事，但对用户是同一件事：现在用不了。
        // 走同一个 `unavailableReason()`，于是两句文案只有一个出处。
        return {false, unavailableReason(Error{ErrorCode::Unavailable, {}, {}})};
    }
    const Error error = backend->availability();
    if (!error.isError())
        return {true, {}};
    return {false, unavailableReason(error)};
}

RepositoryCache::RepositoryCache(QSharedPointer<Backend> backend)
    : m_backend(std::move(backend))
{
}

RepositoryCache::~RepositoryCache() = default;

void RepositoryCache::setBackend(QSharedPointer<Backend> backend)
{
    QMutexLocker lock(&m_mutex);
    m_backend = std::move(backend);
    m_entries.clear();
    // 递增代次：一个在锁外跑到一半的探测属于旧后端，它的结论不得写进新后端的缓存。
    ++m_generation;
}

QString RepositoryCache::keyFor(const QString &path) const
{
    if (path.isEmpty())
        return {};
    // 相对路径先按当前工作目录绝对化：同一条相对路径只有在同一个工作目录下才指同一处，
    // 而缓存跨调用存在，工作目录却可能已经变了（尤其是被别的模块 chdir 过的宿主进程）。
    const QString absolute = QDir::isAbsolutePath(path) ? path : QDir::current().absoluteFilePath(path);
    return QDir::cleanPath(absolute);
}

Result<Repository> RepositoryCache::detect(const QString &path, const std::atomic_bool *cancel)
{
    const QString key = keyFor(path);
    if (key.isEmpty()) {
        // 空路径不是「没缓存」而是「问错了」，直接报错且不进缓存：
        // 把它缓存成一条 NotRepository 会让「空路径」这个调用方的 bug 从此隐身。
        return {{}, {ErrorCode::InvalidPath, QObject::tr("未指定路径，无法探测 Git 仓库。"), {}}};
    }

    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_entries.constFind(key);
        if (it != m_entries.constEnd()) {
            // 命中也要把错误码一起还回去：负结论（NotRepository）同样是缓存内容。
            if (it->error.isError())
                return {{}, it->error};
            return {it->repository, {}};
        }
    }

    // 锁外探测：最长可以走到 git 进程超时（默认 15 秒）。持锁跑它会把任何一个
    // 同线程或别的线程上的 clear()/invalidate() 一起卡住，而那两个入口的调用方
    // 大概率是界面。
    quint64 generation = 0;
    QSharedPointer<Backend> backend;
    {
        QMutexLocker lock(&m_mutex);
        generation = m_generation;
        backend = m_backend;
    }

    Result<Repository> probed;
    if (!backend) {
        // 没有后端时**不计数**：`probeCount()` 的语义是「真的问过后端几次」，
        // 把它算进去会让「换成空后端之后缓存不再被使用」这类断言失去意义。
        probed.error = Error{ErrorCode::Unavailable, {}, {}};
    } else {
        probed = backend->detectRepo(key, cancel);
        QMutexLocker lock(&m_mutex);
        ++m_probes;
    }

    const bool conclusive = probed.ok() || probed.error.code == ErrorCode::NotRepository;
    if (conclusive) {
        QMutexLocker lock(&m_mutex);
        // 代次对不上 = 这次探测期间后端被换掉或缓存被清空过，结论属于另一个世界。
        if (generation == m_generation)
            m_entries.insert(key, Entry{probed.value, probed.error});
    }
    return probed;
}

void RepositoryCache::invalidate(const QString &path)
{
    const QString key = keyFor(path);
    if (key.isEmpty())
        return;
    QMutexLocker lock(&m_mutex);
    m_entries.remove(key);
}

void RepositoryCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    ++m_generation;
}

int RepositoryCache::cachedPathCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

int RepositoryCache::probeCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_probes;
}

} // namespace Vcs
} // namespace LqCompare
