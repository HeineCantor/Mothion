#include "canvas/CurveGizmo.h"

#include "canvas/ViewTransform.h"

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

constexpr int kShownSamples = 24; // points drawn from one node to the next

QPointF toQ(deform::Vec2 v)
{
    return {v.x, v.y};
}

// Where the zone's handles of a Curve's node are: across the curve there, at its radius and at its radius and fade.
deform::Vec2 zoneHandle(const deform::CurveNode &n, const doc::Bone &part, bool fade)
{
    const deform::Vec2 dir = n.direction(), across{-dir.y, dir.x};
    return n.at + across * (part.radius + (fade ? part.fade : 0.0));
}

deform::Vec2 turned(deform::Vec2 p, deform::Vec2 around, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    const deform::Vec2 r = p - around;
    return around + deform::Vec2{c * r.x - s * r.y, s * r.x + c * r.y};
}

} // namespace

CurveGizmo::CurveGizmo(document::DocumentController *document)
    : m_document(document)
{
}

const doc::Cel *CurveGizmo::cel() const
{
    if (m_document->cameraSelected())
        return nullptr;
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!cel)
        return nullptr;
    const doc::DeformerKind kind = cel->properties().deformer;
    return kind == doc::DeformerKind::Curve || kind == doc::DeformerKind::Envelope ? cel : nullptr;
}

bool CurveGizmo::closed() const
{
    const doc::Cel *c = cel();
    return c && c->properties().deformer == doc::DeformerKind::Envelope;
}

bool CurveGizmo::shown() const
{
    return cel() != nullptr;
}

bool CurveGizmo::editable() const
{
    const doc::Cel *c = cel();
    return c && !c->properties().locked;
}

int CurveGizmo::chosenNode() const
{
    const doc::Cel *c = cel();
    return c && c->id() == m_chosenCel && m_chosen >= 0 && m_chosen < int(c->deformer().bones.size()) ? m_chosen : -1;
}

QTransform CurveGizmo::toPage() const
{
    const doc::Cel *c = cel();
    if (!c)
        return {};
    const doc::Matrix2x3 m = m_document->document().followMatrix(*c, m_document->currentFrame());
    return QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
}

deform::Vec2 CurveGizmo::inCurve(const QPointF &docPos) const
{
    const QPointF p = toPage().inverted().map(docPos);
    return {p.x(), p.y()};
}

const doc::Deformer &CurveGizmo::deformerShown() const
{
    return m_drag && m_rig ? m_drag->rig : cel()->deformer();
}

std::vector<deform::CurveNode> CurveGizmo::nodes() const
{
    const doc::Cel *c = cel();
    if (!c)
        return {};
    if (m_drag && m_rig)
        return deform::curveNodes(m_drag->rig, m_drag->rig.rest);
    if (m_rig)
        return deform::curveNodes(c->deformer(), c->deformer().rest);
    return deform::curveNodes(c->deformer(), m_document->document().deformPoseAt(*c, m_document->currentFrame()));
}

CurveGizmo::Hit CurveGizmo::hitAt(const QPointF &docPos, const ViewTransform &view) const
{
    if (!cel())
        return {};
    const QTransform toView = toPage() * view.documentToView();
    const QPointF at = view.documentToView().map(docPos);
    const std::vector<deform::CurveNode> n = nodes();
    Hit best;
    double nearest = kGrab;
    auto consider = [&](int node, Part part, deform::Vec2 p) {
        const double d = QLineF(at, toView.map(toQ(p))).length();
        if (d <= nearest) {
            nearest = d;
            best = {node, part};
        }
    };
    const int chosen = chosenNode();
    if (m_rig && !closed() && chosen >= 0 && std::size_t(chosen) < n.size())
        for (Part part : {Part::ZoneFade, Part::ZoneRadius})
            consider(chosen, part, zoneHandle(n[std::size_t(chosen)], deformerShown().bones[std::size_t(chosen)], part == Part::ZoneFade));
    if (best.node >= 0)
        return best;
    for (std::size_t i = n.size(); i-- > 0;)
        consider(int(i), Part::Node, n[i].at);
    if (best.node >= 0)
        return best;
    for (std::size_t i = n.size(); i-- > 0;) {
        if (n[i].out > 0)
            consider(int(i), Part::Out, n[i].outHandle());
        if (n[i].in > 0)
            consider(int(i), Part::In, n[i].inHandle());
    }
    return best;
}

