#include "registrycompareview.h"
#include "registrycomparesession.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace LqCompare {
namespace {
class RegistryItem : public QTreeWidgetItem {
public:
    explicit RegistryItem(QTreeWidget *tree) : QTreeWidgetItem(tree) {}
    explicit RegistryItem(QTreeWidgetItem *parent) : QTreeWidgetItem(parent) {}
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (column == 1) {
            const int a = data(0, RegistryCompareView::StatusRole).toInt();
            const int b = other.data(0, RegistryCompareView::StatusRole).toInt();
            if (a != b) return a < b;
        }
        const int result = text(column).compare(other.text(column), Qt::CaseInsensitive);
        return result == 0 ? text(0).compare(other.text(0), Qt::CaseInsensitive) < 0 : result < 0;
    }
};

bool filterItem(QTreeWidgetItem *item, int status)
{
    bool visible = status < 0 || item->data(0, RegistryCompareView::StatusRole).toInt() == status;
    for (int i = 0; i < item->childCount(); ++i)
        visible = filterItem(item->child(i), status) || visible;
    item->setHidden(!visible);
    return visible;
}

QString preview(const Registry::Value &value)
{
    const QString text = Registry::displayValue(value);
    constexpr int maxPreview = 4096;
    return text.size() > maxPreview
        ? text.left(maxPreview) + RegistryCompareView::tr(" … (%1 characters)").arg(text.size()) : text;
}
}

