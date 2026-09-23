#include "tst_statuspalette.h"

#include "statuspalette.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <functional>

using namespace LqCompare;

namespace {

// 一套方案里改动一处色值的最小手段：按状态替换浅色或深色档。
// 用例里反复要「把某一档弄坏」，写九行字面量会在下一次新增状态时整体过期。
bool replaceColor(Folder::ColorScheme &scheme, Folder::Status status, const QString &light,
                  const QString &dark)
{
    for (auto &highlight : scheme.highlights) {
        if (highlight.status != status)
            continue;
        if (!light.isNull())
            highlight.light = light;
        if (!dark.isNull())
            highlight.dark = dark;
        return true;
    }
    return false;
}

// 出厂表的一份可改副本。`colorSchemeTable()` 返回的是 const 引用，
// 而动它的唯一合法方式是复制一份——用例改的必须是自己手里这份。
QVector<Folder::ColorScheme> factoryCopy()
{
    return Folder::colorSchemeTable();
}

bool containsProblem(const QStringList &problems, const QString &needle)
{
    for (const QString &problem : problems) {
        if (problem.contains(needle))
            return true;
    }
    return false;
}

QString statusListText(const QStringList &problems)
{
    return problems.join(QStringLiteral(" | "));
}

} // namespace

// ---------------------------------------------------------------------------
// A 组：第 1 条
// ---------------------------------------------------------------------------

void StatusPaletteTests::everySchemeColoursEveryStatusInBothThemes()
{
    // 「每类状态有默认颜色」的可执行形式：不是「表里有九行」，
    // 而是「拿每一档去问，两级主题**各**有一个非空色值」。
    // 只数行数的话，九行全指向同一档状态照样能过。
    for (const auto &scheme : Folder::colorSchemeTable()) {
        QVERIFY2(scheme.coversEveryStatus(), qPrintable(scheme.identifier));
        for (const auto &descriptor : Folder::mainStatusTable()) {
            const QString light = scheme.colorFor(descriptor.value, false);
            const QString dark = scheme.colorFor(descriptor.value, true);
            QVERIFY2(!light.isEmpty(),
                     qPrintable(QStringLiteral("%1/%2 浅色档为空")
                                    .arg(scheme.identifier, Folder::statusIdentifier(descriptor.value))));
            QVERIFY2(!dark.isEmpty(),
                     qPrintable(QStringLiteral("%1/%2 深色档为空")
                                    .arg(scheme.identifier, Folder::statusIdentifier(descriptor.value))));
            // 色值必须真是 #rrggbb：只断言「非空」时，一个 `"#"` 也能过，
            // 而它在界面上是**黑色**——正好是最容易看着像对的一种错。
            QVERIFY2(Folder::contrastRatio(light, scheme.lightBackground) > 0.0, qPrintable(light));
            QVERIFY2(Folder::contrastRatio(dark, scheme.darkBackground) > 0.0, qPrintable(dark));
        }
    }
}

void StatusPaletteTests::everyStatusOwnsAnIconKeySoColourIsNeverTheOnlyCarrier()
{
    // 第 3 条要的是「图标与文字必须同时存在」。图标键住在状态表里（DIR-011），
    // 因此这条判据是**跨表**的：这里同时钉住「每档非空」与「九档互不相同」——
    // 九档共用同一个图标文件等于没有图标，而它是非空的，只查非空发现不了。
    QSet<QString> keys;
    for (const auto &descriptor : Folder::mainStatusTable()) {
        const QString key = Folder::statusIconKey(descriptor.value);
        QVERIFY2(!key.isEmpty(), qPrintable(Folder::statusIdentifier(descriptor.value)));
        keys.insert(key);
        // 文字那一半：状态标签也不能为空。
        QVERIFY(!Folder::statusLabel(descriptor.value).isEmpty());
    }
    QCOMPARE(keys.size(), Folder::mainStatusTable().size());
}

void StatusPaletteTests::lightAndDarkVariantsActuallyDiffer()
{
    // 第 1 条后半句「图标随主题切换自动适配深浅」的可执行形式。
    //
    // 浅深两档写成同一个值是一个**很自然的疏漏**（复制一行改一半），
    // 而它在浅色主题下完全看不出来：深色档只有切到深色主题才被读。
    // 判据是「两级不等」——它不要求差多少，只要求存在两档这件事是真的。
    for (const auto &scheme : Folder::colorSchemeTable()) {
        for (const auto &descriptor : Folder::mainStatusTable()) {
            const QString light = scheme.colorFor(descriptor.value, false);
            const QString dark = scheme.colorFor(descriptor.value, true);
            QVERIFY2(light != dark,
                     qPrintable(QStringLiteral("%1/%2 的浅深两档同色，等于没有深色档")
                                    .arg(scheme.identifier, Folder::statusIdentifier(descriptor.value))));
        }
        QVERIFY2(scheme.excludedLight != scheme.excludedDark,
                 qPrintable(scheme.identifier + QStringLiteral(" 的「已排除」浅深两档同色")));
    }
}

void StatusPaletteTests::theTwoReferenceBackgroundsAreOppositeInLightness()
{
    for (const auto &scheme : Folder::colorSchemeTable()) {
        const double light = Folder::relativeLuminance(scheme.lightBackground);
        const double dark = Folder::relativeLuminance(scheme.darkBackground);
        QVERIFY(light > 0.5);
        QVERIFY(dark < 0.5);
    }
    // 参考背景本身要能算：非法输入必须返回 -1 而不是 0（0 是纯黑的合法亮度，
    // 拿它当哨兵会让「颜色写错了」被读成「这个颜色很暗」）。
    QCOMPARE(Folder::relativeLuminance(QStringLiteral("not-a-colour")), -1.0);
    QCOMPARE(Folder::relativeLuminance(QStringLiteral("#abc")), -1.0);
    QCOMPARE(Folder::relativeLuminance(QStringLiteral("#11223344")), -1.0);
    QCOMPARE(Folder::contrastRatio(QStringLiteral("#000000"), QStringLiteral("#zzzzzz")), -1.0);
    // 已知值：黑白是 21:1，同色是 1:1。
    QVERIFY(qAbs(Folder::contrastRatio(QStringLiteral("#000000"), QStringLiteral("#ffffff")) - 21.0) < 0.01);
    QVERIFY(qAbs(Folder::contrastRatio(QStringLiteral("#895000"), QStringLiteral("#895000")) - 1.0) < 0.001);
}

