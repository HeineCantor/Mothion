#include "core/puppet/PuppetRig.h"

#include <algorithm>
#include <array>

namespace mothion::puppet {

namespace {

// Largest triangle as a fraction of the part's area, per density level:
// roughly 50, 250 and 500 triangles where size decides.
constexpr std::array<double, 3> kAreaFraction{0.02, 0.004, 0.002};
constexpr double kMinTriangleArea = 5.0; // px²

// Where `p` falls in mesh (rest → deformed by barycentric mapping of the
// triangle that contains it in `from`); nullopt if outside.
std::optional<Vec2> mapPoint(const TriMesh &mesh, const std::vector<Vec2> &from, const std::vector<Vec2> &to, Vec2 p)
{
    std::optional<Vec2> best;
    double bestOutside = 1e300;
    for (const auto &t : mesh.triangles) {
        const Vec2 a = from[std::size_t(t[0])], b = from[std::size_t(t[1])], c = from[std::size_t(t[2])];
        const auto w = barycentric(a, b, c, p, 1e300); // always coordinates; judge below
        if (!w)
            continue;
        const double outside = std::max({-(*w)[0], -(*w)[1], -(*w)[2], 0.0});
        if (outside < bestOutside) { // inside (0), or the closest triangle for points on the rim
            bestOutside = outside;
            best = to[std::size_t(t[0])] * (*w)[0] + to[std::size_t(t[1])] * (*w)[1] + to[std::size_t(t[2])] * (*w)[2];
            if (outside == 0.0)
                break;
        }
    }
    if (bestOutside > 1e-6)
        return std::nullopt;
    return best;
}

} // namespace

std::optional<std::array<double, 3>> barycentric(Vec2 a, Vec2 b, Vec2 c, Vec2 p, double tolerance)
{
    const double area = orient(a, b, c);
    if (std::abs(area) < 1e-12)
        return std::nullopt;
    const double wa = orient(b, c, p) / area, wb = orient(c, a, p) / area;
    const std::array<double, 3> w{wa, wb, 1.0 - wa - wb};
    if (w[0] < -tolerance || w[1] < -tolerance || w[2] < -tolerance)
        return std::nullopt;
    return w;
}

PuppetRig::PuppetRig(const Bitmask &shape, RigSettings settings)
    : m_shape(shape)
    , m_settings(settings)
{
    buildMeshes();
}

void PuppetRig::buildMeshes()
{
    m_meshes.clear();
    // At least 1 px: the outline runs through the centers of the edge pixels,
    // whose outer halves would otherwise fall off the mesh.
    const Bitmask grown = dilated(m_shape, std::max(1, m_settings.expand));
    for (const auto &contour : outerContours(grown)) {
        Mesh mesh;
        mesh.outline = simplifyContour(contour);
        if (mesh.outline.size() < 3 || polygonArea(mesh.outline) < kMinTriangleArea)
            continue;
        m_meshes.push_back(std::move(mesh));
        remesh(int(m_meshes.size()) - 1);
        if (m_meshes.back().rest.triangles.empty())
            m_meshes.pop_back();
    }
}

void PuppetRig::remesh(int index)
{
    Mesh &mesh = m_meshes[std::size_t(index)];
    std::vector<Vec2> pinPoints;
    for (const Pin &p : m_pins)
        if (p.mesh == index)
            pinPoints.push_back(p.rest);

    RefineOptions options;
    const int level = std::clamp(m_settings.density, 0, 2);
    options.maxArea = std::max(kAreaFraction[std::size_t(level)] * polygonArea(mesh.outline), kMinTriangleArea);
    TriMesh rest = triangulatePolygon(mesh.outline, pinPoints, options);

    // Carry the deformation over: each new vertex goes where the old mesh took it.
    std::vector<Vec2> deformed(rest.vertices.size());
    for (std::size_t v = 0; v < rest.vertices.size(); ++v) {
        const auto moved = mesh.rest.triangles.empty()
                               ? std::nullopt
                               : mapPoint(mesh.rest, mesh.rest.vertices, mesh.deformed, rest.vertices[v]);
        deformed[v] = moved.value_or(rest.vertices[v]);
    }
    mesh.rest = std::move(rest);
    mesh.deformed = std::move(deformed);

    // Pins onto their vertices (the triangulation kept them as points).
    std::vector<int> handles;
    std::vector<Vec2> targets;
    for (Pin &p : m_pins) {
        if (p.mesh != index)
            continue;
        double best = 1e300;
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v)
            if (const double d = length2(mesh.rest.vertices[v] - p.rest); d < best) {
                best = d;
                p.vertex = int(v);
            }
        if (std::find(handles.begin(), handles.end(), p.vertex) == handles.end()) {
            handles.push_back(p.vertex);
            targets.push_back(p.position);
        }
    }
    mesh.deformer = std::make_unique<ArapDeformer>(mesh.rest);
    mesh.deformer->setHandles(handles);
    if (!handles.empty())
        mesh.deformer->solve(mesh.deformed, targets, 4);
}

