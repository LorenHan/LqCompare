#include "syncbaseline.h"

#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <limits>

using namespace LqCompare;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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

struct Fixture
{
    QTemporaryDir directory;
    QString leftRoot;
    QString rightRoot;
    bool valid = false;
    Fixture()
    {
        leftRoot = directory.filePath(QStringLiteral("left"));
        rightRoot = directory.filePath(QStringLiteral("right"));
        valid = directory.isValid() && QDir().mkpath(leftRoot) && QDir().mkpath(rightRoot);
    }
};

Snapshot::Document document(const QString &root)
{
    Snapshot::Document out;
    out.sourceRoot = root;
    out.capturedAtUtc = QDateTime::fromMSecsSinceEpoch(Q_INT64_C(1700000000123), Qt::UTC);
    out.complete = true;
    out.hashAlgorithm = QStringLiteral("sha256");
    Snapshot::Entry entry;
    entry.kind = Folder::Kind::File;
    entry.size = 7;
    entry.modifiedNanoseconds = Q_INT64_C(1700000000000000123);
    entry.sha256 = digest("payload");
    out.entries.insert(QStringLiteral("file.txt"), entry);
    return out;
}

Sync::Options options()
{
    Sync::Options out;
    out.mode = Sync::Mode::TwoWay;
    return out;
}

Sync::Baseline baseline(const Fixture &fixture)
{
    Sync::Baseline out;
    out.leftRoot = fixture.leftRoot;
    out.rightRoot = fixture.rightRoot;
    out.complete = true;
    out.scopeKey = Sync::scopeKey(options());
    Sync::Fingerprint entry;
    entry.kind = Folder::Kind::File;
    entry.size = 7;
    entry.modifiedNs = Q_INT64_C(1700000000000000123);
    entry.createdNs = Q_INT64_C(1600000000000000321);
    entry.sha256 = digest("payload");
    out.entries.insert(QStringLiteral("file.txt"), entry);
    return out;
}

} // namespace

class SyncBaselineTests : public QObject
{
    Q_OBJECT
private slots:
    void createsCommonStateDespiteMetadataDifferences();
    void emptyCompleteTreesAreAValidCommonState();
    void bindsOptionsScope();
    void rejectsIncompatibleSnapshots_data();
    void rejectsIncompatibleSnapshots();
    void rejectsInvalidOptions_data();
    void rejectsInvalidOptions();
    void cancelledCreationHasNoBaseline();
    void validatesBaseline_data();
    void validatesBaseline();
    void realRoundTripDrivesTwoWayPreview();
    void invalidBaselineDowngradesPreview_data();
    void invalidBaselineDowngradesPreview();
    void roundTripPreserves64BitValues();
    void cancelledSavePreservesOriginal();
    void invalidSavePreservesOriginal();
    void rejectsTruncatedOrMissingFiles();
    void rejectsResignedInvalidJson_data();
    void rejectsResignedInvalidJson();
    void rejectsBrokenIntegrity_data();
    void rejectsBrokenIntegrity();
    void jsonWhitespaceDoesNotAffectIntegrity();
};

void SyncBaselineTests::createsCommonStateDespiteMetadataDifferences()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    auto left = document(fixture.leftRoot);
    auto right = document(fixture.rightRoot);
    right.entries.first().modifiedNanoseconds += 500000;
    right.entries.first().attributes = quint32(Files::FileAttribute::ReadOnly);
    Snapshot::Entry directory;
    directory.kind = Folder::Kind::Directory;
    directory.size = 10;
    directory.modifiedNanoseconds = 123;
    left.entries.insert(QStringLiteral("nested"), directory);
    directory.modifiedNanoseconds = 456;
    directory.size = 20; // Native directory sizes need not match across volumes.
    right.entries.insert(QStringLiteral("nested"), directory);
    const auto result = Sync::fromSnapshots(left, right, options());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.baseline.complete);
    QCOMPARE(result.baseline.leftRoot, fixture.leftRoot);
    QCOMPARE(result.baseline.rightRoot, fixture.rightRoot);
    QCOMPARE(result.baseline.scopeKey, Sync::scopeKey(options()));
    QCOMPARE(result.baseline.entries.keys(), left.entries.keys());
    QCOMPARE(result.baseline.entries.value(QStringLiteral("file.txt")).sha256, digest("payload"));
    QVERIFY2(Sync::validateBaseline(result.baseline).isEmpty(), qPrintable(Sync::validateBaseline(result.baseline)));
}

