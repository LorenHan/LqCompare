#ifndef LQCOMPARE_REGISTRYSTORE_H
#define LQCOMPARE_REGISTRYSTORE_H

#include "Files/filesystem.h"

#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace LqCompare {
namespace Platform {

///
/// \brief 注册表值的类型（PRD: PLAT-005）。
///
/// 为什么不直接用 QVariant 的类型来推
/// --------------------------------
/// QVariant 能表达「字符串」与「整数」，但表达不了 `REG_SZ` 与 `REG_EXPAND_SZ`
/// 的**区别**——而这两者在注册表里有实际后果：`REG_EXPAND_SZ` 会在读取时展开
/// `%ProgramFiles%` 这样的环境变量，`REG_SZ` 不会。写错类型的表现是
/// 「命令行里出现一个字面量 `%ProgramFiles%`，程序启动失败」，
/// 而注册表编辑器里两种值看起来完全一样。
///
/// 所以类型必须显式带着走，不能从值的内容反推。
///
enum class RegistryValueKind {
    String = 0,     ///< REG_SZ
    ExpandString,   ///< REG_EXPAND_SZ（读取时展开 %VAR%）
    DWord,          ///< REG_DWORD

    ///
    /// \brief 我们不解释的类型，原样保留。
    ///
    /// 为什么必须有这一档，而不是「读到不认识的类型就当读取失败」
    /// ------------------------------------------------------
    /// `.patch` / `.diff` 的默认值在使用者机器上可能是任何类型
    /// （`REG_MULTI_SZ`、`REG_BINARY`、`REG_NONE`……）。
    /// 如果 `value()` 对它们返回「读不到」，安装时的「备份旧值」那一步
    /// 就会把「有一个非空但我不认识的值」记成「本来就没有值」，
    /// 于是卸载时删掉它——**这是销毁用户数据**，而且现象是
    /// 「卸完之后某个文件关联彻底没了」，用户根本不会联想到安装器。
    ///
    /// 所以宁可多一档：不认识就原样读出（`raw` + `rawType`），
    /// 卸载时原样写回。我们不需要理解它，只需要不弄丢它。
    ///
    Unsupported,
};

/// 稳定的机器可读标识（"reg_sz" / "reg_expand_sz" / "reg_dword" / "unsupported"）。
const char *registryValueKindIdentifier(RegistryValueKind kind);

/// 注册表编辑器里显示的类型名（"REG_SZ" …），用于日志与校验报告。
QString registryValueKindName(RegistryValueKind kind);

///
/// \brief 一个注册表值：类型 + 内容。
///
/// 字符串与数字分开存，而不是塞进一个 QVariant：这样「DWord 被当成字符串写」
/// 在编译期就不可表达，而不是等到注册表里出现一个 `"32"` 才发现。
///
struct RegistryValue
{
    RegistryValueKind kind = RegistryValueKind::String;
    QString string;
    quint32 dword = 0;

    /// 仅当 `kind == Unsupported` 时有意义：原始字节。
    QByteArray raw;

    /// 仅当 `kind == Unsupported` 时有意义：原始 `REG_*` 类型号。
    /// 没有它就没法原样写回——`RegSetValueExW` 要求给出类型。
    quint32 rawType = 0;

    RegistryValue() = default;

    /// REG_SZ。
    static RegistryValue of(const QString &text);

    /// REG_EXPAND_SZ。只在内容确实含 `%VAR%` 时使用，否则用 of()。
    static RegistryValue expandable(const QString &text);

    /// REG_DWORD。
    static RegistryValue ofNumber(quint32 number);

    /// 我们不解释的类型：原样留着，卸载时原样写回。
    static RegistryValue ofRaw(quint32 rawType, const QByteArray &bytes);

    bool operator==(const RegistryValue &other) const;
    bool operator!=(const RegistryValue &other) const { return !(*this == other); }

