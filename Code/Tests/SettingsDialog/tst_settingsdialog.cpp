#include "tst_settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QFile>

using namespace LqCompare;

// -----------------------------------------------------------------------------
// 测试替身
// -----------------------------------------------------------------------------

RecordingSettings::RecordingSettings(QObject *parent)
    : SessionSettings(parent)
{
}

QStringList RecordingSettings::keys() const
{
    QStringList result = m_values.keys();
    result.sort();
    return result;
}

bool RecordingSettings::contains(const QString &key) const
{
    return m_values.contains(key);
}

QVariant RecordingSettings::value(const QString &key, const QVariant &fallback) const
{
    return m_values.value(key, fallback);
}

bool RecordingSettings::setValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty()) {
        return false;
    }
    m_writes << key;
    m_values.insert(key, value);
    emit changed(key);
    return true;
}

bool RecordingSettings::remove(const QString &key)
{
    return m_values.remove(key) > 0;
}

void RecordingSettings::clear()
{
    m_values.clear();
}

void RecordingSettings::seed(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
}

// -----------------------------------------------------------------------------
// 合成声明
// -----------------------------------------------------------------------------

namespace {

SettingItem makeItem(const QString &key, const QString &title, SettingControl control,
                     const QVariant &defaultValue,
                     SettingValidation validation = SettingValidation())
{
    SettingItem item;
    item.key = key;
    item.title = title;
    item.description = QStringLiteral("%1 的说明").arg(title);
    item.control = control;
    item.defaultValue = defaultValue;
    item.validation = validation;
    return item;
}

QStringList schemaKeys()
{
    return {QStringLiteral("recursive"),    QStringLiteral("threads"),
            QStringLiteral("quick-compare"), QStringLiteral("exclude-masks"),
            QStringLiteral("left-label"),   QStringLiteral("notes"),
            QStringLiteral("version-tags"), QStringLiteral("plain-toggle")};
}

///
/// \brief 从源码里找出被写死的合成声明键。
///
/// 抽成接受字符串的纯函数（而不是直接读文件），是为了能对一段**故意写坏**的
/// 源码做反向验证——一个从不报错的护栏比没有护栏更糟。
/// 与 `Tests/SessionType::homePageHardcodedIdsMatchTheRegistry` 是同一个手法。
///
QStringList hardcodedKeysIn(const QString &source, const QStringList &keys)
{
    QStringList found;
    for (const QString &key : keys) {
        if (source.contains(QStringLiteral("\"%1\"").arg(key))) {
            found << key;
        }
    }
    return found;
}

QString readSourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

///
/// \brief 带「永不弹窗」保护的对话框。
///
/// **为什么需要它**：不装询问处理器时，询问会走真实的 `QMessageBox::exec()`——
/// 在 offscreen 平台下那是一个**永远等不到输入**的模态循环，用例会挂在那里
/// 而不是变红。挂住的代价比一条红的断言大得多：CI 上表现为超时，本地表现为
/// 「测试卡住了」，排查方向完全错。
///
/// 默认答案取最保守的「返回」（什么都不做）。凡是会被问到的用例都因此变成
/// 「断言失败」而不是「卡住」。要用别的答案的用例，构造之后再用
/// `answerAlways()` 覆盖一次即可。
///
class GuardedDialog : public SessionSettingsDialog
{
public:
    GuardedDialog(const LqCompare::SettingsSchema &schema, LqCompare::SessionSettings *target,
                  QWidget *parent = nullptr)
        : SessionSettingsDialog(schema, target, parent)
    {
        setInquiryHandler([](QWidget *, const SettingsInquiry &inquiry) {
            return inquiry.defaultAction; // 恒为「返回」
        });
    }
};

/// 把对话框显示出来。用例里凡是断言「关没关」「还在不在」的都要先显示——
/// 一个从未显示过的 QDialog 本来就 `isVisible() == false`，那时断言什么都成立。
void showDialog(SessionSettingsDialog &dialog)
{
    dialog.resize(900, 620);
    dialog.show();
    QCoreApplication::processEvents();
}

/// 注入一个固定答案的询问处理器。
void answerAlways(SessionSettingsDialog &dialog, SettingsChangeAction answer)
{
    dialog.setInquiryHandler(
        [answer](QWidget *, const SettingsInquiry &) { return answer; });
}

} // namespace

