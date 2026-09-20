#include "tst_filterstack.h"

#include "filterstack.h"
#include "mask.h"
#include "maskfilter.h"

// Services/Session —— 第 4 条要拿真正的存储来断言（见 .pro 里的说明）。
#include "session.h"
#include "settingscope.h"

#include <QStringList>

namespace {

using LqCompare::Filter::FilterLayer;
using LqCompare::Filter::FilterLayerBinder;
using LqCompare::Filter::FilterLayerState;
using LqCompare::Filter::FilterLayerStorage;
using LqCompare::Filter::FilterStack;
using LqCompare::Filter::FilterStackPreview;
using LqCompare::Filter::LayeredFilterDecision;
using LqCompare::Filter::MaskPlatform;
using LqCompare::Filter::MaskRuleKind;
using LqCompare::Filter::MaskSubject;
using LqCompare::Filter::MaskVerdict;

using LqCompare::MemorySessionSettings;
using LqCompare::ScopedSessionSettings;
using LqCompare::SettingScope;

///
/// \brief 造一个装好三层声明的栈。
///
/// 不在辅助函数里写 QVERIFY：那个宏在失败时会 `return`，写在返回非 void 的函数里
/// 要么编译不过、要么悄悄返回一个默认值让失败变成别处的假错误。断言留在调用点。
///
FilterStack stackWith(const QString &format,
                      const QString &session,
                      const QString &view)
{
    FilterStack stack;
    stack.setDeclaration(FilterLayer::Format, format);
    stack.setDeclaration(FilterLayer::Session, session);
    stack.setDeclaration(FilterLayer::View, view);
    return stack;
}

bool acceptsName(const FilterStack &stack, const QString &name)
{
    return stack.accepts(MaskSubject::forName(name));
}

LayeredFilterDecision decideName(const FilterStack &stack, const QString &name)
{
    return stack.decide(MaskSubject::forName(name));
}

QStringList threeNames(const QString &one, const QString &two, const QString &three)
{
    QStringList names;
    names << one << two << three;
    return names;
}

/// 面板里某一行的下标（按规格顺序）。
int rowIndex(FilterLayer layer)
{
    return LqCompare::Filter::filterLayerIndex(layer);
}

} // namespace

namespace QTest {

template <>
char *toString(const LqCompare::Filter::FilterLayer &layer)
{
    return qstrdup(LqCompare::Filter::filterLayerIdentifier(layer));
}

template <>
char *toString(const LqCompare::Filter::FilterLayerStorage &storage)
{
    return qstrdup(LqCompare::Filter::filterLayerStorageIdentifier(storage));
}

template <>
char *toString(const LqCompare::Filter::MaskVerdict &verdict)
{
    return qstrdup(LqCompare::Filter::maskVerdictIdentifier(verdict));
}

template <>
char *toString(const LqCompare::Filter::MaskRuleKind &kind)
{
    return qstrdup(LqCompare::Filter::maskRuleKindIdentifier(kind));
}

} // namespace QTest

void TstFilterStack::initTestCase()
{
    // 启动自检：层表必须是完整的三个。少一个会让下面所有用例「恰好」通过或失败，
    // 而原因看起来会落在被测行为上。
    QCOMPARE(LqCompare::Filter::allFilterLayers().size(), 3);
}

// =============================================================================
// A 层级与落点（第 1、4 条的结构面）
// =============================================================================

void TstFilterStack::layerOrderMatchesTheSpecification()
{
    // 规格给的顺序就是叠加顺序：文件格式定义内建过滤 → 会话设置过滤 → 视图临时过滤。
    const QVector<FilterLayer> layers = LqCompare::Filter::allFilterLayers();
    QCOMPARE(layers.size(), 3);
    QCOMPARE(layers.at(0), FilterLayer::Format);
    QCOMPARE(layers.at(1), FilterLayer::Session);
    QCOMPARE(layers.at(2), FilterLayer::View);
}

void TstFilterStack::layerIdentifiersAreStableAndDistinct()
{
    // 机器标识会进日志与设置键，改了就等于让旧日志与旧配置对不上。
    QCOMPARE(QString::fromLatin1(LqCompare::Filter::filterLayerIdentifier(FilterLayer::Format)),
             QStringLiteral("format"));
    QCOMPARE(QString::fromLatin1(LqCompare::Filter::filterLayerIdentifier(FilterLayer::Session)),
             QStringLiteral("session"));
    QCOMPARE(QString::fromLatin1(LqCompare::Filter::filterLayerIdentifier(FilterLayer::View)),
             QStringLiteral("view"));

    // 三层的中文标签必须互不相同：面板上是靠它区分行的。
    const QString format = LqCompare::Filter::filterLayerLabel(FilterLayer::Format);
    const QString session = LqCompare::Filter::filterLayerLabel(FilterLayer::Session);
    const QString view = LqCompare::Filter::filterLayerLabel(FilterLayer::View);
    QVERIFY(!format.isEmpty());
    QVERIFY(format != session);
    QVERIFY(session != view);
    QVERIFY(format != view);

    // 每一层都要有一句话说明（下拉项的 tooltip 靠它）。
    QVERIFY(!LqCompare::Filter::filterLayerDescription(FilterLayer::Format).isEmpty());
    QVERIFY(!LqCompare::Filter::filterLayerDescription(FilterLayer::Session).isEmpty());
    QVERIFY(!LqCompare::Filter::filterLayerDescription(FilterLayer::View).isEmpty());
}

void TstFilterStack::layerIndexesMatchTheSpecOrder()
{
    // 下标是 FilterStack 内部格子的索引，与 allFilterLayers() 必须同序。
    // 两者错位的表现是「设置的是格式层、生效的是视图层」，而面板上看起来一切正常。
    const QVector<FilterLayer> layers = LqCompare::Filter::allFilterLayers();
    for (int index = 0; index < layers.size(); ++index)
        QCOMPARE(LqCompare::Filter::filterLayerIndex(layers.at(index)), index);
}

void TstFilterStack::storageRoutesEachLayerToItsOwnPlace()
{
    QCOMPARE(LqCompare::Filter::filterLayerStorage(FilterLayer::Format),
             FilterLayerStorage::Builtin);
    QCOMPARE(LqCompare::Filter::filterLayerStorage(FilterLayer::Session),
             FilterLayerStorage::SessionFile);
    // 第 4 条的落点声明。改成 SessionFile 会让下面 E 组的用例全部变红。
    QCOMPARE(LqCompare::Filter::filterLayerStorage(FilterLayer::View),
             FilterLayerStorage::ViewMemory);

    // 三个落点的中文标签也必须互不相同（面板上要显示）。
    const QString builtin = LqCompare::Filter::filterLayerStorageLabel(FilterLayerStorage::Builtin);
    const QString session = LqCompare::Filter::filterLayerStorageLabel(FilterLayerStorage::SessionFile);
    const QString view = LqCompare::Filter::filterLayerStorageLabel(FilterLayerStorage::ViewMemory);
    QVERIFY(!builtin.isEmpty());
    QVERIFY(builtin != session);
    QVERIFY(session != view);
    QVERIFY(builtin != view);
}

void TstFilterStack::onlyTheViewLayerIsDiscardedOnClose()
{
    QVERIFY(!LqCompare::Filter::filterLayerIsDiscardedOnClose(FilterLayer::Format));
    QVERIFY(!LqCompare::Filter::filterLayerIsDiscardedOnClose(FilterLayer::Session));
    QVERIFY(LqCompare::Filter::filterLayerIsDiscardedOnClose(FilterLayer::View));
}

void TstFilterStack::formatLayerIsNotEditableOthersAre()
{
    // 格式层的声明来自文件格式定义，用户改不了——只能整层启用/禁用。
    QVERIFY(!LqCompare::Filter::filterLayerIsEditable(FilterLayer::Format));
    QVERIFY(LqCompare::Filter::filterLayerIsEditable(FilterLayer::Session));
    QVERIFY(LqCompare::Filter::filterLayerIsEditable(FilterLayer::View));
}

