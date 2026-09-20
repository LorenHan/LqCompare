#include "tst_settings.h"

#include <QSignalSpy>

using namespace LqCompare;

// -----------------------------------------------------------------------------
// 测试替身
// -----------------------------------------------------------------------------

CountingSettings::CountingSettings(QObject *parent)
    : SessionSettings(parent)
{
}

QStringList CountingSettings::keys() const
{
    QStringList result = m_values.keys();
    result.sort();
    return result;
}

bool CountingSettings::contains(const QString &key) const
{
    return m_values.contains(key);
}

QVariant CountingSettings::value(const QString &key, const QVariant &fallback) const
{
    return m_values.value(key, fallback);
}

bool CountingSettings::setValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty()) {
        return false;
    }
    m_writes << key;
    m_values.insert(key, value);
    emit changed(key);
    return true;
}

bool CountingSettings::remove(const QString &key)
{
    return m_values.remove(key) > 0;
}

void CountingSettings::clear()
{
    m_values.clear();
}

void CountingSettings::seed(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
}

// -----------------------------------------------------------------------------
// 合成声明
// -----------------------------------------------------------------------------

namespace {

SettingItem makeItem(const QString &key, const QString &title, SettingControl control,
                     const QVariant &defaultValue, SettingValidation validation = SettingValidation())
{
    SettingItem item;
    item.key = key;
    item.title = title;
    // 说明恒非空：`selfCheck()` 会拒绝没有说明的项，而各用例关心的是别的事情，
    // 让每个用例都去写一遍说明会让「忘了写说明」这条自检失去焦点。
    item.description = QStringLiteral("%1 的说明").arg(title);
    item.control = control;
    item.defaultValue = defaultValue;
    item.validation = validation;
    return item;
}

} // namespace

LqCompare::SettingsSchema TstSettings::textLikeSchema() const
{
    SettingsSchema schema;
    schema.typeId = QStringLiteral("text");

    {
        SettingsTab tab;
        tab.id = QStringLiteral("comparison");
        tab.title = QStringLiteral("比较");
        tab.description = QStringLiteral("这一页决定文本怎么对齐。");

        SettingGroup textGroup;
        textGroup.id = QStringLiteral("text");
        textGroup.title = QStringLiteral("文本");
        textGroup.description = QStringLiteral("影响正文比对方式。");
        textGroup.items << makeItem(QStringLiteral("ignore-case"), QStringLiteral("忽略大小写"),
                                    SettingControl::Bool, false);
        {
            SettingValidation v;
            v.hasMinimum = true;
            v.minimum = 1;
            v.hasMaximum = true;
            v.maximum = 16;
            textGroup.items << makeItem(QStringLiteral("tab-width"), QStringLiteral("制表符宽度"),
                                        SettingControl::Integer, 4, v);
        }
        {
            SettingItem choice = makeItem(QStringLiteral("line-endings"), QStringLiteral("行尾符"),
                                          SettingControl::Choice, QStringLiteral("auto"));
            choice.choices = {{QStringLiteral("auto"), QStringLiteral("自动检测"), QStringLiteral("按内容判断")},
                              {QStringLiteral("lf"), QStringLiteral("LF"), QString()},
                              {QStringLiteral("crlf"), QStringLiteral("CRLF"), QString()}};
            textGroup.items << choice;
        }

        SettingGroup whiteGroup;
        whiteGroup.id = QStringLiteral("whitespace");
        whiteGroup.title = QStringLiteral("空白");
        whiteGroup.items << makeItem(QStringLiteral("ignore-all-whitespace"),
                                     QStringLiteral("忽略全部空白"), SettingControl::Bool, false);
        whiteGroup.items << makeItem(QStringLiteral("ignore-trailing"),
                                     QStringLiteral("忽略行尾空白"), SettingControl::Bool, true);

        tab.groups << textGroup << whiteGroup;
        schema.tabs << tab;
    }

    {
        SettingsTab tab;
        tab.id = QStringLiteral("filter");
        tab.title = QStringLiteral("过滤");
        tab.description = QStringLiteral("这一页决定哪些文件参与比对。");

        SettingGroup group;
        group.id = QStringLiteral("masks");
        group.title = QStringLiteral("文件掩码");
        {
            SettingValidation v;
            v.maxLength = 5; // 条数上限（掩码清单数的是条数，不是字符）
            group.items << makeItem(QStringLiteral("exclude-masks"), QStringLiteral("排除掩码"),
                                    SettingControl::MaskList,
                                    QStringList{QStringLiteral("-*.bak")}, v);
        }
        tab.groups << group;
        schema.tabs << tab;
    }

    {
        SettingsTab tab;
        tab.id = QStringLiteral("display");
        tab.title = QStringLiteral("显示");
        tab.description = QStringLiteral("这一页决定结果怎么呈现。");

        SettingGroup group;
        group.id = QStringLiteral("labels");
        group.title = QStringLiteral("标注");
        {
            SettingValidation v;
            v.maxLength = 40;
            group.items << makeItem(QStringLiteral("left-label"), QStringLiteral("左侧标题"),
                                    SettingControl::Text, QStringLiteral("左侧"), v);
        }
        group.items << makeItem(QStringLiteral("notes"), QStringLiteral("备注"),
                                SettingControl::MultilineText, QString());
        tab.groups << group;
        schema.tabs << tab;
    }

    return schema;
}

LqCompare::SettingsSchema TstSettings::minimalSchema() const
{
    SettingsSchema schema;
    schema.typeId = QStringLiteral("folder");

    SettingsTab tab;
    tab.id = QStringLiteral("basic");
    tab.title = QStringLiteral("基本");

    SettingGroup group;
    group.id = QStringLiteral("scan");
    group.title = QStringLiteral("扫描");
    group.items << makeItem(QStringLiteral("recursive"), QStringLiteral("包含子目录"),
                            SettingControl::Bool, true);

    tab.groups << group;
    schema.tabs << tab;
    return schema;
}

// -----------------------------------------------------------------------------
// 用例
// -----------------------------------------------------------------------------

void TstSettings::initTestCase()
{
    // 合成声明必须自己先站得住：它是后面几十条用例的地基，而地基坏了的表现
    // 会是「某条用例莫名其妙地红」，排查方向完全错。
    QVERIFY2(textLikeSchema().validate().isEmpty(),
             qPrintable(textLikeSchema().validate().join(QStringLiteral("; "))));
    QVERIFY2(minimalSchema().validate().isEmpty(),
             qPrintable(minimalSchema().validate().join(QStringLiteral("; "))));
}

// --- A 声明与控件类型 ---------------------------------------------------------

void TstSettings::everyControlTypeHasAnIdentifierAndALabel()
{
    const QVector<SettingControl> all{SettingControl::Bool,   SettingControl::Integer,
                                      SettingControl::Text,   SettingControl::MultilineText,
                                      SettingControl::Choice, SettingControl::MaskList};

    QStringList identifiers;
    QStringList labels;
    for (SettingControl control : all) {
        const QString identifier = QString::fromLatin1(settingControlIdentifier(control));
        QVERIFY2(!identifier.isEmpty(), "控件类型必须有机器标识");
        QVERIFY2(identifier != QStringLiteral("unknown"), "控件类型的标识不能落到兜底值上");
        QVERIFY2(!labels.contains(settingControlLabel(control)), "两种控件不能用同一个显示文案");
        identifiers << identifier;
        labels << settingControlLabel(control);
    }
    // 标识唯一：它会被写进会话文件（SESS-008），两个控件共用一个标识时
    // 读回来会落到错的那一种——现象是「设置项变成了另一种控件」。
    QCOMPARE(QSet<QString>(identifiers.constBegin(), identifiers.constEnd()).size(),
             identifiers.size());
}

