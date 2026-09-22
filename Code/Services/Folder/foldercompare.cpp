#include "foldercompare.h"
#include "entrystatus.h"
#include "maskfilter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QThread>
#include <algorithm>

namespace LqCompare {
namespace Folder {

QString statusLabel(Status status)
{
    switch (status) {
    case Status::Same: return QObject::tr("相同");
    case Status::Different: return QObject::tr("不同");
    case Status::LeftOnly: return QObject::tr("仅左存在");
    case Status::RightOnly: return QObject::tr("仅右存在");
    case Status::TypeConflict: return QObject::tr("类型冲突");
    case Status::Error: return QObject::tr("读取错误");
    case Status::Unknown: return QObject::tr("未知 / 未完整比较");
    // 这两档只在提供有效基线时出现（DIR-011 第 3 条）。
    case Status::BothChanged: return QObject::tr("两侧均改");
    case Status::Conflict: return QObject::tr("冲突");
    }
    return {};
}

bool Entry::isDirectory() const
{
    return left.kind == Kind::Directory || right.kind == Kind::Directory;
}

bool Entry::canCompareAsText() const
{
    return inComparison() && status != Status::Error && status != Status::TypeConflict
        && !(status == Status::Unknown && (!left.exists() || !right.exists()))
        && (left.kind == Kind::File || right.kind == Kind::File)
        && (left.kind == Kind::File || left.kind == Kind::Missing)
        && (right.kind == Kind::File || right.kind == Kind::Missing);
}

namespace {

QString cleanRoot(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isUnder(const QString &child, const QString &parent)
{
    const QString prefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return child.startsWith(prefix, Qt::CaseSensitive);
}

class Comparison
{
public:
    Comparison(const Options &options, const std::atomic_bool *cancelled,
               const Progress &progress, const Files::FileSystem &fs,
               const BaselineView *baseline)
        : options(options), cancelled(cancelled), progress(progress), fs(fs), baseline(baseline) {}

    Result run(const QString &leftRoot, const QString &rightRoot)
    {
        result.leftRoot = cleanRoot(leftRoot);
        result.rightRoot = cleanRoot(rightRoot);
        result.scanMaskDeclaration = options.scanMaskDeclaration;
        // 有效基线才置位：界面与报表靠这一位解释「为什么这一条不是两侧均改」。
        result.baselineApplied = baseline != nullptr && validateBaselineView(*baseline).isEmpty();
        const auto parsed = Filter::MaskFilter::parse(options.scanMaskDeclaration);
        if (!parsed.ok()) {
            result.error = QObject::tr("扫描掩码无效：\n%1").arg(parsed.describeErrors());
            result.complete = false;
            return result;
        }
        mask = parsed.filter;
        mask.setCaseSensitivity(options.nameCaseSensitivity);
        for (const QString &root : {result.leftRoot, result.rightRoot}) {
            const QFileInfo info(root);
            if (root.isEmpty() || !info.isDir()) {
                result.error = QObject::tr("请选择存在的文件夹：%1").arg(root);
                result.complete = false;
                return result;
            }
        }
        // Resolve only the two user-selected roots. Child links are never followed.
        const QString leftCanonical = QFileInfo(result.leftRoot).canonicalFilePath();
        const QString rightCanonical = QFileInfo(result.rightRoot).canonicalFilePath();
        if (leftCanonical == rightCanonical)
            result.warnings << QObject::tr("两侧是同一个文件夹。");
        else if (isUnder(leftCanonical, rightCanonical) || isUnder(rightCanonical, leftCanonical))
            result.warnings << QObject::tr("两侧文件夹互为父子目录；符号链接不会被跟随。");

        walk(result.leftRoot, result.rightRoot, QString(), 0, -1);
        result.cancelled = stopped();
        if (result.cancelled)
            result.complete = false;
        for (const auto &entry : result.entries) {
            if (!entry.inComparison())
                ++result.excludedCount;
            else if (entry.status == Status::Error || entry.status == Status::Unknown)
                result.complete = false;
        }
        return result;
    }

private:
    bool stopped() const { return cancelled && cancelled->load(std::memory_order_relaxed); }

    void applyMask(Entry &entry)
    {
        const auto decide = [this](const Side &side, const QString &root) {
            return mask.decide(Filter::MaskSubject::forPath(QDir(root).relativeFilePath(side.info.path)));
        };
        // An explicit exclusion on either actual filename excludes the pair;
        // an inclusion on either side is sufficient. This stays symmetric when
        // case-insensitive pairing matches differently-spelled names.
        QVector<Filter::MaskDecision> decisions;
        if (!entry.left.info.path.isEmpty())
            decisions.append(decide(entry.left, result.leftRoot));
        if (!entry.right.info.path.isEmpty())
            decisions.append(decide(entry.right, result.rightRoot));
        bool accepted = false;
        for (const auto &decision : decisions) {
            if (decision.verdict == Filter::MaskVerdict::Excluded) {
                entry.excludedByMask = true;
                entry.filterReason = decision.describe();
                return;
            }
            accepted |= decision.verdict == Filter::MaskVerdict::Included;
        }
        entry.excludedByMask = !accepted;
        if (!accepted)
            entry.filterReason = QObject::tr("未命中扫描掩码的包含规则。");
    }

    Side sideFor(const Files::FileInfo &listed)
    {
        Side side;
        Files::ErrorCode error;
        side.info = fs.stat(listed.path, &error);
        if (!error.ok() || !side.info.exists) {
            side.info.path = listed.path;
            side.info.name = listed.name;
            // The enumeration may already have identified a directory. Losing
            // that fact would hide an unreadable, masked parent whose descendants
            // could still match an include rule.
            side.kind = listed.isDirectory ? Kind::Directory
                : listed.isSymLink ? Kind::SymbolicLink : Kind::Missing;
            side.error = Files::errorReport(error.ok() ? Files::ErrorCode(Files::FileSystemError::NotFound)
                                                      : error, listed.path);
            return side;
        }
        if (side.info.isSymLink)
            side.kind = Kind::SymbolicLink;
        else if (side.info.isDirectory)
            side.kind = Kind::Directory;
        else if (QFileInfo(side.info.path).isFile())
            side.kind = Kind::File;
        else
            side.kind = Kind::Other; // FIFOs and devices must never be read like ordinary files.
        return side;
    }

    bool unchanged(const Side &side)
    {
        Files::ErrorCode error;
        const auto now = fs.stat(side.info.path, &error);
        return error.ok() && now.exists && !now.isSymLink && !now.isDirectory
            && now.size == side.info.size && now.lastModified == side.info.lastModified;
    }

    void compareFile(Entry &entry)
    {
        // 内容证据与主状态一起写：它是「结论建立在什么上」的那一维，
        // 任何一条早退分支都必须把它落到与环境相符的档上，否则会留下
        // 「判成相同、证据却是未比较」这种组合（由 statusModelViolations 抓）。
        entry.contentEvidence = ContentEvidence::NotCompared;
        if (entry.left.info.size != entry.right.info.size) {
            entry.status = Status::Different;
            entry.explanation = QObject::tr("文件大小不同，字节内容必然不同。");
        } else if (!options.compareContent) {
            entry.status = Status::Unknown;
            entry.explanation = QObject::tr("大小相同，但尚未比较内容。");
        } else {
            QFile left(fs.toNativePath(entry.left.info.path));
            QFile right(fs.toNativePath(entry.right.info.path));
            if (!left.open(QIODevice::ReadOnly)) {
                entry.status = Status::Error;
                entry.explanation = QObject::tr("无法读取 %1：%2").arg(left.fileName(), left.errorString());
                return;
            }
            if (!right.open(QIODevice::ReadOnly)) {
                entry.status = Status::Error;
                entry.explanation = QObject::tr("无法读取 %1：%2").arg(right.fileName(), right.errorString());
                return;
            }
            // 「只比较前 N 字节」的字节预算；-1 表示不设限（读到底）。
            //
            // 放在大小比较**之后**是有意的：大小不等已经**证明**了内容不同，
            // 而且不需要读一个字节。把它降级成「部分比较」等于把一个确定的结论
            // 说成不确定的——那不是保守，是把已经拿到的证据丢掉。
            const qint64 budget = options.compareFirstBytes > 0 ? options.compareFirstBytes : -1;
            qint64 offset = 0;
            bool budgetReached = false;
            entry.status = Status::Same;
            entry.contentEvidence = ContentEvidence::ByteIdentical;
            entry.explanation = QObject::tr("逐字节比较，内容完全相同。");
            while (!left.atEnd() || !right.atEnd()) {
                if (stopped()) {
                    entry.status = Status::Unknown;
                    entry.contentEvidence = ContentEvidence::NotCompared;
                    entry.explanation = QObject::tr("扫描已取消，内容比较不完整。");
                    break;
                }
                if (budget >= 0 && offset >= budget) {
                    // 到了预算边界而两侧都还有内容没读：这次比较**不足以**判定相同。
                    budgetReached = true;
                    break;
                }
                // 块大小以 kMaximumCompareBlockSize 为界，且不得越过剩余预算，
                // 否则「只比较前 N 字节」会把第 N 字节之后的内容也读进来。
                const qint64 want = budget >= 0
                    ? qMin(kMaximumCompareBlockSize, budget - offset)
                    : kMaximumCompareBlockSize;
                const QByteArray a = left.read(want);
                const QByteArray b = right.read(want);
                if (left.error() != QFileDevice::NoError || right.error() != QFileDevice::NoError) {
                    entry.status = Status::Error;
                    entry.contentEvidence = ContentEvidence::NotCompared;
                    entry.explanation = QObject::tr("读取内容失败：%1 / %2")
                                            .arg(left.errorString(), right.errorString());
                    break;
                }
                if (a != b) {
                    int first = 0;
                    while (first < qMin(a.size(), b.size()) && a.at(first) == b.at(first))
                        ++first;
                    entry.status = Status::Different;
                    entry.contentEvidence = ContentEvidence::ByteDifferent;
                    entry.firstDifference = offset + first;
                    entry.explanation = QObject::tr("字节内容不同；首个差异偏移：%1。")
                                            .arg(entry.firstDifference);
                    break;
                }
                offset += a.size();
            }
            // 只在「限内全部相同、且文件还没读完」时把状态降为未知。
            //
            // 这里复用 Status::Unknown 而不是新加一个 Status::Partial：
            // 主状态分类法（相同/不同/仅左/仅右/错误/未知）归 DIR-011，
            // 而它已经把「未完整比较」定义为 Unknown，并要求「部分比较」放在
            // **内容证据**这一维上而不是主状态里。`ContentEvidence::Partial`
            // 就是那一维；`Entry::partialComparison()` 只是它的一个视图。
            //
            // 副作用是有意的：run() 已经把 Unknown 映射成 complete=false，
            // 于是开了这个开关的整体结果必然标注为「不完整」——正合
            // DIR-008 边界要求的「明确标注为不完整比对」。
            // 两种例外不覆盖 explanation：取消（已写自己的原因）与错误。
            if (budgetReached && entry.status == Status::Same) {
                entry.status = Status::Unknown;
                entry.contentEvidence = ContentEvidence::Partial;
                entry.explanation = QObject::tr("只比较了前 %1 字节，剩余内容未比较（部分比较；不足以判定相同）。")
                                        .arg(budget);
            }
        }
        if (!unchanged(entry.left) || !unchanged(entry.right)) {
            entry.status = Status::Error;
            entry.firstDifference = -1;
            entry.contentEvidence = ContentEvidence::NotCompared;
            entry.explanation = QObject::tr("比较期间文件发生变化，请刷新后重试。");
        }
    }

    void classify(Entry &entry)
    {
        if ((!entry.left.error.isEmpty() || !entry.right.error.isEmpty())
            && (entry.isDirectory() || !entry.excludedByMask)) {
            entry.status = Status::Error;
            entry.explanation = (entry.left.error + QLatin1Char('\n') + entry.right.error).trimmed();
            entry.hasIncludedDescendants = entry.isDirectory();
        } else if (entry.excludedByMask) {
            entry.status = Status::Unknown;
            entry.explanation = QObject::tr("已被扫描掩码排除，未比较内容。%1").arg(entry.filterReason);
        } else if (!entry.left.exists()) {
            entry.status = Status::RightOnly;
            entry.explanation = QObject::tr("左侧目录中没有对应名称。");
        } else if (!entry.right.exists()) {
            entry.status = Status::LeftOnly;
            entry.explanation = QObject::tr("右侧目录中没有对应名称。");
        } else if (entry.left.kind != entry.right.kind) {
            entry.status = Status::TypeConflict;
            entry.explanation = QObject::tr("同一路径的条目类型不同（文件、文件夹或链接）。");
        } else if (entry.left.kind == Kind::File) {
            compareFile(entry);
        } else if (entry.left.kind == Kind::SymbolicLink) {
            Files::ErrorCode leftError, rightError;
            const QString a = fs.linkTarget(entry.left.info.path, &leftError);
            const QString b = fs.linkTarget(entry.right.info.path, &rightError);
            if (!leftError.ok() || !rightError.ok()) {
                entry.status = Status::Error;
                entry.explanation = QObject::tr("无法读取符号链接：%1 %2")
                                        .arg(Files::errorReport(leftError), Files::errorReport(rightError));
            } else {
                entry.status = a == b ? Status::Same : Status::Different;
                // 比较的是链接目标字符串本身，因此这是「字节」档的证据：
                // 链接没有内容可言，目标字符串相等就是全部事实。
                entry.contentEvidence = a == b ? ContentEvidence::ByteIdentical
                                               : ContentEvidence::ByteDifferent;
                entry.explanation = QObject::tr("比较链接本身的目标路径，未跟随链接：%1 / %2").arg(a, b);
            }
        } else {
            entry.status = Status::Unknown;
            entry.explanation = entry.isDirectory() ? QObject::tr("子目录尚未扫描。")
                                                    : QObject::tr("特殊文件未读取。");
        }
    }

    void walk(const QString &leftPath, const QString &rightPath, const QString &relative,
              int depth, int parent)
    {
        if (stopped())
            return;
        Files::ErrorCode leftError, rightError;
        const auto leftEntries = leftPath.isEmpty() ? QVector<Files::FileInfo>()
                                                    : fs.enumerateDirectory(leftPath, &leftError);
        const auto rightEntries = rightPath.isEmpty() ? QVector<Files::FileInfo>()
                                                      : fs.enumerateDirectory(rightPath, &rightError);
        const bool listFailed = !leftError.ok() || !rightError.ok();
        if (listFailed) {
            const QString detail = (!leftError.ok() ? Files::errorReport(leftError, leftPath) : QString())
                + QLatin1Char('\n')
                + (!rightError.ok() ? Files::errorReport(rightError, rightPath) : QString());
            if (parent >= 0) {
                result.entries[parent].status = Status::Error;
                result.entries[parent].explanation = detail.trimmed();
                // We cannot prove a failed subtree contains no included items.
                // Keep its error visible even when the directory name was masked.
                result.entries[parent].hasIncludedDescendants = true;
            } else {
                Entry errorEntry;
                errorEntry.relativePath = QStringLiteral(".");
                errorEntry.status = Status::Error;
                errorEntry.explanation = detail.trimmed();
                result.entries.append(errorEntry);
                result.error = detail.trimmed();
            }
            result.complete = false;
        }

        struct Pairing {
            Files::FileInfo left;
            Files::FileInfo right;
            QString ambiguity;
        };
        struct Group {
            QVector<Files::FileInfo> left;
            QVector<Files::FileInfo> right;
        };
        QMap<QString, Group> groups;
        const auto keyFor = [this](const QString &name) {
            return options.nameCaseSensitivity == Qt::CaseInsensitive ? name.toCaseFolded() : name;
        };
        for (const auto &entry : leftEntries)
            groups[keyFor(entry.name)].left.append(entry);
        for (const auto &entry : rightEntries)
            groups[keyFor(entry.name)].right.append(entry);
        QMap<QString, Pairing> pairs;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            const auto &group = it.value();
            if (group.left.size() <= 1 && group.right.size() <= 1) {
                Pairing pair;
                if (!group.left.isEmpty()) pair.left = group.left.first();
                if (!group.right.isEmpty()) pair.right = group.right.first();
                pairs.insert(!pair.left.name.isEmpty() ? pair.left.name : pair.right.name, pair);
                continue;
            }
            // Never collapse case-fold collisions into one row or guess a pair.
            // Keep all actual names so the user can fix the ambiguity or switch
            // back to case-sensitive comparison without any lost entries.
            QStringList names;
            for (const auto &entry : group.left) names << entry.name;
            for (const auto &entry : group.right) names << entry.name;
            names.removeDuplicates();
            names.sort(Qt::CaseSensitive);
            const QString reason = QObject::tr("忽略大小写后名称有歧义，未自动配对：%1。请启用区分大小写。")
                                       .arg(names.join(QStringLiteral(" / ")));
            for (const auto &entry : group.left) {
                pairs[entry.name].left = entry;
                pairs[entry.name].ambiguity = reason;
            }
            for (const auto &entry : group.right) {
                pairs[entry.name].right = entry;
                pairs[entry.name].ambiguity = reason;
            }
        }
        const int childrenStart = result.entries.size();
        for (auto it = pairs.cbegin(); it != pairs.cend(); ++it) {
            if (stopped())
                break;
            Entry entry;
            entry.relativePath = relative.isEmpty() ? it.key() : relative + QLatin1Char('/') + it.key();
            if (!it.value().left.path.isEmpty())
                entry.left = sideFor(it.value().left);
            if (!it.value().right.path.isEmpty())
                entry.right = sideFor(it.value().right);
            applyMask(entry);
            if (!it.value().ambiguity.isEmpty() && (!entry.excludedByMask || entry.isDirectory())) {
                entry.status = Status::Error;
                entry.explanation = it.value().ambiguity;
                // Ambiguous directories cannot be traversed safely, so their
                // potentially included descendants must remain an explicit error.
                entry.hasIncludedDescendants = entry.isDirectory();
            } else
                classify(entry);
            entry.nameCaseDifference = entry.left.exists() && entry.right.exists()
                && entry.left.info.name != entry.right.info.name && it.value().ambiguity.isEmpty();
            if (entry.nameCaseDifference)
                entry.explanation += QObject::tr("\n名称仅大小写不同：%1 / %2。")
                                         .arg(entry.left.info.name, entry.right.info.name);
            if ((entry.status == Status::LeftOnly && !rightError.ok())
                || (entry.status == Status::RightOnly && !leftError.ok())) {
                entry.status = Status::Unknown;
                entry.explanation = QObject::tr("对侧目录读取失败，无法确定是否仅在一侧存在。");
            }
            // 三个维度的补齐顺序是刻意的：先让主状态定稿（上面全部早退分支都跑完），
            // 再补时间关系与基线细化。基线细化会改写主状态，因此它必须在最后；
            // 而它又只作用在叶子上（目录的结论由子条目汇总，见第 5 条）。
            entry.timeRelation = timeRelationFor(entry);
            if (baseline && !entry.isDirectory())
                applyBaselineStatus(entry, *baseline);
            const int index = result.entries.size();
            result.entries.append(entry);
            if (progress)
                progress(result.entries.size(), entry.relativePath);
            if (entry.isDirectory() && entry.status != Status::TypeConflict
                && entry.status != Status::Error
                && it.value().ambiguity.isEmpty()
                && !(listFailed && (!entry.left.exists() || !entry.right.exists()))) {
                if (options.recursive && depth < qBound(0, options.maximumDepth, 256)) {
                    walk(entry.left.kind == Kind::Directory ? entry.left.info.path : QString(),
                         entry.right.kind == Kind::Directory ? entry.right.info.path : QString(),
                         entry.relativePath, depth + 1, index);
                } else {
                    if (entry.left.exists() && entry.right.exists())
                        result.entries[index].status = Status::Unknown;
                    result.entries[index].explanation = options.recursive
                        ? QObject::tr("已达到递归深度上限，目录内容未比较。")
                        : QObject::tr("子目录未展开比较。");
                    if (options.recursive) {
                        // A masked directory at the safety limit may contain
                        // included descendants we could not inspect.
                        result.entries[index].hasIncludedDescendants = true;
                        result.entries[index].status = Status::Unknown;
                    }
                    if (result.entries[index].inComparison())
                        result.complete = false;
                }
            }
        }

        if (parent >= 0 && !listFailed) {
            // 父目录的结论走 `aggregateChildren()`——引擎与用例调用的是**同一份**
            // 实现，否则「固定数据源与准则下父子视图结论一致」这条标准就变成了
            // 「两套口径碰巧今天结果相同」。
            QVector<ChildStatus> children;
            children.reserve(result.entries.size() - childrenStart);
            for (int i = childrenStart; i < result.entries.size(); ++i) {
                const auto &child = result.entries.at(i);
                children.append({child.status, child.inComparison()});
            }
            const ParentAggregate aggregate = aggregateChildren(children, stopped());
            result.entries[parent].hasIncludedDescendants = aggregate.hasIncludedDescendants;
            // A masked-out directory is still traversed: an include such as
            // src/**/*.cpp can match descendants even if src itself is excluded.
            if (result.entries[parent].excludedByMask && !aggregate.hasIncludedDescendants)
                return;
            if (result.entries[parent].left.exists() && result.entries[parent].right.exists()
                && result.entries[parent].left.kind != result.entries[parent].right.kind) {
                result.entries[parent].status = Status::TypeConflict;
                result.entries[parent].explanation = QObject::tr("同一路径的条目类型不同；扫描范围内包含其后代。");
                return;
            }
            if (!result.entries[parent].left.exists() || !result.entries[parent].right.exists()) {
                result.entries[parent].status = result.entries[parent].left.exists()
                    ? Status::LeftOnly : Status::RightOnly;
                return;
            }
            result.entries[parent].status = aggregate.status;
            result.entries[parent].explanation = aggregate.status == Status::Same
                ? aggregate.hasExcludedDescendants
                    ? QObject::tr("扫描掩码范围内的条目相同；被排除的内容未比较。")
                    : QObject::tr("目录中的全部条目相同。")
                : QObject::tr("目录状态由已扫描的子条目汇总；详情请展开查看。");
        }
    }

    const Options &options;
    const std::atomic_bool *cancelled;
    const Progress &progress;
    const Files::FileSystem &fs;
    const BaselineView *baseline;
    Filter::MaskFilter mask;
    Result result;
};

} // namespace

Result compare(const QString &leftRoot, const QString &rightRoot, const Options &options,
               const std::atomic_bool *cancelled, const Progress &progress,
               const Files::FileSystem *fileSystem, const BaselineView *baseline)
{
    std::unique_ptr<Files::FileSystem> native(fileSystem ? nullptr : Files::createNativeFileSystem());
    return Comparison(options, cancelled, progress, fileSystem ? *fileSystem : *native, baseline)
        .run(leftRoot, rightRoot);
}

Scanner::Scanner(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<LqCompare::Folder::Result>();
}

Scanner::~Scanner()
{
    cancel();
    if (m_thread) {
        m_thread->disconnect(this);
        m_thread->wait();
        delete m_thread;
    }
}

bool Scanner::start(const QString &leftRoot, const QString &rightRoot, const Options &options)
{
    if (isRunning())
        return false;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_cancelled;
    auto result = std::make_shared<Result>();
    m_thread = QThread::create([this, leftRoot, rightRoot, options, cancelFlag, result] {
        QElapsedTimer elapsed;
        elapsed.start();
        *result = compare(leftRoot, rightRoot, options, cancelFlag.get(),
                          [this, &elapsed](int count, const QString &path) {
            if (count == 1 || elapsed.elapsed() >= 75) {
                emit progressChanged(count, path);
                elapsed.restart();
            }
        });
    });
    connect(m_thread, &QThread::finished, this, [this, result] {
        QThread *completed = m_thread;
        m_thread = nullptr;
        completed->deleteLater();
        emit finished(*result);
    });
    m_thread->start();
    return true;
}

void Scanner::cancel()
{
    if (m_cancelled)
        m_cancelled->store(true, std::memory_order_relaxed);
}

} // namespace Folder
} // namespace LqCompare
