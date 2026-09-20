#include "picturecompareview.h"
#include "picturecomparesession.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <functional>

namespace LqCompare {

// Paint directly from shared original images. Zooming never creates an enormous
// scaled QImage/QPixmap, and checkerboard painting is limited to the dirty area.
class PictureCanvas : public QWidget {
public:
    PictureCanvas(PictureCompareSession *session, int side, QWidget *parent = nullptr)
        : QWidget(parent), m_session(session), m_side(side)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }
    void setZoom(double zoom)
    {
        m_zoom = zoom;
        const QSize size = m_session ? m_session->comparison().canvasSize : QSize();
        resize(qMax(1, int(std::ceil(size.width() * zoom))), qMax(1, int(std::ceil(size.height() * zoom))));
        update();
    }
    void setMode(int mode) { m_mode = mode; update(); }
    void setOpacity(double opacity) { m_opacity = opacity; update(); }
    void setShowRegions(bool show) { m_showRegions = show; update(); }
    std::function<void(double)> zoomRequested;
    std::function<void(QPoint)> panRequested;
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        const QRect visible = event->rect();
        constexpr int tile = 12;
        for (int y = visible.top() / tile * tile; y <= visible.bottom(); y += tile)
            for (int x = visible.left() / tile * tile; x <= visible.right(); x += tile)
                painter.fillRect(QRect(x, y, tile, tile), ((x / tile + y / tile) & 1) ? QColor(207, 211, 215) : QColor(245, 245, 245));
        if (!m_session) return;
        const QImage &left = m_session->leftDocument().image;
        const QImage &right = m_session->rightDocument().image;
        if (left.isNull() || right.isNull()) return;
        painter.scale(m_zoom, m_zoom);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
        auto draw = [&painter](const QImage &image) { painter.drawImage(QPoint(0, 0), image); };
        if (m_side == 0) draw(left);
        else if (m_side == 1) draw(right);
        else if (m_mode == 2) draw(m_session->comparison().differenceImage);
        else {
            draw(left);
            painter.setOpacity(m_opacity);
            draw(right);
            painter.setOpacity(1.0);
        }
        // The solid edge distinguishes an actually transparent pixel inside the
        // image from a missing pixel outside its extent on the same checkerboard.
        QPen leftPen(QColor(225, 135, 0)); leftPen.setCosmetic(true);
        QPen rightPen(QColor(0, 130, 195)); rightPen.setCosmetic(true);
        rightPen.setStyle(Qt::DashLine);
        painter.setBrush(Qt::NoBrush);
        if (m_side != 1) { painter.setPen(leftPen); painter.drawRect(QRectF(0, 0, left.width(), left.height())); }
        if (m_side != 0) { painter.setPen(rightPen); painter.drawRect(QRectF(0, 0, right.width(), right.height())); }
        if (m_showRegions) {
            const auto &regions = m_session->comparison().regions;
            const int selected = m_session->currentDifference();
            const QRectF visiblePixels(visible.x() / m_zoom, visible.y() / m_zoom,
                                       visible.width() / m_zoom, visible.height() / m_zoom);
            QPen pen(QColor(80, 235, 130)); pen.setCosmetic(true); pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            for (int i = 0; i < regions.size(); ++i)
                if (i != selected && visiblePixels.intersects(regions[i].bounds))
                    painter.drawRect(QRectF(regions[i].bounds));
            if (selected >= 0 && selected < regions.size()) {
                // A black under-stroke keeps the selected box legible on white.
                pen.setColor(Qt::black); pen.setWidth(4); pen.setStyle(Qt::SolidLine);
                painter.setPen(pen); painter.drawRect(QRectF(regions[selected].bounds));
                pen.setColor(QColor(255, 240, 60)); pen.setWidth(2);
                painter.setPen(pen); painter.drawRect(QRectF(regions[selected].bounds));
            }
        }
    }
    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers() & Qt::ControlModifier) {
            if (zoomRequested) zoomRequested(m_zoom * (event->angleDelta().y() > 0 ? 1.25 : 0.8));
            event->accept();
        } else event->ignore();
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton) {
            m_dragging = true;
            m_lastGlobal = event->globalPos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
        } else QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging) { QWidget::mouseMoveEvent(event); return; }
        const QPoint delta = event->globalPos() - m_lastGlobal;
        m_lastGlobal = event->globalPos();
        if (panRequested) panRequested(delta);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton) {
            m_dragging = false;
            unsetCursor();
            event->accept();
        } else QWidget::mouseReleaseEvent(event);
    }
