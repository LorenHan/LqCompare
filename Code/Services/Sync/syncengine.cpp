#include "syncengine.h"
#include "syncbaseline.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace LqCompare { namespace Sync {
namespace {
bool stopped(const std::atomic_bool *flag) { return flag && flag->load(std::memory_order_relaxed); }
QString absolute(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool beneath(const QString &path, const QString &parent) {
    const auto cs = Qt::CaseInsensitive; // Conservative on case-sensitive volumes too.
    const auto prefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return path.compare(parent, cs) == 0 || path.startsWith(prefix, cs);
}
bool validRelative(const QString &path) {
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char('\\'))
        || path.contains(QLatin1Char(':')) || path.contains(QChar::Null)) return false;
    for (const auto c : path) if (c.unicode() < 32 || c.unicode() == 127) return false;
    const auto parts = path.split(QLatin1Char('/'));
    for (const auto &part : parts)
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) return false;
    return true;
}
bool excluded(const QString &path, const Options &options) {
    for (const auto &prefix : options.excludedPaths)
        if (beneath(path, prefix)) return true;
    return false;
}
bool containsExcluded(const QString &path, const Options &options) {
    for (const auto &prefix : options.excludedPaths)
        if (beneath(prefix, path)) return true;
    return false;
}
QString pathFor(const Plan &plan, const Item &item, bool left) {
    return QDir(left ? plan.leftRoot : plan.rightRoot).filePath(item.relativePath);
}
bool writesLeft(Action action) {
    return action == Action::CopyRightToLeft || action == Action::CreateLeftDirectory || action == Action::DeleteLeft;
}
bool copying(Action action) { return action == Action::CopyLeftToRight || action == Action::CopyRightToLeft; }
bool deleting(Action action) { return action == Action::DeleteLeft || action == Action::DeleteRight; }
bool makingDirectory(Action action) {
    return action == Action::CreateLeftDirectory || action == Action::CreateRightDirectory;
}

