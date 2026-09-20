#ifndef LQCOMPARE_SESSIONTYPE_H
#define LQCOMPARE_SESSIONTYPE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "mask.h" // Services/Filter —— 类型掩码复用同一个掩码语言（FILT-001）。

namespace LqCompare {

/// 会话视图中「被造出来的东西」（`Views/Session/CompareSession`）。
///
/// **只前向声明，不 include**：本文件在服务层，而 `CompareSession` 在界面层
/// （`Views/Session/`），`tools/check_layering.py` 明令服务层不得 include 界面头文件。
/// 前向声明不会带来那条依赖——它既不引入界面代码，也不让本模块的测试需要链接
/// QtWidgets（`Tests/SessionType` 就是纯 QtCore 的）。
///
/// 这正是「依赖倒置」的形状：服务层声明**工厂长什么样**，界面层在注册时给出
/// 真正的实现。反过来说，如果注册表把工厂的返回类型写成 `QObject *` 再让调用方
/// 自己转，类型安全就丢在每一次创建会话的地方了。
class CompareSession;

/// 会话类型的创建工厂（SESS-002 第 1 条要求的「创建工厂」）。
///
/// 参数是父对象；`CompareSession` 的构造函数还要求一个类型 ID，但那个 ID 就是
/// 注册表条目自己的 `SessionType::id`，让调用方再传一遍只会多一个传错的机会。
///
/// 允许为空：类型可以先登记（于是 Home 页、`--list-session-types` 立刻看得到它），
/// 实现稍后到。空工厂与「类型不存在」是两件事——前者是「这一版还没有这个视图」，
/// 后者是拼写错误。`SessionTypeEntry::hasFactory()` 把这个区别摆出来。
using SessionFactory = std::function<CompareSession *(QObject *parent)>;

///
/// \brief 会话类型的分组（Home 页与新建会话向导的一级分区）。
///
/// 四组与 Beyond Compare 的 Home 视图分区一致：文本类 / 文件夹类 / 数据类 /
/// 系统类（新建会话向导把它叫「高级」，见 SESS-005）。
///
/// **划分由注册表拥有，文案由各界面拥有**：同一份数据在两处显示不同标题是产品
/// 决定，不是数据问题。把两套文案都塞进注册表，第三个界面出现时就无从选择了。
/// 这里提供的是默认文案（Home 页那一套），界面要覆盖就自己给字符串。
enum class SessionGroup { Text, Folders, Data, Advanced };

/// 分组标识。机器可读，用于设置文件与命令行——与 `SessionType::id` 同理，
/// 一经发布不可改；改文案不影响它。
const char *sessionGroupIdentifier(SessionGroup group);

/// 分组的默认显示文案（Home 页那一套）。界面可自行覆盖，见枚举说明。
QString sessionGroupLabel(SessionGroup group);

///
/// \brief 会话类型的版本归属。
///
/// **只有两态，没有「未标注」**：研究基线
/// `docs/research/beyondcompare-features.md` 开头的标注约定写着
/// 「Pro 专有功能用 `[Pro]` 标注」——也就是**没有标记即 Standard**。
/// 所以「规格没说」在这套材料里已经有一个确定含义，不需要第三个态。
///
/// 这条约定要守住：不要在代码里凭印象给某个类型补 `Pro`。要加就先改研究文档，
/// 让标注与代码同源；`Tests/SessionType` 有一条用例把当前的三条 `Pro` 锁死。
enum class SessionEdition { Standard, Pro };

/// 版本标识（机器可读），如 `"pro"` / `"standard"`。
const char *sessionEditionIdentifier(SessionEdition edition);

/// 版本显示文案，即 Beyond Compare 自己的写法（`"Pro"` / `"Standard"`），不翻译。
QString sessionEditionLabel(SessionEdition edition);

///
/// \brief 会话类型的平台限定。
///
/// 取值只有两个，理由与 `SessionEdition` 相同：研究基线用 `[Win]` 标注
/// 「仅 Windows 可用」，**没有标记即全平台可用**。
enum class SessionPlatformScope { All, WindowsOnly };

/// 平台限定标识（机器可读），如 `"all"` / `"windows"`。
const char *sessionPlatformScopeIdentifier(SessionPlatformScope scope);

/// 面向用户的说明文案。REG-001 的边界要求「非 Windows 平台该会话类型不可用时
/// 必须在 Home 页明确标注「仅 Windows」」——本函数就是那句标注的唯一来源，
/// 免得 Home 页、新建向导、命令行各拼一份（后面必然有一处写成「Windows 专用」）。
QString sessionPlatformScopeLabel(SessionPlatformScope scope);

/// 当前平台是否满足某个类型声明的平台要求。
///
/// 抽成接受显式参数的纯函数而不是在调用点写 `#ifdef Q_OS_WIN`：那样 Windows 分支
/// 在开发机上永远不执行，也就永远测不到。本仓库在 `pathutils.h` 的路径规则
/// 与 `mask.h` 的大小写策略上用的是同一个手法。
bool currentPlatformAllows(SessionPlatformScope scope);

/// 类型 ID 是否合法：小写字母开头，其余为小写字母、数字或连字符。
///
/// 单独做成公开函数而不是藏在 `add()` 里，是因为命令行（CLI-002 的
/// `--list-session-types` 与 `/fv=<type>`）也要对用户敲进来的字符串做同一件事，
/// 而「两个地方各写一份规则」正是这类校验最容易分家的地方。
bool isValidSessionTypeId(const QString &id);

///
/// \brief 一种会话类型的描述子（纯数据）。
///
/// **为什么把描述子与创建工厂分成两层类型**（本结构体不含工厂，见
/// `SessionTypeEntry`）：描述子要被快照断言、被比较、将来还要写进设置文件，
/// 而 `std::function` **不可比较**、也不该出现在快照里——把它混进来，
/// 「已发布 ID 的字符串值被快照断言锁定」（第 2 条）就没法直接照搬整张表。
///
struct SessionType
{
    /// 类型标识。小写字母、数字与连字符，形如 `text`、`folder-merge`。
    ///
    /// **一经发布不可改名**（SESS-002 的边界）：已保存的会话文件、命令行、
    /// 最近会话列表里存的都是它。改名会让那些入口指向一个不存在的类型，
    /// 而现象是「双击会话没反应」——最难归因的一类故障。因此
    /// `SessionTypeRegistry::add()` 会校验格式并拒绝重复，`Tests/SessionType`
    /// 另有一条快照用例把全部已发布 ID 的值锁死。
    QString id;

