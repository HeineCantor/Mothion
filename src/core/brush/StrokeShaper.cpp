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

// Distance from p to the infinite line through a and b.
double distanceToLine(const BrushInput &p, const BrushInput &a, const BrushInput &b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (len <= 1e-9)
        return distance(p, a);
    return std::abs((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
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

bool isCorner(double ax, double ay, double bx, double by, double cx, double cy, double tolerance)
{
    const double d1 = std::hypot(bx - ax, by - ay);
    const double d2 = std::hypot(cx - bx, cy - by);
    if (d1 < 1e-8 || d2 < 1e-8)
        return true; // a point on top of another: break there rather than smooth through
    const double cosine = ((bx - ax) * (cx - bx) + (by - ay) * (cy - by)) / (d1 * d2);
    const double l = d1 + d2;
    const double threshold = l < 1.0 ? 0.8 : l < 3.0 ? 0.7 : l < 10.0 ? 0.6 : l < 30.0 ? 0.5 : 0.4;
    if (cosine > threshold)
        return false; // turning too little
    if (cosine >= 0.0) { // at most 90 degrees: is it just the hand wobbling?
        const double chord = std::hypot(cx - ax, cy - ay);
        const double offLine = std::abs((cx - ax) * (by - ay) - (cy - ay) * (bx - ax)); // chord x distance
        if (chord > 0.0 && l <= 2.0 * chord && offLine < chord * tolerance)
            return false;
    }
    return true;
}

std::optional<CornerTip> cornerTip(double cx, double cy, double inX, double inY, double outX, double outY,
                                   double radius)
{
    const double inLength = std::hypot(inX, inY), outLength = std::hypot(outX, outY);
    if (inLength < 1e-9 || outLength < 1e-9 || radius <= 0.0)
        return std::nullopt;
    const double ix = inX / inLength, iy = inY / inLength, ox = outX / outLength, oy = outY / outLength;
    if (ix * ox + iy * oy > std::cos(std::numbers::pi / 6.0))
        return std::nullopt; // turning less than 30 degrees: the round dab is enough
    const double turn = ix * oy - iy * ox; // which way it turns
    if (std::abs(turn) < 0.01)
        return std::nullopt; // going back on itself: the edges never meet
    // Outer normals: away from the side the stroke turns to.
    const double side = turn > 0 ? -1.0 : 1.0;
    const double n1x = side * -iy, n1y = side * ix, n2x = side * -oy, n2y = side * ox;
    // The edges meet at radius x (n1 + n2) / (1 + n1.n2) from the corner.
    const double k = radius / (1.0 + n1x * n2x + n1y * n2y);
    double tx = (n1x + n2x) * k, ty = (n1y + n2y) * k;
    const double length = std::hypot(tx, ty);
    if (length > 2.0 * radius) { // a very sharp turn: cap the spike
        tx *= 2.0 * radius / length;
        ty *= 2.0 * radius / length;
    }
    CornerTip tip;
    tip.x[0] = cx, tip.y[0] = cy;
    tip.x[1] = cx + radius * n1x, tip.y[1] = cy + radius * n1y;
    tip.x[2] = cx + tx, tip.y[2] = cy + ty;
    tip.x[3] = cx + radius * n2x, tip.y[3] = cy + radius * n2y;
    return tip;
}

std::optional<CornerTip> cornerTipAt(const std::vector<BrushInput> &points, std::size_t i, double radius)
{
    constexpr double kApart = 0.5; // px: closer neighbours give no direction
    const BrushInput &c = points[i];
    std::optional<std::size_t> before, after;
    for (std::size_t k = i; k-- > 0;)
        if (distance(points[k], c) > kApart) {
            before = k;
            break;
        }
    for (std::size_t k = i + 1; k < points.size(); ++k)
        if (distance(points[k], c) > kApart) {
            after = k;
            break;
        }
    if (!before || !after)
        return std::nullopt;
    const BrushInput &a = points[*before], &b = points[*after];
    return cornerTip(c.x, c.y, c.x - a.x, c.y - a.y, b.x - c.x, b.y - c.y, radius);
}

void CornerDetector::reset()
{
    m_points.clear();
    m_along.clear();
    m_cosine.clear();
    m_taken = m_tested = 0;
}

void CornerDetector::add(double x, double y)
{
    const double along = m_points.empty() ? 0.0
                                          : m_along.back() + std::hypot(x - m_points.back().x, y - m_points.back().y);
    m_points.push_back({x, y});
    m_along.push_back(along);
}

std::vector<CornerDetector::Decision> CornerDetector::take(bool end)
{
    // Judge every point the path has gone `window` past.
    const double reached = m_along.empty() ? 0.0 : m_along.back();
    while (m_tested < m_points.size() && (end || reached - m_along[m_tested] >= m_window)) {
        const std::size_t i = m_tested++;
        double turn = 2.0; // not a corner
        std::size_t a = i, c = i;
        while (a > 0 && m_along[i] - m_along[a] < m_window)
            --a;
        while (c + 1 < m_points.size() && m_along[c] - m_along[i] < m_window)
            ++c;
        if (m_along[i] - m_along[a] >= m_window && m_along[c] - m_along[i] >= m_window) {
            const Point &pa = m_points[a], &pb = m_points[i], &pc = m_points[c];
            if (isCorner(pa.x, pa.y, pb.x, pb.y, pc.x, pc.y, m_tolerance)) {
                const double d1 = std::hypot(pb.x - pa.x, pb.y - pa.y), d2 = std::hypot(pc.x - pb.x, pc.y - pb.y);
                turn = ((pb.x - pa.x) * (pc.x - pb.x) + (pb.y - pa.y) * (pc.y - pb.y)) / (d1 * d2);
            }
        }
        m_cosine.push_back(turn);
    }

    // Hand out points up to the last one whose run of turning neighbours is over.
    std::vector<Decision> out;
    while (m_taken < m_tested) {
        if (m_cosine[m_taken] > 1.5) { // not turning
            out.push_back({});
            ++m_taken;
            continue;
        }
        std::size_t runEnd = m_taken;
        while (runEnd < m_tested && m_cosine[runEnd] <= 1.5)
            ++runEnd;
        if (runEnd == m_tested && !end)
            break; // the run may go on: wait
        std::size_t sharpest = m_taken;
        for (std::size_t k = m_taken; k < runEnd; ++k)
            if (m_cosine[k] < m_cosine[sharpest])
                sharpest = k;
        for (std::size_t k = m_taken; k < runEnd; ++k) {
            Decision d;
            if (k == sharpest) {
                std::size_t a = k, c = k;
                while (a > 0 && m_along[k] - m_along[a] < m_window)
                    --a;
                while (c + 1 < m_points.size() && m_along[c] - m_along[k] < m_window)
                    ++c;
                d.corner = true;
                d.before = m_points[a];
                d.after = m_points[c];
            }
            out.push_back(d);
        }
        m_taken = runEnd;
    }
    return out;
}

double inOutFactor(const InOutStyle &style, double d, double total)
{
    if (style.type == InOutType::Fade)
        return fade(style, d);
    const bool known = total >= 0.0;
    if (style.type == InOutType::Ratio && !known)
        return 1.0; // the zones are fractions of a length not known yet
    const bool ratio = style.type == InOutType::Ratio;
    double inLen = style.in ? (ratio ? style.inRatio * total : style.inLength) : 0.0;
    double outLen = style.out && known ? (ratio ? style.outRatio * total : style.outLength) : 0.0;
    if (known && inLen + outLen > total && inLen + outLen > 0.0) {
        const double k = total / (inLen + outLen);
        inLen *= k;
        outLen *= k;
    }
    double f = 1.0;
    if (inLen > 0.0 && d < inLen)
        f = std::sin(kHalfPi * std::max(0.0, d) / inLen);
    if (outLen > 0.0 && total - d < outLen)
        f = std::min(f, std::sin(kHalfPi * std::max(0.0, total - d) / outLen));
    return f;
}

double startTaperFactor(double length, double d)
{
    if (length <= 0.0 || d >= length)
        return 1.0;
    return std::sin(kHalfPi * std::max(0.0, d) / length);
}

double strokeLength(const std::vector<BrushInput> &points)
{
    return points.empty() ? 0.0 : cumulative(points).back();
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
    const bool endDependent =
        usesInOut(s) && (s.inOut.type == InOutType::Ratio || (s.inOut.type == InOutType::Length && s.inOut.out));
    return endDependent || s.revision > 0 || s.startCorrection > 0;
}

std::vector<BrushInput> finalPoints(const BrushStyle &style, std::vector<BrushInput> points)
{
    if (style.revision > 0)
        points = style.revisionCurves ? postCorrect(points, style.revision, style.revisionBySpeed)
                                      : simplify(points, style.revision);
    if (style.startCorrection > 0)
        applyStartRamp(points, std::max(3, style.startCorrection));
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
        if (points[kept[k]].corner)
            return false;
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
        if (!points[kept[k]].corner && distance(points[kept[k]], points[kept[k - 1]]) < 1.0)
            kept.erase(kept.begin() + std::ptrdiff_t(k));
        else
            ++k;
    }

    // Centripetal Catmull-Rom through the kept points (knots spaced by the
    // square root of the distances: no loops or overshoot where points
    // bunch up at a turn), about every 2 px; the pen state is interpolated
    // linearly between them. At a corner the curve stops: the neighbour
    // across it is not used, so the corner stays sharp.
    auto knot = [](const BrushInput &a, const BrushInput &b) { return std::max(1e-6, std::sqrt(distance(a, b))); };
    struct P
    {
        double x, y;
    };
    auto mix = [](P a, P b, double ta, double tb, double t) {
        const double u = (t - ta) / (tb - ta);
        return P{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
    };
    std::vector<BrushInput> out;
    out.push_back(points[kept.front()]);
    for (std::size_t k = 0; k + 1 < kept.size(); ++k) {
        const BrushInput &p1 = points[kept[k]];
        const BrushInput &p2 = points[kept[k + 1]];
        const BrushInput &p0 = p1.corner ? p1 : points[kept[k == 0 ? 0 : k - 1]];
        const BrushInput &p3 = p2.corner ? p2 : points[kept[std::min(k + 2, kept.size() - 1)]];
        const double t0 = 0.0, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
        const P q0{p0.x, p0.y}, q1{p1.x, p1.y}, q2{p2.x, p2.y}, q3{p3.x, p3.y};
        const int steps = std::max(1, int(std::ceil(distance(p1, p2) / 2.0)));
        for (int j = 1; j <= steps; ++j) {
            const double u = double(j) / steps;
            const double t = t1 + (t2 - t1) * u;
            // Barry-Goldman pyramid.
            const P a1 = mix(q0, q1, t0, t1, t), a2 = mix(q1, q2, t1, t2, t), a3 = mix(q2, q3, t2, t3, t);
            const P b1 = mix(a1, a2, t0, t2, t), b2 = mix(a2, a3, t1, t3, t);
            const P c = mix(b1, b2, t1, t2, t);
            BrushInput p = DabGenerator::interpolate(p1, p2, u);
            p.x = c.x;
            p.y = c.y;
            p.corner = j == steps && p2.corner;
            out.push_back(p);
        }
    }
    return out;
}

std::vector<BrushInput> postCorrect(const std::vector<BrushInput> &points, int level, bool bySpeed)
{
    const std::size_t n = points.size();
    if (n < 3 || level <= 0)
        return points;
    const double f = level * level / 100.0;
    constexpr std::size_t kLookAhead = 200;

    // Nodes.
    std::vector<std::size_t> nodes{0};
    std::size_t s = 0;
    while (s + 1 < n) {
        std::size_t next = std::min(n - 1, s + kLookAhead);
        std::optional<std::size_t> ref;
        for (std::size_t j = s + 1; j <= std::min(n - 1, s + kLookAhead); ++j) {
            if (distance(points[j], points[s]) < 2.0 * f)
                continue; // too close to the node to say anything
            if (!ref) {
                ref = j; // the way the stroke leaves the node
                continue;
            }
            const double off = distanceToSegment(points[j], points[s], points[*ref]);
            const double step = distance(points[j], points[j - 1]);
            const double tolerance = bySpeed ? std::sqrt(0.025) * f * step : f;
            if (off >= tolerance && distanceToLine(points[j], points[s], points[*ref]) >= tolerance) {
                next = j;
                break;
            }
        }
        if (next == n - 1 || next <= s)
            next = std::max(next, s + 1);
        nodes.push_back(next);
        s = next;
    }
    if (nodes.back() != n - 1)
        nodes.push_back(n - 1);

    // Corners among the nodes.
    const std::size_t m = nodes.size();
    std::vector<char> corner(m, 0);
    for (std::size_t k = 1; k + 1 < m; ++k) {
        const BrushInput &a = points[nodes[k - 1]], &b = points[nodes[k]], &c = points[nodes[k + 1]];
        corner[k] = isCorner(a.x, a.y, b.x, b.y, c.x, c.y, 3.0 * f + 1.0) ? 1 : 0;
    }

    // The curve, midpoint to midpoint, through the corners.
    std::vector<BrushInput> out;
    auto add = [&](const BrushInput &from, const BrushInput &control, const BrushInput &to, bool curved,
                   bool endsOnCorner) {
        const double length = distance(from, control) + distance(control, to);
        const int steps = std::max(1, int(std::ceil(length / 2.0)));
        for (int j = 1; j <= steps; ++j) {
            const double t = double(j) / steps;
            BrushInput p = DabGenerator::interpolate(from, to, t);
            if (curved) {
                const double u = 1.0 - t;
                p.x = u * u * from.x + 2.0 * u * t * control.x + t * t * to.x;
                p.y = u * u * from.y + 2.0 * u * t * control.y + t * t * to.y;
            }
            p.corner = j == steps && endsOnCorner;
            out.push_back(p);
        }
    };
    BrushInput current = points[nodes[0]];
    current.corner = false;
    out.push_back(current);
    for (std::size_t k = 1; k + 1 < m; ++k) {
        const BrushInput &node = points[nodes[k]];
        if (corner[k]) { // straight to the corner, which stays where it is
            add(current, node, node, false, true);
            current = node;
        } else {
            const BrushInput mid = DabGenerator::interpolate(node, points[nodes[k + 1]], 0.5);
            add(current, node, mid, true, false);
            current = mid;
        }
    }
    add(current, points[nodes[m - 1]], points[nodes[m - 1]], false, false);
    return out;
}

} // namespace mothion::brush
