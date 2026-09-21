#include "textmergesession.h"
#include "textmergeview.h"
#include "textdiff.h"

#include <QFileInfo>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &reason)
{
    if (error) *error = reason;
    return false;
}
QString actualText(const QVector<Text::Line> &lines)
{
    QString text;
    for (const auto &line : lines) text += line.text + Text::eolText(line.eol);
    return text;
}
QString normalizedText(const QVector<Text::Line> &lines)
{
    QString text;
    for (const auto &line : lines) {
        text += line.text;
        if (line.eol != Text::Eol::None) text += QLatin1Char('\n');
    }
    return text;
}
int rawOffset(const QVector<Text::Line> &lines, int normalized)
{
    int raw = 0;
    for (const auto &line : lines) {
        const int size = line.text.size() + (line.eol == Text::Eol::None ? 0 : 1);
        if (normalized < size) return raw + normalized;
        normalized -= size;
        raw += line.text.size() + Text::eolText(line.eol).size();
    }
    return raw;
}
}

TextMergeSession::TextMergeSession(QObject *parent) : TextMergeSession({}, {}, {}, {}, parent) {}
TextMergeSession::TextMergeSession(const QString &base, const QString &left,
                                 const QString &right, const QString &output, QObject *parent)
    : CompareSession(QStringLiteral("text-merge"), parent), m_basePath(base), m_leftPath(left),
      m_rightPath(right), m_initialOutputPath(output)
{
    setTitle(tr("Text merge"));
}

QWidget *TextMergeSession::createView(QWidget *parent) { return new TextMergeView(this, parent); }

void TextMergeSession::setUseLocalShortcuts(bool enabled)
{
    if (m_useLocalShortcuts == enabled) return;
    m_useLocalShortcuts = enabled;
    emit localShortcutsChanged(enabled);
}

bool TextMergeSession::doOpen(QString *error)
{
    if (m_leftPath.isEmpty() || m_rightPath.isEmpty())
        return reject(error, tr("Choose both left and right source files before opening a merge."));
    Text::Document base, left, right;
    QString reason;
    auto load = [&](const QString &path, Text::Document *document, const char *role) {
        const QByteArray codec = sessionSettings()->value(QStringLiteral("merge.%1Encoding")
            .arg(QString::fromLatin1(role))).toByteArray();
        if (!Text::Document::load(path, document, &reason, codec))
            return reject(error, tr("%1 source: %2").arg(QString::fromLatin1(role), reason));
        if (!document->canEdit())
            return reject(error, tr("%1 source cannot be merged: %2 Use the correct encoding or a hexadecimal comparison.")
                .arg(QString::fromLatin1(role), document->warning()));
        return true;
    };
    if (!load(m_basePath, &base, "base") || !load(m_leftPath, &left, "left")
        || !load(m_rightPath, &right, "right")) return false;
    Merge::OutputFile file;
    if (!file.setPath(m_file.path().isEmpty() ? m_initialOutputPath : m_file.path(),
                      {m_basePath, m_leftPath, m_rightPath}, error)) return false;
    reportProgress(0, 2, tr("Comparing both sources with the base"));
    Merge::Result result = hasBase() ? Merge::merge(base.lines(), left.lines(), right.lines())
        : Merge::mergeWithoutBase(left.lines(), right.lines());
    if (result.blocks.isEmpty()) result.blocks.append(Merge::Block{});
    Snapshot snapshot;
    snapshot.output = hasBase() ? base : left;
    if (!snapshot.output.replaceLines(0, snapshot.output.lines().size(), result.outputLines(), error)) return false;
    int start = 0;
    int selected = -1;
    for (int i = 0; i < result.blocks.size(); ++i) {
        const auto &block = result.blocks[i];
        MergeOutputRange range;
        range.start = start;
        range.length = normalizedText(block.output).size();
        range.resolution = block.kind == Merge::Kind::Conflict ? Merge::Resolution::Unresolved
            : block.kind == Merge::Kind::RightOnly ? Merge::Resolution::Right
            : block.kind == Merge::Kind::Unchanged ? Merge::Resolution::Base : Merge::Resolution::Left;
        snapshot.ranges.append(range);
        start += range.length;
        if (selected < 0 && block.kind == Merge::Kind::Conflict) selected = i;
    }
    m_base = base; m_left = left; m_right = right;
    m_file = file; m_result = result; m_snapshot = snapshot;
    m_undo.clear(); m_redo.clear(); m_currentBlock = selected;
    updateStatus();
    reportProgress(2, 2, tr("Merge ready"));
    emit outputPathChanged();
    emit mergeChanged();
    emit currentBlockChanged(m_currentBlock);
    if (error) error->clear();
    return true;
}
bool TextMergeSession::doReload(QString *error) { return doOpen(error); }
int TextMergeSession::unresolvedCount() const
{
    int count = 0;
    for (int i = 0; i < m_result.blocks.size(); ++i)
        if (m_result.blocks[i].kind == Merge::Kind::Conflict
            && m_snapshot.ranges[i].resolution == Merge::Resolution::Unresolved) ++count;
    return count;
}
bool TextMergeSession::canSaveNow() const { return !m_file.hasSaved() || isDirty(); }
bool TextMergeSession::doSave(QString *error)
{
    if (unresolvedCount())
        return reject(error, tr("There are %1 unresolved conflicts. Resolve or explicitly mark every conflict resolved before saving; neither candidate will be discarded silently.").arg(unresolvedCount()));
    QString reason;
    const QByteArray bytes = m_snapshot.output.bytes(&reason);
    if (!reason.isEmpty()) return reject(error, reason);
    if (!m_file.save(bytes, error)) return false;
    setStatusText(tr("Merged output saved to %1. All conflicts are resolved.").arg(m_file.path()));
    return true;
}

