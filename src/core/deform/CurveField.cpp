#include "core/deform/CurveField.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::deform {

namespace {

constexpr double kDegrees = std::numbers::pi / 180.0;
constexpr int kCurveSamples = 32;    // points from one node to the next, for a Curve
constexpr int kEnvelopeSamples = 16; // and for an Envelope

double param(const doc::Deformer &deformer, const std::vector<double> &pose, std::size_t index)
{
    if (index < pose.size())
        return pose[index];
    return index < deformer.rest.size() ? deformer.rest[index] : 0.0;
}

Vec2 unit(Vec2 v)
{
    const double l = puppet::length(v);
    return l > 1e-12 ? v * (1.0 / l) : Vec2{};
}

Vec2 perpendicular(Vec2 v)
{
    return {-v.y, v.x};
}

Vec2 bezier(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double t)
{
    const double u = 1 - t;
    return a * (u * u * u) + b * (3 * u * u * t) + c * (3 * u * t * t) + d * (t * t * t);
}

// 1 up to `radius`, down to 0 at `radius + fade`, smoothly.
double zone(double distance, double radius, double fade)
{
    if (distance <= radius)
        return 1.0;
    if (fade <= 0 || distance >= radius + fade)
        return 0.0;
    const double u = (distance - radius) / fade;
    return 1.0 - u * u * (3.0 - 2.0 * u);
}

bool samePoints(const std::vector<Vec2> &a, const std::vector<Vec2> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (puppet::length(a[i] - b[i]) > 1e-9)
            return false;
    return true;
}

} // namespace

Vec2 CurveNode::direction() const
{
    return {std::cos(angle), std::sin(angle)};
}

std::vector<CurveNode> curveNodes(const doc::Deformer &deformer, const std::vector<double> &pose)
{
    std::vector<CurveNode> nodes(deformer.bones.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t at = i * doc::kNodeParams;
        CurveNode &n = nodes[i];
        const double a = param(deformer, pose, at + doc::NodeA), b = param(deformer, pose, at + doc::NodeB);
        n.angle = param(deformer, pose, at + doc::NodeAngle) * kDegrees;
        n.in = std::max(0.0, param(deformer, pose, at + doc::NodeIn));
        n.out = std::max(0.0, param(deformer, pose, at + doc::NodeOut));
        n.starts = i == 0 || deformer.bones[i].starts;
        if (n.starts) {
            n.at = {a, b};
        } else {
            const CurveNode &before = nodes[i - 1];
            const double way = before.angle + a * kDegrees;
            n.at = before.at + Vec2{std::cos(way), std::sin(way)} * b;
            n.angle += before.angle;
        }
    }
    return nodes;
}

std::vector<double> poseFromNodes(const std::vector<CurveNode> &nodes, const std::vector<double> &near)
{
    std::vector<double> pose(nodes.size() * doc::kNodeParams, 0.0);
    auto nearest = [&](std::size_t index, double degrees) {
        const double reference = index < near.size() ? near[index] : 0.0;
        return reference + std::remainder(degrees - reference, 360.0);
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t at = i * doc::kNodeParams;
        const CurveNode &n = nodes[i];
        if (i == 0 || n.starts) {
            pose[at + doc::NodeA] = n.at.x;
            pose[at + doc::NodeB] = n.at.y;
            pose[at + doc::NodeAngle] = nearest(at + doc::NodeAngle, n.angle / kDegrees);
        } else {
            const CurveNode &before = nodes[i - 1];
            const Vec2 v = n.at - before.at;
            pose[at + doc::NodeA] = nearest(at + doc::NodeA, (std::atan2(v.y, v.x) - before.angle) / kDegrees);
            pose[at + doc::NodeB] = puppet::length(v);
            pose[at + doc::NodeAngle] = nearest(at + doc::NodeAngle, (n.angle - before.angle) / kDegrees);
        }
        pose[at + doc::NodeIn] = n.in;
        pose[at + doc::NodeOut] = n.out;
    }
    return pose;
}

