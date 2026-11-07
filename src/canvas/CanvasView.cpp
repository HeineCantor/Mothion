#include "canvas/CanvasView.h"

#include "canvas/DebugOverlay.h"
#include "canvas/EyedropperTool.h"
#include "canvas/EffectBrushTools.h"
#include "canvas/FillTool.h"
#include "canvas/MoveTool.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionTool.h"
#include "canvas/ShapeTool.h"
#include "canvas/TransformTool.h"
#include "document/DocumentController.h"
#include "input/TabletInputAdapter.h"
#include "tools/Preferences.h"
#include "tools/ToolState.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QWindow>
#include <QRandomGenerator>
#include <QTabletEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <limits>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

using Mode = NavigationGesture::Mode;
using tools::Preferences;
using tools::ToolId;
using tools::ToolState;

namespace {

// A magnifying glass with a plus, for the zoom tool (Qt has no cursor of the kind). Drawn in code, white
// outlined in black so it shows on any paper; the hotspot is the center of the lens.
QCursor zoomCursor()
{
    static const QCursor cursor = [] {
        constexpr int size = 32;
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF center(13, 13);
        constexpr double radius = 8.5;
        const QPointF from = center + QPointF(6.4, 6.4), to(28, 28);
        for (const auto &[color, extra] : {std::pair{QColor(0, 0, 0), 2.4}, std::pair{QColor(255, 255, 255), 0.0}}) {
            p.setPen(QPen(color, 1.8 + extra, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, radius, radius);
            p.setPen(QPen(color, 3.4 + extra, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(from, to); // the handle
            p.setPen(QPen(color, 1.6 + extra, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(center - QPointF(4, 0), center + QPointF(4, 0)); // the plus
            p.drawLine(center - QPointF(0, 4), center + QPointF(0, 4));
        }
        p.end();
        return QCursor(pixmap, 13, 13);
    }();
    return cursor;
}

// A cursor that shows nothing. Qt::BlankCursor is not honoured everywhere
// (some backends fall back to the arrow), a transparent image is.
QCursor invisibleCursor()
{
    static const QCursor cursor = [] {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        return QCursor(pixmap, 0, 0);
    }();
    return cursor;
}

// How often the selection outline's black and white dashes swap.
constexpr int kAntsIntervalMs = 200;
} // namespace

CanvasView::CanvasView(ToolState *toolState, document::DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_toolState(toolState)
    , m_document(document)
    , m_gizmo(document)
    , m_cameraGizmo(document)
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
            [this](doc::CelId celId, doc::DrawingId drawing, const QRect &rect) {
                m_onionImages.erase(drawing);
                // Only a drawing shown at the current frame changes the frame; one of another frame (a
                // transform of a whole cel tells of every one of them) at most the onion skin, painted over it.
                const doc::Cel *cel = m_document->document().cel(celId);
                // doc::kNoId: a change of the cel itself (its opacity, say), which every drawing of it shows.
                const doc::DrawingId shown = cel ? cel->timing().drawingAt(m_document->currentFrame()) : doc::kNoId;
                if (cel && shown != doc::kNoId && (drawing == doc::kNoId || shown == drawing))
                    renderDocumentRect(rect);
                else if (m_toolState->onionSkin().enabled)
                    update();
            });
    connect(m_document, &DocumentController::structureChanged, this, [this] { m_onionImages.clear(); });
    // The camera's frame follows it (a drag, a key, the frame of the timeline, it being selected or turned off).
    connect(m_document, &DocumentController::cameraChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::cameraSelectionChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::documentReplaced, this, [this] { m_onionImages.clear(); });
    connect(m_document, &DocumentController::currentCelChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::currentLayerChanged, this, qOverload<>(&QWidget::update));
    connect(m_toolState, &ToolState::onionSkinChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::structureChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::currentFrameChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::strokeInterrupted, this, &CanvasView::cancelStroke);
    connect(m_document, &DocumentController::documentReplaced, this, [this] {
        const doc::Document &replaced = m_document->document();
        m_view.setDocumentSize(QSizeF(replaced.width(), replaced.height()));
        m_view.fitToView(); // the frame itself is redrawn on structureChanged, which follows
        emit viewChanged(m_view.zoom(), m_view.rotation());
    });

    m_overlay->move(8, 8);
    m_overlay->hide();
    connect(this, &CanvasView::sampleReceived, m_overlay, &DebugOverlay::addSample);
    connect(this, &CanvasView::deviceChanged, m_overlay, &DebugOverlay::setDevice);

    m_moveTool = std::make_unique<MoveTool>(m_document, [this](const QString &text) { emit statusMessage(text); });
    m_rectSelectTool =
        std::make_unique<SelectionTool>(m_document, SelectionTool::Shape::Rectangle);
    m_lassoSelectTool =
        std::make_unique<SelectionTool>(m_document, SelectionTool::Shape::Lasso);
    m_puppetTool = std::make_unique<PuppetTool>(
        m_document, m_toolState, [this](const QString &text) { emit statusMessage(text); },
        [this] {
            update();
            emit puppetChanged();
        });
    m_eyedropperTool =
        std::make_unique<EyedropperTool>(m_toolState, [this](const QPointF &docPos) { return colorAt(docPos); });
    m_fillTool = std::make_unique<FillTool>(
        m_document, m_toolState, [this]() -> const QImage & { return m_frame.image(); },
        [this](const QString &text) { emit statusMessage(text); });
    m_blendTool = std::make_unique<BlendTool>(m_document, m_toolState,
                                              [this](const QString &text) { emit statusMessage(text); });
    m_liquifyTool = std::make_unique<LiquifyTool>(m_document, m_toolState,
                                                  [this](const QString &text) { emit statusMessage(text); });
    m_shapeTool = std::make_unique<ShapeTool>(
        m_document, m_toolState, [this](const QString &text) { emit statusMessage(text); },
        [this] {
            update();
            emit shapeChanged();
        });
    connect(m_toolState, &ToolState::shapeSettingsChanged, this, &CanvasView::shapeChanged);
    m_transformTool = std::make_unique<TransformTool>(
        m_document, m_toolState, [this](const QString &text) { emit statusMessage(text); },
        [this] {
            update();
            emit transformChanged();
        });
    static_cast<TransformTool *>(m_transformTool.get())->setDeferredRendering(true);
    m_antsTimer = new QTimer(this);
    m_antsTimer->setInterval(kAntsIntervalMs);
    connect(m_antsTimer, &QTimer::timeout, this, [this] {
        m_antsPhase = 1 - m_antsPhase;
        update();
    });
    connect(m_document, &DocumentController::selectionChanged, this, &CanvasView::updateAnts);
    connect(m_document, &DocumentController::documentReplaced, this, &CanvasView::updateAnts);

    connect(m_toolState, &ToolState::activeToolChanged, this, &CanvasView::commitPendingEdits);
    connect(m_toolState, &ToolState::activeToolChanged, this, &CanvasView::updateCursor);
    for (auto signal : {&DocumentController::layerChosenChanged, &DocumentController::cameraSelectionChanged,
                        &DocumentController::structureChanged, &DocumentController::documentReplaced})
        connect(m_document, signal, this, &CanvasView::updateCursor); // may now (not) be drawable
    connect(m_document, &DocumentController::currentFrameChanged, this, &CanvasView::updateCursor);
    connect(m_document, &DocumentController::currentCelChanged, this, &CanvasView::updateCursor);
    connect(m_toolState, &ToolState::activeToolChanged, this, [this] { updateStraightLine(); });
    connect(m_document, &DocumentController::documentReplaced, this, [this] {
        m_lastPoint.reset();
        updateStraightLine();
    });
    connect(m_toolState, &ToolState::activeToolChanged, this, [this] {
        if (CanvasTool *tool = canvasTool()) {
            tool->setViewZoom(m_view.zoom());
            tool->activated();
        }
    });
    connect(m_toolState, &ToolState::toolSettingsChanged, this, [this] { updateBrushOutline(m_pointerPos); });
    connect(&Preferences::instance(), &Preferences::changed, this, [this] { // the outline on or off
        updateCursor();
        update();
    });
    updateCursor();
}

// --- onion skin --------------------------------------------------------------

QList<int> CanvasView::onionSkinFrames() const
{
    QList<int> frames;
    const tools::OnionSkinSettings s = m_toolState->onionSkin();
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!s.enabled || !cel)
        return frames;
    const int current = m_document->currentFrame();
    const int count = m_document->document().frameCount();
    std::vector<doc::DrawingId> seen{cel->timing().drawingAt(current)}; // the current one is not a ghost
    auto side = [&](int direction, int reach) {
        for (int k = 1; k <= reach; ++k) {
            const int frame = current + direction * k;
            if (frame < 0 || frame >= count)
                break;
            const doc::DrawingId id = cel->timing().drawingAt(frame);
            if (id == doc::kNoId || std::find(seen.begin(), seen.end(), id) != seen.end())
                continue;
            seen.push_back(id);
            frames << direction * k;
        }
    };
    side(-1, s.before);
    side(+1, s.after);
    return frames;
}

const CanvasView::OnionImage &CanvasView::onionImage(doc::DrawingId id, const QColor &color)
{
    OnionImage &cached = m_onionImages[id];
    if (!cached.tinted.isNull() && cached.color == color.rgba())
        return cached;
    cached = {};
    cached.color = color.rgba();
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    const doc::Drawing *drawing = cel ? cel->drawing(id) : nullptr;
    const QRect bounds = drawing ? drawingBounds(*drawing, false) : QRect(); // no guides in ghosts
    if (bounds.isEmpty())
        return cached;
    QImage image = composeDrawing(*drawing, bounds, false); // its visible layers, as the cel shows it
    // Every pixel the color, keeping its coverage.
    const int r = color.red(), g = color.green(), b = color.blue();
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int a = qAlpha(line[x]);
            line[x] = a ? qRgba(r * a / 255, g * a / 255, b * a / 255, a) : 0;
        }
    }
    cached.origin = bounds.topLeft();
    cached.tinted = std::move(image);
    return cached;
}

void CanvasView::paintOnionSkin(QPainter &p)
{
    const tools::OnionSkinSettings s = m_toolState->onionSkin();
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!s.enabled || !cel)
        return;
    QList<int> frames = onionSkinFrames();
    // Farthest first, so the nearest lie on top; each step away fainter.
    std::stable_sort(frames.begin(), frames.end(), [](int a, int b) { return std::abs(a) > std::abs(b); });
    for (const int distance : frames) {
        const int reach = distance < 0 ? s.before : s.after;
        const double fade = 1.0 - double(std::abs(distance) - 1) / double(reach + 1);
        const OnionImage &ghost = onionImage(cel->timing().drawingAt(m_document->currentFrame() + distance),
                                             distance < 0 ? s.beforeColor : s.afterColor);
        if (ghost.tinted.isNull())
            continue;
        p.setOpacity(s.opacity * fade);
        p.drawImage(ghost.origin, ghost.tinted);
    }
    p.setOpacity(1.0);
}

