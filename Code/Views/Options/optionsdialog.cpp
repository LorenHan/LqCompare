#include "optionsdialog.h"

#include "fileopsoptions.h"
#include "logging.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace LqCompare {
namespace Options {
namespace {
QString categoryTitle(const QString &category)
{
    if (category == QStringLiteral("general")) return QStringLiteral("常规");
    if (category == QStringLiteral("display")) return QStringLiteral("显示与外观");
    if (category == QStringLiteral("logging")) return QStringLiteral("日志与诊断");
    if (category == QStringLiteral("fileops")) return QStringLiteral("文件操作");
    if (category == QStringLiteral("storage")) return QStringLiteral("存储与迁移");
    return category;
}

// 文件操作那几个下拉项的中文标签取自**服务层**（`Files::*Label()`），
// 不在这张全局标签表里再写一份。写两份的代价在这里尤其明显：
// 「permanent」显示成「永久删除」这件事与「选它就是永久删除」是同一条事实，
// 分家之后界面上会出现一个说不清自己在干什么的选项。
QString fileOpsChoiceLabel(const QString &key, const QString &identifier)
{
    if (key == QStringLiteral("fileops.deleteMode")) {
        Files::DeleteMode mode;
        if (Files::deleteModeFromIdentifier(identifier, &mode)) return Files::deleteModeLabel(mode);
    } else if (key == QStringLiteral("fileops.overwritePolicy")) {
        Files::OverwritePolicy policy;
        if (Files::overwritePolicyFromIdentifier(identifier, &policy))
            return Files::overwritePolicyLabel(policy);
    } else if (key == QStringLiteral("fileops.verifyAfterCopy")) {
        Files::VerifyMode mode;
        if (Files::verifyModeFromIdentifier(identifier, &mode)) return Files::verifyModeLabel(mode);
    }
    return QString();
}

QString displayValue(const QString &key, const QVariant &value)
{
    if (value.type() == QVariant::Bool)
        return value.toBool() ? QStringLiteral("开启") : QStringLiteral("关闭");
    const QString text = value.toString();
    const QString fileOpsLabel = fileOpsChoiceLabel(key, text);
    if (!fileOpsLabel.isEmpty()) return fileOpsLabel;
    if (text.isEmpty()) {
        if (key == QStringLiteral("display.contentFontFamily")) return QStringLiteral("系统等宽字体");
        if (key == QStringLiteral("logging.filePath")) return QStringLiteral("配置目录下 logs/lqcompare.log");
        return QStringLiteral("系统默认");
    }
    if (key == QStringLiteral("display.uiFontSize") && value.toInt() == 0)
        return QStringLiteral("系统默认");
    static const QHash<QString, QString> labels {
        {QStringLiteral("system"), QStringLiteral("系统默认")},
        {QStringLiteral("light"), QStringLiteral("浅色")},
        {QStringLiteral("dark"), QStringLiteral("深色")},
        {QStringLiteral("home"), QStringLiteral("返回 Home 页")},
        {QStringLiteral("exit"), QStringLiteral("退出程序")},
        {QStringLiteral("error"), QStringLiteral("错误")},
        {QStringLiteral("warning"), QStringLiteral("警告")},
        {QStringLiteral("info"), QStringLiteral("信息")},
        {QStringLiteral("debug"), QStringLiteral("调试")}
    };
    return labels.value(text, text);
}

QLabel *noteLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

OptionsDialog::OptionsDialog(Settings::OptionsRepository *repository, QWidget *parent)
    : QDialog(parent), m_repository(repository)
{
    setObjectName(QStringLiteral("optionsDialog"));
    setWindowTitle(QStringLiteral("程序选项"));
    resize(860, 650);
    m_draft = repository ? repository->values() : Settings::OptionsRepository::defaults();
    m_baseline = m_draft;
    auto *layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("optionsSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索设置名称、说明或关键字…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    auto *body = new QHBoxLayout;
    layout->addLayout(body, 1);
    m_categories = new QTreeWidget(this);
    m_categories->setObjectName(QStringLiteral("optionsCategories"));
    m_categories->setHeaderHidden(true);
    m_categories->setRootIsDecorated(false);
    m_categories->setMaximumWidth(180);
    body->addWidget(m_categories);
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("optionsPages"));
    body->addWidget(m_stack, 1);
    for (const QString &category : categories()) {
        auto *item = new QTreeWidgetItem(m_categories, {categoryTitle(category)});
        item->setData(0, Qt::UserRole, category);
        QWidget *page = buildPage(category);
        m_pages.insert(category, page);
        m_stack->addWidget(page);
    }
    m_status = noteLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("optionsStatus"));
    layout->addWidget(m_status);
    auto *footer = new QHBoxLayout;
    layout->addLayout(footer);
    auto *reset = new QPushButton(QStringLiteral("重置当前分类…"), this);
    reset->setObjectName(QStringLiteral("optionsResetCategory"));
    footer->addWidget(reset);
    footer->addStretch();
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                    | QDialogButtonBox::Apply, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    m_buttons->button(QDialogButtonBox::Apply)->setText(QStringLiteral("应用"));
    m_buttons->button(QDialogButtonBox::Apply)->setObjectName(QStringLiteral("optionsApply"));
    footer->addWidget(m_buttons);
    connect(reset, &QPushButton::clicked, this, [this] { resetCategory(m_category); });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this] { applyChanges(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { search(text); });
    connect(m_categories, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item) {
        if (!item) return;
        if (!selectCategory(item->data(0, Qt::UserRole).toString()))
            setSelectedCategory(m_category);
    });
    if (repository) {
        connect(repository, &Settings::OptionsRepository::changed, this,
                [this](const QStringList &keys) {
            for (const QString &key : keys) {
                if (m_draft.value(key) == m_baseline.value(key))
                    m_draft.insert(key, m_repository->value(key));
                m_baseline.insert(key, m_repository->value(key));
            }
            refreshEditors();
        });
        connect(repository, &QObject::destroyed, this, [this] {
            setStatus(QStringLiteral("设置仓库已关闭，无法保存更改。"), true);
            refreshState();
        });
    }
    setSelectedCategory(QStringLiteral("general"));
    refreshEditors();
    if (!repository) setStatus(QStringLiteral("设置仓库不可用。"), true);
    else if (!repository->lastLoadError().isEmpty()) setStatus(repository->lastLoadError(), true);
}

