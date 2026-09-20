#ifndef LQCOMPARE_SESSION_SETTINGSDIALOG_H
#define LQCOMPARE_SESSION_SETTINGSDIALOG_H

#include <QDialog>
#include <QHash>
#include <QStringList>

#include <functional>

#include "settingschema.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QWidget;

namespace LqCompare {

///
/// \brief 询问处理器：拿到一次询问，返回用户的选择。
///
/// **为什么做成可替换的而不是直接弹 `QMessageBox`**：真实模态对话框在测试里要么
/// 挂住（`exec()` 不会返回），要么得靠 `QTimer` 去打补丁——后者让用例的成败依赖
/// 时序，是「偶发红」的经典来源。把「问一句、拿个答案」抽成一个可替换的函数之后，
/// 用例可以精确地断言「问了什么、给了哪几个选项、选了之后发生了什么」，
/// 而默认实现仍然是那个真的 QMessageBox（界面上不多一分人为）。
///
/// 与 `IconService` 的提供者、`RegistryStore` 的存储是同一个手法：把不可测的
/// 那一小块换成可替换的入口，其余逻辑照旧跑真实的实现。
///
using SettingsInquiryHandler = std::function<SettingsChangeAction(QWidget *, const SettingsInquiry &)>;

///
/// \brief 会话设置对话框（PRD: SESS-006）。
///
/// 结构就是第 1 条要求的那个形状：
///
///   ┌──────────────┬────────────────────────────────┐
///   │ Tab 列表     │ 当前 Tab 的内容                 │
///   │（按类型变化）│（由声明逐项生成，框架不硬编码）  │
///   ├──────────────┴────────────────────────────────┤
///   │ 改动保存到：[作用域▾]   恢复默认 应用 确定 取消 │
///   └───────────────────────────────────────────────┘
///
/// **界面完全由 `SettingsSchema` 生成**：这里没有任何一处写死「忽略大小写」之类的
/// 具体设置项。加一种会话类型、加一项设置，改的是数据（各会话类型的声明），
/// 不是这个文件。第 2 条要的就是这件事。
///
/// **草稿在服务层**（`SettingsDraft`）：对话框只做三件事——把声明画成控件、
/// 把用户的操作翻译成对草稿的读写、把草稿的结论翻译成按钮的可用状态。
/// 「脏」的判定、校验、全有或全无的应用都在服务层，因此它们能在无界面测试里
/// 被逐条覆盖（第 4 条）。
///
class SessionSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    ///
    /// \brief 构造对话框。
    ///
    /// `schema` 是要展示的设置声明，`target` 是「应用」时写回的那个会话设置。
    /// `target` 必须非空——没有写回目标的话，这个对话框只是一个可以编辑但
    /// 什么也存不下的表单，那不该由界面去假装支持。
    ///
    SessionSettingsDialog(const SettingsSchema &schema, SessionSettings *target,
                          QWidget *parent = nullptr);
    ~SessionSettingsDialog() override;

    SettingsDraft *draft() const { return m_draft; }
    const SettingsSchema &schema() const { return m_schema; }

    // --- 结构（测试与后续接线用） ---------------------------------------------

    int tabCount() const;
    int currentTabIndex() const;
    QString currentTabId() const;

    /// 左列表的文案，按 Tab 顺序（恒等于声明里的标题——脏标记用的是字重，
    /// 不往文案里塞星号：那样每次改动都要解析一遍字符串，而「脏」本来就有
    /// `isTabDirty()` 这个确定答案）。
    QStringList tabTitles() const;

    /// 有未应用改动的 Tab 标题，按声明顺序。
    QStringList dirtyTabTitles() const;

    /// 第 index 张 Tab 在左列表里是否被标成「有改动」（字重加粗）。
    bool tabIsMarkedDirty(int index) const;

    /// 作用域下拉的项数（恒为 `allSettingScopes().size()`）。
    int scopeCount() const;
    SettingScope scope() const;
    bool setScope(SettingScope scope);

    /// 某一项的编辑控件；找不到（键不存在）时返回 nullptr。
    QWidget *editorFor(const QString &key) const;

    /// 某一项当前的校验错误文案；通过校验时为空串。
    ///
    /// 与 `SettingItem::validate()` 是同一份判断——这里复述它的结论而不是
    /// 再算一遍，否则「界面说有错、草稿说没错」会同时成立。
    QString problemTextFor(const QString &key) const;

    /// 某一项的标题/说明标签（`settingsTitle_<键>` / `settingsDescription_<键>`）。
    QWidget *titleLabelFor(const QString &key) const;
    QWidget *descriptionLabelFor(const QString &key) const;

    // --- 动作（与底部四个按钮一一对应） ---------------------------------------

    /// 确定：有改动就先应用（校验不过则留在对话框里），然后关闭。
    void acceptChanges();