void SyncBaselineTests::emptyCompleteTreesAreAValidCommonState()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    auto left = document(fixture.leftRoot);
    auto right = document(fixture.rightRoot);
    left.entries.clear();
    right.entries.clear();
    const auto result = Sync::fromSnapshots(left, right, options());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.baseline.complete);
    QVERIFY(result.baseline.entries.isEmpty());
}

void SyncBaselineTests::bindsOptionsScope()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    const auto left = document(fixture.leftRoot);
    const auto right = document(fixture.rightRoot);
    auto firstOptions = options();
    auto secondOptions = firstOptions;
    secondOptions.direction = Sync::Direction::RightToLeft;
    const auto first = Sync::fromSnapshots(left, right, firstOptions);
    const auto second = Sync::fromSnapshots(left, right, secondOptions);
    QVERIFY2(first.ok(), qPrintable(first.error));
    QVERIFY2(second.ok(), qPrintable(second.error));
    QCOMPARE(first.baseline.scopeKey, Sync::scopeKey(firstOptions));
    QCOMPARE(second.baseline.scopeKey, Sync::scopeKey(secondOptions));
    QVERIFY(first.baseline.scopeKey != second.baseline.scopeKey);
    auto excluded = firstOptions;
    excluded.excludedPaths.append(QStringLiteral("file.txt"));
    const auto scoped = Sync::fromSnapshots(left, right, excluded);
    QVERIFY2(scoped.ok(), qPrintable(scoped.error));
    QCOMPARE(scoped.baseline.scopeKey, Sync::scopeKey(excluded));
    QVERIFY(scoped.baseline.scopeKey != first.baseline.scopeKey);
    QCOMPARE(scoped.baseline.entries.size(), 1); // Full common state stays intact.
}

void SyncBaselineTests::rejectsIncompatibleSnapshots_data()
{
    QTest::addColumn<QString>("mutation");
    const QStringList mutations{
        QStringLiteral("left-missing-entry"), QStringLiteral("right-missing-entry"),
        QStringLiteral("different-hash"), QStringLiteral("different-size"),
        QStringLiteral("different-kind"), QStringLiteral("left-metadata-only"),
        QStringLiteral("right-metadata-only"), QStringLiteral("left-incomplete"),
        QStringLiteral("right-incomplete"), QStringLiteral("symlink"),
        QStringLiteral("special-file"), QStringLiteral("unsafe-path"),
        QStringLiteral("case-collision"), QStringLiteral("unicode-collision"), QStringLiteral("different-path-case"),
        QStringLiteral("same-root"), QStringLiteral("left-parent"),
        QStringLiteral("right-parent"), QStringLiteral("relative-root"),
        QStringLiteral("empty-root"), QStringLiteral("invalid-snapshot-version")
    };
    for (const auto &mutation : mutations) QTest::newRow(qPrintable(mutation)) << mutation;
}

