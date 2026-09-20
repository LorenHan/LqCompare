#include "settingsdialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <limits>

namespace LqCompare {

namespace {

/// 控件的对象名。测试要按名字找到底部那四个按钮与作用域下拉，而**不给它们
/// 专门的 getter** 是有意的：getter 只能证明「有这么个成员」，对象名能证明
/// 「它真的被放进了布局里」——顺序与位置是第 1 条完成标准的一部分。
QString buttonObjectName(const char *suffix)
{
    return QStringLiteral("settings%1").arg(QString::fromLatin1(suffix));
}

/// 说明文字的统一样式（次要信息，随主题走）。
QString secondaryStyle()
{
    return QStringLiteral("color: palette(mid);");
}

} // namespace

SessionSettingsDialog::SessionSettingsDialog(const SettingsSchema &schema, SessionSettings *target,
                                             QWidget *parent)
    : QDialog(parent)
    , m_schema(schema)
    , m_target(target)
{
    setWindowTitle(QStringLiteral("会话设置"));
    setObjectName(QStringLiteral("sessionSettingsDialog"));

    // 草稿是子对象：对话框析构时一起走，不会把「用户还没应用的改动」泄漏到别处。
    m_draft = new SettingsDraft(m_schema, this);

    // **先读会话当前的设置，再建界面**。反过来的话，各项控件会先按出厂默认值
    // 建好，而那时的 `loadFrom()` 发出的 `valueChanged` 还没有人接（信号连接在
    // 建完界面之后），于是界面上显示的全是默认值——用户会以为自己的设置丢了，
    // 然后点「确定」，把默认值真的写回去。
    m_draft->loadFrom(*m_target);

    auto *outer = new QVBoxLayout(this);

    // --- 上半部分：左 Tab 列表 + 右 内容 -----------------------------------
    auto *body = new QHBoxLayout;
    outer->addLayout(body, 1);

    m_tabList = new QListWidget(this);
    m_tabList->setObjectName(QStringLiteral("settingsTabList"));
    m_tabList->setFixedWidth(170);
    body->addWidget(m_tabList);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("settingsTabStack"));
    body->addWidget(m_stack, 1);

    for (const SettingsTab &tab : m_schema.tabs) {
        m_tabList->addItem(tab.title);
        m_stack->addWidget(buildTabPage(tab));
    }

    // --- 底部：作用域下拉 + 四个按钮 ---------------------------------------
    auto *footer = new QHBoxLayout;
    outer->addLayout(footer);

    auto *scopeLabel = new QLabel(QStringLiteral("改动保存到："), this);
    footer->addWidget(scopeLabel);

    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->setObjectName(QStringLiteral("settingsScopeCombo"));
    for (SettingScope scope : allSettingScopes()) {
        m_scopeCombo->addItem(settingScopeLabel(scope), static_cast<int>(scope));
        // 每个项都带上说明：光看「仅当前视图」用户判断不出改动会不会留到下次打开。
        m_scopeCombo->setItemData(m_scopeCombo->count() - 1, settingScopeDescription(scope),
                                  Qt::ToolTipRole);
    }
    footer->addWidget(m_scopeCombo);
    footer->addStretch(1);

    m_restoreButton = new QPushButton(QStringLiteral("恢复默认"), this);
    m_restoreButton->setObjectName(buttonObjectName("RestoreDefaultsButton"));
    m_restoreButton->setToolTip(QStringLiteral("把当前页的设置项恢复为出厂默认（可以用「取消」撤销）"));
    footer->addWidget(m_restoreButton);

    m_applyButton = new QPushButton(QStringLiteral("应用"), this);
    m_applyButton->setObjectName(buttonObjectName("ApplyButton"));
    footer->addWidget(m_applyButton);

    m_okButton = new QPushButton(QStringLiteral("确定"), this);
    m_okButton->setObjectName(buttonObjectName("OkButton"));
    m_okButton->setDefault(true);
    footer->addWidget(m_okButton);

    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setObjectName(buttonObjectName("CancelButton"));
    footer->addWidget(m_cancelButton);

