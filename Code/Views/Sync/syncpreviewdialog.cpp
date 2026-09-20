#include "syncpreviewdialog.h"
#include "syncbaseline.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <limits>

namespace LqCompare {
namespace {
struct ProgressState {
    QMutex mutex;
    int completed = 0;
    int total = 0;
    QString path;
};
struct RecoveryResult { bool succeeded = false; QString error; };
QString byteText(quint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024) return QString::number(double(bytes) / 1024, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(double(bytes) / (1024 * 1024), 'f', 1) + QStringLiteral(" MiB");
}
bool isDeletion(Sync::Action action)
{ return action == Sync::Action::DeleteLeft || action == Sync::Action::DeleteRight; }
bool targetIsLeft(Sync::Action action)
{
    return action == Sync::Action::CopyRightToLeft || action == Sync::Action::CreateLeftDirectory
        || action == Sync::Action::DeleteLeft;
}
QString absolutePath(const QString &root, const QString &relative)
{ return QDir(root).filePath(relative); }
QString canonicalDestination(QString path)
{
    // Resolve existing ancestor symlinks even when the export file is new.
    QStringList suffix;
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    while (!info.exists() && !info.isSymLink()) {
        suffix.prepend(info.fileName());
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath()) break;
        info.setFile(parent);
    }
    QString root = info.canonicalFilePath();
    if (root.isEmpty()) root = info.absoluteFilePath();
    for (const auto &part : suffix) root = QDir(root).filePath(part);
    return QDir::cleanPath(root);
}
bool within(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    const auto directory = canonicalDestination(root);
    return path.compare(directory, sensitivity) == 0
        || path.startsWith(directory.endsWith('/') ? directory : directory + '/', sensitivity);
}
QString kindText(const Sync::Fingerprint &value)
{
    switch (value.kind) {
    case Folder::Kind::File: return byteText(value.size);
    case Folder::Kind::Directory: return QStringLiteral("目录");
    case Folder::Kind::SymbolicLink: return QStringLiteral("符号链接");
    case Folder::Kind::Other: return QStringLiteral("特殊条目");
    case Folder::Kind::Missing: return QStringLiteral("—");
    }
    return {};
}
QString outcomeText(Sync::Outcome outcome)
{
    switch (outcome) {
    case Sync::Outcome::Succeeded: return QStringLiteral("成功");
    case Sync::Outcome::Skipped: return QStringLiteral("跳过");
    case Sync::Outcome::Failed: return QStringLiteral("失败");
    case Sync::Outcome::Cancelled: return QStringLiteral("已取消");
    }
    return {};
}
}

class SyncPreviewDialog::Private
{
public:
    enum class Work { None, Preview, Execute, Recovery, BaselineRead, BaselineWrite };
    explicit Private(SyncPreviewDialog *owner) : q(owner), executor(std::make_shared<Sync::Executor>()) {}
    SyncPreviewDialog *q;
    QLineEdit *left = nullptr;
    QLineEdit *right = nullptr;
    QComboBox *mode = nullptr;
    QComboBox *direction = nullptr;
    QComboBox *deletion = nullptr;
    QLineEdit *excluded = nullptr;
    QSpinBox *deleteCount = nullptr;
    QSpinBox *deleteMiB = nullptr;
    QPushButton *leftBrowse = nullptr;
    QPushButton *rightBrowse = nullptr;
    QPushButton *previewButton = nullptr;
    QPushButton *executeButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QPushButton *exportButton = nullptr;
    QPushButton *restoreButton = nullptr;
    QPushButton *undoButton = nullptr;
    QPushButton *loadBaselineButton = nullptr;
    QPushButton *saveBaselineButton = nullptr;
    QPushButton *clearBaselineButton = nullptr;
    QComboBox *backupChoices = nullptr;
    QTreeWidget *leftTree = nullptr;
    QTreeWidget *rightTree = nullptr;
    QTreeWidget *planTree = nullptr;
    QTreeWidget *reportTree = nullptr;
    QLabel *summaryLabel = nullptr;
    QLabel *status = nullptr;
    QLabel *baselineLabel = nullptr;
    QLabel *reportLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QTabWidget *tabs = nullptr;
    QTimer *debounce = nullptr;
    QTimer *progressTimer = nullptr;
    QFutureWatcher<Sync::Plan> *previewWatcher = nullptr;
    QFutureWatcher<Sync::Report> *executeWatcher = nullptr;
    QFutureWatcher<RecoveryResult> *recoveryWatcher = nullptr;
    QFutureWatcher<Sync::BaselineResult> *baselineReadWatcher = nullptr;
    QFutureWatcher<Sync::BaselineSaveResult> *baselineWriteWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::shared_ptr<ProgressState> progress;
    std::shared_ptr<Sync::Executor> executor;
    std::unique_ptr<Sync::Baseline> baseline;
    Sync::Options baseOptions;
    Sync::Plan plan;
    Sync::Report report;
    QVector<Sync::ItemResult> backups;
    ConfirmationHandler confirmHandler;
    QString baselineNotice;
    QString baselineOutputPath;
    QString lastTrashTarget;
    QString lastTrashLocation;
    Work work = Work::None;
    quint64 generation = 0;
    quint64 previewGeneration = 0;
    bool validPlan = false;
    bool canSaveCommon = false;
    bool pendingPreview = false;
    bool updating = false;
    bool confirming = false;
    bool canUndo = false;
    bool recoveryWasUndo = false;
    int recoveryBackup = -1;

    void build();
    void invalidate();
    void updateControls();
    void updateSummary();
    void renderPlan();
    void renderReport();
    void finishPreview();
    void finishExecution();
    void finishRecovery();
    void finishBaselineRead();
    void finishBaselineWrite();
    void setWork(Work value);
    void itemChanged(QTreeWidgetItem *item, int column);
    bool confirm(ConfirmationKind kind, const QString &text);
    bool beginRestore(const Sync::ItemResult &item, int backupIndex);
    void addSide(QTreeWidget *tree, const Sync::Item &item, bool onLeft,
                 QMap<QString, QTreeWidgetItem *> &parents);
};

