#include "commandactionbinder.h"
#include "commandregistry.h"
#include "ribbonlayout.h"
#include "shortcutsettingsdialog.h"

#include "LqRibbon.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>

using LqCompare::Command;
using LqCompare::CommandActionBinder;
using LqCompare::CommandRegistry;
using LqCompare::RibbonLayout;
using LqCompare::ShortcutSettingsDialog;

namespace {

Command implementedCommand(const QString &id, std::function<void()> handler = [] {})
{
    Command command;
    command.id = id;
    command.actionId = QStringLiteral("UI-024");
    command.module = QStringLiteral("界面");
    command.text = QStringLiteral("Canonical command title");
    command.description = QStringLiteral("Run the current command.");
    command.icon = QStringLiteral(":/Pictures/ribbon_about.svg");
    command.handler = std::move(handler);
    return command;
}

QList<QShortcut *> shortcutsFor(QWidget &window, const QString &id)
{
    return window.findChildren<QShortcut *>(QStringLiteral("commandShortcut_") + id);
}

QStringList nativeShortcuts(const QList<QKeySequence> &keys)
{
    QStringList result;
    for (const QKeySequence &key : keys)
        result.append(key.toString(QKeySequence::NativeText));
    return result;
}

} // namespace

class TstCommandActions : public QObject
{
    Q_OBJECT

private slots:
    void init() { CommandRegistry::instance().clear(); }
    void cleanup() { CommandRegistry::instance().clear(); }

    void entriesShareRuntimeState();
    void qatMayKeepHiddenCommandsAccessible();
    void executionRefreshesPredicates();
    void checkableActionsUseBusinessState();
    void missingAndUnimplementedCommandsAreDisabled();
    void lateRegistrationAndResetRefreshExistingEntries();
    void binderAndShortcutAreUniquePerWindow();
    void hiddenCommandStillRunsFromKeyboard();
    void customShortcutsRefreshAllPresentations();
    void rejectedShortcutConflictLeavesLiveBindingUntouched();
    void ribbonPlaceholdersAreDisabledWithoutDialogs();
    void ribbonActionTracksRegistryChanges();
    void dialogConflictBlocksSaveAndCancelKeepsOriginalBindings();
    void dialogSavesMultipleBindingsAcrossReload();
    void dialogRestoresOneCommandAndAllCommands();
    void dialogSaveFailureRestoresLiveBindings();
};

void TstCommandActions::entriesShareRuntimeState()
{
    auto &registry = CommandRegistry::instance();
    int calls = 0;
    const QString id = QStringLiteral("test.shared");
    QVERIFY(registry.add(implementedCommand(id, [&] { ++calls; })));

    QWidget window;
    QMenu menu(&window);
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *toolbar = binder->createAction(id, &window, QStringLiteral("Old title"));
    auto *menuAction = binder->createAction(id, &menu, {}, {}, CommandActionBinder::Menu);
    menu.addAction(menuAction);
    QCOMPARE(toolbar->text(), registry.find(id)->text);
    QCOMPARE(menuAction->text(), registry.find(id)->text);
    QVERIFY(toolbar->isEnabled());
    QVERIFY(menuAction->isEnabled());
    toolbar->trigger();
    menuAction->trigger();
    QCOMPARE(calls, 2);

    QVERIFY(registry.setEnabled(id, false, QStringLiteral("Select a comparison first.")));
    QVERIFY(!toolbar->isEnabled());
    QVERIFY(!menuAction->isEnabled());
    QVERIFY(toolbar->toolTip().contains(QStringLiteral("Select a comparison first.")));
    QVERIFY(menuAction->toolTip().contains(QStringLiteral("Select a comparison first.")));
    toolbar->trigger();
    menuAction->trigger();
    QCOMPARE(calls, 2);

    QVERIFY(registry.setVisible(id, false));
    QVERIFY(!toolbar->isVisible());
    QVERIFY(!menuAction->isVisible());
    QVERIFY(registry.setVisible(id, true));
    QVERIFY(registry.setEnabled(id, true));
    QVERIFY(toolbar->isVisible());
    QVERIFY(menuAction->isVisible());
    QVERIFY(toolbar->isEnabled());
    QVERIFY(menuAction->isEnabled());
    QVERIFY(!toolbar->toolTip().contains(QStringLiteral("Select a comparison first.")));
}