    /// 取消：有改动时先询问（第 3 条），用户选「返回」则什么都不做。
    void rejectChanges();

    /// 应用：把草稿写回会话。校验不过或无改动时返回 false 且什么都不写。
    bool applyChanges();

    /// 恢复默认：把**当前 Tab** 的设置项重置为出厂默认，返回真正被改动的项数。
    ///
    /// 只作用于当前 Tab 而不是整份声明：底部这个按钮贴着当前 Tab 的内容，
    /// 用户按它时的预期是「这一页恢复原样」。整表重置是更大范围的动作，
    /// 该有一个说清楚范围的入口（那是 OPT 设置页的事）。
    ///
    /// 重置落在**草稿**上，因此可以用「取消」撤销——「恢复默认」若直接写回会话，
    /// 它就成了一个不可撤销的动作。
    int restoreCurrentTabDefaults();

    /// 切到第 index 张 Tab。有改动时先询问（第 3 条）。
    /// 返回是否真的切过去了（用户选「返回」或应用失败时为假）。
    bool goToTab(int index);

    // --- 测试用的注入点与观察点 ----------------------------------------------

    void setInquiryHandler(SettingsInquiryHandler handler);

    /// 最近一次询问的内容（没有问过时 `ask` 为假）。
    const SettingsInquiry &lastInquiry() const { return m_lastInquiry; }

    /// 问过几次。用来断言「没有改动时不打扰用户」——只看响应而不断言次数的话，
    /// 「每次都弹一个、默认选返回」的实现同样能通过。
    int inquiryCount() const { return m_inquiryCount; }

signals:
    /// 一次成功的应用。`keys` 是被写回的键，按声明顺序。
    void applied(const QStringList &keys);

    /// 当前 Tab 变了（用户切过去之后才发）。
    void tabChanged(const QString &tabId);

protected:
    /// Esc、取消按钮与窗口关闭键都走这里。覆写它（而不是 `closeEvent`）是为了让
    /// 三条路径**只有一份**判断：分开写的话，三处必然有一处漏掉询问，
    /// 而漏掉的那条路径正是最容易被用户误触的（Esc）。
    void reject() override;

private:
    QWidget *buildTabPage(const SettingsTab &tab);
    QWidget *buildRow(const SettingItem &item);
    QWidget *buildEditor(const SettingItem &item);
    void writeEditor(const SettingItem &item, const QVariant &value);
    QVariant readEditor(const SettingItem &item) const;
    void setCurrentTabIndex(int index);

    void onDraftValueChanged(const QString &key);
    void onDraftDirtyChanged(bool dirty);
    void refreshDirtyMarkers();
    void refreshProblems();
    void refreshButtons();
    void refreshItemProblem(const SettingItem &item);

    /// 走一次询问。调用方必须先看过 `SettingsInquiry::ask`（没有改动时不该问），
    /// 因此这里不再自己判断——判断写在 `confirmPendingChanges()` 一处。
    SettingsChangeAction ask(const SettingsInquiry &inquiry);

    /// 未保存改动的统一处置：没有改动就直接放行；有改动则询问，按用户的答案
    /// 应用 / 丢弃 / 拒绝。返回「是否可以继续」（切页或关闭）。
    ///
    /// 切换 Tab 与关闭对话框共用它：两边各写一遍的话，其中一处必然漏掉某个分支
    /// （「先应用」失败时要留下、「放弃改动」要先把草稿丢掉），而漏掉的表现是
    /// 「选了先应用，改动没生效但窗口关了」。
    bool confirmPendingChanges(SettingsInquiryReason reason, const QString &targetTabTitle);

    SettingsSchema m_schema;
    SessionSettings *m_target = nullptr;
    SettingsDraft *m_draft = nullptr;

    QListWidget *m_tabList = nullptr;
    QStackedWidget *m_stack = nullptr;
    QComboBox *m_scopeCombo = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QHash<QString, QWidget *> m_editors;
    QHash<QString, QLabel *> m_titles;
    QHash<QString, QLabel *> m_descriptions;
    QHash<QString, QLabel *> m_problemLabels;

    SettingsInquiryHandler m_inquiryHandler;
    SettingsInquiry m_lastInquiry;
    int m_inquiryCount = 0;

    /// 正在从控件回写草稿的那个键。用来避免「回写草稿 → 草稿发信号 → 又把值写回
    /// 同一个控件」这条回路：它会让光标在用户打字时跳到末尾，而现象是
    /// 「输入框里打字总跳到行尾」，看起来像输入法的问题。
    QString m_editingKey;

    /// 正在从草稿回填控件。期间忽略控件的变更信号，理由同上。
    bool m_loading = false;
};

} // namespace LqCompare

#endif // LQCOMPARE_SESSION_SETTINGSDIALOG_H
