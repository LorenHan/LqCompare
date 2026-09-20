// -----------------------------------------------------------------------------
// 补丁应用（PAT-002 / PAT-005）的测试
//
// 每个用例都在 QTemporaryDir 里造自己的目标树，绝不碰用户真实文件（PAT-005 的
// 安全前提是「应用会写盘」，测试若跑在真实目录上就是灾难）。备份位置通过
// 环境变量配置，环境变量是进程级的，因此每个改它的用例都用 EnvironmentGuard
// 把旧值还原——否则后面的用例会莫名其妙地走到别的目录去。
// -----------------------------------------------------------------------------

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include "patchapply.h"

using namespace LqCompare::Patch;

namespace {

QString statusName(ApplicationStatus status)
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

QString stageName(ApplicationStage stage)
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

QStringList auditStages(const ApplicationResult &result)
{
    QStringList stages;
    for (const ApplicationAudit &entry : result.audit) stages << stageName(entry.stage);
    return stages;
}

// QCOMPARE / QVERIFY2 都是宏，而**只有圆括号**能保护参数里的逗号：
// 直接写 QCOMPARE(x, QStringList{a, b}) 会被当成三个参数，报
// 「too many arguments provided to function-like macro invocation」。
// 构造列表时统一经过这个函数调用，让编译器顺手提供那层圆括号。
QStringList asList(const QStringList &items)
{
    return items;
}

QString auditMessage(const ApplicationResult &result, ApplicationStage stage)
{
    for (const ApplicationAudit &entry : result.audit) {
        if (entry.stage == stage) return entry.message;
    }
    return QString();
}

QString allDiagnostics(const QVector<Diagnostic> &diagnostics)
{
    QString text;
    for (const Diagnostic &item : diagnostics) text += item.message + QLatin1Char('\n');
    return text;
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    return file.readAll();
}

bool putFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    return ok;
}

QStringList entriesIn(const QString &directory)
{
    return QDir(directory).entryList(QStringList{QStringLiteral("*")},
                                     QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}

// 造一个「每行 line-NN、按行号替换」的文本，用来精确控制 hunk 的位置与数量。
QByteArray buildLines(int count, const QMap<int, QByteArray> &replacements = {})
{
    QByteArray out;
    for (int index = 1; index <= count; ++index) {
        if (replacements.contains(index)) out += replacements.value(index);
        else out += QByteArray("line-") + QByteArray::number(index).rightJustified(2, '0') + '\n';
    }
    return out;
}

// 真实路径：走 generate + parse，与应用时的输入形态一致。
Document documentFor(const QByteArray &before, const QByteArray &after,
                     const QString &name = QStringLiteral("sample.txt"))
{
    FileInput input;
    input.oldPath = name;
    input.newPath = name;
    input.oldBytes = before;
    input.newBytes = after;
    const GenerateResult generated = generate({input});
    if (!generated.ok) qFatal("test fixture patch could not be generated");
    const ParseResult parsed = parse(generated.bytes);
    if (!parsed.ok) qFatal("test fixture patch could not be parsed");
    return parsed.document;
}

// 手工构造的 Document：用来验证「不经 parse 也能进来的输入」——路径安全校验
// 不能只靠 parse 把守，补丁文档也可能由别处（剪贴板、API 调用方）组装。
Document handBuiltDocument(const QString &oldPath, const QString &newPath,
                           const QByteArray &oldLine, const QByteArray &newLine)
{
    FilePatch file;
    file.oldPath = oldPath;
    file.newPath = newPath;
    file.patchLine = 1;
    Hunk hunk;
    hunk.oldStart = 1;
    hunk.oldCount = 1;
    hunk.newStart = 1;
    hunk.newCount = 1;
    hunk.patchLine = 2;
    hunk.lines.push_back({'-', oldLine, 3});
    hunk.lines.push_back({'+', newLine, 4});
    file.hunks.push_back(hunk);
    Document document;
    document.files.push_back(file);
    return document;
}

InputProtection inMemory()
{
    InputProtection protection;
    protection.source = PatchSource::InMemory;
    return protection;
}

ApplicationFailureInjector atPoint(ApplicationFailurePoint point, const QString &message)
{
    return [point, message](ApplicationFailurePoint candidate, const QString &) {
        return candidate == point ? message : QString();
    };
}

ApplicationFailureInjector atPoints(ApplicationFailurePoint first, ApplicationFailurePoint second,
                                    const QString &message)
{
    return [first, second, message](ApplicationFailurePoint candidate, const QString &) {
        return (candidate == first || candidate == second) ? message : QString();
    };
}

struct EnvironmentGuard {
    explicit EnvironmentGuard(const char *name) : m_name(name)
    {
        m_saved = qgetenv(name);
        m_wasSet = !m_saved.isNull();
    }
    ~EnvironmentGuard()
    {
        if (m_wasSet) qputenv(m_name, m_saved);
        else qunsetenv(m_name);
    }
    const char *m_name;
    QByteArray m_saved;
    bool m_wasSet = false;
};

} // namespace

class PatchApplyTests : public QObject
{
    Q_OBJECT

private slots:
    // ---------- A 预演与逐 hunk 选择（PAT-002 第 1、2 条） ----------
    void planListsEveryHunkWithApplicability();
    void planReportsWhyAHunkCannotApply();
    void selectedHunksApplyOnlyTheSelection();
    void emptySelectionIsANoOpNotAFailure();
    void planRejectsAnEmptyDocument();

    // ---------- B 边界与路径安全（patchapply.h 边界 / PAT-005 第 1 条） ----------
    void creationPatchIsRejectedWithAffectedFiles();
    void deletionPatchIsRejectedWithAffectedFiles();
    void multipleTargetsAreRejectedWithAllAffectedFiles();
    void traversalPathIsRejectedBeforeAnyWrite();
    void absolutePathIsRejected();
    void symlinkedTargetIsRejected();
    void duplicateTargetsAreRejected();
    void targetEqualToThePatchSourceIsRejected();
    void targetOutsideTheReadOnlyListIsRejected();
    void unspecifiedSourceIsRejected();
    void fileSourceWithoutAPathIsRejected();
    void hunksCarryingNulBytesAreRejectedAsBinary();