// --- steps of an open edit -------------------------------------------------

bool CanvasView::undoInEdit()
{
    CanvasTool *tool = canvasTool();
    const bool done = tool && !m_editTool && tool->undoStep();
    emit editStepsChanged();
    return done;
}

bool CanvasView::redoInEdit()
{
    CanvasTool *tool = canvasTool();
    const bool done = tool && !m_editTool && tool->redoStep();
    emit editStepsChanged();
    return done;
}

bool CanvasView::canUndoInEdit() const
{
    const CanvasTool *tool = canvasTool();
    return tool && tool->canUndoStep();
}

bool CanvasView::canRedoInEdit() const
{
    const CanvasTool *tool = canvasTool();
    return tool && tool->canRedoStep();
}

// --- view commands ---------------------------------------------------------

void CanvasView::zoomIn()
{
    m_view.zoomAround(m_view.viewportCenter(), Preferences::instance().zoomFactor());
    viewWasChanged();
}

void CanvasView::zoomOut()
{
    m_view.zoomAround(m_view.viewportCenter(), 1.0 / Preferences::instance().zoomFactor());
    viewWasChanged();
}

void CanvasView::fitToView()
{
    m_view.fitToView();
    viewWasChanged();
}

void CanvasView::rotateLeft()
{
    m_view.rotateAround(m_view.viewportCenter(), -double(Preferences::instance().rotateStep()));
    viewWasChanged();
}

