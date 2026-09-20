#ifndef LQCOMPARE_ARCHIVECOMPARESESSION_H
#define LQCOMPARE_ARCHIVECOMPARESESSION_H

#include "comparesession.h"
#include "archivecompare.h"

namespace LqCompare {

class ArchiveCompareView;

// Read-only ZIP directory comparison. Successful reads replace both sides together.
class ArchiveCompareSession : public CompareSession
{
    Q_OBJECT
public:
    explicit ArchiveCompareSession(QObject *parent = nullptr);
    ArchiveCompareSession(const QString &leftPath, const QString &rightPath,
                          QObject *parent = nullptr);

    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    const Archive::Comparison &comparison() const { return m_comparison; }
    bool hasComparison() const { return m_hasComparison; }
    ArchiveCompareView *view() const;

signals:
    void pathsChanged();
    void comparisonChanged();

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }

private:
    bool loadPair(const QString &left, const QString &right, QString *error);
    bool fail(const QString &message, QString *error);
    void updateTitle();
    void updateStatus(const QString &message);

    QString m_leftPath;
    QString m_rightPath;
    Archive::Comparison m_comparison;
    bool m_hasComparison = false;
};

} // namespace LqCompare

#endif