    /// 人类可读的一行，用于校验报告与日志（不带类型名时看不出 `32` 与 `"32"`）。
    QString display() const;
};

///
/// \brief 注册表式键值存储的抽象（PRD: PLAT-005）。
///
/// 为什么要有这一层，而不是直接调 `RegCreateKeyExW`
/// ---------------------------------------------
/// 真正的注册表只有 Windows 有，但 Shell 集成这件事里**绝大部分代码不是注册表调用**：
/// 是「写哪些键、每个键写什么值、卸载时把哪些删掉、哪些必须还原成原样」。
/// 而这恰恰是最容易写错、错了最难查的部分——
///   - 卸载漏了一个键：用户右键菜单里多出一项，点下去报「找不到程序」，
///     而重装一遍又好不了（因为它不是我们这次的键）；
///   - 卸载删过头：把用户原有的 `.diff` 关联一起删了，
///     表现是「装之前能双击打开的补丁文件，卸完之后双击没反应」。
/// 这两类缺陷都不会让程序崩溃，只会让用户在一个星期后发现问题，且追不回原因。
///
/// 所以把「存储」抽成一个接口，让上面那批规则跑在**可在任意平台验证**的实现上
/// （内存后端、以及测试里真实落盘的临时后端），Windows 后端只负责把同样的调用
/// 翻译成 `Reg*W`。这与本仓库在 `Files/` 里做的事是同一套手法：
/// 平台规则抽成能真实执行的代码，只把系统调用留在薄层里。
///
/// 大小写：注册表的键名与值名**不区分大小写、但保留大小写**
/// ----------------------------------------------------
/// 这条不是细节，它是上面第二类缺陷的成因之一：如果安装时写成 `LqCompare.DiffFile`，
/// 卸载时按 `lqcompare.difffile` 去找，一个「大小写敏感」的后端会查不到，
/// 于是卸载"成功"了、残留还在。因此本接口要求所有实现**按大小写不敏感匹配**，
/// 并且把这条规则提成 normalizeKey()/normalizeName() 两个公开函数——
/// 它是契约的一部分，不该是实现里各自的判断。
///
/// 但归一**只用于比较**，不用于存储：注册表里保留你写进去的那个拼法。
/// 全部按小写存会让 regedit 里出现 `lqcompare.difffile`，
/// 与 Windows 上 ProgID 的惯例（`Vendor.Product`）对不上，
/// 下一个接手的人会以为这是谁随手敲的。所以实现要守住两条：
/// 查找按归一键，写回保留原始拼法。
///
class RegistryStore
{
public:
    virtual ~RegistryStore();

    /// 后端的机器可读名（"memory" / "win32-registry" / "unsupported"…）。
    virtual QString backendName() const = 0;

    ///
    /// \brief 这个后端在**本进程所在的平台**上是否真的能读写注册表。
    ///
    /// 与非 Windows 平台的关系：无法用的后端不是「抛异常」，而是如实返回 false
    /// 并给出原因。调用方据此把能力置灰并显示原因（PLAT-005 完成标准第 5 条），
    /// 而不是让用户在点击之后才看到一个「操作失败」。
    ///
    virtual bool isAvailable(QString *reason = nullptr) const = 0;

    // --- 读 ---

    /// 键是否存在。注意「键存在但没有任何值」与「键不存在」是两件事：
    /// 卸载时能不能删掉这个键，取决于它是不是**我们**创建的。
    virtual bool keyExists(const QString &key) const = 0;

    /// 读一个值。不存在时返回 false；`out` 不被修改。
    /// `name` 为空表示 `(Default)`。
    virtual bool value(const QString &key, const QString &name, RegistryValue *out) const = 0;

    /// 直接子键名（不是完整路径）。不存在的键返回空列表。
    virtual QStringList subKeys(const QString &key) const = 0;

    /// 直接值名。`(Default)` 以空字符串的形式出现在列表里（若它存在）。
    virtual QStringList valueNames(const QString &key) const = 0;

    // --- 写 ---
    //
    // 每个写操作都带 ErrorCode 出参，与 Files/ 的做法一致（PLAT-008）：
    // 原始系统码必须在失败的那一刻被记下来。后端拿不到原始码时
    // （例如内存后端）如实留空，而不是编一个。

    /// 写入或覆盖一个值。不存在的键会被**创建**（含中间的父键）。
    virtual bool setValue(const QString &key, const QString &name, const RegistryValue &value,
                          Files::ErrorCode *error = nullptr) = 0;

    /// 删除一个值。值本来就不存在时返回 **true**——卸载要幂等：
    /// 「已经不在了」与「删掉了」对调用方是同一个结果，
    /// 报成失败会让用户看到一堆假错误，从而忽略真正的失败。
    virtual bool removeValue(const QString &key, const QString &name,
                             Files::ErrorCode *error = nullptr) = 0;

