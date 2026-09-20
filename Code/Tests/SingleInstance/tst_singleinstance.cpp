#include "tst_singleinstance.h"

#include "instanceprotocol.h"
#include "singleinstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSharedMemory>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <cstdlib>
#include <vector>

// 崩溃用例要真的「跳过所有析构」，只有 _exit 做得到。
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

using namespace LqCompare::Platform;

namespace {

///
/// \brief 子进程往这个目录里写「它收到了什么」，父进程据此断言。
///
/// 函数内静态的 `QTemporaryDir`：它会在**第一次调用**时创建（那时
/// `QCoreApplication` 已经在），并在进程结束时删除，因此既不必手工清理，
/// 也不会有「静态初始化早于应用构造」的风险。
///
QString outputRoot()
{
    static QTemporaryDir directory;
    return directory.path();
}

// -----------------------------------------------------------------------------
// 子进程模式
//
// 父进程把本套件的可执行文件再启动一次，用环境变量告诉它扮演什么角色。
// 用环境变量而不是命令行参数：`QTest::qExec` 会把不认识的参数当成错误，
// 而子进程里仍然要构造 `QCoreApplication`，走环境变量就不用跟它抢命令行。
// -----------------------------------------------------------------------------

QString childMode()
{
    return QString::fromLocal8Bit(qgetenv("LQCOMPARE_TST_SI_MODE"));
}

QString childSeed()
{
    return QString::fromLocal8Bit(qgetenv("LQCOMPARE_TST_SI_SEED"));
}

QString childOutput()
{
    return QString::fromLocal8Bit(qgetenv("LQCOMPARE_TST_SI_OUT"));
}

QString childWorkingDirectory()
{
    return QString::fromLocal8Bit(qgetenv("LQCOMPARE_TST_SI_CWD"));
}

QStringList childArguments()
{
    const QByteArray raw = qgetenv("LQCOMPARE_TST_SI_ARGS");
    if (raw.isEmpty()) {
        return QStringList();
    }
    QStringList arguments;
    for (const QJsonValue &value : QJsonDocument::fromJson(raw).array()) {
        arguments.append(value.toString());
    }
    return arguments;
}

void announceReady()
{
    std::fputs("READY\n", stdout);
    std::fflush(stdout);
}

void appendReport(const QString &line)
{
    const QString path = childOutput();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    file.write(line.toUtf8());
    file.write("\n");
    file.flush();
}

// 故障服务使用同一进程锁占住标识，允许在 OS 共享内存耗尽时继续验证
// 真实套接字拒绝、超时与活实例不可抢占；生命周期由局部 unique_ptr 管理。
bool createIdentifierAndListen(const QString &seed, std::unique_ptr<QLockFile> *memory,
                               std::unique_ptr<QLocalServer> *server)
{
    const QString endpoint = instanceEndpointName(seed, defaultInstanceUserScope());
    *memory = std::make_unique<QLockFile>(
        QDir(QDir::tempPath()).filePath(endpoint + QStringLiteral(".lock")));
    (*memory)->setStaleLockTime(0);
    if (!(*memory)->tryLock(0)) {
        std::fprintf(stdout, "LOCK-FAILED:%d\n", int((*memory)->error()));
        std::fflush(stdout);
        return false;
    }
    if (!server) {
        return true;
    }
    *server = std::make_unique<QLocalServer>();
    QLocalServer::removeServer(endpoint);
    if (!(*server)->listen(endpoint)) {
        std::fputs("LISTEN-FAILED\n", stdout);
        std::fflush(stdout);
        return false;
    }
    return true;
}

///
/// \brief 跑一个子进程角色。返回 false 表示「本次运行不是子进程」。
///
/// 「守着不放」的那几个角色必须交给事件循环（`exec()`）——进程一旦退出，
/// 内核就会把它挂着的共享内存段摘掉，那时它留下的就只是一个**陈迹**，
/// 而「活着的实例」与「陈迹」正是本套件要区分的两件事。
///
bool runChildProcessIfRequested(int *exitCode)
{
    const QString mode = childMode();
    if (mode.isEmpty()) {
        return false;
    }
    if (exitCode) {
        *exitCode = 0;
    }

    if (mode == QLatin1String("hold-primary") || mode == QLatin1String("compete-primary")) {
        // 真正的首个实例：占标识、监听、收下转交过来的参数并写进文件。
        SingleInstanceGuard guard;
        QObject::connect(&guard, &SingleInstanceGuard::relayReceived,
                         [](const RelayRequest &request) {
                             appendReport(QStringLiteral("RECEIVED-CWD=%1")
                                              .arg(request.workingDirectory));
                             appendReport(QStringLiteral("RECEIVED-COUNT=%1")
                                              .arg(request.arguments.size()));
                             for (const QString &argument : request.arguments) {
                                 appendReport(QStringLiteral("RECEIVED-ARG=%1").arg(argument));
                             }
                         });
        QObject::connect(&guard, &SingleInstanceGuard::activationRequested,
                         [](const QStringList &arguments, bool raiseWindow) {
                             appendReport(QStringLiteral("RAISE=%1").arg(raiseWindow ? 1 : 0));
                             appendReport(QStringLiteral("RAISE-COUNT=%1").arg(arguments.size()));
                         });
        QObject::connect(&guard, &SingleInstanceGuard::relayProblem, [](const QString &problem) {
            appendReport(QStringLiteral("PROBLEM=%1").arg(problem));
        });
        const InstanceStartReport report = guard.start(childSeed());
        appendReport(QStringLiteral("ROLE=%1")
                         .arg(QString::fromLatin1(instanceRoleIdentifier(report.role))));
        if (mode == QLatin1String("compete-primary") && report.shouldExit()) {
            announceReady();
            if (exitCode) *exitCode = report.exitCode();
            return true;
        }
        if (!guard.isPrimary() || !guard.isListening()) {
            std::fprintf(stderr, "PRIMARY-FAILED:%s\n", qPrintable(report.detail));
            if (exitCode) *exitCode = 8;
            return true;
        }
        announceReady();
        if (exitCode) {
            *exitCode = QCoreApplication::exec();
        }
        return true;
    }

    if (mode == QLatin1String("crash-primary")) {
        // **故意**不析构：跳过所有清理，于是共享内存段与套接字文件都留在
        // 系统里——与一次真正的崩溃（SIGKILL）等价。这正是要验证的那条路径。
        auto *guard = new SingleInstanceGuard;
        const auto report = guard->start(childSeed());
        if (!guard->isPrimary() || !guard->isListening()) {
            std::fprintf(stderr, "CRASH-SETUP-FAILED:%s\n", qPrintable(report.detail));
            if (exitCode) *exitCode = 8;
            delete guard;
            return true;
        }
        announceReady();
        std::fflush(stdout);
#ifdef Q_OS_WIN
        std::_Exit(0);
#else
        ::_exit(0);
#endif
    }

    if (mode == QLatin1String("hold-identifier-only")) {
        // 占着进程锁却**从不监听**：模拟「首个实例拿到了标识但端点没起来」
        // 这个真实存在的中间状态（startListening 失败时会落到这里）。
        std::unique_ptr<QLockFile> memory;
        if (!createIdentifierAndListen(childSeed(), &memory, nullptr)) {
            return true;
        }
        announceReady();
        if (exitCode) {
            *exitCode = QCoreApplication::exec();
        }
        return true;
    }

    if (mode == QLatin1String("delayed-server")) {
        std::unique_ptr<QLockFile> identifier;
        if (!createIdentifierAndListen(childSeed(), &identifier, nullptr)) return true;
        QLocalServer server;
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&server]() {
            while (QLocalSocket *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
                    RelayRequest request;
                    if (!decodeRelayRequest(socket->readAll(), &request, nullptr)) return;
                    RelayReply reply;
                    reply.accepted = true;
                    reply.argumentCount = request.arguments.size();
                    socket->write(encodeRelayReply(reply));
                    socket->flush();
                    socket->disconnectFromServer();
                });
            }
        });
        QTimer::singleShot(120, &server, [&server]() {
            server.listen(instanceEndpointName(childSeed(), defaultInstanceUserScope()));
        });
        announceReady();
        if (exitCode) *exitCode = QCoreApplication::exec();
        return true;
    }

    if (mode == QLatin1String("silent-server")) {
        // 连上来也不读、不回：模拟「首个实例卡住了」。
        std::unique_ptr<QLockFile> memory;
        std::unique_ptr<QLocalServer> server;
        if (!createIdentifierAndListen(childSeed(), &memory, &server)) {
            return true;
        }
        Q_UNUSED(server)
        announceReady();
        if (exitCode) {
            *exitCode = QCoreApplication::exec();
        }
        return true;
    }

    if (mode == QLatin1String("rejecting-server")) {
        std::unique_ptr<QLockFile> memory;
        std::unique_ptr<QLocalServer> server;
        if (!createIdentifierAndListen(childSeed(), &memory, &server)) {
            return true;
        }
        QObject::connect(server.get(), &QLocalServer::newConnection, server.get(), [server = server.get()]() {
            while (QLocalSocket *socket = server->nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
                    socket->readAll();
                    RelayReply reply;
                    reply.accepted = false;
                    reply.detail = QStringLiteral("测试用的拒绝");
                    socket->write(encodeRelayReply(reply));
                    socket->flush();
                    socket->disconnectFromServer();
                    socket->deleteLater();
                });
            }
        });
        announceReady();
        if (exitCode) {
            *exitCode = QCoreApplication::exec();
        }
        return true;
    }

    if (mode == QLatin1String("send-garbage")) {
        // 一个**不是我们自己的**客户端：连上首个实例的端点，发一段不合协议的
        // 字节，然后把首个实例的应答写进文件。这条覆盖的是接收端的拒绝路径
        // ——一个宽松的实现会在这里分配一块声明出来的巨大内存。
        const QString endpoint = instanceEndpointName(childSeed(), defaultInstanceUserScope());
        QLocalSocket socket;
        socket.connectToServer(endpoint, QIODevice::ReadWrite);
        if (!socket.waitForConnected(3000)) {
            appendReport(QStringLiteral("CONNECT=failed:%1").arg(socket.errorString()));
            return true;
        }
        socket.write("NOT-A-FRAME-AT-ALL");
        socket.flush();
        socket.waitForBytesWritten(1000);

        QByteArray buffer;
        int frameSize = 0;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3000 && frameSize == 0) {
            if (socket.waitForReadyRead(200)) {
                buffer.append(socket.readAll());
                if (inspectRelayFrame(buffer, &frameSize, nullptr) != FrameStatus::Complete) {
                    frameSize = 0;
                }
            }
        }
        RelayReply reply;
        QString problem;
        if (frameSize > 0 && decodeRelayReply(buffer.left(frameSize), &reply, &problem)) {
            appendReport(QStringLiteral("REPLY-ACCEPTED=%1").arg(reply.accepted ? 1 : 0));
            appendReport(QStringLiteral("REPLY-DETAIL=%1").arg(reply.detail));
        } else {
            appendReport(QStringLiteral("REPLY-UNDECODABLE=%1").arg(problem));
        }
        return true;
    }

    if (mode == QLatin1String("miscounting-server")) {
        // 收下了、也回了「接受」，但**报出来的参数个数与我们发出去的不一样**。
        // 这正是应答里那个计数字段存在的理由：只看 `accepted` 是发现不了
        // 「字节在传输里被改过/截断过」的。
        std::unique_ptr<QLockFile> memory;
        std::unique_ptr<QLocalServer> server;
        if (!createIdentifierAndListen(childSeed(), &memory, &server)) {
            return true;
        }
        QObject::connect(server.get(), &QLocalServer::newConnection, server.get(), [server = server.get()]() {
            while (QLocalSocket *socket = server->nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
                    socket->readAll();
                    RelayReply reply;
                    reply.accepted = true;
                    reply.argumentCount = 99;
                    reply.detail = QStringLiteral("数错了");
                    socket->write(encodeRelayReply(reply));
                    socket->flush();
                    socket->disconnectFromServer();
                    socket->deleteLater();
                });
            }
        });
        announceReady();
        if (exitCode) {
            *exitCode = QCoreApplication::exec();
        }
        return true;
    }

    if (mode == QLatin1String("connect-only")) {
        // 连上了，但**一个字节都不发**。接收端必须有一个读的截止时间，
        // 否则这种客户端会一直占着连接（现象只是「连接数慢慢涨」）。
        const QString endpoint = instanceEndpointName(childSeed(), defaultInstanceUserScope());
        QLocalSocket socket;
        socket.connectToServer(endpoint, QIODevice::ReadWrite);
        if (!socket.waitForConnected(3000)) {
            appendReport(QStringLiteral("CONNECT=failed:%1").arg(socket.errorString()));
            return true;
        }
        QByteArray buffer;
        int frameSize = 0;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000 && frameSize == 0) {
            if (socket.waitForReadyRead(200)) {
                buffer.append(socket.readAll());
                if (inspectRelayFrame(buffer, &frameSize, nullptr) != FrameStatus::Complete) {
                    frameSize = 0;
                }
            }
        }
        RelayReply reply;
        QString problem;
        if (frameSize > 0 && decodeRelayReply(buffer.left(frameSize), &reply, &problem)) {
            appendReport(QStringLiteral("REPLY-ACCEPTED=%1").arg(reply.accepted ? 1 : 0));
            appendReport(QStringLiteral("REPLY-DETAIL=%1").arg(reply.detail));
        } else {
            appendReport(QStringLiteral("REPLY-NOTHING"));
        }
        return true;
    }

    if (mode == QLatin1String("relay-and-exit")) {
        // 真正的第二个实例：交不出去时以转发结果的退出码结束。
        // 父进程断言的就是这个进程的退出码。
        SingleInstanceGuard guard;
        guard.setRelayFallback(RelayFallback::ExitWithRelayCode);
        guard.setRelayTimeoutMs(400);
        RelayRequest request;
        request.workingDirectory = childWorkingDirectory();
        request.arguments = childArguments();
        guard.setRelayRequest(request);
        const InstanceStartReport report = guard.start(childSeed());
        appendReport(QStringLiteral("ROLE=%1")
                         .arg(QString::fromLatin1(instanceRoleIdentifier(report.role))));
        appendReport(QStringLiteral("RELAY=%1")
                         .arg(QString::fromLatin1(relayStatusIdentifier(report.relay))));
        if (exitCode && report.shouldExit()) {
            *exitCode = report.exitCode();
        }
        return true;
    }

    // 认不出的模式不能静默当成「不是子进程」——那会让父进程等一个永远不来的
    // READY，最后只看到一句超时。
    std::fprintf(stderr, "unknown child mode: %s\n", qPrintable(mode));
    std::fflush(stderr);
    if (exitCode) {
        *exitCode = 9;
    }
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// 子进程的父进程侧封装
// -----------------------------------------------------------------------------