void CanvasView::rotateRight()
{
    m_view.rotateAround(m_view.viewportCenter(), double(Preferences::instance().rotateStep()));
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
    paintOnionSkin(p);
    p.restore();

    paintSelectionOutline(p, m_view, m_document->selection(), m_antsPhase);
    if (const CanvasTool *tool = canvasTool()) {
        p.save();
        tool->paintOverlay(p, m_view);
        p.restore();
    }
    m_gizmo.paint(p, m_view);
    m_cameraGizmo.paint(p, m_view);
    paintSnapGuide(p);
    paintStraightLinePreview(p);

    m_paintedOutline = QRect();
    if (m_pointerVisible && Preferences::instance().brushOutline() && ToolState::isPaintTool(m_toolState->activeTool())
        && !m_document->drawingBlocked() && !m_gesture.isActive()
        && !picksColor() && m_editTool != m_eyedropperTool.get()) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        const QRectF outline = brushOutlineRect();
        m_paintedOutline = outline.toAlignedRect().adjusted(-2, -2, 2, 2);
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
    // The old outline from where it was painted: its size may have changed
    // since, so brushOutlineRect() would no longer cover it.
    update(m_paintedOutline);
    m_pointerPos = newPos;
    update(brushOutlineRect().toAlignedRect().adjusted(-2, -2, 2, 2));
    if (m_lineArmed)
        update(); // the line follows the pen
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
    // An accepted pen press is not a click to Qt: take the focus ourselves,
    // or keys like Space and Tab keep going to the panel clicked last.
    if (event->type() == QEvent::TabletPress && !hasFocus())
        setFocus(Qt::MouseFocusReason);
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
    syncWindowCursor();
    updateBrushOutline(QPointF(sample.x, sample.y));

    const bool busy = m_gesture.isActive() || m_stroke.has_value() || m_editTool || m_gizmo.isDragging()
                      || m_cameraGizmo.isDragging();
    const QPointF docPos = m_view.mapToDocument(QPointF(sample.x, sample.y));
    const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    switch (sample.phase) {
    case SamplePhase::Press:
        if (busy)
            break; // a second button during a drag: keep doing what we do
        if (const Mode mode = navigationModeFor(sample); mode != Mode::None) {
            m_gesture.begin(mode, QPointF(sample.x, sample.y), m_view);
        } else if (sample.tipDown() && m_cameraGizmo.press(docPos, m_view)) {
            update(); // the camera selected: its frame or a handle, whatever the tool
        } else if (sample.tipDown() && m_gizmo.press(docPos, m_view)) {
            update(); // a handle of the perspective layer, whatever the tool
        } else if (picksColor() && sample.tipDown() && sample.tool != InputTool::Eraser) {
            m_eyedropperTool->press(docPos, modifiers);
            m_editTool = m_eyedropperTool.get();
        } else if (sample.tipDown()
                   && (sample.tool == InputTool::Eraser || ToolState::isPaintTool(m_toolState->activeTool()))) {
            if (m_lineArmed)
                placeStraightLine(sample);
            else
                beginStroke(sample);
        } else if (CanvasTool *tool = canvasTool(); tool && sample.tipDown()) {
            tool->setViewZoom(m_view.zoom());
            tool->press(docPos, modifiers);
            m_editTool = tool->isActive() ? tool : nullptr;
            update();
        }
        break;
    case SamplePhase::Move:
        if (m_gesture.isActive()) {
            m_gesture.update(QPointF(sample.x, sample.y), m_view);
            viewWasChanged();
        } else if (m_cameraGizmo.isDragging()) {
            m_cameraGizmo.move(docPos);
            update();
        } else if (m_gizmo.isDragging()) {
            m_gizmo.move(docPos, m_view);
            update();
        } else if (m_stroke) {
            continueStroke(sample);
        } else if (m_editTool) {
            m_editTool->move(docPos, modifiers);
            update();
        } else if (CanvasTool *tool = canvasTool(); tool && tool->hover(docPos)) {
            update();
        }
        break;
    case SamplePhase::Release:
        m_gesture.end();
        if (m_cameraGizmo.isDragging()) {
            m_cameraGizmo.release(docPos);
            update();
            updateGizmoCursor();
        }
        if (m_gizmo.isDragging()) {
            const bool menu = m_gizmo.release(docPos);
            update();
            if (menu) {
                QMenu *settings = m_gizmo.createMenu(this);
                settings->exec(mapToGlobal(QPointF(sample.x, sample.y).toPoint()));
                settings->deleteLater();
            }
            updateGizmoCursor();
        }
        endStroke();
        if (m_editTool) {
            CanvasTool *tool = m_editTool;
            m_editTool = nullptr; // cleared first: releasing may interrupt (and cancel) itself
            tool->release(docPos, modifiers);
            update();
            emit editStepsChanged();
        }
        break;
    case SamplePhase::Hover:
        if (!busy && m_cameraGizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (!busy && m_gizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (CanvasTool *tool = canvasTool(); tool && !busy) {
            if (tool->hover(docPos))
                update();
            if (tool == m_transformTool.get() && m_gizmo.hovered() == PerspectiveGizmo::Handle::None)
                setCursor(tool->cursor()); // the handle or turn under the pointer
        }
        break;
    }
}

Mode CanvasView::navigationModeFor(const PenSample &sample) const
{
    if (panKeyHeld()) {
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
    // The transparent color makes the brush erase, with the brush's own tip and settings.
    const bool transparentBrush = m_toolState->transparentSelected() && m_toolState->activeTool() != ToolId::Eraser
                                  && sample.tool != InputTool::Eraser;
    m_eraseStroke = sample.tool == InputTool::Eraser || m_toolState->activeTool() == ToolId::Eraser
                    || transparentBrush;
    // Erasing an empty frame has nothing to act on, so only the brush creates drawings.
    QString reason;
    const auto target = m_document->beginPaint(!m_eraseStroke, &reason);
    if (!target) {
        if (!reason.isEmpty())
            emit statusMessage(reason);
        return;
    }
    const ToolId tool = m_eraseStroke && !transparentBrush ? ToolId::Eraser : ToolId::Brush;
    if (target->mask) // a mask holds what is hidden: the brush puts the layer back, the eraser hides it
        m_eraseStroke = !m_eraseStroke;
    const tools::BrushSettings settings = m_toolState->settings(tool);
    const tools::BrushDefinitionPtr definition = m_toolState->brush(tool);
    brush::BrushStyle style = definition->style;
    style.size = settings.size / 2.0;
    // The start rises live or once the stroke is done, not both.
    if (m_toolState->correctsStartAfterStroke())
        style.startTaper = 0.0;
    else
        style.startCorrection = 0;
    auto stamper = std::make_shared<DabStamper>(target->tint(m_toolState->color()), style.hardness, definition->tips,
                                                style.verticalThickness);
    stamper->setSecondaryColor(target->tint(m_toolState->inactiveColor()));
    const doc::Document &d = m_document->document();
    m_strokeImage = target->image;
    m_stroke.emplace(*target->image, std::move(stamper), settings.opacity, m_eraseStroke,
                     QRect(0, 0, d.width(), d.height()));
    if (m_document->hasSelection())
        m_stroke->setSelection(m_document->selection()); // paint or erase only inside it
    m_stroke->setStyle(style);
    m_seed = QRandomGenerator::global()->generate();
    m_dabs.emplace(style, m_seed);
    m_points.clear();
    m_drawnPoints = 0;

    m_smoother.setStrength(m_placingLine ? 0 : m_toolState->smoothing());
    m_smoother.reset();
    m_velocity.reset();
    // With the correction after the stroke, corners are found when the pen lifts.
    m_sharpCorners = m_toolState->sharpCorners() && style.revision == 0 && !m_placingLine;
    m_corners.reset();
    m_heldSamples.clear();
    m_snapHeld.clear();
    m_snapRaw.clear();
    m_snapping = false;
    if (m_toolState->perspectiveSnap() && !m_placingLine) {
        if (const auto layer = m_document->perspectiveLayerById(m_toolState->perspectiveSnapLayer())) {
            m_snap.begin(layer->grid, m_view.mapToDocument(QPointF(sample.x, sample.y)), 6.0 / m_view.zoom());
            m_snapping = true;
        } else {
            emit statusMessage(tr("No perspective layer to snap to: choose one with “Snap to” in the Layers panel"));
        }
    }
    takeSample(sample);
}

void CanvasView::continueStroke(const PenSample &sample)
{
    takeSample(sample);
}

PenSample CanvasView::snappedSample(const PenSample &sample) const
{
    const QPointF onAxis = m_snap.snapped(m_view.mapToDocument(QPointF(sample.x, sample.y)));
    const QPointF inView = m_view.documentToView().map(onAxis);
    PenSample out = sample;
    out.x = inView.x();
    out.y = inView.y();
    return out;
}

void CanvasView::takeSample(const PenSample &sample)
{
    if (!m_snapping) {
        takeRawSample(sample);
        return;
    }
    // Snapping: the first samples wait until the pen has shown which axis it follows, then
    // they and every later one are put on that axis's line.
    m_snapRaw.push_back(sample);
    m_snapHeld.push_back(sample);
    m_snapPen = m_view.mapToDocument(QPointF(sample.x, sample.y));
    update(); // the guide follows the pen
    if (!m_snap.update(m_snapPen))
        return;
    for (const PenSample &held : m_snapHeld)
        takeRawSample(snappedSample(held));
    m_snapHeld.clear();
}

void CanvasView::cycleSnapAxis()
{
    if (!m_stroke || !m_snapping || m_snap.cycle() < 0)
        return;
    // The stroke is drawn again from its first sample, on the new axis: what was drawn on the old one goes.
    const brush::BrushStyle style = m_dabs->style();
    const QRect area = m_stroke->restart();
    m_dabs.emplace(style, m_seed);
    m_points.clear();
    m_drawnPoints = 0;
    m_smoother.reset();
    m_velocity.reset();
    m_corners.reset();
    m_heldSamples.clear();
    m_document->notifyPainted(area);
    for (const PenSample &raw : m_snapRaw)
        takeRawSample(snappedSample(raw));
    m_snapHeld.clear();
    update();
}

void CanvasView::paintSnapGuide(QPainter &p) const
{
    if (!m_stroke || !m_snapping || !m_snap.decided())
        return;
    const int axis = m_snap.axis();
    const QColor color = PerspectiveSnap::axisColor(axis);
    const QTransform toView = m_view.documentToView();
    const QPolygonF guide = toView.map(m_snap.guide(m_snapPen));
    if (guide.size() < 2)
        return;
    const QPointF to = guide.last();
    const ToolId tool = m_eraseStroke ? ToolId::Eraser : ToolId::Brush;
    const double half = std::max(2.0, m_toolState->settings(tool).size / 2.0 * m_view.zoom()) + 2.0;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // An outline around the stroke: a pill the width of the brush, along the line.
    QPainterPath line;
    line.addPolygon(guide);
    line.lineTo(to + QPointF(0.01, 0)); // a dot, not nothing, when it has not moved
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * half);
    stroker.setCapStyle(Qt::RoundCap);
    const QPainterPath outline = stroker.createStroke(line);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 170), 4.0));
    p.drawPath(outline);
    p.setPen(QPen(color, 2.0));
    p.drawPath(outline);
    // The letter, following the pen: white halo, then the color.
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(15);
    QPainterPath letter;
    const QPointF at = to + QPointF(half + 8.0, -half - 6.0);
    letter.addText(at, font, QString(PerspectiveSnap::axisLetter(axis)));
    p.setPen(QPen(QColor(255, 255, 255, 220), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(letter);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(letter);
    p.restore();
}

void CanvasView::takeRawSample(const PenSample &sample)
{
    if (!m_sharpCorners) {
        addStrokePoint(brushInput(m_smoother.filter(sample)));
        return;
    }
    // Corners are found on the pen's own path, before the stabilizer rounds them.
    m_heldSamples.push_back(sample);
    m_corners.add(sample.x, sample.y);
    releaseHeldSamples(false);
}

void CanvasView::releaseHeldSamples(bool end)
{
    for (const brush::CornerDetector::Decision &d : m_corners.take(end)) {
        const PenSample sample = m_heldSamples.front();
        m_heldSamples.pop_front();
        if (!d.corner) {
            addStrokePoint(brushInput(m_smoother.filter(sample)));
            continue;
        }
        // The line goes to the corner itself, then on from there.
        m_smoother.restartAt(sample);
        brush::BrushInput corner = brushInput(sample);
        corner.corner = true;
        addStrokePoint(corner);
        // Its point, between the ways in and out.
        const QPointF in = m_view.mapToDocument(QPointF(d.before.x, d.before.y));
        const QPointF out = m_view.mapToDocument(QPointF(d.after.x, d.after.y));
        const brush::BrushDab dab = m_dabs->dabHere(corner);
        if (const auto tip = brush::cornerTip(corner.x, corner.y, corner.x - in.x(), corner.y - in.y(),
                                              out.x() - corner.x, out.y() - corner.y, dab.radius))
            m_document->notifyPainted(m_stroke->addCornerTip(*tip, dab));
    }
}

void CanvasView::addStrokePoint(const brush::BrushInput &in)
{
    m_points.push_back(in);
    drawPendingPoints();
}

void CanvasView::drawPendingPoints()
{
    const std::size_t end = m_points.size();
    QRect changed;
    for (; m_drawnPoints < end; ++m_drawnPoints) {
        const brush::BrushInput &in = m_points[m_drawnPoints];
        const auto dabs = m_drawnPoints == 0 ? m_dabs->begin(in) : m_dabs->continueTo(in);
        changed |= m_stroke->addDabs(dabs);
    }
    m_document->notifyPainted(changed);
}

void CanvasView::endStroke()
{
    // The release sample itself is not drawn: its pressure is often already 0.
    if (!m_stroke)
        return;
    if (m_snapping) { // a stroke too short to show its direction: as drawn
        for (const PenSample &held : m_snapHeld)
            takeRawSample(held);
        m_snapHeld.clear();
        m_snapRaw.clear();
        m_snapping = false;
        update(); // the guide goes
    }
    if (m_sharpCorners)
        releaseHeldSamples(true);
    drawPendingPoints();
    const brush::BrushStyle style = m_dabs->style();
    if (brush::needsFinalPass(style)) {
        // Redraw the whole stroke as it is drawn for good: same seed, its
        // length now known. Before the end zone the dabs come out the same.
        QRect changed = m_stroke->restart();
        const std::vector<brush::BrushInput> points = brush::finalPoints(style, m_points);
        brush::DabGenerator generator(style, m_seed);
        generator.setStrokeLength(brush::strokeLength(points));
        // Dabs, and the points of sharp corners with the option on.
        changed |= m_stroke->drawPoints(generator, points, m_toolState->sharpCorners());
        m_document->notifyPainted(changed);
    }
    m_document->notifyPainted(m_stroke->finish());
    // Before the stroke is recorded: a stroke on an empty frame that left nothing takes back the frame it made, and
    // the image it painted on goes with it.
    if (!m_points.empty())
        m_lastPoint = lineStartOf(m_points);
    m_document->endPaint(m_stroke->before());
    m_stroke.reset();
    m_strokeImage = nullptr;
    m_dabs.reset();
    m_points.clear();
    updateStraightLine(); // Shift still held: a line from where this stroke ended
}

CanvasTool *CanvasView::canvasTool() const
{
    switch (m_toolState->activeTool()) {
    case ToolId::Move: return m_moveTool.get();
    case ToolId::RectSelect: return m_rectSelectTool.get();
    case ToolId::LassoSelect: return m_lassoSelectTool.get();
    case ToolId::Puppet: return m_puppetTool.get();
    case ToolId::Eyedropper: return m_eyedropperTool.get();
    case ToolId::Fill: return m_fillTool.get();
    case ToolId::Transform: return m_transformTool.get();
    case ToolId::Blend: return m_blendTool.get();
    case ToolId::Liquify: return m_liquifyTool.get();
    case ToolId::Shape: return m_shapeTool.get();
    default: return nullptr;
    }
}

void CanvasView::commitPendingEdits()
{
    // An edit left open (a puppet warp) is applied when another tool takes over.
    for (CanvasTool *tool : {m_moveTool.get(), m_rectSelectTool.get(), m_lassoSelectTool.get(), m_puppetTool.get(),
                              m_shapeTool.get(), m_transformTool.get()})
        if (tool != canvasTool() && tool->hasPendingEdit())
            tool->commit();
    emit editStepsChanged();
}

QPointF CanvasView::lineStartOf(const std::vector<brush::BrushInput> &points) const
{
    // The last point that shows: a stroke's end may thin out or fade to almost nothing, and a line
    // starting there would seem to start in the blank. Back from the end to the last point whose pixel
    // is at least half as opaque as the stroke's most opaque one (an eraser leaves nothing to look at).
    const QPointF last(points.back().x, points.back().y);
    if (!m_strokeImage || m_eraseStroke)
        return last;
    auto alphaAt = [this](const brush::BrushInput &in) {
        return qAlpha(m_strokeImage->pixel(int(std::floor(in.x)), int(std::floor(in.y))));
    };
    int peak = 0;
    for (const brush::BrushInput &in : points)
        peak = std::max(peak, alphaAt(in));
    if (peak == 0)
        return last;
    for (auto it = points.rbegin(); it != points.rend(); ++it)
        if (alphaAt(*it) * 2 >= peak)
            return QPointF(it->x, it->y);
    return last;
}

void CanvasView::updateStraightLine()
{
    const bool armed = m_shiftHeld && m_lastPoint && !m_stroke && !m_gesture.isActive() && !panKeyHeld()
                       && ToolState::isPaintTool(m_toolState->activeTool());
    if (armed != m_lineArmed) {
        m_lineArmed = armed;
        update();
    }
}

void CanvasView::placeStraightLine(const PenSample &touch)
{
    // The line goes from the last point to where the pen touched, at full pressure.
    if (!m_lastPoint)
        return;
    const QPointF from = m_view.documentToView().map(*m_lastPoint);
    PenSample s = touch;
    s.pressure = 1.0f;
    m_placingLine = true;
    PenSample start = s;
    start.x = from.x();
    start.y = from.y();
    beginStroke(start);
    if (m_stroke) {
        constexpr double kSpacing = 3.0;
        constexpr int kMaxSteps = 4000;
        const int steps = std::clamp(int(QLineF(from, QPointF(touch.x, touch.y)).length() / kSpacing), 1, kMaxSteps);
        for (int i = 1; i <= steps; ++i) {
            const double k = double(i) / steps;
            PenSample step = s;
            step.x = from.x() + (touch.x - from.x()) * k;
            step.y = from.y() + (touch.y - from.y()) * k;
            continueStroke(step);
        }
        endStroke();
    }
    m_placingLine = false;
    updateStraightLine(); // Shift still held: the next line starts where this one ended
    update();
}

void CanvasView::paintStraightLinePreview(QPainter &p) const
{
    if (!m_lineArmed || !m_lastPoint || !m_pointerVisible)
        return;
    const QPointF from = m_view.documentToView().map(*m_lastPoint);
    const bool eraser = m_pointerTool == InputTool::Eraser || m_toolState->activeTool() == ToolId::Eraser;
    const double size = m_toolState->settings(eraser ? ToolId::Eraser : ToolId::Brush).size;
    QPainterPath line;
    line.moveTo(from);
    line.lineTo(m_pointerPos + QPointF(0.01, 0));
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(2.0, size * m_view.zoom()));
    stroker.setCapStyle(Qt::RoundCap);
    const QPainterPath body = stroker.createStroke(line);
    QColor fill = eraser ? QColor(128, 128, 128) : m_toolState->color();
    fill.setAlpha(110);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(fill);
    p.setPen(QPen(QColor(0, 0, 0, 150), 1.0));
    p.drawPath(body);
    p.restore();
}

