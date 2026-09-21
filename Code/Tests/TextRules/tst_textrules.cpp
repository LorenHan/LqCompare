#include "tst_textrules.h"

#include "linereplacements.h"
#include "textdiff.h"
#include "textdocument.h"

#include <QRegularExpression>
#include <QSet>

using namespace LqCompare::Text;

namespace {

QVector<ReplacementRule> rules(std::initializer_list<ReplacementRule> list)
{
    QVector<ReplacementRule> out;
    for (const ReplacementRule rule : list) out.append(rule);
    return out;
}

// 把一段文本切成行。与 `Tests/Similarity`、`Tests/Text` 的夹具写法一致：
// 末尾的换行不产生额外的空行，因此 "a\nb\n" 是两行。
QVector<Line> lines(const QString &text)
{
    QVector<Line> out;
    const QStringList parts = text.split(QLatin1Char('\n'));
    for (int i = 0; i + 1 < parts.size(); ++i) out.append({parts.at(i), Eol::LF});
    return out;
}

} // namespace

void TstTextRules::initTestCase()
{
    // 出厂表必须干净。一个「永远在报」的护栏与没有护栏一样糟——它会让真正的红
    // 混在噪声里，而这条用例的定位正是「表写错了要在这一层发现」。
    const QStringList problems = validateReplacementRuleTable(replacementRuleTable());
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("；"))));
}

// ---------------------------------------------------------------------------
// A 规则表
// ---------------------------------------------------------------------------

void TstTextRules::tableDeclaresEverySpecifiedRuleWithMachineReadableIdentifiers()
{
    // 规格点名的四条，一条不少、顺序即声明顺序（也是应用顺序）。
    QVERIFY(availableReplacementRules()
            == rules({ReplacementRule::LeadingNumber, ReplacementRule::DateTime,
                      ReplacementRule::Guid, ReplacementRule::HexAddress}));

    // 默认一条都不开：出厂就开着等于替用户做了内容层面的决定。
    QVERIFY(defaultReplacementRules().isEmpty());

    // 表外取值不编一个假名字——编出来的标识符只会让日志里的人去搜一个不存在的符号。
    QVERIFY(replacementRuleIdentifier(static_cast<ReplacementRule>(99)) == nullptr);
    QVERIFY(replacementRuleDescriptor(static_cast<ReplacementRule>(99)) == nullptr);
    QVERIFY(replacementRuleDescription(static_cast<ReplacementRule>(99)).isEmpty());
}

void TstTextRules::validationRejectsBrokenTables()
{
    // 正的一面：一份干净的小表必须被判为干净，否则反面的红说明不了任何事。
    const QVector<ReplacementRuleDescriptor> good = {
        {ReplacementRule::LeadingNumber, "leading-number", "<NUM>", "^[0-9]+\\. ", "说明", true},
        {ReplacementRule::DateTime, "date-time", "<DATE>", "[0-9]{4}-[0-9]{2}-[0-9]{2}", "说明", true},
    };
    QCOMPARE(validateReplacementRuleTable(good, rules({ReplacementRule::LeadingNumber,
                                                       ReplacementRule::DateTime})),
             QStringList());

    // 反的一面：四种「手写时会写错、写错了也不影响别的」的写法，每一种都要被报出来。
    const QVector<ReplacementRuleDescriptor> duplicatedIdentifier = {
        {ReplacementRule::LeadingNumber, "same", "<NUM>", "^[0-9]+\\. ", "说明", true},
        {ReplacementRule::DateTime, "same", "<DATE>", "[0-9]{4}", "说明", true},
    };
    QVERIFY(!validateReplacementRuleTable(duplicatedIdentifier, {}).isEmpty());

    const QVector<ReplacementRuleDescriptor> anonymous = {
        {ReplacementRule::LeadingNumber, "", "<NUM>", "^[0-9]+\\. ", "说明", true},
    };
    QVERIFY(!validateReplacementRuleTable(anonymous, {}).isEmpty());

    const QVector<ReplacementRuleDescriptor> brokenPattern = {
        {ReplacementRule::Guid, "guid", "<GUID>", "([0-9]", "说明", true},
    };
    QVERIFY(!validateReplacementRuleTable(brokenPattern, {}).isEmpty());

    const QVector<ReplacementRuleDescriptor> missingPlaceholder = {
        {ReplacementRule::Guid, "guid", "", "[0-9]{8}", "说明", true},
    };
    QVERIFY(!validateReplacementRuleTable(missingPlaceholder, {}).isEmpty());

    // 规格点名的规则没实现，也要报——否则「漏登记一条」这件事没有任何东西会响。
    const QVector<ReplacementRuleDescriptor> notImplemented = {
        {ReplacementRule::Guid, "guid", "<GUID>", "[0-9]{8}", "说明", false},
    };
    QVERIFY(!validateReplacementRuleTable(notImplemented, rules({ReplacementRule::Guid})).isEmpty());

    // 表里同一条规则登记两次：后一条会盖住前一条的行为，值多花时间也查不出来。
    const QVector<ReplacementRuleDescriptor> twice = {
        {ReplacementRule::Guid, "guid", "<GUID>", "[0-9]{8}", "说明", true},
        {ReplacementRule::Guid, "guid-again", "<GUID2>", "[0-9]{9}", "说明", true},
    };
    QVERIFY(!validateReplacementRuleTable(twice, {}).isEmpty());
}

