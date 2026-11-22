#include "canvas/CanvasView.h"

#include "ui/Checkerboard.h"

#include "canvas/DebugOverlay.h"
#include "canvas/EyedropperTool.h"
#include "canvas/Symmetry.h"
#include "canvas/VectorPointTool.h"
#include "canvas/EffectBrushTools.h"
#include "canvas/FillTool.h"
#include "canvas/MoveTool.h"
#include "canvas/PuppetTool.h"
#include "canvas/SelectionMask.h"
#include "canvas/SelectionTool.h"
#include "canvas/VectorEraseTool.h"
#include "canvas/VectorRender.h"
#include "canvas/AnimateTool.h"
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
#include <QPainterPath>
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
    , m_trajectoryGizmo(document)
    , m_boneGizmo(document)
    , m_curveGizmo(document)
    , m_symmetryGizmo(document, toolState)
    , m_overlay(new DebugOverlay(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);  // hover moves from the mouse
    setTabletTracking(true); // hover moves from the pen (needed for proximity/outline/overlay)
    setAttribute(Qt::WA_OpaquePaintEvent);
    const doc::Document &d = m_document->document();
    m_view.setDocumentSize(QSizeF(d.width(), d.height()));
    // (the frame shown is drawn at the first paint, for the part of the canvas seen: see syncFrameArea())
    connect(&Preferences::instance(), &Preferences::changed, this, qOverload<>(&QWidget::update)); // out of the scene

    using document::DocumentController;
    connect(m_document, &DocumentController::pixelsChanged, this,
            [this](doc::CelId celId, doc::DrawingId drawing, const QRect &rect) {
                m_onionImages.erase(drawing);
                static_cast<AnimateTool *>(m_animateTool.get())->invalidate(); // its box is of the pixels
                // Only a drawing shown at the current frame changes the frame; one of another frame (a
                // transform of a whole cel tells of every one of them) at most the onion skin, painted over it.
                const doc::Cel *cel = m_document->document().cel(celId);
                // doc::kNoId: a change of the cel itself (its opacity, say), which every drawing of it shows.
                // The current cel's is what the tools work on (a mouth of a mouth chart drawn off the timeline, say).
                const doc::DrawingId shown = !cel                                    ? doc::kNoId
                                             : celId == m_document->currentCel() ? m_document->currentDrawingId()
                                                                                 : cel->timing().drawingAt(m_document->currentFrame());
                if (cel && shown != doc::kNoId && (drawing == doc::kNoId || shown == drawing)) {
                    // A cel moved by its keys shows these pixels somewhere else: where the pose puts them.
                    if (cel->id() == m_document->currentCel())
                        renderDocumentRect(posedRect(rect));
                    else if (!cel->motion().isEmpty() || m_document->document().followedTrajectory(*cel))
                        renderWholeFrame();
                    else
                        renderDocumentRect(rect);
                }
                else if (m_toolState->onionSkin().enabled)
                    update();
            });
    connect(m_document, &DocumentController::structureChanged, this, [this] {
        m_onionImages.clear();
        static_cast<AnimateTool *>(m_animateTool.get())->invalidate();
    });
    // The camera's frame follows it (a drag, a key, the frame of the timeline, it being selected or turned off).
    connect(m_document, &DocumentController::cameraChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::cameraSelectionChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::documentReplaced, this, [this] { m_onionImages.clear(); });
    connect(m_document, &DocumentController::currentCelChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::currentLayerChanged, this, qOverload<>(&QWidget::update));
    connect(m_toolState, &ToolState::onionSkinChanged, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::structureChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::motionChanged, this, [this] { // a key or a pivot: the frame as it is posed
        syncPoseMode();
        m_onionImages.clear();
        renderWholeFrame();
    });
    connect(m_document, &DocumentController::currentFrameChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::mouthEditChanged, this, &CanvasView::renderWholeFrame);
    connect(m_document, &DocumentController::currentCelChanged, this, &CanvasView::syncPoseMode);
    connect(m_document, &DocumentController::celSelectionChanged, this, &CanvasView::syncPoseMode);
    connect(m_document, &DocumentController::documentReplaced, this, &CanvasView::syncPoseMode);
    connect(m_document, &DocumentController::strokeInterrupted, this, &CanvasView::cancelStroke);
    connect(m_document, &DocumentController::poseEditClosing, this, [this] {
        if (m_animateTool->hasPendingEdit()) {
            m_animateTool->commit();
            emit editStepsChanged();
            emit transformChanged();
        }
    });
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
        m_document, m_toolState, [this](const QPoint &seed, bool withImage) { return fillShown(seed, withImage); },
        [this](const QString &text) { emit statusMessage(text); },
        [this](const QPointF &celPos) {
            QPointF onPage = celPos; // the copies by where the fill begins on the page (a tile's cell)
            if (celIsPosed()) {
                const doc::Matrix2x3 m = celPose();
                onPage = QTransform(m.a, m.b, m.c, m.d, m.e, m.f).map(celPos);
            }
            return symmetryCopies(onPage);
        });
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
    m_vectorEraseTool = std::make_unique<VectorEraseTool>(m_document, m_toolState,
                                                          [this](const QString &text) { emit statusMessage(text); });
    m_vectorPointTool = std::make_unique<VectorPointTool>(m_document, m_toolState,
                                                          [this](const QString &text) { emit statusMessage(text); });
    connect(m_toolState, &ToolState::vectorPointsChanged, this, [this] { update(); });
    connect(m_toolState, &ToolState::symmetryChanged, this, [this] { update(); });
    connect(m_document, &DocumentController::symmetryAxesChanged, this, [this] { update(); });
    m_animateTool = std::make_unique<AnimateTool>(
        m_document, m_toolState, [this](const QString &text) { emit statusMessage(text); }, [this] {
            update();
            emit transformChanged();
        });
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
    // Other layers (a cel, a frame...) chosen while a transform is open: it was made for the layers there were, so what is
    // done is applied and the box comes up again round the layers that are chosen now.
    connect(m_document, &DocumentController::layerSelectionChanged, this, [this] {
        if (m_toolState->activeTool() != ToolId::Transform || m_toolState->keyframeMode() || m_transformTool->isActive())
            return;
        if (m_transformTool->hasPendingEdit())
            m_transformTool->commit();
        m_transformTool->activated();
        update();
    });
    // A paste floats in the Transform tool: what was open is applied, and the pasted pixels (or strokes) come up in its
    // box, ready to be moved, scaled or turned without taking what lies under them along.
    connect(m_document, &DocumentController::floatingPasteStarted, this, [this] {
        if (m_toolState->keyframeMode()) { // (the Transform tool moves the cel then: the paste goes in as it is)
            if (const auto pasted = m_document->takeFloatingPaste())
                m_document->pasteImage(pasted->image, pasted->at);
            return;
        }
        for (CanvasTool *tool : {m_moveTool.get(), m_rectSelectTool.get(), m_lassoSelectTool.get(), m_puppetTool.get(),
                                  m_shapeTool.get(), m_transformTool.get(), m_animateTool.get()})
            if (tool->hasPendingEdit())
                tool->commit();
        if (m_toolState->activeTool() == ToolId::Transform) {
            m_transformTool->setViewZoom(m_view.zoom());
            m_transformTool->activated();
        } else {
            m_toolState->setActiveTool(ToolId::Transform);
        }
        update();
    });
    // Trajectory mode: the path of the trajectory is shown and its keys dragged (and nothing is drawn meanwhile).
    m_trajectoryGizmo.setPathVisible(m_toolState->trajectoryMode());
    // The Transform tool: a box round the keys chosen on the path, to move, turn and scale them together.
    m_trajectoryGizmo.setKeyBoxEnabled(m_toolState->activeTool() == ToolId::Transform);
    connect(m_toolState, &ToolState::activeToolChanged, this, [this] {
        m_trajectoryGizmo.setKeyBoxEnabled(m_toolState->activeTool() == ToolId::Transform);
        update();
    });
    connect(m_document, &DocumentController::motionPreviewChanged, this, [this] { update(); }); // keys dragged on the timeline
    // Rig mode: the bones of a Bones deformer at rest, to be made and placed; off, posed and animated.
    m_boneGizmo.setRigMode(m_toolState->rigMode());
    m_frame.setBrushes(m_toolState); // a vector layer bent by a deformer is drawn again from its strokes
    m_boneGizmo.setRepaint([this] { update(); });
    m_curveGizmo.setRigMode(m_toolState->rigMode());
    m_curveGizmo.setRepaint([this] { update(); });
    connect(m_toolState, &ToolState::rigModeChanged, this, [this](bool on) {
        m_boneGizmo.cancel();
        m_boneGizmo.setRigMode(on);
        m_curveGizmo.cancel();
        m_curveGizmo.setRigMode(on);
        update();
    });
    connect(m_toolState, &ToolState::trajectoryModeChanged, this, [this](bool on) {
        m_trajectoryGizmo.setPathVisible(on);
        updateStraightLine(); // no straight line while a path is shown
        update();
    });
    connect(m_toolState, &ToolState::activeToolChanged, this, &CanvasView::renderWholeFrame); // posed or as drawn
    // The Transform and Move tools move the pixels, or (the cel has keys) the cel as a piece, with a key: another tool then
    // is the one at work (what the first left open is applied), and the cel is shown posed or as drawn.
    connect(m_toolState, &ToolState::activeToolChanged, this, &CanvasView::syncAnimateTool);
    connect(m_toolState, &ToolState::keyframeModeChanged, this, [this] {
        syncAnimateTool();
        commitPendingEdits();
        if (CanvasTool *tool = canvasTool()) {
            tool->setViewZoom(m_view.zoom());
            tool->activated();
        }
        renderWholeFrame();
        updateCursor();
    });
    connect(m_document, &DocumentController::currentCelChanged, this, &CanvasView::renderWholeFrame);
    connect(m_toolState, &ToolState::activeToolChanged, this, &CanvasView::updateCursor);
    for (auto signal : {&DocumentController::layerChosenChanged, &DocumentController::cameraSelectionChanged,
                        &DocumentController::structureChanged, &DocumentController::documentReplaced})
        connect(m_document, signal, this, &CanvasView::updateCursor); // may now (not) be drawable
    connect(m_document, &DocumentController::currentFrameChanged, this, &CanvasView::updateCursor);
    connect(m_document, &DocumentController::currentCelChanged, this, &CanvasView::updateCursor);
    connect(m_document, &DocumentController::currentCelChanged, this, &CanvasView::updateStraightLine); // a path shown or not
    // The path's keys chosen (an undo or a redo too): their dots and their box are drawn again.
    connect(m_document, &DocumentController::pathKeysChosen, this, qOverload<>(&QWidget::update));
    connect(m_document, &DocumentController::toolChoiceRestored, this, qOverload<>(&QWidget::update));
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
    connect(&Preferences::instance(), &Preferences::changed, this, [this] { // the outline on or off, the filter
        static_cast<TransformTool *>(m_transformTool.get())->refreshRendering();
        updateCursor();
        update();
    });
    updateCursor();
    syncPoseMode(); // a document that comes with keys
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
        // A ghost is the cel as it is on its own frame: posed by its keys, unless the cel is shown as drawn (to line pixels up).
        const bool posed = !cel->motion().isEmpty() && m_frame.celAsDrawn() != cel->id();
        const doc::CelPose pose = posed ? cel->motion().poseAt(m_document->currentFrame() + distance) : doc::CelPose{};
        const doc::Matrix2x3 follow = m_frame.celAsDrawn() != cel->id()
                                          ? m_document->document().followMatrix(*cel, m_document->currentFrame() + distance)
                                          : doc::Matrix2x3{};
        p.setOpacity(s.opacity * fade * pose.opacity);
        if ((posed && !pose.movesNothing()) || !follow.isIdentity()) {
            const doc::Matrix2x3 m = pose.matrix(cel->motion().pivotX, cel->motion().pivotY).then(follow);
            p.save();
            p.setWorldTransform(QTransform(m.a, m.b, m.c, m.d, m.e, m.f) * p.worldTransform());
            p.drawImage(ghost.origin, ghost.tinted);
            p.restore();
        } else {
            p.drawImage(ghost.origin, ghost.tinted);
        }
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
    syncFrameArea();
    // The paper goes on all over the canvas (it has no end); the size of the project is the frame of the scene.
    const doc::Document &d = m_document->document();
    const QRectF page(0, 0, d.width(), d.height());
    const QColor paper = QColor::fromRgba(d.paperColor());

    // Smooth scaling only when zoomed out; nearest-neighbour shows real pixels up close.
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_view.zoom() < 2.0);
    if (paper.alpha() < 255) // a see-through paper: squares behind it
        ui::paintCheckerboard(p, QRectF(rect()));
    p.save();
    p.setTransform(m_view.documentToView());
    // Only the part of the image seen (the rest is out of the widget).
    const QRect have = m_frame.area();
    const int st = m_frame.step();
    const QRect seenArea = visibleDocumentRect().adjusted(-2, -2, 2, 2) & have;
    const QRect source(QPoint((seenArea.left() - have.left()) / st, (seenArea.top() - have.top()) / st),
                       QPoint((seenArea.right() - have.left()) / st, (seenArea.bottom() - have.top()) / st));
    const QRectF shown(have.left() + source.left() * st, have.top() + source.top() * st, source.width() * st,
                       source.height() * st);
    const double outside = Preferences::instance().outsideScenePercent() / 100.0;
    if (outside >= 1.0) {
        p.drawImage(shown, m_frame.image(), source); // paper included
    } else {
        // Out of the scene's frame: the paper, and what is drawn on it as faint as chosen (not at all: hidden).
        QPainterPath out;
        out.addRect(shown);
        QPainterPath in;
        in.addRect(page);
        p.setClipPath(out.subtracted(in));
        p.fillRect(shown, paper);
        if (outside > 0.0) {
            p.setOpacity(outside);
            p.drawImage(shown, m_frame.image(), source);
            p.setOpacity(1.0);
        }
        p.setClipRect(page);
        p.drawImage(shown, m_frame.image(), source);
        p.setClipping(false);
    }
    paintOnionSkin(p);
    p.restore();
    // The scene's frame: a line around the page, dark and light so that it shows on any paper.
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const QPolygonF frame = m_view.documentToView().map(page);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 140), 3.0));
        p.drawPolygon(frame);
        p.setPen(QPen(QColor(0, 0, 0, 200), 1.0));
        p.drawPolygon(frame);
        p.restore();
    }

    p.save();
    if (celIsPosed()) { // a selection is of the cel's own place: through the pose
        const doc::Matrix2x3 m = celPose();
        p.setWorldTransform(m_view.viewToDocument() * QTransform(m.a, m.b, m.c, m.d, m.e, m.f) * m_view.documentToView());
    }
    paintSelectionOutline(p, m_view, m_document->selection(), m_antsPhase);
    p.restore();
    if (const CanvasTool *tool = canvasTool()) {
        p.save();
        if (celIsPosed() && toolWorksInCel(tool)) { // what the tool shows is of the cel's own place: through the pose
            const QTransform pose(celPose().a, celPose().b, celPose().c, celPose().d, celPose().e, celPose().f);
            p.setWorldTransform(m_view.viewToDocument() * pose * m_view.documentToView());
        }
        tool->paintOverlay(p, m_view);
        p.restore();
    }
    m_gizmo.paint(p, m_view);
    m_cameraGizmo.paint(p, m_view);
    m_trajectoryGizmo.paintPath(p, m_view); // the whole way of the trajectory, under its circle
    m_trajectoryGizmo.paint(p, m_view);
    m_boneGizmo.paint(p, m_view);
    m_curveGizmo.paint(p, m_view);
    m_symmetryGizmo.paint(p, m_view);
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
    const double size = m_toolState->settings(eraser ? ToolId::Eraser : m_toolState->brushTool()).size;
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

