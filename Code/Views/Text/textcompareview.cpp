#include "textcompareview.h"
#include "textcomparesession.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>

namespace LqCompare {
namespace {
class Gutter : public QWidget {
public:
    explicit Gutter(TextPane *pane) : QWidget(pane), m_pane(pane) {}
    void paintEvent(QPaintEvent *event) override { m_pane->paintGutter(event); }
private:
    TextPane *m_pane;
};
QColor colorFor(Text::Change change, bool left)
{
    switch (change) {
    case Text::Change::Insert: return left ? QColor(239, 241, 244) : QColor(218, 246, 224);
    case Text::Change::Delete: return left ? QColor(253, 221, 221) : QColor(239, 241, 244);
    case Text::Change::Replace: return QColor(255, 238, 194);
    case Text::Change::Ignored: return QColor(237, 240, 246);
    case Text::Change::Equal: return {};
    }
    return {};
}
QString displayLine(QString text)
{
    // QTextDocument treats these Unicode characters as document separators.
    // Show their presence without introducing fictitious comparison rows.
    text.replace(QChar(0x2028), QChar(0x21b5));
    text.replace(QChar(0x2029), QChar(0x00b6));
    return text;
}
}

TextPane::TextPane(QWidget *parent) : QPlainTextEdit(parent), m_gutter(new Gutter(this))
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutter(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &, int) { m_gutter->update(); });
    updateGutter();
}

void TextPane::setLineNumbers(const QVector<int> &numbers) { m_numbers = numbers; updateGutter(); }
void TextPane::updateGutter()
{
    int largest = 1;
    for (int number : m_numbers) largest = qMax(largest, number + 1);
    m_gutterWidth = 18 + fontMetrics().horizontalAdvance(QString::number(largest));
    setViewportMargins(m_gutterWidth, 0, 0, 0);
    const QRect rect = contentsRect();
    m_gutter->setGeometry(rect.left(), rect.top(), m_gutterWidth, rect.height());
    m_gutter->update();
}
void TextPane::resizeEvent(QResizeEvent *event) { QPlainTextEdit::resizeEvent(event); updateGutter(); }
void TextPane::focusInEvent(QFocusEvent *event) { QPlainTextEdit::focusInEvent(event); emit activated(); }
void TextPane::paintGutter(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), palette().alternateBase());
    painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
    QTextBlock block = firstVisibleBlock();
    while (block.isValid()) {
        const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
        if (geometry.top() > event->rect().bottom()) break;
        if (geometry.bottom() >= event->rect().top()) {
            const int number = block.blockNumber();
            if (number < m_numbers.size() && m_numbers[number] >= 0)
                painter.drawText(0, qRound(geometry.top()), m_gutterWidth - 7, fontMetrics().height(),
                                 Qt::AlignRight, QString::number(m_numbers[number] + 1));
        }
        block = block.next();
    }
}