bool TextMergeSession::setOutputPath(const QString &path, bool overwrite, QString *error)
{
    if (state() != State::Open) return reject(error, tr("Open the merge before choosing an output file."));
    if (path.isEmpty()) return reject(error, tr("Choose an output file."));
    // Selecting the current path must not acknowledge an unnoticed external edit.
    if (QFileInfo(path).absoluteFilePath() == m_file.path()) return m_file.checkUnchanged(error);
    if (QFileInfo::exists(path) && !overwrite)
        return reject(error, tr("The output already exists. Confirm overwrite or choose a new file."));
    if (!m_file.setPath(path, {m_basePath, m_leftPath, m_rightPath}, error)) return false;
    setDirty(true);
    updateStatus();
    emit outputPathChanged();
    return true;
}

void TextMergeSession::pushUndo()
{
    m_undo.append(m_snapshot);
    // Bound memory while retaining a useful editing history. Qt containers share data.
    if (m_undo.size() > 200) m_undo.removeFirst();
    m_redo.clear();
}
void TextMergeSession::changed()
{
    setDirty(true);
    updateStatus();
    emit mergeChanged();
}
int TextMergeSession::blockAt(int position) const
{
    if (m_currentBlock >= 0) {
        const auto &range = m_snapshot.ranges[m_currentBlock];
        if (range.length == 0 && range.start == position) return m_currentBlock;
    }
    for (int i = 0; i < m_snapshot.ranges.size(); ++i) {
        const auto &range = m_snapshot.ranges[i];
        if (position >= range.start && position < range.start + range.length) return i;
    }
    return m_snapshot.ranges.isEmpty() ? -1 : m_snapshot.ranges.size() - 1;
}
void TextMergeSession::adjustRanges(int position, int removed, int added, int owner, bool manual)
{
    const int end = position + removed;
    const int delta = added - removed;
    for (int i = 0; i < m_snapshot.ranges.size(); ++i) {
        auto &range = m_snapshot.ranges[i];
        const int oldStart = range.start;
        const int oldEnd = oldStart + range.length;
        // The replacement belongs to exactly one block. Adjacent zero-width
        // conflicts remain distinct, so choosing one never erases another.
        if (i < owner) {
            const int newEnd = oldEnd <= position ? oldEnd : qMax(position, oldEnd - removed);
            range.length = qMax(0, newEnd - oldStart);
        } else if (i == owner) {
            range.start = qMin(oldStart, position);
            range.length = qMax(0, qMax(oldEnd, end) + delta - range.start);
        } else {
            range.start = oldStart >= end ? oldStart + delta : position + added;
            const int newEnd = oldEnd >= end ? oldEnd + delta : position + added;
            range.length = qMax(0, newEnd - range.start);
        }
        if (manual && (i == owner || (oldStart < end && oldEnd > position))) {
            range.manuallyEdited = true;
            // Editing cannot silently declare an unresolved conflict resolved.
            if (range.resolution != Merge::Resolution::Unresolved) range.resolution = Merge::Resolution::Manual;
        }
    }
}