    // 按钮 → 动作。四个出口都是公开的成员函数而不是匿名的 lambda：测试与后续的
    // 接线可以直接调用它们（例如命令行的 `--settings` 打开后自动应用），
    // 而按钮仍然走同一条路径，两处不可能出现「按钮做了别的事」。
    connect(m_restoreButton, &QPushButton::clicked, this, [this] { restoreCurrentTabDefaults(); });
    connect(m_applyButton, &QPushButton::clicked, this, [this] { applyChanges(); });
    connect(m_okButton, &QPushButton::clicked, this, [this] { acceptChanges(); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { rejectChanges(); });

    // Tab 列表 → 切页。**必须走 `goToTab()`**，不能直接把行号连到 `setCurrentTabIndex`：
    // `currentRowChanged` 是「已经切了」之后才发的，那时再问已经晚了——用户会被问
    // 一句然后仍然停在新 Tab 上（选「返回」也没用）。因此这里改成：先按新行去
    // 征询，用户拒绝就把列表的选择**拨回**当前 Tab 那一行。
    connect(m_tabList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row == m_stack->currentIndex()) {
            return;
        }
        if (!goToTab(row)) {
            // 用 QSignalBlocker：拨回去这件事本身不该再触发一次询问。
            const QSignalBlocker blocker(m_tabList);
            m_tabList->setCurrentRow(m_stack->currentIndex());
        }
    });

    connect(m_draft, &SettingsDraft::valueChanged, this, &SessionSettingsDialog::onDraftValueChanged);
    connect(m_draft, &SettingsDraft::dirtyChanged, this, &SessionSettingsDialog::onDraftDirtyChanged);

    if (tabCount() > 0) {
        setCurrentTabIndex(0);
    }
    refreshDirtyMarkers();
    refreshProblems();
    refreshButtons();
}

SessionSettingsDialog::~SessionSettingsDialog() = default;

// -----------------------------------------------------------------------------
// 界面生成
// -----------------------------------------------------------------------------

QWidget *SessionSettingsDialog::buildTabPage(const SettingsTab &tab)
{
    // 每一项都要能承载一段说明与一句错误，页面容易变长，因此套一层滚动区。
    // 不套的话，设置项多的类型会把底部的作用域下拉与按钮挤出窗口——而那些
    // 是第 1 条点名要求的控件，不能被内容顶掉。
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("settingsTabPage_%1").arg(tab.id));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *page = new QWidget(scroll);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    for (const SettingGroup &group : tab.groups) {
        auto *box = new QGroupBox(group.title, page);
        box->setObjectName(QStringLiteral("settingsGroup_%1").arg(group.id));
        box->setToolTip(group.description);
        // Pro 标注只是标注，不禁用：本仓库目前没有版本判定（SESS 的 SessionEdition
        // 只是标注），拿它去禁用会让 Standard 用户看到一个点不动的设置项，
        // 而报告里没有地方解释为什么。
        if (group.proFeature) {
            box->setTitle(QStringLiteral("%1（Pro）").arg(group.title));
        }

        auto *grid = new QGridLayout(box);
        int row = 0;
        if (!group.description.isEmpty()) {
            auto *groupNote = new QLabel(group.description, box);
            groupNote->setObjectName(QStringLiteral("settingsGroupNote_%1").arg(group.id));
            groupNote->setStyleSheet(secondaryStyle());
            groupNote->setWordWrap(true);
            grid->addWidget(groupNote, row, 0, 1, 2);
            ++row;
        }
        for (const SettingItem &item : group.items) {
            grid->addWidget(buildRow(item), row, 0, 1, 2);
            ++row;
        }
        pageLayout->addWidget(box);
    }
    pageLayout->addStretch(1);

    scroll->setWidget(page);
    return scroll;
}

