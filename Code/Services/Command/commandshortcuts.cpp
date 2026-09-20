#include "commandregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>

namespace LqCompare {
namespace {

constexpr auto settingsKey = "commands/shortcuts";

QList<QKeySequence> defaultShortcuts(const Command &command)
{
    QList<QKeySequence> result;
    if (!command.shortcut.isEmpty()) {
        result.append(command.shortcut);
    }
    result.append(command.additionalShortcuts);
    return result;
}

bool isValidSequence(const QKeySequence &sequence)
{
    if (sequence.isEmpty() || sequence.count() < 1 || sequence.count() > 4) {
        return false;
    }
    for (int i = 0; i < sequence.count(); ++i) {
        const int key = sequence[i] & ~int(Qt::KeyboardModifierMask);
        if (!key || key == Qt::Key_unknown || key == Qt::Key_Control
            || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta
            || key == Qt::Key_AltGr) {
            return false;
        }
    }
    // The round trip also rejects holes and key values that Qt cannot serialize.
    const QString text = sequence.toString(QKeySequence::PortableText);
    return !text.isEmpty()
        && QKeySequence::fromString(text, QKeySequence::PortableText) == sequence;
}

bool sequencesConflict(const QKeySequence &first, const QKeySequence &second)
{
    const int common = qMin(first.count(), second.count());
    for (int i = 0; i < common; ++i) {
        if (first[i] != second[i]) {
            return false;
        }
    }
    // Equal bindings and a chord that starts with another complete binding are
    // both ambiguous. A longer common prefix alone does not imply a conflict.
    return common > 0;
}

bool resultWithErrors(const QStringList &problems, QStringList *errors)
{
    if (errors) {
        *errors = problems;
    }
    return problems.isEmpty();
}

QString storageError(QSettings::Status status)
{
    return status == QSettings::AccessError
        ? QStringLiteral("无法访问快捷键配置文件")
        : QStringLiteral("快捷键配置文件格式损坏");
}

} // namespace

QList<QKeySequence> CommandRegistry::effectiveShortcuts(const QString &id) const
{
    const auto command = m_commands.constFind(id);
    if (command == m_commands.constEnd()) {
        return {};
    }
    const auto custom = m_shortcutOverrides.constFind(id);
    return custom == m_shortcutOverrides.constEnd()
        ? defaultShortcuts(command.value()) : custom.value();
}

CommandRegistry::ShortcutBindings CommandRegistry::shortcutOverrides() const
{
    return m_shortcutOverrides;
}

QStringList CommandRegistry::validateShortcuts(const ShortcutBindings &overrides) const
{
    QStringList problems;
    for (auto entry = overrides.constBegin(); entry != overrides.constEnd(); ++entry) {
        if (!m_commands.contains(entry.key())) {
            problems.append(QStringLiteral("%1：快捷键覆盖引用了未知命令").arg(entry.key()));
        }
    }

    struct Binding {
        QString id;
        QKeySequence sequence;
    };
    QList<Binding> accepted;
    for (const QString &id : m_order) {
        const QList<QKeySequence> bindings = overrides.contains(id)
            ? overrides.value(id) : defaultShortcuts(m_commands.value(id));
        for (int index = 0; index < bindings.size(); ++index) {
            const QKeySequence &sequence = bindings.at(index);
            if (!isValidSequence(sequence)) {
                problems.append(QStringLiteral("%1：第 %2 个快捷键无效；解除绑定请使用空列表")
                                    .arg(id).arg(index + 1));
                continue;
            }
            for (const Binding &previous : accepted) {
                if (sequencesConflict(previous.sequence, sequence)) {
                    problems.append(QStringLiteral("%1：快捷键 %2 与 %3 的快捷键 %4 冲突")
                                        .arg(id,
                                             sequence.toString(QKeySequence::PortableText),
                                             previous.id,
                                             previous.sequence.toString(QKeySequence::PortableText)));
                }
            }
            accepted.append({id, sequence});
        }
    }
    return problems;
}

bool CommandRegistry::setShortcuts(const QString &id, const QList<QKeySequence> &shortcuts,
                                   QStringList *errors)
{
    ShortcutBindings candidate = m_shortcutOverrides;
    candidate.insert(id, shortcuts);
    return applyShortcutOverrides(candidate, errors);
}

bool CommandRegistry::resetShortcuts(const QString &id, QStringList *errors)
{
    if (!m_commands.contains(id)) {
        return resultWithErrors({QStringLiteral("%1：无法恢复未知命令的快捷键").arg(id)}, errors);
    }
    ShortcutBindings candidate = m_shortcutOverrides;
    candidate.remove(id);
    return applyShortcutOverrides(candidate, errors);
}

bool CommandRegistry::resetAllShortcuts(QStringList *errors)
{
    return applyShortcutOverrides({}, errors);
}

bool CommandRegistry::applyShortcutOverrides(const ShortcutBindings &overrides,
                                             QStringList *errors)
{
    const QStringList problems = validateShortcuts(overrides);
    if (!resultWithErrors(problems, errors)) {
        return false;
    }

    QStringList changedIds;
    for (const QString &id : m_order) {
        const QList<QKeySequence> candidate = overrides.contains(id)
            ? overrides.value(id) : defaultShortcuts(m_commands.value(id));
        if (effectiveShortcuts(id) != candidate) {
            changedIds.append(id);
        }
    }
    // Commit the complete map before emitting, so every observer sees the same
    // completed transaction, including when two commands exchange bindings.
    m_shortcutOverrides = overrides;
    for (const QString &id : changedIds) {
        emit commandChanged(id);
        emit shortcutsChanged(id);
    }
    return true;
}

bool CommandRegistry::saveShortcuts(QSettings &settings, QStringList *errors) const
{
    const QStringList problems = validateShortcuts(m_shortcutOverrides);
    if (!resultWithErrors(problems, errors)) {
        return false;
    }

    QJsonObject overrides;
    for (auto entry = m_shortcutOverrides.constBegin(); entry != m_shortcutOverrides.constEnd();
         ++entry) {
        QJsonArray bindings;
        for (const QKeySequence &sequence : entry.value()) {
            bindings.append(sequence.toString(QKeySequence::PortableText));
        }
        overrides.insert(entry.key(), bindings);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("overrides"), overrides);
    const QString key = QString::fromLatin1(settingsKey);
    const bool hadPreviousValue = settings.contains(key);
    const QVariant previousValue = settings.value(key);
    settings.setValue(key, QJsonDocument(root).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        // QSettings retains failed writes in its cache and can retry them on
        // destruction. Restore the previous value so a later retry cannot save
        // a draft that the caller already reported as failed.
        if (hadPreviousValue) {
            settings.setValue(key, previousValue);
        } else {
            settings.remove(key);
        }
        return resultWithErrors({storageError(settings.status())}, errors);
    }
    return resultWithErrors({}, errors);
}

bool CommandRegistry::loadShortcuts(QSettings &settings, QStringList *errors)
{
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        return resultWithErrors({storageError(settings.status())}, errors);
    }
    if (!settings.contains(QString::fromLatin1(settingsKey))) {
        return applyShortcutOverrides({}, errors);
    }

