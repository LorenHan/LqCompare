#include "tst_batch.h"

#include "batch.h"
#include "fakefilesystem.h"
#include "filesystem.h"

#include <QFile>
#include <QHash>
#include <QTemporaryDir>

#include <cerrno>
#include <memory>

using namespace LqCompare::Files;

// -----------------------------------------------------------------------------
// 失败信息可读化
// -----------------------------------------------------------------------------

namespace QTest {

template <>
char *toString(const FileSystemError &error)
{
    return qstrdup(errorIdentifier(error));
}

/// ErrorCode 的显示：分类 + 原始系统码。
/// 本套件的第一组断言正是「同一分类、不同原始码」，失败时看不到原始码就等于
/// 看不到被断言的差异。
template <>
char *toString(const ErrorCode &error)
{
    const QString text = errorReport(error);
    return qstrdup(qPrintable(text.isEmpty() ? QStringLiteral("none") : text));
}

template <>
char *toString(const ErrorDomain &domain)
{
    return qstrdup(errorDomainIdentifier(domain));
}

template <>
char *toString(const BatchFailurePolicy &policy)
{
    return qstrdup(batchFailurePolicyIdentifier(policy));
}

} // namespace QTest

namespace {

/// 造一条报告用的记录。
BatchRecord makeRecord(const QString &path, const ErrorCode &error, int attempts = 1)
{
    BatchRecord record;
    record.path = path;
    record.error = error;
    record.attempts = attempts;
    return record;
}

///
/// \brief 只用来观察流程的批处理执行器。
///
/// 它不做任何真实文件操作，因此可以把「跳到第几个条目失败」这种情形精确地
/// 摆出来——真实文件系统上做不到这一点（同一个注入点在不同平台上表现不同）。
/// 它同时记录调用日志，测试据此断言「哪些条目被碰过、哪些没有」，
/// 而这是 PLAT-008 第 2/4 条的核心：重试不能碰已成功的条目，
/// 停止后不能碰后面的条目。
///
class ProbeBatchOperation : public BatchOperation
{
public:
    QString operationName() const override { return QStringLiteral("探针操作"); }

    void failAlways(const QString &path, const ErrorCode &error)
    {
        m_failures.insert(path, error);
    }

    void clearFailures() { m_failures.clear(); }

    QStringList callLog() const { return m_callLog; }
    int callCount() const { return m_callLog.size(); }
    int callsFor(const QString &path) const { return m_callLog.count(path); }

protected:
    ErrorCode performOne(const QString &path) override
    {
        m_callLog.append(path);
        // value() 在键不存在时返回默认构造的 ErrorCode，也就是「成功」。
        return m_failures.value(path);
    }

private:
    QHash<QString, ErrorCode> m_failures;
    QStringList m_callLog;
};

/// 读一个文件系统里某个路径是不是只读。
///
/// 参数取 `const FileSystem&` 而不是 `const Test::FakeFileSystem&` 是有原因的：
/// 默认参数只写在基类的声明上（`ErrorCode *error = nullptr`），派生类的重写
/// 声明里没有重复它，因此拿派生类的静态类型调用必须显式传出参。
/// 通过基类引用调用则自动用上基类的默认值——这恰好也让这段代码对
/// 真实实现与替身同样适用。
bool isReadOnly(const FileSystem &fileSystem, const QString &path)
{
    return fileSystem.stat(path).isReadOnly();
}

} // namespace

// -----------------------------------------------------------------------------
// 1. 错误携带（完成标准第 1、5 条）
// -----------------------------------------------------------------------------

void TstBatch::posixErrorKeepsCategoryAndRawCode()
{
    const ErrorCode code = fromSystemError(EACCES);

    QCOMPARE(code.category, FileSystemError::PermissionDenied);
    QCOMPARE(code.domain, ErrorDomain::Posix);
    QCOMPARE(code.raw, qint64(EACCES));
    QVERIFY(!code.ok());
    QVERIFY(code.hasRawCode());
}

void TstBatch::successfulErrorHasNoRawCode()
{
    const ErrorCode code = fromSystemError(0);

    QVERIFY(code.ok());
    QCOMPARE(code.domain, ErrorDomain::None);
    QCOMPARE(code.raw, qint64(0));
    QVERIFY(!code.hasRawCode());
    QVERIFY(errorDetail(code).isEmpty());
}

