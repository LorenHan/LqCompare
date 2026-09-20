#ifndef LQCOMPARE_COMMANDACTIONBINDER_H
#define LQCOMPARE_COMMANDACTIONBINDER_H

#include "commandregistry.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

class QAction;
class QShortcut;
class QWidget;

namespace LqCompare {

/// Shared command presentation and keyboard dispatch for one application window.
///
/// Ribbon, QAT and menus may each request an action. Actions never install Qt
/// shortcuts themselves: one set of window-scoped QShortcuts handles dispatch,
/// including when a command is absent from or hidden in the current layout.
class CommandActionBinder final : public QObject
{
    Q_OBJECT

public:
    enum Presentation { Toolbar, Menu };

    static CommandActionBinder *forWindow(
        QWidget *window, CommandRegistry &registry = CommandRegistry::instance());

    /// The action's commandId and commandShortcuts properties expose its stable
    /// ID and current NativeText shortcut list. Menu presentation adds the first
    /// shortcut after a tab, without registering a second keyboard binding.
    /// QAT entries can pass followVisibility=false to remain accessible when
    /// their command is hidden in the Ribbon layout; availability still applies.
    QAction *createAction(const QString &id, QObject *parent,
                          const QString &fallbackText = QString(),
                          const QString &fallbackActionId = QString(),
                          Presentation presentation = Toolbar,
                          bool followVisibility = true);

    /// Re-evaluate predicates and synchronize every attached presentation.
    void refresh();

private:
    struct ActionBinding {
        QPointer<QAction> action;
        QString fallbackText;
        QString fallbackActionId;
        Presentation presentation = Toolbar;
        bool followVisibility = true;
    };

    explicit CommandActionBinder(QWidget *window, CommandRegistry &registry);
    void syncCommand(const QString &id);
    void syncAll();

    QPointer<QWidget> m_window;
    CommandRegistry &m_registry;
    QHash<QString, QVector<ActionBinding>> m_actions;
    QHash<QString, QList<QKeySequence>> m_sequences;
    QHash<QString, QVector<QPointer<QShortcut>>> m_shortcuts;
};

} // namespace LqCompare

#endif // LQCOMPARE_COMMANDACTIONBINDER_H
