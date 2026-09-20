#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>

#include "registrycompare.h"

using namespace LqCompare::Registry;

namespace {
const QString base = QStringLiteral("HKEY_CURRENT_USER\\Software\\LqCompare测试");
const QString comparisonBase = QStringLiteral("HKEY_CURRENT_USER\\Software\\Comparison");

QString fixture(const char *name)
{
    return QString::fromUtf8(REGISTRY_FIXTURE_DIR) + QLatin1Char('/') + QString::fromLatin1(name);
}

QByteArray document(const QByteArray &body)
{
    return "Windows Registry Editor Version 5.00\n\n" + body;
}

const Difference *entry(const Comparison &result, EntryKind kind,
                        const QString &path, const QString &name = {})
{
    for (const Difference &difference : result.entries) {
        if (difference.kind == kind && identity(difference.path) == identity(path)
            && identity(difference.valueName) == identity(name))
            return &difference;
    }
    return nullptr;
}

void insertKey(Snapshot &snapshot, const QString &path, const QString &error = {},
               quint32 nativeError = 0)
{
    Key key;
    key.path = path;
    key.error = error;
    key.nativeError = nativeError;
    snapshot.keys.insert(identity(path), key);
}

void insertValue(Snapshot &snapshot, const QString &path, const QString &name,
                 quint32 type, const QByteArray &data, bool deleted = false)
{
    Value value;
    value.name = name;
    value.type = type;
    value.data = data;
    value.deleted = deleted;
    snapshot.keys[identity(path)].values.insert(identity(name), value);
}
}

class RegistryTests final : public QObject
{
    Q_OBJECT
private slots:
    void storedUtf16LittleEndianFixture();
    void storedUtf16BigEndianFixture();
    void storedAnsiFixtures();
    void legacyAnsiHexStringTypes();
    void utf8BomIsStrict();
    void quotedNamesAndComments();
    void malformedTypedBytesRemainExact();
    void stringBytesAndFormatting();
    void rootCanonicalization_data();
    void rootCanonicalization();
    void unknownRootsAreRejected();
    void duplicateIdentityUsesLastInstruction();
    void emptyKeyAndDeletionInstructions();
    void malformedInputs_data();
    void malformedInputs();
    void parserLimitsAreAtomic();
    void fileIoFailureIsExplicit();
    void realFileComparison();
    void caseInsensitiveIdentityKeepsDataSensitive();
    void sharpSIdentityDoesNotCollapse();
    void multiStringElementChange();
    void largeDataComparisonUsesAllBytes();
    void keyDeletionIsOperationChange();
    void unreadableSubtreeNeverBecomesMissingOrEqual();
    void unreadableOnBothSidesIsNotEqual();
    void memoryProviderFiltersRoot();
    void memoryProviderMissingRoot();
    void memoryProviderPreservesUnreadable();
    void memoryProviderSparseSnapshot();
    void memoryProviderLimits();
    void platformAvailability();
    void windowsReadOnlyMissingKeySmoke();
};

