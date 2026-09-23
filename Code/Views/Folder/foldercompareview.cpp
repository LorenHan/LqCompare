#include "foldercompareview.h"
#include "entrystatus.h"
#include "maskfilter.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTreeView>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>
#include <vector>

namespace LqCompare {

namespace {

// 当前 Qt 调色板是不是深色主题。三处共用（模型的前景色、状态图标着色、
// 以及将来的其它着色点）：各自写一遍 `lightness() < 128` 的话，
// 有一处把方向写反只会表现为「某个地方的颜色在深色主题下反了」。
bool usesDarkPalette()
{
    return QApplication::palette().color(QPalette::Base).lightness() < 128;
}

} // namespace

QIcon themedStatusIcon(Folder::Status status, const Folder::ColorScheme &scheme, bool dark)
{
    const QString key = Folder::statusIconKey(status);
    if (key.isEmpty())
        return {};
    const QIcon base(key);
    // 资源缺失时如实返回原图标（哪怕是空的），**不要**在这里造一个回退图形：
    // 「图标文件没登记进 qrc」这件事由 `tools/check_icons.py` 那道护栏负责报，
    // 在这里补一个假图标只会让护栏失去意义。
    if (base.isNull())
        return base;
    const QColor tint(scheme.colorFor(status, dark));
    if (!tint.isValid())
        return base;

    QPixmap source = base.pixmap(QSize(16, 16));
    if (source.isNull())
        return base;
    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);
    {
        QPainter painter(&tinted);
        painter.drawPixmap(0, 0, source);
        // SourceIn：只把**已有内容**的像素重新染色，alpha 原样保留。
        // 于是描边的抗锯齿边缘不会被抹平（用 fillRect 直接盖一层会得到一块实心方块）。
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(tinted.rect(), tint);
    }
    return QIcon(tinted);
}

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
    // 配色方案（DIR-012）。默认值取自服务层的出厂默认，不在这里写死一个
    // 标识符字面量——那会让「出厂默认是哪一套」长出第二份说法。
    const Folder::ColorScheme &colorScheme() const { return m_scheme; }
    void setColorScheme(const Folder::ColorScheme &scheme)
    {
        m_scheme = scheme;
        refreshHighlight();
    }
    int totalCount() const { return m_result.entries.size(); }
    int excludedCount() const { return m_result.excludedCount; }
    // 本次比较有没有用上有效基线（DIR-011 第 3 条）。「为什么是这个状态」
    // 要如实回答这一条，不能靠猜。
    bool baselineApplied() const { return m_result.baselineApplied; }

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
            const bool dark = usesDarkPalette();
            // 颜色一律查配色表（DIR-012 第 2 条）。这里原本是一个 switch，
            // 它是状态的**第二份清单**：新增一档状态时没人会记得回来补一行，
            // 界面于是静默少一种颜色；而「高对比 / 色盲友好」要的是把九档
            // 一起换掉，散落的 switch 只能复制九行再改九处。
            if (!item->inComparison())
                return QBrush(QColor(m_scheme.excludedColor(dark)));
            const QString color = m_scheme.colorFor(item->status, dark);
            // 表里查不到这一档时**不猜**：返回空让 Qt 用默认前景色。
            // 猜一个（比如回落到「未知」的颜色）会让「配色表缺了一档」
            // 这件事在界面上完全看不出来，而它正是配色导入最可能的坏法。
            return color.isEmpty() ? QVariant() : QVariant(QBrush(QColor(color)));
        }
        // 状态图标（DIR-011 第 4 条）：颜色之外必须同时有图标和文本。
        // 图标与颜色都只是「辅助」，真正不可省略的是状态列的文字。
        if (role == Qt::DecorationRole && index.column() == State) {
            // 图标跟着配色与主题一起变（DIR-012 第 1 条后半句）。九张资源图是
            // 中性灰描边，深色主题上原样用几乎看不见；用该状态在本方案里的
            // 颜色着色之后，深浅两档都保持可见，而「不同形状」这一点不变。
            const QIcon icon = themedStatusIcon(item->status, m_scheme, usesDarkPalette());
            return icon.isNull() ? QVariant() : QVariant(icon);
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

    // 换配色后逐行重画（第 4 条）。
    //
    // **刻意不用 `beginResetModel()` / `endResetModel()`**：重置模型会把展开状态、
    // 选中项与滚动位置一起丢掉，而「换一个配色」不该让用户重新展开一遍树、
    // 重新挑一遍要看的行。逐行发 `dataChanged` 只刷新颜色与图标这两件事，
    // 其余视图状态原样保留——也因此它**不需要重新扫描**（第 4 条的后半句）。
    void refreshHighlight()
    {
        const int lastColumn = columnCount() - 1;
        emitRowsChanged(&m_root, lastColumn);
    }
    void emitRowsChanged(const Node *node, int lastColumn)
    {
        for (const auto &child : node->children) {
            // 行号取 `child->row`（它在自己父节点里的位置），与 `index()` 的
            // 构造方式同一个来源。另算一次行号就会在筛选/排序之后对不上，
            // 而症状是「换了配色之后有几行没变」。
            const QModelIndex topLeft = createIndex(child->row, 0, child.get());
            const QModelIndex bottomRight = createIndex(child->row, lastColumn, child.get());
            emit dataChanged(topLeft, bottomRight,
                             {Qt::ForegroundRole, Qt::DecorationRole, Qt::ToolTipRole});
            emitRowsChanged(child.get(), lastColumn);
        }
    }

    Folder::ColorScheme m_scheme =
        Folder::colorSchemeByIdentifier(Folder::defaultColorSchemeIdentifier());
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
    auto *recursionLabel = new QLabel(tr("子目录："), this);
    toolbar->addWidget(recursionLabel);
    m_recursionTier = new QComboBox(this);
    m_recursionTier->setObjectName(QStringLiteral("folderRecursionTier"));
    // 铺法直接来自档位表的顺序，不在界面里另写一份清单：
    // 另写一份就不会随表增长（新增档位时下拉会静默落后一格）。
    for (const auto &row : Folder::recursionTierTable()) {
        m_recursionTier->addItem(Folder::recursionTierLabel(row.tier), int(row.tier));
        m_recursionTier->setItemData(m_recursionTier->count() - 1,
                                     Folder::recursionTierDescription(row.tier), Qt::ToolTipRole);
    }
    toolbar->addWidget(m_recursionTier);
    m_maximumDepth = new QSpinBox(this);
    m_maximumDepth->setObjectName(QStringLiteral("folderMaximumDepth"));
    m_maximumDepth->setRange(0, Folder::kMaximumRecursionDepth);
    toolbar->addWidget(m_maximumDepth);
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
    // 铺法直接来自主状态表的顺序，不在界面里另写一份清单：
    // 另写一份就不会随表增长（新增「两侧均改 / 冲突」时界面会静默落后一格）。
    for (const auto &row : Folder::mainStatusTable())
        m_filter->addItem(Folder::statusLabel(row.value), int(row.value));
    displayToolbar->addWidget(m_filter);
    // 配色方案（DIR-012 第 2 条）。它铺在**显示**这一条工具条上而不是
    // 「文件夹选项」那一条：配色改的是画出来的样子，不进 `Options`、
    // 不改变结果集，放在「子目录 / 逐字节比较内容」旁边会让人以为它影响比对。
    // 住所在 View 页 Coloring 组是规格的说法，而那张设置页（OPT-*）尚未落地，
    // 因此入口先落在视图自己的工具条上——与 DIR-003 的档位下拉同一处置。
    displayToolbar->addWidget(new QLabel(tr("配色："), this));
    m_colorScheme = new QComboBox(this);
    m_colorScheme->setObjectName(QStringLiteral("folderColorScheme"));
    displayToolbar->addWidget(m_colorScheme);
    auto *colorMenuButton = new QToolButton(this);
    colorMenuButton->setText(tr("配色…"));
    colorMenuButton->setObjectName(QStringLiteral("folderColorSchemeButton"));
    colorMenuButton->setPopupMode(QToolButton::InstantPopup);
    colorMenuButton->setMenu(createColorSchemeMenu());
    displayToolbar->addWidget(colorMenuButton);
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
        // 右键「为什么是这个状态」（DIR-011 第 4 条）。菜单的构造走一个
        // 独立函数而不是埋在 lambda 里：埋在 lambda 里就没法在不弹菜单的情况下
        // 验证「菜单里确实有这一项」。
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tree, &QWidget::customContextMenuRequested, this, [this, tree](const QPoint &pos) {
            const QModelIndex index = tree->indexAt(pos);
            if (!index.isValid())
                return;
            std::unique_ptr<QMenu> menu(createStatusMenu(index));
            menu->exec(tree->viewport()->mapToGlobal(pos));
        });
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
        setStatusFilter(m_filter->currentData().toInt());
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

    // 递归子目录策略（DIR-003 第 3 条）：两个控件都改**扫描范围**，所以改动即重扫。
    // 「重扫」而不是「对已扫描结果重新过滤」：档位与深度上限决定哪些条目进结果，
    // 不在结果里的条目根本没有行可以过滤。
    connect(m_recursionTier, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const Folder::RecursionTier tier = recursionTier();
        applyTierToControls(tier);
        // 深度控件在用户换档时由这里写一次（它在程序性路径上的写入者是
        // `setOptions()`）。写的是**档位决定的深度**，不是控件里那个旧值：
        // 切到「递归深度 1」之后控件上还留着 128 的话，显示与实际生效的
        // 就是两回事了。这里必须挡住信号——写值本身不是一次用户操作。
        Folder::Options staged;
        staged.maximumDepth = m_maximumDepth->value();
        Folder::applyRecursionTier(staged, tier);
        {
            const QSignalBlocker blocker(m_maximumDepth);
            m_maximumDepth->setValue(staged.maximumDepth);
        }
        emit rescanRequested();
    });
    // 深度上限是档位的细化：把它改成 0 或 1，档位自己落到前两档——在引擎里
    // 那正是它们的含义。于是控件与档位永远不会同时给出两个互相矛盾的档。
    connect(m_maximumDepth, qOverload<int>(&QSpinBox::valueChanged), this, [this](int depth) {
        Folder::Options probe;
        probe.maximumDepth = depth;
        applyTierToControls(Folder::recursionTierOf(probe));
        emit rescanRequested();
    });
    // 配色方案（DIR-012 第 4 条）：换方案**只重画**，绝不重扫。
    // 这条连接里没有 `rescanRequested`，而「没有」是一句不可断言的话——
    // 因此它由 `Tests/Folder` 的 J 组用 `QSignalSpy` 正面钉住。
    connect(m_colorScheme, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!m_colorScheme)
            return;
        const QString identifier = m_colorScheme->currentData().toString();
        if (!identifier.isEmpty())
            setColorScheme(identifier);
    });
    rebuildColorSchemeItems();
    setColorScheme(Folder::defaultColorSchemeIdentifier());
    // 出厂状态就是缺省选项：控件初值不各写一个字面量，免得默认值长出第二份。
    setOptions(Folder::Options());
}

