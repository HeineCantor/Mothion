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

// The corners of the shape's edge pixels, in document coordinates: what the
// mesh must contain for every drawn pixel to come along.
std::vector<Vec2> edgeCorners(const Bitmask &m)
{
    std::vector<Vec2> corners;
    for (int y = 0; y < m.height; ++y)
        for (int x = 0; x < m.width; ++x) {
            if (!m.at(x, y) || (m.at(x - 1, y) && m.at(x + 1, y) && m.at(x, y - 1) && m.at(x, y + 1)))
                continue;
            const double px = m.originX + x, py = m.originY + y;
            corners.insert(corners.end(), {{px, py}, {px + 1, py}, {px, py + 1}, {px + 1, py + 1}});
        }
    return corners;
}

bool coversAll(const std::vector<std::vector<Vec2>> &outlines, const std::vector<Vec2> &points)
{
    struct Box
    {
        double x0, y0, x1, y1;
    };
    std::vector<Box> boxes;
    for (const auto &o : outlines) {
        Box b{1e300, 1e300, -1e300, -1e300};
        for (const Vec2 &p : o)
            b = {std::min(b.x0, p.x), std::min(b.y0, p.y), std::max(b.x1, p.x), std::max(b.y1, p.y)};
        boxes.push_back(b);
    }
    for (const Vec2 &p : points) {
        bool inside = false;
        for (std::size_t i = 0; i < outlines.size() && !inside; ++i)
            inside = p.x >= boxes[i].x0 && p.x <= boxes[i].x1 && p.y >= boxes[i].y0 && p.y <= boxes[i].y1
                     && pointInPolygon(outlines[i], p);
        if (!inside)
            return false;
    }
    return true;
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
    const int expand = std::max(1, m_settings.expand);
    const Bitmask grown = dilated(m_shape, expand);
    const std::vector<std::vector<Vec2>> contours = outerContours(grown);

    // Simplify the outlines as far as the margin allows: they may cut into
    // the expansion, never into the drawing. If an edge pixel of the drawing
    // ends up outside, try again with half the tolerance.
    const std::vector<Vec2> mustCover = edgeCorners(m_shape);
    std::vector<std::vector<Vec2>> outlines;
    for (double tolerance = std::max(0.5, expand - 1.0);; tolerance *= 0.5) {
        outlines.clear();
        for (const auto &contour : contours)
            outlines.push_back(simplifyContour(contour, tolerance));
        if (tolerance < 0.2 || coversAll(outlines, mustCover))
            break;
    }

    for (std::vector<Vec2> &outline : outlines) {
        Mesh mesh;
        mesh.outline = std::move(outline);
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
    for (Pin &p : m_pins) {
        if (p.mesh != index)
            continue;
        double best = 1e300;
        for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v)
            if (const double d = length2(mesh.rest.vertices[v] - p.rest); d < best) {
                best = d;
                p.vertex = int(v);
            }
    }
    std::vector<int> handles;
    std::vector<Vec2> targets;
    pinHandles(index, handles, targets);
    mesh.deformer = std::make_unique<ArapDeformer>(mesh.rest);
    mesh.deformer->setHandles(handles);
    if (!handles.empty())
        mesh.deformer->solve(mesh.deformed, targets, 4);
}

void PuppetRig::pinHandles(int mesh, std::vector<int> &handles, std::vector<Vec2> &targets) const
{
    handles.clear();
    targets.clear();
    for (const Pin &p : m_pins)
        if (p.mesh == mesh && std::find(handles.begin(), handles.end(), p.vertex) == handles.end()) {
            handles.push_back(p.vertex);
            targets.push_back(p.position);
        }
}

std::size_t PuppetRig::nearestVertex(int mesh, Vec2 at) const
{
    const Mesh &m = m_meshes[std::size_t(mesh)];
    std::size_t nearest = 0;
    double best = 1e300;
    for (std::size_t v = 0; v < m.deformed.size(); ++v)
        if (const double d = length2(m.deformed[v] - at); d < best) {
            best = d;
            nearest = v;
        }
    return nearest;
}

std::optional<PuppetRig::VertexRef> PuppetRig::vertexAt(Vec2 at) const
{
    const auto under = restPointUnder(at);
    if (!under)
        return std::nullopt;
    return VertexRef{under->first, int(nearestVertex(under->first, at))};
}

std::optional<Vec2> PuppetRig::localGrabPoint(Vec2 at) const
{
    const auto v = vertexAt(at);
    return v ? std::optional<Vec2>(position(*v)) : std::nullopt;
}

std::vector<Vec2> PuppetRig::localDragPoints() const
{
    std::vector<Vec2> points;
    if (m_local)
        for (const LocalDrag::Part &part : m_local->parts)
            for (const LocalDrag::Grabbed &g : part.grabbed)
                points.push_back(m_meshes[std::size_t(part.mesh)].deformed[std::size_t(g.vertex)]);
    return points;
}

bool PuppetRig::beginLocalDrag(Vec2 at, double radius)
{
    const auto v = vertexAt(at);
    return v && beginLocalDrag({*v}, at, radius);
}

