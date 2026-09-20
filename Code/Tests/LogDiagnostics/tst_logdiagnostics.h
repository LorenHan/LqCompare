#ifndef LQCOMPARE_TST_LOGDIAGNOSTICS_H
#define LQCOMPARE_TST_LOGDIAGNOSTICS_H

#include <QObject>
#include <QtTest>

///
/// \brief 日志轮转与诊断包（PRD: OPT-010 第 2、3、5 条）。
///
/// 分组对齐规格的完成标准：
///   A 轮转策略与自检      —— 第 2 条（按大小 / 按天是**数据**，不是界面上的分支）
///   B 轮转判定（纯函数）   —— 第 2 条（「该不该轮转」不读磁盘、不取当前时间）
///   C 真实文件轮转与清空   —— 第 2、3 条（改名、滚动、保留份数、清空）
///   D 历史文件枚举        —— 第 2 条（诊断包要收进去的那几份）
///   E 脱敏                —— 第 5 条（允许脱敏，且次数要能看出来）
///   F 环境报告与文件名     —— 第 3 条（日志 + 版本 + 环境信息）
///   G 导出前提示          —— 第 5 条（先说清里面有什么，再让用户决定）
///   H 诊断包              —— 第 3、5 条（内容、清单、失败语义）
///   I 源码级护栏          —— 本模块不做界面动作，也不认识 Views/
///
/// 本套件**刻意不链接 QtGui**（见 LogDiagnosticsTests.pro）：这两件事都是
/// 「文件 + 字符串」，一旦有人把 `QMessageBox` / `QFileDialog` 塞进来，
/// 本工程会立刻构建失败。「打开日志目录」那种界面动作留在设置页里，
/// 正是为了让这里保持纯 QtCore。
///
/// 所有涉及文件系统的用例都落在 `QTemporaryDir` 里，不碰用户的真实日志目录。
///
class TstLogDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 轮转策略与自检（第 2 条） ----------
    void defaultsMatchTheDocumentedFactoryValues();
    void fromValuesReadsAllThreeKeys();
    void fromValuesKeepsUnusedFieldsAcrossModeSwitch();
    void unknownRotationModeFallsBackToNoneWithoutError();
    void toValuesRoundTripsThroughFromValues();
    void rotationModeChoicesCoverEveryModeExactlyOnce();
    void rotationModeIdentifiersAreDistinct();
    void rotationModeLabelsAreDistinctAndNonEmpty();
    void rotationModeNameParsingIsCaseInsensitiveAndTrimmed();
    void unknownRotationModeNameLeavesOutputUntouched();
    void validateRejectsKeepFilesOutOfRange();
    void validateRejectsSizeLimitOutsideBounds();
    void validateIgnoresSizeLimitWhenModeIsNotSize();
    void validateAcceptsEveryDefaultPolicy();

    // ---------- B 轮转判定（纯函数，第 2 条） ----------
    void noneModeNeverRotates();
    void sizeModeDoesNotRotateBelowTheLimit();
    void sizeModeRotatesAtExactlyTheLimit();
    void sizeModeRotatesAboveTheLimit();
    void emptyFileNeverRotatesInSizeMode();
    void emptyFileNeverRotatesInDailyMode();
    void dailyModeRotatesWhenTheFileIsFromYesterday();
    void dailyModeDoesNotRotateOnTheSameDay();
    void dailyModeRefusesWhenTheFileDateIsUnknown();
    void dailyModeReasonNamesBothDates();
    void decisionAlwaysCarriesAReason();
    void decisionCarriesCurrentSize();
    void rotationTriggerLabelsAreDistinct();
    void rotationSummaryDescribesEveryMode();

    // ---------- C 真实文件轮转与清空（第 2、3 条） ----------
    void applyRotationRenamesTheCurrentFileToDotOne();
    void applyRotationLeavesNoCurrentFileBehind();
    void applyRotationShiftsExistingHistoryUp();
    void applyRotationDropsFilesBeyondTheKeepCount();
    void applyRotationWithZeroKeepFilesDeletesTheOldFile();
    void applyRotationDoesNothingWhenTheDecisionSaysNo();
    void applyRotationReportsAnEmptyPathAsFailure();
    void applyRotationReportsFailureWhenItCannotFreeTheHistorySlot();
    void applyRotationIsIdempotentForTheSameFile();
    void applyRotationUsesTheFilesLastWriteDateForDailyPolicy();
    void rotatedLogPathIsEmptyForNonPositiveIndexes();
    void rotatedLogPathAppendsTheIndex();
    void clearLogFileTruncatesButKeepsTheFile();
    void clearLogFileSucceedsWhenTheFileDoesNotExist();
    void clearLogFileRejectsAnEmptyPath();

    // ---------- D 历史文件枚举（第 2 条） ----------
    void logHistoryFilesReturnsAscendingIndexes();
    void logHistoryFilesIgnoresLookalikeFiles();
    void logHistoryFilesIgnoresNamesTheGlobMatchedByAccident();
    void logHistoryFilesIsEmptyForAnEmptyPath();
    void logHistoryFilesIsEmptyWhenThereIsNoHistory();

    // ---------- E 脱敏（第 5 条） ----------
    void defaultRulesReplaceHomeAndStorageDirectory();
    void defaultRulesAreSortedLongestPrefixFirst();
    void homeDirectoryIsReplacedByTilde();
    void storageDirectoryIsReplacedByPlaceholder();
    void lookalikeDirectoryNameIsNotReplaced();
    void prefixAtTheEndOfTextIsReplaced();
    void replacementCountAddsUpAcrossRules();
    void emptyRuleListKeepsTheTextUnchanged();
    void blankDirectoriesProduceNoRules();
    void textWithoutAnyMatchIsReturnedVerbatim();
    void windowsSeparatorCountsAsABoundary();

    // ---------- F 环境报告与文件名（第 3 条） ----------
    void environmentReportContainsEveryField();
    void environmentReportSaysNoFileLoggingWhenThePathIsEmpty();
    void environmentReportFallsBackToAPlaceholder();
    void environmentReportCarriesTheExportTime();
    void bundleFileNamesAreStableAndDistinct();

    // ---------- G 导出前提示（第 5 条） ----------
    void noticeAlwaysMentionsPaths();
    void redactedNoticeExplainsWhatGetsReplaced();
    void rawNoticeWarnsAboutUserNames();
    void noticesDifferBetweenModes();

    // ---------- H 诊断包（第 3、5 条） ----------
    void bundleCreatesATimestampedDirectory();
    void bundleWritesManifestEnvironmentAndLog();
    void bundleFilesComeInLogThenEnvironmentThenManifestOrder();
    void bundleArchivesRotatedHistory();
    void bundleRedactsLogContentByDefault();
    void bundleKeepsRawContentWhenRedactionIsOff();
    void bundleRedactsTheEnvironmentReport();
    void manifestRecordsTheRedactionState();
    void manifestCountsRedactedOccurrences();
    void manifestListsEveryFileInTheBundle();
    void manifestIsParseableJson();
    void bundleWithoutAFileLogStillSucceeds();
    void bundleFillsTheEnvironmentLogPathFromTheRequest();
    void bundleReportsAnEmptyOutputDirectoryAsFailure();
    void bundleReportsAnUnwritableOutputDirectoryAsFailure();
    void secondBundleInTheSameSecondGetsASuffix();

    // ---------- I 源码级护栏 ----------
    void moduleNeverIncludesAnyViewHeader();
    void moduleNeverUsesUiOnlyApi();
    void sourceGuardWouldCatchAnInjectedUiCall();

private:
    static QString codeRoot();
    static QString readSource(const QString &relativePath);
    static QString stripComments(const QString &source);
};

#endif // LQCOMPARE_TST_LOGDIAGNOSTICS_H
