#include "instanceprotocol.h"

#include <QCoreApplication>
#include <QProcessEnvironment>

namespace LqCompare {
namespace Platform {

namespace {

// 帧的固定头部：魔数 4 + 类型 1 + 版本 1 + 载荷长度 4。
constexpr int kFrameHeaderSize = 10;
const char kMagic[4] = {'L', 'Q', 'C', 'I'};
constexpr quint8 kKindRequest = 1;
constexpr quint8 kKindReply = 2;

/// 一次转发携带的载荷大小上界。命令行再长也到不了 64 KB（Windows 的
/// `CreateProcess` 自己就把命令行限制在 32 KB 上下），因此这个上界只会在
/// 「对面不是一个正常的客户端」时生效——而那正是需要它的时刻：没有上界时
/// 一个伪造的长度字段就足以让我们为它分配一块内存。
constexpr quint32 kMaxPayloadBytes = 64u * 1024u;
constexpr int kMaxArgumentCount = 4096;

void appendU32(QByteArray &out, quint32 value)
{
    out.append(static_cast<char>((value >> 24) & 0xffu));
    out.append(static_cast<char>((value >> 16) & 0xffu));
    out.append(static_cast<char>((value >> 8) & 0xffu));
    out.append(static_cast<char>(value & 0xffu));
}

void appendString(QByteArray &out, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    appendU32(out, static_cast<quint32>(utf8.size()));
    out.append(utf8);
}

///
/// \brief 一段字节是不是合法的 UTF-8。
///
/// 用「解出来再编回去是否逐字节相同」判定。`QString::fromUtf8` 会把非法序列
/// 换成 U+FFFD，而 U+FFFD 本身是合法字符，两者从解码结果上分不开——但
/// **再编回去就不一样了**：非法序列变成 `EF BF BD`，与原字节不同即判非法；
/// 而原文里真的有一个 U+FFFD 时往返是逐字相同的，判合法也是对的。
///
/// 本项目在**文件名**上有更严格的方案（`PathName` 用私存码位承载原始字节，
/// 见 PLAT-007）。这里刻意不用那一套：转发的是命令行参数，一个非法 UTF-8 的
/// 参数意味着对面不是我们自己的客户端，此时**拒绝**比保真更有用。
///
bool isValidUtf8(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes).toUtf8() == bytes;
}

///
/// \brief 从一帧里顺序读字段的游标。
///
/// 每个读取都先检查剩余字节，因此下面的解码函数里看不到手工的下标运算——
/// 「长度字段超界」是本协议最需要防住的一类输入，把检查收在一处比散在
/// 十几个地方可靠。
///
class FrameReader
{
public:
    explicit FrameReader(const QByteArray &data, int offset = 0)
        : m_data(data), m_position(offset)
    {
    }

    bool readU8(quint8 *out, QString *problem)
    {
        if (remaining() < 1) {
            return fail(QStringLiteral("载荷在读第 %1 个字节时截断").arg(m_position), problem);
        }
        *out = static_cast<quint8>(m_data.at(m_position));
        ++m_position;
        return true;
    }

    bool readU32(quint32 *out, QString *problem)
    {
        if (remaining() < 4) {
            return fail(QStringLiteral("载荷在读第 %1 个字节处的 4 字节长度时截断").arg(m_position),
                        problem);
        }
        quint32 value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 8) | static_cast<quint8>(m_data.at(m_position + i));
        }
        m_position += 4;
        *out = value;
        return true;
    }

