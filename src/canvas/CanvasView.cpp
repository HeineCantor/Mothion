#include "canvas/CanvasView.h"

#include "tools/ToolState.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace mothion::canvas {

using Mode = NavigationGesture::Mode;
using tools::ToolId;

namespace {
constexpr double kZoomStep = 1.25;
constexpr double kRotateStep = 15.0;
const QSize kDocumentSize(1920, 1080); // fixed until there is a document model
} // namespace

CanvasView::CanvasView(tools::ToolState *toolState, QWidget *parent)
    : QWidget(parent)
    , m_toolState(toolState)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_view.setDocumentSize(kDocumentSize);

    connect(m_toolState, &tools::ToolState::activeToolChanged, this, &CanvasView::updateCursor);
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

void CanvasView::viewWasChanged()
{
    update();
    emit viewChanged(m_view.zoom(), m_view.rotation());
}

// --- painting --------------------------------------------------------------

void CanvasView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x3a, 0x3a, 0x3e));

    // Smooth scaling only when zoomed out; nearest-neighbour shows real pixels up close.
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_view.zoom() < 2.0);
    p.setRenderHint(QPainter::Antialiasing);
    p.setTransform(m_view.documentToView());
    const QRectF paper(QPointF(0, 0), m_view.documentSize());
    p.fillRect(paper, Qt::white);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Keep the document center where it was relative to the viewport center.
    m_view.setViewportSize(size());
    if (!m_fittedOnce && width() > 1 && height() > 1) {
        m_fittedOnce = true;
        m_view.fitToView();
    }
    emit viewChanged(m_view.zoom(), m_view.rotation());
}

// --- navigation input ------------------------------------------------------

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

Mode CanvasView::navigationModeFor(Qt::MouseButtons buttons) const
{
    if (buttons & Qt::MiddleButton)
        return Mode::Pan;
    if (m_spaceHeld) {
        const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
        if (mods & Qt::ControlModifier)
            return Mode::Zoom;
        if (mods & Qt::ShiftModifier)
            return Mode::Rotate;
        return Mode::Pan;
    }
    switch (m_toolState->activeTool()) {
    case ToolId::Hand: return Mode::Pan;
    case ToolId::Zoom: return Mode::Zoom;
    default: return Mode::None;
    }
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    const Mode mode = navigationModeFor(event->buttons());
    if (mode != Mode::None)
        m_gesture.begin(mode, event->position(), m_view);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_gesture.isActive()) {
        m_gesture.update(event->position(), m_view);
        viewWasChanged();
    }
}

void CanvasView::mouseReleaseEvent(QMouseEvent *)
{
    m_gesture.end();
}

void CanvasView::updateCursor()
{
    if (m_spaceHeld || m_toolState->activeTool() == ToolId::Hand)
        setCursor(Qt::OpenHandCursor);
    else if (m_toolState->activeTool() == ToolId::Zoom)
        setCursor(Qt::SizeHorCursor);
    else
        setCursor(Qt::CrossCursor);
}

} // namespace mothion::canvas