TextCompareView::TextCompareView(TextCompareSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("textCompareView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    auto *toolbar = new QHBoxLayout;
    const auto command = [this, toolbar](const QString &label, const QString &name, auto callback) {
        auto *button = new QPushButton(label, this);
        button->setObjectName(name);
        toolbar->addWidget(button);
        connect(button, &QPushButton::clicked, this, callback);
        return button;
    };
    command(tr("Compare"), QStringLiteral("compareFiles"), [this] {
        QString error;
        if (!m_session->setPaths(m_paths[0]->text(), m_paths[1]->text(), &error)) showError(error);
    });
    command(tr("First"), QStringLiteral("firstDifference"), [session] { session->firstDifference(); });
    command(tr("Previous"), QStringLiteral("previousDifference"), [session] { session->previousDifference(); });
    command(tr("Next"), QStringLiteral("nextDifference"), [session] { session->nextDifference(); });
    command(tr("Last"), QStringLiteral("lastDifference"), [session] { session->lastDifference(); });
    m_copyToRight = command(tr("Copy →"), QStringLiteral("copyToRight"), [this] {
        QString error; if (!m_session->copyDifference(true, &error)) showError(error);
    });
    m_copyToRight->setToolTip(tr("Copy the selected difference into the right buffer. Save writes it to disk."));
    m_copyToLeft = command(tr("← Copy"), QStringLiteral("copyToLeft"), [this] {
        QString error; if (!m_session->copyDifference(false, &error)) showError(error);
    });
    m_copyToLeft->setToolTip(tr("Copy the selected difference into the left buffer. Save writes it to disk."));
    m_undo = command(tr("Undo"), QStringLiteral("undoText"), [session] { session->undo(); });
    m_redo = command(tr("Redo"), QStringLiteral("redoText"), [session] { session->redo(); });
    command(tr("Find…"), QStringLiteral("findText"), [session] { session->findText(); });
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto *rules = new QHBoxLayout;
    m_ignoreCase = new QCheckBox(tr("Ignore case"), this);
    m_ignoreCase->setObjectName(QStringLiteral("ignoreCase"));
    m_ignoreEol = new QCheckBox(tr("Ignore line endings"), this);
    m_ignoreFinal = new QCheckBox(tr("Ignore final newline"), this);
    m_whitespace = new QComboBox(this);
    m_whitespace->addItems({tr("Exact whitespace"), tr("Ignore whitespace changes"), tr("Ignore all whitespace")});
    rules->addWidget(m_ignoreCase);
    rules->addWidget(m_whitespace);
    rules->addWidget(m_ignoreEol);
    rules->addWidget(m_ignoreFinal);
    rules->addStretch();
    layout->addLayout(rules);
    const auto optionsChanged = [this] {
        Text::CompareOptions options;
        options.ignoreCase = m_ignoreCase->isChecked();
        options.ignoreEol = m_ignoreEol->isChecked();
        options.ignoreFinalNewline = m_ignoreFinal->isChecked();
        options.whitespace = static_cast<Text::Whitespace>(m_whitespace->currentIndex());
        m_session->setComparisonOptions(options);
    };
    connect(m_ignoreCase, &QCheckBox::toggled, this, optionsChanged);
    connect(m_ignoreEol, &QCheckBox::toggled, this, optionsChanged);
    connect(m_ignoreFinal, &QCheckBox::toggled, this, optionsChanged);
    connect(m_whitespace, QOverload<int>::of(&QComboBox::currentIndexChanged), this, optionsChanged);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("textSplitter"));
    splitter->addWidget(makeSide(true));
    splitter->addWidget(makeSide(false));
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setObjectName(QStringLiteral("textComparisonSummary"));
    layout->addWidget(m_summary);

    for (int side = 0; side < 2; ++side) {
        connect(m_panes[side], &TextPane::activated, this, [this, side] { m_activeSide = side; });
        connect(m_panes[side]->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, side](int value) {
            if (m_scrolling) return;
            m_scrolling = true;
            m_panes[1 - side]->verticalScrollBar()->setValue(value);
            m_scrolling = false;
        });
        connect(m_panes[side], &QPlainTextEdit::cursorPositionChanged, this, [this, side] {
            const int row = m_panes[side]->textCursor().blockNumber();
            const auto &result = m_session->comparison();
            if (row >= result.rows.size()) return;
            const int difference = result.differences.indexOf(result.rows[row].block);
            if (difference >= 0 && difference != m_session->currentDifference()) {
                m_selectingText = true;
                m_session->selectDifference(difference);
                m_selectingText = false;
            }
        });
    }
    connect(session, &TextCompareSession::comparisonChanged, this, &TextCompareView::refresh);
    connect(session, &TextCompareSession::readOnlyChanged, this, &TextCompareView::refreshActions);
    connect(session, &TextCompareSession::localShortcutsChanged, this, &TextCompareView::setUseLocalShortcuts);
    connect(session, &TextCompareSession::currentDifferenceChanged, this, &TextCompareView::locate);
    connect(session, &TextCompareSession::pathsChanged, this, [this] {
        m_paths[0]->setText(m_session->leftPath());
        m_paths[1]->setText(m_session->rightPath());
    });
    connect(session, &CompareSession::statusTextChanged, m_summary, &QLabel::setText);
    auto *next = new QShortcut(QKeySequence(Qt::Key_F8), this);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(next, &QShortcut::activated, session, &TextCompareSession::nextDifference);
    auto *previous = new QShortcut(QKeySequence(Qt::Key_F7), this);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QShortcut::activated, session, &TextCompareSession::previousDifference);
    auto *find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, &TextCompareView::promptFind);
    auto *undo = new QShortcut(QKeySequence::Undo, this);
    undo->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undo, &QShortcut::activated, session, &TextCompareSession::undo);
    auto *redo = new QShortcut(QKeySequence::Redo, this);
    redo->setContext(Qt::WidgetWithChildrenShortcut);
    connect(redo, &QShortcut::activated, session, &TextCompareSession::redo);
    m_shortcuts = {next, previous, find, undo, redo};
    setUseLocalShortcuts(session->usesLocalShortcuts());
    refresh();
}

