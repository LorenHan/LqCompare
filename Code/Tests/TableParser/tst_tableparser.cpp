#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "../../Services/Table/tabledocument.h"

using namespace LqCompare::Table;

class TableParserTest : public QObject {
    Q_OBJECT
private slots:
    void recordBoundaries_data();
    void recordBoundaries();
    void quotesUnicodeAndMultiline();
    void raggedRowsStayRagged();
    void generatedAndDuplicateHeaders();
    void automaticDelimiter_data();
    void automaticDelimiter();
    void literalMulticharDelimiter();
    void malformedCsv_data();
    void malformedCsv();
    void unicodeEncodings_data();
    void unicodeEncodings();
    void malformedEncoding_data();
    void malformedEncoding();
    void encodingSelection();
    void unsupportedSources_data();
    void unsupportedSources();
    void optionsAndLimits();
    void loadAndAtomicFailure();
};

void TableParserTest::recordBoundaries_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<QStringList>("lastRow");
    QTest::newRow("empty") << QByteArray() << 0 << QStringList();
    QTest::newRow("one-field") << QByteArray("a") << 1 << QStringList({"a"});
    QTest::newRow("trailing-LF") << QByteArray("a\n") << 1 << QStringList({"a"});
    QTest::newRow("trailing-CRLF") << QByteArray("a\r\n") << 1 << QStringList({"a"});
    QTest::newRow("trailing-CR") << QByteArray("a\r") << 1 << QStringList({"a"});
    QTest::newRow("blank-line-is-record") << QByteArray("\n") << 1 << QStringList({""});
    QTest::newRow("two-blank-lines") << QByteArray("\r\n\r\n") << 2 << QStringList({""});
    QTest::newRow("mixed-line-ends") << QByteArray("a\r\nb\nc\rd") << 4 << QStringList({"d"});
    QTest::newRow("trailing-empty-field") << QByteArray("a,b,") << 1 << QStringList({"a", "b", ""});
    QTest::newRow("only-empty-fields") << QByteArray(",,") << 1 << QStringList({"", "", ""});
    QTest::newRow("quoted-empty-field") << QByteArray("\"\"") << 1 << QStringList({""});
    QTest::newRow("whitespace-is-data") << QByteArray(" a , b ") << 1 << QStringList({" a ", " b "});
}

