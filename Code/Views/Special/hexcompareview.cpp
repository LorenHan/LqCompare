#include "hexcompareview.h"
#include "hexcomparesession.h"
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace LqCompare {
// Only the visible rows are read and painted. Scrollbar values are rows, never
// bytes, and remain in int range under Comparison's 512 MiB input bound.
class HexPane : public QAbstractScrollArea {
public:
    HexPane(HexCompareSession *session, bool left, QWidget *parent)
        : QAbstractScrollArea(parent), m_session(session), m_left(left)
    {
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setFocusPolicy(Qt::StrongFocus);
        setObjectName(left ? QStringLiteral("hexLeftPane") : QStringLiteral("hexRightPane"));
        connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), QOverload<>::of(&QWidget::update));
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), QOverload<>::of(&QWidget::update));
        connect(session, &HexCompareSession::comparisonChanged, this, [this] { reset(); });
        connect(session, &HexCompareSession::bytesPerRowChanged, this, [this] { reset(); reveal(); });
        connect(session, &HexCompareSession::currentOffsetChanged, this, [this] { reveal(); });
        connect(session, &HexCompareSession::searchChanged, viewport(), QOverload<>::of(&QWidget::update));
        reset();
    }
    void reset()
    {
        if (!m_session) return;
        const qint64 rows = (m_session->comparison().extent() + widthBytes() - 1) / widthBytes();
        const int visible = qMax(1, viewport()->height() / rowHeight());
        verticalScrollBar()->setPageStep(visible);
        verticalScrollBar()->setRange(0, int(qMax<qint64>(0, rows - visible)));
        horizontalScrollBar()->setPageStep(viewport()->width());
        horizontalScrollBar()->setRange(0, qMax(0, (17 + widthBytes() * 4) * charWidth() - viewport()->width()));
        viewport()->update();
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QAbstractScrollArea::resizeEvent(event); reset(); reveal(); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), palette().base());
        painter.setPen(palette().text().color());
        if (!m_session) return;
        const auto &comparison = m_session->comparison();
        if (!comparison.isLoaded()) {
            painter.setPen(palette().text().color());
            painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12), Qt::AlignTop | Qt::TextWordWrap,
                tr("Choose a left and right binary file, then Compare. Files are opened read-only."));
            return;
        }
        if (comparison.extent() == 0) {
            painter.drawText(12, rowHeight(), tr("Empty file (0 bytes)"));
            return;
        }
        const int count = qMin(8192, (viewport()->height() / rowHeight() + 2) * widthBytes());
        const qint64 start = qint64(verticalScrollBar()->value()) * widthBytes();
        QString error;
        const QByteArray bytes = start < comparison.size(m_left)
            ? comparison.read(m_left, start, count, &error) : QByteArray();
        if (!error.isEmpty()) {
            painter.setPen(Qt::red);
            painter.drawText(viewport()->rect(), Qt::TextWordWrap, error);
            return;
        }
        const int cw = charWidth(), rh = rowHeight();
        const int xShift = horizontalScrollBar()->value();
        const int hexStart = 11 * cw, asciiStart = (12 + widthBytes() * 3) * cw;
        for (int row = 0; row * widthBytes() < count; ++row) {
            const qint64 base = start + row * widthBytes();
            if (base >= comparison.extent()) break;
            const int top = row * rh, baseline = top + fontMetrics().ascent() + 3;
            painter.setPen(palette().mid().color());
            painter.drawText(cw - xShift, baseline, QStringLiteral("%1").arg(base, 8, 16, QLatin1Char('0')).toUpper());
            for (int col = 0; col < widthBytes(); ++col) {
                const qint64 pos = base + col;
                if (pos >= comparison.extent()) break;
                const int byteIndex = row * widthBytes() + col;
                const bool exists = pos < comparison.size(m_left);
                const bool selected = pos == m_session->currentOffset();
                const QRect hexRect(hexStart + col * 3 * cw - xShift, top, 2 * cw + 1, rh);
                const QRect asciiRect(asciiStart + col * cw - xShift, top, cw, rh);
                if (comparison.differs(pos)) {
                    const QColor bg = exists ? QColor(255, 222, 165) : QColor(245, 195, 195);
                    painter.fillRect(hexRect, bg); painter.fillRect(asciiRect, bg);
                }
                painter.setPen(comparison.differs(pos) ? QColor(60, 35, 15) :
                    exists ? palette().text().color() : palette().mid().color());
                const uchar byte = exists && byteIndex < bytes.size() ? uchar(bytes.at(byteIndex)) : 0;
                painter.drawText(hexRect.left(), baseline, exists ? QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper() : QStringLiteral("--"));
                painter.drawText(asciiRect.left(), baseline, exists ? QString(QChar(byte >= 32 && byte <= 126 ? byte : '.')) : QStringLiteral("·"));
                if (m_session->isSearchMatch(m_left, pos)) {
                    painter.setPen(QPen(QColor(15, 130, 75), 2));
                    painter.drawLine(hexRect.bottomLeft(), hexRect.bottomRight());
                    painter.drawLine(asciiRect.bottomLeft(), asciiRect.bottomRight());
                }
                if (selected) {
                    painter.setPen(QPen(QColor(45, 105, 190), 2));
                    painter.drawRect(hexRect.adjusted(0, 1, -1, -1));
                    painter.drawRect(asciiRect.adjusted(0, 1, -1, -1));
                }
            }
            if (comparison.size(m_left) >= base && comparison.size(m_left) < base + widthBytes()) {
                painter.setPen(QColor(170, 45, 45));
                painter.drawText(asciiStart + widthBytes() * cw + cw - xShift, baseline, tr("EOF"));
            }
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_session) return;
        setFocus();
        m_session->setSearchLeftSide(m_left);
        const int x = event->pos().x() + horizontalScrollBar()->value();
        const int cw = charWidth(), asciiStart = (12 + widthBytes() * 3) * cw;
        int col = -1;
        if (x >= 11 * cw && x < (11 + widthBytes() * 3) * cw) col = (x - 11 * cw) / (3 * cw);
        else if (x >= asciiStart && x < asciiStart + widthBytes() * cw) col = (x - asciiStart) / cw;
        if (col >= 0 && col < widthBytes())
            m_session->jumpToOffset((qint64(verticalScrollBar()->value()) + event->pos().y() / rowHeight()) * widthBytes() + col);
        QAbstractScrollArea::mousePressEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (!m_session) return;
        qint64 offset = m_session->currentOffset();
        switch (event->key()) {
        case Qt::Key_Left: --offset; break;
        case Qt::Key_Right: ++offset; break;
        case Qt::Key_Up: offset -= widthBytes(); break;
        case Qt::Key_Down: offset += widthBytes(); break;
        case Qt::Key_Home: offset = event->modifiers().testFlag(Qt::ControlModifier) ? 0 : offset / widthBytes() * widthBytes(); break;
        case Qt::Key_End: offset = event->modifiers().testFlag(Qt::ControlModifier) ? m_session->comparison().extent() - 1 :
            qMin(m_session->comparison().extent() - 1, (offset / widthBytes() + 1) * widthBytes() - 1); break;
        default: QAbstractScrollArea::keyPressEvent(event); return;
        }
        m_session->jumpToOffset(offset);
        event->accept();
    }
