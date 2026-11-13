#include "canvas/VectorWarp.h"

#include "core/brush/DabGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

using puppet::Vec2;

constexpr double kMaxSegment = 3.0; // px: longer segments get points in between
constexpr double kCell = 16.0;      // px: the grid that finds the triangle holding a point

// The triangles of all the meshes, found by place.
class MeshIndex
{
public:
    explicit MeshIndex(const puppet::PuppetRig &rig)
        : m_rig(rig)
    {
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (const auto &mesh : rig.meshes())
            for (const Vec2 &v : mesh.rest.vertices) {
                minX = std::min(minX, v.x);
                minY = std::min(minY, v.y);
                maxX = std::max(maxX, v.x);
                maxY = std::max(maxY, v.y);
            }
        if (minX > maxX)
            return;
        m_x0 = minX;
        m_y0 = minY;
        m_columns = int((maxX - minX) / kCell) + 1;
        m_rows = int((maxY - minY) / kCell) + 1;
        m_cells.resize(std::size_t(m_columns) * std::size_t(m_rows));
        for (std::size_t m = 0; m < rig.meshes().size(); ++m) {
            const auto &mesh = rig.meshes()[m];
            for (std::size_t t = 0; t < mesh.rest.triangles.size(); ++t) {
                const auto &tri = mesh.rest.triangles[t];
                double lx = 1e300, ly = 1e300, hx = -1e300, hy = -1e300;
                for (int v : tri) {
                    const Vec2 p = mesh.rest.vertices[std::size_t(v)];
                    lx = std::min(lx, p.x);
                    ly = std::min(ly, p.y);
                    hx = std::max(hx, p.x);
                    hy = std::max(hy, p.y);
                }
                for (int cy = column(ly - m_y0, m_rows); cy <= column(hy - m_y0, m_rows); ++cy)
                    for (int cx = column(lx - m_x0, m_columns); cx <= column(hx - m_x0, m_columns); ++cx)
                        m_cells[std::size_t(cy) * std::size_t(m_columns) + std::size_t(cx)].push_back({int(m), int(t)});
            }
        }
    }

    // Where the rest point `p` is now, and how much the brush grows there.
    Vec2 map(Vec2 p, double &scale) const
    {
        scale = 1.0;
        if (!m_cells.empty()) {
            const int cx = column(p.x - m_x0, m_columns), cy = column(p.y - m_y0, m_rows);
            for (const auto &id : m_cells[std::size_t(cy) * std::size_t(m_columns) + std::size_t(cx)]) {
                const auto &mesh = m_rig.meshes()[std::size_t(id.mesh)];
                const auto &tri = mesh.rest.triangles[std::size_t(id.triangle)];
                const Vec2 a = mesh.rest.vertices[std::size_t(tri[0])], b = mesh.rest.vertices[std::size_t(tri[1])],
                           c = mesh.rest.vertices[std::size_t(tri[2])];
                const auto w = puppet::barycentric(a, b, c, p, 1e-6);
                if (!w)
                    continue;
                const Vec2 da = mesh.deformed[std::size_t(tri[0])], db = mesh.deformed[std::size_t(tri[1])],
                           dc = mesh.deformed[std::size_t(tri[2])];
                const double restArea = std::abs(puppet::orient(a, b, c));
                const double nowArea = std::abs(puppet::orient(da, db, dc));
                if (restArea > 1e-12)
                    scale = std::clamp(std::sqrt(nowArea / restArea), 0.2, 5.0);
                return da * (*w)[0] + db * (*w)[1] + dc * (*w)[2];
            }
        }
        // Outside the meshes: with the vertex nearest to it.
        double best = 1e300;
        Vec2 shift;
        for (const auto &mesh : m_rig.meshes())
            for (std::size_t v = 0; v < mesh.rest.vertices.size(); ++v) {
                const double d = puppet::length2(mesh.rest.vertices[v] - p);
                if (d < best) {
                    best = d;
                    shift = mesh.deformed[v] - mesh.rest.vertices[v];
                }
            }
        return p + shift;
    }

private:
    struct TriangleId
    {
        int mesh, triangle;
    };
    static int column(double offset, int count) { return std::clamp(int(offset / kCell), 0, count - 1); }

    const puppet::PuppetRig &m_rig;
    double m_x0 = 0, m_y0 = 0;
    int m_columns = 0, m_rows = 0;
    std::vector<std::vector<TriangleId>> m_cells;
};

} // namespace

std::vector<doc::VectorStroke> warpedStrokes(const std::vector<doc::VectorStroke> &strokes,
                                             const puppet::PuppetRig &rig)
{
    const MeshIndex index(rig);
    std::vector<doc::VectorStroke> out;
    out.reserve(strokes.size());
    for (const doc::VectorStroke &stroke : strokes) {
        doc::VectorStroke moved = stroke;
        moved.points.clear();
        for (std::size_t i = 0; i < stroke.points.size(); ++i) {
            if (i > 0) {
                const brush::BrushInput &a = stroke.points[i - 1], &b = stroke.points[i];
                const int parts = int(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / kMaxSegment));
                for (int k = 1; k < parts; ++k)
                    moved.points.push_back(brush::DabGenerator::interpolate(a, b, double(k) / parts));
            }
            moved.points.push_back(stroke.points[i]);
        }
        for (brush::BrushInput &in : moved.points) {
            double scale = 1.0;
            const Vec2 to = index.map({in.x, in.y}, scale);
            in.x = to.x;
            in.y = to.y;
            in.sizeScale *= scale;
        }
        for (std::size_t i = 0; i < moved.points.size(); ++i) {
            const brush::BrushInput &before = moved.points[i > 0 ? i - 1 : i];
            const brush::BrushInput &after = moved.points[i + 1 < moved.points.size() ? i + 1 : i];
            if (before.x != after.x || before.y != after.y)
                moved.points[i].direction =
                    std::atan2(after.y - before.y, after.x - before.x) * 180.0 / std::numbers::pi;
        }
        out.push_back(std::move(moved));
    }
    return out;
}

} // namespace mothion::canvas