namespace {

///
/// \brief 一个被父进程控制的子进程。
///
/// 两个细节决定了它必须自己写而不是直接用 `QProcess`：
///   1. **等的时候要跑事件循环**。父进程自己在某些用例里就是首个实例，
///      子进程要靠父进程的服务器回话；直接 `waitForFinished()` 会把父进程
///      的事件循环堵死，于是子进程超时——而这与代码对错毫无关系。
///      因此所有等待都走 `QTest::qWait` 轮询。
///   2. **退出码是断言对象**。`relay-and-exit` 那条路径的全部意义就是
///      「第二个实例带着 10 或者 12 退出」，因此进程要留着取 `exitCode()`。
///
class ChildProcess
{
public:
    ChildProcess(const QString &mode,
                 const QString &seed,
                 const QStringList &forwardArguments = QStringList(),
                 const QString &workingDirectory = QString(),
                 const QString &outputPath = QString())
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LQCOMPARE_TST_SI_MODE"), mode);
        environment.insert(QStringLiteral("LQCOMPARE_TST_SI_SEED"), seed);
        m_stopPath = m_controlDirectory.filePath(QStringLiteral("stop"));
        environment.insert(QStringLiteral("LQCOMPARE_TST_SI_STOP"), m_stopPath);
        if (!outputPath.isEmpty()) {
            environment.insert(QStringLiteral("LQCOMPARE_TST_SI_OUT"), outputPath);
        }
        if (!workingDirectory.isEmpty()) {
            environment.insert(QStringLiteral("LQCOMPARE_TST_SI_CWD"), workingDirectory);
        }
        if (!forwardArguments.isEmpty()) {
            environment.insert(QStringLiteral("LQCOMPARE_TST_SI_ARGS"),
                               QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(forwardArguments))
                                                    .toJson(QJsonDocument::Compact)));
        }
        m_process.setProcessEnvironment(environment);
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.start(QCoreApplication::applicationFilePath(), QStringList());
    }

    ~ChildProcess()
    {
        stop();
    }

    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;

    QProcess *process() { return &m_process; }

    /// 等到子进程把 READY 打出来（或它自己先退出了）。
    bool waitForReady(int milliseconds = 15000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            if (m_process.waitForReadyRead(50)) {
                m_stdout.append(m_process.readAll());
            }
            if (m_stdout.contains("READY")) {
                return true;
            }
            if (m_process.state() == QProcess::NotRunning) {
                return m_stdout.contains("READY");
            }
            QTest::qWait(10);
        }
        return false;
    }

    /// 轮询等它退出。**不能**用 `waitForFinished()`：见类注释。
    bool waitForFinished(int milliseconds = 15000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            if (m_process.waitForReadyRead(20)) {
                m_stdout.append(m_process.readAll());
            }
            if (m_process.state() == QProcess::NotRunning) {
                m_stdout.append(m_process.readAll());
                return true;
            }
            QTest::qWait(10);
        }
        return false;
    }

    int exitCode() const { return m_process.exitCode(); }
    QString childOutput() const { return QString::fromLocal8Bit(m_stdout); }

    void stop()
    {
        if (m_process.state() == QProcess::NotRunning) {
            return;
        }
        QFile stopFile(m_stopPath);
        if (stopFile.open(QIODevice::WriteOnly)) {
            stopFile.close();
        }
        if (!waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(3000);
        }
    }

private:
    QTemporaryDir m_controlDirectory;
    QString m_stopPath;
    QProcess m_process;
    QByteArray m_stdout;
};

///
/// \brief 轮询等一个文件里出现某一行。同样要跑事件循环。
///
bool waitForOutputLine(const QString &path, const QString &needle, int milliseconds = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (QString::fromUtf8(file.readAll()).contains(needle)) {
                return true;
            }
        }
        QTest::qWait(10);
    }
    return false;
}

QString readOutputFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString valueOfLine(const QString &text, const QString &key)
{
    const QString prefix = key + QLatin1Char('=');
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(prefix)) {
            return line.mid(prefix.size());
        }
    }
    return QString();
}

QStringList valuesOfLine(const QString &text, const QString &key)
{
    QStringList values;
    const QString prefix = key + QLatin1Char('=');
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(prefix)) {
            values.append(line.mid(prefix.size()));
        }
    }
    return values;
}

/// 打开一帧，返回它声明的长度；构造畸形帧时用得上。
QByteArray withByteAt(const QByteArray &frame, int index, char value)
{
    QByteArray copy = frame;
    copy[index] = value;
    return copy;
}

} // namespace

// -----------------------------------------------------------------------------
// 自定义 main
// -----------------------------------------------------------------------------

///
/// \brief 自定义 main，而不是 `QTEST_MAIN`。
///
/// 唯一的原因是：本套件需要把**自己**再启动一次来扮演第二个进程，而
/// `QTEST_MAIN` 生成的 main 会把命令行原样交给 `QTest::qExec`，没有办法
/// 在它之前拦一道。因此这里手写同一个流程（构造应用、`QTest::qExec`），
/// 只在前面加一次「要不要跑子进程角色」的判断。
///
/// 子进程模式用环境变量传递，命令行因此保持干净：`run-tests.sh` 传的
/// `-o -,txt` 照常被 `QTest::qExec` 解析。
///
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tst_singleinstance"));

    QTimer shutdown;
    if (!childMode().isEmpty()) {
        const QString stopPath = QString::fromLocal8Bit(qgetenv("LQCOMPARE_TST_SI_STOP"));
        QObject::connect(&shutdown, &QTimer::timeout, &application, [&application, stopPath]() {
            if (!stopPath.isEmpty() && QFile::exists(stopPath)) application.quit();
        });
        shutdown.start(20);
        // 父进程被中断也不能让测试守护进程永久占住资源。
        QTimer::singleShot(30000, &application, &QCoreApplication::quit);
    }
    int childExitCode = 0;
    if (runChildProcessIfRequested(&childExitCode)) {
        return childExitCode;
    }

    Tst_SingleInstance testCase;
    return QTest::qExec(&testCase, argc, argv);
}

// -----------------------------------------------------------------------------
// 夹具
// -----------------------------------------------------------------------------

void Tst_SingleInstance::initTestCase()
{
    m_outputDirectory = outputRoot();
    QVERIFY2(!m_outputDirectory.isEmpty() && QDir(m_outputDirectory).exists(),
             qPrintable(m_outputDirectory));
    QVERIFY2(!defaultInstanceUserScope().isEmpty(), "用户作用域不该为空");
    qInfo("临时输出目录：%s", qPrintable(m_outputDirectory));
}

void Tst_SingleInstance::init()
{
    m_seed = makeSeed();
}

void Tst_SingleInstance::cleanup()
{
    // 即使断言提前失败，也只回收本用例唯一标识的遗留资源。常规子进程
    // 正常退出并析构；_exit 只用于明确测试崩溃的角色。
    const QString scope = defaultInstanceUserScope();
    QSharedMemory probe(instanceSharedMemoryKey(m_seed, scope));
    if (probe.attach()) {
        probe.detach();
    }
    const QString endpoint = instanceEndpointName(m_seed, scope);
    QLocalServer::removeServer(endpoint);
    QLockFile lock(QDir(QDir::tempPath()).filePath(endpoint + QStringLiteral(".lock")));
    lock.setStaleLockTime(0);
    if (lock.tryLock(0)) lock.unlock();
}

QString Tst_SingleInstance::makeSeed()
{
    return QStringLiteral("tstsi-%1-%2")
        .arg(QUuid::createUuid().toString(QUuid::Id128))
        .arg(m_counter++);
}

QString Tst_SingleInstance::outputPath(const QString &name) const
{
    return m_outputDirectory + QLatin1Char('/') + name + QStringLiteral(".txt");
}

QString Tst_SingleInstance::readOutput(const QString &path) const
{
    return readOutputFile(path);
}

QString Tst_SingleInstance::readSourceFile(const QString &relativePath) const
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// =============================================================================
// A 标识符：端点名与共享内存键
// =============================================================================

void Tst_SingleInstance::sanitizeKeepsSafeCharacters()
{
    QCOMPARE(sanitizeInstanceToken(QStringLiteral("LqCompare-2.1_beta")),
             QStringLiteral("LqCompare-2.1_beta"));
}

