#ifndef LQCOMPARE_MASK_H
#define LQCOMPARE_MASK_H

#include <QChar>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace LqCompare {
namespace Filter {

///
/// \brief 掩码语言：语法、解析与匹配（PRD: FILT-001）。
///
/// 为什么这个模块必须自成一体
/// --------------------------------
/// 「掩码」在本产品里出现在很多地方：Filters 页的包含/排除框、会话设置的名称过滤、
/// 子目录排除、预设库、命令行参数。如果每个地方各写一套通配逻辑，就会出现
/// 「同一个 `*.tar.gz` 在界面上能匹配、在命令行上匹配不了」这类分歧——而用户
/// 只会得出「这个软件的过滤靠不住」的结论。所以语法只有这一份实现，
/// 其余地方一律调它。
///
/// 与之配套的是两条纪律：
///   * 本模块是**纯函数**：输入输出都是字符串，不访问文件系统、不依赖平台 API、
///     没有全局状态。所以 Windows 的大小写规则也能在 macOS 上被真实执行。
///   * 掩码里的分隔符**恒为 `/`**，与平台无关。理由见 mask.cpp 顶部。
///
/// 本文件只做「一段掩码」的事（语法 + 匹配）。「包含/排除如何叠加」在
/// maskfilter.h 里，那是另一层概念，不混进来。
///

/// 掩码语义所在的平台。
///
/// 只有一件事依赖它：默认的大小写敏感性（Windows 不敏感、Unix 敏感）。
/// 之所以把它做成一个**显式参数**而不是在实现里查 `Q_OS_WIN`，是因为那条
/// 分支写进 `#ifdef` 之后，在开发机（macOS）上永远不被执行，也就永远测不到。
/// 做成参数，两种平台的默认值都能在任意平台上断言——与 `PathUtils::Style`
/// 是同一个手法。
enum class MaskPlatform
{
    Posix,
    Windows,
};

/// 本机对应的平台（编译期决定，只在需要「默认值」的调用点使用）。
MaskPlatform currentMaskPlatform();

/// 某平台的默认大小写策略：Windows 不敏感，Unix 敏感。
Qt::CaseSensitivity defaultCaseSensitivity(MaskPlatform platform);

/// 平台标识（写日志与测试失败信息用）。
const char *maskPlatformIdentifier(MaskPlatform platform);

///
/// \brief 被匹配的条目：名字 + 相对路径。
///
/// 为什么两样都要带上
/// ----------------------
/// `*.txt` 与 `src/*.txt` 是两类不同的掩码。前者不含 `/`，用户的意图是
/// 「任意目录下的 .txt」；如果拿整条相对路径去匹配它，`src/a.txt` 会因为
/// 「`*` 不跨 `/`」而不匹配——与所有人的直觉相反。含 `/` 的掩码才应当拿
/// 整条相对路径去匹配。
///
/// 也就是说，「这条掩码是按名字匹配还是按路径匹配」是由**掩码自己**决定的，
/// 而匹配时手里必须同时有名字与路径。这个结构就是这两样东西的载体，
/// 免得每个调用点各自记一遍哪个该给哪个。
///
struct MaskSubject
{
    QString name; ///< 最后一段（不含分隔符）
    QString path; ///< 相对路径，一律用 `/` 分隔

    /// 只有一个名字（无目录信息）：名字本身也是路径。
    static MaskSubject forName(const QString &name);

    /// 给一条相对路径，自动取出最后一段作为名字。
    static MaskSubject forPath(const QString &path);

