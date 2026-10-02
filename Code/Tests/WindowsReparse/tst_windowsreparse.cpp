#include "windowsreparse.h"

// Qt 5.15.2 的 QtEndian 间接使用 numeric_limits，却未自行包含该标准头。
#include <limits>

#include <QtEndian>
#include <QtTest>

using namespace LqCompare::Files;

namespace {

void write16(QByteArray& bytes, int offset, quint16 value) {
    qToLittleEndian<quint16>(value, bytes.data() + offset);
}

void write32(QByteArray& bytes, int offset, quint32 value) {
    qToLittleEndian<quint32>(value, bytes.data() + offset);
}

QByteArray utf16Bytes(const QString& text) {
    QByteArray bytes(text.size() * 2, '\0');
    for (int index = 0; index < text.size(); ++index)
        write16(bytes, index * 2, text.at(index).unicode());
    return bytes;
}

// 人工构造公开线格式，避免依赖 Win32 结构、主机字节序或真实链接权限。
QByteArray reparseBuffer(quint32 tag, const QString& target, const QString& printName = {},
                         bool printFirst = false, quint32 flags = 0) {
    const int fieldsSize = tag == WindowsReparse::SymbolicLinkTag ? 12 : 8;
    const QByteArray substitute = utf16Bytes(target);
    const QByteArray print = utf16Bytes(printName);
    QByteArray bytes(8 + fieldsSize, '\0');
    write32(bytes, 0, tag);
    write16(bytes, 4, fieldsSize + substitute.size() + print.size());
    write16(bytes, 8, printFirst ? print.size() : 0);
    write16(bytes, 10, substitute.size());
    write16(bytes, 12, printFirst ? 0 : substitute.size());
    write16(bytes, 14, print.size());
    if (fieldsSize == 12)
        write32(bytes, 16, flags);
    bytes += printFirst ? print + substitute : substitute + print;
    return bytes;
}

} // namespace

class TstWindowsReparse : public QObject {
    Q_OBJECT

private slots:
    void targetPreservesStoredUtf16_data();
    void targetPreservesStoredUtf16();
    void rejectsTruncatedReturn_data();
    void rejectsTruncatedReturn();
    void rejectsMalformedBuffer_data();
    void rejectsMalformedBuffer();
    void ignoresBytesOutsideDeclaredPayload();
    void acceptsMaximumBufferSize_data();
    void acceptsMaximumBufferSize();
    void readsUnalignedStorage();
};

void TstWindowsReparse::targetPreservesStoredUtf16_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("expected");

    // 两段独立十六进制夹具固定公开字段布局，不依赖上面的构造器。
    QTest::newRow("fixed-symlink-layout")
        << QByteArray::fromHex("0c0000a00e0000000000020002000000010000007800")
        << QStringLiteral("x");
    QTest::newRow("fixed-junction-layout")
        << QByteArray::fromHex("030000a00a00000000000200020000007800") << QStringLiteral("x");

    const QString absolute = QStringLiteral("\\??\\C:\\目录\\file.txt");
    const QString relative = QStringLiteral("..\\目录\\不存在.txt");
    const QString unc = QStringLiteral("\\??\\UNC\\server\\share\\target");
    const QString supplementary = QString::fromUtf8("目录/🧪/文件.txt");
    for (const quint32 tag : {WindowsReparse::SymbolicLinkTag, WindowsReparse::MountPointTag}) {
        const QByteArray prefix = tag == WindowsReparse::SymbolicLinkTag ? "symlink-" : "junction-";
        QTest::newRow((prefix + "absolute").constData())
            << reparseBuffer(tag, absolute) << absolute;
        QTest::newRow((prefix + "print-name-first").constData())
            << reparseBuffer(tag, absolute, QStringLiteral("display name"), true) << absolute;
        QTest::newRow((prefix + "unc").constData()) << reparseBuffer(tag, unc) << unc;
        QTest::newRow((prefix + "unicode").constData())
            << reparseBuffer(tag, supplementary) << supplementary;
        QTest::newRow((prefix + "one-code-unit-without-null").constData())
            << reparseBuffer(tag, QStringLiteral("x")) << QStringLiteral("x");

        QByteArray terminated = reparseBuffer(tag, absolute);
        terminated.append(2, '\0');
        write16(terminated, 4, terminated.size() - 8);
        QTest::newRow((prefix + "terminator-outside-name-length").constData())
            << terminated << absolute;
    }
    QTest::newRow("relative-symlink")
        << reparseBuffer(WindowsReparse::SymbolicLinkTag, relative, {}, false, 1) << relative;

    // Windows 文件名的 UTF-16 码元不能经 UTF-8 转换丢失，包含未配对代理项的情况。
    const QString unpaired = QStringLiteral("target-") + QChar(0xd800);
    QTest::newRow("preserve-unpaired-utf16")
        << reparseBuffer(WindowsReparse::SymbolicLinkTag, unpaired) << unpaired;
}

void TstWindowsReparse::targetPreservesStoredUtf16() {
    QFETCH(QByteArray, bytes);
    QFETCH(QString, expected);
    QCOMPARE(WindowsReparse::target(bytes, bytes.size()), expected);
}

void TstWindowsReparse::rejectsTruncatedReturn_data() {
    QTest::addColumn<quint32>("tag");
    QTest::newRow("symlink") << WindowsReparse::SymbolicLinkTag;
    QTest::newRow("junction") << WindowsReparse::MountPointTag;
}

