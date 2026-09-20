#include "tst_filter.h"

#include "mask.h"
#include "maskfilter.h"

#include <QElapsedTimer>
#include <QSet>
#include <QStringList>

namespace {

using LqCompare::Filter::Mask;
using LqCompare::Filter::MaskDecision;
using LqCompare::Filter::MaskFilter;
using LqCompare::Filter::MaskFilterParseResult;
using LqCompare::Filter::MaskFilterPreview;
using LqCompare::Filter::MaskParseResult;
using LqCompare::Filter::MaskPlatform;
using LqCompare::Filter::MaskRuleError;
using LqCompare::Filter::MaskRuleKind;
using LqCompare::Filter::MaskSubject;
using LqCompare::Filter::MaskSyntaxEntry;
using LqCompare::Filter::MaskSyntaxSample;
using LqCompare::Filter::MaskVerdict;

///
/// \brief 解析一段掩码并把它当成「已经成功」返回。
///
/// 不在辅助函数里写 QVERIFY：那个宏在失败时会 `return`，写在返回非 void 的函数里
/// 会编译不过（或者悄悄返回一个默认值，让失败变成一条别处的假错误）。
/// 所以断言留在调用点，这里只管少写一遍解析样板。
///
Mask compiledFor(const QString &pattern)
{
    return Mask::compile(pattern).mask;
}

MaskFilter filterFor(const QString &declaration, MaskPlatform platform = MaskPlatform::Posix)
{
    return MaskFilter::parse(declaration, platform).filter;
}

bool hitsPath(const Mask &mask, const QString &path, Qt::CaseSensitivity cs = Qt::CaseSensitive)
{
    return mask.matches(MaskSubject::forPath(path), cs);
}

bool hitsName(const Mask &mask, const QString &name, Qt::CaseSensitivity cs = Qt::CaseSensitive)
{
    return mask.matches(MaskSubject::forName(name), cs);
}

bool acceptsName(const MaskFilter &filter, const QString &name)
{
    return filter.accepts(MaskSubject::forName(name));
}

QStringList nameList(const QString &one, const QString &two, const QString &three)
{
    QStringList names;
    names << one << two << three;
    return names;
}

} // namespace

namespace QTest {

template <>
char *toString(const LqCompare::Filter::MaskPlatform &platform)
{
    return qstrdup(LqCompare::Filter::maskPlatformIdentifier(platform));
}

template <>
char *toString(const LqCompare::Filter::MaskVerdict &verdict)
{
    return qstrdup(LqCompare::Filter::maskVerdictIdentifier(verdict));
}

template <>
char *toString(const LqCompare::Filter::MaskRuleKind &kind)
{
    return qstrdup(LqCompare::Filter::maskRuleKindIdentifier(kind));
}

} // namespace QTest

// -----------------------------------------------------------------------------
// initTestCase
// -----------------------------------------------------------------------------

void TstFilter::initTestCase()
{
    // 套件本身不持全局状态，因此没有 init()/cleanup()。这里只做一件启动自检：
    // 语法速查表必须是有内容的——它是帮助页与文档的唯一数据源，
    // 空了会让「文档与实现同源」这条约束悄悄失效（下面第 H 组才去逐条验证内容）。
    QVERIFY(!LqCompare::Filter::maskSyntaxReference().isEmpty());
}

// -----------------------------------------------------------------------------
// A 掩码基本语义（标准第 1 条）
// -----------------------------------------------------------------------------

void TstFilter::starMatchesAnyRunInsideOneSegment()
{
    const Mask mask = compiledFor(QStringLiteral("src/*.txt"));
    QVERIFY(mask.isValid());
    QVERIFY(hitsPath(mask, QStringLiteral("src/a.txt")));
    QVERIFY(hitsPath(mask, QStringLiteral("src/a.b.txt")));
    QVERIFY(hitsPath(mask, QStringLiteral("src/.txt")));
    QVERIFY(!hitsPath(mask, QStringLiteral("other/a.txt")));
}

void TstFilter::starMatchesEmptyRun()
{
    // `*` 可以吃掉零个字符。这条很容易在实现时漏掉（写成「至少一个」），
    // 而现象是 `*.txt` 匹配不了 `.txt` 这类"只有扩展名"的文件。
    const Mask mask = compiledFor(QStringLiteral("*.txt"));
    QVERIFY(hitsName(mask, QStringLiteral(".txt")));
    QVERIFY(hitsName(mask, QStringLiteral("a.txt")));
}

void TstFilter::starDoesNotCrossSeparator()
{
    const Mask mask = compiledFor(QStringLiteral("src/*.txt"));
    QVERIFY(!hitsPath(mask, QStringLiteral("src/a/b.txt")));
    QVERIFY(!hitsPath(mask, QStringLiteral("src/x/a/b.txt")));
}

