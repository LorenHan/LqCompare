#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <algorithm>

#include "formatdefinition.h"
#include "formatdetector.h"
#include "sessiontype.h"

using namespace LqCompare;
using namespace LqCompare::Format;

namespace {
FormatDefinition definition(const QString &id, const QString &type,
                            const QStringList &masks = {})
{
    FormatDefinition result;
    result.id = id;
    result.name = QStringLiteral("Format ") + id;
    result.sessionTypeId = type;
    result.masks = masks;
    return result;
}

FileProbe content(const QByteArray &bytes, const QString &path = QStringLiteral("a.unknown"),
                  bool complete = true)
{
    FileProbe result;
    result.path = path;
    result.prefix = bytes;
    result.contentAvailable = true;
    result.complete = complete;
    return result;
}

bool addType(SessionTypeRegistry &registry, const QString &id, bool ready = true,
             SessionPlatformScope platform = SessionPlatformScope::All)
{
    SessionType type;
    type.id = id;
    type.displayName = QStringLiteral("View ") + id;
    type.platforms = platform;
    // The detector must inspect availability without ever invoking this factory.
    SessionFactory factory;
    if (ready)
        factory = [](QObject *) -> CompareSession * { return nullptr; };
    return registry.add(type, factory);
}

SessionTypeRegistry availableTypes()
{
    SessionTypeRegistry result;
    for (const auto &type : builtInSessionTypes())
        result.add(type, [](QObject *) -> CompareSession * { return nullptr; });
    return result;
}

QByteArray documentWithEntries(const QJsonArray &entries)
{
    // Obtain the versioned envelope from the public writer, then vary entries.
    QJsonObject document = QJsonDocument::fromJson(serializeDefinitions(
        {definition(QStringLiteral("seed"), QStringLiteral("text"))})).object();
    for (auto it = document.begin(); it != document.end(); ++it) {
        if (it.value().isArray()) {
            it.value() = entries;
            return QJsonDocument(document).toJson();
        }
    }
    return {};
}

QJsonObject entryJson(const FormatDefinition &value)
{
    const QJsonObject document = QJsonDocument::fromJson(serializeDefinitions({value})).object();
    for (auto it = document.begin(); it != document.end(); ++it)
        if (it.value().isArray())
            return it.value().toArray().first().toObject();
    return {};
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QStringList ids(const QVector<FormatDefinition> &definitions)
{
    QStringList result;
    for (const auto &item : definitions)
        result.append(item.id);
    return result;
}
}

class FormatTests : public QObject
{
    Q_OBJECT
private slots:
    void maskPriorityPrecedesSideOrder();
    void overridePrecedesMaskAndSignature();
    void disabledOrInvalidOverridesDoNotHideValidRule();
    void contentSignatureOnlyWhenNoMaskMatches();
    void matchedMaskConflictIsExplained();
    void maskCaseSensitivityIsExplicit();
    void unavailableDedicatedTypeUsesContent();
    void binaryRequiresAvailableHex();
    void textCanUseAvailableHex();
    void registeredMetadataIsNotImplementation();
    void platformAvailabilityIsRequired();
    void detectorNeverCreatesViews();
    void unknownFallback_data();
    void unknownFallback();
    void unavailableExplicitFallbackUsesContent_data();
    void unavailableExplicitFallbackUsesContent();
    void explicitFallbackDoesNotOverrideDefinedFormat();
    void contentAssessment_data();
    void contentAssessment();
    void truncatedUnicode_data();
    void truncatedUnicode();
    void absentSidesAndIoErrors();
    void unnamedMemoryProbeIsSupplied();
    void directoriesMustNotMixWithFiles();
    void boundedFileProbe();
    void invalidProbeLimitsAreRejected();
    void invalidDefinitionReplacementIsAtomic();
    void builtInLanguageMasks_data();
    void builtInLanguageMasks();
    void builtInDefinitionsAreValid();
    void jsonRoundTripPreservesOpaqueSettings();
    void damagedEntriesAreSkipped();
    void invalidDocumentsAreRejected();
    void inheritanceByBaseIdAndSameId();
    void mergePriorityDoesNotMutateBuiltIns();
    void atomicSaveRoundTripAndRejectedSavePreservesFile();
    void oversizedSavePreservesExistingFile();
};

void FormatTests::maskPriorityPrecedesSideOrder()
{
    const auto first = definition("first", "text", {"*.log"});
    const auto second = definition("second", "hex", {"*.txt"});
    FormatDetector detector({first, second});
    const auto registry = availableTypes();
    const auto left = content("left", "left.txt");
    const auto right = content("right", "right.log");
    auto result = detector.detect(left, right, registry);
    QCOMPARE(result.formatId, QStringLiteral("first"));
    QCOMPARE(result.matchedSide, 1);
    QCOMPARE(result.source, DetectionSource::FileMask);
    QCOMPARE(result.matchedRule, QStringLiteral("*.log"));
    QVERIFY(result.canOpen());
    QVERIFY(!result.explanation.isEmpty());
    QVERIFY(detector.setDefinitions({second, first}));
    result = detector.detect(left, right, registry);
    QCOMPARE(result.formatId, QStringLiteral("second"));
    QCOMPARE(result.matchedSide, 0);
}

void FormatTests::overridePrecedesMaskAndSignature()
{
    auto builtin = definition("builtin", "text", {"*.log"});
    builtin.builtIn = true;
    auto user = definition("user", "hex", {"*.log"});
    user.signatures = {{0, QByteArray("LOG")}};
    const auto forced = definition("forced", "table", {"*.csv"});
    FormatDetector detector(mergeDefinitions({builtin}, {user, forced}));
    const QByteArray original = serializeDefinitions(detector.definitions());
    DetectionOptions options;
    options.overrides = {{"*.log", "forced", true}, {"*", "builtin", true}};
    auto result = detector.detect(content("LOG data", "left.log"), {}, availableTypes(), options);
    QCOMPARE(result.formatId, QStringLiteral("forced"));
    QCOMPARE(result.source, DetectionSource::AssociationOverride);
    QCOMPARE(result.sessionTypeId, QStringLiteral("table"));
    QCOMPARE(result.matchedRule, QStringLiteral("*.log"));
    QCOMPARE(result.matchedSide, 0);
    QCOMPARE(serializeDefinitions(detector.definitions()), original);
    options.overrides = {{"*.csv", "forced", true}, {"*.log", "builtin", true}};
    result = detector.detect(content("LOG", "left.log"), content("x,y", "right.csv"),
                             availableTypes(), options);
    QCOMPARE(result.formatId, QStringLiteral("forced"));
    QCOMPARE(result.matchedSide, 1);
}

void FormatTests::disabledOrInvalidOverridesDoNotHideValidRule()
{
    FormatDetector detector({definition("plain", "text", {"*.log"}),
                             definition("forced", "hex")});
    DetectionOptions options;
    options.overrides = {{"*", "forced", false}, {"[", "forced", true},
                         {"*", "missing", true}, {"*.log", "plain", true}};
    const auto result = detector.detect(content("line", "a.log"), {}, availableTypes(), options);
    QCOMPARE(result.formatId, QStringLiteral("plain"));
    QCOMPARE(result.source, DetectionSource::AssociationOverride);
    QVERIFY(!result.diagnostics.isEmpty());
}

void FormatTests::contentSignatureOnlyWhenNoMaskMatches()
{
    auto signedFormat = definition("signed", "archive", {"*.archive"});
    signedFormat.signatures = {{2, QByteArray::fromHex("504b0304")}};
    const auto text = definition("plain", "text", {"*.txt"});
    FormatDetector detector({signedFormat, text});
    auto left = content(QByteArray::fromHex("7878504b030400"), "unknown.blob");
    auto result = detector.detect(left, {}, availableTypes());
    QCOMPARE(result.formatId, QStringLiteral("signed"));
    QCOMPARE(result.source, DetectionSource::ContentSignature);
    QCOMPARE(result.matchedSide, 0);
    left.path = QStringLiteral("known.txt");
    result = detector.detect(left, {}, availableTypes());
    QCOMPARE(result.formatId, QStringLiteral("plain"));
    QCOMPARE(result.source, DetectionSource::FileMask);
    result = detector.detect(content("ordinary"), content(left.prefix, "right.blob"), availableTypes());
    QCOMPARE(result.formatId, QStringLiteral("signed"));
    QCOMPARE(result.matchedSide, 1);
    result = detector.detect(content(QByteArray::fromHex("504b030400")), {}, availableTypes());
    QCOMPARE(result.source, DetectionSource::UnknownFallback);
    QVERIFY(result.formatId.isEmpty());
}

void FormatTests::matchedMaskConflictIsExplained()
{
    auto archive = definition("zip", "archive", {"*.zip"});
    archive.signatures = {{0, QByteArray::fromHex("504b0304")}};
    FormatDetector detector({archive});
    const auto result = detector.detect(content("ordinary text\n", "wrong.zip"), {}, availableTypes());
    QCOMPARE(result.formatId, QStringLiteral("zip"));
    QCOMPARE(result.sessionTypeId, QStringLiteral("archive"));
    QCOMPARE(result.source, DetectionSource::FileMask);
    QVERIFY(!result.diagnostics.isEmpty());
}

void FormatTests::maskCaseSensitivityIsExplicit()
{
    FormatDetector detector({definition("custom", "text", {"*.LoG"})});
    const auto file = content("line", "DIR/a.LOG");
    QCOMPARE(detector.detect(file, {}, availableTypes()).formatId, QStringLiteral("custom"));
    DetectionOptions options;
    options.maskCaseSensitivity = Qt::CaseSensitive;
    QVERIFY(detector.detect(file, {}, availableTypes(), options).formatId.isEmpty());
}

void FormatTests::unavailableDedicatedTypeUsesContent()
{
    SessionTypeRegistry registry;
    QVERIFY(addType(registry, "table", false));
    QVERIFY(addType(registry, "text"));
    QVERIFY(addType(registry, "hex"));
    FormatDetector detector({definition("csv", "table", {"*.csv"})});
    auto result = detector.detect(content("a,b\n1,2", "a.csv"), {}, registry);
    QCOMPARE(result.requestedSessionTypeId, QStringLiteral("table"));
    QCOMPARE(result.sessionTypeId, QStringLiteral("text"));
    QCOMPARE(result.formatId, QStringLiteral("csv"));
    QVERIFY(result.usedAvailabilityFallback);
    QVERIFY(!result.explanation.isEmpty());
    result = detector.detect(content(QByteArray::fromHex("000102"), "a.csv"), {}, registry);
    QCOMPARE(result.sessionTypeId, QStringLiteral("hex"));
    QVERIFY(result.usedAvailabilityFallback);
}

void FormatTests::binaryRequiresAvailableHex()
{
    FormatDetector detector(QVector<FormatDefinition>{});
    SessionTypeRegistry registry;
    QVERIFY(addType(registry, "text"));
    QVERIFY(addType(registry, "hex", false));
    const auto result = detector.detect(content(QByteArray::fromHex("ff0001")), {}, registry);
    QVERIFY(!result.canOpen());
    QVERIFY(!result.explanation.isEmpty());
}

void FormatTests::textCanUseAvailableHex()
{
    FormatDetector detector(QVector<FormatDefinition>{});
    SessionTypeRegistry registry;
    QVERIFY(addType(registry, "text", false));
    QVERIFY(addType(registry, "hex"));
    const auto result = detector.detect(content("hello"), {}, registry);
    QCOMPARE(result.sessionTypeId, QStringLiteral("hex"));
    QVERIFY(result.usedAvailabilityFallback);
}

void FormatTests::registeredMetadataIsNotImplementation()
{
    SessionTypeRegistry registry;
    QCOMPARE(registry.addBuiltInTypes(), 14);
    FormatDetector detector;
    const auto result = detector.detect(content("hello", "a.txt"), {}, registry);
    QVERIFY(!result.canOpen());
    QVERIFY(!result.explanation.isEmpty());
}

void FormatTests::platformAvailabilityIsRequired()
{
    SessionTypeRegistry registry;
    QVERIFY(addType(registry, "registry", true, SessionPlatformScope::WindowsOnly));
    QVERIFY(addType(registry, "text"));
    QVERIFY(addType(registry, "hex"));
    FormatDetector detector({definition("reg", "registry", {"*.reg"})});
    const auto result = detector.detect(content("Windows Registry Editor", "a.reg"), {}, registry);
    QCOMPARE(result.sessionTypeId, currentPlatformAllows(SessionPlatformScope::WindowsOnly)
             ? QStringLiteral("registry") : QStringLiteral("text"));
    QCOMPARE(result.usedAvailabilityFallback,
             !currentPlatformAllows(SessionPlatformScope::WindowsOnly));
}

void FormatTests::detectorNeverCreatesViews()
{
    SessionTypeRegistry registry;
    SessionType type;
    type.id = QStringLiteral("text");
    type.displayName = QStringLiteral("Text");
    int calls = 0;
    QVERIFY(registry.add(type, [&calls](QObject *) -> CompareSession * { ++calls; return nullptr; }));
    FormatDetector detector;
    QVERIFY(detector.detect(content("hello", "a.txt"), {}, registry).canOpen());
    QCOMPARE(calls, 0);
}

void FormatTests::unknownFallback_data()
{
    QTest::addColumn<int>("fallback");
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("expected");
    QTest::newRow("automatic-text") << int(UnknownFallback::Automatic) << QByteArray("hello") << QString("text");
    QTest::newRow("automatic-binary") << int(UnknownFallback::Automatic) << QByteArray::fromHex("00ff") << QString("hex");
    QTest::newRow("explicit-text-on-binary") << int(UnknownFallback::Text) << QByteArray::fromHex("00ff") << QString("text");
    QTest::newRow("explicit-hex-on-text") << int(UnknownFallback::Hex) << QByteArray("hello") << QString("hex");
    QTest::newRow("explicit-picture") << int(UnknownFallback::Picture) << QByteArray("hello") << QString("picture");
    QTest::newRow("ask-text") << int(UnknownFallback::Ask) << QByteArray("hello") << QString();
    QTest::newRow("ask-binary") << int(UnknownFallback::Ask) << QByteArray::fromHex("00ff") << QString();
}

void FormatTests::unknownFallback()
{
    QFETCH(int, fallback);
    QFETCH(QByteArray, bytes);
    QFETCH(QString, expected);
    FormatDetector detector(QVector<FormatDefinition>{});
    DetectionOptions options;
    options.unknownFallback = UnknownFallback(fallback);
    const auto result = detector.detect(content(bytes), {}, availableTypes(), options);
    QCOMPARE(result.source, DetectionSource::UnknownFallback);
    QCOMPARE(result.sessionTypeId, expected);
    QCOMPARE(result.canOpen(), !expected.isEmpty());
    QVERIFY(!result.explanation.isEmpty());
}

void FormatTests::explicitFallbackDoesNotOverrideDefinedFormat()
{
    FormatDetector detector({definition("source", "text", {"*.source"})});
    for (auto fallback : {UnknownFallback::Hex, UnknownFallback::Picture, UnknownFallback::Ask}) {
        DetectionOptions options;
        options.unknownFallback = fallback;
        const auto result = detector.detect(content("code", "a.source"), {}, availableTypes(), options);
        QCOMPARE(result.sessionTypeId, QStringLiteral("text"));
        QCOMPARE(result.source, DetectionSource::FileMask);
    }
}

void FormatTests::unavailableExplicitFallbackUsesContent_data()
{
    QTest::addColumn<int>("fallback");
    QTest::addColumn<QString>("requested");
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("expected");
    QTest::newRow("picture-unimplemented-text") << int(UnknownFallback::Picture)
        << QString("picture") << QByteArray("hello") << QString("text");
    QTest::newRow("picture-unimplemented-binary") << int(UnknownFallback::Picture)
        << QString("picture") << QByteArray::fromHex("00ff") << QString("hex");
    QTest::newRow("text-unimplemented") << int(UnknownFallback::Text)
        << QString("text") << QByteArray("hello") << QString("hex");
    QTest::newRow("hex-unimplemented-text") << int(UnknownFallback::Hex)
        << QString("hex") << QByteArray("hello") << QString("text");
    QTest::newRow("hex-unimplemented-binary") << int(UnknownFallback::Hex)
        << QString("hex") << QByteArray::fromHex("00ff") << QString();
}

void FormatTests::unavailableExplicitFallbackUsesContent()
{
    QFETCH(int, fallback);
    QFETCH(QString, requested);
    QFETCH(QByteArray, bytes);
    QFETCH(QString, expected);
    SessionTypeRegistry registry;
    for (const auto &id : {QStringLiteral("text"), QStringLiteral("hex"), QStringLiteral("picture")})
        QVERIFY(addType(registry, id, id != requested));
    FormatDetector detector(QVector<FormatDefinition>{});
    DetectionOptions options;
    options.unknownFallback = UnknownFallback(fallback);
    const auto result = detector.detect(content(bytes), {}, registry, options);
    QCOMPARE(result.requestedSessionTypeId, requested);
    QCOMPARE(result.sessionTypeId, expected);
    QCOMPARE(result.canOpen(), !expected.isEmpty());
    if (result.canOpen()) {
        QVERIFY(registry.find(result.sessionTypeId)->hasFactory());
        QVERIFY(registry.find(result.sessionTypeId)->isAvailableHere());
        QVERIFY(result.usedAvailabilityFallback);
    }
    QVERIFY(!result.explanation.isEmpty());
}

void FormatTests::contentAssessment_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<int>("kind");
    QTest::newRow("empty") << QByteArray() << int(ContentKind::Text);
    QTest::newRow("ascii") << QByteArray("line\r\n\ttab") << int(ContentKind::Text);
    QTest::newRow("utf8") << QStringLiteral("中文 😀").toUtf8() << int(ContentKind::Text);
    QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbf6869") << int(ContentKind::Text);
    QTest::newRow("utf16-le") << QByteArray::fromHex("fffe680069000a00") << int(ContentKind::Text);
    QTest::newRow("utf16-be") << QByteArray::fromHex("feff00680069000a") << int(ContentKind::Text);
    QTest::newRow("utf32-le") << QByteArray::fromHex("fffe00006800000069000000") << int(ContentKind::Text);
    QTest::newRow("utf32-be") << QByteArray::fromHex("0000feff0000006800000069") << int(ContentKind::Text);
    QTest::newRow("nul") << QByteArray::fromHex("610062") << int(ContentKind::Binary);
    QTest::newRow("invalid-utf8") << QByteArray::fromHex("c328") << int(ContentKind::Binary);
    QTest::newRow("overlong-utf8") << QByteArray::fromHex("c080") << int(ContentKind::Binary);
    QTest::newRow("utf8-surrogate") << QByteArray::fromHex("eda080") << int(ContentKind::Binary);
    QTest::newRow("control-bytes") << QByteArray::fromHex("010203040506") << int(ContentKind::Binary);
}

