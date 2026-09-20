#include "textcomparesession.h"
#include "textcompareview.h"

#include <QFileInfo>
#include <QSignalBlocker>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

TextCompareSession::TextCompareSession(QObject *parent)
    : TextCompareSession({}, {}, parent) {}

TextCompareSession::TextCompareSession(const QString &left, const QString &right, QObject *parent)
    : CompareSession(QStringLiteral("text"), parent), m_leftPath(left), m_rightPath(right)
{
    updateTitle();
    connect(sessionSettings(), &SessionSettings::changed, this, [this](const QString &) {
        auto *settings = sessionSettings();
        m_options.ignoreCase = settings->value(QStringLiteral("text.ignoreCase"), false).toBool();
        m_options.ignoreEol = settings->value(QStringLiteral("text.ignoreEol"), true).toBool();
        m_options.ignoreFinalNewline = settings->value(QStringLiteral("text.ignoreFinalNewline"), false).toBool();
        m_options.whitespace = static_cast<Text::Whitespace>(qBound(0,
            settings->value(QStringLiteral("text.whitespace"), 0).toInt(), 2));
        if (state() == State::Open) recompute();
    });
}

bool TextCompareSession::setPaths(const QString &left, const QString &right, QString *error)
{
    if (state() == State::Closed) return reject(error, tr("This session has been closed."));
    if (isDirty()) return reject(error, tr("Save or discard your changes before choosing other files."));
    if (state() == State::Open) return loadPair(left, right, error);
    m_leftPath = left;
    m_rightPath = right;
    updateTitle();
    emit pathsChanged();
    if (error) error->clear();
    return true;
}

bool TextCompareSession::loadPair(const QString &left, const QString &right, QString *error)
{
    Text::Document nextLeft, nextRight;
    QString reason;
    if (!Text::Document::load(left, &nextLeft, &reason, m_leftCodec))
        return reject(error, tr("Left file: %1").arg(reason));
    if (!Text::Document::load(right, &nextRight, &reason, m_rightCodec))
        return reject(error, tr("Right file: %1").arg(reason));
    m_left = nextLeft;
    m_right = nextRight;
    m_undo.clear();
    m_redo.clear();
    m_leftPath = m_left.path();
    m_rightPath = m_right.path();
    setDirty(false);
    updateTitle();
    emit pathsChanged();
    recompute();
    if (error) error->clear();
    return true;
}

QWidget *TextCompareSession::createView(QWidget *parent) { return new TextCompareView(this, parent); }
bool TextCompareSession::doOpen(QString *error)
{
    if (!m_leftReadOnly || state() != State::Open)
        m_leftCodec = sessionSettings()->value(QStringLiteral("text.leftEncoding")).toByteArray();
    if (!m_rightReadOnly || state() != State::Open)
        m_rightCodec = sessionSettings()->value(QStringLiteral("text.rightEncoding")).toByteArray();
    return loadPair(m_leftPath, m_rightPath, error);
}
bool TextCompareSession::doReload(QString *error) { return doOpen(error); }

void TextCompareSession::updateTitle()
{
    const QString left = m_leftPath.isEmpty() ? tr("Empty") : QFileInfo(m_leftPath).fileName();
    const QString right = m_rightPath.isEmpty() ? tr("Empty") : QFileInfo(m_rightPath).fileName();
    setTitle(tr("%1 ↔ %2").arg(left, right));
}

void TextCompareSession::setComparisonOptions(const Text::CompareOptions &options)
{
    // The settings signal is the one route for both UI and restored definitions.
    auto *settings = sessionSettings();
    const QSignalBlocker blocker(settings);
    settings->setValue(QStringLiteral("text.ignoreCase"), options.ignoreCase);
    settings->setValue(QStringLiteral("text.ignoreEol"), options.ignoreEol);
    settings->setValue(QStringLiteral("text.ignoreFinalNewline"), options.ignoreFinalNewline);
    settings->setValue(QStringLiteral("text.whitespace"), static_cast<int>(options.whitespace));
    m_options = options;
    recompute();
}

