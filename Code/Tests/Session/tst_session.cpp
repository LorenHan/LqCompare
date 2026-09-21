#include "tst_session.h"

#include "comparesession.h"
#include "probesession.h"
#include "sessiontype.h"

#include <QFile>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>
#include <type_traits>

using LqCompare::CompareSession;
using LqCompare::MemorySessionSettings;
using LqCompare::SessionError;
using LqCompare::SessionProgress;
using LqCompare::SessionSettings;
using LqCompare::SessionType;
using LqCompare::SessionTypeEntry;
using LqCompare::SessionTypeRegistry;
using LqCompare::TestSupport::MinimalSession;
using LqCompare::TestSupport::ProbeSession;
using LqCompare::TestSupport::ProbeSettings;

namespace {

QString sessionStateIdentifier(CompareSession::State state)
{
    switch (state) {
    case CompareSession::State::Created:
        return QStringLiteral("Created");
    case CompareSession::State::Opening:
        return QStringLiteral("Opening");
    case CompareSession::State::Open:
        return QStringLiteral("Open");
    case CompareSession::State::Failed:
        return QStringLiteral("Failed");
    case CompareSession::State::Closed:
        return QStringLiteral("Closed");
    }
    return QStringLiteral("unknown");
}

///
/// 收集源码里 `#include "…"` 的目标，返回**不属于本模块白名单**的那些。
///
/// 抽成接受字符串的纯函数（而不是直接读文件），是为了能对一段**故意写坏**的
/// 源码做反向验证——见 theIncludeGuardWouldCatchAConcreteViewInclude()。
/// 一个从不报错的护栏比没有护栏更糟：它会让人以为这块已经被守住了。
///
QStringList foreignQuotedIncludes(const QString &source)
{
    // 白名单只有两项：会话基类自己，以及 Services/Session 的设置接口
    // （Views -> Services 是允许的依赖方向）。
    static const QSet<QString> allowed{QStringLiteral("comparesession.h"),
                                       QStringLiteral("session.h")};
    static const QRegularExpression pattern(QStringLiteral("^\\s*#\\s*include\\s+\"([^\"]+)\""));

    QStringList foreign;
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString target = match.captured(1);
        if (!allowed.contains(target)) {
            foreign << target;
        }
    }
    return foreign;
}

QString readSourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

namespace QTest {

// QCOMPARE 在失败时要打印值。不写这个特化就只能看到「Compared values are not
// the same」，而状态机用例失败时最需要的恰恰是「停在了哪个状态」。
// 必须是 `template <>`：QCOMPARE 内部用的是带显式模板实参的 toString<T>(x)，
// 普通重载不参与重载决议，写了也拿不到值（而且能编译，看不出异常）。
template <>
char *toString(const LqCompare::CompareSession::State &state)
{
    return qstrdup(qPrintable(sessionStateIdentifier(state)));
}

} // namespace QTest

void TstSession::initTestCase()
{
    // F 组护栏用例依赖这两个文件真的读得到。先在这里失败一次，
    // 而不是让它们在「读到空串」的情况下静默通过——「没有断言失败」
    // 与「断言根本没跑」必须能区分开。
    QVERIFY(!readSourceFile(QStringLiteral("/Views/Session/comparesession.h")).isEmpty());
    QVERIFY(!readSourceFile(QStringLiteral("/Views/Session/comparesession.cpp")).isEmpty());
}

void TstSession::cleanupTestCase()
{
}

// ===========================================================================
// A 生命周期与状态迁移（标准第 1 条）
// ===========================================================================

void TstSession::baseClassIsAbstract()
{
    // 纯虚的 createView() 让基类无法被直接实例化——这是「每种会话类型都必须回答
    // 『我的视图是什么』」的编译期保证。少了它，一个漏写视图的会话类型会静静地
    // 打开一个空窗格，而界面上看不出是配置错了还是还没实现。
    QVERIFY(std::is_abstract<CompareSession>::value);

    // 但通过基类指针使用是完备的：契约里的每一项都能编过、能调到。
    ProbeSession probe;
    CompareSession *session = &probe;
    QCOMPARE(session->typeId(), QStringLiteral("probe"));
    QVERIFY(!session->isDirty());
    QVERIFY(!session->canSave());
    QVERIFY(session->sessionSettings() != nullptr);
    QVERIFY(session->widget() == nullptr);
    QCOMPARE(session->state(), CompareSession::State::Created);
}

void TstSession::typeIdAndTitleBehaveAsDocumented()
{
    ProbeSession probe;
    QCOMPARE(probe.typeId(), QStringLiteral("probe"));
    QVERIFY(probe.title().isEmpty());

    int titleSignals = 0;
    connect(&probe, &CompareSession::titleChanged, this,
            [&titleSignals](const QString &) { ++titleSignals; });

    probe.setTitle(QStringLiteral("a.txt ↔ b.txt"));
    QCOMPARE(probe.title(), QStringLiteral("a.txt ↔ b.txt"));
    QCOMPARE(titleSignals, 1);

    // 同样的标题不再发信号：容器把 titleChanged 直接接到标签文字上，
    // 重复发会让标签在重命名流程里反复重排。
    probe.setTitle(QStringLiteral("a.txt ↔ b.txt"));
    QCOMPARE(titleSignals, 1);
}

