/// \file
/// \brief Shell 集成的计划、安装、卸载与残留校验（PRD: PLAT-005）。
///
/// 本文件在**任意平台**上都会被编译与真实执行：它只操作注入进来的 RegistryStore。
/// 真正只有 Windows 有的那段在 registrystore_win.cpp 里。分法的理由写在
/// registrystore.h 的文件头——简单说，最容易出错、错了最难查的那一批规则
/// （卸载漏删、卸载删过头）都在这里，而它们与注册表调用无关。

#include "shellintegration.h"

#include <QSet>

#include <algorithm>

namespace LqCompare {
namespace Platform {

namespace {

/// 存储根。全部写在 HKCU 下：不需要管理员权限，
/// 也不会影响到同一台机器上的其它用户（他们没装就是没装）。
/// 若写到 HKLM，需要提权——而一个「比较工具」要求提权，用户会直接放弃安装。
const char *const kRoot = "HKEY_CURRENT_USER";

/// 文件关联与右键菜单都挂在 `Software\Classes` 下。
const char *const kClassesRoot = "Software\\Classes";

/// 登记信息的根。卸载时靠它找回「当时装了什么、覆盖了谁的什么值」。
const char *const kBookkeepingRootText = "Software\\LqCompare\\ShellIntegration";

constexpr int kBookkeepingVersion = 1;

const char *const kProgIdPatch = "LqCompare.PatchFile";
const char *const kProgIdDiff = "LqCompare.DiffFile";

/// 归档件（`.patch` / `.diff`）在注册表里对应的扩展名。
struct AssociationSpec
{
    const char *extension;      ///< ".patch"
    const char *progId;         ///< "LqCompare.PatchFile"
    const char *description;    ///< 资源管理器「类型」列里显示的文字
};

const AssociationSpec kAssociations[] = {
    {".patch", kProgIdPatch, "LqCompare 补丁文件"},
    {".diff", kProgIdDiff, "LqCompare 差异文件"},
};

/// 备份条目的值名。集中成常量：备份与还原必须用同一套名字，
/// 各写一遍字面量时改一处漏一处，表现是「卸载报告说还原了，其实没还原」。
namespace BackupField {
const char *const Key = "Key";
const char *const Name = "Name";
const char *const Kind = "Kind";
const char *const Existed = "Existed";
const char *const KeyExisted = "KeyExisted";
const char *const Text = "Text";
const char *const Number = "Number";
const char *const Raw = "Raw";
const char *const RawType = "RawType";
} // namespace BackupField

/// 登记信息的值名。
namespace MetaField {
const char *const Version = "Version";
const char *const Executable = "Executable";
const char *const Options = "Options";
} // namespace MetaField

QString joinKey(const QString &head, const QString &tail)
{
    if (head.isEmpty())
        return tail;
    if (tail.isEmpty())
        return head;
    return head + QLatin1Char('\\') + tail;
}

/// 按「深度倒序、同深度字典序」排列。卸载必须从最深的键开始删，
/// 否则父键带着子键一起被递归删掉，中途就看不到真实的残留情况了。
QStringList sortDeepestFirst(QStringList keys)
{
    std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
        const int leftDepth = RegistryStore::keyDepth(left);
        const int rightDepth = RegistryStore::keyDepth(right);
        if (leftDepth != rightDepth)
            return leftDepth > rightDepth;
        return left < right;
    });
    return keys;
}

QStringList sortShallowestFirst(QStringList keys)
{
    QStringList result = sortDeepestFirst(keys);
    std::reverse(result.begin(), result.end());
    return result;
}

QStringList dedupePreservingOrder(const QStringList &items)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &item : items) {
        const QString normalized = RegistryStore::normalizeKey(item);
        if (seen.contains(normalized))
            continue;
        seen.insert(normalized);
        result << item;
    }
    return result;
}

/// 一行日志用的错误描述：分类文案 + 原始码。
/// 与 Files/ 的约定一致（PLAT-008）：原始码必须在出错那一刻记下并一路带出来。
QString describeError(const Files::ErrorCode &error)
{
    if (error.ok())
        return QString();
    return QStringLiteral("%1（%2）")
            .arg(Files::errorMessage(error.category))
            .arg(Files::errorDetail(error));
}

} // namespace

// =============================================================================
// 一、菜单动作
// =============================================================================

const char *shellActionIdentifier(ShellAction action)
{
    switch (action) {
    case ShellAction::Compare:
        return "compare";
    case ShellAction::CompareWith:
        return "compare_with";
    case ShellAction::CompareAsLeft:
        return "compare_as_left";
    case ShellAction::CompareAsRight:
        return "compare_as_right";
    case ShellAction::CompareSecondStep:
        return "compare_second_step";
    case ShellAction::OpenAssociation:
        return "open";
    }
    return "compare";
}

bool shellActionFromIdentifier(const QString &identifier, ShellAction *out)
{
    const QString text = identifier.trimmed();
    for (ShellAction action : allShellActions()) {
        if (text == QLatin1String(shellActionIdentifier(action))) {
            if (out != nullptr)
                *out = action;
            return true;
        }
    }
    return false;
}

QVector<ShellAction> allShellActions()
{
    return QVector<ShellAction>{ShellAction::Compare,
                                ShellAction::CompareWith,
                                ShellAction::CompareAsLeft,
                                ShellAction::CompareAsRight,
                                ShellAction::CompareSecondStep,
                                ShellAction::OpenAssociation};
}

QString shellActionMenuText(ShellAction action)
{
    // `&` 是资源管理器菜单里的加速键标记（显示时下划线，不显示 `&` 本身）。
    // 中文菜单上仍然用拉丁字母作加速键，是因为中文输入法状态下按字母更可靠。
    switch (action) {
    case ShellAction::Compare:
        return QStringLiteral("比较(&C)");
    case ShellAction::CompareWith:
        return QStringLiteral("与…比较（记住为左侧）(&W)");
    case ShellAction::CompareAsLeft:
        return QStringLiteral("作为左侧比较(&L)");
    case ShellAction::CompareAsRight:
        return QStringLiteral("作为右侧比较(&R)");
    case ShellAction::CompareSecondStep:
        // 占位菜单项的文案刻意写着「已选中的左侧」而不是「与…比较」：
        // 它是**第二步**，用户从这里点进来时手上只有一个条目。
        // 没有待比较的左侧时，本程序会给出明确说明而不是默默什么都不做。
        return QStringLiteral("与已选中的左侧比较(&2)");
    case ShellAction::OpenAssociation:
        return QStringLiteral("用 LqCompare 打开");
    }
    return QString();
}

QString shellActionRegistryName(ShellAction action)
{
    return QStringLiteral("LqCompare.") + QLatin1String(shellActionIdentifier(action));
}

int shellActionRequiredPathCount(ShellAction action)
{
    switch (action) {
    case ShellAction::Compare:
        // 需要一个 left 与一个 right。少一个就必须在入口处停下——
        // 拿同一个条目和自己比较会得出「完全相同」这个**错误但看起来合理**的结论。
        return 2;
    case ShellAction::CompareWith:
    case ShellAction::CompareAsLeft:
    case ShellAction::CompareAsRight:
    case ShellAction::CompareSecondStep:
    case ShellAction::OpenAssociation:
        return 1;
    }
    return 1;
}

int shellActionMaximumPathCount(ShellAction action)
{
    switch (action) {
    case ShellAction::Compare:
        // 资源管理器可以把 20 个选中项都塞进 `%1`。多于一左一右时
        // 本程序只取前两个并明确告知——但菜单上的 `MultiSelectModel`
        // 已经尽量把选择限制在合理范围，这一条是兜底。
        return -1;
    default:
        return 1;
    }
}

bool shellActionIsPlaceholder(ShellAction action)
{
    // 只有它是「占位」：它的存在是为了让菜单形状稳定，
    // 真正的动作取决于前一步有没有做过。
    return action == ShellAction::CompareSecondStep;
}

bool shellActionIsAssociationOnly(ShellAction action)
{
    return action == ShellAction::OpenAssociation;
}

QString shellActionDescription(ShellAction action)
{
    switch (action) {
    case ShellAction::Compare:
        return QStringLiteral("选中两个条目后右键，直接打开比较窗口。");
    case ShellAction::CompareWith:
        return QStringLiteral("先记住当前选中的条目作为左侧，再对第二个条目右键并选择"
                              "「与已选中的左侧比较」。");
    case ShellAction::CompareAsLeft:
        return QStringLiteral("把当前选中的条目记为左侧，并由本程序弹出窗口让你挑右侧。");
    case ShellAction::CompareAsRight:
        return QStringLiteral("把当前选中的条目记为右侧；已经有左侧时立刻开始比较。");
    case ShellAction::CompareSecondStep:
        return QStringLiteral("两步式的第二步。需要先用「与…比较」或「作为左侧比较」"
                              "选定左侧，否则会选择暂无内容。");
    case ShellAction::OpenAssociation:
        return QStringLiteral("用本程序打开这个文件。");
    }
    return QString();
}

// =============================================================================
// 二、菜单项挂在哪一类对象上
// =============================================================================

QVector<ShellTarget> allShellTargets()
{
    return QVector<ShellTarget>{ShellTarget::Files, ShellTarget::Directories,
                                ShellTarget::Background};
}

const char *shellTargetIdentifier(ShellTarget target)
{
    switch (target) {
    case ShellTarget::Files:
        return "files";
    case ShellTarget::Directories:
        return "directories";
    case ShellTarget::Background:
        return "background";
    }
    return "files";
}

QString shellTargetClassKey(ShellTarget target)
{
    switch (target) {
    case ShellTarget::Files:
        // `*` 在注册表里表示「任意类型的文件」。
        return QStringLiteral("*");
    case ShellTarget::Directories:
        return QStringLiteral("Directory");
    case ShellTarget::Background:
        // 注意是 `Directory\Background`（一级子键），不是 `Directory.*`。
        // 写成前者之外的东西时资源管理器的表现是**不报错、只是不显示**。
        return QStringLiteral("Directory\\Background");
    }
    return QStringLiteral("*");
}

