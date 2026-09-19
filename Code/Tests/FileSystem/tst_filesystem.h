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
    void fakeTrashNeverPermanentlyDeletes();
    void fakeAddDirectoryBuildsAncestors();
    void fakeEnumerateListsOnlyDirectChildren();

    // --- 真实实现（本机平台）----------------------------------------------
    void nativeFileSystemReportsItsPlatform();
    void nativeFileSystemReadsRealDirectory();
    void nativeFileSystemTrashIsNotSilentlyPermanent();
};

#endif // LQCOMPARE_TST_FILESYSTEM_H
