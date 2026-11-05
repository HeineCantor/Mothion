#include "canvas/PerspectiveGizmo.h"

#include "canvas/ViewTransform.h"
#include "core/document/commands/LayerCommands.h"

#include <QActionGroup>
#include <QColorDialog>
#include <QLineF>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace mothion::canvas {

using document::DocumentController;
using perspective::Vec3;

namespace {

constexpr int kRingSamples = 96;
constexpr double kMoveHalf = 6.0;    // the middle square, px
constexpr double kScaleHalf = 5.0;   // the corner square
constexpr double kMenuRadius = 9.0;  // the menu button
constexpr double kMinAxisOnScreen = 14.0; // shorter: seen end on, not grabbed
constexpr double kNearDepth = 0.1;   // the plane's center never nearer the eye than this much of the focal length
constexpr double kClickSlop = 4.0;

const QColor kAxisColors[3] = {QColor(230, 70, 70), QColor(80, 190, 90), QColor(70, 130, 235)};

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return QLineF(p, a + ab * t).length();
}

QPointF toQt(const perspective::Point &p) { return {p.x, p.y}; }

int axisOf(PerspectiveGizmo::Handle h)
{
    switch (h) {
    case PerspectiveGizmo::Handle::AxisX:
    case PerspectiveGizmo::Handle::RingX: return 0;
    case PerspectiveGizmo::Handle::AxisY:
    case PerspectiveGizmo::Handle::RingY: return 1;
    case PerspectiveGizmo::Handle::AxisZ:
    case PerspectiveGizmo::Handle::RingZ: return 2;
    default: return -1;
    }
}

bool isRing(PerspectiveGizmo::Handle h)
{
    return h == PerspectiveGizmo::Handle::RingX || h == PerspectiveGizmo::Handle::RingY
           || h == PerspectiveGizmo::Handle::RingZ;
}

bool isAxis(PerspectiveGizmo::Handle h)
{
    return h == PerspectiveGizmo::Handle::AxisX || h == PerspectiveGizmo::Handle::AxisY
           || h == PerspectiveGizmo::Handle::AxisZ;
}

} // namespace

PerspectiveGizmo::PerspectiveGizmo(DocumentController *document)
    : m_document(document)
{
}

const std::array<PerspectiveGizmo::Axis, 3> &PerspectiveGizmo::axes()
{
    // x along the grid, y towards its far side, z out of it on the viewer's
    // side: right-handed, as the camera's space is.
    static const Vec3 x{1, 0, 0}, y{0, -1, 0}, z{0, 0, -1};
    static const std::array<Axis, 3> all{{{x, y, z}, {y, z, x}, {z, x, y}}};
    return all;
}

const doc::Layer *PerspectiveGizmo::targetLayer() const
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    const doc::Drawing *drawing = m_document->currentDrawing();
    const doc::Layer *layer = drawing ? drawing->layer(m_document->currentLayer()) : nullptr;
    if (!cel || !layer || layer->kind != doc::LayerKind::Perspective || !cel->properties().visible
        || !layer->properties.visible)
        return nullptr;
    return layer;
}

std::optional<DocumentController::LayerRef> PerspectiveGizmo::target() const
{
    const doc::Layer *layer = targetLayer();
    if (!layer)
        return std::nullopt;
    return DocumentController::LayerRef{m_document->currentCel(), m_document->currentDrawingId(), layer->id};
}

bool PerspectiveGizmo::locked() const
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    const doc::Layer *layer = targetLayer();
    return !cel || !layer || cel->properties().locked || layer->properties.locked;
}

std::optional<QPointF> PerspectiveGizmo::ringPoint(const Geometry &g, int axis, double angle)
{
    const Axis &a = axes()[std::size_t(axis)];
    const Vec3 local = a.u * std::cos(angle) + a.v * std::sin(angle);
    const double radius = kRingRadius / g.zoom * g.units;
    const auto p = perspective::projectCamera(g.grid, g.center3 + perspective::axis(g.grid, local) * radius);
    return p ? std::optional(toQt(*p)) : std::nullopt;
}

