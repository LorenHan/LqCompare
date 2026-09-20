#ifndef LQCOMPARE_SESSION_SETTINGSCHEMA_H
#define LQCOMPARE_SESSION_SETTINGSCHEMA_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

#include "session.h"     // Services/Session —— 草稿最终要写回的那个接口
#include "sessiontype.h" // Services/Session —— 机器键的合法性规则与类型 ID 同源

namespace LqCompare {

///
/// \brief 一个设置项用哪种控件呈现（SESS-006 第 2 条：控件类型是声明的一部分）。
///
/// **为什么只有这六种**：这六种覆盖了会话设置的真实形状——开关（忽略大小写）、
/// 数值（上下文行数）、单行文本（编码名）、多行文本（语法说明）、枚举
/// （行尾符策略）、掩码清单（排除规则，复用 FILT-001 的语法）。刻意**没有**收录
/// 颜色与快捷键（它们是应用级设置，属 OPT-*，不随会话类型变化），也刻意**没有**
/// 收录路径选择器：一个「点了浏览却只能选文件」的路径控件比没有更糟，而会话设置里
/// 的路径项（语法文件、外部工具）都还没落地——等第一个真实需求出现时，按
/// 「选文件 / 选目录 / 两者皆可」一次做对，好过现在先做一个半成品。
///
/// 加一种控件的成本是明确的：这里加一个枚举值、`settingControlLabel()` 加一条文案、
/// 对话框的编辑器工厂加一个分支。三处，编译器会把它们指出来（`switch` 无 default 时
/// `-Wswitch` 会报警告），因此不会漏。
///
enum class SettingControl {
    Bool,          ///< 复选框
    Integer,       ///< 整数微调框（取值区间由校验规则的 minimum / maximum 驱动）
    Text,          ///< 单行文本
    MultilineText, ///< 多行文本
    Choice,        ///< 下拉（取值来自 `SettingItem::choices`）
    MaskList,      ///< 掩码清单（一行一条，语法即 `Services/Filter` 那套，FILT-001）
};

/// 控件类型的机器标识（写进会话文件时用它，而不是靠枚举序号——在中间插一个值
/// 会让所有旧文件的控件类型整体错位）。
const char *settingControlIdentifier(SettingControl control);

/// 控件类型的显示文案（诊断输出与界面提示用）。
QString settingControlLabel(SettingControl control);

///
/// \brief 设置的生效范围（SESS-006 第 1 条要求对话框底部有这个下拉）。
///
/// **本条目只负责「有这三个取值、各叫什么」**，三层优先级的覆盖链与落盘位置是
/// SESS-007（issue #42）。分成两步的理由与 SESS-001 当初把设置**接口**先落地
/// 一样：底面必须先有确定的名字，否则 SESS-007 会自己发明一套（常见的后果是
/// 界面写「当前视图」而数据层写「view」，两者靠一个 `if` 对齐）。
///
enum class SettingScope {
    View,    ///< 仅当前视图（关掉标签即丢弃）
    Session, ///< 当前会话的默认值
    Type,    ///< 该类型全部新会话的默认值
};

/// 作用域的机器标识，如 `"view"`。
const char *settingScopeIdentifier(SettingScope scope);

/// 作用域的显示文案，如「仅当前视图」。
QString settingScopeLabel(SettingScope scope);

/// 作用域的一句话说明（下拉项的 tooltip：要让用户一眼看出改动的去向）。
QString settingScopeDescription(SettingScope scope);

/// 三个作用域，按 `SettingScope` 的固定顺序（下拉的展示顺序）。
QVector<SettingScope> allSettingScopes();

///
/// \brief 枚举型设置项的一个取值。
///
struct SettingChoice
{
    QString value;       ///< 机器值，存进会话文件
    QString label;       ///< 显示文案
    QString description; ///< 一句话说明（下拉项 tooltip）

    /// 自我一致性：机器值与显示文案都必须非空。
    QString selfCheck() const;
};

///
/// \brief 一个设置项的校验规则（第 2 条点名要求的「校验规则」）。
///
/// **规则是声明式的枚举字段，不是一段正则**：正则看着更通用，但它把校验交给一段
/// 不可分析的代码——界面没法回答「这一项在等什么」，测试也没法逐条覆盖边界。
/// 另外 Qt 5.15 的 `QRegularExpression` **没有匹配超时**（`setMatchTimeout()` 是
/// Qt 6.0 才加的，FILT-002 因此被卡住），把用户输入喂给一条不可中断的匹配，
/// 等于给界面留了一个不可控的卡顿入口。这几种规则都是 O(字符串长度) 的。
///
/// `describe()` 把规则翻成一句人话，界面把它显示在标题旁或 tooltip 里——
/// 「请输入 1 到 200 之间的整数」比一个红色的框有用得多。
///
struct SettingValidation
{
    /// 不能为空。文本类指非空串，掩码清单指至少一条，枚举指必须选一个。
    bool required = false;

