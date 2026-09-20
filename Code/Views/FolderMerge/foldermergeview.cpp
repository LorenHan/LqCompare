#include "foldermergeview.h"
#include "foldermergesession.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace LqCompare {
namespace {
QString sideText(const Folder::Side &side)
{
    QString text = FolderMerge::kindLabel(side.kind);
    if (side.kind == Folder::Kind::File) text += QStringLiteral(" · %1 B").arg(side.info.size);
    if (!side.error.isEmpty()) text += QStringLiteral(" · ") + side.error;
    return text;
}
}

FolderMergeView::FolderMergeView(FolderMergeSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("folderMergeView"));
    auto *layout = new QVBoxLayout(this);
    auto *paths = new QGridLayout;
    auto addPath = [this, paths, session](int row, const QString &label, const QString &name, bool output) {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(name);
        auto *browse = new QPushButton(tr("选择…"), this);
        connect(browse, &QPushButton::clicked, this, [this, edit, output] {
            const QString selected = QFileDialog::getExistingDirectory(this,
                output ? tr("选择输出目录（也可直接输入尚不存在的路径）") : tr("选择来源目录"), edit->text());
            if (!selected.isEmpty()) edit->setText(selected);
        });
        paths->addWidget(new QLabel(label, this), row, 0);
        paths->addWidget(edit, row, 1);
        paths->addWidget(browse, row, 2);
        connect(session, &FolderMergeSession::scanningChanged, browse, [browse](bool scanning) { browse->setEnabled(!scanning); });
        return edit;
    };
    m_left = addPath(0, tr("左侧目录"), QStringLiteral("folderMergeLeftPath"), false);
    m_base = addPath(1, tr("祖先目录（可选）"), QStringLiteral("folderMergeBasePath"), false);
    m_right = addPath(2, tr("右侧目录"), QStringLiteral("folderMergeRightPath"), false);
    m_output = addPath(3, tr("输出目录（仅预演）"), QStringLiteral("folderMergeOutputPath"), true);
    layout->addLayout(paths);

    m_baseNotice = new QLabel(this);
    m_baseNotice->setObjectName(QStringLiteral("folderMergeBaseNotice"));
    m_baseNotice->setWordWrap(true);
    m_baseNotice->setStyleSheet(QStringLiteral("color:#9b5d00; font-weight:bold;"));
    layout->addWidget(m_baseNotice);
    auto *scanBar = new QHBoxLayout;
    m_scan = new QPushButton(tr("扫描 / 重新预演"), this);
    m_scan->setObjectName(QStringLiteral("folderMergeScan"));
    m_cancel = new QPushButton(tr("取消扫描"), this);
    m_cancel->setObjectName(QStringLiteral("folderMergeCancel"));
    auto *preview = new QPushButton(tr("查看 / 复制预演清单"), this);
    auto *execute = new QPushButton(tr("应用合并结果（不可用）"), this);
    execute->setObjectName(QStringLiteral("folderMergeExecute"));
    execute->setEnabled(false);
    scanBar->addWidget(m_scan);
    scanBar->addWidget(m_cancel);
    scanBar->addWidget(preview);
    scanBar->addWidget(execute);
    scanBar->addStretch();
    layout->addLayout(scanBar);
    m_executionNotice = new QLabel(this);
    m_executionNotice->setObjectName(QStringLiteral("folderMergeExecutionNotice"));
    m_executionNotice->setWordWrap(true);
    m_executionNotice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_executionNotice);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("folderMergeSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary);

    auto *decisions = new QHBoxLayout;
    auto addChoice = [this, decisions](const QString &label, FolderMerge::Decision choice, const QString &name) {
        auto *button = new QPushButton(label, this);
        button->setObjectName(name);
        decisions->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, choice] { choose(choice); });
        return button;
    };
    m_takeLeft = addChoice(tr("取左侧"), FolderMerge::Decision::TakeLeft, QStringLiteral("folderMergeTakeLeft"));
    m_takeBase = addChoice(tr("取祖先"), FolderMerge::Decision::TakeBase, QStringLiteral("folderMergeTakeBase"));
    m_takeRight = addChoice(tr("取右侧"), FolderMerge::Decision::TakeRight, QStringLiteral("folderMergeTakeRight"));
    m_ignore = addChoice(tr("忽略 / 不输出"), FolderMerge::Decision::Ignore, QStringLiteral("folderMergeIgnore"));
    m_reset = new QPushButton(tr("恢复自动决策"), this);
    m_reset->setObjectName(QStringLiteral("folderMergeReset"));
    m_textMerge = new QPushButton(tr("打开文本合并…"), this);
    m_textMerge->setObjectName(QStringLiteral("folderMergeTextMerge"));
    decisions->addWidget(m_reset);
    decisions->addWidget(m_textMerge);
    decisions->addStretch();
    layout->addLayout(decisions);

    auto *policyBar = new QHBoxLayout;
    policyBar->addWidget(new QLabel(tr("目录子树策略："), this));
    m_directoryPolicy = new QComboBox(this);
    m_directoryPolicy->setObjectName(QStringLiteral("folderMergeDirectoryPolicy"));
    m_directoryPolicy->addItem(tr("保留已有人工决策，仅调整其余子项"), int(FolderMerge::DirectoryPolicy::PreserveManual));
    m_directoryPolicy->addItem(tr("遇到不同人工决策时中止整个操作"), int(FolderMerge::DirectoryPolicy::RejectManualConflicts));
    m_directoryPolicy->addItem(tr("覆盖子项人工决策（操作前确认）"), int(FolderMerge::DirectoryPolicy::OverwriteManual));
    policyBar->addWidget(m_directoryPolicy);
    policyBar->addStretch();
    m_conflictsOnly = new QCheckBox(tr("只显示未解决冲突（含父目录）"), this);
    m_conflictsOnly->setObjectName(QStringLiteral("folderMergeConflictsOnly"));
    policyBar->addWidget(m_conflictsOnly);
    layout->addLayout(policyBar);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("folderMergeTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({tr("相对路径"), tr("左侧"), tr("祖先"), tr("右侧"), tr("合并输出计划 / 来源")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(0, 230);
    for (int i = 1; i <= 3; ++i) m_tree->setColumnWidth(i, 140);
    layout->addWidget(m_tree, 1);
    m_details = new QLabel(tr("选择条目查看判定原因。目录决策会处理整个子树。"), this);
    m_details->setObjectName(QStringLiteral("folderMergeDetails"));
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);

    connect(m_scan, &QPushButton::clicked, this, &FolderMergeView::scan);
    connect(m_cancel, &QPushButton::clicked, session, &FolderMergeSession::cancelScan);
    connect(preview, &QPushButton::clicked, this, &FolderMergeView::showPreview);
    connect(m_reset, &QPushButton::clicked, this, &FolderMergeView::resetChoice);
    connect(m_textMerge, &QPushButton::clicked, this, [this] {
        if (!m_session) return;
        QString error;
        if (!m_session->requestTextMerge(selectedPath(), &error)) showFailure(error);
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &FolderMergeView::refreshActions);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
        if (m_textMerge->isEnabled()) m_textMerge->click();
    });
    connect(m_conflictsOnly, &QCheckBox::toggled, this, &FolderMergeView::applyFilter);
    connect(session, &FolderMergeSession::planChanged, this, &FolderMergeView::refreshPlan);
    connect(session, &FolderMergeSession::pathsChanged, this, &FolderMergeView::refreshPaths);
    connect(session, &FolderMergeSession::scanningChanged, this, &FolderMergeView::refreshActions);
    connect(session, &CompareSession::statusTextChanged, m_summary, &QLabel::setText);
    connect(session, &CompareSession::stateChanged, this, &FolderMergeView::refreshActions);
    connect(session, &QObject::destroyed, this, [this] { setEnabled(false); });
    refreshPaths();
    refreshPlan();
}

