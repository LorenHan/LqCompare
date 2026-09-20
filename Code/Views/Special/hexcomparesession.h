#ifndef LQCOMPARE_HEXCOMPARESESSION_H
#define LQCOMPARE_HEXCOMPARESESSION_H
#include "comparesession.h"
#include "hexdiff.h"
#include "hexsearch.h"
#include <QTimer>

namespace LqCompare {
class HexCompareSession : public CompareSession {
    Q_OBJECT
public:
    explicit HexCompareSession(QObject *parent = nullptr);
    HexCompareSession(const QString &left, const QString &right, QObject *parent = nullptr);
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    const Hex::Comparison &comparison() const { return m_comparison; }
    qint64 currentOffset() const { return m_offset; }
    bool jumpToOffset(qint64 offset, QString *error = nullptr);
    bool jumpToOffset(const QString &text, QString *error = nullptr);
    int bytesPerRow() const { return m_bytesPerRow; }
    void setBytesPerRow(int bytes);
    // Search returns immediately; completion/cancellation is reported by searchChanged.
    bool startSearch(const Hex::SearchPattern &pattern, const Hex::SearchOptions &options, QString *error = nullptr);
    bool findText(const QString &text, bool backward = false, bool caseSensitive = false);
    bool findHex(const QString &text, bool backward = false, QString *error = nullptr);
    bool isSearching() const { return m_searchCursor && m_searchResult.state == Hex::SearchState::Running; }
    const Hex::SearchResult &searchResult() const { return m_searchResult; }
    bool searchLeftSide() const { return m_searchOptions.left; }
    void setSearchLeftSide(bool left) { m_searchOptions.left = left; }
    bool isSearchMatch(bool left, qint64 offset) const;
public slots:
    void findText();
    void findNext();
    void findPrevious();
    void cancelSearch();
    void firstDifference();
    void previousDifference();
    void nextDifference();
    void lastDifference();
    void firstByte();
    void lastByte();
    void previousByte();
    void nextByte();
signals:
    void pathsChanged();
    void comparisonChanged();
    void currentOffsetChanged(qint64 offset);
    void bytesPerRowChanged(int bytes);
    void findRequested();
    void searchChanged();
protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }
private:
    bool loadPair(const QString &left, const QString &right, QString *error);
    void locate(const Hex::Difference &difference);
    void updateStatus();
    void updateTitle();
    void searchStep();
    void repeatSearch(bool backward);
    void finishSearch();
    Hex::Comparison m_comparison;
    QString m_leftPath, m_rightPath;
    qint64 m_offset = 0;
    int m_bytesPerRow = 16;
    QTimer m_searchTimer;
    std::unique_ptr<Hex::SearchCursor> m_searchCursor;
    Hex::SearchPattern m_searchPattern;
    Hex::SearchOptions m_searchOptions;
    Hex::SearchResult m_searchResult;
    bool m_hasSearch = false;
    bool m_matchLeft = true;
};
}
#endif
