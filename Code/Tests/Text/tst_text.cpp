#include <QtTest>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "textdiff.h"

using namespace LqCompare::Text;
namespace {
Document decoded(const QByteArray &bytes)
{
    Document document;
    if (!Document::decode(bytes, &document)) qFatal("Fixture could not decode");
    return document;
}
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Could not write fixture");
}
QByteArray readFile(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
// 差异块的类别名。用名字而不是 `int(change)`：快照串是给人读的，
// 写数字的话一旦枚举顺序被调整，黄金串会跟着变成一串看不懂的错位数字。
QString changeName(Change change)
{
    switch (change) {
    case Change::Equal: return QStringLiteral("Equal");
    case Change::Insert: return QStringLiteral("Insert");
    case Change::Delete: return QStringLiteral("Delete");
    case Change::Replace: return QStringLiteral("Replace");
    case Change::Ignored: return QStringLiteral("Ignored");
    }
    return QStringLiteral("?");
}
// 把 `Result` 的**每一个字段**摊成一行行文本。
//
// 为什么不是一条条 QCOMPARE：`Result` 有 7 个字段要逐字段比（标准 4 的原话），
// 而快照的意义在于「**没被列出来**的字段也要有约束」——只列自己想到的那几个，
// 下次给 `Result` 加字段时不会有任何东西变红，而快照会悄悄失去一半覆盖。
// 摊成文本同时解决了另一件事：失败时可以直接贴出两边的全文，看得见差了哪一行。
QString fingerprint(const Result &result)
{
    QStringList lines;
    lines << QStringLiteral("blocks=%1 differences=%2 ignored=%3 limited=%4 rows=%5")
                 .arg(result.blocks.size()).arg(result.differences.size())
                 .arg(result.ignoredBlocks).arg(result.alignmentLimited ? 1 : 0)
                 .arg(result.rows.size());
    for (int i = 0; i < result.blocks.size(); ++i) {
        const Block &block = result.blocks[i];
        lines << QStringLiteral("b%1 %2 L%3+%4 R%5+%6 rows%7+%8")
                     .arg(i).arg(changeName(block.change))
                     .arg(block.leftStart).arg(block.leftCount)
                     .arg(block.rightStart).arg(block.rightCount)
                     .arg(block.firstRow).arg(block.rowCount);
    }
    for (int i = 0; i < result.rows.size(); ++i) {
        const Row &row = result.rows[i];
        lines << QStringLiteral("r%1 l%2 r%3 b%4 %5")
                     .arg(i).arg(row.leftLine).arg(row.rightLine).arg(row.block)
                     .arg(changeName(row.change));
    }
    QStringList differences;
    for (int index : result.differences) differences << QString::number(index);
    lines << QStringLiteral("diffs=[%1]").arg(differences.join(QLatin1Char(',')));
    return lines.join(QLatin1Char('\n'));
}
// 头文件里那句承诺的唯一可观测点：「Adversarial work is bounded; the remaining range is an
// explicit replacement, **never silently considered equal**」。预算用尽本身不可怕，
// 可怕的是用尽之后把没比过的两段当成相同——那会在界面上表现为「两份文件一模一样」。
bool equalBlocksPairIdenticalLines(const Result &result, const QVector<Line> &left, const QVector<Line> &right)
{
    for (const Block &block : result.blocks) {
        if (block.change != Change::Equal) continue;
        if (block.leftCount != block.rightCount) return false;
        for (int i = 0; i < block.leftCount; ++i)
            if (!(left[block.leftStart + i] == right[block.rightStart + i])) return false;
    }
    return true;
}
}

