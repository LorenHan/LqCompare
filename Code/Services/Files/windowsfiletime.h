#ifndef LQCOMPARE_WINDOWSFILETIME_H
#define LQCOMPARE_WINDOWSFILETIME_H

#include <QtGlobal>

namespace LqCompare {
namespace Files {
namespace WindowsFileTime {

/// FILETIME 的 UTC / 1601 纪元 100ns 数与内部 UTC / 1970 纪元纳秒数互转。
/// 不依赖 Windows SDK，三平台均可验证真正用于原生实现的整数换算。
/// 返回 false 表示超出 qint64 纳秒范围或输出指针为空；失败时不改输出值。
bool fromTicks(quint64 ticks, qint64 *nanoseconds);

/// 舍去不足 100ns 的部分，始终向过去取整：-1ns → -100ns，而非 0。
/// 若取整后超出内部范围（qint64 最小值起的 8ns），拒绝转换，不回绕或钳位。
/// FileTime 的无效值由调用方处理：setTimes 用 nullptr 保留对应原有时间。
bool toTicks(qint64 nanoseconds, quint64 *ticks);

} // namespace WindowsFileTime
} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_WINDOWSFILETIME_H
