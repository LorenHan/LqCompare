#include "tst_settingsscope.h"

using namespace LqCompare;

namespace {

/// 作用域的机器标识（用例里比较枚举一律走它，与 Tests/Settings 的既有写法一致：
/// 字符串是稳定的对外事实，枚举序号不是——在中间插一个值就会整体错位）。
QString idOf(SettingScope scope)
{
    return QString::fromLatin1(settingScopeIdentifier(scope));
}

QString idOf(const QVector<SettingScope> &scopes)
{
    QStringList ids;
    for (SettingScope scope : scopes) {
        ids << idOf(scope);
    }
    return ids.join(QLatin1Char('/'));
}

/// 解析结果转成机器标识，供断言比较。
QString resolvedIdOf(const ScopedSessionSettings &settings, const QString &key)
{
    SettingScope found = SettingScope::Session;
    if (!settings.resolvedFromLayer(key, &found)) {
        return QStringLiteral("<none>");
    }
    return idOf(found);
}

SettingItem boolItem(const QString &key, bool defaultValue)
{
    SettingItem item;
    item.key = key;
    item.title = QStringLiteral("开关 %1").arg(key);
    item.description = QStringLiteral("用于验证覆盖链的合成开关项。");
    item.control = SettingControl::Bool;
    item.defaultValue = defaultValue;
    return item;
}

SettingItem integerItem(const QString &key, int defaultValue)
{
    SettingItem item;
    item.key = key;
    item.title = QStringLiteral("整数 %1").arg(key);
    item.description = QStringLiteral("用于验证覆盖链的合成整数项。");
    item.control = SettingControl::Integer;
    item.defaultValue = defaultValue;
    item.validation.hasMinimum = true;
    item.validation.minimum = 1;
    item.validation.hasMaximum = true;
    item.validation.maximum = 64;
    return item;
}

SettingItem textItem(const QString &key, const QString &defaultValue)
{
    SettingItem item;
    item.key = key;
    item.title = QStringLiteral("文本 %1").arg(key);
    item.description = QStringLiteral("用于验证覆盖链的合成文本项。");
    item.control = SettingControl::Text;
    item.defaultValue = defaultValue;
    return item;
}

SettingItem maskItem(const QString &key, const QStringList &defaultValue)
{
    SettingItem item;
    item.key = key;
    item.title = QStringLiteral("掩码 %1").arg(key);
    item.description = QStringLiteral("用于验证覆盖链的合成掩码清单项。");
    item.control = SettingControl::MaskList;
    item.defaultValue = defaultValue;
    return item;
}

} // namespace

// -----------------------------------------------------------------------------
// 合成声明
// -----------------------------------------------------------------------------

SettingsSchema TstSettingsScope::syntheticSchema() const
{
    SettingsTab matching;
    matching.id = QStringLiteral("matching");
    matching.title = QStringLiteral("比对");
    matching.description = QStringLiteral("怎么比。");

    SettingGroup general;
    general.id = QStringLiteral("general");
    general.title = QStringLiteral("常规");
    general.description = QStringLiteral("常规设置。");
    general.items << boolItem(QStringLiteral("ignore-case"), false)
                  << integerItem(QStringLiteral("tab-width"), 4);
    matching.groups << general;

    SettingGroup files;
    files.id = QStringLiteral("files");
    files.title = QStringLiteral("文件");
    files.description = QStringLiteral("参与比对的文件。");
    // 默认值里**刻意留一个末尾空行**：会话文件里存的多行掩码是一段文本，
    // 从文本读回来最容易带出这个尾巴。它是 `SettingItem::normalized()` 存在的
    // 直接理由，也是「出厂默认必须按归一后的形态交出去」那条用例的抓手——
    // 不归一的话这里会拿到 2 条，而用户看到的设置是 1 条。
    files.items << maskItem(QStringLiteral("exclude-masks"),
                            QStringList{QStringLiteral("*.tmp"), QString()});
    matching.groups << files;

    SettingsTab display;
    display.id = QStringLiteral("display");
    display.title = QStringLiteral("显示");
    display.description = QStringLiteral("怎么显示。");
    SettingGroup text;
    text.id = QStringLiteral("text");
    text.title = QStringLiteral("文本");
    text.description = QStringLiteral("文本显示设置。");
    text.items << textItem(QStringLiteral("encoding"), QStringLiteral("utf-8"));
    display.groups << text;

    SettingsSchema schema;
    schema.typeId = QStringLiteral("synthetic-text");
    schema.tabs << matching << display;
    return schema;
}