RegistryCompareView::RegistryCompareView(RegistryCompareSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("registryCompareView"));
    auto *layout = new QVBoxLayout(this);
    auto addSource = [this, layout](const QString &label, const QString &prefix) {
        auto *row = new QHBoxLayout;
        auto *caption = new QLabel(label, this);
        auto *edit = new QLineEdit(this);
        edit->setObjectName(prefix + QStringLiteral("Path"));
        edit->setPlaceholderText(tr(".reg export file or HKCU\\Software\\…"));
        caption->setBuddy(edit);
        auto *browse = new QPushButton(tr("Browse…"), this);
        browse->setObjectName(prefix + QStringLiteral("Browse"));
        auto *local = new QPushButton(tr("Local HKCU…"), this);
        local->setObjectName(prefix + QStringLiteral("Local"));
        local->setEnabled(Registry::localProviderAvailable());
        local->setToolTip(Registry::localProviderDescription());
        row->addWidget(caption);
        row->addWidget(edit, 1);
        row->addWidget(browse);
        row->addWidget(local);
        layout->addLayout(row);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString path = QFileDialog::getOpenFileName(this, tr("Open registry export"),
                edit->text(), tr("Registry exports (*.reg);;All files (*)"));
            if (!path.isEmpty()) edit->setText(path);
        });
        connect(local, &QPushButton::clicked, this, [this, edit] {
            bool accepted = false;
            const QString root = QInputDialog::getText(this, tr("Read local HKCU subtree"),
                tr("HKCU key path (read-only, current process permissions):"), QLineEdit::Normal,
                QStringLiteral("HKCU\\Software"), &accepted);
            if (accepted && !root.trimmed().isEmpty()) edit->setText(root.trimmed());
        });
        connect(edit, &QLineEdit::returnPressed, this, &RegistryCompareView::comparePaths);
        return edit;
    };
    m_leftPath = addSource(tr("&Left:"), QStringLiteral("registryLeft"));
    m_rightPath = addSource(tr("&Right:"), QStringLiteral("registryRight"));

    auto *controls = new QHBoxLayout;
    auto *compare = new QPushButton(tr("Compare"), this);
    compare->setObjectName(QStringLiteral("registryCompare"));
    connect(compare, &QPushButton::clicked, this, &RegistryCompareView::comparePaths);
    controls->addWidget(compare);
    controls->addWidget(new QLabel(tr("ANSI encoding:"), this));
    m_codec = new QComboBox(this);
    m_codec->setObjectName(QStringLiteral("registryAnsiCodec"));
    m_codec->addItem(tr("Western (Windows-1252)"), QByteArray("Windows-1252"));
    m_codec->addItem(tr("Simplified Chinese (GB18030)"), QByteArray("GB18030"));
    m_codec->addItem(tr("Traditional Chinese (Big5)"), QByteArray("Big5"));
    m_codec->addItem(tr("Japanese (Shift-JIS)"), QByteArray("Shift-JIS"));
    m_codec->addItem(tr("Cyrillic (Windows-1251)"), QByteArray("Windows-1251"));
    m_codec->setToolTip(tr("Used for legacy ANSI exports without a Unicode byte-order mark."));
    controls->addWidget(m_codec);
    controls->addStretch();
    controls->addWidget(new QLabel(tr("Status:"), this));
    m_statusFilter = new QComboBox(this);
    m_statusFilter->setObjectName(QStringLiteral("registryStatusFilter"));
    m_statusFilter->addItem(tr("All"), -1);
    for (const Registry::Status status : {Registry::Status::Equal, Registry::Status::OnlyLeft,
         Registry::Status::OnlyRight, Registry::Status::TypeChanged, Registry::Status::DataChanged,
         Registry::Status::OperationChanged, Registry::Status::Unreadable})
        m_statusFilter->addItem(Registry::statusName(status), int(status));
    controls->addWidget(m_statusFilter);
    layout->addLayout(controls);

    auto *scope = new QLabel(tr("Read-only comparison. Local sources are limited to HKCU. ")
                            + Registry::localProviderDescription(), this);
    scope->setObjectName(QStringLiteral("registryScope"));
    scope->setWordWrap(true);
    layout->addWidget(scope);
    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("registryError"));
    m_error->setWordWrap(true);
    m_error->setTextFormat(Qt::PlainText);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_error->hide();
    layout->addWidget(m_error);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("registryTree"));
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({tr("Key / value"), tr("Status"), tr("Left type"), tr("Left data"),
                            tr("Right type"), tr("Right data")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->setColumnWidth(0, 240);
    m_tree->setColumnWidth(1, 140);
    m_tree->setColumnWidth(2, 110);
    m_tree->setColumnWidth(3, 240);
    m_tree->setColumnWidth(4, 110);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree, 1);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("registrySummary"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RegistryCompareView::applyFilter);
    connect(m_codec, QOverload<int>::of(&QComboBox::activated), this, [this] {
        if (!m_session) return;
        Registry::ReadOptions options = m_session->readOptions();
        options.ansiCodec = m_codec->currentData().toByteArray();
        m_session->setReadOptions(options);
        syncOptions();
    });
    if (session) {
        connect(session, &RegistryCompareSession::pathsChanged, this, &RegistryCompareView::syncPaths);
        connect(session, &RegistryCompareSession::readOptionsChanged, this, &RegistryCompareView::syncOptions);
        connect(session, &RegistryCompareSession::comparisonChanged, this, &RegistryCompareView::rebuildTree);
        connect(session, &CompareSession::statusTextChanged, m_summary, &QLabel::setText);
        connect(session, &CompareSession::errorReported, this, [this](const SessionError &error) {
            QString text = error.message;
            if (!error.detail.isEmpty()) text += QLatin1Char('\n') + error.detail;
            if (m_session && m_session->isLoaded()) text += tr("\nThe previous comparison remains displayed.");
            m_error->setText(text);
            m_error->setVisible(!text.isEmpty());
        });
        connect(session, &CompareSession::stateChanged, this, [this](CompareSession::State state) {
            setEnabled(state != CompareSession::State::Closed && state != CompareSession::State::Opening);
        });
        connect(session, &QObject::destroyed, this, [this] { setEnabled(false); });
        syncPaths();
        syncOptions();
        rebuildTree();
    } else setEnabled(false);
}

void RegistryCompareView::syncPaths()
{
    if (!m_session) return;
    m_leftPath->setText(m_session->leftPath());
    m_rightPath->setText(m_session->rightPath());
}
void RegistryCompareView::syncOptions()
{
    if (!m_session) return;
    const QSignalBlocker blocker(m_codec);
    const QByteArray codec = m_session->readOptions().ansiCodec;
    int index = m_codec->findData(codec);
    if (index < 0) {
        m_codec->addItem(QString::fromLatin1(codec), codec);
        index = m_codec->count() - 1;
    }
    m_codec->setCurrentIndex(index);
}
void RegistryCompareView::comparePaths()
{
    if (!m_session) return;
    if (!m_session->setPaths(m_leftPath->text(), m_rightPath->text())) return;
    if (m_session->state() != CompareSession::State::Open) m_session->open();
}