void CanvasView::syncCelAsDrawn()
{
    // A cel moved by its keys is always shown moved, at rest and with a pixel tool too: the pen works on it as shown (see
    // toCel()).
    m_frame.setCelAsDrawn(doc::kNoId);
    // A cel bent by a deformer is shown as drawn while it is the current one, so that the pen paints where its pixels are;
    // the bend shows when its deformer (or another cel) is the current one, and while playing.
    m_frame.setCelUndeformed(m_playing || m_scrubbing ? doc::kNoId : m_document->currentCel());
    // A mouth of a mouth chart drawn off the timeline is what the canvas shows of the cel.
    m_frame.setDrawingShown(m_document->currentCel(), m_document->mouthDrawingEdited());
    m_page.setDrawingShown(m_document->currentCel(), m_document->mouthDrawingEdited());
}

doc::Matrix2x3 CanvasView::celPose() const
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!cel)
        return {};
    // Its own pose, then what the trajectory it follows does; before both, on a time layer, what that follows of its own.
    const doc::Matrix2x3 follow = m_document->document().followMatrix(*cel, m_document->currentFrame());
    const doc::Matrix2x3 own = timeLayerFollow();
    if (cel->motion().isEmpty())
        return own.then(follow);
    return own.then(m_document->celPoseAt(cel->id(), m_document->currentFrame())
                        .matrix(cel->motion().pivotX, cel->motion().pivotY)
                        .then(follow));
}