QString shellTargetDescription(ShellTarget target)
{
    switch (target) {
    case ShellTarget::Files:
        return QStringLiteral("选中文件时");
    case ShellTarget::Directories:
        return QStringLiteral("选中文件夹时");
    case ShellTarget::Background:
        return QStringLiteral("在文件夹空白处右键时");
    }
    return QString();
}

QVector<ShellAction> shellActionsForTarget(ShellTarget target)
{
    QVector<ShellAction> actions;

    if (target != ShellTarget::Background) {
        // 「比较」只在这两类对象上有意义：空白处一个条目都没选中。
        // 在那里放一个必然报错的菜单项，只会让用户以为自己用错了。
        actions << ShellAction::Compare;
    }

    actions << ShellAction::CompareWith;
    actions << ShellAction::CompareAsLeft;
    actions << ShellAction::CompareAsRight;
    actions << ShellAction::CompareSecondStep;

    return actions;
}

bool shellActionIncludedByOptions(ShellAction action, const ShellIntegrationOptions &options)
{
    // 文件关联的动作不产生右键菜单项，它们由 patchAssociation / diffAssociation
    // 两个开关控制。这里返回 false 而不是 true，是为了让「菜单项的循环」
    // 拿到一个可以直接用的答案——若返回 true，某个将来把 OpenAssociation
    // 加进 shellActionsForTarget() 的改动就会静默地多出一个菜单项。
    if (shellActionIsAssociationOnly(action))
        return false;

    if (!options.contextMenu)
        return false;

    // 两步式的两个菜单项受独立开关控制：它们比普通菜单项多一份跨进程状态
    // （本程序要记住「左侧是谁」），不想要这个功能的用户可以只关掉它们。
    // 这也正是「可单独关闭」这条要求在右键菜单内部的落点。
    if (action == ShellAction::CompareWith || action == ShellAction::CompareSecondStep)
        return options.twoStepCompare;

    return true;
}

// =============================================================================
// 三、安装选项
// =============================================================================

bool ShellIntegrationOptions::operator==(const ShellIntegrationOptions &other) const
{
    return contextMenu == other.contextMenu && twoStepCompare == other.twoStepCompare
            && patchAssociation == other.patchAssociation
            && diffAssociation == other.diffAssociation && menuIcon == other.menuIcon
            && positionAtTop == other.positionAtTop;
}

bool ShellIntegrationOptions::anyEnabled() const
{
    return contextMenu || patchAssociation || diffAssociation;
}

QString ShellIntegrationOptions::summary() const
{
    QStringList parts;

    if (!contextMenu) {
        parts << QStringLiteral("不装右键菜单");
    } else if (twoStepCompare) {
        parts << QStringLiteral("右键菜单（含两步式）");
    } else {
        parts << QStringLiteral("右键菜单（不含两步式）");
    }

    switch (static_cast<int>(patchAssociation) + static_cast<int>(diffAssociation)) {
    case 0:
        parts << QStringLiteral("不改文件关联");
        break;
    case 1:
        parts << QStringLiteral("关联 %1")
                         .arg(patchAssociation ? QStringLiteral(".patch")
                                               : QStringLiteral(".diff"));
        break;
    default:
        parts << QStringLiteral("关联 .patch 与 .diff");
        break;
    }

    return parts.join(QStringLiteral("；"));
}

QString shellOptionsToString(const ShellIntegrationOptions &options)
{
    // 格式刻意用 `键=值;` 这种最土的写法：它要在注册表里被人肉看一眼时能读懂。
    // 用 JSON 也能工作，但注册表编辑器里显示成一长串转义引号，
    // 排查时反而要额外解码一次。
    const auto flag = [](bool on) { return on ? QStringLiteral("1") : QStringLiteral("0"); };

    QStringList parts;
    parts << QStringLiteral("contextMenu=%1").arg(flag(options.contextMenu));
    parts << QStringLiteral("twoStepCompare=%1").arg(flag(options.twoStepCompare));
    parts << QStringLiteral("patchAssociation=%1").arg(flag(options.patchAssociation));
    parts << QStringLiteral("diffAssociation=%1").arg(flag(options.diffAssociation));
    parts << QStringLiteral("menuIcon=%1").arg(flag(options.menuIcon));
    parts << QStringLiteral("positionAtTop=%1").arg(flag(options.positionAtTop));
    return parts.join(QLatin1Char(';'));
}

bool shellOptionsFromString(const QString &text, ShellIntegrationOptions *out,
                            QStringList *unknownKeys)
{
    if (unknownKeys != nullptr)
        unknownKeys->clear();

    ShellIntegrationOptions options;

    // `Qt::SkipEmptyParts`（不是已弃用的 `QString::SkipEmptyParts`）：
    // 后者在 Qt 5.15 起会报 -Wdeprecated-declarations。空片段本来就该丢掉——
    // 手工在 regedit 里编辑时很容易多留一个 `;;`，那不该导致整串解析失败。
    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int cut = part.indexOf(QLatin1Char('='));
        if (cut <= 0)
            continue;

        const QString key = part.left(cut).trimmed();
        const QString value = part.mid(cut + 1).trimmed();
        const bool on = (value == QLatin1String("1")
                         || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);

        // 认不出的键要收集起来，不能默默忽略：那可能是**更新版本写下的**配置，
        // 意味着有本版本不知道的项。默默忽略会让卸载报告说「干净」，
        // 而注册表里还留着东西——这正是「卸载后无残留」最隐蔽的一种失败。
        bool recognized = true;
        if (key.compare(QLatin1String("contextMenu"), Qt::CaseInsensitive) == 0)
            options.contextMenu = on;
        else if (key.compare(QLatin1String("twoStepCompare"), Qt::CaseInsensitive) == 0)
            options.twoStepCompare = on;
        else if (key.compare(QLatin1String("patchAssociation"), Qt::CaseInsensitive) == 0)
            options.patchAssociation = on;
        else if (key.compare(QLatin1String("diffAssociation"), Qt::CaseInsensitive) == 0)
            options.diffAssociation = on;
        else if (key.compare(QLatin1String("menuIcon"), Qt::CaseInsensitive) == 0)
            options.menuIcon = on;
        else if (key.compare(QLatin1String("positionAtTop"), Qt::CaseInsensitive) == 0)
            options.positionAtTop = on;
        else
            recognized = false;

        if (!recognized && unknownKeys != nullptr && !unknownKeys->contains(key))
            unknownKeys->append(key);
    }

    if (out != nullptr)
        *out = options;
    return true;
}

// =============================================================================
// 四、命令行
// =============================================================================

namespace ShellPlaceholder {
const char *const SingleItem = "\"%1\"";
const char *const TargetItem = "\"%V\"";
} // namespace ShellPlaceholder

const char *const ShellActionOptionName = "--shell-action";

QString quoteShellArgument(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1String("\""), QLatin1String("\\\""));

    // 结尾的连续反斜杠必须翻倍。理由见头文件的说明：
    // `"C:\dir\"` 里的 `\"` 被解析成字面引号，引号因此没有闭合，
    // 后面的参数会全部被粘进路径里。
    int trailing = 0;
    for (int i = quoted.size() - 1; i >= 0 && quoted.at(i) == QLatin1Char('\\'); --i)
        ++trailing;
    quoted += QString(trailing, QLatin1Char('\\'));

    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString buildShellCommandLine(const QString &executablePath, ShellAction action,
                              const QStringList &placeholders)
{
    QStringList parts;
    parts << quoteShellArgument(executablePath);
    parts << QStringLiteral("%1=%2")
                     .arg(QLatin1String(ShellActionOptionName),
                          QLatin1String(shellActionIdentifier(action)));

    // 占位符**原样**拼进去，不再加引号：调用方给的就是已经带引号的
    // `"%1"`。若在这里再 quote 一次会得到 `""%1""`，
    // 资源管理器替换之后路径两边会多出一对空引号，变成一个空参数。
    for (const QString &placeholder : placeholders) {
        if (!placeholder.isEmpty())
            parts << placeholder;
    }

    return parts.join(QLatin1Char(' '));
}

QString buildShellInvocationLine(const QString &executablePath, ShellAction action)
{
    return buildShellCommandLine(executablePath, action,
                                 QStringList{QLatin1String(ShellPlaceholder::SingleItem)});
}

// =============================================================================
// 五、计划
// =============================================================================

const char *shellEntryDispositionIdentifier(ShellEntryDisposition disposition)
{
    switch (disposition) {
    case ShellEntryDisposition::Owned:
        return "owned";
    case ShellEntryDisposition::Shared:
        return "shared";
    }
    return "owned";
}

QString ShellEntry::describe() const
{
    const QString target = key + QLatin1Char('\\')
            + (name.isEmpty() ? QStringLiteral("(Default)") : name);
    return QStringLiteral("%1 = %2").arg(target, value.display());
}

QString shellBookkeepingRoot()
{
    return QString::fromLatin1(kBookkeepingRootText);
}

int shellBookkeepingVersion()
{
    return kBookkeepingVersion;
}

bool ShellIntegrationPlan::isEmpty() const
{
    return entries.isEmpty();
}

int ShellIntegrationPlan::entryCount() const
{
    return entries.size();
}

int ShellIntegrationPlan::valueCount() const
{
    return entries.size();
}

QStringList ShellIntegrationPlan::keys() const
{
    QStringList result;
    for (const ShellEntry &entry : entries)
        result << entry.key;
    result = dedupePreservingOrder(result);
    return sortShallowestFirst(result);
}

QVector<ShellEntry> ShellIntegrationPlan::entriesFor(const QString &key) const
{
    const QString wanted = RegistryStore::normalizeKey(key);
    QVector<ShellEntry> result;
    for (const ShellEntry &entry : entries) {
        if (RegistryStore::normalizeKey(entry.key) == wanted)
            result << entry;
    }
    return result;
}

QStringList ShellIntegrationPlan::backupKeys() const
{
    QStringList result;
    for (const ShellEntry &entry : entries) {
        if (entry.disposition == ShellEntryDisposition::Shared)
            result << entry.key;
    }
    return dedupePreservingOrder(result);
}

