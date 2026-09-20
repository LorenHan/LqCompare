#include "pathname.h"

#include <QChar>

namespace LqCompare {
namespace Files {
namespace PathName {

namespace {

constexpr ushort kEscapeBase = 0xDC00; // 私存码位 = kEscapeBase + 字节值

/// 一段字节序列解码出的结果。
struct DecodedSequence
{
    bool valid = false;
    uint codePoint = 0;  ///< 有效时的码位
    int length = 0;      ///< 有效时占用的字节数
};

///
/// \brief 按 RFC 3629 校验并解码一段 UTF-8。
///
/// 为什么要自己写而不直接用 QString::fromUtf8
/// ----------------------------------------
/// 因为 fromUtf8 只告诉我们「解出来了」，不告诉我们「这段字节是不是合法的」。
/// 它会把无效字节替换成 U+FFFD，而 U+FFFD **本身也是合法字符**
/// （字节序列 EF BF BD）。于是「原文里真的有一个 U+FFFD」与「这里有个坏字节」
/// 从结果上完全无法区分——两者都会被当成同一个字符，逆向编码时前者能还原、
/// 后者不能，但代码无从判断该走哪条路。
///
/// 自己按码位范围校验就能区分开，代价只是这几十行。这三条校验一条都不能省：
///   - **过长编码**（如 `C0 80` 表示 U+0000）：合法的 UTF-8 里 U+0000 只能是
///     `00`。不判过长会导致路径里凭空出现一个 NUL，而这在路径里是终止符语义。
///   - **代理区**（U+D800..U+DFFF）：UTF-8 里不应该出现，它们是 UTF-16 的机制。
///     放行会与上面那套私存码位撞车——那就再也分不清「私存的一个字节」和
///     「文件名里真有一个 U+DC80」了。
///   - **上限** U+10FFFF。
///
DecodedSequence decodeUtf8Sequence(const char *bytes, int available)
{
    DecodedSequence result;
    if (available <= 0)
        return result;

    const uchar lead = static_cast<uchar>(bytes[0]);

    int length = 0;
    uint codePoint = 0;
    uint minimum = 0;

    if (lead < 0x80) {
        length = 1;
        codePoint = lead;
        minimum = 0;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        codePoint = lead & 0x1Fu;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        codePoint = lead & 0x0Fu;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        codePoint = lead & 0x07u;
        minimum = 0x10000;
    } else {
        // 0x80..0xBF 是续接字节，不能作为首字节；0xF8..0xFF 是 UTF-8 未定义的首字节。
        return result;
    }

    if (available < length)
        return result;

    for (int i = 1; i < length; ++i) {
        const uchar continuation = static_cast<uchar>(bytes[i]);
        if ((continuation & 0xC0) != 0x80)
            return result;
        codePoint = (codePoint << 6) | (continuation & 0x3Fu);
    }

    if (codePoint < minimum)          // 过长编码
        return result;
    if (codePoint > 0x10FFFFu)        // 超过 Unicode 上限
        return result;
    if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) // 代理区
        return result;

