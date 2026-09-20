#ifndef LQCOMPARE_MAINWINDOW_H
#define LQCOMPARE_MAINWINDOW_H

#include "RibbonWindow.h"
#include "sessiondocument.h"
#include "sessiontype.h"
#include <QHash>
#include <QJsonArray>

class QDockWidget;
class QLabel;
class QPlainTextEdit;

namespace LqCompare {

class SessionArea;
class CompareSession;
namespace Cli { struct Request; }
namespace Settings { class OptionsRepository; }
namespace Options { class OptionsRuntime; }

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
    void updateCommandState();
    void appendOutput(const QString &line);

    /// 新建一个会话并返回其标签索引；标题按序号自动生成。
    void newSession(const QString &typeId, const QString &typeName);

    SessionArea *m_sessions = nullptr;
    QDockWidget *m_outputDock = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLabel *m_statusSession = nullptr;
    QLabel *m_statusSpec = nullptr;
    int m_untitledCounter = 0;
    int m_logSink = 0;
    QHash<CompareSession *, SessionDocument> m_documents;
    QJsonArray m_recent;
    SessionTypeRegistry m_sessionTypes;
    Settings::OptionsRepository *m_options = nullptr;
    Options::OptionsRuntime *m_optionsRuntime = nullptr;
    bool m_waitMode = false;
    bool m_waitForMerge = false;
    bool m_waitMergeSaved = false;
};

} // namespace LqCompare

#endif // LQCOMPARE_MAINWINDOW_H
