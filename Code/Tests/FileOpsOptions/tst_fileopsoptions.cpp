#include "tst_fileopsoptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

using namespace LqCompare::Files;

// 只用别名而不是 `using namespace`：本套件里 `Settings::` 与 `Files::` 的成员
// 名字有重叠（两处都有 Identifier 系列函数的可能），全限定写法更容易看出
// 某个标识符属于哪一层。
namespace Settings = LqCompare::Settings;

namespace {

// QCOMPARE 的宏参数里只有圆括号能保护逗号，花括号不算（见 §6），
// 所以带初始值列表的断言一律先经过它。
QStringList asList(const QStringList &list) { return list; }

QString deleteIdentifier(DeleteMode mode) { return deleteModeIdentifier(mode); }
QString overwriteIdentifier(OverwritePolicy policy) { return overwritePolicyIdentifier(policy); }
QString verifyIdentifier(VerifyMode mode) { return verifyModeIdentifier(mode); }
QString situationIdentifier(OverwriteSituation situation) { return overwriteSituationIdentifier(situation); }

/// 从设置仓库的定义里取某个键的出厂值——测试用它核对「仓库的默认值」
/// 与「服务层的默认值」是不是同一个值，而不是各写一份期望常量。
QVariant repositoryDefault(const QString &key)
{
    const Settings::OptionDefinition *definition = Settings::OptionsRepository::definition(key);
    return definition ? definition->defaultValue : QVariant();
}

/// 把一份策略的六个轴折成一行，用于断言「从设置值构造出来的与手写的相等」。
QStringList policyAxes(const FileOperationPolicy &policy)
{
    return QStringList{deleteIdentifier(policy.deleteMode),
                       overwriteIdentifier(policy.overwritePolicy),
                       policy.preserve.enabledIdentifiers().join(QLatin1Char(',')),
                       QString::number(policy.largeFileConfirmBytes),
                       QString::number(policy.batchDeleteConfirmCount),
                       verifyIdentifier(policy.verifyMode)};
}

} // namespace

// ---------------------------------------------------------------------------
// A 出厂默认与「默认保守」契约
// ---------------------------------------------------------------------------

void TstFileOpsOptions::initTestCase()
{
    QVERIFY2(!codeRoot().isEmpty(), "缺少 LQCOMPARE_CODE_ROOT 定义，源码级护栏无法定位源码。");
    QVERIFY(QFileInfo(codeRoot() + QStringLiteral("/Services/Files/fileopsoptions.cpp")).isFile());
    // 键表是这一整套用例的前提：它坏了，后面的结论都不成立。
    QCOMPARE(validateFileOperationKeyTable(fileOperationKeyTable()), QStringList());
}

void TstFileOpsOptions::defaultsUseTrashForDeletion()
{
    FileOperationPolicy policy;
    QCOMPARE(deleteIdentifier(policy.deleteMode), QStringLiteral("trash"));
}

void TstFileOpsOptions::defaultsAskBeforeOverwriting()
{
    FileOperationPolicy policy;
    QCOMPARE(overwriteIdentifier(policy.overwritePolicy), QStringLiteral("ask"));
}

void TstFileOpsOptions::defaultsPreserveEveryMetadataItem()
{
    FileOperationPolicy policy;
    QVERIFY(policy.preserve.timestamps);
    QVERIFY(policy.preserve.attributes);
    QVERIFY(policy.preserve.permissions);
}

void TstFileOpsOptions::defaultsConfirmLargeFilesAtOneHundredMegabytes()
{
    FileOperationPolicy policy;
    QCOMPARE(policy.largeFileConfirmBytes, qint64(100) * 1024 * 1024);
}

void TstFileOpsOptions::defaultsConfirmBatchDeletesAtTwentyItems()
{
    FileOperationPolicy policy;
    QCOMPARE(policy.batchDeleteConfirmCount, 20);
}

void TstFileOpsOptions::defaultsDoNotVerifyAfterCopy()
{
    FileOperationPolicy policy;
    QCOMPARE(verifyIdentifier(policy.verifyMode), QStringLiteral("none"));
}

void TstFileOpsOptions::defaultPolicyHasNoViolationsOfTheSafetyContract()
{
    FileOperationPolicy policy;
    QCOMPARE(policy.safetyContractViolations(), QStringList());
}

void TstFileOpsOptions::safetyContractReportsPermanentDeletion()
{
    FileOperationPolicy policy;
    policy.deleteMode = DeleteMode::Permanent;
    const QStringList violations = policy.safetyContractViolations();
    QCOMPARE(violations.size(), 1);
    QVERIFY2(violations.first().contains(QStringLiteral("回收站")), qPrintable(violations.first()));
}

void TstFileOpsOptions::safetyContractReportsOverwriteWithoutAsking()
{
    FileOperationPolicy policy;
    policy.overwritePolicy = OverwritePolicy::Overwrite;
    const QStringList violations = policy.safetyContractViolations();
    QCOMPARE(violations.size(), 1);
    QVERIFY2(violations.first().contains(QStringLiteral("询问")), qPrintable(violations.first()));
    // 跳过也是「不问」，但它不丢数据，因此不算违反保守契约。
    // 这一条是刻意的：把 Skip 也算成违规会让「保守」变成一个没有边界的词。
    FileOperationPolicy skipping;
    skipping.overwritePolicy = OverwritePolicy::Skip;
    QCOMPARE(skipping.safetyContractViolations(), QStringList());
}

void TstFileOpsOptions::safetyContractNamesEveryUnpreservedMetadataItem()
{
    FileOperationPolicy policy;
    policy.preserve.attributes = false;
    policy.preserve.permissions = false;
    const QStringList violations = policy.safetyContractViolations();
    QCOMPARE(violations.size(), 1);
    QVERIFY2(violations.first().contains(QStringLiteral("属性位")), qPrintable(violations.first()));
    QVERIFY2(violations.first().contains(QStringLiteral("权限")), qPrintable(violations.first()));
    // 没关的那一项不该被报出来。
    QVERIFY2(!violations.first().contains(QStringLiteral("修改时间")), qPrintable(violations.first()));
}

