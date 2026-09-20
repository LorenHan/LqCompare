#include "tablecompareview.h"
#include "tablecomparesession.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace LqCompare {
namespace {
class TableCellDelegate : public QStyledItemDelegate {
public:
    explicit TableCellDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto visible = option;
        const bool selected = visible.state & QStyle::State_Selected;
        // Keep the actual cell status visible when navigating/selecting a row.
        // A border carries selection independently from the semantic fill.
        visible.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(painter, visible, index);
        if (selected) {
            painter->save();
            painter->setPen(QPen(option.palette.highlight().color(), 2));
            painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
            painter->restore();
        }
    }
};

QString columnName(const Table::Document &document, int column)
{
    return column < 0 ? QObject::tr("Unmapped")
        : QStringLiteral("%1: %2").arg(column + 1).arg(document.headers.value(column));
}
QString roleName(Table::ColumnRole role)
{
    switch (role) {
    case Table::ColumnRole::Compare: return QObject::tr("Compare");
    case Table::ColumnRole::Key: return QObject::tr("Key + compare");
    case Table::ColumnRole::Ignore: return QObject::tr("Ignored");
    case Table::ColumnRole::Display: return QObject::tr("Not compared");
    }
    return {};
}
bool differs(Table::CellStatus status)
{
    return status == Table::CellStatus::Different || status == Table::CellStatus::LeftOnly
        || status == Table::CellStatus::RightOnly;
}
QString delimiterValue(QComboBox *combo)
{
    if (combo->currentIndex() >= 0 && combo->currentText() == combo->itemText(combo->currentIndex()))
        return combo->currentData().toString();
    return combo->currentText() == QStringLiteral("\\t") ? QStringLiteral("\t") : combo->currentText();
}
void setDelimiter(QComboBox *combo, const QString &value)
{
    const int index = combo->findData(value);
    if (index >= 0) combo->setCurrentIndex(index); else combo->setEditText(value);
}
}

TableGridModel::TableGridModel(TableCompareSession *session, bool left, QObject *parent)
    : QAbstractTableModel(parent), m_session(session), m_left(left) {}
int TableGridModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }
int TableGridModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_session->comparison().columns.size();
}
int TableGridModel::visibleRow(int row) const { return m_rows.indexOf(row); }
int TableGridModel::resultRow(int row) const { return m_rows.value(row, -1); }
void TableGridModel::refresh(bool differencesOnly)
{
    beginResetModel();
    m_rows.clear();
    const auto &result = m_session->comparison();
    for (int i = 0; i < result.rows.size(); ++i)
        if (!differencesOnly || result.rows[i].status != Table::RowStatus::Equal) m_rows.append(i);
    endResetModel();
}
QVariant TableGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= columnCount()) return {};
    const auto &result = m_session->comparison();
    const auto &row = result.rows[m_rows[index.row()]];
    const auto &mapping = result.columns[index.column()];
    const auto &document = m_left ? m_session->leftDocument() : m_session->rightDocument();
    const int sourceRow = m_left ? row.left : row.right;
    const int sourceColumn = m_left ? mapping.left : mapping.right;
    const bool present = sourceRow >= 0 && sourceRow < document.rows.size()
        && sourceColumn >= 0 && sourceColumn < document.rows[sourceRow].size();
    const auto status = row.cells.value(index.column(), Table::CellStatus::NotCompared);
    if (role == Qt::DisplayRole) return present ? document.rows[sourceRow][sourceColumn] : QStringLiteral("∅");
    if (role == Qt::ToolTipRole) {
        QString text = tr("%1 • %2").arg(Table::rowStatusLabel(row.status), roleName(mapping.role));
        if (row.duplicateKey) text += tr(" • Duplicate key; all occurrences retained");
        text += present ? tr("\nSource record %1, column %2\n%3").arg(sourceRow + 1).arg(sourceColumn + 1)
                          .arg(document.rows[sourceRow][sourceColumn]) : tr("\nMissing cell (distinct from empty text)");
        return text;
    }
    if (role == Qt::BackgroundRole) {
        if (!present) return QColor(235, 237, 240);
        if (status == Table::CellStatus::Different) return QColor(255, 225, 188);
        if (status == Table::CellStatus::LeftOnly) return QColor(255, 210, 210);
        if (status == Table::CellStatus::RightOnly) return QColor(210, 239, 216);
        if (status == Table::CellStatus::Ignored || status == Table::CellStatus::NotCompared)
            return QColor(242, 242, 242);
        return QColor(255, 255, 255);
    }
    if (role == Qt::ForegroundRole) {
        if (status == Table::CellStatus::Ignored || status == Table::CellStatus::NotCompared || !present)
            return QColor(100, 100, 100);
        return QColor(25, 25, 25);
    }
    return {};
}
QVariant TableGridModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    const auto &result = m_session->comparison();
    if (orientation == Qt::Vertical) {
        if (section < 0 || section >= m_rows.size()) return {};
        const auto &row = result.rows[m_rows[section]];
        const int sourceRow = m_left ? row.left : row.right;
        if (role == Qt::DisplayRole) return QStringLiteral("%1 · %2%3")
            .arg(sourceRow >= 0 ? QString::number(sourceRow + 1) : QStringLiteral("—"),
                 Table::rowStatusLabel(row.status), row.duplicateKey ? QStringLiteral(" ⚠") : QString());
        return {};
    }
    if (section < 0 || section >= result.columns.size()) return {};
    const auto &column = result.columns[section];
    const auto &document = m_left ? m_session->leftDocument() : m_session->rightDocument();
    if (role == Qt::DisplayRole) return columnName(document, m_left ? column.left : column.right)
        + QStringLiteral("\n") + roleName(column.role) + (column.numeric ? tr(" · Numeric") : QString());
    if (role == Qt::ToolTipRole) {
        int count = 0;
        for (const auto &row : result.rows) if (differs(row.cells.value(section))) ++count;
        return tr("%1 ↔ %2\n%3; %4 different cells\nAbsolute tolerance %5; relative tolerance %6")
            .arg(columnName(m_session->leftDocument(), column.left), columnName(m_session->rightDocument(), column.right),
                 roleName(column.role)).arg(count).arg(column.absoluteTolerance).arg(column.relativeTolerance);
    }
    return {};
}

