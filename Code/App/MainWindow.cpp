#include "MainWindow.h"

#include "commandregistry.h"
#include "homepage.h"
#include "logging.h"
#include "ribbonlayout.h"
#include "sessionarea.h"
#include "comparesession.h"
#include "textcomparesession.h"
#include "foldercomparesession.h"
#include "textmergesession.h"
#include "hexcomparesession.h"
#include "picturecomparesession.h"
#include "tablecomparesession.h"
#include "archivecomparesession.h"
#include "versioncomparesession.h"
#include "mediacomparesession.h"
#include "registrycomparesession.h"
#include "foldermergesession.h"
#include "formatdetector.h"
#include "vcsview.h"
#include "optionsdialog.h"
#include "optionsruntime.h"
#include "shortcutsettingsdialog.h"
#include "report.h"
#include "patch.h"
#include "syncpreviewdialog.h"
#include "clioptions.h"
#include "cliexecution.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QStyle>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QComboBox>
#include <QClipboard>
#include <QFile>
#include <QTextEdit>
#include "sessiondocument.h"
#include "session.h"
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QSettings>
#include <QInputDialog>
#include <QPointer>
#include <QTimer>

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
    registerSessionTypes();
    registerCommands();
    buildRibbon();
    setupQuickAccessBar();
    setupDocks();
    setupStatusBar();
    for (const auto *entry : m_sessionTypes.entries())
        m_sessions->homePage()->setTypeAvailable(entry->type.id, entry->hasFactory() && entry->isAvailableHere(),
                                               entry->isAvailableHere() ? tr("This comparison type is not yet available.") : tr("Windows only"));
    m_sessions->homePage()->setTypeAvailable(QStringLiteral("folder-sync"), true);

    if (m_sessions) {
        connect(m_sessions->homePage(), &HomePage::sessionTypeRequested, this,
                [this](const QString &typeId) { newSession(typeId, typeId); });
        connect(m_sessions, &SessionArea::sessionCountChanged, this,
                [this](int) { refreshStatusBar(); refreshTitle(); });
        connect(m_sessions, &SessionArea::activeSessionStateChanged, this, [this] {
            refreshTitle(); refreshStatusBar();
        });
        connect(m_sessions, &SessionArea::statusTextChanged, statusBar(),
                [this](const QString &text) { statusBar()->showMessage(text); });
        // 严重度是**另一条**通道，不能挂在上面那条上：上面那条只在文本变化时触发，
        // 而「两侧从都混合变成都不混合」这类变化文案可以一字不差（图标该灭）。
        connect(m_sessions, &SessionArea::statusSeverityChanged, this,
                [this](CompareSession::StatusSeverity) {
                    updateStatusWarning(m_sessions->currentSession());
                });
        connect(m_sessions, &SessionArea::errorReported, this, &MainWindow::showError);
        connect(m_sessions->homePage(), &HomePage::recentSessionRequested, this, &MainWindow::openRecent);
    }

    setAcceptDrops(true);
    QSettings settings;
    QStringList shortcutErrors;
    CommandRegistry::instance().loadShortcuts(settings, &shortcutErrors);
    for (const auto &error : shortcutErrors) appendOutput(error);
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    m_recent = QJsonDocument::fromJson(settings.value(QStringLiteral("sessions/recent")).toByteArray()).array();
    refreshRecent();
    updateCommandState();
    refreshTitle();
    refreshStatusBar();
    LQCOMPARE_INFO("app", QStringLiteral("LqCompare %1 启动完成").arg(QStringLiteral(LQCOMPARE_VERSION)));
}

MainWindow::~MainWindow()
{
    Log::removeSink(m_logSink);
    if (m_sessions) {
        disconnect(m_sessions, nullptr, this, nullptr);
        for (int i = 0; i < m_sessions->count(); ++i)
            if (auto *session = m_sessions->sessionAt(i)) disconnect(session, nullptr, this, nullptr);
    }
    CommandRegistry::instance().clear();
}