    bool isValid() const { return !name.isEmpty(); }
};

///
/// \brief 掩码解析失败的位置与原因。
///
/// 为什么要带上「列号 + 长度」而不是一句话
/// -------------------------------------------
/// 界面在用户敲掩码时就要能就地标红。只给「掩码语法错误」的话，用户面对一个
/// 二十字符的掩码只能自己逐个字符猜；给出列号与长度，输入框可以直接选中出错的
/// 那一段。`hint` 则是「怎么改」——尤其是 Windows 用户把 `\` 当分隔符写进掩码
/// 这一种，它需要的不是「语法错误」四个字，而是「请改写成 `/`」。
///
struct MaskParseError
{
    int column = -1; ///< 出错的起始列（0 起，相对掩码文本）；-1 表示与位置无关
    int length = 0;  ///< 出错片段的长度，便于界面高亮
    QString message; ///< 出了什么问题
    QString hint;    ///< 怎么改（可为空）

    bool isEmpty() const { return message.isEmpty(); }

    /// 「第 N 列：<message>（<hint>）」
    QString describe() const;
};

// 结果结构体按值持有 Mask，因此要先于它声明、后于它定义。
struct MaskParseResult;

///
/// \brief 编译后的掩码。
///
/// 编译一次、匹配多次。`compile()` 是纯函数：同一段文本永远得到同一个结果，
/// 且不修改任何全局状态（因此可以并行调用，也可以缓存）。
///
class Mask
{
public:
    /// 默认构造得到一段**永远不匹配**的无效掩码（匹配函数恒为 false）。
    Mask();

    ///
    /// \brief 解析一段掩码。失败时返回的原因带列号与建议。
    ///
    /// 支持 `*`、`?`、`[...]`（含 `[!...]` / `[^...]` 取反与 `[a-z]` 区间）、
    /// `**`（整段时跨目录），以及用 `\` 转义 `* ? [ ] - # \` 本字。
    ///
    static MaskParseResult compile(const QString &pattern);

    bool isValid() const { return m_valid; }

    /// 原始文本（不做任何归一，便于界面回显用户写的东西）。
    const QString &pattern() const { return m_pattern; }

    /// 归一后的文本：去掉首尾空白与结尾的 `/`。
    const QString &normalizedPattern() const { return m_normalized; }

    /// 这条掩码是否按整条相对路径匹配（即含有 `/`）。
    ///
    /// 不含 `/` 的掩码按**名字**匹配，因此 `*.txt` 能命中任意目录下的 `.txt`。
    bool targetsPath() const { return m_targetsPath; }

    /// 掩码段数（由 `/` 分段）。
    int segmentCount() const { return m_segments.size(); }

    /// 整段的 `**` 个数。
    ///
    /// 单独暴露出来是为了能把「段内的 `**` 不等于跨目录」这条规则钉死：
    /// 只看匹配结果的话，`a**b` 与 `a*b` 在某些样本上恰好同解，
    /// 断言就落不到这条规则上。
    int crossDirectoryCount() const { return m_crossDirectoryCount; }

    /// 原子总数（调试与结构断言用）。
    int atomCount() const;

    ///
    /// \brief 匹配一个条目。
    ///
    /// `cs` 决定大小写是否敏感。调用方通常传 `MaskFilter::caseSensitivity()`，
    /// 因为那是「平台默认 + 用户显式覆盖」算完之后的结论。
    ///
    bool matches(const MaskSubject &subject, Qt::CaseSensitivity cs) const;

    /// 是否按「路径」这一侧匹配（供调用方与界面解释行为）。
    QString describe() const;

private:
    struct Atom
    {
        enum class Kind
        {
            Literal,     ///< 一个字面字符
            AnyChar,     ///< `?`
            AnyRun,      ///< `*`
            CharSet,     ///< `[...]`
        };

        /// 字符集里的一段：`first == last` 表示单个字符。
        struct SetRange
        {
            QChar first;
            QChar last;
        };

        Kind kind = Kind::Literal;
        QChar literal;               ///< Literal
        QVector<SetRange> setRanges; ///< CharSet
        bool setNegated = false;     ///< CharSet 是否取反
    };

    struct Segment
    {
        QVector<Atom> atoms;
        bool isCrossDirectory = false; ///< 整段就是一个 `**`
    };