void TstFilterStack::allThreeLayersShareOneDeclarationKey()
{
    // 三层共用一个键名，因为它们住在三个不同的存储里。给每层各发明一个键名会让
    // 「键名 ↔ 层」多出一份要维护的对应关系，而漏掉一处的表现是「旧会话文件里
    // 那一层的过滤读不回来」。
    const QString key = LqCompare::Filter::filterDeclarationSettingKey();
    QVERIFY(!key.isEmpty());

    // 键名必须能通过会话设置的键规则（它会被真正写进会话设置）。
    QVERIFY(LqCompare::isValidSettingKey(key));
}

void TstFilterStack::freshStackHasNoActiveLayer()
{
    // 三层默认都是**启用**的，但都还没有规则，因此没有任何一层在生效。
    // 这条正是第 2 条「显示该层是否当前生效」的关键：只按 enabled 判的话，
    // 「某一层生效中」会恒为真，用户无从判断到底是谁在过滤。
    const FilterStack stack;
    QVERIFY(stack.isLayerEnabled(FilterLayer::Format));
    QVERIFY(stack.isLayerEnabled(FilterLayer::Session));
    QVERIFY(stack.isLayerEnabled(FilterLayer::View));
    QVERIFY(stack.activeLayers().isEmpty());
    QVERIFY(!stack.hasActiveRule());
    QVERIFY(stack.combinedExpression().isEmpty());
    QVERIFY(stack.accepts(MaskSubject::forName(QStringLiteral("whatever.txt"))));
}

// =============================================================================
// B 三层叠加（第 1、5 条）
// =============================================================================

void TstFilterStack::everyActiveLayerMustAdmitTheSubject()
{
    // 第 1 条的核心：三层是**交集**，一个条目必须被每一条生效的层放行。
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QStringLiteral("main*"));

    QCOMPARE(decideName(stack, QStringLiteral("main.cpp")).verdict, MaskVerdict::Included);
    QCOMPARE(decideName(stack, QStringLiteral("other.cpp")).verdict, MaskVerdict::NotMatched);
    QCOMPARE(decideName(stack, QStringLiteral("main.h")).verdict, MaskVerdict::NotMatched);
    QCOMPARE(decideName(stack, QStringLiteral("other.h")).verdict, MaskVerdict::NotMatched);
}

void TstFilterStack::addingALayerOnlyNarrowsTheResult()
{
    // 这是「交集」而不是「并集」的**行为**证据。并集下，加了视图层之后
    //「原本被会话层挡住的条目」会重新出现——用户加一个「只看这个」的过滤，
    // 反而看到了更多，而他只会认为过滤器坏了。
    const QStringList names = threeNames(QStringLiteral("main.cpp"),
                                         QStringLiteral("other.cpp"),
                                         QStringLiteral("notes.txt"));

    FilterStack stack;
    stack.setDeclaration(FilterLayer::Format, QStringLiteral("*"));
    const int afterFormat = stack.previewNames(names).included;

    stack.setDeclaration(FilterLayer::Session, QStringLiteral("*.cpp"));
    const int afterSession = stack.previewNames(names).included;

    stack.setDeclaration(FilterLayer::View, QStringLiteral("main*"));
    const int afterView = stack.previewNames(names).included;

    QCOMPARE(afterFormat, 3);
    QCOMPARE(afterSession, 2);
    QCOMPARE(afterView, 1);
    QVERIFY(afterSession <= afterFormat);
    QVERIFY(afterView <= afterSession);
}

void TstFilterStack::sessionWhitelistNarrowsTheFormatWhitelist()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp\n*.h"),
                                        QStringLiteral("*.h"),
                                        QString());
    QVERIFY(acceptsName(stack, QStringLiteral("a.h")));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.cpp")));
    QCOMPARE(decideName(stack, QStringLiteral("a.cpp")).decidingLayer, FilterLayer::Session);
}

void TstFilterStack::viewWhitelistNarrowsFurther()
{
    const FilterStack stack = stackWith(QStringLiteral("*.txt"),
                                        QString(),
                                        QStringLiteral("notes*"));
    QVERIFY(acceptsName(stack, QStringLiteral("notes.txt")));
    QVERIFY(!acceptsName(stack, QStringLiteral("other.txt")));
    QCOMPARE(decideName(stack, QStringLiteral("other.txt")).decidingLayer, FilterLayer::View);
}

void TstFilterStack::anyLayerExclusionWinsOverAllOtherAdmissions()
{
    // 层间的「排除优先」：用户说「不要这个」时，不管它被另外几层放行了多少次，
    // 结论都是不要。反过来会让排除规则在多层场景下完全失效。
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("-main.cpp"),
                                        QStringLiteral("*"));
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("main.cpp"));
    QCOMPARE(decision.verdict, MaskVerdict::Excluded);
    QCOMPARE(decision.decidingLayer, FilterLayer::Session);
    QCOMPARE(decision.ruleKind, MaskRuleKind::Exclude);
    QCOMPARE(decision.ruleText, QStringLiteral("main.cpp"));
}

void TstFilterStack::exclusionInTheOutermostLayerAlsoWins()
{
    // 反过来：格式层的排除同样能压过内层的放行。
    const FilterStack stack = stackWith(QStringLiteral("*\n-*.tmp"),
                                        QStringLiteral("*"),
                                        QStringLiteral("*"));
    QCOMPARE(decideName(stack, QStringLiteral("a.tmp")).verdict, MaskVerdict::Excluded);
    QCOMPARE(decideName(stack, QStringLiteral("a.tmp")).decidingLayer, FilterLayer::Format);
    QVERIFY(acceptsName(stack, QStringLiteral("a.txt")));
}

void TstFilterStack::exclusionOutranksNotMatchedAcrossLayers()
{
    // 同一条目既被某层排除、又被另一层的白名单漏掉时，报「被排除」。
    // 两者给用户的解释完全不同：「你明确要求不要它」vs「你的白名单里没有它」，
    // 后者会引导用户去改一条本来就没错的规则。
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("-*.h"),
                                        QString());
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.h"));
    QCOMPARE(decision.verdict, MaskVerdict::Excluded);
    QCOMPARE(decision.decidingLayer, FilterLayer::Session);
}

void TstFilterStack::noWhitelistAnywhereKeepsEverythingElse()
{
    // 全是排除规则时，没被排除的都保留——与 FILT-001 层内的同一条规矩。
    const FilterStack stack = stackWith(QStringLiteral("-*.tmp"),
                                        QStringLiteral("-*.log"),
                                        QString());
    QVERIFY(acceptsName(stack, QStringLiteral("a.txt")));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.tmp")));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.log")));
}

void TstFilterStack::oneWhitelistMakesEveryLayerFollowIt()
{
    // 白名单为空的层对交集没有贡献（它的「包含」恒为真），
    // 但只要**有一层**写了白名单，语义就变成「必须命中它」——而不是「命中任一层即可」。
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QString());
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.h")));
    QCOMPARE(decideName(stack, QStringLiteral("a.h")).decidingLayer, FilterLayer::Session);
}

void TstFilterStack::decisionNamesTheFirstExcludingLayer()
{
    const FilterStack stack = stackWith(QStringLiteral("*\n-*.tmp"),
                                        QStringLiteral("-*.tmp"),
                                        QString());
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.tmp"));
    QCOMPARE(decision.verdict, MaskVerdict::Excluded);
    QCOMPARE(decision.decidingLayer, FilterLayer::Format);
    QCOMPARE(decision.ruleIndex, 1);
}