QWidget *SessionSettingsDialog::buildRow(const SettingItem &item)
{
    auto *row = new QWidget(this);
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(item.title, row);
    title->setObjectName(QStringLiteral("settingsTitle_%1").arg(item.key));
    title->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (item.advanced) {
        // 「高级」只在标题后加一个标记，**不折叠**。折叠会让用户怀疑「这一项到底
        // 有没有生效」（它明明在声明里、值也确实参与比对），而本条目要求的只是
        // 「声明里有这个字段」，界面按最保守的方式把它呈现出来就够了。
        title->setText(QStringLiteral("%1（高级）").arg(item.title));
    }
    grid->addWidget(title, 0, 0);
    m_titles.insert(item.key, title);

    auto *editor = buildEditor(item);
    grid->addWidget(editor, 0, 1);
    m_editors.insert(item.key, editor);

    auto *description = new QLabel(item.description, row);
    description->setObjectName(QStringLiteral("settingsDescription_%1").arg(item.key));
    description->setStyleSheet(secondaryStyle());
    description->setWordWrap(true);
    grid->addWidget(description, 1, 1);
    m_descriptions.insert(item.key, description);

    auto *problem = new QLabel(row);
    problem->setObjectName(QStringLiteral("settingsProblem_%1").arg(item.key));
    problem->setStyleSheet(QStringLiteral("color: palette(bright-text);"));
    problem->setWordWrap(true);
    problem->hide();
    grid->addWidget(problem, 2, 1);
    m_problemLabels.insert(item.key, problem);

    // 从草稿回填初值。此刻草稿已经 `loadFrom` 过（构造顺序见构造函数），
    // 因此这里拿到的就是会话当前的值。
    writeEditor(item, m_draft->value(item.key));
    return row;
}

QWidget *SessionSettingsDialog::buildEditor(const SettingItem &item)
{
    const QString key = item.key;
    auto commit = [this, key](const QVariant &value) {
        if (m_loading) {
            return;
        }
        m_editingKey = key;
        m_draft->setValue(key, value);
        m_editingKey.clear();
    };

    switch (item.control) {
    case SettingControl::Bool: {
        auto *box = new QCheckBox(this);
        box->setObjectName(QStringLiteral("settingsEditor_%1").arg(key));
        connect(box, &QCheckBox::toggled, this, [commit](bool checked) { commit(checked); });
        return box;
    }
    case SettingControl::Integer: {
        auto *spin = new QSpinBox(this);
        spin->setObjectName(QStringLiteral("settingsEditor_%1").arg(key));
        // **必须显式设范围**：`QSpinBox` 的默认范围是 0..99，用户想填 200 会被
        // 静静夹到 99——「看起来能用、实际改了用户的输入」是最难发现的一类缺陷。
        // 声明了上下界就用它（校验规则顺手变成了界面约束），没声明就用整型全域。
        const int low = item.validation.hasMinimum ? item.validation.minimum
                                                   : std::numeric_limits<int>::min() / 2;
        const int high = item.validation.hasMaximum ? item.validation.maximum
                                                    : std::numeric_limits<int>::max() / 2;
        spin->setRange(low, high);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [commit](int value) { commit(value); });
        return spin;
    }
    case SettingControl::Text: {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("settingsEditor_%1").arg(key));
        // 输入的硬上限从校验规则里取：先挡住超长输入，用户就不会敲完 500 个字符
        // 才被告知最多 200 个。校验仍然会在值上再判一次（会话文件里也可能有
        // 超长的旧值），两处不是重复而是「输入约束」与「取值校验」两件事。
        if (item.validation.maxLength > 0) {
            edit->setMaxLength(item.validation.maxLength);
        }
        connect(edit, &QLineEdit::textChanged, this,
                [commit](const QString &text) { commit(text); });
        return edit;
    }
    case SettingControl::MultilineText: {
        auto *edit = new QPlainTextEdit(this);
        edit->setObjectName(QStringLiteral("settingsEditor_%1").arg(key));
        edit->setTabChangesFocus(true);
        connect(edit, &QPlainTextEdit::textChanged, this,
                [edit, commit] { commit(edit->toPlainText()); });
        return edit;
    }
    case SettingControl::Choice: {
        auto *combo = new QComboBox(this);
        combo->setObjectName(QStringLiteral("settingsEditor_%1").arg(key));
        for (const SettingChoice &choice : item.choices) {
            combo->addItem(choice.label, choice.value);
            if (!choice.description.isEmpty()) {
                combo->setItemData(combo->count() - 1, choice.description, Qt::ToolTipRole);
            }
        }
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [combo, commit](int index) { commit(combo->itemData(index)); });
        return combo;
    }
    case SettingControl::MaskList: {
        auto *edit = new QPlainTextEdit(this);
        edit->setObjectName(QStringLiteral("settingsEditor_%1").arg(key));
        edit->setPlaceholderText(QStringLiteral("每行一条掩码；`-` 开头表示排除"));
        edit->setTabChangesFocus(true);
        connect(edit, &QPlainTextEdit::textChanged, this,
                [edit, commit] { commit(edit->toPlainText()); });
        return edit;
    }
    }
    return new QLabel(QStringLiteral("不支持的控件类型"), this);
}

