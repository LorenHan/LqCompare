#ifndef LQCOMPARE_TST_FILEOPSOPTIONS_H
#define LQCOMPARE_TST_FILEOPSOPTIONS_H

#include <QObject>
#include <QtTest>

#include "fileopsoptions.h"
#include "optionsrepository.h"

///
/// \brief 文件操作的默认行为（PRD: OPT-005）。
///
/// 分组对齐规格的完成标准：
///   A 出厂默认与「默认保守」契约 —— 规格「边界」条款（默认走回收站、默认不覆盖）
///   B 删除方式                  —— 第 1 条（回收站 / 永久删除，永久删除要提示不可恢复）
///   C 覆盖策略与「文件较新」提示  —— 第 2 条（询问 / 覆盖 / 跳过，较新要单独说清楚代价）
///   D 复制时保留的元数据项        —— 第 3 条（时间戳 / 属性 / 权限）
///   E 体积与条数确认阈值          —— 第 4 条（默认 100MB / 20 个，0 表示关闭）
///   F 操作后的校验方式            —— 第 5 条（无 / 大小 / CRC）
///   G 从设置值构造与设置仓库接线   —— 五条都依赖「键名两边一致」这一件事
///   H 键表自检                   —— 反向验证：故意写坏的键表必须被报出来
///   I 源码级护栏                 —— 本模块不读文件、不依赖界面、不认识 OptionsRepository
///
/// 本套件**刻意不链接 QtGui**（见 FileOpsOptionsTests.pro）：文件操作的默认值是纯数据，
/// 一旦有人把 `QMessageBox` / `QIcon` 之类的界面依赖塞进 `fileopsoptions.cpp`，
/// 本工程会立刻构建失败。与 `Tests/Settings`、`Tests/ContentFilter` 同一条纪律。
///
/// `Services/Settings` 与 `Services/Log` 是**测试侧**的依赖，不是模块的依赖：
/// G 组要拿真的设置仓库与真的 `definitions()` 才能证明「策略读的键」与
/// 「仓库登记的键」没有分家。`fileopsoptions.cpp` 本身不 include 任何一个
/// （I 组的源码级护栏盯着它）。
///
class TstFileOpsOptions : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---------- A 出厂默认与「默认保守」契约（规格边界条款） ----------
    void defaultsUseTrashForDeletion();
    void defaultsAskBeforeOverwriting();
    void defaultsPreserveEveryMetadataItem();
    void defaultsConfirmLargeFilesAtOneHundredMegabytes();
    void defaultsConfirmBatchDeletesAtTwentyItems();
    void defaultsDoNotVerifyAfterCopy();
    void defaultPolicyHasNoViolationsOfTheSafetyContract();
    void safetyContractReportsPermanentDeletion();
    void safetyContractReportsOverwriteWithoutAsking();
    void safetyContractNamesEveryUnpreservedMetadataItem();
    void defaultPolicyHasNoInternalProblems();
    void defaultDescribeMentionsEveryAxis();

    // ---------- B 删除方式（第 1 条） ----------
    void deleteModeIdentifiersAreStable();
    void deleteModeRoundTripsThroughItsIdentifier();
    void unknownDeleteModeIdentifierIsRejected();
    void unknownDeleteModeIdentifierKeepsTheConservativeDefault();
    void trashModeNeedsNoWarning();
    void permanentModeWarnsThatItCannotBeUndone();
    void deleteWarningIsEmptyExactlyWhenTrashIsSelected();
    void deleteLabelsDistinguishTrashFromPermanent();

    // ---------- C 覆盖策略与「文件较新」提示（第 2 条） ----------
    void overwritePolicyIdentifiersAreStable();
    void overwritePolicyRoundTripsThroughItsIdentifier();
    void missingTargetNeverAsksRegardlessOfPolicy();
    void missingTargetProducesNoNotice();
    void askPolicyAsksForExistingTargets();
    void overwritePolicyOverwritesWithoutAsking();
    void skipPolicySkipsExistingTargets();
    void newerTargetIsFlagged();
    void olderAndSameTimeTargetsAreNotFlagged();
    void askNoticeSpellsOutThatNewerContentIsLost();
    void askNoticeForOlderTargetDoesNotClaimNewerContentIsLost();
    void overwriteNoticeStillWarnsAboutNewerTarget();
    void skipNoticeSaysTheTargetIsUntouched();
    void situationIdentifiersAreStable();

    // ---------- D 复制时保留的元数据项（第 3 条） ----------
    void enabledIdentifiersListAllThreeByDefault();
    void disabledItemsAreNotListed();
    void allIsFalseWhenAnyItemIsOff();
    void metadataLabelsAreTranslated();
    void unknownMetadataLabelIsEmptyRatherThanInvented();
    void eachMetadataKeyIsReadIndependently();
    void timestampsCanBeDroppedWithoutTouchingTheOthers();
    void attributesCanBeDroppedWithoutTouchingTheOthers();
    void permissionsCanBeDroppedWithoutTouchingTheOthers();
    void nonBooleanMetadataValueKeepsTheItemEnabledAndIsReported();

    // ---------- E 体积与条数确认阈值（第 4 条） ----------
    void megabytesConvertToBytes();
    void zeroMegabytesMeansConfirmationIsOff();
    void largeFileConfirmationTriggersAtTheThreshold();
    void largeFileConfirmationIgnoresSmallerFiles();
    void unknownSizeIsNotTreatedAsHuge();
    void shuttingOffLargeFileConfirmationDisablesItEntirely();
    void batchDeleteConfirmationTriggersAtTheThreshold();
    void batchDeleteConfirmationIgnoresFewerItems();
    void emptyBatchDeleteNeverAsks();
    void shuttingOffBatchDeleteConfirmationDisablesItEntirely();
    void thresholdsRoundTripBetweenMegabytesAndBytes();
    void thresholdThatIsNotAWholeNumberOfMegabytesIsReported();
    void negativeThresholdsAreReported();
    void negativeThresholdValueFallsBackToTheDefault();

    // ---------- F 操作后的校验方式（第 5 条） ----------
    void verifyModeIdentifiersAreStable();
    void verifyModeRoundTripsThroughItsIdentifier();
    void verifyLabelsDistinguishTheThreeModes();
    void unknownVerifyModeKeepsTheDefault();
    void verifyModeIsReadFromItsOwnKey();
    void eachVerifyModeIsAcceptedVerbatim();

    // ---------- G 从设置值构造与设置仓库接线 ----------
    void emptyValuesProduceTheConservativeDefaults();
    void everyKeyInTheTableIsRegisteredInTheRepository();
    void repositoryDefaultsMatchTheServiceDefaults();
    void repositoryDefaultValuesPassValidation();
    void applyingTheRepositoryChangesThePolicy();
    void repositoryChoiceListMatchesTheAcceptedIdentifiers();
    void policyReadsValuesThroughTheKeyTable();
    void valuesMapWithUnknownKeysIsIgnored();
    void wrongTypeForAChoiceIsReportedAndFallsBack();
    void repositoryRoundTripKeepsThePolicyStable();
    void unknownChoiceIsReportedWithoutBeingAdopted();
    void fallbacksAreEmptyForAWellFormedMap();

    // ---------- H 键表自检与反向验证 ----------
    void builtinKeyTableIsClean();
    void builtinKeyTableCoversEightKeys();
    void tableShortNamesResolveToFullKeys();
    void unknownShortNameResolvesToEmpty();
    void keyTableSelfCheckRejectsEmptyIdentifier();
    void keyTableSelfCheckRejectsDuplicateIdentifier();
    void keyTableSelfCheckRejectsDuplicateKey();
    void keyTableSelfCheckRejectsMissingPrefix();
    void keyTableSelfCheckRejectsEmptyPurpose();
    void keyTableSelfCheckRejectsEmptyTable();

    // ---------- I 源码级护栏 ----------
    void moduleNeverTouchesTheFileSystem();
    void sourceGuardWouldCatchAnInjectedRead();
    void moduleNeverIncludesTheSettingsRepository();
    void moduleNeverIncludesAnyViewHeader();

private:
    static QString codeRoot();
    static QString readSource(const QString &relativePath);
    // 去掉 // 与 /* */ 注释：护栏认的是「形状」，不区分代码与注释
    // （本模块的头文件里就逐字写着 QFile 这几个字）。有**成对**用例盯着它，
    // 见 `sourceGuardWouldCatchAnInjectedRead`。
    static QString stripComments(const QString &source);
};

#endif // LQCOMPARE_TST_FILEOPSOPTIONS_H