bool PuppetRig::beginLocalDrag(const std::vector<VertexRef> &grabbed, Vec2 at, double radius)
{
    endLocalDrag();
    LocalDrag drag;
    drag.pressedAt = at;
    for (std::size_t m = 0; m < m_meshes.size(); ++m) {
        std::vector<int> mine;
        for (const VertexRef &g : grabbed)
            if (g.mesh == int(m) && g.vertex >= 0 && g.vertex < int(m_meshes[m].deformed.size())
                && std::find(mine.begin(), mine.end(), g.vertex) == mine.end())
                mine.push_back(g.vertex);
        if (mine.empty())
            continue;
        Mesh &mesh = m_meshes[m];
        auto inADisc = [&](std::size_t v) {
            for (int g : mine)
                if (length(mesh.deformed[v] - mesh.deformed[std::size_t(g)]) <= radius)
                    return true;
            return false;
        };

        // Held: the pins, and every vertex out of the discs, where they are now.
        LocalDrag::Part part;
        part.mesh = int(m);
        std::vector<int> handles;
        pinHandles(part.mesh, handles, part.targets);
        for (std::size_t v = 0; v < mesh.deformed.size(); ++v)
            if (!inADisc(v) && std::find(handles.begin(), handles.end(), int(v)) == handles.end()) {
                handles.push_back(int(v));
                part.targets.push_back(mesh.deformed[v]);
            }
        for (int g : mine) {
            // A pinned vertex is already a handle: it moves, its pin with it.
            auto slot = std::find(handles.begin(), handles.end(), g);
            if (slot == handles.end()) {
                handles.push_back(g);
                part.targets.push_back(mesh.deformed[std::size_t(g)]);
                slot = handles.end() - 1;
            }
            part.grabbed.push_back({g, std::size_t(slot - handles.begin()), mesh.deformed[std::size_t(g)]});
        }
        mesh.deformer->setHandles(handles);
        drag.parts.push_back(std::move(part));
    }
    if (drag.parts.empty())
        return false;
    m_local = std::move(drag);
    return true;
}

void PuppetRig::localDragTo(Vec2 to, int iterations)
{
    if (!m_local)
        return;
    const Vec2 travel = to - m_local->pressedAt;
    for (LocalDrag::Part &part : m_local->parts) {
        for (const LocalDrag::Grabbed &g : part.grabbed) {
            const Vec2 target = g.start + travel;
            part.targets[g.handle] = target;
            for (Pin &p : m_pins) // a grabbed pin goes along
                if (p.mesh == part.mesh && p.vertex == g.vertex)
                    p.position = target;
        }
        Mesh &mesh = m_meshes[std::size_t(part.mesh)];
        mesh.deformer->solve(mesh.deformed, part.targets, iterations);
    }
}

void PuppetRig::endLocalDrag()
{
    if (!m_local)
        return;
    const LocalDrag drag = std::move(*m_local);
    m_local.reset();
    for (const LocalDrag::Part &part : drag.parts) {
        std::vector<int> handles;
        std::vector<Vec2> targets;
        pinHandles(part.mesh, handles, targets);
        m_meshes[std::size_t(part.mesh)].deformer->setHandles(handles);
    }
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

void PuppetRig::movePins(const std::vector<int> &pins, const std::vector<Vec2> &to, int iterations)
{
    std::vector<int> meshes;
    for (std::size_t i = 0; i < pins.size() && i < to.size(); ++i) {
        if (pins[i] < 0 || pins[i] >= int(m_pins.size()))
            continue;
        Pin &p = m_pins[std::size_t(pins[i])];
        p.position = to[i];
        if (std::find(meshes.begin(), meshes.end(), p.mesh) == meshes.end())
            meshes.push_back(p.mesh);
    }
    for (int index : meshes) {
        std::vector<int> handles;
        std::vector<Vec2> targets;
        pinHandles(index, handles, targets);
        Mesh &mesh = m_meshes[std::size_t(index)];
        mesh.deformer->solve(mesh.deformed, targets, iterations);
    }
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

bool PuppetRig::isDeformed() const
{
    for (const Mesh &m : m_meshes)
        for (std::size_t v = 0; v < m.deformed.size(); ++v)
            if (length2(m.deformed[v] - m.rest.vertices[v]) > 1e-6) // remeshing leaves rounding noise
                return true;
    return false;
}

PuppetRig::State PuppetRig::state() const
{
    State s;
    for (const Mesh &m : m_meshes)
        s.meshes.push_back({m.outline, m.rest, m.deformed});
    s.pins = m_pins;
    return s;
}

void PuppetRig::restore(const State &state)
{
    m_local.reset();
    m_meshes.clear();
    m_pins = state.pins;
    for (const State::MeshState &saved : state.meshes) {
        Mesh m;
        m.outline = saved.outline;
        m.rest = saved.rest;
        m.deformed = saved.deformed;
        m.deformer = std::make_unique<ArapDeformer>(m.rest);
        m_meshes.push_back(std::move(m));
    }
    for (std::size_t i = 0; i < m_meshes.size(); ++i) {
        std::vector<int> handles;
        std::vector<Vec2> targets;
        pinHandles(int(i), handles, targets);
        m_meshes[i].deformer->setHandles(handles);
    }
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
