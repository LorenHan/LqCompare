#include "tst_trash.h"

#include "faketrashservice.h"
#include "trash.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

namespace LqCompare {
namespace Files {
// 保守后端没有 WinAPI 依赖；测试直接调用平台工厂，以便每个平台执行相同实现。
TrashService *createWindowsTrashService();
}
}

using namespace LqCompare::Files;

// -----------------------------------------------------------------------------
// 失败信息可读化
// -----------------------------------------------------------------------------

namespace QTest {

/// 让失败信息里直接显示「volume-not-supported」而不是「Compared values are not the same」。
/// 断言失败时看得懂原因，是这些护栏能否被信任的前提。
template <>
char *toString(const TrashAvailability &availability)
{
    return qstrdup(trashAvailabilityIdentifier(availability));
}

template <>
char *toString(const FileSystemError &error)
{
    return qstrdup(errorIdentifier(error));
}

/// ErrorCode 的显示：分类 + 原始系统码（PLAT-008 第 5 条）。
/// 删除失败最常见的是权限与占用，而二者的原始码是查清原因的唯一线索。
template <>
char *toString(const ErrorCode &error)
{
    const QString text = errorReport(error);
    return qstrdup(qPrintable(text.isEmpty() ? QStringLiteral("none") : text));
}

} // namespace QTest

namespace {

// 测试里反复用到的两个真实路径样本。
const QString kNetworkPath = QStringLiteral("/mnt/network-share");

/// 保证测试结束时把进过废纸篓的那一份挪回原处。
///
/// 为什么需要它：真实往返用例断言失败时会直接 return，后面的清理代码不会执行，
/// 于是用户的废纸篓里会留下一个刻意造出来的垃圾文件。而**失败恰恰是最容易
/// 发生的时候**——一个只在成功路径上清理的测试，等于把清理工作交给了最不可靠的时机。
class TrashCleanupGuard
{
public:
    TrashCleanupGuard(const TrashService *service, const QString &inTrash,
                      const QString &restoreTo)
        : m_service(service), m_inTrash(inTrash), m_restoreTo(restoreTo)
    {
    }

    ~TrashCleanupGuard()
    {
        if (m_inTrash.isEmpty() || m_restoreTo.isEmpty())
            return;
        // 已经不在废纸篓里（正常还原成功），或原位置已被占用，都不用管。
        if (!QFile::exists(m_inTrash) || QFile::exists(m_restoreTo))
            return;
        // 优先走服务还原，Linux 还会一并清掉 .trashinfo；直接搬走文件会留下幽灵记录。
        if (!m_service->undoLastDelete())
            QFile::rename(m_inTrash, m_restoreTo);
    }

    void disarm() { m_inTrash.clear(); }

private:
    const TrashService *m_service;
    QString m_inTrash;
    QString m_restoreTo;
};

/// 测试自己造出来的文件，直接删掉即可（它本来就不该留在废纸篓里）。
class TrashFileRemover
{
public:
    explicit TrashFileRemover(const QString &path) : m_path(path) {}
    ~TrashFileRemover()
    {
        if (!m_path.isEmpty())
            QFile::remove(m_path);
    }

private:
    QString m_path;
};

/// 造一个临时文件并写入内容，返回路径。
QString createTemporaryFile(const QString &directory, const QString &name, const QByteArray &content)
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(content);
    file.close();
    return path;
}

} // namespace

// -----------------------------------------------------------------------------
// 1. 可用性与决策
// -----------------------------------------------------------------------------

void TstTrash::onlyAvailableCountsAsUsable()
{
    QVERIFY(isTrashUsable(TrashAvailability::Available));

    // 其余一律不可用，包括 Unknown。
    // 把 Unknown 也算可用是很自然但很危险的直觉：「探不到」不等于「可以用」，
    // 赌注是用户的文件。
    QVERIFY(!isTrashUsable(TrashAvailability::VolumeNotSupported));
    QVERIFY(!isTrashUsable(TrashAvailability::QuotaExceeded));
    QVERIFY(!isTrashUsable(TrashAvailability::NoSpace));
    QVERIFY(!isTrashUsable(TrashAvailability::PlatformNotSupported));
    QVERIFY(!isTrashUsable(TrashAvailability::Unknown));
    QVERIFY(!isTrashUsable(TrashAvailability::RecoverabilityNotGuaranteed));
}

void TstTrash::availableNeedsNoUserChoice()
{
    const TrashDecision decision = decideTrash(TrashAvailability::Available);

    QVERIFY(!decision.requiresUserChoice);
    QCOMPARE(decision.availability, TrashAvailability::Available);
    // 可用时不该弹任何理由或建议——那会让正常的删除多一次无意义的点击。
    QVERIFY(decision.reason.isEmpty());
    QVERIFY(decision.advice.isEmpty());
}

void TstTrash::unavailableAlwaysNeedsUserChoice()
{
    const TrashAvailability unavailable[] = {
        TrashAvailability::VolumeNotSupported,
        TrashAvailability::QuotaExceeded,
        TrashAvailability::NoSpace,
        TrashAvailability::PlatformNotSupported,
        TrashAvailability::Unknown,
        TrashAvailability::RecoverabilityNotGuaranteed,
    };

    for (TrashAvailability availability : unavailable) {
        const TrashDecision decision = decideTrash(availability);
        QVERIFY2(decision.requiresUserChoice,
                 qPrintable(QStringLiteral("状态 %1 未经用户选择就被放行")
                            .arg(QString::fromLatin1(trashAvailabilityIdentifier(availability)))));
        QVERIFY(!decision.reason.isEmpty());
        QVERIFY(!decision.advice.isEmpty());
    }
}

