#include "namefilter.h"

#include <QMutex>
#include <QSet>
#include <QMutexLocker>
#include <QSemaphore>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

///
/// 名称过滤器的实现（PRD: FILT-002）。设计上的取舍写在 namefilter.h 顶部，
/// 这里只记那些「不看代码就想不到」的实现细节。
///
/// ## 为什么判定的三个分支写在同一个 switch 里，而不是拆成三个函数
///
/// 三个模式共享同一套「先看是否被停用 → 再编译好的对象上匹配 → 超时/成功各自
/// 收尾」的骨架。拆开之后，「被停用的表达式仍然要返回 Undecided」这类规则
/// 要写三遍，漏一遍的现象是「停用之后某个模式反而开始过滤了」——
/// 只在正则模式的用例里能发现。
///
/// ## 为什么超时的收尾是「记 Undecided + 问题」而不是「记不匹配」
///
/// 与 attributefilter.h 顶部第 1 条取舍逐字同源：把「判不出来」当成「不符合」，
/// 用户在界面上只会看到一个少了一批文件的结果。详见 namefilter.h 顶部第 3 条。
///

namespace LqCompare {
namespace Filter {

namespace {

// -----------------------------------------------------------------------------
// 小工具
// -----------------------------------------------------------------------------

/// 模式与组合语义的「稳定键」表。两张表各自是唯一的事实来源：
/// 键一旦发布（进预设文件）就不能再改，因此它们**不**与中文标签共用字符串。
struct MatchModeRow
{
    NameMatchMode mode;
    const char *identifier;
    const char *key;
    QString label;
    QString semanticsNote;
    QString declarationPrefix; ///< 规范写法（空串 = 无前缀）
};

///
/// 模式表。
///
/// **必须**是函数内静态：`matchModeRow()` 会把表里某一行的地址交出去，
/// 而返回临时 `QVector` 的写法会让那个指针在函数返回的一瞬间变成悬垂指针——
/// 现象是 `nameMatchModeLabel()` 偶尔返回空串、严重时直接崩溃，
/// 而崩溃位置在离这张表很远的地方。表是常量数据，本来也只该构造一次。
///
const QVector<MatchModeRow> &matchModeTable()
{
    // 中文一律用 QStringLiteral 包起来（不用 const char*）：本仓已经有两条坑记录
    // 说明 const char* + QLatin1String 处理中文会永远不相等。这里的 label 是
    // QString 字段，直接写 QStringLiteral 最省事。
    static const QVector<MatchModeRow> table = QVector<MatchModeRow>{
        MatchModeRow{NameMatchMode::Exact, "exact", "exact",
                     QStringLiteral("精确名"), QStringLiteral("整名匹配：名字与该字符串全等"),
                     QStringLiteral("= ")},
        MatchModeRow{NameMatchMode::Wildcard, "wildcard", "wildcard",
                     QStringLiteral("通配符"),
                     QStringLiteral("整名匹配：掩码语法（`*` 任意字符、`?` 单字符、`[...]` 字符集）"),
                     QString()},
        MatchModeRow{NameMatchMode::Regex, "regex", "regex",
                     QStringLiteral("正则"),
                     QStringLiteral("整名匹配：等价于在两端各加 `^(?:…)$`，要搜子串请写 `.*x.*`"),
                     QStringLiteral("re:")},
    };
    return table;
}

const MatchModeRow *matchModeRow(NameMatchMode mode)
{
    for (const MatchModeRow &row : matchModeTable()) {
        if (row.mode == mode) {
            return &row;
        }
    }
    return nullptr;
}

struct CombineModeRow
{
    NameCombineMode mode;
    const char *identifier;
    const char *key;
    QString label;
    QString explanation;
};

/// 组合语义表。理由同 matchModeTable()：这里也会把行地址交出去。
const QVector<CombineModeRow> &combineModeTable()
{
    static const QVector<CombineModeRow> table = QVector<CombineModeRow>{
        CombineModeRow{NameCombineMode::AnyOf, "any-of", "any-of", QStringLiteral("包含任一"),
                       QStringLiteral("任意一条表达式命中就保留；一条都没命中则排除")},
        CombineModeRow{NameCombineMode::NoneOf, "none-of", "none-of", QStringLiteral("不包含任何"),
                       QStringLiteral("一条表达式都不命中才保留；命中任意一条即排除")},
        CombineModeRow{NameCombineMode::AllOf, "all-of", "all-of", QStringLiteral("全部满足"),
                       QStringLiteral("所有表达式都命中才保留；任意一条不命中即排除")},
    };
    return table;
}

const CombineModeRow *combineModeRow(NameCombineMode mode)
{
    for (const CombineModeRow &row : combineModeTable()) {
        if (row.mode == mode) {
            return &row;
        }
    }
    return nullptr;
}

struct IssueKindRow
{
    NameFilterIssueKind kind;
    const char *identifier;
    QString label;
};

/// 问题种类表。
const QVector<IssueKindRow> &issueKindTable()
{
    static const QVector<IssueKindRow> table = QVector<IssueKindRow>{
        IssueKindRow{NameFilterIssueKind::Syntax, "syntax", QStringLiteral("语法错")},
        IssueKindRow{NameFilterIssueKind::Risky, "risky", QStringLiteral("可能回溯爆炸")},
        IssueKindRow{NameFilterIssueKind::Timeout, "timeout", QStringLiteral("匹配超时")},
        IssueKindRow{NameFilterIssueKind::Disabled, "disabled", QStringLiteral("表达式已停用")},
    };
    return table;
}

/// 拼一条问题。位置信息一律相对**整行**（与 MaskRuleError 同形）。
NameFilterIssue makeIssue(NameFilterIssueKind kind, int line, int column, int length,
                          const QString &text, const QString &message, const QString &hint)
{
    NameFilterIssue issue;
    issue.kind = kind;
    issue.line = line;
    issue.column = column;
    issue.length = length;
    issue.text = text;
    issue.message = message;
    issue.hint = hint;
    return issue;
}

/// 去掉左边的空白（含全角空格？不 —— 全角空格是合法名字字符，不能动）。
QString ltrimAscii(const QString &text)
{
    int index = 0;
    while (index < text.size() && text.at(index).isSpace()) {
        ++index;
    }
    return text.mid(index);
}

QString trimBoth(const QString &text)
{
    return text.trimmed();
}

///
/// 整名匹配。
///
/// **不能**用 `QRegularExpression::anchoredPattern()` 去包一层再交给 Qt：
/// 那会把用户自己的 `^` / `$` / `(?m)` 一起塞进括号里，行为随之改变
/// （`(?m)` 出现在中间就失效了）。这里改成「跑一次普通匹配 + 检查它是否
/// 正好盖住整个名字」，用户写的锚点仍然按 PCRE2 的规则解释。
///
bool regexMatchesWholeName(const QRegularExpression &regex, const QString &name)
{
    const QRegularExpressionMatch match = regex.match(name);
    if (!match.hasMatch()) {
        return false;
    }
    return match.capturedStart() == 0 && match.capturedLength() == name.length();
}

/// 量词判定：这一段（从 `at` 起）是不是一个量词，返回它占用的字符数（0 表示不是）。
int quantifierLengthAt(const QString &pattern, int at)
{
    if (at >= pattern.size()) {
        return 0;
    }
    const QChar c = pattern.at(at);
    if (c == QLatin1Char('*') || c == QLatin1Char('+') || c == QLatin1Char('?')) {
        // `?` 紧跟另一个量词时是「懒惰/占有修饰」，不是独立量词。
        // 这一层不区分：对「组被量化 + 组内有量词」这个判定没有影响。
        return 1;
    }
    if (c != QLatin1Char('{')) {
        return 0;
    }
    // `{n}` / `{n,}` / `{n,m}` 才是量词；`{abc` 在 PCRE 里是字面 `{`。
    int index = at + 1;
    int digits = 0;
    while (index < pattern.size() && pattern.at(index).isDigit()) {
        ++index;
        ++digits;
    }
    if (digits == 0) {
        return 0;
    }
    if (index < pattern.size() && pattern.at(index) == QLatin1Char(',')) {
        ++index;
        while (index < pattern.size() && pattern.at(index).isDigit()) {
            ++index;
        }
    }
    if (index < pattern.size() && pattern.at(index) == QLatin1Char('}')) {
        return index - at + 1;
    }
    return 0;
}

///
/// \brief 这个量词能否吃掉**不定个数**的字符。
///
/// 为什么必须区分：`(a{2})+` 与 `(a+)+` 只差一个字符，但前者每一轮都恰好吃掉
/// 两个 `a`，回溯是线性的；后者每一轮吃几个都行，才是指数爆炸。
/// 不区分的实现会把 `(a{2})+`、`(a{3,3})+` 这种完全无害的写法也报成可疑——
/// 误报的真正代价不是「多一条提示」，而是用户**从此不再看这个提示**，
/// 于是它连本该拦下的那一类也拦不住了。
///
bool isVariableQuantifierAt(const QString &pattern, int at, int length)
{
    if (length <= 0) {
        return false;
    }
    const QChar c = pattern.at(at);
    if (c == QLatin1Char('*') || c == QLatin1Char('+') || c == QLatin1Char('?')) {
        return true;
    }
    // `{n}`：个数固定 → 不是可变；`{n,}` 与 `{n,m}`（m != n）→ 可变。
    const QString body = pattern.mid(at + 1, length - 2);
    const int comma = body.indexOf(QLatin1Char(','));
    if (comma < 0) {
        return false;
    }
    const QString upper = body.mid(comma + 1);
    if (upper.isEmpty()) {
        return true;
    }
    return upper != body.left(comma);
}

// -----------------------------------------------------------------------------
// 匹配任务与工作线程（ThreadNameMatchRunner 的内部件）
// -----------------------------------------------------------------------------

///
/// \brief 一次匹配任务。
///
/// 刻意做成独立的小结构体 + 信号量，而不是让工作线程反过来调运行器回调：
/// 超时之后运行器就**不再持有**这个任务了（它可能被丢弃的线程引用很久），
/// 于是任务自己必须能被独立回收，且**不得**持有任何指向运行器或过滤器的指针。
///
struct MatchJob
{
    std::function<bool()> task;
    QSemaphore done;
    std::atomic<bool> matched{false};
    std::atomic<bool> abandoned{false};
};

} // namespace

// -----------------------------------------------------------------------------
// 模式 / 组合语义
// -----------------------------------------------------------------------------

const char *nameMatchModeIdentifier(NameMatchMode mode)
{
    const MatchModeRow *row = matchModeRow(mode);
    return row ? row->identifier : "wildcard";
}

QString nameMatchModeLabel(NameMatchMode mode)
{
    const MatchModeRow *row = matchModeRow(mode);
    return row ? row->label : QString();
}

QString nameMatchModeKey(NameMatchMode mode)
{
    const MatchModeRow *row = matchModeRow(mode);
    return row ? QString::fromLatin1(row->key) : QString();
}

NameMatchMode nameMatchModeFromKey(const QString &key, bool *ok)
{
    for (const MatchModeRow &row : matchModeTable()) {
        if (key == QString::fromLatin1(row.key)) {
            if (ok) {
                *ok = true;
            }
            return row.mode;
        }
    }
    if (ok) {
        *ok = false;
    }
    return NameMatchMode::Exact;
}

QString nameMatchModeSemanticsNote(NameMatchMode mode)
{
    const MatchModeRow *row = matchModeRow(mode);
    return row ? row->semanticsNote : QString();
}

QVector<NameMatchMode> allNameMatchModes()
{
    QVector<NameMatchMode> modes;
    for (const MatchModeRow &row : matchModeTable()) {
        modes.append(row.mode);
    }
    return modes;
}

QVector<NameMatchModePrefix> nameMatchModePrefixTable()
{
    QVector<NameMatchModePrefix> table;
    for (const MatchModeRow &row : matchModeTable()) {
        NameMatchModePrefix entry;
        entry.mode = row.mode;
        entry.prefix = row.declarationPrefix;
        entry.meaning = row.label;
        table.append(entry);
    }
    return table;
}

QString nameMatchModePrefix(NameMatchMode mode)
{
    const MatchModeRow *row = matchModeRow(mode);
    return row ? row->declarationPrefix : QString();
}

QVector<QString> validateNameFilterTables(const QVector<NameMatchModePrefix> &modeTable,
                                          const QVector<NameCombineModeRow> &combineTable)
{
    QVector<QString> problems;

    // --- 模式表 ---
    const QVector<NameMatchMode> expectedModes = allNameMatchModes();
    QVector<NameMatchMode> seenModes;
    QSet<QString> prefixes;
    for (const NameMatchModePrefix &entry : modeTable) {
        if (seenModes.contains(entry.mode)) {
            problems.append(QStringLiteral("模式表里 `%1` 出现了不止一次")
                                .arg(QString::fromLatin1(nameMatchModeIdentifier(entry.mode))));
        }
        seenModes.append(entry.mode);
        if (entry.meaning.isEmpty()) {
            problems.append(QStringLiteral("模式 `%1` 没有说明")
                                .arg(QString::fromLatin1(nameMatchModeIdentifier(entry.mode))));
        }

        const QString token = entry.prefix.trimmed();
        if (token.isEmpty()) {
            // 空前缀是通配符的合法写法，但**只能有一个**：两个模式都靠空前缀，
            // 解析时后一个永远轮不到，而现象是「某种模式怎么都用不出来」。
            if (prefixes.contains(QString())) {
                problems.append(QStringLiteral("有两个模式都用了空前缀"));
            }
            prefixes.insert(QString());
            continue;
        }
        // 前缀会进声明文本。含空白（`= ` 末尾那一个除外，见下）或含大写字母
        // 都会让「用户手写的 `RE:`」莫名不生效，而那种失败是静默的：
        // 那一行会被当成通配符，于是名字里真的带 `RE:` 的条目才会被匹配。
        if (token.contains(QLatin1Char(' '))) {
            problems.append(QStringLiteral("`%1` 的前缀里有空白").arg(token));
        }
        if (token != token.toLower()) {
            problems.append(QStringLiteral("`%1` 的前缀含大写字母").arg(token));
        }
        if (!entry.prefix.isEmpty() && entry.prefix != token
            && entry.prefix != token + QLatin1Char(' ')) {
            problems.append(QStringLiteral("`%1` 的规范写法只能是它自己或后面跟一个空格")
                                .arg(token));
        }
        if (prefixes.contains(token)) {
            problems.append(QStringLiteral("前缀 `%1` 被两个模式共用").arg(token));
        }
        prefixes.insert(token);
    }
    for (NameMatchMode mode : expectedModes) {
        if (!seenModes.contains(mode)) {
            problems.append(QStringLiteral("模式 `%1` 不在表里（界面上选不到它）")
                                .arg(QString::fromLatin1(nameMatchModeIdentifier(mode))));
        }
    }

    // --- 组合语义表 ---
    // 标签与解释一律**从传进来的表**读，不从内部表读：自检要能检查「一份写坏的表」，
    // 而这一条正是它存在的意义（两种语义共用同一句解释时，界面照常显示，用户看到的
    // 却是另一条语义的说明，于是他按提示去改、越改越不对）。
    QSet<QString> labels;
    QSet<QString> explanations;
    QVector<NameCombineMode> seenCombines;
    for (const NameCombineModeRow &row : combineTable) {
        if (row.label.isEmpty() || row.explanation.isEmpty()) {
            problems.append(QStringLiteral("组合语义 `%1` 缺少标签或解释")
                                .arg(QString::fromLatin1(nameCombineModeIdentifier(row.mode))));
        }
        if (labels.contains(row.label)) {
            problems.append(QStringLiteral("组合语义标签 `%1` 重复").arg(row.label));
        }
        if (explanations.contains(row.explanation)) {
            problems.append(QStringLiteral("组合语义 `%1` 与另一种共用同一句解释").arg(row.label));
        }
        if (seenCombines.contains(row.mode)) {
            problems.append(QStringLiteral("组合语义表里 `%1` 出现了不止一次")
                                .arg(QString::fromLatin1(nameCombineModeIdentifier(row.mode))));
        }
        labels.insert(row.label);
        explanations.insert(row.explanation);
        seenCombines.append(row.mode);
    }
    for (NameCombineMode mode : allNameCombineModes()) {
        if (!seenCombines.contains(mode)) {
            problems.append(QStringLiteral("组合语义 `%1` 不在表里")
                                .arg(QString::fromLatin1(nameCombineModeIdentifier(mode))));
        }
    }

    return problems;
}

NameMatchModePrefixHit matchNameMatchModePrefix(const QString &line)
{
    NameMatchModePrefixHit hit;

    // 前缀从「跳过前导空白」之后开始认。前缀本身的空白（规范写法 `= ` 里那个
    // 空格）不参与识别——它是排版，不是语法。
    int start = 0;
    while (start < line.size() && line.at(start).isSpace()) {
        ++start;
    }
    if (start >= line.size()) {
        return hit;
    }

    // 先试长前缀（`re:` 三个字符），再试单字符前缀。这里按「最长者胜」挑，
    // 而不是按表里的先后顺序——将来再加一个以 `r` 开头的前缀时，
    // 不必回头调表里的行序（那种「顺序即语义」的隐含约定最容易在新增时被破坏）。
    QString bestToken;
    NameMatchMode bestMode = NameMatchMode::Wildcard;
    for (const MatchModeRow &row : matchModeTable()) {
        const QString token = row.declarationPrefix.trimmed();
        if (token.isEmpty()) {
            continue;
        }
        if (line.mid(start, token.size()) != token) {
            continue;
        }
        if (token.size() > bestToken.size()) {
            bestToken = token;
            bestMode = row.mode;
        }
    }
    if (bestToken.isEmpty()) {
        return hit;
    }
    hit.found = true;
    hit.mode = bestMode;
    hit.prefixLength = start + bestToken.size();
    return hit;
}

QVector<NameCombineModeRow> nameCombineModeTable()
{
    QVector<NameCombineModeRow> table;
    for (const CombineModeRow &row : combineModeTable()) {
        NameCombineModeRow entry;
        entry.mode = row.mode;
        entry.label = row.label;
        entry.explanation = row.explanation;
        table.append(entry);
    }
    return table;
}

const char *nameCombineModeIdentifier(NameCombineMode mode)
{
    const CombineModeRow *row = combineModeRow(mode);
    return row ? row->identifier : "any-of";
}

QString nameCombineModeLabel(NameCombineMode mode)
{
    const CombineModeRow *row = combineModeRow(mode);
    return row ? row->label : QString();
}

QString nameCombineModeKey(NameCombineMode mode)
{
    const CombineModeRow *row = combineModeRow(mode);
    return row ? QString::fromLatin1(row->key) : QString();
}

NameCombineMode nameCombineModeFromKey(const QString &key, bool *ok)
{
    for (const CombineModeRow &row : combineModeTable()) {
        const QString rowKey = QString::fromLatin1(row.key);
        // 中文标签也认：预设文件是给人手改的，写「包含任一」比写 `any-of` 自然。
        if (key == rowKey || key == row.label) {
            if (ok) {
                *ok = true;
            }
            return row.mode;
        }
    }
    if (ok) {
        *ok = false;
    }
    return NameCombineMode::AnyOf;
}

QString nameCombineModeExplanation(NameCombineMode mode)
{
    const CombineModeRow *row = combineModeRow(mode);
    return row ? row->explanation : QString();
}

QVector<NameCombineMode> allNameCombineModes()
{
    QVector<NameCombineMode> modes;
    for (const CombineModeRow &row : combineModeTable()) {
        modes.append(row.mode);
    }
    return modes;
}

const char *nameFilterIssueKindIdentifier(NameFilterIssueKind kind)
{
    for (const IssueKindRow &row : issueKindTable()) {
        if (row.kind == kind) {
            return row.identifier;
        }
    }
    return "syntax";
}

QString nameFilterIssueKindLabel(NameFilterIssueKind kind)
{
    for (const IssueKindRow &row : issueKindTable()) {
        if (row.kind == kind) {
            return row.label;
        }
    }
    return QString();
}

const char *nameMatchOutcomeIdentifier(NameMatchOutcome outcome)
{
    switch (outcome) {
    case NameMatchOutcome::NotMatched:
        return "not-matched";
    case NameMatchOutcome::Matched:
        return "matched";
    case NameMatchOutcome::Undecided:
        return "undecided";
    }
    return "not-matched";
}

QString nameMatchOutcomeLabel(NameMatchOutcome outcome)
{
    switch (outcome) {
    case NameMatchOutcome::NotMatched:
        return QStringLiteral("不匹配");
    case NameMatchOutcome::Matched:
        return QStringLiteral("命中");
    case NameMatchOutcome::Undecided:
        return QStringLiteral("不确定");
    }
    return QString();
}

// -----------------------------------------------------------------------------
// 静态风险预检
// -----------------------------------------------------------------------------

QString RegexRisk::describe() const
{
    return QStringLiteral("第 %1 列：`%2` —— %3").arg(column + 1).arg(snippet, reason);
}

QVector<RegexRisk> analyzeRegexPatternRisk(const QString &pattern)
{
    QVector<RegexRisk> risks;

    // 组的状态：只记「这一层的组体里出现过**可变**量词吗」。
    // 为什么要**传递**到父层：`((a+))+` 里外层组体本身没有直接量词，
    // 但它包含了一个被量词修饰的内层组——外层再被量化，爆炸照样发生。
    struct GroupState
    {
        int start = 0;
        bool containsVariableQuantifier = false;
    };
    QVector<GroupState> stack;

    int index = 0;
    const int size = pattern.size();
    while (index < size) {
        const QChar c = pattern.at(index);

        if (c == QLatin1Char('\\')) {
            index += 2; // 转义：下一个字符是字面量，跳过
            continue;
        }

        if (c == QLatin1Char('[')) {
            // 字符集整体跳过：里面的 `\`、`-`、`^` 都不是量词。
            // 首个 `]` 是成员而不是结束符（`[]a]` 这种写法），因此从第 2 个位置起找。
            int scan = index + 1;
            if (scan < size && pattern.at(scan) == QLatin1Char('^')) {
                ++scan;
            }
            if (scan < size && pattern.at(scan) == QLatin1Char(']')) {
                ++scan;
            }
            while (scan < size && pattern.at(scan) != QLatin1Char(']')) {
                if (pattern.at(scan) == QLatin1Char('\\')) {
                    ++scan;
                }
                ++scan;
            }
            index = (scan < size) ? scan + 1 : size;
            continue;
        }

        if (c == QLatin1Char('(')) {
            GroupState state;
            state.start = index;
            stack.append(state);
            ++index;

            // `(?...)` 是「分组修饰」而不是「量词」。不跳过它会把 `(?:a)+`
            // 判成「组内有个 `?` 量词、组又被量化」——一个纯粹的误报，
            // 而误报的代价是用户学会忽略这个提示（见 header 里「宁可漏报」）。
            if (index < size && pattern.at(index) == QLatin1Char('?')) {
                ++index;
                if (index < size) {
                    const QChar marker = pattern.at(index);
                    if (marker == QLatin1Char('#')) {
                        // (?#...) 注释：一直读到最近的 `)`，交给主循环去闭合。
                        while (index < size && pattern.at(index) != QLatin1Char(')')) {
                            ++index;
                        }
                    } else if (marker == QLatin1Char(':') || marker == QLatin1Char('=')
                               || marker == QLatin1Char('!') || marker == QLatin1Char('>')) {
                        ++index;
                    } else if (marker == QLatin1Char('P') && index + 1 < size
                               && pattern.at(index + 1) == QLatin1Char('<')) {
                        index += 2; // (?P<name>
                        while (index < size && pattern.at(index) != QLatin1Char('>')) {
                            ++index;
                        }
                        if (index < size) {
                            ++index;
                        }
                    } else if (marker == QLatin1Char('<')) {
                        ++index;
                        if (index < size
                            && (pattern.at(index) == QLatin1Char('=')
                                || pattern.at(index) == QLatin1Char('!'))) {
                            ++index; // (?<= / (?<!
                        } else {
                            while (index < size && pattern.at(index) != QLatin1Char('>')) {
                                ++index;
                            }
                            if (index < size) {
                                ++index; // (?<name>
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (c == QLatin1Char(')')) {
            if (stack.isEmpty()) {
                ++index; // 不配对的 `)`：交给 PCRE 去报语法错
                continue;
            }
            const GroupState closed = stack.takeLast();

            const int outerLength = quantifierLengthAt(pattern, index + 1);
            const bool outerIsVariable =
                isVariableQuantifierAt(pattern, index + 1, outerLength);

            if (!stack.isEmpty()) {
                // 回传给父层两件事：这个组体内部有没有可变量词，
                // 以及**这个组自己被可变量词修饰了**（后者让
                // `((a){2,})+` 这种「内层是固定次数、外层才是可变」的写法也被认出）。
                stack.last().containsVariableQuantifier =
                    stack.last().containsVariableQuantifier
                    || closed.containsVariableQuantifier || outerIsVariable;
            }

            if (outerIsVariable && closed.containsVariableQuantifier) {
                RegexRisk risk;
                risk.column = closed.start;
                risk.length = index + 1 + outerLength - closed.start;
                risk.snippet = pattern.mid(closed.start, risk.length);
                risk.reason = QStringLiteral(
                    "组内还有可变量词、组本身又被可变量词修饰，输入较长时匹配步数会指数增长"
                    "（如 `(a+)+` 对上 `aaaa…b`）");
                risks.append(risk);
            }
            index += 1 + outerLength;
            continue;
        }

        const int quantifier = quantifierLengthAt(pattern, index);
        if (quantifier > 0) {
            if (!stack.isEmpty()) {
                if (isVariableQuantifierAt(pattern, index, quantifier)) {
                    stack.last().containsVariableQuantifier = true;
                }
            }
            index += quantifier;
            continue;
        }

        ++index;
    }

    return risks;
}

// -----------------------------------------------------------------------------
// 问题与结论的文本
// -----------------------------------------------------------------------------

QString NameFilterIssue::describe() const
{
    QString position;
    if (line >= 0) {
        position = QStringLiteral("第 %1 行").arg(line + 1);
        if (column >= 0) {
            position += QStringLiteral("第 %1 列").arg(column + 1);
        }
        position += QStringLiteral("：");
    }
    QString text = position + nameFilterIssueKindLabel(kind) + QStringLiteral("：") + message;
    if (!hint.isEmpty()) {
        text += QStringLiteral("（%1）").arg(hint);
    }
    return text;
}

int NameFilterProblems::countOfKind(NameFilterIssueKind kind) const
{
    int count = 0;
    for (const NameFilterIssue &issue : issues) {
        if (issue.kind == kind) {
            ++count;
        }
    }
    return count;
}

QString NameFilterProblems::describe() const
{
    if (issues.isEmpty()) {
        return QStringLiteral("没有问题");
    }
    QStringList lines;
    for (const NameFilterIssue &issue : issues) {
        lines.append(issue.describe());
    }
    return lines.join(QLatin1Char('\n'));
}

bool NameFilterLineAnalysis::hasSyntaxError() const
{
    for (const NameFilterIssue &issue : issues) {
        if (issue.kind == NameFilterIssueKind::Syntax) {
            return true;
        }
    }
    return false;
}

QString NameFilterExpression::describe() const
{
    return QStringLiteral("第 %1 行：%2 `%3`").arg(line + 1).arg(nameMatchModeLabel(mode), text);
}

QString NameFilterDecision::describe() const
{
    QString text = QStringLiteral("%1：%2").arg(nameCombineModeLabel(combine),
                                               accepted ? QStringLiteral("保留")
                                                        : QStringLiteral("排除"));
    if (decisiveIndex >= 0) {
        text += QStringLiteral("（由第 %1 条决定）").arg(decisiveIndex + 1);
    } else if (matchedIndexes.isEmpty() && undecidedIndexes.isEmpty()) {
        text += QStringLiteral("（没有表达式参与）");
    }
    if (!matchedIndexes.isEmpty()) {
        QStringList hits;
        for (int index : matchedIndexes) {
            hits.append(QString::number(index + 1));
        }
        text += QStringLiteral("；命中第 %1 条").arg(hits.join(QStringLiteral("、")));
    }
    if (!undecidedIndexes.isEmpty()) {
        text += QStringLiteral("；%1 条不确定（放行但已报出）").arg(undecidedIndexes.size());
    }
    return text;
}

QString NameFilterParseResult::describeErrors() const
{
    NameFilterProblems problems;
    problems.issues = issues;
    return problems.describe();
}

// -----------------------------------------------------------------------------
// 单行分析（解析、实时校验、复验共用的唯一实现）
// -----------------------------------------------------------------------------

NameFilterLineAnalysis analyzeNameFilterLine(const QString &line, int lineNumber, MaskPlatform platform)
{
    Q_UNUSED(platform); // 大小写只在判定时用得到；语法与平台无关（掩码语法也是平台无关的）

    NameFilterLineAnalysis analysis;

    const QString withoutLeft = ltrimAscii(line);
    if (withoutLeft.isEmpty() || withoutLeft.startsWith(QLatin1Char('#'))) {
        return analysis; // 空行与注释：不是表达式，也不是错误
    }

    const NameMatchModePrefixHit hit = matchNameMatchModePrefix(line);
    const NameMatchMode mode = hit.found ? hit.mode : NameMatchMode::Wildcard;

    // 列号要指向**表达式文本的第一个字符**，而不是前缀的末尾。`= README.md`
    // 的前缀是 `= `，但把列号报在空格上会让界面标红一个空白（用户看不出
    // 自己哪里错了）。因此前缀之后还要跨过空白。
    int textColumn = hit.found ? hit.prefixLength : (line.size() - withoutLeft.size());
    while (textColumn < line.size() && line.at(textColumn).isSpace()) {
        ++textColumn;
    }
    const QString text = trimBoth(line.mid(textColumn));

    NameFilterExpression expression;
    expression.mode = mode;
    expression.text = text;
    expression.line = lineNumber;
    expression.column = textColumn;

    if (text.isEmpty()) {
        analysis.issues.append(makeIssue(NameFilterIssueKind::Syntax, lineNumber, textColumn,
                                         qMax(1, line.size() - textColumn),
                                         line.mid(textColumn),
                                         QStringLiteral("这一行只有模式前缀、没有表达式"),
                                         QStringLiteral("在 `%1` 后面写上要匹配的名字")
                                             .arg(nameMatchModePrefix(mode).trimmed())));
        return analysis;
    }

    switch (mode) {
    case NameMatchMode::Exact:
        // 精确名是字面量，没有「语法」可错——任何字符都是合法的名字。
        break;

    case NameMatchMode::Wildcard: {
        const MaskParseResult compiled = Mask::compile(text);
        if (!compiled.ok()) {
            const MaskParseError &error = compiled.error;
            analysis.issues.append(
                makeIssue(NameFilterIssueKind::Syntax, lineNumber,
                          textColumn + qMax(0, error.column), qMax(1, error.length), text,
                          error.message, error.hint));
            return analysis;
        }
        break;
    }

    case NameMatchMode::Regex: {
        // 编译时**不带**大小写选项：大小写策略是判定期的输入（可以被界面改），
        // 把它编进预编译对象会让「改一下大小写开关」要求重新解析整份声明。
        // 预编译只是为了校验语法与省下每次编译的开销，大小写另在判定时应用。
        QRegularExpression probe(text);
        if (!probe.isValid()) {
            const int offset = probe.patternErrorOffset();
            analysis.issues.append(
                makeIssue(NameFilterIssueKind::Syntax, lineNumber,
                          textColumn + qMax(0, offset), 1, text,
                          QStringLiteral("正则表达式语法错误"),
                          QStringLiteral("PCRE2：%1（第 %2 个字符处）")
                              .arg(probe.errorString())
                              .arg(offset + 1)));
            return analysis;
        }
        break;
    }
    }

    // 静态风险预检：只提示、不阻断（这些表达式**仍然生效**）。
    // 与「语法错」的区别是有意的：`(a+)+` 对着短名字可能跑得很快，
    // 用户有权保留它；但界面必须给他一个「这里可能会卡」的提示。
    if (mode == NameMatchMode::Regex) {
        for (const RegexRisk &risk : analyzeRegexPatternRisk(text)) {
            analysis.issues.append(
                makeIssue(NameFilterIssueKind::Risky, lineNumber, textColumn + risk.column,
                          risk.length, risk.snippet,
                          QStringLiteral("这条正则可能灾难性回溯"), risk.reason));
        }
    }

    analysis.hasExpression = true;
    analysis.expression = expression;
    return analysis;
}

// -----------------------------------------------------------------------------
// 运行器
// -----------------------------------------------------------------------------

NameMatchRunner::~NameMatchRunner() = default;

class ThreadNameMatchRunner::Worker : public QThread
{
public:
    Worker() = default;

    ~Worker() override
    {
        // 析构时线程必须已经结束，否则 QThread 会在运行中被销毁。
        // 运行器只在**没有放弃任务**时才持有 Worker，因此这里的 wait() 不会
        // 无限等下去；被放弃的那些 Worker 不归运行器管（见 run() 里的注释）。
        requestQuit();
        wait();
    }

    void setJob(const std::shared_ptr<MatchJob> &job)
    {
        QMutexLocker locker(&m_mutex);
        m_job = job;
        m_cond.wakeOne();
    }

    void requestQuit()
    {
        QMutexLocker locker(&m_mutex);
        m_quit = true;
        m_cond.wakeOne();
    }

protected:
    void run() override
    {
        for (;;) {
            std::shared_ptr<MatchJob> job;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_job && !m_quit) {
                    m_cond.wait(&m_mutex);
                }
                if (m_quit && !m_job) {
                    return;
                }
                job = m_job;
                m_job.reset();
            }

            if (!job) {
                return;
            }

            // 这一段可能**永远**不返回（灾难性回溯）。因此它里面不能碰任何
            // 运行器或过滤器的状态——任务与它引用的对象都是按值持有的副本。
            job->matched.store(job->task());
            job->task = nullptr;
            job->done.release(1);

            if (job->abandoned.load()) {
                return; // 已经被放弃了：干完这一票就退出，不再接新任务
            }
        }
    }

private:
    QMutex m_mutex;
    QWaitCondition m_cond;
    std::shared_ptr<MatchJob> m_job;
    bool m_quit = false;
};

ThreadNameMatchRunner::ThreadNameMatchRunner() = default;

ThreadNameMatchRunner::~ThreadNameMatchRunner()
{
    delete m_worker;
    m_worker = nullptr;
}

NameMatchRunner::Outcome ThreadNameMatchRunner::run(const std::function<bool()> &task, int budgetMs)
{
    Outcome outcome;
    if (budgetMs <= 0 || !task) {
        // 不设预算：直接在当前线程上跑。这条路是给命令行/批处理留的
        // （没有界面可以被冻住），也是用例里验证「不设保护时行为如何」的入口。
        outcome.status = Status::Completed;
        outcome.matched = task ? task() : true;
        return outcome;
    }

    if (!m_worker) {
        m_worker = new Worker;
        m_worker->start();
    }

    auto job = std::make_shared<MatchJob>();
    job->task = task;
    m_worker->setJob(job);

    if (job->done.tryAcquire(1, budgetMs)) {
        outcome.status = Status::Completed;
        outcome.matched = job->matched.load();
        return outcome;
    }

    // 超时：放弃这一条并丢掉整个工作线程。
    //
    // 为什么丢弃而不是继续用：那个线程正卡在 `job->task()` 里，池容量为 1，
    // 再给它投任务只会全部排队、于是**后面每一个条目都报超时**。
    // 丢掉之后新建一个，匹配能力立刻恢复；被放弃的线程跑到自己结束就退出。
    // 它的个数上界由调用方的断路器给出（`NameMatchBudget::consecutiveTimeoutLimit`）。
    job->abandoned.store(true);
    Worker *stuck = m_worker;
    m_worker = nullptr;
    ++m_abandoned;

    // 跑完之后自删。连到 Worker 自己而不是运行器：运行器可能先一步被销毁，
    // 那时这个连接会被自动断开（代价是对象滞留，但线程本身会正常退出）。
    // 注意**不要**在这里 wait()：那正是我们想避免的等待。
    QObject::connect(stuck, &QThread::finished, stuck, &QObject::deleteLater);

    outcome.status = Status::TimedOut;
    outcome.matched = false;
    return outcome;
}

int ThreadNameMatchRunner::abandonedCount() const
{
    return m_abandoned;
}

std::shared_ptr<NameMatchRunner> defaultNameMatchRunner()
{
    // 进程内共享一个：每个过滤器实例一个线程池会变成几百个线程。
    // 函数内静态局部量在 C++11 之后是线程安全的一次性初始化。
    static std::shared_ptr<NameMatchRunner> runner = std::make_shared<ThreadNameMatchRunner>();
    return runner;
}

// -----------------------------------------------------------------------------
// NameFilter
// -----------------------------------------------------------------------------

NameFilter::NameFilter() = default;

NameFilterParseResult NameFilter::parse(const QString &declaration, MaskPlatform platform)
{
    NameFilterParseResult result;
    result.filter.m_platform = platform;
    result.filter.m_case = defaultCaseSensitivity(platform);

    const QStringList lines = splitDeclarationLines(declaration);
    for (int index = 0; index < lines.size(); ++index) {
        const NameFilterLineAnalysis analysis =
            analyzeNameFilterLine(lines.at(index), index, platform);
        for (const NameFilterIssue &issue : analysis.issues) {
            result.issues.append(issue);
        }
        if (!analysis.hasExpression) {
            continue;
        }

        const NameFilterExpression &expression = analysis.expression;
        result.filter.m_expressions.append(expression);
        switch (expression.mode) {
        case NameMatchMode::Exact:
            // 精确名没有预编译对象，塞两个空占位保持三个数组同序。
            result.filter.m_masks.append(Mask());
            result.filter.m_regexes.append(QRegularExpression());
            break;
        case NameMatchMode::Wildcard:
            result.filter.m_masks.append(Mask::compile(expression.text).mask);
            result.filter.m_regexes.append(QRegularExpression());
            break;
        case NameMatchMode::Regex:
            result.filter.m_masks.append(Mask());
            result.filter.m_regexes.append(QRegularExpression(expression.text));
            break;
        }
    }

    result.filter.m_disabled.fill(false, result.filter.m_expressions.size());
    result.filter.m_consecutiveTimeouts.fill(0, result.filter.m_expressions.size());
    result.filter.m_runner = defaultNameMatchRunner();
    return result;
}

void NameFilter::setCombineMode(NameCombineMode mode)
{
    m_combine = mode;
}

QString NameFilter::combineSummary() const
{
    // 只走两个分支（空 / 非空），不再为「恰好一条」单独措辞：一条表达式时
    // 「这 1 条表达式命中就保留」对「包含任一」成立、对「不包含任何」**恰好相反**，
    // 而这类文案写错不会有任何断言变红。把语义完整交给表里的解释句，
    // 于是「界面显示的语义」与「判定用的语义」是同一份数据推出来的。
    if (isEmpty()) {
        return QStringLiteral("%1：还没有表达式，当前不过滤任何条目")
            .arg(nameCombineModeLabel(m_combine));
    }
    return QStringLiteral("%1（%2 条表达式）：%3")
        .arg(nameCombineModeLabel(m_combine))
        .arg(expressionCount())
        .arg(nameCombineModeExplanation(m_combine));
}

void NameFilter::setDefaultMatchMode(NameMatchMode mode)
{
    m_defaultMode = mode;
}

void NameFilter::setCaseSensitivity(Qt::CaseSensitivity cs)
{
    m_case = cs;
    m_caseOverridden = true;
}

void NameFilter::clearCaseSensitivityOverride()
{
    m_caseOverridden = false;
    m_case = defaultCaseSensitivity(m_platform);
}

void NameFilter::setMatchBudget(const NameMatchBudget &budget)
{
    m_budget = budget;
}

void NameFilter::setMatchRunner(const std::shared_ptr<NameMatchRunner> &runner)
{
    m_runner = runner;
}

bool NameFilter::isExpressionDisabled(int index) const
{
    return index >= 0 && index < m_disabled.size() && m_disabled.at(index);
}

int NameFilter::disabledExpressionCount() const
{
    // 数「真被停用的条数」而不是 `m_disabled.size()`：后者是「可能被停用的
    // 槽位数」，恒等于表达式总数。误用它会让「有没有表达式被停用」这个判断
    // 恒为真，界面上的告警从此一直亮着——用户很快就不看它了。
    int count = 0;
    for (bool disabled : m_disabled) {
        if (disabled) {
            ++count;
        }
    }
    return count;
}

void NameFilter::resetTimeoutState()
{
    m_timeoutCount = 0;
    m_disabled.fill(false, m_expressions.size());
    m_consecutiveTimeouts.fill(0, m_expressions.size());
}

bool NameFilter::noteTimeout(int index, const NameFilterExpression &expression) const
{
    Q_UNUSED(expression);
    ++m_timeoutCount;

    if (index < 0 || index >= m_consecutiveTimeouts.size()) {
        return false;
    }
    m_consecutiveTimeouts[index] += 1;

    if (!m_budget.hasCircuitBreaker()) {
        return false;
    }
    if (m_consecutiveTimeouts.at(index) < m_budget.consecutiveTimeoutLimit) {
        return false;
    }
    if (index >= m_disabled.size()) {
        m_disabled.resize(m_expressions.size());
    }
    if (m_disabled.at(index)) {
        return false; // 已经停用过，不重复报
    }
    m_disabled[index] = true;
    return true;
}

NameMatchOutcome NameFilter::evaluate(int index, const QString &name,
                                     QVector<NameFilterIssue> *issues) const
{
    const NameFilterExpression &expression = m_expressions.at(index);

    // 停用之后不再尝试匹配：这正是断路器存在的意义（不再往那个跑不完的能力上
    // 投任务）。结论是 Undecided 而不是 NotMatched —— 见文件顶部第 3 条约定。
    if (isExpressionDisabled(index)) {
        return NameMatchOutcome::Undecided;
    }

    bool matched = false;
    switch (expression.mode) {
    case NameMatchMode::Exact:
        matched = (m_case == Qt::CaseSensitive)
                      ? (name == expression.text)
                      : (name.compare(expression.text, Qt::CaseInsensitive) == 0);
        if (index < m_consecutiveTimeouts.size()) {
            m_consecutiveTimeouts[index] = 0;
        }
        return matched ? NameMatchOutcome::Matched : NameMatchOutcome::NotMatched;

    case NameMatchMode::Wildcard:
        matched = index < m_masks.size()
                  && m_masks.at(index).matches(MaskSubject::forName(name), m_case);
        if (index < m_consecutiveTimeouts.size()) {
            m_consecutiveTimeouts[index] = 0;
        }
        return matched ? NameMatchOutcome::Matched : NameMatchOutcome::NotMatched;

    case NameMatchMode::Regex:
        break;
    }

    if (index >= m_regexes.size()) {
        return NameMatchOutcome::Undecided;
    }

    // 正则里的大小写策略由正则自己的选项表达（`Mask` 那边是匹配时传参，
    // 这里没有那样的入口），而 `m_regexes` 里存的是**不带选项**的编译结果——
    // 这样「改一下大小写开关」不必重新解析整份声明。代价就是这里必须补一次
    // `setPatternOptions`，漏掉它的现象是「正则模式下大小写开关不起作用」，
    // 而另外两个模式都正常，用户只会觉得开关时灵时不灵。
    QRegularExpression regex = m_regexes.at(index);
    if (m_case == Qt::CaseInsensitive) {
        regex.setPatternOptions(regex.patternOptions()
                                | QRegularExpression::CaseInsensitiveOption);
    }

    // **按值**捕获正则与名字：超时被放弃的那次匹配会在工作线程上继续跑，
    // 而它的持有者（本次 decide() 的调用栈、乃至这个 NameFilter）可能已经没了。
    // 引用捕获在这里是悬垂引用——现象是「过滤器偶发崩在无关的地方」。
    // QRegularExpression 与 QString 都是隐式共享的，按值捕获只是多两次引用计数。
    const QString subject = name;
    const int budgetMs = m_budget.perEntryMs;

    const std::function<bool()> task = [regex, subject]() {
        return regexMatchesWholeName(regex, subject);
    };

    if (!m_budget.isEnabled()) {
        matched = task();
        if (index < m_consecutiveTimeouts.size()) {
            m_consecutiveTimeouts[index] = 0;
        }
        return matched ? NameMatchOutcome::Matched : NameMatchOutcome::NotMatched;
    }

    std::shared_ptr<NameMatchRunner> runner = m_runner;
    if (!runner) {
        runner = defaultNameMatchRunner();
    }

    const NameMatchRunner::Outcome outcome = runner->run(task, budgetMs);
    if (outcome.status == NameMatchRunner::Status::Completed) {
        if (index < m_consecutiveTimeouts.size()) {
            m_consecutiveTimeouts[index] = 0;
        }
        return outcome.matched ? NameMatchOutcome::Matched : NameMatchOutcome::NotMatched;
    }

    const bool disabled = noteTimeout(index, expression);
    if (issues) {
        issues->append(makeIssue(NameFilterIssueKind::Timeout, expression.line, expression.column,
                                 qMax(1, expression.text.size()), expression.text,
                                 QStringLiteral("这条正则匹配 `%1` 超过了 %2ms 的时间预算")
                                     .arg(name)
                                     .arg(budgetMs),
                                 QStringLiteral("该条目结论为「不确定」并已放行；"
                                                "建议把它改写得具体一些（避免嵌套量词）")));
        if (disabled) {
            issues->append(makeIssue(
                NameFilterIssueKind::Disabled, expression.line, expression.column,
                qMax(1, expression.text.size()), expression.text,
                QStringLiteral("这条表达式连续超时 %1 次，已停用")
                    .arg(m_budget.consecutiveTimeoutLimit),
                QStringLiteral("Qt 5.15 无法中断正在跑的正则匹配，"
                               "继续使用会让后续每个条目都报超时；"
                               "改好表达式后点「重新校验」即可恢复")));
        }
    }
    return NameMatchOutcome::Undecided;
}

NameFilterDecision NameFilter::decide(const QString &name) const
{
    NameFilterDecision decision;
    decision.combine = m_combine;

    // 空过滤器：什么都不过滤，结论恒为保留。
    // 这条要在「名字是否为空」之前判——没有配置过滤时，用户还没选任何条目，
    // 这时候给出「排除」是纯粹的误导。（启动自检盯着这一条。）
    if (isEmpty()) {
        decision.accepted = true;
        return decision;
    }

    if (!MaskSubject::forName(name).isValid()) {
        // 残缺的条目：保守挡在外面，与 `MaskFilter::decide` 同一方向。
        // 这里**不**跑组合语义：空名字在「不包含任何」下本来会算成「保留」，
        // 而那正是这个分支要避免的结果。三个模式一律报 NotMatched，
        // 让界面显示的「为什么看不到它」与结论一致。
        decision.accepted = false;
        decision.outcomes.fill(NameMatchOutcome::NotMatched, m_expressions.size());
        return decision;
    }

    const int count = m_expressions.size();
    decision.outcomes.reserve(count);
    for (int index = 0; index < count; ++index) {
        const NameMatchOutcome outcome = evaluate(index, name, &decision.issues);
        decision.outcomes.append(outcome);
        if (outcome == NameMatchOutcome::Matched) {
            decision.matchedIndexes.append(index);
        } else if (outcome == NameMatchOutcome::Undecided) {
            decision.undecidedIndexes.append(index);
        }
    }

    const bool hasUndecided = !decision.undecidedIndexes.isEmpty();

    switch (m_combine) {
    case NameCombineMode::AnyOf:
        if (!decision.matchedIndexes.isEmpty()) {
            decision.accepted = true;
            decision.decisiveIndex = decision.matchedIndexes.first();
        } else if (hasUndecided) {
            // 不确定一律放行：见文件顶部第 3 条约定。
            decision.accepted = true;
            decision.decisiveIndex = -1;
        } else {
            decision.accepted = false;
        }
        break;

    case NameCombineMode::NoneOf:
        if (!decision.matchedIndexes.isEmpty()) {
            decision.accepted = false;
            decision.decisiveIndex = decision.matchedIndexes.first();
        } else {
            decision.accepted = true;
            decision.decisiveIndex = -1;
        }
        break;

    case NameCombineMode::AllOf: {
        int firstNotMatched = -1;
        for (int index = 0; index < count; ++index) {
            if (decision.outcomes.at(index) == NameMatchOutcome::NotMatched) {
                firstNotMatched = index;
                break;
            }
        }
        if (firstNotMatched >= 0) {
            decision.accepted = false;
            decision.decisiveIndex = firstNotMatched;
        } else {
            decision.accepted = true;
            decision.decisiveIndex = -1;
        }
        break;
    }
    }

    return decision;
}

bool NameFilter::accepts(const QString &name) const
{
    return decide(name).accepted;
}

NameFilterProblems NameFilter::validate() const
{
    NameFilterProblems problems;
    for (const NameFilterExpression &expression : m_expressions) {
        // 重新拼出一行再交给**同一个**单行分析函数：这样「什么叫合法」只有一份
        // 实现。如果这里另写一遍校验，两份实现迟早分家，而分家的表现是
        // 「界面上没报错、判定时却永远不匹配」——用户完全无法自查。
        const QString line = nameMatchModePrefix(expression.mode) + expression.text;
        const NameFilterLineAnalysis analysis = analyzeNameFilterLine(line, expression.line, m_platform);
        for (const NameFilterIssue &issue : analysis.issues) {
            problems.issues.append(issue);
        }
    }
    return problems;
}

QString NameFilter::describe() const
{
    return QStringLiteral("%1，%2 条表达式，大小写%3（平台默认：%4）")
        .arg(nameCombineModeLabel(m_combine))
        .arg(expressionCount())
        .arg(m_case == Qt::CaseSensitive ? QStringLiteral("敏感") : QStringLiteral("不敏感"),
             QString::fromLatin1(maskPlatformIdentifier(m_platform)));
}

QString NameFilter::toDeclarationText() const
{
    QStringList lines;
    for (const NameFilterExpression &expression : m_expressions) {
        lines.append(nameMatchModePrefix(expression.mode) + expression.text);
    }
    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 预设（第 5 条完成标准）
// -----------------------------------------------------------------------------

namespace {

///
/// 预设记录的标记。
///
/// 用 `QString` 而不是 `const char *`：它是中文，而 `qstrlen()` 数的是**字节**数
/// （`[预设]` 是 8 个字节、5 个字符）。拿它去 `QString::mid()` 切下来的名字会
/// **静默**变成半个字——预设名看起来像乱码，而没有任何地方会报错。
/// 本仓已有两条同源的坑记录（含中文的字面量表字段别用 `const char *`）。
///
QString presetRecordMarker() { return QStringLiteral("[预设]"); }
QString presetNoteKey() { return QStringLiteral("备注:"); }
QString presetCombineKey() { return QStringLiteral("组合:"); }
QString presetCaseKey() { return QStringLiteral("大小写:"); }

/// 元信息键表（唯一的事实来源：解析与「不认识这个键」的提示都从这里取）。
struct PresetMetaRow
{
    QString key;
    QString meaning;
};

QVector<PresetMetaRow> presetMetaTable()
{
    return QVector<PresetMetaRow>{
        PresetMetaRow{presetNoteKey(), QStringLiteral("这份预设是干什么的（自由文本）")},
        PresetMetaRow{presetCombineKey(),
                      QStringLiteral("组合语义：any-of / none-of / all-of，或中文标签")},
        PresetMetaRow{presetCaseKey(), QStringLiteral("大小写：sensitive / insensitive，或「敏感」/「不敏感」")},
    };
}

QString presetMetaKeyList()
{
    QStringList keys;
    for (const PresetMetaRow &row : presetMetaTable()) {
        keys.append(row.key);
    }
    return keys.join(QStringLiteral(" / "));
}

} // namespace

QString nameFilterPresetFormatHeader()
{
    return QStringLiteral("# LqCompare 名称过滤预设 v1");
}

QString nameFilterDeclarationKey()
{
    return QStringLiteral("name-filter");
}

QString serializeNamedNameFilters(const QVector<NamedNameFilter> &presets)
{
    QStringList lines;
    lines.append(nameFilterPresetFormatHeader());
    lines.append(QStringLiteral("# 每条预设以 %1 开头，随后是元信息行，再往下是声明体"
                                "（语法与名称过滤输入框一致）。")
                     .arg(presetRecordMarker()));
    for (const NamedNameFilter &preset : presets) {
        lines.append(QString());
        lines.append(QStringLiteral("%1 %2").arg(presetRecordMarker(), preset.name));
        if (!preset.note.isEmpty()) {
            lines.append(presetNoteKey() + QLatin1Char(' ') + preset.note);
        }
        lines.append(presetCombineKey() + QLatin1Char(' ') + nameCombineModeKey(preset.combine));
        if (preset.caseOverridden) {
            lines.append(presetCaseKey() + QLatin1Char(' ')
                         + (preset.caseSensitivity == Qt::CaseSensitive
                                ? QStringLiteral("sensitive")
                                : QStringLiteral("insensitive")));
        }
        const QStringList body = splitDeclarationLines(preset.declaration);
        for (const QString &bodyLine : body) {
            lines.append(bodyLine);
        }
    }
    lines.append(QString());
    return lines.join(QLatin1Char('\n'));
}

NamedNameFilterParseResult parseNamedNameFilters(const QString &text, MaskPlatform platform)
{
    Q_UNUSED(platform);

    NamedNameFilterParseResult result;

    NamedNameFilter current;
    bool inRecord = false;
    bool bodyStarted = false;
    QStringList body;
    int recordLine = -1;

    const auto flush = [&]() {
        if (!inRecord) {
            return;
        }
        // 记录之间那一个空行会被当成声明体的一部分（记录内部也可能用空行分组，
        // 不能一概丢掉）。末尾的空行不一样：它一定来自排版，留着会让
        // 「导出 → 导入 → 再导出」的文本每次多一个换行，往返不再稳定。
        while (!body.isEmpty() && body.last().trimmed().isEmpty()) {
            body.removeLast();
        }
        current.declaration = body.join(QLatin1Char('\n'));
        if (current.name.isEmpty()) {
            result.issues.append(makeIssue(NameFilterIssueKind::Syntax, recordLine, 0,
                                           0, presetRecordMarker(),
                                           QStringLiteral("这条预设没有名字"),
                                           QStringLiteral("写成 `%1 我的预设`")
                                               .arg(presetRecordMarker())));
        } else {
            result.presets.append(current);
        }
        current = NamedNameFilter();
        body.clear();
        bodyStarted = false;
    };

    const QStringList lines = splitDeclarationLines(text);
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index);
        const QString trimmed = trimBoth(line);

        if (trimmed.isEmpty()) {
            // 正文开始**之前**的空行直接跳过：它不该让「元信息块」结束
            // （`[预设] x` 之后空一行再写 `组合:` 是很自然的写法）。
            // 正文开始之后的空行原样保留，声明体里可能用它分组。
            if (inRecord && bodyStarted) {
                body.append(QString());
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (trimmed.startsWith(presetRecordMarker())) {
            flush();
            inRecord = true;
            bodyStarted = false;
            recordLine = index;
            current = NamedNameFilter();
            current.name = trimBoth(trimmed.mid(presetRecordMarker().size()));
            body.clear();
            continue;
        }
        if (!inRecord) {
            // 记录之外的正文：忽略（文件头可能有人写了几行说明）。
            continue;
        }
        if (!bodyStarted) {
            bool consumed = false;
            const QVector<PresetMetaRow> metaRows = presetMetaTable();
            for (const PresetMetaRow &row : metaRows) {
                if (!trimmed.startsWith(row.key)) {
                    continue;
                }
                const QString value = trimBoth(trimmed.mid(row.key.size()));
                consumed = true;
                if (row.key == presetNoteKey()) {
                    current.note = value;
                } else if (row.key == presetCombineKey()) {
                    bool ok = false;
                    const NameCombineMode mode = nameCombineModeFromKey(value, &ok);
                    if (!ok) {
                        QVector<NameCombineMode> all = allNameCombineModes();
                        QStringList accepted;
                        for (NameCombineMode candidate : all) {
                            accepted.append(nameCombineModeKey(candidate));
                        }
                        result.issues.append(
                            makeIssue(NameFilterIssueKind::Syntax, index, row.key.size(),
                                      qMax(1, value.size()), value,
                                      QStringLiteral("不认识的组合语义"),
                                      QStringLiteral("可用取值：%1").arg(accepted.join(QStringLiteral(" / ")))));
                    } else {
                        current.combine = mode;
                    }
                } else if (row.key == presetCaseKey()) {
                    const QString lower = value.toLower();
                    if (lower == QLatin1String("sensitive") || value == QStringLiteral("敏感")) {
                        current.caseOverridden = true;
                        current.caseSensitivity = Qt::CaseSensitive;
                    } else if (lower == QLatin1String("insensitive")
                               || value == QStringLiteral("不敏感")) {
                        current.caseOverridden = true;
                        current.caseSensitivity = Qt::CaseInsensitive;
                    } else if (lower == QLatin1String("platform")
                               || value == QStringLiteral("平台默认")) {
                        current.caseOverridden = false;
                    } else {
                        result.issues.append(
                            makeIssue(NameFilterIssueKind::Syntax, index, row.key.size(),
                                      qMax(1, value.size()), value,
                                      QStringLiteral("不认识的大小写取值"),
                                      QStringLiteral("可用取值：sensitive / insensitive / platform")));
                    }
                }
                break;
            }
            if (consumed) {
                continue;
            }
            bodyStarted = true;
        }
        body.append(line);
    }
    flush();

    // 一个字都没读到、也不是空文件：给一条提示，否则用户只会拿到一份空结果。
    if (result.presets.isEmpty() && result.issues.isEmpty()) {
        bool allIgnored = true;
        for (const QString &line : lines) {
            const QString trimmed = trimBoth(line);
            if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
                allIgnored = false;
                break;
            }
        }
        if (!allIgnored) {
            result.issues.append(
                makeIssue(NameFilterIssueKind::Syntax, -1, -1, 0, QString(),
                          QStringLiteral("没有找到任何预设"),
                          QStringLiteral("每条预设要以 `%1 名字` 开头；元信息键可用：%2")
                              .arg(presetRecordMarker(), presetMetaKeyList())));
        }
    }

    return result;
}

QString NamedNameFilterParseResult::describeErrors() const
{
    NameFilterProblems problems;
    problems.issues = issues;
    return problems.describe();
}

} // namespace Filter
} // namespace LqCompare