void FormatTests::contentAssessment()
{
    QFETCH(QByteArray, bytes);
    QFETCH(int, kind);
    const auto probe = content(bytes);
    const auto assessed = assessContent(probe);
    QCOMPARE(int(assessed.kind), kind);
    QVERIFY(!assessed.explanation.isEmpty());
    FormatDetector detector(QVector<FormatDefinition>{});
    const auto result = detector.detect(probe, {}, availableTypes());
    QCOMPARE(result.sessionTypeId, kind == int(ContentKind::Binary) ? QStringLiteral("hex") : QStringLiteral("text"));
}

void FormatTests::truncatedUnicode_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("utf8-two-byte") << QByteArray::fromHex("6162c3");
    QTest::newRow("utf8-three-byte") << QByteArray::fromHex("6162e4b8");
    QTest::newRow("utf8-four-byte") << QByteArray::fromHex("6162f09f98");
    QTest::newRow("utf16-le-odd-byte") << QByteArray::fromHex("fffe610062");
    QTest::newRow("utf16-be-odd-byte") << QByteArray::fromHex("feff006100");
    QTest::newRow("utf16-high-surrogate") << QByteArray::fromHex("fffe61003dd8");
    QTest::newRow("utf32-le-partial") << QByteArray::fromHex("fffe0000610000006200");
    QTest::newRow("utf32-be-partial") << QByteArray::fromHex("0000feff000000610000");
}