void FolderCompareView::rebuildColorSchemeItems()
{
    if (!m_colorScheme)
        return;
    // 重建期间挡信号：清空列表会让 `currentIndexChanged` 连发几次，
    // 每次都走一遍 setColorScheme 并重画整棵树。用户没做任何操作，
    // 却会看到列表闪几下，还会收到几个假的 `colorSchemeChanged`。
    const QSignalBlocker blocker(m_colorScheme);
    m_colorScheme->clear();
    for (const auto &scheme : Folder::colorSchemeTable()) {
        m_colorScheme->addItem(scheme.displayName, scheme.identifier);
        m_colorScheme->setItemData(m_colorScheme->count() - 1, scheme.description,
                                   Qt::ToolTipRole);
    }
    if (m_hasCustomColorScheme) {
        // 导入的自定义配色排在最后，并在名字上标明它不是出厂方案——
        // 否则用户下次打开下拉会以为自己改的那套被「重置」了。
        // **只保留一套**：导入第二次是替换而不是追加，否则一个会话里
        // 反复试几份配色之后，下拉会长出一串谁也认不出是哪个的条目。
        m_colorScheme->addItem(tr("%1（导入）").arg(m_customColorScheme.displayName),
                               m_customColorScheme.identifier);
    }
}

Folder::ColorScheme FolderCompareView::schemeForIdentifier(const QString &identifier) const
{
    if (m_hasCustomColorScheme && m_customColorScheme.identifier == identifier)
        return m_customColorScheme;
    // 出厂三套与「认不出来回落默认」都在服务层，这里不重写一遍。
    return Folder::colorSchemeByIdentifier(identifier);
}

