#include "patchapply.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace LqCompare { namespace Patch {
namespace {

// ---------------------------------------------------------------------------
// 可配置项的落点
//
// patchapply.h 是已经定下来的契约：ApplyOptions 与 InputProtection 里都没有
// 「备份放在哪、保留几份」的字段，而本模块不允许改那个头。位置与保留策略因此
// 只能从进程环境读——这是唯一既不改接口、又能被测试真正配置到的做法。
// 默认（不设变量）时备份写在目标文件旁边，且**不裁剪任何历史备份**：删用户目录
// 里的东西必须是显式选择过的行为，不能是默认行为。
// ---------------------------------------------------------------------------
const char *const BackupDirectoryVariable = "LQCOMPARE_PATCH_BACKUP_DIR";
const char *const BackupKeepVariable = "LQCOMPARE_PATCH_BACKUP_KEEP";

// 「大规模改写」的阈值。必须同时满足「改动行数够多」与「占原文比例够高」：
// 只有其中一个条件的话，要么把一个 3 行文件整篇重写报成大规模改写（小范围、
// 低风险），要么把一个大文件里删掉 40 行当成大规模改写（其实只占 1%）。
constexpr int LargeRewriteLines = 40;

// 备份最多尝试多少个序号，纯粹是防「目录里堆了上万个同名备份」导致死循环。
constexpr int BackupNameSearchLimit = 100000;

QString applicationStatusIdentifier(ApplicationStatus status)
{
    switch (status) {
    case ApplicationStatus::Applied: return QStringLiteral("applied");
    case ApplicationStatus::NoChanges: return QStringLiteral("noChanges");
    case ApplicationStatus::Rejected: return QStringLiteral("rejected");
    case ApplicationStatus::FailedUnchanged: return QStringLiteral("failedUnchanged");
    case ApplicationStatus::RolledBack: return QStringLiteral("rolledBack");
    case ApplicationStatus::RecoveryRequired: return QStringLiteral("recoveryRequired");
    }
    return QStringLiteral("unknown");
}

QString applicationStageIdentifier(ApplicationStage stage)
{
    switch (stage) {
    case ApplicationStage::Validation: return QStringLiteral("validation");
    case ApplicationStage::Backup: return QStringLiteral("backup");
    case ApplicationStage::Staging: return QStringLiteral("staging");
    case ApplicationStage::Commit: return QStringLiteral("commit");
    case ApplicationStage::Verification: return QStringLiteral("verification");
    case ApplicationStage::Rollback: return QStringLiteral("rollback");
    case ApplicationStage::Finished: return QStringLiteral("finished");
    }
    return QStringLiteral("unknown");
}

QString describeStatus(ApplicationStatus status)
{
    switch (status) {
    case ApplicationStatus::Applied: return QStringLiteral("应用成功");
    case ApplicationStatus::NoChanges: return QStringLiteral("没有需要应用的改动");
    case ApplicationStatus::Rejected: return QStringLiteral("已拒绝，未做任何写盘");
    case ApplicationStatus::FailedUnchanged: return QStringLiteral("应用失败，目标文件保持原状");
    case ApplicationStatus::RolledBack: return QStringLiteral("校验失败已回滚，目标文件恢复原状");
    case ApplicationStatus::RecoveryRequired: return QStringLiteral("回滚未成功，需要人工恢复");
    }
    return QStringLiteral("未知状态");
}

bool pathInsideRoot(const QString &root, const QString &path)
{
    if (root.isEmpty() || path.isEmpty()) return false;
    if (root == QStringLiteral("/")) return path.startsWith(QLatin1Char('/'));
    return path == root || path.startsWith(root + QLatin1Char('/'));
}

int lineCount(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return 0;
    int count = bytes.count('\n');
    if (!bytes.endsWith('\n')) ++count;
    return count;
}

QByteArray readAllBytes(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        if (error) *error = file.errorString();
        return QByteArray();
    }
    return bytes;
}

