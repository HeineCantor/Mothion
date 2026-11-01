#include "canvas/CanvasView.h"

#include "canvas/DebugOverlay.h"
#include "document/DocumentController.h"
#include "input/TabletInputAdapter.h"
#include "tools/ToolState.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <QWheelEvent>

#include <cmath>

namespace mothion::canvas {

using Mode = NavigationGesture::Mode;
using tools::ToolId;
using tools::ToolState;

namespace {
constexpr double kZoomStep = 1.25;
constexpr double kRotateStep = 15.0;
} // namespace

CanvasView::CanvasView(ToolState *toolState, document::DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_toolState(toolState)
    , m_document(document)
    , m_overlay(new DebugOverlay(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);  // hover moves from the mouse
    setTabletTracking(true); // hover moves from the pen (needed for proximity/outline/overlay)
    setAttribute(Qt::WA_OpaquePaintEvent);
    const doc::Document &d = m_document->document();
    m_view.setDocumentSize(QSizeF(d.width(), d.height()));
    m_frame.renderAll(d, m_document->currentFrame());

    using document::DocumentController;
    connect(m_document, &DocumentController::pixelsChanged, this,
            [this](doc::LayerId, doc::DrawingId, const QRect &rect) { renderDocumentRect(rect); });
    connect(m_document, &DocumentController::structureChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::currentFrameChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::strokeInterrupted, this, &CanvasView::cancelStroke);

    m_overlay->move(8, 8);
    m_overlay->hide();
    connect(this, &CanvasView::sampleReceived, m_overlay, &DebugOverlay::addSample);
    connect(this, &CanvasView::deviceChanged, m_overlay, &DebugOverlay::setDevice);

    connect(m_toolState, &ToolState::activeToolChanged, this, &CanvasView::updateCursor);
    connect(m_toolState, &ToolState::toolSettingsChanged, this, [this] { updateBrushOutline(m_pointerPos); });
    updateCursor();
}

// --- view commands ---------------------------------------------------------

void CanvasView::zoomIn()
{
    m_view.zoomAround(m_view.viewportCenter(), kZoomStep);
    viewWasChanged();
}

void CanvasView::zoomOut()
{
    m_view.zoomAround(m_view.viewportCenter(), 1.0 / kZoomStep);
    viewWasChanged();
}

void CanvasView::fitToView()
{
    m_view.fitToView();
    viewWasChanged();
}

void CanvasView::rotateLeft()
{
    m_view.rotateAround(m_view.viewportCenter(), -kRotateStep);
    viewWasChanged();
}

void CanvasView::rotateRight()
{
    m_view.rotateAround(m_view.viewportCenter(), kRotateStep);
    viewWasChanged();
}

void CanvasView::resetRotation()
{
    m_view.resetRotation();
    viewWasChanged();
}

void CanvasView::actualSize()
{
    setZoom(1.0);
}

void CanvasView::setZoom(double zoom)
{
    m_view.setZoomAround(m_view.viewportCenter(), zoom);
    viewWasChanged();
}

void CanvasView::setRotation(double degrees)
{
    m_view.setRotationAround(m_view.viewportCenter(), degrees);
    viewWasChanged();
}

void CanvasView::setFlipHorizontal(bool flipped)
{
    m_view.setFlippedAround(m_view.viewportCenter(), flipped, m_view.isFlippedVertically());
    viewWasChanged();
}

void CanvasView::setFlipVertical(bool flipped)
{
    m_view.setFlippedAround(m_view.viewportCenter(), m_view.isFlippedHorizontally(), flipped);
    viewWasChanged();
}

void CanvasView::centerOn(const QPointF &docPoint)
{
    m_view.centerOn(docPoint);
    viewWasChanged();
}

void CanvasView::viewWasChanged()
{
    update();
    emit viewChanged(m_view.zoom(), m_view.rotation());
}

// --- painting --------------------------------------------------------------

void CanvasView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // The desk around the sheet: a step away from the window colour, in either theme.
    const QColor window = palette().color(QPalette::Window);
    p.fillRect(rect(), window.lightness() < 128 ? window.lighter(135) : window.darker(115));

    // Smooth scaling only when zoomed out; nearest-neighbour shows real pixels up close.
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_view.zoom() < 2.0);
    p.save();
    p.setTransform(m_view.documentToView());
    p.drawImage(QPointF(0, 0), m_frame.image()); // paper included
    p.restore();