void TstFilter::questionMatchesExactlyOneCharacter()
{
    const Mask mask = compiledFor(QStringLiteral("?.txt"));
    QVERIFY(hitsName(mask, QStringLiteral("a.txt")));
    QVERIFY(hitsName(mask, QStringLiteral("9.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("ab.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("txt")));
    QVERIFY(!hitsName(mask, QStringLiteral(".txt")));
}

void TstFilter::questionDoesNotMatchSeparator()
{
    // `a?b` 不命中 `a/b`。注意它**不是**靠「`?` 拒绝吃 `/`」实现的，而是靠
    // 「不含 `/` 的掩码按**名字**匹配」——`a/b` 的名字是 `b`，与 `a?b` 对不上。
    //
    // 这条规则（掩码里有 `/` 才算路径）才是用户能观察到的那一条；它同时让
    // 「`?`、`*`、字符集都不会把 `/` 吃掉」成为结构性事实：段是按 `/` 切出来的，
    // 段里根本不会有 `/`。测试断言按名字匹配时暴露出来，是因为「`?` 吃掉分隔符」
    // 这件事在掩码里已经写不出来了。
    const Mask mask = compiledFor(QStringLiteral("a?b"));
    QVERIFY(hitsName(mask, QStringLiteral("axb")));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/b")));
}

void TstFilter::literalMatchesItselfOnly()
{
    const Mask mask = compiledFor(QStringLiteral("README"));
    QVERIFY(hitsName(mask, QStringLiteral("README")));
    QVERIFY(!hitsName(mask, QStringLiteral("readme")));
    QVERIFY(!hitsName(mask, QStringLiteral("README.md")));
}

void TstFilter::maskWithoutSeparatorMatchesNameAtAnyDepth()
{
    // 这是「文件掩码」最核心的一条：`*.txt` 必须能在任意目录下命中，
    // 否则用户得为每个目录写一条掩码。实现靠的是「不含 `/` 就按名字匹配」，
    // 而不是「让 `*` 跨目录」——后者会把 `src/*.txt` 也变成跨目录的，
    // 而那恰好是用户不想要的。
    const Mask mask = compiledFor(QStringLiteral("*.txt"));
    QVERIFY(hitsPath(mask, QStringLiteral("a.txt")));
    QVERIFY(hitsPath(mask, QStringLiteral("src/a.txt")));
    QVERIFY(hitsPath(mask, QStringLiteral("src/deep/nested/a.txt")));
}

void TstFilter::maskWithSeparatorMatchesRelativePathFromTheStart()
{
    // 含 `/` 的掩码按相对路径匹配，而且是**从起点**匹配：`a/*` 不该命中 `x/a/b`。
    // 若实现成「任意位置开始匹配」，废弃目录里的同名子目录会被一起过滤掉，
    // 而用户看不出差别在哪。
    const Mask mask = compiledFor(QStringLiteral("a/*"));
    QVERIFY(mask.targetsPath());
    QVERIFY(hitsPath(mask, QStringLiteral("a/b")));
    QVERIFY(!hitsPath(mask, QStringLiteral("x/a/b")));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/b/c")));
}

void TstFilter::subjectWithEmptyNameNeverMatches()
{
    const Mask mask = compiledFor(QStringLiteral("*"));
    QVERIFY(!mask.matches(MaskSubject(), Qt::CaseSensitive));
    QVERIFY(!mask.matches(MaskSubject::forName(QString()), Qt::CaseSensitive));

    // 连 `**`（能匹配任意多段）也不能命中一个「没有名字」的条目——
    // 空名字意味着调用方还没填好，此时给出 true 会让过滤结果莫名其妙地少一项。
    const Mask everything = compiledFor(QStringLiteral("**"));
    QVERIFY(!everything.matches(MaskSubject(), Qt::CaseSensitive));
}

void TstFilter::defaultConstructedMaskIsInvalidAndMatchesNothing()
{
    const Mask mask;
    QVERIFY(!mask.isValid());
    QVERIFY(!hitsName(mask, QStringLiteral("a.txt")));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/b")));
    QCOMPARE(mask.segmentCount(), 0);
    QCOMPARE(mask.describe(), QStringLiteral("无效掩码"));
}

void TstFilter::maskNormalizesSurroundingWhitespace()
{
    const MaskParseResult result = Mask::compile(QStringLiteral("  *.txt\t"));
    QVERIFY(result.ok());
    QCOMPARE(result.mask.normalizedPattern(), QStringLiteral("*.txt"));
    // 原始文本要原样留着，界面回显给用户看的必须是他自己敲的那串。
    QCOMPARE(result.mask.pattern(), QStringLiteral("  *.txt\t"));
    QVERIFY(hitsName(result.mask, QStringLiteral("a.txt")));
}

void TstFilter::maskIgnoresLeadingAndTrailingSeparators()
{
    const Mask trailing = compiledFor(QStringLiteral("build/"));
    QCOMPARE(trailing.normalizedPattern(), QStringLiteral("build"));
    QVERIFY(!trailing.targetsPath());
    QVERIFY(hitsName(trailing, QStringLiteral("build")));
    QVERIFY(!hitsName(trailing, QStringLiteral("buildx")));

    const Mask leading = compiledFor(QStringLiteral("/build"));
    QCOMPARE(leading.normalizedPattern(), QStringLiteral("build"));
    QVERIFY(!leading.targetsPath());
    QVERIFY(hitsName(leading, QStringLiteral("build")));

    const Mask both = compiledFor(QStringLiteral("/build/"));
    QCOMPARE(both.normalizedPattern(), QStringLiteral("build"));
}

void TstFilter::repeatedSeparatorsCollapse()
{
    const Mask mask = compiledFor(QStringLiteral("a//b"));
    QVERIFY(mask.isValid());
    QCOMPARE(mask.segmentCount(), 2);
    QVERIFY(hitsPath(mask, QStringLiteral("a/b")));
    QVERIFY(hitsPath(mask, QStringLiteral("a//b")));
}

// -----------------------------------------------------------------------------
// B 字符集（标准第 1 条）
// -----------------------------------------------------------------------------

void TstFilter::setMatchesAnyMember()
{
    const Mask mask = compiledFor(QStringLiteral("[abc].txt"));
    QVERIFY(hitsName(mask, QStringLiteral("a.txt")));
    QVERIFY(hitsName(mask, QStringLiteral("b.txt")));
    QVERIFY(hitsName(mask, QStringLiteral("c.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("d.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("ab.txt")));
}

void TstFilter::setRangeMatchesInclusiveBounds()
{
    const Mask mask = compiledFor(QStringLiteral("[a-z0-9].bin"));
    QVERIFY(hitsName(mask, QStringLiteral("a.bin")));
    QVERIFY(hitsName(mask, QStringLiteral("z.bin"))); // 上界含
    QVERIFY(hitsName(mask, QStringLiteral("0.bin")));
    QVERIFY(hitsName(mask, QStringLiteral("9.bin"))); // 下界含
    QVERIFY(hitsName(mask, QStringLiteral("m.bin")));
    QVERIFY(!hitsName(mask, QStringLiteral("A.bin"))); // 敏感模式下大写不在区间里
    QVERIFY(!hitsName(mask, QStringLiteral("-.bin"))); // `-` 是字符，不是新增的区间
}

void TstFilter::setNegatedMatchesEverythingElse()
{
    const Mask mask = compiledFor(QStringLiteral("[!abc].txt"));
    QVERIFY(hitsName(mask, QStringLiteral("d.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("a.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("b.txt")));
}

void TstFilter::caretIsAnAliasForNegation()
{
    // `[^...]` 与 `[!...]` 等价。两种写法在别的工具里都很常见，
    // 只认一种会让从 grep 或 .gitignore 抄过来的掩码静静地变成
    // 「匹配集合里那几个字符」——正好相反的意思，而且看不出来。
    const Mask mask = compiledFor(QStringLiteral("[^abc].txt"));
    QVERIFY(hitsName(mask, QStringLiteral("d.txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("a.txt")));

    const Mask bang = compiledFor(QStringLiteral("[!abc].txt"));
    for (const QString &sample : { QStringLiteral("a.txt"), QStringLiteral("d.txt"),
                                   QStringLiteral("9.txt") }) {
        QCOMPARE(hitsName(mask, sample), hitsName(bang, sample));
    }
}

void TstFilter::closingBracketAtStartIsALiteral()
{
    // POSIX 的规矩：`]` 写在集合最前面表示它自己。因此 `[]]` 是「匹配 `]`」。
    const Mask mask = compiledFor(QStringLiteral("[]].txt"));
    QVERIFY(mask.isValid());
    QVERIFY(hitsName(mask, QStringLiteral("].txt")));
    QVERIFY(!hitsName(mask, QStringLiteral("a.txt")));
}

void TstFilter::leadingAndTrailingDashAreLiterals()
{
    // `-` 在集合的首尾退化成它自己（与 POSIX 一致），这样「匹配减号」不需要转义。
    const Mask trailing = compiledFor(QStringLiteral("[a-].txt"));
    QVERIFY(hitsName(trailing, QStringLiteral("a.txt")));
    QVERIFY(hitsName(trailing, QStringLiteral("-.txt")));

    const Mask leading = compiledFor(QStringLiteral("[-a].txt"));
    QVERIFY(hitsName(leading, QStringLiteral("a.txt")));
    QVERIFY(hitsName(leading, QStringLiteral("-.txt")));
}

void TstFilter::separatorCannotBeASetMemberBecauseItSplitsFirst()
{
    // 想写「字符集里包含 `/`」是不可能的：`/` 在掩码里先是路径分隔符，
    // 于是 `[/]` 会被切成 `[` 与 `]` 两段，第一段就是一个没闭合的字符集。
    //
    // 这**不是**缺陷，而是「段里不会出现 `/`」这条结构性保证的另一面：
    // 正因为 `/` 永远先被切走，`?` / `*` / 字符集才都不可能把分隔符吃掉。
    // 这里把这个结果钉住，免得将来有人"友好地"让 `/` 在括号里变成普通字符——
    // 那会引入一个「同一段里 `/` 有两种含义」的状态。
    const MaskParseResult result = Mask::compile(QStringLiteral("[/]"));
    QVERIFY(!result.ok());
    QVERIFY(result.error.message.contains(QStringLiteral("字符集")));
}

void TstFilter::backslashInsideSetIsALiteralMember()
{
    // 集合内部**不认**转义（POSIX 也是这样：想要 `]` 就写在最前面，想要 `-`
    // 就写在首尾）。于是 `[\]` 里的 `\` 就是一个普通成员，而不是"转义了 `]`"。
    const Mask mask = compiledFor(QStringLiteral("a[\\]b"));
    QVERIFY(mask.isValid());
    QVERIFY(hitsName(mask, QStringLiteral("a\\b")));
    QVERIFY(!hitsName(mask, QStringLiteral("ab")));
}

void TstFilter::invertedRangeIsRejected()
{
    // `[z-a]` 是一个永远匹配不到任何东西的字符集，而后面的逻辑看起来完全正常。
    // 与其静默吞掉，不如报出来——这正是「畸形输入」里最典型的一类：
    // 它能跑、只是永远不生效。
    const MaskParseResult result = Mask::compile(QStringLiteral("x[z-a]y"));
    QVERIFY(!result.ok());
    QCOMPARE(result.error.column, 2);
    QCOMPARE(result.error.length, 3);
    QVERIFY(result.error.message.contains(QStringLiteral("区间")));
}

void TstFilter::unterminatedSetIsRejected()
{
    // 刻意**不**把未闭合的 `[` 当成字面量。当字面量的话，用户把 `[abc` 敲漏了
    // 一个 `]`，掩码会变成「匹配字符串 `[abc`」——过滤看起来还在工作，
    // 只是永远不命中，而用户在界面上完全看不出问题。
    const MaskParseResult result = Mask::compile(QStringLiteral("[abc"));
    QVERIFY(!result.ok());
    QCOMPARE(result.error.column, 0);
    QCOMPARE(result.error.length, 1);
    QVERIFY(result.error.message.contains(QStringLiteral("没有闭合")));
    QVERIFY(result.error.hint.contains(QStringLiteral("\\[")));
}

void TstFilter::emptySetIsRejected()
{
    // `[]` 按 POSIX 的「首字符 `]` 是字面量」规则读下去就是「没闭合」，
    // 一条规则同时覆盖两种情况：真正的空集合也进不来。
    const MaskParseResult result = Mask::compile(QStringLiteral("[]"));
    QVERIFY(!result.ok());
    QVERIFY(result.error.message.contains(QStringLiteral("没有闭合")));
}

void TstFilter::multipleRangesInOneSetWork()
{
    const Mask mask = compiledFor(QStringLiteral("[a-cx-z0-2]"));
    QVERIFY(hitsName(mask, QStringLiteral("a")));
    QVERIFY(hitsName(mask, QStringLiteral("c")));
    QVERIFY(hitsName(mask, QStringLiteral("x")));
    QVERIFY(hitsName(mask, QStringLiteral("z")));
    QVERIFY(hitsName(mask, QStringLiteral("0")));
    QVERIFY(hitsName(mask, QStringLiteral("2")));
    QVERIFY(!hitsName(mask, QStringLiteral("d")));
    QVERIFY(!hitsName(mask, QStringLiteral("3")));
}

// -----------------------------------------------------------------------------
// C 跨目录（标准第 1 条）
// -----------------------------------------------------------------------------

void TstFilter::doubleStarMatchesZeroSegments()
{
    // `**` 是「零个或多个路径段」。于是 `build/**` 也命中 `build` 本身——
    // 这对排除一个子树至关重要：用户想排除 `build` 时最自然的写法就是
    // `build/**`，如果它不命中 `build` 这个目录本身，目录树里那一行还在，
    // 看起来像是过滤没生效。
    const Mask mask = compiledFor(QStringLiteral("build/**"));
    QVERIFY(hitsPath(mask, QStringLiteral("build")));
    QVERIFY(hitsPath(mask, QStringLiteral("build/out")));
    QVERIFY(hitsPath(mask, QStringLiteral("build/out/o.txt")));
    QVERIFY(!hitsPath(mask, QStringLiteral("buildx")));
}

void TstFilter::doubleStarMatchesMultipleSegments()
{
    const Mask mask = compiledFor(QStringLiteral("**"));
    QVERIFY(hitsName(mask, QStringLiteral("a")));
    QVERIFY(hitsPath(mask, QStringLiteral("a/b")));
    QVERIFY(hitsPath(mask, QStringLiteral("a/b/c/d/e")));
}

void TstFilter::doubleStarInTheMiddleSpansArbitraryDepth()
{
    const Mask mask = compiledFor(QStringLiteral("a/**/b"));
    QVERIFY(hitsPath(mask, QStringLiteral("a/b")));
    QVERIFY(hitsPath(mask, QStringLiteral("a/x/b")));
    QVERIFY(hitsPath(mask, QStringLiteral("a/x/y/b")));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/b/c")));
    QVERIFY(!hitsPath(mask, QStringLiteral("x/a/b")));
}

void TstFilter::doubleStarDoesNotMatchDifferentTail()
{
    const Mask mask = compiledFor(QStringLiteral("a/**/b"));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/bx")));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/x/bx")));
    QVERIFY(!hitsPath(mask, QStringLiteral("ab")));
}

void TstFilter::doubleStarInsideSegmentDegradesToStar()
{
    // `**` 只有**独占一段**时才跨目录；段内等同于 `*`。这是 gitignore / ant /
    // ripgrep 共同的规则，也是唯一能让「`a**b` 能不能跨目录」有确定答案的写法。
    // 不这样规定的话，`a**b` 与 `a*b` 就没有任何可预期的区别，用户只能靠试。
    const Mask mask = compiledFor(QStringLiteral("a**b"));
    QCOMPARE(mask.crossDirectoryCount(), 0);
    QVERIFY(hitsName(mask, QStringLiteral("axxb")));
    QVERIFY(hitsName(mask, QStringLiteral("ab")));
    QVERIFY(!hitsPath(mask, QStringLiteral("a/x/b")));
}

void TstFilter::doubleStarIsCountedPerSegment()
{
    QCOMPARE(compiledFor(QStringLiteral("a/**/b/**")).crossDirectoryCount(), 2);
    QCOMPARE(compiledFor(QStringLiteral("**")).crossDirectoryCount(), 1);
    // 段内不算。
    QCOMPARE(compiledFor(QStringLiteral("a/**b/**c")).crossDirectoryCount(), 0);
}

void TstFilter::doubleStarSegmentIsRecordedStructurally()
{
    const Mask mask = compiledFor(QStringLiteral("a/**/b"));
    QCOMPARE(mask.segmentCount(), 3);
    QCOMPARE(mask.crossDirectoryCount(), 1);
    QVERIFY(mask.targetsPath());
    QVERIFY(mask.describe().contains(QStringLiteral("跨目录")));
}

void TstFilter::absoluteLookingMaskIsTreatedAsRelative()
{
    // 掩码路径是相对路径，因此开头的 `/` 被忽略：`/a/b` 与 `a/b` 是一件事。
    // 不这样做的话，用户从资源管理器里复制来一条带前导斜杠的路径就会得到
    // 一个永远不命中的掩码——而它看起来完全合理。
    const Mask mask = compiledFor(QStringLiteral("/a/b"));
    QCOMPARE(mask.normalizedPattern(), QStringLiteral("a/b"));
    QVERIFY(hitsPath(mask, QStringLiteral("a/b")));

    // 被测路径那一侧的 `/` 也同样被丢掉：切分是按 `/` 做的，空段自然消失。
    QVERIFY(hitsPath(mask, QStringLiteral("/a/b")));
}

// -----------------------------------------------------------------------------
// D 大小写策略（标准第 3 条）
// -----------------------------------------------------------------------------

void TstFilter::windowsDefaultsToCaseInsensitive()
{
    QCOMPARE(LqCompare::Filter::defaultCaseSensitivity(MaskPlatform::Windows),
             Qt::CaseInsensitive);
}

void TstFilter::posixDefaultsToCaseSensitive()
{
    QCOMPARE(LqCompare::Filter::defaultCaseSensitivity(MaskPlatform::Posix),
             Qt::CaseSensitive);
}

void TstFilter::hostDefaultFollowsCurrentPlatform()
{
    // 两个分支都写出来，是为了让本机（macOS）与将来在 Windows 上跑 CI 时
    // 都表达清晰的期望；写成「默认值 == 某个函数返回的东西」是同义反复。
#ifdef Q_OS_WIN
    QCOMPARE(LqCompare::Filter::currentMaskPlatform(), MaskPlatform::Windows);
    QCOMPARE(LqCompare::Filter::defaultCaseSensitivity(LqCompare::Filter::currentMaskPlatform()),
             Qt::CaseInsensitive);
#else
    QCOMPARE(LqCompare::Filter::currentMaskPlatform(), MaskPlatform::Posix);
    QCOMPARE(LqCompare::Filter::defaultCaseSensitivity(LqCompare::Filter::currentMaskPlatform()),
             Qt::CaseSensitive);
#endif
}

void TstFilter::filterDefaultPlatformIsTheHostPlatform()
{
    // parse() 的平台参数有默认值。这个用例盯的是「默认值不会漂」——
    // 一旦有人把它改成写死的 Posix，Windows 上的默认行为就悄悄变了。
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("*.txt"));
    QCOMPARE(result.filter.platform(), LqCompare::Filter::currentMaskPlatform());
}

void TstFilter::explicitCaseSensitivityOverridesPlatformDefault()
{
    // 「可显式覆盖」是本条目第 3 条完成标准里明确要求的那半句。
    // 在 Linux 上比一份来自 Windows 的目录树时，用户需要能说「这里按不敏感算」。
    MaskFilter filter = filterFor(QStringLiteral("*.TXT"), MaskPlatform::Posix);
    QVERIFY(!filter.isCaseSensitivityOverridden());
    QCOMPARE(filter.caseSensitivity(), Qt::CaseSensitive);
    QVERIFY(!acceptsName(filter, QStringLiteral("a.txt")));

    filter.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(filter.isCaseSensitivityOverridden());
    QVERIFY(acceptsName(filter, QStringLiteral("a.txt")));
    QVERIFY(acceptsName(filter, QStringLiteral("a.TXT")));
}

void TstFilter::clearingOverrideReturnsToPlatformDefault()
{
    MaskFilter filter = filterFor(QStringLiteral("*.txt"), MaskPlatform::Windows);
    QCOMPARE(filter.caseSensitivity(), Qt::CaseInsensitive);

    filter.setCaseSensitivity(Qt::CaseSensitive);
    QCOMPARE(filter.caseSensitivity(), Qt::CaseSensitive);

    filter.clearCaseSensitivityOverride();
    QCOMPARE(filter.caseSensitivity(), Qt::CaseInsensitive);
    QVERIFY(!filter.isCaseSensitivityOverridden());
}

void TstFilter::caseInsensitiveSetMatchesInBothDirections()
{
    // 字符集在大小写不敏感时要**两个方向**都成立：`[a-z]` 命中 `A`，
    // `[A-Z]` 命中 `a`。只做一个方向（比如只把输入折成小写）会让
    // `[A-Z]` 在小写输入上永远不命中，而这类掩码恰好是从 Windows 那边
    // 抄过来的最常见写法。
    const Mask lower = compiledFor(QStringLiteral("[a-z].txt"));
    QVERIFY(hitsName(lower, QStringLiteral("A.txt"), Qt::CaseInsensitive));
    QVERIFY(!hitsName(lower, QStringLiteral("A.txt"), Qt::CaseSensitive));

    const Mask upper = compiledFor(QStringLiteral("[A-Z].txt"));
    QVERIFY(hitsName(upper, QStringLiteral("q.txt"), Qt::CaseInsensitive));
    QVERIFY(!hitsName(upper, QStringLiteral("q.txt"), Qt::CaseSensitive));

    // 不该命中仍然不命中：大小写不敏感不能变成「什么都匹配」。
    QVERIFY(!hitsName(lower, QStringLiteral("0.txt"), Qt::CaseInsensitive));
}

void TstFilter::caseInsensitiveRangeDoesNotInvert()
{
    // `[A-_]` 覆盖 ASCII 的 A.._（含 A-Z、`[`、`\`、`]`、`^`、`_`）。
    // 大小写不敏感时它应当也命中 `a`（因为 `A` 在区间里）。
    //
    // 这条用例专门盯着「把区间两端也折成小写再比」那种实现：折叠后区间变成
    // `a`..`_`，而 `a`(0x61) 比 `_`(0x5F) 大——区间反了，字符集从此永远匹配不到
    // 任何东西，且表面上一切正常。反转字符再比一遍不会踩这个坑。
    const Mask mask = compiledFor(QStringLiteral("[A-_].txt"));
    QVERIFY(hitsName(mask, QStringLiteral("A.txt"), Qt::CaseInsensitive));
    QVERIFY(hitsName(mask, QStringLiteral("a.txt"), Qt::CaseInsensitive));
    QVERIFY(hitsName(mask, QStringLiteral("_.txt"), Qt::CaseInsensitive));
}

void TstFilter::maskMatchingUsesTheGivenSensitivity()
{
    // 匹配本身是纯函数：大小写策略从参数进来，不从全局状态读。
    // 这正是「同一个掩码对象可以被两种策略各用一次」的前提
    // （界面上「区分大小写」勾选框改动时不必重新编译掩码）。
    const Mask mask = compiledFor(QStringLiteral("Makefile"));
    QVERIFY(hitsName(mask, QStringLiteral("Makefile"), Qt::CaseSensitive));
    QVERIFY(!hitsName(mask, QStringLiteral("makefile"), Qt::CaseSensitive));
    QVERIFY(hitsName(mask, QStringLiteral("makefile"), Qt::CaseInsensitive));
}

// -----------------------------------------------------------------------------
// E 声明解析（标准第 2 条）
// -----------------------------------------------------------------------------

void TstFilter::emptyDeclarationProducesNoRules()
{
    const MaskFilterParseResult result = MaskFilter::parse(QString());
    QVERIFY(result.ok());
    QCOMPARE(result.filter.ruleCount(), 0);
    QVERIFY(result.filter.isEmpty());
}

void TstFilter::blankLinesAreIgnored()
{
    const MaskFilterParseResult result =
            MaskFilter::parse(QStringLiteral("\n  \n\t\n*.cpp\n\n"));
    QVERIFY(result.ok());
    QCOMPARE(result.filter.ruleCount(), 1);
    QCOMPARE(result.filter.rules().first().line, 3);
}

void TstFilter::commentLinesAreIgnored()
{
    const MaskFilterParseResult result =
            MaskFilter::parse(QStringLiteral("# 源码\n  # 缩进的注释也算\n*.cpp"));
    QVERIFY(result.ok());
    QCOMPARE(result.filter.ruleCount(), 1);
    QCOMPARE(result.filter.rules().first().text, QStringLiteral("*.cpp"));
}

void TstFilter::leadingDashMarksExclude()
{
    const MaskFilter filter = filterFor(QStringLiteral("-*.tmp"));
    QCOMPARE(filter.ruleCount(), 1);
    QCOMPARE(filter.excludeCount(), 1);
    QCOMPARE(filter.includeCount(), 0);
    // 掩码文本里不该带那个 `-`：它是声明层的标记，不是掩码的一部分。
    // 留着的话掩码会变成「以减号开头」，排除规则反而失效。
    QCOMPARE(filter.rules().first().text, QStringLiteral("*.tmp"));
    QVERIFY(!acceptsName(filter, QStringLiteral("a.tmp")));
}

void TstFilter::escapedLeadingDashIsALiteralName()
{
    const MaskFilter filter = filterFor(QStringLiteral("\\-.txt"));
    QCOMPARE(filter.ruleCount(), 1);
    QCOMPARE(filter.includeCount(), 1);

    // 归一后的文本仍然带着那个反斜杠：`normalizedPattern()` 修的是**首尾空白与
    // 斜杠**这类外围噪声，不是把掩码"翻译"成另一种写法。界面上回显给用户的
    // 必须是他自己敲的那串——显示成 `-.txt` 的话，他会以为排除标记丢了。
    QCOMPARE(filter.rules().first().text, QStringLiteral("\\-.txt"));

    // 而匹配时那个反斜杠是转义符：命中的是名字里真的有一个减号的条目。
    QVERIFY(acceptsName(filter, QStringLiteral("-.txt")));
    QVERIFY(!acceptsName(filter, QStringLiteral("a.txt")));
}

void TstFilter::hashAfterExcludeMarkerIsLiteral()
{
    // `#` 只在一行的第一个非空白字符位置表示注释。于是 `-#foo` 是「排除名为
    // `#foo` 的条目」，而不是「排除空掩码 + 一行注释」。这样「排除以 # 开头的
    // 文件」不必再发明一套语法。
    const MaskFilter filter = filterFor(QStringLiteral("-#hash.txt"));
    QCOMPARE(filter.ruleCount(), 1);
    QCOMPARE(filter.excludeCount(), 1);
    QCOMPARE(filter.rules().first().text, QStringLiteral("#hash.txt"));
    QVERIFY(!acceptsName(filter, QStringLiteral("#hash.txt")));
    QVERIFY(acceptsName(filter, QStringLiteral("other.txt")));
}

void TstFilter::crlfLineEndingsAreStripped()
{
    // 预设库要能导出给团队共享（FILT-007），而 Windows 上编辑过的文本文件是
    // CRLF 行尾。只按 `\n` 切行的话，每行末尾会多一个 `\r`，于是 `*.tmp` 变成
    // `*.tmp\r`——**静静地对不上任何文件**。用户看到的现象是「导入的预设完全
    // 不起作用」，而掩码本身看起来完美无缺，在界面上几乎无法自查。
    const MaskFilterParseResult result =
            MaskFilter::parse(QStringLiteral("*.tmp\r\n-*.log\r\n"));

    QVERIFY(result.ok());
    QCOMPARE(result.filter.ruleCount(), 2);
    QCOMPARE(result.filter.rules().at(0).text, QStringLiteral("*.tmp"));
    QCOMPARE(result.filter.rules().at(1).text, QStringLiteral("*.log"));
    QVERIFY(acceptsName(result.filter, QStringLiteral("a.tmp")));
    QVERIFY(!acceptsName(result.filter, QStringLiteral("a.log")));
    QVERIFY(!acceptsName(result.filter, QStringLiteral("a.txt")));
}

void TstFilter::loneCarriageReturnIsALineBreak()
{
    const MaskFilterParseResult result =
            MaskFilter::parse(QStringLiteral("*.tmp\r*.log"));
    QVERIFY(result.ok());
    QCOMPARE(result.filter.ruleCount(), 2);
    QCOMPARE(result.filter.rules().at(0).line, 0);
    QCOMPARE(result.filter.rules().at(1).line, 1);
}

void TstFilter::lineNumbersAreReportedForRules()
{
    const MaskFilter filter = filterFor(QStringLiteral("# 注释\n\n*.cpp\n-*.tmp"));
    QCOMPARE(filter.ruleCount(), 2);
    QCOMPARE(filter.rules().at(0).line, 2);
    QCOMPARE(filter.rules().at(0).kind, MaskRuleKind::Include);
    QCOMPARE(filter.rules().at(1).line, 3);
    QCOMPARE(filter.rules().at(1).kind, MaskRuleKind::Exclude);
}

void TstFilter::lineNumbersAreReportedForErrors()
{
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("*.cpp\n\n[abc"));
    QVERIFY(!result.ok());
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(result.errors.first().line, 2);
}

void TstFilter::errorColumnIsInLineCoordinates()
{
    // `-a[`：第 0 列是排除标记，掩码从第 1 列开始，掩码内部第 1 列是个没闭合的
    // `[`。报给界面的必须是**整行**的第 2 列，否则输入框标红的位置会整体左移。
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("-a["));
    QVERIFY(!result.ok());
    QCOMPARE(result.errors.first().line, 0);
    QCOMPARE(result.errors.first().column, 2);
    QVERIFY(result.errors.first().describe().contains(QStringLiteral("第 1 行第 3 列")));
}

