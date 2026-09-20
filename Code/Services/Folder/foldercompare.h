#ifndef LQCOMPARE_FOLDERCOMPARE_H
#define LQCOMPARE_FOLDERCOMPARE_H

#include "filesystem.h"

#include <QObject>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace LqCompare {
namespace Folder {

// Content state is independent of modification times; no common ancestor is
// available, so this two-way comparison never guesses which side was edited.
enum class Status { Same, Different, LeftOnly, RightOnly, TypeConflict, Error, Unknown };
enum class Kind { Missing, File, Directory, SymbolicLink, Other };

QString statusLabel(Status status);

struct Side
{
    Files::FileInfo info;
    Kind kind = Kind::Missing;
    QString error;
    bool exists() const { return info.exists; }
};

struct Entry
{
    QString relativePath;
    Side left;
    Side right;
    Status status = Status::Unknown;
    QString explanation;
    qint64 firstDifference = -1;
    bool excludedByMask = false;
    bool hasIncludedDescendants = false;
    bool nameCaseDifference = false;
    QString filterReason;
    bool inComparison() const { return !excludedByMask || hasIncludedDescendants; }
    bool isDirectory() const;
    bool canCompareAsText() const;
};

struct Options
{
    bool recursive = true;
    bool compareContent = true;
    int maximumDepth = 128;
    QString scanMaskDeclaration;
    Qt::CaseSensitivity nameCaseSensitivity = Qt::CaseSensitive;
};

struct Result
{
    QString leftRoot;
    QString rightRoot;
    QVector<Entry> entries; // Deterministic, parent before children.
    QStringList warnings;
    QString error;
    bool cancelled = false;
    bool complete = true; // Complete within the declared scan-mask scope.
    int excludedCount = 0;
    QString scanMaskDeclaration;
};

using Progress = std::function<void(int entries, const QString &relativePath)>;

// Synchronous, UI-independent engine. Filesystem injection covers inaccessible
// entries without relying on the permissions of the user running a test.
Result compare(const QString &leftRoot, const QString &rightRoot,
               const Options &options = {}, const std::atomic_bool *cancelled = nullptr,
               const Progress &progress = {}, const Files::FileSystem *fileSystem = nullptr);

class Scanner : public QObject
{
    Q_OBJECT
public:
    explicit Scanner(QObject *parent = nullptr);
    ~Scanner() override;
    bool isRunning() const { return m_thread != nullptr; }
    bool start(const QString &leftRoot, const QString &rightRoot, const Options &options = {});
    void cancel();

signals:
    void progressChanged(int entries, const QString &relativePath);
    void finished(const LqCompare::Folder::Result &result);

private:
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace Folder
} // namespace LqCompare

Q_DECLARE_METATYPE(LqCompare::Folder::Result)

#endif
