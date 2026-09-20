#ifndef LQCOMPARE_PATCHAPPLY_H
#define LQCOMPARE_PATCHAPPLY_H

#include "patch.h"
#include <QDateTime>
#include <QStringList>
#include <functional>
#include <memory>

namespace LqCompare { namespace Patch {

enum class PatchSource { Unspecified, InMemory, File };
struct InputProtection {
    PatchSource source = PatchSource::Unspecified; // Must be explicitly selected.
    ParseOptions sourceParseOptions;
    // Set for patches read from a file; leave empty for clipboard/in-memory input.
    QString patchFilePath;
    // Other source inputs that must be checked and never overwritten.
    QStringList readOnlyPaths;
};

enum class ApplicationStatus {
    Applied, NoChanges, Rejected, FailedUnchanged, RolledBack, RecoveryRequired
};
enum class ApplicationStage {
    Validation, Backup, Staging, Commit, Verification, Rollback, Finished
};
struct ApplicationAudit {
    ApplicationStage stage = ApplicationStage::Validation;
    bool success = false;
    QString path;
    QString message;
    QDateTime time;
};
struct ApplicationResult {
    ApplicationStatus status = ApplicationStatus::Rejected;
    QString targetPath;
    QString backupPath; // Unique retained original; never an unrelated .orig file.
    QVector<Diagnostic> diagnostics;
    QVector<ApplicationAudit> audit;
    bool ok() const { return status == ApplicationStatus::Applied || status == ApplicationStatus::NoChanges; }
};

// Test seam. A nonempty return injects an I/O failure at the named point. The
// callback may also model an external change; validation runs AFTER callbacks.
enum class ApplicationFailurePoint {
    BeforeBackup, DuringBackupWrite, BeforeStage, DuringStageWrite,
    BeforeCommit, AfterCommit, BeforeRollback, DuringRollbackWrite, BeforeRollbackCommit
};
using ApplicationFailureInjector = std::function<QString(ApplicationFailurePoint, const QString &targetPath)>;

class ApplicationPlan;
ApplicationPlan prepareApplication(const Document &document, const QString &root,
                                   const ApplyOptions &options = {},
                                   const InputProtection &protection = {});
ApplicationResult executeApplication(const ApplicationPlan &plan, bool confirmed = false,
                                     const ApplicationFailureInjector &injectFailure = {});

// Immutable reviewed plan: callers can display the preview, but cannot replace
// resultBytes, paths, hunk selections or source snapshots after confirmation.
class ApplicationPlan {
public:
    ApplicationPlan() = default;
    bool isReady() const;
    const PreviewResult &preview() const;
    const QVector<Diagnostic> &diagnostics() const;
    QString targetPath() const; // Empty for a no-op or rejected plan.
private:
    struct Data;
    std::shared_ptr<const Data> m_data;
    friend ApplicationPlan prepareApplication(const Document &, const QString &, const ApplyOptions &, const InputProtection &);
    friend ApplicationResult executeApplication(const ApplicationPlan &, bool, const ApplicationFailureInjector &);
};

// The boundary is ONE existing regular file replacement. Creation, deletion,
// and more than one changed target are rejected before writing anything.
// QSaveFile provides atomic byte replacement on ordinary runtime I/O failure.
// This Qt-only API assumes a trusted stable directory and cooperating writers:
// repeated path/snapshot checks and a cooperative lock are NOT protection from
// malicious concurrent renames/writes, and no power-loss durability is promised.
// Keep a plan only until execution; re-prepare after a stale-plan rejection.
// Retained backups plus structured audit support recovery; callers must surface
// RecoveryRequired and backupPath, never report it as a successful rollback.
QByteArray applicationAuditJson(const ApplicationResult &result);

} }
#endif
