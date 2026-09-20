#ifndef LQCOMPARE_TST_PATHNAME_H
#define LQCOMPARE_TST_PATHNAME_H

#include <QObject>
#include <QtTest>

///
/// \brief 路径名称处理的测试（PRD: PLAT-007）。
///
/// 分五组：
///   1. 字节保真——无效 UTF-8 字节不丢失，这是本模块存在的理由。
///   2. 显示安全化——换行、控制字符、首尾空格要能看见。
///   3. Unicode 规范化——NFC 与 NFD 要判为同一个名字。
///   4. 文件名校验——每类问题都要有明确原因与可执行建议。
///   5. 真实文件系统——名字真的能建出来、读回来、改回去。
///
class TstPathName : public QObject
{
    Q_OBJECT

private slots:
    // --- 1. 字节保真 --------------------------------------------------------
    void asciiBytesRoundTripThroughText();
    void validUtf8RoundTripThroughText();
    void fourByteUtf8RoundTripsThroughSurrogatePair();
    void invalidBytesSurviveRoundTrip();
    void overlongEncodingIsTreatedAsInvalid();
    void surrogateCodePointInUtf8IsTreatedAsInvalid();
    void truncatedSequenceKeepsEveryByte();
    void strayContinuationByteIsKept();
    void toUtf8WouldLoseRawBytes();
    void hasRawBytesOnlyForEscapeRange();
    void unpairedSurrogateOutsideEscapeRangeBecomesReplacement();
    void utf8BoundaryCodePointsDecodeCorrectly();

    // --- 2. 显示安全化 ------------------------------------------------------
    void displayEscapesLineBreaksAndTabs();
    void displayEscapesOtherControlCharacters();
    void displayEscapesRawBytesAsHex();
    void displayMarksLeadingAndTrailingSpaces();
    void displayLeavesInteriorSpacesAlone();
    void displayOfAllSpaceName();
    void displayDiffersFromActualDetectsChanges();

    // --- 3. Unicode 规范化 --------------------------------------------------
    void composedAndDecomposedNamesCompareEqual();
    void differentNamesStayDifferent();
    void normalizationKeepsRawBytes();
    void equalNamesHonoursCaseSensitivity();

    // --- 4. 文件名校验 ------------------------------------------------------
    void emptyNameIsRejected();
    void dotAndDotDotAreRejected();
    void controlCharactersAreRejected();
    void forbiddenCharactersAreRejected();
    void trailingSpaceOrDotIsRejected();
    void interiorSpaceAndDotAreAccepted();
    void reservedDeviceNamesAreRejected();
    void overlongNameIsRejected();
    void rawByteEscapeIsNotAControlCharacter();
    void problemPositionPointsAtTheCharacter();
    void everyProblemHasItsOwnAdvice();
    void checkFileNameAgreesWithTheThinWrappers();

    // --- 5. 真实文件系统 ----------------------------------------------------
    void realNameWithSpacesAndQuotesRoundTrips();
    void realNameWithTrailingSpaceRoundTrips();
    void realComposedNameIsFoundByOtherForm();
    void realRawByteNameRoundTripsOnPosix();
};

#endif // LQCOMPARE_TST_PATHNAME_H
