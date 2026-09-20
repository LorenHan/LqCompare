#include "comparesession.h"

#include <QLatin1String>

#include <utility>

namespace LqCompare {

int SessionProgress::percent() const
{
    if (total <= 0) {
        return -1;
    }
    if (current <= 0) {
        return 0;
    }
    if (current >= total) {
        return 100;
    }
    return static_cast<int>((static_cast<qint64>(current) * 100) / total);
}

namespace {

///
/// 把三个自定义载体注册进元类型系统。
///
/// 现在全是直连（同线程），不注册也能跑；但会话的进度上报迟早会来自
/// 后台线程（扫描大目录、读取压缩包），那时如果忘了注册，**跨线程投递会静默失败**：
/// `QObject::connect` 只在运行期抱怨一句，界面上表现为「进度条一直不动」。
/// 在这里一次性注册，代价是四行。
///
void ensureSessionMetaTypes()
{
    static const bool registered = []() {
        qRegisterMetaType<LqCompare::SessionError>("LqCompare::SessionError");
        qRegisterMetaType<LqCompare::SessionProgress>("LqCompare::SessionProgress");
        qRegisterMetaType<LqCompare::CompareSession::State>("LqCompare::CompareSession::State");
        return true;
    }();
    Q_UNUSED(registered);
}

} // namespace

CompareSession::CompareSession(QString typeId, QObject *parent)
    : QObject(parent), m_typeId(std::move(typeId))
{
    ensureSessionMetaTypes();
}

CompareSession::~CompareSession() = default;

void CompareSession::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    emit titleChanged(m_title);
}

QWidget *CompareSession::createWidget(QWidget *parent)
{
    if (m_state == State::Closed) {
        reportError(tr("This session has been closed, so it cannot provide a view any more."),
                    tr("Reopening a closed session would show an empty pane with no way to "
                       "load it again; open the data source as a new session instead."));
        return nullptr;
    }
    if (m_widget) {
        return m_widget;
    }

    m_widget = createView(parent);
    if (!m_widget) {
        reportError(tr("The session could not build its view."),
                    tr("createView() returned no widget. The session type is responsible for "
                       "always returning one; a null view leaves the tab empty with no hint."));
    }
    return m_widget;
}

bool CompareSession::open(QString *error)
{
    if (m_state == State::Closed) {
        const QString reason =
            tr("This session has been closed and cannot be opened again.");
        if (error) {
            *error = reason;
        }
        reportError(reason, tr("A closed session no longer holds its data source; create a new "
                               "session to open the same data again."));
        return false;
    }
    if (m_state == State::Open) {
        // 幂等，且**不再调 doOpen()**：界面在恢复标签、切换布局时会重复调用，
        // 重跑一遍 doOpen 会把用户已经滚动到的位置、展开的节点、正在编辑的文本
        // 全部重置回初始状态——而用户只是切了一下标签。
        return true;
    }
    if (m_state == State::Opening) {
        const QString reason = tr("This session is already being opened.");
        if (error) {
            *error = reason;
        }
        reportError(reason, tr("open() was called re-entrantly (from inside doOpen()); the second "
                               "call is ignored so the data source is only loaded once."));
        return false;
    }

    setState(State::Opening);
    QString subclassReason;
    if (!doOpen(&subclassReason)) {
        // 停在 Failed 而不是退回 Created：界面上「还没打开」要提示「请选择文件」，
        // 「上次打开失败」要显示失败原因，两者必须能区分。
        setState(State::Failed);
        const QString message = failureMessage(subclassReason, "be opened");
        if (error) {
            *error = message;
        }
        reportError(message);
        return false;
    }

    setState(State::Open);
    setDirty(false);
    if (error) {
        error->clear();
    }
    return true;
}

bool CompareSession::reload(QString *error)
{
    if (m_state != State::Open) {
        const QString reason = tr("Only an open session can be reloaded.");
        if (error) {
            *error = reason;
        }
        reportError(reason, tr("The session is not open yet, so there is nothing to reread."));
        return false;
    }
    if (m_dirty) {
        // 把「有未保存改动时不许重载」放在基类，而不是交给每个会话类型自己判：
        // 忘了判的后果是用户几十处编辑无声消失，而他只是点了一下「重新加载」。
        // 界面在用户确认丢弃之后调用 setDirty(false) 再重载即可——那条路是显式的，
        // 因此不会因为某处漏判而被走错。
        const QString reason =
            tr("This session has unsaved changes. Save them or discard them before reloading.");
        if (error) {
            *error = reason;
        }
        reportError(reason, tr("Reloading rereads both sides from disk; unsaved changes would be "
                               "lost with no way back."));
        return false;
    }

    QString subclassReason;
    if (!doReload(&subclassReason)) {
        // 状态**保持 Open**：一次重载失败不能把整个会话判死——用户手上的数据还在，
        // 界面上仍然能继续看，只是其中一侧停留在旧内容上。
        const QString message = failureMessage(subclassReason, "be reloaded");
        if (error) {
            *error = message;
        }
        reportError(message);
        return false;
    }

    setDirty(false);
    if (error) {
        error->clear();
    }
    return true;
}