private:
    QPointer<PictureCompareSession> m_session;
    int m_side;
    int m_mode = 1;
    double m_zoom = 1.0;
    double m_opacity = 0.5;
    bool m_dragging = false;
    bool m_showRegions = true;
    QPoint m_lastGlobal;
};

namespace {
QString metadata(const Picture::Document &document)
{
    if (document.image.isNull()) return QObject::tr("No image loaded");
    return QObject::tr("%1 × %2 px • %3 • %4-bit decoded source • Alpha: %5 • DPI: %6 × %7 • %8")
        .arg(document.originalSize.width()).arg(document.originalSize.height())
        .arg(QString::fromLatin1(document.format)).arg(document.sourceDepth)
        .arg(document.hasAlpha ? QObject::tr("yes") : QObject::tr("no"))
        .arg(document.horizontalDpi, 0, 'f', 1).arg(document.verticalDpi, 0, 'f', 1)
        .arg(document.colorSpace);
}

QScrollArea *makeScroll(PictureCanvas *canvas, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidget(canvas);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll->setBackgroundRole(QPalette::Dark);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    return scroll;
}
}

PictureCompareView::PictureCompareView(PictureCompareSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    setObjectName(QStringLiteral("pictureCompareView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *paths = new QHBoxLayout;
    m_leftPath = new QLineEdit(this); m_leftPath->setObjectName(QStringLiteral("pictureLeftPath"));
    m_rightPath = new QLineEdit(this); m_rightPath->setObjectName(QStringLiteral("pictureRightPath"));
    m_leftPath->setPlaceholderText(tr("Choose left image"));
    m_rightPath->setPlaceholderText(tr("Choose right image"));
    auto *leftBrowse = new QPushButton(tr("Left…"), this);
    auto *rightBrowse = new QPushButton(tr("Right…"), this);
    auto *open = new QPushButton(tr("Compare"), this); open->setObjectName(QStringLiteral("pictureOpen"));
    paths->addWidget(leftBrowse); paths->addWidget(m_leftPath, 1);
    paths->addWidget(rightBrowse); paths->addWidget(m_rightPath, 1); paths->addWidget(open);
    layout->addLayout(paths);
    auto browse = [this](QLineEdit *field) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose image"), field->text(), tr("Images (*);;All files (*)"));
        if (!path.isEmpty()) field->setText(path);
    };
    connect(leftBrowse, &QPushButton::clicked, this, [=] { browse(m_leftPath); });
    connect(rightBrowse, &QPushButton::clicked, this, [=] { browse(m_rightPath); });
    connect(open, &QPushButton::clicked, this, &PictureCompareView::openPaths);
    connect(m_leftPath, &QLineEdit::returnPressed, this, &PictureCompareView::openPaths);
    connect(m_rightPath, &QLineEdit::returnPressed, this, &PictureCompareView::openPaths);

    auto *controls = new QHBoxLayout;
    m_mode = new QComboBox(this); m_mode->setObjectName(QStringLiteral("pictureMode"));
    m_mode->addItems({tr("Side by side"), tr("Overlay"), tr("Difference image")});
    controls->addWidget(m_mode);
    m_zoom = new QDoubleSpinBox(this); m_zoom->setObjectName(QStringLiteral("pictureZoom"));
    m_zoom->setRange(1, 1600); m_zoom->setDecimals(1); m_zoom->setSuffix(QStringLiteral("%")); m_zoom->setValue(100);
    controls->addWidget(m_zoom);
    auto *actual = new QPushButton(tr("1:1"), this);
    auto *fit = new QPushButton(tr("Fit"), this);
    controls->addWidget(actual); controls->addWidget(fit);
    controls->addWidget(new QLabel(tr("Right opacity"), this));
    m_opacity = new QSlider(Qt::Horizontal, this); m_opacity->setObjectName(QStringLiteral("pictureOpacity"));
    m_opacity->setRange(0, 100); m_opacity->setValue(50); m_opacity->setMaximumWidth(110); m_opacity->setEnabled(false);
    controls->addWidget(m_opacity);
    controls->addWidget(new QLabel(tr("Tolerance"), this));
    m_tolerance = new QSpinBox(this); m_tolerance->setObjectName(QStringLiteral("pictureTolerance"));
    m_tolerance->setRange(0, 255);
    m_tolerance->setToolTip(tr("A pixel differs when any selected 8-bit channel difference exceeds this value. Missing pixels always differ."));
    controls->addWidget(m_tolerance);
    m_alpha = new QComboBox(this); m_alpha->setObjectName(QStringLiteral("pictureAlpha"));
    m_alpha->addItems({tr("Include alpha"), tr("Ignore alpha"), tr("Alpha only")});
    controls->addWidget(m_alpha); controls->addStretch();
    layout->addLayout(controls);

    auto *navigation = new QHBoxLayout;
    m_firstRegion = new QPushButton(tr("First"), this);
    m_previousRegion = new QPushButton(tr("Previous"), this);
    m_nextRegion = new QPushButton(tr("Next"), this);
    m_lastRegion = new QPushButton(tr("Last"), this);
    m_firstRegion->setObjectName(QStringLiteral("pictureFirstDifference"));
    m_previousRegion->setObjectName(QStringLiteral("picturePreviousDifference"));
    m_nextRegion->setObjectName(QStringLiteral("pictureNextDifference"));
    m_lastRegion->setObjectName(QStringLiteral("pictureLastDifference"));
    for (auto *button : {m_firstRegion, m_previousRegion, m_nextRegion, m_lastRegion}) navigation->addWidget(button);
    m_regions = new QComboBox(this); m_regions->setObjectName(QStringLiteral("pictureRegions"));
    m_regions->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_regions->setMinimumContentsLength(25); m_regions->setMaxVisibleItems(15);
    m_regions->setToolTip(tr("Regions in stable top-to-bottom, left-to-right order. Coordinates are zero-based; dimensions and pixel counts refer to original pixels."));
    navigation->addWidget(m_regions, 1);
    m_connectivity = new QComboBox(this); m_connectivity->setObjectName(QStringLiteral("pictureConnectivity"));
    m_connectivity->addItems({tr("4-neighbor"), tr("8-neighbor")});
    m_connectivity->setToolTip(tr("4-neighbor joins shared edges. 8-neighbor also joins pixels touching at a corner."));
    navigation->addWidget(m_connectivity);
    m_regionBoxes = new QCheckBox(tr("Region boxes"), this);
    m_regionBoxes->setObjectName(QStringLiteral("pictureRegionBoxes")); m_regionBoxes->setChecked(true);
    navigation->addWidget(m_regionBoxes);
    layout->addLayout(navigation);

    auto *info = new QHBoxLayout;
    m_leftInfo = new QLabel(this); m_leftInfo->setObjectName(QStringLiteral("pictureLeftInfo"));
    m_rightInfo = new QLabel(this); m_rightInfo->setObjectName(QStringLiteral("pictureRightInfo"));
    for (QLabel *label : {m_leftInfo, m_rightInfo}) { label->setWordWrap(true); label->setTextInteractionFlags(Qt::TextSelectableByMouse); }
    info->addWidget(m_leftInfo, 1); info->addWidget(m_rightInfo, 1); layout->addLayout(info);
    m_stack = new QStackedWidget(this);
    auto *splitter = new QSplitter(Qt::Horizontal, m_stack);
    m_leftCanvas = new PictureCanvas(session, 0);
    m_rightCanvas = new PictureCanvas(session, 1);
    m_compositeCanvas = new PictureCanvas(session, 2);
    m_leftScroll = makeScroll(m_leftCanvas, splitter);
    m_rightScroll = makeScroll(m_rightCanvas, splitter);
    m_compositeScroll = makeScroll(m_compositeCanvas, m_stack);
    m_leftScroll->setObjectName(QStringLiteral("pictureLeftScroll"));
    m_rightScroll->setObjectName(QStringLiteral("pictureRightScroll"));
    splitter->addWidget(m_leftScroll); splitter->addWidget(m_rightScroll);
    splitter->setChildrenCollapsible(false);
    m_stack->addWidget(splitter); m_stack->addWidget(m_compositeScroll);
    layout->addWidget(m_stack, 1);
    m_legend = new QLabel(this); m_legend->setWordWrap(true); layout->addWidget(m_legend);
    auto *semantics = new QLabel(tr("Read-only • Top-left aligned at original dimensions • Decoded 8-bit RGBA; RGB modes include hidden RGB • No EXIF orientation or color-profile transform • Ctrl+wheel: zoom; middle-drag: pan • Views stay linked"), this);
    semantics->setWordWrap(true); layout->addWidget(semantics);
    m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("pictureStatus"));
    m_status->setWordWrap(true); m_status->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_status);

    for (auto *scroll : {m_leftScroll, m_rightScroll, m_compositeScroll}) {
        connect(scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
            if (m_syncing) return;
            m_syncing = true;
            for (auto *s : {m_leftScroll, m_rightScroll, m_compositeScroll}) s->horizontalScrollBar()->setValue(value);
            m_syncing = false;
        });
        connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
            if (m_syncing) return;
            m_syncing = true;
            for (auto *s : {m_leftScroll, m_rightScroll, m_compositeScroll}) s->verticalScrollBar()->setValue(value);
            m_syncing = false;
        });
    }
    const QList<PictureCanvas *> canvases{m_leftCanvas, m_rightCanvas, m_compositeCanvas};
    const QList<QScrollArea *> scrolls{m_leftScroll, m_rightScroll, m_compositeScroll};
    for (int i = 0; i < canvases.size(); ++i) {
        canvases[i]->zoomRequested = [this](double zoom) { setZoomFactor(zoom); };
        canvases[i]->panRequested = [scroll = scrolls[i]](QPoint delta) {
            scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->value() - delta.x());
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->value() - delta.y());
        };
    }
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
        QScrollArea *before = m_stack->currentIndex() == 0 ? m_leftScroll : m_compositeScroll;
        const QPoint position(before->horizontalScrollBar()->value(), before->verticalScrollBar()->value());
        m_syncing = true;
        m_stack->setCurrentIndex(mode == 0 ? 0 : 1);
        m_compositeCanvas->setMode(mode);
        for (auto *s : {m_leftScroll, m_rightScroll, m_compositeScroll}) {
            s->horizontalScrollBar()->setValue(position.x());
            s->verticalScrollBar()->setValue(position.y());
        }
        m_syncing = false;
        m_opacity->setEnabled(mode == 1);
        m_legend->setText(mode == 2
            ? tr("Difference: black = within tolerance; RGB = absolute channel differences; magenta also shows alpha difference; orange = only left; cyan = only right.")
            : mode == 1 ? tr("Overlay: the right image is composited over the left using its own alpha × Right opacity. Orange solid / cyan dashed outlines show the original extents.")
            : tr("Checkerboard = transparent or missing pixels; orange solid / cyan dashed borders mark each original extent. Pixels outside a border are missing and always count as differences."));
    });
    connect(m_zoom, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double percent) { setZoomFactor(percent / 100.0); });
    connect(actual, &QPushButton::clicked, this, [this] { setZoomFactor(1.0); });
    connect(fit, &QPushButton::clicked, this, &PictureCompareView::fitToWindow);
    connect(m_opacity, &QSlider::valueChanged, this, [this](int value) { m_compositeCanvas->setOpacity(value / 100.0); });
    connect(m_tolerance, QOverload<int>::of(&QSpinBox::valueChanged), this, &PictureCompareView::updateOptions);
    connect(m_alpha, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PictureCompareView::updateOptions);
    connect(m_connectivity, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PictureCompareView::updateOptions);
    connect(m_regions, QOverload<int>::of(&QComboBox::activated), session, &PictureCompareSession::selectDifference);
    connect(m_firstRegion, &QPushButton::clicked, session, &PictureCompareSession::firstDifference);
    connect(m_previousRegion, &QPushButton::clicked, session, &PictureCompareSession::previousDifference);
    connect(m_nextRegion, &QPushButton::clicked, session, &PictureCompareSession::nextDifference);
    connect(m_lastRegion, &QPushButton::clicked, session, &PictureCompareSession::lastDifference);
    connect(m_regionBoxes, &QCheckBox::toggled, this, [this](bool show) {
        for (auto *canvas : {m_leftCanvas, m_rightCanvas, m_compositeCanvas}) canvas->setShowRegions(show);
    });
    connect(session, &PictureCompareSession::comparisonChanged, this, &PictureCompareView::refresh);
    connect(session, &PictureCompareSession::currentDifferenceChanged, this, &PictureCompareView::focusDifference);
    connect(session, &PictureCompareSession::pathsChanged, this, [this] {
        if (!m_session) return;
        m_leftPath->setText(m_session->leftPath()); m_rightPath->setText(m_session->rightPath());
    });
    connect(session, &CompareSession::statusTextChanged, m_status, &QLabel::setText);
    connect(session, &CompareSession::errorReported, this, [this](const SessionError &error) { m_status->setText(tr("Error: %1").arg(error.message)); });
    connect(session, &QObject::destroyed, this, [this] { setEnabled(false); });
    m_leftPath->setText(session->leftPath()); m_rightPath->setText(session->rightPath());
    m_legend->setText(tr("Checkerboard = transparent or missing pixels; orange solid / cyan dashed borders mark each original extent. Pixels outside a border are missing and always count as differences."));
    refresh();
}