void TstTrash::eachUnavailableReasonHasItsOwnAdvice()
{
    // 「网络盘没有回收站」和「配额已满」的处置完全不同：前者要换位置，
    // 后者要清空回收站。如果两者给的是同一句话，用户按建议做了也不管用。
    const QString volumeAdvice = decideTrash(TrashAvailability::VolumeNotSupported).advice;
    const QString quotaAdvice = decideTrash(TrashAvailability::QuotaExceeded).advice;
    const QString spaceAdvice = decideTrash(TrashAvailability::NoSpace).advice;

    QVERIFY(volumeAdvice != quotaAdvice);
    QVERIFY(quotaAdvice != spaceAdvice);
    QVERIFY(volumeAdvice != spaceAdvice);

    // 建议要能落实到动作上，不能只是重述问题。
    QVERIFY(volumeAdvice.contains(QStringLiteral("取消")));
    QVERIFY(quotaAdvice.contains(QStringLiteral("清空回收站")));
    QVERIFY(spaceAdvice.contains(QStringLiteral("空间")));
}

void TstTrash::adviceWarnsThatPermanentDeleteIsIrreversible()
{
    // 平台完全没有回收站时，永久删除是唯一的替代方案。
    // 此时提示必须**明说不可逆**，否则用户会把它当成和平时一样的删除，
    // 而这一次是真的回不来了。
    const TrashDecision decision = decideTrash(TrashAvailability::PlatformNotSupported);

    QVERIFY(decision.advice.contains(QStringLiteral("无法还原")));
    QVERIFY(decision.reason.contains(QStringLiteral("没有可用的回收站")));
}

// -----------------------------------------------------------------------------
// 2. XDG 路径规则（Linux 的规则，在 macOS 上真实执行）
// -----------------------------------------------------------------------------

void TstTrash::xdgHomeTrashUsesDataHome()
{
    QCOMPARE(xdgHomeTrashDirectory(QStringLiteral("/home/u"), QStringLiteral("/home/u/.data")),
             QStringLiteral("/home/u/.data/Trash"));

    // dataHome 带结尾分隔符时不能多出一个分隔符。
    QCOMPARE(xdgHomeTrashDirectory(QStringLiteral("/home/u"), QStringLiteral("/home/u/.data/")),
             QStringLiteral("/home/u/.data/Trash"));
}

void TstTrash::xdgHomeTrashFallsBackToLocalShare()
{
    // XDG_DATA_HOME 未设置时按规范取 $HOME/.local/share。
    QCOMPARE(xdgHomeTrashDirectory(QStringLiteral("/home/u"), QString()),
             QStringLiteral("/home/u/.local/share/Trash"));
}

void TstTrash::xdgHomeTrashIsEmptyWithoutHome()
{
    // 拿不到家目录（以服务身份运行、环境里没有 HOME）时返回空串，
    // 而不是凭空编一个路径——调用方会因此走卷内回收站这条路。
    QVERIFY(xdgHomeTrashDirectory(QString(), QString()).isEmpty());
    QVERIFY(xdgHomeTrashDirectory(QString(), QStringLiteral("/home/u/.data")).isEmpty());
}

void TstTrash::xdgVolumeTrashUsesUserId()
{
    QCOMPARE(xdgVolumeTrashDirectory(QStringLiteral("/media/usb"), 1000, true),
             QStringLiteral("/media/usb/.Trash/1000"));

    // 挂载点带结尾分隔符时同样不能多出分隔符。
    QCOMPARE(xdgVolumeTrashDirectory(QStringLiteral("/media/usb/"), 1000, true),
             QStringLiteral("/media/usb/.Trash/1000"));
}

void TstTrash::xdgVolumeTrashRefusesUnwritableVolume()
{
    // 该卷不可写（网络盘、只读挂载）时返回空串，也就是明确「这里没有回收站」。
    // 返回一个看起来合理但建不出来的路径，会让调用方在删除中途才发现失败——
    // 那时它已经告诉用户「可以删」了。
    QVERIFY(xdgVolumeTrashDirectory(QStringLiteral("/mnt/ro"), 1000, false).isEmpty());
    QVERIFY(xdgVolumeTrashDirectory(QString(), 1000, true).isEmpty());
}

void TstTrash::xdgVolumeTrashFallbackUsesSuffix()
{
    // .Trash/<uid> 建不出来时（.Trash 已属于别的用户）的回退位置。
    QCOMPARE(xdgVolumeTrashFallbackDirectory(QStringLiteral("/media/usb"), 1000),
             QStringLiteral("/media/usb/.Trash-1000"));
    QVERIFY(xdgVolumeTrashFallbackDirectory(QString(), 1000).isEmpty());
}