void TstBatch::windowsErrorKeepsRawCode()
{
    // 「文件被占用」在 Windows 上返回的是 ERROR_SHARING_VIOLATION(32)，
    // 而标准库会把它映射成 EACCES——如果只留下分类，用户拿到的建议会是
    // 「请提权」，一个完全错误的方向。原始码就是用来挡住这种误判的。
    const ErrorCode code = fromWindowsError(Win32Error::SharingViolation);

    QCOMPARE(code.category, FileSystemError::Busy);
    QCOMPARE(code.domain, ErrorDomain::Win32);
    QCOMPARE(code.raw, qint64(Win32Error::SharingViolation));
    QVERIFY(errorDetail(code).contains(QStringLiteral("ERROR_SHARING_VIOLATION")));
}

void TstBatch::cocoaErrorKeepsRawCode()
{
    const ErrorCode code = fromCocoaError(CocoaError::WriteFileExists);

    QCOMPARE(code.category, FileSystemError::AlreadyExists);
    QCOMPARE(code.domain, ErrorDomain::Cocoa);
    QCOMPARE(code.raw, qint64(CocoaError::WriteFileExists));
    QVERIFY(errorDetail(code).contains(QStringLiteral("NSFileWriteFileExistsError")));
}

void TstBatch::unknownCodeStillKeepsItsNumber()
{
    // 分类不上不代表这个码没用。用户拿着 1234 能去搜 Win32 的错误码表，
    // 「未知错误」这四个字搜不出任何东西——所以未知也必须把数字留住。
    constexpr unsigned long kMadeUpCode = 1234;
    const ErrorCode code = fromWindowsError(kMadeUpCode);

    QCOMPARE(code.category, FileSystemError::Unknown);
    QCOMPARE(code.domain, ErrorDomain::Win32);
    QCOMPARE(code.raw, qint64(kMadeUpCode));

    // 但绝不能给它编一个像官方名字的假名字：用户会拿这个符号去搜，然后一无所获。
    QVERIFY(rawErrorName(ErrorDomain::Win32, qint64(kMadeUpCode)) == nullptr);
    QCOMPARE(errorDetail(code), QStringLiteral("Win32 1234"));
}

void TstBatch::errorDetailNamesKnownCodes()
{
    QCOMPARE(errorDetail(fromSystemError(ENOENT)), QStringLiteral("errno 2（ENOENT）"));
    QCOMPARE(errorDetail(fromWindowsError(Win32Error::AccessDenied)),
             QStringLiteral("Win32 5（ERROR_ACCESS_DENIED）"));
    QCOMPARE(errorDetail(fromCocoaError(CocoaError::WriteOutOfSpace)),
             QStringLiteral("NSCocoaErrorDomain 640（NSFileWriteOutOfSpaceError）"));
}

void TstBatch::errorDetailIsEmptyWithoutRawCode()
{
    // 本地产生的分类（不是从系统调用读来的）没有原始码。此时应当返回空串，
    // 由界面决定不显示这一段，而不是显示「原始错误码：（无）」——那是噪声。
    QVERIFY(errorDetail(ErrorCode()).isEmpty());
    QVERIFY(errorDetail(ErrorCode(FileSystemError::Busy)).isEmpty());
}

void TstBatch::errorReportAppendsDetailToMessage()
{
    const QString path = QStringLiteral("/data/report.txt");
    const QString report = errorReport(fromSystemError(EACCES), path);

    QVERIFY(report.contains(path));
    QVERIFY(report.contains(QStringLiteral("EACCES")));
    QVERIFY(report.contains(QStringLiteral("13")));

    // 没有原始码时，errorReport 必须退化成 errorMessage 本身——
    // 否则界面上会出现两种写法的「没有权限」。
    QCOMPARE(errorReport(ErrorCode(FileSystemError::Busy), path),
             errorMessage(FileSystemError::Busy, path));
}

void TstBatch::sameCategoryStaysDistinguishableByRawCode()
{
    // 这是整个 ErrorCode 存在的理由：EPERM(1) 与 EACCES(13) 都归 PermissionDenied，
    // 但前者常常是文件带不可变标志或被安全模块拦截，后者才是 chmod 能解决的。
    // 只报分类会让两者看起来是同一个问题。
    const ErrorCode notPermitted = fromSystemError(EPERM);
    const ErrorCode notAccessible = fromSystemError(EACCES);

    QCOMPARE(notPermitted.category, notAccessible.category);
    QCOMPARE(notPermitted.category, FileSystemError::PermissionDenied);
    QVERIFY(errorDetail(notPermitted) != errorDetail(notAccessible));
    QVERIFY(errorDetail(notPermitted).contains(QStringLiteral("EPERM")));
    QVERIFY(errorDetail(notAccessible).contains(QStringLiteral("EACCES")));
}