void StatusPaletteTests::aSchemeIsJudgedOnItsNameAndItsReferenceBackgroundsToo()
{
    // 本条守的是 `validateOneColorScheme()` 里四条**曾经没有任何输入能走到**的分支。
    // 扫法：把函数里每一条 `problems <<` 的中文片段当针，在整个测试集里搜一遍——
    // 搜不到就说明那条分支没人喂过表。不是「代码写错了」，是「判据没人验过」，
    // 而它的症状是「改了校验却没红」，下一轮会以为那是纵深防御。
    //
    // ① 缺展示名 / ② 缺说明。两者都是 `isEmpty()` 判据，看起来「显然成立」，
    //    但下拉里出现一个空白条目时用户认不出那是哪一套，也没法报出名字来。
    QVector<Folder::ColorScheme> nameless = factoryCopy();
    nameless[1].displayName = QString();
    QStringList problems = Folder::validateColorSchemeTable(nameless);
    QVERIFY2(containsProblem(problems, QStringLiteral("缺展示名")), qPrintable(statusListText(problems)));

    QVector<Folder::ColorScheme> undocumented = factoryCopy();
    undocumented[1].description = QString();
    problems = Folder::validateColorSchemeTable(undocumented);
    QVERIFY2(containsProblem(problems, QStringLiteral("缺说明")), qPrintable(statusListText(problems)));

    // ③ 参考背景不是 `#rrggbb`。「参考背景」这两个色值不参与渲染，
    //    因此它们是最容易被当成「随便填两个」的字段——而对比度判据全靠它们，
    //    填错的后果是**所有**对比度检查一起变成对着一个错背景算的数。
    QVector<Folder::ColorScheme> badBackground = factoryCopy();
    badBackground[1].lightBackground = QStringLiteral("white");
    problems = Folder::validateColorSchemeTable(badBackground);
    QVERIFY2(containsProblem(problems, QStringLiteral("参考背景")), qPrintable(statusListText(problems)));

    // ④ 两档背景不呈一明一暗。都亮意味着这套方案根本没有深色主题那一档，
    //    第 1 条「图标随主题切换自动适配深浅」当场落空，而运行期没有别的现象
    //    （深色主题下只是有几行字看不清）。
    QVector<Folder::ColorScheme> twoLightBackgrounds = factoryCopy();
    twoLightBackgrounds[1].darkBackground = QStringLiteral("#f2f2f2");
    problems = Folder::validateColorSchemeTable(twoLightBackgrounds);
    QVERIFY2(containsProblem(problems, QStringLiteral("一明一暗")),
             qPrintable(statusListText(problems)));

    // 反向：出厂表四条都不报，判据没有误伤。
    const QStringList clean = Folder::validateColorSchemeTable(Folder::colorSchemeTable());
    for (const QString &needle : {QStringLiteral("缺展示名"), QStringLiteral("缺说明"),
                                  QStringLiteral("参考背景"), QStringLiteral("一明一暗")}) {
        QVERIFY2(!containsProblem(clean, needle), qPrintable(statusListText(clean)));
    }
}

void StatusPaletteTests::unknownSchemeIdentifierFallsBackToTheFactoryDefault()
{
    // 标识符有三个来源（设置键、配色文件、界面数据）。任一处写错或来自旧版本时
    // 界面会拿到一套没有颜色的方案，表现为「整张列表退化成一种颜色」。
    // 判据是「**总是**给得出一套能用的方案」，而不是「认不出来就返回空」。
    QVERIFY(Folder::isKnownColorScheme(Folder::defaultColorSchemeIdentifier()));
    QVERIFY(!Folder::isKnownColorScheme(QStringLiteral("no-such-scheme")));
    const Folder::ColorScheme &fallback = Folder::colorSchemeByIdentifier(QStringLiteral("no-such-scheme"));
    QCOMPARE(fallback.identifier, Folder::defaultColorSchemeIdentifier());
    QVERIFY(fallback.coversEveryStatus());
    // 空字符串（设置里没写过、或文件里字段缺失）同样要回落，不能当成"没有配色"。
    QCOMPARE(Folder::colorSchemeByIdentifier(QString()).identifier,
             Folder::defaultColorSchemeIdentifier());
    // 三套都能按标识符取回自己，不是「全都回落到默认」。
    for (const auto &scheme : Folder::colorSchemeTable())
        QCOMPARE(Folder::colorSchemeByIdentifier(scheme.identifier).identifier, scheme.identifier);
}

// ---------------------------------------------------------------------------
// B 组：第 2 条
// ---------------------------------------------------------------------------

void StatusPaletteTests::theTableShipsTheThreeNamedSchemes()
{
    const auto &table = Folder::colorSchemeTable();
    QVERIFY(table.size() >= 3);
    QStringList identifiers;
    for (const auto &scheme : table)
        identifiers << scheme.identifier;
    const QSet<QString> expected{QStringLiteral("default"), QStringLiteral("high-contrast"),
                                 QStringLiteral("color-blind-safe")};
    QCOMPARE(QSet<QString>(identifiers.cbegin(), identifiers.cend()), expected);
    // 第一套是出厂默认：界面初始值、缺省设置与配色文件回退都取它。
    QCOMPARE(table.first().identifier, Folder::defaultColorSchemeIdentifier());
}

void StatusPaletteTests::theFactoryTablePassesItsOwnValidator()
{
    // 出厂表必须自己先合规。这条用例在变异测试里是**第一道**：
    // 任何把某档色值改坏、把覆盖改少、把对比度调低的变异都要先让它红。
    const QStringList problems = Folder::validateColorSchemeTable(Folder::colorSchemeTable());
    QVERIFY2(problems.isEmpty(), qPrintable(statusListText(problems)));
    // 出厂三套的说明与展示名都不能是空壳。
    for (const auto &scheme : Folder::colorSchemeTable()) {
        QVERIFY(!scheme.displayName.isEmpty());
        QVERIFY(!scheme.description.isEmpty());
    }
}

