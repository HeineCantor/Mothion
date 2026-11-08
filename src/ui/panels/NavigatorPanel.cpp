#include "ui/panels/NavigatorPanel.h"

#include "ui/Checkerboard.h"

#include "canvas/CanvasView.h"
#include "ui/IconFactory.h"

#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

using canvas::CanvasView;
using canvas::ViewTransform;

namespace {

// The zoom slider is logarithmic: equal travel for each doubling.
constexpr int kZoomSteps = 1000;

int zoomToSlider(double zoom)
{
    const double t = std::log(zoom / ViewTransform::kMinZoom)
                     / std::log(ViewTransform::kMaxZoom / ViewTransform::kMinZoom);
    return int(std::lround(t * kZoomSteps));
}

double sliderToZoom(int value)
{
    return ViewTransform::kMinZoom
           * std::pow(ViewTransform::kMaxZoom / ViewTransform::kMinZoom, double(value) / kZoomSteps);
}

QToolButton *makeButton(IconId id, const QString &tip, QWidget *parent, bool checkable = false)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setIconSize(QSize(16, 16));
    b->setAutoRaise(true);
    b->setCheckable(checkable);
    b->setToolTip(tip);
    return b;
}

} // namespace

// --- NavigatorPreview -----------------------------------------------------------

NavigatorPreview::NavigatorPreview(CanvasView *canvas, QWidget *parent)
    : QWidget(parent)
    , m_canvas(canvas)
{
    setMinimumHeight(90);
    setCursor(Qt::PointingHandCursor);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(100); // at most ~10 thumbnail rebuilds per second while drawing
    connect(&m_refresh, &QTimer::timeout, this, [this] {
        rebuildThumbnail();
        update();
    });
    connect(m_canvas, &CanvasView::documentChanged, this, &NavigatorPreview::scheduleThumbnail);
    connect(m_canvas, &CanvasView::viewChanged, this, qOverload<>(&QWidget::update));
}

void NavigatorPreview::scheduleThumbnail()
{
    if (!m_refresh.isActive())
        m_refresh.start();
}

QRectF NavigatorPreview::documentRect() const
{
    const QSizeF doc = m_canvas->view().documentSize();
    const QRectF area = QRectF(rect()).adjusted(6, 6, -6, -6);
    const double scale = std::min(area.width() / doc.width(), area.height() / doc.height());
    const QSizeF size = doc * scale;
    return {area.center().x() - size.width() / 2, area.center().y() - size.height() / 2, size.width(),
            size.height()};
}

void NavigatorPreview::rebuildThumbnail()
{
    const QSize size = documentRect().size().toSize();
    if (size.isEmpty())
        return;
    m_thumbnail = m_canvas->documentImage().scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void NavigatorPreview::resizeEvent(QResizeEvent *)
{
    rebuildThumbnail();
}

void NavigatorPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().dark());
    const QRectF docRect = documentRect();
    paintCheckerboard(p, docRect); // white, and the squares where the paper is see-through
    p.drawImage(docRect.topLeft(), m_thumbnail);

    // Visible area: the viewport corners mapped into the document, then into
    // the thumbnail. A polygon, because the view may be rotated.
    const ViewTransform &view = m_canvas->view();
    const QSizeF vp = view.viewportSize();
    const QPolygonF viewport({QPointF(0, 0), QPointF(vp.width(), 0), QPointF(vp.width(), vp.height()),
                              QPointF(0, vp.height())});
    const double scale = docRect.width() / view.documentSize().width();
    QTransform docToThumb;
    docToThumb.translate(docRect.left(), docRect.top());
    docToThumb.scale(scale, scale);
    const QPolygonF visible = docToThumb.map(view.viewToDocument().map(viewport));

    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());
    p.setPen(QPen(QColor(0xe0, 0x4a, 0x4a), 1.5));
    p.drawPolygon(visible);
}

void NavigatorPreview::mousePressEvent(QMouseEvent *event)
{
    mouseMoveEvent(event);
}

void NavigatorPreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QRectF docRect = documentRect();
    const double scale = m_canvas->view().documentSize().width() / docRect.width();
    m_canvas->centerOn((event->position() - docRect.topLeft()) * scale);
}

// --- NavigatorPanel ---------------------------------------------------------------