std::optional<PerspectiveGizmo::Geometry> PerspectiveGizmo::geometry(const perspective::Grid &grid,
                                                                     const ViewTransform &view) const
{
    Geometry g;
    g.grid = grid;
    g.zoom = view.zoom();
    g.center3 = grid.position;
    const auto center = perspective::projectCamera(grid, g.center3);
    if (!center)
        return std::nullopt;
    g.center = toQt(*center);
    const double focal = perspective::focalLength(grid);
    g.units = (focal + g.center3.z) / focal;

    for (int i = 0; i < 3; ++i) {
        g.directions[std::size_t(i)] = perspective::axis(grid, axes()[std::size_t(i)].local);
        const double length = kAxisLength / g.zoom * g.units;
        if (const auto tip = perspective::projectCamera(grid, g.center3 + g.directions[std::size_t(i)] * length))
            if (QLineF(g.center, toQt(*tip)).length() * g.zoom >= kMinAxisOnScreen)
                g.tips[std::size_t(i)] = toQt(*tip);
        for (int k = 0; k <= kRingSamples; ++k)
            if (const auto p = ringPoint(g, i, 2.0 * std::numbers::pi * k / kRingSamples))
                g.rings[std::size_t(i)].push_back(*p);
    }
    // The scale handle: on the grid's diagonal, the first crossing out of
    // the rings (the outer corner if none is), so it is near at hand.
    for (int k = 1; k <= std::max(1, grid.cells); ++k) {
        const auto corner = perspective::project(grid, k * grid.cell, k * grid.cell);
        if (!corner)
            break;
        g.corner = toQt(*corner);
        if (QLineF(g.center, *g.corner).length() * g.zoom >= kRingRadius + 25.0)
            break;
    }
    // The menu button: up and right of the rings, on screen.
    const QPointF onScreen = view.documentToView().map(g.center);
    const double away = (kRingRadius + 24.0) / std::numbers::sqrt2;
    g.menu = view.mapToDocument(onScreen + QPointF(away, -away));
    return g;
}

PerspectiveGizmo::Handle PerspectiveGizmo::handleAt(const QPointF &docPos, const ViewTransform &view) const
{
    const doc::Layer *layer = targetLayer();
    if (!layer || locked())
        return Handle::None;
    const auto g = geometry(layer->properties.perspective, view);
    if (!g)
        return Handle::None;
    const double pick = kPick / g->zoom;
    if (QLineF(docPos, g->menu).length() <= kMenuRadius / g->zoom + 2.0 / g->zoom)
        return Handle::Menu;
    const QPointF d = docPos - g->center;
    if (std::abs(d.x()) * g->zoom <= kMoveHalf + 3.0 && std::abs(d.y()) * g->zoom <= kMoveHalf + 3.0)
        return Handle::Move;
    // The arrows: their outer part (the inner one is crowded by the rings).
    const Handle arrows[3] = {Handle::AxisX, Handle::AxisY, Handle::AxisZ};
    for (int i = 0; i < 3; ++i)
        if (const auto &tip = g->tips[std::size_t(i)])
            if (distanceToSegment(docPos, g->center + (*tip - g->center) * 0.7, *tip) <= pick)
                return arrows[i];
    if (g->corner && QLineF(docPos, *g->corner).length() <= (kScaleHalf + 3.0) / g->zoom)
        return Handle::Scale;
    const Handle rings[3] = {Handle::RingX, Handle::RingY, Handle::RingZ};
    Handle nearest = Handle::None;
    double best = pick;
    for (int i = 0; i < 3; ++i) {
        const std::vector<QPointF> &ring = g->rings[std::size_t(i)];
        for (std::size_t k = 1; k < ring.size(); ++k)
            if (const double dist = distanceToSegment(docPos, ring[k - 1], ring[k]); dist <= best) {
                best = dist;
                nearest = rings[i];
            }
    }
    return nearest;
}

std::optional<QPointF> PerspectiveGizmo::handlePoint(Handle handle, const ViewTransform &view) const
{
    const doc::Layer *layer = targetLayer();
    const auto g = layer ? geometry(layer->properties.perspective, view) : std::nullopt;
    if (!g)
        return std::nullopt;
    switch (handle) {
    case Handle::None: return std::nullopt;
    case Handle::Move: return g->center;
    case Handle::Menu: return g->menu;
    case Handle::Scale: return g->corner;
    default: break;
    }
    const int i = axisOf(handle);
    if (isAxis(handle))
        return g->tips[std::size_t(i)] ? std::optional(g->center + (*g->tips[std::size_t(i)] - g->center) * 0.9)
                                       : std::nullopt;
    // A point of the ring no other handle covers, from an eighth of a turn round.
    for (int k = 0; k < kRingSamples; ++k) {
        const double angle = std::numbers::pi / 4.0 + 2.0 * std::numbers::pi * k / kRingSamples;
        if (const auto p = ringPoint(*g, i, angle); p && handleAt(*p, view) == handle)
            return p;
    }
    return std::nullopt;
}

