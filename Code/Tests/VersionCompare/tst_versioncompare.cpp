#include <QtTest>
#include "../../Services/Version/versioncompare.h"

using namespace LqCompare::Version;

namespace {
const Row *findRow(const QVector<Row> &rows, const QString &group, const QString &key)
{
    for (const Row &row : rows)
        if (row.group == group && row.key == key)
            return &row;
    return nullptr;
}

QVector<Row> groupRows(const QVector<Row> &rows, const QString &group)
{
    QVector<Row> found;
    for (const Row &row : rows)
        if (row.group == group)
            found.append(row);
    return found;
}

VersionResource version(const QString &language = QStringLiteral("1033"))
{
    VersionResource result;
    result.resourceName = QStringLiteral("1");
    result.language = language;
    return result;
}

const QString EnglishVersionGroup = QStringLiteral("Version — resource 1 / language 1033");
}

class VersionCompareTests : public QObject
{
    Q_OBJECT
private slots:
    void numericVersions_data();
    void numericVersions();
    void numericInputLimit();
    void fieldDifferences();
    void versionTextAndNumericRelationAreIndependent();
    void ignoredVersionFieldsLeaveOtherChangesVisible();
    void comparisonForwardsNumericOptions();
    void languagesAndCodePagesRemainSeparate();
    void duplicateResourcesRemainVisible();
    void importsPreserveNamesOrdinalsAndHints();
    void importDependencyWithoutSymbolsIsExplicit();
    void exportsPreserveNamesOrdinalsAddressesAndForwarders();
    void duplicateSymbolsRemainVisible();
    void importNamesCannotOverwriteDuplicateRows();
    void exportNamesCannotOverwriteDuplicateRows();
    void structuralFieldsRemainSeparate();
    void nonPeMetadataReportsNoVersion();
    void csvPreservesSpecialCharacters();
    void csvExportsAllDifferencesAndNumericRelations();
};

void VersionCompareTests::numericVersions_data()
{
    QTest::addColumn<QString>("left");
    QTest::addColumn<QString>("right");
    QTest::addColumn<bool>("padMissing");
    QTest::addColumn<bool>("allowV");
    QTest::addColumn<int>("expected");
    const auto row = [](const char *name, const QString &left, const QString &right,
                        Relation expected, bool padMissing = true, bool allowV = true) {
        QTest::newRow(name) << left << right << padMissing << allowV << int(expected);
    };
    row("same", "1.2.3.4", "1.2.3.4", Relation::Equal);
    row("decimal-not-lexicographic", "1.10.0", "1.9.0", Relation::LeftHigher);
    row("right-higher", "1.2.99", "1.3.0", Relation::RightHigher);
    row("first-differing-segment", "2.0.0", "1.999.999", Relation::LeftHigher);
    row("lowercase-v", "v1.2", "1.2", Relation::Equal);
    row("uppercase-v", "V1.2", "v1.1", Relation::LeftHigher);
    row("surrounding-whitespace", " \tV1.2\r\n", "1.2", Relation::Equal);
    row("prefix-disabled", "v1.2", "1.2", Relation::Incomparable, true, false);
    row("uppercase-prefix-disabled", "1.2", "V1.2", Relation::Incomparable, true, false);
    row("plain-with-prefix-disabled", "1.2", "1.2", Relation::Equal, true, false);
    row("pad-left", "1.2", "1.2.0.0", Relation::Equal);
    row("pad-right", "1.2.0.0", "1.2", Relation::Equal);
    row("nonzero-extra-segment", "1.2", "1.2.0.1", Relation::RightHigher);
    row("strict-left-shorter", "1.2", "1.2.0", Relation::Incomparable, false);
    row("strict-right-shorter", "1.2.0", "1.2", Relation::Incomparable, false);
    row("strict-even-if-first-differs", "2", "1.0", Relation::Incomparable, false);
    row("strict-same-count", "1.2.0", "1.1.9", Relation::LeftHigher, false);
    row("leading-zeroes", "0001.002.000", "1.2.0", Relation::Equal);
    row("all-zeroes", "0000.0000", "0", Relation::Equal);
    row("zeroes-do-not-affect-order", "00010.000", "9.99999", Relation::LeftHigher);
    row("beyond-64-bit", "18446744073709551616", "18446744073709551615", Relation::LeftHigher);
    row("large-segment-width", QString(1000, QLatin1Char('9')), "1" + QString(1000, QLatin1Char('0')), Relation::RightHigher);
    row("large-segment-lexical", QString(1000, QLatin1Char('8')) + "9", QString(1000, QLatin1Char('8')) + "8", Relation::LeftHigher);
    row("suffix-beta", "1.2-beta", "1.2", Relation::Incomparable);
    row("same-suffix-still-incomparable", "1.2-beta", "1.2-beta", Relation::Incomparable);
    row("build-metadata", "1.2+build.3", "1.2", Relation::Incomparable);
    row("arbitrary-prefix", "release1.2", "1.2", Relation::Incomparable);
    row("repeated-prefix", "vv1.2", "1.2", Relation::Incomparable);
    row("prefix-only", "v", "0", Relation::Incomparable);
    row("internal-space", "1. 2", "1.2", Relation::Incomparable);
    row("prefix-space", "v 1.2", "1.2", Relation::Incomparable);
    row("empty", "", "1", Relation::Incomparable);
    row("both-empty", "", "", Relation::Incomparable);
    row("whitespace-only", " \t\r\n", "1", Relation::Incomparable);
    row("leading-dot", ".1", "1", Relation::Incomparable);
    row("trailing-dot", "1.", "1", Relation::Incomparable);
    row("empty-segment", "1..2", "1.2", Relation::Incomparable);
    row("negative", "-1.2", "1.2", Relation::Incomparable);
    row("plus-sign", "+1.2", "1.2", Relation::Incomparable);
    row("unicode-digits", QString::fromUtf8("１.２"), "1.2", Relation::Incomparable);
    row("comma-separator", "1,2,3", "1.2.3", Relation::Incomparable);
    row("embedded-newline", "1.\n2", "1.2", Relation::Incomparable);
    row("embedded-nul", QStringLiteral("1.2") + QChar(0) + QStringLiteral(".3"), "1.2.3", Relation::Incomparable);
}

