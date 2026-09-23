#ifndef LQCOMPARE_VCSAVAILABILITY_H
#define LQCOMPARE_VCSAVAILABILITY_H

#include "vcsbackend.h"

#include <QHash>
#include <QMutex>
#include <QSharedPointer>
#include <QString>

///
/// VCS-001 第 3、4 条的两件事住在同一个头文件里，因为它们是同一个问题的两面：
/// 「这台机器上现在能不能做版本控制查询」（可用性）与「同一个路径要不要再问一遍」
/// （探测缓存）。分开放会让界面各引一处，而两处的默认值迟早会不一致。
///
namespace LqCompare {
namespace Vcs {

// ── 一、VCS 相关命令的可用性（VCS-001 第 3 条） ────────────────────────────
//
// 原文是「无 git 可执行文件时功能优雅降级（VCS 相关命令置灰并说明『未检测到 git』）」。
// 这句话里有两件事必须**各只有一份实现**，否则降级会静默半途而废：
//
//   1. **哪些命令算「VCS 相关」**。写死在界面里的话，将来新加一条版本控制命令时
//      置灰清单会漏掉它——而漏掉的后果是「一个点下去必然报错的按钮」，正是这一条
//      要防的东西。因此识别规则也放在服务层。
//   2. **置灰时给用户看的那句话**。界面、日志、将来的设置页各写一句必然分叉，
//      最后表现为同一个原因在三处措辞不同，用户以为是三个不同的故障。
//
// 界面只负责把结论喂给 `CommandRegistry`，不参与判定。

/// 判断一条命令是否属于版本控制。
///
/// 按**规格条目号**判（`actionId` 以 "VCS-" 开头），不按命令 ID 判（"vcs." 开头）。
/// 理由：命令 ID 是给程序认的、发布后不可改；规格条目号才是「这条命令属于哪个功能域」
/// 的声明。一条版本控制命令完全可以取 `file.revert` 这样的 ID，按 ID 前缀判会漏掉它，
/// 而漏掉的方式是**静默的**（那条命令照样可点，只是没有降级保护）。
///
/// 参数是字符串而不是 `Command`：服务层不认识命令注册中心的类型，也不该认识
/// （`Services/` 里各模块互不依赖是本仓的既有纪律）。
bool isVcsActionId(const QString &actionId);

/// 一次可用性探测的结论，可以直接喂给 `CommandRegistry::setEnabled()`。
struct CommandAvailability
{
    bool available = true;
    /// 不可用时给用户看的一句话；可用时为空。
    /// 约定：非空就一定是「能直接进 tooltip 的人话」，调用方不必再加工。
    QString reason;
};

/// 把后端可用性的原因翻成命令层能直接用的那句话。
///
/// **不直接透传 `error.message`**：后端那句话的落点是「版本控制设置」页
/// （`GitBackend::availability()` 原文就是「请在版本控制设置中配置 Git 路径」），
/// 而这条结论会出现在每一条被置灰的命令的 tooltip 上——那里的主语必须是命令本身，
/// 不该把用户支到一个还没有的页面上去。两者同源（都从 `error.code` 推），
/// 所以「没有 git」这个判定仍然只有一份。
QString unavailableReason(const Error &error);

/// 探测后端可用性并翻成命令层的结论。
///
/// 传 `nullptr` 按**不可用**处理，不按可用处理：宁可不给入口，也不要给一个点下去
/// 必然报错的入口。反过来（默认可用）会让主界面在探测失败时露出一批假按钮，
/// 而「假按钮」比「置灰的按钮」糟得多——用户会以为是功能坏了。
CommandAvailability probeAvailability(const Backend *backend);

// ── 二、仓库探测结果的路径缓存（VCS-001 第 4 条） ──────────────────────────
//
// 「仓库探测结果按路径缓存，路径变化时失效」。
//
// 为什么值得一个类而不是在视图里存一个 `QString + Repository`：探测是要**起进程**的
// （`git rev-parse --show-toplevel`）。`VcsView` 每切一次模式、每按一次刷新都会重新探测，
// 而同一个路径在一次会话里被问十几遍是常态——「与 HEAD 比对」打开一个目录再切到
// 「与索引比对」，问的是同一个仓库。
//
// 「路径变化时失效」在这份实现里落成三条，**少一条就会给出错答案**：
//
//   1. 键是**归一化后的发起路径**。换一个路径查询绝不会命中上一条路径的结论。
//      最容易写出来的错法是单槽缓存（只记「上一次问的路径」与答案）：它看起来有缓存，
//      实际行为是「换路径就给错答案」，而且错得完全没有声音——用户看到的是一份
//      属于另一个目录的仓库信息。用例 `repositoryCacheNeverAnswersForADifferentPath` 专门造这种形状。
//   2. 只缓存**结论性**的答案。见 `detect()` 的注释。
//   3. 三个显式出口：`invalidate(path)` / `clear()` / `setBackend()`。
//      换后端必须清空——结论属于某个后端；「换成假后端之后还返回旧答案」会让用例里
//      所有断言都建立在另一个后端的结果上，而它们全部是绿的。
class RepositoryCache
{
public:
    explicit RepositoryCache(QSharedPointer<Backend> backend = {});
    ~RepositoryCache();

