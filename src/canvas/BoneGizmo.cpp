#include "canvas/BoneGizmo.h"

#include "canvas/ViewTransform.h"

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTimer>

#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr double kDegrees = 180.0 / std::numbers::pi;

QPointF toQ(deform::Vec2 v)
{
    return {v.x, v.y};
}

double segmentDistance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double length2 = QPointF::dotProduct(ab, ab);
    const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / length2, 0.0, 1.0) : 0.0;
    return QLineF(p, a + ab * t).length();
}

bool hasChildren(const doc::Deformer &deformer, int bone)
{
    for (const doc::Bone &b : deformer.bones)
        if (b.parent == bone)
            return true;
    return false;
}

// Whether `bone` hangs from `ancestor` (or is it).
bool hangsFrom(const doc::Deformer &deformer, int bone, int ancestor)
{
    for (int guard = 0; bone >= 0 && guard <= int(deformer.bones.size()); ++guard) {
        if (bone == ancestor)
            return true;
        bone = deformer.bones[std::size_t(bone)].parent;
    }
    return false;
}

// A bone's start is drawn (and taken) on its own only where it is not its parent's end.
bool ownOrigin(const doc::Deformer &deformer, const std::vector<deform::BoneFrame> &frames, std::size_t bone)
{
    const int parent = deformer.bones[bone].parent;
    return parent < 0 || puppet::length(frames[bone].origin - frames[std::size_t(parent)].end()) > 1e-6;
}

} // namespace

BoneGizmo::BoneGizmo(document::DocumentController *document)
    : m_document(document)
{
}

BoneGizmo::~BoneGizmo() = default;

const doc::Cel *BoneGizmo::cel() const
{
    if (m_document->cameraSelected())
        return nullptr;
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    return cel && cel->properties().deformer == doc::DeformerKind::Bones ? cel : nullptr;
}

bool BoneGizmo::shown() const
{
    return cel() != nullptr;
}

bool BoneGizmo::editable() const
{
    const doc::Cel *c = cel();
    return c && !c->properties().locked;
}

int BoneGizmo::chosenBone() const
{
    const doc::Cel *c = cel();
    return c && c->id() == m_chosenCel && m_chosen >= 0 && m_chosen < int(c->deformer().bones.size()) ? m_chosen : -1;
}

QTransform BoneGizmo::toPage() const
{
    const doc::Cel *c = cel();
    if (!c)
        return {};
    const doc::Matrix2x3 m = m_document->document().followMatrix(*c, m_document->currentFrame());
    return QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
}

deform::Vec2 BoneGizmo::inBones(const QPointF &docPos) const
{
    const QPointF p = toPage().inverted().map(docPos);
    return {p.x(), p.y()};
}

std::vector<deform::BoneFrame> BoneGizmo::frames() const
{
    const doc::Cel *c = cel();
    if (!c)
        return {};
    if (m_drag && m_rig)
        return deform::boneFrames(m_drag->rig, m_drag->rig.rest);
    if (m_rig)
        return deform::boneFrames(c->deformer(), c->deformer().rest);
    return deform::boneFrames(c->deformer(), m_document->document().deformPoseAt(*c, m_document->currentFrame()));
}

BoneGizmo::Hit BoneGizmo::hitAt(const QPointF &docPos, const ViewTransform &view) const
{
    const doc::Cel *c = cel();
    if (!c)
        return {};
    const QTransform toView = toPage() * view.documentToView();
    const QPointF at = view.documentToView().map(docPos);
    const std::vector<deform::BoneFrame> f = frames();
    const doc::Deformer &deformer = m_drag && m_rig ? m_drag->rig : c->deformer();
    Hit best;
    double nearest = kGrab;
    // The joints first: the last bones on top.
    for (std::size_t i = f.size(); i-- > 0;) {
        const double end = QLineF(at, toView.map(toQ(f[i].end()))).length();
        if (end <= nearest) {
            nearest = end;
            best = {int(i), Part::End};
        }
        if (ownOrigin(deformer, f, i)) {
            const double origin = QLineF(at, toView.map(toQ(f[i].origin))).length();
            if (origin < nearest) {
                nearest = origin;
                best = {int(i), Part::Origin};
            }
        }
    }
    if (best.bone >= 0)
        return best;
    for (std::size_t i = f.size(); i-- > 0;) {
        const double d = segmentDistance(at, toView.map(toQ(f[i].origin)), toView.map(toQ(f[i].end())));
        if (d <= nearest) {
            nearest = d;
            best = {int(i), Part::Body};
        }
    }
    return best;
}

