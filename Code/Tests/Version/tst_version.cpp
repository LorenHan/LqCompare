#include <QtTest>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "versioninfo.h"
#include "pefixtures.h"

using namespace LqCompare::Version;
using namespace PeFixtures;

class VersionTests : public QObject
{
    Q_OBJECT
private slots:
    void peMetadata_data();
    void peMetadata();
    void multilingualVersionResources();
    void optionalFieldsRemainAbsent();
    void peWithoutVersionResources();
    void importAddressTableFallback();
    void namedResource();
    void exportAlias();
    void unspecifiedAlignmentBytesAreAllowed_data();
    void unspecifiedAlignmentBytesAreAllowed();
    void nonPeFileMetadataAndReadOnly();
    void nonPeByteFormats_data();
    void nonPeByteFormats();
    void fileErrorsAndSizeLimit();
    void configuredLimits_data();
    void configuredLimits();
    void malformed_data();
    void malformed();
    void everyTruncationIsRejected();
    void deterministicMutationSmoke();
};

void VersionTests::peMetadata_data()
{
    QTest::addColumn<bool>("plus");
    QTest::newRow("PE32") << false;
    QTest::newRow("PE32+") << true;
}

void VersionTests::peMetadata()
{
    QFETCH(bool, plus);
    const auto bytes = image(plus);
    const auto snapshot = bytes;
    const auto info = parse(bytes);
    QCOMPARE(info.status, Status::Pe);
    QVERIFY2(info.usable(), qPrintable(info.message));
    QCOMPARE(info.pe32Plus, plus);
    QCOMPARE(info.headers.value(QStringLiteral("Format")), plus ? QStringLiteral("PE32+") : QStringLiteral("PE32"));
    QCOMPARE(info.headers.value(QStringLiteral("NumberOfSections")), QStringLiteral("3"));
    QCOMPARE(info.headers.value(QStringLiteral("ImageBase")).toULongLong(nullptr, 0),
             plus ? Q_UINT64_C(0x180000000) : Q_UINT64_C(0x400000));
    QCOMPARE(info.headers.value(QStringLiteral("CheckSum")).toULongLong(nullptr, 0), Q_UINT64_C(0x12345678));
    QCOMPARE(info.headers.value(QStringLiteral("ExportDllName")), QStringLiteral("fixture.dll"));
    QCOMPARE(info.sections.size(), 3);
    QCOMPARE(info.sections[0].name, QStringLiteral(".text"));
    QCOMPARE(info.sections[0].virtualAddress, textRva);
    QCOMPARE(info.sections[0].rawOffset, quint32(textOffset));
    QCOMPARE(info.sections[0].rawSize, quint32(0x200));
    QCOMPARE(info.sections[0].characteristics, quint32(0x60000020));
    QCOMPARE(info.sections[2].name, QStringLiteral(".rsrc"));
    QCOMPARE(info.imports.size(), 2);
    QCOMPARE(info.imports[0].dll, QStringLiteral("KERNEL32.dll"));
    QCOMPARE(info.imports[0].name, QStringLiteral("CreateFileW"));
    QVERIFY(!info.imports[0].byOrdinal);
    QCOMPARE(info.imports[0].hint, quint16(17));
    QCOMPARE(info.imports[1].dll, QStringLiteral("KERNEL32.dll"));
    QVERIFY(info.imports[1].byOrdinal);
    QCOMPARE(info.imports[1].ordinal, quint16(42));
    QVERIFY(info.imports[1].name.isEmpty());
    QCOMPARE(info.exports.size(), 3);
    QCOMPARE(info.exports[0].name, QStringLiteral("Alpha"));
    QCOMPARE(info.exports[0].ordinal, quint32(7));
    QCOMPARE(info.exports[0].address, textRva);
    QVERIFY(info.exports[0].forwarder.isEmpty());
    QVERIFY(info.exports[1].name.isEmpty());
    QCOMPARE(info.exports[1].ordinal, quint32(8));
    QCOMPARE(info.exports[1].address, textRva + 1);
    QCOMPARE(info.exports[2].name, QStringLiteral("Forwarded"));
    QCOMPARE(info.exports[2].ordinal, quint32(9));
    QCOMPARE(info.exports[2].forwarder, QStringLiteral("KERNEL32.Sleep"));
    QCOMPARE(info.versions.size(), 2);
    QCOMPARE(bytes, snapshot);
}