    /// **递归**删除整个键（含子键与所有值）。键不存在时返回 true（幂等）。
    virtual bool removeKey(const QString &key, Files::ErrorCode *error = nullptr) = 0;

    // --- 供实现与调用方共用的规则 ---

    /// 键路径的大小写归一形式（本仓库统一用 `\` 作分隔符，与注册表一致）。
    static QString normalizeKey(const QString &key);

    /// 值名的大小写归一形式。空名（`(Default)`）归一后仍是空名。
    static QString normalizeName(const QString &name);

    /// 把一个键路径拆成「父键 + 末段」。没有分隔符时父键为空。
    static QString parentKey(const QString &key);

    /// 键路径的深度（`a` 为 1，`a\b` 为 2）。用于「按深度倒序删除」。
    static int keyDepth(const QString &key);
};

/// 键是否既没有子键也没有值。卸载时只有这种键才能安全删掉。
bool isKeyEmpty(const RegistryStore &store, const QString &key);

///
/// \brief 内存后端：测试用，也是「预演」用的后端（PRD: PLAT-005 完成标准第 4 条）。
///
/// 「预演」不是一个测试专用功能，而是本仓库的一贯约定（破坏性操作前先预演）：
/// 用户点「安装 Shell 集成」之前，可以先在内存后端上跑一遍，
/// 拿到「将写入 62 个值、其中 2 个是覆盖你现有的文件关联」这样的一份报告，
/// 而注册表本身一个字节都没动。
///
/// 故障注入（`failOnKey` / `failOnValue`）与 FakeFileSystem 是同一个理由：
/// 「写到第 30 条时失败」这条路径必须能被真实构造出来，否则安装失败后的
/// 清理与回滚逻辑永远只是「写了但没跑过」。
///
class MemoryRegistryStore : public RegistryStore
{
public:
    MemoryRegistryStore();

    QString backendName() const override { return QStringLiteral("memory"); }
    bool isAvailable(QString *reason = nullptr) const override;

    bool keyExists(const QString &key) const override;
    bool value(const QString &key, const QString &name, RegistryValue *out) const override;
    QStringList subKeys(const QString &key) const override;
    QStringList valueNames(const QString &key) const override;

    bool setValue(const QString &key, const QString &name, const RegistryValue &value,
                  Files::ErrorCode *error = nullptr) override;
    bool removeValue(const QString &key, const QString &name,
                     Files::ErrorCode *error = nullptr) override;
    bool removeKey(const QString &key, Files::ErrorCode *error = nullptr) override;

    // --- 测试与预演专用 ---

    /// 把另一个存储里已经有的一切复制进来。
    ///
    /// 预演的用法：`memory.seedFrom(*native)` 之后在 memory 上跑安装，
    /// 于是「会覆盖你现有的 .patch 关联」这件事能被真实算出来——
    /// 若只在空存储上预演，报告会说「没有冲突」，而用户的机器上其实有。
    void seedFrom(const RegistryStore &source, const QString &prefix = QString());

    /// 建立一个**没有值**的键。
    ///
    /// 为什么需要它：`keyExists()` 与「有值」是两件事，而卸载时
    /// 「这个键是不是我建的」取决于前者。预演要把「`.patch` 这个键本来就在、
    /// 但它的默认值是空的」这一事实带进来，光复制值复制不出它。
    void touchKey(const QString &key);

    /// 让接下来对 `key`（及其子键）的所有**写**操作失败。
    void failOnKey(const QString &key, const Files::ErrorCode &error = defaultInjectedError());

    /// 让对 `key` 下名为 `name` 的值的写操作失败（比 failOnKey 更精确）。
    void failOnValue(const QString &key, const QString &name,
                     const Files::ErrorCode &error = defaultInjectedError());

    ///
    /// \brief 让**下一次**对某个值的**写入**失败，然后自动失效。
    ///
    /// 与 `failOnValue()` 的区别很关键：`failOnValue()` 把对该值的**删除**
    /// 也一起挡住，于是「写入失败之后能不能把已经写进去的回滚干净」
    /// 这条路径根本测不出来——回滚必然也失败，报告只会说「回滚未完全成功」，
    /// 而那两种情况（回滚逻辑写错了 / 注册表真的删不掉）从结果上看不出区别。
    ///
    /// 真实世界里的写入失败大多是**瞬时**的（杀毒软件短暂锁住注册表键、
    /// 资源紧张、被另一个进程抢了一下），紧接着的删除是能成功的。
    /// 这个注入就是用来模拟那一类。
    ///
    void failNextWriteOnValue(const QString &key, const QString &name,
                              const Files::ErrorCode &error = defaultInjectedError());