QStringList OptionsDialog::categories() const
{
    return {QStringLiteral("general"), QStringLiteral("display"),
            QStringLiteral("fileops"), QStringLiteral("logging"),
            QStringLiteral("storage")};
}

QWidget *OptionsDialog::buildPage(const QString &category)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *layout = new QVBoxLayout(page);
    auto *heading = noteLabel(QStringLiteral("<h3>%1</h3>").arg(categoryTitle(category)), page);
    layout->addWidget(heading);
    for (const Settings::OptionDefinition &definition : Settings::OptionsRepository::definitions()) {
        if (definition.category != category) continue;
        auto *row = new QWidget(page);
        row->setObjectName(QStringLiteral("optionsRow_") + definition.key);
        row->setProperty("optionsSearchMatch", false);
        auto *form = new QVBoxLayout(row);
        form->setContentsMargins(4, 5, 4, 9);
        auto *title = new QLabel(definition.title, row);
        title->setObjectName(QStringLiteral("optionsTitle_") + definition.key);
        QFont font = title->font();
        font.setBold(true);
        title->setFont(font);
        form->addWidget(title);
        QWidget *editor = buildEditor(definition);
        title->setBuddy(editor);
        form->addWidget(editor);
        QString description = definition.description;
        if (definition.restartRequired && !description.contains(QStringLiteral("下次启动")))
            description += QStringLiteral(" 更改在下次启动时生效。");
        auto *descriptionLabel = noteLabel(description, row);
        form->addWidget(descriptionLabel);
        const QString tooltip = QStringLiteral("%1\n默认值：%2%3")
                .arg(description, displayValue(definition.key, definition.defaultValue),
                     definition.machineSpecific ? QStringLiteral("\n本机设置；导出时默认不包含。") : QString());
        row->setToolTip(tooltip);
        editor->setToolTip(tooltip);
        title->setToolTip(tooltip);
        m_editors.insert(definition.key, editor);
        m_titles.insert(definition.key, title);
        m_rows.insert(definition.key, row);
        layout->addWidget(row);
    }
    if (category == QStringLiteral("general")) {
        layout->addWidget(noteLabel(QStringLiteral("当前启动时打开 Home 页。记忆上次会话、指定工作区、系统自启动、文件关联和多语言切换尚未实现。"), page));
    } else if (category == QStringLiteral("display")) {
        layout->addWidget(noteLabel(QStringLiteral("应用后立即更新界面主题和字体；内容字体作用于已接入的比较视图。系统默认使用启动时的系统外观，尚未实现运行中跟随系统主题变化。差异颜色、语法颜色和图标风格的自定义尚未实现。"), page));
    } else if (category == QStringLiteral("fileops")) {
        m_fileOpsHint = noteLabel(QString(), page);
        m_fileOpsHint->setObjectName(QStringLiteral("optionsFileOpsSafety"));
        layout->addWidget(m_fileOpsHint);
        layout->addWidget(noteLabel(QStringLiteral("这些默认值只在调用方没有单独指定时生效。体积与条数确认是「达到就确认」；回收站可用性在执行那一刻单独探测，不在这里预设。尚未实现：复制/移动的默认冲突处理界面、操作后校验的执行路径、按卷记录不同的删除方式。"), page));
    } else if (category == QStringLiteral("logging")) {
        auto *open = new QPushButton(QStringLiteral("打开当前日志目录"), page);
        open->setObjectName(QStringLiteral("optionsOpenLogDirectory"));
        layout->addWidget(open, 0, Qt::AlignLeft);
        connect(open, &QPushButton::clicked, this, [this] {
            const QString path = Log::logFile();
            if (path.isEmpty()) { setStatus(QStringLiteral("当前未启用文件日志。")); return; }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
                setStatus(QStringLiteral("无法打开日志目录：%1").arg(QFileInfo(path).absolutePath()), true);
        });
        layout->addWidget(noteLabel(QStringLiteral("日志轮转、清空日志、诊断包与独立性能计时开关尚未实现。调试级别会输出已实现的调试日志。"), page));
    } else if (category == QStringLiteral("storage")) {
        const QString location = m_repository ? m_repository->location().filePath() : QStringLiteral("不可用");
        const QString mode = m_repository && m_repository->location().portable
                ? QStringLiteral("便携模式（程序目录 config/）") : QStringLiteral("标准模式（用户配置目录）");
        layout->addWidget(noteLabel(QStringLiteral("当前存储模式：%1\n设置文件：%2\n设置以可读 JSON 保存，标准与便携模式互不覆盖。模式由启动参数或 lqcompare.portable 标记决定，需重新启动。").arg(mode, location), page));
        auto *importButton = new QPushButton(QStringLiteral("导入设置或外观主题…"), page);
        importButton->setObjectName(QStringLiteral("optionsImport"));
        layout->addWidget(importButton, 0, Qt::AlignLeft);
        connect(importButton, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入设置"), {},
                                                            QStringLiteral("JSON 设置 (*.json);;全部文件 (*)"));
            if (!path.isEmpty()) importSettings(path);
        });
        auto *exportButton = new QPushButton(QStringLiteral("导出设置或外观主题…"), page);
        exportButton->setObjectName(QStringLiteral("optionsExport"));
        layout->addWidget(exportButton, 0, Qt::AlignLeft);
        connect(exportButton, &QPushButton::clicked, this, [this] {
            QDialog choice(this);
            choice.setWindowTitle(QStringLiteral("导出范围"));
            auto *choiceLayout = new QVBoxLayout(&choice);
            auto *scope = new QComboBox(&choice);
            scope->addItem(QStringLiteral("全部已实现的全局选项"));
            scope->addItem(QStringLiteral("仅显示与外观主题"));
            choiceLayout->addWidget(scope);
            auto *machine = new QCheckBox(QStringLiteral("包含本机日志路径（默认不包含）"), &choice);
            choiceLayout->addWidget(machine);
            choiceLayout->addWidget(noteLabel(QStringLiteral("导出不包含会话、最近路径、凭据或窗口位置。导出前将询问如何处理尚未应用的表单更改。"), &choice));
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &choice);
            choiceLayout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &choice, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &choice, &QDialog::reject);
            if (choice.exec() != QDialog::Accepted) return;
            const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出设置"),
                    scope->currentIndex() == 1 ? QStringLiteral("lqcompare-theme.json") : QStringLiteral("lqcompare-options.json"),
                    QStringLiteral("JSON 设置 (*.json)"));
            if (!path.isEmpty()) exportSettings(path, scope->currentIndex() == 1, machine->isChecked());
        });
        auto *reset = new QPushButton(QStringLiteral("重置全部全局选项…"), page);
        reset->setObjectName(QStringLiteral("optionsResetAll"));
        layout->addWidget(reset, 0, Qt::AlignLeft);
        connect(reset, &QPushButton::clicked, this, [this] { resetAll(); });
        layout->addWidget(noteLabel(QStringLiteral("导入和重置在确认后立即保存，仓库会先备份当前设置；操作成功后显示备份位置。取消关闭对话框不会撤销已经保存的操作。\n\n格式定义、快捷键、自定义命令、报表预设的迁移尚未接入。网络与远程连接、凭据管理尚未实现。"), page));
    }
    layout->addStretch();
    scroll->setWidget(page);
    return scroll;
}

