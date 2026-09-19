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
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstCommandRegistry)