doc::Matrix2x3 CanvasView::timeLayerFollow() const
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    const doc::TimeLayer *time = cel ? cel->timeLayer(m_document->currentLayer()) : nullptr;
    if (!time || time->parent == doc::kNoId)
        return {};
    return m_document->document().followMatrix(doc::Document::timeLayerAsCel(*cel, *time), m_document->currentFrame());
}

QRect CanvasView::visibleDocumentRect() const
{
    return m_view.viewToDocument().mapRect(QRectF(rect())).toAlignedRect();
}

QImage CanvasView::frameArea(const QRect &area) const
{
    if (m_frame.step() == 1 && m_frame.area().contains(area) && !m_frameStale.intersects(area))
        return m_frame.image().copy(area.translated(-m_frame.area().topLeft()));
    FrameRenderer renderer;
    renderer.setDrawingShown(m_document->currentCel(), m_document->mouthDrawingEdited());
    renderer.setArea(m_document->document(), m_document->currentFrame(), area);
    return renderer.image();
}

std::vector<QTransform> CanvasView::symmetryCopies(const QPointF &pageStart) const
{
    const tools::SymmetrySettings symmetry = m_toolState->symmetry();
    if (!symmetry.on)
        return {};
    const doc::Document &d = m_document->document();
    const std::vector<QTransform> transforms = symmetryTransforms(
        symmetry, m_document->symmetryCenter(), m_document->symmetryAngle(), QSizeF(d.width(), d.height()), pageStart);
    // The axes are the page's: on a posed cel, from its place to the page, the copy, and back.
    QTransform pose;
    if (celIsPosed()) {
        const doc::Matrix2x3 m = celPose();
        pose = QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
    }
    const QTransform poseBack = pose.inverted();
    std::vector<QTransform> out;
    for (std::size_t i = 1; i < transforms.size(); ++i)
        out.push_back(pose * transforms[i] * poseBack);
    return out;
}