void Tst_SingleInstance::sanitizeReplacesPathSeparatorsAndSpaces()
{
    // 共享内存键在 Unix 上会变成一个**文件名**、端点名会变成一个**路径**，
    // 因此 `/` 必须被换掉——它不会报错，只会写到别的地方去。
    const QString sanitized = sanitizeInstanceToken(QStringLiteral("a/b c:d"));
    QVERIFY2(!sanitized.contains(QLatin1Char('/')), qPrintable(sanitized));
    QVERIFY2(!sanitized.contains(QLatin1Char(' ')), qPrintable(sanitized));
    QVERIFY2(!sanitized.contains(QLatin1Char(':')), qPrintable(sanitized));
    QVERIFY(sanitized.startsWith(QStringLiteral("a_b_c_d")));
}

void Tst_SingleInstance::sanitizeAppendsFingerprintWhenItReplacedSomething()
{
    // 没有被替换过时**不追加**特征值：绝大多数正常输入应当得到干净的名字。
    QCOMPARE(sanitizeInstanceToken(QStringLiteral("clean")), QStringLiteral("clean"));

    const QString sanitized = sanitizeInstanceToken(QStringLiteral("with space"));
    // "with_space" 后面还要跟一段 8 位特征值（下划线连接）。
    QCOMPARE(sanitized, QStringLiteral("with_space_") + stableTokenFingerprint(QStringLiteral("with space")));
}