// -----------------------------------------------------------------------------
// 控件 ↔ 草稿
// -----------------------------------------------------------------------------

void SessionSettingsDialog::writeEditor(const SettingItem &item, const QVariant &value)
{
    QWidget *editor = m_editors.value(item.key);
    if (!editor) {
        return;
    }
    // 回填期间抑制变更信号：否则「写控件 → 控件发信号 → 回写草稿 → 再写控件」
    // 会形成回路，而它在界面上表现为「光标每次都被移到末尾」。
    m_loading = true;
    const QVariant v = item.normalized(value);
    switch (item.control) {
    case SettingControl::Bool:
        if (auto *box = qobject_cast<QCheckBox *>(editor)) {
            box->setChecked(v.toBool());
        }
        break;
    case SettingControl::Integer:
        if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            spin->setValue(v.toInt());
        }
        break;
    case SettingControl::Text:
        if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
            edit->setText(v.toString());
        }
        break;
    case SettingControl::MultilineText:
        if (auto *edit = qobject_cast<QPlainTextEdit *>(editor)) {
            edit->setPlainText(v.toString());
        }
        break;
    case SettingControl::Choice:
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            const int index = combo->findData(v.toString());
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
        }
        break;
    case SettingControl::MaskList:
        if (auto *edit = qobject_cast<QPlainTextEdit *>(editor)) {
            edit->setPlainText(v.toStringList().join(QLatin1Char('\n')));
        }
        break;
    }
    m_loading = false;
}

QVariant SessionSettingsDialog::readEditor(const SettingItem &item) const
{
    QWidget *editor = m_editors.value(item.key);
    if (!editor) {
        return QVariant();
    }
    switch (item.control) {
    case SettingControl::Bool:
        if (auto *box = qobject_cast<QCheckBox *>(editor)) {
            return box->isChecked();
        }
        break;
    case SettingControl::Integer:
        if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            return spin->value();
        }
        break;
    case SettingControl::Text:
        if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
            return edit->text();
        }
        break;
    case SettingControl::MultilineText:
        if (auto *edit = qobject_cast<QPlainTextEdit *>(editor)) {
            return edit->toPlainText();
        }
        break;
    case SettingControl::Choice:
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            return combo->currentData();
        }
        break;
    case SettingControl::MaskList:
        if (auto *edit = qobject_cast<QPlainTextEdit *>(editor)) {
            // 返回**原始文本**而不是自己切好的清单：归一化只有一份实现
            // （`SettingItem::normalized`），在界面里再切一次就成了两份，
            // 而两份的差异表现是「界面上看着一样、存进去的值不一样」。
            return edit->toPlainText();
        }
        break;
    }
    return QVariant();
}

// -----------------------------------------------------------------------------
// 结构查询
// -----------------------------------------------------------------------------

int SessionSettingsDialog::tabCount() const
{
    return m_schema.tabs.size();
}

