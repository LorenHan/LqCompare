#ifndef LQCOMPARE_SESSION_SETTINGSCOPE_H
#define LQCOMPARE_SESSION_SETTINGSCOPE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "session.h"       // Services/Session —— 本类实现的那个接口
#include "settingschema.h" // Services/Session —— SettingScope 与出厂默认的来源

namespace LqCompare {

///
/// \brief 三层作用域按**优先级**的顺序（高 → 低）：视图 > 会话 > 类型。
///
/// `allSettingScopes()`（SESS-006）是**下拉的展示顺序**，本函数是**解析顺序**。
/// 两者现在恰好相同，但它们是两件事：哪天产品决定把下拉排成「类型 / 会话 / 视图」
/// 以适应从大到小的心理模型，只有这个函数跟着改，解析逻辑一行都不用动。
/// 合成一个常量的话，那次调整会顺手把优先级也反过来，而现象是「视图级改动
/// 不生效了」——没有人会去怀疑一个下拉的排序。
///
QVector<SettingScope> settingScopePriorityOrder();

/// 该作用域在覆盖链里的排位：0 最高（视图），2 最低（类型）。
int settingScopeRank(SettingScope scope);

///
/// \brief 带作用域链的会话设置：视图 → 会话 → 类型 → 出厂默认（PRD: SESS-007）。
///
/// ## 它解决的是什么问题
///
/// `MemorySessionSettings`（SESS-001）只能回答「这个键当前是什么值」。而
/// 「这个值是从哪儿来的、改一下会存到哪儿、关掉标签之后它还在不在」是三个
/// 不同的问题，且都是**必须能一眼看出**的问题——规格把它写成了本条的边界：
/// 「三个作用域互不覆盖 …… 必须能一眼看出当前改动的去向」。
///
/// 于是这个类把三份存储串成一条链，并明确两件事：
///
///   - **读**按 `视图 > 会话 > 类型 > 出厂默认` 解析（第 4 条）；
///   - **写**只落到 `writeScope()` 指定的那一层，**绝不碰**其它两层（边界条款）。
///
/// ## 三层各自是什么
///
/// | 作用域 | 存储 | 生命周期 |
/// | --- | --- | --- |
/// | 视图 | 会话自己持有的那份 | 关掉标签即丢弃（第 3 条） |
/// | 会话 | 会话文件里那一份 | 跟着会话走（SESS-008 落盘） |
/// | 类型 | 该类型的默认值 | 所有**新建**会话的起点 |
///
/// 三层都是**借用的指针**（不持有所有权）：本类只做合成与路由，谁创建、
/// 谁保存、什么时候落盘都不是它的职责。因此三层里任何一层都可以是 nullptr，
/// 表示「这一层还没有」——比如一个还没实现类型级默认值的早期会话。
///
/// ## 不转发各层的 `changed` 信号（刻意的）
///
/// 本类**不**连接三层的 `changed`。理由是转发会造出两个假信号：
/// 一次写入会先由被写的那一层发出、再由本类发出（上层看到两条）；
/// 而层的 `clear()` 用**空键**表示「全变了」，直接转发会把「某一层清空了」
/// 说成「所有设置都变了」——侦听方（状态栏的「*」标记、会话脏标记）会因此
/// 抖一下。本类自己按「**有效值**有没有变」发信号，这是界面真正关心的那件事。
///
/// 代价是：外部绕过本类直接改某一层时，本类不会察觉。这一点写在下面
/// `ScopedSessionSettings::setValue` 的注释里，因为它影响到「谁来改」的约定。
///
class ScopedSessionSettings : public SessionSettings
{
    Q_OBJECT

public:
    explicit ScopedSessionSettings(QObject *parent = nullptr);
    ~ScopedSessionSettings() override;

    // --- 三层的接法（借用，不接管生命周期） -----------------------------------

    /// 接上某一层的存储，`layer` 为 nullptr 表示这一层空缺。
    void setLayer(SettingScope scope, SessionSettings *layer);

    /// 取某一层的存储；没有接上时返回 nullptr。
    SessionSettings *layer(SettingScope scope) const;

    /// 写入目标层没有接上时返回 false（见 `setValue`）。
    bool hasLayer(SettingScope scope) const;

    // --- 出厂默认的来源 -------------------------------------------------------

