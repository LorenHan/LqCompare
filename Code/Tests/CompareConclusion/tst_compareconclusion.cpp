#include "tst_compareconclusion.h"

#include "compareconclusion.h"

#include <QStringList>

using namespace LqCompare::Text;

namespace {

Document decoded(const QByteArray &bytes)
{
    Document document;
    if (!Document::decode(bytes, &document)) qFatal("Fixture could not decode");
    return document;
}

// 造一个「两侧只差大小写、规则把它忽略了」的比对结果。
//
// 为什么用真的 `compare()` 而不是手工拼一个 `Result`：本组要验的是
// 「结论档跟着**证据**走」，而证据的形状（`ignoredBlocks` 与 `differences`
// 各自什么时候非零）只有引擎说了算。手工拼的话，本用例守的就是我自己的假设，
// 而引擎哪天改成产 `Equal` 而不是 `Ignored`，这里不会有任何东西变红。
Result ignoredOnlyResult()
{
    CompareOptions options;
    options.ignoreCase = true;
    return compare({Line{QStringLiteral("Foo"), Eol::LF}},
                   {Line{QStringLiteral("foo"), Eol::LF}}, options);
}

BomVerdict bomVerdictOf(BomConclusion conclusion)
{
    BomVerdict value;
    value.conclusion = conclusion;
    return value;
}

} // namespace

// ---------------------------------------------------------------------------
// A 结论档
// ---------------------------------------------------------------------------

void TstCompareConclusion::everyConclusionTierHasItsOwnLabel()
{
    // 三档都各有一个档名与一句说明——漏掉的档会在界面上显示成空字符串，
    // 而空标签的表格行看起来与「这一格没有内容」完全一样。
    const QVector<Conclusion> tiers{Conclusion::Identical, Conclusion::RuleIdentical,
                                    Conclusion::Different};
    QStringList labels;
    for (const Conclusion tier : tiers) {
        const QString label = conclusionLabel(tier);
        QVERIFY2(!label.isEmpty(), "每一档都必须有档名");
        QVERIFY2(!conclusionDescription(tier).isEmpty(), "每一档都必须有说明");
        // 「相同」与「规则相同」必须是**两个不同的词**。
        // 这一条是在守规格那句「不得标成字节完全一致」：两档共用一个档名时，
        // 后半句就再也无法被表达，也就无法被验证。
        QVERIFY2(!labels.contains(label), "两档不能共用同一个档名");
        labels.append(label);
    }
    QCOMPARE(labels.size(), 3);
}

void TstCompareConclusion::conclusionTiersFollowTheEvidence()
{
    // 没有任何差异、也没有 BOM 差异 ⇒ 相同。
    {
        const Result result = compare({}, {});
        QCOMPARE(conclude(result, BomVerdict{}), Conclusion::Identical);
    }
    // 有未被规则忽略的差异 ⇒ 不同。
    {
        const Result result = compare({Line{QStringLiteral("a"), Eol::LF}},
                                      {Line{QStringLiteral("b"), Eol::LF}});
        QVERIFY(!result.differences.isEmpty());
        QCOMPARE(conclude(result, BomVerdict{}), Conclusion::Different);
    }
    // 有**被规则忽略**的差异 ⇒ 规则相同，而不是相同。
    // 这一路是 `ignoredBlocks` 的用处：它是「按规则比不出差异，但原始行确实不同」
    // 的唯一证据，把它当成「相同」会声称两句不同的话其实一模一样。
    {
        const Result result = ignoredOnlyResult();
        QVERIFY2(result.differences.isEmpty(), "夹具本不该产出未被忽略的差异");
        QVERIFY2(result.ignoredBlocks > 0, "夹具必须产出被忽略的块，否则这一条验的是空气");
        QCOMPARE(conclude(result, BomVerdict{}), Conclusion::RuleIdentical);
    }
    // BOM 差异被忽略 ⇒ 规则相同（规格点名的正是这一条）。
    {
        const Result result = compare({}, {});
        QCOMPARE(conclude(result, bomVerdictOf(BomConclusion::Ignored)), Conclusion::RuleIdentical);
    }
    // BOM 差异计为不同 ⇒ 不同，**即使行级一个差异块都没有**。
    // 这一条是 `conclude()` 里那一句「BOM 那一路必须单独判」的可观察形式：
    // 只看 `differences` 的话，这里会返回「相同」，而两份文件的原始字节并不相同。
    {
        const Result result = compare({}, {});
        QVERIFY(result.differences.isEmpty());
        QCOMPARE(conclude(result, bomVerdictOf(BomConclusion::Difference)), Conclusion::Different);
    }
}

