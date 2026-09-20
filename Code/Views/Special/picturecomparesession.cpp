#include "picturecomparesession.h"
#include "picturecompareview.h"

#include <QFileInfo>
#include <QSignalBlocker>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &reason)
{
    if (error) *error = reason;
    return false;
}
}

PictureCompareSession::PictureCompareSession(QObject *parent)
    : PictureCompareSession({}, {}, parent) {}

PictureCompareSession::PictureCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("picture"), parent), m_leftPath(left), m_rightPath(right)
{
    updateTitle();
    connect(sessionSettings(), &SessionSettings::changed, this, [this](const QString &) {
        Picture::CompareOptions options;
        options.channelTolerance = qBound(0, sessionSettings()->value(QStringLiteral("picture.channelTolerance"), 0).toInt(), 255);
        options.alphaMode = static_cast<Picture::AlphaMode>(qBound(0,
            sessionSettings()->value(QStringLiteral("picture.alphaMode"), 0).toInt(), 2));
        options.connectivity = sessionSettings()->value(QStringLiteral("picture.connectivity"), 4).toInt() == 8
            ? Picture::Connectivity::Eight : Picture::Connectivity::Four;
        QString error;
        if (!setComparisonOptions(options, &error)) reportError(error);
    });
}

bool PictureCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    if (state() == State::Opening) return reject(error, tr("The image pair is already being opened."));
    if (state() == State::Open) return loadPair(left, right, error);
    m_leftPath = left;
    m_rightPath = right;
    updateTitle();
    emit pathsChanged();
    if (error) error->clear();
    return true;
}

bool PictureCompareSession::loadPair(const QString &left, const QString &right, QString *error)
{
    Picture::Document nextLeft, nextRight;
    Picture::Result nextResult;
    QString reason;
    reportProgress(0, 3, tr("Reading images"));
    if (!Picture::load(left, &nextLeft, &reason)) {
        reportProgress(0, 0);
        return reject(error, tr("Left image: %1").arg(reason));
    }
    reportProgress(1, 3, tr("Reading images"));
    if (!Picture::load(right, &nextRight, &reason)) {
        reportProgress(0, 0);
        return reject(error, tr("Right image: %1").arg(reason));
    }
    reportProgress(2, 3, tr("Comparing original pixels"));
    if (!Picture::compare(nextLeft.image, nextRight.image, &nextResult, &reason, m_options)) {
        reportProgress(0, 0);
        return reject(error, reason);
    }
    m_left = std::move(nextLeft);
    m_right = std::move(nextRight);
    m_result = std::move(nextResult);
    m_currentDifference = m_result.regions.isEmpty() ? -1 : 0;
    m_leftPath = m_left.path;
    m_rightPath = m_right.path;
    setDirty(false);
    updateTitle();
    updateStatus();
    reportProgress(0, 0);
    emit pathsChanged();
    emit comparisonChanged();
    emit currentDifferenceChanged(m_currentDifference);
    if (error) error->clear();
    return true;
}

bool PictureCompareSession::setComparisonOptions(const Picture::CompareOptions &options, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    if (options.channelTolerance < 0 || options.channelTolerance > 255
        || static_cast<int>(options.alphaMode) < 0 || static_cast<int>(options.alphaMode) > 2
        || (options.connectivity != Picture::Connectivity::Four && options.connectivity != Picture::Connectivity::Eight))
        return reject(error, tr("Invalid image comparison options."));
    Picture::Result next;
    if (!m_left.image.isNull() && !m_right.image.isNull()
        && !Picture::compare(m_left.image, m_right.image, &next, error, options)) return false;
    m_options = options;
    if (!next.differenceImage.isNull()) m_result = std::move(next);
    m_currentDifference = m_result.regions.isEmpty() ? -1 : 0;
    const QSignalBlocker blocker(sessionSettings());
    sessionSettings()->setValue(QStringLiteral("picture.channelTolerance"), options.channelTolerance);
    sessionSettings()->setValue(QStringLiteral("picture.alphaMode"), static_cast<int>(options.alphaMode));
    sessionSettings()->setValue(QStringLiteral("picture.connectivity"), options.connectivity == Picture::Connectivity::Eight ? 8 : 4);
    updateStatus();
    emit comparisonChanged();
    emit currentDifferenceChanged(m_currentDifference);
    if (error) error->clear();
    return true;
}

QWidget *PictureCompareSession::createView(QWidget *parent) { return new PictureCompareView(this, parent); }
bool PictureCompareSession::doOpen(QString *error)
{
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) {
        updateStatus();
        if (error) error->clear();
        return true;
    }
    return loadPair(m_leftPath, m_rightPath, error);
}
bool PictureCompareSession::doReload(QString *error) { return doOpen(error); }
bool PictureCompareSession::doSave(QString *error) { return reject(error, tr("Image comparison is read-only.")); }
void PictureCompareSession::doClose()
{
    m_left = {};
    m_right = {};
    m_result = {};
    m_currentDifference = -1;
    updateStatus();
    emit comparisonChanged();
    emit currentDifferenceChanged(-1);
}

void PictureCompareSession::selectDifference(int index)
{
    if (state() == State::Closed || index < 0 || index >= m_result.regions.size()) return;
    m_currentDifference = index;
    updateStatus();
    // Re-selecting a region also relocates it after the user has panned away.
    emit currentDifferenceChanged(index);
}
void PictureCompareSession::previousDifference()
{
    if (!m_result.regions.isEmpty()) selectDifference(qMax(0, m_currentDifference - 1));
}
void PictureCompareSession::nextDifference()
{
    if (!m_result.regions.isEmpty()) selectDifference(qMin(m_result.regions.size() - 1, m_currentDifference + 1));
}
void PictureCompareSession::firstDifference() { selectDifference(0); }
void PictureCompareSession::lastDifference() { selectDifference(m_result.regions.size() - 1); }

void PictureCompareSession::updateTitle()
{
    setTitle(tr("%1 ↔ %2").arg(m_leftPath.isEmpty() ? tr("Choose image") : QFileInfo(m_leftPath).fileName(),
                               m_rightPath.isEmpty() ? tr("Choose image") : QFileInfo(m_rightPath).fileName()));
}

void PictureCompareSession::updateStatus()
{
    if (m_left.image.isNull()) { setStatusText(tr("Read-only image comparison • Choose two images")); return; }
    const QString alpha = m_options.alphaMode == Picture::AlphaMode::Include ? tr("RGBA (including hidden RGB)")
        : m_options.alphaMode == Picture::AlphaMode::Ignore ? tr("RGB; alpha ignored") : tr("Alpha only");
    QString status = tr("Read-only • %1 / %2 pixels differ (%3%) • Only left: %4; only right: %5 • Channel tolerance: %6 • %7")
                  .arg(m_result.differentPixels).arg(m_result.totalPixels)
                  .arg(m_result.differencePercent(), 0, 'f', 2)
                  .arg(m_result.leftOnlyPixels).arg(m_result.rightOnlyPixels)
                  .arg(m_options.channelTolerance).arg(alpha);
    status += tr(" • %1 regions (%2-neighbor); selected %3 / %4")
        .arg(m_result.regionCount).arg(m_options.connectivity == Picture::Connectivity::Four ? 4 : 8)
        .arg(m_currentDifference + 1).arg(m_result.regions.size());
    if (m_result.regionsTruncated)
        status += tr(" • Navigation limited to the first %1 regions; total region/pixel counts are exact.").arg(m_result.regions.size());
    setStatusText(status);
}

}
