#include "versioninfo.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSysInfo>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace LqCompare { namespace Version {
namespace {

// All offsets are widened before arithmetic. No PE structure is overlaid on the
// input, so alignment and the host's byte order do not affect the parser.
struct ParseError { Status status; QString message; };

[[noreturn]] void invalid(const QString &message)
{
    throw ParseError { Status::Invalid, message };
}
[[noreturn]] void limited(const QString &message)
{
    throw ParseError { Status::LimitExceeded, message };
}

QString hex(quint64 value, int width = 8)
{
    return QStringLiteral("0x") + QString::number(value, 16).rightJustified(width, QLatin1Char('0')).toUpper();
}

QString versionNumber(quint32 ms, quint32 ls)
{
    return QStringLiteral("%1.%2.%3.%4").arg(ms >> 16).arg(ms & 0xffff)
            .arg(ls >> 16).arg(ls & 0xffff);
}

void validateLimits(const Limits &limits)
{
    if (limits.maxFileBytes < 0 || limits.maxSections < 0 || limits.maxEntries < 0
            || limits.maxStringBytes < 1 || limits.maxResourceDepth < 0)
        limited(QStringLiteral("Invalid parser limits."));
}

class Parser
{
public:
    Parser(const QByteArray &bytes, const Limits &limits) : b(bytes), lim(limits) {}

    FileInfo run()
    {
        validateLimits(lim);
        if (b.size() > lim.maxFileBytes)
            limited(QStringLiteral("File exceeds the configured byte limit."));
        out.metadata.insert(QStringLiteral("Size"), QString::number(b.size()));
        if (b.size() < 2 || b.at(0) != 'M' || b.at(1) != 'Z') {
            out.status = Status::NonPe;
            out.message = QStringLiteral("Not a PE file; no PE version information is available.");
            return out;
        }
        headers();
        imports();
        exports();
        if (dirs[2].size) {
            QSet<quint32> active;
            resourceDirectory(0, 0, false, QString(), QString(), active);
        }
        out.status = Status::Pe;
        out.message = out.versions.isEmpty()
                ? QStringLiteral("PE parsed; no version resource is present.")
                : QStringLiteral("PE metadata parsed read-only.");
        return out;
    }

private:
    struct Directory { quint32 rva = 0, size = 0; };
    struct Block {
        quint64 start = 0, end = 0, value = 0, valueBytes = 0, children = 0;
        quint16 valueLength = 0, type = 0;
        QString key;
    };
    const QByteArray &b;
    const Limits &lim;
    FileInfo out;
    Directory dirs[3];
    quint32 sizeOfHeaders = 0;
    quint64 entryCount = 0, textBytes = 0;