std::vector<std::pair<std::size_t, std::size_t>> curveRuns(const std::vector<CurveNode> &nodes)
{
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i == 0 || nodes[i].starts)
            runs.push_back({i, i + 1});
        else
            runs.back().second = i + 1;
    }
    return runs;
}

std::vector<Vec2> sampleCurve(const std::vector<CurveNode> &nodes, bool closed, int perSegment)
{
    std::vector<Vec2> points;
    if (nodes.empty())
        return points;
    const std::size_t segments = closed && nodes.size() > 1 ? nodes.size() : nodes.size() - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const CurveNode &a = nodes[i], &b = nodes[(i + 1) % nodes.size()];
        for (int k = 0; k < perSegment; ++k)
            points.push_back(bezier(a.at, a.outHandle(), b.inHandle(), b.at, double(k) / perSegment));
    }
    if (!closed || nodes.size() == 1)
        points.push_back(nodes.back().at);
    return points;
}

// --- Curve -------------------------------------------------------------------------------------------------------------

CurveField::CurveField(const doc::Deformer &deformer, const std::vector<double> &pose)
{
    const std::vector<CurveNode> restNodes = curveNodes(deformer, deformer.rest), posedNodes = curveNodes(deformer, pose);
    for (const auto &[begin, end] : curveRuns(restNodes)) {
        if (end - begin < 2)
            continue; // one node is no curve: it moves nothing
        const std::vector<CurveNode> restRun(restNodes.begin() + std::ptrdiff_t(begin), restNodes.begin() + std::ptrdiff_t(end));
        const std::vector<CurveNode> posedRun(posedNodes.begin() + std::ptrdiff_t(begin), posedNodes.begin() + std::ptrdiff_t(end));
        const std::vector<Vec2> rest = sampleCurve(restRun, false, kCurveSamples);
        const std::vector<Vec2> posed = sampleCurve(posedRun, false, kCurveSamples);
        if (samePoints(rest, posed))
            continue; // this curve did not move: it moves nothing
        const std::size_t n = rest.size();
        std::vector<Sample> samples(n);
        // The normal at each point: the mean of the pieces' on either side.
        auto normals = [n](const std::vector<Vec2> &points, std::size_t i) {
            Vec2 sum;
            if (i > 0)
                sum = sum + perpendicular(unit(points[i] - points[i - 1]));
            if (i + 1 < n)
                sum = sum + perpendicular(unit(points[i + 1] - points[i]));
            return unit(sum);
        };
        for (std::size_t i = 0; i < n; ++i) {
            Sample &s = samples[i];
            s.rest = rest[i];
            s.posed = posed[i];
            s.restNormal = normals(rest, i);
            s.posedNormal = normals(posed, i);
            // The zone between two nodes, as far between them.
            const double along = double(i) / kCurveSamples;
            const std::size_t node = std::min<std::size_t>(std::size_t(along), end - begin - 2);
            const double u = along - double(node);
            const doc::Bone &a = deformer.bones[begin + node], &b = deformer.bones[begin + node + 1];
            s.radius = a.radius + (b.radius - a.radius) * u;
            s.fade = a.fade + (b.fade - a.fade) * u;
        }
        m_curves.push_back(std::move(samples));
    }
    m_identity = m_curves.empty();
}