// ---------------------------------------------------------------------------
// B 单条规则行为
// ---------------------------------------------------------------------------

void TstTextRules::leadingNumberNeedsSeparatorAndSpace()
{
    const QVector<ReplacementRule> only = rules({ReplacementRule::LeadingNumber});

    QCOMPARE(applyReplacementRules(QStringLiteral("1. 引言"), only), QStringLiteral("<NUM>引言"));
    QCOMPARE(applyReplacementRules(QStringLiteral("2) 目标"), only), QStringLiteral("<NUM>目标"));
    QCOMPARE(applyReplacementRules(QStringLiteral("12: 备注"), only), QStringLiteral("<NUM>备注"));
    QCOMPARE(applyReplacementRules(QStringLiteral("   3. 缩进"), only), QStringLiteral("<NUM>缩进"));

    // 反的一面：这几行看起来也像编号，但吃掉它们就是误伤——
    // 在差异视图里表现为「一堆本该不同的行变成了相同」，最难被发现的一种错。
    QCOMPARE(applyReplacementRules(QStringLiteral("42 apples"), only), QStringLiteral("42 apples"));
    QCOMPARE(applyReplacementRules(QStringLiteral("1 引言"), only), QStringLiteral("1 引言"));
    QCOMPARE(applyReplacementRules(QStringLiteral("1.引言"), only), QStringLiteral("1.引言"));
    // 编号只认行首：正文中间出现的 `1.` 不是编号。
    QCOMPARE(applyReplacementRules(QStringLiteral("版本 1. 发布"), only), QStringLiteral("版本 1. 发布"));
}

void TstTextRules::dateTimeCoversItsFormsWithoutEatingPlainNumbers()
{
    const QVector<ReplacementRule> only = rules({ReplacementRule::DateTime});

    QCOMPARE(applyReplacementRules(QStringLiteral("2024-01-01"), only), QStringLiteral("<DATE>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("2024/1/1"), only), QStringLiteral("<DATE>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("2024-01-01 10:20"), only), QStringLiteral("<DATE>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("2024-01-01T10:20:30Z"), only), QStringLiteral("<DATE>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("开始 2024-12-31T23:59:59.500+08:00 结束"), only),
             QStringLiteral("开始 <DATE> 结束"));

    // 反的一面：没有分隔符的八位数字是编号/数量，不是日期。
    QCOMPARE(applyReplacementRules(QStringLiteral("20240101"), only), QStringLiteral("20240101"));
    QCOMPARE(applyReplacementRules(QStringLiteral("1.2 版本"), only), QStringLiteral("1.2 版本"));
    // 前一个字符是词字符时不是词首：`x2024-01-01` 里的那一段不该被当成日期，
    // 否则 `abc2024-01-01` 这种「前缀是标识符的一部分」的文本会被改写。
    QCOMPARE(applyReplacementRules(QStringLiteral("x2024-01-01"), only), QStringLiteral("x2024-01-01"));
}