double PictureCompareView::zoomFactor() const { return m_zoom->value() / 100.0; }

void PictureCompareView::setZoomFactor(double zoom)
{
    const QSignalBlocker blocker(m_zoom);
    m_zoom->setValue(qBound(0.01, zoom, 16.0) * 100.0);
    m_syncing = true;
    for (auto *canvas : {m_leftCanvas, m_rightCanvas, m_compositeCanvas}) canvas->setZoom(zoomFactor());
    m_syncing = false;
    const auto *source = m_stack->currentIndex() == 0 ? m_leftScroll : m_compositeScroll;
    const int x = source->horizontalScrollBar()->value(), y = source->verticalScrollBar()->value();
    for (auto *scroll : {m_leftScroll, m_rightScroll, m_compositeScroll}) {
        scroll->horizontalScrollBar()->setValue(x); scroll->verticalScrollBar()->setValue(y);
    }
}

void PictureCompareView::fitToWindow()
{
    if (!m_session || m_session->comparison().canvasSize.isEmpty()) return;
    const QSize size = m_session->comparison().canvasSize;
    const QSize available = (m_stack->currentIndex() == 0 ? m_leftScroll : m_compositeScroll)->viewport()->size();
    setZoomFactor(qMin(double(available.width()) / size.width(), double(available.height()) / size.height()));
}

