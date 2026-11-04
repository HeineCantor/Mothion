#include "core/perspective/PerspectiveGrid.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::perspective {

namespace {

constexpr double kNearFraction = 0.02; // nothing closer to the eye than this much of the focal length

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

// A plane point in the camera's space: x right, y down, z away from the eye,
// the plane's center at z = 0 (the picture), the eye at z = -focal.
struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;
};

Vec3 toCamera(const Grid &g, double x, double y)
{
    const double ct = std::cos(radians(g.turn)), st = std::sin(radians(g.turn));
    const double x1 = ct * x - st * y, y1 = st * x + ct * y;
    const double cl = std::cos(radians(g.tilt)), sl = std::sin(radians(g.tilt));
    const double y2 = y1 * cl, z2 = -y1 * sl; // the top (y < 0) goes away
    const double cr = std::cos(radians(g.roll)), sr = std::sin(radians(g.roll));
    return {cr * x1 - sr * y2, sr * x1 + cr * y2, z2};
}

Point toImage(const Grid &g, const Vec3 &v, double focal)
{
    const double k = focal / (focal + v.z);
    return {g.centerX + v.x * k, g.centerY + v.y * k};
}

} // namespace

double focalLength(const Grid &grid)
{
    const double half = radians(std::clamp(grid.fov, 1.0, 179.0)) / 2.0;
    return grid.frameWidth / 2.0 / std::tan(half);
}

std::optional<Point> project(const Grid &grid, double x, double y)
{
    const double focal = focalLength(grid);
    const Vec3 v = toCamera(grid, x, y);
    if (focal + v.z < kNearFraction * focal)
        return std::nullopt;
    return toImage(grid, v, focal);
}

std::vector<Segment> gridSegments(const Grid &grid)
{
    std::vector<Segment> segments;
    const double focal = focalLength(grid);
    const double nearDepth = kNearFraction * focal;
    const int n = std::max(1, grid.cells);
    const double extent = n * grid.cell;
    // A line of the plane, clipped to what lies in front of the eye.
    auto add = [&](double x0, double y0, double x1, double y1) {
        Vec3 a = toCamera(grid, x0, y0), b = toCamera(grid, x1, y1);
        const double da = focal + a.z, db = focal + b.z;
        if (da < nearDepth && db < nearDepth)
            return;
        if (da < nearDepth || db < nearDepth) {
            const double t = (nearDepth - da) / (db - da);
            const Vec3 cut{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
            (da < nearDepth ? a : b) = cut;
        }
        segments.push_back({toImage(grid, a, focal), toImage(grid, b, focal)});
    };
    for (int i = -n; i <= n; ++i) {
        add(i * grid.cell, -extent, i * grid.cell, extent);
        add(-extent, i * grid.cell, extent, i * grid.cell);
    }
    return segments;
}

std::optional<Segment> horizonLine(const Grid &grid, double reach)
{
    const double sl = std::sin(radians(grid.tilt));
    if (std::abs(sl) < 1e-6)
        return std::nullopt;
    // Before the roll: level, where the plane's far direction vanishes.
    const double y = -focalLength(grid) * std::cos(radians(grid.tilt)) / sl;
    const double cr = std::cos(radians(grid.roll)), sr = std::sin(radians(grid.roll));
    const Point mid{grid.centerX - sr * y, grid.centerY + cr * y};
    return Segment{{mid.x - cr * reach, mid.y - sr * reach}, {mid.x + cr * reach, mid.y + sr * reach}};
}

Grid defaultGrid(int width, int height)
{
    Grid g;
    g.frameWidth = width;
    g.centerX = width / 2.0;
    g.centerY = height / 2.0;
    g.cell = std::max(8.0, width / 16.0);
    return g;
}

} // namespace mothion::perspective
