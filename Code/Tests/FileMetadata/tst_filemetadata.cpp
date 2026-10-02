// 旧 MinGW 需要在 Qt / CRT 头之前声明可用的符号链接 API 版本。
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#  define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "filesystem.h"

#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>
#ifdef Q_OS_WIN
#  include <windows.h>
#endif

using namespace LqCompare::Files;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// 字段名称采用 ASCII，避免 CI 控制台代码页丢掉具体失配证据。
QString metadata(const FileInfo &info)
{
    return QStringLiteral("exists=%1 directory=%2 link=%3 size=%4 modified.valid=%5 modified.ns=%6 attributes=%7")
        .arg(info.exists).arg(info.isDirectory).arg(info.isSymLink).arg(info.size)
        .arg(info.lastModified.isValid()).arg(info.lastModified.nanosecondsSinceEpoch())
        .arg(quint32(info.attributes));
}

bool sameMetadata(const FileInfo &before, const FileInfo &after)
{
    return before.exists == after.exists && before.isDirectory == after.isDirectory
        && before.isSymLink == after.isSymLink && before.size == after.size
        && before.lastModified == after.lastModified && before.attributes == after.attributes;
}

#ifdef Q_OS_WIN
class Handle
{
public:
    explicit Handle(HANDLE value) : value(value) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    HANDLE value;
    Q_DISABLE_COPY(Handle)
};
LPCWSTR wide(const QString &path) { return reinterpret_cast<LPCWSTR>(path.utf16()); }
#endif
}

class FileMetadataTests : public QObject
{
    Q_OBJECT
private slots:
    void stableDuringReads_data();
    void stableDuringReads();
    void openWriterMetadataIsCurrent_data();
    void openWriterMetadataIsCurrent();
    void volumeRootIsDirectory();
    void errorsResetOnlyOnSuccess();
    void directoryAttributesMatchNativeFlags();
    void enumerationTimingAndNames();
    void statNeverFollowsSymbolicLinks_data();
    void statNeverFollowsSymbolicLinks();
};

void FileMetadataTests::stableDuringReads_data()
{
    QTest::addColumn<QString>("relativePath");
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QStringLiteral("empty.dat") << QByteArray();
    QTest::newRow("file") << QStringLiteral("file.txt") << QByteArray("contents");
    QTest::newRow("binary") << QStringLiteral("binary.dat") << QByteArray("a\0b\xff", 4);
    QTest::newRow("nested") << QStringLiteral("nested/deeper/中文.txt") << QByteArray("nested contents");
}