void PictureCompareView::refresh()
{
    if (!m_session) return;
    m_leftInfo->setText(tr("Left: %1").arg(metadata(m_session->leftDocument())));
    m_rightInfo->setText(tr("Right: %1").arg(metadata(m_session->rightDocument())));
    const QSignalBlocker toleranceBlocker(m_tolerance), alphaBlocker(m_alpha), connectivityBlocker(m_connectivity);
    m_tolerance->setValue(m_session->comparisonOptions().channelTolerance);
    m_alpha->setCurrentIndex(static_cast<int>(m_session->comparisonOptions().alphaMode));
    m_connectivity->setCurrentIndex(m_session->comparisonOptions().connectivity == Picture::Connectivity::Eight ? 1 : 0);
    const QSignalBlocker regionBlocker(m_regions);
    m_regions->clear();
    const auto &regions = m_session->comparison().regions;
    for (int i = 0; i < regions.size(); ++i) {
        const auto &region = regions[i];
        m_regions->addItem(tr("%1: (%2, %3), %4 × %5 — %6 pixels, max Δ %7")
            .arg(i + 1).arg(region.bounds.x()).arg(region.bounds.y())
            .arg(region.bounds.width()).arg(region.bounds.height()).arg(region.pixelCount).arg(region.maximumChannelDifference));
    }
    updateNavigation(m_session->currentDifference());
    m_status->setText(m_session->statusText());
    setZoomFactor(zoomFactor());
}

