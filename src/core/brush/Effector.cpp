#include "core/brush/Effector.h"

#include "core/brush/BrushStyle.h"
#include "core/brush/Random.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace mothion::brush {

namespace {

EffectorCurve::Point midpoint(const EffectorCurve::Point &a, const EffectorCurve::Point &b)
{
    return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
}

double lerpSegment(const EffectorCurve::Point &a, const EffectorCurve::Point &b, double x)
{
    if (b.x == a.x)
        return a.y;
    const double t = std::clamp((x - a.x) / (b.x - a.x), 0.0, 1.0);
    return a.y + (b.y - a.y) * t;
}

// Big-endian readers over a byte span; return false past the end.
struct Reader
{
    std::span<const std::uint8_t> data;

    bool int32(std::size_t offset, std::int32_t &out) const
    {
        if (offset + 4 > data.size())
            return false;
        const std::uint32_t v = (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16)
                                | (std::uint32_t(data[offset + 2]) << 8) | std::uint32_t(data[offset + 3]);
        out = std::bit_cast<std::int32_t>(v);
        return true;
    }

    bool float64(std::size_t offset, double &out) const
    {
        if (offset + 8 > data.size())
            return false;
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < 8; ++i)
            v = (v << 8) | data[offset + i];
        out = std::bit_cast<double>(v);
        return true;
    }
};

bool readCurve(const Reader &r, std::size_t offset, EffectorCurve &out)
{
    std::int32_t header = 0, count = 0, step = 0;
    if (!r.int32(offset, header) || !r.int32(offset + 4, count) || !r.int32(offset + 8, step))
        return false;
    if (header < 12 || step < 16 || count < 0 || count > 64)
        return false;
    std::vector<EffectorCurve::Point> points(static_cast<std::size_t>(count));
    for (std::int32_t k = 0; k < count; ++k) {
        const std::size_t at = offset + std::size_t(header) + std::size_t(k) * std::size_t(step);
        if (!r.float64(at, points[k].x) || !r.float64(at + 8, points[k].y))
            return false;
    }
    out = EffectorCurve(std::move(points));
    return true;
}

} // namespace

EffectorCurve::EffectorCurve(std::vector<Point> points)
    : m_points(std::move(points))
{
}

double EffectorCurve::map(double x) const
{
    const std::size_t n = m_points.size();
    if (n == 0)
        return x;
    if (n == 1)
        return m_points[0].y;
    x = std::clamp(x, m_points.front().x, m_points.back().x);
    if (n == 2)
        return lerpSegment(m_points[0], m_points[1], x);

    // Find the inner control point whose piece contains x.
    std::size_t i = 1;
    while (i < n - 2 && x >= midpoint(m_points[i], m_points[i + 1]).x)
        ++i;
    const Point m0 = i == 1 ? m_points[0] : midpoint(m_points[i - 1], m_points[i]);
    const Point m1 = i == n - 2 ? m_points[n - 1] : midpoint(m_points[i], m_points[i + 1]);
    const Point c = m_points[i];

    // Solve x(t) = x for the quadratic Bézier m0, c, m1, then return y(t).
    const double a = m0.x - 2.0 * c.x + m1.x;
    const double b = 2.0 * (c.x - m0.x);
    const double k = m0.x - x;
    auto yAt = [&](double t) {
        const double u = 1.0 - t;
        return u * u * m0.y + 2.0 * u * t * c.y + t * t * m1.y;
    };
    auto accept = [](double t) { return t >= -1e-9 && t <= 1.0 + 1e-9; };
    if (std::abs(a) < 1e-12) {
        if (std::abs(b) > 1e-12 && accept(-k / b))
            return yAt(std::clamp(-k / b, 0.0, 1.0));
        return lerpSegment(m0, m1, x);
    }
    const double disc = b * b - 4.0 * a * k;
    if (disc >= 0.0) {
        const double s = std::sqrt(disc);
        for (double t : {(-b + s) / (2.0 * a), (-b - s) / (2.0 * a)})
            if (accept(t))
                return yAt(std::clamp(t, 0.0, 1.0));
    }
    return lerpSegment(m0, m1, x);
}

Effector Effector::pressureOnly(EffectorCurve curve, double minimum)
{
    Effector e;
    e.active = InputPressure;
    e.pressure = std::move(curve);
    e.pressureMin = minimum;
    return e;
}

std::optional<Effector> Effector::parse(std::span<const std::uint8_t> blob)
{
    const Reader r{blob};
    std::int32_t h[12] = {};
    for (std::size_t i = 0; i < 12; ++i)
        if (!r.int32(i * 4, h[i]))
            return std::nullopt;
    const std::int32_t headerSize = h[0];
    if (headerSize < 40 || std::size_t(headerSize) > blob.size())
        return std::nullopt;

    Effector e;
    e.available = std::uint32_t(h[1]);
    e.active = std::uint32_t(h[2]);
    e.pressureMin = h[3] * 0.01;
    e.velocityFrom = h[4] * 0.01;
    e.tiltMin = h[5] * 0.01;
    e.randomMin = h[6] * 0.01;
    e.velocityTo = headerSize > 40 ? h[10] * 0.01 : 1.0;

    std::size_t offset = std::size_t(headerSize);
    const std::pair<EffectorCurve *, std::int32_t> curves[] = {
        {&e.pressure, h[8]},
        {&e.velocity, h[9]},
        {&e.tilt, headerSize > 44 ? h[11] : 0},
    };
    for (const auto &[curve, length] : curves) {
        if (length <= 0)
            continue;
        if (!readCurve(r, offset, *curve))
            return std::nullopt;
        offset += std::size_t(length);
    }
    return e;
}

double Effector::evaluate(double base, const BrushInput &in, Lcg *rng) const
{
    const std::uint32_t on = inputs();
    double v = base;
    if (on & InputPressure)
        v *= pressureMin + (1.0 - pressureMin) * pressure.map(in.pressure);
    if (on & InputVelocity)
        v *= velocityFrom + (velocityTo - velocityFrom) * velocity.map(in.velocity);
    if ((on & InputTilt) && in.hasTilt)
        v *= tiltMin + (1.0 - tiltMin) * tilt.map(in.tilt);
    if ((on & InputRandom) && rng)
        v *= randomMin + (1.0 - randomMin) * rng->next01();
    if (on & InputInOut)
        v *= inOutMin + (1.0 - inOutMin) * in.inOut;
    return v;
}

} // namespace mothion::brush
