#include "foldercompareview.h"
#include "maskfilter.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyle>
#include <QTreeView>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>
#include <vector>

namespace LqCompare {

class FolderTreeModel : public QAbstractItemModel
{
public:
    enum Column { LeftName, LeftSize, LeftModified, State, RightName, RightSize, RightModified, ColumnCount };
    enum Role { PathRole = Qt::UserRole + 1, StatusRole, SortRole, InComparisonRole, DirectoryRole };
    explicit FolderTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

    void setResult(const Folder::Result &result)
    {
        beginResetModel();
        m_result = result;
        m_root.children.clear();
        QHash<QString, Node *> paths;
        paths.insert(QString(), &m_root);
        for (int i = 0; i < result.entries.size(); ++i) {
            const auto &entry = result.entries.at(i);
            const int slash = entry.relativePath.lastIndexOf(QLatin1Char('/'));
            const QString parentPath = slash < 0 ? QString() : entry.relativePath.left(slash);
            Node *parentNode = paths.value(parentPath, &m_root);
            auto node = std::make_unique<Node>();
            node->entry = i;
            node->parent = parentNode;
            node->row = int(parentNode->children.size());
            paths.insert(entry.relativePath, node.get());
            parentNode->children.push_back(std::move(node));
        }
        endResetModel();
    }

    const Folder::Entry *entry(const QModelIndex &index) const
    {
        return index.isValid() ? &m_result.entries.at(static_cast<Node *>(index.internalPointer())->entry)
                               : nullptr;
    }
    int totalCount() const { return m_result.entries.size(); }
    int excludedCount() const { return m_result.excludedCount; }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        const Node *parentNode = parent.isValid() ? static_cast<Node *>(parent.internalPointer()) : &m_root;
        return createIndex(row, column, parentNode->children[size_t(row)].get());
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return {};
        const Node *parentNode = static_cast<Node *>(index.internalPointer())->parent;
        return parentNode == &m_root ? QModelIndex()
                                    : createIndex(parentNode->row, 0, const_cast<Node *>(parentNode));
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const Node *node = parent.isValid() ? static_cast<Node *>(parent.internalPointer()) : &m_root;
        return int(node->children.size());
    }

    int columnCount(const QModelIndex & = {}) const override { return ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case LeftName: return tr("左侧名称");
        case RightName: return tr("右侧名称");
        case LeftSize: case RightSize: return tr("大小（字节）");
        case LeftModified: case RightModified: return tr("修改时间");
        case State: return tr("状态");
        }
        return {};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const auto *item = entry(index);
        if (!item)
            return {};
        const bool right = index.column() >= RightName;
        const auto &side = right ? item->right : item->left;
        const bool nameColumn = index.column() == LeftName || index.column() == RightName;
        const bool sizeColumn = index.column() == LeftSize || index.column() == RightSize;
        if (role == PathRole)
            return item->relativePath;
        if (role == StatusRole)
            return int(item->status);
        if (role == InComparisonRole)
            return item->inComparison();
        if (role == DirectoryRole)
            return item->isDirectory();
        if (role == Qt::ToolTipRole) {
            return QStringLiteral("%1\n%2\n%3\n%4")
                .arg(item->relativePath, item->left.info.path, item->right.info.path,
                     item->explanation + (item->excludedByMask ? QStringLiteral("\n") + item->filterReason : QString())).trimmed();
        }
        if (role == Qt::TextAlignmentRole && sizeColumn)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role == Qt::ForegroundRole) {
            const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
            if (!item->inComparison())
                return QBrush(QColor(dark ? "#b0b0b0" : "#666666"));
            switch (item->status) {
            case Folder::Status::Different: return QBrush(QColor(dark ? "#ffbc66" : "#895000"));
            case Folder::Status::LeftOnly: return QBrush(QColor(dark ? "#89beff" : "#2055a0"));
            case Folder::Status::RightOnly: return QBrush(QColor(dark ? "#99dcb6" : "#236c44"));
            case Folder::Status::Error: case Folder::Status::TypeConflict:
                return QBrush(QColor(dark ? "#ff9696" : "#aa2424"));
            default: return {};
            }
        }
        if (role == Qt::DecorationRole && nameColumn && side.exists()) {
            return QApplication::style()->standardIcon(side.kind == Folder::Kind::Directory
                ? QStyle::SP_DirIcon : side.kind == Folder::Kind::SymbolicLink
                ? QStyle::SP_FileLinkIcon : QStyle::SP_FileIcon);
        }
        if (role == SortRole) {
            if (nameColumn)
                return item->relativePath;
            if (sizeColumn)
                return side.exists() ? QVariant::fromValue<qulonglong>(side.info.size) : QVariant();
            if (index.column() == State)
                return int(item->status);
            return side.info.lastModified.isValid()
                ? QVariant(side.info.lastModified.millisecondsSinceEpoch()) : QVariant();
        }
        if (role != Qt::DisplayRole)
            return {};
        if (index.column() == State) {
            if (!item->inComparison())
                return tr("已排除（未比较）");
            return Folder::statusLabel(item->status)
                + (item->nameCaseDifference ? tr(" · 名称大小写不同") : QString());
        }
        if (nameColumn)
            return side.exists() ? side.info.name : QStringLiteral("—");
        if (!side.exists())
            return {};
        if (sizeColumn)
            return side.kind == Folder::Kind::File ? QLocale().toString(side.info.size) : QString();
        return side.info.lastModified.isValid()
            ? side.info.lastModified.toLocalDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : QString();
    }