void StatusPaletteTests::theHighContrastNameIsBackedByMeasuredAAA()
{
    // 「高对比」如果只是一个标识符，那这三个字就没被验过。
    // 判据：它声明的阈值到 AAA，而且**九档实测**到的对比度也真的到 AAA。
    const Folder::ColorScheme &scheme = Folder::colorSchemeByIdentifier(QStringLiteral("high-contrast"));
    QCOMPARE(scheme.identifier, QStringLiteral("high-contrast"));
    QVERIFY(scheme.minimumContrast >= Folder::kStrongReadableContrastRatio);
    double worst = 100.0;
    QString worstWhere;
    for (const auto &descriptor : Folder::mainStatusTable()) {
        const double light = Folder::contrastRatio(scheme.colorFor(descriptor.value, false),
                                                  scheme.lightBackground);
        const double dark = Folder::contrastRatio(scheme.colorFor(descriptor.value, true),
                                                 scheme.darkBackground);
        QVERIFY(light > 0.0);
        QVERIFY(dark > 0.0);
        if (light < worst) {
            worst = light;
            worstWhere = Folder::statusIdentifier(descriptor.value) + QStringLiteral("/light");
        }
        if (dark < worst) {
            worst = dark;
            worstWhere = Folder::statusIdentifier(descriptor.value) + QStringLiteral("/dark");
        }
    }
    QVERIFY2(worst >= Folder::kStrongReadableContrastRatio,
             qPrintable(QStringLiteral("最低一档是 %1 = %2").arg(worstWhere).arg(worst, 0, 'f', 2)));
    // 默认方案只需要 AA：两个阈值必须是两个不同的数，否则「高对比」没有内容。
    const Folder::ColorScheme &standard = Folder::colorSchemeByIdentifier(Folder::defaultColorSchemeIdentifier());
    QVERIFY(standard.minimumContrast < scheme.minimumContrast);
}

void StatusPaletteTests::theColorBlindSchemeBeatsTheDefaultMeasurably()
{
    // 「色盲友好」这套方案存在的理由，是默认方案在红绿色盲眼里分不开。
    // 这条用例把两个数都摆出来——只断言「友好方案过阈值」的话，
    // 把默认方案也一起改成合格色值（于是这个词失去意义）照样能过。
    const Folder::ColorScheme &standard = Folder::colorSchemeByIdentifier(Folder::defaultColorSchemeIdentifier());
    const Folder::ColorScheme &friendly = Folder::colorSchemeByIdentifier(QStringLiteral("color-blind-safe"));

    const double defaultLight = Folder::colorBlindSeparation(standard, false);
    const double defaultDark = Folder::colorBlindSeparation(standard, true);
    QVERIFY(defaultLight > 0.0);
    QVERIFY(defaultDark > 0.0);
    // 默认方案在红绿色盲下确实有状态两两几乎同色（实测 2.8 / 4.2）。
    // 这条断言是「阈值不是拍脑袋」的证据：它低于阈值，而且低很多。
    QVERIFY2(defaultLight < Folder::kMinimumColorBlindSeparation,
             qPrintable(QStringLiteral("默认方案间距 %1").arg(defaultLight, 0, 'f', 1)));
    QVERIFY2(defaultDark < Folder::kMinimumColorBlindSeparation,
             qPrintable(QStringLiteral("默认方案间距 %1").arg(defaultDark, 0, 'f', 1)));

    const double friendlyLight = Folder::colorBlindSeparation(friendly, false);
    const double friendlyDark = Folder::colorBlindSeparation(friendly, true);
    QVERIFY2(friendlyLight >= Folder::kMinimumColorBlindSeparation,
             qPrintable(QStringLiteral("色盲友好方案间距 %1").arg(friendlyLight, 0, 'f', 1)));
    QVERIFY2(friendlyDark >= Folder::kMinimumColorBlindSeparation,
             qPrintable(QStringLiteral("色盲友好方案间距 %1").arg(friendlyDark, 0, 'f', 1)));
    // 而且必须比默认方案明显更好——恰好卡在阈值上的方案与默认方案一样不可用。
    QVERIFY(friendlyLight > defaultLight * 3.0);
    QVERIFY(friendlyDark > defaultDark * 3.0);
    QVERIFY(friendly.colorBlindFriendly);
}

void StatusPaletteTests::claimingColorBlindFriendlyWithoutTheSeparationIsRejected()
{
    // 把「色盲友好」这一位单独打开、而颜色仍是默认那九档 —— 判据必须报出来。
    // 这条用例守的是**这一位是判据的开关**，而不是一个装饰性字段。
    QVector<Folder::ColorScheme> table = factoryCopy();
    for (auto &scheme : table) {
        if (scheme.identifier != Folder::defaultColorSchemeIdentifier())
            continue;
        scheme.colorBlindFriendly = true;
    }
    const QStringList problems = Folder::validateColorSchemeTable(table);
    QVERIFY2(containsProblem(problems, QStringLiteral("投影后最小间距")), qPrintable(statusListText(problems)));
}

// 跨方案的那几条判据（套数 / 唯一性 / 排名 / 名字 ↔ 性质）必须各自可达。
//
// 这一组用例的存在理由是 handoff §6 那条纪律的反面用法：
// **每一条校验分支都要有输入能走到它**。上面那些用例各自只打红一两条，
// 跨方案的四条如果没人喂输入，它们就是没人知道的死代码——
// 而它们查的恰恰是「三套方案被删成两套」「名字与性质对不上」这类
// 改表时最容易犯、运行期却没有任何现象的错。
void StatusPaletteTests::crossSchemeChecksRejectAThinnedReorderedOrMislabelledTable()
{
    // ① 少于三套。
    QVector<Folder::ColorScheme> thinned = factoryCopy();
    thinned.removeLast();
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(thinned), QStringLiteral("至少要有 3 套")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(thinned))));

    // ② 第一套不是出厂默认：界面初始值与配色文件回退都取第一行，
    //    换掉它等于换掉所有人看到的默认外观。
    QVector<Folder::ColorScheme> reordered = factoryCopy();
    reordered.move(0, reordered.size() - 1);
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(reordered), QStringLiteral("第一套方案必须")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(reordered))));

    // ③ 标识符重复：设置键 / 配色文件 / 命令行三处都按标识符取方案，
    //    重名会让「取到哪一套」取决于遍历顺序。
    QVector<Folder::ColorScheme> duplicatedIds = factoryCopy();
    duplicatedIds[1].identifier = duplicatedIds[0].identifier;
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(duplicatedIds), QStringLiteral("必须唯一")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(duplicatedIds))));

    // ④ 一套自称色盲友好的都没有：规格点名要三套，其中一套就是它。
    QVector<Folder::ColorScheme> noneClaim = factoryCopy();
    for (auto &scheme : noneClaim)
        scheme.colorBlindFriendly = false;
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(noneClaim), QStringLiteral("没有任何一套方案自称色盲友好")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(noneClaim))));

    // ⑤ `color-blind-safe` 这个名字不许不使用色盲友好这一位——
    //    否则它只是一个好看的标识符（这条与④不同：④是「全都没标」，这里是「该标没标」）。
    QVector<Folder::ColorScheme> mislabelled = factoryCopy();
    QVector<Folder::ColorScheme> onlyOne = factoryCopy();
    for (auto &scheme : mislabelled) {
        if (scheme.identifier == QStringLiteral("color-blind-safe"))
            scheme.colorBlindFriendly = false;
    }
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(mislabelled), QStringLiteral("不许不使用这一位")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(mislabelled))));

    // ⑥ 「高对比」的阈值必须真的到 AAA。名字与性质绑定在这里：
    //    把阈值调回 AA 之后，这个方案与默认方案就没有任何可度量的区别了。
    QVector<Folder::ColorScheme> softened = factoryCopy();
    for (auto &scheme : softened) {
        if (scheme.identifier == QStringLiteral("high-contrast"))
            scheme.minimumContrast = Folder::kMinimumReadableContrastRatio;
    }
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(softened), QStringLiteral("AAA")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(softened))));

    // 空表与「规格点名的三套缺一」也要有各自的报法。
    QCOMPARE(Folder::validateColorSchemeTable({}).size(), 1);
    QVector<Folder::ColorScheme> missingOne = factoryCopy();
    for (int i = missingOne.size() - 1; i >= 0; --i) {
        if (missingOne[i].identifier == QStringLiteral("color-blind-safe"))
            missingOne.removeAt(i);
    }
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(missingOne),
                             QStringLiteral("色盲友好")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(missingOne))));
}

