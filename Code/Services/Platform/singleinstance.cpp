#include "singleinstance.h"

#include <QElapsedTimer>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSharedMemory>
#include <QThread>
#include <QTimer>

namespace LqCompare {
namespace Platform {

namespace {

// 同时对齐 4 KiB 和 16 KiB 页，避免 Rosetta 下 System V 分配与释放的
// 页大小差异。只存标识，不传载荷；每个首实例最多占这一段。
constexpr int kSharedMemorySize = 16 * 1024;

} // namespace

const char *instanceRoleIdentifier(InstanceRole role)
{
    switch (role) {
    case InstanceRole::Disabled:  return "disabled";
    case InstanceRole::Primary:   return "primary";
    case InstanceRole::Secondary: return "secondary";
    case InstanceRole::Degraded:  return "degraded";
    }
    return "unknown";
}

QString instanceRoleText(InstanceRole role)
{
    switch (role) {
    case InstanceRole::Disabled:
        return QStringLiteral("单实例机制已关闭");
    case InstanceRole::Primary:
        return QStringLiteral("本进程是首个实例");
    case InstanceRole::Secondary:
        return QStringLiteral("参数已交给正在运行的实例，本进程退出");
    case InstanceRole::Degraded:
        return QStringLiteral("通信未完成，按降级策略照常启动新实例");
    }
    return QStringLiteral("未知的角色");
}

const char *relayFallbackIdentifier(RelayFallback fallback)
{
    switch (fallback) {
    case RelayFallback::StartNewInstance: return "start-new-instance";
    case RelayFallback::ExitWithRelayCode: return "exit-with-relay-code";
    }
    return "unknown";
}

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : QObject(parent)
{
    // 进信号的 `RelayRequest` 必须先注册：忘了注册时 `connect` 只在运行期
    // 抱怨一句，编译期什么都看不出来；而队列连接下的现象是「信号发了、
    // 槽没被调用」。
    registerInstanceMetaTypes();
    m_activationQuietWindowMs = defaultActivationQuietWindowMs();
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    releaseEverything();
}

// -----------------------------------------------------------------------------
// 配置
// -----------------------------------------------------------------------------

void SingleInstanceGuard::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool SingleInstanceGuard::isEnabled() const
{
    return m_enabled;
}

void SingleInstanceGuard::setRelayTimeoutMs(int milliseconds)
{
    // 下限取 1：0 会让每次等待立刻超时，于是「首个实例正在跑」被误判成
    // 「没人在跑」，单实例机制看起来还在、实际每次都降级。
    m_relayTimeoutMs = qMax(1, milliseconds);
}

int SingleInstanceGuard::relayTimeoutMs() const
{
    return m_relayTimeoutMs;
}

void SingleInstanceGuard::setRelayFallback(RelayFallback fallback)
{
    m_fallback = fallback;
}

RelayFallback SingleInstanceGuard::relayFallback() const
{
    return m_fallback;
}

void SingleInstanceGuard::setRelayRequest(const RelayRequest &request)
{
    m_request = request;
}

RelayRequest SingleInstanceGuard::relayRequest() const
{
    return m_request;
}

void SingleInstanceGuard::setActivationPolicy(ActivationPolicy policy)
{
    m_activationPolicy = policy;
}

ActivationPolicy SingleInstanceGuard::activationPolicy() const
{
    return m_activationPolicy;
}

void SingleInstanceGuard::setActivationQuietWindowMs(int milliseconds)
{
    m_activationQuietWindowMs = qMax(0, milliseconds);
}

int SingleInstanceGuard::activationQuietWindowMs() const
{
    return m_activationQuietWindowMs;
}

void SingleInstanceGuard::setLastInputAgeProvider(std::function<int()> provider)
{
    m_lastInputAgeProvider = std::move(provider);
}

bool SingleInstanceGuard::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

// -----------------------------------------------------------------------------
// 启动判定
// -----------------------------------------------------------------------------

InstanceStartReport SingleInstanceGuard::start(const QString &instanceSeed)
{
    // 段名与端点名都从「应用 + 用户 + 种子」推出来。按用户分开是必须的：
    // 同一台机器上另一个登录用户启动本程序时，若拿到同一个标识就会被
    // 我们的实例吞掉——他看不到那个窗口，只会觉得「点了没反应」。
    const QString scope = defaultInstanceUserScope();
    const QString endpoint = instanceEndpointName(instanceSeed, scope);
    if (m_enabled && m_primary && endpoint == m_endpointName) {
        return m_report;
    }
    releaseEverything();
    m_endpointName = endpoint;
    m_sharedMemoryKey = instanceSharedMemoryKey(instanceSeed, scope);

    InstanceStartReport result;
    result.endpointName = m_endpointName;
    result.sharedMemoryKey = m_sharedMemoryKey;

    if (!m_enabled) {
        // 关掉时**不占任何标识、不建任何端点**。「关掉了但仍占着锁」会让
        // 后续每一个实例都降级，等于把这个选项做成了「更坏的单实例」。
        result.role = InstanceRole::Disabled;
        result.relay = RelayStatus::NotAttempted;
        result.detail = QStringLiteral("单实例机制已由选项关闭：不占用标识、不建端点，直接启动");
        m_report = result;
        return result;
    }

    QString problem;
    if (takePrimaryIdentifier(&problem)) {
        result.role = InstanceRole::Primary;
        result.relay = RelayStatus::NotAttempted;
        result.detail = problem.isEmpty()
            ? QStringLiteral("本进程是首个实例，已在端点 %1 上等待转交").arg(m_endpointName)
            : QStringLiteral("本进程拿到了标识，但端点监听失败（%1）——"
                             "别的实例将无法把参数交给它，它们会自行启动")
                  .arg(problem);
        if (!m_sharedMemoryProblem.isEmpty()) {
            result.detail.prepend(QStringLiteral("共享内存不可用（%1），已由进程锁保证单实例；")
                                      .arg(m_sharedMemoryProblem));
        }
        m_report = result;
        return result;
    }

    const int forwardedCount = m_request.arguments.size();
    QString detail;
    RelayStatus status = relayOnce(&detail);

    if (status == RelayStatus::NoPrimary) {
        // 套接字上没人应答。两种可能：对方真的退出了（那它应当也放掉了
        // 标识），或者它是**崩溃**退出的——在 Unix 上崩溃不会自动删掉
        // 共享内存段，于是标识永远「存在」，而拥有它的进程早就不在了。
        // 没有下面这一步，这个用户的单实例机制会永久失效。
        //
        // 回收是安全的：attach 让引用计数 +1、detach 再 -1，只有**没有任何
        // 进程还挂着它**时 Qt 才会真正删掉那段内存。活着的实例的计数始终
        // ≥1，段不会被删，我们随后会老老实实地降级。
        QString secondProblem;
        if (takePrimaryIdentifier(&secondProblem)) {
            result.role = InstanceRole::Primary;
            result.relay = RelayStatus::NotAttempted;
            result.detail = QStringLiteral(
                "套接字上没人应答，且标识已可回收（上一个实例的进程已不在）——本进程接手成为首个实例");
            if (!secondProblem.isEmpty()) {
                result.detail += QStringLiteral("；端点监听失败：%1").arg(secondProblem);
            }
            if (!m_sharedMemoryProblem.isEmpty()) {
                result.detail += QStringLiteral("；共享内存不可用（%1），已由进程锁保证单实例")
                                     .arg(m_sharedMemoryProblem);
            }
            m_report = result;
            return result;
        }
        // 回收失败：段还被一个活着的进程挂着，但它在套接字上不应答。
        // 这正是「首个实例卡住」的情形，降级处理。
        detail = QStringLiteral("%1；回收标识失败：%2").arg(detail, secondProblem);
    }

    result.relay = status;
    result.forwardedArgumentCount = forwardedCount;

    if (status == RelayStatus::Delivered) {
        result.role = InstanceRole::Secondary;
        result.detail = QStringLiteral("已把 %1 个参数交给正在运行的实例").arg(forwardedCount);
        m_report = result;
        return result;
    }

    if (m_fallback == RelayFallback::ExitWithRelayCode) {
        result.role = InstanceRole::Secondary;
        result.detail = QStringLiteral("%1；按调用方选择的策略以退出码 %2 结束")
                            .arg(detail)
                            .arg(relayExitCode(status));
    } else {
        result.role = InstanceRole::Degraded;
        result.detail =
            QStringLiteral("%1；按边界条款照常启动新实例（用户不该看到「打不开」）").arg(detail);
    }
    m_report = result;
    return result;
}

bool SingleInstanceGuard::takePrimaryIdentifier(QString *problem)
{
    if (problem) {
        problem->clear();
    }
    // 所有实例先争用同一个可回收的进程锁：共享内存不可用时仍只能选出一个
    // 首实例，且恢复遗留段时不会和另一个守卫的 create/attach 交错。
    auto lock = std::make_unique<QLockFile>(identifierLockPath());
    lock->setStaleLockTime(0); // 活进程的锁不能仅因为超过默认 30 秒就被抢走。
    if (!lock->tryLock(0)) {
        if (problem) {
            *problem = QStringLiteral("实例进程锁不可用（错误 %1）").arg(int(lock->error()));
        }
        return false;
    }
    auto memory = std::make_unique<QSharedMemory>(m_sharedMemoryKey);
    bool created = memory->create(kSharedMemorySize);
    if (!created && memory->error() == QSharedMemory::AlreadyExists) {
        recoverStaleIdentifier();
        created = memory->create(kSharedMemorySize);
    }
    if (!created) {
        if (memory->error() == QSharedMemory::AlreadyExists) {
            // 兼容仍持有旧共享内存标识、尚未采用进程锁的活实例。
            if (problem) *problem = memory->errorString();
            return false;
        }
        // 系统资源耗尽/不支持 IPC 时保留可用的单实例闭环。锁已拿到，
        // 不能把这种错误误当成另一个实例存在，也不需要修改系统限额。
        m_sharedMemoryProblem = memory->errorString();
        memory.reset();
    }

    m_identifierLock = std::move(lock);
    m_sharedMemory = std::move(memory);
    m_primary = true;
    startListening(problem);
    return true;
}

QString SingleInstanceGuard::identifierLockPath() const
{
    return QDir(QDir::tempPath()).filePath(m_endpointName + QStringLiteral(".lock"));
}

void SingleInstanceGuard::startListening(QString *problem)
{
    // **拿到标识之后**才动端点：`removeServer()` 会把同名端点摘掉，若此刻
    // 还有一个活着的首个实例在监听，这一摘等于把它的端点拔了——新来的
    // 客户端从此连不上它，而它自己毫不知情。
    QLocalServer::removeServer(m_endpointName);
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::onNewConnection);
    if (!m_server->listen(m_endpointName)) {
        if (problem) {
            *problem = m_server->errorString();
        }
    }
}

bool SingleInstanceGuard::recoverStaleIdentifier()
{
    // 探测用的对象与 m_sharedMemory 分开：那个对象上刚刚失败过 create()，
    // 复用它的状态容易把「上一次失败」与「这一次探测」混在一起。
    QSharedMemory probe(m_sharedMemoryKey);
    if (!probe.attach()) {
        // 段本来就不在（可能是竞态：对方刚刚正常退出）。这不是错误，
        // 调用方跟着再试一次 create 即可。
        return false;
    }
    return probe.detach();
}

RelayStatus SingleInstanceGuard::relayOnce(QString *detail)
{
    QElapsedTimer elapsed;
    elapsed.start();
    const auto remaining = [this, &elapsed]() {
        return qMax(0, m_relayTimeoutMs - int(elapsed.elapsed()));
    };
    QLocalSocket socket;
    while (true) {
        socket.connectToServer(m_endpointName, QIODevice::ReadWrite);
        if (socket.waitForConnected(remaining())) {
            break;
        }
        if (remaining() == 0) {
            if (detail) {
                *detail = QStringLiteral("连接端点 %1 失败（时限 %2ms）：%3")
                              .arg(m_endpointName).arg(m_relayTimeoutMs).arg(socket.errorString());
            }
            return RelayStatus::NoPrimary;
        }
        // 另一个进程可能刚拿到标识，尚未 listen。拒绝连接可以立即返回，
        // 因此在同一个总截止时间内重试，避免同时启动时多开窗口。
        socket.abort();
        QThread::msleep(static_cast<unsigned long>(qMin(10, remaining())));
    }

    socket.write(encodeRelayRequest(m_request));
    while (socket.bytesToWrite() > 0) {
        if (remaining() == 0 || !socket.waitForBytesWritten(remaining())) {
            if (detail) {
                *detail = QStringLiteral("参数没有写出去：%1").arg(socket.errorString());
            }
            return RelayStatus::HandshakeTimeout;
        }
    }

    QByteArray buffer;
    int frameSize = 0;
    while (true) {
        const int left = remaining();
        if (left <= 0) {
            if (detail) {
                *detail = QStringLiteral("端点已连上，但 %1ms 内没有收到应答").arg(m_relayTimeoutMs);
            }
            return RelayStatus::HandshakeTimeout;
        }

        QString problem;
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(left)) {
            // 对端在我们还没拿到完整应答时就挂了。这最可能是「它刚刚崩了」
            // ——回 NoPrimary 让调用方去试一次标识回收，那才是真正该做的事。
            if (socket.bytesAvailable() == 0
                && socket.state() == QLocalSocket::UnconnectedState) {
                if (detail) {
                    *detail = QStringLiteral("端点在接受参数后断开：%1").arg(socket.errorString());
                }
                return RelayStatus::NoPrimary;
            }
            if (detail) {
                // 时限要写进消息里：排查「为什么这次降级了」时，第一个要问的
                // 就是「等了多久」，而它是调用方设的，不在这一层能猜出来。
                *detail = QStringLiteral("等待应答失败（时限 %1ms）：%2")
                              .arg(m_relayTimeoutMs)
                              .arg(socket.errorString());
            }
            return RelayStatus::HandshakeTimeout;
        }

        buffer.append(socket.readAll());
        const FrameStatus status = inspectRelayFrame(buffer, &frameSize, &problem);
        if (status == FrameStatus::Incomplete) {
            continue;
        }
        if (status == FrameStatus::Invalid) {
            if (detail) {
                *detail = QStringLiteral("应答不是本协议的帧：%1").arg(problem);
            }
            return RelayStatus::Rejected;
        }
        break;
    }