QString ShellIntegrationPlan::summary() const
{
    if (isEmpty()) {
        return QStringLiteral("没有任何可安装的项（右键菜单与文件关联都被关掉了）。");
    }
    return QStringLiteral("将写入 %1 个值到 %2 个注册表键，其中 %3 个键可能已有内容"
                          "（安装前会先记下原值，卸载时还原）。")
            .arg(valueCount())
            .arg(keys().size())
            .arg(backupKeys().size());
}

QStringList ShellIntegrationPlan::detailLines() const
{
    QStringList lines;

    if (!options.contextMenu) {
        lines << QStringLiteral("右键菜单：不安装");
    } else {
        for (ShellTarget target : allShellTargets()) {
            QStringList names;
            const QVector<ShellAction> actions = shellActionsForTarget(target);
            for (ShellAction action : actions) {
                if (!shellActionIncludedByOptions(action, options))
                    continue;
                names << shellActionMenuText(action);
            }
            lines << QStringLiteral("%1：%2").arg(shellTargetDescription(target),
                                                  names.join(QStringLiteral("、")));
        }
    }

    if (extensions.isEmpty()) {
        lines << QStringLiteral("文件关联：不安装");
    } else {
        lines << QStringLiteral("文件关联：%1").arg(extensions.join(QStringLiteral("、")));
    }

    for (const QString &key : keys())
        lines << QStringLiteral("  %1").arg(key);

    return lines;
}

ShellIntegrationPlan buildShellIntegrationPlan(const QString &executablePath,
                                               const ShellIntegrationOptions &options)
{
    ShellIntegrationPlan plan;
    plan.rootKey = QString::fromLatin1(kRoot);
    plan.executablePath = executablePath;
    plan.options = options;
    plan.bookkeepingRoot = shellBookkeepingRoot();

    const QString classesRoot = QString::fromLatin1(kClassesRoot);

    // ---- 右键菜单项（完成标准第 1、2 条） ----
    if (options.contextMenu) {
        for (ShellTarget target : allShellTargets()) {
            for (ShellAction action : shellActionsForTarget(target)) {
                if (!shellActionIncludedByOptions(action, options))
                    continue;

                const QString verbKey = joinKey(
                        classesRoot,
                        QStringLiteral("%1\\shell\\%2")
                                .arg(shellTargetClassKey(target),
                                     shellActionRegistryName(action)));

                ShellEntry menuText;
                menuText.key = verbKey;
                menuText.name = QStringLiteral("MUIVerb");
                menuText.value = RegistryValue::of(shellActionMenuText(action));
                menuText.purpose = QStringLiteral("%1 的菜单文字")
                                           .arg(shellActionDescription(action));
                plan.entries << menuText;

                // MultiSelectModel 决定资源管理器允许多选几个。
                // 不设它的话「作为左侧比较」可以在选中 20 个文件时被点中，
                // 而我们只会用其中一个——另外 19 个被静默忽略。
                ShellEntry multiSelect;
                multiSelect.key = verbKey;
                multiSelect.name = QStringLiteral("MultiSelectModel");
                multiSelect.value = RegistryValue::of(
                        shellActionMaximumPathCount(action) == 1 ? QStringLiteral("Single")
                                                                 : QStringLiteral("Document"));
                multiSelect.purpose = QStringLiteral("限制/放开采纳的选中条目个数");
                plan.entries << multiSelect;

                if (options.positionAtTop) {
                    ShellEntry position;
                    position.key = verbKey;
                    position.name = QStringLiteral("Position");
                    position.value = RegistryValue::of(QStringLiteral("Top"));
                    position.purpose = QStringLiteral("排在资源管理器内置项之前");
                    plan.entries << position;
                }

                if (options.menuIcon) {
                    ShellEntry icon;
                    icon.key = verbKey;
                    icon.name = QStringLiteral("Icon");
                    // 注册表里的 `Icon` 格式是 `"可执行文件路径",资源索引`。
                    //
                    // 这里用索引 0，也就是可执行文件自己的第一个图标资源。
                    // 想要一个专用图标需要把图标编进 exe 的资源节（.rc 文件），
                    // 本项目还没有加——索引指向不存在的资源时资源管理器的表现是
                    // 显示一个**空白占位**，不是报错，所以这个值必须与打包方式
                    // 一起改：加了 .rc 之后这里要换成对应的索引。
                    icon.value = RegistryValue::of(
                            quoteShellArgument(executablePath) + QStringLiteral(",0"));
                    icon.purpose = QStringLiteral("菜单项图标（索引 0 = 程序自身图标）");
                    plan.entries << icon;
                }

                ShellEntry command;
                command.key = joinKey(verbKey, QStringLiteral("shell\\command"));
                command.name = QString(); // (Default)
                command.value = RegistryValue::of(
                        buildShellInvocationLine(executablePath, action));
                command.purpose = QStringLiteral("点击「%1」时执行的命令")
                                          .arg(shellActionMenuText(action));
                plan.entries << command;

                // 这三个键都是我们创建的。列全部三层（而不是只列最外层）
                // 是为了让「按深度倒序、逐个判空」的删除过程在报告里
                // 每一步都有对应记录，出问题时能看出停在了哪一层。
                plan.ownedKeys << verbKey;
                plan.ownedKeys << joinKey(verbKey, QStringLiteral("shell"));
                plan.ownedKeys << joinKey(verbKey, QStringLiteral("shell\\command"));
            }
        }
    }

    // ---- 文件关联（完成标准第 3 条） ----
    for (const AssociationSpec &spec : kAssociations) {
        const bool wanted = (QLatin1String(spec.extension) == QLatin1String(".patch"))
                ? options.patchAssociation
                : options.diffAssociation;
        if (!wanted)
            continue;

        const QString extensionKey = joinKey(classesRoot, QLatin1String(spec.extension));
        const QString progIdKey = joinKey(classesRoot, QLatin1String(spec.progId));

        // 这一条是 Shared：`.patch` 这个键几乎一定早就在（属于别的程序或 Windows
        // 自己）。我们只借用它的默认值，安装前把原值记下来，卸载时还原。
        ShellEntry association;
        association.key = extensionKey;
        association.name = QString(); // (Default)
        association.value = RegistryValue::of(QLatin1String(spec.progId));
        association.disposition = ShellEntryDisposition::Shared;
        association.purpose = QStringLiteral("把 %1 的默认打开方式指向本程序"
                                             "（卸载时还原为安装前的值）")
                                      .arg(QLatin1String(spec.extension));
        plan.entries << association;

        ShellEntry description;
        description.key = progIdKey;
        description.name = QString(); // (Default)
        description.value = RegistryValue::of(QLatin1String(spec.description));
        description.purpose = QStringLiteral("资源管理器「类型」列里显示的名字");
        plan.entries << description;

        if (options.menuIcon) {
            ShellEntry icon;
            icon.key = joinKey(progIdKey, QStringLiteral("DefaultIcon"));
            icon.name = QString();
            icon.value = RegistryValue::of(
                    quoteShellArgument(executablePath) + QStringLiteral(",0"));
            icon.purpose = QStringLiteral("文件类型图标（索引 0 = 程序自身图标）");
            plan.entries << icon;
        }

        ShellEntry open;
        open.key = joinKey(progIdKey, QStringLiteral("shell\\open\\command"));
        open.name = QString();
        open.value = RegistryValue::of(
                buildShellInvocationLine(executablePath, ShellAction::OpenAssociation));
        open.purpose = QStringLiteral("双击 %1 时执行的命令")
                               .arg(QLatin1String(spec.extension));
        plan.entries << open;

        plan.progIds << QLatin1String(spec.progId);
        plan.extensions << QLatin1String(spec.extension);

        plan.ownedKeys << progIdKey;
        plan.ownedKeys << joinKey(progIdKey, QStringLiteral("DefaultIcon"));
        plan.ownedKeys << joinKey(progIdKey, QStringLiteral("shell"));
        plan.ownedKeys << joinKey(progIdKey, QStringLiteral("shell\\open"));
        plan.ownedKeys << joinKey(progIdKey, QStringLiteral("shell\\open\\command"));
    }

    // 登记键刻意**不**放进 ownedKeys。
    //
    // 它的子树（`Version` / `Executable` / `Options` / `Backup\<n>\*`）里
    // 有一半是运行时才知道下标的，塞进静态计划只会让「这个键是不是我们的」
    // 依赖一份容易过期的名单。而它整棵子树**按定义**都是我们写的，
    // 所以删除时直接递归删掉这个根就够了，不需要逐个判空——
    // 这是本文件里唯一允许递归删除的地方。
    // `Software\LqCompare` 这一级刻意不动：那里可能有本程序的其它设置，
    // 也可能有更新版本写入的东西。

    plan.sharedKeys = plan.backupKeys();
    plan.ownedKeys = dedupePreservingOrder(plan.ownedKeys);

    return plan;
}

// =============================================================================
// 六、执行结果
// =============================================================================

QString ShellChangeRecord::describe() const
{
    const QString target = key + QLatin1Char('\\')
            + (name.isEmpty() ? QStringLiteral("(Default)") : name);

    if (succeeded)
        return QStringLiteral("%1：%2").arg(detail, target);

    // 失败时**必须**把 detail 一并带出来。
    //
    // 只给「失败 + 错误分类」的后果是：报告说「失败 ……：找不到 该路径」，
    // 而用户看不出找不到的是**哪一项**、期望它是什么值。
    // 分辨「缺失」与「值不对」这两件事的全部信息都在 detail 里
    // （「缺少这一项（期望 X）」与「现在的值是 Y，期望 X」）。
    const QString reason = describeError(error);
    if (detail.isEmpty())
        return QStringLiteral("失败 %1：%2").arg(target, reason);
    return QStringLiteral("失败 %1：%2 —— %3").arg(target, reason, detail);
}

bool ShellIntegrationReport::allSucceeded() const
{
    for (const ShellChangeRecord &record : records) {
        if (!record.succeeded)
            return false;
    }
    return true;
}