std::optional<QPointF> BoneGizmo::partPoint(int bone, Part part) const
{
    const std::vector<deform::BoneFrame> f = frames();
    if (bone < 0 || bone >= int(f.size()))
        return std::nullopt;
    const deform::BoneFrame &b = f[std::size_t(bone)];
    const deform::Vec2 p = part == Part::Origin ? b.origin : part == Part::End ? b.end() : (b.origin + b.end()) * 0.5;
    return toPage().map(toQ(p));
}

bool BoneGizmo::press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers)
{
    const doc::Cel *c = cel();
    if (!c || !editable() || m_drag)
        return false;
    const Hit hit = hitAt(docPos, view);
    Drag drag;
    drag.start = drag.now = inBones(docPos);
    drag.startFrames = frames();
    drag.bone = hit.bone;
    m_chosenCel = c->id();
    if (m_rig) {
        drag.rig = c->deformer();
        if (hit.part == Part::End) {
            const bool branch = modifiers & Qt::ControlModifier;
            const bool moveIt = (modifiers & Qt::AltModifier) || (hasChildren(drag.rig, hit.bone) && !branch);
            drag.kind = moveIt ? Drag::Kind::MoveEnd : Drag::Kind::NewBone;
            if (!moveIt)
                drag.start = drag.now = drag.startFrames[std::size_t(hit.bone)].end(); // from the end itself
        } else if (hit.part == Part::Origin) {
            drag.kind = Drag::Kind::MoveOrigin;
        } else if (hit.part == Part::Body) {
            drag.kind = Drag::Kind::MoveBody;
        } else {
            drag.kind = Drag::Kind::NewBone; // a root
        }
        m_chosen = hit.bone;
    } else {
        if (hit.bone < 0) {
            m_chosen = -1; // a click on nothing: no bone chosen
            return true;
        }
        m_chosen = hit.bone;
        drag.kind = hit.part == Part::Origin && c->deformer().bones[std::size_t(hit.bone)].parent < 0 ? Drag::Kind::Translate
                                                                                                        : Drag::Kind::Turn;
        drag.stretch = modifiers & Qt::AltModifier;
        drag.before = c->motion();
        const int frame = m_document->currentFrame();
        if (!c->motion().hasKeys(doc::Channel::Deform))
            m_document->beginFirstKey(c->id(), frame);
        drag.pose = m_document->document().deformPoseAt(*c, frame);
    }
    m_drag = std::move(drag);
    return true;
}

void BoneGizmo::apply(Drag &drag)
{
    const doc::Cel *c = cel();
    if (!c)
        return;
    std::vector<deform::BoneFrame> f = drag.startFrames;
    const std::size_t b = std::size_t(std::max(0, drag.bone));
    const deform::Vec2 delta = drag.now - drag.start;
    switch (drag.kind) {
    case Drag::Kind::NewBone: return; // drawn from the press to the pointer, made on release
    case Drag::Kind::MoveEnd: {
        const deform::Vec2 oldEnd = f[b].end();
        const deform::Vec2 way = drag.now - f[b].origin;
        f[b].angle = std::atan2(way.y, way.x);
        f[b].length = puppet::length(way);
        // The bones that start there start where it went, their ends staying.
        for (std::size_t i = 0; i < f.size(); ++i)
            if (drag.rig.bones[i].parent == int(b) && puppet::length(f[i].origin - oldEnd) < 1e-6) {
                const deform::Vec2 to = f[i].end() - drag.now;
                f[i].origin = drag.now;
                f[i].angle = std::atan2(to.y, to.x);
                f[i].length = puppet::length(to);
            }
        break;
    }
    case Drag::Kind::MoveOrigin: {
        const deform::Vec2 to = f[b].end() - drag.now;
        f[b].origin = drag.now;
        f[b].angle = std::atan2(to.y, to.x);
        f[b].length = puppet::length(to);
        break;
    }
    case Drag::Kind::MoveBody:
        for (std::size_t i = 0; i < f.size(); ++i)
            if (hangsFrom(drag.rig, int(i), int(b)))
                f[i].origin = f[i].origin + delta;
        break;
    case Drag::Kind::Turn: {
        std::vector<double> pose = drag.pose;
        const deform::Vec2 way = drag.now - f[b].origin;
        if (puppet::length(way) < 1e-9)
            return;
        const double turn = std::remainder(std::atan2(way.y, way.x) - f[b].angle, 2 * std::numbers::pi);
        pose[b * doc::kBoneParams + doc::BoneAngle] += turn * kDegrees;
        if (drag.stretch)
            pose[b * doc::kBoneParams + doc::BoneLength] = puppet::length(way);
        doc::CelPose p;
        p.deform = std::move(pose);
        m_document->previewCelPose(c->id(), m_document->currentFrame(), p, {doc::Channel::Deform});
        return;
    }
    case Drag::Kind::Translate: {
        std::vector<double> pose = drag.pose;
        pose[b * doc::kBoneParams + doc::BoneX] += delta.x;
        pose[b * doc::kBoneParams + doc::BoneY] += delta.y;
        doc::CelPose p;
        p.deform = std::move(pose);
        m_document->previewCelPose(c->id(), m_document->currentFrame(), p, {doc::Channel::Deform});
        return;
    }
    }
    drag.rig.rest = deform::poseFromFrames(drag.rig, f, c->deformer().rest);
}

