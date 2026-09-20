#include "commandregistry.h"

#include <QRegularExpression>
#include <QScopedValueRollback>

namespace LqCompare {

namespace {

/// 命令 ID 规范：小写字母开头，点分小写段，段内允许数字与短横线。
const QRegularExpression &idPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9]*(\\.[a-z0-9-]+)+$"));
    return pattern;
}

} // namespace

CommandRegistry &CommandRegistry::instance()
{
    static CommandRegistry registry;
    return registry;
}

CommandRegistry::CommandRegistry(QObject *parent) : QObject(parent) {}

bool CommandRegistry::add(const Command &command)
{
    // 快速失败：命令 ID 是跨模块引用的唯一键，形状不合法时不允许进入注册表。
    // 这样错误在注册点就暴露，而不是等到某天界面按钮连不上命令才发现。
    if (!idPattern().match(command.id).hasMatch() || m_commands.contains(command.id)) {
        return false;
    }
    m_commands.insert(command.id, command);
    m_order.append(command.id);
    m_baseStates.insert(command.id, {command.enabled, command.visible, command.checked,
                                     command.disabledReason});
    updateEnabled();
    emit commandAdded(command.id);
    return true;
}

bool CommandRegistry::contains(const QString &id) const
{
    return m_commands.contains(id);
}

const Command *CommandRegistry::find(const QString &id) const
{
    const auto it = m_commands.constFind(id);
    return it == m_commands.constEnd() ? nullptr : &it.value();
}

QVector<Command> CommandRegistry::all() const
{
    QVector<Command> result;
    result.reserve(m_order.size());
    for (const QString &id : m_order) {
        result.append(m_commands.value(id));
    }
    return result;
}

bool CommandRegistry::trigger(const QString &id) const
{
    // Preserve the existing const API while refreshing the shared runtime state.
    auto *registry = const_cast<CommandRegistry *>(this);
    registry->updateEnabled();
    const Command *command = find(id);
    if (!command || !command->handler || !command->enabled) {
        return false;
    }
    // A handler can add/clear commands; copying avoids calling a destroyed function.
    const auto handler = command->handler;
    handler();
    registry->updateEnabled();
    return true;
}

bool CommandRegistry::setEnabled(const QString &id, bool enabled, const QString &reason)
{
    auto it = m_baseStates.find(id);
    if (it == m_baseStates.end()) {
        return false;
    }
    it->enabled = enabled;
    it->disabledReason = reason;
    updateEnabled();
    return true;
}

bool CommandRegistry::setVisible(const QString &id, bool visible)
{
    auto it = m_baseStates.find(id);
    if (it == m_baseStates.end()) {
        return false;
    }
    it->visible = visible;
    updateEnabled();
    return true;
}

bool CommandRegistry::setChecked(const QString &id, bool checked)
{
    auto it = m_baseStates.find(id);
    const Command *command = find(id);
    if (it == m_baseStates.end() || !command || !command->checkable) {
        return false;
    }
    it->checked = checked;
    updateEnabled();
    return true;
}

QString CommandRegistry::currentSessionType() const
{
    return m_currentSessionType;
}

void CommandRegistry::setCurrentSessionType(const QString &type)
{
    m_currentSessionType = type;
    updateEnabled();
}

void CommandRegistry::updateEnabled()
{
    if (m_updating) {
        return;
    }
    QScopedValueRollback<bool> updating(m_updating, true);
    QStringList changed;
    const QStringList ids = m_order;
    for (const QString &id : ids) {
        // Evaluate a copy: the normal contract is a read-only predicate, but a
        // nested event or signal must not leave dangling references into the map.
        const Command command = m_commands.value(id);
        const BaseState base = m_baseStates.value(id);
        const bool sessionAllowed = command.sessionTypes.isEmpty()
            || command.sessionTypes.contains(m_currentSessionType);
        const bool conditionAllowed = !command.enabledWhen || command.enabledWhen();
        const bool enabled = command.isImplemented() && base.enabled
            && sessionAllowed && conditionAllowed;
        const bool visible = base.visible && (!command.visibleWhen || command.visibleWhen());
        const bool checked = command.checkable
            && (command.checkedWhen ? command.checkedWhen() : base.checked);
        QString reason;
        if (!enabled) {
            if (!command.isImplemented()) {
                reason = tr("尚未实现");
            } else if (!sessionAllowed) {
                reason = tr("当前会话不支持此命令");
            } else if (!base.disabledReason.isEmpty()) {
                reason = base.disabledReason;
            } else {
                reason = tr("当前状态下不可用");
            }
        }
        auto it = m_commands.find(id);
        if (it == m_commands.end()) {
            continue;
        }
        if (it->enabled != enabled || it->visible != visible
            || it->checked != checked || it->disabledReason != reason) {
            it->enabled = enabled;
            it->visible = visible;
            it->checked = checked;
            it->disabledReason = reason;
            changed.append(id);
        }
    }
    // Publish only after every command has its new state, so multi-entry UI
    // consumers never observe a mixture of old and new session capabilities.
    m_updating = false;
    updating.commit();
    for (const QString &id : changed) {
        emit commandChanged(id);
    }
}

QStringList CommandRegistry::validate() const
{
    QStringList problems;

    for (const QString &id : m_order) {
        const Command &command = m_commands.value(id);

        if (!idPattern().match(command.id).hasMatch()) {
            problems.append(QStringLiteral("%1：命令 ID 不符合 <域>.<动作> 规范").arg(id));
        }
        if (command.actionId.isEmpty()) {
            problems.append(QStringLiteral("%1：缺少 ACTION-ID（无法回溯规格条目）").arg(id));
        }
        if (command.module.isEmpty()) {
            problems.append(QStringLiteral("%1：缺少功能域").arg(id));
        }
        if (command.text.trimmed().isEmpty()) {
            problems.append(QStringLiteral("%1：缺少显示文本").arg(id));
        }
        // UI-023：两段式 tooltip 的第二段不允许为空。
        if (command.description.trimmed().isEmpty()) {
            problems.append(QStringLiteral("%1：缺少命令说明（PRD UI-023 要求两段式 tooltip）").arg(id));
        }
        // UI-025：每个命令都要有图标，否则界面上会出现无图按钮。
        if (command.icon.isEmpty()) {
            problems.append(QStringLiteral("%1：缺少图标").arg(id));
        }
    }
    problems.append(validateShortcuts(m_shortcutOverrides));
    return problems;
}

void CommandRegistry::clear()
{
    m_commands.clear();
    m_order.clear();
    m_baseStates.clear();
    m_shortcutOverrides.clear();
    m_currentSessionType.clear();
    emit registryReset();
}

} // namespace LqCompare
