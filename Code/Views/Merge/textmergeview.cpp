#include "textmergeview.h"
#include "textmergesession.h"

#include <QAction>
#include <QFileDialog>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace LqCompare {
namespace {
class MergeEditor : public QPlainTextEdit {
public:
    explicit MergeEditor(TextMergeSession *session, QWidget *parent) : QPlainTextEdit(parent), m_session(session) {}
protected:
    bool event(QEvent *event) override {
        if (m_session && !m_session->usesLocalShortcuts() && event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
                // QPlainTextEdit accepts these even when its native undo stack
                // is disabled. Let the window's command binding handle them.
                event->ignore();
                return false;
            }
        }
        return QPlainTextEdit::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (m_session && m_session->usesLocalShortcuts()) {
            if (event->matches(QKeySequence::Undo)) { m_session->undo(); return; }
            if (event->matches(QKeySequence::Redo)) { m_session->redo(); return; }
        }
        QPlainTextEdit::keyPressEvent(event);
    }
private:
    QPointer<TextMergeSession> m_session;
};
QTextEdit::ExtraSelection selection(QPlainTextEdit *edit, int start, int length, const QColor &color)
{
    QTextEdit::ExtraSelection value;
    value.cursor = QTextCursor(edit->document());
    const int last = qMax(0, edit->document()->characterCount() - 1);
    value.cursor.setPosition(qBound(0, start, last));
    value.cursor.setPosition(qBound(0, start + length, last), QTextCursor::KeepAnchor);
    value.format.setBackground(color);
    value.format.setProperty(QTextFormat::FullWidthSelection, true);
    return value;
}
int linePosition(QPlainTextEdit *edit, int line)
{
    const auto block = edit->document()->findBlockByNumber(line);
    return block.isValid() ? block.position() : edit->document()->characterCount() - 1;
}
}
TextMergeView::TextMergeView(TextMergeSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("mergeActions"));
    layout->addWidget(bar);
    auto action = [&](const QString &label, const QString &name) {
        auto *result = bar->addAction(label); result->setObjectName(name); return result;
    };
    auto *previous = action(tr("Previous conflict"), QStringLiteral("mergePreviousConflict"));
    auto *next = action(tr("Next conflict"), QStringLiteral("mergeNextConflict"));
    connect(previous, &QAction::triggered, session, &TextMergeSession::previousConflict);
    connect(next, &QAction::triggered, session, &TextMergeSession::nextConflict);
    bar->addSeparator();
    auto resolve = [&](const QString &label, const QString &name, Merge::Resolution choice) {
        auto *result = action(label, name);
        connect(result, &QAction::triggered, this, [this, choice] {
            if (!m_session) return;
            QString error;
            if (!m_session->resolveCurrent(choice, &error)) showError(error);
        });
        return result;
    };
    m_acceptLeft = resolve(tr("Use left"), QStringLiteral("mergeAcceptLeft"), Merge::Resolution::Left);
    m_acceptRight = resolve(tr("Use right"), QStringLiteral("mergeAcceptRight"), Merge::Resolution::Right);
    m_acceptBase = resolve(tr("Use base"), QStringLiteral("mergeAcceptBase"), Merge::Resolution::Base);
    m_leftRight = resolve(tr("Left then right"), QStringLiteral("mergeAcceptLeftRight"), Merge::Resolution::LeftThenRight);
    m_rightLeft = resolve(tr("Right then left"), QStringLiteral("mergeAcceptRightLeft"), Merge::Resolution::RightThenLeft);
    m_resolve = resolve(tr("Mark resolved"), QStringLiteral("mergeMarkResolved"), Merge::Resolution::Manual);
    m_reopen = resolve(tr("Reopen conflict"), QStringLiteral("mergeReopenConflict"), Merge::Resolution::Unresolved);
    auto *editBar = new QToolBar(this);
    layout->addWidget(editBar);
    m_undo = editBar->addAction(tr("Undo")); m_undo->setObjectName(QStringLiteral("mergeUndo"));
    m_redo = editBar->addAction(tr("Redo")); m_redo->setObjectName(QStringLiteral("mergeRedo"));
    connect(m_undo, &QAction::triggered, session, &TextMergeSession::undo);
    connect(m_redo, &QAction::triggered, session, &TextMergeSession::redo);
    editBar->addSeparator();
    m_save = editBar->addAction(tr("Save merge output")); m_save->setObjectName(QStringLiteral("mergeSave"));
    m_save->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_save->setAutoRepeat(false);
    connect(session, &TextMergeSession::localShortcutsChanged, this, &TextMergeView::setUseLocalShortcuts);
    setUseLocalShortcuts(session->usesLocalShortcuts());
    addAction(m_save);
    auto *saveAs = editBar->addAction(tr("Save as…"));
    saveAs->setObjectName(QStringLiteral("mergeSaveAs"));
    connect(m_save, &QAction::triggered, this, [this] { saveOutput(false); });
    connect(saveAs, &QAction::triggered, this, [this] { saveOutput(true); });
    auto *showBase = editBar->addAction(tr("Show base"));
    showBase->setCheckable(true); showBase->setChecked(true);
    showBase->setObjectName(QStringLiteral("mergeShowBase"));
    auto *main = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(main, 1);
    m_conflicts = new QListWidget(main);
    m_conflicts->setObjectName(QStringLiteral("mergeConflictList"));
    m_conflicts->setMinimumWidth(160);
    auto *panes = new QSplitter(Qt::Horizontal, main);
    auto pane = [&](const QString &role, const QString &path, const QString &name, bool output,
                    QPlainTextEdit **editor) {
        auto *container = new QWidget(panes);
        auto *column = new QVBoxLayout(container);
        column->setContentsMargins(2, 0, 2, 0);
        auto *label = new QLabel(role + QStringLiteral("\n") + path, container);
        label->setWordWrap(true); label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setTextFormat(Qt::PlainText);
        column->addWidget(label);
        *editor = output ? new MergeEditor(session, container) : new QPlainTextEdit(container);
        (*editor)->setObjectName(name);
        (*editor)->setReadOnly(!output);
        (*editor)->setLineWrapMode(QPlainTextEdit::NoWrap);
        (*editor)->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        column->addWidget(*editor, 1);
        if (output) m_outputLabel = label;
        return container;
    };
    pane(tr("LEFT · read only"), session->leftPath(), QStringLiteral("mergeLeftPane"), false, &m_left);
    m_baseContainer = pane(tr("BASE · read only"), session->basePath(), QStringLiteral("mergeBasePane"), false, &m_base);
    pane(tr("RIGHT · read only"), session->rightPath(), QStringLiteral("mergeRightPane"), false, &m_right);
    pane(tr("MERGED OUTPUT · editable"), session->outputPath(), QStringLiteral("mergeOutputPane"), true, &m_output);
    m_output->setUndoRedoEnabled(false);
    main->setStretchFactor(0, 0); main->setStretchFactor(1, 1);
    main->setSizes({190, 1000});
    connect(showBase, &QAction::toggled, m_baseContainer, &QWidget::setVisible);
    m_status = new QLabel(this);
    m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);
    m_status->setObjectName(QStringLiteral("mergeStatus"));
    layout->addWidget(m_status);
    connect(session, &TextMergeSession::statusTextChanged, m_status, &QLabel::setText);
    connect(session, &TextMergeSession::mergeChanged, this, &TextMergeView::refresh);
    connect(session, &TextMergeSession::outputPathChanged, this, &TextMergeView::refresh);
    connect(session, &TextMergeSession::dirtyChanged, this, &TextMergeView::refresh);
    connect(session, &TextMergeSession::stateChanged, this, [this](CompareSession::State state) {
        setEnabled(state != CompareSession::State::Closed); refresh();
    });
    connect(session, &QObject::destroyed, this, [this] { setEnabled(false); });
    connect(session, &TextMergeSession::currentBlockChanged, this, [this] { if (!m_refreshing) { refresh(); highlight(true); } });
    connect(m_conflicts, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_refreshing || !m_session || row < 0) return;
        m_session->selectBlock(m_conflicts->item(row)->data(Qt::UserRole).toInt());
    });
    connect(m_output, &QPlainTextEdit::textChanged, this, [this] {
        if (m_refreshing || !m_session) return;
        QString error;
        if (!m_session->setOutputText(m_output->toPlainText(), &error)) { refresh(); showError(error); }
    });
    connect(m_output, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (m_refreshing || !m_session) return;
        // Cursor selection should change actions without jumping away from typing.
        m_refreshing = true;
        m_session->selectOutputPosition(m_output->textCursor().position());
        m_refreshing = false;
        // QTextEdit may emit cursorPositionChanged before textChanged while
        // typing. Refresh after the edit has reached the session model.
        QTimer::singleShot(0, this, [this] { refresh(); });
    });
    refresh();
    highlight(true);
}
void TextMergeView::setUseLocalShortcuts(bool enabled)
{
    m_save->setShortcut(enabled ? QKeySequence(QKeySequence::Save) : QKeySequence());
}
void TextMergeView::refresh()
{
    if (!m_session || m_refreshing) return;
    m_refreshing = true;
    auto setText = [](QPlainTextEdit *editor, const QString &text) {
        if (editor->toPlainText() == text) return;
        const QSignalBlocker blocker(editor);
        const int cursor = editor->textCursor().position();
        const int scroll = editor->verticalScrollBar()->value();
        editor->setPlainText(text);
        auto next = editor->textCursor(); next.setPosition(qMin(cursor, text.size())); editor->setTextCursor(next);
        editor->verticalScrollBar()->setValue(scroll);
    };
    setText(m_left, m_session->leftDocument().normalizedText());
    setText(m_base, m_session->baseDocument().normalizedText());
    setText(m_right, m_session->rightDocument().normalizedText());
    setText(m_output, m_session->outputText());
    m_outputLabel->setText(tr("MERGED OUTPUT · editable\n%1").arg(m_session->outputPath().isEmpty() ? tr("Choose a path when saving") : m_session->outputPath()));
    m_status->setText(m_session->statusText());
    m_conflicts->clear();
    const auto &blocks = m_session->mergeResult().blocks;
    const auto &ranges = m_session->outputRanges();
    const int current = m_session->currentBlock();
    int number = 0;
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].kind != Merge::Kind::Conflict) continue;
        const bool unresolved = ranges[i].resolution == Merge::Resolution::Unresolved;
        auto *item = new QListWidgetItem(tr("%1. %2\nLeft %3 · Right %4%5")
            .arg(++number).arg(unresolved ? tr("Unresolved") : tr("Resolved"))
            .arg(blocks[i].leftStart + 1).arg(blocks[i].rightStart + 1)
            .arg(ranges[i].manuallyEdited ? tr(" · edited") : QString()), m_conflicts);
        item->setData(Qt::UserRole, i);
        item->setForeground(unresolved ? QColor(160, 50, 20) : QColor(25, 120, 65));
        if (current == i) m_conflicts->setCurrentItem(item);
    }
    const bool open = m_session->state() == CompareSession::State::Open;
    const bool selected = open && current >= 0 && current < blocks.size();
    const bool difference = selected && blocks[current].kind != Merge::Kind::Unchanged
        && !ranges[current].manualGroup;
    const bool conflict = selected && blocks[current].kind == Merge::Kind::Conflict;
    m_acceptLeft->setEnabled(difference); m_acceptRight->setEnabled(difference);
    m_acceptBase->setEnabled(difference && m_session->hasBase());
    m_leftRight->setEnabled(conflict && difference); m_rightLeft->setEnabled(conflict && difference);
    const QString choiceHint = selected && ranges[current].manualGroup
        ? tr("Manual editing joined several blocks. Undo that edit or review and mark resolved.") : QString();
    for (auto *choice : {m_acceptLeft, m_acceptRight, m_acceptBase, m_leftRight, m_rightLeft})
        choice->setToolTip(choiceHint);
    m_resolve->setEnabled(conflict && ranges[current].resolution == Merge::Resolution::Unresolved);
    m_reopen->setEnabled(conflict && ranges[current].resolution != Merge::Resolution::Unresolved);
    m_undo->setEnabled(m_session->canUndo()); m_redo->setEnabled(m_session->canRedo());
    m_save->setEnabled(m_session->canSave());
    m_output->setReadOnly(!open);
    highlight(false);
    m_refreshing = false;
}
void TextMergeView::highlight(bool navigate)
{
    if (!m_session) return;
    QList<QTextEdit::ExtraSelection> left, base, right, output;
    const auto &blocks = m_session->mergeResult().blocks;
    const auto &ranges = m_session->outputRanges();
    const int current = m_session->currentBlock();
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].kind == Merge::Kind::Unchanged && i != current) continue;
        const bool conflict = blocks[i].kind == Merge::Kind::Conflict;
        QColor color = conflict && ranges[i].resolution == Merge::Resolution::Unresolved
            ? QColor(255, 216, 206) : ranges[i].manuallyEdited ? QColor(237, 221, 249) : QColor(220, 241, 222);
        if (i == current) color = conflict && ranges[i].resolution == Merge::Resolution::Unresolved
            ? QColor(255, 182, 159) : QColor(185, 221, 251);
        const auto &block = blocks[i];
        auto source = [&](QPlainTextEdit *editor, int start, int count, QList<QTextEdit::ExtraSelection> &list) {
            const int position = linePosition(editor, start);
            list.append(selection(editor, position, linePosition(editor, start + count) - position, color));
        };
        source(m_left, block.leftStart, block.leftCount, left);
        source(m_base, block.baseStart, block.baseCount, base);
        source(m_right, block.rightStart, block.rightCount, right);
        output.append(selection(m_output, ranges[i].start, ranges[i].length, color));
    }
    m_left->setExtraSelections(left); m_base->setExtraSelections(base);
    m_right->setExtraSelections(right); m_output->setExtraSelections(output);
    if (!navigate || current < 0 || current >= blocks.size()) return;
    const bool previous = m_refreshing; m_refreshing = true;
    auto locate = [](QPlainTextEdit *edit, int position) {
        const QSignalBlocker blocker(edit);
        auto cursor = edit->textCursor();
        cursor.setPosition(qBound(0, position, edit->document()->characterCount() - 1));
        edit->setTextCursor(cursor); edit->centerCursor();
    };
    locate(m_left, linePosition(m_left, blocks[current].leftStart));
    locate(m_base, linePosition(m_base, blocks[current].baseStart));
    locate(m_right, linePosition(m_right, blocks[current].rightStart));
    locate(m_output, ranges[current].start);
    m_refreshing = previous;
}
void TextMergeView::showError(const QString &error) { QMessageBox::warning(this, tr("Merge output"), error); }
void TextMergeView::saveOutput(bool choosePath)
{
    if (!m_session) return;
    if (m_session->unresolvedCount()) {
        showError(tr("There are %1 unresolved conflicts. Review each conflict and choose a version or mark your edited result resolved before saving.").arg(m_session->unresolvedCount()));
        return;
    }
    QString error;
    if (choosePath || m_session->outputPath().isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save merged output"), m_session->outputPath());
        if (path.isEmpty()) return;
        if (!m_session->setOutputPath(path, true, &error)) { showError(error); return; }
    }
    if (!m_session->save(&error)) showError(error);
    refresh();
}
}