void TstFileOpsOptions::defaultPolicyHasNoInternalProblems()
{
    FileOperationPolicy policy;
    QCOMPARE(policy.validate(), QStringList());
}

void TstFileOpsOptions::defaultDescribeMentionsEveryAxis()
{
    FileOperationPolicy policy;
    const QString text = policy.describe();
    for (const QString &fragment : QStringList{QStringLiteral("回收站"), QStringLiteral("询问"),
                                               QStringLiteral("元数据"), QStringLiteral("100 MB"),
                                               QStringLiteral("20 个"), QStringLiteral("不校验")}) {
        QVERIFY2(text.contains(fragment), qPrintable(text + QStringLiteral(" || ") + fragment));
    }
}

// ---------------------------------------------------------------------------
// B 删除方式（第 1 条）
// ---------------------------------------------------------------------------

void TstFileOpsOptions::deleteModeIdentifiersAreStable()
{
    QCOMPARE(deleteIdentifier(DeleteMode::Trash), QStringLiteral("trash"));
    QCOMPARE(deleteIdentifier(DeleteMode::Permanent), QStringLiteral("permanent"));
}

void TstFileOpsOptions::deleteModeRoundTripsThroughItsIdentifier()
{
    for (DeleteMode mode : {DeleteMode::Trash, DeleteMode::Permanent}) {
        DeleteMode parsed = DeleteMode::Permanent;
        QVERIFY(deleteModeFromIdentifier(deleteIdentifier(mode), &parsed));
        QCOMPARE(deleteIdentifier(parsed), deleteIdentifier(mode));
    }
}

void TstFileOpsOptions::unknownDeleteModeIdentifierIsRejected()
{
    DeleteMode parsed = DeleteMode::Trash;
    QVERIFY(!deleteModeFromIdentifier(QStringLiteral("permanant"), &parsed));
    // 被拒绝时不许碰出参——否则调用方会拿到一个「上一次的结论」。
    QCOMPARE(deleteIdentifier(parsed), QStringLiteral("trash"));
}

void TstFileOpsOptions::unknownDeleteModeIdentifierKeepsTheConservativeDefault()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.deleteMode"), QStringLiteral("Permanent")}});
    QCOMPARE(deleteIdentifier(policy.deleteMode), QStringLiteral("trash"));
    QCOMPARE(policy.fallbacks.size(), 1);
    QVERIFY2(policy.fallbacks.first().contains(QStringLiteral("fileops.deleteMode")),
             qPrintable(policy.fallbacks.first()));
    QCOMPARE(policy.validate(), policy.fallbacks);
}

void TstFileOpsOptions::trashModeNeedsNoWarning()
{
    FileOperationPolicy policy;
    QCOMPARE(policy.deleteWarning(), QString());
}

void TstFileOpsOptions::permanentModeWarnsThatItCannotBeUndone()
{
    FileOperationPolicy policy;
    policy.deleteMode = DeleteMode::Permanent;
    const QString warning = policy.deleteWarning();
    QVERIFY2(warning.contains(QStringLiteral("不可恢复")), qPrintable(warning));
    QVERIFY2(warning.contains(QStringLiteral("无法撤销")), qPrintable(warning));
}

void TstFileOpsOptions::deleteWarningIsEmptyExactlyWhenTrashIsSelected()
{
    for (DeleteMode mode : {DeleteMode::Trash, DeleteMode::Permanent}) {
        FileOperationPolicy policy;
        policy.deleteMode = mode;
        QCOMPARE(policy.deleteWarning().isEmpty(), mode == DeleteMode::Trash);
    }
}

void TstFileOpsOptions::deleteLabelsDistinguishTrashFromPermanent()
{
    const QString trash = deleteModeLabel(DeleteMode::Trash);
    const QString permanent = deleteModeLabel(DeleteMode::Permanent);
    QVERIFY(!trash.isEmpty());
    QVERIFY(!permanent.isEmpty());
    QVERIFY(trash != permanent);
}

// ---------------------------------------------------------------------------
// C 覆盖策略与「文件较新」提示（第 2 条）
// ---------------------------------------------------------------------------

void TstFileOpsOptions::overwritePolicyIdentifiersAreStable()
{
    QCOMPARE(overwriteIdentifier(OverwritePolicy::Ask), QStringLiteral("ask"));
    QCOMPARE(overwriteIdentifier(OverwritePolicy::Overwrite), QStringLiteral("overwrite"));
    QCOMPARE(overwriteIdentifier(OverwritePolicy::Skip), QStringLiteral("skip"));
}

void TstFileOpsOptions::overwritePolicyRoundTripsThroughItsIdentifier()
{
    for (OverwritePolicy policy : {OverwritePolicy::Ask, OverwritePolicy::Overwrite, OverwritePolicy::Skip}) {
        OverwritePolicy parsed = OverwritePolicy::Ask;
        QVERIFY(overwritePolicyFromIdentifier(overwriteIdentifier(policy), &parsed));
        QCOMPARE(overwriteIdentifier(parsed), overwriteIdentifier(policy));
    }
}

void TstFileOpsOptions::missingTargetNeverAsksRegardlessOfPolicy()
{
    // 目标不存在时没有什么可覆盖的。缺了这一条，默认的「逐个询问」策略
    // 会在一个空目录里逐条追问用户——而那里根本没有东西可被覆盖。
    for (OverwritePolicy policy : {OverwritePolicy::Ask, OverwritePolicy::Overwrite, OverwritePolicy::Skip}) {
        FileOperationPolicy rules;
        rules.overwritePolicy = policy;
        const OverwriteDecision decision = rules.overwriteDecision(OverwriteSituation::TargetMissing);
        QCOMPARE(decision.action, OverwriteAction::Overwrite);
        QVERIFY(!decision.targetNewer);
    }
}

