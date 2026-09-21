// Patience 对齐与 Myers 回退的验收测试（PRD: TXT-003 / issue #58）。
//
// 分四组，与 issue 的四条完成标准一一对应：
//
//   A 组（标准 1）两侧都只出现一次的行优先于重复行
//   B 组（标准 2）不存在唯一行时平滑回退到 Myers，而不是退化为零匹配
//   C 组（标准 3）引擎两种都会算；表里标成「未实现」的算法不得出现在可选清单里
//   D 组（标准 4）固定语料上切换算法会改变块的数量与位置；且两种算法共用同一个
//                  `Result` 契约（边界条款：「Patience 与 Myers 必须产出同一接口的
//                  差异块，切换算法不改变视图契约」）
//
// 边界条款里那句「算法选择控件与可用算法清单由 TXT-004 负责」意味着本套件**不测**
// 下拉、不测设置键、不测持久化——那些东西在界面上，而这里连 QtGui 都没链接。
// 本套件测的是「引擎确实会算两种、并且只声明自己会算的那两种」。
#include <QtTest>

#include <QElapsedTimer>
#include <QVector>

#include "textdiff.h"

using namespace LqCompare::Text;

namespace {

// 用 `Document::decode` 造夹具，而不是手工拼 `QVector<Line>`：这样连行尾也走真实路径，
// 而「行尾算不算差异」正好是 `keys()` 里最容易搞错的一环（本套件的每个夹具都让
// **两侧**以换行结尾，于是每一行的 Eol 都是 LF，键里那一节不会把两侧错开——
// 若某一侧漏了结尾换行，末行的 Eol 会变成 None，两侧本来一样的一行就不再配对，
// 而现象是「唯一行找不到了」，看起来像算法坏了）。
QVector<Line> linesOf(const char *text)
{
    Document document;
    if (!Document::decode(QByteArray(text), &document)) qFatal("夹具无法解码");
    return document.lines();
}

// 差异块的类别名。用名字而不是 `int(change)`：快照串是给人读的，
// 一旦枚举顺序被调整，写数字的黄金串会变成一串看不懂的错位数字。
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

// 把 `Result` 的每一个字段摊成文本（与 Tests/Text 的同名辅助函数同一个理由：
// 逐字段比才管得住「没被列出来的字段」，失败时还能直接贴出两边全文）。
// 刻意与 Tests/Text 各留一份而不是抽到公共 Support 里：两份都只服务各自套件的断言，
// 抽出去就要多一个被两个套件同时依赖的对象，而它一旦变化，
// 「红的是哪个套件」这件事会立刻变模糊。
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

// 被判定为「两侧都有内容」的配对（左侧行号, 右侧行号）。
// 刻意**只收 `Equal`**：`Ignored` 也是配上了，但它表示「原文不同、按选项等价」，
// 把它算进「配对数」会让 A 组那条「重复行没有被配对」的断言变得没有意义。
QVector<QPair<int, int>> matchedPairs(const Result &result)
{
    QVector<QPair<int, int>> pairs;
    for (const Block &block : result.blocks) {
        if (block.change != Change::Equal) continue;
        for (int i = 0; i < block.leftCount && i < block.rightCount; ++i)
            pairs.append({block.leftStart + i, block.rightStart + i});
    }
    return pairs;
}

bool containsPair(const QVector<QPair<int, int>> &pairs, int left, int right)
{
    for (const QPair<int, int> &pair : pairs)
        if (pair.first == left && pair.second == right) return true;
    return false;
}

// 把可选算法清单摊成标识符串再比，而不是直接 `QCOMPARE` 两个 `QVector<Alignment>`。
// 理由不是省事：`QCOMPARE` 失败时要把两侧打印成人能读的东西，而枚举向量的默认打印
// 是一串 `<unknown>`——「清单里多了一项」与「顺序反了」这两件事就分不出来了，
// 而本套件里这两件事的后果完全不同（前者是算法没实现，后者是默认值会漂移）。
QString alignmentListText(const QVector<Alignment> &list)
{
    QStringList parts;
    for (Alignment alignment : list) parts << QString::fromLatin1(alignmentIdentifier(alignment));
    return parts.join(QLatin1Char(','));
}

// 「两种算法必须产出同一接口的差异块」这句边界条款的可执行形式。
// 返回第一条被违反的不变量（空串表示全部成立）。
//
// 为什么写成一个返回原因的纯函数而不是一串 `QVERIFY`：失败时要能一眼看出
// 是**哪一条**不变量在哪种算法上破了。一串 QVERIFY 只会告诉你「第 7 行断言失败」，
// 而同一个断言在 6 组夹具 × 2 种算法里被跑 12 次，定位全靠数。
QString contractProblem(const Result &result, const QVector<Line> &left, const QVector<Line> &right)
{
    int leftCursor = 0, rightCursor = 0, rowCursor = 0;
    QVector<int> expectedDifferences;
    int expectedIgnored = 0;
    for (int index = 0; index < result.blocks.size(); ++index) {
        const Block &block = result.blocks[index];
        if (block.leftStart != leftCursor)
            return QStringLiteral("块 %1 的 leftStart=%2，应为 %3").arg(index)
                       .arg(block.leftStart).arg(leftCursor);
        if (block.rightStart != rightCursor)
            return QStringLiteral("块 %1 的 rightStart=%2，应为 %3").arg(index)
                       .arg(block.rightStart).arg(rightCursor);
        if (block.firstRow != rowCursor)
            return QStringLiteral("块 %1 的 firstRow=%2，应为 %3").arg(index)
                       .arg(block.firstRow).arg(rowCursor);
        if (block.rowCount != qMax(block.leftCount, block.rightCount))
            return QStringLiteral("块 %1 的 rowCount=%2，与两侧计数 %3/%4 不符").arg(index)
                       .arg(block.rowCount).arg(block.leftCount).arg(block.rightCount);
        if (block.change == Change::Ignored) ++expectedIgnored;
        else if (block.change != Change::Equal) expectedDifferences.append(index);
        if (block.change == Change::Equal && block.leftCount != block.rightCount)
            return QStringLiteral("块 %1 标成 Equal 却两侧计数不等（%2/%3）").arg(index)
                       .arg(block.leftCount).arg(block.rightCount);
        for (int i = 0; i < block.rowCount; ++i) {
            if (rowCursor + i >= result.rows.size())
                return QStringLiteral("块 %1 需要 %2 行，但 rows 只有 %3").arg(index)
                           .arg(block.rowCount).arg(result.rows.size());
            const Row &row = result.rows[rowCursor + i];
            const int expectedLeft = i < block.leftCount ? block.leftStart + i : -1;
            const int expectedRight = i < block.rightCount ? block.rightStart + i : -1;
            if (row.leftLine != expectedLeft || row.rightLine != expectedRight)
                return QStringLiteral("行 %1 的配对是 (%2,%3)，应为 (%4,%5)").arg(rowCursor + i)
                           .arg(row.leftLine).arg(row.rightLine).arg(expectedLeft).arg(expectedRight);
            if (row.block != index || row.change != block.change)
                return QStringLiteral("行 %1 未与所在块的序号/类别一致").arg(rowCursor + i);
        }
        // Equal 块必须真的逐行相同——这是「不许把没比过的两段说成相同」的落点。
        if (block.change == Change::Equal) {
            for (int i = 0; i < block.leftCount; ++i)
                if (!(left[block.leftStart + i] == right[block.rightStart + i]))
                    return QStringLiteral("块 %1 标成 Equal，但第 %2 行原文不同").arg(index).arg(i);
        }
        leftCursor += block.leftCount;
        rightCursor += block.rightCount;
        rowCursor += block.rowCount;
    }
    if (leftCursor != left.size())
        return QStringLiteral("左侧只覆盖了 %1 行，共 %2 行").arg(leftCursor).arg(left.size());
    if (rightCursor != right.size())
        return QStringLiteral("右侧只覆盖了 %1 行，共 %2 行").arg(rightCursor).arg(right.size());
    if (rowCursor != result.rows.size())
        return QStringLiteral("行覆盖到 %1，rows 却有 %2 行").arg(rowCursor).arg(result.rows.size());
    if (result.differences != expectedDifferences)
        return QStringLiteral("differences 与实际非相同块不符");
    if (result.ignoredBlocks != expectedIgnored)
        return QStringLiteral("ignoredBlocks=%1，实际 Ignored 块 %2 个")
                   .arg(result.ignoredBlocks).arg(expectedIgnored);
    return QString();
}

Result aligned(const QVector<Line> &left, const QVector<Line> &right, Alignment alignment)
{
    CompareOptions options;
    options.alignment = alignment;
    return compare(left, right, options);
}

}

