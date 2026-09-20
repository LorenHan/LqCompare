#include "tablecomparesession.h"
#include "tablecompareview.h"

#include <QFileInfo>
#include <QSignalBlocker>
#include <QVariantList>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &reason)
{
    if (error) *error = reason;
    return false;
}
}

TableCompareSession::TableCompareSession(QObject *parent)
    : TableCompareSession({}, {}, parent) {}

TableCompareSession::TableCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("table"), parent), m_leftPath(left), m_rightPath(right)
{
    updateTitle();
    connect(sessionSettings(), &SessionSettings::changed, this, [this](const QString &key) {
        if (!key.isEmpty() && !key.startsWith(QStringLiteral("table."))) return;
        const auto oldLeft = m_leftParse, oldRight = m_rightParse;
        const auto oldOptions = m_options;
        readSettings();
        if (state() == State::Open) {
            QString reason;
            if (!loadPair(m_leftPath, m_rightPath, m_leftParse, m_rightParse, &reason)) {
                m_leftParse = oldLeft;
                m_rightParse = oldRight;
                m_options = oldOptions;
                writeSettings();
                reportError(reason);
            }
        }
    });
}

void TableCompareSession::readSettings()
{
    auto *settings = sessionSettings();
    auto parse = [settings](const QString &side) {
        Table::ParseOptions options;
        const QString prefix = QStringLiteral("table.") + side + QLatin1Char('.');
        options.delimiter = settings->value(prefix + QStringLiteral("delimiter")).toString();
        options.encoding = settings->value(prefix + QStringLiteral("encoding")).toByteArray();
        options.firstRowHeader = settings->value(prefix + QStringLiteral("header"), true).toBool();
        return options;
    };
    m_leftParse = parse(QStringLiteral("left"));
    m_rightParse = parse(QStringLiteral("right"));
    m_options.columnMode = static_cast<Table::ColumnMappingMode>(qBound(0,
        settings->value(QStringLiteral("table.columnMode"), 0).toInt(), 2));
    m_options.alignment = static_cast<Table::RowAlignment>(qBound(0,
        settings->value(QStringLiteral("table.alignment"), 0).toInt(), 2));
    m_options.minimumSimilarity = settings->value(QStringLiteral("table.minimumSimilarity"), 0.5).toDouble();
    m_options.columns.clear();
    for (const QVariant &item : settings->value(QStringLiteral("table.columns")).toList()) {
        const auto map = item.toMap();
        Table::ColumnRule rule;
        rule.left = map.value(QStringLiteral("left"), -1).toInt();
        rule.right = map.value(QStringLiteral("right"), -1).toInt();
        rule.role = static_cast<Table::ColumnRole>(qBound(0, map.value(QStringLiteral("role"), 0).toInt(), 3));
        rule.numeric = map.value(QStringLiteral("numeric"), false).toBool();
        rule.absoluteTolerance = map.value(QStringLiteral("absoluteTolerance"), 0).toDouble();
        rule.relativeTolerance = map.value(QStringLiteral("relativeTolerance"), 0).toDouble();
        rule.compareFormatting = map.value(QStringLiteral("compareFormatting"), false).toBool();
        m_options.columns.append(rule);
    }
}

void TableCompareSession::writeSettings()
{
    auto *settings = sessionSettings();
    const QSignalBlocker blocker(settings);
    auto parse = [settings](const QString &side, const Table::ParseOptions &options) {
        const QString prefix = QStringLiteral("table.") + side + QLatin1Char('.');
        settings->setValue(prefix + QStringLiteral("delimiter"), options.delimiter);
        settings->setValue(prefix + QStringLiteral("encoding"), options.encoding);
        settings->setValue(prefix + QStringLiteral("header"), options.firstRowHeader);
    };
    parse(QStringLiteral("left"), m_leftParse);
    parse(QStringLiteral("right"), m_rightParse);
    settings->setValue(QStringLiteral("table.columnMode"), static_cast<int>(m_options.columnMode));
    settings->setValue(QStringLiteral("table.alignment"), static_cast<int>(m_options.alignment));
    settings->setValue(QStringLiteral("table.minimumSimilarity"), m_options.minimumSimilarity);
    QVariantList columns;
    for (const auto &rule : m_options.columns) {
        QVariantMap map;
        map.insert(QStringLiteral("left"), rule.left);
        map.insert(QStringLiteral("right"), rule.right);
        map.insert(QStringLiteral("role"), static_cast<int>(rule.role));
        map.insert(QStringLiteral("numeric"), rule.numeric);
        map.insert(QStringLiteral("absoluteTolerance"), rule.absoluteTolerance);
        map.insert(QStringLiteral("relativeTolerance"), rule.relativeTolerance);
        map.insert(QStringLiteral("compareFormatting"), rule.compareFormatting);
        columns.append(map);
    }
    settings->setValue(QStringLiteral("table.columns"), columns);
}

