#ifndef LQCOMPARE_MAINWINDOW_H
#define LQCOMPARE_MAINWINDOW_H

#include "RibbonWindow.h"

class QDockWidget;
class QLabel;
class QPlainTextEdit;

namespace LqCompare {

class SessionArea;

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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildRibbon();
    void registerCommands();
    void setupDocks();
    void setupStatusBar();
    void refreshTitle();
    void refreshStatusBar();
    void appendOutput(const QString &line);

    /// 新建一个会话并返回其标签索引；标题按序号自动生成。
    void newSession(const QString &typeId, const QString &typeName);

    SessionArea *m_sessions = nullptr;
    QDockWidget *m_outputDock = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLabel *m_statusSession = nullptr;
    QLabel *m_statusSpec = nullptr;
    int m_untitledCounter = 0;
};

} // namespace LqCompare

#endif // LQCOMPARE_MAINWINDOW_H
