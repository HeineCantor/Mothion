#include "canvas/TrajectoryGizmo.h"

#include "canvas/ViewTransform.h"

#include <QLineF>
#include <QPainterPath>
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
    return cel && !cel->properties().locked && cel->properties().visible;
}

std::optional<TrajectoryGizmo::Shape> TrajectoryGizmo::shape(const ViewTransform &view) const
{
    if (!shown())
        return std::nullopt;
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    const doc::CelMotion &motion = cel->motion();
    const doc::Document &d = m_document->document();
    const int frame = m_document->currentFrame();
    Shape s;
    if (cel->properties().visible) // an inactive trajectory does nothing of its own
        s.pose = motion.poseAt(frame);
    s.parents = d.trajectoryParentMatrix(cel->id(), frame);
    // Where the pivot goes: its own move, then what the parents do; and as turned and scaled by both.
    const doc::Matrix2x3 world = s.pose.matrix(motion.pivotX, motion.pivotY).then(s.parents);
    double cx = 0, cy = 0;
    world.map(motion.pivotX, motion.pivotY, cx, cy);
    s.centerDoc = QPointF(cx, cy);
    const QTransform toView = view.documentToView();
    s.center = toView.map(s.centerDoc);
    s.radius = std::max(kMinRadius, kRadius * std::hypot(world.a, world.b));
    if (const doc::Cel *parent = d.trajectoryParent(*cel)) {
        double px = 0, py = 0;
        d.trajectoryMatrix(parent->id(), frame).map(parent->motion().pivotX, parent->motion().pivotY, px, py);
        s.parentCenter = toView.map(QPointF(px, py));
    }
    // The arrow's way on screen: the rotation's way in the document, seen through the view (which may be turned).
    const double angle = std::atan2(world.b, world.a);
    const QPointF ahead = toView.map(s.centerDoc + QPointF(std::cos(angle), std::sin(angle)));
    QPointF way = ahead - s.center;
    const double length = std::hypot(way.x(), way.y());
    s.direction = length > 0 ? way / length : QPointF(1, 0);
    s.tip = s.center + s.direction * (s.radius + kArrowAway);
    return s;
}

bool TrajectoryGizmo::pathEditable() const
{
    const doc::Cel *trajectory = pathTrajectory();
    return trajectory && !trajectory->properties().locked;
}

std::optional<int> TrajectoryGizmo::keyAt(const QPointF &docPos, const ViewTransform &view) const
{
    const doc::Cel *trajectory = pathTrajectory();
    if (!trajectory)
        return std::nullopt;
    const QTransform toView = view.documentToView();
    const QPointF at = toView.map(docPos);
    std::optional<int> found;
    double best = kGrab;
    for (const doc::CelKey &key : motionOf(*trajectory).keys(doc::Channel::Position)) {
        const double distance = QLineF(at, toView.map(pathPoint(*trajectory, key.frame))).length();
        if (distance <= best) {
            best = distance;
            found = key.frame;
        }
    }
    return found;
}

TrajectoryGizmo::Handle TrajectoryGizmo::handleAt(const QPointF &docPos, const ViewTransform &view) const
{
    const Handle onCircle = circleHandleAt(docPos, view);
    if (onCircle != Handle::None)
        return onCircle;
    return pathEditable() && keyAt(docPos, view) ? Handle::Key : Handle::None;
}

TrajectoryGizmo::Handle TrajectoryGizmo::circleHandleAt(const QPointF &docPos, const ViewTransform &view) const
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
    case Handle::Key:
    case Handle::None: return std::nullopt;
    }
    return view.mapToDocument(at);
}

std::set<int> TrajectoryGizmo::chosenKeys() const
{
    const doc::Cel *trajectory = pathTrajectory();
    if (!trajectory || trajectory->id() != m_chosenCel)
        return {};
    std::set<int> keys;
    const doc::CelMotion &motion = motionOf(*trajectory);
    for (int frame : m_chosen)
        if (motion.keyAt(doc::Channel::Position, frame)) // one taken away or moved since is not chosen any more
            keys.insert(frame);
    return keys;
}