LqCompare::SettingsSchema TstSettingsDialog::folderLikeSchema() const
{
    SettingsSchema schema;
    schema.typeId = QStringLiteral("folder");

    {
        SettingsTab tab;
        tab.id = QStringLiteral("scan");
        tab.title = QStringLiteral("扫描");
        tab.description = QStringLiteral("这一页决定文件夹怎么被遍历。");

        SettingGroup rules;
        rules.id = QStringLiteral("rules");
        rules.title = QStringLiteral("规则");
        rules.description = QStringLiteral("影响扫描阶段。");
        rules.items << makeItem(QStringLiteral("recursive"), QStringLiteral("包含子目录"),
                                SettingControl::Bool, true);
        {
            SettingValidation v;
            v.hasMinimum = true;
            v.minimum = 1;
            v.hasMaximum = true;
            v.maximum = 16;
            rules.items << makeItem(QStringLiteral("threads"), QStringLiteral("并行数"),
                                    SettingControl::Integer, 4, v);
        }
        {
            SettingItem choice = makeItem(QStringLiteral("quick-compare"), QStringLiteral("快速比较"),
                                          SettingControl::Choice, QStringLiteral("size-time"));
            choice.choices = {{QStringLiteral("size-time"), QStringLiteral("大小 + 时间"), QStringLiteral("最快")},
                              {QStringLiteral("size"), QStringLiteral("仅大小"), QString()},
                              {QStringLiteral("content"), QStringLiteral("内容"), QString()}};
            rules.items << choice;
        }
        {
            SettingValidation v;
            v.maxLength = 5;
            rules.items << makeItem(QStringLiteral("exclude-masks"), QStringLiteral("排除掩码"),
                                    SettingControl::MaskList, QStringList{QStringLiteral("-*.bak")}, v);
        }

        SettingGroup pro;
        pro.id = QStringLiteral("pro");
        pro.title = QStringLiteral("版本比较");
        pro.proFeature = true;
        pro.items << makeItem(QStringLiteral("version-tags"), QStringLiteral("比较版本标签"),
                              SettingControl::Bool, false);

        tab.groups << rules << pro;
        schema.tabs << tab;
    }

    {
        SettingsTab tab;
        tab.id = QStringLiteral("display");
        tab.title = QStringLiteral("显示");
        tab.description = QStringLiteral("这一页决定结果怎么呈现。");

        SettingGroup labels;
        labels.id = QStringLiteral("labels");
        labels.title = QStringLiteral("标注");
        {
            SettingValidation v;
            v.maxLength = 40;
            labels.items << makeItem(QStringLiteral("left-label"), QStringLiteral("左侧标题"),
                                     SettingControl::Text, QStringLiteral("左侧"), v);
        }
        labels.items << makeItem(QStringLiteral("notes"), QStringLiteral("备注"),
                                 SettingControl::MultilineText, QString());

        // `advanced` 只影响界面呈现，不进任何逻辑——用它验证「声明里的字段真的
        // 传到控件上了」，而不是「高级项被藏起来了」。
        SettingItem advanced = makeItem(QStringLiteral("plain-toggle"), QStringLiteral("微调"),
                                        SettingControl::Bool, false);
        advanced.advanced = true;
        labels.items << advanced;

        tab.groups << labels;
        schema.tabs << tab;
    }

    return schema;
}

LqCompare::SettingsSchema TstSettingsDialog::tinySchema() const
{
    SettingsSchema schema;
    schema.typeId = QStringLiteral("hex");

    SettingsTab tab;
    tab.id = QStringLiteral("bytes");
    tab.title = QStringLiteral("字节");

    SettingGroup group;
    group.id = QStringLiteral("layout");
    group.title = QStringLiteral("排布");
    group.items << makeItem(QStringLiteral("columns"), QStringLiteral("列数"),
                            SettingControl::Integer, 16);

    tab.groups << group;
    schema.tabs << tab;
    return schema;
}

// -----------------------------------------------------------------------------
// 用例
// -----------------------------------------------------------------------------

void TstSettingsDialog::initTestCase()
{
    QVERIFY2(folderLikeSchema().validate().isEmpty(),
             qPrintable(folderLikeSchema().validate().join(QStringLiteral("; "))));
    QVERIFY2(tinySchema().validate().isEmpty(),
             qPrintable(tinySchema().validate().join(QStringLiteral("; "))));
    // E 组的源码级护栏依赖这个文件真的读得到。先在这里失败一次，
    // 免得落到「护栏静默通过」上。
    QVERIFY(!readSourceFile(QStringLiteral("/Views/Session/settingsdialog.cpp")).isEmpty());
}

// --- A 结构与「由声明生成」 ---------------------------------------------------

void TstSettingsDialog::tabListSitsOnTheLeftAndContentOnTheRight()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    auto *tabList = dialog.findChild<QListWidget *>(QStringLiteral("settingsTabList"));
    auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("settingsTabStack"));
    QVERIFY2(tabList, "第 1 条要求左侧是 Tab 列表");
    QVERIFY2(stack, "第 1 条要求右侧是当前 Tab 的内容");

    // 断言几何关系而不是「两个控件都存在」：只有真把它们放进同一个水平布局，
    // 这条才成立。一个把两者上下摞在一起的实现同样能让两个指针非空。
    const int listX = tabList->mapTo(&dialog, QPoint(0, 0)).x();
    const int stackX = stack->mapTo(&dialog, QPoint(0, 0)).x();
    QVERIFY2(listX < stackX, "Tab 列表必须在内容的左边");
    QCOMPARE(stack->count(), dialog.tabCount());
}