class AlignmentTests : public QObject {
    Q_OBJECT
private slots:
    // ── A 组：标准 1 ──────────────────────────────────────────────────────
    //
    // 「优先匹配两侧都只出现一次的行」在只有一个唯一行时最容易看出取舍：
    // `u` 在两侧各出现一次、`a` 在两侧各出现两次。Patience 的锚点是 `u`，
    // 于是它配 `u`、把两组 `a` 判成一删一插；Myers 求最短编辑脚本，
    // 它更愿意配那两个 `a`（配对数 2 > 1），`u` 反而两边都没配上。
    //
    // 两个断言缺一不可：只断言「配上了 u」的话，一个「把该配的都配上」的
    // 全配实现也能过；只断言「配对数 1」的话，一个「什么都不配」的实现也能过。
    void uniqueLinesWinOverRepeats()
    {
        const auto left = linesOf("a\na\nu\n");
        const auto right = linesOf("u\na\na\n");
        const auto patience = aligned(left, right, Alignment::Patience);
        const auto myers = aligned(left, right, Alignment::Myers);

        const auto pairs = matchedPairs(patience);
        QCOMPARE(pairs.size(), 1);
        QVERIFY2(containsPair(pairs, 2, 0), "唯一行 u（左 2 / 右 0）必须被配上");
        // 手推：唯一行 u 成为锚点，它前面的两个 a 全删、后面的两个 a 全插。
        QCOMPARE(fingerprint(patience), QString::fromUtf8(
            "blocks=3 differences=2 ignored=0 limited=0 rows=5\n"
            "b0 Delete L0+2 R0+0 rows0+2\n"
            "b1 Equal L2+1 R0+1 rows2+1\n"
            "b2 Insert L3+0 R1+2 rows3+2\n"
            "r0 l0 r-1 b0 Delete\n"
            "r1 l1 r-1 b0 Delete\n"
            "r2 l2 r0 b1 Equal\n"
            "r3 l-1 r1 b2 Insert\n"
            "r4 l-1 r2 b2 Insert\n"
            "diffs=[0,2]"));

        // 反面对照：同一份输入下 Myers 配的是两个 a，且**没有**配 u。
        // 这条断言让「Patience 与 Myers 不是同一个东西」有了证据——
        // 否则一个「两个分支都调同一个算法」的实现能让上面每一条都通过。
        const auto myersPairs = matchedPairs(myers);
        QCOMPARE(myersPairs.size(), 2);
        QVERIFY2(!containsPair(myersPairs, 2, 0), "Myers 在最短编辑脚本下不该配这个唯一行");
        QVERIFY(fingerprint(myers) != fingerprint(patience));
    }

