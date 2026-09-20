#include "mediacompareview.h"
#include "mediacomparesession.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

namespace LqCompare {
namespace {
QString quotedPreview(const QString &value, int maxCharacters)
{
    if (value.isEmpty()) return QObject::tr("(empty)");
    QString result = QStringLiteral("\"");
    const int count = qMin(value.size(), maxCharacters);
    for (int i = 0; i < count; ++i) {
        const QChar ch = value.at(i);
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"')) {
            result += QLatin1Char('\\');
            result += ch;
        } else if (ch == QLatin1Char('\n')) result += QStringLiteral("\\n");
        else if (ch == QLatin1Char('\r')) result += QStringLiteral("\\r");
        else if (ch == QLatin1Char('\t')) result += QStringLiteral("\\t");
        else if (ch.isNull() || ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format)
            result += QStringLiteral("\\u%1").arg(ch.unicode(), 4, 16, QLatin1Char('0'));
        else result += ch;
    }
    if (count < value.size()) result += QStringLiteral("…");
    return result + QLatin1Char('"');
}

QString valuePreview(const QStringList &values)
{
    if (values.isEmpty()) return QObject::tr("(empty field)");
    QStringList parts;
    const int count = qMin(values.size(), 4);
    for (int i = 0; i < count; ++i) parts.append(quotedPreview(values.at(i), 192));
    if (count < values.size()) parts.append(QObject::tr("… %1 more values").arg(values.size() - count));
    return parts.join(QStringLiteral(" | "));
}

QString documentInfo(const Media::Document &document, bool attempted)
{
    if (!attempted) return QObject::tr("Not loaded • File size: unknown");
    const QString size = document.fileSize < 0 ? QObject::tr("unknown")
        : QObject::tr("%1 bytes").arg(QLocale().toString(document.fileSize));
    QString text = QObject::tr("%1 • %2 • File size: %3")
        .arg(document.format.isEmpty() ? QObject::tr("Unrecognized format") : document.format,
             Media::statusName(document.status), size);
    if (!document.message.isEmpty()) text += QLatin1Char('\n') + document.message;
    for (const QString &warning : document.warnings.mid(0, 8))
        text += QLatin1Char('\n') + QObject::tr("Warning: %1").arg(warning);
    if (document.warnings.size() > 8)
        text += QLatin1Char('\n') + QObject::tr("%1 additional warnings").arg(document.warnings.size() - 8);
    return text;
}

void configureLabel(QLabel *label)
{
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
}
}

// A model avoids eagerly creating widgets/items for every potentially long tag.
// The table previews are bounded; the selected row exposes full values below.
class MediaFieldModel : public QAbstractTableModel
{
public:
    explicit MediaFieldModel(QObject *parent) : QAbstractTableModel(parent) {}

    void setComparison(const Media::Document &left, const Media::Document &right,
                       const Media::Comparison &comparison)
    {
        beginResetModel();
        m_left = left;
        m_right = right;
        m_fields = comparison.fields;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_fields.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 5; }
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return index.isValid() ? Qt::ItemIsSelectable | Qt::ItemIsEnabled : Qt::NoItemFlags;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Vertical) return section + 1;
        switch (section) {
        case 0: return tr("Type");
        case 1: return tr("Field");
        case 2: return tr("Left value");
        case 3: return tr("Right value");
        case 4: return tr("Result");
        }
        return {};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_fields.size()) return {};
        const auto &field = m_fields.at(index.row());
        const bool comparable = m_left.usable() && m_right.usable();
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case 0: return field.technical ? tr("Technical") : tr("Tag");
            case 1: return field.key;
            case 2: return cellValue(field, m_left, field.left);
            case 3: return cellValue(field, m_right, field.right);
            case 4:
                if (!comparable) return tr("Not comparable");
                switch (field.difference) {
                case Media::Difference::Equal: return tr("Match");
                case Media::Difference::Changed: return tr("Changed");
                case Media::Difference::LeftOnly: return tr("Only left");
                case Media::Difference::RightOnly: return tr("Only right");
                }
            }
        }
        if (role == Qt::ToolTipRole && (index.column() == 2 || index.column() == 3))
            return tr("Select this row to inspect its full values below. Missing fields, empty values and unavailable reads are distinct.");
        if (role == Qt::BackgroundRole && comparable) {
            switch (field.difference) {
            case Media::Difference::Changed: return QColor(255, 240, 206);
            case Media::Difference::LeftOnly: return QColor(255, 230, 220);
            case Media::Difference::RightOnly: return QColor(222, 239, 255);
            case Media::Difference::Equal: break;
            }
        }
        if (role == Qt::ForegroundRole && comparable && field.difference != Media::Difference::Equal)
            return QColor(35, 35, 35); // Keep the highlighted cells readable in dark palettes too.
        if (role == Qt::FontRole && (index.column() == 2 || index.column() == 3)) {
            const auto &document = index.column() == 2 ? m_left : m_right;
            if (!document.usable() || !hasField(document, field)) {
                QFont font;
                font.setItalic(true);
                return font;
            }
        }
        return {};
    }

    QString fieldTitle(int row) const
    {
        if (row < 0 || row >= m_fields.size()) return tr("Select a field to inspect full values");
        const auto &field = m_fields.at(row);
        return tr("%1: %2").arg(field.technical ? tr("Technical parameter") : tr("Tag"), field.key);
    }

    QString fullValue(int row, bool left) const
    {
        if (row < 0 || row >= m_fields.size()) return {};
        const auto &field = m_fields.at(row);
        const auto &document = left ? m_left : m_right;
        if (!document.usable()) return tr("Unavailable: %1").arg(document.message);
        if (!hasField(document, field)) return tr("(missing field)");
        const QStringList &values = left ? field.left : field.right;
        if (values.isEmpty()) return tr("(empty field: no values)");
        QStringList details;
        for (int i = 0; i < values.size(); ++i)
            details.append(values.at(i).isEmpty() ? tr("Value %1: (empty)").arg(i + 1)
                : tr("Value %1:\n%2").arg(i + 1).arg(values.at(i)));
        return details.join(QStringLiteral("\n\n"));
    }

