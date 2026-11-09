#include "canvas/AnimateTool.h"

#include "canvas/FrameRenderer.h"
#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr double kHandle = 9;       // screen pixels, half the side of a grabbed handle
constexpr double kTurnReach = 30;   // screen pixels from the box to the turning handle
constexpr double kRadians = std::numbers::pi / 180.0;

QPointF unit(QPointF v)
{
    const double length = std::hypot(v.x(), v.y());
    return length > 1e-9 ? v / length : QPointF(0, -1);
}

} // namespace

AnimateTool::AnimateTool(document::DocumentController *document, tools::ToolState *toolState,
                         std::function<void(const QString &)> status, std::function<void()> repaint)
    : m_document(document)
    , m_toolState(toolState)
    , m_status(std::move(status))
    , m_repaint(std::move(repaint))
{
}

std::optional<AnimateTool::Shape> AnimateTool::shape() const
{
    if (m_document->cameraSelected())
        return std::nullopt;
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!cel)
        return std::nullopt;
    const doc::Drawing *drawing = cel->drawingAt(m_document->currentFrame());
    if (!drawing)
        return std::nullopt;
    if (!m_boundsValid || m_boundsCel != cel->id() || m_boundsDrawing != drawing->id) {
        m_bounds = visibleBounds(*drawing);
        m_boundsCel = cel->id();
        m_boundsDrawing = drawing->id;
        m_boundsValid = true;
    }
    const QRect bounds = m_bounds;
    if (bounds.isEmpty())
        return std::nullopt;
    Shape s;
    s.box = QRectF(bounds.x(), bounds.y(), bounds.width(), bounds.height()); // (pixels: to their far edges)
    if (cel->motion().isEmpty()) { // not keyed yet: as the first key will have it
        s.pivot = s.box.center();
    } else {
        s.pivot = QPointF(cel->motion().pivotX, cel->motion().pivotY);
        s.pose = cel->motion().poseAt(m_document->currentFrame());
    }
    s.matrix = s.pose.matrix(s.pivot.x(), s.pivot.y());
    return s;
}

QPointF AnimateTool::mapped(const Shape &s, const QPointF &local) const
{
    double x = 0, y = 0;
    s.matrix.map(local.x(), local.y(), x, y);
    return {x, y};
}

QPointF AnimateTool::turnHandle(const Shape &s) const
{
    const QPointF top = mapped(s, QPointF(s.box.center().x(), s.box.top()));
    const QPointF centre = mapped(s, s.box.center());
    return top + unit(top - centre) * (kTurnReach / std::max(0.05, m_zoom));
}

AnimateTool::Grip AnimateTool::pick(const QPointF &docPos, const Shape &s) const
{
    const double reach = kHandle / std::max(0.05, m_zoom);
    auto near = [&](const QPointF &p) { return std::hypot(docPos.x() - p.x(), docPos.y() - p.y()) <= reach; };
    if (near(turnHandle(s)))
        return Grip::Rotate;
    for (const QPointF &corner : {s.box.topLeft(), s.box.topRight(), s.box.bottomRight(), s.box.bottomLeft()})
        if (near(mapped(s, corner)))
            return Grip::Scale;
    QPolygonF frame;
    for (const QPointF &corner : {s.box.topLeft(), s.box.topRight(), s.box.bottomRight(), s.box.bottomLeft()})
        frame << mapped(s, corner);
    return frame.containsPoint(docPos, Qt::OddEvenFill) ? Grip::Move : Grip::None;
}

void AnimateTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    const std::optional<Shape> s = shape();
    if (!s) {
        m_status(QObject::tr("The cel shows nothing on this frame: there is nothing to move"));
        return;
    }
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (cel && cel->properties().locked) {
        m_status(QObject::tr("The cel is locked"));
        return;
    }
    const Grip grip = m_moveOnly ? Grip::Move : pick(docPos, *s);
    if (grip == Grip::None)
        return;
    Drag d;
    d.grip = grip;
    d.cel = m_document->currentCel();
    d.frame = m_document->currentFrame();
    d.before = cel->motion();
    if (m_pending && m_pending->cel != d.cel)
        commit(); // another cel: what was open is applied
    if (!m_pending)
        m_pending = Pending{d.cel, d.before};
    d.start = s->pose;
    d.press = docPos;
    d.pivotNow = s->pivotNow();
    d.startDistance = std::max(1e-3, std::hypot(docPos.x() - d.pivotNow.x(), docPos.y() - d.pivotNow.y()));
    d.lastAngle = std::atan2(docPos.y() - d.pivotNow.y(), docPos.x() - d.pivotNow.x());
    m_drag = d;
}

void AnimateTool::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    Drag &d = *m_drag;
    doc::CelPose pose = d.start;
    switch (d.grip) {
    case Grip::Move:
        pose.x = d.start.x + docPos.x() - d.press.x();
        pose.y = d.start.y + docPos.y() - d.press.y();
        break;
    case Grip::Rotate: {
        // By how much the pen has gone round the pivot (past a half turn too, not wrapped).
        const double angle = std::atan2(docPos.y() - d.pivotNow.y(), docPos.x() - d.pivotNow.x());
        double step = angle - d.lastAngle;
        while (step > std::numbers::pi)
            step -= 2 * std::numbers::pi;
        while (step < -std::numbers::pi)
            step += 2 * std::numbers::pi;
        d.turned += step / kRadians;
        d.lastAngle = angle;
        double rotation = d.start.rotation + d.turned;
        if (modifiers & Qt::ShiftModifier)
            rotation = std::round(rotation / 15.0) * 15.0;
        pose.rotation = rotation;
        break;
    }
    case Grip::Scale: {
        const double factor = std::max(0.02, std::hypot(docPos.x() - d.pivotNow.x(), docPos.y() - d.pivotNow.y())
                                                 / d.startDistance);
        pose.scaleX = d.start.scaleX * factor;
        pose.scaleY = d.start.scaleY * factor;
        break;
    }
    case Grip::None: break;
    }
    m_document->previewCelPose(d.cel, d.frame, pose);
    m_repaint();
}

void AnimateTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    move(docPos, modifiers);
    m_drag.reset(); // the edit stays open: OK, or leaving the frame, applies it
    m_repaint();
}

void AnimateTool::cancel()
{
    if (!m_drag)
        return;
    const Drag d = *m_drag;
    m_drag.reset();
    m_document->cancelCelPose(d.cel, d.before);
    if (m_pending && m_pending->before == d.before)
        m_pending.reset(); // the first drag of the edit: nothing is left open
    m_repaint();
}

void AnimateTool::commit()
{
    m_drag.reset();
    if (!m_pending)
        return;
    const Pending pending = *m_pending;
    m_pending.reset();
    m_document->finishCelPose(pending.cel, pending.before); // one undo step for what was done (nothing for a pose that is no key)
    m_repaint();
}

bool AnimateTool::keyPress(int key)
{
    if (!m_pending)
        return false;
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        commit();
        return true;
    }
    if (key == Qt::Key_Escape) {
        const Pending pending = *m_pending;
        m_drag.reset();
        m_pending.reset();
        m_document->cancelCelPose(pending.cel, pending.before);
        m_repaint();
        return true;
    }
    return false;
}

QRectF AnimateTool::pendingBounds() const
{
    if (!m_pending)
        return {};
    const std::optional<Shape> s = shape();
    if (!s)
        return {};
    QPolygonF frame;
    for (const QPointF &corner : {s->box.topLeft(), s->box.topRight(), s->box.bottomRight(), s->box.bottomLeft()})
        frame << mapped(*s, corner);
    return frame.boundingRect();
}

bool AnimateTool::hover(const QPointF &docPos)
{
    const std::optional<Shape> s = shape();
    const Grip now = s ? (m_moveOnly ? Grip::Move : pick(docPos, *s)) : Grip::None;
    if (now == m_hover)
        return false;
    m_hover = now;
    return true;
}

QCursor AnimateTool::cursor() const
{
    switch (m_hover) {
    case Grip::Move: return Qt::SizeAllCursor;
    case Grip::Rotate: return Qt::CrossCursor;
    case Grip::Scale: return Qt::SizeFDiagCursor;
    case Grip::None: break;
    }
    return Qt::ArrowCursor;
}

void AnimateTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    const std::optional<Shape> s = shape();
    if (!s)
        return;
    const QTransform toView = view.documentToView();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const QColor accent(240, 178, 61);

    // Where the pivot goes over the frames of the keys (a dot for each frame, a bigger one on each key).
    if (const doc::Cel *cel = m_document->document().cel(m_document->currentCel()); cel && !cel->motion().isEmpty()) {
        const auto &keys = cel->motion().keys();
        const doc::CelMotion &motion = cel->motion();
        for (int f = keys.front().frame; f <= keys.back().frame; ++f) {
            const doc::CelPose q = motion.poseAt(f);
            const QPointF at = toView.map(QPointF(motion.pivotX + q.x, motion.pivotY + q.y));
            const bool isKey = motion.keyAt(f) != nullptr;
            painter.setPen(Qt::NoPen);
            painter.setBrush(isKey ? accent : QColor(accent.red(), accent.green(), accent.blue(), 150));
            painter.drawEllipse(at, isKey ? 3.5 : 1.5, isKey ? 3.5 : 1.5);
        }
        painter.setBrush(Qt::NoBrush);
    }

    if (!m_pending)
        return; // the box, its handles and the pivot are of an edit that is open (as the Transform tool's): OK or Esc close them

    QPolygonF frame;
    for (const QPointF &corner : {s->box.topLeft(), s->box.topRight(), s->box.bottomRight(), s->box.bottomLeft()})
        frame << toView.map(mapped(*s, corner));
    painter.setPen(QPen(Qt::white, 3));
    painter.drawPolygon(frame);
    painter.setPen(QPen(QColor(40, 110, 230), 1));
    painter.drawPolygon(frame);
    if (m_moveOnly)
        return; // the box alone: a drag anywhere moves it
    painter.setBrush(Qt::white);
    for (const QPointF &corner : frame)
        painter.drawRect(QRectF(corner.x() - kHandle / 2, corner.y() - kHandle / 2, kHandle, kHandle));
    // The turning handle, over the top side.
    const QPointF top = toView.map(mapped(*s, QPointF(s->box.center().x(), s->box.top())));
    const QPointF turn = toView.map(turnHandle(*s));
    painter.setPen(QPen(QColor(40, 110, 230), 1));
    painter.drawLine(top, turn);
    painter.drawEllipse(turn, kHandle / 2 + 1, kHandle / 2 + 1);
    // The pivot.
    const QPointF pivot = toView.map(s->pivotNow());
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 3));
    painter.drawEllipse(pivot, 6, 6);
    painter.drawLine(QPointF(pivot.x() - 10, pivot.y()), QPointF(pivot.x() + 10, pivot.y()));
    painter.drawLine(QPointF(pivot.x(), pivot.y() - 10), QPointF(pivot.x(), pivot.y() + 10));
    painter.setPen(QPen(QColor(220, 70, 70), 1.5));
    painter.drawEllipse(pivot, 6, 6);
    painter.drawLine(QPointF(pivot.x() - 10, pivot.y()), QPointF(pivot.x() + 10, pivot.y()));
    painter.drawLine(QPointF(pivot.x(), pivot.y() - 10), QPointF(pivot.x(), pivot.y() + 10));
}

} // namespace mothion::canvas