void TstFilter::errorColumnAccountsForLeadingWhitespace()
{
    // 排除标记之后的空白也算在列号里：`-   [abc` 的 `[` 在整行的第 5 列
    // （0 起是 4）。掩码在解析时会把那段空白裁掉，所以这里必须把两段偏移加起来——
    // 只加一段的写法会给出一个「差不多对」的位置，而用户照着标红的地方改是错的。
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("-   [abc"));
    QVERIFY(!result.ok());
    QCOMPARE(result.errors.first().column, 4);
}

void TstFilter::trailingBackslashIsRejected()
{
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("*.txt\\"));
    QVERIFY(!result.ok());
    QVERIFY(result.errors.first().message.contains(QStringLiteral("反斜杠后面没有字符")));
}

void TstFilter::windowsStyleSeparatorInsideMaskIsRejectedWithHint()
{
    // 这条是本模块最想拦住的一类输入：Windows 用户把路径分隔符敲进掩码。
    // 若宽容处理成「不认识的转义就原样保留」，`build\out` 会变成 `buildout`，
    // 过滤看起来生效了、只是漏了一批文件——这是最难查的一种错。
    // 报错还要给出**怎么改**：只说「语法错误」用户改不出来。
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("build\\out"));
    QVERIFY(!result.ok());

    const MaskRuleError error = result.errors.first();
    QVERIFY(error.message.contains(QStringLiteral("反斜杠不能转义")));
    QVERIFY(error.message.contains(QStringLiteral("o")));
    QVERIFY(error.hint.contains(QStringLiteral("/")));
    QCOMPARE(error.column, 5);
}

