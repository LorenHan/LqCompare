#ifndef LQCOMPARE_OPTIONSDIALOG_H
#define LQCOMPARE_OPTIONSDIALOG_H

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QVariantMap>

#include "optionsrepository.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;

namespace LqCompare {
namespace Options {

class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    enum class PendingChoice { Apply, Discard, Cancel };
    explicit OptionsDialog(Settings::OptionsRepository *repository, QWidget *parent = nullptr);
    QString currentCategory() const { return m_category; }
    QStringList categories() const;
    QWidget *editorFor(const QString &key) const;
    QVariant draftValue(const QString &key) const;
    bool setDraftValue(const QString &key, const QVariant &value);
    bool isDirty() const;
    QString lastError() const { return m_lastError; }
    QStringList search(const QString &text);
    bool selectCategory(const QString &category);
    bool applyChanges();
    bool resetCategory(const QString &category);
    bool resetAll();
    // Import preview and per-key choice use the same path as the visible import button.
    bool importSettings(const QString &path);
    bool exportSettings(const QString &path, bool appearanceOnly = false,
                        bool includeMachineSpecific = false);

public slots:
    void accept() override;
    void reject() override;

signals:
    void applied(const QStringList &keys);
    void operationFailed(const QString &message);

protected:
    virtual PendingChoice confirmPendingChanges(const QString &destination);
    virtual bool confirmReset(const QString &category);
    // Return false to cancel; selectedKeys contains exactly the user's checked rows.
    virtual bool chooseImportEntries(const Settings::ImportPreview &preview,
                                     QStringList *selectedKeys);

private:
    QWidget *buildPage(const QString &category);
    QWidget *buildEditor(const Settings::OptionDefinition &definition);
    QVariant readEditor(const Settings::OptionDefinition &definition) const;
    void writeEditor(const Settings::OptionDefinition &definition, const QVariant &value);
    void refreshEditors();
    void refreshState();
    // 「文件操作」页的安全提示随草稿变化：选了永久删除就必须当场看见「不可恢复」。
    // 静态放一段说明做不到这件事——用户改完选项、提示不变，他只会以为提示
    // 与选项无关。
    void updateFileOpsHint();
    void setStatus(const QString &message, bool error = false);
    bool resolvePending(const QString &destination);
    bool finishOperation(const Settings::OperationResult &result, const QString &success);
    void setSelectedCategory(const QString &category);

    QPointer<Settings::OptionsRepository> m_repository;
    QVariantMap m_draft;
    QVariantMap m_baseline;
    QHash<QString, QWidget *> m_editors;
    QHash<QString, QWidget *> m_rows;
    QHash<QString, QLabel *> m_titles;
    QHash<QString, QWidget *> m_pages;
    QTreeWidget *m_categories = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_fileOpsHint = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QString m_category;
    QString m_lastError;
    bool m_loading = false;
};

} // namespace Options
} // namespace LqCompare
#endif