    // 标准 1 的另一半：唯一行**不止一条**、且它们在两侧的出现顺序一致时，
    // 每一条都要被配上（这才是「优先」二字的完整含义——不是碰巧配上一个）。
    //
    // 夹具里 `a` 与 `c` 各出现两次（重复行），`b` 与 `d` 各出现一次（唯一行），
    // 且唯一行的相对顺序在两侧一致（b 在 d 之前），因此两条都该进锚点集合。
    // 左侧第 0 行的 a 与第 5 行的 c 是「多出来的那一份」，应当留在差异里。
    void orderConsistentUniqueLinesAreAllAnchored()
    {
        const auto left = linesOf("a\nb\na\nc\nd\nc\n");
        const auto right = linesOf("b\na\nc\na\nc\nd\n");
        const auto patience = aligned(left, right, Alignment::Patience);
        const auto pairs = matchedPairs(patience);
        QVERIFY2(containsPair(pairs, 1, 0), "唯一行 b（左 1 / 右 0）必须被配上");
        QVERIFY2(containsPair(pairs, 4, 5), "唯一行 d（左 4 / 右 5）必须被配上");
        QCOMPARE(pairs.size(), 4);
        // 手推：两个锚点把它切成「删 a / 配对 b,a,c / 插 a,c / 配对 d / 删 c」。
        QCOMPARE(fingerprint(patience), QString::fromUtf8(
            "blocks=5 differences=3 ignored=0 limited=0 rows=8\n"
            "b0 Delete L0+1 R0+0 rows0+1\n"
            "b1 Equal L1+3 R0+3 rows1+3\n"
            "b2 Insert L4+0 R3+2 rows4+2\n"
            "b3 Equal L4+1 R5+1 rows6+1\n"
            "b4 Delete L5+1 R6+0 rows7+1\n"
            "r0 l0 r-1 b0 Delete\n"
            "r1 l1 r0 b1 Equal\n"
            "r2 l2 r1 b1 Equal\n"
            "r3 l3 r2 b1 Equal\n"
            "r4 l-1 r3 b2 Insert\n"
            "r5 l-1 r4 b2 Insert\n"
            "r6 l4 r5 b3 Equal\n"
            "r7 l5 r-1 b4 Delete\n"
            "diffs=[0,2,4]"));
    }