void TstFileOpsOptions::missingTargetProducesNoNotice()
{
    FileOperationPolicy rules;
    QCOMPARE(rules.overwriteDecision(OverwriteSituation::TargetMissing).notice, QString());
}

void TstFileOpsOptions::askPolicyAsksForExistingTargets()
{
    FileOperationPolicy rules;
    for (OverwriteSituation situation : {OverwriteSituation::TargetOlder,
                                         OverwriteSituation::TargetSameTime,
                                         OverwriteSituation::TargetNewer}) {
        QCOMPARE(rules.overwriteDecision(situation).action, OverwriteAction::Ask);
    }
}

void TstFileOpsOptions::overwritePolicyOverwritesWithoutAsking()
{
    FileOperationPolicy rules;
    rules.overwritePolicy = OverwritePolicy::Overwrite;
    QCOMPARE(rules.overwriteDecision(OverwriteSituation::TargetOlder).action, OverwriteAction::Overwrite);
}

void TstFileOpsOptions::skipPolicySkipsExistingTargets()
{
    FileOperationPolicy rules;
    rules.overwritePolicy = OverwritePolicy::Skip;
    QCOMPARE(rules.overwriteDecision(OverwriteSituation::TargetOlder).action, OverwriteAction::Skip);
}

void TstFileOpsOptions::newerTargetIsFlagged()
{
    FileOperationPolicy rules;
    const OverwriteDecision decision = rules.overwriteDecision(OverwriteSituation::TargetNewer);
    QVERIFY(decision.targetNewer);
    QVERIFY2(decision.notice.contains(QStringLiteral("较新")), qPrintable(decision.notice));
}

void TstFileOpsOptions::olderAndSameTimeTargetsAreNotFlagged()
{
    FileOperationPolicy rules;
    QVERIFY(!rules.overwriteDecision(OverwriteSituation::TargetOlder).targetNewer);
    QVERIFY(!rules.overwriteDecision(OverwriteSituation::TargetSameTime).targetNewer);
}

void TstFileOpsOptions::askNoticeSpellsOutThatNewerContentIsLost()
{
    // 只说「目标已存在」时用户会顺手点「全部覆盖」，而这一批里恰恰混着
    // 几个比源文件更新的目标——那是真正的数据丢失。代价必须写清楚。
    FileOperationPolicy rules;
    const QString notice = rules.overwriteDecision(OverwriteSituation::TargetNewer).notice;
    QVERIFY2(notice.contains(QStringLiteral("较新的内容")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("丢掉")), qPrintable(notice));
}

void TstFileOpsOptions::askNoticeForOlderTargetDoesNotClaimNewerContentIsLost()
{
    FileOperationPolicy rules;
    const QString notice = rules.overwriteDecision(OverwriteSituation::TargetOlder).notice;
    QVERIFY(!notice.contains(QStringLiteral("较新")));
    QVERIFY2(notice.contains(QStringLiteral("旧")), qPrintable(notice));
    // 旧目标与同时刻目标必须给出不同的措辞，否则「较新」这个区分就没有落地。
    const QString same = rules.overwriteDecision(OverwriteSituation::TargetSameTime).notice;
    QVERIFY(same != notice);
}

void TstFileOpsOptions::overwriteNoticeStillWarnsAboutNewerTarget()
{
    FileOperationPolicy rules;
    rules.overwritePolicy = OverwritePolicy::Overwrite;
    const QString notice = rules.overwriteDecision(OverwriteSituation::TargetNewer).notice;
    QVERIFY2(notice.contains(QStringLiteral("较新")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("直接覆盖")), qPrintable(notice));
}

void TstFileOpsOptions::skipNoticeSaysTheTargetIsUntouched()
{
    FileOperationPolicy rules;
    rules.overwritePolicy = OverwritePolicy::Skip;
    const QString notice = rules.overwriteDecision(OverwriteSituation::TargetNewer).notice;
    QVERIFY2(notice.contains(QStringLiteral("跳过")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("未被改动")), qPrintable(notice));
}

void TstFileOpsOptions::situationIdentifiersAreStable()
{
    QCOMPARE(situationIdentifier(OverwriteSituation::TargetMissing), QStringLiteral("target-missing"));
    QCOMPARE(situationIdentifier(OverwriteSituation::TargetOlder), QStringLiteral("target-older"));
    QCOMPARE(situationIdentifier(OverwriteSituation::TargetSameTime), QStringLiteral("target-same-time"));
    QCOMPARE(situationIdentifier(OverwriteSituation::TargetNewer), QStringLiteral("target-newer"));
}

// ---------------------------------------------------------------------------
// D 复制时保留的元数据项（第 3 条）
// ---------------------------------------------------------------------------

void TstFileOpsOptions::enabledIdentifiersListAllThreeByDefault()
{
    MetadataPreservation preserve;
    QCOMPARE(preserve.enabledIdentifiers(),
             asList(QStringList{QStringLiteral("timestamps"), QStringLiteral("attributes"),
                                QStringLiteral("permissions")}));
    QVERIFY(preserve.all());
}

void TstFileOpsOptions::disabledItemsAreNotListed()
{
    MetadataPreservation preserve;
    preserve.attributes = false;
    QCOMPARE(preserve.enabledIdentifiers(),
             asList(QStringList{QStringLiteral("timestamps"), QStringLiteral("permissions")}));
}

void TstFileOpsOptions::allIsFalseWhenAnyItemIsOff()
{
    for (int index = 0; index < 3; ++index) {
        MetadataPreservation preserve;
        if (index == 0) preserve.timestamps = false;
        if (index == 1) preserve.attributes = false;
        if (index == 2) preserve.permissions = false;
        QVERIFY(!preserve.all());
    }
}

void TstFileOpsOptions::metadataLabelsAreTranslated()
{
    QCOMPARE(metadataItemLabel(QStringLiteral("timestamps")), QStringLiteral("修改时间"));
    QCOMPARE(metadataItemLabel(QStringLiteral("attributes")), QStringLiteral("属性位"));
    QCOMPARE(metadataItemLabel(QStringLiteral("permissions")), QStringLiteral("权限"));
}

