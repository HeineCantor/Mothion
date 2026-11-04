#include "canvas/PerspectiveSnap.h"

#include <cmath>

namespace mothion::canvas {

std::array<std::optional<QPointF>, 3> PerspectiveSnap::directions(const perspective::Grid &grid, const QPointF &from)
{
    using perspective::Vec3;
    const double f = perspective::focalLength(grid);
    const Vec3 planeAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    std::array<std::optional<QPointF>, 3> result;
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

QPointF PerspectiveSnap::snapped(const QPointF &pen) const
{
    if (!decided())
        return pen;
    const QPointF u = *m_directions[std::size_t(m_axis)];
    const QPointF w = pen - m_start;
    return m_start + u * (w.x() * u.x() + w.y() * u.y());
}

} // namespace mothion::canvas