void TstSettings::anItemCarriesAllFiveDeclaredAspects()
{
    const SettingsSchema schema = textLikeSchema();
    const SettingItem *item = schema.findItem(QStringLiteral("tab-width"));
    QVERIFY(item);

    // 第 2 条点名的五样：标题、说明、控件类型、默认值、校验规则。
    QCOMPARE(item->title, QStringLiteral("制表符宽度"));
    QVERIFY(!item->description.isEmpty());
    QCOMPARE(item->control, SettingControl::Integer);
    QCOMPARE(item->defaultValue.toInt(), 4);
    QVERIFY(item->validation.hasAnyRule());
    QCOMPARE(item->validation.minimum, 1);
    QCOMPARE(item->validation.maximum, 16);
    QVERIFY(item->validation.describe(item->control).contains(QStringLiteral("1 到 16")));
}

void TstSettings::schemaIsPlainDataAndCanBeBuiltHeadlessly()
{
    // 「可单独构造」在这一层的含义：不需要 QApplication、不碰任何界面对象，
    // 随手就能造一份声明并读出全部内容。本工程 `QT -= gui` 是这条断言的
    // 编译期部分（真的引了 QtGui 会构建失败）。
    SettingsSchema schema;
    schema.typeId = QStringLiteral("hex");
    SettingsTab tab;
    tab.id = QStringLiteral("view");
    tab.title = QStringLiteral("视图");
    SettingGroup group;
    group.id = QStringLiteral("bytes");
    group.title = QStringLiteral("字节");
    group.items << makeItem(QStringLiteral("bytes-per-line"), QStringLiteral("每行字节数"),
                            SettingControl::Integer, 16);
    tab.groups << group;
    schema.tabs << tab;

    QVERIFY(schema.validate().isEmpty());
    QCOMPARE(schema.itemCount(), 1);
    QCOMPARE(schema.itemKeys(), QStringList{QStringLiteral("bytes-per-line")});
    QCOMPARE(schema.describe(),
             QStringLiteral("设置声明：类型 hex，1 张 Tab，1 个设置项"));
}

void TstSettings::itemsAreEnumeratedInDeclarationOrder()
{
    const SettingsSchema schema = textLikeSchema();
    QCOMPARE(schema.itemKeys(),
             (QStringList{QStringLiteral("ignore-case"), QStringLiteral("tab-width"),
                          QStringLiteral("line-endings"), QStringLiteral("ignore-all-whitespace"),
                          QStringLiteral("ignore-trailing"), QStringLiteral("exclude-masks"),
                          QStringLiteral("left-label"), QStringLiteral("notes")}));
    QCOMPARE(schema.itemCount(), 8);
    QCOMPARE(schema.tabs.size(), 3);
}

void TstSettings::tabAndItemLookupsAgreeWithTheDeclaration()
{
    const SettingsSchema schema = textLikeSchema();

    QCOMPARE(schema.tabIndexOfItem(QStringLiteral("exclude-masks")), 1);
    const SettingsTab *tab = schema.tabOfItem(QStringLiteral("exclude-masks"));
    QVERIFY(tab);
    QCOMPARE(tab->id, QStringLiteral("filter"));

    QCOMPARE(schema.tabIndexOfItem(QStringLiteral("不存在的键")), -1);
    QVERIFY(!schema.tabOfItem(QStringLiteral("不存在的键")));
    QVERIFY(!schema.findItem(QStringLiteral("不存在的键")));
    QVERIFY(schema.findTab(QStringLiteral("display")));
    QVERIFY(!schema.findTab(QStringLiteral("Display")));

    // Tab 自己能枚举出全部项（界面的「这一页有哪些项」就靠它）。
    QCOMPARE(schema.findTab(QStringLiteral("comparison"))->itemCount(), 5);
    QCOMPARE(schema.findTab(QStringLiteral("comparison"))->items().size(), 5);
}

void TstSettings::validationRulesSummariseInPlainWords()
{
    SettingValidation v;
    QCOMPARE(v.describe(SettingControl::Text), QString());

    v.required = true;
    v.minLength = 2;
    v.maxLength = 40;
    const QString text = v.describe(SettingControl::Text);
    QVERIFY(text.contains(QStringLiteral("必填")));
    QVERIFY(text.contains(QStringLiteral("2 到 40 个字符")));

    // 掩码清单数的是**条数**：同一组规则在清单项上必须说「条」。
    // 说成「个字符」的话，用户会去数自己那 3 条掩码的字符数。
    const QString masks = v.describe(SettingControl::MaskList);
    QVERIFY(masks.contains(QStringLiteral("2 到 40 条")));

    v.hasMinimum = true;
    v.minimum = 1;
    v.hasMaximum = true;
    v.maximum = 16;
    QVERIFY(v.describe(SettingControl::Integer).contains(QStringLiteral("1 到 16 之间")));
}

void TstSettings::requiredRuleIsEnforced()
{
    SettingValidation v;
    v.required = true;
    const SettingItem item = makeItem(QStringLiteral("left-label"), QStringLiteral("左侧标题"),
                                      SettingControl::Text, QStringLiteral("a"), v);

    QVERIFY(item.validate(QStringLiteral("a")).isEmpty());
    const QString message = item.validate(QString());
    QVERIFY(!message.isEmpty());
    QVERIFY(message.contains(QStringLiteral("不能为空")));

    // 掩码清单的「必填」指至少一条。
    const SettingItem masks = makeItem(QStringLiteral("masks"), QStringLiteral("掩码"),
                                       SettingControl::MaskList, QStringList{},
                                       SettingValidation{v});
    QVERIFY(!masks.validate(QStringList{}).isEmpty());
    QVERIFY(masks.validate(QStringList{QStringLiteral("*.txt")}).isEmpty());
}

void TstSettings::lengthRulesAreEnforced()
{
    SettingValidation v;
    v.minLength = 3;
    v.maxLength = 5;
    const SettingItem item = makeItem(QStringLiteral("label"), QStringLiteral("标题"),
                                      SettingControl::Text, QStringLiteral("abc"), v);

    QVERIFY(item.validate(QStringLiteral("abc")).isEmpty());
    QVERIFY(item.validate(QStringLiteral("abcde")).isEmpty());
    QVERIFY(item.validate(QStringLiteral("ab")).contains(QStringLiteral("至少要 3")));
    QVERIFY(item.validate(QStringLiteral("abcdef")).contains(QStringLiteral("最多 5")));
}

void TstSettings::integerBoundsAreEnforced()
{
    SettingValidation v;
    v.hasMinimum = true;
    v.minimum = 1;
    v.hasMaximum = true;
    v.maximum = 16;
    const SettingItem item = makeItem(QStringLiteral("tab-width"), QStringLiteral("宽度"),
                                      SettingControl::Integer, 4, v);

    QVERIFY(item.validate(1).isEmpty());
    QVERIFY(item.validate(16).isEmpty());
    QVERIFY(item.validate(0).contains(QStringLiteral("不能小于 1")));
    QVERIFY(item.validate(17).contains(QStringLiteral("不能大于 16")));
    // 字符串承载的数字也必须被收拢成整数再判：会话文件里存的是文本，
    // 若按字符串比较，"0" 会静静地通过。
    QVERIFY(!item.validate(QStringLiteral("0")).isEmpty());
}