void TstCompareConclusion::ignoredDifferenceIsRuleIdenticalNotIdentical()
{
    // 规格原话：「仅 BOM 不同且策略为忽略时，比较结论为**规则相同**……
    // **不得标成字节完全一致**」。这里把「不得」落成一个可执行的否定命题：
    // 两档必须**不相等**，而且档名里那个「规则」不能被省掉。
    const Result result = compare({}, {});
    const Conclusion withBomIgnored = conclude(result, bomVerdictOf(BomConclusion::Ignored));
    const Conclusion withoutBomIssue = conclude(result, BomVerdict{});
    QVERIFY(withBomIgnored != withoutBomIssue);
    QCOMPARE(withBomIgnored, Conclusion::RuleIdentical);
    QCOMPARE(withoutBomIssue, Conclusion::Identical);
    QVERIFY(conclusionLabel(Conclusion::RuleIdentical)
            != conclusionLabel(Conclusion::Identical));
    // 「规则相同」这四个字必须真的出现在档名里：改成一个别的词
    // （例如「几乎相同」）会让规格那句话读不出对应关系。
    QVERIFY2(conclusionLabel(Conclusion::RuleIdentical).contains(QStringLiteral("规则")),
             qPrintable(conclusionLabel(Conclusion::RuleIdentical)));
    // 说明里必须点出「并非完全一样」这件事，否则用户只看到「相同」两个字。
    QVERIFY2(conclusionDescription(Conclusion::RuleIdentical).contains(QStringLiteral("并不相同")),
             qPrintable(conclusionDescription(Conclusion::RuleIdentical)));
    // 上面那条盯的是「结论档」的说明。用户在状态栏里真正读到的是
    // `bomConclusionSummary(BomConclusion::Ignored)` 那一句，它必须**自己也**说出
    // 「并非完全一样」。变异测试实测：把那一句里的「两侧的原始字节并不相同」整段
    // 删掉，全部用例依然全绿——因为状态栏那条断言是拿这个函数跟它自己比，
    // 函数改了，等式两边一起改。所以这里另钉一个**字面**断言。
    const QString summary = bomConclusionSummary(BomConclusion::Ignored);
    QVERIFY2(summary.contains(QStringLiteral("并不相同")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("忽略")), qPrintable(summary));
}

// ---------------------------------------------------------------------------
// B 比较侧策略表
// ---------------------------------------------------------------------------

void TstCompareConclusion::bomPolicyTableIsClean()
{
    QVERIFY2(validateBomPolicyTable(bomPolicyTable()).isEmpty(),
             qPrintable(validateBomPolicyTable(bomPolicyTable()).join(QLatin1Char('|'))));
    // 三档都必须是**已实现**的：规格点名要求提供三档，
    // 而 `implemented` 是「能不能选中」的唯一判据。
    QCOMPARE(availableBomPolicies().size(), 3);
    for (const BomPolicy policy : availableBomPolicies())
        QVERIFY2(!bomPolicyLabel(policy).isEmpty(), "每个可选档都必须有界面文案");
}

void TstCompareConclusion::bomPolicyTableRejectsBrokenTables()
{
    // 这一组是「护栏自证会报错」。一条永远不报的校验比没有校验更糟：
    // 它会让人以为这张表已经被守住了。
    const QVector<BomPolicyDescriptor> clean = bomPolicyTable();
    QVERIFY(validateBomPolicyTable(clean).isEmpty());

    // 空表：下拉会一个选项都没有。
    QVERIFY(!validateBomPolicyTable({}).isEmpty());

    // 重复登记同一个取值。
    QVector<BomPolicyDescriptor> duplicates = clean;
    duplicates.append({BomPolicy::Ignore, "ignore-again", true});
    QVERIFY(!validateBomPolicyTable(duplicates).isEmpty());

    // 两个取值共用一个标识符：设置键与日志会把两件事写成一件。
    QVector<BomPolicyDescriptor> shared = clean;
    shared[1].identifier = shared[0].identifier;
    QVERIFY(!validateBomPolicyTable(shared).isEmpty());

    // 标识符为空。
    QVector<BomPolicyDescriptor> nameless = clean;
    nameless[0].identifier = "";
    QVERIFY(!validateBomPolicyTable(nameless).isEmpty());

    // 漏登记一档——**只有**拿规格当期望值才发现得了：
    // 上面几条（重复、空标识符）在这张表上都还成立。
    // 表里最后一项是 `TreatAsDifference`，所以这里去掉的正是它，
    // 期望值也相应换成剩下的两档（否则这一条验的是别的东西）。
    QVector<BomPolicyDescriptor> missing = clean;
    missing.removeLast();
    QVERIFY2(!validateBomPolicyTable(missing).isEmpty(),
             "少一档必须是问题：否则「可选集合少一项」永远没人发现");
    QVERIFY(validateBomPolicyTable(missing, {BomPolicy::Automatic, BomPolicy::Ignore}).isEmpty());

    // 一条已实现的都没有：可选集合为空，默认值也不在里面。
    QVector<BomPolicyDescriptor> noneImplemented = clean;
    for (auto &descriptor : noneImplemented) descriptor.implemented = false;
    const QStringList problems = validateBomPolicyTable(noneImplemented);
    QVERIFY(problems.size() >= 2);
}

