#include "snapshot.h"

#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <limits>
#include <memory>

using namespace LqCompare;
namespace Snap = LqCompare::Snapshot;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray digest(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QJsonObject resign(QJsonObject object)
{
    object.remove(QStringLiteral("integrity"));
    object.insert(QStringLiteral("integrity"), QJsonObject{
        {QStringLiteral("algorithm"), QStringLiteral("sha256")},
        {QStringLiteral("value"), QString::fromLatin1(digest(QJsonDocument(object).toJson(QJsonDocument::Compact)))}});
    return object;
}

Snap::Entry fileEntry(const QByteArray &bytes = QByteArray("before"), bool hashed = false)
{
    Snap::Entry entry;
    entry.kind = Folder::Kind::File;
    entry.size = quint64(bytes.size());
    entry.modifiedNanoseconds = Q_INT64_C(1700000000000000123);
    entry.attributes = quint32(Files::FileAttribute::ReadOnly);
    if (hashed)
        entry.sha256 = digest(bytes);
    return entry;
}

Snap::Document document(bool hashed = false)
{
    Snap::Document result;
    result.sourceRoot = QStringLiteral("/unavailable/original/computer");
    result.capturedAtUtc = QDateTime::fromMSecsSinceEpoch(Q_INT64_C(1700000000123), Qt::UTC);
    result.complete = true;
    result.hashAlgorithm = hashed ? QStringLiteral("sha256") : QStringLiteral("none");
    result.entries.insert(QStringLiteral("file.txt"), fileEntry("before", hashed));
    return result;
}

const Snap::Change *findChange(const Snap::Difference &difference, const QString &path)
{
    for (const auto &change : difference.entries) {
        if (change.relativePath == path)
            return &change;
    }
    return nullptr;
}

Folder::Result folderResult()
{
    Folder::Result result;
    result.leftRoot = QStringLiteral("/offline/left");
    result.rightRoot = QStringLiteral("/offline/right");
    Folder::Entry entry;
    entry.relativePath = QStringLiteral("shared.txt");
    entry.status = Folder::Status::Different;
    entry.left.kind = Folder::Kind::File;
    entry.left.info.exists = true;
    entry.left.info.size = 13;
    entry.left.info.lastModified = Files::FileTime::fromNanosecondsSinceEpoch(123456789);
    entry.left.info.attributes = Files::FileAttribute::ReadOnly;
    entry.right.kind = Folder::Kind::File;
    entry.right.info.exists = true;
    entry.right.info.size = 27;
    entry.right.info.lastModified = Files::FileTime::fromNanosecondsSinceEpoch(987654321);
    entry.right.info.attributes = Files::FileAttribute::Hidden;
    result.entries.append(entry);

    Folder::Entry rightOnly;
    rightOnly.relativePath = QStringLiteral("right-only.txt");
    rightOnly.status = Folder::Status::RightOnly;
    rightOnly.right = entry.right;
    result.entries.append(rightOnly);
    return result;
}

// Forward every normal call to the real filesystem, changing only the exact
// failure under test. This exercises real bytes without chmod/root assumptions.
class FaultFileSystem final : public Files::FileSystem
{
public:
    FaultFileSystem() : native(Files::createNativeFileSystem()) {}
    QString inaccessibleDirectory;
    QString changedFile;
    mutable int changedFileStats = 0;

    Qt::CaseSensitivity caseSensitivity() const override { return native->caseSensitivity(); }
    QChar separator() const override { return native->separator(); }
    QString pathNormalize(const QString &path, Files::ErrorCode *error) const override
    { return native->pathNormalize(path, error); }
    bool isAbsolutePath(const QString &path) const override { return native->isAbsolutePath(path); }
    QString toNativePath(const QString &path) const override { return native->toNativePath(path); }
    Files::FileInfo stat(const QString &path, Files::ErrorCode *error) const override
    {
        auto info = native->stat(path, error);
        if (path == changedFile && ++changedFileStats > 1)
            ++info.size;
        return info;
    }
    QString linkTarget(const QString &path, Files::ErrorCode *error) const override
    { return native->linkTarget(path, error); }
    bool exists(const QString &path, Files::ErrorCode *error) const override
    { return native->exists(path, error); }
    QVector<Files::FileInfo> enumerateDirectory(const QString &path, Files::ErrorCode *error) const override
    {
        if (path == inaccessibleDirectory) {
            if (error) *error = Files::FileSystemError::PermissionDenied;
            return {};
        }
        return native->enumerateDirectory(path, error);
    }
    bool setTimes(const QString &path, const Files::FileTime &modified,
                  const Files::FileTime &accessed, Files::ErrorCode *error) const override
    { return native->setTimes(path, modified, accessed, error); }
    bool setAttributes(const QString &path, Files::FileAttributes attributes,
                       Files::ErrorCode *error) const override
    { return native->setAttributes(path, attributes, error); }
    QString platformName() const override { return native->platformName(); }

private:
    std::unique_ptr<Files::FileSystem> native;
};

} // namespace