Fingerprint fingerprint(const QString &path, const Files::FileSystem &fs, const std::atomic_bool *cancelled) {
    Fingerprint out;
    Files::ErrorCode error;
    const auto before = fs.stat(path, &error);
    if (!before.exists) {
        if (error != Files::FileSystemError::NotFound && !error.ok()) out.error = Files::errorReport(error, path);
        return out;
    }
    if (!error.ok()) { out.error = Files::errorReport(error, path); return out; }
    out.kind = before.isSymLink ? Folder::Kind::SymbolicLink : before.isDirectory ? Folder::Kind::Directory
                    : QFileInfo(path).isFile() ? Folder::Kind::File : Folder::Kind::Other;
    out.size = before.size;
    out.modifiedNs = before.lastModified.nanosecondsSinceEpoch();
    out.createdNs = before.created.isValid() ? before.created.nanosecondsSinceEpoch() : 0;
    out.attributes = int(before.attributes);
    if (out.kind == Folder::Kind::File) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { out.error = file.errorString(); return out; }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd()) {
            if (stopped(cancelled)) { out.error = QStringLiteral("操作已取消"); return out; }
            const auto bytes = file.read(256 * 1024);
            if (file.error() != QFileDevice::NoError) { out.error = file.errorString(); return out; }
            hash.addData(bytes);
        }
        out.sha256 = hash.result().toHex();
        const auto after = fs.stat(path, &error);
        if (!error.ok() || !after.exists || after.isSymLink || after.isDirectory || before.size != after.size
            || before.lastModified != after.lastModified || before.created != after.created)
            out.error = QStringLiteral("读取期间文件发生变化，请重新预演");
    }
    return out;
}
bool sameFingerprint(const Fingerprint &a, const Fingerprint &b, bool directoryMayChange = false) {
    if (!a.error.isEmpty() || !b.error.isEmpty() || a.kind != b.kind) return false;
    if (!a.exists()) return true;
    if (a.createdNs != b.createdNs || a.attributes != b.attributes) return false;
    if (a.kind == Folder::Kind::Directory && directoryMayChange) return true;
    return a.size == b.size && a.modifiedNs == b.modifiedNs && a.sha256 == b.sha256;
}
bool sameContent(const Fingerprint &a, const Fingerprint &b) {
    if (a.kind != b.kind || !a.error.isEmpty() || !b.error.isEmpty()) return false;
    if (!a.exists() || a.kind == Folder::Kind::Directory) return true;
    return a.kind == Folder::Kind::File && !a.sha256.isEmpty() && a.sha256 == b.sha256 && a.size == b.size;
}
bool matchesScanned(const Fingerprint &now, const Folder::Side &side) {
    if (!now.error.isEmpty() || now.kind != side.kind) return false;
    return !now.exists() || (now.size == side.info.size
        && now.modifiedNs == side.info.lastModified.nanosecondsSinceEpoch()
        && now.attributes == int(side.info.attributes));
}
QString safePath(const QString &root, const QString &relative, const Files::FileSystem &fs) {
    if (!validRelative(relative)) return QStringLiteral("相对路径不安全");
    QString current = root;
    const auto parts = relative.split(QLatin1Char('/'));
    for (int i = 0; i < parts.size(); ++i) {
        current = QDir(current).filePath(parts.at(i));
        Files::ErrorCode error;
        auto info = fs.stat(current, &error);
        if (info.isSymLink) return QStringLiteral("路径包含符号链接，必须重新预演：%1").arg(current);
        if (!error.ok() && error != Files::FileSystemError::NotFound) return Files::errorReport(error, current);
        if (i + 1 < parts.size() && info.exists && !info.isDirectory)
            return QStringLiteral("父路径不再是目录：%1").arg(current);
    }
    return {};
}
void assign(Item &item, Action action, const char *code, const QString &reason) {
    item.action = action; item.reasonCode = QString::fromLatin1(code); item.reason = reason;
    item.selected = isActionable(action);
}
void copyTo(Item &item, bool toLeft, const char *code, const QString &reason) {
    const auto &source = toLeft ? item.right : item.left;
    assign(item, source.kind == Folder::Kind::Directory
           ? (toLeft ? Action::CreateLeftDirectory : Action::CreateRightDirectory)
           : (toLeft ? Action::CopyRightToLeft : Action::CopyLeftToRight), code, reason);
}
void removeFrom(Item &item, bool left, const Options &options, const char *code, const QString &reason) {
    if (options.deletion == Deletion::Keep || containsExcluded(item.relativePath, options))
        assign(item, Action::Skip, "deletion-protected", QStringLiteral("删除保护：保留目标条目或范围内含排除项"));
    else assign(item, left ? Action::DeleteLeft : Action::DeleteRight, code, reason);
}
void streamFingerprint(QDataStream &stream, const Fingerprint &f) {
    stream << int(f.kind) << f.size << f.modifiedNs << f.createdNs << f.attributes << f.sha256 << f.error;
}
bool atomicCopy(const QString &source, const QString &target, const Fingerprint &expected,
                const std::atomic_bool *cancelled, QString *error, const Fingerprint *targetBefore = nullptr,
                const std::function<bool()> &pathGuard = {}) {
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) { *error = input.errorString(); return false; }
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) { *error = output.errorString(); return false; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        if (stopped(cancelled)) { *error = QStringLiteral("操作已取消，目标保持原状"); return false; }
        const auto data = input.read(256 * 1024);
        if (input.error() != QFileDevice::NoError || output.write(data) != data.size()) {
            *error = input.error() != QFileDevice::NoError ? input.errorString() : output.errorString(); return false;
        }
        hash.addData(data);
    }
    if (hash.result().toHex() != expected.sha256) {
        *error = QStringLiteral("复制期间源内容改变，目标保持原状，请重新预演"); return false;
    }
    if (!output.setPermissions(input.permissions())) { *error = output.errorString(); return false; }
    if (stopped(cancelled)) { *error = QStringLiteral("操作已取消，目标保持原状"); return false; }
    if (pathGuard && !pathGuard()) { *error = QStringLiteral("写入前目录路径发生变化，已取消写入；请重新预演"); return false; }
    if (targetBefore) {
        std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
        if (!sameFingerprint(*targetBefore, fingerprint(target, *fs, cancelled))) {
            *error = QStringLiteral("写入前目标发生变化，保留目标并要求重新预演"); return false;
        }
    }
    if (!output.commit()) { *error = output.errorString(); return false; }
    return true;
}
bool saveJournal(const QString &path, const Plan &plan, const QVector<ItemResult> &results, QString *error) {
    QJsonArray entries;
    for (const auto &result : results) {
        const auto &original = writesLeft(result.item.action) ? result.item.left : result.item.right;
        QJsonObject entry{{QStringLiteral("path"), result.item.relativePath},
            {QStringLiteral("action"), actionLabel(result.item.action)},
            {QStringLiteral("outcome"), int(result.outcome)}, {QStringLiteral("message"), result.message},
            {QStringLiteral("target"), result.targetPath}, {QStringLiteral("backup"), result.backupPath},
            {QStringLiteral("trash"), result.trashedPath}, {QStringLiteral("originalSha256"), QString::fromLatin1(original.sha256)},
            {QStringLiteral("resultSha256"), QString::fromLatin1(result.resultingTarget.sha256)}};
        entries << entry;
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("LqCompare.SyncJournal")},
        {QStringLiteral("version"), 1}, {QStringLiteral("planId"), plan.id},
        {QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("plan"), exportPlanText(plan)}, {QStringLiteral("mode"), int(plan.options.mode)},
        {QStringLiteral("direction"), int(plan.options.direction)},
        {QStringLiteral("confirmationDigest"), QString::fromLatin1(confirmationDigest(plan))},
        {QStringLiteral("results"), entries}};
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { *error = file.errorString(); return false; }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) { *error = file.errorString(); return false; }
    return true;
}
}