void TstCompareConclusion::bomPolicyDefaultMatchesCompareOptions()
{
    // 出厂值只能有一个来源。`CompareOptions` 里那句字面量必须与表推出来的默认档
    // 相同——不同的话，界面下拉一打开显示的就是一个与引擎实际在用的不同的档，
    // 而用户会以为「默认就是显示的那个」。
    QCOMPARE(CompareOptions().bomPolicy, defaultBomPolicy());
    QCOMPARE(defaultBomPolicy(), BomPolicy::Automatic);
    QVERIFY(availableBomPolicies().contains(defaultBomPolicy()));

    // 空表也**必须**给得出一个默认档。这一支是两条退路的终点：
    // `bomPolicyFromIdentifier()` 认不出标识符时要用它，将来从磁盘读回一份
    // 坏掉的设置时也要用它。返回一个无意义的档会把「读到坏数据」变成
    // 「界面上一片空白」。此处此前**没有任何用例喂过空表**——变异测试实测：
    // 把这两个硬编码回退值改掉（`Automatic` → `Ignore`、`Preserve` → `AlwaysWrite`），
    // 整套用例依然全绿。
    QCOMPARE(defaultBomPolicy({}), BomPolicy::Automatic);
    QCOMPARE(defaultBomSavePolicy({}), BomSavePolicy::Preserve);
}

void TstCompareConclusion::bomPolicyIdentifiersRoundTrip()
{
    for (const BomPolicy policy : availableBomPolicies()) {
        const char *identifier = bomPolicyIdentifier(policy);
        QVERIFY2(identifier != nullptr, "每个可选档都必须有标识符");
        QVERIFY2(*identifier != '\0', "标识符不能是空串");
        BomPolicy parsed = BomPolicy::TreatAsDifference;
        QVERIFY2(bomPolicyFromIdentifier(QString::fromLatin1(identifier), &parsed),
                 "标识符必须能反查回同一个取值");
        QCOMPARE(parsed, policy);
    }
    // 表外标识符：返回 false 并退回默认档，**不猜**。
    BomPolicy parsed = BomPolicy::Ignore;
    QVERIFY(!bomPolicyFromIdentifier(QStringLiteral("no-such-policy"), &parsed));
    QCOMPARE(parsed, defaultBomPolicy());
    // 空串也是表外。
    QVERIFY(!bomPolicyFromIdentifier(QString(), nullptr));
    // `result` 可为空：调用方只是想知道「认不认识」时不必先备一个变量。
    QVERIFY(!bomPolicyFromIdentifier(QStringLiteral("__"), nullptr));
}

// ---------------------------------------------------------------------------
// C BOM 差异判定
// ---------------------------------------------------------------------------

void TstCompareConclusion::identicalBomStateYieldsNoVerdict()
{
    // 三个策略都要：**两侧 BOM 状态相同 ⇒ 这一维没有差异可谈**。
    // 若某个策略在这个输入上仍然报「有差异」，那它比的就不是 BOM 了。
    for (const BomPolicy policy : availableBomPolicies()) {
        BomObservation both{};
        both.leftAvailable = both.rightAvailable = true;
        both.leftHasBom = both.rightHasBom = true;
        both.leftCodec = both.rightCodec = "UTF-8";
        QVERIFY(!evaluateBom(policy, both).differs());

        BomObservation neither = both;
        neither.leftHasBom = neither.rightHasBom = false;
        QVERIFY(!evaluateBom(policy, neither).differs());

        // 两侧都是 UTF-16LE 且都带 BOM —— 编码相同、BOM 状态相同，同样没有差异。
        BomObservation utf16 = both;
        utf16.leftCodec = utf16.rightCodec = "UTF-16LE";
        QVERIFY(!evaluateBom(policy, utf16).differs());
    }
}

