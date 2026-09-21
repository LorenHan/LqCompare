#include "tst_similarity.h"

#include "linesimilarity.h"

#include <QSet>

using namespace LqCompare::Text;

namespace {

// 把一段文本切成行（与 `Tests/Text` 的夹具写法一致：末尾的换行不产生额外的空行，
// 因此 "a\nb\n" 是两行）。
QVector<Line> lines(const QString &text)
{
    QVector<Line> out;
    const QStringList parts = text.split(QLatin1Char('\n'));
    for (int i = 0; i + 1 < parts.size(); ++i) out.append({parts.at(i), Eol::LF});
    return out;
}

QVector<Line> oneLine(const QString &text) { return QVector<Line>{{text, Eol::LF}}; }

// 配对结果的**可读投影**：`l:r` 用逗号连起来。
//
// 比投影而不是比 `QVector<SimilarityPair>` 有两个理由：一是 Qt 打印不出这个类型
// （失败信息会退化成「值不同」，而「少了一对」与「顺序反了」是两种完全不同的错）；
// 二是投影里的下标是**相对区间起点**的偏移，一眼就能对上用例里的注释。
QString projection(const QVector<SimilarityPair> &pairs)
{
    QStringList parts;
    for (const SimilarityPair &pair : pairs)
        parts << QStringLiteral("%1:%2").arg(pair.left).arg(pair.right);
    return parts.join(QLatin1Char(','));
}

QString runProjection(const QVector<DifferenceRun> &runs)
{
    QStringList parts;
    for (const DifferenceRun &run : runs)
        parts << QStringLiteral("%1-%2").arg(run.firstBlock).arg(run.lastBlock);
    return parts.join(QLatin1Char(','));
}

// 标准第 2 条后半句「无丢行」的可执行形式：每一侧的行都必须**恰好**出现在
// `Result::rows` 里一次。
//
// 只断言「块首尾相接」还不够——一个把某一行同时放进两个块的实现同样能让那种断言通过，
// 而它在界面上表现为同一行被画了两次。
QString coverageProblems(const Result &result, int leftLines, int rightLines)
{
    QVector<int> leftSeen(leftLines, 0), rightSeen(rightLines, 0);
    for (const Row &row : result.rows) {
        if (row.leftLine >= 0 && row.leftLine < leftLines) ++leftSeen[row.leftLine];
        if (row.rightLine >= 0 && row.rightLine < rightLines) ++rightSeen[row.rightLine];
    }
    QStringList problems;
    for (int i = 0; i < leftLines; ++i)
        if (leftSeen[i] != 1) problems << QStringLiteral("左 %1 出现 %2 次").arg(i).arg(leftSeen[i]);
    for (int i = 0; i < rightLines; ++i)
        if (rightSeen[i] != 1) problems << QStringLiteral("右 %1 出现 %2 次").arg(i).arg(rightSeen[i]);
    return problems.join(QStringLiteral("；"));
}

} // namespace

void TstSimilarity::initTestCase()
{
    // 默认值只能有一个来源：`defaultSimilarityThreshold()` 必须就是
    // `CompareOptions` 的出厂值。两处各写一个 50，界面与命令行迟早会各说各话，
    // 而「阈值到底是多少」这个问题没有任何机制会发现两个答案。
    QCOMPARE(defaultSimilarityThreshold(), CompareOptions().similarityThreshold);
    QVERIFY(similarityPairingMaximumCells() > 0);
    QVERIFY(similarityPairingBudget() > 0);
}

// -----------------------------------------------------------------------------
// A 分值
// -----------------------------------------------------------------------------

