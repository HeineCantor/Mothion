#include "core/perspective/PerspectiveGrid.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::perspective {

namespace {

constexpr double kNearFraction = 0.02; // nothing closer to the eye than this much of the focal length

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

} // namespace

Vec3 operator+(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3 &v, double k) { return {v.x * k, v.y * k, v.z * k}; }
double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Rotation operator*(const Rotation &a, const Rotation &b)
{
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Vec3 rotate(const Rotation &r, const Vec3 &v)
{
    // v + 2w (q × v) + 2 q × (q × v), q the vector part.
    const Vec3 q{r.x, r.y, r.z};
    const Vec3 t = cross(q, v) * 2.0;
    return v + t * r.w + cross(q, t);
}

Rotation axisAngle(const Vec3 &axis, double degrees)
{
    const double length = std::sqrt(dot(axis, axis));
    if (length < 1e-12)
        return {};
    const double half = radians(degrees) / 2.0, s = std::sin(half) / length;
    return {std::cos(half), axis.x * s, axis.y * s, axis.z * s};
}

Rotation normalized(const Rotation &r)
{
    const double length = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
    if (length < 1e-12)
        return {};
    if (std::abs(length - 1.0) < 1e-12)
        return r; // already: left exactly as it is
    return {r.w / length, r.x / length, r.y / length, r.z / length};
}

Rotation fromAngles(double turn, double tilt, double roll)
{
    // Tilting is a rotation around x by -tilt: y = y cos + z sin, z = -y sin + z cos,
    // so a point above the center (y < 0) goes away (z > 0).
    return axisAngle({0, 0, 1}, roll) * axisAngle({1, 0, 0}, -tilt) * axisAngle({0, 0, 1}, turn);
}

double focalLength(const Grid &grid)
{
    const double half = radians(std::clamp(grid.fov, 1.0, 179.0)) / 2.0;
    return grid.frameWidth / 2.0 / std::tan(half);
}

Vec3 toCamera(const Grid &grid, const Vec3 &planePoint)
{
    return rotate(grid.orientation, planePoint) + grid.position;
}

Vec3 axis(const Grid &grid, const Vec3 &planeAxis)
{
    return rotate(grid.orientation, planeAxis);
}

std::optional<Point> projectCamera(const Grid &grid, const Vec3 &v)
{
    const double focal = focalLength(grid);
    const double depth = focal + v.z;
    if (depth < kNearFraction * focal)
        return std::nullopt;
    const double k = focal / depth;
    return Point{grid.originX + v.x * k, grid.originY + v.y * k};
}

std::optional<Point> project(const Grid &grid, double x, double y)
{
    return projectCamera(grid, toCamera(grid, {x, y, 0.0}));
}

std::vector<Segment> gridSegments(const Grid &grid)
{
    std::vector<Segment> segments;
    const double focal = focalLength(grid);
    // Cut a hair in front of the limit projectCamera() holds to: a point cut exactly on it can round to
    // just behind it, and the whole line would then be lost.
    const double nearDepth = kNearFraction * focal * (1.0 + 1e-6);
    const int n = std::max(1, grid.cells);
    const double extent = n * grid.cell;
    // A line of the plane, clipped to what lies in front of the eye.
    auto add = [&](double x0, double y0, double x1, double y1) {
        Vec3 a = toCamera(grid, {x0, y0, 0.0}), b = toCamera(grid, {x1, y1, 0.0});
        const double da = focal + a.z, db = focal + b.z;
        if (da < nearDepth && db < nearDepth)
            return;
        if (da < nearDepth || db < nearDepth) {
            const double t = (nearDepth - da) / (db - da);
            (da < nearDepth ? a : b) = a + (b - a) * t;
        }
        const auto pa = projectCamera(grid, a), pb = projectCamera(grid, b);
        if (pa && pb)
            segments.push_back({*pa, *pb});
    };
    for (int i = -n; i <= n; ++i) {
        add(i * grid.cell, -extent, i * grid.cell, extent);
        add(-extent, i * grid.cell, extent, i * grid.cell);
    }
    return segments;
}

std::optional<Segment> horizonLine(const Grid &grid, double reach)
{
    // The directions of the plane vanish on the line n · (X - origin, f) = 0.
    const Vec3 n = axis(grid, {0, 0, 1});
    const double planar = n.x * n.x + n.y * n.y;
    if (planar < 1e-12)
        return std::nullopt;
    const double k = -n.z * focalLength(grid) / planar;
    const Point mid{grid.originX + n.x * k, grid.originY + n.y * k};
    const double length = std::sqrt(planar);
    const double dx = -n.y / length, dy = n.x / length;
    return Segment{{mid.x - dx * reach, mid.y - dy * reach}, {mid.x + dx * reach, mid.y + dy * reach}};
}

Grid defaultGrid(int width, int height)
{
    Grid g;
    g.frameWidth = width;
    g.originX = width / 2.0;
    g.originY = height / 2.0;
    g.cell = std::max(8.0, width / 16.0);
    return g;
}

} // namespace mothion::perspective