void TableParserTest::recordBoundaries()
{
    QFETCH(QByteArray, bytes);
    QFETCH(int, rowCount);
    QFETCH(QStringList, lastRow);
    ParseOptions options;
    options.firstRowHeader = false;
    options.delimiter = ",";
    Document document;
    QString error = "stale";
    QVERIFY2(Document::parse(bytes, &document, &error, options), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(document.rows.size(), rowCount);
    if (rowCount)
        QCOMPARE(document.rows.last(), lastRow);
    QCOMPARE(document.hasHeader, false);
}

void TableParserTest::quotesUnicodeAndMultiline()
{
    Document document;
    QString error;
    const QByteArray bytes = QString::fromUtf8("编号,内容,引号\r\n1,\"你好,世界\r\n第二行\n第三行\r第四行\",\"他说\"\"你好\"\" 😀\"\r\n").toUtf8();
    QVERIFY2(Document::parse(bytes, &document, &error), qPrintable(error));
    QCOMPARE(document.headers, QStringList({QString::fromUtf8("编号"), QString::fromUtf8("内容"), QString::fromUtf8("引号")}));
    QCOMPARE(document.rows.size(), 1);
    QCOMPARE(document.rows.first(), QStringList({"1", QString::fromUtf8("你好,世界\r\n第二行\n第三行\r第四行"), QString::fromUtf8("他说\"你好\" 😀")}));
    QCOMPARE(document.delimiter, QString(","));
    QCOMPARE(document.encoding, QByteArray("UTF-8"));
}

void TableParserTest::raggedRowsStayRagged()
{
    Document document;
    QVERIFY(Document::parse("a,b,c\n1\n2,\n3,,\n4,5,6,7", &document));
    QCOMPARE(document.columnCount(), 4);
    QCOMPARE(document.rows.size(), 4);
    QCOMPARE(document.rows.at(0), QStringList({"1"}));
    QCOMPARE(document.rows.at(1), QStringList({"2", ""}));
    QCOMPARE(document.rows.at(2), QStringList({"3", "", ""}));
    QCOMPARE(document.rows.at(3), QStringList({"4", "5", "6", "7"}));
    QCOMPARE(document.headers, QStringList({"a", "b", "c", "Column 4"}));
    QCOMPARE(document.warnings.size(), 2);
}

void TableParserTest::generatedAndDuplicateHeaders()
{
    Document document;
    ParseOptions options;
    options.firstRowHeader = false;
    QVERIFY(Document::parse("1,2\n3", &document, nullptr, options));
    QCOMPARE(document.headers, QStringList({"Column 1", "Column 2"}));
    QCOMPARE(document.rows.first(), QStringList({"1", "2"}));
    QVERIFY(Document::parse("Column 2\n1,2", &document));
    QCOMPARE(document.headers, QStringList({"Column 2", "Column 2_"}));
    QVERIFY(Document::parse("a,a,\n1,2,3", &document));
    QCOMPARE(document.headers, QStringList({"a", "a", ""}));
    QVERIFY(document.warnings.join(" ").contains("ambiguous"));
}

void TableParserTest::automaticDelimiter_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("delimiter");
    QTest::newRow("csv") << QByteArray("a,b\n1,2\n") << QString("data.csv") << QString(",");
    QTest::newRow("tsv") << QByteArray("a\tb\n1\t2\n") << QString() << QString("\t");
    QTest::newRow("semicolon") << QByteArray("a;b\n1;2\n") << QString() << QString(";");
    QTest::newRow("pipe") << QByteArray("a|b\n1|2\n") << QString() << QString("|");
    QTest::newRow("quoted-comma") << QByteArray("a;b\n\"x,y,z\";2\n") << QString() << QString(";");
    QTest::newRow("quoted-newline") << QByteArray("a\tb\n\"x,\ny\"\t2\n") << QString() << QString("\t");
    QTest::newRow("tsv-tie") << QByteArray("a,b\tc\n1,2\t3") << QString("data.TSV") << QString("\t");
    QTest::newRow("tsv-grouped-numbers") << QByteArray("a\t1,234,567\nb\t2,345,678") << QString("data.tsv") << QString("\t");
    QTest::newRow("single-column-tsv") << QByteArray("a\nb") << QString("data.tsv") << QString("\t");
    QTest::newRow("empty-tsv") << QByteArray() << QString("data.tsv") << QString("\t");
    QTest::newRow("empty-default") << QByteArray() << QString() << QString(",");
}

void TableParserTest::automaticDelimiter()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, source);
    QFETCH(QString, delimiter);
    Document document;
    QString error;
    QVERIFY2(Document::parse(bytes, &document, &error, {}, source), qPrintable(error));
    QCOMPARE(document.delimiter, delimiter);
}

void TableParserTest::literalMulticharDelimiter()
{
    ParseOptions options;
    options.delimiter = "||";
    Document document;
    QString error;
    QVERIFY2(Document::parse("a||b||c\n\"x||y\"||z|w||\n", &document, &error, options), qPrintable(error));
    QCOMPARE(document.headers, QStringList({"a", "b", "c"}));
    QCOMPARE(document.rows.first(), QStringList({"x||y", "z|w", ""}));
    options.delimiter = ".*";
    QVERIFY(Document::parse("a.*b\n1.*2", &document, &error, options));
    QCOMPARE(document.rows.first(), QStringList({"1", "2"}));
    options.delimiter = QString::fromUtf8("分隔");
    QVERIFY(Document::parse(QString::fromUtf8("a分隔b\n1分隔2").toUtf8(), &document, &error, options));
    QCOMPARE(document.rows.first(), QStringList({"1", "2"}));
}

void TableParserTest::malformedCsv_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("position");
    QTest::newRow("unclosed") << QByteArray("a,b\n1,\"x") << QString("row 2, column 2");
    QTest::newRow("junk-after-close") << QByteArray("a,b\n1,\"x\"z") << QString("row 2, column 2");
    QTest::newRow("space-after-close") << QByteArray("a,b\n1,\"x\" ") << QString("row 2, column 2");
    QTest::newRow("quote-inside-unquoted") << QByteArray("a,b\n1,x\"y") << QString("row 2, column 2");
    QTest::newRow("quote-after-space") << QByteArray("a,b\n1, \"y\"") << QString("row 2, column 2");
    QTest::newRow("multiline-unclosed") << QByteArray("a,b\n1,\"x\ny\r\nz") << QString("line 4, character 2");
}