void TstSettingsScope::initTestCase()
{
    // 用例自己的合成声明必须是合法声明。它不合法的话，后面所有关于覆盖链的
    // 结论都建立在一张写错的表上，而每一条用例仍然可能是绿的。
    const SettingsSchema schema = syntheticSchema();
    const QStringList problems = schema.validate();
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("；"))));
}

// -----------------------------------------------------------------------------
// A 优先级顺序（第 1 条）
// -----------------------------------------------------------------------------

void TstSettingsScope::priorityOrderIsViewSessionType()
{
    // 第 1 条点名的三层优先级：视图 > 会话 > 类型。这条用例是它的直译，
    // 把顺序反过来的实现在这里当场红，而不是等到某个用户的视图级改动不生效。
    QCOMPARE(idOf(settingScopePriorityOrder()),
             QStringLiteral("view/session/type"));
}

void TstSettingsScope::ranksFollowThePriorityOrder()
{
    QCOMPARE(settingScopeRank(SettingScope::View), 0);
    QCOMPARE(settingScopeRank(SettingScope::Session), 1);
    QCOMPARE(settingScopeRank(SettingScope::Type), 2);
}

void TstSettingsScope::everyScopeAppearsExactlyOnceInBothOrders()
{
    // 「展示顺序」与「解析顺序」是两个函数（理由见头文件）。它们必须都覆盖
    // **不多不少**三个作用域：少一个，那一层就永远不可能被解析到——
    // 而现象是「某一层设了没用」，看不出是排序函数漏了一项。
    QStringList display;
    for (SettingScope scope : allSettingScopes()) {
        display << idOf(scope);
    }
    display.sort();

    QStringList priority;
    for (SettingScope scope : settingScopePriorityOrder()) {
        priority << idOf(scope);
    }
    priority.sort();

    QCOMPARE(priority, display);
    QCOMPARE(display, (QStringList{QStringLiteral("session"), QStringLiteral("type"),
                                   QStringLiteral("view")}));
}

void TstSettingsScope::higherPriorityScopeWins()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("tab-width"), 8);
    session.setValue(QStringLiteral("tab-width"), 16);
    type.setValue(QStringLiteral("tab-width"), 32);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 8);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("view"));
}

void TstSettingsScope::aLayerWithoutTheKeyIsSkipped()
{
    MemorySessionSettings view;   // 视图层里没有这一项
    MemorySessionSettings session;
    MemorySessionSettings type;
    type.setValue(QStringLiteral("tab-width"), 32);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    // 视图层「存在但没有这一项」与「视图层不存在」必须是同一个结果：
    // 前者若被当成「有值（空值）」，用户会发现自己每打开一个会话、
    // 设置就莫名其妙回到空。
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 32);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("type"));

    // 再补上会话层（同样没有这一项）——结果不变。
    settings.setLayer(SettingScope::Session, &session);
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 32);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("type"));
}

// -----------------------------------------------------------------------------
// B 覆盖链与出厂默认（第 4 条）
// -----------------------------------------------------------------------------