void TstFilterStack::decisionNamesTheFirstRejectingWhitelist()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("*.cpp"),
                                        QStringLiteral("*.cpp"));
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.h"));
    QCOMPARE(decision.verdict, MaskVerdict::NotMatched);
    QCOMPARE(decision.decidingLayer, FilterLayer::Format);
    // 未命中时没有「起决定作用的规则」，规则下标必须是 -1——
    // 编一个 0 出来会让界面去高亮一条根本没参与匹配的规则。
    QCOMPARE(decision.ruleIndex, -1);
}

void TstFilterStack::decisionNamesTheLastWhitelistForIncluded()
{
    // 「保留」的结论取**最后一个有白名单**的生效层——它是最后一道闸门。
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*"),
                                        QStringLiteral("*"));
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.txt"));
    QCOMPARE(decision.verdict, MaskVerdict::Included);
    QCOMPARE(decision.decidingLayer, FilterLayer::View);

    // 视图层只有排除规则（没有白名单）时，答案退到会话层。
    const FilterStack excludingView = stackWith(QStringLiteral("*"),
                                                QStringLiteral("*"),
                                                QStringLiteral("-*.tmp"));
    QCOMPARE(decideName(excludingView, QStringLiteral("a.txt")).decidingLayer,
             FilterLayer::Session);
}

void TstFilterStack::decisionNamesTheLastActiveLayerWhenNoWhitelistExists()
{
    // 一层白名单都没有时，「全部保留」是各层共同给出的结论，取最后一个生效层。
    const FilterStack stack = stackWith(QStringLiteral("-*.log"),
                                        QStringLiteral("-*.tmp"),
                                        QString());
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.txt"));
    QCOMPARE(decision.verdict, MaskVerdict::Included);
    QCOMPARE(decision.decidingLayer, FilterLayer::Session);
}

void TstFilterStack::allExcludingLayersAreReported()
{
    // decidingLayer 只报第一个，而用户改掉第一个之后往往发现还是看不见。
    const FilterStack stack = stackWith(QStringLiteral("-*.tmp"),
                                        QStringLiteral("-*.tmp"),
                                        QStringLiteral("-a*"));
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.tmp"));
    QCOMPARE(decision.decidingLayer, FilterLayer::Format);
    QCOMPARE(decision.excludingLayers.size(), 3);
    QCOMPARE(decision.excludingLayers.at(0), FilterLayer::Format);
    QCOMPARE(decision.excludingLayers.at(1), FilterLayer::Session);
    QCOMPARE(decision.excludingLayers.at(2), FilterLayer::View);

    // 只有一个层排除它时，清单里就只有那一层。
    const QVector<FilterLayer> onlySession =
        stackWith(QStringLiteral("*"), QStringLiteral("-*.tmp"), QString())
            .excludingLayers(MaskSubject::forName(QStringLiteral("a.tmp")));
    QCOMPARE(onlySession.size(), 1);
    QCOMPARE(onlySession.at(0), FilterLayer::Session);
}

void TstFilterStack::disabledLayerDoesNotAffectTheVerdict()
{
    // 第 2 条的行为面：禁用一层之后，它的规则必须完全不参与。
    FilterStack stack = stackWith(QStringLiteral("*"),
                                  QStringLiteral("*.cpp"),
                                  QString());
    QVERIFY(!acceptsName(stack, QStringLiteral("a.h")));

    stack.setLayerEnabled(FilterLayer::Session, false);
    QVERIFY(acceptsName(stack, QStringLiteral("a.h")));
    QCOMPARE(stack.activeLayers().size(), 1);
    QCOMPARE(stack.activeLayers().at(0), FilterLayer::Format);

    // 禁用的层即使有排除规则也不再排除。
    FilterStack excluding = stackWith(QStringLiteral("*"),
                                      QStringLiteral("-*.tmp"),
                                      QString());
    QVERIFY(!acceptsName(excluding, QStringLiteral("a.tmp")));
    excluding.setLayerEnabled(FilterLayer::Session, false);
    QVERIFY(acceptsName(excluding, QStringLiteral("a.tmp")));
}

void TstFilterStack::enabledButEmptyLayerDoesNotAffectTheVerdict()
{
    FilterStack stack = stackWith(QStringLiteral("*"),
                                  QStringLiteral(""),
                                  QStringLiteral(""));
    QVERIFY(stack.isLayerEnabled(FilterLayer::Session));
    QVERIFY(!stack.isLayerActive(FilterLayer::Session));
    QVERIFY(acceptsName(stack, QStringLiteral("anything.txt")));
}

void TstFilterStack::emptyStackAcceptsEverything()
{
    const FilterStack stack;
    const LayeredFilterDecision decision = decideName(stack, QStringLiteral("a.txt"));
    QCOMPARE(decision.verdict, MaskVerdict::Included);
    QVERIFY(!decision.anyLayerActive);
    QVERIFY(decision.excludingLayers.isEmpty());
    QVERIFY(decision.describe().contains(QStringLiteral("没有任何过滤层生效")));

    // describe() 在「没有层生效」与「层放行了它」之间必须给出不同的话，
    // 否则诊断面板会把「什么都没配置」说成「三层都放行了」。
    const FilterStack active = stackWith(QStringLiteral("*"), QString(), QString());
    QVERIFY(decideName(active, QStringLiteral("a.txt")).anyLayerActive);
    QVERIFY(decideName(active, QStringLiteral("a.txt")).describe()
            != decision.describe());
}

void TstFilterStack::decisionReportsThatNoLayerIsActive()
{
    const FilterStack stack = stackWith(QStringLiteral(""),
                                        QStringLiteral("*.cpp"),
                                        QString());
    FilterStack disabled = stack;
    disabled.setLayerEnabled(FilterLayer::Session, false);

    const LayeredFilterDecision decision = decideName(disabled, QStringLiteral("a.h"));
    QVERIFY(!decision.anyLayerActive);
    QCOMPARE(decision.verdict, MaskVerdict::Included);
}

void TstFilterStack::oneBadLineOnlyDropsThatLine()
{
    // 与 FILT-001 一致：一行写错只丢那一行，其余照常生效。分层之后这条规矩
    // 必须**逐层独立**——一层的语法错误不该把另外两层也停掉。
    FilterStack stack = stackWith(QStringLiteral("*.cpp\n[abc"),
                                  QStringLiteral("*.cpp"),
                                  QString());
    QCOMPARE(stack.layerErrors(FilterLayer::Format).size(), 1);
    QVERIFY(stack.hasErrors());
    QVERIFY(stack.isLayerActive(FilterLayer::Format));
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.h")));

    // 另外两层没有错误。
    QVERIFY(stack.layerErrors(FilterLayer::Session).isEmpty());
    QVERIFY(stack.layerErrors(FilterLayer::View).isEmpty());
}

void TstFilterStack::errorsAreReportedWithTheirLayerName()
{
    // 三层的行号各自从 0 起，不带层名的话用户不知道该去哪一框改。
    const FilterStack stack = stackWith(QStringLiteral("*.cpp\n[abc"),
                                        QStringLiteral("[def"),
                                        QString());
    const QString text = stack.describeErrors();
    QVERIFY(text.contains(LqCompare::Filter::filterLayerLabel(FilterLayer::Format)));
    QVERIFY(text.contains(LqCompare::Filter::filterLayerLabel(FilterLayer::Session)));
    QVERIFY(!text.contains(LqCompare::Filter::filterLayerLabel(FilterLayer::View)));
}

// =============================================================================
// C 启用与生效（第 2 条）
// =============================================================================

void TstFilterStack::newStackHasEveryLayerEnabled()
{
    const FilterStack stack;
    for (FilterLayer layer : LqCompare::Filter::allFilterLayers())
        QVERIFY(stack.isLayerEnabled(layer));
}

void TstFilterStack::enabledButEmptyLayerIsNotActive()
{
    FilterStack stack;
    QVERIFY(stack.isLayerEnabled(FilterLayer::View));
    QVERIFY(!stack.isLayerActive(FilterLayer::View));

    stack.setDeclaration(FilterLayer::View, QStringLiteral("   \n# 只有注释\n"));
    QVERIFY(stack.isLayerEnabled(FilterLayer::View));
    QVERIFY(!stack.isLayerActive(FilterLayer::View));
}