void TstFileOpsOptions::unknownMetadataLabelIsEmptyRatherThanInvented()
{
    // 编一个假名字会让用户拿一个根本不存在的符号去搜。
    QCOMPARE(metadataItemLabel(QStringLiteral("acl")), QString());
    QCOMPARE(metadataItemLabel(QString()), QString());
}

void TstFileOpsOptions::eachMetadataKeyIsReadIndependently()
{
    struct Row {
        const char *key;
        bool MetadataPreservation::*member;
    };
    const QVector<Row> rows {
        { "fileops.preserveTimestamps", &MetadataPreservation::timestamps },
        { "fileops.preserveAttributes", &MetadataPreservation::attributes },
        { "fileops.preservePermissions", &MetadataPreservation::permissions }
    };
    for (const Row &row : rows) {
        FileOperationPolicy policy = FileOperationPolicy::fromValues(
                {{QString::fromLatin1(row.key), false}});
        QVERIFY2(!(policy.preserve.*(row.member)), row.key);
        // 只关掉这一项，另外两项必须还在——三条键很容易读串成一条。
        QCOMPARE(policy.preserve.enabledIdentifiers().size(), 2);
        QCOMPARE(policy.fallbacks, QStringList());
    }
}

void TstFileOpsOptions::timestampsCanBeDroppedWithoutTouchingTheOthers()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.preserveTimestamps"), false}});
    QVERIFY(!policy.preserve.timestamps);
    QVERIFY(policy.preserve.attributes);
    QVERIFY(policy.preserve.permissions);
}

void TstFileOpsOptions::attributesCanBeDroppedWithoutTouchingTheOthers()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.preserveAttributes"), false}});
    QVERIFY(policy.preserve.timestamps);
    QVERIFY(!policy.preserve.attributes);
    QVERIFY(policy.preserve.permissions);
}

void TstFileOpsOptions::permissionsCanBeDroppedWithoutTouchingTheOthers()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.preservePermissions"), false}});
    QVERIFY(policy.preserve.timestamps);
    QVERIFY(policy.preserve.attributes);
    QVERIFY(!policy.preserve.permissions);
}

void TstFileOpsOptions::nonBooleanMetadataValueKeepsTheItemEnabledAndIsReported()
{
    // 设置文件是可以被手改的。一个字符串 "no" 既不该被当成 false（用户看不出
    // 自己写错了），也不该让程序认不出这个键。
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.preserveTimestamps"), QStringLiteral("no")}});
    QVERIFY(policy.preserve.timestamps);
    QCOMPARE(policy.fallbacks.size(), 1);
    QVERIFY2(policy.fallbacks.first().contains(QStringLiteral("修改时间")),
             qPrintable(policy.fallbacks.first()));
}

// ---------------------------------------------------------------------------
// E 体积与条数确认阈值（第 4 条）
// ---------------------------------------------------------------------------

void TstFileOpsOptions::megabytesConvertToBytes()
{
    QCOMPARE(largeFileConfirmMegabytesToBytes(1), qint64(1024) * 1024);
    QCOMPARE(largeFileConfirmMegabytesToBytes(100), qint64(104857600));
    QCOMPARE(largeFileConfirmBytesToMegabytes(qint64(104857600)), 100);
}

void TstFileOpsOptions::zeroMegabytesMeansConfirmationIsOff()
{
    // 0 是「关闭确认」，不是「0 字节以上都要确认」——后者用户永远关不掉，
    // 只能把阈值设成一个天文数字。
    QCOMPARE(largeFileConfirmMegabytesToBytes(0), qint64(0));
    QCOMPARE(largeFileConfirmMegabytesToBytes(-1), qint64(0));
    QCOMPARE(largeFileConfirmBytesToMegabytes(0), 0);
    QCOMPARE(largeFileConfirmBytesToMegabytes(-5), 0);
}

void TstFileOpsOptions::largeFileConfirmationTriggersAtTheThreshold()
{
    FileOperationPolicy policy;
    QVERIFY(policy.needsLargeFileConfirmation(policy.largeFileConfirmBytes));
    QVERIFY(policy.needsLargeFileConfirmation(policy.largeFileConfirmBytes * 3));
}

void TstFileOpsOptions::largeFileConfirmationIgnoresSmallerFiles()
{
    FileOperationPolicy policy;
    QVERIFY(!policy.needsLargeFileConfirmation(policy.largeFileConfirmBytes - 1));
    QVERIFY(!policy.needsLargeFileConfirmation(0));
    QVERIFY(!policy.needsLargeFileConfirmation(1));
}

void TstFileOpsOptions::unknownSizeIsNotTreatedAsHuge()
{
    // 大小为负表示「不知道」。把未知当超大文件会让每一个拿不到大小的条目
    // 都弹一个框，而那种框用户只会闭着眼点掉。
    FileOperationPolicy policy;
    QVERIFY(!policy.needsLargeFileConfirmation(-1));
}

void TstFileOpsOptions::shuttingOffLargeFileConfirmationDisablesItEntirely()
{
    FileOperationPolicy policy;
    policy.largeFileConfirmBytes = 0;
    QVERIFY(!policy.needsLargeFileConfirmation(0));
    QVERIFY(!policy.needsLargeFileConfirmation(qint64(1024) * 1024 * 1024));
}

void TstFileOpsOptions::batchDeleteConfirmationTriggersAtTheThreshold()
{
    FileOperationPolicy policy;
    QVERIFY(policy.needsBatchDeleteConfirmation(policy.batchDeleteConfirmCount));
    QVERIFY(policy.needsBatchDeleteConfirmation(policy.batchDeleteConfirmCount + 1));
}

