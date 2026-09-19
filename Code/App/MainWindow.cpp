#include "MainWindow.h"

#include "commandregistry.h"
#include "homepage.h"
#include "logging.h"
#include "ribbonlayout.h"
#include "sessionarea.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSysInfo>
#include <QUrl>

#ifndef LQCOMPARE_VERSION
#define LQCOMPARE_VERSION "0.0.0-dev"
#endif

namespace LqCompare {

namespace {

/// 图标资源的统一前缀，避免在注册表里到处写 ":/Pictures/"（UI-025）。
QString icon(const char *name)
{
    return QStringLiteral(":/Pictures/") + QLatin1String(name);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : RibbonWindow(parent)
{
    // 先注册命令，再构建 Ribbon——声明表要能查到命令的图标、说明与快捷键。
    registerCommands();
    buildRibbon();
    setupDocks();
    setupStatusBar();

    if (m_sessions) {
        connect(m_sessions->homePage(), &HomePage::sessionTypeRequested, this,
                [this](const QString &typeId) { newSession(typeId, typeId); });
        connect(m_sessions, &SessionArea::sessionCountChanged, this,
                [this](int) { refreshStatusBar(); });
    }

    refreshTitle();
    refreshStatusBar();
    LQCOMPARE_INFO("app", QStringLiteral("LqCompare %1 启动完成").arg(QStringLiteral(LQCOMPARE_VERSION)));
}

void MainWindow::buildRibbon()
{
    const int buttons = RibbonLayout::build(ribbonBar());

    int ready = 0;
    const QVector<Command> commands = CommandRegistry::instance().all();
    for (const Command &command : commands) {
        if (command.isImplemented()) {
            ++ready;
        }
    }

    appendOutput(tr("Ribbon built: %1 pages, %2 groups, %3 buttons.")
                     .arg(RibbonLayout::pageCount())
                     .arg(RibbonLayout::groupCount())
                     .arg(buttons));
    appendOutput(tr("Commands registered: %1 (implemented: %2)").arg(commands.size()).arg(ready));
}

void MainWindow::registerCommands()
{
    CommandRegistry &registry = CommandRegistry::instance();
    const auto add = [&registry](Command command) {
        if (!registry.add(command)) {
            LQCOMPARE_ERROR("command", QStringLiteral("命令注册失败（ID 重复？）：%1").arg(command.id));
        }
    };

    // --- Home / File --------------------------------------------------------
    add({"file.open", "UI-007", "界面", tr("Open"), tr("Open files, folders or a saved session."),
         icon("ribbon_open.svg"), QKeySequence::Open, [this]() {
             const QString path = QFileDialog::getOpenFileName(
                 this, tr("Open Session"), QString(),
                 tr("LqCompare session (*.lqc);;All files (*)"));
             if (path.isEmpty()) {
                 return;
             }
             appendOutput(tr("Open requested: %1").arg(path));
             newSession(QStringLiteral("text"), QFileInfo(path).fileName());
         }});

    add({"file.save", "UI-007", "界面", tr("Save"), tr("Save the active session."),
         icon("ribbon_save.svg"), QKeySequence::Save,
         [this]() { appendOutput(tr("Save: session persistence is specified in SESS-008.")); }});

    add({"file.saveas", "UI-007", "界面", tr("Save As"), tr("Save the active session to a new file."),
         icon("ribbon_saveas.svg"), QKeySequence::SaveAs,
         [this]() { appendOutput(tr("Save As: specified in SESS-008.")); }});

    add({"file.reload", "UI-007", "界面", tr("Reload"), tr("Reload both sides from disk."),
         icon("ribbon_reload.svg"), QKeySequence(Qt::CTRL | Qt::Key_F5),
         [this]() { appendOutput(tr("Reload: incremental refresh is specified in DIR-034.")); }});

    add({"file.editable", "SESS-017", "会话", tr("Enable Edit"),
         tr("Allow editing in this session. Read-only sessions keep every write command disabled."),
         icon("ribbon_edit.svg"), QKeySequence(), [this]() {
             appendOutput(tr("Enable Edit toggle: specified in SESS-017."));
         }});

    add({"file.exit", "UI-005", "界面", tr("Exit"), tr("Close LqCompare."),
         icon("ribbon_exit.svg"), QKeySequence::Quit, [this]() { close(); }});

    // --- Session -----------------------------------------------------------
    add({"session.new", "SESS-005", "会话", tr("New Session"),
         tr("Create a new comparison session."), icon("ribbon_new.svg"),
         QKeySequence(Qt::CTRL | Qt::Key_N),
         [this]() { newSession(QStringLiteral("text"), tr("Text Compare")); }});

    add({"session.open", "SESS-008", "会话", tr("Open Session"),
         tr("Load a saved session definition."), icon("ribbon_browse.svg"),
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
         [this]() { appendOutput(tr("Open Session: specified in SESS-004 / SESS-008.")); }});

    add({"session.save", "SESS-008", "会话", tr("Save Session"),
         tr("Save the current session definition."), icon("ribbon_save.svg"),
         QKeySequence(), [this]() { appendOutput(tr("Save Session: specified in SESS-008.")); }});

    add({"session.recent", "SESS-009", "会话", tr("Recent"),
         tr("Reopen a recently used session."), icon("ribbon_recent.svg"),
         QKeySequence(), [this]() { appendOutput(tr("Recent sessions: specified in SESS-009.")); }});

    add({"session.close", "SESS-010", "会话", tr("Close Session"),
         tr("Close the active session tab."), icon("ribbon_close.svg"),
         QKeySequence::Close, [this]() {
             if (m_sessions) {
                 m_sessions->closeCurrentSession();
             }
         }});

    // --- Navigate ----------------------------------------------------------
    add({"nav.prevdiff", "TXT-022", "文本比对", tr("Previous Difference"),
         tr("Jump to the previous difference block."), icon("ribbon_prev.svg"),
         QKeySequence(Qt::Key_F7), [this]() {
             appendOutput(tr("Previous difference: requires a session (TXT-022)."));
         }});

    add({"nav.nextdiff", "TXT-022", "文本比对", tr("Next Difference"),
         tr("Jump to the next difference block."), icon("ribbon_next.svg"),
         QKeySequence(Qt::Key_F8), [this]() {
             appendOutput(tr("Next difference: requires a session (TXT-022)."));
         }});

    add({"nav.prevconflict", "MRG-010", "三方合并", tr("Previous Conflict"),
         tr("Jump to the previous unresolved conflict."), icon("ribbon_prev_conflict.svg"),
         QKeySequence(), [this]() { appendOutput(tr("Previous conflict: specified in MRG-010.")); }});

    add({"nav.nextconflict", "MRG-010", "三方合并", tr("Next Conflict"),
         tr("Jump to the next unresolved conflict."), icon("ribbon_next_conflict.svg"),
         QKeySequence(), [this]() { appendOutput(tr("Next conflict: specified in MRG-010.")); }});

    add({"nav.gotoline", "TXT-036", "文本比对", tr("Go To Line"),
         tr("Move the caret to a specific line."), icon("ribbon_gotoline.svg"),
         QKeySequence(Qt::CTRL | Qt::Key_G),
         [this]() { appendOutput(tr("Go to line: specified in TXT-036.")); }});

    // --- Edit --------------------------------------------------------------
    add({"edit.undo", "TXT-029", "文本比对", tr("Undo"), tr("Undo the last edit."),
         icon("ribbon_undo.svg"), QKeySequence::Undo,
         [this]() { appendOutput(tr("Undo: requires an editable session (TXT-029).")); }});

    add({"edit.redo", "TXT-029", "文本比对", tr("Redo"), tr("Redo the last undone edit."),
         icon("ribbon_redo.svg"), QKeySequence::Redo,
         [this]() { appendOutput(tr("Redo: requires an editable session (TXT-029).")); }});

    add({"edit.find", "TXT-028", "文本比对", tr("Find"),
         tr("Find text and highlight every match in the pane."), icon("ribbon_search.svg"),
         QKeySequence::Find,
         [this]() { appendOutput(tr("Find and match highlighting: specified in TXT-028.")); }});

    // --- View --------------------------------------------------------------
    add({"view.outputpane", "SESS-020", "会话", tr("Output Pane"),
         tr("Show or hide the output pane."), icon("ribbon_output.svg"),
         QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_O), [this]() {
             if (m_outputDock) {
                 m_outputDock->setVisible(!m_outputDock->isVisible());
             }
         }});

    add({"view.minimizeribbon", "UI-002", "界面", tr("Minimize Ribbon"),
         tr("Collapse the Ribbon to tab titles only."), icon("ribbon_minimize.svg"),
         QKeySequence(Qt::CTRL | Qt::Key_F1), [this]() {
             if (RibbonBar *bar = ribbonBar()) {
                 bar->setMinimized(!bar->isMinimized());
             }
         }});

    // --- Tools -------------------------------------------------------------
    add({"tools.options", "OPT-001", "选项外观", tr("Program Options"),
         tr("Open program options. The option dialog is specified in OPT-001."),
         icon("ribbon_options.svg"), QKeySequence(Qt::CTRL | Qt::Key_Comma), [this]() {
             QMessageBox::information(this, tr("Program Options"),
                                      tr("The options dialog is specified in PRD entry OPT-001."));
         }});

    // --- Help --------------------------------------------------------------
    add({"help.about", "UI-016", "界面", tr("About LqCompare"),
         tr("Show version, build and third-party license information."),
         icon("ribbon_about.svg"), QKeySequence(), [this]() {
             QMessageBox::about(
                 this, tr("About LqCompare"),
                 tr("<b>LqCompare</b> %1<br>"
                    "Qt %2 · %3<br><br>"
                    "File and folder comparison. Product specification lives in "
                    "<code>docs/PRD-actions.md</code>; every entry is tracked as a GitHub issue.")
                     .arg(QStringLiteral(LQCOMPARE_VERSION), QString::fromLatin1(qVersion()),
                          QSysInfo::prettyProductName()));
         }});

    add({"help.validate", "UI-023", "界面", tr("Validate Commands"),
         tr("Check the command registry for missing text, icons, ACTION-IDs and shortcut conflicts."),
         icon("ribbon_validate.svg"), QKeySequence(), [this]() {
             const QStringList problems = CommandRegistry::instance().validate();
             if (problems.isEmpty()) {
                 QMessageBox::information(
                     this, tr("Validate Commands"),
                     tr("All %1 registered commands passed validation.")
                         .arg(CommandRegistry::instance().all().size()));
                 return;
             }
             QMessageBox::warning(this, tr("Validate Commands"),
                                  tr("%1 problem(s) found:\n\n%2")
                                      .arg(problems.size())
                                      .arg(problems.join(QLatin1Char('\n'))));
         }});

    add({"help.shortcutref", "DOC-004", "文档", tr("Shortcut Reference"),
         tr("List shortcut keys of every implemented command."), icon("ribbon_reference.svg"),
         QKeySequence(), [this]() {
             QStringList lines;
             for (const Command &command : CommandRegistry::instance().all()) {
                 if (!command.isImplemented()) {
                     continue;
                 }
                 const QString key = command.shortcut.isEmpty()
                                         ? tr("(none)")
                                         : command.shortcut.toString(QKeySequence::NativeText);
                 lines.append(QStringLiteral("%1  —  %2").arg(key, command.text));
             }
             lines.sort();
             QMessageBox::information(this, tr("Shortcut Reference"), lines.join(QLatin1Char('\n')));
         }});

    add({"help.manual", "DOC-001", "文档", tr("User Manual"),
         tr("Open the offline user manual."), icon("ribbon_manual.svg"), QKeySequence(),
         [this]() {
             QMessageBox::information(
                 this, tr("User Manual"),
                 tr("The user manual is maintained in docs/ (see DOC-001)."));
         }});

    add({"help.commandline", "DOC-002", "文档", tr("Command Line"),
         tr("List the command line parameters and exit codes."), icon("ribbon_reference.svg"),
         QKeySequence(), [this]() {
             QMessageBox::information(
                 this, tr("Command Line"),
                 tr("Command line reference is specified in DOC-002 and CLI-001..CLI-012."));
         }});

    add({"help.loglevel", "OPT-010", "选项外观", tr("Log Level"),
         tr("Toggle verbose logging for troubleshooting."), icon("ribbon_log.svg"),
         QKeySequence(), [this]() {
             static bool verbose = false;
             verbose = !verbose;
             Log::setLevel(verbose ? Log::Level::Debug : Log::Level::Warning);
             appendOutput(tr("Log level switched to %1.")
                              .arg(verbose ? QStringLiteral("debug") : QStringLiteral("warning")));
         }});

    add({"help.openlogdir", "OPT-010", "选项外观", tr("Open Log Folder"),
         tr("Open the folder that holds the log files."), icon("ribbon_log.svg"),
         QKeySequence(), [this]() {
             const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
             QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
             appendOutput(tr("Log folder: %1").arg(dir));
         }});

    add({"help.resetsettings", "OPT-012", "选项外观", tr("Reset Settings"),
         tr("Reset all program settings back to factory defaults."), icon("ribbon_reset.svg"),
         QKeySequence(), [this]() {
             QMessageBox::warning(this, tr("Reset Settings"),
                                  tr("Settings storage and reset are specified in OPT-012."));
         }});

    add({"help.licenses", "ENG-013", "工程质量", tr("Third-Party Licenses"),
         tr("List third-party components and their licenses."), icon("ribbon_reference.svg"),
         QKeySequence(), [this]() {
             QMessageBox::information(
                 this, tr("Third-Party Licenses"),
                 tr("<b>LqRibbon</b> — in-house Ribbon control library.<br>"
                    "<b>Qt %1</b> — LGPLv3, dynamically linked.<br><br>"
                    "GPL/AGPL components are not used (see ENG-013).")
                     .arg(QString::fromLatin1(qVersion())));
         }});
}

void MainWindow::setupDocks()
{
    m_sessions = new SessionArea(this);
    setCentralWidget(m_sessions);

    m_outputDock = new QDockWidget(tr("Output"), this);
    m_outputDock->setObjectName(QStringLiteral("outputDock"));
    m_output = new QPlainTextEdit(m_outputDock);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(2000);
    m_outputDock->setWidget(m_output);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
}

void MainWindow::setupStatusBar()
{
    m_statusSpec = new QLabel(this);
    m_statusSession = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusSpec);
    statusBar()->addPermanentWidget(m_statusSession);
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::refreshTitle()
{
    if (!m_sessions) {
        setWindowTitle(tr("LqCompare"));
        return;
    }
    if (m_sessions->isHomeCurrent()) {
        setWindowTitle(tr("LqCompare — Home"));
        return;
    }
    setWindowTitle(tr("LqCompare — %1").arg(m_sessions->tabText(m_sessions->currentIndex())));
}

void MainWindow::refreshStatusBar()
{
    if (!m_sessions || !m_statusSession || !m_statusSpec) {
        return;
    }
    m_statusSession->setText(tr("Sessions: %1").arg(m_sessions->sessionCount()));

    int ready = 0;
    const QVector<Command> commands = CommandRegistry::instance().all();
    for (const Command &command : commands) {
        if (command.isImplemented()) {
            ++ready;
        }
    }
    // 状态栏常显规格进度，避免"界面看着齐全、其实大部分没实现"的误判（SESS-019 的口径要求）。
    m_statusSpec->setText(tr("Commands %1/%2").arg(ready).arg(commands.size()));
}

void MainWindow::appendOutput(const QString &line)
{
    if (m_output) {
        m_output->appendPlainText(line);
    }
}

void MainWindow::newSession(const QString &typeId, const QString &typeName)
{
    if (!m_sessions) {
        return;
    }
    ++m_untitledCounter;
    const QString title = tr("Untitled %1 (%2)").arg(m_untitledCounter).arg(typeName);
    m_sessions->addSession(title, typeId);
    m_sessions->homePage()->rememberSession(title, tr("type: %1").arg(typeId));
    refreshTitle();
    refreshStatusBar();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // SESS-018：会话脏状态必须先解决再关闭。会话尚未实现内容模型，
    // 因此这里只在存在会话标签时确认。
    if (m_sessions && m_sessions->sessionCount() > 0) {
        const auto answer = QMessageBox::question(
            this, tr("Close LqCompare"),
            tr("%1 session(s) are open. Close anyway?").arg(m_sessions->sessionCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    LQCOMPARE_INFO("app", QStringLiteral("LqCompare 退出"));
    LqRibbon::RibbonMainWindow::closeEvent(event);
}

} // namespace LqCompare