void MainWindow::updateCommandState()
{
    if (!m_sessions) return;
    auto &registry = CommandRegistry::instance();
    auto *session = m_sessions->currentSession();
    auto *text = qobject_cast<TextCompareSession *>(session);
    auto *merge = qobject_cast<TextMergeSession *>(session);
    auto *folder = qobject_cast<FolderCompareSession *>(session);
    const bool ready = session && session->state() == CompareSession::State::Open;
    const bool navigable = text || merge || folder || qobject_cast<HexCompareSession *>(session) || qobject_cast<TableCompareSession *>(session);
    registry.setCurrentSessionType(session ? session->typeId() : QString());
    for (const auto &id : {"file.reload", "session.save", "session.close"})
        registry.setEnabled(QString::fromLatin1(id), session != nullptr, tr("Open a comparison first."));
    registry.setEnabled(QStringLiteral("file.save"), session && session->canSave(), tr("There are no writable changes to save."));
    registry.setEnabled(QStringLiteral("file.saveas"), (text && (!text->isSideReadOnly(true) || !text->isSideReadOnly(false))) || merge);
    for (const auto &id : {"nav.prevdiff", "nav.nextdiff"})
        registry.setEnabled(QString::fromLatin1(id), navigable, tr("Open a supported comparison first."));
    for (const auto &id : {"nav.gotoline", "edit.find"}) registry.setEnabled(QString::fromLatin1(id), text != nullptr);
    registry.setEnabled(QStringLiteral("file.editable"), text && !text->isSideReadOnly(false));
    registry.setEnabled(QStringLiteral("edit.undo"), (text && text->canUndo()) || (merge && merge->canUndo()));
    registry.setEnabled(QStringLiteral("edit.redo"), (text && text->canRedo()) || (merge && merge->canRedo()));
    for (const auto &id : {"nav.prevconflict", "nav.nextconflict", "merge.useleft", "merge.useright", "merge.leftthenright", "merge.rightthenleft"})
        registry.setEnabled(QString::fromLatin1(id), merge != nullptr);
    registry.setEnabled(QStringLiteral("merge.save"), merge && merge->canSave());
    for (const auto &id : {"report.file", "report.clipboard"}) registry.setEnabled(QString::fromLatin1(id), ready && (text || (folder && !folder->isScanning())));
    registry.setEnabled(QStringLiteral("patch.generate"), ready && text);
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
             chooseFiles();
         }});

    add({"file.save", "UI-007", "界面", tr("Save"), tr("Save the active session."),
         icon("ribbon_save.svg"), QKeySequence::Save,
         [this]() { saveCurrent(); }});

    add({"file.saveas", "UI-007", "界面", tr("Save As"), tr("Save the active session to a new file."),
         icon("ribbon_saveas.svg"), QKeySequence::SaveAs,
         [this]() {
             auto *session = m_sessions->currentSession();
             if (auto *text = qobject_cast<TextCompareSession *>(session)) {
                 QStringList sides;
                 if (!text->isSideReadOnly(true)) sides.append(tr("Left"));
                 if (!text->isSideReadOnly(false)) sides.append(tr("Right"));
                 if (sides.isEmpty()) return;
                 bool accepted=false;
                 const auto side=QInputDialog::getItem(this,tr("Save As"),tr("Which side?"),sides,sides.size()-1,false,&accepted);
                 if (!accepted) return;
                 const bool left=side==tr("Left");
                 const auto path=QFileDialog::getSaveFileName(this,tr("Save As"),left?text->leftPath():text->rightPath());
                 if (path.isEmpty()) return;
                 QString error;
                 if (!text->saveSideAs(left,path,true,&error)) showError(error);
             } else if (auto *merge=qobject_cast<TextMergeSession *>(session)) {
                 const auto path=QFileDialog::getSaveFileName(this,tr("Save Merged As"),merge->outputPath());
                 if (path.isEmpty()) return;
                 QString error;
                 if (!merge->setOutputPath(path,true,&error) || !merge->save(&error)) showError(error);
             }
         }});

    add({"file.reload", "UI-007", "界面", tr("Reload"), tr("Reload both sides from disk."),
         icon("ribbon_reload.svg"), QKeySequence(Qt::CTRL | Qt::Key_F5),
         [this]() { reloadCurrent(); }});

    add({"file.editable", "SESS-017", "会话", tr("Enable Edit"),
         tr("Allow editing in this session. Read-only sessions keep every write command disabled."),
         icon("ribbon_edit.svg"), QKeySequence(), [this]() {
             if (auto *text = qobject_cast<TextCompareSession *>(m_sessions->currentSession()))
                 text->editSide(false);
         }});

    add({"file.exit", "UI-005", "界面", tr("Exit"), tr("Close LqCompare."),
         icon("ribbon_exit.svg"), QKeySequence::Quit, [this]() { close(); }});

    // --- Session -----------------------------------------------------------
    add({"session.new", "SESS-005", "会话", tr("New Session"),
         tr("Create a new comparison session."), icon("ribbon_new.svg"),
         QKeySequence(Qt::CTRL | Qt::Key_N),
         [this]() { chooseComparison(); }});

    add({"session.open", "SESS-008", "会话", tr("Open Session"),
         tr("Load a saved session definition."), icon("ribbon_browse.svg"),
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
         [this]() {
             const QString path = QFileDialog::getOpenFileName(this, tr("Open Session"), {}, tr("LqCompare session (*.lqc)"));
             if (!path.isEmpty()) openSessionFile(path);
         }});

    add({"session.save", "SESS-008", "会话", tr("Save Session"),
         tr("Save the current session definition."), icon("ribbon_save.svg"),
         QKeySequence(), [this]() { saveSessionDefinition(false); }});

    add({"session.recent", "SESS-009", "会话", tr("Recent"),
         tr("Reopen a recently used session."), icon("ribbon_recent.svg"),
         QKeySequence(), [this]() { m_sessions->setCurrentWidget(m_sessions->homePage()); }});

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
             navigateDifference(false);
         }});

    add({"nav.nextdiff", "TXT-022", "文本比对", tr("Next Difference"),
         tr("Jump to the next difference block."), icon("ribbon_next.svg"),
         QKeySequence(Qt::Key_F8), [this]() {
             navigateDifference(true);
         }});

    add({"nav.prevconflict", "MRG-010", "三方合并", tr("Previous Conflict"),
         tr("Jump to the previous unresolved conflict."), icon("ribbon_prev_conflict.svg"),
         QKeySequence(), [this]() { if (auto *merge = qobject_cast<TextMergeSession *>(m_sessions->currentSession())) merge->previousConflict(); }});

    add({"nav.nextconflict", "MRG-010", "三方合并", tr("Next Conflict"),
         tr("Jump to the next unresolved conflict."), icon("ribbon_next_conflict.svg"),
         QKeySequence(), [this]() { if (auto *merge = qobject_cast<TextMergeSession *>(m_sessions->currentSession())) merge->nextConflict(); }});

    add({"nav.gotoline", "TXT-036", "文本比对", tr("Go To Line"),
         tr("Move the caret to a specific line."), icon("ribbon_gotoline.svg"),
         QKeySequence(Qt::CTRL | Qt::Key_G),
         [this]() { if (auto *text = qobject_cast<TextCompareSession *>(m_sessions->currentSession())) text->goToLine(); }});

    // --- Edit --------------------------------------------------------------
    add({"edit.undo", "TXT-029", "文本比对", tr("Undo"), tr("Undo the last edit."),
         icon("ribbon_undo.svg"), QKeySequence::Undo,
         [this]() { if (auto *text = qobject_cast<TextCompareSession *>(m_sessions->currentSession())) text->undo();
             else if (auto *merge = qobject_cast<TextMergeSession *>(m_sessions->currentSession())) merge->undo(); }});

    add({"edit.redo", "TXT-029", "文本比对", tr("Redo"), tr("Redo the last undone edit."),
         icon("ribbon_redo.svg"), QKeySequence::Redo,
         [this]() { if (auto *text = qobject_cast<TextCompareSession *>(m_sessions->currentSession())) text->redo();
             else if (auto *merge = qobject_cast<TextMergeSession *>(m_sessions->currentSession())) merge->redo(); }});

    add({"edit.find", "TXT-028", "文本比对", tr("Find"),
         tr("Find text and highlight every match in the pane."), icon("ribbon_search.svg"),
         QKeySequence::Find,
         [this]() { if (auto *text = qobject_cast<TextCompareSession *>(m_sessions->currentSession())) text->findText(); }});

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
             if (m_options) { Options::OptionsDialog dialog(m_options, this); dialog.exec(); }
         }});

    add({"tools.shortcuts", "UI-027", "界面", tr("Customize Shortcuts"), tr("Edit command shortcuts and resolve conflicts."),
         icon("ribbon_shortcuts.svg"), {}, [this] { QSettings settings; ShortcutSettingsDialog dialog(settings, CommandRegistry::instance(), this); dialog.exec(); }});
    add({"report.file", "RPT-002", "报表导出", tr("To File"), tr("Export the current comparison as HTML or text."),
         icon("ribbon_report.svg"), {}, [this] { exportReport(); }});
    add({"report.clipboard", "RPT-002", "报表导出", tr("To Clipboard"), tr("Copy a text comparison report to the clipboard."),
         icon("ribbon_copy.svg"), {}, [this] { exportReport(true); }});
    add({"patch.generate", "PAT-001", "补丁", tr("Generate Patch"), tr("Export a unified patch from the original file bytes."),
         icon("ribbon_report.svg"), {}, [this] { exportPatch(); }});
    for (const auto &choice : {qMakePair(QStringLiteral("merge.useleft"), Merge::Resolution::Left),
                              qMakePair(QStringLiteral("merge.useright"), Merge::Resolution::Right),
                              qMakePair(QStringLiteral("merge.leftthenright"), Merge::Resolution::LeftThenRight),
                              qMakePair(QStringLiteral("merge.rightthenleft"), Merge::Resolution::RightThenLeft)}) {
        const auto mode = choice.second;
        add({choice.first, "MRG-005", "三方合并", choice.first.section('.',1), tr("Resolve the selected merge conflict using this source order."),
             icon("ribbon_merge.svg"), {}, [this, mode] {
                 if (auto *merge = qobject_cast<TextMergeSession *>(m_sessions->currentSession())) {
                     QString error; if (!merge->resolveCurrent(mode, &error)) showError(error);
                 }
             }});
    }
    add({"merge.save", "MRG-013", "三方合并", tr("Save Merged"), tr("Save the resolved merged output."),
         icon("ribbon_save.svg"), {}, [this] { saveCurrent(); }});
    add({"vcs.diffhead", "VCS-002", "版本控制", tr("Diff vs HEAD"), tr("Compare a Git working tree against HEAD."),
         icon("ribbon_compare.svg"), {}, [this] { showVcs(0); }});
    add({"vcs.tworevisions", "VCS-003", "版本控制", tr("Two Revisions"), tr("Compare two Git revisions without changing the repository."),
         icon("ribbon_compare.svg"), {}, [this] { showVcs(2); }});
    add({"vcs.log", "VCS-006", "版本控制", tr("Show Log"), tr("Browse commits and compare changed files."),
         icon("ribbon_recent.svg"), {}, [this] { showVcs(3); }});
    add({"ops.mirror", "SYNC-001", "文件夹同步", tr("Synchronize Folders"), tr("Preview and confirm a folder synchronization plan."),
         icon("ribbon_compare.svg"), {}, [this] { showSync(); }});

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
                                         : [&command] {
                     QStringList keys;
                     for (const auto &key : CommandRegistry::instance().effectiveShortcuts(command.id)) keys.append(key.toString(QKeySequence::NativeText));
                     return keys.join(QStringLiteral(", "));
                 }();
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
             if (m_options && QMessageBox::question(this, tr("Reset Settings"), tr("Reset all global options? A backup will be saved."),
                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
                 const auto result = m_options->reset();
                 if (!result.ok) showError(result.error);
             }
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
    m_outputDock->hide();
    const QPointer<QPlainTextEdit> output(m_output);
    m_logSink = Log::addSink([output](const Log::Record &record) {
        if (!output) return;
        const QString line = record.line();
        QMetaObject::invokeMethod(output, [output, line] { if (output) output->appendPlainText(line); }, Qt::QueuedConnection);
    });
}