void VersionTests::multilingualVersionResources()
{
    const auto info = parse(image());
    QCOMPARE(info.status, Status::Pe);
    QCOMPARE(info.versions.size(), 2);
    QCOMPARE(info.versions[0].resourceName, QStringLiteral("1"));
    QCOMPARE(info.versions[0].language, QStringLiteral("0409"));
    QCOMPARE(info.versions[1].language, QStringLiteral("0804"));
    for (const auto &resource : info.versions) {
        QCOMPARE(resource.fixed.value(QStringLiteral("FileVersion")), QStringLiteral("1.2.3.4"));
        QCOMPARE(resource.fixed.value(QStringLiteral("ProductVersion")), QStringLiteral("5.6.7.8"));
        QCOMPARE(resource.fixed.value(QStringLiteral("FileFlags")).toULongLong(nullptr, 0), Q_UINT64_C(1));
        QCOMPARE(resource.fixed.value(QStringLiteral("FileOS")).toULongLong(nullptr, 0), Q_UINT64_C(0x40004));
        QCOMPARE(resource.strings.size(), 7);
        QCOMPARE(resource.strings.value(QStringLiteral("040904B0/CompanyName")), QStringLiteral("Fixture Labs"));
        QCOMPARE(resource.strings.value(QStringLiteral("040904B0/FileDescription")), QStringLiteral("Native fixture"));
        QCOMPARE(resource.strings.value(QStringLiteral("040904B0/FileVersion")), QStringLiteral("1.2.3.4"));
        QCOMPARE(resource.strings.value(QStringLiteral("040904B0/CustomField")), QStringLiteral("preserved"));
        QCOMPARE(resource.strings.value(QStringLiteral("080404B0/CompanyName")), QString::fromUtf8("样例公司"));
        QCOMPARE(resource.strings.value(QStringLiteral("080404B0/FileDescription")), QString::fromUtf8("版本语料"));
        QCOMPARE(resource.translations, QVector<quint32>({0x04b00409, 0x04b00804}));
    }
}

void VersionTests::optionalFieldsRemainAbsent()
{
    const auto info = parse(image(false, true, true));
    QCOMPARE(info.status, Status::Pe);
    QCOMPARE(info.versions.size(), 2);
    const auto &strings = info.versions[0].strings;
    QVERIFY(!strings.contains(QStringLiteral("040904B0/CompanyName")));
    QVERIFY(!strings.contains(QStringLiteral("040904B0/ProductVersion")));
    QCOMPARE(strings.value(QStringLiteral("080404B0/CompanyName")), QString::fromUtf8("样例公司"));
    QCOMPARE(info.versions[0].fixed.value(QStringLiteral("ProductVersion")), QStringLiteral("5.6.7.8"));
}

void VersionTests::peWithoutVersionResources()
{
    const auto info = parse(image(false, false));
    QCOMPARE(info.status, Status::Pe);
    QVERIFY(info.usable());
    QVERIFY(info.versions.isEmpty());
    QVERIFY(!info.message.isEmpty());
    QCOMPARE(info.imports.size(), 2);
    QCOMPARE(info.exports.size(), 3);
}

void VersionTests::importAddressTableFallback()
{
    for (bool plus : {false, true}) {
        auto bytes = image(plus);
        put32(bytes, importOffset, 0);
        const auto info = parse(bytes);
        QCOMPARE(info.status, Status::Pe);
        QCOMPARE(info.imports.size(), 2);
        QCOMPARE(info.imports[0].name, QStringLiteral("CreateFileW"));
        QCOMPARE(info.imports[1].ordinal, quint16(42));
    }
}

void VersionTests::namedResource()
{
    auto bytes = image();
    put16(bytes, resourceOffset + 0x20 + 12, 1);
    put16(bytes, resourceOffset + 0x20 + 14, 0);
    put32(bytes, resourceOffset + 0x30, 0x80000080);
    put16(bytes, resourceOffset + 0x80, 7);
    putBytes(bytes, resourceOffset + 0x82, utf16(QStringLiteral("Version"), false));
    const auto info = parse(bytes);
    QCOMPARE(info.status, Status::Pe);
    QCOMPARE(info.versions.size(), 2);
    QCOMPARE(info.versions[0].resourceName, QStringLiteral("Version"));
}

void VersionTests::exportAlias()
{
    auto bytes = image();
    put16(bytes, dataOffset + 0x5a, 0); // Both distinct names refer to the same ordinal.
    const auto info = parse(bytes);
    QCOMPARE(info.status, Status::Pe);
    QStringList aliasNames;
    for (const auto &entry : info.exports)
        if (entry.ordinal == 7) aliasNames += entry.name;
    aliasNames.sort();
    QCOMPARE(aliasNames, QStringList({QStringLiteral("Alpha"), QStringLiteral("Forwarded")}));
}

