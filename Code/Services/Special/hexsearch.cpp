#include "hexsearch.h"
#include "hexdiff.h"

#include <QElapsedTimer>
#include <QVector>
#include <algorithm>

namespace LqCompare { namespace Hex {
namespace {
constexpr int MaximumPatternBytes = 4096;
constexpr int ReadChunkBytes = 64 * 1024;

bool reject(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

int hexValue(QChar ch)
{
    const ushort value = ch.unicode();
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

uchar foldAscii(uchar value)
{
    return value >= 'A' && value <= 'Z' ? uchar(value + ('a' - 'A')) : value;
}

bool isWordByte(uchar value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '_';
}
}

bool parseHexPattern(const QString &text, SearchPattern *pattern, QString *error)
{
    if (!pattern) return reject(error, QStringLiteral("A destination search pattern is required."));
    QByteArray bytes;
    bytes.reserve(qMin(MaximumPatternBytes, text.size() / 2));
    int high = -1;
    for (QChar ch : text) {
        if (ch.isSpace()) {
            if (high >= 0)
                return reject(error, QStringLiteral("Each hexadecimal byte needs two adjacent digits (for example, 00 FF or 00FF)."));
            continue;
        }
        const int digit = hexValue(ch);
        if (digit < 0)
            return reject(error, QStringLiteral("Use hexadecimal byte pairs only. Prefixes, wildcards and other tokens are not supported."));
        if (high < 0) high = digit;
        else {
            if (bytes.size() == MaximumPatternBytes)
                return reject(error, QStringLiteral("Search patterns are limited to 4096 bytes."));
            bytes.append(char((high << 4) | digit));
            high = -1;
        }
    }
    if (high >= 0)
        return reject(error, QStringLiteral("Each hexadecimal byte needs two digits; the final byte is incomplete."));
    if (bytes.isEmpty()) return reject(error, QStringLiteral("Enter at least one hexadecimal byte."));
    *pattern = {bytes, true, false};
    if (error) error->clear();
    return true;
}

bool textPattern(const QString &text, bool caseSensitive, bool wholeWords,
                 SearchPattern *pattern, QString *error)
{
    if (!pattern) return reject(error, QStringLiteral("A destination search pattern is required."));
    if (text.isEmpty()) return reject(error, QStringLiteral("Enter non-empty text to search for."));
    // Every UTF-16 code unit requires at least one UTF-8 byte, so reject clearly
    // oversized input before allocating its encoded copy.
    if (text.size() > MaximumPatternBytes)
        return reject(error, QStringLiteral("UTF-8 search patterns are limited to 4096 bytes."));
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() > MaximumPatternBytes)
        return reject(error, QStringLiteral("UTF-8 search patterns are limited to 4096 bytes."));
    *pattern = {bytes, caseSensitive, wholeWords};
    if (error) error->clear();
    return true;
}

struct SearchCursor::Data {
    const Comparison *comparison = nullptr;
    SearchOptions options;
    SearchResult result;
    QElapsedTimer elapsed;
    QByteArray needle;
    QVector<int> prefix;
    bool wholeWords = false;
    bool caseSensitive = true;
    qint64 fileSize = 0;
    // Each phase holds candidate start offsets, inclusive. The second phase is
    // the unvisited portion after wrapping; the KMP state is reset between them.
    qint64 phaseLow[2] = {0, 0};
    qint64 phaseHigh[2] = {-1, -1};
    int phase = 0;
    qint64 nextByte = 0;
    qint64 lastByte = -1;
    int matched = 0;
    QByteArray page;
    qint64 pageStart = 0;
    qint64 feedLow = 0;
    qint64 feedHigh = -1;

    SearchState finish(SearchState state, const QString &message)
    {
        result.state = state;
        result.message = message;
        return state;
    }

    void beginPhase(int index)
    {
        phase = index;
        matched = 0;
        page.clear();
        feedLow = 0;
        feedHigh = -1;
        if (options.backward) {
            nextByte = phaseHigh[index] + needle.size() - 1;
            lastByte = phaseLow[index];
        } else {
            nextByte = phaseLow[index];
            lastByte = phaseHigh[index] + needle.size() - 1;
        }
        if (index == 1) result.wrapped = true;
    }

    bool phaseFinished() const
    {
        return phaseHigh[phase] < phaseLow[phase] ||
            (options.backward ? nextByte < lastByte : nextByte > lastByte);
    }