    /// 长度下界（文本类＝字符数，掩码清单＝条目数）。0 表示不限。
    int minLength = 0;

    /// 长度上界。**0 表示不限**——注意不是「长度必须为 0」。
    int maxLength = 0;

    /// 数值下界（只对 `Integer` 有效）。
    bool hasMinimum = false;
    int minimum = 0;

    /// 数值上界（只对 `Integer` 有效）。
    bool hasMaximum = false;
    int maximum = 0;

    /// 枚举取值必须在 `SettingItem::choices` 里。
    ///
    /// 默认**为真**，而且不建议关掉：一个不在取值表里的枚举值在界面上无法显示，
    /// 用户会看到下拉框空白而程序认为「已选好」。留这个开关只是为了让「允许用户
    /// 手输一个自定义值」这类需求将来有地方表达，而不是让每处都去关心它。
    bool mustBeListed = true;

    /// 是否声明了任何一条规则（界面据此决定要不要显示规则摘要）。
    bool hasAnyRule() const;

    /// 规则的人话摘要；没有任何规则时返回空串。
    ///
    /// 需要控件类型是因为长度的单位不一样：掩码清单数的是**条数**、文本数的是
    /// **字符数**。两种都写成「个字符」的话，用户会对着「最多 200 个字符」去数
    /// 自己那 3 条掩码，而真正越界的是条数。
    QString describe(SettingControl control) const;

    /// 自我一致性：上下界不许写反。
    QString selfCheck() const;
};

///
/// \brief 一个设置项的声明（标题、说明、控件类型、默认值、校验规则）。
///
/// 这四样加一个机器键，就是 SESS-006 第 2 条列的全部内容。界面完全由它生成，
/// 因此**框架里没有任何一处硬编码的设置项**——那是各会话类型自己的数据。
///
struct SettingItem
{
    /// 机器键，写进会话文件的那个名字。
    ///
    /// 规则与 `SessionType::id` **同一条**（`isValidSessionTypeId`）：小写字母开头，
    /// 其余小写字母、数字或连字符，形如 `ignore-case`、`tab-width`。两处各写一份
    /// 规则必然分家，而分家的现象是「存进文件的键读不回来」——最难归因的一类。
    QString key;

    /// 显示标题，如「忽略大小写」。
    QString title;

    /// 一句话说明。**必填**（`selfCheck()` 会报出来）：一个只有标题的设置项
    /// 在界面上等于没写——用户不知道勾了它会发生什么，只能去试。
    QString description;

    SettingControl control = SettingControl::Bool;

    /// 出厂默认值。类型必须与控件类型相符（`Bool` 要 `bool`、`Integer` 要整数、
    /// 掩码清单要 `QStringList`……），否则 `selfCheck()` 报错。
    QVariant defaultValue;

    /// `Choice` 的取值表。其他控件类型必须留空——留着一份用不上的取值表，
    /// 下一个人会以为它生效。
    QVector<SettingChoice> choices;

    SettingValidation validation;

    /// 是否属于「高级」项。界面默认把它折叠起来，但**不影响任何逻辑**：
    /// 读写、校验、脏判定对它一视同仁。
    bool advanced = false;

    /// 把任意来源的值收拢成本控件类型的规范形态。
    ///
    /// **为什么必须有这一步**：会话文件里存的多行掩码是**一段文本**，读回来是
    /// `QString`，而控件与默认值是 `QStringList`。不归一的话，「这个值等于默认值吗」
    /// 会永远为假——于是「恢复默认」看起来没生效、干净的表单被判定成有改动、
    /// 关窗口时白问一句。归一之后再比较，这些问题在结构上就不可能发生。
    QVariant normalized(const QVariant &value) const;

    /// 值（按规范形态比较）是否等于出厂默认值。
    bool isDefault(const QVariant &value) const;

    /// 校验一个值。通过返回**空串**，否则返回一句可以直接显示给用户的原因。
    ///
    /// 返回原因而不是 bool：界面要把「哪里不对、应该是什么」写在控件旁边，
    /// 只给 false 的话每个调用点都要再猜一遍原因，猜出来的还各不相同。
    QString validate(const QVariant &value) const;

    /// 自我一致性检查（由 `SettingsSchema::validate()` 统一收集）。
    QString selfCheck() const;