void TstFilterStack::disablingAnActiveLayerMakesItInactive()
{
    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.cpp"));
    QVERIFY(stack.isLayerActive(FilterLayer::View));

    stack.setLayerEnabled(FilterLayer::View, false);
    QVERIFY(!stack.isLayerActive(FilterLayer::View));
    // 规则本身还在（重新勾上就恢复），只是不参与。
    QCOMPARE(stack.filter(FilterLayer::View).ruleCount(), 1);
}

void TstFilterStack::statusTextDistinguishesDisabledFromEmpty()
{
    // 第 2 条要求「显示该层是否当前生效」。三种状态必须说得出区别，
    // 否则面板上会出现两个看起来一样的「未生效」，用户不知道该做什么。
    FilterStack stack;
    const QString emptyText = stack.layerStatusText(FilterLayer::View);

    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.cpp"));
    const QString activeText = stack.layerStatusText(FilterLayer::View);

    stack.setLayerEnabled(FilterLayer::View, false);
    const QString disabledText = stack.layerStatusText(FilterLayer::View);

    QVERIFY(!emptyText.isEmpty());
    QVERIFY(emptyText != activeText);
    QVERIFY(activeText != disabledText);
    QVERIFY(emptyText != disabledText);
    QVERIFY(activeText.contains(QStringLiteral("生效中")));
    QVERIFY(disabledText.contains(QStringLiteral("已禁用")));
}

void TstFilterStack::statusTextReportsRuleCounts()
{
    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.cpp\n*.h\n-*_test.cpp"));
    const QString text = stack.layerStatusText(FilterLayer::View);
    QVERIFY(text.contains(QStringLiteral("2")));  // 包含 2 条
    QVERIFY(text.contains(QStringLiteral("1")));  // 排除 1 条
}

void TstFilterStack::statusTextMentionsErrorsWhileOtherRulesStayActive()
{
    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.cpp\n[abc"));
    QVERIFY(stack.isLayerActive(FilterLayer::View));
    const QString text = stack.layerStatusText(FilterLayer::View);
    QVERIFY(text.contains(QStringLiteral("语法错误")));
    // 必须说清「其余规则照常生效」，否则用户会以为整层都失效了。
    QVERIFY(text.contains(QStringLiteral("照常生效")));
}

void TstFilterStack::activeLayersListsOnlyTheActiveOnes()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("# 注释而已"),
                                        QStringLiteral("-*.tmp"));
    const QVector<FilterLayer> active = stack.activeLayers();
    QCOMPARE(active.size(), 2);
    QCOMPARE(active.at(0), FilterLayer::Format);
    QCOMPARE(active.at(1), FilterLayer::View);
}

void TstFilterStack::setLayerStateReparsesFromTheDeclaration()
{
    // 允许传一份不一致的解析结果，就等于给「界面上写的是新规则、过滤用的是旧的」
    // 开了一道门。所以 setLayerState 一律按声明文本重新解析。
    FilterLayerState state;
    state.layer = FilterLayer::View;
    state.declaration = QStringLiteral("*.cpp");
    state.enabled = false;
    // 故意塞一份与声明不匹配的解析结果。
    state.filter = LqCompare::Filter::MaskFilter::parse(QStringLiteral("-*.tmp")).filter;

    FilterStack stack;
    stack.setLayerState(FilterLayer::View, state);

    QCOMPARE(stack.filter(FilterLayer::View).includeCount(), 1);
    QCOMPARE(stack.filter(FilterLayer::View).excludeCount(), 0);
    QVERIFY(!stack.isLayerEnabled(FilterLayer::View));
}

void TstFilterStack::setLayerStateForcesTheLayerField()
{
    // 调用点复制一份状态过来时很容易忘了改 layer 字段，而错了之后面板上会出现
    // 两层同名的行，很难归因。所以 layer 由参数说了算。
    FilterLayerState state;
    state.layer = FilterLayer::Format;
    state.declaration = QStringLiteral("*.cpp");

    FilterStack stack;
    stack.setLayerState(FilterLayer::View, state);
    QCOMPARE(stack.layerState(FilterLayer::View).layer, FilterLayer::View);
    QCOMPARE(stack.layerState(FilterLayer::Format).layer, FilterLayer::Format);
    QVERIFY(stack.declaration(FilterLayer::Format).isEmpty());
}

void TstFilterStack::caseSensitivityOverrideAppliesToEveryLayer()
{
    FilterStack stack = stackWith(QStringLiteral("*.CPP"),
                                  QStringLiteral("*.CPP"),
                                  QStringLiteral("*.CPP"));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.cpp")));

    stack.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(stack.isCaseSensitivityOverridden());
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
    // 三层都要被覆盖到——只改一层的话，失败现象是「有的层认大小写、有的不认」。
    for (FilterLayer layer : LqCompare::Filter::allFilterLayers())
        QCOMPARE(stack.filter(layer).caseSensitivity(), Qt::CaseInsensitive);
}

void TstFilterStack::caseOverrideSurvivesLaterDeclarations()
{
    // 覆盖是在**解析时**应用的，而 `FilterStack::setDeclaration` 会重新解析。
    // 忘了重新应用的话，用户在对话框里勾了「忽略大小写」，再改一次掩码就失效了——
    // 而那个勾还打着勾。
    FilterStack stack;
    stack.setCaseSensitivity(Qt::CaseInsensitive);
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.CPP"));
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));

    // 换平台重建解析也要保住覆盖。
    stack.setPlatform(MaskPlatform::Windows);
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
}

void TstFilterStack::switchingPlatformReparsesEveryLayer()
{
    // 平台只影响**默认**大小写敏感性，而它是在解析时固化进 MaskFilter 的。
    // 只记下来不重解析的话，「切到 Windows 语义」看起来生效了、实际一条都没变。
    FilterStack stack = stackWith(QStringLiteral("*.CPP"), QString(), QString());
    QCOMPARE(stack.platform(), MaskPlatform::Posix);
    QVERIFY(!acceptsName(stack, QStringLiteral("a.cpp")));

    stack.setPlatform(MaskPlatform::Windows);
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));

    stack.setPlatform(MaskPlatform::Posix);
    QVERIFY(!acceptsName(stack, QStringLiteral("a.cpp")));
}

void TstFilterStack::clearingCaseOverrideReturnsToPlatformDefault()
{
    FilterStack stack = stackWith(QStringLiteral("*.CPP"), QString(), QString());
    stack.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));

    stack.clearCaseSensitivityOverride();
    QVERIFY(!stack.isCaseSensitivityOverridden());
    QVERIFY(!acceptsName(stack, QStringLiteral("a.cpp")));

    // 平台上本来就默认不敏感时，清掉覆盖之后应当跟着平台走——而不是一律变敏感。
    stack.setPlatform(MaskPlatform::Windows);
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
}

void TstFilterStack::settingTheSamePlatformIsANoOp()
{
    // 重解析会把用户手改过的东西抹掉吗？不会——声明是唯一事实来源，
    // 但这条仍然值得钉住：同平台重设不该改动任何一层的规则。
    FilterStack stack = stackWith(QStringLiteral("*.cpp"), QStringLiteral("-*.tmp"), QString());
    const int before = stack.filter(FilterLayer::Format).ruleCount();
    stack.setPlatform(stack.platform());
    QCOMPARE(stack.filter(FilterLayer::Format).ruleCount(), before);
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
}

// =============================================================================
// D 表达式与面板（第 3 条）
// =============================================================================

void TstFilterStack::expressionCombinesWhitelistsWithAndBlacklistsWithOr()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("*.h"),
                                        QStringLiteral("-*.tmp"));
    QCOMPARE(stack.combinedExpression(), QStringLiteral("(*.cpp || *.h) && !(*.tmp)"));
}

