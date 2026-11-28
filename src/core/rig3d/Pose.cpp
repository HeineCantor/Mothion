#include "core/rig3d/Pose.h"

#include "core/rig3d/Skinning.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mothion::rig3d {

namespace {
constexpr double kRadians = kPi / 180.0;
} // namespace

std::vector<int> Model::joints() const
{
    std::vector<int> out;
    std::vector<bool> seen(nodes.size(), false);
    for (const Skin &skin : skins)
        for (int j : skin.joints)
            if (j >= 0 && j < int(nodes.size()) && !seen[std::size_t(j)]) {
                seen[std::size_t(j)] = true;
                out.push_back(j);
            }
    if (out.empty())
        for (int n : order)
            out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}

bool Model::isJoint(int node) const
{
    const std::vector<int> all = joints();
    return std::binary_search(all.begin(), all.end(), node);
}

std::vector<Mat4> Model::restGlobals() const
{
    std::vector<Mat4> g(nodes.size());
    for (int n : order) {
        const Node &node = nodes[std::size_t(n)];
        const Mat4 local = Mat4::trs(node.translation, node.rotation, node.scale);
        g[std::size_t(n)] = node.parent >= 0 ? g[std::size_t(node.parent)] * local : local;
    }
    return g;
}

Vec3 OrbitCamera::eye() const
{
    const double y = yaw * kRadians, p = pitch * kRadians;
    return target + Vec3{std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)} * distance;
}

Mat4 OrbitCamera::view() const
{
    const Vec3 e = eye();
    const Vec3 forward = normalized(target - e);
    Vec3 right = cross(forward, {0, 1, 0});
    if (length(right) < 1e-9) // looking straight up or down
        right = {std::cos(yaw * kRadians), 0, -std::sin(yaw * kRadians)};
    right = normalized(right);
    const Vec3 up = cross(right, forward);
    Mat4 v;
    v(0, 0) = right.x, v(0, 1) = right.y, v(0, 2) = right.z, v(0, 3) = -dot(right, e);
    v(1, 0) = up.x, v(1, 1) = up.y, v(1, 2) = up.z, v(1, 3) = -dot(up, e);
    v(2, 0) = -forward.x, v(2, 1) = -forward.y, v(2, 2) = -forward.z, v(2, 3) = dot(forward, e);
    return v;
}

