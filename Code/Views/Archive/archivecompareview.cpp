#include "archivecompareview.h"

#include <QCheckBox>
#include <QColor>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

namespace LqCompare {
namespace {
class ArchiveFilterModel final : public QSortFilterProxyModel
{
public:
    explicit ArchiveFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {}
    void setSearch(const QString &search) { m_search = search; invalidateFilter(); }
    void setDifferencesOnly(bool value) { m_differencesOnly = value; invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const auto index = sourceModel()->index(row, ArchiveEntryModel::Path, parent);
        if (m_differencesOnly && index.data(ArchiveEntryModel::DifferenceRole).toInt()
            == static_cast<int>(Archive::Difference::MatchingMetadata)) return false;
        return m_search.isEmpty() || index.data().toString().contains(m_search, Qt::CaseInsensitive);
    }

private:
    QString m_search;
    bool m_differencesOnly = false;
};

QString methodName(quint16 method)
{
    if (method == 0) return QObject::tr("存储 (0)");
    if (method == 8) return QObject::tr("Deflate (8)");
    return QObject::tr("未知 (%1)").arg(method);
}
}

ArchiveEntryModel::ArchiveEntryModel(QObject *parent) : QAbstractTableModel(parent) {}

int ArchiveEntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_comparison.rows.size();
}

int ArchiveEntryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ArchiveEntryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_comparison.rows.size()
        || index.column() < 0 || index.column() >= ColumnCount) return {};
    const auto &row = m_comparison.rows.at(index.row());
    if (role == DifferenceRole) return static_cast<int>(row.difference);
    if (role == EvidenceRole) return row.evidence;
    if (role == Qt::ToolTipRole) return row.path + QStringLiteral("\n") + row.evidence
                                      + QStringLiteral("\n") + Archive::metadataNotice();
    if (role == Qt::ForegroundRole && index.column() == Status) {
        switch (row.difference) {
        case Archive::Difference::MatchingMetadata: return QColor(90, 90, 90);
        case Archive::Difference::LeftOnly: return QColor(35, 96, 163);
        case Archive::Difference::RightOnly: return QColor(133, 74, 152);
        default: return QColor(157, 73, 20);
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() >= LeftSize && index.column() <= RightCrc)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole && role != SortRole) return {};
    if (index.column() == Path) return row.path;
    if (index.column() == Status) return Archive::differenceLabel(row.difference);

    const bool leftSide = index.column() % 2 == 0;
    const int entryIndex = leftSide ? row.leftIndex : row.rightIndex;
    const auto &entries = leftSide ? m_comparison.left.entries : m_comparison.right.entries;
    if (entryIndex < 0 || entryIndex >= entries.size())
        return role == SortRole ? QVariant() : QVariant(QStringLiteral("—"));
    const auto &entry = entries.at(entryIndex);
    switch (index.column()) {
    case LeftSize: case RightSize:
        if (entry.directory) return role == SortRole ? QVariant() : QVariant(QStringLiteral("—"));
        return role == SortRole ? QVariant::fromValue(entry.uncompressedSize)
                                : QVariant(QString::number(entry.uncompressedSize));
    case LeftPackedSize: case RightPackedSize:
        if (entry.directory) return role == SortRole ? QVariant() : QVariant(QStringLiteral("—"));
        return role == SortRole ? QVariant::fromValue(entry.compressedSize)
                                : QVariant(QString::number(entry.compressedSize));
    case LeftCrc: case RightCrc:
        if (entry.directory) return role == SortRole ? QVariant() : QVariant(QStringLiteral("—"));
        return role == SortRole ? QVariant(entry.crc32)
            : QVariant(QStringLiteral("%1").arg(entry.crc32, 8, 16, QLatin1Char('0')).toUpper());
    case LeftModified: case RightModified:
        if (!entry.modified.isValid()) return role == SortRole ? QVariant() : QVariant(QStringLiteral("—"));
        return role == SortRole ? QVariant(entry.modified)
            : QVariant(entry.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    case LeftKind: case RightKind:
        return entry.directory ? (entry.implicitDirectory ? tr("目录（推导）") : tr("目录")) : tr("文件");
    case LeftMethod: case RightMethod:
        return entry.directory ? QStringLiteral("—") : methodName(entry.method);
    default: return {};
    }
}