QString FolderCompareView::colorSchemeId() const
{
    // 取**模型实际在用的**那一套，而不是下拉选中项：标准要的是「切换生效」，
    // 而只改控件不改模型正是「选了没反应」那种坏法。
    return m_model ? m_model->colorScheme().identifier : QString();
}

void FolderCompareView::setColorScheme(const QString &identifier)
{
    if (!m_model)
        return;
    const Folder::ColorScheme scheme = schemeForIdentifier(identifier);
    const bool changed = m_model->colorScheme().identifier != scheme.identifier;
    // 即使标识符没变也重新下发一次：调用方可能刚导入了一份**同名**的自定义配色，
    // 此时「没变」是假的，而只比较标识符会让新色值永远不生效。
    m_model->setColorScheme(scheme);
    if (m_colorScheme) {
        const int index = m_colorScheme->findData(scheme.identifier);
        const QSignalBlocker blocker(m_colorScheme);
        m_colorScheme->setCurrentIndex(index < 0 ? 0 : index);
    }
    // 「值没变就不发信号」与本仓其余状态入口同一条纪律：恢复存档、
    // 切标签重播之类的程序性路径不该让容器以为用户改了配色。
    if (changed)
        emit colorSchemeChanged(scheme.identifier);
}

bool FolderCompareView::exportColorScheme(const QString &path, QStringList *problems) const
{
    if (!m_model)
        return false;
    return Folder::saveColorSchemeFile(path, m_model->colorScheme(), problems);
}

