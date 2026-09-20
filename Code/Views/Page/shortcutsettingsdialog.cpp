#include "shortcutsettingsdialog.h"

#include <QBrush>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace LqCompare {

namespace {
QStringList shortcutLabels(const QList<QKeySequence> &sequences)
{
    QStringList labels;
    for (const QKeySequence &sequence : sequences)
        labels.append(sequence.toString(QKeySequence::NativeText));
    return labels;
}
} // namespace

ShortcutSettingsDialog::ShortcutSettingsDialog(QSettings &settings,
                                               CommandRegistry &registry,
                                               QWidget *parent)
    : QDialog(parent), m_settings(settings), m_registry(registry),
      m_draft(registry.shortcutOverrides())
{
    setObjectName(QStringLiteral("shortcutSettingsDialog"));
    setWindowTitle(tr("自定义快捷键"));
    resize(820, 600);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("选择命令后按下新的快捷键。可为同一命令添加多个绑定；"
                              "冲突必须解决后才能保存。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_commands = new QTableWidget(0, 3, this);
    m_commands->setObjectName(QStringLiteral("shortcutCommands"));
    m_commands->setHorizontalHeaderLabels({tr("命令"), tr("快捷键"), tr("来源")});
    m_commands->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_commands->setSelectionMode(QAbstractItemView::SingleSelection);
    m_commands->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_commands->verticalHeader()->hide();
    m_commands->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_commands->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_commands->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(m_commands, 1);

    auto *editor = new QHBoxLayout;
    m_bindings = new QListWidget(this);
    m_bindings->setObjectName(QStringLiteral("shortcutBindings"));
    m_bindings->setMaximumHeight(104);
    editor->addWidget(m_bindings, 1);
    auto *editButtons = new QVBoxLayout;
    m_capture = new QKeySequenceEdit(this);
    m_capture->setObjectName(QStringLiteral("shortcutCapture"));
    m_capture->setToolTip(tr("点击此处并按下快捷键，支持多段按键序列。"));
    editButtons->addWidget(m_capture);
    auto *mutationButtons = new QHBoxLayout;
    auto *add = new QPushButton(tr("添加绑定"), this);
    add->setObjectName(QStringLiteral("shortcutAdd"));
    auto *replace = new QPushButton(tr("替换绑定"), this);
    replace->setObjectName(QStringLiteral("shortcutReplace"));
    auto *remove = new QPushButton(tr("移除绑定"), this);
    remove->setObjectName(QStringLiteral("shortcutRemove"));
    for (QPushButton *button : {add, replace, remove}) {
        button->setAutoDefault(false);
        mutationButtons->addWidget(button);
    }
    editButtons->addLayout(mutationButtons);
    editor->addLayout(editButtons, 2);
    layout->addLayout(editor);

    auto *resets = new QHBoxLayout;
    auto *reset = new QPushButton(tr("恢复当前命令默认值"), this);
    reset->setObjectName(QStringLiteral("shortcutReset"));
    reset->setAutoDefault(false);
    auto *resetAll = new QPushButton(tr("恢复全部默认值"), this);
    resetAll->setObjectName(QStringLiteral("shortcutResetAll"));
    resetAll->setAutoDefault(false);
    resets->addWidget(reset);
    resets->addWidget(resetAll);
    resets->addStretch();
    layout->addLayout(resets);

    m_errors = new QLabel(this);
    m_errors->setObjectName(QStringLiteral("shortcutErrors"));
    m_errors->setTextFormat(Qt::PlainText);
    m_errors->setWordWrap(true);
    m_errors->setStyleSheet(QStringLiteral("color: #ad3030;"));
    layout->addWidget(m_errors);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(QStringLiteral("shortcutDialogButtons"));
    layout->addWidget(m_buttons);

    connect(m_commands, &QTableWidget::currentCellChanged, this, [this]() { updateSelection(); });
    connect(m_bindings, &QListWidget::currentRowChanged, this, [this](int row) {
        const auto sequences = draftShortcuts(selectedCommand());
        m_capture->setKeySequence(row >= 0 && row < sequences.size() ? sequences.at(row)
                                                                   : QKeySequence());
    });
    connect(add, &QPushButton::clicked, this, [this]() { editBinding(false); });
    connect(replace, &QPushButton::clicked, this, [this]() { editBinding(true); });
    connect(remove, &QPushButton::clicked, this, &ShortcutSettingsDialog::removeBinding);
    connect(reset, &QPushButton::clicked, this, [this]() {
        m_draft.remove(selectedCommand());
        updateRows();
    });
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        m_draft.clear();
        updateRows();
    });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &ShortcutSettingsDialog::saveDraft);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Newly registered commands still participate in conflict checks while the
    // dialog is open. Existing edits remain in this dialog's private draft.
    connect(&registry, &CommandRegistry::commandAdded, this, [this]() { updateRows(); });
    connect(&registry, &CommandRegistry::registryReset, this, [this]() { updateRows(); });
    updateRows();
}