Mat4 OrbitCamera::projection(double aspect, double radius) const
{
    const double f = 1.0 / std::tan(std::clamp(fov, 1.0, 170.0) * kRadians / 2);
    const double r = std::max(radius, 1e-6);
    const double farPlane = distance + 4 * r;
    const double nearPlane = std::max(farPlane * 1e-4, distance - 4 * r);
    Mat4 p;
    p.m = {};
    p(0, 0) = f / std::max(aspect, 1e-9);
    p(1, 1) = f;
    p(2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
    p(2, 3) = 2 * farPlane * nearPlane / (nearPlane - farPlane);
    p(3, 2) = -1;
    return p;
}

void restBounds(const Model &model, Vec3 &center, double &radius)
{
    if (model.boundsRadius > 0) { // worked out when it was read
        center = model.boundsCenter;
        radius = model.boundsRadius;
        return;
    }
    // A model made by hand: where its nodes are.
    const std::vector<Mat4> g = model.restGlobals();
    constexpr double inf = std::numeric_limits<double>::infinity();
    Vec3 lo{inf, inf, inf}, hi{-inf, -inf, -inf};
    for (int n : model.order) {
        const Vec3 p = g[std::size_t(n)].translationPart();
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    center = lo.x <= hi.x ? (lo + hi) * 0.5 : Vec3{};
    radius = lo.x <= hi.x ? std::max(1e-6, length(hi - lo) * 0.5) : 1.0;
}

void computeRestBounds(Model &model)
{
    // The meshes as the rest pose puts them (skinned: a skin's bind space need not be where the model stands, when a
    // node above its bones turns or scales it).
    Pose rest;
    for (const Node &n : model.nodes) {
        rest.rotations.push_back(n.rotation);
        rest.translations.push_back(n.translation);
    }
    for (const MorphSlot &slot : model.morphs) {
        const Mesh &mesh = model.meshes[std::size_t(slot.mesh)];
        rest.morphs.push_back(std::size_t(slot.target) < mesh.targetWeights.size() ? mesh.targetWeights[std::size_t(slot.target)]
                                                                                : 0.0);
    }
    constexpr double inf = std::numeric_limits<double>::infinity();
    Vec3 lo{inf, inf, inf}, hi{-inf, -inf, -inf};
    for (const PosedPrimitive &posed : posedMeshes(model, rest))
        for (std::size_t v = 0; v + 2 < posed.positions.size(); v += 3) {
            const Vec3 p{posed.positions[v], posed.positions[v + 1], posed.positions[v + 2]};
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
    model.boundsRadius = 0;
    if (lo.x <= hi.x) {
        model.boundsCenter = (lo + hi) * 0.5;
        model.boundsRadius = std::max(1e-6, length(hi - lo) * 0.5);
    }
}

OrbitCamera framingCamera(const Model &model)
{
    OrbitCamera c;
    double radius = 1;
    restBounds(model, c.target, radius);
    c.distance = radius / std::sin(c.fov * kRadians / 2) * 1.05;
    return c;
}

Pose Pose::rest(const Model &model)
{
    Pose p;
    p.rotations.reserve(model.nodes.size());
    p.translations.reserve(model.nodes.size());
    for (const Node &n : model.nodes) {
        p.rotations.push_back(n.rotation);
        p.translations.push_back(n.translation);
    }
    for (const MorphSlot &slot : model.morphs) {
        const Mesh &mesh = model.meshes[std::size_t(slot.mesh)];
        p.morphs.push_back(std::size_t(slot.target) < mesh.targetWeights.size() ? mesh.targetWeights[std::size_t(slot.target)]
                                                                                : 0.0);
    }
    p.camera = framingCamera(model);
    return p;
}

std::vector<double> Pose::toNumbers() const
{
    std::vector<double> out;
    out.reserve(kCameraNumbers + rotations.size() * kNodeNumbers + morphs.size());
    out.insert(out.end(), {camera.yaw, camera.pitch, camera.distance, camera.target.x, camera.target.y, camera.target.z,
                           camera.fov});
    for (std::size_t i = 0; i < rotations.size(); ++i) {
        const Quat &q = rotations[i];
        const Vec3 &t = translations[i];
        out.insert(out.end(), {q.w, q.x, q.y, q.z, t.x, t.y, t.z});
    }
    out.insert(out.end(), morphs.begin(), morphs.end());
    return out;
}

Pose Pose::fromNumbers(const Model &model, const std::vector<double> &n)
{
    Pose p = rest(model);
    auto at = [&](std::size_t i, double fallback) { return i < n.size() && std::isfinite(n[i]) ? n[i] : fallback; };
    OrbitCamera &c = p.camera;
    c = {at(0, c.yaw), at(1, c.pitch), at(2, c.distance), {at(3, c.target.x), at(4, c.target.y), at(5, c.target.z)},
         at(6, c.fov)};
    std::size_t i = kCameraNumbers;
    for (std::size_t node = 0; node < model.nodes.size(); ++node, i += kNodeNumbers) {
        const Quat r = p.rotations[node];
        const Quat q{at(i, r.w), at(i + 1, r.x), at(i + 2, r.y), at(i + 3, r.z)};
        p.rotations[node] = q.norm() > 1e-12 ? q.normalized() : r;
        const Vec3 t = p.translations[node];
        p.translations[node] = {at(i + 4, t.x), at(i + 5, t.y), at(i + 6, t.z)};
    }
    for (std::size_t m = 0; m < p.morphs.size(); ++m, ++i)
        p.morphs[m] = at(i, p.morphs[m]);
    return p;
}

std::vector<Mat4> globalMatrices(const Model &model, const Pose &pose)
{
    std::vector<Mat4> g(model.nodes.size());
    for (int n : model.order) {
        const Node &node = model.nodes[std::size_t(n)];
        const Mat4 local = Mat4::trs(pose.translations[std::size_t(n)], pose.rotations[std::size_t(n)], node.scale);
        g[std::size_t(n)] = node.parent >= 0 ? g[std::size_t(node.parent)] * local : local;
    }
    return g;
}

Projection projectionOf(const Model &model, const OrbitCamera &camera, double width, double height)
{
    Vec3 center;
    double radius = 1;
    restBounds(model, center, radius);
    radius += length(center - camera.target); // what is far from the target too
    Projection p;
    p.viewProjection = camera.projection(width / std::max(height, 1.0), radius) * camera.view();
    p.width = width;
    p.height = height;
    return p;
}

Vec3 Projection::toPage(const Vec3 &p) const
{
    const Vec3 ndc = viewProjection.point(p);
    return {(ndc.x + 1) / 2 * width, (1 - ndc.y) / 2 * height, ndc.z};
}

Vec3 Projection::fromPage(double x, double y, const Vec3 &depthOf) const
{
    const double depth = viewProjection.point(depthOf).z;
    const Vec3 ndc{x / width * 2 - 1, 1 - y / height * 2, depth};
    // The inverse of the view-projection: a general 4×4 inverse by cofactors would do; the point is unprojected by
    // solving M·(p, 1) = w·(ndc, 1) for p and w.
    const Mat4 &m = viewProjection;
    // Rows: m(r,0..2)·p + m(r,3) = w·ndc_r for r = 0..2, and m(3,0..2)·p + m(3,3) = w.
    // So (m(r,·) − ndc_r·m(3,·))·p = ndc_r·m(3,3) − m(r,3).
    Mat3 a;
    Vec3 b;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            a(r, c) = m(r, c) - ndc[r] * m(3, c);
        const double rhs = ndc[r] * m(3, 3) - m(r, 3);
        (r == 0 ? b.x : r == 1 ? b.y : b.z) = rhs;
    }
    bool ok = false;
    const Mat3 inv = a.inverse(&ok);
    return ok ? inv * b : depthOf;
}

} // namespace mothion::rig3d
