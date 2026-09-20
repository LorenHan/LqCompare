#include "settingschema.h"

#include "maskfilter.h" // Services/Filter —— 掩码清单的校验复用 FILT-001 的解析器

#include <QSet>

namespace LqCompare {

// -----------------------------------------------------------------------------
// 控件类型
// -----------------------------------------------------------------------------

const char *settingControlIdentifier(SettingControl control)
{
    switch (control) {
    case SettingControl::Bool:
        return "bool";
    case SettingControl::Integer:
        return "integer";
    case SettingControl::Text:
        return "text";
    case SettingControl::MultilineText:
        return "multiline-text";
    case SettingControl::Choice:
        return "choice";
    case SettingControl::MaskList:
        return "mask-list";
    }
    return "unknown";
}

QString settingControlLabel(SettingControl control)
{
    switch (control) {
    case SettingControl::Bool:
        return QStringLiteral("开关");
    case SettingControl::Integer:
        return QStringLiteral("整数");
    case SettingControl::Text:
        return QStringLiteral("单行文本");
    case SettingControl::MultilineText:
        return QStringLiteral("多行文本");
    case SettingControl::Choice:
        return QStringLiteral("下拉选择");
    case SettingControl::MaskList:
        return QStringLiteral("掩码清单");
    }
    return QStringLiteral("未知控件");
}

// -----------------------------------------------------------------------------
// 作用域（SESS-007 的语义在那边，这里只定名字）
// -----------------------------------------------------------------------------

const char *settingScopeIdentifier(SettingScope scope)
{
    switch (scope) {
    case SettingScope::View:
        return "view";
    case SettingScope::Session:
        return "session";
    case SettingScope::Type:
        return "type";
    }
    return "unknown";
}

QString settingScopeLabel(SettingScope scope)
{
    switch (scope) {
    case SettingScope::View:
        return QStringLiteral("仅当前视图");
    case SettingScope::Session:
        return QStringLiteral("当前会话默认值");
    case SettingScope::Type:
        // 与 SESS-007 第 1 条里那三种作用域的写法**逐字一致**（「该类型全部新会话
        // 默认值」）。SESS-006 当初先落的是「该类型全部新会话」，本轮对齐成规格
        // 的原文：界面上少一个字都可能让用户把它读成「只影响新建的会话、
        // 不影响默认值」，而它恰恰就是默认值。
        return QStringLiteral("该类型全部新会话默认值");
    }
    return QStringLiteral("未知范围");
}

QString settingScopeDescription(SettingScope scope)
{
    switch (scope) {
    case SettingScope::View:
        return QStringLiteral("改动只作用在这个标签页上，关闭标签后丢弃。");
    case SettingScope::Session:
        return QStringLiteral("改动写进当前会话，下次打开这个会话还在。");
    case SettingScope::Type:
        return QStringLiteral("改动成为该类型所有新建会话的默认值。");
    }
    return QString();
}

QVector<SettingScope> allSettingScopes()
{
    return {SettingScope::View, SettingScope::Session, SettingScope::Type};
}

// -----------------------------------------------------------------------------
// 取值选项与校验规则
// -----------------------------------------------------------------------------

QString SettingChoice::selfCheck() const
{
    if (value.isEmpty()) {
        return QStringLiteral("枚举取值的机器值为空");
    }
    if (label.isEmpty()) {
        return QStringLiteral("枚举取值「%1」没有显示文案").arg(value);
    }
    return QString();
}

bool isValidSettingKey(const QString &key)
{
    // 刻意委托给类型 ID 的规则而不是抄一遍：键与 ID 都是「一经发布不可改名的
    // 机器键」，两份规则必然在某一处先改，而分家的现象是「存进会话文件的键
    // 读不回来」——最难归因的一类故障。
    return isValidSessionTypeId(key);
}

bool SettingValidation::hasAnyRule() const
{
    return required || minLength > 0 || maxLength > 0 || hasMinimum || hasMaximum;
}

QString SettingValidation::describe(SettingControl control) const
{
    // 长度的单位随控件类型变化：掩码清单里数的是**条数**，文本里数的是字符。
    // 两种都写成「个字符」的话，用户会对着「最多 200 个字符」去数自己那 3 条掩码，
    // 而真正越界的是条数。
    const QString unit = (control == SettingControl::MaskList) ? QStringLiteral("条")
                                                              : QStringLiteral("个字符");
    QStringList parts;
    if (required) {
        parts << QStringLiteral("必填");
    }
    if (minLength > 0 && maxLength > 0) {
        parts << QStringLiteral("%1 到 %2 %3").arg(minLength).arg(maxLength).arg(unit);
    } else if (minLength > 0) {
        parts << QStringLiteral("至少 %1 %2").arg(minLength).arg(unit);
    } else if (maxLength > 0) {
        parts << QStringLiteral("最多 %1 %2").arg(maxLength).arg(unit);
    }
    if (hasMinimum && hasMaximum) {
        parts << QStringLiteral("%1 到 %2 之间").arg(minimum).arg(maximum);
    } else if (hasMinimum) {
        parts << QStringLiteral("不小于 %1").arg(minimum);
    } else if (hasMaximum) {
        parts << QStringLiteral("不大于 %1").arg(maximum);
    }
    if (parts.isEmpty()) {
        return QString();
    }
    return QStringLiteral("（%1）").arg(parts.join(QStringLiteral("、")));
}

QString SettingValidation::selfCheck() const
{
    if (minLength < 0 || maxLength < 0) {
        return QStringLiteral("长度下/上界不能为负");
    }
    if (maxLength > 0 && minLength > maxLength) {
        return QStringLiteral("长度下界 %1 大于上界 %2——这样的区间里没有任何合法值")
            .arg(minLength)
            .arg(maxLength);
    }
    if (hasMinimum && hasMaximum && minimum > maximum) {
        return QStringLiteral("数值下界 %1 大于上界 %2——这样的区间里没有任何合法值")
            .arg(minimum)
            .arg(maximum);
    }
    return QString();
}

namespace {

/// 把一段文本按行切开，三种行尾都认。
///
/// FILT-001 已经为掩码声明定过同一条规则（「换行同时认 `\n`、`\r\n`、`\r`」），
/// 那里踩过的坑是：只按 `\n` 切的话，Windows 上编辑过的文本每行末尾会多出一个
/// `\r`，于是 `*.tmp` 变成 `*.tmp\r`，**静静地对不上任何文件**。
/// 这里做同样的事，理由完全相同——用户的掩码清单会被粘进会话文件再读回来，
/// 中间完全可能经过一次 Windows 上的编辑。
QStringList splitLines(const QString &text)
{
    if (text.isEmpty()) {
        return QStringList();
    }
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = normalized.split(QLatin1Char('\n'));
    // **只去掉末尾的空行**，中间的空行留着。
    //
    // 末尾空行要掉：在编辑框里敲完最后一条顺手按一下回车，`toPlainText()` 会以
    // `\n` 结尾，切出来就多一个空条目——留着它会让「内容和默认值一样」变成
    // 「多了一条空掩码」，于是干净的设置被判定成有改动、关窗口时白问一句。
    // 中间的空行不能掉：校验报错带的是**行号**，掉了行号就与用户在编辑框里
    // 看到的不一致，而「第 3 行有错」指到别的行比不给行号更让人困惑。
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

} // namespace

// -----------------------------------------------------------------------------
// 设置项
// -----------------------------------------------------------------------------

QVariant SettingItem::normalized(const QVariant &value) const
{
    switch (control) {
    case SettingControl::Bool:
        return value.toBool();
    case SettingControl::Integer:
        return value.toInt();
    case SettingControl::Text:
    case SettingControl::MultilineText:
    case SettingControl::Choice:
        return value.toString();
    case SettingControl::MaskList: {
        // 传进来的是清单就用它，是一段文本就按行切——会话文件里存的是文本，
        // 读回来必须能落到同一个值上，否则「等于默认值吗」永远为假。
        if (value.type() == QVariant::StringList) {
            QStringList lines = value.toStringList();
            while (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
            return lines;
        }
        return splitLines(value.toString());
    }
    }
    return value;
}

bool SettingItem::isDefault(const QVariant &value) const
{
    return normalized(value) == normalized(defaultValue);
}

QString SettingItem::validate(const QVariant &value) const
{
    const QVariant v = normalized(value);
    const bool isList = (control == SettingControl::MaskList);
    const bool isTextual = (control == SettingControl::Text || control == SettingControl::MultilineText);

    if (isList) {
        const QStringList lines = v.toStringList();
        if (validation.required && lines.isEmpty()) {
            return QStringLiteral("「%1」不能为空，请至少写一条掩码").arg(title);
        }
        if (validation.minLength > 0 && lines.size() < validation.minLength) {
            return QStringLiteral("「%1」至少要 %2 条，现在有 %3 条")
                .arg(title)
                .arg(validation.minLength)
                .arg(lines.size());
        }
        if (validation.maxLength > 0 && lines.size() > validation.maxLength) {
            return QStringLiteral("「%1」最多 %2 条，现在有 %3 条")
                .arg(title)
                .arg(validation.maxLength)
                .arg(lines.size());
        }
        const QString declaration = lines.join(QLatin1Char('\n'));
        if (!declaration.isEmpty()) {
            // 掩码语法不在这里重新实现一遍：`MaskFilter::parse()` 是 FILT-001
            // 那份唯一实现的入口，它连「第几行第几列」都算好了。自己写一份
            // 「大概看一眼」的校验，必然与真正的解析器分家——而分家的表现是
            // 「设置里说合法、扫描时却把这一行丢掉了」。
            const Filter::MaskFilterParseResult parsed = Filter::MaskFilter::parse(declaration);
            if (!parsed.ok()) {
                // 归一化刻意**不丢空行**，因此这里的行号与编辑器里看到的一致。
                return QStringLiteral("「%1」里有无法解析的掩码：%2")
                    .arg(title, parsed.errors.first().describe());
            }
        }
        return QString();
    }

    if (isTextual) {
        const QString text = v.toString();
        if (validation.required && text.isEmpty()) {
            return QStringLiteral("「%1」不能为空").arg(title);
        }
        // 空值不参与长度判定：「不填」与「填得太短」要给出不同的提示，
        // 否则一个可选文本项在留空时会报「至少 3 个字符」，用户以为它必填。
        if (!text.isEmpty() && validation.minLength > 0 && text.size() < validation.minLength) {
            return QStringLiteral("「%1」至少要 %2 个字符，现在有 %3 个")
                .arg(title)
                .arg(validation.minLength)
                .arg(text.size());
        }
        if (validation.maxLength > 0 && text.size() > validation.maxLength) {
            return QStringLiteral("「%1」最多 %2 个字符，现在有 %3 个")
                .arg(title)
                .arg(validation.maxLength)
                .arg(text.size());
        }
        return QString();
    }

    if (control == SettingControl::Choice) {
        const QString chosen = v.toString();
        if (chosen.isEmpty()) {
            return validation.required ? QStringLiteral("请为「%1」选一个取值").arg(title) : QString();
        }
        if (validation.mustBeListed) {
            bool found = false;
            for (const SettingChoice &choice : choices) {
                if (choice.value == chosen) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // 不在取值表里的枚举值在界面上无法显示，用户会看到空白下拉框
                // 而程序认为「已选好」——所以这条报错必须说清合法取值有哪些。
                QStringList known;
                for (const SettingChoice &choice : choices) {
                    known << choice.value;
                }
                return QStringLiteral("「%1」的取值 %2 不在可选范围内（可选：%3）")
                    .arg(title, chosen, known.join(QStringLiteral(" / ")));
            }
        }
        return QString();
    }

    if (control == SettingControl::Integer) {
        const int number = v.toInt();
        if (validation.hasMinimum && number < validation.minimum) {
            return QStringLiteral("「%1」不能小于 %2（现在 %3）")
                .arg(title)
                .arg(validation.minimum)
                .arg(number);
        }
        if (validation.hasMaximum && number > validation.maximum) {
            return QStringLiteral("「%1」不能大于 %2（现在 %3）")
                .arg(title)
                .arg(validation.maximum)
                .arg(number);
        }
        return QString();
    }

    return QString();
}

QString SettingItem::selfCheck() const
{
    if (!isValidSettingKey(key)) {
        return QStringLiteral("设置键「%1」不合法：要求小写字母开头，其余为小写字母、数字或连字符")
            .arg(key);
    }
    if (title.isEmpty()) {
        return QStringLiteral("设置项 %1 没有标题").arg(key);
    }
    if (description.isEmpty()) {
        // 说明必填的理由：一个只有标题的设置项在界面上等于没写，用户只能靠试。
        // 这与 UI-023 的「tooltip 强制两段式」是同一条纪律。
        return QStringLiteral("设置项 %1 没有说明").arg(key);
    }

    const QString ruleProblem = validation.selfCheck();
    if (!ruleProblem.isEmpty()) {
        return QStringLiteral("设置项 %1：%2").arg(key, ruleProblem);
    }

    if (control == SettingControl::Choice) {
        if (choices.isEmpty()) {
            return QStringLiteral("枚举项 %1 没有给出任何取值").arg(key);
        }
        QSet<QString> seen;
        for (const SettingChoice &choice : choices) {
            const QString choiceProblem = choice.selfCheck();
            if (!choiceProblem.isEmpty()) {
                return QStringLiteral("设置项 %1：%2").arg(key, choiceProblem);
            }
            if (seen.contains(choice.value)) {
                return QStringLiteral("设置项 %1 的取值 %2 重复了").arg(key, choice.value);
            }
            seen.insert(choice.value);
        }
    } else if (!choices.isEmpty()) {
        // 留着一份用不上的取值表，下一个人会以为它生效。
        return QStringLiteral("设置项 %1 不是枚举控件，却给出了取值表").arg(key);
    }

    // 默认值必须符合**自己声明的**控件类型。写成 `QVariant(QStringLiteral("true"))`
    // 这类错误在登记时完全看不出来，只在界面第一次取值时表现为「勾选框没勾上」。
    const QVariant def = defaultValue;
    switch (control) {
    case SettingControl::Bool:
        if (def.type() != QVariant::Bool) {
            return QStringLiteral("设置项 %1 是开关，默认值却不是布尔值（实际是 %2）")
                .arg(key, QString::fromLatin1(QVariant::typeToName(def.type())));
        }
        break;
    case SettingControl::Integer:
        if (!def.canConvert<int>() || def.type() == QVariant::String || def.type() == QVariant::Bool) {
            return QStringLiteral("设置项 %1 是整数，默认值却不是整数（实际是 %2）")
                .arg(key, QString::fromLatin1(QVariant::typeToName(def.type())));
        }
        break;
    case SettingControl::Text:
    case SettingControl::MultilineText:
    case SettingControl::Choice:
        if (def.type() != QVariant::String) {
            return QStringLiteral("设置项 %1 的默认值应当是字符串（实际是 %2）")
                .arg(key, QString::fromLatin1(QVariant::typeToName(def.type())));
        }
        break;
    case SettingControl::MaskList:
        if (def.type() != QVariant::StringList) {
            return QStringLiteral("设置项 %1 是掩码清单，默认值应当是字符串清单（实际是 %2）")
                .arg(key, QString::fromLatin1(QVariant::typeToName(def.type())));
        }
        break;
    }

    // 出厂默认值必须能通过自己的校验。否则「恢复默认」会把界面推进一个
    // 「有改动且校验不通过」的状态，而用户什么都没做错——那个按钮看起来坏了。
    const QString violation = validate(defaultValue);
    if (!violation.isEmpty()) {
        return QStringLiteral("设置项 %1 的默认值不符合它自己的校验规则：%2").arg(key, violation);
    }

    return QString();
}

QString SettingItem::describe() const
{
    QString line = QStringLiteral("%1（%2，%3）")
                       .arg(key, title, settingControlLabel(control));
    const QString rule = validation.describe(control);
    if (!rule.isEmpty()) {
        line += rule;
    }
    if (defaultValue.isValid()) {
        const QVariant def = normalized(defaultValue);
        const QString printable =
            def.type() == QVariant::StringList ? def.toStringList().join(QStringLiteral(" | "))
                                               : def.toString();
        line += QStringLiteral(" 默认=%1")
                    .arg(printable.isEmpty() ? QStringLiteral("（空）") : printable);
    }
    if (advanced) {
        line += QStringLiteral(" [高级]");
    }
    return line;
}

// -----------------------------------------------------------------------------
// 分组与 Tab
// -----------------------------------------------------------------------------

const SettingItem *SettingGroup::findItem(const QString &key) const
{
    for (const SettingItem &item : items) {
        if (item.key == key) {
            return &item;
        }
    }
    return nullptr;
}

QString SettingGroup::selfCheck() const
{
    if (id.isEmpty()) {
        return QStringLiteral("设置分组缺少 ID");
    }
    if (title.isEmpty()) {
        return QStringLiteral("设置分组 %1 没有标题").arg(id);
    }
    if (items.isEmpty()) {
        // 空分组在界面上是一个只有标题的空框，用户会以为内容没加载出来。
        return QStringLiteral("设置分组 %1 里没有任何设置项").arg(id);
    }
    QSet<QString> seen;
    for (const SettingItem &item : items) {
        const QString problem = item.selfCheck();
        if (!problem.isEmpty()) {
            return problem;
        }
        if (seen.contains(item.key)) {
            return QStringLiteral("设置分组 %1 里设置键 %2 重复了").arg(id, item.key);
        }
        seen.insert(item.key);
    }
    return QString();
}

QVector<const SettingItem *> SettingsTab::items() const
{
    QVector<const SettingItem *> result;
    for (const SettingGroup &group : groups) {
        for (const SettingItem &item : group.items) {
            result.append(&item);
        }
    }
    return result;
}

const SettingItem *SettingsTab::findItem(const QString &key) const
{
    for (const SettingGroup &group : groups) {
        if (const SettingItem *item = group.findItem(key)) {
            return item;
        }
    }
    return nullptr;
}

int SettingsTab::itemCount() const
{
    int total = 0;
    for (const SettingGroup &group : groups) {
        total += group.items.size();
    }
    return total;
}

QString SettingsTab::selfCheck() const
{
    if (id.isEmpty()) {
        return QStringLiteral("设置 Tab 缺少 ID");
    }
    if (title.isEmpty()) {
        return QStringLiteral("设置 Tab %1 没有标题").arg(id);
    }
    if (groups.isEmpty()) {
        return QStringLiteral("设置 Tab %1 里没有任何分组").arg(id);
    }
    QSet<QString> seenGroups;
    QSet<QString> seenKeys;
    for (const SettingGroup &group : groups) {
        const QString problem = group.selfCheck();
        if (!problem.isEmpty()) {
            return QStringLiteral("Tab %1：%2").arg(id, problem);
        }
        if (seenGroups.contains(group.id)) {
            return QStringLiteral("Tab %1 里分组 ID %2 重复了").arg(id, group.id);
        }
        seenGroups.insert(group.id);
        for (const SettingItem &item : group.items) {
            if (seenKeys.contains(item.key)) {
                return QStringLiteral("Tab %1 里设置键 %2 重复了").arg(id, item.key);
            }
            seenKeys.insert(item.key);
        }
    }
    return QString();
}

// -----------------------------------------------------------------------------
// 声明
// -----------------------------------------------------------------------------

QVector<const SettingItem *> SettingsSchema::items() const
{
    QVector<const SettingItem *> result;
    for (const SettingsTab &tab : tabs) {
        result += tab.items();
    }
    return result;
}

int SettingsSchema::itemCount() const
{
    int total = 0;
    for (const SettingsTab &tab : tabs) {
        total += tab.itemCount();
    }
    return total;
}

const SettingItem *SettingsSchema::findItem(const QString &key) const
{
    for (const SettingsTab &tab : tabs) {
        if (const SettingItem *item = tab.findItem(key)) {
            return item;
        }
    }
    return nullptr;
}

const SettingsTab *SettingsSchema::tabOfItem(const QString &key) const
{
    for (const SettingsTab &tab : tabs) {
        if (tab.findItem(key)) {
            return &tab;
        }
    }
    return nullptr;
}

int SettingsSchema::tabIndexOfItem(const QString &key) const
{
    for (int i = 0; i < tabs.size(); ++i) {
        if (tabs.at(i).findItem(key)) {
            return i;
        }
    }
    return -1;
}

QStringList SettingsSchema::itemKeys() const
{
    QStringList keys;
    for (const SettingsTab &tab : tabs) {
        const QVector<const SettingItem *> tabItems = tab.items();
        for (const SettingItem *item : tabItems) {
            keys << item->key;
        }
    }
    return keys;
}

const SettingsTab *SettingsSchema::findTab(const QString &tabId) const
{
    for (const SettingsTab &tab : tabs) {
        if (tab.id == tabId) {
            return &tab;
        }
    }
    return nullptr;
}

QStringList SettingsSchema::validate() const
{
    QStringList problems;

    if (!typeId.isEmpty() && !isValidSessionTypeId(typeId)) {
        problems << QStringLiteral("声明的会话类型 ID「%1」不合法").arg(typeId);
    }
    if (tabs.isEmpty()) {
        problems << QStringLiteral("声明（类型 %1）里没有任何 Tab")
                        .arg(typeId.isEmpty() ? QStringLiteral("<通用>") : typeId);
    }

    QSet<QString> seenTabs;
    QSet<QString> seenKeys;
    for (const SettingsTab &tab : tabs) {
        const QString problem = tab.selfCheck();
        if (!problem.isEmpty()) {
            problems << problem;
        }
        if (seenTabs.contains(tab.id)) {
            problems << QStringLiteral("Tab ID %1 重复了").arg(tab.id);
        }
        seenTabs.insert(tab.id);

        const QVector<const SettingItem *> tabItems = tab.items();
        for (const SettingItem *item : tabItems) {
            // 键是**全表**唯一的，不只是 Tab 内唯一：会话文件里只存键，
            // 两个 Tab 各有一个同名键时，读回来落到哪一项取决于遍历顺序，
            // 而现象是「改了 A 页的值，B 页也跟着变」。
            if (seenKeys.contains(item->key)) {
                problems << QStringLiteral("设置键 %1 在全表范围内重复了").arg(item->key);
            }
            seenKeys.insert(item->key);
        }
    }

    return problems;
}

QString SettingsSchema::describe() const
{
    return QStringLiteral("设置声明：类型 %1，%2 张 Tab，%3 个设置项")
        .arg(typeId.isEmpty() ? QStringLiteral("<通用>") : typeId)
        .arg(tabs.size())
        .arg(itemCount());
}

// -----------------------------------------------------------------------------
// 校验问题
// -----------------------------------------------------------------------------

QString SettingProblem::describe() const
{
    if (tabId.isEmpty()) {
        return QStringLiteral("%1：%2").arg(key, message);
    }
    return QStringLiteral("%1（Tab %2）：%3").arg(key, tabId, message);
}

// -----------------------------------------------------------------------------
// 草稿
// -----------------------------------------------------------------------------

SettingsDraft::SettingsDraft(QObject *parent)
    : QObject(parent)
{
}

SettingsDraft::SettingsDraft(const SettingsSchema &schema, QObject *parent)
    : QObject(parent)
{
    // 走 `setSchema()` 而不是自己填一遍：建表、落默认值、定基准这三件事必须
    // 只有一个实现——两份的差异会在「换声明」之后才显形（漏掉的那一步让
    // 草稿里一个键都没有，而现象是「设置项全都读不到」）。
    setSchema(schema);
}

SettingsDraft::~SettingsDraft() = default;

void SettingsDraft::setSchema(const SettingsSchema &schema)
{
    const bool wasDirty = isDirty();
    m_schema = schema;
    m_values.clear();
    m_baseline.clear();
    m_order.clear();
    for (const SettingsTab &tab : m_schema.tabs) {
        const QVector<const SettingItem *> tabItems = tab.items();
        for (const SettingItem *item : tabItems) {
            m_order << item->key;
            m_values.insert(item->key, item->normalized(item->defaultValue));
        }
    }
    m_baseline = m_values;
    if (wasDirty) {
        emit dirtyChanged(false);
    }
}

void SettingsDraft::loadFrom(const SessionSettings &settings)
{
    const bool wasDirty = isDirty();
    QStringList changedKeys;
    for (const QString &key : m_order) {
        const SettingItem *item = m_schema.findItem(key);
        if (!item) {
            continue;
        }
        // 读不到就落默认值：草稿里**没有「未设置」这个状态**，每一项都有值。
        // 留着「未设置」的话，界面就得决定「空输入框表示什么」，而那个决定
        // 会在每个控件里各做一遍。
        const QVariant loaded = item->normalized(settings.value(key, item->normalized(item->defaultValue)));
        bool changed = false;
        storeValue(key, loaded, &changed);
        if (changed) {
            changedKeys << key;
        }
    }
    m_baseline = m_values;
    for (const QString &key : changedKeys) {
        emit valueChanged(key);
    }
    if (wasDirty) {
        emit dirtyChanged(false);
    }
}

bool SettingsDraft::storeValue(const QString &key, const QVariant &value, bool *changed)
{
    const SettingItem *item = m_schema.findItem(key);
    if (!item) {
        if (changed) {
            *changed = false;
        }
        return false;
    }
    const QVariant incoming = item->normalized(value);
    const bool differs = (m_values.value(key) != incoming);
    m_values.insert(key, incoming);
    if (changed) {
        *changed = differs;
    }
    return true;
}

QVariant SettingsDraft::value(const QString &key) const
{
    return m_values.value(key);
}

bool SettingsDraft::setValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty() || !m_values.contains(key)) {
        return false;
    }
    const bool wasDirty = isDirty();
    bool changed = false;
    if (!storeValue(key, value, &changed)) {
        return false;
    }
    if (!changed) {
        // 与 `SessionSettings::setValue` 同一条约定：值没变不算改动、不发信号。
        // 「看一眼再确定关掉」不该让会话变脏。
        return true;
    }
    emit valueChanged(key);
    emitDirtyTransition(wasDirty);
    return true;
}

bool SettingsDraft::contains(const QString &key) const
{
    return m_values.contains(key);
}

bool SettingsDraft::isDirty() const
{
    return !dirtyKeys().isEmpty();
}

QStringList SettingsDraft::dirtyKeys() const
{
    QStringList keys;
    for (const QString &key : m_order) {
        if (isItemDirty(key)) {
            keys << key;
        }
    }
    return keys;
}

bool SettingsDraft::isItemDirty(const QString &key) const
{
    if (!m_values.contains(key) || !m_baseline.contains(key)) {
        return false;
    }
    return m_values.value(key) != m_baseline.value(key);
}

bool SettingsDraft::isTabDirty(const QString &tabId) const
{
    return !dirtyKeysInTab(tabId).isEmpty();
}

QStringList SettingsDraft::dirtyKeysInTab(const QString &tabId) const
{
    const SettingsTab *tab = m_schema.findTab(tabId);
    if (!tab) {
        return QStringList();
    }
    QStringList keys;
    const QVector<const SettingItem *> tabItems = tab->items();
    for (const SettingItem *item : tabItems) {
        if (isItemDirty(item->key)) {
            keys << item->key;
        }
    }
    return keys;
}

bool SettingsDraft::isDefault(const QString &key) const
{
    const SettingItem *item = m_schema.findItem(key);
    if (!item) {
        return false;
    }
    return item->isDefault(m_values.value(key));
}

bool SettingsDraft::resetToDefault(const QString &key)
{
    const SettingItem *item = m_schema.findItem(key);
    if (!item || !m_values.contains(key)) {
        return false;
    }
    return setValue(key, item->normalized(item->defaultValue));
}

int SettingsDraft::resetTabToDefaults(const QString &tabId)
{
    const SettingsTab *tab = m_schema.findTab(tabId);
    if (!tab) {
        return 0;
    }
    const bool wasDirty = isDirty();
    const QVector<const SettingItem *> tabItems = tab->items();

    // 先算出改动前的快照，再一次性写入，最后比对。**不用 `resetToDefault()`
    // 的返回值累加**：那个返回值回答的是「写入成功了没有」，不是「值变了没有」，
    // 拿它当「改了几项」会在「本来就是默认值」的项上多数（界面提示写「已恢复 12 项」
    // 而用户其实只改过 1 项）。
    QVariantMap before;
    for (const SettingItem *item : tabItems) {
        before.insert(item->key, m_values.value(item->key));
        m_values.insert(item->key, item->normalized(item->defaultValue));
    }

    int count = 0;
    for (const SettingItem *item : tabItems) {
        if (before.value(item->key) != m_values.value(item->key)) {
            ++count;
            emit valueChanged(item->key);
        }
    }
    emitDirtyTransition(wasDirty);
    return count;
}

int SettingsDraft::resetAllToDefaults()
{
    const bool wasDirty = isDirty();
    const QVariantMap before = m_values;
    for (const QString &key : m_order) {
        const SettingItem *item = m_schema.findItem(key);
        if (!item) {
            continue;
        }
        m_values.insert(key, item->normalized(item->defaultValue));
    }

    int count = 0;
    for (const QString &key : m_order) {
        if (before.value(key) != m_values.value(key)) {
            ++count;
            emit valueChanged(key);
        }
    }
    emitDirtyTransition(wasDirty);
    return count;
}

void SettingsDraft::revert()
{
    const QVariantMap before = m_values;
    const bool wasDirty = isDirty();
    m_values = m_baseline;
    for (const QString &key : m_order) {
        if (before.value(key) != m_values.value(key)) {
            emit valueChanged(key);
        }
    }
    if (wasDirty) {
        emit dirtyChanged(false);
    }
}

QVector<SettingProblem> SettingsDraft::problems() const
{
    QVector<SettingProblem> result;
    for (const QString &key : m_order) {
        const SettingItem *item = m_schema.findItem(key);
        if (!item) {
            continue;
        }
        const QString message = item->validate(m_values.value(key));
        if (message.isEmpty()) {
            continue;
        }
        const SettingsTab *tab = m_schema.tabOfItem(key);
        SettingProblem problem;
        problem.key = key;
        problem.tabId = tab ? tab->id : QString();
        problem.message = message;
        result.append(problem);
    }
    return result;
}

bool SettingsDraft::canApply() const
{
    return problems().isEmpty();
}

bool SettingsDraft::applyTo(SessionSettings *settings, QStringList *appliedKeys)
{
    if (appliedKeys) {
        appliedKeys->clear();
    }
    if (!settings) {
        return false;
    }
    const QStringList keys = dirtyKeys();
    if (keys.isEmpty()) {
        // 与 `CompareSession::save()` 一致：没有改动就不是一次成功的保存。
        // 「什么都没改也点应用」若返回成功，同步目录里会留下一串无意义的版本，
        // 而调用点也没法区分「应用了 0 项」与「应用失败」。
        return false;
    }
    if (!canApply()) {
        // 全有或全无：半应用会让用户看到「一部分生效、一部分没有」，
        // 而被拦下的原因只覆盖其中一项。
        return false;
    }
    for (const QString &key : keys) {
        settings->setValue(key, m_values.value(key));
    }
    m_baseline = m_values;
    if (appliedKeys) {
        *appliedKeys = keys;
    }
    emit dirtyChanged(false);
    return true;
}

QString SettingsDraft::describe() const
{
    return QStringLiteral("设置草稿：%1 项，%2 项有改动，%3 项校验不通过")
        .arg(m_order.size())
        .arg(dirtyKeys().size())
        .arg(problems().size());
}

void SettingsDraft::emitDirtyTransition(bool previous)
{
    const bool now = isDirty();
    if (now != previous) {
        emit dirtyChanged(now);
    }
}

// -----------------------------------------------------------------------------
// 未保存改动的询问
// -----------------------------------------------------------------------------

const char *settingsChangeActionIdentifier(SettingsChangeAction action)
{
    switch (action) {
    case SettingsChangeAction::Apply:
        return "apply";
    case SettingsChangeAction::Discard:
        return "discard";
    case SettingsChangeAction::Cancel:
        return "cancel";
    }
    return "unknown";
}

QString settingsChangeActionLabel(SettingsChangeAction action)
{
    switch (action) {
    case SettingsChangeAction::Apply:
        return QStringLiteral("先应用");
    case SettingsChangeAction::Discard:
        return QStringLiteral("放弃改动");
    case SettingsChangeAction::Cancel:
        return QStringLiteral("返回");
    }
    return QStringLiteral("未知操作");
}

QString SettingsInquiry::describe() const
{
    if (!ask) {
        return QStringLiteral("无改动，不询问");
    }
    QStringList labels;
    for (SettingsChangeAction action : options) {
        labels << settingsChangeActionLabel(action);
    }
    QString defaultLabel = settingsChangeActionLabel(defaultAction);
    return QStringLiteral("%1 / %2 / 选项[%3] / 默认[%4]")
        .arg(title, text, labels.join(QStringLiteral(", ")), defaultLabel);
}

SettingsInquiry inquiryForUnsavedChanges(SettingsInquiryReason reason,
                                         const QStringList &dirtyKeys,
                                         const QString &targetTabTitle)
{
    SettingsInquiry inquiry;
    if (dirtyKeys.isEmpty()) {
        // 没有改动就不问。这是**必须由界面尊重**的结论：每次点标签都弹一次
        // 「要不要保存」是本条目最容易犯的过度设计，代价全落在用户身上。
        return inquiry;
    }

    inquiry.ask = true;
    inquiry.defaultAction = SettingsChangeAction::Cancel;
    inquiry.title = QStringLiteral("有未应用的改动");

    switch (reason) {
    case SettingsInquiryReason::SwitchTab:
        inquiry.text = QStringLiteral("有 %1 项改动尚未应用。切换到「%2」不会丢掉它们，"
                                      "但可以先写回当前会话。")
                           .arg(dirtyKeys.size())
                           .arg(targetTabTitle);
        // 切换 Tab 时草稿原样带过去，没有任何东西会丢——因此**不给**「放弃改动」
        // 这个出口。给一个用不上的破坏性按钮，等于凭空造出一条丢工作的路径。
        inquiry.options = {SettingsChangeAction::Apply, SettingsChangeAction::Cancel};
        break;
    case SettingsInquiryReason::CloseDialog:
        inquiry.text = QStringLiteral("有 %1 项改动尚未应用。关闭后这些改动会丢失。")
                           .arg(dirtyKeys.size());
        // 关闭必须给出「放弃」这个出口，否则用户没有别的选择，只能回去逐项改回来。
        inquiry.options = {SettingsChangeAction::Apply, SettingsChangeAction::Discard,
                           SettingsChangeAction::Cancel};
        break;
    }

    return inquiry;
}

// -----------------------------------------------------------------------------
// 声明目录
// -----------------------------------------------------------------------------

SessionSettingsCatalog::SessionSettingsCatalog() = default;

bool SessionSettingsCatalog::add(const SettingsSchema &schema, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!schema.typeId.isEmpty() && !isValidSessionTypeId(schema.typeId)) {
        if (error) {
            *error = QStringLiteral("会话类型 ID「%1」不合法，声明未登记").arg(schema.typeId);
        }
        return false;
    }
    for (const SettingsSchema &existing : m_schemas) {
        if (existing.typeId == schema.typeId) {
            if (error) {
                *error = schema.typeId.isEmpty()
                             ? QStringLiteral("通用设置声明已经有了一份，拒绝重复登记")
                             : QStringLiteral("会话类型 %1 的设置声明已经登记过").arg(schema.typeId);
            }
            return false;
        }
    }
    // 整条拒绝，不做部分接受：一份「Tab 进去了但设置项丢了一半」的声明会让
    // 设置对话框显示一个残缺的表单，而用户看不出少的是什么。
    const QStringList problems = schema.validate();
    if (!problems.isEmpty()) {
        if (error) {
            *error = QStringLiteral("设置声明 %1 未通过自检：%2")
                         .arg(schema.typeId.isEmpty() ? QStringLiteral("<通用>") : schema.typeId,
                              problems.first());
        }
        return false;
    }
    m_schemas.append(schema);
    return true;
}