void TstFilter::oneBadLineDoesNotDisableTheOthers()
{
    // 用户在界面上是一行一行改的。若一行写错就整段失效，他会以为是自己把别处
    // 敲坏了，于是去动本来正确的行。所以：**只丢那一行**，错误单独收着。
    const MaskFilterParseResult result = MaskFilter::parse(QStringLiteral("*.cpp\n[abc\n*.h"));

    QVERIFY(!result.ok());
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(result.errors.first().line, 1);

    QCOMPARE(result.filter.ruleCount(), 2);
    QVERIFY(acceptsName(result.filter, QStringLiteral("main.cpp")));
    QVERIFY(acceptsName(result.filter, QStringLiteral("main.h")));
    QVERIFY(!acceptsName(result.filter, QStringLiteral("main.txt")));

    // 多行拼接的提示文案也要能把行号带出来。
    QVERIFY(result.describeErrors().contains(QStringLiteral("第 2 行")));
}

void TstFilter::declarationWithoutTrailingNewlineStillParses()
{
    QCOMPARE(filterFor(QStringLiteral("*.cpp")).ruleCount(), 1);
    QCOMPARE(filterFor(QStringLiteral("*.cpp\n")).ruleCount(), 1);
}

void TstFilter::ruleDescribeMentionsLineAndKind()
{
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*.tmp"));
    QCOMPARE(filter.rules().at(1).describe(), QStringLiteral("第 2 行：排除 `*.tmp`"));
}