void TstSession::openRunsTheImplementationOnceAndIsIdempotent()
{
    ProbeSession probe;
    QCOMPARE(probe.state(), CompareSession::State::Created);

    QVERIFY(probe.open());
    QCOMPARE(probe.state(), CompareSession::State::Open);
    QCOMPARE(probe.openCalls, 1);

    // 第二次 open() 必须幂等，且**不再调 doOpen()**：界面在恢复标签、切换布局
    // 时会重复调用，重跑一遍会把滚动位置、展开的节点、正在编辑的文本全部重置，
    // 而用户只是切了一下标签。
    QVERIFY(probe.open());
    QCOMPARE(probe.openCalls, 1);
    QCOMPARE(probe.state(), CompareSession::State::Open);
}

void TstSession::openFailureStopsInFailedStateAndCarriesTheReason()
{
    ProbeSession probe;
    probe.openResult = false;
    probe.openReason = QStringLiteral("左侧文件已被删除。");

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QString error;
    QVERIFY(!probe.open(&error));

    // 停在 Failed 而不是退回 Created：界面上「还没打开」要提示「请选择文件」，
    // 「上次打开失败」要显示失败原因，两者必须能区分。
    QCOMPARE(probe.state(), CompareSession::State::Failed);
    QCOMPARE(error, probe.openReason);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.last().message, probe.openReason);

    // 失败之后仍然可以重试——Failed 不是终态，只有 Closed 是。
    probe.openResult = true;
    QVERIFY(probe.open());
    QCOMPARE(probe.state(), CompareSession::State::Open);
    QCOMPARE(probe.openCalls, 2);
}

void TstSession::openFailureWithoutAReasonGetsAFallbackMessage()
{
    ProbeSession probe;
    probe.openResult = false; // 刻意不设 openReason：子类有权不写原因

    QString error;
    QVERIFY(!probe.open(&error));

    // 兜底文案必须落在 message 上，而不是只写在 detail 里：状态栏与对话框都只读
    // message，空的 message 到了用户面前就是「一个没有内容的错误对话框」。
    QVERIFY(!error.isEmpty());

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });
    QVERIFY(!probe.open(&error));
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.last().message.isEmpty());
}

void TstSession::openAfterCloseIsRefused()
{
    ProbeSession probe;
    QVERIFY(probe.open());
    probe.close();

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QString error;
    QVERIFY(!probe.open(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(probe.openCalls, 1);
    QCOMPARE(probe.state(), CompareSession::State::Closed);

    // 拒绝的同时要说明「怎么办」：只说「不能打开」用户会去点别的按钮，
    // 而正确的出路是新建一个会话。
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.last().detail.isEmpty());
}

void TstSession::reentrantOpenIsRefused()
{
    ProbeSession probe;
    probe.reentrantOpen = true;

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QVERIFY(probe.open());
    QCOMPARE(probe.openCalls, 1); // 重入没有触发第二次 doOpen
    QCOMPARE(probe.state(), CompareSession::State::Open);
    // 但重入这件事必须被上报，不能静默吞掉：数据源只会被加载一次，
    // 而第二次调用的返回值是 false，调用方需要知道为什么。
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.last().message.isEmpty());
}