    // ---------- C 确认（PAT-005 第 2 条） ----------
    void executionWithoutConfirmationIsRejectedAndListsTargets();
    void confirmedExecutionApplies();
    void largeRewriteNeedsConfirmationAndNamesTheFile();
    void smallChangeIsNotReportedAsALargeRewrite();
    void noChangeNeedsNoConfirmation();

    // ---------- D 备份（PAT-005 第 3 条） ----------
    void applyCreatesAnOrigBackupOfTheOriginalBytes();
    void backupLocationIsConfigurable();
    void existingUnrelatedOrigIsPreserved();
    void retentionPolicyPrunesNumberedBackupsOnly();
    void unsetRetentionKeepsEveryBackup();
    void missingBackupDirectoryFailsWithoutChanges();
    void backupWriteFailureLeavesTheTargetUnchanged();
    void corruptedBackupIsDetectedBeforeApplying();

    // ---------- E 原子性与回滚（PAT-002 第 3 条） ----------
    void failureBeforeStageLeavesTheTargetUnchanged();
    void failureDuringStageWriteLeavesTheTargetUnchanged();
    void failureBeforeCommitLeavesTheTargetUnchanged();
    void failedApplicationLeavesNoTemporaryFiles();
    void verificationFailureRollsBackByteExactly();
    void verificationMismatchRollsBack();
    void rollbackWriteFailureReportsRecoveryRequired();
    void rollbackCommitFailureReportsRecoveryRequired();
    void rollbackStartFailureReportsRecoveryRequired();
    void stalePlanIsRejectedAndExternalChangePreserved();
    void externalChangeDuringStagingIsDetected();
    void symlinkSwappedInAfterPrepareIsRejected();
    void cooperativeLockIsHonoured();
    void lockIsReleasedAfterApply();

    // ---------- F 审计与报表 JSON（patchapply.h / PAT-005 第 5 条） ----------
    void successAuditFollowsTheDocumentedStageOrder();
    void rollbackAuditIncludesTheRollbackStage();
    void failureAuditStopsAtTheStageThatFailed();
    void auditJsonCarriesStatusPathsAndStages();
    void auditJsonEscapesUnicodeAndQuotes();
    void auditJsonReportsRecoveryRequiredWithItsBackup();
    void auditJsonKeepsDiagnosticLines();
    void auditJsonOfARejectedPlanHasNoTarget();

    // ---------- G 应用统计（PAT-002 第 4 条） ----------
    void reportListsAppliedHunksAndFuzzOffsets();
    void reportCountsOnlyTheSelectedAndAppliedHunks();

    // ---------- H 反向应用（PAT-002 第 5 条） ----------
    void reverseApplyUndoesAnAppliedPatch();
    void reverseGeneratedPatchRestoresTheOriginal();

    // ---------- I 二进制与权限变更的单独提示（PAT-005 第 4 条） ----------
    void permissionChangePatchIsRefusedAtParse();
    void binaryPatchIsRefusedAtParse();
};

// -----------------------------------------------------------------------------
// A 预演与逐 hunk 选择
// -----------------------------------------------------------------------------

void PatchApplyTests::planListsEveryHunkWithApplicability()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = buildLines(40);
    const QByteArray after = buildLines(40, {{3, "CHANGED-03\n"}, {20, "CHANGED-20\n"}, {37, "CHANGED-37\n"}});
    const Document document = documentFor(before, after);
    QCOMPARE(document.files.size(), 1);
    QCOMPARE(document.files.first().hunks.size(), 3); // 三处改动被 6 行以上未改内容隔开

    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, before));

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY2(plan.isReady(), qPrintable(allDiagnostics(plan.diagnostics())));
    QCOMPARE(plan.diagnostics().size(), 0);
    QCOMPARE(plan.preview().files.size(), 1);
    QCOMPARE(plan.preview().files.first().hunks.size(), 3);
    for (const HunkPreview &hunk : plan.preview().files.first().hunks) {
        QVERIFY(hunk.selected);
        QVERIFY(hunk.applicable);
        QVERIFY(hunk.error.isEmpty());
    }
    QCOMPARE(plan.targetPath(), QFileInfo(target).canonicalFilePath());
}

void PatchApplyTests::planReportsWhyAHunkCannotApply()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = "alpha\nbeta\ngamma\ndelta\n";
    const QByteArray after = "alpha\nBETA\ngamma\ndelta\n";
    const Document document = documentFor(before, after);
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    // 磁盘上是完全对不上的内容：预演必须给出「为什么不能应用」，而不是只说失败。
    QVERIFY(putFile(target, "nothing\nmatches\nhere\nat-all\n"));

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    QVERIFY(!plan.preview().ok);
    QVERIFY(!plan.preview().files.first().hunks.first().applicable);
    QVERIFY(!plan.preview().files.first().hunks.first().error.isEmpty());
    const QString diagnostics = allDiagnostics(plan.diagnostics());
    QVERIFY2(diagnostics.contains(QStringLiteral("上下文")), qPrintable(diagnostics));
    QCOMPARE(bytesOf(target), QByteArray("nothing\nmatches\nhere\nat-all\n"));
    QCOMPARE(plan.targetPath(), QString()); // 被拒绝的计划不报目标路径
}

void PatchApplyTests::selectedHunksApplyOnlyTheSelection()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = buildLines(40);
    const QByteArray after = buildLines(40, {{3, "CHANGED-03\n"}, {20, "CHANGED-20\n"}, {37, "CHANGED-37\n"}});
    const QByteArray expected = buildLines(40, {{3, "CHANGED-03\n"}, {37, "CHANGED-37\n"}});
    const Document document = documentFor(before, after);
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, before));

    ApplyOptions options;
    options.selectedHunks.insert(0, QSet<int>{0, 2});
    const ApplicationPlan plan = prepareApplication(document, root.path(), options, inMemory());
    QVERIFY2(plan.isReady(), qPrintable(allDiagnostics(plan.diagnostics())));
    QVERIFY(!plan.preview().files.first().hunks.at(1).selected);
    QVERIFY(plan.preview().files.first().hunks.at(2).selected);

    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(target), expected);
}