// 写入一个**新**文件（备份）。用 QSaveFile 而不是裸 QFile：宁可这里失败，
// 也不能留下半份内容看起来像备份的字节——用户会在恢复时相信它。
bool writeNewFileAtomically(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    // 关掉直接写入回退：回退路径在目标目录不可写时会直接写原文件，
    // 那样备份就失去了「要么完整、要么没有」的性质。
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        if (error) *error = message;
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString backupDirectoryFor(const QString &target, QString *error)
{
    const QByteArray configured = qgetenv(BackupDirectoryVariable);
    if (configured.isEmpty()) return QFileInfo(target).absolutePath();
    // 环境变量承载的是文件系统路径，必须按本地编码解码，不能当 UTF-8 猜。
    const QString directory = QFile::decodeName(configured);
    const QFileInfo info(directory);
    if (!info.exists() || !info.isDir() || info.isSymLink()) {
        *error = QStringLiteral("备份目录不存在、不是普通目录或是符号链接：%1").arg(directory);
        return QString();
    }
    return info.canonicalFilePath();
}

struct BackupChoice {
    QString path;
    bool numbered = false;
};

// 备份名优先用约定俗成的 <name>.orig；已被占用就退到 <name>.orig.<n>。
// 绝不覆盖已存在的 .orig——那个文件可能是别的工具（或上一次会话）留下的，
// 覆盖它等于把用户唯一的一份原始内容弄丢；ApplicationResult::backupPath
// 因此永远指向**我们自己刚写的**那一份。
BackupChoice chooseBackupPath(const QString &target, const QString &directory, QString *error)
{
    BackupChoice choice;
    const QString base = QFileInfo(target).fileName();
    choice.path = QDir(directory).filePath(base + QStringLiteral(".orig"));
    if (!QFileInfo::exists(choice.path)) return choice;
    for (int index = 1; index < BackupNameSearchLimit; ++index) {
        choice.path = QDir(directory).filePath(base + QStringLiteral(".orig.") + QString::number(index));
        if (!QFileInfo::exists(choice.path)) {
            choice.numbered = true;
            return choice;
        }
    }
    choice.path.clear();
    *error = QStringLiteral("备份名冲突过多，无法为 %1 选出唯一的名字").arg(base);
    return choice;
}

// 返回 -1 表示「未配置，不裁剪」。显式配置 0 是合法选择（只保留本次这一份），
// 因此不能让 0 与「没配置」共用同一个值。
int keepCountFromEnvironment()
{
    const QByteArray configured = qgetenv(BackupKeepVariable);
    if (configured.isEmpty()) return -1;
    bool ok = false;
    const int value = configured.toInt(&ok);
    if (!ok || value < 0) return -1;
    return value;
}

// 保留策略：只裁剪**带序号的**历史备份，且本次创建的备份与无序号 .orig 永不删除。
// 无序号 .orig 是别的工具与用户自己也会用的名字，删它就越权了。
void pruneNumberedBackups(const QString &directory, const QString &target, int keep,
                          const BackupChoice &current)
{
    if (keep < 0) return; // 未配置：保留全部
    const QString prefix = QFileInfo(target).fileName() + QStringLiteral(".orig.");
    struct Generation {
        int number = 0;
        QString path;
    };
    QVector<Generation> generations;
    const QStringList filter{prefix + QStringLiteral("*")};
    const QFileInfoList entries = QDir(directory).entryInfoList(filter, QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo &info : entries) {
        const QString absolute = info.absoluteFilePath();
        if (absolute == current.path) continue; // 本次的备份在报告里，不能自己删掉
        bool ok = false;
        const int number = info.fileName().mid(prefix.size()).toInt(&ok);
        if (!ok) continue; // 不是本工具命名的一代，不动它
        generations.append({number, absolute});
    }
    std::sort(generations.begin(), generations.end(),
              [](const Generation &a, const Generation &b) { return a.number > b.number; });
    int quota = keep - (current.numbered ? 1 : 0);
    if (quota < 0) quota = 0;
    for (int index = quota; index < generations.size(); ++index) QFile::remove(generations.at(index).path);
}

// 协作锁。它只挡得住「同样遵守这个约定的调用方」，挡不住恶意的并发改名/写入——
// patchapply.h 里已经写明这不是安全边界，注释留在这里是为了别让它被误当成防线。
class CooperativeLock
{
public:
    ~CooperativeLock()
    {
        if (!m_held) return;
        m_file.close();
        QFile::remove(m_path);
    }
    bool acquire(const QString &path, QString *error)
    {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            *error = QStringLiteral("目标文件正被另一个协作应用操作锁定：%1").arg(path);
            return false;
        }
        m_file.write(QByteArray::number(QCoreApplication::applicationPid()));
        m_path = path;
        m_held = true;
        return true;
    }

private:
    QFile m_file;
    QString m_path;
    bool m_held = false;
};