// -----------------------------------------------------------------------------
// F 叠加（标准第 2 条）
// -----------------------------------------------------------------------------

void TstFilter::excludeWinsOverInclude()
{
    // 本条目第 2 条完成标准的全部内容。改成包含优先的话，排除掩码就永远不起作用，
    // 而用户没有任何替代写法——「除测试以外的 cpp」用正向掩码是写不出来的。
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*_test.cpp"));
    QCOMPARE(filter.includeCount(), 1);
    QCOMPARE(filter.excludeCount(), 1);

    QVERIFY(acceptsName(filter, QStringLiteral("main.cpp")));
    QVERIFY(!acceptsName(filter, QStringLiteral("main_test.cpp")));

    const MaskDecision decision = filter.decide(MaskSubject::forName(QStringLiteral("main_test.cpp")));
    QCOMPARE(decision.verdict, MaskVerdict::Excluded);
    QCOMPARE(decision.ruleKind, MaskRuleKind::Exclude);
    QCOMPARE(decision.ruleText, QStringLiteral("*_test.cpp"));
}

void TstFilter::includeOnlyActsAsWhitelist()
{
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp"));
    QVERIFY(acceptsName(filter, QStringLiteral("a.cpp")));
    QVERIFY(!acceptsName(filter, QStringLiteral("a.h")));
    QCOMPARE(filter.decide(MaskSubject::forName(QStringLiteral("a.h"))).verdict,
             MaskVerdict::NotMatched);
}

void TstFilter::excludeOnlyKeepsEverythingElse()
{
    // 只填了排除框是正常用法，不是「白名单里一条都没有，于是全隐藏」。
    // 后者会让用户第一次用过滤器时以为程序坏了。
    const MaskFilter filter = filterFor(QStringLiteral("-*.tmp"));
    QVERIFY(acceptsName(filter, QStringLiteral("a.txt")));
    QVERIFY(acceptsName(filter, QStringLiteral("a.cpp")));
    QVERIFY(!acceptsName(filter, QStringLiteral("a.tmp")));

    const MaskDecision kept = filter.decide(MaskSubject::forName(QStringLiteral("a.txt")));
    QCOMPARE(kept.verdict, MaskVerdict::Included);
    QCOMPARE(kept.ruleIndex, -1); // 没有规则参与——靠的是「一条包含规则都没有」
}