TableCompareView::TableCompareView(TableCompareSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *sources = new QGridLayout;
    for (int side = 0; side < 2; ++side) {
        sources->addWidget(new QLabel(side == 0 ? tr("Left") : tr("Right"), this), side, 0);
        m_paths[side] = new QLineEdit(this);
        m_paths[side]->setObjectName(side == 0 ? "leftTablePath" : "rightTablePath");
        m_paths[side]->setPlaceholderText(tr("CSV / TSV / delimited text file"));
        sources->addWidget(m_paths[side], side, 1);
        auto *browse = new QPushButton(tr("Browse…"), this);
        sources->addWidget(browse, side, 2);
        connect(browse, &QPushButton::clicked, this, [this, side] { choosePath(side == 0); });
        connect(m_paths[side], &QLineEdit::returnPressed, this, &TableCompareView::loadPaths);
        m_delimiters[side] = new QComboBox(this);
        m_delimiters[side]->setObjectName(side == 0 ? "leftTableDelimiter" : "rightTableDelimiter");
        m_delimiters[side]->setEditable(true);
        m_delimiters[side]->addItem(tr("Auto delimiter"), QString());
        m_delimiters[side]->addItem(tr("Comma ,"), QStringLiteral(","));
        m_delimiters[side]->addItem(tr("Tab \\t"), QStringLiteral("\t"));
        m_delimiters[side]->addItem(tr("Semicolon ;"), QStringLiteral(";"));
        m_delimiters[side]->addItem(tr("Pipe |"), QStringLiteral("|"));
        m_delimiters[side]->setToolTip(tr("Choose a separator or type a literal custom separator."));
        sources->addWidget(m_delimiters[side], side, 3);
        m_encodings[side] = new QComboBox(this);
        m_encodings[side]->setEditable(true);
        m_encodings[side]->addItems({tr("Auto encoding"), QStringLiteral("UTF-8"), QStringLiteral("UTF-16LE"),
                                   QStringLiteral("UTF-16BE"), QStringLiteral("GB18030"), QStringLiteral("ISO-8859-1")});
        sources->addWidget(m_encodings[side], side, 4);
        m_headers[side] = new QCheckBox(tr("First row is header"), this);
        m_headers[side]->setChecked(true);
        sources->addWidget(m_headers[side], side, 5);
    }
    sources->setColumnStretch(1, 1);
    layout->addLayout(sources);
    auto *actions = new QHBoxLayout;
    auto *compare = new QPushButton(tr("Compare paths"), this);
    auto *format = new QPushButton(tr("Apply formats"), this);
    auto *reload = new QPushButton(tr("Reload"), this);
    auto *columns = new QPushButton(tr("Columns / keys / tolerance…"), this);
    columns->setObjectName("tableColumnRules");
    actions->addWidget(compare);
    actions->addWidget(format);
    actions->addWidget(reload);
    actions->addWidget(columns);
    m_mapping = new QComboBox(this);
    m_mapping->setObjectName("tableMappingMode");
    m_mapping->addItems({tr("Columns: header names"), tr("Columns: positions"), tr("Columns: custom")});
    m_mapping->setToolTip(tr("Automatic mapping uses unique header names when both sides have headers; otherwise it uses column positions."));
    actions->addWidget(m_mapping);
    m_alignment = new QComboBox(this);
    m_alignment->setObjectName("tableAlignment");
    m_alignment->addItems({tr("Rows: content"), tr("Rows: positions"), tr("Rows: keys")});
    actions->addWidget(m_alignment);
    actions->addStretch();
    layout->addLayout(actions);
    connect(compare, &QPushButton::clicked, this, &TableCompareView::loadPaths);
    connect(format, &QPushButton::clicked, this, &TableCompareView::applyFormats);
    connect(reload, &QPushButton::clicked, this, [this] {
        QString error;
        if (!m_session->reload(&error)) showError(error); else showError({});
    });
    connect(columns, &QPushButton::clicked, this, &TableCompareView::editColumns);
    connect(m_alignment, QOverload<int>::of(&QComboBox::activated), this, [this](int value) {
        auto options = m_session->comparisonOptions();
        options.alignment = static_cast<Table::RowAlignment>(value);
        QString error;
        if (!m_session->setComparisonOptions(options, &error)) { showError(error); refresh(); }
        else showError({});
    });
    connect(m_mapping, QOverload<int>::of(&QComboBox::activated), this, [this](int value) {
        if (value == 2) { editColumns(); refresh(); return; }
        auto options = m_session->comparisonOptions();
        options.columnMode = static_cast<Table::ColumnMappingMode>(value);
        options.columns.clear();
        options.alignment = Table::RowAlignment::Content;
        QString error;
        if (!m_session->setComparisonOptions(options, &error)) { showError(error); refresh(); }
        else showError({});
    });
    auto *filters = new QHBoxLayout;
    m_differencesOnly = new QCheckBox(tr("Only difference rows"), this);
    m_differencesOnly->setObjectName("differenceOnly");
    m_differenceColumns = new QCheckBox(tr("Only columns with differences"), this);
    m_differenceColumns->setObjectName("differenceColumnsOnly");
    filters->addWidget(m_differencesOnly);
    filters->addWidget(m_differenceColumns);
    auto *previous = new QPushButton(tr("Previous difference"), this);
    auto *next = new QPushButton(tr("Next difference"), this);
    filters->addWidget(previous);
    filters->addWidget(next);
    filters->addStretch();
    layout->addLayout(filters);
    connect(previous, &QPushButton::clicked, session, &TableCompareSession::previousDifference);
    connect(next, &QPushButton::clicked, session, &TableCompareSession::nextDifference);
    connect(m_differencesOnly, &QCheckBox::toggled, this, &TableCompareView::refresh);
    connect(m_differenceColumns, &QCheckBox::toggled, this, &TableCompareView::refresh);
    m_error = new QLabel(this);
    m_error->setObjectName("tableError");
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #b3261e"));
    m_error->hide();
    layout->addWidget(m_error);
    m_warnings = new QLabel(this);
    m_warnings->setObjectName("tableWarnings");
    m_warnings->setTextFormat(Qt::PlainText);
    m_warnings->setWordWrap(true);
    m_warnings->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_warnings);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    for (int side = 0; side < 2; ++side) {
        m_tables[side] = new QTableView(splitter);
        m_tables[side]->setObjectName(side == 0 ? "leftTable" : "rightTable");
        m_models[side] = new TableGridModel(session, side == 0, m_tables[side]);
        m_tables[side]->setModel(m_models[side]);
        m_tables[side]->setItemDelegate(new TableCellDelegate(m_tables[side]));
        m_tables[side]->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tables[side]->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tables[side]->setWordWrap(false);
        m_tables[side]->horizontalHeader()->setDefaultSectionSize(155);
        m_tables[side]->verticalHeader()->setDefaultSectionSize(26);
        m_tables[side]->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_tables[side]->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        connect(m_tables[side], &QTableView::doubleClicked, this,
                [this, side](const QModelIndex &index) { showCell(side == 0, index); });
        connect(m_tables[side], &QTableView::clicked, this, [this, side](const QModelIndex &index) {
            const int resultRow = m_models[side]->resultRow(index.row());
            const int difference = m_session->comparison().differences.indexOf(resultRow);
            if (difference >= 0) m_session->selectDifference(difference);
        });
    }
    for (int side = 0; side < 2; ++side) {
        connect(m_tables[side]->verticalScrollBar(), &QScrollBar::valueChanged,
                m_tables[1 - side]->verticalScrollBar(), &QScrollBar::setValue);
        connect(m_tables[side]->horizontalScrollBar(), &QScrollBar::valueChanged,
                m_tables[1 - side]->horizontalScrollBar(), &QScrollBar::setValue);
    }
    layout->addWidget(splitter, 1);
    m_summary = new QLabel(this);
    m_summary->setObjectName("tableSummary");
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    auto *support = new QLabel(tr("Read only: CSV / TSV / literal separators. XLSX, XLS and HTML tables are not supported. Double-click a cell for full text. ∅ means missing."), this);
    support->setWordWrap(true);
    layout->addWidget(support);
    connect(session, &TableCompareSession::comparisonChanged, this, &TableCompareView::refresh);
    connect(session, &TableCompareSession::pathsChanged, this, [this] {
        m_paths[0]->setText(m_session->leftPath());
        m_paths[1]->setText(m_session->rightPath());
    });
    connect(session, &TableCompareSession::currentDifferenceChanged, this, &TableCompareView::selectDifference);
    m_paths[0]->setText(session->leftPath());
    m_paths[1]->setText(session->rightPath());
    refresh();
}