void VersionCompareTests::numericVersions()
{
    QFETCH(QString, left);
    QFETCH(QString, right);
    QFETCH(bool, padMissing);
    QFETCH(bool, allowV);
    QFETCH(int, expected);
    CompareOptions options;
    options.padMissingVersionSegments = padMissing;
    options.allowLeadingV = allowV;
    QCOMPARE(int(compareNumbers(left, right, options)), expected);
    const Relation inverse = expected == int(Relation::LeftHigher) ? Relation::RightHigher :
                             expected == int(Relation::RightHigher) ? Relation::LeftHigher : Relation(expected);
    QCOMPARE(int(compareNumbers(right, left, options)), int(inverse));
}

void VersionCompareTests::numericInputLimit()
{
    const QString atLimit(4096, QLatin1Char('9'));
    QCOMPARE(compareNumbers(atLimit, atLimit), Relation::Equal);
    QCOMPARE(compareNumbers(atLimit + QLatin1Char('0'), atLimit), Relation::Incomparable);
    QCOMPARE(compareNumbers(atLimit, atLimit + QLatin1Char('0')), Relation::Incomparable);
    const QString manySegments = QStringLiteral("0.").repeated(2047) + QStringLiteral("0");
    QCOMPARE(compareNumbers(manySegments, QStringLiteral("0")), Relation::Equal);
}

void VersionCompareTests::fieldDifferences()
{
    FileInfo left, right;
    left.metadata = {{"Same", "value"}, {"Changed", "before"}, {"Left", "only"}, {"Empty left", ""}, {"Empty both", ""}};
    right.metadata = {{"Same", "value"}, {"Changed", "after"}, {"Right", "only"}, {"Empty right", ""}, {"Empty both", ""}};
    const auto rows = compare(left, right);
    const auto check = [&rows](const QString &key, Difference expected, const QString &a, const QString &b) {
        const Row *row = findRow(rows, QStringLiteral("File metadata"), key);
        QVERIFY2(row, qPrintable(key));
        QCOMPARE(row->difference, expected);
        QCOMPARE(row->left, a);
        QCOMPARE(row->right, b);
        QVERIFY(!row->versionNumber);
        QCOMPARE(row->relation, Relation::Incomparable);
    };
    check("Same", Difference::Equal, "value", "value");
    check("Changed", Difference::Changed, "before", "after");
    check("Left", Difference::LeftOnly, "only", "");
    check("Right", Difference::RightOnly, "", "only");
    check("Empty left", Difference::LeftOnly, "", "");
    check("Empty right", Difference::RightOnly, "", "");
    check("Empty both", Difference::Equal, "", "");
}