QWidget *OptionsDialog::buildEditor(const Settings::OptionDefinition &definition)
{
    QWidget *editor = nullptr;
    const auto edited = [this, key = definition.key] {
        if (m_loading) return;
        const auto *item = Settings::OptionsRepository::definition(key);
        if (item) m_draft.insert(key, readEditor(*item));
        refreshState();
    };
    if (definition.defaultValue.type() == QVariant::Bool) {
        auto *check = new QCheckBox(QStringLiteral("启用"), this);
        connect(check, &QCheckBox::toggled, this, edited);
        editor = check;
    } else if (!definition.choices.isEmpty()) {
        auto *combo = new QComboBox(this);
        for (const QString &choice : definition.choices)
            combo->addItem(displayValue(definition.key, choice), choice);
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, edited);
        editor = combo;
    } else if (definition.key.endsWith(QStringLiteral("FontFamily"))) {
        auto *combo = new QComboBox(this);
        combo->addItem(displayValue(definition.key, QString()), QString());
        const QStringList families = QFontDatabase().families();
        for (const QString &family : families) combo->addItem(family, family);
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, edited);
        editor = combo;
    } else if (definition.defaultValue.type() == QVariant::Int) {
        auto *spin = new QSpinBox(this);
        spin->setRange(definition.minimum, definition.maximum);
        if (definition.key == QStringLiteral("display.uiFontSize")) {
            // 5 is a UI-only sentinel: arrows jump directly from system to 6 pt.
            spin->setMinimum(5);
            spin->setSpecialValueText(QStringLiteral("系统默认"));
        } else spin->setSuffix(definition.unit);
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, edited);
        editor = spin;
    } else {
        auto *edit = new QLineEdit(this);
        if (definition.key == QStringLiteral("logging.filePath"))
            edit->setPlaceholderText(QStringLiteral("留空使用配置目录下 logs/lqcompare.log；也可填写绝对路径"));
        connect(edit, &QLineEdit::textChanged, this, edited);
        editor = edit;
    }
    editor->setObjectName(QStringLiteral("optionsEditor_") + definition.key);
    return editor;
}