    bool fillPage()
    {
        // Retain this page across step() calls, including one-byte slices. At
        // most 64 KiB + 4097 bytes are read, with enough surrounding context for
        // both whole-word boundaries without a read per potential match.
        qint64 end;
        if (options.backward) {
            feedHigh = nextByte;
            feedLow = qMax(lastByte, nextByte - ReadChunkBytes + 1);
            pageStart = qMax<qint64>(0, feedLow - 1);
            end = qMin(fileSize, feedHigh + needle.size() + 1);
        } else {
            feedLow = nextByte;
            feedHigh = qMin(lastByte, nextByte + ReadChunkBytes - 1);
            pageStart = qMax<qint64>(0, feedLow - needle.size());
            end = qMin(fileSize, feedHigh + 2);
        }
        QString error;
        page = comparison->read(options.left, pageStart, int(end - pageStart), &error);
        if (!error.isEmpty() || page.size() != end - pageStart) {
            finish(SearchState::Error, error.isEmpty()
                ? QStringLiteral("The binary snapshot could not supply a complete search page.") : error);
            return false;
        }
        return true;
    }

    bool wordBoundaries(qint64 candidate) const
    {
        const qint64 before = candidate - 1;
        const qint64 after = candidate + needle.size();
        // fillPage supplies these bytes even when a match crosses page/step
        // boundaries. BOF and EOF themselves are valid word boundaries.
        return (before < 0 || !isWordByte(uchar(page.at(int(before - pageStart))))) &&
            (after == fileSize || !isWordByte(uchar(page.at(int(after - pageStart)))));
    }
};

SearchCursor::SearchCursor() : m_data(std::make_unique<Data>()) {}
SearchCursor::~SearchCursor() = default;
SearchCursor::SearchCursor(SearchCursor &&) noexcept = default;
SearchCursor &SearchCursor::operator=(SearchCursor &&) noexcept = default;

bool SearchCursor::start(const Comparison *comparison, const SearchPattern &pattern,
                         const SearchOptions &options, QString *error)
{
    m_data = std::make_unique<Data>();
    Data &data = *m_data;
    const auto invalid = [&data, error](const QString &message) {
        data.finish(SearchState::Error, message);
        return reject(error, message);
    };
    if (!comparison || !comparison->isLoaded())
        return invalid(QStringLiteral("Load a binary comparison before searching."));
    if (pattern.bytes.isEmpty() || pattern.bytes.size() > MaximumPatternBytes)
        return invalid(QStringLiteral("Search patterns must contain between 1 and 4096 bytes."));
    if (options.maximumScanBytes <= 0 || options.maximumElapsedMs <= 0)
        return invalid(QStringLiteral("Search byte and time limits must both be positive."));
    data.comparison = comparison;
    data.options = options;
    data.fileSize = comparison->size(options.left);
    // 合法偏移是「被搜索文件的字节位置」加上两个哨兵：-1（文件之前）与
    // fileSize（文件之后）。调用方正是拿这两个哨兵表示「该方向的首段已无
    // 候选」（见 HexCompareSession::repeatSearch 里那次 qBound），所以必须
    // 继续接受它们。再往外的偏移没有任何合法来源：静默夹紧会把调用方的算术
    // 下溢（如 qint64::min）变成一次「从文件头开始」的搜索并报告命中——调用
    // 方从未要求过这个区间，却拿到一个看起来成功的结果。因此与 pattern、
    // 限额一样明确报错，而不是替调用方猜一个区间。
    if (options.startOffset < -1 || options.startOffset > data.fileSize)
        return invalid(QStringLiteral("Search offsets must lie between -1 and the size of the searched file."));
    data.caseSensitive = pattern.caseSensitive;
    data.wholeWords = pattern.wholeWords;
    data.needle = pattern.bytes;
    if (!data.caseSensitive) {
        for (int i = 0; i < data.needle.size(); ++i)
            data.needle[i] = char(foldAscii(uchar(data.needle.at(i))));
    }
    if (options.backward) std::reverse(data.needle.begin(), data.needle.end());
    data.prefix.resize(data.needle.size());
    for (int i = 1, matched = 0; i < data.needle.size(); ++i) {
        while (matched > 0 && data.needle.at(i) != data.needle.at(matched))
            matched = data.prefix.at(matched - 1);
        if (data.needle.at(i) == data.needle.at(matched)) ++matched;
        data.prefix[i] = matched;
    }
    data.elapsed.start();
    if (error) error->clear();
    if (data.fileSize < data.needle.size()) {
        data.finish(SearchState::NotFound, QStringLiteral("No matching bytes were found."));
        return true;
    }
    const qint64 lastCandidate = data.fileSize - data.needle.size();
    if (options.backward) {
        const qint64 split = qBound<qint64>(-1, options.startOffset, lastCandidate);
        data.phaseLow[0] = 0;
        data.phaseHigh[0] = split;
        data.phaseLow[1] = split + 1;
        data.phaseHigh[1] = options.wrap ? lastCandidate : -1;
    } else {
        const qint64 split = qBound<qint64>(0, options.startOffset, lastCandidate + 1);
        data.phaseLow[0] = split;
        data.phaseHigh[0] = lastCandidate;
        data.phaseLow[1] = 0;
        data.phaseHigh[1] = options.wrap ? split - 1 : -1;
    }
    data.beginPhase(0);
    return true;
}

SearchState SearchCursor::step(int maximumBytes, int maximumMilliseconds)
{
    if (!m_data) m_data = std::make_unique<Data>();
    Data &data = *m_data;
    if (data.result.state != SearchState::Running) return data.result.state;
    if (!data.comparison)
        return data.finish(SearchState::Error, QStringLiteral("Start a binary search before advancing it."));
    QElapsedTimer slice;
    slice.start();
    int consumed = 0;
    for (;;) {
        if (data.phaseFinished()) {
            if (data.phase == 0 && data.phaseHigh[1] >= data.phaseLow[1]) data.beginPhase(1);
            else return data.finish(SearchState::NotFound, QStringLiteral("No matching bytes were found."));
        }
        if (data.elapsed.elapsed() >= data.options.maximumElapsedMs)
            return data.finish(SearchState::LimitReached, QStringLiteral("The search time limit was reached; unsearched bytes may still contain a match."));
        if (data.result.scannedBytes >= data.options.maximumScanBytes)
            return data.finish(SearchState::LimitReached, QStringLiteral("The search byte limit was reached; unsearched bytes may still contain a match."));
        if (consumed >= maximumBytes || slice.elapsed() >= maximumMilliseconds)
            return SearchState::Running;
        if (data.nextByte < data.feedLow || data.nextByte > data.feedHigh) {
            if (!data.fillPage()) return data.result.state;
            // Disk I/O is bounded to one small page, but can itself consume the
            // slice. Keep the page ready for the next invocation in that case.
            if (data.elapsed.elapsed() >= data.options.maximumElapsedMs)
                return data.finish(SearchState::LimitReached, QStringLiteral("The search time limit was reached; unsearched bytes may still contain a match."));
            if (slice.elapsed() >= maximumMilliseconds) return SearchState::Running;
        }
        const qint64 position = data.nextByte;
        uchar value = uchar(data.page.at(int(position - data.pageStart)));
        if (!data.caseSensitive) value = foldAscii(value);
        while (data.matched > 0 && value != uchar(data.needle.at(data.matched)))
            data.matched = data.prefix.at(data.matched - 1);
        if (value == uchar(data.needle.at(data.matched))) ++data.matched;
        data.nextByte += data.options.backward ? -1 : 1;
        ++data.result.scannedBytes;
        ++consumed;
        if (data.matched == data.needle.size()) {
            const qint64 candidate = data.options.backward ? position : position - data.needle.size() + 1;
            data.matched = data.prefix.at(data.matched - 1);
            if (!data.wholeWords || data.wordBoundaries(candidate)) {
                data.result.offset = candidate;
                data.result.length = data.needle.size();
                return data.finish(SearchState::Found, QStringLiteral("Matching bytes found."));
            }
        }
    }
}

void SearchCursor::cancel()
{
    if (m_data && m_data->result.state == SearchState::Running)
        m_data->finish(SearchState::Cancelled, QStringLiteral("Search cancelled."));
}

const SearchResult &SearchCursor::result() const
{
    static const SearchResult unavailable{SearchState::Error, -1, 0, 0, false,
        QStringLiteral("This search cursor has been moved from.")};
    return m_data ? m_data->result : unavailable;
}

} }