void FormatTests::truncatedUnicode()
{
    QFETCH(QByteArray, bytes);
    auto probe = content(bytes, "a.unknown", false);
    QCOMPARE(assessContent(probe).kind, ContentKind::Text);
    FormatDetector detector(QVector<FormatDefinition>{});
    QCOMPARE(detector.detect(probe, {}, availableTypes()).sessionTypeId, QStringLiteral("text"));
    probe.complete = true;
    QCOMPARE(assessContent(probe).kind, ContentKind::Binary);
}

void FormatTests::absentSidesAndIoErrors()
{
    FormatDetector detector;
    const auto registry = availableTypes();
    QVERIFY(!detector.detect({}, {}, registry).canOpen());
    auto result = detector.detect({}, content("hello", "right.txt"), registry);
    QVERIFY(result.canOpen());
    QCOMPARE(result.matchedSide, 1);
    FileProbe failed;
    failed.path = QStringLiteral("missing.txt");
    failed.error = QStringLiteral("No such file");
    QVERIFY(!detector.detect(failed, {}, registry).canOpen());
    QVERIFY(!detector.detect(content("ok", "left.txt"), failed, registry).canOpen());
    QVERIFY(!detector.detectFiles("/lqcompare-does-not-exist/missing.txt", {}, registry).canOpen());
}