private:
    struct Node {
        int entry = -1;
        int row = 0;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };
    Node m_root;
    Folder::Result m_result;
};

class FolderFilterModel : public QSortFilterProxyModel
{
public:
    explicit FolderFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
    {
        setRecursiveFilteringEnabled(true);
        setSortRole(FolderTreeModel::SortRole);
    }
    void setStatusFilter(int filter) { m_filter = filter; invalidateFilter(); }
    void setHideExcluded(bool hide) { m_hideExcluded = hide; invalidateFilter(); }
    void setHideEmpty(bool hide) { m_hideEmpty = hide; invalidateFilter(); }
    bool matchesOwnFilters(const QModelIndex &proxyIndex) const
    {
        const auto source = mapToSource(proxyIndex);
        return source.isValid() && filterAcceptsRow(source.row(), source.parent());
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex index = sourceModel()->index(row, 0, parent);
        const int status = index.data(FolderTreeModel::StatusRole).toInt();
        if (m_hideExcluded && !index.data(FolderTreeModel::InComparisonRole).toBool())
            return false;
        if (m_hideEmpty && index.data(FolderTreeModel::DirectoryRole).toBool()
            && sourceModel()->rowCount(index) == 0 && status != int(Folder::Status::Error)
            && status != int(Folder::Status::Unknown) && status != int(Folder::Status::TypeConflict))
            return false;
        if (m_filter == -2)
            return status != int(Folder::Status::Same);
        return m_filter == -1 || status == m_filter;
    }

private:
    int m_filter = -1;
    bool m_hideExcluded = true;
    bool m_hideEmpty = false;
};

namespace {

void visitIndexes(QAbstractItemModel *model, const QModelIndex &parent,
                  const std::function<void(const QModelIndex &)> &visit)
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        visit(index);
        visitIndexes(model, index, visit);
    }
}

} // namespace