void VersionTests::unspecifiedAlignmentBytesAreAllowed_data()
{
    QTest::addColumn<bool>("resourceTail");
    QTest::newRow("between-string-structures") << false;
    QTest::newRow("after-version-root") << true;
}

void VersionTests::unspecifiedAlignmentBytesAreAllowed()
{
    QFETCH(bool, resourceTail);
    auto bytes = image();
    QString expectedCompany = QStringLiteral("Fixture Labs");
    if (resourceTail) {
        // An absent fixed value is legal. End the root halfway through a DWORD
        // so that its resource leaf can contain two trailing alignment bytes.
        const auto root = block(QStringLiteral("VS_VERSION_INFO"), 0, {}, {
            block(QStringLiteral("StringFileInfo"), 1, {}, {
                block(QStringLiteral("040904B0"), 1, {}, {
                    stringValue(QStringLiteral("CompanyName"), QStringLiteral("XY"))
                })
            })
        });
        QCOMPARE(root.size() % 4, 2);
        putBytes(bytes, versionOffset, root);
        put32(bytes, resourceOffset + 0x64, quint32(root.size() + 2));
        bytes[versionOffset + root.size()] = char(0x7f);
        bytes[versionOffset + root.size() + 1] = char(0x80);
        expectedCompany = QStringLiteral("XY");
    } else {
        const auto key = utf16(QStringLiteral("CompanyName"));
        const int keyOffset = bytes.indexOf(key, versionOffset);
        QVERIFY(keyOffset >= versionOffset + 6);
        const int start = keyOffset - 6;
        const int length = uchar(bytes[start]) | (int(uchar(bytes[start + 1])) << 8);
        const int end = start + length;
        QCOMPARE((end - versionOffset) % 4, 2);
        // These bytes belong to neither adjacent String structure. They are
        // distinct from the documented zero WORD Padding after the key.
        bytes[end] = char(0x7f);
        bytes[end + 1] = char(0x80);
    }
    const auto info = parse(bytes);
    QCOMPARE(info.status, Status::Pe);
    QCOMPARE(info.versions.size(), 2);
    QCOMPARE(info.versions[0].strings.value(QStringLiteral("040904B0/CompanyName")), expectedCompany);
    if (resourceTail) QVERIFY(info.versions[0].fixed.isEmpty());
}

void VersionTests::nonPeFileMetadataAndReadOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QString::fromUtf8("普通文件.exe"));
    const QByteArray content("plain bytes\0with binary tail", 28);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(content), qint64(content.size()));
    file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser));
    const auto before = QFileInfo(path);
    const auto modified = before.lastModified().toUTC();
    const auto permissions = before.permissions();
    const auto info = inspectFile(path);
    QCOMPARE(info.status, Status::NonPe);
    QVERIFY(info.usable());
    QCOMPARE(info.path, path);
    QCOMPARE(info.metadata.value(QStringLiteral("Size")).toLongLong(), qint64(content.size()));
    QCOMPARE(info.metadata.value(QStringLiteral("FileName")), QFileInfo(path).fileName());
    QVERIFY(!info.metadata.value(QStringLiteral("HostPlatform")).isEmpty());
    QVERIFY(!info.metadata.value(QStringLiteral("Permissions")).isEmpty());
    QVERIFY(!info.metadata.value(QStringLiteral("ModifiedUtc")).isEmpty());
    QCOMPARE(QDateTime::fromString(info.metadata.value(QStringLiteral("ModifiedUtc")), Qt::ISODateWithMs), modified);
    QVERIFY(info.versions.isEmpty());
    QVERIFY(info.imports.isEmpty());
    QVERIFY(!info.message.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), content);
    file.close();
    QCOMPARE(QFileInfo(path).lastModified().toUTC(), modified);
    QCOMPARE(QFileInfo(path).permissions(), permissions);
}

void VersionTests::nonPeByteFormats_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("one-byte") << QByteArrayLiteral("M");
    QTest::newRow("text") << QByteArrayLiteral("not a PE version file");
    QTest::newRow("ELF") << QByteArray::fromHex("7f454c46020101000000000000000000");
    QTest::newRow("Mach-O") << QByteArray::fromHex("cffaedfe0c0000010000000002000000");
}

void VersionTests::nonPeByteFormats()
{
    QFETCH(QByteArray, bytes);
    const auto info = parse(bytes);
    QCOMPARE(info.status, Status::NonPe);
    QVERIFY(info.usable());
    QVERIFY(info.versions.isEmpty());
    QVERIFY(!info.message.isEmpty());
}

