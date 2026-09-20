#ifndef LQCOMPARE_TST_SINGLEINSTANCE_H
#define LQCOMPARE_TST_SINGLEINSTANCE_H

#include <QtTest>

///
/// \brief 单实例机制的测试（PRD: PLAT-006 第 1、2、4 条）。
///
/// 本套件刻意分成两类用例，比例大约是 3:2：
///
///   * **单进程可判定的部分**：标识符推导、线协议编解码、分帧、退出码表与它的
///     自检、命令行开关、置前策略。这部分是本条目里最容易写错、又最不容易被
///     人手工发现的规则（推错了标识只是「第二个实例连不上第一个」，
///     解码宽松只是「某天有个客户端把内存撑满了」）。
///   * **真的需要第二个进程的部分**：转交、退出码、超时降级、崩溃遗留标识的
///     回收。这几件事**不能**在同进程里测——发送端是阻塞等待应答的，而
///     服务器需要事件循环才能收下连接，两者在同一个线程上必然死锁。
///     本套件因此会把**自己**再启动一次（带一个环境变量），用真正的两个
///     进程去跑这几条路径。子进程模式的实现就在 `tst_singleinstance.cpp`
///     里，没有往生产代码里加任何测试钩子。
///
class Tst_SingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // --- A 标识符：端点名与共享内存键 -----------------------------------------
    void sanitizeKeepsSafeCharacters();
    void sanitizeReplacesPathSeparatorsAndSpaces();
    void sanitizeAppendsFingerprintWhenItReplacedSomething();
    void sanitizeDistinguishesTwoNonAsciiNames();
    void sanitizeReturnsEmptyForBlankInput();
    void fingerprintIsStableForAKnownInput();
    void endpointNameCombinesPrefixUserAndSeed();
    void endpointNameStaysWithinTheLimitAndKeepsDistinctness();
    void sharedMemoryKeyRespectsThePosixNameLimit();
    void identifierDistinguishesRawInputFromSanitizedInput();
    void identifierKeepsFieldBoundaries();
    void userScopeFallsBackToAnonymous();

    // --- B 线协议：编解码 -----------------------------------------------------
    void requestRoundTripsWorkingDirectoryAndArguments();
    void requestRoundTripsEmptyArgumentList();
    void requestRoundTripsEmptyStringArgument();
    void requestRoundTripsUnicodeArguments();
    void requestRoundTripsManyArguments();
    void replyRoundTripsAcceptance();
    void replyRoundTripsRejectionDetail();
    void decodeRejectsShortFrame();
    void decodeRejectsWrongMagic();
    void decodeRejectsWrongKind();
    void decodeRejectsVersionMismatch();
    void decodeRejectsOversizedDeclaredPayload();
    void decodeRejectsDeclaredAndActualMismatch();
    void decodeRejectsTruncatedString();
    void decodeRejectsAbsurdArgumentCount();
    void decodeRejectsInvalidUtf8();
    void decodeRejectsTrailingPayloadBytes();
    void decodeFailureLeavesOutputUntouched();
    void describeRelayRequestMentionsCountAndDirectory();

    // --- C 分帧：缓冲区里有没有一整帧 -----------------------------------------
    void inspectWaitsForTheFullHeader();
    void inspectWaitsForTheWholePayload();
    void inspectAcceptsExactlyOneFrame();
    void inspectReportsTrailingBytesAsASeparateFrame();
    void inspectRejectsWrongMagic();
    void inspectRejectsOversizedDeclaration();

    // --- D 退出码表 ------------------------------------------------------------
    void exitCodeTableCoversEveryStatusOnce();
    void exitCodeIdentifiersAreDistinct();
    void exitCodesSitInTheirOwnBand();
    void notAttemptedCarriesNoExitCode();
    void cliContractUpperBoundIsPinned();
    void statusTextsAreDistinctAndNonEmpty();

    // --- E 退出码表自检的反向验证 ---------------------------------------------
    void selfCheckAcceptsTheBuiltInTable();
    void selfCheckReportsAMissingStatus();
    void selfCheckReportsAnExtraRow();
    void selfCheckReportsDuplicateIdentifiers();
    void selfCheckReportsMissingIdentifiers();
    void selfCheckReportsDuplicateCodes();
    void selfCheckReportsCodesInsideTheCliBand();
    void selfCheckReportsCodesOutsideTheBand();
    void selfCheckReportsASharedText();

    // --- F 命令行开关 ----------------------------------------------------------
    void switchesHaveNamesAndDescriptions();
    void noSwitchUsesTheFallback();
    void switchesEnableAndDisable();
    void theLastTableRowWins();
    void unknownSwitchNamesAreIgnored();
    void resolutionWorksOnASynthesizedTable();

    // --- G 置前策略 ------------------------------------------------------------
    void alwaysPolicyAlwaysRaises();
    void neverPolicyNeverRaises();
    void quietInputAllowsRaising();
    void recentInputSuppressesRaising();
    void unknownInputAgeRaises();
    void policyIdentifiersAndTextsAreDistinct();

    // --- H 角色与启动结论 -----------------------------------------------------
    void roleIdentifiersAreDistinctAndNonEmpty();
    void fallbackIdentifiersAreDistinct();
    void aDisabledReportCarriesNoRelay();
    void theReportExitCodeFollowsTheRelayStatus();

    // --- I 真的第二个进程：首个实例与转交 -------------------------------------
    void aLoneProcessBecomesPrimary();
    void primaryTakesAnIdentifierAndListensOnAnEndpoint();
    void releasingTheIdentifierLetsTheNextProcessBecomePrimary();
    void aSecondProcessIsToldThatItsArgumentsWereDelivered();
    void theFirstProcessReceivesTheArgumentsAndTheWorkingDirectory();
    void theFirstProcessIsAskedToRaiseItsWindow();
    void aSecondProcessExitsWithTheDeliveredCode();
    void theReplyCarriesTheArgumentCount();

    // --- J 真的第二个进程：失败与降级 -----------------------------------------
    void garbageFromAClientIsRejectedAndReported();
    void defaultFallbackStartsANewInstanceInsteadOfFailing();
    void exitWithRelayCodeTurnsADeliveryFailureIntoAnExitCode();
    void aSilentFirstProcessLeadsToATimeout();
    void aRejectingFirstProcessIsReportedAsRejected();
    void aFirstProcessThatMiscountsTheArgumentsIsRejected();
    void aClientThatNeverSendsIsDroppedByTheDeadline();
    void disablingTheMechanismTakesNoIdentifier();
    void twoDisabledGuardsDoNotCollide();

    // --- K 崩溃遗留标识的回收 -------------------------------------------------
    void aCrashedProcessLeavesItsIdentifierBehind();
    void theStaleIdentifierIsRecoveredByTheNextProcess();
    void aRecoveredFirstProcessCanStillServe();
    void recoveryNeverStealsALivingIdentifier();

    void repeatedStartKeepsThePrimaryAndListener();
    void restartingWithAnotherSeedReleasesTheOldIdentifier();
    void disablingAPrimaryReleasesAllResources();
    void manyNormalLifetimesDoNotExhaustIdentifiers();
    void normalChildShutdownReleasesItsIdentifier();
    void manyCrashRecoveriesKeepServing();
    void concurrentStartsElectOnlyOnePrimary();
    void aPrimaryStillStartingGetsTimeToListen();
    void realRelayPreservesNewlinesAndEmptyArguments();

    // --- L 源码级护栏 ---------------------------------------------------------
    void theModuleIncludesNoGuiHeaders();
    void theModuleCarriesNoPlatformIfdef();
    void theModuleAvoidsQHashForIdentity();
    void theSourceGuardActuallyFailsOnABrokenSample();

private:
    /// 每个用例一个独立的种子：用例之间（以及与真实运行的程序之间）不会
    /// 互相顶掉，清理也简单——只回收自己那一个标识。
    QString m_seed;
    int m_counter = 0;
    QString m_outputDirectory;

    QString outputPath(const QString &name) const;
    QString readSourceFile(const QString &relativePath) const;
    QString readOutput(const QString &path) const;
    QString makeSeed();
};
#endif // LQCOMPARE_TST_SINGLEINSTANCE_H