void TstSession::reloadRefusesWhenSessionIsNotOpen()
{
    ProbeSession probe;
    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QString error;
    QVERIFY(!probe.reload(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(probe.reloadCalls, 0);
    QCOMPARE(errors.size(), 1);
}

void TstSession::reloadRefusesWhenSessionIsDirty()
{
    ProbeSession probe;
    QVERIFY(probe.open());
    probe.setDirty(true);

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QString error;
    QVERIFY(!probe.reload(&error));
    QVERIFY(!error.isEmpty());

    // 一次都没进子类：忘了判脏的话，用户几十处编辑会在点一下「重新加载」之后
    // 无声消失，而那条路径没有任何提示。
    QCOMPARE(probe.reloadCalls, 0);
    QVERIFY(probe.isDirty());
    QCOMPARE(probe.state(), CompareSession::State::Open);
    QCOMPARE(errors.size(), 1);

    // 用户确认丢弃之后走的是显式路径：先清脏（setDirty 是公开槽），再重载。
    probe.setDirty(false);
    QVERIFY(probe.reload());
    QCOMPARE(probe.reloadCalls, 1);
}

void TstSession::reloadFailureKeepsTheSessionOpen()
{
    ProbeSession probe;
    QVERIFY(probe.open());
    probe.reloadResult = false;
    probe.openReason = QStringLiteral("磁盘暂时不可读。");

    QString error;
    QVERIFY(!probe.reload(&error));

    // 状态保持 Open：一次刷新失败不能把整个会话判死——用户手上的数据还在，
    // 界面上仍然能继续看，只是其中一侧停留在旧内容上。
    QCOMPARE(probe.state(), CompareSession::State::Open);
    QCOMPARE(error, probe.openReason);
    QCOMPARE(probe.reloadCalls, 1);
}

void TstSession::reloadClearsDirtyOnSuccess()
{
    // 探针在 doReload() 里把会话标脏，模拟「重建视图时视图自己报了改动」。
    ProbeSession probe;
    probe.markDirtyInDoReload = true;

    QVERIFY(probe.open());
    QVERIFY(probe.reload());
    // 成功之后必须回到干净：否则重载完的会话一直是脏的，
    // 用户每次点保存都在写同一份内容。
    QVERIFY(!probe.isDirty());

    // 失败那一次不清：内容确实没被重新读进来，清掉标记等于告诉用户「已经是最新的」。
    probe.reloadResult = false;
    QVERIFY(!probe.reload());
    QVERIFY(probe.isDirty());
}

void TstSession::saveRefusesWhenSessionIsNotOpen()
{
    ProbeSession probe;
    QVERIFY(!probe.canSave());

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QString error;
    QVERIFY(!probe.save(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(probe.saveCalls, 0);
    QCOMPARE(errors.size(), 1);
}

void TstSession::saveRefusesWhenNothingChanged()
{
    ProbeSession probe;
    QVERIFY(probe.open());
    QVERIFY(!probe.isDirty());
    QVERIFY(!probe.canSave());

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QString error;
    QVERIFY(!probe.save(&error));

    // 关键断言是「doSave 一次都没被调用」：把没有改动的会话重写一遍，
    // 在同步目录里会产生一串毫无意义的版本，在只读介质上则会凭空失败。
    QCOMPARE(probe.saveCalls, 0);
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.last().detail.isEmpty());
}

void TstSession::saveClearsDirtyOnSuccess()
{
    ProbeSession probe;
    QVERIFY(probe.open());

    QVector<bool> dirtySignals;
    connect(&probe, &CompareSession::dirtyChanged, this,
            [&dirtySignals](bool dirty) { dirtySignals << dirty; });

    probe.setDirty(true);
    QVERIFY(probe.canSave());
    QCOMPARE(dirtySignals.size(), 1);

    // 与状态文本同一条纪律：值没变就不发信号。容器把 dirtyChanged 直接接到
    // 标签上的「*」标记，重复发会让它在批量操作里反复重绘。
    probe.setDirty(true);
    QCOMPARE(dirtySignals.size(), 1);

    QVERIFY(probe.save());
    QCOMPARE(probe.saveCalls, 1);
    QVERIFY(!probe.isDirty());
    QCOMPARE(dirtySignals.size(), 2);
    QCOMPARE(dirtySignals.last(), false);
}

void TstSession::saveFailureKeepsTheDirtyFlag()
{
    ProbeSession probe;
    QVERIFY(probe.open());
    probe.setDirty(true);
    probe.saveResult = false;
    probe.openReason = QStringLiteral("目标文件被占用。");

    QString error;
    QVERIFY(!probe.save(&error));

    // 失败不 dirty=false：内容确实还是没被保存，把标记清掉等于告诉用户
    // 「已经存好了」，之后他关窗口时就不会再被提醒。
    QVERIFY(probe.isDirty());
    QCOMPARE(error, probe.openReason);
    QCOMPARE(probe.saveCalls, 1);
}

void TstSession::canSaveCanBeOverriddenByTheSessionType()
{
    ProbeSession probe;
    QVERIFY(probe.open());
    QVERIFY(!probe.canSave());

    probe.alwaysSavable = true;
    QVERIFY(probe.canSave());
    QVERIFY(probe.save());
    QCOMPARE(probe.saveCalls, 1);

    // 但「会话没打开就不能保存」这一条由基类判，子类覆写不掉：
    // 让每个子类自己带上状态判断的话，漏掉的那个会在未打开时被允许保存。
    ProbeSession unopened;
    unopened.alwaysSavable = true;
    QVERIFY(!unopened.canSave());

    QString error;
    QVERIFY(!unopened.save(&error));
    QCOMPARE(unopened.saveCalls, 0);
}

void TstSession::closeIsIdempotent()
{
    ProbeSession probe;
    QVERIFY(probe.open());

    probe.close();
    probe.close();

    // 幂等：界面在「关标签」与「退出程序」两条路上都会调它。
    QCOMPARE(probe.closeCalls, 1);
    QCOMPARE(probe.state(), CompareSession::State::Closed);

    probe.close();
    QCOMPARE(probe.closeCalls, 1);
}

void TstSession::closeFromFailedSessionStillReleasesResources()
{
    ProbeSession probe;
    probe.openResult = false;
    QVERIFY(!probe.open());
    QCOMPARE(probe.state(), CompareSession::State::Failed);

    probe.close();

    // 打开失败也可能已经占住一半资源（打开了其中一个文件、起了后台线程）。
    // 只对 Open 的会话调 doClose() 的话，那些资源直到进程退出才释放。
    QCOMPARE(probe.closeCalls, 1);
    QCOMPARE(probe.state(), CompareSession::State::Closed);
}

void TstSession::stateChangesAreReportedInOrder()
{
    ProbeSession probe;
    QVector<CompareSession::State> states;
    connect(&probe, &CompareSession::stateChanged, this,
            [&states](CompareSession::State state) { states << state; });

    probe.setDirty(true); // 与状态无关的动作不该产生状态信号
    QVERIFY(probe.open());
    probe.close();

    // Opening 也要发出来：加载大目录可能几秒钟，界面需要一个忙碌态。
    QCOMPARE(states.size(), 3);
    QCOMPARE(states.value(0), CompareSession::State::Opening);
    QCOMPARE(states.value(1), CompareSession::State::Open);
    QCOMPARE(states.value(2), CompareSession::State::Closed);
}

// ===========================================================================
// B 视图契约（标准第 1 条）
// ===========================================================================

void TstSession::createWidgetBuildsTheViewOnlyOnce()
{
    QWidget container;
    ProbeSession probe;

    QWidget *first = probe.createWidget(&container);
    QVERIFY(first != nullptr);
    QCOMPARE(probe.viewCalls, 1);

    QWidget *second = probe.createWidget(&container);
    QCOMPARE(second, first);
    // 关键断言：第二次没有重建。「只建一次」冻结在基类里，子类不必保证它——
    // 漏掉的子类会在第二次切换标签时把滚动位置与编辑状态全部重置。
    QCOMPARE(probe.viewCalls, 1);
    QCOMPARE(probe.widget(), first);
}

void TstSession::createWidgetPassesTheParentThrough()
{
    QWidget container;
    ProbeSession probe;

    QWidget *view = probe.createWidget(&container);
    QVERIFY(view != nullptr);
    QCOMPARE(probe.lastViewParent, &container);
    QCOMPARE(view->parentWidget(), &container);

    // 第二次带另一个父对象调用时既不重建、也不搬家：视图的父子关系属于容器，
    // 基类擅自 reparent 会把「分离窗格把一个视图挂到另一个窗口」这类用法弄坏。
    QWidget other;
    QCOMPARE(probe.createWidget(&other), view);
    QCOMPARE(view->parentWidget(), &container);
    QCOMPARE(probe.viewCalls, 1);
}

void TstSession::createWidgetReturnsNullAfterClose()
{
    QWidget container;
    ProbeSession probe;
    QVERIFY(probe.createWidget(&container) != nullptr);

    probe.close();

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    // 关闭之后再给一个视图，界面上会出现一个没有任何数据、也无法重新加载的空窗格；
    // 返回 nullptr 让调用方能就地处理（不创建标签），比给一个空壳好。
    QVERIFY(probe.createWidget(&container) == nullptr);
    QCOMPARE(probe.viewCalls, 1);
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.last().detail.isEmpty());
}

void TstSession::createWidgetReportsWhenTheViewIsMissing()
{
    QWidget container;
    ProbeSession probe;
    probe.viewResult = false;

    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    QVERIFY(probe.createWidget(&container) == nullptr);
    QCOMPARE(probe.viewCalls, 1);
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.last().message.isEmpty());
    // detail 要指出责任方：「会话类型应当总是返回一个控件」，
    // 否则下一个人只会看到「视图没建出来」而不知道该去改哪里。
    QVERIFY(!errors.last().detail.isEmpty());

    // 失败的这一次没有被缓存，再调一次会重建（计数变 2）——
    // 与「已建好就不再建」正好相反，两条行为在这里被区分开。
    QVERIFY(probe.createWidget(&container) == nullptr);
    QCOMPARE(probe.viewCalls, 2);
}

void TstSession::widgetFollowsTheContainersLifetime()
{
    auto *container = new QWidget;
    ProbeSession probe;
    QWidget *view = probe.createWidget(container);
    QVERIFY(view != nullptr);
    QCOMPARE(probe.widget(), view);

    delete container; // 容器删标签时会连带析构视图

    // 基类存的是 QPointer。若换成裸指针，这里仍然返回一个已析构的对象，
    // 下一次 createWidget() 会把悬空指针交给界面——崩溃点与真正的错误毫不相干。
    QVERIFY(probe.widget() == nullptr);

    QWidget *rebuilt = probe.createWidget(nullptr);
    QVERIFY(rebuilt != nullptr);
    QCOMPARE(probe.viewCalls, 2);
    QCOMPARE(probe.widget(), rebuilt);
    delete rebuilt; // 没有父对象，由测试自己收尾
}

// ===========================================================================
// C 三个公共出口（标准第 3 条）
// ===========================================================================

void TstSession::statusTextIsReportedOnlyWhenItChanges()
{
    ProbeSession probe;
    QVector<QString> texts;
    connect(&probe, &CompareSession::statusTextChanged, this,
            [&texts](const QString &text) { texts << text; });

    QVERIFY(probe.statusText().isEmpty());

    probe.setStatusText(QStringLiteral("Loaded a.txt and b.txt."));
    probe.setStatusText(QStringLiteral("Loaded a.txt and b.txt."));
    probe.setStatusText(QStringLiteral("2 differences."));

    // 状态栏刷新常见的写法是「按当前文件重写一遍文字」，每次都发信号的话
    // 状态栏会在批量过程中反复重排，界面看起来在抖。
    QCOMPARE(texts.size(), 2);
    QCOMPARE(texts.value(1), QStringLiteral("2 differences."));
}

void TstSession::statusTextIsReadableBeforeAnySignal()
{
    ProbeSession probe;
    QVERIFY(probe.statusText().isEmpty());
    QVERIFY(probe.progress().what.isEmpty());

    probe.setStatusText(QStringLiteral("Ready."));

    // 容器可能在会话上报之后才建立连接（会话是别的对象创建的）。
    // 能直接读一次最近的状态，就不会漏掉唯一的那条文本。
    QCOMPARE(probe.statusText(), QStringLiteral("Ready."));
}

void TstSession::statusSeverityDefaultsToNormalWhenOnlyTextIsSet()
{
    ProbeSession probe;
    QCOMPARE(probe.statusSeverity(), CompareSession::StatusSeverity::Normal);

    // 老写法（只传一句文本）必须继续把严重度压回 `Normal`，而不是保留上一次的
    // `Warning`。否则「上一次是行尾混合、这一次只是一句普通提示」时警告图标会一直亮着
    // ——图标与它旁边那句话对不上，用户只能学会忽略图标。
    probe.setStatusText(QStringLiteral("Mixed endings."), CompareSession::StatusSeverity::Warning);
    QCOMPARE(probe.statusSeverity(), CompareSession::StatusSeverity::Warning);
    probe.setStatusText(QStringLiteral("All good."));
    QCOMPARE(probe.statusSeverity(), CompareSession::StatusSeverity::Normal);
}

void TstSession::statusSeverityIsASeparateChannelFromTheText()
{
    ProbeSession probe;
    QVector<QString> texts;
    QVector<CompareSession::StatusSeverity> severities;
    connect(&probe, &CompareSession::statusTextChanged, this,
            [&texts](const QString &text) { texts << text; });
    connect(&probe, &CompareSession::statusSeverityChanged, this,
            [&severities](CompareSession::StatusSeverity severity) { severities << severity; });

    const QString text = QStringLiteral("1 difference block(s) • Left: UTF-8, Mixed (LF 1 / CRLF 1)");
    probe.setStatusText(text, CompareSession::StatusSeverity::Warning);
    QCOMPARE(texts.size(), 1);
    QCOMPARE(severities.size(), 1);

    // 同一句话、同一档严重度：两条通道都不该发（状态栏刷新是高频路径）。
    probe.setStatusText(text, CompareSession::StatusSeverity::Warning);
    QCOMPARE(texts.size(), 1);
    QCOMPARE(severities.size(), 1);

    // **本轮的关键一条**：文本一字不差、只有严重度变。
    //
    // 这条为什么非有不可——`setStatusText()` 原本是「文本没变就 return」，
    // 而严重度是写在同一句里的（行尾是 `LF` 还是 `Mixed`），于是一个看起来
    // 完全合理的早退会把严重度的变化一起吞掉：状态栏的警告图标会卡在上一次的值上，
    // 而且**没有任何东西会红**——文本那条断言照旧通过。
    probe.setStatusText(text, CompareSession::StatusSeverity::Normal);
    QCOMPARE(texts.size(), 1);
    QCOMPARE(severities.size(), 2);
    QCOMPARE(severities.last(), CompareSession::StatusSeverity::Normal);
    QCOMPARE(probe.statusText(), text);

    // 反过来：只换文本、严重度不动，则只发文本那条。
    probe.setStatusText(QStringLiteral("0 difference block(s)"), CompareSession::StatusSeverity::Normal);
    QCOMPARE(texts.size(), 2);
    QCOMPARE(severities.size(), 2);

    // 读回的值也必须跟着走——只在切标签时读一次 `statusSeverity()` 的容器
    // （`SessionArea` 就是这么用的）拿到的应当是当前这一档。
    QCOMPARE(probe.statusSeverity(), CompareSession::StatusSeverity::Normal);
    probe.setStatusText(QStringLiteral("Mixed again."), CompareSession::StatusSeverity::Warning);
    QCOMPARE(probe.statusSeverity(), CompareSession::StatusSeverity::Warning);
}

void TstSession::errorReportCarriesMessageAndDetail()
{
    ProbeSession probe;
    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    probe.reportError(QStringLiteral("Failed to write."),
                      QStringLiteral("0x20 ERROR_SHARING_VIOLATION"));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.last().message, QStringLiteral("Failed to write."));
    QCOMPARE(errors.last().detail, QStringLiteral("0x20 ERROR_SHARING_VIOLATION"));
    QVERIFY(!errors.last().isEmpty());

    // 没有诊断信息时 detail 是空串，而不是「详情：」后面跟一片空白——
    // 界面据此决定整段不显示，与 PLAT-008 的 errorDetail() 同一条约定。
    probe.reportError(QStringLiteral("The folder is empty."));
    QCOMPARE(errors.size(), 2);
    QVERIFY(errors.last().detail.isEmpty());
    QVERIFY(!errors.last().isEmpty());

    // 三个载体在会话构造时注册进元类型系统。忘了注册时 QObject::connect 只在
    // 运行期抱怨一句，现象是「进度条一直不动」——而不是一眼能看出的编译错误。
    QVERIFY(QMetaType::type("LqCompare::SessionError") != QMetaType::UnknownType);
    QVERIFY(QMetaType::type("LqCompare::SessionProgress") != QMetaType::UnknownType);
    QVERIFY(QMetaType::type("LqCompare::CompareSession::State") != QMetaType::UnknownType);
}