    /// 诊断用的一行摘要。
    QString describe() const;
};

/// 键是否合法（与类型 ID 同一条规则，理由见 `SettingItem::key`）。
bool isValidSettingKey(const QString &key);

///
/// \brief 一个设置分组（同一张 Tab 里的一个小节，界面用分组框呈现）。
///
struct SettingGroup
{
    /// 组 ID。只在所属 Tab 内要求唯一，因此界面可以只按它定位。
    QString id;

    QString title;
    QString description;

    QVector<SettingItem> items;

    /// 是否属于 Pro 版本。与 `SessionType::edition` 同源，
    /// 界面据此标注——**但不禁用**，因为本仓库目前没有版本判定（SESS 的
    /// `SessionEdition` 只是标注）。留这个字段是为了让界面不需要第二份名单。
    bool proFeature = false;

    const SettingItem *findItem(const QString &key) const;

    QString selfCheck() const;
};

///
/// \brief 一张设置 Tab（对话框左侧列表里的一个条目）。
///
/// 「Tab 按会话类型变化」（第 1 条）就是靠这里：一个会话类型的设置声明里
/// 有几张 Tab、各叫什么，全是数据。
///
struct SettingsTab
{
    /// Tab ID，在所属声明内唯一。界面用它做定位（也用于「记得上次停在哪张 Tab」）。
    QString id;

    QString title;
    QString description;

    QVector<SettingGroup> groups;

    /// 本 Tab 的全部设置项，按声明顺序（组顺序 → 组内顺序）。
    ///
    /// 返回的指针指向本对象内部，**在结构被改写或对象被拷贝之后失效**。
    /// 对话框在构造时一次性取完，因此这个约定在实践中成立：它换来的好处是
    /// 界面不必维护「第几组的第几项」这种二重下标。
    QVector<const SettingItem *> items() const;

    const SettingItem *findItem(const QString &key) const;

    /// 项数（不含组标题）。
    int itemCount() const;

    QString selfCheck() const;
};

///
/// \brief 一个会话类型的全部设置声明。
///
/// **框架不含任何具体设置项**：内置的 14 种会话类型目前一个声明都没有
/// （`SessionSettingsCatalog` 里是空的），因为「文本比对该有哪些设置」是
/// `TEXT-*` / `FOLD-*` 的产品决定，不是框架的一部分。这一点是有意留白的：
/// 先编一份看起来完整的设置表，等各类型真正落地时会被逐条质疑，
/// 而框架本身现在就可用——登记一份声明，对话框立刻有内容。
///
struct SettingsSchema
{
    /// 归属的会话类型 ID（`SessionType::id`）。**空表示通用声明**——
    /// 所有类型共用的设置放这里，由界面按需合并（合并规则属 SESS-007，
    /// 本条目只把「通用」这个取值表示出来，免得将来用一个假 ID 冒充它）。
    QString typeId;

    QVector<SettingsTab> tabs;

    /// 全部 Tab 的全部设置项，按声明顺序。
    QVector<const SettingItem *> items() const;

    int itemCount() const;

    /// 全表范围按键查找（跨 Tab、跨组）。
    const SettingItem *findItem(const QString &key) const;

    /// 某个键所在的 Tab；找不到返回 nullptr。
    const SettingsTab *tabOfItem(const QString &key) const;

    /// 某个键所在的 Tab 序号；找不到返回 -1。
    int tabIndexOfItem(const QString &key) const;

    /// 全部键，按声明顺序。**不排序**：顺序就是界面的呈现顺序，
    /// 排序会让「会话文件里的键顺序」与「界面顺序」成为两份事实。
    QStringList itemKeys() const;

    const SettingsTab *findTab(const QString &tabId) const;

    /// 自检：键重复、标题/说明缺失、默认值过不了自己的校验、取值表与控件类型
    /// 不匹配……返回**可直接写进日志**的问题清单（空列表表示干净）。
    ///
    /// 与 `SessionTypeRegistry::validate()` 同一个定位：查的是「手写这张表时
    /// 容易写错、写错了也不影响登记成功」的几件事。登记时就能拦住的（键格式、
    /// Tab ID 重复）在 `add()` 里拦，这里不再查一遍——一条永远不会红的护栏
    /// 比没有护栏更糟。
    QStringList validate() const;

    /// 诊断用的整表摘要。
    QString describe() const;
};

///
/// \brief 一条校验不通过记录。
///
struct SettingProblem
{
    QString key;
    QString tabId;
    QString message;