void VersionTests::fileErrorsAndSizeLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(inspectFile(dir.filePath(QStringLiteral("missing.dll"))).status, Status::IoError);
    QCOMPARE(inspectFile(dir.path()).status, Status::IoError);
    QFile file(dir.filePath(QStringLiteral("fixture.dll")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto bytes = image(true);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    Limits limits;
    limits.maxFileBytes = bytes.size() - 1;
    const auto rejected = inspectFile(file.fileName(), limits);
    QCOMPARE(rejected.status, Status::LimitExceeded);
    QVERIFY(!rejected.usable());
    QVERIFY(!rejected.message.isEmpty());
    limits.maxFileBytes = bytes.size();
    QCOMPARE(inspectFile(file.fileName(), limits).status, Status::Pe);
}

void VersionTests::configuredLimits_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("file-bytes") << 0;
    QTest::newRow("section-count") << 1;
    QTest::newRow("entry-count") << 2;
    QTest::newRow("string-bytes") << 3;
    QTest::newRow("resource-depth") << 4;
}

void VersionTests::configuredLimits()
{
    QFETCH(int, kind);
    const auto bytes = image();
    Limits limits;
    if (kind == 0) limits.maxFileBytes = bytes.size() - 1;
    if (kind == 1) limits.maxSections = 2;
    if (kind == 2) limits.maxEntries = 1;
    if (kind == 3) limits.maxStringBytes = 4;
    if (kind == 4) limits.maxResourceDepth = 1;
    const auto info = parse(bytes, limits);
    QCOMPARE(info.status, Status::LimitExceeded);
    QVERIFY(!info.usable());
    QVERIFY(!info.message.isEmpty());
}

void VersionTests::malformed_data()
{
    QTest::addColumn<QByteArray>("bytes");
    auto add = [](const char *name, const QByteArray &bytes) { QTest::newRow(name) << bytes; };
    auto word = [&](const char *name, int offset, quint16 value) {
        auto bytes = image(); put16(bytes, offset, value); add(name, bytes);
    };
    auto dword = [&](const char *name, int offset, quint32 value) {
        auto bytes = image(); put32(bytes, offset, value); add(name, bytes);
    };
    add("truncated-DOS", QByteArrayLiteral("MZ"));
    add("truncated-COFF", image().left(coffOffset + 10));
    add("truncated-optional", image().left(optionalOffset + 60));
    add("truncated-section-table", image().left(sectionOffset(false) + 100));
    add("truncated-section-raw", image().left(fileSize - 1));
    dword("e-lfanew-overflow", 0x3c, 0xfffffff0);
    dword("bad-PE-signature", peOffset, 0x11111111);
    word("unsupported-optional-magic", optionalOffset, 0x107);
    word("optional-header-too-small", coffOffset + 16, 20);
    dword("directory-count-outside-optional", optionalOffset + 92, 0xffffffff);
    word("huge-section-count", coffOffset + 2, 0xffff);
    dword("section-raw-offset-overflow", sectionOffset(false) + 20, 0xfffffff0);
    dword("section-raw-size-overflow", sectionOffset(false) + 16, 0xffffffff);
    dword("section-virtual-range-overflow", sectionOffset(false) + 12, 0xfffffff0);
    dword("export-directory-unmapped-RVA", directoryOffset(false), 0xfffffff0);
    dword("export-directory-truncated", directoryOffset(false) + 4, 20);
    dword("export-function-count-huge", dataOffset + 20, 0xffffffff);
    dword("export-name-count-huge", dataOffset + 24, 0xffffffff);
    dword("export-addresses-unmapped", dataOffset + 28, 0x70000000);
    dword("export-name-RVA-unmapped", dataOffset + 0x50, 0x70000000);
    word("export-ordinal-outside-functions", dataOffset + 0x58, 3);
    dword("export-ordinal-overflow", dataOffset + 16, 0xffffffff);
    dword("import-directory-unmapped", directoryOffset(false) + 8, 0x70000000);
    dword("import-directory-missing-sentinel", directoryOffset(false) + 12, 20);
    dword("import-DLL-unmapped", importOffset + 12, 0x70000000);
    dword("import-thunk-unmapped", importOffset, 0x70000000);
    dword("import-hint-name-unmapped", thunkOffset, 0x70000000);
    dword("import-ordinal-reserved-bits", thunkOffset + 4, 0x8001002a);
    dword("resource-directory-unmapped", directoryOffset(false) + 16, 0x70000000);
    dword("resource-directory-too-short", directoryOffset(false) + 20, 20);
    word("resource-count-overflow", resourceOffset + 14, 0xffff);
    dword("resource-cycle-root", resourceOffset + 20, 0x80000000);
    dword("resource-cycle-child", resourceOffset + 0x34, 0x80000020);
    dword("resource-child-outside-directory", resourceOffset + 20, 0x8000ffff);
    dword("resource-leaf-unmapped", resourceOffset + 0x60, 0x70000000);
    dword("resource-leaf-size-overflow", resourceOffset + 0x64, 0xffffffff);
    dword("resource-name-outside-directory", resourceOffset + 0x30, 0x8000ffff);
    word("version-zero-block-length", versionOffset, 0);
    word("version-block-length-outside-leaf", versionOffset, 0xffff);
    word("version-fixed-size-outside-block", versionOffset + 2, 0xffff);
    dword("version-invalid-fixed-signature", versionOffset + 40, 0x12345678);
    {
        auto bytes = image();
        const auto key = utf16(QStringLiteral("CompanyName"));
        const int keyOffset = bytes.indexOf(key, versionOffset);
        Q_ASSERT(keyOffset >= versionOffset);
        const int paddingOffset = keyOffset + key.size();
        Q_ASSERT(paddingOffset % 4 == 2); // Documented zero-word Padding after szKey.
        bytes[paddingOffset] = char(1);
        add("version-key-padding-nonzero", bytes);
    }
    {
        auto bytes = image();
        put32(bytes, importOffset + 12, dataRva + 0x5ff);
        bytes[dataOffset + 0x5ff] = 'X';
        add("DLL-string-does-not-cross-section", bytes);
    }
    {
        auto bytes = image();
        put32(bytes, importOffset, dataRva + 0x5fc);
        put32(bytes, dataOffset + 0x5fc, 0x80000001);
        add("thunk-no-sentinel-before-section-end", bytes);
    }
    {
        auto bytes = image();
        put32(bytes, thunkOffset, dataRva + 0x5fe);
        put16(bytes, dataOffset + 0x5fe, 7);
        add("hint-without-name", bytes);
    }
    {
        auto bytes = image();
        put32(bytes, dataOffset + 0x48, dataRva + 0x17f);
        bytes[dataOffset + 0x17f] = 'X';
        add("forwarder-unterminated-in-directory", bytes);
    }
    {
        auto bytes = image(true);
        put64(bytes, thunkOffset, Q_UINT64_C(0x00000001000022a0));
        add("PE32plus-name-RVA-too-wide", bytes);
    }
    {
        auto bytes = image(true);
        put64(bytes, thunkOffset + 8, Q_UINT64_C(0x800000010000002a));
        add("PE32plus-ordinal-reserved-bits", bytes);
    }
}

