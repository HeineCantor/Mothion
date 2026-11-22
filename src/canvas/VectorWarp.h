#pragma once

#include "core/deform/Field.h"
#include "core/document/VectorStroke.h"
#include "core/puppet/PuppetRig.h"

#include <functional>
#include <vector>

namespace mothion::canvas {

// `strokes` carried along by a puppet warp: each point goes where the mesh triangle that holds it has been taken
// (barycentric coordinates in the rest mesh, applied to the deformed one); a point outside every mesh moves with
// the nearest vertex. Long segments get points in between first, so that a curve bends with the warp. The brush
// grows or shrinks as far as the mesh was stretched across the line (not along it), and a tip held at an angle (a
// flat brush) turns as far as the mesh turned there, both smoothed along the line: drawn again, the line is as the
// warp's preview showed it, and the edge of a colour under it stays covered. The direction of travel is found again
// from the new path.
std::vector<doc::VectorStroke> warpedStrokes(const std::vector<doc::VectorStroke> &strokes,
                                             const puppet::PuppetRig &rig);
// The same through a deformation cel's field (see deform::Field): each point goes where the field takes it, the brush
// as wide as the field stretches across the line there.
std::vector<doc::VectorStroke> warpedStrokes(const std::vector<doc::VectorStroke> &strokes, const deform::Field &field);
// The same through any map: where a point goes, and the linear part of the map there (set in its second argument).
using PointMap = std::function<puppet::Vec2(puppet::Vec2, deform::Field::Linear &)>;
std::vector<doc::VectorStroke> warpedStrokes(const std::vector<doc::VectorStroke> &strokes, const PointMap &map);

} // namespace mothion::canvas