    /// 界面显示名（可翻译），如「十六进制比较会话」。
    QString displayName;

    /// Beyond Compare 的官方英文名，如 `Hex Compare`。
    ///
    /// 单独留一个字段而不是从 `displayName` 推导：它是 `/fv=<type>` 的取值
    /// （CLI-027 要求「类型串需与官方完全一致」），也是和研究者对照规格时的锚点。
    /// 中文界面下它仍然有用——Home 页现在的卡片标题就是英文的。
    QString englishName;

    /// 一句话说明，供 Home 页与新建向导的卡片直接用（SESS-003 / SESS-005 都要求
    /// 卡片上有一句说明）。
    QString summary;

    /// 图标资源键，写法与 `Command::icon` 一致（`":/Pictures/<name>.svg"`）。
    ///
    /// **可以为空，当前全部为空**：会话类型的图标属于 SESS-003 的资源批次，
    /// 现在没有对应的 SVG。留空是诚实的——从现有 ribbon 图标里随便认领一个
    /// （比如拿 `ribbon_hex.svg` 当十六进制会话的图标）会在界面成型的那天
    /// 变成一批要逐个纠正的错误视觉。`Tests/SessionType` 有一条用例保证：
    /// 哪天有人填进来，它必须在 `Pictures.qrc` 里真实存在。
    QString iconKey;

    /// 默认文件掩码（SESS-002 第 1 条的「默认文件掩码」）。
    ///
    /// **这不是 Beyond Compare 那张完整的文件格式关联表**，而是一组**无歧义的
    /// 种子**：`.csv` 给表格、`.png` 给图片、`.diff` 给补丁视图。真正的权威在
    /// `FORMAT-*`（文件格式定义）——那是「扩展名 → 视图」的完整映射，落地后
    /// 由它回答，本字段退化为「没有任何文件格式认领时的兜底」。
    /// 现在如实只放种子，好过抄一份假装完整的表：假装完整的那份会在
    /// `FORMAT-*` 落地时被逐条质疑，而种子不会。
    ///
    /// 掩码语法就是 `Services/Filter` 那套（`mask.h`，FILT-001）——
    /// FILT-001 的边界写着「不允许各处实现各自的通配逻辑」，这里是它的第一个
    /// 复用方。空列表表示「不参与按掩码的自动选择」（目录类型、兜底类型、
    /// 只能显式打开的辅助视图）。
    QStringList fileMasks;