void TstTrash::xdgTrashInfoEncodesSpecialCharacters()
{
    const QString path = QStringLiteral("/home/u/我的 报告.txt");
    const QString contents = xdgTrashInfoContents(path, QDateTime(QDate(2026, 3, 1), QTime(12, 34, 56)));

    QVERIFY(contents.startsWith(QStringLiteral("[Trash Info]\n")));
    QVERIFY(contents.endsWith(QLatin1Char('\n')));

    // 空格必须编码成 %20。含空格的文件名在别人的回收站里不编码，
    // 解析方会在第一个空格处截断，还原时回到一个不存在的路径。
    QVERIFY(contents.contains(QStringLiteral("%20")));

    // 非 ASCII 也要编码。没编码的话这里会原样出现中文字符。
    QVERIFY(!contents.contains(QStringLiteral("报告")));

    // 但分隔符 '/' 要保留：Path 是路径而不是单个名字，
    // 把分隔符也编码掉会得到一个无法还原的字符串。
    QVERIFY(contents.contains(QStringLiteral("Path=/home/u/")));
}

void TstTrash::xdgTrashInfoRoundTripsThroughParsing()
{
    // 往返是最有力的断言：写进去再读回来必须得到同一个路径。
    // 逐字符断言编码结果会把测试绑死在具体编码细节上（%20 还是 +，
    // 中文用 UTF-8 还是别的），而往返只要求「能还原」这个真正重要的性质。
    const QStringList samples = {
        QStringLiteral("/home/u/plain.txt"),
        QStringLiteral("/home/u/with space.txt"),
        QStringLiteral("/home/u/我的报告.txt"),
        QStringLiteral("/home/u/100% 完成.txt"),
        QStringLiteral("/home/u/a#b?c.txt"),
    };

    for (const QString &path : samples) {
        const QString contents = xdgTrashInfoContents(path, QDateTime::currentDateTime());
        QCOMPARE(parseXdgTrashInfoPath(contents), path);
    }
}

void TstTrash::xdgTrashInfoUsesLocalTimeWithoutZoneSuffix()
{
    const QDateTime local(QDate(2026, 3, 1), QTime(12, 34, 56));
    const QString contents = xdgTrashInfoContents(QStringLiteral("/a.txt"), local);

    // 规范规定 DeletionDate 是本地时间且不带时区后缀。
    // 写成 UTC 或带 "Z" 会让部分实现解析失败。
    QVERIFY(contents.contains(QStringLiteral("DeletionDate=2026-03-01T12:34:56\n")));
    QVERIFY(!contents.contains(QStringLiteral("Z\n")));
    QVERIFY(!contents.contains(QStringLiteral("+08")));
}

void TstTrash::parseXdgTrashInfoIgnoresOtherSections()
{
    // 规范允许实现往 .trashinfo 里追加自定义段与自定义键。
    // 不加段判断就会把别的段里的 Path 当成目标，还原到错误的路径。
    const QString contents = QStringLiteral(
            "[Other Info]\n"
            "Path=/wrong/target.txt\n"
            "[Trash Info]\n"
            "Path=/right/target.txt\n"
            "DeletionDate=2026-03-01T12:34:56\n"
            "X-Custom-Key=whatever\n");

    QCOMPARE(parseXdgTrashInfoPath(contents), QStringLiteral("/right/target.txt"));
}

void TstTrash::parseXdgTrashInfoWithoutPathIsEmpty()
{
    QVERIFY(parseXdgTrashInfoPath(QString()).isEmpty());
    QVERIFY(parseXdgTrashInfoPath(QStringLiteral("[Trash Info]\nDeletionDate=2026-03-01T12:34:56\n")).isEmpty());

    // 段名写错（大小写不同）时不应误取——规范里的段名是精确匹配的。
    QVERIFY(parseXdgTrashInfoPath(QStringLiteral("[trash info]\nPath=/a.txt\n")).isEmpty());
}

// -----------------------------------------------------------------------------
// 3. 错误分类（Cocoa）
// -----------------------------------------------------------------------------

void TstTrash::classifyCocoaErrors()
{
    QCOMPARE(classifyCocoaError(0), FileSystemError::None);
    QCOMPARE(classifyCocoaError(CocoaError::NoSuchFile), FileSystemError::NotFound);
    QCOMPARE(classifyCocoaError(CocoaError::WriteNoPermission), FileSystemError::PermissionDenied);
    QCOMPARE(classifyCocoaError(CocoaError::ReadNoPermission), FileSystemError::PermissionDenied);
    QCOMPARE(classifyCocoaError(CocoaError::WriteOutOfSpace), FileSystemError::NoSpace);
    QCOMPARE(classifyCocoaError(CocoaError::WriteVolumeReadOnly), FileSystemError::ReadOnlyFileSystem);
    QCOMPARE(classifyCocoaError(CocoaError::WriteInvalidFileName), FileSystemError::InvalidName);
    QCOMPARE(classifyCocoaError(CocoaError::WriteFileExists), FileSystemError::AlreadyExists);
}

void TstTrash::classifyCocoaUnmountBusyIsRetryable()
{
    // 卸载繁忙与文件加锁都归 Busy，因为它们的共同特征是「等一会儿可能就好了」。
    // 落到 Unknown 会让界面失去「重试」这个选项。
    QCOMPARE(classifyCocoaError(CocoaError::ManagerUnmountBusy), FileSystemError::Busy);
    QCOMPARE(classifyCocoaError(CocoaError::FileLocking), FileSystemError::Busy);

    QVERIFY(isRetryable(classifyCocoaError(CocoaError::ManagerUnmountBusy)));
    QVERIFY(isRetryable(classifyCocoaError(CocoaError::FileLocking)));
}

