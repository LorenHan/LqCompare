#include "foldercomparesession.h"
#include "foldercompareview.h"
#include "maskfilter.h"

#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>

#include <cmath>

namespace LqCompare {
namespace {

const QString maskKey = QStringLiteral("folder.scanMaskDeclaration");
const QString caseKey = QStringLiteral("folder.nameCaseSensitivity");
const QString recursiveKey = QStringLiteral("folder.recursive");
const QString contentKey = QStringLiteral("folder.compareContent");
const QString depthKey = QStringLiteral("folder.maximumDepth");
const QString bytesKey = QStringLiteral("folder.compareFirstBytes");

QString optionsError(const Folder::Options &options)
{
    if (options.nameCaseSensitivity != Qt::CaseSensitive
        && options.nameCaseSensitivity != Qt::CaseInsensitive)
        return QObject::tr("名称大小写策略无效；必须选择区分或忽略大小写。");
    if (options.maximumDepth < 0 || options.maximumDepth > 256)
        return QObject::tr("递归深度上限必须是 0 到 256 之间的整数。");
    if (options.compareFirstBytes < 0)
        return QObject::tr("「只比较前 N 字节」必须是 0（关闭）或正整数。");
    const auto parsed = Filter::MaskFilter::parse(options.scanMaskDeclaration);
    return parsed.ok() ? QString() : QObject::tr("扫描掩码无效：\n%1").arg(parsed.describeErrors());
}

bool exactInteger(const QVariant &value, int minimum, int maximum, int *number)
{
    switch (value.type()) {
    case QVariant::Int: case QVariant::UInt: case QVariant::LongLong:
    case QVariant::ULongLong: case QVariant::Double: break;
    default: return false;
    }
    bool ok = false;
    const double candidate = value.toDouble(&ok);
    if (!ok || !std::isfinite(candidate) || candidate < minimum || candidate > maximum
        || std::floor(candidate) != candidate)
        return false;
    *number = static_cast<int>(candidate);
    return true;
}

// 字节预算只接受非负整数，且**必须是整数值**。
//
// 为什么不像 depth 那样宽松到「能转成整数就算」：0.5 被静默截断成 0 恰好等于
// 「关闭」——用户设了个值，得到的却是默认行为，而且没有任何反馈。
// 这类「值被悄悄改掉」的设置错比「值非法被拒绝」难发现得多。
// 允许 Double 只是为了兼容 QVariant 里以浮点存储的整数值（如 JSON 反序列化）。
bool exactByteCount(const QVariant &value, qint64 *bytes)
{
    switch (value.type()) {
    case QVariant::Int: case QVariant::UInt: case QVariant::LongLong:
    case QVariant::ULongLong: case QVariant::Double: break;
    default: return false;
    }
    bool ok = false;
    const double candidate = value.toDouble(&ok);
    if (!ok || !std::isfinite(candidate) || candidate < 0.0 || std::floor(candidate) != candidate)
        return false;
    *bytes = static_cast<qint64>(candidate);
    return true;
}

QString settingsOptions(SessionSettings *settings, Folder::Options *options)
{
    Folder::Options restored;
    const auto invalid = [](const QString &key) {
        return QObject::tr("文件夹会话设置 %1 的值无效；请修正后重新比较。").arg(key);
    };
    const QVariant mask = settings->value(maskKey, restored.scanMaskDeclaration);
    if (mask.type() != QVariant::String)
        return invalid(maskKey);
    restored.scanMaskDeclaration = mask.toString();
    const QVariant recursive = settings->value(recursiveKey, restored.recursive);
    if (recursive.type() != QVariant::Bool)
        return invalid(recursiveKey);
    restored.recursive = recursive.toBool();
    const QVariant content = settings->value(contentKey, restored.compareContent);
    if (content.type() != QVariant::Bool)
        return invalid(contentKey);
    restored.compareContent = content.toBool();
    int caseValue = int(restored.nameCaseSensitivity);
    if (!exactInteger(settings->value(caseKey, caseValue), int(Qt::CaseInsensitive),
                      int(Qt::CaseSensitive), &caseValue))
        return invalid(caseKey);
    restored.nameCaseSensitivity = static_cast<Qt::CaseSensitivity>(caseValue);
    if (!exactInteger(settings->value(depthKey, restored.maximumDepth), 0, 256,
                      &restored.maximumDepth))
        return invalid(depthKey);
    if (!exactByteCount(settings->value(bytesKey, restored.compareFirstBytes),
                        &restored.compareFirstBytes))
        return invalid(bytesKey);
    const QString error = optionsError(restored);
    if (!error.isEmpty())
        return error;
    *options = restored;
    return {};
}

} // namespace

FolderCompareSession::FolderCompareSession(QObject *parent)
    : CompareSession(QStringLiteral("folder"), parent)
{
    setTitle(tr("文件夹比较"));
    connect(sessionSettings(), &SessionSettings::changed, this, [this](const QString &key) {
        if (m_writingSettings)
            return;
        if (key.isEmpty() || key == maskKey || key == caseKey || key == recursiveKey
            || key == contentKey || key == depthKey || key == bytesKey)
            readComparisonSettings();
    });
    // Persist defaults too: a saved session keeps its comparison semantics if
    // a later application version changes the defaults. Display filters belong
    // exclusively to FolderCompareView and intentionally never enter this store.
    setComparisonOptions(m_options);
    connect(&m_scanner, &Folder::Scanner::progressChanged, this, [this](int entries, const QString &path) {
        if (m_closing || m_pendingScan)
            return;
        reportProgress(entries, 0, path);
        updateStatus(tr("正在扫描：%1 项 · %2").arg(entries).arg(path));
    });
    connect(&m_scanner, &Folder::Scanner::finished, this, [this](const Folder::Result &result) {
        if (m_closing)
            return;
        if (m_pendingScan) {
            m_pendingScan = false;
            QString error;
            if (!startScan(&error)) {
                if (view())
                    view()->setScanning(false);
                updateStatus(error);
            }
            return;
        }
        if (result.leftRoot != m_leftPath || result.rightRoot != m_rightPath) {
            if (view())
                view()->setScanning(false);
            updateStatus(tr("来源已改变，请开始比较。"));
            return;
        }
        m_result = result;
        m_hasResult = true;
        if (view()) {
            view()->setScanning(false);
            view()->setResult(result);
        }
        int same = 0, different = 0, unresolved = 0;
        for (const auto &entry : result.entries) {
            if (!entry.inComparison())
                continue;
            if (entry.status == Folder::Status::Same)
                ++same;
            else if (entry.status == Folder::Status::Error || entry.status == Folder::Status::Unknown)
                ++unresolved;
            else
                ++different;
        }
        QString status = result.cancelled ? tr("扫描已取消，结果不完整。 ")
            : result.complete ? tr("比较完成。 ") : tr("比较完成，包含未比较或读取失败的条目。 ");
        status += tr("相同 %1 · 不同/独有/冲突 %2 · 未确认 %3").arg(same).arg(different).arg(unresolved);
        if (!result.scanMaskDeclaration.trimmed().isEmpty())
            status += tr(" · 仅比较扫描掩码范围（排除 %1 项，未比较其内容）").arg(result.excludedCount);
        if (!result.error.isEmpty())
            status += QStringLiteral("\n") + result.error;
        if (!result.warnings.isEmpty())
            status += QStringLiteral("\n") + result.warnings.join(QLatin1Char(' '));
        updateStatus(status);
        reportProgress(0, 0);
        emit scanFinished(m_result);
    });
}

FolderCompareSession::FolderCompareSession(const QString &leftPath, const QString &rightPath, QObject *parent)
    : FolderCompareSession(parent)
{
    setPaths(leftPath, rightPath);
}

FolderCompareSession::~FolderCompareSession()
{
    doClose();
}

void FolderCompareSession::setPaths(const QString &leftPath, const QString &rightPath)
{
    const auto normalize = [](const QString &path) {
        return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    };
    const QString left = normalize(leftPath);
    const QString right = normalize(rightPath);
    if (left == m_leftPath && right == m_rightPath)
        return;
    m_leftPath = left;
    m_rightPath = right;
    m_pendingScan = false;
    m_hasResult = false;
    m_result = Folder::Result();
    if (m_scanner.isRunning())
        m_scanner.cancel();
    if (view()) {
        view()->setPaths(left, right);
        view()->setResult(m_result);
    }
    setTitle(left.isEmpty() && right.isEmpty() ? tr("文件夹比较")
        : tr("%1 ↔ %2").arg(QFileInfo(left).fileName(), QFileInfo(right).fileName()));
    emit pathsChanged(left, right);
}

FolderCompareView *FolderCompareSession::view() const
{
    return qobject_cast<FolderCompareView *>(widget());
}

QWidget *FolderCompareSession::createView(QWidget *parent)
{
    auto *view = new FolderCompareView(parent);
    view->setPaths(m_leftPath, m_rightPath);
    view->setOptions(m_options);
    view->setScanning(m_scanner.isRunning());
    if (m_hasResult)
        view->setResult(m_result);
    if (!statusText().isEmpty())
        view->setStatus(statusText());
    connect(view, &FolderCompareView::compareRequested, this, [this](const QString &left, const QString &right) {
        QString error;
        // An explicit Compare action can replace invalid restored settings with
        // the valid form. Ordinary open() must never silently erase bad input.
        if (!setComparisonOptions(this->view()->options(), &error)) {
            updateStatus(error);
            return;
        }
        setPaths(left, right);
        const bool ok = state() == State::Open ? reload(&error) : open(&error);
        if (!ok)
            updateStatus(error);
    });
    connect(view, &FolderCompareView::cancelRequested, this, &FolderCompareSession::cancelScan);
    connect(view, &FolderCompareView::compareFilesRequested, this, &FolderCompareSession::compareFilesRequested);
    connect(view, &FolderCompareView::navigationStatus, this, &FolderCompareSession::updateStatus);
    return view;
}

bool FolderCompareSession::doOpen(QString *error)
{
    if (!m_settingsError.isEmpty()) {
        if (error)
            *error = m_settingsError;
        return false;
    }
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) {
        updateStatus(tr("选择两个文件夹，开始比较。"));
        return true;
    }
    return startScan(error);
}

