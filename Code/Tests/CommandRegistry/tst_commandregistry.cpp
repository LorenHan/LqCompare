#include "tst_commandregistry.h"

#include "commandregistry.h"

using LqCompare::Command;
using LqCompare::CommandRegistry;

namespace {

Command makeCommand(const QString &id)
{
    Command command;
    command.id = id;
    command.actionId = QStringLiteral("UI-024");
    command.module = QStringLiteral("界面");
    command.text = QStringLiteral("Test Command");
    command.description = QStringLiteral("A command used by the registry tests.");
    command.icon = QStringLiteral(":/Pictures/ribbon_about.svg");
    return command;
}

} // namespace

void TstCommandRegistry::rejectsDuplicateId()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    QVERIFY(registry.add(makeCommand(QStringLiteral("test.one"))));
    QVERIFY(!registry.add(makeCommand(QStringLiteral("test.one"))));
    QCOMPARE(registry.all().size(), 1);
}

void TstCommandRegistry::rejectsInvalidIdShape()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    // 规范是 <域>.<动作>，小写字母开头。
    QVERIFY(!registry.add(makeCommand(QStringLiteral("nodot"))));
    QVERIFY(!registry.add(makeCommand(QStringLiteral("Bad.Case"))));
    QVERIFY(!registry.add(makeCommand(QStringLiteral(".leading"))));
    QVERIFY(!registry.add(makeCommand(QString())));
    QVERIFY(registry.all().isEmpty());
}

void TstCommandRegistry::rejectsEmptyFields()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    Command command = makeCommand(QStringLiteral("test.empty"));
    command.text.clear();
    // id 合法但 text 为空：注册本身成功，问题由 validate() 报出。
    QVERIFY(registry.add(command));

    const QStringList problems = registry.validate();
    QVERIFY(!problems.isEmpty());
    QVERIFY(problems.filter(QStringLiteral("缺少显示文本")).size() == 1);
}

void TstCommandRegistry::exposesCommandsInRegistrationOrder()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    registry.add(makeCommand(QStringLiteral("test.third")));
    registry.add(makeCommand(QStringLiteral("test.first")));
    registry.add(makeCommand(QStringLiteral("test.second")));

    const QVector<Command> all = registry.all();
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.at(0).id, QStringLiteral("test.third"));
    QCOMPARE(all.at(1).id, QStringLiteral("test.first"));
    QCOMPARE(all.at(2).id, QStringLiteral("test.second"));
}

void TstCommandRegistry::triggerReturnsFalseForUnimplemented()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    registry.add(makeCommand(QStringLiteral("test.notimplemented")));
    QVERIFY(!registry.trigger(QStringLiteral("test.notimplemented")));
}

void TstCommandRegistry::triggerRunsHandler()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    int calls = 0;
    Command command = makeCommand(QStringLiteral("test.implemented"));
    command.handler = [&calls]() { ++calls; };
    QVERIFY(registry.add(command));

    QVERIFY(registry.trigger(QStringLiteral("test.implemented")));
    QCOMPARE(calls, 1);
}

void TstCommandRegistry::validateReportsMissingDescription()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    Command command = makeCommand(QStringLiteral("test.nodesc"));
    command.description.clear();
    registry.add(command);

    const QStringList problems = registry.validate();
    // UI-023：两段式 tooltip 的第二段不允许为空。
    QVERIFY(problems.filter(QStringLiteral("缺少命令说明")).size() == 1);
}

void TstCommandRegistry::validateReportsMissingIcon()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    Command command = makeCommand(QStringLiteral("test.noicon"));
    command.icon.clear();
    registry.add(command);

    // UI-025：界面上不允许出现没有图标的按钮。
    QCOMPARE(registry.validate().filter(QStringLiteral("缺少图标")).size(), 1);
}

void TstCommandRegistry::validateReportsMissingActionId()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    Command command = makeCommand(QStringLiteral("test.noactionid"));
    command.actionId.clear();
    registry.add(command);

    QCOMPARE(registry.validate().filter(QStringLiteral("缺少 ACTION-ID")).size(), 1);
}

void TstCommandRegistry::validateReportsShortcutConflict()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    Command first = makeCommand(QStringLiteral("test.firstkey"));
    first.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+K"));
    Command second = makeCommand(QStringLiteral("test.secondkey"));
    second.shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+K"));

    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));

    const QStringList problems = registry.validate();
    QCOMPARE(problems.filter(QStringLiteral("冲突")).size(), 1);
}

void TstCommandRegistry::validateAcceptsWellFormedCommand()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    QVERIFY(registry.add(makeCommand(QStringLiteral("test.wellformed"))));
    QVERIFY2(registry.validate().isEmpty(),
             qPrintable(registry.validate().join(QLatin1Char('\n'))));
}

