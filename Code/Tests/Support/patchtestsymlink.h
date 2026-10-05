#ifndef LQCOMPARE_PATCHTESTSYMLINK_H
#define LQCOMPARE_PATCHTESTSYMLINK_H

// 必须早于 Qt 头文件：旧版 MinGW 默认目标不会声明 Vista 的符号链接 API。
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace PatchTest {

struct SymlinkResult {
    bool created = false;
    bool privilegeUnavailable = false;
    QString error;
};

inline SymlinkResult createSymlink(const QString &source, const QString &link, bool directory)
{
#ifdef Q_OS_WIN
    const QString nativeSource = QDir::toNativeSeparators(source);
    const QString nativeLink = QDir::toNativeSeparators(link);
    const DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    // 开发者模式无需管理员权限；旧系统只在不认识该标志时回退。
    if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),
                            reinterpret_cast<LPCWSTR>(nativeSource.utf16()), flags | 0x2))
        return {true, false, {}};
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) {
        if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),
                                reinterpret_cast<LPCWSTR>(nativeSource.utf16()), flags))
            return {true, false, {}};
        error = GetLastError();
    }
    // 只有明确缺失符号链接权限才允许跳过；路径错误、拒绝访问等必须失败。
    return {false, error == ERROR_PRIVILEGE_NOT_HELD,
            QStringLiteral("CreateSymbolicLinkW failed (%1): %2 -> %3")
                .arg(error).arg(link, source)};
#else
    Q_UNUSED(directory)
    if (::symlink(QFile::encodeName(source).constData(), QFile::encodeName(link).constData()) == 0)
        return {true, false, {}};
    return {false, false, QStringLiteral("symlink failed (%1): %2 -> %3").arg(errno).arg(link, source)};
#endif
}

} // namespace PatchTest

// QSKIP 必须退出当前测试槽，不能藏在只会返回辅助函数的普通函数里。
#define LQCOMPARE_REQUIRE_PATCH_SYMLINK(source, link, directory) \
    do { \
        const auto symlinkResult = PatchTest::createSymlink(source, link, directory); \
        if (symlinkResult.privilegeUnavailable) QSKIP(qPrintable(symlinkResult.error)); \
        QVERIFY2(symlinkResult.created, qPrintable(symlinkResult.error)); \
        QVERIFY2(QFileInfo(link).isSymbolicLink(), "Fixture must be a native symbolic link"); \
    } while (false)

#endif