bool TextMergeSession::setOutputText(const QString &text, QString *error)
{
    if (state() != State::Open) return reject(error, tr("Open the merge before editing its output."));
    const QString previous = outputText();
    if (previous == text) { if (error) error->clear(); return true; }
    Text::Document next = m_snapshot.output;
    if (!next.setNormalizedText(text, error)) return false;
    const auto beforeLines = Text::Document::splitLines(previous);
    const auto afterLines = Text::Document::splitLines(text);
    Text::CompareOptions exact;
    exact.ignoreEol = false;
    const auto alignment = Text::compare(beforeLines, afterLines, exact);
    QVector<int> offsets;
    int offset = 0;
    for (const auto &line : beforeLines) {
        offsets.append(offset);
        offset += line.text.size() + (line.eol == Text::Eol::None ? 0 : 1);
    }
    offsets.append(offset);
    // Map whole output lines back to source blocks. Character prefix/suffix
    // edits can assign a later block's terminator to an unrelated conflict.
    auto ownerForLine = [&](int line) {
        const int position = offsets[qBound(0, line, offsets.size() - 1)];
        for (int i = 0; i < m_snapshot.ranges.size(); ++i) {
            const auto &range = m_snapshot.ranges[i];
            if (position >= range.start && position < range.start + range.length) return i;
        }
        return blockAt(position);
    };
    pushUndo();
    auto ranges = m_snapshot.ranges;
    for (auto &range : ranges) range.length = 0;
    // 「这次手工编辑把几个块粘在了一起」必须按**一处改动**判断，不能按单个差异块。
    // TXT-005 起，一段改写在这份对齐里可能是「删除 + 新增」两块：单看插入块的话，
    // 它的左右行数是 0 : m，`joinsBlocks` 里的 `leftCount != rightCount` 照样成立，
    // 但 `firstOwner` 与 `finalOwner` 会落在同一个块上，于是恒假——「用户已经把这处
    // 输出改成跨块的内容」这件事被漏掉，`resolveBlock()` 会在他改过的输出上照旧
    // 套用某一侧的原文。归并成一处改动之后，两侧行数之比与首末归属都回到了原来的口径。
    const QVector<Text::DifferenceRun> runs = Text::differenceRuns(alignment);
    QVector<int> runOfBlock(alignment.blocks.size(), -1);
    QVector<int> runFirstOld(runs.size(), 0), runOldCount(runs.size(), 0);
    QVector<int> runFirstNew(runs.size(), 0);
    QVector<int> runFinalOwner(runs.size(), -1), runJoinsBlocks(runs.size(), 0);
    for (int index = 0; index < runs.size(); ++index) {
        int leftTotal = 0, rightTotal = 0;
        for (int b = runs[index].firstBlock; b <= runs[index].lastBlock; ++b) {
            runOfBlock[b] = index;
            leftTotal += alignment.blocks[b].leftCount;
            rightTotal += alignment.blocks[b].rightCount;
        }
        const auto &first = alignment.blocks[runs[index].firstBlock];
        runFirstOld[index] = first.leftStart;
        runOldCount[index] = leftTotal;
        // 一处改动里第一块与最后一块的**新增侧起点相同**（`compare()` 的删除块与
        // 紧随其后的新增块共用同一个 `b`），所以取第一块的 `rightStart` 就够了。
        runFirstNew[index] = first.rightStart;
        runFinalOwner[index] = leftTotal
            ? ownerForLine(first.leftStart + leftTotal - 1)
            : blockAt(offsets[qBound(0, first.leftStart, offsets.size() - 1)]);
        runJoinsBlocks[index] = ownerForLine(first.leftStart) != runFinalOwner[index]
            && leftTotal != rightTotal;
    }
    int lastOwner = 0;
    for (int b = 0; b < alignment.blocks.size(); ++b) {
        const auto &block = alignment.blocks[b];
        const int run = runOfBlock[b];
        const bool changedBlock = run >= 0;
        // 相同块的行一定左右成对（`Equal` / `Ignored` 都是按行发出的），
        // 走到这个兜底分支只可能是差异块，所以它的口径是「改动末尾」。
        const int blockAnchor = block.leftCount
            ? ownerForLine(block.leftStart + block.leftCount - 1)
            : blockAt(offsets[qBound(0, block.leftStart, offsets.size() - 1)]);
        for (int r = block.firstRow; r < block.firstRow + block.rowCount; ++r) {
            const auto &row = alignment.rows[r];
            int owner = -1;
            if (row.leftLine >= 0) {
                owner = ownerForLine(row.leftLine);
            } else if (!changedBlock) {
                owner = blockAnchor;
            } else {
                // 纯新增行没有对应的旧行，只能用**位置**找它的对应物：一处改动里
                // 「第 i 个新增行 ↔ 第 i 个旧行」。这个对应关系只存在于这一层——
                // 逐行去看 `Row` 是看不出来的（纯新增行的 `leftLine` 是 -1）。
                // 它决定手工编辑后的输出行归属哪个块，也就决定「改过的块还能不能
                // 被某一侧整体覆盖」（`selectingFirstBlockPreservesLaterManualEdits`
                // 守的就是这一条）。新增行比旧行多时，多出来的那些归给改动末尾。
                const int offset = row.rightLine - runFirstNew[run];
                const int counterpart = runFirstOld[run] + offset;
                const bool inside = runOldCount[run] > 0 && offset >= 0
                    && counterpart < runFirstOld[run] + runOldCount[run];
                owner = inside ? ownerForLine(counterpart) : runFinalOwner[run];
            }
            owner = qMax(lastOwner, owner);
            if (owner < 0 || owner >= ranges.size()) continue;
            if (changedBlock) {
                ranges[owner].manuallyEdited = true;
                if (ranges[owner].resolution != Merge::Resolution::Unresolved)
                    ranges[owner].resolution = Merge::Resolution::Manual;
            }
            if (row.rightLine < 0) continue;
            lastOwner = owner;
            const auto &line = afterLines[row.rightLine];
            ranges[owner].length += line.text.size() + (line.eol == Text::Eol::None ? 0 : 1);
            if (changedBlock && runJoinsBlocks[run]) ranges[owner].manualGroup = true;
        }
    }
    int start = 0;
    for (auto &range : ranges) {
        range.start = start;
        start += range.length;
    }
    m_snapshot.ranges = ranges;
    m_snapshot.output = next;
    changed();
    return true;
}