QString lockPathFor(const QString &target)
{
    const QFileInfo info(target);
    return QDir(info.absolutePath()).filePath(QStringLiteral(".") + info.fileName() + QStringLiteral(".lqcompare-lock"));
}

QString offsetsText(const QVector<int> &offsets)
{
    if (offsets.isEmpty()) return QStringLiteral("无");
    QStringList parts;
    for (int offset : offsets) {
        parts << (offset >= 0 ? QStringLiteral("+%1").arg(offset) : QString::number(offset));
    }
    return parts.join(QStringLiteral(", "));
}

QString hunkSummaryText(int applied, int selected, int total, const QVector<int> &offsets)
{
    return QStringLiteral("已应用 %1 个 hunk（选中 %2，文件共 %3）；偏移调整：%4")
        .arg(applied).arg(selected).arg(total).arg(offsetsText(offsets));
}

// 执行期重新校验目标路径。预演已经查过一遍，但计划被确认到真正写盘之间隔着
// 用户交互，中间目标完全可能被换成符号链接或指到根目录之外——所以这里按
// 「现在这一秒的事实」再查一遍，而不是相信计划里的结论。
bool targetStillSafe(const QString &root, const QString &target, QString *error)
{
    const QFileInfo info(target);
    if (info.isSymLink()) {
        *error = QStringLiteral("目标文件是符号链接，拒绝应用：%1").arg(target);
        return false;
    }
    if (!info.exists() || !info.isFile()) {
        *error = QStringLiteral("目标文件不存在或不是普通文件：%1").arg(target);
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !pathInsideRoot(root, canonical)) {
        *error = QStringLiteral("目标路径逃逸出根目录：%1").arg(target);
        return false;
    }
    if (canonical != target) {
        *error = QStringLiteral("目标路径在预演之后被替换为指向别处的路径：%1").arg(target);
        return false;
    }
    return true;
}

QVector<Diagnostic> fileDiagnosticsOf(const PreviewResult &preview)
{
    QVector<Diagnostic> diagnostics = preview.diagnostics;
    for (const FilePreview &file : preview.files) diagnostics += file.diagnostics;
    return diagnostics;
}

} // namespace

// ---------------------------------------------------------------------------
// ApplicationPlan
// ---------------------------------------------------------------------------

struct ApplicationPlan::Data {
    bool ready = false;
    bool noChanges = false;
    bool risky = false;
    PreviewResult preview;
    QVector<Diagnostic> diagnostics;
    ApplyOptions options;
    InputProtection protection;
    QString canonicalRoot;
    QString targetPath;
    QString relativePath;
    QByteArray sourceSnapshot;
    QByteArray resultBytes;
    int fileIndex = -1;
    int appliedHunkCount = 0;
    int selectedHunkCount = 0;
    int totalHunkCount = 0;
    QVector<int> fuzzOffsets;
    QStringList affectedFiles;
};

bool ApplicationPlan::isReady() const
{
    return m_data && m_data->ready;
}

const PreviewResult &ApplicationPlan::preview() const
{
    // 函数内 static 返回引用：按值返回临时容器、再把指向其中元素的指针交出去
    // 会立刻悬垂（handoff §6 记过这个坑），空计划这条路径尤其容易踩到。
    static const PreviewResult empty;
    return m_data ? m_data->preview : empty;
}

const QVector<Diagnostic> &ApplicationPlan::diagnostics() const
{
    static const QVector<Diagnostic> empty;
    return m_data ? m_data->diagnostics : empty;
}

QString ApplicationPlan::targetPath() const
{
    return m_data ? m_data->targetPath : QString();
}

// ---------------------------------------------------------------------------
// prepareApplication —— 只读的审查计划
// ---------------------------------------------------------------------------