void FormatTests::directoriesMustNotMixWithFiles()
{
    FileProbe left;
    left.path = QStringLiteral("left-folder");
    left.directory = true;
    auto right = left;
    right.path = QStringLiteral("right-folder");
    FormatDetector detector;
    const auto registry = availableTypes();
    auto result = detector.detect(left, right, registry);
    QCOMPARE(result.source, DetectionSource::Directory);
    QCOMPARE(result.sessionTypeId, QStringLiteral("folder"));
    QVERIFY(!detector.detect(left, content("file", "a.txt"), registry).canOpen());
    QVERIFY(!detector.detect(content("file", "a.txt"), right, registry).canOpen());
}

void FormatTests::unnamedMemoryProbeIsSupplied()
{
    FormatDetector detector(QVector<FormatDefinition>{});
    const auto registry = availableTypes();
    // detectFiles treats an empty path as absent. The pure-data detect API also
    // accepts unnamed samples when contentAvailable explicitly marks them so.
    const auto memory = content(QByteArray::fromHex("ff0001"), QString());
    const auto result = detector.detect(memory, {}, registry);
    QCOMPARE(result.sessionTypeId, QStringLiteral("hex"));
    QCOMPARE(result.leftContent.kind, ContentKind::Binary);
    QCOMPARE(result.rightContent.kind, ContentKind::Unknown);
    QVERIFY(!detector.detect({}, {}, registry).canOpen());
    QVERIFY(!detector.detectFiles({}, {}, registry).canOpen());
}