void TstFileOpsOptions::batchDeleteConfirmationIgnoresFewerItems()
{
    FileOperationPolicy policy;
    QVERIFY(!policy.needsBatchDeleteConfirmation(policy.batchDeleteConfirmCount - 1));
    QVERIFY(!policy.needsBatchDeleteConfirmation(1));
}

void TstFileOpsOptions::emptyBatchDeleteNeverAsks()
{
    FileOperationPolicy policy;
    QVERIFY(!policy.needsBatchDeleteConfirmation(0));
}

void TstFileOpsOptions::shuttingOffBatchDeleteConfirmationDisablesItEntirely()
{
    FileOperationPolicy policy;
    policy.batchDeleteConfirmCount = 0;
    QVERIFY(!policy.needsBatchDeleteConfirmation(0));
    QVERIFY(!policy.needsBatchDeleteConfirmation(500));
}

void TstFileOpsOptions::thresholdsRoundTripBetweenMegabytesAndBytes()
{
    // 设置里存的是整兆字节。往返不一致会让「应用之后读回来不一样」，
    // 而这个现象在界面上表现为「设置没保存」。
    for (int megabytes : {0, 1, 2, 20, 100, 512, 1024, 102400}) {
        FileOperationPolicy policy = FileOperationPolicy::fromValues(
                {{QStringLiteral("fileops.largeFileConfirmMegabytes"), megabytes}});
        QCOMPARE(largeFileConfirmBytesToMegabytes(policy.largeFileConfirmBytes), megabytes);
        QCOMPARE(policy.validate(), QStringList());
    }
}

void TstFileOpsOptions::thresholdThatIsNotAWholeNumberOfMegabytesIsReported()
{
    FileOperationPolicy policy;
    policy.largeFileConfirmBytes = 1048577; // 1 MiB + 1 字节：设置里存不下
    const QStringList problems = policy.validate();
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("整兆")), qPrintable(problems.first()));
}

void TstFileOpsOptions::negativeThresholdsAreReported()
{
    FileOperationPolicy policy;
    policy.largeFileConfirmBytes = -1;
    policy.batchDeleteConfirmCount = -3;
    const QStringList problems = policy.validate();
    QCOMPARE(problems.size(), 2);
    QVERIFY2(problems.join(QStringLiteral(" ")).contains(QStringLiteral("不得为负")),
             qPrintable(problems.join(QStringLiteral(" "))));
}

void TstFileOpsOptions::negativeThresholdValueFallsBackToTheDefault()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.largeFileConfirmMegabytes"), -5},
             {QStringLiteral("fileops.batchDeleteConfirmCount"), -1}});
    QCOMPARE(policy.largeFileConfirmBytes, DefaultLargeFileConfirmBytes);
    QCOMPARE(policy.batchDeleteConfirmCount, DefaultBatchDeleteConfirmCount);
    QCOMPARE(policy.fallbacks.size(), 2);
}

// ---------------------------------------------------------------------------
// F 操作后的校验方式（第 5 条）
// ---------------------------------------------------------------------------

void TstFileOpsOptions::verifyModeIdentifiersAreStable()
{
    QCOMPARE(verifyIdentifier(VerifyMode::None), QStringLiteral("none"));
    QCOMPARE(verifyIdentifier(VerifyMode::Size), QStringLiteral("size"));
    QCOMPARE(verifyIdentifier(VerifyMode::Crc), QStringLiteral("crc"));
}

void TstFileOpsOptions::verifyModeRoundTripsThroughItsIdentifier()
{
    for (VerifyMode mode : {VerifyMode::None, VerifyMode::Size, VerifyMode::Crc}) {
        VerifyMode parsed = VerifyMode::Crc;
        QVERIFY(verifyModeFromIdentifier(verifyIdentifier(mode), &parsed));
        QCOMPARE(verifyIdentifier(parsed), verifyIdentifier(mode));
    }
}

void TstFileOpsOptions::verifyLabelsDistinguishTheThreeModes()
{
    QSet<QString> unique;
    for (VerifyMode mode : {VerifyMode::None, VerifyMode::Size, VerifyMode::Crc}) {
        const QString label = verifyModeLabel(mode);
        QVERIFY2(!label.isEmpty(), verifyModeIdentifier(mode).toUtf8().constData());
        unique.insert(label);
    }
    // 三种模式必须能被用户分辨出来：「比对大小」与「比对 CRC」写成同一句话
    // 就等于没有这个选项。
    QCOMPARE(unique.size(), 3);
}

void TstFileOpsOptions::unknownVerifyModeKeepsTheDefault()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.verifyAfterCopy"), QStringLiteral("md5")}});
    // 认不出的校验方式必须回退到「不校验」这个出厂值，而不是「碰巧是最后一个」。
    QCOMPARE(verifyIdentifier(policy.verifyMode), QStringLiteral("none"));
    QCOMPARE(policy.fallbacks.size(), 1);
}

void TstFileOpsOptions::verifyModeIsReadFromItsOwnKey()
{
    FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.verifyAfterCopy"), QStringLiteral("crc")}});
    QCOMPARE(verifyIdentifier(policy.verifyMode), QStringLiteral("crc"));
    // 只有校验方式被改，别的轴一律保持出厂值：把校验那一格改回出厂的
    // 「不校验」之后，六条轴应当与出厂策略逐字相同。
    const QStringList expected = policyAxes(FileOperationPolicy());
    QStringList actual = policyAxes(policy);
    QVERIFY(actual != expected);
    actual[5] = verifyIdentifier(VerifyMode::None);
    QCOMPARE(actual, asList(expected));
}

void TstFileOpsOptions::eachVerifyModeIsAcceptedVerbatim()
{
    for (VerifyMode mode : {VerifyMode::None, VerifyMode::Size, VerifyMode::Crc}) {
        FileOperationPolicy policy = FileOperationPolicy::fromValues(
                {{QStringLiteral("fileops.verifyAfterCopy"), verifyModeIdentifier(mode)}});
        QCOMPARE(verifyIdentifier(policy.verifyMode), verifyIdentifier(mode));
        QCOMPARE(policy.fallbacks, QStringList());
    }
}

