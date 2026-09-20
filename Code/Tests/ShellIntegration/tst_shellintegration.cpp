/// \file
/// \brief 注册表存储与 Shell 集成的测试（PRD: PLAT-005）。
///
/// 本套件用内存后端把「安装 → 校验 → 卸载 → 残留检查」整条链路真实跑完。
/// 真正运行在 Windows 上的只有 registrystore_win.cpp 那一层薄薄的直译，
/// 因此这一整套断言在交付到 Windows 之前就已经是有效证据。

#include "tst_shellintegration.h"

#include "Files/filesystem.h"

#include <QByteArray>
#include <QScopedPointer>
#include <QSet>
#include <QStringList>
#include <QtTest>

#include <algorithm>

using namespace LqCompare::Platform;

// `Files::` 是 LqCompare::Files 的别名。写全名会让每一处错误断言都长一截，
// 而这一类的断言在本文件里有几十处。
namespace Files = LqCompare::Files;

namespace {

/// 被测程序自己的路径。刻意**带空格**（`Program Files`）：
/// 引号规则里最容易出错的一种输入，而它又是最常见的真实情况。
QString executablePath()
{
    return QStringLiteral("C:\\Program Files\\LqCompare\\LqCompare.exe");
}

QString classesKey(const QString &tail)
{
    return QStringLiteral("Software\\Classes\\") + tail;
}

QString verbKey(ShellTarget target, ShellAction action)
{
    return classesKey(shellTargetClassKey(target) + QStringLiteral("\\shell\\")
                      + shellActionRegistryName(action));
}

///
/// \brief 某个目标类的**菜单项**前缀。
///
/// 判「这是不是菜单项」不能用 `key.contains("\\shell\\")`：文件关联那一侧的
/// 命令键（`Software\Classes\LqCompare.PatchFile\shell\open\command`）同样含
/// `\shell\`，但它不是我们加在右键菜单里的项，而是 ProgID 自己的打开动词。
/// 两者混在一起判会导致「关掉右键菜单」这条断言必然失败——而失败的原因
/// 与它想验证的事实（菜单项有没有少）无关。
///
/// 区分办法是看**挂在哪个类键下**：菜单项挂在 `*` / `Directory` /
/// `Directory\Background` 这三个目标类下，关联挂在 ProgID 下。
///
QString menuPrefix(ShellTarget target)
{
    return classesKey(shellTargetClassKey(target) + QStringLiteral("\\shell\\"));
}

/// 菜单项命令所在的键（比动词键深一层）。
QString commandKey(ShellTarget target, ShellAction action)
{
    return verbKey(target, action) + QStringLiteral("\\shell\\command");
}

QString bookkeepingRoot()
{
    return shellBookkeepingRoot();
}

QString backupRoot()
{
    return shellBookkeepingRoot() + QStringLiteral("\\Backup");
}

ShellIntegrationOptions fullOptions()
{
    return ShellIntegrationOptions();
}

ShellIntegrationPlan makePlan(const ShellIntegrationOptions &options)
{
    return buildShellIntegrationPlan(executablePath(), options);
}

/// 把计划拍成一个可比较的清单：同一份输入生成两次必须完全一致。
/// `planIsPureFunctionOfItsInputs` 用它断言「计划里没有藏在时间或地址里的状态」。
QStringList planFingerprint(const ShellIntegrationPlan &plan)
{
    QStringList lines;
    for (const ShellEntry &entry : plan.entries) {
        lines << entry.key + QLatin1Char('\0') + entry.name + QLatin1Char('=')
                        + entry.value.display()
                        + QLatin1String(entry.disposition == ShellEntryDisposition::Owned
                                                ? " [owned]"
                                                : " [shared]");
    }
    return lines;
}

bool mentionOf(const ShellIntegrationReport &report, const QString &needle)
{
    for (const ShellChangeRecord &record : report.records) {
        if (record.detail.contains(needle) || record.purpose.contains(needle))
            return true;
    }
    return false;
}

bool hasFinding(const ShellResidueReport &report, ShellResidueFinding::Kind kind)
{
    for (const ShellResidueFinding &finding : report.findings) {
        if (finding.kind == kind)
            return true;
    }
    return false;
}

/// 记忆体存储里某个键下的某个值是否等于期望。
bool valueIs(const MemoryRegistryStore &store, const QString &key, const QString &name,
             const QString &expected)
{
    RegistryValue value;
    if (!store.value(key, name, &value))
        return false;
    return value.string == expected;
}

///
/// \brief `CommandLineToArgvW` 的规则的一个参考实现，**只用在测试里**。
///
/// 生产代码不需要它：本程序拿到的是 Qt 已经切好的 `argv`。
/// 把它放在这里是为了让引号规则**可被验证**而不是靠人眼看：
/// `quoteShellArgument` 的正确性由「切回来必须还是原值」来证明，
/// 而「结尾反斜杠翻倍」这条规则正是靠这个往返才敢说写对了。
///
QStringList splitWindowsCommandLine(const QString &line)
{
    QStringList arguments;
    QString current;
    bool haveArgument = false;
    bool inQuotes = false;
    int index = 0;

    while (index < line.size()) {
        const QChar character = line.at(index);

        // 引号内的空白**不是**分隔符 —— 这正是引号存在的意义。
        // 少了这一条判断，`"a b" c` 会被切成三个参数，
        // 而含空格的路径（`C:\Program Files\...`）是最常见的输入。
        if (!inQuotes && (character == QLatin1Char(' ') || character == QLatin1Char('\t'))) {
            if (haveArgument) {
                arguments << current;
                current.clear();
                haveArgument = false;
            }
            ++index;
            continue;
        }

        haveArgument = true;

        if (character == QLatin1Char('\\')) {
            int slashes = 0;
            while (index < line.size() && line.at(index) == QLatin1Char('\\')) {
                ++slashes;
                ++index;
            }
            const bool followedByQuote = (index < line.size()
                                          && line.at(index) == QLatin1Char('"'));
            if (followedByQuote) {
                // 2n 个反斜杠 + 引号 → n 个反斜杠，引号是**分隔符**（切换引号状态）；
                // 2n+1 个 → n 个反斜杠 + 一个字面引号。
                current += QString(slashes / 2, QLatin1Char('\\'));
                if (slashes % 2 == 1) {
                    current += QLatin1Char('"');
                } else {
                    inQuotes = !inQuotes;
                }
                ++index;
                continue;
            }
            current += QString(slashes, QLatin1Char('\\'));
            continue;
        }

        if (character == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            ++index;
            continue;
        }

        current += character;
        ++index;
    }

    if (haveArgument)
        arguments << current;

    return arguments;
}

} // namespace

void TstShellIntegration::initTestCase()
{
    // 自检一下测试自身的工具：参考分词器若写错了，
    // 「引号往返」那几条用例会变成在验证一个错的东西，
    // 而且它仍然是绿的。这里先用一个手写的例子把它钉住。
    const QStringList actual = splitWindowsCommandLine(QStringLiteral("\"a b\" c"));
    const QStringList expected{QStringLiteral("a b"), QStringLiteral("c")};
    QVERIFY2(actual == expected,
             qPrintable(QStringLiteral("测试用的分词器不对：%1")
                                .arg(actual.join(QStringLiteral(" | ")))));
}

void TstShellIntegration::cleanupTestCase()
{
    // 本套件全部在内存后端上跑，没有需要清理的系统状态。
    // 刻意留一个空的实现（而不是 = default）：它是一句可被读到的说明——
    // 与 Trash 套件不同，那边必须用 RAII 守卫清理真实废纸篓里的残留。
}

// =============================================================================
// A 动作与目标（标准第 1、2 条）
// =============================================================================

void TstShellIntegration::everyActionHasUniqueIdentifier()
{
    QStringList identifiers;
    const QVector<ShellAction> actions = allShellActions();
    QVERIFY(actions.size() >= 6);

    for (ShellAction action : actions) {
        const QString identifier = QString::fromLatin1(shellActionIdentifier(action));
        QVERIFY2(!identifier.isEmpty(), "动作标识不能为空");
        QVERIFY2(!identifiers.contains(identifier),
                 qPrintable(QStringLiteral("动作标识重复：%1").arg(identifier)));
        identifiers << identifier;
    }
}

void TstShellIntegration::actionIdentifierRoundTrips()
{
    for (ShellAction action : allShellActions()) {
        ShellAction parsed = ShellAction::Compare;
        QVERIFY(shellActionFromIdentifier(
                QString::fromLatin1(shellActionIdentifier(action)), &parsed));
        QVERIFY(parsed == action);
    }
}

void TstShellIntegration::unknownActionIdentifierIsRejected()
{
    ShellAction parsed = ShellAction::Compare;
    QVERIFY(!shellActionFromIdentifier(QStringLiteral("compare_everything"), &parsed));
    QVERIFY(!shellActionFromIdentifier(QString(), &parsed));
    // 大小写不同也不行：命令行开关是机器生成的，容错反而会掩盖生成端的错误。
    QVERIFY(!shellActionFromIdentifier(QStringLiteral("COMPARE"), &parsed));
}

void TstShellIntegration::everyActionHasMenuTextAndDescription()
{
    for (ShellAction action : allShellActions()) {
        QVERIFY(!shellActionMenuText(action).isEmpty());
        QVERIFY(!shellActionDescription(action).isEmpty());
        QVERIFY(!shellActionRegistryName(action).isEmpty());

        if (shellActionIsAssociationOnly(action)) {
            // 文件关联的动作不产生菜单项，因此不需要 `&` 加速键。
            continue;
        }
        QVERIFY2(shellActionMenuText(action).contains(QLatin1Char('&')),
                 "菜单项必须有加速键，否则键盘用户无法快速选中它");
    }
}