void FileMetadataTests::stableDuringReads()
{
    QFETCH(QString, relativePath);
    QFETCH(QByteArray, bytes);
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    // 每轮使用新目录，不能通过预读、延时或重试掩盖首次查询的缓存问题。
    for (int iteration = 0; iteration != 8; ++iteration) {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeFile(root.filePath(relativePath), bytes));
        const QString directoryPath = QFileInfo(root.filePath(relativePath)).absolutePath();
        ErrorCode error;
        const auto rootBefore = fs->stat(root.path(), &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        const auto directoryBefore = fs->stat(directoryPath, &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        const auto entries = fs->enumerateDirectory(directoryPath, &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        QCOMPARE(entries.size(), 1);
        QFile file(root.filePath(relativePath));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        file.close();
        const auto fileAfter = fs->stat(root.filePath(relativePath), &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        const auto directoryAfter = fs->stat(directoryPath, &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        const auto rootAfter = fs->stat(root.path(), &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        QVERIFY2(sameMetadata(rootBefore, rootAfter),
                 qPrintable(QStringLiteral("root before: %1; after: %2").arg(metadata(rootBefore), metadata(rootAfter))));
        QVERIFY2(sameMetadata(directoryBefore, directoryAfter),
                 qPrintable(QStringLiteral("directory before: %1; after: %2").arg(metadata(directoryBefore), metadata(directoryAfter))));
        QVERIFY2(sameMetadata(entries.first(), fileAfter),
                 qPrintable(QStringLiteral("enumerated: %1; stat: %2").arg(metadata(entries.first()), metadata(fileAfter))));
    }
}

void FileMetadataTests::openWriterMetadataIsCurrent_data()
{
    QTest::addColumn<bool>("directory");
    QTest::addColumn<bool>("enumeration");
    QTest::newRow("file-stat") << false << false;
    QTest::newRow("directory-stat") << true << false;
    QTest::newRow("file-enumeration") << false << true;
    QTest::newRow("directory-enumeration") << true << true;
}

void FileMetadataTests::openWriterMetadataIsCurrent()
{
#ifndef Q_OS_WIN
    QSKIP("Native Win32 handle metadata contract.");
#else
    QFETCH(bool, directory);
    QFETCH(bool, enumeration);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath(QStringLiteral("entry"));
    QVERIFY(directory ? QDir().mkdir(path) : writeFile(path, "metadata"));
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    const QString nativePath = fs->toNativePath(path);
    Handle writer(::CreateFileW(wide(nativePath), FILE_WRITE_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    QVERIFY2(writer.value != INVALID_HANDLE_VALUE, qPrintable(QString::number(::GetLastError())));
    // 独立黄金值：Unix 1700000000123456700 ns 对应 Windows 133444736001234567 ticks。
    const quint64 ticks = Q_UINT64_C(133444736001234567);
    FILETIME modified{DWORD(ticks & 0xffffffff), DWORD(ticks >> 32)};
    QVERIFY(::SetFileTime(writer.value, nullptr, nullptr, &modified));
    BY_HANDLE_FILE_INFORMATION current{};
    QVERIFY(::GetFileInformationByHandle(writer.value, &current));
    QCOMPARE(current.ftLastWriteTime.dwLowDateTime, modified.dwLowDateTime);
    QCOMPARE(current.ftLastWriteTime.dwHighDateTime, modified.dwHighDateTime);
    // 写句柄仍打开：stat 不能用父目录的旧缓存冒充当前条目元数据。
    ErrorCode error;
    FileInfo info;
    if (enumeration) {
        const auto entries = fs->enumerateDirectory(root.path(), &error);
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        QCOMPARE(entries.size(), 1);
        info = entries.first();
    } else {
        info = fs->stat(path, &error);
    }
    QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
    QVERIFY2(info.exists, qPrintable(metadata(info)));
    QCOMPARE(info.isDirectory, directory);
    QVERIFY2(info.lastModified.isValid(), qPrintable(metadata(info)));
    QCOMPARE(info.lastModified.nanosecondsSinceEpoch(), Q_INT64_C(1700000000123456700));
#endif
}

void FileMetadataTests::volumeRootIsDirectory()
{
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    ErrorCode error = FileSystemError::PermissionDenied;
    const auto info = fs->stat(QDir::rootPath(), &error);
    QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
    QVERIFY2(info.exists && info.isDirectory && !info.isSymLink, qPrintable(metadata(info)));
    error = FileSystemError::PermissionDenied;
    QVERIFY2(fs->exists(QDir::rootPath(), &error), qPrintable(errorDetail(error)));
    QVERIFY(error.ok());
    QVERIFY(!error.hasRawCode());
    QVERIFY(fs->exists(QDir::rootPath(), nullptr));
}

void FileMetadataTests::errorsResetOnlyOnSuccess()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    const QString missing = root.filePath(QStringLiteral("missing"));
    ErrorCode error;
    QVERIFY(!fs->stat(missing, &error).exists);
    QCOMPARE(error.category, FileSystemError::NotFound);
    QVERIFY(error.hasRawCode());
    const ErrorCode statError = error;
    QVERIFY(!fs->exists(missing, &error));
    QCOMPARE(error.category, statError.category);
    QCOMPARE(error.domain, statError.domain);
    QCOMPARE(error.raw, statError.raw);
#ifdef Q_OS_WIN
    const QString nativePath = fs->toNativePath(missing);
    Handle handle(::CreateFileW(wide(nativePath), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    const DWORD nativeCode = ::GetLastError();
    QVERIFY(handle.value == INVALID_HANDLE_VALUE);
    QCOMPARE(error.domain, ErrorDomain::Win32);
    QCOMPARE(error.raw, qint64(nativeCode));
#endif
    QVERIFY(fs->stat(root.path(), &error).exists);
    QVERIFY(error.ok());
    QVERIFY(!error.hasRawCode());
    QVERIFY(!fs->stat(missing, nullptr).exists);
    QVERIFY(fs->stat(root.path(), nullptr).exists);
    error = statError;
    QVERIFY(fs->exists(root.path(), &error));
    QVERIFY(error.ok());
    QVERIFY(!error.hasRawCode());
    QVERIFY(!fs->exists(missing, nullptr));
    QVERIFY(fs->exists(root.path(), nullptr));
}

void FileMetadataTests::directoryAttributesMatchNativeFlags()
{
#ifndef Q_OS_WIN
    QSKIP("Native Win32 directory attribute contract.");
#else
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath(QStringLiteral("directory"));
    QVERIFY(QDir().mkdir(path));
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    const QString nativePath = fs->toNativePath(path);
    const DWORD original = ::GetFileAttributesW(wide(nativePath));
    QVERIFY(original != INVALID_FILE_ATTRIBUTES);
    QVERIFY(::SetFileAttributesW(wide(nativePath), original | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN));
    ErrorCode error;
    const auto info = fs->stat(path, &error);
    // 先还原真实夹具再断言，失败也不留下只读/隐藏临时目录。
    const BOOL restored = ::SetFileAttributesW(wide(nativePath), original);
    QVERIFY(restored);
    QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
    QVERIFY(info.exists && info.isDirectory && !info.isSymLink);
    QVERIFY(info.isReadOnly());
    QVERIFY(info.attributes.testFlag(FileAttribute::Hidden));
#endif
}

void FileMetadataTests::enumerationTimingAndNames()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QSet<QString> expected;
    for (int index = 0; index != 1000; ++index) {
        const QString name = QStringLiteral("Entry-%1-中文.txt").arg(index, 4, 10, QLatin1Char('0'));
        QVERIFY(writeFile(root.filePath(name), "x"));
        expected.insert(name);
    }
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    for (int iteration = 0; iteration != 3; ++iteration) {
        ErrorCode error;
        QElapsedTimer timer;
        timer.start();
        const auto entries = fs->enumerateDirectory(root.path(), &error);
        const qint64 elapsedNanoseconds = timer.nsecsElapsed();
        QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
        QCOMPARE(entries.size(), 1000);
        QSet<QString> actual;
        for (const auto &entry : entries) {
            QVERIFY(entry.exists && !entry.isDirectory && !entry.isSymLink);
            QCOMPARE(entry.size, quint64(1));
            actual.insert(entry.name);
        }
        QCOMPARE(actual, expected);
        // 只记录真实平台耗时，不设依赖 CI 负载的抖动阈值。
        qInfo().nospace() << "metadata-enumeration entries=" << entries.size()
                          << " iteration=" << iteration << " elapsed.ns=" << elapsedNanoseconds
                          << " platform=" << fs->platformName();
    }
}

void FileMetadataTests::statNeverFollowsSymbolicLinks_data()
{
    QTest::addColumn<bool>("directory");
    QTest::addColumn<bool>("dangling");
    QTest::newRow("file-link") << false << false;
    QTest::newRow("directory-link") << true << false;
    QTest::newRow("dangling-file-link") << false << true;
    QTest::newRow("dangling-directory-link") << true << true;
}

void FileMetadataTests::statNeverFollowsSymbolicLinks()
{
#ifndef Q_OS_WIN
    QSKIP("Native Win32 reparse-point metadata contract.");
#else
    QFETCH(bool, directory);
    QFETCH(bool, dangling);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath(QStringLiteral("link"));
    const QString target = root.filePath(QStringLiteral("target"));
    if (!dangling) QVERIFY(directory ? QDir().mkdir(target) : writeFile(target, "target contents"));
    const QString nativePath = QDir::toNativeSeparators(path);
    const QString nativeTarget = QDir::toNativeSeparators(target);
    const DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    // 0x2 允许启用开发者模式的系统不请求管理员权限。
    BOOLEAN created = ::CreateSymbolicLinkW(wide(nativePath), wide(nativeTarget), flags | 0x2);
    DWORD code = created ? ERROR_SUCCESS : ::GetLastError();
    if (!created && code == ERROR_INVALID_PARAMETER) {
        created = ::CreateSymbolicLinkW(wide(nativePath), wide(nativeTarget), flags);
        code = created ? ERROR_SUCCESS : ::GetLastError();
    }
    if (!created && code == ERROR_PRIVILEGE_NOT_HELD) QSKIP("Windows runner lacks symbolic-link privilege.");
    QVERIFY2(created, qPrintable(QString::number(code)));
    std::unique_ptr<FileSystem> fs(createNativeFileSystem());
    ErrorCode error;
    const auto info = fs->stat(path, &error);
    QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
    QVERIFY2(info.exists && info.isSymLink, qPrintable(metadata(info)));
    QVERIFY(info.attributes.testFlag(FileAttribute::SymLink));
    QCOMPARE(info.isDirectory, directory);
    // 即使目标不存在，exists 也必须保留链接本身的存在性，并清除旧错误。
    error = FileSystemError::PermissionDenied;
    QVERIFY2(fs->exists(path, &error), qPrintable(errorDetail(error)));
    QVERIFY(error.ok());
    QVERIFY(!error.hasRawCode());
    QVERIFY(fs->exists(path, nullptr));
    const auto children = fs->enumerateDirectory(root.path(), &error);
    QVERIFY2(error.ok(), qPrintable(errorDetail(error)));
    const auto found = std::find_if(children.cbegin(), children.cend(), [](const FileInfo &entry) {
        return entry.name == QLatin1String("link");
    });
    QVERIFY(found != children.cend());
    QVERIFY2(sameMetadata(*found, info), qPrintable(metadata(*found)));
#endif
}

QTEST_GUILESS_MAIN(FileMetadataTests)
#include "tst_filemetadata.moc"
