/// \file
/// \brief Linux 的系统图标提供者（PRD: PLAT-004 完成标准第 1、4 条）。
///
/// **本文件在本项目的开发机（macOS）上从未被编译过，也未在 Linux 上跑过。**
/// 如实标出来：下面用手写的主题图标名与 QMimeDatabase 的调用，
/// 只能靠代码审查与 Qt 的 API 契约来保证正确。
///
/// 与 macOS / Windows 的实现有个本质区别：Linux 上「系统图标」其实是
/// **桌面主题提供的一组命名图标**，不是「按扩展名问系统要一张图」。
/// 于是流程是两段：
///   1. 扩展名 --(shared-mime-info)--> MIME 类型
///   2. MIME 类型 --(图标主题)------> 主题里的一个图标名 --(QIcon::fromTheme)--> 图
/// 第二段在无桌面环境时必然失败（没有主题），此时回退到内置图标——
/// 这正是完成标准第 4 条要的行为，也是这个平台上最常见的一种「不支持」。

#include "iconkey.h"
#include "iconservice.h"

// 只在非 Windows、非 macOS 上编译。理由见 iconservice_mac.mm 的说明：
// 三个实现引用的框架互不相容，不能放在同一个编译单元里。
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)

#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace LqCompare {
namespace Platform {

namespace {

///
/// \brief 无桌面环境时的兜底。
///
/// 存在的意义与 UnsupportedTrashService 相同：让「永不返回 nullptr」与
/// 「拿不到就明确说拿不到」这两条契约成立。这里其实是给
/// `createNativeIconProvider()` 在 Linux 上的正常路径用的——
/// 主题缺失、没有 XDG 目录、容器里只有一个 busybox，都会走到它。
///
class HeadlessIconProvider : public IconProvider
{
public:
    QString platformName() const override { return QStringLiteral("linux-headless"); }

    IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) override
    {
        Q_UNUSED(extensionKey);
        Q_UNUSED(isDirectory);
        Q_UNUSED(pixelSize);
        // 返回 Missing，IconService 会统一回退到内置图标集。
        return IconEntry();
    }
};

///
/// \brief 按 XDG 图标主题取图。
///
/// `QIcon::fromTheme` 会依次查 `$XDG_DATA_HOME/icons`、`$XDG_DATA_DIRS/icons`
/// 与 `~/.icons`，并按 `index.theme` 里的继承链找替代主题。找不到时返回
/// **空 QIcon**（不是异常），因此「拿不到」这件事要靠 `isNull()` 判断——
/// 忘了判断的话，缓存里会存进一堆空图标，而界面上表现为「所有条目都没有图标」，
/// 看不出是「主题缺失」还是「我们的键算错了」。
///
QIcon iconFromTheme(const QString &name, int pixelSize)
{
    if (name.isEmpty())
        return QIcon();

    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        return icon;

    // 先确认这个尺寸真的能取到像素：主题里可能只有更大的几档，
    // 而 QIcon 会在需要时自行缩放。这一步不做也不影响正确性，
    // 但能让「主题存在却只有 512 的图」这种情况在日志里看得出来。
    if (icon.pixmap(pixelSize, pixelSize).isNull())
        return QIcon();

    return icon;
}

class LinuxIconProvider : public IconProvider
{
public:
    QString platformName() const override { return QStringLiteral("linux"); }

    IconEntry resolve(const QString &extensionKey, bool isDirectory, int pixelSize) override;
};

IconEntry LinuxIconProvider::resolve(const QString &extensionKey, bool isDirectory, int pixelSize)
{
    IconEntry entry;

    if (pixelSize <= 0)
        return entry;

    QStringList candidates;

    if (isDirectory) {
        // 目录没有 MIME 类型可查（目录本来就不是「文件类型」），
        // 直接用约定俗成的主题名。`folder` 是 freedesktop 图标命名规范里
        // 的必备名，任何合规主题都必须提供它。
        candidates << QStringLiteral("folder");
    } else {
        // 拼一个只用来提供扩展名的假名字。用 MatchExtension 而不是
        // MatchDefault：后者会去读磁盘，而这里**不能**读磁盘——
        // 对不存在的路径也要能给出图标（与 Windows 的
        // SHGFI_USEFILEATTRIBUTES 是同一个理由）。
        const QString probe = QStringLiteral("probe.")
                + (extensionKey.isEmpty() ? QStringLiteral("dat") : extensionKey);

        const QMimeDatabase database;
        const QMimeType type =
                database.mimeTypeForFile(probe, QMimeDatabase::MatchExtension);

        // 顺序很重要：先具体（iconName），再通用（genericIconName）。
        // 倒过来的话，一个 .odt 会显示成通用的「文档」图标，
        // 而主题里明明有专门的文字处理图标。
        candidates << type.iconName();
        candidates << type.genericIconName();
        // 最后再兜一层：连 MIME 都没认出来时，用规范里的通用文件图标名。
        candidates << QStringLiteral("text-x-generic");
    }

    const int pixels = pixelSize;

    for (const QString &name : qAsConst(candidates)) {
        const QIcon icon = iconFromTheme(name, pixels);
        if (icon.isNull())
            continue;

        entry.source = IconSource::System;
        entry.actualPixelSize = pixels;
        entry.payload = QVariant::fromValue(icon);
        return entry;
    }

    // 候选全试完都拿不到：交给 IconService 回退到内置图标。
    return entry;
}

/// 本机有没有可用的图标主题。
///
/// 判据是「主题里能不能取到 folder 这个必备名」——比自己去看
/// `$XDG_DATA_DIRS` 目录是否存在更可靠：目录在但主题被裁剪掉的容器镜像
/// 很常见（只有 hicolor 里零散几个图标）。
bool hasUsableIconTheme()
{
    return !iconFromTheme(QStringLiteral("folder"), 16).isNull();
}

} // namespace

IconProvider *createLinuxIconProvider()
{
    if (!hasUsableIconTheme())
        return new HeadlessIconProvider();
    return new LinuxIconProvider();
}

} // namespace Platform
} // namespace LqCompare

#endif // !Q_OS_WIN && !Q_OS_MACOS