bool TextMergeSession::resolveBlock(int index, Merge::Resolution choice, QString *error)
{
    if (state() != State::Open) return reject(error, tr("Open the merge before resolving conflicts."));
    if (index < 0 || index >= m_result.blocks.size()) return reject(error, tr("Select a difference or conflict first."));
    const auto &block = m_result.blocks[index];
    if (block.kind == Merge::Kind::Unchanged) return reject(error, tr("The selected block has no difference."));
    if ((choice == Merge::Resolution::LeftThenRight || choice == Merge::Resolution::RightThenLeft)
        && block.kind != Merge::Kind::Conflict) return reject(error, tr("Both-side choices apply to conflicts only."));
    if (choice == Merge::Resolution::Base && !hasBase()) return reject(error, tr("This two-way merge has no base source."));
    if (choice == Merge::Resolution::Manual || choice == Merge::Resolution::Unresolved) {
        if (block.kind != Merge::Kind::Conflict) return reject(error, tr("The selected block is not a conflict."));
        if (m_snapshot.ranges[index].resolution == choice) { if (error) error->clear(); return true; }
        pushUndo();
        m_snapshot.ranges[index].resolution = choice;
        changed();
        return true;
    }
    if (m_snapshot.ranges[index].manualGroup)
        return reject(error, tr("Manual editing joined content from several blocks here. Undo that edit before choosing a source, or review the output and mark the conflict resolved."));
    QVector<Text::Line> lines;
    switch (choice) {
    case Merge::Resolution::Left: lines = block.left; break;
    case Merge::Resolution::Right: lines = block.right; break;
    case Merge::Resolution::Base: lines = block.base; break;
    case Merge::Resolution::LeftThenRight: lines = block.left; lines += block.right; break;
    case Merge::Resolution::RightThenLeft: lines = block.right; lines += block.left; break;
    default: return reject(error, tr("Unsupported merge choice."));
    }
    for (int i = 0; i + 1 < lines.size(); ++i)
        if (lines[i].eol == Text::Eol::None) lines[i].eol = m_snapshot.output.preferredEol();
    const MergeOutputRange range = m_snapshot.ranges[index];
    const QString current = outputText();
    if (!lines.isEmpty() && range.start > 0 && current[range.start - 1] != QLatin1Char('\n'))
        return reject(error, tr("This block follows a manually edited line without a line break. Insert a line break or undo that edit before choosing a source."));
    if (!lines.isEmpty() && lines.last().eol == Text::Eol::None
        && range.start + range.length < current.size())
        return reject(error, tr("This source choice has no final line break and would join the next edited block. Edit the output directly or undo the boundary edit."));
    QString raw = actualText(m_snapshot.output.lines());
    const int start = rawOffset(m_snapshot.output.lines(), range.start);
    const int end = rawOffset(m_snapshot.output.lines(), range.start + range.length);
    raw.replace(start, end - start, actualText(lines));
    Text::Document next = m_snapshot.output;
    if (!next.replaceLines(0, next.lines().size(), Text::Document::splitLines(raw), error)) return false;
    pushUndo();
    adjustRanges(range.start, range.length, normalizedText(lines).size(), index, false);
    m_snapshot.output = next;
    m_snapshot.ranges[index].resolution = choice;
    m_snapshot.ranges[index].manuallyEdited = false;
    changed();
    return true;
}
bool TextMergeSession::resolveCurrent(Merge::Resolution choice, QString *error)
{
    return resolveBlock(m_currentBlock, choice, error);
}
bool TextMergeSession::markCurrentResolved(bool resolved, QString *error)
{
    return resolveCurrent(resolved ? Merge::Resolution::Manual : Merge::Resolution::Unresolved, error);
}
bool TextMergeSession::selectBlock(int block)
{
    if (block < 0 || block >= m_result.blocks.size()) return false;
    m_currentBlock = block;
    emit currentBlockChanged(block);
    return true;
}
void TextMergeSession::selectOutputPosition(int position)
{
    const int block = blockAt(position);
    if (block != m_currentBlock) selectBlock(block);
}
void TextMergeSession::previousConflict()
{
    for (int i = (m_currentBlock < 0 ? m_result.blocks.size() : m_currentBlock) - 1; i >= 0; --i)
        if (m_result.blocks[i].kind == Merge::Kind::Conflict
            && m_snapshot.ranges[i].resolution == Merge::Resolution::Unresolved) { selectBlock(i); updateStatus(); return; }
    setStatusText(tr("No earlier unresolved conflict."));
}
void TextMergeSession::nextConflict()
{
    for (int i = m_currentBlock + 1; i < m_result.blocks.size(); ++i)
        if (m_result.blocks[i].kind == Merge::Kind::Conflict
            && m_snapshot.ranges[i].resolution == Merge::Resolution::Unresolved) { selectBlock(i); updateStatus(); return; }
    setStatusText(tr("No later unresolved conflict."));
}
void TextMergeSession::undo()
{
    if (state() != State::Open || m_undo.isEmpty()) return;
    m_redo.append(m_snapshot); m_snapshot = m_undo.takeLast(); changed();
}
void TextMergeSession::redo()
{
    if (state() != State::Open || m_redo.isEmpty()) return;
    m_undo.append(m_snapshot); m_snapshot = m_redo.takeLast(); changed();
}
void TextMergeSession::updateStatus()
{
    const int total = m_result.conflictCount();
    const int unresolved = unresolvedCount();
    const QString name = m_file.path().isEmpty() ? tr("Untitled output") : QFileInfo(m_file.path()).fileName();
    setTitle(unresolved ? tr("%1 — %2 unresolved").arg(name).arg(unresolved) : tr("%1 — Merge").arg(name));
    QString status = tr("%1 conflicts • %2 resolved • %3 unresolved • Output: %4, %5")
        .arg(total).arg(total - unresolved).arg(unresolved)
        .arg(QString::fromLatin1(m_snapshot.output.codecName()), m_snapshot.output.eolDescription());
    if (!hasBase()) status += tr(" • No base: two-way mode; differing regions require a choice");
    if (m_left.codecName() != m_right.codecName() || (hasBase() && m_base.codecName() != m_left.codecName()))
        status += tr(" • Source encodings differ; output uses %1").arg(QString::fromLatin1(m_snapshot.output.codecName()));
    if (m_left.eolDescription() != m_right.eolDescription() || (hasBase() && m_base.eolDescription() != m_left.eolDescription()))
        status += tr(" • Source line endings differ; selected line endings are preserved");
    if (m_result.alignmentLimited) status += tr(" • Alignment limit reached; ambiguous changes require review");
    if (!unresolved) status += tr(" • All conflicts resolved; output can be saved");
    setStatusText(status);
}
}