    // ── B 组：标准 2 ──────────────────────────────────────────────────────
    //
    // 「不存在唯一行时平滑回退到 Myers，而不是退化为零匹配」。
    //
    // 断言写成「与 Myers 的结果**逐字段相同**」而不是「配对数大于 0」：
    // 后者太松——一个「找不到唯一行就随便配最长的公共前后缀」的实现同样能让
    // 配对数大于 0，而它并不是回退到 Myers。「逐字段相同」是唯一能证明
    // 「真的走了 Myers」的断言，因为 Myers 的输出（块边界与类型）是它的指纹。
    void withoutUniqueLinesFallsBackToMyers()
    {
        const auto left = linesOf("a\na\nb\nb\n");
        const auto right = linesOf("b\nb\na\na\n");
        const auto patience = aligned(left, right, Alignment::Patience);
        const auto myers = aligned(left, right, Alignment::Myers);
        // 先自证这份夹具确实没有唯一行：a 与 b 在两侧都各出现两次。
        // 不写这一条的话，夹具被改成「其实有唯一行」时，本条会退化成
        // 「在某份别的输入上比较两种算法」——它仍然会绿，但守的东西没了。
        QCOMPARE(matchedPairs(myers).size(), 2);
        QCOMPARE(fingerprint(patience), fingerprint(myers));
        QCOMPARE(matchedPairs(patience).size(), 2);
        QVERIFY(!patience.alignmentLimited);
    }

    // 同一件事的规模放大版：一份全是重复行的大输入。
    // 「退化为零匹配」在这类输入上最致命——整份文件会显示成「全删 + 全插」。
    void fallbackStillMatchesOnLargerRepeatOnlyInput()
    {
        const auto left = linesOf("x\nx\nx\ny\ny\n");
        const auto right = linesOf("y\ny\nx\nx\nx\n");
        const auto patience = aligned(left, right, Alignment::Patience);
        const auto myers = aligned(left, right, Alignment::Myers);
        QCOMPARE(fingerprint(patience), fingerprint(myers));
        // LCS("xxxyy","yyxxx") = 3（"xxx"）；一个「零匹配」的实现会给 0。
        QCOMPARE(matchedPairs(patience).size(), 3);
    }

    // 唯一性必须在**两侧**都成立。`q` 在左侧只出现一次、在右侧出现两次，
    // `p` 两侧都重复 —— 因此这条输入一条合格锚点都没有，必须整体回退到 Myers。
    //
    // 这份夹具是为一个具体的写歪方式准备的：只查左侧唯一性
    // （`countA == 1` 而漏掉 `countB == 1`）。那种实现会拿 `q` 当锚点，
    // 于是左侧的 `p` 与右侧的 `q` 被配成一对——而它俩原文不同，
    // 落到结果里就是一个 `Ignored` 块。**本夹具两侧的键等价关系与原文完全相同**
    // （没有忽略规则），因此 `Ignored` 在这里必然意味着「配错了两条不同的行」，
    // 这个断言是手推出来的，不是从实现里抄的。
    void uniquenessIsRequiredOnBothSides()
    {
        const auto left = linesOf("p\nq\np\nr\n");
        const auto right = linesOf("q\nq\np\np\n");
        const auto patience = aligned(left, right, Alignment::Patience);
        const auto myers = aligned(left, right, Alignment::Myers);
        QCOMPARE(fingerprint(patience), fingerprint(myers));
        QCOMPARE(patience.ignoredBlocks, 0);
        QVERIFY(!patience.differences.isEmpty());
    }