void StatusPaletteTests::theDefaultSchemeWouldFailTheColorBlindThresholdIfItClaimedIt()
{
    // 上一条的反面：出厂表里**只有**色盲友好那一套会被这条判据检查，
    // 默认方案与高对比方案不受它约束（它们不许诺这件事）。
    // 因此「把出厂表的两套调换颜色」应当被抓住，而「默认方案本身不达标」
    // 不应被当成错误——这两件事必须同时成立，否则判据要么太松要么太紧。
    const QStringList problems = Folder::validateColorSchemeTable(Folder::colorSchemeTable());
    QVERIFY2(!containsProblem(problems, QStringLiteral("default")), qPrintable(statusListText(problems)));

    // 真的把两套的九档互换：色盲友好那套从此用的是默认色值。
    QVector<Folder::ColorScheme> swapped = factoryCopy();
    int standardIndex = -1;
    int friendlyIndex = -1;
    for (int i = 0; i < swapped.size(); ++i) {
        if (swapped[i].identifier == Folder::defaultColorSchemeIdentifier())
            standardIndex = i;
        if (swapped[i].identifier == QStringLiteral("color-blind-safe"))
            friendlyIndex = i;
    }
    QVERIFY(standardIndex >= 0);
    QVERIFY(friendlyIndex >= 0);
    swapped[friendlyIndex].highlights = swapped[standardIndex].highlights;
    const QStringList swappedProblems = Folder::validateColorSchemeTable(swapped);
    QVERIFY2(containsProblem(swappedProblems, QStringLiteral("投影后最小间距")),
             qPrintable(statusListText(swappedProblems)));
}

// ---------------------------------------------------------------------------
// C 组：第 3 条
// ---------------------------------------------------------------------------

void StatusPaletteTests::aStatusTableWithoutIconKeysIsRejected()
{
    // 第 3 条「颜色仅作为辅助，状态图标与文字提示必须同时存在」。
    //
    // 图标键住在**另一张表**里，所以这条判据必须能把状态表当参数收到手里——
    // 否则「图标被删空了」这件事在配色表里完全看不出来（配色表九档齐全、
    // 色值合法、对比度达标）。**表是参数**是这条用例的全部意义，
    // 与 `validateRecursionTierTable()` / `check_winapi.py --self-test` 同一纪律。
    QVector<Folder::MainStatusDescriptor> statuses = Folder::mainStatusTable();
    QVERIFY(!statuses.isEmpty());
    statuses.first().iconKey = "";
    const QStringList problems = Folder::validateColorSchemeTable(Folder::colorSchemeTable(), statuses);
    QVERIFY2(containsProblem(problems, QStringLiteral("没有图标键")), qPrintable(statusListText(problems)));
}

void StatusPaletteTests::aSchemeMissingOrDuplicatingAStatusIsRejected()
{
    // 缺一档：用户会在某一档状态上看到「没有配色」——界面回退成 Qt 默认前景色，
    // 那与「这一档不重要」看起来一模一样。
    QVector<Folder::ColorScheme> missing = factoryCopy();
    missing[0].highlights.removeFirst();
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(missing), QStringLiteral("缺状态")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(missing))));

    // 重复一档：`colorFor()` 返回第一个命中的行，于是第二行永远读不到——
    // 一份改了没生效的配色，比没有配色更难查。
    QVector<Folder::ColorScheme> duplicated = factoryCopy();
    duplicated[0].highlights.append(duplicated[0].highlights.first());
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(duplicated), QStringLiteral("份颜色")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(duplicated))));

    // 表外状态：整数不落在主状态表里，通常来自旧版本文件或手改。
    QVector<Folder::ColorScheme> alien = factoryCopy();
    Folder::StatusHighlight stray;
    stray.status = Folder::Status(99);
    stray.light = QStringLiteral("#123456");
    stray.dark = QStringLiteral("#123456");
    alien[0].highlights.append(stray);
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(alien), QStringLiteral("表外状态")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(alien))));

    // 覆盖不全时 `colorBlindSeparation()` 必须返回 -1（算不出来），
    // 而不是 0（「两档完全重合」）。两者都会被判成不达标，但含义完全不同，
    // 混在一起会让「少了一档」被读成「颜色分不开」。
    Folder::ColorScheme incomplete = missing[0];
    QCOMPARE(Folder::colorBlindSeparation(incomplete, false), -1.0);

    // `coversEveryStatus()` 是**另一个**谓词——第 1 条「每类状态有默认颜色」
    // 的正面形式，`validateOneColorScheme()` 的「缺一档 / 重复一档」是它的反面
    // 报错路径。两处都得有输入，因为它们是两份实现。
    //
    // 探针实证：把它的第二句 `return highlights.size() == statuses.size();`
    // 改成 `return true;`（即只查「九档都在」、不查「没有多余的行」）
    // **全部 29 条用例一条都不红**。判据是「删掉之后谁变红」，
    // 答案是没人变红 ⇒ 这半句此前没有任何东西守着。
    QVERIFY(!duplicated[0].coversEveryStatus()); // 十行、九档（多出一行）
    QVERIFY(!missing[0].coversEveryStatus());    // 八行、缺一档
    QVERIFY(!alien[0].coversEveryStatus());      // 十行、其中一档是表外状态
    // 反面：出厂三套必须都返回真，否则上面那三条只是在验一个恒假的谓词。
    for (const auto &scheme : Folder::colorSchemeTable())
        QVERIFY2(scheme.coversEveryStatus(), qPrintable(scheme.identifier));
}

