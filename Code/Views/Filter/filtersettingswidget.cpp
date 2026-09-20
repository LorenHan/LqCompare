#include "filtersettingswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace LqCompare {
using namespace Filter;
namespace {
QLabel *label(const QString &text, QWidget *parent, const char *name = nullptr)
{
    auto *result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (name) result->setObjectName(QString::fromLatin1(name));
    return result;
}
QComboBox *caseSelector(QWidget *parent)
{
    auto *result = new QComboBox(parent);
    result->addItem(QStringLiteral("大小写：跟随平台"), -1);
    result->addItem(QStringLiteral("大小写敏感"), int(Qt::CaseSensitive));
    result->addItem(QStringLiteral("忽略大小写"), int(Qt::CaseInsensitive));
    return result;
}
template<typename Issue>
void highlightIssues(QPlainTextEdit *editor, const QVector<Issue> &issues)
{
    QList<QTextEdit::ExtraSelection> selections;
    for (const auto &issue : issues) {
        if (issue.line < 0) continue;
        const QTextBlock block = editor->document()->findBlockByNumber(issue.line);
        if (!block.isValid()) continue;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        const int length = block.text().size();
        const int start = qBound(0, issue.column, length);
        selection.cursor.setPosition(block.position() + start);
        selection.cursor.setPosition(block.position() + qMin(length, start + qMax(1, issue.length)), QTextCursor::KeepAnchor);
        selection.format.setUnderlineColor(QColor(190, 45, 45));
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setBackground(QColor(255, 230, 230));
        if (!selection.cursor.hasSelection()) selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.append(selection);
    }
    editor->setExtraSelections(selections);
}
QTableWidgetItem *cell(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setToolTip(text);
    return item;
}
QString prefixedExpression(NameMatchMode mode, const QString &text)
{
    return nameMatchModePrefix(mode) + (mode == NameMatchMode::Wildcard ? QString() : QStringLiteral(" ")) + text.trimmed();
}
}

