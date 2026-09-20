#include "foldermergesession.h"
#include "foldermergeview.h"

#include <QDir>
#include <QFileInfo>

namespace LqCompare {
namespace {
bool reject(QString *error, const QString &reason)
{
    if (error) *error = reason;
    return false;
}
QString normalized(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Resolve every existing ancestor of a not-yet-created output. canonicalFilePath
// alone returns empty for such paths and would miss an existing symlink above it.
QString physicalPath(const QString &path)
{
    if (path.isEmpty()) return {};
    QString current = normalized(path);
    QStringList missing;
    for (;;) {
        const QFileInfo info(current);
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty()) {
            QString resolved = canonical;
            for (const QString &component : missing) resolved = QDir(resolved).filePath(component);
            return QDir::cleanPath(resolved);
        }
        // An unresolvable symlink (including a dangling link) is never a safe
        // output ancestor. Existing paths without canonical access also fail closed.
        if (info.exists() || info.isSymLink()) return {};
        const QString parent = info.absolutePath();
        if (parent == current || info.fileName().isEmpty()) return {};
        missing.prepend(info.fileName());
        current = parent;
    }
}

bool within(const QString &path, const QString &directory)
{
    // Conservative on case-sensitive volumes too: an unnecessary rejection is
    // safer than allowing a spelling alias to overwrite an input on macOS/Windows.
    return path.compare(directory, Qt::CaseInsensitive) == 0
        || path.startsWith(directory.endsWith(QLatin1Char('/')) ? directory : directory + QLatin1Char('/'), Qt::CaseInsensitive);
}
}

FolderMergeSession::FolderMergeSession(QObject *parent)
    : CompareSession(QStringLiteral("folder-merge"), parent)
{
    setTitle(tr("文件夹合并预演"));
    connect(&m_scanner, &FolderMerge::Scanner::progressChanged, this,
            [this](int count, const QString &path) {
        if (m_closing) return;
        reportProgress(count, 0, path);
        setStatusText(tr("只读扫描：%1 项 · %2").arg(count).arg(path));
    });
    connect(&m_scanner, &FolderMerge::Scanner::finished, this,
            [this](const FolderMerge::Plan &plan) {
        if (m_closing) return;
        m_plan = plan;
        setDirty(false);
        reportProgress(0, 0);
        publishPlan();
        emit scanningChanged(false);
        emit scanFinished(m_plan);
    });
}

FolderMergeSession::FolderMergeSession(const QString &base, const QString &left,
                                     const QString &right, const QString &output, QObject *parent)
    : FolderMergeSession(parent)
{
    setPaths(base, left, right, output);
}

FolderMergeSession::~FolderMergeSession() { doClose(); }

FolderMergeView *FolderMergeSession::view() const
{
    return qobject_cast<FolderMergeView *>(widget());
}

QWidget *FolderMergeSession::createView(QWidget *parent)
{
    return new FolderMergeView(this, parent);
}

bool FolderMergeSession::setPaths(const QString &base, const QString &left, const QString &right,
                                 const QString &output, QString *error, bool discardManual)
{
    if (m_closing) return reject(error, tr("会话已关闭。"));
    if (isScanning()) return reject(error, tr("请先取消扫描并等待停止，再更换来源。"));
    const FolderMerge::Paths paths{normalized(base), normalized(left), normalized(right), normalized(output)};
    if (paths.base == m_paths.base && paths.left == m_paths.left
        && paths.right == m_paths.right && paths.output == m_paths.output) {
        if (error) error->clear();
        return true;
    }
    if ((isDirty() || m_plan.manualCount()) && !discardManual)
        return reject(error, tr("更换路径会丢弃未应用的人工决策，请先明确确认丢弃。"));
    m_paths = paths;
    m_plan = FolderMerge::Plan();
    m_plan.paths = paths;
    m_plan.hasBase = !paths.base.isEmpty();
    setDirty(false);
    setTitle(tr("%1 ↔ %2 · 合并预演").arg(QFileInfo(paths.left).fileName(), QFileInfo(paths.right).fileName()));
    emit pathsChanged();
    publishPlan();
    if (error) error->clear();
    return true;
}

bool FolderMergeSession::doOpen(QString *error)
{
    if (m_paths.left.isEmpty() && m_paths.right.isEmpty()) {
        setStatusText(tr("选择左、右目录和输出路径；祖先可留空。当前仅支持只读预演。"));
        return true;
    }
    return startScan(false, error);
}

bool FolderMergeSession::doReload(QString *error) { return startScan(false, error); }

bool FolderMergeSession::rescan(bool discardManual, QString *error)
{
    if (state() != State::Open) return reject(error, tr("请先打开会话。"));
    return startScan(discardManual, error);
}

bool FolderMergeSession::startScan(bool discardManual, QString *error)
{
    if (m_closing) return reject(error, tr("会话已关闭。"));
    if (isScanning()) return reject(error, tr("扫描仍在运行，请先取消并等待停止。"));
    if ((isDirty() || m_plan.manualCount()) && !discardManual)
        return reject(error, tr("重扫会丢弃未应用的人工决策，请先明确确认丢弃。"));
    for (const QString &path : {m_paths.left, m_paths.right}) {
        if (path.isEmpty() || !QFileInfo(path).isDir())
            return reject(error, tr("请选择存在的左右目录：%1").arg(path));
    }
    if (!m_paths.base.isEmpty() && !QFileInfo(m_paths.base).isDir())
        return reject(error, tr("祖先目录不存在：%1").arg(m_paths.base));
    if (m_paths.output.isEmpty()) return reject(error, tr("请指定输出目录路径；预演不会创建该目录。"));
    if (!m_scanner.start(m_paths)) return reject(error, tr("无法开始扫描。"));
    // Clear stale decisions only after the new read-only job was accepted.
    m_plan = FolderMerge::Plan();
    m_plan.paths = m_paths;
    m_plan.hasBase = !m_paths.base.isEmpty();
    setDirty(false);
    emit planChanged();
    emit scanningChanged(true);
    setStatusText(tr("正在只读扫描三个来源；扫描不完整时禁止推导或接受删除。"));
    if (error) error->clear();
    return true;
}

void FolderMergeSession::cancelScan()
{
    if (m_closing || !isScanning()) return;
    m_scanner.cancel();
    setStatusText(tr("正在取消扫描；停止后将显示不完整计划，禁止删除。"));
}

bool FolderMergeSession::canEdit(QString *error) const
{
    if (m_closing || state() != State::Open) return reject(error, tr("会话未打开或已关闭。"));
    if (isScanning()) return reject(error, tr("扫描期间不能修改计划。"));
    return true;
}

bool FolderMergeSession::setDecision(const QString &relativePath, FolderMerge::Decision decision,
                                    FolderMerge::DirectoryPolicy policy, QString *error)
{
    if (!canEdit(error) || !FolderMerge::setDecision(m_plan, relativePath, decision, policy, error)) return false;
    setDirty(m_plan.manualCount() != 0);
    publishPlan();
    return true;
}

bool FolderMergeSession::resetDecision(const QString &relativePath, QString *error)
{
    if (!canEdit(error) || !FolderMerge::resetDecision(m_plan, relativePath, error)) return false;
    setDirty(m_plan.manualCount() != 0);
    publishPlan();
    return true;
}

bool FolderMergeSession::requestTextMerge(const QString &relativePath, QString *error)
{
    if (!canEdit(error)) return false;
    const auto *entry = m_plan.find(relativePath);
    if (!entry || !entry->canRequestTextMerge())
        return reject(error, tr("此条目不是可请求文本合并的三侧普通文件冲突。"));
    if (!m_plan.error.isEmpty()) return reject(error, m_plan.error);
    const QString destination = QDir(m_paths.output).filePath(relativePath);
    const QString outputRoot = physicalPath(m_paths.output);
    const QString outputFile = physicalPath(destination);
    if (m_paths.output.isEmpty() || outputRoot.isEmpty() || outputFile.isEmpty())
        return reject(error, tr("无法安全解析文本合并输出路径；请使用独立输出目录。"));
    for (const QString &input : {m_paths.base, m_paths.left, m_paths.right}) {
        const QString inputRoot = physicalPath(input);
        if (inputRoot.isEmpty() || within(outputRoot, inputRoot) || within(inputRoot, outputRoot)
            || within(outputFile, inputRoot))
            return reject(error, tr("文本合并输出与输入目录重叠或经符号链接指向输入；请使用独立输出目录。"));
    }
    emit textMergeRequested(QDir(m_paths.base).filePath(relativePath), QDir(m_paths.left).filePath(relativePath),
                            QDir(m_paths.right).filePath(relativePath), destination);
    if (error) error->clear();
    return true;
}

void FolderMergeSession::publishPlan()
{
    QString status = m_plan.cancelled ? tr("扫描已取消，计划不完整")
        : m_plan.complete ? tr("只读预演完成") : tr("计划不完整，禁止删除");
    status += tr(" · %1 项 · 未解决 %2 · 人工决策 %3").arg(m_plan.entries.size())
        .arg(m_plan.unresolvedCount()).arg(m_plan.manualCount());
    if (!m_plan.hasBase) status += tr(" · 无祖先：差异须人工决定");
    if (!m_plan.error.isEmpty()) status += QLatin1Char('\n') + m_plan.error;
    if (!m_plan.warnings.isEmpty()) status += QLatin1Char('\n') + m_plan.warnings.join(QLatin1Char('\n'));
    setStatusText(status);
    emit planChanged();
}

void FolderMergeSession::doClose()
{
    if (m_closing) return;
    m_closing = true;
    m_scanner.cancel();
    reportProgress(0, 0);
    if (view()) view()->setEnabled(false);
    emit scanningChanged(false);
}

}