void VersionCompareTests::versionTextAndNumericRelationAreIndependent()
{
    FileInfo left, right;
    auto a = version(), b = version();
    a.fixed = {{"FileVersion", "01.2"}, {"ProductVersion", "1.10.0"}};
    b.fixed = {{"FileVersion", "1.2.0"}, {"ProductVersion", "1.9.0"}};
    a.strings = {{"040904B0/FileVersion", "1.2-beta"}, {"040904B0/ProductVersion", "V2"}, {"040904B0/CompanyName", "1.10"}};
    b.strings = {{"040904B0/FileVersion", "1.2-beta"}, {"040904B0/ProductVersion", "v3"}, {"040904B0/CompanyName", "1.9"}};
    left.versions.append(a);
    right.versions.append(b);
    const auto rows = compare(left, right);
    const Row *file = findRow(rows, EnglishVersionGroup, "Fixed/FileVersion");
    const Row *product = findRow(rows, EnglishVersionGroup, "Fixed/ProductVersion");
    const Row *beta = findRow(rows, EnglishVersionGroup, "Strings/040904B0/FileVersion");
    const Row *stringProduct = findRow(rows, EnglishVersionGroup, "Strings/040904B0/ProductVersion");
    const Row *company = findRow(rows, EnglishVersionGroup, "Strings/040904B0/CompanyName");
    QVERIFY(file && product && beta && stringProduct && company);
    QCOMPARE(file->difference, Difference::Changed);
    QCOMPARE(file->relation, Relation::Equal);
    QCOMPARE(product->relation, Relation::LeftHigher);
    QCOMPARE(beta->difference, Difference::Equal);
    QCOMPARE(beta->relation, Relation::Incomparable);
    QCOMPARE(stringProduct->relation, Relation::RightHigher);
    QVERIFY(file->versionNumber && product->versionNumber && beta->versionNumber && stringProduct->versionNumber);
    QVERIFY(!company->versionNumber);
    QCOMPARE(company->difference, Difference::Changed);
    QCOMPARE(company->relation, Relation::Incomparable);
}

void VersionCompareTests::ignoredVersionFieldsLeaveOtherChangesVisible()
{
    FileInfo left, right;
    auto a = version(), b = version();
    a.fixed = {{"FileVersion", "1"}, {"ProductVersion", "2"}};
    b.fixed = {{"FileVersion", "3"}, {"ProductVersion", "2"}};
    a.strings = {{"040904B0/FileVersion", "1"}, {"040904B0/CompanyName", "Before"}};
    b.strings = {{"040904B0/ProductVersion", "2"}, {"040904B0/CompanyName", "After"}};
    left.versions.append(a);
    right.versions.append(b);
    CompareOptions options;
    options.ignoreVersionNumbers = true;
    const auto rows = compare(left, right, options);
    const Row *file = findRow(rows, EnglishVersionGroup, "Fixed/FileVersion");
    const Row *same = findRow(rows, EnglishVersionGroup, "Fixed/ProductVersion");
    const Row *leftOnly = findRow(rows, EnglishVersionGroup, "Strings/040904B0/FileVersion");
    const Row *rightOnly = findRow(rows, EnglishVersionGroup, "Strings/040904B0/ProductVersion");
    const Row *company = findRow(rows, EnglishVersionGroup, "Strings/040904B0/CompanyName");
    QVERIFY(file && same && leftOnly && rightOnly && company);
    QCOMPARE(file->difference, Difference::Ignored);
    QCOMPARE(file->relation, Relation::RightHigher);
    QCOMPARE(file->left, QStringLiteral("1"));
    QCOMPARE(file->right, QStringLiteral("3"));
    QCOMPARE(same->difference, Difference::Equal);
    QCOMPARE(leftOnly->difference, Difference::Ignored);
    QCOMPARE(rightOnly->difference, Difference::Ignored);
    QCOMPARE(leftOnly->relation, Relation::Incomparable);
    QCOMPARE(company->difference, Difference::Changed);
}