void MainWindow::setupStatusBar()
{
    m_statusSpec = new QLabel(this);
    m_statusSession = new QLabel(this);
    // 状态栏警告图标（TXT-010 第 4 条）。
    //
    // 为什么是一个**独立控件**而不是往状态文本里贴一个字符：状态文本是各会话自己
    // 拼的字符串，塞进去等于要求每个会话都懂「怎么贴图标」，而且图标会跟着
    // `showMessage()` 的生命周期一起被清掉。做成永久控件才能独立开关。
    //
    // 图标在装配时**一次**设好，之后只切可见性：每次刷新都重新生成一遍 pixmap
    // 是白白开销，而且状态栏刷新在批量操作里是高频路径。
    m_statusWarning = new QLabel(this);
    m_statusWarning->setObjectName(QStringLiteral("statusWarningIcon"));
    m_statusWarning->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(12, 12));
    m_statusWarning->setToolTip(tr("This comparison needs attention; see the status message."));
    m_statusWarning->setVisible(false);
    statusBar()->addPermanentWidget(m_statusWarning);
    statusBar()->addPermanentWidget(m_statusSpec);
    statusBar()->addPermanentWidget(m_statusSession);
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::updateStatusWarning(CompareSession *session)
{
    if (!m_statusWarning) return;
    // 图标只有**这一个写入点**（调用方也只有会话的严重度信号这一处）。
    //
    // 一开始这里还从 `refreshStatusBar()` 里也刷一遍，理由是「刷新状态栏时
    // 顺手把图标也重算」——听起来无害，实际是**同一件事的第二条路**：
    // 切标签时两条路都会跑，于是任何一条被删掉，图标的表现**一点变化都没有**
    // （另一条把它兜住了）。本轮的两处变异（去掉容器在切标签时的严重度重播、
    // 去掉 `refreshStatusBar()` 里的这次调用）因此**互相遮蔽、两边都测不出来**。
    // 保留的是「容器重播」那条：它与 `SessionArea::statusTextChanged` 在切标签时
    // 同样重播的既有约定对称，而 `refreshStatusBar()` 只负责状态栏的三个标签。
    const bool warning = session
        && session->statusSeverity() == CompareSession::StatusSeverity::Warning;
    m_statusWarning->setVisible(warning);
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

    auto *session = m_sessions->currentSession();
    m_statusSpec->setText(session ? session->typeId() : QString());
    if (session) statusBar()->showMessage(session->statusText());
    // 这里**故意不**刷警告图标：图标的唯一写入点是 `updateStatusWarning()`，
    // 由会话的严重度信号驱动（切标签时容器会重播一次，因此这里漏不掉）。
    // 两条路并存会让变异测试互相遮蔽，理由写在 `updateStatusWarning()` 上。
    updateCommandState();
}