// 值域、对称性与几个手推的分值。
//
// 这一组的期望值全是**在纸上按「LCS 的最长公共子序列长度」逐个数出来的**，
// 不是把实现跑出来的结果抄回去——抄回来的快照只能证明「今天和昨天一样」，
// 证明不了「今天是对的」，而且会把当前的分值公式一起固化成「期望」。
void TstSimilarity::similarityIsBoundedSymmetricAndExactOnEqualInput()
{
    struct Case { const char *left; const char *right; int expected; const char *rule; };
    const Case cases[] = {
        {"abc", "abc", 100, "完全相同：满分"},
        {"a", "abc", 50, "一侧是另一侧的前缀：LCS=1，2·1/(1+3)=50"},
        {"a  b", "a b", 86, "差一个空格：LCS=3，2·3/(4+3)=85.7→86"},
        {"abc", "cba", 33, "字符相同但顺序不同：LCS=1，2·1/6=33.3→33（顺序被计入）"},
        {"abc", "xyz", 0, "毫无公共字符"},
        {"abc", "", 0, "一侧为空"},
        {"", "", 100, "两侧都为空：按判等语义它们一致"},
    };
    for (const Case &row : cases) {
        const QString left = QString::fromUtf8(row.left), right = QString::fromUtf8(row.right);
        const int forward = lineSimilarityPercent(left, right);
        QVERIFY2(forward == row.expected,
                 qPrintable(QStringLiteral("「%1」/「%2」：期望 %3，实际 %4（%5）")
                                .arg(left, right).arg(row.expected).arg(forward)
                                .arg(QString::fromUtf8(row.rule))));
        // 对称：判等是两侧都过完整条链之后才比的，分值必须跟着同一条纪律走。
        // 万一哪天写成「拿短的一侧当基准」，这里会立刻红。
        QCOMPARE(lineSimilarityPercent(right, left), row.expected);
        QVERIFY(forward >= 0);
        QVERIFY(forward <= MaximumSimilarity);
    }
}

// 分值走**规范化链**，而链里的大写折叠跟着 `ignoreCase` 走。
//
// 这条不是形式：开了「忽略大小写」之后，一个纯粹的大小写差异必须变成满分，
// 否则用户看到的是「开了忽略大小写，这两行反而被判成不够相似，于是被拆成一删一增」。
void TstSimilarity::similarityIsCaseSensitiveUnlessTheOptionSaysOtherwise()
{
    const QString left = QStringLiteral("HELLO world"), right = QStringLiteral("hello world");
    CompareOptions options;
    // 手推：只有公共后缀 ` world`（6 个字符）算数，`HELLO` 与 `hello` 在区分大小写时
    // 一个公共字符都没有，于是 LCS=6，两侧各 11 个字符，2·6/22=54.5→55。
    QCOMPARE(lineSimilarityPercent(left, right, options), 55);
    options.ignoreCase = true;
    QCOMPARE(lineSimilarityPercent(left, right, options), MaximumSimilarity);

    // 纯大小写差异的短行在**不折叠**时只有 20 分（LCS 只有首字母 `B`，
    // 两侧各 5 个字符，2·1/10=20）。这个数字是 B 组那条边界用例的邻居，
    // 而它低于出厂阈值——这是「忽略大小写要单独开」的代价，不是实现写漏了一条规则。
    QCOMPARE(lineSimilarityPercent(QStringLiteral("Bravo"), QStringLiteral("BRAVO"),
                                   CompareOptions()), 20);
}

// 空白三模式在**分值**上必须是三个不同的答案（与 `Tests/Text` 对判等的断言同源，
// 只是这次从分值这一侧看）。
void TstSimilarity::similarityFollowsTheWhitespaceChain()
{
    const QString padded = QStringLiteral("  a b  "), plain = QStringLiteral("a b");
    CompareOptions options;
    QCOMPARE(lineSimilarityPercent(padded, plain, options), 60); // LCS=3，2·3/10
    options.whitespace = Whitespace::IgnoreChanges;
    // 折叠内部连续空白并去掉首尾之后两侧都是 `a b`，于是满分。
    QCOMPARE(lineSimilarityPercent(padded, plain, options), MaximumSimilarity);
    options.whitespace = Whitespace::IgnoreAll;
    QCOMPARE(lineSimilarityPercent(padded, plain, options), MaximumSimilarity);

    // `IgnoreChanges` 管的是白空的「数量」，`IgnoreAll` 才连「有没有」也不管：
    // 这一对在中间那一级仍然不是满分——与 `textdiff.h` 里那张表的
    //「分水岭」一行是同一件事的两个视角。
    const QString compact = QStringLiteral("ab"), spaced = QStringLiteral("a b");
    options.whitespace = Whitespace::IgnoreChanges;
    QCOMPARE(lineSimilarityPercent(compact, spaced, options), 80); // LCS=2，2·2/5
    options.whitespace = Whitespace::IgnoreAll;
    QCOMPARE(lineSimilarityPercent(compact, spaced, options), MaximumSimilarity);
}

