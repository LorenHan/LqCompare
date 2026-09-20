#ifndef LQCOMPARE_HEXDIFF_H
#define LQCOMPARE_HEXDIFF_H

#include <QByteArray>
#include <QString>
#include <memory>

namespace LqCompare { namespace Hex {

struct Difference {
    qint64 offset = -1;
    qint64 length = 0;
    bool isValid() const { return offset >= 0 && length > 0; }
};

// Read-only, absolute-offset comparison. Input bytes are streamed to private
// temporary snapshots; paging never rereads a subsequently changed source.
// 512 MiB per side. A loaded pair uses <=64 MiB for its bitmap;
// transactional reload retains the old pair, so peak bitmap memory is 128 MiB
// plus two 1 MiB scan buffers. Snapshots use <=1 GiB disk (2 GiB on reload).
class Comparison {
public:
    static constexpr qint64 MaximumFileBytes = 512LL * 1024 * 1024;
    static constexpr int MaximumReadBytes = 1024 * 1024;
    Comparison();
    ~Comparison();
    Comparison(Comparison &&) noexcept;
    Comparison &operator=(Comparison &&) noexcept;
    Comparison(const Comparison &) = delete;
    Comparison &operator=(const Comparison &) = delete;

    bool load(const QString &left, const QString &right, QString *error = nullptr);
    bool isLoaded() const;
    QString path(bool left) const;
    qint64 size(bool left) const;
    qint64 extent() const;
    qint64 differentBytes() const;
    qint64 differenceRegions() const;
    qint64 bitmapBytes() const;
    bool differs(qint64 offset) const;
    QByteArray read(bool left, qint64 offset, int length, QString *error = nullptr) const;
    Difference differenceAt(qint64 offset) const;
    Difference firstDifference() const;
    Difference lastDifference() const;
    Difference nextDifference(qint64 offset) const;
    Difference previousDifference(qint64 offset) const;
    static bool parseOffset(const QString &text, qint64 *offset, QString *error = nullptr);
private:
    struct Data;
    std::unique_ptr<Data> m_data;
    qint64 findBit(qint64 from, bool value, bool forward) const;
};

} }
#endif