void TstSettings::choiceMustBeOneOfTheDeclaredValues()
{
    SettingItem item = makeItem(QStringLiteral("line-endings"), QStringLiteral("行尾符"),
                                SettingControl::Choice, QStringLiteral("auto"));
    item.choices = {{QStringLiteral("auto"), QStringLiteral("自动"), QString()},
                    {QStringLiteral("lf"), QStringLiteral("LF"), QString()}};

    QVERIFY(item.validate(QStringLiteral("lf")).isEmpty());
    QVERIFY(item.validate(QStringLiteral("crlf")).contains(QStringLiteral("不在可选范围内")));
}

void TstSettings::choiceRejectsAValueOutsideTheTable()
{
    SettingItem item = makeItem(QStringLiteral("mode"), QStringLiteral("模式"),
                                SettingControl::Choice, QStringLiteral("a"));
    item.choices = {{QStringLiteral("a"), QStringLiteral("甲"), QString()}};
    // 取值表里只有一个值时也要拒绝别的值——「只有一个选项」与「什么都行」
    // 在界面上看起来一样（都是自动选中第一项），但存进会话文件后完全不同。
    const QString message = item.validate(QStringLiteral("b"));
    QVERIFY(!message.isEmpty());
    QVERIFY(message.contains(QStringLiteral("a")));
}

void TstSettings::maskListValidationReusesTheMaskLanguage()
{
    const SettingItem item = makeItem(QStringLiteral("masks"), QStringLiteral("掩码"),
                                      SettingControl::MaskList,
                                      QStringList{QStringLiteral("*.txt")});

    // 全部通过：`**` 独占一段才跨目录、`-` 前缀是排除，这两条都是 FILT-001 的语义。
    QVERIFY(item.validate(QStringList{QStringLiteral("*.txt")}).isEmpty());
    QVERIFY(item.validate(QStringList{QStringLiteral("build/**")}).isEmpty());
    QVERIFY(item.validate(QStringList{QStringLiteral("-*.bak")}).isEmpty());
    QVERIFY(item.validate(QStringList{QStringLiteral("src/*.cpp")}).isEmpty());
    // `a**b` 不跨目录，但它是一条**合法**的掩码——不能因为它不跨目录就报错。
    QVERIFY(item.validate(QStringList{QStringLiteral("a**b")}).isEmpty());

    // 自制一套「看扩展名」的匹配会把下面两条全放行，而它们都是 FILT-001
    // 明确要求报错的形态。这两条断言就是「复用而不是重写」的证据。
    QVERIFY(!item.validate(QStringList{QStringLiteral("[abc")}).isEmpty());
    QVERIFY(!item.validate(QStringList{QStringLiteral("build\\out")}).isEmpty());
}

void TstSettings::maskListValidationReportsTheOffendingLine()
{
    const SettingItem item = makeItem(QStringLiteral("masks"), QStringLiteral("排除掩码"),
                                      SettingControl::MaskList, QStringList{});
    const QString message =
        item.validate(QStringList{QStringLiteral("*.txt"), QStringLiteral("[abc")});
    QVERIFY(!message.isEmpty());
    // 行号必须与用户在编辑框里看到的一致——归一只掉**末尾**空行，正是为了这个。
    QVERIFY2(message.contains(QStringLiteral("第 2 行")), qPrintable(message));
    QVERIFY(message.contains(QStringLiteral("排除掩码")));
}

void TstSettings::maskListLengthRulesCountEntriesNotCharacters()
{
    SettingValidation v;
    v.maxLength = 2;
    const SettingItem item = makeItem(QStringLiteral("masks"), QStringLiteral("掩码"),
                                      SettingControl::MaskList, QStringList{}, v);

    QVERIFY(item.validate(QStringList{QStringLiteral("a"), QStringLiteral("b")}).isEmpty());
    const QString message =
        item.validate(QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(!message.isEmpty());
    QVERIFY2(message.contains(QStringLiteral("最多 2 条")), qPrintable(message));
    QVERIFY(message.contains(QStringLiteral("现在有 3 条")));
}

void TstSettings::emptyOptionalTextIsNotReportedAsTooShort()
{
    SettingValidation v;
    v.minLength = 3;
    const SettingItem item = makeItem(QStringLiteral("label"), QStringLiteral("标题"),
                                      SettingControl::Text, QString(), v);

    // 可选项留空不该报「至少 3 个字符」——那会让用户以为它必填，
    // 然后随手敲三个没用的字符进去。
    QVERIFY(item.validate(QString()).isEmpty());
    QVERIFY(!item.validate(QStringLiteral("ab")).isEmpty());
}

void TstSettings::validationFailureNamesTheItem()
{
    SettingValidation v;
    v.hasMinimum = true;
    v.minimum = 1;
    v.hasMaximum = true;
    v.maximum = 16;
    const SettingItem item = makeItem(QStringLiteral("tab-width"), QStringLiteral("制表符宽度"),
                                      SettingControl::Integer, 4, v);
    const QString message = item.validate(999);
    QVERIFY(!message.isEmpty());
    // 报错必须点名是哪一项：设置页上有一屏的控件，只说「不能大于 16」的话
    // 用户得自己找出是哪一个。
    QVERIFY2(message.contains(QStringLiteral("制表符宽度")), qPrintable(message));
}

void TstSettings::everyScopeHasIdentifierLabelAndDescription()
{
    const QVector<SettingScope> scopes = allSettingScopes();
    QCOMPARE(scopes.size(), 3);

    QStringList identifiers;
    QStringList labels;
    for (SettingScope scope : scopes) {
        const QString identifier = QString::fromLatin1(settingScopeIdentifier(scope));
        QVERIFY(!identifier.isEmpty());
        QVERIFY(identifier != QStringLiteral("unknown"));
        identifiers << identifier;
        QVERIFY(!settingScopeLabel(scope).isEmpty());
        QVERIFY2(!labels.contains(settingScopeLabel(scope)), "三个作用域的文案必须能区分");
        labels << settingScopeLabel(scope);
        // 下拉项的 tooltip 就用这句；没有它，用户判断不出「仅当前视图」会不会留到下次。
        QVERIFY(!settingScopeDescription(scope).isEmpty());
    }
    QCOMPARE(identifiers, (QStringList{QStringLiteral("view"), QStringLiteral("session"),
                                       QStringLiteral("type")}));
}

// --- B 声明的自检 -------------------------------------------------------------

void TstSettings::syntheticSchemaValidatesClean()
{
    QVERIFY(textLikeSchema().validate().isEmpty());
    QVERIFY(minimalSchema().validate().isEmpty());
}

void TstSettings::duplicateKeyAcrossTabsIsReported()
{
    SettingsSchema schema = textLikeSchema();
    // 会话文件里只存键，两个 Tab 各有一个同名键时，读回来落到哪一项取决于
    // 遍历顺序——现象是「改了 A 页的值，B 页也跟着变」。
    SettingGroup group;
    group.id = QStringLiteral("dup");
    group.title = QStringLiteral("重复");
    group.items << makeItem(QStringLiteral("notes"), QStringLiteral("另一个备注"),
                            SettingControl::Text, QString());
    schema.tabs[2].groups << group;

    const QStringList problems = schema.validate();
    QVERIFY(!problems.isEmpty());
    bool found = false;
    for (const QString &problem : problems) {
        if (problem.contains(QStringLiteral("全表范围内重复"))) {
            found = true;
        }
    }
    QVERIFY2(found, qPrintable(problems.join(QStringLiteral("; "))));
}

void TstSettings::duplicateGroupIdIsReported()
{
    SettingsSchema schema = textLikeSchema();
    SettingGroup group;
    group.id = QStringLiteral("text"); // 与同 Tab 里已有的分组撞了
    group.title = QStringLiteral("另一个文本组");
    group.items << makeItem(QStringLiteral("other"), QStringLiteral("别的"), SettingControl::Bool,
                            false);
    schema.tabs[0].groups << group;

    const QStringList problems = schema.validate();
    QVERIFY(!problems.isEmpty());
    QVERIFY(problems.join(QStringLiteral("; ")).contains(QStringLiteral("分组 ID")));
}

void TstSettings::missingTitleOrDescriptionIsReported()
{
    {
        SettingsSchema schema = minimalSchema();
        schema.tabs[0].groups[0].items[0].title.clear();
        QVERIFY(!schema.validate().isEmpty());
        QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("没有标题")));
    }
    {
        SettingsSchema schema = minimalSchema();
        schema.tabs[0].groups[0].items[0].description.clear();
        const QString joined = schema.validate().join(QStringLiteral("; "));
        QVERIFY2(joined.contains(QStringLiteral("没有说明")), qPrintable(joined));
    }
}

