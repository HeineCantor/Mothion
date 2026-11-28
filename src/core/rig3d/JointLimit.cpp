#include "core/rig3d/JointLimit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mothion::rig3d {

namespace {

constexpr double kRadians = kPi / 180.0;
// The furthest a bend may be limited to: short of half a turn, where the bone would point back at the pole the plane is
// seen from (whose image is at infinity).
constexpr double kMaxBend = kPi - 0.001;
// Past this the point is (nearly) on the circle's axis: every point of the circle is as near.
constexpr double kAxisDot = 0.9999995;

// The point of the sphere whose stereographic image (from −x) is (y, z).
Vec3 fromPlane(double y, double z)
{
    const double r2 = y * y + z * z;
    return {(1 - r2) / (1 + r2), 2 * y / (1 + r2), 2 * z / (1 + r2)};
}

// The circle where the stereographic coordinate `axis` (1: y, 2: z) is tan(angle / 2), keeping the side where it is
// greater (atLeast) or smaller. Its plane goes through the pole −x, so in the plane it is a straight line.
SphereCircle bendCircle(int axis, double angle, bool atLeast)
{
    const double s = std::sin(angle / 2), c = std::cos(angle / 2);
    // P_axis ≥ tan(angle/2) ⇔ v_axis·c − v_x·s ≥ s (with 1 + v_x > 0).
    Vec3 n{-s, 0, 0};
    (axis == 1 ? n.y : n.z) = c;
    SphereCircle circle{n, s};
    if (!atLeast)
        circle = {-n, -s};
    return circle;
}

// The cap whose stereographic image is the disk of center (cy, cz) and radius r.
SphereCircle diskCircle(double cy, double cz, double r)
{
    // |P − C|² ≤ r² with P = (v_y, v_z) / (1 + v_x): times (1 + v_x), with t = |C|² − r²,
    // (1 − t)·v_x + 2·cy·v_y + 2·cz·v_z ≥ 1 + t.
    const double t = cy * cy + cz * cz - r * r;
    const double norm = std::sqrt((1 + t) * (1 + t) + 4 * r * r);
    return {Vec3{1 - t, 2 * cy, 2 * cz} / norm, (1 + t) / norm};
}

} // namespace

Quat limitFrame(const Model &model, int node)
{
    const Node &n = model.nodes[std::size_t(node)];
    Vec3 along{0, 1, 0};
    for (int c : n.children)
        if (length(model.nodes[std::size_t(c)].translation) > 1e-9) {
            along = normalized(model.nodes[std::size_t(c)].translation);
            break;
        }
    const Vec3 x = along;
    Vec3 reference{1, 0, 0};
    if (std::abs(dot(reference, x)) > 0.9)
        reference = {0, 0, 1};
    const Vec3 y = normalized(reference - x * dot(reference, x));
    return Mat3::fromColumns(x, y, cross(x, y)).toQuat();
}

SwingTwist SwingTwist::of(const Quat &q)
{
    SwingTwist st;
    st.direction = normalized(q.rotate({1, 0, 0}));
    const Quat twist = Quat::between({1, 0, 0}, st.direction).conjugate() * q; // a turn round x
    st.twist = wrapAngle(2 * std::atan2(twist.x, twist.w));
    return st;
}

Quat SwingTwist::toQuat() const
{
    return Quat::between({1, 0, 0}, direction) * Quat::axisAngle({1, 0, 0}, twist);
}

double SwingTwist::bend(int axis) const
{
    const double denominator = 1 + direction.x;
    const double value = axis == 0 ? direction.y : direction.z;
    if (denominator < 1e-12)
        return value >= 0 ? kPi : -kPi;
    return 2 * std::atan(value / denominator);
}

bool SphereCircle::nearest(const Vec3 &v, Vec3 &out) const
{
    const double c = dot(n, v);
    if (std::abs(c) > kAxisDot || std::abs(d) >= 1)
        return false;
    // Into the circle's plane along n, then out to the sphere: c·(v − (n·v)·n) + d·n with c = √((1 − d²)/(1 − (n·v)²)).
    const double g = std::sqrt((1 - c * c) / (1 - d * d));
    out = normalized(v - n * (c - g * d));
    return true;
}