void TstShellIntegration::menuTextsUseDistinctAcceleratorsPerTarget()
{
    // 同一个菜单里两个项用同一个加速键时，资源管理器只会让第一个生效，
    // 第二个**永远点不到键盘**——而界面上看不出任何异常。
    for (ShellTarget target : allShellTargets()) {
        QSet<QChar> accelerators;
        const QVector<ShellAction> actions = shellActionsForTarget(target);
        for (ShellAction action : actions) {
            const QString text = shellActionMenuText(action);
            const int marker = text.indexOf(QLatin1Char('&'));
            QVERIFY(marker >= 0 && marker + 1 < text.size());
            const QChar accelerator = text.at(marker + 1);
            QVERIFY2(!accelerators.contains(accelerator),
                     qPrintable(QStringLiteral("加速键重复：%1 里的 %2")
                                        .arg(shellTargetDescription(target), accelerator)));
            accelerators.insert(accelerator);
        }
    }
}

void TstShellIntegration::requiredPathCountMatchesTheAction()
{
    QCOMPARE(shellActionRequiredPathCount(ShellAction::Compare), 2);
    QCOMPARE(shellActionRequiredPathCount(ShellAction::CompareWith), 1);
    QCOMPARE(shellActionRequiredPathCount(ShellAction::CompareAsLeft), 1);
    QCOMPARE(shellActionRequiredPathCount(ShellAction::CompareAsRight), 1);
    QCOMPARE(shellActionRequiredPathCount(ShellAction::CompareSecondStep), 1);
    QCOMPARE(shellActionRequiredPathCount(ShellAction::OpenAssociation), 1);
}

void TstShellIntegration::onlySecondStepIsPlaceholder()
{
    for (ShellAction action : allShellActions()) {
        const bool expected = (action == ShellAction::CompareSecondStep);
        QVERIFY(shellActionIsPlaceholder(action) == expected);
    }
}

void TstShellIntegration::onlyOpenActionIsAssociationOnly()
{
    for (ShellAction action : allShellActions()) {
        const bool expected = (action == ShellAction::OpenAssociation);
        QVERIFY(shellActionIsAssociationOnly(action) == expected);
    }
}

void TstShellIntegration::compareAcceptsManyPathsOthersAcceptOne()
{
    // 「比较」要从 `%1` 里接受多个（用户选中两个才点的菜单），
    // 其余动作必须在菜单层就把选择限制成一个——否则用户选中 20 个文件
    // 点「作为左侧比较」，我们会静默地只用其中一个。
    QCOMPARE(shellActionMaximumPathCount(ShellAction::Compare), -1);
    QCOMPARE(shellActionMaximumPathCount(ShellAction::CompareWith), 1);
    QCOMPARE(shellActionMaximumPathCount(ShellAction::CompareAsLeft), 1);
    QCOMPARE(shellActionMaximumPathCount(ShellAction::CompareAsRight), 1);
    QCOMPARE(shellActionMaximumPathCount(ShellAction::CompareSecondStep), 1);
}

void TstShellIntegration::targetClassKeysAreDistinctAndExpected()
{
    QCOMPARE(shellTargetClassKey(ShellTarget::Files), QStringLiteral("*"));
    QCOMPARE(shellTargetClassKey(ShellTarget::Directories), QStringLiteral("Directory"));
    // 空白处的路径必须是嵌套的一级子键。写成别的形状时资源管理器
    // **不报错、只是不显示**，所以这里把它钉死。
    QCOMPARE(shellTargetClassKey(ShellTarget::Background),
             QStringLiteral("Directory\\Background"));

    QSet<QString> keys;
    for (ShellTarget target : allShellTargets()) {
        const QString key = shellTargetClassKey(target);
        QVERIFY(!keys.contains(key));
        keys.insert(key);
        QVERIFY(!shellTargetDescription(target).isEmpty());
    }
}

void TstShellIntegration::backgroundTargetHasNoPlainCompare()
{
    const QVector<ShellAction> background = shellActionsForTarget(ShellTarget::Background);
    QVERIFY(!background.contains(ShellAction::Compare));

    // 而另外两类对象上必须有它——否则最基本的用法（选中两个文件右键比较）
    // 就不存在了。
    QVERIFY(shellActionsForTarget(ShellTarget::Files).contains(ShellAction::Compare));
    QVERIFY(shellActionsForTarget(ShellTarget::Directories).contains(ShellAction::Compare));
}

void TstShellIntegration::everyTargetOffersTheSameFourCompareActions()
{
    for (ShellTarget target : allShellTargets()) {
        const QVector<ShellAction> actions = shellActionsForTarget(target);
        QVERIFY(actions.contains(ShellAction::CompareWith));
        QVERIFY(actions.contains(ShellAction::CompareAsLeft));
        QVERIFY(actions.contains(ShellAction::CompareAsRight));
        QVERIFY(actions.contains(ShellAction::CompareSecondStep));
        // 文件关联的动作不该出现在菜单里。
        QVERIFY(!actions.contains(ShellAction::OpenAssociation));

        // 没有重复项：重复会让同一个菜单项被写两次（键名相同，后写的覆盖前写的），
        // 表现是「少了一项」，很难看出是重复造成的。
        QSet<int> seen;
        for (ShellAction action : actions) {
            const int key = static_cast<int>(action);
            QVERIFY(!seen.contains(key));
            seen.insert(key);
        }
    }
}

void TstShellIntegration::targetsCoverFilesFoldersAndBackground()
{
    const QVector<ShellTarget> targets = allShellTargets();
    QCOMPARE(targets.size(), 3);
    QVERIFY(targets.contains(ShellTarget::Files));
    QVERIFY(targets.contains(ShellTarget::Directories));
    QVERIFY(targets.contains(ShellTarget::Background));
}

void TstShellIntegration::includedByOptionsRespectsContextMenuSwitch()
{
    ShellIntegrationOptions options;
    options.contextMenu = false;

    QVERIFY(!shellActionIncludedByOptions(ShellAction::Compare, options));
    QVERIFY(!shellActionIncludedByOptions(ShellAction::CompareAsLeft, options));

    options.contextMenu = true;
    QVERIFY(shellActionIncludedByOptions(ShellAction::Compare, options));
}

void TstShellIntegration::includedByOptionsRespectsTwoStepSwitch()
{
    ShellIntegrationOptions options;
    options.twoStepCompare = false;

    // 两步式的两项要一起消失：只关掉其中一个会留下一个「点了没反应」的
    // 占位项——那比没有更糟。
    QVERIFY(!shellActionIncludedByOptions(ShellAction::CompareWith, options));
    QVERIFY(!shellActionIncludedByOptions(ShellAction::CompareSecondStep, options));
    // 而其余项不受影响。
    QVERIFY(shellActionIncludedByOptions(ShellAction::Compare, options));
    QVERIFY(shellActionIncludedByOptions(ShellAction::CompareAsLeft, options));
    QVERIFY(shellActionIncludedByOptions(ShellAction::CompareAsRight, options));
}

void TstShellIntegration::includedByOptionsNeverExposesAssociationAction()
{
    // 即使全部开关都打开，文件关联的动作也不该被当成菜单项安装。
    ShellIntegrationOptions options;
    options.contextMenu = true;
    options.twoStepCompare = true;
    QVERIFY(!shellActionIncludedByOptions(ShellAction::OpenAssociation, options));
}

// =============================================================================
// B 选项（标准第 3 条）
// =============================================================================

void TstShellIntegration::optionsRoundTripThroughString()
{
    ShellIntegrationOptions options;
    options.contextMenu = false;
    options.twoStepCompare = false;
    options.patchAssociation = true;
    options.diffAssociation = false;
    options.menuIcon = false;
    options.positionAtTop = true;

    ShellIntegrationOptions parsed;
    QStringList unknown;
    QVERIFY(shellOptionsFromString(shellOptionsToString(options), &parsed, &unknown));
    QVERIFY(unknown.isEmpty());
    QVERIFY(parsed == options);

    // 默认值也要能原样往返。
    const ShellIntegrationOptions defaults;
    QVERIFY(shellOptionsFromString(shellOptionsToString(defaults), &parsed, &unknown));
    QVERIFY(parsed == defaults);
}

void TstShellIntegration::optionsStringIsReadableInRegedit()
{
    const QString text = shellOptionsToString(fullOptions());
    // 格式刻意选成最土的 `键=值;`：注册表编辑器里要能人肉读懂。
    QVERIFY(text.contains(QStringLiteral("contextMenu=1")));
    QVERIFY(text.contains(QStringLiteral("patchAssociation=")));
    QVERIFY(text.contains(QLatin1Char(';')));
    QVERIFY2(!text.contains(QLatin1Char('{')), "不要用 JSON，regedit 里读不了");
}

void TstShellIntegration::optionsParsingIsCaseInsensitive()
{
    ShellIntegrationOptions parsed;
    QStringList unknown;
    QVERIFY(shellOptionsFromString(
            QStringLiteral("CONTEXTMENU=0;TwoStepCompare=1"), &parsed, &unknown));
    QVERIFY(unknown.isEmpty());
    QVERIFY(!parsed.contextMenu);
    QVERIFY(parsed.twoStepCompare);
}