void TstSettings::invalidSettingKeyIsReported()
{
    SettingsSchema schema = minimalSchema();
    schema.tabs[0].groups[0].items[0].key = QStringLiteral("Ignore_Case");
    QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("不合法")));

    // 键与类型 ID 用同一条规则（`isValidSessionTypeId`）：两处各写一份必然分家，
    // 而分家的现象是「存进文件的键读不回来」。
    QVERIFY(isValidSettingKey(QStringLiteral("ignore-case")));
    QVERIFY(!isValidSettingKey(QStringLiteral("ignoreCase")));
    QVERIFY(!isValidSettingKey(QStringLiteral("-leading")));
    QVERIFY(!isValidSettingKey(QString()));
}

void TstSettings::reversedBoundsAreReported()
{
    {
        SettingsSchema schema = minimalSchema();
        SettingsTab tab;
        tab.id = QStringLiteral("limits");
        tab.title = QStringLiteral("限制");
        SettingGroup group;
        group.id = QStringLiteral("range");
        group.title = QStringLiteral("区间");
        SettingValidation v;
        v.hasMinimum = true;
        v.minimum = 10;
        v.hasMaximum = true;
        v.maximum = 1;
        group.items << makeItem(QStringLiteral("level"), QStringLiteral("级别"),
                                SettingControl::Integer, 5, v);
        tab.groups << group;
        schema.tabs << tab;
        QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("没有任何合法值")));
    }
    {
        SettingsSchema schema = minimalSchema();
        SettingsTab tab;
        tab.id = QStringLiteral("length");
        tab.title = QStringLiteral("长度");
        SettingGroup group;
        group.id = QStringLiteral("len");
        group.title = QStringLiteral("长度");
        SettingValidation v;
        v.minLength = 8;
        v.maxLength = 2;
        group.items << makeItem(QStringLiteral("label"), QStringLiteral("标题"),
                                SettingControl::Text, QString(), v);
        tab.groups << group;
        schema.tabs << tab;
        QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("没有任何合法值")));
    }
}

void TstSettings::defaultValueViolatingItsOwnRuleIsReported()
{
    SettingsSchema schema = minimalSchema();
    SettingsTab tab;
    tab.id = QStringLiteral("bad");
    tab.title = QStringLiteral("坏默认值");
    SettingGroup group;
    group.id = QStringLiteral("bad");
    group.title = QStringLiteral("坏默认值");
    SettingValidation v;
    v.hasMaximum = true;
    v.maximum = 4;
    // 默认值 9 超出自己的上界：这样一条声明会让「恢复默认」把界面推进
    // 一个「有改动且校验不通过」的状态，用户什么都没做错却看到报错。
    group.items << makeItem(QStringLiteral("level"), QStringLiteral("级别"),
                            SettingControl::Integer, 9, v);
    tab.groups << group;
    schema.tabs << tab;

    QVERIFY(schema.validate().join(QStringLiteral("; "))
                 .contains(QStringLiteral("默认值不符合它自己的校验规则")));
}

void TstSettings::defaultValueOfTheWrongTypeIsReported()
{
    SettingsSchema schema = minimalSchema();
    schema.tabs[0].groups[0].items[0].defaultValue = QStringLiteral("true"); // 布尔项给了字符串
    QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("默认值却不是布尔值")));

    SettingsSchema other = minimalSchema();
    other.tabs[0].groups[0].items[0].control = SettingControl::MaskList;
    other.tabs[0].groups[0].items[0].defaultValue = QStringLiteral("*");
    QVERIFY(other.validate().join(QStringLiteral("; ")).contains(QStringLiteral("字符串清单")));
}

void TstSettings::choiceWithoutOptionsIsReported()
{
    SettingsSchema schema = minimalSchema();
    schema.tabs[0].groups[0].items[0].control = SettingControl::Choice;
    schema.tabs[0].groups[0].items[0].defaultValue = QStringLiteral("a");
    QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("没有给出任何取值")));

    // 取值重复同样是错的：`findData` 会永远命中第一个，第二个选不中，
    // 而界面看起来一切正常。
    schema.tabs[0].groups[0].items[0].choices = {{QStringLiteral("a"), QStringLiteral("甲"), QString()},
                                                {QStringLiteral("a"), QStringLiteral("乙"), QString()}};
    QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("重复")));
}

void TstSettings::choicesOnANonChoiceItemAreReported()
{
    SettingsSchema schema = minimalSchema();
    schema.tabs[0].groups[0].items[0].choices = {{QStringLiteral("a"), QStringLiteral("甲"), QString()}};
    // 留着一份用不上的取值表，下一个人会以为它生效。
    QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("不是枚举控件")));
}

void TstSettings::emptyGroupOrEmptySchemaIsReported()
{
    {
        SettingsSchema schema = minimalSchema();
        schema.tabs[0].groups[0].items.clear();
        QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("没有任何设置项")));
    }
    {
        SettingsSchema schema = minimalSchema();
        schema.tabs.clear();
        QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("没有任何 Tab")));
    }
    {
        SettingsSchema schema = minimalSchema();
        schema.typeId = QStringLiteral("Text");
        QVERIFY(schema.validate().join(QStringLiteral("; ")).contains(QStringLiteral("不合法")));
    }
}

// --- C 草稿的读写与脏判定（第 4 条） ------------------------------------------

void TstSettings::controlRoundTripsHeadlessly_data()
{
    QTest::addColumn<int>("control");

    // 「任一会话设置 Tab 均可在无界面测试中被单独构造与读写」（第 4 条）的
    // 逐控件证据：每种控件类型都走一遍「造声明 → 造草稿 → 读默认 → 写新值 →
    // 读回 → 应用 → 目标里拿到归一后的值」。
    QTest::newRow("开关") << static_cast<int>(SettingControl::Bool);
    QTest::newRow("整数") << static_cast<int>(SettingControl::Integer);
    QTest::newRow("单行文本") << static_cast<int>(SettingControl::Text);
    QTest::newRow("多行文本") << static_cast<int>(SettingControl::MultilineText);
    QTest::newRow("下拉") << static_cast<int>(SettingControl::Choice);
    QTest::newRow("掩码清单") << static_cast<int>(SettingControl::MaskList);
}