void VersionCompareTests::comparisonForwardsNumericOptions()
{
    FileInfo left, right;
    auto a = version(), b = version();
    a.fixed = {{"FileVersion", "v1.2"}, {"ProductVersion", "1.2"}};
    b.fixed = {{"FileVersion", "1.2"}, {"ProductVersion", "1.2.0"}};
    left.versions.append(a);
    right.versions.append(b);
    CompareOptions options;
    options.allowLeadingV = false;
    options.padMissingVersionSegments = false;
    const auto rows = compare(left, right, options);
    const Row *file = findRow(rows, EnglishVersionGroup, "Fixed/FileVersion");
    const Row *product = findRow(rows, EnglishVersionGroup, "Fixed/ProductVersion");
    QVERIFY(file && product);
    QCOMPARE(file->relation, Relation::Incomparable);
    QCOMPARE(product->relation, Relation::Incomparable);
    QCOMPARE(file->difference, Difference::Changed);
    QCOMPARE(product->difference, Difference::Changed);
}

void VersionCompareTests::languagesAndCodePagesRemainSeparate()
{
    FileInfo left, right;
    auto english = version(), chinese = version("2052"), german = version("1031");
    english.fixed = {{"FileVersion", "1.2.3.4"}};
    english.strings = {{"040904B0/CompanyName", "English Unicode"}, {"040904E4/CompanyName", "English ANSI"}};
    english.translations = {0x04b00409U, 0x04e40409U};
    chinese.fixed = {{"FileVersion", "1.2.3.4"}};
    chinese.strings = {{"080404B0/CompanyName", QString::fromUtf8("公司")}};
    german.strings = {{"040704B0/CompanyName", "Firma"}};
    left.versions = {english, chinese};
    auto changedEnglish = english;
    changedEnglish.strings["040904E4/CompanyName"] = "Changed ANSI";
    right.versions = {chinese, changedEnglish, german};
    const auto rows = compare(left, right);
    const Row *unicode = findRow(rows, EnglishVersionGroup, "Strings/040904B0/CompanyName");
    const Row *ansi = findRow(rows, EnglishVersionGroup, "Strings/040904E4/CompanyName");
    const Row *zh = findRow(rows, "Version — resource 1 / language 2052", "Strings/080404B0/CompanyName");
    const Row *de = findRow(rows, "Version — resource 1 / language 1031", "Strings/040704B0/CompanyName");
    const Row *translations = findRow(rows, EnglishVersionGroup, "Translations");
    QVERIFY(unicode && ansi && zh && de && translations);
    QCOMPARE(unicode->difference, Difference::Equal);
    QCOMPARE(unicode->left, QStringLiteral("English Unicode"));
    QCOMPARE(ansi->difference, Difference::Changed);
    QCOMPARE(ansi->left, QStringLiteral("English ANSI"));
    QCOMPARE(ansi->right, QStringLiteral("Changed ANSI"));
    QCOMPARE(zh->difference, Difference::Equal);
    QCOMPARE(zh->left, QString::fromUtf8("公司"));
    QCOMPARE(de->difference, Difference::RightOnly);
    QCOMPARE(translations->left, QStringLiteral("0X4B00409, 0X4E40409"));
    QCOMPARE(translations->difference, Difference::Equal);
}

void VersionCompareTests::duplicateResourcesRemainVisible()
{
    FileInfo left, right;
    auto first = version(), second = version();
    first.fixed = {{"FileVersion", "1"}};
    second.fixed = {{"FileVersion", "2"}};
    left.versions = {first, second};
    right.versions = left.versions;
    const auto rows = compare(left, right);
    const Row *a = findRow(rows, EnglishVersionGroup, "Fixed/FileVersion");
    const Row *b = findRow(rows, EnglishVersionGroup + " [2]", "Fixed/FileVersion");
    QVERIFY(a && b);
    QCOMPARE(a->left, QStringLiteral("1"));
    QCOMPARE(b->left, QStringLiteral("2"));
    QCOMPARE(a->difference, Difference::Equal);
    QCOMPARE(b->difference, Difference::Equal);
}