void PatchApplyTests::emptySelectionIsANoOpNotAFailure()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = "alpha\nbeta\ngamma\n";
    const QByteArray after = "alpha\nBETA\ngamma\n";
    const Document document = documentFor(before, after);
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, before));

    ApplyOptions options;
    options.selectedHunks.insert(0, QSet<int>{}); // 显式选择「一个都不应用」
    const ApplicationPlan plan = prepareApplication(document, root.path(), options, inMemory());
    QVERIFY2(plan.isReady(), qPrintable(allDiagnostics(plan.diagnostics())));
    QCOMPARE(plan.targetPath(), QString()); // 无操作的计划没有目标路径

    const ApplicationResult result = executeApplication(plan, false);
    QCOMPARE(statusName(result.status), QStringLiteral("noChanges"));
    QVERIFY(result.ok());
    QCOMPARE(bytesOf(target), before);
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".orig")));
}

void PatchApplyTests::planRejectsAnEmptyDocument()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ApplicationPlan plan = prepareApplication(Document{}, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("不包含任何文件记录")));
}

// -----------------------------------------------------------------------------
// B 边界与路径安全
// -----------------------------------------------------------------------------

void PatchApplyTests::creationPatchIsRejectedWithAffectedFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    FileInput input;
    input.oldPath = QStringLiteral("created.txt");
    input.newPath = QStringLiteral("created.txt");
    input.oldExists = false;
    input.newBytes = "brand new\n";
    const GenerateResult generated = generate({input});
    QVERIFY(generated.ok);
    const ParseResult parsed = parse(generated.bytes);
    QVERIFY(parsed.ok);

    const ApplicationPlan plan = prepareApplication(parsed.document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    const QString diagnostics = allDiagnostics(plan.diagnostics());
    QVERIFY2(diagnostics.contains(QStringLiteral("创建文件")), qPrintable(diagnostics));
    QVERIFY2(diagnostics.contains(QStringLiteral("created.txt")), qPrintable(diagnostics));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("created.txt"))));
}

void PatchApplyTests::deletionPatchIsRejectedWithAffectedFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    FileInput input;
    input.oldPath = QStringLiteral("doomed.txt");
    input.newPath = QStringLiteral("doomed.txt");
    input.oldBytes = "going away\n";
    input.newExists = false;
    const GenerateResult generated = generate({input});
    QVERIFY(generated.ok);
    const ParseResult parsed = parse(generated.bytes);
    QVERIFY(parsed.ok);
    const QString doomed = root.filePath(QStringLiteral("doomed.txt"));
    QVERIFY(putFile(doomed, "going away\n"));

    const ApplicationPlan plan = prepareApplication(parsed.document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    const QString diagnostics = allDiagnostics(plan.diagnostics());
    QVERIFY2(diagnostics.contains(QStringLiteral("删除文件")), qPrintable(diagnostics));
    QVERIFY2(diagnostics.contains(QStringLiteral("doomed.txt")), qPrintable(diagnostics));
    QCOMPARE(bytesOf(doomed), QByteArray("going away\n")); // 文件还在
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("doomed.txt")});
}

void PatchApplyTests::multipleTargetsAreRejectedWithAllAffectedFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = "alpha\nbeta\n";
    const QByteArray after = "alpha\nBETA\n";
    QVector<FileInput> inputs;
    for (const QString &name : {QStringLiteral("one.txt"), QStringLiteral("two.txt")}) {
        FileInput input;
        input.oldPath = input.newPath = name;
        input.oldBytes = before;
        input.newBytes = after;
        inputs.append(input);
        QVERIFY(putFile(root.filePath(name), before));
    }
    const GenerateResult generated = generate(inputs);
    QVERIFY(generated.ok);
    const ParseResult parsed = parse(generated.bytes);
    QVERIFY(parsed.ok);

    const ApplicationPlan plan = prepareApplication(parsed.document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    const QString diagnostics = allDiagnostics(plan.diagnostics());
    QVERIFY2(diagnostics.contains(QStringLiteral("同时改动 2 个目标")), qPrintable(diagnostics));
    QVERIFY2(diagnostics.contains(QStringLiteral("one.txt")), qPrintable(diagnostics));
    QVERIFY2(diagnostics.contains(QStringLiteral("two.txt")), qPrintable(diagnostics));
    for (const QString &name : {QStringLiteral("one.txt"), QStringLiteral("two.txt")})
        QCOMPARE(bytesOf(root.filePath(name)), before);
}

void PatchApplyTests::traversalPathIsRejectedBeforeAnyWrite()
{
    QTemporaryDir outer;
    QVERIFY(outer.isValid());
    const QString rootPath = outer.filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(rootPath));
    const QString escaped = outer.filePath(QStringLiteral("escaped.txt"));
    QVERIFY(putFile(escaped, "old\n"));

    // 手写路径而非 parse：parse 也会拒绝，但应用层不能把安全托付给上游。
    const Document document = handBuiltDocument(QStringLiteral("root/../../escaped.txt"),
                                                QStringLiteral("root/../../escaped.txt"),
                                                "old\n", "new\n");
    const ApplicationPlan plan = prepareApplication(document, rootPath, ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    const QString diagnostics = allDiagnostics(plan.diagnostics());
    QVERIFY2(diagnostics.contains(QStringLiteral("遍历")), qPrintable(diagnostics));
    QCOMPARE(bytesOf(escaped), QByteArray("old\n"));
}

void PatchApplyTests::absolutePathIsRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const Document document = handBuiltDocument(QStringLiteral("/tmp/lqcompare-absolute.txt"),
                                                QStringLiteral("/tmp/lqcompare-absolute.txt"),
                                                "old\n", "new\n");
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("相对路径")));
}