void TstCommandRegistry::unknownCommandDoesNotThrow()
{
    CommandRegistry &registry = CommandRegistry::instance();
    registry.clear();

    QVERIFY(!registry.contains(QStringLiteral("test.missing")));
    QCOMPARE(registry.find(QStringLiteral("test.missing")), nullptr);
    QVERIFY(!registry.trigger(QStringLiteral("test.missing")));
    QVERIFY(!registry.setEnabled(QStringLiteral("test.missing"), true));
    QVERIFY(!registry.setVisible(QStringLiteral("test.missing"), true));
    QVERIFY(!registry.setChecked(QStringLiteral("test.missing"), true));
}

void TstCommandRegistry::oldAggregateInitializerKeepsDefaults()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    int calls = 0;
    Command command{"test.legacy", "UI-024", "界面", "Legacy", "Legacy aggregate", "icon",
                    QKeySequence(Qt::Key_F6), [&calls]() { ++calls; }};
    QVERIFY(registry.add(command));
    QVERIFY(registry.find(command.id)->enabled);
    QVERIFY(registry.find(command.id)->visible);
    QVERIFY(!registry.find(command.id)->checkable);
    QVERIFY(!registry.find(command.id)->checked);
    QVERIFY(registry.trigger(command.id));
    QCOMPARE(calls, 1);
}

void TstCommandRegistry::disabledCommandCannotExecute()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    int calls = 0;
    auto command = makeCommand(QStringLiteral("file.save"));
    command.handler = [&calls]() { ++calls; };
    QVERIFY(registry.add(command));
    QVERIFY(registry.setEnabled(command.id, false, QStringLiteral("No writable session")));
    QVERIFY(!registry.find(command.id)->enabled);
    QCOMPARE(registry.find(command.id)->disabledReason, QStringLiteral("No writable session"));
    QVERIFY(!registry.trigger(command.id));
    QCOMPARE(calls, 0);
    QVERIFY(registry.setEnabled(command.id, true));
    QVERIFY(registry.find(command.id)->disabledReason.isEmpty());
    QVERIFY(registry.trigger(command.id));
    QCOMPARE(calls, 1);
}

void TstCommandRegistry::hiddenCommandCanExecute()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    int calls = 0;
    auto command = makeCommand(QStringLiteral("test.hidden"));
    command.handler = [&calls]() { ++calls; };
    command.visible = false;
    QVERIFY(registry.add(command));
    QVERIFY(!registry.find(command.id)->visible);
    QVERIFY(registry.trigger(command.id));
    QCOMPARE(calls, 1);
    QVERIFY(registry.setEnabled(command.id, false));
    QVERIFY(!registry.trigger(command.id));
    QCOMPARE(calls, 1);
}

void TstCommandRegistry::unimplementedCommandCannotBeEnabled()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    const auto command = makeCommand(QStringLiteral("test.placeholder"));
    QVERIFY(registry.add(command));
    QVERIFY(!registry.find(command.id)->enabled);
    QVERIFY(!registry.find(command.id)->disabledReason.isEmpty());
    QVERIFY(registry.setEnabled(command.id, true));
    QVERIFY(!registry.find(command.id)->enabled);
    QVERIFY(!registry.trigger(command.id));
}

void TstCommandRegistry::runtimeStateSignalsOnlyOnChanges()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    QSignalSpy added(&registry, &CommandRegistry::commandAdded);
    auto command = makeCommand(QStringLiteral("test.state"));
    command.handler = []() {};
    command.checkable = true;
    QVERIFY(registry.add(command));
    QCOMPARE(added.count(), 1);
    QCOMPARE(added.first().first().toString(), command.id);
    QSignalSpy changed(&registry, &CommandRegistry::commandChanged);
    registry.updateEnabled();
    registry.setEnabled(command.id, true);
    registry.setVisible(command.id, true);
    registry.setChecked(command.id, false);
    QCOMPARE(changed.count(), 0);
    registry.setEnabled(command.id, false, QStringLiteral("Busy"));
    QCOMPARE(changed.count(), 1);
    registry.setEnabled(command.id, false, QStringLiteral("Busy"));
    QCOMPARE(changed.count(), 1);
    registry.setEnabled(command.id, false, QStringLiteral("Read only"));
    QCOMPARE(changed.count(), 2);
    registry.setVisible(command.id, false);
    registry.setChecked(command.id, true);
    QCOMPARE(changed.count(), 4);
    for (const auto &arguments : changed) {
        QCOMPARE(arguments.first().toString(), command.id);
    }
}