bool FolderCompareSession::doReload(QString *error)
{
    return startScan(error);
}

bool FolderCompareSession::startScan(QString *error)
{
    if (!m_settingsError.isEmpty()) {
        if (error)
            *error = m_settingsError;
        return false;
    }
    for (const QString &path : {m_leftPath, m_rightPath}) {
        if (path.isEmpty() || !QFileInfo(path).isDir()) {
            if (error)
                *error = tr("请选择存在的文件夹：%1").arg(path);
            return false;
        }
    }
    const Folder::Options options = view() ? view()->options() : m_options;
    if (!setComparisonOptions(options, error))
        return false;
    if (m_scanner.isRunning()) {
        m_pendingScan = true;
        m_scanner.cancel();
        updateStatus(tr("正在停止上一次扫描，随后重新比较…"));
        return true;
    }
    m_scanner.start(m_leftPath, m_rightPath, m_options);
    if (view())
        view()->setScanning(true);
    updateStatus(tr("正在扫描文件夹…"));
    return true;
}

void FolderCompareSession::cancelScan()
{
    m_pendingScan = false;
    m_scanner.cancel();
    if (m_scanner.isRunning())
        updateStatus(tr("正在取消扫描，已扫描条目将保留…"));
}

bool FolderCompareSession::setComparisonOptions(const Folder::Options &options, QString *error)
{
    const QString reason = state() == State::Closed ? tr("此文件夹会话已关闭。") : optionsError(options);
    if (!reason.isEmpty()) {
        if (error)
            *error = reason;
        return false;
    }
    // Validate the entire value first. A rejected mask, enum or depth must not
    // partially replace any field, stored value or form control.
    const QScopedValueRollback<bool> writing(m_writingSettings, true);
    m_options = options;
    m_settingsError.clear();
    auto *settings = sessionSettings();
    settings->setValue(maskKey, options.scanMaskDeclaration);
    settings->setValue(caseKey, int(options.nameCaseSensitivity));
    settings->setValue(recursiveKey, options.recursive);
    settings->setValue(contentKey, options.compareContent);
    settings->setValue(depthKey, options.maximumDepth);
    settings->setValue(bytesKey, options.compareFirstBytes);
    if (view())
        view()->setOptions(options);
    if (error)
        error->clear();
    // Like setPaths(), this stages the sources/settings. open()/reload() starts
    // the scan so restoring a session never accidentally starts it twice.
    return true;
}

