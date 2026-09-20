/// \file
/// \brief 注册表存储的 Windows 原生后端（PRD: PLAT-005）。
///
/// **本文件在本项目的开发机（macOS）上从未被编译过。**
/// 这一点必须如实标出来，与 filesystem_win.cpp / trash_win.cpp / iconservice_win.cpp
/// 是同一类遗留，见 docs/development/current-handoff.md 的「已知未验证」一节。
///
/// 正因为它无法在开发机上验证，本文件刻意写得**薄**：
/// 所有规则（写哪些键、卸载怎么还原、残留怎么查、大小写怎么归一）
/// 都在 registrystore.cpp 与 shellintegration.cpp 里，那两份在任意平台都会被
/// 真实执行。这里只剩「一次系统调用换一次」的直译，出错面因此小得多。
///
/// 三条必须写对的细节（都来自 Windows 注册表本身的语义）：
///   1. **全部用 `*W` 后缀**。不带后缀的 `RegCreateKeyEx` 按 UNICODE 宏展开，
///      行为取决于编译参数；带 `A` 的版本会让含中文的键名或路径乱码。
///      本文件由 tools/check_winapi.py 在构建期强制（清单里已登记这批 API）。
///   2. **`REG_MULTI_SZ` 之类的类型必须原样读回**，不能当成「读不到」——
///      否则安装时会把用户原有的关联记成「本来没有」，卸载时删掉它。
///      这一条对应 RegistryValueKind::Unsupported 那一档。
///   3. **写入字符串要带上结尾的 NUL**（长度用 `(字符数 + 1) * 2`）。
///      少了它，值在 regedit 里看起来正常，但某些读取方会把结尾的
///      垃圾字节一起读进去。

#include "registrystore.h"

#include <QString>

#include <algorithm>

#include <windows.h>

namespace LqCompare {
namespace Platform {

namespace {

///
/// \brief 注册表视图标志：**必须**带上，否则 32 位版本写进去的菜单项资源管理器看不见。
///
/// 这是一个非常经典、而且症状极具误导性的坑：
///   - 本项目的交付目标是 **Windows MinGW 8.1.0 32 位**，而 64 位 Windows 上的
///     资源管理器是 64 位进程；
///   - 注册表的 `HKCU\Software\Classes`（以及 HKLM 的 `Software\Classes`）
///     受 WOW64 重定向：32 位进程不带此标志时，读写的是
///     `...\WOW6432Node\Software\Classes` 这一份**影子副本**；
///   - 于是安装"成功"、卸载"成功"、校验也"通过"（因为校验用的是同一个 32 位
///     进程与同一份影子副本），**但右键菜单里什么都没有**。
///     用户会以为「这软件没实现这个功能」，而不是「写错了注册表位置」——
///     开发机上如果用的是 32 位资源管理器或 32 位调试器，也复现不出来。
///
/// 因此读与写都要显式指定 64 位视图，两边保持一致。
/// 只给写加是更糟的一种：写完立刻读不到，校验会报「装有但没写进去」。
///
/// 例外：`HKEY_CLASSES_ROOT` 是 HKLM 与 HKCU 的合并视图，不受 WOW64 标志影响
/// （给它加上通常无副作用，但语义上不同）。本项目的 Shell 集成只写 HKCU。
///
constexpr REGSAM kRegistryView = KEY_WOW64_64KEY;

/// 把 QString 转成可以直接传给 Win32 宽字符 API 的指针。
///
/// `QString` 内部就是 UTF-16，而 Windows 的 `wchar_t` 也是 16 位，
/// 因此这里不需要任何转码——转码反而是错的那一步（会引入一次
/// 「本地代码页 → UTF-16」的猜测）。Qt 在 Windows 上自己的实现也是这么做的。
const wchar_t *widePointer(const QString &text)
{
    return reinterpret_cast<const wchar_t *>(text.utf16());
}

/// 值名允许为空（表示 `(Default)`），此时必须传 nullptr 而不是指向空串的指针。
const wchar_t *wideNamePointer(const QString &name)
{
    return name.isEmpty() ? nullptr : reinterpret_cast<const wchar_t *>(name.utf16());
}

/// 一个完整键路径的「根」部分。Windows 上只有这几个预定义句柄。
struct HiveInfo
{
    QString name;
    HKEY handle = nullptr;
};

const HiveInfo kHives[] = {
    {QStringLiteral("HKEY_CURRENT_USER"), HKEY_CURRENT_USER},
    {QStringLiteral("HKEY_LOCAL_MACHINE"), HKEY_LOCAL_MACHINE},
    {QStringLiteral("HKEY_CLASSES_ROOT"), HKEY_CLASSES_ROOT},
    {QStringLiteral("HKEY_USERS"), HKEY_USERS},
    {QStringLiteral("HKEY_CURRENT_CONFIG"), HKEY_CURRENT_CONFIG},
};

/// 把 `HKEY_CURRENT_USER\Software\Classes` 拆成句柄 + 相对路径。
/// 认不出根时 `handle` 为 nullptr，调用方据此报「路径写错了」。
void splitHive(const QString &fullPath, HKEY *handle, QString *relative)
{
    *handle = nullptr;
    *relative = QString();

    const int cut = fullPath.indexOf(QLatin1Char('\\'));
    const QString head = (cut < 0 ? fullPath : fullPath.left(cut)).toUpper();
    const QString tail = (cut < 0 ? QString() : fullPath.mid(cut + 1));

    for (const HiveInfo &hive : kHives) {
        if (hive.name == head) {
            *handle = hive.handle;
            *relative = tail;
            return;
        }
    }
}

class Win32RegistryStore : public RegistryStore
{
public:
    /// `rootPath` 形如 `HKEY_CURRENT_USER`（默认）或 `HKEY_CURRENT_USER\Software`。
    explicit Win32RegistryStore(const QString &rootPath);
    ~Win32RegistryStore() override;

