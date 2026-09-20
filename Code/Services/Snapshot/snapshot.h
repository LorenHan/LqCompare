#ifndef LQCOMPARE_SNAPSHOT_H
#define LQCOMPARE_SNAPSHOT_H

#include "foldercompare.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

namespace LqCompare {
namespace Snapshot {

constexpr int CurrentFormatVersion = 1;

struct Entry
{
    Folder::Kind kind = Folder::Kind::Missing;
    quint64 size = 0;
    qint64 modifiedNanoseconds = 0;
    quint32 attributes = 0;
    QByteArray sha256; // Lowercase hexadecimal, 64 ASCII characters, or empty.
};

// This is a read-only inventory, never a backup. sourceRoot is provenance only:
// loading/diffing a document never accesses that path or reconstructs file bytes.
struct Document
{
    int formatVersion = CurrentFormatVersion;
    QString sourceRoot;
    QDateTime capturedAtUtc;
    bool complete = false; // Entire recursive tree; false means absence is unknown.
    QString hashAlgorithm = QStringLiteral("none"); // "none" or "sha256"
    QMap<QString, Entry> entries; // Portable, validated relative paths using '/'.
};

struct CaptureOptions
{
    bool includeHashes = false;
    int maximumDepth = 128;
};

struct DocumentResult
{
    Document document;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

struct SaveResult
{
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

using Progress = std::function<void(int entries, const QString &relativePath)>;

DocumentResult capture(const QString &root, const CaptureOptions &options = {},
                       const std::atomic_bool *cancelled = nullptr,
                       const Progress &progress = {},
                       const Files::FileSystem *fileSystem = nullptr);
// Reuses scanner metadata without re-reading content. Only accepts a complete,
// error-free recursive result; never converts an incomplete scan into a baseline.
DocumentResult fromFolder(const Folder::Result &result, bool leftSide);
QString validate(const Document &document);
SaveResult save(const Document &document, const QString &path,
                const std::atomic_bool *cancelled = nullptr);
DocumentResult load(const QString &path);

enum class ContentState { NotApplicable, Unknown, Same, Different };
enum class ChangeFlag : quint32 {
    None = 0, Added = 0x1, Removed = 0x2, Content = 0x4,
    Size = 0x8, Time = 0x10, Attributes = 0x20, Type = 0x40
};
Q_DECLARE_FLAGS(ChangeFlags, ChangeFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(ChangeFlags)

struct Change
{
    QString relativePath;
    ChangeFlags flags;
    ContentState content = ContentState::NotApplicable;
    bool presenceUnknown = false; // Missing from an incomplete side, not added/deleted.
};

struct Difference
{
    QVector<Change> entries; // Includes unchanged metadata with unknown content.
    int added = 0;
    int removed = 0;
    int modified = 0;
    int contentUnknown = 0;
    int presenceUnknown = 0;
    QString error;
    QString summary() const;
};

Difference compare(const Document &before, const Document &after);

enum class Operation {
    CompareMetadata, CompareHashes, CompareBytes, CompareRules,
    CopyFileContents, ModifySource, DeleteSource
};
// UI/command consumers must use this gate for a snapshot side. No write or
// content-restoration API exists on Document itself.
bool allows(const Document &document, Operation operation, QString *reason = nullptr);
QString readOnlyNotice();

} // namespace Snapshot
} // namespace LqCompare

#endif