void TstSettingsScope::chainEndsAtTheFactoryDefault()
{
    const SettingsSchema schema = syntheticSchema();

    ScopedSessionSettings settings;
    settings.setSchema(&schema);

    // 三层都没有值，链的最后一环是声明里的出厂默认。
    QCOMPARE(settings.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf-8"));
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(settings.value(QStringLiteral("ignore-case")).toBool(), false);
}

void TstSettingsScope::factoryDefaultComesBeforeTheCallersFallback()
{
    const SettingsSchema schema = syntheticSchema();

    ScopedSessionSettings settings;
    settings.setSchema(&schema);

    // 第 4 条把链写成「视图 → 会话 → 类型 → 出厂默认」，**没有**调用方的 fallback
    // 这一环。把 fallback 排在声明之前的实现会在这里返回 "latin-1"——
    // 而它看上去同样「有个值」，界面上完全看不出这一项读错了来源。
    QCOMPARE(settings.value(QStringLiteral("encoding"), QStringLiteral("latin-1")).toString(),
             QStringLiteral("utf-8"));
}

void TstSettingsScope::aKeyWithoutADeclarationUsesTheCallersFallback()
{
    const SettingsSchema schema = syntheticSchema();

    MemorySessionSettings type;
    type.setValue(QStringLiteral("legacy-field"), QStringLiteral("kept"));

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::Type, &type);
    settings.setSchema(&schema);

    // 声明里没有这一项、三层里也没有 → 只能给调用方给的值。
    QCOMPARE(settings.value(QStringLiteral("no-such-key"), 42).toInt(), 42);
    // 但三层里有的时候不问声明：链在到达出厂默认之前就找到值了。
    QCOMPARE(settings.value(QStringLiteral("legacy-field")).toString(), QStringLiteral("kept"));
}

void TstSettingsScope::anExplicitEmptyValueIsNotAMissingValue()
{
    MemorySessionSettings session;
    MemorySessionSettings type;
    type.setValue(QStringLiteral("encoding"), QStringLiteral("latin-1"));
    // 用户把这一项清空了。这是一次真实的设置，不是「没设置」。
    session.setValue(QStringLiteral("encoding"), QString());

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    // 按「值是不是空的」判断命中的实现会越过会话层，拿到类型层的 latin-1 ——
    // 于是用户在设置里把编码清掉，重开一看又回来了，而且没有任何提示。
    QCOMPARE(settings.value(QStringLiteral("encoding")).toString(), QString());
    QVERIFY(settings.contains(QStringLiteral("encoding")));
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("encoding")), QStringLiteral("session"));
}

void TstSettingsScope::withoutASchemaTheChainStopsAtTheTypeLayer()
{
    MemorySessionSettings type;
    type.setValue(QStringLiteral("tab-width"), 7);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::Type, &type);

    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 7);
    // 没有声明就没有出厂默认这一环，直接落到调用方的取值。
    QCOMPARE(settings.value(QStringLiteral("encoding"), QStringLiteral("fallback")).toString(),
             QStringLiteral("fallback"));
}

void TstSettingsScope::theFactoryDefaultIsHandedOutNormalised()
{
    const SettingsSchema schema = syntheticSchema();
    const SettingItem *item = schema.findItem(QStringLiteral("exclude-masks"));
    QVERIFY(item != nullptr);

    ScopedSessionSettings settings;
    settings.setSchema(&schema);

    // 合成声明里 `exclude-masks` 的默认值带着一个末尾空行（那是从一段文本读回来
    // 最常见的形态）。交出去之前必须归一：直接返回声明里那一份的话，这里会拿到
    // 2 条，其中一条是空掩码 —— 而界面显示的是 1 条，于是「改动了吗」永远为真。
    const QVariant value = settings.value(QStringLiteral("exclude-masks"));
    QCOMPARE(value.toStringList(), (QStringList{QStringLiteral("*.tmp")}));
    QCOMPARE(value.toStringList(), item->normalized(item->defaultValue).toStringList());
    QCOMPARE(value.toStringList().size(), 1);
}

// -----------------------------------------------------------------------------
// C 写入路由与「三层互不覆盖」（第 1 条边界）
// -----------------------------------------------------------------------------

void TstSettingsScope::writeLandsOnlyInTheTargetLayer()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);
    settings.setWriteScope(SettingScope::Session);

    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 8));

    QCOMPARE(session.value(QStringLiteral("tab-width")).toInt(), 8);
    QVERIFY(!view.contains(QStringLiteral("tab-width")));
    QVERIFY(!type.contains(QStringLiteral("tab-width")));
}