void TstSession::repeatedErrorsAreNotDeduplicated()
{
    ProbeSession probe;
    QVector<SessionError> errors;
    connect(&probe, &CompareSession::errorReported, this,
            [&errors](const SessionError &error) { errors << error; });

    probe.reportError(QStringLiteral("Same reason."));
    probe.reportError(QStringLiteral("Same reason."));

    // 错误是**事件**而不是状态：同一个原因连续失败两次时，上层要能知道
    // 「它又试了一次并再次失败」，而不是以为信号漏了一个。
    QCOMPARE(errors.size(), 2);
}

void TstSession::progressIsReportedOnlyWhenItChanges()
{
    ProbeSession probe;
    QVector<SessionProgress> updates;
    connect(&probe, &CompareSession::progressChanged, this,
            [&updates](const SessionProgress &progress) { updates << progress; });

    probe.reportProgress(1, 10, QStringLiteral("Scanning"));
    probe.reportProgress(1, 10, QStringLiteral("Scanning"));
    probe.reportProgress(2, 10, QStringLiteral("Scanning"));
    probe.reportProgress(2, 10, QStringLiteral("Comparing"));

    // 逐项循环里同一个「N / M」会被反复上报，不去重会刷爆事件队列
    // （读者未必在跑事件循环，队列会一直涨）。
    QCOMPARE(updates.size(), 3);
    QCOMPARE(probe.progress().current, 2);
    QCOMPARE(probe.progress().total, 10);
    QCOMPARE(probe.progress().what, QStringLiteral("Comparing"));
    QVERIFY(probe.progress().isActive());
}