    void range(quint64 offset, quint64 length) const
    {
        if (offset > quint64(b.size()) || length > quint64(b.size()) - offset)
            invalid(QStringLiteral("Truncated PE data at file offset %1.").arg(hex(offset)));
    }
    quint16 u16(quint64 offset) const
    {
        range(offset, 2);
        return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + offset));
    }
    quint32 u32(quint64 offset) const
    {
        range(offset, 4);
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + offset));
    }
    quint64 u64(quint64 offset) const
    {
        range(offset, 8);
        return qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(b.constData() + offset));
    }
    void count(quint64 amount = 1)
    {
        if (amount > quint64(lim.maxEntries) || entryCount > quint64(lim.maxEntries) - amount)
            limited(QStringLiteral("PE entries exceed the configured enumeration limit."));
        entryCount += amount;
    }
    void textCount(quint64 amount)
    {
        // Repeated pointers must not amplify a small input into unbounded text.
        const quint64 budget = quint64(lim.maxEntries) * quint64(lim.maxStringBytes);
        if (amount > budget || textBytes > budget - amount)
            limited(QStringLiteral("PE text exceeds the configured output limit."));
        textBytes += amount;
    }
    quint64 mapped(quint64 rva, quint64 length, quint64 *available = nullptr) const
    {
        if (rva > 0xffffffffULL || length > 0x100000000ULL - rva)
            invalid(QStringLiteral("RVA arithmetic overflows the PE address space."));
        if (rva < sizeOfHeaders) {
            const quint64 avail = quint64(sizeOfHeaders) - rva;
            if (length > avail)
                invalid(QStringLiteral("RVA range crosses the PE headers."));
            range(rva, length);
            if (available) *available = avail;
            return rva;
        }
        for (const Section &s : out.sections) {
            const quint64 extent = std::max(s.virtualSize, s.rawSize);
            if (rva >= s.virtualAddress && rva - s.virtualAddress < extent) {
                const quint64 delta = rva - s.virtualAddress;
                if (delta >= s.rawSize || length > quint64(s.rawSize) - delta)
                    invalid(QStringLiteral("RVA points to data not stored in the file."));
                const quint64 offset = quint64(s.rawOffset) + delta;
                range(offset, length);
                if (available) *available = quint64(s.rawSize) - delta;
                return offset;
            }
        }
        invalid(QStringLiteral("Unmapped RVA %1.").arg(hex(rva)));
    }
    QString ascii(quint64 rva, quint64 endRva = 0x100000000ULL)
    {
        quint64 available = 0;
        const quint64 offset = mapped(rva, 1, &available);
        if (rva >= endRva)
            invalid(QStringLiteral("String is outside its declared directory."));
        available = std::min(available, endRva - rva);
        const quint64 scan = std::min(available, quint64(lim.maxStringBytes));
        for (quint64 i = 0; i < scan; ++i) {
            const uchar c = uchar(b.at(int(offset + i)));
            if (!c) {
                textCount(i);
                return QString::fromLatin1(b.constData() + offset, int(i));
            }
            if (c > 0x7f)
                invalid(QStringLiteral("A PE ASCII symbol contains a non-ASCII byte."));
        }
        if (available >= quint64(lim.maxStringBytes))
            limited(QStringLiteral("PE string exceeds the configured byte limit or is unterminated."));
        invalid(QStringLiteral("Unterminated PE string."));
    }
    QString utf16(quint64 offset, quint64 byteLength)
    {
        if (byteLength > quint64(lim.maxStringBytes))
            limited(QStringLiteral("Version string exceeds the configured byte limit."));
        if (byteLength % 2)
            invalid(QStringLiteral("Odd-sized UTF-16 string."));
        range(offset, byteLength);
        QString result;
        result.reserve(int(byteLength / 2));
        for (quint64 i = 0; i < byteLength; i += 2) {
            const quint16 c = u16(offset + i);
            if (QChar::isHighSurrogate(c)) {
                if (i + 2 >= byteLength || !QChar::isLowSurrogate(u16(offset + i + 2)))
                    invalid(QStringLiteral("Invalid UTF-16 surrogate in a version resource."));
                result.append(QChar(c));
                i += 2;
                result.append(QChar(u16(offset + i)));
            } else if (QChar::isLowSurrogate(c)) {
                invalid(QStringLiteral("Invalid UTF-16 surrogate in a version resource."));
            } else {
                result.append(QChar(c));
            }
        }
        textCount(byteLength);
        return result;
    }
    void headers()
    {
        range(0, 64);
        const quint64 pe = u32(0x3c);
        if (pe < 64)
            invalid(QStringLiteral("PE signature overlaps the DOS header."));
        range(pe, 24);
        if (u32(pe) != 0x00004550)
            invalid(QStringLiteral("MZ file has no valid PE signature."));
        const quint64 coff = pe + 4, opt = pe + 24;
        const quint16 sectionCount = u16(coff + 2), optionalBytes = u16(coff + 16);
        if (sectionCount > lim.maxSections)
            limited(QStringLiteral("Section count exceeds the configured limit."));
        range(opt, optionalBytes);
        if (optionalBytes < 2)
            invalid(QStringLiteral("PE optional header is missing."));
        const quint16 magic = u16(opt);
        if (magic != 0x10b && magic != 0x20b)
            invalid(QStringLiteral("Unsupported PE optional header magic."));
        out.pe32Plus = magic == 0x20b;
        const quint64 directoryOffset = out.pe32Plus ? 112 : 96;
        if (optionalBytes < directoryOffset)
            invalid(QStringLiteral("Truncated PE optional header."));
        const quint32 directoryCount = u32(opt + directoryOffset - 4);
        if (quint64(directoryCount) * 8 > quint64(optionalBytes) - directoryOffset)
            invalid(QStringLiteral("Data directories exceed the optional header."));
        const quint64 table = opt + optionalBytes;
        range(table, quint64(sectionCount) * 40);
        sizeOfHeaders = u32(opt + 60);
        if (sizeOfHeaders < table + quint64(sectionCount) * 40 || sizeOfHeaders > quint64(b.size()))
            invalid(QStringLiteral("Invalid or truncated SizeOfHeaders."));
        auto &h = out.headers;
        h.insert(QStringLiteral("Format"), out.pe32Plus ? QStringLiteral("PE32+") : QStringLiteral("PE32"));
        h.insert(QStringLiteral("Machine"), hex(u16(coff), 4));
        h.insert(QStringLiteral("NumberOfSections"), QString::number(sectionCount));
        h.insert(QStringLiteral("TimeDateStamp"), QString::number(u32(coff + 4)));
        h.insert(QStringLiteral("Characteristics"), hex(u16(coff + 18), 4));
        h.insert(QStringLiteral("LinkerVersion"), QStringLiteral("%1.%2").arg(uchar(b.at(int(opt + 2)))).arg(uchar(b.at(int(opt + 3)))));
        h.insert(QStringLiteral("SizeOfCode"), QString::number(u32(opt + 4)));
        h.insert(QStringLiteral("SizeOfInitializedData"), QString::number(u32(opt + 8)));
        h.insert(QStringLiteral("SizeOfUninitializedData"), QString::number(u32(opt + 12)));
        h.insert(QStringLiteral("AddressOfEntryPoint"), hex(u32(opt + 16)));
        h.insert(QStringLiteral("BaseOfCode"), hex(u32(opt + 20)));
        if (!out.pe32Plus) h.insert(QStringLiteral("BaseOfData"), hex(u32(opt + 24)));
        h.insert(QStringLiteral("ImageBase"), out.pe32Plus ? hex(u64(opt + 24), 16) : hex(u32(opt + 28)));
        h.insert(QStringLiteral("SectionAlignment"), QString::number(u32(opt + 32)));
        h.insert(QStringLiteral("FileAlignment"), QString::number(u32(opt + 36)));
        h.insert(QStringLiteral("OperatingSystemVersion"), QStringLiteral("%1.%2").arg(u16(opt + 40)).arg(u16(opt + 42)));
        h.insert(QStringLiteral("ImageVersion"), QStringLiteral("%1.%2").arg(u16(opt + 44)).arg(u16(opt + 46)));
        h.insert(QStringLiteral("SubsystemVersion"), QStringLiteral("%1.%2").arg(u16(opt + 48)).arg(u16(opt + 50)));
        h.insert(QStringLiteral("SizeOfImage"), QString::number(u32(opt + 56)));
        h.insert(QStringLiteral("SizeOfHeaders"), QString::number(sizeOfHeaders));
        h.insert(QStringLiteral("CheckSum"), hex(u32(opt + 64)));
        h.insert(QStringLiteral("Subsystem"), QString::number(u16(opt + 68)));
        h.insert(QStringLiteral("DllCharacteristics"), hex(u16(opt + 70), 4));
        const QStringList sizes { QStringLiteral("SizeOfStackReserve"), QStringLiteral("SizeOfStackCommit"),
                                  QStringLiteral("SizeOfHeapReserve"), QStringLiteral("SizeOfHeapCommit") };
        for (int i = 0; i < sizes.size(); ++i)
            h.insert(sizes.at(i), QString::number(out.pe32Plus ? u64(opt + 72 + i * 8) : u32(opt + 72 + i * 4)));
        for (quint16 i = 0; i < sectionCount; ++i) {
            const quint64 p = table + quint64(i) * 40;
            Section s;
            const QByteArray name = b.mid(int(p), 8);
            const int zero = name.indexOf('\0');
            s.name = QString::fromLatin1(name.constData(), zero < 0 ? 8 : zero);
            s.virtualSize = u32(p + 8); s.virtualAddress = u32(p + 12);
            s.rawSize = u32(p + 16); s.rawOffset = u32(p + 20);
            s.characteristics = u32(p + 36);
            const quint64 extent = std::max(s.virtualSize, s.rawSize);
            if (extent > 0x100000000ULL - s.virtualAddress)
                invalid(QStringLiteral("Section virtual range overflows."));
            if (extent && s.virtualAddress < sizeOfHeaders)
                invalid(QStringLiteral("Section virtual range overlaps PE headers."));
            if (s.rawSize) {
                range(s.rawOffset, s.rawSize);
                if (s.rawOffset < sizeOfHeaders)
                    invalid(QStringLiteral("Section raw data overlaps PE headers."));
            }
            for (const Section &previous : out.sections) {
                const quint64 oldExtent = std::max(previous.virtualSize, previous.rawSize);
                if (extent && oldExtent && s.virtualAddress < quint64(previous.virtualAddress) + oldExtent
                        && previous.virtualAddress < quint64(s.virtualAddress) + extent)
                    invalid(QStringLiteral("Ambiguous overlapping section RVAs."));
                if (s.rawSize && previous.rawSize && s.rawOffset < quint64(previous.rawOffset) + previous.rawSize
                        && previous.rawOffset < quint64(s.rawOffset) + s.rawSize)
                    invalid(QStringLiteral("Overlapping section file data."));
            }
            out.sections.append(s);
        }
        for (quint32 i = 0; i < std::min(directoryCount, quint32(3)); ++i) {
            dirs[i] = { u32(opt + directoryOffset + i * 8), u32(opt + directoryOffset + i * 8 + 4) };
            if (bool(dirs[i].rva) != bool(dirs[i].size))
                invalid(QStringLiteral("Incomplete PE data directory address/size pair."));
            if (dirs[i].size) mapped(dirs[i].rva, dirs[i].size);
        }
    }
    void imports()
    {
        const Directory d = dirs[1];
        if (!d.size) return;
        const quint64 begin = mapped(d.rva, d.size);
        bool terminated = false;
        for (quint64 p = begin; p + 20 <= begin + d.size; p += 20) {
            const quint32 lookup = u32(p), stamp = u32(p + 4), chain = u32(p + 8);
            const quint32 name = u32(p + 12), iat = u32(p + 16);
            if (!lookup && !stamp && !chain && !name && !iat) { terminated = true; break; }
            count();
            if (!name || !iat)
                invalid(QStringLiteral("Import descriptor has no DLL name or address table."));
            const QString dll = ascii(name);
            if (dll.isEmpty()) invalid(QStringLiteral("Import DLL name is empty."));
            if (!lookup && stamp)
                invalid(QStringLiteral("Bound imports without a lookup table cannot be resolved safely."));
            const quint64 thunkRva = lookup ? lookup : iat;
            const quint64 width = out.pe32Plus ? 8 : 4;
            quint64 available = 0;
            const quint64 thunk = mapped(thunkRva, width, &available);
            bool thunkTerminated = false;
            quint64 importCount = 0;
            for (quint64 n = 0; n + width <= available; n += width) {
                const quint64 value = out.pe32Plus ? u64(thunk + n) : u32(thunk + n);
                // Validate the corresponding IAT storage, including its sentinel;
                // bound IAT values themselves are not interpreted as names.
                mapped(quint64(iat) + n, width);
                if (!value) { thunkTerminated = true; break; }
                count();
                ++importCount;
                Import item; item.dll = dll;
                const quint64 ordinalBit = out.pe32Plus ? 0x8000000000000000ULL : 0x80000000ULL;
                item.byOrdinal = (value & ordinalBit) != 0;
                if (item.byOrdinal) {
                    if (value & ~(ordinalBit | 0xffffULL))
                        invalid(QStringLiteral("Reserved bits are set in an ordinal import."));
                    item.ordinal = quint16(value);
                } else {
                    if (value > 0x7fffffffULL)
                        invalid(QStringLiteral("Import name RVA has reserved high bits set."));
                    item.hint = u16(mapped(value, 2));
                    item.name = ascii(value + 2);
                    if (item.name.isEmpty()) invalid(QStringLiteral("Import symbol name is empty."));
                }
                out.imports.append(item);
            }
            if (!thunkTerminated) invalid(QStringLiteral("Unterminated import lookup table."));
            if (!importCount) {
                // Preserve a real DLL dependency even if its thunk list is empty.
                Import item; item.dll = dll; out.imports.append(item);
            }
        }
        if (!terminated) invalid(QStringLiteral("Unterminated import directory."));
    }
    void exports()
    {
        const Directory d = dirs[0];
        if (!d.size) return;
        if (d.size < 40) invalid(QStringLiteral("Truncated export directory."));
        const quint64 p = mapped(d.rva, 40);
        const quint32 dllName = u32(p + 12), ordinalBase = u32(p + 16);
        const quint32 functions = u32(p + 20), names = u32(p + 24);
        const quint32 addressesRva = u32(p + 28), namesRva = u32(p + 32), ordinalsRva = u32(p + 36);
        if (functions > quint32(lim.maxEntries) || names > quint32(lim.maxEntries))
            limited(QStringLiteral("Export count exceeds the configured limit."));
        if (functions && quint64(ordinalBase) + functions - 1 > 0xffffffffULL)
            invalid(QStringLiteral("Export ordinal range overflows."));
        if (dllName) out.headers.insert(QStringLiteral("ExportDllName"), ascii(dllName));
        if ((functions && !addressesRva) || (names && (!namesRva || !ordinalsRva)))
            invalid(QStringLiteral("Export table pointer is missing."));
        const quint64 addresses = functions ? mapped(addressesRva, quint64(functions) * 4) : 0;
        const quint64 nameTable = names ? mapped(namesRva, quint64(names) * 4) : 0;
        const quint64 ordinalTable = names ? mapped(ordinalsRva, quint64(names) * 2) : 0;
        QMap<quint32, QVector<QString>> namesByIndex;
        QSet<QString> seenNames;
        for (quint32 i = 0; i < names; ++i) {
            count();
            const quint16 index = u16(ordinalTable + quint64(i) * 2);
            if (index >= functions) invalid(QStringLiteral("Export name ordinal is outside the address table."));
            const quint32 nameRva = u32(nameTable + quint64(i) * 4);
            if (!nameRva) invalid(QStringLiteral("Export name RVA is zero."));
            const QString name = ascii(nameRva);
            if (name.isEmpty() || seenNames.contains(name))
                invalid(QStringLiteral("Empty or duplicate export symbol name."));
            seenNames.insert(name);
            namesByIndex[index].append(name);
        }
        for (quint32 i = 0; i < functions; ++i) {
            count();
            const quint32 address = u32(addresses + quint64(i) * 4);
            if (!address) {
                if (namesByIndex.contains(i)) invalid(QStringLiteral("Named export points to an empty address slot."));
                continue; // A zero address is a hole, not an ordinal-only export.
            }
            Export item; item.ordinal = ordinalBase + i; item.address = address;
            if (address >= d.rva && quint64(address) < quint64(d.rva) + d.size) {
                item.forwarder = ascii(address, quint64(d.rva) + d.size);
                if (item.forwarder.isEmpty()) invalid(QStringLiteral("Empty export forwarder."));
            }
            // Export RVAs may designate virtual-only data, or absolute symbols.
            // They are displayed as stored and are never dereferenced as code.
            const auto found = namesByIndex.constFind(i);
            if (found == namesByIndex.cend()) out.exports.append(item);
            else for (const QString &name : found.value()) { item.name = name; out.exports.append(item); }
        }
    }
    quint64 resourceOffset(quint64 relative, quint64 length) const
    {
        if (relative > dirs[2].size || length > quint64(dirs[2].size) - relative)
            invalid(QStringLiteral("Resource-relative offset exceeds the resource directory."));
        return mapped(quint64(dirs[2].rva) + relative, length);
    }
    QString resourceName(quint32 field)
    {
        if (!(field & 0x80000000U)) return QString::number(field);
        const quint64 relative = field & 0x7fffffffU;
        const quint16 chars = u16(resourceOffset(relative, 2));
        return utf16(resourceOffset(relative + 2, quint64(chars) * 2), quint64(chars) * 2);
    }
    void resourceDirectory(quint32 relative, int depth, bool isVersion,
                           const QString &name, const QString &language, QSet<quint32> &active)
    {
        if (active.contains(relative)) invalid(QStringLiteral("Cycle in the PE resource directory."));
        if (depth >= lim.maxResourceDepth || depth >= 64)
            limited(QStringLiteral("Resource directory exceeds the depth limit."));
        count();
        active.insert(relative);
        const quint64 p = resourceOffset(relative, 16);
        const quint32 named = u16(p + 12), total = named + u16(p + 14);
        count(total);
        resourceOffset(quint64(relative) + 16, quint64(total) * 8);
        for (quint32 i = 0; i < total; ++i) {
            const quint64 entry = p + 16 + quint64(i) * 8;
            const quint32 key = u32(entry), child = u32(entry + 4);
            if (bool(key & 0x80000000U) != (i < named))
                invalid(QStringLiteral("Resource name/ID entry count does not match its entries."));
            const QString identifier = resourceName(key);
            const bool version = depth == 0 ? key == 16 : isVersion;
            const QString resource = depth == 1 ? identifier : name;
            const QString lang = depth == 2
                    ? ((key & 0x80000000U) ? identifier : QString::number(key, 16).rightJustified(4, QLatin1Char('0')).toUpper())
                    : language;
            if (child & 0x80000000U) {
                resourceDirectory(child & 0x7fffffffU, depth + 1, version, resource, lang, active);
            } else {
                const quint64 data = resourceOffset(child, 16);
                const quint32 rva = u32(data), size = u32(data + 4);
                if (u32(data + 12)) invalid(QStringLiteral("Resource data reserved field is nonzero."));
                if (size) mapped(rva, size);
                if (version) {
                    if (depth != 2 || !size)
                        invalid(QStringLiteral("Version resource has an invalid language/data hierarchy."));
                    VersionResource value; value.resourceName = resource; value.language = lang;
                    versionResource(mapped(rva, size), size, value);
                    out.versions.append(value);
                }
            }
        }
        active.remove(relative);
    }
    static quint64 align4(quint64 value, quint64 base)
    {
        return base + ((value - base + 3) & ~quint64(3));
    }
    void padding(quint64 begin, quint64 end, bool requireZero = false) const
    {
        if (end < begin || end - begin > 3)
            invalid(QStringLiteral("Invalid version block alignment padding."));
        range(begin, end - begin);
        if (requireZero) {
            for (quint64 p = begin; p < end; ++p)
                if (b.at(int(p))) invalid(QStringLiteral("Nonzero version block alignment padding."));
        }
    }
    Block block(quint64 start, quint64 parentEnd, quint64 base)
    {
        count();
        if (start > parentEnd || parentEnd - start < 6)
            invalid(QStringLiteral("Truncated version block header."));
        Block n; n.start = start; n.end = start + u16(start);
        n.valueLength = u16(start + 2); n.type = u16(start + 4);
        if (n.end > parentEnd || n.end < start + 8 || n.type > 1)
            invalid(QStringLiteral("Invalid version block length or type."));
        quint64 keyEnd = start + 6;
        while (keyEnd + 2 <= n.end && u16(keyEnd)) {
            keyEnd += 2;
            if (keyEnd - (start + 6) >= quint64(lim.maxStringBytes))
                limited(QStringLiteral("Version block key exceeds the string limit."));
        }
        if (keyEnd + 2 > n.end) invalid(QStringLiteral("Unterminated version block key."));
        n.key = utf16(start + 6, keyEnd - (start + 6));
        n.value = align4(keyEnd + 2, base);
        n.valueBytes = quint64(n.valueLength) * (n.type == 1 ? 2 : 1);
        if (n.valueBytes && (n.value > n.end || n.valueBytes > n.end - n.value))
            invalid(QStringLiteral("Version value exceeds its block."));
        if (!n.valueBytes && n.value > n.end) n.value = n.end;
        // The documented Padding/Padding1 fields require zero WORDs. Padding
        // between adjacent child structures has no specified byte value.
        padding(keyEnd + 2, n.value, true);
        n.children = std::min(align4(n.value + n.valueBytes, base), n.end);
        padding(n.value + n.valueBytes, n.children, n.key == QStringLiteral("VS_VERSION_INFO"));
        return n;
    }
    template<typename Function>
    void children(const Block &parent, quint64 base, Function function)
    {
        quint64 p = parent.children;
        while (p < parent.end) {
            if (parent.end - p < 6) {
                // A parent's final alignment pad is permitted, but is not data.
                padding(p, parent.end);
                return;
            }
            const Block child = block(p, parent.end, base);
            function(child);
            p = std::min(align4(child.end, base), parent.end);
            padding(child.end, p);
        }
    }
    void noChildren(const Block &n) const
    {
        for (quint64 p = n.children; p < n.end; ++p)
            if (b.at(int(p))) invalid(QStringLiteral("Unexpected child data in a version value."));
    }
    void fixedInfo(const Block &root, VersionResource &value)
    {
        if (!root.valueBytes) return;
        if (root.type != 0 || root.valueBytes != 52 || u32(root.value) != 0xfeef04bdU)
            invalid(QStringLiteral("Invalid VS_FIXEDFILEINFO signature, type, or length."));
        const quint64 p = root.value;
        auto &f = value.fixed;
        f.insert(QStringLiteral("Signature"), hex(u32(p)));
        f.insert(QStringLiteral("StructVersion"), hex(u32(p + 4)));
        f.insert(QStringLiteral("FileVersion"), versionNumber(u32(p + 8), u32(p + 12)));
        f.insert(QStringLiteral("ProductVersion"), versionNumber(u32(p + 16), u32(p + 20)));
        f.insert(QStringLiteral("FileFlagsMask"), hex(u32(p + 24)));
        f.insert(QStringLiteral("FileFlags"), hex(u32(p + 28)));
        f.insert(QStringLiteral("FileOS"), hex(u32(p + 32)));
        f.insert(QStringLiteral("FileType"), hex(u32(p + 36)));
        f.insert(QStringLiteral("FileSubtype"), hex(u32(p + 40)));
        f.insert(QStringLiteral("FileDate"), hex((quint64(u32(p + 44)) << 32) | u32(p + 48), 16));
    }
    void stringFileInfo(const Block &info, quint64 base, VersionResource &value)
    {
        if (info.valueBytes) invalid(QStringLiteral("StringFileInfo has an unexpected value."));
        children(info, base, [&](const Block &table) {
            if (table.valueBytes || table.key.size() != 8)
                invalid(QStringLiteral("Invalid StringTable language/codepage key."));
            for (const QChar c : table.key) {
                const ushort v = c.unicode();
                if (!((v >= '0' && v <= '9') || (v >= 'a' && v <= 'f') || (v >= 'A' && v <= 'F')))
                    invalid(QStringLiteral("Invalid StringTable language/codepage key."));
            }
            children(table, base, [&](const Block &s) {
                if (s.type != 1 || s.key.isEmpty()) invalid(QStringLiteral("Invalid version String block."));
                QString text;
                if (s.valueBytes) {
                    if (u16(s.value + s.valueBytes - 2)) invalid(QStringLiteral("Unterminated version string value."));
                    text = utf16(s.value, s.valueBytes - 2);
                    if (text.contains(QChar(0))) invalid(QStringLiteral("Embedded terminator in a version string value."));
                }
                noChildren(s);
                const QString key = table.key + QLatin1Char('/') + s.key;
                if (value.strings.contains(key)) invalid(QStringLiteral("Duplicate version StringTable key."));
                value.strings.insert(key, text);
            });
        });
    }
    void varFileInfo(const Block &info, quint64 base, VersionResource &value)
    {
        if (info.valueBytes) invalid(QStringLiteral("VarFileInfo has an unexpected value."));
        children(info, base, [&](const Block &var) {
            if (var.key != QStringLiteral("Translation") || var.type != 0 || var.valueBytes % 4)
                invalid(QStringLiteral("Invalid version Translation block."));
            count(var.valueBytes / 4);
            for (quint64 p = var.value; p < var.value + var.valueBytes; p += 4)
                value.translations.append(u32(p));
            noChildren(var);
        });
    }
    void versionResource(quint64 base, quint64 size, VersionResource &value)
    {
        const Block root = block(base, base + size, base);
        if (root.key != QStringLiteral("VS_VERSION_INFO") || root.type != 0)
            invalid(QStringLiteral("Invalid VS_VERSION_INFO root."));
        // The resource may include only DWORD alignment after the root block.
        if (base + size > align4(root.end, base))
            invalid(QStringLiteral("Unexpected data after VS_VERSION_INFO."));
        padding(root.end, base + size);
        fixedInfo(root, value);
        children(root, base, [&](const Block &info) {
            if (info.key == QStringLiteral("StringFileInfo")) stringFileInfo(info, base, value);
            else if (info.key == QStringLiteral("VarFileInfo")) varFileInfo(info, base, value);
            else invalid(QStringLiteral("Unsupported child of VS_VERSION_INFO: %1.").arg(info.key));
        });
    }
};

