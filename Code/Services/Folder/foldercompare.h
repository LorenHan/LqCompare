#ifndef LQCOMPARE_FOLDERCOMPARE_H
#define LQCOMPARE_FOLDERCOMPARE_H

#include "filesystem.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace LqCompare {
namespace Folder {

// 主状态：**互斥**集合。一个条目在任一时刻只落在其中一个取值上。
//
// Content state is independent of modification times; no common ancestor is
// available, so this two-way comparison never guesses which side was edited.
//
// `BothChanged` 与 `Conflict` 只可能由**有效基线**推导（DIR-011 第 3 条）：
// 没有基线时它们一个都不得出现。判据落在 `statusModelViolations()` 上，
// 由 `Tests/EntryStatus` 逐条钉住——「不会出现」这种否定命题不写成可执行的东西
// 就等于没验过。取值一律追加在末尾：`Status` 的整数值会进模型角色、筛选下拉与
// 报表/命令行映射，插在中间会把既有取值整体挪位。
enum class Status {
    Same, Different, LeftOnly, RightOnly, TypeConflict, Error, Unknown,
    BothChanged, Conflict
};
enum class Kind { Missing, File, Directory, SymbolicLink, Other };

// 内容证据：回答「内容这一维**比到了什么**」，与主状态正交（DIR-011 第 1 条）。
//
// 为什么另开一维而不是把「部分比较」做成第三种主状态：主状态那一栏回答
// 「结论是什么」，证据这一栏回答「结论建立在什么上」。混在一起必然长出一套
// 优先级规则，界面 / 报表 / 命令行各实现一遍（DIR-008 已经踩过这个坑）。
// 规格点名的五档是「字节相同 / 规则相同 / CRC 相同 / 未比较 / 部分比较」；
// 另加三档「…不同」是必需的：证据这一维若不能表达「比过且不同」，
// 最常见的那种情形就只能靠主状态反推，两维立刻失去独立性。
enum class ContentEvidence {
    NotCompared,   // 未比较
    Partial,       // 部分比较（只比较了前 N 字节，不足以判定相同）
    ByteIdentical, // 字节相同
    ByteDifferent, // 字节不同
    RuleIdentical, // 规则相同
    RuleDifferent, // 规则不同
    CrcIdentical,  // CRC 相同
    CrcDifferent,  // CRC 不同
};

// 时间关系：独立于主状态与内容证据的第三维（DIR-011 第 2 条）。
// 比较的是 UTC 时刻（`Files::FileTime` 本身就是 UTC 纳秒），不是本地显示时间。
enum class TimeRelation { Unknown, LeftNewer, RightNewer, Same };

// 共同祖先的一条记录（DIR-011 第 3 条）。
//
// 只消费「大小 + 修改时间」两项：它们是比对侧**唯一握有**的事实。
// `Sync::Baseline` 另存 SHA-256，那是写入侧为了计划重放用的；比对侧不引入它，
// 因为「两侧均改」的判定必须在**一条内容都没读完**时也能给出（内容比对可能是
// 关闭的）。哈希这一维等 DIR-007 落地后再往这里加，届时只多一个可空字段。
struct AncestorRecord
{
    quint64 size = 0;
    // 用 `FileTime` 而不是裸 qint64：默认构造的 `FileTime` 是**无效**的，
    // 而 0 纳秒是一个合法时刻（纪元）。裸整数会让两者长得一样，
    // 于是「这条祖先记录没有时间」会被读成「时间正好是 1970-01-01」。
    Files::FileTime modified;
    bool isValid() const { return modified.isValid(); }
};

// 有效基线：必须同时绑定两侧根目录，且至少有一条祖先记录。
// 只给一个 `QHash` 而不绑根目录，等于让「同名不同树」的两对目录互相冒充基线。
struct BaselineView
{
    QString leftRoot;
    QString rightRoot;
    QHash<QString, AncestorRecord> ancestors;
    bool valid = false;
    bool hasAncestorFor(const QString &relativePath) const
    {
        return ancestors.contains(relativePath);
    }
};

QString statusLabel(Status status);

// 逐字节比较时单次读入内存的块大小上界（DIR-008 第 3 条）。
// 公开成常量而不是留在 .cpp 里，是为了让「分块读取且块大小有上界」这条验收标准
// 能被测试直接钉住。若把 256 KiB 藏进实现，测试就只能去匹配源码里的字面量，
// 那是第二份事实来源，改一次实现就得跟着改一次测试。
inline constexpr qint64 kMaximumCompareBlockSize = 256 * 1024;

struct Side
{
    Files::FileInfo info;
    Kind kind = Kind::Missing;
    QString error;
    bool exists() const { return info.exists; }
};

struct Entry
{
    QString relativePath;
    Side left;
    Side right;
    Status status = Status::Unknown;
    QString explanation;
    qint64 firstDifference = -1;
    bool excludedByMask = false;
    bool hasIncludedDescendants = false;
    bool nameCaseDifference = false;
    QString filterReason;
    // 只比较了文件的前若干字节时成立（DIR-008 第 2 条「部分比较」）。
    //
    // 这里**不再单独存一个布尔字段**：那会让「部分比较」同时存在两份说法
    // （一个布尔 + 证据维度里的 `Partial`），而两份说法迟早会分叉。现在
    // 唯一的存储是 `contentEvidence`，本方法只是它的一个视图。
    bool partialComparison() const { return contentEvidence == ContentEvidence::Partial; }
    // 内容证据（DIR-011 第 1 条后半句）。
    ContentEvidence contentEvidence = ContentEvidence::NotCompared;
    // 时间关系（DIR-011 第 2 条）。两侧有一侧不存在时为 Unknown：
    // 「孤儿项没有时间关系」和「两侧时间一样」是两件事，不能塌成一档。
    TimeRelation timeRelation = TimeRelation::Unknown;
    bool inComparison() const { return !excludedByMask || hasIncludedDescendants; }
    bool isDirectory() const;
    bool canCompareAsText() const;
};

struct Options
{
    // `recursive` 与 `maximumDepth` 这两个字段就是 DIR-003 的「三档递归策略」
    // 的全部存储——「不进子目录 / 只进一层 / 一路递归」都由它们的组合表达，
    // **不另存第三个字段**。档位到这两个字段的映射（以及反查）在
    // `recursionstrategy.h`；要改三档的行为请改那一处，不要在这里加字段。
    //
    // 注意 `maximumDepth` 的初值必须等于 `recursionstrategy.h` 的
    // `kDefaultFullDepth`，否则「缺省选项」反查出来的档位不是「完全递归」，
    // 界面下拉一打开就显示错档。`Tests/Folder` 有一条用例钉住这个等式。
    bool recursive = true;
    bool compareContent = true;
    int maximumDepth = 128;
    QString scanMaskDeclaration;
    Qt::CaseSensitivity nameCaseSensitivity = Qt::CaseSensitive;
    // 「只比较前 N 字节」的快速模式（DIR-008 第 2 条）。0 = 关闭，比较到文件末尾。
    // 字段只能加在末尾：本结构体在仓库里存在按位置聚合初始化的写法，
    // 插在中间会把既有调用点的值整体错位（与 OptionDefinition 同一条纪律）。
    qint64 compareFirstBytes = 0;
};

struct Result
{
    QString leftRoot;
    QString rightRoot;
    QVector<Entry> entries; // Deterministic, parent before children.
    QStringList warnings;
    QString error;
    bool cancelled = false;
    bool complete = true; // Complete within the declared scan-mask scope.
    int excludedCount = 0;
    QString scanMaskDeclaration;
    // 本次比较是否真的用上了有效基线（DIR-011 第 3 条）。界面要拿它解释
    // 「为什么这一条不是两侧均改」——没有这一位，界面只能靠猜。
    bool baselineApplied = false;
};

using Progress = std::function<void(int entries, const QString &relativePath)>;

// Synchronous, UI-independent engine. Filesystem injection covers inaccessible
// entries without relying on the permissions of the user running a test.
//
// `baseline` 只影响一个方向（DIR-011 第 3 条）：有**有效**基线时，一个本会判成
// 不同的条目可能被细化为「两侧均改」或「冲突」；没有基线时指针为空，
// `BothChanged` / `Conflict` 一个都不会出现。参数加在末尾，既有按位置调用不受影响。
Result compare(const QString &leftRoot, const QString &rightRoot,
               const Options &options = {}, const std::atomic_bool *cancelled = nullptr,
               const Progress &progress = {}, const Files::FileSystem *fileSystem = nullptr,
               const BaselineView *baseline = nullptr);

class Scanner : public QObject
{
    Q_OBJECT
public:
    explicit Scanner(QObject *parent = nullptr);
    ~Scanner() override;
    bool isRunning() const { return m_thread != nullptr; }
    bool start(const QString &leftRoot, const QString &rightRoot, const Options &options = {});
    void cancel();

signals:
    void progressChanged(int entries, const QString &relativePath);
    void finished(const LqCompare::Folder::Result &result);

private:
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace Folder
} // namespace LqCompare

Q_DECLARE_METATYPE(LqCompare::Folder::Result)

#endif