void TstFilterStack::expressionIsEmptyWhenNothingIsActive()
{
    const FilterStack stack;
    QVERIFY(stack.combinedExpression().isEmpty());
    QVERIFY(!stack.hasActiveRule());

    // 只有注释与空行等于没有规则。
    FilterStack commented = stackWith(QStringLiteral("# 说明"), QStringLiteral(""), QStringLiteral("  "));
    QVERIFY(commented.combinedExpression().isEmpty());
}

void TstFilterStack::expressionOmitsDisabledAndEmptyLayers()
{
    FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                  QStringLiteral("*.h"),
                                  QStringLiteral(""));
    QCOMPARE(stack.combinedExpression(), QStringLiteral("(*.cpp || *.h)"));

    stack.setLayerEnabled(FilterLayer::Session, false);
    QCOMPARE(stack.combinedExpression(), QStringLiteral("*.cpp"));
}

void TstFilterStack::expressionQuotesAtomsThatContainOperators()
{
    // `a|b` 是一条名字里带竖线的掩码，不是「a 或 b」。不加引号的话表达式有歧义，
    // 而用户会照着它去理解过滤行为。
    const FilterStack stack = stackWith(QStringLiteral("a|b.txt"),
                                        QStringLiteral("c&d.txt"),
                                        QString());
    QCOMPARE(stack.combinedExpression(), QStringLiteral("('a|b.txt' || 'c&d.txt')"));
}

void TstFilterStack::expressionQuotesAtomsThatContainSpaces()
{
    const FilterStack stack = stackWith(QStringLiteral("a b.txt"), QString(), QString());
    QCOMPARE(stack.combinedExpression(), QStringLiteral("'a b.txt'"));
}

void TstFilterStack::expressionDeduplicatesRepeatedRules()
{
    // 同一段掩码出现在两层里是常事（格式层与会话层都要 *.cpp）。显示两遍会让
    // 面板看起来像坏了——用户会以为程序把同一条规则算了两遍。
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("*.cpp"),
                                        QString());
    QCOMPARE(stack.combinedExpression(), QStringLiteral("*.cpp"));
}

void TstFilterStack::expressionOfPureExcludesIsANegation()
{
    const FilterStack stack = stackWith(QStringLiteral("-*.tmp"),
                                        QStringLiteral("-*.log"),
                                        QString());
    QCOMPARE(stack.combinedExpression(), QStringLiteral("!(*.tmp || *.log)"));
}

void TstFilterStack::layerExpressionDescribesThatLayerAlone()
{
    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.cpp\n-*_test.cpp"));
    QCOMPARE(LqCompare::Filter::filterLayerExpression(stack.layerState(FilterLayer::View)),
             QStringLiteral("*.cpp && !(*_test.cpp)"));

    // 没有规则的层没有表达式（面板上显示为空，而不是「全部」）。
    QVERIFY(LqCompare::Filter::filterLayerExpression(stack.layerState(FilterLayer::Format)).isEmpty());
}

void TstFilterStack::panelRowsFollowTheSpecOrder()
{
    const FilterStack stack = stackWith(QStringLiteral("*"), QString(), QString());
    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(
        stack, threeNames(QStringLiteral("a.cpp"), QStringLiteral("b.h"), QStringLiteral("c.txt")));

    QCOMPARE(panel.layers.size(), 3);
    QCOMPARE(panel.layers.at(0).layer, FilterLayer::Format);
    QCOMPARE(panel.layers.at(1).layer, FilterLayer::Session);
    QCOMPARE(panel.layers.at(2).layer, FilterLayer::View);
}

void TstFilterStack::panelReportsStatusStorageAndSource()
{
    FilterLayerBinder binder;
    binder.setBuiltinDeclaration(QStringLiteral("*.cpp"));
    binder.setBuiltinSource(QStringLiteral("文件格式：C++ 源文件"));

    FilterStack stack;
    binder.loadInto(&stack);

    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(
        stack, QStringList() << QStringLiteral("a.cpp"));

    const auto &formatRow = panel.layers.at(rowIndex(FilterLayer::Format));
    QCOMPARE(formatRow.storageLabel,
             LqCompare::Filter::filterLayerStorageLabel(FilterLayerStorage::Builtin));
    QVERIFY(!formatRow.editable);
    QVERIFY(formatRow.active);
    QCOMPARE(formatRow.source, QStringLiteral("文件格式：C++ 源文件"));
    QCOMPARE(formatRow.expression, QStringLiteral("*.cpp"));
    QVERIFY(formatRow.statusText.contains(QStringLiteral("生效中")));

    // 没有声明的层：未生效，且行上明确说「已启用，但没有规则」——
    // 「没配置」与「配置了但被禁用」必须看得出区别。
    const auto &viewRow = panel.layers.at(rowIndex(FilterLayer::View));
    QVERIFY(viewRow.editable);
    QVERIFY(!viewRow.active);
    QVERIFY(viewRow.expression.isEmpty());
    QVERIFY(viewRow.statusText.contains(QStringLiteral("没有规则")));
    QCOMPARE(viewRow.storageLabel,
             LqCompare::Filter::filterLayerStorageLabel(FilterLayerStorage::ViewMemory));
}

void TstFilterStack::panelSummaryReusesTheFilterWording()
{
    // 第 3 条要「匹配计数」。这句话在 FILT-001 里已经被断言过形状，
    // 在这里重写一遍的话，两处迟早会出现「共 M 项 / 匹配 N 项」这种
    // 截图比对时没人会注意的顺序差异。
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QString());
    const QStringList names = threeNames(QStringLiteral("a.cpp"),
                                         QStringLiteral("b.cpp"),
                                         QStringLiteral("c.h"));

    const FilterStackPreview preview = stack.previewNames(names);
    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(stack, names);

    QCOMPARE(panel.summary, QStringLiteral("匹配 2 项 / 共 3 项"));
    QCOMPARE(panel.summary, preview.summary());
    QCOMPARE(panel.summary, preview.asMaskPreview().summary());
    QCOMPARE(panel.total, 3);
    QCOMPARE(panel.included, 2);
    QCOMPARE(panel.hidden(), 1);
}

void TstFilterStack::panelDecidedCountsAttributeTheDecision()
{
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QStringLiteral("main*"));
    const QStringList names = threeNames(QStringLiteral("main.cpp"),
                                         QStringLiteral("other.cpp"),
                                         QStringLiteral("notes.txt"));
    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(stack, names);

    // main.cpp → 放行（最后一道白名单是视图层）
    // other.cpp → 视图层白名单没命中
    // notes.txt → 会话层白名单没命中
    QCOMPARE(panel.layers.at(rowIndex(FilterLayer::View)).decided, 2);
    QCOMPARE(panel.layers.at(rowIndex(FilterLayer::Session)).decided, 1);
    QCOMPARE(panel.layers.at(rowIndex(FilterLayer::Format)).decided, 0);

    int sum = 0;
    for (const auto &row : panel.layers)
        sum += row.decided;
    QCOMPARE(sum, names.size());
}

void TstFilterStack::panelExpressionMatchesTheStack()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("*.h"),
                                        QStringLiteral("-*_test.cpp"));
    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(
        stack, QStringList() << QStringLiteral("a.cpp"));

    QCOMPARE(panel.expression, stack.combinedExpression());
    QVERIFY(panel.expression.contains(QStringLiteral("*.cpp")));
    QVERIFY(panel.expression.contains(QStringLiteral("*_test.cpp")));
    QVERIFY(panel.semantics.contains(QStringLiteral("交集")));
    QVERIFY(!panel.empty());
}