    /// 注入故障时的默认错误码。
    ///
    /// 特意不是 `ErrorCode()`（那是「成功」）：默认值写错了的后果是
    /// 注入的失败被上层当成成功，于是「安装中途失败要回滚」这条路径
    /// 在测试里静默地什么都没测——而且测试仍然是绿的。
    /// 取「拒绝访问」是因为它正是真实注册表最常给的失败（缺管理员权限）。
    static Files::ErrorCode defaultInjectedError();

    /// 清空故障注入。测试之间调用，避免一个用例的注入影响下一个。
    void clearInjectedFailures();

    /// 当前存储里的全部键（**保留写入时的拼法**，已排序）。
    /// 用于断言「什么都没有留下」。
    QStringList allKeys() const;

    /// 一条 `键\0值名` 形式的扁平清单，便于一次性比对残留。
    QStringList allValues() const;

    /// 写操作被调用了几次。用来断言「卸载没有多删一个键」这类事实。
    int writeCallCount() const { return m_writeCalls; }

private:
    ///
    /// \brief 是否应该对这次操作注入失败。命中时填好错误码并返回 true。
    ///
    /// `writing` 用来区分「写入（新建/覆盖一个值）」与「删除」。
    /// 一次性注入（`failNextWriteOnValue()`）只在 `writing` 为真时命中并消费，
    /// 因为它的用途恰恰是「让这一次写入失败，然后看回滚能不能把已写进去的删干净」——
    /// 若删除也被挡住，「回滚逻辑错了」与「注册表真的删不掉」就分不开。
    ///
    /// 这个函数**不是 const**：一次性注入要在命中的那一刻把自己从待办里去掉。
    /// 用 `mutable` 成员假装 const 会让「读操作会改状态」这件事藏起来，
    /// 而这正是一个查询函数不该有的行为。
    bool shouldFail(const QString &key, const QString &name, bool writing,
                    Files::ErrorCode *error);

    /// `key` 是否是某个注入前缀的子键（含自身）。
    static bool isUnder(const QString &key, const QString &prefix);

    struct Entry
    {
        /// 第一次写这个键时的拼法，之后一直沿用它。
        QString spelling;

        // 归一后的值名 → 值。`(Default)` 用空字符串作键。
        QMap<QString, RegistryValue> values;

        /// 归一后的值名 → 首次写入时的拼法。
        QMap<QString, QString> valueSpellings;
    };

    QMap<QString, Entry> m_keys;
    QVector<QPair<QString, Files::ErrorCode>> m_failingKeys;
    QVector<QPair<QPair<QString, QString>, Files::ErrorCode>> m_failingValues;
    QVector<QPair<QPair<QString, QString>, Files::ErrorCode>> m_nextWriteFailures;
    int m_writeCalls = 0;
};

///
/// \brief 本平台原生的注册表后端。
///
/// Windows 上返回一个真正读写注册表的实现；其它平台返回一个
/// `isAvailable()` 为 false 的实现（"unsupported"），原因可从 `reason` 取到。
///
/// 返回的对象归调用方所有。
///
RegistryStore *createNativeRegistryStore();

///
/// \brief 本平台有没有「注册表」这一机制（PRD: PLAT-005 完成标准第 5 条）。
///
/// 与 `store->isAvailable()` 的区别值得留意，两者都有人用：
///   - 这个回答的是一个**平台事实**：Windows 有注册表，macOS/Linux 没有。
///     界面据此把「Shell 集成」整块置灰并解释原因。
///   - `isAvailable()` 回答的是「这个后端现在能不能写」。
///     预演用的内存后端永远是 true——它不是平台能力，是功能可用性。
///
/// 把界面置灰绑到前者，否则在预演或测试场景下界面会显示成「可用」。
///
bool platformHasRegistry();

/// 非 Windows 平台上「为什么不支持」的完整说明：原因 + 建议。
QString platformRegistryUnsupportedReason();
QString platformRegistryUnsupportedAdvice();

} // namespace Platform
} // namespace LqCompare

#endif // LQCOMPARE_REGISTRYSTORE_H