MaskFilterWidget::MaskFilterWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_layer = new QComboBox(this);
    m_layer->setObjectName(QStringLiteral("maskLayer"));
    for (FilterLayer layer : allFilterLayers()) m_layer->addItem(filterLayerLabel(layer), int(layer));
    m_enabled = new QCheckBox(QStringLiteral("启用此层"), this);
    m_enabled->setObjectName(QStringLiteral("maskLayerEnabled"));
    m_case = caseSelector(this);
    m_case->setObjectName(QStringLiteral("maskCaseSensitivity"));
    auto *help = new QPushButton(QStringLiteral("? 掩码语法速查"), this);
    help->setObjectName(QStringLiteral("maskHelp"));
    top->addWidget(m_layer); top->addWidget(m_enabled); top->addStretch(); top->addWidget(m_case); top->addWidget(help);
    layout->addLayout(top);
    m_source = label({}, this, "maskLayerSource"); layout->addWidget(m_source);
    layout->addWidget(label(QStringLiteral("每行一条掩码；前导 - 表示排除，# 表示注释。排除优先；没有包含规则时默认保留全部。"), this));
    m_editor = new QPlainTextEdit(this);
    m_editor->setObjectName(QStringLiteral("maskDeclaration"));
    m_editor->setAccessibleName(QStringLiteral("当前层掩码声明"));
    m_editor->setPlaceholderText(QStringLiteral("*.cpp\n*.h\n- build/**"));
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setMinimumHeight(100);
    layout->addWidget(m_editor, 1);
    m_errors = label({}, this, "maskErrors"); layout->addWidget(m_errors);
    auto *saveRow = new QHBoxLayout;
    m_save = new QPushButton(QStringLiteral("保存当前层声明"), this);
    m_save->setObjectName(QStringLiteral("maskSaveLayer"));
    m_saveStatus = label({}, this, "maskSaveStatus");
    saveRow->addWidget(m_save); saveRow->addWidget(m_saveStatus, 1); layout->addLayout(saveRow);
    layout->addWidget(label(QStringLiteral("最终生效过滤（掩码三层）"), this));
    m_semantics = label({}, this, "maskLayerSemantics"); layout->addWidget(m_semantics);
    m_layers = new QTableWidget(3, 4, this);
    m_layers->setObjectName(QStringLiteral("effectiveFilterLayers"));
    m_layers->setHorizontalHeaderLabels({QStringLiteral("来源 / 保存位置"), QStringLiteral("启用"), QStringLiteral("状态"), QStringLiteral("表达式")});
    m_layers->verticalHeader()->hide();
    m_layers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_layers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_layers->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_layers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_layers->setMinimumHeight(142);
    layout->addWidget(m_layers);
    m_expression = label({}, this, "effectiveFilterExpression"); layout->addWidget(m_expression);
    m_summary = label({}, this, "maskPreviewSummary"); layout->addWidget(m_summary);
    auto *diagnosticRow = new QHBoxLayout;
    diagnosticRow->addWidget(label(QStringLiteral("为什么看不到："), this));
    m_diagnosticPath = new QLineEdit(this);
    m_diagnosticPath->setObjectName(QStringLiteral("filterDiagnosticPath"));
    m_diagnosticPath->setPlaceholderText(QStringLiteral("输入文件名或相对路径，例如 src/main.cpp"));
    diagnosticRow->addWidget(m_diagnosticPath, 1); layout->addLayout(diagnosticRow);
    m_diagnosis = label({}, this, "filterDiagnosis"); layout->addWidget(m_diagnosis);
    connect(m_layer, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshEditor(); });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!filterLayerIsEditable(currentLayer())) return;
        m_stack.setDeclaration(currentLayer(), m_editor->toPlainText());
        m_saveStatus->clear(); refreshResult(); emit filterChanged();
    });
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) { setLayerEnabled(currentLayer(), enabled); });
    connect(m_case, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (m_case->currentData().toInt() < 0) m_stack.clearCaseSensitivityOverride();
        else m_stack.setCaseSensitivity(Qt::CaseSensitivity(m_case->currentData().toInt()));
        refreshResult(); emit filterChanged();
    });
    connect(m_layers, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == 1) setLayerEnabled(allFilterLayers().at(item->row()), item->checkState() == Qt::Checked);
    });
    connect(m_save, &QPushButton::clicked, this, [this] { saveLayer(currentLayer()); });
    connect(help, &QPushButton::clicked, this, &MaskFilterWidget::showSyntaxReference);
    connect(m_diagnosticPath, &QLineEdit::textChanged, this, [this](const QString &path) { m_diagnosis->setText(diagnosePath(path)); });
    setCurrentLayer(FilterLayer::View);
    refreshResult();
}

