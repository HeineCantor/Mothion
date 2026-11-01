#include "core/brush/VelocityMeter.h"

#include <algorithm>
#include <cmath>

namespace mothion::brush {

namespace {

constexpr double kReferencePxPerMs = 8.0;
constexpr double kReferencePxPerSample = 20.0;

} // namespace

double VelocityMeter::sample(double t, double x, double y)
{
    const double distance = m_samples.empty() ? 0.0 : std::hypot(x - m_samples.back().x, y - m_samples.back().y);
    m_samples.push_back({t, x, y, distance});
    while (int(m_samples.size()) > m_history + 1)
        m_samples.pop_front();
    if (m_samples.size() < 2)
        return 0.0;

    double travelled = 0.0;
    for (std::size_t i = 1; i < m_samples.size(); ++i)
        travelled += m_samples[i].distance;
    const double elapsedMs = (m_samples.back().t - m_samples.front().t) * 1000.0;
    double v = 0.0;
    if (elapsedMs > 0.5)
        v = travelled / elapsedMs / kReferencePxPerMs;
    else
        v = distance / kReferencePxPerSample;
    return std::clamp(v, 0.0, 1.0);
}

} // namespace mothion::brush
