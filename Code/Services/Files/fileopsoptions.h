#ifndef LQCOMPARE_FILEOPSOPTIONS_H
#define LQCOMPARE_FILEOPSOPTIONS_H

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace LqCompare {
namespace Files {

///
/// \brief 文件操作的默认行为（PRD: OPT-005）。
///
/// 这个模块回答的唯一问题是：「用户没有在这一次操作上单独指定时，
/// 复制/移动/删除/覆盖该怎么做」。它**不执行任何文件操作**，
/// 也**不读任何文件**——输入是一份设置值（`QVariantMap`），
/// 输出是若干个可直接用于确认对话框与执行流程的结论。
///
/// 为什么单独一层，而不是把默认值散在各个调用点
/// ------------------------------------------------
/// 这三类默认值有一个共同点：**它们的错误方向是不可逆的**。
///
///   - 删除方式写成「永久删除」，用户的文件不再进回收站；
///   - 覆盖策略写成「覆盖」，一批「文件较新」的目标会被旧内容盖掉；
///   - 「保留元数据」漏掉时间戳，一次复制就把整棵目录树的修改时间刷成今天。
///
/// 所以规格的「边界」条款要求**默认值必须保守**（默认走回收站、默认不覆盖）。
/// 把这条要求写成一段散文放在文档里，下一个人读到时它已经过期了；
/// 写成 `safetyContractViolations()` 之后，它是一条会红的断言
/// （`Tests/FileOpsOptions` 的 A 组拿它钉住默认值）。
///
/// 为什么「保存的元数据项」是三个独立的布尔项而不是一个集合类型
/// ----------------------------------------------------------
/// 设置仓库（`Services/Settings/optionsrepository.h`）的值模型只支持
/// 布尔 / 整数 / 单值字符串三种，界面控件也是按这个模型一一对应的
/// （勾选框 / 数字框 / 下拉框）。要表达「多选集合」就得给模型加第四种值类型，
/// 而那会同时牵动校验、导入导出、控件生成与往返测试四处——为了一条设置项
/// 不值得。三个布尔项各有独立的键名与默认值，逐项可测、导入导出免费可用，
/// 而「集合」这个概念由 `MetadataPreservation` 在服务层恢复。
///
/// 目标文件相对源文件的修改时间关系。
///
/// 为什么把「不存在」也放进同一个枚举，而不是让调用方先判存在性：
/// 调用方手里的信息本来就是一次 `stat` 的结果，分成两个函数就会诱导出
/// 「先问存在、再问新旧」这种两次 `stat` 的写法，而两次之间目标文件可能
/// 已经被别人换掉了——那个竞态没有人会去处理。
enum class OverwriteSituation {
    TargetMissing = 0,
    TargetOlder,
    TargetSameTime,
    TargetNewer,
};

enum class OverwriteAction {
    Ask = 0,
    Overwrite,
    Skip,
};

/// 删除方式的默认值。
///
/// 默认是 `Trash`，且**不允许**通过「探测回收站是否可用」来反推——
/// 可用性探测是执行那一刻的事（`TrashService::availabilityFor()`），
/// 而这里是「用户希望怎么做」。两者混在一起会让「网络盘上不可用」
/// 变成「用户想永久删除」。
enum class DeleteMode {
    Trash = 0,      ///< 走回收站（默认，可还原）
    Permanent,      ///< 直接永久删除
};

/// 目标已存在时的默认处置。
///
/// 默认是 `Ask`：规格的边界条款要求「默认不覆盖」。
/// 注意 `Skip` 与 `Ask` 的区别不是「问不问」而是**能不能恢复**：
/// `Skip` 是用户事先声明「这一批我不要覆盖」，因此不该再逐条打扰他；
/// `Ask` 是用户愿意逐条决定。
enum class OverwritePolicy {
    Ask = 0,        ///< 询问（默认）
    Overwrite,      ///< 直接覆盖
    Skip,           ///< 跳过已存在的目标
};

/// 操作完成后的校验方式。
///
/// 默认 `None`：CRC 要读完整份内容，把它设成默认会让每一次复制
/// 都变成两倍 I/O，而大多数复制发生在用户可见的交互里，
/// 「复制 4 GB 的文件慢了一倍」比「校验漏了一次」更容易被投诉。
/// 需要强一致的场景（脚本、同步）由调用方显式打开。
enum class VerifyMode {
    None = 0,       ///< 不校验（默认）
    Size,           ///< 比对字节数（一次 stat）
    Crc,            ///< 读回全部内容比对 CRC32
};

/// 复制/移动时保留哪些元数据。
///
/// 三项默认全开。理由与上面三条相反：**保留是无损方向**，
/// 关掉才会丢信息（时间戳被刷成「现在」、只读位消失、权限变成默认）。
/// 一个文件比对工具「复制完两个文件竟然不再相同」是灾难。
struct MetadataPreservation {
    bool timestamps = true;
    bool attributes = true;
    bool permissions = true;

    bool all() const { return timestamps && attributes && permissions; }
    /// 稳定标识符列表（"timestamps" / "attributes" / "permissions"），已启用的。
    QStringList enabledIdentifiers() const;
};

/// `overwriteDecision()` 的结论。
struct OverwriteDecision {
    OverwriteAction action = OverwriteAction::Ask;
    bool targetNewer = false;
    /// 面向用户的一句话。目标较新时必定包含专门的说明（见下面的实现注释）。
    QString notice;
};

/// 默认大文件确认阈值（字节）。与设置里存的「兆字节」是同一个值的两种单位。
constexpr qint64 DefaultLargeFileConfirmBytes = 100LL * 1024 * 1024;
/// 默认批量删除条数确认阈值。
constexpr int DefaultBatchDeleteConfirmCount = 20;

/// 兆字节 ↔ 字节。0 表示「关闭确认」，不是「任何文件都要确认」。
qint64 largeFileConfirmMegabytesToBytes(int megabytes);
/// 字节 → 兆字节（向下取整）。只对整兆字节的值做往返，见 `validate()`。
int largeFileConfirmBytesToMegabytes(qint64 bytes);

///
/// \brief 一次文件操作要用到的全部默认行为。
///
struct FileOperationPolicy {
    DeleteMode deleteMode = DeleteMode::Trash;
    OverwritePolicy overwritePolicy = OverwritePolicy::Ask;
    MetadataPreservation preserve;
    qint64 largeFileConfirmBytes = DefaultLargeFileConfirmBytes;
    int batchDeleteConfirmCount = DefaultBatchDeleteConfirmCount;
    VerifyMode verifyMode = VerifyMode::None;

    ///
    /// \brief `fromValues()` 里被拒绝并回退到默认值的项（人类可读）。
    ///
    /// 单独留一条通道，是因为「设置文件被用户手改坏了」必须**看得见**：
    /// 悄悄回退到默认值会让用户以为自己的设置生效了，而悄悄采用一个
    /// 认不出的值（比如当成「永久删除」）更糟。策略本身仍然可用，
    /// 只是多了一条待显示的告警。
    ///
    QStringList fallbacks;

    ///
    /// \brief 从设置仓库的值构造。
    ///
    /// 未知 / 类型不对 / 标识符不认识的项一律回退到保守默认值，
    /// 并把原因记在 `validate()` 里。**不要**在这里抛错或让调用点
    /// 自己去判——设置文件是可以被用户手改的，一个拼错的大小写不该
    /// 让程序起不来，但也不该被静默当成「用户选了永久删除」。
    ///
    static FileOperationPolicy fromValues(const QVariantMap &values);

    /// 当前策略自身的内部一致性问题（空表示没问题）。
    ///
    /// 目前只查两件真实会发生的事：阈值不是整兆字节（设置里存的是整数兆，
    /// 存不下 104857600.5 这种值，读回来必然不等）、阈值为负。
    QStringList validate() const;

    ///
    /// \brief 规格「默认值必须保守」这条边界的机器可读形式。
    ///
    /// 返回被破坏的条款描述。对出厂默认值调用必须返回空列表；
    /// 任何一处默认值被改成不保守的方向时，它会指出是哪一处。
    /// A 组的用例正是拿它反向验证的（把默认改成 `Permanent` 必须红）。
    ///
    QStringList safetyContractViolations() const;

    /// 大文件确认：`largeFileConfirmBytes` 为 0 时返回 false（确认已关闭）。
    bool needsLargeFileConfirmation(qint64 bytes) const;
    /// 批量删除确认：`batchDeleteConfirmCount` 为 0 时返回 false（确认已关闭）。
    bool needsBatchDeleteConfirmation(int count) const;

    /// 删除前的提示。走回收站时为空串（没有需要警告的事），
    /// 永久删除时返回不可恢复的提示。
    ///
    /// 为什么空串而不是「会进回收站」这类中性文案：调用方看到空串就知道
    /// 不必弹确认，于是「默认走回收站」在界面上就是**不需要任何交互**的
    /// 默认路径。给一句中性文案会诱导出「每次删除都弹一次没有信息量的框」，
    /// 用户点几次之后就不看内容了——那时真正的警告也拦不住他。
    QString deleteWarning() const;

    /// 目标已存在时的结论。`action` 直接由 `overwritePolicy` 决定，
    /// 情况只影响 `notice` 的措辞。
    OverwriteDecision overwriteDecision(OverwriteSituation situation) const;

    /// 一行摘要，给启动日志与诊断包用。
    QString describe() const;
};

// ---------------------------------------------------------------------------
// 稳定标识符（会进设置文件、导入导出与日志，因此是**对外事实**
// ——中文标签改多少次都不影响它，反之亦然）
// ---------------------------------------------------------------------------

QString deleteModeIdentifier(DeleteMode mode);
bool deleteModeFromIdentifier(const QString &identifier, DeleteMode *mode);
QString overwritePolicyIdentifier(OverwritePolicy policy);
bool overwritePolicyFromIdentifier(const QString &identifier, OverwritePolicy *policy);
QString verifyModeIdentifier(VerifyMode mode);
bool verifyModeFromIdentifier(const QString &identifier, VerifyMode *mode);
QString overwriteSituationIdentifier(OverwriteSituation situation);

// ---------------------------------------------------------------------------
// 中文标签（只给界面用）。标识符才是存储与比较的事实来源——
// 这与 `Tests/SettingsScope` 不直接 `QCOMPARE` 枚举是同一个理由。
// ---------------------------------------------------------------------------

QString deleteModeLabel(DeleteMode mode);
QString overwritePolicyLabel(OverwritePolicy policy);
QString verifyModeLabel(VerifyMode mode);
/// 三项元数据的标签；认不出的标识符返回空串（**不要**编一个假名字）。
QString metadataItemLabel(const QString &itemIdentifier);

// ---------------------------------------------------------------------------
// 设置键表
//
// 键名只在这张表里写一次。`FileOperationPolicy::fromValues()` 也按这张表取键，
// 于是「策略读的键」与「设置仓库登记的键」不可能分家——这是本仓对
// 「两份事实来源」的一贯处理（对比 `filterLayerTable()`、
// `attributeConditionTable()`、`nameFilterModeTable()`）。
// ---------------------------------------------------------------------------

struct FileOperationKey {
    QString identifier;  ///< 短名，`fileOperationKey()` 的入参
    QString key;         ///< 设置仓库里的完整键名（`fileops.` 前缀）
    QString purpose;     ///< 一句话说明，自检要求非空
};

const QVector<FileOperationKey> &fileOperationKeyTable();
QStringList fileOperationKeys();
/// 短名 → 完整键名；短名不存在时返回空串。
QString fileOperationKey(const QString &identifier);

///
/// \brief 键表自检。**表当参数**，否则它永远不会红（见 §6 那条坑）。
///
/// 查四件事：短名与完整键名都非空且唯一、完整键名都带 `fileops.` 前缀、
/// 说明都非空。返回人类可读的问题清单。
///
QStringList validateFileOperationKeyTable(const QVector<FileOperationKey> &table);

} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_FILEOPSOPTIONS_H
