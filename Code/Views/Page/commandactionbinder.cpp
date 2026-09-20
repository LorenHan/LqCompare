#include "commandactionbinder.h"

#include <QAction>
#include <QIcon>
#include <QSet>
#include <QShortcut>
#include <QWidget>

namespace LqCompare {

CommandActionBinder *CommandActionBinder::forWindow(QWidget *window,
                                                   CommandRegistry &registry)
{
    if (!window)
        return nullptr;
    window = window->window();
    const auto existing = window->findChildren<CommandActionBinder *>(
        QString(), Qt::FindDirectChildrenOnly);
    for (CommandActionBinder *binder : existing) {
        if (&binder->m_registry == &registry)
            return binder;
    }
    return new CommandActionBinder(window, registry);
}

CommandActionBinder::CommandActionBinder(QWidget *window, CommandRegistry &registry)
    : QObject(window), m_window(window), m_registry(registry)
{
    setObjectName(QStringLiteral("commandActionBinder"));
    connect(&registry, &CommandRegistry::commandChanged,
            this, &CommandActionBinder::syncCommand);
    connect(&registry, &CommandRegistry::commandAdded,
            this, &CommandActionBinder::syncCommand);
    connect(&registry, &CommandRegistry::shortcutsChanged,
            this, &CommandActionBinder::syncCommand);
    connect(&registry, &CommandRegistry::registryReset,
            this, &CommandActionBinder::syncAll);
    refresh();
}

QAction *CommandActionBinder::createAction(const QString &id, QObject *parent,
                                          const QString &fallbackText,
                                          const QString &fallbackActionId,
                                          Presentation presentation,
                                          bool followVisibility)
{
    auto *action = new QAction(parent ? parent : this);
    action->setObjectName(QStringLiteral("cmd_") + id);
    action->setProperty("commandId", id);
    m_actions[id].append({action, fallbackText, fallbackActionId, presentation, followVisibility});
    connect(action, &QAction::triggered, this, [this, id]() {
        // A checkable QAction toggles itself before triggered(). The command is
        // authoritative, so restore its value even if its handler rejects it or
        // deliberately leaves checked unchanged.
        QPointer<CommandActionBinder> guard(this);
        m_registry.trigger(id);
        if (guard)
            syncCommand(id);
    });
    syncCommand(id);
    return action;
}

void CommandActionBinder::refresh()
{
    m_registry.updateEnabled();
    syncAll();
}

void CommandActionBinder::syncAll()
{
    QSet<QString> ids;
    for (const Command &command : m_registry.all())
        ids.insert(command.id);
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
        ids.insert(it.key());
    for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it)
        ids.insert(it.key());
    for (const QString &id : ids)
        syncCommand(id);
}

void CommandActionBinder::syncCommand(const QString &id)
{
    const Command *found = m_registry.find(id);
    const bool registered = found != nullptr;
    const Command command = found ? *found : Command();
    const bool enabled = registered && command.isImplemented() && command.enabled;
    const QList<QKeySequence> sequences = m_registry.effectiveShortcuts(id);
    QStringList shortcutTexts;
    for (const QKeySequence &sequence : sequences)
        shortcutTexts.append(sequence.toString(QKeySequence::NativeText));

    // Changing only availability must not replace an actively executing
    // QShortcut. Rebuild only when the effective bindings themselves change.
    if (m_sequences.value(id) != sequences) {
        for (const QPointer<QShortcut> &shortcut : m_shortcuts.take(id)) {
            if (shortcut) {
                shortcut->setEnabled(false);
                shortcut->deleteLater();
            }
        }
        m_sequences.insert(id, sequences);
        if (m_window) {
            for (const QKeySequence &sequence : sequences) {
                auto *shortcut = new QShortcut(sequence, m_window);
                shortcut->setObjectName(QStringLiteral("commandShortcut_") + id);
                shortcut->setContext(Qt::WindowShortcut);
                shortcut->setAutoRepeat(false);
                connect(shortcut, &QShortcut::activated, this, [this, id]() {
                    m_registry.trigger(id);
                });
                m_shortcuts[id].append(shortcut);
            }
        }
    }
    for (const QPointer<QShortcut> &shortcut : m_shortcuts.value(id)) {
        if (shortcut)
            shortcut->setEnabled(enabled);
    }

    QVector<ActionBinding> &bindings = m_actions[id];
    for (int index = bindings.size() - 1; index >= 0; --index) {
        if (!bindings.at(index).action)
            bindings.removeAt(index);
    }
    // Copy: QAction::changed listeners are allowed to create another entry.
    const auto currentBindings = bindings;
    for (const ActionBinding &binding : currentBindings) {
        QAction *action = binding.action;
        if (!action)
            continue;
        const QString title = !command.text.isEmpty() ? command.text
                              : !binding.fallbackText.isEmpty() ? binding.fallbackText : id;
        const QString actionId = !command.actionId.isEmpty() ? command.actionId
                                                            : binding.fallbackActionId;
        QString description = command.description;
        if (description.isEmpty())
            description = tr("尚未实现");
        QStringList tooltip{title, description};
        if (!shortcutTexts.isEmpty())
            tooltip.append(tr("快捷键：%1").arg(shortcutTexts.join(QStringLiteral(", "))));
        QString reason;
        if (!registered || !command.isImplemented())
            reason = tr("尚未实现");
        else if (!enabled)
            reason = command.disabledReason.isEmpty() ? tr("当前状态下不可用")
                                                      : command.disabledReason;
        if (!reason.isEmpty() && reason != description)
            tooltip.append(reason);
        if (!actionId.isEmpty())
            tooltip.append(tr("规格条目：%1").arg(actionId));

        QString text = title;
        if (binding.presentation == Menu && !shortcutTexts.isEmpty())
            text += QLatin1Char('\t') + shortcutTexts.first();
        action->setText(text);
        action->setData(actionId);
        action->setIcon(QIcon(command.icon));
        action->setShortcuts(QList<QKeySequence>());
        action->setProperty("commandShortcuts", shortcutTexts);
        action->setToolTip(tooltip.join(QLatin1Char('\n')));
        action->setStatusTip(reason.isEmpty() ? description
                                             : description + QStringLiteral(" — ") + reason);
        action->setCheckable(registered && command.checkable);
        action->setChecked(registered && command.checkable && command.checked);
        action->setVisible(!binding.followVisibility || !registered || command.visible);
        action->setEnabled(enabled);
    }
}

} // namespace LqCompare
