#ifndef LQCOMPARE_WINDOWSREPARSE_H
#define LQCOMPARE_WINDOWSREPARSE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace LqCompare {
namespace Files {
namespace WindowsReparse {

// Windows 公开的线格式常量；不依赖旧版 MinGW 缺失的 REPARSE_DATA_BUFFER。
constexpr quint32 SymbolicLinkTag = 0xa000000c;
constexpr quint32 MountPointTag = 0xa0000003;
constexpr int MaximumBufferSize = 16 * 1024;

/// 从 FSCTL_GET_REPARSE_POINT 返回的数据中读取替代名称（原始 UTF-16 目标）。
/// 只读取 returnedSize 内、且属于声明载荷的字节；未知标签或损坏数据返回空串。
/// 目标长度内不允许 U+0000，防止 Win32 路径截断；长度外的可选终止符不参与解码。
/// 不访问文件系统、不解析相对路径，也不依赖主机的字节序或 wchar_t 宽度。
QString target(const QByteArray& buffer, quint32 returnedSize);

} // namespace WindowsReparse
} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_WINDOWSREPARSE_H
