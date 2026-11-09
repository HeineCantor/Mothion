#include "canvas/AnimateTool.h"

#include "canvas/FrameRenderer.h"
#include "canvas/TransformTool.h"
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

constexpr double kHandleSize = 8.0;  // screen px, as drawn
constexpr double kGrabRadius = 6.0;  // screen px, to pick a handle: just outside it is the turn
constexpr double kRotateSnap = 15.0; // degrees, with Shift
constexpr double kMinScale = 0.02;
constexpr double kRadians = std::numbers::pi / 180.0;

double clampScale(double s)
{
    return std::abs(s) < kMinScale ? (s < 0 ? -kMinScale : kMinScale) : s;
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

QPointF AnimateTool::handlePoint(const Shape &s, int index) const
{
    const QRectF &b = s.box;
    switch (index) {
    case 0: return b.topLeft();
    case 1: return b.topRight();
    case 2: return b.bottomRight();
    case 3: return b.bottomLeft();
    case 4: return {b.center().x(), b.top()};
    case 5: return {b.right(), b.center().y()};
    case 6: return {b.center().x(), b.bottom()};
    default: return {b.left(), b.center().y()};
    }
}

AnimateTool::Pick AnimateTool::pick(const QPointF &docPos, const Shape &s) const
{
    if (m_moveOnly)
        return {Grip::Move, -1};
    // The handle nearest the pen, if close enough on screen; on a small box less close, so there is room inside it to move.
    QPolygonF corners;
    for (int i = 0; i < 4; ++i)
        corners << mapped(s, handlePoint(s, i));
    const double shorter = std::min(QLineF(corners[0], corners[1]).length(), QLineF(corners[1], corners[2]).length());
    int nearest = -1;
    double best = std::min(kGrabRadius / std::max(0.05, m_zoom), shorter / 4);
    for (int i = 0; i < 8; ++i) {
        const double distance = QLineF(mapped(s, handlePoint(s, i)), docPos).length();
        if (distance <= best) {
            best = distance;
            nearest = i;
        }
    }
    if (nearest >= 0)
        return {nearest < 4 ? Grip::Corner : Grip::Side, nearest};
    return {corners.containsPoint(docPos, Qt::OddEvenFill) ? Grip::Move : Grip::Rotate, -1};
}

bool AnimateTool::showsBox() const
{
    return !m_moveOnly || m_pending.has_value();
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
    const Pick picked = pick(docPos, *s);
    Drag d;
    d.grip = picked.grip;
    d.cel = m_document->currentCel();
    d.frame = m_document->currentFrame();
    d.before = cel->motion();
    if (m_pending && m_pending->cel != d.cel)
        commit(); // another cel: what was open is applied
    if (!m_pending)
        m_pending = Pending{d.cel, d.before};
    d.start = *s;
    d.press = docPos;
    if (picked.handle >= 0) {
        d.handle = handlePoint(*s, picked.handle);
        // The opposite corner, or the middle of the opposite side.
        d.anchor = handlePoint(*s, picked.handle < 4 ? (picked.handle + 2) % 4 : 4 + (picked.handle - 4 + 2) % 4);
        d.horizontal = picked.handle == 5 || picked.handle == 7;
    }
    const QPointF centre = mapped(*s, s->box.center());
    d.lastAngle = std::atan2(docPos.y() - centre.y(), docPos.x() - centre.x());
    m_hover = picked; // what the pointer stays over during the drag
    m_drag = d;
}

void AnimateTool::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    Drag &d = *m_drag;
    const Shape &start = d.start;
    doc::CelPose pose = start.pose;
    // The turn and the scale are around the pivot; the point that stays put (the box's middle, the anchor) is then brought
    // back to where it was by the move.
    auto keepPut = [&](const QPointF &local) {
        const QPointF before = mapped(start, local);
        double x = 0, y = 0;
        pose.matrix(start.pivot.x(), start.pivot.y()).map(local.x(), local.y(), x, y);
        pose.x += before.x() - x;
        pose.y += before.y() - y;
    };
    switch (d.grip) {
    case Grip::Move:
        pose.x = start.pose.x + docPos.x() - d.press.x();
        pose.y = start.pose.y + docPos.y() - d.press.y();
        break;
    case Grip::Rotate: {
        // By how much the pen has gone round the middle of the box (past a half turn too, not wrapped).
        const QPointF centre = mapped(start, start.box.center());
        const double angle = std::atan2(docPos.y() - centre.y(), docPos.x() - centre.x());
        double step = angle - d.lastAngle;
        while (step > std::numbers::pi)
            step -= 2 * std::numbers::pi;
        while (step < -std::numbers::pi)
            step += 2 * std::numbers::pi;
        d.turned += step / kRadians;
        d.lastAngle = angle;
        double degrees = d.turned;
        if (modifiers & Qt::ShiftModifier)
            degrees = std::round(degrees / kRotateSnap) * kRotateSnap;
        pose.rotation = start.pose.rotation + degrees;
        keepPut(start.box.center());
        break;
    }
    case Grip::Corner:
    case Grip::Side: {
        // The pen in the cel's own coordinates, and how many times further from the anchor than the handle it is.
        double px = 0, py = 0;
        start.matrix.inverted().map(docPos.x(), docPos.y(), px, py);
        const QPointF span = d.handle - d.anchor;
        double sx = d.grip == Grip::Side && !d.horizontal ? 1.0 : clampScale((px - d.anchor.x()) / span.x());
        double sy = d.grip == Grip::Side && d.horizontal ? 1.0 : clampScale((py - d.anchor.y()) / span.y());
        if (d.grip == Grip::Corner && (modifiers & Qt::ShiftModifier)) {
            const double both = std::max(std::abs(sx), std::abs(sy));
            sx = std::copysign(both, sx);
            sy = std::copysign(both, sy);
        }
        pose.scaleX = start.pose.scaleX * sx;
        pose.scaleY = start.pose.scaleY * sy;
        keepPut(d.anchor);
        break;
    }
    case Grip::None: return;
    }
    // What changed: the keys of those channels (a turn or a scale moves the piece too, unless it goes round its middle).
    std::vector<doc::Channel> channels;
    for (doc::Channel c : doc::kChannels)
        if (doc::channelsDiffer(c, start.pose, pose))
            channels.push_back(c);
    if (!channels.empty())
        m_document->previewCelPose(d.cel, d.frame, pose, channels);
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
    m_document->finishCelPose(pending.cel, pending.before); // one undo step for what was done (nothing for a value that is no key)
    m_repaint();
}