    if (m_pointerVisible && ToolState::isPaintTool(m_toolState->activeTool()) && !m_gesture.isActive()) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        const QRectF outline = brushOutlineRect();
        p.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        p.drawEllipse(outline);
        p.setPen(QPen(QColor(255, 255, 255, 160), 1.0));
        p.drawEllipse(outline.adjusted(1, 1, -1, -1));
    }
}

QRectF CanvasView::brushOutlineRect() const
{
    const bool eraser = m_pointerTool == InputTool::Eraser || m_toolState->activeTool() == ToolId::Eraser;
    const double size = m_toolState->settings(eraser ? ToolId::Eraser : ToolId::Brush).size;
    const double r = std::max(2.0, size * m_view.zoom() / 2.0);
    return {m_pointerPos.x() - r, m_pointerPos.y() - r, 2 * r, 2 * r};
}

void CanvasView::updateBrushOutline(const QPointF &newPos)
{
    update(brushOutlineRect().toAlignedRect().adjusted(-2, -2, 2, 2));
    m_pointerPos = newPos;
    update(brushOutlineRect().toAlignedRect().adjusted(-2, -2, 2, 2));
}

void CanvasView::renderWholeFrame()
{
    m_frame.renderAll(m_document->document(), m_document->currentFrame());
    update();
    emit documentChanged(m_frame.image().rect());
}

void CanvasView::renderDocumentRect(const QRect &docRect)
{
    // Re-composing all layers of the rect is correct whichever layer changed.
    m_frame.render(m_document->document(), m_document->currentFrame(), docRect);
    updateDocumentRect(docRect);
}