void SessionSettingsCatalog::clear()
{
    m_schemas.clear();
}

bool SessionSettingsCatalog::isEmpty() const
{
    return m_schemas.isEmpty();
}

int SessionSettingsCatalog::count() const
{
    return m_schemas.size();
}

const SettingsSchema *SessionSettingsCatalog::find(const QString &typeId) const
{
    for (const SettingsSchema &schema : m_schemas) {
        if (schema.typeId == typeId) {
            return &schema;
        }
    }
    return nullptr;
}

const SettingsSchema *SessionSettingsCatalog::common() const
{
    return find(QString());
}

QStringList SessionSettingsCatalog::typeIds() const
{
    QStringList ids;
    for (const SettingsSchema &schema : m_schemas) {
        ids << schema.typeId;
    }
    return ids;
}

QStringList SessionSettingsCatalog::validate() const
{
    QStringList problems;
    for (const SettingsSchema &schema : m_schemas) {
        problems += schema.validate();
    }
    return problems;
}

QString SessionSettingsCatalog::describe() const
{
    int items = 0;
    for (const SettingsSchema &schema : m_schemas) {
        items += schema.itemCount();
    }
    return QStringLiteral("会话设置目录：%1 份声明，共 %2 个设置项").arg(m_schemas.size()).arg(items);
}

} // namespace LqCompare