void TstSettingsScope::writingToViewDoesNotPolluteSessionOrTypeDefaults()
{
    // 这是边界条款最直接的那一半：「视图级改动不得污染会话默认值」。
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    session.setValue(QStringLiteral("tab-width"), 4);
    type.setValue(QStringLiteral("tab-width"), 4);
    const QStringList sessionKeysBefore = session.keys();
    const QStringList typeKeysBefore = type.keys();

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);
    settings.setWriteScope(SettingScope::View);

    QVERIFY(settings.setValue(QStringLiteral("ignore-case"), true));
    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 12));

    QCOMPARE(view.value(QStringLiteral("ignore-case")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("tab-width")).toInt(), 12);
    // 会话层与类型层必须**一个字都没变**。一个「顺手把改动也写进会话」的
    // 实现会让用户下次打开这个会话时发现自己上次只是临时试了一下的大小写
    // 设置变成了默认值，而他从没同意过。
    QCOMPARE(session.keys(), sessionKeysBefore);
    QCOMPARE(type.keys(), typeKeysBefore);
    QCOMPARE(session.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(type.value(QStringLiteral("tab-width")).toInt(), 4);
}

void TstSettingsScope::writingToSessionDoesNotTouchTheViewLayer()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("tab-width"), 12);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);
    settings.setWriteScope(SettingScope::Session);

    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 4));

    QCOMPARE(view.value(QStringLiteral("tab-width")).toInt(), 12);
    QCOMPARE(session.value(QStringLiteral("tab-width")).toInt(), 4);
    QVERIFY(!type.contains(QStringLiteral("tab-width")));
}

void TstSettingsScope::writingToTypeDoesNotTouchTheOtherTwoLayers()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("ignore-case"), true);
    session.setValue(QStringLiteral("ignore-case"), false);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);
    settings.setWriteScope(SettingScope::Type);

    QVERIFY(settings.setValue(QStringLiteral("ignore-case"), true));

    QCOMPARE(type.value(QStringLiteral("ignore-case")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("ignore-case")).toBool(), true);
    QCOMPARE(session.value(QStringLiteral("ignore-case")).toBool(), false);
}

void TstSettingsScope::writeFailsAndStoresNothingWhenTheTargetLayerIsMissing()
{
    MemorySessionSettings view;
    view.setValue(QStringLiteral("encoding"), QStringLiteral("utf-8"));

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    // 写入目标是类型层，而这一层没有接上（例如这个会话的类型还没有默认值存储）。
    settings.setWriteScope(SettingScope::Type);

    QVERIFY(!settings.setValue(QStringLiteral("tab-width"), 8));
    // 「退而写入某一层」的实现在这里会返回 true 并把值塞进视图层：
    // 用户以为「保存成所有新会话的默认值」了，其实关掉标签就没了。
    QVERIFY(!view.contains(QStringLiteral("tab-width")));
    QVERIFY(!settings.contains(QStringLiteral("tab-width")));
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("<none>"));
}

void TstSettingsScope::emptyKeyIsRejected()
{
    MemorySessionSettings session;
    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::Session, &session);
    settings.setWriteScope(SettingScope::Session);

    // 空键在链里无法与「没设置」区分，接口层面就拒掉（与 MemorySessionSettings 一致）。
    QVERIFY(!settings.setValue(QString(), 1));
    QVERIFY(!settings.remove(QString()));
    QVERIFY(!settings.contains(QString()));
    QCOMPARE(settings.value(QString(), QStringLiteral("fallback")).toString(),
             QStringLiteral("fallback"));
    QVERIFY(session.keys().isEmpty());
}