FolderCompareView::FolderCompareView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("folderCompareView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *paths = new QHBoxLayout;
    m_leftPath = new QLineEdit(this);
    m_rightPath = new QLineEdit(this);
    m_leftPath->setObjectName(QStringLiteral("folderLeftPath"));
    m_rightPath->setObjectName(QStringLiteral("folderRightPath"));
    m_leftPath->setPlaceholderText(tr("左侧文件夹"));
    m_rightPath->setPlaceholderText(tr("右侧文件夹"));
    for (QLineEdit *edit : {m_leftPath, m_rightPath}) {
        paths->addWidget(edit, 1);
        auto *browse = new QPushButton(tr("浏览…"), this);
        paths->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString path = QFileDialog::getExistingDirectory(this, tr("选择比较文件夹"), edit->text());
            if (!path.isEmpty())
                edit->setText(path);
        });
        connect(edit, &QLineEdit::returnPressed, this, [this] {
            emit compareRequested(m_leftPath->text(), m_rightPath->text());
        });
    }
    layout->addLayout(paths);

    auto *toolbar = new QHBoxLayout;
    auto *refresh = new QPushButton(tr("比较 / 刷新"), this);
    refresh->setObjectName(QStringLiteral("folderRefresh"));
    // Keyboard shortcuts are owned by MainWindow's active-session routing.
    toolbar->addWidget(refresh);
    m_cancel = new QPushButton(tr("取消扫描"), this);
    m_cancel->setObjectName(QStringLiteral("folderCancel"));
    m_cancel->setEnabled(false);
    toolbar->addWidget(m_cancel);
    m_recursive = new QCheckBox(tr("递归子目录"), this);
    m_recursive->setChecked(true);
    toolbar->addWidget(m_recursive);
    m_content = new QCheckBox(tr("逐字节比较内容"), this);
    m_content->setChecked(true);
    m_content->setToolTip(tr("关闭后仅比较大小；大小相同会显示未知。符号链接只比较链接本身。"));
    toolbar->addWidget(m_content);
    auto *previous = new QPushButton(tr("上一差异"), this);
    auto *next = new QPushButton(tr("下一差异"), this);
    auto *selectDifferences = new QPushButton(tr("选择差异"), this);
    previous->setObjectName(QStringLiteral("folderPreviousDifference"));
    next->setObjectName(QStringLiteral("folderNextDifference"));
    selectDifferences->setObjectName(QStringLiteral("folderSelectDifferences"));
    toolbar->addWidget(previous);
    toolbar->addWidget(next);
    toolbar->addWidget(selectDifferences);
    toolbar->addStretch();
    auto *optionsToggle = new QToolButton(this);
    optionsToggle->setText(tr("文件夹选项"));
    optionsToggle->setCheckable(true);
    optionsToggle->setObjectName(QStringLiteral("folderOptionsToggle"));
    toolbar->addWidget(optionsToggle);
    layout->addLayout(toolbar);

    auto *optionsPanel = new QWidget(this);
    auto *optionsLayout = new QHBoxLayout(optionsPanel);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    auto *optionsDescription = new QLabel(tr("扫描掩码\n每行一条，- 开头排除"), optionsPanel);
    optionsLayout->addWidget(optionsDescription);
    m_scanMask = new QPlainTextEdit(optionsPanel);
    m_scanMask->setObjectName(QStringLiteral("folderScanMask"));
    m_scanMask->setPlaceholderText(tr("*.cpp\n*.h\n-build/**"));
    m_scanMask->setMaximumHeight(76);
    m_scanMask->setToolTip(tr("留空表示全部。掩码在点击比较后改变比较范围，排除项不读取内容；目录继续遍历，避免漏掉符合包含规则的后代。"));
    optionsLayout->addWidget(m_scanMask, 1);
    auto *optionsNotes = new QVBoxLayout;
    m_caseSensitive = new QCheckBox(tr("名称及掩码区分大小写"), optionsPanel);
    m_caseSensitive->setObjectName(QStringLiteral("folderCaseSensitive"));
    m_caseSensitive->setChecked(true);
    m_caseSensitive->setToolTip(tr("这是本次比较的名称配对规则；不更改文件系统。忽略大小写产生重名歧义时将显示错误。"));
    optionsNotes->addWidget(m_caseSensitive);
    auto *scopeNote = new QLabel(tr("扫描掩码改变比较范围；下方显示筛选仅改变显示。"), optionsPanel);
    scopeNote->setWordWrap(true);
    optionsNotes->addWidget(scopeNote);
    m_maskError = new QLabel(optionsPanel);
    m_maskError->setTextFormat(Qt::PlainText);
    m_maskError->setWordWrap(true);
    optionsNotes->addWidget(m_maskError);
    optionsLayout->addLayout(optionsNotes, 1);
    optionsPanel->setVisible(false);
    layout->addWidget(optionsPanel);
    connect(optionsToggle, &QToolButton::toggled, optionsPanel, &QWidget::setVisible);
    connect(m_scanMask, &QPlainTextEdit::textChanged, this, [this] {
        const auto parsed = Filter::MaskFilter::parse(m_scanMask->toPlainText());
        m_maskError->setText(parsed.ok() ? QString() : parsed.describeErrors());
    });

    auto *displayToolbar = new QHBoxLayout;
    displayToolbar->addWidget(new QLabel(tr("显示筛选："), this));
    m_filter = new QComboBox(this);
    m_filter->setObjectName(QStringLiteral("folderStatusFilter"));
    m_filter->addItem(tr("全部条目"), -1);
    m_filter->addItem(tr("差异与未确认"), -2);
    for (Folder::Status status : {Folder::Status::Same, Folder::Status::Different,
             Folder::Status::LeftOnly, Folder::Status::RightOnly, Folder::Status::TypeConflict,
             Folder::Status::Error, Folder::Status::Unknown})
        m_filter->addItem(Folder::statusLabel(status), int(status));
    displayToolbar->addWidget(m_filter);
    m_hideExcluded = new QCheckBox(tr("隐藏扫描排除项"), this);
    m_hideExcluded->setObjectName(QStringLiteral("folderHideExcluded"));
    m_hideExcluded->setChecked(true);
    m_hideEmpty = new QCheckBox(tr("隐藏空文件夹"), this);
    m_hideEmpty->setObjectName(QStringLiteral("folderHideEmpty"));
    displayToolbar->addWidget(m_hideExcluded);
    displayToolbar->addWidget(m_hideEmpty);
    auto *resetDisplay = new QPushButton(tr("重置显示"), this);
    resetDisplay->setObjectName(QStringLiteral("folderResetDisplay"));
    displayToolbar->addWidget(resetDisplay);
    displayToolbar->addStretch();
    auto *expand = new QPushButton(tr("展开全部"), this);
    auto *collapse = new QPushButton(tr("折叠全部"), this);
    displayToolbar->addWidget(expand);
    displayToolbar->addWidget(collapse);
    layout->addLayout(displayToolbar);

    m_model = new FolderTreeModel(this);
    m_filterModel = new FolderFilterModel(this);
    m_filterModel->setSourceModel(m_model);
    auto *splitter = new QSplitter(this);
    m_leftTree = new QTreeView(splitter);
    m_rightTree = new QTreeView(splitter);
    m_leftTree->setObjectName(QStringLiteral("folderLeftTree"));
    m_rightTree->setObjectName(QStringLiteral("folderRightTree"));
    for (QTreeView *tree : {m_leftTree, m_rightTree}) {
        tree->setModel(m_filterModel);
        tree->setAlternatingRowColors(true);
        tree->setUniformRowHeights(true);
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        tree->setSortingEnabled(true);
        tree->installEventFilter(this);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(QHeaderView::Interactive);
        tree->header()->setSectionResizeMode(FolderTreeModel::State, QHeaderView::Stretch);
        // QTreeView emits activated for both double-click and Return. Connecting
        // doubleClicked as well would open the same pair twice on some platforms.
        connect(tree, &QTreeView::activated, this, &FolderCompareView::activate);
        connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this] { updateCount(); });
    }
    for (int column : {FolderTreeModel::RightName, FolderTreeModel::RightSize, FolderTreeModel::RightModified})
        m_leftTree->hideColumn(column);
    for (int column : {FolderTreeModel::LeftName, FolderTreeModel::LeftSize, FolderTreeModel::LeftModified})
        m_rightTree->hideColumn(column);
    m_rightTree->setTreePosition(FolderTreeModel::RightName);
    m_rightTree->header()->moveSection(m_rightTree->header()->visualIndex(FolderTreeModel::State), 6);
    m_leftTree->setColumnWidth(FolderTreeModel::LeftName, 210);
    m_rightTree->setColumnWidth(FolderTreeModel::RightName, 210);
    m_leftTree->setColumnWidth(FolderTreeModel::LeftSize, 100);
    m_rightTree->setColumnWidth(FolderTreeModel::RightSize, 100);
    m_leftTree->setColumnWidth(FolderTreeModel::LeftModified, 195);
    m_rightTree->setColumnWidth(FolderTreeModel::RightModified, 195);
    m_leftTree->sortByColumn(FolderTreeModel::LeftName, Qt::AscendingOrder);
    connect(m_leftTree, &QTreeView::expanded, m_rightTree, &QTreeView::expand);
    connect(m_rightTree, &QTreeView::expanded, m_leftTree, &QTreeView::expand);
    connect(m_leftTree, &QTreeView::collapsed, m_rightTree, &QTreeView::collapse);
    connect(m_rightTree, &QTreeView::collapsed, m_leftTree, &QTreeView::collapse);
    connect(m_leftTree->verticalScrollBar(), &QScrollBar::valueChanged,
            m_rightTree->verticalScrollBar(), &QScrollBar::setValue);
    connect(m_rightTree->verticalScrollBar(), &QScrollBar::valueChanged,
            m_leftTree->verticalScrollBar(), &QScrollBar::setValue);
    layout->addWidget(splitter, 1);

    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(tr("选择两个文件夹，开始比较。"), this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_count = new QLabel(this);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_count);
    layout->addLayout(bottom);

    connect(refresh, &QPushButton::clicked, this, [this] {
        emit compareRequested(m_leftPath->text(), m_rightPath->text());
    });
    connect(m_cancel, &QPushButton::clicked, this, &FolderCompareView::cancelRequested);
    connect(previous, &QPushButton::clicked, this, &FolderCompareView::previousDifference);
    connect(next, &QPushButton::clicked, this, &FolderCompareView::nextDifference);
    connect(selectDifferences, &QPushButton::clicked, this, &FolderCompareView::selectAllDifferences);
    connect(resetDisplay, &QPushButton::clicked, this, &FolderCompareView::resetDisplayFilters);
    connect(m_filter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_filterModel->setStatusFilter(m_filter->currentData().toInt());
        updateCount();
    });
    connect(m_hideExcluded, &QCheckBox::toggled, this, [this](bool hide) {
        m_filterModel->setHideExcluded(hide);
        updateCount();
    });
    connect(m_hideEmpty, &QCheckBox::toggled, this, [this](bool hide) {
        m_filterModel->setHideEmpty(hide);
        updateCount();
    });
    connect(expand, &QPushButton::clicked, m_leftTree, &QTreeView::expandAll);
    connect(expand, &QPushButton::clicked, m_rightTree, &QTreeView::expandAll);
    connect(collapse, &QPushButton::clicked, m_leftTree, &QTreeView::collapseAll);
    connect(collapse, &QPushButton::clicked, m_rightTree, &QTreeView::collapseAll);
}

