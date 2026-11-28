#include "canvas/ModelGizmo.h"

#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"

#include <QApplication>
#include <QLineF>
#include <QPainter>

#include <cmath>

namespace mothion::canvas {

namespace {

constexpr double kOrbitDegreesPerPixel = 0.4;
constexpr double kTwistStep = rig3d::kPi / 180.0; // a degree each step of the pointer

QPointF toQ(const rig3d::Vec3 &v)
{
    return {v.x, v.y};
}

} // namespace

ModelGizmo::ModelGizmo(document::DocumentController *document)
    : m_document(document)
{
}

const doc::Cel *ModelGizmo::cel() const
{
    if (m_document->cameraSelected())
        return nullptr;
    const doc::Cel *c = m_document->document().cel(m_document->currentCel());
    return c && c->properties().model3d && !c->model().isEmpty() ? c : nullptr;
}

bool ModelGizmo::shown() const
{
    const doc::Cel *c = cel();
    return c && c->properties().visible;
}

bool ModelGizmo::editable() const
{
    const doc::Cel *c = cel();
    return c && c->properties().visible && !c->properties().locked;
}

int ModelGizmo::chosenJoint() const
{
    const doc::Cel *c = cel();
    return c && c->id() == m_chosenCel && m_chosen >= 0 && m_chosen < int(c->model().model()->nodes.size()) ? m_chosen : -1;
}

QTransform ModelGizmo::toDocument() const
{
    const doc::Cel *c = cel();
    if (!c)
        return {};
    const doc::Document &d = m_document->document();
    const int frame = m_document->currentFrame();
    const doc::CelPose pose = c->motion().isEmpty() ? doc::CelPose{} : c->motion().poseAt(frame);
    const doc::Matrix2x3 m = pose.matrix(c->motion().pivotX, c->motion().pivotY).then(d.followMatrix(*c, frame));
    return QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
}

rig3d::Pose ModelGizmo::currentPose() const
{
    const doc::Cel *c = cel();
    if (m_drag)
        return m_drag->pose;
    return rig3d::Pose::fromNumbers(*c->model().model(), m_document->document().deformPoseAt(*c, m_document->currentFrame()));
}

rig3d::Projection ModelGizmo::projection(const rig3d::Pose &pose) const
{
    const doc::Document &d = m_document->document();
    return rig3d::projectionOf(*cel()->model().model(), pose.camera, d.width(), d.height());
}

QPointF ModelGizmo::onPage(const QPointF &docPos) const
{
    return toDocument().inverted().map(docPos);
}

std::vector<std::optional<QPointF>> ModelGizmo::jointPoints(const rig3d::Pose &pose) const
{
    const rig3d::Model &model = *cel()->model().model();
    const std::vector<rig3d::Mat4> g = rig3d::globalMatrices(model, pose);
    const rig3d::Projection p = projection(pose);
    const QTransform toDoc = toDocument();
    std::vector<std::optional<QPointF>> out(model.nodes.size());
    for (int n : model.joints()) {
        const rig3d::Vec3 page = p.toPage(g[std::size_t(n)].translationPart());
        if (page.z > -1 && page.z < 1) // in front of the camera
            out[std::size_t(n)] = toDoc.map(toQ(page));
    }
    return out;
}

std::optional<QPointF> ModelGizmo::jointPoint(int node) const
{
    if (!cel() || node < 0)
        return std::nullopt;
    const auto points = jointPoints(currentPose());
    return std::size_t(node) < points.size() ? points[std::size_t(node)] : std::nullopt;
}

int ModelGizmo::jointAt(const QPointF &docPos, const ViewTransform &view) const
{
    if (!cel())
        return -1;
    const QPointF at = view.documentToView().map(docPos);
    const auto points = jointPoints(currentPose());
    int best = -1;
    double bestDistance = kGrab;
    for (std::size_t n = 0; n < points.size(); ++n)
        if (points[n]) {
            const double d = QLineF(at, view.documentToView().map(*points[n])).length();
            if (d <= bestDistance) {
                bestDistance = d;
                best = int(n);
            }
        }
    return best;
}

bool ModelGizmo::press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers)
{
    const doc::Cel *c = cel();
    if (!c || !editable() || m_drag)
        return false;
    m_view = &view;
    const int joint = jointAt(docPos, view);
    const rig3d::Model &model = *c->model().model();
    if (joint >= 0) {
        m_chosenCel = c->id();
        m_chosen = joint;
        // A second click on the same joint, soon after a click (not a drag): its settings.
        const bool twice = m_lastClickJoint == joint && m_lastClick.isValid()
                           && m_lastClick.elapsed() <= QApplication::doubleClickInterval();
        m_lastClickJoint = -1;
        if (twice && m_editJoint) {
            m_editJoint(c->id(), joint);
            return true;
        }
        if (m_rig) { // nothing moves in rig mode: a press is a click
            m_lastClick.start();
            m_lastClickJoint = joint;
        }
    } else {
        m_lastClickJoint = -1;
        if (m_rig || !m_orbitOnEmpty)
            return false; // the press goes to the tool
    }
    if (m_rig) {
        return true; // in rig mode a joint is chosen, not moved
    }
    Drag drag;
    drag.node = joint;
    drag.startPage = drag.nowPage = onPage(docPos);
    drag.startView = drag.lastView = view.documentToView().map(docPos);
    if (joint < 0) {
        drag.kind = modifiers & Qt::AltModifier ? Drag::Kind::Pan
                    : modifiers & Qt::ControlModifier ? Drag::Kind::Dolly
                                                      : Drag::Kind::Orbit;
    } else if (modifiers & Qt::ShiftModifier) {
        drag.kind = Drag::Kind::Ik;
    } else if (modifiers & Qt::ControlModifier) {
        drag.kind = Drag::Kind::Twist;
    } else {
        const int parent = model.nodes[std::size_t(joint)].parent;
        drag.kind = parent >= 0 && model.isJoint(parent) ? Drag::Kind::Aim : Drag::Kind::Translate;
    }
    drag.before = c->motion();
    const int frame = m_document->currentFrame();
    if (!c->motion().hasKeys(doc::Channel::Deform))
        m_document->beginFirstKey(c->id(), frame);
    drag.startPose = drag.pose = rig3d::Pose::fromNumbers(model, m_document->document().deformPoseAt(*c, frame));
    m_drag = std::move(drag);
    return true;
}