void TstSettingsScope::aShadowedWriteIsStillStoredWhereItWasAskedToGo()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    view.setValue(QStringLiteral("tab-width"), 12);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setWriteScope(SettingScope::Session);

    QSignalSpy spy(&settings, &SessionSettings::changed);
    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 4));

    // 用户要求「保存到当前会话」，那就保存到当前会话 —— 即使他眼下看到的仍然是
    // 视图层那个值。悄悄改写视图层（让它看起来生效了）才是错的：那会让这次改动
    // 活不过关标签，而用户以为自己存下来了。
    QCOMPARE(session.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(view.value(QStringLiteral("tab-width")).toInt(), 12);
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 12);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("view"));
    // 有效值没变，所以不发 changed —— 界面不会因为一次「看不到效果的保存」抖动。
    QCOMPARE(spy.count(), 0);
}

void TstSettingsScope::removeOnlyAffectsTheTargetLayer()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("tab-width"), 12);
    session.setValue(QStringLiteral("tab-width"), 4);
    type.setValue(QStringLiteral("tab-width"), 2);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);
    settings.setWriteScope(SettingScope::Session);

    QVERIFY(settings.remove(QStringLiteral("tab-width")));

    // 用户的意思是「这个会话不要单独设这一项了」，不是「把类型的默认值也删掉」。
    QVERIFY(!session.contains(QStringLiteral("tab-width")));
    QCOMPARE(view.value(QStringLiteral("tab-width")).toInt(), 12);
    QCOMPARE(type.value(QStringLiteral("tab-width")).toInt(), 2);
    // 目标层本来就没有这一条时不能再往下删。
    QVERIFY(!settings.remove(QStringLiteral("tab-width")));
    QCOMPARE(type.value(QStringLiteral("tab-width")).toInt(), 2);
}

void TstSettingsScope::clearOnlyClearsTheTargetLayer()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("ignore-case"), true);
    session.setValue(QStringLiteral("tab-width"), 8);
    session.setValue(QStringLiteral("encoding"), QStringLiteral("gbk"));
    type.setValue(QStringLiteral("tab-width"), 4);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);
    settings.setWriteScope(SettingScope::Session);

    settings.clear();

    QVERIFY(session.keys().isEmpty());
    // 类型层是所有新会话共用的默认值。被一个会话的「清空」带走等于一次跨会话的
    // 破坏性操作，而且不可逆 —— 这里必须原封不动。
    QCOMPARE(type.keys(), (QStringList{QStringLiteral("tab-width")}));
    QCOMPARE(type.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(view.keys(), (QStringList{QStringLiteral("ignore-case")}));
    // 清空的返回值：只清到了几条就报几条。
    QCOMPARE(settings.clearLayer(SettingScope::View), 1);
    QVERIFY(view.keys().isEmpty());
}

void TstSettingsScope::keysAreTheUnionOfTheThreeLayers()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("ignore-case"), true);
    session.setValue(QStringLiteral("encoding"), QStringLiteral("gbk"));
    session.setValue(QStringLiteral("tab-width"), 8);
    type.setValue(QStringLiteral("ignore-case"), false);
    type.setValue(QStringLiteral("wrap"), true);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    // 三层并集、去重、按字典序（接口约定）。同一项在视图层与会话层都有是常态，
    // 不去重的话 keys() 会把同一个键报两遍，而调用方多半拿它去驱动一个列表。
    QCOMPARE(settings.keys(), (QStringList{QStringLiteral("encoding"), QStringLiteral("ignore-case"),
                                           QStringLiteral("tab-width"), QStringLiteral("wrap")}));
}