void MainWindow::appendOutput(const QString &line)
{
    if (m_output) {
        m_output->appendPlainText(line);
    }
}

void MainWindow::newSession(const QString &typeId, const QString &typeName)
{
    Q_UNUSED(typeName)
    if (typeId == QLatin1String("folder-sync")) { showSync(); return; }
    if (typeId.endsWith(QLatin1String("-merge"))) { chooseComparison(typeId); return; }
    openComparison(typeId, {}, {});
}

void MainWindow::showError(const QString &message, const QString &detail)
{
    appendOutput(message + (detail.isEmpty() ? QString() : QStringLiteral("\n") + detail));
    m_outputDock->show();
    statusBar()->showMessage(message, 15000);
}

void MainWindow::registerSessionTypes()
{
    for (const auto &type : builtInSessionTypes()) {
        SessionFactory factory;
        if (type.id == QLatin1String("text")) factory = [](QObject *p) { return new TextCompareSession(p); };
        else if (type.id == QLatin1String("folder")) factory = [](QObject *p) { return new FolderCompareSession(p); };
        else if (type.id == QLatin1String("text-merge")) factory = [](QObject *p) { return new TextMergeSession(p); };
        else if (type.id == QLatin1String("hex")) factory = [](QObject *p) { return new HexCompareSession(p); };
        else if (type.id == QLatin1String("picture")) factory = [](QObject *p) { return new PictureCompareSession(p); };
        else if (type.id == QLatin1String("table")) factory = [](QObject *p) { return new TableCompareSession(p); };
        else if (type.id == QLatin1String("archive")) factory = [](QObject *p) { return new ArchiveCompareSession(p); };
        else if (type.id == QLatin1String("version")) factory = [](QObject *p) { return new VersionCompareSession(p); };
        else if (type.id == QLatin1String("media")) factory = [](QObject *p) { return new MediaCompareSession(p); };
        else if (type.id == QLatin1String("registry")) factory = [](QObject *p) { return new RegistryCompareSession(p); };
        else if (type.id == QLatin1String("folder-merge")) factory = [](QObject *p) { return new FolderMergeSession(p); };
        m_sessionTypes.add(type, factory);
    }
}

CompareSession *MainWindow::openComparison(const QString &typeId, const QString &left, const QString &right, bool transient)
{
    SessionDocument document;
    document.typeId = typeId;
    document.leftPath = left;
    document.rightPath = right;
    return openDocument(document, {}, transient);
}

CompareSession *MainWindow::openDocument(const SessionDocument &document, const QString &definitionFile, bool transient)
{
    const auto *entry = m_sessionTypes.find(document.typeId);
    if (!entry || !entry->hasFactory() || !entry->isAvailableHere()) {
        showError(tr("This comparison type is not available: %1").arg(document.typeId)); return nullptr;
    }
    CompareSession *session = document.typeId == QLatin1String("text-merge")
        ? new TextMergeSession(document.basePath, document.leftPath, document.rightPath, document.outputPath, this)
        : document.typeId == QLatin1String("folder-merge")
            ? static_cast<CompareSession *>(new FolderMergeSession(document.basePath, document.leftPath, document.rightPath, document.outputPath, this))
            : entry->factory(this);
    session->setProperty("transientSource", transient);
    const auto &left = document.leftPath, &right = document.rightPath;
    if (auto *text = qobject_cast<TextCompareSession *>(session)) {
        text->setPaths(left, right);
        text->setUseLocalShortcuts(false);
        text->setReadOnly(document.settings.value(QStringLiteral("session.leftReadOnly"), false).toBool(),
                          document.settings.value(QStringLiteral("session.rightReadOnly"), false).toBool());
        connect(text, &TextCompareSession::readOnlyChanged, this, &MainWindow::updateCommandState);
        connect(text, &TextCompareSession::pathsChanged, this, [this, text] { remember(text); });
        connect(text, &TextCompareSession::comparisonChanged, this, &MainWindow::updateCommandState);
    } else if (auto *folder = qobject_cast<FolderCompareSession *>(session)) {
        folder->setPaths(left, right);
        connect(folder, &FolderCompareSession::compareFilesRequested, this,
                [this](const QString &l, const QString &r) { openPaths({l, r}); });
        connect(folder, &FolderCompareSession::pathsChanged, this, [this, folder] { remember(folder); });
    } else if (auto *hex = qobject_cast<HexCompareSession *>(session)) hex->setPaths(left, right);
    else if (auto *picture = qobject_cast<PictureCompareSession *>(session)) picture->setPaths(left, right);
    else if (auto *table = qobject_cast<TableCompareSession *>(session)) table->setPaths(left, right);
    else if (auto *archive = qobject_cast<ArchiveCompareSession *>(session)) archive->setPaths(left, right);
    else if (auto *version = qobject_cast<VersionCompareSession *>(session)) version->setPaths(left, right);
    else if (auto *media = qobject_cast<MediaCompareSession *>(session)) media->setPaths(left, right);
    else if (auto *registry = qobject_cast<RegistryCompareSession *>(session)) registry->setPaths(left, right);
    if (auto *folderMerge = qobject_cast<FolderMergeSession *>(session)) {
        connect(folderMerge, &FolderMergeSession::textMergeRequested, this,
                [this](const QString &base, const QString &l, const QString &r, const QString &output) {
            SessionDocument merge;
            merge.typeId=QStringLiteral("text-merge"); merge.basePath=base;
            merge.leftPath=l; merge.rightPath=r; merge.outputPath=output;
            openDocument(merge);
        });
    }
    if (auto *merge = qobject_cast<TextMergeSession *>(session)) {
        merge->setUseLocalShortcuts(false);
        connect(merge, &TextMergeSession::mergeChanged, this, &MainWindow::updateCommandState);
    }
    for (auto it = document.settings.cbegin(); it != document.settings.cend(); ++it)
        session->sessionSettings()->setValue(it.key(), it.value());
    m_documents.insert(session, document);
    session->setProperty("definitionFile", definitionFile);
    connect(session, &QObject::destroyed, this, [this, session] { m_documents.remove(session); });
    if (m_sessions->addSession(session) < 0) { delete session; return nullptr; }
    connect(session, &CompareSession::titleChanged, this, [this, session] { remember(session); });
    QString error;
    const bool opened=session->open(&error);
    session->setProperty("openedSuccessfully",opened);
    if (!opened) showError(error);
    else remember(session);
    if (!document.title.isEmpty()) session->setTitle(document.title);
    applyContentFont();
    refreshTitle();
    return session;
}