void TextCompareSession::recompute()
{
    m_result = Text::compare(m_left.lines(), m_right.lines(), m_options);
    m_currentDifference = m_result.differences.isEmpty() ? -1
        : qBound(0, m_currentDifference, m_result.differences.size() - 1);
    setDirty(m_left.isModified() || m_right.isModified());
    emit comparisonChanged();
    updateStatus();
}

void TextCompareSession::updateStatus()
{
    QString status = tr("%1 difference block(s), %2 ignored • Left: %3, %4 • Right: %5, %6")
        .arg(m_result.differences.size()).arg(m_result.ignoredBlocks)
        .arg(QString::fromLatin1(m_left.codecName()), m_left.eolDescription(),
             QString::fromLatin1(m_right.codecName()), m_right.eolDescription());
    if (m_options.ignoreEol) status += tr(" • Line endings ignored");
    if (m_options.ignoreFinalNewline) status += tr(" • Final newline ignored");
    if (m_left.hasBom() != m_right.hasBom()) status += tr(" • BOM differs (metadata only)");
    if (m_left.codecName() != m_right.codecName()) status += tr(" • Encoding differs (metadata only)");
    if (m_result.alignmentLimited) status += tr(" • Alignment work limit reached; unmatched range shown as replacement");
    if (!m_left.warning().isEmpty()) status += tr(" • Left: %1").arg(m_left.warning());
    if (!m_right.warning().isEmpty()) status += tr(" • Right: %1").arg(m_right.warning());
    if (m_leftReadOnly) status += tr(" • Left read-only");
    if (m_rightReadOnly) status += tr(" • Right read-only");
    setStatusText(status);
}

bool TextCompareSession::setText(bool left, const QString &text, QString *error)
{
    if (state() != State::Open) return reject(error, tr("Open the session before editing."));
    if (isSideReadOnly(left)) return reject(error, tr("This side is read-only."));
    const auto before = bufferState();
    if (!(left ? m_left : m_right).setNormalizedText(text, error)) return false;
    recordChange(before);
    recompute();
    return true;
}

bool TextCompareSession::saveSide(bool left, QString *error)
{
    if (state() != State::Open) return reject(error, tr("This session is not open."));
    if (isSideReadOnly(left)) return reject(error, tr("This side is read-only and cannot be saved."));
    Text::Document &document = left ? m_left : m_right;
    if (!document.isModified()) { if (error) error->clear(); return true; }
    if (!document.save(error)) return false;
    recompute();
    return true;
}

bool TextCompareSession::saveSideAs(bool left, const QString &path, bool overwrite, QString *error)
{
    if (state() != State::Open) return reject(error, tr("This session is not open."));
    if (isSideReadOnly(left)) return reject(error, tr("This side is read-only and cannot be saved."));
    Text::Document &document = left ? m_left : m_right;
    const Text::Document &other = left ? m_right : m_left;
    if (other.refersToPath(path))
        return reject(error, tr("Save As cannot overwrite the other side of this comparison. Copy the difference into that side and save it instead."));
    if (!document.saveAs(path, overwrite, error)) return false;
    if (left) m_leftPath = document.path(); else m_rightPath = document.path();
    updateTitle();
    emit pathsChanged();
    recompute();
    return true;
}

bool TextCompareSession::doSave(QString *error)
{
    if ((m_leftReadOnly && m_left.isModified()) || (m_rightReadOnly && m_right.isModified()))
        return reject(error, tr("A modified side is read-only and cannot be saved."));
    // Preflight every changed side before the first write, so a known conflict
    // cannot produce a partially saved comparison.
    if (m_left.isModified() && !m_left.checkUnchangedOnDisk(error)) return false;
    if (m_right.isModified() && !m_right.checkUnchangedOnDisk(error)) return false;
    if (m_left.isModified() && m_right.isModified()
        && (m_left.refersToPath(m_right.path()) || m_right.refersToPath(m_left.path())))
        return reject(error, tr("Both sides refer to the same file. Save one side to a different file first."));
    QString conversion;
    if (m_left.isModified()) { m_left.bytes(&conversion); if (!conversion.isEmpty()) return reject(error, conversion); }
    if (m_right.isModified()) { m_right.bytes(&conversion); if (!conversion.isEmpty()) return reject(error, conversion); }
    const bool savedLeft = m_left.isModified();
    if (savedLeft && !saveSide(true, error)) return false;
    if (m_right.isModified() && !saveSide(false, error)) {
        if (error && savedLeft) *error = tr("The left side was saved, but the right side was not: %1").arg(*error);
        return false;
    }
    return true;
}