QString FolderMergeView::selectedPath() const
{
    const auto *item = m_tree->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void FolderMergeView::refreshPaths()
{
    if (!m_session) return;
    m_left->setText(m_session->leftPath());
    m_base->setText(m_session->basePath());
    m_right->setText(m_session->rightPath());
    m_output->setText(m_session->outputPath());
}

void FolderMergeView::refreshPlan()
{
    if (!m_session) return;
    const QString selected = selectedPath();
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->isExpanded()) expanded.insert((*it)->data(0, Qt::UserRole).toString());
    m_tree->clear();
    const auto &plan = m_session->plan();
    QMap<QString, QTreeWidgetItem *> nodes;
    for (const auto &entry : plan.entries) {
        const int slash = entry.relativePath.lastIndexOf(QLatin1Char('/'));
        const QString parentPath = slash < 0 ? QString() : entry.relativePath.left(slash);
        auto *item = nodes.contains(parentPath) ? new QTreeWidgetItem(nodes.value(parentPath)) : new QTreeWidgetItem(m_tree);
        nodes.insert(entry.relativePath, item);
        item->setData(0, Qt::UserRole, entry.relativePath);
        item->setText(0, slash < 0 ? entry.relativePath : entry.relativePath.mid(slash + 1));
        item->setText(1, sideText(entry.left));
        item->setText(2, plan.hasBase ? sideText(entry.base) : tr("无祖先"));
        item->setText(3, sideText(entry.right));
        QString output = FolderMerge::decisionLabel(entry.decision);
        const Folder::Side *source = entry.decision == FolderMerge::Decision::TakeLeft ? &entry.left
            : entry.decision == FolderMerge::Decision::TakeRight ? &entry.right
            : entry.decision == FolderMerge::Decision::TakeBase ? &entry.base : nullptr;
        if (!entry.manual && entry.isDirectory() && source && source->kind == Folder::Kind::Directory)
            output = tr("合并目录（子项分别决策）");
        output += entry.manual ? tr(" · 人工") : tr(" · 自动");
        if (entry.unresolved()) output += tr(" · 冲突：%1").arg(FolderMerge::conflictLabel(entry.conflict));
        if (entry.descendantConflicts) output += tr(" · 子项冲突 %1").arg(entry.descendantConflicts);
        if (entry.blockedByAncestor) output += tr(" · 受父级条目阻挡");
        item->setText(4, output);
        item->setData(0, Qt::UserRole + 1, entry.unresolved() || entry.descendantConflicts > 0);
        for (int column = 0; column < 5; ++column) {
            item->setToolTip(column, entry.relativePath + QLatin1Char('\n') + entry.explanation);
            if (entry.unresolved() || entry.descendantConflicts) item->setForeground(column, QColor(QStringLiteral("#a33a24")));
        }
        if (expanded.contains(entry.relativePath) || entry.descendantConflicts) item->setExpanded(true);
        if (entry.relativePath == selected) m_tree->setCurrentItem(item);
    }
    m_baseNotice->setText(plan.hasBase ? tr("三方只读预演：所有决定仅保存在内存计划中，输入目录不变。")
        : tr("无祖先 · 当前为两路合并：无法判断哪一侧发生变化，不同内容和单侧条目必须人工决定。"));
    m_executionNotice->setText(tr("执行已禁用：%1").arg(plan.executionDisabledReason()));
    m_summary->setText(m_session->statusText());
    applyFilter();
    refreshActions();
}

