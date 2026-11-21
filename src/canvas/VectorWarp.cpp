#include "canvas/VectorWarp.h"

#include "canvas/VectorRender.h"
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

    // The linear part of the affine map of a triangle: a vector (x, y) at rest becomes (a x + b y, c x + d y).
    struct Linear
    {
        double a = 1, b = 0, c = 0, d = 1;
        Vec2 apply(Vec2 v) const { return {a * v.x + b * v.y, c * v.x + d * v.y}; }
        double det() const { return a * d - b * c; }
    };

    // Where the rest point `p` is now, and the linear part of the map there (the identity outside the meshes).
    Vec2 map(Vec2 p, Linear &linear) const
    {
        linear = {};
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
                // Rest edges e1 = b − a, e2 = c − a go to f1 = db − da, f2 = dc − da: L = F·E⁻¹.
                const Vec2 e1 = b - a, e2 = c - a, f1 = db - da, f2 = dc - da;
                const double det = e1.x * e2.y - e2.x * e1.y;
                if (std::abs(det) > 1e-12) {
                    const double i11 = e2.y / det, i12 = -e2.x / det, i21 = -e1.y / det, i22 = e1.x / det;
                    linear = {f1.x * i11 + f2.x * i21, f1.x * i12 + f2.x * i22, f1.y * i11 + f2.y * i21,
                              f1.y * i12 + f2.y * i22};
                }
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
        if (stroke.fill) { // a shape of colour: its outlines, closing edges too, bent point by point
            doc::VectorStroke moved = stroke;
            moved.points.clear();
            moved.outlines.clear();
            for (const std::vector<QPointF> &outline : fillOutlines(stroke)) {
                if (!moved.points.empty())
                    moved.outlines.push_back(int(moved.points.size()));
                for (std::size_t i = 0; i < outline.size(); ++i) {
                    const QPointF a = outline[i], b = outline[(i + 1) % outline.size()];
                    const int parts = std::max(1, int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / kMaxSegment)));
                    for (int k = 0; k < parts; ++k) {
                        MeshIndex::Linear unused;
                        const Vec2 to = index.map({a.x() + (b.x() - a.x()) * k / parts, a.y() + (b.y() - a.y()) * k / parts}, unused);
                        brush::BrushInput in;
                        in.x = to.x;
                        in.y = to.y;
                        moved.points.push_back(in);
                    }
                }
            }
            out.push_back(std::move(moved));
            continue;
        }
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
        // The width goes as far as the mesh stretched across the line, not along it: a band of width w becomes
        // w·det(L)/|L·t| wide (t the line's direction at rest), so the edge of a colour under the line moves as the line's
        // edge does and stays covered. Smoothed along the line, so that it does not step from one triangle to the next.
        const std::size_t n = moved.points.size();
        std::vector<double> across(n, 1.0);
        std::vector<double> turn(n, 0.0); // degrees the mesh turned there (the rotation of its polar decomposition)
        std::vector<Vec2> to(n);
        for (std::size_t i = 0; i < n; ++i) {
            const brush::BrushInput &before = moved.points[i > 0 ? i - 1 : i];
            const brush::BrushInput &after = moved.points[i + 1 < n ? i + 1 : i];
            MeshIndex::Linear linear;
            to[i] = index.map({moved.points[i].x, moved.points[i].y}, linear);
            // Where the line has no direction, the turn of the mesh there (its polar decomposition's rotation).
            turn[i] = std::atan2(linear.c - linear.b, linear.a + linear.d) * 180.0 / std::numbers::pi;
            Vec2 t{after.x - before.x, after.y - before.y};
            const double length = std::hypot(t.x, t.y);
            if (length > 1e-9) {
                t = {t.x / length, t.y / length};
                const Vec2 along = linear.apply(t);
                const double stretch = std::hypot(along.x, along.y);
                if (stretch > 1e-9) {
                    across[i] = std::clamp(std::abs(linear.det()) / stretch, 0.25, 4.0);
                    // The tip turns as the line did there, so it meets the line at the angle it met it before: the
                    // width across is then the old one times `across`, as in the preview, however unevenly the mesh
                    // was stretched.
                    turn[i] = (std::atan2(along.y, along.x) - std::atan2(t.y, t.x)) * 180.0 / std::numbers::pi;
                }
            }
        }
        constexpr int kSmooth = 4; // points each side (about 12 px)
        for (std::size_t i = 0; i < n; ++i) {
            double sum = 0, turned = 0;
            int count = 0;
            for (std::ptrdiff_t k = std::ptrdiff_t(i) - kSmooth; k <= std::ptrdiff_t(i) + kSmooth; ++k)
                if (k >= 0 && k < std::ptrdiff_t(n)) {
                    sum += across[std::size_t(k)];
                    turned += std::remainder(turn[std::size_t(k)] - turn[i], 360.0); // as near to this one as can be
                    ++count;
                }
            brush::BrushInput &in = moved.points[i];
            in.x = to[i].x;
            in.y = to[i].y;
            in.sizeScale *= sum / count;
            // A tip held at an angle (a flat brush) turns with the line, as the pixels of the warp's preview do.
            in.tipAngle = std::remainder(in.tipAngle + turn[i] + turned / count, 360.0);
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