bool TextCompareSession::setEncoding(bool left, const QByteArray &codec, QString *error)
{
    if (state() != State::Open) return reject(error, tr("Open the session before changing its encoding."));
    if (isSideReadOnly(left)) return reject(error, tr("This side is read-only; its encoding cannot be changed."));
    if (isDirty()) return reject(error, tr("Save changes before decoding the files again."));
    const QByteArray previous = left ? m_leftCodec : m_rightCodec;
    if (left) m_leftCodec = codec; else m_rightCodec = codec;
    if (!loadPair(m_leftPath, m_rightPath, error)) {
        if (left) m_leftCodec = previous; else m_rightCodec = previous;
        return false;
    }
    sessionSettings()->setValue(left ? QStringLiteral("text.leftEncoding") : QStringLiteral("text.rightEncoding"), codec);
    return true;
}

void TextCompareSession::setLineEnding(bool left, Text::Eol eol)
{
    if (state() != State::Open) return;
    if (isSideReadOnly(left)) {
        setStatusText(tr("This side is read-only; its line endings cannot be changed."));
        return;
    }
    const auto before = bufferState();
    (left ? m_left : m_right).setEol(eol);
    recordChange(before);
    recompute();
}

bool TextCompareSession::copyDifference(bool leftToRight, QString *error)
{
    if (state() != State::Open || m_currentDifference < 0)
        return reject(error, tr("Select a difference first."));
    if (isSideReadOnly(!leftToRight))
        return reject(error, tr("The destination side is read-only."));
    const Text::Block block = m_result.blocks[m_result.differences[m_currentDifference]];
    const Text::Document &source = leftToRight ? m_left : m_right;
    if (!source.canEdit())
        return reject(error, tr("The source cannot be copied safely: %1").arg(source.warning()));
    Text::Document &destination = leftToRight ? m_right : m_left;
    const auto before = bufferState();
    const auto lines = source.lines().mid(leftToRight ? block.leftStart : block.rightStart,
                                         leftToRight ? block.leftCount : block.rightCount);
    if (!destination.replaceLines(leftToRight ? block.rightStart : block.leftStart,
                                  leftToRight ? block.rightCount : block.leftCount, lines, error)) return false;
    recordChange(before);
    recompute();
    if (m_currentDifference >= 0) emit currentDifferenceChanged(m_currentDifference);
    return true;
}

void TextCompareSession::selectDifference(int index)
{
    if (index < 0 || index >= m_result.differences.size()) {
        setStatusText(m_result.differences.isEmpty() ? tr("No differences under the current comparison rules.")
            : tr("Reached the %1 difference.").arg(index < 0 ? tr("first") : tr("last")));
        return;
    }
    m_currentDifference = index;
    updateStatus();
    emit currentDifferenceChanged(index);
}
void TextCompareSession::previousDifference() { selectDifference(m_currentDifference - 1); }
void TextCompareSession::nextDifference() { selectDifference(m_currentDifference + 1); }
void TextCompareSession::firstDifference() { selectDifference(0); }
void TextCompareSession::lastDifference() { selectDifference(m_result.differences.size() - 1); }

