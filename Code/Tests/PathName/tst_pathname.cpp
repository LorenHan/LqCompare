#include "tst_pathname.h"

#include "pathname.h"
#include "pathutils.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace LqCompare::Files;
// 文件名校验在 PathUtils 命名空间里（路径规则与名称规则同源）。
using namespace LqCompare::Files::PathUtils;

namespace QTest {

/// 让失败信息里直接显示问题标识，而不是「Compared values are not the same」。
template <>
char *toString(const FileNameProblem &problem)
{
    return qstrdup(PathUtils::fileNameProblemIdentifier(problem));
}

} // namespace QTest

namespace {

/// 名字里承载了一个原始字节时的私存码位。
QChar rawByte(char value)
{
    return QChar(static_cast<ushort>(0xDC00 + static_cast<uchar>(value)));
}

} // namespace

// -----------------------------------------------------------------------------
// 1. 字节保真
// -----------------------------------------------------------------------------

void TstPathName::asciiBytesRoundTripThroughText()
{
    const QByteArray bytes = QByteArrayLiteral("/home/u/notes.txt");

    const QString text = PathName::fromNativeBytes(bytes);
    QCOMPARE(text, QStringLiteral("/home/u/notes.txt"));
    QVERIFY(!PathName::hasRawBytes(text));
    QCOMPARE(PathName::toNativeBytes(text), bytes);
}

void TstPathName::validUtf8RoundTripThroughText()
{
    // 中文名、带声调的拉丁字母、以及需要一对代理才能表示的字符。
    const QStringList samples = {
        QStringLiteral("/tmp/我的报告.txt"),
        QStringLiteral("/tmp/café.txt"),
        QStringLiteral("/tmp/日本語のファイル.txt"),
        QStringLiteral("/tmp/mixed 中文 café.txt"),
    };

    for (const QString &sample : samples) {
        const QByteArray bytes = sample.toUtf8();
        const QString decoded = PathName::fromNativeBytes(bytes);

        QCOMPARE(decoded, sample);
        QVERIFY2(!PathName::hasRawBytes(decoded),
                 "合法的 UTF-8 不该产生私存码位——产生了说明解码把正常字节误判成无效");
        QCOMPARE(PathName::toNativeBytes(decoded), bytes);
    }
}