void Tst_SingleInstance::sanitizeDistinguishesTwoNonAsciiNames()
{
    // 净化会把两个中文名都变成一串下划线。若不加特征值，这两个用户会撞进
    // 同一个标识——其中一个的程序会被另一个的实例吞掉，而他看到的只是
    // 「点了没反应」。
    const QString first = sanitizeInstanceToken(QStringLiteral("张三"));
    const QString second = sanitizeInstanceToken(QStringLiteral("李四"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY2(first != second, "两个不同的用户必须得到不同的标识");
}

void Tst_SingleInstance::sanitizeReturnsEmptyForBlankInput()
{
    QVERIFY(sanitizeInstanceToken(QString()).isEmpty());
}

void Tst_SingleInstance::fingerprintIsStableForAKnownInput()
{
    // **快照**：这个取值必须跨进程、跨运行、跨平台一致。本仓有一个坑是
    // 「拿 qHash() 做标识」——它对每个进程使用随机种子，于是第一个实例
    // 记下的名字与第二个实例算出来的永远对不上，单实例机制静默失效。
    // 这条用例把「用的是稳定哈希」变成一条会红的断言。
    QCOMPARE(stableTokenFingerprint(QStringLiteral("LqCompare")), QStringLiteral("ffb2e8d5"));
    QCOMPARE(stableTokenFingerprint(QStringLiteral("tst-single-instance")),
             QStringLiteral("17fd112d"));
    // 中文走的是 UTF-8 字节，不是 UTF-16 码元。
    QCOMPARE(stableTokenFingerprint(QStringLiteral("用户名")), QStringLiteral("367cc513"));
    QCOMPARE(stableTokenFingerprint(QStringLiteral("LqCompare")).size(), 8);
}

void Tst_SingleInstance::endpointNameCombinesPrefixUserAndSeed()
{
    const QString endpoint = instanceEndpointName(QStringLiteral("LqCompare"), QStringLiteral("loren"));
    QCOMPARE(endpoint, QStringLiteral("lqcompare-loren-LqCompare-ffea25ea"));
}

void Tst_SingleInstance::endpointNameStaysWithinTheLimitAndKeepsDistinctness()
{
    const QString scope = QStringLiteral("loren");
    const QString longA = QString(120, QLatin1Char('a')) + QStringLiteral("-project-alpha");
    const QString longB = QString(120, QLatin1Char('a')) + QStringLiteral("-project-beta");

    const QString first = instanceEndpointName(longA, scope);
    const QString second = instanceEndpointName(longB, scope);

    QVERIFY2(first.size() <= instanceEndpointNameLimit(), qPrintable(first));
    QVERIFY2(second.size() <= instanceEndpointNameLimit(), qPrintable(second));
    // 直接砍尾巴会让两个只在后半段不同的种子得到同一个名字——表现是
    // 「另一个程序的实例把自己的实例顶掉」。因此超长时要带特征值。
    QVERIFY2(first != second, "超长种子被截断后必须仍然互相可区分");
}

void Tst_SingleInstance::sharedMemoryKeyRespectsThePosixNameLimit()
{
    const QString key = instanceSharedMemoryKey(QString(200, QLatin1Char('z')), QStringLiteral("loren"));
    QVERIFY2(key.size() <= instanceSharedMemoryKeyLimit(), qPrintable(key));
    QVERIFY2(!key.contains(QLatin1Char('/')), qPrintable(key));
    // 短种子不受影响。
    QCOMPARE(instanceSharedMemoryKey(QStringLiteral("LqCompare"), QStringLiteral("loren")),
             QStringLiteral("lqc-loren-LqCompare-cf933b76"));
}

void Tst_SingleInstance::identifierDistinguishesRawInputFromSanitizedInput()
{
    const QString raw = QStringLiteral("a b");
    const QString sanitized = sanitizeInstanceToken(raw);
    QVERIFY(instanceEndpointName(raw, QStringLiteral("loren"))
            != instanceEndpointName(sanitized, QStringLiteral("loren")));
    QVERIFY(instanceSharedMemoryKey(raw, QStringLiteral("loren"))
            != instanceSharedMemoryKey(sanitized, QStringLiteral("loren")));
}

void Tst_SingleInstance::identifierKeepsFieldBoundaries()
{
    QVERIFY(instanceEndpointName(QStringLiteral("c"), QStringLiteral("a-b"))
            != instanceEndpointName(QStringLiteral("b-c"), QStringLiteral("a")));
    QVERIFY(instanceSharedMemoryKey(QStringLiteral("c"), QStringLiteral("a-b"))
            != instanceSharedMemoryKey(QStringLiteral("b-c"), QStringLiteral("a")));
}

void Tst_SingleInstance::userScopeFallsBackToAnonymous()
{
    QCOMPARE(instanceUserScopeFromName(QByteArray()), QStringLiteral("anonymous"));
    QCOMPARE(instanceUserScopeFromName(QByteArray("  ")), QStringLiteral("anonymous"));
    QCOMPARE(instanceUserScopeFromName(QByteArray("loren")), QStringLiteral("loren"));
    QCOMPARE(instanceUserScopeFromName(QByteArray("  loren  ")), QStringLiteral("loren"));
}

// =============================================================================
// B 线协议：编解码
// =============================================================================

void Tst_SingleInstance::requestRoundTripsWorkingDirectoryAndArguments()
{
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/Users/loren/work");
    request.arguments = QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt")};

    RelayRequest decoded;
    QString problem;
    QVERIFY2(decodeRelayRequest(encodeRelayRequest(request), &decoded, &problem), qPrintable(problem));
    QCOMPARE(decoded.workingDirectory, request.workingDirectory);
    QCOMPARE(decoded.arguments, request.arguments);
}

void Tst_SingleInstance::requestRoundTripsEmptyArgumentList()
{
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/tmp");
    RelayRequest decoded;
    QVERIFY(decodeRelayRequest(encodeRelayRequest(request), &decoded, nullptr));
    QVERIFY(decoded.arguments.isEmpty());
    QCOMPARE(decoded.workingDirectory, QStringLiteral("/tmp"));
}

void Tst_SingleInstance::requestRoundTripsEmptyStringArgument()
{
    // 空字符串是**一个参数**，不是「没有参数」。用「零字节表示结束」那种
    // 编码写法会把它丢掉，而 `lqcompare "" b.txt` 在真实命令行里是合法的。
    RelayRequest request;
    request.arguments = QStringList{QString(), QStringLiteral("b.txt"), QString()};
    RelayRequest decoded;
    QVERIFY(decodeRelayRequest(encodeRelayRequest(request), &decoded, nullptr));
    QCOMPARE(decoded.arguments.size(), 3);
    QCOMPARE(decoded.arguments.at(0), QString());
    QCOMPARE(decoded.arguments.at(1), QStringLiteral("b.txt"));
    QCOMPARE(decoded.arguments.at(2), QString());
}

void Tst_SingleInstance::requestRoundTripsUnicodeArguments()
{
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/Users/loren/桌面");
    request.arguments = QStringList{QStringLiteral("报告 v2.txt"), QStringLiteral("图像.png")};
    RelayRequest decoded;
    QString problem;
    QVERIFY2(decodeRelayRequest(encodeRelayRequest(request), &decoded, &problem), qPrintable(problem));
    QCOMPARE(decoded.workingDirectory, request.workingDirectory);
    QCOMPARE(decoded.arguments, request.arguments);
}

void Tst_SingleInstance::requestRoundTripsManyArguments()
{
    RelayRequest request;
    request.arguments.reserve(512);
    for (int i = 0; i < 512; ++i) {
        request.arguments.append(QStringLiteral("arg-%1").arg(i));
    }
    RelayRequest decoded;
    QVERIFY(decodeRelayRequest(encodeRelayRequest(request), &decoded, nullptr));
    QCOMPARE(decoded.arguments, request.arguments);
}

void Tst_SingleInstance::replyRoundTripsAcceptance()
{
    RelayReply reply;
    reply.accepted = true;
    reply.argumentCount = 3;
    reply.detail = QStringLiteral("已交给正在运行的实例");

    RelayReply decoded;
    QString problem;
    QVERIFY2(decodeRelayReply(encodeRelayReply(reply), &decoded, &problem), qPrintable(problem));
    QCOMPARE(decoded.accepted, true);
    QCOMPARE(decoded.argumentCount, 3);
    QCOMPARE(decoded.detail, reply.detail);
}

void Tst_SingleInstance::replyRoundTripsRejectionDetail()
{
    RelayReply reply;
    reply.accepted = false;
    reply.detail = QStringLiteral("协议版本不一致");
    RelayReply decoded;
    QVERIFY(decodeRelayReply(encodeRelayReply(reply), &decoded, nullptr));
    QCOMPARE(decoded.accepted, false);
    QCOMPARE(decoded.detail, reply.detail);
}

void Tst_SingleInstance::decodeRejectsShortFrame()
{
    RelayRequest decoded;
    QString problem;
    QVERIFY(!decodeRelayRequest(QByteArray("LQ"), &decoded, &problem));
    QVERIFY2(!problem.isEmpty(), "拒绝必须给出原因");
}

void Tst_SingleInstance::decodeRejectsWrongMagic()
{
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a")};
    QByteArray frame = encodeRelayRequest(request);
    frame[0] = 'X';
    QVERIFY(!decodeRelayRequest(frame, nullptr, nullptr));
}

void Tst_SingleInstance::decodeRejectsWrongKind()
{
    // 把应答当请求解（或反过来）必须失败：两者载荷布局不同，混着解会读到
    // 完全不同的字段，而结果看起来「解出来了」。
    RelayReply reply;
    reply.accepted = true;
    reply.argumentCount = 1;
    QVERIFY(!decodeRelayRequest(encodeRelayReply(reply), nullptr, nullptr));
    RelayRequest request;
    QVERIFY(!decodeRelayReply(encodeRelayRequest(request), nullptr, nullptr));
}

void Tst_SingleInstance::decodeRejectsVersionMismatch()
{
    RelayRequest request;
    QByteArray frame = encodeRelayRequest(request);
    frame[5] = static_cast<char>(relayProtocolVersion() + 1);
    QString problem;
    QVERIFY(!decodeRelayRequest(frame, nullptr, &problem));
    QVERIFY2(problem.contains(QStringLiteral("协议版本")), qPrintable(problem));
}

void Tst_SingleInstance::decodeRejectsOversizedDeclaredPayload()
{
    RelayRequest request;
    QByteArray frame = encodeRelayRequest(request);
    // 声明一个远超上界的载荷长度：宽松的实现会据此去分配内存。
    const quint32 huge = relayMaxPayloadBytes() + 1;
    for (int i = 0; i < 4; ++i) {
        frame[6 + i] = static_cast<char>((huge >> (8 * (3 - i))) & 0xff);
    }
    QVERIFY(!decodeRelayRequest(frame, nullptr, nullptr));
}

void Tst_SingleInstance::decodeRejectsDeclaredAndActualMismatch()
{
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a")};
    QByteArray frame = encodeRelayRequest(request);
    frame.chop(1); // 声明不变、实际少一个字节
    QVERIFY(!decodeRelayRequest(frame, nullptr, nullptr));
}

void Tst_SingleInstance::decodeRejectsAFrameShorterThanItsDeclaredPayload()
{
    // 「声明得比实际多」是分片没到齐时的真实形态：调用方把一段还没收全的字节
    // 交给解码器。此处刻意让**已有的这段字节本身就是一个完整可解析的请求**，
    // 于是「声明的长度必须等于实际长度」是唯一能拦住它的检查——少了这一条，
    // 一段没到齐的帧会被当成完整的请求放行，第二个参数从此凭空消失。
    // 这条用例是反向验证补出来的：把 splitFrame 里那一比较去掉，只有它能变红。
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a")};
    QByteArray frame = encodeRelayRequest(request);

    // 头部布局：魔数 4 + 类型 1 + 版本 1 + 载荷长度 4（大端），因此长度在偏移 6。
    const int declaredOffset = 6;
    quint32 declared = 0;
    for (int i = 0; i < 4; ++i) {
        declared = (declared << 8) | static_cast<quint8>(frame.at(declaredOffset + i));
    }
    const quint32 raised = declared + 1;
    for (int i = 0; i < 4; ++i) {
        frame[declaredOffset + i] = static_cast<char>((raised >> (8 * (3 - i))) & 0xffu);
    }

    RelayRequest out;
    QVERIFY(!decodeRelayRequest(frame, &out, nullptr));
}

void Tst_SingleInstance::decodeRejectsTruncatedString()
{
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/tmp");
    request.arguments = QStringList{QStringLiteral("a.txt")};
    QByteArray frame = encodeRelayRequest(request);

    // 把「工作目录」那一段声明的长度改成一个超过剩余字节的值。
    const int payloadStart = relayFrameHeaderSize();
    const quint32 declared = 0x7fffffffu;
    for (int i = 0; i < 4; ++i) {
        frame[payloadStart + i] = static_cast<char>((declared >> (8 * (3 - i))) & 0xff);
    }
    QString problem;
    QVERIFY(!decodeRelayRequest(frame, nullptr, &problem));
    QVERIFY2(problem.contains(QStringLiteral("超过剩余")), qPrintable(problem));
}

void Tst_SingleInstance::decodeRejectsAbsurdArgumentCount()
{
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a")};
    QByteArray frame = encodeRelayRequest(request);

    // 参数个数在「工作目录字符串」之后：头部 + 工作目录长度字段 + 工作目录字节。
    const int countOffset = relayFrameHeaderSize() + 4 + request.workingDirectory.toUtf8().size();
    const quint32 huge = static_cast<quint32>(relayMaxArgumentCount() + 1);
    for (int i = 0; i < 4; ++i) {
        frame[countOffset + i] = static_cast<char>((huge >> (8 * (3 - i))) & 0xff);
    }
    QString problem;
    QVERIFY(!decodeRelayRequest(frame, nullptr, &problem));
    QVERIFY2(problem.contains(QStringLiteral("参数个数")), qPrintable(problem));
}

void Tst_SingleInstance::decodeRejectsInvalidUtf8()
{
    // 一次转发里的非法 UTF-8 意味着对面不是我们自己的客户端。静默把它换成
    // U+FFFD 会让首个实例去打开一个**名字不同**的文件，而那完全看不出来。
    RelayRequest request;
    request.workingDirectory = QStringLiteral("AB");
    QByteArray frame = encodeRelayRequest(request);
    const int payloadStart = relayFrameHeaderSize();
    // 把 "AB" 的第一个字节换成 0xFF（不是任何合法 UTF-8 序列的开头）。
    frame[payloadStart + 4] = static_cast<char>(0xff);
    QString problem;
    QVERIFY(!decodeRelayRequest(frame, nullptr, &problem));
    QVERIFY2(problem.contains(QStringLiteral("UTF-8")), qPrintable(problem));
}

void Tst_SingleInstance::decodeRejectsTrailingPayloadBytes()
{
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a")};
    QByteArray frame = encodeRelayRequest(request);
    // 尾部多一个字节：必须拒绝而不是忽略。忽略的话，「两帧粘在一起」会被
    // 静默当成一帧，第二个请求从此排在队列里等一个不会到来的帧头。
    frame.append('\0');
    QVERIFY(!decodeRelayRequest(frame, nullptr, nullptr));
}

void Tst_SingleInstance::decodeFailureLeavesOutputUntouched()
{
    RelayRequest decoded;
    decoded.workingDirectory = QStringLiteral("原有内容");
    decoded.arguments = QStringList{QStringLiteral("保留")};
    QVERIFY(!decodeRelayRequest(QByteArray("garbage"), &decoded, nullptr));
    QCOMPARE(decoded.workingDirectory, QStringLiteral("原有内容"));
    QCOMPARE(decoded.arguments, QStringList{QStringLiteral("保留")});
}

void Tst_SingleInstance::describeRelayRequestMentionsCountAndDirectory()
{
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/tmp");
    request.arguments = QStringList{QStringLiteral("a"), QStringLiteral("b")};
    const QString text = describeRelayRequest(request);
    QVERIFY2(text.contains(QStringLiteral("2")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("/tmp")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("a")), qPrintable(text));

    // 空的那几项要说清楚是「空」而不是留白，否则日志里什么都看不出来。
    RelayRequest empty;
    QVERIFY(describeRelayRequest(empty).contains(QStringLiteral("<空>")));
    QVERIFY(describeRelayRequest(empty).contains(QStringLiteral("<无>")));
}

// =============================================================================
// C 分帧：缓冲区里有没有一整帧
// =============================================================================

void Tst_SingleInstance::inspectWaitsForTheFullHeader()
{
    RelayRequest request;
    QByteArray frame = encodeRelayRequest(request);
    QCOMPARE(inspectRelayFrame(frame.left(relayFrameHeaderSize() - 1), nullptr, nullptr),
             FrameStatus::Incomplete);
}

void Tst_SingleInstance::inspectWaitsForTheWholePayload()
{
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a.txt")};
    const QByteArray frame = encodeRelayRequest(request);
    QVERIFY(frame.size() > relayFrameHeaderSize());
    QCOMPARE(inspectRelayFrame(frame.left(frame.size() - 1), nullptr, nullptr),
             FrameStatus::Incomplete);
}

void Tst_SingleInstance::inspectAcceptsExactlyOneFrame()
{
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a.txt")};
    const QByteArray frame = encodeRelayRequest(request);
    int frameSize = 0;
    QCOMPARE(inspectRelayFrame(frame, &frameSize, nullptr), FrameStatus::Complete);
    QCOMPARE(frameSize, frame.size());
}

void Tst_SingleInstance::inspectReportsTrailingBytesAsASeparateFrame()
{
    // 两帧粘在一起时，第一帧必须被完整识别出来（长度取自帧头），
    // 而不是「缓冲区有多大就当一帧有多大」。
    RelayRequest first;
    first.arguments = QStringList{QStringLiteral("a")};
    RelayRequest second;
    second.arguments = QStringList{QStringLiteral("b"), QStringLiteral("c")};

    const QByteArray firstFrame = encodeRelayRequest(first);
    QByteArray buffer = firstFrame + encodeRelayRequest(second);

    int frameSize = 0;
    QCOMPARE(inspectRelayFrame(buffer, &frameSize, nullptr), FrameStatus::Complete);
    QCOMPARE(frameSize, firstFrame.size());
    QVERIFY(frameSize < buffer.size());

    // 把第一帧切掉之后，剩下的应当正好是第二帧。
    QCOMPARE(inspectRelayFrame(buffer.mid(frameSize), &frameSize, nullptr), FrameStatus::Complete);
    QCOMPARE(frameSize, encodeRelayRequest(second).size());
}

void Tst_SingleInstance::inspectRejectsWrongMagic()
{
    RelayRequest request;
    QByteArray frame = encodeRelayRequest(request);
    frame = withByteAt(frame, 0, 'X');
    QString problem;
    QCOMPARE(inspectRelayFrame(frame, nullptr, &problem), FrameStatus::Invalid);
    QVERIFY2(problem.contains(QStringLiteral("魔数")), qPrintable(problem));
}

void Tst_SingleInstance::inspectRejectsOversizedDeclaration()
{
    // 没有这一条时，一个伪造的长度会让接收端**一直等**下去（等一个永远
    // 不会到来的几 GB 载荷），连接和内存就那样挂着。
    RelayRequest request;
    QByteArray frame = encodeRelayRequest(request);
    const quint32 huge = relayMaxPayloadBytes() + 1;
    for (int i = 0; i < 4; ++i) {
        frame[6 + i] = static_cast<char>((huge >> (8 * (3 - i))) & 0xff);
    }
    QCOMPARE(inspectRelayFrame(frame, nullptr, nullptr), FrameStatus::Invalid);
}

// =============================================================================
// D 退出码表
// =============================================================================

void Tst_SingleInstance::exitCodeTableCoversEveryStatusOnce()
{
    QVERIFY(validateRelayExitCodeTable(relayExitCodeTable()).isEmpty());
    QCOMPARE(relayExitCodeTable().size(), 5);
}

void Tst_SingleInstance::exitCodeIdentifiersAreDistinct()
{
    QStringList identifiers;
    for (const RelayExitCodeRow &row : relayExitCodeTable()) {
        const QString identifier = QString::fromLatin1(row.identifier);
        QVERIFY2(!identifier.isEmpty(), "每一行都要有机器标识");
        QVERIFY2(!identifiers.contains(identifier), qPrintable(identifier));
        identifiers.append(identifier);
    }
}

void Tst_SingleInstance::exitCodesSitInTheirOwnBand()
{
    for (const RelayExitCodeRow &row : relayExitCodeTable()) {
        if (row.status == RelayStatus::NotAttempted) {
            continue;
        }
        QVERIFY(row.code >= relayExitCodeBandFirst());
        QVERIFY(row.code <= relayExitCodeBandLast());
    }
    QCOMPARE(relayExitCode(RelayStatus::Delivered), relayExitCodeBandFirst());
    QCOMPARE(relayExitCode(RelayStatus::Rejected), 11);
    QCOMPARE(relayExitCode(RelayStatus::NoPrimary), 12);
    QCOMPARE(relayExitCode(RelayStatus::HandshakeTimeout), 13);
}

void Tst_SingleInstance::notAttemptedCarriesNoExitCode()
{
    // 「没有发生转发」不是一个可以被返回的结局（本进程就是首个实例）。
    // 它占着 0 ——而 0 必须落在转发区间**之外**，否则 `role == Primary`
    // 这条路径会带着一个「成功」的退出码退出。
    QCOMPARE(relayExitCode(RelayStatus::NotAttempted), 0);
    QVERIFY(relayExitCodeBandFirst() > cliReservedExitCodeLast());
}

void Tst_SingleInstance::cliContractUpperBoundIsPinned()
{
    // CLI-004 的规格原文把 0~4 用掉了（0 无差异 / 1 有差异 / 2 参数错误 /
    // 3 数据源打不开 / 4 内部错误）。**转发进程没有执行任何比较**，
    // 若沿用 0~4，`lqcompare a b && echo identical` 会在什么都没比的情况下
    // 报「无差异」。这条用例把那个上界钉住：CLI-004 落地时若改了它的取值，
    // 这里会红，提醒把两边重新对齐。
    QCOMPARE(cliReservedExitCodeLast(), 4);
}

void Tst_SingleInstance::statusTextsAreDistinctAndNonEmpty()
{
    QStringList texts;
    for (const RelayExitCodeRow &row : relayExitCodeTable()) {
        QVERIFY2(!row.text.isEmpty(), "每一行都要有说明文案");
        QVERIFY2(!texts.contains(row.text), qPrintable(row.text));
        texts.append(row.text);
        QVERIFY(!relayStatusText(row.status).isEmpty());
    }
}

// =============================================================================
// E 退出码表自检的反向验证
// =============================================================================

namespace {

/// 拿一份改过的表跑同一个自检——不这样测，自检就是**一条永远不会红的护栏**。
QVector<QString> selfCheckOf(QVector<RelayExitCodeRow> table)
{
    return validateRelayExitCodeTable(table);
}

bool mentionsAny(const QVector<QString> &problems, const QString &needle)
{
    for (const QString &problem : problems) {
        if (problem.contains(needle)) {
            return true;
        }
    }
    return false;
}

} // namespace

void Tst_SingleInstance::selfCheckAcceptsTheBuiltInTable()
{
    QCOMPARE(selfCheckOf(relayExitCodeTable()).size(), 0);
}

void Tst_SingleInstance::selfCheckReportsAMissingStatus()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table.removeLast(); // 丢掉「超时」那一行
    const QVector<QString> problems = selfCheckOf(table);
    QVERIFY(!problems.isEmpty());
    QVERIFY(mentionsAny(problems, QStringLiteral("handshake-timeout")));
}

void Tst_SingleInstance::selfCheckReportsAnExtraRow()
{
    // 有人新增了一种结局却忘了把它加进「已知结局」清单里：那一行会完全
    // 逃过所有约束（包括退出码区间），而它恰恰是最需要被看见的一行。
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table.append({RelayStatus::Delivered, 99, "another", QStringLiteral("另一次交付")});
    const QVector<QString> problems = selfCheckOf(table);
    QVERIFY(!problems.isEmpty());
    QVERIFY(mentionsAny(problems, QStringLiteral("行")));
    // 而且**必须**报得出来——不是「恰好也报了点别的」。
    QVERIFY(mentionsAny(problems, QStringLiteral("已知的结局")));
}

void Tst_SingleInstance::selfCheckReportsDuplicateIdentifiers()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table[2].identifier = table[1].identifier;
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("出现了两次")));
}