void TstSettingsScope::containsIgnoresTheFactoryDefault()
{
    const SettingsSchema schema = syntheticSchema();

    ScopedSessionSettings settings;
    settings.setSchema(&schema);

    // contains() 回答的是「用户设过没有」，value() 回答的是「现在的值是多少」。
    // 把出厂默认也算进 contains()，「这一项用户动过吗」就永远为真。
    QVERIFY(!settings.contains(QStringLiteral("encoding")));
    QCOMPARE(settings.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf-8"));
    QVERIFY(settings.keys().isEmpty());
}

// -----------------------------------------------------------------------------
// D 写入去向的文案与切换提示（第 2 条）
// -----------------------------------------------------------------------------

void TstSettingsScope::destinationTextMatchesTheSpecWording()
{
    // 逐字钉住规格里的形状「本次修改将保存到 X」，三种作用域各一条。
    // 写成用 settingScopeLabel() 拼出来的期望值会变成同义反复；这里要的是
    // 「界面上到底显示什么」，所以三句都写死。
    QCOMPARE(writeDestinationText(SettingScope::View),
             QStringLiteral("本次修改将保存到「仅当前视图」"));
    QCOMPARE(writeDestinationText(SettingScope::Session),
             QStringLiteral("本次修改将保存到「当前会话默认值」"));
    QCOMPARE(writeDestinationText(SettingScope::Type),
             QStringLiteral("本次修改将保存到「该类型全部新会话默认值」"));
}

void TstSettingsScope::eachDestinationTextIsDistinct()
{
    QStringList texts;
    for (SettingScope scope : allSettingScopes()) {
        const QString text = writeDestinationText(scope);
        QVERIFY(!text.isEmpty());
        QVERIFY2(!texts.contains(text), qPrintable(QStringLiteral("去向文案重复：%1").arg(text)));
        texts << text;
    }
    QCOMPARE(texts.size(), 3);
}

void TstSettingsScope::noPendingChangesMeansNoNotice()
{
    // 没有待定的改动就没必要问。每次拉一下下拉都弹一次提示，用户会学会无视它，
    // 于是真正该看到的提示也一起失效。
    const ScopeSwitchNotice notice =
        scopeSwitchNotice(SettingScope::View, SettingScope::Session, QStringList());
    QVERIFY(!notice.ask);
    QVERIFY(notice.title.isEmpty());
    QVERIFY(notice.text.isEmpty());
}

void TstSettingsScope::switchingToTheSameScopeMeansNoNotice()
{
    QVERIFY(!scopeSwitchNotice(SettingScope::Session, SettingScope::Session,
                               QStringList{QStringLiteral("tab-width")})
                 .ask);
}

void TstSettingsScope::noticeNamesTheNewDestinationAndTheCount()
{
    const QStringList pending{QStringLiteral("tab-width"), QStringLiteral("encoding")};
    const ScopeSwitchNotice notice =
        scopeSwitchNotice(SettingScope::Session, SettingScope::Type, pending);

    QVERIFY(notice.ask);
    QVERIFY(!notice.title.isEmpty());
    // 「已有改动将改写到何处」必须同时说清**有多少项**与**去哪儿**。
    QVERIFY2(notice.text.contains(QStringLiteral("2")), qPrintable(notice.text));
    QVERIFY2(notice.text.contains(settingScopeLabel(SettingScope::Type)), qPrintable(notice.text));
    QVERIFY(!notice.describe().isEmpty());
}

void TstSettingsScope::noticeDoesNotClaimThatAppliedChangesMove()
{
    const QStringList pending{QStringLiteral("tab-width")};
    const ScopeSwitchNotice notice =
        scopeSwitchNotice(SettingScope::Type, SettingScope::View, pending);

    // 切换作用域**不会**把已经应用过的值搬到新的层（见 setWriteScope 的说明）。
    // 文案若写成「会把它们改写到新位置」，用户会以为下拉是一次迁移命令，
    // 于是切换之后去检查原来那一层的值，发现还在，进而认为切换坏了。
    QVERIFY2(notice.text.contains(QStringLiteral("尚未应用")), qPrintable(notice.text));
}

void TstSettingsScope::theDestinationTextFollowsTheWriteScope()
{
    ScopedSessionSettings settings;
    settings.setWriteScope(SettingScope::Session);
    QCOMPARE(settings.writeDestinationText(), writeDestinationText(SettingScope::Session));

    settings.setWriteScope(SettingScope::Type);
    QCOMPARE(settings.writeDestinationText(), writeDestinationText(SettingScope::Type));

    // 切换写入目标本身不是一次设置改动，不该让会话变脏。
    QSignalSpy spy(&settings, &SessionSettings::changed);
    settings.setWriteScope(SettingScope::View);
    QCOMPARE(spy.count(), 0);
}

// -----------------------------------------------------------------------------
// E 关闭标签时丢弃视图级设置（第 3 条）
// -----------------------------------------------------------------------------

void TstSettingsScope::noViewScopeSettingsMeansNoQuestion()
{
    const ViewScopeClosePlan plan = planViewScopeClose(QStringList());

    QVERIFY(!plan.ask);
    QVERIFY(plan.keys.isEmpty());
    QVERIFY(!plan.describe().isEmpty());
}

void TstSettingsScope::closePlanNamesTheCountAndSaysDiscarded()
{
    const ViewScopeClosePlan plan =
        planViewScopeClose(QStringList{QStringLiteral("tab-width"), QStringLiteral("ignore-case")});

    QVERIFY(plan.ask);
    QCOMPARE(plan.keys, (QStringList{QStringLiteral("ignore-case"), QStringLiteral("tab-width")}));
    QVERIFY2(plan.text.contains(QStringLiteral("2")), qPrintable(plan.text));
    // 「关闭前有提示」的提示必须说清后果是**丢弃**，而不是「保存」。
    QVERIFY2(plan.text.contains(QStringLiteral("丢弃")), qPrintable(plan.text));
    QVERIFY(!plan.title.isEmpty());
}

void TstSettingsScope::discardRestoresTheLowerLayersValues()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    session.setValue(QStringLiteral("tab-width"), 4);
    type.setValue(QStringLiteral("encoding"), QStringLiteral("gbk"));
    view.setValue(QStringLiteral("tab-width"), 12);
    view.setValue(QStringLiteral("encoding"), QStringLiteral("utf-16"));

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 12);
    QCOMPARE(settings.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf-16"));

    const int dropped = settings.discardViewScope();

    // 「关闭标签即丢弃」的落点：丢弃之后这个会话回到自己那一套值。
    QCOMPARE(dropped, 2);
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(settings.value(QStringLiteral("encoding")).toString(), QStringLiteral("gbk"));
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("session"));
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("encoding")), QStringLiteral("type"));
    QVERIFY(view.keys().isEmpty());
}