void TstBatch::errorCodeConvertsToCategoryForOldCallSites()
{
    // 隐式转换是为了让 $*error == FileSystemError::X$ 这类既有写法继续有效。
    // 这不是语法糖：它意味着这次改造不必把上百处调用点一起改掉，
    // 从而不会在改造过程中顺手引入别的错误。
    const ErrorCode code = fromSystemError(EACCES);
    QVERIFY(code == FileSystemError::PermissionDenied);
    QVERIFY(code != FileSystemError::Busy);

    // 反方向的隐式构造：既有的 `*error = FileSystemError::None` 写法继续可用。
    const ErrorCode plain = FileSystemError::ReadOnly;
    QCOMPARE(plain.category, FileSystemError::ReadOnly);
    QVERIFY(!plain.hasRawCode());
}

// -----------------------------------------------------------------------------
// 2. 失败清单（完成标准第 2 条）
// -----------------------------------------------------------------------------

void TstBatch::emptyReportIsAllSucceeded()
{
    const BatchReport report;

    QVERIFY(report.allSucceeded());
    QCOMPARE(report.totalCount(), 0);
    QCOMPARE(report.failedCount(), 0);
    QVERIFY(!report.anySucceeded()); // 空批次没有任何条目成功过
    QVERIFY(report.failureGroups().isEmpty());
    // 没有失败就不输出清单，而不是输出一句「失败 0 个」。
    QVERIFY(report.failureSummary().isEmpty());
    QCOMPARE(report.firstError(), FileSystemError::None);
    QVERIFY(report.firstErrorCode().ok());
}

void TstBatch::reportSplitsPathsInOrder()
{
    BatchReport report;
    report.records << makeRecord(QStringLiteral("/a.txt"), ErrorCode())
                   << makeRecord(QStringLiteral("/b.txt"), fromSystemError(EBUSY))
                   << makeRecord(QStringLiteral("/c.txt"), ErrorCode())
                   << makeRecord(QStringLiteral("/d.txt"), fromSystemError(EACCES));

    QCOMPARE(report.totalCount(), 4);
    QCOMPARE(report.succeededCount(), 2);
    QCOMPARE(report.failedCount(), 2);
    QVERIFY(!report.allSucceeded());
    QVERIFY(report.anySucceeded());
    QCOMPARE(report.totalAttempts(), 4);

    // 两侧都保持调用方给的顺序：界面上清单的顺序要和用户看到的一致，
    // 否则他数不出「第几个出问题了」。
    QCOMPARE(report.succeededPaths(),
             (QStringList{QStringLiteral("/a.txt"), QStringLiteral("/c.txt")}));
    QCOMPARE(report.failedPaths(),
             (QStringList{QStringLiteral("/b.txt"), QStringLiteral("/d.txt")}));

    QCOMPARE(report.firstError(), FileSystemError::Busy);
    QCOMPARE(report.firstErrorCode().raw, qint64(EBUSY));
}

void TstBatch::failureGroupsMergeByCategory()
{
    BatchReport report;
    report.records << makeRecord(QStringLiteral("/1.txt"), fromSystemError(EBUSY))
                   << makeRecord(QStringLiteral("/2.txt"), fromSystemError(EACCES))
                   << makeRecord(QStringLiteral("/3.txt"), fromSystemError(EBUSY))
                   << makeRecord(QStringLiteral("/4.txt"), fromSystemError(EBUSY))
                   << makeRecord(QStringLiteral("/5.txt"), fromSystemError(EACCES));

    const QVector<FailureGroup> groups = report.failureGroups();

    // 五条失败压成两组，用户只需要做两次决策，而不是五次。
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).category, FileSystemError::Busy);
    QCOMPARE(groups.at(0).count(), 3);
    QCOMPARE(groups.at(1).category, FileSystemError::PermissionDenied);
    QCOMPARE(groups.at(1).count(), 2);

    QCOMPARE(groups.at(0).paths(),
             (QStringList{QStringLiteral("/1.txt"), QStringLiteral("/3.txt"),
                          QStringLiteral("/4.txt")}));
    QCOMPARE(groups.at(1).paths(),
             (QStringList{QStringLiteral("/2.txt"), QStringLiteral("/5.txt")}));
}