JointLimit::JointLimit(const JointSettings &s)
{
    if (s.limitTwist) {
        double lo = s.twistMin * kRadians, hi = s.twistMax * kRadians;
        if (lo > hi)
            std::swap(lo, hi);
        if (hi - lo < 2 * kPi) {
            m_twist = true;
            m_twistMin = wrapAngle(lo);
            m_twistMax = m_twistMin + (hi - lo);
        }
    }
    double tLo[2], tHi[2];
    for (int a = 0; a < 2; ++a) {
        m_bend[a] = s.limitBend[a];
        double lo = s.bendMin[a] * kRadians, hi = s.bendMax[a] * kRadians;
        if (lo > hi)
            std::swap(lo, hi);
        if (!m_bend[a]) {
            lo = -kMaxBend;
            hi = kMaxBend;
        }
        m_bendMin[a] = std::clamp(lo, -kMaxBend, kMaxBend);
        m_bendMax[a] = std::clamp(hi, -kMaxBend, kMaxBend);
        tLo[a] = std::tan(m_bendMin[a] / 2);
        tHi[a] = std::tan(m_bendMax[a] / 2);
    }
    if (!m_bend[0] && !m_bend[1])
        return; // the bone may point anywhere
    // A stadium only between two limited bends (a free one would make its round ends as wide as the sphere).
    m_shape = s.rectangle || !m_bend[0] || !m_bend[1] ? Shape::Rectangle : Shape::Stadium;
    double yLo = tLo[0], yHi = tHi[0], zLo = tLo[1], zHi = tHi[1];
    if (m_shape == Shape::Stadium) {
        const double h = yHi - yLo, w = zHi - zLo;
        if (w > h) { // wide: the ends along z rounded, each a half circle as wide as the band
            const double r = h / 2, cy = (yLo + yHi) / 2;
            zLo += r;
            zHi -= r;
            m_caps = {diskCircle(cy, zLo, r), diskCircle(cy, zHi, r)};
        } else { // tall: the ends along y
            const double r = w / 2, cz = (zLo + zHi) / 2;
            yLo += r;
            yHi -= r;
            m_caps = {diskCircle(yLo, cz, r), diskCircle(yHi, cz, r)};
        }
    }
    m_planes = {bendCircle(1, 2 * std::atan(yLo), true), bendCircle(1, 2 * std::atan(yHi), false),
                bendCircle(2, 2 * std::atan(zLo), true), bendCircle(2, 2 * std::atan(zHi), false)};
    m_corners = {fromPlane(yLo, zLo), fromPlane(yHi, zLo), fromPlane(yHi, zHi), fromPlane(yLo, zHi)};
}

bool JointLimit::swingContains(const Vec3 &v, double tolerance) const
{
    if (m_shape == Shape::None)
        return true;
    bool inRect = true;
    for (const SphereCircle &p : m_planes)
        inRect = inRect && p.contains(v, tolerance);
    if (inRect)
        return true;
    for (const SphereCircle &c : m_caps)
        if (c.contains(v, tolerance))
            return true;
    return false;
}

Vec3 JointLimit::clampSwing(const Vec3 &direction) const
{
    const Vec3 v = normalized(direction);
    if (swingContains(v))
        return direction;
    // The nearest point of the region is on its outline: inside one of its edges (there it is the nearest point of
    // that edge's whole circle) or at a corner. Of those that are in the region, the nearest (the greatest dot).
    Vec3 best = v;
    double bestDot = -2;
    auto consider = [&](const Vec3 &u) {
        const double d = dot(u, v);
        if (d > bestDot) {
            bestDot = d;
            best = u;
        }
    };
    for (const Vec3 &c : m_corners)
        consider(c);
    auto edges = [&](const std::vector<SphereCircle> &circles) {
        for (const SphereCircle &c : circles) {
            Vec3 u;
            if (c.nearest(v, u) && swingContains(u, 1e-9))
                consider(u);
        }
    };
    edges(m_planes);
    edges(m_caps);
    return best;
}

double JointLimit::clampTwist(double twist) const
{
    if (!m_twist)
        return twist;
    const double range = m_twistMax - m_twistMin;
    double past = std::fmod(twist - m_twistMin, 2 * kPi); // how far round from the low end
    if (past < 0)
        past += 2 * kPi;
    if (past <= range)
        return twist;
    // Out of the range: to the nearer of its ends, round whichever way is shorter.
    return past - range < 2 * kPi - past ? wrapAngle(m_twistMax) : m_twistMin;
}

Quat JointLimit::clamp(const Quat &turn) const
{
    if (isFree())
        return turn;
    SwingTwist st = SwingTwist::of(turn);
    const Vec3 direction = clampSwing(st.direction);
    const double twist = clampTwist(st.twist);
    if (direction == st.direction && twist == st.twist)
        return turn;
    st.direction = direction;
    st.twist = twist;
    return st.toQuat();
}

double JointLimit::margin(const Quat &turn) const
{
    double m = std::numeric_limits<double>::max();
    const SwingTwist st = SwingTwist::of(turn);
    if (m_twist) {
        double past = std::fmod(st.twist - m_twistMin, 2 * kPi);
        if (past < 0)
            past += 2 * kPi;
        const double range = m_twistMax - m_twistMin;
        m = std::min(m, past <= range ? std::min(past, range - past) : 0.0);
    }
    if (m_shape != Shape::None) {
        if (!swingContains(st.direction))
            return 0;
        // The distance to the bends' bounds, along each bend (for a stadium, an estimate: its corners are cut off).
        for (int a = 0; a < 2; ++a)
            if (m_bend[a]) {
                const double b = st.bend(a);
                m = std::min(m, std::max(0.0, std::min(b - m_bendMin[a], m_bendMax[a] - b)));
            }
    }
    return m;
}

Quat NodeLimit::clamp(const Quat &local) const
{
    if (limit.isFree())
        return local;
    return fromLimit(limit.clamp(toLimit(local))).normalized();
}

double NodeLimit::margin(const Quat &local) const
{
    return limit.margin(toLimit(local));
}

} // namespace mothion::rig3d