    /// 出厂默认值的权威声明。可为 nullptr（表示「这个会话还没有设置声明」），
    /// 此时链的最后一环退化成调用方给的 `fallback`。
    ///
    /// **为什么出厂默认必须来自声明而不是三层里的某一份**：三层记的都是
    /// 「用户设过的值」，而「用户没设过」时的值只能来自 `SettingItem::defaultValue`。
    /// 把它塞进类型层会让「用户从没改过」与「用户改成过默认值」变得不可区分，
    /// 于是「恢复默认」到底该不该在类型层留一条记录就没法回答了。
    void setSchema(const SettingsSchema *schema);
    const SettingsSchema *schema() const;

    // --- 写入目标 -------------------------------------------------------------

    /// 当前写入目标（对话框底部那个下拉）。
    SettingScope writeScope() const;

    /// 改写入目标。**不迁移已有值**：切换作用域是「以后改的存哪儿」，
    /// 不是「把已经改好的搬过去」——搬过去会静默改掉另一层的默认值，
    /// 而那正是边界条款要防的「互相污染」。已有的改动该往哪儿去由
    /// `scopeSwitchNotice()` 明确告诉用户。
    void setWriteScope(SettingScope scope);

    // --- SessionSettings 接口 -------------------------------------------------

    /// 三层里显式设置过的键的**并集**，按字典序（接口约定）。
    QStringList keys() const override;

    /// 三层里有任一层显式设置过这个键。
    ///
    /// **不含出厂默认**：`contains()` 回答的是「用户设过没有」，而 `value()`
    /// 回答的是「现在的值是多少」。把出厂默认也算进 `contains()` 的话，
    /// 「这一项用户动过吗」这个问题就永远为真，而它正是「要不要在界面上
    /// 标出被改动过」的依据。
    bool contains(const QString &key) const override;

    /// 按 视图 → 会话 → 类型 → **出厂默认** → `fallback` 解析（第 4 条）。
    ///
    /// 出厂默认排在调用方给的 `fallback` **之前**：声明是权威（它是
    /// `SettingItem::defaultValue`），而 `fallback` 只用于「连声明都没有的键」
    /// ——那种键要么是旧会话文件里的遗留字段，要么是调用点写错了键名。
    /// 反过来的话，一个已经声明了默认值的设置项会因为调用点传了个 `fallback`
    /// 而拿到那个临时值，且从结果上看不出任何异常。
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const override;

    /// 写入 `writeScope()` 指定的那一层。
    ///
    /// 返回 false 的三种情况，**一个键都不写**：空键；写入目标那一层没有接上；
    /// 那一层自己拒绝（例如空值）。
    ///
    /// **不因为「目标层没有接上」而退而写入别的层**：用户的动作是「保存到
    /// 当前会话默认值」，写到视图层会让他关掉标签后以为设置丢了、而其实
    /// 它当时生效过——比直接报「这一层暂时不可用」糟得多。
    ///
    /// `changed` 按**有效值**发：写入目标层比它下面那一层低时（例如当前会话
    /// 已经有一条视图级的值），写进去也不会改变用户看到的值，此时不发信号。
    /// 注意这时**值确实存进去了**——用户要求「保存到这个会话」，那就是
    /// 保存了这个会话，只是他眼下看到的仍是视图层的值。`resolvedScope()`
    /// 就是为这种情形准备的：界面据此可以显示「当前生效值来自「仅当前视图」」。
    bool setValue(const QString &key, const QVariant &value) override;

    /// 从**写入目标层**删掉一条（回到由下面几层决定）。
    bool remove(const QString &key) override;

    /// 清空**写入目标层**。
    ///
    /// **不是清空三层**：类型层是所有新会话共用的默认值，被一个会话的「清空」
    /// 带走等于一次跨会话的破坏性操作，而且不可逆。要清别的层请显式调
    /// `clearLayer()`——把范围写在函数名上，比在文档里解释要好。
    void clear() override;

    /// 清空指定的一层，返回真正被删掉的键数。
    int clearLayer(SettingScope scope);

    // --- 解析诊断（第 2 条的「一眼看出」靠这两个） ----------------------------

    /// 这个键最终由哪一层决定。三层都没显式设置过时返回 false
    /// （此时的值来自出厂默认或调用方的 `fallback`）。
    bool resolvedFromLayer(const QString &key, SettingScope *scope = nullptr) const;