    RelayReply reply;
    QString problem;
    if (!decodeRelayReply(buffer.left(frameSize), &reply, &problem)) {
        if (detail) {
            *detail = QStringLiteral("应答解不开：%1").arg(problem);
        }
        return RelayStatus::Rejected;
    }
    if (!reply.accepted) {
        if (detail) {
            *detail = reply.detail.isEmpty() ? QStringLiteral("首个实例拒绝了这份参数")
                                            : reply.detail;
        }
        return RelayStatus::Rejected;
    }

    // 首个实例回话时带上了它解析出的参数个数。它跟我们对不上，说明字节在
    // 传输里被改过或截断过——只看「它接受了」是发现不了这件事的。
    if (reply.argumentCount != m_request.arguments.size()) {
        if (detail) {
            *detail = QStringLiteral("首个实例收到的参数个数是 %1，我们发出去的是 %2")
                          .arg(reply.argumentCount)
                          .arg(m_request.arguments.size());
        }
        return RelayStatus::Rejected;
    }

    if (detail) {
        *detail = reply.detail.isEmpty()
            ? QStringLiteral("已把 %1 个参数交给正在运行的实例").arg(reply.argumentCount)
            : reply.detail;
    }
    return RelayStatus::Delivered;
}

// -----------------------------------------------------------------------------
// 首个实例侧：接收
// -----------------------------------------------------------------------------

