#include "mediacomparesession.h"
#include "mediacompareview.h"

#include <QFileInfo>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <utility>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &reason)
{
    if (error) *error = reason;
    return false;
}
}

MediaCompareSession::MediaCompareSession(QObject *parent)
    : MediaCompareSession({}, {}, parent) {}

MediaCompareSession::MediaCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("media"), parent), m_leftPath(left), m_rightPath(right)
{
    updateTitle();
    updateStatus();
    connect(sessionSettings(), &SessionSettings::changed, this, [this](const QString &key) {
        if (!key.isEmpty() && key != QStringLiteral("media.ignoreTechnical")) return;
        QString error;
        if (!setIgnoreTechnical(sessionSettings()->value(QStringLiteral("media.ignoreTechnical"), false).toBool(), &error)) reportError(error);
    });
}

bool MediaCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    if (state() == State::Opening || m_loading) return reject(error, tr("Media metadata is already being read."));
    if (state() == State::Open) return loadPair(left, right, error);
    m_leftPath = left;
    m_rightPath = right;
    m_left = {};
    m_right = {};
    m_comparison = {};
    m_hasReadAttempt = false;
    updateTitle();
    updateStatus();
    emit pathsChanged();
    emit comparisonChanged();
    if (error) error->clear();
    return true;
}

bool MediaCompareSession::loadPair(const QString &left, const QString &right, QString *error)
{
    if (m_loading) return reject(error, tr("Media metadata is already being read."));
    const QScopedValueRollback<bool> loading(m_loading, true);
    Media::Document nextLeft, nextRight;
    QString leftError, rightError;
    reportProgress(0, 3, tr("Reading left media metadata"));
    const bool leftOk = Media::load(left, &nextLeft, &leftError);
    reportProgress(1, 3, tr("Reading right media metadata"));
    // A failed left read must not prevent the right status and metadata being refreshed.
    const bool rightOk = Media::load(right, &nextRight, &rightError);
    reportProgress(2, 3, tr("Comparing available metadata"));
    m_leftPath = left;
    m_rightPath = right;
    m_left = std::move(nextLeft);
    m_right = std::move(nextRight);
    m_hasReadAttempt = true;
    updateComparison();
    setDirty(false);
    updateTitle();
    updateStatus();
    reportProgress(0, 0);
    emit pathsChanged();
    emit comparisonChanged();
    if (!leftOk || !rightOk) {
        QStringList reasons;
        if (!leftOk) reasons.append(tr("Left: %1").arg(leftError.isEmpty() ? m_left.message : leftError));
        if (!rightOk) reasons.append(tr("Right: %1").arg(rightError.isEmpty() ? m_right.message : rightError));
        return reject(error, reasons.join(QLatin1Char('\n')));
    }
    if (error) error->clear();
    return true;
}

bool MediaCompareSession::setIgnoreTechnical(bool ignore, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    m_ignoreTechnical = ignore;
    const QSignalBlocker blocker(sessionSettings());
    sessionSettings()->setValue(QStringLiteral("media.ignoreTechnical"), ignore);
    updateComparison();
    updateStatus();
    emit comparisonChanged();
    if (error) error->clear();
    return true;
}

void MediaCompareSession::updateComparison()
{
    m_comparison = m_hasReadAttempt ? Media::compare(m_left, m_right, m_ignoreTechnical) : Media::Comparison();
    // Incomplete readers can compare the fields they obtained, never assert complete equality.
    m_comparison.complete = m_comparison.complete && m_left.complete() && m_right.complete();
}

QWidget *MediaCompareSession::createView(QWidget *parent) { return new MediaCompareView(this, parent); }
bool MediaCompareSession::doOpen(QString *error)
{
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) {
        m_left = {};
        m_right = {};
        m_comparison = {};
        m_hasReadAttempt = false;
        updateStatus();
        emit comparisonChanged();
        if (error) error->clear();
        return true;
    }
    return loadPair(m_leftPath, m_rightPath, error);
}
bool MediaCompareSession::doReload(QString *error) { return doOpen(error); }
bool MediaCompareSession::doSave(QString *error) { return reject(error, tr("Media metadata comparison is read-only.")); }
void MediaCompareSession::doClose()
{
    m_left = {};
    m_right = {};
    m_comparison = {};
    m_hasReadAttempt = false;
    setStatusText(tr("Media comparison closed."));
    emit comparisonChanged();
}

void MediaCompareSession::updateTitle()
{
    setTitle(tr("%1 ↔ %2").arg(m_leftPath.isEmpty() ? tr("Choose media") : QFileInfo(m_leftPath).fileName(),
                               m_rightPath.isEmpty() ? tr("Choose media") : QFileInfo(m_rightPath).fileName()));
}

void MediaCompareSession::updateStatus()
{
    if (!m_hasReadAttempt) {
        setStatusText(tr("Read-only metadata comparison • Choose two files and compare"));
        return;
    }
    const QString scope = m_ignoreTechnical ? tr("Tags only; technical parameters ignored") : tr("Tags and available technical parameters");
    if (!m_left.usable() || !m_right.usable()) {
        setStatusText(tr("Comparison unavailable • Left: %1; right: %2 • %3")
                      .arg(Media::statusName(m_left.status), Media::statusName(m_right.status), scope));
    } else if (!m_comparison.complete) {
        setStatusText(tr("Incomplete metadata comparison • %1 differing available fields • Complete tag equality is unknown • %2")
                      .arg(m_comparison.differenceCount).arg(scope));
    } else if (m_comparison.differenceCount == 0) {
        setStatusText(tr("Compared metadata fields match • %1 • Audio content has not been compared")
                      .arg(scope));
    } else {
        setStatusText(tr("%1 metadata fields differ • %2 • Audio content has not been compared")
                      .arg(m_comparison.differenceCount).arg(scope));
    }
}

} // namespace LqCompare