void StatusPaletteTests::aWashedOutColorIsRejected()
{
    // 「不得因着色影响可读性」的可执行形式：把一档颜色调到接近背景色。
    // #f8f8f8 在白底上几乎是看不见的，而它依然是一个合法色值、
    // 依然能通过「非空」与「格式正确」两道检查。
    QVector<Folder::ColorScheme> table = factoryCopy();
    QVERIFY(replaceColor(table[0], Folder::Status::Different, QStringLiteral("#f8f8f8"), QString()));
    const QStringList problems = Folder::validateColorSchemeTable(table);
    QVERIFY2(containsProblem(problems, QStringLiteral("对比度")), qPrintable(statusListText(problems)));

    // 深色档同理：在深色背景上放一个接近黑的颜色。
    QVector<Folder::ColorScheme> darkTable = factoryCopy();
    QVERIFY(replaceColor(darkTable[0], Folder::Status::Different, QString(),
                         QStringLiteral("#101010")));
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(darkTable), QStringLiteral("深色主题")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(darkTable))));

    // 阈值本身也要有下限：一个把 minimumContrast 设成 1.0 的方案
    // 会让上面两道检查全部静默失效（1.0 是「同色」，永远达标）。
    QVector<Folder::ColorScheme> lenient = factoryCopy();
    lenient[0].minimumContrast = 1.0;
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(lenient), QStringLiteral("可读下限")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(lenient))));
}

void StatusPaletteTests::theExcludedColourMustBeReadableAndDistinctFromSame()
{
    // 「已排除」不是主状态，但它也是用户要读的一行字，因此同样受对比度约束。
    // 这里刻意**不留**「弱化色可低于下限」的例外：例外需要有东西守得住
    // 「确实弱到该弱的程度」，而那件事没有客观判据。
    QVector<Folder::ColorScheme> washed = factoryCopy();
    washed[0].excludedLight = QStringLiteral("#fafafa");
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(washed), QStringLiteral("已排除")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(washed))));

    // 弱化色不是色值这一支（「弱化色必须可读」之外的**另一条**分支）。
    // 它与上面那条共用「已排除」三个字，因此只用「已排除」当针时，
    // 把这一支整段删掉不会有任何用例变红——本轮扫出来的五处之一。
    // 判据用只在这一支里出现的「弱化色必须」。
    QVector<Folder::ColorScheme> notAColour = factoryCopy();
    notAColour[0].excludedLight = QStringLiteral("grey");
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(notAColour),
                             QStringLiteral("弱化色必须")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(notAColour))));

    // 与「相同」同色：用户分不清「两边确实一样」与「根本没比」，
    // 而这两件事的处置完全相反（一个可以放手、一个必须去看）。
    QVector<Folder::ColorScheme> sameAsSame = factoryCopy();
    const QString sameLight = sameAsSame[0].colorFor(Folder::Status::Same, false);
    const QString sameDark = sameAsSame[0].colorFor(Folder::Status::Same, true);
    QVERIFY(!sameLight.isEmpty());
    sameAsSame[0].excludedLight = sameLight;
    sameAsSame[0].excludedDark = sameDark;
    QVERIFY2(containsProblem(Folder::validateColorSchemeTable(sameAsSame), QStringLiteral("同色")),
             qPrintable(statusListText(Folder::validateColorSchemeTable(sameAsSame))));

    // 出厂表里这条约束是**真的成立**的（否则上面那条用例只是在验一个空条件）。
    for (const auto &scheme : Folder::colorSchemeTable()) {
        QVERIFY2(scheme.excludedLight != scheme.colorFor(Folder::Status::Same, false),
                 qPrintable(scheme.identifier));
        QVERIFY2(scheme.excludedDark != scheme.colorFor(Folder::Status::Same, true),
                 qPrintable(scheme.identifier));
    }
}

void StatusPaletteTests::anUnparsableColourIsNotSilentlyTreatedAsBlack()
{
    // 色值解析必须**严格**。#abc（3 位缩写）与 #rrggbbaa（带 alpha）
    // 都是合法的 CSS 写法，收下它们会让「看着一样」与「相等」分家；
    // 而 alpha 更是另一件事——本模块算的是「文字压在背景上的对比度」，
    // 带 alpha 的前景色得先与背景合成，那时「一档状态一个颜色」就不成立了。
    for (const QString &bad : {QStringLiteral("#abc"), QStringLiteral("abc"),
                               QStringLiteral("#12345"), QStringLiteral("#1234567"),
                               QStringLiteral("#12345g"), QStringLiteral("#+12345"),
                               QStringLiteral("rgb(1,2,3)"), QString()}) {
        QCOMPARE(Folder::relativeLuminance(bad), -1.0);
    }
    // 合法值不能因为严格的解析器被误伤。
    QVERIFY(Folder::relativeLuminance(QStringLiteral("#000000")) >= 0.0);
    QVERIFY(Folder::relativeLuminance(QStringLiteral("#FFFFFF")) >= 0.0);
    QVERIFY(qAbs(Folder::relativeLuminance(QStringLiteral("#ffffff")) - 1.0) < 0.001);
}