bool ShellIntegrationReport::ok() const
{
    if (!allSucceeded())
        return false;
    if (rolledBack && !rollbackClean)
        return false;
    if (residueChecked && !residueClean)
        return false;
    return true;
}

int ShellIntegrationReport::succeededCount() const
{
    int count = 0;
    for (const ShellChangeRecord &record : records) {
        if (record.succeeded)
            ++count;
    }
    return count;
}

int ShellIntegrationReport::failedCount() const
{
    return records.size() - succeededCount();
}

QStringList ShellIntegrationReport::failedKeys() const
{
    QStringList result;
    for (const ShellChangeRecord &record : records) {
        if (!record.succeeded)
            result << record.key;
    }
    return dedupePreservingOrder(result);
}

QStringList ShellIntegrationReport::failureGroups() const
{
    // 与 PLAT-008 的失败清单同一个思路：同一类错误的处置动作只有一个，
    // 平铺 60 条「拒绝访问」要用户做 60 次判断，按分类归并后只需一次。
    //
    // 分组键直接用枚举本身，不走 errorIdentifier() 字符串再解析回来——
    // 那样要额外写一个「从字符串还原枚举」的函数，而它是第二份事实来源：
    // 加了新分类忘记同步时，分类会静默地合并成一类。
    QMap<int, QStringList> grouped;
    QMap<int, Files::FileSystemError> categories;

    for (const ShellChangeRecord &record : records) {
        if (record.succeeded)
            continue;
        const int key = static_cast<int>(record.error.category);
        grouped[key] << record.key;
        categories.insert(key, record.error.category);
    }

    QStringList lines;
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        const Files::FileSystemError category = categories.value(it.key());
        lines << QStringLiteral("%1 个键：%2 —— %3")
                     .arg(it.value().size())
                     .arg(Files::errorMessage(category))
                     .arg(Files::errorAdvice(category));
    }
    return lines;
}

QString ShellIntegrationReport::summary() const
{
    QStringList parts;
    parts << QStringLiteral("%1：成功 %2 项，失败 %3 项")
                 .arg(operationName)
                 .arg(succeededCount())
                 .arg(failedCount());

    if (rolledBack)
        parts << (rollbackClean ? QStringLiteral("已回滚，注册表恢复原状")
                                : QStringLiteral("回滚未完全成功，需要手工清理"));

    if (residueChecked)
        parts << (residueClean ? QStringLiteral("残留检查通过") : QStringLiteral("发现残留"));

    if (!notes.isEmpty())
        parts << notes.join(QStringLiteral("；"));

    return parts.join(QStringLiteral("；"));
}

QStringList ShellIntegrationReport::lines() const
{
    QStringList result;
    result << summary();

    for (const ShellChangeRecord &record : records) {
        if (!record.succeeded)
            result << QStringLiteral("  ✗ %1").arg(record.describe());
    }
    for (const ShellChangeRecord &record : records) {
        if (record.succeeded)
            result << QStringLiteral("  ✓ %1").arg(record.describe());
    }
    for (const QString &group : failureGroups())
        result << QStringLiteral("  · %1").arg(group);
    for (const QString &line : residueLines)
        result << QStringLiteral("  · %1").arg(line);

    return result;
}

const char *shellResidueKindIdentifier(ShellResidueFinding::Kind kind)
{
    switch (kind) {
    case ShellResidueFinding::Kind::LeftoverKey:
        return "leftover_key";
    case ShellResidueFinding::Kind::LeftoverValue:
        return "leftover_value";
    case ShellResidueFinding::Kind::UnrestoredShare:
        return "unrestored_share";
    case ShellResidueFinding::Kind::ForeignEntry:
        return "foreign_entry";
    }
    return "leftover_key";
}

QString ShellResidueFinding::describe() const
{
    const QString target = name.isEmpty() ? key : (key + QLatin1Char('\\') + name);
    switch (kind) {
    case Kind::LeftoverKey:
        return QStringLiteral("残留的键：%1（%2）").arg(target, detail);
    case Kind::LeftoverValue:
        return QStringLiteral("残留的值：%1（%2）").arg(target, detail);
    case Kind::UnrestoredShare:
        return QStringLiteral("没有还原：%1（%2）").arg(target, detail);
    case Kind::ForeignEntry:
        return QStringLiteral("外来内容：%1（%2）").arg(target, detail);
    }
    return target;
}

bool ShellResidueReport::clean() const
{
    return findings.isEmpty();
}

QString ShellResidueReport::summary() const
{
    if (!planAvailable) {
        return QStringLiteral("只能做粗略检查（登记信息不完整，无法推导出完整的安装清单）");
    }
    if (clean())
        return QStringLiteral("残留检查通过：安装时写入的键与值都已清除");
    return QStringLiteral("发现 %1 处残留").arg(findings.size());
}

QStringList ShellResidueReport::lines() const
{
    QStringList result;
    result << summary();
    for (const ShellResidueFinding &finding : findings)
        result << QStringLiteral("  · %1").arg(finding.describe());
    return result;
}

QString ShellIntegrationCapability::summary() const
{
    if (available)
        return QStringLiteral("Shell 集成可用（后端：%1）").arg(backend);

    QStringList parts;
    parts << QStringLiteral("Shell 集成不可用（后端：%1）").arg(backend);
    if (!reason.isEmpty())
        parts << reason;
    if (!advice.isEmpty())
        parts << advice;
    return parts.join(QStringLiteral(" "));
}

QString ShellIntegration::InstalledState::summary() const
{
    if (!installed)
        return QStringLiteral("未安装 Shell 集成");

    QStringList parts;
    if (installing) {
        parts << QStringLiteral("**上一次安装没有完成**（登记版本停在 0）。"
                                "请先执行一次卸载把半装的内容清掉，再重新安装");
    }
    parts << QStringLiteral("已安装（记录版本 %1）").arg(version);
    parts << options.summary();
    if (!executablePath.isEmpty())
        parts << QStringLiteral("程序路径：%1").arg(executablePath);
    parts << QStringLiteral("备份项：%1 个").arg(backupCount);
    if (hasUnknownOptions) {
        parts << QStringLiteral("注意：配置里含有本版本不认识的项（%1），"
                                "可能有更新版本写入的内容不会在本版本被清理")
                         .arg(unknownOptionKeys.join(QStringLiteral("、")));
    }
    return parts.join(QStringLiteral("；"));
}

// =============================================================================
// 七、服务实现
// =============================================================================

namespace {

/// 一条备份记录：安装前某个共享键的值**原来是什么**。
///
/// 这是整个卸载能力的基础。没有它，「卸载」就只能是「把我们写过的值删掉」——
/// 而那对 `.patch` 来说是**把用户的关联删掉了**，不是还原。
struct BackupRecord
{
    QString key;
    QString name;
    RegistryValue value;
    bool existed = false;     ///< 安装前这个**值**存不存在
    bool keyExisted = false;  ///< 安装前这个**键**存不存在
    bool complete = false;    ///< 记录本身读全了没有
};

/// 备份记录在登记树下占的下标（`Backup\0`、`Backup\1`…）。
int readBackupIndices(const RegistryStore &store, const QString &backupRoot, QStringList *out)
{
    const QStringList indices = store.subKeys(backupRoot);
    if (out != nullptr)
        *out = indices;
    return indices.size();
}

bool readBackupRecord(const RegistryStore &store, const QString &backupRoot,
                      const QString &index, BackupRecord *out)
{
    BackupRecord record;
    const QString root = backupRoot + QLatin1Char('\\') + index;

    RegistryValue field;
    if (!store.value(root, QLatin1String(BackupField::Key), &field))
        return false; // 连键路径都没有，这条记录不成形
    record.key = field.string;

    if (store.value(root, QLatin1String(BackupField::Name), &field))
        record.name = field.string;

    RegistryValue existedField;
    if (store.value(root, QLatin1String(BackupField::Existed), &existedField))
        record.existed = (existedField.dword != 0);

    RegistryValue keyExistedField;
    if (store.value(root, QLatin1String(BackupField::KeyExisted), &keyExistedField))
        record.keyExisted = (keyExistedField.dword != 0);

    RegistryValue kindField;
    const QString kindText = store.value(root, QLatin1String(BackupField::Kind), &kindField)
            ? kindField.string
            : QString();

    if (record.existed) {
        if (kindText == QLatin1String(registryValueKindIdentifier(RegistryValueKind::String))) {
            RegistryValue text;
            if (store.value(root, QLatin1String(BackupField::Text), &text)) {
                record.value = RegistryValue::of(text.string);
                record.complete = true;
            }
        } else if (kindText == QLatin1String(
                           registryValueKindIdentifier(RegistryValueKind::ExpandString))) {
            RegistryValue text;
            if (store.value(root, QLatin1String(BackupField::Text), &text)) {
                record.value = RegistryValue::expandable(text.string);
                record.complete = true;
            }
        } else if (kindText == QLatin1String(
                           registryValueKindIdentifier(RegistryValueKind::DWord))) {
            RegistryValue number;
            if (store.value(root, QLatin1String(BackupField::Number), &number)) {
                record.value = RegistryValue::ofNumber(number.dword);
                record.complete = true;
            }
        } else if (kindText == QLatin1String(
                           registryValueKindIdentifier(RegistryValueKind::Unsupported))) {
            RegistryValue raw;
            RegistryValue rawType;
            if (store.value(root, QLatin1String(BackupField::Raw), &raw)
                    && store.value(root, QLatin1String(BackupField::RawType), &rawType)) {
                record.value = RegistryValue::ofRaw(rawType.dword, raw.raw);
                record.complete = true;
            }
        }
        // 认不出的 kind / 缺字段时 complete 保持 false：
        // 调用方据此报「没能还原」，而不是拿一个默认值去覆盖用户的注册表。
        // 后者比不还原更糟——用户会以为原来的东西还在。
    } else {
        // 本来就没有这个值 → 不需要写回任何东西，记录本身是完整的。
        record.complete = true;
    }

    if (out != nullptr)
        *out = record;
    return true;
}

/// 把一条备份记录写成注册表值（安装时用）。
/// 参数是**非 const** 引用：这个辅助函数要写注册表，而读写接口分得很清楚
/// （`value()` 是 const，`setValue()` 不是），所以签名必须如实反映它。
bool writeBackupRecord(RegistryStore &store, const QString &backupRoot,
                       const QString &index, const BackupRecord &record,
                       Files::ErrorCode *error)
{
    const QString root = backupRoot + QLatin1Char('\\') + index;

    if (!store.setValue(root, QLatin1String(BackupField::Key),
                        RegistryValue::of(record.key), error))
        return false;
    if (!store.setValue(root, QLatin1String(BackupField::Name),
                        RegistryValue::of(record.name), error))
        return false;
    if (!store.setValue(root, QLatin1String(BackupField::Existed),
                        RegistryValue::ofNumber(record.existed ? 1 : 0), error))
        return false;
    if (!store.setValue(root, QLatin1String(BackupField::KeyExisted),
                        RegistryValue::ofNumber(record.keyExisted ? 1 : 0), error))
        return false;
    if (!store.setValue(root, QLatin1String(BackupField::Kind),
                        RegistryValue::of(QString::fromLatin1(
                                registryValueKindIdentifier(record.value.kind))),
                        error))
        return false;

    if (record.existed) {
        switch (record.value.kind) {
        case RegistryValueKind::String:
        case RegistryValueKind::ExpandString:
            return store.setValue(root, QLatin1String(BackupField::Text),
                                  RegistryValue::of(record.value.string), error);
        case RegistryValueKind::DWord:
            return store.setValue(root, QLatin1String(BackupField::Number),
                                  RegistryValue::ofNumber(record.value.dword), error);
        case RegistryValueKind::Unsupported:
            if (!store.setValue(root, QLatin1String(BackupField::RawType),
                                RegistryValue::ofNumber(record.value.rawType), error))
                return false;
            return store.setValue(root, QLatin1String(BackupField::Raw),
                                  RegistryValue::ofRaw(record.value.rawType, record.value.raw),
                                  error);
        }
    }
    return true;
}

/// 已经存在的备份里，有没有覆盖同一个 (键, 值名) 的那一条。
///
/// **这条检查决定了「装两次再卸载」能不能还原正确。** 第二次安装时，
/// `.patch` 的当前值已经是我们的 ProgID 了；若直接把它当「原值」备份下来，
/// 卸载就会把 `.patch` 还原成我们的 ProgID——用户看到的现象是
/// 「卸载之后补丁文件还是被这个程序接管」，而报告说「已还原」。
/// 所以重复安装要沿用已有的备份，不覆盖它。
bool findExistingBackup(const RegistryStore &store, const QString &backupRoot,
                        const QString &key, const QString &name, QString *indexOut)
{
    QStringList indices;
    readBackupIndices(store, backupRoot, &indices);

    const QString wantedKey = RegistryStore::normalizeKey(key);
    const QString wantedName = RegistryStore::normalizeName(name);

    for (const QString &index : indices) {
        BackupRecord record;
        if (!readBackupRecord(store, backupRoot, index, &record))
            continue;
        if (RegistryStore::normalizeKey(record.key) == wantedKey
                && RegistryStore::normalizeName(record.name) == wantedName) {
            if (indexOut != nullptr)
                *indexOut = index;
            return true;
        }
    }
    return false;
}

} // namespace