void TableParserTest::malformedCsv()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, position);
    Document document;
    document.path = "unchanged";
    document.rows.append(QStringList({"original"}));
    QString error;
    QVERIFY(!Document::parse(bytes, &document, &error));
    QVERIFY2(error.contains(position), qPrintable(error));
    QCOMPARE(document.path, QString("unchanged"));
    QCOMPARE(document.rows, QVector<QStringList>({QStringList({"original"})}));
}

void TableParserTest::unicodeEncodings_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QByteArray>("encoding");
    const QString content = QString::fromUtf8("名,值\n甲,😀");
    QTest::newRow("UTF8") << content.toUtf8() << QByteArray("UTF-8");
    QTest::newRow("UTF8-BOM") << (QByteArray::fromHex("efbbbf") + content.toUtf8()) << QByteArray("UTF-8");
    QByteArray little = QByteArray::fromHex("fffe");
    QByteArray big = QByteArray::fromHex("feff");
    for (QChar c : content) {
        little.append(char(c.unicode() & 0xff));
        little.append(char(c.unicode() >> 8));
        big.append(char(c.unicode() >> 8));
        big.append(char(c.unicode() & 0xff));
    }
    QTest::newRow("UTF16LE-BOM") << little << QByteArray("UTF-16LE");
    QTest::newRow("UTF16BE-BOM") << big << QByteArray("UTF-16BE");
}

void TableParserTest::unicodeEncodings()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QByteArray, encoding);
    Document document;
    QString error;
    QVERIFY2(Document::parse(bytes, &document, &error), qPrintable(error));
    QCOMPARE(document.encoding, encoding);
    QCOMPARE(document.headers, QStringList({QString::fromUtf8("名"), QString::fromUtf8("值")}));
    QCOMPARE(document.rows.first(), QStringList({QString::fromUtf8("甲"), QString::fromUtf8("😀")}));
}

void TableParserTest::malformedEncoding_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("utf8-incomplete") << QByteArray::fromHex("61e4b8");
    QTest::newRow("utf8-invalid") << QByteArray::fromHex("61ff");
    QTest::newRow("utf8-overlong") << QByteArray::fromHex("c0af");
    QTest::newRow("utf8-surrogate") << QByteArray::fromHex("eda080");
    QTest::newRow("utf8-beyond-range") << QByteArray::fromHex("f4908080");
    QTest::newRow("utf16le-odd") << QByteArray::fromHex("fffe610001");
    QTest::newRow("utf16be-odd") << QByteArray::fromHex("feff006101");
    QTest::newRow("utf16le-lone-high") << QByteArray::fromHex("fffe00d8");
    QTest::newRow("utf16be-lone-low") << QByteArray::fromHex("feffdc00");
    QTest::newRow("utf16-high-ascii") << QByteArray::fromHex("fffe00d86100");
    QTest::newRow("utf16-high-high") << QByteArray::fromHex("fffe00d800d8");
    QTest::newRow("nul-control") << QByteArray::fromHex("612c62000a312c32");
    QTest::newRow("binary-control") << QByteArray::fromHex("612c62010a312c32");
    QTest::newRow("UTF32LE") << QByteArray::fromHex("fffe000061000000");
    QTest::newRow("UTF32BE") << QByteArray::fromHex("0000feff00000061");
}

