#include <QtTest>

#include "tablecompare.h"

#include <limits>

using namespace LqCompare::Table;

namespace {

Document table(std::initializer_list<const char *> headers,
               std::initializer_list<std::initializer_list<const char *>> rows,
               bool hasHeader = true)
{
    Document result;
    result.hasHeader = hasHeader;
    for (const char *header : headers)
        result.headers.append(QString::fromUtf8(header));
    for (const auto &row : rows) {
        QStringList values;
        for (const char *value : row)
            values.append(QString::fromUtf8(value));
        result.rows.append(values);
    }
    return result;
}

CompareOptions explicitRules(QVector<ColumnRule> columns, RowAlignment alignment = RowAlignment::Position)
{
    CompareOptions result;
    result.columnMode = ColumnMappingMode::Explicit;
    result.alignment = alignment;
    result.columns = columns;
    return result;
}

const Row *leftRow(const Result &result, int index)
{
    for (const auto &row : result.rows) {
        if (row.left == index)
            return &row;
    }
    return nullptr;
}

bool warningContains(const Result &result, const QString &needle)
{
    for (const auto &warning : result.warnings) {
        if (warning.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void verifyCoverage(const Result &result, int leftCount, int rightCount)
{
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVector<int> leftSeen(leftCount, 0);
    QVector<int> rightSeen(rightCount, 0);
    QVector<int> differences;
    for (int i = 0; i < result.rows.size(); ++i) {
        const auto &row = result.rows.at(i);
        QVERIFY(row.left >= 0 || row.right >= 0);
        QVERIFY(row.left < leftCount && row.right < rightCount);
        if (row.left >= 0)
            ++leftSeen[row.left];
        if (row.right >= 0)
            ++rightSeen[row.right];
        QCOMPARE(row.cells.size(), result.columns.size());
        if (row.status != RowStatus::Equal)
            differences.append(i);
    }
    for (int count : leftSeen)
        QCOMPARE(count, 1);
    for (int count : rightSeen)
        QCOMPARE(count, 1);
    QCOMPARE(result.differences, differences);
    QCOMPARE(result.statistics.equalRows + result.statistics.differentRows
             + result.statistics.leftOnlyRows + result.statistics.rightOnlyRows, result.rows.size());
}

} // namespace

class TableCompareTests : public QObject
{
    Q_OBJECT
private slots:
    void autoNameMapping();
    void duplicateHeadersRequireChoice();
    void noHeaderUsesPosition();
    void explicitMappingValidation_data();
    void explicitMappingValidation();
    void unmappedAndRoles();
    void noComparisonColumns();
    void missingVersusEmpty();
    void contentInsertionAndReorder();
    void exactMatchBeforeSimilarity();
    void similarityThreshold();
    void duplicateKeysRetainRows();
    void compositeKeysAndMissing();
    void numeric_data();
    void numeric();
    void numericContentAlignment();
    void invalidNumericValuesStayLiteral();
    void statistics();
    void boundedContentWork();
    void longNumericContentWork();
    void resultCellBound();
};

void TableCompareTests::autoNameMapping()
{
    const auto left = table({"id", "值", "left extra"}, {{"A", "红", "secret"}});
    const auto right = table({"值", "id", "right extra"}, {{"红", "A", "visible"}});
    const auto result = compare(left, right);
    verifyCoverage(result, 1, 1);
    QCOMPARE(result.columns.size(), 4);
    QCOMPARE(result.columns.at(0).left, 0);
    QCOMPARE(result.columns.at(0).right, 1);
    QCOMPARE(result.columns.at(1).left, 1);
    QCOMPARE(result.columns.at(1).right, 0);
    QCOMPARE(result.columns.at(2).role, ColumnRole::Display);
    QCOMPARE(result.columns.at(3).role, ColumnRole::Display);
    QCOMPARE(result.rows.at(0).cells, QVector<CellStatus>({CellStatus::Equal, CellStatus::Equal,
             CellStatus::NotCompared, CellStatus::NotCompared}));
    QCOMPARE(result.statistics.equalRows, 1);
    QVERIFY(warningContains(result, QStringLiteral("not compared")));
}

void TableCompareTests::duplicateHeadersRequireChoice()
{
    const auto document = table({"id", "id"}, {{"a", "b"}});
    QVERIFY(!compare(document, document).ok());
    CompareOptions options;
    options.columnMode = ColumnMappingMode::Position;
    const auto result = compare(document, document, options);
    verifyCoverage(result, 1, 1);
    QCOMPARE(result.statistics.equalRows, 1);
    auto right = table({"id", "value"}, {{"a", "b"}});
    QVERIFY(!compare(document, right).ok());
    QVERIFY(!compare(right, document).ok());
}

void TableCompareTests::noHeaderUsesPosition()
{
    const auto left = table({}, {{"1", "two"}}, false);
    const auto right = table({"B", "A"}, {{"1", "two"}});
    const auto result = compare(left, right);
    verifyCoverage(result, 1, 1);
    QCOMPARE(result.columns.size(), 2);
    QCOMPARE(result.columns.at(0).left, result.columns.at(0).right);
    QCOMPARE(result.statistics.equalRows, 1);
}

void TableCompareTests::explicitMappingValidation_data()
{
    QTest::addColumn<int>("scenario");
    for (int i = 0; i < 14; ++i)
        QTest::newRow(qPrintable(QString::number(i))) << i;
}

void TableCompareTests::explicitMappingValidation()
{
    QFETCH(int, scenario);
    const auto document = table({"id", "value"}, {{"a", "b"}});
    auto options = explicitRules({{0, 0}});
    switch (scenario) {
    case 0: options.columns[0].left = -2; break;
    case 1: options.columns[0].right = 2; break;
    case 2: options.columns[0] = {-1, -1}; break;
    case 3: options.columns.append({0, 1}); break;
    case 4: options.columns.append({1, 0}); break;
    case 5: options.columns[0].absoluteTolerance = -1; break;
    case 6: options.columns[0].relativeTolerance = std::numeric_limits<double>::infinity(); break;
    case 7: options.columns[0].absoluteTolerance = std::numeric_limits<double>::quiet_NaN(); break;
    case 8: options.columns[0] = {0, -1, ColumnRole::Key}; break;
    case 9: options.alignment = RowAlignment::Key; break;
    case 10: options.minimumSimilarity = -0.1; break;
    case 11: options.minimumSimilarity = 1.1; break;
    case 12: options.minimumSimilarity = std::numeric_limits<double>::quiet_NaN(); break;
    case 13: options.minimumSimilarity = std::numeric_limits<double>::infinity(); break;
    }
    const auto result = compare(document, document, options);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.rows.isEmpty());
}

void TableCompareTests::unmappedAndRoles()
{
    const auto left = table({"id", "noise", "display", "unmapped"}, {{"a", "one", "x", "q"}});
    const auto right = table({"id", "noise", "display", "unmapped"}, {{"a", "two", "y", "r"}});
    const auto options = explicitRules({{0, 0, ColumnRole::Key}, {1, 1, ColumnRole::Ignore},
                                        {2, 2, ColumnRole::Display}}, RowAlignment::Key);
    const auto result = compare(left, right, options);
    verifyCoverage(result, 1, 1);
    QCOMPARE(result.columns.size(), 5);
    QCOMPARE(result.rows.at(0).cells, QVector<CellStatus>({CellStatus::Equal, CellStatus::Ignored,
             CellStatus::NotCompared, CellStatus::NotCompared, CellStatus::NotCompared}));
    QCOMPARE(result.statistics.equalRows, 1);
    QCOMPARE(result.statistics.ignoredCells, 1);
    QCOMPARE(result.statistics.differentCells, 0);
}

void TableCompareTests::noComparisonColumns()
{
    const auto left = table({"left"}, {{"a"}});
    const auto right = table({"right"}, {{"a"}});
    QVERIFY(!compare(left, right).ok());
    QVERIFY(!compare(left, left, explicitRules({{0, 0, ColumnRole::Ignore}})).ok());
    QVERIFY(!compare(left, left, explicitRules({{0, 0, ColumnRole::Display}})).ok());
    const auto oneSide = compare(left, Document());
    verifyCoverage(oneSide, 1, 0);
    QCOMPARE(oneSide.statistics.leftOnlyRows, 1);
    QVERIFY(compare(Document(), Document()).ok());
}

void TableCompareTests::missingVersusEmpty()
{
    const auto left = table({"id", "value"}, {{"a"}, {"b", ""}, {"c"}});
    const auto right = table({"id", "value"}, {{"a", ""}, {"b"}, {"c"}});
    CompareOptions options;
    options.alignment = RowAlignment::Position;
    const auto result = compare(left, right, options);
    verifyCoverage(result, 3, 3);
    QCOMPARE(result.rows.at(0).cells.at(1), CellStatus::RightOnly);
    QCOMPARE(result.rows.at(1).cells.at(1), CellStatus::LeftOnly);
    QCOMPARE(result.rows.at(2).cells.at(1), CellStatus::Equal);
    QCOMPARE(result.statistics.differentRows, 2);
    QCOMPARE(result.statistics.equalRows, 1);
    QCOMPARE(result.statistics.differentCells, 2);
}

void TableCompareTests::contentInsertionAndReorder()
{
    const auto left = table({"value"}, {{"A"}, {"B"}, {"C"}});
    const auto inserted = table({"value"}, {{"X"}, {"A"}, {"B"}, {"C"}});
    const auto result = compare(left, inserted);
    verifyCoverage(result, 3, 4);
    QCOMPARE(result.statistics.equalRows, 3);
    QCOMPARE(result.statistics.rightOnlyRows, 1);
    QCOMPARE(result.statistics.differentRows, 0);
    QCOMPARE(result.rows.at(0).right, 0);
    QCOMPARE(result.rows.at(0).status, RowStatus::RightOnly);
    const auto reordered = compare(left, table({"value"}, {{"C"}, {"A"}, {"B"}}));
    verifyCoverage(reordered, 3, 3);
    QCOMPARE(reordered.statistics.equalRows, 3);
    QCOMPARE(leftRow(reordered, 0)->right, 1);
    QCOMPARE(leftRow(reordered, 1)->right, 2);
    QCOMPARE(leftRow(reordered, 2)->right, 0);
    const auto deleted = compare(inserted, left);
    verifyCoverage(deleted, 4, 3);
    QCOMPARE(deleted.statistics.leftOnlyRows, 1);
    QCOMPARE(deleted.statistics.equalRows, 3);
}

void TableCompareTests::exactMatchBeforeSimilarity()
{
    const auto left = table({"id", "value"}, {{"A", "1"}});
    const auto right = table({"id", "value"}, {{"A", "2"}, {"A", "1"}});
    const auto result = compare(left, right);
    verifyCoverage(result, 1, 2);
    QCOMPARE(leftRow(result, 0)->right, 1);
    QCOMPARE(result.statistics.equalRows, 1);
    QCOMPARE(result.statistics.rightOnlyRows, 1);
}

void TableCompareTests::similarityThreshold()
{
    const auto left = table({"id", "value"}, {{"A", "1"}});
    const auto right = table({"id", "value"}, {{"A", "2"}});
    CompareOptions options;
    options.minimumSimilarity = 0.5;
    auto result = compare(left, right, options);
    verifyCoverage(result, 1, 1);
    QCOMPARE(result.statistics.differentRows, 1);
    options.minimumSimilarity = 0.51;
    result = compare(left, right, options);
    QCOMPARE(result.statistics.leftOnlyRows, 1);
    QCOMPARE(result.statistics.rightOnlyRows, 1);
    auto modified = compare(table({"text"}, {{"original text"}}),
                            table({"text"}, {{"original texts"}}));
    QCOMPARE(modified.statistics.differentRows, 1);
    options.minimumSimilarity = 1;
    modified = compare(table({"text"}, {{"original text"}}),
                       table({"text"}, {{"original texts"}}), options);
    QCOMPARE(modified.rows.size(), 2);
}

void TableCompareTests::duplicateKeysRetainRows()
{
    const auto left = table({"id", "value"}, {{"K", "a"}, {"K", "b"}, {"K", "c"}, {"L", "old"}});
    const auto right = table({"id", "value"}, {{"K", "b"}, {"K", "a"}, {"L", "new"}, {"R", "r"}, {"R", "s"}});
    const auto options = explicitRules({{0, 0, ColumnRole::Key}, {1, 1}}, RowAlignment::Key);
    const auto result = compare(left, right, options);
    verifyCoverage(result, 4, 5);
    QCOMPARE(leftRow(result, 0)->right, 1);
    QCOMPARE(leftRow(result, 1)->right, 0);
    QCOMPARE(leftRow(result, 2)->right, -1);
    for (int i = 0; i < 3; ++i)
        QVERIFY(leftRow(result, i)->duplicateKey);
    QVERIFY(!leftRow(result, 3)->duplicateKey);
    for (const auto &row : result.rows) {
        if (row.left == -1)
            QVERIFY(row.duplicateKey);
    }
    QCOMPARE(result.statistics.equalRows, 2);
    QCOMPARE(result.statistics.differentRows, 1);
    QCOMPARE(result.statistics.leftOnlyRows, 1);
    QCOMPARE(result.statistics.rightOnlyRows, 2);
    QVERIFY(warningContains(result, QStringLiteral("duplicate key")));
    const auto repeat = compare(left, right, options);
    for (int i = 0; i < result.rows.size(); ++i) {
        QCOMPARE(result.rows.at(i).left, repeat.rows.at(i).left);
        QCOMPARE(result.rows.at(i).right, repeat.rows.at(i).right);
    }
}

void TableCompareTests::compositeKeysAndMissing()
{
    const auto left = table({"a", "b"}, {{"ab", "c"}, {"a", "bc"}, {"a:b", "\nc"}, {"missing"}, {""}});
    const auto right = table({"a", "b"}, {{"a", "bc"}, {"ab", "c"}, {"a:b", "\nc"}, {"missing", ""}, {"", ""}});
    const auto options = explicitRules({{0, 0, ColumnRole::Key}, {1, 1, ColumnRole::Key}}, RowAlignment::Key);
    const auto result = compare(left, right, options);
    verifyCoverage(result, 5, 5);
    QCOMPARE(leftRow(result, 0)->right, 1);
    QCOMPARE(leftRow(result, 1)->right, 0);
    QCOMPARE(result.statistics.equalRows, 3);
    QCOMPARE(result.statistics.leftOnlyRows, 2);
    QCOMPARE(result.statistics.rightOnlyRows, 2);
    ColumnRule numericKey{0, 0, ColumnRole::Key};
    numericKey.numeric = true;
    const auto rawKey = compare(table({"id"}, {{"001"}}), table({"id"}, {{"1"}}),
                                explicitRules({numericKey}, RowAlignment::Key));
    QCOMPARE(rawKey.rows.size(), 2);
}

void TableCompareTests::numeric_data()
{
    QTest::addColumn<QString>("left");
    QTest::addColumn<QString>("right");
    QTest::addColumn<double>("absolute");
    QTest::addColumn<double>("relative");
    QTest::addColumn<bool>("formatting");
    QTest::addColumn<bool>("equal");
    const auto row = [](const char *name, const char *a, const char *b, double absolute,
                        double relative, bool formatting, bool equal) {
        QTest::newRow(name) << QString::fromUtf8(a) << QString::fromUtf8(b)
                           << absolute << relative << formatting << equal;
    };
    row("format-ignored", "1.0", "1.00", 0, 0, false, true);
    row("format-compared", "1.0", "1.00", 0, 0, true, false);
    row("scientific", "+.10e1", "01.000", 0, 0, false, true);
    row("negative-zero", "-0", "+0.000", 0, 0, false, true);
    row("whitespace", " 1 ", "1", 0, 0, false, true);
    row("absolute-boundary", "1.1", "1", 0.1, 0, false, true);
    row("absolute-outside", "1.10001", "1", 0.1, 0, false, false);
    row("strict-boundary-outside", "0", "1.000000000000001", 1, 0, false, false);
    row("decimal-tolerance-boundary", "0", "0.3", 0.3, 0, false, true);
    row("decimal-tolerance-outside", "0", "0.30000000000000001", 0.3, 0, false, false);
    row("small-tolerance-boundary", "0", "1e-308", 1e-308, 0, false, true);
    row("absolute-zero", "0", "0.001", 0.001, 0, false, true);
    row("negative", "-1", "-1.25", 0.25, 0, false, true);
    row("opposite-sign", "-0.25", "0.25", 0.49, 0, false, false);
    row("opposite-sign-boundary", "-0.25", "0.25", 0.5, 0, false, true);
    row("relative-boundary", "99", "100", 0, 0.01, false, true);
    row("relative-outside", "98.99", "100", 0, 0.01, false, false);
    row("relative-negative", "-99", "-100", 0, 0.01, false, true);
    row("maximum-not-sum", "100", "102", 1, 0.01, false, false);
    row("huge-overflow", "1e308", "-1e308", 0, 1.5, false, false);
    row("huge-relative-boundary", "1e308", "-1e308", 0, 2, false, true);
    row("huge-absolute", "1e308", "-1e308", 1e308, 0, false, false);
    row("adjacent-huge-integers", "9007199254740992", "9007199254740993", 0, 0, false, false);
    row("huge-integer-absolute", "9007199254740992", "9007199254740993", 1, 0, false, true);
    row("huge-integer-tiny-tolerance", "9007199254740992", "9007199254740993", 1e-20, 0, false, false);
    row("huge-precision-fraction", "1.000000000000000000000000000001", "1", 0, 0, false, false);
    row("fraction-below-tolerance", "1.000000000000000000000000000001", "1", 1e-29, 0, false, true);
    row("fraction-above-tolerance", "1.000000000000000000000000000001", "1", 1e-31, 0, false, false);
    row("nan", "NaN", "0", 100, 100, false, false);
    row("inf", "inf", "0", 100, 100, false, false);
    row("overflow-source", "1e309", "0", 100, 100, false, false);
    row("comma", "1,000", "1000", 0, 0, false, false);
    row("invalid-same", "invalid", "invalid", 0, 0, false, true);
    row("empty-not-zero", "", "0", 0, 0, false, false);
}

void TableCompareTests::numeric()
{
    QFETCH(QString, left);
    QFETCH(QString, right);
    QFETCH(double, absolute);
    QFETCH(double, relative);
    QFETCH(bool, formatting);
    QFETCH(bool, equal);
    Document a;
    Document b;
    a.headers = b.headers = QStringList{QStringLiteral("value")};
    a.rows.append({left});
    b.rows.append({right});
    ColumnRule numeric{0, 0};
    numeric.numeric = true;
    numeric.absoluteTolerance = absolute;
    numeric.relativeTolerance = relative;
    numeric.compareFormatting = formatting;
    const auto result = compare(a, b, explicitRules({numeric}));
    verifyCoverage(result, 1, 1);
    QCOMPARE(result.rows.at(0).cells.at(0), equal ? CellStatus::Equal : CellStatus::Different);
    QCOMPARE(result.statistics.differentCells, equal ? 0 : 1);
    QCOMPARE(result.statistics.equalRows, equal ? 1 : 0);
}

void TableCompareTests::numericContentAlignment()
{
    ColumnRule numeric{0, 0};
    numeric.numeric = true;
    auto options = explicitRules({numeric}, RowAlignment::Content);
    const auto result = compare(table({"x"}, {{"1.0"}, {"9007199254740992"}}),
                                table({"x"}, {{"9007199254740993"}, {"1.00"}}), options);
    verifyCoverage(result, 2, 2);
    QCOMPARE(leftRow(result, 0)->right, 1);
    QCOMPARE(leftRow(result, 1)->right, -1);
    QCOMPARE(result.statistics.equalRows, 1);
    numeric.absoluteTolerance = 0.1;
    options.columns = {numeric};
    const auto tolerant = compare(table({"x"}, {{"1"}}), table({"x"}, {{"1.1"}}), options);
    QCOMPARE(tolerant.statistics.equalRows, 1);
}

void TableCompareTests::invalidNumericValuesStayLiteral()
{
    ColumnRule numeric{0, 0};
    numeric.numeric = true;
    const auto result = compare(table({"x"}, {{"bad"}, {"NaN"}, {""}}),
                                table({"x"}, {{"0"}, {"nan"}, {"0"}}), explicitRules({numeric}));
    QCOMPARE(result.statistics.differentCells, 3);
    QVERIFY(warningContains(result, QStringLiteral("literal text")));
}

void TableCompareTests::statistics()
{
    const auto left = table({"id", "value", "ignore"}, {{"A", "same", "x"}, {"B", "old", "x"}, {"L", "left", "x"}});
    const auto right = table({"id", "value", "ignore"}, {{"A", "same", "y"}, {"B", "new", "y"}, {"R", "right", "y"}});
    const auto result = compare(left, right, explicitRules({{0, 0, ColumnRole::Key}, {1, 1},
                                                            {2, 2, ColumnRole::Ignore}}, RowAlignment::Key));
    verifyCoverage(result, 3, 3);
    QCOMPARE(result.statistics.equalRows, 1);
    QCOMPARE(result.statistics.differentRows, 1);
    QCOMPARE(result.statistics.leftOnlyRows, 1);
    QCOMPARE(result.statistics.rightOnlyRows, 1);
    QCOMPARE(result.statistics.differentCells, 5);
    QCOMPARE(result.statistics.ignoredCells, 4);
    QCOMPARE(result.differences, QVector<int>({1, 2, 3}));
}

void TableCompareTests::boundedContentWork()
{
    Document left;
    Document right;
    left.headers = right.headers = QStringList{QStringLiteral("value")};
    for (int i = 0; i < 650; ++i) {
        left.rows.append({QStringLiteral("L%1").arg(i)});
        right.rows.append({QStringLiteral("R%1").arg(i)});
    }
    CompareOptions options;
    options.minimumSimilarity = 1;
    const auto result = compare(left, right, options);
    verifyCoverage(result, 650, 650);
    QCOMPARE(result.statistics.leftOnlyRows, 650);
    QCOMPARE(result.statistics.rightOnlyRows, 650);
    QCOMPARE(result.statistics.equalRows, 0);
    QVERIFY(warningContains(result, QStringLiteral("work limit")));
}

void TableCompareTests::longNumericContentWork()
{
    Document left;
    Document right;
    left.headers = right.headers = QStringList{QStringLiteral("value")};
    const QString precision(8192, QLatin1Char('1'));
    for (int i = 0; i < 16; ++i) {
        left.rows.append({QStringLiteral("1.") + precision + QString::number(i)});
        right.rows.append({QStringLiteral("2.") + precision + QString::number(i)});
    }
    ColumnRule numeric{0, 0};
    numeric.numeric = true;
    const auto result = compare(left, right, explicitRules({numeric}, RowAlignment::Content));
    verifyCoverage(result, 16, 16);
    QCOMPARE(result.statistics.leftOnlyRows, 16);
    QCOMPARE(result.statistics.rightOnlyRows, 16);
    QVERIFY(warningContains(result, QStringLiteral("work limit")));
}

void TableCompareTests::resultCellBound()
{
    Document large;
    for (int i = 0; i < 2001; ++i)
        large.headers.append(QString::number(i));
    large.rows.fill(QStringList{QStringLiteral("a")}, 2000);
    const auto result = compare(large, large);
    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("result-cell limit")));
    QVERIFY(result.rows.isEmpty());
}

QTEST_GUILESS_MAIN(TableCompareTests)
#include "tst_tablecompare.moc"