ShellIntegration::ShellIntegration(RegistryStore *store) : m_store(store)
{
    // 刻意不接管 store 的所有权：调用方常常是「一个长期存在的存储 +
    // 若干次临时构造的服务」。若这里接管，第二次构造同一存储的服务时
    // 就会 double free——而现象是随机的崩溃，查起来与 Shell 集成毫无关系。
}

ShellIntegrationCapability ShellIntegration::capability() const
{
    ShellIntegrationCapability capability;
    capability.platformSupported = platformHasRegistry();
    capability.backend = (m_store != nullptr) ? m_store->backendName()
                                             : QStringLiteral("none");

    QString reason;
    capability.available = (m_store != nullptr) && m_store->isAvailable(&reason);
    capability.reason = reason;

    if (!capability.platformSupported) {
        // 平台本身没有注册表这一机制：原因与建议都用平台给的那一套。
        // 「置灰并说明」里的「说明」就是这两段文字（完成标准第 5 条）。
        if (capability.reason.isEmpty())
            capability.reason = platformRegistryUnsupportedReason();
        capability.advice = platformRegistryUnsupportedAdvice();
    } else if (!capability.available) {
        capability.advice = QStringLiteral("请确认程序有写入 HKEY_CURRENT_USER 的权限"
                                           "（通常不需要管理员，除非被组策略限制），"
                                           "然后重试。");
    }

    return capability;
}

ShellIntegration::InstalledState ShellIntegration::installedState() const
{
    InstalledState state;
    if (m_store == nullptr)
        return state;

    const QString root = shellBookkeepingRoot();

    RegistryValue version;
    if (!m_store->value(root, QLatin1String(MetaField::Version), &version))
        return state;

    state.installed = true;
    if (version.kind == RegistryValueKind::DWord)
        state.version = static_cast<int>(version.dword);
    state.installing = (state.version == 0);

    RegistryValue executable;
    if (m_store->value(root, QLatin1String(MetaField::Executable), &executable))
        state.executablePath = executable.string;

    RegistryValue optionsText;
    if (m_store->value(root, QLatin1String(MetaField::Options), &optionsText)) {
        shellOptionsFromString(optionsText.string, &state.options,
                               &state.unknownOptionKeys);
    }
    state.hasUnknownOptions = !state.unknownOptionKeys.isEmpty();

    state.backupCount = readBackupIndices(*m_store,
                                         joinKey(root, QStringLiteral("Backup")), nullptr);
    return state;
}