QString actionLabel(Action action) {
    switch (action) {
    case Action::Skip: return QStringLiteral("跳过");
    case Action::Conflict: return QStringLiteral("冲突（不执行）");
    case Action::CopyLeftToRight: return QStringLiteral("复制 / 覆盖 →");
    case Action::CopyRightToLeft: return QStringLiteral("← 复制 / 覆盖");
    case Action::CreateLeftDirectory: return QStringLiteral("← 新建目录");
    case Action::CreateRightDirectory: return QStringLiteral("新建目录 →");
    case Action::DeleteLeft: return QStringLiteral("左侧删除到回收站");
    case Action::DeleteRight: return QStringLiteral("右侧删除到回收站");
    }
    return {};
}
bool isActionable(Action action) { return action != Action::Skip && action != Action::Conflict; }
QByteArray scopeKey(const Options &options) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_5_15);
    auto excludedPaths = options.excludedPaths; excludedPaths.sort();
    stream << int(options.mode) << int(options.direction) << int(options.deletion) << excludedPaths;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

Plan makePlan(const Folder::Result &scan, const Options &options, const Baseline *baseline,
              const std::atomic_bool *cancelled) {
    Plan plan; plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces); plan.options = options;
    plan.leftRoot = absolute(scan.leftRoot); plan.rightRoot = absolute(scan.rightRoot); plan.warnings = scan.warnings;
    if (scan.leftRoot.isEmpty() || scan.rightRoot.isEmpty() || !scan.complete || scan.cancelled || !scan.error.isEmpty()) {
        plan.error = QStringLiteral("扫描不完整或已取消；禁止执行，请重新预演。%1").arg(scan.error); return plan;
    }
    if (!scan.scanMaskDeclaration.trimmed().isEmpty() || scan.excludedCount > 0) {
        plan.error = QStringLiteral("此扫描已按掩码排除条目，不能直接转换为同步计划；请使用同步的范围排除选项重新预演。"); return plan;
    }
    if (options.deleteCountThreshold < 0) { plan.error = QStringLiteral("删除数量阈值不可为负数"); return plan; }
    if ((options.mode != Mode::Update && options.mode != Mode::Mirror && options.mode != Mode::TwoWay)
        || (options.direction != Direction::LeftToRight && options.direction != Direction::RightToLeft)
        || (options.deletion != Deletion::Keep && options.deletion != Deletion::Trash)) {
        plan.error = QStringLiteral("同步模式、方向或删除策略无效"); return plan;
    }
    if (options.mode == Mode::Mirror && options.deletion == Deletion::Keep) {
        plan.error = QStringLiteral("镜像与“保留目标多余条目”矛盾；请改为更新模式，或允许移到回收站。"); return plan;
    }
    for (const auto &path : options.excludedPaths)
        if (!validRelative(path)) { plan.error = QStringLiteral("排除范围必须为安全的相对路径：%1").arg(path); return plan; }
    std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
    plan.leftRootIdentity = fingerprint(plan.leftRoot, *fs, cancelled);
    plan.rightRootIdentity = fingerprint(plan.rightRoot, *fs, cancelled);
    const auto leftCanonical = QFileInfo(plan.leftRoot).canonicalFilePath();
    const auto rightCanonical = QFileInfo(plan.rightRoot).canonicalFilePath();
    if (plan.leftRootIdentity.kind != Folder::Kind::Directory || plan.rightRootIdentity.kind != Folder::Kind::Directory
        || leftCanonical.isEmpty() || rightCanonical.isEmpty()
        || beneath(leftCanonical, rightCanonical) || beneath(rightCanonical, leftCanonical)) {
        plan.error = QStringLiteral("同步需要两个独立的本地目录；相同目录、父子目录或符号链接根目录不能执行。"); return plan;
    }
    plan.baselineUsed = baseline && baseline->complete && baseline->leftRoot == plan.leftRoot
        && baseline->rightRoot == plan.rightRoot && baseline->scopeKey == scopeKey(options)
        && validateBaseline(*baseline).isEmpty();
    if (options.mode == Mode::TwoWay && !plan.baselineUsed)
        plan.warnings << QStringLiteral("当前无有效基线，使用保守模式：两侧内容不同即冲突，不能据此声称两侧均改。");
    if (!options.excludedPaths.isEmpty()) plan.warnings << QStringLiteral("已按排除条件限定范围；排除项在两侧均受删除保护。");
    QSet<QString> names;
    QStringList blockedParents;
    auto entries = scan.entries;
    std::sort(entries.begin(), entries.end(), [](const Folder::Entry &a, const Folder::Entry &b) { return a.relativePath < b.relativePath; });
    for (const auto &entry : entries) {
        if (stopped(cancelled)) { plan.error = QStringLiteral("预演已取消"); return plan; }
        if (!validRelative(entry.relativePath)) { plan.error = QStringLiteral("扫描包含不安全路径"); return plan; }
        if (entry.excludedByMask) {
            plan.error = QStringLiteral("扫描包含被范围排除的条目，不能直接生成同步计划；请重新预演。"); return plan;
        }
        if (entry.status == Folder::Status::Error || entry.status == Folder::Status::Unknown) {
            plan.error = QStringLiteral("存在未知或读取失败的条目，必须完整比较后再预演"); return plan;
        }
        const QString normalized = entry.relativePath.normalized(QString::NormalizationForm_C).toCaseFolded();
        if (names.contains(normalized)) { plan.error = QStringLiteral("名称大小写或 Unicode 规范形式冲突：%1").arg(entry.relativePath); return plan; }
        names.insert(normalized);
        Item item; item.relativePath = entry.relativePath;
        bool parentBlocked = false;
        for (const auto &path : blockedParents) if (beneath(item.relativePath, path)) parentBlocked = true;
        if (excluded(item.relativePath, options)) {
            assign(item, Action::Skip, "excluded", QStringLiteral("范围排除：两侧均不复制、不删除")); plan.items << item; continue;
        }
        if (parentBlocked) {
            assign(item, Action::Conflict, "parent-conflict", QStringLiteral("父路径存在类型冲突，子项不执行")); plan.items << item; continue;
        }
        const auto leftSafe = safePath(plan.leftRoot, item.relativePath, *fs);
        const auto rightSafe = safePath(plan.rightRoot, item.relativePath, *fs);
        item.left = fingerprint(pathFor(plan, item, true), *fs, cancelled);
        item.right = fingerprint(pathFor(plan, item, false), *fs, cancelled);
        if (!matchesScanned(item.left, entry.left) || !matchesScanned(item.right, entry.right)) {
            plan.error = QStringLiteral("扫描后条目变化或读取失败：%1，请重新预演").arg(item.relativePath); return plan;
        }
        const bool unsupported = item.left.kind == Folder::Kind::SymbolicLink || item.right.kind == Folder::Kind::SymbolicLink
            || item.left.kind == Folder::Kind::Other || item.right.kind == Folder::Kind::Other;
        if (unsupported || entry.nameCaseDifference || !leftSafe.isEmpty() || !rightSafe.isEmpty()
            || (item.left.exists() && item.right.exists() && item.left.kind != item.right.kind)) {
            assign(item, Action::Conflict, "unsupported-type", QStringLiteral("类型冲突、符号链接或特殊文件：需手动处理，不跟随链接"));
            blockedParents << item.relativePath;
        } else if (sameContent(item.left, item.right)) {
            assign(item, Action::Skip, "equal", QStringLiteral("内容相同，无需同步"));
        } else if (options.mode != Mode::TwoWay) {
            const bool toLeft = options.direction == Direction::RightToLeft;
            const auto &source = toLeft ? item.right : item.left;
            const auto &target = toLeft ? item.left : item.right;
            if (!source.exists()) {
                if (options.mode == Mode::Mirror) removeFrom(item, toLeft, options, "mirror-extra", QStringLiteral("镜像：目标侧多余条目，移到回收站"));
                else assign(item, Action::Skip, "update-keep-extra", QStringLiteral("更新：保留目标侧独有条目"));
            } else if (!target.exists()) copyTo(item, toLeft, "source-only", QStringLiteral("源侧独有条目"));
            else if (options.mode == Mode::Mirror) copyTo(item, toLeft, "mirror-different", QStringLiteral("镜像：以源侧内容覆盖目标；先备份目标"));
            else if (source.modifiedNs > target.modifiedNs) copyTo(item, toLeft, "source-newer", QStringLiteral("更新：源侧较新；先备份目标"));
            else assign(item, Action::Skip, "source-not-newer", QStringLiteral("更新：源侧不比目标新，保留目标"));
        } else if (!plan.baselineUsed) {
            if (!item.left.exists()) copyTo(item, true, "right-only-no-baseline", QStringLiteral("无基线：右侧独有，复制到左侧（不推断删除）"));
            else if (!item.right.exists()) copyTo(item, false, "left-only-no-baseline", QStringLiteral("无基线：左侧独有，复制到右侧（不推断删除）"));
            else assign(item, Action::Conflict, "different-without-baseline", QStringLiteral("无基线：两侧内容不同，无法判断哪侧改动；请手动决定"));
        } else {
            const auto prior = baseline->entries.value(item.relativePath);
            const bool priorUsable = prior.kind == Folder::Kind::Missing || prior.kind == Folder::Kind::Directory
                || (prior.kind == Folder::Kind::File && prior.sha256.size() == 64);
            const bool leftChanged = !sameContent(item.left, prior), rightChanged = !sameContent(item.right, prior);
            if (!priorUsable) assign(item, Action::Conflict, "baseline-content-unknown", QStringLiteral("基线缺少内容摘要，不能推断变化方向"));
            else if (leftChanged && rightChanged) assign(item, Action::Conflict, "both-changed", QStringLiteral("两侧相对有效基线均已变化（包括删除与修改冲突），不自动覆盖"));
            else if (leftChanged) {
                if (item.left.exists()) copyTo(item, false, "left-changed", QStringLiteral("左侧相对基线已修改"));
                else removeFrom(item, false, options, "left-deleted", QStringLiteral("左侧相对基线已删除，右侧未变；右侧移到回收站"));
            } else if (rightChanged) {
                if (item.right.exists()) copyTo(item, true, "right-changed", QStringLiteral("右侧相对基线已修改"));
                else removeFrom(item, true, options, "right-deleted", QStringLiteral("右侧相对基线已删除，左侧未变；左侧移到回收站"));
            } else assign(item, Action::Conflict, "baseline-inconsistent", QStringLiteral("基线状态无法解释当前差异，请重建基线"));
        }
        plan.items << item;
    }
    plan.complete = true;
    return plan;
}

