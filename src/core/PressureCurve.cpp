#include "core/PressureCurve.h"

#include <algorithm>
#include <vector>

namespace mothion {

namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// One coordinate of a cubic Bézier with P0 = 0 and P3 = 1.
float bezier(float t, float c1, float c2)
{
    const float u = 1.0f - t;
    return 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t;
}

} // namespace

PressureCurve::PressureCurve()
    : PressureCurve({1.0f / 3.0f, 1.0f / 3.0f}, {2.0f / 3.0f, 2.0f / 3.0f})
{
}

PressureCurve::PressureCurve(Point control1, Point control2)
    : m_c1{clamp01(control1.x), clamp01(control1.y)}
    , m_c2{clamp01(control2.x), clamp01(control2.y)}
{
    rebuildLut();
}

void PressureCurve::rebuildLut()
{
    // Sample the curve densely in t, then resample it at evenly spaced x
    // values. x(t) is non-decreasing because the control x's are in [0,1].
    constexpr int kSteps = 1024;
    std::vector<Point> samples(kSteps + 1);
    for (int i = 0; i <= kSteps; ++i) {
        const float t = float(i) / kSteps;
        samples[i] = {bezier(t, m_c1.x, m_c2.x), bezier(t, m_c1.y, m_c2.y)};
    }

    std::size_t s = 0;
    for (int i = 0; i < kLutSize; ++i) {
        const float x = float(i) / (kLutSize - 1);
        while (s + 1 < samples.size() && samples[s + 1].x < x)
            ++s;
        if (s + 1 >= samples.size()) {
            m_lut[i] = samples.back().y;
            continue;
        }
        const Point &a = samples[s];
        const Point &b = samples[s + 1];
        const float span = b.x - a.x;
        const float f = span > 1e-6f ? (x - a.x) / span : 0.0f;
        m_lut[i] = clamp01(a.y + (b.y - a.y) * clamp01(f));
    }
}

float PressureCurve::map(float pressure) const
{
    const float pos = clamp01(pressure) * (kLutSize - 1);
    const int i = std::min(int(pos), kLutSize - 2);
    const float f = pos - float(i);
    return m_lut[i] + (m_lut[i + 1] - m_lut[i]) * f;
}

} // namespace mothion