void PatchApplyTests::symlinkedTargetIsRejected()
{
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    const QString outsideFile = outside.filePath(QStringLiteral("real.txt"));
    QVERIFY(putFile(outsideFile, "alpha\nbeta\n"));
    const QString link = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(QFile(outsideFile).link(link));
    QVERIFY(QFileInfo(link).isSymLink());

    const Document document = documentFor("alpha\nbeta\n", "alpha\nBETA\n");
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("符号链接")));
    QCOMPARE(bytesOf(outsideFile), QByteArray("alpha\nbeta\n"));
}

void PatchApplyTests::duplicateTargetsAreRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\n"));

    Document document = handBuiltDocument(QStringLiteral("a/sample.txt"), QStringLiteral("b/sample.txt"),
                                          "alpha\n", "beta\n");
    FilePatch second = document.files.first();
    second.patchLine = 5;
    document.files.append(second);

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("重复")));
    QCOMPARE(bytesOf(target), QByteArray("alpha\n"));
}

void PatchApplyTests::targetEqualToThePatchSourceIsRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    // 说明：这里只验证「路径同一」这一条判定，因此磁盘上放的是补丁的旧侧内容
    // （真实场景里它就是补丁文件本身；判定本身与文件内容无关）。
    const QString target = root.filePath(QStringLiteral("input.diff"));
    QVERIFY(putFile(target, "alpha\n"));
    const Document document = documentFor("alpha\n", "beta\n", QStringLiteral("input.diff"));

    InputProtection protection;
    protection.source = PatchSource::File;
    protection.patchFilePath = target;
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), protection);
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("补丁来源文件")));
    QCOMPARE(bytesOf(target), QByteArray("alpha\n"));
}

void PatchApplyTests::targetOutsideTheReadOnlyListIsRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\n"));
    const Document document = documentFor("alpha\n", "beta\n");

    InputProtection protection = inMemory();
    protection.readOnlyPaths << target;
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), protection);
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("只读输入清单")));
    QCOMPARE(bytesOf(target), QByteArray("alpha\n"));
}

void PatchApplyTests::unspecifiedSourceIsRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\n"));
    const Document document = documentFor("alpha\n", "beta\n");

    // 默认构造的 InputProtection 的 source 是 Unspecified：必须显式声明来源。
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), InputProtection{});
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("来源未显式指定")));
}

void PatchApplyTests::fileSourceWithoutAPathIsRejected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\n"));
    const Document document = documentFor("alpha\n", "beta\n");

    InputProtection protection;
    protection.source = PatchSource::File; // 忘了给路径
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), protection);
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("补丁文件路径为空")));
}

void PatchApplyTests::hunksCarryingNulBytesAreRejectedAsBinary()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "old\n"));
    // 路径带一层前缀（与 git 的 a/ 一致），这样剥掉一层后正好落到 sample.txt。
    Document document = handBuiltDocument(QStringLiteral("a/sample.txt"), QStringLiteral("a/sample.txt"),
                                          "old\n", QByteArray("new\0x\n", 6));
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    QVERIFY(allDiagnostics(plan.diagnostics()).contains(QStringLiteral("二进制")));
    QCOMPARE(bytesOf(target), QByteArray("old\n"));
}

// -----------------------------------------------------------------------------
// C 确认
// -----------------------------------------------------------------------------

void PatchApplyTests::executionWithoutConfirmationIsRejectedAndListsTargets()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\ngamma\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, false);
    QCOMPARE(statusName(result.status), QStringLiteral("rejected"));
    QVERIFY(!result.ok());
    QCOMPARE(bytesOf(target), original);
    const QString diagnostics = allDiagnostics(result.diagnostics);
    QVERIFY2(diagnostics.contains(QStringLiteral("显式确认")), qPrintable(diagnostics));
    QVERIFY2(diagnostics.contains(QStringLiteral("受影响文件：sample.txt")), qPrintable(diagnostics));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".orig")));
}

void PatchApplyTests::confirmedExecutionApplies()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\n";
    const QByteArray edited = "alpha\nBETA\ngamma\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, edited);

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QVERIFY(result.ok());
    QCOMPARE(bytesOf(target), edited);
    QCOMPARE(result.targetPath, QFileInfo(target).canonicalFilePath());
}

void PatchApplyTests::largeRewriteNeedsConfirmationAndNamesTheFile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = buildLines(200);
    QMap<int, QByteArray> changes;
    for (int index = 1; index <= 100; ++index)
        changes.insert(index, QByteArray("REWRITTEN-") + QByteArray::number(index) + '\n');
    const QByteArray after = buildLines(200, changes);
    const Document document = documentFor(before, after);

    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, before));

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY2(plan.isReady(), qPrintable(allDiagnostics(plan.diagnostics())));
    const QString planDiagnostics = allDiagnostics(plan.diagnostics());
    QVERIFY2(planDiagnostics.contains(QStringLiteral("大规模改写")), qPrintable(planDiagnostics));
    QVERIFY2(planDiagnostics.contains(QStringLiteral("sample.txt")), qPrintable(planDiagnostics));
    QVERIFY2(planDiagnostics.contains(QStringLiteral("需要显式确认")), qPrintable(planDiagnostics));

    const ApplicationResult refused = executeApplication(plan, false);
    QCOMPARE(statusName(refused.status), QStringLiteral("rejected"));
    QCOMPARE(bytesOf(target), before);

    const ApplicationResult applied = executeApplication(plan, true);
    QCOMPARE(statusName(applied.status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(target), after);
}

void PatchApplyTests::smallChangeIsNotReportedAsALargeRewrite()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = buildLines(200);
    const QByteArray after = buildLines(200, {{7, "ONLY-ONE-CHANGED\n"}});
    const Document document = documentFor(before, after);
    QVERIFY(putFile(root.filePath(QStringLiteral("sample.txt")), before));

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    QVERIFY(!allDiagnostics(plan.diagnostics()).contains(QStringLiteral("大规模改写")));
}