void TstBatch::failureGroupsFollowFirstAppearanceOrder()
{
    // 分组的顺序按「该分类第一次出现」而不是枚举定义顺序：
    // 用户是按路径顺序看的，界面上第一组应当是他最先遇到的那类问题。
    BatchReport report;
    report.records << makeRecord(QStringLiteral("/1.txt"), fromSystemError(EACCES))
                   << makeRecord(QStringLiteral("/2.txt"), fromSystemError(EBUSY));

    const QVector<FailureGroup> groups = report.failureGroups();
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).category, FileSystemError::PermissionDenied);
    QCOMPARE(groups.at(1).category, FileSystemError::Busy);
}

void TstBatch::failureGroupsCarryAdviceAndRetryability()
{
    BatchReport report;
    report.records << makeRecord(QStringLiteral("/1.txt"), fromSystemError(EBUSY))
                   << makeRecord(QStringLiteral("/2.txt"), fromSystemError(EACCES))
                   << makeRecord(QStringLiteral("/3.txt"),
                                 ErrorCode(FileSystemError::ReadOnlyFileSystem));

    const QVector<FailureGroup> groups = report.failureGroups();
    QCOMPARE(groups.size(), 3);

    // 四类可处置错误必须给出不同的建议——这正是 PLAT-008 第 1 条。
    QVERIFY(groups.at(0).advice.contains(QStringLiteral("关闭")));
    QVERIFY(groups.at(1).advice.contains(QStringLiteral("管理员")));
    QVERIFY(groups.at(2).advice.contains(QStringLiteral("只读")));
    QVERIFY(groups.at(0).advice != groups.at(1).advice);
    QVERIFY(groups.at(1).advice != groups.at(2).advice);

    // 「能不能重试」跟着分类走：只有「被占用」是等一会儿可能自己好的。
    QVERIFY(groups.at(0).retryable);
    QVERIFY(!groups.at(1).retryable);
    QVERIFY(!groups.at(2).retryable);
}

void TstBatch::retryablePathsOnlyPicksRetryableCategories()
{
    BatchReport report;
    report.records << makeRecord(QStringLiteral("/busy.txt"), fromSystemError(EBUSY))
                   << makeRecord(QStringLiteral("/denied.txt"), fromSystemError(EACCES))
                   << makeRecord(QStringLiteral("/ro.txt"), ErrorCode(FileSystemError::ReadOnly));

    // 把「重试失败项」的范围限定在真正可能成功的那些上：权限类重试一百次
    // 也是同样的结果，只会让用户以为程序卡住了。
    QCOMPARE(report.retryablePaths(), QStringList{QStringLiteral("/busy.txt")});
}

void TstBatch::failureSummaryMentionsPathAdviceAndRawCode()
{
    BatchReport report;
    report.operationName = QStringLiteral("批量复制");
    report.records << makeRecord(QStringLiteral("/src/a.txt"), ErrorCode())
                   << makeRecord(QStringLiteral("/src/b.txt"), fromSystemError(EBUSY), 3)
                   << makeRecord(QStringLiteral("/src/c.txt"), fromSystemError(EACCES));

    const QString summary = report.failureSummary();

    QVERIFY(summary.contains(QStringLiteral("批量复制")));
    QVERIFY(summary.contains(QStringLiteral("完成 1 个")));
    QVERIFY(summary.contains(QStringLiteral("失败 2 个")));
    QVERIFY(summary.contains(QStringLiteral("/src/b.txt")));
    QVERIFY(summary.contains(QStringLiteral("/src/c.txt")));
    // 完成的条目不该出现在失败清单里。
    QVERIFY(!summary.contains(QStringLiteral("/src/a.txt")));

    // 第 5 条：每一条都带原始系统码。
    QVERIFY(summary.contains(QStringLiteral("EBUSY")));
    QVERIFY(summary.contains(QStringLiteral("EACCES")));

    // 建议要出现在清单里，否则用户拿到清单还是不知道该做什么。
    QVERIFY(summary.contains(QStringLiteral("关闭正在打开该文件的程序")));

    // 尝试过多次的要写出来，用户据此判断该继续等还是换办法。
    QVERIFY(summary.contains(QStringLiteral("已尝试 3 次")));
}