void TableCompareView::showError(const QString &error)
{
    m_error->setText(error.isEmpty() || m_session->comparison().rows.isEmpty() ? error
        : error + tr(" The previous comparison is still shown."));
    m_error->setVisible(!error.isEmpty());
}
void TableCompareView::refresh()
{
    const auto &result = m_session->comparison();
    QVector<bool> differentColumns(result.columns.size(), false);
    for (const auto &row : result.rows)
        for (int column = 0; column < row.cells.size(); ++column)
            if (differs(row.cells[column])) differentColumns[column] = true;
    for (int side = 0; side < 2; ++side) {
        m_models[side]->refresh(m_differencesOnly->isChecked());
        const auto parse = m_session->parseOptions(side == 0);
        setDelimiter(m_delimiters[side], parse.delimiter);
        m_encodings[side]->setCurrentText(parse.encoding.isEmpty() ? tr("Auto encoding") : QString::fromLatin1(parse.encoding));
        m_headers[side]->setChecked(parse.firstRowHeader);
        for (int column = 0; column < result.columns.size(); ++column) {
            m_tables[side]->setColumnHidden(column, m_differenceColumns->isChecked() && !differentColumns[column]);
        }
    }
    m_alignment->setCurrentIndex(static_cast<int>(m_session->comparisonOptions().alignment));
    m_mapping->setCurrentIndex(static_cast<int>(m_session->comparisonOptions().columnMode));
    m_summary->setText(m_session->statusText());
    QStringList warnings = result.warnings;
    if (m_session->comparisonOptions().columnMode == Table::ColumnMappingMode::AutoName
        && (!m_session->leftDocument().hasHeader || !m_session->rightDocument().hasHeader))
        warnings.append(tr("Automatic mapping uses column positions because a side has no header."));
    warnings.append(m_session->leftDocument().warnings);
    warnings.append(m_session->rightDocument().warnings);
    if (!result.ok()) warnings.prepend(tr("Cannot compare: %1. Adjust the column mapping.").arg(result.error));
    if (m_session->leftDocument().delimiter != m_session->rightDocument().delimiter
        || m_session->leftDocument().encoding != m_session->rightDocument().encoding
        || m_session->leftDocument().hasHeader != m_session->rightDocument().hasHeader)
        warnings.append(tr("The two sides use different parsing formats."));
    QStringList numericRules;
    for (const auto &rule : result.columns)
        if (rule.numeric) numericRules.append(tr("%1: abs %2, rel %3%4")
            .arg(columnName(m_session->leftDocument(), rule.left)).arg(rule.absoluteTolerance)
            .arg(rule.relativeTolerance).arg(rule.compareFormatting ? tr(", compare formatting") : QString()));
    if (!numericRules.isEmpty()) warnings.append(tr("Numeric rules — %1").arg(numericRules.join(QStringLiteral("; "))));
    m_warnings->setText(warnings.join(QStringLiteral("\n")));
    m_warnings->setVisible(!warnings.isEmpty());
    selectDifference(m_session->currentDifference());
}