// -----------------------------------------------------------------------------
// B 阈值判定
// -----------------------------------------------------------------------------

// 阈值**含等号**，而且这件事在引擎上看得见：`L0` / `R0` 正好 50 分，
// 阈值 50 配得上、51 配不上。
//
// 用真实的行而不是直接构造分值：阈值取「含等号」还是「不含」的差别，
// 只有走完整条链（规范化 → 算分 → 判定 → 铺块）才看得见后果。
void TstSimilarity::thresholdIsInclusiveAtItsOwnValue()
{
    const QVector<Line> left = oneLine(QStringLiteral("L0"));
    const QVector<Line> right = oneLine(QStringLiteral("R0"));
    QCOMPARE(lineSimilarityPercent(QStringLiteral("L0"), QStringLiteral("R0")), 50);
    QVERIFY(isSimilarEnough(50, 50));
    QVERIFY(!isSimilarEnough(49, 50));

    CompareOptions options;
    options.similarityThreshold = 50;
    const Result inclusive = compare(left, right, options);
    QCOMPARE(inclusive.differences.size(), 1);
    QCOMPARE(inclusive.blocks.first().change, Change::Replace);

    options.similarityThreshold = 51;
    const Result exclusive = compare(left, right, options);
    QCOMPARE(exclusive.differences.size(), 2);
    QCOMPARE(exclusive.blocks[exclusive.differences[0]].change, Change::Delete);
    QCOMPARE(exclusive.blocks[exclusive.differences[1]].change, Change::Insert);
}

// 落在 0–100 之外的值**钳到边界**，而不是退回默认值。
//
// 两种做法的代价不对等：钳制只把一个手改坏的数字压到边界（用户看得懂「最小 0、最大 100」），
// 而「退回默认值」会把一个他没设过的数字（50）显示在界面上——
// 于是「我明明改成 80 了」这件事在界面上查不出来。
void TstSimilarity::thresholdClampsInsteadOfFallingBack()
{
    QCOMPARE(clampSimilarityThreshold(-5), 0);
    QCOMPARE(clampSimilarityThreshold(0), 0);
    QCOMPARE(clampSimilarityThreshold(37), 37);
    QCOMPARE(clampSimilarityThreshold(MaximumSimilarity), MaximumSimilarity);
    QCOMPARE(clampSimilarityThreshold(9999), MaximumSimilarity);
    // 判定走同一条钳制：阈值 -5 等于 0（什么都配得上），9999 等于 100（只有完全相同的行）。
    QVERIFY(isSimilarEnough(0, -5));
    QVERIFY(isSimilarEnough(100, 9999));
    QVERIFY(!isSimilarEnough(99, 9999));
}

// -----------------------------------------------------------------------------
// C 单调配对
// -----------------------------------------------------------------------------