bool FolderCompareView::importColorScheme(const QString &path, QStringList *problems)
{
    Folder::ColorScheme loaded;
    QStringList localProblems;
    if (!Folder::loadColorSchemeFile(path, loaded, &localProblems)) {
        // 失败时**什么都不改**：界面仍用当前配色，下拉不改选择。
        // 一次失败的导入把界面留在半套配色上，比直接说「没读进来」糟得多。
        if (problems)
            *problems += localProblems;
        return false;
    }
    m_customColorScheme = loaded;
    m_hasCustomColorScheme = true;
    rebuildColorSchemeItems();
    setColorScheme(loaded.identifier);
    return true;
}

QMenu *FolderCompareView::createColorSchemeMenu()
{
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("folderColorSchemeMenu"));
    auto *exportAction = menu->addAction(tr("导出当前配色…"));
    exportAction->setObjectName(QStringLiteral("folderColorSchemeExport"));
    auto *importAction = menu->addAction(tr("导入配色…"));
    importAction->setObjectName(QStringLiteral("folderColorSchemeImport"));
    connect(exportAction, &QAction::triggered, this, [this] { promptExportColorScheme(); });
    connect(importAction, &QAction::triggered, this, [this] { promptImportColorScheme(); });
    return menu;
}

void FolderCompareView::promptExportColorScheme()
{
    // 默认文件名取自当前方案标识符，用户改起来比从空白框开始省事。
    const QString suggestion = colorSchemeId() + QLatin1Char('.')
        + Folder::colorSchemeFileExtension();
    const QString path = QFileDialog::getSaveFileName(this, tr("导出配色方案"), suggestion,
                                                     Folder::colorSchemeFileFilter());
    if (path.isEmpty())
        return;
    QStringList problems;
    if (!exportColorScheme(path, &problems)) {
        QMessageBox::warning(this, tr("导出配色方案"), problems.join(QLatin1Char('\n')));
        return;
    }
    m_status->setText(tr("已导出配色方案：%1").arg(path));
}