int SessionSettingsDialog::currentTabIndex() const
{
    return m_stack ? m_stack->currentIndex() : -1;
}

QString SessionSettingsDialog::currentTabId() const
{
    const int index = currentTabIndex();
    if (index < 0 || index >= m_schema.tabs.size()) {
        return QString();
    }
    return m_schema.tabs.at(index).id;
}

QStringList SessionSettingsDialog::tabTitles() const
{
    QStringList titles;
    for (const SettingsTab &tab : m_schema.tabs) {
        titles << tab.title;
    }
    return titles;
}

QStringList SessionSettingsDialog::dirtyTabTitles() const
{
    QStringList titles;
    for (const SettingsTab &tab : m_schema.tabs) {
        if (m_draft->isTabDirty(tab.id)) {
            titles << tab.title;
        }
    }
    return titles;
}

bool SessionSettingsDialog::tabIsMarkedDirty(int index) const
{
    if (!m_tabList || index < 0 || index >= m_tabList->count()) {
        return false;
    }
    return m_tabList->item(index)->font().bold();
}

int SessionSettingsDialog::scopeCount() const
{
    return m_scopeCombo ? m_scopeCombo->count() : 0;
}

SettingScope SessionSettingsDialog::scope() const
{
    if (!m_scopeCombo) {
        return SettingScope::Session;
    }
    return static_cast<SettingScope>(m_scopeCombo->currentData().toInt());
}

bool SessionSettingsDialog::setScope(SettingScope scope)
{
    if (!m_scopeCombo) {
        return false;
    }
    const int index = m_scopeCombo->findData(static_cast<int>(scope));
    if (index < 0) {
        return false;
    }
    m_scopeCombo->setCurrentIndex(index);
    return true;
}

QWidget *SessionSettingsDialog::editorFor(const QString &key) const
{
    return m_editors.value(key);
}

QString SessionSettingsDialog::problemTextFor(const QString &key) const
{
    const SettingItem *item = m_schema.findItem(key);
    if (!item) {
        return QString();
    }
    return item->validate(m_draft->value(key));
}

QWidget *SessionSettingsDialog::titleLabelFor(const QString &key) const
{
    return m_titles.value(key);
}

QWidget *SessionSettingsDialog::descriptionLabelFor(const QString &key) const
{
    return m_descriptions.value(key);
}

// -----------------------------------------------------------------------------
// 动作
// -----------------------------------------------------------------------------

bool SessionSettingsDialog::applyChanges()
{
    QStringList keys;
    if (!m_draft->applyTo(m_target, &keys)) {
        // 两种失败原因都**不弹对话框**：校验不通过时错误已经逐项写在控件旁边了
        // （再弹一次只是重复），而没有改动时本来就不该打扰用户。
        refreshProblems();
        refreshButtons();
        return false;
    }
    refreshProblems();
    refreshDirtyMarkers();
    refreshButtons();
    emit applied(keys);
    return true;
}

int SessionSettingsDialog::restoreCurrentTabDefaults()
{
    const QString tabId = currentTabId();
    if (tabId.isEmpty()) {
        return 0;
    }
    const int changed = m_draft->resetTabToDefaults(tabId);
    refreshProblems();
    refreshDirtyMarkers();
    refreshButtons();
    return changed;
}

void SessionSettingsDialog::acceptChanges()
{
    // 有改动就先应用；校验不过就留在对话框里（错误已经标在对应项旁边），
    // 用户不会在「以为保存了」的情况下把改动弄丢。
    if (m_draft->isDirty() && !applyChanges()) {
        return;
    }
    QDialog::accept();
}

void SessionSettingsDialog::rejectChanges()
{
    reject();
}

void SessionSettingsDialog::reject()
{
    if (!confirmPendingChanges(SettingsInquiryReason::CloseDialog, QString())) {
        return; // 用户选了「返回」，或应用失败（校验不过）——留在对话框里
    }
    // 显式调基类版本：本函数就是 `reject()`，直接写 `reject()` 会递归。
    QDialog::reject();
}