void TstSession::progressPercentIsClampedWhileRawValuesStayHonest()
{
    ProbeSession probe;

    probe.reportProgress(1, 3);
    QCOMPARE(probe.progress().percent(), 33);

    probe.reportProgress(3, 3);
    QCOMPARE(probe.progress().percent(), 100);

    // total 已经告诉界面「一共 2 项」，而子类报了 3——这是子类的计数错误。
    // 百分比夹紧到 100（否则界面上出现 150%，用户只会认为程序坏了），
    // 但原始数字必须原样留着，否则这个错误永远查不出来。
    probe.reportProgress(3, 2);
    QCOMPARE(probe.progress().current, 3);
    QCOMPARE(probe.progress().total, 2);
    QCOMPARE(probe.progress().percent(), 100);

    probe.reportProgress(-5, 10);
    QCOMPARE(probe.progress().current, -5);
    QCOMPARE(probe.progress().percent(), 0);
}

void TstSession::unknownTotalHasNoPercent()
{
    ProbeSession probe;

    // 扫描目录时还不知道有多少项，total 传 0。这时没有百分比可算，
    // 给 0% 会让进度条停在起点，用户以为卡住了；-1 让界面改画忙碌条。
    probe.reportProgress(5, 0, QStringLiteral("Scanning"));
    QCOMPARE(probe.progress().percent(), -1);
    QVERIFY(!probe.progress().isActive());
    QCOMPARE(probe.progress().current, 5);

    probe.reportProgress(5, -1);
    QCOMPARE(probe.progress().percent(), -1);
    QVERIFY(!probe.progress().isActive());
}