void BoneGizmo::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    m_drag->now = inBones(docPos);
    if (m_drag->kind == Drag::Kind::Turn)
        m_drag->stretch = modifiers & Qt::AltModifier;
    m_drag->moved = m_drag->moved || puppet::length(m_drag->now - m_drag->start) > 1e-9;
    if (m_rig) { // only the bones are drawn again: at once
        apply(*m_drag);
        return;
    }
    m_pending = true;
    const qint64 wait = std::max<qint64>(m_drawCost, 8); // at most about 120 drawings a second
    const qint64 since = m_lastDraw.isValid() ? m_lastDraw.elapsed() : wait;
    if (since >= wait) {
        applyPending();
        return;
    }
    if (!m_timer) {
        m_timer = std::make_unique<QTimer>();
        m_timer->setSingleShot(true);
        QObject::connect(m_timer.get(), &QTimer::timeout, m_timer.get(), [this] {
            applyPending();
            if (m_repaint)
                m_repaint();
        });
    }
    if (!m_timer->isActive())
        m_timer->start(int(wait - since));
}

void BoneGizmo::applyPending()
{
    if (m_timer)
        m_timer->stop();
    if (!m_pending || !m_drag)
        return;
    m_pending = false;
    QElapsedTimer cost;
    cost.start();
    apply(*m_drag);
    m_drawCost = cost.elapsed();
    m_lastDraw.start();
}

void BoneGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return;
    m_pending = false; // the release settles at its own place
    if (m_timer)
        m_timer->stop();
    m_drag->now = inBones(docPos);
    m_drag->moved = m_drag->moved || puppet::length(m_drag->now - m_drag->start) > 1e-9;
    if (m_drag->moved)
        apply(*m_drag);
    Drag drag = std::move(*m_drag);
    m_drag.reset();
    const doc::Cel *c = cel();
    if (!c)
        return;
    if (!m_rig) {
        m_document->finishCelPose(c->id(), drag.before);
        return;
    }
    if (!drag.moved)
        return; // a click: the bone chosen (or none)
    if (drag.kind == Drag::Kind::NewBone) {
        const int made = m_document->addBone(c->id(), drag.bone, toQ(drag.start), toQ(drag.now));
        if (made >= 0)
            m_chosen = made;
        return;
    }
    m_document->setDeformerRest(c->id(), drag.rig);
}

void BoneGizmo::cancel()
{
    if (!m_drag)
        return;
    m_pending = false;
    if (m_timer)
        m_timer->stop();
    const Drag drag = std::move(*m_drag);
    m_drag.reset();
    if (!m_rig)
        if (const doc::Cel *c = cel())
            m_document->cancelCelPose(c->id(), drag.before);
}

bool BoneGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const Hit hit = editable() ? hitAt(docPos, view) : Hit{};
    if (hit.bone == m_hovered.bone && hit.part == m_hovered.part)
        return false;
    m_hovered = hit;
    return true;
}

bool BoneGizmo::removeChosenBone()
{
    const int bone = chosenBone();
    if (!m_rig || bone < 0 || !editable())
        return false;
    m_chosen = -1;
    return m_document->removeBone(cel()->id(), bone);
}