void PictureCompareView::openPaths()
{
    if (!m_session) return;
    QString error;
    if (!m_session->setPaths(m_leftPath->text(), m_rightPath->text(), &error)
        || !m_session->open(&error)) m_status->setText(tr("Error: %1").arg(error));
    else fitToWindow();
}

void PictureCompareView::updateOptions()
{
    if (!m_session) return;
    Picture::CompareOptions options;
    options.channelTolerance = m_tolerance->value();
    options.alphaMode = static_cast<Picture::AlphaMode>(m_alpha->currentIndex());
    options.connectivity = m_connectivity->currentIndex() == 1 ? Picture::Connectivity::Eight : Picture::Connectivity::Four;
    QString error;
    if (!m_session->setComparisonOptions(options, &error)) m_status->setText(tr("Error: %1").arg(error));
}

void PictureCompareView::updateNavigation(int index)
{
    const int count = m_session ? m_session->comparison().regions.size() : 0;
    const QSignalBlocker blocker(m_regions);
    m_regions->setCurrentIndex(index);
    m_regions->setEnabled(count > 0);
    m_firstRegion->setEnabled(index > 0);
    m_previousRegion->setEnabled(index > 0);
    m_nextRegion->setEnabled(index >= 0 && index + 1 < count);
    m_lastRegion->setEnabled(index >= 0 && index + 1 < count);
    for (auto *canvas : {m_leftCanvas, m_rightCanvas, m_compositeCanvas}) canvas->update();
}