std::optional<QPointF> CurveGizmo::partPoint(int node, Part part) const
{
    const std::vector<deform::CurveNode> n = nodes();
    if (node < 0 || node >= int(n.size()))
        return std::nullopt;
    const deform::CurveNode &c = n[std::size_t(node)];
    const deform::Vec2 p = part == Part::In    ? c.inHandle()
                           : part == Part::Out ? c.outHandle()
                           : part == Part::ZoneRadius || part == Part::ZoneFade
                               ? zoneHandle(c, deformerShown().bones[std::size_t(node)], part == Part::ZoneFade)
                               : c.at;
    return toPage().map(toQ(p));
}

bool CurveGizmo::press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers)
{
    const doc::Cel *c = cel();
    if (!c || !editable() || m_drag)
        return false;
    const Hit hit = hitAt(docPos, view);
    m_chosenCel = c->id();
    if (hit.node < 0) {
        if (m_rig) {
            // On from the chosen node if it ends its curve (and Ctrl is not held); else a curve of its own.
            const std::vector<deform::CurveNode> n = nodes();
            const int chosen = chosenNode();
            const bool ends = chosen >= 0 && (std::size_t(chosen) + 1 == n.size() || n[std::size_t(chosen) + 1].starts);
            const int after = ends && !(modifiers & Qt::ControlModifier) ? chosen : -1;
            const int made = m_document->addNode(c->id(), toQ(inCurve(docPos)), after);
            if (made >= 0)
                m_chosen = made;
        } else {
            m_chosen = -1;
        }
        return true;
    }
    m_chosen = hit.node;
    Drag drag;
    drag.node = hit.node;
    drag.part = hit.part;
    drag.start = drag.now = inCurve(docPos);
    drag.local = closed() || (modifiers & Qt::AltModifier);
    drag.startNodes = nodes();
    if (m_rig) {
        drag.rig = c->deformer();
    } else {
        if (hit.part == Part::ZoneRadius || hit.part == Part::ZoneFade)
            return true; // the zone is the rig's
        drag.before = c->motion();
        const int frame = m_document->currentFrame();
        if (!c->motion().hasKeys(doc::Channel::Deform))
            m_document->beginFirstKey(c->id(), frame);
        drag.pose = m_document->document().deformPoseAt(*c, frame);
    }
    m_drag = std::move(drag);
    return true;
}

void CurveGizmo::apply(Drag &drag)
{
    const doc::Cel *c = cel();
    if (!c || drag.node < 0 || std::size_t(drag.node) >= drag.startNodes.size())
        return;
    std::vector<deform::CurveNode> n = drag.startNodes;
    const std::size_t i = std::size_t(drag.node);
    switch (drag.part) {
    case Part::None: return;
    case Part::ZoneRadius:
    case Part::ZoneFade: {
        const double away = puppet::length(drag.now - n[i].at);
        doc::Bone &part = drag.rig.bones[i];
        if (drag.part == Part::ZoneRadius)
            part.radius = std::max(1.0, away);
        else
            part.fade = std::max(0.0, away - part.radius);
        return;
    }
    case Part::Node: {
        const deform::Vec2 delta = drag.now - drag.start;
        for (std::size_t j = i; j < n.size() && (j == i || (!drag.local && !n[j].starts)); ++j)
            n[j].at = n[j].at + delta;
        break;
    }
    case Part::In:
    case Part::Out: {
        deform::Vec2 way = drag.now - n[i].at;
        const double length = puppet::length(way);
        if (length < 1e-9)
            return;
        if (drag.part == Part::In)
            way = way * -1.0; // the tangent runs from the handle back to the node
        const double turn = std::remainder(std::atan2(way.y, way.x) - n[i].angle, 2 * std::numbers::pi);
        n[i].angle += turn;
        (drag.part == Part::In ? n[i].in : n[i].out) = length;
        if (!drag.local)
            for (std::size_t j = i + 1; j < n.size() && !n[j].starts; ++j) {
                n[j].at = turned(n[j].at, n[i].at, turn);
                n[j].angle += turn;
            }
        break;
    }
    }
    if (m_rig) {
        drag.rig.rest = deform::poseFromNodes(n, c->deformer().rest);
        return;
    }
    doc::CelPose p;
    p.deform = deform::poseFromNodes(n, drag.pose);
    m_document->previewCelPose(c->id(), m_document->currentFrame(), p, {doc::Channel::Deform});
}

