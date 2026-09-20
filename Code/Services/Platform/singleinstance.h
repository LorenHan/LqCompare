#ifndef LQCOMPARE_SINGLEINSTANCE_H
#define LQCOMPARE_SINGLEINSTANCE_H

#include "instanceprotocol.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QLocalServer;
class QLocalSocket;
class QLockFile;
class QSharedMemory;

namespace LqCompare {
namespace Platform {

///
/// \brief 单实例机制（PRD: PLAT-006 第 1、2、4 条）。
///
/// ## 它解决的是什么问题
///
/// 用户双击第二个文件、或者从终端再敲一次 `lqcompare a.txt b.txt` 时，
/// 他想要的**不是**第二个窗口，而是「已经开着的那个窗口里多出一组比对」。
/// 同时「一个进程 = 一份用户配置 = 一个正在编辑的会话集合」这件事也是
/// 本工具的数据模型前提。
///
/// ## 身份与通信
///
/// 所有实例先争用 QLockFile 进程锁，再创建 QSharedMemory 标识并监听
/// QLocalServer。锁文件避免恢复遗留段时的竞争，也在系统共享内存耗尽时
/// 保证只有一个首实例；共享内存不可用的原因会进入启动报告。
///
/// Qt 的 System V 后端通过 attach/detach 回收无人持有的遗留段；恢复前
/// 必须先拿进程锁。QLockFile 根据进程存活信息恢复崩溃遗留锁，禁止按锁龄
/// 抢走活进程的锁。正常退出按连接、监听、共享内存、进程锁的顺序释放。
///
/// 转交在一个总截止时间内等待连接、写入与应答；刚拿到锁但尚未监听的
/// 首实例有机会完成启动。无响应时遵循 RelayFallback，不会无限等待。
/// 关闭机制时不持有任何标识或端点。
///
/// 本模块只依赖 QtCore 与 QtNetwork，不含平台分支或界面依赖。
/// relayReceived 交付参数和发送方工作目录；activationRequested 提供置前
/// 策略结论。窗口与会话创建由调用方完成。
///

///
/// \brief 本次启动扮演的角色。
///
enum class InstanceRole {
    /// 单实例机制被关掉了，本进程照常启动，且不占用任何标识。
    Disabled,
    /// 本进程就是首个实例，已持有标识并在监听端点。
    Primary,
    /// 参数已经交给别人（或被判定为不可交付且选择了「按退出码退出」），
    /// 本进程应当退出。
    Secondary,
    /// 通信没能完成，但按边界条款**照常启动**——本进程会成为一个额外的实例。
    Degraded,
};

const char *instanceRoleIdentifier(InstanceRole role);
QString instanceRoleText(InstanceRole role);

///
/// \brief 交不出去的时候怎么办。
///
enum class RelayFallback {
    /// 默认：照常启动一个新实例（PLAT-006 的边界条款「绝不让用户看到失败」）。
    StartNewInstance,
    /// 用转发结果的退出码退出。给无界面与脚本场景用——那里没有窗口可开，
    /// 「悄悄多出一个进程」比直接报错更糟。
    ExitWithRelayCode,
};

const char *relayFallbackIdentifier(RelayFallback fallback);

///
/// \brief 一次 `start()` 的结论。
///
/// 做成一个结构而不是一串 `isXxx()` 查询：调用方要拿它一次性决定「继续启动
/// 界面」还是「带着某个退出码退出」，而把这几个事实分开取会让「角色已经有
/// 了、转发结论还没看」这种半截状态出现在调用点。
///
struct InstanceStartReport
{
    InstanceRole role = InstanceRole::Disabled;
    RelayStatus relay = RelayStatus::NotAttempted;
    QString endpointName;
    QString sharedMemoryKey;
    /// 我们把多少个参数交了出去（`role == Secondary` 时有意义）。
    int forwardedArgumentCount = 0;
    /// 一句话说明，直接进日志。**成功的路径也有内容**——「什么都没打印」
    /// 会让接手的人分不清「降级了」与「日志级别把它挡住了」。
    QString detail;

    /// 转发结果对应的退出码（见 `relayExitCodeTable`）。
    int exitCode() const { return relayExitCode(relay); }
    /// 本进程是否应当立刻退出。
    bool shouldExit() const { return role == InstanceRole::Secondary; }
};

class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);
    ~SingleInstanceGuard() override;

    // --- 配置（都应当在 start() 之前设好） ------------------------------------

    /// 关掉单实例机制（对应 `--no-single-instance` / OPT-002 的选项）。
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// 等应答的时限。默认 2000ms：这个等待发生在**第二个实例**的启动路径上，
    /// 用户此时还没看到任何窗口，所以它不能长；但太短会把「首个实例正在忙」
    /// 误判成「没人在跑」，于是又多出一个窗口。
    void setRelayTimeoutMs(int milliseconds);
    int relayTimeoutMs() const;