void FolderCompareView::setPaths(const QString &leftPath, const QString &rightPath)
{
    m_leftPath->setText(leftPath);
    m_rightPath->setText(rightPath);
    m_leftPath->setToolTip(leftPath);
    m_rightPath->setToolTip(rightPath);
}

void FolderCompareView::setResult(const Folder::Result &result)
{
    QSet<QString> expanded, leftSelected, rightSelected;
    const QString leftCurrent = m_leftTree->currentIndex().data(FolderTreeModel::PathRole).toString();
    const QString rightCurrent = m_rightTree->currentIndex().data(FolderTreeModel::PathRole).toString();
    visitIndexes(m_filterModel, {}, [this, &expanded, &leftSelected, &rightSelected](const QModelIndex &index) {
        const QString path = index.data(FolderTreeModel::PathRole).toString();
        if (m_leftTree->isExpanded(index))
            expanded.insert(path);
        if (m_leftTree->selectionModel()->isRowSelected(index.row(), index.parent()))
            leftSelected.insert(path);
        if (m_rightTree->selectionModel()->isRowSelected(index.row(), index.parent()))
            rightSelected.insert(path);
    });
    const int scroll = m_leftTree->verticalScrollBar()->value();
    m_model->setResult(result);
    visitIndexes(m_filterModel, {}, [this, &expanded, &leftSelected, &rightSelected,
                                    &leftCurrent, &rightCurrent](const QModelIndex &index) {
        const QString path = index.data(FolderTreeModel::PathRole).toString();
        if (expanded.contains(path)) {
            m_leftTree->expand(index);
            m_rightTree->expand(index);
        }
        if (leftSelected.contains(path))
            m_leftTree->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (rightSelected.contains(path))
            m_rightTree->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!leftCurrent.isEmpty() && path == leftCurrent)
            m_leftTree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        if (!rightCurrent.isEmpty() && path == rightCurrent)
            m_rightTree->selectionModel()->setCurrentIndex(index.siblingAtColumn(FolderTreeModel::RightName),
                                                         QItemSelectionModel::NoUpdate);
    });
    if (!m_hasResult && result.entries.size() < 10000) {
        m_leftTree->expandToDepth(0);
        m_rightTree->expandToDepth(0);
    }
    m_hasResult = true;
    m_leftTree->verticalScrollBar()->setValue(scroll);
    updateCount();
}