void FolderCompareSession::readComparisonSettings()
{
    Folder::Options restored;
    const QString error = settingsOptions(sessionSettings(), &restored);
    m_settingsError = error;
    if (!error.isEmpty()) {
        // Keep the last valid options and form intact. The invalid persisted
        // value remains visible in the settings store for diagnosis, and open /
        // reload refuse it instead of comparing with an unexpected fallback.
        updateStatus(error);
        return;
    }
    m_options = restored;
    if (view())
        view()->setOptions(restored);
    if (state() == State::Open)
        updateStatus(tr("比较设置已更新，重新比较后生效。"));
}

void FolderCompareSession::previousDifference() { if (view()) view()->previousDifference(); }
void FolderCompareSession::nextDifference() { if (view()) view()->nextDifference(); }
void FolderCompareSession::firstDifference() { if (view()) view()->firstDifference(); }
void FolderCompareSession::lastDifference() { if (view()) view()->lastDifference(); }
void FolderCompareSession::selectAllDifferences() { if (view()) view()->selectAllDifferences(); }

void FolderCompareSession::doClose()
{
    m_closing = true;
    m_pendingScan = false;
    m_scanner.cancel();
    if (view())
        view()->setEnabled(false);
}

void FolderCompareSession::updateStatus(const QString &status)
{
    setStatusText(status);
    if (view())
        view()->setStatus(status);
}

} // namespace LqCompare