void ModelGizmo::apply(Drag &drag)
{
    const doc::Cel *c = cel();
    if (!c)
        return;
    const rig3d::Model &model = *c->model().model();
    const rig3d::Rig rig = rig3d::Rig::make(model, c->model().joints);
    const QPointF delta = drag.lastView - drag.startView;
    switch (drag.kind) {
    case Drag::Kind::Orbit: {
        drag.pose = drag.startPose;
        drag.pose.camera.yaw = drag.startPose.camera.yaw - delta.x() * kOrbitDegreesPerPixel;
        drag.pose.camera.pitch = std::clamp(drag.startPose.camera.pitch + delta.y() * kOrbitDegreesPerPixel, -89.0, 89.0);
        break;
    }
    case Drag::Kind::Dolly: {
        drag.pose = drag.startPose;
        drag.pose.camera.distance = drag.startPose.camera.distance * std::exp(delta.y() * 0.01);
        break;
    }
    case Drag::Kind::Pan: {
        // The point looked at goes the other way of the pointer, in the plane across the view through it.
        drag.pose = drag.startPose;
        const rig3d::Projection p = projection(drag.startPose);
        const rig3d::Vec3 a = p.fromPage(drag.startPage.x(), drag.startPage.y(), drag.startPose.camera.target);
        const rig3d::Vec3 b = p.fromPage(drag.nowPage.x(), drag.nowPage.y(), drag.startPose.camera.target);
        drag.pose.camera.target = drag.startPose.camera.target - (b - a);
        break;
    }
    case Drag::Kind::Translate: {
        drag.pose = drag.startPose;
        const rig3d::Projection p = projection(drag.startPose);
        const std::vector<rig3d::Mat4> g = rig3d::globalMatrices(model, drag.startPose);
        const rig3d::Vec3 at = g[std::size_t(drag.node)].translationPart();
        const rig3d::Vec3 move = p.fromPage(drag.nowPage.x(), drag.nowPage.y(), at)
                                 - p.fromPage(drag.startPage.x(), drag.startPage.y(), at);
        // Into the parent's space (the node's place is told there).
        const int parent = model.nodes[std::size_t(drag.node)].parent;
        const rig3d::Vec3 local = parent >= 0 ? g[std::size_t(parent)].linear().inverse() * move : move;
        drag.pose.translations[std::size_t(drag.node)] = drag.startPose.translations[std::size_t(drag.node)] + local;
        break;
    }
    case Drag::Kind::Aim:
    case Drag::Kind::Ik: {
        // From the pose the drag began with each time: where the pointer is says where the bones are, whatever way it went.
        drag.pose = drag.startPose;
        const rig3d::Projection p = projection(drag.startPose);
        const rig3d::Vec3 at = rig3d::nodePosition(model, drag.startPose, drag.node);
        const rig3d::Vec3 target = p.fromPage(drag.nowPage.x(), drag.nowPage.y(), at);
        if (drag.kind == Drag::Kind::Ik)
            rig3d::solveIk(model, rig, drag.pose, drag.node, target);
        else
            rig3d::aimAt(model, rig, drag.pose, drag.node, target);
        break;
    }
    case Drag::Kind::Twist:
        break; // turned step by step in move()
    }
    publish(drag.pose);
}