void SyncBaselineTests::rejectsIncompatibleSnapshots()
{
    QFETCH(QString, mutation);
    Fixture fixture;
    QVERIFY(fixture.valid);
    auto left = document(fixture.leftRoot);
    auto right = document(fixture.rightRoot);
    if (mutation == QLatin1String("left-missing-entry")) left.entries.clear();
    else if (mutation == QLatin1String("right-missing-entry")) right.entries.clear();
    else if (mutation == QLatin1String("different-hash")) right.entries.first().sha256 = digest("changed");
    else if (mutation == QLatin1String("different-size")) ++right.entries.first().size;
    else if (mutation == QLatin1String("different-kind")) {
        right.entries.first().kind = Folder::Kind::Directory;
        right.entries.first().sha256.clear();
    } else if (mutation == QLatin1String("left-metadata-only")) {
        left.hashAlgorithm = QStringLiteral("none"); left.entries.first().sha256.clear();
    } else if (mutation == QLatin1String("right-metadata-only")) {
        right.hashAlgorithm = QStringLiteral("none"); right.entries.first().sha256.clear();
    } else if (mutation == QLatin1String("left-incomplete")) left.complete = false;
    else if (mutation == QLatin1String("right-incomplete")) right.complete = false;
    else if (mutation == QLatin1String("symlink") || mutation == QLatin1String("special-file")) {
        for (auto *side : {&left, &right}) {
            side->entries.first().kind = mutation == QLatin1String("symlink") ? Folder::Kind::SymbolicLink : Folder::Kind::Other;
            side->entries.first().sha256.clear();
            if (mutation == QLatin1String("symlink")) side->entries.first().attributes = quint32(Files::FileAttribute::SymLink);
        }
    } else if (mutation == QLatin1String("unsafe-path")) {
        const auto entry = left.entries.take(QStringLiteral("file.txt"));
        left.entries.insert(QStringLiteral("../escape.txt"), entry);
        right.entries = left.entries;
    } else if (mutation == QLatin1String("case-collision")) {
        left.entries.insert(QStringLiteral("FILE.txt"), left.entries.first()); right.entries = left.entries;
    } else if (mutation == QLatin1String("unicode-collision")) {
        left.entries.insert(QStringLiteral("é.txt"), left.entries.first());
        left.entries.insert(QStringLiteral("e\u0301.txt"), left.entries.first());
        right.entries = left.entries;
    } else if (mutation == QLatin1String("different-path-case")) {
        const auto entry = right.entries.take(QStringLiteral("file.txt")); right.entries.insert(QStringLiteral("FILE.txt"), entry);
    } else if (mutation == QLatin1String("same-root")) right.sourceRoot = left.sourceRoot;
    else if (mutation == QLatin1String("left-parent")) left.sourceRoot = fixture.directory.path();
    else if (mutation == QLatin1String("right-parent")) right.sourceRoot = fixture.directory.path();
    else if (mutation == QLatin1String("relative-root")) left.sourceRoot = QStringLiteral("relative/left");
    else if (mutation == QLatin1String("empty-root")) left.sourceRoot.clear();
    else if (mutation == QLatin1String("invalid-snapshot-version")) ++left.formatVersion;
    const auto result = Sync::fromSnapshots(left, right, options());
    QVERIFY2(!result.ok(), qPrintable(mutation));
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.baseline.complete);
    QVERIFY(result.baseline.entries.isEmpty());
}

void SyncBaselineTests::rejectsInvalidOptions_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("invalid-mode") << 0;
    QTest::newRow("invalid-direction") << 1;
    QTest::newRow("invalid-deletion") << 2;
    QTest::newRow("negative-threshold") << 3;
    QTest::newRow("mirror-keep") << 4;
    QTest::newRow("unsafe-exclusion") << 5;
    QTest::newRow("absolute-exclusion") << 6;
}

void SyncBaselineTests::rejectsInvalidOptions()
{
    QFETCH(int, mutation);
    Fixture fixture;
    QVERIFY(fixture.valid);
    auto settings = options();
    switch (mutation) {
    case 0: settings.mode = static_cast<Sync::Mode>(99); break;
    case 1: settings.direction = static_cast<Sync::Direction>(99); break;
    case 2: settings.deletion = static_cast<Sync::Deletion>(99); break;
    case 3: settings.deleteCountThreshold = -1; break;
    case 4: settings.mode = Sync::Mode::Mirror; settings.deletion = Sync::Deletion::Keep; break;
    case 5: settings.excludedPaths.append(QStringLiteral("../outside")); break;
    case 6: settings.excludedPaths.append(QStringLiteral("/outside")); break;
    }
    const auto result = Sync::fromSnapshots(document(fixture.leftRoot), document(fixture.rightRoot), settings);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.baseline.complete);
}

void SyncBaselineTests::cancelledCreationHasNoBaseline()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    std::atomic_bool cancelled(true);
    const auto result = Sync::fromSnapshots(document(fixture.leftRoot), document(fixture.rightRoot), options(), &cancelled);
    QVERIFY(!result.ok());
    QVERIFY(result.cancelled);
    QVERIFY(!result.baseline.complete);
    QVERIFY(result.baseline.entries.isEmpty());
}

void SyncBaselineTests::validatesBaseline_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("incomplete") << 0;
    QTest::newRow("scope-key-empty") << 1;
    QTest::newRow("scope-key-nonhex") << 2;
    QTest::newRow("same-root") << 3;
    QTest::newRow("parent-root") << 4;
    QTest::newRow("relative-root") << 5;
    QTest::newRow("missing-kind") << 6;
    QTest::newRow("symlink") << 7;
    QTest::newRow("special-file") << 8;
    QTest::newRow("missing-hash") << 9;
    QTest::newRow("short-hash") << 10;
    QTest::newRow("uppercase-hash") << 11;
    QTest::newRow("error-fingerprint") << 12;
    QTest::newRow("unsafe-path") << 13;
    QTest::newRow("case-collision") << 14;
    QTest::newRow("orphan-parent") << 15;
    QTest::newRow("negative-attributes") << 16;
    QTest::newRow("unknown-attributes") << 17;
    QTest::newRow("unicode-collision") << 18;
}

