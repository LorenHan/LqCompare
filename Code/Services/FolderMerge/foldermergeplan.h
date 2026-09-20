#ifndef LQCOMPARE_FOLDERMERGEPLAN_H
#define LQCOMPARE_FOLDERMERGEPLAN_H

#include "foldercompare.h"

namespace LqCompare {
namespace FolderMerge {

struct Paths { QString base, left, right, output; };
enum class Decision { Unresolved, TakeLeft, TakeRight, TakeBase, Delete, Ignore };
enum class Conflict { None, Content, DeleteModify, Type, NoBase, Incomplete, Unsupported };
enum class DirectoryPolicy { RejectManualConflicts, PreserveManual, OverwriteManual };

QString decisionLabel(Decision decision);
QString conflictLabel(Conflict conflict);
QString kindLabel(Folder::Kind kind);

struct Entry {
    QString relativePath;
    Folder::Side base, left, right;
    Decision automaticDecision = Decision::Unresolved;
    Decision decision = Decision::Unresolved;
    Conflict conflict = Conflict::None;
    QString explanation;
    bool manual = false;
    bool blockedByAncestor = false;
    int descendantConflicts = 0;
    bool isDirectory() const;
    bool unresolved() const;
    bool canRequestTextMerge() const;
};

struct Plan {
    Paths paths;
    QVector<Entry> entries; // Lexical path order, parent before children.
    bool hasBase = false;
    bool complete = false;
    bool cancelled = false;
    QString error;
    QStringList warnings;
    int unresolvedCount() const;
    int manualCount() const;
    const Entry *find(const QString &relativePath) const;
    bool canExecute() const { return false; }
    QString executionDisabledReason() const;
    QString previewText() const;
};

// Read-only: no paths are created or modified. Folder::Options can intentionally
// limit scans for tests; incomplete scans never infer or accept deletions.
Plan buildPlan(const Paths &paths, const Folder::Options &options = {},
               const std::atomic_bool *cancelled = nullptr,
               const Folder::Progress &progress = {},
               const Files::FileSystem *fileSystem = nullptr);

// Directory decisions cover the subtree. Conflicting manual children cause an
// atomic rejection by default; callers can explicitly preserve or overwrite.
bool setDecision(Plan &plan, const QString &relativePath, Decision decision,
                 DirectoryPolicy policy = DirectoryPolicy::RejectManualConflicts,
                 QString *error = nullptr);
bool resetDecision(Plan &plan, const QString &relativePath, QString *error = nullptr);

class Scanner : public QObject {
    Q_OBJECT
public:
    explicit Scanner(QObject *parent = nullptr);
    ~Scanner() override;
    bool isRunning() const { return m_thread != nullptr; }
    bool start(const Paths &paths, const Folder::Options &options = {});
    void cancel();
signals:
    void progressChanged(int entries, const QString &relativePath);
    void finished(const LqCompare::FolderMerge::Plan &plan);
private:
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace FolderMerge
} // namespace LqCompare
Q_DECLARE_METATYPE(LqCompare::FolderMerge::Plan)
#endif