void TstTrash::classifyCocoaUnknownFallsBackToUnknown()
{
    QCOMPARE(classifyCocoaError(999999), FileSystemError::Unknown);
}

// -----------------------------------------------------------------------------
// 4. 模板方法契约（假替身）
// -----------------------------------------------------------------------------

void TstTrash::unavailableTrashNeverCallsTheMover()
{
    Test::FakeTrashService service;
    service.setAvailabilityFor(kNetworkPath, TrashAvailability::VolumeNotSupported);

    const TrashReport report =
            service.deleteToTrash(QStringList{kNetworkPath + QStringLiteral("/a.txt")});

    QVERIFY(!report.succeeded());
    QCOMPARE(report.firstError(), FileSystemError::NotSupported);

    // 最关键的一条断言：**搬移函数一次都没被调用**。
    //
    // 只断言「返回了失败」是不够的——一个「先搬移、再报失败」的实现同样满足
    // 那句断言，但用户的文件已经没了。这条断言才真正守住「不可用时一个字节都不动」。
    //
    // 它同时也验证了基类模板方法的价值：平台实现根本接触不到这条路径，
    // 因此不可能不小心跳过可用性检查。
    QCOMPARE(service.callCount(Test::FakeTrashService::Operation::TrashPaths), 0);
    QVERIFY(service.trashedPaths().isEmpty());
    QVERIFY(service.trashContents().isEmpty());
}

void TstTrash::unavailableTrashReportsEveryEntry()
{
    // 提示里要能一次说清「这几个都不行」，而不是删一次被拦一次、
    // 用户得一个个试。因此所有条目都必须出现在报告里，
    // 即使第一条就已经判定不可用。
    Test::FakeTrashService service;
    service.setDefaultAvailability(TrashAvailability::VolumeNotSupported);

    const QStringList targets = {QStringLiteral("/a.txt"), QStringLiteral("/b.txt"),
                                 QStringLiteral("/c.txt")};
    const TrashReport report = service.deleteToTrash(targets);

    QCOMPARE(report.records.size(), 3);
    QCOMPARE(report.failedPaths(), targets);

    // 三条都要查过可用性，不能查出一条不行就提前收工。
    QCOMPARE(service.callCount(Test::FakeTrashService::Operation::Availability), 3);
}

void TstTrash::batchWithOneUnusableEntryIsRejectedWholly()
{
    // 批量语义是「要么都做、要么都不做」。
    //
    // 只把网络盘上的那一条跳过、其余的照删，会让用户看到「列表里少了几项、
    // 回收站里只有一部分」——而这是最难恢复的状态：用户不知道自己丢了什么、
    // 也不知道哪些还在。宁可不做，也不要部分做。
    Test::FakeTrashService service;
    service.setAvailabilityFor(kNetworkPath, TrashAvailability::VolumeNotSupported);

    const TrashReport report = service.deleteToTrash(
            QStringList{QStringLiteral("/home/u/ok.txt"), kNetworkPath + QStringLiteral("/no.txt")});

    QVERIFY(!report.succeeded());
    QCOMPARE(report.records.size(), 2);
    QCOMPARE(service.callCount(Test::FakeTrashService::Operation::TrashPaths), 0);
    QVERIFY(service.trashContents().isEmpty());
}

void TstTrash::batchKeepsPartlySucceededEntries()
{
    // 可用性检查全过、但搬移时个别失败——这是真实的现场（文件被占用，
    // 或在检查与搬移之间空间被别人占满）。此时成功的那些必须老老实实
    // 留在回收站里：把它们回滚掉，用户会以为删成了、其实文件还在原处。
    Test::FakeTrashService service;
    service.failTrashPath(QStringLiteral("/b.txt"), FileSystemError::Busy);

    const TrashReport report =
            service.deleteToTrash(QStringList{QStringLiteral("/a.txt"), QStringLiteral("/b.txt"),
                                              QStringLiteral("/c.txt")});

    QVERIFY(!report.succeeded());
    QVERIFY(report.anySucceeded());
    QCOMPARE(report.firstError(), FileSystemError::Busy);
    QCOMPARE(report.failedPaths(), QStringList{QStringLiteral("/b.txt")});
    QCOMPARE(service.trashedPaths(), (QStringList{QStringLiteral("/a.txt"), QStringLiteral("/c.txt")}));
}

void TstTrash::successfulRecordMayHaveNoFilesystemPath()
{
    // Windows 的 Shell 命名空间不是磁盘路径。这里只验证报告契约，绝不真的删除：
    // 当前后端不能自动还原，所以真实删除夹具无法保证失败时完整清理。
    TrashRecord record;
    record.originalPath = QStringLiteral("C:/temporary/report.txt");
    record.error = FileSystemError::None;
    QVERIFY(record.trashedPath.isEmpty());
    QVERIFY(record.succeeded());

    TrashReport report;
    report.records.append(record);
    QVERIFY(report.succeeded());
    QVERIFY(report.anySucceeded());
    QCOMPARE(report.firstError(), FileSystemError::None);
    QCOMPARE(report.firstErrorCode(), ErrorCode());
    QVERIFY(report.failedPaths().isEmpty());
    QVERIFY(report.trashedPaths().isEmpty());
    QCOMPARE(report.records.first().originalPath, record.originalPath);
}

