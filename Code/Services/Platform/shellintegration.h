#ifndef LQCOMPARE_SHELLINTEGRATION_H
#define LQCOMPARE_SHELLINTEGRATION_H

#include "Files/filesystem.h"
#include "registrystore.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Platform {

// =============================================================================
// 一、菜单动作
// =============================================================================

///
/// \brief Shell 集成提供的动作（PRD: PLAT-005 完成标准第 1、2、3 条）。
///
/// 为什么动作要单独成枚举，而不是在生成注册表项时用字符串散在各处
/// ----------------------------------------------------------
/// 一个动作在**四个地方**同时出现：注册表里的键名、菜单上显示的文字、
/// 命令行里传给本程序的动作名、以及本程序收到之后该做什么。
/// 四处各写一遍字符串，改动时漏掉一处不会有任何报错——
/// 典型症状是「菜单项在，点下去程序启动后什么都不做」，
/// 而程序看起来完全正常。
///
/// 收成一个枚举之后，漏改一处会变成编译错误（switch 少一个分支）。
///
enum class ShellAction {
    Compare = 0,        ///< 「比较」：选中两个及以上条目，直接比较
    CompareWith,        ///< 「与…比较」：记住选中项为左侧，等用户再选一个（两步式第一步）
    CompareAsLeft,      ///< 「作为左侧比较」：标记为左侧，并让本程序去要第二个
    CompareAsRight,     ///< 「作为右侧比较」：标记为右侧，已有左侧则直接比较
    CompareSecondStep,  ///< 「与已选中的左侧比较」：两步式的第二步（占位菜单项）
    OpenAssociation,    ///< 双击 .patch / .diff 时打开（文件关联，非菜单项）
};

/// 按注册顺序列出全部动作。用于测试与自检「每个动作都被处理到了」。
QVector<ShellAction> allShellActions();

/// 稳定的机器可读标识（"compare" / "compare_with" …）。也是命令行参数值与注册表键名。
const char *shellActionIdentifier(ShellAction action);

/// 从标识解析回来。认不出时返回 false，`out` 不被修改。
bool shellActionFromIdentifier(const QString &identifier, ShellAction *out);

/// 菜单上显示的文字（含 `&` 加速键）。
QString shellActionMenuText(ShellAction action);

/// 注册表里 `<class>\shell\` 下面那一段键名（如 `LqCompare.compare`）。
QString shellActionRegistryName(ShellAction action);

///
/// \brief 这个动作最少要求几个路径。
///
/// 为什么这件事必须显式写出来
/// ------------------------
/// 资源管理器**不会**告诉我们用户选了几个。`%1` 会被替换成选中的那些条目
/// （各自带引号，空格分隔），因此「比较」拿到一个路径时，只可能是用户只选了
/// 一个文件就点了菜单——此时应该提示「请选中两个文件」，而不是拿同一个文件
/// 和它自己比较（那会给出一个「完全相同」的结论，看起来还挺正常）。
///
int shellActionRequiredPathCount(ShellAction action);

/// 这个动作最多接受几个路径。`-1` 表示不限。
int shellActionMaximumPathCount(ShellAction action);

/// 是否是两步式里的「占位菜单项」（PLAT-005 完成标准第 2 条）。
bool shellActionIsPlaceholder(ShellAction action);

/// 是否只是文件关联用的动作（不产生右键菜单项）。
bool shellActionIsAssociationOnly(ShellAction action);

/// 面向用户的一句话说明：这个菜单项会做什么。
QString shellActionDescription(ShellAction action);


// =============================================================================
// 二、菜单项挂在哪一类对象上
// =============================================================================

///
/// \brief 菜单项挂在哪一类对象上（PRD: PLAT-005 完成标准第 1 条）。
///
/// 资源管理器把「选中文件」「选中文件夹」「在文件夹空白处右键」当成三类不同
/// 的对象，各自的注册表位置也不同。只注册文件那一类的话，用户在一个文件夹上
/// 右键时看不到任何菜单项——而「比较两个文件夹」恰恰是本工具的主场。
///
enum class ShellTarget {
    Files = 0,   ///< `Software\Classes\*`
    Directories, ///< `Software\Classes\Directory`
    Background,  ///< `Software\Classes\Directory\Background`（空白处）
};

