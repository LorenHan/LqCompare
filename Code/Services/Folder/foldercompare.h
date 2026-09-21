#ifndef LQCOMPARE_FOLDERCOMPARE_H
#define LQCOMPARE_FOLDERCOMPARE_H

#include "filesystem.h"

#include <QObject>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace LqCompare {
namespace Folder {

// Content state is independent of modification times; no common ancestor is
// available, so this two-way comparison never guesses which side was edited.
enum class Status { Same, Different, LeftOnly, RightOnly, TypeConflict, Error, Unknown };
enum class Kind { Missing, File, Directory, SymbolicLink, Other };

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
    // 是否只比较了文件的前若干字节（DIR-008 第 2 条「部分比较」）。
    // 与 status 正交：status 回答「结论是什么」，本字段回答「结论覆盖了多少内容」。
    // 只在「限内全部相同、但文件还没读完」时为真——一旦在限内发现差异，
    // 差异已经被证明，结论不再是不完整的，因此那时它保持 false。
    bool partialComparison = false;
    bool inComparison() const { return !excludedByMask || hasIncludedDescendants; }
    bool isDirectory() const;
    bool canCompareAsText() const;
};

struct Options
{
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
};

using Progress = std::function<void(int entries, const QString &relativePath)>;

// Synchronous, UI-independent engine. Filesystem injection covers inaccessible
// entries without relying on the permissions of the user running a test.
Result compare(const QString &leftRoot, const QString &rightRoot,
               const Options &options = {}, const std::atomic_bool *cancelled = nullptr,
               const Progress &progress = {}, const Files::FileSystem *fileSystem = nullptr);

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