void TstTrash::trashedPathsContainsOnlyKnownSuccessfulPaths()
{
    TrashReport report;
    report.records.append({QStringLiteral("/a.txt"), QStringLiteral("/trash/a.2.txt"),
                           FileSystemError::None});
    report.records.append({QStringLiteral("C:/b.txt"), QString(), FileSystemError::None});
    report.records.append({QStringLiteral("/c.txt"), QStringLiteral("/trash/c.txt"),
                           FileSystemError::Busy});
    report.records.append({QStringLiteral("/d.txt"), QStringLiteral("/trash/d.txt"),
                           FileSystemError::None});

    // 既不能把未知位置编造为一个路径，也不能因某个成功条目没有路径而丢掉其余记录。
    QCOMPARE(report.trashedPaths(),
             (QStringList{QStringLiteral("/trash/a.2.txt"), QStringLiteral("/trash/d.txt")}));
    QCOMPARE(report.failedPaths(), QStringList{QStringLiteral("/c.txt")});
    QVERIFY(!report.succeeded());
    QVERIFY(report.anySucceeded());
    QCOMPARE(report.firstError(), FileSystemError::Busy);
}

void TstTrash::trashNeverRecordsPermanentDeletion()
{
    Test::FakeTrashService service;
    service.deleteToTrash(QStringList{QStringLiteral("/a.txt"), QStringLiteral("/b.txt")});

    // 这个列表永远必须为空。它一旦不为空，就说明有代码绕过了回收站——
    // 「删除必须可逆」这条对用户的基本承诺被破坏了。
    QVERIFY2(service.permanentlyDeletedPaths().isEmpty(),
             "存在被永久删除的路径：删除的可逆性被破坏");

    // 同时要确认删除**确实发生了**（进了回收站），否则上面那句空断言
    // 在一个什么都没做的实现上也会通过。
    QCOMPARE(service.trashContents().size(), 2);
}

void TstTrash::undoUsesTheActualTrashedPath()
{
    // 回收站里已有同名文件时，真实实现（macOS 与 XDG）都会自动改名。
    // 因此撤销必须用实现返回的 trashedPath，不能自己拿原名去拼路径。
    //
    // 这个替身刻意模拟了改名，就是为了让「按原名拼路径」的写法在这里失败——
    // 那种写法在真机上要等到一次重名才会暴露，而暴露时的现象是
    // 「撤销说找不到文件」，用户多半以为是自己清空过回收站。
    Test::FakeTrashService service;

    service.deleteToTrash(QStringList{QStringLiteral("/docs/报告.txt")});
    const QString firstName = service.lastDelete().records.at(0).trashedPath;

    service.deleteToTrash(QStringList{QStringLiteral("/other/报告.txt")});
    const QString secondName = service.lastDelete().records.at(0).trashedPath;

    QVERIFY2(firstName != secondName, "替身没有模拟重名改名，这个用例就白写了");

    ErrorCode error;
    QVERIFY(service.undoLastDelete(&error));
    QCOMPARE(error, FileSystemError::None);

    // 撤销之后回收站里只剩第一次的那一份——第二次是被正确还原的那一次。
    QCOMPARE(service.trashContents(), QStringList{firstName});
}

void TstTrash::undoWithoutPriorDeleteIsNotFound()
{
    Test::FakeTrashService service;

    ErrorCode error;
    QVERIFY(!service.undoLastDelete(&error));

    // 「还没删除过」与「还原失败」必须能区分：前者界面该显示
    // 「没有可还原的删除」，后者要给出具体原因与建议。
    // 两者都报一句「还原失败」会让用户去找一个根本不存在的问题。
    QCOMPARE(error, FileSystemError::NotFound);
}

void TstTrash::undoClearsTheUndoPoint()
{
    // 撤销过之后就不该再能撤销。第二次撤销会把已经回到原处的文件再挪进回收站，
    // 而用户以为自己只是在「取消上一次撤销」——那是真正的数据丢失。
    Test::FakeTrashService service;
    service.deleteToTrash(QStringList{QStringLiteral("/a.txt")});

    ErrorCode error;
    QVERIFY(service.undoLastDelete(&error));
    QCOMPARE(error, FileSystemError::None);

    QVERIFY(service.lastDelete().records.isEmpty());
    QVERIFY(!service.undoLastDelete(&error));
    QCOMPARE(error, FileSystemError::NotFound);
}

void TstTrash::lastDeleteKeepsFailedBatches()
{
    // 失败批次也要更新撤销点。
    //
    // 若不更新，用户点「撤销」还原的会是**更早的那一次**删除，
    // 而界面上刚刚提示过一次删除失败——撤销的对象和用户以为的对不上，
    // 结果是回收站里少了一份本该在的东西。
    Test::FakeTrashService service;
    service.deleteToTrash(QStringList{QStringLiteral("/first.txt")});

    service.failTrashPath(QStringLiteral("/second.txt"), FileSystemError::Busy);
    service.deleteToTrash(QStringList{QStringLiteral("/second.txt")});

    QCOMPARE(service.lastDelete().failedPaths(), QStringList{QStringLiteral("/second.txt")});
}

