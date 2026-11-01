#include "core/brush/StrokeShaper.h"

#include "core/brush/DabGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::brush {

namespace {

constexpr double kHalfPi = std::numbers::pi / 2.0;

double distance(const BrushInput &a, const BrushInput &b) { return std::hypot(b.x - a.x, b.y - a.y); }

std::vector<double> cumulative(const std::vector<BrushInput> &points)
{
    std::vector<double> s(points.size(), 0.0);
    for (std::size_t i = 1; i < points.size(); ++i)
        s[i] = s[i - 1] + distance(points[i - 1], points[i]);
    return s;
}

double fade(const InOutStyle &style, double d)
{
    if (style.type != InOutType::Fade || !style.out || style.outLength <= 0.0)
        return 1.0;
    return d < style.outLength ? 1.0 - d / style.outLength : 0.0;
}

double distanceToSegment(const BrushInput &p, const BrushInput &a, const BrushInput &b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 1e-12)
        return distance(p, a);
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

} // namespace

bool isJitter(const BrushInput &a, const BrushInput &b, const BrushInput &c, double factor)
{
    const double d1 = distance(a, b);
    const double d2 = distance(b, c);
    if (d2 <= 1e-9)
        return true; // c is b again: nothing lost by replacing it
    if (d1 <= 1e-9)
        return false;
    const double l = (d1 + d2) * factor;
    const double threshold = l < 1.0 ? 0.8 : l < 3.0 ? 0.7 : l < 10.0 ? 0.6 : l < 30.0 ? 0.5 : 0.4;
    const double abx = b.x - a.x, aby = b.y - a.y;
    const double bcx = c.x - b.x, bcy = c.y - b.y;
    const double cosine = (abx * bcx + aby * bcy) / (d1 * d2);
    if (cosine > threshold)
        return false; // a smooth continuation
    // Moving on along a nearly straight line: b is on the way, keep it.
    const double acx = c.x - a.x, acy = c.y - a.y;
    const double ac = std::hypot(acx, acy);
    if (cosine >= 0.0 && ac * 2.0 >= d1 + d2 && std::abs(acx * aby - acy * abx) < ac)
        return false;
    return true;
}

double liveInOut(const InOutStyle &style, double d)
{
    if (style.type == InOutType::Fade)
        return fade(style, d);
    if (style.type == InOutType::Length && style.in && style.inLength > 0.0 && d < style.inLength)
        return std::sin(kHalfPi * d / style.inLength);
    return 1.0;
}

bool usesInOut(const BrushStyle &s)
{
    if (!s.inOut.in && !s.inOut.out)
        return false;
    for (const Effector *e : {&s.sizeEffector, &s.dabOpacityEffector, &s.flowEffector, &s.spacingEffector,
                              &s.thicknessEffector, &s.spraySizeEffector, &s.sprayDensityEffector,
                              &s.color.subColorEffector, &s.color.hueEffector, &s.color.saturationEffector,
                              &s.color.valueEffector, &s.mix.amountEffector, &s.mix.densityEffector})
        if (e->uses(InputInOut))
            return true;
    return false;
}

bool needsFinalPass(const BrushStyle &s)
{
    return (usesInOut(s) && s.inOut.type != InOutType::Fade) || s.correction > 0 || s.revision > 0;
}

std::vector<BrushInput> finalPoints(const BrushStyle &style, std::vector<BrushInput> points)
{
    if (style.revision > 0)
        points = simplify(points, style.revision);
    if (style.correction > 0)
        applyStartRamp(points, std::max(3, style.correction));
    if (!usesInOut(style))
        return points;
    if (style.inOut.type != InOutType::Fade)
        return applyInOut(points, style.inOut);
    const std::vector<double> s = cumulative(points);
    for (std::size_t i = 0; i < points.size(); ++i)
        points[i].inOut = fade(style.inOut, s[i]);
    return points;
}

void applyStartRamp(std::vector<BrushInput> &points, int count)
{
    const std::size_t n = std::min(points.size(), std::size_t(std::max(count, 0)));
    if (n == 0)
        return;
    const std::vector<double> s = cumulative(points);
    const double length = std::max(2.0, s[n - 1]);
    for (std::size_t i = 0; i < n; ++i)
        points[i].pressure *= std::sin(kHalfPi * std::min(1.0, s[i] / length));
}