    /// 解析结果的一句话说明，可直接显示在界面上：
    /// 「来自「仅当前视图」」/「来自该类型的出厂默认」/「未设置」。
    QString describeResolution(const QString &key) const;

    /// 第 2 条的「本次修改将保存到 X」，按当前写入目标生成。
    ///
    /// 只是转发给下面那个自由函数：界面手上通常同时有本对象与一个下拉，
    /// 让它自己 `writeDestinationText(settings.writeScope())` 就多了一处
    /// 「两个值可能不同步」的机会，而不同步的现象是「下拉选的是会话、
    /// 提示写着视图」——用户会照着提示去理解，然后发现存错了地方。
    QString writeDestinationText() const;

    // --- 第 3 条：视图级的改动在关标签时丢弃 ----------------------------------

    /// 视图层里显式设置过的键（关标签时会被丢弃的东西）。
    QStringList viewScopeKeys() const;

    /// 丢弃视图层，返回真正被丢弃的键数。
    ///
    /// 只对**有效值确实变了**的键发 `changed`：视图层的值和会话层一样时，
    /// 丢掉它用户看不出任何变化，发信号会让状态栏与脏标记白抖一次。
    int discardViewScope();

private:
    /// 解析用的内部入口：`QVariant()` 表示「没有值」。
    QVariant resolvedValue(const QString &key) const;

    /// 出厂默认（按声明归一后）；没有声明或键不在声明里时返回 QVariant()。
    QVariant factoryDefault(const QString &key) const;

    /// 按 `keys()` 比较两个值是否等价（都为空视为相等）。
    static bool sameValue(const QVariant &a, const QVariant &b);

    SessionSettings *m_layers[3] = {nullptr, nullptr, nullptr};
    const SettingsSchema *m_schema = nullptr;
    SettingScope m_writeScope = SettingScope::Session;
};

///
/// \brief 第 2 条的前半句：「本次修改将保存到 X」。
///
/// 做成自由函数而不是对话框的私有方法，是为了让**文案与作用域是同一份事实**：
/// 三个作用域各有一句话，测试逐条断言它等于规格里的形状（「本次修改将保存到
/// 「当前会话默认值」」）。写在对话框里的话，这段文案会在「下拉项的 tooltip」、
/// 「状态提示」两处各写一遍，然后演化出两种说法。
///
QString writeDestinationText(SettingScope scope);

///
/// \brief 第 2 条的后半句：切换作用域时提示已有改动将改写到何处。
///
struct ScopeSwitchNotice
{
    /// 要不要提示。没有待定的改动、或者作用域没变时为假——
    /// 每次拉一下下拉都弹一次提示，用户会学会无视它。
    bool ask = false;

    QString title;
    QString text;

    QString describe() const;
};

///
/// \brief 生成一次作用域切换提示。
///
/// `pendingKeys` 是**尚未应用**的改动（`SettingsDraft::dirtyKeys()`）。
/// 已经应用过的改动不在其中：它们已经落在原来那一层了，切换作用域不会
/// 把它们搬走（见 `setWriteScope`），因此提示里绝不能写成「会把它们改写到
/// 新位置」——那会引导用户以为切换作用域是一次迁移。
///
ScopeSwitchNotice scopeSwitchNotice(SettingScope from,
                                    SettingScope to,
                                    const QStringList &pendingKeys);

///
/// \brief 第 3 条：关闭标签前的提示（视图级设置会被丢弃）。
///
struct ViewScopeClosePlan
{
    /// 有没有会被丢弃的东西。为假时界面不该问任何问题。
    bool ask = false;

    /// 会被丢弃的键，按字典序。
    QStringList keys;

    QString title;
    QString text;

    QString describe() const;
};

///
/// \brief 生成「关闭这个标签会丢弃什么」的提示。
///
/// 传进来的就是视图层里显式设置过的键（`ScopedSessionSettings::viewScopeKeys()`）。
/// 做成吃一个键清单的纯函数，是为了让「没有视图级设置时一个字都不问」这条
/// 能被单独断言——它正是这条例最容易被写过头的部分。
///
ViewScopeClosePlan planViewScopeClose(const QStringList &viewScopeKeys);

} // namespace LqCompare

#endif // LQCOMPARE_SESSION_SETTINGSCOPE_H