void TstTrash::windowsTrashRefusesUnverifiedRecovery()
{
    const std::unique_ptr<TrashService> service(createWindowsTrashService());
    QCOMPARE(service->platformName(), QStringLiteral("windows"));
    QCOMPARE(service->displayLocation(), QStringLiteral("shell:RecycleBinFolder"));
    const TrashAvailability availability = service->availabilityFor(QStringLiteral("C:/example.txt"));
    QCOMPARE(availability, TrashAvailability::RecoverabilityNotGuaranteed);
    QCOMPARE(QString::fromLatin1(trashAvailabilityIdentifier(availability)),
             QStringLiteral("recoverability-not-guaranteed"));
    const TrashDecision decision = decideTrash(availability);
    QVERIFY(decision.requiresUserChoice);
    QVERIFY(decision.reason.contains(QStringLiteral("不能保证删除可恢复")));
    QVERIFY(decision.advice.contains(QStringLiteral("保留文件")));
    const TrashFallback defaultChoice{};
    QVERIFY(defaultChoice == TrashFallback::Cancel);

    // 新状态也必须在共同入口拦住，不能绕到搬移函数或任何永久删除回退。
    Test::FakeTrashService fake;
    fake.setDefaultAvailability(availability);
    const TrashReport rejected = fake.deleteToTrash({QStringLiteral("/untouched.txt")});
    QVERIFY(!rejected.anySucceeded());
    QCOMPARE(rejected.firstError(), FileSystemError::NotSupported);
    QCOMPARE(fake.callCount(Test::FakeTrashService::Operation::TrashPaths), 0);
    QVERIFY(fake.permanentlyDeletedPaths().isEmpty());

    ErrorCode error = FileSystemError::Busy;
    QVERIFY(!service->undoLastDelete(&error));
    QCOMPARE(error, FileSystemError::NotSupported);
    QVERIFY(!service->undoLastDelete(nullptr));
    QVERIFY(service->lastDelete().records.isEmpty());
}

void TstTrash::windowsTrashRejectsFilesWithoutChangingTheirBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray content("keep\0these\r\nbytes", 17);
    const QString original = createTemporaryFile(directory.path(), QStringLiteral("原件.txt"), content);
    const QString sibling = createTemporaryFile(directory.path(), QStringLiteral("sibling.txt"),
                                                QByteArrayLiteral("leave sibling alone"));
    QVERIFY(!original.isEmpty());
    QVERIFY(!sibling.isEmpty());
    const QStringList before = QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);

    const std::unique_ptr<TrashService> service(createWindowsTrashService());
    const TrashReport report = service->deleteToTrash({original});
    QVERIFY(!report.succeeded());
    QVERIFY(!report.anySucceeded());
    QCOMPARE(report.records.size(), 1);
    QCOMPARE(report.failedPaths(), QStringList{original});
    QCOMPARE(report.firstError(), FileSystemError::NotSupported);
    QVERIFY(report.trashedPaths().isEmpty());
    QVERIFY(report.records.first().trashedPath.isEmpty());
    QCOMPARE(service->lastDelete().failedPaths(), QStringList{original});
    QVERIFY(!service->lastDelete().anySucceeded());

    QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
    QFile originalFile(original);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    QCOMPARE(originalFile.readAll(), content);
    QFile siblingFile(sibling);
    QVERIFY(siblingFile.open(QIODevice::ReadOnly));
    QCOMPARE(siblingFile.readAll(), QByteArrayLiteral("leave sibling alone"));
    ErrorCode error;
    QVERIFY(!service->undoLastDelete(&error));
    QCOMPARE(error, FileSystemError::NotSupported);
    QCOMPARE(service->lastDelete().failedPaths(), QStringList{original});
}

void TstTrash::windowsTrashRejectsDirectoriesWithoutChangingTheirContents()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.path() + QStringLiteral("/target");
    const QString nested = target + QStringLiteral("/nested");
    QVERIFY(QDir().mkpath(nested));
    const QString child = createTemporaryFile(nested, QStringLiteral("child.txt"),
                                              QByteArrayLiteral("nested original"));
    const QString marker = createTemporaryFile(directory.path(), QStringLiteral("parent-marker.txt"),
                                               QByteArrayLiteral("parent original"));
    QVERIFY(!child.isEmpty());
    QVERIFY(!marker.isEmpty());
    const QStringList before = QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);

    const std::unique_ptr<TrashService> service(createWindowsTrashService());
    const TrashReport report = service->deleteToTrash({child, nested, target});
    QVERIFY(!report.succeeded());
    QVERIFY(!report.anySucceeded());
    QCOMPARE(report.records.size(), 3);
    QCOMPARE(report.failedPaths(), (QStringList{child, nested, target}));
    QVERIFY(report.trashedPaths().isEmpty());
    for (const TrashRecord &record : report.records) {
        QCOMPARE(record.error, FileSystemError::NotSupported);
        QVERIFY(record.trashedPath.isEmpty());
    }
    QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
    QCOMPARE(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("nested")});
    QCOMPARE(QDir(nested).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("child.txt")});
    QFile childFile(child);
    QVERIFY(childFile.open(QIODevice::ReadOnly));
    QCOMPARE(childFile.readAll(), QByteArrayLiteral("nested original"));
    QFile markerFile(marker);
    QVERIFY(markerFile.open(QIODevice::ReadOnly));
    QCOMPARE(markerFile.readAll(), QByteArrayLiteral("parent original"));
}

