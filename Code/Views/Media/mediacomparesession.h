#ifndef LQCOMPARE_MEDIACOMPARESESSION_H
#define LQCOMPARE_MEDIACOMPARESESSION_H

#include "comparesession.h"
#include "mediametadata.h"

namespace LqCompare {

// Read-only metadata comparison; no conclusion here describes audio identity.
class MediaCompareSession : public CompareSession
{
    Q_OBJECT
public:
    explicit MediaCompareSession(QObject *parent = nullptr);
    MediaCompareSession(const QString &left, const QString &right, QObject *parent = nullptr);

    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    const Media::Document &leftDocument() const { return m_left; }
    const Media::Document &rightDocument() const { return m_right; }
    const Media::Comparison &comparison() const { return m_comparison; }
    bool hasReadAttempt() const { return m_hasReadAttempt; }
    bool ignoreTechnical() const { return m_ignoreTechnical; }
    bool setIgnoreTechnical(bool ignore, QString *error = nullptr);

signals:
    void pathsChanged();
    void comparisonChanged();

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    bool doSave(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }

private:
    bool loadPair(const QString &left, const QString &right, QString *error);
    void updateComparison();
    void updateTitle();
    void updateStatus();

    QString m_leftPath;
    QString m_rightPath;
    Media::Document m_left;
    Media::Document m_right;
    Media::Comparison m_comparison;
    bool m_ignoreTechnical = false;
    bool m_hasReadAttempt = false;
    bool m_loading = false;
};

} // namespace LqCompare
#endif