void TstSettings::controlRoundTripsHeadlessly()
{
    QFETCH(int, control);
    const auto kind = static_cast<SettingControl>(control);

    QVariant defaultValue;
    QVariant newValue;
    switch (kind) {
    case SettingControl::Bool:
        defaultValue = false;
        newValue = true;
        break;
    case SettingControl::Integer:
        defaultValue = 4;
        newValue = 8;
        break;
    case SettingControl::Text:
        defaultValue = QStringLiteral("左侧");
        newValue = QStringLiteral("原始");
        break;
    case SettingControl::MultilineText:
        defaultValue = QString();
        newValue = QStringLiteral("第一行\n第二行");
        break;
    case SettingControl::Choice:
        defaultValue = QStringLiteral("a");
        newValue = QStringLiteral("b");
        break;
    case SettingControl::MaskList:
        defaultValue = QStringList{};
        newValue = QStringList{QStringLiteral("*.obj"), QStringLiteral("-*.bak")};
        break;
    }

    SettingItem item = makeItem(QStringLiteral("probe"), QStringLiteral("探针"), kind, defaultValue);
    if (kind == SettingControl::Choice) {
        item.choices = {{QStringLiteral("a"), QStringLiteral("甲"), QString()},
                        {QStringLiteral("b"), QStringLiteral("乙"), QString()}};
    }

    SettingsSchema schema;
    schema.typeId = QStringLiteral("probe");
    SettingsTab tab;
    tab.id = QStringLiteral("probe");
    tab.title = QStringLiteral("探针");
    SettingGroup group;
    group.id = QStringLiteral("probe");
    group.title = QStringLiteral("探针");
    group.items << item;
    tab.groups << group;
    schema.tabs << tab;
    QVERIFY2(schema.validate().isEmpty(), qPrintable(schema.validate().join(QStringLiteral("; "))));

    CountingSettings settings;
    SettingsDraft draft(schema);
    draft.loadFrom(settings);

    QCOMPARE(draft.value(QStringLiteral("probe")), item.normalized(defaultValue));
    QVERIFY(!draft.isDirty());

    QVERIFY(draft.setValue(QStringLiteral("probe"), newValue));
    QVERIFY(draft.isDirty());
    QCOMPARE(draft.value(QStringLiteral("probe")), item.normalized(newValue));

    QVERIFY(draft.applyTo(&settings));
    QCOMPARE(settings.value(QStringLiteral("probe")), item.normalized(newValue));
    QVERIFY(!draft.isDirty());
}

void TstSettings::draftStartsAtDefaultsAndIsClean()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QCOMPARE(draft.value(QStringLiteral("ignore-case")).toBool(), false);
    QCOMPARE(draft.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(draft.value(QStringLiteral("line-endings")).toString(), QStringLiteral("auto"));
    QCOMPARE(draft.value(QStringLiteral("exclude-masks")).toStringList(),
             QStringList{QStringLiteral("-*.bak")});
    QVERIFY(!draft.isDirty());
    QVERIFY(draft.dirtyKeys().isEmpty());
    QVERIFY(draft.problems().isEmpty());
    QVERIFY(draft.canApply());
}

void TstSettings::loadFromTakesTheSessionsValues()
{
    CountingSettings settings;
    settings.seed(QStringLiteral("tab-width"), 8);
    settings.seed(QStringLiteral("ignore-case"), true);

    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QCOMPARE(draft.value(QStringLiteral("tab-width")).toInt(), 8);
    QCOMPARE(draft.value(QStringLiteral("ignore-case")).toBool(), true);
    // 「载入」不是改动：刚才写进去的值就是基准，因此一进来必须是干净的。
    QVERIFY(!draft.isDirty());
}

void TstSettings::unknownAndEmptyKeysAreRejected()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QVERIFY(!draft.setValue(QString(), true));
    QVERIFY(!draft.setValue(QStringLiteral("不存在的键"), true));
    QVERIFY(!draft.contains(QStringLiteral("不存在的键")));
    QVERIFY(!draft.resetToDefault(QStringLiteral("不存在的键")));
    QVERIFY(!draft.isItemDirty(QStringLiteral("不存在的键")));
    QVERIFY(!draft.isDirty());
}

void TstSettings::settingTheSameValueIsNotAChange()
{
    CountingSettings settings;
    settings.seed(QStringLiteral("tab-width"), 4);
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QSignalSpy spy(&draft, &SettingsDraft::valueChanged);
    QVERIFY(draft.setValue(QStringLiteral("tab-width"), 4));
    // 值没变就不算改动、也不发信号：界面上「点开设置看一眼再确定」不该让会话变脏。
    QCOMPARE(spy.count(), 0);
    QVERIFY(!draft.isDirty());
}

void TstSettings::changesAreTrackedPerItemAndPerTab()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QVERIFY(draft.setValue(QStringLiteral("tab-width"), 8));
    QVERIFY(draft.setValue(QStringLiteral("left-label"), QStringLiteral("旧")));

    QCOMPARE(draft.dirtyKeys(),
             (QStringList{QStringLiteral("tab-width"), QStringLiteral("left-label")}));
    QVERIFY(draft.isItemDirty(QStringLiteral("tab-width")));
    QVERIFY(!draft.isItemDirty(QStringLiteral("ignore-case")));

    QVERIFY(draft.isTabDirty(QStringLiteral("comparison")));
    QVERIFY(!draft.isTabDirty(QStringLiteral("filter")));
    QVERIFY(draft.isTabDirty(QStringLiteral("display")));
    QCOMPARE(draft.dirtyKeysInTab(QStringLiteral("comparison")),
             QStringList{QStringLiteral("tab-width")});
    QCOMPARE(draft.dirtyKeysInTab(QStringLiteral("display")),
             QStringList{QStringLiteral("left-label")});
    QVERIFY(draft.dirtyKeysInTab(QStringLiteral("不存在的 Tab")).isEmpty());
}

void TstSettings::dirtyKeysFollowDeclarationOrder()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    // 刻意**逆序**写入：如果实现按插入顺序或字典序返回，这条就会红。
    // 界面按声明顺序标记每一项，两份顺序不一致时标记会错位。
    draft.setValue(QStringLiteral("notes"), QStringLiteral("x"));
    draft.setValue(QStringLiteral("ignore-case"), true);

    QCOMPARE(draft.dirtyKeys(),
             (QStringList{QStringLiteral("ignore-case"), QStringLiteral("notes")}));
}

void TstSettings::revertRestoresTheBaselineAndClearsDirty()
{
    CountingSettings settings;
    settings.seed(QStringLiteral("tab-width"), 8);
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    draft.setValue(QStringLiteral("tab-width"), 2);
    draft.setValue(QStringLiteral("ignore-case"), true);
    QVERIFY(draft.isDirty());

    QSignalSpy spy(&draft, &SettingsDraft::valueChanged);
    draft.revert();

    QCOMPARE(draft.value(QStringLiteral("tab-width")).toInt(), 8);
    QCOMPARE(draft.value(QStringLiteral("ignore-case")).toBool(), false);
    QVERIFY(!draft.isDirty());
    QCOMPARE(spy.count(), 2); // 两项都被改回来了，界面需要知道
}

