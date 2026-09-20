#ifndef LQCOMPARE_INSTANCEPROTOCOL_H
#define LQCOMPARE_INSTANCEPROTOCOL_H

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace LqCompare {
namespace Platform {

///
/// \brief 单实例与进程间通信的**纯逻辑**部分（PRD: PLAT-006）。
///
/// 为什么这些事要单独抽出来
/// ----------------------
/// PLAT-006 的四件事里，只有两件真的需要系统能力：占住「我是唯一那个」的标识、
/// 在两端之间搬字节。剩下全部是**规则**，而且是那种「写错了不会崩、只会
/// 静默地不工作」的规则：
///
///   * 端点名与共享内存键怎么从「应用名 + 用户 + 种子」推出来。推错了的表现是
///     第二个实例连不上第一个（于是用户得到两个窗口），或者**两个不同用户的
///     进程互相顶掉**（同一台机器上第二个登录的用户一启动就被吞掉）。
///   * 转发的结果怎么翻译成退出码。这段取值与 CLI-004 已经占用的 0~4 相邻，
///     越界的表现是「脚本以为比对跑过了、而且没有差异」。
///   * 一段字节怎么判断它是不是一个完整的转发请求。这段一旦宽松，任何一个
///     误连过来的客户端都能让我们分配一大块内存或读越界。
///   * 「要不要把窗口抢到前台」。判错的表现是用户正在另一个窗口里打字，
///     结果焦点被抢走——而这是本条目里唯一一条与人的手感有关的规则。
///
/// 这批规则全部放进本文件，于是它们可以在**只链接 QtCore + QtNetwork** 的
/// 测试套件里被完整执行，不需要真的启动两个进程去点来点去。
/// 同一套做法在本仓已经用过三次（掩码、属性条件、名称过滤）。
///
/// ## 与 `singleinstance.h` 的分工
///
/// 本文件回答「叫什么名字、发什么字节、算不算完整、该返回几、该不该抢焦点」；
/// `singleinstance.h` 回答「谁来占标识、谁来收发、失败了怎么办」。
/// 前者是**数据与判定**，后者是**动作**——所以只有后者需要 QtNetwork 的
/// `QLocalServer` / `QLocalSocket`。
///

// -----------------------------------------------------------------------------
// 标识符：端点名与共享内存键
// -----------------------------------------------------------------------------

/// 端点名（本地套接字名）的长度上界。
///
/// **这个上界不是审美问题**：Unix 域套接字的路径存在 `sockaddr_un::sun_path`
/// 里，macOS 上是 104 字节；而 `QLocalServer` 会在名字前面拼上临时目录
/// （macOS 的私有临时目录形如 `/var/folders/8k/<30 个字符>/T/`，本身就有约 48
/// 字符）。名字一旦超长，`listen()` 会失败——现象是「第二个实例永远连不上」，
/// 与长度看起来毫无关系。40 是留足余量之后的取值。
int instanceEndpointNameLimit();

/// 共享内存键的长度上界。
///
/// 取 31 是照 `PSHMNAMLEN` 定的（macOS 的 POSIX 共享内存名字上限就是 31，
/// 含开头的 `/`）。本机的 Qt 5.15.2 走的是 SysV 那条路（键会变成一个文件名），
/// 31 对它是过分保守的；但 Qt 有一个编译开关可以把 `QSharedMemory` 切到
/// POSIX 实现上，而**切过去之后超长的键会直接失败**。取一个在两种实现下都
/// 合法的长度，换来的是「换一个 Qt 构建也不会坏」。
int instanceSharedMemoryKeyLimit();

///
/// \brief 把一个字符串净化成可以安全出现在端点名 / 共享内存键里的形态。
///
/// 规则：`[A-Za-z0-9._-]` 原样保留，其余一律换成 `_`。
///
/// 为什么必须净化：共享内存键在 Unix 上会变成一个**文件名**（Qt 会拿它去
/// `ftok`），端点名会变成一个**套接字路径**。名字里出现 `/` 的后果不是一个
/// 错误码，而是「写到别的目录去了」或「路径不存在」——两者都表现为莫名其妙
/// 的失败。
///
/// 被替换过字符时**追加一段原始输入的稳定哈希**。这一步是为了保住区分度：
/// 中文用户名、带空格的用户名经净化后会变成一长串下划线，两个不同的用户
/// 会因此撞进同一个标识，于是其中一个用户的程序会被另一个用户的实例吞掉
/// （他看不到对方的窗口，只会觉得「点了没反应」）。哈希只用来区分，不要求
/// 可逆。
///
/// 净化后为空时返回空串，由调用方决定退化成什么（见 `composeInstanceIdentifier`）。
///
QString sanitizeInstanceToken(const QString &raw);

///
/// \brief 稳定哈希的十六进制形式（8 个字符，FNV-1a 32 位）。
///
/// **不能用 `qHash()`**：Qt 的 `qHash` 对每个进程使用随机种子（`QT_HASH_SEED`），
/// 于是同一个输入在不同进程里得到不同的值。而这里推出来的名字要**跨进程一致**
/// ——第一个实例记的是 A、第二个实例算出来是 B，两者永远对不上。这个坑不会
/// 报错，只会让单实例机制彻底失效（每个实例都认为自己是第一个）。
///
QString stableTokenFingerprint(const QString &text);

///
/// \brief 由「前缀 + 用户 + 种子」拼出一个有界标识符。
///
/// 可读前缀后始终附加原始「前缀、用户、种子」的稳定摘要。摘要输入为逐字段
/// 长度前缀编码，不依赖净化或分隔符拼接后的文本，避免不同原始输入得到相同
/// 可读文本时共享身份。超出 `limit` 时只截短可读部分；`limit <= 0` 不限长。
///
QString composeInstanceIdentifier(const QString &prefix,
                                  const QString &seed,
                                  const QString &userScope,
                                  int limit);

/// 本实例在系统里占用的端点名。
QString instanceEndpointName(const QString &seed, const QString &userScope);
/// 本实例在系统里占用的共享内存键。
QString instanceSharedMemoryKey(const QString &seed, const QString &userScope);

///
/// \brief 由环境里的用户名推出「用户作用域」，取不到时返回 `"anonymous"`。
///
/// 为什么要按用户分开：同一台机器上可以同时有多个登录用户（多用户登录、
/// Windows 的「切换用户」）。不分开时，第二个用户启动程序会被第一个用户的
/// 实例吞掉——他看不到那个窗口，程序看起来就是「点了没反应」。
///
/// 这是一个**不带 `const` 的实际依赖**（读环境变量），因此做成接受参数的
/// 纯函数 + 一个薄封装，用例可以直接喂各种取值。
///
QString instanceUserScopeFromName(const QByteArray &userName);
/// 读 `USER` / `USERNAME` 环境变量（两个平台各用一个）。
QString defaultInstanceUserScope();

// -----------------------------------------------------------------------------
// 转发的结果与退出码
// -----------------------------------------------------------------------------

///
/// \brief 一次转发的结局。
///
enum class RelayStatus {
    /// 没有发生转发（本进程就是首个实例，或单实例机制被关掉了）。
    NotAttempted,
    /// 首个实例收下了参数。
    Delivered,
    /// 连上了首个实例，但它明确拒绝（载荷不合法 / 协议版本不一致）。
    Rejected,
    /// 套接字上没有人应答（没有首个实例在跑，或它已经死了只留下陈迹）。
    NoPrimary,
    /// 连上了，但首个实例在时限内没有回话。
    HandshakeTimeout,
};

const char *relayStatusIdentifier(RelayStatus status);
QString relayStatusText(RelayStatus status);

///
/// \brief 退出码表的一行。
///
struct RelayExitCodeRow
{
    RelayStatus status = RelayStatus::NotAttempted;
    /// 该结局对应的退出码。`NotAttempted` 恒为 0：它不是一个会被返回的结局。
    int code = 0;
    /// 稳定的机器标识（会进日志与脚本），与 `relayStatusIdentifier` 同源。
    ///
    /// 类型是 `const char *` 而不是 `QString`：`relayStatusIdentifier()` 要返回
    /// 一个可以长期持有的指针，而 `row.identifier.toUtf8().constData()` 取到的
    /// 是**临时 `QByteArray`** 的数据，函数一返回就悬垂——现象是日志里印出
    /// 一串乱码或空串，与真正的原因毫无关系。本仓的坑表里有同类记录。
    ///
    /// 这里的取值全部是纯 ASCII 短名，因此 `const char *` 是安全的。
    /// 反过来，**含中文的字段绝不能**用它（`QLatin1String` 会把 UTF-8 当
    /// Latin-1 解释，比较永远不相等）——`text` 就仍然是 `QString`。
    ///
    const char *identifier = "";
    /// 一句话说明，直接进日志。
    QString text;
};

/// 转发退出码区间的下界。
int relayExitCodeBandFirst();
/// 转发退出码区间的上界。
int relayExitCodeBandLast();

///
/// \brief CLI-004 已经占用的退出码上界（`0 = 无差异` 到 `4 = 内部错误`）。
///
/// 单独取个名字而不是在代码里写 `4`：这一段取值是**别人的契约**（CLI-004 的
/// 规格原文），本模块只是绕开它。CLI-004 落地时应当把这里换成对它的引用，
/// 并保留下面的自检——它会在那一刻报出来。
///
int cliReservedExitCodeLast();

const QVector<RelayExitCodeRow> &relayExitCodeTable();
int relayExitCode(RelayStatus status);

///
/// \brief 校验退出码表。
///
/// 查六件事：行数是否与已知结局种数一致（多出来的一行没有任何约束，
/// 恰恰最需要被看见）、每种结局是否各有且只有一行、除 `NotAttempted` 外
/// 是否都落在转发区间内且不与 CLI-004 的 0~4 重叠、码是否唯一、
/// 标识是否唯一且非空、说明是否唯一且非空。
///
/// **表当参数，且表内的每一行都按它自己的字段判断**（与
/// `validateNameFilterTables` / `validateAttributeConditionTable` 同一条纪律）：
/// 自检若从模块内部取表、或按 status 回查模块内部那份表，用例就没法拿一份
/// 故意写坏的表证明它真会报，于是它会变成一条**永远不会红**的护栏。
///
QVector<QString> validateRelayExitCodeTable(const QVector<RelayExitCodeRow> &table);

// -----------------------------------------------------------------------------
// 命令行开关
// -----------------------------------------------------------------------------

/// 单实例机制的三态：没提到 / 打开 / 关闭。
enum class SingleInstanceSwitchDecision {
    Unset,
    Enabled,
    Disabled,
};

const char *singleInstanceSwitchDecisionIdentifier(SingleInstanceSwitchDecision decision);

///
/// \brief 一个命令行开关。
///
struct SingleInstanceSwitch
{
    QString name;
    SingleInstanceSwitchDecision decision = SingleInstanceSwitchDecision::Unset;
    QString description;
};

///
/// \brief 本模块认的开关表（唯一事实来源）。
///
/// 表放在模块里而不是写在 `main.cpp` 里：`main.cpp` 目前既要把它们交给
/// `QCommandLineParser`（这样 `--help` 会列出来，而且它们不会被当成位置参数
/// 也就是「要比对的文件路径」），又要把它们翻译成三态。两处各写一遍名称，
/// 迟早会出现「帮助里有一个、解析时认另一个」。
///
const QVector<SingleInstanceSwitch> &singleInstanceSwitches();

///
/// \brief 由「哪些开关被设置了」推出最终决定。
///
/// 语义：没提到任何开关时用 `fallback`；提到多个时**表里靠后的胜出**
/// （表顺序即优先级，`--no-single-instance` 在表尾，因此它总是能压过前者）。
/// 选「表顺序」而不是「命令行顺序」是因为 `QCommandLineParser` 不保留顺序，
/// 而为了这件事去自己解析一遍命令行会形成第二份解析实现。
///
/// **表当参数**，理由与 `validateRelayExitCodeTable` 相同。
///
SingleInstanceSwitchDecision resolveSingleInstanceSwitch(
    const QVector<SingleInstanceSwitch> &table,
    const QStringList &setSwitchNames,
    SingleInstanceSwitchDecision fallback);

// -----------------------------------------------------------------------------
// 窗口置前策略
// -----------------------------------------------------------------------------

///
/// \brief 收到转发过来的参数时，要不要把首个实例的窗口抢到前台。
///
/// PLAT-006 第 3 条的括注是「不抢焦点导致用户中断输入时可配置」。
/// 三种取值对应三种真实诉求：双击文件关联的人希望窗口**立刻**出来；
/// 正在别的窗口里打字的人希望程序**别**把焦点抢走（哪怕参数确实进去了）；
/// 批处理与脚本场景则完全不希望界面被抬起来。
///
enum class ActivationPolicy {
    Always,
    UnlessTypingRecently,
    Never,
};

const char *activationPolicyIdentifier(ActivationPolicy policy);
QString activationPolicyText(ActivationPolicy policy);

/// 「最近有输入」的静默窗口默认值（毫秒）。2500ms 大致覆盖一次连续输入
/// 的间歇——比它更短会把「正在打字」误判成「没在打字」。
int defaultActivationQuietWindowMs();

///
/// \brief 置前判定的唯一实现。
///
/// `millisecondsSinceLastInput` 为**负数**表示「不知道」（例如还没有接上输入
/// 来源）。不知道时按「置前」处理：这一条走的是「用户双击了一个文件」的
/// 主路径，把他要的窗口藏在别的窗口后面比抢一次焦点更糟。
///
bool shouldActivateWindow(ActivationPolicy policy,
                          int millisecondsSinceLastInput,
                          int quietWindowMs);

// -----------------------------------------------------------------------------
// 线协议
// -----------------------------------------------------------------------------

///
/// \brief 转发请求的载荷。
///
struct RelayRequest
{
    /// 第二个实例的当前目录。首个实例必须用它来解析相对路径，否则
    /// `lqcompare ../a.txt ../b.txt` 会按**第一个实例**的目录去解析。
    QString workingDirectory;
    QStringList arguments;
};

///
/// \brief 转发应答的载荷。
///
struct RelayReply
{
    bool accepted = false;
    /// 首个实例实际解析出的参数个数。请求方拿它跟自己发出去的个数比对，
    /// 就能发现「字节在传输中被截断了」这类问题——只看 `accepted`
    /// 是看不出这个的。
    int argumentCount = 0;
    /// 人读的说明（拒绝原因等）。
    QString detail;
};

/// 协议版本。两端不一致时**拒绝**而不是尽力解析。
int relayProtocolVersion();

/// 单条载荷的字节数上界（超过即拒绝，不为其分配内存）。
quint32 relayMaxPayloadBytes();
/// 一次转发携带的参数个数上界。
int relayMaxArgumentCount();

///
/// \brief 把一次转发请求编成一整帧。
///
/// 帧格式（全部大端）：
///
///     魔数 "LQCI"（4 字节）
///     消息类型（1 字节：1 = 请求，2 = 应答）
///     协议版本（1 字节）
///     载荷长度（4 字节）
///     载荷
///
/// 请求载荷：工作目录（长度 + UTF-8 字节）、参数个数、每个参数（长度 + UTF-8）。
/// 应答载荷：是否接受（1 字节）、参数个数（4 字节）、说明（长度 + UTF-8）。
///
/// **长度前缀是必需的**：本地套接字是字节流，一次 `write` 与一次
/// `readyRead` 之间没有任何对应关系。少了它就只能靠「读到断开为止」，
/// 而那意味着一个永远不挂断的客户端能把内存撑满。
///
QByteArray encodeRelayRequest(const RelayRequest &request);
QByteArray encodeRelayReply(const RelayReply &reply);

///
/// \brief 解出一帧。
///
/// 失败时返回 false 并把原因写进 `problem`（`problem` 可传 `nullptr`）。
/// **不修改 `out`**，于是调用方可以拿它当「这次解出来的是不是我要的东西」。
///
/// 拒绝的情形（每一种都有用例）：长度不足一个头、魔数不对、类型不对、
/// 协议版本不一致、声明的载荷长度超过上界、实际字节数与声明不符、
/// 参数个数超过上界、某个长度字段超出剩余字节、UTF-8 非法。
///
bool decodeRelayRequest(const QByteArray &frame, RelayRequest *out, QString *problem);
bool decodeRelayReply(const QByteArray &frame, RelayReply *out, QString *problem);

///
/// \brief 一段累积的字节里，第一帧的状态。
///
enum class FrameStatus {
    /// 还没到齐，继续等。
    Incomplete,
    /// 正好到齐，`frameSize` 给出这一帧的字节数。
    Complete,
    /// 不可能构成一个合法的帧（魔数不对、版本不对、声明的长度超过上界），
    /// 继续等下去只会白等到超时。
    Invalid,
};

///
/// \brief 判断「缓冲区里有没有一整帧」。
///
/// 收发两端都需要它：发送端要知道应答到齐了没有，接收端要知道请求到齐了没有。
/// 抽成纯函数的收益是**超时与分片这两件事可以在单进程里被完整覆盖**
/// ——它们是本协议里最容易写错的部分（「数据一次到齐」在本地套接字上
/// 大多数时候成立，于是写错的分片处理往往到用户机器上才暴露）。
///
/// `problem` 只在前两种「不可能合法」的情形下被填写。
///
FrameStatus inspectRelayFrame(const QByteArray &buffer, int *frameSize, QString *problem);

/// 一帧的固定头部长度（魔数 + 类型 + 版本 + 载荷长度）。
int relayFrameHeaderSize();

/// 一句话描述一个转发请求，用于日志。
QString describeRelayRequest(const RelayRequest &request);

///
/// \brief 注册进信号槽需要的元类型。
///
/// `RelayRequest` 会出现在 `SingleInstanceGuard::relayReceived` 上。忘了注册时
/// `QObject::connect` 只在**运行期**抱怨一句，编译期什么都看不出来；而
/// 队列连接（迟早会用到）那时的现象是「信号发出去了但槽没被调用」。
/// 构造函数里用函数内静态做一次性注册，并有一条用例断言它真的注册上了。
///
void registerInstanceMetaTypes();

} // namespace Platform
} // namespace LqCompare

// 进了信号的载荷要能跨连接传递。声明放在命名空间之外是 Qt 的要求。
Q_DECLARE_METATYPE(LqCompare::Platform::RelayRequest)

#endif // LQCOMPARE_INSTANCEPROTOCOL_H