void FolderCompareView::setScanning(bool scanning)
{
    m_cancel->setEnabled(scanning);
}

void FolderCompareView::setStatus(const QString &status)
{
    m_status->setText(status);
}

Folder::Options FolderCompareView::options() const
{
    Folder::Options options;
    options.recursive = m_recursive->isChecked();
    options.compareContent = m_content->isChecked();
    options.maximumDepth = m_maximumDepth;
    options.scanMaskDeclaration = m_scanMask->toPlainText();
    options.nameCaseSensitivity = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    return options;
}

void FolderCompareView::setOptions(const Folder::Options &options)
{
    m_recursive->setChecked(options.recursive);
    m_content->setChecked(options.compareContent);
    m_maximumDepth = options.maximumDepth;
    m_scanMask->setPlainText(options.scanMaskDeclaration);
    m_caseSensitive->setChecked(options.nameCaseSensitivity == Qt::CaseSensitive);
}

void FolderCompareView::activate(const QModelIndex &index)
{
    const auto *entry = m_model->entry(m_filterModel->mapToSource(index));
    if (entry && entry->canCompareAsText())
        emit compareFilesRequested(entry->left.info.path, entry->right.info.path);
}

void FolderCompareView::updateCount()
{
    if (!m_count)
        return;
    int visible = 0;
    visitIndexes(m_filterModel, {}, [&visible](const QModelIndex &) { ++visible; });
    m_count->setText(tr("显示 %1 / 共 %2 · 扫描排除 %3 · 左选 %4 / 右选 %5")
                        .arg(visible).arg(m_model->totalCount()).arg(m_model->excludedCount())
                        .arg(m_leftTree->selectionModel()->selectedRows().size())
                        .arg(m_rightTree->selectionModel()->selectedRows().size()));
}