void MaskFilterWidget::setFilterStack(const FilterStack &stack)
{
    m_stack = stack;
    QSignalBlocker blocker(m_case);
    const int value = stack.isCaseSensitivityOverridden() ? int(stack.filter(FilterLayer::View).caseSensitivity()) : -1;
    m_case->setCurrentIndex(m_case->findData(value));
    refreshEditor(); refreshResult(); emit filterChanged();
}
void MaskFilterWidget::setBinder(const FilterLayerBinder &binder)
{
    m_binder = binder;
    reloadFromStores();
}
void MaskFilterWidget::reloadFromStores()
{
    m_binder.loadInto(&m_stack);
    m_saveStatus->clear(); refreshEditor(); refreshResult(); emit filterChanged();
}
bool MaskFilterWidget::saveLayer(FilterLayer layer)
{
    QString message;
    if (!m_stack.layerErrors(layer).isEmpty()) message = QStringLiteral("请先修正此层的掩码错误，再保存声明。");
    else if (!m_binder.saveLayer(layer, m_stack)) message = QStringLiteral("此层不可写或未连接对应存储，声明未保存。");
    if (!message.isEmpty()) {
        m_saveStatus->setText(message); emit saveFailed(message); return false;
    }
    m_saveStatus->setText(QStringLiteral("已保存至%1；启用开关与大小写选择仅影响当前过滤栈。")
                         .arg(filterLayerStorageLabel(filterLayerStorage(layer))));
    emit layerSaved(); return true;
}
FilterLayer MaskFilterWidget::currentLayer() const { return FilterLayer(m_layer->currentData().toInt()); }
void MaskFilterWidget::setCurrentLayer(FilterLayer layer) { m_layer->setCurrentIndex(m_layer->findData(int(layer))); }
void MaskFilterWidget::setDeclaration(FilterLayer layer, const QString &declaration)
{
    m_stack.setDeclaration(layer, declaration); m_saveStatus->clear();
    if (layer == currentLayer()) refreshEditor();
    refreshResult(); emit filterChanged();
}
void MaskFilterWidget::setLayerEnabled(FilterLayer layer, bool enabled)
{
    if (m_stack.isLayerEnabled(layer) == enabled) return;
    m_stack.setLayerEnabled(layer, enabled); refreshEditor(); refreshResult(); emit filterChanged();
}
void MaskFilterWidget::setPreviewSubjects(const QVector<MaskSubject> &subjects) { m_subjects = subjects; refreshResult(); }
void MaskFilterWidget::setPreviewNames(const QStringList &names)
{
    QVector<MaskSubject> subjects;
    for (const QString &name : names) subjects.append(MaskSubject::forName(name));
    setPreviewSubjects(subjects);
}
QString MaskFilterWidget::errorText() const { return m_stack.describeErrors(); }
void MaskFilterWidget::refreshEditor()
{
    const QSignalBlocker editorBlocker(m_editor), enabledBlocker(m_enabled);
    m_editor->setPlainText(m_stack.declaration(currentLayer()));
    m_editor->setReadOnly(!filterLayerIsEditable(currentLayer()));
    m_enabled->setChecked(m_stack.isLayerEnabled(currentLayer()));
    m_save->setEnabled(m_binder.canSaveLayer(currentLayer()));
    const QString source = m_stack.layerState(currentLayer()).source;
    m_source->setText(filterLayerDescription(currentLayer()) + (source.isEmpty() ? QString() : QStringLiteral("\n") + source));
    highlightIssues(m_editor, m_stack.layerErrors(currentLayer()));
}
void MaskFilterWidget::refreshResult()
{
    m_panel = buildEffectiveFilterPanel(m_stack, m_subjects);
    m_errors->setText(errorText()); m_errors->setVisible(!errorText().isEmpty());
    highlightIssues(m_editor, m_stack.layerErrors(currentLayer()));
    m_semantics->setText(m_panel.semantics);
    m_expression->setText(QStringLiteral("合并表达式：%1").arg(m_panel.expression.isEmpty() ? QStringLiteral("全部保留（无生效规则）") : m_panel.expression));
    m_summary->setText(m_panel.summary + QStringLiteral("；隐藏 %1 项").arg(m_panel.hidden()));
    const QSignalBlocker blocker(m_layers);
    for (int row = 0; row < m_panel.layers.size(); ++row) {
        const auto &layer = m_panel.layers.at(row);
        m_layers->setItem(row, 0, cell(layer.label + QStringLiteral("\n") + layer.storageLabel));
        auto *enabled = cell({}); enabled->setFlags(enabled->flags() | Qt::ItemIsUserCheckable);
        enabled->setCheckState(layer.enabled ? Qt::Checked : Qt::Unchecked);
        m_layers->setItem(row, 1, enabled);
        m_layers->setItem(row, 2, cell(layer.statusText));
        m_layers->setItem(row, 3, cell(layer.expression));
    }
    m_layers->resizeRowsToContents();
    m_diagnosis->setText(diagnosePath(m_diagnosticPath->text()));
    emit validationChanged(!m_stack.hasErrors());
}
QString MaskFilterWidget::diagnosePath(const QString &relativePath) const
{
    if (relativePath.trimmed().isEmpty()) return {};
    const auto subject = MaskSubject::forPath(relativePath);
    QStringList lines {m_stack.decide(subject).describe()};
    for (FilterLayer layer : allFilterLayers()) {
        if (!m_stack.isLayerActive(layer)) continue;
        const auto &filter = m_stack.filter(layer);
        const auto decision = filter.decide(subject);
        if (decision.verdict != MaskVerdict::Included) lines.append(filterLayerLabel(layer) + QStringLiteral("：") + decision.describe());
        for (int index : filter.matchingRuleIndexes(subject)) {
            const auto &rule = filter.rules().at(index);
            if (rule.kind == MaskRuleKind::Exclude) lines.append(filterLayerLabel(layer) + QStringLiteral("：") + rule.describe());
        }
    }
    return lines.join(QLatin1Char('\n'));
}
void MaskFilterWidget::showSyntaxReference()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("maskSyntaxReferenceDialog"));
    dialog->setWindowTitle(QStringLiteral("掩码语法速查（离线）"));
    auto *layout = new QVBoxLayout(dialog);
    const auto reference = maskSyntaxReference();
    auto *table = new QTableWidget(reference.size(), 3, dialog);
    table->setObjectName(QStringLiteral("maskSyntaxTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("写法"), QStringLiteral("含义"), QStringLiteral("示例")});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int row = 0; row < reference.size(); ++row) {
        const auto &entry = reference.at(row);
        QStringList samples;
        for (const auto &sample : entry.samples) samples.append(sample.text + (sample.matches ? QStringLiteral(" ✓") : QStringLiteral(" ✗")));
        table->setItem(row, 0, cell(entry.pattern)); table->setItem(row, 1, cell(entry.meaning)); table->setItem(row, 2, cell(samples.join(QStringLiteral("\n"))));
    }
    table->resizeRowsToContents(); layout->addWidget(table);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons); dialog->resize(850, 520); dialog->show();
}