FillTool::Shown CanvasView::fillShown(const QPoint &seed, bool withImage) const
{
    // The page, and the part of the canvas seen (not more than so far around the seed, however far out it is zoomed: the
    // fill looks at every pixel of it).
    constexpr int kReach = 4096;
    const doc::Document &d = m_document->document();
    const bool posed = celIsPosed();
    const doc::Matrix2x3 pose = posed ? celPose() : doc::Matrix2x3{};
    const QTransform forward(pose.a, pose.b, pose.c, pose.d, pose.e, pose.f); // the cel's place to the page
    const QPoint seedOnPage = forward.map(QPointF(seed)).toPoint();
    const QRect seen = visibleDocumentRect() & QRect(seedOnPage - QPoint(kReach, kReach), QSize(2 * kReach, 2 * kReach));
    const QRect onPage = seen | QRect(0, 0, d.width(), d.height());
    const QTransform back = forward.inverted();
    FillTool::Shown shown;
    shown.area = posed ? back.mapRect(QRectF(onPage)).toAlignedRect() : onPage;
    if (!withImage)
        return shown;
    // What is shown, seen from the cel's own pixels: the frame is as it is on the page, the cel is moved on it.
    const QRect pageArea = posed ? forward.mapRect(QRectF(shown.area)).toAlignedRect().adjusted(-2, -2, 2, 2) : onPage;
    const QImage frame = frameArea(pageArea);
    if (!posed) {
        shown.image = frame;
        return shown;
    }
    shown.image = QImage(shown.area.size(), QImage::Format_ARGB32_Premultiplied);
    shown.image.fill(Qt::transparent);
    QPainter p(&shown.image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setWorldTransform(QTransform::fromTranslate(pageArea.x(), pageArea.y()) * back
                        * QTransform::fromTranslate(-shown.area.x(), -shown.area.y()));
    p.drawImage(QPointF(0, 0), frame);
    return shown;
}

const QImage &CanvasView::documentImage() const
{
    const doc::Document &d = m_document->document();
    const QRect page(0, 0, d.width(), d.height());
    if (m_pageMade && m_pageStale.isEmpty() && m_pageImage.size() == page.size())
        return m_pageImage;
    if (m_frame.step() == 1 && m_frame.area().contains(page) && !m_frameStale.intersects(page)) {
        // The canvas shows all of the page as it is: no need to draw it again.
        m_pageImage = m_frame.image().copy(page.translated(-m_frame.area().topLeft()));
        m_pageFromFrame = true;
    } else {
        if (!m_pageMade || m_pageFromFrame || m_page.area() != page)
            m_page.renderAll(d, m_document->currentFrame());
        else
            m_page.render(d, m_document->currentFrame(), m_pageStale);
        m_pageImage = m_page.image();
        m_pageFromFrame = false;
    }
    m_pageMade = true;
    m_pageStale = QRect();
    return m_pageImage;
}

void CanvasView::syncFrameArea()
{
    // A pixel of the image for one of the screen or more: zoomed out, a pixel of it is 2, 4... of the document a side.
    const double zoom = m_view.zoom();
    int step = 1;
    while (step < 64 && zoom * step * 2 <= 1.0 + 1e-9)
        step *= 2;
    const QRect seen = visibleDocumentRect();
    if (seen.isEmpty())
        return;
    const QRect have = m_frame.area();
    const auto size = [](const QRect &r) { return qint64(r.width()) * qint64(r.height()); };
    syncCelAsDrawn();
    if (step != m_frame.step() || !have.contains(seen) || size(have) > 4 * size(seen)) {
        // Some more all around, so that a pan draws only now and then (and then only what comes into the image).
        const int mx = seen.width() / 4 + 64, my = seen.height() / 4 + 64;
        const int oldStep = m_frame.step();
        m_frame.setArea(m_document->document(), m_document->currentFrame(), seen.adjusted(-mx, -my, mx, my), step);
        // What comes new is drawn; what is kept may still be out of date.
        m_frameStale = step == oldStep ? m_frameStale & QRegion(have & m_frame.area()) : QRegion();
    }
    // What comes into sight that was left when the frame changed.
    const QRegion due = m_frameStale & QRegion(seen);
    for (const QRect &r : due)
        m_frame.render(m_document->document(), m_document->currentFrame(), r);
    m_frameStale -= due;
}

bool CanvasView::celIsPosed() const
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!cel)
        return false;
    if (!m_document->document().followMatrix(*cel, m_document->currentFrame()).isIdentity()
        || !timeLayerFollow().isIdentity())
        return true;
    return !cel->motion().isEmpty() && !m_document->celPoseAt(cel->id(), m_document->currentFrame()).movesNothing();
}

QPointF CanvasView::toCel(const QPointF &page) const
{
    if (!celIsPosed())
        return page;
    double x = 0, y = 0;
    celPose().inverted().map(page.x(), page.y(), x, y);
    return {x, y};
}