void TstCompareConclusion::incompleteObservationYieldsNoVerdict()
{
    // 「还没打开」与「两侧 BOM 状态相同」是两件事：前者不下结论。
    // 不区分的话，状态栏会在打开之前就印出一句关于 BOM 的话。
    for (const BomPolicy policy : availableBomPolicies()) {
        BomObservation observation{};
        observation.leftAvailable = false;
        observation.rightAvailable = true;
        observation.leftHasBom = true; // 就算事实看着像「有差异」
        QVERIFY(!evaluateBom(policy, observation).differs());
        QVERIFY(!evaluateBom(policy, BomObservation{}).differs());
    }
}

void TstCompareConclusion::ignorePolicyNeverCountsABomDifference()
{
    // 无论编码是什么、哪一侧带 BOM，`Ignore` 都只给「被忽略」这一档，
    // 绝不给「差异」。
    const QVector<QByteArray> codecs{"UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE", "GBK"};
    for (const QByteArray &codec : codecs) {
        for (const bool leftHasBom : {true, false}) {
            BomObservation observation;
            observation.leftAvailable = observation.rightAvailable = true;
            observation.leftHasBom = leftHasBom;
            observation.rightHasBom = !leftHasBom;
            observation.leftCodec = codec;
            observation.rightCodec = codec;
            const BomVerdict verdict = evaluateBom(BomPolicy::Ignore, observation);
            QCOMPARE(verdict.conclusion, BomConclusion::Ignored);
            QVERIFY(verdict.ignored());
            QVERIFY(!verdict.countedAsDifference());
        }
    }
}

void TstCompareConclusion::treatAsDifferenceAlwaysCountsABomDifference()
{
    const QVector<QByteArray> codecs{"UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE", "GBK"};
    for (const QByteArray &codec : codecs) {
        for (const bool leftHasBom : {true, false}) {
            BomObservation observation;
            observation.leftAvailable = observation.rightAvailable = true;
            observation.leftHasBom = leftHasBom;
            observation.rightHasBom = !leftHasBom;
            observation.leftCodec = codec;
            observation.rightCodec = codec;
            const BomVerdict verdict = evaluateBom(BomPolicy::TreatAsDifference, observation);
            QCOMPARE(verdict.conclusion, BomConclusion::Difference);
            QVERIFY(verdict.countedAsDifference());
            QVERIFY(!verdict.ignored());
        }
    }
}

void TstCompareConclusion::automaticIgnoresTheRedundantUtf8Bom()
{
    // UTF-8 没有字节序问题，它的 BOM 是一个可有可无的标记：
    // `Automatic` 下这个差异不计较。
    // 先钉住前提——「UTF-8 的 BOM 不是编码的一部分」这句话本身。
    QVERIFY(!bomIsEncodingCritical("UTF-8"));
    QVERIFY(!bomIsEncodingCritical("GBK"));
    QVERIFY(!bomIsEncodingCritical("Big5"));
    QVERIFY(!bomIsEncodingCritical("ISO-8859-1"));
    // 错拼的编码名**不得**被当成「BOM 是关键信息」——那会让一个本不该带 BOM 的
    // 编码把差异报出来。白名单之外一律 false。
    QVERIFY(!bomIsEncodingCritical("utf-8"));
    QVERIFY(!bomIsEncodingCritical("UTF8"));
    QVERIFY(!bomIsEncodingCritical(QByteArray()));

    BomObservation observation;
    observation.leftAvailable = observation.rightAvailable = true;
    observation.leftHasBom = true;
    observation.rightHasBom = false;
    observation.leftCodec = observation.rightCodec = "UTF-8";
    QCOMPARE(evaluateBom(BomPolicy::Automatic, observation).conclusion, BomConclusion::Ignored);

    // 一侧 UTF-8、另一侧 GBK：BOM 的有无也不是这个差异的来源
    // （真正的差异是编码，那一维由状态栏另一句话报）。
    BomObservation legacy = observation;
    legacy.rightCodec = "GB18030";
    QCOMPARE(evaluateBom(BomPolicy::Automatic, legacy).conclusion, BomConclusion::Ignored);
}