void TstFilterStack::panelIsEmptyWhenNothingIsActive()
{
    const FilterStack stack;
    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(
        stack, QStringList() << QStringLiteral("a.cpp"));

    QVERIFY(panel.empty());
    QVERIFY(panel.expression.isEmpty());
    // 没有生效层时，计数仍然是全量——「保留」不等于「没有算过」。
    QCOMPARE(panel.total, 1);
    QCOMPARE(panel.included, 1);
    QCOMPARE(panel.summary, QStringLiteral("匹配 1 项 / 共 1 项"));
    QVERIFY(panel.describe().contains(QStringLiteral("没有任何生效的过滤层")));
}

void TstFilterStack::panelTextListsEveryLayerAndTheExpression()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("*.h"),
                                        QStringLiteral("-*_test.cpp"));
    const auto panel = LqCompare::Filter::buildEffectiveFilterPanel(
        stack, QStringList() << QStringLiteral("a.cpp"));

    const QString text = panel.describe();
    QCOMPARE(panel.title, QStringLiteral("查看最终生效的过滤"));
    QVERIFY(text.contains(panel.title));
    QVERIFY(text.contains(panel.expression));
    // 三层都要出现在面板上，包括没生效的那些——「哪一层没在过滤」同样是要看的信息。
    for (FilterLayer layer : LqCompare::Filter::allFilterLayers())
        QVERIFY(text.contains(LqCompare::Filter::filterLayerLabel(layer)));
    QVERIFY(text.contains(panel.summary));

    // 引号说明只在**真的出现引号**时才印，免得解释一个不存在的引号。
    QVERIFY(!panel.expression.contains(QLatin1Char('\'')));
    QVERIFY(!text.contains(QStringLiteral("显示用的引号")));

    const FilterStack quoted = stackWith(QStringLiteral("a|b.txt"), QString(), QString());
    const auto quotedPanel = LqCompare::Filter::buildEffectiveFilterPanel(
        quoted, QStringList() << QStringLiteral("a|b.txt"));
    QVERIFY(quotedPanel.expression.contains(QLatin1Char('\'')));
    QVERIFY(quotedPanel.describe().contains(QStringLiteral("显示用的引号")));
}

// =============================================================================
// E 视图层不落盘（第 4 条）
// =============================================================================

void TstFilterStack::savingTheViewLayerWritesOnlyTheViewStore()
{
    MemorySessionSettings viewStore;
    MemorySessionSettings sessionStore;
    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);
    binder.setSessionStore(&sessionStore);

    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.tmp"));

    QVERIFY(binder.canSaveLayer(FilterLayer::View));
    QVERIFY(binder.saveLayer(FilterLayer::View, stack));

    // 视图存储里有，会话存储里**没有** —— 第 4 条的全部内容。
    QCOMPARE(viewStore.keys().size(), 1);
    QCOMPARE(viewStore.keys().at(0), LqCompare::Filter::filterDeclarationSettingKey());
    QCOMPARE(viewStore.value(LqCompare::Filter::filterDeclarationSettingKey()).toString(),
             QStringLiteral("*.tmp"));
    QVERIFY(sessionStore.keys().isEmpty());
}

void TstFilterStack::savingTheSessionLayerWritesOnlyTheSessionStore()
{
    MemorySessionSettings viewStore;
    MemorySessionSettings sessionStore;
    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);
    binder.setSessionStore(&sessionStore);

    FilterStack stack;
    stack.setDeclaration(FilterLayer::Session, QStringLiteral("*.cpp"));

    QVERIFY(binder.saveLayer(FilterLayer::Session, stack));
    QCOMPARE(sessionStore.value(LqCompare::Filter::filterDeclarationSettingKey()).toString(),
             QStringLiteral("*.cpp"));
    QVERIFY(viewStore.keys().isEmpty());
}

void TstFilterStack::formatLayerCanNeverBeSaved()
{
    MemorySessionSettings viewStore;
    MemorySessionSettings sessionStore;
    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);
    binder.setSessionStore(&sessionStore);
    binder.setBuiltinDeclaration(QStringLiteral("*.cpp"));

    FilterStack stack;
    stack.setDeclaration(FilterLayer::Format, QStringLiteral("*"));

    QVERIFY(!binder.canSaveLayer(FilterLayer::Format));
    QVERIFY(!binder.saveLayer(FilterLayer::Format, stack));
    // 一个键都没写——格式层来自文件格式定义，不该在用户的存储里留下痕迹。
    QVERIFY(viewStore.keys().isEmpty());
    QVERIFY(sessionStore.keys().isEmpty());
}

void TstFilterStack::savingWithoutAViewStoreFailsInsteadOfFallingBack()
{
    // 视图存储没接上时写进会话存储，意味着用户的一次临时过滤变成了永久的，
    // 而他从没同意过。这与 SESS-007「写入目标层缺失时返回失败」是同一条纪律。
    MemorySessionSettings sessionStore;
    FilterLayerBinder binder;
    binder.setSessionStore(&sessionStore);

    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.tmp"));

    QVERIFY(!binder.canSaveLayer(FilterLayer::View));
    QVERIFY(!binder.saveLayer(FilterLayer::View, stack));
    QVERIFY(sessionStore.keys().isEmpty());
}

void TstFilterStack::clearingALayerIsIdempotent()
{
    MemorySessionSettings viewStore;
    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);

    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.tmp"));
    QVERIFY(binder.saveLayer(FilterLayer::View, stack));
    QCOMPARE(viewStore.keys().size(), 1);

    // 用户把临时过滤删干净了：这是**正常操作**，不是失败。
    stack.setDeclaration(FilterLayer::View, QString());
    QVERIFY(binder.saveLayer(FilterLayer::View, stack));
    QVERIFY(viewStore.keys().isEmpty());

    // 再来一次也要成功：清空一个本来就空的层没有错。
    QVERIFY(binder.saveLayer(FilterLayer::View, stack));
    QVERIFY(viewStore.keys().isEmpty());
}

void TstFilterStack::discardViewLayerRemovesExactlyOneKey()
{
    MemorySessionSettings viewStore;
    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);

    QCOMPARE(binder.discardViewLayer(), 0); // 本来就没有，不算丢掉了东西

    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.tmp"));
    QVERIFY(binder.saveLayer(FilterLayer::View, stack));

    QCOMPARE(binder.discardViewLayer(), 1);
    QVERIFY(viewStore.keys().isEmpty());
    QCOMPARE(binder.discardViewLayer(), 0);
}

void TstFilterStack::loadIntoReadsEachLayerFromItsOwnStore()
{
    MemorySessionSettings viewStore;
    MemorySessionSettings sessionStore;
    sessionStore.setValue(LqCompare::Filter::filterDeclarationSettingKey(), QStringLiteral("*.cpp"));
    viewStore.setValue(LqCompare::Filter::filterDeclarationSettingKey(), QStringLiteral("main*"));

    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);
    binder.setSessionStore(&sessionStore);
    binder.setBuiltinDeclaration(QStringLiteral("*"));
    binder.setBuiltinSource(QStringLiteral("文件格式：源文件"));

    FilterStack stack;
    binder.loadInto(&stack);

    QCOMPARE(stack.declaration(FilterLayer::Format), QStringLiteral("*"));
    QCOMPARE(stack.declaration(FilterLayer::Session), QStringLiteral("*.cpp"));
    QCOMPARE(stack.declaration(FilterLayer::View), QStringLiteral("main*"));

    // 来源说明是面板上「这一层从哪儿来」的依据。
    QCOMPARE(stack.layerState(FilterLayer::Format).source, QStringLiteral("文件格式：源文件"));
    QVERIFY(!stack.layerState(FilterLayer::Session).source.isEmpty());
    QVERIFY(!stack.layerState(FilterLayer::View).source.isEmpty());

    // 三层真的叠在一起了。
    QVERIFY(acceptsName(stack, QStringLiteral("main.cpp")));
    QVERIFY(!acceptsName(stack, QStringLiteral("other.cpp")));
    QVERIFY(!acceptsName(stack, QStringLiteral("main.h")));
}