void VersionTests::malformed()
{
    QFETCH(QByteArray, bytes);
    const auto info = parse(bytes);
    QVERIFY2(info.status == Status::Invalid || info.status == Status::LimitExceeded,
             qPrintable(QStringLiteral("Malformed PE accepted: status=%1 %2").arg(int(info.status)).arg(info.message)));
    QVERIFY(!info.usable());
    QVERIFY(!info.message.isEmpty());
}

void VersionTests::everyTruncationIsRejected()
{
    for (const bool plus : {false, true}) {
        const auto bytes = image(plus);
        for (int size = 2; size < bytes.size(); ++size) {
            const auto info = parse(bytes.left(size));
            QVERIFY2(info.status == Status::Invalid || info.status == Status::LimitExceeded,
                     qPrintable(QStringLiteral("Accepted truncated PE%1 at byte %2").arg(plus ? 64 : 32).arg(size)));
        }
    }
}

void VersionTests::deterministicMutationSmoke()
{
    // A bounded supplement to named regressions: no random/non-reproducible corpus.
    // Mutations need not all be invalid (e.g. timestamps are arbitrary), but parsing
    // must terminate without mutating the input or producing unbounded output.
    quint32 state = 0x4c715045;
    const auto original = image(true);
    for (int iteration = 0; iteration < 512; ++iteration) {
        auto bytes = original;
        for (int mutation = 0; mutation < 4; ++mutation) {
            state = state * 1664525u + 1013904223u;
            const int offset = int(state % quint32(bytes.size()));
            state = state * 1664525u + 1013904223u;
            bytes[offset] = char(state >> 24);
        }
        const auto snapshot = bytes;
        const auto info = parse(bytes);
        QCOMPARE(bytes, snapshot);
        QVERIFY(info.sections.size() <= 96);
        QVERIFY(info.imports.size() <= 16384);
        QVERIFY(info.exports.size() <= 16384);
        QVERIFY(info.versions.size() <= 16384);
    }
}

QTEST_APPLESS_MAIN(VersionTests)
#include "tst_version.moc"
