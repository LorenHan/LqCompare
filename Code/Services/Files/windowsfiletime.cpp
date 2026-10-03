#include "windowsfiletime.h"

#include <limits>

namespace LqCompare {
namespace Files {
namespace WindowsFileTime {

namespace {

// FILETIME 定义：https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime
// 1601 到 1970 相差 11644473600 秒。必须以 100ns 为单位保存纪元差，
// 换成纳秒会超过 qint64；先乘 100 再减纪元差，对现代时间同样会溢出。
constexpr qint64 kUnixEpochTicks = 116444736000000000LL;
constexpr qint64 kNanosecondsPerTick = 100;
constexpr qint64 kMaximumOffsetTicks = std::numeric_limits<qint64>::max() / kNanosecondsPerTick;

} // namespace

bool fromTicks(quint64 ticks, qint64 *nanoseconds)
{
    if (!nanoseconds)
        return false;

    const quint64 epoch = static_cast<quint64>(kUnixEpochTicks);
    const bool beforeEpoch = ticks < epoch;
    // 无符号差值在比较后才相减，连最高位为 1 的输入也不会被误当成负数。
    const quint64 magnitude = beforeEpoch ? epoch - ticks : ticks - epoch;
    if (magnitude > static_cast<quint64>(kMaximumOffsetTicks))
        return false;

    const qint64 offset = static_cast<qint64>(magnitude) * kNanosecondsPerTick;
    *nanoseconds = beforeEpoch ? -offset : offset;
    return true;
}

bool toTicks(qint64 nanoseconds, quint64 *ticks)
{
    if (!ticks)
        return false;

    qint64 offset = nanoseconds / kNanosecondsPerTick;
    if (nanoseconds % kNanosecondsPerTick < 0)
        --offset;
    // 最低 8ns 向过去取整后无法读回内部类型，不能伪造一个有效时间。
    if (offset < -kMaximumOffsetTicks)
        return false;

    // qint64 纳秒范围比 FILETIME 小：此时相加必定为正且不会溢出。
    *ticks = static_cast<quint64>(kUnixEpochTicks + offset);
    return true;
}

} // namespace WindowsFileTime
} // namespace Files
} // namespace LqCompare