// ===========================================================================
// D 设置接口（标准第 1 条）
// ===========================================================================

void TstSession::everySessionHasASettingsStore()
{
    ProbeSession probe;
    MinimalSession minimal;

    // 默认实现返回 nullptr 的话，每个调用点都要判空，而「判空之后什么都不做」
    // 正是最难发现的一类缺陷：设置看起来保存了、其实丢了。
    QVERIFY(probe.sessionSettings() != nullptr);
    QVERIFY(minimal.sessionSettings() != nullptr);

    probe.sessionSettings()->setValue(QStringLiteral("ignore.trailing"), true);
    QCOMPARE(probe.sessionSettings()->value(QStringLiteral("ignore.trailing")).toBool(), true);
}

void TstSession::settingsStoreIsCreatedLazilyAndReused()
{
    ProbeSession probe;

    // 刻意不在构造函数里创建：构造函数里调 createSettings() 只会得到基类版本
    // （C++ 里虚函数在构造期间不派发），子类的覆写永远不生效，
    // 而现象是「设置改了不生效」这种要查很久的问题。
    QCOMPARE(probe.settingsFactoryCalls, 0);

    SessionSettings *first = probe.sessionSettings();
    SessionSettings *second = probe.sessionSettings();
    QCOMPARE(first, second);
    QCOMPARE(probe.settingsFactoryCalls, 1);
}

void TstSession::sessionCanReplaceItsSettingsStore()
{
    ProbeSession probe;
    probe.customSettings = true;

    SessionSettings *store = probe.sessionSettings();
    QCOMPARE(probe.settingsFactoryCalls, 1);
    // 覆写真的生效——SESS-006 会用同一条路把内存实现换成带作用域链与落盘的那个。
    QVERIFY(qobject_cast<ProbeSettings *>(store) != nullptr);
    QVERIFY(store->keys().isEmpty());
}

void TstSession::memorySettingsRoundTripAndFallback()
{
    MemorySessionSettings settings;
    QCOMPARE(settings.keys().size(), 0);
    QVERIFY(!settings.contains(QStringLiteral("indent")));

    // 键不存在时返回调用方给的回退值，而不是空 QVariant：
    // 「这个键没被设置过」与「这个键被设成了一个无效值」在界面上是两回事。
    QCOMPARE(settings.value(QStringLiteral("indent"), 4).toInt(), 4);
    QVERIFY(settings.value(QStringLiteral("indent")).isNull());

    QVERIFY(settings.setValue(QStringLiteral("indent"), 4));
    QVERIFY(settings.contains(QStringLiteral("indent")));
    QCOMPARE(settings.value(QStringLiteral("indent"), 99).toInt(), 4);

    QCOMPARE(settings.keys(), QStringList{QStringLiteral("indent")});
}

void TstSession::memorySettingsRejectsBlankKey()
{
    MemorySessionSettings settings;

    QVERIFY(!settings.setValue(QString(), 1));
    QVERIFY(!settings.contains(QString()));
    QVERIFY(!settings.remove(QString()));
    QCOMPARE(settings.value(QString(), 7).toInt(), 7);
    QCOMPARE(settings.keys().size(), 0);

    // contains 与 setValue 对空键的判断必须一致，否则会出现
    // 「contains 说没有、value 却能取到一个值」这种自相矛盾的状态——
    // 而空键在作用域链里本来就无法与「没设置」区分，所以只能整条拒掉。
}

void TstSession::memorySettingsIgnoresUnchangedValue()
{
    MemorySessionSettings settings;
    QVector<QString> changed;
    connect(&settings, &SessionSettings::changed, this,
            [&changed](const QString &key) { changed << key; });

    QVERIFY(settings.setValue(QStringLiteral("k"), 1));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.value(0), QStringLiteral("k"));

    // 写入同一个值：操作成功，但**不算改动**。界面把 changed 直接连到
    // 「会话变脏」，用户点开设置看一眼再确定关掉不该让会话变脏。
    QVERIFY(settings.setValue(QStringLiteral("k"), 1));
    QCOMPARE(changed.size(), 1);

    QVERIFY(settings.setValue(QStringLiteral("k"), 2));
    QCOMPARE(changed.size(), 2);
}