// ---------------------------------------------------------------------------
// G 从设置值构造与设置仓库接线
// ---------------------------------------------------------------------------

void TstFileOpsOptions::emptyValuesProduceTheConservativeDefaults()
{
    const FileOperationPolicy policy = FileOperationPolicy::fromValues({});
    QCOMPARE(policyAxes(policy), asList(policyAxes(FileOperationPolicy())));
}

void TstFileOpsOptions::everyKeyInTheTableIsRegisteredInTheRepository()
{
    // 这是整套用例里最要紧的一条：策略读的键必须真的在设置仓库里登记过。
    // 少了它，策略会永远读到默认值——而界面上那个选项看起来完全正常。
    for (const FileOperationKey &entry : fileOperationKeyTable()) {
        const Settings::OptionDefinition *definition = Settings::OptionsRepository::definition(entry.key);
        QVERIFY2(definition, qPrintable(entry.key));
        QCOMPARE(definition->category, QStringLiteral("fileops"));
    }
}

void TstFileOpsOptions::repositoryDefaultsMatchTheServiceDefaults()
{
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.deleteMode")).toString(), QStringLiteral("trash"));
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.overwritePolicy")).toString(), QStringLiteral("ask"));
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.preserveTimestamps")).toBool(), true);
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.preserveAttributes")).toBool(), true);
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.preservePermissions")).toBool(), true);
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.largeFileConfirmMegabytes")).toInt(), 100);
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.batchDeleteConfirmCount")).toInt(), 20);
    QCOMPARE(repositoryDefault(QStringLiteral("fileops.verifyAfterCopy")).toString(), QStringLiteral("none"));

    // 反过来也要成立：把仓库的出厂值整份喂进服务层，必须得到同一份策略。
    QVariantMap values;
    for (const QString &key : fileOperationKeys())
        values.insert(key, repositoryDefault(key));
    const FileOperationPolicy policy = FileOperationPolicy::fromValues(values);
    QCOMPARE(policyAxes(policy), asList(policyAxes(FileOperationPolicy())));
    QCOMPARE(policy.fallbacks, QStringList());
}

void TstFileOpsOptions::repositoryDefaultValuesPassValidation()
{
    for (const FileOperationKey &entry : fileOperationKeyTable()) {
        const Settings::OptionDefinition *definition = Settings::OptionsRepository::definition(entry.key);
        QVERIFY(definition);
        QCOMPARE(Settings::OptionsRepository::validate(entry.key, definition->defaultValue), QString());
    }
}

void TstFileOpsOptions::applyingTheRepositoryChangesThePolicy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Settings::OptionsRepository repository({directory.path(), false});
    QVERIFY(repository.load().ok);
    const Settings::OperationResult applied = repository.apply(
            {{QStringLiteral("fileops.deleteMode"), QStringLiteral("permanent")},
             {QStringLiteral("fileops.overwritePolicy"), QStringLiteral("skip")},
             {QStringLiteral("fileops.preserveTimestamps"), false},
             {QStringLiteral("fileops.largeFileConfirmMegabytes"), 5},
             {QStringLiteral("fileops.batchDeleteConfirmCount"), 3},
             {QStringLiteral("fileops.verifyAfterCopy"), QStringLiteral("crc")}});
    QVERIFY2(applied.ok, qPrintable(applied.error));

    const FileOperationPolicy policy = FileOperationPolicy::fromValues(repository.values());
    QCOMPARE(deleteIdentifier(policy.deleteMode), QStringLiteral("permanent"));
    QCOMPARE(overwriteIdentifier(policy.overwritePolicy), QStringLiteral("skip"));
    QVERIFY(!policy.preserve.timestamps);
    QCOMPARE(policy.largeFileConfirmBytes, qint64(5) * 1024 * 1024);
    QCOMPARE(policy.batchDeleteConfirmCount, 3);
    QCOMPARE(verifyIdentifier(policy.verifyMode), QStringLiteral("crc"));
    // 改完之后策略确实不再满足「默认保守」——这条同时说明 A 组的契约
    // 不是一个无论输入都成立的断言。
    QCOMPARE(policy.safetyContractViolations().size(), 2);
    QCOMPARE(policy.fallbacks, QStringList());
}

void TstFileOpsOptions::repositoryChoiceListMatchesTheAcceptedIdentifiers()
{
    struct Row {
        const char *key;
        QStringList accepted;
    };
    const QVector<Row> rows {
        { "fileops.deleteMode",
          QStringList{deleteModeIdentifier(DeleteMode::Trash), deleteModeIdentifier(DeleteMode::Permanent)} },
        { "fileops.overwritePolicy",
          QStringList{overwritePolicyIdentifier(OverwritePolicy::Ask),
                      overwritePolicyIdentifier(OverwritePolicy::Overwrite),
                      overwritePolicyIdentifier(OverwritePolicy::Skip)} },
        { "fileops.verifyAfterCopy",
          QStringList{verifyModeIdentifier(VerifyMode::None), verifyModeIdentifier(VerifyMode::Size),
                      verifyModeIdentifier(VerifyMode::Crc)} }
    };
    for (const Row &row : rows) {
        const Settings::OptionDefinition *definition =
                Settings::OptionsRepository::definition(QString::fromLatin1(row.key));
        QVERIFY(definition);
        // 两边互为子集：仓库里挂的每一个选项服务层都认，服务层产出的每一个
        // 标识符仓库里都列了。少一半的话，界面会显示一个存不下去的值。
        for (const QString &choice : definition->choices) {
            QVERIFY2(row.accepted.contains(choice),
                     qPrintable(QString::fromLatin1(row.key) + QStringLiteral(" -> ") + choice));
        }
        for (const QString &choice : row.accepted) {
            QVERIFY2(definition->choices.contains(choice),
                     qPrintable(QString::fromLatin1(row.key) + QStringLiteral(" <- ") + choice));
        }
    }
}