class SnapshotTests : public QObject
{
    Q_OBJECT
private slots:
    void captureMetadata();
    void captureHashes();
    void captureInvalidRoot();
    void captureCancelledBeforeStart();
    void captureCancelledDuringProgress();
    void captureDepthLimitCannotBeComplete();
    void captureEnumerationFailureDiscardsPartialResult();
    void captureChangedFileDiscardsPartialResult();
    void captureSymbolicLinks();
    void saveLoadRoundTripWithoutContents();
    void saveLoadPreserves64BitIntegers();
    void saveCancelledPreservesTarget();
    void saveInvalidDocumentPreservesTarget();
    void loadTruncatedOrMissing();
    void loadRejectsInvalidFields_data();
    void loadRejectsInvalidFields();
    void loadIntegrityCorruption_data();
    void loadIntegrityCorruption();
    void loadAcceptsJsonWhitespace();
    void validateInvalidPath_data();
    void validateInvalidPath();
    void validateInvalidDocument_data();
    void validateInvalidDocument();
    void compareAddedRemovedAndModified();
    void compareEqualSizeHashes();
    void compareMetadataFlags_data();
    void compareMetadataFlags();
    void compareWithoutDigestKeepsContentUnknown();
    void compareIncompletePresenceIsUnknown();
    void compareTypeChange();
    void compareInvalidDocument();
    void deniesAllWriteAndContentOperations_data();
    void deniesAllWriteAndContentOperations();
    void permitsOnlyAvailableComparisons();
    void fromFolderSelectsSideAndMetadata();
    void fromFolderWarningDoesNotImplyFailure();
    void fromFolderTypeConflictRequiresCompleteSelectedTree();
    void fromFolderRejectsIncompleteInput_data();
    void fromFolderRejectsIncompleteInput();
};

void SnapshotTests::captureMetadata()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("nested")));
    const QByteArray bytes("inventory fixture\n");
    const QString path = root.filePath(QStringLiteral("nested/中文.txt"));
    QVERIFY(writeFile(path, bytes));
    std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
    const Files::FileInfo info = fs->stat(path);

    const Snap::DocumentResult result = Snap::capture(root.path());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.document.complete);
    QVERIFY(result.document.capturedAtUtc.isValid());
    QCOMPARE(result.document.hashAlgorithm, QStringLiteral("none"));
    QCOMPARE(result.document.entries.size(), 2);
    QCOMPARE(result.document.entries.value(QStringLiteral("nested")).kind, Folder::Kind::Directory);
    QVERIFY(result.document.entries.contains(QStringLiteral("nested/中文.txt")));
    const Snap::Entry captured = result.document.entries.value(QStringLiteral("nested/中文.txt"));
    QCOMPARE(captured.kind, Folder::Kind::File);
    QCOMPARE(captured.size, quint64(bytes.size()));
    QCOMPARE(captured.modifiedNanoseconds, info.lastModified.nanosecondsSinceEpoch());
    QCOMPARE(captured.attributes, quint32(info.attributes));
    QVERIFY(captured.sha256.isEmpty());
    QVERIFY2(Snap::validate(result.document).isEmpty(), qPrintable(Snap::validate(result.document)));
    QCOMPARE(readFile(path), bytes);
}

void SnapshotTests::captureHashes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray bytes = QByteArrayLiteral("hash fixture\0with binary content");
    QVERIFY(writeFile(root.filePath(QStringLiteral("binary.dat")), bytes));
    QVERIFY(writeFile(root.filePath(QStringLiteral("empty.dat")), {}));
    Snap::CaptureOptions options;
    options.includeHashes = true;
    const auto result = Snap::capture(root.path(), options);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.document.complete);
    QCOMPARE(result.document.hashAlgorithm, QStringLiteral("sha256"));
    QCOMPARE(result.document.entries.value(QStringLiteral("binary.dat")).sha256, digest(bytes));
    QCOMPARE(result.document.entries.value(QStringLiteral("empty.dat")).sha256, digest({}));
    QCOMPARE(result.document.entries.value(QStringLiteral("binary.dat")).sha256.size(), 64);
}

void SnapshotTests::captureInvalidRoot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto missing = Snap::capture(root.filePath(QStringLiteral("missing")));
    QVERIFY(!missing.ok());
    QVERIFY(!missing.error.isEmpty());
    QVERIFY(!missing.document.complete);
    const QString file = root.filePath(QStringLiteral("file.txt"));
    QVERIFY(writeFile(file, "data"));
    const auto ordinaryFile = Snap::capture(file);
    QVERIFY(!ordinaryFile.ok());
    QVERIFY(!ordinaryFile.error.isEmpty());
}

void SnapshotTests::captureCancelledBeforeStart()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath(QStringLiteral("untouched.txt")), "untouched"));
    std::atomic_bool cancelled(true);
    const auto result = Snap::capture(root.path(), {}, &cancelled);
    QVERIFY(!result.ok());
    QVERIFY(result.cancelled);
    QVERIFY(!result.document.complete);
    QVERIFY(result.document.entries.isEmpty());
    QCOMPARE(readFile(root.filePath(QStringLiteral("untouched.txt"))), QByteArray("untouched"));
}

void SnapshotTests::captureCancelledDuringProgress()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath(QStringLiteral("a.txt")), "a"));
    QVERIFY(writeFile(root.filePath(QStringLiteral("b.txt")), "b"));
    std::atomic_bool cancelled(false);
    int callbacks = 0;
    const auto result = Snap::capture(root.path(), {}, &cancelled,
                                     [&](int, const QString &) { ++callbacks; cancelled.store(true); });
    QVERIFY(callbacks > 0);
    QVERIFY(result.cancelled);
    QVERIFY(!result.ok());
    QVERIFY(!result.document.complete);
    QVERIFY(result.document.entries.isEmpty());
    QCOMPARE(readFile(root.filePath(QStringLiteral("a.txt"))), QByteArray("a"));
    QCOMPARE(readFile(root.filePath(QStringLiteral("b.txt"))), QByteArray("b"));
}

void SnapshotTests::captureDepthLimitCannotBeComplete()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("one/two/three")));
    QVERIFY(writeFile(root.filePath(QStringLiteral("one/two/three/file.txt")), "deep"));
    Snap::CaptureOptions options;
    options.maximumDepth = 1;
    const auto result = Snap::capture(root.path(), options);
    QVERIFY(!result.document.complete);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
}

