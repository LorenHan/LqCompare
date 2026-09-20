#ifndef LQCOMPARE_HEXSEARCH_H
#define LQCOMPARE_HEXSEARCH_H

#include <QByteArray>
#include <QString>
#include <memory>

namespace LqCompare { namespace Hex {

class Comparison;

struct SearchPattern {
    QByteArray bytes;
    bool caseSensitive = true;
    bool wholeWords = false;
};

// Patterns contain 1..4096 bytes. Hex input accepts contiguous hex pairs or
// whitespace-separated groups of complete pairs; prefixes/wildcards are rejected.
bool parseHexPattern(const QString &text, SearchPattern *pattern, QString *error = nullptr);
// Text is encoded as UTF-8. Case-insensitive matching folds ASCII A-Z only;
// whole-word boundaries are ASCII letters, digits and underscore.
bool textPattern(const QString &text, bool caseSensitive, bool wholeWords,
                 SearchPattern *pattern, QString *error = nullptr);

struct SearchOptions {
    bool left = true;
    qint64 startOffset = 0;
    bool backward = false;
    bool wrap = true;
    qint64 maximumScanBytes = 64LL * 1024 * 1024;
    int maximumElapsedMs = 5000;
};

enum class SearchState { Running, Found, NotFound, Cancelled, LimitReached, Error };

struct SearchResult {
    SearchState state = SearchState::Running;
    qint64 offset = -1;
    int length = 0;
    qint64 scannedBytes = 0;
    bool wrapped = false;
    QString message;
};

// Cooperative, bounded-memory streaming KMP. startOffset is an inclusive
// candidate start and must lie in [-1, fileSize]; the two out-of-file sentinels
// mean "the initial segment in this direction is exhausted". Anything further
// out is rejected by start() rather than clamped to a different search.
// A wrap scans the remaining candidates exactly once and never joins EOF to BOF.
// Comparison must remain alive and unchanged until this cursor is discarded.
// Cancel/discard the cursor before reloading or closing its comparison.
class SearchCursor {
public:
    SearchCursor();
    ~SearchCursor();
    SearchCursor(SearchCursor &&) noexcept;
    SearchCursor &operator=(SearchCursor &&) noexcept;
    SearchCursor(const SearchCursor &) = delete;
    SearchCursor &operator=(const SearchCursor &) = delete;

    bool start(const Comparison *comparison, const SearchPattern &pattern,
               const SearchOptions &options, QString *error = nullptr);
    SearchState step(int maximumBytes = 64 * 1024, int maximumMilliseconds = 8);
    void cancel();
    const SearchResult &result() const;

private:
    struct Data;
    std::unique_ptr<Data> m_data;
};

} }
#endif
