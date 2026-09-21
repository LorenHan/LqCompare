#ifndef LQCOMPARE_COMPARESESSION_H
#define LQCOMPARE_COMPARESESSION_H

#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "session.h" // Services/Session —— 设置接口。Views -> Services 是允许的方向。

namespace LqCompare {

///
/// \brief 会话错误上报的载体（三个公共出口之一）。
///
/// **先成结构、再成文本**，与 `Log::Record` 同一个理由：状态栏、错误对话框、
/// 输出面板对同一次失败需要不同的呈现（一行摘要 / 带详情 / 带原始错误码），
/// 而只给一行拼好的字符串的话，它们只能去**反向解析**那段文字——
/// 任何一次文案调整都会静默打断其中一处。
///
struct SessionError
{
    /// 面向用户的一句话，直接显示。
    QString message;
    /// 诊断信息：原始错误码、路径、期望值。可为空——为空时界面不显示这一段，
    /// 而不是显示「详情：」加一片空白（理由同 PLAT-008 的 `errorDetail()`）。
    QString detail;

    bool isEmpty() const { return message.isEmpty() && detail.isEmpty(); }
};

///
/// \brief 进度上报的载体（三个公共出口之一）。
///
/// `current` / `total` **原样保留**，不在这里夹紧：界面上显示 120% 是
/// 「程序坏了」的观感，但把 12/10 悄悄写成 10/10 会让一个真实的计数错误
/// 永远查不出来（与 PLAT-004 的 `actualPixelSize` 同一条思路——
/// 如实报出，需要好看的数字由显示方自己处理）。
///
struct SessionProgress
{
    int current = 0;
    /// 0 或负数表示「总量未知」（例如扫描目录时还不知道有多少项）。
    int total = 0;
    /// 正在进行什么，可为空。
    QString what;

    /// 显示用的百分比，已夹紧到 0..100；总量未知时返回 -1（显示方据此改画忙碌条）。
    int percent() const;

    /// 是否处于「有进度可显示」的状态。
    bool isActive() const { return total > 0; }
};

///
/// \brief 会话抽象基类（PRD: SESS-001）。
///
/// 每个会话类型（文本比对、文件夹比对、三方合并……）都继承本类，只实现
/// 「怎么建视图」与「打开/重新加载/保存时做什么」，其余由基类统一负责。
///
/// **为什么把「只建一次视图」冻结在基类里**：会话容器在切换标签、恢复窗口、
/// 拖到另一个显示器时都会多次索取视图。让每个子类各自保证「只建一次」的话，
/// 漏掉的那个会在第二次切换时把界面重建一遍——滚动位置、展开的节点、
/// 正在编辑的文本全部回到初始状态，而这类缺陷只在特定操作序列下出现。
/// 基类写成模板方法（`createWidget()` 公开、`createView()` 由子类实现），
/// 子类的代码根本不在「重复创建」那条路径上。
///
/// 同理，`open()` / `reload()` / `save()` 的状态迁移、脏标记与错误上报也都在基类，
/// 子类只实现 `doOpen()` / `doReload()` / `doSave()`。三个公共出口
/// （状态栏文本、错误上报、进度上报）也由基类统一提供，避免每个会话类型
/// 各自发明一套——那样状态栏就得知道自己在跟谁说话。
///
/// **本类不得包含任何具体视图类型的判断，也不得 include 任何具体视图头文件**
/// （SESS-001 第 4 条）。这条由 `Tests/Session` 做两道校验：
/// 一是该测试工程只把 `Views/Session` 与 `Services/Session` 放进 INCLUDEPATH，
/// 因此本头文件里出现 `#include "homepage.h"` 会**编译失败**；
/// 二是源码级用例逐个检查本模块里的 `#include "…"` 是否都落在自己的模块内。
///
class CompareSession : public QObject
{
    Q_OBJECT

public:
    /// 会话状态。`Failed` 与 `Open` 都是「一次尝试的终态」——
    /// 打开失败后停在 `Failed`，而不是退回 `Created`，否则界面无法区分
    /// 「还没打开」与「打开过但失败了」（前者的提示是「请选择文件」，后者是「上次失败的原因」）。
    enum class State {
        Created, ///< 已构造，尚未打开
        Opening, ///< open() 执行中，供界面显示忙碌态
        Open,    ///< 已打开
        Failed,  ///< 打开失败
        Closed,  ///< 已关闭；关闭之后不可再打开
    };
    Q_ENUM(State)