void MainWindow::chooseFiles()
{
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Choose one or two files"), {},
                                                   tr("All files (*);;LqCompare session (*.lqc)"));
    if (paths.isEmpty()) return;
    if (paths.size() == 1 && !paths.first().endsWith(QLatin1String(".lqc"), Qt::CaseInsensitive)) {
        const QString right = QFileDialog::getOpenFileName(this, tr("Choose the right file (Cancel for an empty side)"));
        openPaths({paths.first(), right});
        return;
    }
    openPaths(paths);
}

bool MainWindow::openPaths(const QStringList &paths, const QString &requestedType)
{
    if (paths.isEmpty()) return true;
    if (paths.size() == 1 && paths.first().endsWith(QLatin1String(".lqc"), Qt::CaseInsensitive))
        return openSessionFile(paths.first());
    if (paths.size() > 2) { showError(tr("Choose at most two files or folders for a comparison.")); return false; }
    const QString left = paths.value(0), right = paths.value(1);
    const bool lDir = QFileInfo(left).isDir(), rDir = QFileInfo(right).isDir();
    if (!right.isEmpty() && lDir != rDir) { showError(tr("A file cannot be compared with a folder.")); return false; }
    QString type = requestedType;
    if (type.isEmpty()) {
        Format::FormatDetector detector;
        const auto detected = detector.detectFiles(left, right, m_sessionTypes);
        if (!detected.canOpen()) { showError(detected.explanation, detected.diagnostics.join(QLatin1Char('\n'))); return false; }
        type = detected.sessionTypeId;
        appendOutput(detected.explanation);
        for (const auto &diagnostic : detected.diagnostics) appendOutput(diagnostic);
    }
    return openComparison(type, left, right) != nullptr;
}

bool MainWindow::openSessionFile(const QString &path)
{
    SessionDocument document;
    QString error;
    if (!SessionDocument::load(path, &document, &error)) { showError(error); return false; }
    auto *opened=openDocument(document, QFileInfo(path).absoluteFilePath());
    return opened && opened->property("openedSuccessfully").toBool();
}

SessionDocument MainWindow::documentFor(CompareSession *session) const
{
    SessionDocument document = m_documents.value(session);
    if (!session) return document;
    document.typeId = session->typeId();
    document.title = session->title();
    if (auto *text = qobject_cast<TextCompareSession *>(session)) {
        document.leftPath = text->leftPath(); document.rightPath = text->rightPath();
        document.settings.insert(QStringLiteral("session.leftReadOnly"), text->isSideReadOnly(true));
        document.settings.insert(QStringLiteral("session.rightReadOnly"), text->isSideReadOnly(false));
    } else if (auto *folder = qobject_cast<FolderCompareSession *>(session)) {
        document.leftPath = folder->leftPath(); document.rightPath = folder->rightPath();
    } else if (auto *merge = qobject_cast<TextMergeSession *>(session)) {
        document.leftPath = merge->leftPath(); document.rightPath = merge->rightPath();
        document.basePath = merge->basePath(); document.outputPath = merge->outputPath();
    } else if (auto *hex = qobject_cast<HexCompareSession *>(session)) {
        document.leftPath = hex->leftPath(); document.rightPath = hex->rightPath();
    } else if (auto *picture = qobject_cast<PictureCompareSession *>(session)) {
        document.leftPath = picture->leftPath(); document.rightPath = picture->rightPath();
    } else if (auto *table = qobject_cast<TableCompareSession *>(session)) {
        document.leftPath = table->leftPath(); document.rightPath = table->rightPath();
    } else if (auto *archive = qobject_cast<ArchiveCompareSession *>(session)) {
        document.leftPath = archive->leftPath(); document.rightPath = archive->rightPath();
    }
    if (auto *version=qobject_cast<VersionCompareSession *>(session)) {
        document.leftPath=version->leftPath(); document.rightPath=version->rightPath();
    } else if (auto *media=qobject_cast<MediaCompareSession *>(session)) {
        document.leftPath=media->leftPath(); document.rightPath=media->rightPath();
    } else if (auto *registry=qobject_cast<RegistryCompareSession *>(session)) {
        document.leftPath=registry->leftPath(); document.rightPath=registry->rightPath();
    } else if (auto *folderMerge=qobject_cast<FolderMergeSession *>(session)) {
        document.leftPath=folderMerge->leftPath(); document.rightPath=folderMerge->rightPath();
        document.basePath=folderMerge->basePath(); document.outputPath=folderMerge->outputPath();
    }
    for (const auto &key : session->sessionSettings()->keys())
        document.settings.insert(key, session->sessionSettings()->value(key));
    return document;
}

