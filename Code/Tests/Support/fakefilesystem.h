#ifndef LQCOMPARE_FAKEFILESYSTEM_H
#define LQCOMPARE_FAKEFILESYSTEM_H

#include "filesystem.h"
#include "pathutils.h"

#include <QHash>
#include <QStringList>

namespace LqCompare {
namespace Files {
namespace Test {

///
/// \brief 可注入故障的内存文件系统（PRD: PLAT-002 完成标准第 5 条）。
///
/// 为什么需要一个假实现
/// --------------------
/// PLAT-002 要求「异常路径（权限、占用、不存在）用假实现覆盖」。用真实文件系统
/// 制造这些情形既难又不可靠：
///   - 「权限不足」要改权限位，而测试进程若是 root 就完全不生效；
///   - 「文件被占用」在 Windows 上难以稳定复现，在 POSIX 上几乎没有对应概念；
///   - 「只读文件系统」需要一个只读挂载点，测试机上通常没有，有也不能动。
///
/// 于是这三类错误在真实文件系统上**几乎不可能被测到**，而它们恰恰是用户最容易
/// 遇到、最需要给出正确建议的错误。假实现让这些路径变成一行注入。
///
/// 它同时也是「抽象层可替换」这个设计要求的证据：如果业务代码偷偷绕开抽象层
/// 直接调用系统 API，那么用假实现跑测试时那些调用会去动真实磁盘，
/// 测试会立刻暴露出来。
///
/// 设计取舍：
///   - **不模拟真实文件内容**。本工具只需要元数据（大小、时间、属性），
///     加内容支持只会让假实现变复杂，而没人会用它测内容比对。
///   - **记录调用**。这样测试可以断言「调用了什么、没调用什么」，
///     尤其是断言「在报错前没有多走一步」。
///
/// 回收站不在这个替身的职责范围内：删除要走 TrashService（见 trash.h 的说明），
/// 相应的替身是 faketrashservice.h。这里刻意不提供 deleteToTrash，
/// 因为「删除只有一条入口」本身就是一条要被结构保证的约束。
///
class FakeFileSystem : public FileSystem
{
public:
    /// 可被单独注入故障的操作。粒度按接口方法划分，而不是按错误类型——
    /// 「同一个路径在 stat 时正常、在删除时报占用」是真实存在的场景。
    enum class Operation {
        PathNormalize,
        Stat,
        LinkTarget,
        Exists,
        Enumerate,
        SetTimes,
        SetAttributes,
    };

    FakeFileSystem();
    ~FakeFileSystem() override;

    // --- 构造文件树 --------------------------------------------------------

    /// 添加一个目录。会自动补齐路径上的各级父目录，
    /// 因此 addDirectory("/a/b/c") 之后 /a 与 /a/b 也都存在。
    void addDirectory(const QString &path);

    /// 添加一个文件。同样会自动补齐父目录。
    void addFile(const QString &path, quint64 size = 0);

    /// 标记为符号链接并设置其指向。
    void addSymLink(const QString &path, const QString &target);

    /// 移除一个条目（只从内存树里去掉，不产生任何删除记录）。
    void remove(const QString &path);

    void setModifiedTime(const QString &path, const FileTime &time);
    void setCreatedTime(const QString &path, const FileTime &time);
    void setSize(const QString &path, quint64 size);
    void setAttributesFor(const QString &path, FileAttributes attributes);

    // --- 故障注入 ----------------------------------------------------------

    /// 让某个操作在某个路径上失败并返回指定错误。
    /// 同一个操作不同路径可以注入不同错误；对父目录注入不影响的子路径。
    void fail(Operation operation, const QString &path, FileSystemError error);

    /// 取消全部故障注入。
    void clearFailures();

    // --- 平台语义切换 ------------------------------------------------------

    /// 切换成 Windows 语义（反斜杠、盘符、UNC、大小写不敏感）。
    void useWindowsSemantics();

    /// 切换成 POSIX 语义（正斜杠、大小写敏感）。
    void usePosixSemantics();

    // --- 调用观察 ----------------------------------------------------------

    /// 某个操作是否在某个路径上被调用过。
    bool wasCalled(Operation operation, const QString &path) const;

    /// 某个操作的总调用次数。
    int callCount(Operation operation) const;

    /// 清空调用记录（不清空文件树与故障注入）。
    void clearCallLog();

    /// 供测试断言用的可读调用日志，形如 "stat:/a/b"。
    QStringList callLog() const { return m_callLog; }

    // --- FileSystem 接口 ---------------------------------------------------

    Qt::CaseSensitivity caseSensitivity() const override;
    QChar separator() const override;
    QString pathNormalize(const QString &path, FileSystemError *error) const override;
    bool isAbsolutePath(const QString &path) const override;
    QString toNativePath(const QString &path) const override;
    FileInfo stat(const QString &path, FileSystemError *error) const override;
    QString linkTarget(const QString &path, FileSystemError *error) const override;
    bool exists(const QString &path, FileSystemError *error) const override;
    QVector<FileInfo> enumerateDirectory(const QString &path, FileSystemError *error) const override;
    bool setTimes(const QString &path, const FileTime &lastModified, const FileTime &lastAccessed,
                  FileSystemError *error) const override;
    bool setAttributes(const QString &path, FileAttributes attributes,
                       FileSystemError *error) const override;
    QString platformName() const override;

    /// 稳定标识，用于错误信息与测试名称。
    static const char *operationIdentifier(Operation operation);

private:
    /// 记录一次调用并查询是否注入了故障。
    /// 返回 true 表示「已注入故障」，此时 error 被写入注入的错误。
    bool intercept(Operation operation, const QString &path, FileSystemError *error) const;

    bool hasEntry(const QString &path) const;

    /// 取条目。返回非 const 指针，因为容器本身是 mutable——
    /// FileSystem 的写操作在接口上标为 const（它们改的是「文件系统」这个
    /// 外部状态而非对象自身），因此假实现必须在 const 方法里改自己的内存树。
    /// 这与调用日志、回收站记录的处理方式一致。
    FileInfo *findEntry(const QString &path) const;

    /// 注入键：操作 + 路径。用字符串拼接而不是结构体做键，
    /// 是因为路径本身不含 '|'，拼接不会产生歧义，且便于在日志里直接看懂。
    static QString failureKey(Operation operation, const QString &path);

    // m_entries 与 m_linkTargets 标为 mutable 的原因见 findEntry 的说明：
    // 接口把写操作定义为 const，而假实现需要真的改动自己的内存树。
    mutable QHash<QString, FileInfo> m_entries;          ///< 规范化路径 -> 元数据
    QHash<QString, FileSystemError> m_failures;          ///< 注入的故障
    mutable QHash<QString, QString> m_linkTargets;       ///< 链接路径 -> 指向

    // 这些字段在 const 方法里也要写，因此标为 mutable。
    // 调用记录属于「观测副产品」，不改变文件系统状态，不影响 const 语义。
    mutable QStringList m_callLog;

    PathUtils::Style m_style;
    Qt::CaseSensitivity m_caseSensitivity = Qt::CaseSensitive;
    QString m_platformName = QStringLiteral("fake");
};

} // namespace Test
} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_FAKEFILESYSTEM_H