ApplicationPlan prepareApplication(const Document &document, const QString &root,
                                   const ApplyOptions &options, const InputProtection &protection)
{
    ApplicationPlan plan;
    auto data = std::make_shared<ApplicationPlan::Data>();
    data->options = options;
    data->protection = protection;

    auto finish = [&](bool ready) {
        data->ready = ready;
        plan.m_data = data;
        return plan;
    };
    auto reject = [&](const QString &message) {
        data->diagnostics.append({0, message});
        return finish(false);
    };

    data->preview = preview(document, root, options);
    data->canonicalRoot = data->preview.root;
    if (!data->preview.ok) {
        // 预演的具体原因（哪一行、哪个文件）都带上，否则界面只能显示一句
        // 「预演失败」，用户无从下手。
        const QVector<Diagnostic> reasons = fileDiagnosticsOf(data->preview);
        data->diagnostics += reasons;
        if (reasons.isEmpty()) data->diagnostics.append({0, QStringLiteral("预演未通过，未做任何写盘")});
        return finish(false);
    }

    if (protection.source == PatchSource::Unspecified)
        return reject(QStringLiteral("补丁来源未显式指定（InputProtection::source），拒绝应用"));
    if (protection.source == PatchSource::File && protection.patchFilePath.isEmpty())
        return reject(QStringLiteral("补丁来源声明为文件，但补丁文件路径为空"));
    if (document.files.isEmpty())
        return reject(QStringLiteral("补丁不包含任何文件记录，没有可应用的改动"));

    // 二进制改写必须单独拦下来：parse() 已经拒绝含 NUL 的补丁文本，但手工构造的
    // Document 不经 parse 也能进来，而「文本 hunks 里夹着 NUL」在写盘后是不可逆的
    // 内容破坏，不能只靠上游把守。
    for (const FilePatch &file : document.files) {
        for (const Hunk &hunk : file.hunks) {
            for (const Line &line : hunk.lines) {
                if (line.bytes.contains('\0'))
                    return reject(QStringLiteral("补丁包含二进制改写（hunk 正文含 NUL 字节），"
                                                 "本模块只支持文本 unified diff"));
            }
        }
    }

    QVector<int> changed;
    for (int index = 0; index < data->preview.files.size(); ++index) {
        const FilePreview &file = data->preview.files.at(index);
        // 「改动目标」= 被选中且真的能应用、且结果与原文不同的那个文件。
        // 未选中的文件记录不写盘，因此不属于边界要拒绝的对象。
        if (file.selected && file.applicable && file.resultBytes != file.originalBytes)
            changed.append(index);
    }
    QStringList affected;
    for (int index : changed) affected << data->preview.files.at(index).relativePath;
    data->affectedFiles = affected;

    if (changed.isEmpty()) {
        // 逐 hunk 选择可以合法地选出「一个都不应用」——那是无操作，不是失败。
        data->noChanges = true;
        return finish(true);
    }

    const auto describe = [](const QStringList &names) { return names.join(QStringLiteral("、")); };
    if (changed.size() > 1) {
        return reject(QStringLiteral("补丁同时改动 %1 个目标，超出「单个已存在普通文件的替换」这一范围：%2")
                          .arg(changed.size()).arg(describe(affected)));
    }

    const FilePreview &target = data->preview.files.at(changed.first());
    if (target.createsFile) {
        return reject(QStringLiteral("补丁会创建文件，超出「单个已存在普通文件的替换」这一范围；"
                                     "受影响文件：%1").arg(describe(affected)));
    }
    if (target.deletesFile) {
        // 删除是 PAT-005 点名要单独确认的危险动作；本模块的边界不收创建/删除，
        // 因此这里直接拒绝，并把受影响文件列出来，让用户知道被挡掉的是哪一份。
        return reject(QStringLiteral("补丁会删除文件，超出「单个已存在普通文件的替换」这一范围；"
                                     "受影响文件：%1").arg(describe(affected)));
    }

    const QString canonicalTarget = QFileInfo(target.absolutePath).canonicalFilePath();
    if (canonicalTarget.isEmpty() || !pathInsideRoot(data->canonicalRoot, canonicalTarget)) {
        return reject(QStringLiteral("目标路径逃逸出根目录，拒绝应用：%1").arg(target.absolutePath));
    }

    // 输入保护：目标绝不能是补丁来源本身，也不能在只读清单里。这两条是「应用补丁
    // 把自己读的输入覆盖掉」的直接防护，比较用规范化后的绝对路径，避免别名绕过。
    const auto canonicalOf = [](const QString &path) {
        return path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    };
    const QString sourceCanonical = protection.source == PatchSource::File
        ? canonicalOf(protection.patchFilePath) : QString();
    if (!sourceCanonical.isEmpty() && sourceCanonical == canonicalTarget)
        return reject(QStringLiteral("目标文件就是补丁来源文件，拒绝覆盖自己的输入：%1").arg(canonicalTarget));
    for (const QString &readOnly : protection.readOnlyPaths) {
        const QString canonicalReadOnly = canonicalOf(readOnly);
        if (!canonicalReadOnly.isEmpty() && canonicalReadOnly == canonicalTarget)
            return reject(QStringLiteral("目标文件在只读输入清单中，拒绝覆盖：%1").arg(canonicalTarget));
    }

    // 计划里的源快照必须与此刻磁盘上的内容一致：预演读文件与这里之间有任何
    // 改动，计划就已经过期，不能让用户对着旧内容点确认。
    QString readError;
    const QByteArray current = readAllBytes(target.absolutePath, &readError);
    if (current != target.originalBytes) {
        return reject(QStringLiteral("目标文件在预演之后已被改动，计划已过期，拒绝应用（请重新预演）：%1")
                          .arg(target.absolutePath));
    }

    data->sourceSnapshot = target.originalBytes;
    data->resultBytes = target.resultBytes;
    data->fileIndex = changed.first();
    data->relativePath = target.relativePath;
    data->totalHunkCount = target.hunks.size();

    int changedLines = 0;
    const FilePatch &filePatch = document.files.at(changed.first());
    for (const HunkPreview &hunk : target.hunks) {
        if (!hunk.selected) continue;
        ++data->selectedHunkCount;
        // hunk.index 是它在文件记录里的下标，用它回取正文才能数出真实改动行数。
        if (hunk.index < 0 || hunk.index >= filePatch.hunks.size()) continue;
        for (const Line &line : filePatch.hunks.at(hunk.index).lines) {
            if (line.kind == '+' || line.kind == '-') ++changedLines;
        }
        if (hunk.applicable) {
            ++data->appliedHunkCount;
            data->fuzzOffsets.append(hunk.offset);
        }
    }
    if (changedLines >= LargeRewriteLines && changedLines * 2 >= lineCount(target.originalBytes)) {
        data->risky = true;
        // 危险操作的提示走 diagnostics：ApplicationPlan 的公开成员是写定的，
        // 界面要显示「这次改动很大」只能从这里拿。
        data->diagnostics.append({filePatch.patchLine,
            QStringLiteral("大规模改写：受影响文件 %1（改动 %2 行，原文 %3 行），执行前需要显式确认")
                .arg(target.relativePath).arg(changedLines).arg(lineCount(target.originalBytes))});
    }

    data->targetPath = canonicalTarget;
    return finish(true);
}