void TstSettingsDialog::footerCarriesTheScopeComboAndFourButtons()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("settingsScopeCombo"));
    auto *restore = dialog.findChild<QPushButton *>(QStringLiteral("settingsRestoreDefaultsButton"));
    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApplyButton"));
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("settingsOkButton"));
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancelButton"));

    QVERIFY2(combo, "第 1 条要求底部有作用域下拉");
    QVERIFY2(restore, "第 1 条要求底部有「恢复默认」");
    QVERIFY2(apply, "第 1 条要求底部有「应用」");
    QVERIFY2(ok, "第 1 条要求底部有「确定」");
    QVERIFY2(cancel, "第 1 条要求底部有「取消」");

    // 文案也要钉住：四个按钮都建成「确定」的话，功能上仍然「都在」，
    // 但用户看到的是一个没法用的对话框。
    QCOMPARE(restore->text(), QStringLiteral("恢复默认"));
    QCOMPARE(apply->text(), QStringLiteral("应用"));
    QCOMPARE(ok->text(), QStringLiteral("确定"));
    QCOMPARE(cancel->text(), QStringLiteral("取消"));

    // 底部的意思：它必须排在内容区下方。
    auto *stack = dialog.findChild<QStackedWidget *>(QStringLiteral("settingsTabStack"));
    const int stackBottom = stack->mapTo(&dialog, QPoint(0, 0)).y() + stack->height();
    QVERIFY2(combo->mapTo(&dialog, QPoint(0, 0)).y() >= stackBottom,
             "作用域下拉必须在内容区下方");
}

void TstSettingsDialog::scopeComboCoversAllThreeScopesWithDescriptions()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    QCOMPARE(dialog.scopeCount(), allSettingScopes().size());
    for (int i = 0; i < dialog.scopeCount(); ++i) {
        auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("settingsScopeCombo"));
        const auto scope = static_cast<SettingScope>(combo->itemData(i).toInt());
        QCOMPARE(combo->itemText(i), settingScopeLabel(scope));
        // 每个项都要带说明：光看「仅当前视图」用户判断不出改动会不会留到下次打开。
        QVERIFY(!combo->itemData(i, Qt::ToolTipRole).toString().isEmpty());
    }

    QVERIFY(dialog.setScope(SettingScope::Type));
    QCOMPARE(dialog.scope(), SettingScope::Type);
    QVERIFY(dialog.setScope(SettingScope::View));
    QCOMPARE(dialog.scope(), SettingScope::View);
}

void TstSettingsDialog::tabListFollowsTheSchema()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    QCOMPARE(dialog.tabCount(), 2);
    QCOMPARE(dialog.tabTitles(), (QStringList{QStringLiteral("扫描"), QStringLiteral("显示")}));
    QCOMPARE(dialog.currentTabIndex(), 0);
    QCOMPARE(dialog.currentTabId(), QStringLiteral("scan"));

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("settingsTabList"));
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("扫描"));
    QCOMPARE(list->item(0)->toolTip(), folderLikeSchema().tabs.at(0).description);
}

void TstSettingsDialog::everyItemGetsATitleEditorAndDescription()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    const SettingsSchema schema = folderLikeSchema();
    const QVector<const SettingItem *> items = schema.items();
    QCOMPARE(items.size(), 8);

    for (const SettingItem *item : items) {
        QVERIFY2(dialog.editorFor(item->key) != nullptr, qPrintable(item->key));
        QWidget *title = dialog.titleLabelFor(item->key);
        QVERIFY2(title, qPrintable(item->key));
        // 高级项在标题后带一个标记；其余项的标题必须与声明逐字相同。
        QVERIFY2(title->property("text").toString().startsWith(item->title),
                 qPrintable(title->property("text").toString()));
        QVERIFY2(dialog.descriptionLabelFor(item->key), qPrintable(item->key));
    }
}

void TstSettingsDialog::descriptionLabelCarriesTheDeclaredText()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    const SettingsSchema schema = folderLikeSchema();

    // 「说明」是第 2 条点名的五样之一，它必须真的出现在界面上，而不是只存在数据里。
    auto *label = qobject_cast<QLabel *>(dialog.descriptionLabelFor(QStringLiteral("threads")));
    QVERIFY(label);
    QCOMPARE(label->text(), schema.findItem(QStringLiteral("threads"))->description);
    QVERIFY(label->wordWrap());
}

void TstSettingsDialog::eachControlTypeMapsToTheMatchingWidget()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    QVERIFY(qobject_cast<QCheckBox *>(dialog.editorFor(QStringLiteral("recursive"))));
    QVERIFY(qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("threads"))));
    QVERIFY(qobject_cast<QComboBox *>(dialog.editorFor(QStringLiteral("quick-compare"))));
    QVERIFY(qobject_cast<QPlainTextEdit *>(dialog.editorFor(QStringLiteral("exclude-masks"))));
    QVERIFY(qobject_cast<QLineEdit *>(dialog.editorFor(QStringLiteral("left-label"))));
    QVERIFY(qobject_cast<QPlainTextEdit *>(dialog.editorFor(QStringLiteral("notes"))));

    // 掩码清单与多行文本都用 QPlainTextEdit，但它们的值形态不同——按控件类型
    // 反推值形态会错，因此这一条只能验证「控件选对了」，值形态由下面几条验证。
    QCOMPARE(dialog.editorFor(QStringLiteral("不存在的键")), nullptr);
}