// 低于阈值的候选对**不配对**；阈值 0 则退化成「无论多不像都配对」
//（也就是这个功能出现之前的引擎行为）。
void TstSimilarity::pairingDropsCandidatePairsBelowTheThreshold()
{
    // 手推：(0,0) 完全相同 100；(1,1) `bravo two` / `qqqqqqqqqqqq` 毫无公共字符 0；
    // (1,0) `bravo two` / `alpha one` 只有公共后缀 ` one`，2·4/17=47——正好也在阈值下。
    const QVector<Line> left = lines(QStringLiteral("alpha one\nbravo two\n"));
    const QVector<Line> right = lines(QStringLiteral("alpha one\nqqqqqqqqqqqq\n"));

    const SimilarityPlan strict = pairSimilarLines(left, 0, left.size(), right, 0, right.size(),
                                                   CompareOptions(), 50);
    QVERIFY(!strict.limited);
    QCOMPARE(projection(strict.pairs), QStringLiteral("0:0"));

    const SimilarityPlan loose = pairSimilarLines(left, 0, left.size(), right, 0, right.size(),
                                                  CompareOptions(), 0);
    QVERIFY(!loose.limited);
    // 阈值 0 与「关闭总开关」是两条不同的路径：前者把门槛放到最低而不取消配对，
    // 后者取消配对（见 D 组）。这条断言把两者分开，免得将来有人把
    // 「阈值 0」实现成「等于关闭开关」。
    QCOMPARE(projection(loose.pairs), QStringLiteral("0:0,1:1"));
}

// 配对必须**保序**（左右下标各自严格递增），而且一侧都不能丢行。
//
// 保序不是审美：块是按顺序铺出来的，一对交叉的配对会让某一行落在已经铺过的区间里，
// 于是那一行要么丢、要么被放两次。
void TstSimilarity::pairingIsMonotoneAndKeepsEveryLine()
{
    // 两侧行数不同、且只有下边那一对够像——于是配对结果是**非按位**的
    //（左侧第 0 行不配、第 1 行配右侧第 0 行）。按位配对的实现在这里会红。
    const QVector<Line> left = lines(QStringLiteral("left 0\nbravo X\n"));
    const QVector<Line> right = lines(QStringLiteral("bravo X\n"));
    const SimilarityPlan plan = pairSimilarLines(left, 0, left.size(), right, 0, right.size(),
                                                 CompareOptions(), 50);
    QVERIFY(!plan.limited);
    QCOMPARE(projection(plan.pairs), QStringLiteral("1:0"));
    for (int i = 0; i < plan.pairs.size(); ++i) {
        if (i > 0) {
            QVERIFY(plan.pairs[i].left > plan.pairs[i - 1].left);
            QVERIFY(plan.pairs[i].right > plan.pairs[i - 1].right);
        }
        QVERIFY(plan.pairs[i].left >= 0 && plan.pairs[i].left < left.size());
        QVERIFY(plan.pairs[i].right >= 0 && plan.pairs[i].right < right.size());
    }

    // 引擎侧：一段改动被拆开之后，两侧的每一行仍然恰好出现一次。
    const QVector<Line> wide = lines(QStringLiteral("alpha one\nbravo X\ncharlie three\n"));
    const QVector<Line> other = lines(QStringLiteral("alpha one\nbravo X\nqqqqqqqqqqqqqq\n"));
    const Result result = compare(wide, other);
    QVERIFY2(coverageProblems(result, wide.size(), other.size()).isEmpty(),
             qPrintable(coverageProblems(result, wide.size(), other.size())));
    QVERIFY(!result.similarityPairingLimited);
    // 最后那一对不像，于是这一段是「替换（前两行相等）+ 删除 + 新增」。
    QCOMPARE(result.differences.size(), 2);
    QCOMPARE(result.blocks[result.differences[0]].change, Change::Delete);
    QCOMPARE(result.blocks[result.differences[1]].change, Change::Insert);
}