void SyncPreviewDialog::Private::build()
{
    q->setWindowTitle(QStringLiteral("目录同步 · 先预演后执行"));
    q->resize(1250, 820);
    auto *layout = new QVBoxLayout(q);
    auto *paths = new QGridLayout;
    left = new QLineEdit(q); left->setObjectName("syncLeftPath");
    right = new QLineEdit(q); right->setObjectName("syncRightPath");
    leftBrowse = new QPushButton(QStringLiteral("浏览…"), q);
    rightBrowse = new QPushButton(QStringLiteral("浏览…"), q);
    paths->addWidget(new QLabel(QStringLiteral("左目录"), q), 0, 0);
    paths->addWidget(left, 0, 1); paths->addWidget(leftBrowse, 0, 2);
    paths->addWidget(new QLabel(QStringLiteral("右目录"), q), 1, 0);
    paths->addWidget(right, 1, 1); paths->addWidget(rightBrowse, 1, 2);
    layout->addLayout(paths);
    auto *rules = new QGridLayout;
    mode = new QComboBox(q); mode->setObjectName("syncMode");
    mode->addItem(QStringLiteral("更新（复制源侧独有或较新文件）"), int(Sync::Mode::Update));
    mode->addItem(QStringLiteral("镜像（目标与源一致，含删除）"), int(Sync::Mode::Mirror));
    mode->addItem(QStringLiteral("双向（基线缺失时保守处理）"), int(Sync::Mode::TwoWay));
    direction = new QComboBox(q); direction->setObjectName("syncDirection");
    direction->addItem(QStringLiteral("左 → 右"), int(Sync::Direction::LeftToRight));
    direction->addItem(QStringLiteral("右 → 左"), int(Sync::Direction::RightToLeft));
    deletion = new QComboBox(q); deletion->setObjectName("syncDeletion");
    deletion->addItem(QStringLiteral("移到回收站（默认）"), int(Sync::Deletion::Trash));
    deletion->addItem(QStringLiteral("保留，不删除"), int(Sync::Deletion::Keep));
    rules->addWidget(new QLabel(QStringLiteral("模式"), q), 0, 0); rules->addWidget(mode, 0, 1);
    rules->addWidget(new QLabel(QStringLiteral("方向"), q), 0, 2); rules->addWidget(direction, 0, 3);
    rules->addWidget(new QLabel(QStringLiteral("删除策略"), q), 0, 4); rules->addWidget(deletion, 0, 5);
    deleteCount = new QSpinBox(q); deleteCount->setObjectName("syncDeleteCount");
    deleteCount->setRange(0, std::numeric_limits<int>::max()); deleteCount->setValue(20);
    deleteCount->setSuffix(QStringLiteral(" 项"));
    deleteMiB = new QSpinBox(q); deleteMiB->setObjectName("syncDeleteMiB");
    deleteMiB->setRange(0, std::numeric_limits<int>::max()); deleteMiB->setValue(100);
    deleteMiB->setSuffix(QStringLiteral(" MiB"));
    rules->addWidget(new QLabel(QStringLiteral("额外确认：删除超过"), q), 1, 0);
    rules->addWidget(deleteCount, 1, 1);
    rules->addWidget(new QLabel(QStringLiteral("或"), q), 1, 2); rules->addWidget(deleteMiB, 1, 3);
    rules->addWidget(new QLabel(QStringLiteral("覆盖前备份 · 删除不可降级为永久删除"), q), 1, 4, 1, 2);
    excluded = new QLineEdit(q); excluded->setObjectName("syncExcludedPaths");
    excluded->setPlaceholderText(QStringLiteral("相对路径，以分号分隔；这些子树在两侧均受保护"));
    rules->addWidget(new QLabel(QStringLiteral("排除子树"), q), 2, 0); rules->addWidget(excluded, 2, 1, 1, 5);
    layout->addLayout(rules);
    baselineLabel = new QLabel(q); baselineLabel->setObjectName("syncBaselineStatus"); baselineLabel->setWordWrap(true); baselineLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(baselineLabel);
    auto *baselineButtons = new QHBoxLayout;
    loadBaselineButton = new QPushButton(QStringLiteral("加载基线…"), q); loadBaselineButton->setObjectName("syncLoadBaseline");
    saveBaselineButton = new QPushButton(QStringLiteral("保存共同状态基线…"), q); saveBaselineButton->setObjectName("syncSaveBaseline");
    clearBaselineButton = new QPushButton(QStringLiteral("解除基线"), q); clearBaselineButton->setObjectName("syncClearBaseline");
    baselineButtons->addWidget(loadBaselineButton); baselineButtons->addWidget(saveBaselineButton);
    baselineButtons->addWidget(clearBaselineButton); baselineButtons->addStretch(); layout->addLayout(baselineButtons);
    auto *buttons = new QHBoxLayout;
    previewButton = new QPushButton(QStringLiteral("生成预演"), q); previewButton->setObjectName("syncPreview");
    executeButton = new QPushButton(QStringLiteral("确认并执行…"), q); executeButton->setObjectName("syncExecute");
    exportButton = new QPushButton(QStringLiteral("导出计划文本…"), q);
    cancelButton = new QPushButton(QStringLiteral("取消当前操作"), q); cancelButton->setObjectName("syncCancel");
    buttons->addWidget(previewButton); buttons->addWidget(executeButton); buttons->addWidget(exportButton);
    buttons->addStretch(); buttons->addWidget(cancelButton); layout->addLayout(buttons);
    summaryLabel = new QLabel(QStringLiteral("请先生成预演。"), q);
    summaryLabel->setWordWrap(true); summaryLabel->setTextFormat(Qt::PlainText); layout->addWidget(summaryLabel);
    tabs = new QTabWidget(q);
    auto *splitter = new QSplitter(Qt::Horizontal, tabs);
    auto makeTree = [splitter](const QString &name, const QStringList &headers) {
        auto *tree = new QTreeWidget(splitter); tree->setObjectName(name);
        tree->setHeaderLabels(headers); tree->setAlternatingRowColors(true);
        tree->setUniformRowHeights(true); tree->header()->setStretchLastSection(true);
        return tree;
    };
    leftTree = makeTree("syncLeftTree", {QStringLiteral("左目录"), QStringLiteral("类型 / 大小")});
    planTree = makeTree("syncPlanTree", {QStringLiteral("动作"), QStringLiteral("相对路径"), QStringLiteral("源"),
                                       QStringLiteral("目标"), QStringLiteral("大小"), QStringLiteral("原因")});
    planTree->setColumnWidth(0, 205); planTree->setColumnWidth(1, 180);
    planTree->setColumnWidth(2, 60); planTree->setColumnWidth(3, 60); planTree->setColumnWidth(4, 80);
    rightTree = makeTree("syncRightTree", {QStringLiteral("右目录"), QStringLiteral("类型 / 大小")});
    splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 4); splitter->setStretchFactor(2, 1);
    splitter->setSizes({210, 760, 210}); tabs->addTab(splitter, QStringLiteral("同步预演"));
    auto *reportPage = new QWidget(tabs); auto *reportLayout = new QVBoxLayout(reportPage);
    reportLabel = new QLabel(QStringLiteral("尚未执行。"), reportPage);
    reportLabel->setTextFormat(Qt::PlainText); reportLabel->setWordWrap(true); reportLayout->addWidget(reportLabel);
    reportTree = new QTreeWidget(reportPage); reportTree->setObjectName("syncReportTree");
    reportTree->setHeaderLabels({QStringLiteral("结果"), QStringLiteral("相对路径"), QStringLiteral("覆盖备份位置"),
                                QStringLiteral("回收站位置"), QStringLiteral("说明")});
    reportTree->setAlternatingRowColors(true); reportTree->setUniformRowHeights(true);
    reportLayout->addWidget(reportTree);
    auto *recover = new QHBoxLayout;
    backupChoices = new QComboBox(reportPage); backupChoices->setMinimumContentsLength(30);
    backupChoices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    restoreButton = new QPushButton(QStringLiteral("恢复选中覆盖备份…"), reportPage);
    restoreButton->setObjectName("syncRestoreBackup");
    undoButton = new QPushButton(QStringLiteral("撤销最近一批回收站删除…"), reportPage);
    undoButton->setObjectName("syncUndoTrash");
    recover->addWidget(backupChoices, 1); recover->addWidget(restoreButton); recover->addWidget(undoButton);
    reportLayout->addLayout(recover);
    auto *recoveryNotice = new QLabel(QStringLiteral("覆盖备份可逐项恢复，仅在目标仍为本次同步结果时允许。回收站撤销范围以平台最近一次成功删除批次为准。"), reportPage);
    recoveryNotice->setWordWrap(true); reportLayout->addWidget(recoveryNotice);
    tabs->addTab(reportPage, QStringLiteral("执行报告与恢复")); layout->addWidget(tabs, 1);
    status = new QLabel(QStringLiteral("请先选择两个目录并生成预演。"), q);
    status->setObjectName("syncStatus"); status->setWordWrap(true); status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    progressBar = new QProgressBar(q); progressBar->setRange(0, 100); progressBar->setValue(0); layout->addWidget(progressBar);
    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, q); layout->addWidget(closeButtons);
    QObject::connect(closeButtons, &QDialogButtonBox::rejected, q, &SyncPreviewDialog::reject);
    debounce = new QTimer(q); debounce->setSingleShot(true); debounce->setInterval(350);
    QObject::connect(debounce, &QTimer::timeout, q, [this] { q->startPreview(); });
    progressTimer = new QTimer(q); progressTimer->setInterval(120);
    QObject::connect(progressTimer, &QTimer::timeout, q, [this] {
        if (!progress || work == Work::None) return;
        QMutexLocker lock(&progress->mutex);
        if (progress->total > 0) { progressBar->setRange(0, progress->total); progressBar->setValue(progress->completed); }
        status->setText(QStringLiteral("%1 %2：%3%4")
            .arg(work == Work::Preview ? QStringLiteral("正在预演") : QStringLiteral("正在处理"))
            .arg(progress->completed).arg(progress->path)
            .arg(cancelled && cancelled->load() ? QStringLiteral("（已请求取消）") : QString()));
    });
    previewWatcher = new QFutureWatcher<Sync::Plan>(q);
    executeWatcher = new QFutureWatcher<Sync::Report>(q);
    recoveryWatcher = new QFutureWatcher<RecoveryResult>(q);
    baselineReadWatcher = new QFutureWatcher<Sync::BaselineResult>(q);
    baselineWriteWatcher = new QFutureWatcher<Sync::BaselineSaveResult>(q);
    QObject::connect(previewWatcher, &QFutureWatcher<Sync::Plan>::finished, q, [this] { finishPreview(); });
    QObject::connect(executeWatcher, &QFutureWatcher<Sync::Report>::finished, q, [this] { finishExecution(); });
    QObject::connect(recoveryWatcher, &QFutureWatcher<RecoveryResult>::finished, q, [this] { finishRecovery(); });
    QObject::connect(baselineReadWatcher, &QFutureWatcher<Sync::BaselineResult>::finished, q, [this] { finishBaselineRead(); });
    QObject::connect(baselineWriteWatcher, &QFutureWatcher<Sync::BaselineSaveResult>::finished, q, [this] { finishBaselineWrite(); });
    for (auto *edit : {left, right, excluded})
        QObject::connect(edit, &QLineEdit::textChanged, q, [this] { if (!updating) invalidate(); });
    for (auto *combo : {mode, direction, deletion})
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), q, [this] { if (!updating) invalidate(); });
    QObject::connect(deleteCount, QOverload<int>::of(&QSpinBox::valueChanged), q, [this](int value) {
        if (!updating) { baseOptions.deleteCountThreshold = value; invalidate(); }
    });
    QObject::connect(deleteMiB, QOverload<int>::of(&QSpinBox::valueChanged), q, [this](int value) {
        if (!updating) { baseOptions.deleteBytesThreshold = quint64(value) * 1024 * 1024; invalidate(); }
    });
    QObject::connect(leftBrowse, &QPushButton::clicked, q, [this] {
        const auto path = QFileDialog::getExistingDirectory(q, QStringLiteral("选择左目录"), left->text());
        if (!path.isEmpty()) left->setText(path);
    });
    QObject::connect(rightBrowse, &QPushButton::clicked, q, [this] {
        const auto path = QFileDialog::getExistingDirectory(q, QStringLiteral("选择右目录"), right->text());
        if (!path.isEmpty()) right->setText(path);
    });
    QObject::connect(previewButton, &QPushButton::clicked, q, [this] { q->startPreview(); });
    QObject::connect(executeButton, &QPushButton::clicked, q, [this] { q->executePlan(); });
    QObject::connect(cancelButton, &QPushButton::clicked, q, &SyncPreviewDialog::cancelOperation);
    QObject::connect(planTree, &QTreeWidget::itemChanged, q, [this](QTreeWidgetItem *item, int column) { itemChanged(item, column); });
    QObject::connect(exportButton, &QPushButton::clicked, q, [this] {
        const auto path = QFileDialog::getSaveFileName(q, QStringLiteral("导出计划到同步目录之外"), {}, QStringLiteral("文本文件 (*.txt)"));
        if (path.isEmpty()) return;
        QString error;
        status->setText(q->exportPlan(path, &error) ? QStringLiteral("计划已导出：") + path : error);
    });
    QObject::connect(restoreButton, &QPushButton::clicked, q, [this] {
        const int index = backupChoices->currentData().toInt();
        if (index >= 0 && index < backups.size()) beginRestore(backups.at(index), index);
    });
    QObject::connect(undoButton, &QPushButton::clicked, q, [this] { q->undoLastTrash(); });
    QObject::connect(loadBaselineButton, &QPushButton::clicked, q, [this] {
        const auto path = QFileDialog::getOpenFileName(q, QStringLiteral("加载同步基线"), {}, QStringLiteral("JSON 基线 (*.json)"));
        if (!path.isEmpty()) q->loadBaseline(path);
    });
    QObject::connect(saveBaselineButton, &QPushButton::clicked, q, [this] {
        const auto path = QFileDialog::getSaveFileName(q, QStringLiteral("保存共同状态基线到同步目录之外"), {}, QStringLiteral("JSON 基线 (*.json)"));
        if (!path.isEmpty()) q->saveBaseline(path);
    });
    QObject::connect(clearBaselineButton, &QPushButton::clicked, q, &SyncPreviewDialog::clearBaseline);
    updateControls();
}

