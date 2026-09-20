#ifndef LQCOMPARE_SYNCENGINE_H
#define LQCOMPARE_SYNCENGINE_H

#include "foldercompare.h"
#include "trash.h"
#include <QMap>

namespace LqCompare { namespace Sync {

enum class Mode { Update, Mirror, TwoWay };
enum class Direction { LeftToRight, RightToLeft };
enum class Deletion { Keep, Trash };
enum class Action { Skip, Conflict, CopyLeftToRight, CopyRightToLeft,
                    CreateLeftDirectory, CreateRightDirectory, DeleteLeft, DeleteRight };

struct Options {
    Mode mode = Mode::Update;
    Direction direction = Direction::LeftToRight;
    Deletion deletion = Deletion::Trash;
    QStringList excludedPaths; // Relative subtrees, protected on BOTH sides.
    int deleteCountThreshold = 20;
    quint64 deleteBytesThreshold = 100 * 1024 * 1024;
};

struct Fingerprint {
    Folder::Kind kind = Folder::Kind::Missing;
    quint64 size = 0;
    qint64 modifiedNs = 0;
    qint64 createdNs = 0;
    int attributes = 0;
    QByteArray sha256;
    QString error;
    bool exists() const { return kind != Folder::Kind::Missing; }
};

// A baseline is a confirmed common state, bound to this pair and scope/rules.
// Missing hashes are never sufficient evidence to infer which side changed.
struct Baseline {
    QString leftRoot;
    QString rightRoot;
    QByteArray scopeKey;
    QMap<QString, Fingerprint> entries;
    bool complete = false;
};

struct Item {
    QString relativePath;
    Action action = Action::Skip;
    QString reasonCode;
    QString reason;
    Fingerprint left;
    Fingerprint right;
    bool selected = false;
};

struct Plan {
    QString id;
    QString leftRoot;
    QString rightRoot;
    Fingerprint leftRootIdentity;
    Fingerprint rightRootIdentity;
    Options options;
    QVector<Item> items;
    QStringList warnings;
    QString error;
    bool complete = false;
    bool baselineUsed = false;
    bool executable() const { return complete && error.isEmpty(); }
};

struct Summary {
    int copies = 0;
    int directories = 0;
    int deletions = 0;
    int conflicts = 0;
    int skipped = 0;
    quint64 copyBytes = 0;
    quint64 deleteBytes = 0;
    bool needsDeleteConfirmation = false;
    QString text() const;
};

struct Confirmation {
    QByteArray planDigest;
    bool userConfirmed = false;
    bool largeDeleteConfirmed = false;
};

enum class Outcome { Succeeded, Skipped, Failed, Cancelled };
struct ItemResult {
    Item item;
    Outcome outcome = Outcome::Skipped;
    QString message;
    QString backupPath;
    QString trashedPath;
    QString targetPath;
    QString targetRoot;
    Fingerprint targetRootIdentity;
    Fingerprint resultingTarget;
};

struct Report {
    QVector<ItemResult> items;
    QString error;
    QString backupDirectory;
    QString journalPath;
    bool cancelled = false;
    bool baselineEligible = false;
    int succeededCount() const;
    int failedCount() const;
    QString text() const;
};

using Progress = std::function<void(int completed, int total, const QString &path)>;

QString actionLabel(Action action);
bool isActionable(Action action);
QByteArray scopeKey(const Options &options);
// Reuses Folder scanner entries, then records content hashes for revalidation.
Plan makePlan(const Folder::Result &scan, const Options &options = {},
              const Baseline *baseline = nullptr, const std::atomic_bool *cancelled = nullptr);
Plan preview(const QString &leftRoot, const QString &rightRoot, const Options &options = {},
             const Baseline *baseline = nullptr, const std::atomic_bool *cancelled = nullptr,
             const Folder::Progress &progress = {});
Summary summarize(const Plan &plan);
QByteArray confirmationDigest(const Plan &plan);
QString exportPlanText(const Plan &plan);

class Executor {
public:
    // backupRoot must be outside both synchronized trees. Empty uses AppLocalData.
    explicit Executor(const QString &backupRoot = {}, Files::TrashService *trash = nullptr);
    ~Executor();
    Report execute(const Plan &plan, const Confirmation &confirmation,
                   const std::atomic_bool *cancelled = nullptr, const Progress &progress = {});
    // Restores one overwritten file only if the current result is unchanged.
    bool restoreBackup(const ItemResult &result, QString *error = nullptr);
    // Native TrashService may support only its latest successful delete batch.
    bool undoLastTrash(QString *error = nullptr);
private:
    QString m_backupRoot;
    std::unique_ptr<Files::TrashService> m_ownedTrash;
    Files::TrashService *m_trash = nullptr;
};

// Only equal, complete, unfiltered outcomes can establish a fresh common state.
Baseline commonBaseline(const Plan &verifiedPlan);

} }
#endif
