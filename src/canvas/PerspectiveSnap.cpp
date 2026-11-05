#include "canvas/PerspectiveSnap.h"

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

namespace {

using perspective::Vec3;

Vec3 unit(const Vec3 &v)
{
    const double length = std::sqrt(perspective::dot(v, v));
    return length > 0.0 ? v * (1.0 / length) : v;
}

// Of the axis's curve through the ray `from`: where it goes at the start, in the picture.
std::optional<QPointF> curveTangent(const perspective::Grid &grid, const Vec3 &ray, const Vec3 &axis)
{
    const Vec3 across = axis - ray * perspective::dot(axis, ray);
    const double length = std::sqrt(perspective::dot(across, across));
    if (length < 1e-6)
        return std::nullopt;
    const auto a = perspective::projectDirection(grid, ray);
    const auto b = perspective::projectDirection(grid, unit(ray + across * (1e-3 / length)));
    if (!a || !b)
        return std::nullopt;
    const QPointF d(b->x - a->x, b->y - a->y);
    const double n = std::hypot(d.x(), d.y());
    if (n < 1e-12)
        return std::nullopt;
    return d / n;
}

} // namespace

std::array<std::optional<QPointF>, 3> PerspectiveSnap::directions(const perspective::Grid &grid, const QPointF &from)
{
    using perspective::Vec3;
    const double f = perspective::focalLength(grid);
    const Vec3 planeAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    std::array<std::optional<QPointF>, 3> result;
    if (grid.fisheye > 0.0) {
        const Vec3 ray = perspective::unprojectDirection(grid, {from.x(), from.y()});
        for (int i = 0; i < 3; ++i)
            result[std::size_t(i)] = curveTangent(grid, ray, unit(perspective::axis(grid, planeAxes[i])));
        return result;
    }
    for (int i = 0; i < 3; ++i) {
        const Vec3 d = perspective::axis(grid, planeAxes[i]);
        QPointF direction;
        if (std::abs(d.z) < 1e-6 * std::max(1.0, std::hypot(d.x, d.y))) {
            direction = QPointF(d.x, d.y); // in the picture's plane: parallel lines
        } else {
            // The point the axis's lines meet at: where the direction, from the eye, hits the picture.
            const QPointF vanishing(grid.originX + f * d.x / d.z, grid.originY + f * d.y / d.z);
            direction = vanishing - from;
        }
        const double length = std::hypot(direction.x(), direction.y());
        if (length > 1e-9)
            result[std::size_t(i)] = direction / length;
    }
    return result;
}

void PerspectiveSnap::begin(const perspective::Grid &grid, const QPointF &start, double threshold)
{
    m_grid = grid;
    m_ray = perspective::unprojectDirection(grid, {start.x(), start.y()});
    m_start = start;
    m_threshold = threshold;
    m_axis = -1;
    m_directions = directions(grid, start);
}

void PerspectiveSnap::reset()
{
    m_axis = -1;
    m_directions = {};
}

bool PerspectiveSnap::update(const QPointF &pen)
{
    if (decided())
        return true;
    const QPointF w = pen - m_start;
    const double length = std::hypot(w.x(), w.y());
    if (length < m_threshold || length <= 0.0)
        return false;
    // The axis whose line the pen's direction is closest to (the sine of the angle between them).
    double best = 2.0;
    for (int i = 0; i < 3; ++i) {
        const auto &u = m_directions[std::size_t(i)];
        if (!u)
            continue;
        const double sine = std::abs(u->x() * w.y() - u->y() * w.x()) / length;
        if (sine < best) {
            best = sine;
            m_axis = i;
        }
    }
    return decided();
}

int PerspectiveSnap::cycle()
{
    for (int step = 1; step <= 3; ++step) {
        const int axis = ((m_axis < 0 ? -1 : m_axis) + step + 3) % 3;
        if (m_directions[std::size_t(axis)]) {
            m_axis = axis;
            return axis;
        }
    }
    return -1;
}

QColor PerspectiveSnap::axisColor(int axis)
{
    static const QColor colors[3] = {QColor(230, 70, 70), QColor(80, 190, 90), QColor(70, 130, 235)};
    return colors[std::clamp(axis, 0, 2)];
}

QChar PerspectiveSnap::axisLetter(int axis)
{
    return QChar(u"XYZ"[std::clamp(axis, 0, 2)]);
}

std::optional<Vec3> PerspectiveSnap::curveDirection(const QPointF &pen) const
{
    static const Vec3 planeAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const Vec3 axis = unit(perspective::axis(m_grid, planeAxes[m_axis]));
    const Vec3 normal = perspective::cross(m_ray, axis);
    const double length = std::sqrt(perspective::dot(normal, normal));
    if (length < 1e-9)
        return std::nullopt;
    const Vec3 n = normal * (1.0 / length);
    const Vec3 w = perspective::unprojectDirection(m_grid, {pen.x(), pen.y()});
    const Vec3 inPlane = w - n * perspective::dot(w, n);
    if (perspective::dot(inPlane, inPlane) < 1e-18)
        return std::nullopt;
    return unit(inPlane);
}

QPolygonF PerspectiveSnap::guide(const QPointF &pen) const
{
    if (!decided())
        return {};
    if (m_grid.fisheye <= 0.0)
        return {m_start, snapped(pen)};
    const auto end = curveDirection(pen);
    if (!end)
        return {m_start, pen};
    // From the start's ray to the pen's, turning in the plane of the curve.
    constexpr int steps = 32;
    QPolygonF line;
    const double cosine = std::clamp(perspective::dot(m_ray, *end), -1.0, 1.0);
    const double angle = std::acos(cosine);
    const Vec3 across = unit(*end - m_ray * cosine);
    for (int i = 0; i <= steps; ++i) {
        const double a = angle * i / steps;
        if (const auto p = perspective::projectDirection(m_grid, m_ray * std::cos(a) + across * std::sin(a)))
            line << QPointF(p->x, p->y);
    }
    return line;
}

QPointF PerspectiveSnap::snapped(const QPointF &pen) const
{
    if (!decided())
        return pen;
    if (m_grid.fisheye > 0.0) {
        if (const auto d = curveDirection(pen))
            if (const auto p = perspective::projectDirection(m_grid, *d))
                return QPointF(p->x, p->y);
        return pen;
    }
    const QPointF u = *m_directions[std::size_t(m_axis)];
    const QPointF w = pen - m_start;
    return m_start + u * (w.x() * u.x() + w.y() * u.y());
}

} // namespace mothion::canvas