// ---------------------------------------------------------------------------
// executeApplication
// ---------------------------------------------------------------------------

ApplicationResult executeApplication(const ApplicationPlan &plan, bool confirmed,
                                     const ApplicationFailureInjector &injectFailure)
{
    ApplicationResult result;
    const auto audit = [&result](ApplicationStage stage, bool success, const QString &path,
                                 const QString &message) {
        ApplicationAudit entry;
        entry.stage = stage;
        entry.success = success;
        entry.path = path;
        entry.message = message;
        entry.time = QDateTime::currentDateTimeUtc();
        result.audit.append(entry);
    };
    // 结束动作用一个函数收口：审计的最后一格永远是 Finished，漏写它会让
    // 「审计按阶段顺序如实记录」这条契约出现一个只在成功路径上成立的漏洞。
    const auto conclude = [&result, &audit](ApplicationStatus status, const QString &path,
                                            const QString &message) {
        result.status = status;
        audit(ApplicationStage::Finished, result.ok(), path, message);
        return result;
    };

    if (!plan.m_data) {
        result.diagnostics.append({0, QStringLiteral("应用计划为空，没有可执行的内容")});
        audit(ApplicationStage::Validation, false, QString(), QStringLiteral("应用计划为空"));
        return conclude(ApplicationStatus::Rejected, QString(), QStringLiteral("应用计划为空"));
    }
    const ApplicationPlan::Data &data = *plan.m_data;

    if (!data.ready) {
        result.diagnostics += data.diagnostics;
        if (result.diagnostics.isEmpty())
            result.diagnostics.append({0, QStringLiteral("应用计划未通过预演或边界检查，未做任何写盘")});
        audit(ApplicationStage::Validation, false, QString(),
              QStringLiteral("计划未被接受，未执行任何应用动作"));
        return conclude(ApplicationStatus::Rejected, QString(), describeStatus(ApplicationStatus::Rejected));
    }

    result.diagnostics += data.diagnostics; // 大规模改写之类的提示要一起进报告
    result.targetPath = data.targetPath;

    if (data.noChanges) {
        audit(ApplicationStage::Validation, true, QString(), QStringLiteral("没有需要应用的改动"));
        return conclude(ApplicationStatus::NoChanges, QString(), describeStatus(ApplicationStatus::NoChanges));
    }

    // 强制确认。这里不是「顺手加的礼貌检查」：补丁会改写用户的文件，未经确认
    // 的调用一律不落盘。受影响文件一并列出，危险操作才有得看。
    if (!confirmed) {
        result.diagnostics.append({0, QStringLiteral("应用补丁需要显式确认（confirmed=true），"
                                                     "本次调用未写盘")});
        for (const QString &path : data.affectedFiles)
            result.diagnostics.append({0, QStringLiteral("受影响文件：%1").arg(path)});
        const QString message = QStringLiteral("未确认，拒绝应用");
        audit(ApplicationStage::Validation, false, data.targetPath, message);
        return conclude(ApplicationStatus::Rejected, data.targetPath, message);
    }

    // --- Validation -------------------------------------------------------
    QString failure;
    if (!targetStillSafe(data.canonicalRoot, data.targetPath, &failure)) {
        result.diagnostics.append({0, failure});
        audit(ApplicationStage::Validation, false, data.targetPath, failure);
        return conclude(ApplicationStatus::Rejected, data.targetPath, failure);
    }
    const QByteArray beforeBytes = readAllBytes(data.targetPath, &failure);
    if (beforeBytes != data.sourceSnapshot) {
        const QString message =
            QStringLiteral("目标文件在计划确认之后已被改动，拒绝应用（请重新预演）：%1").arg(data.targetPath);
        result.diagnostics.append({0, message});
        audit(ApplicationStage::Validation, false, data.targetPath, message);
        return conclude(ApplicationStatus::Rejected, data.targetPath, message);
    }
    CooperativeLock lock;
    if (!lock.acquire(lockPathFor(data.targetPath), &failure)) {
        result.diagnostics.append({0, failure});
        audit(ApplicationStage::Validation, false, data.targetPath, failure);
        return conclude(ApplicationStatus::Rejected, data.targetPath, failure);
    }
    audit(ApplicationStage::Validation, true, data.targetPath, QStringLiteral("路径、快照与协作锁校验通过"));

    // 回调可以模拟外部改写，因此**校验必须排在回调之后**：先让回调动手，
    // 再用同一份判定去查现在的事实，否则「回调改了文件」这条路径永远测不到。
    const auto snapshotStillMatches = [&data](QString *why) {
        QString error;
        const QByteArray now = readAllBytes(data.targetPath, &error);
        if (now != data.sourceSnapshot) {
            if (why)
                *why = QStringLiteral("目标文件在应用过程中被外部改写，拒绝继续（请重新预演）：%1")
                           .arg(data.targetPath);
            return false;
        }
        return true;
    };

    // 失败但目标未被改动时，把我们自己刚创建的备份删掉：目标没变，留着它只会在
    // 用户目录里堆积无用文件；backupPath 也不会被设置，报告不会指着一个多余文件。
    QString createdBackup;
    const auto dropOwnBackup = [&createdBackup]() {
        if (!createdBackup.isEmpty()) {
            QFile::remove(createdBackup);
            createdBackup.clear();
        }
    };
    const auto failUnchanged = [&](ApplicationStage stage, const QString &path, const QString &message) {
        dropOwnBackup();
        result.diagnostics.append({0, message});
        audit(stage, false, path, message);
        return conclude(ApplicationStatus::FailedUnchanged, data.targetPath, message);
    };

    // --- Backup -----------------------------------------------------------
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::BeforeBackup, data.targetPath);
        if (!injected.isEmpty()) return failUnchanged(ApplicationStage::Backup, QString(), injected);
    }
    QString revalidateError;
    if (!snapshotStillMatches(&revalidateError)) {
        audit(ApplicationStage::Backup, false, QString(), revalidateError);
        result.diagnostics.append({0, revalidateError});
        return conclude(ApplicationStatus::Rejected, data.targetPath, revalidateError);
    }

    QString backupError;
    const QString backupDirectory = backupDirectoryFor(data.targetPath, &backupError);
    if (backupDirectory.isEmpty()) return failUnchanged(ApplicationStage::Backup, QString(), backupError);
    const BackupChoice backup = chooseBackupPath(data.targetPath, backupDirectory, &backupError);
    if (backup.path.isEmpty()) return failUnchanged(ApplicationStage::Backup, QString(), backupError);
    if (!writeNewFileAtomically(backup.path, data.sourceSnapshot, &backupError))
        return failUnchanged(ApplicationStage::Backup, backup.path, backupError);
    createdBackup = backup.path;

    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::DuringBackupWrite, data.targetPath);
        if (!injected.isEmpty()) return failUnchanged(ApplicationStage::Backup, createdBackup, injected);
    }
    // 备份必须逐字节等于原始内容：它是唯一能在失败后救回文件的东西，
    // 「写完了」与「写对了」是两件事。
    QString verifyBackupError;
    if (readAllBytes(createdBackup, &verifyBackupError) != data.sourceSnapshot)
        return failUnchanged(ApplicationStage::Backup, createdBackup,
                             QStringLiteral("备份内容与原文件不一致，未继续应用：%1").arg(createdBackup));
    if (!snapshotStillMatches(&revalidateError)) {
        audit(ApplicationStage::Backup, false, createdBackup, revalidateError);
        result.diagnostics.append({0, revalidateError});
        dropOwnBackup();
        return conclude(ApplicationStatus::Rejected, data.targetPath, revalidateError);
    }
    audit(ApplicationStage::Backup, true, createdBackup,
          QStringLiteral("已创建原始内容备份（%1 字节）").arg(data.sourceSnapshot.size()));

    // --- Staging ----------------------------------------------------------
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::BeforeStage, data.targetPath);
        if (!injected.isEmpty()) return failUnchanged(ApplicationStage::Staging, QString(), injected);
    }
    if (!snapshotStillMatches(&revalidateError)) {
        audit(ApplicationStage::Staging, false, QString(), revalidateError);
        result.diagnostics.append({0, revalidateError});
        dropOwnBackup();
        return conclude(ApplicationStatus::Rejected, data.targetPath, revalidateError);
    }

    // QSaveFile 把新内容写进同目录的临时文件，commit() 才做原子替换。中途任何
    // 失败（含被注入的失败）都只影响那个临时文件，目标文件仍然是一个字节都没动。
    QSaveFile stage(data.targetPath);
    stage.setDirectWriteFallback(false);
    if (!stage.open(QIODevice::WriteOnly))
        return failUnchanged(ApplicationStage::Staging, QString(), stage.errorString());
    if (stage.write(data.resultBytes) != data.resultBytes.size()) {
        const QString message = stage.errorString();
        stage.cancelWriting();
        return failUnchanged(ApplicationStage::Staging, QString(), message);
    }
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::DuringStageWrite, data.targetPath);
        if (!injected.isEmpty()) {
            stage.cancelWriting();
            return failUnchanged(ApplicationStage::Staging, QString(), injected);
        }
    }
    if (!snapshotStillMatches(&revalidateError)) {
        stage.cancelWriting();
        audit(ApplicationStage::Staging, false, QString(), revalidateError);
        result.diagnostics.append({0, revalidateError});
        dropOwnBackup();
        return conclude(ApplicationStatus::Rejected, data.targetPath, revalidateError);
    }
    const QString summary = hunkSummaryText(data.appliedHunkCount, data.selectedHunkCount,
                                            data.totalHunkCount, data.fuzzOffsets);
    audit(ApplicationStage::Staging, true, data.targetPath,
          QStringLiteral("新内容已就绪：%1").arg(summary));

    // --- Commit -----------------------------------------------------------
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::BeforeCommit, data.targetPath);
        if (!injected.isEmpty()) {
            stage.cancelWriting();
            return failUnchanged(ApplicationStage::Commit, QString(), injected);
        }
    }
    if (!stage.commit())
        return failUnchanged(ApplicationStage::Commit, QString(), stage.errorString());
    audit(ApplicationStage::Commit, true, data.targetPath, summary);

    // --- Verification -----------------------------------------------------
    bool verificationFailed = false;
    QString verificationMessage;
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::AfterCommit, data.targetPath);
        if (!injected.isEmpty()) {
            verificationFailed = true;
            verificationMessage = injected;
        }
    }
    if (!verificationFailed) {
        QString readBackError;
        if (readAllBytes(data.targetPath, &readBackError) != data.resultBytes) {
            verificationFailed = true;
            verificationMessage = QStringLiteral("提交后校验失败：目标文件内容与预期结果不一致：%1")
                                      .arg(data.targetPath);
        }
    }
    if (!verificationFailed) {
        audit(ApplicationStage::Verification, true, data.targetPath, QStringLiteral("提交后校验通过"));
        result.backupPath = createdBackup;
        pruneNumberedBackups(backupDirectory, data.targetPath, keepCountFromEnvironment(), backup);
        return conclude(ApplicationStatus::Applied, data.targetPath,
                        QStringLiteral("应用成功；%1").arg(summary));
    }
    audit(ApplicationStage::Verification, false, data.targetPath, verificationMessage);
    result.diagnostics.append({0, verificationMessage});

    // --- Rollback ---------------------------------------------------------
    // 校验没过就必须把原文件放回去。回滚本身也可能失败，两条路径必须分开报告：
    // 把「回滚失败」说成「已回滚」等于让用户以为文件是好的，而它其实不是。
    const auto recoveryRequired = [&](const QString &message) {
        result.backupPath = createdBackup; // 唯一能救命的东西，必须留在报告里
        audit(ApplicationStage::Rollback, false, createdBackup, message);
        result.diagnostics.append({0, message});
        return conclude(ApplicationStatus::RecoveryRequired, data.targetPath, message);
    };
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::BeforeRollback, data.targetPath);
        if (!injected.isEmpty()) return recoveryRequired(injected);
    }
    QSaveFile restore(data.targetPath);
    restore.setDirectWriteFallback(false);
    if (!restore.open(QIODevice::WriteOnly)) return recoveryRequired(restore.errorString());
    if (restore.write(data.sourceSnapshot) != data.sourceSnapshot.size()) {
        const QString message = restore.errorString();
        restore.cancelWriting();
        return recoveryRequired(message);
    }
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::DuringRollbackWrite, data.targetPath);
        if (!injected.isEmpty()) {
            restore.cancelWriting();
            return recoveryRequired(injected);
        }
    }
    if (injectFailure) {
        const QString injected = injectFailure(ApplicationFailurePoint::BeforeRollbackCommit, data.targetPath);
        if (!injected.isEmpty()) {
            restore.cancelWriting();
            return recoveryRequired(injected);
        }
    }
    if (!restore.commit()) return recoveryRequired(restore.errorString());

    QString restoreReadError;
    if (readAllBytes(data.targetPath, &restoreReadError) != data.sourceSnapshot)
        return recoveryRequired(QStringLiteral("回滚后校验失败：目标文件未恢复为原始内容：%1").arg(data.targetPath));
    result.backupPath = createdBackup;
    audit(ApplicationStage::Rollback, true, createdBackup, QStringLiteral("已从备份回滚，目标文件恢复原状"));
    return conclude(ApplicationStatus::RolledBack, data.targetPath, describeStatus(ApplicationStatus::RolledBack));
}