QVector<ShellTarget> allShellTargets();
const char *shellTargetIdentifier(ShellTarget target);
QString shellTargetClassKey(ShellTarget target);
QString shellTargetDescription(ShellTarget target);

///
/// \brief 某一类对象上应该出现哪些动作。
///
/// 规则里唯一需要解释的是「空白处没有『比较』」：那里一个条目都没选中，
/// 让菜单里出现一个必然报错的项只会让用户以为自己用错了。
/// 而「作为左侧比较」在空白处**正好**是该出现的位置——文件夹间的比较
/// 通常就是这样开始的。
///
QVector<ShellAction> shellActionsForTarget(ShellTarget target);

// =============================================================================
// 三、安装选项
// =============================================================================

///
/// \brief Shell 集成的可选内容（PLAT-005 完成标准第 3 条：可单独关闭）。
///
/// 每一项都对应界面上**一个独立的勾选框**，而不是一个「全部安装」开关。
/// 理由是这些项的风险与可接受度差别很大：右键菜单项是「多两行字」，
/// 而接管 `.patch`/`.diff` 关联会改变用户双击补丁文件的默认行为。
/// 把两者绑在一起，用户就只能全要或全不要。
///
struct ShellIntegrationOptions
{
    /// 右键菜单项（标准第 1 条）。关掉之后不影响文件关联。
    bool contextMenu = true;

    /// 两步式交互的菜单项（标准第 2 条，含「与…比较」与占位项）。
    bool twoStepCompare = true;

    /// 注册 `.patch` 关联（标准第 3 条）。
    bool patchAssociation = true;

    /// 注册 `.diff` 关联（标准第 3 条）。
    bool diffAssociation = true;

    /// 菜单项带图标。关掉之后菜单里只有文字——
    /// 在图标索引没配好（见 buildPlan 里关于 `Icon` 的说明）时，
    /// 关掉它比显示一个空白占位更整齐。
    bool menuIcon = true;

    /// 把菜单项排在资源管理器内置项**之前**。
    /// 只影响顺序，不影响功能；默认打开是因为内置的「打开方式」子菜单很长，
    /// 排在它后面等于要用户滚一遍。
    bool positionAtTop = true;

    bool operator==(const ShellIntegrationOptions &other) const;
    bool operator!=(const ShellIntegrationOptions &other) const { return !(*this == other); }

    /// 至少有一项需要写入时才值得安装。
    bool anyEnabled() const;

    /// 人类可读的一行，用于确认对话框与日志。
    QString summary() const;
};

///
/// \brief 当前选项下，这个动作该不该被安装。
///
/// 为什么提成一个函数：这条规则至少有两个使用者——生成计划的
/// `buildShellIntegrationPlan()` 与给用户看的 `ShellIntegrationPlan::detailLines()`。
/// 各写一遍时两处必然分歧，而分歧的表现是**预览对话框里列的项
/// 与实际装进去的不一致**：用户按预览做的判断从此不可信，
/// 而这种不一致不会有任何报错。
///
bool shellActionIncludedByOptions(ShellAction action, const ShellIntegrationOptions &options);

///
/// \brief 选项的持久化形式（写进登记键，卸载时靠它还原成同一套计划）。
///
/// 为什么卸载不能直接用「当前的选项」
/// ------------------------------
/// 用户可能装了「右键菜单 + .patch」，之后把 `.patch` 的勾去掉再点一次安装，
/// 也可能直接点卸载。若卸载按「当前选项」推导该删什么，那些被关掉过的项
/// 就会留在注册表里——这正是「卸载后注册表无残留」最常见的失败方式。
/// 所以安装时把实际生效的选项记下来，卸载按记录执行。
///
QString shellOptionsToString(const ShellIntegrationOptions &options);