void PatchApplyTests::noChangeNeedsNoConfirmation()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = "alpha\nbeta\n";
    QVERIFY(putFile(root.filePath(QStringLiteral("sample.txt")), before));
    const Document document = documentFor(before, "alpha\nBETA\n");

    ApplyOptions options;
    options.selectedHunks.insert(0, QSet<int>{});
    const ApplicationPlan plan = prepareApplication(document, root.path(), options, inMemory());
    QVERIFY(plan.isReady());
    // 无操作就是无操作：没有写盘动作，因此不必让用户为「什么都没发生」点确认。
    const ApplicationResult result = executeApplication(plan, false);
    QCOMPARE(statusName(result.status), QStringLiteral("noChanges"));
    QVERIFY(result.ok());
}

// -----------------------------------------------------------------------------
// D 备份
// -----------------------------------------------------------------------------

void PatchApplyTests::applyCreatesAnOrigBackupOfTheOriginalBytes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\ngamma\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QVERIFY(result.backupPath.endsWith(QStringLiteral(".orig")));
    QCOMPARE(bytesOf(result.backupPath), original);
    QCOMPARE(entriesIn(root.path()), asList(QStringList{QStringLiteral("sample.txt"), QStringLiteral("sample.txt.orig")}));
}

void PatchApplyTests::backupLocationIsConfigurable()
{
    EnvironmentGuard guard("LQCOMPARE_PATCH_BACKUP_DIR");
    QTemporaryDir root;
    QTemporaryDir backups;
    QVERIFY(root.isValid());
    QVERIFY(backups.isValid());
    qputenv("LQCOMPARE_PATCH_BACKUP_DIR", QFile::encodeName(backups.path()));

    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QCOMPARE(QFileInfo(result.backupPath).absolutePath(), QFileInfo(backups.path()).canonicalFilePath());
    QCOMPARE(bytesOf(result.backupPath), original);
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::existingUnrelatedOrigIsPreserved()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    // 别的工具（或上一次会话）留下的 .orig：绝不能覆盖，backupPath 也不能指向它。
    const QString unrelated = target + QStringLiteral(".orig");
    QVERIFY(putFile(unrelated, "unrelated older copy\n"));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(unrelated), QByteArray("unrelated older copy\n"));
    QVERIFY(result.backupPath.endsWith(QStringLiteral("sample.txt.orig.1")));
    QCOMPARE(bytesOf(result.backupPath), original);
}

void PatchApplyTests::retentionPolicyPrunesNumberedBackupsOnly()
{
    EnvironmentGuard guard("LQCOMPARE_PATCH_BACKUP_KEEP");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    qputenv("LQCOMPARE_PATCH_BACKUP_KEEP", QByteArray("0"));
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    QVERIFY(putFile(target + QStringLiteral(".orig.1"), "generation one\n"));
    QVERIFY(putFile(target + QStringLiteral(".orig.2"), "generation two\n"));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(result.backupPath), original);            // 本次备份保留
    QVERIFY(QFileInfo::exists(target + QStringLiteral(".orig")));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".orig.1")));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".orig.2")));
}

void PatchApplyTests::unsetRetentionKeepsEveryBackup()
{
    EnvironmentGuard guard("LQCOMPARE_PATCH_BACKUP_KEEP");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    qunsetenv("LQCOMPARE_PATCH_BACKUP_KEEP");
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    QVERIFY(putFile(target + QStringLiteral(".orig.1"), "generation one\n"));
    QVERIFY(putFile(target + QStringLiteral(".orig.2"), "generation two\n"));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    // 默认不删任何东西：删用户目录里的文件必须是显式配置过的行为。
    QVERIFY(QFileInfo::exists(target + QStringLiteral(".orig.1")));
    QVERIFY(QFileInfo::exists(target + QStringLiteral(".orig.2")));
    QCOMPARE(bytesOf(target + QStringLiteral(".orig.2")), QByteArray("generation two\n"));
}

void PatchApplyTests::missingBackupDirectoryFailsWithoutChanges()
{
    EnvironmentGuard guard("LQCOMPARE_PATCH_BACKUP_DIR");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString missing = root.filePath(QStringLiteral("no/such/backup/dir"));
    qputenv("LQCOMPARE_PATCH_BACKUP_DIR", QFile::encodeName(missing));

    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    QCOMPARE(bytesOf(target), original);
    QVERIFY(!allDiagnostics(result.diagnostics).isEmpty());
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::backupWriteFailureLeavesTheTargetUnchanged()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::DuringBackupWrite, QStringLiteral("注入：备份写失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    QCOMPARE(bytesOf(target), original);
    QCOMPARE(result.backupPath, QString()); // 目标没动，不应该留下备份
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
    QCOMPARE(auditStages(result), asList(QStringList{QStringLiteral("validation"), QStringLiteral("backup"),
                                              QStringLiteral("finished")}));
}

void PatchApplyTests::corruptedBackupIsDetectedBeforeApplying()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    // 备份是唯一能在失败后救回原文件的东西。这里让回调模拟「备份刚写完就被旁路
    // 改坏」：此时继续提交，等于把原文件换成新内容、而手上的备份已经不是原件。
    const QString directory = root.path();
    const ApplicationFailureInjector corruptBackup =
        [directory](ApplicationFailurePoint point, const QString &) {
            if (point != ApplicationFailurePoint::DuringBackupWrite) return QString();
            for (const QString &name : entriesIn(directory)) {
                if (name.contains(QStringLiteral(".orig")))
                    (void)putFile(QDir(directory).filePath(name), "corrupted backup\n");
            }
            return QString();
        };
    const ApplicationResult result = executeApplication(plan, true, corruptBackup);
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    QCOMPARE(bytesOf(target), original);
    QVERIFY(allDiagnostics(result.diagnostics).contains(QStringLiteral("备份内容")));
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

// -----------------------------------------------------------------------------
// E 原子性与回滚
// -----------------------------------------------------------------------------

void PatchApplyTests::failureBeforeStageLeavesTheTargetUnchanged()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::BeforeStage, QStringLiteral("注入：暂存之前失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    QCOMPARE(bytesOf(target), original);
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::failureDuringStageWriteLeavesTheTargetUnchanged()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\ngamma\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    // 这一条正是「原子替换」的意义所在：新内容已经写出去一半了，目标文件仍要
    // 一个字节都没动。若把 QSaveFile 换成直接写目标文件，这里会立刻变红。
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::DuringStageWrite, QStringLiteral("注入：暂存写失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    QCOMPARE(bytesOf(target), original);
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::failureBeforeCommitLeavesTheTargetUnchanged()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::BeforeCommit, QStringLiteral("注入：提交之前失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    QCOMPARE(bytesOf(target), original);
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::failedApplicationLeavesNoTemporaryFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\nbeta\n"));
    const Document document = documentFor("alpha\nbeta\n", "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::DuringStageWrite, QStringLiteral("注入：暂存写失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("failedUnchanged"));
    // 目录里只能有目标文件本身：QSaveFile 的临时文件必须被清理干净。
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::verificationFailureRollsBackByteExactly()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\ngamma\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::AfterCommit, QStringLiteral("注入：提交后故障")));
    QCOMPARE(statusName(result.status), QStringLiteral("rolledBack"));
    QVERIFY(!result.ok());
    QCOMPARE(bytesOf(target), original);
    QVERIFY(!result.backupPath.isEmpty());
    QCOMPARE(bytesOf(result.backupPath), original);
    QCOMPARE(auditStages(result), asList(QStringList{QStringLiteral("validation"), QStringLiteral("backup"),
                                              QStringLiteral("staging"), QStringLiteral("commit"),
                                              QStringLiteral("verification"), QStringLiteral("rollback"),
                                              QStringLiteral("finished")}));
}

void PatchApplyTests::verificationMismatchRollsBack()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\ngamma\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    // 回调在提交后把目标文件改坏，再返回空字符串：校验读到的事实与预期不符，
    // 必须回滚而不是相信「回调没报错所以一切正常」。
    const ApplicationFailureInjector tamper =
        [target](ApplicationFailurePoint point, const QString &) {
            if (point == ApplicationFailurePoint::AfterCommit) (void)putFile(target, "tampered\n");
            return QString();
        };
    const ApplicationResult result = executeApplication(plan, true, tamper);
    QCOMPARE(statusName(result.status), QStringLiteral("rolledBack"));
    QCOMPARE(bytesOf(target), original);
}

