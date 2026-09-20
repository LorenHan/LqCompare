#ifndef LQCOMPARE_FOLDERMERGESESSION_H
#define LQCOMPARE_FOLDERMERGESESSION_H

#include "comparesession.h"
#include "foldermergeplan.h"

namespace LqCompare {

class FolderMergeView;

class FolderMergeSession : public CompareSession {
    Q_OBJECT
public:
    explicit FolderMergeSession(QObject *parent = nullptr);
    FolderMergeSession(const QString &base, const QString &left, const QString &right,
                       const QString &output, QObject *parent = nullptr);
    ~FolderMergeSession() override;

    QString basePath() const { return m_paths.base; }
    QString leftPath() const { return m_paths.left; }
    QString rightPath() const { return m_paths.right; }
    QString outputPath() const { return m_paths.output; }
    const FolderMerge::Plan &plan() const { return m_plan; }
    bool isScanning() const { return !m_closing && m_scanner.isRunning(); }
    FolderMergeView *view() const;

    // Changing sources and scanning never silently drop a manual plan.
    bool setPaths(const QString &base, const QString &left, const QString &right,
                  const QString &output, QString *error = nullptr, bool discardManual = false);
    bool rescan(bool discardManual = false, QString *error = nullptr);
    bool setDecision(const QString &relativePath, FolderMerge::Decision decision,
                     FolderMerge::DirectoryPolicy policy = FolderMerge::DirectoryPolicy::RejectManualConflicts,
                     QString *error = nullptr);
    bool resetDecision(const QString &relativePath, QString *error = nullptr);
    bool requestTextMerge(const QString &relativePath, QString *error = nullptr);

public slots:
    void cancelScan();

signals:
    void pathsChanged();
    void planChanged();
    void scanningChanged(bool scanning);
    void scanFinished(const LqCompare::FolderMerge::Plan &plan);
    void textMergeRequested(const QString &basePath, const QString &leftPath,
                            const QString &rightPath, const QString &outputPath);

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }

private:
    bool startScan(bool discardManual, QString *error);
    bool canEdit(QString *error) const;
    void publishPlan();
    FolderMerge::Paths m_paths;
    FolderMerge::Plan m_plan;
    FolderMerge::Scanner m_scanner;
    bool m_closing = false;
};

}
#endif