/// 解析。成功时返回 true。
/// `unknownKeys` 非空时，把认不出的键名（将来版本新增的）收集进去——
/// 旧版本程序遇到新版本写下的选项时，必须知道「有我没见过的东西」，
/// 而不是悄悄用默认值继续，那会漏删。
bool shellOptionsFromString(const QString &text, ShellIntegrationOptions *out,
                            QStringList *unknownKeys = nullptr);

// =============================================================================
// 四、命令行
// =============================================================================

///
/// \brief 按 Windows 的规则把一个参数加引号（PRD: PLAT-005 完成标准第 1 条）。
///
/// 两条规则，都很容易写错，且错了之后症状极难定位：
///
/// 1. **总是加引号**，即使里面没有空格。
///    `C:\Program Files\...` 不加引号时会被拆成两个参数（这是经典的
///    无引号服务路径提权漏洞的成因）。而「没有空格就不加」这种优化
///    会在用户把程序装到 `C:\My Tools\` 之后失效。
///
/// 2. **结尾的反斜杠要翻倍**。
///    `"C:\dir\"` 里的 `\"` 被解析成「一个字面引号」，
///    于是引号没有闭合、后面的参数全部粘进路径里。
///    `"C:\dir\\"` 才是「`C:\dir\`」。
///    这条规则的完整形式是：反斜杠数 2n 时输出 n 个并把引号当分隔符，
///    2n+1 时输出 n 个加一个字面引号——所以这里只处理结尾的那一串。
///
QString quoteShellArgument(const QString &text);

///
/// \brief 拼出注册表里 `shell\command` 的默认值。
///
/// `placeholders` 是资源管理器要替换的东西（通常就是 `"%1"`）：
/// 传 `QStringList{ShellPlaceholder::SingleItem}`。
///
namespace ShellPlaceholder {
/// `%1`：选中的条目。多选时资源管理器会替换成**每一个**条目（各自带引号）。
extern const char *const SingleItem;
/// `%V`：调用动词时作用在哪个条目上。在「空白处右键」时它是那个文件夹。
/// 本项目的菜单项不用它，但写在这里是为了让下一个人知道有这么个东西、
/// 以及为什么我们没用——`%V` 对文件夹会带上结尾的反斜杠，
/// 而结尾反斜杠正是上面那条引号规则最容易出错的地方。
extern const char *const TargetItem;
} // namespace ShellPlaceholder

QString buildShellCommandLine(const QString &executablePath, ShellAction action,
                              const QStringList &placeholders);

/// 命令行里那个选择动作的开关名（`--shell-action`）。
extern const char *const ShellActionOptionName;

/// 把命令行拼成完整形式：`"exe" --shell-action=compare "%1"`。
QString buildShellInvocationLine(const QString &executablePath, ShellAction action);

// =============================================================================
// 五、计划
// =============================================================================

///
/// \brief 一条注册表写入是「我们的」还是「可能属于别人的」（PRD: PLAT-005 完成标准第 4 条）。
///
/// 这是整个 Shell 集成里最要紧的一个区分，它决定了卸载时敢不敢删。
///
///   - `Owned`：键是我们创建的，值也是我们写的。卸载时删值；键空了就删键。
///   - `Shared`：键**可能早就在**（`.patch` / `.diff` 这两个键几乎一定
///     属于用户的某个程序，或属于 Windows 自己）。我们只借用它的默认值，
///     因此安装前必须把旧值记下来，卸载时**还原**，而不是删掉。
///
/// 把 `.patch` 当 `Owned` 处理的后果是：卸载时把整个 `Software\Classes\.patch`
/// 删掉，用户原有的关联（可能是记事本、可能是别的工具）一起消失。
/// 而用户不会把「双击 .patch 没反应了」联想到几天前装过的一个比较工具。
///
enum class ShellEntryDisposition {
    Owned = 0,
    Shared,
};

const char *shellEntryDispositionIdentifier(ShellEntryDisposition disposition);

/// 一条注册表写入。
struct ShellEntry
{
    /// 相对存储根的键路径（例如 `Software\Classes\*\shell\LqCompare.compare`）。
    QString key;

    /// 值名。空串表示默认值 `(Default)`。
    QString name;

    RegistryValue value;