    void setRelayFallback(RelayFallback fallback);
    RelayFallback relayFallback() const;

    /// 要交出去的参数与「第二个实例的当前目录」。
    ///
    /// 工作目录必须一起交：`lqcompare ../a.txt ../b.txt` 里的相对路径要按
    /// **第二个实例**的目录解析，而不是按首个实例的——两个实例的当前目录
    /// 完全可以不同（从不同目录里双击）。
    void setRelayRequest(const RelayRequest &request);
    RelayRequest relayRequest() const;

    void setActivationPolicy(ActivationPolicy policy);
    ActivationPolicy activationPolicy() const;
    void setActivationQuietWindowMs(int milliseconds);
    int activationQuietWindowMs() const;

    /// 「距上次用户输入过了多少毫秒」的来源（负数表示不知道）。
    ///
    /// 做成可注入的函数对象而不是在这里读键盘钩子：本模块是服务层，
    /// 「用户刚才有没有在打字」只有界面层知道。不注入时按「不知道」处理，
    /// 也就是照常置前。
    void setLastInputAgeProvider(std::function<int()> provider);

    // --- 动作 -----------------------------------------------------------------

    ///
    /// \brief 判定本次启动的角色，并在必要时把自己变成首个实例。
    ///
    /// 同一首实例重复 start() 保持原监听；切换种子或关闭机制会先释放旧资源。
    ///
    /// `instanceSeed` 是「哪个程序」的标识（本工具传应用名；测试传一个
    /// 每次运行都不同的名字，这样测试之间、测试与真实运行的程序之间
    /// 不会互相顶掉）。
    ///
    /// 返回之后：`Primary` / `Degraded` / `Disabled` → 调用方继续启动界面；
    /// `Secondary` → 调用方立刻以 `report.exitCode()` 退出。
    ///
    InstanceStartReport start(const QString &instanceSeed);

    const InstanceStartReport &report() const { return m_report; }

    /// 本进程是否持有标识（也就是「是不是首个实例」）。
    bool isPrimary() const { return m_primary; }
    /// 系统共享内存耗尽时仍以进程锁选主，参数继续通过本地套接字转交。
    bool usesSharedMemoryIdentifier() const { return m_sharedMemory != nullptr; }
    /// 端点是否真的在监听。持有标识但监听失败是一个真实存在的中间状态，
    /// 单独暴露出来，免得调用方把「拿到标识」当成「能收到参数」。
    bool isListening() const;
    QString endpointName() const { return m_endpointName; }
    QString sharedMemoryKey() const { return m_sharedMemoryKey; }
    QString identifierLockPath() const;

signals:
    /// 首个实例收到了第二个实例转交过来的参数。
    void relayReceived(const LqCompare::Platform::RelayRequest &request);
    /// 请把窗口置前。`raiseWindow` 是按 `ActivationPolicy` 算出来的结论，
    /// 界面照着做即可，不要在这里再判一次——两处判断必然分歧。
    void activationRequested(const QStringList &arguments, bool raiseWindow);
    /// 首个实例侧收不下去了（载荷不合法、超时），附原因，供日志记录。
    void relayProblem(const QString &problem);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketFinished();

private:
    bool takePrimaryIdentifier(QString *problem);
    /// 失败时只把原因写进 `problem`：标识已经拿到手了，「监听失败」是一个
    /// 需要如实报告的中间状态，而不是「没成为首个实例」。
    void startListening(QString *problem);
    bool recoverStaleIdentifier();
    RelayStatus relayOnce(QString *detail);
    void deliverRequest(const RelayRequest &request);
    void replyRejection(QLocalSocket *socket, const QString &problem);
    void finishSocket(QLocalSocket *socket, const QByteArray &reply);
    void releaseEverything();

    bool m_enabled = true;
    int m_relayTimeoutMs = 2000;
    RelayFallback m_fallback = RelayFallback::StartNewInstance;
    RelayRequest m_request;
    ActivationPolicy m_activationPolicy = ActivationPolicy::Always;
    int m_activationQuietWindowMs = 0;
    std::function<int()> m_lastInputAgeProvider;

    QString m_endpointName;
    QString m_sharedMemoryKey;
    bool m_primary = false;
    std::unique_ptr<QLockFile> m_identifierLock;
    std::unique_ptr<QSharedMemory> m_sharedMemory;
    QString m_sharedMemoryProblem;
    QLocalServer *m_server = nullptr;
    /// 每个连接上已经收到的字节。分片与超时都要靠它。
    QHash<QLocalSocket *, QByteArray> m_buffers;
    InstanceStartReport m_report;
};

} // namespace Platform
} // namespace LqCompare

#endif // LQCOMPARE_SINGLEINSTANCE_H