NavigatorPanel::NavigatorPanel(CanvasView *canvas, QWidget *parent)
    : QWidget(parent)
    , m_canvas(canvas)
    , m_preview(new NavigatorPreview(canvas, this))
    , m_zoom(new QSlider(Qt::Horizontal, this))
    , m_zoomLabel(new QLabel(this))
    , m_rotation(new QSlider(Qt::Horizontal, this))
    , m_rotationLabel(new QLabel(this))
    , m_flipH(makeButton(IconId::FlipHorizontal, tr("Flip horizontally"), this, true))
    , m_flipV(makeButton(IconId::FlipVertical, tr("Flip vertically"), this, true))
{
    m_zoom->setRange(0, kZoomSteps);
    m_rotation->setRange(-180, 180);
    for (QLabel *l : {m_zoomLabel, m_rotationLabel}) {
        l->setMinimumWidth(44);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }

    auto *zoomOut = makeButton(IconId::ZoomOut, tr("Zoom out"), this);
    auto *zoomIn = makeButton(IconId::Zoom, tr("Zoom in"), this);
    auto *actual = makeButton(IconId::ZoomActual, tr("Actual size (100 %)"), this);
    auto *fit = makeButton(IconId::ZoomFit, tr("Fit to window"), this);
    auto *rotLeft = makeButton(IconId::RotateLeft, tr("Rotate left"), this);
    auto *rotRight = makeButton(IconId::RotateRight, tr("Rotate right"), this);
    auto *rotReset = makeButton(IconId::RotateReset, tr("Reset rotation"), this);

    // Two rows: [slider][value][buttons...]
    auto *controls = new QGridLayout;
    controls->setContentsMargins(4, 2, 4, 4);
    controls->setHorizontalSpacing(2);
    controls->setVerticalSpacing(2);
    controls->setColumnStretch(0, 1);
    int col = 0;
    controls->addWidget(m_zoom, 0, col++);
    controls->addWidget(m_zoomLabel, 0, col++);
    for (QToolButton *b : {zoomOut, zoomIn, actual, fit})
        controls->addWidget(b, 0, col++);
    col = 0;
    controls->addWidget(m_rotation, 1, col++);
    controls->addWidget(m_rotationLabel, 1, col++);
    for (QToolButton *b : {rotLeft, rotRight, rotReset})
        controls->addWidget(b, 1, col++);
    auto *flips = new QHBoxLayout;
    flips->setSpacing(0);
    flips->addWidget(m_flipH);
    flips->addWidget(m_flipV);
    controls->addLayout(flips, 2, 2, 1, 4, Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_preview, 1);
    layout->addLayout(controls);

    connect(m_zoom, &QSlider::valueChanged, m_canvas, [this](int v) { m_canvas->setZoom(sliderToZoom(v)); });
    connect(m_rotation, &QSlider::valueChanged, m_canvas, &CanvasView::setRotation);
    connect(zoomOut, &QToolButton::clicked, m_canvas, &CanvasView::zoomOut);
    connect(zoomIn, &QToolButton::clicked, m_canvas, &CanvasView::zoomIn);
    connect(actual, &QToolButton::clicked, m_canvas, &CanvasView::actualSize);
    connect(fit, &QToolButton::clicked, m_canvas, &CanvasView::fitToView);
    connect(rotLeft, &QToolButton::clicked, m_canvas, &CanvasView::rotateLeft);
    connect(rotRight, &QToolButton::clicked, m_canvas, &CanvasView::rotateRight);
    connect(rotReset, &QToolButton::clicked, m_canvas, &CanvasView::resetRotation);
    connect(m_flipH, &QToolButton::toggled, m_canvas, &CanvasView::setFlipHorizontal);
    connect(m_flipV, &QToolButton::toggled, m_canvas, &CanvasView::setFlipVertical);
    connect(m_canvas, &CanvasView::viewChanged, this, &NavigatorPanel::syncFromView);
    syncFromView();
}

void NavigatorPanel::syncFromView()
{
    const ViewTransform &view = m_canvas->view();
    // Block signals: these updates come *from* the canvas and must not echo back.
    const QSignalBlocker b1(m_zoom), b2(m_rotation), b3(m_flipH), b4(m_flipV);
    m_zoom->setValue(zoomToSlider(view.zoom()));
    m_rotation->setValue(int(std::lround(view.rotation())));
    m_flipH->setChecked(view.isFlippedHorizontally());
    m_flipV->setChecked(view.isFlippedVertically());
    m_zoomLabel->setText(QStringLiteral("%1%").arg(view.zoom() * 100, 0, 'f', view.zoom() < 1 ? 1 : 0));
    m_rotationLabel->setText(QStringLiteral("%1°").arg(view.rotation(), 0, 'f', 0));
}

} // namespace mothion::ui