bool CompareSession::save(QString *error)
{
    if (!canSave()) {
        QString reason;
        QString detail;
        if (m_state != State::Open) {
            reason = tr("This session is not open, so there is nothing to save.");
            detail = tr("Saving an unopened session would either write an empty file or fail on a "
                        "missing data source; the command should be disabled here.");
        } else {
            reason = tr("There are no changes to save.");
            detail = tr("Writing an unchanged session rewrites the same bytes; on a synced folder "
                        "that produces a stream of meaningless revisions, and on a read-only "
                        "medium it fails for no reason.");
        }
        if (error) {
            *error = reason;
        }
        reportError(reason, detail);
        return false;
    }

    QString subclassReason;
    if (!doSave(&subclassReason)) {
        // 失败**不清脏标记**：内容确实还是没被保存，把标记清掉等于告诉用户
        // 「已经存好了」，之后他关窗口时就不会再被提醒。
        const QString message = failureMessage(subclassReason, "be saved");
        if (error) {
            *error = message;
        }
        reportError(message);
        return false;
    }

    setDirty(false);
    if (error) {
        error->clear();
    }
    return true;
}

void CompareSession::close()
{
    if (m_state == State::Closed) {
        return; // 幂等：界面在「关标签」与「退出程序」两条路上都会调它。
    }
    // 即使当前状态是 Failed / Created 也要调 doClose()：失败的打开可能已经
    // 占住了一部分资源（打开了一半的文件、起了后台线程），子类正好在这里收尾。
    doClose();
    setState(State::Closed);

    // 刻意**不**销毁视图，也刻意不清空 m_widget。
    //
    // 视图的父子关系属于容器：会话基类无法知道这个视图有没有被别处引用
    // （分离窗格会把同一个视图挂到另一个窗口下）。基类替容器删掉它就是越权，
    // 而后果是容器手上留一个悬空指针。容器删标签时视图会随父对象一起析构，
    // `m_widget` 是 QPointer，那时会自动变成 nullptr。
    //
    // 已关闭的会话仍可能持有视图对象，但 createWidget() 已经拒绝再提供它，
    // 界面也没有理由继续访问——这条边界写在 createWidget() 的错误说明里。
}

bool CompareSession::canSave() const
{
    // 状态这一条由基类判，子类只回答「我这个类型现在存得下去吗」。
    // 让每个子类自己带上状态判断的话，漏掉的那个会在未打开时被允许保存。
    if (m_state != State::Open) {
        return false;
    }
    return canSaveNow();
}

SessionSettings *CompareSession::sessionSettings()
{
    if (!m_settings) {
        m_settings = createSettings(); // 惰性构造，理由见头文件（构造函数里调虚函数不生效）
    }
    return m_settings;
}

void CompareSession::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

void CompareSession::setStatusText(const QString &text)
{
    // 文本没变就不发信号：状态栏刷新是「按当前文件重写一遍文字」的常见写法，
    // 每次都发会让状态栏在批量过程中反复重排，界面看起来在抖。
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged(m_statusText);
}

void CompareSession::reportError(const QString &message, const QString &detail)
{
    SessionError report;
    report.message = message;
    report.detail = detail;
    // 错误是**事件**而不是状态，因此不去重：同一个原因连续失败两次，
    // 上层要能知道「它又试了一次并再次失败」，而不是以为信号漏了一个。
    emit errorReported(report);
}

void CompareSession::reportProgress(int current, int total, const QString &what)
{
    SessionProgress next;
    next.current = current;
    next.total = total;
    next.what = what;
    if (next.current == m_progress.current && next.total == m_progress.total
        && next.what == m_progress.what) {
        return; // 逐项循环里同一个「N / M」会被重复上报，去重避免刷爆事件队列
    }
    m_progress = next;
    emit progressChanged(m_progress);
}

bool CompareSession::doOpen(QString *error)
{
    Q_UNUSED(error);
    return true;
}

bool CompareSession::doReload(QString *error)
{
    return doOpen(error);
}

bool CompareSession::doSave(QString *error)
{
    Q_UNUSED(error);
    return true;
}

void CompareSession::doClose()
{
}

bool CompareSession::canSaveNow() const
{
    return m_dirty;
}

SessionSettings *CompareSession::createSettings()
{
    return new MemorySessionSettings(this);
}

void CompareSession::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

QString CompareSession::failureMessage(const QString &subclassReason, const char *action) const
{
    if (!subclassReason.isEmpty()) {
        return subclassReason;
    }
    // 兜底文案是必需的：子类完全可以在失败时不写原因（参数可为空指针）。
    // 一个 message 为空的 error 信号到了界面上就是「一个没有内容的错误对话框」。
    return tr("The session could not %1.").arg(QLatin1String(action));
}

} // namespace LqCompare