void TstSettingsDialog::integerEditorUsesTheDeclaredBounds()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *spin = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("threads")));
    QVERIFY(spin);
    // 校验规则顺手变成界面约束。**不设范围**的话 QSpinBox 默认只允许 0..99，
    // 用户想填 200 会被静静夹到 99——「看起来能用、实际改了用户的输入」。
    QCOMPARE(spin->minimum(), 1);
    QCOMPARE(spin->maximum(), 16);
    QCOMPARE(spin->value(), 4);
}

void TstSettingsDialog::textEditorUsesTheDeclaredMaxLength()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *edit = qobject_cast<QLineEdit *>(dialog.editorFor(QStringLiteral("left-label")));
    QVERIFY(edit);
    // 先挡住超长输入，用户就不会敲完 200 个字符才被告知最多 40 个。
    QCOMPARE(edit->maxLength(), 40);
}

void TstSettingsDialog::choiceEditorIsPopulatedFromTheSchema()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *combo = qobject_cast<QComboBox *>(dialog.editorFor(QStringLiteral("quick-compare")));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QStringLiteral("大小 + 时间"));
    QCOMPARE(combo->itemData(0).toString(), QStringLiteral("size-time"));
    QCOMPARE(combo->itemText(1), QStringLiteral("仅大小"));
    // 取值自己的说明也要带上（下拉项的 tooltip）。
    QCOMPARE(combo->itemData(0, Qt::ToolTipRole).toString(), QStringLiteral("最快"));
    // 当前选中的应当是与默认值对应的那一项。
    QCOMPARE(combo->currentData().toString(), QStringLiteral("size-time"));
}

void TstSettingsDialog::editorsShowTheSessionsCurrentValuesNotTheDefaults()
{
    RecordingSettings settings;
    settings.seed(QStringLiteral("recursive"), false); // 出厂默认是 true
    settings.seed(QStringLiteral("threads"), 8);       // 出厂默认是 4
    settings.seed(QStringLiteral("exclude-masks"),
                  QStringList{QStringLiteral("*.obj"), QStringLiteral("-*.bak")});

    GuardedDialog dialog(folderLikeSchema(), &settings);

    // 这条盯的是构造顺序：先读会话、再建界面。反过来的话各项控件会先按默认值
    // 建好，而那时的 `loadFrom()` 还没有人接信号——界面上显示的全是默认值，
    // 用户会以为自己的设置丢了，然后点「确定」把默认值真的写回去。
    auto *box = qobject_cast<QCheckBox *>(dialog.editorFor(QStringLiteral("recursive")));
    auto *spin = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("threads")));
    auto *masks = qobject_cast<QPlainTextEdit *>(dialog.editorFor(QStringLiteral("exclude-masks")));
    QVERIFY(box && spin && masks);
    QVERIFY(!box->isChecked());
    QCOMPARE(spin->value(), 8);
    QCOMPARE(masks->toPlainText(), QStringLiteral("*.obj\n-*.bak"));
    QVERIFY2(!dialog.draft()->isDirty(), "把会话现值读进来不是一次改动");
}

void TstSettingsDialog::dialogWithoutTabsIsStillUsable()
{
    RecordingSettings settings;
    SettingsSchema empty;
    empty.typeId = QStringLiteral("empty");

    GuardedDialog dialog(empty, &settings);
    showDialog(dialog);

    QCOMPARE(dialog.tabCount(), 0);
    QCOMPARE(dialog.currentTabIndex(), -1);
    QVERIFY(dialog.currentTabId().isEmpty());
    // 底部那五个控件与声明无关，因此照样在（用户不会看到一片空白）。
    QCOMPARE(dialog.scopeCount(), 3);
    QVERIFY(dialog.applyChanges() == false);
    QCOMPARE(settings.writeCount(), 0);
    // 没有改动时关掉：不询问、也不崩。
    answerAlways(dialog, SettingsChangeAction::Cancel);
    dialog.rejectChanges();
    QCOMPARE(dialog.inquiryCount(), 0);
}

void TstSettingsDialog::groupsBecomeGroupBoxesInTheOrderDeclared()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *page = dialog.findChild<QScrollArea *>(QStringLiteral("settingsTabPage_scan"));
    QVERIFY(page);
    const QList<QGroupBox *> boxes = page->widget()->findChildren<QGroupBox *>();
    QCOMPARE(boxes.size(), 2);
    QCOMPARE(boxes.at(0)->title(), QStringLiteral("规则"));
    QCOMPARE(boxes.at(1)->title(), QStringLiteral("版本比较（Pro）"));
    // 分组的说明也要出现在界面上（这里用它做 tooltip）。
    QCOMPARE(boxes.at(0)->toolTip(), QStringLiteral("影响扫描阶段。"));
}

