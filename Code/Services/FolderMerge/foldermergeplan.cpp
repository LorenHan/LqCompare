#include "foldermergeplan.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMap>
#include <QThread>

namespace LqCompare {
namespace FolderMerge {
namespace {
using Kind = Folder::Kind;
using Status = Folder::Status;
using Side = Folder::Side;
using Index = QMap<QString, Folder::Entry>;

QString normalized(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool descendant(const QString &child, const QString &parent)
{
    return child.startsWith(parent + QLatin1Char('/'), Qt::CaseSensitive);
}

const Side *chosen(const Entry &entry, Decision decision)
{
    switch (decision) {
    case Decision::TakeLeft: return &entry.left;
    case Decision::TakeRight: return &entry.right;
    case Decision::TakeBase: return &entry.base;
    default: return nullptr;
    }
}

bool sameSnapshot(const Side &a, const Side &b)
{
    return a.exists() == b.exists() && a.kind == b.kind
        && (!a.exists() || (a.info.size == b.info.size && a.info.lastModified == b.info.lastModified));
}

Index indexResult(const Folder::Result &result)
{
    Index index;
    for (const auto &entry : result.entries)
        if (entry.relativePath != QStringLiteral("."))
            index.insert(entry.relativePath, entry);
    return index;
}

bool equal(const QString &path, const Side &a, const Side &b, const Index &comparison)
{
    if (!a.exists() && !b.exists()) return true;
    if (!a.exists() || !b.exists() || a.kind != b.kind) return false;
    const auto it = comparison.constFind(path);
    return it != comparison.cend() && it->status == Status::Same;
}

bool validPath(const QString &path)
{
    if (path.isEmpty() || path == QStringLiteral(".") || QDir::isAbsolutePath(path)) return false;
    const auto parts = path.split(QLatin1Char('/'));
    for (const auto &part : parts)
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) return false;
    return true;
}

// A directory row is a container, not a blanket copy operation. Each child keeps
// its own decision. Structural conflicts block descendants until resolved.
void refresh(Plan &plan)
{
    QMap<QString, int> indices;
    for (int i = 0; i < plan.entries.size(); ++i) {
        auto &entry = plan.entries[i];
        entry.blockedByAncestor = false;
        entry.descendantConflicts = 0;
        QString ancestor = entry.relativePath;
        while (ancestor.contains(QLatin1Char('/'))) {
            ancestor = ancestor.left(ancestor.lastIndexOf(QLatin1Char('/')));
            const auto it = indices.constFind(ancestor);
            if (it == indices.cend()) continue;
            const auto &parent = plan.entries.at(*it);
            if (parent.decision == Decision::Unresolved || parent.blockedByAncestor) {
                entry.blockedByAncestor = true;
                break;
            }
            const Side *source = chosen(parent, parent.decision);
            const bool parentHasDirectory = source && source->kind == Kind::Directory;
            const bool childOmitted = entry.decision == Decision::Ignore || entry.decision == Decision::Delete;
            if (!parentHasDirectory && !childOmitted) {
                entry.blockedByAncestor = true;
                break;
            }
        }
        indices.insert(entry.relativePath, i);
    }
    for (const auto &entry : plan.entries) {
        if (!entry.unresolved()) continue;
        QString ancestor = entry.relativePath;
        while (ancestor.contains(QLatin1Char('/'))) {
            ancestor = ancestor.left(ancestor.lastIndexOf(QLatin1Char('/')));
            const auto it = indices.constFind(ancestor);
            if (it != indices.cend()) ++plan.entries[*it].descendantConflicts;
        }
    }
}

void classify(Entry &entry, const Plan &plan, const Index &bl, const Index &br, const Index &lr)
{
    const auto conflict = [&entry](Conflict value, const QString &explanation) {
        entry.conflict = value;
        entry.explanation = explanation;
    };
    if (!plan.complete) {
        conflict(Conflict::Incomplete, QObject::tr("扫描不完整或来源在扫描期间变化；不推断缺失，不生成删除。请重新扫描。"));
        return;
    }
    for (const Side *side : {&entry.base, &entry.left, &entry.right}) {
        if (side->exists() && side->kind == Kind::Other) {
            conflict(Conflict::Unsupported, QObject::tr("特殊文件未比较，不能自动合并。"));
            return;
        }
    }
    // A change in kind is always explicitly reviewed, including a one-sided
    // file-to-directory replacement. No recursive copy hides that decision.
    QVector<Kind> kinds;
    for (const Side *side : {&entry.base, &entry.left, &entry.right})
        if (side->exists() && !kinds.contains(side->kind)) kinds.append(side->kind);
    if (kinds.size() > 1) {
        conflict(Conflict::Type, QObject::tr("祖先/左/右的条目类型不同，需明确选择保留哪一侧。"));
        return;
    }
    const bool leftRightEqual = equal(entry.relativePath, entry.left, entry.right, lr);
    if (!plan.hasBase) {
        if (leftRightEqual) {
            entry.decision = Decision::TakeLeft;
            entry.explanation = QObject::tr("没有祖先；左右内容完全相同，可共同采用，但无法判断修改历史。");
        } else if (entry.left.kind == Kind::Directory && entry.right.kind == Kind::Directory) {
            entry.decision = Decision::TakeLeft;
            entry.explanation = QObject::tr("没有祖先；目录仅作为容器，子项需分别决策。");
        } else {
            conflict(Conflict::NoBase, QObject::tr("没有祖先，无法判断单侧新增、删除或修改；需人工选择。"));
        }
        return;
    }
    if (leftRightEqual) {
        entry.decision = entry.left.exists() ? Decision::TakeLeft : Decision::Delete;
        entry.explanation = entry.left.exists()
            ? QObject::tr("左右内容相同，采用共同结果。") : QObject::tr("左右均已删除，计划不输出该条目。");
        return;
    }
    const bool leftBaseEqual = equal(entry.relativePath, entry.base, entry.left, bl);
    const bool rightBaseEqual = equal(entry.relativePath, entry.base, entry.right, br);
    if (leftBaseEqual) {
        entry.decision = entry.right.exists() ? Decision::TakeRight : Decision::Delete;
        entry.explanation = QObject::tr("左侧与祖先相同，仅右侧改变；采用右侧结果。");
    } else if (rightBaseEqual) {
        entry.decision = entry.left.exists() ? Decision::TakeLeft : Decision::Delete;
        entry.explanation = QObject::tr("右侧与祖先相同，仅左侧改变；采用左侧结果。");
    } else if (entry.base.exists() && (!entry.left.exists() || !entry.right.exists())) {
        conflict(Conflict::DeleteModify, QObject::tr("一侧删除，另一侧相对祖先有修改（含子目录内容）；需人工决策。"));
    } else if (entry.left.kind == Kind::Directory && entry.right.kind == Kind::Directory) {
        entry.decision = Decision::TakeLeft;
        entry.explanation = QObject::tr("目录作为合并容器；左右的子项分别按祖先判定，展开查看决策。");
    } else {
        conflict(Conflict::Content, QObject::tr("左右相对祖先均有改动且不相同；需人工选择或请求文本合并。"));
    }
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

QString decisionLabel(Decision decision)
{
    switch (decision) {
    case Decision::Unresolved: return QObject::tr("待决策");
    case Decision::TakeLeft: return QObject::tr("取左侧");
    case Decision::TakeRight: return QObject::tr("取右侧");
    case Decision::TakeBase: return QObject::tr("取祖先");
    case Decision::Delete: return QObject::tr("删除 / 不输出");
    case Decision::Ignore: return QObject::tr("忽略（不写入）");
    }
    return {};
}

QString conflictLabel(Conflict conflict)
{
    switch (conflict) {
    case Conflict::None: return QObject::tr("无冲突");
    case Conflict::Content: return QObject::tr("内容冲突");
    case Conflict::DeleteModify: return QObject::tr("删除 / 修改冲突");
    case Conflict::Type: return QObject::tr("类型冲突");
    case Conflict::NoBase: return QObject::tr("无祖先，无法推断");
    case Conflict::Incomplete: return QObject::tr("扫描不完整");
    case Conflict::Unsupported: return QObject::tr("未支持的条目");
    }
    return {};
}

QString kindLabel(Kind kind)
{
    switch (kind) {
    case Kind::Missing: return QObject::tr("不存在");
    case Kind::File: return QObject::tr("文件");
    case Kind::Directory: return QObject::tr("目录");
    case Kind::SymbolicLink: return QObject::tr("符号链接");
    case Kind::Other: return QObject::tr("特殊文件");
    }
    return {};
}

bool Entry::isDirectory() const
{
    return base.kind == Kind::Directory || left.kind == Kind::Directory || right.kind == Kind::Directory;
}

bool Entry::unresolved() const
{
    return decision == Decision::Unresolved || blockedByAncestor;
}

bool Entry::canRequestTextMerge() const
{
    return !blockedByAncestor && conflict == Conflict::Content && decision == Decision::Unresolved
        && base.kind == Kind::File && left.kind == Kind::File && right.kind == Kind::File;
}

int Plan::unresolvedCount() const
{
    int count = 0;
    for (const auto &entry : entries) if (entry.unresolved()) ++count;
    return count;
}

int Plan::manualCount() const
{
    int count = 0;
    for (const auto &entry : entries) if (entry.manual) ++count;
    return count;
}

const Entry *Plan::find(const QString &relativePath) const
{
    for (const auto &entry : entries) if (entry.relativePath == relativePath) return &entry;
    return nullptr;
}

QString Plan::executionDisabledReason() const
{
    return QObject::tr("仅提供只读合并计划。事务写入、输出备份与恢复尚未实现，应用合并结果已禁用；输入和输出均不会被修改。");
}

QString Plan::previewText() const
{
    // Escape control characters so filenames cannot forge rows in this report.
    const auto escaped = [](QString text) {
        text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
        text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        return text;
    };
    QStringList lines;
    lines << QObject::tr("文件夹合并逻辑计划（只读预演，不是实际写入操作清单）")
          << QObject::tr("祖先：%1").arg(escaped(paths.base))
          << QObject::tr("左侧：%1").arg(escaped(paths.left))
          << QObject::tr("右侧：%1").arg(escaped(paths.right))
          << QObject::tr("输出：%1").arg(escaped(paths.output))
          << QObject::tr("完整扫描：%1；未决策：%2；人工决策：%3")
                 .arg(complete ? QObject::tr("是") : QObject::tr("否"))
                 .arg(unresolvedCount()).arg(manualCount())
          << executionDisabledReason();
    if (!error.isEmpty()) lines << QObject::tr("错误：%1").arg(escaped(error));
    for (const auto &warning : warnings) lines << QObject::tr("提示：%1").arg(escaped(warning));
    lines << QObject::tr("相对路径\t左类型\t祖先类型\t右类型\t输出决策\t来源\t判定\t说明");
    for (const auto &entry : entries) {
        const Side *source = chosen(entry, entry.decision);
        lines << QStringList{escaped(entry.relativePath), kindLabel(entry.left.kind), kindLabel(entry.base.kind),
                              kindLabel(entry.right.kind), decisionLabel(entry.decision),
                              source ? escaped(source->info.path) : QString(),
                              (entry.manual ? QObject::tr("人工；") : QObject::tr("自动；"))
                                  + conflictLabel(entry.conflict)
                                  + (entry.blockedByAncestor ? QObject::tr("；被父项阻止") : QString()),
                              escaped(entry.explanation)}.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

Plan buildPlan(const Paths &paths, const Folder::Options &options, const std::atomic_bool *cancelled,
               const Folder::Progress &progress, const Files::FileSystem *fileSystem)
{
    Plan plan;
    plan.paths = {normalized(paths.base), normalized(paths.left), normalized(paths.right), normalized(paths.output)};
    plan.hasBase = !plan.paths.base.isEmpty();
    if (options.nameCaseSensitivity != Qt::CaseSensitive) {
        plan.error = QObject::tr("三方计划要求区分大小写的精确相对路径；暂不支持忽略大小写配对。");
        return plan;
    }
    std::unique_ptr<Files::FileSystem> native(fileSystem ? nullptr : Files::createNativeFileSystem());
    const Files::FileSystem *fs = fileSystem ? fileSystem : native.get();
    QMap<QString, Files::FileInfo> rootSnapshots;
    for (const QString &root : {plan.paths.base, plan.paths.left, plan.paths.right}) {
        if (!root.isEmpty()) rootSnapshots.insert(root, fs->stat(root));
    }
    if (!plan.hasBase) plan.warnings << QObject::tr("未提供祖先：两路模式不能推断哪侧新增、删除或修改。");
    plan.warnings << QObject::tr("输出目录现状未扫描；本计划仅展示合并归属，不代表已验证的新增/覆盖/删除操作。未知输出条目不会被删除。");
    if (plan.paths.output.isEmpty()) plan.warnings << QObject::tr("尚未选择输出目录。");
    else {
        const auto canonicalOrAbsolute = [](const QString &path) {
            const QString canonical = QFileInfo(path).canonicalFilePath();
            return canonical.isEmpty() ? path : canonical;
        };
        const QString output = canonicalOrAbsolute(plan.paths.output);
        for (const QString &input : {plan.paths.base, plan.paths.left, plan.paths.right}) {
            if (input.isEmpty()) continue;
            const QString root = canonicalOrAbsolute(input);
            if (output == root || descendant(output, root) || descendant(root, output)) {
                plan.warnings << QObject::tr("输出与输入重叠；当前仅允许只读预演，不能原地写入。");
                break;
            }
        }
    }
    bool allComplete = true;
    int progressOffset = 0;
    const auto scan = [&](const QString &a, const QString &b) {
        Folder::Result result;
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            result.complete = false;
        } else {
            result = Folder::compare(a, b, options, cancelled,
                [&](int count, const QString &path) { if (progress) progress(progressOffset + count, path); }, fs);
        }
        progressOffset += result.entries.size();
        allComplete &= result.complete && result.error.isEmpty() && result.excludedCount == 0;
        plan.cancelled |= result.cancelled;
        if (!result.error.isEmpty() && !plan.error.contains(result.error)) {
            if (!plan.error.isEmpty()) plan.error += QLatin1Char('\n');
            plan.error += result.error;
        }
        for (const auto &warning : result.warnings)
            if (!warning.contains(QObject::tr("两侧是同一个文件夹"))) plan.warnings << warning;
        return indexResult(result);
    };
    // Self scans preserve descendants under a file/directory conflict: the
    // two-way Folder comparator deliberately stops at such a boundary.
    const Index left = scan(plan.paths.left, plan.paths.left);
    const Index right = scan(plan.paths.right, plan.paths.right);
    const Index base = plan.hasBase ? scan(plan.paths.base, plan.paths.base) : Index();
    const Index lr = scan(plan.paths.left, plan.paths.right);
    const Index bl = plan.hasBase ? scan(plan.paths.base, plan.paths.left) : Index();
    const Index br = plan.hasBase ? scan(plan.paths.base, plan.paths.right) : Index();
    QMap<QString, bool> pathsSeen;
    for (const auto *index : {&base, &left, &right, &bl, &br, &lr})
        for (auto it = index->cbegin(); it != index->cend(); ++it) pathsSeen.insert(it.key(), true);
    for (auto it = pathsSeen.cbegin(); it != pathsSeen.cend(); ++it) {
        if (!validPath(it.key())) {
            allComplete = false;
            plan.error = QObject::tr("扫描包含不安全的相对路径。");
            continue;
        }
        Entry entry;
        entry.relativePath = it.key();
        if (base.contains(it.key())) entry.base = base.value(it.key()).left;
        if (left.contains(it.key())) entry.left = left.value(it.key()).left;
        if (right.contains(it.key())) entry.right = right.value(it.key()).left;
        const auto stablePair = [&](const Index &index, const Side &a, const Side &b) {
            const auto found = index.constFind(it.key());
            if (found != index.cend())
                return sameSnapshot(found->left, a) && sameSnapshot(found->right, b);
            if (!a.exists() && !b.exists()) return true;
            // Only an explicit type boundary explains a missing row. Otherwise
            // an item disappeared between its inventory and pair comparison.
            QString ancestor = it.key();
            while (ancestor.contains(QLatin1Char('/'))) {
                ancestor = ancestor.left(ancestor.lastIndexOf(QLatin1Char('/')));
                const auto parent = index.constFind(ancestor);
                if (parent != index.cend() && parent->status == Status::TypeConflict) return true;
            }
            return false;
        };
        if (!stablePair(lr, entry.left, entry.right)
            || (plan.hasBase && (!stablePair(bl, entry.base, entry.left) || !stablePair(br, entry.base, entry.right)))) {
            allComplete = false;
            plan.warnings << QObject::tr("多轮扫描中的条目状态发生变化：%1。请重新扫描。").arg(entry.relativePath);
        }
        plan.entries.append(entry);
    }
    // Pair comparisons stop at type conflicts. Check every inventoried source
    // again so edits inside such a subtree cannot keep a stale plan complete.
    // Root metadata additionally detects late top-level additions/removals.
    const auto stillCurrent = [fs](const Files::FileInfo &before) {
        Files::ErrorCode error;
        const auto now = fs->stat(before.path, &error);
        return error.ok() && before.exists && now.exists
            && before.isDirectory == now.isDirectory && before.isSymLink == now.isSymLink
            && before.size == now.size && before.lastModified == now.lastModified;
    };
    if (allComplete) {
        for (const auto &entry : plan.entries) {
            for (const Side *side : {&entry.base, &entry.left, &entry.right}) {
                if (side->exists() && !stillCurrent(side->info)) {
                    allComplete = false;
                    plan.warnings << QObject::tr("扫描后来源已变化：%1。请重新扫描。").arg(side->info.path);
                }
            }
            if (cancelled && cancelled->load(std::memory_order_relaxed)) {
                plan.cancelled = true;
                allComplete = false;
                break;
            }
        }
        for (auto it = rootSnapshots.cbegin(); it != rootSnapshots.cend(); ++it) {
            if (!stillCurrent(it.value())) {
                allComplete = false;
                plan.warnings << QObject::tr("扫描后根目录已变化：%1。请重新扫描。").arg(it.key());
            }
        }
    }
    plan.complete = allComplete && !plan.cancelled;
    if (!plan.complete) plan.warnings << QObject::tr("扫描不完整：任何删除决策均已禁止。过滤/深度限制之外的缺失不能视为删除。");
    plan.warnings.removeDuplicates();
    for (auto &entry : plan.entries) {
        classify(entry, plan, bl, br, lr);
        entry.automaticDecision = entry.decision;
    }
    refresh(plan);
    return plan;
}

bool setDecision(Plan &plan, const QString &relativePath, Decision decision, DirectoryPolicy policy, QString *error)
{
    if (error) error->clear();
    const Entry *target = plan.find(relativePath);
    if (!target) return fail(error, QObject::tr("计划中没有该条目。"));
    if (decision == Decision::Unresolved) return resetDecision(plan, relativePath, error);
    if (decision == Decision::TakeBase && !plan.hasBase)
        return fail(error, QObject::tr("没有祖先，无法接受祖先。"));
    if (decision != Decision::TakeLeft && decision != Decision::TakeRight && decision != Decision::TakeBase
        && decision != Decision::Delete && decision != Decision::Ignore)
        return fail(error, QObject::tr("无效的合并决策。"));
    Plan candidate = plan;
    for (auto &entry : candidate.entries) {
        if (entry.relativePath != relativePath && !(target->isDirectory() && descendant(entry.relativePath, relativePath))) continue;
        Decision applied = decision;
        const Side *source = chosen(entry, applied);
        if (source && !source->exists()) applied = Decision::Delete;
        if (entry.relativePath != relativePath && entry.manual && entry.decision != applied) {
            if (policy == DirectoryPolicy::RejectManualConflicts)
                return fail(error, QObject::tr("子项已有不同的人工决策：%1。请选择保留或覆盖逐项决策。").arg(entry.relativePath));
            if (policy == DirectoryPolicy::PreserveManual) continue;
        }
        if (applied == Decision::Delete && !plan.complete)
            return fail(error, QObject::tr("扫描不完整，禁止删除或接受缺失的一侧；请完成扫描后重试。"));
        if (source && source->exists() && (!source->error.isEmpty() || source->kind == Kind::Other))
            return fail(error, QObject::tr("来源条目不可读取或类型未支持：%1。").arg(entry.relativePath));
        entry.decision = applied;
        entry.manual = true;
    }
    refresh(candidate);
    // Reject impossible explicit choices, including preserving a child under a
    // parent replaced with a file. Unresolved structural ancestors must be
    // decided first instead of silently allowing a disconnected output path.
    for (const auto &entry : candidate.entries) {
        if (entry.manual && entry.blockedByAncestor && entry.decision != Decision::Ignore && entry.decision != Decision::Delete)
            return fail(error, QObject::tr("条目 %1 的父目录尚未决策或不会输出为目录；请先处理父项。保留子项与父项决策不兼容时不能应用目录决策。")
                                  .arg(entry.relativePath));
    }
    plan = candidate;
    return true;
}

bool resetDecision(Plan &plan, const QString &relativePath, QString *error)
{
    if (error) error->clear();
    const Entry *target = plan.find(relativePath);
    if (!target) return fail(error, QObject::tr("计划中没有该条目。"));
    const bool subtree = target->isDirectory();
    for (auto &entry : plan.entries)
        if (entry.relativePath == relativePath || (subtree && descendant(entry.relativePath, relativePath))) {
            entry.decision = entry.automaticDecision;
            entry.manual = false;
        }
    refresh(plan);
    return true;
}

Scanner::Scanner(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<LqCompare::FolderMerge::Plan>();
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

bool Scanner::start(const Paths &paths, const Folder::Options &options)
{
    if (isRunning()) return false;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    const auto flag = m_cancelled;
    const auto plan = std::make_shared<Plan>();
    m_thread = QThread::create([this, paths, options, flag, plan] {
        QElapsedTimer elapsed;
        elapsed.start();
        *plan = buildPlan(paths, options, flag.get(), [this, &elapsed](int count, const QString &path) {
            if (count == 1 || elapsed.elapsed() >= 75) {
                emit progressChanged(count, path);
                elapsed.restart();
            }
        });
    });
    connect(m_thread, &QThread::finished, this, [this, plan] {
        QThread *completed = m_thread;
        m_thread = nullptr;
        completed->deleteLater();
        emit finished(*plan);
    });
    m_thread->start();
    return true;
}

void Scanner::cancel()
{
    if (m_cancelled) m_cancelled->store(true, std::memory_order_relaxed);
}

} // namespace FolderMerge
} // namespace LqCompare