void CanvasView::cancelStroke()
{
    m_gizmo.cancel();
    m_cameraGizmo.cancel();
    if (m_editTool) {
        // A drag cut short is dropped, unless it belongs to a longer edit.
        CanvasTool *tool = m_editTool;
        m_editTool = nullptr;
        if (!tool->hasPendingEdit())
            tool->cancel();
        update();
    }
    // Points of a polygon or curve belong to the layer they were placed on: dropped.
    m_shapeTool->cancel();
    // An open edit is applied: undo then takes it back, a frame or layer
    // change keeps it.
    for (CanvasTool *tool : {m_moveTool.get(), m_rectSelectTool.get(), m_lassoSelectTool.get(), m_puppetTool.get(),
                              m_transformTool.get()})
        if (tool->hasPendingEdit())
            tool->commit();
    emit editStepsChanged();
    if (!m_stroke)
        return;
    // Restore pixels first: cancelPaint() may remove the drawing they live in.
    m_stroke->cancel();
    m_snapHeld.clear();
    m_snapRaw.clear();
    m_snapping = false;
    m_stroke.reset();
    m_strokeImage = nullptr;
    m_dabs.reset();
    m_heldSamples.clear();
    m_document->cancelPaint();
    renderWholeFrame();
}

brush::BrushInput CanvasView::brushInput(const PenSample &smoothed)
{
    brush::BrushInput in;
    const QPointF doc = m_view.mapToDocument(QPointF(smoothed.x, smoothed.y));
    in.x = doc.x();
    in.y = doc.y();
    in.pressure = m_placingLine ? 1.0 : m_toolState->pressureCurve().map(smoothed.pressure);
    in.velocity = m_velocity.sample(smoothed.t, smoothed.x, smoothed.y);
    in.hasTilt = m_toolState->usesTilt();
    in.tilt = in.hasTilt ? brush::tiltInput(smoothed.tiltX, smoothed.tiltY) : 1.0;
    // Barrel rotation when the pen reports it, else the direction it leans to.
    const bool leaning = in.hasTilt && (smoothed.tiltX != 0.0f || smoothed.tiltY != 0.0f);
    in.penAngle = smoothed.rotation != 0.0f || !leaning
                      ? double(smoothed.rotation)
                      : std::atan2(double(smoothed.tiltY), double(smoothed.tiltX)) * 180.0 / std::numbers::pi;
    return in;
}