bool CanvasView::toolWorksInCel(const CanvasTool *tool) const
{
    return tool != m_animateTool.get() && tool != m_eyedropperTool.get();
}

QRect CanvasView::posedRect(const QRect &celRect) const
{
    if (!celIsPosed() || celRect.isEmpty())
        return celRect;
    const doc::Matrix2x3 m = celPose();
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const QPointF &c : {QPointF(celRect.left(), celRect.top()), QPointF(celRect.right() + 1, celRect.top()),
                             QPointF(celRect.right() + 1, celRect.bottom() + 1), QPointF(celRect.left(), celRect.bottom() + 1)}) {
        double x = 0, y = 0;
        m.map(c.x(), c.y(), x, y);
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    // A pixel or two more: the resampling of the pose reaches beyond the pixels it came from.
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).toAlignedRect().adjusted(-3, -3, 3, 3);
}

void CanvasView::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    m_onionImages.clear();
    renderWholeFrame(); // the cel as drawn, or as the animation has it
}

void CanvasView::setScrubbing(bool scrubbing)
{
    if (scrubbing == m_scrubbing)
        return;
    m_scrubbing = scrubbing;
    m_onionImages.clear();
    renderWholeFrame();
}

void CanvasView::syncAnimateTool()
{
    // The Move tool, when it moves the cel, is the animate tool without handles: a drag anywhere moves the cel.
    static_cast<AnimateTool *>(m_animateTool.get())->setMoveOnly(m_toolState->activeTool() == ToolId::Move);
}

void CanvasView::renderWholeFrame()
{
    syncCelAsDrawn();
    // What is seen now; the rest of the image when it comes into sight (a frame changed while scrubbing, say).
    const QRect seen = visibleDocumentRect() & m_frame.area();
    m_frame.render(m_document->document(), m_document->currentFrame(), seen);
    m_frameStale = QRegion(m_frame.area()) - QRegion(seen);
    const doc::Document &d = m_document->document();
    m_pageStale = QRect(0, 0, d.width(), d.height());
    update();
    emit documentChanged(m_pageStale);
    emit transformChanged(); // the box of a cel moved by its keys follows the frame
}

