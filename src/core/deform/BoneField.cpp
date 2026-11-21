#include "core/deform/BoneField.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::deform {

namespace {

constexpr double kDegrees = std::numbers::pi / 180.0;

double param(const doc::Deformer &deformer, const std::vector<double> &pose, std::size_t index)
{
    if (index < pose.size())
        return pose[index];
    return index < deformer.rest.size() ? deformer.rest[index] : 0.0;
}

// The distance from `p` to the segment from `a` along `direction` for `length`.
double segmentDistance(Vec2 p, Vec2 a, Vec2 direction, double length)
{
    const double along = std::clamp(puppet::dot(p - a, direction), 0.0, std::max(0.0, length));
    return puppet::length(p - (a + direction * along));
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

} // namespace

Vec2 BoneFrame::direction() const
{
    return {std::cos(angle), std::sin(angle)};
}

Vec2 BoneFrame::end() const
{
    return origin + direction() * length;
}

std::vector<BoneFrame> boneFrames(const doc::Deformer &deformer, const std::vector<double> &pose)
{
    std::vector<BoneFrame> frames(deformer.bones.size());
    for (std::size_t i = 0; i < deformer.bones.size(); ++i) {
        const std::size_t at = i * doc::kBoneParams;
        const double x = param(deformer, pose, at + doc::BoneX), y = param(deformer, pose, at + doc::BoneY);
        BoneFrame &f = frames[i];
        f.angle = param(deformer, pose, at + doc::BoneAngle) * kDegrees;
        f.length = std::max(0.0, param(deformer, pose, at + doc::BoneLength));
        const int parent = deformer.bones[i].parent;
        if (parent >= 0 && std::size_t(parent) < i) {
            const BoneFrame &p = frames[std::size_t(parent)];
            const Vec2 along = p.direction(), across{-along.y, along.x};
            f.origin = p.end() + along * x + across * y;
            f.angle += p.angle;
        } else {
            f.origin = {x, y};
        }
    }
    return frames;
}

std::vector<double> poseFromFrames(const doc::Deformer &deformer, const std::vector<BoneFrame> &frames,
                                   const std::vector<double> &near)
{
    std::vector<double> pose(deformer.bones.size() * doc::kBoneParams, 0.0);
    for (std::size_t i = 0; i < deformer.bones.size() && i < frames.size(); ++i) {
        const std::size_t at = i * doc::kBoneParams;
        const BoneFrame &f = frames[i];
        const int parent = deformer.bones[i].parent;
        double angle = f.angle;
        if (parent >= 0 && std::size_t(parent) < i) {
            const BoneFrame &p = frames[std::size_t(parent)];
            const Vec2 along = p.direction(), across{-along.y, along.x}, offset = f.origin - p.end();
            pose[at + doc::BoneX] = puppet::dot(offset, along);
            pose[at + doc::BoneY] = puppet::dot(offset, across);
            angle -= p.angle;
        } else {
            pose[at + doc::BoneX] = f.origin.x;
            pose[at + doc::BoneY] = f.origin.y;
        }
        double degrees = angle / kDegrees;
        const double reference = at + doc::BoneAngle < near.size() ? near[at + doc::BoneAngle] : 0.0;
        degrees = reference + std::remainder(degrees - reference, 360.0);
        pose[at + doc::BoneAngle] = degrees;
        pose[at + doc::BoneLength] = f.length;
    }
    return pose;
}

BoneField::BoneField(const doc::Deformer &deformer, const std::vector<double> &pose)
{
    const std::vector<BoneFrame> rest = boneFrames(deformer, deformer.rest), now = boneFrames(deformer, pose);
    m_parts.reserve(rest.size());
    for (std::size_t i = 0; i < rest.size(); ++i) {
        Part part;
        part.origin = rest[i].origin;
        part.direction = rest[i].direction();
        part.length = rest[i].length;
        part.stretch = rest[i].length > 1e-9 ? now[i].length / rest[i].length : 1.0;
        part.radius = deformer.bones[i].radius;
        part.fade = deformer.bones[i].fade;
        // The rigid move from rest to pose: turned by θ round the page's origin, then moved by t.
        const double theta = now[i].angle - rest[i].angle;
        const double c = std::cos(theta), s = std::sin(theta);
        const Vec2 t = now[i].origin - Vec2{c * rest[i].origin.x - s * rest[i].origin.y, s * rest[i].origin.x + c * rest[i].origin.y};
        part.rx = std::cos(theta / 2);
        part.ry = std::sin(theta / 2);
        // ½ t r̄
        part.sx = 0.5 * (t.x * part.rx + t.y * part.ry);
        part.sy = 0.5 * (t.y * part.rx - t.x * part.ry);
        if (std::abs(theta) > 1e-9 || std::abs(t.x) > 1e-9 || std::abs(t.y) > 1e-9 || std::abs(part.stretch - 1) > 1e-9)
            m_identity = false;
        m_parts.push_back(part);
    }
}

Vec2 BoneField::map(Vec2 p) const
{
    if (m_identity)
        return p;
    // How much each bone's zone holds the point, and how much any does.
    double held = 0, total = 0;
    thread_local std::vector<double> weights;
    weights.assign(m_parts.size(), 0.0);
    for (std::size_t i = 0; i < m_parts.size(); ++i) {
        const Part &b = m_parts[i];
        const double d = segmentDistance(p, b.origin, b.direction, b.length);
        const double z = zone(d, b.radius, b.fade);
        if (z <= 0)
            continue;
        held = std::max(held, z);
        // The nearer bones count more: a joint is shared by both its bones, and bends evenly between them.
        const double soft = std::max(1.0, 0.5 * b.radius);
        weights[i] = z / (d * d + soft * soft);
        total += weights[i];
    }
    if (held <= 0 || total <= 0)
        return p;

    // The point stretched by each bone, blended; and the bones' rigid moves, blended as dual numbers.
    const double still = 1.0 - held;
    Vec2 stretched = p * still;
    // The hemisphere of the bone that holds most (r and −r are the same turn: all are taken on its side).
    std::size_t strongest = 0;
    for (std::size_t i = 1; i < m_parts.size(); ++i)
        if (weights[i] > weights[strongest])
            strongest = i;
    const Part &ref = m_parts[strongest];
    double rx = ref.rx >= 0 ? still : -still, ry = 0, sx = 0, sy = 0; // the point's own place counts as no move
    for (std::size_t i = 0; i < m_parts.size(); ++i) {
        if (weights[i] <= 0)
            continue;
        const Part &b = m_parts[i];
        const double w = held * weights[i] / total;
        const Vec2 rel = p - b.origin;
        const double along = puppet::dot(rel, b.direction);
        const double moved = along <= 0 ? along : along <= b.length ? along * b.stretch : along + b.length * (b.stretch - 1);
        stretched = stretched + (p + b.direction * (moved - along)) * w;
        const double sign = b.rx * ref.rx + b.ry * ref.ry < 0 ? -w : w;
        rx += sign * b.rx;
        ry += sign * b.ry;
        sx += sign * b.sx;
        sy += sign * b.sy;
    }
    const double norm = std::hypot(rx, ry);
    if (norm < 1e-12)
        return stretched;
    rx /= norm, ry /= norm, sx /= norm, sy /= norm;
    // r² turns, t = 2 s r moves.
    const double c = rx * rx - ry * ry, s = 2 * rx * ry;
    const double tx = 2 * (sx * rx - sy * ry), ty = 2 * (sx * ry + sy * rx);
    return {c * stretched.x - s * stretched.y + tx, s * stretched.x + c * stretched.y + ty};
}

} // namespace mothion::deform