void FolderMergeView::refreshActions()
{
    if (!m_session) { setEnabled(false); return; }
    const bool scanning = m_session->isScanning();
    const bool open = m_session->state() == CompareSession::State::Open;
    const bool closed = m_session->state() == CompareSession::State::Closed;
    m_scan->setEnabled(!scanning && !closed);
    m_cancel->setEnabled(scanning);
    for (auto *edit : {m_base, m_left, m_right, m_output}) edit->setEnabled(!scanning && !closed);
    const auto *entry = m_session->plan().find(selectedPath());
    const bool available = entry && open && !scanning && !closed;
    m_takeLeft->setEnabled(available);
    m_takeRight->setEnabled(available);
    m_takeBase->setEnabled(available && m_session->plan().hasBase);
    m_ignore->setEnabled(available);
    m_reset->setEnabled(available);
    m_directoryPolicy->setEnabled(available && entry->isDirectory());
    m_textMerge->setEnabled(available && entry->canRequestTextMerge() && m_session->plan().error.isEmpty());
    m_details->setText(entry ? tr("%1\n%2\n判定类型：%3 · 当前来源：%4")
        .arg(entry->relativePath, entry->explanation, FolderMerge::conflictLabel(entry->conflict),
             FolderMerge::decisionLabel(entry->decision)) : tr("选择条目查看判定原因。目录决策会处理整个子树。"));
}