void FormatTests::boundedFileProbe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("sample.unknown");
    QVERIFY(writeFile(path, QByteArray("0123456789")));
    auto probe = probeFile(path, 4);
    QCOMPARE(probe.path, path);
    QCOMPARE(probe.prefix, QByteArray("0123"));
    QVERIFY(probe.contentAvailable);
    QVERIFY(!probe.complete);
    QVERIFY(probe.error.isEmpty());
    probe = probeFile(path, 10);
    QVERIFY(probe.complete);
    QCOMPARE(probe.prefix, QByteArray("0123456789"));
    QVERIFY(writeFile(path, {}));
    probe = probeFile(path);
    QVERIFY(probe.complete);
    QVERIFY(probe.contentAvailable);
    QCOMPARE(assessContent(probe).kind, ContentKind::Text);
    QVERIFY(probeFile(dir.path()).directory);
    QVERIFY(!probeFile(dir.filePath("missing")).error.isEmpty());
    const auto empty = probeFile({});
    QVERIFY(empty.path.isEmpty());
    QVERIFY(empty.error.isEmpty());
}

void FormatTests::invalidDefinitionReplacementIsAtomic()
{
    const auto valid = definition("valid", "text", {"*.txt"});
    FormatDetector detector({valid});
    const QByteArray snapshot = serializeDefinitions(detector.definitions());
    QVector<QVector<FormatDefinition>> invalidTables;
    auto bad = valid;
    bad.id.clear();
    invalidTables.append({bad});
    bad = valid;
    bad.name.clear();
    invalidTables.append({bad});
    bad = valid;
    bad.masks = QStringList{QStringLiteral("[")};
    invalidTables.append({bad});
    bad = valid;
    bad.signatures = {{-1, QByteArray("x")}};
    invalidTables.append({bad});
    bad = valid;
    bad.sessionTypeId = QStringLiteral("7text");
    invalidTables.append({bad});
    bad = valid;
    bad.signatures = {{1024 * 1024, QByteArray("x")}};
    invalidTables.append({bad});
    invalidTables.append({valid, valid});
    for (const auto &table : invalidTables) {
        QString error;
        QVERIFY(!detector.setDefinitions(table, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(serializeDefinitions(detector.definitions()), snapshot);
        QCOMPARE(detector.detect(content("text", "a.txt"), {}, availableTypes()).formatId,
                 QStringLiteral("valid"));
    }
    auto boundary = valid;
    boundary.id = QStringLiteral("7zip"); // Format IDs may begin with a digit.
    boundary.signatures = {{1024 * 1024 - 1, QByteArray("x")}};
    QVERIFY(validateDefinition(boundary).isEmpty());
    QVERIFY(detector.setDefinitions({boundary}));
}

void FormatTests::invalidProbeLimitsAreRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("sample.txt");
    QVERIFY(writeFile(path, "hello"));
    for (const qint64 limit : {qint64(-1), qint64(0), qint64(1024 * 1024 + 1)}) {
        const auto probe = probeFile(path, limit);
        QVERIFY(!probe.contentAvailable);
        QVERIFY(!probe.error.isEmpty());
        QVERIFY(probe.prefix.isEmpty());
        DetectionOptions options;
        options.probeLimit = limit;
        QVERIFY(!FormatDetector().detectFiles(path, {}, availableTypes(), options).canOpen());
    }
    const auto minimum = probeFile(path, 1);
    QVERIFY(minimum.contentAvailable);
    QCOMPARE(minimum.prefix, QByteArray("h"));
    QVERIFY(!minimum.complete);
}

void FormatTests::builtInLanguageMasks_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expectedType");
    QTest::addColumn<QString>("expectedFormat");
    const QStringList names = {"readme.txt", "main.cpp", "widget.h", "script.py", "app.js",
        "data.json", "doc.xml", "index.html", "style.css", "README.md", "app.ini",
        "setup.sh", "build.bat", "query.sql", "data.csv", "data.tsv", "server.log", "Makefile"};
    const QStringList formats = {"plain-text", "cpp", "headers", "python", "javascript",
        "json", "xml", "html", "css", "markdown", "ini", "shell", "batch", "sql", "csv",
        "tsv", "log", "makefile"};
    for (int i = 0; i < names.size(); ++i) {
        const auto &name = names.at(i);
        QTest::newRow(qPrintable(name)) << name
            << (name.endsWith(".csv") || name.endsWith(".tsv") ? QString("table") : QString("text"))
            << formats.at(i);
    }
}