void SingleInstanceGuard::onNewConnection()
{
    if (!m_server) {
        return;
    }
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, &SingleInstanceGuard::onReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &SingleInstanceGuard::onSocketFinished);

        // 连上来却迟迟不发完整帧的客户端不能一直占着连接。超时按同一个
        // 时限处理，并把原因报出去（否则现象只是「连接数慢慢涨」）。
        QTimer *deadline = new QTimer(socket);
        deadline->setSingleShot(true);
        deadline->setInterval(m_relayTimeoutMs);
        connect(deadline, &QTimer::timeout, this, [this, socket]() {
            if (!m_buffers.contains(socket)) {
                return;
            }
            const QString problem =
                QStringLiteral("端点上的连接在 %1ms 内没有发来完整的一帧").arg(m_relayTimeoutMs);
            emit relayProblem(problem);
            RelayReply reply;
            reply.accepted = false;
            reply.detail = problem;
            finishSocket(socket, encodeRelayReply(reply));
        });
        deadline->start();
    }
}

void SingleInstanceGuard::onReadyRead()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket || !m_buffers.contains(socket)) {
        return;
    }
    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    int frameSize = 0;
    QString problem;
    switch (inspectRelayFrame(buffer, &frameSize, &problem)) {
    case FrameStatus::Incomplete:
        return;
    case FrameStatus::Invalid:
        emit relayProblem(problem);
        replyRejection(socket, problem);
        return;
    case FrameStatus::Complete:
        break;
    }

    RelayRequest request;
    if (!decodeRelayRequest(buffer.left(frameSize), &request, &problem)) {
        emit relayProblem(problem);
        replyRejection(socket, problem);
        return;
    }

    if (QTimer *deadline = socket->findChild<QTimer *>()) {
        deadline->stop();
    }

    deliverRequest(request);

    RelayReply reply;
    reply.accepted = true;
    reply.argumentCount = request.arguments.size();
    reply.detail = QStringLiteral("已交给正在运行的实例");
    finishSocket(socket, encodeRelayReply(reply));
}