private:
    static bool hasField(const Media::Document &document, const Media::FieldDifference &field)
    {
        if (field.technical && field.key == QStringLiteral("file_size")) return document.fileSize >= 0;
        if (field.technical && field.key == QStringLiteral("format")) return !document.format.isEmpty();
        return (field.technical ? document.technical : document.tags).contains(field.key);
    }
    QString cellValue(const Media::FieldDifference &field, const Media::Document &document,
                      const QStringList &values) const
    {
        if (!document.usable()) return tr("(unavailable)");
        if (!hasField(document, field)) return tr("(missing)");
        return valuePreview(values);
    }
    Media::Document m_left;
    Media::Document m_right;
    QVector<Media::FieldDifference> m_fields;
};

MediaCompareView::MediaCompareView(MediaCompareSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("mediaCompareView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *paths = new QHBoxLayout;
    m_leftPath = new QLineEdit(this);
    m_leftPath->setObjectName(QStringLiteral("mediaLeftPath"));
    m_leftPath->setPlaceholderText(tr("Choose left media file"));
    m_rightPath = new QLineEdit(this);
    m_rightPath->setObjectName(QStringLiteral("mediaRightPath"));
    m_rightPath->setPlaceholderText(tr("Choose right media file"));
    auto *leftBrowse = new QPushButton(tr("Left…"), this);
    leftBrowse->setObjectName(QStringLiteral("mediaLeftBrowse"));
    auto *rightBrowse = new QPushButton(tr("Right…"), this);
    rightBrowse->setObjectName(QStringLiteral("mediaRightBrowse"));
    m_compare = new QPushButton(tr("Compare"), this);
    m_compare->setObjectName(QStringLiteral("mediaCompare"));
    m_reload = new QPushButton(tr("Reload"), this);
    m_reload->setObjectName(QStringLiteral("mediaReload"));
    paths->addWidget(leftBrowse);
    paths->addWidget(m_leftPath, 1);
    paths->addWidget(rightBrowse);
    paths->addWidget(m_rightPath, 1);
    paths->addWidget(m_compare);
    paths->addWidget(m_reload);
    layout->addLayout(paths);
    auto browse = [this](QLineEdit *field) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose media file"), field->text(),
            tr("Supported metadata (*.mp3 *.MP3 *.flac *.FLAC);;All files (*)"));
        if (!path.isEmpty()) field->setText(path);
    };
    connect(leftBrowse, &QPushButton::clicked, this, [this, browse] { browse(m_leftPath); });
    connect(rightBrowse, &QPushButton::clicked, this, [this, browse] { browse(m_rightPath); });
    connect(m_compare, &QPushButton::clicked, this, &MediaCompareView::openPaths);
    connect(m_reload, &QPushButton::clicked, this, &MediaCompareView::reloadPaths);
    connect(m_leftPath, &QLineEdit::returnPressed, this, &MediaCompareView::openPaths);
    connect(m_rightPath, &QLineEdit::returnPressed, this, &MediaCompareView::openPaths);

    m_ignoreTechnical = new QCheckBox(tr("Ignore technical parameter differences (compare tags only)"), this);
    m_ignoreTechnical->setObjectName(QStringLiteral("mediaIgnoreTechnical"));
    layout->addWidget(m_ignoreTechnical);
    connect(m_ignoreTechnical, &QCheckBox::toggled, this, [this](bool ignore) {
        if (!m_session) return;
        QString error;
        if (!m_session->setIgnoreTechnical(ignore, &error)) m_status->setText(tr("Error: %1").arg(error));
    });

    auto *info = new QHBoxLayout;
    m_leftInfo = new QLabel(this);
    m_leftInfo->setObjectName(QStringLiteral("mediaLeftInfo"));
    m_rightInfo = new QLabel(this);
    m_rightInfo->setObjectName(QStringLiteral("mediaRightInfo"));
    configureLabel(m_leftInfo);
    configureLabel(m_rightInfo);
    info->addWidget(m_leftInfo, 1);
    info->addWidget(m_rightInfo, 1);
    layout->addLayout(info);

    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("mediaFields"));
    m_model = new MediaFieldModel(m_table);
    m_table->setModel(m_model);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 85);
    m_table->setColumnWidth(1, 180);
    m_table->setColumnWidth(4, 125);
    layout->addWidget(m_table, 1);
    m_detailTitle = new QLabel(this);
    m_detailTitle->setObjectName(QStringLiteral("mediaDetailTitle"));
    configureLabel(m_detailTitle);
    layout->addWidget(m_detailTitle);
    auto *details = new QSplitter(Qt::Horizontal, this);
    m_leftDetail = new QPlainTextEdit(details);
    m_leftDetail->setObjectName(QStringLiteral("mediaLeftDetail"));
    m_leftDetail->setAccessibleName(tr("Left full field values"));
    m_rightDetail = new QPlainTextEdit(details);
    m_rightDetail->setObjectName(QStringLiteral("mediaRightDetail"));
    m_rightDetail->setAccessibleName(tr("Right full field values"));
    for (auto *detail : {m_leftDetail, m_rightDetail}) {
        detail->setReadOnly(true);
        detail->setMaximumHeight(140);
        detail->setPlaceholderText(tr("Select a field above"));
    }
    details->setChildrenCollapsible(false);
    layout->addWidget(details);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this] { updateDetail(); });

    auto *semantics = new QLabel(tr("Read-only metadata • Equal tags do not mean equal audio content. Audio content is not decoded or compared."), this);
    semantics->setObjectName(QStringLiteral("mediaSemantics"));
    configureLabel(semantics);
    layout->addWidget(semantics);
    auto *support = new QLabel(Media::supportDescription(), this);
    support->setObjectName(QStringLiteral("mediaSupport"));
    configureLabel(support);
    layout->addWidget(support);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("mediaStatus"));
    configureLabel(m_status);
    layout->addWidget(m_status);

    if (session) {
        connect(session, &MediaCompareSession::comparisonChanged, this, &MediaCompareView::refresh);
        connect(session, &MediaCompareSession::pathsChanged, this, [this] {
            if (!m_session) return;
            m_leftPath->setText(m_session->leftPath());
            m_rightPath->setText(m_session->rightPath());
        });
        connect(session, &CompareSession::statusTextChanged, m_status, &QLabel::setText);
        connect(session, &CompareSession::errorReported, this, [this](const SessionError &error) {
            m_status->setText(tr("Error: %1").arg(error.message));
        });
        connect(session, &CompareSession::stateChanged, this, &MediaCompareView::updateEnabledState);
        connect(session, &QObject::destroyed, this, [this] { setEnabled(false); });
        m_leftPath->setText(session->leftPath());
        m_rightPath->setText(session->rightPath());
    }
    refresh();
}