    /// 换后端并清空全部缓存。传空指针表示「没有后端」，此时每次探测都返回
    /// `Unavailable`（与 `GitBackend` 找不到 git 时同一档），而不是返回空仓库——
    /// 空仓库会让调用方以为「探测成功，这里没有仓库」。
    void setBackend(QSharedPointer<Backend> backend);

    /// 探测仓库（命中缓存则不起进程）。
    ///
    /// **只有结论性的答案进缓存**：
    ///   * 成功；
    ///   * `NotRepository`——「这个路径确实不在任何仓库里」是确定的答案，
    ///     而它恰恰是最常被重复问到的（用户在非仓库目录里反复刷新）。
    ///
    /// 其余一律不缓存，因为它们是**这一次的处境**而不是结论：
    /// `Unavailable`（用户可能刚装好 git 或刚配好路径）、`Cancelled`（缓存一个
    /// 被取消的结果会让「重新查询」永远失败）、以及 `Process` / `Timeout` / `Io` /
    /// `TooLarge` 这些瞬时故障。缓存它们等于把「重试」变成一句空话：用户改完环境
    /// 再点一次，拿到的还是上一次的错误，而且看不出这是一条陈旧结论。
    ///
    /// 命中判定与真实探测**不持同一把锁**：真实探测最坏要等一个 git 进程走到超时
    /// （默认 15 秒），持锁跑它会把同线程/别的线程上任何一个 `clear()` 一起卡住。
    /// 代价是同一路径可能被并发探测两次（多做一次功，不会给出错答案）。
    Result<Repository> detect(const QString &path, const std::atomic_bool *cancel = nullptr);

    /// 丢掉某个路径的缓存。该路径的仓库刚被 `git init`、删掉或移走时调用。
    void invalidate(const QString &path);
    void clear();

    /// 现在缓存着多少个路径。诊断用。
    int cachedPathCount() const;
    /// 真的问过后端几次。**「缓存生效」由它回答**，不由「感觉更快了」回答。
    /// 用例断言的是这个数，这样「缓存写对了」与「缓存根本没走」是可区分的。
    int probeCount() const;

private:
    struct Entry
    {
        Repository repository;
        Error error;
    };

    /// 归一化成缓存键；空路径返回空串（调用方据此直接报错，不进缓存）。
    QString keyFor(const QString &path) const;

    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    QSharedPointer<Backend> m_backend;
    /// 每当后端被换掉或缓存被清空就 +1。在锁外探测的那一段用它判断
    /// 「我这次探测的结果是不是已经过期了」——过期就丢掉，不写进缓存。
    quint64 m_generation = 0;
    int m_probes = 0;
};

} // namespace Vcs
} // namespace LqCompare

#endif // LQCOMPARE_VCSAVAILABILITY_H