void TstCommandActions::executionRefreshesPredicates()
{
    auto &registry = CommandRegistry::instance();
    bool available = false;
    int calls = 0;
    const QString id = QStringLiteral("test.predicate");
    Command command = implementedCommand(id, [&] { ++calls; available = false; });
    command.enabledWhen = [&] { return available; };
    QVERIFY(registry.add(command));

    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *first = binder->createAction(id, &window);
    auto *second = binder->createAction(id, &window);
    QVERIFY(!first->isEnabled());
    available = true;
    registry.updateEnabled();
    QVERIFY(first->isEnabled());
    QVERIFY(second->isEnabled());
    first->trigger();
    QCOMPARE(calls, 1);
    QVERIFY(!first->isEnabled());
    QVERIFY(!second->isEnabled());

    // Even if the view has not yet refreshed, dispatch must re-evaluate state.
    available = true;
    registry.updateEnabled();
    available = false;
    second->trigger();
    QCOMPARE(calls, 1);
    QVERIFY(!first->isEnabled());
    QVERIFY(!second->isEnabled());
}

void TstCommandActions::qatMayKeepHiddenCommandsAccessible()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("test.qat");
    int calls = 0;
    QVERIFY(registry.add(implementedCommand(id, [&] { ++calls; })));
    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *ribbon = binder->createAction(id, &window);
    auto *qat = binder->createAction(id, &window, {}, {}, CommandActionBinder::Toolbar, false);
    QVERIFY(registry.setVisible(id, false));
    QVERIFY(!ribbon->isVisible());
    QVERIFY(qat->isVisible());
    QVERIFY(qat->isEnabled());
    qat->trigger();
    QCOMPARE(calls, 1);
    QVERIFY(registry.setEnabled(id, false));
    QVERIFY(qat->isVisible());
    QVERIFY(!qat->isEnabled());
    qat->trigger();
    QCOMPARE(calls, 1);
}

void TstCommandActions::checkableActionsUseBusinessState()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("test.checkable");
    bool handlerChangesState = false;
    Command command = implementedCommand(id, [&] {
        if (handlerChangesState)
            registry.setChecked(id, !registry.find(id)->checked);
    });
    command.checkable = true;
    QVERIFY(registry.add(command));

    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *first = binder->createAction(id, &window);
    auto *second = binder->createAction(id, &window);
    QVERIFY(first->isCheckable());
    first->trigger();
    QVERIFY(!first->isChecked());
    QVERIFY(!second->isChecked());
    QVERIFY(!registry.find(id)->checked);

    handlerChangesState = true;
    second->trigger();
    QVERIFY(first->isChecked());
    QVERIFY(second->isChecked());
    QVERIFY(registry.find(id)->checked);
    first->trigger();
    QVERIFY(!first->isChecked());
    QVERIFY(!second->isChecked());
}

void TstCommandActions::missingAndUnimplementedCommandsAreDisabled()
{
    auto &registry = CommandRegistry::instance();
    Command pending = implementedCommand(QStringLiteral("test.pending"));
    pending.handler = {};
    pending.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(registry.add(pending));

    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *unknown = binder->createAction(QStringLiteral("test.missing"), &window,
                                         QStringLiteral("Pending feature"), QStringLiteral("UI-027"));
    auto *unimplemented = binder->createAction(pending.id, &window);
    QVERIFY(!unknown->isEnabled());
    QVERIFY(!unimplemented->isEnabled());
    QVERIFY(unknown->toolTip().contains(QStringLiteral("尚未实现")));
    QVERIFY(unknown->toolTip().contains(QStringLiteral("UI-027")));
    QVERIFY(unimplemented->toolTip().contains(QStringLiteral("尚未实现")));
    for (QShortcut *shortcut : shortcutsFor(window, pending.id))
        QVERIFY(!shortcut->isEnabled());
}