void FormatTests::builtInLanguageMasks()
{
    QFETCH(QString, path);
    QFETCH(QString, expectedType);
    QFETCH(QString, expectedFormat);
    FormatDetector detector;
    const auto result = detector.detect(content("sample", path), {}, availableTypes());
    QCOMPARE(result.source, DetectionSource::FileMask);
    QCOMPARE(result.sessionTypeId, expectedType);
    QCOMPARE(result.formatId, expectedFormat);
    const auto definitions = detector.definitions();
    auto it = std::find_if(definitions.cbegin(), definitions.cend(), [&](const FormatDefinition &d) {
        return d.id == result.formatId;
    });
    QVERIFY(it != definitions.cend());
    QVERIFY(it->builtIn);
}

void FormatTests::builtInDefinitionsAreValid()
{
    const auto builtIns = builtInFormatDefinitions();
    QVERIFY(builtIns.size() >= 18);
    QStringList seen;
    for (const auto &item : builtIns) {
        QVERIFY2(validateDefinition(item).isEmpty(), qPrintable(item.id));
        QVERIFY2(!seen.contains(item.id), qPrintable(item.id));
        seen.append(item.id);
        QVERIFY(item.builtIn);
    }
}

void FormatTests::jsonRoundTripPreservesOpaqueSettings()
{
    auto item = definition("portable", "text", {"*.portable", "README"});
    item.signatures = {{0, QByteArray::fromHex("001122ff")}, {4, QByteArray("magic")}};
    item.settings = QJsonObject{{"encoding", "UTF-16LE"}, {"future-key", QJsonObject{{"enabled", true},
        {"values", QJsonArray{1, "two", QJsonValue::Null}}}},
        {"externalConverter", QJsonObject{{"program", "/untrusted/program"}, {"arguments", "$(touch never)"}}}};
    const QByteArray json = serializeDefinitions({item});
    const auto parsed = parseDefinitions(json);
    QVERIFY2(parsed.documentValid, qPrintable(parsed.diagnostics.join('\n')));
    QCOMPARE(parsed.definitions.size(), 1);
    const auto &actual = parsed.definitions.first();
    QCOMPARE(actual.id, item.id);
    QCOMPARE(actual.name, item.name);
    QCOMPARE(actual.sessionTypeId, item.sessionTypeId);
    QCOMPARE(actual.masks, item.masks);
    QCOMPARE(actual.settings, item.settings);
    QCOMPARE(actual.signatures.size(), item.signatures.size());
    for (int i = 0; i < item.signatures.size(); ++i) {
        QCOMPARE(actual.signatures.at(i).offset, item.signatures.at(i).offset);
        QCOMPARE(actual.signatures.at(i).bytes, item.signatures.at(i).bytes);
    }
    QCOMPARE(serializeDefinitions(parsed.definitions), json);
}