QVariant ArchiveEntryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    const QStringList labels = {tr("归档内路径"), tr("结构 / 元数据状态"),
        tr("左 原始字节"), tr("右 原始字节"), tr("左 压缩字节"), tr("右 压缩字节"),
        tr("左 CRC-32 声明值"), tr("右 CRC-32 声明值"), tr("左 DOS 修改时间（无时区）"),
        tr("右 DOS 修改时间（无时区）"), tr("左 类型"), tr("右 类型"), tr("左 算法"), tr("右 算法")};
    return section >= 0 && section < labels.size() ? QVariant(labels.at(section)) : QVariant();
}

void ArchiveEntryModel::setComparison(const Archive::Comparison &comparison)
{
    beginResetModel();
    m_comparison = comparison;
    endResetModel();
}

ArchiveCompareView::ArchiveCompareView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("archiveCompareView"));
    auto *layout = new QVBoxLayout(this);
    const auto addPath = [this, layout](const QString &label, const QString &name) {
        auto *row = new QHBoxLayout;
        auto *edit = new QLineEdit(this);
        edit->setObjectName(name);
        edit->setPlaceholderText(tr("选择 ZIP / JAR 文件"));
        auto *browse = new QPushButton(tr("浏览…"), this);
        browse->setObjectName(name + QStringLiteral("Browse"));
        row->addWidget(new QLabel(label, this));
        row->addWidget(edit, 1);
        row->addWidget(browse);
        layout->addLayout(row);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString file = QFileDialog::getOpenFileName(this, tr("选择归档"), edit->text(),
                tr("ZIP / JAR 归档 (*.zip *.jar);;所有文件 (*)"));
            if (!file.isEmpty()) edit->setText(file);
        });
        return edit;
    };
    m_left = addPath(tr("左侧"), QStringLiteral("archiveLeftPath"));
    m_right = addPath(tr("右侧"), QStringLiteral("archiveRightPath"));

    auto *actions = new QHBoxLayout;
    auto *compare = new QPushButton(tr("比较 ZIP"), this);
    compare->setObjectName(QStringLiteral("archiveCompare"));
    m_reload = new QPushButton(tr("重新读取"), this);
    m_reload->setObjectName(QStringLiteral("archiveReload"));
    m_previous = new QPushButton(tr("上一差异"), this);
    m_previous->setObjectName(QStringLiteral("archivePreviousDifference"));
    m_next = new QPushButton(tr("下一差异"), this);
    m_next->setObjectName(QStringLiteral("archiveNextDifference"));
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("archiveSearch"));
    m_search->setPlaceholderText(tr("筛选归档内路径（文字匹配）"));
    m_search->setClearButtonEnabled(true);
    m_differencesOnly = new QCheckBox(tr("仅显示结构 / 元数据差异"), this);
    m_differencesOnly->setObjectName(QStringLiteral("archiveDifferencesOnly"));
    actions->addWidget(compare);
    actions->addWidget(m_reload);
    actions->addWidget(m_previous);
    actions->addWidget(m_next);
    actions->addWidget(m_search, 1);
    actions->addWidget(m_differencesOnly);
    layout->addLayout(actions);

    auto *notice = new QLabel(Archive::metadataNotice(), this);
    notice->setObjectName(QStringLiteral("archiveMetadataNotice"));
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(notice);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("archiveSummary"));
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_model = new ArchiveEntryModel(this);
    m_model->setObjectName(QStringLiteral("archiveEntryModel"));
    auto *proxy = new ArchiveFilterModel(this);
    m_proxy = proxy;
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(ArchiveEntryModel::SortRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseSensitive);
    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("archiveEntries"));
    m_table->setModel(m_proxy);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ArchiveEntryModel::Path, Qt::AscendingOrder);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(25);
    m_table->horizontalHeader()->setDefaultSectionSize(105);
    m_table->setColumnWidth(ArchiveEntryModel::Path, 235);
    m_table->setColumnWidth(ArchiveEntryModel::Status, 330);
    m_table->setColumnWidth(ArchiveEntryModel::LeftCrc, 155);
    m_table->setColumnWidth(ArchiveEntryModel::RightCrc, 155);
    m_table->setColumnWidth(ArchiveEntryModel::LeftModified, 230);
    m_table->setColumnWidth(ArchiveEntryModel::RightModified, 230);
    layout->addWidget(m_table, 1);

    m_evidence = new QLabel(this);
    m_evidence->setObjectName(QStringLiteral("archiveEvidence"));
    m_evidence->setTextFormat(Qt::PlainText);
    m_evidence->setWordWrap(true);
    m_evidence->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_evidence);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("archiveStatus"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    const auto requestCompare = [this] { emit compareRequested(m_left->text(), m_right->text()); };
    connect(compare, &QPushButton::clicked, this, requestCompare);
    connect(m_left, &QLineEdit::returnPressed, this, requestCompare);
    connect(m_right, &QLineEdit::returnPressed, this, requestCompare);
    connect(m_reload, &QPushButton::clicked, this, &ArchiveCompareView::reloadRequested);
    connect(m_previous, &QPushButton::clicked, this, &ArchiveCompareView::previousDifference);
    connect(m_next, &QPushButton::clicked, this, &ArchiveCompareView::nextDifference);
    connect(m_search, &QLineEdit::textChanged, this, [this, proxy](const QString &text) {
        proxy->setSearch(text);
        refreshVisibleRows();
    });
    connect(m_differencesOnly, &QCheckBox::toggled, this, [this, proxy](bool checked) {
        proxy->setDifferencesOnly(checked);
        refreshVisibleRows();
    });
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, [this] { refreshVisibleRows(); });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { updateEvidence(); });
    setComparison({}, false);
}