void TstCommandActions::lateRegistrationAndResetRefreshExistingEntries()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("test.late");
    int calls = 0;
    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *action = binder->createAction(id, &window, QStringLiteral("Loading command"));
    QVERIFY(!action->isEnabled());
    Command command = implementedCommand(id, [&] { ++calls; });
    command.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+L"));
    QVERIFY(registry.add(command));
    QVERIFY(action->isEnabled());
    QCOMPARE(action->text(), command.text);
    action->trigger();
    QCOMPARE(calls, 1);
    QCOMPARE(shortcutsFor(window, id).size(), 1);

    registry.clear();
    QVERIFY(!action->isEnabled());
    action->trigger();
    QCOMPARE(calls, 1);
    for (QShortcut *shortcut : shortcutsFor(window, id))
        QVERIFY(!shortcut->isEnabled());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    command.text = QStringLiteral("Replacement command");
    QVERIFY(registry.add(command));
    QVERIFY(action->isEnabled());
    QCOMPARE(action->text(), command.text);
    QCOMPARE(shortcutsFor(window, id).size(), 1);
    action->trigger();
    QCOMPARE(calls, 2);
}

void TstCommandActions::binderAndShortcutAreUniquePerWindow()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("test.unique");
    Command command = implementedCommand(id);
    command.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+U"));
    QVERIFY(registry.add(command));

    QWidget firstWindow;
    QWidget secondWindow;
    auto *first = CommandActionBinder::forWindow(&firstWindow);
    QCOMPARE(CommandActionBinder::forWindow(&firstWindow), first);
    auto *second = CommandActionBinder::forWindow(&secondWindow);
    QVERIFY(second != first);
    auto *toolbar = first->createAction(id, &firstWindow);
    auto *qat = first->createAction(id, &firstWindow);
    auto *menu = first->createAction(id, &firstWindow, {}, {}, CommandActionBinder::Menu);
    second->createAction(id, &secondWindow);
    first->refresh();
    QCOMPARE(shortcutsFor(firstWindow, id).size(), 1);
    QCOMPARE(shortcutsFor(secondWindow, id).size(), 1);
    QVERIFY(toolbar->shortcuts().isEmpty());
    QVERIFY(qat->shortcuts().isEmpty());
    QVERIFY(menu->shortcuts().isEmpty());
    QCOMPARE(shortcutsFor(firstWindow, id).first()->context(), Qt::WindowShortcut);
}