bool TableCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    if (state() == State::Open) return loadPair(left, right, m_leftParse, m_rightParse, error);
    m_leftPath = left;
    m_rightPath = right;
    updateTitle();
    emit pathsChanged();
    if (error) error->clear();
    return true;
}

bool TableCompareSession::loadPair(const QString &left, const QString &right,
                                  const Table::ParseOptions &leftParse,
                                  const Table::ParseOptions &rightParse, QString *error)
{
    Table::Document a, b;
    QString reason;
    const QString leftSource = left.isEmpty() ? QString() : QFileInfo(left).absoluteFilePath();
    const QString rightSource = right.isEmpty() ? QString() : QFileInfo(right).absoluteFilePath();
    if (!Table::Document::load(leftSource, &a, &reason, leftParse))
        return reject(error, tr("Left file: %1").arg(reason));
    if (!Table::Document::load(rightSource, &b, &reason, rightParse))
        return reject(error, tr("Right file: %1").arg(reason));
    // Commit both sources together. Mapping errors remain visible and repairable
    // using the column dialog, without losing the successfully parsed sources.
    auto result = Table::compare(a, b, m_options);
    m_left = a;
    m_right = b;
    m_leftPath = a.path;
    m_rightPath = b.path;
    m_leftParse = leftParse;
    m_rightParse = rightParse;
    m_result = result;
    updateTitle();
    emit pathsChanged();
    publishResult();
    if (error) error->clear();
    return true;
}

bool TableCompareSession::setComparisonOptions(const Table::CompareOptions &options, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    auto result = Table::compare(m_left, m_right, options);
    if (!result.ok()) return reject(error, result.error);
    m_options = options;
    m_result = result;
    writeSettings();
    publishResult();
    if (error) error->clear();
    return true;
}

bool TableCompareSession::setParseOptions(const Table::ParseOptions &left,
                                         const Table::ParseOptions &right, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    if (state() == State::Open && !loadPair(m_leftPath, m_rightPath, left, right, error)) return false;
    m_leftParse = left;
    m_rightParse = right;
    writeSettings();
    if (error) error->clear();
    return true;
}

QWidget *TableCompareSession::createView(QWidget *parent) { return new TableCompareView(this, parent); }
bool TableCompareSession::doOpen(QString *error)
{
    readSettings();
    return loadPair(m_leftPath, m_rightPath, m_leftParse, m_rightParse, error);
}
bool TableCompareSession::doReload(QString *error) { return doOpen(error); }

void TableCompareSession::updateTitle()
{
    setTitle(tr("%1 ↔ %2").arg(m_leftPath.isEmpty() ? tr("Empty table") : QFileInfo(m_leftPath).fileName(),
                               m_rightPath.isEmpty() ? tr("Empty table") : QFileInfo(m_rightPath).fileName()));
}

void TableCompareSession::publishResult()
{
    m_currentDifference = m_result.differences.isEmpty() ? -1
        : qBound(0, m_currentDifference, m_result.differences.size() - 1);
    QString status;
    if (!m_result.ok()) status = tr("Cannot compare: %1").arg(m_result.error);
    else {
        const auto &s = m_result.statistics;
        status = tr("Equal %1 • Changed %2 • Left only %3 • Right only %4 • Different cells %5 • Ignored cells %6")
            .arg(s.equalRows).arg(s.differentRows).arg(s.leftOnlyRows).arg(s.rightOnlyRows)
            .arg(s.differentCells).arg(s.ignoredCells);
    }
    status += tr(" • Read only • Left %1 / Right %2").arg(QString::fromLatin1(m_left.encoding),
                                                         QString::fromLatin1(m_right.encoding));
    setStatusText(status);
    emit comparisonChanged();
}

void TableCompareSession::selectDifference(int index)
{
    if (index < 0 || index >= m_result.differences.size()) return;
    m_currentDifference = index;
    emit currentDifferenceChanged(index);
}
void TableCompareSession::previousDifference() { selectDifference(m_currentDifference - 1); }
void TableCompareSession::nextDifference() { selectDifference(m_currentDifference + 1); }
void TableCompareSession::firstDifference() { selectDifference(0); }
void TableCompareSession::lastDifference() { selectDifference(m_result.differences.size() - 1); }

}