QWidget *TextCompareView::makeSide(bool left)
{
    const int side = left ? 0 : 1;
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(new QLabel(left ? tr("Left") : tr("Right"), widget));
    m_paths[side] = new QLineEdit(widget);
    m_paths[side]->setObjectName(left ? QStringLiteral("leftPath") : QStringLiteral("rightPath"));
    m_paths[side]->setPlaceholderText(tr("Empty side, or enter a file path"));
    pathRow->addWidget(m_paths[side], 1);
    auto *browse = new QPushButton(tr("Browse…"), widget);
    connect(browse, &QPushButton::clicked, this, [this, side] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose text file"), m_paths[side]->text());
        if (!path.isEmpty()) m_paths[side]->setText(path);
    });
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);
    auto *actions = new QHBoxLayout;
    m_edit[side] = new QPushButton(tr("Edit…"), widget);
    m_edit[side]->setObjectName(left ? QStringLiteral("editLeft") : QStringLiteral("editRight"));
    m_edit[side]->setToolTip(tr("Edit original text, without comparison padding. Apply changes to the buffer, then save."));
    connect(m_edit[side], &QPushButton::clicked, this, [this, left] { edit(left); });
    actions->addWidget(m_edit[side]);
    m_save[side] = new QPushButton(tr("Save"), widget);
    m_save[side]->setObjectName(left ? QStringLiteral("saveLeft") : QStringLiteral("saveRight"));
    connect(m_save[side], &QPushButton::clicked, this, [this, left] { save(left, false); });
    actions->addWidget(m_save[side]);
    auto *saveAs = new QPushButton(tr("Save As…"), widget);
    m_saveAs[side] = saveAs;
    saveAs->setObjectName(left ? QStringLiteral("saveAsLeft") : QStringLiteral("saveAsRight"));
    connect(saveAs, &QPushButton::clicked, this, [this, left] { save(left, true); });
    actions->addWidget(saveAs);
    auto *encoding = new QComboBox(widget);
    m_encodings[side] = encoding;
    encoding->setObjectName(left ? QStringLiteral("encodingLeft") : QStringLiteral("encodingRight"));
    encoding->addItems({tr("Decode as…"), QStringLiteral("UTF-8"), QStringLiteral("UTF-16LE"),
                       QStringLiteral("UTF-16BE"), QStringLiteral("UTF-32LE"), QStringLiteral("UTF-32BE"),
                       QStringLiteral("GB18030"), QStringLiteral("GBK"),
                       QStringLiteral("Big5"), QStringLiteral("ISO-8859-1")});
    connect(encoding, QOverload<int>::of(&QComboBox::activated), this, [this, left, encoding](int index) {
        if (!index) return;
        QString error;
        if (!m_session->setEncoding(left, encoding->itemText(index).toLatin1(), &error)) showError(error);
        encoding->setCurrentIndex(0);
    });
    actions->addWidget(encoding);
    auto *ending = new QComboBox(widget);
    m_endings[side] = ending;
    ending->setObjectName(left ? QStringLiteral("lineEndingLeft") : QStringLiteral("lineEndingRight"));
    ending->addItems({tr("Line endings…"), QStringLiteral("LF"), QStringLiteral("CRLF"), QStringLiteral("CR")});
    connect(ending, QOverload<int>::of(&QComboBox::activated), this, [this, left, ending](int index) {
        if (index) m_session->setLineEnding(left, static_cast<Text::Eol>(index));
        ending->setCurrentIndex(0);
    });
    actions->addWidget(ending);
    actions->addStretch();
    layout->addLayout(actions);
    m_panes[side] = new TextPane(widget);
    m_panes[side]->setObjectName(left ? QStringLiteral("leftTextPane") : QStringLiteral("rightTextPane"));
    layout->addWidget(m_panes[side], 1);
    m_metadata[side] = new QLabel(widget);
    m_metadata[side]->setWordWrap(true);
    layout->addWidget(m_metadata[side]);
    return widget;
}