    result.valid = true;
    result.codePoint = codePoint;
    result.length = length;
    return result;
}

/// 按 UTF-8 追加一个码位（不含私存字节，那是单字节直通）。
void appendUtf8(QByteArray &out, uint codePoint)
{
    if (codePoint < 0x80u) {
        out.append(static_cast<char>(codePoint));
    } else if (codePoint < 0x800u) {
        out.append(static_cast<char>(0xC0u | (codePoint >> 6)));
        out.append(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint < 0x10000u) {
        out.append(static_cast<char>(0xE0u | (codePoint >> 12)));
        out.append(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.append(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        out.append(static_cast<char>(0xF0u | (codePoint >> 18)));
        out.append(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        out.append(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.append(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

/// 把一个字节值写成 `\xFF` 形式的转义（十六进制大写，前缀保持小写的 `\x`）。
///
/// 为什么前缀不能跟着大写：`\XFF` 不是任何语言认的转义写法。用户看到
/// `\xFF` 会知道这是一个字节，也愿意把它复制到脚本或命令行里试；
/// 看到 `\XFF` 只会以为这个界面输出的东西不能直接用。
QString byteEscape(ushort value)
{
    return QStringLiteral("\\x")
            + QString::number(value, 16).rightJustified(2, QLatin1Char('0')).toUpper();
}

/// 把码位写进 QString（需要时输出一对代理）。
void appendCodePoint(QString &out, uint codePoint)
{
    if (codePoint < 0x10000u) {
        out.append(QChar(static_cast<ushort>(codePoint)));
        return;
    }

    const uint adjusted = codePoint - 0x10000u;
    out.append(QChar(static_cast<ushort>(0xD800u + (adjusted >> 10))));
    out.append(QChar(static_cast<ushort>(0xDC00u + (adjusted & 0x3FFu))));
}

} // namespace

QString fromNativeBytes(const QByteArray &bytes)
{
    QString result;
    result.reserve(bytes.size());

    const char *data = bytes.constData();
    const int size = bytes.size();

    int index = 0;
    while (index < size) {
        const DecodedSequence decoded = decodeUtf8Sequence(data + index, size - index);

        if (decoded.valid) {
            appendCodePoint(result, decoded.codePoint);
            index += decoded.length;
            continue;
        }

        // 无效字节：**不丢弃**，用私存码位承载原始字节值。
        //
        // 这一步是整个模块存在的理由。换成 U+FFFD 的话，原始字节当场丢失，
        // 之后这个文件既打不开也删不掉——而失败现象是「找不到文件」，
        // 与「名字里有个坏字节」看不出关系，用户只能看到一堆无法解释的失败。
        const uchar raw = static_cast<uchar>(data[index]);
        result.append(QChar(static_cast<ushort>(kEscapeBase + raw)));
        ++index;
    }

    return result;
}

QByteArray toNativeBytes(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());

    for (int i = 0; i < text.size(); ++i) {
        const ushort unit = text.at(i).unicode();

        // 私存字节：单字节直通，**不走 UTF-8 编码**。
        // 这是与 fromNativeBytes 的配对约定，也是它不能被 toUtf8() 代替的原因。
        if (unit >= RawByteEscapeFirst && unit <= RawByteEscapeLast) {
            result.append(static_cast<char>(unit - kEscapeBase));
            continue;
        }

        if (QChar::isHighSurrogate(unit) && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate()) {
            appendUtf8(result, QChar::surrogateToUcs4(text.at(i), text.at(i + 1)));
            ++i;
            continue;
        }

        if (QChar::isSurrogate(unit)) {
            // 未配对代理，且不在私存区间内：这只可能来自我们自己构造文本时出错
            // （或有人手工拼了 QString）。编码成 U+FFFD 而不是放行——
            // 放行会产出一段无效 UTF-8，交给系统调用后行为不确定。
            // 换成丢失原始字节更糟，但这里本来就没有「原始字节」这回事。
            appendUtf8(result, 0xFFFDu);
            continue;
        }

        appendUtf8(result, unit);
    }

    return result;
}

bool hasRawBytes(const QString &text)
{
    for (int i = 0; i < text.size(); ++i) {
        const ushort unit = text.at(i).unicode();
        if (unit >= RawByteEscapeFirst && unit <= RawByteEscapeLast)
            return true;
    }
    return false;
}

QString forDisplay(const QString &name)
{
    QString result;
    result.reserve(name.size());

    for (int i = 0; i < name.size(); ++i) {
        const ushort unit = name.at(i).unicode();

        if (unit >= RawByteEscapeFirst && unit <= RawByteEscapeLast) {
            // 用真实的字节值显示：用户想拿这个名字去写脚本或命令行时，
            // 这个值是可以直接对上的。换成别的替代字符就只能看，没法用。
            result += byteEscape(static_cast<ushort>(unit - kEscapeBase));
            continue;
        }

        switch (unit) {
        case '\n':
            result += QStringLiteral("\\n");
            continue;
        case '\r':
            result += QStringLiteral("\\r");
            continue;
        case '\t':
            result += QStringLiteral("\\t");
            continue;
        default:
            break;
        }

        if (unit < 0x20 || unit == 0x7F) {
            result += byteEscape(unit);
            continue;
        }

        result += name.at(i);
    }

    // 首尾空格：直接显示会完全看不出来，而「a.txt」与「a.txt 」是两个不同的文件。
    // 用户在列表里挑一个「看起来一样」的名字去重命名或删除，挑错的概率是百分之百
    // ——不是他粗心，是界面没有把信息给他。
    //
    // 用中点（U+00B7）而不是引号：引号会被误认为名字的一部分，中点不会，
    // 而且它在等宽与非等宽字体下都居中可见。
    int firstNonSpace = 0;
    while (firstNonSpace < result.size() && result.at(firstNonSpace) == QLatin1Char(' '))
        ++firstNonSpace;

    int lastNonSpace = result.size() - 1;
    while (lastNonSpace >= 0 && result.at(lastNonSpace) == QLatin1Char(' '))
        --lastNonSpace;

    const QChar visibleSpace(0x00B7);
    for (int i = 0; i < firstNonSpace; ++i)
        result[i] = visibleSpace;
    for (int i = lastNonSpace + 1; i < result.size(); ++i)
        result[i] = visibleSpace;

    return result;
}

bool displayDiffersFromActual(const QString &name)
{
    return forDisplay(name) != name;
}

QString normalizedName(const QString &name)
{
    // 组合形式统一到 NFC（预组合）。
    //
    // 选 NFC 而不是 NFD：用户在输入框里敲出来、从剪贴板粘进来的绝大多数是 NFC，
    // 统一到用户常见的形式，界面上的「看起来相同」与「判为相同」才一致。
    //
    // 已实测：normalized() 会原样保留私存码位（未配对低代理），
    // 因此这里不需要先把原始字节摘出来再规范化——多那一步只会多一个出错点。
    return name.normalized(QString::NormalizationForm_C);
}

bool equalNames(const QString &left, const QString &right, Qt::CaseSensitivity caseSensitivity)
{
    // 先比长度相同的快速路径没有意义：规范化本来就要构造新字符串。
    // 直接比，代码短、出错点少。
    return QString::compare(normalizedName(left), normalizedName(right), caseSensitivity) == 0;
}

} // namespace PathName
} // namespace Files
} // namespace LqCompare