void TstCompareConclusion::automaticCountsTheByteOrderMarks()
{
    // UTF-16 / UTF-32 的 BOM **就是**字节序声明，去掉之后同样的字节有两种读法。
    QVERIFY(bomIsEncodingCritical("UTF-16LE"));
    QVERIFY(bomIsEncodingCritical("UTF-16BE"));
    QVERIFY(bomIsEncodingCritical("UTF-32LE"));
    QVERIFY(bomIsEncodingCritical("UTF-32BE"));

    const QVector<QByteArray> critical{"UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE"};
    for (const QByteArray &codec : critical) {
        BomObservation observation;
        observation.leftAvailable = observation.rightAvailable = true;
        observation.leftHasBom = true;
        observation.rightHasBom = false;
        observation.leftCodec = observation.rightCodec = codec;
        QCOMPARE(evaluateBom(BomPolicy::Automatic, observation).conclusion,
                 BomConclusion::Difference);

        // **只看一侧不够**：一侧是 UTF-16LE（BOM 必需）、另一侧是 UTF-8（BOM 冗余）时，
        // 少的那个 BOM 让「这份文件按什么读」从确定变成了猜测，仍然算差异。
        //
        // 两个方向都必须喂。原先这里只有「把右侧降级成 UTF-8」这一行，而它恰好把
        // **关键编码留在左侧**——于是「只看左侧编码」这个实现照样返回 Difference、
        // 用例全绿。变异测试实测：把 `|| bomIsEncodingCritical(rightCodec)` 删掉时，
        // 整套用例仍然 29 passed / 0 failed。下面第二行才是真判据。
        BomObservation leftCritical = observation;
        leftCritical.rightCodec = "UTF-8";
        QCOMPARE(evaluateBom(BomPolicy::Automatic, leftCritical).conclusion,
                 BomConclusion::Difference);

        // 镜像方向：**只有右侧**是字节序关键编码。少了这一行，「任一侧」里的
        // 「任」字就没有任何东西守着。
        BomObservation rightCritical = observation;
        rightCritical.leftCodec = "UTF-8";
        rightCritical.rightCodec = codec;
        QCOMPARE(evaluateBom(BomPolicy::Automatic, rightCritical).conclusion,
                 BomConclusion::Difference);
    }

    // 两侧都带 BOM 但编码不同 ⇒ BOM 状态相同，这一维没有差异可谈
    // （编码差异由状态栏另一句话负责，不能在这里重复计一次）。
    BomObservation bothBom;
    bothBom.leftAvailable = bothBom.rightAvailable = true;
    bothBom.leftHasBom = bothBom.rightHasBom = true;
    bothBom.leftCodec = "UTF-16LE";
    bothBom.rightCodec = "UTF-16BE";
    QVERIFY(!evaluateBom(BomPolicy::Automatic, bothBom).differs());
}

// ---------------------------------------------------------------------------
// D 保存侧策略
// ---------------------------------------------------------------------------

void TstCompareConclusion::bomSavePolicyTableIsClean()
{
    QVERIFY2(validateBomSavePolicyTable(bomSavePolicyTable()).isEmpty(),
             qPrintable(validateBomSavePolicyTable(bomSavePolicyTable()).join(QLatin1Char('|'))));
    QCOMPARE(availableBomSavePolicies().size(), 3);
    QCOMPARE(defaultBomSavePolicy(), BomSavePolicy::Preserve);
    for (const BomSavePolicy policy : availableBomSavePolicies())
        QVERIFY2(!bomSavePolicyLabel(policy).isEmpty(), "每个可选档都必须有界面文案");
}

void TstCompareConclusion::bomSavePolicyTableRejectsBrokenTables()
{
    const QVector<BomSavePolicyDescriptor> clean = bomSavePolicyTable();
    QVERIFY(validateBomSavePolicyTable(clean).isEmpty());
    QVERIFY(!validateBomSavePolicyTable({}).isEmpty());

    QVector<BomSavePolicyDescriptor> duplicates = clean;
    duplicates.append({BomSavePolicy::Preserve, "preserve-again", true});
    QVERIFY(!validateBomSavePolicyTable(duplicates).isEmpty());

    QVector<BomSavePolicyDescriptor> shared = clean;
    shared[1].identifier = shared[0].identifier;
    QVERIFY(!validateBomSavePolicyTable(shared).isEmpty());

    QVector<BomSavePolicyDescriptor> nameless = clean;
    nameless[0].identifier = "";
    QVERIFY(!validateBomSavePolicyTable(nameless).isEmpty());

    QVector<BomSavePolicyDescriptor> missing = clean;
    missing.removeLast();
    QVERIFY(!validateBomSavePolicyTable(missing).isEmpty());
    QVERIFY(validateBomSavePolicyTable(missing, {BomSavePolicy::Preserve, BomSavePolicy::AlwaysWrite})
                .isEmpty());

    for (const BomSavePolicy policy : availableBomSavePolicies())
        QVERIFY2(bomSavePolicyIdentifier(policy) != nullptr, "每个档都必须有标识符");
}