void TableParserTest::malformedEncoding()
{
    QFETCH(QByteArray, bytes);
    Document document;
    document.path = "original";
    QString error;
    QVERIFY(!Document::parse(bytes, &document, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.path, QString("original"));
}

void TableParserTest::encodingSelection()
{
    ParseOptions options;
    options.encoding = "ISO-8859-1";
    Document document;
    QString error;
    QVERIFY2(Document::parse(QByteArray::fromHex("636166e90a6f6ce9"), &document, &error, options), qPrintable(error));
    QCOMPARE(document.headers, QStringList({QString::fromUtf8("café")}));
    QCOMPARE(document.rows.first(), QStringList({QString::fromUtf8("olé")}));
    options.encoding = "UTF-16LE";
    QVERIFY(Document::parse(QByteArray::fromHex("61000a006200"), &document, &error, options));
    QCOMPARE(document.headers, QStringList({"a"}));
    QCOMPARE(document.rows.first(), QStringList({"b"}));
    options.encoding = "UTF-16";
    QVERIFY(!Document::parse(QByteArray::fromHex("6100"), &document, &error, options));
    QVERIFY(Document::parse(QByteArray::fromHex("fffe6100"), &document, &error, options));
    options.encoding = "UTF-8";
    QVERIFY(!Document::parse(QByteArray::fromHex("fffe6100"), &document, &error, options));
    QVERIFY(error.contains("conflicts"));
    options.encoding = "not-a-codec";
    QVERIFY(!Document::parse("a,b", &document, &error, options));
    QVERIFY(error.contains("Unknown"));
    QVERIFY(Document::parse(QString::fromUtf8("a\n�").toUtf8(), &document));
    QCOMPARE(document.rows.first(), QStringList({QString::fromUtf8("�")}));
}

void TableParserTest::unsupportedSources_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("xlsx-extension") << QString("data.XLSX") << QByteArray("a,b");
    QTest::newRow("xls-extension") << QString("data.xls") << QByteArray("a,b");
    QTest::newRow("html-extension") << QString("data.html") << QByteArray("a,b");
    QTest::newRow("zip-magic") << QString("data.csv") << QByteArray::fromHex("504b03046162");
    QTest::newRow("ole-magic") << QString("data.csv") << QByteArray::fromHex("d0cf11e0a1b11ae1");
    QTest::newRow("html-magic") << QString() << QByteArray(" \n<!DOCTYPE HTML>\n<html><table><tr><td>1</td></tr></table></html>");
    QTest::newRow("html-table") << QString() << QByteArray("<table class='x'><tr><td>1</td></tr></table>");
}

void TableParserTest::unsupportedSources()
{
    QFETCH(QString, source);
    QFETCH(QByteArray, bytes);
    Document document;
    QString error;
    QVERIFY(!Document::parse(bytes, &document, &error, {}, source));
    QVERIFY2(error.contains("not supported"), qPrintable(error));
}

void TableParserTest::optionsAndLimits()
{
    Document document;
    QString error;
    ParseOptions options;
    for (const QString &delimiter : {QString("\""), QString("\n"), QString("\r"), QString(QChar(0))}) {
        options.delimiter = delimiter;
        QVERIFY(!Document::parse("a,b", &document, &error, options));
        QVERIFY(error.contains("delimiter"));
    }
    QVERIFY(!Document::parse({}, nullptr, &error));
    QVERIFY(!Document::load({}, nullptr, &error));
    QVERIFY(!Document::parse(QByteArray(16384, ','), &document, &error));
    QVERIFY2(error.contains("column limit"), qPrintable(error));
}

void TableParserTest::loadAndAtomicFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("data.tsv");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("a\tb\n1\t2\n"), qint64(8));
    file.close();
    Document document;
    QString error;
    QVERIFY2(Document::load(path, &document, &error), qPrintable(error));
    QCOMPARE(document.path, path);
    QCOMPARE(document.delimiter, QString("\t"));
    QCOMPARE(document.rows.first(), QStringList({"1", "2"}));
    QVERIFY(!Document::load(directory.filePath("missing.csv"), &document, &error));
    QCOMPARE(document.path, path);
    QCOMPARE(document.rows.first(), QStringList({"1", "2"}));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(Document::MaximumFileBytes + 1));
    file.close();
    QVERIFY(!Document::load(path, &document, &error));
    QVERIFY(error.contains("32 MiB"));
    QCOMPARE(document.rows.first(), QStringList({"1", "2"}));
    QVERIFY(Document::load({}, &document, &error));
    QVERIFY(document.path.isEmpty());
    QVERIFY(document.rows.isEmpty());
    QVERIFY(document.headers.isEmpty());
    QVERIFY(error.isEmpty());
    ParseOptions options;
    options.encoding = "UTF-16";
    QVERIFY2(Document::load({}, &document, &error, options), qPrintable(error));
    QVERIFY(document.rows.isEmpty());
    QCOMPARE(document.encoding, QByteArray("UTF-16"));
    options.encoding = "unknown-codec";
    QVERIFY(!Document::load({}, &document, &error, options));
    options.encoding.clear();
    options.delimiter = "\n";
    QVERIFY(!Document::load({}, &document, &error, options));
}

QTEST_GUILESS_MAIN(TableParserTest)
#include "tst_tableparser.moc"