void PatchApplyTests::rollbackWriteFailureReportsRecoveryRequired()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoints(ApplicationFailurePoint::AfterCommit,
                             ApplicationFailurePoint::DuringRollbackWrite,
                             QStringLiteral("注入：回滚写失败")));
    // 回滚没成功就绝不能报「已回滚」——用户会以为文件是好的。
    QCOMPARE(statusName(result.status), QStringLiteral("recoveryRequired"));
    QVERIFY(!result.ok());
    QVERIFY(!result.backupPath.isEmpty());
    QCOMPARE(bytesOf(result.backupPath), original);
    QCOMPARE(bytesOf(target), QByteArray("alpha\nBETA\n")); // 目标此刻仍是新内容
    for (const ApplicationAudit &entry : result.audit) {
        if (entry.stage == ApplicationStage::Rollback) QVERIFY(!entry.success);
    }
}

void PatchApplyTests::rollbackCommitFailureReportsRecoveryRequired()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoints(ApplicationFailurePoint::AfterCommit,
                             ApplicationFailurePoint::BeforeRollbackCommit,
                             QStringLiteral("注入：回滚提交失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("recoveryRequired"));
    QVERIFY(!result.backupPath.isEmpty());
}

void PatchApplyTests::rollbackStartFailureReportsRecoveryRequired()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoints(ApplicationFailurePoint::AfterCommit, ApplicationFailurePoint::BeforeRollback,
                             QStringLiteral("注入：回滚之前失败")));
    QCOMPARE(statusName(result.status), QStringLiteral("recoveryRequired"));
    QVERIFY(!result.backupPath.isEmpty());
}

void PatchApplyTests::stalePlanIsRejectedAndExternalChangePreserved()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    QVERIFY(putFile(target, "changed while the user was reading the preview\n"));

    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("rejected"));
    QVERIFY(allDiagnostics(result.diagnostics).contains(QStringLiteral("已被改动")));
    // 用户在此期间改的内容不能被悄悄覆盖掉。
    QCOMPARE(bytesOf(target), QByteArray("changed while the user was reading the preview\n"));
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::externalChangeDuringStagingIsDetected()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    // 回调模拟外部改写后返回空串：校验必须排在回调之后，才可能发现它。
    const ApplicationFailureInjector externalChange =
        [target](ApplicationFailurePoint point, const QString &) {
            if (point == ApplicationFailurePoint::BeforeStage) (void)putFile(target, "external writer won\n");
            return QString();
        };
    const ApplicationResult result = executeApplication(plan, true, externalChange);
    QCOMPARE(statusName(result.status), QStringLiteral("rejected"));
    QCOMPARE(bytesOf(target), QByteArray("external writer won\n"));
    QCOMPARE(entriesIn(root.path()), QStringList{QStringLiteral("sample.txt")});
}

void PatchApplyTests::symlinkSwappedInAfterPrepareIsRejected()
{
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());

    // 预演之后把目标换成指向根目录外的符号链接，且内容与快照一模一样：
    // 这样只有「执行期重新校验路径」才拦得住它（快照比较会被内容相同骗过去）。
    const QString outsideFile = outside.filePath(QStringLiteral("real.txt"));
    QVERIFY(putFile(outsideFile, original));
    QVERIFY(QFile::remove(target));
    QVERIFY(QFile(outsideFile).link(target));

    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("rejected"));
    QVERIFY(QFileInfo(target).isSymLink()); // 链接本身没有被替换成普通文件
    QCOMPARE(bytesOf(outsideFile), original);
}

void PatchApplyTests::cooperativeLockIsHonoured()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const QByteArray edited = "alpha\nBETA\n";
    const Document document = documentFor(original, edited);

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const QString lock = root.filePath(QStringLiteral(".sample.txt.lqcompare-lock"));
    QVERIFY(putFile(lock, "held by another writer\n"));

    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("rejected"));
    QVERIFY(allDiagnostics(result.diagnostics).contains(QStringLiteral("锁定")));
    QCOMPARE(bytesOf(target), original);
    QVERIFY(QFileInfo::exists(lock)); // 别人的锁不能被我们删掉
    QCOMPARE(bytesOf(lock), QByteArray("held by another writer\n"));
}