QWidget *OptionsDialog::editorFor(const QString &key) const { return m_editors.value(key); }
QVariant OptionsDialog::draftValue(const QString &key) const { return m_draft.value(key); }

QVariant OptionsDialog::readEditor(const Settings::OptionDefinition &definition) const
{
    QWidget *editor = editorFor(definition.key);
    if (auto *check = qobject_cast<QCheckBox *>(editor)) return check->isChecked();
    if (auto *combo = qobject_cast<QComboBox *>(editor)) return combo->currentData();
    if (auto *spin = qobject_cast<QSpinBox *>(editor))
        return definition.key == QStringLiteral("display.uiFontSize") && spin->value() == 5 ? 0 : spin->value();
    if (auto *edit = qobject_cast<QLineEdit *>(editor)) return edit->text();
    return {};
}

void OptionsDialog::writeEditor(const Settings::OptionDefinition &definition, const QVariant &value)
{
    QWidget *editor = editorFor(definition.key);
    if (auto *check = qobject_cast<QCheckBox *>(editor)) check->setChecked(value.toBool());
    else if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        int index = combo->findData(value);
        if (index < 0 && definition.key.endsWith(QStringLiteral("FontFamily"))) {
            combo->addItem(value.toString() + QStringLiteral("（当前机器未安装，将使用替代字体）"), value);
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
    } else if (auto *spin = qobject_cast<QSpinBox *>(editor))
        spin->setValue(definition.key == QStringLiteral("display.uiFontSize") && value.toInt() == 0 ? 5 : value.toInt());
    else if (auto *edit = qobject_cast<QLineEdit *>(editor)) edit->setText(value.toString());
}

