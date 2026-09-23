#include "textcomparesession.h"
#include "linesimilarity.h"
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
        // 出厂值从 `CompareOptions` 取，不在这一行再写一个 true / 50：
        // 默认值只能有一个来源，否则界面、命令行与文档会各说各话。
        const Text::CompareOptions defaults;
        m_options.alignSimilarLines = settings->value(QStringLiteral("text.alignSimilarLines"),
                                                      defaults.alignSimilarLines).toBool();
        // 阈值走 `clampSimilarityThreshold()`：会话文件是用户手改得动的，
        // 落在外面的值要压回边界（命令行那条路会直接报错，见 clioptions.cpp）。
        m_options.similarityThreshold = Text::clampSimilarityThreshold(
            settings->value(QStringLiteral("text.similarityThreshold"),
                            defaults.similarityThreshold).toInt());
        // BOM 策略按**标识符**存取，不按枚举序号。序号是隐式的第二事实来源：
        // 往枚举中间插一个取值，设置文件里那个整数就会换一个含义，
        // 而现象是「升级之后比较规则自己变了」，没有任何东西会红。
        // 认不出来的标识符由 `bomPolicyFromIdentifier()` 退回默认档
        // （返回值是「认不认识」，档本身走输出参数——刻意不把两者混成一个值，
        // 否则「认不出来」就只能表达成某一个**合法**档，调用方再也分不清）。
        Text::bomPolicyFromIdentifier(
            settings->value(QStringLiteral("text.bomPolicy"),
                            QString::fromLatin1(Text::bomPolicyIdentifier(defaults.bomPolicy)))
                .toString(),
            &m_options.bomPolicy);
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
    // 保存侧的 BOM 策略跨重新加载保留：它是会话级偏好，而 `Document` 在每次
    // `loadPair()` 时都会被整体替换。忘了这一句的表现是「改了策略、
    // 一重新加载就悄悄回到默认」，而用户只会在保存之后才发现文件带了 BOM。
    m_left.setBomSavePolicy(m_leftBomSavePolicy);
    m_right.setBomSavePolicy(m_rightBomSavePolicy);
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
    settings->setValue(QStringLiteral("text.alignSimilarLines"), options.alignSimilarLines);
    settings->setValue(QStringLiteral("text.similarityThreshold"),
                       Text::clampSimilarityThreshold(options.similarityThreshold));
    // 表外取值退回默认档的标识符，**不写一个空串**：空串读回来虽然也会落到默认档，
    // 但设置文件里会留下一处「有键无值」，排查时看不出它是有意的还是一处损坏。
    const char *policyIdentifier = Text::bomPolicyIdentifier(options.bomPolicy);
    settings->setValue(QStringLiteral("text.bomPolicy"),
                       QString::fromLatin1(policyIdentifier
                                               ? policyIdentifier
                                               : Text::bomPolicyIdentifier(Text::defaultBomPolicy())));
    m_options = options;
    recompute();
}

void TextCompareSession::recompute()
{
    m_result = Text::compare(m_left.lines(), m_right.lines(), m_options);
    // 把引擎的块列表归并成「一处改动」：判据与理由见 `Text::differenceRuns()`。
    // 归并逻辑只此一份——命令行摘要数「差异数」时走的也是它。
    m_differenceRuns = Text::differenceRuns(m_result);
    m_currentDifference = m_differenceRuns.isEmpty() ? -1
        : qBound(0, m_currentDifference, m_differenceRuns.size() - 1);
    setDirty(m_left.isModified() || m_right.isModified());
    emit comparisonChanged();
    updateStatus();
}

int TextCompareSession::differenceFirstBlock(int index) const
{
    if (index < 0 || index >= m_differenceRuns.size()) return -1;
    return m_differenceRuns[index].firstBlock;
}

int TextCompareSession::differenceLastBlock(int index) const
{
    if (index < 0 || index >= m_differenceRuns.size()) return -1;
    return m_differenceRuns[index].lastBlock;
}

int TextCompareSession::differenceIndexOfBlock(int block) const
{
    for (int index = 0; index < m_differenceRuns.size(); ++index) {
        const Text::DifferenceRun &run = m_differenceRuns.at(index);
        if (block >= run.firstBlock && block <= run.lastBlock) return index;
    }
    return -1;
}