std::vector<BrushInput> applyInOut(const std::vector<BrushInput> &points, const InOutStyle &style)
{
    if (points.size() < 2 || (!style.in && !style.out))
        return points;
    const std::vector<double> s = cumulative(points);
    const double total = s.back();
    if (total <= 0.0)
        return points;
    const bool ratio = style.type == InOutType::Ratio;
    double inLen = style.in ? (ratio ? style.inRatio * total : style.inLength) : 0.0;
    double outLen = style.out ? (ratio ? style.outRatio * total : style.outLength) : 0.0;
    if (inLen + outLen > total) {
        const double k = total / (inLen + outLen);
        inLen *= k;
        outLen *= k;
    }

    // New points every quarter of each zone.
    std::vector<double> cuts;
    for (int k = 1; k <= 4; ++k) {
        if (inLen > 0.0)
            cuts.push_back(inLen * k / 4.0);
        if (outLen > 0.0)
            cuts.push_back(total - outLen * k / 4.0);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) { return b - a < 1e-6; }), cuts.end());

    std::vector<BrushInput> out;
    std::vector<double> at;
    out.push_back(points.front());
    at.push_back(0.0);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const double s0 = s[i], s1 = s[i + 1];
        if (s1 > s0)
            for (double c : cuts)
                if (c > s0 + 1e-6 && c < s1 - 1e-6) {
                    out.push_back(DabGenerator::interpolate(points[i], points[i + 1], (c - s0) / (s1 - s0)));
                    at.push_back(c);
                }
        out.push_back(points[i + 1]);
        at.push_back(s1);
    }

    for (std::size_t i = 0; i < out.size(); ++i) {
        double f = 1.0;
        if (inLen > 0.0 && at[i] < inLen)
            f = std::sin(kHalfPi * at[i] / inLen);
        if (outLen > 0.0 && total - at[i] < outLen)
            f = std::min(f, std::sin(kHalfPi * std::max(0.0, total - at[i]) / outLen));
        out[i].inOut = f;
    }
    return out;
}

std::vector<BrushInput> simplify(const std::vector<BrushInput> &points, int level)
{
    if (points.size() < 3 || level <= 0)
        return points;
    const double tolerance = std::min(3.0, 0.1 * level + 1.2);
    const int iterations = std::min(10, 3 + level / 5);

    // Indices of the points kept; each dropped one must stay near the chord
    // between the kept points around it.
    std::vector<std::size_t> kept(points.size());
    for (std::size_t i = 0; i < kept.size(); ++i)
        kept[i] = i;
    auto removable = [&](std::size_t k) {
        const BrushInput &a = points[kept[k - 1]];
        const BrushInput &b = points[kept[k + 1]];
        for (std::size_t i = kept[k - 1] + 1; i < kept[k + 1]; ++i)
            if (distanceToSegment(points[i], a, b) > tolerance)
                return false;
        return true;
    };
    for (int pass = 0; pass < iterations; ++pass) {
        bool changed = false;
        for (std::size_t k = 1; k + 1 < kept.size();) {
            if (removable(k)) {
                kept.erase(kept.begin() + std::ptrdiff_t(k));
                changed = true;
            } else {
                ++k;
            }
        }
        if (!changed)
            break;
    }
    // Nearly coincident points add nothing but kinks.
    for (std::size_t k = 1; k + 1 < kept.size();) {
        if (distance(points[kept[k]], points[kept[k - 1]]) < 1.0)
            kept.erase(kept.begin() + std::ptrdiff_t(k));
        else
            ++k;
    }

    // Catmull-Rom through the kept points, about every 2 px; the pen state
    // is interpolated linearly between them.
    std::vector<BrushInput> out;
    out.push_back(points[kept.front()]);
    for (std::size_t k = 0; k + 1 < kept.size(); ++k) {
        const BrushInput &p0 = points[kept[k == 0 ? 0 : k - 1]];
        const BrushInput &p1 = points[kept[k]];
        const BrushInput &p2 = points[kept[k + 1]];
        const BrushInput &p3 = points[kept[std::min(k + 2, kept.size() - 1)]];
        const int steps = std::max(1, int(std::ceil(distance(p1, p2) / 2.0)));
        for (int j = 1; j <= steps; ++j) {
            const double t = double(j) / steps;
            BrushInput p = DabGenerator::interpolate(p1, p2, t);
            const double t2 = t * t, t3 = t2 * t;
            auto cr = [&](double a, double b, double c, double d) {
                return 0.5 * (2.0 * b + (c - a) * t + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2
                              + (3.0 * b - a - 3.0 * c + d) * t3);
            };
            p.x = cr(p0.x, p1.x, p2.x, p3.x);
            p.y = cr(p0.y, p1.y, p2.y, p3.y);
            out.push_back(p);
        }
    }
    return out;
}

} // namespace mothion::brush