bool OptionsDialog::setDraftValue(const QString &key, const QVariant &value)
{
    const auto *definition = Settings::OptionsRepository::definition(key);
    if (!definition || !m_editors.contains(key)) return false;
    m_draft.insert(key, value);
    m_loading = true;
    writeEditor(*definition, value);
    m_loading = false;
    refreshState();
    return true;
}

void OptionsDialog::refreshEditors()
{
    m_loading = true;
    for (const auto &definition : Settings::OptionsRepository::definitions())
        writeEditor(definition, m_draft.value(definition.key));
    m_loading = false;
    refreshState();
}

bool OptionsDialog::isDirty() const
{
    for (const auto &definition : Settings::OptionsRepository::definitions())
        if (m_draft.value(definition.key) != m_baseline.value(definition.key)) return true;
    return false;
}

void OptionsDialog::refreshState()
{
    if (!m_buttons) return;
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(m_repository && isDirty());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_repository);
    for (int i = 0; i < m_categories->topLevelItemCount(); ++i) {
        auto *item = m_categories->topLevelItem(i);
        const QString category = item->data(0, Qt::UserRole).toString();
        bool dirty = false;
        for (const auto &definition : Settings::OptionsRepository::definitions())
            if (definition.category == category && m_draft.value(definition.key) != m_baseline.value(definition.key))
                dirty = true;
        QFont font = item->font(0);
        font.setBold(dirty);
        item->setFont(0, font);
    }
    updateFileOpsHint();
}

void OptionsDialog::updateFileOpsHint()
{
    if (!m_fileOpsHint) return;
    const Files::FileOperationPolicy policy = Files::FileOperationPolicy::fromValues(m_draft);
    QStringList lines;
    // 只有永久删除才换文案：走回收站时写一句「删除可以撤销」，
    // 而不是把警告去掉留一行空白——用户要能看出这两者的区别在哪。
    lines.append(QStringLiteral("删除方式「%1」：%2")
                         .arg(Files::deleteModeLabel(policy.deleteMode),
                              policy.deleteWarning().isEmpty()
                                      ? QStringLiteral("删除的文件进入回收站，可以还原。")
                                      : policy.deleteWarning()));
    // 「文件较新」这一条必须现出：它是这批默认值里唯一会造成真正数据丢失的情况。
    lines.append(QStringLiteral("覆盖策略「%1」：%2")
                         .arg(Files::overwritePolicyLabel(policy.overwritePolicy),
                              policy.overwriteDecision(Files::OverwriteSituation::TargetNewer).notice));
    lines.append(QStringLiteral("确认阈值：大文件 %1，批量删除 %2 条；复制后校验：%3。")
                         .arg(policy.largeFileConfirmBytes > 0
                                      ? QStringLiteral("%1 MB").arg(policy.largeFileConfirmBytes / (1024 * 1024))
                                      : QStringLiteral("已关闭"),
                              policy.batchDeleteConfirmCount > 0
                                      ? QString::number(policy.batchDeleteConfirmCount)
                                      : QStringLiteral("已关闭"),
                              Files::verifyModeLabel(policy.verifyMode)));
    const QStringList problems = policy.validate();
    if (!problems.isEmpty())
        lines.append(QStringLiteral("注意：%1").arg(problems.join(QStringLiteral("；"))));
    m_fileOpsHint->setText(lines.join(QLatin1Char('\n')));
}

void OptionsDialog::setStatus(const QString &message, bool error)
{
    m_lastError = error ? message : QString();
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #c64040;") : QString());
    if (error) emit operationFailed(message);
}

bool OptionsDialog::finishOperation(const Settings::OperationResult &result, const QString &success)
{
    if (!result.ok) { setStatus(result.error, true); return false; }
    m_draft = m_repository->values();
    m_baseline = m_draft;
    refreshEditors();
    QString message = success;
    if (!result.backupPath.isEmpty()) message += QStringLiteral("\n备份：%1").arg(result.backupPath);
    setStatus(message);
    if (!result.changedKeys.isEmpty()) emit applied(result.changedKeys);
    return true;
}

