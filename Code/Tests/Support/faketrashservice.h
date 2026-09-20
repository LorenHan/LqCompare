#ifndef LQCOMPARE_FAKETRASHSERVICE_H
#define LQCOMPARE_FAKETRASHSERVICE_H

#include "trash.h"

#include <QHash>
#include <QStringList>
#include <QVector>

namespace LqCompare {
namespace Files {
namespace Test {

///
/// \brief 可注入故障的内存回收站（PRD: PLAT-003 完成标准第 4 条）。
///
/// 为什么需要它
/// ----------
/// PLAT-003 最要紧的一条约束是「回收站不可用时绝不静默降级为永久删除」。
/// 这条约束在真实系统上**几乎无法验证**：
///   - 要制造「网络盘没有回收站」，测试机上得先有一个网络挂载点；
///   - 要制造「配额已满」，Windows 的回收站配额还得先调注册表；
///   - 要制造「空间不足」，得把磁盘填满。
/// 三种情况在 CI 里都不可行，于是这条最关键的约束会变成「写了但没人验过」。
///
/// 假实现把可用性变成一行注入。更关键的是它提供 trashPathsCallCount()：
/// 不可用时可以断言**搬移函数一次都没被调用**，而不是只断言「返回了失败」——
/// 后者在「先搬移再返回失败」的实现里同样成立，测不出问题。
///
/// 它与真实实现共用 TrashService 基类的 deleteToTrash()，因此验证的是
/// 那条被所有平台共用的模板方法，而不是某个平台的特例。
///
class FakeTrashService : public TrashService
{
public:
    /// 可被观察的调用。粒度按对外接口划分，而不是按内部步骤——
    /// 测试关心的是「用户看到的这个动作有没有发生」。
    enum class Operation {
        Availability,   ///< availabilityFor()
        TrashPaths,     ///< trashPaths()，也就是真正搬动文件那一步
        UndoLastDelete, ///< undoLastDelete()
    };

    FakeTrashService();
    ~FakeTrashService() override;

    // --- 可用性注入 --------------------------------------------------------

    /// 所有路径的默认可用性。初始为 Available。
    void setDefaultAvailability(TrashAvailability availability);

    /// 单独设定某个路径（或其祖先目录）的可用性。
    /// 与文件系统的「对父目录注入不影响子路径」不同，回收站可用性是**按卷**的，
    /// 因此这里从路径向上找最近的已注入祖先——同一个卷下的所有条目答案一致，
    /// 这与真实实现的行为相符（Windows 上一整块盘要么有回收站要么没有）。
    void setAvailabilityFor(const QString &path, TrashAvailability availability);

    // --- 故障注入 ----------------------------------------------------------

    /// 让搬移某个路径时失败。其余条目不受影响——批量删除里
    /// 「一部分进了回收站、一部分没进」是真实存在的现场，必须能测。
    void failTrashPath(const QString &path, const ErrorCode &error);

    /// 让撤销失败（例如原位置已被别的文件占用）。
    void failUndo(const ErrorCode &error);

    // --- 观察 --------------------------------------------------------------

    /// 成功进入回收站的条目（原始路径，按顺序）。
    QStringList trashedPaths() const;

    /// 被**永久删除**的路径。这里永远是空的——如果哪天它不为空，
    /// 说明有代码绕过了「删除必须可逆」的约束。与 FakeFileSystem 的同名方法
    /// 是同一个用意。
    QStringList permanentlyDeletedPaths() const { return QStringList(); }

    /// 回收站里当前有哪些条目（回收站内的路径，按顺序）。
    QStringList trashContents() const;

    int callCount(Operation operation) const;
    QStringList callLog() const;
    void clearCallLog();

    /// 稳定标识，用于调用日志与测试名称。
    static const char *operationIdentifier(Operation operation);

    // --- TrashService 接口 -------------------------------------------------

    QString platformName() const override { return QStringLiteral("fake"); }
    TrashAvailability availabilityFor(const QString &path) const override;
    QString displayLocation() const override { return QStringLiteral("/fake/.Trash"); }
    bool undoLastDelete(ErrorCode *error) const override;

protected:
    TrashReport trashPaths(const QStringList &paths) const override;

private:
    QString normalize(const QString &path) const;

    /// 回收站里已有的名字 -> 条数。用于模拟「同名自动改名」：
    /// 真实实现（macOS / XDG）遇到重名都会改名，因此 trashedPath
    /// 不一定等于原目录下的同名路径。撤销必须用返回值而不是自己拼路径——
    /// 这个替身要能暴露「按原名拼路径」这个错误。
    mutable QHash<QString, int> m_nameCollisions;

    /// 已经进回收站的条目：回收站内路径 -> 原始路径。
    mutable QHash<QString, QString> m_trashContents;

    /// 注入的可用性：路径 -> 答案。查找时会向上找最近的祖先，
    /// 因此注入 "/mnt/net" 就能覆盖它下面的所有条目（按卷生效）。
    QHash<QString, TrashAvailability> m_availabilityByPath;
    QHash<QString, ErrorCode> m_trashFailures;

    mutable QVector<QString> m_trashedOriginals;
    mutable QStringList m_callLog;

    TrashAvailability m_defaultAvailability = TrashAvailability::Available;
    ErrorCode m_undoFailure;
};

} // namespace Test
} // namespace Files
} // namespace LqCompare

#endif // LQCOMPARE_FAKETRASHSERVICE_H