void RegistryTests::storedUtf16LittleEndianFixture()
{
    const ReadResult result = readRegFile(fixture("unicode-types-utf16le.reg"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.snapshot.keys.contains(identity(base)));
    const Key key = result.snapshot.keys.value(identity(base));
    QCOMPARE(key.path, base);
    QCOMPARE(key.values.value(QString()).name, QString());
    QCOMPARE(key.values.value(QString()).type, quint32(1));
    QCOMPARE(key.values.value(QString()).data, QByteArray::fromHex("d89ea48b3c500000"));
    QCOMPARE(key.values.value(identity("Path")).data,
             encodeString(QStringLiteral("C:\\Users\\测试\\\"quoted\"")));
    QCOMPARE(key.values.value(identity("Emoji")).data, QByteArray::fromHex("3dd811dd0000"));
    QCOMPARE(key.values.value(identity("Enabled")).type, quint32(4));
    QCOMPARE(key.values.value(identity("Enabled")).data, QByteArray::fromHex("2a000000"));
    QCOMPARE(key.values.value(identity("Wide")).type, quint32(11));
    QCOMPARE(key.values.value(identity("Wide")).data, QByteArray::fromHex("efcdab8967452301"));
    QCOMPARE(key.values.value(identity("Blob")).type, quint32(3));
    QCOMPARE(key.values.value(identity("Blob")).data, QByteArray::fromHex("00017f80ff"));
    QCOMPARE(key.values.value(identity("Continued")).data, QByteArray::fromHex("0102030405"));
    QCOMPARE(key.values.value(identity("Expand")).type, quint32(2));
    QCOMPARE(key.values.value(identity("Expand")).data,
             QByteArray::fromHex("2500540045004d00500025000000"));
    QCOMPARE(key.values.value(identity("Multi")).type, quint32(7));
    QCOMPARE(multiStrings(key.values.value(identity("Multi")).data),
             QStringList({QStringLiteral("one"), QStringLiteral("二")}));
    QCOMPARE(key.values.value(identity("DwordHex")).type, quint32(4));
    QCOMPARE(key.values.value(identity("DwordHex")).data, QByteArray::fromHex("2a000000"));
    QCOMPARE(key.values.value(identity("QwordHex")).type, quint32(11));
    QCOMPARE(key.values.value(identity("QwordHex")).data, QByteArray::fromHex("efcdab8967452301"));
    QCOMPARE(key.values.value(identity("Unknown")).type, quint32(0x1234));
    QCOMPARE(key.values.value(identity("Unknown")).data, QByteArray::fromHex("aabb"));
    QVERIFY(key.values.value(identity("EmptyBinary")).data.isEmpty());
    QCOMPARE(key.values.value(identity("EmptyBinary")).type, quint32(3));
    QVERIFY(key.values.value(identity("Removed")).deleted);
    QVERIFY(result.snapshot.keys.contains(identity(base + "\\Empty")));
    QVERIFY(result.snapshot.keys.value(identity(base + "\\Empty")).values.isEmpty());
    QVERIFY(result.snapshot.keys.value(identity(base + "\\Old")).deleted);
}

void RegistryTests::storedUtf16BigEndianFixture()
{
    const ReadResult result = readRegFile(fixture("unicode-utf16be.reg"));
    QVERIFY2(result.ok, qPrintable(result.error));
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Software\\大端");
    QVERIFY(result.snapshot.keys.contains(identity(path)));
    QCOMPARE(result.snapshot.keys.value(identity(path)).values.value(QString()).data,
             QByteArray::fromHex("604f7d5920003dd811dd0000"));
}

void RegistryTests::storedAnsiFixtures()
{
    const ReadResult western = readRegFile(fixture("ansi-windows1252.reg"));
    QVERIFY2(western.ok, qPrintable(western.error));
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Software\\Café");
    QVERIFY(western.snapshot.keys.contains(identity(path)));
    QCOMPARE(western.snapshot.keys.value(identity(path)).values.value(identity("Currency")).data,
             QByteArray::fromHex("ac202000a3002000630061006600e9000000"));

    ReadOptions options;
    options.ansiCodec = "GB18030";
    const ReadResult chinese = readRegFile(fixture("ansi-gb18030.reg"), options);
    QVERIFY2(chinese.ok, qPrintable(chinese.error));
    const QString chinesePath = QStringLiteral("HKEY_CURRENT_USER\\Software\\中文");
    QVERIFY(chinese.snapshot.keys.contains(identity(chinesePath)));
    QCOMPARE(chinese.snapshot.keys.value(identity(chinesePath)).values.value(identity(QStringLiteral("消息"))).data,
             QByteArray::fromHex("e86c8c516888fc5bfa510000"));

    options.ansiCodec = "this-codec-does-not-exist";
    const ReadResult invalidCodec = readRegFile(fixture("ansi-windows1252.reg"), options);
    QVERIFY(!invalidCodec.ok);
    QVERIFY(!invalidCodec.error.isEmpty());
    QVERIFY(invalidCodec.snapshot.keys.isEmpty());
}

void RegistryTests::stringBytesAndFormatting()
{
    QCOMPARE(encodeString(QStringLiteral("A二🔑")), QByteArray::fromHex("41008c4e3dd811dd0000"));
    QCOMPARE(encodeString(QString()), QByteArray::fromHex("0000"));
    QCOMPARE(multiStrings(QByteArray::fromHex("00000000")), QStringList());
    Value dword;
    dword.type = 4;
    dword.data = QByteArray::fromHex("2a000000");
    const QString formatted = displayValue(dword);
    QVERIFY2(formatted.contains("42"), qPrintable(formatted));
    QVERIFY2(formatted.contains("2a", Qt::CaseInsensitive), qPrintable(formatted));
    QCOMPARE(typeName(1), QStringLiteral("REG_SZ"));
    QCOMPARE(typeName(4), QStringLiteral("REG_DWORD"));
    QCOMPARE(typeName(7), QStringLiteral("REG_MULTI_SZ"));
    QCOMPARE(typeName(11), QStringLiteral("REG_QWORD"));
    QVERIFY(!typeName(0x1234).isEmpty());
    Value blob;
    blob.type = 3;
    blob.data = QByteArray::fromHex("00abff");
    QVERIFY(displayValue(blob).contains("ab", Qt::CaseInsensitive));
}

void RegistryTests::legacyAnsiHexStringTypes()
{
    const ReadResult result = parseReg(
        "REGEDIT4\r\n\r\n[HKCU\\Software\\Legacy]\r\n"
        "\"Expand\"=hex(2):25,54,45,4d,50,25,00\r\n"
        "\"Multi\"=hex(7):63,61,66,e9,00,80,00,00\r\n"
        "\"String\"=hex(1):63,61,66,e9,00\r\n"
        "\"NoTerminator\"=hex(1):e9\r\n");
    QVERIFY2(result.ok, qPrintable(result.error));
    const Key key = result.snapshot.keys.value(identity("HKEY_CURRENT_USER\\Software\\Legacy"));
    QCOMPARE(key.values.value(identity("Expand")).data, QByteArray::fromHex("2500540045004d00500025000000"));
    QCOMPARE(key.values.value(identity("Multi")).data, QByteArray::fromHex("630061006600e9000000ac2000000000"));
    QCOMPARE(multiStrings(key.values.value(identity("Multi")).data),
             QStringList({QStringLiteral("café"), QStringLiteral("€")}));
    QCOMPARE(key.values.value(identity("String")).data, QByteArray::fromHex("630061006600e9000000"));
    QCOMPARE(key.values.value(identity("NoTerminator")).data, QByteArray::fromHex("e900"));
    QVERIFY(displayValue(key.values.value(identity("NoTerminator"))).contains("Malformed", Qt::CaseInsensitive));
}

void RegistryTests::utf8BomIsStrict()
{
    const QByteArray bom = QByteArray::fromHex("efbbbf");
    const QByteArray bytes = bom + document(QStringLiteral("[HKCU\\Software\\UTF8]\n\"键\"=\"值🔑\"\n").toUtf8());
    const ReadResult valid = parseReg(bytes);
    QVERIFY2(valid.ok, qPrintable(valid.error));
    const Key key = valid.snapshot.keys.value(identity("HKEY_CURRENT_USER\\Software\\UTF8"));
    QCOMPARE(key.values.value(identity(QStringLiteral("键"))).data, QByteArray::fromHex("3c503dd811dd0000"));
    const ReadResult invalid = parseReg(bom + document("[HKCU\\Software\\UTF8]\n\"Value\"=\"")
                                         + QByteArray::fromHex("c328") + "\"\n");
    QVERIFY(!invalid.ok);
    QVERIFY(!invalid.error.isEmpty());
    QVERIFY(invalid.snapshot.keys.isEmpty());
}

void RegistryTests::quotedNamesAndComments()
{
    const ReadResult result = parseReg(document(
        "# full line comment\n[HKCU\\Software\\Names] ; key comment with ] and [ delimiters\n"
        "\"name=;part\"=hex:01,02,\\ ; first line comment\n  03,04 ; final byte comment\n"
        "\"quoted\\\"\\\\\"=\"literal;=#text\" ; value comment\n"
        "\"DWORD;=\"=dword:0000002a ; integer comment\n"));
    QVERIFY2(result.ok, qPrintable(result.error));
    const Key key = result.snapshot.keys.value(identity("HKEY_CURRENT_USER\\Software\\Names"));
    QCOMPARE(key.values.value(identity("name=;part")).data, QByteArray::fromHex("01020304"));
    const QString quotedName = QStringLiteral("quoted\"\\");
    QVERIFY(key.values.contains(identity(quotedName)));
    QCOMPARE(key.values.value(identity(quotedName)).data, encodeString("literal;=#text"));
    QCOMPARE(key.values.value(identity("DWORD;=")).data, QByteArray::fromHex("2a000000"));
}

void RegistryTests::malformedTypedBytesRemainExact()
{
    const ReadResult left = parseReg(document(
        "[HKCU\\Software\\MalformedData]\n\"String\"=hex(1):41\n"
        "\"Multi\"=hex(7):61,00,00,00\n\"Dword\"=hex(4):01,00\n"));
    const ReadResult right = parseReg(document(
        "[HKCU\\Software\\MalformedData]\n\"String\"=hex(1):42\n"
        "\"Multi\"=hex(7):61,00,00,00\n\"Dword\"=hex(4):01,00\n"));
    QVERIFY2(left.ok, qPrintable(left.error));
    QVERIFY2(right.ok, qPrintable(right.error));
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Software\\MalformedData");
    const Key key = left.snapshot.keys.value(identity(path));
    QCOMPARE(key.values.value(identity("String")).data, QByteArray::fromHex("41"));
    QCOMPARE(key.values.value(identity("Multi")).data, QByteArray::fromHex("61000000"));
    QCOMPARE(key.values.value(identity("Dword")).data, QByteArray::fromHex("0100"));
    QVERIFY(displayValue(key.values.value(identity("String"))).contains("Malformed", Qt::CaseInsensitive));
    QVERIFY(displayValue(key.values.value(identity("Multi"))).contains("Malformed", Qt::CaseInsensitive));
    const Comparison result = compare(left.snapshot, right.snapshot);
    QCOMPARE(result.differenceCount, 1);
    const Difference *difference = entry(result, EntryKind::Value, path, "String");
    QVERIFY(difference);
    QCOMPARE(int(difference->status), int(Status::DataChanged));
}

void RegistryTests::rootCanonicalization_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("hkcu") << QStringLiteral("HKCU\\Software\\Case") << QStringLiteral("HKEY_CURRENT_USER\\Software\\Case");
    QTest::newRow("hklm") << QStringLiteral("hklm\\Software") << QStringLiteral("HKEY_LOCAL_MACHINE\\Software");
    QTest::newRow("hkcr") << QStringLiteral("HKCR") << QStringLiteral("HKEY_CLASSES_ROOT");
    QTest::newRow("hku") << QStringLiteral("HKU") << QStringLiteral("HKEY_USERS");
    QTest::newRow("hkcc") << QStringLiteral("HKCC") << QStringLiteral("HKEY_CURRENT_CONFIG");
    QTest::newRow("long-lowercase") << QStringLiteral("hkey_current_user\\Software") << QStringLiteral("HKEY_CURRENT_USER\\Software");
}

