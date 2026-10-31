#include "canvas/StrokeRenderer.h"

#include <QLineF>

#include <algorithm>
#include <vector>

namespace mothion::canvas {

namespace {

// Dab distance as a fraction of the radius: smaller = smoother edge, more work.
double spacingFor(double radius)
{
    return std::max(0.5, radius * 0.15);
}

} // namespace

QRect StrokeRenderer::begin(const Point &p, RasterSurface &surface)
{
    m_last = p;
    m_distanceSinceDab = 0.0;
    const Dab dab{p.pos, p.radius};
    return surface.addDabs({&dab, 1});
}

QRect StrokeRenderer::continueTo(const Point &p, RasterSurface &surface)
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
    return surface.addDabs(dabs);
}

} // namespace mothion::canvas