class TextTests : public QObject {
    Q_OBJECT
private slots:
    void emptyAndEndings()
    {
        QCOMPARE(decoded({}).lines().size(), 0);
        QCOMPARE(decoded("\n").lines().size(), 1);
        QCOMPARE(decoded("\n").lines().first().text, QString());
        Document doc = decoded("one\r\ntwo\rthree\nfour");
        QCOMPARE(doc.lines().size(), 4);
        QCOMPARE(doc.bytes(), QByteArray("one\r\ntwo\rthree\nfour"));
        QVERIFY(doc.eolDescription().startsWith("Mixed"));
        QCOMPARE(doc.normalizedText(), QStringLiteral("one\ntwo\nthree\nfour"));
        QVERIFY(!doc.isModified());
    }
    void unicodeRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QByteArray>("codec");
        QTest::newRow("utf8") << QByteArray::fromHex("e4b8ade696870af09f9880") << QByteArray("UTF-8");
        QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbfe4b8ade696870a") << QByteArray("UTF-8");
        QTest::newRow("utf16le") << QByteArray::fromHex("fffe2d4e87650d000a003dd800de") << QByteArray("UTF-16LE");
        QTest::newRow("utf16be") << QByteArray::fromHex("feff4e2d6587000d000ad83dde00") << QByteArray("UTF-16BE");
        QTest::newRow("utf32le") << QByteArray::fromHex("fffe00002d4e0000876500000d0000000a00000000f60100") << QByteArray("UTF-32LE");
        QTest::newRow("utf32be") << QByteArray::fromHex("0000feff00004e2d000065870000000d0000000a0001f600") << QByteArray("UTF-32BE");
        QTest::newRow("utf32le-empty") << QByteArray::fromHex("fffe0000") << QByteArray("UTF-32LE");
        QTest::newRow("utf32be-empty") << QByteArray::fromHex("0000feff") << QByteArray("UTF-32BE");
    }
    void unicodeRoundTrip()
    {
        QFETCH(QByteArray, input); QFETCH(QByteArray, codec);
        auto doc = decoded(input);
        QCOMPARE(doc.codecName(), codec);
        QCOMPARE(doc.decodingErrors(), 0);
        QCOMPARE(doc.bytes(), input);
        QVERIFY(!doc.isModified());
    }
    void unsafeDecodingAndLegacy()
    {
        auto invalid = decoded(QByteArray::fromHex("6162ff63e4b8"));
        QVERIFY(invalid.decodingErrors() >= 1);
        QVERIFY(!invalid.canEdit());
        QVERIFY(!invalid.setNormalizedText("changed"));
        QVERIFY(!invalid.warning().isEmpty());
        auto binary = decoded(QByteArray("a\0b", 3));
        QVERIFY(!binary.canEdit());
        const auto paragraph = decoded(QByteArray::fromHex("61e280a962"));
        QVERIFY(!paragraph.canEdit());
        QCOMPARE(paragraph.bytes(), QByteArray::fromHex("61e280a962"));
        Document latin;
        QVERIFY(Document::decode(QByteArray::fromHex("636166e9"), &latin, nullptr, "ISO-8859-1"));
        QCOMPARE(latin.normalizedText(), QString::fromUtf8(QByteArray::fromHex("636166c3a9")));
        QVERIFY(latin.setNormalizedText(QString::fromUtf8(QByteArray::fromHex("e4b8ade69687"))));
        QString error;
        latin.bytes(&error);
        QVERIFY(!error.isEmpty());
        auto incomplete = decoded("valid");
        QVERIFY(incomplete.setNormalizedText(QString(QChar(0xd800))));
        incomplete.bytes(&error);
        QVERIFY(!error.isEmpty());
    }
    void utf32RejectsMalformedScalarsAndTruncatedUnits()
    {
        const QList<QByteArray> damaged = {
            QByteArray::fromHex("fffe000000001100"), // Above U+10FFFF.
            QByteArray::fromHex("fffe000000d80000"), // Surrogate scalar, LE.
            QByteArray::fromHex("0000feff0000dfff"), // Surrogate scalar, BE.
            QByteArray::fromHex("0000feffffffffff"), // Invalid scalar.
            QByteArray::fromHex("fffe0000610000"),   // Truncated LE unit.
            QByteArray::fromHex("0000feff000061")    // Truncated BE unit.
        };
        QTemporaryDir directory;
        for (const auto &input : damaged) {
            auto document = decoded(input);
            QCOMPARE(document.decodingErrors(), 1);
            QCOMPARE(document.normalizedText(), QString(QChar::ReplacementCharacter));
            QVERIFY(!document.canEdit());
            QString error;
            QVERIFY(!document.setNormalizedText("replacement", &error));
            QVERIFY(!document.saveAs(directory.filePath("must-not-exist"), false, &error));
            QVERIFY(!QFile::exists(directory.filePath("must-not-exist")));
        }
    }
    void utf32EditSavePreservesBomAndEndianness()
    {
        QTemporaryDir directory;
        const QList<QByteArray> inputs = {
            QByteArray::fromHex("fffe0000610000000d0000000a000000"),
            QByteArray::fromHex("0000feff000000610000000d0000000a")
        };
        const QList<QByteArray> expected = {
            QByteArray::fromHex("fffe000000f601000d0000000a000000"),
            QByteArray::fromHex("0000feff0001f6000000000d0000000a")
        };
        const QString edited = QString::fromUtf8(QByteArray::fromHex("f09f98800a"));
        for (int i = 0; i < inputs.size(); ++i) {
            const QString path = directory.filePath(QString::number(i));
            writeFile(path, inputs[i]);
            Document document;
            QVERIFY(Document::load(path, &document));
            QVERIFY(document.canEdit());
            QVERIFY(document.setNormalizedText(edited));
            QVERIFY(document.save());
            QCOMPARE(readFile(path), expected[i]);
            QVERIFY(!document.isModified());
            // Explicit decoding also supports BOM-less UTF-32 without adding a BOM.
            Document noBom;
            QVERIFY(Document::decode(expected[i].mid(4), &noBom, nullptr, document.codecName()));
            QVERIFY(!noBom.hasBom());
            QCOMPARE(noBom.normalizedText(), edited);
            QCOMPARE(noBom.bytes(), expected[i].mid(4));
        }
    }
    void excessiveLineCountsFailWithoutReplacingCurrentDocument()
    {
        Document document = decoded("keep me\n");
        QString error;
        QVERIFY(!Document::decode(QByteArray(Document::MaximumLines + 1, '\n'), &document, &error));
        QVERIFY(error.contains("500,000"));
        QCOMPARE(document.bytes(), QByteArray("keep me\n"));
        QVERIFY(!document.setNormalizedText(QString(Document::MaximumLines, QLatin1Char('\n')) + QLatin1Char('x'), &error));
        QCOMPARE(document.bytes(), QByteArray("keep me\n"));
    }
    void insertionDeletionReplacement()
    {
        auto left = decoded("a\nb\nc\nd\n");
        auto right = decoded("a\nnew\nb\nchanged\nd\n");
        const auto result = compare(left.lines(), right.lines());
        QCOMPARE(result.differences.size(), 2);
        QCOMPARE(result.blocks[result.differences[0]].change, Change::Insert);
        QCOMPARE(result.blocks[result.differences[1]].change, Change::Replace);
        QCOMPARE(result.rows[1].leftLine, -1);
        QCOMPARE(result.rows[1].rightLine, 1);
        QCOMPARE(result.rows[2].leftLine, 1);
        QCOMPARE(result.rows[2].rightLine, 2);
        const auto reversed = compare(right.lines(), left.lines());
        QCOMPARE(reversed.blocks[reversed.differences[0]].change, Change::Delete);
        const auto empty = compare({}, left.lines());
        QCOMPARE(empty.blocks.size(), 1);
        QCOMPARE(empty.blocks.first().change, Change::Insert);
        QVERIFY(compare({}, {}).blocks.isEmpty());
    }
    void eolRulesIndependent()
    {
        auto a = decoded("a\r\nb\r\n"), b = decoded("a\nb\n"), c = decoded("a\nb");
        CompareOptions options;
        QVERIFY(compare(a.lines(), b.lines(), options).differences.isEmpty());
        QVERIFY(!compare(a.lines(), c.lines(), options).differences.isEmpty());
        options.ignoreFinalNewline = true;
        QVERIFY(compare(a.lines(), c.lines(), options).differences.isEmpty());
        options.ignoreEol = false;
        QCOMPARE(compare(a.lines(), b.lines(), options).differences.size(), 1);
        QVERIFY(compare(b.lines(), c.lines(), options).differences.isEmpty());
    }
    void ignoreRulesKeepOriginals()
    {
        const auto a = decoded(" Foo  BAR \nfoo bar\n"), b = decoded("foo\tbar\nfoobar\n");
        CompareOptions options;
        options.ignoreCase = true;
        options.whitespace = Whitespace::IgnoreChanges;
        auto result = compare(a.lines(), b.lines(), options);
        QCOMPARE(result.ignoredBlocks, 1);
        QCOMPARE(result.differences.size(), 1);
        options.whitespace = Whitespace::IgnoreAll;
        result = compare(a.lines(), b.lines(), options);
        QVERIFY(result.differences.isEmpty());
        QCOMPARE(a.bytes(), QByteArray(" Foo  BAR \nfoo bar\n"));
    }
    void editPreservesMixedEndings()
    {
        auto doc = decoded("first\r\nsecond\rthird\nlast");
        QVERIFY(doc.setNormalizedText("first\nsecond\nthird\nlast"));
        QVERIFY(!doc.isModified());
        QVERIFY(doc.setNormalizedText("first\nsecond changed\nthird\nlast"));
        QCOMPARE(doc.bytes(), QByteArray("first\r\nsecond changed\rthird\nlast"));
        doc.setEol(Eol::CRLF);
        QCOMPARE(doc.bytes(), QByteArray("first\r\nsecond changed\r\nthird\r\nlast"));
    }
    void guardedAtomicSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("left.txt");
        writeFile(path, "original\r\n");
        Document document;
        QVERIFY(Document::load(path, &document));
        QVERIFY(document.setNormalizedText("edited\n"));
        writeFile(path, "external\r\n");
        QString error;
        QVERIFY(!document.save(&error));
        QVERIFY(error.contains("changed on disk"));
        QCOMPARE(readFile(path), QByteArray("external\r\n"));
        QVERIFY(document.isModified());
        const auto copy = directory.filePath("copy.txt");
        QVERIFY(document.saveAs(copy, false, &error));
        QCOMPARE(readFile(copy), QByteArray("edited\r\n"));
        QVERIFY(!document.isModified());
        QVERIFY(document.setNormalizedText("another\n"));
        QVERIFY(!document.saveAs(path, false, &error));
        QVERIFY(document.save(&error));
        QCOMPARE(readFile(copy), QByteArray("another\r\n"));
    }
    void randomAlignmentsAreMinimal()
    {
        quint32 seed = 17;
        auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed; };
        for (int sample = 0; sample < 1200; ++sample) {
            QVector<Line> a, b;
            const int n = next() % 24, m = next() % 24;
            for (int i = 0; i < n; ++i) a.append({QString::number(next() % 7), Eol::LF});
            for (int i = 0; i < m; ++i) b.append({QString::number(next() % 7), Eol::LF});
            QVector<int> dp((n + 1) * (m + 1));
            for (int i = 1; i <= n; ++i) for (int j = 1; j <= m; ++j)
                dp[i * (m + 1) + j] = a[i - 1] == b[j - 1]
                    ? 1 + dp[(i - 1) * (m + 1) + j - 1]
                    : qMax(dp[(i - 1) * (m + 1) + j], dp[i * (m + 1) + j - 1]);
            const auto result = compare(a, b);
            int leftCursor = 0, rightCursor = 0, equal = 0;
            for (const auto &block : result.blocks) {
                QCOMPARE(block.leftStart, leftCursor);
                QCOMPARE(block.rightStart, rightCursor);
                leftCursor += block.leftCount;
                rightCursor += block.rightCount;
                if (block.change == Change::Equal) {
                    QCOMPARE(block.leftCount, block.rightCount);
                    for (int i = 0; i < block.leftCount; ++i)
                        QVERIFY(a[block.leftStart + i] == b[block.rightStart + i]);
                    equal += block.leftCount;
                }
            }
            QCOMPARE(leftCursor, n); QCOMPARE(rightCursor, m);
            QCOMPARE(equal, dp[n * (m + 1) + m]);
            QVERIFY(!result.alignmentLimited);
            const auto again = compare(a, b);
            QCOMPARE(again.rows.size(), result.rows.size());
            for (int i = 0; i < result.rows.size(); ++i) {
                QCOMPARE(again.rows[i].leftLine, result.rows[i].leftLine);
                QCOMPARE(again.rows[i].rightLine, result.rows[i].rightLine);
            }
        }
    }
    void tenThousandUnrelatedLines()
    {
        QVector<Line> a, b;
        for (int i = 0; i < 10000; ++i) {
            a.append({QStringLiteral("left %1").arg(i), Eol::LF});
            b.append({QStringLiteral("right %1").arg(i), Eol::LF});
        }
        QElapsedTimer timer; timer.start();
        const auto result = compare(a, b);
        QVERIFY2(timer.elapsed() < 2000, "10k unrelated lines exceeded 2s");
        QCOMPARE(result.blocks.size(), 1);
        QCOMPARE(result.blocks.first().change, Change::Replace);
        QVERIFY(!result.alignmentLimited);
        for (int i = 0; i < 10000; i += 100) b[i] = a[i];
        timer.restart();
        const auto typical = compare(a, b);
        QVERIFY2(timer.elapsed() < 2000, "bounded adversarial alignment exceeded 2s");
        QVERIFY(!typical.differences.isEmpty());
    }
    // 标准 3 的前半句：「对全相同输入返回单个相同块」。
    //
    // 这条之所以要单独写，是因为**「没有差异」与「只有一个块」是两件事**：
    // 一个把所有行都拆成独立块的实现同样没有差异，界面却会把每一行都画成被改动过的行
    // （块边界就是界面上分隔线的位置）。原有用例只断言了 `differences` 为空，
    // 对「有几个块」没有任何约束——反向验证里把 `addBlock` 的合并逻辑去掉，
    // 那一整轮没有一条用例变红。
    void identicalInputYieldsASingleEqualBlock()
    {
        auto left = decoded("a\nb\nc\n");
        auto right = decoded("a\nb\nc\n");
        const auto result = compare(left.lines(), right.lines());
        QCOMPARE(result.blocks.size(), 1);
        const Block &block = result.blocks.first();
        QCOMPARE(block.change, Change::Equal);
        QCOMPARE(block.leftStart, 0);
        QCOMPARE(block.rightStart, 0);
        QCOMPARE(block.leftCount, 3);
        QCOMPARE(block.rightCount, 3);
        QCOMPARE(block.firstRow, 0);
        QCOMPARE(block.rowCount, 3);
        QVERIFY(result.differences.isEmpty());
        QCOMPARE(result.ignoredBlocks, 0);
        QVERIFY(!result.alignmentLimited);
        QCOMPARE(result.rows.size(), 3);
        for (int i = 0; i < result.rows.size(); ++i) {
            QCOMPARE(result.rows[i].leftLine, i);
            QCOMPARE(result.rows[i].rightLine, i);
            QCOMPARE(result.rows[i].block, 0);
            QCOMPARE(result.rows[i].change, Change::Equal);
        }
        // 边界：单行、以及两个空输入（都不许崩，也不许产出空块）。
        const auto single = compare(decoded("only\n").lines(), decoded("only\n").lines());
        QCOMPARE(single.blocks.size(), 1);
        QCOMPARE(single.blocks.first().leftCount, 1);
        const auto nothing = compare({}, {});
        QVERIFY(nothing.blocks.isEmpty());
        QVERIFY(nothing.rows.isEmpty());
        QVERIFY(nothing.differences.isEmpty());
        // 「全相同」的另一种来源：两行**原文不同、按选项等价**（忽略空白）。
        // 它仍然必须合并成一个块，而且必须被标成 Ignored 而不是 Equal
        // ——标成 Equal 会让界面把「被忽略的差异」画成「真的相同」。
        auto spaced = decoded("a b\nc d\n"), compact = decoded("ab\ncd\n");
        CompareOptions options;
        options.whitespace = Whitespace::IgnoreAll;
        const auto ignored = compare(spaced.lines(), compact.lines(), options);
        QCOMPARE(ignored.blocks.size(), 1);
        QCOMPARE(ignored.blocks.first().change, Change::Ignored);
        QCOMPARE(ignored.blocks.first().leftCount, 2);
        QCOMPARE(ignored.ignoredBlocks, 1);
        QVERIFY(ignored.differences.isEmpty());
    }
    // 标准 4：算法为纯函数，输入相同则输出**逐字段**相同，并且有快照。
    //
    // 两个断言各管一件事，缺一不可：
    //  - 两次运行逐字段相同 —— 管「纯函数」。原有用例只比了两遍的 `rows[i].leftLine/rightLine`，
    //    块、block 归属、differences、ignoredBlocks 与 alignmentLimited 都不在约束里；
    //    一个随手引入的 `QSet`/`QHash` 迭代顺序依赖（本模块真的在 `run()` 里用 `QSet`）
    //    可以只在块边界上表现不稳定，而那种不稳定正好落在没有被比对的字段上。
    //  - 与写死的黄金串相同 —— 管「快照」。只比自己两次等于在测「我等于我自己」，
    //    一个把左右两侧整体对调的 bug 会让两遍都一样地错。
    void resultIsPureAndMatchesSnapshot()
    {
        struct Fixture { const char *left; const char *right; const char *golden; };
        // 黄金串是**手推**出来的（在纸上枚举 Myers 的 ranges 再算块与行），
        // 不是把实现跑出来的结果抄回来——抄回来的快照只能证明「今天和昨天一样」，
        // 证明不了「今天是对的」。
        const Fixture fixtures[] = {
            {"a\nb\n", "a\nc\nb\n",
             "blocks=3 differences=1 ignored=0 limited=0 rows=3\n"
             "b0 Equal L0+1 R0+1 rows0+1\n"
             "b1 Insert L1+0 R1+1 rows1+1\n"
             "b2 Equal L1+1 R2+1 rows2+1\n"
             "r0 l0 r0 b0 Equal\n"
             "r1 l-1 r1 b1 Insert\n"
             "r2 l1 r2 b2 Equal\n"
             "diffs=[1]"},
            // 同一对输入左右对调：块类型必须整体从 Insert 变成 Delete，
            // 行里的 -1 也必须换到另一侧。
            {"a\nc\nb\n", "a\nb\n",
             "blocks=3 differences=1 ignored=0 limited=0 rows=3\n"
             "b0 Equal L0+1 R0+1 rows0+1\n"
             "b1 Delete L1+1 R1+0 rows1+1\n"
             "b2 Equal L2+1 R1+1 rows2+1\n"
             "r0 l0 r0 b0 Equal\n"
             "r1 l1 r-1 b1 Delete\n"
             "r2 l2 r1 b2 Equal\n"
             "diffs=[1]"},
            // 空左 / 非空右：整份内容是一个 Insert 块，且没有「左右都空」的空块。
            {"", "x\ny\n",
             "blocks=1 differences=1 ignored=0 limited=0 rows=2\n"
             "b0 Insert L0+0 R0+2 rows0+2\n"
             "r0 l-1 r0 b0 Insert\n"
             "r1 l-1 r1 b0 Insert\n"
             "diffs=[0]"},
            // 这一条是为了把 `firstRow` 与「块序号」分开而**必须**存在的夹具。
            //
            // 中间那个块占两行，于是它后面那个块的 `firstRow`（3）与它的序号（2）不再相等。
            // 前面四个夹具里每个块都只占一行，`firstRow` 恰好等于 `index`——把
            // `firstRow = rows.size()` 变异成 `firstRow = index` 时，它们一条都不会红。
            // 这不是断言写错了，而是**夹具让两个不同的量重合了**：
            // 写夹具时就该问一句「这里有没有两个量在当前数据上恰好相等」。
            {"a\nb\nc\n", "a\nX\nY\nb\nc\n",
             "blocks=3 differences=1 ignored=0 limited=0 rows=5\n"
             "b0 Equal L0+1 R0+1 rows0+1\n"
             "b1 Insert L1+0 R1+2 rows1+2\n"
             "b2 Equal L1+2 R3+2 rows3+2\n"
             "r0 l0 r0 b0 Equal\n"
             "r1 l-1 r1 b1 Insert\n"
             "r2 l-1 r2 b1 Insert\n"
             "r3 l1 r3 b2 Equal\n"
             "r4 l2 r4 b2 Equal\n"
             "diffs=[1]"},
            // 整份文档都被忽略：一个 Ignored 块、零个差异。
            {"Foo\n", "foo\n",
             "blocks=1 differences=0 ignored=1 limited=0 rows=1\n"
             "b0 Ignored L0+1 R0+1 rows0+1\n"
             "r0 l0 r0 b0 Ignored\n"
             "diffs=[]"},
        };
        for (const Fixture &fixture : fixtures) {
            auto left = decoded(fixture.left), right = decoded(fixture.right);
            CompareOptions options;
            // 第四条夹具靠忽略大小写才成为「整份忽略」；其余三条用它无影响。
            options.ignoreCase = true;
            const auto once = compare(left.lines(), right.lines(), options);
            const auto twice = compare(left.lines(), right.lines(), options);
            const QString printed = fingerprint(once);
            QCOMPARE(fingerprint(twice), printed);
            QCOMPARE(printed, QString::fromUtf8(fixture.golden));
            // 顺带把「行必被完整覆盖」这条不变量也钉住：块首尾相接铺满两侧，
            // 任何一行都不许凭空消失（`alignmentLimited` 时尤其如此——
            // 受限的那一段必须是显式的 Replace，而不是被静默当成相同）。
            int leftCursor = 0, rightCursor = 0;
            for (const Block &block : once.blocks) {
                QCOMPARE(block.firstRow + block.rowCount <= once.rows.size(), true);
                QCOMPARE(block.leftStart, leftCursor);
                QCOMPARE(block.rightStart, rightCursor);
                leftCursor += block.leftCount;
                rightCursor += block.rightCount;
            }
            QCOMPARE(leftCursor, left.lines().size());
            QCOMPARE(rightCursor, right.lines().size());
        }
    }
    // 边界条款：「输入规模超过阈值时自动切换到线性空间变体，不允许内存无界增长」。
    //
    // 这条可观测的那一面不是「跑得快」，而是 `alignmentLimited` —— 预算用尽必须**被报告**，
    // 因为调用方（界面）要靠它决定是画一个笼统的替换块，还是提示「这两份文件太大，
    // 只做了粗略对齐」。没有这条断言，`limited` 就是快照里唯一没人守的字段：
    // 把它恒置为 false，其余用例一条都不会红（它们全都在预算之内）。
    //
    // 构造：两侧各 4000 行、互不相同，只在正中间共享一行。共享行是必需的——
    // 若两侧毫无公共行，`run()` 会走「无公共行」的快速路径直接产出替换块，
    // 那条路径**刻意不消耗预算**也不设 `limited`（它本来就精确，没有受限可言）。
    void boundedAdversarialInputIsReportedAsLimited()
    {
        QVector<Line> left, right;
        for (int i = 0; i < 2000; ++i) {
            left.append({QStringLiteral("L%1").arg(i), Eol::LF});
            right.append({QStringLiteral("R%1").arg(i), Eol::LF});
        }
        left.append({QStringLiteral("shared"), Eol::LF});
        right.append({QStringLiteral("shared"), Eol::LF});
        for (int i = 2000; i < 4000; ++i) {
            left.append({QStringLiteral("L%1").arg(i), Eol::LF});
            right.append({QStringLiteral("R%1").arg(i), Eol::LF});
        }
        const auto result = compare(left, right);
        // 如果这条红了，先确认是不是这一轮把预算或算法改好了（那是好事）：
        // 那就把断言改写成对**新**上界的断言，而不是把它删掉——它守的是
        // 「受限这件事必须被说出来」，删掉它等于允许一次静默的粗略对齐。
        QVERIFY2(result.alignmentLimited, "预算用尽的输入必须报告 alignmentLimited");
        // 受限段必须是显式替换，绝不许把没比过的两段说成相同。
        QVERIFY(equalBlocksPairIdenticalLines(result, left, right));
        int leftCursor = 0, rightCursor = 0;
        for (const Block &block : result.blocks) {
            QCOMPARE(block.leftStart, leftCursor);
            QCOMPARE(block.rightStart, rightCursor);
            leftCursor += block.leftCount;
            rightCursor += block.rightCount;
        }
        QCOMPARE(leftCursor, left.size());
        QCOMPARE(rightCursor, right.size());
        // 受限之后仍然必须是纯函数：预算的消耗顺序不能受任何不确定来源影响。
        const auto again = compare(left, right);
        QCOMPARE(fingerprint(again), fingerprint(result));
    }

    // =========================================================================
    // TXT-008 忽略大小写差异（issue #63）
    //
    // 这一组守的是「Unicode 大小写**折叠**」而不是「小写化」。两者在 ASCII 上完全
    // 一样，只在非 ASCII 上分家；而分家的那几处（希腊语词尾 sigma、德语 ß、
    // Kelvin 符号、土耳其语 i/İ）恰好是用户真的会遇到、错了也看不出原因的输入。
    //
    // 为什么这一组不去新建一个套件：实现（规范化链）本来就住在 `textdiff.{h,cpp}`，
    // 而 TXT-008 的完成标准里**没有一条**指向还不存在的模块——与 TXT-002/TXT-003
    // 同一处置方式（见 handoff §1.25）。新套件只会把同一个 `compare()` 再包一层。
    // =========================================================================

    // 标准 1 前半句 + 标准 2 前半句：对齐固定的语料里，开关切换改变**每一行**的重要性。
    //
    // 为什么强调「对齐固定」：语料等长逐行对应，Myers 只会产出
    // Replace / Equal / Ignored 三种块，不会有 Insert / Delete 把行错开，
    // 于是「第 i 行」在两侧都有定义，断言才能落在**逐行**上。标准 2 后半句明确说了
    // 「不要求差异块数等于大小写行数」——所以「数块个数」这件事本身说明不了问题，
    // 逐行断言才是那条标准要的形状。
    void caseOnlyChangesTurnRowsIntoIgnoredButKeepThemMarked()
    {
        auto left = decoded("Alpha\nBravo\nCharlie\nDelta\n");
        // 右侧**故意**有一行写成全大写：两侧都含会被折叠改动的字符，
        // 于是「只对一侧应用规范化」的实现会在这里红——若右侧全是已经折好的
        // 小写，那种错误在数据上与正确实现完全重合，用例会静默放过它。
        auto right = decoded("alpha\nBRAVO\ncharlie\ndelta\n");
        CompareOptions options;

        const auto before = compare(left.lines(), right.lines(), options);
        QCOMPARE(before.blocks.size(), 1);
        QCOMPARE(before.blocks.first().change, Change::Replace);
        QCOMPARE(before.differences.size(), 1);
        QCOMPARE(before.ignoredBlocks, 0);
        QCOMPARE(before.rows.size(), 4);
        for (const Row &row : before.rows) QCOMPARE(row.change, Change::Replace);

        options.ignoreCase = true;
        const auto after = compare(left.lines(), right.lines(), options);
        // 行数与行本身都没变，变的是每一行的**语义**（真差异 → 被忽略）。
        // 视图画弱化标记的依据就是这个语义，所以它必须留在 `Result` 里：
        // 一个「把忽略行直接当成不存在」的实现会让这条红。
        QCOMPARE(after.rows.size(), before.rows.size());
        for (const Row &row : after.rows) QCOMPARE(row.change, Change::Ignored);
        QCOMPARE(after.blocks.size(), 1);
        QCOMPARE(after.blocks.first().change, Change::Ignored);
        QVERIFY(after.differences.isEmpty());
        QCOMPARE(after.ignoredBlocks, 1);
        // 折叠只发生在判等用的键上，原文一个字都不许动。
        QCOMPARE(left.lines().at(0).text, QStringLiteral("Alpha"));
        QCOMPARE(right.lines().at(0).text, QStringLiteral("alpha"));
    }

    // 标准 1 + 标准 2：混合语料里**只有**「仅大小写不同」的行失去差异身份。
    // 这一条防的是「开了忽略大小写就整体放行」——那种实现同样能让上一条全绿，
    // 却会让真正不同的行也悄悄消失，是本模块最危险的一种错。
    void onlyCaseOnlyRowsLoseDifferenceStatus()
    {
        auto left = decoded("Alpha\nkeep\nBravo\n");
        // 与上一条同理：第 0 行的右侧写成全大写，让「只折一侧」的错误无处躲。
        auto right = decoded("ALPHA\nkeep\nBravo2\n");
        CompareOptions options;

        const auto off = compare(left.lines(), right.lines(), options);
        QCOMPARE(off.blocks.size(), 3);
        QCOMPARE(off.blocks[0].change, Change::Replace);
        QCOMPARE(off.blocks[1].change, Change::Equal);
        QCOMPARE(off.blocks[2].change, Change::Replace);
        QCOMPARE(off.rows.size(), 3);
        QCOMPARE(off.rows[0].change, Change::Replace);
        QCOMPARE(off.rows[1].change, Change::Equal);
        QCOMPARE(off.rows[2].change, Change::Replace);
        QCOMPARE(off.differences.size(), 2);
        QCOMPARE(off.differences[0], 0);
        QCOMPARE(off.differences[1], 2);
        QCOMPARE(off.ignoredBlocks, 0);

        options.ignoreCase = true;
        const auto on = compare(left.lines(), right.lines(), options);
        QCOMPARE(on.blocks.size(), 3);
        QCOMPARE(on.blocks[0].change, Change::Ignored);
        QCOMPARE(on.blocks[1].change, Change::Equal);
        QCOMPARE(on.blocks[2].change, Change::Replace);
        QCOMPARE(on.rows.size(), 3);
        QCOMPARE(on.rows[0].change, Change::Ignored);
        QCOMPARE(on.rows[1].change, Change::Equal);
        QCOMPARE(on.rows[2].change, Change::Replace);
        QCOMPARE(on.differences.size(), 1);
        QCOMPARE(on.differences.first(), 2);
        QCOMPARE(on.ignoredBlocks, 1);
        // 被忽略的那一行必须**仍是一个块**，不许并进相邻的 Equal 块：
        // 并进去等于「视图再没有任何理由把它画出来」，标准 1 那句
        // 「仍在视图中有弱化提示」就落空了。
        QCOMPARE(on.blocks[0].leftCount, 1);
        QCOMPARE(on.blocks[0].rightCount, 1);
        QCOMPARE(on.blocks[0].rowCount, 1);
    }

    // 标准 3：统一规范化链。
    //
    // 这一条必须造一个「少做任何一步都不相等」的输入，否则它测不出链里掉了哪一步
    // ——那种实现下用例照样是绿的。语料于是刻意选成**同一行同时有大小写和空白变化**。
    void normalizationChainAppliesEveryEnabledRuleToBothSides()
    {
        auto left = decoded("Foo  Bar\n");   // 首字母大写 + 两个空格
        auto right = decoded("foo bar\n");   // 全小写 + 一个空格
        QVERIFY(left.lines().first().text != right.lines().first().text);

        CompareOptions options;
        // 只开大小写：空白那一步没做 → 仍是差异。
        options.ignoreCase = true;
        QCOMPARE(compare(left.lines(), right.lines(), options).differences.size(), 1);
        // 只开空白：大小写那一步没做 → 仍是差异。
        options.ignoreCase = false;
        options.whitespace = Whitespace::IgnoreChanges;
        QCOMPARE(compare(left.lines(), right.lines(), options).differences.size(), 1);
        // 两步都做 → 判等。
        options.ignoreCase = true;
        const auto both = compare(left.lines(), right.lines(), options);
        QVERIFY(both.differences.isEmpty());
        QCOMPARE(both.ignoredBlocks, 1);
        // 而且**两侧都过了整条链**：左右对调，结论必须一致。
        // 只对一侧应用规则的实现（例如只折叠右侧）会在这里露出来。
        const auto swapped = compare(right.lines(), left.lines(), options);
        QVERIFY(swapped.differences.isEmpty());
        QCOMPARE(swapped.ignoredBlocks, both.ignoredBlocks);
        QCOMPARE(swapped.rows.size(), both.rows.size());
        QCOMPARE(swapped.blocks.size(), both.blocks.size());
        // 链本身可以直接问：两步都生效时，规范化结果是「全小写 + 单空格」。
        QCOMPARE(normalizedLine(QStringLiteral("Foo  Bar"), options), QStringLiteral("foo bar"));
        // 规范化只是「判等用的键」，原文一个字都不动。
        QCOMPARE(left.lines().first().text, QStringLiteral("Foo  Bar"));
        QCOMPARE(right.lines().first().text, QStringLiteral("foo bar"));
    }

    // 标准 4 前半句：非 ASCII 按 Unicode 大小写规则处理。
    //
    // 表里每一行都点名一条规则；期望值是**按 Unicode simple case folding 的语义
    // 手推**出来的，不是把实现跑一遍抄回来的。两处故意写成 `false` 的是已知限制
    // 而不是 bug（Qt 5.15 只提供 simple folding，不做多字符展开），
    // 取舍的完整理由写在 `textdiff.h` 的 `normalizedLine()` 注释里。
    void nonAsciiFoldingFollowsUnicodeSimpleCaseFolding()
    {
        struct Sample { const char *left; const char *right; bool equal; const char *rule; };
        const Sample samples[] = {
            {"\xC3\x84\xC3\x96\xC3\x9C", "\xC3\xA4\xC3\xB6\xC3\xBC", true,
             "Latin-1 的 ÄÖÜ/äöü"},
            {"\xE1\xBA\x9E", "\xC3\x9F", true,
             "U+1E9E ẞ 折成 U+00DF ß"},
            // 注意 `\x9F` 后面紧跟着 `e` 会连成一个超范围的十六进制转义，
            // 所以字符串必须在这里断开——不是排版，是 C++ 的取最长匹配规则。
            {"STRASSE", "stra\xC3\x9F" "e", false,
             "simple folding 不展开 ß→ss"},
            {"\xEF\xAC\x81le", "FILE", false,
             "simple folding 不展开连字 ﬁ→fi"},
            {"\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3", "\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82", true,
             "词尾 sigma：Σ 与 ς 都折成 σ（换成小写化，这一条会红）"},
            {"\xCE\xA3\xCE\xA3\xCE\xA3", "\xCF\x82\xCF\x82\xCF\x82", true,
             "三种 sigma 互相等价"},
            {"\xE2\x84\xAA", "k", true,
             "U+212A KELVIN SIGN 折成 k"},
            {"\xC2\xB5", "\xCE\xBC", true,
             "U+00B5 MICRO SIGN 折成 U+03BC"},
            {"\xCE\x9C", "\xCE\xBC", true,
             "U+039C 折成 U+03BC"},
            {"\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2",
             "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", true,
             "西里尔文"},
            {"\xF0\x90\x90\x80", "\xF0\x90\x90\xA8", true,
             "非 BMP：Deseret U+10400 折成 U+10428"},
            {"\xE4\xB8\xAD\xE6\x96\x87", "\xE4\xB8\xAD\xE6\x96\x87", true,
             "无大小写概念的文字，折叠必须是恒等"},
        };
        for (const Sample &sample : samples) {
            const QString left = QString::fromUtf8(sample.left);
            const QString right = QString::fromUtf8(sample.right);
            CompareOptions options;
            options.ignoreCase = true;
            const bool equal = compare(QVector<Line>{{left, Eol::LF}},
                                       QVector<Line>{{right, Eol::LF}},
                                       options).differences.isEmpty();
            QVERIFY2(equal == sample.equal,
                     qPrintable(QStringLiteral("大小写折叠与 Unicode 规则不符（%1）：%2 / %3")
                                    .arg(QString::fromUtf8(sample.rule), left, right)));
            // 反向的一半：原文不同的那些对，**关掉**折叠时必须仍是差异。
            // 少了这一半，一个「永远返回无差异」的实现能让上面全部通过。
            if (left != right) {
                options.ignoreCase = false;
                QCOMPARE(compare(QVector<Line>{{left, Eol::LF}},
                                 QVector<Line>{{right, Eol::LF}},
                                 options).differences.size(), 1);
            }
        }
    }

    // 标准 4 后半句：土耳其语 i/İ 的取舍必须有明确结论并被记录。
    //
    // 这里钉的是 `textdiff.h` 里那张表：我们**不做**土耳其语折叠，于是
    // `I`/`i` 相同，而 `İ`/`i` 与 `ı`/`I` 都不同——与土耳其语正字法**恰好相反**。
    // 取舍是刻意的（理由：结论不许随机器 locale 变），把它写成用例是为了让
    // 「哪天有人觉得这是个 bug、顺手改成 locale-aware」当场变红，
    // 而不是在界面上表现为「同一对文件在两台机器上结果不同」。
    //
    // 第二段还顺带证明了一件事：结果与 `QLocale::setDefault()` 无关。
    // 今天 `toCaseFolded()` 通过得很轻松，但这条不变量的作用不是区分某个已知实现，
    // 而是把门关死——谁把 locale 引进判等，它就在这里红。
    void turkicCasePairsFollowTheRecordedTradeoffAndStayLocaleIndependent()
    {
        const QString capitalI = QStringLiteral("I");
        const QString lowerI = QStringLiteral("i");
        const QString dotless = QString::fromUtf8("\xC4\xB1");  // U+0131 ı
        const QString dotted = QString::fromUtf8("\xC4\xB0");   // U+0130 İ
        CompareOptions options;
        options.ignoreCase = true;
        auto equal = [&options](const QString &a, const QString &b) {
            return compare(QVector<Line>{{a, Eol::LF}}, QVector<Line>{{b, Eol::LF}}, options)
                .differences.isEmpty();
        };
        QVERIFY(equal(capitalI, lowerI));    // 与 locale 无关的折叠：相同
        QVERIFY(!equal(dotted, lowerI));     // 土耳其语里相同，本实现里不同
        QVERIFY(!equal(dotless, capitalI));  // 土耳其语里相同，本实现里不同
        QVERIFY(!equal(dotless, lowerI));    // 两边一致：不同

        // 上面四条在土耳其语 locale 下必须**原样成立**。
        // 守卫用 RAII：任何一条断言失败都会 `return`，析构仍然会跑，
        // 不会把土耳其语 locale 留给后面的用例。
        struct LocaleGuard {
            QLocale saved;
            ~LocaleGuard() { QLocale::setDefault(saved); }
        } guard{QLocale()};
        QLocale::setDefault(QLocale(QLocale::Turkish, QLocale::Turkey));
        QVERIFY(equal(capitalI, lowerI));
        QVERIFY(!equal(dotted, lowerI));
        QVERIFY(!equal(dotless, capitalI));
        QVERIFY(!equal(dotless, lowerI));
        QCOMPARE(normalizedLine(dotted, options), dotted);   // İ 折成它自己（simple folding）
        QCOMPARE(normalizedLine(dotless, options), dotless);
    }

    // 标准 4 的附带契约（记录在 `textdiff.h`）：折叠**长度守恒**。
    //
    // 抽成独立用例是因为它守的是另一件事：不是「折得对不对」，而是
    // 「折叠后还能不能把规范化下标直接当原文下标用」——TXT-025 的行内字符级高亮
    // 靠的就是这一点。哪天把 simple folding 换成 full folding（ß→ss、ﬁ→ffi），
    // 这条会红，提醒对方先去把偏移映射补上，而不是只改链本身。
    void normalizationPreservesLengthSoOffsetsStayAddressable()
    {
        CompareOptions options;
        options.ignoreCase = true;
        const QString samples[] = {
            QStringLiteral("plain text 123"),
            QString::fromUtf8("\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F"),
            QString::fromUtf8("\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3\xCF\x82\xCF\x83\xCE\xA3"),
            QString::fromUtf8("\xC4\xB0\xC4\xB1I"),
            QString::fromUtf8("\xF0\x90\x90\x80\xF0\x90\x90\xA8"),
            QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97"),
            QString::fromUtf8("\xE2\x84\xAA\xC2\xB5\xCE\x9C\xEF\xAC\x81"),
        };
        bool sawAFold = false;
        for (const QString &sample : samples) {
            const QString folded = normalizedLine(sample, options);
            QCOMPARE(folded.size(), sample.size());
            if (folded != sample) sawAFold = true;
        }
        // 保证上面那句不是在比「字符串等于它自己」：至少要有样本真的被折过。
        QVERIFY2(sawAFold, "样本集里没有一个被折叠，长度断言等于恒真");
        QVERIFY(normalizedLine(QString::fromUtf8("\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3"), options)
                != QString::fromUtf8("\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3"));
        QVERIFY(normalizedLine(QString::fromUtf8("\xF0\x90\x90\x80"), options)
                != QString::fromUtf8("\xF0\x90\x90\x80"));
    }

    // =========================================================================
    // TXT-009 忽略空白变化（issue #64）
    // =========================================================================

    // 标准 1 + 标准 2：两级语义必须在**同一份固定语料**上被严格区分开。
    //
    // 为什么非得有这张表：两个模式的实现都只有一行（`simplified()` 与「删掉全部空白」），
    // 而它们在**大多数**输入上给出同样的结论——随便抓几行语料测「开了就不算差异」，
    // 把两级写成一个也会全绿。真正分家的是「空白**有没有**」这一类（表里第 5~7 行）：
    // `IgnoreChanges` 判**不同**，`IgnoreAll` 才判相同。这张表就是按这条边界搭的。
    //
    // 表里的期望值是手推的（按规格的定义逐行算），不是跑一遍抄回来的。
    void whitespaceLevelsAreStrictlyDistinctOnAFixedCorpus()
    {
        struct Row {
            const char *left;
            const char *right;
            bool exact;
            bool changes;
            bool all;
            const char *rule;
        };
        const Row rows[] = {
            {"a b", "a b", true, true, true, "逐字符相同：三个模式都必须判等"},
            {"a  b", "a b", false, true, true, "内部连续空白数量不同"},
            {"a b c", "a  b   c", false, true, true, "多处连续空白数量不同"},
            {"  a b", "a b", false, true, true, "仅前导空白不同"},
            {"a b  ", "a b", false, true, true, "仅尾随空白不同"},
            {"  a b  ", "a b", false, true, true, "首尾都不同"},
            {"ab", "a b", false, false, true, "空白的**有无**不同：两级的分水岭"},
            {"a b", "ab", false, false, true, "同上，方向相反"},
            {"abc", "a b c", false, false, true, "空白有无不同（三处）"},
        };
        for (const Row &row : rows) {
            const QVector<Line> left{{QString::fromUtf8(row.left), Eol::LF}};
            const QVector<Line> right{{QString::fromUtf8(row.right), Eol::LF}};
            // 只有原文真的不同时，「判等」这件事才说明问题；相同时单独验一次三个模式。
            const bool rawEqual = left.first().text == right.first().text;
            const auto check = [&](Whitespace mode, bool expected) {
                CompareOptions options;
                options.whitespace = mode;
                const bool equal = compare(left, right, options).differences.isEmpty();
                QVERIFY2(equal == expected,
                         qPrintable(QStringLiteral("空白模式 %1 与规格定义不符（%2）：[%3] / [%4]")
                                        .arg(QString::fromLatin1(whitespaceIdentifier(mode)),
                                             QString::fromUtf8(row.rule),
                                             QString::fromUtf8(row.left),
                                             QString::fromUtf8(row.right))));
            };
            check(Whitespace::Exact, row.exact);
            check(Whitespace::IgnoreChanges, row.changes);
            check(Whitespace::IgnoreAll, row.all);
            // 结构不变量：三个模式是**层层放宽**的，判等关系必须嵌套
            // （Exact ⊆ IgnoreChanges ⊆ IgnoreAll）。加第四个模式时也要满足它。
            QVERIFY2(!(row.exact && !row.changes) && !(row.changes && !row.all),
                     qPrintable(QStringLiteral("三个模式的宽松程度没有嵌套：%1").arg(
                         QString::fromUtf8(row.rule))));
            if (rawEqual) QVERIFY(row.exact);
        }
    }

    // 标准 5：Tab 与空格混排的行也必须走同一套定义（有固定语料）。
    //
    // 单独成一条而不是并进上一张表，是因为这里除了 Tab/空格还钉住了
    // **「哪些字符算空白」**这个更基础的问题：`QChar::isSpace()` 的判定范围
    // 比「空格和 Tab」宽得多（实测 NBSP、表意空格、窄 NBSP、U+2028/2029 都算，
    // 零宽空格 U+200B 不算）。这些字符在从别的编辑器粘贴过来的行里真的会出现，
    // 而它们看不见——搞错了只表现为「某些行怎么也不算相同」，很难归因。
    void tabAndSpaceMixturesFollowTheSameTwoLevelDefinitions()
    {
        struct Row {
            const char *left;
            const char *right;
            bool changes;
            bool all;
            const char *rule;
        };
        const Row rows[] = {
            {"a\tb", "a b", true, true, "Tab 与空格：数量相同"},
            {"a\tb", "a  b", true, true, "Tab 与两个空格"},
            {"a\t\tb", "a  b", true, true, "两个 Tab 与两个空格"},
            {"a \t b", "a b", true, true, "空格与 Tab 交替出现"},
            {"\ttrimmed\t", " trimmed ", true, true, "用 Tab 做首尾空白"},
            {"a\tb", "ab", false, true, "Tab 的**有无**不同：只有 IgnoreAll 判等"},
            // 下限那一侧：非空白字符本身就不同，两个级别都不能判等。
            // 这一行是「忽略空白」的护栏——忽略空白**不能**凭空造出少掉的字符，
            // 否则一个把所有空白都删完再比前缀的实现也能骗过上面几行。
            {"a\tb", "a b c", false, false, "非空白字符本身不同：两级都不判等"},
            {"a\xC2\xA0" "b", "a b", true, true, "NBSP(U+00A0) 算空白（实测 isSpace 为真）"},
            {"a\xE3\x80\x80" "b", "a b", true, true, "表意空格 U+3000 算空白"},
            {"a\xE2\x80\x8B" "b", "a b", false, false, "零宽空格 U+200B **不算**空白"},
        };
        for (const Row &row : rows) {
            const QVector<Line> left{{QString::fromUtf8(row.left), Eol::LF}};
            const QVector<Line> right{{QString::fromUtf8(row.right), Eol::LF}};
            const auto check = [&](Whitespace mode, bool expected) {
                CompareOptions options;
                options.whitespace = mode;
                QVERIFY2(compare(left, right, options).differences.isEmpty() == expected,
                         qPrintable(QStringLiteral("空白模式 %1 在 Tab/空格混排语料上不符（%2）")
                                        .arg(QString::fromLatin1(whitespaceIdentifier(mode)),
                                             QString::fromUtf8(row.rule))));
            };
            check(Whitespace::IgnoreChanges, row.changes);
            check(Whitespace::IgnoreAll, row.all);
            // 这一组用的语料原文都不相同，所以关掉两级开关时**必须**是差异：
            // 少了这半边，一个「什么都不做就返回相等」的实现能让上面全部通过。
            CompareOptions exact;
            QCOMPARE(compare(left, right, exact).differences.size(), 1);
        }
    }

    // 标准 3：三个模式以**单一枚举**呈现、互斥，且枚举与「可选清单」只有一个来源。
    //
    // 这一条服务的其实是界面：下拉铺什么、读回什么，都必须从模式表推导。
    // 原来的写法是界面写死三行文案 + `static_cast<Whitespace>(currentIndex())`，
    // 也就是**按序号**对应——往枚举中间插一个取值、或调换两条文案，
    // 「忽略全部空白」会静默变成别的模式，没有任何东西会红。
    void whitespaceModesAreASingleExclusiveEnumBackedByOneTable()
    {
        // 出厂值必须就是表里的默认项，否则「比较空白」这个名字与它实际的行为会分家。
        CompareOptions defaults;
        QCOMPARE(defaults.whitespace, defaultWhitespace());
        QCOMPARE(defaultWhitespace(), Whitespace::Exact);

        const QVector<Whitespace> modes = availableWhitespaces();
        QCOMPARE(modes.size(), 3);
        QCOMPARE(modes[0], Whitespace::Exact);
        QCOMPARE(modes[1], Whitespace::IgnoreChanges);
        QCOMPARE(modes[2], Whitespace::IgnoreAll);
        // 互斥：同一条目不能出现两次（枚举值唯一），标识符也不能重名。
        QCOMPARE(QSet<int>({static_cast<int>(modes[0]), static_cast<int>(modes[1]),
                            static_cast<int>(modes[2])}).size(), 3);
        for (Whitespace mode : modes) {
            const char *identity = whitespaceIdentifier(mode);
            QVERIFY(identity != nullptr);
            QVERIFY(*identity != '\0');
        }
        QCOMPARE(QString::fromLatin1(whitespaceIdentifier(Whitespace::Exact)), QStringLiteral("exact"));
        QCOMPARE(QString::fromLatin1(whitespaceIdentifier(Whitespace::IgnoreChanges)), QStringLiteral("changes"));
        QCOMPARE(QString::fromLatin1(whitespaceIdentifier(Whitespace::IgnoreAll)), QStringLiteral("all"));
        // 表外的取值不编名字——编出来的标识符会让人去搜一个不存在的符号。
        QVERIFY(whitespaceIdentifier(static_cast<Whitespace>(99)) == nullptr);
        QVERIFY(validateWhitespaceTable(whitespaceTable()).isEmpty());

        // 自检必须能自证会报错：拿四份**故意写坏**的表跑同一个判定。
        // 只断言「长度大于 0」不够——一个把所有输入都判成有问题的实现同样能过，
        // 所以每处都断言**问题的内容**。
        const auto problemsOf = [](const QVector<WhitespaceDescriptor> &broken) {
            return validateWhitespaceTable(broken).join(QLatin1Char('\n'));
        };
        // 少一个模式：只有「规格点名了但表里没有」这条规则会响，所以它必须响。
        QVERIFY(problemsOf({{Whitespace::Exact, "exact", true},
                            {Whitespace::IgnoreChanges, "changes", true}})
                    .contains(QStringLiteral("规格点名的空白模式")));
        // 同一个模式登记两次：界面会多出一行同义项，而读回时只有一个能生效。
        QVERIFY(problemsOf({{Whitespace::Exact, "exact", true},
                            {Whitespace::Exact, "exact2", true},
                            {Whitespace::IgnoreChanges, "changes", true},
                            {Whitespace::IgnoreAll, "all", true}})
                    .contains(QStringLiteral("出现了不止一次")));
        // 标识符为空 / 重复：日志里分不出是哪一个。
        QVERIFY(problemsOf({{Whitespace::Exact, "", true},
                            {Whitespace::IgnoreChanges, "changes", true},
                            {Whitespace::IgnoreAll, "all", true}})
                    .contains(QStringLiteral("没有标识符")));
        QVERIFY(problemsOf({{Whitespace::Exact, "same", true},
                            {Whitespace::IgnoreChanges, "same", true},
                            {Whitespace::IgnoreAll, "all", true}})
                    .contains(QStringLiteral("重复")));
        // 规格点名的模式被标成「未实现」：这是把一次缺失变成一次静默的降级。
        QVERIFY(problemsOf({{Whitespace::Exact, "exact", true},
                            {Whitespace::IgnoreChanges, "changes", true},
                            {Whitespace::IgnoreAll, "all", false}})
                    .contains(QStringLiteral("规格被静默降级")));
        // 空表：一条可选模式都没有。
        QVERIFY(problemsOf({}).contains(QStringLiteral("表为空")));

        // 枚举取到表外时的行为：**退化成 Exact，不做兜底、不改写成默认模式**。
        // 方向是刻意选的（理由写在 `normalizedLine()` 的注释里）：Exact 只会多报差异，
        // 不会把差异藏掉；而 Alignment 那处必须兜底，因为「什么都不跑」会返回
        // 零个块、也就是「两份文件完全一样」。
        CompareOptions outOfRange;
        outOfRange.whitespace = static_cast<Whitespace>(99);
        CompareOptions exact;
        CompareOptions changes;
        changes.whitespace = Whitespace::IgnoreChanges;
        changes.ignoreCase = outOfRange.ignoreCase = exact.ignoreCase = false;
        auto left = decoded("a  b\n");
        auto right = decoded("a b\n");
        const int outOfRangeDifferences = compare(left.lines(), right.lines(), outOfRange).differences.size();
        QCOMPARE(outOfRangeDifferences, compare(left.lines(), right.lines(), exact).differences.size());
        // 而且它**不是**「退化成两级忽略」——那样会把这个差异藏掉。
        QCOMPARE(compare(left.lines(), right.lines(), changes).differences.size(), 0);
        QCOMPARE(outOfRangeDifferences, 1);
    }
};
QTEST_APPLESS_MAIN(TextTests)
#include "tst_text.moc"