void RegistryTests::rootCanonicalization()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QString error;
    QCOMPARE(canonicalKeyPath(input, &error), expected);
    QVERIFY(error.isEmpty());
}

void RegistryTests::unknownRootsAreRejected()
{
    QString error;
    QVERIFY(canonicalKeyPath(QStringLiteral("NOT_A_HIVE\\Software"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    const ReadResult result = parseReg(document("[NOT_A_HIVE\\Software]\n"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorLine, 3);
    QVERIFY(result.snapshot.keys.isEmpty());
}

void RegistryTests::duplicateIdentityUsesLastInstruction()
{
    const ReadResult result = parseReg(document(
        "[HKEY_CURRENT_USER\\Software\\Case]\n\"Name\"=dword:00000001\n"
        "[hkey_current_user\\software\\CASE]\n\"NAME\"=dword:00000002\n"));
    QVERIFY2(result.ok, qPrintable(result.error));
    const QString keyId = identity(QStringLiteral("HKEY_CURRENT_USER\\Software\\Case"));
    QVERIFY(result.snapshot.keys.contains(keyId));
    const Key key = result.snapshot.keys.value(keyId);
    QCOMPARE(key.values.size(), 1);
    QCOMPARE(key.values.value(identity("name")).data, QByteArray::fromHex("02000000"));
}

void RegistryTests::emptyKeyAndDeletionInstructions()
{
    const ReadResult result = parseReg(document(
        "; comment\n[HKEY_CURRENT_USER\\Software\\Empty]\n\n"
        "[HKEY_CURRENT_USER\\Software\\Values]\n@=-\n\"gone\"=-\n"
        "[-HKEY_CURRENT_USER\\Software\\Delete]\n"));
    QVERIFY2(result.ok, qPrintable(result.error));
    const Key empty = result.snapshot.keys.value(identity("HKEY_CURRENT_USER\\Software\\Empty"));
    QVERIFY(!empty.path.isEmpty());
    QVERIFY(empty.values.isEmpty());
    QVERIFY(!empty.deleted);
    const Key values = result.snapshot.keys.value(identity("HKEY_CURRENT_USER\\Software\\Values"));
    QVERIFY(values.values.value(QString()).deleted);
    QVERIFY(values.values.value(identity("gone")).deleted);
    const Key removed = result.snapshot.keys.value(identity("HKEY_CURRENT_USER\\Software\\Delete"));
    QVERIFY(!removed.path.isEmpty());
    QVERIFY(removed.deleted);
}

void RegistryTests::malformedInputs_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<int>("line");
    QTest::newRow("bad-header") << QByteArray("not a registry export\n") << 1;
    QTest::newRow("value-before-key") << document("\"v\"=dword:00000001\n") << 3;
    const QByteArray prefix = document("[HKEY_CURRENT_USER\\Software\\Atomic]\n\"good\"=\"retained only on success\"\n");
    QTest::newRow("unclosed-key") << (prefix + "[HKEY_CURRENT_USER\\Broken\n") << 5;
    QTest::newRow("unclosed-name") << (prefix + "\"broken=\"value\"\n") << 5;
    QTest::newRow("missing-equals") << (prefix + "\"broken\"\"value\"\n") << 5;
    QTest::newRow("unclosed-string") << (prefix + "\"broken\"=\"value\n") << 5;
    QTest::newRow("garbage-after-string") << (prefix + "\"broken\"=\"value\" garbage\n") << 5;
    QTest::newRow("unknown-value-syntax") << (prefix + "\"broken\"=banana:00\n") << 5;
    QTest::newRow("nonhex-dword") << (prefix + "\"broken\"=dword:zz000000\n") << 5;
    QTest::newRow("overflow-dword") << (prefix + "\"broken\"=dword:100000000\n") << 5;
    QTest::newRow("overflow-qword") << (prefix + "\"broken\"=qword:10000000000000000\n") << 5;
    QTest::newRow("bad-hex-byte") << (prefix + "\"broken\"=hex:00,gg\n") << 5;
    QTest::newRow("oversized-hex-byte") << (prefix + "\"broken\"=hex:00,123\n") << 5;
    QTest::newRow("hex-double-comma") << (prefix + "\"broken\"=hex:00,,01\n") << 5;
    QTest::newRow("bad-hex-type") << (prefix + "\"broken\"=hex(x):00\n") << 5;
    QTest::newRow("overflow-hex-type") << (prefix + "\"broken\"=hex(100000000):00\n") << 5;
    QTest::newRow("dangling-continuation") << (prefix + "\"broken\"=hex:00,\\") << 5;
    QTest::newRow("continuation-without-comma") << (prefix + "\"broken\"=hex:00\\\n01\n") << 5;
    QTest::newRow("invalid-escape") << (prefix + "\"broken\"=\"bad\\q\"\n") << 5;
    QTest::newRow("value-in-deleted-key") << (prefix + "[-HKCU\\Deleted]\n\"broken\"=\"v\"\n") << 6;
    QTest::newRow("conflicting-key-instructions") << (prefix + "[-HKEY_CURRENT_USER\\Software\\Atomic]\n") << 5;
    QTest::newRow("delete-parent-before-child") << (prefix + "[-HKCU\\Parent]\n[HKCU\\Parent\\Child]\n") << 6;
    QTest::newRow("delete-parent-after-child") << (prefix + "[HKCU\\Parent\\Child]\n[-HKCU\\Parent]\n") << 6;
    QTest::newRow("odd-utf16") << QByteArray::fromHex("fffe5700ff") << 0;
    QTest::newRow("unpaired-utf16-surrogate") << QByteArray::fromHex("fffe00d84100") << 0;
    QTest::newRow("utf16-without-bom") << QByteArray("W\0i\0n\0", 6) << 0;
}

void RegistryTests::malformedInputs()
{
    QFETCH(QByteArray, bytes);
    QFETCH(int, line);
    const ReadResult result = parseReg(bytes, QStringLiteral("malformed.reg"));
    QVERIFY2(!result.ok, bytes.constData());
    QVERIFY(!result.error.isEmpty());
    QVERIFY2(result.snapshot.keys.isEmpty(), "Failed parses must not expose partially parsed keys");
    if (line > 0)
        QCOMPARE(result.errorLine, line);
}

void RegistryTests::parserLimitsAreAtomic()
{
    const QByteArray bytes = document("[HKEY_CURRENT_USER\\A\\B\\C]\n\"one\"=\"a\"\n\"two\"=\"b\"\n[HKEY_CURRENT_USER\\Other]\n");
    ReadOptions options;
    options.maxBytes = bytes.size() - 1;
    ReadResult result = parseReg(bytes, {}, options);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.snapshot.keys.isEmpty());
    options = ReadOptions();
    options.maxKeys = 1;
    result = parseReg(bytes, {}, options);
    QVERIFY(!result.ok);
    QVERIFY(result.snapshot.keys.isEmpty());
    options = ReadOptions();
    options.maxValues = 1;
    result = parseReg(bytes, {}, options);
    QVERIFY(!result.ok);
    QVERIFY(result.snapshot.keys.isEmpty());
    options = ReadOptions();
    options.maxDepth = 1;
    result = parseReg(bytes, {}, options);
    QVERIFY(!result.ok);
    QVERIFY(result.snapshot.keys.isEmpty());
    options = ReadOptions();
    options.maxBytes = 4;
    result = readRegFile(fixture("unicode-types-utf16le.reg"), options);
    QVERIFY(!result.ok);
    QVERIFY(result.snapshot.keys.isEmpty());
}

void RegistryTests::fileIoFailureIsExplicit()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const ReadResult missing = readRegFile(temporary.filePath("missing.reg"));
    QVERIFY(!missing.ok);
    QVERIFY(!missing.error.isEmpty());
    QVERIFY(missing.snapshot.keys.isEmpty());
    const ReadResult directory = readRegFile(temporary.path());
    QVERIFY(!directory.ok);
    QVERIFY(!directory.error.isEmpty());
    QVERIFY(directory.snapshot.keys.isEmpty());
}

void RegistryTests::realFileComparison()
{
    const ReadResult left = readRegFile(fixture("compare-left.reg"));
    const ReadResult right = readRegFile(fixture("compare-right.reg"));
    QVERIFY2(left.ok, qPrintable(left.error));
    QVERIFY2(right.ok, qPrintable(right.error));
    const Comparison result = compare(left.snapshot, right.snapshot);
    QVERIFY(result.complete());
    QVERIFY(!result.equal());
    QCOMPARE(result.unreadableKeys, 0);
    QCOMPARE(result.differenceCount, 9);
    const struct { const char *name; Status status; } expectations[] = {
        {"", Status::Equal}, {"Type", Status::TypeChanged},
        {"Data", Status::DataChanged}, {"BlobLength", Status::DataChanged},
        {"Multi", Status::DataChanged}, {"Left", Status::OnlyLeft},
        {"Right", Status::OnlyRight}, {"Operation", Status::OperationChanged}
    };
    for (const auto &expectation : expectations) {
        const Difference *found = entry(result, EntryKind::Value, comparisonBase,
                                         QString::fromLatin1(expectation.name));
        QVERIFY2(found, expectation.name);
        QCOMPARE(int(found->status), int(expectation.status));
    }
    const Difference *emptyLeft = entry(result, EntryKind::Key, comparisonBase + "\\EmptyLeft");
    const Difference *emptyRight = entry(result, EntryKind::Key, comparisonBase + "\\EmptyRight");
    QVERIFY(emptyLeft);
    QVERIFY(emptyRight);
    QCOMPARE(int(emptyLeft->status), int(Status::OnlyLeft));
    QCOMPARE(int(emptyRight->status), int(Status::OnlyRight));
    QVERIFY(compare(left.snapshot, left.snapshot).equal());
}

void RegistryTests::caseInsensitiveIdentityKeepsDataSensitive()
{
    const ReadResult left = parseReg(document("[HKEY_CURRENT_USER\\Software\\Case]\n\"Name\"=\"Value\"\n"));
    const ReadResult right = parseReg(document("[hkey_current_user\\software\\CASE]\n\"NAME\"=\"Value\"\n"));
    const ReadResult changed = parseReg(document("[hkey_current_user\\software\\CASE]\n\"NAME\"=\"value\"\n"));
    QVERIFY(left.ok && right.ok && changed.ok);
    QVERIFY(compare(left.snapshot, right.snapshot).equal());
    const Comparison diff = compare(left.snapshot, changed.snapshot);
    QCOMPARE(diff.differenceCount, 1);
    const Difference *value = entry(diff, EntryKind::Value,
                                     QStringLiteral("HKEY_CURRENT_USER\\Software\\Case"), "name");
    QVERIFY(value);
    QCOMPARE(int(value->status), int(Status::DataChanged));
}

void RegistryTests::keyDeletionIsOperationChange()
{
    const ReadResult left = parseReg(document("[-HKEY_CURRENT_USER\\Software\\Delete]\n"));
    const ReadResult right = parseReg(document("[HKEY_CURRENT_USER\\Software\\Delete]\n"));
    QVERIFY(left.ok && right.ok);
    const Comparison result = compare(left.snapshot, right.snapshot);
    QCOMPARE(result.differenceCount, 1);
    const Difference *key = entry(result, EntryKind::Key, "HKEY_CURRENT_USER\\Software\\Delete");
    QVERIFY(key);
    QCOMPARE(int(key->status), int(Status::OperationChanged));
    QVERIFY(compare(left.snapshot, left.snapshot).equal());
}

void RegistryTests::sharpSIdentityDoesNotCollapse()
{
    QVERIFY(identity(QStringLiteral("Straße")) != identity(QStringLiteral("Strasse")));
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Identity");
    Snapshot left;
    Snapshot right;
    insertKey(left, path);
    insertKey(right, path);
    insertValue(left, path, QStringLiteral("Straße"), 1, encodeString("same"));
    insertValue(right, path, QStringLiteral("Strasse"), 1, encodeString("same"));
    const Comparison result = compare(left, right);
    QCOMPARE(result.differenceCount, 2);
    const Difference *onlyLeft = entry(result, EntryKind::Value, path, QStringLiteral("Straße"));
    const Difference *onlyRight = entry(result, EntryKind::Value, path, QStringLiteral("Strasse"));
    QVERIFY(onlyLeft && onlyRight);
    QCOMPARE(int(onlyLeft->status), int(Status::OnlyLeft));
    QCOMPARE(int(onlyRight->status), int(Status::OnlyRight));
}

void RegistryTests::multiStringElementChange()
{
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Multi");
    Snapshot left;
    Snapshot right;
    insertKey(left, path);
    insertKey(right, path);
    insertValue(left, path, "Array", 7, QByteArray::fromHex("61000000620000000000"));
    insertValue(right, path, "Array", 7, QByteArray::fromHex("61000000630000000000"));
    const Comparison result = compare(left, right);
    QCOMPARE(result.differenceCount, 1);
    const Difference *changed = entry(result, EntryKind::Value, path, "Array");
    QVERIFY(changed);
    QCOMPARE(int(changed->status), int(Status::DataChanged));
    QCOMPARE(multiStrings(changed->left.data), QStringList({"a", "b"}));
    QCOMPARE(multiStrings(changed->right.data), QStringList({"a", "c"}));
}

void RegistryTests::largeDataComparisonUsesAllBytes()
{
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Large");
    Snapshot left;
    Snapshot right;
    insertKey(left, path);
    insertKey(right, path);
    QByteArray first(65536, 'a');
    QByteArray second = first;
    second[second.size() - 1] = 'b';
    insertValue(left, path, "Blob", 3, first);
    insertValue(right, path, "Blob", 3, second);
    const Value value = left.keys.value(identity(path)).values.value(identity("Blob"));
    QVERIFY(displayValue(value).size() < 4096);
    const Comparison result = compare(left, right);
    QCOMPARE(result.differenceCount, 1);
    const Difference *changed = entry(result, EntryKind::Value, path, "Blob");
    QVERIFY(changed);
    QCOMPARE(int(changed->status), int(Status::DataChanged));
    QCOMPARE(changed->left.data, first);
    QCOMPARE(changed->right.data, second);
}

void RegistryTests::unreadableSubtreeNeverBecomesMissingOrEqual()
{
    const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\Protected");
    Snapshot left;
    Snapshot right;
    insertKey(left, root, QStringLiteral("Access denied"), 5);
    insertKey(right, root);
    insertValue(right, root, "Value", 4, QByteArray::fromHex("01000000"));
    insertKey(right, root + "\\Child");
    insertValue(right, root + "\\Child", "Nested", 1, encodeString("unknown on left"));
    insertKey(right, root + "Sibling");
    const Comparison result = compare(left, right);
    QVERIFY(!result.complete());
    QVERIFY(!result.equal());
    QVERIFY(result.unreadableKeys >= 1);
    const Difference *unreadable = entry(result, EntryKind::Key, root);
    QVERIFY(unreadable);
    QCOMPARE(int(unreadable->status), int(Status::Unreadable));
    // Descendant values/keys may be omitted or marked unreadable, never treated as known.
    for (const Difference &difference : result.entries) {
        if (identity(difference.path) == identity(root)
            || identity(difference.path).startsWith(identity(root + '\\')))
            QCOMPARE(int(difference.status), int(Status::Unreadable));
    }
    const Difference *sibling = entry(result, EntryKind::Key, root + "Sibling");
    QVERIFY(sibling);
    QCOMPARE(int(sibling->status), int(Status::OnlyRight));
    QCOMPARE(result.differenceCount, 1);
}

void RegistryTests::unreadableOnBothSidesIsNotEqual()
{
    Snapshot snapshot;
    insertKey(snapshot, "HKEY_CURRENT_USER\\Protected", "Access denied", 5);
    const Comparison result = compare(snapshot, snapshot);
    QVERIFY(!result.complete());
    QVERIFY(!result.equal());
    QCOMPARE(result.differenceCount, 0);
    QVERIFY(result.unreadableKeys > 0);
}

void RegistryTests::memoryProviderFiltersRoot()
{
    const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\Fixture");
    Snapshot snapshot;
    insertKey(snapshot, root);
    insertKey(snapshot, root + "\\Child");
    insertValue(snapshot, root + "\\Child", "Value", 1, encodeString("present"));
    insertKey(snapshot, root + "Other");
    MemoryProvider provider(snapshot);
    const ReadResult result = provider.read(QStringLiteral("hkcu\\software\\FIXTURE"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.snapshot.keys.contains(identity(root)));
    QVERIFY(result.snapshot.keys.contains(identity(root + "\\Child")));
    QVERIFY(!result.snapshot.keys.contains(identity(root + "Other")));
    QCOMPARE(result.snapshot.keys.value(identity(root + "\\Child")).values.value(identity("Value")).data,
             encodeString("present"));
}

void RegistryTests::memoryProviderMissingRoot()
{
    MemoryProvider provider(Snapshot{});
    const ReadResult result = provider.read(QStringLiteral("HKCU\\Absent"));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    const QString keyId = identity("HKEY_CURRENT_USER\\Absent");
    QVERIFY(result.snapshot.keys.contains(keyId));
    QVERIFY(!result.snapshot.keys.value(keyId).error.isEmpty());
    QVERIFY(!compare(result.snapshot, result.snapshot).equal());
}

void RegistryTests::memoryProviderPreservesUnreadable()
{
    const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\Protected");
    Snapshot snapshot;
    insertKey(snapshot, root, "Access denied", 5);
    MemoryProvider provider(snapshot);
    const ReadResult direct = provider.read(root);
    QVERIFY(direct.ok);
    QCOMPARE(direct.snapshot.keys.value(identity(root)).nativeError, quint32(5));
    QVERIFY(!direct.snapshot.keys.value(identity(root)).error.isEmpty());
    const ReadResult child = provider.read(root + "\\Child");
    QVERIFY(child.ok);
    QCOMPARE(child.snapshot.keys.value(identity(root + "\\Child")).nativeError, quint32(5));
    QVERIFY(!child.snapshot.keys.value(identity(root + "\\Child")).error.isEmpty());
    QVERIFY(!compare(direct.snapshot, direct.snapshot).equal());
    QVERIFY(!compare(child.snapshot, child.snapshot).equal());
}

void RegistryTests::memoryProviderLimits()
{
    const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\Limits");
    Snapshot snapshot;
    insertKey(snapshot, root);
    insertKey(snapshot, root + "\\Child");
    insertKey(snapshot, root + "\\Child\\Grandchild");
    insertValue(snapshot, root, "One", 3, QByteArray(100, 'a'));
    insertValue(snapshot, root, "Two", 3, QByteArray(100, 'b'));
    MemoryProvider provider(snapshot);
    for (int limit = 0; limit < 4; ++limit) {
        ReadOptions options;
        if (limit == 0) options.maxDepth = 1;
        if (limit == 1) options.maxKeys = 1;
        if (limit == 2) options.maxValues = 1;
        if (limit == 3) options.maxBytes = 1;
        const ReadResult result = provider.read(root, options);
        QVERIFY(result.ok);
        QVERIFY(!result.snapshot.keys.value(identity(root)).error.isEmpty());
        QVERIFY(!compare(result.snapshot, result.snapshot).equal());
    }
}

void RegistryTests::memoryProviderSparseSnapshot()
{
    const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\Sparse");
    Snapshot snapshot;
    insertKey(snapshot, root + "\\Middle\\Leaf");
    MemoryProvider provider(snapshot);
    const ReadResult result = provider.read(root);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.snapshot.keys.contains(identity(root)));
    QVERIFY(result.snapshot.keys.contains(identity(root + "\\Middle")));
    QVERIFY(result.snapshot.keys.contains(identity(root + "\\Middle\\Leaf")));
    QVERIFY(compare(result.snapshot, result.snapshot).equal());
}

void RegistryTests::platformAvailability()
{
    const std::unique_ptr<Provider> provider = createLocalProvider();
    QVERIFY(provider);
    QVERIFY(!localProviderDescription().isEmpty());
#ifdef Q_OS_WIN
    QVERIFY(localProviderAvailable());
#else
    QVERIFY(!localProviderAvailable());
    const ReadResult result = provider->read();
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    if (!result.snapshot.keys.isEmpty())
        QVERIFY(!compare(result.snapshot, result.snapshot).equal());
#endif
}

void RegistryTests::windowsReadOnlyMissingKeySmoke()
{
#ifndef Q_OS_WIN
    QSKIP("Windows native Unicode registry enumeration is not executable on this platform; file and memory tests remain active.");
#else
    const QString path = QStringLiteral("HKEY_CURRENT_USER\\Software\\LqCompareReadOnlyTest_")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const std::unique_ptr<Provider> provider = createLocalProvider();
    ReadOptions options;
    options.maxKeys = 8;
    options.maxValues = 8;
    options.maxBytes = 4096;
    // No creation/import/write is performed. Missing/denied must remain unknown.
    const ReadResult result = provider->read(path, options);
    QVERIFY(result.ok);
    QVERIFY(result.snapshot.keys.contains(identity(path)));
    const Key key = result.snapshot.keys.value(identity(path));
    QVERIFY(!key.error.isEmpty());
    QVERIFY(key.nativeError != 0);
    QVERIFY(!compare(result.snapshot, result.snapshot).equal());
#endif
}

QTEST_APPLESS_MAIN(RegistryTests)
#include "tst_registry.moc"