void TstPathName::fourByteUtf8RoundTripsThroughSurrogatePair()
{
    // U+1F600（笑脸）在 UTF-8 里是 4 字节 F0 9F 98 80，在 QString 里是一对代理。
    // 这条路径最容易出错的地方是「4 字节序列被当成两个无效字节」——
    // 那样会导致 emoji 文件名在我们这里变成一串 \xNN。
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80.txt");
    // 代理对 2 个码元 + ".txt" 4 个 = 6。写成 3 是我一开始把 ".txt" 数成了 1 个字符。
    QCOMPARE(emoji.size(), 6);

    const QByteArray bytes = QByteArrayLiteral("\xF0\x9F\x98\x80.txt");
    const QString decoded = PathName::fromNativeBytes(bytes);

    QVERIFY(!PathName::hasRawBytes(decoded));
    QCOMPARE(decoded, emoji);
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::invalidBytesSurviveRoundTrip()
{
    // 这是本模块存在的理由：无效 UTF-8 字节必须原样带过去。
    //
    // 真实场景：Linux 上一个由别的程序（或旧工具链）创建的、名字里带 0xFF
    // 的文件。QString::fromUtf8 会把它变成 U+FFFD，之后再也拼不出原名。
    QByteArray bytes = QByteArrayLiteral("bad");
    bytes.append(static_cast<char>(0xFF));
    bytes.append(static_cast<char>(0xFE));
    bytes.append(QByteArrayLiteral(".txt"));

    const QString decoded = PathName::fromNativeBytes(bytes);

    QVERIFY2(PathName::hasRawBytes(decoded), "无效字节没有被标记为原始字节");
    QCOMPARE(decoded.size(), 9); // "bad" + 2 个私存码位 + ".txt"
    QCOMPARE(decoded.at(3), rawByte(static_cast<char>(0xFF)));
    QCOMPARE(decoded.at(4), rawByte(static_cast<char>(0xFE)));

    // 关键断言：往返之后字节完全一致。
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::overlongEncodingIsTreatedAsInvalid()
{
    // C0 80 是 U+0000 的「过长编码」。合法的 UTF-8 里 U+0000 只能是 00。
    //
    // 不判过长会有两个后果，都很严重：
    //   1. 原始字节 C0 80 丢失，往返不一致；
    //   2. 解码结果里凭空出现一个 NUL，而 NUL 在路径里是终止符语义——
    //      系统调用会在那里截断。
    QByteArray bytes = QByteArrayLiteral("x");
    bytes.append(static_cast<char>(0xC0));
    bytes.append(static_cast<char>(0x80));

    const QString decoded = PathName::fromNativeBytes(bytes);

    QCOMPARE(decoded.size(), 3);
    QVERIFY(!decoded.contains(QChar(0))); // 不能凭空出现 NUL
    QCOMPARE(decoded.at(1), rawByte(static_cast<char>(0xC0)));
    QCOMPARE(decoded.at(2), rawByte(static_cast<char>(0x80)));
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::surrogateCodePointInUtf8IsTreatedAsInvalid()
{
    // ED A0 80 是 U+D800 的 UTF-8 编码。UTF-8 里不该出现代理码位，
    // 而且放行它会与我们的私存方案直接撞车（私存用的也是 U+DC00 段）——
    // 那就再也分不清「承载的一个字节」与「名字里真的有一个代理字符」。
    QByteArray bytes;
    bytes.append(static_cast<char>(0xED));
    bytes.append(static_cast<char>(0xA0));
    bytes.append(static_cast<char>(0x80));

    const QString decoded = PathName::fromNativeBytes(bytes);

    QCOMPARE(decoded.size(), 3);
    for (int i = 0; i < 3; ++i)
        QVERIFY(PathName::hasRawBytes(QString(decoded.at(i))));
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::truncatedSequenceKeepsEveryByte()
{
    // 多字节序列被截断时，**每一个**字节都要各自承载，不能整个丢掉，
    // 也不能只承载前面那个首字节。
    QByteArray bytes = QByteArrayLiteral("a");
    bytes.append(static_cast<char>(0xE4)); // 三字节序列的首字节
    bytes.append(static_cast<char>(0xB8)); // 只跟了一个续接字节就断了

    const QString decoded = PathName::fromNativeBytes(bytes);

    QCOMPARE(decoded.size(), 3);
    QCOMPARE(decoded.at(0), QChar('a'));
    QCOMPARE(decoded.at(1), rawByte(static_cast<char>(0xE4)));
    QCOMPARE(decoded.at(2), rawByte(static_cast<char>(0xB8)));
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::strayContinuationByteIsKept()
{
    // 0x80..0xBF 是续接字节，不能作为序列首字节。
    QByteArray bytes;
    bytes.append(static_cast<char>(0xBF));
    bytes.append(QByteArrayLiteral("end"));

    const QString decoded = PathName::fromNativeBytes(bytes);

    QCOMPARE(decoded.size(), 4);
    QCOMPARE(decoded.at(0), rawByte(static_cast<char>(0xBF)));
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::toUtf8WouldLoseRawBytes()
{
    // 这条用例把「必须用 toNativeBytes 而不是 toUtf8」写成可执行的断言。
    //
    // 实测行为：QString 里的未配对代理被 toUtf8() 替换成 '?'（0x3F）。
    // 一旦有人把 toNativeBytes 换成 toUtf8，这条用例会失败——
    // 而不是等到某个用户的文件在某个不确定的时刻变得打不开。
    QByteArray bytes = QByteArrayLiteral("a");
    bytes.append(static_cast<char>(0xFF));
    bytes.append(QByteArrayLiteral("b"));

    const QString decoded = PathName::fromNativeBytes(bytes);

    const QByteArray viaQt = decoded.toUtf8();
    QVERIFY2(viaQt != bytes, "如果 Qt 的 toUtf8 已经不丢字节了，这条护栏需要重新评估");
    QVERIFY(viaQt.contains('?'));

    // 我们自己那条路径不丢。
    QCOMPARE(PathName::toNativeBytes(decoded), bytes);
}

void TstPathName::hasRawBytesOnlyForEscapeRange()
{
    QVERIFY(!PathName::hasRawBytes(QString()));
    QVERIFY(!PathName::hasRawBytes(QStringLiteral("普通名字.txt")));
    QVERIFY(!PathName::hasRawBytes(QStringLiteral("\xF0\x9F\x98\x80"))); // emoji 是一对代理，不是私存

    QVERIFY(PathName::hasRawBytes(QString(rawByte(static_cast<char>(0x80)))));
    QVERIFY(PathName::hasRawBytes(QString(rawByte(static_cast<char>(0xFF)))));

    // 边界：DC7F 与 DD00 都在私存区间之外（区间是 DC80..DCFF）。
    QVERIFY(!PathName::hasRawBytes(QString(QChar(0xDC7F))));
    QVERIFY(!PathName::hasRawBytes(QString(QChar(0xDD00))));
}

void TstPathName::unpairedSurrogateOutsideEscapeRangeBecomesReplacement()
{
    // 未配对、且不在私存区间内的代理只可能来自我们自己构造文本时出错。
    // 编码成 U+FFFD 而不是放行——放行会产出无效 UTF-8，交给系统调用后行为不确定。
    const QByteArray encoded = PathName::toNativeBytes(QString(QChar(0xDC7F)));

    QCOMPARE(encoded, QString(QChar(0xFFFD)).toUtf8());
}

void TstPathName::utf8BoundaryCodePointsDecodeCorrectly()
{
    // 每个长度的下界与上界，用来盯住「过长编码判定」的边界写反。
    struct Sample { const char *hex; const char *label; };
    const Sample samples[] = {
        {"7F", "1 字节上界 U+007F"},
        {"C280", "2 字节下界 U+0080"},
        {"DFBF", "2 字节上界 U+07FF"},
        {"E0A080", "3 字节下界 U+0800"},
        {"EFBFBF", "3 字节上界 U+FFFF"},
        {"F0908080", "4 字节下界 U+10000"},
        {"F48FBFBF", "4 字节上界 U+10FFFF"},
    };

    for (const Sample &sample : samples) {
        const QByteArray bytes = QByteArray::fromHex(sample.hex);
        const QString decoded = PathName::fromNativeBytes(bytes);

        QVERIFY2(!PathName::hasRawBytes(decoded),
                 qPrintable(QStringLiteral("%1 被误判为无效字节").arg(QString::fromLatin1(sample.label))));
        QCOMPARE(PathName::toNativeBytes(decoded), bytes);
    }

    // 刚好越过 4 字节上界的编码必须被拒。
    const QByteArray tooBig = QByteArray::fromHex("F4908080"); // U+110000
    QVERIFY(PathName::hasRawBytes(PathName::fromNativeBytes(tooBig)));
}

// -----------------------------------------------------------------------------
// 2. 显示安全化
// -----------------------------------------------------------------------------

void TstPathName::displayEscapesLineBreaksAndTabs()
{
    // 换行是这里最要紧的：名字里有一个换行，列表里就会出现两行，
    // 用户看到的是一个不存在的条目，而真正的名字被截成了两半。
    QCOMPARE(PathName::forDisplay(QStringLiteral("a\nb.txt")), QStringLiteral("a\\nb.txt"));
    QCOMPARE(PathName::forDisplay(QStringLiteral("a\tb.txt")), QStringLiteral("a\\tb.txt"));
    QCOMPARE(PathName::forDisplay(QStringLiteral("a\rb.txt")), QStringLiteral("a\\rb.txt"));
}

void TstPathName::displayEscapesOtherControlCharacters()
{
    QString name = QStringLiteral("a");
    name.append(QChar(0x01));
    name.append(QChar(0x7F));
    name.append(QStringLiteral("b"));

    QCOMPARE(PathName::forDisplay(name), QStringLiteral("a\\x01\\x7Fb"));

    // 私存区间（0xDC80 起）不是控制字符，不能被这里顺手转义掉——
    // 它有自己的一套显示规则（见下一条）。
    QVERIFY(!PathName::forDisplay(QString(rawByte(static_cast<char>(0xFF)))).contains(QStringLiteral("\\xDC")));
}

void TstPathName::displayEscapesRawBytesAsHex()
{
    // 显示的是**真实字节值**。用户想拿这个名字去写脚本、去命令行里操作时，
    // 这个值可以直接对上；换成别的替代字符就只能看，没法用。
    QByteArray bytes = QByteArrayLiteral("a");
    bytes.append(static_cast<char>(0xFF));
    bytes.append(QByteArrayLiteral("b"));

    QCOMPARE(PathName::forDisplay(PathName::fromNativeBytes(bytes)),
             QStringLiteral("a\\xFFb"));
}

void TstPathName::displayMarksLeadingAndTrailingSpaces()
{
    // 「a.txt」与「a.txt 」在列表里长得一模一样，而它们是两个不同的文件。
    // 用户在这两个里面挑一个去删除，挑错的概率是百分之百——不是他粗心，
    // 是界面没有把信息给他。
    QCOMPARE(PathName::forDisplay(QStringLiteral(" a.txt")), QStringLiteral("·a.txt"));
    QCOMPARE(PathName::forDisplay(QStringLiteral("a.txt ")), QStringLiteral("a.txt·"));
    QCOMPARE(PathName::forDisplay(QStringLiteral("  a.txt  ")), QStringLiteral("··a.txt··"));
}

void TstPathName::displayLeavesInteriorSpacesAlone()
{
    // 内部的空格是看得见的，替换掉反而会让名字与真实值不一致。
    QCOMPARE(PathName::forDisplay(QStringLiteral("my report.txt")),
             QStringLiteral("my report.txt"));
    QCOMPARE(PathName::forDisplay(QStringLiteral("a  b")), QStringLiteral("a  b"));
}

void TstPathName::displayOfAllSpaceName()
{
    // 全是空格的名字（POSIX 上合法）必须整体可见，不能显示成一片空白。
    QCOMPARE(PathName::forDisplay(QStringLiteral("   ")), QStringLiteral("···"));
}

void TstPathName::displayDiffersFromActualDetectsChanges()
{
    QVERIFY(!PathName::displayDiffersFromActual(QStringLiteral("report.txt")));
    QVERIFY(PathName::displayDiffersFromActual(QStringLiteral("report.txt ")));
    QVERIFY(PathName::displayDiffersFromActual(QStringLiteral("a\nb")));
    QVERIFY(PathName::displayDiffersFromActual(QStringLiteral("café.txt")) == false);
}

// -----------------------------------------------------------------------------
// 3. Unicode 规范化
// -----------------------------------------------------------------------------

void TstPathName::composedAndDecomposedNamesCompareEqual()
{
    // "é" 的两种写法：NFC 是单码位 U+00E9，NFD 是 'e' + U+0301。
    // 视觉完全相同，字节完全不同。
    const QString composed = QString::fromUtf8("caf\xC3\xA9.txt");
    const QString decomposed = QString::fromUtf8("cafe\xCC\x81.txt");

    QVERIFY2(composed != decomposed, "两种形式的字节本来就不同，这个用例的前提是它们确实不同");
    QVERIFY(PathName::equalNames(composed, decomposed, Qt::CaseSensitive));
    QVERIFY(PathName::equalNames(decomposed, composed, Qt::CaseSensitive));

    // 规范化统一到 NFC：两种输入都产出同一个结果。
    QCOMPARE(PathName::normalizedName(composed), PathName::normalizedName(decomposed));
    QCOMPARE(PathName::normalizedName(decomposed), composed);
}

void TstPathName::differentNamesStayDifferent()
{
    // 反向约束：规范化不能把本来就不同的名字抹平成相同。
    QVERIFY(!PathName::equalNames(QStringLiteral("café.txt"), QStringLiteral("cafe.txt"),
                                  Qt::CaseSensitive));
    QVERIFY(!PathName::equalNames(QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                  Qt::CaseSensitive));
}

void TstPathName::normalizationKeepsRawBytes()
{
    // 实测过 Qt 的 normalized() 会原样保留未配对代理，但这是一条**外部依赖**：
    // 哪天 Qt 改了行为，整套私存方案会静默失效（原始字节变成 U+FFFD）。
    // 这条用例把它钉住。
    QByteArray bytes = QByteArrayLiteral("cafe");
    bytes.append(static_cast<char>(0xFF));
    bytes.append(QByteArrayLiteral("."));

    const QString decoded = PathName::fromNativeBytes(bytes);
    const QString normalized = PathName::normalizedName(decoded);

    QVERIFY2(PathName::hasRawBytes(normalized), "规范化把原始字节弄丢了");
    QCOMPARE(PathName::toNativeBytes(normalized), bytes);
}

void TstPathName::equalNamesHonoursCaseSensitivity()
{
    // 大小写语义必须由调用方传入：Linux 上 a.txt 与 A.TXT 是两个文件，
    // Windows 与 macOS 默认是一个。判错会让文件夹比对给出错误结论。
    QVERIFY(PathName::equalNames(QStringLiteral("Report.TXT"), QStringLiteral("report.txt"),
                                 Qt::CaseInsensitive));
    QVERIFY(!PathName::equalNames(QStringLiteral("Report.TXT"), QStringLiteral("report.txt"),
                                  Qt::CaseSensitive));
}

// -----------------------------------------------------------------------------
// 4. 文件名校验
// -----------------------------------------------------------------------------

void TstPathName::emptyNameIsRejected()
{
    QCOMPARE(checkFileName(QString()).problem, FileNameProblem::Empty);
    QVERIFY(!isValidFileName(QString()));
}

void TstPathName::dotAndDotDotAreRejected()
{
    QCOMPARE(checkFileName(QStringLiteral(".")).problem, FileNameProblem::DotOrDotDot);
    QCOMPARE(checkFileName(QStringLiteral("..")).problem, FileNameProblem::DotOrDotDot);

    // 但 ".." 出现在名字中间或结尾是正常的（"a..b"、"v1..2"）。
    QVERIFY(isValidFileName(QStringLiteral("a..b")));
    QVERIFY(isValidFileName(QStringLiteral("..a")));
}

void TstPathName::controlCharactersAreRejected()
{
    QCOMPARE(checkFileName(QStringLiteral("a\nb")).problem, FileNameProblem::ControlCharacter);
    QCOMPARE(checkFileName(QStringLiteral("a\tb")).problem, FileNameProblem::ControlCharacter);

    QString withNul = QStringLiteral("ab");
    withNul.insert(1, QChar(0));
    QCOMPARE(checkFileName(withNul).problem, FileNameProblem::ControlCharacter);
}

void TstPathName::forbiddenCharactersAreRejected()
{
    // 取所有平台里最严格的一套（Windows 的保留字符集）。
    // 理由是跨平台文件树：在 Linux 上放行 "a:b.txt"，等同步到 Windows 才失败，
    // 那时已经追不到源头。
    const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for (const QChar character : forbidden) {
        const QString name = QStringLiteral("a") + character + QStringLiteral("b.txt");
        QCOMPARE(checkFileName(name).problem, FileNameProblem::ForbiddenCharacter);
    }

    // '/' 与 NUL 在任何平台都不能出现在单个名字里，也在这一组。
    QCOMPARE(checkFileName(QStringLiteral("a/b")).problem, FileNameProblem::ForbiddenCharacter);
}

void TstPathName::trailingSpaceOrDotIsRejected()
{
    // 结束的空格与点在名字中间完全合法（"a b.txt"、"v1.2"），
    // 只有出现在结尾才有问题——所以它是「位置非法」而不是「字符非法」。
    QCOMPARE(checkFileName(QStringLiteral("a.txt ")).problem, FileNameProblem::TrailingSpaceOrDot);
    QCOMPARE(checkFileName(QStringLiteral("a.txt.")).problem, FileNameProblem::TrailingSpaceOrDot);

    // 位置指向最后一个字符（"a.txt " 的最后一个字符索引是 5），
    // 界面可以把光标放到那里。
    QCOMPARE(checkFileName(QStringLiteral("a.txt ")).position, 5);
}

void TstPathName::interiorSpaceAndDotAreAccepted()
{
    QVERIFY(isValidFileName(QStringLiteral("my report.txt")));
    QVERIFY(isValidFileName(QStringLiteral("v1.2.3")));
    QVERIFY(isValidFileName(QStringLiteral("我的 报告.txt")));
    QVERIFY(isValidFileName(QStringLiteral("a b .txt")));
}

void TstPathName::reservedDeviceNamesAreRejected()
{
    // 保留名不含任何非法字符，只能整名比较；匹配时不带扩展名、不区分大小写。
    QCOMPARE(checkFileName(QStringLiteral("CON")).problem, FileNameProblem::ReservedName);
    QCOMPARE(checkFileName(QStringLiteral("nul")).problem, FileNameProblem::ReservedName);
    QCOMPARE(checkFileName(QStringLiteral("Com1.txt")).problem, FileNameProblem::ReservedName);
    QCOMPARE(checkFileName(QStringLiteral("LPT9.log")).problem, FileNameProblem::ReservedName);

    // 相似但不同的名字必须放行——过宽的匹配会让一堆正常名字建不出来。
    QVERIFY(isValidFileName(QStringLiteral("CONS.txt")));
    QVERIFY(isValidFileName(QStringLiteral("COM10.txt")));
    QVERIFY(isValidFileName(QStringLiteral("MYCON.txt")));
}

void TstPathName::overlongNameIsRejected()
{
    QCOMPARE(checkFileName(QString(255, QLatin1Char('a'))).problem, FileNameProblem::None);
    QCOMPARE(checkFileName(QString(256, QLatin1Char('a'))).problem, FileNameProblem::TooLong);
}

void TstPathName::rawByteEscapeIsNotAControlCharacter()
{
    // 承载原始字节的私存码位（U+DC80..DCFF）代表文件名的**真实字节**，
    // 不是控制字符。拦下它等于禁止用户重命名那个文件——而他什么错都没犯。
    const QString name = QStringLiteral("a") + rawByte(static_cast<char>(0xFF))
            + QStringLiteral("b");

    QCOMPARE(checkFileName(name).problem, FileNameProblem::None);
    QVERIFY(isValidFileName(name));
}

void TstPathName::problemPositionPointsAtTheCharacter()
{
    QCOMPARE(checkFileName(QStringLiteral("ab\nc")).position, 2);
    QCOMPARE(checkFileName(QStringLiteral("ab?c")).position, 2);
    QCOMPARE(checkFileName(QStringLiteral(".")).position, 0);
    QCOMPARE(checkFileName(QStringLiteral("good.txt")).position, -1);

    // 薄封装与主实现必须给出一致的答案。
    QCOMPARE(findInvalidFileNameCharacter(QStringLiteral("ab?c")), 2);
    QCOMPARE(findInvalidFileNameCharacter(QStringLiteral("good.txt")), -1);
}

void TstPathName::everyProblemHasItsOwnAdvice()
{
    const FileNameProblem problems[] = {
        FileNameProblem::Empty,           FileNameProblem::DotOrDotDot,
        FileNameProblem::ControlCharacter, FileNameProblem::ForbiddenCharacter,
        FileNameProblem::TrailingSpaceOrDot, FileNameProblem::ReservedName,
        FileNameProblem::TooLong,
    };

    QStringList seen;
    for (FileNameProblem problem : problems) {
        const QString advice = describeFileNameProblem(problem);
        QVERIFY2(!advice.isEmpty(),
                 qPrintable(QStringLiteral("问题 %1 没有给出说明")
                            .arg(QString::fromLatin1(fileNameProblemIdentifier(problem)))));
        QVERIFY2(!seen.contains(advice),
                 qPrintable(QStringLiteral("问题 %1 与其他问题给了同一句话：%2")
                            .arg(QString::fromLatin1(fileNameProblemIdentifier(problem)), advice)));
        seen.append(advice);
    }

    // 合法时不该有话说。
    QVERIFY(describeFileNameProblem(FileNameProblem::None).isEmpty());
}

void TstPathName::checkFileNameAgreesWithTheThinWrappers()
{
    // 防止「两个事实来源」：isValidFileName / findInvalidFileNameCharacter
    // 都是 checkFileName 的薄封装，必须永远给出一致的答案。
    // 各写一遍校验逻辑的话，分歧是静默的——调用方各取所需，
    // 用户遇到的是「有时能建、有时不能建」。
    const QStringList samples = {
        QStringLiteral("good.txt"),
        QString(),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("a\nb"),
        QStringLiteral("a?b"),
        QStringLiteral("a "),
        QStringLiteral("a."),
        QStringLiteral("CON"),
        QString(300, QLatin1Char('x')),
        QStringLiteral("我的 报告.txt"),
        QStringLiteral("my report.txt"),
    };

    for (const QString &sample : samples) {
        const FileNameCheck check = checkFileName(sample);
        QCOMPARE(isValidFileName(sample), check.isValid());
        QCOMPARE(findInvalidFileNameCharacter(sample), check.position);
    }
}

// -----------------------------------------------------------------------------
// 5. 真实文件系统
// -----------------------------------------------------------------------------

void TstPathName::realNameWithSpacesAndQuotesRoundTrips()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // 已存在的名字要能正确显示与操作（PLAT-007 第 3 条）。
    // 注意这与第 4 条（新建时严格校验）不矛盾：前者面向文件系统里已有的数据，
    // 后者面向用户输入。下面两条断言一起说明这个取舍是有意的。
    const QString name = QStringLiteral(" a \"quoted\" name .txt");
    const QString path = directory.path() + QLatin1Char('/') + name;

    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), "含引号与首尾空格的名字在 POSIX 上本来就合法");
    file.write("x");
    file.close();

    const QStringList entries = QDir(directory.path()).entryList(QDir::Files);
    QCOMPARE(entries.size(), 1);
    // 原样读回：不能被 trim，也不能被转义（转义只发生在显示层）。
    QCOMPARE(entries.at(0), name);

    // 显示层才动手：开头的空格标出来，引号保持原样（它没被改动过，
    // 只是这个名字在 Windows 上建不出来，所以校验层会拦）。
    QCOMPARE(PathName::forDisplay(entries.at(0)), QStringLiteral("·a \"quoted\" name .txt"));

    // 而校验层会拦下这个名字——因为它在 Windows 上建不出来。
    // 这是刻意的跨平台保守取舍，不是 bug。
    QCOMPARE(checkFileName(name).problem, FileNameProblem::ForbiddenCharacter);
}

void TstPathName::realNameWithTrailingSpaceRoundTrips()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString name = QStringLiteral("report.txt ");
    const QString path = directory.path() + QLatin1Char('/') + name;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        // Windows 会在创建时静默去掉结尾空格，于是这个名字根本不存在。
        // 这正是校验层要拦下它的原因（见 TrailingSpaceOrDot 的说明）。
        QSKIP("本机不允许以空格结尾的名字（Windows 的典型行为）");
    }
    file.write("x");
    file.close();

    const QStringList entries = QDir(directory.path()).entryList(QDir::Files);
    QCOMPARE(entries.size(), 1);

    // POSIX 上名字原样保留，没有任何 trim。
    QCOMPARE(entries.at(0), name);

    // 「看起来一样的两个名字」在显示层必须能分辨出来。
    QVERIFY(PathName::forDisplay(entries.at(0)) != PathName::forDisplay(QStringLiteral("report.txt")));
}

void TstPathName::realComposedNameIsFoundByOtherForm()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString composed = QString::fromUtf8("caf\xC3\xA9.txt");      // NFC
    const QString decomposed = QString::fromUtf8("cafe\xCC\x81.txt");   // NFD

    const QString path = directory.path() + QLatin1Char('/') + composed;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    // HFS+/APFS 会把名字规范化后存储，Linux 则原样保留。**两种平台行为不同**，
    // 所以这里不断言系统怎么做，只断言我们提供给上层的那条性质：
    // 读回来的名字与写进去的名字必须是「同一个名字」。
    const QStringList entries = QDir(directory.path()).entryList(QDir::Files);
    QCOMPARE(entries.size(), 1);

    QVERIFY2(PathName::equalNames(entries.at(0), composed, Qt::CaseSensitive),
             qPrintable(QStringLiteral("读回的名字与写入时判成了不同的名字：%1 vs %2")
                        .arg(PathName::forDisplay(entries.at(0)), PathName::forDisplay(composed))));

    // 两种写法也要判为同一个名字——这是上层「按名字查找」的基础。
    QVERIFY(PathName::equalNames(composed, decomposed, Qt::CaseSensitive));
}