void TstSettingsDialog::advancedAndProFlagsReachTheWidgets()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    // Pro 只做标注、不禁用：本仓库目前没有版本判定，拿它去禁用会让 Standard 用户
    // 看到一个点不动的设置项，而没有任何地方解释为什么。
    auto *page = dialog.findChild<QScrollArea *>(QStringLiteral("settingsTabPage_scan"));
    const QList<QGroupBox *> boxes = page->widget()->findChildren<QGroupBox *>();
    QCOMPARE(boxes.at(1)->title(), QStringLiteral("版本比较（Pro）"));
    QVERIFY(boxes.at(1)->isEnabled());

    // 高级项：标题带标记，但**照样可改**（折叠会让用户怀疑它到底有没有生效）。
    auto *title = qobject_cast<QLabel *>(dialog.titleLabelFor(QStringLiteral("plain-toggle")));
    QVERIFY(title);
    QVERIFY2(title->text().contains(QStringLiteral("微调")), qPrintable(title->text()));
    QVERIFY2(title->text().contains(QStringLiteral("高级")), qPrintable(title->text()));
    QVERIFY(dialog.editorFor(QStringLiteral("plain-toggle"))->isEnabled());
}

// --- B 切 Tab 与关闭的确认（第 3 条） -----------------------------------------

void TstSettingsDialog::switchingTabWithoutChangesDoesNotAsk()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    QVERIFY(dialog.goToTab(1));
    QCOMPARE(dialog.currentTabIndex(), 1);
    QCOMPARE(dialog.currentTabId(), QStringLiteral("display"));
    // 没有改动就不打扰用户：每次都弹一次「要不要保存」是本条目最容易犯的
    // 过度设计，代价全落在用户身上。因此断言的是**询问次数**，不是「有没有响应」。
    QCOMPARE(dialog.inquiryCount(), 0);
}

void TstSettingsDialog::switchingTabWithChangesCanStayPut()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    answerAlways(dialog, SettingsChangeAction::Cancel);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QVERIFY(!dialog.goToTab(1));
    // 用户选「返回」→ 停在原处，改动一个字都没丢。
    QCOMPARE(dialog.inquiryCount(), 1);
    QCOMPARE(dialog.currentTabIndex(), 0);
    QCOMPARE(dialog.draft()->value(QStringLiteral("threads")).toInt(), 8);
    QCOMPARE(settings.writeCount(), 0);

    // 询问里「去哪张 Tab」必须写清楚，否则用户不知道自己被问的是什么。
    QVERIFY(dialog.lastInquiry().text.contains(QStringLiteral("显示")));
    QVERIFY(dialog.lastInquiry().ask);
    QVERIFY(!dialog.lastInquiry().options.contains(SettingsChangeAction::Discard));
}

void TstSettingsDialog::switchingTabWithChangesCanApplyFirst()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    answerAlways(dialog, SettingsChangeAction::Apply);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QVERIFY(dialog.goToTab(1));
    QCOMPARE(dialog.currentTabIndex(), 1);
    // 「先应用」这个出口的意义就在这里：用户不用先切回去按下应用再切过来。
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);
    QVERIFY(!dialog.draft()->isDirty());
    QCOMPARE(settings.writeCount(), 1);
}

void TstSettingsDialog::clickingTheTabListGoesThroughTheSameInquiry()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("settingsTabList"));
    QVERIFY(list);

    // 从列表点过去必须与 `goToTab()` 走同一条判断。直接把 `currentRowChanged`
    // 连到切换上的话，用户会被问一句然后**仍然停在新 Tab 上**（选「返回」没用）。
    answerAlways(dialog, SettingsChangeAction::Cancel);
    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    list->setCurrentRow(1);
    QCOMPARE(dialog.inquiryCount(), 1);
    QCOMPARE(dialog.currentTabIndex(), 0);

    // 选「返回」后列表的选择也要拨回来，否则界面说的是「显示」而内容是「扫描」。
    QCOMPARE(list->currentRow(), 0);

    answerAlways(dialog, SettingsChangeAction::Apply);
    list->setCurrentRow(1);
    QCOMPARE(dialog.currentTabIndex(), 1);
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);
}

void TstSettingsDialog::refusingToSwitchKeepsTheSelectionOnTheOldTab()
{
    RecordingSettings settings;
    SettingsSchema schema = folderLikeSchema();
    GuardedDialog dialog(schema, &settings);
    showDialog(dialog);

    // 从 0 跳到 1 再退回 0：用户看到的「选择」与「内容」必须是同一张 Tab。
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("settingsTabList"));
    answerAlways(dialog, SettingsChangeAction::Cancel);
    dialog.draft()->setValue(QStringLiteral("left-label"), QStringLiteral("旧"));
    list->setCurrentRow(1);
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(dialog.currentTabId(), QStringLiteral("scan"));
    QCOMPARE(dialog.draft()->dirtyKeys().size(), 1);
}

void TstSettingsDialog::closingWithChangesAsksAndCanBeRefused()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Cancel);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    dialog.rejectChanges();

    QCOMPARE(dialog.inquiryCount(), 1);
    QCOMPARE(dialog.lastInquiry().options.size(), 3);
    QCOMPARE(dialog.lastInquiry().defaultAction, SettingsChangeAction::Cancel);
    // 选「返回」→ 对话框不许关，改动也还在。
    QVERIFY2(dialog.isVisible(), "用户选了「返回」，对话框必须留着");
    QVERIFY(dialog.draft()->isDirty());
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettingsDialog::closingWithChangesCanDiscardThem()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Discard);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    dialog.close(); // 关窗口键

    QVERIFY2(!dialog.isVisible(), "选了「放弃改动」之后要真的关掉");
    QVERIFY2(!dialog.draft()->isDirty(), "放弃之后草稿要回到载入时的值");
    QCOMPARE(dialog.draft()->value(QStringLiteral("threads")).toInt(), 4);
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettingsDialog::closingWithChangesCanApplyFirst()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Apply);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    dialog.close();

    QVERIFY(!dialog.isVisible());
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);
    QCOMPARE(settings.writeCount(), 1);
}