void SnapshotTests::captureEnumerationFailureDiscardsPartialResult()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath(QStringLiteral("a.txt")), "a"));
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("z-denied")));
    FaultFileSystem fileSystem;
    fileSystem.inaccessibleDirectory = root.filePath(QStringLiteral("z-denied"));
    const auto result = Snap::capture(root.path(), {}, nullptr, {}, &fileSystem);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.document.complete);
    QVERIFY(result.document.entries.isEmpty());
}

void SnapshotTests::captureChangedFileDiscardsPartialResult()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath(QStringLiteral("changing.txt"));
    QVERIFY(writeFile(path, "real bytes"));
    FaultFileSystem fileSystem;
    fileSystem.changedFile = path;
    Snap::CaptureOptions options;
    options.includeHashes = true;
    const auto result = Snap::capture(root.path(), options, nullptr, {}, &fileSystem);
    QVERIFY(fileSystem.changedFileStats >= 2);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.document.complete);
    QVERIFY(result.document.entries.isEmpty());
    QCOMPARE(readFile(path), QByteArray("real bytes"));
}

void SnapshotTests::captureSymbolicLinks()
{
#ifdef Q_OS_UNIX
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(writeFile(outside.filePath(QStringLiteral("outside.txt")), "outside contents"));
    const QString link = root.filePath(QStringLiteral("linked-directory"));
    QVERIFY(QFile::link(outside.path(), link));
    Snap::CaptureOptions options;
    options.includeHashes = true;
    const auto child = Snap::capture(root.path(), options);
    QVERIFY2(child.ok(), qPrintable(child.error));
    QCOMPARE(child.document.entries.size(), 1);
    QCOMPARE(child.document.entries.value(QStringLiteral("linked-directory")).kind, Folder::Kind::SymbolicLink);
    QVERIFY(child.document.entries.value(QStringLiteral("linked-directory")).sha256.isEmpty());
    const auto linkedRoot = Snap::capture(link, options);
    QVERIFY(!linkedRoot.ok());
    QVERIFY(!linkedRoot.document.complete);
    QVERIFY(linkedRoot.document.entries.isEmpty());
#else
    QSKIP("This fixture requires native POSIX symbolic links.");
#endif
}

void SnapshotTests::saveLoadRoundTripWithoutContents()
{
    QTemporaryDir files;
    QTemporaryDir output;
    QVERIFY(files.isValid());
    QVERIFY(output.isValid());
    const QByteArray secret = QByteArrayLiteral("BODY-MUST-NOT-BE-IN-SNAPSHOT-9d41ad6f\0binary\xff");
    QVERIFY(writeFile(files.filePath(QStringLiteral("source.bin")), secret));
    Snap::CaptureOptions options;
    options.includeHashes = true;
    const auto captured = Snap::capture(files.path(), options);
    QVERIFY2(captured.ok(), qPrintable(captured.error));
    const QString target = output.filePath(QStringLiteral("snapshot.json"));
    const auto saved = Snap::save(captured.document, target);
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    const QByteArray serialized = readFile(target);
    QVERIFY(!serialized.contains("BODY-MUST-NOT-BE-IN-SNAPSHOT"));
    QVERIFY(!serialized.contains(secret.toBase64()));
    QVERIFY(!serialized.contains(secret.toHex()));
    QVERIFY(QJsonDocument::fromJson(serialized).isObject());

    // Import must work after the source disappeared, without restoring bytes.
    QVERIFY(QFile::remove(files.filePath(QStringLiteral("source.bin"))));
    const auto loaded = Snap::load(target);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.document.sourceRoot, captured.document.sourceRoot);
    QCOMPARE(loaded.document.capturedAtUtc, captured.document.capturedAtUtc);
    QCOMPARE(loaded.document.complete, captured.document.complete);
    QCOMPARE(loaded.document.hashAlgorithm, captured.document.hashAlgorithm);
    QCOMPARE(loaded.document.entries.keys(), captured.document.entries.keys());
    const auto before = captured.document.entries.first();
    const auto after = loaded.document.entries.first();
    QCOMPARE(after.kind, before.kind);
    QCOMPARE(after.size, before.size);
    QCOMPARE(after.modifiedNanoseconds, before.modifiedNanoseconds);
    QCOMPARE(after.attributes, before.attributes);
    QCOMPARE(after.sha256, before.sha256);
    QVERIFY(!QFile::exists(files.filePath(QStringLiteral("source.bin"))));
}

void SnapshotTests::saveLoadPreserves64BitIntegers()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    auto original = document();
    original.entries[QStringLiteral("file.txt")].size = std::numeric_limits<quint64>::max();
    original.entries[QStringLiteral("file.txt")].modifiedNanoseconds = std::numeric_limits<qint64>::max();
    original.entries.insert(QStringLiteral("earliest.txt"), fileEntry());
    original.entries[QStringLiteral("earliest.txt")].size = Q_UINT64_C(9007199254740993);
    original.entries[QStringLiteral("earliest.txt")].modifiedNanoseconds = std::numeric_limits<qint64>::min();
    const QString path = output.filePath(QStringLiteral("wide.json"));
    const auto saved = Snap::save(original, path);
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    const auto loaded = Snap::load(path);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    for (auto it = original.entries.cbegin(); it != original.entries.cend(); ++it) {
        QCOMPARE(loaded.document.entries.value(it.key()).size, it.value().size);
        QCOMPARE(loaded.document.entries.value(it.key()).modifiedNanoseconds, it.value().modifiedNanoseconds);
    }
}