#if defined(Q_OS_UNIX)
void TstPathName::realRawByteNameRoundTripsOnPosix()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // 名字：'a' + 0xFF（无效 UTF-8 字节）+ ".txt"
    QByteArray rawName = QByteArrayLiteral("a");
    rawName.append(static_cast<char>(0xFF));
    rawName.append(QByteArrayLiteral(".txt"));

    const QByteArray rawPath = QFile::encodeName(directory.path()) + '/' + rawName;

    // 用原始系统调用创建——QFile 接受的是 QString，走不到「无效字节」这条路上。
    const int descriptor = ::open(rawPath.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        // macOS 的 APFS/HFS+ 不接受无效 UTF-8 的文件名。CI（Ubuntu）上会真实执行。
        QSKIP("本机文件系统不接受无效 UTF-8 的名字，该用例只在 Linux 上执行");
    }
    ::close(descriptor);

    // 保真解码：0xFF 不能被丢掉。
    const QString decoded = PathName::fromNativeBytes(rawName);
    QVERIFY(PathName::hasRawBytes(decoded));
    QCOMPARE(PathName::toNativeBytes(decoded), rawName);

    // 最关键的一步：用「文本 -> 字节」这条链路真的能操作那个文件。
    // 这一步如果丢过字节，下面就会报「文件不存在」。
    const QByteArray roundTrippedPath =
            QFile::encodeName(directory.path()) + '/' + PathName::toNativeBytes(decoded);

    struct stat info;
    QCOMPARE(::stat(roundTrippedPath.constData(), &info), 0);
    QCOMPARE(static_cast<qint64>(info.st_size), qint64(0));

    // 显示层要能看出那里有一个坏字节，并且给出真实值。
    QVERIFY2(PathName::forDisplay(decoded).contains(QStringLiteral("\\xFF")),
             qPrintable(PathName::forDisplay(decoded)));

    // 收尾：删掉那个文件，别在用户的临时目录里留东西。
    QCOMPARE(::unlink(rawPath.constData()), 0);
}
#else
void TstPathName::realRawByteNameRoundTripsOnPosix()
{
    QSKIP("无效 UTF-8 的文件名是 POSIX 概念，Windows 上不存在");
}
#endif

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstPathName)