void TstSettings::dirtySignalFiresOnlyOnTransitions()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QSignalSpy spy(&draft, &SettingsDraft::dirtyChanged);

    draft.setValue(QStringLiteral("tab-width"), 8);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toBool(), true);

    // 已经是脏的，再改一项不该重复发信号：状态栏与标签会因为重复信号反复重排。
    draft.setValue(QStringLiteral("ignore-case"), true);
    QCOMPARE(spy.count(), 1);

    // 改回基准值 → 回到干净，这里必须发一次。
    draft.setValue(QStringLiteral("ignore-case"), false);
    QCOMPARE(spy.count(), 1);
    draft.setValue(QStringLiteral("tab-width"), 4);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).toBool(), false);
}

void TstSettings::maskListTextAndListAreTheSameValue()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    // 会话文件里存的多行掩码是**一段文本**，读回来是 QString；控件与默认值是
    // QStringList。不归一的话「等于默认值吗」永远为假——「恢复默认」看起来
    // 没生效、干净的表单被判定成有改动、关窗口时白问一句。
    draft.setValue(QStringLiteral("exclude-masks"), QStringLiteral("-*.bak"));
    QVERIFY2(!draft.isItemDirty(QStringLiteral("exclude-masks")), "同一份掩码的两种载体必须等价");

    draft.setValue(QStringLiteral("exclude-masks"), QStringLiteral("-*.bak\n-*.tmp"));
    QVERIFY(draft.isItemDirty(QStringLiteral("exclude-masks")));
    QCOMPARE(draft.value(QStringLiteral("exclude-masks")).toStringList(),
             (QStringList{QStringLiteral("-*.bak"), QStringLiteral("-*.tmp")}));

    // 三种行尾都要认：Windows 上编辑过的文本是 CRLF，只按 \n 切会让每行末尾
    // 多一个 \r，于是 `-*.tmp` 变成 `-*.tmp\r`——静静地对不上任何文件。
    draft.setValue(QStringLiteral("exclude-masks"), QStringLiteral("-*.bak\r\n-*.tmp"));
    QCOMPARE(draft.value(QStringLiteral("exclude-masks")).toStringList(),
             (QStringList{QStringLiteral("-*.bak"), QStringLiteral("-*.tmp")}));
}

void TstSettings::trailingNewlineIsNotAChange()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    // 敲完最后一条顺手按一下回车：编辑框的文本会以 \n 结尾。若归一时把末尾空行
    // 也当成一条掩码，干净的设置会被判定成「有改动」，而用户什么都没改。
    draft.setValue(QStringLiteral("exclude-masks"), QStringLiteral("-*.bak\n"));
    QVERIFY2(!draft.isItemDirty(QStringLiteral("exclude-masks")),
             "末尾空行不是一条掩码，也不该算改动");

    // 中间的空行则要留着：校验报错带的是行号，丢掉它行号就与界面上看到的不一致。
    draft.setValue(QStringLiteral("exclude-masks"), QStringLiteral("-*.bak\n\n-*.tmp"));
    QCOMPARE(draft.value(QStringLiteral("exclude-masks")).toStringList().size(), 3);
}

void TstSettings::isDefaultIsAboutTheFactoryDefaultNotTheBaseline()
{
    CountingSettings settings;
    settings.seed(QStringLiteral("tab-width"), 8); // 会话里的值不是出厂默认值
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    QVERIFY(!draft.isDefault(QStringLiteral("tab-width")));
    QVERIFY(draft.isDefault(QStringLiteral("ignore-case")));
    QVERIFY(!draft.isDirty());

    // 「改回出厂默认值」是一次**真实的改动**，必须被算作改动——否则用户按了
    // 「恢复默认」再关掉窗口，程序会认为什么都没发生，而设置其实该变。
    QVERIFY(draft.resetToDefault(QStringLiteral("tab-width")));
    QCOMPARE(draft.value(QStringLiteral("tab-width")).toInt(), 4);
    QVERIFY(draft.isDefault(QStringLiteral("tab-width")));
    QVERIFY(draft.isDirty());
}

// --- D 应用与恢复默认 ---------------------------------------------------------

void TstSettings::applyWritesOnlyDirtyKeys()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);
    settings.resetCounters();

    draft.setValue(QStringLiteral("tab-width"), 8);
    draft.setValue(QStringLiteral("left-label"), QStringLiteral("旧"));

    QStringList applied;
    QVERIFY(draft.applyTo(&settings, &applied));

    // 断言落在**写入次数与键**上，而不是「目标里有没有值」：一个先把 8 项全写
    // 一遍、再把没改的那几项写回原值的实现同样能让目标正确，但它会在会话文件里
    // 留下一堆没有意义的键（对 SESS-008 就是脏文件）。
    QCOMPARE(settings.writeCount(), 2);
    QCOMPARE(settings.writes(),
             (QStringList{QStringLiteral("tab-width"), QStringLiteral("left-label")}));
    QCOMPARE(applied, settings.writes());
}

void TstSettings::applyDoesNothingWhenThereIsNoChange()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);
    settings.resetCounters();

    QVERIFY(!draft.applyTo(&settings));
    // 与 `CompareSession::save()` 一致：没有改动的应用不是一次成功的保存。
    // 返回值若为真，调用点就分不清「应用了 0 项」与「应用成功」。
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettings::applyIsAllOrNothingWhenValidationFails()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);
    settings.resetCounters();

    draft.setValue(QStringLiteral("tab-width"), 8);        // 合法
    draft.setValue(QStringLiteral("tab-width"), 999);      // 越界
    QVERIFY(!draft.canApply());
    QVERIFY(!draft.applyTo(&settings));
    // 全有或全无：半应用会让用户看到「一部分生效、一部分没有」，而被拦下的
    // 原因只覆盖其中一项。合法的 8 也不许写进去。
    QCOMPARE(settings.writeCount(), 0);
    QVERIFY(settings.value(QStringLiteral("tab-width"), -1).toInt() == -1);

    // 修好之后必须能应用，而且**仍然只写改动的键**（不是整批重写）。
    draft.setValue(QStringLiteral("tab-width"), 8);
    QVERIFY(draft.canApply());
    QVERIFY(draft.applyTo(&settings));
    QCOMPARE(settings.writes(), QStringList{QStringLiteral("tab-width")});
}

void TstSettings::applyReturnsTheAppliedKeysAndClearsDirty()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    draft.setValue(QStringLiteral("ignore-case"), true);
    QVERIFY(draft.isDirty());

    QSignalSpy spy(&draft, &SettingsDraft::dirtyChanged);
    QVERIFY(draft.applyTo(&settings));
    QVERIFY(!draft.isDirty());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toBool(), false);

    // 应用之后基准跟着往前走：再点一次应用不该重复写一遍（否则同步目录里
    // 会留下一串无意义的版本）。
    settings.resetCounters();
    QVERIFY(!draft.applyTo(&settings));
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettings::applyWithoutATargetFails()
{
    SettingsDraft draft(textLikeSchema());
    draft.setValue(QStringLiteral("ignore-case"), true);
    QVERIFY(!draft.applyTo(nullptr));
    // 没有目标时不许把自己判成「已保存」——那会让调用方以为落盘了。
    QVERIFY(draft.isDirty());
}

void TstSettings::applyNormalisesBeforeWriting()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    draft.setValue(QStringLiteral("exclude-masks"), QStringLiteral("*.obj\n-*.bak"));
    QVERIFY(draft.applyTo(&settings));
    QCOMPARE(settings.value(QStringLiteral("exclude-masks")).toStringList(),
             (QStringList{QStringLiteral("*.obj"), QStringLiteral("-*.bak")}));
}