void SnapshotTests::saveCancelledPreservesTarget()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString path = output.filePath(QStringLiteral("snapshot.json"));
    const QByteArray original("existing valid backup must survive cancellation");
    QVERIFY(writeFile(path, original));
    std::atomic_bool cancelled(true);
    const auto result = Snap::save(document(), path, &cancelled);
    QVERIFY(result.cancelled);
    QVERIFY(!result.ok());
    QCOMPARE(readFile(path), original);
    const QString missing = output.filePath(QStringLiteral("cancelled.json"));
    QVERIFY(!Snap::save(document(), missing, &cancelled).ok());
    QVERIFY(!QFile::exists(missing));
    QCOMPARE(QDir(output.path()).entryList(QDir::Files), QStringList({QStringLiteral("snapshot.json")}));
}

void SnapshotTests::saveInvalidDocumentPreservesTarget()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString path = output.filePath(QStringLiteral("snapshot.json"));
    const QByteArray original("previous snapshot");
    QVERIFY(writeFile(path, original));
    auto invalid = document();
    invalid.entries.insert(QStringLiteral("../escape"), fileEntry());
    const auto result = Snap::save(invalid, path);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFile(path), original);
}

void SnapshotTests::loadTruncatedOrMissing()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString path = output.filePath(QStringLiteral("snapshot.json"));
    QVERIFY(!Snap::load(path).ok());
    const auto saved = Snap::save(document(true), path);
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    const QByteArray good = readFile(path);
    QVERIFY(writeFile(path, good.left(good.size() / 2)));
    const auto truncated = Snap::load(path);
    QVERIFY(!truncated.ok());
    QVERIFY(!truncated.error.isEmpty());
    QVERIFY(!truncated.document.complete);
    QVERIFY(writeFile(path, "{}"));
    const auto missingFields = Snap::load(path);
    QVERIFY(!missingFields.ok());
    QVERIFY(!missingFields.error.isEmpty());
}

void SnapshotTests::loadRejectsInvalidFields_data()
{
    QTest::addColumn<QString>("mutation");
    const QStringList mutations{
        QStringLiteral("future-version"), QStringLiteral("zero-version"),
        QStringLiteral("version-string"), QStringLiteral("format-name"),
        QStringLiteral("extra-envelope-field"), QStringLiteral("missing-source-root"),
        QStringLiteral("empty-source-root"), QStringLiteral("extra-payload-field"),
        QStringLiteral("complete-string"), QStringLiteral("invalid-capture-time"),
        QStringLiteral("capture-time-local"), QStringLiteral("invalid-hash-algorithm"),
        QStringLiteral("entries-object"), QStringLiteral("entry-not-object"),
        QStringLiteral("duplicate-path"), QStringLiteral("missing-path"),
        QStringLiteral("unsafe-path"), QStringLiteral("orphan-parent"),
        QStringLiteral("parent-is-file"), QStringLiteral("unknown-kind"),
        QStringLiteral("missing-size"), QStringLiteral("extra-entry-field"),
        QStringLiteral("numeric-size"), QStringLiteral("negative-size"),
        QStringLiteral("overflow-size"), QStringLiteral("noncanonical-size"),
        QStringLiteral("numeric-time"), QStringLiteral("overflow-time"),
        QStringLiteral("noncanonical-time"), QStringLiteral("negative-attributes"),
        QStringLiteral("fractional-attributes"), QStringLiteral("unknown-attributes"),
        QStringLiteral("string-attributes"), QStringLiteral("missing-hash"),
        QStringLiteral("short-hash"), QStringLiteral("nonhex-hash"),
        QStringLiteral("uppercase-hash"), QStringLiteral("directory-with-hash"),
        QStringLiteral("symlink-without-attribute"), QStringLiteral("file-with-symlink-attribute")
    };
    for (const auto &mutation : mutations)
        QTest::newRow(qPrintable(mutation)) << mutation;
}