void TstShellIntegration::unknownOptionKeysAreReported()
{
    ShellIntegrationOptions parsed;
    QStringList unknown;
    QVERIFY(shellOptionsFromString(
            QStringLiteral("contextMenu=1;futureFeature=1"), &parsed, &unknown));

    // 更新版本写下的配置必须被认出来：否则旧版本卸载时会用默认值推导计划，
    // 漏删自己不知道的那些项，而且报告会说「干净」。
    QCOMPARE(unknown, QStringList{QStringLiteral("futureFeature")});
}

void TstShellIntegration::optionsSummaryMentionsWhatIsTurnedOff()
{
    ShellIntegrationOptions options;
    options.contextMenu = false;
    QVERIFY(options.summary().contains(QStringLiteral("不装右键菜单")));

    options = ShellIntegrationOptions();
    options.diffAssociation = false;
    const QString text = options.summary();
    QVERIFY(text.contains(QStringLiteral(".patch")));
    QVERIFY(!text.contains(QStringLiteral(".diff")));
}

void TstShellIntegration::anyEnabledDetectsAllOff()
{
    ShellIntegrationOptions options;
    QVERIFY(options.anyEnabled());

    options.contextMenu = false;
    options.patchAssociation = false;
    options.diffAssociation = false;
    QVERIFY(!options.anyEnabled());

    // 只剩「两步式」不算：它依附于右键菜单，菜单都没装时它无从体现。
    options.twoStepCompare = true;
    QVERIFY(!options.anyEnabled());
}

// =============================================================================
// C 命令行（标准第 1 条）
// =============================================================================

void TstShellIntegration::quotingAlwaysAddsQuotesEvenWithoutSpaces()
{
    // 「没有空格就不加引号」这种优化会在用户把程序装到 `C:\My Tools\` 之后失效，
    // 所以这里要求**总是**加。
    QCOMPARE(quoteShellArgument(QStringLiteral("C:\\Tools\\app.exe")),
             QStringLiteral("\"C:\\Tools\\app.exe\""));
    QCOMPARE(quoteShellArgument(QStringLiteral("a b")), QStringLiteral("\"a b\""));
}

void TstShellIntegration::quotingDoublesTrailingBackslash()
{
    // `"C:\dir\"` 里的 `\"` 会被解析成字面引号，引号因此没有闭合，
    // 后面的参数全部粘进路径里。`"C:\dir\\"` 才是 `C:\dir\`。
    QCOMPARE(quoteShellArgument(QStringLiteral("C:\\dir\\")),
             QStringLiteral("\"C:\\dir\\\\\""));
    QCOMPARE(quoteShellArgument(QStringLiteral("C:\\a\\\\")),
             QStringLiteral("\"C:\\a\\\\\\\\\""));
    // 结尾不是反斜杠时不受影响。
    QCOMPARE(quoteShellArgument(QStringLiteral("C:\\dir")),
             QStringLiteral("\"C:\\dir\""));
}

void TstShellIntegration::quotingEscapesEmbeddedQuote()
{
    // Windows 文件名里不可能有引号，但插件/脚本可能传进来，
    // 而未转义的引号会让后续所有参数错位。
    QCOMPARE(quoteShellArgument(QStringLiteral("a\"b")),
             QStringLiteral("\"a\\\"b\""));
}

void TstShellIntegration::quotingEmptyStringGivesEmptyQuoted()
{
    // 空参数必须留一对引号，否则它会被整个吞掉，
    // 后面的参数因此左移一位——调用方拿到的参数个数是对的，内容全错。
    QCOMPARE(quoteShellArgument(QString()), QStringLiteral("\"\""));
}

void TstShellIntegration::quotedArgumentsSurviveWindowsSplitting()
{
    // 这一条才是引号规则的真正验证：拼出来再按 Windows 的规则切回来，
    // 必须与原值逐个相同。只看 `quoteShellArgument` 的输出「长得对不对」
    // 是看不出结尾反斜杠那条规则的。
    const QStringList samples{
        QStringLiteral("C:\\Program Files\\LqCompare\\LqCompare.exe"),
        QStringLiteral("C:\\dir\\"),
        QStringLiteral("C:\\a\\\\"),
        QStringLiteral("C:\\plain\\app.exe"),
        QStringLiteral("D:\\数据 目录\\文件.txt"),
        QStringLiteral("relative.exe"),
        QString(),
    };

    for (const QString &sample : samples) {
        const QStringList roundTripped =
                splitWindowsCommandLine(quoteShellArgument(sample));
        QVERIFY2(roundTripped.size() == 1,
                 qPrintable(QStringLiteral("「%1」切回来变成了 %2 个参数")
                                    .arg(sample)
                                    .arg(roundTripped.size())));
        QCOMPARE(roundTripped.first(), sample);
    }
}

void TstShellIntegration::invocationLineQuotesPlaceholderExactlyOnce()
{
    const QString line = buildShellInvocationLine(executablePath(), ShellAction::Compare);

    // `"%1"` 只能有一对引号。写成 `""%1""` 时资源管理器替换之后
    // 会多出一对空引号，变成一个空参数混进参数表。
    QVERIFY(line.endsWith(QStringLiteral(" \"%1\"")));
    QVERIFY(!line.contains(QStringLiteral("\"\"%1")));

    // 切回来时，`%1` 位置在替换前是一个占位符（这里用真实路径代替它验证引号规则）。
    const QString substituted = line;
    QVERIFY(substituted.contains(QStringLiteral("\"%1\"")));
}

void TstShellIntegration::invocationLineCarriesTheActionSwitch()
{
    const QString line = buildShellInvocationLine(executablePath(), ShellAction::CompareAsLeft);
    QVERIFY(line.contains(QStringLiteral("--shell-action=compare_as_left")));
    QVERIFY(line.startsWith(QStringLiteral("\"C:\\Program Files\\")));

    // 开关与路径的顺序要稳定：本程序的解析依赖它，测试也依赖它。
    const QString compareWith =
            buildShellInvocationLine(executablePath(), ShellAction::CompareWith);
    QVERIFY(compareWith.contains(QStringLiteral("--shell-action=compare_with")));
}

void TstShellIntegration::singleItemPlaceholderIsAlreadyQuoted()
{
    // 占位符由调用方给成**已带引号**的形式，buildShellCommandLine 不再加工。
    // 这一条把契约钉住：未来有人在 buildShellCommandLine 里补一次 quote
    // 会立刻让这里失败，而不是等到用户在带空格的文件名上遇到怪事。
    QCOMPARE(QString::fromLatin1(ShellPlaceholder::SingleItem), QStringLiteral("\"%1\""));
    QCOMPARE(QString::fromLatin1(ShellPlaceholder::TargetItem), QStringLiteral("\"%V\""));
}

void TstShellIntegration::associationCommandUsesOpenAction()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    const QString openKey = classesKey(QStringLiteral("LqCompare.PatchFile\\shell\\open\\command"));
    const QVector<ShellEntry> entries = plan.entriesFor(openKey);
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries.first().value.string.contains(QStringLiteral("--shell-action=open")));
}

// =============================================================================
// D 计划
// =============================================================================

void TstShellIntegration::planIsPureFunctionOfItsInputs()
{
    QCOMPARE(planFingerprint(makePlan(fullOptions())),
             planFingerprint(makePlan(fullOptions())));

    // 选项不同时必须真的不同（否则上面那条可能只是「两次都是空的」）。
    ShellIntegrationOptions other = fullOptions();
    other.menuIcon = false;
    QVERIFY(planFingerprint(makePlan(other)) != planFingerprint(makePlan(fullOptions())));
}

void TstShellIntegration::planWithoutContextMenuHasNoMenuEntries()
{
    ShellIntegrationOptions options;
    options.contextMenu = false;
    const ShellIntegrationPlan plan = makePlan(options);

    for (const ShellEntry &entry : plan.entries) {
        for (ShellTarget target : allShellTargets()) {
            // 只对**菜单项前缀**做判断：关联侧的命令键（ProgID 下的 open 动词）
            // 本来就含 `\shell\`，把它算成菜单项是这次断言写错的原因。
            QVERIFY2(!entry.key.startsWith(menuPrefix(target)),
                     qPrintable(QStringLiteral("关掉右键菜单后仍有菜单项：%1").arg(entry.key)));
        }
    }
    // 但文件关联还在。
    QCOMPARE(plan.extensions.size(), 2);
}

void TstShellIntegration::planWithoutTwoStepOmitsBothTwoStepItems()
{
    ShellIntegrationOptions options;
    options.twoStepCompare = false;
    const ShellIntegrationPlan plan = makePlan(options);

    const QString withKey = verbKey(ShellTarget::Files, ShellAction::CompareWith);
    const QString stepKey = verbKey(ShellTarget::Files, ShellAction::CompareSecondStep);
    QVERIFY(plan.entriesFor(withKey).isEmpty());
    QVERIFY(plan.entriesFor(stepKey).isEmpty());
    // 而「比较」与左右两项还在。
    QVERIFY(!plan.entriesFor(verbKey(ShellTarget::Files, ShellAction::Compare)).isEmpty());
    QVERIFY(!plan.entriesFor(verbKey(ShellTarget::Files, ShellAction::CompareAsLeft)).isEmpty());
    QVERIFY(!plan.entriesFor(verbKey(ShellTarget::Files, ShellAction::CompareAsRight)).isEmpty());
}

void TstShellIntegration::planWithoutPatchOmitsPatchEntries()
{
    ShellIntegrationOptions options;
    options.patchAssociation = false;
    const ShellIntegrationPlan plan = makePlan(options);

    // 单独关掉一个扩展名时，另一个必须完好——这正是「可单独关闭」的含义。
    QVERIFY(!plan.extensions.contains(QStringLiteral(".patch")));
    QVERIFY(plan.extensions.contains(QStringLiteral(".diff")));
    QVERIFY(plan.entriesFor(classesKey(QStringLiteral(".patch"))).isEmpty());
    QVERIFY(!plan.entriesFor(classesKey(QStringLiteral(".diff"))).isEmpty());
    QVERIFY(!plan.entriesFor(
                    classesKey(QStringLiteral("LqCompare.PatchFile\\shell\\open\\command")))
                     .isEmpty()
            == false);
}