    const QVariant stored = settings.value(QString::fromLatin1(settingsKey));
    if (stored.type() != QVariant::ByteArray && stored.type() != QVariant::String) {
        return resultWithErrors({QStringLiteral("快捷键配置必须为 JSON 文本")}, errors);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(stored.toByteArray(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return resultWithErrors({QStringLiteral("快捷键配置 JSON 损坏：%1").arg(parseError.errorString())},
                                errors);
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("version")).isDouble()
        || root.value(QStringLiteral("version")).toDouble() != 1.0
        || !root.value(QStringLiteral("overrides")).isObject()) {
        return resultWithErrors({QStringLiteral("快捷键配置版本或结构无效")}, errors);
    }

    ShortcutBindings candidate;
    QStringList problems;
    const QJsonObject overrides = root.value(QStringLiteral("overrides")).toObject();
    for (auto entry = overrides.constBegin(); entry != overrides.constEnd(); ++entry) {
        if (!entry.value().isArray()) {
            problems.append(QStringLiteral("%1：快捷键配置应为数组").arg(entry.key()));
            continue;
        }
        QList<QKeySequence> bindings;
        const QJsonArray array = entry.value().toArray();
        for (int index = 0; index < array.size(); ++index) {
            const QJsonValue value = array.at(index);
            const QString text = value.toString();
            const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
            // Reject partial parses instead of silently changing a damaged binding.
            if (!value.isString() || !isValidSequence(sequence)
                || sequence.toString(QKeySequence::PortableText) != text) {
                problems.append(QStringLiteral("%1：第 %2 个持久化快捷键无效")
                                    .arg(entry.key()).arg(index + 1));
            } else {
                bindings.append(sequence);
            }
        }
        candidate.insert(entry.key(), bindings);
    }
    problems.append(validateShortcuts(candidate));
    if (!resultWithErrors(problems, errors)) {
        return false;
    }
    return applyShortcutOverrides(candidate, errors);
}

} // namespace LqCompare