void TstSettingsScope::discardSignalsOnlyKeysWhoseEffectiveValueChanged()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    session.setValue(QStringLiteral("tab-width"), 4);
    session.setValue(QStringLiteral("ignore-case"), true);
    view.setValue(QStringLiteral("tab-width"), 12); // 有效值会变
    view.setValue(QStringLiteral("ignore-case"), true); // 与下面一层相同，有效值不变

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);

    QSignalSpy spy(&settings, &SessionSettings::changed);
    QCOMPARE(settings.discardViewScope(), 2);

    // 两条都被丢弃了，但只有一条的改变用户看得出来。为另一条也发信号会让
    // 状态栏与标签上的脏标记白抖一次，而抖动的来源极难归因到某一次赋值。
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("tab-width"));
}

void TstSettingsScope::discardReturnsTheNumberOfDroppedKeys()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    view.setValue(QStringLiteral("a-setting"), 1);
    view.setValue(QStringLiteral("b-setting"), 2);
    session.setValue(QStringLiteral("c-setting"), 3);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);

    // 数的是**视图层里真有的**条数，不是三层并集的大小。
    QCOMPARE(settings.discardViewScope(), 2);
    // 第二次调用没有东西可丢，必须是 0 而不是报错。
    QCOMPARE(settings.discardViewScope(), 0);
    QCOMPARE(session.keys(), (QStringList{QStringLiteral("c-setting")}));
}

void TstSettingsScope::viewScopeKeysAreSortedAndDeduped()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;
    view.setValue(QStringLiteral("tab-width"), 12);
    view.setValue(QStringLiteral("ignore-case"), true);
    session.setValue(QStringLiteral("tab-width"), 4);
    session.setValue(QStringLiteral("encoding"), QStringLiteral("gbk"));
    type.setValue(QStringLiteral("wrap"), true);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    // 关标签时会被丢掉的只有视图层那两条。把另外两层的键也算进来的话，
    // 提示会报出「有 5 项将被丢弃」，而用户根本找不出那 3 项。
    QCOMPARE(settings.viewScopeKeys(),
             (QStringList{QStringLiteral("ignore-case"), QStringLiteral("tab-width")}));
}