void TstShellIntegration::planWithoutDiffOmitsDiffEntries()
{
    ShellIntegrationOptions options;
    options.diffAssociation = false;
    const ShellIntegrationPlan plan = makePlan(options);

    QVERIFY(!plan.extensions.contains(QStringLiteral(".diff")));
    QVERIFY(plan.progIds.contains(QStringLiteral("LqCompare.PatchFile")));
    QVERIFY(!plan.progIds.contains(QStringLiteral("LqCompare.DiffFile")));
}

void TstShellIntegration::planWithNothingEnabledIsEmpty()
{
    ShellIntegrationOptions options;
    options.contextMenu = false;
    options.twoStepCompare = false;
    options.patchAssociation = false;
    options.diffAssociation = false;

    const ShellIntegrationPlan plan = makePlan(options);
    QVERIFY(plan.isEmpty());
    QCOMPARE(plan.valueCount(), 0);
    QVERIFY(plan.summary().contains(QStringLiteral("没有任何可安装的项")));
}

void TstShellIntegration::planMenuTextMatchesAction()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    for (ShellTarget target : allShellTargets()) {
        for (ShellAction action : shellActionsForTarget(target)) {
            if (!shellActionIncludedByOptions(action, fullOptions()))
                continue;

            // 菜单文字挂在**动词键**上（`...\shell\LqCompare.compare`），
            // 命令挂在它下面一层的 `shell\command` 上。两者不在同一个键里，
            // 所以必须分两次取——用动词键去取命令会一条都取不到。
            const QVector<ShellEntry> entries = plan.entriesFor(verbKey(target, action));
            bool foundText = false;
            for (const ShellEntry &entry : entries) {
                if (entry.name == QLatin1String("MUIVerb")) {
                    foundText = true;
                    QCOMPARE(entry.value.string, shellActionMenuText(action));
                }
            }
            QVERIFY2(foundText, qPrintable(QStringLiteral("缺少菜单文字：%1/%2")
                                                   .arg(shellTargetClassKey(target),
                                                        shellActionRegistryName(action))));

            const QVector<ShellEntry> commands = plan.entriesFor(commandKey(target, action));
            QVERIFY2(!commands.isEmpty(),
                     qPrintable(QStringLiteral("缺少命令项：%1").arg(commandKey(target, action))));
            // 命令必须写在 (Default) 上——资源管理器只读这个值的默认值来执行。
            for (const ShellEntry &entry : commands) {
                if (entry.name.isEmpty())
                    QCOMPARE(entry.value.string, buildShellInvocationLine(executablePath(), action));
            }
        }
    }
}

void TstShellIntegration::planRestrictsSinglePathActionsToOneSelection()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    for (ShellTarget target : allShellTargets()) {
        for (ShellAction action : shellActionsForTarget(target)) {
            if (!shellActionIncludedByOptions(action, fullOptions()))
                continue;

            const QVector<ShellEntry> entries = plan.entriesFor(verbKey(target, action));
            QString model;
            for (const ShellEntry &entry : entries) {
                if (entry.name == QLatin1String("MultiSelectModel"))
                    model = entry.value.string;
            }

            const QString expected = (shellActionMaximumPathCount(action) == 1)
                    ? QStringLiteral("Single")
                    : QStringLiteral("Document");
            QCOMPARE(model, expected);
        }
    }
}

void TstShellIntegration::planPositionOnlyWhenAtTop()
{
    ShellIntegrationOptions options = fullOptions();
    options.positionAtTop = false;
    const ShellIntegrationPlan plan = makePlan(options);

    for (const ShellEntry &entry : plan.entries)
        QVERIFY(entry.name != QLatin1String("Position"));

    options.positionAtTop = true;
    const ShellIntegrationPlan positioned = makePlan(options);
    bool found = false;
    for (const ShellEntry &entry : positioned.entries) {
        if (entry.name == QLatin1String("Position")) {
            QCOMPARE(entry.value.string, QStringLiteral("Top"));
            found = true;
        }
    }
    QVERIFY(found);
}

void TstShellIntegration::planIconOnlyWhenMenuIconEnabled()
{
    ShellIntegrationOptions options = fullOptions();
    options.menuIcon = false;
    const ShellIntegrationPlan plan = makePlan(options);

    for (const ShellEntry &entry : plan.entries)
        QVERIFY(entry.name != QLatin1String("Icon"));

    const ShellIntegrationPlan withIcons = makePlan(fullOptions());
    int iconCount = 0;
    for (const ShellEntry &entry : withIcons.entries) {
        if (entry.name == QLatin1String("Icon"))
            ++iconCount;
    }
    QVERIFY(iconCount > 0);
}

void TstShellIntegration::planIconPointsAtExecutableIndexZero()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    for (const ShellEntry &entry : plan.entries) {
        if (entry.name != QLatin1String("Icon"))
            continue;
        // 格式固定是 `"可执行文件",索引`。索引指向不存在的资源时资源管理器
        // 显示空白占位而不是报错，所以这个值必须与打包方式一起改——
        // 这里把「当前用的是索引 0」钉住，改打包时要一起改这里。
        QVERIFY(entry.value.string.startsWith(quoteShellArgument(executablePath())));
        QVERIFY(entry.value.string.endsWith(QStringLiteral(",0")));
    }
}

void TstShellIntegration::planCommandUsesQuotedExecutableAndPlaceholder()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    int commandCount = 0;

    for (const ShellEntry &entry : plan.entries) {
        if (!entry.key.endsWith(QStringLiteral("\\shell\\command"))
                && !entry.key.endsWith(QStringLiteral("\\shell\\open\\command"))) {
            continue;
        }
        ++commandCount;
        QVERIFY(entry.value.string.startsWith(quoteShellArgument(executablePath())));
        QVERIFY(entry.value.string.contains(QStringLiteral("\"%1\"")));
        QVERIFY(entry.value.string.contains(QStringLiteral("--shell-action=")));
    }
    QVERIFY(commandCount > 0);
}

void TstShellIntegration::extensionDefaultIsSharedWhileProgIdIsOwned()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    const QVector<ShellEntry> patch = plan.entriesFor(classesKey(QStringLiteral(".patch")));
    QCOMPARE(patch.size(), 1);
    // `.patch` 这个键几乎一定早就在（属于别的程序或 Windows 自己），
    // 因此它的默认值必须按「借用」处理：安装前备份、卸载时还原。
    QVERIFY(patch.first().disposition == ShellEntryDisposition::Shared);
    QCOMPARE(patch.first().name, QString());

    const QVector<ShellEntry> progId =
            plan.entriesFor(classesKey(QStringLiteral("LqCompare.PatchFile")));
    QCOMPARE(progId.size(), 1);
    QVERIFY(progId.first().disposition == ShellEntryDisposition::Owned);

    // 共享键必须出现在备份清单里，否则卸载时会直接删掉整条关联。
    QVERIFY(plan.backupKeys().contains(classesKey(QStringLiteral(".patch"))));
    QVERIFY(plan.backupKeys().contains(classesKey(QStringLiteral(".diff"))));
    QVERIFY(!plan.ownedKeys.contains(classesKey(QStringLiteral(".patch"))));
}

void TstShellIntegration::planKeysAreAllRelativeToStoreRoot()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QCOMPARE(plan.rootKey, QStringLiteral("HKEY_CURRENT_USER"));

    for (const QString &key : plan.keys()) {
        QVERIFY2(!key.contains(QStringLiteral("HKEY_")),
                 "计划里的键必须是相对存储根的路径，不能自带根名");
        QVERIFY2(key.startsWith(QStringLiteral("Software\\")),
                 qPrintable(QStringLiteral("键不在 HKCU\\Software 下：%1").arg(key)));
    }
    for (const QString &key : plan.ownedKeys) {
        QVERIFY(!key.contains(QStringLiteral("HKEY_")));
        QVERIFY(key.startsWith(QStringLiteral("Software\\")));
    }
}

void TstShellIntegration::planSummaryMentionsBackupCount()
{
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    const QString summary = plan.summary();
    QVERIFY(summary.contains(QStringLiteral("可能已有内容")));
    QVERIFY(summary.contains(QStringLiteral("%1").arg(plan.backupKeys().size())));
    QVERIFY(plan.detailLines().join(QLatin1Char('\n')).contains(QStringLiteral(".patch")));
}

void TstShellIntegration::planDetailLinesListTargetsAndExtensions()
{
    const QStringList lines = makePlan(fullOptions()).detailLines();
    const QString text = lines.join(QLatin1Char('\n'));

    for (ShellTarget target : allShellTargets())
        QVERIFY2(text.contains(shellTargetDescription(target)), "预览里必须列出每一类对象");
    QVERIFY(text.contains(QStringLiteral(".patch")));
    QVERIFY(text.contains(QStringLiteral(".diff")));

    // 关掉右键菜单之后，预览要如实说不装，而不是继续列菜单项。
    ShellIntegrationOptions options;
    options.contextMenu = false;
    options.patchAssociation = false;
    options.diffAssociation = false;
    const QString off = makePlan(options).detailLines().join(QLatin1Char('\n'));
    QVERIFY(off.contains(QStringLiteral("右键菜单：不安装")));
    QVERIFY(off.contains(QStringLiteral("文件关联：不安装")));
}