void TstCommandActions::hiddenCommandStillRunsFromKeyboard()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("test.keyboard");
    int calls = 0;
    Command command = implementedCommand(id, [&] { ++calls; });
    command.shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K);
    QVERIFY(registry.add(command));

    QWidget window;
    QLineEdit editor(&window);
    window.resize(300, 120);
    editor.resize(200, 40);
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *first = binder->createAction(id, &window);
    binder->createAction(id, &window, {}, {}, CommandActionBinder::Menu);
    QVERIFY(registry.setVisible(id, false));
    QVERIFY(!first->isVisible());
    QCOMPARE(shortcutsFor(window, id).size(), 1);
    QVERIFY(shortcutsFor(window, id).first()->isEnabled());

    window.show();
    window.activateWindow();
    editor.setFocus();
    QTRY_COMPARE(QApplication::activeWindow(), &window);
    QTest::keyClick(&editor, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(calls, 1);

    QVERIFY(registry.setEnabled(id, false));
    QVERIFY(!shortcutsFor(window, id).first()->isEnabled());
    QTest::keyClick(&editor, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QCOMPARE(calls, 1);

    QVERIFY(registry.setEnabled(id, true));
    QTest::keyClick(&editor, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(calls, 2);

    QVERIFY(registry.setShortcuts(id, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)}));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::keyClick(&editor, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QCOMPARE(calls, 2);
    QTest::keyClick(&editor, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(calls, 3);
}

void TstCommandActions::customShortcutsRefreshAllPresentations()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("test.custom");
    Command command = implementedCommand(id);
    command.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+A"));
    QVERIFY(registry.add(command));

    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *toolbar = binder->createAction(id, &window);
    auto *menu = binder->createAction(id, &window, {}, {}, CommandActionBinder::Menu);
    const QList<QKeySequence> custom = {QKeySequence(QStringLiteral("Ctrl+Shift+B")),
                                        QKeySequence(QStringLiteral("Alt+Shift+B"))};
    QStringList errors;
    QVERIFY2(registry.setShortcuts(id, custom, &errors), qPrintable(errors.join('\n')));
    QCOMPARE(toolbar->property("commandShortcuts").toStringList(), nativeShortcuts(custom));
    QCOMPARE(menu->property("commandShortcuts").toStringList(), nativeShortcuts(custom));
    QVERIFY(toolbar->shortcuts().isEmpty());
    QVERIFY(menu->shortcuts().isEmpty());
    QVERIFY(!toolbar->text().contains('\t'));
    QVERIFY(menu->text().contains('\t'));
    QVERIFY(menu->text().contains(nativeShortcuts(custom).first()));
    for (const QString &shortcut : nativeShortcuts(custom)) {
        QVERIFY(toolbar->toolTip().contains(shortcut));
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto shortcuts = shortcutsFor(window, id);
    QCOMPARE(shortcuts.size(), 2);
    QList<QKeySequence> bound;
    for (QShortcut *shortcut : shortcuts)
        bound.append(shortcut->key());
    QCOMPARE(bound, custom);

    QVERIFY(registry.setShortcuts(id, {}, &errors));
    QVERIFY(toolbar->property("commandShortcuts").toStringList().isEmpty());
    QVERIFY(!menu->text().contains('\t'));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(shortcutsFor(window, id).isEmpty());

    QVERIFY(registry.resetShortcuts(id, &errors));
    QCOMPARE(shortcutsFor(window, id).size(), 1);
    QCOMPARE(shortcutsFor(window, id).first()->key(), command.shortcut);
    QCOMPARE(toolbar->property("commandShortcuts").toStringList(),
             nativeShortcuts({command.shortcut}));
}

void TstCommandActions::rejectedShortcutConflictLeavesLiveBindingUntouched()
{
    auto &registry = CommandRegistry::instance();
    Command first = implementedCommand(QStringLiteral("test.first"));
    first.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+F"));
    Command second = implementedCommand(QStringLiteral("test.second"));
    second.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+S"));
    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));
    QWidget window;
    auto *binder = CommandActionBinder::forWindow(&window);
    auto *action = binder->createAction(second.id, &window);
    QStringList errors;
    QVERIFY(!registry.setShortcuts(second.id, {first.shortcut}, &errors));
    QVERIFY(!errors.isEmpty());
    QCOMPARE(shortcutsFor(window, second.id).size(), 1);
    QCOMPARE(shortcutsFor(window, second.id).first()->key(), second.shortcut);
    QCOMPARE(action->property("commandShortcuts").toStringList(),
             nativeShortcuts({second.shortcut}));
}

void TstCommandActions::ribbonPlaceholdersAreDisabledWithoutDialogs()
{
    QWidget window;
    LqRibbon::RibbonBar ribbon(&window);
    const int created = RibbonLayout::build(&ribbon);
    QVERIFY(created > 100);
    int commandActions = 0;
    bool dialogShown = false;
    // Prevent a regression to the old blocking placeholder dialog from hanging the suite.
    QTimer closeDialogs;
    connect(&closeDialogs, &QTimer::timeout, this, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QMessageBox *>(widget)) {
                dialogShown = true;
                dialog->reject();
            }
        }
    });
    closeDialogs.start(1);
    for (QAction *action : ribbon.findChildren<QAction *>()) {
        if (!action->objectName().startsWith(QStringLiteral("cmd_")))
            continue;
        ++commandActions;
        QVERIFY2(!action->isEnabled(), qPrintable(action->objectName()));
        QVERIFY2(action->toolTip().contains(QStringLiteral("尚未实现")),
                 qPrintable(action->objectName()));
        action->trigger();
    }
    QCoreApplication::processEvents();
    QCOMPARE(commandActions, created);
    QVERIFY(!dialogShown);
}

void TstCommandActions::ribbonActionTracksRegistryChanges()
{
    auto &registry = CommandRegistry::instance();
    const QString id = QStringLiteral("file.open");
    int calls = 0;
    Command command = implementedCommand(id, [&] { ++calls; });
    command.shortcut = QKeySequence(QStringLiteral("Ctrl+O"));
    QVERIFY(registry.add(command));
    QWidget window;
    LqRibbon::RibbonBar ribbon(&window);
    QVERIFY(RibbonLayout::build(&ribbon) > 0);
    auto *action = ribbon.findChild<QAction *>(QStringLiteral("cmd_") + id);
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    auto *qat = CommandActionBinder::forWindow(&window)->createAction(id, &window);
    QCOMPARE(shortcutsFor(window, id).size(), 1);
    action->trigger();
    QCOMPARE(calls, 1);
    QVERIFY(registry.setEnabled(id, false, QStringLiteral("Operation in progress")));
    QVERIFY(!action->isEnabled());
    QVERIFY(!qat->isEnabled());
    QVERIFY(action->toolTip().contains(QStringLiteral("Operation in progress")));
    QVERIFY(registry.setEnabled(id, true));
    qat->trigger();
    QCOMPARE(calls, 2);
}