bool FolderCompareView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        if (watched == m_leftTree) m_rightActive = false;
        if (watched == m_rightTree) m_rightActive = true;
    }
    return QWidget::eventFilter(watched, event);
}

QTreeView *FolderCompareView::activeTree() const
{
    return m_rightActive ? m_rightTree : m_leftTree;
}

QVector<QModelIndex> FolderCompareView::differenceIndexes() const
{
    QVector<QModelIndex> differences;
    visitIndexes(m_filterModel, {}, [this, &differences](const QModelIndex &index) {
        const auto *entry = m_model->entry(m_filterModel->mapToSource(index));
        if (!entry || !entry->inComparison() || entry->status == Folder::Status::Same
            || !m_filterModel->matchesOwnFilters(index))
            return;
        // A directory's aggregate difference would repeat its children's stops.
        // Empty or unscanned directories, and explicit errors/conflicts, still stop.
        if (entry->isDirectory() && m_filterModel->rowCount(index) > 0
            && entry->status != Folder::Status::Error && entry->status != Folder::Status::TypeConflict)
            return;
        differences.append(index);
    });
    return differences;
}

int FolderCompareView::visibleDifferenceCount() const
{
    return differenceIndexes().size();
}

void FolderCompareView::reveal(const QModelIndex &index)
{
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
        m_leftTree->expand(parent);
        m_rightTree->expand(parent);
    }
    QTreeView *tree = activeTree();
    const QModelIndex activeIndex = index.siblingAtColumn(m_rightActive ? FolderTreeModel::RightName
                                                                      : FolderTreeModel::LeftName);
    tree->selectionModel()->setCurrentIndex(activeIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    tree->scrollTo(activeIndex, QAbstractItemView::PositionAtCenter);
    tree->setFocus();
}