Plan preview(const QString &leftRoot, const QString &rightRoot, const Options &options,
             const Baseline *baseline, const std::atomic_bool *cancelled, const Folder::Progress &progress) {
    return makePlan(Folder::compare(leftRoot, rightRoot, {}, cancelled, progress), options, baseline, cancelled);
}

Summary summarize(const Plan &plan) {
    Summary out;
    for (const auto &item : plan.items) {
        if (item.action == Action::Conflict) { ++out.conflicts; continue; }
        if (!item.selected || !isActionable(item.action)) { ++out.skipped; continue; }
        if (copying(item.action)) { ++out.copies; out.copyBytes += writesLeft(item.action) ? item.right.size : item.left.size; }
        else if (makingDirectory(item.action)) ++out.directories;
        else { ++out.deletions; const auto &target = writesLeft(item.action) ? item.left : item.right;
            if (target.kind == Folder::Kind::File) out.deleteBytes += target.size; }
    }
    out.needsDeleteConfirmation = out.deletions > plan.options.deleteCountThreshold || out.deleteBytes > plan.options.deleteBytesThreshold;
    return out;
}
QString Summary::text() const {
    return QStringLiteral("复制/覆盖 %1 个文件（%2 字节），新建 %3 个目录，删除到回收站 %4 个条目（%5 字节），冲突 %6 项，跳过 %7 项。")
        .arg(copies).arg(copyBytes).arg(directories).arg(deletions).arg(deleteBytes).arg(conflicts).arg(skipped);
}
QByteArray confirmationDigest(const Plan &plan) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_5_15);
    stream << plan.id << plan.leftRoot << plan.rightRoot << scopeKey(plan.options)
           << plan.options.deleteCountThreshold << plan.options.deleteBytesThreshold << plan.complete << plan.error;
    streamFingerprint(stream, plan.leftRootIdentity); streamFingerprint(stream, plan.rightRootIdentity);
    for (const auto &item : plan.items) {
        stream << item.relativePath << int(item.action) << item.selected << item.reasonCode << item.reason;
        streamFingerprint(stream, item.left); streamFingerprint(stream, item.right);
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
QString exportPlanText(const Plan &plan) {
    QString out = QStringLiteral("左：%1\n右：%2\n%3\n%4\n").arg(plan.leftRoot, plan.rightRoot, summarize(plan).text(), plan.warnings.join(QLatin1Char('\n')));
    for (const auto &item : plan.items)
        out += QStringLiteral("%1\t%2\t%3\t%4\n").arg(item.selected ? QStringLiteral("[x]") : QStringLiteral("[ ]"), actionLabel(item.action), item.relativePath, item.reason);
    return out;
}

Executor::Executor(const QString &backupRoot, Files::TrashService *trash) : m_backupRoot(backupRoot), m_trash(trash) {
    if (m_backupRoot.isEmpty()) m_backupRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("sync-backups"));
    if (!m_trash) { m_ownedTrash.reset(Files::createNativeTrashService()); m_trash = m_ownedTrash.get(); }
}
Executor::~Executor() = default;