bool PerspectiveGizmo::press(const QPointF &docPos, const ViewTransform &view)
{
    const Handle handle = handleAt(docPos, view);
    const auto ref = target();
    if (handle == Handle::None || !ref)
        return false;
    Drag drag;
    drag.handle = handle;
    drag.ref = *ref;
    drag.before = targetLayer()->properties;
    drag.grid = drag.before.perspective;
    drag.press = drag.last = docPos;
    drag.zoom = view.zoom();
    if (isRing(handle)) {
        // Which way along the ring the pointer goes, where it was grabbed.
        const auto g = geometry(drag.grid, view);
        const int i = axisOf(handle);
        double angle = 0.0, best = std::numeric_limits<double>::max();
        for (int k = 0; k < kRingSamples; ++k) {
            const double a = 2.0 * std::numbers::pi * k / kRingSamples;
            if (const auto p = ringPoint(*g, i, a); p && QLineF(*p, docPos).length() < best) {
                best = QLineF(*p, docPos).length();
                angle = a;
            }
        }
        constexpr double step = 0.01;
        const auto a = ringPoint(*g, i, angle - step), b = ringPoint(*g, i, angle + step);
        drag.tangent = a && b ? (*b - *a) / (2.0 * step) : QPointF();
        // Never so short that a small move would spin the plane round (a ring seen edge on).
        const double floor = 0.25 * kRingRadius / g->zoom;
        if (const double length = std::hypot(drag.tangent.x(), drag.tangent.y()); length < floor)
            drag.tangent = length > 0 ? drag.tangent * (floor / length) : QPointF(floor, 0);
    }
    m_drag = drag;
    m_hovered = handle;
    return true;
}

void PerspectiveGizmo::preview(const perspective::Grid &grid)
{
    if (grid == m_drag->grid)
        return;
    m_drag->grid = grid;
    m_drag->moved = true;
    doc::LayerProperties shown = m_drag->before;
    shown.perspective = grid;
    m_document->previewLayerProperties(m_drag->ref, shown);
}

void PerspectiveGizmo::move(const QPointF &docPos, const ViewTransform &view)
{
    if (!m_drag)
        return;
    perspective::Grid grid = m_drag->grid;
    const double focal = perspective::focalLength(grid);
    const QPointF step = docPos - m_drag->last;
    m_drag->last = docPos;
    const int i = axisOf(m_drag->handle);
    switch (m_drag->handle) {
    case Handle::Move: {
        // Parallel to the screen, at its depth: as far as the pointer went on the sheet.
        const double units = (focal + grid.position.z) / focal;
        grid.position.x += step.x() * units;
        grid.position.y += step.y() * units;
        break;
    }
    case Handle::AxisX:
    case Handle::AxisY:
    case Handle::AxisZ: {
        // Along the arrow as drawn now, by how far the pointer went along it.
        const auto g = geometry(grid, view);
        if (!g || !g->tips[std::size_t(i)])
            return;
        const QPointF arrow = *g->tips[std::size_t(i)] - g->center;
        const double along = QPointF::dotProduct(step, arrow) / QPointF::dotProduct(arrow, arrow);
        const double length = kAxisLength / g->zoom * g->units;
        const Vec3 moved = grid.position + g->directions[std::size_t(i)] * (along * length);
        if (focal + moved.z < kNearDepth * focal)
            return; // not behind the eye
        grid.position = moved;
        break;
    }
    case Handle::RingX:
    case Handle::RingY:
    case Handle::RingZ: {
        // How far round the ring the pointer went, from the press, as an angle.
        const QPointF t = m_drag->tangent;
        const double radians = QPointF::dotProduct(docPos - m_drag->press, t) / QPointF::dotProduct(t, t);
        grid.orientation = perspective::normalized(
            m_drag->before.perspective.orientation
            * perspective::axisAngle(axes()[std::size_t(i)].local, radians * 180.0 / std::numbers::pi));
        break;
    }
    case Handle::Scale: {
        // The corner follows the pointer away from or towards the center.
        const auto g = geometry(m_drag->before.perspective, view);
        if (!g || !g->corner)
            return;
        const QPointF from = *g->corner - g->center;
        const double ratio = QPointF::dotProduct(docPos - g->center, from) / QPointF::dotProduct(from, from);
        grid.cell = std::max(1.0, m_drag->before.perspective.cell * std::max(0.05, ratio));
        break;
    }
    case Handle::Menu:
    case Handle::None: return;
    }
    preview(grid);
}