void FolderMergeView::applyFilter()
{
    if (!m_session) return;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        (*it)->setHidden(m_conflictsOnly->isChecked() && !(*it)->data(0, Qt::UserRole + 1).toBool());
    }
    // A resolved item disappears under the conflict filter. Do not leave an
    // invisible current row as the target of the next choice.
    if (m_tree->currentItem() && m_tree->currentItem()->isHidden()) m_tree->setCurrentItem(nullptr);
    refreshActions();
}

void FolderMergeView::scan()
{
    if (!m_session) return;
    bool discard = false;
    if (m_session->isDirty() || m_session->plan().manualCount()) {
        const auto response = QMessageBox::warning(this, tr("丢弃未应用决策并重扫？"),
            tr("重扫会丢弃 %1 项人工决策。计划未应用，所有输入目录和输出目录均不会因重扫而修改。")
                .arg(m_session->plan().manualCount()), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (response != QMessageBox::Discard) return;
        discard = true;
    }
    QString error;
    if (!m_session->setPaths(m_base->text(), m_left->text(), m_right->text(), m_output->text(), &error, discard)) {
        showFailure(error); return;
    }
    const bool ok = m_session->state() == CompareSession::State::Open
        ? m_session->rescan(discard, &error) : m_session->open(&error);
    if (!ok) showFailure(error);
}

void FolderMergeView::choose(FolderMerge::Decision decision)
{
    if (!m_session) return;
    const QString path = selectedPath();
    const auto *entry = m_session->plan().find(path);
    if (!entry) return;
    const auto policy = static_cast<FolderMerge::DirectoryPolicy>(m_directoryPolicy->currentData().toInt());
    if (entry->isDirectory()) {
        int count = 0;
        QStringList manual;
        for (const auto &child : m_session->plan().entries) {
            if (child.relativePath == path || child.relativePath.startsWith(path + QLatin1Char('/'))) {
                ++count;
                if (child.manual) manual.append(child.relativePath + QStringLiteral(" — ") + FolderMerge::decisionLabel(child.decision));
            }
        }
        QMessageBox confirm(QMessageBox::Question, tr("确认目录子树决策"),
            tr("目录：%1\n范围：此目录及子树，共 %2 项（包含当前过滤隐藏项）。\n决策：%3\n策略：%4\n已有人工决策：%5 项。仅更新预演，不写入文件系统。")
                .arg(path).arg(count).arg(FolderMerge::decisionLabel(decision), m_directoryPolicy->currentText()).arg(manual.size()),
            QMessageBox::Ok | QMessageBox::Cancel, this);
        confirm.setDefaultButton(QMessageBox::Cancel);
        if (!manual.isEmpty()) confirm.setDetailedText(manual.join(QLatin1Char('\n')));
        if (confirm.exec() != QMessageBox::Ok) return;
    }
    QString error;
    if (!m_session->setDecision(path, decision, policy, &error)) showFailure(error);
}

void FolderMergeView::resetChoice()
{
    if (!m_session) return;
    const QString path = selectedPath();
    const auto *entry = m_session->plan().find(path);
    if (!entry) return;
    if (entry->isDirectory() && QMessageBox::question(this, tr("恢复整个子树的自动决策？"),
        tr("%1 及全部子项的人工决策将被移除，包含当前过滤隐藏项。文件系统不会变化。").arg(path),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok) return;
    QString error;
    if (!m_session->resetDecision(path, &error)) showFailure(error);
}

void FolderMergeView::showPreview()
{
    if (!m_session) return;
    const QString text = m_session->plan().previewText();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("合并预演清单（只读，未执行）"));
    dialog.resize(850, 550);
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setObjectName(QStringLiteral("folderMergePreviewText"));
    editor->setReadOnly(true);
    editor->setPlainText(text);
    layout->addWidget(editor);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *copy = buttons->addButton(tr("复制清单"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, &dialog, [text] { QApplication::clipboard()->setText(text); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void FolderMergeView::showFailure(const QString &message)
{
    m_details->setText(message);
    if (m_session) m_session->reportError(message);
}

}