    ShellEntryDisposition disposition = ShellEntryDisposition::Owned;

    /// 这一条为什么存在 / 对应哪个菜单项。会出现在安装报告与校验报告里——
    /// 报告只有「键路径」时，用户看不懂哪一条对应界面上的哪个选项。
    QString purpose;

    /// 一行可读描述。
    QString describe() const;
};

///
/// \brief 一次安装要写的一切。
///
/// 计划是**纯数据**：给定程序路径与选项就能算出来，不碰注册表。
/// 于是「装了什么」「该卸什么」「现在对不对」这三件事共用一个事实来源——
/// 各算一遍必然分歧，而分歧的表现是卸载删不干净或删过头。
///
struct ShellIntegrationPlan
{
    /// 存储根（本项目的约定是 `HKEY_CURRENT_USER`，全在用户级，
    /// 不需要管理员权限、也不会影响其它用户）。
    QString rootKey;

    QString executablePath;
    ShellIntegrationOptions options;

    /// 按写入顺序排列。
    QVector<ShellEntry> entries;

    /// 我们**创建**的键（卸载时若空了就删掉）。不含 `.patch`/`.diff` 这类共享键。
    QStringList ownedKeys;

    /// 可能属于别人的键（`Shared` 条目所在的键）。
    QStringList sharedKeys;

    /// 登记信息的根键。卸载时靠它找回「当时装了什么」。
    QString bookkeepingRoot;

    QStringList progIds;
    QStringList extensions;

    bool isEmpty() const;
    int entryCount() const;
    int valueCount() const;

    /// 去重后的键清单（按深度、字典序排列，便于报告展示）。
    QStringList keys() const;

    /// 某个键下的全部条目。
    QVector<ShellEntry> entriesFor(const QString &key) const;

    /// 需要备份的键（`Shared` 条目所在的键）。
    QStringList backupKeys() const;

    /// 一份给用户看的说明：「将写入 N 个值到 M 个键，其中 K 个键可能已有内容」。
    QString summary() const;

    /// 逐条列出会发生什么，用于安装前的确认对话框。
    QStringList detailLines() const;
};

/// 登记信息的键路径（不含存储根）。
QString shellBookkeepingRoot();
int shellBookkeepingVersion();

/// 生成计划。`executablePath` 必须是本程序自身的完整路径。
ShellIntegrationPlan buildShellIntegrationPlan(const QString &executablePath,
                                               const ShellIntegrationOptions &options);

// =============================================================================
// 六、执行结果
// =============================================================================

/// 一次写操作的记录。
struct ShellChangeRecord
{
    QString key;
    QString name;
    QString purpose;

    bool succeeded = true;
    Files::ErrorCode error;

    /// 实际做了什么：「写入」「还原为 …」「删除」「本来就是对的，跳过」。
    /// 报告里只有「成功/失败」两列时，用户没法判断该不该重试。
    QString detail;

    QString describe() const;
};

///
/// \brief 安装 / 卸载 / 校验的汇总报告。
///
struct ShellIntegrationReport
{
    /// "install" / "uninstall" / "verify" / "preview"
    QString operationName;

    QVector<ShellChangeRecord> records;

    /// 安装中途失败时是否已经回滚干净。见 ShellIntegration::install 的说明。
    bool rolledBack = false;

    /// 回滚本身是否完全成功。只有 `rolledBack` 为真时才有意义。
    bool rollbackClean = true;

    /// 卸载后自动做的那次残留检查（只在卸载时填）。
    /// 做成「卸载必然顺带查一次」而不是让调用方自己记得调——
    /// 「卸载后注册表无残留（有校验）」这条完成标准里，
    /// 校验不是可选项。
    bool residueChecked = false;
    bool residueClean = true;
    QStringList residueLines;

    /// 未安装时执行卸载，或本来就装好时执行安装：不算失败，但要说明。
    QStringList notes;

    bool allSucceeded() const;
    bool ok() const;
    int succeededCount() const;
    int failedCount() const;
    QStringList failedKeys() const;

    /// 失败原因按分类归并（与 PLAT-008 的失败清单同一个思路）。
    QStringList failureGroups() const;