void PatchApplyTests::lockIsReleasedAfterApply()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral(".sample.txt.lqcompare-lock"))));
}

// -----------------------------------------------------------------------------
// F 审计与报表 JSON
// -----------------------------------------------------------------------------

void PatchApplyTests::successAuditFollowsTheDocumentedStageOrder()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QCOMPARE(auditStages(result),
             asList(QStringList{QStringLiteral("validation"), QStringLiteral("backup"), QStringLiteral("staging"),
                         QStringLiteral("commit"), QStringLiteral("verification"), QStringLiteral("finished")}));
    for (const ApplicationAudit &entry : result.audit) QVERIFY(entry.success);
    QVERIFY(result.audit.last().time.isValid());
}

void PatchApplyTests::rollbackAuditIncludesTheRollbackStage()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\nbeta\n"));
    const Document document = documentFor("alpha\nbeta\n", "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::AfterCommit, QStringLiteral("注入：提交后故障")));
    QCOMPARE(auditStages(result),
             asList(QStringList{QStringLiteral("validation"), QStringLiteral("backup"), QStringLiteral("staging"),
                         QStringLiteral("commit"), QStringLiteral("verification"), QStringLiteral("rollback"),
                         QStringLiteral("finished")}));
    // 回滚成功，但这次应用并没有成功：Finished 必须如实标成失败。
    QVERIFY(!result.audit.last().success);
}

void PatchApplyTests::failureAuditStopsAtTheStageThatFailed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    QVERIFY(putFile(target, "alpha\nbeta\n"));
    const Document document = documentFor("alpha\nbeta\n", "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoint(ApplicationFailurePoint::BeforeStage, QStringLiteral("注入：暂存之前失败")));
    QCOMPARE(auditStages(result), asList(QStringList{QStringLiteral("validation"), QStringLiteral("backup"),
                                              QStringLiteral("staging"), QStringLiteral("finished")}));
    QVERIFY(!result.audit.at(2).success); // staging 失败
    QVERIFY(!result.audit.last().success);
}

void PatchApplyTests::auditJsonCarriesStatusPathsAndStages()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    const QByteArray json = applicationAuditJson(result);
    const QJsonObject object = QJsonDocument::fromJson(json).object();

    QCOMPARE(object.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
    QCOMPARE(object.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(object.value(QStringLiteral("targetPath")).toString(), QFileInfo(target).canonicalFilePath());
    QCOMPARE(object.value(QStringLiteral("backupPath")).toString(), result.backupPath);

    const QJsonArray audit = object.value(QStringLiteral("audit")).toArray();
    QCOMPARE(audit.size(), result.audit.size());
    QCOMPARE(audit.first().toObject().value(QStringLiteral("stage")).toString(), QStringLiteral("validation"));
    QCOMPARE(audit.last().toObject().value(QStringLiteral("stage")).toString(), QStringLiteral("finished"));
    QCOMPARE(audit.first().toObject().value(QStringLiteral("success")).toBool(), true);
    QVERIFY(!audit.first().toObject().value(QStringLiteral("time")).toString().isEmpty());
    QVERIFY(audit.first().toObject().contains(QStringLiteral("message")));
    QVERIFY(audit.first().toObject().contains(QStringLiteral("path")));
}

void PatchApplyTests::auditJsonEscapesUnicodeAndQuotes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString name = QStringLiteral("中文 \"引用\".txt");
    const QString target = root.filePath(name);
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n", name);

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY2(plan.isReady(), qPrintable(allDiagnostics(plan.diagnostics())));
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));

    const QByteArray json = applicationAuditJson(result);
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    // 往返一致：路径里的引号与中文都不能把报表变成坏 JSON 或被截断。
    QCOMPARE(parsed.object().value(QStringLiteral("targetPath")).toString(), result.targetPath);
    QVERIFY(result.targetPath.endsWith(name));
}

void PatchApplyTests::auditJsonReportsRecoveryRequiredWithItsBackup()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, "alpha\nBETA\n");

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(
        plan, true, atPoints(ApplicationFailurePoint::AfterCommit,
                             ApplicationFailurePoint::DuringRollbackWrite,
                             QStringLiteral("注入：回滚写失败")));
    const QJsonObject object = QJsonDocument::fromJson(applicationAuditJson(result)).object();
    QCOMPARE(object.value(QStringLiteral("status")).toString(), QStringLiteral("recoveryRequired"));
    QCOMPARE(object.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("backupPath")).toString(), result.backupPath);
    QVERIFY(!result.backupPath.isEmpty());
    QVERIFY(QFileInfo::exists(result.backupPath));
}

void PatchApplyTests::auditJsonKeepsDiagnosticLines()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = "alpha\nbeta\ngamma\ndelta\n";
    const Document document = documentFor(before, "alpha\nBETA\ngamma\ndelta\n");
    QVERIFY(putFile(root.filePath(QStringLiteral("sample.txt")), "does not match\n"));

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    const QJsonObject object = QJsonDocument::fromJson(applicationAuditJson(result)).object();
    const QJsonArray diagnostics = object.value(QStringLiteral("diagnostics")).toArray();
    QVERIFY(!diagnostics.isEmpty());
    bool sawLine = false;
    for (const QJsonValue &value : diagnostics) {
        const QJsonObject item = value.toObject();
        QVERIFY(item.contains(QStringLiteral("message")));
        if (item.value(QStringLiteral("line")).toInt() > 0) sawLine = true;
    }
    QVERIFY(sawLine); // 补丁行的定位信息不能丢
}

void PatchApplyTests::auditJsonOfARejectedPlanHasNoTarget()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const Document document = handBuiltDocument(QStringLiteral("a/../../escape.txt"),
                                                QStringLiteral("a/../../escape.txt"), "old\n", "new\n");
    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(!plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    const QJsonObject object = QJsonDocument::fromJson(applicationAuditJson(result)).object();
    QCOMPARE(object.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
    QCOMPARE(object.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("targetPath")).toString(), QString());
    QCOMPARE(object.value(QStringLiteral("backupPath")).toString(), QString());
}