void SyncPreviewDialog::Private::setWork(Work value)
{
    const bool wasBusy = work != Work::None;
    work = value;
    if (value != Work::None) {
        progressBar->setRange(0, 0); progressTimer->start();
    } else {
        progressTimer->stop(); progressBar->setRange(0, 100); progressBar->setValue(100);
    }
    updateControls();
    if (wasBusy != (value != Work::None)) emit q->busyChanged(value != Work::None);
}

void SyncPreviewDialog::Private::invalidate()
{
    ++generation;
    validPlan = false;
    plan = {};
    planTree->clear(); leftTree->clear(); rightTree->clear();
    summaryLabel->setText(QStringLiteral("路径或规则已更改，旧计划已失效。请重新生成预演。"));
    if (work == Work::Preview && cancelled) cancelled->store(true);
    pendingPreview = !left->text().trimmed().isEmpty() && !right->text().trimmed().isEmpty();
    if (pendingPreview) debounce->start(); else debounce->stop();
    updateControls();
    emit q->planInvalidated();
}

void SyncPreviewDialog::Private::updateControls()
{
    const bool writing = work != Work::None && work != Work::Preview;
    for (auto *edit : {left, right, excluded}) edit->setEnabled(!writing);
    for (auto *widget : {static_cast<QWidget *>(mode), static_cast<QWidget *>(deletion),
                         static_cast<QWidget *>(deleteCount), static_cast<QWidget *>(deleteMiB),
                         static_cast<QWidget *>(leftBrowse), static_cast<QWidget *>(rightBrowse)}) widget->setEnabled(!writing);
    direction->setEnabled(!writing && Sync::Mode(mode->currentData().toInt()) != Sync::Mode::TwoWay);
    previewButton->setEnabled(work == Work::None && !left->text().trimmed().isEmpty() && !right->text().trimmed().isEmpty());
    bool selected = false;
    for (const auto &item : plan.items) if (item.selected && Sync::isActionable(item.action)) { selected = true; break; }
    executeButton->setEnabled(work == Work::None && validPlan && plan.executable() && selected && !confirming);
    executeButton->setToolTip(!validPlan ? QStringLiteral("请先生成完整预演") : QStringLiteral("仅执行已勾选项；冲突不会自动解决"));
    exportButton->setEnabled(work == Work::None && validPlan);
    cancelButton->setEnabled(work == Work::Preview || work == Work::Execute);
    planTree->setEnabled(work == Work::None && validPlan);
    restoreButton->setEnabled(work == Work::None && backupChoices->count() > 0 && !confirming);
    undoButton->setEnabled(work == Work::None && canUndo && !confirming);
    loadBaselineButton->setEnabled(work == Work::None && !confirming);
    saveBaselineButton->setEnabled(work == Work::None && validPlan && !confirming && canSaveCommon);
    saveBaselineButton->setToolTip(QStringLiteral("仅完整、未过滤、两侧全部一致的预演可保存共同状态基线；保存后不会自动执行"));
    clearBaselineButton->setEnabled(work == Work::None && bool(baseline) && !confirming);
    const bool twoWay = Sync::Mode(mode->currentData().toInt()) == Sync::Mode::TwoWay;
    baselineLabel->setVisible(twoWay || !excluded->text().isEmpty() || !baselineNotice.isEmpty());
    QStringList notices;
    if (!baselineNotice.isEmpty()) notices << baselineNotice;
    if (twoWay && validPlan && baseline && !plan.baselineUsed)
        notices << QStringLiteral("提供的基线与当前目录对或规则不匹配；本次按无基线模式预演。");
    else if (twoWay) notices << (baseline && baseline->complete
        ? QStringLiteral("已提供基线；服务将核验目录对与规则是否匹配。")
        : QStringLiteral("未提供有效基线：两侧存在且内容不同的文件将标为冲突；不会推断两侧都发生过修改。"));
    if (!excluded->text().isEmpty()) notices << QStringLiteral("已按排除子树限定范围；被排除条目在两侧都不会参与删除。 ");
    baselineLabel->setText(notices.join('\n'));
}

