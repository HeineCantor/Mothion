#pragma once

#include "core/puppet/Geometry.h"

#include <array>
#include <vector>

namespace mothion::puppet {

// A triangle mesh in document coordinates. Triangles list vertex indices,
// counter-clockwise in the y-up sense (orient() > 0).
struct TriMesh
{
    std::vector<Vec2> vertices;
    std::vector<std::array<int, 3>> triangles;

    double area() const;
};

struct RefineOptions
{
    double minAngleDegrees = 20.0; // smallest angle wanted in a triangle
    double maxArea = 0.0;          // largest triangle area wanted, px² (0 = no limit)
    double minSegment = 0.75;      // boundary edges are not split below this, px
    int maxVertices = 20000;       // safety stop
};

// Quality triangulation of the inside of a simple polygon: a constrained
// Delaunay triangulation of the outline (incremental insertion with
// Lawson flips, edges recovered by Sloan's flipping), refined Ruppert-style
// by inserting circumcenters of triangles that are too thin or too large
// and splitting boundary edges they encroach. `extraPoints` inside the
// polygon become vertices too (e.g. pin positions).
TriMesh triangulatePolygon(const std::vector<Vec2> &outline, const std::vector<Vec2> &extraPoints,
                           const RefineOptions &options);

// Area of a polygon (positive whatever its orientation).
double polygonArea(const std::vector<Vec2> &polygon);
bool pointInPolygon(const std::vector<Vec2> &polygon, Vec2 p);

} // namespace mothion::puppet