QString ShortcutSettingsDialog::selectedCommand() const
{
    const auto *item = m_commands->item(m_commands->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QList<QKeySequence> ShortcutSettingsDialog::draftShortcuts(const QString &id) const
{
    if (m_draft.contains(id))
        return m_draft.value(id);
    const Command *command = m_registry.find(id);
    if (!command)
        return {};
    QList<QKeySequence> result;
    if (!command->shortcut.isEmpty())
        result.append(command->shortcut);
    // Preserve invalid/duplicate defaults here so they remain visible and the
    // user can remove them when registry validation reports a problem.
    result.append(command->additionalShortcuts);
    return result;
}

void ShortcutSettingsDialog::updateRows()
{
    const QString previous = selectedCommand();
    const auto commands = m_registry.all();
    QSet<QString> conflicts;
    for (int left = 0; left < commands.size(); ++left) {
        const auto leftKeys = draftShortcuts(commands.at(left).id);
        for (int right = left + 1; right < commands.size(); ++right) {
            const auto rightKeys = draftShortcuts(commands.at(right).id);
            for (const auto &a : leftKeys) {
                for (const auto &b : rightKeys) {
                    if (a.matches(b) != QKeySequence::NoMatch
                        || b.matches(a) != QKeySequence::NoMatch) {
                        conflicts.insert(commands.at(left).id);
                        conflicts.insert(commands.at(right).id);
                    }
                }
            }
        }
        for (int a = 0; a < leftKeys.size(); ++a) {
            for (int b = a + 1; b < leftKeys.size(); ++b) {
                if (leftKeys.at(a).matches(leftKeys.at(b)) != QKeySequence::NoMatch
                    || leftKeys.at(b).matches(leftKeys.at(a)) != QKeySequence::NoMatch)
                    conflicts.insert(commands.at(left).id);
            }
        }
    }

    {
        QSignalBlocker blocker(m_commands);
        m_commands->setRowCount(commands.size());
        int selection = commands.isEmpty() ? -1 : 0;
        for (int row = 0; row < commands.size(); ++row) {
            const Command &command = commands.at(row);
            auto *name = new QTableWidgetItem(command.text + QStringLiteral("  [")
                                            + command.id + QLatin1Char(']'));
            name->setData(Qt::UserRole, command.id);
            auto *keys = new QTableWidgetItem(shortcutLabels(draftShortcuts(command.id))
                                                .join(QStringLiteral(", ")));
            auto *source = new QTableWidgetItem(m_draft.contains(command.id)
                                                   ? tr("自定义") : tr("默认"));
            m_commands->setItem(row, 0, name);
            m_commands->setItem(row, 1, keys);
            m_commands->setItem(row, 2, source);
            if (conflicts.contains(command.id)) {
                for (QTableWidgetItem *item : {name, keys, source}) {
                    item->setBackground(QColor(255, 225, 225));
                    item->setForeground(QColor(120, 20, 20));
                    item->setToolTip(tr("此命令存在快捷键冲突，请修改或移除冲突绑定。"));
                }
            }
            if (command.id == previous)
                selection = row;
        }
        m_commands->setCurrentCell(selection, selection < 0 ? -1 : 0);
    }
    const QStringList errors = m_registry.validateShortcuts(m_draft);
    m_errors->setText(errors.join(QLatin1Char('\n')));
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(errors.isEmpty());
    updateSelection();
}

void ShortcutSettingsDialog::updateSelection()
{
    m_bindings->clear();
    m_bindings->addItems(shortcutLabels(draftShortcuts(selectedCommand())));
    m_capture->clear();
    if (m_bindings->count())
        m_bindings->setCurrentRow(0);
}

void ShortcutSettingsDialog::editBinding(bool replace)
{
    const QString id = selectedCommand();
    const QKeySequence sequence = m_capture->keySequence();
    if (id.isEmpty() || sequence.isEmpty())
        return;
    auto sequences = draftShortcuts(id);
    if (replace) {
        const int row = m_bindings->currentRow();
        if (row < 0 || row >= sequences.size())
            return;
        sequences[row] = sequence;
    } else if (!sequences.contains(sequence)) {
        sequences.append(sequence);
    }
    m_draft.insert(id, sequences);
    updateRows();
}

void ShortcutSettingsDialog::removeBinding()
{
    const QString id = selectedCommand();
    auto sequences = draftShortcuts(id);
    const int row = m_bindings->currentRow();
    if (row < 0 || row >= sequences.size())
        return;
    sequences.removeAt(row);
    m_draft.insert(id, sequences); // An empty custom list explicitly unbinds.
    updateRows();
}

void ShortcutSettingsDialog::saveDraft()
{
    QStringList errors;
    const auto previous = m_registry.shortcutOverrides();
    if (!m_registry.applyShortcutOverrides(m_draft, &errors)) {
        updateRows();
        return;
    }
    if (!m_registry.saveShortcuts(m_settings, &errors)) {
        m_registry.applyShortcutOverrides(previous);
        m_errors->setText(errors.join(QLatin1Char('\n')));
        return;
    }
    accept();
}

} // namespace LqCompare
