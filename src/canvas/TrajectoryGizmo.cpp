#include "canvas/TrajectoryGizmo.h"

#include "canvas/ViewTransform.h"

#include <QLineF>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr double kRadians = std::numbers::pi / 180.0;
constexpr double kRotateSnap = 15.0; // degrees, with Shift
constexpr double kMinScale = 0.02;

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (length2 <= 0)
        return QLineF(p, a).length();
    const double t = std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / length2, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

} // namespace

TrajectoryGizmo::TrajectoryGizmo(document::DocumentController *document)
    : m_document(document)
{
}

bool TrajectoryGizmo::shown() const
{
    if (m_document->cameraSelected())
        return false;
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    return cel && cel->properties().trajectory;
}

bool TrajectoryGizmo::editable() const
{
    const doc::Cel *cel = shown() ? m_document->document().cel(m_document->currentCel()) : nullptr;
    return cel && !cel->properties().locked;
}

std::optional<TrajectoryGizmo::Shape> TrajectoryGizmo::shape(const ViewTransform &view) const
{
    if (!shown())
        return std::nullopt;
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    const doc::CelMotion &motion = cel->motion();
    Shape s;
    s.pose = motion.poseAt(m_document->currentFrame());
    s.centerDoc = QPointF(motion.pivotX + s.pose.x, motion.pivotY + s.pose.y);
    const QTransform toView = view.documentToView();
    s.center = toView.map(s.centerDoc);
    s.radius = std::max(kMinRadius, kRadius * std::abs(s.pose.scaleX));
    // The arrow's way on screen: the rotation's way in the document, seen through the view (which may be turned).
    const double angle = s.pose.rotation * kRadians;
    const QPointF ahead = toView.map(s.centerDoc + QPointF(std::cos(angle), std::sin(angle)));
    QPointF way = ahead - s.center;
    const double length = std::hypot(way.x(), way.y());
    s.direction = length > 0 ? way / length : QPointF(1, 0);
    s.tip = s.center + s.direction * (s.radius + kArrowAway);
    return s;
}

TrajectoryGizmo::Handle TrajectoryGizmo::handleAt(const QPointF &docPos, const ViewTransform &view) const
{
    const std::optional<Shape> s = shape(view);
    if (!s)
        return Handle::None;
    const QPointF at = view.documentToView().map(docPos);
    const double fromCenter = QLineF(at, s->center).length();
    if (QLineF(at, s->tip).length() <= kGrab * 1.4)
        return Handle::Rotate;
    if (fromCenter <= kGrab)
        return Handle::Move;
    // The arrow's line beyond the edge turns too (not the part inside, near the middle).
    if (distanceToSegment(at, s->center + s->direction * s->radius, s->tip) <= kGrab * 0.6)
        return Handle::Rotate;
    if (std::abs(fromCenter - s->radius) <= kGrab)
        return Handle::Scale;
    return Handle::None;
}

std::optional<QPointF> TrajectoryGizmo::handlePoint(Handle handle, const ViewTransform &view) const
{
    const std::optional<Shape> s = shape(view);
    if (!s)
        return std::nullopt;
    QPointF at;
    switch (handle) {
    case Handle::Move: at = s->center; break;
    case Handle::Rotate: at = s->tip; break;
    case Handle::Scale: { // the edge, opposite the arrow
        at = s->center - s->direction * s->radius;
        break;
    }
    case Handle::None: return std::nullopt;
    }
    return view.mapToDocument(at);
}

bool TrajectoryGizmo::press(const QPointF &docPos, const ViewTransform &view)
{
    if (!editable())
        return false;
    const Handle handle = handleAt(docPos, view);
    if (handle == Handle::None)
        return false;
    const std::optional<Shape> s = shape(view);
    Drag d;
    d.handle = handle;
    d.cel = m_document->currentCel();
    d.frame = m_document->currentFrame();
    d.before = m_document->document().cel(d.cel)->motion();
    d.press = docPos;
    d.toView = view.documentToView();
    d.centerDoc = s->centerDoc;
    const QPointF at = d.toView.map(docPos);
    d.pressDistance = std::max(1.0, QLineF(at, s->center).length());
    d.lastAngle = std::atan2(docPos.y() - s->centerDoc.y(), docPos.x() - s->centerDoc.x());
    // The first key of a trajectory is made as the drag begins, not as a step of its own: the drag's step takes it in.
    if (d.before.isEmpty())
        m_document->beginFirstKey(d.cel, d.frame);
    d.start = m_document->celPoseAt(d.cel, d.frame);
    m_hovered = handle;
    m_drag = d;
    return true;
}