void TextCompareSession::appendHistory(QVector<BufferState> &history, const BufferState &buffers)
{
    history.append(buffers);
    // Keep at least the latest operation undoable; cap older snapshots at 32 MiB.
    qint64 cost = 0;
    int keepFrom = history.size() - 1;
    for (int i = history.size() - 1; i >= 0 && history.size() - i <= 50; --i) {
        qint64 entryCost = 0;
        for (const auto &line : history[i].left) entryCost += sizeof(Text::Line) + qint64(line.text.size()) * 2;
        for (const auto &line : history[i].right) entryCost += sizeof(Text::Line) + qint64(line.text.size()) * 2;
        if (i < history.size() - 1 && cost + entryCost > 32 * 1024 * 1024) break;
        cost += entryCost;
        keepFrom = i;
    }
    if (keepFrom > 0) history.remove(0, keepFrom);
}

void TextCompareSession::recordChange(const BufferState &before)
{
    if (before.left == m_left.lines() && before.right == m_right.lines()) return;
    appendHistory(m_undo, before);
    m_redo.clear();
}

void TextCompareSession::restoreBuffers(const BufferState &buffers)
{
    // Only content is restored. Paths, original disk snapshots and encoding stay
    // current so Undo after Save correctly becomes an unsaved content change.
    if (buffers.left != m_left.lines()) m_left.replaceLines(0, m_left.lines().size(), buffers.left);
    if (buffers.right != m_right.lines()) m_right.replaceLines(0, m_right.lines().size(), buffers.right);
    recompute();
}

bool TextCompareSession::canRestoreBuffers(const BufferState &buffers) const
{
    return (!m_leftReadOnly || buffers.left == m_left.lines())
        && (!m_rightReadOnly || buffers.right == m_right.lines());
}

bool TextCompareSession::canUndo() const
{
    return state() == State::Open && !m_undo.isEmpty() && canRestoreBuffers(m_undo.last());
}

bool TextCompareSession::canRedo() const
{
    return state() == State::Open && !m_redo.isEmpty() && canRestoreBuffers(m_redo.last());
}

void TextCompareSession::undo()
{
    if (state() != State::Open || m_undo.isEmpty()) return;
    if (!canUndo()) { setStatusText(tr("Undo would change a read-only side.")); return; }
    appendHistory(m_redo, bufferState());
    restoreBuffers(m_undo.takeLast());
}

void TextCompareSession::redo()
{
    if (state() != State::Open || m_redo.isEmpty()) return;
    if (!canRedo()) { setStatusText(tr("Redo would change a read-only side.")); return; }
    appendHistory(m_undo, bufferState());
    restoreBuffers(m_redo.takeLast());
}

bool TextCompareSession::findText(const QString &text, bool backward, bool caseSensitive)
{
    auto *view = qobject_cast<TextCompareView *>(widget());
    return view && view->findText(text, backward, caseSensitive);
}
bool TextCompareSession::goToLine(int oneBasedLine, bool left)
{
    auto *view = qobject_cast<TextCompareView *>(widget());
    return view && view->goToLine(oneBasedLine, left);
}
void TextCompareSession::findText()
{
    if (auto *view = qobject_cast<TextCompareView *>(widget())) view->promptFind();
}
void TextCompareSession::goToLine()
{
    if (auto *view = qobject_cast<TextCompareView *>(widget())) view->promptGoToLine();
}
void TextCompareSession::editSide(bool left)
{
    if (isSideReadOnly(left)) { setStatusText(tr("This side is read-only.")); return; }
    if (auto *view = qobject_cast<TextCompareView *>(widget())) view->edit(left);
}

void TextCompareSession::setReadOnly(bool left, bool right)
{
    if (m_leftReadOnly == left && m_rightReadOnly == right) return;
    m_leftReadOnly = left;
    m_rightReadOnly = right;
    updateStatus();
    emit readOnlyChanged();
}

bool TextCompareSession::canSaveNow() const
{
    return isDirty() && !(m_leftReadOnly && m_left.isModified())
        && !(m_rightReadOnly && m_right.isModified());
}

void TextCompareSession::setUseLocalShortcuts(bool enabled)
{
    if (m_useLocalShortcuts == enabled) return;
    m_useLocalShortcuts = enabled;
    emit localShortcutsChanged(enabled);
}

}