void SnapshotTests::loadRejectsInvalidFields()
{
    QFETCH(QString, mutation);
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString path = output.filePath(QStringLiteral("invalid.json"));
    const auto saved = Snap::save(document(true), path);
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    QJsonObject object = QJsonDocument::fromJson(readFile(path)).object();
    QJsonObject payload = object.value(QStringLiteral("payload")).toObject();
    QJsonArray entries = payload.value(QStringLiteral("entries")).toArray();
    QVERIFY(!entries.isEmpty());
    QJsonObject entry = entries.first().toObject();

    if (mutation == QLatin1String("future-version")) object[QStringLiteral("version")] = Snap::CurrentFormatVersion + 1;
    else if (mutation == QLatin1String("zero-version")) object[QStringLiteral("version")] = 0;
    else if (mutation == QLatin1String("version-string")) object[QStringLiteral("version")] = QStringLiteral("1");
    else if (mutation == QLatin1String("format-name")) object[QStringLiteral("format")] = QStringLiteral("other.snapshot");
    else if (mutation == QLatin1String("extra-envelope-field")) object[QStringLiteral("content")] = QStringLiteral("forbidden");
    else if (mutation == QLatin1String("missing-source-root")) payload.remove(QStringLiteral("sourceRoot"));
    else if (mutation == QLatin1String("empty-source-root")) payload[QStringLiteral("sourceRoot")] = QString();
    else if (mutation == QLatin1String("extra-payload-field")) payload[QStringLiteral("content")] = QStringLiteral("forbidden");
    else if (mutation == QLatin1String("complete-string")) payload[QStringLiteral("complete")] = QStringLiteral("true");
    else if (mutation == QLatin1String("invalid-capture-time")) payload[QStringLiteral("capturedAtUtc")] = QStringLiteral("not-a-time");
    else if (mutation == QLatin1String("capture-time-local")) payload[QStringLiteral("capturedAtUtc")] = QStringLiteral("2023-11-14T22:13:20.123");
    else if (mutation == QLatin1String("invalid-hash-algorithm")) payload[QStringLiteral("hashAlgorithm")] = QStringLiteral("md5");
    else if (mutation == QLatin1String("missing-path")) entry.remove(QStringLiteral("path"));
    else if (mutation == QLatin1String("unsafe-path")) entry[QStringLiteral("path")] = QStringLiteral("../escape");
    else if (mutation == QLatin1String("orphan-parent")) entry[QStringLiteral("path")] = QStringLiteral("missing/file.txt");
    else if (mutation == QLatin1String("unknown-kind")) entry[QStringLiteral("kind")] = QStringLiteral("future-kind");
    else if (mutation == QLatin1String("missing-size")) entry.remove(QStringLiteral("size"));
    else if (mutation == QLatin1String("extra-entry-field")) entry[QStringLiteral("contents")] = QStringLiteral("forbidden");
    else if (mutation == QLatin1String("numeric-size")) entry[QStringLiteral("size")] = 6;
    else if (mutation == QLatin1String("negative-size")) entry[QStringLiteral("size")] = QStringLiteral("-1");
    else if (mutation == QLatin1String("overflow-size")) entry[QStringLiteral("size")] = QStringLiteral("18446744073709551616");
    else if (mutation == QLatin1String("noncanonical-size")) entry[QStringLiteral("size")] = QStringLiteral("06");
    else if (mutation == QLatin1String("numeric-time")) entry[QStringLiteral("modifiedNanoseconds")] = 100;
    else if (mutation == QLatin1String("overflow-time")) entry[QStringLiteral("modifiedNanoseconds")] = QStringLiteral("9223372036854775808");
    else if (mutation == QLatin1String("noncanonical-time")) entry[QStringLiteral("modifiedNanoseconds")] = QStringLiteral("+123");
    else if (mutation == QLatin1String("negative-attributes")) entry[QStringLiteral("attributes")] = -1;
    else if (mutation == QLatin1String("fractional-attributes")) entry[QStringLiteral("attributes")] = 0.5;
    else if (mutation == QLatin1String("unknown-attributes")) entry[QStringLiteral("attributes")] = 65536;
    else if (mutation == QLatin1String("string-attributes")) entry[QStringLiteral("attributes")] = QStringLiteral("1");
    else if (mutation == QLatin1String("missing-hash")) entry.remove(QStringLiteral("sha256"));
    else if (mutation == QLatin1String("short-hash")) entry[QStringLiteral("sha256")] = QStringLiteral("12");
    else if (mutation == QLatin1String("nonhex-hash")) entry[QStringLiteral("sha256")] = QString(64, QLatin1Char('g'));
    else if (mutation == QLatin1String("uppercase-hash")) entry[QStringLiteral("sha256")] = QString(64, QLatin1Char('A'));
    else if (mutation == QLatin1String("directory-with-hash")) entry[QStringLiteral("kind")] = QStringLiteral("directory");
    else if (mutation == QLatin1String("symlink-without-attribute")) {
        entry[QStringLiteral("kind")] = QStringLiteral("symlink");
        entry[QStringLiteral("sha256")] = QString();
    } else if (mutation == QLatin1String("file-with-symlink-attribute")) {
        entry[QStringLiteral("attributes")] = int(Files::FileAttribute::SymLink);
    }

    entries[0] = entry;
    if (mutation == QLatin1String("duplicate-path")) entries.append(entry);
    else if (mutation == QLatin1String("entry-not-object")) entries[0] = true;
    else if (mutation == QLatin1String("parent-is-file")) {
        auto child = entry;
        child[QStringLiteral("path")] = QStringLiteral("file.txt/child.txt");
        entries.append(child);
    }
    payload[QStringLiteral("entries")] = entries;
    if (mutation == QLatin1String("entries-object")) payload[QStringLiteral("entries")] = QJsonObject{};
    object[QStringLiteral("payload")] = payload;
    // Re-sign corrupted fields: rejection must exercise schema validation,
    // rather than every row merely hitting the integrity mismatch branch.
    QVERIFY(writeFile(path, QJsonDocument(resign(object)).toJson()));
    const auto loaded = Snap::load(path);
    QVERIFY2(!loaded.ok(), qPrintable(mutation));
    QVERIFY(!loaded.error.isEmpty());
    QVERIFY(!loaded.document.complete);
    QVERIFY(loaded.document.entries.isEmpty());
}

void SnapshotTests::loadIntegrityCorruption_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("tampered-payload") << 0;
    QTest::newRow("tampered-checksum") << 1;
    QTest::newRow("missing-integrity") << 2;
    QTest::newRow("unknown-integrity-algorithm") << 3;
    QTest::newRow("missing-integrity-value") << 4;
    QTest::newRow("malformed-integrity-value") << 5;
}

void SnapshotTests::loadIntegrityCorruption()
{
    QFETCH(int, mutation);
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString path = output.filePath(QStringLiteral("corrupt.json"));
    QVERIFY(Snap::save(document(), path).ok());
    QJsonObject object = QJsonDocument::fromJson(readFile(path)).object();
    if (mutation == 0) {
        auto payload = object[QStringLiteral("payload")].toObject();
        payload[QStringLiteral("sourceRoot")] = QStringLiteral("/tampered-root");
        object[QStringLiteral("payload")] = payload;
    } else if (mutation == 2) {
        object.remove(QStringLiteral("integrity"));
    } else {
        auto integrity = object[QStringLiteral("integrity")].toObject();
        if (mutation == 1) integrity[QStringLiteral("value")] = QString(64, QLatin1Char('0'));
        else if (mutation == 3) integrity[QStringLiteral("algorithm")] = QStringLiteral("none");
        else if (mutation == 4) integrity.remove(QStringLiteral("value"));
        else if (mutation == 5) integrity[QStringLiteral("value")] = QStringLiteral("broken");
        object[QStringLiteral("integrity")] = integrity;
    }
    QVERIFY(writeFile(path, QJsonDocument(object).toJson()));
    const auto loaded = Snap::load(path);
    QVERIFY(!loaded.ok());
    QVERIFY(!loaded.error.isEmpty());
    QVERIFY(!loaded.document.complete);
}