    QString describe() const;
};

///
/// \brief 设置对话框的「草稿」：用户改的是它，不是会话本身。
///
/// 把「用户正在编辑的值」与「会话当前生效的值」分开是这一条的关键设计：
///
///   - 「取消」= 丢掉草稿。若直接改会话，取消就得把每一项改回去，而
///     「改回去」这件事有一百种做错的方式（漏掉一项、顺序错、把用户原本就
///     设过的值当成默认值），且错的后果是用户点了取消、设置却变了；
///   - 「恢复默认」= 把草稿重置成出厂默认。若直接写会话，这一步立刻生效，
///     于是它成了一个**不可撤销**的动作——本仓库的纪律是破坏性操作必须可逆；
///   - 「脏」的判定天然落在草稿上：与**进入对话框时读到的值**比，而不是与
///     出厂默认比。用户什么都没改就不该被问「要不要保存」（第 3 条）；
///     而「改回默认值」是一次真实的改动，必须被算作改动。
///
/// 草稿不是 `SessionSettings` 的实现——它没有作用域、不落盘，也没有「键不存在」
/// 这个状态：每一项都有值（读不到就落默认值）。它的职责只有「暂存 + 判定 + 校验」。
///
class SettingsDraft : public QObject
{
    Q_OBJECT

public:
    explicit SettingsDraft(QObject *parent = nullptr);
    SettingsDraft(const SettingsSchema &schema, QObject *parent = nullptr);
    ~SettingsDraft() override;

    /// 换一份声明。会**丢弃全部改动**并重新载入（值先落默认值，等 `loadFrom`）。
    void setSchema(const SettingsSchema &schema);

    const SettingsSchema &schema() const { return m_schema; }

    /// 从会话当前设置里读一遍，作为「没改过」的基准。未设过的键落默认值。
    void loadFrom(const SessionSettings &settings);

    /// 当前草稿里的值（已按控件类型归一）。键不存在时返回空 QVariant。
    QVariant value(const QString &key) const;

    /// 写入草稿。**值没变返回 true 但什么也不发**——与 `SessionSettings::setValue`
    /// 同一条约定：界面把改动直接连到「会话变脏」，看一眼再确定关掉不该让它变脏。
    /// 空键返回 false。
    bool setValue(const QString &key, const QVariant &value);

    bool contains(const QString &key) const;

    /// 是否有「与载入时不同」的改动。
    bool isDirty() const;

    /// 有改动的键，按声明顺序（不是字典序：界面按声明顺序标记，两份顺序会错位）。
    QStringList dirtyKeys() const;

    bool isItemDirty(const QString &key) const;

    /// 某张 Tab 上有没有改动（第 3 条的判定依据）。
    bool isTabDirty(const QString &tabId) const;

    QStringList dirtyKeysInTab(const QString &tabId) const;

    /// 某个键的当前值是否等于出厂默认值。
    bool isDefault(const QString &key) const;

    /// 把一项重设为出厂默认值。值本来就等于默认时返回 true 且不发信号。
    bool resetToDefault(const QString &key);

    /// 把一张 Tab / 全部项重设为出厂默认值，返回**真正被改动**的项数。
    int resetTabToDefaults(const QString &tabId);
    int resetAllToDefaults();

    /// 丢弃全部改动，回到 `loadFrom` 那一刻的值。
    void revert();

    /// 逐项校验，返回不通过的项（按声明顺序）。
    QVector<SettingProblem> problems() const;

    /// 是否可以把草稿写回去（校验全过）。
    bool canApply() const;

    ///
    /// \brief 把改动写回会话设置。**全有或全无**。
    ///
    /// 只写**有改动**的键，因此调用方拿到的 `appliedKeys` 就是「这次应用影响了
    /// 什么」的准确答案。
    ///
    /// 校验不过、或本来就没有改动时返回 false 且**一个键都不写**。不做部分应用：
    /// 半应用的会话会让用户看到「有一部分生效、有一部分没有」，而错误提示只覆盖
    /// 被拦下的那一项，其余几项的命运完全看不出。与 PLAT-005 的安装「全有或全无」
    /// 同一个理由。
    ///
    /// 没有改动时返回 false 且不写——与 `CompareSession::save()` 一致，
    /// 让「什么都没改也点应用」不会在同步目录里留下一串无意义的版本。
    bool applyTo(SessionSettings *settings, QStringList *appliedKeys = nullptr);

    /// 诊断用摘要。
    QString describe() const;

signals:
    /// 某项的值确实变了（值相同时不发）。
    void valueChanged(const QString &key);

    /// 脏状态变化（只在真假翻转时发一次，而不是每次改动都发）。
    void dirtyChanged(bool dirty);

private:
    /// 记录值并比对，返回值是否真的变了。
    bool storeValue(const QString &key, const QVariant &value, bool *changed);