    SessionGroup group = SessionGroup::Text;

    SessionEdition edition = SessionEdition::Standard;

    SessionPlatformScope platforms = SessionPlatformScope::All;

    /// 诊断用的一行/多行摘要。
    QString describe() const;

    /// 是否声明了任一掩码。
    bool hasFileMasks() const { return !fileMasks.isEmpty(); }
};

///
/// \brief 内置的全部会话类型（14 条）。
///
/// **顺序就是优先级**（SESS-002 第 3 条：按掩码查询时按注册顺序返回首个命中），
/// 同时也是 Home 页分组展示的顺序——一个顺序服务两个用途是刻意的：
/// 若「查询优先级」与「展示顺序」是两份，用户会看到「明明表格比对在上面，
/// 双击 .html 却打开了文本比对」这种无法自洽的界面。
///
/// 为什么是 14 条而研究基线的 §1 写「共 13 种」：那 13 种是 BC 的
/// **新建会话可选类型**，不含 Archive Compare——BC 把压缩包当作文件夹处理
/// （ARC-001「压缩包默认显示为带图标的文件夹」）。但我们已在 Home 页与
/// `CLI-002` 的规格里把「压缩包」列成一个类型，所以照 14 条登记。
/// **这个差额是有意留下的、已知的**，`Tests/SessionType` 里有一条用例
/// 把 14 这个数字与 13 这个事实同时钉住，改的时候必须做一次决定。
///
/// 每次调用都重新构造：表里含可翻译文案，缓存成函数内静态的话，
/// 「先取表、后切换语言」会把旧语言的字符串留在界面里。
/// 十四条约等于几次字符串拷贝，不值得为它冒这个风险。
QVector<SessionType> builtInSessionTypes();

/// 内置类型的 ID 列表，按注册顺序。
QStringList builtInSessionTypeIds();

///
/// \brief 注册表里的一个条目：描述子 + 创建工厂。
///
struct SessionTypeEntry
{
    SessionType type;
    SessionFactory factory;

    bool hasFactory() const { return static_cast<bool>(factory); }

    /// 当前平台是否可用（按 `type.platforms` 判断）。
    bool isAvailableHere() const { return currentPlatformAllows(type.platforms); }

    /// 面向用户的「不可用原因」，可用时为空串。
    /// 界面把这一句直接贴在置灰的入口旁边（REG-001 的第 3 条完成标准）。
    QString unavailableReason() const;
};

///
/// \brief 会话类型注册表（PRD: SESS-002）。
///
/// 应用启动时把全部类型登记进来，之后**只读**：Home 页、新建会话向导、
/// 文件对自动选视图（SESS-013）、命令行的 `--list-session-types`（CLI-002）
/// 都从这一处取。
///
/// 为什么不做成全局单例：注册表要被测试反复构造（合成的小表用于验证优先级、
/// 内置的大表用于验证快照），单例会让「这一次测试往表里加了什么」泄漏到下一处。
/// 由 `MainWindow` 构造一个并往下传——依赖关系写得出来，也改得动。
///
class SessionTypeRegistry
{
public:
    SessionTypeRegistry();

    ///
    /// \brief 登记一个类型。
    ///
    /// 校验：ID 非空且符合 `[a-z][a-z0-9-]*`、ID 未重复、显示名非空、
    /// 每条掩码都能编译。任一条不过就整条拒绝并说明原因——
    /// **不做部分接受**：一个「ID 进去了但掩码全丢了」的条目会让按掩码的
    /// 自动选择静默失效，而界面上看不出任何异常。
    ///
    bool add(const SessionType &type, const SessionFactory &factory = SessionFactory(),
             QString *error = nullptr);

    /// 把 `builtInSessionTypes()` 全部登记进来（应用启动时调一次）。
    /// 返回成功登记的数量；有失败时把第一条错误写进 `error`。
    int addBuiltInTypes(QString *error = nullptr);

    /// 清空。测试里构造合成表时用。
    void clear();

    bool isEmpty() const;
    int count() const;