void SyncPreviewDialog::Private::updateSummary()
{
    const auto summary = Sync::summarize(plan);
    QString text = summary.text();
    if (!plan.warnings.isEmpty()) text += '\n' + plan.warnings.join('\n');
    if (summary.conflicts) text += QStringLiteral("\n冲突条目不可执行，请先检查并解决后重新预演。");
    summaryLabel->setText(text);
    updateControls();
}

void SyncPreviewDialog::Private::addSide(QTreeWidget *tree, const Sync::Item &item, bool onLeft,
                                        QMap<QString, QTreeWidgetItem *> &parents)
{
    const auto &fingerprint = onLeft ? item.left : item.right;
    if (!fingerprint.exists()) return;
    const auto parts = item.relativePath.split('/', Qt::SkipEmptyParts);
    QString path;
    QTreeWidgetItem *parent = nullptr;
    for (const auto &part : parts) {
        path = path.isEmpty() ? part : path + '/' + part;
        auto *node = parents.value(path);
        if (!node) {
            node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
            node->setText(0, part); parents.insert(path, node);
        }
        parent = node;
    }
    if (parent) {
        parent->setText(1, kindText(fingerprint));
        parent->setToolTip(0, absolutePath(onLeft ? plan.leftRoot : plan.rightRoot, item.relativePath));
        if (!fingerprint.error.isEmpty()) parent->setToolTip(1, fingerprint.error);
    }
}