ShellIntegrationReport ShellIntegration::install(const ShellIntegrationPlan &plan)
{
    ShellIntegrationReport report;
    report.operationName = QStringLiteral("install");

    if (m_store == nullptr) {
        ShellChangeRecord record;
        record.succeeded = false;
        record.error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        record.detail = QStringLiteral("没有可用的注册表存储");
        report.records << record;
        return report;
    }

    const ShellIntegrationCapability current = capability();
    if (!current.available) {
        // 不可用时**一条都不写**，并且只报一条记录说明原因。
        // 把它铺成 78 条「不支持」既没有信息量，也会让真正的失败被淹没。
        ShellChangeRecord record;
        record.key = plan.rootKey;
        record.purpose = QStringLiteral("检查 Shell 集成是否可用");
        record.succeeded = false;
        record.error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        record.detail = current.summary();
        report.records << record;
        return report;
    }

    if (plan.isEmpty()) {
        report.notes << QStringLiteral("没有勾选任何要安装的项，注册表未改动");
        return report;
    }

    // ---- 步骤 0：已经装过时，先按**旧选项**完整拆掉 ----
    //
    // 「安装」的语义因此是「把注册表调成与当前勾选项一致」，而不是「往已有的
    // 上面再加一层」。这件事必须做对，因为用户会这样用：装好之后取消勾选
    // `.patch`，再点一次安装。若只是叠加，注册表里会留下 `.patch` 那一套，
    // 而登记（卸载的依据）写的是新选项——卸载时按新选项推导，
    // 那些旧选项独有的项不会被删，**而报告会说「残留检查通过」**。
    // 用户看到的是「卸载了但 .patch 还是被占用」，且没有任何提示告诉他为什么。
    //
    // 先拆后装还有一个额外的好处：拆的过程会把 `.patch` / `.diff` 还原成
    // 安装前的原值，于是紧接着的安装重新备份到的是**真正的原值**，
    // 而不是我们上一轮写进去的 ProgID。（重复安装时备份不会被我们的值污染，
    // 这一条同时由 findExistingBackup() 兜底。）
    const InstalledState previous = installedState();
    if (previous.installed) {
        const ShellIntegrationPlan previousPlan =
                buildShellIntegrationPlan(previous.executablePath, previous.options);
        const ShellIntegrationReport teardown =
                removeInstallation(previousPlan, QStringLiteral("install-reconfigure"));

        if (!teardown.allSucceeded()) {
            // 拆不干净就不要接着装：那会叠出一个既不是旧样子也不是新样子的
            // 注册表，而用户以为自己只是改了勾选项。
            ShellChangeRecord record;
            record.key = previousPlan.bookkeepingRoot;
            record.purpose = QStringLiteral("按旧选项拆除上一次安装");
            record.succeeded = false;
            record.error = Files::ErrorCode(Files::FileSystemError::Busy);
            record.detail = QStringLiteral("上一次安装没有拆干净，为避免叠加出一个"
                                           "既非旧样子也非新样子的注册表，本次安装已中止");
            report.records << record;
            report.notes << teardown.summary();
            report.residueChecked = true;
            const ShellResidueReport residue =
                    findResidue(buildShellIntegrationPlan(previous.executablePath,
                                                         previous.options));
            report.residueClean = residue.clean();
            report.residueLines = residue.lines();
            return report;
        }

        report.notes << QStringLiteral("检测到已安装，已先按原选项拆除（%1 步）")
                                .arg(teardown.records.size());
        if (previous.hasUnknownOptions) {
            report.notes << QStringLiteral("原登记含本版本不认识的项（%1），"
                                           "那部分可能没有拆到")
                                    .arg(previous.unknownOptionKeys.join(
                                            QStringLiteral("、")));
        }
    }

    const QString root = shellBookkeepingRoot();
    const QString backupRoot = joinKey(root, QStringLiteral("Backup"));

    // 局部函数：一次失败就回滚，并把这个事实记进报告。
    //
    // 回滚与卸载共用同一段代码（removeInstallation），理由见它的说明。
    const auto failAndRollBack = [&](const QString &key, const QString &name,
                                     const QString &purpose, const QString &detail,
                                     const Files::ErrorCode &error) {
        ShellChangeRecord record;
        record.key = key;
        record.name = name;
        record.purpose = purpose;
        record.succeeded = false;
        record.error = error;
        record.detail = detail;
        report.records << record;

        const ShellIntegrationReport rollback =
                removeInstallation(plan, QStringLiteral("install-rollback"));
        report.rolledBack = true;
        report.rollbackClean = rollback.allSucceeded();
        report.notes << QStringLiteral("安装中断，已尝试恢复原状：%1").arg(rollback.summary());
        return report;
    };

    // ---- 步骤 1：先写下安装意图 ----
    //
    // 顺序是有讲究的：版本号先写 **0**，全部写完后再改成 1。
    // 于是「版本号是 0」精确对应「有人写了一半就走了」，卸载入口据此
    // 提示用户先清理——没有这个标记时，半装状态与「完全没装」无法区分，
    // 而用户能看到的现象只是「菜单项怪怪的」。
    //
    // 选项与程序路径也要**在写菜单项之前**落盘：否则中途断电后，
    // 我们无从知道当时打算装什么，也就无法把半装的内容清理干净。
    {
        ShellChangeRecord record;
        record.key = root;
        record.purpose = QStringLiteral("登记本次安装（选项与程序路径）");

        Files::ErrorCode error;
        bool ok = m_store->setValue(root, QLatin1String(MetaField::Version),
                                    RegistryValue::ofNumber(0), &error);
        if (ok)
            ok = m_store->setValue(root, QLatin1String(MetaField::Executable),
                                   RegistryValue::of(plan.executablePath), &error);
        if (ok)
            ok = m_store->setValue(root, QLatin1String(MetaField::Options),
                                   RegistryValue::of(shellOptionsToString(plan.options)),
                                   &error);

        if (!ok) {
            record.succeeded = false;
            record.error = error;
            record.detail = describeError(error);
            report.records << record;
            // 这一步还没写任何菜单项，但已经写下的登记值要清掉，
            // 否则会留下一个「装了却没装」的登记，误导下一次安装的判断。
            const ShellIntegrationReport rollback =
                    removeInstallation(plan, QStringLiteral("install-rollback"));
            report.rolledBack = true;
            report.rollbackClean = rollback.allSucceeded();
            return report;
        }

        record.detail = QStringLiteral("已登记：%1").arg(plan.options.summary());
        report.records << record;
    }

    // ---- 步骤 2：备份共享键的原值（必须在覆盖之前） ----
    int backupIndex = readBackupIndices(*m_store, backupRoot, nullptr);
    for (const ShellEntry &entry : plan.entries) {
        if (entry.disposition != ShellEntryDisposition::Shared)
            continue;

        QString existing;
        if (findExistingBackup(*m_store, backupRoot, entry.key, entry.name, &existing)) {
            // 重复安装：沿用**最早那次**的备份。理由见 findExistingBackup 的说明。
            ShellChangeRecord record;
            record.key = entry.key;
            record.name = entry.name;
            record.purpose = QStringLiteral("沿用已有的原值备份（重复安装）");
            record.detail = QStringLiteral("原值备份仍然是备份 %1").arg(existing);
            report.records << record;
            continue;
        }

        BackupRecord backup;
        backup.key = entry.key;
        backup.name = entry.name;
        backup.keyExisted = m_store->keyExists(entry.key);
        backup.existed = m_store->value(entry.key, entry.name, &backup.value);

        ShellChangeRecord record;
        record.key = entry.key;
        record.name = entry.name;
        record.purpose = QStringLiteral("备份安装前的原值，供卸载还原");

        Files::ErrorCode error;
        if (!writeBackupRecord(*m_store, backupRoot, QString::number(backupIndex),
                               backup, &error)) {
            record.succeeded = false;
            record.error = error;
            record.detail = QStringLiteral("备份失败，为避免卸载无法还原，安装已中止：%1")
                                    .arg(describeError(error));
            report.records << record;
            const ShellIntegrationReport rollback =
                    removeInstallation(plan, QStringLiteral("install-rollback"));
            report.rolledBack = true;
            report.rollbackClean = rollback.allSucceeded();
            return report;
        }

        record.detail = backup.existed
                ? QStringLiteral("原值为 %1，已记入备份 %2")
                          .arg(backup.value.display())
                          .arg(backupIndex)
                : QStringLiteral("原先没有这个值，已记入备份 %1（卸载时删除我们写的）")
                          .arg(backupIndex);
        report.records << record;
        ++backupIndex;
    }

    // ---- 步骤 3：写入全部计划条目 ----
    for (const ShellEntry &entry : plan.entries) {
        Files::ErrorCode error;
        if (!m_store->setValue(entry.key, entry.name, entry.value, &error)) {
            return failAndRollBack(entry.key, entry.name, entry.purpose,
                                   QStringLiteral("写入失败：%1").arg(describeError(error)),
                                   error);
        }

        ShellChangeRecord record;
        record.key = entry.key;
        record.name = entry.name;
        record.purpose = entry.purpose;
        record.detail = QStringLiteral("写入 %1").arg(entry.value.display());
        report.records << record;
    }

    // ---- 步骤 4：把版本号改成 1，表示这次安装完整结束了 ----
    {
        Files::ErrorCode error;
        if (!m_store->setValue(root, QLatin1String(MetaField::Version),
                               RegistryValue::ofNumber(kBookkeepingVersion), &error)) {
            return failAndRollBack(root, QLatin1String(MetaField::Version),
                                   QStringLiteral("收尾：标记安装完成"),
                                   QStringLiteral("写入失败：%1").arg(describeError(error)),
                                   error);
        }

        ShellChangeRecord record;
        record.key = root;
        record.purpose = QStringLiteral("收尾：标记安装完成");
        record.detail = QStringLiteral("版本号改为 %1").arg(kBookkeepingVersion);
        report.records << record;
    }

    return report;
}

ShellIntegrationReport ShellIntegration::removeInstallation(const ShellIntegrationPlan &plan,
                                                            const QString &operationName)
{
    ShellIntegrationReport report;
    report.operationName = operationName;

    if (m_store == nullptr) {
        ShellChangeRecord record;
        record.succeeded = false;
        record.error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        record.detail = QStringLiteral("没有可用的注册表存储");
        report.records << record;
        return report;
    }

    const QString backupRoot = joinKey(plan.bookkeepingRoot, QStringLiteral("Backup"));

    // ---- 步骤 1：把共享键还原成安装前的样子 ----
    //
    // 顺序：先删我们写的、再写回原值。反过来的话中间会有一个瞬间
    // 该扩展名指向我们的 ProgID 而我们的键已经没了——
    // 那个瞬间若被资源管理器读到，用户会看到一个「找不到程序」的错误框。
    {
        QStringList indices;
        readBackupIndices(*m_store, backupRoot, &indices);

        for (const QString &index : indices) {
            BackupRecord backup;
            const QString recordPath = backupRoot + QLatin1Char('\\') + index;

            if (!readBackupRecord(*m_store, backupRoot, index, &backup) || !backup.complete) {
                ShellChangeRecord record;
                record.key = recordPath;
                record.purpose = QStringLiteral("还原安装前的原值");
                record.succeeded = false;
                record.error = Files::ErrorCode(Files::FileSystemError::Unknown);
                record.detail = QStringLiteral("备份记录不完整，**没有**改回原值——"
                                               "拿一个默认值去覆盖比不还原更糟");
                report.records << record;
                continue;
            }

            Files::ErrorCode error;
            bool ok = true;
            QString detail;

            if (backup.existed) {
                ok = m_store->setValue(backup.key, backup.name, backup.value, &error);
                detail = QStringLiteral("还原为 %1").arg(backup.value.display());
            } else {
                ok = m_store->removeValue(backup.key, backup.name, &error);
                detail = QStringLiteral("删除（安装前本无此值）");
            }

            // 键本来不存在 → 是我们建起来的 → 现在空了就应该删掉，
            // 否则留下一个空的 `.patch` 键：它不影响功能，但属于残留。
            if (ok && !backup.keyExisted && isKeyEmpty(*m_store, backup.key)) {
                Files::ErrorCode keyError;
                if (!m_store->removeKey(backup.key, &keyError)) {
                    ok = false;
                    error = keyError;
                } else {
                    detail += QStringLiteral("，并删掉当时由我们创建的键");
                }
            }

            ShellChangeRecord record;
            record.key = backup.key;
            record.name = backup.name;
            record.purpose = QStringLiteral("还原安装前的原值");
            record.succeeded = ok;
            record.error = error;
            record.detail = ok ? detail : QStringLiteral("还原失败：%1").arg(describeError(error));
            report.records << record;
        }
    }

    // ---- 步骤 2：删掉我们写过的值（共享键已由上面还原，跳过） ----
    //
    // 用逆序：后写的先删。注册表本身不要求这一点，但报告读起来是「倒着拆」，
    // 与安装报告正好互为镜像，出问题时容易对齐。
    for (int i = plan.entries.size() - 1; i >= 0; --i) {
        const ShellEntry &entry = plan.entries.at(i);
        if (entry.disposition == ShellEntryDisposition::Shared)
            continue;

        Files::ErrorCode error;
        const bool ok = m_store->removeValue(entry.key, entry.name, &error);

        ShellChangeRecord record;
        record.key = entry.key;
        record.name = entry.name;
        record.purpose = entry.purpose;
        record.succeeded = ok;
        record.error = error;
        record.detail = ok ? QStringLiteral("删除") : describeError(error);
        report.records << record;
    }

    // ---- 步骤 3：按深度倒序删掉我们创建的键 ----
    //
    // 每个键都**先判空**再删。这里刻意不用「递归删除」：
    // 键下若出现了不是我们写的东西（别的程序、或者用户手工加的），
    // 递归删会把它一起删掉，而那是别人的数据。
    // 判断不出来时就留下并报告——卸载报告说「有一处没清掉」
    // 远好过静默删掉用户的东西。
    const QStringList ownedKeys = sortDeepestFirst(plan.ownedKeys);
    for (const QString &key : ownedKeys) {
        if (!m_store->keyExists(key))
            continue;

        const QStringList names = m_store->valueNames(key);
        const QStringList children = m_store->subKeys(key);

        // 我们计划里的值应该已经在上一步删掉了。这里再扫一遍是为了处理
        // 「半装状态」：那时可能有计划里有、但当时写失败的项留下的值。
        for (const QString &name : names) {
            bool planned = false;
            const QVector<ShellEntry> plannedEntries = plan.entriesFor(key);
            for (const ShellEntry &entry : plannedEntries) {
                if (RegistryStore::normalizeName(entry.name)
                        == RegistryStore::normalizeName(name)) {
                    planned = true;
                    break;
                }
            }
            if (!planned)
                continue; // 不是我们写的，留给残留检查去报

            Files::ErrorCode error;
            m_store->removeValue(key, name, &error);
        }

        bool hasForeignValue = false;
        const QStringList remainingNames = m_store->valueNames(key);
        for (const QString &name : remainingNames) {
            const QVector<ShellEntry> plannedEntries = plan.entriesFor(key);
            bool planned = false;
            for (const ShellEntry &entry : plannedEntries) {
                if (RegistryStore::normalizeName(entry.name)
                        == RegistryStore::normalizeName(name)) {
                    planned = true;
                    break;
                }
            }
            if (!planned)
                hasForeignValue = true;
        }

        bool hasForeignChild = false;
        for (const QString &child : children) {
            const QString childKey = joinKey(key, child);
            bool planned = false;
            for (const QString &owned : plan.ownedKeys) {
                if (RegistryStore::normalizeKey(owned)
                        == RegistryStore::normalizeKey(childKey)) {
                    planned = true;
                    break;
                }
            }
            if (!planned)
                hasForeignChild = true;
        }

        if (hasForeignValue || hasForeignChild) {
            ShellChangeRecord record;
            record.key = key;
            record.purpose = QStringLiteral("删除由我们创建的键");
            record.succeeded = false;
            // 分类取 AlreadyExists 而不是 Busy：Busy 的处置建议是「关掉占用它的
            // 程序」，而这里根本没有占用者——真正的情况是「键下已经有一些
            // 不是我们写的内容，需要你来决定怎么办」。给错建议比不给更误导。
            record.error = Files::ErrorCode(Files::FileSystemError::AlreadyExists);
            record.detail = QStringLiteral("键下还有不是我们写的内容，未删除（留给残留检查报告）");
            report.records << record;
            continue;
        }

        if (!isKeyEmpty(*m_store, key))
            continue; // 子键还没删完（更深的会先被处理，这里只是兜底）

        Files::ErrorCode error;
        const bool ok = m_store->removeKey(key, &error);
        if (!ok) {
            ShellChangeRecord record;
            record.key = key;
            record.purpose = QStringLiteral("删除由我们创建的键");
            record.succeeded = false;
            record.error = error;
            record.detail = QStringLiteral("删除失败：%1").arg(describeError(error));
            report.records << record;
        }
    }

    // ---- 步骤 4：递归删掉登记子树 ----
    //
    // 这是本文件里唯一允许递归删除的地方：`Software\LqCompare\ShellIntegration`
    // 这棵子树里的每一个键与值**按定义**都是本程序写的（含运行时才知道下标的
    // `Backup\<n>`）。逐个判空反而会漏——那些下标键不在静态计划里，
    // 会被上面的「外来内容」检查误判成别人的东西而拒绝删除，
    // 于是每次卸载都留下一份备份。（这正是第一版实现里的真实缺陷。）
    if (m_store->keyExists(plan.bookkeepingRoot)) {
        Files::ErrorCode error;
        ShellChangeRecord record;
        record.key = plan.bookkeepingRoot;
        record.purpose = QStringLiteral("删除安装登记（整棵子树都是本程序写的）");
        if (m_store->removeKey(plan.bookkeepingRoot, &error)) {
            record.detail = QStringLiteral("已删除");
        } else {
            record.succeeded = false;
            record.error = error;
            record.detail = QStringLiteral("删除失败：%1").arg(describeError(error));
        }
        report.records << record;
    }

    return report;
}

