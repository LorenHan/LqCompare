#include "hexcomparesession.h"
#include "hexcompareview.h"
#include <QFileInfo>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &message) { if (error) *error = message; return false; }
}
HexCompareSession::HexCompareSession(QObject *parent) : HexCompareSession({}, {}, parent) {}
HexCompareSession::HexCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("hex"), parent), m_leftPath(left), m_rightPath(right)
{
    updateTitle();
    m_searchTimer.setInterval(0);
    connect(&m_searchTimer, &QTimer::timeout, this, &HexCompareSession::searchStep);
    connect(sessionSettings(), &SessionSettings::changed, this, [this](const QString &key) {
        if (key == QStringLiteral("hex.bytesPerRow"))
            setBytesPerRow(sessionSettings()->value(key, 16).toInt());
    });
}
void HexCompareSession::updateTitle()
{
    setTitle(m_leftPath.isEmpty() && m_rightPath.isEmpty() ? tr("Hex Compare") :
        tr("%1 ↔ %2 [Hex]").arg(QFileInfo(m_leftPath).fileName(), QFileInfo(m_rightPath).fileName()));
}
bool HexCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed || state() == State::Opening)
        return reject(error, tr("This session is closed or currently opening."));
    if (state() == State::Open) return loadPair(left, right, error);
    m_leftPath = left; m_rightPath = right;
    updateTitle();
    emit pathsChanged();
    if (error) error->clear();
    return true;
}
bool HexCompareSession::loadPair(const QString &left, const QString &right, QString *error)
{
    cancelSearch();
    if (!m_comparison.load(left, right, error)) return false;
    m_searchResult = {};
    m_searchResult.state = Hex::SearchState::NotFound;
    emit searchChanged();
    m_leftPath = m_comparison.path(true); m_rightPath = m_comparison.path(false);
    m_offset = 0;
    updateTitle();
    setDirty(false);
    emit pathsChanged();
    emit comparisonChanged();
    emit currentOffsetChanged(m_offset);
    updateStatus();
    return true;
}
bool HexCompareSession::doOpen(QString *error)
{
    setBytesPerRow(sessionSettings()->value(QStringLiteral("hex.bytesPerRow"), 16).toInt());
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) {
        if (error) error->clear();
        updateStatus();
        return true;
    }
    return loadPair(m_leftPath, m_rightPath, error);
}
bool HexCompareSession::doReload(QString *error) { return doOpen(error); }
void HexCompareSession::doClose()
{
    cancelSearch();
    m_searchResult = {};
    m_searchResult.state = Hex::SearchState::NotFound;
    m_comparison = Hex::Comparison();
    emit searchChanged();
}
QWidget *HexCompareSession::createView(QWidget *parent) { return new HexCompareView(this, parent); }
bool HexCompareSession::jumpToOffset(qint64 offset, QString *error)
{
    if (state() == State::Closed || !m_comparison.isLoaded() || offset < 0 || offset >= m_comparison.extent())
        return reject(error, tr("Offset is outside the compared files (0–%1).")
                      .arg(qMax<qint64>(0, m_comparison.extent() - 1)));
    m_offset = offset;
    emit currentOffsetChanged(offset);
    updateStatus();
    if (error) error->clear();
    return true;
}
bool HexCompareSession::jumpToOffset(const QString &text, QString *error)
{
    qint64 offset;
    return Hex::Comparison::parseOffset(text, &offset, error) && jumpToOffset(offset, error);
}
void HexCompareSession::setBytesPerRow(int bytes)
{
    if (bytes != 8 && bytes != 16 && bytes != 32 && bytes != 64) return;
    if (m_bytesPerRow == bytes) return;
    m_bytesPerRow = bytes;
    sessionSettings()->setValue(QStringLiteral("hex.bytesPerRow"), bytes);
    emit bytesPerRowChanged(bytes);
}
void HexCompareSession::locate(const Hex::Difference &difference)
{
    if (difference.isValid()) jumpToOffset(difference.offset);
}
void HexCompareSession::firstDifference() { locate(m_comparison.firstDifference()); }
void HexCompareSession::lastDifference() { locate(m_comparison.lastDifference()); }
void HexCompareSession::previousDifference() { locate(m_comparison.previousDifference(m_offset)); }
void HexCompareSession::nextDifference() { locate(m_comparison.nextDifference(m_offset)); }
void HexCompareSession::firstByte() { jumpToOffset(qint64(0)); }
void HexCompareSession::lastByte() { jumpToOffset(m_comparison.extent() - 1); }
void HexCompareSession::previousByte() { jumpToOffset(m_offset - 1); }
void HexCompareSession::nextByte() { jumpToOffset(m_offset + 1); }
bool HexCompareSession::startSearch(const Hex::SearchPattern &pattern, const Hex::SearchOptions &options, QString *error)
{
    if (state() != State::Open || !m_comparison.isLoaded())
        return reject(error, tr("Open two files before searching."));
    auto next = std::make_unique<Hex::SearchCursor>();
    if (!next->start(&m_comparison, pattern, options, error)) return false;
    cancelSearch();
    m_searchPattern = pattern;
    m_searchOptions = options;
    m_matchLeft = options.left;
    m_hasSearch = true;
    m_searchCursor = std::move(next);
    m_searchResult = m_searchCursor->result();
    m_searchTimer.start();
    setStatusText(tr("Searching %1 snapshot… Cancel is available.").arg(options.left ? tr("left") : tr("right")));
    emit searchChanged();
    return true;
}
bool HexCompareSession::findText(const QString &text, bool backward, bool caseSensitive)
{
    Hex::SearchPattern pattern;
    QString error;
    if (!Hex::textPattern(text, caseSensitive, false, &pattern, &error)) { reportError(error); return false; }
    Hex::SearchOptions options = m_searchOptions;
    options.backward = backward;
    options.startOffset = m_offset;
    if (!startSearch(pattern, options, &error)) { reportError(error); return false; }
    return true;
}
bool HexCompareSession::findHex(const QString &text, bool backward, QString *error)
{
    Hex::SearchPattern pattern;
    if (!Hex::parseHexPattern(text, &pattern, error)) return false;
    Hex::SearchOptions options = m_searchOptions;
    options.backward = backward;
    options.startOffset = m_offset;
    return startSearch(pattern, options, error);
}
void HexCompareSession::findText()
{
    if (state() == State::Closed) return;
    if (!widget()) createWidget();
    emit findRequested();
}
void HexCompareSession::repeatSearch(bool backward)
{
    if (!m_hasSearch) { findText(); return; }
    Hex::SearchOptions options = m_searchOptions;
    options.backward = backward;
    const qint64 size = m_comparison.size(options.left);
    options.startOffset = m_offset + (backward ? -1 : 1);
    // A repeat can start just beyond either end; the cursor wraps once if enabled.
    options.startOffset = qBound<qint64>(-1, options.startOffset, size);
    QString error;
    if (!startSearch(m_searchPattern, options, &error)) reportError(error);
}
void HexCompareSession::findNext() { repeatSearch(false); }
void HexCompareSession::findPrevious() { repeatSearch(true); }
void HexCompareSession::cancelSearch()
{
    if (!m_searchCursor) return;
    m_searchTimer.stop();
    m_searchCursor->cancel();
    m_searchResult = m_searchCursor->result();
    m_searchCursor.reset();
    setStatusText(m_searchResult.message);
    emit searchChanged();
}
void HexCompareSession::searchStep()
{
    if (!m_searchCursor) return;
    m_searchCursor->step(64 * 1024, 8);
    m_searchResult = m_searchCursor->result();
    if (m_searchResult.state != Hex::SearchState::Running) finishSearch();
    else emit searchChanged();
}
void HexCompareSession::finishSearch()
{
    m_searchTimer.stop();
    m_searchCursor.reset();
    if (m_searchResult.state == Hex::SearchState::Found) {
        jumpToOffset(m_searchResult.offset);
        setStatusText(tr("Found %1 bytes in %2 at 0x%3 (%4)%5 • Read-only")
            .arg(m_searchResult.length).arg(m_matchLeft ? tr("left") : tr("right"))
            .arg(QString::number(m_searchResult.offset, 16).toUpper()).arg(m_searchResult.offset)
            .arg(m_searchResult.wrapped ? tr(" • Wrapped") : QString()));
    } else setStatusText(m_searchResult.message);
    emit searchChanged();
}
bool HexCompareSession::isSearchMatch(bool left, qint64 offset) const
{
    return m_searchResult.state == Hex::SearchState::Found && left == m_matchLeft &&
        offset >= m_searchResult.offset && offset - m_searchResult.offset < m_searchResult.length;
}
void HexCompareSession::updateStatus()
{
    if (!m_comparison.isLoaded()) {
        setStatusText(tr("Choose two binary files • Read-only • Absolute offsets"));
        return;
    }
    setStatusText(tr("%1 different bytes / %2 bytes • %3 regions • Left %4 bytes / Right %5 bytes • Offset 0x%6 (%7) • Read-only, absolute offsets")
        .arg(m_comparison.differentBytes()).arg(m_comparison.extent()).arg(m_comparison.differenceRegions())
        .arg(m_comparison.size(true)).arg(m_comparison.size(false))
        .arg(QString::number(m_offset, 16).toUpper()).arg(m_offset));
}
}