void TstSettingsDialog::escapeGoesThroughTheSameInquiry()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Cancel);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QTest::keyClick(&dialog, Qt::Key_Escape);

    // Esc 是最容易被误触的关闭路径，它必须与「取消」按钮走同一份判断——
    // 分成两处写的话，漏掉的那一处正好就是这里。
    QCOMPARE(dialog.inquiryCount(), 1);
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.draft()->isDirty());
}

void TstSettingsDialog::closingWithoutChangesDoesNotAsk()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Discard);

    dialog.close();

    QCOMPARE(dialog.inquiryCount(), 0);
    QVERIFY(!dialog.isVisible());
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettingsDialog::aFailedApplyKeepsTheDialogOpen()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Apply);

    dialog.draft()->setValue(QStringLiteral("threads"), 999); // 越界
    dialog.close();

    // 校验不通过时不许关：用户会以为保存成功了，而改动其实一个字都没写。
    QVERIFY(dialog.isVisible());
    QCOMPARE(settings.writeCount(), 0);
}

// --- C 四个按钮 ---------------------------------------------------------------

void TstSettingsDialog::applyWritesTheChangedKeysAndClearsTheDirtyMarks()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    QSignalSpy spy(&dialog, &SessionSettingsDialog::applied);

    // 通过控件改动（而不是直接改草稿），顺带验证控件 → 草稿这条接线是通的。
    auto *spin = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("threads")));
    auto *box = qobject_cast<QCheckBox *>(dialog.editorFor(QStringLiteral("recursive")));
    QVERIFY(spin && box);
    spin->setValue(8);
    box->setChecked(false);

    QVERIFY(dialog.draft()->isDirty());
    QVERIFY(dialog.applyChanges());

    QCOMPARE(settings.writes(),
             (QStringList{QStringLiteral("recursive"), QStringLiteral("threads")}));
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);
    QCOMPARE(settings.value(QStringLiteral("recursive")).toBool(), false);
    QVERIFY(!dialog.draft()->isDirty());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toStringList().size(), 2);
}

void TstSettingsDialog::applyIsDisabledWhenThereIsNothingToApply()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApplyButton"));
    QVERIFY(apply);
    // 没有改动时把「应用」灰掉，而不是让它点了没反应：一个点了什么都不发生的
    // 按钮会让人以为程序卡了。
    QVERIFY(!apply->isEnabled());

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QVERIFY(apply->isEnabled());

    // 改回原值 → 又没得应用了。
    dialog.draft()->setValue(QStringLiteral("threads"), 4);
    QVERIFY(!apply->isEnabled());

    // 「确定」不跟着灰：没改动时它的语义就是「关闭」。
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("settingsOkButton"));
    QVERIFY(ok->isEnabled());
}

void TstSettingsDialog::applyIsDisabledWhileTheValueIsInvalid()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApplyButton"));
    dialog.draft()->setValue(QStringLiteral("threads"), 999);
    QVERIFY(!apply->isEnabled());

    // 点了也不该写进去（`click()` 在禁用状态下什么都不做，这里额外直接调一次
    // 公开出口，证明这不是靠按钮的禁用「挡」住的）。
    apply->click();
    QCOMPARE(settings.writeCount(), 0);
    QVERIFY(!dialog.applyChanges());
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettingsDialog::cancelClosesWithoutWriting()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Discard);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    dialog.rejectChanges();

    QVERIFY(!dialog.isVisible());
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettingsDialog::restoreDefaultsResetsOnlyTheCurrentTab()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);          // 扫描页
    dialog.draft()->setValue(QStringLiteral("left-label"), QStringLiteral("旧")); // 显示页

    QCOMPARE(dialog.restoreCurrentTabDefaults(), 1);
    QCOMPARE(dialog.draft()->value(QStringLiteral("threads")).toInt(), 4);
    // 「恢复默认」只作用于当前 Tab：按钮贴着当前页的内容，用户按它时的预期是
    // 「这一页恢复原样」。整表重置是更大范围的动作，该有一个说清范围的入口。
    QCOMPARE(dialog.draft()->value(QStringLiteral("left-label")).toString(), QStringLiteral("旧"));
    QCOMPARE(dialog.draft()->dirtyKeys(), QStringList{QStringLiteral("left-label")});
    QCOMPARE(settings.writeCount(), 0);
}

