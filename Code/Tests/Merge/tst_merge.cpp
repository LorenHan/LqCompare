#include <QtTest>
#include <QElapsedTimer>
#include <algorithm>

#include "mergeengine.h"
#include "textdiff.h"

using namespace LqCompare;

namespace {
QVector<Text::Line> lines(const QString &text)
{
    return Text::Document::splitLines(text);
}

QString content(const QVector<Text::Line> &value)
{
    QString result;
    for (const auto &line : value) result += line.text + Text::eolText(line.eol);
    return result;
}

QString kinds(const Merge::Result &result)
{
    QString value;
    for (const auto &block : result.blocks) {
        switch (block.kind) {
        case Merge::Kind::Unchanged: value += 'U'; break;
        case Merge::Kind::LeftOnly: value += 'L'; break;
        case Merge::Kind::RightOnly: value += 'R'; break;
        case Merge::Kind::SameChange: value += 'S'; break;
        case Merge::Kind::Conflict: value += 'C'; break;
        }
    }
    return value;
}

// Every source line must appear exactly once, in order, with honest ranges.
// This also checks that automatic decisions never hide an unmodified side.
bool hasCompleteSources(const Merge::Result &result, const QVector<Text::Line> &base,
                        const QVector<Text::Line> &left, const QVector<Text::Line> &right,
                        bool hasBase = true)
{
    int b = 0, l = 0, r = 0;
    QVector<Text::Line> allBase, allLeft, allRight;
    for (const auto &block : result.blocks) {
        if (block.baseStart != b || block.leftStart != l || block.rightStart != r
            || block.baseCount != block.base.size() || block.leftCount != block.left.size()
            || block.rightCount != block.right.size()
            || block.base != base.mid(b, block.baseCount)
            || block.left != left.mid(l, block.leftCount)
            || block.right != right.mid(r, block.rightCount)) return false;
        switch (block.kind) {
        case Merge::Kind::Unchanged:
            if (block.left != block.right || block.output != block.left
                || (hasBase && block.base != block.left)) return false;
            break;
        case Merge::Kind::LeftOnly:
            if (block.right != block.base || block.output != block.left) return false;
            break;
        case Merge::Kind::RightOnly:
            if (block.left != block.base || block.output != block.right) return false;
            break;
        case Merge::Kind::SameChange:
            if (block.left != block.right || block.output != block.left) return false;
            break;
        case Merge::Kind::Conflict:
            if (block.left == block.right || block.output != block.base) return false;
            break;
        }
        allBase += block.base;
        allLeft += block.left;
        allRight += block.right;
        b += block.baseCount;
        l += block.leftCount;
        r += block.rightCount;
    }
    const auto output = result.outputLines();
    for (int i = 0; i + 1 < output.size(); ++i)
        if (output[i].eol == Text::Eol::None) return false;
    return allBase == base && allLeft == left && allRight == right;
}
}