bool AnimateTool::keyPress(int key)
{
    if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Escape)
        return false;
    // The Move tool has something to close only after a drag; the Transform tool's box is always open.
    if (m_moveOnly && !m_pending)
        return false;
    if (key == Qt::Key_Escape) {
        m_drag.reset();
        if (m_pending) {
            const Pending pending = *m_pending;
            m_pending.reset();
            m_document->cancelCelPose(pending.cel, pending.before);
        }
        m_repaint();
    } else {
        commit();
    }
    if (!m_moveOnly)
        m_toolState->setActiveTool(m_toolState->previousTool()); // as the Transform tool of the pixels does
    return true;
}

QRectF AnimateTool::shownBounds() const
{
    if (!showsBox())
        return {};
    const std::optional<Shape> s = shape();
    if (!s)
        return {};
    QPolygonF frame;
    for (int i = 0; i < 4; ++i)
        frame << mapped(*s, handlePoint(*s, i));
    return frame.boundingRect();
}

bool AnimateTool::hover(const QPointF &docPos)
{
    const std::optional<Shape> s = shape();
    const Pick now = s ? pick(docPos, *s) : Pick{};
    if (now.grip == m_hover.grip && now.handle == m_hover.handle)
        return false;
    m_hover = now;
    return true;
}

QCursor AnimateTool::cursor() const
{
    switch (m_hover.grip) {
    case Grip::Move: return Qt::SizeAllCursor;
    case Grip::Rotate: return turnCursor();
    case Grip::Corner:
    case Grip::Side:
        if (const std::optional<Shape> s = shape())
            return resizeCursor(mapped(*s, s->box.center()), mapped(*s, handlePoint(*s, m_hover.handle)));
        break;
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
        const doc::CelMotion &motion = cel->motion();
        const std::vector<int> keyFrames = motion.keyFrames();
        for (int f = keyFrames.front(); f <= keyFrames.back(); ++f) {
            const doc::CelPose q = motion.poseAt(f);
            const QPointF at = toView.map(QPointF(motion.pivotX + q.x, motion.pivotY + q.y));
            const bool isKey = motion.keyAt(doc::Channel::Position, f) != nullptr; // the dots are the path of the position
            painter.setPen(Qt::NoPen);
            painter.setBrush(isKey ? accent : QColor(accent.red(), accent.green(), accent.blue(), 150));
            painter.drawEllipse(at, isKey ? 3.5 : 1.5, isKey ? 3.5 : 1.5);
        }
        painter.setBrush(Qt::NoBrush);
    }

    if (!showsBox())
        return;
    QPolygonF frame;
    for (int i = 0; i < 4; ++i)
        frame << toView.map(mapped(*s, handlePoint(*s, i)));
    painter.setPen(QPen(Qt::white, 3));
    painter.drawPolygon(frame);
    painter.setPen(QPen(QColor(40, 110, 230), 1));
    painter.drawPolygon(frame);
    if (m_moveOnly)
        return; // the box alone: a drag anywhere moves it
    painter.setBrush(Qt::white);
    for (int i = 0; i < 8; ++i) {
        const QPointF at = toView.map(mapped(*s, handlePoint(*s, i)));
        painter.drawRect(QRectF(at.x() - kHandleSize / 2, at.y() - kHandleSize / 2, kHandleSize, kHandleSize));
    }
}

} // namespace mothion::canvas