    QString summary() const;
    QStringList lines() const;
};

/// 一条残留发现。
struct ShellResidueFinding
{
    enum class Kind {
        LeftoverKey = 0,   ///< 我们创建的键还在
        LeftoverValue,     ///< 我们写过的值还在
        UnrestoredShare,   ///< 备份了但没有还原回原处
        ForeignEntry,      ///< 我们的键下出现了不是我们写的东西
    };

    Kind kind = Kind::LeftoverKey;
    QString key;
    QString name;
    QString detail;

    QString describe() const;
};

const char *shellResidueKindIdentifier(ShellResidueFinding::Kind kind);

///
/// \brief 卸载之后的残留检查结果（PLAT-005 完成标准第 4 条）。
///
struct ShellResidueReport
{
    QVector<ShellResidueFinding> findings;

    /// 检查时用的计划是否可推导（选项记录损坏时为 false，
    /// 此时只能做「登记键还在不在」这类粗查，报告里要说明）。
    bool planAvailable = true;

    bool clean() const;
    QString summary() const;
    QStringList lines() const;
};

///
/// \brief 能力查询（PLAT-005 完成标准第 5 条）。
///
/// 「置灰并说明」——说明是这句要求里最容易做丢的一半。
/// 因此除了 available 之外，一定同时给出 reason（为什么不行）
/// 与 advice（那用户该怎么办）。
///
struct ShellIntegrationCapability
{
    /// 当前这个后端现在能不能写。
    bool available = false;

    /// 本平台有没有「注册表」这一机制。界面置灰要绑这个，不是上面的 available——
    /// 预演用的内存后端永远 available，但它不代表平台支持。
    bool platformSupported = false;

    QString backend;
    QString reason;
    QString advice;

    QString summary() const;
};

// =============================================================================
// 七、服务
// =============================================================================

///
/// \brief Shell 集成的安装 / 卸载 / 校验（PRD: PLAT-005）。
///
/// 这里只操作注入进来的 RegistryStore，因此整套流程可以在任意平台上被真实执行。
///
class ShellIntegration
{
public:
    /// 不接管 `store` 的所有权；调用方要保证它的生命周期更长。
    explicit ShellIntegration(RegistryStore *store);

    /// 能力查询。界面据此决定是否置灰。
    ShellIntegrationCapability capability() const;

    /// 从真实存储里读出「装了什么」。卸载与界面展示都用它。
    struct InstalledState
    {
        bool installed = false;

        ///
        /// \brief 上一次安装被中断在半路。
        ///
        /// 判据是登记版本号停在 0。安装的写法是「先写 0，全部写完后改成 1」，
        /// 于是「版本号是 0」精确对应「有人写了一半就走了」（断电、被结束进程、
        /// 崩溃）。没有这个标记的话，半装状态与「完全没装」无法区分——
        /// 而两者的处置完全不同：前者需要先清理再装，后者直接装就行。
        ///
        bool installing = false;

        int version = 0;
        QString executablePath;
        ShellIntegrationOptions options;
        int backupCount = 0;

        /// 选项记录里出现了本版本不认识的键。为真时卸载要提示
        /// 「有更新版本写入的配置，可能有本版本不知道的项没被清理」。
        bool hasUnknownOptions = false;
        QStringList unknownOptionKeys;

        QString summary() const;
    };

    InstalledState installedState() const;

    ///
    /// \brief 安装。
    ///
    /// **全有或全无**：任何一条写入失败都会回滚整个安装。
    ///
    /// 为什么这里不沿用 PLAT-008 的「跳过并继续」默认值
    /// --------------------------------------------
    /// 注册表安装与批量文件操作的性质不同：
    ///   - 批量改属性时，第 7 个文件失败不影响第 8 个，继续做是对的；
    ///   - 而「装了一半」的 Shell 集成是一种**用户看不懂的状态**：
    ///     菜单项写进去了、它的 `shell\command` 没写进去，
    ///     于是菜单里出现一项，点下去什么都没发生（或弹一个「找不到文件」）。
    ///     用户既不会想到是安装失败了，也无从修复——重装一遍可能好、
    ///     可能还是坏（取决于哪一半失败）。
    /// 而回滚是**能做成**的：写入之前已经把要覆盖的旧值备份好了，
    /// 删除我们刚写的值不会伤到任何人。所以这里选原子性。
    ///
    /// 这也是 PLAT-008 把策略做成显式参数（`BatchFailurePolicy`）的用处：
    /// 两个场景可以各自选一个，而不是让默认值替所有场景做决定。
    ///
    ShellIntegrationReport install(const ShellIntegrationPlan &plan);