void TstCompareConclusion::bomSavePolicyAppliesRejectsImpossibleCombinations()
{
    // `NeverWrite` 对 UTF-16 / UTF-32 不成立：BOM 就是字节序声明，
    // 去掉之后文件不再可读。这条判定抽出来的理由就是让这句话能被直接问。
    QVERIFY(!bomSavePolicyApplies(BomSavePolicy::NeverWrite, "UTF-16LE"));
    QVERIFY(!bomSavePolicyApplies(BomSavePolicy::NeverWrite, "UTF-16BE"));
    QVERIFY(!bomSavePolicyApplies(BomSavePolicy::NeverWrite, "UTF-32LE"));
    QVERIFY(!bomSavePolicyApplies(BomSavePolicy::NeverWrite, "UTF-32BE"));
    QVERIFY(bomSavePolicyApplies(BomSavePolicy::NeverWrite, "UTF-8"));

    // `AlwaysWrite` 也不是到处都成立：对 GBK 写一段 BOM 字节，
    // 结果是文件头多出几个乱码——那三个字节在那里是**内容**，不是标记。
    QVERIFY(!bomSavePolicyApplies(BomSavePolicy::AlwaysWrite, "GBK"));
    QVERIFY(!bomSavePolicyApplies(BomSavePolicy::AlwaysWrite, "ISO-8859-1"));
    QVERIFY(bomSavePolicyApplies(BomSavePolicy::AlwaysWrite, "UTF-8"));
    QVERIFY(bomSavePolicyApplies(BomSavePolicy::AlwaysWrite, "UTF-16LE"));

    // `Preserve` 永远「做得到」——它要的是「什么都不改」。
    const QVector<QByteArray> codecs{"UTF-8", "UTF-16LE", "GBK", "Big5", QByteArray()};
    for (const QByteArray &codec : codecs)
        QVERIFY(bomSavePolicyApplies(BomSavePolicy::Preserve, codec));
}

void TstCompareConclusion::bomShouldBeWrittenHonoursEveryPolicy()
{
    struct Row {
        BomSavePolicy policy;
        bool originalHasBom;
        QByteArray codec;
        bool expected;
        const char *why;
    };
    const QVector<Row> rows{
        {BomSavePolicy::Preserve, true, "UTF-8", true, "保留：原来就有"},
        {BomSavePolicy::Preserve, false, "UTF-8", false, "保留：原来就没有"},
        {BomSavePolicy::Preserve, true, "UTF-16LE", true, "保留：原来就有（关键编码）"},
        {BomSavePolicy::AlwaysWrite, false, "UTF-8", true, "强制写"},
        {BomSavePolicy::AlwaysWrite, true, "UTF-8", true, "强制写"},
        {BomSavePolicy::NeverWrite, true, "UTF-8", false, "强制不写"},
        {BomSavePolicy::NeverWrite, false, "UTF-8", false, "强制不写"},
        {BomSavePolicy::NeverWrite, false, "UTF-16BE", false, "本来就没有，没什么可降级"},
        // 下面两行是**降级**：做不到时按保留走，而不是照办写出一个打不开的文件。
        {BomSavePolicy::NeverWrite, true, "UTF-16LE", true, "降级：字节序声明去掉就读不了"},
        {BomSavePolicy::NeverWrite, true, "UTF-32BE", true, "降级：同上"},
        // 下面两行是**另一个方向**的做不到：编码承载不了 BOM。
        {BomSavePolicy::AlwaysWrite, false, "GBK", false, "编码没有 BOM 可写"},
        {BomSavePolicy::Preserve, true, "GBK", false, "原来是 UTF-8 的 BOM，改成 GBK 之后写不进去"},
    };
    for (const Row &row : rows) {
        QVERIFY2(bomShouldBeWritten(row.policy, row.originalHasBom, row.codec) == row.expected,
                 qPrintable(QStringLiteral("%1（策略 %2，原文件 %3，编码 %4）")
                                .arg(QString::fromLatin1(row.why))
                                .arg(static_cast<int>(row.policy))
                                .arg(row.originalHasBom ? 1 : 0)
                                .arg(QString::fromLatin1(row.codec))));
    }
}