private:
    int charWidth() const { return qMax(1, fontMetrics().horizontalAdvance(QLatin1Char('0'))); }
    int rowHeight() const { return fontMetrics().height() + 6; }
    int widthBytes() const { return m_session ? m_session->bytesPerRow() : 16; }
    void reveal()
    {
        if (!m_session) return;
        const int row = int(m_session->currentOffset() / widthBytes());
        const int first = verticalScrollBar()->value();
        const int visible = qMax(1, viewport()->height() / rowHeight());
        if (row < first) verticalScrollBar()->setValue(row);
        else if (row >= first + visible) verticalScrollBar()->setValue(row - visible + 1);
        const int byteLeft = (11 + int(m_session->currentOffset() % widthBytes()) * 3) * charWidth();
        const int byteRight = byteLeft + 2 * charWidth() + 1;
        const int horizontal = horizontalScrollBar()->value();
        if (byteLeft < horizontal) horizontalScrollBar()->setValue(byteLeft);
        else if (byteRight > horizontal + viewport()->width())
            horizontalScrollBar()->setValue(byteRight - viewport()->width());
        viewport()->update();
    }
    QPointer<HexCompareSession> m_session;
    bool m_left;
};

HexCompareView::HexCompareView(HexCompareSession *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("hexCompareView"));
    connect(session, &QObject::destroyed, this, [this] { setEnabled(false); update(); });
    auto *layout = new QVBoxLayout(this);
    m_leftPath = new QLineEdit(session->leftPath(), this);
    m_rightPath = new QLineEdit(session->rightPath(), this);
    m_leftPath->setObjectName(QStringLiteral("hexLeftPath"));
    m_rightPath->setObjectName(QStringLiteral("hexRightPath"));
    const auto pathRow = [this, layout](const QString &label, QLineEdit *edit) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this)); row->addWidget(edit, 1);
        auto *browse = new QPushButton(tr("Browse…"), this); row->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString file = QFileDialog::getOpenFileName(this, tr("Choose binary file"), edit->text());
            if (!file.isEmpty()) edit->setText(file);
        });
        layout->addLayout(row);
    };
    pathRow(tr("Left"), m_leftPath); pathRow(tr("Right"), m_rightPath);
    auto *toolbar = new QHBoxLayout;
    auto *compare = new QPushButton(tr("Compare"), this);
    compare->setObjectName(QStringLiteral("hexCompareButton"));
    toolbar->addWidget(compare);
    connect(compare, &QPushButton::clicked, this, &HexCompareView::comparePaths);
    const auto button = [this, toolbar, session](const QString &text, void (HexCompareSession::*slot)()) {
        auto *b = new QPushButton(text, this); toolbar->addWidget(b); connect(b, &QPushButton::clicked, session, slot);
    };
    button(tr("First Δ"), &HexCompareSession::firstDifference);
    button(tr("Previous Δ"), &HexCompareSession::previousDifference);
    button(tr("Next Δ"), &HexCompareSession::nextDifference);
    button(tr("Last Δ"), &HexCompareSession::lastDifference);
    auto *width = new QComboBox(this);
    width->setObjectName(QStringLiteral("hexBytesPerRow"));
    for (int n : {8, 16, 32, 64}) width->addItem(tr("%1 bytes/row").arg(n), n);
    width->setCurrentIndex(width->findData(session->bytesPerRow()));
    toolbar->addWidget(width);
    connect(width, QOverload<int>::of(&QComboBox::currentIndexChanged), session, [session, width](int) { session->setBytesPerRow(width->currentData().toInt()); });
    connect(session, &HexCompareSession::bytesPerRowChanged, this, [width](int n) { width->setCurrentIndex(width->findData(n)); });
    toolbar->addStretch(); layout->addLayout(toolbar);
    auto *navigation = new QHBoxLayout;
    auto *head = new QPushButton(tr("File start"), this);
    auto *tail = new QPushButton(tr("File end"), this);
    navigation->addWidget(head); navigation->addWidget(tail);
    connect(head, &QPushButton::clicked, session, &HexCompareSession::firstByte);
    connect(tail, &QPushButton::clicked, session, &HexCompareSession::lastByte);
    navigation->addWidget(new QLabel(tr("Offset (decimal or 0x):"), this));
    m_offset = new QLineEdit(QStringLiteral("0"), this); m_offset->setMaximumWidth(200);
    m_offset->setObjectName(QStringLiteral("hexOffsetInput")); navigation->addWidget(m_offset);
    auto *go = new QPushButton(tr("Go"), this); navigation->addWidget(go);
    auto *address = new QLabel(this); address->setObjectName(QStringLiteral("hexAddress"));
    navigation->addWidget(address); navigation->addStretch(); layout->addLayout(navigation);
    m_error = new QLabel(this); m_error->setObjectName(QStringLiteral("hexError")); m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #b42318")); m_error->hide(); layout->addWidget(m_error);
    const auto jump = [this] {
        if (!m_session) return;
        QString error;
        m_session->jumpToOffset(m_offset->text(), &error);
        m_error->setText(error); m_error->setVisible(!error.isEmpty());
    };
    connect(go, &QPushButton::clicked, this, jump); connect(m_offset, &QLineEdit::returnPressed, this, jump);
    auto *searchRow = new QHBoxLayout;
    auto *searchKind = new QComboBox(this); searchKind->setObjectName(QStringLiteral("hexSearchKind"));
    searchKind->addItems({tr("Text (UTF-8)"), tr("Hex bytes")}); searchRow->addWidget(searchKind);
    auto *query = new QComboBox(this); query->setObjectName(QStringLiteral("hexSearchQuery"));
    query->setEditable(true); query->setInsertPolicy(QComboBox::NoInsert); query->setMaxCount(10);
    query->addItems(session->sessionSettings()->value(QStringLiteral("hex.searchHistory")).toStringList());
    query->setCurrentText(QString()); query->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    query->lineEdit()->setPlaceholderText(tr("Text or hex pairs, e.g. 48 65 6C 6C 6F"));
    searchRow->addWidget(query, 1);
    auto *side = new QComboBox(this); side->setObjectName(QStringLiteral("hexSearchSide"));
    side->addItems({tr("Left"), tr("Right")}); searchRow->addWidget(side);
    auto *findPrevious = new QPushButton(tr("Find previous"), this);
    auto *findNext = new QPushButton(tr("Find next"), this);
    auto *cancel = new QPushButton(tr("Cancel search"), this); cancel->setEnabled(false);
    findPrevious->setObjectName(QStringLiteral("hexFindPrevious")); findNext->setObjectName(QStringLiteral("hexFindNext"));
    cancel->setObjectName(QStringLiteral("hexCancelSearch"));
    searchRow->addWidget(findPrevious); searchRow->addWidget(findNext); searchRow->addWidget(cancel);
    layout->addLayout(searchRow);
    auto *searchOptions = new QHBoxLayout;
    auto *matchCase = new QCheckBox(tr("Match case (ASCII)"), this); matchCase->setObjectName(QStringLiteral("hexSearchCase"));
    auto *wholeWord = new QCheckBox(tr("Whole ASCII word"), this); wholeWord->setObjectName(QStringLiteral("hexSearchWholeWord"));
    auto *wrap = new QCheckBox(tr("Wrap once"), this); wrap->setChecked(true);
    wrap->setObjectName(QStringLiteral("hexSearchWrap"));
    auto *quota = new QComboBox(this); quota->setObjectName(QStringLiteral("hexSearchQuota"));
    for (int mb : {64, 128, 512}) quota->addItem(tr("%1 MiB scan limit").arg(mb), mb);
    auto *seconds = new QSpinBox(this); seconds->setObjectName(QStringLiteral("hexSearchSeconds"));
    seconds->setRange(1, 60); seconds->setValue(5); seconds->setSuffix(tr(" s limit"));
    searchOptions->addWidget(matchCase); searchOptions->addWidget(wholeWord); searchOptions->addWidget(wrap);
    searchOptions->addWidget(quota); searchOptions->addWidget(seconds); searchOptions->addStretch();
    layout->addLayout(searchOptions);
    auto *searchStatus = new QLabel(this); searchStatus->setObjectName(QStringLiteral("hexSearchStatus"));
    searchStatus->setWordWrap(true); layout->addWidget(searchStatus);
    query->setToolTip(tr("Text is encoded as UTF-8. Case-insensitive matching folds ASCII A-Z only. Whole-word boundaries use ASCII letters, digits and underscore. Hex mode requires exact byte pairs; wildcards are not supported."));
    connect(side, QOverload<int>::of(&QComboBox::currentIndexChanged), session, [session](int index) { session->setSearchLeftSide(index == 0); });
    connect(searchKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [matchCase, wholeWord](int index) {
        matchCase->setEnabled(index == 0); wholeWord->setEnabled(index == 0);
    });
    const auto runSearch = [this, query, searchKind, side, matchCase, wholeWord, wrap, quota, seconds, searchStatus](bool backward) {
        if (!m_session) return;
        Hex::SearchPattern pattern;
        QString error;
        const QString text = query->currentText();
        const bool valid = searchKind->currentIndex() == 0
            ? Hex::textPattern(text, matchCase->isChecked(), wholeWord->isChecked(), &pattern, &error)
            : Hex::parseHexPattern(text, &pattern, &error);
        if (!valid) { searchStatus->setText(error); return; }
        Hex::SearchOptions options;
        options.left = side->currentIndex() == 0; options.backward = backward;
        options.wrap = wrap->isChecked(); options.startOffset = m_session->currentOffset();
        options.maximumScanBytes = qint64(quota->currentData().toInt()) * 1024 * 1024;
        options.maximumElapsedMs = seconds->value() * 1000;
        const QString signature = QStringLiteral("%1/%2/%3/%4/%5/%6").arg(searchKind->currentIndex()).arg(side->currentIndex())
            .arg(matchCase->isChecked()).arg(wholeWord->isChecked()).arg(wrap->isChecked()).arg(text);
        if (signature == m_lastSearchSignature && m_session->searchResult().state == Hex::SearchState::Found &&
            m_session->currentOffset() == m_session->searchResult().offset)
            options.startOffset += backward ? -1 : 1;
        if (!m_session->startSearch(pattern, options, &error)) { searchStatus->setText(error); return; }
        m_lastSearchSignature = signature;
        QStringList history = m_session->sessionSettings()->value(QStringLiteral("hex.searchHistory")).toStringList();
        history.removeAll(text); history.prepend(text); while (history.size() > 10) history.removeLast();
        m_session->sessionSettings()->setValue(QStringLiteral("hex.searchHistory"), history);
        const QSignalBlocker blocker(query);
        query->clear(); query->addItems(history); query->setCurrentText(text);
    };
    connect(findNext, &QPushButton::clicked, this, [runSearch] { runSearch(false); });
    connect(findPrevious, &QPushButton::clicked, this, [runSearch] { runSearch(true); });
    connect(query->lineEdit(), &QLineEdit::returnPressed, this, [runSearch] { runSearch(false); });
    connect(cancel, &QPushButton::clicked, session, &HexCompareSession::cancelSearch);
    connect(session, &HexCompareSession::findRequested, this, [query, searchKind, side, session] {
        searchKind->setCurrentIndex(0); side->setCurrentIndex(session->searchLeftSide() ? 0 : 1);
        query->setFocus(Qt::ShortcutFocusReason); query->lineEdit()->selectAll();
    });
    connect(session, &HexCompareSession::searchChanged, this, [this, cancel, searchStatus] {
        if (!m_session) return;
        cancel->setEnabled(m_session->isSearching());
        const auto &result = m_session->searchResult();
        if (m_session->isSearching()) searchStatus->setText(tr("Searching… %1 bytes scanned; green underline marks a result.").arg(result.scannedBytes));
        else if (result.state == Hex::SearchState::Found)
            searchStatus->setText(tr("Match: 0x%1 (%2), %3 bytes%4 • Green underline")
                .arg(QString::number(result.offset, 16).toUpper()).arg(result.offset).arg(result.length)
                .arg(result.wrapped ? tr(" • Wrapped") : QString()));
        else searchStatus->setText(result.message);
    });
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, session, QOverload<>::of(&HexCompareSession::findText));
    auto *repeatNext = new QShortcut(QKeySequence(Qt::Key_F3), this);
    connect(repeatNext, &QShortcut::activated, session, &HexCompareSession::findNext);
    auto *repeatPrevious = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(repeatPrevious, &QShortcut::activated, session, &HexCompareSession::findPrevious);
    auto *note = new QLabel(tr("Read-only • Absolute offsets: inserted/deleted bytes shift all following comparisons • Snapshots use temporary disk space (up to 2 GiB during reload) • 512 MiB/file limit"), this);
    note->setWordWrap(true); layout->addWidget(note);
    auto *splitter = new QSplitter(this);
    // 捕获表里没有 this：这个 lambda 只用到 splitter/session 与自己的形参，
    // 里面的 tr() 是 QObject 的静态成员，经由外层成员函数的类作用域就能查到，
    // 不需要对象指针。留着 this 会触发 -Wunused-lambda-capture，也掩盖了
    // 「这里没有访问 m_left/m_right 等成员」这个事实。
    const auto panel = [splitter, session](bool left, HexPane **pane) {
        auto *container = new QWidget(splitter);
        auto *box = new QVBoxLayout(container); box->setContentsMargins(0, 0, 0, 0);
        box->addWidget(new QLabel(left ? tr("LEFT — Offset / Hex / ASCII") : tr("RIGHT — Offset / Hex / ASCII"), container));
        *pane = new HexPane(session, left, container); box->addWidget(*pane, 1);
        splitter->addWidget(container);
    };
    panel(true, &m_left); panel(false, &m_right); layout->addWidget(splitter, 1);
    connect(m_left->verticalScrollBar(), &QScrollBar::valueChanged, m_right->verticalScrollBar(), &QScrollBar::setValue);
    connect(m_right->verticalScrollBar(), &QScrollBar::valueChanged, m_left->verticalScrollBar(), &QScrollBar::setValue);
    auto *summary = new QLabel(session->statusText(), this); summary->setWordWrap(true); layout->addWidget(summary);
    connect(session, &CompareSession::statusTextChanged, summary, &QLabel::setText);
    connect(session, &HexCompareSession::pathsChanged, this, [this] { m_leftPath->setText(m_session->leftPath()); m_rightPath->setText(m_session->rightPath()); });
    const auto updateAddress = [this, address](qint64 offset) {
        address->setText(tr("0x%1 = %2 bytes").arg(QString::number(offset, 16).toUpper()).arg(offset));
        m_offset->setText(QStringLiteral("0x%1").arg(QString::number(offset, 16).toUpper()));
    };
    connect(session, &HexCompareSession::currentOffsetChanged, this, updateAddress); updateAddress(session->currentOffset());
    auto *next = new QShortcut(QKeySequence(Qt::Key_F7), this);
    connect(next, &QShortcut::activated, session, &HexCompareSession::nextDifference);
    auto *previous = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7), this);
    connect(previous, &QShortcut::activated, session, &HexCompareSession::previousDifference);
}
void HexCompareView::comparePaths()
{
    if (!m_session) return;
    QString error;
    if (m_session->setPaths(m_leftPath->text(), m_rightPath->text(), &error) && m_session->state() != CompareSession::State::Open)
        m_session->open(&error);
    m_error->setText(error); m_error->setVisible(!error.isEmpty());
}
}