void TstTextRules::guidIgnoresCaseAndBracesButNotPartialMatches()
{
    const QVector<ReplacementRule> only = rules({ReplacementRule::Guid});

    const QString lower = QStringLiteral("550e8400-e29b-41d4-a716-446655440000");
    QCOMPARE(applyReplacementRules(lower, only), QStringLiteral("<GUID>"));
    QCOMPARE(applyReplacementRules(lower.toUpper(), only), QStringLiteral("<GUID>"));
    // 花括号一起吃掉：只吃中间那 36 个字符会把花括号留在两侧，
    // 于是 `{...}` 与不带花括号的写法在判等上仍然不同，而它们表达的是同一个标识。
    QCOMPARE(applyReplacementRules(QStringLiteral("{") + lower + QStringLiteral("}"), only),
             QStringLiteral("<GUID>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("id=") + lower + QStringLiteral(";"), only),
             QStringLiteral("id=<GUID>;"));

    // 反的一面。
    QCOMPARE(applyReplacementRules(QStringLiteral("550e8400e29b41d4a716446655440000"), only),
             QStringLiteral("550e8400e29b41d4a716446655440000")); // 没有连字符
    QCOMPARE(applyReplacementRules(QStringLiteral("550e8400-e29b-41d4-a716-44665544000"), only),
             QStringLiteral("550e8400-e29b-41d4-a716-44665544000")); // 最后一段少一位
    // 嵌在更长的十六进制串里：那是一个更大的十六进制数，不是 GUID。
    QCOMPARE(applyReplacementRules(QStringLiteral("aabb") + lower, only),
             QStringLiteral("aabb") + lower);
}

void TstTextRules::hexAddressNeedsItsPrefix()
{
    const QVector<ReplacementRule> only = rules({ReplacementRule::HexAddress});

    QCOMPARE(applyReplacementRules(QStringLiteral("0x7FFE0000"), only), QStringLiteral("<HEX>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("0X1f"), only), QStringLiteral("<HEX>"));
    QCOMPARE(applyReplacementRules(QStringLiteral("(&p == 0xdeadbeef)"), only),
             QStringLiteral("(&p == <HEX>)"));

    // 反的一面：没有前缀的十六进制串**不是**地址。`add` / `beef` / `face` / `cafe`
    // 都是普通英文单词，裸串规则会在散文中大开杀戒。
    QCOMPARE(applyReplacementRules(QStringLiteral("DEADBEEF"), only), QStringLiteral("DEADBEEF"));
    QCOMPARE(applyReplacementRules(QStringLiteral("add beef to the face"), only),
             QStringLiteral("add beef to the face"));
    // `0x` 后面没有十六进制数字：不是一个地址，保持原样（而不是留下一个半截的 `0x`）。
    QCOMPARE(applyReplacementRules(QStringLiteral("0xZZ"), only), QStringLiteral("0xZZ"));
    // 前缀前面还跟着标识符字符时，它是那个标识符的一部分。
    QCOMPARE(applyReplacementRules(QStringLiteral("foo0x12"), only), QStringLiteral("foo0x12"));
}

// ---------------------------------------------------------------------------
// C 开关集合
// ---------------------------------------------------------------------------

void TstTextRules::rulesAreOffByDefaultAndAnEmptySetShortCircuits()
{
    const ReplacementSet set;
    QVERIFY(set.isEmpty());
    QVERIFY(set.enabledRules().isEmpty());
    for (const ReplacementRule rule : availableReplacementRules())
        QVERIFY(!set.isEnabled(rule));

    // 短路：一条规则都没开时，连一个字符都不该改动。
    const QString line = QStringLiteral(
        "1. 0x7FFE0000 2024-01-01 550e8400-e29b-41d4-a716-446655440000");
    QCOMPARE(set.apply(line), line);
}

void TstTextRules::eachRuleTogglesIndependently()
{
    ReplacementSet set = ReplacementSet::fromRules(rules({ReplacementRule::DateTime}));
    QVERIFY(set.isEnabled(ReplacementRule::DateTime));
    QVERIFY(!set.isEnabled(ReplacementRule::Guid));
    QVERIFY(!set.isEmpty());

    const QString line = QStringLiteral("550e8400-e29b-41d4-a716-446655440000 2024-01-01");
    QCOMPARE(set.apply(line), QStringLiteral("550e8400-e29b-41d4-a716-446655440000 <DATE>"));

    set.setEnabled(ReplacementRule::Guid, true);
    QCOMPARE(set.apply(line), QStringLiteral("<GUID> <DATE>"));

    set.setEnabled(ReplacementRule::DateTime, false);
    QCOMPARE(set.apply(line), QStringLiteral("<GUID> 2024-01-01"));

    set.setEnabled(ReplacementRule::Guid, false);
    QVERIFY(set.isEmpty());
    QCOMPARE(set.apply(line), line);
}

void TstTextRules::enabledRulesFollowTableOrderNotToggleOrder()
{
    // 勾选顺序与表顺序**刻意相反**：先开第三条（GUID）再开第一条（行首编号）。
    // 若实现把勾选顺序当成应用顺序，同一份配置会因为用户点选的先后给出不同结果，
    // 而且在两台机器上结论不同、没有任何东西会报错。
    ReplacementSet set;
    set.setEnabled(ReplacementRule::Guid, true);
    set.setEnabled(ReplacementRule::LeadingNumber, true);
    QVERIFY2(set.enabledRules() == rules({ReplacementRule::LeadingNumber, ReplacementRule::Guid}),
             "启用集合必须按表顺序枚举，而不是按用户点选的先后");

    // `fromRules` 走的是同一条路。
    ReplacementSet fromList = ReplacementSet::fromRules(
        rules({ReplacementRule::Guid, ReplacementRule::LeadingNumber}));
    QVERIFY(fromList.enabledRules()
            == rules({ReplacementRule::LeadingNumber, ReplacementRule::Guid}));
}

void TstTextRules::unknownEnumValueIsSkippedInsteadOfFallingBack()
{
    // 枚举取值可能来自会话文件 / 命令行 / 设置仓库（那些地方存的是整数）。
    // 表外取值必须被**跳过**——不兜底成某一条规则，也不崩。
    ReplacementSet set;
    set.setEnabled(static_cast<ReplacementRule>(99), true);
    QVERIFY(set.isEmpty());

    const QString line = QStringLiteral("1. 引言");
    QCOMPARE(applyReplacementRules(line, rules({static_cast<ReplacementRule>(99)})), line);
}

void TstTextRules::everyRuleExposesItsPatternAndDescription()
{
    // 完成标准第 2 条「显示其正则或匹配说明」的服务层一半：正则与说明都是可读数据，
    // 界面直接搬就行，**不要**在界面里再抄一份说明（那就是第二份事实来源）。
    QSet<QString> identifiers;
    for (const ReplacementRule rule : availableReplacementRules()) {
        const ReplacementRuleDescriptor *descriptor = replacementRuleDescriptor(rule);
        QVERIFY(descriptor != nullptr);

        const QString pattern = QString::fromUtf8(descriptor->pattern);
        QVERIFY2(!pattern.isEmpty(), "界面要显示的正则不能在服务层是空的");
        QVERIFY2(QRegularExpression(pattern).isValid(), qPrintable(pattern));
        QVERIFY2(!replacementRuleDescription(rule).isEmpty(), "界面要显示的说明不能是空的");

        const QString identifier = QString::fromUtf8(replacementRuleIdentifier(rule));
        QVERIFY(!identifier.isEmpty());
        QVERIFY2(!identifiers.contains(identifier),
                 "两条规则共用标识符会让设置键指向同一条规则");
        identifiers.insert(identifier);
    }
    QCOMPARE(identifiers.size(), 4);
}

// ---------------------------------------------------------------------------
// D 顺序与叠加
// ---------------------------------------------------------------------------

void TstTextRules::singleRuleReplacementMatchesTheFixedCorpus()
{
    const QString guid = QStringLiteral("550e8400-e29b-41d4-a716-446655440000");

    QCOMPARE(applyReplacementRules(QStringLiteral("id ") + guid + QStringLiteral(" 就绪"),
                                   rules({ReplacementRule::Guid})),
             QStringLiteral("id <GUID> 就绪"));
    QCOMPARE(applyReplacementRules(QStringLiteral("写入 2024-06-30 完成"),
                                   rules({ReplacementRule::DateTime})),
             QStringLiteral("写入 <DATE> 完成"));
    QCOMPARE(applyReplacementRules(QStringLiteral("   7. 第七项"),
                                   rules({ReplacementRule::LeadingNumber})),
             QStringLiteral("<NUM>第七项"));
    QCOMPARE(applyReplacementRules(QStringLiteral("基址 0x00007FFE0000"),
                                   rules({ReplacementRule::HexAddress})),
             QStringLiteral("基址 <HEX>"));
}

void TstTextRules::overlappingRulesRevealTheApplicationOrder()
{
    // 这条语料是**刻意构造**的：它是整个套件里唯一一处「两条规则都想吃同一段文本」。
    // 表里 Guid 排在 HexAddress 之前，所以先按 GUID 认出 8-4-4-4-12，
    // `0x` 前缀因此被留在原地；反过来先跑 HexAddress，`0x12345678` 会被当作一个
    // 十六进制地址吃掉，剩下的 4-4-4-12 就再也凑不出 GUID 了。
    //
    // 没有这条语料，「按声明顺序依次应用」这句话是不可观察的：两条规则的区间一旦不相交，
    // 谁先谁后都给出同一个结果，于是一个把顺序写反的实现也能让全套用例变绿。
    const QString line = QStringLiteral("0x12345678-1234-1234-1234-123456789abc");

    const QString byTableOrder = applyReplacementRules(line, availableReplacementRules());
    const QString byReverseOrder = applyReplacementRules(
        line, rules({ReplacementRule::HexAddress, ReplacementRule::Guid}));

    QCOMPARE(byTableOrder, QStringLiteral("0x<GUID>"));
    QCOMPARE(byReverseOrder, QStringLiteral("<HEX>-1234-1234-1234-123456789abc"));
    QVERIFY2(byTableOrder != byReverseOrder,
             "顺序必须真的可观察，否则这条语料没有存在的意义");

    // 集合走的是同一条路：它按表顺序取出启用项，`fromRules` 的入参顺序不影响结果。
    ReplacementSet set = ReplacementSet::fromRules(
        rules({ReplacementRule::HexAddress, ReplacementRule::Guid}));
    QCOMPARE(set.enabledRules(), rules({ReplacementRule::Guid, ReplacementRule::HexAddress}));
    QCOMPARE(set.apply(line), byTableOrder);
}

void TstTextRules::stackedRulesRewriteOneLineInOnePass()
{
    // 第 4 条要的固定语料：一行里四条规则都命中，一次写成。
    const QString line = QStringLiteral(
        "1. [2024-01-01T10:20:30] user 550e8400-e29b-41d4-a716-446655440000 hit 0x7FFE0000");
    QCOMPARE(applyReplacementRules(line, availableReplacementRules()),
             QStringLiteral("<NUM>[<DATE>] user <GUID> hit <HEX>"));
}

void TstTextRules::everyMatchOnALineIsReplaced()
{
    // 同一行出现两处地址，必须**都**被吃掉：只换第一处的实现会让
    // 「一行里两处地址只有一处不同」仍被报成差异，而那正是这条规则要治的情况。
    const QVector<ReplacementRule> only = rules({ReplacementRule::HexAddress});
    QCOMPARE(applyReplacementRules(QStringLiteral("0x1F -> 0x2F"), only),
             QStringLiteral("<HEX> -> <HEX>"));

    const QVector<ReplacementRule> dates = rules({ReplacementRule::DateTime});
    QCOMPARE(applyReplacementRules(QStringLiteral("2024-01-01 与 2024-12-31"), dates),
             QStringLiteral("<DATE> 与 <DATE>"));
}

// ---------------------------------------------------------------------------
// E 与引擎的集成
// ---------------------------------------------------------------------------

void TstTextRules::compareIgnoresTheDifferencesTheRulesCover()
{
    const QVector<Line> left = lines(QStringLiteral(
        "id 550e8400-e29b-41d4-a716-446655440000\nstarted 2024-01-01\npayload\n"));
    const QVector<Line> right = lines(QStringLiteral(
        "id 11111111-2222-3333-4444-555555555555\nstarted 2025-12-31\npayload\n"));

    // 反的一面：规则关着时这两份文件本来就该有差异。少了这一步，下面那句
    // 「差异为空」可能只是因为夹具写错了。
    const Result plain = compare(left, right);
    QVERIFY2(!plain.differences.isEmpty(), "规则关闭时这份语料本来就该报出差异");

    // 规则随后在**比对前**统一作用于行内容（第 3 条）——落在 `keys()` 那一层，
    // 因此对齐算法拿到的已经是改写后的键。
    CompareOptions options;
    options.replacements = ReplacementSet::fromRules(
        rules({ReplacementRule::Guid, ReplacementRule::DateTime}));
    const Result rewritten = compare(left, right, options);
    QVERIFY2(rewritten.differences.isEmpty(), "规则命中后这两行应当被判为相同");

    // 但「相同」到手的是**忽略**而不是「原文一致」：两行的键相同、`Line` 却不同，
    // 于是走的是既有的 `Change::Ignored` 通道（`textdiff.cpp` 收尾循环里那句
    // `left[..] == right[..] ? Equal : Ignored`）。这条断言是刻意的——
    // 视图层因此不需要为替换规则新增一种状态，而用户仍然能看到「这一行被规则吃掉了」。
    // 注意 `ignoredBlocks` 数的是**块**不是行：两行被合并成了一个块。
    QCOMPARE(rewritten.ignoredBlocks, 1);

    QStringList shape;
    for (const Block &block : rewritten.blocks)
        shape << QStringLiteral("%1(左%2/右%3)").arg(static_cast<int>(block.change))
                     .arg(block.leftCount).arg(block.rightCount);
    QVERIFY2(rewritten.blocks.size() == 2, qPrintable(shape.join(QStringLiteral(", "))));
    QVERIFY(rewritten.blocks.first().change == Change::Ignored);
    QVERIFY(rewritten.blocks.last().change == Change::Equal);
}

void TstTextRules::normalizedLineRunsReplacementsBeforeCaseFolding()
{
    CompareOptions options;
    options.ignoreCase = true;
    options.replacements = ReplacementSet::fromRules(rules({ReplacementRule::LeadingNumber}));

    const QString first = normalizedLine(QStringLiteral("1. Foo"), options);
    const QString second = normalizedLine(QStringLiteral("2. foo"), options);

    // 两条断言一起钉住链上的两件事：
    //   ① 替换规则**在**规范化链里（否则 `1. Foo` 与 `2. foo` 只有编号不同，仍是差异）；
    //   ② 替换**先于**大小写折叠（占位符本身也被折成了小写）。
    // 把替换挪到折叠之后，`<NUM>` 会保持大写，第二条断言立刻红——
    // 这正是「顺序」在链上的可观察形式。
    QCOMPARE(first, second);
    QVERIFY2(first.contains(QStringLiteral("<num>")), qPrintable(first));
}

// 与 `Tests/Similarity` 一致：moc 交给 qmake 自动生成（本工程不手写 `.moc` include），
// `QTEST_APPLESS_MAIN` 在 `QT -= gui` 下用 `QCoreApplication`——本套件刻意不链接 QtGui。
QTEST_APPLESS_MAIN(TstTextRules)