void TstCompareConclusion::documentWritesTheBomItsPolicyAsksFor()
{
    // 带 BOM 的 UTF-8 文件：`Preserve` 原样回写（逐字节相同），
    // `NeverWrite` 去掉那三个字节，`AlwaysWrite` 保持。
    const QByteArray withBom = QByteArray::fromHex("efbbbfe4b8ade696870a");
    Document document = decoded(withBom);
    QVERIFY(document.hasBom());
    QCOMPARE(document.codecName(), QByteArray("UTF-8"));
    QCOMPARE(document.bytes(), withBom); // Preserve 是出厂值
    QVERIFY(document.bomWillBeWritten());
    QVERIFY(!document.isModified());

    document.setBomSavePolicy(BomSavePolicy::NeverWrite);
    QVERIFY(!document.bomWillBeWritten());
    QCOMPARE(document.bytes(), QByteArray::fromHex("e4b8ade696870a"));
    // 去掉 BOM 就是一次真实的改动：不把脏标记抬起来的话，Save 按钮会是灰的，
    // 用户改完策略反而存不下去。
    QVERIFY(document.isModified());

    document.setBomSavePolicy(BomSavePolicy::AlwaysWrite);
    QVERIFY(document.bomWillBeWritten());
    QCOMPARE(document.bytes(), withBom);
    QVERIFY(!document.isModified());

    // 不带 BOM 的 UTF-8 文件：`AlwaysWrite` 加上它。
    Document plain = decoded(QByteArray::fromHex("e4b8ade696870a"));
    QVERIFY(!plain.hasBom());
    QCOMPARE(plain.bytes(), QByteArray::fromHex("e4b8ade696870a"));
    plain.setBomSavePolicy(BomSavePolicy::AlwaysWrite);
    QVERIFY(plain.bomWillBeWritten());
    QCOMPARE(plain.bytes(), withBom);
    plain.setBomSavePolicy(BomSavePolicy::NeverWrite);
    QCOMPARE(plain.bytes(), QByteArray::fromHex("e4b8ade696870a"));
    QVERIFY(!plain.isModified());

    // 每个编码写出来的那段字节必须就是该编码的 BOM——
    // 逐字节钉住，因为「写错编码的 BOM」是一个能生成打不开的文件的错法。
    QCOMPARE(bomBytesForCodec("UTF-8"), QByteArray::fromHex("efbbbf"));
    QCOMPARE(bomBytesForCodec("UTF-16LE"), QByteArray::fromHex("fffe"));
    QCOMPARE(bomBytesForCodec("UTF-16BE"), QByteArray::fromHex("feff"));
    QCOMPARE(bomBytesForCodec("UTF-32LE"), QByteArray::fromHex("fffe0000"));
    QCOMPARE(bomBytesForCodec("UTF-32BE"), QByteArray::fromHex("0000feff"));
    // 表外编码不猜。
    QVERIFY(bomBytesForCodec("GBK").isEmpty());
    QVERIFY(bomBytesForCodec(QByteArray()).isEmpty());
}

void TstCompareConclusion::removingTheBomOfAByteOrderMarkedEncodingIsRefused()
{
    // 这一条守的是 `bomShouldBeWritten()` 里那句「降级成保留」。
    // 它很容易被「修」掉——把 `NeverWrite` 一律照办的写法更短，也更符合直觉，
    // 而代价是写出一份**字节序不明**的文件：同一次保存之后，
    // 下次打开可能解成另一串字符。
    const QVector<QByteArray> critical{
        QByteArray::fromHex("fffe2d4e87650d000a00"), // UTF-16LE "中文\r\n"
        QByteArray::fromHex("feff4e2d6587000d000a"), // UTF-16BE，同上
    };
    for (const QByteArray &bytes : critical) {
        Document document = decoded(bytes);
        QVERIFY(document.hasBom());
        QVERIFY(bomIsEncodingCritical(document.codecName()));
        document.setBomSavePolicy(BomSavePolicy::NeverWrite);
        // 策略做不到，于是 BOM 仍然会写出来，而且**文件一个字节都没变**。
        QVERIFY2(document.bomWillBeWritten(),
                 "字节序声明的编码不允许去掉 BOM：写出去的文件会读不回来");
        QCOMPARE(document.bytes(), bytes);
        QVERIFY(!document.isModified());
    }
}

// ---------------------------------------------------------------------------
// E 编码判定：UTF-8 BOM 绝不会被读成 UTF-16
// ---------------------------------------------------------------------------