void TextCompareView::refresh()
{
    const QSignalBlocker b1(m_ignoreCase), b2(m_ignoreEol), b3(m_ignoreFinal), b4(m_whitespace);
    const auto options = m_session->comparisonOptions();
    m_ignoreCase->setChecked(options.ignoreCase);
    m_ignoreEol->setChecked(options.ignoreEol);
    m_ignoreFinal->setChecked(options.ignoreFinalNewline);
    m_whitespace->setCurrentIndex(static_cast<int>(options.whitespace));
    m_paths[0]->setText(m_session->leftPath());
    m_paths[1]->setText(m_session->rightPath());
    for (int side = 0; side < 2; ++side) {
        const auto &document = side == 0 ? m_session->leftDocument() : m_session->rightDocument();
        QStringList content;
        QVector<int> numbers;
        for (const auto &row : m_session->comparison().rows) {
            const int line = side == 0 ? row.leftLine : row.rightLine;
            numbers.append(line);
            content.append(line >= 0 ? displayLine(document.lines()[line].text) : QString());
        }
        const int position = m_panes[side]->verticalScrollBar()->value();
        const int horizontal = m_panes[side]->horizontalScrollBar()->value();
        const QSignalBlocker paneBlocker(m_panes[side]);
        m_panes[side]->setPlainText(content.join(QLatin1Char('\n')));
        m_panes[side]->setLineNumbers(numbers);
        m_panes[side]->verticalScrollBar()->setValue(position);
        m_panes[side]->horizontalScrollBar()->setValue(horizontal);
        QString metadata = tr("%1 lines • %2%3 • %4 • %5")
            .arg(document.lines().size()).arg(QString::fromLatin1(document.codecName()))
            .arg(document.hasBom() ? tr(" BOM") : QString())
            .arg(document.eolDescription())
            .arg(document.lines().isEmpty() || document.lines().last().eol == Text::Eol::None
                 ? tr("No final newline") : tr("Final newline"));
        if (document.isModified()) metadata += tr(" • Unsaved");
        if (!document.warning().isEmpty()) metadata += tr(" • %1").arg(document.warning());
        m_metadata[side]->setText(metadata);
    }
    highlight();
    refreshActions();
    m_summary->setText(m_session->statusText());
}

void TextCompareView::refreshActions()
{
    for (int side = 0; side < 2; ++side) {
        const bool writable = !m_session->isSideReadOnly(side == 0);
        const auto &document = side == 0 ? m_session->leftDocument() : m_session->rightDocument();
        m_edit[side]->setEnabled(writable && document.canEdit());
        m_save[side]->setEnabled(writable && document.isModified());
        m_saveAs[side]->setEnabled(writable && document.canEdit());
        m_encodings[side]->setEnabled(writable);
        m_endings[side]->setEnabled(writable && document.canEdit());
    }
    const bool hasDifference = m_session->currentDifference() >= 0;
    m_copyToLeft->setEnabled(hasDifference && !m_session->isSideReadOnly(true)
                            && m_session->leftDocument().canEdit() && m_session->rightDocument().canEdit());
    m_copyToRight->setEnabled(hasDifference && !m_session->isSideReadOnly(false)
                             && m_session->leftDocument().canEdit() && m_session->rightDocument().canEdit());
    m_undo->setEnabled(m_session->canUndo());
    m_redo->setEnabled(m_session->canRedo());
}

void TextCompareView::setUseLocalShortcuts(bool enabled)
{
    for (QShortcut *shortcut : m_shortcuts) shortcut->setEnabled(enabled);
}

void TextCompareView::highlight()
{
    const auto &result = m_session->comparison();
    const int current = m_session->currentDifference() >= 0
        ? result.differences[m_session->currentDifference()] : -1;
    for (int side = 0; side < 2; ++side) {
        QList<QTextEdit::ExtraSelection> selections;
        for (int row = 0; row < result.rows.size(); ++row) {
            const auto &entry = result.rows[row];
            if (entry.change == Text::Change::Equal) continue;
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(m_panes[side]->document()->findBlockByNumber(row));
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            QColor color = colorFor(entry.change, side == 0);
            if (entry.block == current) color = color.darker(106);
            selection.format.setBackground(color);
            selection.format.setForeground(QColor(28, 34, 43));
            selections.append(selection);
        }
        m_panes[side]->setExtraSelections(selections);
    }
}