bool TrajectoryGizmo::beginMarquee(const QPointF &docPos, bool lasso, Qt::KeyboardModifiers modifiers)
{
    if (m_drag || !pathEditable())
        return false;
    m_marquee = Marquee{lasso, QPolygonF{docPos, docPos}, modifiers};
    if (!lasso)
        m_marquee->points.resize(2); // the two corners
    return true;
}

QPolygonF TrajectoryGizmo::marqueeArea() const
{
    if (!m_marquee)
        return {};
    if (m_marquee->lasso)
        return m_marquee->points;
    return QPolygonF(QRectF(m_marquee->points[0], m_marquee->points[1]).normalized());
}

bool TrajectoryGizmo::press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers)
{
    if (!editable() && !pathEditable())
        return false;
    const Handle handle = handleAt(docPos, view);
    if (handle == Handle::None || (handle != Handle::Key && !editable()))
        return false;
    if (handle == Handle::Key) { // a key of the path, dragged along the page: its position follows the pen
        const doc::Cel *trajectory = pathTrajectory();
        const int frame = *keyAt(docPos, view);
        Drag d;
        d.handle = handle;
        d.cel = trajectory->id();
        d.frame = frame;
        d.before = trajectory->motion();
        d.press = docPos;
        d.toView = view.documentToView();
        d.centerDoc = pathPoint(*trajectory, frame);
        d.parentsBack = m_document->document().trajectoryParentMatrix(d.cel, frame).inverted();
        d.start = m_document->celPoseAt(d.cel, frame);
        m_hovered = handle;
        m_hoverKey = frame;
        m_drag = d;
        // The keys chosen: a key not among them is chosen alone (with Shift, added); the others chosen go with it.
        std::set<int> chosen = chosenKeys();
        if (!chosen.contains(frame)) {
            if (!(modifiers & Qt::ShiftModifier))
                chosen.clear();
            chosen.insert(frame);
        }
        m_chosenCel = d.cel;
        m_chosen = chosen;
        m_followers.clear();
        for (int other : chosen) {
            if (other == frame)
                continue;
            Follower f;
            f.frame = other;
            f.centerDoc = pathPoint(*trajectory, other);
            f.parentsBack = m_document->document().trajectoryParentMatrix(d.cel, other).inverted();
            f.start = m_document->celPoseAt(d.cel, other);
            m_followers.push_back(f);
        }
        return true;
    }
    const std::optional<Shape> s = shape(view);
    Drag d;
    d.handle = handle;
    d.cel = m_document->currentCel();
    d.frame = m_document->currentFrame();
    d.before = m_document->document().cel(d.cel)->motion();
    d.press = docPos;
    d.toView = view.documentToView();
    d.centerDoc = s->centerDoc;
    d.parents = s->parents;
    d.parentsBack = s->parents.inverted();
    d.turnWay = s->parents.a * s->parents.d - s->parents.b * s->parents.c < 0 ? -1.0 : 1.0;
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
    if (m_marquee) {
        if (m_marquee->lasso)
            m_marquee->points << docPos;
        else
            m_marquee->points[1] = docPos;
        return;
    }
    if (!m_drag)
        return;
    const Drag &d = *m_drag;
    doc::CelPose pose = d.start;
    std::vector<doc::Channel> channels;
    switch (d.handle) {
    case Handle::Key: // the key's position goes where the pen takes the dot (through what the parents do)
    case Handle::Move: {
        // The circle goes where the pen takes it; its own pose is what, after the parents, puts it there.
        const doc::CelMotion &motion = m_document->document().cel(d.cel)->motion();
        const QPointF target = d.centerDoc + (docPos - d.press);
        double lx = 0, ly = 0;
        d.parentsBack.map(target.x(), target.y(), lx, ly);
        pose.x = lx - motion.pivotX;
        pose.y = ly - motion.pivotY;
        channels = {doc::Channel::Position};
        break;
    }
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
        pose.rotation = d.start.rotation + degrees * d.turnWay; // (turned the other way where the parents are flipped)
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
    if (d.handle == Handle::Key && !m_followers.empty()) {
        // The other keys chosen move as far on the page, each through what the parents do on its own frame.
        const doc::CelMotion &motion = m_document->document().cel(d.cel)->motion();
        std::vector<document::DocumentController::CelPoseChange> changes{{d.cel, d.frame, pose, channels}};
        for (const Follower &f : m_followers) {
            const QPointF target = f.centerDoc + (docPos - d.press);
            double lx = 0, ly = 0;
            f.parentsBack.map(target.x(), target.y(), lx, ly);
            doc::CelPose moved = f.start;
            moved.x = lx - motion.pivotX;
            moved.y = ly - motion.pivotY;
            changes.push_back({d.cel, f.frame, moved, channels});
        }
        m_document->previewCelPoses(changes);
        return;
    }
    m_document->previewCelPose(d.cel, d.frame, pose, channels);
}

void TrajectoryGizmo::release(const QPointF &docPos)
{
    if (m_marquee) {
        move(docPos);
        const Marquee marquee = *m_marquee;
        const QPolygonF area = marqueeArea();
        m_marquee.reset();
        const doc::Cel *trajectory = pathTrajectory();
        if (!trajectory)
            return;
        std::set<int> chosen = chosenKeys();
        if (!(marquee.modifiers & (Qt::ShiftModifier | Qt::AltModifier)))
            chosen.clear();
        QPainterPath inside;
        inside.addPolygon(area);
        inside.closeSubpath();
        for (const doc::CelKey &key : motionOf(*trajectory).keys(doc::Channel::Position)) {
            if (!inside.contains(pathPoint(*trajectory, key.frame)))
                continue;
            if (marquee.modifiers & Qt::AltModifier)
                chosen.erase(key.frame);
            else
                chosen.insert(key.frame);
        }
        m_chosenCel = trajectory->id();
        m_chosen = chosen;
        return;
    }
    if (!m_drag)
        return;
    move(docPos);
    const Drag d = *m_drag;
    m_drag.reset();
    m_document->finishCelPose(d.cel, d.before); // one undo step (none for a pose that stays only on the frame)
}

void TrajectoryGizmo::cancel()
{
    m_marquee.reset();
    if (!m_drag)
        return;
    const Drag d = *m_drag;
    m_drag.reset();
    m_document->cancelCelPose(d.cel, d.before);
}

bool TrajectoryGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const Handle now = editable() || pathEditable() ? handleAt(docPos, view) : Handle::None;
    const int key = now == Handle::Key ? keyAt(docPos, view).value_or(-1) : -1;
    if (now == m_hovered && key == m_hoverKey)
        return false;
    m_hovered = now;
    m_hoverKey = key;
    return true;
}

