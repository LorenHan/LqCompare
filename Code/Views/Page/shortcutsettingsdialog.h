#ifndef LQCOMPARE_SHORTCUTSETTINGSDIALOG_H
#define LQCOMPARE_SHORTCUTSETTINGSDIALOG_H

#include "commandregistry.h"

#include <QDialog>

class QDialogButtonBox;
class QKeySequenceEdit;
class QLabel;
class QListWidget;
class QSettings;
class QTableWidget;

namespace LqCompare {

/// A private editing draft. Conflicts are shown on both command rows and prevent
/// saving; Cancel never changes runtime bindings or persistent settings.
class ShortcutSettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ShortcutSettingsDialog(
        QSettings &settings, CommandRegistry &registry = CommandRegistry::instance(),
        QWidget *parent = nullptr);

private:
    QString selectedCommand() const;
    QList<QKeySequence> draftShortcuts(const QString &id) const;
    void updateRows();
    void updateSelection();
    void editBinding(bool replace);
    void removeBinding();
    void saveDraft();

    QSettings &m_settings;
    CommandRegistry &m_registry;
    CommandRegistry::ShortcutBindings m_draft;
    QTableWidget *m_commands = nullptr;
    QListWidget *m_bindings = nullptr;
    QKeySequenceEdit *m_capture = nullptr;
    QLabel *m_errors = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace LqCompare

#endif // LQCOMPARE_SHORTCUTSETTINGSDIALOG_H