void TstFilter::emptyFilterAcceptsEverything()
{
    const MaskFilter filter = filterFor(QString());
    QVERIFY(filter.isEmpty());
    QVERIFY(acceptsName(filter, QStringLiteral("a.txt")));
    QVERIFY(acceptsName(filter, QStringLiteral("a.tmp")));
    QCOMPARE(filter.decide(MaskSubject::forName(QStringLiteral("a.txt"))).ruleIndex, -1);
}

void TstFilter::excludedAndNotMatchedAreDistinct()
{
    // 两者对用户的意义完全不同：一个是「你明确要求不要它」，一个是「你的包含
    // 掩码里没有它」。FILT-006 的批量安全提示与 FILT-011 的「我为什么看不到」
    // 都要靠这个区分。合成一个之后，那两个功能只能重跑一遍匹配去猜。
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*_test.cpp"));

    const MaskDecision excluded =
            filter.decide(MaskSubject::forName(QStringLiteral("main_test.cpp")));
    const MaskDecision notMatched =
            filter.decide(MaskSubject::forName(QStringLiteral("readme.md")));

    QCOMPARE(excluded.verdict, MaskVerdict::Excluded);
    QCOMPARE(notMatched.verdict, MaskVerdict::NotMatched);
    QVERIFY(excluded.describe() != notMatched.describe());
    QCOMPARE(QString::fromLatin1(LqCompare::Filter::maskVerdictIdentifier(excluded.verdict)),
             QStringLiteral("excluded"));
    QCOMPARE(QString::fromLatin1(LqCompare::Filter::maskVerdictIdentifier(notMatched.verdict)),
             QStringLiteral("not-matched"));
}

void TstFilter::decisionNamesTheDecidingRule()
{
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*.tmp\n-*_test.cpp"));

    const MaskDecision byExclude =
            filter.decide(MaskSubject::forName(QStringLiteral("a_test.cpp")));
    QCOMPARE(byExclude.ruleIndex, 2);
    QCOMPARE(byExclude.ruleText, QStringLiteral("*_test.cpp"));
    QVERIFY(byExclude.describe().contains(QStringLiteral("#3")));

    const MaskDecision byInclude =
            filter.decide(MaskSubject::forName(QStringLiteral("a.cpp")));
    QCOMPARE(byInclude.ruleIndex, 0);
    QVERIFY(byInclude.describe().contains(QStringLiteral("#1")));
}

void TstFilter::decisionExplainsDefaultInclusion()
{
    const MaskFilter filter = filterFor(QStringLiteral("-*.tmp"));
    const MaskDecision decision = filter.decide(MaskSubject::forName(QStringLiteral("a.txt")));
    QVERIFY(decision.describe().contains(QStringLiteral("默认")));
}

void TstFilter::matchingRuleIndexesReportsEveryHit()
{
    // decide() 只报「起决定作用的那一条」，而诊断面板要回答「是哪几条在管它」。
    // 一个条目可能同时命中两条排除规则，只报第一条会让用户改掉一条之后
    // 发现还是看不见。
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*x*\n-*_test*"));
    const QVector<int> hits =
            filter.matchingRuleIndexes(MaskSubject::forName(QStringLiteral("a_x_test.cpp")));

    QCOMPARE(hits.size(), 3);
    QCOMPARE(hits.at(0), 0);
    QCOMPARE(hits.at(1), 1);
    QCOMPARE(hits.at(2), 2);

    // 起决定作用的是第一条命中的排除规则。
    QCOMPARE(filter.decide(MaskSubject::forName(QStringLiteral("a_x_test.cpp"))).ruleIndex, 1);
    QCOMPARE(filter.matchingRuleIndexes(MaskSubject::forName(QStringLiteral("nothing")))
                     .size(),
             0);
}

void TstFilter::filterDescribeCountsRulesAndCase()
{
    const MaskFilter posix = filterFor(QStringLiteral("*.cpp\n-*.tmp"), MaskPlatform::Posix);
    const QString posixText = posix.describe();
    QVERIFY(posixText.contains(QStringLiteral("包含 1 条")));
    QVERIFY(posixText.contains(QStringLiteral("排除 1 条")));
    QVERIFY(posixText.contains(QStringLiteral("大小写敏感")));
    QVERIFY(posixText.contains(QStringLiteral("posix")));

    const MaskFilter windows = filterFor(QStringLiteral("*.cpp"), MaskPlatform::Windows);
    QVERIFY(windows.describe().contains(QStringLiteral("大小写不敏感")));
    QVERIFY(windows.describe().contains(QStringLiteral("windows")));

    QCOMPARE(filterFor(QString()).describe(), QStringLiteral("没有任何规则（全部保留）"));
}

// -----------------------------------------------------------------------------
// G 预览（标准第 4 条）
// -----------------------------------------------------------------------------

void TstFilter::summaryUsesTheWordingFromTheSpec()
{
    // 规格第 4 条点名要「匹配 N 项 / 共 M 项」这句文案。做成函数而不是让每个
    // 界面各拼一遍：一旦有两处拼，其中一处迟早会写成「共 M 项 / 匹配 N 项」，
    // 而截图对比时没人会注意顺序变了。
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp"));
    const MaskFilterPreview result = LqCompare::Filter::previewNames(
            filter, nameList(QStringLiteral("a.cpp"), QStringLiteral("b.cpp"),
                             QStringLiteral("c.h")));

    QCOMPARE(result.total, 3);
    QCOMPARE(result.included, 2);
    QCOMPARE(result.summary(), QStringLiteral("匹配 2 项 / 共 3 项"));
}

void TstFilter::previewCountsEachVerdict()
{
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*_test.cpp"));
    const MaskFilterPreview result = LqCompare::Filter::previewNames(
            filter, nameList(QStringLiteral("a.cpp"), QStringLiteral("a_test.cpp"),
                             QStringLiteral("readme.md")));

    QCOMPARE(result.total, 3);
    QCOMPARE(result.included, 1);
    QCOMPARE(result.excluded, 1);
    QCOMPARE(result.notMatched, 1);
    QCOMPARE(result.hidden(), 2);
    // 三类加起来必须等于总数：漏掉一类会让状态栏上的「可见 N / 共 M」对不上。
    QCOMPARE(result.included + result.excluded + result.notMatched, result.total);
}

void TstFilter::previewCountsHitsPerRule()
{
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp\n-*_test.cpp"));
    const MaskFilterPreview result = LqCompare::Filter::previewNames(
            filter, nameList(QStringLiteral("a.cpp"), QStringLiteral("b.cpp"),
                             QStringLiteral("a_test.cpp")));

    QCOMPARE(result.hitsByRule.size(), 2);
    QCOMPARE(result.hitsByRule.at(0), 2); // 两条 .cpp 由包含规则 #1 保留
    QCOMPARE(result.hitsByRule.at(1), 1); // 一条测试文件被排除规则 #2 挡掉
}

