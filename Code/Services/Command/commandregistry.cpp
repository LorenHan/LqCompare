#include "commandregistry.h"

#include <QRegularExpression>

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
    const Command *command = find(id);
    if (!command || !command->handler) {
        return false;
    }
    command->handler();
    return true;
}

QStringList CommandRegistry::validate() const
{
    QStringList problems;
    QMap<QString, QString> shortcutOwner;

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
        if (!command.shortcut.isEmpty()) {
            const QString sequence = command.shortcut.toString(QKeySequence::PortableText);
            if (shortcutOwner.contains(sequence)) {
                problems.append(QStringLiteral("%1：快捷键 %2 与 %3 冲突")
                                    .arg(id, sequence, shortcutOwner.value(sequence)));
            } else {
                shortcutOwner.insert(sequence, id);
            }
        }
    }
    return problems;
}

void CommandRegistry::clear()
{
    m_commands.clear();
    m_order.clear();
}

} // namespace LqCompare