void SnapshotTests::loadAcceptsJsonWhitespace()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QString path = output.filePath(QStringLiteral("compact.json"));
    QVERIFY(Snap::save(document(true), path).ok());
    const auto json = QJsonDocument::fromJson(readFile(path));
    QVERIFY(writeFile(path, json.toJson(QJsonDocument::Compact)));
    const auto result = Snap::load(path);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.document.entries.first().sha256, document(true).entries.first().sha256);
}

void SnapshotTests::validateInvalidPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::newRow("empty") << QString();
    QTest::newRow("absolute") << QStringLiteral("/absolute.txt");
    QTest::newRow("traversal") << QStringLiteral("../outside.txt");
    QTest::newRow("inner-traversal") << QStringLiteral("a/../outside.txt");
    QTest::newRow("dot") << QStringLiteral(".");
    QTest::newRow("inner-dot") << QStringLiteral("a/./file.txt");
    QTest::newRow("repeated-separator") << QStringLiteral("a//file.txt");
    QTest::newRow("trailing-separator") << QStringLiteral("a/");
    QTest::newRow("windows-absolute") << QStringLiteral("C:/outside.txt");
    QTest::newRow("windows-relative") << QStringLiteral("C:outside.txt");
    QTest::newRow("backslash") << QStringLiteral("a\\file.txt");
    QTest::newRow("nul") << (QStringLiteral("a") + QChar(0) + QStringLiteral("b"));
}

void SnapshotTests::validateInvalidPath()
{
    QFETCH(QString, path);
    auto invalid = document();
    invalid.entries.clear();
    invalid.entries.insert(path, fileEntry());
    QVERIFY2(!Snap::validate(invalid).isEmpty(), qPrintable(path));
}

void SnapshotTests::validateInvalidDocument_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("future-version") << 0;
    QTest::newRow("invalid-time") << 1;
    QTest::newRow("unknown-hash-algorithm") << 2;
    QTest::newRow("short-digest") << 3;
    QTest::newRow("non-hex-digest") << 4;
    QTest::newRow("uppercase-digest") << 5;
    QTest::newRow("missing-kind") << 6;
    QTest::newRow("invalid-kind") << 7;
    QTest::newRow("hash-with-none-algorithm") << 8;
}

void SnapshotTests::validateInvalidDocument()
{
    QFETCH(int, mutation);
    auto invalid = document(true);
    switch (mutation) {
    case 0: invalid.formatVersion = Snap::CurrentFormatVersion + 1; break;
    case 1: invalid.capturedAtUtc = {}; break;
    case 2: invalid.hashAlgorithm = QStringLiteral("md5"); break;
    case 3: invalid.entries.first().sha256 = "12"; break;
    case 4: invalid.entries.first().sha256 = QByteArray(64, 'g'); break;
    case 5: invalid.entries.first().sha256 = QByteArray(64, 'A'); break;
    case 6: invalid.entries.first().kind = Folder::Kind::Missing; break;
    case 7: invalid.entries.first().kind = static_cast<Folder::Kind>(999); break;
    case 8: invalid.hashAlgorithm = QStringLiteral("none"); break;
    }
    QVERIFY(!Snap::validate(invalid).isEmpty());
}

void SnapshotTests::compareAddedRemovedAndModified()
{
    auto before = document(true);
    before.entries.insert(QStringLiteral("removed.txt"), fileEntry("removed", true));
    auto after = before;
    after.entries.remove(QStringLiteral("removed.txt"));
    after.entries.insert(QStringLiteral("added.txt"), fileEntry("added", true));
    auto &changed = after.entries[QStringLiteral("file.txt")];
    changed.sha256 = digest("longer and modified");
    changed.size = 19;
    ++changed.modifiedNanoseconds;
    changed.attributes ^= quint32(Files::FileAttribute::ReadOnly);
    const auto diff = Snap::compare(before, after);
    QVERIFY2(diff.error.isEmpty(), qPrintable(diff.error));
    QCOMPARE(diff.added, 1);
    QCOMPARE(diff.removed, 1);
    QCOMPARE(diff.modified, 1);
    QVERIFY(!diff.summary().isEmpty());
    const auto added = findChange(diff, QStringLiteral("added.txt"));
    const auto removed = findChange(diff, QStringLiteral("removed.txt"));
    const auto modified = findChange(diff, QStringLiteral("file.txt"));
    QVERIFY(added);
    QVERIFY(removed);
    QVERIFY(modified);
    QVERIFY(added->flags.testFlag(Snap::ChangeFlag::Added));
    QVERIFY(removed->flags.testFlag(Snap::ChangeFlag::Removed));
    QVERIFY(modified->flags.testFlag(Snap::ChangeFlag::Content));
    QVERIFY(modified->flags.testFlag(Snap::ChangeFlag::Size));
    QVERIFY(modified->flags.testFlag(Snap::ChangeFlag::Time));
    QVERIFY(modified->flags.testFlag(Snap::ChangeFlag::Attributes));
    QCOMPARE(modified->content, Snap::ContentState::Different);
}