void TstFileOpsOptions::policyReadsValuesThroughTheKeyTable()
{
    QCOMPARE(fileOperationKey(QStringLiteral("deleteMode")), QStringLiteral("fileops.deleteMode"));
    // 表给出的键改得动策略，而一个手写的「看起来对」的键改不动它——
    // 这就是「策略按表取键」而不是「按字面量取键」的证据。
    const FileOperationPolicy throughTable = FileOperationPolicy::fromValues(
            {{fileOperationKey(QStringLiteral("deleteMode")), QStringLiteral("permanent")}});
    QCOMPARE(deleteIdentifier(throughTable.deleteMode), QStringLiteral("permanent"));

    const FileOperationPolicy handWritten = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.delete"), QStringLiteral("permanent")}});
    QCOMPARE(deleteIdentifier(handWritten.deleteMode), QStringLiteral("trash"));
    QCOMPARE(handWritten.fallbacks, QStringList());
}

void TstFileOpsOptions::valuesMapWithUnknownKeysIsIgnored()
{
    const FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.notAKey"), 1},
             {QStringLiteral("display.theme"), QStringLiteral("dark")}});
    QCOMPARE(policyAxes(policy), asList(policyAxes(FileOperationPolicy())));
    QCOMPARE(policy.fallbacks, QStringList());
}

void TstFileOpsOptions::wrongTypeForAChoiceIsReportedAndFallsBack()
{
    const FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.overwritePolicy"), 3}});
    QCOMPARE(overwriteIdentifier(policy.overwritePolicy), QStringLiteral("ask"));
    QCOMPARE(policy.fallbacks.size(), 1);
}

void TstFileOpsOptions::unknownChoiceIsReportedWithoutBeingAdopted()
{
    const FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.deleteMode"), QStringLiteral("Trash")}});
    // 大小写不同就是另一个标识符——「看起来像」不等于「是」。
    QCOMPARE(deleteIdentifier(policy.deleteMode), QStringLiteral("trash"));
    QVERIFY(deleteIdentifier(policy.deleteMode) != QStringLiteral("permanent"));
    QCOMPARE(policy.fallbacks.size(), 1);
}

void TstFileOpsOptions::repositoryRoundTripKeepsThePolicyStable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Settings::OptionsRepository repository({directory.path(), false});
    QVERIFY(repository.load().ok);
    QVERIFY(repository.apply({{QStringLiteral("fileops.batchDeleteConfirmCount"), 7},
                              {QStringLiteral("fileops.preservePermissions"), false}}).ok);
    const FileOperationPolicy before = FileOperationPolicy::fromValues(repository.values());

    Settings::OptionsRepository reread({directory.path(), false});
    QVERIFY(reread.load().ok);
    const FileOperationPolicy after = FileOperationPolicy::fromValues(reread.values());
    QCOMPARE(policyAxes(after), asList(policyAxes(before)));
    QCOMPARE(after.batchDeleteConfirmCount, 7);
    QVERIFY(!after.preserve.permissions);
}

void TstFileOpsOptions::fallbacksAreEmptyForAWellFormedMap()
{
    const FileOperationPolicy policy = FileOperationPolicy::fromValues(
            {{QStringLiteral("fileops.deleteMode"), QStringLiteral("permanent")},
             {QStringLiteral("fileops.overwritePolicy"), QStringLiteral("overwrite")},
             {QStringLiteral("fileops.preserveTimestamps"), false},
             {QStringLiteral("fileops.preserveAttributes"), false},
             {QStringLiteral("fileops.preservePermissions"), false},
             {QStringLiteral("fileops.largeFileConfirmMegabytes"), 0},
             {QStringLiteral("fileops.batchDeleteConfirmCount"), 0},
             {QStringLiteral("fileops.verifyAfterCopy"), QStringLiteral("size")}});
    QCOMPARE(policy.fallbacks, QStringList());
    QCOMPARE(policy.validate(), QStringList());
}

// ---------------------------------------------------------------------------
// H 键表自检与反向验证
// ---------------------------------------------------------------------------

void TstFileOpsOptions::builtinKeyTableIsClean()
{
    QCOMPARE(validateFileOperationKeyTable(fileOperationKeyTable()), QStringList());
}

void TstFileOpsOptions::builtinKeyTableCoversEightKeys()
{
    // 八条键对应五条完成标准：删除方式 1、覆盖策略 1、元数据 3、阈值 2、校验 1。
    const QStringList keys = fileOperationKeys();
    QCOMPARE(keys.size(), 8);
    QSet<QString> unique;
    for (const QString &key : keys) {
        QVERIFY2(key.startsWith(QStringLiteral("fileops.")), qPrintable(key));
        unique.insert(key);
    }
    QCOMPARE(unique.size(), keys.size());
}

void TstFileOpsOptions::tableShortNamesResolveToFullKeys()
{
    for (const FileOperationKey &entry : fileOperationKeyTable())
        QCOMPARE(fileOperationKey(entry.identifier), entry.key);
}

void TstFileOpsOptions::unknownShortNameResolvesToEmpty()
{
    // 返回空串而不是「原样返回」：调用方拿到空串就知道表里没有这一项，
    // 原样返回会让它去读一个根本不存在的设置键而毫无察觉。
    QCOMPARE(fileOperationKey(QStringLiteral("preserveAcl")), QString());
    QCOMPARE(fileOperationKey(QString()), QString());
}

void TstFileOpsOptions::keyTableSelfCheckRejectsEmptyIdentifier()
{
    QVector<FileOperationKey> table = fileOperationKeyTable();
    table[0].identifier.clear();
    const QStringList problems = validateFileOperationKeyTable(table);
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("短名")), qPrintable(problems.first()));
}

void TstFileOpsOptions::keyTableSelfCheckRejectsDuplicateIdentifier()
{
    QVector<FileOperationKey> table = fileOperationKeyTable();
    table[1].identifier = table[0].identifier;
    const QStringList problems = validateFileOperationKeyTable(table);
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("重复")), qPrintable(problems.first()));
}