// -----------------------------------------------------------------------------
// G 应用统计
// -----------------------------------------------------------------------------

void PatchApplyTests::reportListsAppliedHunksAndFuzzOffsets()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = "alpha\nbeta\ngamma\ndelta\n";
    const QByteArray after = "alpha\nBETA\ngamma\ndelta\n";
    const Document document = documentFor(before, after);
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    // 在文件头部插两行：hunk 的完整上下文往后挪 2 行，预演应报偏移 +2。
    const QByteArray shifted = "extra-one\nextra-two\n" + before;
    QVERIFY(putFile(target, shifted));

    const ApplicationPlan plan = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY2(plan.isReady(), qPrintable(allDiagnostics(plan.diagnostics())));
    QCOMPARE(plan.preview().files.first().hunks.first().offset, 2);

    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    const QString commitMessage = auditMessage(result, ApplicationStage::Commit);
    QVERIFY2(commitMessage.contains(QStringLiteral("偏移")), qPrintable(commitMessage));
    QVERIFY2(commitMessage.contains(QStringLiteral("+2")), qPrintable(commitMessage));
    // 报表里也要看得到：偏移量恒置 0 的实现会在这里变红。
    const QByteArray json = applicationAuditJson(result);
    QVERIFY(json.contains(QByteArray("+2")));
    QCOMPARE(bytesOf(target), "extra-one\nextra-two\nalpha\nBETA\ngamma\ndelta\n");
}

void PatchApplyTests::reportCountsOnlyTheSelectedAndAppliedHunks()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray before = buildLines(40);
    const QByteArray after = buildLines(40, {{3, "CHANGED-03\n"}, {20, "CHANGED-20\n"}, {37, "CHANGED-37\n"}});
    const Document document = documentFor(before, after);
    QVERIFY(putFile(root.filePath(QStringLiteral("sample.txt")), before));

    ApplyOptions options;
    options.selectedHunks.insert(0, QSet<int>{0});
    const ApplicationPlan plan = prepareApplication(document, root.path(), options, inMemory());
    QVERIFY(plan.isReady());
    const ApplicationResult result = executeApplication(plan, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));

    const QString message = auditMessage(result, ApplicationStage::Commit);
    QVERIFY2(message.contains(QStringLiteral("已应用 1 个 hunk")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("选中 1")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("文件共 3")), qPrintable(message));
}

// -----------------------------------------------------------------------------
// H 反向应用
// -----------------------------------------------------------------------------

void PatchApplyTests::reverseApplyUndoesAnAppliedPatch()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "alpha\nbeta\ngamma\ndelta\n";
    const QByteArray edited = "alpha\nBETA\ngamma\nDELTA\n";
    QVERIFY(putFile(target, original));
    const Document document = documentFor(original, edited);

    const ApplicationPlan forward = prepareApplication(document, root.path(), ApplyOptions(), inMemory());
    QVERIFY(forward.isReady());
    QCOMPARE(statusName(executeApplication(forward, true).status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(target), edited);

    ApplyOptions reverseOptions;
    reverseOptions.reverse = true; // 撤销已应用的补丁
    const ApplicationPlan undo = prepareApplication(document, root.path(), reverseOptions, inMemory());
    QVERIFY2(undo.isReady(), qPrintable(allDiagnostics(undo.diagnostics())));
    const ApplicationResult result = executeApplication(undo, true);
    QCOMPARE(statusName(result.status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(target), original);
    QCOMPARE(bytesOf(result.backupPath), edited); // 撤销前的状态也被备份下来了
}

void PatchApplyTests::reverseGeneratedPatchRestoresTheOriginal()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("sample.txt"));
    const QByteArray original = "one\ntwo\nthree\nfour\n";
    const QByteArray edited = "one\nTWO\nthree\nFOUR\n";
    QVERIFY(putFile(target, original));

    const ApplicationPlan forward = prepareApplication(
        documentFor(original, edited), root.path(), ApplyOptions(), inMemory());
    QVERIFY(forward.isReady());
    QCOMPARE(statusName(executeApplication(forward, true).status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(target), edited);

    // 由 B→A 生成的补丁，正向应用即等价于撤销。
    const ApplicationPlan back = prepareApplication(
        documentFor(edited, original), root.path(), ApplyOptions(), inMemory());
    QVERIFY(back.isReady());
    QCOMPARE(statusName(executeApplication(back, true).status), QStringLiteral("applied"));
    QCOMPARE(bytesOf(target), original);
}

// -----------------------------------------------------------------------------
// I 二进制与权限变更
// -----------------------------------------------------------------------------

void PatchApplyTests::permissionChangePatchIsRefusedAtParse()
{
    const QByteArray patch =
        "diff --git a/script.sh b/script.sh\n"
        "old mode 100644\n"
        "new mode 100755\n"
        "--- a/script.sh\n"
        "+++ b/script.sh\n"
        "@@ -1,1 +1,1 @@\n"
        "-echo old\n"
        "+echo new\n";
    const ParseResult parsed = parse(patch);
    QVERIFY(!parsed.ok);
    const QString diagnostics = allDiagnostics(parsed.diagnostics);
    // 权限变更必须被单独点名，不能只说一句「格式不支持」让用户猜。
    QVERIFY2(diagnostics.contains(QStringLiteral("权限")), qPrintable(diagnostics));
}

void PatchApplyTests::binaryPatchIsRefusedAtParse()
{
    const QByteArray patch =
        "diff --git a/blob.bin b/blob.bin\n"
        "index 1111111..2222222 100644\n"
        "GIT binary patch\n"
        "literal 4\n"
        "Lc$@\n";
    const ParseResult parsed = parse(patch);
    QVERIFY(!parsed.ok);
    QVERIFY(allDiagnostics(parsed.diagnostics).contains(QStringLiteral("二进制")));
}

QTEST_GUILESS_MAIN(PatchApplyTests)
#include "tst_patchapply.moc"