void VersionCompareTests::importsPreserveNamesOrdinalsAndHints()
{
    FileInfo left, right;
    left.imports = {{"KERNEL32.dll", "CreateFileW", false, 0, 7}, {"USER32.dll", "", true, 12, 0}, {"LEFT.dll", "LeftName", false, 0, 1}};
    right.imports = {{"KERNEL32.dll", "CreateFileW", false, 0, 8}, {"USER32.dll", "", true, 12, 0}, {"RIGHT.dll", "RightName", false, 0, 2}};
    const auto rows = compare(left, right);
    const auto imports = groupRows(rows, "Imports");
    QCOMPARE(imports.size(), 4);
    const Row *named = findRow(rows, "Imports", "KERNEL32.dll / CreateFileW");
    const Row *ordinal = findRow(rows, "Imports", "USER32.dll / #12");
    const Row *leftOnly = findRow(rows, "Imports", "LEFT.dll / LeftName");
    const Row *rightOnly = findRow(rows, "Imports", "RIGHT.dll / RightName");
    QVERIFY(named && ordinal && leftOnly && rightOnly);
    QCOMPARE(named->left, QStringLiteral("Name: CreateFileW; hint: 7"));
    QCOMPARE(named->right, QStringLiteral("Name: CreateFileW; hint: 8"));
    QCOMPARE(named->difference, Difference::Changed);
    QCOMPARE(ordinal->left, QStringLiteral("Ordinal 12"));
    QCOMPARE(ordinal->difference, Difference::Equal);
    QCOMPARE(leftOnly->difference, Difference::LeftOnly);
    QCOMPARE(rightOnly->difference, Difference::RightOnly);
    for (const auto &row : imports) {
        QVERIFY(!row.versionNumber);
        QCOMPARE(row.relation, Relation::Incomparable);
        QVERIFY(!row.key.contains("Version"));
        QVERIFY(!row.left.contains("("));
    }
}

void VersionCompareTests::importDependencyWithoutSymbolsIsExplicit()
{
    FileInfo info;
    info.imports = {{"EMPTY.dll", "", false, 0, 0}};
    const auto rows = compare(info, info);
    const Row *dependency = findRow(rows, "Imports", "EMPTY.dll / ");
    QVERIFY(dependency);
    QCOMPARE(dependency->left, QStringLiteral("DLL dependency; no symbol entries"));
    QCOMPARE(dependency->difference, Difference::Equal);
    QVERIFY(!dependency->versionNumber);
    QCOMPARE(groupRows(rows, "Imports").size(), 1);
}

void VersionCompareTests::exportsPreserveNamesOrdinalsAddressesAndForwarders()
{
    FileInfo left, right;
    left.exports = {{"Public", "", 5, 0x1234}, {"", "", 6, 0x1250}, {"Forwarded", "OTHER.Target", 7, 0x2000}, {"OldName", "", 9, 0x1300}};
    right.exports = {{"Public", "", 5, 0x1235}, {"", "", 6, 0x1250}, {"Forwarded", "OTHER.#17", 7, 0x2000}, {"NewName", "", 9, 0x1300}};
    const auto rows = compare(left, right);
    QCOMPARE(groupRows(rows, "Exports").size(), 5);
    const Row *named = findRow(rows, "Exports", "#5 / Public");
    const Row *unnamed = findRow(rows, "Exports", "#6 / Unnamed");
    const Row *forwarded = findRow(rows, "Exports", "#7 / Forwarded");
    const Row *old = findRow(rows, "Exports", "#9 / OldName");
    const Row *added = findRow(rows, "Exports", "#9 / NewName");
    QVERIFY(named && unnamed && forwarded && old && added);
    QCOMPARE(named->left, QStringLiteral("RVA 0X1234"));
    QCOMPARE(named->right, QStringLiteral("RVA 0X1235"));
    QCOMPARE(named->difference, Difference::Changed);
    QCOMPARE(unnamed->difference, Difference::Equal);
    QCOMPARE(forwarded->left, QStringLiteral("RVA 0X2000; forwards to OTHER.Target"));
    QCOMPARE(forwarded->right, QStringLiteral("RVA 0X2000; forwards to OTHER.#17"));
    QCOMPARE(forwarded->difference, Difference::Changed);
    QCOMPARE(old->difference, Difference::LeftOnly);
    QCOMPARE(added->difference, Difference::RightOnly);
    QVERIFY(!forwarded->versionNumber);
}