void TstBatch::failureSummaryIsEmptyWhenNothingFailed()
{
    BatchReport report;
    report.operationName = QStringLiteral("批量复制");
    report.records << makeRecord(QStringLiteral("/a.txt"), ErrorCode());

    QVERIFY(report.failureSummary().isEmpty());
}

void TstBatch::failureSummaryMentionsStopAndAttempts()
{
    BatchReport report;
    report.operationName = QStringLiteral("批量删除");
    report.stoppedEarly = true;
    report.records << makeRecord(QStringLiteral("/a.txt"), ErrorCode())
                   << makeRecord(QStringLiteral("/b.txt"), fromSystemError(EBUSY));

    // 提前结束时必须说清楚，否则用户会把「失败 1 个」理解成「只有 1 个有问题」，
    // 而真相是「根本还没轮到剩下的那些」。
    QVERIFY(report.failureSummary().contains(QStringLiteral("停止")));
}

// -----------------------------------------------------------------------------
// 3. 执行流程（完成标准第 3、4 条）
// -----------------------------------------------------------------------------

void TstBatch::defaultPolicyKeepsGoingAfterFailure()
{
    ProbeBatchOperation operation;
    operation.failAlways(QStringLiteral("/c.txt"), fromSystemError(EBUSY));

    const BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                         QStringLiteral("/b.txt"),
                                                         QStringLiteral("/c.txt"),
                                                         QStringLiteral("/d.txt"),
                                                         QStringLiteral("/e.txt")});

    // 第 4 条：中间那个失败之后，后面的条目照常执行。
    QCOMPARE(operation.callCount(), 5);
    QVERIFY(!report.stoppedEarly);

    QCOMPARE(report.totalCount(), 5);
    QCOMPARE(report.succeededCount(), 4);
    QCOMPARE(report.failedCount(), 1);
    QVERIFY(report.succeededPaths().contains(QStringLiteral("/d.txt")));
    QVERIFY(report.succeededPaths().contains(QStringLiteral("/e.txt")));
    QCOMPARE(report.failedPaths(), QStringList{QStringLiteral("/c.txt")});
}

void TstBatch::stopOnFirstErrorKeepsCompletedProgress()
{
    ProbeBatchOperation operation;
    operation.setPolicy(BatchFailurePolicy::StopOnFirstError);
    operation.failAlways(QStringLiteral("/c.txt"), fromSystemError(EBUSY));

    const BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                         QStringLiteral("/b.txt"),
                                                         QStringLiteral("/c.txt"),
                                                         QStringLiteral("/d.txt"),
                                                         QStringLiteral("/e.txt")});

    QVERIFY(report.stoppedEarly);
    QCOMPARE(report.policy, BatchFailurePolicy::StopOnFirstError);

    // 「停止」不等于「回滚」：已完成的两个必须在报告里，否则界面上的进度会倒退。
    QCOMPARE(report.totalCount(), 3);
    QCOMPARE(report.succeededCount(), 2);
    QCOMPARE(report.failedCount(), 1);

    // 后面的条目根本没被碰过——这一点必须能从调用日志上验证，
    // 而不能只看报告：报告里没有它们，也可能是因为「做了但没记」。
    QCOMPARE(operation.callCount(), 3);
    QVERIFY(!operation.callLog().contains(QStringLiteral("/d.txt")));
    QVERIFY(!operation.callLog().contains(QStringLiteral("/e.txt")));
}

void TstBatch::skippedItemsAreNotReportedAsSuccessful()
{
    ProbeBatchOperation operation;
    operation.setPolicy(BatchFailurePolicy::StopOnFirstError);
    operation.failAlways(QStringLiteral("/b.txt"), fromSystemError(EBUSY));

    const BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                         QStringLiteral("/b.txt"),
                                                         QStringLiteral("/c.txt")});

    QVERIFY(report.stoppedEarly);
    QCOMPARE(report.totalCount(), 2);
    QVERIFY(!report.allSucceeded());

    // 没轮到执行的条目既不在成功清单里，也不在失败清单里。
    // 这一点很要紧：把它算进「成功」会让用户以为整批做完了。
    QVERIFY(!report.succeededPaths().contains(QStringLiteral("/c.txt")));
    QVERIFY(!report.failedPaths().contains(QStringLiteral("/c.txt")));
}