// 目标函数是 `(配对数, 总分值)` 的字典序：**能配就配**，配对数相同时才比更像的那一对。
//
// 把两项顺序写反（先比总分值）时，「一对 100 分」会压过「两对 80 分」，
// 于是用户看到两行改动被缩成一处——现象很不显眼，只有固定语料上的对应关系能验出来。
void TstSimilarity::pairingPrefersMorePairsThenHigherScore()
{
    // 配对数优先：两对 80 分（`aaaa`/`aaaa x`、`bbbb`/`bbbb x`）胜过任何一对 100 分。
    // 交叉的那些对只有 44 分（公共的 ` x`），阈值下不来插一脚。
    const QVector<Line> left = lines(QStringLiteral("aaaa\nbbbb\n"));
    const QVector<Line> right = lines(QStringLiteral("aaaa x\nbbbb x\n"));
    const SimilarityPlan byCount = pairSimilarLines(left, 0, left.size(), right, 0, right.size(),
                                                    CompareOptions(), 50);
    QVERIFY(!byCount.limited);
    QCOMPARE(projection(byCount.pairs), QStringLiteral("0:0,1:1"));

    // 配对数相同时比总分值：左侧两行都只能配右侧那一行（`hello worlds` 满分 100，
    // `hello world` 96），挑更像的那一对——也就是**右侧那一行对应的左侧第 1 行**。
    const QVector<Line> many = lines(QStringLiteral("hello world\nhello worlds\n"));
    const QVector<Line> single = lines(QStringLiteral("hello worlds\n"));
    const SimilarityPlan byScore = pairSimilarLines(many, 0, many.size(), single, 0, single.size(),
                                                    CompareOptions(), 50);
    QVERIFY(!byScore.limited);
    QCOMPARE(byScore.pairs.size(), 1);
    QCOMPARE(byScore.pairs.first().left, 1);
    QCOMPARE(byScore.pairs.first().right, 0);
}

// -----------------------------------------------------------------------------
// D 总开关
// -----------------------------------------------------------------------------

// 关闭开关：一段「两侧都有行」的改动被拆成**删除块 + 新增块两条独立块**。
// 顺序固定「先删后增」——TXT-006 的手动对齐要合并的正是这一对相邻块。
void TstSimilarity::disabledSwitchTurnsASimilarPairIntoDeleteAndInsert()
{
    const QVector<Line> left = oneLine(QStringLiteral("int count = 0;"));
    const QVector<Line> right = oneLine(QStringLiteral("int count = 1;"));
    CompareOptions options;
    options.alignSimilarLines = false;
    const Result result = compare(left, right, options);
    QCOMPARE(result.blocks.size(), 2);
    QCOMPARE(result.blocks[0].change, Change::Delete);
    QCOMPARE(result.blocks[1].change, Change::Insert);
    // 删在前、增在后，且各自只占自己那一侧的那一行。
    QCOMPARE(result.blocks[0].leftCount, 1);
    QCOMPARE(result.blocks[0].rightCount, 0);
    QCOMPARE(result.blocks[1].leftCount, 0);
    QCOMPARE(result.blocks[1].rightCount, 1);
    QVERIFY(coverageProblems(result, 1, 1).isEmpty());
    // 拆成两块仍然是**一处改动**：用户按一次「下一处」不该在同一处上停两次。
    QCOMPARE(runProjection(differenceRuns(result)), QStringLiteral("0-1"));
}

// 打开开关（出厂值）：同样的输入是一处**修改块**，行是配好的。
void TstSimilarity::enabledSwitchKeepsASimilarPairAsOneModificationBlock()
{
    QVERIFY(CompareOptions().alignSimilarLines);
    const QVector<Line> left = oneLine(QStringLiteral("int count = 0;"));
    const QVector<Line> right = oneLine(QStringLiteral("int count = 1;"));
    const Result result = compare(left, right);
    QCOMPARE(result.blocks.size(), 1);
    QCOMPARE(result.blocks.first().change, Change::Replace);
    QCOMPARE(result.rows.size(), 1);
    QCOMPARE(result.rows.first().leftLine, 0);
    QCOMPARE(result.rows.first().rightLine, 0);
    QCOMPARE(runProjection(differenceRuns(result)), QStringLiteral("0-0"));

    // 两侧行数不等、且只有一部分够像：够像的配成一对、不够像的各自成块，
    // 但**仍然是一处改动**。这正是「关掉开关」与「阈值太高」在界面上的区别：
    // 前者把整段拆开，后者只拆掉不够像的那几行。
    const QVector<Line> wide = lines(QStringLiteral("keep\nint count = 0;\nqqqqqqqqqqqqqq\n"));
    const QVector<Line> other = lines(QStringLiteral("keep\nint count = 1;\n"));
    const Result mixed = compare(wide, other);
    QCOMPARE(runProjection(differenceRuns(mixed)), QStringLiteral("1-2"));
    int replaces = 0;
    for (const Block &block : mixed.blocks)
        if (block.change == Change::Replace) ++replaces;
    QCOMPARE(replaces, 1);
    QVERIFY(coverageProblems(mixed, wide.size(), other.size()).isEmpty());
    // 那一处替换的行必须是**配好的**一行（两侧都有行号），而不是碰巧落进同一个块。
    for (const Row &row : mixed.rows) {
        if (row.change != Change::Replace) continue;
        QVERIFY(row.leftLine >= 0);
        QVERIFY(row.rightLine >= 0);
    }
}