void RegistryCompareView::rebuildTree()
{
    m_tree->setSortingEnabled(false);
    m_tree->clear();
    m_error->clear();
    m_error->hide();
    if (!m_session) return;
    m_summary->setText(m_session->statusText());
    QHash<QString, QTreeWidgetItem *> keys;
    auto ensureKey = [this, &keys](const QString &path) {
        QTreeWidgetItem *parent = nullptr;
        QString full;
        for (const QString &part : path.split(QLatin1Char('\\'), Qt::SkipEmptyParts)) {
            if (!full.isEmpty()) full += QLatin1Char('\\');
            full += part;
            const QString id = Registry::identity(full);
            QTreeWidgetItem *item = keys.value(id);
            if (!item) {
                item = parent ? new RegistryItem(parent) : new RegistryItem(m_tree);
                item->setText(0, part);
                QFont keyFont = item->font(0);
                keyFont.setBold(true);
                item->setFont(0, keyFont);
                item->setData(0, StatusRole, -1);
                item->setData(0, KindRole, int(Registry::EntryKind::Key));
                item->setData(0, KeyPathRole, full);
                item->setToolTip(0, full);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                keys.insert(id, item);
            }
            parent = item;
        }
        return parent;
    };
    for (const Registry::Difference &difference : m_session->comparison().entries) {
        QTreeWidgetItem *key = ensureKey(difference.path);
        if (!key) continue;
        QTreeWidgetItem *item = key;
        if (difference.kind == Registry::EntryKind::Value) {
            item = new RegistryItem(key);
            item->setText(0, difference.valueName.isEmpty() ? tr("(Default)") : difference.valueName);
            item->setData(0, ValueNameRole, difference.valueName);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
        item->setData(0, KindRole, int(difference.kind));
        item->setData(0, KeyPathRole, difference.path);
        item->setData(0, StatusRole, int(difference.status));
        item->setText(1, Registry::statusName(difference.status));
        item->setToolTip(0, difference.path);
        QString detail = difference.detail;
        if (difference.kind == Registry::EntryKind::Key && difference.status == Registry::Status::Equal)
            detail = tr("This key's existence and export operation match. Child keys and values are compared separately.")
                + (detail.isEmpty() ? QString() : QLatin1Char('\n') + detail);
        for (int column = 1; column < 6; ++column) item->setToolTip(column, detail);
        auto populateSide = [item, &difference, this](bool left) {
            if (!(left ? difference.hasLeft : difference.hasRight)) return;
            const int typeColumn = left ? 2 : 4;
            if (difference.kind == Registry::EntryKind::Value) {
                const Registry::Value &value = left ? difference.left : difference.right;
                item->setText(typeColumn, value.deleted ? tr("Delete instruction") : Registry::typeName(value.type));
                item->setText(typeColumn + 1, preview(value));
                item->setToolTip(typeColumn + 1, preview(value));
            } else {
                const auto &snapshot = left ? m_session->leftSnapshot() : m_session->rightSnapshot();
                const auto entry = snapshot.keys.constFind(Registry::identity(difference.path));
                item->setText(typeColumn, tr("Key"));
                if (entry != snapshot.keys.cend()) {
                    if (!entry->error.isEmpty()) item->setText(typeColumn + 1, entry->error);
                    else if (entry->deleted) item->setText(typeColumn + 1, tr("Delete instruction (not executed)"));
                }
            }
        };
        populateSide(true);
        populateSide(false);
    }
    m_tree->setSortingEnabled(true);
    // Qt's size hint only considers visible rows. Measure collapsed descendants
    // too so expanding a key cannot reveal clipped status and registry type names.
    const int columns[] = {1, 2, 4};
    int widths[] = {110, 110, 110};
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        for (int i = 0; i < 3; ++i)
            widths[i] = qMax(widths[i], m_tree->fontMetrics().horizontalAdvance((*it)->text(columns[i])) + 28);
    for (int i = 0; i < 3; ++i) m_tree->setColumnWidth(columns[i], widths[i]);
    applyFilter();
    m_tree->expandToDepth(2);
}
void RegistryCompareView::applyFilter()
{
    const int status = m_statusFilter->currentData().toInt();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        filterItem(m_tree->topLevelItem(i), status);
    if (status >= 0) m_tree->expandAll();
}

} // namespace LqCompare
