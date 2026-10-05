// Windows 回收站的保守边界（PRD: PLAT-003 / #324）。
//
// 旧后端以 SHQueryRecycleBin 成功作为放行条件，然后用 SHFileOperation +
// FOF_ALLOWUNDO | FOF_NOCONFIRMATION 删除。前者只查询大小/数量，后者只在
// 可能时保留撤销信息，不能保证配额、策略变化等情况下不退化为永久删除。
// 在可验证的拒绝永久删除方案落地前，必须保留原件，不能用成功返回值冒充可恢复。
//
// 此后端刻意不包含 Windows/Shell API，也不读写文件或枚举系统回收站。
// 测试因此可在任意平台直接执行同一生产实现，验证拒绝后文件完整保留。

#include "trash.h"

namespace LqCompare {
namespace Files {

class WindowsTrashService final : public TrashService
{
public:
    QString platformName() const override { return QStringLiteral("windows"); }

    TrashAvailability availabilityFor(const QString &path) const override
    {
        Q_UNUSED(path);
        // 表示本应用尚未提供安全删除能力，不表示 Windows 本身没有回收站。
        // 不查询路径是否存在：未开放的能力优先于文件存在性诊断。
        return TrashAvailability::RecoverabilityNotGuaranteed;
    }

    QString displayLocation() const override
    {
        // 系统回收站仍可供用户自行打开；此命名空间不是普通磁盘文件路径。
        return QStringLiteral("shell:RecycleBinFolder");
    }

    bool undoLastDelete(ErrorCode *error) const override
    {
        // 本后端尚未实现自动还原，不伪造成功，也不消费或猜测任何回收站条目。
        if (error)
            *error = FileSystemError::NotSupported;
        return false;
    }

protected:
    TrashReport trashPaths(const QStringList &paths) const override
    {
        // 非空批次会被基类的可用性检查挡住；即使未来入口改变，这里仍只拒绝。
        // 空批次保留公共约定：无操作、无成功条目、空报告视为成功。
        TrashReport report;
        for (const QString &path : paths) {
            TrashRecord record;
            record.originalPath = path;
            record.error = FileSystemError::NotSupported;
            report.records.append(record);
        }
        return report;
    }
};

TrashService *createWindowsTrashService()
{
    return new WindowsTrashService();
}

} // namespace Files
} // namespace LqCompare