class MergeTests : public QObject {
    Q_OBJECT
private slots:
    void corpus_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::addColumn<QString>("output");
        QTest::addColumn<QString>("ownership");
        auto row = [](const char *name, const char *base, const char *left, const char *right,
                      const char *output, const char *ownership) {
            QTest::newRow(name) << QString::fromUtf8(base) << QString::fromUtf8(left)
                << QString::fromUtf8(right) << QString::fromUtf8(output) << QString::fromLatin1(ownership);
        };
        row("empty", "", "", "", "", "");
        row("unchanged", "A\nB\n", "A\nB\n", "A\nB\n", "A\nB\n", "U");
        row("left-only", "A\nB\nC\n", "A\nL\nC\n", "A\nB\nC\n", "A\nL\nC\n", "ULU");
        row("right-only", "A\nB\nC\n", "A\nB\nC\n", "A\nR\nC\n", "A\nR\nC\n", "URU");
        row("same-replacement", "A\nB\nC\n", "A\nX\nC\n", "A\nX\nC\n", "A\nX\nC\n", "USU");
        row("different-replacement", "A\nB\nC\n", "A\nL\nC\n", "A\nR\nC\n", "A\nB\nC\n", "UCU");
        row("all-conflict", "B\n", "L\n", "R\n", "B\n", "C");
        row("adjacent-replacements", "A\nB\n", "L\nB\n", "A\nR\n", "L\nR\n", "LR");
        row("separated-replacements", "A\nB\nC\n", "L\nB\nC\n", "A\nB\nR\n", "L\nB\nR\n", "LUR");
        row("left-insertion", "A\n", "L\nA\n", "A\n", "L\nA\n", "LU");
        row("same-insertion", "A\n", "X\nA\n", "X\nA\n", "X\nA\n", "SU");
        row("different-insertion", "A\n", "L\nA\n", "R\nA\n", "A\n", "CU");
        row("insert-before-replacement", "A\nB\n", "L\nB\n", "R\nA\nB\n", "R\nL\nB\n", "RLU");
        row("insert-after-replacement", "A\nB\n", "L\nB\n", "A\nR\nB\n", "L\nR\nB\n", "LRU");
        row("insert-inside-replacement", "A\nB\nC\n", "L\nC\n", "A\nR\nB\nC\n", "A\nB\nC\n", "CU");
        row("adjacent-deletions", "A\nB\nC\n", "B\nC\n", "A\nC\n", "C\n", "LRU");
        row("same-deletion", "A\nB\nC\n", "A\nC\n", "A\nC\n", "A\nC\n", "USU");
        row("delete-versus-replace", "A\nB\nC\n", "A\nC\n", "A\nR\nC\n", "A\nB\nC\n", "UCU");
        row("insert-at-deletion-start", "A\nB\n", "B\n", "R\nA\nB\n", "R\nB\n", "RLU");
        row("insert-at-deletion-end", "A\nB\n", "B\n", "A\nR\nB\n", "R\nB\n", "LRU");
        row("insert-at-eof", "A\n", "L\n", "A\nR\n", "L\nR\n", "LR");
        row("empty-base-same", "", "X", "X", "X", "S");
        row("empty-base-conflict", "", "L", "R", "", "C");
        row("empty-base-one-side", "", "", "R", "R", "R");
        row("no-final-newline", "A\nB", "L\nB", "A\nR", "L\nR", "LR");
        row("final-newline-is-edit", "A", "A\n", "R", "A", "C");
        row("unterminated-replacement-then-append", "A\nB\n", "A", "A\nB\nR\n", "A\nB\n", "C");
        row("unterminated-right-then-append", "A\nB\n", "A\nB\nL\n", "A", "A\nB\n", "C");
        row("remove-final-newline-versus-append", "A\n", "A", "A\nR\n", "A\n", "C");
        row("unterminated-candidate-then-append", "A\nB\n", "L", "R\nB\nextra\n", "A\nB\n", "C");
        row("unterminated-right-candidate-then-append", "A\nB\n", "L\nB\nextra\n", "R", "A\nB\n", "C");
        row("line-ending-is-edit", "A\r\nB\r\n", "A\nB\r\n", "R\r\nB\r\n", "A\r\nB\r\n", "CU");
        row("same-line-ending-edit", "A\r\n", "A\n", "A\n", "A\n", "S");
        row("separate-mixed-endings", "A\r\nB\rC", "L\r\nB\rC", "A\r\nB\rR", "L\r\nB\rR", "LUR");
        row("whitespace-not-ignored", "A\n", " A\n", "A \n", "A\n", "C");
        row("case-not-ignored", "Ab\n", "ab\n", "AB\n", "Ab\n", "C");
        row("repeated-lines", "A\nX\nA\nY\nA\n", "A\nL\nA\nY\nA\n", "A\nX\nA\nR\nA\n", "A\nL\nA\nR\nA\n", "ULURU");
        row("overlap-transitivity", "A\nB\nC\nD\nE\nF\n", "X\nE\nF\n", "A\nY\nC\nZ\nF\n", "A\nB\nC\nD\nE\nF\n", "CU");
    }

    void corpus()
    {
        QFETCH(QString, base); QFETCH(QString, left); QFETCH(QString, right);
        QFETCH(QString, output); QFETCH(QString, ownership);
        const auto b = lines(base), l = lines(left), r = lines(right);
        const auto result = Merge::merge(b, l, r);
        QVERIFY(hasCompleteSources(result, b, l, r));
        QCOMPARE(content(result.outputLines()), output);
        QCOMPARE(kinds(result), ownership);
        QCOMPARE(result.conflictCount(), ownership.count('C'));
        QVERIFY(!result.alignmentLimited);
        const auto swapped = Merge::merge(b, r, l);
        QVERIFY(hasCompleteSources(swapped, b, r, l));
        QCOMPARE(swapped.conflictCount(), result.conflictCount());
        QVERIFY(swapped.outputLines() == result.outputLines());
        QCOMPARE(swapped.blocks.size(), result.blocks.size());
        for (int i = 0; i < result.blocks.size(); ++i) {
            QVERIFY(swapped.blocks[i].left == result.blocks[i].right);
            QVERIFY(swapped.blocks[i].right == result.blocks[i].left);
            QVERIFY(swapped.blocks[i].base == result.blocks[i].base);
        }
    }

    void sourceRangesAroundInsertionsAndConflicts()
    {
        const auto result = Merge::merge(lines("A\nB\nC\nD\n"),
                                        lines("L\nA\nB\nX\nD\n"),
                                        lines("A\nB\nR\nD\nZ\n"));
        QCOMPARE(kinds(result), QString("LUCUR"));
        const auto &conflict = result.blocks[2];
        QCOMPARE(conflict.baseStart, 2);
        QCOMPARE(conflict.baseCount, 1);
        QCOMPARE(conflict.leftStart, 3);
        QCOMPARE(conflict.leftCount, 1);
        QCOMPARE(conflict.rightStart, 2);
        QCOMPARE(conflict.rightCount, 1);
        QCOMPARE(content(conflict.base), QString("C\n"));
        QCOMPARE(content(conflict.left), QString("X\n"));
        QCOMPARE(content(conflict.right), QString("R\n"));
        const auto &tail = result.blocks.last();
        QCOMPARE(tail.baseStart, 4);
        QCOMPARE(tail.baseCount, 0);
        QCOMPARE(tail.leftStart, 5);
        QCOMPARE(tail.leftCount, 0);
        QCOMPARE(tail.rightStart, 4);
        QCOMPARE(tail.rightCount, 1);
    }

    void twoWayWithoutAncestor()
    {
        const auto left = lines("shared\nL\nend\n"), right = lines("shared\nR\nend\nextra");
        const auto result = Merge::mergeWithoutBase(left, right);
        QVERIFY(hasCompleteSources(result, {}, left, right, false));
        QCOMPARE(kinds(result), QString("UCUC"));
        QCOMPARE(result.conflictCount(), 2);
        QCOMPARE(content(result.outputLines()), QString("shared\nend\n"));
        const auto same = Merge::mergeWithoutBase(left, left);
        QCOMPARE(same.conflictCount(), 0);
        QVERIFY(same.outputLines() == left);
        const auto ending = Merge::mergeWithoutBase(lines("A\r\n"), lines("A\n"));
        QCOMPARE(ending.conflictCount(), 1);
        QCOMPARE(Merge::mergeWithoutBase({}, {}).blocks.size(), 0);
    }

    void unterminatedBoundaryPreservesCandidates()
    {
        const auto base = lines("A\nB\n");
        const auto left = lines("A");
        const auto right = lines("A\nB\nR\n");
        const auto result = Merge::merge(base, left, right);
        QVERIFY(hasCompleteSources(result, base, left, right));
        QCOMPARE(result.conflictCount(), 1);
        QCOMPARE(result.blocks.size(), 1);
        const auto &block = result.blocks.first();
        QCOMPARE(block.baseStart, 0);
        QCOMPARE(block.baseCount, 2);
        QCOMPARE(block.leftStart, 0);
        QCOMPARE(block.leftCount, 1);
        QCOMPARE(block.rightStart, 0);
        QCOMPARE(block.rightCount, 3);
        QVERIFY(block.left == left);
        QVERIFY(block.right == right);
        QCOMPARE(block.left.last().eol, Text::Eol::None);
        QVERIFY(result.outputLines() == base);
    }

    void exhaustiveShortRepeatedDocuments()
    {
        QVector<QVector<Text::Line>> corpus = {{}};
        for (int length = 1; length <= 3; ++length) {
            for (int bits = 0; bits < (1 << length); ++bits) {
                QString text;
                for (int i = 0; i < length; ++i) text += (bits & (1 << i)) ? "A\n" : "B\n";
                corpus += lines(text);
                text.chop(1);
                corpus += lines(text);
            }
        }
        QCOMPARE(corpus.size(), 29);
        for (const auto &base : corpus) {
            for (const auto &left : corpus) {
                const auto onlyLeft = Merge::merge(base, left, base);
                QVERIFY(onlyLeft.outputLines() == left);
                QCOMPARE(onlyLeft.conflictCount(), 0);
                const auto same = Merge::merge(base, left, left);
                QVERIFY(same.outputLines() == left);
                QCOMPARE(same.conflictCount(), 0);
                for (const auto &right : corpus) {
                    const auto result = Merge::merge(base, left, right);
                    QVERIFY2(hasCompleteSources(result, base, left, right),
                             qPrintable(QString("Source partition failed: base=%1 left=%2 right=%3")
                                        .arg(content(base), content(left), content(right))));
                    const auto swapped = Merge::merge(base, right, left);
                    QCOMPARE(result.conflictCount(), swapped.conflictCount());
                    QVERIFY(result.outputLines() == swapped.outputLines());
                    QCOMPARE(result.blocks.size(), swapped.blocks.size());
                    for (int i = 0; i < result.blocks.size(); ++i) {
                        QVERIFY(result.blocks[i].left == swapped.blocks[i].right);
                        QVERIFY(result.blocks[i].right == swapped.blocks[i].left);
                        QVERIFY(result.blocks[i].base == swapped.blocks[i].base);
                    }
                }
            }
        }
    }

    void tenThousandLinesAndBoundedFallback()
    {
        QVector<Text::Line> base;
        for (int i = 0; i < 10000; ++i) base += Text::Line{QString::number(i), Text::Eol::LF};
        auto left = base, right = base, expected = base;
        for (int i = 0; i < base.size(); i += 100) {
            left[i].text = expected[i].text = "left-" + QString::number(i);
            right[i + 1].text = expected[i + 1].text = "right-" + QString::number(i);
        }
        QElapsedTimer timer;
        timer.start();
        const auto result = Merge::merge(base, left, right);
        QVERIFY(hasCompleteSources(result, base, left, right));
        QVERIFY(result.outputLines() == expected);
        QCOMPARE(result.conflictCount(), 0);
        QVERIFY(!result.alignmentLimited);

        std::reverse(left.begin(), left.end());
        const auto bounded = Merge::merge(base, left, base);
        QVERIFY(bounded.alignmentLimited);
        QVERIFY(hasCompleteSources(bounded, base, left, base));
        QVERIFY(bounded.outputLines() == left);
        QCOMPARE(bounded.conflictCount(), 0);
        QVERIFY2(timer.elapsed() < 60000, "10,000-line input exceeded the suite's time budget");
    }
};

QTEST_GUILESS_MAIN(MergeTests)
#include "tst_merge.moc"