void TstCommandActions::dialogConflictBlocksSaveAndCancelKeepsOriginalBindings()
{
    auto &registry = CommandRegistry::instance();
    Command first = implementedCommand(QStringLiteral("test.first"));
    first.shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F);
    Command second = implementedCommand(QStringLiteral("test.second"));
    second.shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S);
    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("shortcuts.ini")), QSettings::IniFormat);
    ShortcutSettingsDialog dialog(settings);
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutCommands"));
    auto *capture = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("shortcutCapture"));
    auto *replace = dialog.findChild<QPushButton *>(QStringLiteral("shortcutReplace"));
    auto *errors = dialog.findChild<QLabel *>(QStringLiteral("shortcutErrors"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("shortcutDialogButtons"));
    QVERIFY(table && capture && replace && errors && buttons);
    QCOMPARE(table->rowCount(), 2);
    table->setCurrentCell(1, 0);
    capture->clear();
    QTest::keyClick(capture, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(capture->keySequence(), first.shortcut);
    replace->click();
    QVERIFY(!buttons->button(QDialogButtonBox::Save)->isEnabled());
    QVERIFY(!errors->text().isEmpty());
    QVERIFY(errors->text().contains(first.id));
    QVERIFY(errors->text().contains(second.id));
    QVERIFY(table->item(0, 0)->background().style() != Qt::NoBrush);
    QVERIFY(table->item(1, 0)->background().style() != Qt::NoBrush);
    QCOMPARE(registry.effectiveShortcuts(second.id), QList<QKeySequence>{second.shortcut});
    QVERIFY(settings.allKeys().isEmpty());

    capture->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    replace->click();
    QVERIFY(buttons->button(QDialogButtonBox::Save)->isEnabled());
    QVERIFY(errors->text().isEmpty());
    QVERIFY(table->item(0, 0)->background().style() == Qt::NoBrush);
    QVERIFY(table->item(1, 0)->background().style() == Qt::NoBrush);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    buttons->button(QDialogButtonBox::Cancel)->click();
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(registry.effectiveShortcuts(first.id), QList<QKeySequence>{first.shortcut});
    QCOMPARE(registry.effectiveShortcuts(second.id), QList<QKeySequence>{second.shortcut});
    QVERIFY(settings.allKeys().isEmpty());
}

void TstCommandActions::dialogSavesMultipleBindingsAcrossReload()
{
    auto &registry = CommandRegistry::instance();
    Command command = implementedCommand(QStringLiteral("test.persisted"));
    command.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(registry.add(command));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("shortcuts.ini"));
    QSettings settings(file, QSettings::IniFormat);
    ShortcutSettingsDialog dialog(settings);
    auto *capture = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("shortcutCapture"));
    auto *replace = dialog.findChild<QPushButton *>(QStringLiteral("shortcutReplace"));
    auto *add = dialog.findChild<QPushButton *>(QStringLiteral("shortcutAdd"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("shortcutDialogButtons"));
    QVERIFY(capture && replace && add && buttons);
    const QList<QKeySequence> custom = {QKeySequence(QStringLiteral("Ctrl+Shift+B")),
                                        QKeySequence(QStringLiteral("Alt+Shift+B"))};
    capture->setKeySequence(custom.at(0));
    replace->click();
    capture->setKeySequence(custom.at(1));
    add->click();
    QCOMPARE(registry.effectiveShortcuts(command.id), QList<QKeySequence>{command.shortcut});
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(registry.effectiveShortcuts(command.id), custom);
    QCOMPARE(settings.status(), QSettings::NoError);
    QVERIFY(QFile::exists(file));

    QVERIFY(registry.resetAllShortcuts());
    QCOMPARE(registry.effectiveShortcuts(command.id), QList<QKeySequence>{command.shortcut});
    QSettings reopened(file, QSettings::IniFormat);
    QStringList errors;
    QVERIFY2(registry.loadShortcuts(reopened, &errors), qPrintable(errors.join('\n')));
    QCOMPARE(registry.effectiveShortcuts(command.id), custom);
}