    // ── C 组：标准 3 ──────────────────────────────────────────────────────
    void theTableAdvertisesExactlyTheImplementedAlgorithms()
    {
        QCOMPARE(alignmentTable().size(), 2);
        QCOMPARE(alignmentListText(availableAlignments()), QStringLiteral("myers,patience"));
        // 默认值必须与 `CompareOptions` 的默认值一致——否则「不设选项」与「设成默认」
        // 会走两个分支，而界面上这两件事看起来完全一样。
        QCOMPARE(defaultAlignment(), Alignment::Myers);
        QCOMPARE(CompareOptions().alignment, defaultAlignment());
        QCOMPARE(QString::fromLatin1(alignmentIdentifier(Alignment::Myers)), QStringLiteral("myers"));
        QCOMPARE(QString::fromLatin1(alignmentIdentifier(Alignment::Patience)),
                 QStringLiteral("patience"));
        // 认不出的取值不许编名字。
        QVERIFY(alignmentIdentifier(static_cast<Alignment>(42)) == nullptr);
        const QStringList problems = validateAlignmentTable(alignmentTable());
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char(';'))));
    }

    // 「未实现算法不得可选」有两种失败方式，本条把两种都钉住：
    //  1. 清单里出现未实现的算法   → 由 `availableAlignments()` 的过滤挡住；
    //  2. 规格点名的算法被悄悄降级 → 由 `validateAlignmentTable()` 报出来。
    // 只做第 1 条的话，「把 Patience 标成未实现」会静默地做成一条合规的清单，
    // 而 issue 第 3 条要求实现它——被降级的规格必须有人喊。
    void unimplementedAlgorithmsAreNotOfferedButStillReported()
    {
        QVector<AlignmentDescriptor> table = alignmentTable();
        for (AlignmentDescriptor &descriptor : table)
            if (descriptor.alignment == Alignment::Patience) descriptor.implemented = false;
        QCOMPARE(alignmentListText(availableAlignments(table)), QStringLiteral("myers"));
        // 默认值取「第一条已实现的条目」，因此这里仍然是 Myers——它不能指向
        // 一条没实现的算法，否则默认路径会走空。
        QCOMPARE(defaultAlignment(table), Alignment::Myers);
        const QStringList problems = validateAlignmentTable(table);
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(QStringLiteral("patience")));

        // 一条都不实现：默认值仍要给得出来（Myers 是唯一不依赖别的东西的算法），
        // 但自检必须同时报「没有可实现项」与两条「规格要求被降级」。
        QVector<AlignmentDescriptor> empty = table;
        for (AlignmentDescriptor &descriptor : empty) descriptor.implemented = false;
        QVERIFY(availableAlignments(empty).isEmpty());
        QCOMPARE(defaultAlignment(empty), Alignment::Myers);
        const QStringList allOff = validateAlignmentTable(empty);
        QCOMPARE(allOff.size(), 3);
    }

    void validateReportsMalformedTables()
    {
        // 表为空。**四条**而不是两条：空表同时触发「表为空」「一条都没实现」
        // 以及两条规格点名的算法「没有登记」。这三类问题各有各的修法
        // （补表 / 补实现 / 补登记），合并成一条会让人只修一处就以为干净了。
        const QStringList empty = validateAlignmentTable({});
        QCOMPARE(empty.size(), 4);
        QVERIFY(empty.first().contains(QStringLiteral("为空")));

        // 少登记一种规格点名的算法——这条只有靠 `expected` 才能发现，
        // 因为别的规则都在查「已登记的那些行」，而漏掉的那行本身就是不存在的行。
        QVector<AlignmentDescriptor> missing;
        missing.append({Alignment::Myers, "myers", true});
        const QStringList missingProblems = validateAlignmentTable(missing);
        QCOMPARE(missingProblems.size(), 1);
        QVERIFY(missingProblems.first().contains(
            QString::number(static_cast<int>(Alignment::Patience))));

        // 标识符为空 / 重复。
        const QVector<AlignmentDescriptor> blank{
            {Alignment::Myers, "", true},
            {Alignment::Patience, "patience", true},
        };
        const QStringList blankProblems = validateAlignmentTable(blank);
        QCOMPARE(blankProblems.size(), 1);
        QVERIFY(blankProblems.first().contains(QStringLiteral("没有标识符")));

        const QVector<AlignmentDescriptor> duplicated{
            {Alignment::Myers, "same", true},
            {Alignment::Patience, "same", true},
        };
        const QStringList duplicateProblems = validateAlignmentTable(duplicated);
        QCOMPARE(duplicateProblems.size(), 1);
        QVERIFY(duplicateProblems.first().contains(QStringLiteral("重复")));

        // 干净的表在**几种不同形状**下都不许报——否则上面那些「恰好等于 1 条」
        // 的断言只是在数一个恒定非零的数字。
        const QVector<AlignmentDescriptor> extra{
            {Alignment::Myers, "myers", true},
            {Alignment::Patience, "patience", true},
            {static_cast<Alignment>(77), "unreleased", false},
        };
        QVERIFY(validateAlignmentTable(extra).isEmpty());
        QCOMPARE(alignmentListText(availableAlignments(extra)), QStringLiteral("myers,patience"));
    }

    // 枚举之外 / 未实现的取值从别处（会话文件、命令行、设置仓库存的都是整数）
    // 传进来时，必须**退到默认算法**，不能落进「什么都不跑」的分支——
    // 那会返回一个零块零行的结果，在界面上就是「两份文件完全一样」。
    void anOutOfRangeAlignmentValueFallsBackToTheDefault()
    {
        const auto left = linesOf("a\nb\nc\n");
        const auto right = linesOf("a\nx\nb\nc\n");
        const auto fallback = aligned(left, right, static_cast<Alignment>(99));
        const auto myers = aligned(left, right, Alignment::Myers);
        QVERIFY(!myers.blocks.isEmpty());
        QCOMPARE(fingerprint(fallback), fingerprint(myers));

        // Patience 这条路也不能因为兜底逻辑被写反而静默变成别的东西。
        const auto patience = aligned(left, right, Alignment::Patience);
        QVERIFY(!fingerprint(patience).isEmpty());
        const QString problem = contractProblem(patience, left, right);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    // ── D 组：标准 4 ──────────────────────────────────────────────────────
    //
    // 「切换算法后差异块数量与位置变化被断言（用固定语料的一致性测试）」。
    //
    // 这份语料是挑出来的：`c` 在两侧各出现一次，`a` 出现两次。
    // LCS("abac","caba") 恰好是 3 且**唯一**（只能取 "aba"），
    // 于是 Myers 必然配 (0,1)(1,2)(2,3) 三行；而 Patience 的锚点是唯一行 c，
    // 它只配 (3,0) 一行。两者的块数与块位置都不同，且这个差异是**手推**出来的
    // （两侧文本都不长，LCS 的唯一性可以直接枚举验证），不是把实现跑一遍抄回来的
    // ——抄回来的黄金串只能证明「今天和昨天一样」。
    void switchingAlignmentChangesBlocksAndPositionsOnFixedCorpus()
    {
        const auto left = linesOf("a\nb\na\nc\n");
        const auto right = linesOf("c\na\nb\na\n");
        const auto patience = aligned(left, right, Alignment::Patience);
        const auto myers = aligned(left, right, Alignment::Myers);

        QCOMPARE(fingerprint(patience), QString::fromUtf8(
            "blocks=3 differences=2 ignored=0 limited=0 rows=7\n"
            "b0 Delete L0+3 R0+0 rows0+3\n"
            "b1 Equal L3+1 R0+1 rows3+1\n"
            "b2 Insert L4+0 R1+3 rows4+3\n"
            "r0 l0 r-1 b0 Delete\n"
            "r1 l1 r-1 b0 Delete\n"
            "r2 l2 r-1 b0 Delete\n"
            "r3 l3 r0 b1 Equal\n"
            "r4 l-1 r1 b2 Insert\n"
            "r5 l-1 r2 b2 Insert\n"
            "r6 l-1 r3 b2 Insert\n"
            "diffs=[0,2]"));

        // 块数相同（都是 3）但**配对数与块位置不同**：这条断言防的是
        // 「只在块数上做文章的假实现」——标准 4 要的是「数量与位置」两件事。
        QCOMPARE(myers.blocks.size(), patience.blocks.size());
        QCOMPARE(matchedPairs(myers).size(), 3);
        QCOMPARE(matchedPairs(patience).size(), 1);
        QVERIFY(containsPair(matchedPairs(myers), 0, 1));
        QVERIFY(containsPair(matchedPairs(myers), 1, 2));
        QVERIFY(containsPair(matchedPairs(myers), 2, 3));
        QVERIFY(containsPair(matchedPairs(patience), 3, 0));
        QVERIFY2(fingerprint(myers) != fingerprint(patience),
                 "切换算法没有改变任何块：要么两条分支接到了同一个算法，要么锚点没生效");
    }

    // 「切换算法不改变视图契约」。夹具刻意覆盖四类形状：全相同、单行插入、
    // 大量重复行、以及「原文不同但按选项等价」（走 `Ignored` 而不是 `Equal`
    // ——这一条最容易在换算法时被漏掉，因为它只在开了忽略规则时才出现）。
    void bothAlgorithmsSatisfyTheSameBlockContract()
    {
        struct Fixture { const char *left; const char *right; bool ignoreCase; };
        const Fixture fixtures[] = {
            {"a\nb\nc\n", "a\nb\nc\n", false},
            {"a\nb\nc\n", "a\nb\nX\nY\nc\n", false},
            {"a\nb\na\nc\n", "c\na\nb\na\n", false},
            {"a\na\nb\nb\n", "b\nb\na\na\n", false},
            {"x\nx\nx\ny\ny\n", "y\ny\nx\nx\nx\n", false},
            {"Foo\nBar\n", "foo\nbar\n", true},
            {"a\n", "a\nb\n", false},
        };
        const QVector<Alignment> algorithms{Alignment::Myers, Alignment::Patience};
        for (const Fixture &fixture : fixtures) {
            const auto left = linesOf(fixture.left);
            const auto right = linesOf(fixture.right);
            for (Alignment algorithm : algorithms) {
                CompareOptions options;
                options.alignment = algorithm;
                options.ignoreCase = fixture.ignoreCase;
                const Result once = compare(left, right, options);
                const Result twice = compare(left, right, options);
                const QString label = QStringLiteral("%1 / %2")
                    .arg(QString::fromLatin1(fixture.left), QString::fromLatin1(alignmentIdentifier(algorithm)));
                const QString problem = contractProblem(once, left, right);
                QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("%1：%2").arg(label, problem)));
                // 纯函数：同一输入两次必须逐字段相同。Patience 里有两处 `QHash`
                // （计数唯一行），若有人改成按迭代顺序挑锚点，这里会红。
                QVERIFY2(fingerprint(twice) == fingerprint(once),
                         qPrintable(QStringLiteral("%1：两次运行结果不同").arg(label)));
            }
        }
        // 忽略大小写那一组必须真的产生 `Ignored` 块（否则上一段等于没覆盖那条路径）。
        CompareOptions options;
        options.ignoreCase = true;
        const auto left = linesOf("Foo\nBar\n");
        const auto right = linesOf("foo\nbar\n");
        for (Alignment algorithm : algorithms) {
            options.alignment = algorithm;
            const Result result = compare(left, right, options);
            QCOMPARE(result.blocks.size(), 1);
            QCOMPARE(result.blocks.first().change, Change::Ignored);
            QCOMPARE(result.ignoredBlocks, 1);
            QVERIFY(result.differences.isEmpty());
        }
    }

    // Patience 在没有公共行的**大**输入上必须与 Myers 逐字段相同且开销有界。
    // 「唯一行」的统计要为每一段建两张哈希表，若有人在其中退化成两两比较，
    // 这条会先超时再报错——而真实场景里这种输入是常态（两份互不相关的导出文件）。
    void patienceOnUnrelatedInputMatchesMyersWithinBudget()
    {
        QVector<Line> left, right;
        for (int i = 0; i < 3000; ++i) {
            left.append({QStringLiteral("left %1").arg(i), Eol::LF});
            right.append({QStringLiteral("right %1").arg(i), Eol::LF});
        }
        QElapsedTimer timer;
        timer.start();
        const auto patience = aligned(left, right, Alignment::Patience);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < 3000, qPrintable(QStringLiteral("Patience 耗时 %1ms").arg(elapsed)));
        QCOMPARE(fingerprint(patience), fingerprint(aligned(left, right, Alignment::Myers)));
        const QString problem = contractProblem(patience, left, right);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }
};

QTEST_APPLESS_MAIN(AlignmentTests)
#include "tst_alignment.moc"
