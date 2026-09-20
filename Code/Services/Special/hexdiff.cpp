#include "hexdiff.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <algorithm>
#include <limits>

namespace LqCompare { namespace Hex {
namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}
struct Comparison::Data {
    QString leftPath, rightPath;
    qint64 leftSize = 0, rightSize = 0;
    qint64 changedBytes = 0, regions = 0, first = -1, last = -1;
    QByteArray bits;
    QTemporaryFile leftSnapshot{QDir::tempPath() + QStringLiteral("/lqcompare-hex-XXXXXX")};
    QTemporaryFile rightSnapshot{QDir::tempPath() + QStringLiteral("/lqcompare-hex-XXXXXX")};
};
Comparison::Comparison() = default;
Comparison::~Comparison() = default;
Comparison::Comparison(Comparison &&) noexcept = default;
Comparison &Comparison::operator=(Comparison &&) noexcept = default;

bool Comparison::load(const QString &left, const QString &right, QString *error)
{
    auto next = std::make_unique<Data>();
    QFile leftFile(left), rightFile(right);
    const QFileInfo leftInfo(left), rightInfo(right);
    if (left.isEmpty() || right.isEmpty())
        return fail(error, QStringLiteral("Choose both binary files before comparing."));
    for (QFile *file : {&leftFile, &rightFile}) {
        const QFileInfo info(file->fileName());
        if (!info.isFile()) return fail(error, QStringLiteral("Not a regular binary file: %1").arg(file->fileName()));
        if (info.size() > MaximumFileBytes)
            return fail(error, QStringLiteral("Binary file exceeds the 512 MiB per-file limit: %1 (%2 bytes)")
                        .arg(file->fileName()).arg(info.size()));
        if (!file->open(QIODevice::ReadOnly))
            return fail(error, QStringLiteral("Cannot open %1: %2").arg(file->fileName(), file->errorString()));
    }
    const QDateTime leftModified = leftInfo.lastModified();
    const QDateTime rightModified = rightInfo.lastModified();
    next->leftPath = leftInfo.absoluteFilePath();
    next->rightPath = rightInfo.absoluteFilePath();
    next->leftSize = leftFile.size();
    next->rightSize = rightFile.size();
    const qint64 total = qMax(next->leftSize, next->rightSize);
    if (total > MaximumFileBytes)
        return fail(error, QStringLiteral("A binary file grew beyond the 512 MiB limit while opening."));
    if (!next->leftSnapshot.open() || !next->rightSnapshot.open())
        return fail(error, QStringLiteral("Cannot create temporary binary snapshots. Check free space and temporary-directory permissions."));
    next->bits = QByteArray(int((total + 7) / 8), '\0');
    bool previousChanged = false;
    for (qint64 base = 0; base < total; base += MaximumReadBytes) {
        const int count = int(qMin<qint64>(MaximumReadBytes, total - base));
        const int leftCount = int(qBound<qint64>(0, next->leftSize - base, count));
        const int rightCount = int(qBound<qint64>(0, next->rightSize - base, count));
        const QByteArray a = leftFile.read(leftCount), b = rightFile.read(rightCount);
        if (a.size() != leftCount || b.size() != rightCount)
            return fail(error, QStringLiteral("A binary file changed or could not be read completely. Reload both files."));
        if (next->leftSnapshot.write(a) != a.size() || next->rightSnapshot.write(b) != b.size())
            return fail(error, QStringLiteral("Cannot write temporary binary snapshots. Check available disk space."));
        if (a.size() == b.size() && a == b) {
            previousChanged = false;
            continue;
        }
        for (int i = 0; i < count; ++i) {
            const bool changed = i >= a.size() || i >= b.size() || a.at(i) != b.at(i);
            if (changed) {
                const qint64 pos = base + i;
                next->bits[int(pos / 8)] = char(uchar(next->bits.at(int(pos / 8))) | (1u << (pos % 8)));
                ++next->changedBytes;
                if (!previousChanged) ++next->regions;
                if (next->first < 0) next->first = pos;
                next->last = pos;
            }
            previousChanged = changed;
        }
    }
    if (!next->leftSnapshot.flush() || !next->rightSnapshot.flush())
        return fail(error, QStringLiteral("Cannot flush temporary binary snapshots. Check available disk space."));
    const QFileInfo finalLeft(left), finalRight(right);
    if (leftFile.size() != next->leftSize || rightFile.size() != next->rightSize ||
        finalLeft.lastModified() != leftModified || finalRight.lastModified() != rightModified)
        return fail(error, QStringLiteral("A source file changed during comparison. Reload both files."));
    m_data = std::move(next);
    if (error) error->clear();
    return true;
}
bool Comparison::isLoaded() const { return bool(m_data); }
QString Comparison::path(bool left) const { return !m_data ? QString() : left ? m_data->leftPath : m_data->rightPath; }
qint64 Comparison::size(bool left) const { return !m_data ? 0 : left ? m_data->leftSize : m_data->rightSize; }
qint64 Comparison::extent() const { return qMax(size(true), size(false)); }
qint64 Comparison::differentBytes() const { return m_data ? m_data->changedBytes : 0; }
qint64 Comparison::differenceRegions() const { return m_data ? m_data->regions : 0; }
qint64 Comparison::bitmapBytes() const { return m_data ? m_data->bits.size() : 0; }
bool Comparison::differs(qint64 offset) const
{
    return m_data && offset >= 0 && offset < extent() &&
        (uchar(m_data->bits.at(int(offset / 8))) & (1u << (offset % 8)));
}
QByteArray Comparison::read(bool left, qint64 offset, int length, QString *error) const
{
    if (!m_data || offset < 0 || offset > size(left) || length < 0 || length > MaximumReadBytes) {
        fail(error, QStringLiteral("Invalid binary page request (maximum 1 MiB)."));
        return {};
    }
    auto &file = left ? m_data->leftSnapshot : m_data->rightSnapshot;
    const qint64 expected = qMin<qint64>(length, size(left) - offset);
    if (!file.seek(offset)) {
        fail(error, QStringLiteral("Cannot seek binary snapshot: %1").arg(file.errorString()));
        return {};
    }
    QByteArray bytes = file.read(expected);
    if (bytes.size() != expected) {
        fail(error, QStringLiteral("Cannot read binary snapshot: %1").arg(file.errorString()));
        return {};
    }
    if (error) error->clear();
    return bytes;
}
qint64 Comparison::findBit(qint64 from, bool value, bool forward) const
{
    if (!m_data || from < 0 || from >= extent()) return -1;
    const qint64 step = forward ? 1 : -1;
    qint64 pos = from;
    while (pos >= 0 && pos < extent()) {
        const uchar byte = uchar(m_data->bits.at(int(pos / 8)));
        if ((value && byte == 0) || (!value && byte == 255)) {
            pos = forward ? (pos / 8 + 1) * 8 : (pos / 8) * 8 - 1;
            continue;
        }
        if (bool(byte & (1u << (pos % 8))) == value) return pos;
        pos += step;
    }
    return -1;
}
Difference Comparison::differenceAt(qint64 offset) const
{
    if (!differs(offset)) return {};
    const qint64 start = findBit(offset, false, false) + 1;
    const qint64 nextEqual = findBit(offset, false, true);
    const qint64 end = nextEqual < 0 ? extent() : nextEqual;
    return {start, end - start};
}
Difference Comparison::firstDifference() const { return differenceAt(m_data ? m_data->first : -1); }
Difference Comparison::lastDifference() const { return differenceAt(m_data ? m_data->last : -1); }
Difference Comparison::nextDifference(qint64 offset) const
{
    if (offset < -1 || offset >= extent()) return {};
    const Difference current = differenceAt(offset);
    const qint64 start = current.isValid() ? current.offset + current.length : offset + 1;
    return differenceAt(findBit(qMax<qint64>(0, start), true, true));
}
Difference Comparison::previousDifference(qint64 offset) const
{
    if (offset <= 0 || offset > extent()) return {};
    const Difference current = differenceAt(offset);
    const qint64 start = current.isValid() ? current.offset - 1 : offset - 1;
    return differenceAt(findBit(qMin(extent() - 1, start), true, false));
}
bool Comparison::parseOffset(const QString &text, qint64 *offset, QString *error)
{
    QString value = text.trimmed();
    int base = 10;
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) { base = 16; value.remove(0, 2); }
    if (value.isEmpty() || !offset) return fail(error, QStringLiteral("Enter a decimal offset or a hexadecimal offset beginning with 0x."));
    for (QChar c : value) {
        const bool digit = c >= QLatin1Char('0') && c <= QLatin1Char('9');
        const bool hex = base == 16 && ((c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F')));
        if (!digit && !hex) return fail(error, QStringLiteral("Invalid byte offset. Use decimal digits or 0x-prefixed hexadecimal."));
    }
    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok, base);
    if (!ok || parsed > qulonglong(std::numeric_limits<qint64>::max()))
        return fail(error, QStringLiteral("Byte offset is too large."));
    *offset = qint64(parsed);
    if (error) error->clear();
    return true;
}
} }
