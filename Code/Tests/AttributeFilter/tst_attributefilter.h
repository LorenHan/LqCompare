#ifndef LQCOMPARE_TST_ATTRIBUTEFILTER_H
#define LQCOMPARE_TST_ATTRIBUTEFILTER_H

#include <QObject>
#include <QtTest>

///
/// \brief 属性过滤的测试（PRD: FILT-003）。
///
/// 分组对齐规格的五条完成标准：
///   A 大小范围与单位 —— 第 1 条（最小值/最大值 + KB/MB/GB 单位）
///   B 修改时间范围   —— 第 2 条（相对时间与绝对区间）
///   C 属性位         —— 第 3 条的前半（只读/隐藏/系统/归档，三态）
///   D 所有者与组     —— 第 3 条的后半（Unix），含平台决定的大小写
///   E 与名称过滤的与 —— 第 4 条（条件之间为与，且与名称过滤构成整体的与关系）
///   F 声明文本       —— 让属性条件能落盘的形态（与会话层的存储机制对接）
///   G 缺失与写错     —— 第 1、2 条那种「放行 + 报出来」的边界
///   H 条件表自检     —— 启动护栏，以及「表是唯一事实来源」
///   I 与内容解耦     —— 第 5 条里**能独立验证**的那一半（源码级护栏）
///
/// 本套件**刻意不链接 QtGui**（见 AttributeFilterTests.pro）：属性过滤全是纯逻辑，
/// 一旦有人把界面依赖（图标、字体、控件）塞进 attributefilter.cpp，
/// 本工程会立刻构建失败。
///
/// 它**链接 Services/Session**：E 组要拿真正的 `FilterStack` 做「三层名称过滤 ×
/// 属性过滤」的合成，而 `FilterStack`（FILT-005）要用 `SessionSettings` 表达落点。
///
class TstAttributeFilter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 大小范围与单位（第 1 条） ----------
    void sizeMinimumRejectsSmallerEntries();
    void sizeMaximumRejectsLargerEntries();
    void sizeRangeKeepsBothEndpoints();
    void sizeUnitsAreBinary();
    void sizeAcceptsFractionalValues();
    void sizeBareNumberMeansBytes();
    void sizeMaximumZeroKeepsOnlyEmptyFiles();
    void sizeRejectsNegativeText();
    void sizeRejectsUnknownUnit();
    void sizeRejectsOverflow();
    void sizeInvertedRangeDoesNotApplyAndIsReported();
    void sizePartialRangeStillAppliesTheGoodBound();
    void sizeIsUndecidedWhenSizeIsUnknown();
    void sizeDoesNothingWhileDisabled();
    void sizeFormattingIsReadableAndExact();

    // ---------- B 修改时间范围（第 2 条） ----------
    void timeAbsoluteRangeKeepsBothEndpoints();
    void timeAbsoluteRangeRejectsOutside();
    void timeAbsoluteLowerBoundOnly();
    void timeAbsoluteUpperBoundOnly();
    void timeRelativeWindowIsRolling();
    void timeRelativeWindowExcludesFutureTimestamps();
    void timeRelativeWithoutReferenceTimeIsUndecided();
    void timeRelativeTextAcceptsCommonForms();
    void timeRelativeRejectsZeroDays();
    void timeInvertedRangeDoesNotApplyAndIsReported();
    void timeTextAcceptsChineseDateForm();
    void timeTextRejectsGarbage();
    void timeWindowIsDescribedForDisplay();
    void timeIsUndecidedWhenLastModifiedIsUnknown();

    // ---------- C 属性位（第 3 条前半） ----------
    void requiredAttributeMustBeSet();
    void forbiddenAttributeMustBeUnset();
    void unknownAttributeBitIsUndecidedNotRejected();
    void partiallyKnownAttributeBitsAreUndecided();
    void multipleAttributeRequirementsAreAnded();
    void attributeRequirementDefaultsToIgnore();
    void attributeIdentifiersAndLabelsAreDistinctAndStable();
    void parseEntryAttributeAcceptsNamesAndAliases();
    void attributeConditionDescribesSetAndUnsetBits();
    void everyEntryAttributeIsAddressableFromTheTable();

    // ---------- D 所有者与组（第 3 条后半） ----------
    void ownerAnyOfKeepsListedOwners();
    void ownerAnyOfRejectsOtherOwners();
    void ownerNoneOfExcludesListedOwners();
    void ownerListIsCaseSensitiveOnPosix();
    void ownerListIsCaseInsensitiveOnWindows();
    void groupRequirementIsIndependentOfOwner();
    void unknownOwnerIsUndecided();
    void nameListTextSplitsOnCommasAndSpaces();
    void ownerConditionDescribesBothModes();

    // ---------- E 与名称过滤的与关系（第 4 条） ----------
    void entryIsKeptOnlyWhenBothSidesAgree();
    void nameExclusionWinsEvenWhenAttributesPass();
    void attributeRejectionWinsEvenWhenNamePasses();
    void decideEntryReportsTheDecidingLayer();
    void decideEntryWithASingleMaskFilterReportsTheRule();
    void entryDecisionTextMentionsBothSides();
    void entryDecisionIsAConjunctionOverAllCombinations();
    void singleMaskFilterOverloadHasNoDecidingLayer();

    // ---------- F 声明文本 ----------
    void declarationRoundTripsEveryCondition();
    void declarationIgnoresCommentsAndBlankLines();
    void declarationRejectsUnknownKeyAndListsAvailableOnes();
    void declarationRejectsMissingValue();
    void declarationRejectsDuplicateKey();
    void declarationRejectsConflictingTimeKeys();
    void declarationRejectsUnknownAttributeName();
    void declarationAcceptsChineseAttributeLabels();
    void declarationAcceptsCrlfLineEndings();
    void declarationEnablesConditionsThatHaveConstraints();
    void declarationReportsLineAndColumn();
    void declarationKeyDiffersFromTheMaskDeclarationKey();
    void declarationMinusPrefixMeansExclusion();

    // ---------- G 缺失与写错 ----------
    void emptyFilterAcceptsEverything();
    void filterWithNoActiveConditionAcceptsEverything();
    void decisionListsUndecidedConditionsOnly();
    void decisionReportsProblemsWithoutBlocking();
    void decisionNamesTheFirstBlockingCondition();
    void decisionListsEveryBlockingCondition();
    void outcomeTriStateIsReadableInText();
    void activeConditionsFollowTheTableOrder();
    void filterDescriptionListsEveryCondition();

    // ---------- H 条件表自检 ----------
    void shippedConditionTableIsClean();
    void tableDrivesEveryCondition();
    void validatorCatchesAWrongRowCount();
    void validatorCatchesAReorderedTable();
    void validatorCatchesADuplicatedIdentifier();
    void validatorCatchesADuplicatedDeclarationKey();
    void validatorCatchesAConditionThatWouldReadContent();
    void validatorCatchesAMissingDeclarationKey();

    // ---------- I 与内容解耦（第 5 条能独立验证的那一半） ----------
    void attributeFilterSourcesNeverTouchFileContents();
    void contentScanDetectsAPlantedRead();

private:
    /// 读一段仓库源码（用 `LQCOMPARE_CODE_ROOT` 定位，与 Tests/SessionType 同法）。
    static QString readSourceFile(const QString &relativePath);
};

#endif // LQCOMPARE_TST_ATTRIBUTEFILTER_H