// --- keyboard / wheel navigation --------------------------------------------

void CanvasView::wheelEvent(QWheelEvent *event)
{
    // Some platforms turn Shift+wheel into horizontal scrolling: accept either axis.
    const QPoint a = event->angleDelta();
    const int delta = a.y() != 0 ? a.y() : a.x();
    if (CanvasTool *tool = canvasTool(); tool && tool->wheel(delta / 120.0, event->modifiers())) {
        update();
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier)
        m_view.rotateAround(event->position(), delta > 0 ? -double(Preferences::instance().rotateStep()) : double(Preferences::instance().rotateStep()));
    else
        m_view.zoomAround(event->position(), std::pow(1.0015, delta));
    viewWasChanged();
    event->accept();
}

bool CanvasView::event(QEvent *event)
{
    // The pointer comes back (from a panel, a floating bar over the canvas) or the window is
    // activated again: the cursor is set again, whatever the system left there.
    if (event->type() == QEvent::Enter || event->type() == QEvent::WindowActivate)
        updateCursor();
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (const CanvasTool *tool = canvasTool(); tool && key->modifiers() == Qt::NoModifier && tool->wantsKey(key->key())) {
            event->accept(); // no shortcut: it comes as a key press
            return true;
        }
    }
    return QWidget::event(event);
}