void SyncBaselineTests::validatesBaseline()
{
    QFETCH(int, mutation);
    Fixture fixture;
    QVERIFY(fixture.valid);
    auto invalid = baseline(fixture);
    switch (mutation) {
    case 0: invalid.complete = false; break;
    case 1: invalid.scopeKey.clear(); break;
    case 2: invalid.scopeKey = QByteArray(64, 'g'); break;
    case 3: invalid.rightRoot = invalid.leftRoot; break;
    case 4: invalid.leftRoot = fixture.directory.path(); break;
    case 5: invalid.leftRoot = QStringLiteral("relative/left"); break;
    case 6: invalid.entries.first().kind = Folder::Kind::Missing; break;
    case 7: invalid.entries.first().kind = Folder::Kind::SymbolicLink; break;
    case 8: invalid.entries.first().kind = Folder::Kind::Other; break;
    case 9: invalid.entries.first().sha256.clear(); break;
    case 10: invalid.entries.first().sha256 = "12"; break;
    case 11: invalid.entries.first().sha256 = QByteArray(64, 'A'); break;
    case 12: invalid.entries.first().error = QStringLiteral("unreadable"); break;
    case 13: { const auto entry = invalid.entries.take(QStringLiteral("file.txt")); invalid.entries.insert(QStringLiteral("../escape"), entry); break; }
    case 14: invalid.entries.insert(QStringLiteral("FILE.txt"), invalid.entries.first()); break;
    case 15: { const auto entry = invalid.entries.take(QStringLiteral("file.txt")); invalid.entries.insert(QStringLiteral("missing/file.txt"), entry); break; }
    case 16: invalid.entries.first().attributes = -1; break;
    case 17: invalid.entries.first().attributes = 65536; break;
    case 18:
        invalid.entries.insert(QStringLiteral("é.txt"), invalid.entries.first());
        invalid.entries.insert(QStringLiteral("e\u0301.txt"), invalid.entries.first());
        break;
    }
    QVERIFY(!Sync::validateBaseline(invalid).isEmpty());
}

void SyncBaselineTests::realRoundTripDrivesTwoWayPreview()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QByteArray content("PRIVATE-PAYLOAD-NOT-STORED-IN-BASELINE");
    const QString leftFile = QDir(fixture.leftRoot).filePath(QStringLiteral("file.txt"));
    const QString rightFile = QDir(fixture.rightRoot).filePath(QStringLiteral("file.txt"));
    QVERIFY(writeFile(leftFile, content));
    QVERIFY(writeFile(rightFile, content));
    Snapshot::CaptureOptions captureOptions;
    captureOptions.includeHashes = true;
    const auto left = Snapshot::capture(fixture.leftRoot, captureOptions);
    const auto right = Snapshot::capture(fixture.rightRoot, captureOptions);
    QVERIFY2(left.ok(), qPrintable(left.error));
    QVERIFY2(right.ok(), qPrintable(right.error));
    const auto common = Sync::fromSnapshots(left.document, right.document, options());
    QVERIFY2(common.ok(), qPrintable(common.error));
    const QString path = fixture.directory.filePath(QStringLiteral("baseline.json"));
    const auto saved = Sync::saveBaseline(common.baseline, path);
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    const auto bytes = readFile(path);
    QVERIFY(QJsonDocument::fromJson(bytes).isObject());
    QVERIFY(!bytes.contains(content));
    QVERIFY(!bytes.contains(content.toBase64()));
    QVERIFY(!bytes.contains(content.toHex()));
    const auto loaded = Sync::loadBaseline(path);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.baseline.leftRoot, common.baseline.leftRoot);
    QCOMPARE(loaded.baseline.rightRoot, common.baseline.rightRoot);
    QCOMPARE(loaded.baseline.scopeKey, common.baseline.scopeKey);
    QCOMPARE(loaded.baseline.entries.first().sha256, digest(content));
    QCOMPARE(loaded.baseline.entries.first().size, quint64(content.size()));

    QVERIFY(writeFile(leftFile, "left changed after baseline"));
    const auto plan = Sync::preview(fixture.leftRoot, fixture.rightRoot, options(), &loaded.baseline);
    QVERIFY2(plan.executable(), qPrintable(plan.error));
    QVERIFY(plan.baselineUsed);
    QCOMPARE(plan.items.size(), 1);
    QCOMPARE(plan.items.first().relativePath, QStringLiteral("file.txt"));
    QCOMPARE(plan.items.first().action, Sync::Action::CopyLeftToRight);
    QCOMPARE(readFile(rightFile), content); // Preview cannot execute the copy.

    auto changedScope = options();
    changedScope.direction = Sync::Direction::RightToLeft;
    const auto noBaseline = Sync::preview(fixture.leftRoot, fixture.rightRoot, changedScope, &loaded.baseline);
    QVERIFY2(noBaseline.executable(), qPrintable(noBaseline.error));
    QVERIFY(!noBaseline.baselineUsed);
    QCOMPARE(noBaseline.items.first().action, Sync::Action::Conflict);
}

