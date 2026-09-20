#ifndef LQCOMPARE_FOLDERCOMPARESESSION_H
#define LQCOMPARE_FOLDERCOMPARESESSION_H

#include "comparesession.h"
#include "foldercompare.h"

namespace LqCompare {

class FolderCompareView;

class FolderCompareSession : public CompareSession
{
    Q_OBJECT
public:
    explicit FolderCompareSession(QObject *parent = nullptr);
    FolderCompareSession(const QString &leftPath, const QString &rightPath, QObject *parent = nullptr);
    ~FolderCompareSession() override;

    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    void setPaths(const QString &leftPath, const QString &rightPath);
    bool isScanning() const { return m_scanner.isRunning(); }
    const Folder::Result &result() const { return m_result; }
    Folder::Options comparisonOptions() const { return m_options; }
    bool setComparisonOptions(const Folder::Options &options, QString *error = nullptr);
    FolderCompareView *view() const;

public slots:
    void cancelScan();
    void previousDifference();
    void nextDifference();
    void firstDifference();
    void lastDifference();
    void selectAllDifferences();

signals:
    void pathsChanged(const QString &leftPath, const QString &rightPath);
    void compareFilesRequested(const QString &leftPath, const QString &rightPath);
    void scanFinished(const LqCompare::Folder::Result &result);

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }

private:
    bool startScan(QString *error);
    void updateStatus(const QString &status);
    void readComparisonSettings();

    QString m_leftPath;
    QString m_rightPath;
    Folder::Scanner m_scanner;
    Folder::Result m_result;
    Folder::Options m_options;
    QString m_settingsError;
    bool m_writingSettings = false;
    bool m_pendingScan = false;
    bool m_closing = false;
    bool m_hasResult = false;
};

} // namespace LqCompare

#endif