void SnapshotTests::compareEqualSizeHashes()
{
    const auto before = document(true);
    auto after = before;
    after.entries.first().sha256 = digest("AFTER!"); // Same six-byte length.
    const auto changed = Snap::compare(before, after);
    QVERIFY2(changed.error.isEmpty(), qPrintable(changed.error));
    QCOMPARE(changed.modified, 1);
    const auto contentChange = findChange(changed, QStringLiteral("file.txt"));
    QVERIFY(contentChange);
    QCOMPARE(contentChange->content, Snap::ContentState::Different);
    QCOMPARE(contentChange->flags, Snap::ChangeFlags(Snap::ChangeFlag::Content));

    after = before;
    ++after.entries.first().modifiedNanoseconds;
    const auto sameContents = Snap::compare(before, after);
    QVERIFY(sameContents.error.isEmpty());
    const auto timeChange = findChange(sameContents, QStringLiteral("file.txt"));
    QVERIFY(timeChange);
    QCOMPARE(timeChange->content, Snap::ContentState::Same);
    QCOMPARE(timeChange->flags, Snap::ChangeFlags(Snap::ChangeFlag::Time));
    QCOMPARE(sameContents.contentUnknown, 0);
}

void SnapshotTests::compareMetadataFlags_data()
{
    QTest::addColumn<int>("flag");
    QTest::newRow("size-only") << int(Snap::ChangeFlag::Size);
    QTest::newRow("time-only") << int(Snap::ChangeFlag::Time);
    QTest::newRow("attributes-only") << int(Snap::ChangeFlag::Attributes);
}

void SnapshotTests::compareMetadataFlags()
{
    QFETCH(int, flag);
    const auto before = document();
    auto after = before;
    auto &entry = after.entries.first();
    switch (Snap::ChangeFlag(flag)) {
    case Snap::ChangeFlag::Size: ++entry.size; break;
    case Snap::ChangeFlag::Time: ++entry.modifiedNanoseconds; break;
    case Snap::ChangeFlag::Attributes: entry.attributes ^= quint32(Files::FileAttribute::ReadOnly); break;
    default: QFAIL("Invalid test row");
    }
    const auto diff = Snap::compare(before, after);
    QVERIFY2(diff.error.isEmpty(), qPrintable(diff.error));
    QCOMPARE(diff.modified, 1);
    const auto changed = findChange(diff, QStringLiteral("file.txt"));
    QVERIFY(changed);
    QVERIFY(changed->flags.testFlag(Snap::ChangeFlag(flag)));
    QVERIFY(!changed->flags.testFlag(Snap::ChangeFlag::Added));
    QVERIFY(!changed->flags.testFlag(Snap::ChangeFlag::Removed));
}

void SnapshotTests::compareWithoutDigestKeepsContentUnknown()
{
    const auto before = document();
    const auto diff = Snap::compare(before, before);
    QVERIFY2(diff.error.isEmpty(), qPrintable(diff.error));
    QCOMPARE(diff.added, 0);
    QCOMPARE(diff.removed, 0);
    QCOMPARE(diff.modified, 0);
    QCOMPARE(diff.contentUnknown, 1);
    const auto sameMetadata = findChange(diff, QStringLiteral("file.txt"));
    QVERIFY(sameMetadata);
    QCOMPARE(sameMetadata->content, Snap::ContentState::Unknown);
    QVERIFY(!sameMetadata->flags.testFlag(Snap::ChangeFlag::Content));

    // One side with a digest cannot establish content equality either.
    const auto mixed = Snap::compare(document(true), before);
    QVERIFY(mixed.error.isEmpty());
    QCOMPARE(mixed.contentUnknown, 1);
    const auto mixedChange = findChange(mixed, QStringLiteral("file.txt"));
    QVERIFY(mixedChange);
    QCOMPARE(mixedChange->content, Snap::ContentState::Unknown);
}

void SnapshotTests::compareIncompletePresenceIsUnknown()
{
    const auto complete = document(true);
    auto incomplete = complete;
    incomplete.complete = false;
    incomplete.entries.clear();
    const auto deletion = Snap::compare(complete, incomplete);
    QVERIFY2(deletion.error.isEmpty(), qPrintable(deletion.error));
    QCOMPARE(deletion.removed, 0);
    QCOMPARE(deletion.presenceUnknown, 1);
    const auto absentAfter = findChange(deletion, QStringLiteral("file.txt"));
    QVERIFY(absentAfter);
    QVERIFY(absentAfter->presenceUnknown);
    QVERIFY(!absentAfter->flags.testFlag(Snap::ChangeFlag::Removed));

    const auto addition = Snap::compare(incomplete, complete);
    QVERIFY(addition.error.isEmpty());
    QCOMPARE(addition.added, 0);
    QCOMPARE(addition.presenceUnknown, 1);
    const auto absentBefore = findChange(addition, QStringLiteral("file.txt"));
    QVERIFY(absentBefore);
    QVERIFY(absentBefore->presenceUnknown);
    QVERIFY(!absentBefore->flags.testFlag(Snap::ChangeFlag::Added));
}

void SnapshotTests::compareTypeChange()
{
    const auto before = document(true);
    auto after = before;
    after.entries.first().kind = Folder::Kind::Directory;
    after.entries.first().sha256.clear();
    const auto diff = Snap::compare(before, after);
    QVERIFY2(diff.error.isEmpty(), qPrintable(diff.error));
    QCOMPARE(diff.modified, 1);
    const auto change = findChange(diff, QStringLiteral("file.txt"));
    QVERIFY(change);
    QVERIFY(change->flags.testFlag(Snap::ChangeFlag::Type));
}

void SnapshotTests::compareInvalidDocument()
{
    auto invalid = document();
    invalid.formatVersion += 10;
    QVERIFY(!Snap::compare(invalid, document()).error.isEmpty());
    QVERIFY(!Snap::compare(document(), invalid).error.isEmpty());
}