    /// 卸载。幂等：没装过时返回「未安装，无需卸载」的成功报告。
    /// 按**登记下来的选项**推导该删什么，而不是按当前选项。
    /// 结束时必然跑一次残留检查，结果附在报告的 residue* 字段里。
    ShellIntegrationReport uninstall();

    /// 校验：计划里的每一条现在对不对。只读，不改。
    ShellIntegrationReport verify(const ShellIntegrationPlan &plan) const;

    /// 残留检查。不传 `plan` 时按登记信息推导。
    ShellResidueReport findResidue() const;
    ShellResidueReport findResidue(const ShellIntegrationPlan &plan) const;

    ///
    /// \brief 预演：把真实存储里相关的部分复制进 `scratch`，在 `scratch` 上装一遍。
    ///
    /// 真实存储一个字节都不动。用来在确认对话框里回答「会覆盖我现有的关联吗」——
    /// 只在空存储上预演会给出「没有冲突」，而用户的机器上其实有。
    ///
    ShellIntegrationReport previewInstall(const ShellIntegrationPlan &plan,
                                          MemoryRegistryStore *scratch) const;

private:
    ///
    /// \brief 拆除一次安装：删值、还原共享值、按深度倒序删空键。
    ///
    /// **卸载与「安装失败后的回滚」共用同一段代码。** 这不是为了少写几行：
    /// 两处各写一遍必然分歧，而分歧的表现是「卸载能清干净、回滚不能」（或反之）——
    /// 后者只在安装失败时才走到，而安装失败本身就不常见，
    /// 于是这个缺陷可以躺很久，等到用户真的遇到一次失败的安装时，
    /// 他得到的是一台半装状态的机器。
    ///
    ShellIntegrationReport removeInstallation(const ShellIntegrationPlan &plan,
                                              const QString &operationName);

    RegistryStore *m_store = nullptr;
};

// =============================================================================
// 八、本程序收到 Shell 调用之后
// =============================================================================

/// 本程序如何看待这次启动。
enum class ShellInvocationKind {
    NotShellInvocation = 0, ///< 命令行里没有本程序的动作开关 → 不关本模块的事
    Valid,                  ///< 动作有效、路径个数也符合要求
    Invalid,                ///< 是 Shell 调用，但参数有问题 → 要给出明确提示
};

///
/// \brief 解析出来的一次 Shell 调用。
///
struct ShellInvocation
{
    ShellInvocationKind kind = ShellInvocationKind::NotShellInvocation;
    ShellAction action = ShellAction::Compare;
    QStringList paths;

    /// `Invalid` 时的原因（面向用户）。`NotShellInvocation` 时为空。
    QString problem;

    bool valid() const { return kind == ShellInvocationKind::Valid; }

    /// 一行可读描述，用于日志。
    QString describe() const;
};

///
/// \brief 从命令行参数解析。
///
/// 为什么路径个数要在这里校验而不是交给后续逻辑
/// ----------------------------------------
/// 「比较」拿到一个路径时，后续逻辑若不作声地拿它和自己比，会得出
/// 「两个文件完全相同」的结论——一个**错误但看起来合理**的结果。
/// 这类结果最危险：用户会据此认为文件没被改动过。
/// 所以个数不对时必须在入口处停下，并给出「请选中两个文件」这样的话。
///
ShellInvocation parseShellInvocation(const QStringList &arguments);

} // namespace Platform
} // namespace LqCompare

#endif // LQCOMPARE_SHELLINTEGRATION_H