void TstFilterStack::loadIntoDoesNotUseTheScopePrecedenceChain()
{
    // 本条目最容易被踩坏的一处。SESS-007 的三层是**覆盖**（只有一个胜出），
    // 而过滤的三层是**叠加**（全部生效）。拿 ScopedSessionSettings::value()
    // 去取过滤声明，只会拿到优先级最高（视图层）那一条，会话层的过滤从此静默失效——
    // 而界面上两条设置看起来都在。
    //
    // 这个用例把两种读法的差别同时钉住：先证明作用域链确实只给一条，
    // 再证明 FilterLayerBinder 把三层都装进了栈。
    MemorySessionSettings viewLayer;
    MemorySessionSettings sessionLayer;
    MemorySessionSettings typeLayer;
    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &viewLayer);
    settings.setLayer(SettingScope::Session, &sessionLayer);
    settings.setLayer(SettingScope::Type, &typeLayer);

    sessionLayer.setValue(LqCompare::Filter::filterDeclarationSettingKey(),
                          QStringLiteral("-*.h"));
    viewLayer.setValue(LqCompare::Filter::filterDeclarationSettingKey(), QStringLiteral("*"));

    // 作用域链只返回视图层那一条——这正是**不能**用来读过滤声明的证据。
    QCOMPARE(settings.value(LqCompare::Filter::filterDeclarationSettingKey()).toString(),
             QStringLiteral("*"));

    FilterLayerBinder binder;
    binder.setViewStore(&viewLayer);
    binder.setSessionStore(&sessionLayer);
    binder.setBuiltinDeclaration(QStringLiteral("*"));

    FilterStack stack;
    binder.loadInto(&stack);

    // 按存储分别读之后，会话层的排除规则确实在起作用。
    // 若按作用域链读，栈里只会有一条 `*`，于是 a.h 会被放行——这就是那一层过滤
    // 静默消失的样子，而界面上两条设置看起来都在。
    QCOMPARE(stack.declaration(FilterLayer::Session), QStringLiteral("-*.h"));
    QCOMPARE(stack.declaration(FilterLayer::View), QStringLiteral("*"));
    QVERIFY(acceptsName(stack, QStringLiteral("a.cpp")));
    QVERIFY(!acceptsName(stack, QStringLiteral("a.h")));
    QCOMPARE(decideName(stack, QStringLiteral("a.h")).decidingLayer, FilterLayer::Session);
}

void TstFilterStack::loadIntoPreservesEnabledFlags()
{
    // `enabled` 是界面上的启用开关（第 2 条），不属于存储里的声明。
    // 整份替换会让每次重新载入都把用户的禁用选择抹掉，而现象是
    //「我刚关掉的那一层又自己开起来了」。
    MemorySessionSettings viewStore;
    viewStore.setValue(LqCompare::Filter::filterDeclarationSettingKey(), QStringLiteral("-*.tmp"));
    FilterLayerBinder binder;
    binder.setViewStore(&viewStore);

    FilterStack stack;
    stack.setLayerEnabled(FilterLayer::View, false);
    stack.setLayerEnabled(FilterLayer::Session, false);
    binder.loadInto(&stack);

    QVERIFY(!stack.isLayerEnabled(FilterLayer::View));
    QVERIFY(!stack.isLayerEnabled(FilterLayer::Session));
    // 声明确实载进来了（规则在），但因为被禁用，不参与叠加。
    QCOMPARE(stack.declaration(FilterLayer::View), QStringLiteral("-*.tmp"));
    QCOMPARE(stack.filter(FilterLayer::View).excludeCount(), 1);
    QVERIFY(acceptsName(stack, QStringLiteral("a.tmp")));

    // 重新勾上就恢复生效——证明上一条断言真的是靠「被禁用」而不是靠规则没载进来。
    stack.setLayerEnabled(FilterLayer::View, true);
    QVERIFY(!acceptsName(stack, QStringLiteral("a.tmp")));
}

void TstFilterStack::loadIntoWithoutStoresClearsDeclarations()
{
    FilterLayerBinder binder; // 什么都没接
    FilterStack stack;
    stack.setDeclaration(FilterLayer::View, QStringLiteral("*.tmp"));
    stack.setDeclaration(FilterLayer::Session, QStringLiteral("*.cpp"));

    binder.loadInto(&stack);

    QVERIFY(stack.declaration(FilterLayer::Format).isEmpty());
    QVERIFY(stack.declaration(FilterLayer::Session).isEmpty());
    QVERIFY(stack.declaration(FilterLayer::View).isEmpty());
    QVERIFY(stack.activeLayers().isEmpty());
    QVERIFY(stack.accepts(MaskSubject::forName(QStringLiteral("a.tmp"))));
}

void TstFilterStack::binderReportsWhatIsConnected()
{
    MemorySessionSettings viewStore;
    FilterLayerBinder binder;
    const QString before = binder.describe();
    QVERIFY(before.contains(QStringLiteral("未接上")));

    binder.setViewStore(&viewStore);
    binder.setBuiltinDeclaration(QStringLiteral("*.cpp"));
    const QString after = binder.describe();
    QVERIFY(after.contains(QStringLiteral("已接上")));
    QVERIFY(after.contains(QStringLiteral("*.cpp")));
    QVERIFY(after != before);
}

// =============================================================================
// F 计数（第 3、5 条）
// =============================================================================

void TstFilterStack::previewCountsEachVerdict()
{
    const FilterStack stack = stackWith(QStringLiteral("*\n-*.log"),
                                        QStringLiteral("*.cpp"),
                                        QString());
    const QStringList names = threeNames(QStringLiteral("a.cpp"),
                                         QStringLiteral("b.h"),
                                         QStringLiteral("c.log"));

    const FilterStackPreview preview = stack.previewNames(names);
    QCOMPARE(preview.total, 3);
    QCOMPARE(preview.included, 1);  // a.cpp
    QCOMPARE(preview.notMatched, 1); // b.h（会话层白名单）
    QCOMPARE(preview.excluded, 1);   // c.log（格式层排除）
    QCOMPARE(preview.hidden(), 2);
}

void TstFilterStack::previewCountsEachLayerIndependently()
{
    // 同一条目被两层同时排除时，**逐层计数会重叠**（各算一次），而合并计数只有一份。
    // 这两者不是一回事，面板上要分开写：「这一层自己命中了多少」与
    //「最终结论由谁起决定作用」。
    const FilterStack stack = stackWith(QStringLiteral("-*.tmp"),
                                        QStringLiteral("-*.tmp"),
                                        QString());
    const QStringList names = threeNames(QStringLiteral("a.tmp"),
                                         QStringLiteral("b.tmp"),
                                         QStringLiteral("c.txt"));

    const FilterStackPreview preview = stack.previewNames(names);
    QCOMPARE(preview.excluded, 2);
    QCOMPARE(preview.included, 1);

    const auto *formatStats = preview.statsFor(FilterLayer::Format);
    const auto *sessionStats = preview.statsFor(FilterLayer::Session);
    const auto *viewStats = preview.statsFor(FilterLayer::View);
    QVERIFY(formatStats != nullptr);
    QVERIFY(sessionStats != nullptr);
    QVERIFY(viewStats != nullptr);
    QCOMPARE(formatStats->excluded, 2);
    QCOMPARE(sessionStats->excluded, 2); // 同一批条目，两个层各自都算了一遍
    QCOMPARE(viewStats->excluded, 0);
    QVERIFY(!viewStats->active);
    QCOMPARE(formatStats->ruleCount, 1);
    QCOMPARE(formatStats->enabled, true);
    QCOMPARE(formatStats->active, true);
}