void StatusPaletteTests::anIdentifierThatIsNotMachineReadableIsRejected()
{
    // 标识符形状：**小写字母 / 数字 / 连字符**。
    //
    // 这条判据曾经是死的——接手被中断的那一轮时，`validateOneColorScheme()`
    // 里这段检查被 `if (false)` 包着，于是 `isMachineReadableIdentifier()`
    // 全仓找不到调用者。把它恢复成真之后必须有输入能走到它，
    // 否则它只是一段「看着像纵深防御」的代码（handoff §6 的纪律：
    // 删掉之后没有任何用例变红的分支不是纵深防御）。
    //
    // 为什么形状值得单独守：标识符要进三个由不得空格与大写的地方——
    // 设置键、`.lqcolors` 文件、命令行。三处各自做一次归一化，
    // 归一化迟早分叉，表现是「同一套配色在三处被认成三套」。
    const QStringList shapeNeedle{QStringLiteral("标识符必须是小写字母")};

    // ① 空格 + 大写（人最自然的写法：「High Contrast」）。
    QVector<Folder::ColorScheme> spaced = factoryCopy();
    spaced[1].identifier = QStringLiteral("High Contrast");
    QStringList problems = Folder::validateColorSchemeTable(spaced);
    QVERIFY2(containsProblem(problems, shapeNeedle.first()), qPrintable(statusListText(problems)));

    // ② 下划线。它在设置键里会被某些层当成分隔符，在命令行里又不合法。
    QVector<Folder::ColorScheme> underscored = factoryCopy();
    underscored[1].identifier = QStringLiteral("high_contrast");
    problems = Folder::validateColorSchemeTable(underscored);
    QVERIFY2(containsProblem(problems, shapeNeedle.first()), qPrintable(statusListText(problems)));

    // ③ 非 ASCII。中文标识符进 `const char *` 会被当 Latin-1 逐字符解释，
    //    比较永远不相等（本仓踩过一次，见 handoff §6）。
    QVector<Folder::ColorScheme> nonAscii = factoryCopy();
    nonAscii[1].identifier = QStringLiteral("高对比");
    problems = Folder::validateColorSchemeTable(nonAscii);
    QVERIFY2(containsProblem(problems, shapeNeedle.first()), qPrintable(statusListText(problems)));

    // ④ 空标识符：报错文案必须读得通（`where` 已换成「（未命名方案）」），
    //    而不是印出「方案  的标识符……」这种缺主语的句子。
    QVector<Folder::ColorScheme> unnamed = factoryCopy();
    unnamed[1].identifier = QString();
    problems = Folder::validateColorSchemeTable(unnamed);
    QVERIFY2(containsProblem(problems, shapeNeedle.first()), qPrintable(statusListText(problems)));
    QVERIFY2(containsProblem(problems, QStringLiteral("（未命名方案）")),
             qPrintable(statusListText(problems)));

    // ⑤ 首尾空格。人从文档里复制粘贴时最常见的一种，而它在界面上
    //    **完全看不出来**——下拉框渲染时前导空格不显眼。
    QVector<Folder::ColorScheme> padded = factoryCopy();
    padded[1].identifier = QStringLiteral(" high-contrast");
    problems = Folder::validateColorSchemeTable(padded);
    QVERIFY2(containsProblem(problems, shapeNeedle.first()), qPrintable(statusListText(problems)));

    // ⑥ 正面控制：一个**合规**的自定义标识符不许被误伤。没有这一条的话，
    //    把上面五种写法全部拒绝的最省事做法是「一律返回 false」。
    //    这里同时钉住「合规但叫法不同」也放行——判据查的是**形状**，
    //    不是「必须在出厂三套的白名单里」（那样自定义配色就永远存不下来，
    //    第 5 条当场落空）。
    QVector<Folder::ColorScheme> customNamed = factoryCopy();
    customNamed[1].identifier = QStringLiteral("my-scheme-2");
    problems = Folder::validateColorSchemeTable(customNamed);
    QVERIFY2(!containsProblem(problems, shapeNeedle.first()), qPrintable(statusListText(problems)));
    // 它仍然应该被报「缺规格点名的 high-contrast」——说明上面那句
    // 「没有形状问题」不是因为校验整体没跑。
    QVERIFY2(containsProblem(problems, QStringLiteral("缺规格点名")),
             qPrintable(statusListText(problems)));
}

void StatusPaletteTests::anIdentifierIsJudgedOnItselfNotOnlyAgainstItsSiblings()
{
    // 这条用例把「形状」与「唯一性」两件事**分别**钉住，因为它们是两条
    // 独立的判据，而只有唯一性在位时有一条很隐蔽的漏检路径：
    // 一个叫「High Contrast」的方案**没有重名**，唯一性检查放它过去，
    // 于是「标识符必须是机器可读的」这件事没有任何东西会红。
    //
    // 判据一句话：**「不重名」不等于「名字合法」**。
    const QString shapeNeedle = QStringLiteral("标识符必须是小写字母");
    const QString uniquenessNeedle = QStringLiteral("必须唯一");

    // ① 非法但唯一 → 形状报，唯一性不报。
    QVector<Folder::ColorScheme> uniqueButIllegal = factoryCopy();
    uniqueButIllegal[1].identifier = QStringLiteral("High Contrast");
    const QStringList onlyShape = Folder::validateColorSchemeTable(uniqueButIllegal);
    QVERIFY2(containsProblem(onlyShape, shapeNeedle), qPrintable(statusListText(onlyShape)));
    QVERIFY2(!containsProblem(onlyShape, uniquenessNeedle), qPrintable(statusListText(onlyShape)));

    // ② 合法但重复 → 唯一性报，形状不报。
    //    这一半保证上一半不是「凡改动必报形状」的空条件。
    QVector<Folder::ColorScheme> duplicatedButLegal = factoryCopy();
    duplicatedButLegal[1].identifier = duplicatedButLegal[0].identifier;
    const QStringList onlyUniqueness = Folder::validateColorSchemeTable(duplicatedButLegal);
    QVERIFY2(containsProblem(onlyUniqueness, uniquenessNeedle),
             qPrintable(statusListText(onlyUniqueness)));
    QVERIFY2(!containsProblem(onlyUniqueness, shapeNeedle),
             qPrintable(statusListText(onlyUniqueness)));

    // ③ 出厂表两条都不报：判据没有误伤。
    const QStringList clean = Folder::validateColorSchemeTable(Folder::colorSchemeTable());
    QVERIFY2(!containsProblem(clean, shapeNeedle), qPrintable(statusListText(clean)));
    QVERIFY2(!containsProblem(clean, uniquenessNeedle), qPrintable(statusListText(clean)));

    // ④ 形状检查是**逐套**做的，不是只看第一套。把同一个非法标识符
    //    挪到第一套上必须照样报——否则「标识符形状」这件事只在
    //    「改错了哪一套」的某些排列下才被守住，另一些排列下静默通过。
    QVector<Folder::ColorScheme> illegalFirst = factoryCopy();
    illegalFirst[0].identifier = QStringLiteral("High Contrast");
    const QStringList firstSlot = Folder::validateColorSchemeTable(illegalFirst);
    QVERIFY2(containsProblem(firstSlot, shapeNeedle), qPrintable(statusListText(firstSlot)));
}

// ---------------------------------------------------------------------------
// D 组：第 5 条
// ---------------------------------------------------------------------------