QRectF CanvasView::transformBounds() const
{
    const auto *tool = static_cast<const TransformTool *>(m_transformTool.get());
    if (!tool->hasPendingEdit())
        return {};
    return tool->transform().mapRect(QRectF(tool->box()));
}

void CanvasView::finishTransform(bool apply)
{
    if (m_transformTool->keyPress(apply ? Qt::Key_Return : Qt::Key_Escape))
        emit editStepsChanged();
}

QRectF CanvasView::puppetBounds() const
{
    return static_cast<const PuppetTool *>(m_puppetTool.get())->pendingBounds();
}

void CanvasView::finishPuppet(bool apply)
{
    if (m_puppetTool->keyPress(apply ? Qt::Key_Return : Qt::Key_Escape))
        emit editStepsChanged();
}

QRectF CanvasView::shapeBounds() const
{
    return static_cast<const ShapeTool *>(m_shapeTool.get())->pendingBounds();
}

bool CanvasView::shapeCanFinish() const
{
    return static_cast<const ShapeTool *>(m_shapeTool.get())->canFinish();
}

void CanvasView::finishShape(bool apply)
{
    if (m_shapeTool->keyPress(apply ? Qt::Key_Return : Qt::Key_Escape))
        emit editStepsChanged();
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    // Shift held: the straight line from the last point (see updateStraightLine()).
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        m_shiftHeld = true;
        if (!m_lastPoint && ToolState::isPaintTool(m_toolState->activeTool()) && !m_stroke)
            emit statusMessage(tr("Draw something first: Shift then draws a straight line from the last point"));
        updateStraightLine();
    }
    // Repeats too: a held key may keep stepping (the puppet radius).
    if (CanvasTool *tool = canvasTool(); tool && tool->keyPress(event->key())) {
        event->accept();
        emit editStepsChanged();
        return;
    }
    if (bool *held = panKeyFlag(event->key())) {
        if (!event->isAutoRepeat()) {
            *held = true;
            updateCursor();
            updateStraightLine();
        }
        return;
    }
    // A, during a snapped stroke: the next axis (X, Y, Z...).
    if (event->key() == Qt::Key_A && !event->isAutoRepeat() && m_snapping && m_stroke) {
        cycleSnapAxis();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Alt && !m_altHeld) {
        m_altHeld = true;
        updateCursor();
    }
    QWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        m_shiftHeld = false;
        updateStraightLine();
    }
    if (bool *held = panKeyFlag(event->key())) {
        if (!event->isAutoRepeat()) {
            *held = false;
            updateCursor();
            updateStraightLine();
        }
        return;
    }
    if (event->key() == Qt::Key_Alt && m_altHeld) {
        m_altHeld = false;
        updateCursor();
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasView::updateAnts()
{
    if (!m_document->hasSelection())
        m_antsTimer->stop();
    else if (!m_antsTimer->isActive())
        m_antsTimer->start();
    update();
}

bool CanvasView::picksColor() const
{
    // The key flag for the cursor; the modifier too, for an Alt pressed
    // while the focus was elsewhere.
    const bool alt = m_altHeld || (QGuiApplication::keyboardModifiers() & Qt::AltModifier);
    const ToolId tool = m_toolState->activeTool();
    return alt && (ToolState::isPaintTool(tool) || tool == ToolId::Fill);
}

QColor CanvasView::colorAt(const QPointF &docPos) const
{
    const QImage &frame = m_frame.image();
    const QPoint p(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    return frame.rect().contains(p) ? frame.pixelColor(p) : QColor();
}

bool *CanvasView::panKeyFlag(int key)
{
    switch (key) {
    case Qt::Key_Space: return &m_spaceHeld;
    case Qt::Key_Tab: return &m_tabHeld;
    default: return nullptr;
    }
}

void CanvasView::focusOutEvent(QFocusEvent *event)
{
    // The release of a key held while focus moves away never reaches us.
    if (m_shiftHeld) {
        m_shiftHeld = false;
        updateStraightLine();
    }
    if (panKeyHeld() || m_altHeld) {
        m_spaceHeld = m_tabHeld = m_altHeld = false;
        updateCursor();
    }
    QWidget::focusOutEvent(event);
}

void CanvasView::updateGizmoCursor()
{
    if ((m_gizmo.hovered() != PerspectiveGizmo::Handle::None || m_cameraGizmo.hovered() != CameraGizmo::Handle::None)
        && !panKeyHeld())
        setCursor(Qt::PointingHandCursor);
    else
        updateCursor();
}

void CanvasView::syncWindowCursor()
{
    QWindow *handle = window() ? window()->windowHandle() : nullptr;
    if (!handle)
        return;
    const QCursor wanted = cursor();
    const QCursor now = handle->cursor();
    const bool same = now.shape() == wanted.shape()
                      && (wanted.shape() != Qt::BitmapCursor || now.pixmap().cacheKey() == wanted.pixmap().cacheKey());
    if (!same)
        handle->setCursor(wanted);
}

void CanvasView::updateCursor()
{
    if (panKeyHeld() || m_toolState->activeTool() == ToolId::Hand)
        setCursor(Qt::OpenHandCursor);
    else if (m_toolState->activeTool() == ToolId::Zoom)
        setCursor(zoomCursor());
    else if (picksColor())
        setCursor(Qt::CrossCursor);
    else if (ToolState::editsPixels(m_toolState->activeTool()) && m_document->drawingBlocked())
        setCursor(Qt::ForbiddenCursor); // nothing to draw on: a cel, a frame or the camera is selected
    else if (ToolState::isPaintTool(m_toolState->activeTool()) && Preferences::instance().brushOutline())
        setCursor(invisibleCursor()); // the brush's own outline, drawn in paintEvent(), is the cursor
    else if (const CanvasTool *tool = canvasTool())
        setCursor(tool->cursor());
    else
        setCursor(Qt::CrossCursor);
    update();
}

} // namespace mothion::canvas