void Tst_SingleInstance::selfCheckReportsMissingIdentifiers()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table[1].identifier = "";
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("没有机器标识")));
}

void Tst_SingleInstance::selfCheckReportsDuplicateCodes()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table[3].code = table[1].code;
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("被两个结局共用")));
}

void Tst_SingleInstance::selfCheckReportsCodesInsideTheCliBand()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table[1].code = 1; // 与 CLI-004 的「有差异」撞车
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("CLI-004")));
    // 同时也要报「不在转发区间内」——两条检查互不替代。
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("转发区间")));
}

void Tst_SingleInstance::selfCheckReportsCodesOutsideTheBand()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table[1].code = relayExitCodeBandLast() + 1;
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("转发区间")));
}

void Tst_SingleInstance::selfCheckReportsASharedText()
{
    QVector<RelayExitCodeRow> table = relayExitCodeTable();
    table[4].text = table[1].text;
    QVERIFY(mentionsAny(selfCheckOf(table), QStringLiteral("被两个结局共用")));
}

// =============================================================================
// F 命令行开关
// =============================================================================

void Tst_SingleInstance::switchesHaveNamesAndDescriptions()
{
    for (const SingleInstanceSwitch &row : singleInstanceSwitches()) {
        QVERIFY2(!row.name.isEmpty(), "开关要有名字");
        QVERIFY2(!row.description.isEmpty(), "开关要有说明（--help 里直接用它）");
        QVERIFY2(row.decision != SingleInstanceSwitchDecision::Unset,
                 "表里的开关必须表达一个明确的三态取值");
    }
}

void Tst_SingleInstance::noSwitchUsesTheFallback()
{
    const SingleInstanceSwitchDecision decision = resolveSingleInstanceSwitch(
        singleInstanceSwitches(), QStringList(), SingleInstanceSwitchDecision::Enabled);
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(decision)),
             QStringLiteral("enabled"));
}

void Tst_SingleInstance::switchesEnableAndDisable()
{
    const QVector<SingleInstanceSwitch> &table = singleInstanceSwitches();
    const SingleInstanceSwitchDecision enabled = resolveSingleInstanceSwitch(
        table, QStringList{QStringLiteral("single-instance")},
        SingleInstanceSwitchDecision::Disabled);
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(enabled)),
             QStringLiteral("enabled"));

    const SingleInstanceSwitchDecision disabled = resolveSingleInstanceSwitch(
        table, QStringList{QStringLiteral("no-single-instance")},
        SingleInstanceSwitchDecision::Enabled);
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(disabled)),
             QStringLiteral("disabled"));
}

void Tst_SingleInstance::theLastTableRowWins()
{
    // 两个都写了时按**表顺序**定胜负（表尾的那个赢）。用表顺序而不是命令行
    // 顺序：`QCommandLineParser` 不保留顺序，为了这件事再自己解析一遍命令行
    // 就会形成第二份解析实现。
    const SingleInstanceSwitchDecision decision = resolveSingleInstanceSwitch(
        singleInstanceSwitches(),
        QStringList{QStringLiteral("single-instance"), QStringLiteral("no-single-instance")},
        SingleInstanceSwitchDecision::Enabled);
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(decision)),
             QStringLiteral("disabled"));
}

void Tst_SingleInstance::unknownSwitchNamesAreIgnored()
{
    const SingleInstanceSwitchDecision decision = resolveSingleInstanceSwitch(
        singleInstanceSwitches(), QStringList{QStringLiteral("some-other-flag")},
        SingleInstanceSwitchDecision::Unset);
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(decision)),
             QStringLiteral("unset"));
}

void Tst_SingleInstance::resolutionWorksOnASynthesizedTable()
{
    // 表当参数：拿一份自己造的表跑同一个判定，证明这个函数真的在读表，
    // 而不是把结论写死在自己的常量里。
    const QVector<SingleInstanceSwitch> table = {
        {QStringLiteral("on"), SingleInstanceSwitchDecision::Enabled, QStringLiteral("打开")},
        {QStringLiteral("off"), SingleInstanceSwitchDecision::Disabled, QStringLiteral("关闭")},
    };
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(
                 resolveSingleInstanceSwitch(table, QStringList{QStringLiteral("off")},
                                             SingleInstanceSwitchDecision::Unset))),
             QStringLiteral("disabled"));
    // 表里带 `Unset` 的行不参与判定。
    const QVector<SingleInstanceSwitch> withUnset = {
        {QStringLiteral("maybe"), SingleInstanceSwitchDecision::Unset, QStringLiteral("不确定")},
    };
    QCOMPARE(QString::fromLatin1(singleInstanceSwitchDecisionIdentifier(
                 resolveSingleInstanceSwitch(withUnset, QStringList{QStringLiteral("maybe")},
                                             SingleInstanceSwitchDecision::Enabled))),
             QStringLiteral("enabled"));
}

// =============================================================================
// G 置前策略
// =============================================================================

void Tst_SingleInstance::alwaysPolicyAlwaysRaises()
{
    // 「总是」连「刚刚有输入」也不管：双击文件关联的人要的就是窗口立刻出来。
    QVERIFY(shouldActivateWindow(ActivationPolicy::Always, 0, 2500));
    QVERIFY(shouldActivateWindow(ActivationPolicy::Always, -1, 2500));
}

void Tst_SingleInstance::neverPolicyNeverRaises()
{
    QVERIFY(!shouldActivateWindow(ActivationPolicy::Never, 60000, 2500));
    QVERIFY(!shouldActivateWindow(ActivationPolicy::Never, -1, 2500));
}

void Tst_SingleInstance::quietInputAllowsRaising()
{
    QVERIFY(shouldActivateWindow(ActivationPolicy::UnlessTypingRecently, 2500, 2500));
    QVERIFY(shouldActivateWindow(ActivationPolicy::UnlessTypingRecently, 90000, 2500));
}

void Tst_SingleInstance::recentInputSuppressesRaising()
{
    // 括号里那句「不抢焦点导致用户中断输入」就是这一条。
    QVERIFY(!shouldActivateWindow(ActivationPolicy::UnlessTypingRecently, 0, 2500));
    QVERIFY(!shouldActivateWindow(ActivationPolicy::UnlessTypingRecently, 2499, 2500));
}

void Tst_SingleInstance::unknownInputAgeRaises()
{
    // 负数表示「不知道」（还没接上输入来源）。不知道时置前：这条走的正是
    // 主路径，把他要的窗口藏在别的窗口后面比抢一次焦点更糟。
    QVERIFY(shouldActivateWindow(ActivationPolicy::UnlessTypingRecently, -1, 2500));
    QVERIFY(defaultActivationQuietWindowMs() > 0);
}

void Tst_SingleInstance::policyIdentifiersAndTextsAreDistinct()
{
    const QVector<ActivationPolicy> policies = {ActivationPolicy::Always,
                                                ActivationPolicy::UnlessTypingRecently,
                                                ActivationPolicy::Never};
    QStringList identifiers;
    QStringList texts;
    for (ActivationPolicy policy : policies) {
        const QString identifier = QString::fromLatin1(activationPolicyIdentifier(policy));
        const QString text = activationPolicyText(policy);
        QVERIFY2(!identifier.isEmpty(), "每种策略都要有机器标识");
        QVERIFY2(!text.isEmpty(), "每种策略都要有说明");
        QVERIFY2(!identifiers.contains(identifier), qPrintable(identifier));
        QVERIFY2(!texts.contains(text), qPrintable(text));
        identifiers.append(identifier);
        texts.append(text);
    }
}

// =============================================================================
// H 角色与启动结论
// =============================================================================

void Tst_SingleInstance::roleIdentifiersAreDistinctAndNonEmpty()
{
    const QVector<InstanceRole> roles = {InstanceRole::Disabled, InstanceRole::Primary,
                                         InstanceRole::Secondary, InstanceRole::Degraded};
    QStringList identifiers;
    QStringList texts;
    for (InstanceRole role : roles) {
        const QString identifier = QString::fromLatin1(instanceRoleIdentifier(role));
        const QString text = instanceRoleText(role);
        QVERIFY(!identifier.isEmpty());
        QVERIFY(!text.isEmpty());
        QVERIFY2(!identifiers.contains(identifier), qPrintable(identifier));
        QVERIFY2(!texts.contains(text), qPrintable(text));
        identifiers.append(identifier);
        texts.append(text);
    }
}

void Tst_SingleInstance::fallbackIdentifiersAreDistinct()
{
    QCOMPARE(QString::fromLatin1(relayFallbackIdentifier(RelayFallback::StartNewInstance)),
             QStringLiteral("start-new-instance"));
    QCOMPARE(QString::fromLatin1(relayFallbackIdentifier(RelayFallback::ExitWithRelayCode)),
             QStringLiteral("exit-with-relay-code"));
}

void Tst_SingleInstance::aDisabledReportCarriesNoRelay()
{
    InstanceStartReport report;
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("disabled"));
    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)),
             QStringLiteral("not-attempted"));
    QVERIFY(!report.shouldExit());
}