void TextCompareView::locate(int difference)
{
    if (m_selectingText) { highlight(); return; }
    const auto &result = m_session->comparison();
    if (difference < 0 || difference >= result.differences.size()) return;
    const int row = result.blocks[result.differences[difference]].firstRow;
    for (TextPane *pane : m_panes) {
        const QSignalBlocker blocker(pane);
        pane->setTextCursor(QTextCursor(pane->document()->findBlockByNumber(row)));
        pane->centerCursor();
    }
    highlight();
}

void TextCompareView::showError(const QString &message)
{
    QMessageBox::warning(this, tr("Text comparison"), message);
}

void TextCompareView::edit(bool left)
{
    if (m_session->isSideReadOnly(left)) {
        m_session->setStatusText(tr("This side is read-only."));
        return;
    }
    const auto &document = left ? m_session->leftDocument() : m_session->rightDocument();
    if (!document.canEdit()) { showError(document.warning()); return; }
    QDialog dialog(this);
    dialog.setWindowTitle(left ? tr("Edit left file") : tr("Edit right file"));
    dialog.resize(900, 640);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Apply changes to the comparison buffer. Use Save to write the file."), &dialog));
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setObjectName(QStringLiteral("textBufferEditor"));
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setPlainText(document.normalizedText());
    layout->addWidget(editor, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !editor->document()->isModified()) return;
    QString text = editor->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    QString error;
    if (!m_session->setText(left, text, &error)) showError(error);
}

void TextCompareView::save(bool left, bool saveAs)
{
    if (m_session->isSideReadOnly(left)) {
        m_session->setStatusText(tr("This side is read-only and cannot be saved."));
        return;
    }
    const auto &document = left ? m_session->leftDocument() : m_session->rightDocument();
    QString error;
    if (saveAs || document.path().isEmpty()) {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save text file"), document.path());
        if (path.isEmpty()) return;
        // QFileDialog's default overwrite confirmation supplied this decision.
        if (!m_session->saveSideAs(left, path, true, &error)) showError(error);
    } else if (!m_session->saveSide(left, &error)) showError(error);
}

bool TextCompareView::findText(const QString &text, bool backward, bool caseSensitive)
{
    if (text.isEmpty()) return false;
    m_searchText = text;
    TextPane *pane = m_panes[m_activeSide];
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    const QTextCursor previous = pane->textCursor();
    if (pane->find(text, flags)) { pane->setFocus(); return true; }
    QTextCursor cursor(pane->document());
    cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    pane->setTextCursor(cursor);
    if (pane->find(text, flags)) {
        pane->setFocus();
        m_session->setStatusText(tr("Search wrapped to the %1 of the %2 file.")
            .arg(backward ? tr("end") : tr("start"), m_activeSide == 0 ? tr("left") : tr("right")));
        return true;
    }
    pane->setTextCursor(previous);
    m_session->setStatusText(tr("Text not found in the %1 file: %2")
        .arg(m_activeSide == 0 ? tr("left") : tr("right"), text));
    return false;
}

bool TextCompareView::goToLine(int oneBasedLine, bool left)
{
    const int side = left ? 0 : 1;
    const auto &rows = m_session->comparison().rows;
    for (int i = 0; i < rows.size(); ++i) {
        if ((left ? rows[i].leftLine : rows[i].rightLine) != oneBasedLine - 1 || oneBasedLine < 1) continue;
        m_panes[side]->setTextCursor(QTextCursor(m_panes[side]->document()->findBlockByNumber(i)));
        m_panes[side]->centerCursor();
        m_panes[side]->setFocus();
        m_session->setStatusText(tr("%1 file, line %2").arg(left ? tr("Left") : tr("Right")).arg(oneBasedLine));
        return true;
    }
    m_session->setStatusText(tr("Line %1 is outside this file.").arg(oneBasedLine));
    return false;
}

void TextCompareView::promptFind()
{
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("Find text"),
        m_activeSide == 0 ? tr("Find in left file:") : tr("Find in right file:"),
        QLineEdit::Normal, m_searchText, &accepted);
    if (accepted) findText(text);
}

void TextCompareView::promptGoToLine()
{
    const int count = (m_activeSide == 0 ? m_session->leftDocument() : m_session->rightDocument()).lines().size();
    if (!count) { m_session->setStatusText(tr("This file is empty.")); return; }
    bool accepted = false;
    const int line = QInputDialog::getInt(this, tr("Go to line"),
        m_activeSide == 0 ? tr("Left line number:") : tr("Right line number:"), 1, 1, count, 1, &accepted);
    if (accepted) goToLine(line, m_activeSide == 0);
}

}