    // 解析与匹配的实现细节。放在类内是因为它们要引用 Atom/Segment 这两个私有类型；
    // 做成自由函数就只能把它们暴露成公开类型，那是把实现细节写进接口。

    /// 单个原子能否吃掉一个字符。`AnyRun` 不在这里处理（它由 matchSegment 主循环吃掉）。
    static bool atomMatchesChar(const Atom &atom, QChar c, Qt::CaseSensitivity cs);

    /// 字符集成员判定。大小写不敏感时对「原字符」与「换过大小写的字符」各试一次，
    /// 理由见实现处的注释。
    static bool setContains(const Atom &atom, QChar c, Qt::CaseSensitivity cs);

    /// 段内匹配：掩码原子序列 vs 一个不含分隔符的字符串。
    static bool matchesSegment(const QVector<Atom> &atoms, const QString &text,
                              Qt::CaseSensitivity cs);

    /// 解析一段掩码（不含 `/`）。列号以 columnOffset 为基准换算回整段掩码文本。
    static bool compileSegment(const QString &segment, int columnOffset, Segment *out,
                              MaskParseError *error);

    bool m_valid = false;
    QString m_pattern;
    QString m_normalized;
    bool m_targetsPath = false;
    QVector<Segment> m_segments;
    int m_crossDirectoryCount = 0;
};

///
/// \brief 一段掩码的解析结果。
///
/// 用「结果结构体」而不是 `bool + 出参`：调用方拿到失败时几乎总要立刻把
/// 位置与原因显示出来——用出参就得在每一处调用点都留一个 `MaskParseError`
/// 变量，而且很容易在失败分支里忘了读它。
///
struct MaskParseResult
{
    Mask mask;
    MaskParseError error;

    bool ok() const { return error.isEmpty(); }
};

///
/// \brief 语法速查里的一条示例：一段文本，以及它是否应当被接受。
///
/// 「示例」不是写在文档里的字符串，而是**可执行的断言**——见 maskSyntaxReference()。
///
struct MaskSyntaxSample
{
    QString text;          ///< 被测条目（名字或 `/` 分隔的路径）
    bool matches = false;  ///< 期望结果：是否被接受

    MaskSyntaxSample() {}
    MaskSyntaxSample(const QString &textValue, bool matchesValue)
        : text(textValue), matches(matchesValue) {}
};

///
/// \brief 掩码语法速查里的一行。
struct MaskSyntaxEntry
{
    enum class Kind
    {
        Mask,        ///< pattern 是一段掩码，按「单独作为包含规则」测试
        Declaration, ///< pattern 是过滤声明里的一整行（可带 `-` 前缀或 `#` 注释）
    };

    Kind kind = Kind::Mask;
    QString pattern;
    QString meaning;
    QVector<MaskSyntaxSample> samples;

    const char *kindIdentifier() const;
};

///
/// \brief 掩码语法速查的全部条目（FILT-001 的第 4 条完成标准）。
///
/// 为什么把「文档表格」做成数据
/// -------------------------------
/// FILT-011 的边界写得很明确：**文档必须与实现同源**，文档里的示例要与测试语料
/// 用同一个数据源。手写一份 markdown 表格是做不到这一点的——它必然在某次修改
/// 之后与实现分家，而错误方式是「帮助里说 `[!a]` 是取反、程序其实不认」这种
/// 用户完全无法自查的偏差。
///
/// 所以速查表在这里是**数据**，并且每条都自带可执行样本；测试会逐条把样本跑一遍
/// （见 Tests/Filter），实现改了而速查表没改会立刻红。帮助页与本文档的表格
/// 都由 maskSyntaxReferenceText() 生成。
///
QVector<MaskSyntaxEntry> maskSyntaxReference();

/// 速查表的纯文本形式（表头 + 对齐的若干行），供帮助页与文档使用。
QString maskSyntaxReferenceText();

} // namespace Filter
} // namespace LqCompare

#endif // LQCOMPARE_MASK_H