void TstBatch::retryFailedOnlyRunsFailedItems()
{
    ProbeBatchOperation operation;
    operation.failAlways(QStringLiteral("/b.txt"), fromSystemError(EBUSY));

    BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                   QStringLiteral("/b.txt"),
                                                   QStringLiteral("/c.txt")});
    QCOMPARE(report.failedCount(), 1);
    QCOMPARE(operation.callCount(), 3);

    // 用户按建议关掉了占用程序，然后点「重试失败项」。
    operation.clearFailures();
    report = operation.retryFailed();

    QVERIFY(report.allSucceeded());
    QCOMPARE(operation.callCount(), 4); // 只多跑了一次

    // 第 2 条的关键断言：已经成功的条目**一次都不能**被重跑。
    // 只看「重试后全部成功」是测不出问题的——一个「整批重跑」的实现
    // 同样会全部成功，但它可能覆盖用户刚确认过的结果。
    QCOMPARE(operation.callsFor(QStringLiteral("/a.txt")), 1);
    QCOMPARE(operation.callsFor(QStringLiteral("/c.txt")), 1);
    QCOMPARE(operation.callsFor(QStringLiteral("/b.txt")), 2);
    QVERIFY(report.retried);
}

void TstBatch::retryFailedWithNoFailuresDoesNothing()
{
    ProbeBatchOperation operation;
    const BatchReport first = operation.run(QStringList{QStringLiteral("/a.txt")});
    QVERIFY(first.allSucceeded());
    const int callsBefore = operation.callCount();

    const BatchReport again = operation.retryFailed();

    // 一次调用都不发生。界面上的按钮在全部成功时本该是灰的，
    // 万一被点到，什么也不做才是对的——而不是把文件再做一遍。
    QCOMPARE(operation.callCount(), callsBefore);
    QVERIFY(again.allSucceeded());
    QCOMPARE(again.totalCount(), 1);
    QVERIFY(!again.retried);
}

void TstBatch::retryFailedMergesIntoWholeReport()
{
    ProbeBatchOperation operation;
    operation.failAlways(QStringLiteral("/b.txt"), fromSystemError(EBUSY));

    BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                   QStringLiteral("/b.txt"),
                                                   QStringLiteral("/c.txt")});
    QCOMPARE(report.totalCount(), 3);
    QCOMPARE(report.failedCount(), 1);

    operation.clearFailures();
    report = operation.retryFailed();

    // 返回的必须是**整批的当前状态**而不是「只含被重试的那几条」。
    // 否则界面上的计数器会从「完成 2/3」退回「完成 1/1」，
    // 用户会以为前面两个白做了——这正是第 4 条要防的「进度丢失」。
    QCOMPARE(report.totalCount(), 3);
    QCOMPARE(report.succeededCount(), 3);
    QVERIFY(report.succeededPaths().contains(QStringLiteral("/a.txt")));
    QVERIFY(report.succeededPaths().contains(QStringLiteral("/c.txt")));
    QVERIFY(report.retried);
}

void TstBatch::attemptsAccumulateAcrossRetries()
{
    ProbeBatchOperation operation;
    operation.failAlways(QStringLiteral("/a.txt"), fromSystemError(EBUSY));

    BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt")});
    QCOMPARE(report.records.first().attempts, 1);

    report = operation.retryFailed();
    QCOMPARE(report.records.first().attempts, 2);

    report = operation.retryFailed();
    QCOMPARE(report.records.first().attempts, 3);

    QCOMPARE(report.totalAttempts(), 3);
    QCOMPARE(operation.callsFor(QStringLiteral("/a.txt")), 3);
    // 用户要看到的是「一共试了几次」，所以是累加而不是覆盖。
    QVERIFY(report.failureSummary().contains(QStringLiteral("已尝试 3 次")));
}

void TstBatch::duplicatePathsAreCollapsed()
{
    ProbeBatchOperation operation;

    const BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                         QStringLiteral("/a.txt"),
                                                         QStringLiteral("/b.txt")});

    // 同一路径做两遍没有意义（第二遍看到的是第一遍的结果），而且会让失败清单
    // 里出现重复行，用户重试时会以为程序写错了。
    QCOMPARE(report.totalCount(), 2);
    QCOMPARE(operation.callCount(), 2);
    QCOMPARE(operation.callsFor(QStringLiteral("/a.txt")), 1);
}

