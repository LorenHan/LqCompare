#ifndef LQCOMPARE_VERSIONCOMPARESESSION_H
#define LQCOMPARE_VERSIONCOMPARESESSION_H
#include "comparesession.h"
#include "versioncompare.h"
namespace LqCompare {
class VersionCompareSession : public CompareSession {
    Q_OBJECT
public:
    explicit VersionCompareSession(QObject *parent = nullptr);
    VersionCompareSession(const QString &left, const QString &right, QObject *parent = nullptr);
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    // Schedules a bounded worker when open. Completion/failure is reported via signals.
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    bool isBusy() const { return m_busy; }
    bool isLoaded() const { return m_loaded; }
    const Version::FileInfo &leftInfo() const { return m_left; }
    const Version::FileInfo &rightInfo() const { return m_right; }
    const QVector<Version::Row> &rows() const { return m_rows; }
    Version::CompareOptions options() const { return m_options; }
    void setOptions(const Version::CompareOptions &options);
    bool exportCsv(const QString &path, QString *error = nullptr) const;
signals:
    void pathsChanged();
    void comparisonChanged();
    void busyChanged(bool busy);
    void loadFinished(bool success, const QString &error);
protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }
private:
    bool startLoad(const QString &left, const QString &right, QString *error);
    void updateTitle();
    void updateComparison();
    QString m_leftPath, m_rightPath;
    Version::FileInfo m_left, m_right;
    QVector<Version::Row> m_rows;
    Version::CompareOptions m_options;
    quint64 m_generation = 0;
    bool m_busy = false, m_loaded = false;
};
}
#endif
