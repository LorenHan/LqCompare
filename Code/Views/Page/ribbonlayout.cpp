#include "ribbonlayout.h"

#include "commandregistry.h"
#include "logging.h"

#include "LqRibbon.h"

#include <QAction>
#include <QCoreApplication>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QVector>

namespace LqCompare {

namespace {

enum ButtonSize {
    Large, ///< 图标在上文字在下（32×32）
    Small, ///< 仅图标配文字（16×16），横向排列
};

struct ButtonSpec
{
    const char *commandId;
    const char *text;
    const char *actionId; ///< 对应的 PRD ACTION-ID；未实现时展示给用户
    ButtonSize size;
};

struct GroupSpec
{
    const char *objectName;
    const char *title;
    QVector<ButtonSpec> buttons;
};

struct PageSpec
{
    const char *objectName;
    const char *title;
    QVector<GroupSpec> groups;
};

// ---------------------------------------------------------------------------
// Ribbon 声明表
//
// 页面/分组的划分来自 docs/research/tortoisegit-diff-features.md §C.3
// （建议的 Ribbon 页面 → 组 → 按钮），按钮取舍以 Beyond Compare 的功能面为准。
// ---------------------------------------------------------------------------
const QVector<PageSpec> &layout()
{
    static const QVector<PageSpec> table = {
        {"ribbonHomePage", QT_TRANSLATE_NOOP("RibbonLayout", "Home"),
         {
             {"ribbonGroupFile", QT_TRANSLATE_NOOP("RibbonLayout", "File"),
              {
                  {"file.open", QT_TRANSLATE_NOOP("RibbonLayout", "Open"), "UI-007", Large},
                  {"file.save", QT_TRANSLATE_NOOP("RibbonLayout", "Save"), "UI-007", Large},
                  {"file.saveas", QT_TRANSLATE_NOOP("RibbonLayout", "Save As"), "UI-007", Small},
                  {"file.reload", QT_TRANSLATE_NOOP("RibbonLayout", "Reload"), "UI-007", Small},
                  {"file.editable", QT_TRANSLATE_NOOP("RibbonLayout", "Enable Edit"), "SESS-017", Small},
                  {"file.exit", QT_TRANSLATE_NOOP("RibbonLayout", "Exit"), "UI-005", Small},
              }},
             {"ribbonGroupSession", QT_TRANSLATE_NOOP("RibbonLayout", "Session"),
              {
                  {"session.new", QT_TRANSLATE_NOOP("RibbonLayout", "New Session"), "SESS-005", Large},
                  {"session.open", QT_TRANSLATE_NOOP("RibbonLayout", "Open Session"), "SESS-008", Small},
                  {"session.save", QT_TRANSLATE_NOOP("RibbonLayout", "Save Session"), "SESS-008", Small},
                  {"session.recent", QT_TRANSLATE_NOOP("RibbonLayout", "Recent"), "SESS-009", Small},
              }},
             {"ribbonGroupNavigate", QT_TRANSLATE_NOOP("RibbonLayout", "Navigate"),
              {
                  {"nav.prevdiff", QT_TRANSLATE_NOOP("RibbonLayout", "Previous Difference"), "TXT-022", Large},
                  {"nav.nextdiff", QT_TRANSLATE_NOOP("RibbonLayout", "Next Difference"), "TXT-022", Large},
                  {"nav.prevconflict", QT_TRANSLATE_NOOP("RibbonLayout", "Previous Conflict"), "MRG-010", Small},
                  {"nav.nextconflict", QT_TRANSLATE_NOOP("RibbonLayout", "Next Conflict"), "MRG-010", Small},
                  {"nav.gotoline", QT_TRANSLATE_NOOP("RibbonLayout", "Go To Line"), "TXT-036", Small},
              }},
             {"ribbonGroupClipboard", QT_TRANSLATE_NOOP("RibbonLayout", "Clipboard"),
              {
                  {"edit.cut", QT_TRANSLATE_NOOP("RibbonLayout", "Cut"), "UI-010", Small},
                  {"edit.copy", QT_TRANSLATE_NOOP("RibbonLayout", "Copy"), "UI-010", Small},
                  {"edit.paste", QT_TRANSLATE_NOOP("RibbonLayout", "Paste"), "UI-010", Small},
                  {"edit.selectall", QT_TRANSLATE_NOOP("RibbonLayout", "Select All"), "DIR-018", Small},
              }},
             {"ribbonGroupSourceControl", QT_TRANSLATE_NOOP("RibbonLayout", "Source Control"),
              {
                  {"vcs.diffhead", QT_TRANSLATE_NOOP("RibbonLayout", "Diff vs HEAD"), "VCS-002", Large},
                  {"vcs.tworevisions", QT_TRANSLATE_NOOP("RibbonLayout", "Two Revisions"), "VCS-003", Small},
                  {"vcs.twobranches", QT_TRANSLATE_NOOP("RibbonLayout", "Two Branches"), "VCS-004", Small},
                  {"vcs.log", QT_TRANSLATE_NOOP("RibbonLayout", "Show Log"), "VCS-006", Small},
                  {"vcs.blame", QT_TRANSLATE_NOOP("RibbonLayout", "Blame"), "VCS-012", Small},
                  {"vcs.revisiongraph", QT_TRANSLATE_NOOP("RibbonLayout", "Revision Graph"), "VCS-011", Small},
              }},
         }},

        {"ribbonComparePage", QT_TRANSLATE_NOOP("RibbonLayout", "Compare"),
         {
             {"ribbonGroupSources", QT_TRANSLATE_NOOP("RibbonLayout", "Sources"),
              {
                  {"source.left", QT_TRANSLATE_NOOP("RibbonLayout", "Left Path"), "UI-022", Large},
                  {"source.right", QT_TRANSLATE_NOOP("RibbonLayout", "Right Path"), "UI-022", Large},
                  {"source.swap", QT_TRANSLATE_NOOP("RibbonLayout", "Swap Sides"), "DIR-001", Small},
                  {"source.reloadboth", QT_TRANSLATE_NOOP("RibbonLayout", "Reload Both"), "DIR-034", Small},
              }},
             {"ribbonGroupRules", QT_TRANSLATE_NOOP("RibbonLayout", "Rules"),
              {
                  {"rules.settings", QT_TRANSLATE_NOOP("RibbonLayout", "Comparison Settings"), "SESS-006", Large},
                  {"rules.ignorewhitespace", QT_TRANSLATE_NOOP("RibbonLayout", "Ignore Whitespace"), "TXT-009", Small},
                  {"rules.ignoreallwhitespace", QT_TRANSLATE_NOOP("RibbonLayout", "Ignore All Whitespace"), "TXT-009", Small},
                  {"rules.comparewhitespace", QT_TRANSLATE_NOOP("RibbonLayout", "Compare Whitespace"), "TXT-009", Small},
                  {"rules.ignoreeol", QT_TRANSLATE_NOOP("RibbonLayout", "Ignore Line Endings"), "TXT-010", Small},
                  {"rules.ignorecase", QT_TRANSLATE_NOOP("RibbonLayout", "Ignore Case"), "TXT-008", Small},
                  {"rules.ignorecomments", QT_TRANSLATE_NOOP("RibbonLayout", "Ignore Comments"), "TXT-011", Small},
              }},
             {"ribbonGroupAlignment", QT_TRANSLATE_NOOP("RibbonLayout", "Alignment"),
              {
                  {"align.similar", QT_TRANSLATE_NOOP("RibbonLayout", "Align Similar Lines"), "TXT-005", Large},
                  {"align.style", QT_TRANSLATE_NOOP("RibbonLayout", "Alignment Style"), "TXT-004", Small},
                  {"align.manual", QT_TRANSLATE_NOOP("RibbonLayout", "Manual Align"), "TXT-006", Small},
                  {"align.break", QT_TRANSLATE_NOOP("RibbonLayout", "Disconnect"), "TXT-006", Small},
              }},
             {"ribbonGroupSyncScroll", QT_TRANSLATE_NOOP("RibbonLayout", "Sync Scroll"),
              {
                  {"sync.horizontal", QT_TRANSLATE_NOOP("RibbonLayout", "Sync Horizontal"), "TXT-019", Small},
                  {"sync.vertical", QT_TRANSLATE_NOOP("RibbonLayout", "Sync Vertical"), "TXT-019", Small},
                  {"sync.link", QT_TRANSLATE_NOOP("RibbonLayout", "Link Panes"), "IMG-004", Small},
                  {"sync.isolate", QT_TRANSLATE_NOOP("RibbonLayout", "Isolate Selection"), "DIR-018", Small},
              }},
             {"ribbonGroupFolderOptions", QT_TRANSLATE_NOOP("RibbonLayout", "Folder Options"),
              {
                  {"folder.subfolders", QT_TRANSLATE_NOOP("RibbonLayout", "Compare Subfolders"), "DIR-003", Large},
                  {"folder.background", QT_TRANSLATE_NOOP("RibbonLayout", "Scan in Background"), "DIR-002", Small},
                  {"folder.timestamps", QT_TRANSLATE_NOOP("RibbonLayout", "Compare Timestamps"), "DIR-005", Small},
                  {"folder.size", QT_TRANSLATE_NOOP("RibbonLayout", "Compare Size"), "DIR-004", Small},
                  {"folder.content", QT_TRANSLATE_NOOP("RibbonLayout", "Compare Contents"), "DIR-007", Small},
                  {"folder.archives", QT_TRANSLATE_NOOP("RibbonLayout", "Archive Handling"), "DIR-033", Small},
                  {"folder.filenamecase", QT_TRANSLATE_NOOP("RibbonLayout", "Compare Filename Case"), "DIR-006", Small},
                  {"folder.symlinks", QT_TRANSLATE_NOOP("RibbonLayout", "Follow Symlinks"), "DIR-037", Small},
              }},
         }},

        {"ribbonMergePage", QT_TRANSLATE_NOOP("RibbonLayout", "Merge"),
         {
             {"ribbonGroupMergeOutput", QT_TRANSLATE_NOOP("RibbonLayout", "Merge Output"),
              {
                  {"merge.showpane", QT_TRANSLATE_NOOP("RibbonLayout", "Merged Pane"), "MRG-001", Large},
                  {"merge.auto", QT_TRANSLATE_NOOP("RibbonLayout", "Auto Merge"), "MRG-003", Large},
                  {"merge.save", QT_TRANSLATE_NOOP("RibbonLayout", "Save Merged"), "MRG-013", Small},
                  {"merge.recompare", QT_TRANSLATE_NOOP("RibbonLayout", "Recompare Output"), "MRG-014", Small},
              }},
             {"ribbonGroupBlockActions", QT_TRANSLATE_NOOP("RibbonLayout", "Block Actions"),
              {
                  {"merge.useleft", QT_TRANSLATE_NOOP("RibbonLayout", "Use Left"), "MRG-005", Large},
                  {"merge.useright", QT_TRANSLATE_NOOP("RibbonLayout", "Use Right"), "MRG-005", Large},
                  {"merge.leftthenright", QT_TRANSLATE_NOOP("RibbonLayout", "Left Then Right"), "MRG-006", Small},
                  {"merge.rightthenleft", QT_TRANSLATE_NOOP("RibbonLayout", "Right Then Left"), "MRG-006", Small},
                  {"merge.wholefile", QT_TRANSLATE_NOOP("RibbonLayout", "Use Whole File"), "MRG-007", Small},
              }},
             {"ribbonGroupConflict", QT_TRANSLATE_NOOP("RibbonLayout", "Conflict"),
              {
                  {"conflict.resolve", QT_TRANSLATE_NOOP("RibbonLayout", "Mark Resolved"), "MRG-009", Large},
                  {"conflict.resolveall", QT_TRANSLATE_NOOP("RibbonLayout", "Resolve All"), "MRG-009", Small},
              }},
             {"ribbonGroupThreeWay", QT_TRANSLATE_NOOP("RibbonLayout", "Three-Way"),
              {
                  {"threeway.base", QT_TRANSLATE_NOOP("RibbonLayout", "Base Pane"), "MRG-002", Small},
                  {"threeway.conflictsonly", QT_TRANSLATE_NOOP("RibbonLayout", "Conflicts Only"), "MRG-011", Small},
                  {"threeway.separatediff", QT_TRANSLATE_NOOP("RibbonLayout", "Separate Diff"), "MRG-012", Small},
              }},
             {"ribbonGroupApplyChange", QT_TRANSLATE_NOOP("RibbonLayout", "Apply Change"),
              {
                  {"apply.toleft", QT_TRANSLATE_NOOP("RibbonLayout", "Copy to Left"), "TXT-030", Large},
                  {"apply.toright", QT_TRANSLATE_NOOP("RibbonLayout", "Copy to Right"), "TXT-030", Large},
                  {"apply.tomerged", QT_TRANSLATE_NOOP("RibbonLayout", "Copy to Merged"), "MRG-008", Small},
                  {"apply.deleteblock", QT_TRANSLATE_NOOP("RibbonLayout", "Delete Block"), "TXT-030", Small},
              }},
         }},

        {"ribbonEditPage", QT_TRANSLATE_NOOP("RibbonLayout", "Edit"),
         {
             {"ribbonGroupUndo", QT_TRANSLATE_NOOP("RibbonLayout", "Undo"),
              {
                  {"edit.undo", QT_TRANSLATE_NOOP("RibbonLayout", "Undo"), "TXT-029", Large},
                  {"edit.redo", QT_TRANSLATE_NOOP("RibbonLayout", "Redo"), "TXT-029", Small},
                  {"edit.revert", QT_TRANSLATE_NOOP("RibbonLayout", "Revert File"), "SESS-018", Small},
              }},
             {"ribbonGroupFind", QT_TRANSLATE_NOOP("RibbonLayout", "Find"),
              {
                  {"edit.find", QT_TRANSLATE_NOOP("RibbonLayout", "Find"), "TXT-028", Large},
                  {"edit.findnext", QT_TRANSLATE_NOOP("RibbonLayout", "Find Next"), "TXT-028", Small},
                  {"edit.replace", QT_TRANSLATE_NOOP("RibbonLayout", "Replace"), "TXT-028", Small},
              }},
             {"ribbonGroupSelection", QT_TRANSLATE_NOOP("RibbonLayout", "Selection"),
              {
                  {"edit.columnmode", QT_TRANSLATE_NOOP("RibbonLayout", "Column Mode"), "TXT-035", Small},
                  {"edit.selectdiff", QT_TRANSLATE_NOOP("RibbonLayout", "Select Difference"), "DIR-018", Small},
                  {"edit.bookmark", QT_TRANSLATE_NOOP("RibbonLayout", "Toggle Bookmark"), "TXT-027", Small},
              }},
             {"ribbonGroupTransform", QT_TRANSLATE_NOOP("RibbonLayout", "Transform"),
              {
                  {"transform.upper", QT_TRANSLATE_NOOP("RibbonLayout", "To Upper"), "TXT-031", Small},
                  {"transform.lower", QT_TRANSLATE_NOOP("RibbonLayout", "To Lower"), "TXT-031", Small},
                  {"transform.sortlines", QT_TRANSLATE_NOOP("RibbonLayout", "Sort Lines"), "TXT-031", Small},
                  {"transform.trimtrailing", QT_TRANSLATE_NOOP("RibbonLayout", "Trim Trailing"), "TXT-031", Small},
              }},
             {"ribbonGroupWhitespace", QT_TRANSLATE_NOOP("RibbonLayout", "Whitespace"),
              {
                  {"whitespace.tabtospaces", QT_TRANSLATE_NOOP("RibbonLayout", "Tabs to Spaces"), "TXT-017", Small},
                  {"whitespace.spacestotab", QT_TRANSLATE_NOOP("RibbonLayout", "Spaces to Tabs"), "TXT-017", Small},
                  {"whitespace.showinvisible", QT_TRANSLATE_NOOP("RibbonLayout", "Show Invisible"), "TXT-033", Small},
              }},
         }},

        {"ribbonViewPage", QT_TRANSLATE_NOOP("RibbonLayout", "View"),
         {
             {"ribbonGroupLayout", QT_TRANSLATE_NOOP("RibbonLayout", "Layout"),
              {
                  {"view.sidebyside", QT_TRANSLATE_NOOP("RibbonLayout", "Side by Side"), "TXT-019", Large},
                  {"view.stacked", QT_TRANSLATE_NOOP("RibbonLayout", "Stacked"), "TXT-020", Small},
                  {"view.single", QT_TRANSLATE_NOOP("RibbonLayout", "Single Pane"), "TXT-021", Small},
                  {"view.inline", QT_TRANSLATE_NOOP("RibbonLayout", "All Inline"), "TXT-021", Small},
              }},
             {"ribbonGroupDisplayFilters", QT_TRANSLATE_NOOP("RibbonLayout", "Display Filters"),
              {
                  {"view.onlydifference", QT_TRANSLATE_NOOP("RibbonLayout", "Only Differences"), "TXT-026", Small},
                  {"view.onlysame", QT_TRANSLATE_NOOP("RibbonLayout", "Only Matches"), "TXT-026", Small},
                  {"view.onlyorphan", QT_TRANSLATE_NOOP("RibbonLayout", "Only Orphans"), "DIR-017", Small},
                  {"view.contextlines", QT_TRANSLATE_NOOP("RibbonLayout", "Context Lines"), "TXT-026", Small},
              }},
             {"ribbonGroupColoring", QT_TRANSLATE_NOOP("RibbonLayout", "Coloring"),
              {
                  {"view.colors", QT_TRANSLATE_NOOP("RibbonLayout", "Colors"), "UI-028", Small},
                  {"view.theme", QT_TRANSLATE_NOOP("RibbonLayout", "Theme"), "UI-028", Small},
                  {"view.font", QT_TRANSLATE_NOOP("RibbonLayout", "Fonts"), "OPT-004", Small},
              }},
             {"ribbonGroupZoom", QT_TRANSLATE_NOOP("RibbonLayout", "Zoom"),
              {
                  {"view.zoomin", QT_TRANSLATE_NOOP("RibbonLayout", "Zoom In"), "IMG-004", Small},
                  {"view.zoomout", QT_TRANSLATE_NOOP("RibbonLayout", "Zoom Out"), "IMG-004", Small},
                  {"view.zoomreset", QT_TRANSLATE_NOOP("RibbonLayout", "Reset Zoom"), "IMG-004", Small},
                  {"view.fitwidth", QT_TRANSLATE_NOOP("RibbonLayout", "Fit Width"), "IMG-004", Small},
              }},
             {"ribbonGroupPanes", QT_TRANSLATE_NOOP("RibbonLayout", "Panes"),
              {
                  {"view.overviewbar", QT_TRANSLATE_NOOP("RibbonLayout", "Overview Bar"), "TXT-023", Small},
                  {"view.linenumbers", QT_TRANSLATE_NOOP("RibbonLayout", "Line Numbers"), "TXT-024", Small},
                  {"view.outputpane", QT_TRANSLATE_NOOP("RibbonLayout", "Output Pane"), "SESS-020", Small},
                  {"view.minimizeribbon", QT_TRANSLATE_NOOP("RibbonLayout", "Minimize Ribbon"), "UI-002", Small},
              }},
         }},

        {"ribbonFilterPage", QT_TRANSLATE_NOOP("RibbonLayout", "Filter"),
         {
             {"ribbonGroupFileMasks", QT_TRANSLATE_NOOP("RibbonLayout", "File Masks"),
              {
                  {"filter.include", QT_TRANSLATE_NOOP("RibbonLayout", "Include"), "FILT-001", Large},
                  {"filter.exclude", QT_TRANSLATE_NOOP("RibbonLayout", "Exclude"), "FILT-001", Large},
                  {"filter.presets", QT_TRANSLATE_NOOP("RibbonLayout", "Presets"), "FILT-007", Small},
              }},
             {"ribbonGroupNameFilters", QT_TRANSLATE_NOOP("RibbonLayout", "Name Filters"),
              {
                  {"filter.regex", QT_TRANSLATE_NOOP("RibbonLayout", "Regular Expression"), "FILT-002", Small},
                  {"filter.casesensitive", QT_TRANSLATE_NOOP("RibbonLayout", "Case Sensitive"), "FILT-001", Small},
                  {"filter.combine", QT_TRANSLATE_NOOP("RibbonLayout", "Combine Mode"), "FILT-002", Small},
              }},
             {"ribbonGroupAttributeFilters", QT_TRANSLATE_NOOP("RibbonLayout", "Attributes"),
              {
                  {"filter.bysize", QT_TRANSLATE_NOOP("RibbonLayout", "By Size"), "FILT-003", Small},
                  {"filter.bytime", QT_TRANSLATE_NOOP("RibbonLayout", "By Time"), "FILT-003", Small},
                  {"filter.byattr", QT_TRANSLATE_NOOP("RibbonLayout", "By Attribute"), "FILT-003", Small},
              }},
             {"ribbonGroupContentFilters", QT_TRANSLATE_NOOP("RibbonLayout", "Content"),
              {
                  {"filter.linefilters", QT_TRANSLATE_NOOP("RibbonLayout", "Line Filters"), "FILT-004", Small},
                  {"filter.keybytes", QT_TRANSLATE_NOOP("RibbonLayout", "Key Bytes"), "FILT-004", Small},
                  {"filter.finalexpression", QT_TRANSLATE_NOOP("RibbonLayout", "Effective Filter"), "FILT-005", Small},
              }},
         }},

        {"ribbonSessionPage", QT_TRANSLATE_NOOP("RibbonLayout", "Session"),
         {
             {"ribbonGroupSessionProperties", QT_TRANSLATE_NOOP("RibbonLayout", "Properties"),
              {
                  {"session.properties", QT_TRANSLATE_NOOP("RibbonLayout", "Session Properties"), "SESS-008", Large},
                  {"session.rename", QT_TRANSLATE_NOOP("RibbonLayout", "Rename Session"), "SESS-004", Small},
              }},
             {"ribbonGroupSessionLoad", QT_TRANSLATE_NOOP("RibbonLayout", "Load"),
              {
                  {"session.browse", QT_TRANSLATE_NOOP("RibbonLayout", "Browse Sessions"), "SESS-004", Small},
                  {"session.fromcommandline", QT_TRANSLATE_NOOP("RibbonLayout", "From Command Line"), "SESS-016", Small},
              }},
             {"ribbonGroupSessionDefaults", QT_TRANSLATE_NOOP("RibbonLayout", "Defaults"),
              {
                  {"session.setasdefault", QT_TRANSLATE_NOOP("RibbonLayout", "Set as Default"), "SESS-015", Small},
                  {"session.resetfromdefault", QT_TRANSLATE_NOOP("RibbonLayout", "Reset from Default"), "SESS-015", Small},
              }},
             {"ribbonGroupSessionRules", QT_TRANSLATE_NOOP("RibbonLayout", "Rules"),
              {
                  {"session.importrules", QT_TRANSLATE_NOOP("RibbonLayout", "Import Rules"), "SESS-008", Small},
                  {"session.exportrules", QT_TRANSLATE_NOOP("RibbonLayout", "Export Rules"), "SESS-008", Small},
              }},
             {"ribbonGroupWorkspaces", QT_TRANSLATE_NOOP("RibbonLayout", "Workspaces"),
              {
                  {"workspace.save", QT_TRANSLATE_NOOP("RibbonLayout", "Save Workspace"), "SESS-011", Small},
                  {"workspace.open", QT_TRANSLATE_NOOP("RibbonLayout", "Open Workspace"), "SESS-011", Small},
              }},
         }},

        {"ribbonReportPage", QT_TRANSLATE_NOOP("RibbonLayout", "Report"),
         {
             {"ribbonGroupReportOutput", QT_TRANSLATE_NOOP("RibbonLayout", "Output"),
              {
                  {"report.file", QT_TRANSLATE_NOOP("RibbonLayout", "To File"), "RPT-002", Large},
                  {"report.clipboard", QT_TRANSLATE_NOOP("RibbonLayout", "To Clipboard"), "RPT-002", Small},
                  {"report.print", QT_TRANSLATE_NOOP("RibbonLayout", "Print"), "RPT-004", Small},
                  {"report.preview", QT_TRANSLATE_NOOP("RibbonLayout", "Print Preview"), "RPT-004", Small},
              }},
             {"ribbonGroupReportLayout", QT_TRANSLATE_NOOP("RibbonLayout", "Layout"),
              {
                  {"report.summary", QT_TRANSLATE_NOOP("RibbonLayout", "Summary"), "RPT-006", Small},
                  {"report.sidebyside", QT_TRANSLATE_NOOP("RibbonLayout", "Side by Side"), "RPT-001", Small},
                  {"report.statistical", QT_TRANSLATE_NOOP("RibbonLayout", "Statistical"), "RPT-005", Small},
              }},
             {"ribbonGroupReportOptions", QT_TRANSLATE_NOOP("RibbonLayout", "Options"),
              {
                  {"report.includesame", QT_TRANSLATE_NOOP("RibbonLayout", "Include Matches"), "RPT-003", Small},
                  {"report.includeorphan", QT_TRANSLATE_NOOP("RibbonLayout", "Include Orphans"), "RPT-003", Small},
                  {"report.includecrc", QT_TRANSLATE_NOOP("RibbonLayout", "Include CRC"), "RPT-003", Small},
              }},
             {"ribbonGroupReportSpecialized", QT_TRANSLATE_NOOP("RibbonLayout", "Reports"),
              {
                  {"report.textcompare", QT_TRANSLATE_NOOP("RibbonLayout", "Text Compare"), "TXT-037", Small},
                  {"report.foldercompare", QT_TRANSLATE_NOOP("RibbonLayout", "Folder Compare"), "DIR-035", Small},
                  {"report.foldersync", QT_TRANSLATE_NOOP("RibbonLayout", "Folder Sync"), "SYNC-008", Small},
                  {"report.foldermerge", QT_TRANSLATE_NOOP("RibbonLayout", "Folder Merge"), "FMG-008", Small},
              }},
         }},

        {"ribbonToolsPage", QT_TRANSLATE_NOOP("RibbonLayout", "Tools"),
         {
             {"ribbonGroupExternalTools", QT_TRANSLATE_NOOP("RibbonLayout", "External Tools"),
              {
                  {"tools.reveal", QT_TRANSLATE_NOOP("RibbonLayout", "Reveal in Explorer"), "DIR-028", Large},
                  {"tools.openwith", QT_TRANSLATE_NOOP("RibbonLayout", "Open With"), "DIR-028", Small},
                  {"tools.customtool", QT_TRANSLATE_NOOP("RibbonLayout", "Custom Tool"), "DIR-030", Small},
              }},
             {"ribbonGroupCustomCommands", QT_TRANSLATE_NOOP("RibbonLayout", "Custom Commands"),
              {
                  {"tools.customcommands", QT_TRANSLATE_NOOP("RibbonLayout", "Manage Commands"), "OPT-011", Small},
                  {"tools.variables", QT_TRANSLATE_NOOP("RibbonLayout", "Insert Variable"), "DIR-030", Small},
              }},
             {"ribbonGroupOptions", QT_TRANSLATE_NOOP("RibbonLayout", "Options"),
              {
                  {"tools.options", QT_TRANSLATE_NOOP("RibbonLayout", "Program Options"), "OPT-001", Large},
                  {"tools.formats", QT_TRANSLATE_NOOP("RibbonLayout", "File Formats"), "FMT-002", Small},
                  {"tools.shortcuts", QT_TRANSLATE_NOOP("RibbonLayout", "Customize Shortcuts"), "UI-027", Small},
                  {"tools.snapshots", QT_TRANSLATE_NOOP("RibbonLayout", "Snapshots"), "SNAP-004", Small},
              }},
             {"ribbonGroupOperations", QT_TRANSLATE_NOOP("RibbonLayout", "Operations"),
              {
                  {"ops.copytoleft", QT_TRANSLATE_NOOP("RibbonLayout", "Copy to Left"), "DIR-019", Small},
                  {"ops.copytoright", QT_TRANSLATE_NOOP("RibbonLayout", "Copy to Right"), "DIR-019", Small},
                  {"ops.mirror", QT_TRANSLATE_NOOP("RibbonLayout", "Mirror"), "DIR-025", Small},
                  {"ops.delete", QT_TRANSLATE_NOOP("RibbonLayout", "Delete"), "DIR-022", Small},
                  {"ops.rename", QT_TRANSLATE_NOOP("RibbonLayout", "Rename"), "DIR-023", Small},
                  {"ops.newfolder", QT_TRANSLATE_NOOP("RibbonLayout", "New Folder"), "DIR-024", Small},
                  {"ops.properties", QT_TRANSLATE_NOOP("RibbonLayout", "Properties"), "DIR-026", Small},
                  {"ops.operationlog", QT_TRANSLATE_NOOP("RibbonLayout", "Operation Log"), "DIR-027", Small},
              }},
         }},

        {"ribbonHelpPage", QT_TRANSLATE_NOOP("RibbonLayout", "Help"),
         {
             {"ribbonGroupHelp", QT_TRANSLATE_NOOP("RibbonLayout", "Help"),
              {
                  {"help.manual", QT_TRANSLATE_NOOP("RibbonLayout", "User Manual"), "DOC-001", Small},
                  {"help.shortcutref", QT_TRANSLATE_NOOP("RibbonLayout", "Shortcut Reference"), "DOC-004", Small},
                  {"help.maskref", QT_TRANSLATE_NOOP("RibbonLayout", "Mask Reference"), "DOC-004", Small},
                  {"help.commandline", QT_TRANSLATE_NOOP("RibbonLayout", "Command Line"), "DOC-002", Small},
              }},
             {"ribbonGroupDiagnostics", QT_TRANSLATE_NOOP("RibbonLayout", "Diagnostics"),
              {
                  {"help.loglevel", QT_TRANSLATE_NOOP("RibbonLayout", "Log Level"), "OPT-010", Small},
                  {"help.openlogdir", QT_TRANSLATE_NOOP("RibbonLayout", "Open Log Folder"), "OPT-010", Small},
                  {"help.exportdiagnostics", QT_TRANSLATE_NOOP("RibbonLayout", "Export Diagnostics"), "OPT-010", Small},
                  {"help.resetsettings", QT_TRANSLATE_NOOP("RibbonLayout", "Reset Settings"), "OPT-012", Small},
              }},
             {"ribbonGroupAbout", QT_TRANSLATE_NOOP("RibbonLayout", "About"),
              {
                  {"help.about", QT_TRANSLATE_NOOP("RibbonLayout", "About LqCompare"), "UI-016", Large},
                  {"help.licenses", QT_TRANSLATE_NOOP("RibbonLayout", "Third-Party Licenses"), "ENG-013", Small},
                  {"help.validate", QT_TRANSLATE_NOOP("RibbonLayout", "Validate Commands"), "UI-023", Small},
              }},
         }},
    };
    return table;
}

} // namespace

int RibbonLayout::pageCount()
{
    return layout().size();
}

int RibbonLayout::groupCount()
{
    int total = 0;
    for (const PageSpec &page : layout()) {
        total += page.groups.size();
    }
    return total;
}

int RibbonLayout::buttonCount()
{
    int total = 0;
    for (const PageSpec &page : layout()) {
        for (const GroupSpec &group : page.groups) {
            total += group.buttons.size();
        }
    }
    return total;
}

int RibbonLayout::build(LqRibbon::RibbonBar *bar)
{
    if (!bar) {
        return 0;
    }

    const CommandRegistry &registry = CommandRegistry::instance();

    // 一条命令对应唯一 QAction：这样同一命令出现在 Ribbon、快速访问栏、
    // 菜单与快捷键上时，启用状态与文本严格一致（UI-024）。
    QHash<QString, QAction *> actions;
    int created = 0;

    const auto actionFor = [&](const ButtonSpec &spec) -> QAction * {
        const QString id = QString::fromLatin1(spec.commandId);
        const auto existing = actions.constFind(id);
        if (existing != actions.constEnd()) {
            return existing.value();
        }

        const QString text = QCoreApplication::translate("RibbonLayout", spec.text);
        const QString actionId = QString::fromLatin1(spec.actionId);

        auto *action = new QAction(bar);
        action->setObjectName(QStringLiteral("cmd_") + id);
        action->setText(text);
        action->setData(actionId);

        const Command *command = registry.find(id);
        if (command) {
            // 已登记：图标、快捷键、两段式 tooltip 都来自注册表（单一出口）。
            action->setIcon(QIcon(command->icon));
            action->setShortcut(command->shortcut);
            action->setToolTip(QStringLiteral("%1\n%2\n[%3]").arg(command->text,
                                                                command->description, actionId));
            action->setStatusTip(command->description);
            if (command->isImplemented()) {
                QObject::connect(action, &QAction::triggered, bar,
                                 [id]() { CommandRegistry::instance().trigger(id); });
            }
        }

        if (!command || !command->isImplemented()) {
            // 尚未实现的命令：保留按钮形状，点击后说明它对应哪条规格条目。
            // 这不是最终形态，而是让界面骨架先跑起来、行为按 issue 逐个补齐。
            action->setToolTip(QStringLiteral("%1\n尚未实现\n规格条目：%2").arg(text, actionId));
            action->setStatusTip(QStringLiteral("尚未实现，见规格条目 %1").arg(actionId));
            QObject::connect(action, &QAction::triggered, bar, [text, actionId, id]() {
                QMessageBox::information(
                    nullptr, QCoreApplication::translate("RibbonLayout", "Not Implemented Yet"),
                    QCoreApplication::translate(
                        "RibbonLayout",
                        "Command: %1\nCommand ID: %2\nSpec entry: %3\n\n"
                        "This command is not implemented yet. "
                        "See the matching GitHub issue for its progress.")
                        .arg(text, id, actionId));
            });
        }

        actions.insert(id, action);
        ++created;
        return action;
    };

    for (const PageSpec &pageSpec : layout()) {
        LqRibbon::RibbonPage *page =
            bar->addPage(QCoreApplication::translate("RibbonLayout", pageSpec.title));
        if (!page) {
            continue;
        }
        page->setObjectName(QString::fromLatin1(pageSpec.objectName));

        for (const GroupSpec &groupSpec : pageSpec.groups) {
            LqRibbon::RibbonGroup *group =
                page->addGroup(QCoreApplication::translate("RibbonLayout", groupSpec.title));
            if (!group) {
                continue;
            }
            group->setObjectName(QString::fromLatin1(groupSpec.objectName));

            for (const ButtonSpec &button : groupSpec.buttons) {
                QAction *action = actionFor(button);
                const Qt::ToolButtonStyle style = button.size == Large
                                                      ? Qt::ToolButtonTextUnderIcon
                                                      : Qt::ToolButtonTextBesideIcon;
                group->addAction(action, style);
            }
        }
    }

    LQCOMPARE_INFO("ribbon", QStringLiteral("Ribbon 构建完成：%1 页 / %2 组 / %3 个按钮")
                                 .arg(pageCount())
                                 .arg(groupCount())
                                 .arg(created));
    return created;
}

} // namespace LqCompare
