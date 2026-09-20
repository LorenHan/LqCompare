#ifndef LQCOMPARE_PICTURECOMPARESESSION_H
#define LQCOMPARE_PICTURECOMPARESESSION_H

#include "comparesession.h"
#include "picturediff.h"

namespace LqCompare {

class PictureCompareSession : public CompareSession {
    Q_OBJECT
public:
    explicit PictureCompareSession(QObject *parent = nullptr);
    PictureCompareSession(const QString &left, const QString &right, QObject *parent = nullptr);
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    const Picture::Document &leftDocument() const { return m_left; }
    const Picture::Document &rightDocument() const { return m_right; }
    const Picture::Result &comparison() const { return m_result; }
    Picture::CompareOptions comparisonOptions() const { return m_options; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    bool setComparisonOptions(const Picture::CompareOptions &options, QString *error = nullptr);
    int currentDifference() const { return m_currentDifference; }
    void selectDifference(int index);

public slots:
    void previousDifference();
    void nextDifference();
    void firstDifference();
    void lastDifference();

signals:
    void pathsChanged();
    void comparisonChanged();
    void currentDifferenceChanged(int index);

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    bool doSave(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }

private:
    bool loadPair(const QString &left, const QString &right, QString *error);
    void updateTitle();
    void updateStatus();
    QString m_leftPath;
    QString m_rightPath;
    Picture::Document m_left;
    Picture::Document m_right;
    Picture::Result m_result;
    Picture::CompareOptions m_options;
    int m_currentDifference = -1;
};

}
#endif