void VersionCompareTests::duplicateSymbolsRemainVisible()
{
    FileInfo info;
    info.imports = {{"DLL", "Same", false, 0, 1}, {"DLL", "Same", false, 0, 2}};
    info.exports = {{"Same", "", 8, 0x1000}, {"Same", "", 8, 0x2000}};
    const auto rows = compare(info, info);
    QCOMPARE(groupRows(rows, "Imports").size(), 2);
    QCOMPARE(groupRows(rows, "Exports").size(), 2);
    const Row *firstImport = findRow(rows, "Imports", "DLL / Same");
    const Row *secondImport = findRow(rows, "Imports", "DLL / Same [2]");
    const Row *firstExport = findRow(rows, "Exports", "#8 / Same");
    const Row *secondExport = findRow(rows, "Exports", "#8 / Same [2]");
    QVERIFY(firstImport && secondImport && firstExport && secondExport);
    QCOMPARE(firstImport->left, QStringLiteral("Name: Same; hint: 1"));
    QCOMPARE(secondImport->left, QStringLiteral("Name: Same; hint: 2"));
    QCOMPARE(firstExport->left, QStringLiteral("RVA 0X1000"));
    QCOMPARE(secondExport->left, QStringLiteral("RVA 0X2000"));
}

void VersionCompareTests::importNamesCannotOverwriteDuplicateRows()
{
    // A literal name ending in the display duplicate suffix must not hide an entry.
    FileInfo info;
    info.imports = {{"DLL", "Same", false, 0, 1}, {"DLL", "Same", false, 0, 2}, {"DLL", "Same [2]", false, 0, 3}};
    const auto rows = compare(info, info);
    const auto imports = groupRows(rows, "Imports");
    QCOMPARE(imports.size(), 3);
    QStringList importValues;
    for (const Row &row : imports) importValues << row.left;
    QVERIFY(importValues.contains("Name: Same; hint: 1"));
    QVERIFY(importValues.contains("Name: Same; hint: 2"));
    QVERIFY(importValues.contains("Name: Same [2]; hint: 3"));
}

void VersionCompareTests::exportNamesCannotOverwriteDuplicateRows()
{
    FileInfo info;
    info.exports = {{"Same", "", 8, 0x1000}, {"Same", "", 8, 0x2000}, {"Same [2]", "", 8, 0x3000}};
    const auto exports = groupRows(compare(info, info), "Exports");
    QCOMPARE(exports.size(), 3);
    QStringList exportValues;
    for (const Row &row : exports) exportValues << row.left;
    QVERIFY(exportValues.contains("RVA 0X1000"));
    QVERIFY(exportValues.contains("RVA 0X2000"));
    QVERIFY(exportValues.contains("RVA 0X3000"));
}

void VersionCompareTests::structuralFieldsRemainSeparate()
{
    FileInfo left, right;
    left.status = right.status = Status::Pe;
    left.pe32Plus = false;
    right.pe32Plus = true;
    left.headers = {{"Machine", "x86"}, {"EntryPoint", "0x1000"}};
    right.headers = {{"Machine", "x64"}, {"EntryPoint", "0x1000"}};
    left.metadata = {{"Machine", "host"}};
    right.metadata = left.metadata;
    left.sections = {{".text", 0x1000, 100, 0x200, 512, 0x60000020}, {".text", 0x2000, 200, 0x400, 512, 0x60000020}};
    right.sections = left.sections;
    right.sections[1].virtualSize = 201;
    const auto rows = compare(left, right);
    const Row *format = findRow(rows, "File metadata", "Format");
    const Row *headerMachine = findRow(rows, "PE headers", "Machine");
    const Row *metadataMachine = findRow(rows, "File metadata", "Machine");
    const Row *firstSection = findRow(rows, "Sections", "[0] .text / VirtualSize");
    const Row *secondSection = findRow(rows, "Sections", "[1] .text / VirtualSize");
    QVERIFY(format && headerMachine && metadataMachine && firstSection && secondSection);
    QCOMPARE(format->left, QStringLiteral("PE32"));
    QCOMPARE(format->right, QStringLiteral("PE32+"));
    QCOMPARE(format->difference, Difference::Changed);
    QCOMPARE(headerMachine->difference, Difference::Changed);
    QCOMPARE(metadataMachine->difference, Difference::Equal);
    QCOMPARE(groupRows(rows, "Sections").size(), 10);
    QCOMPARE(firstSection->difference, Difference::Equal);
    QCOMPARE(secondSection->difference, Difference::Changed);
    QCOMPARE(secondSection->left, QStringLiteral("200"));
    QCOMPARE(secondSection->right, QStringLiteral("201"));
}