bool OptionsDialog::applyChanges()
{
    if (!m_repository) { setStatus(QStringLiteral("设置仓库不可用。"), true); return false; }
    QVariantMap changes;
    for (const auto &definition : Settings::OptionsRepository::definitions()) {
        const QVariant value = m_draft.value(definition.key);
        if (value == m_baseline.value(definition.key)) continue;
        const QString error = Settings::OptionsRepository::validate(definition.key, value);
        if (!error.isEmpty()) {
            setSelectedCategory(definition.category);
            editorFor(definition.key)->setFocus();
            setStatus(error, true);
            return false;
        }
        changes.insert(definition.key, value);
    }
    const bool restart = changes.contains(QStringLiteral("general.singleInstance"));
    if (changes.isEmpty()) return true;
    return finishOperation(m_repository->apply(changes), restart
            ? QStringLiteral("设置已保存。单实例行为将在下次启动时生效。")
            : QStringLiteral("设置已保存并应用。"));
}

OptionsDialog::PendingChoice OptionsDialog::confirmPendingChanges(const QString &destination)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("尚未应用的更改"),
                    QStringLiteral("前往“%1”前，如何处理尚未应用的更改？").arg(destination),
                    QMessageBox::NoButton, this);
    auto *apply = box.addButton(QStringLiteral("应用更改"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("放弃更改"), QMessageBox::DestructiveRole);
    auto *cancel = box.addButton(QStringLiteral("返回"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == apply) return PendingChoice::Apply;
    if (box.clickedButton() == discard) return PendingChoice::Discard;
    return PendingChoice::Cancel;
}

bool OptionsDialog::resolvePending(const QString &destination)
{
    if (!isDirty()) return true;
    switch (confirmPendingChanges(destination)) {
    case PendingChoice::Apply: return applyChanges();
    case PendingChoice::Discard:
        m_draft = m_baseline;
        refreshEditors();
        return true;
    case PendingChoice::Cancel: return false;
    }
    return false;
}

void OptionsDialog::setSelectedCategory(const QString &category)
{
    if (!m_pages.contains(category)) return;
    m_category = category;
    m_stack->setCurrentWidget(m_pages.value(category));
    QSignalBlocker blocker(m_categories);
    for (int i = 0; i < m_categories->topLevelItemCount(); ++i) {
        auto *item = m_categories->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == category) m_categories->setCurrentItem(item);
    }
    if (auto *button = findChild<QPushButton *>(QStringLiteral("optionsResetCategory")))
        button->setEnabled(category != QStringLiteral("storage"));
}

bool OptionsDialog::selectCategory(const QString &category)
{
    if (!m_pages.contains(category)) return false;
    if (m_category == category) return true;
    if (!resolvePending(categoryTitle(category))) return false;
    setSelectedCategory(category);
    return true;
}

QStringList OptionsDialog::search(const QString &text)
{
    const QString query = text.trimmed();
    QStringList matches;
    for (const auto &definition : Settings::OptionsRepository::definitions()) {
        const QString searchable = definition.title + QLatin1Char(' ') + definition.description
                + QLatin1Char(' ') + definition.key + QLatin1Char(' ') + categoryTitle(definition.category);
        const bool match = !query.isEmpty() && searchable.contains(query, Qt::CaseInsensitive);
        auto *row = m_rows.value(definition.key);
        if (row) row->setProperty("optionsSearchMatch", match);
        auto *title = m_titles.value(definition.key);
        if (title) title->setStyleSheet(match ? QStringLiteral("background-color: #ffe08a; color: #24221e; padding: 3px;") : QString());
        if (match) matches << definition.key;
    }
    if (!matches.isEmpty()) {
        const auto *first = Settings::OptionsRepository::definition(matches.first());
        if (first) {
            if (!selectCategory(first->category)) return matches;
            if (auto *scroll = qobject_cast<QScrollArea *>(m_pages.value(first->category)))
                scroll->ensureWidgetVisible(m_rows.value(first->key));
        }
    }
    setStatus(query.isEmpty() ? QString() : matches.isEmpty()
              ? QStringLiteral("没有找到已实现的设置。未实现的功能无法在此配置。")
              : QStringLiteral("找到 %1 项；已高亮匹配设置。").arg(matches.size()));
    return matches;
}

bool OptionsDialog::confirmReset(const QString &category)
{
    const QString scope = category.isEmpty() ? QStringLiteral("全部全局选项") : categoryTitle(category);
    QMessageBox box(QMessageBox::Warning, QStringLiteral("确认恢复默认设置"),
                    QStringLiteral("将“%1”恢复出厂默认并立即保存。操作前会自动备份现有设置；备份失败则不重置。此操作不影响会话类型默认值。\n\n是否继续？").arg(scope),
                    QMessageBox::Reset | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Cancel);
    box.button(QMessageBox::Reset)->setText(QStringLiteral("备份并重置"));
    return box.exec() == QMessageBox::Reset;
}