void TstSession::memorySettingsRemoveAndClear()
{
    MemorySessionSettings settings;
    QVector<QString> changed;
    connect(&settings, &SessionSettings::changed, this,
            [&changed](const QString &key) { changed << key; });

    // 删一个不存在的键不算改动——空转不是改动，与其他几个入口同一条纪律。
    QVERIFY(!settings.remove(QStringLiteral("missing")));
    QCOMPARE(changed.size(), 0);

    settings.setValue(QStringLiteral("a"), 1);
    settings.setValue(QStringLiteral("b"), 2);
    QCOMPARE(settings.keys().size(), 2);
    QCOMPARE(changed.size(), 2);

    settings.clear();
    QVERIFY(settings.keys().isEmpty());
    QCOMPARE(changed.size(), 3);
    // 空键承载「全部变了」：逐项发 N 次通知没有意义，接收方真正需要知道的
    // 就是「别只看某一项了，全部重读」。
    QCOMPARE(changed.last(), QString());

    settings.clear(); // 空转不算改动
    QCOMPARE(changed.size(), 3);

    QVERIFY(!settings.remove(QStringLiteral("a")));
    QCOMPARE(changed.size(), 3);
}

// ===========================================================================
// E 可扩展性（标准第 2 条）
// ===========================================================================

void TstSession::aNewTypeNeedsOnlyTheBaseContract()
{
    // MinimalSession 是「将来新增的一种会话类型」的替身：它只写了 createView()
    // 一个实现点，没有 doOpen / doReload / doSave / doClose / canSaveNow / createSettings。
    MinimalSession session;
    QCOMPARE(session.typeId(), QStringLiteral("minimal"));
    QCOMPARE(session.state(), CompareSession::State::Created);

    QVERIFY(session.open());
    QCOMPARE(session.state(), CompareSession::State::Open);
    QVERIFY(!session.isDirty());

    QWidget container;
    QVERIFY(session.createWidget(&container) != nullptr);
    QCOMPARE(session.viewCalls, 1);

    session.setDirty(true);
    QVERIFY(session.canSave());
    QVERIFY(session.save());
    QVERIFY(!session.isDirty());

    QVERIFY(session.reload());
    session.close();
    QCOMPARE(session.state(), CompareSession::State::Closed);
    QVERIFY(session.createWidget(&container) == nullptr);

    // 注意：本条只覆盖「实现基类契约」这半句。契约里那半句「并注册」
    // 由 G 组（类型注册表 SESS-002）接着做——见 issue #36 的落地说明。
}

