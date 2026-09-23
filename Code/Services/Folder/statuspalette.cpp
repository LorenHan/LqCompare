#include "statuspalette.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace LqCompare {
namespace Folder {
namespace {

// ---------------------------------------------------------------------------
// 十六进制色值
// ---------------------------------------------------------------------------

// 严格只认 `#rrggbb`：6 位、大小写都收（写出来统一小写）。
//
// 刻意**不**收 `#rgb` 与 `#rrggbbaa`：前者与后者在同一个字段里混用时，
// 「看着一样」与「相等」会分家；alpha 更是另一件事——本模块算的是
// 「文字压在背景上的对比度」，带 alpha 的前景色得先与背景合成，
// 而合成的结果是背景相关的，那时「一档状态一个颜色」这句话就不成立了。
bool parseHexColor(const QString &color, int *r, int *g, int *b)
{
    if (color.size() != 7 || !color.startsWith(QLatin1Char('#')))
        return false;
    int channels[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        const QString part = color.mid(1 + index * 2, 2);
        bool ok = false;
        const int value = part.toInt(&ok, 16);
        if (!ok)
            return false;
        // `toInt(…, 16)` 收得下 `+5` / `-1` 这类写法，而它们在色值里没有意义。
        for (const QChar ch : part) {
            if (!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                  || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
                  || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))))
                return false;
        }
        channels[index] = value;
    }
    if (r)
        *r = channels[0];
    if (g)
        *g = channels[1];
    if (b)
        *b = channels[2];
    return true;
}

bool isHexColor(const QString &color)
{
    return parseHexColor(color, nullptr, nullptr, nullptr);
}

QString hexFromChannels(int r, int g, int b)
{
    const auto clamp = [](int value) { return std::max(0, std::min(255, value)); };
    return QStringLiteral("#%1%2%3")
        .arg(clamp(r), 2, 16, QLatin1Char('0'))
        .arg(clamp(g), 2, 16, QLatin1Char('0'))
        .arg(clamp(b), 2, 16, QLatin1Char('0'))
        .toLower();
}