void TstSettingsDialog::restoreDefaultsCanBeUndoneByCancel()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);
    answerAlways(dialog, SettingsChangeAction::Discard);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    dialog.applyChanges();
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);

    // 应用之后再按「恢复默认」：这次重置把 8 改回 4（相对基准是一次改动），
    // 但**没有**写回会话——因此「取消」能把它撤销掉。
    QCOMPARE(dialog.restoreCurrentTabDefaults(), 1);
    QCOMPARE(dialog.draft()->value(QStringLiteral("threads")).toInt(), 4);
    QVERIFY(dialog.draft()->isDirty());
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);

    dialog.rejectChanges();
    QVERIFY(!dialog.isVisible());
    // 会话里仍然是应用过的那份值：取消撤销的是「恢复默认」，不是那一次应用。
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);
}

void TstSettingsDialog::restoreDefaultsIsDisabledWhenEverythingIsDefault()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *restore = dialog.findChild<QPushButton *>(QStringLiteral("settingsRestoreDefaultsButton"));
    QVERIFY(restore);
    // 第一页上一切都是出厂默认 → 没什么可恢复的，按钮该是灰的。
    QVERIFY(!restore->isEnabled());

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QVERIFY(restore->isEnabled());

    dialog.restoreCurrentTabDefaults();
    QVERIFY(!restore->isEnabled());

    // 切到另一页时按**当前页**重新判定：显示页各项也都是出厂默认，
    // 因此这里仍然不可用；改一项之后才可用。
    QVERIFY(dialog.goToTab(1));
    QVERIFY(!restore->isEnabled());
    dialog.draft()->setValue(QStringLiteral("left-label"), QStringLiteral("旧"));
    QVERIFY(restore->isEnabled());
}

void TstSettingsDialog::okAppliesAndCloses()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("settingsOkButton"));
    ok->click();

    QVERIFY(!dialog.isVisible());
    QCOMPARE(settings.value(QStringLiteral("threads")).toInt(), 8);
    QCOMPARE(dialog.inquiryCount(), 0); // 「确定」不该再问一遍
}

void TstSettingsDialog::okWithoutChangesJustCloses()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("settingsOkButton"));
    ok->click();

    QVERIFY(!dialog.isVisible());
    QCOMPARE(settings.writeCount(), 0);
    QCOMPARE(dialog.inquiryCount(), 0);
}

void TstSettingsDialog::okStaysOpenWhenTheValueIsInvalid()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    dialog.draft()->setValue(QStringLiteral("threads"), 999);
    dialog.acceptChanges();

    QVERIFY2(dialog.isVisible(), "校验不通过时「确定」不许关");
    QCOMPARE(settings.writeCount(), 0);
    QVERIFY(dialog.draft()->isDirty());
}

// --- D 校验的界面反馈 ---------------------------------------------------------

void TstSettingsDialog::anInvalidValueShowsAProblemUnderTheItem()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    // 必须真的显示出来：`isVisible()` 为假也可能是「父窗口没显示」，
    // 那样这条用例断言的就成了「对话框没打开」这件与错误行无关的事。
    showDialog(dialog);

    auto *problem =
        qobject_cast<QLabel *>(dialog.findChild<QLabel *>(QStringLiteral("settingsProblem_threads")));
    QVERIFY(problem);
    QVERIFY2(!problem->isVisible(), "校验通过时不该显示错误行");

    dialog.draft()->setValue(QStringLiteral("threads"), 999);
    QVERIFY(problem->isVisible());
    const QString text = problem->text();
    QVERIFY(!text.isEmpty());
    // 报错要能落到具体项上：只说「不能大于 16」的话，用户得在一屏控件里自己找。
    QVERIFY2(text.contains(QStringLiteral("并行数")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("16")));
}

void TstSettingsDialog::fixingTheValueClearsTheProblem()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);
    showDialog(dialog);

    auto *problem =
        qobject_cast<QLabel *>(dialog.findChild<QLabel *>(QStringLiteral("settingsProblem_threads")));

    dialog.draft()->setValue(QStringLiteral("threads"), 999);
    QVERIFY(problem->isVisible());

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QVERIFY2(!problem->isVisible(), "改回合法值之后错误行要消失");
    QVERIFY(problem->text().isEmpty());
}

void TstSettingsDialog::theProblemTextIsTheServiceLayersConclusion()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    auto *problem =
        qobject_cast<QLabel *>(dialog.findChild<QLabel *>(QStringLiteral("settingsProblem_threads")));

    dialog.draft()->setValue(QStringLiteral("threads"), 999);
    // 界面复述服务层的结论，而不是自己再算一遍：两处各算一次的话，
    // 「界面说有错、草稿说没错」会同时成立，而用户不知道信哪个。
    const QString fromService = dialog.problemTextFor(QStringLiteral("threads"));
    QVERIFY(!fromService.isEmpty());
    QCOMPARE(problem->text(), fromService);
    QVERIFY(!dialog.draft()->canApply());
    QCOMPARE(dialog.draft()->problems().size(), 1);
    QCOMPARE(dialog.problemTextFor(QStringLiteral("不存在的键")), QString());
}