ShellIntegrationReport ShellIntegration::uninstall()
{
    ShellIntegrationReport report;
    report.operationName = QStringLiteral("uninstall");

    if (m_store == nullptr) {
        ShellChangeRecord record;
        record.succeeded = false;
        record.error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        record.detail = QStringLiteral("没有可用的注册表存储");
        report.records << record;
        return report;
    }

    const InstalledState state = installedState();
    if (!state.installed) {
        // 幂等：没装过不是错误。用户点两次卸载不该看到一条红色失败。
        report.notes << QStringLiteral("没有找到安装记录，注册表未改动");
        report.residueChecked = true;
        report.residueClean = true;
        return report;
    }

    if (state.installing) {
        report.notes << QStringLiteral("登记显示上一次安装没有完成，"
                                       "本次卸载会把半装的内容一并清掉");
    }

    // 用**记录下来的**选项与程序路径推导计划，而不是调用方当前的选项。
    // 理由见 shellOptionsToString 的说明：按当前选项推导会漏删被关掉过的项。
    const ShellIntegrationPlan plan =
            buildShellIntegrationPlan(state.executablePath, state.options);

    const ShellIntegrationReport removal = removeInstallation(plan, report.operationName);
    report.records = removal.records;

    // 登记子树已经由 removeInstallation 的最后一步递归删掉，
    // 这里刻意**不**再删一遍：同一件事两份实现迟早分歧，
    // 而分歧的表现是「回滚留下的登记被卸载清掉了、卸载留下的没有」。

    // 卸载必然顺带查一次残留。完成标准第 4 条写的是「卸载后注册表无残留
    // （有校验）」——校验不是可选项，所以不留给调用方记得调。
    const ShellResidueReport residue = findResidue(plan);
    report.residueChecked = true;
    report.residueClean = residue.clean();
    report.residueLines = residue.lines();

    return report;
}

ShellIntegrationReport ShellIntegration::verify(const ShellIntegrationPlan &plan) const
{
    ShellIntegrationReport report;
    report.operationName = QStringLiteral("verify");

    if (m_store == nullptr) {
        ShellChangeRecord record;
        record.succeeded = false;
        record.error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        record.detail = QStringLiteral("没有可用的注册表存储");
        report.records << record;
        return report;
    }

    for (const ShellEntry &entry : plan.entries) {
        ShellChangeRecord record;
        record.key = entry.key;
        record.name = entry.name;
        record.purpose = entry.purpose;

        RegistryValue actual;
        if (!m_store->value(entry.key, entry.name, &actual)) {
            record.succeeded = false;
            record.error = Files::ErrorCode(Files::FileSystemError::NotFound);
            record.detail = QStringLiteral("缺少这一项（期望 %1）").arg(entry.value.display());
        } else if (!(actual == entry.value)) {
            // 「值不对」用 AlreadyExists 这个分类：它的含义是「已经有了、
            // 但不是我们想要的那个」，处置与「缺失」不同（前者要改、后者要补），
            // 而 Files::FileSystemError 里没有更贴切的一档。
            record.succeeded = false;
            record.error = Files::ErrorCode(Files::FileSystemError::AlreadyExists);
            record.detail = QStringLiteral("现在的值是 %1，期望 %2")
                                    .arg(actual.display(), entry.value.display());
        } else {
            record.detail = QStringLiteral("本来就是对的");
        }

        report.records << record;
    }

    // 登记信息也要在。它不在时「装了的菜单项」仍然能工作，
    // 但卸载会失去「当时装了什么」的依据 —— 校验必须把这件事报出来。
    {
        const InstalledState state = installedState();
        ShellChangeRecord record;
        record.key = shellBookkeepingRoot();
        record.purpose = QStringLiteral("安装登记");
        if (!state.installed) {
            record.succeeded = false;
            record.error = Files::ErrorCode(Files::FileSystemError::NotFound);
            record.detail = QStringLiteral("缺少安装登记，卸载将无法还原被覆盖的原值");
        } else if (state.installing) {
            record.succeeded = false;
            record.error = Files::ErrorCode(Files::FileSystemError::Unknown);
            record.detail = QStringLiteral("登记显示上一次安装没有完成（版本号停在 0）");
        } else {
            record.detail = QStringLiteral("版本 %1，程序路径 %2")
                                    .arg(state.version)
                                    .arg(state.executablePath);
        }
        report.records << record;
    }

    return report;
}