Vec2 CurveField::map(Vec2 p) const
{
    if (m_identity)
        return p;
    // The piece of a curve at rest nearest the point, and how far along it.
    const std::vector<Sample> *curve = nullptr;
    std::size_t best = 0;
    double bestDistance = 1e300, bestT = 0;
    for (const std::vector<Sample> &samples : m_curves)
        for (std::size_t k = 0; k + 1 < samples.size(); ++k) {
            const Vec2 a = samples[k].rest, ab = samples[k + 1].rest - a;
            const double length2 = puppet::length2(ab);
            const double t = length2 > 1e-18 ? std::clamp(puppet::dot(p - a, ab) / length2, 0.0, 1.0) : 0.0;
            const double d = puppet::length2(p - (a + ab * t));
            if (d < bestDistance) {
                bestDistance = d;
                curve = &samples;
                best = k;
                bestT = t;
            }
        }
    if (!curve)
        return p;
    const std::vector<Sample> &samples = *curve;
    const Sample &s0 = samples[best], &s1 = samples[best + 1];
    const Vec2 ab = s1.rest - s0.rest;
    const double length2 = puppet::length2(ab);
    // Past an end: on along the end piece's line.
    double t = bestT;
    if (length2 > 1e-18) {
        const double raw = puppet::dot(p - s0.rest, ab) / length2;
        if ((best == 0 && raw < 0) || (best + 2 == samples.size() && raw > 1))
            t = raw;
    }
    const double inside = std::clamp(t, 0.0, 1.0);
    const Vec2 restAt = s0.rest + ab * t;
    const Vec2 restNormal = unit(s0.restNormal * (1 - inside) + s1.restNormal * inside);
    const Vec2 tangent = unit(ab);
    // The point as so far along the piece and so far across, in the piece's own tangent and the smooth normal.
    const Vec2 rel = p - restAt;
    const double det = tangent.x * restNormal.y - tangent.y * restNormal.x;
    if (std::abs(det) < 1e-9)
        return p;
    const double along = (rel.x * restNormal.y - rel.y * restNormal.x) / det;
    const double across = (tangent.x * rel.y - tangent.y * rel.x) / det;
    const Vec2 pab = s1.posed - s0.posed;
    const Vec2 posedAt = s0.posed + pab * t;
    const Vec2 posedNormal = unit(s0.posedNormal * (1 - inside) + s1.posedNormal * inside);
    const double stretch = std::sqrt(length2) > 1e-9 ? puppet::length(pab) / std::sqrt(length2) : 1.0;
    const Vec2 moved = posedAt + unit(pab) * (along * stretch) + posedNormal * across;
    // How much of it the curve holds there.
    const double radius = s0.radius + (s1.radius - s0.radius) * inside, fade = s0.fade + (s1.fade - s0.fade) * inside;
    const double held = zone(std::sqrt(bestDistance), radius, fade);
    return p + (moved - p) * held;
}

// --- Envelope ----------------------------------------------------------------------------------------------------------

EnvelopeField::EnvelopeField(const doc::Deformer &deformer, const std::vector<double> &pose)
{
    if (deformer.bones.size() < 3)
        return; // fewer than three nodes close round nothing
    m_rest = sampleCurve(curveNodes(deformer, deformer.rest), true, kEnvelopeSamples);
    m_posed = sampleCurve(curveNodes(deformer, pose), true, kEnvelopeSamples);
    m_identity = samePoints(m_rest, m_posed);
}

Vec2 EnvelopeField::map(Vec2 p) const
{
    if (m_identity)
        return p;
    const std::size_t n = m_rest.size();
    thread_local std::vector<double> r, halfTan;
    r.resize(n);
    halfTan.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = puppet::length(m_rest[i] - p);
        if (r[i] < 1e-9)
            return m_posed[i]; // on a point of the curve
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const Vec2 a = m_rest[i] - p, b = m_rest[j] - p;
        const double cross = puppet::cross(a, b), dot = puppet::dot(a, b);
        const double denominator = r[i] * r[j] + dot;
        if (std::abs(denominator) < 1e-9 * r[i] * r[j]) {
            // On the piece between two points: as far between their places in the pose.
            const double u = r[i] / (r[i] + r[j]);
            return m_posed[i] + (m_posed[j] - m_posed[i]) * u;
        }
        halfTan[i] = cross / denominator; // tan of half the angle the piece takes up, seen from the point (signed)
    }
    double total = 0;
    Vec2 sum;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = (halfTan[(i + n - 1) % n] + halfTan[i]) / r[i];
        total += w;
        sum = sum + m_posed[i] * w;
    }
    if (std::abs(total) < 1e-12)
        return p;
    return sum * (1.0 / total);
}

} // namespace mothion::deform
