#include "core/rig3d/Ik.h"

#include <algorithm>
#include <cmath>

namespace mothion::rig3d {

namespace {

// The turn of a node in the model's space (its global matrix's rotation; a scale is taken as uniform).
Quat globalTurn(const Model &model, const Pose &pose, int node)
{
    Quat q;
    std::vector<int> up;
    for (int n = node; n >= 0; n = model.nodes[std::size_t(n)].parent)
        up.push_back(n);
    for (auto it = up.rbegin(); it != up.rend(); ++it)
        q = q * pose.rotations[std::size_t(*it)];
    return q;
}

Quat parentTurn(const Model &model, const Pose &pose, int node)
{
    const int parent = model.nodes[std::size_t(node)].parent;
    return parent >= 0 ? globalTurn(model, pose, parent) : Quat{};
}

Quat slerp(const Quat &a, Quat b, double t)
{
    double d = dot(a, b);
    if (d < 0) {
        b = {-b.w, -b.x, -b.y, -b.z};
        d = -d;
    }
    if (d > 0.9995)
        return Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}.normalized();
    const double theta = std::acos(std::clamp(d, -1.0, 1.0));
    const double sa = std::sin((1 - t) * theta) / std::sin(theta), sb = std::sin(t * theta) / std::sin(theta);
    return Quat{a.w * sa + b.w * sb, a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb}.normalized();
}

// The local turn of `node` once it is turned by `world` (a turn in the model's space round its start), brought within
// its limits, softly first: within the soft zone of a limit a turn toward it goes only part of the way.
Quat limitedTurn(const Model &model, const Rig &rig, const Pose &pose, int node, const Quat &world)
{
    const Quat parent = parentTurn(model, pose, node);
    const Quat current = pose.rotations[std::size_t(node)];
    const Quat wanted = (parent.conjugate() * world * parent * current).normalized();
    const NodeLimit &limit = rig.limits[std::size_t(node)];
    Quat out = limit.clamp(wanted);
    const double zone = rig.settings[std::size_t(node)].softZone * kPi / 180.0;
    if (zone > 0) {
        const double before = limit.margin(current), after = limit.margin(out);
        if (after < before && after < zone) {
            out = slerp(current, out, std::max(0.1, after / zone));
            out = limit.clamp(out);
        }
    }
    return out;
}

bool isBone(const Model &model, int node)
{
    return model.isJoint(node);
}

} // namespace

Rig Rig::make(const Model &model, const std::vector<JointSettings> &settings)
{
    Rig rig;
    rig.settings = settings;
    rig.settings.resize(model.nodes.size());
    rig.limits.resize(model.nodes.size());
    for (std::size_t n = 0; n < model.nodes.size(); ++n)
        rig.limits[n] = {model.nodes[n].rotation, limitFrame(model, int(n)), JointLimit(rig.settings[n])};
    Vec3 center;
    restBounds(model, center, rig.scale);
    return rig;
}

std::vector<int> ikChain(const Model &model, const Rig &rig, int node)
{
    std::vector<int> chain;
    if (node < 0 || node >= int(model.nodes.size()))
        return chain;
    for (int n = model.nodes[std::size_t(node)].parent; n >= 0; n = model.nodes[std::size_t(n)].parent) {
        if (rig.settings[std::size_t(n)].anchor || !isBone(model, n))
            break;
        chain.push_back(n);
    }
    return chain;
}

Vec3 nodePosition(const Model &model, const Pose &pose, int node)
{
    return globalMatrices(model, pose)[std::size_t(node)].translationPart();
}