ShellResidueReport ShellIntegration::findResidue(const ShellIntegrationPlan &plan) const
{
    ShellResidueReport report;

    if (m_store == nullptr)
        return report;

    const QString backupRoot = joinKey(plan.bookkeepingRoot, QStringLiteral("Backup"));

    // ---- 1. 我们写过的值还在不在 ----
    for (const ShellEntry &entry : plan.entries) {
        RegistryValue actual;
        if (!m_store->value(entry.key, entry.name, &actual))
            continue;

        if (entry.disposition == ShellEntryDisposition::Shared) {
            // 共享键里「值还在」不等于残留：卸载会把原值写回去，
            // 那时值当然还在——只是内容变回了原样。
            // 只有当它仍然**等于我们写的那个值**时才算残留。
            if (!(actual == entry.value))
                continue;
        }

        ShellResidueFinding finding;
        finding.kind = ShellResidueFinding::Kind::LeftoverValue;
        finding.key = entry.key;
        finding.name = entry.name;
        finding.detail = QStringLiteral("现在的值是 %1").arg(actual.display());
        if (entry.disposition == ShellEntryDisposition::Shared) {
            finding.detail += QStringLiteral("，与本程序写入的值相同——"
                                             "原值没有被还原回去");
        }
        report.findings << finding;
    }

    // ---- 2. 我们创建的键还在不在 ----
    if (m_store->keyExists(plan.bookkeepingRoot)) {
        ShellResidueFinding finding;
        finding.kind = ShellResidueFinding::Kind::LeftoverKey;
        finding.key = plan.bookkeepingRoot;
        finding.detail = QStringLiteral("安装登记还在");
        report.findings << finding;
    }

    for (const QString &key : sortDeepestFirst(plan.ownedKeys)) {
        if (!m_store->keyExists(key))
            continue;

        ShellResidueFinding finding;
        finding.kind = ShellResidueFinding::Kind::LeftoverKey;
        finding.key = key;
        finding.detail = isKeyEmpty(*m_store, key)
                ? QStringLiteral("空键，可以直接删掉")
                : QStringLiteral("键下还有内容（值是 %1 个、子键 %2 个）")
                          .arg(m_store->valueNames(key).size())
                          .arg(m_store->subKeys(key).size());
        report.findings << finding;
    }

    // ---- 3. 备份记录还在不在（说明还原这一步没跑完） ----
    {
        QStringList indices;
        readBackupIndices(*m_store, backupRoot, &indices);
        for (const QString &index : indices) {
            BackupRecord backup;
            const bool readable = readBackupRecord(*m_store, backupRoot, index, &backup);

            ShellResidueFinding finding;
            finding.kind = ShellResidueFinding::Kind::UnrestoredShare;
            finding.key = readable ? backup.key : (backupRoot + QLatin1Char('\\') + index);
            finding.name = readable ? backup.name : QString();
            finding.detail = readable
                    ? QStringLiteral("还原记录还在，说明没走完还原流程")
                    : QStringLiteral("备份记录本身不可读");
            report.findings << finding;
        }
    }

    // ---- 4. 我们的键下有没有不是我们写的东西 ----
    //
    // 这一项与前三项方向相反：前三项是「我们留下的」，这一项是「别人放进去的」。
    // 它的用处是解释「为什么某个键删不掉」——没有它，报告只会说
    // 「这个键还在」，用户会以为是我们没删干净，于是手工去删别人的东西。
    for (const QString &key : plan.ownedKeys) {
        if (!m_store->keyExists(key))
            continue;

        const QVector<ShellEntry> planned = plan.entriesFor(key);

        const QStringList names = m_store->valueNames(key);
        for (const QString &name : names) {
            bool known = false;
            for (const ShellEntry &entry : planned) {
                if (RegistryStore::normalizeName(entry.name)
                        == RegistryStore::normalizeName(name)) {
                    known = true;
                    break;
                }
            }
            if (known)
                continue;

            ShellResidueFinding finding;
            finding.kind = ShellResidueFinding::Kind::ForeignEntry;
            finding.key = key;
            finding.name = name;
            finding.detail = QStringLiteral("不是本程序写入的值");
            report.findings << finding;
        }

        const QStringList children = m_store->subKeys(key);
        for (const QString &child : children) {
            const QString childKey = joinKey(key, child);
            bool known = false;
            for (const QString &owned : plan.ownedKeys) {
                if (RegistryStore::normalizeKey(owned)
                        == RegistryStore::normalizeKey(childKey)) {
                    known = true;
                    break;
                }
            }
            if (known)
                continue;

            ShellResidueFinding finding;
            finding.kind = ShellResidueFinding::Kind::ForeignEntry;
            finding.key = childKey;
            finding.detail = QStringLiteral("不是本程序创建的子键");
            report.findings << finding;
        }
    }

    return report;
}

ShellResidueReport ShellIntegration::findResidue() const
{
    if (m_store == nullptr)
        return ShellResidueReport();

    const InstalledState state = installedState();
    if (!state.installed) {
        // 没有登记信息 —— 可能是从没装过（那没什么可查的），
        // 也可能是登记被删掉了而菜单项还在。两者的区分只能靠
        // 「登记键下面有没有东西」这个间接证据，不足以推导出完整计划，
        // 因此如实说明「只能粗查」而不是假装查干净了。
        ShellResidueReport report;
        const bool rootExists = m_store->keyExists(shellBookkeepingRoot());
        report.planAvailable = false;
        if (rootExists) {
            ShellResidueFinding finding;
            finding.kind = ShellResidueFinding::Kind::LeftoverKey;
            finding.key = shellBookkeepingRoot();
            finding.detail = QStringLiteral("登记键存在但没有版本号，可能是上次安装被中断");
            report.findings << finding;
        }
        return report;
    }

    ShellResidueReport report =
            findResidue(buildShellIntegrationPlan(state.executablePath, state.options));
    report.planAvailable = !state.hasUnknownOptions;
    return report;
}

ShellIntegrationReport ShellIntegration::previewInstall(const ShellIntegrationPlan &plan,
                                                       MemoryRegistryStore *scratch) const
{
    ShellIntegrationReport report;
    report.operationName = QStringLiteral("preview");

    if (scratch == nullptr || m_store == nullptr) {
        ShellChangeRecord record;
        record.succeeded = false;
        record.error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        record.detail = QStringLiteral("预演需要同时提供真实存储与一个空的临时存储");
        report.records << record;
        return report;
    }

    // 把真实存储里「这次安装会碰到的东西」复制进临时存储。
    //
    // 只复制这几处、而不是整棵 HKCU\Software：后者在装了软件的机器上
    // 有几万条，预演会卡住，而且完全没必要——安装只会碰这些键。
    scratch->clearInjectedFailures();

    for (const QString &key : plan.backupKeys()) {
        const QStringList names = m_store->valueNames(key);
        for (const QString &name : names) {
            RegistryValue value;
            if (m_store->value(key, name, &value))
                scratch->setValue(key, name, value);
        }
        // 键存在但一个值都没有时也要带过来：卸载敢不敢删这个键取决于
        // 「它是不是我们建的」，而那正是 keyExists() 回答的问题。
        if (names.isEmpty() && m_store->keyExists(key))
            scratch->touchKey(key);
    }

    // 登记树整棵带过来（重复安装时要用到已有的备份记录）。
    scratch->seedFrom(*m_store, plan.bookkeepingRoot);

    ShellIntegration preview(scratch);
    ShellIntegrationReport result = preview.install(plan);
    result.operationName = QStringLiteral("preview");
    result.notes.prepend(QStringLiteral("预演：注册表未改动。报告里的「成功」表示"
                                        "在真实注册表上也会成功"));
    return result;
}

// =============================================================================
// 八、本程序收到 Shell 调用之后
// =============================================================================

QString ShellInvocation::describe() const
{
    if (kind == ShellInvocationKind::NotShellInvocation)
        return QStringLiteral("不是 Shell 集成发起的调用");

    if (kind == ShellInvocationKind::Invalid) {
        return QStringLiteral("Shell 调用参数有问题：%1").arg(problem);
    }

    return QStringLiteral("Shell 调用 %1，%2 个路径：%3")
            .arg(QLatin1String(shellActionIdentifier(action)))
            .arg(paths.size())
            .arg(paths.join(QStringLiteral(" | ")));
}

ShellInvocation parseShellInvocation(const QStringList &arguments)
{
    ShellInvocation result;

    const QString equalForm = QStringLiteral("%1=").arg(QLatin1String(ShellActionOptionName));
    const QString bareForm = QLatin1String(ShellActionOptionName);

    QString actionText;
    bool sawActionOption = false;
    QStringList paths;

    for (int i = 0; i < arguments.size(); ++i) {
        // 第 0 个是程序自身的路径，不是参数。
        if (i == 0)
            continue;

        const QString argument = arguments.at(i);

        if (argument.startsWith(equalForm)) {
            if (sawActionOption) {
                result.kind = ShellInvocationKind::Invalid;
                result.problem = QStringLiteral("命令行里出现了多个动作开关");
                return result;
            }
            sawActionOption = true;
            actionText = argument.mid(equalForm.size());
            continue;
        }

        if (argument == bareForm) {
            // 也接受 `--shell-action compare` 这种写法：手工在命令行里试的时候
            // 比 `=` 形式好敲，而且测试与文档里更清楚。
            if (sawActionOption) {
                result.kind = ShellInvocationKind::Invalid;
                result.problem = QStringLiteral("命令行里出现了多个动作开关");
                return result;
            }
            sawActionOption = true;
            if (i + 1 >= arguments.size()) {
                result.kind = ShellInvocationKind::Invalid;
                result.problem = QStringLiteral("动作开关后面没有跟动作名");
                return result;
            }
            actionText = arguments.at(++i);
            continue;
        }

        paths << argument;
    }

    if (!sawActionOption)
        return result; // NotShellInvocation：不关本模块的事

    ShellAction action = ShellAction::Compare;
    if (!shellActionFromIdentifier(actionText, &action)) {
        result.kind = ShellInvocationKind::Invalid;
        result.problem = QStringLiteral("不认识的动作「%1」").arg(actionText);
        return result;
    }
    result.action = action;
    result.paths = paths;

    const int required = shellActionRequiredPathCount(action);
    if (paths.size() < required) {
        result.kind = ShellInvocationKind::Invalid;
        result.problem = (required > 1)
                ? QStringLiteral("「%1」需要选中 %2 个条目，现在只收到 %3 个。"
                                 "请同时选中要比较的两个条目再右键。")
                          .arg(shellActionMenuText(action))
                          .arg(required)
                          .arg(paths.size())
                : QStringLiteral("「%1」需要一个条目，现在一个都没有收到。")
                          .arg(shellActionMenuText(action));
        return result;
    }

    const int maximum = shellActionMaximumPathCount(action);
    if (maximum >= 0 && paths.size() > maximum) {
        // 只取前若干个并告知，而不是整条拒绝：用户可能真的只多选了鼠标滑过的一个，
        // 直接报错会让他以为功能坏了。
        result.paths = paths.mid(0, maximum);
        result.kind = ShellInvocationKind::Valid;
        result.problem = QStringLiteral("多选了 %1 个条目，只用了前 %2 个")
                                 .arg(paths.size())
                                 .arg(maximum);
        return result;
    }

    // 左右两侧是同一个条目时比较没有意义，而且会得出「完全相同」这个
    // **错误但看起来合理**的结论。这类结果最危险，所以在这里就拦住。
    if (action == ShellAction::Compare && paths.size() >= 2
            && paths.at(0) == paths.at(1)) {
        result.kind = ShellInvocationKind::Invalid;
        result.problem = QStringLiteral("选中的两个条目是同一个：%1").arg(paths.at(0));
        return result;
    }

    result.kind = ShellInvocationKind::Valid;
    return result;
}

} // namespace Platform
} // namespace LqCompare