void SyncBaselineTests::invalidBaselineDowngradesPreview_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("nonhex-hash") << 0;
    QTest::newRow("fingerprint-error") << 1;
    QTest::newRow("unsafe-path") << 2;
}

void SyncBaselineTests::invalidBaselineDowngradesPreview()
{
    QFETCH(int, mutation);
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString leftFile = QDir(fixture.leftRoot).filePath(QStringLiteral("file.txt"));
    const QString rightFile = QDir(fixture.rightRoot).filePath(QStringLiteral("file.txt"));
    QVERIFY(writeFile(leftFile, "left changed"));
    QVERIFY(writeFile(rightFile, "payload"));
    auto corrupt = baseline(fixture);
    if (mutation == 0) corrupt.entries.first().sha256 = QByteArray(64, 'g');
    else if (mutation == 1) corrupt.entries.first().error = QStringLiteral("cannot read baseline entry");
    else {
        const auto entry = corrupt.entries.take(QStringLiteral("file.txt"));
        corrupt.entries.insert(QStringLiteral("../outside.txt"), entry);
    }
    const auto plan = Sync::preview(fixture.leftRoot, fixture.rightRoot, options(), &corrupt);
    QVERIFY2(plan.executable(), qPrintable(plan.error));
    QVERIFY(!plan.baselineUsed);
    QCOMPARE(plan.items.size(), 1);
    QCOMPARE(plan.items.first().action, Sync::Action::Conflict);
    QCOMPARE(plan.items.first().reasonCode, QStringLiteral("different-without-baseline"));
    QVERIFY(!plan.warnings.isEmpty());
    QCOMPARE(readFile(leftFile), QByteArray("left changed"));
    QCOMPARE(readFile(rightFile), QByteArray("payload"));
}

void SyncBaselineTests::roundTripPreserves64BitValues()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    auto original = baseline(fixture);
    original.entries.first().size = std::numeric_limits<quint64>::max();
    original.entries.first().modifiedNs = std::numeric_limits<qint64>::max();
    original.entries.first().createdNs = std::numeric_limits<qint64>::min();
    original.entries.insert(QStringLiteral("precise.txt"), original.entries.first());
    original.entries[QStringLiteral("precise.txt")].size = Q_UINT64_C(9007199254740993);
    original.entries[QStringLiteral("precise.txt")].modifiedNs = Q_INT64_C(9007199254740993);
    original.entries[QStringLiteral("precise.txt")].createdNs = -Q_INT64_C(9007199254740993);
    const QString path = fixture.directory.filePath(QStringLiteral("large-values.json"));
    const auto saved = Sync::saveBaseline(original, path);
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    const auto loaded = Sync::loadBaseline(path);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.baseline.entries.keys(), original.entries.keys());
    for (auto it = original.entries.cbegin(); it != original.entries.cend(); ++it) {
        const auto actual = loaded.baseline.entries.value(it.key());
        QCOMPARE(actual.kind, it->kind);
        QCOMPARE(actual.size, it->size);
        QCOMPARE(actual.modifiedNs, it->modifiedNs);
        QCOMPARE(actual.createdNs, it->createdNs);
        QCOMPARE(actual.attributes, it->attributes);
        QCOMPARE(actual.sha256, it->sha256);
    }
}

void SyncBaselineTests::cancelledSavePreservesOriginal()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString path = fixture.directory.filePath(QStringLiteral("baseline.json"));
    QVERIFY(Sync::saveBaseline(baseline(fixture), path).ok());
    const auto originalBytes = readFile(path);
    std::atomic_bool cancelled(true);
    const auto saved = Sync::saveBaseline(baseline(fixture), path, &cancelled);
    QVERIFY(!saved.ok());
    QVERIFY(saved.cancelled);
    QCOMPARE(readFile(path), originalBytes);
    const QString absent = fixture.directory.filePath(QStringLiteral("cancelled.json"));
    QVERIFY(Sync::saveBaseline(baseline(fixture), absent, &cancelled).cancelled);
    QVERIFY(!QFile::exists(absent));
    QCOMPARE(QDir(fixture.directory.path()).entryList(QDir::Files | QDir::Hidden), QStringList({QStringLiteral("baseline.json")}));
}