    void emitDirtyTransition(bool previous);

    SettingsSchema m_schema;
    QVariantMap m_values;     ///< 键 → 规范形态的值
    QVariantMap m_baseline;   ///< 键 → 载入时的值（脏判定的基准）
    QStringList m_order;      ///< 键的声明顺序（`QStringList` 而不是字典序）
};

///
/// \brief 未保存改动的询问（第 3 条：切换 Tab 或关闭对话框时给出确认）。
///
/// **为什么把「该不该问」抽成纯函数**：这是本条目里唯一一条既有交互又必须能
/// 反向验证的规则——「有改动才问」与「没改动也问」从代码上看只差一个判断，
/// 而用户体感是「这个对话框很烦」。放进服务层之后，无界面测试可以逐条断言
/// 选项集合与默认项，对话框那一层只剩「把它翻译成按钮」。
///
enum class SettingsInquiryReason {
    SwitchTab,   ///< 用户点了另一张 Tab
    CloseDialog, ///< 用户点了取消 / 关掉窗口 / 按了 Esc
};

/// 询问的选项。顺序即界面上的按钮顺序。
enum class SettingsChangeAction {
    Apply,   ///< 先应用，再继续
    Discard, ///< 丢弃改动，再继续
    Cancel,  ///< 什么都不做，留在原地
};

const char *settingsChangeActionIdentifier(SettingsChangeAction action);
QString settingsChangeActionLabel(SettingsChangeAction action);

struct SettingsInquiry
{
    /// 要不要问。没有改动时为假——**界面必须尊重它**：每次点标签都弹一次
    /// 「要不要保存」是本条目最容易犯的过度设计，而它的代价落在用户身上。
    bool ask = false;

    QString title;
    QString text;

    /// 可选项，顺序即按钮顺序。
    QVector<SettingsChangeAction> options;

    /// 默认项（回车 / 焦点所在）。**恒为 `Cancel`**：一个手快的回车不应该
    /// 丢掉用户刚敲进去的东西。
    SettingsChangeAction defaultAction = SettingsChangeAction::Cancel;

    QString describe() const;
};

///
/// \brief 生成一次询问。
///
/// `targetTabTitle` 只在 `SwitchTab` 时有意义（询问文案里要写明去哪张 Tab）。
///
/// **切换 Tab 不给「丢弃」这个出口**：切换 Tab 时草稿会原样带到下一张 Tab，
/// 没有任何东西会丢——给一个「丢弃」按钮等于凭空造出一条丢工作的路径。
/// 关闭对话框才必须给（否则用户没有别的出路）。这是「破坏性操作必须可逆」
/// 在交互上的落点：能不给的破坏性出口就不给。
///
SettingsInquiry inquiryForUnsavedChanges(SettingsInquiryReason reason,
                                         const QStringList &dirtyKeys,
                                         const QString &targetTabTitle = QString());

///
/// \brief 会话设置的声明目录：按类型 ID 查「这个类型有哪些设置」。
///
/// 与 `SessionTypeRegistry` 是**两回事**，刻意分开：注册表回答「有哪些类型、
/// 哪个文件该用哪个类型」，目录回答「某个类型的设置长什么样」。合成一个会让
/// 「查设置」的调用点把整张类型表也一起拉进来，而两者没有任何共同字段。
///
/// 同样不做单例（理由与 `SessionTypeRegistry` 一致：表要被测试反复构造）。
///
class SessionSettingsCatalog
{
public:
    SessionSettingsCatalog();

    /// 登记一份声明。校验：类型 ID 合法且未重复、声明本身通过 `validate()`。
    /// 任一不过就整条拒绝。
    bool add(const SettingsSchema &schema, QString *error = nullptr);

    void clear();

    bool isEmpty() const;
    int count() const;

    /// 按类型 ID 查（大小写敏感，与 `SessionTypeRegistry::find` 一致）。
    const SettingsSchema *find(const QString &typeId) const;

    /// `typeId` 为空的通用声明；没有时返回 nullptr。
    const SettingsSchema *common() const;

    /// 已登记的类型 ID，按登记顺序。
    QStringList typeIds() const;

    /// 自检：逐份声明的 `validate()` 汇总（跨声明的键冲突不查——
    /// 键带声明作用域，两个类型各有一个 `tab-width` 是完全正常的）。
    QStringList validate() const;

    QString describe() const;

private:
    QVector<SettingsSchema> m_schemas;
};

} // namespace LqCompare

#endif // LQCOMPARE_SESSION_SETTINGSCHEMA_H