// -----------------------------------------------------------------------------
// E 出厂默认值
// -----------------------------------------------------------------------------

// 出厂阈值**在固定语料上验证过**——这是规格的边界条款明确要求的。
//
// 语料两侧各自贴着手推的分值，50 落在中间：
//   * 一边是「同一行的两种改写」（72 / 93 / 94 分）——必须配对；
//   * 另一边是「本来就不相干的两行」（46 / 0 分）——必须不配对。
//
// 把默认值改成 0 会让左边那一列仍然通过、右边那一列整列变红；改成 100 会让左边也变红。
// 因此这条用例同时钉住了「默认值是多少」与「它为什么在这里」——
// 只钉数值的话，下一个人把它改成 0 也能让用例全绿。
void TstSimilarity::defaultThresholdSeparatesRewritesFromUnrelatedLines()
{
    QCOMPARE(defaultSimilarityThreshold(), 50);
    struct Case { const char *left; const char *right; bool pairs; int percent; const char *reason; };
    const Case cases[] = {
        {"return true;", "return false;", true, 72, "同一行的两种返回"},
        {"int count = 0;", "int count = 1;", true, 93, "常量改了"},
        {"  total += value;", "  total -= value;", true, 94, "运算符改了"},
        {"left 0", "right 0", false, 46, "只是碰巧共享几个字符"},
        {"alpha", "qqqqq", false, 0, "毫无关系"},
    };
    for (const Case &row : cases) {
        const QString left = QString::fromUtf8(row.left), right = QString::fromUtf8(row.right);
        const int percent = lineSimilarityPercent(left, right);
        QVERIFY2(percent == row.percent,
                 qPrintable(QStringLiteral("「%1」/「%2」的分值变了：期望 %3，实际 %4")
                                .arg(left, right).arg(row.percent).arg(percent)));
        const Result result = compare(oneLine(left), oneLine(right));
        const bool paired = result.blocks.size() == 1
            && result.blocks.first().change == Change::Replace;
        QVERIFY2(paired == row.pairs,
                 qPrintable(QStringLiteral("出厂阈值下「%1」/「%2」的呈现方式变了（%3）")
                                .arg(left, right, QString::fromUtf8(row.reason))));
    }
}

// -----------------------------------------------------------------------------
// F 一处改动的归并
// -----------------------------------------------------------------------------