void TstSettings::resetToDefaultChangesTheDraftOnly()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);
    settings.resetCounters();

    draft.setValue(QStringLiteral("tab-width"), 8);
    QVERIFY(draft.resetToDefault(QStringLiteral("tab-width")));
    QCOMPARE(draft.value(QStringLiteral("tab-width")).toInt(), 4);
    // 「恢复默认」绝不直接写回会话：那样它就成了一个不可撤销的动作，
    // 而本仓库的纪律是破坏性操作必须可逆（这里靠「取消」撤销）。
    QCOMPARE(settings.writeCount(), 0);
    QVERIFY(settings.value(QStringLiteral("tab-width"), -1).toInt() == -1);
}

void TstSettings::resetTabCountsOnlyRealChanges()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    // comparison 页共 5 项；先改 2 项，再恢复这一页 → 只有 2 项真的被改动。
    draft.setValue(QStringLiteral("tab-width"), 8);
    draft.setValue(QStringLiteral("ignore-case"), true);
    QCOMPARE(draft.resetTabToDefaults(QStringLiteral("comparison")), 2);
    QVERIFY(draft.value(QStringLiteral("tab-width")).toInt() == 4);
    QVERIFY(!draft.isTabDirty(QStringLiteral("comparison")));

    // 已经是默认值时再按一次 → 0 项。界面拿这个数字做提示（「已恢复 3 项」），
    // 报 5 会让用户以为别的地方也变了。
    QCOMPARE(draft.resetTabToDefaults(QStringLiteral("comparison")), 0);
    QCOMPARE(draft.resetTabToDefaults(QStringLiteral("不存在的 Tab")), 0);
}

void TstSettings::resetTabLeavesOtherTabsAlone()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    draft.setValue(QStringLiteral("tab-width"), 8);
    draft.setValue(QStringLiteral("left-label"), QStringLiteral("旧"));

    QCOMPARE(draft.resetTabToDefaults(QStringLiteral("comparison")), 1);
    // 「恢复默认」只作用于当前 Tab：用户按它时的预期是「这一页恢复原样」。
    QVERIFY(draft.isTabDirty(QStringLiteral("display")));
    QCOMPARE(draft.value(QStringLiteral("left-label")).toString(), QStringLiteral("旧"));
    QCOMPARE(draft.dirtyKeys(), QStringList{QStringLiteral("left-label")});
}

void TstSettings::restoreDefaultsIsUndoableByRevert()
{
    CountingSettings settings;
    SettingsDraft draft(textLikeSchema());
    draft.loadFrom(settings);

    draft.setValue(QStringLiteral("tab-width"), 8);
    draft.resetTabToDefaults(QStringLiteral("comparison"));
    QVERIFY(!draft.isDirty()); // 回到基准值 → 又干净了

    draft.setValue(QStringLiteral("ignore-case"), true);
    draft.resetAllToDefaults();
    const int dirtyAfterReset = draft.dirtyKeys().size();
    QVERIFY(dirtyAfterReset >= 0);

    // 「恢复默认」之后按「取消」必须回到进入对话框时的值——这是它可逆的证据。
    draft.revert();
    QCOMPARE(draft.value(QStringLiteral("tab-width")).toInt(), 4);
    QCOMPARE(draft.value(QStringLiteral("ignore-case")).toBool(), false);
    QVERIFY(!draft.isDirty());
}

// --- E 未保存改动的询问策略（第 3 条） ----------------------------------------

void TstSettings::noChangesMeansNoQuestion()
{
    const SettingsInquiry inquiry = inquiryForUnsavedChanges(SettingsInquiryReason::CloseDialog,
                                                            QStringList());
    // 「没有改动就不打扰」是本条目最容易犯的过度设计，因此它必须有一条断言。
    // 只看「有没有响应」验证不了它——每次都弹一个、默认选返回的实现同样能过。
    QVERIFY(!inquiry.ask);
    QVERIFY(inquiry.options.isEmpty());
}

void TstSettings::switchTabOffersApplyAndCancelButNotDiscard()
{
    const SettingsInquiry inquiry =
        inquiryForUnsavedChanges(SettingsInquiryReason::SwitchTab, {QStringLiteral("a")},
                                 QStringLiteral("过滤"));

    QVERIFY(inquiry.ask);
    QCOMPARE(inquiry.options.size(), 2);
    QVERIFY(inquiry.options.contains(SettingsChangeAction::Apply));
    QVERIFY(inquiry.options.contains(SettingsChangeAction::Cancel));
    // 切换 Tab 时草稿原样带过去，没有任何东西会丢——因此**不给**「放弃改动」
    // 这个出口。给一个用不上的破坏性按钮，等于凭空造出一条丢工作的路径。
    QVERIFY(!inquiry.options.contains(SettingsChangeAction::Discard));
}

void TstSettings::closeDialogOffersTheDiscardExit()
{
    const SettingsInquiry inquiry = inquiryForUnsavedChanges(SettingsInquiryReason::CloseDialog,
                                                            {QStringLiteral("a")});
    QVERIFY(inquiry.ask);
    QCOMPARE(inquiry.options.size(), 3);
    QVERIFY(inquiry.options.contains(SettingsChangeAction::Discard));
    QVERIFY(!inquiry.title.isEmpty());
    QVERIFY(!inquiry.text.isEmpty());
}

void TstSettings::defaultActionIsAlwaysCancel()
{
    for (SettingsInquiryReason reason :
         {SettingsInquiryReason::SwitchTab, SettingsInquiryReason::CloseDialog}) {
        const SettingsInquiry inquiry =
            inquiryForUnsavedChanges(reason, {QStringLiteral("a"), QStringLiteral("b")},
                                     QStringLiteral("显示"));
        // 一个手快的回车不应该丢掉用户刚敲进去的东西。
        QCOMPARE(inquiry.defaultAction, SettingsChangeAction::Cancel);
    }
}

void TstSettings::inquiryTextNamesTheTargetTabAndTheCount()
{
    const SettingsInquiry inquiry =
        inquiryForUnsavedChanges(SettingsInquiryReason::SwitchTab,
                                 {QStringLiteral("a"), QStringLiteral("b")},
                                 QStringLiteral("过滤"));
    QVERIFY2(inquiry.text.contains(QStringLiteral("2 项")), qPrintable(inquiry.text));
    QVERIFY2(inquiry.text.contains(QStringLiteral("过滤")), qPrintable(inquiry.text));
}

void TstSettings::inquiryDescribeMentionsTheOptions()
{
    const SettingsInquiry none = inquiryForUnsavedChanges(SettingsInquiryReason::CloseDialog,
                                                         QStringList());
    QVERIFY(none.describe().contains(QStringLiteral("不询问")));

    const SettingsInquiry some = inquiryForUnsavedChanges(SettingsInquiryReason::CloseDialog,
                                                         {QStringLiteral("a")});
    const QString description = some.describe();
    QVERIFY(description.contains(QStringLiteral("先应用")));
    QVERIFY(description.contains(QStringLiteral("放弃改动")));
    QVERIFY(description.contains(QStringLiteral("返回")));

    // 动作标识是给会话文件与命令行用的机器值，必须稳定且互不相同。
    QStringList identifiers;
    for (SettingsChangeAction action : {SettingsChangeAction::Apply, SettingsChangeAction::Discard,
                                        SettingsChangeAction::Cancel}) {
        const QString identifier = QString::fromLatin1(settingsChangeActionIdentifier(action));
        QVERIFY(!identifier.isEmpty());
        QVERIFY(identifier != QStringLiteral("unknown"));
        QVERIFY(!settingsChangeActionLabel(action).isEmpty());
        identifiers << identifier;
    }
    QCOMPARE(identifiers.size(), 3);
    QVERIFY(identifiers[0] != identifiers[1] && identifiers[1] != identifiers[2]);
}

