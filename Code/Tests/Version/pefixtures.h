#ifndef LQCOMPARE_TESTS_PEFIXTURES_H
#define LQCOMPARE_TESTS_PEFIXTURES_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

// Generated structural PE test data. This is never executed or passed to a loader.
// Layout follows Microsoft PE/COFF and VERSIONINFO documentation:
// https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
// https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo
// https://learn.microsoft.com/en-us/windows/win32/menurc/string-str
// https://learn.microsoft.com/en-us/windows/win32/menurc/var-str
namespace PeFixtures {
constexpr int peOffset = 0x80;
constexpr int coffOffset = peOffset + 4;
constexpr int optionalOffset = coffOffset + 20;
constexpr int textOffset = 0x400;
constexpr int dataOffset = 0x600;
constexpr int resourceOffset = 0xc00;
constexpr int fileSize = 0x1800;
constexpr quint32 textRva = 0x1000;
constexpr quint32 dataRva = 0x2000;
constexpr quint32 resourceRva = 0x3000;
constexpr int importOffset = dataOffset + 0x200;
constexpr int thunkOffset = dataOffset + 0x240;
constexpr int dllOffset = dataOffset + 0x280;
constexpr int hintNameOffset = dataOffset + 0x2a0;
constexpr int versionOffset = resourceOffset + 0x100;
constexpr int secondVersionOffset = resourceOffset + 0x600;

inline void put16(QByteArray &bytes, int offset, quint16 value)
{
    Q_ASSERT(offset >= 0 && offset + 2 <= bytes.size());
    for (int i = 0; i < 2; ++i) bytes[offset + i] = char(value >> (8 * i));
}
inline void put32(QByteArray &bytes, int offset, quint32 value)
{
    Q_ASSERT(offset >= 0 && offset + 4 <= bytes.size());
    for (int i = 0; i < 4; ++i) bytes[offset + i] = char(value >> (8 * i));
}
inline void put64(QByteArray &bytes, int offset, quint64 value)
{
    Q_ASSERT(offset >= 0 && offset + 8 <= bytes.size());
    for (int i = 0; i < 8; ++i) bytes[offset + i] = char(value >> (8 * i));
}
inline void putBytes(QByteArray &bytes, int offset, const QByteArray &value)
{
    Q_ASSERT(offset >= 0 && offset + value.size() <= bytes.size());
    bytes.replace(offset, value.size(), value);
}
inline void putAscii(QByteArray &bytes, int offset, const char *value)
{
    putBytes(bytes, offset, QByteArray(value) + '\0');
}
inline QByteArray utf16(const QString &value, bool terminated = true)
{
    QByteArray result((value.size() + (terminated ? 1 : 0)) * 2, '\0');
    for (int i = 0; i < value.size(); ++i) put16(result, i * 2, value.at(i).unicode());
    return result;
}
inline void align4(QByteArray &bytes)
{
    while (bytes.size() % 4) bytes.append('\0');
}
inline QByteArray block(const QString &key, quint16 type, const QByteArray &value,
                        const QVector<QByteArray> &children = {})
{
    QByteArray bytes(6, '\0');
    put16(bytes, 2, quint16(type == 1 ? value.size() / 2 : value.size()));
    put16(bytes, 4, type);
    bytes += utf16(key);
    align4(bytes);
    bytes += value;
    for (const auto &child : children) {
        align4(bytes);
        bytes += child;
    }
    Q_ASSERT(bytes.size() <= 0xffff);
    put16(bytes, 0, quint16(bytes.size()));
    return bytes;
}
inline QByteArray stringValue(const QString &key, const QString &value)
{
    return block(key, 1, utf16(value));
}
inline QByteArray versionInfo(bool missingFields = false)
{
    QByteArray fixed(52, '\0');
    put32(fixed, 0, 0xfeef04bd);
    put32(fixed, 4, 0x00010000);
    put32(fixed, 8, 0x00010002); // FileVersion 1.2.3.4
    put32(fixed, 12, 0x00030004);
    put32(fixed, 16, 0x00050006); // ProductVersion 5.6.7.8
    put32(fixed, 20, 0x00070008);
    put32(fixed, 24, 0x3f);
    put32(fixed, 28, 0x1);
    put32(fixed, 32, 0x00040004); // VOS_NT_WINDOWS32
    put32(fixed, 36, 0x2); // VFT_DLL
    QVector<QByteArray> english {
        stringValue(QStringLiteral("FileVersion"), QStringLiteral("1.2.3.4")),
        stringValue(QStringLiteral("CustomField"), QStringLiteral("preserved"))
    };
    QVector<QByteArray> chinese {
        stringValue(QStringLiteral("FileDescription"), QString::fromUtf8("版本语料")),
        stringValue(QStringLiteral("CompanyName"), QString::fromUtf8("样例公司"))
    };
    if (!missingFields) {
        english += stringValue(QStringLiteral("CompanyName"), QStringLiteral("Fixture Labs"));
        english += stringValue(QStringLiteral("FileDescription"), QStringLiteral("Native fixture"));
        english += stringValue(QStringLiteral("ProductVersion"), QStringLiteral("5.6.7.8"));
    }
    const auto strings = block(QStringLiteral("StringFileInfo"), 1, {}, {
        block(QStringLiteral("040904B0"), 1, {}, english),
        block(QStringLiteral("080404B0"), 1, {}, chinese)
    });
    QByteArray translations(8, '\0');
    put32(translations, 0, 0x04b00409);
    put32(translations, 4, 0x04b00804);
    const auto vars = block(QStringLiteral("VarFileInfo"), 1, {}, {
        block(QStringLiteral("Translation"), 0, translations)
    });
    return block(QStringLiteral("VS_VERSION_INFO"), 0, fixed, {strings, vars});
}
inline int directoryOffset(bool pe32Plus) { return optionalOffset + (pe32Plus ? 112 : 96); }
inline int sectionOffset(bool pe32Plus) { return optionalOffset + (pe32Plus ? 240 : 224); }
inline void setDirectory(QByteArray &bytes, bool pe32Plus, int index, quint32 rva, quint32 size)
{
    put32(bytes, directoryOffset(pe32Plus) + index * 8, rva);
    put32(bytes, directoryOffset(pe32Plus) + index * 8 + 4, size);
}
inline void section(QByteArray &bytes, int offset, const char *name, quint32 rva,
                    quint32 rawOffset, quint32 size, quint32 characteristics)
{
    putBytes(bytes, offset, QByteArray(name));
    put32(bytes, offset + 8, size);
    put32(bytes, offset + 12, rva);
    put32(bytes, offset + 16, size);
    put32(bytes, offset + 20, rawOffset);
    put32(bytes, offset + 36, characteristics);
}
inline QByteArray image(bool pe32Plus = false, bool withVersions = true, bool missingFields = false)
{
    QByteArray bytes(fileSize, '\0');
    putBytes(bytes, 0, QByteArrayLiteral("MZ"));
    put32(bytes, 0x3c, peOffset);
    putBytes(bytes, peOffset, QByteArray("PE\0\0", 4));
    put16(bytes, coffOffset, pe32Plus ? 0x8664 : 0x014c);
    put16(bytes, coffOffset + 2, 3);
    put32(bytes, coffOffset + 4, 0x65a0bc00);
    put16(bytes, coffOffset + 16, pe32Plus ? 240 : 224);
    put16(bytes, coffOffset + 18, pe32Plus ? 0x2022 : 0x2102);
    put16(bytes, optionalOffset, pe32Plus ? 0x20b : 0x10b);
    bytes[optionalOffset + 2] = 14;
    put32(bytes, optionalOffset + 4, 0x200);
    put32(bytes, optionalOffset + 8, 0x1200);
    put32(bytes, optionalOffset + 16, textRva);
    put32(bytes, optionalOffset + 20, textRva);
    if (pe32Plus) {
        put64(bytes, optionalOffset + 24, Q_UINT64_C(0x180000000));
        put64(bytes, optionalOffset + 72, 0x100000);
        put64(bytes, optionalOffset + 80, 0x1000);
        put64(bytes, optionalOffset + 88, 0x100000);
        put64(bytes, optionalOffset + 96, 0x1000);
    } else {
        put32(bytes, optionalOffset + 24, dataRva);
        put32(bytes, optionalOffset + 28, 0x00400000);
        put32(bytes, optionalOffset + 72, 0x100000);
        put32(bytes, optionalOffset + 76, 0x1000);
        put32(bytes, optionalOffset + 80, 0x100000);
        put32(bytes, optionalOffset + 84, 0x1000);
    }
    put32(bytes, optionalOffset + 32, 0x1000);
    put32(bytes, optionalOffset + 36, 0x200);
    put16(bytes, optionalOffset + 40, 6);
    put16(bytes, optionalOffset + 48, 6);
    put32(bytes, optionalOffset + 56, 0x4000);
    put32(bytes, optionalOffset + 60, 0x400);
    put32(bytes, optionalOffset + 64, 0x12345678);
    put16(bytes, optionalOffset + 68, 3);
    put16(bytes, optionalOffset + 70, 0x0140);
    put32(bytes, optionalOffset + (pe32Plus ? 108 : 92), 16);
    section(bytes, sectionOffset(pe32Plus), ".text", textRva, textOffset, 0x200, 0x60000020);
    section(bytes, sectionOffset(pe32Plus) + 40, ".rdata", dataRva, dataOffset, 0x600, 0x40000040);
    section(bytes, sectionOffset(pe32Plus) + 80, ".rsrc", resourceRva, resourceOffset, 0xc00, 0x40000040);
    bytes[textOffset] = char(0xc3);
    bytes[textOffset + 1] = char(0xc3);

    // Export table: ordinal base 7, named 7, ordinal-only 8, forwarded 9.
    setDirectory(bytes, pe32Plus, 0, dataRva, 0x180);
    put32(bytes, dataOffset + 12, dataRva + 0x60);
    put32(bytes, dataOffset + 16, 7);
    put32(bytes, dataOffset + 20, 3);
    put32(bytes, dataOffset + 24, 2);
    put32(bytes, dataOffset + 28, dataRva + 0x40);
    put32(bytes, dataOffset + 32, dataRva + 0x50);
    put32(bytes, dataOffset + 36, dataRva + 0x58);
    put32(bytes, dataOffset + 0x40, textRva);
    put32(bytes, dataOffset + 0x44, textRva + 1);
    put32(bytes, dataOffset + 0x48, dataRva + 0xb0);
    put32(bytes, dataOffset + 0x50, dataRva + 0x80);
    put32(bytes, dataOffset + 0x54, dataRva + 0x90);
    put16(bytes, dataOffset + 0x58, 0);
    put16(bytes, dataOffset + 0x5a, 2);
    putAscii(bytes, dataOffset + 0x60, "fixture.dll");
    putAscii(bytes, dataOffset + 0x80, "Alpha");
    putAscii(bytes, dataOffset + 0x90, "Forwarded");
    putAscii(bytes, dataOffset + 0xb0, "KERNEL32.Sleep");

    setDirectory(bytes, pe32Plus, 1, dataRva + 0x200, 40);
    put32(bytes, importOffset, dataRva + 0x240);
    put32(bytes, importOffset + 12, dataRva + 0x280);
    put32(bytes, importOffset + 16, dataRva + 0x260);
    for (const int offset : {thunkOffset, dataOffset + 0x260}) {
        if (pe32Plus) {
            put64(bytes, offset, dataRva + 0x2a0);
            put64(bytes, offset + 8, Q_UINT64_C(0x800000000000002a));
        } else {
            put32(bytes, offset, dataRva + 0x2a0);
            put32(bytes, offset + 4, 0x8000002a);
        }
    }
    putAscii(bytes, dllOffset, "KERNEL32.dll");
    put16(bytes, hintNameOffset, 17);
    putAscii(bytes, hintNameOffset + 2, "CreateFileW");

    if (withVersions) {
        setDirectory(bytes, pe32Plus, 2, resourceRva, 0xc00);
        put16(bytes, resourceOffset + 14, 1);
        put32(bytes, resourceOffset + 16, 16); // RT_VERSION
        put32(bytes, resourceOffset + 20, 0x80000020);
        put16(bytes, resourceOffset + 0x20 + 14, 1);
        put32(bytes, resourceOffset + 0x30, 1);
        put32(bytes, resourceOffset + 0x34, 0x80000040);
        put16(bytes, resourceOffset + 0x40 + 14, 2);
        put32(bytes, resourceOffset + 0x50, 0x0409);
        put32(bytes, resourceOffset + 0x54, 0x60);
        put32(bytes, resourceOffset + 0x58, 0x0804);
        put32(bytes, resourceOffset + 0x5c, 0x70);
        const auto version = versionInfo(missingFields);
        Q_ASSERT(version.size() <= 0x500);
        put32(bytes, resourceOffset + 0x60, resourceRva + 0x100);
        put32(bytes, resourceOffset + 0x64, quint32(version.size()));
        put32(bytes, resourceOffset + 0x68, 1200);
        put32(bytes, resourceOffset + 0x70, resourceRva + 0x600);
        put32(bytes, resourceOffset + 0x74, quint32(version.size()));
        put32(bytes, resourceOffset + 0x78, 1200);
        putBytes(bytes, versionOffset, version);
        putBytes(bytes, secondVersionOffset, version);
    }
    return bytes;
}
}
#endif