void MainWindow::saveSessionDefinition(bool choosePath)
{
    auto *session = m_sessions->currentSession();
    if (!session) return;
    if (session->property("transientSource").toBool()) { showError(tr("Git snapshots are temporary and cannot be saved as a reusable session.")); return; }
    QString path = session->property("definitionFile").toString();
    if (choosePath || path.isEmpty()) path = QFileDialog::getSaveFileName(this, tr("Save Session Definition"), path,
                                                                       tr("LqCompare session (*.lqc)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1String(".lqc"), Qt::CaseInsensitive)) path += QStringLiteral(".lqc");
    const auto document = documentFor(session);
    const auto same = [](const QString &a, const QString &b) {
        if (b.isEmpty()) return false;
        return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath()
            || (!QFileInfo(a).canonicalFilePath().isEmpty() && QFileInfo(a).canonicalFilePath() == QFileInfo(b).canonicalFilePath());
    };
    if (same(path, document.leftPath) || same(path, document.rightPath) || same(path, document.basePath) || same(path, document.outputPath)) {
        showError(tr("A session definition must not overwrite a compared source file.")); return;
    }
    QString error;
    if (!document.save(path, &error)) { showError(error); return; }
    m_documents.insert(session, document);
    session->setProperty("definitionFile", QFileInfo(path).absoluteFilePath());
    remember(session);
    statusBar()->showMessage(tr("Session saved: %1").arg(path), 5000);
}

void MainWindow::saveCurrent()
{
    auto *session = m_sessions->currentSession();
    if (!session) return;
    if (!session->canSave() && !qobject_cast<TextMergeSession *>(session)) return;
    if (auto *merge=qobject_cast<TextMergeSession *>(session)) {
        if (merge->outputPath().isEmpty()) {
            const auto path=QFileDialog::getSaveFileName(this,tr("Save Merged Output"));
            if (path.isEmpty()) return;
            QString error;
            if (!merge->setOutputPath(path,true,&error)) { showError(error); return; }
        }
    }
    QString error;
    if (!session->save(&error)) showError(error);
}

void MainWindow::reloadCurrent()
{
    auto *session = m_sessions->currentSession();
    if (!session) return;
    const bool wasDirty = session->isDirty();
    bool discarded = false;
    if (wasDirty) {
        const auto answer = QMessageBox::question(this, tr("Reload"), tr("Save changes before reloading?"),
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return;
        QString error;
        if (answer == QMessageBox::Save && !session->save(&error)) { showError(error); return; }
        if (answer == QMessageBox::Discard) { session->setDirty(false); discarded = true; }
    }
    QString error;
    const bool loaded = session->state() == CompareSession::State::Open ? session->reload(&error) : session->open(&error);
    if (!loaded) {
        if (discarded) session->setDirty(true);
        showError(error);
    }
}

void MainWindow::remember(CompareSession *session)
{
    if (!session || session->property("transientSource").toBool()) return;
    const auto document = documentFor(session);
    if (document.leftPath.isEmpty() && document.rightPath.isEmpty()) return;
    const auto definitionFile = session->property("definitionFile").toString();
    const auto recordKey = document.typeId + QChar::Null + document.leftPath + QChar::Null + document.rightPath + QChar::Null + definitionFile;
    QJsonArray next;
    QJsonObject record = document.toJson();
    record.insert(QStringLiteral("definitionFile"), definitionFile);
    record.insert(QStringLiteral("usedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    record.insert(QStringLiteral("key"), recordKey);
    next.append(record);
    const int capacity = qBound(1, QSettings().value(QStringLiteral("sessions/recentCapacity"), 20).toInt(), 200);
    for (const auto &value : m_recent) {
        if (next.size() >= capacity) break;
        if (value.toObject().value(QStringLiteral("key")).toString() != recordKey) next.append(value);
    }
    m_recent = next;
    QSettings().setValue(QStringLiteral("sessions/recent"), QJsonDocument(m_recent).toJson(QJsonDocument::Compact));
    refreshRecent();
}

void MainWindow::refreshRecent()
{
    QList<QPair<QString, QString>> entries;
    for (const auto &value : m_recent) {
        const auto record = value.toObject();
        const auto sources = record.value(QStringLiteral("sources")).toObject();
        const auto left = sources.value(QStringLiteral("left")).toString();
        const auto right = sources.value(QStringLiteral("right")).toString();
        const bool missing = (!left.isEmpty() && !QFileInfo::exists(left)) || (!right.isEmpty() && !QFileInfo::exists(right));
        entries.append({record.value(QStringLiteral("title")).toString() + (missing ? tr(" — source missing") : QString()),
                        left + QStringLiteral(" ↔ ") + right + QStringLiteral("\n") + record.value(QStringLiteral("usedAt")).toString()});
    }
    m_sessions->homePage()->setRecentSessions(entries);
}

void MainWindow::openRecent(int index)
{
    if (index < 0 || index >= m_recent.size()) return;
    const auto record = m_recent[index].toObject();
    const auto file = record.value(QStringLiteral("definitionFile")).toString();
    if (!file.isEmpty()) { openSessionFile(file); return; }
    SessionDocument document;
    QString error;
    if (!SessionDocument::fromJson(record, QDir::currentPath(), &document, &error)) { showError(error); return; }
    openDocument(document);
}

void MainWindow::chooseComparison(const QString &initialType)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Comparison"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *type = new QComboBox(&dialog);
    for (const auto *entry : m_sessionTypes.entries())
        if (entry->hasFactory() && entry->isAvailableHere()) type->addItem(entry->type.displayName, entry->type.id);
    if (!initialType.isEmpty()) type->setCurrentIndex(type->findData(initialType));
    form->addRow(tr("Comparison type"), type);
    const auto row = [&](const QString &label, bool output) {
        auto *container = new QWidget(&dialog);
        auto *horizontal = new QHBoxLayout(container);
        horizontal->setContentsMargins(0,0,0,0);
        auto *edit = new QLineEdit(container);
        auto *browse = new QPushButton(tr("Browse…"), container);
        horizontal->addWidget(edit,1); horizontal->addWidget(browse);
        form->addRow(label,container);
        connect(browse,&QPushButton::clicked,&dialog,[&, edit, output] {
            QString path;
            if (type->currentData().toString().startsWith(QLatin1String("folder")))
                path = QFileDialog::getExistingDirectory(&dialog,tr("Choose Folder"),edit->text());
            else if (output) path = QFileDialog::getSaveFileName(&dialog,tr("Choose Output File"),edit->text());
            else path = QFileDialog::getOpenFileName(&dialog,tr("Choose File"),edit->text());
            if (!path.isEmpty()) edit->setText(path);
        });
        return edit;
    };
    auto *left = row(tr("Left"),false);
    auto *right = row(tr("Right"),false);
    auto *base = row(tr("Base (optional)"),false);
    auto *output = row(tr("Merged output"),true);
    const auto update = [type,base,output] {
        const bool merge = type->currentData().toString().endsWith(QLatin1String("-merge"));
        base->parentWidget()->setEnabled(merge); output->parentWidget()->setEnabled(merge);
    };
    connect(type,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[update] { update(); });
    update();
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel,&dialog);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(620,240);
    if (dialog.exec() != QDialog::Accepted) return;
    SessionDocument document;
    document.typeId=type->currentData().toString();
    document.leftPath=left->text(); document.rightPath=right->text();
    if (document.typeId.endsWith(QLatin1String("-merge"))) {
        document.basePath=base->text(); document.outputPath=output->text();
    }
    openDocument(document);
}

void MainWindow::navigateDifference(bool next)
{
    auto *session = m_sessions->currentSession();
    if (auto *text = qobject_cast<TextCompareSession *>(session)) next ? text->nextDifference() : text->previousDifference();
    else if (auto *folder = qobject_cast<FolderCompareSession *>(session)) next ? folder->nextDifference() : folder->previousDifference();
    else if (auto *hex = qobject_cast<HexCompareSession *>(session)) next ? hex->nextDifference() : hex->previousDifference();
    else if (auto *table = qobject_cast<TableCompareSession *>(session)) next ? table->nextDifference() : table->previousDifference();
    else if (auto *merge = qobject_cast<TextMergeSession *>(session)) next ? merge->nextConflict() : merge->previousConflict();
}

void MainWindow::configureWaitMode(bool merge)
{
    m_waitMode=true; m_waitForMerge=merge;
    connect(m_sessions,&SessionArea::sessionClosing,this,[this](CompareSession *session) {
        if (auto *merge=qobject_cast<TextMergeSession *>(session)) m_waitMergeSaved=merge->hasSavedOutput();
    });
    connect(m_sessions,&SessionArea::sessionCountChanged,this,[this](int count) {
        if (m_waitMode && count==0) QTimer::singleShot(0,this,[this] {
            QCoreApplication::exit(m_waitForMerge && !m_waitMergeSaved ? 1 : 0);
        });
    });
}

void MainWindow::setOptions(Settings::OptionsRepository *repository, Options::OptionsRuntime *runtime)
{
    m_options = repository; m_optionsRuntime = runtime;
    if (runtime) {
        connect(runtime, &Options::OptionsRuntime::contentFontChanged, this, [this] { applyContentFont(); });
        connect(runtime, &Options::OptionsRuntime::runtimeError, this, [this](const QString &message) { showError(message); });
        applyContentFont();
    }
    if (repository) {
        connect(repository, &Settings::OptionsRepository::changed, this, [this] {
            ribbonBar()->setFont(QApplication::font());
        });
        connect(m_sessions, &SessionArea::sessionCountChanged, this, [this](int count) {
            if (count == 0 && m_options->value(QStringLiteral("general.lastSessionAction")).toString() == QLatin1String("exit"))
                QTimer::singleShot(0,this,&QWidget::close);
        });
    }
}

void MainWindow::applyContentFont()
{
    if (!m_optionsRuntime || !m_sessions) return;
    for (int i=0;i<m_sessions->count();++i) {
        if (!m_sessions->sessionAt(i)) continue;
        for (auto *editor : m_sessions->widget(i)->findChildren<QPlainTextEdit *>())
            editor->setFont(m_optionsRuntime->contentFont());
    }
}

bool MainWindow::openRequest(const Cli::Request &request, const QString &workingDirectory)
{
    const QDir directory(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    const auto absolute = [&directory](const QString &path) { return path.isEmpty() ? path : directory.absoluteFilePath(path); };
    if (request.left.isEmpty() && request.right.isEmpty() && request.sessionType.isEmpty()) return true;
    if (request.left.endsWith(QLatin1String(".lqc"),Qt::CaseInsensitive) && request.right.isEmpty()) {
        SessionDocument saved;
        QString error;
        const auto path=absolute(request.left);
        if (!SessionDocument::load(path,&saved,&error)) { showError(error); return false; }
        saved.settings.insert(QStringLiteral("session.leftReadOnly"), request.leftReadOnly || saved.settings.value(QStringLiteral("session.leftReadOnly")).toBool());
        saved.settings.insert(QStringLiteral("session.rightReadOnly"), request.rightReadOnly || saved.settings.value(QStringLiteral("session.rightReadOnly")).toBool());
        if (!request.sessionType.isEmpty()) saved.typeId=request.sessionType;
        auto *opened=openDocument(saved,path);
        return opened && opened->property("openedSuccessfully").toBool();
    }
    SessionDocument document;
    document.leftPath=absolute(request.left); document.rightPath=absolute(request.right);
    document.basePath=absolute(request.base); document.outputPath=absolute(request.output);
    document.typeId=request.sessionType;
    if (document.typeId.isEmpty()) {
        const auto detected=Format::FormatDetector().detectFiles(document.leftPath,document.rightPath,m_sessionTypes);
        if (!detected.canOpen()) { showError(detected.explanation,detected.diagnostics.join(QLatin1Char('\n'))); return false; }
        document.typeId=detected.sessionTypeId;
    }
    if ((!request.base.isEmpty() || !request.output.isEmpty()) && request.sessionType.isEmpty()) document.typeId=QStringLiteral("text-merge");
    document.settings.insert(QStringLiteral("session.leftReadOnly"),request.leftReadOnly);
    document.settings.insert(QStringLiteral("session.rightReadOnly"),request.rightReadOnly);
    document.settings.insert(QStringLiteral("text.ignoreCase"),request.textOptions.ignoreCase);
    document.settings.insert(QStringLiteral("text.ignoreEol"),request.textOptions.ignoreEol);
    document.settings.insert(QStringLiteral("text.ignoreFinalNewline"),request.textOptions.ignoreFinalNewline);
    document.settings.insert(QStringLiteral("text.whitespace"),int(request.textOptions.whitespace));
    document.settings.insert(QStringLiteral("text.alignSimilarLines"),request.textOptions.alignSimilarLines);
    document.settings.insert(QStringLiteral("text.similarityThreshold"),request.textOptions.similarityThreshold);
    if (!request.encoding.isEmpty()) {
        document.settings.insert(QStringLiteral("text.leftEncoding"),QString::fromLatin1(request.encoding));
        document.settings.insert(QStringLiteral("text.rightEncoding"),QString::fromLatin1(request.encoding));
    }
    auto *session=openDocument(document);
    if (auto *text=qobject_cast<TextCompareSession *>(session)) text->setReadOnly(request.leftReadOnly,request.rightReadOnly);
    return session && session->property("openedSuccessfully").toBool();
}

void MainWindow::exportReport(bool clipboard)
{
    auto *session=m_sessions->currentSession();
    if (!session || session->state() != CompareSession::State::Open) {
        showError(tr("Open a valid comparison before exporting a report.")); return;
    }
    Report::Model model;
    if (auto *text=qobject_cast<TextCompareSession *>(session))
        model=Report::fromText(text->leftDocument(),text->rightDocument(),text->comparison(),text->comparisonOptions());
    else if (auto *folder=qobject_cast<FolderCompareSession *>(session)) {
        if (folder->isScanning()) { showError(tr("Wait for the folder comparison to finish.")); return; }
        model=Report::fromFolder(folder->result(),folder->comparisonOptions());
    } else return;
    Report::Options options;
    QString error;
    if (clipboard) {
        options.format=Report::Format::PlainText;
        const auto output=Report::render(model,options,&error);
        if (!error.isEmpty()) { showError(error); return; }
        QApplication::clipboard()->setText(output);
        statusBar()->showMessage(tr("Report copied."),5000);
        return;
    }
    QString selected;
    const auto path=QFileDialog::getSaveFileName(this,tr("Export Report"),{},tr("HTML report (*.html);;Text report (*.txt)"),&selected);
    if (path.isEmpty()) return;
    const auto source=documentFor(session);
    if (!Cli::isSafeOutputPath(path,{source.leftPath,source.rightPath,source.basePath,source.outputPath},&error)) { showError(error); return; }
    options.format=selected.startsWith(QLatin1String("Text"))?Report::Format::PlainText:Report::Format::Html;
    if (!Report::writeFile(path,model,options,&error,nullptr,{},true)) showError(error);
    else statusBar()->showMessage(tr("Report saved: %1").arg(path),5000);
}

void MainWindow::exportPatch()
{
    auto *text=qobject_cast<TextCompareSession *>(m_sessions->currentSession());
    if (!text || text->state() != CompareSession::State::Open) { showError(tr("Open a valid text comparison before generating a patch.")); return; }
    if (text->isDirty()) { showError(tr("Save both sides before generating a patch from disk.")); return; }
    Patch::FileInput input;
    const auto left=text->leftPath(),right=text->rightPath();
    input.oldExists=!left.isEmpty(); input.newExists=!right.isEmpty();
    input.oldPath=QFileInfo(left.isEmpty()?right:left).fileName();
    input.newPath=input.oldPath;
    QString encodeError;
    input.oldBytes=text->leftDocument().bytes(&encodeError);
    if (!encodeError.isEmpty()) { showError(encodeError); return; }
    input.newBytes=text->rightDocument().bytes(&encodeError);
    if (!encodeError.isEmpty()) { showError(encodeError); return; }
    const auto result=Patch::generate({input},{});
    if (!result.ok) { showError(tr("Unable to generate this patch.")); return; }
    const auto path=QFileDialog::getSaveFileName(this,tr("Export Unified Patch"),{},tr("Patch (*.patch *.diff)"));
    if (path.isEmpty()) return;
    QString error;
    if (!Cli::isSafeOutputPath(path,{left,right},&error)) { showError(error); return; }
    if (!Patch::writeFile(path,result.bytes,&error,true)) showError(error);
    else statusBar()->showMessage(tr("Patch saved: %1").arg(path),5000);
}

void MainWindow::showVcs(int mode)
{
    QString path;
    if (auto *session=m_sessions->currentSession()) path=documentFor(session).leftPath;
    if (path.isEmpty()) path=QFileDialog::getExistingDirectory(this,tr("Choose Git Working Tree"));
    if (path.isEmpty()) return;
    auto *dialog=new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Git Comparisons"));
    auto *layout=new QVBoxLayout(dialog);
    auto *view=new VcsView(path,dialog);
    view->setMode(static_cast<VcsView::Mode>(mode));
    layout->addWidget(view);
    connect(view,&VcsView::errorOccurred,this,[this](const QString &error) { showError(error); });
    connect(view,&VcsView::compareRequested,this,[this](const Vcs::Comparison &comparison) {
        auto *session=openComparison(comparison.binary?QStringLiteral("hex"):QStringLiteral("text"),comparison.leftPath,comparison.rightPath,true);
        if (!session) return;
        // Retain snapshots for the complete comparison lifetime, including lazy views.
        connect(session,&QObject::destroyed,this,[lifetime=comparison.lifetime] { Q_UNUSED(lifetime) });
        if (auto *text=qobject_cast<TextCompareSession *>(session)) text->setReadOnly(true,true);
        session->setTitle(comparison.title);
        session->setProperty("transientSource",true);
    });
    dialog->resize(1000,700); dialog->show();
}

void MainWindow::showSync()
{
    auto *dialog=new SyncPreviewDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (auto *folder=qobject_cast<FolderCompareSession *>(m_sessions->currentSession()))
        dialog->setDirectories(folder->leftPath(),folder->rightPath());
    dialog->show();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || urls.size() > 2) return;
    for (const auto &url : urls) if (!url.isLocalFile()) return;
    event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) paths.append(url.toLocalFile());
    if (openPaths(paths)) event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_sessions && !m_sessions->closeAllSessions()) { event->ignore(); return; }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    if (m_waitMode) QCoreApplication::exit(m_waitForMerge && !m_waitMergeSaved ? 1 : 0);
    LqRibbon::RibbonMainWindow::closeEvent(event);
}

} // namespace LqCompare
