#ifndef LQCOMPARE_TABLECOMPARESESSION_H
#define LQCOMPARE_TABLECOMPARESESSION_H

#include "comparesession.h"
#include "tablecompare.h"

namespace LqCompare {

class TableCompareSession : public CompareSession {
    Q_OBJECT
public:
    explicit TableCompareSession(QObject *parent = nullptr);
    TableCompareSession(const QString &left, const QString &right, QObject *parent = nullptr);
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    const Table::Document &leftDocument() const { return m_left; }
    const Table::Document &rightDocument() const { return m_right; }
    const Table::Result &comparison() const { return m_result; }
    Table::CompareOptions comparisonOptions() const { return m_options; }
    Table::ParseOptions parseOptions(bool left) const { return left ? m_leftParse : m_rightParse; }
    int currentDifference() const { return m_currentDifference; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    bool setComparisonOptions(const Table::CompareOptions &options, QString *error = nullptr);
    bool setParseOptions(const Table::ParseOptions &left, const Table::ParseOptions &right,
                         QString *error = nullptr);
    void selectDifference(int index);

public slots:
    void previousDifference();
    void nextDifference();
    void firstDifference();
    void lastDifference();

signals:
    void comparisonChanged();
    void pathsChanged();
    void currentDifferenceChanged(int index);

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    bool canSaveNow() const override { return false; }

private:
    bool loadPair(const QString &left, const QString &right,
                  const Table::ParseOptions &leftParse, const Table::ParseOptions &rightParse,
                  QString *error);
    void readSettings();
    void writeSettings();
    void publishResult();
    void updateTitle();
    QString m_leftPath, m_rightPath;
    Table::Document m_left, m_right;
    Table::ParseOptions m_leftParse, m_rightParse;
    Table::CompareOptions m_options;
    Table::Result m_result;
    int m_currentDifference = -1;
};

}
#endif