void ArchiveCompareView::setPaths(const QString &left, const QString &right)
{
    m_left->setText(left);
    m_right->setText(right);
}

void ArchiveCompareView::setComparison(const Archive::Comparison &comparison, bool available)
{
    m_model->setComparison(comparison);
    m_reload->setEnabled(available);
    refreshVisibleRows();
    updateEvidence();
}

void ArchiveCompareView::setStatus(const QString &text)
{
    m_status->setText(text);
}

void ArchiveCompareView::refreshVisibleRows()
{
    m_visibleDifferences.clear();
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        if (m_proxy->index(row, 0).data(ArchiveEntryModel::DifferenceRole).toInt()
            != static_cast<int>(Archive::Difference::MatchingMetadata)) m_visibleDifferences.append(row);
    }
    m_summary->setText(tr("显示 %1 / %2 项 · 当前可见差异 %3 项（目录与文件）")
                       .arg(m_proxy->rowCount()).arg(m_model->rowCount()).arg(m_visibleDifferences.size()));
    m_previous->setEnabled(!m_visibleDifferences.isEmpty());
    m_next->setEnabled(!m_visibleDifferences.isEmpty());
    updateEvidence();
}

void ArchiveCompareView::previousDifference() { navigateDifference(false); }
void ArchiveCompareView::nextDifference() { navigateDifference(true); }

void ArchiveCompareView::navigateDifference(bool forward)
{
    if (m_visibleDifferences.isEmpty()) return;
    const int current = m_table->currentIndex().row();
    int destination = forward ? m_visibleDifferences.first() : m_visibleDifferences.last();
    if (current >= 0) {
        destination = forward ? m_visibleDifferences.last() : m_visibleDifferences.first();
        if (forward) {
            for (int row : m_visibleDifferences) {
                if (row > current) { destination = row; break; }
            }
        } else {
            for (int i = m_visibleDifferences.size() - 1; i >= 0; --i) {
                if (m_visibleDifferences.at(i) < current) { destination = m_visibleDifferences.at(i); break; }
            }
        }
    }
    m_table->selectRow(destination);
    m_table->setCurrentIndex(m_proxy->index(destination, ArchiveEntryModel::Path));
    m_table->scrollTo(m_table->currentIndex());
}

void ArchiveCompareView::updateEvidence()
{
    const auto index = m_table->currentIndex();
    m_evidence->setText(index.isValid() ? index.data(ArchiveEntryModel::EvidenceRole).toString()
                                      : tr("选择条目查看差异证据；匹配的大小与 CRC 不证明内容相同。"));
}

} // namespace LqCompare