void SingleInstanceGuard::onSocketFinished()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    m_buffers.remove(socket);
    socket->deleteLater();
}

void SingleInstanceGuard::deliverRequest(const RelayRequest &request)
{
    emit relayReceived(request);

    // 「用户刚才有没有在打字」只有界面层知道，因此本模块只**问一次**，
    // 把结论放进信号里让界面照着做。界面若自己再判一次，两处判断迟早
    // 分歧——而分歧的表现是「有时抢焦点、有时不抢」，最难查。
    const int age = m_lastInputAgeProvider ? m_lastInputAgeProvider() : -1;
    const bool raise = shouldActivateWindow(m_activationPolicy, age, m_activationQuietWindowMs);
    emit activationRequested(request.arguments, raise);
}

void SingleInstanceGuard::replyRejection(QLocalSocket *socket, const QString &problem)
{
    RelayReply reply;
    reply.accepted = false;
    reply.detail = problem;
    finishSocket(socket, encodeRelayReply(reply));
}

void SingleInstanceGuard::finishSocket(QLocalSocket *socket, const QByteArray &reply)
{
    if (!socket) {
        return;
    }
    // 只摘掉继续读的连接：`disconnected` 那条还要用来做清理。
    disconnect(socket, &QLocalSocket::readyRead, this, &SingleInstanceGuard::onReadyRead);
    if (!reply.isEmpty()) {
        socket->write(reply);
        // 先 flush 再断开：应答已经落进内核缓冲，对端读得到；
        // 不 flush 直接断开会有一个「有时收得到、有时收不到」的窗口。
        socket->flush();
    }
    m_buffers.remove(socket);
    socket->disconnectFromServer();
    socket->deleteLater();
}

void SingleInstanceGuard::releaseEverything()
{
    // 顺序：先断连接、再摘端点、最后放标识。中间那一小段里标识仍被我们
    // 持有，因此不会有别的实例误判「没人在跑」而同时启动。
    const QList<QLocalSocket *> sockets = m_buffers.keys();
    for (QLocalSocket *socket : sockets) {
        if (socket) {
            socket->abort();
            delete socket;
        }
    }
    m_buffers.clear();

    if (m_server) {
        // close() 在 Unix 上会摘掉套接字文件（析构也会，但显式做一次更早）。
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }

    // QSharedMemory 的析构会 detach；引用计数归零时 Qt 会真正删掉那段内存
    // （Qt 5.15 的 System V 后端）。最后再释放进程锁，允许下一实例接手。
    m_sharedMemory.reset();
    m_identifierLock.reset();
    m_sharedMemoryProblem.clear();
    m_primary = false;
}

} // namespace Platform
} // namespace LqCompare