const doc::CelMotion &TrajectoryGizmo::motionOf(const doc::Cel &cel) const
{
    const doc::CelMotion *preview = m_document->previewedMotion(cel.id()); // keys being dragged on the timeline
    return preview ? *preview : cel.motion();
}

const doc::Cel *TrajectoryGizmo::pathTrajectory() const
{
    if (!m_pathVisible || m_document->cameraSelected())
        return nullptr;
    const doc::Document &d = m_document->document();
    const doc::Cel *cel = d.cel(m_document->currentCel());
    if (!cel)
        return nullptr;
    const doc::Cel *trajectory = cel->properties().trajectory ? cel : d.followedTrajectory(*cel);
    return trajectory && trajectory->properties().visible ? trajectory : nullptr; // an inactive one does nothing
}

QPointF TrajectoryGizmo::pathPoint(const doc::Cel &trajectory, int frame) const
{
    const doc::Document &d = m_document->document();
    const doc::CelMotion &motion = motionOf(trajectory);
    const doc::Matrix2x3 world =
        motion.interpolatedAt(frame).matrix(motion.pivotX, motion.pivotY).then(d.trajectoryParentMatrix(trajectory.id(), frame));
    double x = 0, y = 0;
    world.map(motion.pivotX, motion.pivotY, x, y);
    return {x, y};
}