    QString backendName() const override { return QStringLiteral("win32-registry"); }
    bool isAvailable(QString *reason) const override;

    bool keyExists(const QString &key) const override;
    bool value(const QString &key, const QString &name, RegistryValue *out) const override;
    QStringList subKeys(const QString &key) const override;
    QStringList valueNames(const QString &key) const override;

    bool setValue(const QString &key, const QString &name, const RegistryValue &value,
                  Files::ErrorCode *error) override;
    bool removeValue(const QString &key, const QString &name, Files::ErrorCode *error) override;
    bool removeKey(const QString &key, Files::ErrorCode *error) override;

private:
    /// 把调用方的相对键拼成完整路径。
    QString fullPath(const QString &key) const;

    /// 打开一个已存在的键。失败返回 false（不区分失败原因，两种情况调用方
    /// 的处理都一样：读不到就是读不到）。
    bool openKey(const QString &key, REGSAM access, HKEY *handle) const;

    /// 打开或创建（含中间层）。返回 Win32 状态码，成功时 `handle` 有效。
    LONG openOrCreateKey(const QString &key, HKEY *handle) const;

    /// 只带相对路径的键，去掉根。
    QString m_relativeRoot;
    HKEY m_hive = nullptr;
};

Win32RegistryStore::Win32RegistryStore(const QString &rootPath)
{
    splitHive(rootPath, &m_hive, &m_relativeRoot);
}

Win32RegistryStore::~Win32RegistryStore() = default;

bool Win32RegistryStore::isAvailable(QString *reason) const
{
    // 根认不出来是配置写错，不是平台不支持——这种情况必须报出来，
    // 否则会表现成「所有读写都静默失败」，最难查的那一类。
    if (m_hive == nullptr) {
        if (reason != nullptr) {
            *reason = QStringLiteral("注册表根路径无法识别：%1"
                                     "（可用的是 HKEY_CURRENT_USER / HKEY_LOCAL_MACHINE / "
                                     "HKEY_CLASSES_ROOT / HKEY_USERS / HKEY_CURRENT_CONFIG）")
                    .arg(m_relativeRoot);
        }
        return false;
    }
    if (reason != nullptr)
        reason->clear();
    return true;
}

QString Win32RegistryStore::fullPath(const QString &key) const
{
    const QString normalized = RegistryStore::normalizeKey(key);
    if (m_relativeRoot.isEmpty())
        return normalized;
    if (normalized.isEmpty())
        return m_relativeRoot;
    return m_relativeRoot + QLatin1Char('\\') + normalized;
}

bool Win32RegistryStore::openKey(const QString &key, REGSAM access, HKEY *handle) const
{
    if (!isAvailable(nullptr))
        return false;

    const QString path = fullPath(key);
    if (path.isEmpty())
        return false;

    return RegOpenKeyExW(m_hive, widePointer(path), 0, access | kRegistryView, handle)
            == ERROR_SUCCESS;
}

LONG Win32RegistryStore::openOrCreateKey(const QString &key, HKEY *handle) const
{
    if (!isAvailable(nullptr))
        return ERROR_BAD_PATHNAME;

    QString path = fullPath(key);
    if (path.isEmpty())
        return ERROR_BAD_PATHNAME;

    // 键名必须按原样保留大小写（ProgID 惯例是 `Vendor.Product`），
    // 所以这里不能把归一后的路径直接拿去创建——那会把 `LqCompare.DiffFile`
    // 写成 `lqcompare.difffile`，regedit 里看起来像是随手敲的。
    // 归一后的路径只用于「查找」。
    const QStringList parts = key.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    QString built = m_relativeRoot;
    for (const QString &part : parts) {
        built = built.isEmpty() ? part : built + QLatin1Char('\\') + part;
    }
    if (built.isEmpty())
        return ERROR_BAD_PATHNAME;

    DWORD disposition = 0;
    return RegCreateKeyExW(m_hive, widePointer(built), 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE | kRegistryView,
                           nullptr, handle, &disposition);
}

bool Win32RegistryStore::keyExists(const QString &key) const
{
    HKEY handle = nullptr;
    if (!openKey(key, KEY_READ, &handle))
        return false;
    RegCloseKey(handle);
    return true;
}

bool Win32RegistryStore::value(const QString &key, const QString &name, RegistryValue *out) const
{
    HKEY handle = nullptr;
    if (!openKey(key, KEY_READ, &handle))
        return false;

    const QString normalizedName = RegistryStore::normalizeName(name);

    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExW(handle, wideNamePointer(normalizedName), nullptr, &type,
                                   nullptr, &size);
    if (status != ERROR_SUCCESS) {
        RegCloseKey(handle);
        return false;
    }

    QByteArray buffer(static_cast<int>(size) + 4, '\0');
    DWORD capacity = static_cast<DWORD>(buffer.size());
    status = RegQueryValueExW(handle, wideNamePointer(normalizedName), nullptr, &type,
                              reinterpret_cast<BYTE *>(buffer.data()), &capacity);
    RegCloseKey(handle);

    if (status != ERROR_SUCCESS)
        return false;
    buffer.truncate(static_cast<int>(capacity));

    RegistryValue parsed;
    switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
        // 按 UTF-16 解，并在第一个 NUL 处截断——
        // 值里可能带着 NUL 之后的残留（写入方多写了几个字节就会这样）。
        const int units = buffer.size() / 2;
        QString text = QString::fromUtf16(
                reinterpret_cast<const ushort *>(buffer.constData()), units);
        const int nul = text.indexOf(QChar(0));
        if (nul >= 0)
            text.truncate(nul);
        parsed = (type == REG_SZ) ? RegistryValue::of(text) : RegistryValue::expandable(text);
        break;
    }
    case REG_DWORD: {
        if (buffer.size() < 4)
            return false;
        const quint32 number = static_cast<quint32>(
                static_cast<quint8>(buffer.at(0))
                | (static_cast<quint32>(static_cast<quint8>(buffer.at(1))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(buffer.at(2))) << 16)
                | (static_cast<quint32>(static_cast<quint8>(buffer.at(3))) << 24));
        parsed = RegistryValue::ofNumber(number);
        break;
    }
    default:
        // 不认识的类型原样留着。**绝不能**返回 false：
        // 那会让「备份旧值」把「有一个我不认识的值」记成「本来没有值」，
        // 于是卸载时删掉它——那是销毁用户数据。
        parsed = RegistryValue::ofRaw(static_cast<quint32>(type), buffer);
        break;
    }