void TstShellIntegration::planKeysAreSortedShallowestFirst()
{
    const QStringList keys = makePlan(fullOptions()).keys();
    QVERIFY(!keys.isEmpty());

    int previousDepth = 0;
    for (const QString &key : keys) {
        const int depth = RegistryStore::keyDepth(key);
        QVERIFY2(depth >= previousDepth, "报告里的键要按深度递增排列，便于对照阅读");
        previousDepth = depth;
    }
}

// =============================================================================
// E 安装（标准第 4 条）
// =============================================================================

void TstShellIntegration::installWritesEveryPlannedValue()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    const ShellIntegrationReport report = integration.install(plan);
    QVERIFY2(report.allSucceeded(), qPrintable(report.lines().join(QLatin1Char('\n'))));
    QVERIFY(!report.rolledBack);

    for (const ShellEntry &entry : plan.entries) {
        RegistryValue actual;
        QVERIFY2(store.value(entry.key, entry.name, &actual),
                 qPrintable(QStringLiteral("没有写进去：%1").arg(entry.describe())));
        QVERIFY2(actual == entry.value,
                 qPrintable(QStringLiteral("值不对：%1，实际 %2")
                                    .arg(entry.describe(), actual.display())));
    }

    // 登记信息也要在。
    RegistryValue version;
    QVERIFY(store.value(bookkeepingRoot(), QStringLiteral("Version"), &version));
    QCOMPARE(version.dword, quint32(1));
}

void TstShellIntegration::installMarksCompletionWithVersionOne()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    integration.install(makePlan(fullOptions()));

    const ShellIntegration::InstalledState state = integration.installedState();
    QVERIFY(state.installed);
    QVERIFY(!state.installing);
    QCOMPARE(state.version, shellBookkeepingVersion());
    QVERIFY(state.summary().contains(QStringLiteral("已安装")));
}

void TstShellIntegration::installRecordsExecutablePathAndOptions()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    ShellIntegrationOptions options;
    options.diffAssociation = false;
    options.menuIcon = false;
    integration.install(makePlan(options));

    const ShellIntegration::InstalledState state = integration.installedState();
    QCOMPARE(state.executablePath, executablePath());
    QVERIFY(state.options == options);
    // 备份只针对开了的那一个扩展名。
    QCOMPARE(state.backupCount, 1);
}

void TstShellIntegration::installRefusesWhenBackendUnavailable()
{
#ifdef Q_OS_WIN
    QSKIP("Windows 上原生后端可用，这一条针对「平台没有注册表」的情形");
#else
    QScopedPointer<RegistryStore> native(createNativeRegistryStore());
    ShellIntegration integration(native.data());

    const ShellIntegrationReport report = integration.install(makePlan(fullOptions()));

    QVERIFY(!report.allSucceeded());
    // 只报一条记录说明原因，不铺成几十条「不支持」——后者会把真正的失败淹没。
    QCOMPARE(report.records.size(), 1);
    QVERIFY(report.records.first().error.category == Files::FileSystemError::NotSupported);
    QVERIFY(!report.records.first().detail.isEmpty());
#endif
}

void TstShellIntegration::installWithNothingEnabledDoesNotTouchStore()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    ShellIntegrationOptions options;
    options.contextMenu = false;
    options.twoStepCompare = false;
    options.patchAssociation = false;
    options.diffAssociation = false;

    const ShellIntegrationReport report = integration.install(makePlan(options));
    QVERIFY(report.allSucceeded());
    QVERIFY(store.allKeys().isEmpty());
    QVERIFY(store.allValues().isEmpty());
    QCOMPARE(store.writeCallCount(), 0);
    QVERIFY(report.summary().contains(QStringLiteral("没有勾选")));
}

void TstShellIntegration::installBacksUpSharedValueBeforeOverwriting()
{
    MemoryRegistryStore store;
    const QString previous = QStringLiteral("Other.Tool.Patch");
    store.setValue(classesKey(QStringLiteral(".patch")), QString(), RegistryValue::of(previous));

    ShellIntegration integration(&store);
    const ShellIntegrationReport report = integration.install(makePlan(fullOptions()));
    QVERIFY(report.allSucceeded());

    // 覆盖成功了。
    QVERIFY(valueIs(store, classesKey(QStringLiteral(".patch")), QString(),
                    QStringLiteral("LqCompare.PatchFile")));

    // 而且备份里留着原值——这条断言才是「卸载能还原」的全部依据。
    QVERIFY(mentionOf(report, previous));
    bool foundBackup = false;
    for (const QString &index : store.subKeys(backupRoot())) {
        RegistryValue text;
        if (store.value(backupRoot() + QLatin1Char('\\') + index,
                        QStringLiteral("Text"), &text)
                && text.string == previous) {
            foundBackup = true;
        }
    }
    QVERIFY2(foundBackup, "备份里没有原值，卸载将无法还原");
}

void TstShellIntegration::installIsAtomicWhenOneEntryFails()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    // 让 `.patch` 的关联**写入**失败一次。此时前面几十条已经写进去了，
    // 正是「装了一半」那种用户看不懂的状态。
    //
    // 这里必须用 failNextWriteOnValue() 而不是 failOnValue()：后者会把
    // 该值的**删除**也一起挡住，于是回滚必然也失败，`rollbackClean`
    // 永远是假——而「回滚逻辑写错了」与「注册表真的删不掉」从结果上
    // 完全分不开。一次性注入只挡这一次写入，回滚是自由的，
    // 于是 `rollbackClean` 才真的在断言「回滚把写进去的都删干净了」。
    store.failNextWriteOnValue(classesKey(QStringLiteral(".patch")), QString(),
                               Files::ErrorCode(Files::FileSystemError::PermissionDenied));
    const ShellIntegrationReport report = integration.install(plan);

    QVERIFY(!report.allSucceeded());
    QVERIFY2(report.rolledBack, "安装失败必须回滚：半装的 Shell 集成用户看不懂也修不了");
    QVERIFY2(report.rollbackClean, qPrintable(report.lines().join(QLatin1Char('\n'))));
    QVERIFY(report.summary().contains(QStringLiteral("回滚")));
}

void TstShellIntegration::failedInstallLeavesStoreEmpty()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    // 同样用一次性注入（原因见上一条）。用 failOnValue() 的话，
    // 「回滚之后一条不剩」这条断言测不出东西：写入失败了所以本来就没写进去，
    // 而删除又被挡住，无论回滚写没写对，存储都是空的。
    store.failNextWriteOnValue(classesKey(QStringLiteral(".patch")), QString(),
                               Files::ErrorCode(Files::FileSystemError::PermissionDenied));
    integration.install(makePlan(fullOptions()));

    // 回滚之后必须一条不剩。留下任何一条都是「卸载后无残留」的反例。
    QVERIFY2(store.allValues().isEmpty(),
             qPrintable(store.allValues().join(QStringLiteral("\n"))));
    QVERIFY2(store.allKeys().isEmpty(),
             qPrintable(store.allKeys().join(QStringLiteral("\n"))));
}

void TstShellIntegration::failedDuringBookkeepingLeavesStoreEmpty()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    // 让登记那一步就失败：这是最早的可能失败点，此时注册表几乎还是空的。
    //
    // 这一条**故意**用持续注入（failOnValue）而不是一次性注入：它要验证的是
    // 「即使这个值一直写不进去，回滚也能把注册表清干净」。之所以能做到，
    // 是因为回滚删登记那一整棵子树用的是**删键**（removeKey），
    // 而 failOnValue 只挡「某个值的删与写」。这个性质值得钉住：
    // 如果哪天回滚改成逐个 removeValue，这条用例会立刻变红。
    store.failOnValue(bookkeepingRoot(), QStringLiteral("Version"),
                      Files::ErrorCode(Files::FileSystemError::PermissionDenied));
    const ShellIntegrationReport report = integration.install(makePlan(fullOptions()));

    // 刻意不调用 clearInjectedFailures()：注入仍然生效时存储就该是空的，
    // 清掉再断言等于放宽了条件。
    QVERIFY(!report.allSucceeded());
    QVERIFY(report.rolledBack);
    QVERIFY(store.allValues().isEmpty());
    QVERIFY(store.allKeys().isEmpty());
}

void TstShellIntegration::installTwiceKeepsTheOriginalBackup()
{
    MemoryRegistryStore store;
    const QString previous = QStringLiteral("Other.Tool.Diff");
    store.setValue(classesKey(QStringLiteral(".diff")), QString(), RegistryValue::of(previous));

    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    QVERIFY(integration.install(plan).allSucceeded());
    QVERIFY(integration.install(plan).allSucceeded());

    // 第二次安装不能把「当前值（我们的 ProgID）」当成原值备份下来，
    // 否则卸载会把 `.diff` 还原成我们的 ProgID——用户看到的现象是
    // 「卸载之后补丁文件还是被这个程序接管」，而报告说「已还原」。
    const ShellIntegration::InstalledState state = integration.installedState();
    QCOMPARE(state.backupCount, 2);

    const ShellIntegrationReport removal = integration.uninstall();
    QVERIFY2(removal.allSucceeded(), qPrintable(removal.lines().join(QLatin1Char('\n'))));
    QVERIFY(valueIs(store, classesKey(QStringLiteral(".diff")), QString(), previous));
}

