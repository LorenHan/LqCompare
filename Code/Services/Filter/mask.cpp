#include "mask.h"

#include <QStringList>

namespace LqCompare {
namespace Filter {

// -----------------------------------------------------------------------------
// 为什么掩码里的分隔符恒为 `/`
//
// 掩码是**用户写下来的意图**，而不是本地路径。如果让掩码的分隔符跟着平台走，
// 同一份预设库（FILT-007 要求它能导出给团队共享）在 Windows 与 Unix 上就不是
// 同一套语法了：`build/out` 在一台机器上排除一整个子目录，在另一台上排除了一个
// 名字里带 `/` 的条目——而没有人会想到「预设」竟然是有平台色彩的。
//
// 所以这里定死：**掩码里 `/` 是分隔符、`\` 是转义字符**，与平台无关。
// 由此还带来一个好处：Windows 用户把 `build\out` 写进掩码时，我们能把它当成
// 明确的错误报出来并建议改写成 `/`，而不是静默解释成 `buildout`——后者是这里
// 最容易发生、也最难查的一类偏差：过滤看起来生效了，只是漏了一批文件。
//
// 那为什么不用 Files::PathUtils::split？因为那是**本地路径**的规则（分隔符随平台
// 变、要处理盘符与 UNC），而这里的输入是**掩码路径**，它只有一种形态。借用过来
// 会让「Windows 上掩码要不要认 `\`」这类问题跑到路径模块里去，而答案（不认）
// 是掩码语言的属性。下面那几行只在掩码语言变化时才需要改。
// -----------------------------------------------------------------------------

namespace {

QStringList splitMaskPath(const QString &path)
{
    QStringList parts;
    const int length = path.length();
    int start = 0;
    for (int i = 0; i <= length; ++i) {
        if (i < length && path.at(i) != QLatin1Char('/'))
            continue;
        // 空段（连续分隔符、首尾分隔符）在这里被自然丢弃。
        if (i > start)
            parts.append(path.mid(start, i - start));
        start = i + 1;
    }
    return parts;
}

/// 路径的最后一段。结尾的分隔符先去掉（`a/b/` 的「名字」是 `b`）。
QString lastSegment(const QString &path)
{
    QString trimmed = path;
    while (trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    const int index = trimmed.lastIndexOf(QLatin1Char('/'));
    return index < 0 ? trimmed : trimmed.mid(index + 1);
}

bool isMaskSpace(QChar c)
{
    return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

/// 大小写折叠：只对「有大小写概念」的字符做转换，其余原样返回。
///
/// 需要的是一个**双向**的转换（大写变小写、小写变大写），而不是 `toLower()`：
/// 字符集匹配要在「原字符」与「换过大小写的字符」两边各试一次（理由见
/// setContains），只往一个方向折会漏掉 `[A-Z]` 命中 `a` 这种情况。
QChar swapCase(QChar c)
{
    if (c.isUpper())
        return c.toLower();
    if (c.isLower())
        return c.toUpper();
    return c;
}

/// `\` 可以转义的本字。**这是唯一的白名单**：写下 `\x` 而 `x` 不在表里就是错误。
///
/// 刻意不做「不认识的转义就原样保留」这种宽容处理：`\` 在掩码里最常见的误用
/// 是把 Windows 路径分隔符写进来（`build\out`），而宽容处理会把它悄悄变成
/// `buildout`。宁可报错，也不要给出一个「看起来生效了」的过滤器。
bool isEscapable(QChar c)
{
    return c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('[')
            || c == QLatin1Char(']') || c == QLatin1Char('-') || c == QLatin1Char('#')
            || c == QLatin1Char('\\');
}

QString escapeHint()
{
    return QStringLiteral("`\\` 只能用来转义 `* ? [ ] - # \\` 这几个字符；"
                          "如果这是 Windows 路径分隔符，请改写成 `/`");
}

} // namespace

// -----------------------------------------------------------------------------
// 平台默认值
// -----------------------------------------------------------------------------

MaskPlatform currentMaskPlatform()
{
#ifdef Q_OS_WIN
    return MaskPlatform::Windows;
#else
    return MaskPlatform::Posix;
#endif
}

Qt::CaseSensitivity defaultCaseSensitivity(MaskPlatform platform)
{
    // Windows 的文件系统不区分大小写，`*.TXT` 与 `*.txt` 在那里指同一批文件，
    // 因此把默认值定成「敏感」会与用户手上的文件管理器行为不一致——用户会
    // 以为掩码失效了。反过来，Unix 上定成「不敏感」会让 `Makefile` 与
    // `makefile` 这两个**不同的文件**混为一谈，而这是更糟的一类错误：
    // 它把本该显示出来的条目隐藏了，用户没有任何办法察觉。
    return platform == MaskPlatform::Windows ? Qt::CaseInsensitive : Qt::CaseSensitive;
}

const char *maskPlatformIdentifier(MaskPlatform platform)
{
    return platform == MaskPlatform::Windows ? "windows" : "posix";
}

// -----------------------------------------------------------------------------
// MaskSubject
// -----------------------------------------------------------------------------

MaskSubject MaskSubject::forName(const QString &name)
{
    MaskSubject subject;
    subject.name = lastSegment(name);
    // 只知道名字时，路径就是名字本身。于是 `src/*.txt` 这样的路径掩码不会命中
    // 它——这是对的：调用方并没有给出目录信息，就不该假定它在 `src` 下面。
    subject.path = name;
    return subject;
}

MaskSubject MaskSubject::forPath(const QString &path)
{
    MaskSubject subject;
    subject.name = lastSegment(path);
    subject.path = path;
    return subject;
}

// -----------------------------------------------------------------------------
// 构造
// -----------------------------------------------------------------------------

Mask::Mask() = default;

// -----------------------------------------------------------------------------
// MaskParseError
// -----------------------------------------------------------------------------

QString MaskParseError::describe() const
{
    if (message.isEmpty())
        return QString();

    // 对外显示用 1 起列号（「第 1 列」），内部保持 0 起——这样界面把它直接喂给
    // 输入框的 setSelection 时不用再减一。
    QString text;
    if (column >= 0)
        text = QStringLiteral("第 %1 列：%2").arg(column + 1).arg(message);
    else
        text = message;

    if (!hint.isEmpty())
        text += QStringLiteral("（%1）").arg(hint);

    return text;
}

// -----------------------------------------------------------------------------
// 单字符匹配
// -----------------------------------------------------------------------------

bool Mask::setContains(const Atom &atom, QChar c, Qt::CaseSensitivity cs)
{
    for (const Atom::SetRange &range : atom.setRanges) {
        if (c >= range.first && c <= range.last)
            return true;
    }

    if (cs == Qt::CaseSensitive)
        return false;

    // 大小写不敏感时，拿「换过大小写的字符」再试一遍。
    //
    // 为什么不干脆把区间两端也折叠一下再比：`[A-_]` 折叠后是 `a`..`_`，
    // 而 `a`(U+0061) 比 `_`(U+005F) 大 —— 区间就**反了**，这个字符集从此
    // 匹配不到任何东西，而表面上一切正常。反转字符再比一遍既没有这个问题，
    // 也不用为区间维护两套边界（`[a-z]` 命中 `A`、`[A-Z]` 命中 `a` 都自然成立）。
    const QChar swapped = swapCase(c);
    if (swapped == c)
        return false;

    for (const Atom::SetRange &range : atom.setRanges) {
        if (swapped >= range.first && swapped <= range.last)
            return true;
    }
    return false;
}

bool Mask::atomMatchesChar(const Atom &atom, QChar c, Qt::CaseSensitivity cs)
{
    switch (atom.kind) {
    case Atom::Kind::Literal:
        return cs == Qt::CaseSensitive
                ? (c == atom.literal)
                : (c.toCaseFolded() == atom.literal.toCaseFolded());

    case Atom::Kind::AnyChar:
        // `?` 与 `*`、字符集一样都不吃分隔符。若允许 `?` 吃 `/`，`a?b` 就会同时
        // 命中 `a/b`——而「斜杠是不是分隔符」必须只有一个答案，否则
        // 「`*` 不跨目录」这条规则会跟着形同虚设。
        return c != QLatin1Char('/');

    case Atom::Kind::CharSet: {
        if (c == QLatin1Char('/'))
            return false;
        const bool inside = setContains(atom, c, cs);
        return atom.setNegated ? !inside : inside;
    }

    case Atom::Kind::AnyRun:
        break; // `*` 由 matchesSegment 的主循环处理，不在这里
    }
    return false;
}

bool Mask::matchesSegment(const QVector<Atom> &atoms, const QString &text, Qt::CaseSensitivity cs)
{
    // 经典的「单个 `*` 贪心 + 回溯点」算法，复杂度 O(文本长度 × 原子数) 上界。
    //
    // 为什么不用朴素递归：`*a*a*a*a*a*a*a*a*b` 这类模式在递归实现下是指数级的，
    // 而掩码是用户手写的输入，一个手抖多敲几个 `*` 就会让整个扫描卡住。
    // 这个写法的关键点是「回溯点只记最近一个 `*`」——由于 `*` 之间互相吞噬
    // 是等价的（`**` 与 `*` 同解），只需要退到最近那个就够，不需要真正的回溯栈。
    const int textLength = text.length();
    const int atomCount = atoms.size();

    int textIndex = 0;
    int atomIndex = 0;
    int lastStarAtom = -1;
    int lastStarText = 0;

    while (textIndex < textLength) {
        if (atomIndex < atomCount && atoms.at(atomIndex).kind != Atom::Kind::AnyRun
            && atomMatchesChar(atoms.at(atomIndex), text.at(textIndex), cs)) {
            ++textIndex;
            ++atomIndex;
            continue;
        }

        if (atomIndex < atomCount && atoms.at(atomIndex).kind == Atom::Kind::AnyRun) {
            // 先让 `*` 吃零个字符，再靠回溯慢慢加——反过来（先尽量多吃）
            // 需要知道「还剩多少要匹配」，在这个循环里表达不出来。
            lastStarAtom = atomIndex;
            lastStarText = textIndex;
            ++atomIndex;
            continue;
        }

        if (lastStarAtom >= 0) {
            atomIndex = lastStarAtom + 1;
            ++lastStarText;
            textIndex = lastStarText;
            continue;
        }

        return false;
    }

    // 文本走完了，掩码还剩尾巴：只有全是 `*` 才算匹配。
    while (atomIndex < atomCount && atoms.at(atomIndex).kind == Atom::Kind::AnyRun)
        ++atomIndex;

    return atomIndex == atomCount;
}

// -----------------------------------------------------------------------------
// 解析
// -----------------------------------------------------------------------------

bool Mask::compileSegment(const QString &segment, int columnOffset, Segment *out,
                          MaskParseError *error)
{
    out->atoms.clear();
    out->isCrossDirectory = false;

    // `**` 的跨目录含义**只在它独占一段时成立**。这是 gitignore、ant、
    // ripgrep 共同的规则，也是唯一能让「`a**b` 到底能不能跨目录」有确定答案的
    // 写法：段内的 `**` 退化成 `*`。若不这样规定，`a**b` 与 `a*b` 的行为就没有
    // 任何可预期的区别，用户只能靠试。
    if (segment == QLatin1String("**")) {
        out->isCrossDirectory = true;
        return true;
    }

    const int length = segment.length();
    int index = 0;

    while (index < length) {
        const QChar current = segment.at(index);

        if (current == QLatin1Char('\\')) {
            if (index + 1 >= length) {
                error->column = columnOffset + index;
                error->length = 1;
                error->message = QStringLiteral("反斜杠后面没有字符");
                error->hint = escapeHint();
                return false;
            }
            const QChar escaped = segment.at(index + 1);
            if (!isEscapable(escaped)) {
                error->column = columnOffset + index;
                error->length = 2;
                error->message = QStringLiteral("反斜杠不能转义 `%1`").arg(escaped);
                error->hint = escapeHint();
                return false;
            }
            Atom atom;
            atom.kind = Atom::Kind::Literal;
            atom.literal = escaped;
            out->atoms.append(atom);
            index += 2;
            continue;
        }

        if (current == QLatin1Char('*')) {
            // 连续的 `*` 合并成一个：两个相邻的 `*` 与一个的匹配能力完全相同，
            // 合并之后 atomCount() 才是稳定的（否则 `**` 与 `*` 的原子个数不同，
            // 结构断言会变得很难写，而且没人看得出为什么）。
            while (index < length && segment.at(index) == QLatin1Char('*'))
                ++index;

            Atom atom;
            atom.kind = Atom::Kind::AnyRun;
            out->atoms.append(atom);
            continue;
        }

        if (current == QLatin1Char('?')) {
            Atom atom;
            atom.kind = Atom::Kind::AnyChar;
            out->atoms.append(atom);
            ++index;
            continue;
        }

        if (current == QLatin1Char('[')) {
            const int open = index;
            int cursor = index + 1;

            bool negated = false;
            if (cursor < length
                && (segment.at(cursor) == QLatin1Char('!')
                    || segment.at(cursor) == QLatin1Char('^'))) {
                negated = true;
                ++cursor;
            }

            Atom atom;
            atom.kind = Atom::Kind::CharSet;
            atom.setNegated = negated;

            // `]` 写在最前面的位置表示它自己（POSIX 的规矩）。因此这里要区分
            // 「刚开括号」与「已经在集合里」——`[]]` 是「匹配 `]`」，
            // 而 `[]` 是「忘了写闭合括号」。
            bool atSetStart = true;
            bool closed = false;

            while (cursor < length) {
                const QChar member = segment.at(cursor);

                if (member == QLatin1Char(']') && !atSetStart) {
                    closed = true;
                    ++cursor;
                    break;
                }

                // 区间 `x-y`：要求 `-` 后面还有字符、且那个字符不是收尾的 `]`。
                // 于是 `[a-]` 与 `[-a]` 里的 `-` 都退化成它自己，与 POSIX 一致。
                if (cursor + 2 < length && segment.at(cursor + 1) == QLatin1Char('-')
                    && segment.at(cursor + 2) != QLatin1Char(']')) {
                    const QChar first = member;
                    const QChar last = segment.at(cursor + 2);
                    if (first > last) {
                        error->column = columnOffset + cursor;
                        error->length = 3;
                        error->message = QStringLiteral("字符集的区间起点 `%1` 比终点 `%2` 大")
                                                 .arg(first)
                                                 .arg(last);
                        error->hint = QStringLiteral("把区间写成从小到大，例如 `[a-z]`");
                        return false;
                    }
                    Atom::SetRange range;
                    range.first = first;
                    range.last = last;
                    atom.setRanges.append(range);
                    atSetStart = false;
                    cursor += 3;
                    continue;
                }

                Atom::SetRange range;
                range.first = member;
                range.last = member;
                atom.setRanges.append(range);
                atSetStart = false;
                ++cursor;
            }

            if (!closed) {
                // 刻意**不**把未闭合的 `[` 当成字面量。当字面量的话，
                // 用户把 `[abc` 敲漏一个 `]`，掩码会静静地变成「匹配 `[abc`
                // 这个字符串」——过滤看起来还在工作，只是永远什么都不命中。
                // 报错能让界面在输入框里就地标出来。
                error->column = columnOffset + open;
                error->length = 1;
                error->message = QStringLiteral("字符集没有闭合的 `]`");
                error->hint = QStringLiteral("补上 `]`，或把 `[` 写成 `\\[` 表示字面量");
                return false;
            }

            if (atom.setRanges.isEmpty()) {
                error->column = columnOffset + open;
                error->length = cursor - open;
                error->message = QStringLiteral("字符集里没有任何字符");
                error->hint = QStringLiteral("至少要写一个字符，例如 `[abc]`");
                return false;
            }

            out->atoms.append(atom);
            index = cursor;
            continue;
        }

        Atom atom;
        atom.kind = Atom::Kind::Literal;
        atom.literal = current;
        out->atoms.append(atom);
        ++index;
    }

    return true;
}

MaskParseResult Mask::compile(const QString &pattern)
{
    MaskParseResult result;

    // 首尾空白一律裁掉。掩码里不存在「有意义的首尾空格」——PLAT-007 已经把所有
    // 平台里带首尾空格的名字当成**问题名**（Windows 上这类名字根本建不出来、
    // 跨平台同步时也必然出错），所以裁掉不会误伤任何合法意图；而用户从表格里
    // 复制一列掩码时总会带上空白。
    int begin = 0;
    while (begin < pattern.length() && isMaskSpace(pattern.at(begin)))
        ++begin;
    int end = pattern.length();
    while (end > begin && isMaskSpace(pattern.at(end - 1)))
        --end;

    const QString trimmed = pattern.mid(begin, end - begin);

    result.mask.m_pattern = pattern; // 回显用原始文本，归一后的另存一份

    if (trimmed.isEmpty()) {
        result.error.column = 0;
        result.error.length = 0;
        result.error.message = QStringLiteral("掩码为空");
        result.error.hint = QStringLiteral("写一个掩码（如 `*.txt`），或用 `#` 开头的整行注释");
        return result;
    }

    // 首尾的 `/` 与重复的 `/` 都忽略：掩码路径是相对路径，`/a` 与 `a` 是同一件事。
    // 记下被去掉的前导斜杠数，后面报错时才能把列号换算回用户看到的文本。
    QString normalized = trimmed;
    while (normalized.endsWith(QLatin1Char('/')))
        normalized.chop(1);

    int leadingSlashes = 0;
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
        ++leadingSlashes;
    }

    if (normalized.isEmpty()) {
        result.error.column = begin;
        result.error.length = end - begin;
        result.error.message = QStringLiteral("掩码里只有路径分隔符");
        result.error.hint = QStringLiteral("至少要写一段名字，例如 `build/**`");
        return result;
    }

    const int columnBase = begin + leadingSlashes;

    QVector<Segment> segments;
    int crossDirectoryCount = 0;

    // 逐段切分，顺便记住每段在文本里的起点——报错列号要靠它。
    // （不用 QString::split 再 indexOf 反查：那在重复段名时会取到错误的位置。）
    int cursor = 0;
    while (cursor <= normalized.length()) {
        int next = normalized.indexOf(QLatin1Char('/'), cursor);
        if (next < 0)
            next = normalized.length();

        const QString text = normalized.mid(cursor, next - cursor);
        if (!text.isEmpty()) {
            Segment segment;
            MaskParseError error;
            if (!compileSegment(text, columnBase + cursor, &segment, &error)) {
                result.error = error;
                return result;
            }
            if (segment.isCrossDirectory)
                ++crossDirectoryCount;
            segments.append(segment);
        }

        if (next >= normalized.length())
            break;
        cursor = next + 1;
    }

    result.mask.m_normalized = normalized;
    result.mask.m_targetsPath = normalized.contains(QLatin1Char('/'));
    result.mask.m_segments = segments;
    result.mask.m_crossDirectoryCount = crossDirectoryCount;
    result.mask.m_valid = !segments.isEmpty();

    if (!result.mask.m_valid) {
        // 理论上不可达（normalized 非空且已去掉所有分隔符），留一道兜底，
        // 避免将来归一逻辑改动后这里悄悄产出一段「永远不匹配」的掩码。
        result.error.column = begin;
        result.error.length = end - begin;
        result.error.message = QStringLiteral("掩码里没有可用的段");
        return result;
    }

    return result;
}

// -----------------------------------------------------------------------------
// 匹配
// -----------------------------------------------------------------------------

bool Mask::matches(const MaskSubject &subject, Qt::CaseSensitivity cs) const
{
    if (!m_valid || !subject.isValid())
        return false;

    // 不含 `/` 的掩码按**名字**匹配，于是 `*.txt` 在任意目录下都命中——这是所有
    // 人对「文件掩码」的预期；含 `/` 的掩码才按相对路径匹配。
    QVector<QString> pathSegments;
    if (m_targetsPath) {
        const QStringList parts = splitMaskPath(subject.path);
        for (const QString &part : parts)
            pathSegments.append(part);
    } else {
        pathSegments.append(subject.name);
    }

    const int pathCount = pathSegments.size();
    const int maskCount = m_segments.size();

    // 段级匹配用一张「可达的掩码段」表做 NFA 模拟，而不是递归回溯。
    //
    // 为什么不能用递归：`**` 每一步都有「吃零段」与「吃一段」两个选择，于是
    // `**/**/**/…/x` 对上有 N 段的路径时有 2^N 条可能路径。一个手抖敲出来的
    // 掩码就足以让扫描停在那里不动——而「恶意与畸形输入」正是本条目第 5 条
    // 完成标准点名要求处理的。这张表把复杂度压到 O(路径段数 × 掩码段数)，
    // 并且没有递归深度问题。测试里有一条专门的样本盯着它。
    QVector<bool> reachable(maskCount + 1, false);
    reachable[0] = true;

    for (int i = 0; i <= pathCount; ++i) {
        // `**` 的 ε 闭包：它可以一段都不吃，所以「到达 `**` 就能立刻跳到下一段」。
        // 传播只朝后，因此一次顺序扫描就够（不需要反复迭代到不动点）。
        for (int j = 0; j < maskCount; ++j) {
            if (reachable.at(j) && m_segments.at(j).isCrossDirectory)
                reachable[j + 1] = true;
        }

        if (i == pathCount)
            break;

        QVector<bool> next(maskCount + 1, false);
        for (int j = 0; j < maskCount; ++j) {
            if (!reachable.at(j))
                continue;

            const Segment &segment = m_segments.at(j);
            if (segment.isCrossDirectory) {
                // 吃掉这一段，但仍停在这一段上——它还能继续吃。
                next[j] = true;
            } else if (matchesSegment(segment.atoms, pathSegments.at(i), cs)) {
                next[j + 1] = true;
            }
        }
        reachable = next;
    }

    return reachable.at(maskCount);
}

int Mask::atomCount() const
{
    int total = 0;
    for (const Segment &segment : m_segments)
        total += segment.atoms.size();
    return total;
}

QString Mask::describe() const
{
    if (!m_valid)
        return QStringLiteral("无效掩码");

    const QString scope = m_targetsPath ? QStringLiteral("按相对路径匹配")
                                        : QStringLiteral("按名字匹配");

    QString text = QStringLiteral("`%1`（%2，%3 段").arg(m_normalized, scope).arg(m_segments.size());
    if (m_crossDirectoryCount > 0)
        text += QStringLiteral("，其中 %1 段跨目录").arg(m_crossDirectoryCount);
    return text + QStringLiteral("）");
}

// -----------------------------------------------------------------------------
// 语法速查
// -----------------------------------------------------------------------------

const char *MaskSyntaxEntry::kindIdentifier() const
{
    return kind == Kind::Declaration ? "declaration" : "mask";
}

QVector<MaskSyntaxEntry> maskSyntaxReference()
{
    QVector<MaskSyntaxEntry> table;

    const auto addMask = [&table](const QString &pattern, const QString &meaning,
                                  const QVector<MaskSyntaxSample> &samples) {
        MaskSyntaxEntry entry;
        entry.kind = MaskSyntaxEntry::Kind::Mask;
        entry.pattern = pattern;
        entry.meaning = meaning;
        entry.samples = samples;
        table.append(entry);
    };

    const auto addDeclaration = [&table](const QString &pattern, const QString &meaning,
                                         const QVector<MaskSyntaxSample> &samples) {
        MaskSyntaxEntry entry;
        entry.kind = MaskSyntaxEntry::Kind::Declaration;
        entry.pattern = pattern;
        entry.meaning = meaning;
        entry.samples = samples;
        table.append(entry);
    };

    addMask(QStringLiteral("*"),
            QStringLiteral("任意个字符。不含 `/` 的掩码按**名字**匹配，因此在任意目录下都生效"),
            { MaskSyntaxSample(QStringLiteral("a.txt"), true),
              MaskSyntaxSample(QStringLiteral("README"), true),
              MaskSyntaxSample(QStringLiteral("a.tar.gz"), true) });

    addMask(QStringLiteral("?.txt"),
            QStringLiteral("`?` 恰好一个字符（不含 `/`）"),
            { MaskSyntaxSample(QStringLiteral("a.txt"), true),
              MaskSyntaxSample(QStringLiteral("9.txt"), true),
              MaskSyntaxSample(QStringLiteral("ab.txt"), false),
              MaskSyntaxSample(QStringLiteral("txt"), false) });

    addMask(QStringLiteral("*.tar.gz"),
            QStringLiteral("通配符可以组合使用，`*` 不跨 `/`（但按名字匹配时名字里本来就没有 `/`）"),
            { MaskSyntaxSample(QStringLiteral("src.tar.gz"), true),
              MaskSyntaxSample(QStringLiteral("src.gz"), false) });

    addMask(QStringLiteral("a/*"),
            QStringLiteral("含 `/` 时按**相对路径**匹配；`*` 只吃一段"),
            { MaskSyntaxSample(QStringLiteral("a/b"), true),
              MaskSyntaxSample(QStringLiteral("a/b/c"), false),
              MaskSyntaxSample(QStringLiteral("x/a/b"), false) });

    addMask(QStringLiteral("**"),
            QStringLiteral("零个或多个路径段，可以跨 `/`"),
            { MaskSyntaxSample(QStringLiteral("a"), true),
              MaskSyntaxSample(QStringLiteral("a/b/c"), true) });

    addMask(QStringLiteral("a/**/b"),
            QStringLiteral("夹在中间的 `**` 表示「任意层目录」，可以是零层"),
            { MaskSyntaxSample(QStringLiteral("a/b"), true),
              MaskSyntaxSample(QStringLiteral("a/x/b"), true),
              MaskSyntaxSample(QStringLiteral("a/x/y/b"), true),
              MaskSyntaxSample(QStringLiteral("a/b/c"), false) });

    addMask(QStringLiteral("build/**"),
            QStringLiteral("排除整个目录及其下所有内容（`build` 本身也命中）"),
            { MaskSyntaxSample(QStringLiteral("build"), true),
              MaskSyntaxSample(QStringLiteral("build/out/o.txt"), true),
              MaskSyntaxSample(QStringLiteral("buildx"), false) });

    addMask(QStringLiteral("[abc].txt"),
            QStringLiteral("字符集：命中集合里的任意一个字符"),
            { MaskSyntaxSample(QStringLiteral("a.txt"), true),
              MaskSyntaxSample(QStringLiteral("c.txt"), true),
              MaskSyntaxSample(QStringLiteral("d.txt"), false),
              MaskSyntaxSample(QStringLiteral("ab.txt"), false) });

    addMask(QStringLiteral("[a-z0-9].bin"),
            QStringLiteral("字符集支持区间，可以连写多个"),
            { MaskSyntaxSample(QStringLiteral("q.bin"), true),
              MaskSyntaxSample(QStringLiteral("7.bin"), true),
              MaskSyntaxSample(QStringLiteral("Q.bin"), false) });

    addMask(QStringLiteral("[!abc].txt"),
            QStringLiteral("`[!...]` 取反：命中**不在**集合里的字符"),
            { MaskSyntaxSample(QStringLiteral("d.txt"), true),
              MaskSyntaxSample(QStringLiteral("a.txt"), false) });

    addMask(QStringLiteral("[^abc].txt"),
            QStringLiteral("`[^...]` 是取反的另一种写法（与 `[!...]` 等价）"),
            { MaskSyntaxSample(QStringLiteral("d.txt"), true),
              MaskSyntaxSample(QStringLiteral("a.txt"), false) });

    addMask(QStringLiteral("src/*.txt"),
            QStringLiteral("`*` 与 `?`、字符集都**不吃** `/`；想跨目录只能用 `**`"),
            { MaskSyntaxSample(QStringLiteral("src/a.txt"), true),
              MaskSyntaxSample(QStringLiteral("src/a/b.txt"), false) });

    addMask(QStringLiteral("a**b"),
            QStringLiteral("`**` 只有**独占一段**时才跨目录；`a**b` 里的它等同于 `*`"),
            { MaskSyntaxSample(QStringLiteral("axxb"), true),
              MaskSyntaxSample(QStringLiteral("ab"), true),
              MaskSyntaxSample(QStringLiteral("a/x/b"), false) });

    addMask(QStringLiteral("\\*.txt"),
            QStringLiteral("用 `\\` 转义，表示字面的星号"),
            { MaskSyntaxSample(QStringLiteral("*.txt"), true),
              MaskSyntaxSample(QStringLiteral("a.txt"), false) });

    addMask(QStringLiteral("a/"),
            QStringLiteral("结尾的 `/` 被忽略：掩码路径是相对路径，`a/` 与 `a` 等价"),
            { MaskSyntaxSample(QStringLiteral("a"), true),
              MaskSyntaxSample(QStringLiteral("a/b"), false) });

    addDeclaration(QStringLiteral("# 整行注释"),
                   QStringLiteral("以 `#` 开头的行是注释；注释行不产生规则，因此什么都不过滤"),
                   { MaskSyntaxSample(QStringLiteral("anything.txt"), true),
                     MaskSyntaxSample(QStringLiteral("a/b"), true) });

    addDeclaration(QStringLiteral("*.cpp"),
                   QStringLiteral("只写包含掩码时，未命中的条目被隐藏"),
                   { MaskSyntaxSample(QStringLiteral("main.cpp"), true),
                     MaskSyntaxSample(QStringLiteral("main.h"), false) });

    addDeclaration(QStringLiteral("-*.tmp"),
                   QStringLiteral("以 `-` 开头是排除掩码；只写排除时，其余条目全部保留"),
                   { MaskSyntaxSample(QStringLiteral("a.txt"), true),
                     MaskSyntaxSample(QStringLiteral("a.tmp"), false) });

    addDeclaration(QStringLiteral("*.cpp\n-*_test.cpp"),
                   QStringLiteral("同一行只能有一条掩码；多条写成多行。**排除优先于包含**"),
                   { MaskSyntaxSample(QStringLiteral("main.cpp"), true),
                     MaskSyntaxSample(QStringLiteral("main_test.cpp"), false) });

    addDeclaration(QStringLiteral("-#hash.txt"),
                   QStringLiteral("`#` 只在一行的**第一个**非空白位置表示注释；`-` 之后它是普通字符"),
                   { MaskSyntaxSample(QStringLiteral("#hash.txt"), false),
                     MaskSyntaxSample(QStringLiteral("other.txt"), true) });

    addDeclaration(QStringLiteral("\\-.txt"),
                   QStringLiteral("用 `\\-` 转义开头的减号，表示它属于名字而不是排除标记"),
                   { MaskSyntaxSample(QStringLiteral("-.txt"), true),
                     MaskSyntaxSample(QStringLiteral("a.txt"), false) });

    return table;
}

QString maskSyntaxReferenceText()
{
    const QString rule = QString(60, QLatin1Char('-'));

    QString text = QStringLiteral("掩码语法速查\n%1\n").arg(rule);
    text += QStringLiteral("掩码里 `/` 是路径分隔符、`\\` 是转义字符，两者与平台无关。\n");
    text += QStringLiteral("把多行掩码写在一起就是过滤声明：`-` 开头的行是排除，`#` 开头的行是注释。\n");
    text += QStringLiteral("%1\n").arg(rule);

    const QVector<MaskSyntaxEntry> table = maskSyntaxReference();
    for (const MaskSyntaxEntry &entry : table) {
        // 这里刻意用「一段一块」而不是等宽表格：表格要靠空格把中英文对齐，而中日韩
        // 字符在多数等宽字体里占两格、在少数里占一格，任何按字符数补空格的写法都会
        // 在某个字体下整体错位。块状排版没有这个问题，斜体/粗体也不用管。
        text += QStringLiteral("%1\n  含义：%2\n").arg(entry.pattern, entry.meaning);

        QStringList parts;
        for (const MaskSyntaxSample &sample : entry.samples) {
            parts.append(QStringLiteral("`%1` %2").arg(
                    sample.text, sample.matches ? QStringLiteral("匹配")
                                                : QStringLiteral("不匹配")));
        }
        text += QStringLiteral("  示例：%1\n%2\n").arg(parts.join(QStringLiteral("、")), rule);
    }

    return text;
}

} // namespace Filter
} // namespace LqCompare