void TstSettingsDialog::theDirtyTabIsMarkedByFontWeight()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    QCOMPARE(dialog.dirtyTabTitles(), QStringList());
    QVERIFY(!dialog.tabIsMarkedDirty(0));
    QVERIFY(!dialog.tabIsMarkedDirty(1));

    dialog.draft()->setValue(QStringLiteral("left-label"), QStringLiteral("旧"));

    // 脏标记落在**第二**张 Tab 上：如果实现不区分 Tab 而是一起加粗，这条会红。
    QVERIFY(!dialog.tabIsMarkedDirty(0));
    QVERIFY(dialog.tabIsMarkedDirty(1));
    QCOMPARE(dialog.dirtyTabTitles(), QStringList{QStringLiteral("显示")});

    // 文案里不许出现星号之类的标记：那会迫使每一处读标题的代码都去处理它。
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("settingsTabList"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("显示"));
}

void TstSettingsDialog::theDirtyMarkIsClearedAfterApply()
{
    RecordingSettings settings;
    GuardedDialog dialog(folderLikeSchema(), &settings);

    dialog.draft()->setValue(QStringLiteral("threads"), 8);
    QVERIFY(dialog.tabIsMarkedDirty(0));

    QVERIFY(dialog.applyChanges());
    QVERIFY(!dialog.tabIsMarkedDirty(0));
    QCOMPARE(dialog.dirtyTabTitles(), QStringList());

    // 改了再撤销（revert 由「放弃改动」触发）也要清干净。
    dialog.draft()->setValue(QStringLiteral("recursive"), false);
    QVERIFY(dialog.tabIsMarkedDirty(0));
    dialog.draft()->revert();
    QVERIFY(!dialog.tabIsMarkedDirty(0));
}

// --- E 反向验证 ---------------------------------------------------------------

void TstSettingsDialog::twoDifferentSchemasProduceTwoDifferentDialogs()
{
    RecordingSettings settingsA;
    SessionSettingsDialog folderDialog(folderLikeSchema(), &settingsA);
    QCOMPARE(folderDialog.tabCount(), 2);
    QCOMPARE(folderDialog.tabTitles(),
             (QStringList{QStringLiteral("扫描"), QStringLiteral("显示")}));
    QVERIFY(folderDialog.editorFor(QStringLiteral("recursive")));
    QVERIFY(folderDialog.titleLabelFor(QStringLiteral("recursive")));

    RecordingSettings settingsB;
    SessionSettingsDialog hexDialog(tinySchema(), &settingsB);
    // 换一份声明，界面必须跟着变。若「由声明生成」其实是「把合成声明的键写死在
    // 代码里」，这一个对话框会多出一个叫「包含子目录」的控件。
    QCOMPARE(hexDialog.tabCount(), 1);
    QCOMPARE(hexDialog.tabTitles(), QStringList{QStringLiteral("字节")});
    QVERIFY(!hexDialog.editorFor(QStringLiteral("recursive")));
    QVERIFY(!hexDialog.titleLabelFor(QStringLiteral("recursive")));
    QVERIFY(hexDialog.editorFor(QStringLiteral("columns")));
    QVERIFY(hexDialog.descriptionLabelFor(QStringLiteral("columns")));

    // 控件的数量也要跟着声明走。
    auto *page = hexDialog.findChild<QScrollArea *>(QStringLiteral("settingsTabPage_bytes"));
    QVERIFY(page);
    QCOMPARE(page->widget()->findChildren<QGroupBox *>().size(), 1);
}

void TstSettingsDialog::aKeyFromAnotherSchemaHasNoEditorHere()
{
    RecordingSettings settings;
    GuardedDialog dialog(tinySchema(), &settings);

    // 一份声明里没有的键，界面不该凭空造出控件来（否则「声明是唯一事实来源」
    // 这句话就不成立了）。`problemTextFor` 同样要对未知键给出空结论。
    QCOMPARE(dialog.editorFor(QStringLiteral("notes")), nullptr);
    QCOMPARE(dialog.titleLabelFor(QStringLiteral("notes")), nullptr);
    QVERIFY(dialog.problemTextFor(QStringLiteral("notes")).isEmpty());
    QCOMPARE(dialog.tabCount(), 1);
}

void TstSettingsDialog::theDialogSourceHardcodesNoSchemaKeys()
{
    // 「框架本身不硬编码任何具体设置项」是 SESS-006 的边界。这条护栏读源码，
    // 断言对话框的源文件里没有出现任何一份合成声明的键。
    const QString header = readSourceFile(QStringLiteral("/Views/Session/settingsdialog.h"));
    const QString source = readSourceFile(QStringLiteral("/Views/Session/settingsdialog.cpp"));
    QVERIFY(!header.isEmpty());
    QVERIFY(!source.isEmpty());

    const QStringList keys = schemaKeys();
    QVERIFY(hardcodedKeysIn(source, keys).isEmpty());
    QVERIFY(hardcodedKeysIn(header, keys).isEmpty());

    // 反向验证：把一段故意写坏的源码喂给同一个判定流程，它必须报出来。
    // 一个从不报错的护栏比没有护栏更糟——它会让人以为这块已经被守住了。
    const QString broken =
        QStringLiteral("if (key == \"recursive\") { /* 特殊处理 */ }");
    QCOMPARE(hardcodedKeysIn(broken, keys), QStringList{QStringLiteral("recursive")});
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstSettingsDialog)