void StatusPaletteTests::roundTripThroughAFilePreservesEveryField()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 拿一份改过的配色当真值：往返必须保住**自定义**的值，
    // 只测出厂表的话，一个「读回来永远是出厂表」的实现照样能过。
    Folder::ColorScheme custom = Folder::colorSchemeByIdentifier(QStringLiteral("high-contrast"));
    custom.identifier = QStringLiteral("my-custom-scheme");
    custom.displayName = QStringLiteral("我的配色 · 高对比变体");
    custom.description = QStringLiteral("把「不同」改成了青色，用于弱视同事的机器。");
    custom.colorBlindFriendly = false;
    custom.minimumContrast = Folder::kStrongReadableContrastRatio;
    QVERIFY(replaceColor(custom, Folder::Status::Different, QStringLiteral("#00494f"), QString()));
    QVERIFY(replaceColor(custom, Folder::Status::Different, QString(), QStringLiteral("#66e6f0")));

    const QString path = dir.filePath(QStringLiteral("mine") + QLatin1Char('.')
                                      + Folder::colorSchemeFileExtension());
    QStringList problems;
    QVERIFY2(Folder::saveColorSchemeFile(path, custom, &problems), qPrintable(statusListText(problems)));
    QVERIFY(QFile::exists(path));

    Folder::ColorScheme loaded;
    QVERIFY2(Folder::loadColorSchemeFile(path, loaded, &problems), qPrintable(statusListText(problems)));
    QCOMPARE(loaded.identifier, custom.identifier);
    QCOMPARE(loaded.displayName, custom.displayName);
    QCOMPARE(loaded.description, custom.description);
    QCOMPARE(loaded.colorBlindFriendly, custom.colorBlindFriendly);
    QVERIFY(qAbs(loaded.minimumContrast - custom.minimumContrast) < 0.0001);
    QCOMPARE(loaded.lightBackground, custom.lightBackground);
    QCOMPARE(loaded.darkBackground, custom.darkBackground);
    QCOMPARE(loaded.excludedLight, custom.excludedLight);
    QCOMPARE(loaded.excludedDark, custom.excludedDark);
    QVERIFY(loaded.coversEveryStatus());
    for (const auto &descriptor : Folder::mainStatusTable()) {
        QCOMPARE(loaded.colorFor(descriptor.value, false), custom.colorFor(descriptor.value, false));
        QCOMPARE(loaded.colorFor(descriptor.value, true), custom.colorFor(descriptor.value, true));
    }
    // 非 ASCII 的展示名与说明必须原样活过往返（文件是 UTF-8 JSON）。
    QVERIFY(loaded.displayName.contains(QStringLiteral("我的配色")));
    // 文件扩展名与过滤器同源。
    QCOMPARE(Folder::colorSchemeFileExtension(), QStringLiteral("lqcolors"));
    QVERIFY(Folder::colorSchemeFileFilter().contains(QStringLiteral("*.lqcolors")));
}

void StatusPaletteTests::theSerializedFormIsKeyedByStatusIdentifierNotByIndex()
{
    // 状态用稳定标识符当键。用下标当键的话，主状态表下一次新增一档，
    // 所有已分享出去的文件就会**整体错位一格**——而那些文件不会报错，
    // 只会把颜色配到错误的状态上。
    const QString text = Folder::serializeColorScheme(Folder::colorSchemeByIdentifier(
        Folder::defaultColorSchemeIdentifier()));
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("format")).toString(),
             QStringLiteral("lqcompare-color-scheme"));
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    const QJsonArray statuses = root.value(QStringLiteral("statuses")).toArray();
    QCOMPARE(statuses.size(), Folder::mainStatusTable().size());
    QSet<QString> identifiers;
    for (const QJsonValue &value : statuses)
        identifiers.insert(value.toObject().value(QStringLiteral("status")).toString());
    for (const auto &descriptor : Folder::mainStatusTable())
        QVERIFY2(identifiers.contains(Folder::statusIdentifier(descriptor.value)),
                 qPrintable(Folder::statusIdentifier(descriptor.value)));
    // 每一项都带浅深两档，不能只写一档（只写一档时读回来那一档是空的）。
    for (const QJsonValue &value : statuses) {
        const QJsonObject entry = value.toObject();
        QVERIFY(entry.contains(QStringLiteral("light")));
        QVERIFY(entry.contains(QStringLiteral("dark")));
    }
}

void StatusPaletteTests::filesThatAreNotPalettesAreRejectedWithAUsefulReason()
{
    Folder::ColorScheme out;
    QStringList problems;

    // 选错了文件是最常见的一种失败。报「缺字段 identifier」会把用户
    // 引去查自己的配色文件，而真正的问题是「他选了一份会话文件」。
    problems.clear();
    QVERIFY(!Folder::parseColorScheme(QStringLiteral("{\"sessions\":[]}"), out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("format")), qPrintable(statusListText(problems)));

    problems.clear();
    QVERIFY(!Folder::parseColorScheme(QStringLiteral("这不是 JSON"), out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("JSON")), qPrintable(statusListText(problems)));

    // 版本比格式标记更容易被忽略：将来的 v2 文件读进来会「看起来正常」。
    problems.clear();
    const QString future = QStringLiteral(
        "{\"format\":\"lqcompare-color-scheme\",\"version\":2,\"identifier\":\"x\"}");
    QVERIFY(!Folder::parseColorScheme(future, out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("版本")), qPrintable(statusListText(problems)));

    // 根节点必须是对象（顶层是数组的 JSON 也是合法 JSON）。
    problems.clear();
    QVERIFY(!Folder::parseColorScheme(QStringLiteral("[]"), out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("对象")), qPrintable(statusListText(problems)));
}

