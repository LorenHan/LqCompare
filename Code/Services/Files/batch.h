#ifndef LQCOMPARE_BATCH_H
#define LQCOMPARE_BATCH_H

#include "filesystem.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace LqCompare {
namespace Files {

///
/// \brief 批量操作遇到错误时的处置策略（PRD: PLAT-008 完成标准第 4 条）。
///
/// 为什么把「要不要停」做成显式的策略，而不是在循环里写死
/// --------------------------------------------------
/// 两种选择在真实场景里都成立，而且理由完全相反：
///
///   - **跳过并继续**（默认）：一个条目失败不影响其它条目。批量改属性、
///     批量复制、同步一批文件都属于这类——第 7 个文件被占用，没有理由
///     因此不处理第 8 个。PLAT-008 第 4 条要的正是这个。
///   - **遇到错误就停**：条目之间有依赖时，继续做只会制造更多难以理解的
///     失败。典型例子是「先建目录、再往里放文件」这类有序批次：前一步失败，
///     后面每一步都会因为「目录不存在」而失败，用户拿到一份 200 条的失败清单，
///     其中真正的原因只有第 1 条。
///
/// 写死任何一种都会让另一类场景拿到错误的现场，所以这里让它显式。
/// 默认值是 SkipAndContinue：PLAT-008 第 4 条明确要求「长任务中途出现错误时
/// 不中断整体」，而依赖型批次是少数，由调用方主动声明更合适。
///
enum class BatchFailurePolicy {
    SkipAndContinue = 0,  ///< 记录失败、继续做后面的条目（默认）
    StopOnFirstError,     ///< 遇到第一个错误就停下，已完成的进度保留
};

/// 稳定的机器可读标识（"skip-and-continue" / "stop-on-first-error"）。
const char *batchFailurePolicyIdentifier(BatchFailurePolicy policy);

///
/// \brief 一个条目在一次批量操作里的累计结果（PRD: PLAT-008）。
///
struct BatchRecord
{
    QString path;   ///< 条目路径（原样保留调用方给的形式）

    /// 最近一次尝试的错误。成功时 ok() 为真，且不带原始码。
    ///
    /// 类型是 ErrorCode 而不是 FileSystemError：PLAT-008 第 2 条要求失败清单
    /// 「可单独重试」，而用户判断「现在能不能重试成功」靠的正是原始错误码——
    /// 「ERROR_SHARING_VIOLATION」说明关掉占用程序就好了，
    /// 「ERROR_ACCESS_DENIED」说明重试多少次都一样。
    ErrorCode error;

    /// 累计尝试次数。首次执行为 1，每次重试加 1。
    ///
    /// 单独记下来是为了让失败清单能说出「已尝试 3 次仍未成功」——
    /// 用户据此判断是继续等还是换个办法，而「失败了」三个字不带这个信息。
    int attempts = 1;

    bool succeeded() const { return error.ok(); }
};

///
/// \brief 失败清单里的一组同类失败（PRD: PLAT-008 完成标准第 2 条）。
///
/// 为什么要按错误分类分组，而不是平铺一张失败路径列表
/// ------------------------------------------------
/// 因为**同一类错误的处置动作是一个**。若 200 个条目里有 180 个是「文件被
/// 占用」、20 个是「没有权限」，平铺的清单会让用户做 200 次决策；
/// 分组之后他只需要做两次：「关掉占用程序后重试这 180 个」和
/// 「这 20 个要么提权、要么换目录」。
///
/// 分组同时让「重试失败项」这个动作有了明确的范围——只重试可重试的那一组，
/// 而不是把所有失败项一股脑再跑一遍（权限类重试一百次也是同样的结果，
/// 只会让用户以为程序卡住了）。
///
struct FailureGroup
{
    FileSystemError category = FileSystemError::None;

    /// 该分类下的失败条目，顺序与调用方给的顺序一致。
    QVector<BatchRecord> items;

    /// 来自 errorAdvice()，可直接显示。
    QString advice;

    /// 来自 isRetryable()。界面据此决定「重试失败项」是否对这一组可用。
    bool retryable = false;

    int count() const { return items.size(); }
    QStringList paths() const;
};

///
/// \brief 一次批量操作（含重试）的完整报告（PRD: PLAT-008）。
///
struct BatchReport
{
    /// 操作名，用于提示与日志。为空时由界面填一个通用说法。
    QString operationName;

    BatchFailurePolicy policy = BatchFailurePolicy::SkipAndContinue;

    /// 每个条目一条，顺序与调用方给的顺序一致；重试只更新其中的错误与次数，
    /// **不会**把成功的条目删掉。
    QVector<BatchRecord> records;

    /// 是否因 StopOnFirstError 提前结束。
    bool stoppedEarly = false;

    /// 是否包含过至少一次重试。
    bool retried = false;

    int totalCount() const { return records.size(); }
    int succeededCount() const;
    int failedCount() const;

    /// 全部条目都成功。空报告视为成功（没有失败项）——与 TrashReport 的
    /// 判断一致，否则界面在「一批是空的」时会显示一片红。
    bool allSucceeded() const;

    bool anySucceeded() const;

    int totalAttempts() const;

    QStringList succeededPaths() const;
    QStringList failedPaths() const;

    /// 第一个失败条目的完整错误；全成功时返回 ok() 为真的 ErrorCode。
    ErrorCode firstErrorCode() const;

    /// 第一个失败条目的分类；全成功时返回 None。
    FileSystemError firstError() const;

    /// 按错误分类归并的失败清单。无失败时为空。
    ///
    /// 分组的顺序是**该分类第一次出现的顺序**，而不是枚举定义的顺序：
    /// 用户是按路径顺序一条条看的，界面上第一组应当是他最先遇到的那类问题。
    QVector<FailureGroup> failureGroups() const;