void ModelGizmo::publish(const rig3d::Pose &pose)
{
    const doc::Cel *c = cel();
    if (!c || !m_drag)
        return;
    // Each turn on the same side of the sphere of quaternions as it began: the keys in between go the short way.
    rig3d::Pose kept = pose;
    for (std::size_t n = 0; n < kept.rotations.size() && n < m_drag->startPose.rotations.size(); ++n)
        if (rig3d::dot(kept.rotations[n], m_drag->startPose.rotations[n]) < 0) {
            const rig3d::Quat q = kept.rotations[n];
            kept.rotations[n] = {-q.w, -q.x, -q.y, -q.z};
        }
    doc::CelPose p;
    p.deform = kept.toNumbers();
    m_document->previewCelPose(c->id(), m_document->currentFrame(), p, {doc::Channel::Deform});
}

void ModelGizmo::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_drag)
        return;
    const QPointF view = m_view ? m_view->documentToView().map(docPos) : docPos;
    m_drag->nowPage = onPage(docPos);
    m_drag->moved = m_drag->moved || QLineF(view, m_drag->startView).length() > 0.5;
    if (m_drag->kind == Drag::Kind::Twist) {
        // CSP's twist: a step one way or the other for each move of the pointer, by the sign of how far it went right
        // and down together.
        const QPointF step = view - m_drag->lastView;
        const double sum = step.x() + step.y();
        m_drag->lastView = view;
        if (sum == 0)
            return;
        if (const doc::Cel *c = cel()) {
            const rig3d::Model &model = *c->model().model();
            rig3d::twistNode(model, rig3d::Rig::make(model, c->model().joints), m_drag->pose, m_drag->node,
                             sum > 0 ? kTwistStep : -kTwistStep);
            publish(m_drag->pose);
        }
        return;
    }
    m_drag->lastView = view;
    m_throttle.request([this] {
        if (m_drag)
            apply(*m_drag);
    });
}

void ModelGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return;
    m_throttle.cancel();
    if (m_drag->kind != Drag::Kind::Twist) {
        m_drag->nowPage = onPage(docPos);
        if (m_view)
            m_drag->lastView = m_view->documentToView().map(docPos);
        if (m_drag->moved)
            apply(*m_drag);
    }
    Drag drag = std::move(*m_drag);
    m_drag.reset();
    if (!drag.moved && drag.node >= 0) { // a click: the first of a double click, maybe
        m_lastClick.start();
        m_lastClickJoint = drag.node;
    }
    if (const doc::Cel *c = cel())
        m_document->finishCelPose(c->id(), drag.before);
}