void Tst_SingleInstance::theReportExitCodeFollowsTheRelayStatus()
{
    const QVector<RelayStatus> statuses = {RelayStatus::NotAttempted, RelayStatus::Delivered,
                                           RelayStatus::Rejected, RelayStatus::NoPrimary,
                                           RelayStatus::HandshakeTimeout};
    for (RelayStatus status : statuses) {
        InstanceStartReport report;
        report.relay = status;
        QCOMPARE(report.exitCode(), relayExitCode(status));
        // 只有「交给别人了」才该退出；其余角色由调用方决定继续启动。
        QVERIFY(!report.shouldExit());
    }
    InstanceStartReport secondary;
    secondary.role = InstanceRole::Secondary;
    secondary.relay = RelayStatus::Delivered;
    QVERIFY(secondary.shouldExit());
    QCOMPARE(secondary.exitCode(), relayExitCodeBandFirst());
}

// =============================================================================
// I 真的第二个进程：首个实例与转交
// =============================================================================

void Tst_SingleInstance::aLoneProcessBecomesPrimary()
{
    SingleInstanceGuard guard;
    const InstanceStartReport report = guard.start(m_seed);
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("primary"));
    QVERIFY(guard.isPrimary());
    QVERIFY(!report.shouldExit());
    QVERIFY2(!report.detail.isEmpty(), "成功的路径也要有一句能进日志的话");
}

void Tst_SingleInstance::primaryTakesAnIdentifierAndListensOnAnEndpoint()
{
    SingleInstanceGuard guard;
    const InstanceStartReport report = guard.start(m_seed);

    QVERIFY(guard.isListening());
    QVERIFY(!report.endpointName.isEmpty());
    QVERIFY(!report.sharedMemoryKey.isEmpty());
    QVERIFY2(report.sharedMemoryKey.size() <= instanceSharedMemoryKeyLimit(),
             qPrintable(report.sharedMemoryKey));
    QCOMPARE(report.endpointName, guard.endpointName());
    QCOMPARE(report.sharedMemoryKey, guard.sharedMemoryKey());

    // 「首个实例已经把键占住了」——用底层存储直接验证，而不是在本进程里
    // 再起一个守卫去转交：**同进程内的转交是测不出来的**，发送的一端会
    // 阻塞等待应答，而服务器要收下连接必须有事件循环，两者在同一个线程上
    // 必然互相等到超时。真正的转交路径由下面那批「两个进程」的用例覆盖。
    QLockFile probe(guard.identifierLockPath());
    probe.setStaleLockTime(0);
    QVERIFY2(!probe.tryLock(0), "首个实例必须持续持有进程锁");
    if (guard.usesSharedMemoryIdentifier()) {
        QSharedMemory sharedProbe(guard.sharedMemoryKey());
        QVERIFY2(!sharedProbe.create(16 * 1024), "共享内存键也应被占住");
        QCOMPARE(sharedProbe.error(), QSharedMemory::AlreadyExists);
    } else {
        QVERIFY2(report.detail.contains(QStringLiteral("共享内存不可用")), qPrintable(report.detail));
    }
}

void Tst_SingleInstance::releasingTheIdentifierLetsTheNextProcessBecomePrimary()
{
    // 正常退出必须放掉标识，否则「关掉程序再打开」会得到一个降级的实例，
    // 而用户什么都没做错。QSharedMemory 的析构会 detach，只有当没有任何
    // 进程还挂着那段内存时 Qt 才真正删掉它（实测于 Qt 5.15.2）。
    {
        SingleInstanceGuard guard;
        QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(guard.start(m_seed).role)),
                 QStringLiteral("primary"));
    }
    SingleInstanceGuard next;
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(next.start(m_seed).role)),
             QStringLiteral("primary"));
}

void Tst_SingleInstance::aSecondProcessIsToldThatItsArgumentsWereDelivered()
{
    const QString output = outputPath(QStringLiteral("delivered"));
    ChildProcess primary(QStringLiteral("hold-primary"), m_seed, QStringList(), QString(), output);
    QVERIFY2(primary.waitForReady(), qPrintable(primary.childOutput()));

    SingleInstanceGuard second;
    second.setRelayTimeoutMs(2000);
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/tmp");
    request.arguments = QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt")};
    second.setRelayRequest(request);

    const InstanceStartReport report = second.start(m_seed);
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("secondary"));
    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("delivered"));
    QVERIFY(report.shouldExit());
    QCOMPARE(report.exitCode(), relayExitCodeBandFirst());
    QCOMPARE(report.forwardedArgumentCount, 2);
}

void Tst_SingleInstance::theFirstProcessReceivesTheArgumentsAndTheWorkingDirectory()
{
    const QString output = outputPath(QStringLiteral("received"));
    ChildProcess primary(QStringLiteral("hold-primary"), m_seed, QStringList(), QString(), output);
    QVERIFY2(primary.waitForReady(), qPrintable(primary.childOutput()));

    SingleInstanceGuard second;
    second.setRelayTimeoutMs(2000);
    RelayRequest request;
    request.workingDirectory = QStringLiteral("/Users/loren/桌面");
    request.arguments = QStringList{QStringLiteral("报告 v2.txt"), QStringLiteral("图像.png")};
    second.setRelayRequest(request);
    const InstanceStartReport report = second.start(m_seed);
    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("delivered"));

    QVERIFY2(waitForOutputLine(output, QStringLiteral("RECEIVED-COUNT=2")),
             qPrintable(readOutput(output)));
    const QString text = readOutput(output);
    // 逐字比对参数：中文与空格都在这一条里。
    QCOMPARE(valuesOfLine(text, QStringLiteral("RECEIVED-ARG")),
             (QStringList{QStringLiteral("报告 v2.txt"), QStringLiteral("图像.png")}));
    // 工作目录必须一起过来：相对路径要按**第二个实例**的目录解析。
    QCOMPARE(valueOfLine(text, QStringLiteral("RECEIVED-CWD")), QStringLiteral("/Users/loren/桌面"));
}

void Tst_SingleInstance::theFirstProcessIsAskedToRaiseItsWindow()
{
    const QString output = outputPath(QStringLiteral("raise"));
    ChildProcess primary(QStringLiteral("hold-primary"), m_seed, QStringList(), QString(), output);
    QVERIFY2(primary.waitForReady(), qPrintable(primary.childOutput()));

    SingleInstanceGuard second;
    second.setRelayTimeoutMs(2000);
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a.txt")};
    second.setRelayRequest(request);
    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(second.start(m_seed).relay)),
             QStringLiteral("delivered"));

    // 默认策略是「总是置前」：这条走的是双击文件关联的主路径。
    QVERIFY2(waitForOutputLine(output, QStringLiteral("RAISE=1")), qPrintable(readOutput(output)));
    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("RAISE-COUNT")), QStringLiteral("1"));
}

void Tst_SingleInstance::aSecondProcessExitsWithTheDeliveredCode()
{
    // 这一条验证的是「**退出码**反映转发结果」——断言的是**另一个进程**的
    // 退出码，不是某个函数的返回值。
    SingleInstanceGuard primary;
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(primary.start(m_seed).role)),
             QStringLiteral("primary"));

    const QString output = outputPath(QStringLiteral("relay-exit-ok"));
    ChildProcess second(QStringLiteral("relay-and-exit"), m_seed,
                        QStringList{QStringLiteral("x.txt")}, QStringLiteral("/tmp"), output);
    QVERIFY2(second.waitForFinished(), qPrintable(second.childOutput()));
    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("ROLE")), QStringLiteral("secondary"));
    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("RELAY")), QStringLiteral("delivered"));
    QCOMPARE(second.exitCode(), relayExitCodeBandFirst());
}

void Tst_SingleInstance::theReplyCarriesTheArgumentCount()
{
    // 应答里带着首个实例解析出的参数个数。少了这个字段，请求方只能知道
    // 「它接受了」，发现不了「字节在传输里被截断了」。
    const QString output = outputPath(QStringLiteral("count"));
    ChildProcess primary(QStringLiteral("hold-primary"), m_seed, QStringList(), QString(), output);
    QVERIFY2(primary.waitForReady(), qPrintable(primary.childOutput()));

    SingleInstanceGuard second;
    second.setRelayTimeoutMs(2000);
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    second.setRelayRequest(request);
    const InstanceStartReport report = second.start(m_seed);

    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("delivered"));
    // 首个实例侧的计数（写进文件的）与我们发出去的一致。
    QVERIFY(waitForOutputLine(output, QStringLiteral("RECEIVED-COUNT=3")));
    QCOMPARE(valuesOfLine(readOutput(output), QStringLiteral("RECEIVED-ARG")).size(), 3);
}

// =============================================================================
// J 真的第二个进程：失败与降级
// =============================================================================

void Tst_SingleInstance::garbageFromAClientIsRejectedAndReported()
{
    // 接收端的**拒绝**路径：连上来的是别人（不是我们自己的第二个实例），
    // 发的东西不合协议。宽松的实现会在这里为一段「声明出来的巨大载荷」
    // 分配内存，或者干脆一直等下去。这一段必须被认出来、回一条拒绝、
    // 并且把原因报给日志。
    SingleInstanceGuard primary;
    QStringList problems;
    QObject::connect(&primary, &SingleInstanceGuard::relayProblem,
                     [&problems](const QString &problem) { problems.append(problem); });
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(primary.start(m_seed).role)),
             QStringLiteral("primary"));

    const QString output = outputPath(QStringLiteral("garbage"));
    ChildProcess client(QStringLiteral("send-garbage"), m_seed, QStringList(), QString(), output);
    // 注意：这里的等待必须**跑事件循环**——父进程自己就是首个实例，
    // 子进程要靠它回话。直接 waitForFinished() 会把事件循环堵死。
    QVERIFY2(client.waitForFinished(), qPrintable(client.childOutput()));

    QVERIFY2(waitForOutputLine(output, QStringLiteral("REPLY-ACCEPTED=")),
             qPrintable(readOutput(output)));
    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("REPLY-ACCEPTED")),
             QStringLiteral("0"));
    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("REPLY-DETAIL")),
             QStringLiteral("缓冲区开头不是本协议的帧（魔数不匹配）"));

    // 首个实例自己也要把这件事报出来，否则日志里只会看到「某个连接不见了」。
    QVERIFY2(!problems.isEmpty(), "接收端必须把拒绝的原因报出来");
    QVERIFY2(problems.first().contains(QStringLiteral("魔数")), qPrintable(problems.first()));
}

void Tst_SingleInstance::defaultFallbackStartsANewInstanceInsteadOfFailing()
{
    // 默认策略：交不出去就照常启动，**绝不让用户看到失败**。
    // 用一个「占着标识却从不监听」的进程制造这个局面。
    ChildProcess holder(QStringLiteral("hold-identifier-only"), m_seed);
    QVERIFY2(holder.waitForReady(), qPrintable(holder.childOutput()));

    SingleInstanceGuard guard;
    guard.setRelayTimeoutMs(500);
    const InstanceStartReport report = guard.start(m_seed);

    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("no-primary"));
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("degraded"));
    QVERIFY2(!report.shouldExit(), "默认策略下绝不退出");
    QVERIFY2(report.detail.contains(QStringLiteral("照常启动")), qPrintable(report.detail));
    QVERIFY2(!guard.isPrimary(), "标识还被人占着，我们不是首个实例");
}

void Tst_SingleInstance::exitWithRelayCodeTurnsADeliveryFailureIntoAnExitCode()
{
    // 无界面 / 脚本场景：没有窗口可开，「悄悄多一个进程」比直接报错更糟。
    ChildProcess holder(QStringLiteral("hold-identifier-only"), m_seed);
    QVERIFY2(holder.waitForReady(), qPrintable(holder.childOutput()));

    const QString output = outputPath(QStringLiteral("relay-exit-fail"));
    ChildProcess second(QStringLiteral("relay-and-exit"), m_seed, QStringList(), QString(), output);
    QVERIFY2(second.waitForFinished(), qPrintable(second.childOutput()));

    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("RELAY")), QStringLiteral("no-primary"));
    QCOMPARE(second.exitCode(), relayExitCode(RelayStatus::NoPrimary));
}

