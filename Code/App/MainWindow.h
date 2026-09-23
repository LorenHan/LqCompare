#ifndef LQCOMPARE_MAINWINDOW_H
#define LQCOMPARE_MAINWINDOW_H

#include "RibbonWindow.h"
#include "sessiondocument.h"
#include "sessiontype.h"
#include <QHash>
#include <QJsonArray>
#include <memory>

class QDockWidget;
class QLabel;
class QPlainTextEdit;

namespace LqCompare {

class SessionArea;
class CompareSession;
namespace Cli { struct Request; }
namespace Settings { class OptionsRepository; }
namespace Options { class OptionsRuntime; }
namespace Vcs { class Backend; }

///
/// \brief The MainWindow class
/// 主窗口：把 Ribbon 页面、会话标签容器、输出面板与状态栏装配成应用（PRD: UI-001、UI-026）。
///
/// 命令注册发生在这里，因为处理器需要访问窗口内的对象。
/// 界面元素本身不直接连接业务槽——它们通过 CommandRegistry 的单一出口执行（UI-024）。
///
class MainWindow : public RibbonWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool openPaths(const QStringList &paths, const QString &typeId = QString());
    CompareSession *openComparison(const QString &typeId, const QString &left, const QString &right, bool transient = false);
    bool openSessionFile(const QString &path);
    bool openRequest(const Cli::Request &request, const QString &workingDirectory = QString());
    void setOptions(Settings::OptionsRepository *repository, Options::OptionsRuntime *runtime);
    /// 注入版本控制后端（VCS-001 第 3 条）。
    ///
    /// 主窗口用它回答一个问题：「这台机器上现在能不能做版本控制查询」，答不出来就把
    /// 版本控制命令全部置灰。后端本身**不归主窗口所有**（`VcsView` 自己造一个），
    /// 这里只要能问一句可用性。传 `nullptr` 表示恢复构造时自建的那个。
    ///
    /// 注入之后**立即**重新探测并刷新命令状态，调用方不必再找别的方式触发一次刷新；
    /// 生产路径上这条入口对应的是「用户配好了 git 路径」那类设置变更。
    ///
    /// 生命周期由调用方负责：传进来的对象必须活到主窗口析构或下一次注入。
    void setVcsBackend(const Vcs::Backend *backend);
    void configureWaitMode(bool merge);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildRibbon();
    void registerCommands();
    void setupDocks();
    void setupStatusBar();
    void refreshTitle();
    void chooseFiles();
    void registerSessionTypes();
    void chooseComparison(const QString &typeId = QString());
    CompareSession *openDocument(const SessionDocument &document, const QString &definitionFile = QString(), bool transient = false);
    void navigateDifference(bool next);
    void exportReport(bool clipboard = false);
    void exportPatch();
    void showVcs(int mode);
    void showSync();
    void applyContentFont();
    void saveCurrent();
    void reloadCurrent();
    void saveSessionDefinition(bool choosePath);
    SessionDocument documentFor(CompareSession *session) const;
    void remember(CompareSession *session);
    void refreshRecent();
    void openRecent(int index);
    void showError(const QString &message, const QString &detail = QString());
    void refreshStatusBar();
    /// 按当前会话的 `CompareSession::StatusSeverity` 开关状态栏警告图标（TXT-010）。
    void updateStatusWarning(CompareSession *session);
    void updateCommandState();
    /// 重新探测版本控制后端并按结论刷新命令状态（VCS-001 第 3 条）。
    ///
    /// 探测结果落在 `m_vcsAvailability*` 上，`updateCommandState()` 只**读**它。
    /// 分开的理由是代价：探测要问后端（真实后端上是一次 `findExecutable`，
    /// 换后端时可能更贵），而 `updateCommandState()` 每切一次会话就会被调用一次。
    void refreshVcsAvailability();
    void appendOutput(const QString &line);

    /// 新建一个会话并返回其标签索引；标题按序号自动生成。
    void newSession(const QString &typeId, const QString &typeName);

    SessionArea *m_sessions = nullptr;
    QDockWidget *m_outputDock = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLabel *m_statusSession = nullptr;
    QLabel *m_statusSpec = nullptr;
    QLabel *m_statusWarning = nullptr;
    int m_untitledCounter = 0;
    int m_logSink = 0;
    QHash<CompareSession *, SessionDocument> m_documents;
    QJsonArray m_recent;
    SessionTypeRegistry m_sessionTypes;
    Settings::OptionsRepository *m_options = nullptr;
    Options::OptionsRuntime *m_optionsRuntime = nullptr;
    // 自建的版本控制后端（`setVcsBackend(nullptr)` 之后回到它）。
    // 用 `unique_ptr` 是为了让头文件只需前置声明 `Vcs::Backend`。
    std::unique_ptr<Vcs::Backend> m_ownedVcsBackend;
    /// 当前用于回答「VCS 能不能用」的后端；不持有所有权，见 `setVcsBackend()`。
    const Vcs::Backend *m_vcsBackend = nullptr;
    /// 上一次探测的结论。`updateCommandState()` 只读这两个字段，不重新探测。
    bool m_vcsAvailable = true;
    QString m_vcsReason;
    bool m_waitMode = false;
    bool m_waitForMerge = false;
    bool m_waitMergeSaved = false;
};

} // namespace LqCompare

#endif // LQCOMPARE_MAINWINDOW_H
