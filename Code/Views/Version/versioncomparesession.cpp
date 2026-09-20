#include "versioncomparesession.h"
#include "versioncompareview.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
namespace LqCompare {
namespace {
bool reject(QString *error, const QString &reason) { if (error) *error = reason; return false; }
struct PairResult { Version::FileInfo left, right; QString error; };
}
VersionCompareSession::VersionCompareSession(QObject *parent) : VersionCompareSession({}, {}, parent) {}
VersionCompareSession::VersionCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("version"), parent), m_leftPath(left), m_rightPath(right)
{
    updateTitle();
    setStatusText(tr("Choose two files • Version comparison is read-only"));
}
void VersionCompareSession::updateTitle()
{
    setTitle(m_leftPath.isEmpty() && m_rightPath.isEmpty() ? tr("Version Compare") :
             tr("%1 ↔ %2 [Version]").arg(QFileInfo(m_leftPath).fileName(), QFileInfo(m_rightPath).fileName()));
}
bool VersionCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed || state() == State::Opening || m_busy)
        return reject(error, tr("The session is closed or a comparison is still running."));
    if (state() == State::Open) return startLoad(left, right, error);
    m_leftPath = left; m_rightPath = right; updateTitle(); emit pathsChanged();
    if (error) error->clear();
    return true;
}
bool VersionCompareSession::startLoad(const QString &left, const QString &right, QString *error)
{
    if (m_busy) return reject(error, tr("A version comparison is still running."));
    if (left.isEmpty() || right.isEmpty()) return reject(error, tr("Choose both left and right files."));
    const QString leftPath = QFileInfo(left).absoluteFilePath(), rightPath = QFileInfo(right).absoluteFilePath();
    m_busy = true; emit busyChanged(true);
    setStatusText(tr("Reading version information… • Up to 64 MiB per file"));
    reportProgress(0, 2, tr("Reading files"));
    const quint64 generation = ++m_generation;
    auto *watcher = new QFutureWatcher<PairResult>(this);
    connect(watcher, &QFutureWatcher<PairResult>::finished, this, [this, watcher, generation, leftPath, rightPath] {
        const PairResult result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation || state() == State::Closed) return;
        m_busy = false; emit busyChanged(false); reportProgress(0, 0);
        if (!result.error.isEmpty()) {
            if (m_loaded) emit pathsChanged(); // Displayed paths must match retained rows.
            setStatusText(m_loaded ? tr("Loading failed; previous comparison retained") : tr("Version information could not be loaded"));
            reportError(result.error);
            emit loadFinished(false, result.error);
            return;
        }
        m_left = result.left; m_right = result.right;
        m_leftPath = leftPath; m_rightPath = rightPath; m_loaded = true;
        updateTitle(); setDirty(false); emit pathsChanged(); updateComparison(); emit loadFinished(true, {});
    });
    watcher->setFuture(QtConcurrent::run([leftPath, rightPath] {
        PairResult result;
        result.left = Version::inspectFile(leftPath);
        if (!result.left.usable()) { result.error = QObject::tr("Left file: %1").arg(result.left.message); return result; }
        result.right = Version::inspectFile(rightPath);
        if (!result.right.usable()) result.error = QObject::tr("Right file: %1").arg(result.right.message);
        return result;
    }));
    if (error) error->clear();
    return true;
}
bool VersionCompareSession::doOpen(QString *error)
{
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) { if (error) error->clear(); return true; }
    return startLoad(m_leftPath, m_rightPath, error);
}
bool VersionCompareSession::doReload(QString *error) { return doOpen(error); }
void VersionCompareSession::doClose()
{
    ++m_generation; m_busy = false; m_loaded = false;
    m_left = {}; m_right = {}; m_rows.clear();
    emit busyChanged(false); emit comparisonChanged(); reportProgress(0, 0);
}
QWidget *VersionCompareSession::createView(QWidget *parent) { return new VersionCompareView(this, parent); }
void VersionCompareSession::setOptions(const Version::CompareOptions &options)
{
    m_options = options;
    if (m_loaded) updateComparison();
}
void VersionCompareSession::updateComparison()
{
    m_rows = Version::compare(m_left, m_right, m_options);
    int differences = 0;
    for (const auto &row : m_rows)
        if (row.difference != Version::Difference::Equal && row.difference != Version::Difference::Ignored) ++differences;
    setStatusText(tr("%1 differences / %2 fields • Left: %3 • Right: %4 • Read-only")
        .arg(differences).arg(m_rows.size())
        .arg(m_left.versions.isEmpty() ? tr("no version resource") : tr("%1 version resources").arg(m_left.versions.size()),
             m_right.versions.isEmpty() ? tr("no version resource") : tr("%1 version resources").arg(m_right.versions.size())));
    emit comparisonChanged();
}
bool VersionCompareSession::exportCsv(const QString &path, QString *error) const
{
    if (!m_loaded) return reject(error, tr("There is no comparison to export."));
    const QFileInfo target(path);
    for (const QString &input : {m_leftPath, m_rightPath}) {
        const QFileInfo source(input);
        if (target.absoluteFilePath() == source.absoluteFilePath() ||
            (!target.canonicalFilePath().isEmpty() && target.canonicalFilePath() == source.canonicalFilePath()))
            return reject(error, tr("Choose an export path different from both input files."));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return reject(error, file.errorString());
    const QByteArray data = Version::toCsv(m_rows).toUtf8();
    if (file.write(data) != data.size()) return reject(error, file.errorString());
    if (!file.commit()) return reject(error, file.errorString());
    if (error) error->clear();
    return true;
}
}