void CurveGizmo::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_drag)
        return;
    m_drag->now = inCurve(docPos);
    m_drag->local = closed() || (modifiers & Qt::AltModifier);
    m_drag->moved = m_drag->moved || puppet::length(m_drag->now - m_drag->start) > 1e-9;
    if (m_rig) {
        apply(*m_drag);
        return;
    }
    m_throttle.request([this] {
        if (m_drag)
            apply(*m_drag);
    });
}

void CurveGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return;
    m_throttle.cancel();
    m_drag->now = inCurve(docPos);
    m_drag->moved = m_drag->moved || puppet::length(m_drag->now - m_drag->start) > 1e-9;
    if (m_drag->moved)
        apply(*m_drag);
    Drag drag = std::move(*m_drag);
    m_drag.reset();
    const doc::Cel *c = cel();
    if (!c)
        return;
    if (!m_rig)
        m_document->finishCelPose(c->id(), drag.before);
    else if (drag.moved)
        m_document->setDeformerRest(c->id(), drag.rig);
}

void CurveGizmo::cancel()
{
    if (!m_drag)
        return;
    m_throttle.cancel();
    const Drag drag = std::move(*m_drag);
    m_drag.reset();
    if (!m_rig)
        if (const doc::Cel *c = cel())
            m_document->cancelCelPose(c->id(), drag.before);
}

bool CurveGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    const Hit hit = editable() ? hitAt(docPos, view) : Hit{};
    if (hit.node == m_hovered.node && hit.part == m_hovered.part)
        return false;
    m_hovered = hit;
    return true;
}

bool CurveGizmo::removeChosenNode()
{
    const int node = chosenNode();
    if (!m_rig || node < 0 || !editable())
        return false;
    m_chosen = -1;
    return m_document->removeDeformerPart(cel()->id(), node);
}