void TstSession::differentTypesShareTheSameBaseBehaviour()
{
    ProbeSession probe;
    MinimalSession minimal;
    QVERIFY(probe.typeId() != minimal.typeId());

    // 两个互不相干的类型走同一套基类行为：未打开时都拒绝保存、都拒绝重载，
    // 并且都上报了非空原因。共享行为集中在基类，新增类型不会各自长出变体。
    const QVector<CompareSession *> sessions{&probe, &minimal};
    for (CompareSession *session : sessions) {
        QVector<SessionError> errors;
        connect(session, &CompareSession::errorReported, this,
                [&errors](const SessionError &error) { errors << error; });

        QVERIFY(!session->canSave());

        QString error;
        QVERIFY(!session->save(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(errors.size(), 1);
        QVERIFY(!errors.last().message.isEmpty());

        QVERIFY(!session->reload(&error));
        QCOMPARE(errors.size(), 2);
        QCOMPARE(session->state(), CompareSession::State::Created);
    }
}

// ===========================================================================
// F 源码级护栏（标准第 4 条）
// ===========================================================================

void TstSession::baseModuleIncludesStayInTheirOwnModules()
{
    const QStringList sources{QStringLiteral("/Views/Session/comparesession.h"),
                              QStringLiteral("/Views/Session/comparesession.cpp")};

    for (const QString &relativePath : sources) {
        const QString source = readSourceFile(relativePath);
        QVERIFY2(!source.isEmpty(), qPrintable(QStringLiteral("读不到 ") + relativePath));

        const QStringList foreign = foreignQuotedIncludes(source);
        QVERIFY2(foreign.isEmpty(),
                 qPrintable(QStringLiteral("%1 依赖了本模块之外的头文件：%2")
                                .arg(relativePath, foreign.join(QStringLiteral(", ")))));
    }
}

void TstSession::theIncludeGuardWouldCatchAConcreteViewInclude()
{
    // 反向验证：把护栏喂给一段**故意写坏**的源码，它必须报出来。
    // 一个从不报错的护栏比没有护栏更糟——它会让人以为这块已经被守住了。
    const QString broken = QStringLiteral("#include \"comparesession.h\"\n"
                                          "#include <QWidget>\n"
                                          "#include \"session.h\"\n"
                                          "#include \"homepage.h\"\n"
                                          "#include \"sessionarea.h\"\n");
    const QStringList foreign = foreignQuotedIncludes(broken);
    QCOMPARE(foreign.size(), 2);
    QVERIFY(foreign.contains(QStringLiteral("homepage.h")));
    QVERIFY(foreign.contains(QStringLiteral("sessionarea.h")));

    // 干净的一段必须一条都不报：会误报的护栏迟早被人关掉。
    // 尖括号头（Qt 的）不参与判断——它们由 Qt 自己解析，不属于「具体视图」。
    const QString clean = QStringLiteral("#include \"comparesession.h\"\n"
                                        "#include <QPointer>\n"
                                        "#include <QWidget>\n"
                                        "#include \"session.h\"\n");
    QVERIFY(foreignQuotedIncludes(clean).isEmpty());
}

// ===========================================================================
// G 与类型注册表的衔接（标准第 2 条里「并注册」那半句）
//
// E 组证明了「新增一种会话类型只需实现基类契约」；这里接着证明后半句
// **「并注册」**：把一个只实现基类契约的类型登记进注册表，就能按类型 ID 造出
// 真的会话，并把它走完整个生命周期。两组合起来才是 SESS-001 第 2 条的完整含义。
//
// 这一步要等 SESS-002 的类型注册表落地（本组就是它落地后补上的）。
// 注册表在服务层、会话基类在视图层，因此**只有本套件能同时看到两者**——
// 它链接 QtWidgets，而 Tests/SessionType 刻意只链接 QtCore。
// ===========================================================================

namespace {

/// 造一条指向 `MinimalSession` 的描述子。
///
/// 描述子的 ID 与 MinimalSession 自己报的 typeId() 都是 `minimal`——这不是巧合
/// 而是契约：会话文件的入口是「类型 ID → 工厂 → 会话」，若两处 ID 不一致，
/// 造出来的会话就会在保存时被记成另一个类型，而界面上看不出任何异常。
SessionType minimalTypeDescriptor()
{
    SessionType type;
    type.id = QStringLiteral("minimal");
    type.displayName = QStringLiteral("最小会话（测试替身）");
    type.englishName = QStringLiteral("Minimal Session");
    type.summary = QStringLiteral("只实现 createView() 的会话类型。");
    return type;
}

LqCompare::SessionFactory minimalFactory()
{
    return [](QObject *parent) -> CompareSession * { return new MinimalSession(parent); };
}

} // namespace

void TstSession::aTypeRegisteredWithAFactoryProducesARealSession()
{
    SessionTypeRegistry registry;
    QString error;
    QVERIFY2(registry.add(minimalTypeDescriptor(), minimalFactory(), &error), qPrintable(error));

    const SessionTypeEntry *entry = registry.find(QStringLiteral("minimal"));
    QVERIFY(entry != nullptr);
    QVERIFY(entry->hasFactory());

    std::unique_ptr<CompareSession> session(entry->factory(nullptr));
    QVERIFY(session != nullptr);
    QCOMPARE(session->typeId(), QStringLiteral("minimal"));
    QCOMPARE(session->state(), CompareSession::State::Created);
}

void TstSession::aSessionCreatedThroughTheRegistryRunsItsWholeLifecycle()
{
    SessionTypeRegistry registry;
    registry.add(minimalTypeDescriptor(), minimalFactory());
    const SessionTypeEntry *entry = registry.find(QStringLiteral("minimal"));
    QVERIFY(entry != nullptr);

    // 真正的重点在这里：这个会话**不是**在用例里直接 new 出来的，而是从注册表
    // 拿工厂造出来的。也就是说「实现基类契约 + 注册」这一条路真的能通到
    // 「打开 → 拿视图 → 标脏 → 保存 → 重载 → 关闭」。
    std::unique_ptr<CompareSession> session(entry->factory(nullptr));
    QVERIFY(session != nullptr);

    QVERIFY(session->open());
    QCOMPARE(session->state(), CompareSession::State::Open);

    QWidget container;
    QWidget *view = session->createWidget(&container);
    QVERIFY(view != nullptr);
    QCOMPARE(view->parentWidget(), &container);

    session->setDirty(true);
    QVERIFY(session->canSave());
    QVERIFY(session->save());
    QVERIFY(!session->isDirty());

    QVERIFY(session->reload());
    QCOMPARE(session->state(), CompareSession::State::Open);

    session->close();
    QCOMPARE(session->state(), CompareSession::State::Closed);
    QVERIFY(session->createWidget(&container) == nullptr);
}

void TstSession::theCreatedSessionAgreesWithItsRegistryEntry()
{
    SessionTypeRegistry registry;
    registry.add(minimalTypeDescriptor(), minimalFactory());
    registry.addBuiltInTypes();

    const SessionTypeEntry *entry = registry.find(QStringLiteral("minimal"));
    QVERIFY(entry != nullptr);

    std::unique_ptr<CompareSession> session(entry->factory(nullptr));
    QVERIFY(session != nullptr);

    // 「会话自己报的类型 ID」必须能在注册表里查回同一个条目。
    // 这是「按类型 ID 造会话」能成立的前提：会话文件只存 ID，加载时靠它反查。
    // 两处 ID 一旦分家，现象是「双击会话没反应」——最难归因的一类故障。
    const SessionTypeEntry *lookedUp = registry.find(session->typeId());
    QVERIFY(lookedUp != nullptr);
    QCOMPARE(lookedUp->type.id, entry->type.id);
    QCOMPARE(lookedUp->type.englishName, QStringLiteral("Minimal Session"));

    // 内置的 14 种类型此刻**还没有实现**，因此它们查得到、但造不出来。
    // 「类型已登记」与「这一版还没有这个视图」是两件事，注册表要能把它们分开。
    const SessionTypeEntry *hex = registry.find(QStringLiteral("hex"));
    QVERIFY(hex != nullptr);
    QVERIFY(!hex->hasFactory());
    QVERIFY(hex->isAvailableHere());
}

void TstSession::typesWithoutAFactoryCannotBeCreated()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    // 内置表只登记描述子，工厂由各会话类型的实现方给。因此「按 ID 造会话」
    // 对它们应当返回空，而不是造出一个没有视图、打开就崩的东西。
    for (const SessionTypeEntry *entry : registry.entries()) {
        QVERIFY2(!entry->hasFactory(), qPrintable(entry->type.id));
    }

    // 反面：给了工厂的那一条是能造的。不写这一句的话，上面那个循环
    // 在一个「工厂永远为空」的实现下也会通过。
    SessionTypeRegistry withFactory;
    QVERIFY(withFactory.add(minimalTypeDescriptor(), minimalFactory()));
    const SessionTypeEntry *entry = withFactory.find(QStringLiteral("minimal"));
    QVERIFY(entry != nullptr);
    QVERIFY(entry->hasFactory());
    std::unique_ptr<CompareSession> session(entry->factory(nullptr));
    QVERIFY(session != nullptr);
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
QTEST_MAIN(TstSession)