void TstFilter::previewOfEmptyFilterKeepsEverything()
{
    const MaskFilter filter = filterFor(QString());
    const MaskFilterPreview result = LqCompare::Filter::previewNames(
            filter, nameList(QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")));

    QCOMPARE(result.included, 3);
    QCOMPARE(result.excluded, 0);
    QCOMPARE(result.notMatched, 0);
    QCOMPARE(result.hitsByRule.size(), 0);
    QCOMPARE(result.summary(), QStringLiteral("匹配 3 项 / 共 3 项"));
}

void TstFilter::hiddenIsTotalMinusIncluded()
{
    // 「可见 N / 共 M」是状态栏常显的那一项（FILT-006），因此 M - N 必须是
    // 「被隐藏的条目数」，而不是「被排除的条目数」——把「未命中」漏掉的话，
    // 白名单掩码下状态栏会显示「可见 3 / 共 10」，而隐藏数算出来是 0。
    const MaskFilter filter = filterFor(QStringLiteral("*.cpp"));
    const MaskFilterPreview result = LqCompare::Filter::previewNames(
            filter, nameList(QStringLiteral("a.cpp"), QStringLiteral("b.h"),
                             QStringLiteral("c.md")));

    QCOMPARE(result.included, 1);
    QCOMPARE(result.excluded, 0);
    QCOMPARE(result.notMatched, 2);
    QCOMPARE(result.hidden(), 2);
    QCOMPARE(result.hidden(), result.total - result.included);

    // 全被挡住时也不能出现负数。
    const MaskFilter rejectAll = filterFor(QStringLiteral("*.nothingmatches"));
    const MaskFilterPreview empty = LqCompare::Filter::previewNames(
            rejectAll, nameList(QStringLiteral("a"), QStringLiteral("b"),
                                QStringLiteral("c")));
    QCOMPARE(empty.included, 0);
    QCOMPARE(empty.hidden(), 3);
}

void TstFilter::previewNamesIsEquivalentToSubjects()
{
    // 两个入口必须是同一件事的两种写法：`previewNames` 只是替调用方把每个名字
    // 包成 `MaskSubject::forName`。两者结果不等，说明有人往其中一条路径里
    // 多塞了逻辑（最常见的是「空名字」与「带目录的名字」在这里分叉）。
    const MaskFilter filter = filterFor(QStringLiteral("*.txt"));

    QVector<MaskSubject> subjects;
    subjects.append(MaskSubject::forName(QStringLiteral("a.txt")));
    subjects.append(MaskSubject::forName(QStringLiteral("a.cpp")));
    subjects.append(MaskSubject::forName(QStringLiteral("b.txt")));

    const MaskFilterPreview byNames = LqCompare::Filter::previewNames(
            filter, nameList(QStringLiteral("a.txt"), QStringLiteral("a.cpp"),
                             QStringLiteral("b.txt")));
    const MaskFilterPreview bySubjects = LqCompare::Filter::preview(filter, subjects);

    QCOMPARE(byNames.total, bySubjects.total);
    QCOMPARE(byNames.included, bySubjects.included);
    QCOMPARE(byNames.notMatched, bySubjects.notMatched);
    QCOMPARE(byNames.hitsByRule.size(), bySubjects.hitsByRule.size());
    QCOMPARE(byNames.hitsByRule.at(0), bySubjects.hitsByRule.at(0));

    // 空名字不是合法条目（MaskSubject::isValid() 为假），因此它落到「未命中」，
    // 也就是不可见——而不是「保留」。这个方向是刻意选的：调用方给了残缺的条目时，
    // 保守的一侧是把它挡在外面；放它通过过滤器，会让一个本该报错的输入
    // 变成「界面上少了一个条目」这种无声无息的结果。
    const MaskFilterPreview withEmpty = LqCompare::Filter::previewNames(
            filter, QStringList() << QStringLiteral("a.txt") << QString());
    QCOMPARE(withEmpty.total, 2);
    QCOMPARE(withEmpty.included, 1);
    QCOMPARE(withEmpty.notMatched, 1);
}

// -----------------------------------------------------------------------------
// H 语法速查（标准第 4 条）
// -----------------------------------------------------------------------------

void TstFilter::everyReferenceEntryCompilesAndSamplesHold()
{
    // 这一条是 FILT-001 与 FILT-011 之间那座桥：FILT-011 要求「文档中的示例掩码
    // 与测试语料中的用例一致（用同一个数据源生成）」。速查表就是那个数据源，
    // 而这里把表里的每一条样本**真的跑一遍**——文档说 `[!a]` 是取反，
    // 程序就必须真的是取反，否则这条用例红。
    const QVector<MaskSyntaxEntry> table = LqCompare::Filter::maskSyntaxReference();
    QVERIFY(!table.isEmpty());

    for (const MaskSyntaxEntry &entry : table) {
        if (entry.kind != MaskSyntaxEntry::Kind::Mask)
            continue;

        const MaskParseResult parsed = Mask::compile(entry.pattern);
        QVERIFY2(parsed.ok(),
                 qPrintable(QStringLiteral("速查表里的掩码 `%1` 解析失败：%2")
                                    .arg(entry.pattern, parsed.error.describe())));

        for (const MaskSyntaxSample &sample : entry.samples) {
            const bool actual = parsed.mask.matches(MaskSubject::forPath(sample.text),
                                                   Qt::CaseSensitive);
            QVERIFY2(actual == sample.matches,
                     qPrintable(QStringLiteral("速查表 `%1` 对 `%2` 的期望是 %3，实际是 %4")
                                        .arg(entry.pattern, sample.text,
                                             sample.matches ? QStringLiteral("匹配")
                                                            : QStringLiteral("不匹配"),
                                             actual ? QStringLiteral("匹配")
                                                    : QStringLiteral("不匹配"))));
        }
    }
}

void TstFilter::declarationEntriesAreExercisedThroughTheFilter()
{
    // 声明类条目（`-` 前缀、`#` 注释、多行叠加）不能被 Mask 单独测，要走整条
    // 过滤器。样本的「是否匹配」在这里的含义是「是否被接受（保留）」。
    const QVector<MaskSyntaxEntry> table = LqCompare::Filter::maskSyntaxReference();
    int checked = 0;

    for (const MaskSyntaxEntry &entry : table) {
        if (entry.kind != MaskSyntaxEntry::Kind::Declaration)
            continue;
        ++checked;

        const MaskFilterParseResult parsed =
                MaskFilter::parse(entry.pattern, MaskPlatform::Posix);
        QVERIFY2(parsed.ok(),
                 qPrintable(QStringLiteral("速查表里的声明 %1 解析失败：%2")
                                    .arg(entry.pattern, parsed.describeErrors())));

        for (const MaskSyntaxSample &sample : entry.samples) {
            const bool actual = parsed.filter.accepts(MaskSubject::forPath(sample.text));
            QVERIFY2(actual == sample.matches,
                     qPrintable(QStringLiteral("速查表声明 %1 对 `%2` 的期望是 %3，实际是 %4")
                                        .arg(entry.pattern, sample.text,
                                             sample.matches ? QStringLiteral("保留")
                                                            : QStringLiteral("不保留"),
                                             actual ? QStringLiteral("保留")
                                                    : QStringLiteral("不保留"))));
        }
    }

    QVERIFY2(checked >= 4, "速查表里的声明类条目太少，覆盖不到注释与排除优先");
}

void TstFilter::referenceContainsBothOutcomes()
{
    // 一条只会展示「匹配」的文档表格是没法说明规则的——用户看不到边界在哪。
    // 全表至少要同时出现两种结论。
    bool sawTrue = false;
    bool sawFalse = false;

    for (const MaskSyntaxEntry &entry : LqCompare::Filter::maskSyntaxReference()) {
        for (const MaskSyntaxSample &sample : entry.samples) {
            if (sample.matches)
                sawTrue = true;
            else
                sawFalse = true;
        }
    }

    QVERIFY(sawTrue);
    QVERIFY(sawFalse);
}

void TstFilter::referenceEntriesAreUniqueAndNonEmpty()
{
    const QVector<MaskSyntaxEntry> table = LqCompare::Filter::maskSyntaxReference();
    QSet<QString> seen;

    for (const MaskSyntaxEntry &entry : table) {
        QVERIFY(!entry.pattern.isEmpty());
        QVERIFY(!entry.meaning.isEmpty());
        QVERIFY2(!entry.samples.isEmpty(),
                 qPrintable(QStringLiteral("速查表 `%1` 没有任何样本，等于没写").arg(entry.pattern)));

        const QString key = QString::fromLatin1(entry.kindIdentifier()) + QLatin1Char('/')
                + entry.pattern;
        QVERIFY2(!seen.contains(key),
                 qPrintable(QStringLiteral("速查表里有重复条目：%1").arg(key)));
        seen.insert(key);

        QSet<QString> sampleTexts;
        for (const MaskSyntaxSample &sample : entry.samples) {
            QVERIFY2(!sample.text.isEmpty(),
                     qPrintable(QStringLiteral("速查表 `%1` 有空样本").arg(entry.pattern)));
            QVERIFY2(!sampleTexts.contains(sample.text),
                     qPrintable(QStringLiteral("速查表 `%1` 的样本 `%2` 重复了")
                                        .arg(entry.pattern, sample.text)));
            sampleTexts.insert(sample.text);
        }
    }
}

void TstFilter::referenceTextIsGeneratedFromTheSameData()
{
    // 纯文本速查（帮助页与文档用的那一份）必须是从数据生成的，而不是手抄的
    // 第二份。判据：每一个掩码、每一条样本都必须出现在文本里。
    const QString text = LqCompare::Filter::maskSyntaxReferenceText();
    QVERIFY(!text.isEmpty());

    for (const MaskSyntaxEntry &entry : LqCompare::Filter::maskSyntaxReference()) {
        QVERIFY2(text.contains(entry.pattern),
                 qPrintable(QStringLiteral("速查文本里缺少掩码 `%1`").arg(entry.pattern)));
        for (const MaskSyntaxSample &sample : entry.samples) {
            QVERIFY2(text.contains(sample.text),
                     qPrintable(QStringLiteral("速查文本里缺少样本 `%1`").arg(sample.text)));
        }
    }
}

void TstFilter::referenceTextExplainsTheDeclarationSyntax()
{
    const QString text = LqCompare::Filter::maskSyntaxReferenceText();
    QVERIFY(text.contains(QStringLiteral("掩码语法速查")));
    QVERIFY(text.contains(QStringLiteral("`-` 开头的行是排除")));
    QVERIFY(text.contains(QStringLiteral("`#` 开头的行是注释")));
    // 分隔符与平台无关这条必须写清楚：它是本模块最容易让人踩空的一个约定。
    QVERIFY(text.contains(QStringLiteral("与平台无关")));
}

void TstFilter::referenceIsPureAndRepeatable()
{
    // 速查表是函数而不是全局表——模块里没有任何全局状态，这是它可以被
    // 并行调用、也可以在任意平台上得到同一份结果的前提。
    const QVector<MaskSyntaxEntry> first = LqCompare::Filter::maskSyntaxReference();
    const QVector<MaskSyntaxEntry> second = LqCompare::Filter::maskSyntaxReference();
    QCOMPARE(first.size(), second.size());
    QCOMPARE(LqCompare::Filter::maskSyntaxReferenceText(),
             LqCompare::Filter::maskSyntaxReferenceText());
}

// -----------------------------------------------------------------------------
// I 恶意与畸形输入（标准第 5 条）
// -----------------------------------------------------------------------------

void TstFilter::pathologicalCrossSegmentPatternCompletesQuickly()
{
    // `**` 每步都有「吃零段」与「吃一段」两个选择，所以朴素递归实现下
    // `**/**/**/…` 对上有 N 段的路径时有 2^N 条路径。24 个 `**` 对 40 段
    // 就是 2^40 量级——这条用例会直接挂住，而不是失败。
    //
    // 实现用的是「可达掩码段」表（O(路径段数 × 掩码段数)），因此这里实际耗时
    // 是微秒级。时间上界刻意给得很宽：它要抓的是**指数级退化**，
    // 不是给性能定基线（性能基线是 FILT-009 的事）。
    QString pattern;
    for (int i = 0; i < 24; ++i)
        pattern += QStringLiteral("**/");
    pattern += QStringLiteral("x");

    QStringList segments;
    for (int i = 0; i < 40; ++i)
        segments << QStringLiteral("d");
    const QString path = segments.join(QLatin1Char('/'));

    const Mask mask = compiledFor(pattern);
    QVERIFY(mask.isValid());

    QElapsedTimer timer;
    timer.start();
    const bool matched = hitsPath(mask, path);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(!matched);
    QVERIFY2(elapsed < 2000,
             qPrintable(QStringLiteral("跨段匹配耗时 %1 ms，疑似退化成指数级").arg(elapsed)));
}

void TstFilter::pathologicalSegmentPatternCompletesQuickly()
{
    // 段内的经典陷阱：`*a*a*a*a*a*a*a*a*a*b` 对上一串 `a`。
    // 朴素递归在这里也是指数级的，而且模式本身看起来很普通。
    QString pattern;
    for (int i = 0; i < 12; ++i)
        pattern += QStringLiteral("*a");
    pattern += QStringLiteral("b");

    const QString text(200, QLatin1Char('a'));

    const Mask mask = compiledFor(pattern);
    QVERIFY(mask.isValid());

    QElapsedTimer timer;
    timer.start();
    const bool matched = hitsName(mask, text);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(!matched);
    QVERIFY2(elapsed < 2000,
             qPrintable(QStringLiteral("段内匹配耗时 %1 ms，疑似退化成指数级").arg(elapsed)));
}

void TstFilter::manySegmentsDoNotBlowUp()
{
    QStringList segments;
    for (int i = 0; i < 400; ++i)
        segments << QStringLiteral("d%1").arg(i);
    const QString path = segments.join(QLatin1Char('/'));

    const Mask mask = compiledFor(QStringLiteral("**/d399"));

    QElapsedTimer timer;
    timer.start();
    const bool matched = hitsPath(mask, path);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(matched);
    QVERIFY2(elapsed < 2000, qPrintable(QStringLiteral("400 段耗时 %1 ms").arg(elapsed)));

    // 同一段长路径上再问一次不存在的尾巴，依然要有界。
    QVERIFY(!hitsPath(mask, path + QStringLiteral("/nope")));
}

void TstFilter::veryLongLiteralMaskIsHandled()
{
    const QString longName(5000, QLatin1Char('a'));

    const Mask mask = compiledFor(longName);
    QVERIFY(mask.isValid());
    QVERIFY(hitsName(mask, longName));
    QVERIFY(!hitsName(mask, longName + QStringLiteral("b")));
    QVERIFY(!hitsName(mask, QString(4999, QLatin1Char('a'))));

    // 掩码比名字长很多时也不能越界。
    QVERIFY(!hitsName(mask, QStringLiteral("a")));
}

void TstFilter::veryLongSetIsHandled()
{
    // 一个几千成员的字符集。这里盯的是「按成员逐个比」的实现不会因为集合太大
    // 而崩掉或读越界——区间与单字符混在一起时最容易写错边界。
    QString members;
    for (int i = 0; i < 500; ++i)
        members.append(QChar(0x0400 + i));

    const Mask mask = compiledFor(QStringLiteral("[%1]").arg(members));
    QVERIFY(mask.isValid());
    QVERIFY(hitsName(mask, QString(QChar(0x0400))));
    QVERIFY(hitsName(mask, QString(QChar(0x0400 + 499))));
    QVERIFY(!hitsName(mask, QString(QChar(0x0400 + 500))));
    QVERIFY(!hitsName(mask, QStringLiteral("a")));
}

void TstFilter::compileIsPureAndRepeatable()
{
    const QString pattern = QStringLiteral("a/**/[!x]*.tar.gz");

    const MaskParseResult first = Mask::compile(pattern);
    const MaskParseResult second = Mask::compile(pattern);

    QVERIFY(first.ok());
    QVERIFY(second.ok());
    QCOMPARE(first.mask.normalizedPattern(), second.mask.normalizedPattern());
    QCOMPARE(first.mask.segmentCount(), second.mask.segmentCount());
    QCOMPARE(first.mask.atomCount(), second.mask.atomCount());
    QCOMPARE(first.mask.crossDirectoryCount(), second.mask.crossDirectoryCount());

    // 失败也要是可重复的：同一个错误输入两次必须给出同样的位置与原因，
    // 否则界面上标红的位置会闪。
    const MaskParseResult badFirst = Mask::compile(QStringLiteral("a[b"));
    const MaskParseResult badSecond = Mask::compile(QStringLiteral("a[b"));
    QVERIFY(!badFirst.ok());
    QVERIFY(!badSecond.ok());
    QCOMPARE(badFirst.error.column, badSecond.error.column);
    QCOMPARE(badFirst.error.message, badSecond.error.message);
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstFilter)