void TstWindowsReparse::rejectsTruncatedReturn() {
    QFETCH(quint32, tag);
    const QString target = QStringLiteral("..\\target");
    const QByteArray complete = reparseBuffer(tag, target);
    for (int size = 0; size < complete.size(); ++size) {
        // 容量里其实有完整目标，也不能越过系统声称写入的边界。
        QVERIFY2(WindowsReparse::target(complete, size).isEmpty(),
                 qPrintable(QString::number(size)));
        QVERIFY(WindowsReparse::target(complete.left(size), size).isEmpty());
    }
    QCOMPARE(WindowsReparse::target(complete, complete.size()), target);
}

void TstWindowsReparse::rejectsMalformedBuffer_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<quint32>("returnedSize");

    auto add = [](const QByteArray& name, const QByteArray& bytes) {
        QTest::newRow(name.constData()) << bytes << quint32(bytes.size());
    };
    add("empty", QByteArray());
    add("unknown-tag", reparseBuffer(0x80000099, QStringLiteral("target")));
    for (const quint32 tag : {WindowsReparse::SymbolicLinkTag, WindowsReparse::MountPointTag}) {
        const QByteArray prefix = tag == WindowsReparse::SymbolicLinkTag ? "symlink-" : "junction-";
        const QByteArray valid = reparseBuffer(tag, QStringLiteral("target"));
        const int fieldsSize = tag == WindowsReparse::SymbolicLinkTag ? 12 : 8;
        auto changed16 = [&](const char* name, int offset, quint16 value) {
            QByteArray bytes = valid;
            write16(bytes, offset, value);
            add(prefix + name, bytes);
        };
        changed16("missing-fields", 4, fieldsSize - 1);
        changed16("payload-beyond-return", 4, valid.size() - 7);
        changed16("largest-payload", 4, 0xffff);
        changed16("odd-path-byte-count", 4, valid.size() - 9);
        changed16("name-beyond-declared-payload", 4, fieldsSize + 2);
        changed16("empty-target", 10, 0);
        // 名称长度中的 U+0000 会让 Win32 实际路径与界面显示不一致。
        changed16("null-at-target-start", 8 + fieldsSize, 0);
        changed16("null-in-target-middle", 8 + fieldsSize + 6, 0);
        changed16("null-at-target-end", 8 + fieldsSize + 10, 0);
        changed16("odd-substitute-offset", 8, 1);
        changed16("odd-substitute-length", 10, 3);
        changed16("substitute-offset-past-end", 8, 14);
        changed16("substitute-offset-at-end", 8, 12);
        changed16("substitute-length-past-end", 10, 14);
        changed16("largest-even-substitute-offset", 8, 0xfffe);
        changed16("largest-even-substitute-length", 10, 0xfffe);
        changed16("odd-print-offset", 12, 1);
        changed16("odd-print-length", 14, 1);
        changed16("empty-print-past-end", 12, 14);
        changed16("print-length-past-end", 14, 2);
        changed16("largest-even-print-offset", 12, 0xfffe);
        changed16("largest-even-print-length", 14, 0xfffe);
        QTest::newRow((prefix + "returned-past-capacity").constData())
            << valid << quint32(valid.size() + 1);
        QTest::newRow((prefix + "largest-returned-count").constData())
            << valid << std::numeric_limits<quint32>::max();
    }
    QByteArray oversized = reparseBuffer(WindowsReparse::SymbolicLinkTag, QStringLiteral("target"));
    oversized.resize(WindowsReparse::MaximumBufferSize + 2);
    add("returned-past-windows-limit", oversized);
}

void TstWindowsReparse::rejectsMalformedBuffer() {
    QFETCH(QByteArray, bytes);
    QFETCH(quint32, returnedSize);
    QVERIFY(WindowsReparse::target(bytes, returnedSize).isEmpty());
}

void TstWindowsReparse::ignoresBytesOutsideDeclaredPayload() {
    const QString expected = QStringLiteral("target");
    QByteArray bytes = reparseBuffer(WindowsReparse::SymbolicLinkTag, expected);
    const quint32 returnedSize = bytes.size();
    bytes += QByteArray(50, '\xff');
    QCOMPARE(WindowsReparse::target(bytes, returnedSize), expected);
    // 即便系统多返回尾部字节，也不能把它们当作目标的一部分。
    QCOMPARE(WindowsReparse::target(bytes, bytes.size()), expected);
}

void TstWindowsReparse::acceptsMaximumBufferSize_data() {
    rejectsTruncatedReturn_data();
}

void TstWindowsReparse::acceptsMaximumBufferSize() {
    QFETCH(quint32, tag);
    const int fieldsSize = tag == WindowsReparse::SymbolicLinkTag ? 12 : 8;
    const QString expected((WindowsReparse::MaximumBufferSize - 8 - fieldsSize) / 2, QChar(0x4e2d));
    const QByteArray bytes = reparseBuffer(tag, expected);
    QCOMPARE(bytes.size(), WindowsReparse::MaximumBufferSize);
    QCOMPARE(WindowsReparse::target(bytes, bytes.size()), expected);
}

void TstWindowsReparse::readsUnalignedStorage() {
    const QString expected = QStringLiteral("中文路径");
    const QByteArray bytes = reparseBuffer(WindowsReparse::SymbolicLinkTag, expected);
    const QByteArray unaligned = QByteArray(1, '\0') + bytes;
    const QByteArray view = QByteArray::fromRawData(unaligned.constData() + 1, bytes.size());
    QCOMPARE(WindowsReparse::target(view, view.size()), expected);
}

QTEST_APPLESS_MAIN(TstWindowsReparse)
#include "tst_windowsreparse.moc"