NameFilterWidget::NameFilterWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_combine = new QComboBox(this); m_combine->setObjectName(QStringLiteral("nameCombineMode"));
    for (NameCombineMode mode : allNameCombineModes()) m_combine->addItem(nameCombineModeLabel(mode), int(mode));
    m_case = caseSelector(this); m_case->setObjectName(QStringLiteral("nameCaseSensitivity"));
    top->addWidget(m_combine); top->addStretch(); top->addWidget(m_case); layout->addLayout(top);
    m_semantics = label({}, this, "nameFilterSemantics"); layout->addWidget(m_semantics);
    auto *addRow = new QHBoxLayout;
    m_mode = new QComboBox(this); m_mode->setObjectName(QStringLiteral("nameMatchMode"));
    for (NameMatchMode mode : allNameMatchModes()) m_mode->addItem(nameMatchModeLabel(mode), int(mode));
    m_mode->setCurrentIndex(m_mode->findData(int(NameMatchMode::Wildcard)));
    m_addition = new QLineEdit(this); m_addition->setObjectName(QStringLiteral("nameExpressionInput"));
    m_addition->setPlaceholderText(QStringLiteral("新增表达式；切换模式仅影响本次添加"));
    m_add = new QPushButton(QStringLiteral("添加表达式"), this); m_add->setObjectName(QStringLiteral("nameAddExpression"));
    addRow->addWidget(m_mode); addRow->addWidget(m_addition, 1); addRow->addWidget(m_add); layout->addLayout(addRow);
    m_additionError = label({}, this, "nameAdditionError"); layout->addWidget(m_additionError);
    layout->addWidget(label(QStringLiteral("一行一条：= 精确名；无前缀为通配符；re: 正则。三种模式均整名匹配，- 在此不表示排除。非法行不参与过滤；正则默认每条 200 ms，超时条目保留并报告问题。"), this));
    m_editor = new QPlainTextEdit(this); m_editor->setObjectName(QStringLiteral("nameDeclaration"));
    m_editor->setAccessibleName(QStringLiteral("名称过滤表达式"));
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setPlaceholderText(QStringLiteral("*.cpp\n= README.md\nre: .*\\.log"));
    layout->addWidget(m_editor, 1);
    m_errors = label({}, this, "nameFilterErrors"); layout->addWidget(m_errors);
    m_previewLabel = label({}, this, "namePreviewSummary"); layout->addWidget(m_previewLabel);
    m_previewErrors = label({}, this, "namePreviewErrors"); layout->addWidget(m_previewErrors);
    auto *presetRow = new QHBoxLayout;
    m_presetList = new QComboBox(this); m_presetList->setObjectName(QStringLiteral("namePresetList"));
    auto *apply = new QPushButton(QStringLiteral("应用预设"), this);
    m_presetName = new QLineEdit(this); m_presetName->setObjectName(QStringLiteral("namePresetName")); m_presetName->setPlaceholderText(QStringLiteral("预设名称"));
    auto *save = new QPushButton(QStringLiteral("保存预设"), this); save->setObjectName(QStringLiteral("nameSavePreset"));
    auto *exportButton = new QPushButton(QStringLiteral("导出预设…"), this);
    auto *importButton = new QPushButton(QStringLiteral("导入预设…"), this);
    presetRow->addWidget(m_presetList); presetRow->addWidget(apply); presetRow->addWidget(m_presetName, 1); presetRow->addWidget(save); presetRow->addWidget(importButton); presetRow->addWidget(exportButton);
    layout->addLayout(presetRow);
    m_presetStatus = label(QStringLiteral("预设暂存在当前面板；导出后可移交或重新导入。"), this, "namePresetStatus"); layout->addWidget(m_presetStatus);
    m_timer = new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(120);
    m_watcher = new QFutureWatcher<NameFilterWidgetPreview>(this);
    connect(m_timer, &QTimer::timeout, this, &NameFilterWidget::startPreview);
    connect(m_watcher, &QFutureWatcher<NameFilterWidgetPreview>::finished, this, [this] {
        const auto result = m_watcher->result();
        if (m_runningGeneration == m_generation && !result.cancelled) {
            m_preview = result; m_previewPending = false;
            m_previewLabel->setText(QStringLiteral("匹配 %1 项 / 共 %2 项（名称过滤）").arg(result.included).arg(result.total));
            m_previewErrors->setText(result.issues.join(QLatin1Char('\n')));
            emit previewChanged();
        } else if (!m_timer->isActive()) startPreview();
    });
    connect(m_editor, &QPlainTextEdit::textChanged, this, &NameFilterWidget::reparse);
    connect(m_combine, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NameFilterWidget::reparse);
    connect(m_case, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NameFilterWidget::reparse);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NameFilterWidget::validateAddition);
    connect(m_addition, &QLineEdit::textChanged, this, &NameFilterWidget::validateAddition);
    connect(m_add, &QPushButton::clicked, this, [this] {
        QString error;
        if (addExpression(NameMatchMode(m_mode->currentData().toInt()), m_addition->text(), &error)) m_addition->clear();
        else m_additionError->setText(error);
    });
    connect(m_addition, &QLineEdit::returnPressed, m_add, &QPushButton::click);
    connect(save, &QPushButton::clicked, this, [this] {
        QString error;
        m_presetStatus->setText(savePreset(m_presetName->text(), {}, &error) ? QStringLiteral("已保存预设；可导出为文件。") : error);
    });
    connect(apply, &QPushButton::clicked, this, [this] { applyPreset(m_presetList->currentIndex()); });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出名称过滤预设"), QStringLiteral("name-filters.txt"), QStringLiteral("文本文件 (*.txt)"));
        if (path.isEmpty()) return;
        QSaveFile file(path); const QByteArray data = exportPresets().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) m_presetStatus->setText(QStringLiteral("导出失败：") + file.errorString());
        else m_presetStatus->setText(QStringLiteral("已导出：") + path);
    });
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入名称过滤预设（替换当前预设列表）"), {}, QStringLiteral("文本文件 (*.txt);;所有文件 (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { m_presetStatus->setText(QStringLiteral("导入失败：") + file.errorString()); return; }
        QString error;
        m_presetStatus->setText(importPresets(QString::fromUtf8(file.readAll()), &error) ? QStringLiteral("已导入预设；选择一项后应用。") : error);
    });
    validateAddition(); reparse();
}
NameFilterWidget::~NameFilterWidget() { if (m_cancel) m_cancel->store(true); }
void NameFilterWidget::setDeclaration(const QString &declaration) { if (declaration != m_editor->toPlainText()) m_editor->setPlainText(declaration); }
QString NameFilterWidget::declaration() const { return m_editor->toPlainText(); }
void NameFilterWidget::setCombineMode(NameCombineMode mode) { m_combine->setCurrentIndex(m_combine->findData(int(mode))); }
NameCombineMode NameFilterWidget::combineMode() const { return NameCombineMode(m_combine->currentData().toInt()); }
void NameFilterWidget::setPlatform(MaskPlatform platform) { m_platform = platform; reparse(); validateAddition(); }
void NameFilterWidget::setCaseSensitivity(Qt::CaseSensitivity sensitivity) { m_case->setCurrentIndex(m_case->findData(int(sensitivity))); }
void NameFilterWidget::clearCaseSensitivityOverride() { m_case->setCurrentIndex(0); }
void NameFilterWidget::setPreviewNames(const QStringList &names) { m_names = names; queuePreview(); }
QString NameFilterWidget::errorText() const { return m_errors->text(); }
void NameFilterWidget::reparse()
{
    const auto parsed = NameFilter::parse(declaration(), m_platform);
    m_filter = parsed.filter; m_filter.setCombineMode(combineMode());
    if (m_case->currentData().toInt() >= 0) m_filter.setCaseSensitivity(Qt::CaseSensitivity(m_case->currentData().toInt()));
    m_errors->setText(parsed.describeErrors()); m_errors->setVisible(!parsed.issues.isEmpty());
    highlightIssues(m_editor, parsed.issues);
    m_semantics->setText(m_filter.combineSummary());
    bool valid = true;
    for (const auto &issue : parsed.issues) if (issue.kind == NameFilterIssueKind::Syntax) valid = false;
    queuePreview(); emit validationChanged(valid); emit filterChanged();
}
void NameFilterWidget::queuePreview()
{
    ++m_generation;
    if (m_cancel) m_cancel->store(true);
    m_previewPending = true; m_previewLabel->setText(QStringLiteral("正在计算名称过滤匹配数…")); m_previewErrors->clear(); m_timer->start();
}
void NameFilterWidget::startPreview()
{
    if (m_watcher->isRunning()) return;
    m_runningGeneration = m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const auto names = m_names;
    auto filter = m_filter;
    m_watcher->setFuture(QtConcurrent::run([filter, names, cancel]() mutable {
        // The default service runner is shared; never send concurrent UI previews
        // through it. Each background batch owns and releases its own runner.
        filter.setMatchRunner(std::make_shared<ThreadNameMatchRunner>());
        filter.resetTimeoutState();
        NameFilterWidgetPreview result; result.total = names.size();
        for (const QString &name : names) {
            if (cancel->load()) { result.cancelled = true; return result; }
            const auto decision = filter.decide(name);
            if (decision.accepted) ++result.included;
            result.issueCount += decision.issues.size();
            for (const auto &issue : decision.issues) {
                const QString message = issue.describe();
                if (result.issues.size() < 12 && !result.issues.contains(message)) result.issues.append(message);
            }
        }
        if (result.issueCount > result.issues.size()) result.issues.append(QStringLiteral("共 %1 个运行期问题；以上显示不同问题的前 12 项。").arg(result.issueCount));
        return result;
    }));
}
void NameFilterWidget::validateAddition()
{
    const NameMatchMode mode = NameMatchMode(m_mode->currentData().toInt());
    const auto analysis = analyzeNameFilterLine(prefixedExpression(mode, m_addition->text()), 0, m_platform);
    QStringList errors;
    if (!m_addition->text().trimmed().isEmpty()) for (const auto &issue : analysis.issues) errors.append(issue.describe());
    if (analysis.hasExpression && analysis.expression.mode != mode)
        errors.append(QStringLiteral("当前选择为通配符，请移除 = 或 re: 模式前缀，或切换匹配模式。"));
    m_additionError->setText(errors.isEmpty() ? nameMatchModeSemanticsNote(mode) : errors.join(QLatin1Char('\n')));
    m_add->setEnabled(!m_addition->text().trimmed().isEmpty() && analysis.hasExpression && !analysis.hasSyntaxError() && analysis.expression.mode == mode);
}
bool NameFilterWidget::addExpression(NameMatchMode mode, const QString &text, QString *error)
{
    const QString line = prefixedExpression(mode, text);
    const auto analysis = analyzeNameFilterLine(line, 0, m_platform);
    if (text.trimmed().isEmpty() || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')) || !analysis.hasExpression || analysis.hasSyntaxError() || analysis.expression.mode != mode) {
        if (error) *error = QStringLiteral("请输入一条符合当前模式的表达式；通配符不得以模式前缀或 # 开头。");
        return false;
    }
    QString updated = declaration();
    if (!updated.isEmpty() && !updated.endsWith(QLatin1Char('\n'))) updated += QLatin1Char('\n');
    setDeclaration(updated + line);
    if (error) error->clear();
    return true;
}
bool NameFilterWidget::savePreset(const QString &name, const QString &note, QString *error)
{
    if (name.trimmed().isEmpty()) { if (error) *error = QStringLiteral("请输入预设名称。"); return false; }
    const auto parsed = NameFilter::parse(declaration(), m_platform);
    for (const auto &issue : parsed.issues) {
        if (issue.kind == NameFilterIssueKind::Syntax) { if (error) *error = QStringLiteral("请先修正表达式错误，再保存预设。"); return false; }
    }
    NamedNameFilter preset; preset.name = name.trimmed(); preset.note = note;
    preset.declaration = declaration(); preset.combine = combineMode();
    preset.caseOverridden = m_filter.isCaseSensitivityOverridden(); preset.caseSensitivity = m_filter.caseSensitivity();
    for (const auto &existing : m_presets) {
        if (existing.name == preset.name) { if (error) *error = QStringLiteral("此名称已存在，请使用新的预设名称。"); return false; }
    }
    m_presets.append(preset); refreshPresets(); m_presetList->setCurrentIndex(m_presets.size() - 1);
    if (error) error->clear(); emit presetsChanged(); return true;
}
void NameFilterWidget::setPresets(const QVector<NamedNameFilter> &presets) { m_presets = presets; refreshPresets(); emit presetsChanged(); }
void NameFilterWidget::refreshPresets()
{
    m_presetList->clear();
    for (const auto &preset : m_presets) m_presetList->addItem(preset.name);
}
bool NameFilterWidget::applyPreset(int index)
{
    if (index < 0 || index >= m_presets.size()) return false;
    const auto preset = m_presets.at(index);
    {
        const QSignalBlocker a(m_editor), b(m_combine), c(m_case);
        m_editor->setPlainText(preset.declaration);
        m_combine->setCurrentIndex(m_combine->findData(int(preset.combine)));
        m_case->setCurrentIndex(m_case->findData(preset.caseOverridden ? int(preset.caseSensitivity) : -1));
    }
    reparse(); return true;
}
QString NameFilterWidget::exportPresets() const { return serializeNamedNameFilters(m_presets); }
bool NameFilterWidget::importPresets(const QString &text, QString *error)
{
    const auto parsed = parseNamedNameFilters(text, m_platform);
    if (!parsed.ok()) { if (error) *error = parsed.describeErrors(); return false; }
    setPresets(parsed.presets); if (error) error->clear(); return true;
}

FilterSettingsWidget::FilterSettingsWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(label(QStringLiteral("掩码层级与高级名称过滤分别预览；实际条目需同时通过启用的过滤条件。"), this));
    auto *tabs = new QTabWidget(this);
    m_masks = new MaskFilterWidget(tabs); m_names = new NameFilterWidget(tabs);
    tabs->addTab(m_masks, QStringLiteral("掩码与三层过滤")); tabs->addTab(m_names, QStringLiteral("高级名称过滤"));
    layout->addWidget(tabs);
    connect(m_masks, &MaskFilterWidget::filterChanged, this, &FilterSettingsWidget::filterChanged);
    connect(m_names, &NameFilterWidget::filterChanged, this, &FilterSettingsWidget::filterChanged);
}
void FilterSettingsWidget::setPreviewSubjects(const QVector<MaskSubject> &subjects)
{
    m_masks->setPreviewSubjects(subjects);
    QStringList names; for (const auto &subject : subjects) names.append(subject.name);
    m_names->setPreviewNames(names);
}
} // namespace LqCompare