QMap<QString, QString> metadata(const QFileInfo &fi)
{
    QMap<QString, QString> result;
    result.insert(QStringLiteral("FileName"), fi.fileName());
    result.insert(QStringLiteral("Size"), QString::number(fi.size()));
    const auto addTime = [&](const QString &key, const QDateTime &time) {
        result.insert(key, time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("Unavailable"));
    };
    addTime(QStringLiteral("ModifiedUtc"), fi.lastModified());
    addTime(QStringLiteral("CreatedUtc"), fi.birthTime());
    addTime(QStringLiteral("AccessedUtc"), fi.lastRead());
    result.insert(QStringLiteral("Owner"), fi.owner().isEmpty() ? QStringLiteral("Unavailable") : fi.owner());
    result.insert(QStringLiteral("Group"), fi.group().isEmpty() ? QStringLiteral("Unavailable") : fi.group());
    result.insert(QStringLiteral("Permissions"), hex(quint32(fi.permissions()), 4));
    result.insert(QStringLiteral("HostPlatform"), QSysInfo::prettyProductName());
    return result;
}

} // namespace

FileInfo parse(const QByteArray &bytes, const Limits &limits)
{
    try {
        return Parser(bytes, limits).run();
    } catch (const ParseError &failure) {
        // Never return partially populated PE information after any failure.
        FileInfo result;
        result.status = failure.status;
        result.message = failure.message;
        result.metadata.insert(QStringLiteral("Size"), QString::number(bytes.size()));
        return result;
    }
}