void TableCompareView::choosePath(bool left)
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose delimited table"), m_paths[left ? 0 : 1]->text(),
        tr("Delimited tables (*.csv *.tsv *.tab *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    m_paths[left ? 0 : 1]->setText(path);
    loadPaths();
}
void TableCompareView::loadPaths()
{
    QString error;
    if (!m_session->setPaths(m_paths[0]->text(), m_paths[1]->text(), &error)
        || (m_session->state() != CompareSession::State::Open && !m_session->open(&error))) showError(error);
    else showError({});
}
void TableCompareView::applyFormats()
{
    Table::ParseOptions options[2];
    for (int side = 0; side < 2; ++side) {
        options[side].delimiter = delimiterValue(m_delimiters[side]);
        options[side].encoding = m_encodings[side]->currentText() == tr("Auto encoding") ? QByteArray()
            : m_encodings[side]->currentText().toLatin1();
        options[side].firstRowHeader = m_headers[side]->isChecked();
    }
    QString error;
    if (!m_session->setParseOptions(options[0], options[1], &error)) showError(error);
    else showError({});
}

void TableCompareView::selectDifference(int index)
{
    const int resultRow = m_session->comparison().differences.value(index, -1);
    if (resultRow < 0) return;
    for (int side = 0; side < 2; ++side) {
        const int row = m_models[side]->visibleRow(resultRow);
        if (row < 0) continue;
        m_tables[side]->selectRow(row);
        m_tables[side]->scrollTo(m_models[side]->index(row, 0));
    }
}

