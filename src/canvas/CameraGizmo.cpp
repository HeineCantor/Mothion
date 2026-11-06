#include "canvas/CameraGizmo.h"

#include "canvas/ViewTransform.h"

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr double kMinZoom = 0.05, kMaxZoom = 64.0;
const QColor kRed(235, 55, 55);

double degreesOf(const QPointF &v)
{
    return std::atan2(v.y(), v.x()) * 180.0 / std::numbers::pi;
}

} // namespace

CameraGizmo::CameraGizmo(document::DocumentController *document)
    : m_document(document)
{
}

bool CameraGizmo::editable() const
{
    const auto &camera = m_document->camera();
    return camera && camera->visible && !camera->isEmpty() && m_document->cameraSelected();
}

std::optional<CameraGizmo::Shape> CameraGizmo::shape(const ViewTransform &view) const
{
    const auto &camera = m_document->camera();
    if (!camera || camera->isEmpty())
        return std::nullopt;
    const doc::Document &d = m_document->document();
    Shape s;
    s.pose = camera->poseAt(m_document->currentFrame());
    const auto frame = s.pose.frame(d.width(), d.height());
    const QTransform toView = view.documentToView();
    for (int i = 0; i < 4; ++i)
        s.corners[i] = toView.map(QPointF(frame[std::size_t(i)][0], frame[std::size_t(i)][1]));
    s.center = (s.corners[0] + s.corners[2]) / 2.0;
    // The knob: out from the middle of the top edge, along the way the camera's up points.
    const QPointF topMiddle = (s.corners[0] + s.corners[1]) / 2.0;
    const QLineF up(s.center, topMiddle);
    s.knob = up.length() > 0 ? topMiddle + (topMiddle - s.center) / up.length() * kKnobAway : topMiddle;
    return s;
}

CameraGizmo::Handle CameraGizmo::handleAt(const QPointF &docPos, const ViewTransform &view) const
{
    if (!editable())
        return Handle::None;
    const auto s = shape(view);
    if (!s)
        return Handle::None;
    const QPointF at = view.documentToView().map(docPos);
    if (QLineF(at, s->knob).length() <= kKnob + 3)
        return Handle::Rotate;
    for (const QPointF &corner : s->corners)
        if (std::abs(at.x() - corner.x()) <= kCorner + 3 && std::abs(at.y() - corner.y()) <= kCorner + 3)
            return Handle::Scale;
    if (QPolygonF({s->corners[0], s->corners[1], s->corners[2], s->corners[3]}).containsPoint(at, Qt::OddEvenFill))
        return Handle::Move;
    return Handle::None;
}

std::optional<QPointF> CameraGizmo::handlePoint(Handle handle, const ViewTransform &view) const
{
    const auto s = shape(view);
    if (!s || handle == Handle::None)
        return std::nullopt;
    QPointF viewPoint;
    switch (handle) {
    case Handle::Rotate: viewPoint = s->knob; break;
    case Handle::Scale: viewPoint = s->corners[2]; break;
    case Handle::Move: viewPoint = s->center; break;
    case Handle::None: return std::nullopt;
    }
    return view.viewToDocument().map(viewPoint);
}

bool CameraGizmo::press(const QPointF &docPos, const ViewTransform &view)
{
    const Handle handle = handleAt(docPos, view);
    if (handle == Handle::None)
        return false;
    const auto &camera = m_document->camera();
    Drag drag;
    drag.handle = handle;
    drag.before = camera;
    drag.frame = m_document->currentFrame();
    drag.pose = camera->poseAt(drag.frame);
    drag.press = docPos;
    drag.lastAngle = degreesOf(docPos - QPointF(drag.pose.centerX, drag.pose.centerY));
    m_drag = drag;
    return true;
}

void CameraGizmo::show(const doc::CameraPose &pose)
{
    // The camera as it is shown while the pen is down: the pose is its key at the frame.
    std::optional<doc::Camera> camera = m_drag->before;
    camera->setKey(m_drag->frame, pose);
    m_document->previewCamera(camera);
}