void TstCompareConclusion::utf8BomIsNeverReadAsUtf16_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QByteArray>("codec");
    QTest::addColumn<bool>("hasBom");
    // 空的期望文本表示「这一行只验编码与 BOM 标记」——有些对抗性输入本来
    // 就解不出可读文本（UTF-8 里出现 FF 就是非法字节），而那些行的用意正是
    // 「即便内容解不动，也不能把它的**编码判错**」。
    QTest::addColumn<QString>("text");

    const QString chinese = QStringLiteral("中文");
    QTest::newRow("no-bom") << QByteArray::fromHex("e4b8ade696870a") << QByteArray("UTF-8")
                            << false << (chinese + QLatin1Char('\n'));
    QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbfe4b8ade696870a") << QByteArray("UTF-8")
                              << true << (chinese + QLatin1Char('\n'));
    // **两个 BOM**：第二个是内容里的 U+FEFF，不是第二个标记。这一行专门盯住
    // 「只剥一次」——剥两次的实现会把内容吃掉一个字符。
    QTest::newRow("double-utf8-bom")
        << QByteArray::fromHex("efbbbfefbbbfe4b8ad") << QByteArray("UTF-8") << true
        << (QString(QChar(0xFEFF)) + QStringLiteral("中"));
    // 最刁的一行：UTF-8 的 BOM 后面紧跟 UTF-16LE 的 BOM 字节。
    // 按「先长后短」的顺序判，它仍然是 UTF-8 的 `EF BB BF`——
    // 若哪天有人把 `fffe` 那一条挪到前面，这里会立刻变成 UTF-16LE。
    QTest::newRow("utf8-bom-then-fffe") << QByteArray::fromHex("efbbbffffe6162")
                                        << QByteArray("UTF-8") << true << QString();
    // 反向：真的 UTF-16 BOM 不能被当成 UTF-8。
    QTest::newRow("utf16le-bom") << QByteArray::fromHex("fffe2d4e87650a00") << QByteArray("UTF-16LE")
                                 << true << (chinese + QLatin1Char('\n'));
    QTest::newRow("utf16be-bom") << QByteArray::fromHex("feff4e2d6587000a") << QByteArray("UTF-16BE")
                                 << true << (chinese + QLatin1Char('\n'));
    // UTF-32LE 的 BOM 前两字节与 UTF-16LE 相同，必须按**最长**那个认。
    QTest::newRow("utf32le-bom")
        << QByteArray::fromHex("fffe00002d4e0000876500000a000000") << QByteArray("UTF-32LE") << true
        << (chinese + QLatin1Char('\n'));
    QTest::newRow("utf32be-bom")
        << QByteArray::fromHex("0000feff00004e2d000065870000000a") << QByteArray("UTF-32BE") << true
        << (chinese + QLatin1Char('\n'));
}

void TstCompareConclusion::utf8BomIsNeverReadAsUtf16()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QByteArray, codec);
    QFETCH(bool, hasBom);
    QFETCH(QString, text);

    // 判之前先确认这一行**恰好有一个** BOM：没有的话「没被误判」验的是空气，
    // 有两个的话下面那条「剥一次就够」的断言会换个含义。
    Document document = decoded(bytes);
    QCOMPARE(document.codecName(), codec);
    QCOMPARE(document.hasBom(), hasBom);
    if (!document.hasBom()) {
        QVERIFY(!document.bomWillBeWritten());
    } else {
        // 判出来的编码必须**就是那个 BOM 指名的那一个**——这就是
        // 「UTF-8 BOM 被误判为 UTF-16 的情况不会发生」的可执行形式。
        QVERIFY2(bomBytesForCodec(document.codecName()) == bytes.left(bomBytesForCodec(document.codecName()).size()),
                 qPrintable(QStringLiteral("判出来的编码 %1 与开头的 BOM 字节不匹配")
                                .arg(QString::fromLatin1(document.codecName()))));
        // 并且这个编码要不要 BOM，与「是不是 UTF-8」这件事一致：
        // UTF-8 的 BOM 是冗余标记，UTF-16/UTF-32 的不是。
        QCOMPARE(bomIsEncodingCritical(document.codecName()),
                 document.codecName() != QByteArray("UTF-8"));
    }
    if (!text.isEmpty()) QCOMPARE(document.normalizedText(), text);

    // UTF-8 带 BOM 的那几行再走一步：把同样的内容**不带 BOM** 解一遍，
    // 两次必须得到同一段文本。这正是「UTF-8 的 BOM 不参与解字符」那句话——
    // 它一旦被当成 UTF-16 的标记，这一条会给出完全不同的结果。
    //
    // `double-utf8-bom` 那一行刻意跳过：它的载荷本身就以 `EF BB BF` 开头，
    // 「去掉一个之后就一个都不剩了」对它并不成立（那一行由上面的文本断言
    // 负责证明**只剥了一次**）。在这里硬跑会让 `!withoutBom.hasBom()` 恒假，
    // 而那种失败看起来像「UTF-8 被判成了带 BOM 的编码」，方向完全跑偏。
    if (codec == QByteArray("UTF-8") && hasBom && !text.isEmpty()) {
        const QByteArray payload = bytes.mid(3);
        if (!payload.startsWith(QByteArray::fromHex("efbbbf"))) {
            Document withoutBom = decoded(payload);
            QCOMPARE(withoutBom.codecName(), QByteArray("UTF-8"));
            QVERIFY(!withoutBom.hasBom());
            QCOMPARE(withoutBom.normalizedText(), document.normalizedText());
        }
    }
}

QTEST_APPLESS_MAIN(TstCompareConclusion)