    /// 「重试有可能成功」的条目路径（按原顺序）。
    ///
    /// 用于把「重试失败项」按钮的作用范围说清楚：界面上应当提示
    /// 「其中 N 项是文件被占用，关闭占用程序后可重试」，而不是笼统地
    /// 提供一个对全部失败项都生效的重试。
    QStringList retryablePaths() const;

    /// 可以直接显示或贴进日志的失败清单文本；全部成功时返回空串。
    ///
    /// 内容包含：每个分类的原因、处置建议、条目路径，以及**每条的原始系统
    /// 错误码**（PLAT-008 第 5 条）与累计尝试次数。
    QString failureSummary() const;
};

///
/// \brief 批量文件操作的执行器（PRD: PLAT-008 完成标准第 2、3、4 条）。
///
/// 把三条语义冻结在基类里，派生类只写「单个条目怎么做」（performOne）：
///
///   1. **中途失败不中断整体**（第 4 条）。默认策略下，一个条目失败只被记录，
///      循环照常往下走，已完成的部分保持有效。派生类的代码**不在**
///      「要不要停」这条路径上，因此不可能不小心把整批放弃掉。
///   2. **失败清单可单独重试**（第 2 条）。retryFailed() 只对上一次失败的
///      条目再跑一遍；已经成功的条目不会被重复执行。
///   3. **两条出路**（第 3 条）。retryFailed() 是「重试失败项」；
///      直接读取 run() 返回的报告、接受当前进度就是「跳过并继续」。
///      两条路都不需要重新跑一遍整批。
///
/// 为什么不做成一个「接收 std::function 的自由函数」
/// --------------------------------------------
/// 因为长任务的进度必须跨多次调用保留。自由函数不得不在每次重试时把上一次的
/// 报告传回来，于是「哪些条目已经成功」就有了两个事实来源；一旦调用方算错
/// （漏掉一个、把顺序弄乱），已经成功的文件会被再做一遍。对「批量改属性」
/// 也许无所谓，对「批量复制」就是覆盖用户刚确认过的结果。
/// 把进度放在执行器自己身上，这类错误在结构上就不可能发生。
///
class BatchOperation
{
public:
    virtual ~BatchOperation();

    /// 操作名，用于提示与日志。不要返回空串。
    virtual QString operationName() const = 0;

    void setPolicy(BatchFailurePolicy policy) { m_policy = policy; }
    BatchFailurePolicy policy() const { return m_policy; }

    ///
    /// \brief 每个条目处理完后回调：(条目、已完成数、总数)。
    ///
    /// 存在的理由是「长任务」这个前提：一个几万条的批量要跑几十秒到几分钟，
    /// 界面必须能画出进度，否则用户会以为程序死了。
    /// 只读地传一份条目快照，回调里不要试图修改执行器的状态。
    ///
    using ProgressCallback = std::function<void(const BatchRecord &, int, int)>;
    void setProgressCallback(ProgressCallback callback) { m_progress = std::move(callback); }

    /// 执行整批。
    ///
    /// 会**重新开始**一批：上一次的报告被丢弃，所有条目（包括上一次已成功的）
    /// 都会被再做一遍。界面上正常的流程是 run() 一次，之后只用 retryFailed()；
    /// 重新开始整批应当是用户明确选择的动作。
    BatchReport run(const QStringList &paths);

    /// 只重试上一次 run() / retryFailed() 之后仍未成功的条目。
    ///
    /// 没有失败项时**一次调用也不发生**，直接返回当前报告（而不是报错或
    /// 清空报告）——界面上的「重试失败项」在全部成功时本该是灰的，
    /// 万一被点到，什么也不做才是对的。
    ///
    /// 返回的是**整批的当前状态**，不是「只含被重试的那几条」。这一点很要紧：
    /// 若返回只有失败项的报告，界面在重试成功后会把之前成功的部分算成没做，
    /// 用户看到的进度就倒退回去了（第 4 条）。
    BatchReport retryFailed();

    /// 当前批次的报告（含未重试的条目与累计尝试次数）。
    BatchReport lastReport() const { return m_report; }

protected:
    /// 处理单个条目。成功返回 ok() 为真的 ErrorCode。
    ///
    /// 派生类**不应该**在这里判断「失败要不要放弃整批」——那是策略，
    /// 由 run() 统一决定。这个方法只回答「这一个条目成不成」。
    virtual ErrorCode performOne(const QString &path) = 0;

private:
    BatchReport execute(const QStringList &paths);

    BatchFailurePolicy m_policy = BatchFailurePolicy::SkipAndContinue;
    ProgressCallback m_progress;
    BatchReport m_report;
};

///
/// \brief 批量设置文件属性（PRD: PLAT-008 的参考实现）。
///
/// 为什么拿它当参考实现
/// ------------------
/// PLAT-008 给出的四条建议里，「解除只读后重试」这条需要一个批量入口才能闭环：
/// 失败清单说「这 12 个文件是只读的」，用户得能一次把它们的只读位都去掉，
/// 再一键重试。这个类就是那条闭环——它是让「建议」可执行的那一步。
///
/// 语义与 FileSystem::setAttributes 保持一致：只有 attributes 里点名的属性会被
/// 改动，其余保持不变。包含 ReadOnly 表示「设为只读」，不包含表示「解除只读」。
///
class SetAttributesBatch : public BatchOperation
{
public:
    SetAttributesBatch(const FileSystem &fileSystem, FileAttributes attributes);

    QString operationName() const override;

protected:
    ErrorCode performOne(const QString &path) override;

private:
    const FileSystem &m_fileSystem;
    FileAttributes m_attributes;
};

} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_BATCH_H