void TstTrash::windowsTrashEmptyBatchIsANoopAndMissingTargetsAreRejected()
{
    const std::unique_ptr<TrashService> service(createWindowsTrashService());
    const TrashReport empty = service->deleteToTrash({});
    QVERIFY(empty.succeeded());
    QVERIFY(!empty.anySucceeded());
    QVERIFY(empty.records.isEmpty());
    QVERIFY(service->lastDelete().records.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing = directory.path() + QStringLiteral("/does-not-exist");
    const TrashReport report = service->deleteToTrash({QString(), missing});
    QVERIFY(!report.succeeded());
    QVERIFY(!report.anySucceeded());
    QCOMPARE(report.records.size(), 2);
    QCOMPARE(report.failedPaths(), (QStringList{QString(), missing}));
    for (const TrashRecord &record : report.records) {
        // 能力尚未开放，不能声称已查询到 NotFound，更不能把空路径当成空批次成功。
        QCOMPARE(record.error, FileSystemError::NotSupported);
        QVERIFY(record.trashedPath.isEmpty());
    }
    QVERIFY(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(!QFile::exists(missing));
    QVERIFY(!service->lastDelete().anySucceeded());
}

// -----------------------------------------------------------------------------
// 5. 真实实现（本机平台）
// -----------------------------------------------------------------------------

void TstTrash::nativeTrashServiceReportsItsPlatform()
{
    const std::unique_ptr<TrashService> service(createNativeTrashService());
    QVERIFY(service != nullptr);

#if defined(Q_OS_WIN)
    QCOMPARE(service->platformName(), QStringLiteral("windows"));
#elif defined(Q_OS_MACOS)
    QCOMPARE(service->platformName(), QStringLiteral("macos"));
#elif defined(Q_OS_LINUX)
    QCOMPARE(service->platformName(), QStringLiteral("linux"));
#else
    QCOMPARE(service->platformName(), QStringLiteral("unsupported"));
#endif
}

void TstTrash::nativeTrashServiceHasDisplayLocation()
{
    // 界面要告诉用户「文件移到哪儿去了」，位置可以是目录，也可以是 Shell 命名空间。
    // 兜底实现返回空串是允许的，但当前三个平台实现都必须给出位置。
    const std::unique_ptr<TrashService> service(createNativeTrashService());
    const QString location = service->displayLocation();

#if defined(Q_OS_WIN)
    QCOMPARE(location, QStringLiteral("shell:RecycleBinFolder"));
    QVERIFY(!QDir::isAbsolutePath(location));
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    QVERIFY2(!location.isEmpty(), "平台实现没有给出回收站位置，界面无法提示用户");
    QVERIFY(QDir::isAbsolutePath(location));
#else
    QVERIFY(location.isEmpty());
#endif
}

void TstTrash::nativeTrashAvailabilityNeverAssumesUsable()
{
    // 不能断言「临时目录一定是 Available」——CI 或某些环境下 /tmp 可能是另一块盘、
    // 甚至是不支持废纸篓的内存盘。能断言的是这两条：
    const std::unique_ptr<TrashService> service(createNativeTrashService());
    const QString probe = QDir::tempPath();

    // 1. 探测是确定性的：同一个路径问两次答案必须一样。
    //    两次不同说明探测里混进了会变的东西（剩余空间、时间），
    //    那会让「删除前问一次、删除时再判断一次」得到矛盾的结果。
    QCOMPARE(service->availabilityFor(probe), service->availabilityFor(probe));

    // 2. 答案与决策自洽：可用就免询问，不可用就必须先问用户。
    const TrashAvailability answer = service->availabilityFor(probe);
    const TrashDecision decision = decideTrash(answer);
    QCOMPARE(decision.availability, answer);
    QCOMPARE(decision.requiresUserChoice, !isTrashUsable(answer));

    // 这里刻意**不**断言「不存在的路径必须返回 Unknown」。
    //
    // 本机实测：macOS 对 /no/such/volume 也能解析出所在卷（它会向上找到 "/"）
    // 并回答 Available——这不是 bug，而是正确行为：删除前判断的是「这个位置
    // 所在的卷有没有回收站」，与条目此刻在不在无关（同步场景下它可能刚被
    // 别的进程处理掉）。真正的「文件不存在」会在搬移时报 NotFound，
    // 那时错误分类会给出准确原因。
    //
    // 「探测失败不能当成可用」这条约束由假替身的 Unknown 注入来守——
    // 那是可以稳定复现的，见 unavailableAlwaysNeedsUserChoice()。
}

void TstTrash::nativeTrashUndoWithoutPriorDeleteReportsCapability()
{
    const std::unique_ptr<TrashService> service(createNativeTrashService());
    QVERIFY(service->lastDelete().records.isEmpty());

    ErrorCode error = FileSystemError::Busy;
    QVERIFY(!service->undoLastDelete(&error));
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    QCOMPARE(error, FileSystemError::NotFound);
#else
    // Windows 当前后端明确拒绝自动还原；不能把无事可做伪装成已还原成功。
    QCOMPARE(error, FileSystemError::NotSupported);
#endif
    QVERIFY(!service->undoLastDelete(nullptr));
    QVERIFY(service->lastDelete().records.isEmpty());
}

void TstTrash::nativeTrashRoundTripRestoresTheFile()
{
#if defined(Q_OS_WIN)
    // 在任何删除之前说明边界，不能先删再因取不到实际路径而让夹具滞留系统回收站。
    QSKIP("Windows 当前后端拒绝未验证可恢复性的删除；拒绝删除与 NotSupported 契约由独立用例验证");
#endif
    const std::unique_ptr<TrashService> service(createNativeTrashService());

    if (!isTrashUsable(service->availabilityFor(QDir::tempPath())))
        QSKIP("本机临时目录没有回收站，跳过真实往返用例");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString victim = createTemporaryFile(directory.path(),
                                               QStringLiteral("lqcompare-trash-roundtrip.txt"),
                                               QByteArrayLiteral("do not lose me"));
    QVERIFY(!victim.isEmpty());

    const TrashReport report = service->deleteToTrash(QStringList{victim});
    const QString inTrash = report.records.value(0).trashedPath;
    TrashCleanupGuard guard(service.get(), inTrash, victim);
    QVERIFY2(report.succeeded(), "删除到回收站失败：这个用例的前提是它应该成功");
    QCOMPARE(report.records.size(), 1);

    // 断言 1：原位置确实空了。没空说明删除根本没发生。
    QVERIFY(!QFile::exists(victim));

    // 断言 2：文件**确实在废纸篓里**，而不是被无声删掉了。
    // 这是「删除必须可逆」最直接的证据：文件还在某个地方。
    QVERIFY(!inTrash.isEmpty());
    QVERIFY2(QFile::exists(inTrash),
             qPrintable(QStringLiteral("废纸篓里找不到 %1：文件可能被永久删除了").arg(inTrash)));

    ErrorCode error;
    QVERIFY2(service->undoLastDelete(&error),
             qPrintable(QStringLiteral("还原失败：%1").arg(errorMessage(error))));
    QCOMPARE(error, FileSystemError::None);

    // 断言 3：文件回到了原处，而且废纸篓里那份已经不在。
    QVERIFY(QFile::exists(victim));
    QVERIFY(!QFile::exists(inTrash));
    QFile restored(victim);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArrayLiteral("do not lose me"));

    guard.disarm();
}

void TstTrash::nativeTrashRestoreRefusesToOverwrite()
{
#if defined(Q_OS_WIN)
    QSKIP("Windows 当前后端拒绝未验证可恢复性的删除；拒绝删除与 NotSupported 契约由独立用例验证");
#endif
    const std::unique_ptr<TrashService> service(createNativeTrashService());

    if (!isTrashUsable(service->availabilityFor(QDir::tempPath())))
        QSKIP("本机临时目录没有回收站，跳过真实还原冲突用例");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString original = createTemporaryFile(directory.path(),
                                                 QStringLiteral("lqcompare-occupied.txt"),
                                                 QByteArrayLiteral("original content"));
    QVERIFY(!original.isEmpty());

    const TrashReport report = service->deleteToTrash(QStringList{original});
    const QString inTrash = report.records.value(0).trashedPath;
    TrashCleanupGuard guard(service.get(), inTrash, original);
    QVERIFY2(report.succeeded(), "删除到回收站失败：这个用例的前提是它应该成功");
    QCOMPARE(report.records.size(), 1);
    QVERIFY(!inTrash.isEmpty());
    QVERIFY(QFile::exists(inTrash));

    // 模拟「用户已经把另一个文件放回原位置了」。
    const QString replacement = createTemporaryFile(directory.path(),
                                                    QStringLiteral("lqcompare-occupied.txt"),
                                                    QByteArrayLiteral("a file the user cares about"));
    // 先清理测试自造的占位文件，再由 guard 正常还原原件，避免留下 .trashinfo。
    TrashFileRemover remover(replacement);
    QVERIFY(!replacement.isEmpty());

    ErrorCode error;
    QVERIFY2(!service->undoLastDelete(&error), "原位置已被占用时还原必须失败，而不是覆盖");

    // 覆盖会删掉一个用户没打算碰、而且**不在废纸篓里**的文件——比「还原失败」
    // 严重得多，因为那个文件无法恢复。所以这里必须是 AlreadyExists，
    // 让界面能提示「原位置已有同名文件，请先处理」。
    QCOMPARE(error, FileSystemError::AlreadyExists);

    // 原位置上的文件必须原封不动。
    QFile survivor(replacement);
    QVERIFY(survivor.open(QIODevice::ReadOnly));
    QCOMPARE(survivor.readAll(), QByteArrayLiteral("a file the user cares about"));
    survivor.close();

    // 废纸篓里的那一份也还在（还原失败不该顺手把它删了）。
    QVERIFY(QFile::exists(inTrash));
    // 不 disarm：正常与失败路径都先清理占位文件，再还原原件。
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
// 曾经在这里加过一次 .moc include，报的是「No rule to make target」。
QTEST_MAIN(TstTrash)