    ///
    /// \brief 状态栏文本的严重程度。
    ///
    /// **为什么要另开一条通道，而不是把「警告」写进文本里**：`TXT-010` 第 4 条
    /// 要求「行尾混合时给出**警告图标**」——图标不是文字，容器无法从文本里
    /// 可靠地推出来。若让 `MainWindow` 去嗅 `statusText()` 里有没有某个词，
    /// 就是**反向解析自己刚拼好的那句话**（SESS-001 的注释里已经写过这条禁忌）：
    /// 任何一次文案调整或翻译都会静默让图标失灵，而失败的样子是「图标不见了」，
    /// 没有任何东西会红。严重程度与文本因此**分开上报、各自去重**。
    ///
    /// 取值刻意只有两档：需求里没有「错误/致命」这一级（未打开、失败由
    /// `reportError()` 走另一条出口），多留一档就会有人随手用错。
    ///
    enum class StatusSeverity {
        Normal,  ///< 普通信息，状态栏不额外显示任何图标
        Warning, ///< 需要用户注意（例如两侧行尾混合），状态栏显示警告图标
    };
    Q_ENUM(StatusSeverity)

    explicit CompareSession(QString typeId, QObject *parent = nullptr);
    ~CompareSession() override;

    /// 会话类型 ID，取值见 SESS-002 的类型注册表。一经发布不可改名。
    QString typeId() const { return m_typeId; }

    /// 会话标题（标签页与标题栏用）。空标题由界面自己生成「未命名 N」。
    QString title() const { return m_title; }
    void setTitle(const QString &title);

    // -----------------------------------------------------------------------
    // 契约（SESS-001 第 1 条）
    // -----------------------------------------------------------------------

    /// 取本会话的视图，首次调用时创建，之后返回同一个对象。
    ///
    /// 会话已关闭时返回 `nullptr` 并上报错误：关闭之后再给一个视图，
    /// 界面上会出现一个没有任何数据、也无法重新加载的空窗格。
    QWidget *createWidget(QWidget *parent = nullptr);

    /// 已创建的视图；尚未创建、或视图已被容器析构时为 `nullptr`。
    /// 后一种情况靠 `QPointer` 自动失效——容器删标签时会连带删掉视图，
    /// 若这里存的是裸指针，下一次 `createWidget()` 会返回一个已析构的对象。
    QWidget *widget() const { return m_widget; }

    /// 打开会话。失败时返回 false，`error` 非空则写入原因，并通过错误出口上报。
    bool open(QString *error = nullptr);

    /// 重新加载两侧数据源。只在已打开且**没有未保存改动**的会话上执行。
    bool reload(QString *error = nullptr);

    /// 保存。只在 `canSave()` 为真的会话上执行。
    bool save(QString *error = nullptr);

    /// 关闭会话。可重复调用（幂等）。
    void close();

    State state() const { return m_state; }

    /// 是否有未保存的改动。
    bool isDirty() const { return m_dirty; }

    /// 现在能不能保存。默认实现是 `canSaveNow()`，即「已打开 + 有改动」。
    bool canSave() const;

    /// 本会话的设置存储。首次调用时构造，之后返回同一个实例。
    /// 子类可通过覆写 `createSettings()` 换掉实现（SESS-006 会这么做）。
    SessionSettings *sessionSettings();

public slots:
    /// 显式设置脏标记。界面在「用户已确认丢弃改动」之后需要把它清掉
    /// （例如确认过再重新加载），因此它必须是可调用的，而不是只能由子类内部设置。
    void setDirty(bool dirty);

signals:
    void titleChanged(const QString &title);
    void stateChanged(LqCompare::CompareSession::State state);
    void dirtyChanged(bool dirty);