void SyncBaselineTests::invalidSavePreservesOriginal()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString path = fixture.directory.filePath(QStringLiteral("baseline.json"));
    QVERIFY(Sync::saveBaseline(baseline(fixture), path).ok());
    const auto originalBytes = readFile(path);
    auto invalid = baseline(fixture);
    invalid.complete = false;
    const auto result = Sync::saveBaseline(invalid, path);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFile(path), originalBytes);
}

void SyncBaselineTests::rejectsTruncatedOrMissingFiles()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString path = fixture.directory.filePath(QStringLiteral("missing.json"));
    const auto missing = Sync::loadBaseline(path);
    QVERIFY(!missing.ok());
    QVERIFY(!missing.error.isEmpty());
    QVERIFY(Sync::saveBaseline(baseline(fixture), path).ok());
    const auto good = readFile(path);
    QVERIFY(writeFile(path, good.left(good.size() / 2)));
    const auto truncated = Sync::loadBaseline(path);
    QVERIFY(!truncated.ok());
    QVERIFY(!truncated.error.isEmpty());
    QVERIFY(!truncated.baseline.complete);
    QVERIFY(truncated.baseline.entries.isEmpty());
    QVERIFY(writeFile(path, "{}"));
    QVERIFY(!Sync::loadBaseline(path).ok());
}

void SyncBaselineTests::rejectsResignedInvalidJson_data()
{
    QTest::addColumn<QString>("mutation");
    const QStringList mutations{
        QStringLiteral("future-version"), QStringLiteral("zero-version"), QStringLiteral("string-version"),
        QStringLiteral("wrong-format"), QStringLiteral("extra-envelope-field"),
        QStringLiteral("missing-left-root"), QStringLiteral("relative-left-root"),
        QStringLiteral("same-roots"), QStringLiteral("parent-roots"),
        QStringLiteral("missing-scope-key"), QStringLiteral("invalid-scope-key"),
        QStringLiteral("incomplete"), QStringLiteral("string-complete"),
        QStringLiteral("extra-payload-field"), QStringLiteral("entries-not-array"),
        QStringLiteral("entry-not-object"), QStringLiteral("duplicate-path"),
        QStringLiteral("case-collision"), QStringLiteral("unicode-collision"), QStringLiteral("missing-path"),
        QStringLiteral("unsafe-path"), QStringLiteral("orphan-parent"), QStringLiteral("parent-is-file"),
        QStringLiteral("unknown-kind"), QStringLiteral("symlink-kind"), QStringLiteral("other-kind"),
        QStringLiteral("missing-size"), QStringLiteral("numeric-size"), QStringLiteral("negative-size"),
        QStringLiteral("overflow-size"), QStringLiteral("noncanonical-size"),
        QStringLiteral("numeric-modified-time"), QStringLiteral("overflow-modified-time"),
        QStringLiteral("numeric-created-time"), QStringLiteral("overflow-created-time"),
        QStringLiteral("missing-created-time"), QStringLiteral("noncanonical-created-time"),
        QStringLiteral("negative-attributes"), QStringLiteral("fractional-attributes"),
        QStringLiteral("unknown-attributes"), QStringLiteral("string-attributes"),
        QStringLiteral("missing-hash"), QStringLiteral("short-hash"),
        QStringLiteral("nonhex-hash"), QStringLiteral("uppercase-hash"),
        QStringLiteral("directory-with-hash"), QStringLiteral("extra-entry-field")
    };
    for (const auto &mutation : mutations) QTest::newRow(qPrintable(mutation)) << mutation;
}

