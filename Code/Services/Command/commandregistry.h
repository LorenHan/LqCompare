#ifndef LQCOMPARE_COMMANDREGISTRY_H
#define LQCOMPARE_COMMANDREGISTRY_H

#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QSettings;

namespace LqCompare {

///
/// \brief The Command struct
/// 一条可执行命令的完整声明。
///
/// 命令注册中心是 Ribbon 按钮、菜单项、快速访问工具栏、快捷键与搜索栏的**唯一出口**
/// （PRD: UI-024）。界面元素不允许直接连接业务槽函数。
///
/// description 对应 PRD UI-023 的两段式 tooltip 规范：第二段不允许为空，
/// 缺失时 CommandRegistry::validate() 会报出来。
///
struct Command
{
    QString id;          ///< 稳定命令 ID，形如 "session.new"，发布后不可改名
    QString actionId;    ///< 对应的 PRD ACTION-ID，形如 "SESS-005"，便于回溯规格
    QString module;      ///< 功能域，取值见 tools/spec/__init__.py 的 MODULES
    QString text;        ///< 界面显示文本
    QString description; ///< 一句话说明（tooltip 第二段）
    QString icon;        ///< 资源路径，形如 ":/Pictures/ribbon_open.svg"，可为空
    QKeySequence shortcut; ///< 默认快捷键，可为空
    std::function<void()> handler; ///< 执行体；为空表示「尚未实现」

    // 新字段只追加在末尾，保持已有八字段 aggregate initializer 的兼容性。
    bool enabled = true;  ///< 当前有效状态；无 handler 的命令始终禁用
    bool visible = true;  ///< 布局可见性；隐藏本身不禁止其他入口执行
    bool checkable = false;
    bool checked = false; ///< 由 handler/setChecked 或 checkedWhen 提供真实业务状态
    QString disabledReason = {};
    QStringList sessionTypes = {}; ///< 空表示跨会话命令，否则须匹配当前会话类型
    std::function<bool()> enabledWhen = {};
    std::function<bool()> visibleWhen = {};
    std::function<bool()> checkedWhen = {};
    QList<QKeySequence> additionalShortcuts = {}; ///< 除 shortcut 外的默认绑定

    bool isImplemented() const { return static_cast<bool>(handler); }
};

///
/// \brief The CommandRegistry class
/// 全量命令的注册与查询。
///
class CommandRegistry : public QObject
{
    Q_OBJECT

public:
    using ShortcutBindings = QMap<QString, QList<QKeySequence>>;

    static CommandRegistry &instance();

    /// 注册一条命令。
    ///
    /// 返回 false 的情况：id 不符合 <域>.<动作> 规范，或 id 已经存在（不覆盖已有条目）。
    bool add(const Command &command);

    bool contains(const QString &id) const;

    /// 查询命令；不存在时返回 nullptr。
    const Command *find(const QString &id) const;

    /// 按注册顺序返回全部命令。
    QVector<Command> all() const;

    /// 执行前后统一刷新状态。不存在、未实现或禁用时返回 false。
    /// visible 只控制布局，隐藏且 enabled 的命令仍允许执行。
    bool trigger(const QString &id) const;

    /// 设定基础状态；enabledWhen/sessionTypes 仍可进一步限制可用性。
    /// 未知 ID 返回 false；不重复发送未发生变化的 commandChanged。
    bool setEnabled(const QString &id, bool enabled, const QString &reason = QString());
    bool setVisible(const QString &id, bool visible);
    bool setChecked(const QString &id, bool checked);

    QString currentSessionType() const;
    void setCurrentSessionType(const QString &type);

    /// 统一刷新所有 enabled/visible/checked 条件。会话切换、选择及脏状态变化时调用。
    /// 回调应为只读查询；registry 与界面一样在所属线程调用。
    void updateEnabled();

    /// 有效快捷键：自定义整组覆盖默认值；空的自定义列表表示明确解绑。
    QList<QKeySequence> effectiveShortcuts(const QString &id) const;
    ShortcutBindings shortcutOverrides() const;
    QStringList validateShortcuts(const ShortcutBindings &overrides) const;
    bool setShortcuts(const QString &id, const QList<QKeySequence> &shortcuts,
                      QStringList *errors = nullptr);
    bool resetShortcuts(const QString &id, QStringList *errors = nullptr);
    bool resetAllShortcuts(QStringList *errors = nullptr);
    /// 整批替换并原子校验，支持两条命令交换快捷键；失败时不改变运行期绑定。
    bool applyShortcutOverrides(const ShortcutBindings &overrides, QStringList *errors = nullptr);
    bool saveShortcuts(QSettings &settings, QStringList *errors = nullptr) const;
    bool loadShortcuts(QSettings &settings, QStringList *errors = nullptr);

    /// 自检：返回问题清单（空列表表示通过）。
    ///
    /// 检查项：id 命名规范、缺少 actionId / module / text / description、
    /// 缺少图标、重复快捷键。调试构建下启动时调用，见 App/main.cpp。
    QStringList validate() const;

    /// 清空注册表（仅测试使用）。
    void clear();

signals:
    void commandAdded(const QString &id);
    void commandChanged(const QString &id);
    void shortcutsChanged(const QString &id);
    void registryReset();

private:
    explicit CommandRegistry(QObject *parent = nullptr);

    struct BaseState {
        bool enabled = true;
        bool visible = true;
        bool checked = false;
        QString disabledReason;
    };

    QMap<QString, Command> m_commands;
    QStringList m_order;
    QMap<QString, BaseState> m_baseStates;
    ShortcutBindings m_shortcutOverrides;
    QString m_currentSessionType;
    bool m_updating = false;
};

} // namespace LqCompare

#endif // LQCOMPARE_COMMANDREGISTRY_H