void CanvasView::updateDocumentRect(const QRect &docRect)
{
    if (docRect.isEmpty())
        return;
    emit documentChanged(docRect);
    update(m_view.documentToView().mapRect(QRectF(docRect)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The pan is relative to the viewport center, so the document stays centered.
    m_view.setViewportSize(size());
    if (!m_fittedOnce && width() > 1 && height() > 1) {
        m_fittedOnce = true;
        m_view.fitToView();
    }
    emit viewChanged(m_view.zoom(), m_view.rotation());
}

// --- Qt events -> PenSample -------------------------------------------------

void CanvasView::tabletEvent(QTabletEvent *event)
{
    // Accepting is what stops Qt from also synthesizing a mouse event
    // (and those synthesized mouse events may be compressed).
    event->accept();
    noteDevice(event->pointingDevice());
    handleSample(input::toPenSample(*event));
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if (auto s = input::toPenSample(*event)) {
        noteDevice(event->pointingDevice());
        handleSample(*s);
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (auto s = input::toPenSample(*event)) {
        noteDevice(event->pointingDevice());
        handleSample(*s);
    }
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (auto s = input::toPenSample(*event))
        handleSample(*s);
}

void CanvasView::noteDevice(const QPointingDevice *device)
{
    if (device == m_lastDevice)
        return;
    m_lastDevice = device;
    emit deviceChanged(input::describeDevice(device));
}

void CanvasView::setDebugOverlayVisible(bool visible)
{
    m_overlay->setVisible(visible);
}

void CanvasView::setPenProximity(const ProximityEvent &event)
{
    m_overlay->setProximity(event);
    m_pointerTool = event.tool;
    if (!event.entering) {
        m_pointerVisible = false;
        updateBrushOutline(m_pointerPos);
    }
}

void CanvasView::leaveEvent(QEvent *event)
{
    m_pointerVisible = false;
    updateBrushOutline(m_pointerPos);
    QWidget::leaveEvent(event);
}

// --- PenSample handling ----------------------------------------------------

void CanvasView::handleSample(const PenSample &sample)
{
    emit sampleReceived(sample);
    m_pointerTool = sample.tool;
    m_pointerVisible = true;
    updateBrushOutline(QPointF(sample.x, sample.y));

    const bool busy = m_gesture.isActive() || m_stroke.has_value();
    switch (sample.phase) {
    case SamplePhase::Press:
        if (busy)
            break; // a second button during a drag: keep doing what we do
        if (const Mode mode = navigationModeFor(sample); mode != Mode::None)
            m_gesture.begin(mode, QPointF(sample.x, sample.y), m_view);
        else if (sample.tipDown()
                 && (sample.tool == InputTool::Eraser || ToolState::isPaintTool(m_toolState->activeTool())))
            beginStroke(sample);
        break;
    case SamplePhase::Move:
        if (m_gesture.isActive()) {
            m_gesture.update(QPointF(sample.x, sample.y), m_view);
            viewWasChanged();
        } else if (m_stroke) {
            continueStroke(sample);
        }
        break;
    case SamplePhase::Release:
        m_gesture.end();
        endStroke();
        break;
    case SamplePhase::Hover:
        break;
    }
}

Mode CanvasView::navigationModeFor(const PenSample &sample) const
{
    if (m_spaceHeld) {
        const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
        if (mods & Qt::ControlModifier)
            return Mode::Zoom;
        if (mods & Qt::ShiftModifier)
            return Mode::Rotate;
        return Mode::Pan;
    }
    // Middle mouse button, or a pen barrel button pressed while hovering.
    if (!sample.tipDown())
        return Mode::Pan;
    // The eraser end of the pen always erases, whatever tool is selected.
    if (sample.tool == InputTool::Eraser)
        return Mode::None;
    switch (m_toolState->activeTool()) {
    case ToolId::Hand: return Mode::Pan;
    case ToolId::Zoom: return Mode::Zoom;
    default: return Mode::None;
    }
}

void CanvasView::beginStroke(const PenSample &sample)
{
    m_eraseStroke = sample.tool == InputTool::Eraser || m_toolState->activeTool() == ToolId::Eraser;
    // Erasing an empty frame has nothing to act on, so only the brush creates drawings.
    QString reason;
    const auto target = m_document->beginPaint(!m_eraseStroke, &reason);
    if (!target) {
        if (!reason.isEmpty())
            emit statusMessage(reason);
        return;
    }
    const tools::BrushSettings settings = m_toolState->settings(m_eraseStroke ? ToolId::Eraser : ToolId::Brush);
    const doc::Document &d = m_document->document();
    m_stroke.emplace(*target->image, m_toolState->color(), settings.opacity, m_eraseStroke,
                     QRect(0, 0, d.width(), d.height()));

    m_smoother.setStrength(m_toolState->smoothing());
    m_smoother.reset();
    m_document->notifyPainted(m_stroke->addDabs(m_spacer.begin(strokePoint(m_smoother.filter(sample)))));
}

void CanvasView::continueStroke(const PenSample &sample)
{
    m_document->notifyPainted(m_stroke->addDabs(m_spacer.continueTo(strokePoint(m_smoother.filter(sample)))));
}

void CanvasView::endStroke()
{
    // The release sample itself is not drawn: its pressure is often already 0.
    if (!m_stroke)
        return;
    m_document->endPaint(m_stroke->before());
    m_stroke.reset();
}

void CanvasView::cancelStroke()
{
    if (!m_stroke)
        return;
    // Restore pixels first: cancelPaint() may remove the drawing they live in.
    m_stroke->cancel();
    m_stroke.reset();
    m_document->cancelPaint();
    renderWholeFrame();
}

DabSpacer::Point CanvasView::strokePoint(const PenSample &smoothed) const
{
    const double size = m_toolState->settings(m_eraseStroke ? ToolId::Eraser : ToolId::Brush).size;
    const float pressure = m_toolState->pressureCurve().map(smoothed.pressure);
    return {m_view.mapToDocument(QPointF(smoothed.x, smoothed.y)), size / 2.0 * pressure};
}

// --- keyboard / wheel navigation --------------------------------------------

void CanvasView::wheelEvent(QWheelEvent *event)
{
    // Some platforms turn Shift+wheel into horizontal scrolling: accept either axis.
    const QPoint a = event->angleDelta();
    const int delta = a.y() != 0 ? a.y() : a.x();
    if (event->modifiers() & Qt::ShiftModifier)
        m_view.rotateAround(event->position(), delta > 0 ? -kRotateStep : kRotateStep);
    else
        m_view.zoomAround(event->position(), std::pow(1.0015, delta));
    viewWasChanged();
    event->accept();
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasView::updateCursor()
{
    if (m_spaceHeld || m_toolState->activeTool() == ToolId::Hand)
        setCursor(Qt::OpenHandCursor);
    else if (m_toolState->activeTool() == ToolId::Zoom)
        setCursor(Qt::SizeHorCursor);
    else
        setCursor(Qt::CrossCursor);
    update();
}

} // namespace mothion::canvas