void SyncBaselineTests::rejectsResignedInvalidJson()
{
    QFETCH(QString, mutation);
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString path = fixture.directory.filePath(QStringLiteral("invalid.json"));
    QVERIFY(Sync::saveBaseline(baseline(fixture), path).ok());
    auto object = QJsonDocument::fromJson(readFile(path)).object();
    auto payload = object[QStringLiteral("payload")].toObject();
    auto entries = payload[QStringLiteral("entries")].toArray();
    QVERIFY(!entries.isEmpty());
    auto entry = entries.first().toObject();
    if (mutation == QLatin1String("future-version")) object[QStringLiteral("version")] = 2;
    else if (mutation == QLatin1String("zero-version")) object[QStringLiteral("version")] = 0;
    else if (mutation == QLatin1String("string-version")) object[QStringLiteral("version")] = QStringLiteral("1");
    else if (mutation == QLatin1String("wrong-format")) object[QStringLiteral("format")] = QStringLiteral("LqCompare.snapshot");
    else if (mutation == QLatin1String("extra-envelope-field")) object[QStringLiteral("contents")] = QStringLiteral("forbidden");
    else if (mutation == QLatin1String("missing-left-root")) payload.remove(QStringLiteral("leftRoot"));
    else if (mutation == QLatin1String("relative-left-root")) payload[QStringLiteral("leftRoot")] = QStringLiteral("relative/left");
    else if (mutation == QLatin1String("same-roots")) payload[QStringLiteral("rightRoot")] = payload[QStringLiteral("leftRoot")];
    else if (mutation == QLatin1String("parent-roots")) payload[QStringLiteral("leftRoot")] = fixture.directory.path();
    else if (mutation == QLatin1String("missing-scope-key")) payload.remove(QStringLiteral("scopeKey"));
    else if (mutation == QLatin1String("invalid-scope-key")) payload[QStringLiteral("scopeKey")] = QString(64, QLatin1Char('g'));
    else if (mutation == QLatin1String("incomplete")) payload[QStringLiteral("complete")] = false;
    else if (mutation == QLatin1String("string-complete")) payload[QStringLiteral("complete")] = QStringLiteral("true");
    else if (mutation == QLatin1String("extra-payload-field")) payload[QStringLiteral("contents")] = QStringLiteral("forbidden");
    else if (mutation == QLatin1String("missing-path")) entry.remove(QStringLiteral("path"));
    else if (mutation == QLatin1String("unsafe-path")) entry[QStringLiteral("path")] = QStringLiteral("../outside.txt");
    else if (mutation == QLatin1String("orphan-parent")) entry[QStringLiteral("path")] = QStringLiteral("missing/file.txt");
    else if (mutation == QLatin1String("unknown-kind")) entry[QStringLiteral("kind")] = QStringLiteral("unknown");
    else if (mutation == QLatin1String("symlink-kind")) entry[QStringLiteral("kind")] = QStringLiteral("symlink");
    else if (mutation == QLatin1String("other-kind")) entry[QStringLiteral("kind")] = QStringLiteral("other");
    else if (mutation == QLatin1String("missing-size")) entry.remove(QStringLiteral("size"));
    else if (mutation == QLatin1String("numeric-size")) entry[QStringLiteral("size")] = 7;
    else if (mutation == QLatin1String("negative-size")) entry[QStringLiteral("size")] = QStringLiteral("-1");
    else if (mutation == QLatin1String("overflow-size")) entry[QStringLiteral("size")] = QStringLiteral("18446744073709551616");
    else if (mutation == QLatin1String("noncanonical-size")) entry[QStringLiteral("size")] = QStringLiteral("07");
    else if (mutation == QLatin1String("numeric-modified-time")) entry[QStringLiteral("modifiedNs")] = 123;
    else if (mutation == QLatin1String("overflow-modified-time")) entry[QStringLiteral("modifiedNs")] = QStringLiteral("9223372036854775808");
    else if (mutation == QLatin1String("numeric-created-time")) entry[QStringLiteral("createdNs")] = 123;
    else if (mutation == QLatin1String("overflow-created-time")) entry[QStringLiteral("createdNs")] = QStringLiteral("-9223372036854775809");
    else if (mutation == QLatin1String("missing-created-time")) entry.remove(QStringLiteral("createdNs"));
    else if (mutation == QLatin1String("noncanonical-created-time")) entry[QStringLiteral("createdNs")] = QStringLiteral("+123");
    else if (mutation == QLatin1String("negative-attributes")) entry[QStringLiteral("attributes")] = -1;
    else if (mutation == QLatin1String("fractional-attributes")) entry[QStringLiteral("attributes")] = 0.5;
    else if (mutation == QLatin1String("unknown-attributes")) entry[QStringLiteral("attributes")] = 65536;
    else if (mutation == QLatin1String("string-attributes")) entry[QStringLiteral("attributes")] = QStringLiteral("0");
    else if (mutation == QLatin1String("missing-hash")) entry.remove(QStringLiteral("sha256"));
    else if (mutation == QLatin1String("short-hash")) entry[QStringLiteral("sha256")] = QStringLiteral("12");
    else if (mutation == QLatin1String("nonhex-hash")) entry[QStringLiteral("sha256")] = QString(64, QLatin1Char('g'));
    else if (mutation == QLatin1String("uppercase-hash")) entry[QStringLiteral("sha256")] = QString(64, QLatin1Char('A'));
    else if (mutation == QLatin1String("directory-with-hash")) entry[QStringLiteral("kind")] = QStringLiteral("directory");
    else if (mutation == QLatin1String("extra-entry-field")) entry[QStringLiteral("contents")] = QStringLiteral("forbidden");
    entries[0] = entry;
    if (mutation == QLatin1String("duplicate-path")) entries.append(entry);
    else if (mutation == QLatin1String("entry-not-object")) entries[0] = true;
    else if (mutation == QLatin1String("unicode-collision")) {
        auto other = entry;
        other[QStringLiteral("path")] = QStringLiteral("é.txt");
        entries.append(other);
        other[QStringLiteral("path")] = QStringLiteral("e\u0301.txt");
        entries.append(other);
    }
    else if (mutation == QLatin1String("case-collision") || mutation == QLatin1String("parent-is-file")) {
        auto other = entry;
        other[QStringLiteral("path")] = mutation == QLatin1String("case-collision")
            ? QStringLiteral("FILE.txt") : QStringLiteral("file.txt/child.txt");
        entries.append(other);
    }
    payload[QStringLiteral("entries")] = entries;
    if (mutation == QLatin1String("entries-not-array")) payload[QStringLiteral("entries")] = QJsonObject{};
    object[QStringLiteral("payload")] = payload;
    // Field mutations are deliberately signed correctly: each must reach the
    // relevant validation branch, independently of the checksum guard.
    QVERIFY(writeFile(path, QJsonDocument(resign(object)).toJson()));
    const auto result = Sync::loadBaseline(path);
    QVERIFY2(!result.ok(), qPrintable(mutation));
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.baseline.complete);
    QVERIFY(result.baseline.entries.isEmpty());
}