void VersionCompareTests::nonPeMetadataReportsNoVersion()
{
    FileInfo left, right;
    left.status = right.status = Status::NonPe;
    left.metadata = {{"Size", "10"}, {"Modified", "2026-09-20T12:00:00Z"}, {"Platform", "macOS"}};
    right.metadata = {{"Size", "20"}, {"Modified", "2026-09-20T12:00:01Z"}, {"Platform", "macOS"}};
    left.message = right.message = "Not a PE file; no version resource";
    const auto rows = compare(left, right);
    const Row *availability = findRow(rows, "Version availability", "Version resource");
    const Row *details = findRow(rows, "Version availability", "Details");
    const Row *size = findRow(rows, "File metadata", "Size");
    const Row *format = findRow(rows, "File metadata", "Format");
    QVERIFY(availability && details && size && format);
    QCOMPARE(availability->left, QStringLiteral("No version resource"));
    QCOMPARE(details->left, left.message);
    QCOMPARE(format->left, QStringLiteral("Non-PE"));
    QCOMPARE(size->difference, Difference::Changed);
    for (const Row &row : rows) QVERIFY(!row.versionNumber);
}

void VersionCompareTests::csvPreservesSpecialCharacters()
{
    Row row;
    row.group = QString::fromUtf8("元数据,\"组\"");
    row.key = "field\nsecond line";
    row.left = "left\r\n\"quoted\",value";
    row.right = "right\rvalue\t";
    row.difference = Difference::Changed;
    const QString expected = QString::fromUtf8(
        "\"Group\",\"Field\",\"Left\",\"Right\",\"Difference\",\"Numeric relation\"\r\n"
        "\"元数据,\"\"组\"\"\",\"field\nsecond line\",\"left\r\n\"\"quoted\"\",value\",\"right\rvalue\t\",\"changed\",\"\"\r\n");
    QCOMPARE(toCsv({row}), expected);
    QCOMPARE(toCsv({}), QStringLiteral("\"Group\",\"Field\",\"Left\",\"Right\",\"Difference\",\"Numeric relation\"\r\n"));
}

void VersionCompareTests::csvExportsAllDifferencesAndNumericRelations()
{
    QVector<Row> rows;
    const Difference differences[] = {Difference::Equal, Difference::Changed, Difference::LeftOnly, Difference::RightOnly, Difference::Ignored};
    const Relation relations[] = {Relation::Equal, Relation::LeftHigher, Relation::RightHigher, Relation::Incomparable, Relation::Incomparable};
    for (int i = 0; i < 5; ++i) {
        Row row;
        row.group = "Version";
        row.key = QString::number(i);
        row.left = "1";
        row.right = "2";
        row.difference = differences[i];
        row.relation = relations[i];
        row.versionNumber = i != 4;
        rows.append(row);
    }
    const QString expected = QStringLiteral(
        "\"Group\",\"Field\",\"Left\",\"Right\",\"Difference\",\"Numeric relation\"\r\n"
        "\"Version\",\"0\",\"1\",\"2\",\"equal\",\"Same numeric version\"\r\n"
        "\"Version\",\"1\",\"1\",\"2\",\"changed\",\"Left higher\"\r\n"
        "\"Version\",\"2\",\"1\",\"2\",\"left-only\",\"Right higher\"\r\n"
        "\"Version\",\"3\",\"1\",\"2\",\"right-only\",\"Incomparable\"\r\n"
        "\"Version\",\"4\",\"1\",\"2\",\"ignored\",\"\"\r\n");
    QCOMPARE(toCsv(rows), expected);
}

QTEST_GUILESS_MAIN(VersionCompareTests)
#include "tst_versioncompare.moc"