void TstSettingsScope::discardWithoutAViewLayerIsHarmless()
{
    MemorySessionSettings session;
    session.setValue(QStringLiteral("tab-width"), 4);

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::Session, &session);

    // 视图层没接上（或从来没有过）时不是错误：绝大多数关闭动作都不带视图级设置。
    QCOMPARE(settings.discardViewScope(), 0);
    QCOMPARE(settings.viewScopeKeys(), QStringList());
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 4);
}

// -----------------------------------------------------------------------------
// F 解析诊断与反向验证
// -----------------------------------------------------------------------------

void TstSettingsScope::resolvedScopeReportsWhichLayerWon()
{
    MemorySessionSettings view;
    MemorySessionSettings session;
    MemorySessionSettings type;

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setLayer(SettingScope::Session, &session);
    settings.setLayer(SettingScope::Type, &type);

    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("<none>"));

    type.setValue(QStringLiteral("tab-width"), 2);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("type"));

    session.setValue(QStringLiteral("tab-width"), 4);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("session"));

    view.setValue(QStringLiteral("tab-width"), 8);
    QCOMPARE(resolvedIdOf(settings, QStringLiteral("tab-width")), QStringLiteral("view"));

    // 空键永远解析不到任何一层。
    SettingScope found = SettingScope::View;
    QVERIFY(!settings.resolvedFromLayer(QString(), &found));
}

void TstSettingsScope::describeResolutionNamesTheLayerAndTheFactoryDefault()
{
    const SettingsSchema schema = syntheticSchema();
    MemorySessionSettings view;
    view.setValue(QStringLiteral("encoding"), QStringLiteral("utf-16"));

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::View, &view);
    settings.setSchema(&schema);

    // 「一眼看出当前改动的去向」在诊断上就是这一行：它必须说清值来自哪里。
    QVERIFY2(settings.describeResolution(QStringLiteral("encoding"))
                 .contains(settingScopeLabel(SettingScope::View)),
             qPrintable(settings.describeResolution(QStringLiteral("encoding"))));
    QVERIFY2(settings.describeResolution(QStringLiteral("tab-width"))
                 .contains(QStringLiteral("出厂默认")),
             qPrintable(settings.describeResolution(QStringLiteral("tab-width"))));
    QCOMPARE(settings.describeResolution(QStringLiteral("no-such-key")),
             QStringLiteral("未设置"));
}

void TstSettingsScope::changedFiresOnlyWhenTheEffectiveValueChanges()
{
    const SettingsSchema schema = syntheticSchema();
    MemorySessionSettings type;

    ScopedSessionSettings settings;
    settings.setLayer(SettingScope::Type, &type);
    settings.setSchema(&schema);
    settings.setWriteScope(SettingScope::Type);

    QSignalSpy spy(&settings, &SessionSettings::changed);

    // 把一项设成**与出厂默认相同**的值：这是一次真实的写入（类型层里多了一条），
    // 但用户看到的值没有变。侦听方（状态栏、脏标记）关心的是后者。
    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 4));
    QCOMPARE(spy.count(), 0);
    QVERIFY(type.contains(QStringLiteral("tab-width")));

    // 改成不同的值 → 恰好一条信号。
    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 8));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("tab-width"));

    // 同一个值再写一次 → 不发。
    QVERIFY(settings.setValue(QStringLiteral("tab-width"), 8));
    QCOMPARE(spy.count(), 1);

    // 删掉 → 有效值回到出厂默认 4，发一条。
    QVERIFY(settings.remove(QStringLiteral("tab-width")));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(settings.value(QStringLiteral("tab-width")).toInt(), 4);

    // 再删一次（目标层里已经没有了）→ 返回 false 且不发。
    QVERIFY(!settings.remove(QStringLiteral("tab-width")));
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(TstSettingsScope)