void CurveGizmo::paint(QPainter &painter, const ViewTransform &view) const
{
    const doc::Cel *c = cel();
    if (!c)
        return;
    const std::vector<deform::CurveNode> n = nodes();
    const QTransform toView = toPage() * view.documentToView();
    const double scale = std::sqrt(std::abs(toView.determinant()));
    painter.save();
    painter.setWorldTransform(QTransform());
    painter.setRenderHint(QPainter::Antialiasing);
    const bool live = editable() && c->properties().visible;
    const QColor accent = m_rig ? QColor(64, 196, 120) : QColor(80, 160, 255);
    const QColor ink = live ? accent : accent.darker(160);
    const int chosen = chosenNode();
    const bool loop = closed();

    // The curves (an Envelope's one, closed).
    QPainterPath path;
    std::vector<deform::Vec2> chosenLine; // the curve of the chosen node
    std::size_t chosenAt = 0;             // and the node's place along it
    const auto runs = loop ? std::vector<std::pair<std::size_t, std::size_t>>{{0, n.size()}} : deform::curveRuns(n);
    for (const auto &[begin, end] : runs) {
        const std::vector<deform::CurveNode> run(n.begin() + std::ptrdiff_t(begin), n.begin() + std::ptrdiff_t(end));
        const std::vector<deform::Vec2> line = deform::sampleCurve(run, loop, kShownSamples);
        for (std::size_t k = 0; k < line.size(); ++k)
            k == 0 ? path.moveTo(toView.map(toQ(line[k]))) : path.lineTo(toView.map(toQ(line[k])));
        if (loop && line.size() > 2)
            path.closeSubpath();
        if (chosen >= int(begin) && chosen < int(end)) {
            chosenLine = line;
            chosenAt = std::size_t(chosen) - begin;
        }
    }
    // The zone of a Curve round the chosen node's two pieces, in rig mode.
    if (m_rig && !loop && chosen >= 0 && chosenLine.size() >= 2) {
        const doc::Bone &part = deformerShown().bones[std::size_t(chosen)];
        const std::vector<deform::Vec2> &line = chosenLine;
        QPainterPath near;
        const std::size_t from = (chosenAt > 0 ? chosenAt - 1 : 0) * kShownSamples;
        const std::size_t to = std::min(line.size() - 1, (chosenAt + 1) * kShownSamples);
        for (std::size_t k = from; k <= to; ++k)
            k == from ? near.moveTo(toView.map(toQ(line[k]))) : near.lineTo(toView.map(toQ(line[k])));
        QPainterPathStroker inner, outer;
        inner.setCapStyle(Qt::RoundCap);
        outer.setCapStyle(Qt::RoundCap);
        inner.setWidth(std::max(1.0, 2 * part.radius * scale));
        outer.setWidth(std::max(1.0, 2 * (part.radius + part.fade) * scale));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 40));
        painter.drawPath(inner.createStroke(near));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 160), 1.0, Qt::DashLine));
        painter.drawPath(outer.createStroke(near));
        for (Part zonePart : {Part::ZoneRadius, Part::ZoneFade}) {
            const QPointF h = toView.map(toQ(zoneHandle(n[std::size_t(chosen)], part, zonePart == Part::ZoneFade)));
            const bool hot = m_hovered.node == chosen && m_hovered.part == zonePart;
            painter.setPen(QPen(ink, 1.4));
            painter.setBrush(hot ? ink : QColor(Qt::white));
            painter.drawRect(QRectF(h - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 4.0));
    painter.drawPath(path);
    painter.setPen(QPen(ink, 2.0));
    painter.drawPath(path);

    // The handles, then the nodes.
    for (std::size_t i = 0; i < n.size(); ++i) {
        const QPointF at = toView.map(toQ(n[i].at));
        for (Part part : {Part::In, Part::Out}) {
            const double length = part == Part::In ? n[i].in : n[i].out;
            if (length <= 0)
                continue;
            const QPointF h = toView.map(toQ(part == Part::In ? n[i].inHandle() : n[i].outHandle()));
            painter.setPen(QPen(ink, 1.0));
            painter.drawLine(at, h);
            const bool hot = m_hovered.node == int(i) && m_hovered.part == part;
            painter.setBrush(hot ? ink : QColor(Qt::white));
            painter.drawEllipse(h, 3.5, 3.5);
            painter.setBrush(Qt::NoBrush);
        }
    }
    for (std::size_t i = 0; i < n.size(); ++i) {
        const QPointF at = toView.map(toQ(n[i].at));
        const bool hot = (m_hovered.node == int(i) && m_hovered.part == Part::Node) || int(i) == chosen;
        painter.setPen(QPen(ink, 1.6));
        painter.setBrush(hot ? ink : QColor(Qt::white));
        const double r = int(i) == chosen ? 5.5 : 4.5;
        painter.drawRect(QRectF(at - QPointF(r, r), QSizeF(2 * r, 2 * r)));
    }
    painter.restore();
}

} // namespace mothion::canvas
