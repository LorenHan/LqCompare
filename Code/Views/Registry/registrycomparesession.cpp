#include "registrycomparesession.h"
#include "registrycompareview.h"

#include <QFileInfo>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool nativeSource(const QString &source)
{
    QString root = source;
    root.replace(QLatin1Char('/'), QLatin1Char('\\'));
    root = root.section(QLatin1Char('\\'), 0, 0).toUpper();
    return root.startsWith(QStringLiteral("HKEY_")) || root == QStringLiteral("HKCU")
        || root == QStringLiteral("HKLM") || root == QStringLiteral("HKCR")
        || root == QStringLiteral("HKU") || root == QStringLiteral("HKCC");
}
QString sourceLabel(const QString &path)
{
    return nativeSource(path) ? path : QFileInfo(path).fileName();
}
QString loadError(const QString &side, const QString &path, const Registry::ReadResult &result)
{
    const QString line = result.errorLine > 0
        ? RegistryCompareSession::tr(" (line %1)").arg(result.errorLine) : QString();
    return RegistryCompareSession::tr("%1 source: %2%3\n%4")
        .arg(side, result.error, line, path);
}
}

RegistryCompareSession::RegistryCompareSession(QObject *parent)
    : RegistryCompareSession({}, {}, parent) {}

RegistryCompareSession::RegistryCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("registry"), parent), m_leftPath(left), m_rightPath(right),
      m_provider(Registry::createLocalProvider())
{
    updateTitle();
    updateStatus();
}

bool RegistryCompareSession::setLocalProvider(std::shared_ptr<const Registry::Provider> provider)
{
    if (state() != State::Created && state() != State::Failed) return false;
    m_provider = std::move(provider);
    return true;
}

Registry::ReadResult RegistryCompareSession::readSource(const QString &path,
                                                       const Registry::ReadOptions &options) const
{
    if (!nativeSource(path)) return Registry::readRegFile(path, options);
    Registry::ReadResult result;
    QString reason;
    const QString canonical = Registry::canonicalKeyPath(path, &reason);
    if (canonical.isEmpty()) {
        result.error = reason;
        return result;
    }
    const QString root = canonical.section(QLatin1Char('\\'), 0, 0);
    if (root.compare(QStringLiteral("HKEY_CURRENT_USER"), Qt::CaseInsensitive) != 0) {
        result.error = tr("Local registry comparison only reads HKEY_CURRENT_USER (HKCU). Choose a .reg export for other roots.");
        return result;
    }
    if (!m_provider) {
        result.error = Registry::localProviderDescription();
        return result;
    }
    return m_provider->read(canonical, options);
}

bool RegistryCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    QString reason;
    if (state() == State::Closed || state() == State::Opening) {
        reason = tr("This session is closed or currently opening.");
    } else if (state() == State::Open) {
        if (loadPair(left, right, m_options, &reason)) {
            if (error) error->clear();
            return true;
        }
    } else {
        m_leftPath = left.trimmed();
        m_rightPath = right.trimmed();
        updateTitle();
        emit pathsChanged();
        if (error) error->clear();
        return true;
    }
    reportError(reason);
    return reject(error, reason);
}

bool RegistryCompareSession::setReadOptions(const Registry::ReadOptions &options, QString *error)
{
    QString reason;
    if (state() == State::Closed || state() == State::Opening) {
        reason = tr("This session is closed or currently opening.");
    } else if (m_loaded) {
        if (loadPair(m_leftPath, m_rightPath, options, &reason)) {
            if (error) error->clear();
            return true;
        }
    } else {
        m_options = options;
        emit readOptionsChanged();
        if (error) error->clear();
        return true;
    }
    reportError(reason);
    return reject(error, reason);
}

bool RegistryCompareSession::loadPair(const QString &left, const QString &right,
                                    const Registry::ReadOptions &options, QString *error)
{
    const QString leftSource = left.trimmed(), rightSource = right.trimmed();
    if (leftSource.isEmpty() != rightSource.isEmpty())
        return reject(error, tr("Choose a source for both sides before comparing."));

    Registry::Snapshot nextLeft, nextRight;
    Registry::Comparison nextComparison;
    const bool hasSources = !leftSource.isEmpty();
    if (hasSources) {
        const Registry::ReadResult l = readSource(leftSource, options);
        if (!l.ok) return reject(error, loadError(tr("Left"), leftSource, l));
        const Registry::ReadResult r = readSource(rightSource, options);
        if (!r.ok) return reject(error, loadError(tr("Right"), rightSource, r));
        nextLeft = l.snapshot;
        nextRight = r.snapshot;
        nextComparison = Registry::compare(nextLeft, nextRight);
    }

    // Commit both sides and their options together; failed reads leave every old field intact.
    m_left = std::move(nextLeft);
    m_right = std::move(nextRight);
    m_comparison = std::move(nextComparison);
    m_leftPath = !hasSources || nativeSource(leftSource) ? leftSource : QFileInfo(leftSource).absoluteFilePath();
    m_rightPath = !hasSources || nativeSource(rightSource) ? rightSource : QFileInfo(rightSource).absoluteFilePath();
    m_options = options;
    m_loaded = hasSources;
    updateTitle();
    updateStatus();
    setDirty(false);
    emit pathsChanged();
    emit readOptionsChanged();
    emit comparisonChanged();
    if (error) error->clear();
    return true;
}

bool RegistryCompareSession::doOpen(QString *error)
{
    return loadPair(m_leftPath, m_rightPath, m_options, error);
}
bool RegistryCompareSession::doReload(QString *error) { return doOpen(error); }
void RegistryCompareSession::doClose()
{
    m_left = {};
    m_right = {};
    m_comparison = {};
    m_loaded = false;
    updateStatus();
    emit comparisonChanged();
}
QWidget *RegistryCompareSession::createView(QWidget *parent)
{
    return new RegistryCompareView(this, parent);
}
void RegistryCompareSession::updateTitle()
{
    setTitle(m_leftPath.isEmpty() && m_rightPath.isEmpty() ? tr("Registry Compare")
        : tr("%1 ↔ %2 [Registry]").arg(sourceLabel(m_leftPath), sourceLabel(m_rightPath)));
}
void RegistryCompareSession::updateStatus()
{
    if (!m_loaded) {
        setStatusText(tr("Choose two .reg exports or local HKCU sources • Read-only"));
        return;
    }
    QString status = tr("%1 differences • %2 keys unreadable • Read-only")
        .arg(m_comparison.differenceCount).arg(m_comparison.unreadableKeys);
    if (!m_comparison.complete()) status += tr(" • Incomplete comparison: unreadable keys are unknown");
    else if (m_comparison.equal()) status += tr(" • Equal");
    setStatusText(status);
}

} // namespace LqCompare