bool TrajectoryGizmo::pathRange(const doc::Cel &trajectory, int *first, int *last) const
{
    const doc::Document &d = m_document->document();
    int lo = -1, hi = -1;
    int guard = 0;
    for (const doc::Cel *t = &trajectory; t && guard < 64; t = d.trajectoryParent(*t), ++guard) { // its parents move it too
        const std::vector<int> keys = motionOf(*t).keyFrames();
        if (keys.empty())
            continue;
        lo = lo < 0 ? keys.front() : std::min(lo, keys.front());
        hi = std::max(hi, keys.back());
    }
    if (lo < 0)
        return false;
    *first = lo;
    *last = hi;
    return true;
}

void TrajectoryGizmo::paintPath(QPainter &painter, const ViewTransform &view) const
{
    const doc::Cel *trajectory = pathTrajectory();
    int first = 0, last = 0;
    if (!trajectory || !pathRange(*trajectory, &first, &last))
        return;
    constexpr int kMostFrames = 4000; // a very long trajectory is drawn every few frames
    const int step = std::max(1, (last - first + kMostFrames) / kMostFrames);
    const QTransform toView = view.documentToView();
    QPolygonF line;
    for (int f = first; f <= last; f += step)
        line << toView.map(pathPoint(*trajectory, f));
    if (last > first && (last - first) % step != 0)
        line << toView.map(pathPoint(*trajectory, last));
    painter.save();
    painter.setWorldTransform(QTransform()); // view coordinates
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const QColor accent(240, 178, 61); // the keys (as the circle)
    const QColor pathColor(46, 190, 224); // the way between them: cyan
    const QColor faint(pathColor.red(), pathColor.green(), pathColor.blue(), 235);
    if (line.size() > 1) {
        painter.setPen(QPen(Qt::white, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); // an edge of white
        painter.drawPolyline(line);
        painter.setPen(QPen(faint, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(line);
    }
    // A dot on each frame (when there is room for them), and a bigger one on each key.
    if (step == 1) {
        double gap = 1e9; // on screen, between two frames
        if (line.size() > 1)
            gap = QLineF(line[0], line[1]).length();
        if (gap >= 5.0) {
            painter.setPen(QPen(QColor(255, 255, 255, 190), 0.8)); // a hair of white round each, to read on any picture
            painter.setBrush(faint);
            for (const QPointF &p : line)
                painter.drawEllipse(p, 2.1, 2.1);
        }
    }
    // The keys: a dot each (the ones of the position are dragged; a key of only the rotation or scale is a small ring).
    const bool editable = pathEditable();
    const std::set<int> chosen = chosenKeys();
    for (int f : motionOf(*trajectory).keyFrames()) {
        const QPointF at = toView.map(pathPoint(*trajectory, f));
        if (motionOf(*trajectory).keyAt(doc::Channel::Position, f)) {
            const bool picked = editable && (keyFrame() == f || chosen.contains(f));
            painter.setPen(QPen(Qt::white, 1.8));
            painter.setBrush(picked ? QColor(40, 110, 230) : accent);
            painter.drawEllipse(at, picked ? 6.0 : 5.0, picked ? 6.0 : 5.0);
        } else {
            painter.setPen(QPen(accent.darker(120), 1.6));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(at, 3.0, 3.0);
        }
    }
    // The box or lasso being drawn to choose keys.
    if (m_marquee) {
        const QPolygonF area = toView.map(marqueeArea());
        painter.setBrush(QColor(40, 110, 230, 30));
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawPolygon(area);
        painter.setPen(QPen(QColor(40, 110, 230), 1.0, Qt::DashLine));
        painter.drawPolygon(area);
    }
    painter.restore();
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
    // A faint line to the circle of the parent it follows.
    if (s->parentCenter) {
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 150), 1.2, Qt::DotLine));
        painter.drawLine(s->center, *s->parentCenter);
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 150));
        painter.drawEllipse(*s->parentCenter, 3.0, 3.0);
        painter.setBrush(Qt::NoBrush);
    }
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