bool OptionsDialog::resetCategory(const QString &category)
{
    if (!m_repository || !categories().contains(category) || category == QStringLiteral("storage")) return false;
    if (!confirmReset(category) || !resolvePending(QStringLiteral("重置设置"))) return false;
    return finishOperation(m_repository->reset(category), QStringLiteral("“%1”已恢复默认。 ").arg(categoryTitle(category)));
}

bool OptionsDialog::resetAll()
{
    if (!m_repository || !confirmReset(QString()) || !resolvePending(QStringLiteral("重置设置"))) return false;
    return finishOperation(m_repository->reset(), QStringLiteral("全部全局选项已恢复默认。"));
}

bool OptionsDialog::chooseImportEntries(const Settings::ImportPreview &preview, QStringList *selectedKeys)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("预览导入设置"));
    dialog.resize(780, 480);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(noteLabel(QStringLiteral("逐项选择要导入的设置。冲突项会覆盖当前值；未勾选的项保持原值。导入前自动备份，失败时不更改设置。"), &dialog));
    auto *table = new QTableWidget(preview.entries.size(), 4, &dialog);
    table->setObjectName(QStringLiteral("optionsImportPreview"));
    table->setHorizontalHeaderLabels({QStringLiteral("导入项"), QStringLiteral("当前值"), QStringLiteral("传入值"), QStringLiteral("状态")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    for (int row = 0; row < preview.entries.size(); ++row) {
        const auto &entry = preview.entries.at(row);
        const auto *definition = Settings::OptionsRepository::definition(entry.key);
        auto *item = new QTableWidgetItem(definition ? definition->title : entry.key);
        item->setToolTip(entry.key);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(entry.conflict ? Qt::Unchecked : Qt::Checked);
        table->setItem(row, 0, item);
        table->setItem(row, 1, new QTableWidgetItem(displayValue(entry.key, entry.currentValue)));
        table->setItem(row, 2, new QTableWidgetItem(displayValue(entry.key, entry.incomingValue)));
        table->setItem(row, 3, new QTableWidgetItem(entry.conflict ? QStringLiteral("冲突：选中后覆盖") : QStringLiteral("与当前值相同")));
    }
    layout->addWidget(table, 1);
    if (!preview.ignoredKeys.isEmpty())
        layout->addWidget(noteLabel(QStringLiteral("不支持的设置项已忽略：%1").arg(preview.ignoredKeys.join(QStringLiteral("、"))), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("备份并导入勾选项"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    selectedKeys->clear();
    for (int row = 0; row < preview.entries.size(); ++row)
        if (table->item(row, 0)->checkState() == Qt::Checked) selectedKeys->append(preview.entries.at(row).key);
    return true;
}

bool OptionsDialog::importSettings(const QString &path)
{
    if (!m_repository || !resolvePending(QStringLiteral("导入设置"))) return false;
    const auto preview = m_repository->previewImport(path);
    if (!preview.ok) { setStatus(preview.error, true); return false; }
    QStringList keys;
    if (!chooseImportEntries(preview, &keys)) return false;
    if (keys.isEmpty()) { setStatus(QStringLiteral("未选择任何导入项，设置保持不变。")); return true; }
    return finishOperation(m_repository->importFile(path, keys, preview.sourceFingerprint), QStringLiteral("所选设置已导入并应用。"));
}

bool OptionsDialog::exportSettings(const QString &path, bool appearanceOnly, bool includeMachineSpecific)
{
    if (!m_repository) { setStatus(QStringLiteral("设置仓库不可用。"), true); return false; }
    if (!resolvePending(QStringLiteral("导出设置"))) return false;
    const auto result = m_repository->exportFile(path, appearanceOnly ? QStringList{QStringLiteral("display")} : QStringList{}, includeMachineSpecific);
    if (!result.ok) { setStatus(result.error, true); return false; }
    setStatus(QStringLiteral("已导出到：%1%2").arg(path, isDirty() ? QStringLiteral("\n尚未应用的更改未包含在导出中。") : QString()));
    return true;
}

void OptionsDialog::accept()
{
    if (!applyChanges()) return;
    QDialog::accept();
}

void OptionsDialog::reject()
{
    if (!resolvePending(QStringLiteral("关闭选项"))) return;
    QDialog::reject();
}

} // namespace Options
} // namespace LqCompare