void FormatTests::damagedEntriesAreSkipped()
{
    const QJsonObject first = entryJson(definition("first", "text", {"*.one"}));
    const QJsonObject last = entryJson(definition("last", "hex", {"*.last"}));
    QVERIFY(!first.isEmpty());
    auto bad = first;
    bad["id"] = QStringLiteral("bad");
    bad["masks"] = QJsonArray{"["};
    auto badBase = first;
    badBase["id"] = QStringLiteral("bad-base");
    badBase["baseId"] = QJsonValue(42);
    const auto result = parseDefinitions(documentWithEntries({first, bad, QJsonValue(42), badBase, last}));
    QVERIFY(result.documentValid);
    QCOMPARE(ids(result.definitions), QStringList({"first", "last"}));
    QVERIFY(!result.diagnostics.isEmpty());
}

void FormatTests::invalidDocumentsAreRejected()
{
    for (const QByteArray &json : {QByteArray("{"), QByteArray("[]"), QByteArray("null"), QByteArray("{}"),
         QByteArray("{\"version\":2,\"definitions\":[]}"), QByteArray("{\"version\":1,\"definitions\":{}}")}) {
        const auto result = parseDefinitions(json);
        QVERIFY(!result.documentValid);
        QVERIFY(result.definitions.isEmpty());
        QVERIFY(!result.diagnostics.isEmpty());
    }
}