void FolderCompareView::promptImportColorScheme()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("导入配色方案"), QString(),
                                                     Folder::colorSchemeFileFilter());
    if (path.isEmpty())
        return;
    QStringList problems;
    if (!importColorScheme(path, &problems)) {
        QMessageBox::warning(this, tr("导入配色方案"), problems.join(QLatin1Char('\n')));
        return;
    }
    m_status->setText(tr("已导入配色方案：%1").arg(path));
}

Folder::RecursionTier FolderCompareView::recursionTier() const
{
    const int raw = m_recursionTier->currentData().toInt();
    for (const auto &row : Folder::recursionTierTable()) {
        if (int(row.tier) == raw)
            return row.tier;
    }
    // 下拉里认不出来的整数不能直接 `static_cast`：越界值会凭空造出第四种档位，
    // 而它进了 `options()` 之后没有任何一处认得出来。
    return Folder::recursionTierOf(Folder::Options());
}

void FolderCompareView::applyTierToControls(Folder::RecursionTier tier)
{
    const int index = m_recursionTier->findData(int(tier));
    // 程序性回填不发信号：否则 setOptions()（恢复存档、会话里改设置）会顺带
    // 触发一次重扫，而那时用户什么都没做。
    // 这里**不碰深度控件**：档位对深度的写入只发生在用户换档那一条路上，
    // 放在这里就会在恢复存档时把「不递归 + 深度 7」改写成 0。
    const QSignalBlocker blocker(m_recursionTier);
    m_recursionTier->setCurrentIndex(index < 0 ? 0 : index);
    // 深度上限只在「完全递归」档下可改：另两档的深度就是档位本身。
    // 不可改时**保留**已有数值并在提示里说明它本次不生效——把它清成 0 会让
    // 「换个档试一下再换回来」把用户设好的上限悄悄丢掉。
    const bool editable = tier == Folder::RecursionTier::Full;
    m_maximumDepth->setEnabled(editable);
    m_maximumDepth->setToolTip(editable
        ? tr("递归深度上限：超过该层数的目录只显示、不展开。改成 0 或 1 时档位会随之落到前两档。")
        : tr("当前档位已经决定了递归深度；此值会被保留，但本次比较不生效。"));
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
    // 一个字段一个写入者：`recursive` 归档位下拉，`maximumDepth` 归深度控件。
    // 档位**不在这里**碰深度——用户另设的上限必须活过一次档位往返，否则
    // `.lqc` 存档里的「不递归 + 深度 7」会被静默改写成 0。
    options.recursive = Folder::recursionTierRecurses(recursionTier());
    options.maximumDepth = m_maximumDepth->value();
    options.compareContent = m_content->isChecked();
    options.scanMaskDeclaration = m_scanMask->toPlainText();
    options.nameCaseSensitivity = m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    options.compareFirstBytes = m_compareFirstBytes;
    return options;
}

