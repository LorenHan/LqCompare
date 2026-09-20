#ifndef LQCOMPARE_OPTIONSREPOSITORY_H
#define LQCOMPARE_OPTIONSREPOSITORY_H

#include <QObject>
#include <QByteArray>
#include <QVariantMap>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Settings {

struct StorageLocation {
    QString directory;
    bool portable = false;
    QString filePath() const;
    static StorageLocation detect(bool forcePortable = false,
                                  const QString &executableDirectory = QString(),
                                  const QString &standardDirectory = QString());
};

struct OptionDefinition {
    QString key;
    QString category; // general, display, logging
    QString title;
    QString description;
    QVariant defaultValue;
    QStringList choices; // persisted identifiers, not translated display names
    int minimum = 0;
    int maximum = 0;
    bool machineSpecific = false;
    bool restartRequired = false;

    // 数字项的单位后缀（" pt" / " MB" / " 个"）。
    //
    // 为什么放在**最后**：定义表是按位置初始化的，把新字段插在中间会让所有
    // 只写到 `maximum` 的行整体错位一格——`false` 会被塞给 `unit`，
    // 报错是一句「no matching constructor for QVector<OptionDefinition>」，
    // 指不到任何一行。追加在末尾则老行仍然合法（未提供的成员走默认值）。
    // 顺带一提，末位成员**必须**有默认值：否则每个老行都会触发
    // `-Wmissing-field-initializers`，本仓「0 warning」就保不住了。
    QString unit = QString();
};

struct OperationResult {
    bool ok = false;
    QString error;
    QString backupPath;
    QStringList changedKeys;
};

struct ImportEntry {
    QString key;
    QVariant currentValue;
    QVariant incomingValue;
    bool conflict = false;
};

struct ImportPreview {
    bool ok = false;
    QString error;
    QVector<ImportEntry> entries;
    QStringList ignoredKeys;
    QByteArray sourceFingerprint;
};

// Owns ONLY global options. Session/type defaults keep their existing repository.
// All writes are atomic; reset/import first create an exact backup of current state.
class OptionsRepository : public QObject {
    Q_OBJECT
public:
    explicit OptionsRepository(StorageLocation location = StorageLocation::detect(),
                               QObject *parent = nullptr);
    static const QVector<OptionDefinition> &definitions();
    static QVariantMap defaults();
    static const OptionDefinition *definition(const QString &key);
    static QString validate(const QString &key, const QVariant &value);

    StorageLocation location() const;
    QVariant value(const QString &key) const;
    QVariantMap values() const;
    OperationResult load(); // missing file succeeds; corrupt data falls back and is backed up
    OperationResult apply(const QVariantMap &changes);
    OperationResult reset(const QString &category = QString()); // caller confirms scope first
    OperationResult exportFile(const QString &path, const QStringList &categories = {},
                               bool includeMachineSpecific = false) const;
    ImportPreview previewImport(const QString &path) const;
    OperationResult importFile(const QString &path, const QStringList &selectedKeys,
                               const QByteArray &expectedFingerprint = {});
    QString lastLoadError() const;

signals:
    void changed(const QStringList &keys);
    void loadWarning(const QString &message);

private:
    OperationResult writeValues(const QVariantMap &values, bool requireBackup,
                                bool allowRecovery = false, bool discardUnknown = false);
    OperationResult backupCurrent() const;
    QString validateDestination(const QVariantMap &values, bool checkWritable) const;
    StorageLocation m_location;
    QVariantMap m_values;
    QVariantMap m_unknown;
    QString m_loadError;
    bool m_writeBlocked = false;
};

} // namespace Settings
} // namespace LqCompare
#endif
