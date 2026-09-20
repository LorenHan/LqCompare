#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "optionsrepository.h"
#include "logging.h"

using namespace LqCompare::Settings;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray envelope(const QVariantMap &values, int version = 1)
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("LqCompare.Options"));
    object.insert(QStringLiteral("version"), version);
    object.insert(version == 0 ? QStringLiteral("values") : QStringLiteral("settings"),
                  QJsonObject::fromVariantMap(values));
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QVariantMap persistedValues(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object()
            .value(QStringLiteral("settings")).toObject().toVariantMap();
}

QStringList keys(const ImportPreview &preview)
{
    QStringList result;
    for (const auto &entry : preview.entries)
        result.append(entry.key);
    return result;
}

const ImportEntry *entry(const ImportPreview &preview, const QString &key)
{
    for (const auto &candidate : preview.entries) {
        if (candidate.key == key)
            return &candidate;
    }
    return nullptr;
}
}

class OptionsTests : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        LqCompare::Log::clearSinks();
        LqCompare::Log::setLevel(LqCompare::Log::Level::Info);
        LqCompare::Log::setLogFile(QString());
    }

    void missingFileUsesDefaults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        const auto result = repository.load();
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QVERIFY(repository.lastLoadError().isEmpty());
        QVERIFY(result.backupPath.isEmpty());
        QVERIFY(!QFileInfo::exists(repository.location().filePath()));
    }

    void applyAndUnicodeRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        QVERIFY(repository.load().ok);
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const QString family = QString::fromUtf8("思源黑体 \"特殊\" \\ café Ω 😀");
        const QString logPath = directory.filePath(QString::fromUtf8("日志与诊断/运行 café.log"));
        const QVariantMap changes{{"display.theme", "dark"},
                                  {"display.uiFontFamily", family},
                                  {"display.uiFontSize", 16},
                                  {"display.contentFontFamily", QString::fromUtf8("等宽 Ω")},
                                  {"display.contentFontSize", 19},
                                  {"general.singleInstance", false},
                                  {"logging.filePath", logPath},
                                  {"logging.level", "debug"}};
        const auto result = repository.apply(changes);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(changed.count(), 1);
        QStringList actual = changed.first().first().toStringList();
        actual.sort();
        QStringList expected = changes.keys();
        expected.sort();
        QCOMPARE(actual, expected);

        OptionsRepository reopened(repository.location());
        const auto loaded = reopened.load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QCOMPARE(reopened.values(), repository.values());
        QCOMPARE(reopened.value("display.uiFontFamily").toString(), family);
        QCOMPARE(reopened.value("logging.filePath").toString(), logPath);
        const QJsonObject document = QJsonDocument::fromJson(readFile(repository.location().filePath())).object();
        QCOMPARE(document.value("format").toString(), QStringLiteral("LqCompare.Options"));
        QCOMPARE(document.value("version").toInt(), 1);
    }

    void invalidApplyIsAtomic_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("unknown") << QString("unknown.option") << QVariant(true);
        QTest::newRow("bool-as-string") << QString("general.singleInstance") << QVariant("false");
        QTest::newRow("bool-as-number") << QString("general.singleInstance") << QVariant(0);
        QTest::newRow("unknown-theme") << QString("display.theme") << QVariant("sepia");
        QTest::newRow("font-size-as-text") << QString("display.uiFontSize") << QVariant("14");
        QTest::newRow("font-size-fraction") << QString("display.uiFontSize") << QVariant(14.5);
        QTest::newRow("ui-font-too-small") << QString("display.uiFontSize") << QVariant(5);
        QTest::newRow("ui-font-too-large") << QString("display.uiFontSize") << QVariant(49);
        QTest::newRow("content-font-too-small") << QString("display.contentFontSize") << QVariant(0);
        QTest::newRow("content-font-too-large") << QString("display.contentFontSize") << QVariant(73);
        QTest::newRow("invalid-log-level") << QString("logging.level") << QVariant("verbose");
        QTest::newRow("relative-log-path") << QString("logging.filePath") << QVariant("logs/run.log");
    }

    void invalidApplyIsAtomic()
    {
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QVariantMap before = repository.values();
        const QByteArray bytesBefore = readFile(repository.location().filePath());
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        QVariantMap changes{{"logging.level", "debug"}, {"display.uiFontFamily", "Changed"}};
        changes.insert(key, value);
        const auto result = repository.apply(changes);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(repository.values(), before);
        QCOMPARE(readFile(repository.location().filePath()), bytesBefore);
        QCOMPARE(changed.count(), 0);
        QVERIFY(result.changedKeys.isEmpty());
    }

    void malformedStorageFallsBackAndKeepsExactBackup_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("truncated-json") << QByteArray("{\n  \"version\": 1, \"settings\": {\"display.theme\": \"dark\"");
        QTest::newRow("array-root") << QByteArray("[1, 2, 3]\n");
        QTest::newRow("wrong-format") << QByteArray("{\"format\":\"OtherApp\",\"version\":1,\"settings\":{}}\n");
        QTest::newRow("version-as-string") << QByteArray("{\"format\":\"LqCompare.Options\",\"version\":\"1\",\"settings\":{}}\n");
        QTest::newRow("wrong-settings-type") << QByteArray("{\"format\":\"LqCompare.Options\",\"version\":1,\"settings\":[]}\n");
        QTest::newRow("invalid-known-value") << envelope({{"general.singleInstance", "not-a-bool"}});
    }

    void malformedStorageFallsBackAndKeepsExactBackup()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        QVERIFY(writeFile(repository.location().filePath(), bytes));
        QSignalSpy warning(&repository, &OptionsRepository::loadWarning);
        QVector<LqCompare::Log::Record> records;
        LqCompare::Log::addSink([&records](const LqCompare::Log::Record &record) { records.append(record); });
        const auto result = repository.load();
        LqCompare::Log::clearSinks();
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!repository.lastLoadError().isEmpty());
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QCOMPARE(warning.count(), 1);
        QVERIFY(!records.isEmpty());
        QCOMPARE(result.backupPath, repository.location().filePath() + ".bak");
        QCOMPARE(readFile(result.backupPath), bytes);
    }

    void legacyVersionMigratesAndFillsMissingDefaults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        // Older files have no format marker and store only explicitly changed keys.
        QJsonObject legacy{{"version", 0},
                           {"values", QJsonObject{{"display.theme", "dark"},
                                                   {"general.singleInstance", false}}}};
        QVERIFY(writeFile(repository.location().filePath(), QJsonDocument(legacy).toJson()));
        const auto loaded = repository.load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QVariantMap expected = OptionsRepository::defaults();
        expected.insert("display.theme", "dark");
        expected.insert("general.singleInstance", false);
        QCOMPARE(repository.values(), expected);
        QVERIFY(repository.apply({{"logging.level", "debug"}}).ok);
        const QJsonObject saved = QJsonDocument::fromJson(readFile(repository.location().filePath())).object();
        QCOMPARE(saved.value("version").toInt(), 1);
        QCOMPARE(saved.value("format").toString(), QString("LqCompare.Options"));
        QVERIFY(saved.value("settings").isObject());
        QVERIFY(!saved.contains("values"));
    }

    void futureVersionIsProtectedUntilExplicitReset()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        const QByteArray bytes = envelope({{"display.theme", "dark"}, {"future.extension", "preserve me"}}, 42);
        QVERIFY(writeFile(repository.location().filePath(), bytes));
        const auto loaded = repository.load();
        QVERIFY(!loaded.ok);
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QCOMPARE(readFile(repository.location().filePath()), bytes);
        const auto refused = repository.apply({{"display.theme", "light"}});
        QVERIFY(!refused.ok);
        QVERIFY(!refused.error.isEmpty());
        QCOMPARE(readFile(repository.location().filePath()), bytes);
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QVERIFY(!repository.reset("display").ok);
        QCOMPARE(readFile(repository.location().filePath()), bytes);

        const auto reset = repository.reset();
        QVERIFY2(reset.ok, qPrintable(reset.error));
        QVERIFY(!reset.backupPath.isEmpty());
        QCOMPARE(readFile(reset.backupPath), bytes);
        QVERIFY(repository.lastLoadError().isEmpty());
        const auto current = QJsonDocument::fromJson(readFile(repository.location().filePath())).object();
        QCOMPARE(current.value("version").toInt(), 1);
        QVERIFY(!current.value("settings").toObject().contains("future.extension"));
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
    }

    void portableDetectionAndStandardStorageAreIsolated()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString executable = directory.filePath("program");
        const QString standard = directory.filePath("user-config");
        QVERIFY(QDir().mkpath(executable));
        const auto standardLocation = StorageLocation::detect(false, executable, standard);
        const auto portableLocation = StorageLocation::detect(true, executable, standard);
        QVERIFY(!standardLocation.portable);
        QVERIFY(portableLocation.portable);
        QCOMPARE(QDir::cleanPath(standardLocation.directory), QDir::cleanPath(standard));
        QCOMPARE(QDir::cleanPath(portableLocation.directory), QDir(executable).filePath("config"));
        QVERIFY(standardLocation.filePath() != portableLocation.filePath());

        OptionsRepository standardRepository(standardLocation);
        OptionsRepository portableRepository(portableLocation);
        QVERIFY(standardRepository.apply({{"display.theme", "dark"}}).ok);
        QVERIFY(portableRepository.apply({{"display.theme", "light"}}).ok);
        OptionsRepository standardReloaded(standardLocation);
        OptionsRepository portableReloaded(portableLocation);
        QVERIFY(standardReloaded.load().ok);
        QVERIFY(portableReloaded.load().ok);
        QCOMPARE(standardReloaded.value("display.theme").toString(), QString("dark"));
        QCOMPARE(portableReloaded.value("display.theme").toString(), QString("light"));
        const QByteArray standardBytes = readFile(standardLocation.filePath());
        QVERIFY(portableReloaded.reset().ok);
        QCOMPARE(readFile(standardLocation.filePath()), standardBytes);

        QVERIFY(writeFile(QDir(executable).filePath("lqcompare.portable"), QByteArray()));
        const auto detected = StorageLocation::detect(false, executable, standard);
        QVERIFY(detected.portable);
        QCOMPARE(detected.directory, portableLocation.directory);
        QVERIFY(QFile::remove(QDir(executable).filePath("lqcompare.portable")));
        QVERIFY(!StorageLocation::detect(false, executable, standard).portable);
    }

    void unknownStoredKeysSurviveEditsButAreNotExported()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        const QVariantMap unknown{{"future.secretReference", "private-reference"},
                                   {"future.nested", QVariantMap{{"enabled", true}, {"name", QString::fromUtf8("保留")}}}};
        QVariantMap input = unknown;
        input.insert("display.theme", "dark");
        QVERIFY(writeFile(repository.location().filePath(), envelope(input)));
        QVERIFY(repository.load().ok);
        QVERIFY(repository.apply({{"display.uiFontSize", 15}}).ok);
        const QVariantMap saved = persistedValues(repository.location().filePath());
        for (auto it = unknown.cbegin(); it != unknown.cend(); ++it)
            QCOMPARE(saved.value(it.key()), it.value());
        const QString exportPath = directory.filePath("export.json");
        QVERIFY(repository.exportFile(exportPath).ok);
        const QVariantMap exported = persistedValues(exportPath);
        for (auto it = unknown.cbegin(); it != unknown.cend(); ++it)
            QVERIFY(!exported.contains(it.key()));

        const QString importPath = directory.filePath("incoming.json");
        QVERIFY(writeFile(importPath, envelope(input)));
        const auto preview = repository.previewImport(importPath);
        QVERIFY2(preview.ok, qPrintable(preview.error));
        for (auto it = unknown.cbegin(); it != unknown.cend(); ++it) {
            QVERIFY(preview.ignoredKeys.contains(it.key()));
            QVERIFY(!keys(preview).contains(it.key()));
        }
    }

    void exportRoundTripOmitsMachinePathsByDefault()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository source({directory.filePath("source"), false});
        QVERIFY(source.apply({{"display.theme", "dark"},
                              {"display.uiFontFamily", QString::fromUtf8("中文 Ω \"font\"")},
                              {"display.contentFontSize", 20},
                              {"general.singleInstance", false},
                              {"logging.filePath", directory.filePath("source.log")}}).ok);
        const QString exportPath = directory.filePath("portable-settings.json");
        QVector<LqCompare::Log::Record> records;
        LqCompare::Log::addSink([&records](const LqCompare::Log::Record &record) { records.append(record); });
        const auto exported = source.exportFile(exportPath);
        LqCompare::Log::clearSinks();
        QVERIFY2(exported.ok, qPrintable(exported.error));
        QVERIFY(!records.isEmpty());
        QVERIFY(!persistedValues(exportPath).contains("logging.filePath"));

        OptionsRepository destination({directory.filePath("destination"), false});
        const QString destinationLog = directory.filePath("destination.log");
        QVERIFY(destination.apply({{"logging.filePath", destinationLog}}).ok);
        const auto preview = destination.previewImport(exportPath);
        QVERIFY2(preview.ok, qPrintable(preview.error));
        const auto imported = destination.importFile(exportPath, keys(preview));
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QCOMPARE(destination.value("logging.filePath").toString(), destinationLog);
        for (const auto &definition : OptionsRepository::definitions()) {
            if (!definition.machineSpecific)
                QCOMPARE(destination.value(definition.key), source.value(definition.key));
        }
        const QString explicitExport = directory.filePath("with-machine-values.json");
        QVERIFY(source.exportFile(explicitExport, {}, true).ok);
        QCOMPARE(persistedValues(explicitExport).value("logging.filePath").toString(), source.value("logging.filePath").toString());
    }

    void appearanceExportContainsOnlyAppearance()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}, {"logging.level", "debug"}}).ok);
        const QString path = directory.filePath("theme.json");
        const auto result = repository.exportFile(path, {"display"});
        QVERIFY2(result.ok, qPrintable(result.error));
        const QVariantMap exported = persistedValues(path);
        QVERIFY(!exported.isEmpty());
        for (const auto &definition : OptionsRepository::definitions())
            QCOMPARE(exported.contains(definition.key), definition.category == "display");
        QCOMPARE(exported.value("display.theme").toString(), QString("dark"));
        const QString before = repository.value("logging.level").toString();
        QVERIFY(repository.reset("display").ok);
        const auto preview = repository.previewImport(path);
        QVERIFY(preview.ok);
        QVERIFY(repository.importFile(path, keys(preview)).ok);
        QCOMPARE(repository.value("display.theme").toString(), QString("dark"));
        QCOMPARE(repository.value("logging.level").toString(), before);
    }

    void importPreviewAndPerItemSelectionPreserveUnselectedValues()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}, {"display.uiFontSize", 17}, {"logging.level", "info"}}).ok);
        const QByteArray before = readFile(repository.location().filePath());
        const QString path = directory.filePath("incoming.json");
        QVERIFY(writeFile(path, envelope({{"display.theme", "light"},
                                           {"display.uiFontSize", 22},
                                           {"logging.level", "info"},
                                           {"future.setting", "ignored"}})));
        const auto preview = repository.previewImport(path);
        QVERIFY2(preview.ok, qPrintable(preview.error));
        QCOMPARE(preview.entries.size(), 3);
        QVERIFY(preview.ignoredKeys.contains("future.setting"));
        QVERIFY(entry(preview, "display.theme"));
        QVERIFY(entry(preview, "display.theme")->conflict);
        QCOMPARE(entry(preview, "display.theme")->currentValue.toString(), QString("dark"));
        QCOMPARE(entry(preview, "display.theme")->incomingValue.toString(), QString("light"));
        QVERIFY(entry(preview, "logging.level"));
        QVERIFY(!entry(preview, "logging.level")->conflict);
        QCOMPARE(readFile(repository.location().filePath()), before);

        QSignalSpy changed(&repository, &OptionsRepository::changed);
        QVector<LqCompare::Log::Record> records;
        LqCompare::Log::addSink([&records](const LqCompare::Log::Record &record) { records.append(record); });
        const auto imported = repository.importFile(path, {"display.theme"}, preview.sourceFingerprint);
        LqCompare::Log::clearSinks();
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QVERIFY(!records.isEmpty());
        QCOMPARE(readFile(imported.backupPath), before);
        QCOMPARE(repository.value("display.theme").toString(), QString("light"));
        QCOMPARE(repository.value("display.uiFontSize").toInt(), 17);
        QCOMPARE(repository.value("logging.level").toString(), QString("info"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toStringList(), QStringList{"display.theme"});
    }

    void invalidImportDoesNotAlterExistingState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QVariantMap valuesBefore = repository.values();
        const QByteArray bytesBefore = readFile(repository.location().filePath());
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const QString path = directory.filePath("invalid.json");
        QVERIFY(writeFile(path, envelope({{"display.theme", "light"}, {"display.uiFontSize", -1}})));
        QVERIFY(!repository.previewImport(path).ok);
        const auto imported = repository.importFile(path, {"display.theme", "display.uiFontSize"});
        QVERIFY(!imported.ok);
        QVERIFY(!imported.error.isEmpty());
        QCOMPARE(repository.values(), valuesBefore);
        QCOMPARE(readFile(repository.location().filePath()), bytesBefore);
        QCOMPARE(changed.count(), 0);

        QVERIFY(writeFile(path, envelope({{"display.theme", "light"}})));
        const auto unknownSelected = repository.importFile(path, {"general.singleInstance"});
        QVERIFY(!unknownSelected.ok);
        QCOMPARE(repository.values(), valuesBefore);
        QCOMPARE(readFile(repository.location().filePath()), bytesBefore);
        QCOMPARE(changed.count(), 0);
    }

    void categoryResetPreservesOtherCategoriesAndBacksUp()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        QVERIFY(repository.apply({{"display.theme", "dark"},
                                  {"display.uiFontSize", 18},
                                  {"general.singleInstance", false},
                                  {"logging.level", "debug"}}).ok);
        const QVariantMap before = repository.values();
        const QByteArray bytesBefore = readFile(repository.location().filePath());
        const auto reset = repository.reset("display");
        QVERIFY2(reset.ok, qPrintable(reset.error));
        QCOMPARE(readFile(reset.backupPath), bytesBefore);
        for (const auto &definition : OptionsRepository::definitions()) {
            const QVariant expected = definition.category == "display" ? definition.defaultValue : before.value(definition.key);
            QCOMPARE(repository.value(definition.key), expected);
        }
        const QByteArray after = readFile(repository.location().filePath());
        const QVariantMap valuesAfter = repository.values();
        const auto rejected = repository.reset("missing-category");
        QVERIFY(!rejected.ok);
        QCOMPARE(readFile(repository.location().filePath()), after);
        QCOMPARE(repository.values(), valuesAfter);
        const auto all = repository.reset();
        QVERIFY2(all.ok, qPrintable(all.error));
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QCOMPARE(readFile(all.backupPath), after);
        QCOMPARE(readFile(reset.backupPath), bytesBefore);
        QVERIFY(all.backupPath != reset.backupPath);
    }

    void writeFailureDoesNotCommitInMemory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString blocker = directory.filePath("not-a-directory");
        QVERIFY(writeFile(blocker, "keep this file"));
        OptionsRepository repository({QDir(blocker).filePath("settings"), false});
        const QVariantMap before = repository.values();
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const auto result = repository.apply({{"display.theme", "dark"}});
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(repository.values(), before);
        QCOMPARE(readFile(blocker), QByteArray("keep this file"));
        QCOMPARE(changed.count(), 0);
    }

    void backupFailureAbortsImportAndReset()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QVariantMap before = repository.values();
        const QString path = repository.location().filePath();
        const QByteArray bytesBefore = readFile(path);
        QVERIFY(QFile::rename(path, path + ".original"));
        QVERIFY(QDir().mkdir(path)); // An unreadable-as-a-file target works even when tests run as root.
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const QString importPath = directory.filePath("import.json");
        QVERIFY(writeFile(importPath, envelope({{"display.theme", "light"}})));
        const auto imported = repository.importFile(importPath, {"display.theme"});
        QVERIFY(!imported.ok);
        QVERIFY(!imported.error.isEmpty());
        QVERIFY(imported.backupPath.isEmpty());
        QCOMPARE(repository.values(), before);
        const auto reset = repository.reset();
        QVERIFY(!reset.ok);
        QVERIFY(!reset.error.isEmpty());
        QVERIFY(reset.backupPath.isEmpty());
        QCOMPARE(repository.values(), before);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(readFile(path + ".original"), bytesBefore);
        QVERIFY(QFileInfo(path).isDir());
    }

    void existingBackupsAreNeverOverwritten()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QString originalBackup = repository.location().filePath() + ".bak";
        const QByteArray originalBytes("earlier backup\r\nwith exact bytes\0end", 35);
        QVERIFY(writeFile(originalBackup, originalBytes));
        const QByteArray beforeReset = readFile(repository.location().filePath());
        const auto reset = repository.reset("display");
        QVERIFY2(reset.ok, qPrintable(reset.error));
        QCOMPARE(readFile(reset.backupPath), beforeReset);
        QVERIFY(reset.backupPath != originalBackup);
        const QByteArray beforeImport = readFile(repository.location().filePath());
        const QString importPath = directory.filePath("import.json");
        QVERIFY(writeFile(importPath, envelope({{"display.theme", "light"}})));
        const auto imported = repository.importFile(importPath, {"display.theme"});
        QVERIFY2(imported.ok, qPrintable(imported.error));
        QVERIFY(imported.backupPath != reset.backupPath);
        QVERIFY(imported.backupPath != originalBackup);
        QCOMPARE(readFile(originalBackup), originalBytes);
        QCOMPARE(readFile(reset.backupPath), beforeReset);
        QCOMPARE(readFile(imported.backupPath), beforeImport);
    }

    void importRevalidatesSourceAfterPreview_data()
    {
        QTest::addColumn<QString>("replacement");
        QTest::newRow("source-removed") << QString("remove");
        QTest::newRow("source-corrupted") << QString("corrupt");
        QTest::newRow("source-replaced-with-valid-json") << QString("replace");
    }

    void importRevalidatesSourceAfterPreview()
    {
        QFETCH(QString, replacement);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QVariantMap before = repository.values();
        const QByteArray bytesBefore = readFile(repository.location().filePath());
        const QString importPath = directory.filePath("import.json");
        QVERIFY(writeFile(importPath, envelope({{"display.theme", "light"}})));
        const auto preview = repository.previewImport(importPath);
        QVERIFY(preview.ok);
        QVERIFY(!preview.sourceFingerprint.isEmpty());
        if (replacement == "remove")
            QVERIFY(QFile::remove(importPath));
        else if (replacement == "corrupt")
            QVERIFY(writeFile(importPath, "not valid json"));
        else
            QVERIFY(writeFile(importPath, envelope({{"display.theme", "system"}})));
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const auto imported = repository.importFile(importPath, keys(preview), preview.sourceFingerprint);
        QVERIFY(!imported.ok);
        QVERIFY(!imported.error.isEmpty());
        QVERIFY(imported.backupPath.isEmpty());
        QCOMPARE(repository.values(), before);
        QCOMPARE(readFile(repository.location().filePath()), bytesBefore);
        QCOMPARE(changed.count(), 0);
    }

    void directoryAtStorageFileIsAnError()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.path(), false});
        QVERIFY(QDir().mkdir(repository.location().filePath()));
        QSignalSpy warning(&repository, &OptionsRepository::loadWarning);
        const auto loaded = repository.load();
        QVERIFY(!loaded.ok);
        QVERIFY(!loaded.error.isEmpty());
        QVERIFY(!repository.lastLoadError().isEmpty());
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QCOMPARE(warning.count(), 1);
        QVERIFY(loaded.backupPath.isEmpty());
        QVERIFY(!repository.apply({{"display.theme", "dark"}}).ok);
        QVERIFY(QFileInfo(repository.location().filePath()).isDir());
    }

    void unsafeLogDestinationIsRejectedOnApplyAndImport_data()
    {
        QTest::addColumn<QString>("destinationKind");
        QTest::newRow("settings-file") << QString("settings");
        QTest::newRow("settings-backup") << QString("backup");
        QTest::newRow("existing-directory") << QString("directory");
        QTest::newRow("file-as-ancestor") << QString("ancestor");
    }

    void unsafeLogDestinationIsRejectedOnApplyAndImport()
    {
        QFETCH(QString, destinationKind);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QVariantMap before = repository.values();
        const QByteArray bytesBefore = readFile(repository.location().filePath());
        QString destination = repository.location().filePath();
        if (destinationKind == "backup") {
            destination += ".bak";
        } else if (destinationKind == "directory") {
            destination = directory.filePath("directory");
            QVERIFY(QDir().mkdir(destination));
        } else if (destinationKind == "ancestor") {
            const QString blocker = directory.filePath("blocker");
            QVERIFY(writeFile(blocker, "must remain a file"));
            destination = QDir(blocker).filePath("log.txt");
        }
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const auto applied = repository.apply({{"logging.filePath", destination}, {"display.theme", "light"}});
        QVERIFY(!applied.ok);
        QVERIFY(!applied.error.isEmpty());
        QCOMPARE(repository.values(), before);
        QCOMPARE(readFile(repository.location().filePath()), bytesBefore);
        const QString importPath = directory.filePath("import.json");
        QVERIFY(writeFile(importPath, envelope({{"logging.filePath", destination}, {"display.theme", "light"}})));
        const auto imported = repository.importFile(importPath, {"logging.filePath", "display.theme"});
        QVERIFY(!imported.ok);
        QVERIFY(!imported.error.isEmpty());
        QCOMPARE(repository.values(), before);
        QCOMPARE(readFile(repository.location().filePath()), bytesBefore);
        QCOMPARE(changed.count(), 0);
    }

    void defaultLogSymlinkCannotAliasSettingsOrBackup_data()
    {
        QTest::addColumn<bool>("aliasBackup");
        QTest::newRow("settings-file") << false;
        QTest::newRow("settings-backup") << true;
    }

    void defaultLogSymlinkCannotAliasSettingsOrBackup()
    {
#ifdef Q_OS_WIN
        QSKIP("QFile::link creates a Windows shortcut; this case requires a filesystem symlink.");
#else
        QFETCH(bool, aliasBackup);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        const QString settingsPath = repository.location().filePath();
        const QByteArray settingsBytes = envelope({{"display.theme", "dark"},
                                                  {"logging.filePath", ""}});
        QVERIFY(writeFile(settingsPath, settingsBytes));
        const QString aliasTarget = aliasBackup ? settingsPath + ".bak" : settingsPath;
        const QByteArray backupBytes("previous configuration backup\n");
        if (aliasBackup)
            QVERIFY(writeFile(aliasTarget, backupBytes));
        const QString defaultLog = QDir(repository.location().directory).filePath("logs/lqcompare.log");
        QVERIFY(QDir().mkpath(QFileInfo(defaultLog).absolutePath()));
        QVERIFY(QFile::link(aliasTarget, defaultLog));
        QVERIFY(QFileInfo(defaultLog).isSymLink());
        QCOMPARE(QFileInfo(defaultLog).canonicalFilePath(), QFileInfo(aliasTarget).canonicalFilePath());

        QSignalSpy warning(&repository, &OptionsRepository::loadWarning);
        const auto loaded = repository.load();
        QVERIFY(!loaded.ok);
        QVERIFY(!loaded.error.isEmpty());
        QVERIFY(!repository.lastLoadError().isEmpty());
        QCOMPARE(warning.count(), 1);
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QCOMPARE(readFile(settingsPath), settingsBytes);
        QCOMPARE(readFile(loaded.backupPath), settingsBytes);

        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const auto applied = repository.apply({{"display.theme", "light"}});
        QVERIFY(!applied.ok);
        QVERIFY(!applied.error.isEmpty());
        QVERIFY(applied.backupPath.isEmpty());
        QCOMPARE(repository.values(), OptionsRepository::defaults());
        QCOMPARE(readFile(settingsPath), settingsBytes);
        QCOMPARE(changed.count(), 0);
        QVERIFY(QFileInfo(defaultLog).isSymLink());
        if (aliasBackup)
            QCOMPARE(readFile(aliasTarget), backupBytes);
#endif
    }

    void exportCannotOverwriteActiveLog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        OptionsRepository repository({directory.filePath("settings"), false});
        QVERIFY(repository.apply({{"display.theme", "dark"}}).ok);
        const QVariantMap valuesBefore = repository.values();
        const QByteArray settingsBefore = readFile(repository.location().filePath());
        const QString logPath = directory.filePath("active.log");
        QVERIFY(writeFile(logPath, "existing diagnostics\r\n"));
        QVERIFY(LqCompare::Log::setLogFile(logPath));
        QCOMPARE(LqCompare::Log::logFile(), logPath);
        LqCompare::Log::write(LqCompare::Log::Level::Warning, "options-test", "active log fixture");
        const QByteArray logBefore = readFile(logPath);
        QVERIFY(logBefore.contains("active log fixture"));
        QSignalSpy changed(&repository, &OptionsRepository::changed);
        const auto exported = repository.exportFile(logPath);
        QVERIFY(!exported.ok);
        QVERIFY(!exported.error.isEmpty());
        QCOMPARE(readFile(logPath), logBefore);
        QCOMPARE(readFile(repository.location().filePath()), settingsBefore);
        QCOMPARE(repository.values(), valuesBefore);
        QCOMPARE(changed.count(), 0);
#ifndef Q_OS_WIN
        const QString alias = directory.filePath("log-alias.json");
        QVERIFY(QFile::link(logPath, alias));
        QVERIFY(QFileInfo(alias).isSymLink());
        const auto aliasedExport = repository.exportFile(alias);
        QVERIFY(!aliasedExport.ok);
        QVERIFY(!aliasedExport.error.isEmpty());
        QCOMPARE(readFile(logPath), logBefore);
        QCOMPARE(readFile(alias), logBefore);
        QVERIFY(QFileInfo(alias).isSymLink());
#endif
    }
};

QTEST_GUILESS_MAIN(OptionsTests)
#include "tst_options.moc"