void TstBatch::progressCallbackSeesEveryItem()
{
    ProbeBatchOperation operation;
    operation.failAlways(QStringLiteral("/b.txt"), fromSystemError(EBUSY));

    QStringList seenPaths;
    QVector<int> seenCompleted;
    QVector<int> seenTotal;

    operation.setProgressCallback([&](const BatchRecord &record, int completed, int total) {
        seenPaths.append(record.path);
        seenCompleted.append(completed);
        seenTotal.append(total);
    });

    operation.run(QStringList{QStringLiteral("/a.txt"), QStringLiteral("/b.txt"),
                              QStringLiteral("/c.txt")});

    // 失败的条目也要报进度。跳过它会让进度条停在 2/3,
    // 用户以为程序卡住了，而其实它已经做完了整批。
    QCOMPARE(seenPaths, (QStringList{QStringLiteral("/a.txt"), QStringLiteral("/b.txt"),
                                     QStringLiteral("/c.txt")}));

    QCOMPARE(seenCompleted.size(), 3);
    QCOMPARE(seenCompleted.at(0), 1);
    QCOMPARE(seenCompleted.at(1), 2);
    QCOMPARE(seenCompleted.at(2), 3);

    QCOMPARE(seenTotal.size(), 3);
    QCOMPARE(seenTotal.at(0), 3);
    QCOMPARE(seenTotal.at(1), 3);
    QCOMPARE(seenTotal.at(2), 3);
}

void TstBatch::reportCarriesRawCodeFromOperation()
{
    ProbeBatchOperation operation;
    operation.failAlways(QStringLiteral("/locked.txt"), fromSystemError(EBUSY));

    const BatchReport report = operation.run(QStringList{QStringLiteral("/locked.txt")});

    const QVector<FailureGroup> groups = report.failureGroups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().category, FileSystemError::Busy);

    // 原始码从 performOne() 一路传到失败清单，中间没有任何一步把它丢掉。
    QCOMPARE(groups.first().items.first().error.domain, ErrorDomain::Posix);
    QCOMPARE(groups.first().items.first().error.raw, qint64(EBUSY));

    QVERIFY(groups.first().retryable);
    QVERIFY(report.failureSummary().contains(QStringLiteral("EBUSY")));
}

void TstBatch::runAgainStartsANewBatch()
{
    ProbeBatchOperation operation;

    BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                   QStringLiteral("/b.txt")});
    QCOMPARE(report.totalCount(), 2);

    // 记录下这个行为：run() 是「重新开始一批」，不是「接着上一批」。
    // 它不叠加，也不保留上一次的尝试次数——否则重复点「开始」会让
    // 尝试次数越滚越大，界面上的「已尝试 N 次」就失去了意义。
    report = operation.run(QStringList{QStringLiteral("/a.txt")});
    QCOMPARE(report.totalCount(), 1);
    QCOMPARE(report.records.first().attempts, 1);
    QCOMPARE(operation.callCount(), 3);
}

// -----------------------------------------------------------------------------
// 4. 真实实现（批量改属性）
// -----------------------------------------------------------------------------

void TstBatch::setAttributesBatchReportsReadOnlyWithAdvice()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a.txt"));
    fileSystem.addFile(QStringLiteral("/b.txt"));
    fileSystem.fail(Test::FakeFileSystem::Operation::SetAttributes, QStringLiteral("/b.txt"),
                    fromSystemError(EROFS));

    SetAttributesBatch operation(fileSystem, FileAttribute::ReadOnly);
    const BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                         QStringLiteral("/b.txt")});

    QCOMPARE(report.operationName, QStringLiteral("设为只读"));
    QCOMPARE(report.totalCount(), 2);
    QCOMPARE(report.succeededCount(), 1);
    QCOMPARE(report.firstError(), FileSystemError::ReadOnlyFileSystem);

    const QVector<FailureGroup> groups = report.failureGroups();
    QCOMPARE(groups.size(), 1);
    QVERIFY(groups.first().advice.contains(QStringLiteral("只读")));
    // 「只读文件系统」重试多少次都一样，只能说换目标目录——因此不可重试。
    QVERIFY(!groups.first().retryable);

    // 失败的条目不影响已经完成的那一个。
    QVERIFY(isReadOnly(fileSystem, QStringLiteral("/a.txt")));
    QVERIFY(!isReadOnly(fileSystem, QStringLiteral("/b.txt")));
}