void TstFileOpsOptions::keyTableSelfCheckRejectsDuplicateKey()
{
    QVector<FileOperationKey> table = fileOperationKeyTable();
    table[2].key = table[0].key;
    const QStringList problems = validateFileOperationKeyTable(table);
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("重复")), qPrintable(problems.first()));
}

void TstFileOpsOptions::keyTableSelfCheckRejectsMissingPrefix()
{
    QVector<FileOperationKey> table = fileOperationKeyTable();
    table[3].key = QStringLiteral("preserveAttributes");
    const QStringList problems = validateFileOperationKeyTable(table);
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("fileops.")), qPrintable(problems.first()));
}

void TstFileOpsOptions::keyTableSelfCheckRejectsEmptyPurpose()
{
    QVector<FileOperationKey> table = fileOperationKeyTable();
    table[4].purpose.clear();
    const QStringList problems = validateFileOperationKeyTable(table);
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("说明")), qPrintable(problems.first()));
}

void TstFileOpsOptions::keyTableSelfCheckRejectsEmptyTable()
{
    // 空表是最坏的一种「坏」：策略会静默回退到全部默认值，
    // 于是界面上改了什么都不生效，而没有任何一处报错。
    const QStringList problems = validateFileOperationKeyTable({});
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("空")), qPrintable(problems.first()));
}

// ---------------------------------------------------------------------------
// I 源码级护栏
// ---------------------------------------------------------------------------

QString TstFileOpsOptions::codeRoot()
{
    return QString::fromUtf8(LQCOMPARE_CODE_ROOT);
}

QString TstFileOpsOptions::readSource(const QString &relativePath)
{
    QFile file(codeRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

QString TstFileOpsOptions::stripComments(const QString &source)
{
    QString result;
    result.reserve(source.size());
    int index = 0;
    while (index < source.size()) {
        if (source.at(index) == QLatin1Char('/') && index + 1 < source.size()
            && source.at(index + 1) == QLatin1Char('*')) {
            index += 2;
            while (index < source.size()
                   && !(source.at(index) == QLatin1Char('*') && index + 1 < source.size()
                        && source.at(index + 1) == QLatin1Char('/')))
                ++index;
            index += 2;
            continue;
        }
        if (source.at(index) == QLatin1Char('/') && index + 1 < source.size()
            && source.at(index + 1) == QLatin1Char('/')) {
            while (index < source.size() && source.at(index) != QLatin1Char('\n')) ++index;
            continue;
        }
        result.append(source.at(index));
        ++index;
    }
    return result;
}

void TstFileOpsOptions::moduleNeverTouchesTheFileSystem()
{
    // 这个模块的输入永远是「已经读进来的值」。它的任何一个执行文件操作的分支
    // 都意味着「默认行为」开始自己决定怎么做，而那正是调用方要自己负责的事。
    const QStringList forbidden {QStringLiteral("QFile"), QStringLiteral("QDir"),
                                 QStringLiteral("QTextStream"), QStringLiteral("QDataStream"),
                                 QStringLiteral("readAll"), QStringLiteral("QSaveFile")};
    for (const QString &path : QStringList{QStringLiteral("Services/Files/fileopsoptions.h"),
                                           QStringLiteral("Services/Files/fileopsoptions.cpp")}) {
        const QString source = readSource(path);
        QVERIFY2(!source.isEmpty(), qPrintable(path));
        const QString code = stripComments(source);
        for (const QString &token : forbidden) {
            QVERIFY2(!code.contains(token),
                     qPrintable(path + QStringLiteral(" 使用了 ") + token));
        }
    }
}

void TstFileOpsOptions::sourceGuardWouldCatchAnInjectedRead()
{
    // 成对断言，两件事都要证明：
    //   1. stripComments 不会把「代码里的」QFile 一起去掉（否则护栏永远绿）；
    //   2. stripComments 确实会把「注释里的」QFile 去掉（否则护栏永远红，
    //      因为本模块的头文件里逐字写着这些词）。
    const QString injected = QStringLiteral(
            "// QFile here is only a comment mention\n"
            "QByteArray payload = QFile(path).readAll();\n");
    QCOMPARE(injected.count(QStringLiteral("QFile")), 2);
    const QString code = stripComments(injected);
    QCOMPARE(code.count(QStringLiteral("QFile")), 1);
    QVERIFY(code.contains(QStringLiteral("QFile(path).readAll()")));
    QVERIFY(!code.contains(QStringLiteral("comment mention")));
}

void TstFileOpsOptions::moduleNeverIncludesTheSettingsRepository()
{
    // 「策略读的键」与「设置仓库登记的键」是两边各自的事实来源，
    // 靠键表对齐（见 G 组）。让本模块直接 include 仓库会把这层关系变成
    // 一条编译期依赖，而服务层不该认识设置存储的实现。
    for (const QString &path : QStringList{QStringLiteral("Services/Files/fileopsoptions.h"),
                                           QStringLiteral("Services/Files/fileopsoptions.cpp")}) {
        const QString code = stripComments(readSource(path));
        QVERIFY2(!code.contains(QStringLiteral("optionsrepository.h")), qPrintable(path));
        QVERIFY2(!code.contains(QStringLiteral("OptionsRepository")), qPrintable(path));
    }
}

void TstFileOpsOptions::moduleNeverIncludesAnyViewHeader()
{
    for (const QString &path : QStringList{QStringLiteral("Services/Files/fileopsoptions.h"),
                                           QStringLiteral("Services/Files/fileopsoptions.cpp")}) {
        const QString code = stripComments(readSource(path));
        QVERIFY2(!code.contains(QStringLiteral("\"Views/")), qPrintable(path));
        QVERIFY2(!code.contains(QStringLiteral("QMessageBox")), qPrintable(path));
    }
}

QTEST_MAIN(TstFileOpsOptions)