void PictureCompareView::focusDifference(int index)
{
    updateNavigation(index);
    if (!m_session || index < 0 || index >= m_session->comparison().regions.size()) return;
    const QRect bounds = m_session->comparison().regions[index].bounds;
    const bool paired = m_stack->currentIndex() == 0;
    QScrollArea *source = paired ? m_leftScroll : m_compositeScroll;
    QSize available = source->viewport()->size();
    if (paired) available = available.boundedTo(m_rightScroll->viewport()->size());
    if (available.width() <= 20 || available.height() <= 20) return;
    const double fit = qMin(double(available.width() - 16) / bounds.width(),
                            double(available.height() - 16) / bounds.height());
    // Preserve zoom when the region already fits; otherwise show its full bounds.
    if (zoomFactor() > fit) setZoomFactor(std::floor(fit * 1000.0) / 1000.0);
    const double zoom = zoomFactor();
    int x = qMax(0, int((bounds.x() + bounds.width() / 2.0) * zoom - available.width() / 2.0));
    int y = qMax(0, int((bounds.y() + bounds.height() / 2.0) * zoom - available.height() / 2.0));
    int maxX = source->horizontalScrollBar()->maximum(), maxY = source->verticalScrollBar()->maximum();
    if (paired) {
        maxX = qMin(maxX, m_rightScroll->horizontalScrollBar()->maximum());
        maxY = qMin(maxY, m_rightScroll->verticalScrollBar()->maximum());
    }
    x = qMin(x, maxX); y = qMin(y, maxY);
    m_syncing = true;
    for (auto *scroll : {m_leftScroll, m_rightScroll, m_compositeScroll}) {
        scroll->horizontalScrollBar()->setValue(x); scroll->verticalScrollBar()->setValue(y);
    }
    m_syncing = false;
}

} // namespace LqCompare