void MediaCompareView::refresh()
{
    if (!m_session) { setEnabled(false); return; }
    m_model->setComparison(m_session->leftDocument(), m_session->rightDocument(), m_session->comparison());
    m_leftInfo->setText(tr("Left: %1").arg(documentInfo(m_session->leftDocument(), m_session->hasReadAttempt())));
    m_rightInfo->setText(tr("Right: %1").arg(documentInfo(m_session->rightDocument(), m_session->hasReadAttempt())));
    const QSignalBlocker blocker(m_ignoreTechnical);
    m_ignoreTechnical->setChecked(m_session->ignoreTechnical());
    m_status->setText(m_session->statusText());
    updateDetail();
    updateEnabledState();
}

void MediaCompareView::updateEnabledState()
{
    if (!m_session) { setEnabled(false); return; }
    setEnabled(m_session->state() != CompareSession::State::Closed);
    const bool opening = m_session->state() == CompareSession::State::Opening;
    m_compare->setEnabled(!opening);
    // Failed initial reads are retried using open(); reload() requires State::Open.
    m_reload->setEnabled(!opening && m_session->hasReadAttempt());
    m_ignoreTechnical->setEnabled(!opening);
}

void MediaCompareView::updateDetail()
{
    const QModelIndex index = m_table->currentIndex();
    const int row = index.isValid() ? index.row() : -1;
    m_detailTitle->setText(m_model->fieldTitle(row));
    m_leftDetail->setPlainText(m_model->fullValue(row, true));
    m_rightDetail->setPlainText(m_model->fullValue(row, false));
}

void MediaCompareView::openPaths()
{
    if (!m_session) return;
    QString error;
    if (!m_session->setPaths(m_leftPath->text(), m_rightPath->text(), &error)
        || !m_session->open(&error)) m_status->setText(tr("Error: %1").arg(error));
}

void MediaCompareView::reloadPaths()
{
    if (!m_session) return;
    QString error;
    const bool ok = m_session->state() == CompareSession::State::Open
        ? m_session->reload(&error) : m_session->open(&error);
    if (!ok) m_status->setText(tr("Error: %1").arg(error));
}

} // namespace LqCompare
