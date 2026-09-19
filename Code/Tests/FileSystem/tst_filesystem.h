#ifndef LQCOMPARE_TST_FILESYSTEM_H
#define LQCOMPARE_TST_FILESYSTEM_H

#include <QObject>
#include <QtTest>

///
/// \brief 文件系统服务抽象层的测试（PRD: PLAT-002、PLAT-008）。
///
/// 分四组：
///   1. 路径规则——重点是 Windows 的盘符 / UNC / 长路径前缀，
///      这些用例在 macOS 上也会真实执行（见 FileSystemTests.pro 的说明）。
///   2. 时间戳——重点是时区正确性与「拿不到创建时间」的处理。
///   3. 错误分类——重点是 Windows 的「被占用」不能被误判成「权限不足」。
///   4. 异常路径——用内存文件系统注入故障，覆盖真实文件系统上做不到的场景。
///
class TstFileSystem : public QObject
{
    Q_OBJECT

private slots:
    // --- 路径规则（平台无关，含 Windows 规则）------------------------------
    void normalizeCollapsesRedundantSeparators();
    void normalizeResolvesDotAndDotDot();
    void normalizeKeepsRelativeDotDot();
    void normalizeDoesNotEscapeRoot();
    void normalizeHandlesWindowsSeparators();
    void normalizeHandlesWindowsDriveRoot();
    void normalizeHandlesUncPath();
    void normalizeUncRootKeepsTrailingSeparator();
    void unifySeparatorsDoesNotCollapse();

    void splitDropsEmptySegments();
    void joinAvoidsDoubleSeparator();
    void joinKeepsDriveRelativePrefix();

    void fileNameAndParentPath();
    void parentPathStopsAtRoot();

    void isAbsoluteDistinguishesDriveRelative();
    void isUncAndUncPrefix();
    void drivePrefixOnlyForWindowsStyle();
    void isRootPathRecognisesAllRootForms();

    void toExtendedPathAddsPrefixOnlyForLongAbsoluteWindowsPaths();
    void toExtendedPathHandlesUncForm();
    void toExtendedPathIsIdempotent();

    void comparePathsHonoursCaseSensitivity();

    void findInvalidFileNameCharacterReportsPosition();
    void isValidFileNameRejectsReservedNames();

    // --- 时间戳 -----------------------------------------------------------
    void fileTimeRoundTripsThroughUtc();
    void fileTimeTreatsLocalAndUtcInputEqually();
    void fileTimeInvalidIsNotEpoch();
    void fileTimeComparisonOperators();
    void fileTimeKeepsNanosecondPrecision();

    // --- 错误分类 ---------------------------------------------------------
    void classifyPosixErrors();
    void classifyWindowsAccessDeniedIsNotBusy();
    void classifyWindowsErrorCodes();
    void errorIdentifierIsStable();
    void adviceIsDistinctForActionableErrors();
    void onlyBusyIsRetryable();

    // --- 异常路径（内存文件系统注入故障）----------------------------------
    void fakeStatReportsInjectedError();
    void fakeStatOnMissingPathIsNotFound();
    void fakeEnumerateInjectedBusyStopsEnumeration();
    void fakeEnumerateOnFileIsNotDirectory();
    void fakeExistsDistinguishesMissingFromDenied();
    void fakeSetTimesHonoursInjectedReadOnly();
    void fakeSetTimesLeavesUnspecifiedTimestampAlone();
    void fakeChecksCaseInsensitivityUnderWindowsSemantics();
    void fakeReproducesHeldFileScenario();
    void fakeAddDirectoryBuildsAncestors();
    void fakeEnumerateListsOnlyDirectChildren();

    // --- 真实实现（本机平台）----------------------------------------------
    void nativeFileSystemReportsItsPlatform();
    void nativeFileSystemReadsRealDirectory();
};

// 回收站（PLAT-003）的用例在 Code/Tests/Trash 里。
//
// 原先这里有两条「deleteToTrash 必须返回 NotSupported」的用例，作用是锁住
// 「删除必须可逆」这个契约、等 PLAT-003 接上真实实现时失败以提醒改它。
// PLAT-003 落地时这两条连同 FileSystem::deleteToTrash 一起去掉了：
// 现在删除只有 TrashService 一条入口，契约由 Trash 套件用真实往返验证
// （删到废纸篓 → 断言文件确实在废纸篓里 → 还原 → 断言回到原处），
// 比原来那句「必须返回不支持」更有力。

#endif // LQCOMPARE_TST_FILESYSTEM_H