void TstShellIntegration::previewInstallDoesNotTouchRealStore()
{
    MemoryRegistryStore real;
    real.setValue(classesKey(QStringLiteral(".diff")), QString(), RegistryValue::of(
                                                               QStringLiteral("Other.Tool")));

    ShellIntegration integration(&real);
    const QStringList before = real.allValues();

    MemoryRegistryStore scratch;
    const ShellIntegrationReport report =
            integration.previewInstall(makePlan(fullOptions()), &scratch);

    QVERIFY(report.allSucceeded());
    QCOMPARE(report.operationName, QStringLiteral("preview"));
    QVERIFY(report.summary().contains(QStringLiteral("预演")));
    // 真实存储一个字节都不能动。
    QCOMPARE(real.allValues(), before);
}

// =============================================================================
// F 卸载与还原（标准第 3、4 条）
// =============================================================================

void TstShellIntegration::uninstallRestoresPreviousAssociation()
{
    MemoryRegistryStore store;
    const QString previous = QStringLiteral("Notepad.PatchFile");
    store.setValue(classesKey(QStringLiteral(".patch")), QString(), RegistryValue::of(previous));

    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());

    const ShellIntegrationReport removal = integration.uninstall();
    QVERIFY2(removal.allSucceeded(), qPrintable(removal.lines().join(QLatin1Char('\n'))));

    // 这一条是本套件最要紧的断言。把 `.patch` 当「我们的键」删掉的实现
    // 会让用户原有的关联一起消失，而用户不会把「双击 .patch 没反应了」
    // 联想到几天前装过的一个比较工具。
    QVERIFY(valueIs(store, classesKey(QStringLiteral(".patch")), QString(), previous));
}

void TstShellIntegration::uninstallDeletesOurValuesAndKeys()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());
    QVERIFY(!store.allValues().isEmpty());

    const ShellIntegrationReport removal = integration.uninstall();
    QVERIFY2(removal.allSucceeded(), qPrintable(removal.lines().join(QLatin1Char('\n'))));

    // 没有预先存在的关联时，卸载之后注册表必须回到空。
    QVERIFY2(store.allValues().isEmpty(),
             qPrintable(store.allValues().join(QStringLiteral("\n"))));
    QVERIFY2(store.allKeys().isEmpty(),
             qPrintable(store.allKeys().join(QStringLiteral("\n"))));

    // 登记子树也整棵删掉。
    QVERIFY(!store.keyExists(bookkeepingRoot()));
}

void TstShellIntegration::uninstallRemovesValueThatNeverExisted()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());
    QVERIFY(integration.uninstall().allSucceeded());

    // `.patch` 键原本不存在，我们写的默认值也必须消失——
    // 留下它等于把用户的关联指向一个已卸载的程序。
    RegistryValue value;
    QVERIFY(!store.value(classesKey(QStringLiteral(".patch")), QString(), &value));
    QVERIFY(!store.value(classesKey(QStringLiteral(".diff")), QString(), &value));
}

void TstShellIntegration::uninstallDeletesKeyWeCreated()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());
    // 安装时 `.patch` 键不存在（store 是空的）→ 它由我们创建。
    QVERIFY(store.keyExists(classesKey(QStringLiteral(".patch"))));

    QVERIFY(integration.uninstall().allSucceeded());

    // 由我们创建的键要删掉，否则留下一个空的 `.patch` 键。
    // 它不影响功能，但会让「注册表无残留」这句话不成立。
    QVERIFY(!store.keyExists(classesKey(QStringLiteral(".patch"))));
}

void TstShellIntegration::uninstallKeepsKeyWeDidNotCreate()
{
    MemoryRegistryStore store;
    // `.diff` 键本来就存在（只是默认值是空的）。它属于别人，我们不能删。
    store.touchKey(classesKey(QStringLiteral(".diff")));

    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());
    QVERIFY(integration.uninstall().allSucceeded());

    QVERIFY2(store.keyExists(classesKey(QStringLiteral(".diff"))),
             "这个键安装前就存在，卸载不能把它删掉");
}

void TstShellIntegration::uninstallIsIdempotent()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());

    QVERIFY(integration.uninstall().allSucceeded());
    const ShellIntegrationReport second = integration.uninstall();

    // 用户点两次卸载不该看到红色失败：第二次的结果「什么都没有」，
    // 与第一次的目的一致。
    QVERIFY(second.allSucceeded());
    QVERIFY(second.summary().contains(QStringLiteral("没有找到安装记录")));
    QVERIFY(store.allKeys().isEmpty());
}

void TstShellIntegration::uninstallWithoutInstallReportsNotInstalled()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    const ShellIntegrationReport report = integration.uninstall();
    QVERIFY(report.allSucceeded());
    QVERIFY(report.summary().contains(QStringLiteral("没有找到安装记录")));
    QCOMPARE(store.writeCallCount(), 0);
}

void TstShellIntegration::reconfigureRemovesEntriesOfRemovedOptions()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    // 第一次：全装。
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());
    const QString compareKey = verbKey(ShellTarget::Files, ShellAction::Compare);
    const QString patchProgId = classesKey(QStringLiteral("LqCompare.PatchFile"));
    QVERIFY(store.keyExists(compareKey));
    QVERIFY(store.keyExists(patchProgId));

    // 第二次：用户取消勾选右键菜单与 `.patch`，只留 `.diff`，再点一次安装。
    ShellIntegrationOptions reduced;
    reduced.contextMenu = false;
    reduced.patchAssociation = false;
    const ShellIntegrationReport second = integration.install(makePlan(reduced));
    QVERIFY2(second.allSucceeded(), qPrintable(second.lines().join(QLatin1Char('\n'))));
    QVERIFY(second.summary().contains(QStringLiteral("已先按原选项拆除")));

    // 「安装」的语义是「把注册表调成与当前勾选项一致」，不是叠加。
    // 若只是叠加，旧选项独有的那些项会留下来，而登记（卸载的依据）
    // 写的是新选项——卸载时按新选项推导，那些项不会被删，
    // 而残留检查会说「通过」。用户看到的是「卸载了但 .patch 还是被占用」。
    QVERIFY2(!store.keyExists(compareKey), "被取消勾选的菜单项必须从注册表里消失");
    QVERIFY2(!store.keyExists(patchProgId), "被取消勾选的 ProgID 必须从注册表里消失");
    QVERIFY(!store.keyExists(classesKey(QStringLiteral(".patch"))));

    // 而保留下来的 `.diff` 仍然完好（不能顺手把整块都清了）。
    QVERIFY(store.keyExists(classesKey(QStringLiteral("LqCompare.DiffFile"))));
    QVERIFY(valueIs(store, classesKey(QStringLiteral(".diff")), QString(),
                    QStringLiteral("LqCompare.DiffFile")));

    // 再卸载一次，必须无残留——这一条才是上面那件事的最终验收标准。
    const ShellIntegrationReport removal = integration.uninstall();
    QVERIFY2(removal.allSucceeded(), qPrintable(removal.lines().join(QLatin1Char('\n'))));
    QVERIFY2(removal.residueClean,
             qPrintable(removal.residueLines.join(QStringLiteral("\n"))));
    QVERIFY(store.allKeys().isEmpty());
}

void TstShellIntegration::uninstallRestoresUnsupportedValueKindByteForByte()
{
    MemoryRegistryStore store;
    // 用户原有的关联可能是我们不解释的类型（REG_MULTI_SZ / REG_BINARY …）。
    // 读不出来时若记成「本来没有值」，卸载就会删掉它——那是销毁用户数据。
    const QByteArray raw("a\0b\0\0", 5);
    store.setValue(classesKey(QStringLiteral(".patch")), QString(),
                   RegistryValue::ofRaw(7 /* REG_MULTI_SZ */, raw));

    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());
    QVERIFY(integration.uninstall().allSucceeded());

    RegistryValue restored;
    QVERIFY(store.value(classesKey(QStringLiteral(".patch")), QString(), &restored));
    QVERIFY(restored.kind == RegistryValueKind::Unsupported);
    QCOMPARE(restored.rawType, quint32(7));
    QVERIFY2(restored.raw == raw, "未知类型的值必须逐字节还原");
}

void TstShellIntegration::uninstallCleansInterruptedInstall()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    // 手工造出「安装被中断」的现场：登记写下了、版本号停在 0。
    // 这是断电、被结束进程、崩溃时的真实状态。
    store.setValue(bookkeepingRoot(), QStringLiteral("Version"),
                   RegistryValue::ofNumber(0));
    store.setValue(bookkeepingRoot(), QStringLiteral("Executable"),
                   RegistryValue::of(executablePath()));
    store.setValue(bookkeepingRoot(), QStringLiteral("Options"),
                   RegistryValue::of(shellOptionsToString(fullOptions())));

    const ShellIntegration::InstalledState state = integration.installedState();
    QVERIFY(state.installed);
    QVERIFY(state.installing);
    QVERIFY(state.summary().contains(QStringLiteral("没有完成")));

    const ShellIntegrationReport report = integration.uninstall();
    QVERIFY(report.allSucceeded());
    QVERIFY(report.summary().contains(QStringLiteral("半装")));
    QVERIFY(store.allKeys().isEmpty());
}

void TstShellIntegration::uninstallAfterPartialWriteLeavesNoResidue()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());

    // 登记在半路，只写进去一部分条目 —— 模拟「写到第 20 条时断电」。
    store.setValue(bookkeepingRoot(), QStringLiteral("Version"), RegistryValue::ofNumber(0));
    store.setValue(bookkeepingRoot(), QStringLiteral("Executable"),
                   RegistryValue::of(executablePath()));
    store.setValue(bookkeepingRoot(), QStringLiteral("Options"),
                   RegistryValue::of(shellOptionsToString(plan.options)));
    for (int i = 0; i < 20 && i < plan.entries.size(); ++i) {
        const ShellEntry &entry = plan.entries.at(i);
        store.setValue(entry.key, entry.name, entry.value);
    }

    const ShellIntegrationReport report = integration.uninstall();
    QVERIFY2(report.allSucceeded(), qPrintable(report.lines().join(QLatin1Char('\n'))));
    QVERIFY(store.allKeys().isEmpty());
    QVERIFY(report.residueClean);
}