void TstCommandRegistry::triggerRefreshesConditionsBeforeAndAfter()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    bool ready = false;
    bool shown = true;
    bool selected = false;
    int calls = 0;
    auto command = makeCommand(QStringLiteral("test.dynamic"));
    command.checkable = true;
    command.enabledWhen = [&ready]() { return ready; };
    command.visibleWhen = [&shown]() { return shown; };
    command.checkedWhen = [&selected]() { return selected; };
    command.handler = [&]() { ++calls; ready = false; shown = false; selected = true; };
    QVERIFY(registry.add(command));
    QVERIFY(!registry.trigger(command.id));
    ready = true; // Intentionally no explicit refresh: trigger must check fresh state.
    QVERIFY(registry.trigger(command.id));
    QCOMPARE(calls, 1);
    const auto *current = registry.find(command.id);
    QVERIFY(!current->enabled);
    QVERIFY(!current->visible);
    QVERIFY(current->checked);
    QVERIFY(!registry.trigger(command.id));
    ready = true;
    registry.updateEnabled();
    QVERIFY(registry.find(command.id)->enabled); // Predicate state must not latch false.
    registry.setEnabled(command.id, false);
    QVERIFY(!registry.trigger(command.id)); // Explicit gating remains authoritative.
    registry.clear(); // Do not leave callbacks referencing locals in the singleton.
}

void TstCommandRegistry::sessionTypeLimitsEnabledState()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    auto command = makeCommand(QStringLiteral("merge.resolve"));
    command.sessionTypes = QStringList{QStringLiteral("merge"), QStringLiteral("text")};
    command.handler = []() {};
    QVERIFY(registry.add(command));
    QVERIFY(!registry.trigger(command.id));
    registry.setCurrentSessionType(QStringLiteral("merge"));
    QVERIFY(registry.trigger(command.id));
    registry.setCurrentSessionType(QStringLiteral("folder"));
    QVERIFY(!registry.trigger(command.id));
    QVERIFY(!registry.find(command.id)->disabledReason.isEmpty());
    registry.setCurrentSessionType(QStringLiteral("text"));
    QVERIFY(registry.trigger(command.id));
    registry.setCurrentSessionType(QString());
    QVERIFY(!registry.trigger(command.id));
}

void TstCommandRegistry::checkedStateRemainsBusinessOwned()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    auto command = makeCommand(QStringLiteral("view.output"));
    command.checkable = true;
    command.handler = []() {};
    QVERIFY(registry.add(command));
    QVERIFY(registry.trigger(command.id));
    QVERIFY(!registry.find(command.id)->checked); // A cancelled operation must not fake a toggle.
    QVERIFY(registry.setChecked(command.id, true));
    QVERIFY(registry.find(command.id)->checked);
    QVERIFY(registry.trigger(command.id));
    QVERIFY(registry.find(command.id)->checked);
    auto nonToggle = makeCommand(QStringLiteral("test.action"));
    nonToggle.checked = true;
    nonToggle.handler = []() {};
    QVERIFY(registry.add(nonToggle));
    QVERIFY(!registry.find(nonToggle.id)->checked);
    QVERIFY(!registry.setChecked(nonToggle.id, true));
}

void TstCommandRegistry::observersSeeConsistentState()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    bool enabled = true;
    auto first = makeCommand(QStringLiteral("test.first"));
    first.handler = []() {};
    first.enabledWhen = [&enabled]() { return enabled; };
    auto second = first;
    second.id = QStringLiteral("test.second");
    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));
    int notificationCount = 0;
    const auto connection = connect(&registry, &CommandRegistry::commandChanged, this,
                                    [&](const QString &) {
        ++notificationCount;
        QVERIFY(!registry.find(first.id)->enabled);
        QVERIFY(!registry.find(second.id)->enabled);
        registry.updateEnabled(); // Unchanged nested refresh must terminate without extra signals.
    });
    enabled = false;
    registry.updateEnabled();
    QCOMPARE(notificationCount, 2);
    disconnect(connection);
    registry.clear();
}

void TstCommandRegistry::handlerMayClearRegistry()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    bool called = false;
    auto command = makeCommand(QStringLiteral("test.clear"));
    command.handler = [&]() { registry.clear(); called = true; };
    QVERIFY(registry.add(command));
    QVERIFY(registry.trigger(command.id));
    QVERIFY(called);
    QVERIFY(registry.all().isEmpty());
}

void TstCommandRegistry::clearResetsAllRuntimeState()
{
    auto &registry = CommandRegistry::instance();
    registry.clear();
    auto command = makeCommand(QStringLiteral("test.reset"));
    command.handler = []() {};
    command.shortcut = QKeySequence(Qt::Key_F6);
    QVERIFY(registry.add(command));
    registry.setEnabled(command.id, false);
    registry.setVisible(command.id, false);
    registry.setCurrentSessionType(QStringLiteral("text"));
    QVERIFY(registry.setShortcuts(command.id, QList<QKeySequence>()));
    QSignalSpy reset(&registry, &CommandRegistry::registryReset);
    registry.clear();
    QCOMPARE(reset.count(), 1);
    QVERIFY(registry.currentSessionType().isEmpty());
    QVERIFY(registry.shortcutOverrides().isEmpty());
    QVERIFY(registry.add(command));
    QVERIFY(registry.find(command.id)->enabled);
    QVERIFY(registry.find(command.id)->visible);
    QCOMPARE(registry.effectiveShortcuts(command.id), QList<QKeySequence>{command.shortcut});
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstCommandRegistry)