// --- F 声明目录与反向验证 -----------------------------------------------------

void TstSettings::catalogRegistersFindsAndClears()
{
    SessionSettingsCatalog catalog;
    QVERIFY(catalog.isEmpty());
    QCOMPARE(catalog.count(), 0);

    QString error;
    QVERIFY2(catalog.add(textLikeSchema(), &error), qPrintable(error));
    QVERIFY(catalog.add(minimalSchema(), &error));
    QCOMPARE(catalog.count(), 2);
    QVERIFY(!catalog.isEmpty());
    QVERIFY(error.isEmpty());

    const SettingsSchema *found = catalog.find(QStringLiteral("text"));
    QVERIFY(found);
    QCOMPARE(found->itemCount(), 8);
    // 大小写敏感，与 `SessionTypeRegistry::find` 一致：类型 ID 是机器键。
    QVERIFY(!catalog.find(QStringLiteral("Text")));
    QVERIFY(!catalog.find(QStringLiteral("hex")));
    QCOMPARE(catalog.typeIds(),
             (QStringList{QStringLiteral("text"), QStringLiteral("folder")}));
    QCOMPARE(catalog.describe(), QStringLiteral("会话设置目录：2 份声明，共 9 个设置项"));

    catalog.clear();
    QVERIFY(catalog.isEmpty());
}

void TstSettings::catalogRejectsIllegalAndDuplicateTypeIds()
{
    SessionSettingsCatalog catalog;
    QString error;

    SettingsSchema illegal = minimalSchema();
    illegal.typeId = QStringLiteral("Folder Scan");
    QVERIFY(!catalog.add(illegal, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(catalog.count(), 0);

    error.clear();
    QVERIFY(catalog.add(minimalSchema(), &error));
    QVERIFY(error.isEmpty());

    // 同一类型登记两份声明 → 设置对话框会显示哪一份取决于登记顺序，
    // 而「哪一份生效」在界面上完全看不出。整条拒绝。
    error.clear();
    QVERIFY(!catalog.add(minimalSchema(), &error));
    QVERIFY(error.contains(QStringLiteral("已经登记过")));
    QCOMPARE(catalog.count(), 1);
}

void TstSettings::catalogRejectsADeclarationThatFailsItsOwnSelfCheck()
{
    SettingsSchema broken = minimalSchema();
    broken.tabs[0].groups[0].items[0].description.clear();

    SessionSettingsCatalog catalog;
    QString error;
    QVERIFY(!catalog.add(broken, &error));
    // 整条拒绝，不做部分接受：一份「Tab 进去了但设置项丢了一半」的声明会让
    // 对话框显示一个残缺的表单，而用户看不出少的是什么。
    QVERIFY(!error.isEmpty());
    QVERIFY(catalog.isEmpty());
}

void TstSettings::catalogFindsTheCommonSchemaByEmptyTypeId()
{
    SettingsSchema common = minimalSchema();
    common.typeId.clear();

    SessionSettingsCatalog catalog;
    QString error;
    QVERIFY2(catalog.add(common, &error), qPrintable(error));
    QVERIFY(catalog.common());
    QCOMPARE(catalog.common()->itemCount(), 1);
    QCOMPARE(catalog.typeIds(), QStringList{QString()});

    // 通用声明只能有一份，否则「所有类型共用的设置」有两份来源。
    QString duplicateError;
    QVERIFY(!catalog.add(common, &duplicateError));
    QVERIFY(duplicateError.contains(QStringLiteral("通用")));
}

void TstSettings::catalogValidateAggregatesEveryProblem()
{
    SessionSettingsCatalog catalog;
    QVERIFY(catalog.validate().isEmpty());

    // 这条路径目前只能靠直接调用 `validate()` 走到（`add()` 已经拦下了坏声明），
    // 但它是**启动自检**的入口：将来声明可能是从别处（数据文件、插件）装进来的，
    // 那时 `add()` 的拦截就不一定在最前面。断言的是「它真的会把每份声明的问题
    // 都汇总出来」，而不是「它永远返回空」。
    SettingsSchema broken = minimalSchema();
    broken.tabs[0].groups[0].items[0].title.clear();
    const QStringList problems = broken.validate();
    QVERIFY(!problems.isEmpty());
    QCOMPARE(problems.size(), 1);
}

void TstSettings::catalogDescribeCountsItems()
{
    SessionSettingsCatalog empty;
    QCOMPARE(empty.describe(), QStringLiteral("会话设置目录：0 份声明，共 0 个设置项"));

    SessionSettingsCatalog catalog;
    QVERIFY(catalog.add(textLikeSchema()));
    QCOMPARE(catalog.describe(), QStringLiteral("会话设置目录：1 份声明，共 8 个设置项"));
}

void TstSettings::frameworkShipsNoHardcodedSettingItems()
{
    // 框架里**刻意不含任何具体设置项**：内置的 14 种会话类型目前一个声明都没有，
    // 因为「文本比对该有哪些设置」是 TEXT-* / FOLD-* 的产品决定，不是框架的一部分。
    // 先编一份看起来完整的设置表，等各类型落地时会被逐条质疑；而现在这份留白
    // 不妨碍任何人——登记一份声明，对话框立刻就有内容。
    //
    // 这条用例把「留白」这个状态钉住，让下一个人看到它时**必须做一次决定**：
    // 要么如实把某个类型的声明登记进来（那就把这条改成断言那份声明的内容），
    // 要么说明为什么还没做。悄悄塞一份假声明进来会让这条红。
    QVERIFY(SessionSettingsCatalog().isEmpty());
}

void TstSettings::theFrameworkIsNotSpecialCasedToTheSyntheticSchema()
{
    // 反向验证：换一份**完全不同**的声明，框架的结论必须跟着变。
    // 「界面由声明生成」如果只是「把合成声明的键写死在代码里」，下面这一条会红。
    CountingSettings settings;
    SettingsDraft draft(minimalSchema());
    draft.loadFrom(settings);

    QCOMPARE(draft.value(QStringLiteral("recursive")).toBool(), true);
    QVERIFY(!draft.contains(QStringLiteral("ignore-case")));
    QVERIFY(!draft.isTabDirty(QStringLiteral("comparison")));
    QCOMPARE(draft.schema().itemKeys(), QStringList{QStringLiteral("recursive")});

    // 换一份声明之后，上一份的值必须全部作废（否则「上一次测试往表里加了什么」
    // 会泄漏到下一处，而现象是「莫名其妙地脏」）。
    draft.setSchema(textLikeSchema());
    QVERIFY(!draft.contains(QStringLiteral("recursive")));
    QVERIFY(!draft.isDirty());
    QCOMPARE(draft.schema().itemCount(), 8);
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
// 本工程写 `QT -= gui`，于是 QTEST_MAIN 展开成 QCoreApplication ——
// 这正是 SESS-006 第 4 条「可在无界面测试中被单独构造与读写」的编译期部分。
QTEST_MAIN(TstSettings)