void CanvasView::renderDocumentRect(const QRect &docRect)
{
    // Re-composing all layers of the rect is correct whichever layer changed.
    syncCelAsDrawn();
    m_frame.render(m_document->document(), m_document->currentFrame(), docRect);
    m_frameStale -= QRegion(docRect & m_frame.area());
    m_pageStale |= docRect & QRect(0, 0, m_document->document().width(), m_document->document().height());
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
                      || m_cameraGizmo.isDragging() || m_trajectoryGizmo.isDragging() || m_symmetryGizmo.isDragging()
                      || m_boneGizmo.isDragging() || m_curveGizmo.isDragging()
                      || m_flip.has_value();
    const QPointF docPos = m_view.mapToDocument(QPointF(sample.x, sample.y));
    const QPointF celPos = toCel(docPos);
    // The place a tool is given: the cel's own, but for the tools that work on what is shown.
    auto at = [&](const CanvasTool *tool) { return tool && toolWorksInCel(tool) ? celPos : docPos; };
    const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    switch (sample.phase) {
    case SamplePhase::Press:
        if (busy)
            break; // a second button during a drag: keep doing what we do
        if (const Mode mode = navigationModeFor(sample); mode != Mode::None) {
            m_gesture.begin(mode, QPointF(sample.x, sample.y), m_view);
        } else if (sample.tipDown() && m_cameraGizmo.press(docPos, m_view)) {
            update(); // the camera selected: its frame or a handle, whatever the tool
        } else if (sample.tipDown()
                   && m_symmetryGizmo.press(docPos, m_view,
                                            (modifiers & Qt::ControlModifier)
                                                && (sample.tool == InputTool::Eraser
                                                    || ToolState::isPaintTool(m_toolState->activeTool())))) {
            update(); // the symmetry's center or knob, whatever the tool; Ctrl+drag with a brush brings the center
        } else if (sample.tipDown() && m_trajectoryGizmo.press(docPos, m_view, modifiers)) {
            update(); // the circle of the trajectory cel selected: its middle, its arrow or its edge, whatever the tool
        } else if (sample.tipDown() && m_boneGizmo.press(docPos, m_view, modifiers)) {
            update(); // the bones of a deformer: made and placed in rig mode, else turned, whatever the tool
        } else if (sample.tipDown() && m_curveGizmo.press(docPos, m_view, modifiers)) {
            update(); // the nodes of a curve or an envelope: the same
        } else if (sample.tipDown() && m_gizmo.press(docPos, m_view)) {
            update(); // a handle of the perspective layer, whatever the tool
        } else if (sample.tipDown() && m_trajectoryGizmo.pathShown()
                   && (m_toolState->activeTool() == ToolId::RectSelect || m_toolState->activeTool() == ToolId::LassoSelect)
                   && m_trajectoryGizmo.beginMarquee(docPos, m_toolState->activeTool() == ToolId::LassoSelect, modifiers)) {
            update(); // while a path is shown the selection tools choose its keys, to move them together
        } else if (sample.tipDown() && m_trajectoryGizmo.pathShown() && !m_trajectoryGizmo.chosenKeys().empty()) {
            m_trajectoryGizmo.clearChosenKeys(); // a click on nothing, with other tools: none of the path's keys chosen
            update();
        } else if (sample.tipDown() && m_trajectoryGizmo.pathShown()
                   && (sample.tool == InputTool::Eraser || ToolState::hasSize(m_toolState->activeTool())
                       || m_toolState->activeTool() == ToolId::Fill)) {
            // Trajectory mode excludes drawing while a path is shown: the pen only drags its keys.
            emit statusMessage(tr("Trajectory mode is on: turn it off to draw"));
        } else if (picksColor() && sample.tipDown() && sample.tool != InputTool::Eraser) {
            // Alt and the pen down: a drag turns the frames over, a click picks the color (known on release).
            m_flip = Flip{sample.x, m_document->currentFrame(), docPos, false};
        } else if (sample.tipDown() && !m_lineArmed
                   && (sample.tool == InputTool::Eraser || m_toolState->activeTool() == ToolId::Eraser)
                   && m_toolState->vectorEraser().mode != tools::VectorEraserSettings::Mode::Off
                   && m_document->currentLayerIsVector()) {
            // The eraser on a vector layer takes away strokes or parts of them.
            m_vectorEraseTool->press(celPos, modifiers);
            m_editTool = m_vectorEraseTool->isActive() ? m_vectorEraseTool.get() : nullptr;
            update();
        } else if (sample.tipDown()
                   && (sample.tool == InputTool::Eraser || ToolState::isPaintTool(m_toolState->activeTool()))) {
            if (m_lineArmed)
                placeStraightLine(sample);
            else
                beginStroke(sample);
        } else if (CanvasTool *tool = canvasTool(); tool && sample.tipDown()) {
            tool->setViewZoom(m_view.zoom());
            tool->press(at(tool), modifiers);
            m_editTool = tool->isActive() ? tool : nullptr;
            update();
        }
        break;
    case SamplePhase::Move:
        if (m_flip) {
            constexpr double kStartPixels = 8;     // less than this is still a click
            constexpr double kPixelsPerFrame = 20; // a frame for each so many pixels sideways
            const double dx = sample.x - m_flip->startX;
            if (!m_flip->moved && std::abs(dx) >= kStartPixels) {
                m_flip->moved = true;
                setCursor(Qt::SizeHorCursor);
            }
            if (m_flip->moved) {
                const int target = std::clamp(m_flip->startFrame + int(std::lround(dx / kPixelsPerFrame)), 0,
                                              m_document->document().frameCount() - 1);
                if (target != m_document->currentFrame())
                    m_document->setCurrentFrame(target);
            }
        } else if (m_gesture.isActive()) {
            m_gesture.update(QPointF(sample.x, sample.y), m_view);
            viewWasChanged();
        } else if (m_cameraGizmo.isDragging()) {
            m_cameraGizmo.move(docPos);
            update();
        } else if (m_symmetryGizmo.isDragging()) {
            m_symmetryGizmo.move(docPos, modifiers);
            update();
        } else if (m_trajectoryGizmo.isDragging()) {
            m_trajectoryGizmo.move(docPos, modifiers);
            update();
        } else if (m_boneGizmo.isDragging()) {
            m_boneGizmo.move(docPos, modifiers);
            update();
        } else if (m_curveGizmo.isDragging()) {
            m_curveGizmo.move(docPos, modifiers);
            update();
        } else if (m_gizmo.isDragging()) {
            m_gizmo.move(docPos, m_view);
            update();
        } else if (m_stroke) {
            continueStroke(sample);
        } else if (m_editTool) {
            m_editTool->move(at(m_editTool), modifiers);
            update();
        } else if (CanvasTool *tool = canvasTool(); tool && tool->hover(at(tool))) {
            update();
        }
        break;
    case SamplePhase::Release:
        if (m_flip) {
            const Flip flip = *m_flip;
            m_flip.reset();
            updateCursor();
            if (!flip.moved) { // a click: the color under it
                m_eyedropperTool->press(flip.docPos, modifiers);
                m_eyedropperTool->release(flip.docPos, modifiers);
                emit editStepsChanged();
            }
            break;
        }
        m_gesture.end();
        if (m_cameraGizmo.isDragging()) {
            m_cameraGizmo.release(docPos);
            update();
            updateGizmoCursor();
        }
        if (m_symmetryGizmo.isDragging()) {
            m_symmetryGizmo.release(docPos);
            update();
            updateGizmoCursor();
        }
        if (m_trajectoryGizmo.isDragging()) {
            m_trajectoryGizmo.release(docPos);
            update();
            updateGizmoCursor();
        }
        if (m_boneGizmo.isDragging()) {
            m_boneGizmo.release(docPos);
            update();
        }
        if (m_curveGizmo.isDragging()) {
            m_curveGizmo.release(docPos);
            update();
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
            tool->release(at(tool), modifiers);
            update();
            emit editStepsChanged();
        }
        break;
    case SamplePhase::Hover:
        if (!busy && m_cameraGizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (!busy && m_symmetryGizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (!busy && m_trajectoryGizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (!busy && m_boneGizmo.hover(docPos, m_view))
            update();
        if (!busy && m_curveGizmo.hover(docPos, m_view))
            update();
        if (!busy && m_gizmo.hover(docPos, m_view)) {
            updateGizmoCursor();
            update();
        }
        if (CanvasTool *tool = canvasTool(); tool && !busy) {
            if (tool->hover(at(tool)))
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
    const auto target = m_document->beginPaint(!m_eraseStroke, &reason, true, true); // a vector layer keeps the stroke
    if (!target) {
        if (!reason.isEmpty())
            emit statusMessage(reason);
        return;
    }
    const ToolId tool = m_eraseStroke && !transparentBrush ? ToolId::Eraser : m_toolState->brushTool();
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
    if (target->vector) { // what reads the pixels under the stroke cannot be drawn again from its points
        style.mix.on = false;
        style.waterEdge.on = false;
    }
    auto stamper = std::make_shared<DabStamper>(target->tint(m_toolState->color()), style.hardness, definition->tips,
                                                style.verticalThickness);
    stamper->setSecondaryColor(target->tint(m_toolState->inactiveColor()));
    m_strokeImage = target->image;
    m_stroke.emplace(*target->image, std::move(stamper), settings.opacity, m_eraseStroke); // the canvas has no end
    if (m_document->hasSelection())
        m_stroke->setSelection(m_document->selection()); // paint or erase only inside it
    m_stroke->setStyle(style);
    m_seed = QRandomGenerator::global()->generate();
    m_dabs.emplace(style, m_seed);
    // The copies round the symmetry's axes: the axes are on the page, the stroke's points in the cel (which its pose
    // may move), so each copy's transform goes out to the page and back.
    m_symmetryCopies.clear();
    for (const QTransform &transform : symmetryCopies(m_view.mapToDocument(QPointF(sample.x, sample.y)))) {
        SymmetryCopy copy;
        copy.transform = transform;
        copy.dabs.emplace(style, m_seed);
        m_symmetryCopies.push_back(std::move(copy));
    }
    m_points.clear();
    m_drawnPoints = 0;
    m_vectorStroke.reset();
    if (target->vector) { // what is needed to draw it again; the points come when the pen lifts
        doc::VectorStroke kept;
        kept.brush = definition->name.toStdString();
        kept.size = settings.size;
        kept.opacity = settings.opacity;
        kept.color = m_toolState->color().rgba();
        kept.secondaryColor = m_toolState->inactiveColor().rgba();
        kept.seed = m_seed;
        kept.erase = m_eraseStroke;
        kept.cornerTips = m_toolState->sharpCorners();
        kept.startCorrectedAfter = m_toolState->correctsStartAfterStroke();
        m_vectorStroke = std::move(kept);
    }

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
    for (SymmetryCopy &copy : m_symmetryCopies)
        copy.dabs.emplace(style, m_seed);
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
    const ToolId tool = m_eraseStroke ? ToolId::Eraser : m_toolState->brushTool();
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
        const QPointF in = toCel(m_view.mapToDocument(QPointF(d.before.x, d.before.y)));
        const QPointF out = toCel(m_view.mapToDocument(QPointF(d.after.x, d.after.y)));
        const brush::BrushDab dab = m_dabs->dabHere(corner);
        if (const auto tip = brush::cornerTip(corner.x, corner.y, corner.x - in.x(), corner.y - in.y(),
                                              out.x() - corner.x, out.y() - corner.y, dab.radius))
            m_document->notifyPainted(m_stroke->addCornerTip(*tip, dab));
        for (SymmetryCopy &copy : m_symmetryCopies) { // the corner's point on each copy too
            const brush::BrushInput c = transformedInput(corner, copy.transform);
            const QPointF i = copy.transform.map(in), o = copy.transform.map(out);
            const brush::BrushDab copyDab = copy.dabs->dabHere(c);
            if (const auto tip = brush::cornerTip(c.x, c.y, c.x - i.x(), c.y - i.y(), o.x() - c.x, o.y() - c.y,
                                                  copyDab.radius))
                m_document->notifyPainted(m_stroke->addCornerTip(*tip, copyDab));
        }
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
        for (SymmetryCopy &copy : m_symmetryCopies) {
            const brush::BrushInput moved = transformedInput(in, copy.transform);
            changed |= m_stroke->addDabs(m_drawnPoints == 0 ? copy.dabs->begin(moved) : copy.dabs->continueTo(moved));
        }
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
        for (const SymmetryCopy &copy : m_symmetryCopies) {
            std::vector<brush::BrushInput> moved;
            moved.reserve(m_points.size());
            for (const brush::BrushInput &in : m_points)
                moved.push_back(transformedInput(in, copy.transform));
            const std::vector<brush::BrushInput> copyPoints = brush::finalPoints(style, moved);
            brush::DabGenerator copyGenerator(style, m_seed);
            copyGenerator.setStrokeLength(brush::strokeLength(copyPoints));
            changed |= m_stroke->drawPoints(copyGenerator, copyPoints, m_toolState->sharpCorners());
        }
        m_document->notifyPainted(changed);
    }
    m_document->notifyPainted(m_stroke->finish());
    // Before the stroke is recorded: a stroke on an empty frame that left nothing takes back the frame it made, and
    // the image it painted on goes with it.
    if (!m_points.empty())
        m_lastPoint = lineStartOf(m_points);
    if (m_vectorStroke) {
        m_vectorStroke->points = m_points;
        // The stroke, then each of its copies round the symmetry's axes, a stroke of its own.
        std::vector<doc::VectorStroke> kept{*m_vectorStroke};
        for (const SymmetryCopy &copy : m_symmetryCopies) {
            doc::VectorStroke moved = *m_vectorStroke;
            for (brush::BrushInput &in : moved.points)
                in = transformedInput(in, copy.transform);
            kept.push_back(std::move(moved));
        }
        const bool inSelection = m_document->hasSelection(); // painted only inside the selection: so is what is kept
        const SelectionMask mask = inSelection ? SelectionMask::fromPath(m_document->selection()) : SelectionMask();
        for (doc::VectorStroke &stroke : kept) {
            if (!inSelection) {
                m_document->attachStroke(std::move(stroke));
                continue;
            }
            for (const doc::VectorStroke &piece : splitStrokeByMask(stroke, mask).inside)
                m_document->attachStroke(piece);
        }
        m_vectorStroke.reset();
    }
    m_document->endPaint(m_stroke->before());
    m_stroke.reset();
    m_strokeImage = nullptr;
    m_dabs.reset();
    m_symmetryCopies.clear();
    m_points.clear();
    updateStraightLine(); // Shift still held: a line from where this stroke ended
}

CanvasTool *CanvasView::canvasTool() const
{
    switch (m_toolState->activeTool()) {
    case ToolId::Move: return m_toolState->keyframeMode() ? m_animateTool.get() : m_moveTool.get();
    case ToolId::RectSelect: return m_rectSelectTool.get();
    case ToolId::LassoSelect: return m_lassoSelectTool.get();
    case ToolId::Puppet: return m_puppetTool.get();
    case ToolId::Eyedropper: return m_eyedropperTool.get();
    case ToolId::Fill: return m_fillTool.get();
    case ToolId::Transform: return m_toolState->keyframeMode() ? m_animateTool.get() : m_transformTool.get();
    case ToolId::Blend: return m_blendTool.get();
    case ToolId::Liquify: return m_liquifyTool.get();
    case ToolId::Shape: return m_shapeTool.get();
    case ToolId::VectorPoint: return m_vectorPointTool.get();
    default: return nullptr;
    }
}

void CanvasView::commitPendingEdits()
{
    // An edit left open (a puppet warp) is applied when another tool takes over.
    for (CanvasTool *tool : {m_moveTool.get(), m_rectSelectTool.get(), m_lassoSelectTool.get(), m_puppetTool.get(),
                              m_shapeTool.get(), m_transformTool.get(), m_animateTool.get()})
        if (tool != canvasTool() && tool->hasPendingEdit())
            tool->commit();
    emit editStepsChanged();
    emit transformChanged();
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
    // Not while a path is shown in trajectory mode: nothing is drawn then, and Shift chooses more of its keys.
    const bool armed = m_shiftHeld && m_lastPoint && !m_stroke && !m_gesture.isActive() && !panKeyHeld()
                       && ToolState::isPaintTool(m_toolState->activeTool()) && !m_trajectoryGizmo.pathShown();
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
    const double size = m_toolState->settings(eraser ? ToolId::Eraser : m_toolState->brushTool()).size;
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
    m_trajectoryGizmo.cancel();
    m_boneGizmo.cancel();
    m_curveGizmo.cancel();
    m_symmetryGizmo.cancel();
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
                              m_transformTool.get(), m_animateTool.get()})
        if (tool->hasPendingEdit())
            tool->commit();
    emit editStepsChanged();
    emit transformChanged();
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
    m_symmetryCopies.clear();
    m_vectorStroke.reset();
    m_heldSamples.clear();
    m_document->cancelPaint();
    renderWholeFrame();
}

brush::BrushInput CanvasView::brushInput(const PenSample &smoothed)
{
    brush::BrushInput in;
    const QPointF doc = toCel(m_view.mapToDocument(QPointF(smoothed.x, smoothed.y))); // the cel as it is shown
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
        if ((key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) && m_toolState->rigMode()
            && (m_boneGizmo.chosenBone() >= 0 || m_curveGizmo.chosenNode() >= 0)) {
            event->accept(); // the chosen bone goes, not the frame
            return true;
        }
        if (const CanvasTool *tool = canvasTool(); tool && key->modifiers() == Qt::NoModifier && tool->wantsKey(key->key())) {
            event->accept(); // no shortcut: it comes as a key press
            return true;
        }
    }
    return QWidget::event(event);
}

QRectF CanvasView::transformBounds() const
{
    if (m_toolState->movesTheCel())
        return m_playing || m_scrubbing ? QRectF() : static_cast<const AnimateTool *>(m_animateTool.get())->shownBounds();
    const auto *tool = static_cast<const TransformTool *>(m_transformTool.get());
    if (!tool->hasPendingEdit())
        return {};
    return tool->transform().mapRect(QRectF(tool->box()));
}

void CanvasView::finishTransform(bool apply)
{
    CanvasTool *tool = m_toolState->movesTheCel() ? m_animateTool.get() : m_transformTool.get();
    if (tool->keyPress(apply ? Qt::Key_Return : Qt::Key_Escape)) {
        emit editStepsChanged();
        emit transformChanged();
    }
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

bool CanvasView::selectAllInTool()
{
    if (m_toolState->activeTool() != ToolId::VectorPoint
        || !static_cast<VectorPointTool *>(m_vectorPointTool.get())->chooseAllAnchors())
        return false;
    update();
    return true;
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
        if (!m_lastPoint && ToolState::isPaintTool(m_toolState->activeTool()) && !m_stroke && !m_trajectoryGizmo.pathShown())
            emit statusMessage(tr("Draw something first: Shift then draws a straight line from the last point"));
        updateStraightLine();
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && (m_boneGizmo.removeChosenBone() || m_curveGizmo.removeChosenNode())) {
        event->accept();
        update();
        return;
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
    const QPoint p(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    const doc::Document &d = m_document->document();
    if (QRect(0, 0, d.width(), d.height()).contains(p))
        return documentImage().pixelColor(p);
    return frameArea(QRect(p, QSize(1, 1))).pixelColor(0, 0); // out of the page: the canvas goes on
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
    using H = TrajectoryGizmo::Handle;
    const H hovered = m_trajectoryGizmo.hovered();
    if (hovered == H::BoxScale && !panKeyHeld()) {
        const QLineF grip = m_trajectoryGizmo.scaleGripLine(m_view);
        setCursor(resizeCursor(m_view.documentToView().map(grip.p1()), m_view.documentToView().map(grip.p2())));
    } else if (hovered != H::None && !panKeyHeld())
        setCursor(hovered == H::Move || hovered == H::Key || hovered == H::BoxMove ? Qt::SizeAllCursor
                  : hovered == H::Rotate || hovered == H::BoxRotate              ? turnCursor()
                                                                                 : Qt::SizeFDiagCursor);
    else if (m_symmetryGizmo.hovered() != SymmetryGizmo::Handle::None && !panKeyHeld())
        setCursor(m_symmetryGizmo.hovered() == SymmetryGizmo::Handle::Turn ? turnCursor() : Qt::SizeAllCursor);
    else if ((m_gizmo.hovered() != PerspectiveGizmo::Handle::None || m_cameraGizmo.hovered() != CameraGizmo::Handle::None)
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
    else if (ToolState::editsPixels(m_toolState->activeTool()) && m_document->drawingBlocked()
             && (m_document->cameraSelected() || !ToolState::worksOnChosenLayers(m_toolState->activeTool())))
        // Nothing to draw on: a cel, a frame or the camera is selected. The tools that work on all the layers chosen (a
        // cel's or a frame's) do work then, but not on the camera.
        setCursor(Qt::ForbiddenCursor);
    else if (ToolState::isPaintTool(m_toolState->activeTool()) && Preferences::instance().brushOutline())
        setCursor(invisibleCursor()); // the brush's own outline, drawn in paintEvent(), is the cursor
    else if (const CanvasTool *tool = canvasTool())
        setCursor(tool->cursor());
    else
        setCursor(Qt::CrossCursor);
    update();
}

void CanvasView::syncPoseMode()
{
    // Transform and Move move the cel as a piece (not its pixels) once it has keys.
    bool keyed = false; // the current cel has keys, or one of the cels chosen with it (they are moved together)
    for (doc::CelId id : m_document->celSelection()) {
        const doc::CelMotion *motion = m_document->celMotion(id);
        keyed = keyed || (motion && !motion->isEmpty());
    }
    m_toolState->setKeyframeMode(keyed);
}

} // namespace mothion::canvas