bool PerspectiveGizmo::release(const QPointF &docPos)
{
    if (!m_drag)
        return false;
    const Drag drag = *m_drag;
    m_drag.reset();
    if (drag.handle == Handle::Menu) // a click, not a slip off the button
        return QLineF(docPos, drag.press).length() * drag.zoom <= kClickSlop;
    if (drag.moved) {
        doc::LayerProperties after = drag.before;
        after.perspective = drag.grid;
        m_document->commitLayerProperties(drag.ref, drag.before, after);
    }
    return false;
}

void PerspectiveGizmo::cancel()
{
    if (!m_drag)
        return;
    const Drag drag = *m_drag;
    m_drag.reset();
    if (drag.moved)
        m_document->previewLayerProperties(drag.ref, drag.before);
}

bool PerspectiveGizmo::hover(const QPointF &docPos, const ViewTransform &view)
{
    if (m_drag)
        return false;
    const Handle handle = handleAt(docPos, view);
    if (handle == m_hovered)
        return false;
    m_hovered = handle;
    return true;
}

void PerspectiveGizmo::paint(QPainter &p, const ViewTransform &view) const
{
    const doc::Layer *layer = targetLayer();
    if (!layer)
        return;
    const auto g = geometry(m_drag ? m_drag->grid : layer->properties.perspective, view);
    if (!g)
        return;
    const QTransform toView = view.documentToView();
    const Handle active = m_drag ? m_drag->handle : m_hovered;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (locked())
        p.setOpacity(0.35);
    auto color = [&](int i, Handle h) {
        return active == h ? kAxisColors[i].lighter(130) : kAxisColors[i];
    };

    const Handle rings[3] = {Handle::RingX, Handle::RingY, Handle::RingZ};
    for (int i = 0; i < 3; ++i) {
        QPolygonF ring;
        for (const QPointF &pt : g->rings[std::size_t(i)])
            ring << toView.map(pt);
        p.setPen(QPen(QColor(0, 0, 0, 90), active == rings[i] ? 5.0 : 3.5));
        p.drawPolyline(ring);
        p.setPen(QPen(color(i, rings[i]), active == rings[i] ? 3.0 : 1.8));
        p.drawPolyline(ring);
    }

    const Handle arrows[3] = {Handle::AxisX, Handle::AxisY, Handle::AxisZ};
    const QPointF center = toView.map(g->center);
    for (int i = 0; i < 3; ++i) {
        if (!g->tips[std::size_t(i)])
            continue;
        const QPointF tip = toView.map(*g->tips[std::size_t(i)]);
        const QLineF shaft(center, tip);
        const QColor c = color(i, arrows[i]);
        const double width = active == arrows[i] ? 3.5 : 2.2;
        p.setPen(QPen(QColor(0, 0, 0, 90), width + 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(shaft);
        p.setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(shaft);
        // The head, pointing the way the arrow goes on screen.
        const QPointF dir = (tip - center) / shaft.length(), side(-dir.y(), dir.x());
        const double head = active == arrows[i] ? 13.0 : 11.0;
        const QPolygonF triangle{tip + dir * 4.0, tip - dir * head + side * (head * 0.45),
                                 tip - dir * head - side * (head * 0.45)};
        p.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
        p.setBrush(c);
        p.drawPolygon(triangle);
    }

    auto square = [&p](const QPointF &at, double half, bool hot) {
        p.setPen(QPen(QColor(30, 30, 30), 1.2));
        p.setBrush(hot ? QColor(255, 220, 90) : QColor(250, 250, 250));
        p.drawRect(QRectF(at.x() - half, at.y() - half, 2 * half, 2 * half));
    };
    if (g->corner)
        square(toView.map(*g->corner), kScaleHalf, active == Handle::Scale);
    square(center, kMoveHalf, active == Handle::Move);

    // The menu button: three dots in a circle.
    const QPointF menu = toView.map(g->menu);
    p.setPen(QPen(QColor(30, 30, 30), 1.2));
    p.setBrush(active == Handle::Menu ? QColor(255, 220, 90) : QColor(250, 250, 250));
    p.drawEllipse(menu, kMenuRadius, kMenuRadius);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 40));
    for (int k = -1; k <= 1; ++k)
        p.drawEllipse(menu + QPointF(k * 4.0, 0), 1.4, 1.4);
    p.restore();
}