    ///
    /// \brief 读一个「长度 + UTF-8 字节」的字符串。
    ///
    /// 长度先与**剩余字节**比对再分配：不比对的话，一个声明的长度会让我们
    /// 先按它去 `reserve`，而实际数据可能一个字节都没有。
    ///
    bool readString(QString *out, QString *problem)
    {
        quint32 length = 0;
        if (!readU32(&length, problem)) {
            return false;
        }
        if (length > static_cast<quint32>(remaining())) {
            return fail(QStringLiteral("声明的字符串长度 %1 超过剩余的 %2 字节")
                            .arg(length)
                            .arg(remaining()),
                        problem);
        }
        const QByteArray raw = m_data.mid(m_position, static_cast<int>(length));
        m_position += static_cast<int>(length);
        if (!isValidUtf8(raw)) {
            return fail(QStringLiteral("字符串不是合法的 UTF-8（%1 字节）").arg(length), problem);
        }
        *out = QString::fromUtf8(raw);
        return true;
    }

    int remaining() const { return m_data.size() - m_position; }
    bool exhausted() const { return m_position == m_data.size(); }

private:
    bool fail(const QString &reason, QString *problem)
    {
        if (problem) {
            *problem = reason;
        }
        return false;
    }

    const QByteArray &m_data;
    int m_position = 0;
};

///
/// \brief 校验帧头并把载荷交给调用方。
///
bool splitFrame(const QByteArray &frame, quint8 expectedKind, QByteArray *payload, QString *problem)
{
    if (frame.size() < kFrameHeaderSize) {
        if (problem) {
            *problem = QStringLiteral("帧只有 %1 字节，不足一个 %2 字节的头部")
                           .arg(frame.size())
                           .arg(kFrameHeaderSize);
        }
        return false;
    }
    if (qstrncmp(frame.constData(), kMagic, 4) != 0) {
        if (problem) {
            *problem = QStringLiteral("魔数不匹配（前 4 字节不是 LQCI）");
        }
        return false;
    }
    const quint8 kind = static_cast<quint8>(frame.at(4));
    if (kind != expectedKind) {
        if (problem) {
            *problem = QStringLiteral("消息类型是 %1，期望 %2").arg(kind).arg(expectedKind);
        }
        return false;
    }
    const quint8 version = static_cast<quint8>(frame.at(5));
    if (version != static_cast<quint8>(relayProtocolVersion())) {
        if (problem) {
            *problem = QStringLiteral("协议版本是 %1，本进程认的是 %2")
                           .arg(version)
                           .arg(relayProtocolVersion());
        }
        return false;
    }

    quint32 declared = 0;
    for (int i = 6; i < 10; ++i) {
        declared = (declared << 8) | static_cast<quint8>(frame.at(i));
    }
    if (declared > kMaxPayloadBytes) {
        if (problem) {
            *problem = QStringLiteral("声明的载荷 %1 字节超过上界 %2").arg(declared).arg(kMaxPayloadBytes);
        }
        return false;
    }
    // 要求「正好一帧」而不是「至少一帧」：解码函数的契约是「给我一帧」。
    // 允许尾部有多余字节的话，「两次转发粘在一起」会被静默当成一次，
    // 第二个请求从此永远排在队列里等一个不会到来的帧头。
    if (declared != static_cast<quint32>(frame.size() - kFrameHeaderSize)) {
        if (problem) {
            *problem = QStringLiteral("声明载荷 %1 字节，实际带了 %2 字节")
                           .arg(declared)
                           .arg(frame.size() - kFrameHeaderSize);
        }
        return false;
    }
    *payload = frame.mid(kFrameHeaderSize);
    return true;
}