bool SessionSettingsDialog::goToTab(int index)
{
    if (index < 0 || index >= m_schema.tabs.size()) {
        return false;
    }
    if (index == currentTabIndex()) {
        return true;
    }
    if (!confirmPendingChanges(SettingsInquiryReason::SwitchTab, m_schema.tabs.at(index).title)) {
        return false;
    }
    setCurrentTabIndex(index);
    return true;
}

bool SessionSettingsDialog::confirmPendingChanges(SettingsInquiryReason reason,
                                                  const QString &targetTabTitle)
{
    const SettingsInquiry inquiry =
        inquiryForUnsavedChanges(reason, m_draft->dirtyKeys(), targetTabTitle);
    if (!inquiry.ask) {
        // 没有改动就不问、直接继续。这是 `inquiryForUnsavedChanges` 的结论，
        // 界面必须尊重它——每次切页都弹一次「要不要保存」是本条目最容易犯的
        // 过度设计，代价全落在用户身上。记录一下是为了让 `lastInquiry()` 也能
        // 回答「刚才为什么没问」。
        m_lastInquiry = inquiry;
        return true;
    }

    // 切换 Tab 与关闭对话框**共用这一份判断**：两边各写一遍的话，其中一处必然
    // 漏掉某个分支（「先应用」失败时要留下、「放弃改动」要先把草稿丢掉），
    // 而漏掉的表现是「选了先应用，改动没生效但窗口关了」。
    switch (ask(inquiry)) {
    case SettingsChangeAction::Apply:
        // 应用失败（校验不过）时**不许继续**：用户会以为改动已经保存了。
        return applyChanges();
    case SettingsChangeAction::Discard:
        // 切换 Tab 的默认询问里不给这个出口（草稿会原样带过去，没有东西会丢）。
        // 但处理器是可注入的，注入的实现可能返回它，因此这里仍要有确定的行为：
        // 丢弃 = 回到载入时的值。悄悄忽略返回值才是真的危险。
        m_draft->revert();
        return true;
    case SettingsChangeAction::Cancel:
        return false;
    }
    return false;
}

void SessionSettingsDialog::setCurrentTabIndex(int index)
{
    if (m_tabList && m_tabList->currentRow() != index) {
        // 直接设行会再次触发 `currentRowChanged`，那一次会看到「已经在目标行」
        // 而立即返回——不会绕回来，因为 goToTab 里的相等判断在最前面。
        const QSignalBlocker blocker(m_tabList);
        m_tabList->setCurrentRow(index);
    }
    if (m_stack) {
        m_stack->setCurrentIndex(index);
    }
    refreshDirtyMarkers();
    const QString tabId = currentTabId();
    if (!tabId.isEmpty()) {
        emit tabChanged(tabId);
    }
}