QMenu *PerspectiveGizmo::createMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    const doc::Layer *layer = targetLayer();
    const auto ref = target();
    if (!layer || !ref)
        return menu;
    const perspective::Grid grid = layer->properties.perspective;
    // Every choice is an undo step of its own.
    auto apply = [this, ref = *ref](auto change) {
        const doc::Layer *l = m_document->document().layer(ref.cel, ref.drawing, ref.layer);
        if (!l)
            return;
        doc::LayerProperties p = l->properties;
        change(p.perspective);
        m_document->execute(doc::makeSetLayerProperties(m_document->document(), ref.cel, ref.drawing, ref.layer, p));
    };

    menu->addAction(QObject::tr("Grid color…"), menu, [apply, grid, parent] {
        const QColor chosen = QColorDialog::getColor(QColor::fromRgba(grid.color), parent,
                                                     QObject::tr("Perspective grid color"),
                                                     QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            apply([&chosen](perspective::Grid &g) { g.color = chosen.rgba(); });
    });
    QAction *horizon = menu->addAction(QObject::tr("Show horizon"));
    horizon->setCheckable(true);
    horizon->setChecked(grid.horizon);
    QObject::connect(horizon, &QAction::toggled, menu,
                     [apply](bool on) { apply([on](perspective::Grid &g) { g.horizon = on; }); });

    QMenu *cells = menu->addMenu(QObject::tr("Cells"));
    auto *cellGroup = new QActionGroup(cells);
    for (int n : {2, 4, 6, 8, 10, 12, 16, 20, 30}) {
        QAction *a = cells->addAction(QObject::tr("%1 × %1").arg(2 * n));
        a->setCheckable(true);
        a->setChecked(grid.cells == n);
        a->setData(n);
        cellGroup->addAction(a);
        QObject::connect(a, &QAction::triggered, menu,
                         [apply, n] { apply([n](perspective::Grid &g) { g.cells = n; }); });
    }
    QMenu *lens = menu->addMenu(QObject::tr("Lens"));
    auto *lensGroup = new QActionGroup(lens);
    for (int fov : {20, 30, 40, 50, 60, 75, 90, 120}) {
        QAction *a = lens->addAction(QObject::tr("%1°").arg(fov));
        a->setCheckable(true);
        a->setChecked(std::abs(grid.fov - fov) < 0.5);
        a->setData(fov);
        lensGroup->addAction(a);
        QObject::connect(a, &QAction::triggered, menu,
                         [apply, fov] { apply([fov](perspective::Grid &g) { g.fov = fov; }); });
    }
    QMenu *fisheye = menu->addMenu(QObject::tr("Fisheye"));
    auto *fisheyeGroup = new QActionGroup(fisheye);
    for (int percent : {0, 25, 50, 75, 100, 150, 200, 300}) {
        QAction *a = fisheye->addAction(percent == 0 ? QObject::tr("Off") : QObject::tr("%1%").arg(percent));
        a->setCheckable(true);
        a->setChecked(std::abs(grid.fisheye * 100.0 - percent) < 0.5);
        fisheyeGroup->addAction(a);
        QObject::connect(a, &QAction::triggered, menu,
                         [apply, percent] { apply([percent](perspective::Grid &g) { g.fisheye = percent / 100.0; }); });
    }
    menu->addSeparator();
    menu->addAction(QObject::tr("Reset rotation"), menu,
                    [apply] { apply([](perspective::Grid &g) { g.orientation = perspective::Grid().orientation; }); });
    menu->addAction(QObject::tr("Reset position"), menu,
                    [apply] { apply([](perspective::Grid &g) { g.position = {}; }); });
    return menu;
}

} // namespace mothion::canvas
