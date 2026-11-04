#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace mothion::perspective {

// A flat plane with a square grid on it, seen through a pinhole camera: what
// a perspective layer shows, a guide to draw in perspective. Before any
// rotation the plane faces the viewer, its center at (centerX, centerY) of
// the document, where one unit of the plane is one document pixel.
// The rotations, in degrees, apply in this order:
//  - turn: around the plane's own normal (the grid spins on the plane);
//  - tilt: the top of the plane goes away (0 = facing, towards 90 = edge on);
//  - roll: the whole picture around the center, in the image.
// The camera's horizontal field of view `fov` spans `frameWidth` px (the
// document's width when the layer was made).
struct Grid
{
    double turn = 30.0;
    double tilt = 60.0;
    double roll = 0.0;
    double fov = 60.0;
    double frameWidth = 1920.0;
    double centerX = 960.0;
    double centerY = 540.0;
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

// Distance from the eye to the picture, px: the plane's center lies there.
double focalLength(const Grid &grid);

// Where the plane's point (x, y) is drawn, document px; nothing if it lies
// behind the eye or too close to it.
std::optional<Point> project(const Grid &grid, double x, double y);

// The grid's lines drawn on the document: the parts in front of the eye.
std::vector<Segment> gridSegments(const Grid &grid);

// The plane's horizon (its vanishing line), as a segment `reach` px long
// each way of the point straight above the center; nothing when the plane
// faces the viewer (the horizon is then at infinity).
std::optional<Segment> horizonLine(const Grid &grid, double reach);

// A grid for a document of `width` × `height` px: centered, a cell 1/16 of
// the width.
Grid defaultGrid(int width, int height);

} // namespace mothion::perspective