SettingsChangeAction SessionSettingsDialog::ask(const SettingsInquiry &inquiry)
{
    m_lastInquiry = inquiry;
    ++m_inquiryCount;
    if (m_inquiryHandler) {
        return m_inquiryHandler(this, inquiry);
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(inquiry.title);
    box.setText(inquiry.text);
    // 按钮 → 动作的映射用一张表，而不是事后按文字或序号去反推：文案会改、
    // 按钮顺序来自数据，反推出来的结论在改动之后会静默错位（把「放弃改动」
    // 认成「先应用」的后果是用户以为丢了、其实保存了）。
    QHash<QAbstractButton *, SettingsChangeAction> mapping;
    QPushButton *defaultButton = nullptr;
    for (SettingsChangeAction action : inquiry.options) {
        QPushButton *button = box.addButton(settingsChangeActionLabel(action),
                                            QMessageBox::AcceptRole);
        mapping.insert(button, action);
        if (action == inquiry.defaultAction) {
            defaultButton = button;
        }
    }
    if (defaultButton) {
        // 默认项落在「返回」上：一个手快的回车不应该丢掉用户刚敲进去的东西。
        box.setDefaultButton(defaultButton);
    }
    box.exec();

    if (QAbstractButton *clicked = box.clickedButton()) {
        const auto found = mapping.constFind(clicked);
        if (found != mapping.constEnd()) {
            return found.value();
        }
    }
    // 认不出点击了哪个按钮（用户直接关掉了消息框）时退回默认项，而不是猜一个——
    // 猜错的那次会丢掉用户的改动。
    return inquiry.defaultAction;
}

// -----------------------------------------------------------------------------
// 刷新
// -----------------------------------------------------------------------------

void SessionSettingsDialog::onDraftValueChanged(const QString &key)
{
    const SettingItem *item = m_schema.findItem(key);
    if (!item) {
        return;
    }
    if (key != m_editingKey) {
        // 不是用户正在编辑的那一项（例如「恢复默认」批量改了值），把控件刷新过来。
        writeEditor(*item, m_draft->value(key));
    }
    refreshItemProblem(*item);
    refreshDirtyMarkers();
    refreshButtons();
}

void SessionSettingsDialog::onDraftDirtyChanged(bool dirty)
{
    Q_UNUSED(dirty);
    refreshDirtyMarkers();
    refreshButtons();
}

void SessionSettingsDialog::refreshDirtyMarkers()
{
    if (!m_tabList) {
        return;
    }
    for (int i = 0; i < m_schema.tabs.size() && i < m_tabList->count(); ++i) {
        const SettingsTab &tab = m_schema.tabs.at(i);
        const bool dirty = m_draft->isTabDirty(tab.id);
        QListWidgetItem *entry = m_tabList->item(i);
        QFont font = entry->font();
        // 脏标记用**字重**而不是往标题里塞星号：星号会迫使每一次读取标题的代码
        // 都去处理它（漏掉一处就是「Tab 名字里带了一个 *」），而「这一页有没有
        // 改动」本来就有 `isTabDirty()` 这个确定答案。
        if (font.bold() != dirty) {
            font.setBold(dirty);
            entry->setFont(font);
        }
        entry->setToolTip(dirty ? QStringLiteral("这一页有未应用的改动") : tab.description);
    }
}

void SessionSettingsDialog::refreshProblems()
{
    for (const SettingItem *item : m_schema.items()) {
        refreshItemProblem(*item);
    }
}

void SessionSettingsDialog::refreshItemProblem(const SettingItem &item)
{
    QLabel *label = m_problemLabels.value(item.key);
    if (!label) {
        return;
    }
    const QString message = item.validate(m_draft->value(item.key));
    if (message.isEmpty()) {
        label->clear();
        label->hide();
        return;
    }
    label->setText(message);
    label->show();
}

void SessionSettingsDialog::refreshButtons()
{
    if (m_applyButton) {
        // 没有改动或校验不过时把「应用」灰掉，而不是让它点了没反应：
        // 一个点了什么都不发生的按钮会让人以为程序卡了。两种原因都不灰「确定」——
        // 没改动时「确定」的语义就是「关闭」。
        m_applyButton->setEnabled(m_draft->isDirty() && m_draft->canApply());
    }
    if (m_restoreButton) {
        const SettingsTab *tab = currentTabIndex() >= 0 && currentTabIndex() < m_schema.tabs.size()
                                     ? &m_schema.tabs.at(currentTabIndex())
                                     : nullptr;
        bool anyNotDefault = false;
        if (tab) {
            const QVector<const SettingItem *> tabItems = tab->items();
            for (const SettingItem *item : tabItems) {
                if (!m_draft->isDefault(item->key)) {
                    anyNotDefault = true;
                    break;
                }
            }
        }
        m_restoreButton->setEnabled(anyNotDefault);
    }
    if (m_okButton) {
        m_okButton->setEnabled(true);
    }
}

void SessionSettingsDialog::setInquiryHandler(SettingsInquiryHandler handler)
{
    m_inquiryHandler = std::move(handler);
}

} // namespace LqCompare
