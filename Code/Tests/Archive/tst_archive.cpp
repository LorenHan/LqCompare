#include <QtTest>

#include "archivecompare.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace LqCompare::Archive;

namespace {
const Entry *entry(const Directory &directory, const QString &path)
{
    for (const auto &value : directory.entries) {
        if (value.path == path)
            return &value;
    }
    return nullptr;
}

const Row *row(const Comparison &comparison, const QString &path)
{
    for (const auto &value : comparison.rows) {
        if (value.path == path)
            return &value;
    }
    return nullptr;
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool mentionsMetadata(const QString &text)
{
    return text.contains(QStringLiteral("元数据")) || text.contains(QStringLiteral("metadata"), Qt::CaseInsensitive);
}
}

class ArchiveTests : public QObject
{
    Q_OBJECT
    QTemporaryDir fixtures;

    QString fixture(const QString &name) const { return fixtures.filePath(name); }
    Directory read(const QString &name, const Limits &limits = {}) const
    {
        return readZip(fixture(name), limits);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(fixtures.isValid());
        QString python = qEnvironmentVariable("LQCOMPARE_PYTHON");
        if (python.isEmpty())
            python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            python = QStandardPaths::findExecutable(QStringLiteral("python"));
        QVERIFY2(!python.isEmpty(), "Python 3 standard library is required to create real ZIP fixtures; set LQCOMPARE_PYTHON.");
        const QString generator = QFINDTESTDATA("generate_fixtures.py");
        QVERIFY(!generator.isEmpty());
        QProcess process;
        process.start(python, {generator, QStringLiteral("--output"), fixtures.path(), QStringLiteral("--large")});
        QVERIFY2(process.waitForFinished(30000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        QVERIFY(QFileInfo::exists(fixture("large-50000.zip")));
    }

    void emptyArchive()
    {
        const auto result = read("empty.zip");
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.entries.isEmpty());
        QCOMPARE(result.storedEntryCount, 0);
        QCOMPARE(result.totalUncompressedSize, quint64(0));
        QCOMPARE(result.totalCompressedSize, quint64(0));
    }

    void storedEntriesAndImplicitParents()
    {
        const auto result = read("stored.zip");
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.storedEntryCount, 4);
        QCOMPARE(result.entries.size(), 6);
        QStringList names;
        for (const auto &item : result.entries)
            names.append(item.path);
        QCOMPARE(names, QStringList({"a", "a/deep", "a/deep/readme.txt", "explicit", "z.txt", "zero.bin"}));
        QVERIFY(entry(result, "a")->directory);
        QVERIFY(entry(result, "a")->implicitDirectory);
        QVERIFY(entry(result, "a/deep")->implicitDirectory);
        QVERIFY(entry(result, "explicit")->directory);
        QVERIFY(!entry(result, "explicit")->implicitDirectory);
        const Entry *file = entry(result, "a/deep/readme.txt");
        QVERIFY(file);
        QVERIFY(!file->directory);
        QCOMPARE(file->uncompressedSize, quint64(6));
        QCOMPARE(file->compressedSize, quint64(6));
        QCOMPARE(file->crc32, quint32(0x363a3020));
        QCOMPARE(file->method, quint16(0));
        QCOMPARE(file->rawName, QByteArray("a/deep/readme.txt"));
        QCOMPARE(file->modified.date(), QDate(2024, 1, 2));
        QCOMPARE(file->modified.time(), QTime(3, 4, 6));
        QCOMPARE(result.totalUncompressedSize, quint64(10));
        QCOMPARE(result.totalCompressedSize, quint64(10));
    }

    void deflateMetadata()
    {
        const auto result = read("deflated.zip");
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.entries.size(), 1);
        QCOMPARE(result.entries.first().method, quint16(8));
        QCOMPARE(result.entries.first().uncompressedSize, quint64(1800));
        QVERIFY(result.entries.first().compressedSize < 100);
        QCOMPARE(result.totalUncompressedSize, quint64(1800));
    }