void CameraGizmo::move(const QPointF &docPos)
{
    if (!m_drag)
        return;
    const QPointF center(m_drag->pose.centerX, m_drag->pose.centerY);
    doc::CameraPose pose = m_drag->pose;
    switch (m_drag->handle) {
    case Handle::Move: { // the frame follows the pen
        const QPointF delta = docPos - m_drag->press;
        pose.centerX += delta.x();
        pose.centerY += delta.y();
        break;
    }
    case Handle::Scale: { // a corner out is a frame bigger: the camera sees more, so it is less zoomed
        const double was = QLineF(center, m_drag->press).length();
        const double now = QLineF(center, docPos).length();
        if (was > 1e-6 && now > 1e-6)
            pose.zoom = std::clamp(m_drag->pose.zoom * was / now, kMinZoom, kMaxZoom);
        break;
    }
    case Handle::Rotate: { // the frame turns with the pen around its middle
        const double now = degreesOf(docPos - center);
        double step = now - m_drag->lastAngle;
        while (step > 180.0)
            step -= 360.0; // the shorter way: a sample never jumps half a turn...
        while (step < -180.0)
            step += 360.0;
        m_drag->turned += step; // ...and the samples add up: the pen going round and round turns the frame on
        m_drag->lastAngle = now;
        pose.rotation = m_drag->pose.rotation + m_drag->turned;
        break;
    }
    case Handle::None: return;
    }
    m_drag->moved = m_drag->moved || !(pose == m_drag->pose);
    show(pose);
}

void CameraGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return;
    move(docPos);
    const Drag drag = *m_drag;
    m_drag.reset();
    if (!drag.moved) {
        m_document->previewCamera(drag.before);
        return;
    }
    m_document->commitCamera(drag.before, m_document->camera());
}

void CameraGizmo::cancel()
{
    if (!m_drag)
        return;
    m_document->previewCamera(m_drag->before);
    m_drag.reset();
}

bool CameraGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const Handle now = handleAt(docPos, view);
    if (now == m_hovered)
        return false;
    m_hovered = now;
    return true;
}

void CameraGizmo::paint(QPainter &p, const ViewTransform &view) const
{
    const auto s = shape(view);
    if (!s)
        return;
    const auto &camera = m_document->camera();
    const bool selected = m_document->cameraSelected();
    const QPolygonF frame({s->corners[0], s->corners[1], s->corners[2], s->corners[3]});

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (!camera->visible) { // off: nothing but a dashed outline, and only while it is the selected row
        if (selected) {
            p.setPen(QPen(QColor(150, 150, 150, 200), 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(frame);
        }
        p.restore();
        return;
    }
    if (!selected) { // faint: there, and out of the way
        p.setPen(QPen(QColor(kRed.red(), kRed.green(), kRed.blue(), 90), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(frame);
        p.restore();
        return;
    }

    // Selected: what the camera does not see is dimmed, the frame is strong, with its handles.
    QPainterPath outside;
    outside.addRect(QRectF(QPointF(-4, -4), QSizeF(view.viewportSize()) + QSizeF(8, 8)));
    QPainterPath inside;
    inside.addPolygon(frame);
    inside.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawPath(outside.subtracted(inside));
    p.setPen(QPen(kRed, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(frame);
    // Its middle, and whether the frame shown is a key of the camera.
    const bool key = camera->keyAt(m_document->currentFrame()) != nullptr;
    p.drawLine(s->center + QPointF(-6, 0), s->center + QPointF(6, 0));
    p.drawLine(s->center + QPointF(0, -6), s->center + QPointF(0, 6));
    const QPointF topMiddle = (s->corners[0] + s->corners[1]) / 2.0;
    p.drawLine(topMiddle, s->knob);
    p.setBrush(m_hovered == Handle::Rotate ? QColor(255, 255, 255) : QColor(kRed));
    p.drawEllipse(s->knob, kKnob, kKnob);
    for (const QPointF &corner : s->corners) {
        p.setBrush(m_hovered == Handle::Scale ? QColor(kRed) : QColor(255, 255, 255));
        p.drawRect(QRectF(corner.x() - kCorner, corner.y() - kCorner, 2 * kCorner, 2 * kCorner));
    }
    // A key's diamond at the frame's top left corner, inside.
    if (key) {
        const QPointF at = s->corners[0] + QPointF(18, 18);
        QPolygonF diamond({at + QPointF(0, -8), at + QPointF(8, 0), at + QPointF(0, 8), at + QPointF(-8, 0)});
        p.setBrush(kRed);
        p.setPen(QPen(Qt::white, 1));
        p.drawPolygon(diamond);
    }
    p.restore();
}

} // namespace mothion::canvas