void BoneGizmo::paint(QPainter &painter, const ViewTransform &view) const
{
    const doc::Cel *c = cel();
    if (!c)
        return;
    const std::vector<deform::BoneFrame> f = frames();
    const doc::Deformer &deformer = m_drag && m_rig ? m_drag->rig : c->deformer();
    const QTransform toView = toPage() * view.documentToView();
    const double scale = std::sqrt(std::abs(toView.determinant()));
    painter.save();
    painter.setWorldTransform(QTransform()); // view coordinates
    painter.setRenderHint(QPainter::Antialiasing);
    const bool live = editable() && c->properties().visible;
    const QColor accent = m_rig ? QColor(64, 196, 120) : QColor(80, 160, 255);
    const QColor ink = live ? accent : accent.darker(160);
    const int chosen = chosenBone();

    // The zone of the chosen bone, in rig mode: all of it within the radius, fading to the dashed line.
    if (m_rig && chosen >= 0 && std::size_t(chosen) < f.size()) {
        const doc::Bone &bone = deformer.bones[std::size_t(chosen)];
        QPainterPath axis;
        axis.moveTo(toView.map(toQ(f[std::size_t(chosen)].origin)));
        axis.lineTo(toView.map(toQ(f[std::size_t(chosen)].end())));
        QPainterPathStroker inner, outer;
        inner.setCapStyle(Qt::RoundCap);
        outer.setCapStyle(Qt::RoundCap);
        inner.setWidth(std::max(1.0, 2 * bone.radius * scale));
        outer.setWidth(std::max(1.0, 2 * (bone.radius + bone.fade) * scale));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 40));
        painter.drawPath(inner.createStroke(axis));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 160), 1.0, Qt::DashLine));
        painter.drawPath(outer.createStroke(axis));
    }

    // A bone: a long diamond from its start to its end.
    auto drawBone = [&](const QPointF &from, const QPointF &to, bool strong) {
        const QLineF line(from, to);
        const double length = line.length();
        if (length < 1e-6)
            return;
        const QPointF dir = (to - from) / length, side(-dir.y(), dir.x());
        const double width = std::clamp(length * 0.12, 3.0, 9.0);
        const QPointF knee = from + dir * std::min(length * 0.2, 24.0);
        const QPolygonF shape{from, knee + side * width, to, knee - side * width};
        painter.setPen(QPen(Qt::white, 3.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(shape);
        painter.setPen(QPen(strong ? ink.lighter(130) : ink, strong ? 2.0 : 1.4));
        painter.setBrush(QColor(ink.red(), ink.green(), ink.blue(), strong ? 150 : 90));
        painter.drawPolygon(shape);
    };
    for (std::size_t i = 0; i < f.size(); ++i) {
        const bool strong = int(i) == chosen || (m_hovered.bone == int(i) && m_hovered.part == Part::Body);
        drawBone(toView.map(toQ(f[i].origin)), toView.map(toQ(f[i].end())), strong);
        // A bone that starts away from its parent's end: a faint line to it.
        const int parent = deformer.bones[i].parent;
        if (parent >= 0 && ownOrigin(deformer, f, i)) {
            painter.setPen(QPen(ink, 1.0, Qt::DotLine));
            painter.drawLine(toView.map(toQ(f[std::size_t(parent)].end())), toView.map(toQ(f[i].origin)));
        }
    }
    // The bone being drawn.
    if (m_drag && m_rig && m_drag->kind == Drag::Kind::NewBone && m_drag->moved)
        drawBone(toView.map(toQ(m_drag->start)), toView.map(toQ(m_drag->now)), true);
    // The joints.
    auto joint = [&](const QPointF &at, bool hot) {
        painter.setPen(QPen(ink, 1.6));
        painter.setBrush(hot ? ink : QColor(Qt::white));
        painter.drawEllipse(at, hot ? 5.0 : 4.0, hot ? 5.0 : 4.0);
    };
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (ownOrigin(deformer, f, i))
            joint(toView.map(toQ(f[i].origin)), m_hovered.bone == int(i) && m_hovered.part == Part::Origin);
        joint(toView.map(toQ(f[i].end())), m_hovered.bone == int(i) && m_hovered.part == Part::End);
    }
    painter.restore();
}

} // namespace mothion::canvas