FileInfo inspectFile(const QString &path, const Limits &limits)
{
    FileInfo result;
    const QFileInfo before(path);
    result.path = before.absoluteFilePath();
    result.metadata = metadata(before);
    const auto failure = [&](Status status, const QString &message) {
        result.status = status;
        result.message = message;
        return result;
    };
    try { validateLimits(limits); }
    catch (const ParseError &e) { return failure(e.status, e.message); }
    if (path.isEmpty() || !before.exists() || !before.isFile())
        return failure(Status::IoError, QStringLiteral("Path is not an existing regular file."));
    if (before.size() > limits.maxFileBytes || before.size() > std::numeric_limits<int>::max() - 1)
        return failure(Status::LimitExceeded, QStringLiteral("File exceeds the configured byte limit."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(Status::IoError, file.errorString());
    if (file.isSequential()) return failure(Status::IoError, QStringLiteral("Sequential devices are not supported."));
    const qint64 expected = file.size();
    if (expected < 0 || expected > limits.maxFileBytes || expected > std::numeric_limits<int>::max() - 1)
        return failure(Status::LimitExceeded, QStringLiteral("File exceeds the configured byte limit."));
    // One extra byte detects growth without reading an unbounded stream.
    const QByteArray bytes = file.read(expected + 1);
    if (file.error() != QFileDevice::NoError) return failure(Status::IoError, file.errorString());
    const QFileInfo after(path);
    if (bytes.size() != expected || file.size() != expected || before.size() != expected
            || !after.isFile() || after.size() != expected || before.lastModified() != after.lastModified())
        return failure(Status::IoError, QStringLiteral("File changed while it was being inspected; retry with a stable file."));
    FileInfo parsed = parse(bytes, limits);
    parsed.path = result.path;
    parsed.metadata = result.metadata;
    return parsed;
}

} } // namespace LqCompare::Version