bool solveIk(const Model &model, const Rig &rig, Pose &pose, int node, const Vec3 &target, const IkOptions &options)
{
    const std::vector<int> chain = ikChain(model, rig, node);
    const double tolerance = options.tolerance * rig.scale;
    const double tiny = 1e-4 * rig.scale; // CSP's 1e-4 on lengths, for a model of size 1
    auto reached = [&] { return length(nodePosition(model, pose, node) - target) <= tolerance; };
    if (chain.empty())
        return reached();
    for (int it = 0; it < options.iterations; ++it) {
        if (reached())
            return true;
        for (int j : chain) {
            const std::vector<Mat4> g = globalMatrices(model, pose);
            const Vec3 p = g[std::size_t(j)].translationPart();
            const Vec3 a = g[std::size_t(node)].translationPart() - p, b = target - p;
            if (length(a) * length(b) < tiny * tiny)
                continue;
            const Vec3 axis = cross(a, b);
            if (length(axis) < tiny * tiny)
                continue; // already pointing there (or straight away: no one way to turn)
            const Vec3 n = normalized(axis);
            double theta = std::acos(std::clamp(dot(a, b) / (length(a) * length(b)), -1.0, 1.0));
            theta *= 1 - std::clamp(rig.settings[std::size_t(j)].stiffness, 0.0, 0.99);
            const Quat before = pose.rotations[std::size_t(j)];
            const Quat after = limitedTurn(model, rig, pose, j, Quat::axisAngle(n, theta));
            if (rig.limits[std::size_t(j)].limit.isFree()) {
                pose.rotations[std::size_t(j)] = after;
                continue;
            }
            // What is left of the turn once limited: taken back if it moves the node the wrong way.
            pose.rotations[std::size_t(j)] = after;
            const Vec3 moved = nodePosition(model, pose, node) - p;
            const Vec3 done = cross(a, moved);
            if (length(done) > tiny * tiny && dot(normalized(done), n) < 0.1)
                pose.rotations[std::size_t(j)] = before;
        }
    }
    return reached();
}

void aimAt(const Model &model, const Rig &rig, Pose &pose, int node, const Vec3 &target, int rounds)
{
    if (node < 0 || node >= int(model.nodes.size()))
        return;
    // The parent, then the bones above it that follow, while each does.
    std::vector<int> bones;
    for (int n = model.nodes[std::size_t(node)].parent; n >= 0; n = model.nodes[std::size_t(n)].parent) {
        if (!isBone(model, n) || (!bones.empty() && !rig.settings[std::size_t(n)].follow))
            break;
        bones.push_back(n);
    }
    // From the top down: each takes 1/n of the angle still left (n: the bones still to turn, itself included), so the
    // parent, last, takes all that is left and the node points at the target.
    std::reverse(bones.begin(), bones.end());
    const double tiny = 1e-4 * rig.scale;
    for (int round = 0; round < std::max(1, rounds); ++round)
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const int j = bones[i];
            const std::vector<Mat4> g = globalMatrices(model, pose);
            const Vec3 p = g[std::size_t(j)].translationPart();
            const Vec3 a = g[std::size_t(node)].translationPart() - p, b = target - p;
            const Vec3 axis = cross(a, b);
            if (length(a) * length(b) < tiny * tiny || length(axis) < tiny * tiny)
                continue;
            const double theta = std::acos(std::clamp(dot(a, b) / (length(a) * length(b)), -1.0, 1.0));
            const double share = theta / double(bones.size() - i);
            pose.rotations[std::size_t(j)] = limitedTurn(model, rig, pose, j, Quat::axisAngle(normalized(axis), share));
        }
}

void twistNode(const Model &model, const Rig &rig, Pose &pose, int node, double radians)
{
    if (node < 0 || node >= int(model.nodes.size()))
        return;
    const NodeLimit &limit = rig.limits[std::size_t(node)];
    // Round the bone's own axis: the limit frame's x, in the node's space.
    const Vec3 axis = limit.frame.rotate({1, 0, 0});
    const Quat turned = (pose.rotations[std::size_t(node)] * Quat::axisAngle(axis, radians)).normalized();
    pose.rotations[std::size_t(node)] = limit.clamp(turned);
}

} // namespace mothion::rig3d