void TextCompareSession::updateStatus()
{
    QString status = tr("%1 difference block(s), %2 ignored • Left: %3, %4 • Right: %5, %6")
        .arg(m_differenceRuns.size()).arg(m_result.ignoredBlocks)
        .arg(QString::fromLatin1(m_left.codecName()), m_left.eolDescription(),
             QString::fromLatin1(m_right.codecName()), m_right.eolDescription());
    if (m_options.ignoreEol) status += tr(" • Line endings ignored");
    if (m_options.ignoreFinalNewline) status += tr(" • Final newline ignored");
    // BOM 这一维的结论（TXT-015 第 2 条）。原来这里写死的是一句
    // 「BOM differs (metadata only)」——它把「两侧 BOM 不同」**无条件**说成
    // 只是元数据、与结论无关。那正是规格不允许的：BOM 差异算不算差异
    // 由策略决定，而这句文案必须说清它落在了哪一档（被忽略 / 计为不同）。
    // 措辞取自 `Text::bomConclusionSummary()`，不在界面这一层另写一份。
    const Text::BomVerdict bom = bomVerdict();
    if (bom.differs()) status += QStringLiteral(" • ") + Text::bomConclusionSummary(bom.conclusion);
    if (m_left.codecName() != m_right.codecName()) status += tr(" • Encoding differs (metadata only)");
    if (m_result.alignmentLimited) status += tr(" • Alignment work limit reached; unmatched range shown as replacement");
    // 相似度配对没做成（工作量超限）与对齐受限是两件事，各自有各自的提示：
    // 合成一句会让用户按错误的旋钮去调（见 `Result::similarityPairingLimited`）。
    if (m_result.similarityPairingLimited) status += tr(" • Similar-line pairing skipped for an oversized change; lines paired by position");
    if (!m_left.warning().isEmpty()) status += tr(" • Left: %1").arg(m_left.warning());
    if (!m_right.warning().isEmpty()) status += tr(" • Right: %1").arg(m_right.warning());
    if (m_leftReadOnly) status += tr(" • Left read-only");
    if (m_rightReadOnly) status += tr(" • Right read-only");
    // 行尾混合时把严重度抬到 `Warning`（状态栏据此亮警告图标，TXT-010 第 4 条）。
    // 判据取自 `Document::hasMixedEndings()` 而不是在这里嗅 `eolDescription()` 的
    // 文字——文案是给用户看的，判定不该跟着文案走。
    //
    // **两侧任意一侧混合就警告**：混合行尾本身是要处理的问题（提交进版本库后
    // 会让 diff 工具反复报同一批行），而状态栏只有一行，没法按侧分别亮两个图标。
    // 到底哪一侧混合，文本里两个 `eolDescription()` 已经分别写清楚了。
    const StatusSeverity severity = (m_left.hasMixedEndings() || m_right.hasMixedEndings()
                                     // 被判成「真差异」的 BOM 差异也要抬到 `Warning`：
                                     // 此时行级差异块是 0，状态栏若按普通信息显示，
                                     // 用户看到的是一行「0 difference block(s)」再加一句
                                     // 被淹没在里面的说明——而「这两份文件其实不同」
                                     // 恰恰是最需要显眼的一件事。
                                     || bom.countedAsDifference())
        ? StatusSeverity::Warning : StatusSeverity::Normal;
    setStatusText(status, severity);
}

Text::BomVerdict TextCompareSession::bomVerdict() const
{
    // 未打开（或打开失败、已关闭）时不下结论：那时候 `m_left` / `m_right` 只是两个
    // 默认构造的文档，「两侧都没有 BOM」会被读成「BOM 状态相同」，
    // 于是状态栏在打开之前就印出一句关于 BOM 的话。
    return Text::evaluateBom(m_options.bomPolicy,
                             Text::observeBom(m_left, m_right, state() == State::Open));
}

Text::Conclusion TextCompareSession::conclusion() const
{
    return Text::conclude(m_result, bomVerdict());
}

void TextCompareSession::setBomSavePolicy(bool left, Text::BomSavePolicy policy)
{
    if (left) m_leftBomSavePolicy = policy;
    else m_rightBomSavePolicy = policy;
    (left ? m_left : m_right).setBomSavePolicy(policy);
    // 保存策略不影响比较结论，但它**影响 `Document::isModified()`**：把原文件带 BOM
    // 的一侧设成「不写入 BOM」会让 `bytes()` 与原始字节不同，于是这一侧立刻变成
    // 「有未保存修改」。所以必须走一次 `recompute()` 把脏标记与状态栏一起刷新——
    // 漏了它，界面上的 Save 按钮会一直是灰的，用户改完策略反而存不下去。
    recompute();
}

Text::BomSavePolicy TextCompareSession::bomSavePolicy(bool left) const
{
    return left ? m_leftBomSavePolicy : m_rightBomSavePolicy;
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
    const Text::Document &source = leftToRight ? m_left : m_right;
    if (!source.canEdit())
        return reject(error, tr("The source cannot be copied safely: %1").arg(source.warning()));
    Text::Document &destination = leftToRight ? m_right : m_left;
    const auto before = bufferState();
    // 复制的范围是**一整处改动**而不是一个块：TXT-005 起一处改动可能由
    // 相邻的若干块拼成（不够像的行各自成块），只搬其中一块会让目标侧多出一截，
    // 而用户按的是「复制这一处」。
    //
    // 区间由「首块起点」到「末块终点」推出，而不是把各块长度相加：
    // 块序列在两侧都是首尾相接的（引擎保证），因此两端一减就是准确长度，
    // 也不怕将来块类型再增加。
    const int first = differenceFirstBlock(m_currentDifference);
    const int last = differenceLastBlock(m_currentDifference);
    if (first < 0 || last < first) return reject(error, tr("Select a difference first."));
    const Text::Block &head = m_result.blocks[first];
    const Text::Block &tail = m_result.blocks[last];
    const int sourceStart = leftToRight ? head.leftStart : head.rightStart;
    const int sourceCount = (leftToRight ? tail.leftStart + tail.leftCount
                                         : tail.rightStart + tail.rightCount) - sourceStart;
    const int targetStart = leftToRight ? head.rightStart : head.leftStart;
    const int targetCount = (leftToRight ? tail.rightStart + tail.rightCount
                                         : tail.leftStart + tail.leftCount) - targetStart;
    const auto lines = source.lines().mid(sourceStart, sourceCount);
    if (!destination.replaceLines(targetStart, targetCount, lines, error)) return false;
    recordChange(before);
    recompute();
    if (m_currentDifference >= 0) emit currentDifferenceChanged(m_currentDifference);
    return true;
}

void TextCompareSession::selectDifference(int index)
{
    // 边界按「一处改动」数（`m_differenceRuns`）而不是引擎块数：用户按「下一处」
    // 应当在**整处**改动之间走，而不是在同一处改动的两个半块之间停一下。
    if (index < 0 || index >= m_differenceRuns.size()) {
        setStatusText(m_differenceRuns.isEmpty() ? tr("No differences under the current comparison rules.")
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
void TextCompareSession::lastDifference() { selectDifference(m_differenceRuns.size() - 1); }

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
