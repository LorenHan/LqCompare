/// \file
/// \brief 非 Windows 平台上的注册表后端桩（PRD: PLAT-005 完成标准第 5 条）。
///
/// 完成标准第 5 条是「非 Windows 平台该能力置灰并说明」。这句要求里有两个字
/// 常被做丢：**说明**。把入口藏起来或直接报「不支持」都不算说明——
/// 用户想知道的是「为什么这里没有」以及「那我想达到同样目的该怎么做」。
/// 所以这里返回的是一整段原因 + 一条可执行的替代路径，而不是一个 bool。
///
/// 本文件本身是平台无关的：它在 macOS 与 Linux 上都会被编译，
/// 因此它的行为可以被真实执行（测试里就有这么几条）。真正只有 Windows 才有的
/// 是 registrystore_win.cpp。

#include "registrystore.h"

namespace LqCompare {
namespace Platform {

namespace {

///
/// \brief 「本平台没有注册表」的后端。
///
/// 它的存在让 `createNativeRegistryStore()` 有一个**永不返回 nullptr** 的契约。
/// 返回空指针会让每个调用点都要判空，而漏判的那一处就是一次崩溃；
/// 返回一个「如实说自己不可用」的对象，调用方只需要问一次 `isAvailable()`。
/// 这与 trash.h 里的 `UnsupportedTrashService`、iconservice_linux.cpp 里的
/// `HeadlessIconProvider` 是同一个模式。
///
class UnsupportedRegistryStore : public RegistryStore
{
public:
    QString backendName() const override { return QStringLiteral("unsupported"); }

    bool isAvailable(QString *reason) const override
    {
        if (reason != nullptr)
            *reason = platformRegistryUnsupportedReason();
        return false;
    }

    // 读：一律给出「什么都没有」。
    //
    // 这里刻意**不**报错。原因与 Files/ 里的假实现一致：读操作报错的唯一用处
    // 是让调用方区分「不存在」与「读不到」，而在一个连注册表这个概念都没有的
    // 平台上，两者对调用方的处置完全相同——按「没有」处理即可。
    // 真正的告警由 isAvailable() 一次性给出，不该在每个读操作上重复。
    bool keyExists(const QString &key) const override
    {
        Q_UNUSED(key);
        return false;
    }

    bool value(const QString &key, const QString &name, RegistryValue *out) const override
    {
        Q_UNUSED(key);
        Q_UNUSED(name);
        Q_UNUSED(out);
        return false;
    }

    QStringList subKeys(const QString &key) const override
    {
        Q_UNUSED(key);
        return QStringList();
    }

    QStringList valueNames(const QString &key) const override
    {
        Q_UNUSED(key);
        return QStringList();
    }

    // 写：同样不给「假成功」。
    //
    // 这一处的选择比读更值得说明。若在这里返回 true（假装写成功），
    // 安装流程会一路走完并报告「62 项全部成功」——用户在 macOS 上看到
    // 「Shell 集成已安装」，然后一个菜单项都不会出现，且没有任何提示。
    // 因此这里返回 false 并带上 `NotSupported`：安装报告会显示每一项都失败，
    // 原因写着「本平台没有注册表」。难看，但真实。
    bool setValue(const QString &key, const QString &name, const RegistryValue &value,
                  Files::ErrorCode *error) override
    {
        Q_UNUSED(key);
        Q_UNUSED(name);
        Q_UNUSED(value);
        if (error != nullptr)
            *error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        return false;
    }

    bool removeValue(const QString &key, const QString &name, Files::ErrorCode *error) override
    {
        Q_UNUSED(key);
        Q_UNUSED(name);
        if (error != nullptr)
            *error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        return false;
    }

    bool removeKey(const QString &key, Files::ErrorCode *error) override
    {
        Q_UNUSED(key);
        if (error != nullptr)
            *error = Files::ErrorCode(Files::FileSystemError::NotSupported);
        return false;
    }
};

} // namespace

RegistryStore *createNativeRegistryStore()
{
    return new UnsupportedRegistryStore();
}

bool platformHasRegistry()
{
    return false;
}

QString platformRegistryUnsupportedReason()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral(
            "「Shell 集成」使用 Windows 注册表实现（HKEY_CURRENT_USER\\Software\\Classes 下的"
            "右键菜单项与文件关联），macOS 没有对应的机制。");
#else
    return QStringLiteral(
            "「Shell 集成」使用 Windows 注册表实现（HKEY_CURRENT_USER\\Software\\Classes 下的"
            "右键菜单项与文件关联），本平台没有注册表。");
#endif
}

QString platformRegistryUnsupportedAdvice()
{
#if defined(Q_OS_MACOS)
    // 给一条**可执行**的替代路径，而不是「暂不支持」四个字。
    // 用户看到「不支持」之后的下一个问题是「那我怎么办」。
    return QStringLiteral(
            "macOS 的等价能力是 Finder 的「服务」（Services）与「快速操作」，"
            "需要用 Automator 建一个调用本程序的服务，或自建一个应用包并把它拖进"
            "「系统设置 → 隐私与安全性 → 扩展」里的「访达扩展」。"
            "这一步需要你自己确认，本程序不会自动修改系统设置。"
            "在此之前，可以直接把两个文件或文件夹拖到本程序窗口里进行比较。");
#else
    return QStringLiteral(
            "本平台可按桌面环境自建文件管理器的自定义动作（例如 KDE 的 Dolphin Service Menu、"
            "GNOME 的 Nautilus Scripts 或 nemo-action），但各家格式互不兼容，"
            "本程序不自动写入。在此之前，可以直接把两个文件或文件夹拖到本程序窗口里进行比较。");
#endif
}

} // namespace Platform
} // namespace LqCompare
