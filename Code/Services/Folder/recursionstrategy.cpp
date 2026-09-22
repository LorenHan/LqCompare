#include "recursionstrategy.h"

#include <QDir>
#include <QFileInfo>

namespace LqCompare {
namespace Folder {

// ---------------------------------------------------------------------------
// 档位表
// ---------------------------------------------------------------------------

const QVector<RecursionTierDescriptor> &recursionTierTable()
{
    // 顺序 = 规格里点名的顺序（仅根目录直属条目 / 递归深度 1 / 完全递归）。
    // 界面下拉按这张表的顺序铺，因此这里的顺序本身就是一处事实来源：
    // 界面里另写一份清单就不会随表增长（新增档位时界面会静默落后一格）。
    //
    // `Full` 这一行的 depthLimit 是**缺省**而不是硬上限：它只在用户从别的档
    // 切过来时写回（见 `applyRecursionTier`），之后由用户另设（第 4 条）。
    static const QVector<RecursionTierDescriptor> table = {
        {RecursionTier::DirectChildren, "direct-children", false, kDirectChildrenDepth},
        {RecursionTier::OneLevel, "one-level", true, 1},
        {RecursionTier::Full, "full", true, kDefaultFullDepth},
    };
    return table;
}

QString recursionTierIdentifier(RecursionTier tier)
{
    for (const auto &row : recursionTierTable()) {
        if (row.tier == tier)
            return QString::fromUtf8(row.identifier);
    }
    return {};
}

QString recursionTierLabel(RecursionTier tier)
{
    switch (tier) {
    case RecursionTier::DirectChildren: return QObject::tr("仅根目录直属条目");
    case RecursionTier::OneLevel: return QObject::tr("递归深度 1");
    case RecursionTier::Full: return QObject::tr("完全递归");
    }
    return {};
}

QString recursionTierDescription(RecursionTier tier)
{
    switch (tier) {
    case RecursionTier::DirectChildren:
        return QObject::tr("只比较两个根目录里的条目；子目录仍然列出，但不进入、标记为未扫描。");
    case RecursionTier::OneLevel:
        return QObject::tr("进入直接子目录，比较其中的直属条目；更深的层次不再进入。");
    case RecursionTier::Full:
        return QObject::tr("递归遍历全部子树；可另设深度上限以避免超深目录树。");
    }
    return {};
}

bool recursionTierRecurses(RecursionTier tier)
{
    for (const auto &row : recursionTierTable()) {
        if (row.tier == tier)
            return row.recursive;
    }
    return false;
}

int recursionTierDepthLimit(RecursionTier tier)
{
    for (const auto &row : recursionTierTable()) {
        if (row.tier == tier)
            return row.depthLimit;
    }
    return kDirectChildrenDepth;
}

// ---------------------------------------------------------------------------
// 档位 ↔ Options
// ---------------------------------------------------------------------------

void applyRecursionTier(Options &options, RecursionTier tier)
{
    switch (tier) {
    case RecursionTier::DirectChildren:
        options.recursive = false;
        options.maximumDepth = kDirectChildrenDepth;
        return;
    case RecursionTier::OneLevel:
        options.recursive = true;
        options.maximumDepth = 1;
        return;
    case RecursionTier::Full:
        options.recursive = true;
        // 把与档位自相矛盾的值提上来，其余**原样保留**：用户另设的深度上限
        // 不因为「又选了一次完全递归」被重置回缺省。少了这一提，
        // `applyRecursionTier(o, Full)` 之后 `recursionTierOf(o)` 会落到别的档上。
        if (options.maximumDepth <= 1)
            options.maximumDepth = kDefaultFullDepth;
        return;
    }
}

RecursionTier recursionTierOf(const Options &options)
{
    // 按**行为**归类：`maximumDepth <= 0` 与 `recursive == false` 在引擎里
    // 完全同行为（`depth < maximumDepth` 永远不成立），因此它们是同一档。
    if (!options.recursive || options.maximumDepth <= kDirectChildrenDepth)
        return RecursionTier::DirectChildren;
    if (options.maximumDepth == 1)
        return RecursionTier::OneLevel;
    return RecursionTier::Full;
}

QStringList validateRecursionTierTable(const QVector<RecursionTierDescriptor> &table)
{
    QStringList problems;
    const QVector<RecursionTier> expected = {
        RecursionTier::DirectChildren, RecursionTier::OneLevel, RecursionTier::Full,
    };
    if (table.size() != expected.size())
        problems << QObject::tr("档位表应有 %1 行，实际 %2 行。")
                        .arg(expected.size()).arg(table.size());
    for (int i = 0; i < expected.size(); ++i) {
        if (i >= table.size())
            break;
        if (table.at(i).tier != expected.at(i))
            problems << QObject::tr("档位表第 %1 行应为档位 %2，实际 %3。")
                            .arg(i + 1).arg(int(expected.at(i))).arg(int(table.at(i).tier));
    }

    QVector<QString> identifiers;
    for (int i = 0; i < table.size(); ++i) {
        const auto &row = table.at(i);
        const QString identifier = QString::fromUtf8(row.identifier);
        if (identifier.isEmpty()) {
            problems << QObject::tr("档位表第 %1 行的标识符为空。").arg(i + 1);
        } else if (identifiers.contains(identifier)) {
            problems << QObject::tr("档位表里标识符「%1」重复。").arg(identifier);
        } else {
            identifiers.append(identifier);
            // 标识符要进设置文件、日志与命令行，含非 ASCII 时用 `const char *`
            // 承载会被当成 Latin-1 逐字符解释，比较永远不相等。
            bool machineReadable = true;
            for (const QChar &ch : identifier) {
                const bool ok = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
                    || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                    || ch == QLatin1Char('-');
                machineReadable &= ok;
            }
            if (!machineReadable)
                problems << QObject::tr("档位表里标识符「%1」不是机器可读的（只允许小写字母、数字与连字符）。")
                                .arg(identifier);
        }

        // 「不递归」与「深度上限为 0」必须同时成立：只写一边时档位与行为
        // 就成了两份说法，而反查按行为归类，于是同一份 `Options` 会有两个答案。
        if (row.recursive != (row.depthLimit > 0)) {
            problems << QObject::tr("档位表第 %1 行的 recursive=%2 与 depthLimit=%3 互相矛盾"
                                    "（不递归当且仅当深度上限为 0）。")
                            .arg(i + 1).arg(row.recursive ? "true" : "false").arg(row.depthLimit);
        }
        if (row.recursive && (row.depthLimit < 1 || row.depthLimit > kMaximumRecursionDepth)) {
            problems << QObject::tr("档位表第 %1 行是递归档，深度上限 %2 必须落在 1 到 %3 之间。")
                            .arg(i + 1).arg(row.depthLimit).arg(kMaximumRecursionDepth);
        }
    }

    // 反查穷尽性：每一档按自己声明的字段回代，必须还能被反查成它自己。
    // 这一条把「表改了、反查没跟上」变成一次红灯——否则下拉里那一档永远选不中。
    for (const auto &row : table) {
        Options probe;
        probe.recursive = row.recursive;
        probe.maximumDepth = row.depthLimit;
        if (recursionTierOf(probe) != row.tier) {
            problems << QObject::tr("档位表里「%1」声明的字段反查回档位 %2，与自身不一致。")
                            .arg(QString::fromUtf8(row.identifier)).arg(int(recursionTierOf(probe)));
        }
    }

    // `Full` 的缺省上限必须与 `Options::maximumDepth` 的初值一致（本模块头文件
    // 里写了理由）。两处都是字面量，头文件之间互相 include，`static_assert`
    // 写不出来，只能在这里报出来、再用例钉住这个等式。
    for (const auto &row : table) {
        if (row.tier == RecursionTier::Full && row.depthLimit != kDefaultFullDepth) {
            problems << QObject::tr("档位表里「完全递归」的缺省深度上限应为 %1，实际 %2。")
                            .arg(kDefaultFullDepth).arg(row.depthLimit);
        }
    }
    return problems;
}

// ---------------------------------------------------------------------------
// 深度边界上的解释
// ---------------------------------------------------------------------------

QString recursionBoundaryExplanation(const Options &options)
{
    const RecursionTier tier = recursionTierOf(options);
    if (tier != RecursionTier::Full) {
        return QObject::tr("当前档位「%1」，子目录未展开比较；其内部条目未枚举，"
                           "既不能判定为相同，也不能判定为不存在。")
            .arg(recursionTierLabel(tier));
    }
    return QObject::tr("已达到递归深度上限（%1），目录内容未比较；其内部条目未枚举，"
                       "既不能判定为相同，也不能判定为不存在。")
        // 与引擎的 `qBound(0, maximumDepth, kMaximumRecursionDepth)` 印同一个数：
        // 调用方直接传一个超界值（例如 300）时，实际生效的是 256，
        // 照着原值印会让提示里的数字与真正拦住扫描的那个数不是一回事。
        .arg(qBound(0, options.maximumDepth, kMaximumRecursionDepth));
}

// ---------------------------------------------------------------------------
// 循环符号链接
// ---------------------------------------------------------------------------

namespace {

// `ancestor` 是不是 `path` 的**严格**上级（相等不算，相等单独判）。
// 根目录的写法要特别对待：`QDir::cleanPath("/")` 是 `/`，直接拼 `/` 会得到 `//`，
// 于是「链接指向 `/`」这种真正的循环会被漏判。
bool isStrictAncestor(const QString &ancestor, const QString &path, Qt::CaseSensitivity cs)
{
    if (ancestor.isEmpty())
        return false;
    const QString prefix = ancestor.endsWith(QLatin1Char('/'))
        ? ancestor : ancestor + QLatin1Char('/');
    return path.startsWith(prefix, cs);
}

} // namespace

bool linkTargetReentersAncestor(const QString &rootPath, const QString &relativePath,
                                const QString &linkTarget, Qt::CaseSensitivity caseSensitivity)
{
    if (linkTarget.isEmpty())
        return false;
    const QString root = QDir::cleanPath(rootPath);
    if (root.isEmpty())
        return false;
    const QString link = QDir::cleanPath(root + QLatin1Char('/') + relativePath);
    // 相对目标按**链接所在目录**解析，不是按扫描根：`..` 在链接自己的位置上的
    // 含义才是它真正的含义。
    const QString resolved = QDir::isAbsolutePath(linkTarget)
        ? QDir::cleanPath(linkTarget)
        : QDir::cleanPath(QFileInfo(link).absolutePath() + QLatin1Char('/') + linkTarget);
    if (resolved.isEmpty())
        return false;
    // 指向自己：跟随它一步都不会前进。
    if (QString::compare(resolved, link, caseSensitivity) == 0)
        return true;
    // 指向自己的严格上级：跟随它必然再次枚举到这个链接。
    // 这一条同时覆盖「指向扫描根」与「越过扫描根」——两种情形下 resolved 都是
    // 链接路径的字符串上级，不需要为它们各开一档。
    return isStrictAncestor(resolved, link, caseSensitivity);
}

QString linkCycleExplanation(const QString &relativePath, const QString &linkTarget)
{
    return QObject::tr("检测到循环符号链接：%1 → %2 指向自身或其上级目录，跟随它会无限递归；"
                       "已终止展开并记为本条错误。")
        .arg(relativePath, linkTarget);
}

} // namespace Folder
} // namespace LqCompare