void FormatTests::inheritanceByBaseIdAndSameId()
{
    auto base = definition("base", "text", {"*.base"});
    base.builtIn = true;
    base.settings = {{"encoding", "UTF-8"}, {"preserved", 42}};
    base.signatures = {{0, QByteArray("BASE")}};
    const auto original = serializeDefinitions({base});
    const QJsonObject copy{{"id", "copy"}, {"baseId", "base"}, {"name", "My copied format"},
                           {"settings", QJsonObject{{"encoding", "UTF-16LE"}}}};
    const QJsonObject same{{"id", "base"}, {"name", "Renamed override"}};
    const auto loaded = parseDefinitions(documentWithEntries({copy, same}), {base});
    QVERIFY2(loaded.documentValid, qPrintable(loaded.diagnostics.join('\n')));
    QCOMPARE(loaded.definitions.size(), 2);
    const auto child = loaded.definitions.at(0);
    QCOMPARE(child.id, QStringLiteral("copy"));
    QCOMPARE(child.sessionTypeId, base.sessionTypeId);
    QCOMPARE(child.masks, base.masks);
    QCOMPARE(child.settings.value("encoding").toString(), QStringLiteral("UTF-16LE"));
    QCOMPARE(child.settings.value("preserved").toInt(), 42);
    QVERIFY(!child.builtIn);
    QCOMPARE(child.signatures.first().bytes, QByteArray("BASE"));
    const auto override = loaded.definitions.at(1);
    QCOMPARE(override.id, base.id);
    QCOMPARE(override.name, QStringLiteral("Renamed override"));
    QCOMPARE(override.masks, base.masks);
    QCOMPARE(override.settings, base.settings);
    QVERIFY(!override.builtIn);
    QCOMPARE(serializeDefinitions({base}), original);
}

void FormatTests::mergePriorityDoesNotMutateBuiltIns()
{
    auto base = definition("base", "text", {"*.base"});
    base.builtIn = true;
    auto kept = definition("kept", "text", {"*.keep"});
    kept.builtIn = true;
    auto user = base;
    user.name = QStringLiteral("User override");
    user.builtIn = false;
    const auto extra = definition("extra", "hex", {"*.extra"});
    const QVector<FormatDefinition> builtIns{base, kept};
    const QVector<FormatDefinition> users{user, extra};
    const auto beforeBuiltIns = serializeDefinitions(builtIns);
    const auto beforeUsers = serializeDefinitions(users);
    auto merged = mergeDefinitions(builtIns, users);
    QCOMPARE(ids(merged), QStringList({"base", "extra", "kept"}));
    QCOMPARE(merged.first().name, user.name);
    QVERIFY(!merged.first().builtIn);
    merged = mergeDefinitions(builtIns, users, {"kept", "extra", "missing", "extra"});
    QCOMPARE(ids(merged), QStringList({"kept", "extra", "base"}));
    QCOMPARE(serializeDefinitions(builtIns), beforeBuiltIns);
    QCOMPARE(serializeDefinitions(users), beforeUsers);
}

void FormatTests::atomicSaveRoundTripAndRejectedSavePreservesFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto valid = definition("saved", "text", {"*.saved"});
    const QString path = dir.filePath("formats.json");
    QString error;
    QVERIFY2(saveDefinitions(path, {valid}, &error), qPrintable(error));
    const auto loaded = loadDefinitions(path);
    QVERIFY(loaded.documentValid);
    QCOMPARE(ids(loaded.definitions), QStringList({"saved"}));
    auto invalid = valid;
    invalid.masks = QStringList{QStringLiteral("[")};
    QVERIFY(!saveDefinitions(path, {invalid}, &error));
    QVERIFY(!error.isEmpty());
    const auto stillThere = loadDefinitions(path);
    QVERIFY(stillThere.documentValid);
    QCOMPARE(serializeDefinitions(stillThere.definitions), serializeDefinitions({valid}));
    QVERIFY(!saveDefinitions(dir.path(), {valid}, &error));
    QVERIFY(!error.isEmpty());
    const auto missing = loadDefinitions(dir.filePath("absent.json"));
    QVERIFY(!missing.documentValid);
    QVERIFY(!missing.diagnostics.isEmpty());
}

void FormatTests::oversizedSavePreservesExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("formats.json");
    auto item = definition("retained", "text", {"*.retained"});
    QString error;
    QVERIFY2(saveDefinitions(path, {item}, &error), qPrintable(error));
    const QByteArray snapshot = serializeDefinitions({item});
    item.settings.insert(QStringLiteral("large-value"), QString(8 * 1024 * 1024, QLatin1Char('x')));
    QVERIFY(serializeDefinitions({item}).size() > 8 * 1024 * 1024);
    QVERIFY(!saveDefinitions(path, {item}, &error));
    QVERIFY(!error.isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), snapshot);
}

QTEST_GUILESS_MAIN(FormatTests)
#include "tst_format.moc"
