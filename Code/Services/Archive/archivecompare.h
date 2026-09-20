#ifndef LQCOMPARE_ARCHIVECOMPARE_H
#define LQCOMPARE_ARCHIVECOMPARE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <functional>

namespace LqCompare { namespace Archive {

enum class ErrorCode {
    None, Io, UnknownFormat, Corrupt, Encrypted, Zip64, Unsupported,
    UnsafePath, DuplicatePath, LimitExceeded, Cancelled
};

struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
    QString entryPath;
    qint64 offset = -1;
    bool isError() const { return code != ErrorCode::None; }
};

// Budgets apply before allocating a directory or accepting declared sizes.
// No entry payload is decompressed, extracted, or CRC-verified by this module.
struct Limits {
    quint64 maxArchiveBytes = 1024ull * 1024 * 1024;
    quint64 maxCentralDirectoryBytes = 32ull * 1024 * 1024;
    int maxEntries = 50000; // Includes synthesized parent directories.
    int maxPathBytes = 4096;
    int maxPathDepth = 128;
    quint64 maxEntryUncompressedBytes = 512ull * 1024 * 1024;
    quint64 maxTotalUncompressedBytes = 2ull * 1024 * 1024 * 1024;
    quint64 maxCompressionRatio = 1000;
};

struct Entry {
    QString path; // NFC, '/' separators, no trailing slash, case-sensitive.
    QByteArray rawName;
    bool directory = false;
    bool implicitDirectory = false;
    quint64 uncompressedSize = 0;
    quint64 compressedSize = 0;
    quint32 crc32 = 0; // Claimed metadata, never proof of equal content.
    quint16 method = 0;
    QDateTime modified; // ZIP DOS wall-clock time, no source timezone.
    quint32 localHeaderOffset = 0;
};

struct Directory {
    QString sourcePath;
    QVector<Entry> entries; // Sorted by normalized case-sensitive path.
    quint64 totalUncompressedSize = 0;
    quint64 totalCompressedSize = 0;
    int storedEntryCount = 0;
    Error error;
    bool ok() const { return !error.isError(); }
};

// Reading is transactional: any structural/safety error returns no entries.
// Signature-based ZIP/JAR reader; supports stored/deflate metadata only.
// UTF-8 and CP437 names are supported; no heuristic GBK detection.
Directory readZip(const QString &path, const Limits &limits = Limits(),
                  const std::function<bool()> &isCancelled = {});

// Also useful to callers validating archive member paths before any write.
// Rejects traversal, absolute/drive/UNC paths, controls, ambiguous Windows names.
bool normalizeEntryPath(const QString &raw, QString *normalized, QString *reason = nullptr,
                        const Limits &limits = Limits(), ErrorCode *errorCode = nullptr);

enum class Difference {
    LeftOnly, RightOnly, TypeMismatch, SizeDifferent, CrcDifferent,
    MetadataDifferent, MatchingMetadata
};

struct Row {
    QString path;
    int leftIndex = -1;
    int rightIndex = -1;
    Difference difference = Difference::MatchingMetadata;
    QString evidence;
};

struct Comparison {
    Directory left;
    Directory right;
    QVector<Row> rows;
    int differenceCount = 0;
    bool ok() const { return left.ok() && right.ok(); }
};

Comparison compare(const Directory &left, const Directory &right);
QString differenceLabel(Difference difference);
QString metadataNotice();

} } // namespace LqCompare::Archive

#endif