    /// 公共出口 1：状态栏文本。会话不直接碰状态栏，由容器转接。
    void statusTextChanged(const QString &text);
    /// 公共出口 1 的伴随通道：状态文本的严重程度变化（图标由容器决定怎么画）。
    void statusSeverityChanged(LqCompare::CompareSession::StatusSeverity severity);
    /// 公共出口 2：错误上报。
    void errorReported(const LqCompare::SessionError &error);
    /// 公共出口 3：进度上报。
    void progressChanged(const LqCompare::SessionProgress &progress);

public:
    // -----------------------------------------------------------------------
    // 三个公共出口
    //
    // 做成 public 而不是 protected：容器与命令行也会往同一个出口推
    // （「正在打开命令行传入的文件……」），而让它们各自 emit 信号是不可能的——
    // 信号只能由本类发出。入口保持一个，状态栏就不必判断消息是谁发的。
    // -----------------------------------------------------------------------
    void setStatusText(const QString &text, StatusSeverity severity = StatusSeverity::Normal);
    void reportError(const QString &message, const QString &detail = QString());
    void reportProgress(int current, int total, const QString &what = QString());

    /// 最近一次上报的状态栏文本。容器在挂上信号之前可以先读一次，
    /// 避免「连接建立得比第一次上报晚」而漏掉唯一的那条状态。
    QString statusText() const { return m_statusText; }
    /// 最近一次上报的严重程度。缺省是 `Normal`——没上报过就说明还没有警告。
    StatusSeverity statusSeverity() const { return m_statusSeverity; }
    /// 最近一次上报的进度。
    SessionProgress progress() const { return m_progress; }

protected:
    // -----------------------------------------------------------------------
    // 子类实现点
    // -----------------------------------------------------------------------

    /// 构建本会话的视图。**由基类保证只调用一次**（见类注释），
    /// 因此子类不必在这里做缓存，也不要在这里读 `widget()`。
    virtual QWidget *createView(QWidget *parent) = 0;

    /// 打开数据源。返回 false 表示失败，子类应把原因写进 `error`（可为空指针）。
    virtual bool doOpen(QString *error);
    /// 重新加载。默认实现是再打开一次；数据源本身没变的会话可以覆写成空操作。
    virtual bool doReload(QString *error);
    /// 保存。默认实现返回 true（无内容的会话没有可保存的东西）。
    virtual bool doSave(QString *error);
    /// 释放数据源。默认什么也不做。视图的析构不在这里——见 `.cpp` 里的说明。
    virtual void doClose();

    /// 现在存得下去吗。默认实现只回答「有改动没有」——「会话已打开」这一条由
    /// 基类的 `canSave()` 另行判定，子类不必重复它（重复的话，漏掉的那一处
    /// 会在未打开时被允许保存）。只读会话、数据源缺失的会话覆写它。
    virtual bool canSaveNow() const;

    /// 构造本会话的设置存储，默认是内存实现。**在首次取用 `sessionSettings()` 时调用**，
    /// 不是在构造函数里——构造函数里调虚函数只会得到基类版本（C++ 的经典陷阱），
    /// 于是子类的覆写永远不生效，而现象是「设置改了不生效」。
    virtual SessionSettings *createSettings();

private:
    void setState(State state);
    /// 把一次失败的原因整理成错误文本：`*error` 里已有子类给的原因就沿用，
    /// 否则给一句兜底的话——空消息的 error 信号在界面上是一个没有内容的对话框。
    QString failureMessage(const QString &subclassReason, const char *action) const;

    QString m_typeId;
    QString m_title;
    State m_state = State::Created;
    bool m_dirty = false;
    QPointer<QWidget> m_widget;
    SessionSettings *m_settings = nullptr;
    QString m_statusText;
    StatusSeverity m_statusSeverity = StatusSeverity::Normal;
    SessionProgress m_progress;
};

} // namespace LqCompare

Q_DECLARE_METATYPE(LqCompare::SessionError)
Q_DECLARE_METATYPE(LqCompare::SessionProgress)

#endif // LQCOMPARE_COMPARESESSION_H