void TstShellIntegration::uninstallKeepsForeignContentAndSaysSo()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());

    // 有人往我们的菜单项键里塞了一个值（比如用户手工加的，或另一个工具）。
    const QString compareKey = verbKey(ShellTarget::Files, ShellAction::Compare);
    store.setValue(compareKey, QStringLiteral("SomeoneElse"),
                   RegistryValue::of(QStringLiteral("do not delete me")));

    const ShellIntegrationReport report = integration.uninstall();

    // 别人的东西必须留下，而且必须**说出来**。默默删掉是数据损失；
    // 默默留下而不报告会让「无残留」这句话变成假的。
    QVERIFY(valueIs(store, compareKey, QStringLiteral("SomeoneElse"),
                    QStringLiteral("do not delete me")));
    QVERIFY(!report.allSucceeded());
    QVERIFY(!report.residueClean);

    bool mentioned = false;
    for (const QString &line : report.residueLines) {
        if (line.contains(QStringLiteral("外来内容")))
            mentioned = true;
    }
    QVERIFY2(mentioned, "残留检查必须报出「键下有不是我们写的内容」");
}

void TstShellIntegration::uninstallReportsResidueWhenDeletionFails()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    QVERIFY(integration.install(makePlan(fullOptions())).allSucceeded());

    // 让某个值的删除失败。
    const QString compareKey = verbKey(ShellTarget::Files, ShellAction::Compare);
    store.failOnValue(compareKey, QStringLiteral("MUIVerb"),
                      Files::ErrorCode(Files::FileSystemError::PermissionDenied));

    const ShellIntegrationReport report = integration.uninstall();
    store.clearInjectedFailures();

    QVERIFY(!report.allSucceeded());
    QVERIFY(report.residueChecked);
    QVERIFY2(!report.residueClean, "删不掉就必须报残留，不能因为「我们尽力了」而说干净");
    QVERIFY(report.residueLines.join(QLatin1Char('\n')).contains(QStringLiteral("残留")));
}

void TstShellIntegration::secondInstallReusesTheOriginalBackup()
{
    MemoryRegistryStore store;
    const QString original = QStringLiteral("Original.Tool");
    store.setValue(classesKey(QStringLiteral(".patch")), QString(), RegistryValue::of(original));
    store.setValue(classesKey(QStringLiteral(".diff")), QString(), RegistryValue::of(original));

    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());
    QVERIFY(integration.install(plan).allSucceeded());

    // 两次安装之后备份仍然是两条（每个扩展名一条），而不是四条：
    // 第二次安装沿用了第一次的备份，没有把「我们的 ProgID」当成原值。
    QCOMPARE(integration.installedState().backupCount, 2);

    QVERIFY(integration.uninstall().allSucceeded());
    QVERIFY(valueIs(store, classesKey(QStringLiteral(".patch")), QString(), original));
    QVERIFY(valueIs(store, classesKey(QStringLiteral(".diff")), QString(), original));
}

// =============================================================================
// G 校验（标准第 4 条）
// =============================================================================

void TstShellIntegration::verifyPassesAfterInstall()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    const ShellIntegrationReport report = integration.verify(plan);
    QVERIFY2(report.allSucceeded(), qPrintable(report.lines().join(QLatin1Char('\n'))));
    // 计划里每一条 + 一条登记检查。
    QCOMPARE(report.records.size(), plan.entries.size() + 1);
}

void TstShellIntegration::verifyReportsMissingEntry()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    const QString compareKey = verbKey(ShellTarget::Files, ShellAction::Compare);
    QVERIFY(store.removeValue(compareKey, QStringLiteral("MUIVerb")));

    const ShellIntegrationReport report = integration.verify(plan);
    QVERIFY(!report.allSucceeded());
    QCOMPARE(report.failedCount(), 1);
    QVERIFY(report.failedKeys().contains(compareKey));
    QVERIFY(report.failureGroups().join(QLatin1Char('\n'))
                    .contains(QStringLiteral("移动或删除")));
}

void TstShellIntegration::verifyReportsChangedValue()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    // 有人改掉了命令行的路径（例如程序换了安装位置）。
    const QString commandKey =
            verbKey(ShellTarget::Files, ShellAction::Compare)
            + QStringLiteral("\\shell\\command");
    store.setValue(commandKey, QString(),
                   RegistryValue::of(QStringLiteral("\"D:\\old\\other.exe\" \"%1\"")));

    const ShellIntegrationReport report = integration.verify(plan);
    QVERIFY(!report.allSucceeded());

    bool mentionedBothValues = false;
    for (const ShellChangeRecord &record : report.records) {
        if (record.detail.contains(QStringLiteral("现在的值是"))
                && record.detail.contains(QStringLiteral("期望"))) {
            mentionedBothValues = true;
        }
    }
    // 「值不对」必须同时报出现值与期望值：只说「不一致」的话，
    // 用户既不知道该不该改，也不知道改成什么。
    QVERIFY(mentionedBothValues);
    QVERIFY(report.failureGroups().join(QLatin1Char('\n'))
                    .contains(QStringLiteral("已存在的目标")));
}

void TstShellIntegration::verifyReportsMissingBookkeeping()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    // 登记被删掉时菜单项仍然能工作，但卸载会失去「当时装了什么」的依据。
    // 校验必须把这件事报出来，否则用户会在卸载时才发现。
    store.removeKey(bookkeepingRoot());

    const ShellIntegrationReport report = integration.verify(plan);
    QVERIFY(!report.allSucceeded());
    bool mentioned = false;
    for (const ShellChangeRecord &record : report.records) {
        if (record.detail.contains(QStringLiteral("卸载将无法还原")))
            mentioned = true;
    }
    QVERIFY(mentioned);
}

void TstShellIntegration::verifyReportsInterruptedInstall()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    store.setValue(bookkeepingRoot(), QStringLiteral("Version"), RegistryValue::ofNumber(0));

    const ShellIntegrationReport report = integration.verify(plan);
    QVERIFY(!report.allSucceeded());
    QVERIFY(report.lines().join(QLatin1Char('\n')).contains(QStringLiteral("没有完成")));
}

// =============================================================================
// H 残留（标准第 4 条）
// =============================================================================

void TstShellIntegration::residueIsCleanAfterCorrectUninstall()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    const ShellIntegrationReport removal = integration.uninstall();
    QVERIFY(removal.residueChecked);
    QVERIFY2(removal.residueClean,
             qPrintable(removal.residueLines.join(QStringLiteral("\n"))));

    const ShellResidueReport residue = integration.findResidue(plan);
    QVERIFY2(residue.clean(), qPrintable(residue.lines().join(QStringLiteral("\n"))));
}

void TstShellIntegration::residueFindsLeftoverValue()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    // 不卸载，直接查——安装好的状态本身就是「到处都是我们写的东西」。
    const ShellResidueReport residue = integration.findResidue(plan);
    QVERIFY(!residue.clean());
    QVERIFY(hasFinding(residue, ShellResidueFinding::Kind::LeftoverValue));
    QVERIFY(residue.summary().contains(QStringLiteral("发现")));
}

void TstShellIntegration::residueFindsLeftoverKey()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    const ShellResidueReport residue = integration.findResidue(plan);
    QVERIFY(hasFinding(residue, ShellResidueFinding::Kind::LeftoverKey));

    // 登记子树也要被查到。
    bool bookkeepingReported = false;
    for (const ShellResidueFinding &finding : residue.findings) {
        if (finding.key == bookkeepingRoot())
            bookkeepingReported = true;
    }
    QVERIFY(bookkeepingReported);
}

void TstShellIntegration::residueFindsUnrestoredShare()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    // 备份记录还在 = 还原这一步没走完。这一项的价值是把
    // 「关联还是我们的」与「关联已经被改回去了」区分开——
    // 只看 `.patch` 的当前值分辨不出这两者。
    const ShellResidueReport residue = integration.findResidue(plan);
    QVERIFY(hasFinding(residue, ShellResidueFinding::Kind::UnrestoredShare));

    QStringList kinds;
    for (const ShellResidueFinding &finding : residue.findings)
        kinds << QString::fromLatin1(shellResidueKindIdentifier(finding.kind));
    QVERIFY(kinds.contains(QStringLiteral("unrestored_share")));
}

void TstShellIntegration::residueFindsForeignEntry()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    const ShellIntegrationPlan plan = makePlan(fullOptions());
    QVERIFY(integration.install(plan).allSucceeded());

    const QString compareKey = verbKey(ShellTarget::Files, ShellAction::Compare);
    store.setValue(compareKey, QStringLiteral("SomeoneElse"),
                   RegistryValue::of(QStringLiteral("x")));
    store.setValue(compareKey + QStringLiteral("\\NotOurs"), QString(),
                   RegistryValue::of(QStringLiteral("y")));

    const ShellResidueReport residue = integration.findResidue(plan);
    QVERIFY(hasFinding(residue, ShellResidueFinding::Kind::ForeignEntry));

    // 报出来的文案要能让人看出「这不是我们删不干净，而是别人放的」。
    const QString text = residue.lines().join(QLatin1Char('\n'));
    QVERIFY(text.contains(QStringLiteral("外来内容")));
    QVERIFY(text.contains(QStringLiteral("SomeoneElse")));
}