void SyncBaselineTests::rejectsBrokenIntegrity_data()
{
    QTest::addColumn<int>("mutation");
    QTest::newRow("tampered-payload") << 0;
    QTest::newRow("tampered-checksum") << 1;
    QTest::newRow("missing-integrity") << 2;
    QTest::newRow("wrong-algorithm") << 3;
    QTest::newRow("missing-value") << 4;
    QTest::newRow("malformed-value") << 5;
    QTest::newRow("extra-integrity-field") << 6;
}

void SyncBaselineTests::rejectsBrokenIntegrity()
{
    QFETCH(int, mutation);
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString path = fixture.directory.filePath(QStringLiteral("corrupt.json"));
    QVERIFY(Sync::saveBaseline(baseline(fixture), path).ok());
    auto object = QJsonDocument::fromJson(readFile(path)).object();
    if (mutation == 0) {
        auto payload = object[QStringLiteral("payload")].toObject();
        payload[QStringLiteral("scopeKey")] = QString(64, QLatin1Char('0'));
        object[QStringLiteral("payload")] = payload;
    } else if (mutation == 2) object.remove(QStringLiteral("integrity"));
    else {
        auto integrity = object[QStringLiteral("integrity")].toObject();
        if (mutation == 1) integrity[QStringLiteral("value")] = QString(64, QLatin1Char('0'));
        else if (mutation == 3) integrity[QStringLiteral("algorithm")] = QStringLiteral("none");
        else if (mutation == 4) integrity.remove(QStringLiteral("value"));
        else if (mutation == 5) integrity[QStringLiteral("value")] = QStringLiteral("broken");
        else if (mutation == 6) integrity[QStringLiteral("ignored")] = true;
        object[QStringLiteral("integrity")] = integrity;
    }
    QVERIFY(writeFile(path, QJsonDocument(object).toJson()));
    const auto result = Sync::loadBaseline(path);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.baseline.complete);
}

void SyncBaselineTests::jsonWhitespaceDoesNotAffectIntegrity()
{
    Fixture fixture;
    QVERIFY(fixture.valid);
    const QString path = fixture.directory.filePath(QStringLiteral("compact.json"));
    QVERIFY(Sync::saveBaseline(baseline(fixture), path).ok());
    const auto json = QJsonDocument::fromJson(readFile(path));
    QVERIFY(writeFile(path, json.toJson(QJsonDocument::Compact)));
    const auto result = Sync::loadBaseline(path);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.baseline.complete);
    QCOMPARE(result.baseline.entries.first().sha256, digest("payload"));
}

QTEST_GUILESS_MAIN(SyncBaselineTests)
#include "tst_syncbaseline.moc"
