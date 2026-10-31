#include "canvas/DabSpacer.h"

#include <QLineF>

#include <algorithm>

namespace mothion::canvas {

namespace {

// Dab distance as a fraction of the radius: smaller = smoother edge, more work.
double spacingFor(double radius)
{
    return std::max(0.5, radius * 0.15);
}

} // namespace

std::vector<Dab> DabSpacer::begin(const Point &p)
{
    m_last = p;
    m_distanceSinceDab = 0.0;
    return {Dab{p.pos, p.radius}};
}

std::vector<Dab> DabSpacer::continueTo(const Point &p)
{
    const double length = QLineF(m_last.pos, p.pos).length();
    if (length <= 0.0) {
        m_last.radius = p.radius;
        return {};
    }

    std::vector<Dab> dabs;
    double travelled = 0.0;
    for (;;) {
        // Spacing follows the radius at the current position along the segment.
        const double f = travelled / length;
        const double radius = m_last.radius + (p.radius - m_last.radius) * f;
        const double step = std::max(0.0, spacingFor(radius) - m_distanceSinceDab);
        if (travelled + step > length)
            break;
        travelled += step;
        m_distanceSinceDab = 0.0;
        const double g = travelled / length;
        dabs.push_back({m_last.pos + (p.pos - m_last.pos) * g,
                        m_last.radius + (p.radius - m_last.radius) * g});
    }
    m_distanceSinceDab += length - travelled;
    m_last = p;
    return dabs;
}

} // namespace mothion::canvas