    /// 全部条目，按注册顺序。
    ///
    /// 注意返回的指针指向注册表内部，**在再次 `add()` 之后失效**（`QVector`
    /// 可能重新分配）。应用启动时一次登记完、之后只读，这个约定在实践中成立；
    /// 按掩码查询是文件夹扫描的热路径，为每一次文件匹配复制一串掩码没有好处。
    QVector<const SessionTypeEntry *> entries() const;

    /// 按 ID 查。**大小写敏感**——ID 是机器键，`Text` 与 `text` 必须是两件事，
    /// 否则「ID 一经发布不可改名」就守不住了（多出大小写变体）。
    const SessionTypeEntry *find(const QString &id) const;

    /// 按名字查：依次匹配 ID、英文原名、显示名，**大小写不敏感**。
    ///
    /// 为 CLI-002 第 2 条备好（「视图类型名称支持别名与大小写不敏感匹配」）——
    /// 命令行上用户敲的是 `Text Compare` 或 `hex`，而存进会话文件的是 ID，
    /// 两者不能是同一个字段（一个要求大小写不敏感，一个要求严格稳定）。
    const SessionTypeEntry *findByName(const QString &name) const;

    ///
    /// \brief 按文件名查首个匹配的类型（第 3 条）。
    ///
    /// 按**注册顺序**返回第一个命中，因此先登记的类型优先级更高。
    /// 没有任何掩码命中时返回 nullptr——调用方据此走兜底（例如按二进制判定
    /// 落到十六进制视图）。
    ///
    /// `cs` 显式传入而不是从平台内部取：这样「Windows 上大小写不敏感」
    /// 这条规则在 macOS 上也能被真实断言（与 `mask.h` 的 `defaultCaseSensitivity`
    /// 同一手法）。单参数重载用平台默认值。
    const SessionTypeEntry *findByFileMask(const QString &fileName, Qt::CaseSensitivity cs) const;
    const SessionTypeEntry *findByFileMask(const QString &fileName) const;

    /// 全部命中的类型，按注册顺序。
    ///
    /// 与 `findByFileMask` 分开而不是让它返回列表：自动选择要的是「选哪个」，
    /// 而「同时命中几个」是另一个问题——`SESS-014` 的「掩码冲突时以列表顺序
    /// 决定」与 `ALL` 用例诊断都要用后者。只报第一个会让用户改掉一条规则后
    /// 仍然得到同一个结果，而界面上没有任何线索说明还有第二条规则在起作用。
    QVector<const SessionTypeEntry *> allByFileMask(const QString &fileName,
                                                   Qt::CaseSensitivity cs) const;
    QVector<const SessionTypeEntry *> allByFileMask(const QString &fileName) const;

    /// 按分组枚举（第 4 条：供 Home 页与新建向导直接生成入口）。
    /// `onlyAvailable` 为真时只返回当前平台可用的（默认如此：不可用的类型
    /// 由界面单独置灰展示，不混进可点列表）。
    QVector<const SessionTypeEntry *> byGroup(SessionGroup group,
                                             bool onlyAvailable = true) const;

    /// 出现过的分组，按 `SessionGroup` 的固定顺序。
    QVector<SessionGroup> groups(bool onlyAvailable = true) const;

    /// 大小写策略的显式覆盖（与 `MaskFilter` 同一套入口）。不设时用平台默认值。
    Qt::CaseSensitivity caseSensitivity() const;
    bool isCaseSensitivityOverridden() const;
    void setCaseSensitivity(Qt::CaseSensitivity cs);
    void clearCaseSensitivityOverride();

    /// 自检：列出内部不一致（重复 ID、掩码为空串、掩码编译失败等）。
    /// 内置表必须返回空列表。
    QStringList validate() const;

    /// 诊断用的整表摘要。
    QString describe() const;

private:
    /// 条目 + 注册时编译好的掩码。
    ///
    /// 编好的掩码与描述子放在同一条记录里，而不是并行两个 `QVector`：
    /// 并行数组一旦有一处忘了同步，现象是「A 类型的掩码被拿去匹配 B 类型」——
    /// 只在特定扩展名上出现，几乎不可能靠读代码发现。
    struct Row
    {
        SessionTypeEntry entry;
        QVector<Filter::Mask> masks;
    };

    const Row *rowForId(const QString &id) const;

    QVector<Row> m_rows;
    Qt::CaseSensitivity m_case = Qt::CaseSensitive;
    bool m_caseOverridden = false;
};

} // namespace LqCompare

#endif // LQCOMPARE_SESSIONTYPE_H
