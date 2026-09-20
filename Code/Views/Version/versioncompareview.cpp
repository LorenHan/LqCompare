#include "versioncompareview.h"
#include "versioncomparesession.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
namespace LqCompare {
VersionCompareView::VersionCompareView(VersionCompareSession *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("versionCompareView"));
    auto *layout = new QVBoxLayout(this);
    m_leftPath = new QLineEdit(session->leftPath(), this); m_leftPath->setObjectName(QStringLiteral("versionLeftPath"));
    m_rightPath = new QLineEdit(session->rightPath(), this); m_rightPath->setObjectName(QStringLiteral("versionRightPath"));
    const auto addPath = [this, layout, session](const QString &label, QLineEdit *edit) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this)); row->addWidget(edit, 1);
        auto *browse = new QPushButton(tr("Browse…"), this); row->addWidget(browse); layout->addLayout(row);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString path = QFileDialog::getOpenFileName(this, tr("Choose file"), edit->text());
            if (!path.isEmpty()) edit->setText(path);
        });
        connect(session, &VersionCompareSession::busyChanged, browse, [browse](bool busy) { browse->setEnabled(!busy); });
        connect(session, &VersionCompareSession::busyChanged, edit, [edit](bool busy) { edit->setEnabled(!busy); });
        browse->setEnabled(!session->isBusy()); edit->setEnabled(!session->isBusy());
        connect(edit, &QLineEdit::returnPressed, this, &VersionCompareView::comparePaths);
    };
    addPath(tr("Left"), m_leftPath); addPath(tr("Right"), m_rightPath);
    auto *toolbar = new QHBoxLayout;
    auto *compare = new QPushButton(tr("Compare"), this); compare->setObjectName(QStringLiteral("versionCompareButton")); toolbar->addWidget(compare);
    connect(compare, &QPushButton::clicked, this, &VersionCompareView::comparePaths);
    compare->setEnabled(!session->isBusy());
    connect(session, &VersionCompareSession::busyChanged, compare, [compare](bool busy) { compare->setEnabled(!busy); });
    auto *ignore = new QCheckBox(tr("Ignore version number differences"), this); ignore->setObjectName(QStringLiteral("versionIgnoreNumbers"));
    ignore->setChecked(session->options().ignoreVersionNumbers); toolbar->addWidget(ignore);
    auto *padding = new QComboBox(this); padding->setObjectName(QStringLiteral("versionSegmentPolicy"));
    padding->addItem(tr("1.2 equals 1.2.0"), true); padding->addItem(tr("Different segment counts are incomparable"), false);
    padding->setCurrentIndex(session->options().padMissingVersionSegments ? 0 : 1); toolbar->addWidget(padding);
    const auto options = [session, ignore, padding] {
        auto options = session->options(); options.ignoreVersionNumbers = ignore->isChecked(); options.padMissingVersionSegments = padding->currentData().toBool(); session->setOptions(options);
    };
    connect(ignore, &QCheckBox::toggled, this, options); connect(padding, QOverload<int>::of(&QComboBox::currentIndexChanged), this, options);
    toolbar->addStretch();
    auto *exportButton = new QPushButton(tr("Export CSV…"), this); exportButton->setObjectName(QStringLiteral("versionExportCsv")); toolbar->addWidget(exportButton);
    exportButton->setEnabled(session->isLoaded());
    connect(session, &VersionCompareSession::comparisonChanged, exportButton, [session, exportButton] { exportButton->setEnabled(session->isLoaded()); });
    connect(exportButton, &QPushButton::clicked, this, [this, session] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export version comparison"), QStringLiteral("version-comparison.csv"), tr("CSV files (*.csv)"));
        if (path.isEmpty()) return;
        QString error; session->exportCsv(path, &error); m_error->setText(error); m_error->setVisible(!error.isEmpty());
    });
    layout->addLayout(toolbar);
    auto *note = new QLabel(tr("Read-only • PE32 / PE32+ • Other files show metadata and no version • Imports contain recorded names/ordinals, not DLL versions or function signatures. Numeric comparison accepts decimal segments and an optional v prefix; suffixes such as -beta are incomparable."), this);
    note->setWordWrap(true); layout->addWidget(note);
    m_error = new QLabel(this); m_error->setObjectName(QStringLiteral("versionError")); m_error->setWordWrap(true); m_error->setStyleSheet(QStringLiteral("color: #b42318")); m_error->hide(); layout->addWidget(m_error);
    m_table = new QTreeWidget(this); m_table->setObjectName(QStringLiteral("versionDifferenceTable"));
    m_table->setColumnCount(5); m_table->setHeaderLabels({tr("Field / group"), tr("Left"), tr("Right"), tr("Difference"), tr("Numeric relation")});
    m_table->setAlternatingRowColors(true); m_table->setUniformRowHeights(true); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection); m_table->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(0, 310); m_table->setColumnWidth(1, 230); m_table->setColumnWidth(2, 230); m_table->setColumnWidth(3, 110); m_table->setColumnWidth(4, 165);
    layout->addWidget(m_table, 1);
    auto *status = new QLabel(session->statusText(), this); status->setObjectName(QStringLiteral("versionStatus")); status->setWordWrap(true); layout->addWidget(status);
    connect(session, &CompareSession::statusTextChanged, status, &QLabel::setText);
    connect(session, &VersionCompareSession::pathsChanged, this, [this] { m_leftPath->setText(m_session->leftPath()); m_rightPath->setText(m_session->rightPath()); });
    connect(session, &VersionCompareSession::comparisonChanged, this, &VersionCompareView::refresh);
    connect(session, &VersionCompareSession::loadFinished, this, [this](bool, const QString &error) { m_error->setText(error); m_error->setVisible(!error.isEmpty()); });
    refresh();
}
void VersionCompareView::comparePaths()
{
    QString error;
    if (m_session->setPaths(m_leftPath->text(), m_rightPath->text(), &error) && m_session->state() != CompareSession::State::Open)
        m_session->open(&error);
    m_error->setText(error); m_error->setVisible(!error.isEmpty());
}
void VersionCompareView::refresh()
{
    QMap<QString, bool> expanded;
    for (int i = 0; i < m_table->topLevelItemCount(); ++i) {
        const auto *item = m_table->topLevelItem(i); expanded[item->data(0, Qt::UserRole).toString()] = item->isExpanded();
    }
    m_table->clear(); QMap<QString, QTreeWidgetItem *> groups; QMap<QString, int> counts; QSet<QString> versionGroups;
    for (const auto &row : m_session->rows()) {
        if (row.versionNumber || row.key.startsWith(QStringLiteral("Fixed/")) || row.key.startsWith(QStringLiteral("Strings/")))
            versionGroups.insert(row.group);
        auto *group = groups.value(row.group);
        if (!group) {
            group = new QTreeWidgetItem(m_table, {row.group}); group->setData(0, Qt::UserRole, row.group);
            groups[row.group] = group; group->setExpanded(expanded.value(row.group, true));
        }
        QString difference;
        switch (row.difference) {
        case Version::Difference::Equal: difference = tr("Equal"); break;
        case Version::Difference::Changed: difference = tr("Changed"); break;
        case Version::Difference::LeftOnly: difference = tr("Left only"); break;
        case Version::Difference::RightOnly: difference = tr("Right only"); break;
        case Version::Difference::Ignored: difference = tr("Ignored"); break;
        }
        auto *item = new QTreeWidgetItem(group, {row.key, row.left, row.right, difference, row.versionNumber ? Version::relationText(row.relation) : QString()});
        for (int col = 0; col < 5; ++col) item->setToolTip(col, item->text(col));
        if (row.difference != Version::Difference::Equal && row.difference != Version::Difference::Ignored) {
            ++counts[row.group];
            for (int col = 0; col < 5; ++col) { item->setBackground(col, QColor(255, 232, 193)); item->setForeground(col, QColor(60, 35, 15)); }
        }
    }
    int versionIndex = 0;
    for (auto i = groups.cbegin(); i != groups.cend(); ++i) {
        auto *group = i.value();
        group->setText(0, tr("%1 (%2 differences)").arg(i.key()).arg(counts.value(i.key())));
        group->setToolTip(0, group->text(0));
        group->setFirstColumnSpanned(true);
        if (versionGroups.contains(i.key())) {
            m_table->takeTopLevelItem(m_table->indexOfTopLevelItem(group));
            m_table->insertTopLevelItem(versionIndex++, group);
            group->setExpanded(expanded.value(i.key(), true));
            group->setFirstColumnSpanned(true);
        }
    }
}
}
