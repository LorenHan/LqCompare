#include <QtTest>
#include <QFile>
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
};
QTEST_APPLESS_MAIN(TextTests)
#include "tst_text.moc"
