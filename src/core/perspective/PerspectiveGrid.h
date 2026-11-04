#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace mothion::perspective {

// Space as the camera sees it: x to the right, y down, z away from the eye,
// in document pixels. The eye is at z = -focal, the picture (the document)
// at z = 0, where one unit is one document pixel.
struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;
    bool operator==(const Vec3 &) const = default;
};

Vec3 operator+(const Vec3 &a, const Vec3 &b);
Vec3 operator-(const Vec3 &a, const Vec3 &b);
Vec3 operator*(const Vec3 &v, double k);
double dot(const Vec3 &a, const Vec3 &b);
Vec3 cross(const Vec3 &a, const Vec3 &b);

// A rotation as a unit quaternion.
struct Rotation
{
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
    bool operator==(const Rotation &) const = default;
};

// `a` after `b`: rotating by the result is rotating by b, then by a.
Rotation operator*(const Rotation &a, const Rotation &b);
Vec3 rotate(const Rotation &r, const Vec3 &v);
// Around `axis` (any length) by `degrees`, right-handed.
Rotation axisAngle(const Vec3 &axis, double degrees);
Rotation normalized(const Rotation &r);
// The rotation of these angles, in degrees: `turn` around the plane's normal
// (the grid spins on it), then `tilt` (its top goes away from the viewer),
// then `roll` around the line of sight.
Rotation fromAngles(double turn, double tilt, double roll);

// A flat plane with a square grid on it, seen through a pinhole camera: what
// a perspective layer shows, a guide to draw in perspective.
//
// In its own space the plane is z = 0, its grid lines along x and y, its
// center at the origin. `orientation` turns it into the camera's space and
// `position` puts its center there: at (0, 0, 0) it lies on the picture,
// centered on where the camera looks. The camera looks at (originX,
// originY) of the document; its horizontal field of view `fov` spans
// `frameWidth` px (the document's width when the layer was made).
struct Grid
{
    Rotation orientation = fromAngles(30.0, 60.0, 0.0);
    Vec3 position;
    double fov = 60.0;
    double frameWidth = 1920.0;
    double originX = 960.0;
    double originY = 540.0;
    double cell = 120.0; // grid cell, plane units
    int cells = 8;       // per side of the center: 2 × cells across
    std::uint32_t color = 0xff3a7bd5; // ARGB, not premultiplied
    bool horizon = true;              // also the plane's horizon line

    bool operator==(const Grid &) const = default;
};

struct Point
{
    double x = 0.0, y = 0.0;
};

struct Segment
{
    Point a, b;
};

// Distance from the eye to the picture, px.
double focalLength(const Grid &grid);

// The plane's point (x, y, z) of its own space, in the camera's space.
Vec3 toCamera(const Grid &grid, const Vec3 &planePoint);
// The plane's own axes (unit vectors), in the camera's space: x and y along
// its grid lines, z its normal.
Vec3 axis(const Grid &grid, const Vec3 &planeAxis);

// Where a point of the camera's space is drawn, document px; nothing if it
// lies behind the eye or too close to it.
std::optional<Point> projectCamera(const Grid &grid, const Vec3 &cameraPoint);
// Where the plane's point (x, y) is drawn.
std::optional<Point> project(const Grid &grid, double x, double y);

// The grid's lines drawn on the document: the parts in front of the eye.
std::vector<Segment> gridSegments(const Grid &grid);

// The plane's horizon (its vanishing line), as a segment `reach` px long each
// way of the point of it nearest to where the camera looks; nothing when the
// plane faces the viewer (the horizon is then at infinity).
std::optional<Segment> horizonLine(const Grid &grid, double reach);

// A grid for a document of `width` × `height` px: centered, a cell 1/16 of
// the width.
Grid defaultGrid(int width, int height);

} // namespace mothion::perspective