QByteArray composeFrame(quint8 kind, const QByteArray &payload)
{
    QByteArray out;
    out.reserve(kFrameHeaderSize + payload.size());
    out.append(kMagic, 4);
    out.append(static_cast<char>(kind));
    out.append(static_cast<char>(relayProtocolVersion()));
    appendU32(out, static_cast<quint32>(payload.size()));
    out.append(payload);
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
// 标识符
// -----------------------------------------------------------------------------

int instanceEndpointNameLimit()
{
    return 40;
}

int instanceSharedMemoryKeyLimit()
{
    return 31;
}

QString stableTokenFingerprint(const QString &text)
{
    // FNV-1a 32 位。**刻意不用 qHash()**：它对每个进程使用随机种子，
    // 于是同一个字符串在两个进程里得到不同的值，而这里推出来的名字必须
    // 跨进程一致——否则第二个实例算出来的名字与第一个记下的永远对不上，
    // 表现是「单实例机制完全没生效」，且不会有任何报错。
    const QByteArray bytes = text.toUtf8();
    quint32 hash = 2166136261u;
    for (int i = 0; i < bytes.size(); ++i) {
        hash ^= static_cast<quint8>(bytes.at(i));
        hash *= 16777619u;
    }
    return QString::number(hash, 16).rightJustified(8, QLatin1Char('0'));
}

QString sanitizeInstanceToken(const QString &raw)
{
    QString sanitized;
    sanitized.reserve(raw.size());
    bool replacedAnything = false;
    for (int i = 0; i < raw.size(); ++i) {
        const QChar character = raw.at(i);
        const bool safe = (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
            || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
            || (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            || character == QLatin1Char('.') || character == QLatin1Char('_')
            || character == QLatin1Char('-');
        if (safe) {
            sanitized.append(character);
        } else {
            sanitized.append(QLatin1Char('_'));
            replacedAnything = true;
        }
    }

    if (sanitized.isEmpty()) {
        return QString();
    }
    // 有字符被替换掉时补一段原始输入的特征值：中文用户名、带空格或带反斜杠的
    // 用户名经净化后都是「一串下划线」，两个不同的用户会因此撞进同一个标识，
    // 其中一个的程序会被另一个的实例吞掉。
    if (replacedAnything) {
        sanitized += QLatin1Char('_') + stableTokenFingerprint(raw);
    }
    return sanitized;
}

QString composeInstanceIdentifier(const QString &prefix,
                                  const QString &seed,
                                  const QString &userScope,
                                  int limit)
{
    const QString safePrefix = sanitizeInstanceToken(prefix);
    const QString safeSeed = sanitizeInstanceToken(seed);
    const QString safeScope = sanitizeInstanceToken(userScope);
    const QString head = safePrefix.isEmpty() ? QStringLiteral("lqcompare") : safePrefix;
    const QString tail = safeSeed.isEmpty() ? QStringLiteral("app") : safeSeed;
    const QString middle = safeScope.isEmpty() ? QStringLiteral("anonymous") : safeScope;

    // 可读部分不能决定身份："a-b" + "c" 与 "a" + "b-c" 的拼接相同，
    // 而净化后的字符串本身也可能是另一个合法输入。对原始字段加长度前缀，
    // 再为所有标识追加摘要（短标识也一样），保留字段边界与净化前的区别。
    const QString identity = QString::number(prefix.size()) + QLatin1Char(':') + prefix
        + QString::number(userScope.size()) + QLatin1Char(':') + userScope
        + QString::number(seed.size()) + QLatin1Char(':') + seed;
    const QString fingerprint = stableTokenFingerprint(identity);
    const QString suffix = QLatin1Char('-') + fingerprint;
    const QString readable = head + QLatin1Char('-') + middle + QLatin1Char('-') + tail;
    if (limit <= 0 || readable.size() + suffix.size() <= limit) {
        return readable + suffix;
    }

    // 上限只裁剪可读部分，保留同一份原始身份摘要。
    if (limit <= suffix.size()) {
        return fingerprint.left(limit);
    }
    return readable.left(limit - suffix.size()) + suffix;
}

QString instanceEndpointName(const QString &seed, const QString &userScope)
{
    return composeInstanceIdentifier(QStringLiteral("lqcompare"), seed, userScope,
                                     instanceEndpointNameLimit());
}

QString instanceSharedMemoryKey(const QString &seed, const QString &userScope)
{
    return composeInstanceIdentifier(QStringLiteral("lqc"), seed, userScope,
                                     instanceSharedMemoryKeyLimit());
}

QString instanceUserScopeFromName(const QByteArray &userName)
{
    const QString name = QString::fromLocal8Bit(userName).trimmed();
    if (name.isEmpty()) {
        return QStringLiteral("anonymous");
    }
    return name;
}

QString defaultInstanceUserScope()
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QByteArray user = environment.value(QStringLiteral("USER")).toLocal8Bit();
    if (user.isEmpty()) {
        user = environment.value(QStringLiteral("USERNAME")).toLocal8Bit();
    }
    return instanceUserScopeFromName(user);
}

// -----------------------------------------------------------------------------
// 转发结果与退出码
// -----------------------------------------------------------------------------

int relayExitCodeBandFirst()
{
    return 10;
}

int relayExitCodeBandLast()
{
    return 19;
}

int cliReservedExitCodeLast()
{
    // CLI-004 的规格原文：「0 = 无差异、1 = 有差异、2 = 参数或使用错误、
    // 3 = 无法打开数据源、4 = 内部错误」。
    return 4;
}

const QVector<RelayExitCodeRow> &relayExitCodeTable()
{
    // 函数内静态并返回引用：按值返回再取元素指针会立刻悬垂（本仓踩过，
    // 现象是取标签的函数返回空串、下一条用例直接 SIGSEGV）。
    static const QVector<RelayExitCodeRow> table = {
        {RelayStatus::NotAttempted, 0, "not-attempted",
         QStringLiteral("没有发生转发（本进程就是首个实例，或单实例机制被关掉了）")},
        {RelayStatus::Delivered, 10, "delivered",
         QStringLiteral("参数已转交给正在运行的实例")},
        {RelayStatus::Rejected, 11, "rejected",
         QStringLiteral("已连上正在运行的实例，但它拒绝了这份参数")},
        {RelayStatus::NoPrimary, 12, "no-primary",
         QStringLiteral("套接字上没有人应答（没有正在运行的实例，或它已退出）")},
        {RelayStatus::HandshakeTimeout, 13, "handshake-timeout",
         QStringLiteral("已连上正在运行的实例，但它在时限内没有应答")},
    };
    return table;
}

const char *relayStatusIdentifier(RelayStatus status)
{
    // 从表里查而不是另写一份 switch：标识有两个来源时，某天有人只改了
    // 一处，`--help` 里印的和日志里印的就会是两个不同的字符串。
    const QVector<RelayExitCodeRow> &table = relayExitCodeTable();
    for (const RelayExitCodeRow &row : table) {
        if (row.status == status) {
            return row.identifier;
        }
    }
    return "unknown";
}

QString relayStatusText(RelayStatus status)
{
    const QVector<RelayExitCodeRow> &table = relayExitCodeTable();
    for (const RelayExitCodeRow &row : table) {
        if (row.status == status) {
            return row.text;
        }
    }
    return QStringLiteral("未知的转发结局");
}

int relayExitCode(RelayStatus status)
{
    const QVector<RelayExitCodeRow> &table = relayExitCodeTable();
    for (const RelayExitCodeRow &row : table) {
        if (row.status == status) {
            return row.code;
        }
    }
    return 0;
}

QVector<QString> validateRelayExitCodeTable(const QVector<RelayExitCodeRow> &table)
{
    QVector<QString> problems;

    const QVector<RelayStatus> expected = {
        RelayStatus::NotAttempted, RelayStatus::Delivered, RelayStatus::Rejected,
        RelayStatus::NoPrimary,    RelayStatus::HandshakeTimeout,
    };

    // 行数也要查：只查「这 5 种各出现一次」时，有人新增了一种结局却忘了
    // 往 `expected` 里加，那条新行会被完全放过——而它恰恰是最需要被看见的
    // 一行（它的退出码没有任何约束）。
    if (table.size() != expected.size()) {
        problems.append(QStringLiteral("表里有 %1 行，已知的结局有 %2 种——多的那一行没有任何约束")
                            .arg(table.size())
                            .arg(expected.size()));
    }
    for (RelayStatus status : expected) {
        int seen = 0;
        for (const RelayExitCodeRow &row : table) {
            if (row.status == status) {
                ++seen;
            }
        }
        if (seen != 1) {
            problems.append(QStringLiteral("结局「%1」在表里出现 %2 次，应当恰好 1 次")
                                .arg(QString::fromLatin1(relayStatusIdentifier(status)))
                                .arg(seen));
        }
    }

    QVector<int> codes;
    QVector<QString> identifiers;
    QVector<QString> texts;
    for (const RelayExitCodeRow &row : table) {
        // 一律按**表里的那一行**取标识，而不是按 status 回查本模块的表：
        // 回查的话，用例拿一份标识写重复的坏表进来，查出来的还是模块里那份
        // 好标识，于是这条检查永远不会红。自检的数据来源必须是参数。
        const bool hasIdentifier = row.identifier != nullptr && *row.identifier != '\0';
        const QString identifier = hasIdentifier ? QString::fromLatin1(row.identifier)
                                                 : QStringLiteral("<空标识>");

        if (row.status == RelayStatus::NotAttempted) {
            if (row.code != 0) {
                problems.append(QStringLiteral("「没有发生转发」的退出码应当是 0，实际是 %1")
                                    .arg(row.code));
            }
        } else if (row.code < relayExitCodeBandFirst() || row.code > relayExitCodeBandLast()) {
            problems.append(QStringLiteral("结局「%1」的退出码 %2 落在转发区间 %3~%4 之外")
                                .arg(identifier)
                                .arg(row.code)
                                .arg(relayExitCodeBandFirst())
                                .arg(relayExitCodeBandLast()));
        }
        if (row.code > 0 && row.code <= cliReservedExitCodeLast()) {
            problems.append(QStringLiteral("结局「%1」的退出码 %2 与 CLI-004 已占用的 0~%3 重叠")
                                .arg(identifier)
                                .arg(row.code)
                                .arg(cliReservedExitCodeLast()));
        }

        if (!hasIdentifier) {
            problems.append(QStringLiteral("退出码 %1 那一行没有机器标识").arg(row.code));
        } else if (identifiers.contains(identifier)) {
            problems.append(QStringLiteral("机器标识「%1」在表里出现了两次").arg(identifier));
        }
        if (row.text.isEmpty()) {
            problems.append(QStringLiteral("结局「%1」没有说明文案").arg(identifier));
        } else if (texts.contains(row.text)) {
            problems.append(QStringLiteral("说明文案「%1」被两个结局共用").arg(row.text));
        }

        if (codes.contains(row.code)) {
            problems.append(QStringLiteral("退出码 %1 被两个结局共用").arg(row.code));
        }
        codes.append(row.code);
        identifiers.append(identifier);
        texts.append(row.text);
    }

    return problems;
}

// -----------------------------------------------------------------------------
// 命令行开关
// -----------------------------------------------------------------------------

const char *singleInstanceSwitchDecisionIdentifier(SingleInstanceSwitchDecision decision)
{
    switch (decision) {
    case SingleInstanceSwitchDecision::Unset:    return "unset";
    case SingleInstanceSwitchDecision::Enabled:  return "enabled";
    case SingleInstanceSwitchDecision::Disabled: return "disabled";
    }
    return "unknown";
}

const QVector<SingleInstanceSwitch> &singleInstanceSwitches()
{
    static const QVector<SingleInstanceSwitch> table = {
        {QStringLiteral("single-instance"), SingleInstanceSwitchDecision::Enabled,
         QStringLiteral("Hand arguments over to a running instance (overrides settings).")},
        {QStringLiteral("no-single-instance"), SingleInstanceSwitchDecision::Disabled,
         QStringLiteral("Always start a new instance (useful for testing and automation).")},
    };
    return table;
}

SingleInstanceSwitchDecision resolveSingleInstanceSwitch(
    const QVector<SingleInstanceSwitch> &table,
    const QStringList &setSwitchNames,
    SingleInstanceSwitchDecision fallback)
{
    SingleInstanceSwitchDecision decision = fallback;
    for (const SingleInstanceSwitch &row : table) {
        if (row.decision == SingleInstanceSwitchDecision::Unset) {
            continue;
        }
        if (setSwitchNames.contains(row.name)) {
            decision = row.decision;
        }
    }
    return decision;
}

// -----------------------------------------------------------------------------
// 窗口置前策略
// -----------------------------------------------------------------------------

const char *activationPolicyIdentifier(ActivationPolicy policy)
{
    switch (policy) {
    case ActivationPolicy::Always:               return "always";
    case ActivationPolicy::UnlessTypingRecently: return "unless-typing-recently";
    case ActivationPolicy::Never:                return "never";
    }
    return "unknown";
}

QString activationPolicyText(ActivationPolicy policy)
{
    switch (policy) {
    case ActivationPolicy::Always:
        return QStringLiteral("总是把窗口置前");
    case ActivationPolicy::UnlessTypingRecently:
        return QStringLiteral("最近有输入时不把窗口置前（避免打断正在打字的人）");
    case ActivationPolicy::Never:
        return QStringLiteral("从不把窗口置前，只把参数交给会话");
    }
    return QStringLiteral("未知的置前策略");
}

int defaultActivationQuietWindowMs()
{
    return 2500;
}

bool shouldActivateWindow(ActivationPolicy policy,
                          int millisecondsSinceLastInput,
                          int quietWindowMs)
{
    switch (policy) {
    case ActivationPolicy::Always:
        return true;
    case ActivationPolicy::Never:
        return false;
    case ActivationPolicy::UnlessTypingRecently:
        // 负数表示「不知道」。不知道时置前：这条走的正是「用户双击了一个
        // 文件」的主路径，把他要的窗口藏在别的窗口后面比抢一次焦点更糟。
        if (millisecondsSinceLastInput < 0) {
            return true;
        }
        return millisecondsSinceLastInput >= quietWindowMs;
    }
    return true;
}

// -----------------------------------------------------------------------------
// 线协议
// -----------------------------------------------------------------------------

int relayProtocolVersion()
{
    return 1;
}

quint32 relayMaxPayloadBytes()
{
    return kMaxPayloadBytes;
}

int relayMaxArgumentCount()
{
    return kMaxArgumentCount;
}

int relayFrameHeaderSize()
{
    return kFrameHeaderSize;
}

QByteArray encodeRelayRequest(const RelayRequest &request)
{
    QByteArray payload;
    appendString(payload, request.workingDirectory);
    appendU32(payload, static_cast<quint32>(request.arguments.size()));
    for (const QString &argument : request.arguments) {
        appendString(payload, argument);
    }
    return composeFrame(kKindRequest, payload);
}

QByteArray encodeRelayReply(const RelayReply &reply)
{
    QByteArray payload;
    payload.append(static_cast<char>(reply.accepted ? 1 : 0));
    appendU32(payload, static_cast<quint32>(reply.argumentCount));
    appendString(payload, reply.detail);
    return composeFrame(kKindReply, payload);
}

bool decodeRelayRequest(const QByteArray &frame, RelayRequest *out, QString *problem)
{
    QByteArray payload;
    if (!splitFrame(frame, kKindRequest, &payload, problem)) {
        return false;
    }

    FrameReader reader(payload);
    RelayRequest decoded;
    if (!reader.readString(&decoded.workingDirectory, problem)) {
        return false;
    }
    quint32 count = 0;
    if (!reader.readU32(&count, problem)) {
        return false;
    }
    if (count > static_cast<quint32>(kMaxArgumentCount)) {
        if (problem) {
            *problem = QStringLiteral("声明的参数个数 %1 超过上界 %2").arg(count).arg(kMaxArgumentCount);
        }
        return false;
    }
    decoded.arguments.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
        QString argument;
        if (!reader.readString(&argument, problem)) {
            return false;
        }
        decoded.arguments.append(argument);
    }
    if (!reader.exhausted()) {
        if (problem) {
            *problem = QStringLiteral("载荷尾部多出 %1 个字节").arg(reader.remaining());
        }
        return false;
    }

    if (out) {
        *out = decoded;
    }
    return true;
}

bool decodeRelayReply(const QByteArray &frame, RelayReply *out, QString *problem)
{
    QByteArray payload;
    if (!splitFrame(frame, kKindReply, &payload, problem)) {
        return false;
    }

    FrameReader reader(payload);
    RelayReply decoded;
    quint8 accepted = 0;
    if (!reader.readU8(&accepted, problem)) {
        return false;
    }
    if (accepted > 1) {
        if (problem) {
            *problem = QStringLiteral("「是否接受」字段是 %1，只认 0 与 1").arg(accepted);
        }
        return false;
    }
    decoded.accepted = (accepted == 1);

    quint32 count = 0;
    if (!reader.readU32(&count, problem)) {
        return false;
    }
    if (count > static_cast<quint32>(kMaxArgumentCount)) {
        if (problem) {
            *problem = QStringLiteral("应答里声明的参数个数 %1 超过上界 %2")
                           .arg(count)
                           .arg(kMaxArgumentCount);
        }
        return false;
    }
    decoded.argumentCount = static_cast<int>(count);

    if (!reader.readString(&decoded.detail, problem)) {
        return false;
    }
    if (!reader.exhausted()) {
        if (problem) {
            *problem = QStringLiteral("应答载荷尾部多出 %1 个字节").arg(reader.remaining());
        }
        return false;
    }

    if (out) {
        *out = decoded;
    }
    return true;
}

QString describeRelayRequest(const RelayRequest &request)
{
    return QStringLiteral("%1 个参数（工作目录 %2）：%3")
        .arg(request.arguments.size())
        .arg(request.workingDirectory.isEmpty() ? QStringLiteral("<空>") : request.workingDirectory)
        .arg(request.arguments.isEmpty() ? QStringLiteral("<无>")
                                         : request.arguments.join(QLatin1Char(' ')));
}

FrameStatus inspectRelayFrame(const QByteArray &buffer, int *frameSize, QString *problem)
{
    if (buffer.size() < kFrameHeaderSize) {
        return FrameStatus::Incomplete;
    }
    if (qstrncmp(buffer.constData(), kMagic, 4) != 0) {
        if (problem) {
            *problem = QStringLiteral("缓冲区开头不是本协议的帧（魔数不匹配）");
        }
        return FrameStatus::Invalid;
    }
    const quint8 version = static_cast<quint8>(buffer.at(5));
    if (version != static_cast<quint8>(relayProtocolVersion())) {
        if (problem) {
            *problem = QStringLiteral("帧声明的协议版本 %1 与本进程的 %2 不一致")
                           .arg(version)
                           .arg(relayProtocolVersion());
        }
        return FrameStatus::Invalid;
    }
    quint32 declared = 0;
    for (int i = 6; i < 10; ++i) {
        declared = (declared << 8) | static_cast<quint8>(buffer.at(i));
    }
    if (declared > kMaxPayloadBytes) {
        if (problem) {
            *problem = QStringLiteral("帧声明的载荷 %1 字节超过上界 %2")
                           .arg(declared)
                           .arg(kMaxPayloadBytes);
        }
        return FrameStatus::Invalid;
    }
    const quint32 total = declared + static_cast<quint32>(kFrameHeaderSize);
    if (static_cast<quint32>(buffer.size()) < total) {
        return FrameStatus::Incomplete;
    }
    if (frameSize) {
        *frameSize = static_cast<int>(total);
    }
    return FrameStatus::Complete;
}

void registerInstanceMetaTypes()
{
    // 函数内静态：只会注册一次。返回的整数随后就没用了，注册本身才是目的。
    static const int handle = qRegisterMetaType<RelayRequest>("LqCompare::Platform::RelayRequest");
    Q_UNUSED(handle)
}

} // namespace Platform
} // namespace LqCompare