std::optional<std::pair<int, Vec2>> PuppetRig::restPointUnder(Vec2 at) const
{
    for (std::size_t m = 0; m < m_meshes.size(); ++m) {
        const Mesh &mesh = m_meshes[m];
        if (const auto rest = mapPoint(mesh.rest, mesh.deformed, mesh.rest.vertices, at))
            return std::make_pair(int(m), *rest);
    }
    return std::nullopt;
}

std::optional<int> PuppetRig::addPin(Vec2 at)
{
    const auto under = restPointUnder(at);
    if (!under)
        return std::nullopt;
    m_pins.push_back({under->first, -1, under->second, at});
    remesh(under->first);
    return int(m_pins.size()) - 1;
}

void PuppetRig::removePin(int pin)
{
    if (pin < 0 || pin >= int(m_pins.size()))
        return;
    const int mesh = m_pins[std::size_t(pin)].mesh;
    m_pins.erase(m_pins.begin() + pin);
    remesh(mesh);
}

void PuppetRig::movePin(int pin, Vec2 to, int iterations)
{
    if (pin < 0 || pin >= int(m_pins.size()))
        return;
    m_pins[std::size_t(pin)].position = to;
    const int index = m_pins[std::size_t(pin)].mesh;
    Mesh &mesh = m_meshes[std::size_t(index)];
    std::vector<Vec2> targets;
    std::vector<int> seen;
    for (const Pin &p : m_pins)
        if (p.mesh == index && std::find(seen.begin(), seen.end(), p.vertex) == seen.end()) {
            seen.push_back(p.vertex);
            targets.push_back(p.position);
        }
    mesh.deformer->solve(mesh.deformed, targets, iterations);
}

std::optional<int> PuppetRig::pinAt(Vec2 at, double radius) const
{
    std::optional<int> best;
    double bestDistance = radius * radius;
    for (std::size_t p = 0; p < m_pins.size(); ++p)
        if (const double d = length2(m_pins[p].position - at); d <= bestDistance) {
            bestDistance = d;
            best = int(p);
        }
    return best;
}

void PuppetRig::setSettings(RigSettings settings)
{
    if (settings.density == m_settings.density && settings.expand == m_settings.expand)
        return;
    m_settings = settings;
    // Keep the old meshes to carry the deformation over, then rebuild.
    std::vector<Mesh> old = std::move(m_meshes);
    std::vector<Pin> pins = std::move(m_pins);
    m_pins.clear();
    buildMeshes();
    for (Pin p : pins) {
        // The pin's rest point on the new meshes.
        for (std::size_t m = 0; m < m_meshes.size(); ++m)
            if (pointInPolygon(m_meshes[m].outline, p.rest)) {
                p.mesh = int(m);
                m_pins.push_back(p);
                break;
            }
    }
    for (std::size_t m = 0; m < m_meshes.size(); ++m) {
        // Seed the deformation from the old meshes before re-solving.
        Mesh &mesh = m_meshes[m];
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v)
            for (const Mesh &o : old)
                if (const auto moved = mapPoint(o.rest, o.rest.vertices, o.deformed, mesh.rest.vertices[v])) {
                    mesh.deformed[v] = *moved;
                    break;
                }
        remesh(int(m));
    }
}

} // namespace mothion::puppet