void FolderCompareView::setOptions(const Folder::Options &options)
{
    // 程序性回填，**不做任何归一化**：恢复存档时「不递归 + 深度 7」必须原样落到
    // 控件上（`savedFolderOptionsRoundTripThroughLqcAndActuallyScan` 钉住这一点）；
    // 归一化会把它改写成 0，用户的设置就静默丢了。
    applyTierToControls(Folder::recursionTierOf(options));
    {
        // 落盘值来自会话文件（可能被人手改过），越界整数直接喂进控件会被它
        // 悄悄夹到边界上；这里按引擎的上界显式夹一次，让「值被改掉」发生在一处。
        const QSignalBlocker blocker(m_maximumDepth);
        m_maximumDepth->setValue(qBound(0, options.maximumDepth, Folder::kMaximumRecursionDepth));
    }
    m_content->setChecked(options.compareContent);
    m_scanMask->setPlainText(options.scanMaskDeclaration);
    m_caseSensitive->setChecked(options.nameCaseSensitivity == Qt::CaseSensitive);
    m_compareFirstBytes = options.compareFirstBytes;
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

const Folder::Entry *FolderCompareView::entryForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    return m_model->entry(m_filterModel->mapToSource(index));
}

void FolderCompareView::setStatusFilter(int status)
{
    // -1 / -2 是本类自己的哨兵值；其余只接受主状态表里的取值。
    // 越界值不能直接喂进筛选：它会让列表一条都不显示，而用户以为自己
    // 选中的是某个状态——状态整数要经过模型角色与下拉数据两跳，任一跳出错
    // 都会走到这里，而「一整屏空白」是最难归因的一种失败。
    const bool sentinel = status == -1 || status == -2;
    const int target = sentinel || Folder::isKnownStatus(status) ? status : -1;
    const int row = m_filter->findData(target);
    if (row >= 0)
        m_filter->setCurrentIndex(row);
    m_filterModel->setStatusFilter(target);
    updateCount();
}

QString FolderCompareView::statusExplanation(const QModelIndex &index) const
{
    const auto *entry = entryForIndex(index);
    if (!entry)
        return {};
    const auto lines = Folder::statusReasonLines(*entry, options(), m_model->baselineApplied());
    // 三节**恒定**出现，哪怕某一节没有内容。规格要求这一栏「列出各准则、
    // 覆盖策略与最终结论」；按需省略空小节会让读者分不清「这一节没有内容」
    // 和「这一节根本没实现」，而排查一个诡异状态时最要紧的恰恰是这一区分。
    static const QVector<Folder::ReasonKind> sections = {
        Folder::ReasonKind::Criterion, Folder::ReasonKind::Override,
        Folder::ReasonKind::Conclusion};
    QStringList text;
    for (Folder::ReasonKind kind : sections) {
        text << QStringLiteral("%1：").arg(Folder::reasonKindLabel(kind));
        bool any = false;
        for (const auto &line : lines) {
            if (line.kind != kind)
                continue;
            text << QStringLiteral("    ") + line.text;
            any = true;
        }
        if (!any)
            text << QStringLiteral("    （无）");
    }
    return text.join(QLatin1Char('\n'));
}

QMenu *FolderCompareView::createStatusMenu(const QModelIndex &index)
{
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("folderStatusMenu"));
    QAction *why = menu->addAction(tr("为什么是这个状态…"));
    why->setObjectName(QStringLiteral("folderWhyStatusAction"));
    why->setEnabled(entryForIndex(index) != nullptr);
    const QModelIndex captured = index;
    connect(why, &QAction::triggered, this, [this, captured] { showStatusReason(captured); });
    return menu;
}

void FolderCompareView::showStatusReason(const QModelIndex &index)
{
    const auto *entry = entryForIndex(index);
    if (!entry)
        return;
    // 连点两次右键不留一叠窗口。
    if (auto *previous = findChild<QMessageBox *>(QStringLiteral("folderStatusReasonBox")))
        previous->deleteLater();
    auto *box = new QMessageBox(QMessageBox::NoIcon, tr("为什么是这个状态：%1").arg(entry->relativePath),
                                statusExplanation(index), QMessageBox::Close, this);
    box->setObjectName(QStringLiteral("folderStatusReasonBox"));
    // 刻意不用 exec()：模态对话框会在这里开一个嵌套事件循环，
    // 右键之后视图只能靠人去点掉——离屏测试与自动化会一起卡在这一行。
    box->setModal(false);
    box->show();
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