void TstBatch::setAttributesBatchRetriesAfterFailureCleared()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a.txt"));
    fileSystem.fail(Test::FakeFileSystem::Operation::SetAttributes, QStringLiteral("/a.txt"),
                    fromSystemError(EPERM));

    SetAttributesBatch operation(fileSystem, FileAttribute::ReadOnly);
    BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt")});

    QCOMPARE(report.failedCount(), 1);
    QCOMPARE(report.firstErrorCode().raw, qint64(EPERM));
    QVERIFY(!isReadOnly(fileSystem, QStringLiteral("/a.txt")));

    // 用户按建议处理之后回来重试。
    fileSystem.clearFailures();
    report = operation.retryFailed();

    QVERIFY(report.allSucceeded());
    QVERIFY(isReadOnly(fileSystem, QStringLiteral("/a.txt")));
    QCOMPARE(fileSystem.callCount(Test::FakeFileSystem::Operation::SetAttributes), 2);
}

void TstBatch::setAttributesBatchKeepsCompletedFilesOnFailure()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a.txt"));
    fileSystem.addFile(QStringLiteral("/b.txt"));
    fileSystem.addFile(QStringLiteral("/c.txt"));
    fileSystem.fail(Test::FakeFileSystem::Operation::SetAttributes, QStringLiteral("/b.txt"),
                    fromSystemError(EBUSY));

    SetAttributesBatch operation(fileSystem, FileAttribute::ReadOnly);
    const BatchReport report = operation.run(QStringList{QStringLiteral("/a.txt"),
                                                         QStringLiteral("/b.txt"),
                                                         QStringLiteral("/c.txt")});

    QCOMPARE(report.totalCount(), 3);
    QCOMPARE(report.failedCount(), 1);
    QVERIFY(!report.stoppedEarly);

    // 第 4 条最实际的含义：失败发生在中间，前后两个的改变**已经真的生效了**。
    // 不是「回滚掉重来」，也不是「后面的放弃」。
    QVERIFY(isReadOnly(fileSystem, QStringLiteral("/a.txt")));
    QVERIFY(!isReadOnly(fileSystem, QStringLiteral("/b.txt")));
    QVERIFY(isReadOnly(fileSystem, QStringLiteral("/c.txt")));

    QCOMPARE(report.retryablePaths(), QStringList{QStringLiteral("/b.txt")});
}

void TstBatch::setAttributesBatchOnRealFileSystem()
{
    std::unique_ptr<FileSystem> fileSystem(createNativeFileSystem());
    FileSystem *native = fileSystem.get();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString first = directory.filePath(QStringLiteral("first.txt"));
    const QString second = directory.filePath(QStringLiteral("second.txt"));
    const QString missing = directory.filePath(QStringLiteral("does-not-exist.txt"));

    for (const QString &path : {first, second}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("x"), qint64(1));
        file.close();
    }

    // 真机上把两个文件设为只读，第三个路径故意不存在：
    // 这同时验证了「成功照常落地」与「失败进清单且不中断」。
    SetAttributesBatch makeReadOnly(*native, FileAttribute::ReadOnly);
    BatchReport report = makeReadOnly.run(QStringList{first, second, missing});

    QCOMPARE(report.totalCount(), 3);
    QCOMPARE(report.succeededCount(), 2);
    QCOMPARE(report.failedCount(), 1);
    QCOMPARE(report.firstError(), FileSystemError::NotFound);
    QVERIFY(native->stat(first).isReadOnly());
    QVERIFY(native->stat(second).isReadOnly());

    const QVector<FailureGroup> groups = report.failureGroups();
    QCOMPARE(groups.size(), 1);
    QVERIFY(groups.first().advice.contains(QStringLiteral("删除")));

    // 再解除只读。这既验证了「反向操作走同一个类」，也把目录恢复成可清理的状态——
    // 留着一堆只读文件会让临时目录的清理在部分平台上失败。
    SetAttributesBatch makeWritable(*native, FileAttribute::None);
    QCOMPARE(makeWritable.operationName(), QStringLiteral("解除只读"));

    const BatchReport undone = makeWritable.run(QStringList{first, second});
    QVERIFY(undone.allSucceeded());
    QVERIFY(!native->stat(first).isReadOnly());
    QVERIFY(!native->stat(second).isReadOnly());
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "tst_batch.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstBatch)