// ---------------------------------------------------------------------------
// applicationAuditJson
// ---------------------------------------------------------------------------

QByteArray applicationAuditJson(const ApplicationResult &result)
{
    QJsonObject root;
    root.insert(QStringLiteral("status"), applicationStatusIdentifier(result.status));
    root.insert(QStringLiteral("ok"), result.ok());
    root.insert(QStringLiteral("targetPath"), result.targetPath);
    root.insert(QStringLiteral("backupPath"), result.backupPath);

    QJsonArray diagnostics;
    for (const Diagnostic &item : result.diagnostics) {
        QJsonObject object;
        object.insert(QStringLiteral("line"), item.line);
        object.insert(QStringLiteral("message"), item.message);
        diagnostics.append(object);
    }
    root.insert(QStringLiteral("diagnostics"), diagnostics);

    QJsonArray audit;
    for (const ApplicationAudit &item : result.audit) {
        QJsonObject object;
        object.insert(QStringLiteral("stage"), applicationStageIdentifier(item.stage));
        object.insert(QStringLiteral("success"), item.success);
        object.insert(QStringLiteral("path"), item.path);
        object.insert(QStringLiteral("message"), item.message);
        // 时间统一用 UTC 的 ISO 形式：报表要跨机器比对，本地时间没有可比性。
        object.insert(QStringLiteral("time"), item.time.toUTC().toString(Qt::ISODateWithMs));
        audit.append(object);
    }
    root.insert(QStringLiteral("audit"), audit);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} }