void SnapshotTests::deniesAllWriteAndContentOperations_data()
{
    QTest::addColumn<int>("operation");
    QTest::newRow("byte-comparison") << int(Snap::Operation::CompareBytes);
    QTest::newRow("rule-comparison") << int(Snap::Operation::CompareRules);
    QTest::newRow("content-copy") << int(Snap::Operation::CopyFileContents);
    QTest::newRow("modify-source") << int(Snap::Operation::ModifySource);
    QTest::newRow("delete-source") << int(Snap::Operation::DeleteSource);
}

void SnapshotTests::deniesAllWriteAndContentOperations()
{
    QFETCH(int, operation);
    for (bool hashed : {false, true}) {
        QString reason;
        QVERIFY(!Snap::allows(document(hashed), Snap::Operation(operation), &reason));
        QVERIFY(!reason.isEmpty());
    }
    QVERIFY(!Snap::readOnlyNotice().isEmpty());
}

void SnapshotTests::permitsOnlyAvailableComparisons()
{
    QString reason;
    QVERIFY(Snap::allows(document(), Snap::Operation::CompareMetadata, &reason));
    QVERIFY(!Snap::allows(document(), Snap::Operation::CompareHashes, &reason));
    QVERIFY(!reason.isEmpty());
    QVERIFY(Snap::allows(document(true), Snap::Operation::CompareHashes, &reason));
}

void SnapshotTests::fromFolderSelectsSideAndMetadata()
{
    const auto source = folderResult();
    const auto left = Snap::fromFolder(source, true);
    const auto right = Snap::fromFolder(source, false);
    QVERIFY2(left.ok(), qPrintable(left.error));
    QVERIFY2(right.ok(), qPrintable(right.error));
    QVERIFY(left.document.complete);
    QVERIFY(right.document.complete);
    QCOMPARE(left.document.sourceRoot, source.leftRoot);
    QCOMPARE(right.document.sourceRoot, source.rightRoot);
    QCOMPARE(left.document.entries.size(), 1);
    QCOMPARE(right.document.entries.size(), 2);
    QCOMPARE(left.document.hashAlgorithm, QStringLiteral("none"));
    const auto entry = left.document.entries.value(QStringLiteral("shared.txt"));
    QCOMPARE(entry.size, quint64(13));
    QCOMPARE(entry.modifiedNanoseconds, qint64(123456789));
    QCOMPARE(entry.attributes, quint32(Files::FileAttribute::ReadOnly));
    QVERIFY(entry.sha256.isEmpty());
    QCOMPARE(right.document.entries.value(QStringLiteral("shared.txt")).size, quint64(27));
}

void SnapshotTests::fromFolderWarningDoesNotImplyFailure()
{
    auto source = folderResult();
    source.warnings.append(QStringLiteral("两侧是同一个文件夹"));
    const auto result = Snap::fromFolder(source, true);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.document.complete);
    QCOMPARE(result.document.entries.size(), 1);
}

void SnapshotTests::fromFolderTypeConflictRequiresCompleteSelectedTree()
{
    auto source = folderResult();
    source.entries.first().status = Folder::Status::TypeConflict;
    source.entries.first().left.kind = Folder::Kind::Directory;
    source.entries.first().left.info.isDirectory = true;
    const auto directorySide = Snap::fromFolder(source, true);
    QVERIFY(!directorySide.ok());
    QVERIFY(!directorySide.document.complete);
    QVERIFY(directorySide.document.entries.isEmpty());
    const auto fileSide = Snap::fromFolder(source, false);
    QVERIFY2(fileSide.ok(), qPrintable(fileSide.error));
    QVERIFY(fileSide.document.complete);
    QCOMPARE(fileSide.document.entries.value(QStringLiteral("shared.txt")).kind, Folder::Kind::File);
}

void SnapshotTests::fromFolderRejectsIncompleteInput_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("incomplete") << 0;
    QTest::newRow("cancelled") << 1;
    QTest::newRow("global-error") << 2;
    QTest::newRow("entry-unknown") << 3;
    QTest::newRow("entry-error") << 4;
    QTest::newRow("left-side-error") << 5;
    QTest::newRow("right-side-error") << 6;
    QTest::newRow("unsafe-path") << 7;
    QTest::newRow("scan-mask") << 8;
    QTest::newRow("excluded-count") << 9;
    QTest::newRow("excluded-entry") << 10;
    QTest::newRow("different-name-case") << 11;
}

void SnapshotTests::fromFolderRejectsIncompleteInput()
{
    QFETCH(int, mutation);
    auto source = folderResult();
    switch (mutation) {
    case 0: source.complete = false; break;
    case 1: source.cancelled = true; break;
    case 2: source.error = QStringLiteral("cannot enumerate"); break;
    case 3: source.entries.first().status = Folder::Status::Unknown; break;
    case 4: source.entries.first().status = Folder::Status::Error; break;
    case 5: source.entries.first().left.error = QStringLiteral("inaccessible"); break;
    case 6: source.entries.first().right.error = QStringLiteral("inaccessible"); break;
    case 7: source.entries.first().relativePath = QStringLiteral("../escape"); break;
    case 8: source.scanMaskDeclaration = QStringLiteral("*.txt"); break;
    case 9: source.excludedCount = 1; break;
    case 10: source.entries.first().excludedByMask = true; break;
    case 11: source.entries.first().nameCaseDifference = true; break;
    }
    const auto result = Snap::fromFolder(source, true);
    QVERIFY(!result.ok());
    QVERIFY(!result.document.complete);
    QVERIFY(result.cancelled || !result.error.isEmpty());
}

QTEST_GUILESS_MAIN(SnapshotTests)
#include "tst_snapshot.moc"