void Tst_SingleInstance::aSilentFirstProcessLeadsToATimeout()
{
    // 「首个实例卡住」：它接了连接，但一直不回话。超时之后必须降级启动，
    // 并把这件事记下来——让用户看到「程序打不开」是这里最不能接受的结果。
    ChildProcess silent(QStringLiteral("silent-server"), m_seed);
    QVERIFY2(silent.waitForReady(), qPrintable(silent.childOutput()));

    SingleInstanceGuard guard;
    guard.setRelayTimeoutMs(300);
    QElapsedTimer timer;
    timer.start();
    const InstanceStartReport report = guard.start(m_seed);
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)),
             QStringLiteral("handshake-timeout"));
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("degraded"));
    QCOMPARE(report.exitCode(), relayExitCode(RelayStatus::HandshakeTimeout));

    // 超时保护必须**真的生效**：不能等到天荒地老，也不能几乎立刻返回
    // （那说明根本没等）。给它一个宽区间。
    QVERIFY2(elapsed >= 200, qPrintable(QString::number(elapsed)));
    QVERIFY2(elapsed < 10000, qPrintable(QString::number(elapsed)));

    // 原因要能进日志：光有一个「降级了」看不出是为什么。
    QVERIFY2(report.detail.contains(QStringLiteral("300")), qPrintable(report.detail));
}

void Tst_SingleInstance::aRejectingFirstProcessIsReportedAsRejected()
{
    ChildProcess rejecting(QStringLiteral("rejecting-server"), m_seed);
    QVERIFY2(rejecting.waitForReady(), qPrintable(rejecting.childOutput()));

    SingleInstanceGuard guard;
    guard.setRelayTimeoutMs(1000);
    const InstanceStartReport report = guard.start(m_seed);

    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("rejected"));
    QCOMPARE(report.exitCode(), relayExitCode(RelayStatus::Rejected));
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("degraded"));
    QVERIFY2(report.detail.contains(QStringLiteral("测试用的拒绝")), qPrintable(report.detail));
}

void Tst_SingleInstance::disablingTheMechanismTakesNoIdentifier()
{
    // 关掉时**不占任何标识**。若「关了但仍然占着锁」，后续每一个实例都会
    // 降级——这个选项就成了「更坏的单实例」。
    SingleInstanceGuard disabled;
    disabled.setEnabled(false);
    const InstanceStartReport report = disabled.start(m_seed);

    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("disabled"));
    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)),
             QStringLiteral("not-attempted"));
    QVERIFY(!disabled.isPrimary());
    QVERIFY(!disabled.isListening());
    QVERIFY(!report.shouldExit());

    // 证明它没占标识：另一个守卫用同一个种子能立刻成为首个实例。
    SingleInstanceGuard enabled;
    enabled.setRelayTimeoutMs(500);
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(enabled.start(m_seed).role)),
             QStringLiteral("primary"));
}

void Tst_SingleInstance::twoDisabledGuardsDoNotCollide()
{
    SingleInstanceGuard first;
    first.setEnabled(false);
    SingleInstanceGuard second;
    second.setEnabled(false);

    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(first.start(m_seed).role)),
             QStringLiteral("disabled"));
    // 两者用的是同一个种子，但都没有占标识，因此第二个同样是「关闭」而不是
    // 被第一个挡住。
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(second.start(m_seed).role)),
             QStringLiteral("disabled"));
}

void Tst_SingleInstance::aFirstProcessThatMiscountsTheArgumentsIsRejected()
{
    // 首个实例回了「接受」，但报出来的参数个数与我们发出去的不是一回事。
    // 这必须被判成**没有交付**：否则用户会在首个实例里看到一组不是他点的
    // 参数，而这边还以为一切正常、静默退出。
    ChildProcess lying(QStringLiteral("miscounting-server"), m_seed);
    QVERIFY2(lying.waitForReady(), qPrintable(lying.childOutput()));

    SingleInstanceGuard guard;
    guard.setRelayTimeoutMs(1000);
    RelayRequest request;
    request.arguments = QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt")};
    guard.setRelayRequest(request);
    const InstanceStartReport report = guard.start(m_seed);

    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("rejected"));
    QVERIFY2(report.detail.contains(QStringLiteral("99")), qPrintable(report.detail));
    QVERIFY2(report.detail.contains(QStringLiteral("2")), qPrintable(report.detail));
    QVERIFY2(!report.shouldExit(), "默认策略下不退出");
}

void Tst_SingleInstance::aClientThatNeverSendsIsDroppedByTheDeadline()
{
    // 接收端的读截止时间：连上来却一个字节都不发的连接必须被收掉，
    // 并且**把原因报出来**——否则现象只是「连接数慢慢涨」，没人会想到是它。
    SingleInstanceGuard primary;
    primary.setRelayTimeoutMs(300);
    QStringList problems;
    QObject::connect(&primary, &SingleInstanceGuard::relayProblem,
                     [&problems](const QString &problem) { problems.append(problem); });
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(primary.start(m_seed).role)),
             QStringLiteral("primary"));

    const QString output = outputPath(QStringLiteral("connect-only"));
    ChildProcess client(QStringLiteral("connect-only"), m_seed, QStringList(), QString(), output);
    QVERIFY2(client.waitForFinished(), qPrintable(client.childOutput()));

    const QString text = readOutput(output);
    QVERIFY2(text.contains(QStringLiteral("REPLY-ACCEPTED=0")), qPrintable(text));
    QCOMPARE(valueOfLine(text, QStringLiteral("REPLY-ACCEPTED")), QStringLiteral("0"));
    QVERIFY2(valueOfLine(text, QStringLiteral("REPLY-DETAIL"))
                 .contains(QStringLiteral("没有发来完整的一帧")),
             qPrintable(text));
    QVERIFY2(!problems.isEmpty(), "接收端要把截止时间的原因报出来");
}

// =============================================================================
// K 崩溃遗留标识的回收
// =============================================================================

void Tst_SingleInstance::aCrashedProcessLeavesItsIdentifierBehind()
{
    // _exit 留下锁文件；Windows 会自动释放共享内存，Unix 则可能留下段。
    // 回归断言面向可观察的实例身份，不把 Unix 的段保留行为强加给 Windows。
    ChildProcess crashed(QStringLiteral("crash-primary"), m_seed);
    QVERIFY2(crashed.waitForFinished(15000), qPrintable(crashed.childOutput()));
    QCOMPARE(crashed.exitCode(), 0);
    const QString endpoint = instanceEndpointName(m_seed, defaultInstanceUserScope());
    QVERIFY(QFile::exists(QDir(QDir::tempPath()).filePath(endpoint + QStringLiteral(".lock"))));
    SingleInstanceGuard recovered;
    const auto report = recovered.start(m_seed);
    QVERIFY2(recovered.isPrimary(), qPrintable(report.detail));
    QVERIFY(recovered.isListening());
}

void Tst_SingleInstance::theStaleIdentifierIsRecoveredByTheNextProcess()
{
    ChildProcess crashed(QStringLiteral("crash-primary"), m_seed);
    QVERIFY2(crashed.waitForFinished(15000), qPrintable(crashed.childOutput()));

    // 没有这条恢复路径时，这个用户的单实例机制会**永久失效**（标识永远
    // 被一个已经不在的进程占着），而他能做的只有重启机器。
    SingleInstanceGuard next;
    next.setRelayTimeoutMs(500);
    const InstanceStartReport report = next.start(m_seed);
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("primary"));
    QVERIFY(next.isPrimary());
    QVERIFY(next.isListening());
    QVERIFY2(!report.detail.isEmpty(), "恢复结果必须能写入日志");
}

void Tst_SingleInstance::aRecoveredFirstProcessCanStillServe()
{
    // 接手之后必须真的能收参数，而不只是「把标识抢过来了」。
    ChildProcess crashed(QStringLiteral("crash-primary"), m_seed);
    QVERIFY2(crashed.waitForFinished(15000), qPrintable(crashed.childOutput()));

    SingleInstanceGuard recovered;
    recovered.setRelayTimeoutMs(500);
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(recovered.start(m_seed).role)),
             QStringLiteral("primary"));

    const QString output = outputPath(QStringLiteral("after-recovery"));
    ChildProcess second(QStringLiteral("relay-and-exit"), m_seed,
                        QStringList{QStringLiteral("after.txt")}, QStringLiteral("/tmp"), output);
    QVERIFY2(second.waitForFinished(), qPrintable(second.childOutput()));
    QCOMPARE(valueOfLine(readOutput(output), QStringLiteral("RELAY")), QStringLiteral("delivered"));
    QCOMPARE(second.exitCode(), relayExitCodeBandFirst());
}

void Tst_SingleInstance::recoveryNeverStealsALivingIdentifier()
{
    // 即使首实例未监听也不能抢走它仍持有的进程锁。超时后只允许降级。
    ChildProcess holder(QStringLiteral("hold-identifier-only"), m_seed);
    QVERIFY2(holder.waitForReady(), qPrintable(holder.childOutput()));

    SingleInstanceGuard guard;
    guard.setRelayTimeoutMs(500);
    const InstanceStartReport report = guard.start(m_seed);

    QVERIFY2(!guard.isPrimary(), "标识还在别人手里，不能抢");
    QCOMPARE(QString::fromLatin1(relayStatusIdentifier(report.relay)), QStringLiteral("no-primary"));
    QCOMPARE(QString::fromLatin1(instanceRoleIdentifier(report.role)), QStringLiteral("degraded"));

    // 而且那个进程仍然占着标识——降级没有把它的锁删掉。
    QLockFile probe(guard.identifierLockPath());
    probe.setStaleLockTime(0);
    QVERIFY2(!probe.tryLock(0), "活着的实例的标识必须原样保留");
}

void Tst_SingleInstance::repeatedStartKeepsThePrimaryAndListener()
{
    SingleInstanceGuard guard;
    const auto first = guard.start(m_seed);
    QVERIFY2(guard.isPrimary(), qPrintable(first.detail));
    const auto again = guard.start(m_seed);
    QCOMPARE(again.role, InstanceRole::Primary);
    QCOMPARE(again.endpointName, first.endpointName);
    QVERIFY(guard.isListening());
    ChildProcess client(QStringLiteral("relay-and-exit"), m_seed);
    QVERIFY2(client.waitForFinished(), qPrintable(client.childOutput()));
    QCOMPARE(client.exitCode(), relayExitCode(RelayStatus::Delivered));
}

void Tst_SingleInstance::restartingWithAnotherSeedReleasesTheOldIdentifier()
{
    SingleInstanceGuard guard;
    QCOMPARE(guard.start(m_seed).role, InstanceRole::Primary);
    const QString oldLock = guard.identifierLockPath();
    QCOMPARE(guard.start(makeSeed()).role, InstanceRole::Primary);
    QVERIFY(!QFile::exists(oldLock));
    SingleInstanceGuard previous;
    QCOMPARE(previous.start(m_seed).role, InstanceRole::Primary);
    QVERIFY(previous.isListening());
    QVERIFY(guard.isListening());
}

void Tst_SingleInstance::disablingAPrimaryReleasesAllResources()
{
    SingleInstanceGuard guard;
    QCOMPARE(guard.start(m_seed).role, InstanceRole::Primary);
    const QString oldLock = guard.identifierLockPath();
    guard.setEnabled(false);
    QCOMPARE(guard.start(m_seed).role, InstanceRole::Disabled);
    QVERIFY(!guard.isPrimary());
    QVERIFY(!guard.isListening());
    QVERIFY(!QFile::exists(oldLock));
    SingleInstanceGuard next;
    QCOMPARE(next.start(m_seed).role, InstanceRole::Primary);
}