    void acceptedVariants_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("entries");
        QTest::newRow("signed-data-descriptor") << "descriptor.zip" << 1;
        QTest::newRow("unsigned-data-descriptor") << "descriptor-unsigned.zip" << 1;
        QTest::newRow("comment-contains-signature") << "comment-signatures.zip" << 1;
        QTest::newRow("comment-contains-full-fake-eocd") << "comment-full-eocd.zip" << 1;
        QTest::newRow("comment-contains-fake-zip64-locator") << "comment-fake-locator.zip" << 1;
        QTest::newRow("deflated-empty-directory") << "deflated-directory.zip" << 1;
        QTest::newRow("jar") << "signature.jar" << 6;
        QTest::newRow("signature-over-extension") << "signature.data" << 6;
        QTest::newRow("nested-payload-is-not-expanded") << "nested.zip" << 1;
        QTest::newRow("explicit-parent-after-child") << "explicit-parent-after-child.zip" << 2;
    }

    void acceptedVariants()
    {
        QFETCH(QString, name);
        QFETCH(int, entries);
        const auto result = read(name);
        QVERIFY2(result.ok(), qPrintable(name + ": " + result.error.message));
        QCOMPARE(result.entries.size(), entries);
        if (name == "explicit-parent-after-child.zip") {
            QVERIFY(entry(result, "parent")->directory);
            QVERIFY(!entry(result, "parent")->implicitDirectory);
        }
    }

    void encodingsAndNormalization()
    {
        const auto utf8 = read("utf8.zip");
        QVERIFY2(utf8.ok(), qPrintable(utf8.error.message));
        QVERIFY(entry(utf8, QStringLiteral("资料/测试.txt")));
        QVERIFY(entry(utf8, QStringLiteral("café.txt")));
        QVERIFY(entry(utf8, QStringLiteral("😀.txt")));
        const auto cp437 = read("cp437.zip");
        QVERIFY2(cp437.ok(), qPrintable(cp437.error.message));
        QCOMPARE(cp437.entries.first().path, QStringLiteral("café.txt"));
        QCOMPARE(cp437.entries.first().rawName, QByteArray("caf\x82.txt"));
        const auto normalized = read("normalized.zip");
        QVERIFY2(normalized.ok(), qPrintable(normalized.error.message));
        QVERIFY(entry(normalized, "one/two/three.txt"));
        QVERIFY(entry(normalized, "Case"));
        QVERIFY(entry(normalized, "case"));
        QCOMPARE(normalized.entries.size(), 5);
    }

    void unicodePathExtraFields()
    {
        const auto unicode = read("unicode-extra.zip");
        QVERIFY2(unicode.ok(), qPrintable(unicode.error.message));
        QCOMPARE(unicode.entries.size(), 1);
        QCOMPARE(unicode.entries.first().path, QStringLiteral("中文.txt"));
        QCOMPARE(unicode.entries.first().rawName, QByteArray("legacy.txt"));
        const auto stale = read("unicode-stale.zip");
        QVERIFY2(stale.ok(), qPrintable(stale.error.message));
        QCOMPARE(stale.entries.first().path, QStringLiteral("legacy.txt"));
    }

    void normalizePaths_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("slashes-dots") << "a\\b/./file" << "a/b/file";
        QTest::newRow("directory-trailing-slash") << "a/b/" << "a/b";
        QTest::newRow("nfc") << QStringLiteral("cafe\u0301") << QStringLiteral("café");
        QTest::newRow("hidden") << ".git/config" << ".git/config";
        QTest::newRow("case-retained") << "Some/FILE" << "Some/FILE";
        QTest::newRow("unicode-surrogate-pair") << QStringLiteral("emoji/😀.txt") << QStringLiteral("emoji/😀.txt");
    }

    void normalizePaths()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QString normalized, reason;
        QVERIFY2(normalizeEntryPath(input, &normalized, &reason), qPrintable(reason));
        QCOMPARE(normalized, expected);
    }

    void rejectedArchive_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("expectedCode");
        auto add = [](const char *name, ErrorCode code) {
            QTest::newRow(name) << QString::fromLatin1(name) << int(code);
        };
        for (const auto *name : {"unsafe-traversal.zip", "unsafe-nested-traversal.zip",
                 "unsafe-backslash-traversal.zip", "unsafe-absolute.zip", "unsafe-drive.zip",
                 "unsafe-drive-relative.zip", "unsafe-unc.zip", "unsafe-colon.zip", "unsafe-reserved.zip",
                 "unsafe-reserved-number.zip", "unsafe-trailing-dot.zip", "unsafe-trailing-space.zip",
                 "unsafe-control.zip", "unsafe-only-dot.zip", "unsafe-nul.zip", "unsafe-empty.zip", "unsafe-link.zip",
                 "unsafe-format-language-tag.zip", "unsafe-format-tag-space.zip", "unsafe-format-tag-delete.zip",
                 "unicode-unsafe-alias.zip", "unicode-unsafe-raw.zip"})
            add(name, ErrorCode::UnsafePath);
        for (const auto *name : {"duplicate-exact.zip", "duplicate-nfc.zip", "duplicate-slash.zip",
                 "duplicate-dot.zip", "duplicate-file-directory.zip", "duplicate-ancestor.zip", "duplicate-ancestor-reverse.zip"})
            add(name, ErrorCode::DuplicatePath);
        for (const auto *name : {"encrypted.zip", "strong-encrypted.zip"})
            add(name, ErrorCode::Encrypted);
        for (const auto *name : {"zip64-count.zip", "zip64-directory-size.zip", "zip64-directory-offset.zip", "zip64-extra.zip"})
            add(name, ErrorCode::Zip64);
        for (const auto *name : {"unknown-method.zip", "bzip2-method.zip", "split.zip"})
            add(name, ErrorCode::Unsupported);
        for (const auto *name : {"invalid-utf8.zip", "truncated.zip", "truncated-central.zip",
                 "corrupt-local-method.zip", "corrupt-local-flags.zip", "corrupt-local-crc.zip",
                 "corrupt-local-size.zip", "corrupt-local-compressed-size.zip", "corrupt-local-signature.zip",
                 "corrupt-local-name.zip", "corrupt-local-offset.zip", "corrupt-data-bounds.zip",
                 "corrupt-overlap.zip", "corrupt-directory-size.zip", "corrupt-stored-size.zip",
                 "corrupt-count.zip", "corrupt-central-size.zip", "corrupt-extra.zip", "descriptor-bad-crc.zip",
                 "unicode-invalid.zip", "unicode-duplicate.zip", "unicode-utf8-disagreement.zip", "unicode-local-disagreement.zip"})
            add(name, ErrorCode::Corrupt);
        add("unicode-local-type-disagreement.zip", ErrorCode::Corrupt);
        add("unknown.zip", ErrorCode::UnknownFormat);
        add("does-not-exist.zip", ErrorCode::Io);
    }

    void rejectedArchive()
    {
        QFETCH(QString, name);
        QFETCH(int, expectedCode);
        const auto result = read(name);
        QVERIFY2(!result.ok(), qPrintable(name + " was accepted"));
        QVERIFY2(int(result.error.code) == expectedCode,
                 qPrintable(QString("%1: expected %2, got %3: %4").arg(name).arg(expectedCode)
                            .arg(int(result.error.code)).arg(result.error.message)));
        QVERIFY(!result.error.message.isEmpty());
        QVERIFY2(result.entries.isEmpty(), "Safety/structural failure must not expose a partial valid directory");
        QCOMPARE(result.totalUncompressedSize, quint64(0));
        QCOMPARE(result.totalCompressedSize, quint64(0));
    }

    void limits_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto *kind : {"archive-bytes", "central-bytes", "stored-entry-count", "implicit-entry-count",
                                "path-bytes", "decoded-path-bytes", "path-depth", "entry-bytes", "total-bytes", "compression-ratio"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }

    void limits()
    {
        QFETCH(QString, kind);
        Limits limits;
        QString name = "stored.zip";
        if (kind == "archive-bytes") limits.maxArchiveBytes = 30;
        if (kind == "central-bytes") limits.maxCentralDirectoryBytes = 20;
        if (kind == "stored-entry-count") limits.maxEntries = 3;
        if (kind == "implicit-entry-count") { limits.maxEntries = 3; name = "deep.zip"; }
        if (kind == "path-bytes") limits.maxPathBytes = 8;
        if (kind == "decoded-path-bytes") { limits.maxPathBytes = 8; name = "cp437.zip"; }
        if (kind == "path-depth") { limits.maxPathDepth = 3; name = "deep.zip"; }
        if (kind == "entry-bytes") limits.maxEntryUncompressedBytes = 5;
        if (kind == "total-bytes") limits.maxTotalUncompressedBytes = 9;
        if (kind == "compression-ratio") { limits.maxCompressionRatio = 10; name = "ratio.zip"; }
        const auto result = read(name, limits);
        QVERIFY2(!result.ok(), qPrintable(kind + " budget was ignored"));
        QVERIFY2(result.error.code == ErrorCode::LimitExceeded,
                 qPrintable(QString("%1 got error %2: %3").arg(kind).arg(int(result.error.code)).arg(result.error.message)));
        QVERIFY(!result.error.message.isEmpty());
        QVERIFY(result.entries.isEmpty());
    }

    void exactLimitsPermitArchive()
    {
        Limits limits;
        limits.maxEntries = 6;
        limits.maxEntryUncompressedBytes = 6;
        limits.maxTotalUncompressedBytes = 10;
        limits.maxPathBytes = 17;
        limits.maxPathDepth = 3;
        limits.maxArchiveBytes = quint64(QFileInfo(fixture("stored.zip")).size());
        const auto result = read("stored.zip", limits);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.entries.size(), 6);
    }

    void cancellationBeforeAndDuringRead()
    {
        auto result = readZip(fixture("stored.zip"), {}, [] { return true; });
        QCOMPARE(result.error.code, ErrorCode::Cancelled);
        QVERIFY(result.entries.isEmpty());
        int probes = 0;
        result = readZip(fixture("large-50000.zip"), {}, [&probes] { return ++probes > 30; });
        QCOMPARE(result.error.code, ErrorCode::Cancelled);
        QVERIFY(probes > 30);
        QVERIFY(result.entries.isEmpty());
        QCOMPARE(result.totalUncompressedSize, quint64(0));
    }

    void fiftyThousandEntriesHaveBoundedRuntime()
    {
        QElapsedTimer timer;
        timer.start();
        const auto result = read("large-50000.zip");
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.storedEntryCount, 50000);
        QCOMPARE(result.entries.size(), 50000);
        QCOMPARE(result.entries.first().path, QString("f00000"));
        QCOMPARE(result.entries.last().path, QString("f49999"));
        qInfo("Enumerated 50,000 real ZIP32 members in %lld ms", static_cast<long long>(elapsed));
        QVERIFY2(elapsed < 15000, "50,000-member enumeration exceeded 15 seconds; check for unbounded/quadratic work");
        Limits limits;
        limits.maxEntries = 49999;
        const auto limited = read("large-50000.zip", limits);
        QCOMPARE(limited.error.code, ErrorCode::LimitExceeded);
        QVERIFY(limited.entries.isEmpty());
    }

    void allComparisonStates()
    {
        const auto result = compare(read("compare-left.zip"), read("compare-right.zip"));
        QVERIFY2(result.ok(), qPrintable(result.left.error.message + result.right.error.message));
        QCOMPARE(result.rows.size(), 9);
        QCOMPARE(result.differenceCount, 6);
        QVERIFY(row(result, "same.txt"));
        QCOMPARE(row(result, "same.txt")->difference, Difference::MatchingMetadata);
        QCOMPARE(row(result, "nested")->difference, Difference::MatchingMetadata);
        QCOMPARE(row(result, "nested/path.txt")->difference, Difference::MatchingMetadata);
        QCOMPARE(row(result, "size.txt")->difference, Difference::SizeDifferent);
        QCOMPARE(row(result, "crc.txt")->difference, Difference::CrcDifferent);
        QCOMPARE(row(result, "metadata.txt")->difference, Difference::MetadataDifferent);
        QCOMPARE(row(result, "kind")->difference, Difference::TypeMismatch);
        QCOMPARE(row(result, "left-only.txt")->difference, Difference::LeftOnly);
        QCOMPARE(row(result, "right-only.txt")->difference, Difference::RightOnly);
        QCOMPARE(row(result, "left-only.txt")->rightIndex, -1);
        QCOMPARE(row(result, "right-only.txt")->leftIndex, -1);
        QString previous;
        for (const auto &item : result.rows) {
            QVERIFY(previous.isEmpty() || previous < item.path);
            previous = item.path;
            QVERIFY(!item.evidence.isEmpty());
            QVERIFY(!differenceLabel(item.difference).isEmpty());
        }
    }

    void crcCollisionIsOnlyMatchingMetadata()
    {
        const auto left = read("collision-left.zip");
        const auto right = read("collision-right.zip");
        QVERIFY(left.ok());
        QVERIFY(right.ok());
        QVERIFY(readBytes(fixture("collision-left.zip")) != readBytes(fixture("collision-right.zip")));
        QCOMPARE(left.entries.first().crc32, quint32(0x8c58dcdc));
        QCOMPARE(right.entries.first().crc32, quint32(0x8c58dcdc));
        QCOMPARE(left.entries.first().uncompressedSize, quint64(12));
        const auto result = compare(left, right);
        QCOMPARE(result.rows.first().difference, Difference::MatchingMetadata);
        QCOMPARE(result.differenceCount, 0);
        QVERIFY(mentionsMetadata(result.rows.first().evidence));
        QVERIFY(mentionsMetadata(differenceLabel(Difference::MatchingMetadata)));
        QVERIFY(!metadataNotice().isEmpty());
        QVERIFY(mentionsMetadata(metadataNotice()));
    }

    void missingImplicitDirectoryMetadataDoesNotCreateDifference()
    {
        const auto explicitDirectory = read("deflated-directory.zip");
        const auto implicitDirectory = read("implicit-directory.zip");
        QVERIFY(explicitDirectory.ok());
        QVERIFY(implicitDirectory.ok());
        for (const auto &comparison : {compare(explicitDirectory, implicitDirectory),
                                       compare(implicitDirectory, explicitDirectory)}) {
            QCOMPARE(comparison.rows.size(), 2);
            QCOMPARE(comparison.differenceCount, 1); // Only the actual file is one-sided.
            QVERIFY(row(comparison, "directory"));
            QCOMPARE(row(comparison, "directory")->difference, Difference::MatchingMetadata);
        }
    }

    void damagedPayloadCannotBecomeAnIntegrityClaim()
    {
        for (const auto &pair : {qMakePair(QString("payload-original.zip"), QString("payload-corrupted.zip")),
                                 qMakePair(QString("deflated.zip"), QString("deflate-corrupted.zip"))}) {
            const auto left = read(pair.first);
            const auto right = read(pair.second);
            QVERIFY2(left.ok(), qPrintable(left.error.message));
            QVERIFY2(right.ok(), qPrintable(right.error.message));
            QVERIFY(readBytes(fixture(pair.first)) != readBytes(fixture(pair.second)));
            const auto result = compare(left, right);
            QCOMPARE(result.rows.size(), 1);
            QCOMPARE(result.rows.first().difference, Difference::MatchingMetadata);
            QVERIFY(mentionsMetadata(result.rows.first().evidence));
        }
    }

    void errorSideDoesNotInventOneSidedDifferences()
    {
        const auto result = compare(read("stored.zip"), read("truncated.zip"));
        QVERIFY(!result.ok());
        QVERIFY(result.rows.isEmpty());
        QCOMPARE(result.differenceCount, 0);
    }

    void readingDoesNotWriteOrExtract()
    {
        const auto before = QDir(fixtures.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        const auto bytes = readBytes(fixture("stored.zip"));
        QVERIFY(read("stored.zip").ok());
        QVERIFY(!read("unsafe-traversal.zip").ok());
        QVERIFY(!read("unsafe-link.zip").ok());
        QCOMPARE(readBytes(fixture("stored.zip")), bytes);
        QCOMPARE(QDir(fixtures.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
        QVERIFY(!QFileInfo::exists(fixtures.filePath("a")));
        QVERIFY(!QFileInfo::exists(fixtures.filePath("accepted-before-error.txt")));
    }
};

QTEST_GUILESS_MAIN(ArchiveTests)
#include "tst_archive.moc"