void FolderCompareView::navigateDifference(int direction, bool fromEdge)
{
    const auto differences = differenceIndexes();
    if (differences.isEmpty()) {
        emit navigationStatus(tr("当前显示范围没有差异；可重置显示筛选查看其他条目。"));
        return;
    }
    const QModelIndex current = activeTree()->currentIndex().siblingAtColumn(0);
    QHash<QModelIndex, int> positions;
    int position = 0;
    visitIndexes(m_filterModel, {}, [&positions, &position](const QModelIndex &index) {
        positions.insert(index, position++);
    });
    const int currentPosition = positions.value(current, -1);
    int selected = -1;
    if (fromEdge || currentPosition < 0) {
        selected = direction > 0 ? 0 : differences.size() - 1;
    } else if (direction > 0) {
        for (int i = 0; i < differences.size(); ++i) {
            if (positions.value(differences.at(i)) > currentPosition) { selected = i; break; }
        }
    } else {
        for (int i = differences.size() - 1; i >= 0; --i) {
            if (positions.value(differences.at(i)) < currentPosition) { selected = i; break; }
        }
    }
    if (selected < 0) {
        emit navigationStatus(direction > 0 ? tr("已经是最后一处可见差异。") : tr("已经是第一处可见差异。"));
        return;
    }
    reveal(differences.at(selected));
    emit navigationStatus(tr("可见差异 %1 / %2 · %3").arg(selected + 1).arg(differences.size())
                              .arg(differences.at(selected).data(FolderTreeModel::PathRole).toString()));
}

void FolderCompareView::nextDifference() { navigateDifference(1); }
void FolderCompareView::previousDifference() { navigateDifference(-1); }
void FolderCompareView::firstDifference() { navigateDifference(1, true); }
void FolderCompareView::lastDifference() { navigateDifference(-1, true); }

void FolderCompareView::selectAllDifferences()
{
    const auto differences = differenceIndexes();
    QItemSelection selection;
    for (const QModelIndex &index : differences)
        selection.select(index, index.siblingAtColumn(FolderTreeModel::ColumnCount - 1));
    activeTree()->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    emit navigationStatus(tr("已在%1侧选中 %2 处可见差异；隐藏条目未被选中。")
                              .arg(m_rightActive ? tr("右") : tr("左")).arg(differences.size()));
}

void FolderCompareView::resetDisplayFilters()
{
    m_filter->setCurrentIndex(0);
    m_hideExcluded->setChecked(false);
    m_hideEmpty->setChecked(false);
    emit navigationStatus(tr("显示筛选已重置；扫描掩码的比较范围保持不变。"));
}

} // namespace LqCompare