    if (out != nullptr)
        *out = parsed;
    return true;
}

QStringList Win32RegistryStore::subKeys(const QString &key) const
{
    QStringList result;

    HKEY handle = nullptr;
    if (!openKey(key, KEY_READ, &handle))
        return result;

    // 键名长度上限是 255 字符，1600 已经足够，留出余量是为了不写魔数。
    QVector<wchar_t> buffer(1600);

    for (DWORD index = 0;; ++index) {
        DWORD length = static_cast<DWORD>(buffer.size());
        const LONG status = RegEnumKeyExW(handle, index, buffer.data(), &length,
                                          nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status == ERROR_MORE_DATA)
            continue; // 名字超长，跳过这一个（不可能出现在我们自己写的键里）
        if (status != ERROR_SUCCESS)
            break;

        result << QString::fromWCharArray(buffer.constData(), static_cast<int>(length));
    }

    RegCloseKey(handle);

    std::sort(result.begin(), result.end());
    return result;
}

QStringList Win32RegistryStore::valueNames(const QString &key) const
{
    QStringList result;

    HKEY handle = nullptr;
    if (!openKey(key, KEY_READ, &handle))
        return result;

    // 值名上限是 16383 字符，一次到位比反复扩容简单，代价只是 32 KB 栈外的缓冲。
    QVector<wchar_t> buffer(16384 + 2);

    for (DWORD index = 0;; ++index) {
        DWORD length = static_cast<DWORD>(buffer.size());
        const LONG status = RegEnumValueW(handle, index, buffer.data(), &length,
                                          nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS)
            break;

        // (Default) 枚举出来时长度为 0 —— 与我们的契约一致（空串就是默认值）。
        result << QString::fromWCharArray(buffer.constData(), static_cast<int>(length));
    }

    RegCloseKey(handle);

    // 与内存后端保持同样的顺序：默认值在最前，其余按名字排序。
    // 顺序不一致会让「校验报告」在两个后端上给出不同的 diff。
    QString named = result;
    named.removeAll(QString());
    std::sort(named.begin(), named.end());

    QStringList ordered;
    if (result.contains(QString()))
        ordered << QString();
    ordered << named;
    return ordered;
}

bool Win32RegistryStore::setValue(const QString &key, const QString &name,
                                  const RegistryValue &value, Files::ErrorCode *error)
{
    if (error != nullptr)
        *error = Files::ErrorCode();

    HKEY handle = nullptr;
    const LONG opened = openOrCreateKey(key, &handle);
    if (opened != ERROR_SUCCESS) {
        if (error != nullptr)
            *error = Files::fromWindowsError(static_cast<unsigned long>(opened));
        return false;
    }

    const QString normalizedName = RegistryStore::normalizeName(name);

    DWORD type = REG_SZ;
    QByteArray payload;
    switch (value.kind) {
    case RegistryValueKind::String:
    case RegistryValueKind::ExpandString: {
        type = (value.kind == RegistryValueKind::String) ? REG_SZ : REG_EXPAND_SZ;
        const int units = value.string.size();
        payload.resize((units + 1) * 2);
        // 手动按 UTF-16 小端铺字节，不用 memcpy 依赖 QString 的内存布局：
        // 多写一行但读起来是确定的，而这里本来就无法在开发机上跑。
        for (int i = 0; i < units; ++i) {
            const ushort unit = value.string.at(i).unicode();
            payload[i * 2] = static_cast<char>(unit & 0xFF);
            payload[i * 2 + 1] = static_cast<char>((unit >> 8) & 0xFF);
        }
        // 结尾的 NUL 已经在 resize 时置零（QByteArray 填 '\0'）。
        break;
    }
    case RegistryValueKind::DWord: {
        type = REG_DWORD;
        payload.resize(4);
        payload[0] = static_cast<char>(value.dword & 0xFF);
        payload[1] = static_cast<char>((value.dword >> 8) & 0xFF);
        payload[2] = static_cast<char>((value.dword >> 16) & 0xFF);
        payload[3] = static_cast<char>((value.dword >> 24) & 0xFF);
        break;
    }
    case RegistryValueKind::Unsupported: {
        type = static_cast<DWORD>(value.rawType);
        payload = value.raw;
        break;
    }
    }

    const LONG status = RegSetValueExW(
            handle, wideNamePointer(normalizedName), 0, type,
            reinterpret_cast<const BYTE *>(payload.constData()),
            static_cast<DWORD>(payload.size()));

    RegCloseKey(handle);

    if (status != ERROR_SUCCESS) {
        if (error != nullptr)
            *error = Files::fromWindowsError(static_cast<unsigned long>(status));
        return false;
    }
    return true;
}

bool Win32RegistryStore::removeValue(const QString &key, const QString &name,
                                     Files::ErrorCode *error)
{
    if (error != nullptr)
        *error = Files::ErrorCode();

    HKEY handle = nullptr;
    if (!openKey(key, KEY_SET_VALUE, &handle)) {
        // 键不存在 → 值当然不存在 → 幂等地报成功。
        // 报失败会让卸载给出一堆假错误，用户从此忽略真正的失败。
        return true;
    }

    const QString normalizedName = RegistryStore::normalizeName(name);
    const LONG status = RegDeleteValueW(handle, wideNamePointer(normalizedName));
    RegCloseKey(handle);

    if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
        return true;

    if (error != nullptr)
        *error = Files::fromWindowsError(static_cast<unsigned long>(status));
    return false;
}

bool Win32RegistryStore::removeKey(const QString &key, Files::ErrorCode *error)
{
    if (error != nullptr)
        *error = Files::ErrorCode();

    const QString path = fullPath(key);
    if (path.isEmpty())
        return true;

    const int cut = path.lastIndexOf(QLatin1Char('\\'));
    if (cut < 0)
        return true; // 根句柄本身不删

    const QString parent = path.left(cut);
    const QString leaf = path.mid(cut + 1);
    if (leaf.isEmpty())
        return true;

    HKEY parentHandle = nullptr;
    // 注意这里打开的是**完整路径**的父键，所以直接用 RegOpenKeyExW。
    if (RegOpenKeyExW(m_hive, widePointer(parent), 0,
                      KEY_READ | KEY_WRITE | kRegistryView, &parentHandle) != ERROR_SUCCESS) {
        return true; // 父键都不在，这个键自然不在
    }

    // 先删子键与值，再删自己。
    // 用 RegDeleteTreeW 而不是依赖 RegDeleteKeyW 在 Win7+ 上的递归行为：
    // 后者的语义随系统版本变过，而这里无法在开发机上试。
    const LONG treeStatus = RegDeleteTreeW(parentHandle, widePointer(leaf));
    const LONG keyStatus = RegDeleteKeyW(parentHandle, widePointer(leaf));
    RegCloseKey(parentHandle);

    const bool treeOk = (treeStatus == ERROR_SUCCESS || treeStatus == ERROR_FILE_NOT_FOUND);
    const bool keyOk = (keyStatus == ERROR_SUCCESS || keyStatus == ERROR_FILE_NOT_FOUND);
    if (treeOk && keyOk)
        return true;

    const LONG failing = treeOk ? keyStatus : treeStatus;
    if (error != nullptr)
        *error = Files::fromWindowsError(static_cast<unsigned long>(failing));
    return false;
}

} // namespace

RegistryStore *createNativeRegistryStore()
{
    return new Win32RegistryStore(QStringLiteral("HKEY_CURRENT_USER"));
}

bool platformHasRegistry()
{
    return true;
}

QString platformRegistryUnsupportedReason()
{
    return QString();
}

QString platformRegistryUnsupportedAdvice()
{
    return QString();
}

} // namespace Platform
} // namespace LqCompare