void ModelGizmo::cancel()
{
    if (!m_drag)
        return;
    m_throttle.cancel();
    const Drag drag = std::move(*m_drag);
    m_drag.reset();
    if (const doc::Cel *c = cel())
        m_document->cancelCelPose(c->id(), drag.before);
}

bool ModelGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const int joint = editable() ? jointAt(docPos, view) : -1;
    if (joint == m_hovered)
        return false;
    m_hovered = joint;
    return true;
}

void ModelGizmo::paint(QPainter &painter, const ViewTransform &view) const
{
    const doc::Cel *c = cel();
    if (!c || !c->properties().visible)
        return;
    const rig3d::Model &model = *c->model().model();
    const rig3d::Pose pose = currentPose();
    const auto points = jointPoints(pose);
    painter.save();
    painter.setWorldTransform(QTransform());
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = m_rig ? QColor(64, 196, 120) : QColor(255, 150, 60);
    const QColor ink = editable() ? accent : accent.darker(160);
    const int chosen = chosenJoint();
    // The bones: from each joint to its parent's.
    for (std::size_t n = 0; n < points.size(); ++n) {
        const int parent = model.nodes[n].parent;
        if (!points[n] || parent < 0 || !points[std::size_t(parent)])
            continue;
        const QPointF a = view.documentToView().map(*points[std::size_t(parent)]), b = view.documentToView().map(*points[n]);
        painter.setPen(QPen(Qt::white, 3.0));
        painter.drawLine(a, b);
        painter.setPen(QPen(ink, 1.5));
        painter.drawLine(a, b);
    }
    // The chosen joint's limit frame, in rig mode.
    if (m_rig && chosen >= 0 && points[std::size_t(chosen)]) {
        const std::vector<rig3d::Mat4> g = rig3d::globalMatrices(model, pose);
        const rig3d::Projection p = projection(pose);
        const rig3d::Quat frame = rig3d::limitFrame(model, chosen);
        rig3d::Vec3 center;
        double radius = 1;
        rig3d::restBounds(model, center, radius);
        const rig3d::Vec3 origin = g[std::size_t(chosen)].translationPart();
        const QTransform toDoc = toDocument();
        const QColor colors[3] = {QColor(230, 60, 60), QColor(60, 190, 80), QColor(70, 120, 240)};
        for (int axis = 0; axis < 3; ++axis) {
            const rig3d::Vec3 local = frame.rotate({axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0});
            const rig3d::Vec3 tip = origin + rig3d::normalized(g[std::size_t(chosen)].vector(local)) * (radius * 0.12);
            painter.setPen(QPen(colors[axis], 2.0));
            const QTransform toView = toDoc * view.documentToView();
            painter.drawLine(toView.map(toQ(p.toPage(origin))), toView.map(toQ(p.toPage(tip))));
        }
    }
    for (std::size_t n = 0; n < points.size(); ++n) {
        if (!points[n])
            continue;
        const bool hot = int(n) == m_hovered || int(n) == chosen;
        const bool anchored = n < c->model().joints.size() && c->model().joints[n].anchor;
        painter.setPen(QPen(ink, 1.6));
        painter.setBrush(hot ? ink : QColor(Qt::white));
        const QPointF at = view.documentToView().map(*points[n]);
        if (anchored)
            painter.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
        else
            painter.drawEllipse(at, hot ? 5.0 : 4.0, hot ? 5.0 : 4.0);
    }
    if (chosen >= 0 && points[std::size_t(chosen)]) {
        const QPointF at = view.documentToView().map(*points[std::size_t(chosen)]) + QPointF(8, -8);
        painter.setPen(QPen(Qt::black));
        painter.drawText(at + QPointF(1, 1), QString::fromStdString(model.nodes[std::size_t(chosen)].name));
        painter.setPen(QPen(Qt::white));
        painter.drawText(at, QString::fromStdString(model.nodes[std::size_t(chosen)].name));
    }
    painter.restore();
}

} // namespace mothion::canvas
