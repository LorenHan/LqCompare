#include "windowsreparse.h"

// Qt 5.15.2 的 QtEndian 间接使用 numeric_limits，却未自行包含该标准头。
#include <limits>

#include <QtEndian>

namespace LqCompare {
namespace Files {
namespace WindowsReparse {

namespace {

constexpr int HeaderSize = 8;
constexpr int SymbolicLinkFieldsSize = 12;
constexpr int MountPointFieldsSize = 8;

// 偏移与长度以字节计；先检验，再按 UTF-16 码元读取。
// 用减法比较剩余容量，不能让 offset + length 的溢出绕过检查。
bool validNameRange(int offset, int length, int pathBytes) {
    return offset % 2 == 0 && length % 2 == 0 && offset <= pathBytes &&
           length <= pathBytes - offset;
}

} // namespace

QString target(const QByteArray& buffer, quint32 returnedSize) {
    // 分配容量不等于系统实际写入的长度；未写入的尾部绝不能参与解析。
    if (returnedSize < HeaderSize || returnedSize > MaximumBufferSize ||
        returnedSize > static_cast<quint32>(buffer.size())) {
        return QString();
    }

    // 布局来自 Microsoft 的 REPARSE_DATA_BUFFER 文档：
    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_reparse_data_buffer
    // qFromLittleEndian 使用字节读取，不把 QByteArray 强转为有对齐要求的结构。
    const char* bytes = buffer.constData();
    const quint32 tag = qFromLittleEndian<quint32>(bytes);
    const int payloadSize = qFromLittleEndian<quint16>(bytes + 4);
    if (payloadSize > static_cast<int>(returnedSize) - HeaderSize)
        return QString();

    int fieldsSize = 0;
    if (tag == SymbolicLinkTag)
        fieldsSize = SymbolicLinkFieldsSize;
    else if (tag == MountPointTag)
        fieldsSize = MountPointFieldsSize;
    else
        return QString();

    if (payloadSize < fieldsSize)
        return QString();

    const int substituteOffset = qFromLittleEndian<quint16>(bytes + HeaderSize);
    const int substituteLength = qFromLittleEndian<quint16>(bytes + HeaderSize + 2);
    const int printOffset = qFromLittleEndian<quint16>(bytes + HeaderSize + 4);
    const int printLength = qFromLittleEndian<quint16>(bytes + HeaderSize + 6);
    const int pathBytes = payloadSize - fieldsSize;
    if (pathBytes % 2 != 0 || substituteLength == 0 ||
        !validNameRange(substituteOffset, substituteLength, pathBytes) ||
        !validNameRange(printOffset, printLength, pathBytes)) {
        return QString();
    }

    // 两种名称可以任意排列，也不要求以空字符结尾；只按替代名称的长度读取。
    // 相对链接的 Flags 不用于改写目标，保持上层 linkTarget 的现有语义。
    const int start = HeaderSize + fieldsSize + substituteOffset;
    QString result(substituteLength / 2, Qt::Uninitialized);
    for (int index = 0; index < result.size(); ++index) {
        const quint16 codeUnit = qFromLittleEndian<quint16>(bytes + start + index * 2);
        // 名称长度不包含终止符；内部空字符会让 Win32 路径与 QString 显示不一致。
        if (codeUnit == 0)
            return QString();
        result[index] = QChar(codeUnit);
    }
    return result;
}

QString toWin32Target(const QString& target) {
    // 替代名称采用 NT 命名空间，不能直接让 Qt 与普通 C:\ / UNC 扫描根比较。
    // 仅识别有明确 Win32 对应的两类；Volume GUID、Device 等命名空间保留原样。
    const QString ntPrefix = QStringLiteral("\\??\\");
    if (!target.startsWith(ntPrefix))
        return target;

    const QString path = target.mid(ntPrefix.size());
    if (path.size() >= 3 && path.at(1) == QLatin1Char(':')
        && path.at(2) == QLatin1Char('\\')) {
        const ushort letter = path.at(0).unicode();
        if ((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'))
            return path;
    }
    if (path.startsWith(QStringLiteral("UNC\\"), Qt::CaseInsensitive)) {
        const QString suffix = path.mid(4);
        const int serverEnd = suffix.indexOf(QLatin1Char('\\'));
        // server/share 两段必须存在，不能把截断的 NT 名称误变成网络根。
        if (serverEnd > 0 && serverEnd + 1 < suffix.size()
            && suffix.at(serverEnd + 1) != QLatin1Char('\\'))
            return QStringLiteral("\\\\") + suffix;
    }
    return target;
}

} // namespace WindowsReparse
} // namespace Files
} // namespace LqCompare