// 归并只合并**块下标连续**的差异块。两处独立的改动之间必然隔着一个相同块，
// 因此它们的下标不可能连续——这条判据背后是引擎的区间结构，不是巧合。
void TstSimilarity::differenceRunsMergeAdjacentBlocksOnly()
{
    // 两处各被拆成「删除 + 新增」，中间隔着相同的 `c`。
    const QVector<Line> left = lines(QStringLiteral("a\nleft\nc\nleftagain\n"));
    const QVector<Line> right = lines(QStringLiteral("a\nqqqq\nc\nwwww\n"));
    const Result result = compare(left, right);
    QCOMPARE(result.blocks.size(), 6);
    QCOMPARE(result.differences.size(), 4);
    QCOMPARE(runProjection(differenceRuns(result)), QStringLiteral("1-2,4-5"));

    // 够像的行配成一对时，两处改动各自是一个**单独的替换块**，
    // 归并结果仍然是两处而不是一处——「下标连续」这条判据不受块类型影响。
    const QVector<Line> similarLeft = lines(QStringLiteral("a\nleft two\nc\nleft four\n"));
    const QVector<Line> similarRight = lines(QStringLiteral("a\nright two\nc\nright four\n"));
    QCOMPARE(runProjection(differenceRuns(compare(similarLeft, similarRight))),
             QStringLiteral("1-1,3-3"));

    // 完全相同的输入：零处改动。
    QCOMPARE(differenceRuns(compare(left, left)).size(), 0);
    QCOMPARE(runProjection(differenceRuns(compare(oneLine(QStringLiteral("int count = 0;")),
                                                  oneLine(QStringLiteral("int count = 1;"))))),
             QStringLiteral("0-0"));
}

// 工作量超限时**退回按位配对**（这个功能出现之前的旧行为），并且如实报出来。
//
// 「退回」与「静默失效」的区别就是这条用例的全部意义：静默的话，用户把阈值从 0 调到 100
// 会看到结果一模一样，而没有任何线索指向真正的原因（那一段改动太大了）。
// 两个上限各有一条断言，它们挡的是两件不同的事：单元格上限挡「行数太多的区间」，
// 字符预算挡「行本身太长的区间」。
void TstSimilarity::oversizedChangeFallsBackToPositionalPairingAndSaysSo()
{
    QVERIFY(similarityPairingMaximumCells() == 512 * 512);

    // 单元格上限：513×513 超过 512×512；两侧毫无公共行，因此 Myers 走「无公共行」
    // 的快速路径，整份文件是一段非相同区间——正是要构造的形状。
    QVector<Line> wideLeft, wideRight;
    for (int i = 0; i < 513; ++i) {
        wideLeft.append({QStringLiteral("L%1").arg(i), Eol::LF});
        wideRight.append({QStringLiteral("R%1").arg(i), Eol::LF});
    }
    const Result over = compare(wideLeft, wideRight);
    QVERIFY2(over.similarityPairingLimited, "超过单元格上限却没有报出来");
    QCOMPARE(over.blocks.size(), 1);
    QCOMPARE(over.blocks.first().change, Change::Replace);
    QVERIFY(coverageProblems(over, wideLeft.size(), wideRight.size()).isEmpty());

    // 字符预算：行数在单元格上限之内，但每行很长（60 × 210 字符的两侧乘起来超过预算）。
    QVector<Line> longLeft, longRight;
    const QString filler(200, QLatin1Char('x'));
    for (int i = 0; i < 60; ++i) {
        longLeft.append({QStringLiteral("left %1 %2").arg(i).arg(filler), Eol::LF});
        longRight.append({QStringLiteral("right %1 %2").arg(i).arg(filler), Eol::LF});
    }
    const Result longOver = compare(longLeft, longRight);
    QVERIFY2(longOver.similarityPairingLimited, "超过字符预算却没有报出来");
    QCOMPARE(longOver.blocks.size(), 1);
    QCOMPARE(longOver.blocks.first().change, Change::Replace);
    QVERIFY(coverageProblems(longOver, longLeft.size(), longRight.size()).isEmpty());

    // 反例：规模之内的输入**不许**被标成受限——否则「一律退回」的实现在这里也能过，
    // 而那等于这个功能从来没生效过。
    QVector<Line> smallLeft, smallRight;
    for (int i = 0; i < 8; ++i) {
        smallLeft.append({QStringLiteral("left %1 %2").arg(i).arg(filler), Eol::LF});
        smallRight.append({QStringLiteral("right %1 %2").arg(i).arg(filler), Eol::LF});
    }
    const Result small = compare(smallLeft, smallRight);
    QVERIFY(!small.similarityPairingLimited);
    QCOMPARE(small.blocks.size(), 1);
    QCOMPARE(small.blocks.first().change, Change::Replace);
}

QTEST_MAIN(TstSimilarity)