void TrajectoryGizmo::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    const Drag &d = *m_drag;
    doc::CelPose pose = d.start;
    std::vector<doc::Channel> channels;
    switch (d.handle) {
    case Handle::Move:
        pose.x = d.start.x + docPos.x() - d.press.x();
        pose.y = d.start.y + docPos.y() - d.press.y();
        channels = {doc::Channel::Position};
        break;
    case Handle::Rotate: {
        const double angle = std::atan2(docPos.y() - d.centerDoc.y(), docPos.x() - d.centerDoc.x());
        double step = angle - d.lastAngle;
        while (step > std::numbers::pi)
            step -= 2 * std::numbers::pi;
        while (step < -std::numbers::pi)
            step += 2 * std::numbers::pi;
        m_drag->turned += step / kRadians;
        m_drag->lastAngle = angle;
        double degrees = m_drag->turned;
        if (modifiers & Qt::ShiftModifier)
            degrees = std::round(degrees / kRotateSnap) * kRotateSnap;
        pose.rotation = d.start.rotation + degrees;
        channels = {doc::Channel::Rotation};
        break;
    }
    case Handle::Scale: {
        const QPointF at = d.toView.map(docPos);
        const QPointF center = d.toView.map(d.centerDoc);
        double ratio = QLineF(at, center).length() / d.pressDistance;
        auto scaled = [&](double s) {
            const double v = s * ratio;
            return std::abs(v) < kMinScale ? (s < 0 ? -kMinScale : kMinScale) : v;
        };
        pose.scaleX = scaled(d.start.scaleX);
        pose.scaleY = scaled(d.start.scaleY);
        channels = {doc::Channel::Scale};
        break;
    }
    case Handle::None: return;
    }
    m_document->previewCelPose(d.cel, d.frame, pose, channels);
}

void TrajectoryGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return;
    move(docPos);
    const Drag d = *m_drag;
    m_drag.reset();
    m_document->finishCelPose(d.cel, d.before); // one undo step (none for a pose that stays only on the frame)
}

void TrajectoryGizmo::cancel()
{
    if (!m_drag)
        return;
    const Drag d = *m_drag;
    m_drag.reset();
    m_document->cancelCelPose(d.cel, d.before);
}

bool TrajectoryGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const Handle now = editable() ? handleAt(docPos, view) : Handle::None;
    if (now == m_hovered)
        return false;
    m_hovered = now;
    return true;
}

void TrajectoryGizmo::paint(QPainter &painter, const ViewTransform &view) const
{
    const std::optional<Shape> s = shape(view);
    if (!s)
        return;
    painter.save();
    painter.setWorldTransform(QTransform()); // view coordinates
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent(240, 178, 61);
    const bool live = editable();
    auto drawn = [&](const QPen &pen, auto &&draw) {
        painter.setPen(QPen(Qt::white, pen.widthF() + 2.0)); // an edge of white, to read on any picture
        draw();
        painter.setPen(pen);
        draw();
    };
    painter.setBrush(Qt::NoBrush);
    QPen dashed(live ? accent : accent.darker(160), 1.6, Qt::DashLine);
    drawn(dashed, [&] { painter.drawEllipse(s->center, s->radius, s->radius); });
    // The line from the middle to the arrow, whose head points the way of the rotation.
    const QPointF from = s->center, to = s->tip;
    QPen line(live ? accent : accent.darker(160), 1.8);
    drawn(line, [&] { painter.drawLine(from, to); });
    const QPointF side(-s->direction.y(), s->direction.x());
    const QPolygonF head{to + s->direction * 6.0, to - s->direction * 8.0 + side * 6.0,
                         to - s->direction * 8.0 - side * 6.0};
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(live ? accent : accent.darker(160));
    painter.drawPolygon(head);
    // The middle, taken to move.
    painter.setBrush(Qt::white);
    painter.setPen(QPen(live ? accent : accent.darker(160), 2.0));
    painter.drawEllipse(s->center, 5.0, 5.0);
    // The handle under the pointer is picked out.
    if (live && m_hovered != Handle::None) {
        painter.setPen(QPen(QColor(40, 110, 230), 2.0));
        painter.setBrush(Qt::NoBrush);
        if (m_hovered == Handle::Scale)
            painter.drawEllipse(s->center, s->radius + 2.0, s->radius + 2.0);
        else
            painter.drawEllipse(m_hovered == Handle::Move ? s->center : s->tip, 9.0, 9.0);
    }
    painter.restore();
}

} // namespace mothion::canvas