void SyncPreviewDialog::Private::renderPlan()
{
    QSignalBlocker blocker(planTree);
    planTree->clear(); leftTree->clear(); rightTree->clear();
    QMap<int, QTreeWidgetItem *> groups;
    QMap<QString, QTreeWidgetItem *> leftParents, rightParents;
    for (int i = 0; i < plan.items.size(); ++i) {
        auto &item = plan.items[i];
        addSide(leftTree, item, true, leftParents); addSide(rightTree, item, false, rightParents);
        const bool actionable = Sync::isActionable(item.action);
        if (!actionable) item.selected = false;
        const int group = item.action == Sync::Action::Conflict ? 0 : isDeletion(item.action) ? 1
            : !actionable ? 4 : targetIsLeft(item.action) ? 3 : 2;
        auto *section = groups.value(group);
        if (!section) {
            const QString labels[] = {QStringLiteral("⚠ 冲突（不可执行）"), QStringLiteral("删除到回收站"),
                                      QStringLiteral("左 → 右"), QStringLiteral("右 → 左"), QStringLiteral("跳过")};
            section = new QTreeWidgetItem(); section->setText(0, labels[group]);
            section->setData(0, Qt::UserRole, -1);
            QFont font = section->font(0); font.setBold(true); section->setFont(0, font);
            section->setFlags(Qt::ItemIsEnabled);
            if (actionable) {
                section->setFlags(section->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
                section->setCheckState(0, Qt::Unchecked);
            }
            groups.insert(group, section);
        }
        auto *row = new QTreeWidgetItem(section); row->setData(0, Qt::UserRole, i);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (actionable) {
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(0, item.selected ? Qt::Checked : Qt::Unchecked);
        }
        QString action = Sync::actionLabel(item.action);
        if ((item.action == Sync::Action::CopyLeftToRight && item.right.exists())
            || (item.action == Sync::Action::CopyRightToLeft && item.left.exists())) action += QStringLiteral("（覆盖，先备份）");
        if (isDeletion(item.action)) action += QStringLiteral("（回收站）");
        row->setText(0, action); row->setText(1, item.relativePath);
        const QString leftPath = absolutePath(plan.leftRoot, item.relativePath);
        const QString rightPath = absolutePath(plan.rightRoot, item.relativePath);
        if (actionable) {
            row->setText(2, isDeletion(item.action) ? QStringLiteral("—") : targetIsLeft(item.action) ? QStringLiteral("右") : QStringLiteral("左"));
            row->setText(3, targetIsLeft(item.action) ? QStringLiteral("左") : QStringLiteral("右"));
            row->setToolTip(2, isDeletion(item.action) ? QString() : targetIsLeft(item.action) ? rightPath : leftPath);
            row->setToolTip(3, targetIsLeft(item.action) ? leftPath : rightPath);
            const auto &source = isDeletion(item.action) ? (targetIsLeft(item.action) ? item.left : item.right)
                                                      : (targetIsLeft(item.action) ? item.right : item.left);
            row->setText(4, kindText(source));
        }
        row->setText(5, item.reason); row->setToolTip(5, item.reasonCode + ": " + item.reason);
        if (item.action == Sync::Action::Conflict || isDeletion(item.action)) {
            row->setIcon(0, q->style()->standardIcon(QStyle::SP_MessageBoxWarning));
            for (int column = 0; column < row->columnCount(); ++column)
                row->setForeground(column, QBrush(item.action == Sync::Action::Conflict ? QColor(180, 35, 35) : QColor(155, 77, 0)));
        }
    }
    for (auto *section : groups) { planTree->addTopLevelItem(section); section->setExpanded(true); }
    leftTree->expandToDepth(1); rightTree->expandToDepth(1);
    updateSummary();
}

void SyncPreviewDialog::Private::itemChanged(QTreeWidgetItem *item, int column)
{
    if (column != 0 || !validPlan || work != Work::None) return;
    // Read every leaf after Qt's tristate propagation, including group toggles.
    Q_UNUSED(item)
    for (int group = 0; group < planTree->topLevelItemCount(); ++group) {
        auto *section = planTree->topLevelItem(group);
        for (int row = 0; row < section->childCount(); ++row) {
            auto *leaf = section->child(row); const int index = leaf->data(0, Qt::UserRole).toInt();
            if (index >= 0 && index < plan.items.size())
                plan.items[index].selected = Sync::isActionable(plan.items[index].action) && leaf->checkState(0) == Qt::Checked;
        }
    }
    updateSummary();
}

bool SyncPreviewDialog::Private::confirm(ConfirmationKind kind, const QString &text)
{
    if (confirmHandler) return confirmHandler(kind, text);
    if (kind == ConfirmationKind::LargeDelete) {
        bool accepted = false;
        const auto phrase = QInputDialog::getText(q, QStringLiteral("删除超过安全阈值"),
            text + QStringLiteral("\n请输入「确认删除」以继续："), QLineEdit::Normal, {}, &accepted);
        return accepted && phrase == QStringLiteral("确认删除");
    }
    return QMessageBox::warning(q, QStringLiteral("确认文件系统操作"), text,
                                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

void SyncPreviewDialog::Private::finishPreview()
{
    const auto result = previewWatcher->result();
    const bool obsolete = previewGeneration != generation;
    const bool wasCancelled = cancelled && cancelled->load();
    setWork(Work::None);
    if (!obsolete && !wasCancelled) {
        plan = result;
        validPlan = plan.executable();
        canSaveCommon = validPlan && Sync::commonBaseline(plan).complete;
        renderPlan();
        status->setText(validPlan ? QStringLiteral("预演完成；尚未写入文件。请审阅勾选项后确认执行。")
                                  : QStringLiteral("无法执行此计划：") + (plan.error.isEmpty() ? QStringLiteral("预演未完成，请重新预演。") : plan.error));
        emit q->previewReady();
    } else if (!obsolete) {
        validPlan = false; status->setText(QStringLiteral("预演已取消，没有生成可执行计划。"));
    }
    if (pendingPreview) { pendingPreview = false; debounce->start(0); }
    updateControls();
}

void SyncPreviewDialog::Private::renderReport()
{
    reportTree->clear();
    if (!report.error.isEmpty()) {
        auto *failure = new QTreeWidgetItem(reportTree,
            {QStringLiteral("执行被拒绝"), QStringLiteral("整个计划"), {}, {}, report.error});
        failure->setToolTip(4, report.error);
        for (int col = 0; col < failure->columnCount(); ++col)
            failure->setForeground(col, QBrush(QColor(180, 35, 35)));
    }
    for (const auto &result : report.items) {
        auto *row = new QTreeWidgetItem(reportTree, {outcomeText(result.outcome), result.item.relativePath,
                                                   result.backupPath, result.trashedPath, result.message});
        for (int col = 0; col < row->columnCount(); ++col) row->setToolTip(col, row->text(col));
        if (result.outcome == Sync::Outcome::Failed)
            for (int col = 0; col < row->columnCount(); ++col) row->setForeground(col, QBrush(QColor(180, 35, 35)));
        if (!result.backupPath.isEmpty()) {
            const int index = backups.size(); backups.push_back(result);
            backupChoices->addItem(result.item.relativePath + QStringLiteral(" — ") + result.backupPath, index);
        }
        if (!result.trashedPath.isEmpty() && result.outcome == Sync::Outcome::Succeeded) {
            canUndo = true; lastTrashTarget = result.targetPath; lastTrashLocation = result.trashedPath;
        }
    }
    reportLabel->setText(QStringLiteral("完成 %1 项，失败 %2 项%3。%4\n备份目录：%5")
        .arg(report.succeededCount()).arg(report.failedCount())
        .arg(report.cancelled ? QStringLiteral("（已取消，已完成操作保留）") : QString())
        .arg(report.error).arg(report.backupDirectory) + QStringLiteral("\n恢复日志：") + report.journalPath);
    reportLabel->setToolTip(report.text());
    tabs->setCurrentIndex(1);
}

void SyncPreviewDialog::Private::finishExecution()
{
    report = executeWatcher->result();
    renderReport();
    setWork(Work::None);
    validPlan = false;
    emit q->executionFinished();
    // A fresh preview never reuses confirmation or auto-executes any operation.
    q->startPreview();
}

void SyncPreviewDialog::Private::finishRecovery()
{
    const auto result = recoveryWatcher->result();
    setWork(Work::None);
    status->setText(result.succeeded ? QStringLiteral("恢复完成；请检查重新生成的预演。") : QStringLiteral("恢复失败：") + result.error);
    reportLabel->setText(reportLabel->text() + '\n' + status->text());
    if (result.succeeded && recoveryWasUndo) canUndo = false;
    if (result.succeeded && recoveryBackup >= 0) {
        const int choice = backupChoices->findData(recoveryBackup);
        if (choice >= 0) backupChoices->removeItem(choice);
    }
    emit q->recoveryFinished(result.succeeded);
    q->startPreview();
}

bool SyncPreviewDialog::Private::beginRestore(const Sync::ItemResult &item, int backupIndex)
{
    if (work != Work::None || confirming || item.backupPath.isEmpty()) return false;
    confirming = true; updateControls();
    const bool accepted = confirm(ConfirmationKind::RestoreBackup,
        QStringLiteral("恢复覆盖前内容：%1\n恢复目标：%2\n备份：%3\n当前目标必须仍是本次同步产生的内容；否则恢复会被拒绝。")
        .arg(item.item.relativePath, item.targetPath, item.backupPath));
    confirming = false; updateControls();
    if (!accepted || work != Work::None) return false;
    debounce->stop(); pendingPreview = false; validPlan = false;
    recoveryWasUndo = false; recoveryBackup = backupIndex;
    auto keptExecutor = executor;
    progress.reset();
    setWork(Work::Recovery);
    recoveryWatcher->setFuture(QtConcurrent::run([keptExecutor, item] {
        RecoveryResult result;
        result.succeeded = keptExecutor->restoreBackup(item, &result.error);
        return result;
    }));
    return true;
}

SyncPreviewDialog::SyncPreviewDialog(QWidget *parent) : QDialog(parent), d(new Private(this)) { d->build(); }
SyncPreviewDialog::~SyncPreviewDialog() { if (d->cancelled) d->cancelled->store(true); }
void SyncPreviewDialog::setDirectories(const QString &left, const QString &right)
{
    if (d->work != Private::Work::None && d->work != Private::Work::Preview) return;
    d->updating = true; d->left->setText(left); d->right->setText(right); d->updating = false; d->invalidate();
}
void SyncPreviewDialog::setOptions(const Sync::Options &options)
{
    if (d->work != Private::Work::None && d->work != Private::Work::Preview) return;
    d->updating = true; d->baseOptions = options;
    d->mode->setCurrentIndex(d->mode->findData(int(options.mode)));
    d->direction->setCurrentIndex(d->direction->findData(int(options.direction)));
    d->deletion->setCurrentIndex(d->deletion->findData(int(options.deletion)));
    d->deleteCount->setValue(options.deleteCountThreshold);
    d->deleteMiB->setValue(int(qMin<quint64>(options.deleteBytesThreshold / (1024 * 1024), std::numeric_limits<int>::max())));
    d->excluded->setText(options.excludedPaths.join(';'));
    d->updating = false; d->invalidate();
}
Sync::Options SyncPreviewDialog::options() const
{
    auto value = d->baseOptions;
    value.mode = Sync::Mode(d->mode->currentData().toInt());
    value.direction = Sync::Direction(d->direction->currentData().toInt());
    value.deletion = Sync::Deletion(d->deletion->currentData().toInt());
    value.excludedPaths = d->excluded->text().split(';', Qt::SkipEmptyParts);
    for (auto &path : value.excludedPaths) path = path.trimmed();
    return value;
}
void SyncPreviewDialog::setBaseline(const Sync::Baseline &baseline)
{
    if (d->work != Private::Work::None && d->work != Private::Work::Preview) return;
    d->baseline.reset(new Sync::Baseline(baseline)); d->baselineNotice.clear(); d->invalidate();
}
void SyncPreviewDialog::clearBaseline()
{
    if (d->work != Private::Work::None && d->work != Private::Work::Preview) return;
    d->baseline.reset(); d->baselineNotice = QStringLiteral("基线已解除；双向同步按无基线模式处理。"); d->invalidate();
}
bool SyncPreviewDialog::isBusy() const { return d->work != Private::Work::None; }
bool SyncPreviewDialog::hasValidPlan() const { return d->validPlan; }
Sync::Plan SyncPreviewDialog::currentPlan() const { return d->plan; }
Sync::Report SyncPreviewDialog::lastReport() const { return d->report; }
QString SyncPreviewDialog::statusText() const { return d->status->text(); }
bool SyncPreviewDialog::setExecutor(std::shared_ptr<Sync::Executor> executor)
{
    if (isBusy() || d->confirming || !executor || !d->backups.isEmpty() || d->canUndo) return false;
    d->executor = std::move(executor); return true;
}
void SyncPreviewDialog::setConfirmationHandler(ConfirmationHandler handler) { d->confirmHandler = std::move(handler); }

bool SyncPreviewDialog::startPreview()
{
    if (d->confirming) return false;
    d->debounce->stop();
    if (isBusy()) {
        if (d->work == Private::Work::Preview) { d->pendingPreview = true; d->cancelled->store(true); }
        return false;
    }
    d->pendingPreview = false;
    if (d->left->text().trimmed().isEmpty() || d->right->text().trimmed().isEmpty()) {
        d->status->setText(QStringLiteral("请选择两个目录。")); return false;
    }
    d->validPlan = false; d->previewGeneration = ++d->generation;
    d->cancelled = std::make_shared<std::atomic_bool>(false);
    d->progress = std::make_shared<ProgressState>();
    const auto cancelled = d->cancelled; const auto progress = d->progress;
    const auto left = d->left->text(), right = d->right->text(); const auto opts = options();
    const auto baseline = d->baseline ? std::make_shared<Sync::Baseline>(*d->baseline) : std::shared_ptr<Sync::Baseline>();
    d->status->setText(QStringLiteral("正在扫描并生成预演，没有写入文件。"));
    d->setWork(Private::Work::Preview);
    d->previewWatcher->setFuture(QtConcurrent::run([left, right, opts, baseline, cancelled, progress] {
        return Sync::preview(left, right, opts, baseline.get(), cancelled.get(), [progress](int count, const QString &path) {
            QMutexLocker lock(&progress->mutex); progress->completed = count; progress->path = path;
        });
    }));
    return true;
}

void SyncPreviewDialog::cancelOperation()
{
    d->debounce->stop(); d->pendingPreview = false;
    if (d->cancelled && isBusy()) {
        d->cancelled->store(true);
        d->status->setText(QStringLiteral("已请求取消；正在完成安全收尾，已完成的操作会保留在报告中。"));
    }
}

bool SyncPreviewDialog::executePlan()
{
    if (isBusy() || d->confirming || !d->validPlan || !d->plan.executable()) return false;
    const auto plan = d->plan;
    const auto summary = Sync::summarize(plan);
    if (summary.copies + summary.directories + summary.deletions == 0) return false;
    const auto generation = d->generation;
    const auto digest = Sync::confirmationDigest(plan);
    const auto description = QStringLiteral("左目录：%1\n右目录：%2\n\n%3\n\n覆盖前会创建备份；删除只移到回收站。只执行本次勾选项。")
        .arg(plan.leftRoot, plan.rightRoot, summary.text());
    d->confirming = true; d->updateControls();
    const bool accepted = d->confirm(ConfirmationKind::Execute, description);
    const bool largeAccepted = accepted && (!summary.needsDeleteConfirmation
        || d->confirm(ConfirmationKind::LargeDelete, QStringLiteral("将删除 %1 项，共 %2。\n%3")
                      .arg(summary.deletions).arg(byteText(summary.deleteBytes), description)));
    d->confirming = false; d->updateControls();
    if (!accepted || !largeAccepted) { d->status->setText(QStringLiteral("已取消确认，没有执行计划。")); return false; }
    if (isBusy() || !d->validPlan || generation != d->generation || digest != Sync::confirmationDigest(d->plan)) {
        d->status->setText(QStringLiteral("确认期间计划发生变化，必须重新预演并确认。")); return false;
    }
    d->debounce->stop(); d->pendingPreview = false; d->validPlan = false;
    d->cancelled = std::make_shared<std::atomic_bool>(false); d->progress = std::make_shared<ProgressState>();
    const auto cancelled = d->cancelled; const auto progress = d->progress; const auto executor = d->executor;
    Sync::Confirmation confirmation; confirmation.planDigest = digest;
    confirmation.userConfirmed = true; confirmation.largeDeleteConfirmed = summary.needsDeleteConfirmation && largeAccepted;
    d->setWork(Private::Work::Execute);
    d->executeWatcher->setFuture(QtConcurrent::run([executor, plan, confirmation, cancelled, progress] {
        return executor->execute(plan, confirmation, cancelled.get(), [progress](int completed, int total, const QString &path) {
            QMutexLocker lock(&progress->mutex); progress->completed = completed; progress->total = total; progress->path = path;
        });
    }));
    return true;
}

bool SyncPreviewDialog::restoreBackup(int reportItemIndex)
{
    if (reportItemIndex < 0 || reportItemIndex >= d->report.items.size()) return false;
    const auto item = d->report.items.at(reportItemIndex);
    int backup = -1;
    for (int index = 0; index < d->backups.size(); ++index)
        if (d->backups[index].backupPath == item.backupPath) { backup = index; break; }
    return d->beginRestore(item, backup);
}

bool SyncPreviewDialog::undoLastTrash()
{
    if (isBusy() || d->confirming || !d->canUndo) return false;
    d->confirming = true; d->updateControls();
    const bool accepted = d->confirm(ConfirmationKind::UndoTrash,
        QStringLiteral("恢复平台记录的最近一批成功回收站删除。\n最近删除的原路径：%1\n回收站位置：%2\n原路径被占用或平台不支持时会明确报告失败。")
        .arg(d->lastTrashTarget, d->lastTrashLocation));
    d->confirming = false; d->updateControls();
    if (!accepted || isBusy()) return false;
    d->debounce->stop(); d->pendingPreview = false; d->validPlan = false;
    d->recoveryWasUndo = true; d->recoveryBackup = -1;
    const auto executor = d->executor;
    d->progress.reset();
    d->setWork(Private::Work::Recovery);
    d->recoveryWatcher->setFuture(QtConcurrent::run([executor] {
        RecoveryResult result; result.succeeded = executor->undoLastTrash(&result.error); return result;
    }));
    return true;
}

bool SyncPreviewDialog::exportPlan(const QString &path, QString *error) const
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (isBusy() || !d->validPlan) return fail(QStringLiteral("请先生成完整预演。"));
    if (path.isEmpty()) return fail(QStringLiteral("导出路径为空。"));
    const auto destination = canonicalDestination(path);
    const auto lexicalDestination = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (within(destination, d->plan.leftRoot) || within(destination, d->plan.rightRoot)
        || within(lexicalDestination, d->plan.leftRoot) || within(lexicalDestination, d->plan.rightRoot))
        return fail(QStringLiteral("请将计划导出到两个同步目录之外，避免导出文件参与后续同步。"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    const auto bytes = Sync::exportPlanText(d->plan).toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    if (error) error->clear();
    return true;
}


void SyncPreviewDialog::Private::finishBaselineRead()
{
    const auto result = baselineReadWatcher->result();
    setWork(Work::None);
    if (result.ok()) {
        baseline.reset(new Sync::Baseline(result.baseline));
        baselineNotice = QStringLiteral("已加载基线；将重新预演并核验适用范围。");
    } else {
        baseline.reset();
        baselineNotice = QStringLiteral("基线加载失败，已清除旧基线并降级为无基线模式：") + result.error;
    }
    invalidate();
    status->setText(baselineNotice);
    emit q->baselineLoaded(result.ok());
}

void SyncPreviewDialog::Private::finishBaselineWrite()
{
    const auto result = baselineWriteWatcher->result();
    setWork(Work::None);
    baselineNotice = result.ok() ? QStringLiteral("共同状态基线已保存：") + baselineOutputPath
                                : QStringLiteral("基线保存失败：") + result.error;
    status->setText(baselineNotice);
    updateControls();
    emit q->baselineSaved(result.ok());
}

bool SyncPreviewDialog::loadBaseline(const QString &path)
{
    if (isBusy() || d->confirming || path.isEmpty()) return false;
    d->debounce->stop(); d->pendingPreview = false;
    d->validPlan = false; ++d->generation; d->progress.reset();
    d->setWork(Private::Work::BaselineRead);
    d->status->setText(QStringLiteral("正在加载并校验基线…"));
    d->baselineReadWatcher->setFuture(QtConcurrent::run([path] { return Sync::loadBaseline(path); }));
    return true;
}

bool SyncPreviewDialog::saveBaseline(const QString &path)
{
    if (isBusy() || d->confirming || !d->validPlan || path.isEmpty()) return false;
    const auto baseline = Sync::commonBaseline(d->plan);
    if (!baseline.complete) {
        d->status->setText(QStringLiteral("仅完整、未过滤且两侧一致的预演可保存共同状态基线。")); return false;
    }
    const auto destination = canonicalDestination(path);
    const auto lexicalDestination = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (within(destination, d->plan.leftRoot) || within(destination, d->plan.rightRoot)
        || within(lexicalDestination, d->plan.leftRoot) || within(lexicalDestination, d->plan.rightRoot)) {
        d->status->setText(QStringLiteral("请将基线保存到两个同步目录之外，避免基线文件参与同步。")); return false;
    }
    d->debounce->stop(); d->pendingPreview = false;
    d->baselineOutputPath = path; d->progress.reset();
    d->cancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = d->cancelled;
    d->setWork(Private::Work::BaselineWrite);
    d->status->setText(QStringLiteral("正在原子保存共同状态基线…"));
    d->baselineWriteWatcher->setFuture(QtConcurrent::run([baseline, path, cancelled] {
        return Sync::saveBaseline(baseline, path, cancelled.get());
    }));
    return true;
}

void SyncPreviewDialog::reject()
{
    if (isBusy()) {
        cancelOperation();
        d->status->setText(QStringLiteral("请等待当前操作安全结束并查看报告，然后关闭窗口。"));
        return;
    }
    d->debounce->stop(); QDialog::reject();
}

} // namespace LqCompare