// sRGB 分量（0..255）↔ 线性分量（0..1）。WCAG 与色觉模拟都要在这一侧做，
// 因为两者的物理量（相对亮度、锥细胞响应）都在线性空间里可加。
double linearize(int channel)
{
    const double value = channel / 255.0;
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double delinearize(double value)
{
    const double clamped = std::max(0.0, std::min(1.0, value));
    return clamped <= 0.0031308 ? clamped * 12.92
                               : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

// 标识符的合法形状：小写字母 / 数字 / 连字符，非空。
// 与 `validateRecursionTierTable()` 同一条规则——这些标识符会进设置键、
// 配色文件与命令行，出现空格或大写就会在不同消费点被不同地归一化。
bool isMachineReadableIdentifier(const QString &identifier)
{
    if (identifier.isEmpty())
        return false;
    for (const QChar ch : identifier) {
        const bool ok = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('-');
        if (!ok)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 单套方案的语义自检
// ---------------------------------------------------------------------------
//
// 抽出来是为了让**配色文件解析**与**表自检**共用同一份判据。各写一份的话，
// 「这个文件能读进来」与「这张表算合规」会各自演化，而用户遇到的正是
// 「导入成功了，可是字看不清」。
QStringList validateOneColorScheme(const ColorScheme &scheme,
                                   const QVector<MainStatusDescriptor> &statuses)
{
    QStringList problems;
    const QString where = scheme.identifier.isEmpty() ? QObject::tr("（未命名方案）")
                                                      : scheme.identifier;

    // 标识符形状。**这条曾经是死的**：接手本轮时它是 `if (false)` 包着的一句，
    // 于是 `isMachineReadableIdentifier()` 全仓没有任何调用者，而它要守的那件事
    // ——「标识符是小写字母 / 数字 / 连字符」——没有任何东西会红。
    //
    // 为什么必须在这里守，而不是只靠 `validateColorSchemeTable()` 里的「唯一性」：
    // 唯一性只说「两套之间不重名」，一个叫「High Contrast」的方案没有重名，
    // 照样能通过。而标识符要进三个由不得空格与大写的地方——设置键、
    // `.lqcolors` 文件、命令行——每一处都会各自做一次归一化。三处归一化
    // 迟早分叉，表现是「同一个方案在三处被认成三个」。
    //
    // 空标识符同样报在这里（`isMachineReadableIdentifier()` 对空串返回 false）：
    // 空串在 `where` 里已经被换成了「（未命名方案）」，因此报错文案读得通。
    if (!isMachineReadableIdentifier(scheme.identifier))
        problems << QObject::tr("方案 %1 的标识符必须是小写字母 / 数字 / 连字符，实际「%2」。")
                        .arg(where, scheme.identifier);
    if (scheme.displayName.isEmpty())
        problems << QObject::tr("方案 %1 缺展示名。").arg(where);
    if (scheme.description.isEmpty())
        problems << QObject::tr("方案 %1 缺说明。").arg(where);

    if (!isHexColor(scheme.lightBackground) || !isHexColor(scheme.darkBackground)) {
        problems << QObject::tr("方案 %1 的两个参考背景色必须是 #rrggbb，实际「%2」/「%3」。")
                        .arg(where, scheme.lightBackground, scheme.darkBackground);
    } else {
        // 两档背景必须一明一暗。都亮意味着这套方案根本没有深色主题那一档，
        // 第 1 条「图标随主题切换自动适配深浅」当场落空——而这件事在运行期
        // 没有别的现象（深色主题下只是有几行字看不清）。
        if (relativeLuminance(scheme.lightBackground) < 0.5
            || relativeLuminance(scheme.darkBackground) > 0.5) {
            problems << QObject::tr("方案 %1 的两个背景色必须一明一暗，实际亮度 %2 / %3。")
                            .arg(where)
                            .arg(relativeLuminance(scheme.lightBackground), 0, 'f', 3)
                            .arg(relativeLuminance(scheme.darkBackground), 0, 'f', 3);
        }
    }
    if (!isHexColor(scheme.excludedLight) || !isHexColor(scheme.excludedDark)) {
        problems << QObject::tr("方案 %1 的「已排除」弱化色必须是 #rrggbb。").arg(where);
    } else {
        // 「已排除」也是要读的文字，因此同样受对比度约束——它没有豁免。
        //
        // 这里特意**不留**一条「弱化色可以低于下限」的例外：留了它就得有东西
        // 守得住「确实弱到该弱的程度」，而那件事没有客观判据，
        // 于是它会退化成一句没人验过的声明（handoff §6 那条纪律）。
        const double lightRatio = contrastRatio(scheme.excludedLight, scheme.lightBackground);
        const double darkRatio = contrastRatio(scheme.excludedDark, scheme.darkBackground);
        if (lightRatio < scheme.minimumContrast || darkRatio < scheme.minimumContrast) {
            problems << QObject::tr("方案 %1 的「已排除」弱化色对比度不足（%2 / %3 低于 %4）。")
                            .arg(where)
                            .arg(lightRatio, 0, 'f', 2)
                            .arg(darkRatio, 0, 'f', 2)
                            .arg(scheme.minimumContrast, 0, 'f', 2);
        }
        // 弱化色必须与「相同」不同色。两者同色时用户分不清「两边确实一样」
        // 与「根本没比」，而这两件事的处置完全相反（一个可以放手、一个必须去看）。
        const QString sameLight = scheme.colorFor(Status::Same, false);
        const QString sameDark = scheme.colorFor(Status::Same, true);
        if ((!sameLight.isEmpty() && scheme.excludedLight == sameLight)
            || (!sameDark.isEmpty() && scheme.excludedDark == sameDark)) {
            problems << QObject::tr("方案 %1 的「已排除」弱化色与「相同」同色，"
                                    "用户分不清「一样」和「没比」。")
                            .arg(where);
        }
    }

    if (scheme.minimumContrast < kMinimumReadableContrastRatio)
        problems << QObject::tr("方案 %1 的最低对比度 %2 低于可读下限 %3。")
                        .arg(where)
                        .arg(scheme.minimumContrast, 0, 'f', 2)
                        .arg(kMinimumReadableContrastRatio, 0, 'f', 2);

    // 覆盖检查：每档主状态**恰好一次**。
    QHash<int, int> seen;
    for (const auto &highlight : scheme.highlights)
        seen[int(highlight.status)] += 1;
    for (const auto &descriptor : statuses) {
        const int count = seen.value(int(descriptor.value), 0);
        if (count == 0) {
            problems << QObject::tr("方案 %1 缺状态「%2」的颜色（每类状态都必须有颜色）。")
                            .arg(where, statusLabel(descriptor.value));
        } else if (count > 1) {
            problems << QObject::tr("方案 %1 的状态「%2」有 %3 份颜色，只能有一份。")
                            .arg(where, statusLabel(descriptor.value))
                            .arg(count);
        }
    }
    for (auto it = seen.constBegin(); it != seen.constEnd(); ++it) {
        const bool known = std::any_of(statuses.cbegin(), statuses.cend(),
                                       [it](const MainStatusDescriptor &descriptor) {
                                           return int(descriptor.value) == it.key();
                                       });
        if (!known)
            problems << QObject::tr("方案 %1 含表外状态（整数 %2）的颜色。")
                            .arg(where)
                            .arg(it.key());
    }

    // 图标这一侧：第 3 条要求「图标与文字必须同时存在」，而图标键住在
    // 状态表里。它与配色是两张表，因此这条检查必须**跨表**——
    // 把状态表当参数传进来，测试才能喂一张把某档图标键清空的状态表进来，
    // 并看到这条真的会红。
    for (const auto &descriptor : statuses) {
        if (QString::fromLatin1(descriptor.iconKey).isEmpty())
            problems << QObject::tr("状态「%1」没有图标键，只有颜色的行在灰度打印与"
                                    "色觉障碍下不含任何信息（第 3 条）。")
                            .arg(statusLabel(descriptor.value));
    }

    // 对比度：九档颜色都要达到本方案自己的阈值。
    for (const auto &highlight : scheme.highlights) {
        if (!isHexColor(highlight.light) || !isHexColor(highlight.dark)) {
            problems << QObject::tr("方案 %1 的状态「%2」色值必须是 #rrggbb，实际「%3」/「%4」。")
                            .arg(where, statusLabel(highlight.status), highlight.light,
                                 highlight.dark);
            continue;
        }
        const double lightRatio = contrastRatio(highlight.light, scheme.lightBackground);
        const double darkRatio = contrastRatio(highlight.dark, scheme.darkBackground);
        if (lightRatio < scheme.minimumContrast) {
            problems << QObject::tr("方案 %1 的状态「%2」在浅色主题下对比度 %3 低于 %4。")
                            .arg(where, statusLabel(highlight.status))
                            .arg(lightRatio, 0, 'f', 2)
                            .arg(scheme.minimumContrast, 0, 'f', 2);
        }
        if (darkRatio < scheme.minimumContrast) {
            problems << QObject::tr("方案 %1 的状态「%2」在深色主题下对比度 %3 低于 %4。")
                            .arg(where, statusLabel(highlight.status))
                            .arg(darkRatio, 0, 'f', 2)
                            .arg(scheme.minimumContrast, 0, 'f', 2);
        }
    }
    return problems;
}

} // namespace

// ---------------------------------------------------------------------------
// 方案表
// ---------------------------------------------------------------------------

QString ColorScheme::colorFor(Status status, bool dark) const
{
    for (const auto &highlight : highlights) {
        if (highlight.status == status)
            return highlight.color(dark);
    }
    return {};
}

bool ColorScheme::coversEveryStatus() const
{
    const auto &statuses = mainStatusTable();
    for (const auto &descriptor : statuses) {
        const bool found = std::any_of(highlights.cbegin(), highlights.cend(),
                                       [&descriptor](const StatusHighlight &highlight) {
                                           return highlight.status == descriptor.value;
                                       });
        if (!found)
            return false;
    }
    return highlights.size() == statuses.size();
}

const QVector<ColorScheme> &colorSchemeTable()
{
    // 顺序 = 界面下拉的顺序，第一套是出厂默认。界面按这张表铺下拉，
    // 因此这里就是顺序的唯一事实来源（另写一份清单不会随表增长）。
    //
    // 浅色档与 DIR-011 上线时视图里那份 switch **逐值相同**（TypeConflict 除外），
    // 所以本条目对默认外观零回归；这是刻意的——一条「加配色方案」的条目
    // 顺带改掉所有人看到的颜色，会让回归的归因变成「最近谁动过颜色」。
    static const QVector<ColorScheme> table = [] {
        const auto highlight = [](Status status, const char *light, const char *dark) {
            StatusHighlight row;
            row.status = status;
            row.light = QString::fromLatin1(light);
            row.dark = QString::fromLatin1(dark);
            return row;
        };
        // 展示名与说明以 `QString` 传入、`tr()` 在调用点就地写：把 `const char *`
        // 传进 `tr()` 时那句话不再是字面量，`lupdate` 抓不到它，
        // 翻译表里会静默少两条（而界面照样能跑）。
        const auto scheme = [](const char *identifier, const QString &name,
                               const QString &description, bool colorBlindFriendly,
                               double minimumContrast, const char *lightBackground,
                               const char *darkBackground, const char *excludedLight,
                               const char *excludedDark) {
            ColorScheme row;
            row.identifier = QString::fromLatin1(identifier);
            row.displayName = name;
            row.description = description;
            row.colorBlindFriendly = colorBlindFriendly;
            row.minimumContrast = minimumContrast;
            row.lightBackground = QString::fromLatin1(lightBackground);
            row.darkBackground = QString::fromLatin1(darkBackground);
            row.excludedLight = QString::fromLatin1(excludedLight);
            row.excludedDark = QString::fromLatin1(excludedDark);
            return row;
        };

        QVector<ColorScheme> result;

        ColorScheme standard = scheme("default", QObject::tr("默认"),
                                      QObject::tr("与工具其余部分一致的常规配色；"
                                                  "浅色与深色主题各一档。"),
                                      false, kMinimumReadableContrastRatio, "#ffffff", "#1e1e1e",
                                      "#666666", "#b0b0b0");
        // `Same` 与「已排除」刻意不同色：前者是「两边确实一样」，后者是
        // 「根本没比」，处置完全相反（一个可以放手，一个必须去看）。
        // 同色时用户只能靠状态列的文字分辨，而扫列表时他看的是颜色。
        standard.highlights = {
            highlight(Status::Same, "#3d3d3d", "#dcdcdc"),
            highlight(Status::Different, "#895000", "#ffbc66"),
            highlight(Status::LeftOnly, "#2055a0", "#89beff"),
            highlight(Status::RightOnly, "#236c44", "#99dcb6"),
            highlight(Status::TypeConflict, "#8a3a8a", "#d99ce0"),
            highlight(Status::Error, "#aa2424", "#ff9696"),
            highlight(Status::Unknown, "#5c6670", "#a9b6c2"),
            highlight(Status::BothChanged, "#5a3fa0", "#c9b3ff"),
            highlight(Status::Conflict, "#8a1f6a", "#ff9ad2"),
        };
        result.append(standard);

        // 高对比：阈值提到 AAA（7.0）。颜色刻意压到极暗/极亮两端，
        // 因此这一套**不以区分为目标**——区分由图标与文字承担（第 3 条）。
        // 这正是「颜色仅作为辅助」的极端情形，也是它可用的原因。
        ColorScheme highContrast = scheme(
            "high-contrast", QObject::tr("高对比"),
            QObject::tr("前景色压到明暗两端、对比度达 WCAG AAA；低视力与强光下更易读，"
                        "区分主要靠图标。"),
            false, kStrongReadableContrastRatio, "#ffffff", "#1e1e1e", "#2b2b2b", "#a8a8a8");
        highContrast.highlights = {
            highlight(Status::Same, "#000000", "#e0e0e0"),
            highlight(Status::Different, "#7a3b00", "#ffa000"),
            highlight(Status::LeftOnly, "#003366", "#7fc4ff"),
            highlight(Status::RightOnly, "#004d00", "#7fff9f"),
            highlight(Status::TypeConflict, "#4b0082", "#d0b0ff"),
            highlight(Status::Error, "#8b0000", "#ff8888"),
            highlight(Status::Unknown, "#2b2b2b", "#a8a8a8"),
            highlight(Status::BothChanged, "#00008b", "#b0b0ff"),
            highlight(Status::Conflict, "#8b0050", "#ff9ad2"),
        };
        result.append(highContrast);

        // 色盲友好：这九档色值不是「挑好看」，是**按判据搜出来的**——
        // 红绿色盲把颜色压成一条蓝—黄轴加明度，因此九档必须主要靠
        // 「明度 + 蓝黄位置」拉开。实测投影后最小间距 26.2（浅）/ 23.2（深），
        // 相对默认方案的 2.8 是两个数量级的差别，判据在
        // `kMinimumColorBlindSeparation` 上。
        ColorScheme colorBlind = scheme(
            "color-blind-safe", QObject::tr("色盲友好"),
            QObject::tr("按红绿色盲的可见轴重排九档颜色，投影后两两最小间距远超默认方案；"
                        "图标与文字仍是主要信息载体。"),
            true, kMinimumReadableContrastRatio, "#ffffff", "#1e1e1e", "#5f5f5f", "#b6b6b6");
        colorBlind.highlights = {
            highlight(Status::Same, "#454545", "#a8a8a8"),
            highlight(Status::Different, "#7a6a00", "#ffbc66"),
            highlight(Status::LeftOnly, "#0d5aa0", "#a8d2ff"),
            highlight(Status::RightOnly, "#0a6b52", "#9fe0bb"),
            highlight(Status::TypeConflict, "#4f2a7f", "#b49cf0"),
            highlight(Status::Error, "#7f0a0a", "#f09090"),
            highlight(Status::Unknown, "#38566b", "#9fb0c0"),
            highlight(Status::BothChanged, "#5040c0", "#a5b4ff"),
            highlight(Status::Conflict, "#b01070", "#ffc2e6"),
        };
        result.append(colorBlind);

        return result;
    }();
    return table;
}

QString defaultColorSchemeIdentifier()
{
    return QStringLiteral("default");
}

bool isKnownColorScheme(const QString &identifier)
{
    const auto &table = colorSchemeTable();
    return std::any_of(table.cbegin(), table.cend(),
                       [&identifier](const ColorScheme &scheme) {
                           return scheme.identifier == identifier;
                       });
}

const ColorScheme &colorSchemeByIdentifier(const QString &identifier)
{
    const auto &table = colorSchemeTable();
    for (const auto &scheme : table) {
        if (scheme.identifier == identifier)
            return scheme;
    }
    // 认不出来就回落到出厂默认。**不返回空方案**：界面拿到一套没有颜色的方案时，
    // 整张列表会退化成同一种颜色，而用户完全无从判断是配色坏了还是数据坏了。
    for (const auto &scheme : table) {
        if (scheme.identifier == defaultColorSchemeIdentifier())
            return scheme;
    }
    // 表里连默认方案都没有——只有把表喂坏才可能走到这里，`Tests/StatusPalette`
    // 用一份写坏的表覆盖过它。此时返回第一行，绝不返回悬空引用。
    return table.first();
}

// ---------------------------------------------------------------------------
// 对比度
// ---------------------------------------------------------------------------

double relativeLuminance(const QString &color)
{
    int r = 0, g = 0, b = 0;
    if (!parseHexColor(color, &r, &g, &b))
        return -1.0;
    return 0.2126 * linearize(r) + 0.7152 * linearize(g) + 0.0722 * linearize(b);
}

double contrastRatio(const QString &first, const QString &second)
{
    const double a = relativeLuminance(first);
    const double b = relativeLuminance(second);
    if (a < 0.0 || b < 0.0)
        return -1.0;
    const double lighter = std::max(a, b);
    const double darker = std::min(a, b);
    return (lighter + 0.05) / (darker + 0.05);
}

// ---------------------------------------------------------------------------
// 色觉障碍模拟
// ---------------------------------------------------------------------------

QString simulateColorVisionDeficiency(const QString &color, ColorVisionDeficiency kind)
{
    int channels[3] = {0, 0, 0};
    if (!parseHexColor(color, &channels[0], &channels[1], &channels[2]))
        return {};

    // 线性 sRGB（0..255 标度，与下面那组矩阵的标度约定一致）。
    //
    // 标度这件事必须与矩阵配套：Viénot 那一族的系数是在 0..255 的线性分量上
    // 推出来的，把 0..1 的分量喂进去不会报错，只会得到一片接近全黑的颜色——
    // 而「模拟结果全黑」看起来像是「色盲看到的世界就是黑的」这种合理结论。
    const double red = linearize(channels[0]) * 255.0;
    const double green = linearize(channels[1]) * 255.0;
    const double blue = linearize(channels[2]) * 255.0;

    // 线性 RGB → LMS（Hunt-Pointer-Estevez，D65）。
    double l = 17.8824 * red + 43.5161 * green + 4.11935 * blue;
    double m = 3.45565 * red + 27.1554 * green + 3.86714 * blue;
    const double s = 0.0299566 * red + 0.184309 * green + 1.46709 * blue;

    // 缺失的那一种锥细胞响应由另外两种线性组合补出（Viénot/Brettel 近似）。
    if (kind == ColorVisionDeficiency::Deuteranopia)
        m = 0.494207 * l + 1.24827 * s;
    else
        l = 2.02344 * m - 2.52581 * s;

    // LMS → 线性 RGB。
    const double outRed = 0.0809444479 * l - 0.130504409 * m + 0.116721066 * s;
    const double outGreen = -0.0102485335 * l + 0.0540193266 * m - 0.113614708 * s;
    const double outBlue = -0.000365296938 * l - 0.00412161469 * m + 0.693511405 * s;

    return hexFromChannels(int(std::round(delinearize(outRed / 255.0) * 255.0)),
                           int(std::round(delinearize(outGreen / 255.0) * 255.0)),
                           int(std::round(delinearize(outBlue / 255.0) * 255.0)));
}

double colorBlindSeparation(const ColorScheme &scheme, bool dark)
{
    const auto &statuses = mainStatusTable();
    if (!scheme.coversEveryStatus())
        return -1.0;

    // 先把九档在两种色觉障碍下的投影都算出来，再逐对量距离。
    QVector<QVector<QString>> projected;
    for (int kind = 0; kind <= 1; ++kind) {
        const auto deficiency = kind == 0 ? ColorVisionDeficiency::Deuteranopia
                                          : ColorVisionDeficiency::Protanopia;
        QVector<QString> row;
        for (const auto &descriptor : statuses) {
            const QString color = scheme.colorFor(descriptor.value, dark);
            if (color.isEmpty())
                return -1.0;
            const QString simulated = simulateColorVisionDeficiency(color, deficiency);
            if (simulated.isEmpty())
                return -1.0;
            row.append(simulated);
        }
        projected.append(row);
    }

    double worst = -1.0;
    for (const auto &row : projected) {
        for (int i = 0; i < row.size(); ++i) {
            for (int j = i + 1; j < row.size(); ++j) {
                int ar = 0, ag = 0, ab = 0, br = 0, bg = 0, bb = 0;
                parseHexColor(row.at(i), &ar, &ag, &ab);
                parseHexColor(row.at(j), &br, &bg, &bb);
                const double dr = double(ar - br);
                const double dg = double(ag - bg);
                const double db = double(ab - bb);
                const double distance = std::sqrt(dr * dr + dg * dg + db * db);
                if (worst < 0.0 || distance < worst)
                    worst = distance;
            }
        }
    }
    return worst;
}

// ---------------------------------------------------------------------------
// 表自检
// ---------------------------------------------------------------------------

QStringList validateColorSchemeTable(const QVector<ColorScheme> &schemes,
                                     const QVector<MainStatusDescriptor> &statuses)
{
    QStringList problems;
    if (schemes.isEmpty())
        return QStringList{QObject::tr("配色方案表为空。")};

    // ① 至少三套，且规格点名的三套都在。名字不是装饰：下面两条把
    // 「高对比」与「色盲友好」这两个**名字**绑定到可度量的性质上——
    // 否则一套纯灰配色也能叫「色盲友好」而没有任何东西会红。
    if (schemes.size() < 3)
        problems << QObject::tr("配色方案至少要有 3 套（默认 / 高对比 / 色盲友好），实际 %1 套。")
                        .arg(schemes.size());
    const char *requiredIdentifiers[] = {"default", "high-contrast", "color-blind-safe"};
    for (const char *raw : requiredIdentifiers) {
        const QString identifier = QString::fromLatin1(raw);
        const bool found = std::any_of(schemes.cbegin(), schemes.cend(),
                                       [&identifier](const ColorScheme &scheme) {
                                           return scheme.identifier == identifier;
                                       });
        if (!found)
            problems << QObject::tr("配色方案表里缺规格点名的「%1」。").arg(identifier);
    }

    // ② 标识符唯一。
    QHash<QString, int> identifierCounts;
    for (const auto &scheme : schemes)
        identifierCounts[scheme.identifier] += 1;
    for (auto it = identifierCounts.constBegin(); it != identifierCounts.constEnd(); ++it) {
        if (it.value() > 1)
            problems << QObject::tr("方案标识符「%1」出现了 %2 次，必须唯一。")
                            .arg(it.key())
                            .arg(it.value());
    }

    // ③ 第一套必须是出厂默认：界面初始值、缺省设置与配色文件回退都取它，
    // 换掉第一行等于换掉所有人看到的默认外观，那不该是一次表格重排的副作用。
    if (!schemes.first().identifier.isEmpty()
        && schemes.first().identifier != defaultColorSchemeIdentifier()) {
        problems << QObject::tr("第一套方案必须是出厂默认「%1」，实际「%2」。")
                        .arg(defaultColorSchemeIdentifier(), schemes.first().identifier);
    }

    // ④ 逐套的语义检查。
    for (const auto &scheme : schemes)
        problems += validateOneColorScheme(scheme, statuses);

    // ⑤ 名字 ↔ 性质。两条都在这里，而不是写进 `validateOneColorScheme()`：
    // 它们是**跨方案**的断言（「总得有一套」），单套范围内看不出来。
    int colorBlindCount = 0;
    for (const auto &scheme : schemes) {
        if (scheme.colorBlindFriendly)
            ++colorBlindCount;
        if (scheme.identifier == QStringLiteral("high-contrast")
            && scheme.minimumContrast < kStrongReadableContrastRatio) {
            problems << QObject::tr("「高对比」方案的最低对比度 %1 低于 AAA 的 %2。")
                            .arg(scheme.minimumContrast, 0, 'f', 2)
                            .arg(kStrongReadableContrastRatio, 0, 'f', 2);
        }
        if (scheme.identifier == QStringLiteral("color-blind-safe") && !scheme.colorBlindFriendly) {
            problems << QObject::tr("「color-blind-safe」方案没有标记为色盲友好——"
                                    "这个名字不许不使用这一位。");
        }
    }
    if (colorBlindCount == 0)
        problems << QObject::tr("没有任何一套方案自称色盲友好。");

    // ⑥ 色盲友好的判据：自称色盲友好就必须真的过阈值。
    //
    // 这里**只留一条绝对阈值**，刻意不写「必须比默认方案更分得开」那种相对比较。
    // 实测理由：默认方案的投影间距是 2.8，远低于阈值 20，因此任何过得了阈值的
    // 方案**自动**比默认方案好 7 倍以上——那条相对比较分支在出厂表上永远走不到。
    // 按本仓的纪律（handoff §6：删掉之后没有任何用例变红的分支不是纵深防御），
    // 走不到的分支就是没人知道的死代码，留着会让下一个人以为它被验过。
    // 「色盲友好方案确实明显优于默认方案」这句断言仍然存在，只是它落在
    // `Tests/StatusPalette` 对**两张表的值**的直接断言上，而不是这个校验分支里。
    for (const auto &scheme : schemes) {
        if (!scheme.colorBlindFriendly)
            continue;
        for (const bool dark : {false, true}) {
            const double separation = colorBlindSeparation(scheme, dark);
            const QString theme = dark ? QObject::tr("深色") : QObject::tr("浅色");
            if (separation < 0.0) {
                problems << QObject::tr("色盲友好方案 %1 在%2主题下算不出投影间距"
                                        "（覆盖不全或色值非法）。")
                                .arg(scheme.identifier, theme);
                continue;
            }
            if (separation < kMinimumColorBlindSeparation) {
                problems << QObject::tr("色盲友好方案 %1 在%2主题下投影后最小间距 %3 低于 %4。")
                                .arg(scheme.identifier, theme)
                                .arg(separation, 0, 'f', 1)
                                .arg(kMinimumColorBlindSeparation, 0, 'f', 1);
            }
        }
    }
    return problems;
}

// ---------------------------------------------------------------------------
// 配色文件
// ---------------------------------------------------------------------------

QString serializeColorScheme(const ColorScheme &scheme)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("lqcompare-color-scheme"));
    // 版本号从第一天就写上：配色文件是**分享出去**的东西，第一份自定义配色
    // 一旦发出去，格式就只能向前兼容，而没有版本号时读取方只能靠猜。
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("identifier"), scheme.identifier);
    root.insert(QStringLiteral("displayName"), scheme.displayName);
    root.insert(QStringLiteral("description"), scheme.description);
    root.insert(QStringLiteral("colorBlindFriendly"), scheme.colorBlindFriendly);
    root.insert(QStringLiteral("minimumContrast"), scheme.minimumContrast);
    root.insert(QStringLiteral("lightBackground"), scheme.lightBackground);
    root.insert(QStringLiteral("darkBackground"), scheme.darkBackground);
    root.insert(QStringLiteral("excludedLight"), scheme.excludedLight);
    root.insert(QStringLiteral("excludedDark"), scheme.excludedDark);

    // 状态用**稳定标识符**当键（DIR-011 的 `statusIdentifier()`），
    // 顺序按主状态表。用下标当键的话，主状态表下一次新增一档，
    // 所有已分享出去的文件就会整体错位一格——而那些文件不会报错，
    // 只会把颜色配到错误的状态上。
    QJsonArray statuses;
    for (const auto &descriptor : mainStatusTable()) {
        const QString color = scheme.colorFor(descriptor.value, false);
        const QString darkColor = scheme.colorFor(descriptor.value, true);
        QJsonObject entry;
        entry.insert(QStringLiteral("status"), statusIdentifier(descriptor.value));
        entry.insert(QStringLiteral("light"), color);
        entry.insert(QStringLiteral("dark"), darkColor);
        statuses.append(entry);
    }
    root.insert(QStringLiteral("statuses"), statuses);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool parseColorScheme(const QString &text, ColorScheme &out, QStringList *problems)
{
    QStringList localProblems;
    const auto fail = [&]() {
        if (problems)
            *problems += localProblems;
        return false; // out 保持原样，绝不写出半成品
    };

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        localProblems << QObject::tr("配色文件不是合法 JSON：%1（偏移 %2）。")
                             .arg(parseError.errorString())
                             .arg(parseError.offset);
        return fail();
    }
    if (!document.isObject()) {
        localProblems << QObject::tr("配色文件的根节点必须是对象。");
        return fail();
    }
    const QJsonObject root = document.object();

    // 格式标记与版本都要对上。缺了格式标记时，用户很可能把别的文件
    // （会话、快照、格式定义）选进了这个对话框；直接报「缺字段 identifier」
    // 会把他引到错误的方向去。
    if (root.value(QStringLiteral("format")).toString()
        != QStringLiteral("lqcompare-color-scheme")) {
        localProblems << QObject::tr("这不是 LqCompare 配色文件（format 字段缺失或不是 "
                                     "lqcompare-color-scheme）。");
        return fail();
    }
    if (root.value(QStringLiteral("version")).toInt(-1) != 1) {
        localProblems << QObject::tr("配色文件版本 %1 不受支持（本版本认 1）。")
                             .arg(root.value(QStringLiteral("version")).toInt(-1));
        return fail();
    }

    ColorScheme parsed;
    parsed.identifier = root.value(QStringLiteral("identifier")).toString();
    parsed.displayName = root.value(QStringLiteral("displayName")).toString();
    parsed.description = root.value(QStringLiteral("description")).toString();
    parsed.colorBlindFriendly = root.value(QStringLiteral("colorBlindFriendly")).toBool(false);
    // 缺 minimumContrast 时用 AA 而不是 0：用 0 会让对比度检查静默失效，
    // 于是一个手写坏了的文件「导入成功」，代价是用户看不清字。
    parsed.minimumContrast =
        root.value(QStringLiteral("minimumContrast")).toDouble(kMinimumReadableContrastRatio);
    parsed.lightBackground = root.value(QStringLiteral("lightBackground")).toString();
    parsed.darkBackground = root.value(QStringLiteral("darkBackground")).toString();
    parsed.excludedLight = root.value(QStringLiteral("excludedLight")).toString();
    parsed.excludedDark = root.value(QStringLiteral("excludedDark")).toString();

    if (!root.value(QStringLiteral("statuses")).isArray()) {
        localProblems << QObject::tr("配色文件缺 statuses 数组。");
        return fail();
    }

    // 标识符 → 状态的反查表。建立在**当前**主状态表上：文件里出现表外标识符时
    // 要明确报出来，而不是静默丢掉那一档（丢掉之后覆盖检查会报「缺状态」，
    // 那句话说的是另一件事，会把用户引去查「为什么少了一档」）。
    QHash<QString, Status> byIdentifier;
    for (const auto &descriptor : mainStatusTable())
        byIdentifier.insert(statusIdentifier(descriptor.value), descriptor.value);

    const QJsonArray statuses = root.value(QStringLiteral("statuses")).toArray();
    for (const QJsonValue &value : statuses) {
        if (!value.isObject()) {
            localProblems << QObject::tr("statuses 里有一项不是对象。");
            return fail();
        }
        const QJsonObject entry = value.toObject();
        const QString identifier = entry.value(QStringLiteral("status")).toString();
        if (!byIdentifier.contains(identifier)) {
            localProblems << QObject::tr("配色文件里的状态标识符「%1」不认识。").arg(identifier);
            continue;
        }
        StatusHighlight highlight;
        highlight.status = byIdentifier.value(identifier);
        highlight.light = entry.value(QStringLiteral("light")).toString();
        highlight.dark = entry.value(QStringLiteral("dark")).toString();
        parsed.highlights.append(highlight);
    }

    // 复用单套方案的判据，因此「导入成功」与「表合规」不可能分家。
    localProblems += validateOneColorScheme(parsed, mainStatusTable());
    if (!localProblems.isEmpty())
        return fail();

    out = parsed;
    return true;
}

QString colorSchemeFileExtension()
{
    return QStringLiteral("lqcolors");
}

QString colorSchemeFileFilter()
{
    return QObject::tr("LqCompare 配色文件 (*.%1)").arg(colorSchemeFileExtension());
}

bool saveColorSchemeFile(const QString &path, const ColorScheme &scheme, QStringList *problems)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (problems)
            *problems << QObject::tr("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = serializeColorScheme(scheme).toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (problems)
            *problems << QObject::tr("写入 %1 不完整：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool loadColorSchemeFile(const QString &path, ColorScheme &scheme, QStringList *problems)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (problems)
            *problems << QObject::tr("无法读取 %1：%2").arg(path, file.errorString());
        return false;
    }
    return parseColorScheme(QString::fromUtf8(file.readAll()), scheme, problems);
}

} // namespace Folder
} // namespace LqCompare