void TstFilterStack::previewDecidedMatchesTheDecision()
{
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QStringLiteral("main*"));
    const QStringList names = threeNames(QStringLiteral("main.cpp"),
                                         QStringLiteral("other.cpp"),
                                         QStringLiteral("notes.txt"));
    const FilterStackPreview preview = stack.previewNames(names);

    // 逐条核对：每条结论都必须能在它点名的那一层上记上一笔。
    for (const QString &name : names) {
        const LayeredFilterDecision decision = decideName(stack, name);
        if (!decision.anyLayerActive)
            continue;
        const auto *stats = preview.statsFor(decision.decidingLayer);
        QVERIFY(stats != nullptr);
        QVERIFY(stats->decided >= 1);
    }

    QCOMPARE(preview.statsFor(FilterLayer::View)->decided, 2);
    QCOMPARE(preview.statsFor(FilterLayer::Session)->decided, 1);
    QCOMPARE(preview.statsFor(FilterLayer::Format)->decided, 0);
}

void TstFilterStack::previewSummaryUsesTheFilterWording()
{
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QString());
    const FilterStackPreview preview =
        stack.previewNames(threeNames(QStringLiteral("a.cpp"),
                                      QStringLiteral("b.cpp"),
                                      QStringLiteral("c.h")));
    QCOMPARE(preview.summary(), QStringLiteral("匹配 2 项 / 共 3 项"));

    // hitsByRule 在分层之后没有统一的意义，必须留空——硬填会让人以为
    // 它的下标跨层连续。
    QVERIFY(preview.asMaskPreview().hitsByRule.isEmpty());
}

void TstFilterStack::previewNamesEqualsPreviewSubjects()
{
    const FilterStack stack = stackWith(QStringLiteral("*"),
                                        QStringLiteral("*.cpp"),
                                        QString());
    const QStringList names = threeNames(QStringLiteral("a.cpp"),
                                         QStringLiteral("b.cpp"),
                                         QStringLiteral("c.h"));

    QVector<MaskSubject> subjects;
    for (const QString &name : names)
        subjects.append(MaskSubject::forName(name));

    const FilterStackPreview fromNames = stack.previewNames(names);
    const FilterStackPreview fromSubjects = stack.preview(subjects);

    QCOMPARE(fromNames.total, fromSubjects.total);
    QCOMPARE(fromNames.included, fromSubjects.included);
    QCOMPARE(fromNames.excluded, fromSubjects.excluded);
    QCOMPARE(fromNames.notMatched, fromSubjects.notMatched);
    QCOMPARE(fromNames.summary(), fromSubjects.summary());
}

void TstFilterStack::stackDescribeMentionsTheExpression()
{
    const FilterStack stack = stackWith(QStringLiteral("*.cpp"),
                                        QStringLiteral("# 这一层没有规则"),
                                        QString());
    const QString text = stack.describe();
    QVERIFY(text.contains(QStringLiteral("*.cpp")));
    QVERIFY(text.contains(QStringLiteral("三层过滤")));

    // 一层都没生效时要说「没有任何生效的过滤」，而不是给一个空表达式。
    const FilterStack empty;
    QVERIFY(empty.describe().contains(QStringLiteral("没有任何生效的过滤")));
}

// =============================================================================
// G 层级表自检（第 1、4 条的启动护栏）
// =============================================================================
//
// 这一组存在的理由：`validateFilterLayerTable()` 是**启动时**跑的护栏，而一条永远
// 不会红的护栏比没有护栏更糟——它会让人以为这块已经被守住了。所以这里逐条拿
//「故意写坏」的表跑同一个判定，证明它真的会报。
//
// 注意每条都断言了问题的**内容**而不只是「不为空」：只断言非空的话，一个把所有输入
// 都判成有问题的实现同样能过，而那等价于没有护栏。

namespace {

using LqCompare::Filter::FilterLayerDescriptor;

QVector<FilterLayerDescriptor> shippedTable()
{
    return LqCompare::Filter::filterLayerTable();
}

QString joinedProblems(const QStringList &problems)
{
    return problems.join(QStringLiteral(" / "));
}

} // namespace

void TstFilterStack::shippedLayerTableIsClean()
{
    // 真实的那张表必须是干净的——否则每次启动都会往日志里写错误。
    const QStringList problems =
        LqCompare::Filter::validateFilterLayerTable(LqCompare::Filter::filterLayerTable());
    QVERIFY2(problems.isEmpty(), qPrintable(joinedProblems(problems)));
}

void TstFilterStack::layerTableIsTheSingleSourceOfTruth()
{
    // 顺序、下标、落点三样都从表里推导。分成三处各写一遍的话，往表里插一层就会出现
    //「面板上排在第二、写进存储用的却是第三个落点」这类错位。
    const QVector<FilterLayerDescriptor> table = LqCompare::Filter::filterLayerTable();
    QCOMPARE(LqCompare::Filter::allFilterLayers().size(), table.size());
    for (int index = 0; index < table.size(); ++index) {
        QCOMPARE(LqCompare::Filter::allFilterLayers().at(index), table.at(index).layer);
        QCOMPARE(LqCompare::Filter::filterLayerIndex(table.at(index).layer), index);
        QCOMPARE(LqCompare::Filter::filterLayerStorage(table.at(index).layer), table.at(index).storage);
    }
}

void TstFilterStack::validatorCatchesAMissingLayer()
{
    QVector<FilterLayerDescriptor> broken = shippedTable();
    broken.removeLast();
    const QStringList problems = LqCompare::Filter::validateFilterLayerTable(broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinedProblems(problems).contains(
        LqCompare::Filter::filterLayerLabel(FilterLayer::View)));
}

void TstFilterStack::validatorCatchesADuplicatedLayer()
{
    QVector<FilterLayerDescriptor> broken = shippedTable();
    broken[2] = broken[0];
    const QStringList problems = LqCompare::Filter::validateFilterLayerTable(broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinedProblems(problems).contains(QStringLiteral("2 次")));
}

void TstFilterStack::validatorCatchesAReorderedTable()
{
    // 交集本身与顺序无关（交换律），但顺序决定「第一个排除它的层」报的是谁，
    // 而用户是照那一层给的建议去改的。
    QVector<FilterLayerDescriptor> broken = shippedTable();
    qSwap(broken[0], broken[2]);
    const QStringList problems = LqCompare::Filter::validateFilterLayerTable(broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinedProblems(problems).contains(QStringLiteral("第 1 行")));
}

void TstFilterStack::validatorCatchesAViewLayerThatWouldPersist()
{
    // 本条最贵的一处错误：视图层改成随会话保存之后，过滤照样工作，只是关掉标签
    // 它还在——没有任何运行期现象会提示这件事。
    QVector<FilterLayerDescriptor> broken = shippedTable();
    for (FilterLayerDescriptor &row : broken) {
        if (row.layer == FilterLayer::View)
            row.storage = FilterLayerStorage::SessionFile;
    }
    const QStringList problems = LqCompare::Filter::validateFilterLayerTable(broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinedProblems(problems).contains(QStringLiteral("视图层的落点")));
}

void TstFilterStack::validatorCatchesAFormatLayerThatWouldBeWritable()
{
    QVector<FilterLayerDescriptor> broken = shippedTable();
    for (FilterLayerDescriptor &row : broken) {
        if (row.layer == FilterLayer::Format)
            row.storage = FilterLayerStorage::ViewMemory;
    }
    const QStringList problems = LqCompare::Filter::validateFilterLayerTable(broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinedProblems(problems).contains(QStringLiteral("格式层的落点")));
}

void TstFilterStack::validatorCatchesAWrongRowCount()
{
    QVector<FilterLayerDescriptor> broken = shippedTable();
    broken.append(FilterLayerDescriptor{FilterLayer::View, FilterLayerStorage::ViewMemory});
    const QStringList problems = LqCompare::Filter::validateFilterLayerTable(broken);
    QVERIFY(!problems.isEmpty());
    QVERIFY(joinedProblems(problems).contains(QStringLiteral("实际 4 行")));
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstFilterStack)