void TstCommandActions::dialogRestoresOneCommandAndAllCommands()
{
    auto &registry = CommandRegistry::instance();
    Command first = implementedCommand(QStringLiteral("test.first"));
    first.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+F"));
    Command second = implementedCommand(QStringLiteral("test.second"));
    second.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+S"));
    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));
    const QList<QKeySequence> firstCustom = {QKeySequence(QStringLiteral("Alt+Shift+F"))};
    const QList<QKeySequence> secondCustom = {QKeySequence(QStringLiteral("Alt+Shift+S"))};
    QVERIFY(registry.setShortcuts(first.id, firstCustom));
    QVERIFY(registry.setShortcuts(second.id, secondCustom));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("shortcuts.ini")), QSettings::IniFormat);
    {
        ShortcutSettingsDialog dialog(settings);
        auto *reset = dialog.findChild<QPushButton *>(QStringLiteral("shortcutReset"));
        auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("shortcutDialogButtons"));
        QVERIFY(reset && buttons);
        reset->click();
        QCOMPARE(registry.effectiveShortcuts(first.id), firstCustom);
        buttons->button(QDialogButtonBox::Save)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(registry.effectiveShortcuts(first.id), QList<QKeySequence>{first.shortcut});
        QCOMPARE(registry.effectiveShortcuts(second.id), secondCustom);
    }
    {
        ShortcutSettingsDialog dialog(settings);
        auto *reset = dialog.findChild<QPushButton *>(QStringLiteral("shortcutResetAll"));
        auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("shortcutDialogButtons"));
        QVERIFY(reset && buttons);
        reset->click();
        QCOMPARE(registry.effectiveShortcuts(second.id), secondCustom);
        buttons->button(QDialogButtonBox::Save)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(registry.effectiveShortcuts(first.id), QList<QKeySequence>{first.shortcut});
        QCOMPARE(registry.effectiveShortcuts(second.id), QList<QKeySequence>{second.shortcut});
        QVERIFY(registry.shortcutOverrides().isEmpty());
    }
}

void TstCommandActions::dialogSaveFailureRestoresLiveBindings()
{
    auto &registry = CommandRegistry::instance();
    Command command = implementedCommand(QStringLiteral("test.unsaved"));
    command.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+U"));
    QVERIFY(registry.add(command));
    const QList<QKeySequence> previous = {QKeySequence(QStringLiteral("Alt+Shift+U"))};
    QVERIFY(registry.setShortcuts(command.id, previous));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // A regular file cannot serve as a parent directory, on any desktop platform.
    QFile blocker(directory.filePath(QStringLiteral("not-a-directory")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QSettings settings(blocker.fileName() + QStringLiteral("/shortcuts.ini"), QSettings::IniFormat);
    QWidget window;
    auto *action = CommandActionBinder::forWindow(&window)->createAction(command.id, &window);
    ShortcutSettingsDialog dialog(settings);
    auto *capture = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("shortcutCapture"));
    auto *replace = dialog.findChild<QPushButton *>(QStringLiteral("shortcutReplace"));
    auto *errors = dialog.findChild<QLabel *>(QStringLiteral("shortcutErrors"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("shortcutDialogButtons"));
    QVERIFY(capture && replace && errors && buttons);
    capture->setKeySequence(QKeySequence(QStringLiteral("Alt+Shift+V")));
    replace->click();
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(accepted.count(), 0);
    QVERIFY(!errors->text().isEmpty());
    QCOMPARE(registry.effectiveShortcuts(command.id), previous);
    QCOMPARE(action->property("commandShortcuts").toStringList(), nativeShortcuts(previous));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(shortcutsFor(window, command.id).size(), 1);
    QCOMPARE(shortcutsFor(window, command.id).first()->key(), previous.first());
    QVERIFY(settings.allKeys().isEmpty());
}

QTEST_MAIN(TstCommandActions)
#include "tst_commandactions.moc"