void TableCompareView::showCell(bool left, const QModelIndex &index)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Cell details — read only"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(m_models[left ? 0 : 1]->data(index, Qt::ToolTipRole).toString());
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(640, 360);
    dialog.exec();
}

void TableCompareView::editColumns()
{
    const int leftColumns = m_session->leftDocument().columnCount();
    const int rightColumns = m_session->rightDocument().columnCount();
    if (leftColumns > 256) {
        showError(tr("The column-rule editor supports up to 256 left columns. Use automatic or position mapping for this wider table."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Column mapping and comparison rules"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *explanation = new QLabel(tr("Map each left column to at most one right column. Unmapped columns remain visible as not compared. Key values match exactly; duplicate keys retain all rows. Numeric tolerance uses max(absolute, relative × maximum magnitude). Numbers use a decimal point; invalid numeric text is compared exactly."), &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto *rightNames = new QStandardItemModel(&dialog);
    auto *unmapped = new QStandardItem(tr("Unmapped"));
    unmapped->setData(-1, Qt::UserRole);
    rightNames->appendRow(unmapped);
    for (int column = 0; column < rightColumns; ++column) {
        auto *item = new QStandardItem(columnName(m_session->rightDocument(), column));
        item->setData(column, Qt::UserRole);
        rightNames->appendRow(item);
    }
    auto *grid = new QTableWidget(leftColumns, 7, &dialog);
    grid->setHorizontalHeaderLabels({tr("Left column"), tr("Right column"), tr("Role"), tr("Type"),
                                     tr("Absolute tolerance"), tr("Relative tolerance"), tr("Compare format")});
    for (int row = 0; row < grid->rowCount(); ++row) {
        Table::ColumnRule rule;
        rule.left = row;
        for (const auto &current : m_session->comparison().columns) if (current.left == row) { rule = current; break; }
        auto *name = new QTableWidgetItem(columnName(m_session->leftDocument(), row));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        grid->setItem(row, 0, name);
        auto *right = new QComboBox(grid);
        right->setModel(rightNames);
        right->setCurrentIndex(qMax(0, right->findData(rule.right)));
        grid->setCellWidget(row, 1, right);
        auto *role = new QComboBox(grid);
        role->addItems({tr("Compare"), tr("Key + compare"), tr("Ignore"), tr("Display only")});
        role->setCurrentIndex(static_cast<int>(rule.role));
        grid->setCellWidget(row, 2, role);
        auto *type = new QComboBox(grid);
        type->addItems({tr("Text"), tr("Numeric")});
        type->setCurrentIndex(rule.numeric ? 1 : 0);
        grid->setCellWidget(row, 3, type);
        for (int column = 4; column <= 5; ++column) {
            auto *tolerance = new QLineEdit(grid);
            auto *validator = new QDoubleValidator(0, std::numeric_limits<double>::max(), 17, tolerance);
            validator->setNotation(QDoubleValidator::ScientificNotation);
            validator->setLocale(QLocale::c());
            tolerance->setValidator(validator);
            tolerance->setText(QString::number(column == 4 ? rule.absoluteTolerance : rule.relativeTolerance, 'g', 17));
            tolerance->setPlaceholderText(QStringLiteral("0 or 1e-9"));
            grid->setCellWidget(row, column, tolerance);
        }
        auto *format = new QCheckBox(grid);
        format->setChecked(rule.compareFormatting);
        grid->setCellWidget(row, 6, format);
    }
    grid->resizeColumnsToContents();
    layout->addWidget(grid);
    auto *keyRows = new QCheckBox(tr("Align rows using the selected key columns"), &dialog);
    keyRows->setChecked(m_session->comparisonOptions().alignment == Table::RowAlignment::Key);
    layout->addWidget(keyRows);
    auto *similarityRow = new QHBoxLayout;
    similarityRow->addWidget(new QLabel(tr("Content matching minimum similarity (0–1)"), &dialog));
    auto *similarity = new QDoubleSpinBox(&dialog);
    similarity->setRange(0, 1);
    similarity->setDecimals(2);
    similarity->setSingleStep(0.05);
    similarity->setValue(m_session->comparisonOptions().minimumSimilarity);
    similarityRow->addWidget(similarity);
    similarityRow->addStretch();
    layout->addLayout(similarityRow);
    auto *error = new QLabel(&dialog);
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    error->setStyleSheet(QStringLiteral("color: #b3261e"));
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, grid, keyRows, similarity, error] {
        auto options = m_session->comparisonOptions();
        options.columnMode = Table::ColumnMappingMode::Explicit;
        options.alignment = keyRows->isChecked() ? Table::RowAlignment::Key : Table::RowAlignment::Content;
        options.minimumSimilarity = similarity->value();
        options.columns.clear();
        for (int row = 0; row < grid->rowCount(); ++row) {
            Table::ColumnRule rule;
            rule.left = row;
            rule.right = qobject_cast<QComboBox *>(grid->cellWidget(row, 1))->currentData().toInt();
            rule.role = static_cast<Table::ColumnRole>(qobject_cast<QComboBox *>(grid->cellWidget(row, 2))->currentIndex());
            if (rule.right < 0) rule.role = Table::ColumnRole::Display;
            rule.numeric = qobject_cast<QComboBox *>(grid->cellWidget(row, 3))->currentIndex() == 1;
            bool absoluteOk = false, relativeOk = false;
            rule.absoluteTolerance = qobject_cast<QLineEdit *>(grid->cellWidget(row, 4))->text().toDouble(&absoluteOk);
            rule.relativeTolerance = qobject_cast<QLineEdit *>(grid->cellWidget(row, 5))->text().toDouble(&relativeOk);
            if (!absoluteOk || !relativeOk || !std::isfinite(rule.absoluteTolerance)
                || !std::isfinite(rule.relativeTolerance) || rule.absoluteTolerance < 0 || rule.relativeTolerance < 0) {
                error->setText(tr("Column %1: tolerances must be finite, nonnegative numbers.").arg(row + 1));
                return;
            }
            rule.compareFormatting = qobject_cast<QCheckBox *>(grid->cellWidget(row, 6))->isChecked();
            options.columns.append(rule);
        }
        QString reason;
        if (!m_session->setComparisonOptions(options, &reason)) { error->setText(reason); return; }
        showError({});
        dialog.accept();
    });
    dialog.resize(1040, 460);
    dialog.exec();
}

}
