#ifndef LQCOMPARE_SYNCPREVIEWDIALOG_H
#define LQCOMPARE_SYNCPREVIEWDIALOG_H

#include "syncengine.h"
#include <QDialog>
#include <functional>
#include <memory>

namespace LqCompare {

// Standalone preview/confirmation window. All writes require an explicit user
// confirmation tied to the selected plan; directory changes invalidate it.
class SyncPreviewDialog : public QDialog
{
    Q_OBJECT
public:
    enum class ConfirmationKind { Execute, LargeDelete, RestoreBackup, UndoTrash };
    using ConfirmationHandler = std::function<bool(ConfirmationKind, const QString &)>;

    explicit SyncPreviewDialog(QWidget *parent = nullptr);
    ~SyncPreviewDialog() override;
    void setDirectories(const QString &left, const QString &right);
    void setOptions(const Sync::Options &options);
    Sync::Options options() const;
    void setBaseline(const Sync::Baseline &baseline);
    void clearBaseline();
    bool isBusy() const;
    bool hasValidPlan() const;
    Sync::Plan currentPlan() const;
    Sync::Report lastReport() const;
    QString statusText() const;
    // Executor is retained for undo and can use an isolated backup/trash in tests.
    // Replacing it is rejected while work is in progress or recovery exists.
    bool setExecutor(std::shared_ptr<Sync::Executor> executor);
    void setConfirmationHandler(ConfirmationHandler handler);
    bool exportPlan(const QString &path, QString *error = nullptr) const;

public slots:
    bool startPreview();
    void cancelOperation();
    bool executePlan();
    bool loadBaseline(const QString &path);
    bool saveBaseline(const QString &path);
    bool restoreBackup(int reportItemIndex);
    bool undoLastTrash();
    void reject() override;

signals:
    void busyChanged(bool busy);
    void previewReady();
    void executionFinished();
    void recoveryFinished(bool succeeded);
    void baselineLoaded(bool succeeded);
    void baselineSaved(bool succeeded);
    void planInvalidated();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LqCompare
#endif