Report Executor::execute(const Plan &plan, const Confirmation &confirmation,
                         const std::atomic_bool *cancelled, const Progress &progress) {
    Report report;
    if (!plan.executable() || !confirmation.userConfirmed || confirmation.planDigest != confirmationDigest(plan)) {
        report.error = QStringLiteral("必须先完整预演并明确确认当前勾选计划；计划更改后需重新确认。"); return report;
    }
    if (summarize(plan).needsDeleteConfirmation && !confirmation.largeDeleteConfirmed) {
        report.error = QStringLiteral("删除超出数量或体积阈值，需要额外确认。"); return report;
    }
    auto cancelledReport = [&] {
        report.cancelled = true;
        report.error = QStringLiteral("执行前校验已取消，未写入文件。");
        for (const auto &item : plan.items) {
            ItemResult result; result.item = item;
            result.outcome = item.selected && isActionable(item.action) ? Outcome::Cancelled : Outcome::Skipped;
            result.message = result.outcome == Outcome::Cancelled ? QStringLiteral("取消后未执行") : QStringLiteral("按预演清单跳过");
            report.items << result;
        }
        return report;
    };
    if (stopped(cancelled)) return cancelledReport();
    std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
    auto rootsValid = [&](bool changed) {
        return sameFingerprint(plan.leftRootIdentity, fingerprint(plan.leftRoot, *fs, cancelled), changed)
            && sameFingerprint(plan.rightRootIdentity, fingerprint(plan.rightRoot, *fs, cancelled), changed);
    };
    if (!rootsValid(false)) {
        if (stopped(cancelled)) return cancelledReport();
        report.error = QStringLiteral("目录在预演后发生变化，请重新预演。"); return report;
    }
    QMap<QString, Fingerprint> leftDirectories, rightDirectories;
    for (const auto &item : plan.items) {
        if (item.left.kind == Folder::Kind::Directory) leftDirectories.insert(item.relativePath, item.left);
        if (item.right.kind == Folder::Kind::Directory) rightDirectories.insert(item.relativePath, item.right);
    }
    auto parentsValid = [&](const Item &item, bool changed) {
        QString parent = item.relativePath;
        while (parent.contains(QLatin1Char('/'))) {
            parent = parent.left(parent.lastIndexOf(QLatin1Char('/')));
            if (leftDirectories.contains(parent)
                && !sameFingerprint(leftDirectories.value(parent), fingerprint(QDir(plan.leftRoot).filePath(parent), *fs, cancelled), changed)) return false;
            if (rightDirectories.contains(parent)
                && !sameFingerprint(rightDirectories.value(parent), fingerprint(QDir(plan.rightRoot).filePath(parent), *fs, cancelled), changed)) return false;
        }
        return true;
    };
    // Validate all selected items BEFORE the first write. No stale-plan partial overwrite.
    for (const auto &item : plan.items) {
        if (!item.selected || !isActionable(item.action)) continue;
        const auto leftError = safePath(plan.leftRoot, item.relativePath, *fs);
        const auto rightError = safePath(plan.rightRoot, item.relativePath, *fs);
        if (!leftError.isEmpty() || !rightError.isEmpty() || !parentsValid(item, false)
            || !sameFingerprint(item.left, fingerprint(pathFor(plan, item, true), *fs, cancelled))
            || !sameFingerprint(item.right, fingerprint(pathFor(plan, item, false), *fs, cancelled))) {
            if (stopped(cancelled)) return cancelledReport();
            report.error = QStringLiteral("预演已失效，源或目标发生变化：%1；请重新预演。").arg(item.relativePath); return report;
        }
    }
    QString backupRoot = absolute(m_backupRoot);
    // Resolve the nearest existing ancestor too: a symlink must not place backups inside a source tree.
    QString existing = backupRoot;
    while (!QFileInfo::exists(existing) && QFileInfo(existing).dir().absolutePath() != existing)
        existing = QFileInfo(existing).dir().absolutePath();
    const QString canonicalBackup = QDir(QFileInfo(existing).canonicalFilePath()).filePath(QDir(existing).relativeFilePath(backupRoot));
    if (beneath(QDir::cleanPath(canonicalBackup), QFileInfo(plan.leftRoot).canonicalFilePath())
        || beneath(QDir::cleanPath(canonicalBackup), QFileInfo(plan.rightRoot).canonicalFilePath())) {
        report.error = QStringLiteral("备份目录必须位于两个同步目录之外。"); return report;
    }
    report.backupDirectory = QDir(backupRoot).filePath(plan.id);
    report.journalPath = QDir(report.backupDirectory).filePath(QStringLiteral("journal.json"));
    if (!QDir().mkpath(report.backupDirectory) || QFileInfo::exists(report.journalPath)) {
        report.error = QStringLiteral("无法创建独立恢复记录，或该计划已经执行过；请重新预演。"); return report;
    }
    QString journalError;
    if (!saveJournal(report.journalPath, plan, {}, &journalError)) {
        report.error = QStringLiteral("不能写入恢复记录，未执行：%1").arg(journalError); return report;
    }
    QVector<int> order;
    for (int i = 0; i < plan.items.size(); ++i) order << i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto &x = plan.items.at(a), &y = plan.items.at(b);
        const int xr = deleting(x.action) ? 2 : makingDirectory(x.action) ? 0 : 1;
        const int yr = deleting(y.action) ? 2 : makingDirectory(y.action) ? 0 : 1;
        if (xr != yr) return xr < yr;
        const int xd = x.relativePath.count(QLatin1Char('/')), yd = y.relativePath.count(QLatin1Char('/'));
        return xr == 2 ? xd > yd : xd < yd;
    });
    bool allSelected = true;
    bool journalFailed = false;
    for (int index : order) {
        const auto &item = plan.items.at(index);
        ItemResult result; result.item = item;
        if (!item.selected || !isActionable(item.action)) {
            result.message = item.action == Action::Conflict ? QStringLiteral("未解决冲突，保留两侧")
                : item.selected ? item.reason : QStringLiteral("按预演清单跳过");
            if (isActionable(item.action) || item.action == Action::Conflict || item.reasonCode != QLatin1String("equal")) allSelected = false;
        } else if (stopped(cancelled) || journalFailed) {
            result.outcome = Outcome::Cancelled; result.message = QStringLiteral("取消后未执行"); report.cancelled = true; allSelected = false;
        } else {
            const bool toLeft = writesLeft(item.action);
            const QString target = pathFor(plan, item, toLeft), source = pathFor(plan, item, !toLeft);
            result.targetPath = target;
            result.targetRoot = toLeft ? plan.leftRoot : plan.rightRoot;
            result.targetRootIdentity = toLeft ? plan.leftRootIdentity : plan.rightRootIdentity;
            const auto &expectedTarget = toLeft ? item.left : item.right;
            const auto &expectedSource = toLeft ? item.right : item.left;
            QString error = safePath(toLeft ? plan.leftRoot : plan.rightRoot, item.relativePath, *fs);
            if (error.isEmpty()) error = safePath(toLeft ? plan.rightRoot : plan.leftRoot, item.relativePath, *fs);
            if (error.isEmpty() && (!rootsValid(true) || !parentsValid(item, true)
                || !sameFingerprint(expectedTarget, fingerprint(target, *fs, cancelled), true)
                || !sameFingerprint(expectedSource, fingerprint(source, *fs, cancelled), true)))
                error = QStringLiteral("条目在执行前发生变化，跳过并要求重新预演");
            if (error.isEmpty() && copying(item.action)) {
                if (!QFileInfo(QFileInfo(target).dir().absolutePath()).isDir()) error = QStringLiteral("父目录未创建或已被取消勾选");
                if (error.isEmpty() && (expectedTarget.attributes & int(Files::FileAttribute::ReadOnly)))
                    error = QStringLiteral("目标为只读文件，保留目标；调整权限后重新预演");
                if (error.isEmpty() && expectedTarget.exists()) {
                    result.backupPath = QDir(report.backupDirectory).filePath((toLeft ? QStringLiteral("left/") : QStringLiteral("right/")) + item.relativePath);
                    if (!QDir().mkpath(QFileInfo(result.backupPath).dir().absolutePath())) error = QStringLiteral("无法创建备份目录");
                    else if (QFileInfo::exists(result.backupPath)) error = QStringLiteral("本次计划已有备份，必须重新预演，避免覆盖恢复点");
                    else if (!atomicCopy(target, result.backupPath, expectedTarget, cancelled, &error)) { /* original untouched */ }
                }
                // Persist the restore mapping before replacing any existing bytes.
                if (error.isEmpty() && !result.backupPath.isEmpty()) {
                    auto pending = report.items; result.message = QStringLiteral("覆盖前备份已完成，写入待执行"); pending << result;
                    if (!saveJournal(report.journalPath, plan, pending, &error)) journalFailed = true;
                }
                const auto guard = [&] {
                    return rootsValid(true) && parentsValid(item, true)
                        && safePath(result.targetRoot, item.relativePath, *fs).isEmpty();
                };
                if (error.isEmpty() && atomicCopy(source, target, expectedSource, cancelled, &error, &expectedTarget, guard)) {
                    Files::ErrorCode timeError;
                    if (!fs->setTimes(target, Files::FileTime::fromNanosecondsSinceEpoch(expectedSource.modifiedNs), {}, &timeError))
                        error = QStringLiteral("文件已复制，但修改时间未保留：%1").arg(Files::errorReport(timeError));
                    result.resultingTarget = fingerprint(target, *fs, cancelled);
                    if (error.isEmpty() && !sameContent(expectedSource, result.resultingTarget)) error = QStringLiteral("复制后 SHA-256 校验失败；原目标备份已保留");
                }
            } else if (error.isEmpty() && makingDirectory(item.action)) {
                if (!QDir().mkdir(target)) error = QStringLiteral("无法创建目录（父目录可能未勾选）");
                else {
                    result.resultingTarget = fingerprint(target, *fs, cancelled);
                    (toLeft ? leftDirectories : rightDirectories).insert(item.relativePath, result.resultingTarget);
                }
            } else if (error.isEmpty() && deleting(item.action)) {
                if (expectedTarget.kind == Folder::Kind::Directory
                    && !QDir(target).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
                    error = QStringLiteral("目录仍有未删除或范围外条目，保留目录；请重新预演");
                if (error.isEmpty()) {
                    const auto trashed = m_trash->deleteToTrash({target});
                    if (trashed.records.size() != 1) error = QStringLiteral("回收站未返回完整的删除结果");
                    else if (!trashed.succeeded()) error = Files::errorReport(trashed.firstErrorCode(), target);
                    else { result.trashedPath = trashed.records.first().trashedPath;
                        if (QFileInfo::exists(target)) error = QStringLiteral("回收站未移走目标，删除未完成"); }
                }
            }
            if (error.isEmpty()) {
                result.outcome = Outcome::Succeeded;
                result.message = deleting(item.action) ? QStringLiteral("已移到回收站：%1").arg(result.trashedPath)
                    : result.backupPath.isEmpty() ? QStringLiteral("完成") : QStringLiteral("完成；覆盖前备份：%1").arg(result.backupPath);
            } else {
                result.outcome = stopped(cancelled) ? Outcome::Cancelled : Outcome::Failed;
                result.message = error; allSelected = false;
                if (result.outcome == Outcome::Cancelled) report.cancelled = true;
            }
        }
        report.items << result;
        if (!saveJournal(report.journalPath, plan, report.items, &journalError)) {
            report.error = QStringLiteral("恢复记录写入失败，停止后续操作：%1；备份目录：%2").arg(journalError, report.backupDirectory);
            journalFailed = true; allSelected = false;
        }
        if (progress) progress(report.items.size(), plan.items.size(), item.relativePath);
    }
    report.baselineEligible = allSelected && !report.cancelled && report.failedCount() == 0;
    return report;
}

