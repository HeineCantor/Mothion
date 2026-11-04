#include "canvas/CanvasView.h"

#include "canvas/DebugOverlay.h"
#include "canvas/EyedropperTool.h"
#include "canvas/FillTool.h"
#include "canvas/MoveTool.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionTool.h"
#include "canvas/TransformTool.h"
#include "document/DocumentController.h"
#include "input/TabletInputAdapter.h"
#include "tools/ToolState.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QRandomGenerator>
#include <QTabletEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

using Mode = NavigationGesture::Mode;
using tools::ToolId;
using tools::ToolState;

namespace {
constexpr double kZoomStep = 1.25;
constexpr double kRotateStep = 15.0;
// How often the selection outline's black and white dashes swap.
constexpr int kAntsIntervalMs = 200;
} // namespace

CanvasView::CanvasView(ToolState *toolState, document::DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_toolState(toolState)
    , m_document(document)
    , m_gizmo(document)
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
            [this](doc::CelId, doc::DrawingId drawing, const QRect &rect) {
                m_onionImages.erase(drawing);
                renderDocumentRect(rect);
            });
    connect(m_document, &DocumentController::structureChanged, this, [this] { m_onionImages.clear(); });
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
        m_document, m_toolState, [this](const QString &text) { emit statusMessage(text); }, [this] { update(); });
    m_eyedropperTool =
        std::make_unique<EyedropperTool>(m_toolState, [this](const QPointF &docPos) { return colorAt(docPos); });
    m_fillTool = std::make_unique<FillTool>(
        m_document, m_toolState, [this]() -> const QImage & { return m_frame.image(); },
        [this](const QString &text) { emit statusMessage(text); });
    m_transformTool = std::make_unique<TransformTool>(
        m_document, m_toolState, [this](const QString &text) { emit statusMessage(text); }, [this] { update(); });
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
    connect(m_toolState, &ToolState::activeToolChanged, this, [this] {
        if (CanvasTool *tool = canvasTool()) {
            tool->setViewZoom(m_view.zoom());
            tool->activated();
        }
    });
    connect(m_toolState, &ToolState::toolSettingsChanged, this, [this] { updateBrushOutline(m_pointerPos); });
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
    paintOnionSkin(p);
    p.restore();

    paintSelectionOutline(p, m_view, m_document->selection(), m_antsPhase);
    if (const CanvasTool *tool = canvasTool()) {
        p.save();
        tool->paintOverlay(p, m_view);
        p.restore();
    }
    m_gizmo.paint(p, m_view);

    m_paintedOutline = QRect();
    if (m_pointerVisible && ToolState::isPaintTool(m_toolState->activeTool()) && !m_gesture.isActive()
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
    updateBrushOutline(QPointF(sample.x, sample.y));

    const bool busy = m_gesture.isActive() || m_stroke.has_value() || m_editTool || m_gizmo.isDragging();
    const QPointF docPos = m_view.mapToDocument(QPointF(sample.x, sample.y));
    const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    switch (sample.phase) {
    case SamplePhase::Press:
        if (busy)
            break; // a second button during a drag: keep doing what we do
        if (const Mode mode = navigationModeFor(sample); mode != Mode::None) {
            m_gesture.begin(mode, QPointF(sample.x, sample.y), m_view);
        } else if (sample.tipDown() && m_gizmo.press(docPos, m_view)) {
            update(); // a handle of the perspective layer, whatever the tool
        } else if (picksColor() && sample.tipDown() && sample.tool != InputTool::Eraser) {
            m_eyedropperTool->press(docPos, modifiers);
            m_editTool = m_eyedropperTool.get();
        } else if (sample.tipDown()
                   && (sample.tool == InputTool::Eraser || ToolState::isPaintTool(m_toolState->activeTool()))) {
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
        if (!busy && m_gizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (CanvasTool *tool = canvasTool(); tool && !busy && tool->hover(docPos))
            update();
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
    m_eraseStroke = sample.tool == InputTool::Eraser || m_toolState->activeTool() == ToolId::Eraser;
    // Erasing an empty frame has nothing to act on, so only the brush creates drawings.
    QString reason;
    const auto target = m_document->beginPaint(!m_eraseStroke, &reason);
    if (!target) {
        if (!reason.isEmpty())
            emit statusMessage(reason);
        return;
    }
    const ToolId tool = m_eraseStroke ? ToolId::Eraser : ToolId::Brush;
    const tools::BrushSettings settings = m_toolState->settings(tool);
    const tools::BrushDefinitionPtr definition = m_toolState->brush(tool);
    brush::BrushStyle style = definition->style;
    style.size = settings.size / 2.0;
    // The start rises live or once the stroke is done, not both.
    if (m_toolState->correctsStartAfterStroke())
        style.startTaper = 0.0;
    else
        style.startCorrection = 0;
    auto stamper = std::make_shared<DabStamper>(m_toolState->color(), style.hardness, definition->tips,
                                                style.verticalThickness);
    stamper->setSecondaryColor(m_toolState->secondaryColor());
    const doc::Document &d = m_document->document();
    m_stroke.emplace(*target->image, std::move(stamper), settings.opacity, m_eraseStroke,
                     QRect(0, 0, d.width(), d.height()));
    m_stroke->setStyle(style);
    m_seed = QRandomGenerator::global()->generate();
    m_dabs.emplace(style, m_seed);
    m_points.clear();
    m_drawnPoints = 0;

    m_smoother.setStrength(m_toolState->smoothing());
    m_smoother.reset();
    m_velocity.reset();
    // With the correction after the stroke, corners are found when the pen lifts.
    m_sharpCorners = m_toolState->sharpCorners() && style.revision == 0;
    m_corners.reset();
    m_heldSamples.clear();
    takeSample(sample);
}

void CanvasView::continueStroke(const PenSample &sample)
{
    takeSample(sample);
}

void CanvasView::takeSample(const PenSample &sample)
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
    m_document->endPaint(m_stroke->before());
    m_stroke.reset();
    m_dabs.reset();
    m_points.clear();
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
    default: return nullptr;
    }
}

void CanvasView::commitPendingEdits()
{
    // An edit left open (a puppet warp) is applied when another tool takes over.
    for (CanvasTool *tool : {m_moveTool.get(), m_rectSelectTool.get(), m_lassoSelectTool.get(), m_puppetTool.get(),
                              m_transformTool.get()})
        if (tool != canvasTool() && tool->hasPendingEdit())
            tool->commit();
    emit editStepsChanged();
}

void CanvasView::cancelStroke()
{
    m_gizmo.cancel();
    if (m_editTool) {
        // A drag cut short is dropped, unless it belongs to a longer edit.
        CanvasTool *tool = m_editTool;
        m_editTool = nullptr;
        if (!tool->hasPendingEdit())
            tool->cancel();
        update();
    }
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
    m_stroke.reset();
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
    in.pressure = m_toolState->pressureCurve().map(smoothed.pressure);
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
        m_view.rotateAround(event->position(), delta > 0 ? -kRotateStep : kRotateStep);
    else
        m_view.zoomAround(event->position(), std::pow(1.0015, delta));
    viewWasChanged();
    event->accept();
}

bool CanvasView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (const CanvasTool *tool = canvasTool(); tool && key->modifiers() == Qt::NoModifier && tool->wantsKey(key->key())) {
            event->accept(); // no shortcut: it comes as a key press
            return true;
        }
    }
    return QWidget::event(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
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
        }
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
    if (bool *held = panKeyFlag(event->key())) {
        if (!event->isAutoRepeat()) {
            *held = false;
            updateCursor();
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
    if (panKeyHeld() || m_altHeld) {
        m_spaceHeld = m_tabHeld = m_altHeld = false;
        updateCursor();
    }
    QWidget::focusOutEvent(event);
}

void CanvasView::updateGizmoCursor()
{
    if (m_gizmo.hovered() != PerspectiveGizmo::Handle::None && !panKeyHeld())
        setCursor(Qt::PointingHandCursor);
    else
        updateCursor();
}

void CanvasView::updateCursor()
{
    if (panKeyHeld() || m_toolState->activeTool() == ToolId::Hand)
        setCursor(Qt::OpenHandCursor);
    else if (m_toolState->activeTool() == ToolId::Zoom)
        setCursor(Qt::SizeHorCursor);
    else if (picksColor())
        setCursor(Qt::CrossCursor);
    else if (const CanvasTool *tool = canvasTool())
        setCursor(tool->cursor());
    else
        setCursor(Qt::CrossCursor);
    update();
}

} // namespace mothion::canvas