void Tst_SingleInstance::manyNormalLifetimesDoNotExhaustIdentifiers()
{
    for (int i = 0; i < 128; ++i) {
        QString lockPath;
        {
            SingleInstanceGuard guard;
            const auto report = guard.start(makeSeed());
            QVERIFY2(guard.isPrimary(), qPrintable(report.detail));
            QVERIFY(guard.isListening());
            lockPath = guard.identifierLockPath();
        }
        QVERIFY2(!QFile::exists(lockPath), qPrintable(lockPath));
    }
}

void Tst_SingleInstance::normalChildShutdownReleasesItsIdentifier()
{
    ChildProcess primary(QStringLiteral("hold-primary"), m_seed);
    QVERIFY2(primary.waitForReady(), qPrintable(primary.childOutput()));
    const QString lock = QDir(QDir::tempPath()).filePath(
        instanceEndpointName(m_seed, defaultInstanceUserScope()) + QStringLiteral(".lock"));
    QVERIFY(QFile::exists(lock));
    primary.stop();
    QCOMPARE(primary.process()->exitStatus(), QProcess::NormalExit);
    QCOMPARE(primary.exitCode(), 0);
    QVERIFY(!QFile::exists(lock));
    SingleInstanceGuard next;
    QCOMPARE(next.start(m_seed).role, InstanceRole::Primary);
}

void Tst_SingleInstance::manyCrashRecoveriesKeepServing()
{
    for (int i = 0; i < 12; ++i) {
        ChildProcess crashed(QStringLiteral("crash-primary"), m_seed);
        QVERIFY2(crashed.waitForFinished(), qPrintable(crashed.childOutput()));
        QCOMPARE(crashed.exitCode(), 0);
        SingleInstanceGuard recovered;
        const auto report = recovered.start(m_seed);
        QVERIFY2(recovered.isPrimary(), qPrintable(report.detail));
        ChildProcess client(QStringLiteral("relay-and-exit"), m_seed);
        QVERIFY2(client.waitForFinished(), qPrintable(client.childOutput()));
        QCOMPARE(client.exitCode(), relayExitCode(RelayStatus::Delivered));
    }
}

void Tst_SingleInstance::concurrentStartsElectOnlyOnePrimary()
{
    std::vector<std::unique_ptr<ChildProcess>> children;
    QStringList outputs;
    for (int i = 0; i < 4; ++i) {
        outputs.append(outputPath(QStringLiteral("race-%1").arg(i)));
        children.push_back(std::make_unique<ChildProcess>(QStringLiteral("compete-primary"),
                            m_seed, QStringList(), QString(), outputs.last()));
    }
    int primaries = 0;
    for (size_t i = 0; i < children.size(); ++i) {
        QVERIFY2(children[i]->waitForReady(), qPrintable(children[i]->childOutput()));
        const QString role = valueOfLine(readOutput(outputs.at(int(i))), QStringLiteral("ROLE"));
        if (role == QLatin1String("primary")) ++primaries;
        else {
            QCOMPARE(role, QStringLiteral("secondary"));
            QVERIFY(children[i]->waitForFinished());
            QCOMPARE(children[i]->exitCode(), relayExitCode(RelayStatus::Delivered));
        }
    }
    QCOMPARE(primaries, 1);
}

void Tst_SingleInstance::aPrimaryStillStartingGetsTimeToListen()
{
    ChildProcess delayed(QStringLiteral("delayed-server"), m_seed);
    QVERIFY2(delayed.waitForReady(), qPrintable(delayed.childOutput()));
    SingleInstanceGuard second;
    second.setRelayTimeoutMs(600);
    const auto report = second.start(m_seed);
    QCOMPARE(report.role, InstanceRole::Secondary);
    QVERIFY2(report.relay == RelayStatus::Delivered, qPrintable(report.detail));
}

void Tst_SingleInstance::realRelayPreservesNewlinesAndEmptyArguments()
{
    SingleInstanceGuard primary;
    RelayRequest received;
    int count = 0;
    QObject::connect(&primary, &SingleInstanceGuard::relayReceived,
                     [&received, &count](const RelayRequest &request) { received = request; ++count; });
    QCOMPARE(primary.start(m_seed).role, InstanceRole::Primary);
    const QStringList arguments = {QString(), QStringLiteral("line\nbreak.txt"),
                                   QStringLiteral(" leading\t中文 trailing ")};
    const QString cwd = QStringLiteral("/tmp/目录 with space");
    ChildProcess client(QStringLiteral("relay-and-exit"), m_seed, arguments, cwd);
    QVERIFY2(client.waitForFinished(), qPrintable(client.childOutput()));
    QCOMPARE(client.exitCode(), relayExitCode(RelayStatus::Delivered));
    QCOMPARE(count, 1);
    QCOMPARE(received.arguments, arguments);
    QCOMPARE(received.workingDirectory, cwd);
}

// =============================================================================
// L 源码级护栏
// =============================================================================

namespace {

///
/// \brief 读源码并判断「有没有界面依赖」。
///
/// 抽成接受**源码文本**的函数，这样能拿一份故意写坏的样本跑同一个判定，
/// 证明它不是恒真（本仓有一条铁律：一条永远不会红的护栏比没有护栏更糟）。
///
QStringList guiIncludesIn(const QString &source)
{
    QStringList hits;
    const QStringList forbidden = {QStringLiteral("QtWidgets"), QStringLiteral("QtGui"),
                                   QStringLiteral("QWidget"),    QStringLiteral("QIcon"),
                                   QStringLiteral("QPixmap"),    QStringLiteral("QApplication")};
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.contains(QStringLiteral("#include"))) {
            continue;
        }
        for (const QString &needle : forbidden) {
            if (line.contains(needle)) {
                hits.append(line.trimmed());
            }
        }
    }
    return hits;
}

QStringList platformIfdefsIn(const QString &source)
{
    QStringList hits;
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("#ifdef Q_OS_"))
            || trimmed.startsWith(QStringLiteral("#if defined(Q_OS_"))
            || trimmed.contains(QStringLiteral("defined(Q_OS_WIN)"))) {
            hits.append(trimmed);
        }
    }
    return hits;
}

///
/// \brief 去掉注释之后的源码。
///
/// **护栏会扫到注释**——本仓踩过这个坑：在注释里写下一个完整的资源路径
/// 字面量，`check_icons.py` 立刻把它当成一次真实的引用。这里同理：
/// `instanceprotocol.cpp` 的注释里**必须**能写「标识不能用 qHash() 算」
/// 这句话，而那不该被判成违规。因此判定前先去掉注释，与
/// `check_winapi.py` 的做法一致。
///
QString withoutComments(const QString &source)
{
    QString result;
    result.reserve(source.size());
    bool inLineComment = false;
    bool inBlockComment = false;
    for (int i = 0; i < source.size(); ++i) {
        const QChar character = source.at(i);
        const QChar next = (i + 1 < source.size()) ? source.at(i + 1) : QChar();
        if (inLineComment) {
            if (character == QLatin1Char('\n')) {
                inLineComment = false;
                result.append(character);
            }
            continue;
        }
        if (inBlockComment) {
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (character == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLineComment = true;
            ++i;
            continue;
        }
        if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlockComment = true;
            ++i;
            continue;
        }
        result.append(character);
    }
    return result;
}

} // namespace

void Tst_SingleInstance::theModuleIncludesNoGuiHeaders()
{
    const QStringList files = {QStringLiteral("/Services/Platform/instanceprotocol.h"),
                               QStringLiteral("/Services/Platform/instanceprotocol.cpp"),
                               QStringLiteral("/Services/Platform/singleinstance.h"),
                               QStringLiteral("/Services/Platform/singleinstance.cpp")};
    for (const QString &file : files) {
        const QString source = readSourceFile(file);
        QVERIFY2(!source.isEmpty(), qPrintable(file));
        const QStringList hits = guiIncludesIn(source);
        QVERIFY2(hits.isEmpty(), qPrintable(file + QStringLiteral(": ") + hits.join(QStringLiteral(" | "))));
    }
}

void Tst_SingleInstance::theModuleCarriesNoPlatformIfdef()
{
    // 本模块刻意不分成「平台无关 + 三平台薄层」两部分，因为
    // QSharedMemory / QLocalServer / QLocalSocket 三者都是 Qt 自带且三平台
    // 都有的实现。一旦有人往这里加 `#ifdef Q_OS_WIN`，就说明这个前提被
    // 打破了——那时应当像 Files/ 与 Platform/ 的其余部分那样把规则抽出来，
    // 而不是在服务层里分叉。
    const QStringList files = {QStringLiteral("/Services/Platform/instanceprotocol.cpp"),
                               QStringLiteral("/Services/Platform/singleinstance.cpp")};
    for (const QString &file : files) {
        const QString source = readSourceFile(file);
        QVERIFY2(!source.isEmpty(), qPrintable(file));
        const QStringList hits = platformIfdefsIn(source);
        QVERIFY2(hits.isEmpty(), qPrintable(file + QStringLiteral(": ") + hits.join(QStringLiteral(" | "))));
    }
}

void Tst_SingleInstance::theModuleAvoidsQHashForIdentity()
{
    // 本仓的一条真实教训：`qHash` 对每个进程使用随机种子，用它算端点名或
    // 共享内存键会让「第一个实例记下的名字」与「第二个实例算出来的名字」
    // 永远对不上——单实例机制静默失效，且不会有任何报错。
    // 因此标识一律走 `stableTokenFingerprint`（FNV-1a）。
    //
    // 判断前先去掉注释：这个文件的注释里**必须**能写「不要用 qHash」，
    // 而那不该被判成违规（护栏扫注释是本仓踩过的另一个坑）。
    const QString source = readSourceFile(QStringLiteral("/Services/Platform/instanceprotocol.cpp"));
    QVERIFY(!source.isEmpty());
    const QString code = withoutComments(source);
    QVERIFY2(!code.contains(QStringLiteral("qHash(")), "标识不能用 qHash 算");
    QVERIFY2(code.contains(QStringLiteral("16777619")), "稳定哈希的 FNV 常数应当在源码里");
    // 顺带确认这条判定不是「去掉注释之后什么都剩不下」。
    QVERIFY2(code.contains(QStringLiteral("stableTokenFingerprint")),
             "去掉注释之后应当还剩下实现本身");
}

void Tst_SingleInstance::theSourceGuardActuallyFailsOnABrokenSample()
{
    // 反向验证：拿一份故意写坏的样本跑上面那两个判定，必须报出来。
    const QString brokenGui = QStringLiteral("#include <QtWidgets/QApplication>\n");
    QVERIFY(!guiIncludesIn(brokenGui).isEmpty());

    const QString cleanGui = QStringLiteral("#include <QCoreApplication>\n#include \"instanceprotocol.h\"\n");
    QVERIFY(guiIncludesIn(cleanGui).isEmpty());

    const QString brokenIfdef = QStringLiteral("#ifdef Q_OS_WIN\n#endif\n");
    QVERIFY(!platformIfdefsIn(brokenIfdef).isEmpty());
    QVERIFY(platformIfdefsIn(cleanGui).isEmpty());

    // 「去掉注释」这一步本身也要反向验证：注释里的 qHash 不算数，
    // 代码里的才算。少了这条，上面那条断言可能只是「恰好源码里没有它」。
    QVERIFY(!withoutComments(QStringLiteral("// 不要用 qHash(x) 来算标识")).contains(
        QStringLiteral("qHash(")));
    QVERIFY(withoutComments(QStringLiteral("/* qHash(x) */")).simplified().isEmpty());
    QVERIFY(withoutComments(QStringLiteral("const uint h = qHash(name);")).contains(
        QStringLiteral("qHash(")));
}