bool Executor::restoreBackup(const ItemResult &result, QString *errorOut) {
    QString error;
    if (result.backupPath.isEmpty() || !copying(result.item.action)) error = QStringLiteral("此条目没有覆盖前备份");
    std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
    if (error.isEmpty() && (result.targetRoot.isEmpty() || result.targetPath != QDir(result.targetRoot).filePath(result.item.relativePath)
        || !sameFingerprint(result.targetRootIdentity, fingerprint(result.targetRoot, *fs, nullptr), true)))
        error = QStringLiteral("恢复目标目录已变化或路径无效");
    if (error.isEmpty()) error = safePath(result.targetRoot, result.item.relativePath, *fs);
    const auto &original = writesLeft(result.item.action) ? result.item.left : result.item.right;
    if (error.isEmpty() && (!sameFingerprint(result.resultingTarget, fingerprint(result.targetPath, *fs, nullptr))
        || !sameContent(original, fingerprint(result.backupPath, *fs, nullptr))))
        error = QStringLiteral("同步后目标或备份已变化，禁止自动覆盖；请手动检查备份");
    const auto restoreGuard = [&] {
        return sameFingerprint(result.targetRootIdentity, fingerprint(result.targetRoot, *fs, nullptr), true)
            && safePath(result.targetRoot, result.item.relativePath, *fs).isEmpty();
    };
    if (error.isEmpty() && atomicCopy(result.backupPath, result.targetPath, original, nullptr, &error, &result.resultingTarget, restoreGuard)) {
        Files::ErrorCode code;
        if (!fs->setTimes(result.targetPath, Files::FileTime::fromNanosecondsSinceEpoch(original.modifiedNs), {}, &code))
            error = QStringLiteral("内容已还原，但恢复修改时间失败：%1").arg(Files::errorReport(code));
    }
    if (errorOut) *errorOut = error;
    return error.isEmpty();
}
bool Executor::undoLastTrash(QString *error) {
    Files::ErrorCode code;
    const bool ok = m_trash->undoLastDelete(&code);
    if (error) *error = ok ? QString() : Files::errorReport(code);
    return ok;
}
int Report::succeededCount() const { return std::count_if(items.cbegin(), items.cend(), [](const ItemResult &r) { return r.outcome == Outcome::Succeeded; }); }
int Report::failedCount() const { return std::count_if(items.cbegin(), items.cend(), [](const ItemResult &r) { return r.outcome == Outcome::Failed; }); }
QString Report::text() const {
    QString out = QStringLiteral("成功 %1 项，失败 %2 项%3\n%4\n恢复记录：%5\n").arg(succeededCount()).arg(failedCount()).arg(cancelled ? QStringLiteral("，已取消") : QString(), error, journalPath);
    for (const auto &result : items)
        out += QStringLiteral("%1\t%2\t%3\n").arg(actionLabel(result.item.action), result.item.relativePath, result.message);
    return out;
}
Baseline commonBaseline(const Plan &plan) {
    Baseline out;
    if (!plan.executable() || !plan.options.excludedPaths.isEmpty()) return out;
    for (const auto &item : plan.items) {
        if (item.action != Action::Skip || item.reasonCode != QLatin1String("equal") || !sameContent(item.left, item.right)) return out;
        out.entries.insert(item.relativePath, item.left);
    }
    out.leftRoot = plan.leftRoot; out.rightRoot = plan.rightRoot; out.scopeKey = scopeKey(plan.options); out.complete = true;
    return out;
}
} }
