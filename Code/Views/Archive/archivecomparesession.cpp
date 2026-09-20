#include "archivecomparesession.h"
#include "archivecompareview.h"

#include <QDir>
#include <QFileInfo>

namespace LqCompare {
namespace {
QString normalizedSource(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString readError(const Archive::Directory &directory, const QString &side)
{
    QString text = side + QStringLiteral(": ") + directory.error.message;
    if (!directory.error.entryPath.isEmpty())
        text += QStringLiteral("\n") + directory.error.entryPath;
    if (directory.error.offset >= 0)
        text += QObject::tr(" (ZIP offset %1)").arg(directory.error.offset);
    return text;
}
}

ArchiveCompareSession::ArchiveCompareSession(QObject *parent)
    : ArchiveCompareSession({}, {}, parent)
{
}

ArchiveCompareSession::ArchiveCompareSession(const QString &left, const QString &right,
                                           QObject *parent)
    : CompareSession(QStringLiteral("archive"), parent),
      m_leftPath(normalizedSource(left)), m_rightPath(normalizedSource(right))
{
    updateTitle();
    updateStatus(tr("选择两个 ZIP / JAR 文件，比较归档目录。"));
}

bool ArchiveCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed) {
        const QString message = tr("此归档会话已关闭。请创建新会话。 ");
        if (error) *error = message;
        reportError(message);
        return false;
    }
    if (state() == State::Open) {
        QString reason;
        const bool ok = loadPair(left, right, &reason);
        if (error) *error = reason;
        if (!ok) reportError(reason);
        return ok;
    }
    m_leftPath = normalizedSource(left);
    m_rightPath = normalizedSource(right);
    updateTitle();
    if (view()) view()->setPaths(m_leftPath, m_rightPath);
    emit pathsChanged();
    if (error) error->clear();
    return true;
}

ArchiveCompareView *ArchiveCompareSession::view() const
{
    return qobject_cast<ArchiveCompareView *>(widget());
}

QWidget *ArchiveCompareSession::createView(QWidget *parent)
{
    auto *result = new ArchiveCompareView(parent);
    result->setPaths(m_leftPath, m_rightPath);
    result->setComparison(m_comparison, m_hasComparison);
    result->setStatus(statusText());
    connect(result, &ArchiveCompareView::compareRequested, this,
            [this](const QString &left, const QString &right) {
        QString error;
        if (setPaths(left, right, &error) && state() != State::Open)
            open(&error);
    });
    connect(result, &ArchiveCompareView::reloadRequested, this, [this] {
        QString error;
        if (state() == State::Open) reload(&error);
        else open(&error);
    });
    return result;
}

bool ArchiveCompareSession::doOpen(QString *error)
{
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) {
        updateStatus(tr("选择两个 ZIP / JAR 文件，比较归档目录。"));
        if (error) error->clear();
        return true;
    }
    return loadPair(m_leftPath, m_rightPath, error);
}

bool ArchiveCompareSession::doReload(QString *error)
{
    return loadPair(m_leftPath, m_rightPath, error);
}

bool ArchiveCompareSession::loadPair(const QString &leftPath, const QString &rightPath,
                                    QString *error)
{
    if (leftPath.isEmpty() || rightPath.isEmpty())
        return fail(tr("请选择左右两侧 ZIP / JAR 文件。"), error);

    const QString left = normalizedSource(leftPath), right = normalizedSource(rightPath);
    // Directory reads are bounded by Archive::Limits; payloads are never decompressed.
    const auto nextLeft = Archive::readZip(left);
    if (!nextLeft.ok()) return fail(readError(nextLeft, tr("左侧归档")), error);
    const auto nextRight = Archive::readZip(right);
    if (!nextRight.ok()) return fail(readError(nextRight, tr("右侧归档")), error);
    const auto next = Archive::compare(nextLeft, nextRight);
    if (!next.ok()) return fail(tr("归档目录无法比较。"), error);

    m_leftPath = left;
    m_rightPath = right;
    m_comparison = next;
    m_hasComparison = true;
    setDirty(false);
    updateTitle();
    if (view()) {
        view()->setPaths(left, right);
        view()->setComparison(m_comparison, true);
    }
    updateStatus(tr("目录读取完成：%1 项，%2 项结构或元数据差异。内容未经解压或逐字节验证。")
                 .arg(next.rows.size()).arg(next.differenceCount));
    emit pathsChanged();
    emit comparisonChanged();
    if (error) error->clear();
    return true;
}

bool ArchiveCompareSession::fail(const QString &message, QString *error)
{
    if (error) *error = message;
    if (m_hasComparison && view()) view()->setPaths(m_leftPath, m_rightPath);
    updateStatus(message + (m_hasComparison ? tr("\n读取失败，保留上次成功比较结果。") : QString()));
    return false;
}

void ArchiveCompareSession::doClose()
{
    m_comparison = {};
    m_hasComparison = false;
    m_leftPath.clear();
    m_rightPath.clear();
    setDirty(false);
    if (view()) {
        view()->setComparison(m_comparison, false);
        view()->setPaths({}, {});
        view()->setEnabled(false);
    }
    updateStatus(tr("归档会话已关闭。"));
    emit pathsChanged();
    emit comparisonChanged();
}

void ArchiveCompareSession::updateTitle()
{
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) setTitle(tr("压缩包比较"));
    else setTitle(tr("%1 ↔ %2").arg(QFileInfo(m_leftPath).fileName(),
                                     QFileInfo(m_rightPath).fileName()));
}

void ArchiveCompareSession::updateStatus(const QString &message)
{
    setStatusText(message);
    if (view()) view()->setStatus(message);
}

} // namespace LqCompare
