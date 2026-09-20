#include "commandregistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using LqCompare::Command;
using LqCompare::CommandRegistry;

namespace {

QKeySequence key(const char *text)
{
    return QKeySequence::fromString(QString::fromLatin1(text), QKeySequence::PortableText);
}

Command command(const char *id, const char *shortcut = "")
{
    Command result;
    result.id = QString::fromLatin1(id);
    result.actionId = QStringLiteral("UI-027");
    result.module = QStringLiteral("界面");
    result.text = QStringLiteral("Shortcut test");
    result.description = QStringLiteral("Exercise command keyboard bindings.");
    result.icon = QStringLiteral(":/Pictures/ribbon_about.svg");
    result.shortcut = key(shortcut);
    result.handler = [] {};
    return result;
}

CommandRegistry &registry()
{
    return CommandRegistry::instance();
}

const QString first = QStringLiteral("test.first");
const QString second = QStringLiteral("test.second");
const QString third = QStringLiteral("test.third");

} // namespace

class TstCommandShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        registry().clear();
    }

    void cleanup()
    {
        registry().clear();
    }

    void exposesDefaultAndAdditionalBindings()
    {
        Command item = command("test.first", "Ctrl+K");
        item.additionalShortcuts = {key("Alt+K"), key("Ctrl+Shift+K")};
        QVERIFY(registry().add(item));
        QCOMPARE(registry().effectiveShortcuts(first),
                 QList<QKeySequence>({key("Ctrl+K"), key("Alt+K"), key("Ctrl+Shift+K")}));
        QVERIFY(registry().effectiveShortcuts(QStringLiteral("test.missing")).isEmpty());
        QVERIFY(registry().shortcutOverrides().isEmpty());
        QVERIFY(registry().validateShortcuts({}).isEmpty());
    }

    void overridesMultipleBindingsWithoutChangingDefaults()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QStringList errors{QStringLiteral("old error")};
        const QList<QKeySequence> custom{key("Ctrl+K, Ctrl+C"), key("Alt+K")};
        QVERIFY(registry().setShortcuts(first, custom, &errors));
        QVERIFY(errors.isEmpty());
        QCOMPARE(registry().effectiveShortcuts(first), custom);
        QCOMPARE(registry().find(first)->shortcut, key("Ctrl+A"));
        QCOMPARE(registry().shortcutOverrides().value(first), custom);
    }

    void emptyOverrideUnbindsAndResetRestoresDefaults()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().setShortcuts(first, {}));
        QVERIFY(registry().effectiveShortcuts(first).isEmpty());
        QVERIFY(registry().shortcutOverrides().contains(first));
        QVERIFY(registry().resetShortcuts(first));
        QCOMPARE(registry().effectiveShortcuts(first), QList<QKeySequence>({key("Ctrl+A")}));
        QVERIFY(!registry().shortcutOverrides().contains(first));
    }

    void collisionRejectsAtomicallyAndReportsBothOwners()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+B")));
        QVERIFY(registry().setShortcuts(first, {key("Alt+A")}));
        const auto before = registry().shortcutOverrides();
        QSignalSpy commands(&registry(), &CommandRegistry::commandChanged);
        QSignalSpy shortcuts(&registry(), &CommandRegistry::shortcutsChanged);
        QStringList errors;
        QVERIFY(!registry().setShortcuts(second, {key("Ctrl+C"), key("Alt+A")}, &errors));
        QCOMPARE(registry().shortcutOverrides(), before);
        const QString text = errors.join(QLatin1Char('\n'));
        QVERIFY(text.contains(first));
        QVERIFY(text.contains(second));
        QVERIFY(text.contains(QStringLiteral("Alt+A")));
        QCOMPARE(commands.count(), 0);
        QCOMPARE(shortcuts.count(), 0);
    }

    void prefixConflicts_data()
    {
        QTest::addColumn<QString>("existing");
        QTest::addColumn<QString>("candidate");
        QTest::newRow("short-prefix") << "Ctrl+K" << "Ctrl+K, Ctrl+C";
        QTest::newRow("long-prefix") << "Ctrl+K, Ctrl+C" << "Ctrl+K";
        QTest::newRow("two-chord-prefix") << "Ctrl+K, Ctrl+C" << "Ctrl+K, Ctrl+C, Ctrl+A";
        QTest::newRow("exact-chord") << "Ctrl+K, Ctrl+C" << "Ctrl+K, Ctrl+C";
    }

    void prefixConflicts()
    {
        QFETCH(QString, existing);
        QFETCH(QString, candidate);
        Command item = command("test.first");
        item.shortcut = QKeySequence::fromString(existing, QKeySequence::PortableText);
        QVERIFY(registry().add(item));
        QVERIFY(registry().add(command("test.second")));
        QStringList errors;
        QVERIFY(!registry().setShortcuts(second,
            {QKeySequence::fromString(candidate, QKeySequence::PortableText)}, &errors));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().contains(first));
        QVERIFY(errors.first().contains(second));
    }

    void siblingChordsAreAllowed()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+K, Ctrl+C")));
        QVERIFY(registry().add(command("test.second")));
        QVERIFY(registry().setShortcuts(second, {key("Ctrl+K, Ctrl+U")}));
    }

    void duplicateWithinOneCommandIsRejected()
    {
        QVERIFY(registry().add(command("test.first")));
        QStringList errors;
        QVERIFY(!registry().setShortcuts(first, {key("Ctrl+A"), key("Ctrl+A")}, &errors));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().contains(first));
        QVERIFY(registry().shortcutOverrides().isEmpty());
    }

    void invalidBindings_data()
    {
        QTest::addColumn<QKeySequence>("sequence");
        QTest::newRow("empty") << QKeySequence();
        QTest::newRow("unrecognized") << key("Not a key name");
        QTest::newRow("modifier-mask-only") << QKeySequence(int(Qt::CTRL));
        QTest::newRow("modifier-key-only") << QKeySequence(int(Qt::Key_Control));
        QTest::newRow("unknown-key") << QKeySequence(int(Qt::CTRL) | int(Qt::Key_unknown));
        QTest::newRow("second-chord-unknown") << QKeySequence(int(Qt::CTRL) | int(Qt::Key_A),
                                                            int(Qt::Key_unknown));
        QTest::newRow("hole") << QKeySequence(0, int(Qt::CTRL) | int(Qt::Key_A));
    }

    void invalidBindings()
    {
        QFETCH(QKeySequence, sequence);
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QStringList errors;
        QVERIFY(!registry().setShortcuts(first, {sequence}, &errors));
        QVERIFY(!errors.isEmpty());
        QCOMPARE(registry().effectiveShortcuts(first), QList<QKeySequence>({key("Ctrl+A")}));
    }

    void batchCanSwapBindingsAndObserversSeeCommittedMap()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+B")));
        QVERIFY(!registry().setShortcuts(first, {key("Ctrl+B")}));
        bool observerSawCompleteMap = true;
        const auto connection = connect(&registry(), &CommandRegistry::shortcutsChanged, this,
            [&observerSawCompleteMap] {
                observerSawCompleteMap &= registry().effectiveShortcuts(first) == QList<QKeySequence>{key("Ctrl+B")}
                    && registry().effectiveShortcuts(second) == QList<QKeySequence>{key("Ctrl+A")};
            });
        QVERIFY(registry().applyShortcutOverrides({{first, {key("Ctrl+B")}}, {second, {key("Ctrl+A")}}}));
        QVERIFY(observerSawCompleteMap);
        disconnect(connection);
    }

    void batchReplacesEntireOverrideMap()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+B")));
        QVERIFY(registry().setShortcuts(first, {key("Alt+A")}));
        QVERIFY(registry().applyShortcutOverrides({{second, {key("Alt+B")}}}));
        QVERIFY(!registry().shortcutOverrides().contains(first));
        QCOMPARE(registry().effectiveShortcuts(first), QList<QKeySequence>({key("Ctrl+A")}));
        QCOMPARE(registry().effectiveShortcuts(second), QList<QKeySequence>({key("Alt+B")}));
    }

    void unknownCommandIsRejected()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QStringList errors;
        QVERIFY(!registry().setShortcuts(QStringLiteral("test.missing"), {}, &errors));
        QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("test.missing")));
        QVERIFY(!registry().resetShortcuts(QStringLiteral("test.missing"), &errors));
        QVERIFY(registry().shortcutOverrides().isEmpty());
    }

    void signalsOnlyDescribeEffectiveChanges()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+B")));
        QSignalSpy commands(&registry(), &CommandRegistry::commandChanged);
        QSignalSpy shortcuts(&registry(), &CommandRegistry::shortcutsChanged);
        QVERIFY(registry().setShortcuts(first, {key("Ctrl+A")}));
        QVERIFY(registry().shortcutOverrides().contains(first));
        QCOMPARE(commands.count(), 0);
        QCOMPARE(shortcuts.count(), 0);
        QVERIFY(registry().setShortcuts(first, {key("Alt+A")}));
        QCOMPARE(commands.count(), 1);
        QCOMPARE(shortcuts.count(), 1);
        QCOMPARE(commands.first().first().toString(), first);
        QCOMPARE(shortcuts.first().first().toString(), first);
        QVERIFY(registry().setShortcuts(first, {key("Alt+A")}));
        QCOMPARE(shortcuts.count(), 1);
        QVERIFY(registry().setShortcuts(second, {}));
        commands.clear();
        shortcuts.clear();
        QVERIFY(registry().resetAllShortcuts());
        QCOMPARE(commands.count(), 2);
        QCOMPARE(shortcuts.count(), 2);
        QCOMPARE(shortcuts.at(0).first().toString(), first);
        QCOMPARE(shortcuts.at(1).first().toString(), second);
    }

    void resetRejectsDefaultsThatWouldConflict()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+A")));
        QVERIFY(!registry().validate().isEmpty());
        QVERIFY(registry().setShortcuts(first, {key("Alt+A")}));
        const auto before = registry().shortcutOverrides();
        QStringList errors;
        QVERIFY(!registry().resetShortcuts(first, &errors));
        QVERIFY(!registry().resetAllShortcuts(&errors));
        QCOMPARE(registry().shortcutOverrides(), before);
        QVERIFY(registry().validate().isEmpty());
    }

    void fullTableValidationIncludesAdditionalDefaults()
    {
        Command a = command("test.first", "Ctrl+A");
        a.additionalShortcuts = {key("Alt+B")};
        QVERIFY(registry().add(a));
        QVERIFY(registry().add(command("test.second", "Alt+B")));
        QVERIFY(registry().add(command("test.third")));
        QStringList errors;
        QVERIFY(!registry().setShortcuts(third, {key("Ctrl+T")}, &errors));
        QVERIFY(errors.join(QLatin1Char('\n')).contains(first));
        QVERIFY(errors.join(QLatin1Char('\n')).contains(second));
        QVERIFY(registry().shortcutOverrides().isEmpty());
    }

    void persistedOverridesRoundTripIncludingUnbound()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+B")));
        QVERIFY(registry().add(command("test.third", "Ctrl+C")));
        const CommandRegistry::ShortcutBindings custom{
            {first, {key("Alt+K"), key("Ctrl+K, Ctrl+C"), key("Ctrl+,")}}, {second, {}}};
        QVERIFY(registry().applyShortcutOverrides(custom));
        const QString file = dir.filePath(QStringLiteral("settings.ini"));
        {
            QSettings output(file, QSettings::IniFormat);
            output.setValue(QStringLiteral("other/setting"), QStringLiteral("preserved"));
            QStringList errors;
            QVERIFY2(registry().saveShortcuts(output, &errors), qPrintable(errors.join(QLatin1Char('\n'))));
            const QJsonDocument document = QJsonDocument::fromJson(output.value(QStringLiteral("commands/shortcuts")).toByteArray());
            QCOMPARE(document.object().value(QStringLiteral("version")).toInt(), 1);
        }
        QVERIFY(registry().resetAllShortcuts());
        QSettings input(file, QSettings::IniFormat);
        QStringList errors;
        QVERIFY2(registry().loadShortcuts(input, &errors), qPrintable(errors.join(QLatin1Char('\n'))));
        QCOMPARE(registry().shortcutOverrides(), custom);
        QVERIFY(registry().effectiveShortcuts(second).isEmpty());
        QCOMPARE(registry().effectiveShortcuts(third), QList<QKeySequence>({key("Ctrl+C")}));
        QCOMPARE(input.value(QStringLiteral("other/setting")).toString(), QStringLiteral("preserved"));
    }

    void malformedPersistence_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("broken-json") << QByteArray("{broken");
        QTest::newRow("wrong-root") << QByteArray("[]");
        QTest::newRow("unknown-version") << QByteArray(R"({"version":2,"overrides":{}})");
        QTest::newRow("fractional-version") << QByteArray(R"({"version":1.5,"overrides":{}})");
        QTest::newRow("missing-overrides") << QByteArray(R"({"version":1})");
        QTest::newRow("wrong-array-type") << QByteArray(R"({"version":1,"overrides":{"test.first":"Ctrl+A"}})");
        QTest::newRow("wrong-binding-type") << QByteArray(R"({"version":1,"overrides":{"test.first":[12]}})");
        QTest::newRow("empty-binding") << QByteArray(R"({"version":1,"overrides":{"test.first":[""]}})");
        QTest::newRow("invalid-binding") << QByteArray(R"({"version":1,"overrides":{"test.first":["Not a key"]}})");
        QTest::newRow("partial-parse") << QByteArray(R"({"version":1,"overrides":{"test.first":["Ctrl+A, Ctrl+B, Ctrl+C, Ctrl+D, Ctrl+E"]}})");
        QTest::newRow("unknown-id") << QByteArray(R"({"version":1,"overrides":{"test.missing":[]}})");
        QTest::newRow("collision") << QByteArray(R"({"version":1,"overrides":{"test.first":["Ctrl+B"]}})");
        QTest::newRow("prefix-collision") << QByteArray(R"({"version":1,"overrides":{"test.first":["Ctrl+B, Ctrl+C"]}})");
    }

    void malformedPersistence()
    {
        QFETCH(QByteArray, json);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+B")));
        QVERIFY(registry().setShortcuts(first, {key("Alt+A")}));
        const auto before = registry().shortcutOverrides();
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("commands/shortcuts"), json);
        QSignalSpy changes(&registry(), &CommandRegistry::shortcutsChanged);
        QStringList errors;
        QVERIFY(!registry().loadShortcuts(settings, &errors));
        QVERIFY(!errors.isEmpty());
        QCOMPARE(registry().shortcutOverrides(), before);
        QCOMPARE(changes.count(), 0);
    }

    void missingPersistenceRestoresDefaults()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().setShortcuts(first, {}));
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        QVERIFY(registry().loadShortcuts(settings));
        QVERIFY(registry().shortcutOverrides().isEmpty());
        QCOMPARE(registry().effectiveShortcuts(first), QList<QKeySequence>({key("Ctrl+A")}));
    }

    void refusesNonTextPersistence()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("commands/shortcuts"), 12);
        QStringList errors;
        QVERIFY(!registry().loadShortcuts(settings, &errors));
        QVERIFY(!errors.isEmpty());
    }

    void saveRejectsConflictsBeforeWriting()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().add(command("test.second", "Ctrl+A")));
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("commands/shortcuts"), QByteArray("old-value"));
        QStringList errors;
        QVERIFY(!registry().saveShortcuts(settings, &errors));
        QCOMPARE(settings.value(QStringLiteral("commands/shortcuts")).toByteArray(), QByteArray("old-value"));
        QVERIFY(!errors.isEmpty());
    }

    void saveReportsAccessFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QSettings settings(dir.path(), QSettings::IniFormat);
        QStringList errors;
        QVERIFY(!registry().saveShortcuts(settings, &errors));
        QVERIFY(!errors.isEmpty());
        QCOMPARE(settings.status(), QSettings::AccessError);
        QVERIFY(!settings.contains(QStringLiteral("commands/shortcuts")));
    }

    void saveFailureRestoresPreviousSettingsCache()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QSettings settings(dir.path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("commands/shortcuts"), QByteArray("previous"));
        QStringList errors;
        QVERIFY(!registry().saveShortcuts(settings, &errors));
        QCOMPARE(settings.value(QStringLiteral("commands/shortcuts")).toByteArray(), QByteArray("previous"));
    }

    void clearRemovesOverrides()
    {
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QVERIFY(registry().setShortcuts(first, {}));
        registry().clear();
        QVERIFY(registry().shortcutOverrides().isEmpty());
        QVERIFY(registry().add(command("test.first", "Ctrl+A")));
        QCOMPARE(registry().effectiveShortcuts(first), QList<QKeySequence>({key("Ctrl+A")}));
    }
};

QTEST_GUILESS_MAIN(TstCommandShortcuts)
#include "tst_commandshortcuts.moc"