void StatusPaletteTests::structurallyBrokenEntriesAreRejected()
{
    const auto build = [](const std::function<void(QJsonObject &, QJsonArray &)> &mutate) {
        Folder::ColorScheme scheme = Folder::colorSchemeByIdentifier(QStringLiteral("high-contrast"));
        scheme.identifier = QStringLiteral("custom");
        QJsonDocument document = QJsonDocument::fromJson(
            Folder::serializeColorScheme(scheme).toUtf8());
        QJsonObject root = document.object();
        QJsonArray statuses = root.value(QStringLiteral("statuses")).toArray();
        mutate(root, statuses);
        root.insert(QStringLiteral("statuses"), statuses);
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    };

    Folder::ColorScheme out;
    QStringList problems;

    // 少一档。
    problems.clear();
    QString text = build([](QJsonObject &, QJsonArray &statuses) { statuses.removeAt(statuses.size() - 1); });
    QVERIFY(!Folder::parseColorScheme(text, out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("缺状态")), qPrintable(statusListText(problems)));

    // 表外标识符：要明确报出来，而不是静默丢掉那一档（丢掉之后报的是
    // 「缺状态」，那句话说的是另一件事，会把用户引去查「为什么少了一档」）。
    problems.clear();
    text = build([](QJsonObject &, QJsonArray &statuses) {
        QJsonObject entry = statuses.at(0).toObject();
        entry.insert(QStringLiteral("status"), QStringLiteral("no-such-status"));
        statuses.replace(0, entry);
    });
    QVERIFY(!Folder::parseColorScheme(text, out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("不认识")), qPrintable(statusListText(problems)));

    // 非法色值。
    problems.clear();
    text = build([](QJsonObject &, QJsonArray &statuses) {
        QJsonObject entry = statuses.at(0).toObject();
        entry.insert(QStringLiteral("light"), QStringLiteral("#abc"));
        statuses.replace(0, entry);
    });
    QVERIFY(!Folder::parseColorScheme(text, out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("#rrggbb")), qPrintable(statusListText(problems)));

    // 空数组与「整个键缺失」是**两件事**，报出来的原因也必须不同：
    // 空数组说明文件被写坏了（或被人清空了），此时逐档报「缺状态」最有用；
    // 键缺失说明这根本不是配色文件，报「缺 statuses 数组」才对。
    // 把两者塌成一句话会让用户拿着正确方向的反面去排查。
    problems.clear();
    text = build([](QJsonObject &, QJsonArray &statuses) { statuses = QJsonArray(); });
    QVERIFY(!Folder::parseColorScheme(text, out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("缺状态")), qPrintable(statusListText(problems)));

    problems.clear();
    QJsonObject withoutStatuses =
        QJsonDocument::fromJson(
            Folder::serializeColorScheme(
                Folder::colorSchemeByIdentifier(QStringLiteral("high-contrast"))).toUtf8())
            .object();
    withoutStatuses.remove(QStringLiteral("statuses"));
    QVERIFY(!Folder::parseColorScheme(QString::fromUtf8(QJsonDocument(withoutStatuses).toJson()), out,
                                      &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("缺 statuses")), qPrintable(statusListText(problems)));
}

void StatusPaletteTests::aHandEditedFileThatBreaksContrastIsRejected()
{
    // 这条是「导入成功」与「表合规」不能分家的证据：解析器复用同一份判据，
    // 因此一个手改坏了对比度的文件不会被当成合法的自定义配色装进界面。
    // 只做结构层校验的实现会放它进去，而用户看到的只是「字看不清」。
    Folder::ColorScheme scheme = Folder::colorSchemeByIdentifier(QStringLiteral("high-contrast"));
    scheme.identifier = QStringLiteral("custom");
    QJsonDocument document = QJsonDocument::fromJson(
        Folder::serializeColorScheme(scheme).toUtf8());
    QJsonObject root = document.object();
    QJsonArray statuses = root.value(QStringLiteral("statuses")).toArray();
    QJsonObject entry = statuses.at(0).toObject();
    entry.insert(QStringLiteral("light"), QStringLiteral("#fdfdfd")); // 白底上几乎看不见
    statuses.replace(0, entry);
    root.insert(QStringLiteral("statuses"), statuses);

    Folder::ColorScheme out;
    QStringList problems;
    QVERIFY(!Folder::parseColorScheme(QString::fromUtf8(QJsonDocument(root).toJson()), out, &problems));
    QVERIFY2(containsProblem(problems, QStringLiteral("对比度")), qPrintable(statusListText(problems)));

    // 缺 minimumContrast 时按 AA 兜底，**不能**按 0：
    // 按 0 会让对比度检查静默失效，于是一个手写坏了的文件「导入成功」。
    QJsonObject root2 = QJsonDocument::fromJson(
        Folder::serializeColorScheme(Folder::colorSchemeByIdentifier(
            Folder::defaultColorSchemeIdentifier())).toUtf8()).object();
    root2.remove(QStringLiteral("minimumContrast"));
    root2.insert(QStringLiteral("identifier"), QStringLiteral("custom"));
    Folder::ColorScheme second;
    QStringList secondProblems;
    QVERIFY(Folder::parseColorScheme(QString::fromUtf8(QJsonDocument(root2).toJson()), second,
                                     &secondProblems));
    QVERIFY(qAbs(second.minimumContrast - Folder::kMinimumReadableContrastRatio) < 0.0001);
}

void StatusPaletteTests::aFailedLoadLeavesTheCallersSchemeUntouched()
{
    // 失败时不许写出半成品：调用方手里那份方案是界面上**正在用**的那套，
    // 一次导入失败把它改成「读了一半」的状态，界面就会用一份缺几档的配色去画，
    // 而用户以为自己只是「导入没成功」。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.lqcolors"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("{\"format\":\"lqcompare-color-scheme\",\"version\":1,\"statuses\":[]}") > 0);
    file.close();

    Folder::ColorScheme target = Folder::colorSchemeByIdentifier(QStringLiteral("color-blind-safe"));
    const Folder::ColorScheme before = target;
    QStringList problems;
    QVERIFY(!Folder::loadColorSchemeFile(path, target, &problems));
    QVERIFY(!problems.isEmpty());
    QCOMPARE(target.identifier, before.identifier);
    QCOMPARE(target.highlights.size(), before.highlights.size());
    QCOMPARE(target.colorFor(Folder::Status::Conflict, false),
             before.colorFor(Folder::Status::Conflict, false));

    // 文件根本不存在时同样只报原因、不写结果。
    Folder::ColorScheme missingTarget = before;
    QStringList missingProblems;
    QVERIFY(!Folder::loadColorSchemeFile(dir.filePath(QStringLiteral("nope.lqcolors")), missingTarget,
                                         &missingProblems));
    QVERIFY2(containsProblem(missingProblems, QStringLiteral("无法读取")),
             qPrintable(statusListText(missingProblems)));
    QCOMPARE(missingTarget.identifier, before.identifier);
}

void StatusPaletteTests::savingIntoAnUnwritablePathReportsWhy()
{
    // 写失败不能静默：用户会以为配色已经导出、把文件发给同事，
    // 而同事那边根本收到不东西。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("no-such-dir/nested/mine.lqcolors"));
    QStringList problems;
    QVERIFY(!Folder::saveColorSchemeFile(path, Folder::colorSchemeByIdentifier(
        Folder::defaultColorSchemeIdentifier()), &problems));
    QVERIFY(!problems.isEmpty());
    QVERIFY2(containsProblem(problems, QStringLiteral("无法写入")), qPrintable(statusListText(problems)));
}

QTEST_APPLESS_MAIN(StatusPaletteTests)