void TstShellIntegration::residueWithoutPlanSaysItIsApproximate()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    // 从没装过：没有登记信息，推导不出完整计划，因此只能做粗查。
    // 关键是它必须**说明**这一点，而不是报「干净」——
    // 后者会让用户以为「没有残留」，而其实什么都没查。
    const ShellResidueReport residue = integration.findResidue();
    QVERIFY(!residue.planAvailable);
    QVERIFY(residue.clean());
    QVERIFY(residue.summary().contains(QStringLiteral("粗略")));
}

// =============================================================================
// I 能力（标准第 5 条）
// =============================================================================

void TstShellIntegration::memoryBackendIsAvailableButPlatformFollowsReality()
{
    MemoryRegistryStore store;
    QString reason;
    QVERIFY2(store.isAvailable(&reason), "内存后端与平台无关，必须永远可用");
    QVERIFY(store.backendName() == QStringLiteral("memory"));

    // 而「本平台有没有注册表」是一个平台事实，不能因为换了个后端就变。
#ifdef Q_OS_WIN
    QVERIFY(platformHasRegistry());
#else
    QVERIFY2(!platformHasRegistry(), "非 Windows 平台没有注册表这一机制");
#endif
}

void TstShellIntegration::nativeBackendAvailabilityMatchesThePlatform()
{
    QScopedPointer<RegistryStore> native(createNativeRegistryStore());
    // 契约：永不返回空指针。返回空会让每个调用点都要判空，漏判的那处就是崩溃。
    QVERIFY(!native.isNull());

    QString reason;
    const bool available = native->isAvailable(&reason);

#ifdef Q_OS_WIN
    QVERIFY(available);
    QVERIFY(native->backendName() == QStringLiteral("win32-registry"));
#else
    QVERIFY(!available);
    QVERIFY(native->backendName() == QStringLiteral("unsupported"));
    QVERIFY2(!reason.isEmpty(), "不可用时必须给出原因，不能只说「不支持」");
#endif
}

void TstShellIntegration::unavailableCapabilityAlwaysExplainsWhy()
{
    QScopedPointer<RegistryStore> native(createNativeRegistryStore());
    ShellIntegration integration(native.data());
    const ShellIntegrationCapability capability = integration.capability();

#ifndef Q_OS_WIN
    QVERIFY(!capability.available);
    QVERIFY(!capability.platformSupported);
    // 「置灰**并说明**」——说明是这句要求里最容易做丢的一半。
    // 原因告诉用户为什么，建议告诉他那该怎么办；两者缺一都只是半个功能。
    QVERIFY2(!capability.reason.isEmpty(), "必须说明为什么不可用");
    QVERIFY2(!capability.advice.isEmpty(), "必须给出可执行的替代做法");
    QVERIFY(capability.advice.contains(QStringLiteral("拖")));
#else
    QVERIFY(capability.available);
#endif
}

void TstShellIntegration::capabilitySummaryNeverEmpty()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);
    QVERIFY(!integration.capability().summary().isEmpty());
    QVERIFY(integration.capability().summary().contains(store.backendName()));
    QVERIFY(!integration.installedState().summary().isEmpty());
    QVERIFY(integration.installedState().summary().contains(QStringLiteral("未安装")));
}

// =============================================================================
// J 预演
// =============================================================================

void TstShellIntegration::previewRefusesWithoutAScratchStore()
{
    MemoryRegistryStore store;
    ShellIntegration integration(&store);

    const ShellIntegrationReport report =
            integration.previewInstall(makePlan(fullOptions()), nullptr);
    QVERIFY(!report.allSucceeded());
    QCOMPARE(report.records.size(), 1);
}

void TstShellIntegration::previewDetectsOverwriteOfExistingAssociation()
{
    MemoryRegistryStore real;
    const QString other = QStringLiteral("Other.Program.Diff");
    real.setValue(classesKey(QStringLiteral(".diff")), QString(), RegistryValue::of(other));

    ShellIntegration integration(&real);
    MemoryRegistryStore scratch;
    const ShellIntegrationReport report =
            integration.previewInstall(makePlan(fullOptions()), &scratch);

    QVERIFY2(report.allSucceeded(), qPrintable(report.lines().join(QLatin1Char('\n'))));

    // 预演的全部意义就在这里：告诉用户「你会覆盖掉别人的关联」。
    // 只在空存储上预演会给出「没有冲突」，而用户的机器上其实有。
    QVERIFY2(mentionOf(report, other),
             "预演必须报出会被覆盖的原有值，否则它只是假装在预演");

    // scratch 上真的装起来了，真实存储一点没动。
    QVERIFY(valueIs(scratch, classesKey(QStringLiteral(".diff")), QString(),
                    QStringLiteral("LqCompare.DiffFile")));
    QVERIFY(valueIs(real, classesKey(QStringLiteral(".diff")), QString(), other));
}

// =============================================================================
// K 命令行解析
// =============================================================================

void TstShellIntegration::parseWithoutSwitchIsNotShellInvocation()
{
    // 用户直接在命令行里跑本程序时不该走 Shell 集成的分支。
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"), QStringLiteral("a.txt")});
    QVERIFY(invocation.kind == ShellInvocationKind::NotShellInvocation);
    QVERIFY(!invocation.valid());
    QVERIFY(invocation.describe().contains(QStringLiteral("不是 Shell 集成")));
}

void TstShellIntegration::parseCompareWithTwoPathsIsValid()
{
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=compare"),
                        QStringLiteral("C:\\a.txt"), QStringLiteral("C:\\b.txt")});
    QVERIFY(invocation.valid());
    QVERIFY(invocation.action == ShellAction::Compare);
    QCOMPARE(invocation.paths.size(), 2);
    QVERIFY(invocation.describe().contains(QStringLiteral("compare")));
}

void TstShellIntegration::parseCompareWithOnePathIsInvalid()
{
    // 只选中一个文件就点「比较」时，必须**停下**并说明。
    // 若拿同一个文件和自己比较，会得出「完全相同」——一个错误但看起来合理的结论。
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=compare"),
                        QStringLiteral("C:\\a.txt")});
    QVERIFY(invocation.kind == ShellInvocationKind::Invalid);
    QVERIFY(invocation.problem.contains(QStringLiteral("需要选中")));
    QVERIFY(invocation.problem.contains(QStringLiteral("2")));
}

void TstShellIntegration::parseCompareWithSamePathTwiceIsInvalid()
{
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=compare"),
                        QStringLiteral("C:\\a.txt"), QStringLiteral("C:\\a.txt")});
    QVERIFY(invocation.kind == ShellInvocationKind::Invalid);
    QVERIFY(invocation.problem.contains(QStringLiteral("同一个")));
}

void TstShellIntegration::parseUnknownActionIsInvalid()
{
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=explode"),
                        QStringLiteral("C:\\a.txt")});
    QVERIFY(invocation.kind == ShellInvocationKind::Invalid);
    QVERIFY(invocation.problem.contains(QStringLiteral("不认识的动作")));
}

void TstShellIntegration::parseDoubleSwitchIsInvalid()
{
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=compare"),
                        QStringLiteral("--shell-action=compare_as_left"),
                        QStringLiteral("C:\\a.txt")});
    QVERIFY(invocation.kind == ShellInvocationKind::Invalid);
    QVERIFY(invocation.problem.contains(QStringLiteral("多个动作开关")));
}

void TstShellIntegration::parseBareFormIsAccepted()
{
    // 手工在命令行里试的时候 `--shell-action compare` 比 `=` 形式好敲。
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action"), QStringLiteral("compare_as_right"),
                        QStringLiteral("C:\\a.txt")});
    QVERIFY(invocation.valid());
    QVERIFY(invocation.action == ShellAction::CompareAsRight);

    // 开关后面什么都不跟时要明确报出来，不能静默当成「没给动作」。
    const ShellInvocation truncated = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"), QStringLiteral("--shell-action")});
    QVERIFY(truncated.kind == ShellInvocationKind::Invalid);
}

void TstShellIntegration::parseTooManyPathsForSingleActionKeepsFirst()
{
    // 资源管理器可能把多选的条目都塞进 `%1`。对只接受一个的动作，
    // 取第一个并告知，比整条拒绝更友好——用户可能只是鼠标滑过多选了一个。
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=compare_as_left"),
                        QStringLiteral("C:\\a.txt"), QStringLiteral("C:\\b.txt")});
    QVERIFY(invocation.valid());
    QCOMPARE(invocation.paths.size(), 1);
    QCOMPARE(invocation.paths.first(), QStringLiteral("C:\\a.txt"));
    QVERIFY(!invocation.problem.isEmpty());
}

void TstShellIntegration::parseOpenActionNeedsExactlyOnePath()
{
    const ShellInvocation invocation = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=open"),
                        QStringLiteral("C:\\p.patch")});
    QVERIFY(invocation.valid());
    QVERIFY(invocation.action == ShellAction::OpenAssociation);
    QCOMPARE(invocation.paths.size(), 1);

    const ShellInvocation missing = parseShellInvocation(
            QStringList{QStringLiteral("LqCompare.exe"),
                        QStringLiteral("--shell-action=open")});
    QVERIFY(missing.kind == ShellInvocationKind::Invalid);
}

// Q_OBJECT 声明在头文件里，因此这里**不要**写 #include "tst_shellintegration.moc"：
// qmake 会对 HEADERS 里的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译，
// 再加一行 .moc include 会报 "No rule to make target"（本仓库坑表里有这条）。
QTEST_MAIN(TstShellIntegration)
