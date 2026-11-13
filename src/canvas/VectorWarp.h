#pragma once

#include "core/document/VectorStroke.h"
#include "core/puppet/PuppetRig.h"

#include <vector>

namespace mothion::canvas {

// `strokes` carried along by a puppet warp: each point goes where the mesh triangle that holds it has been taken
// (barycentric coordinates in the rest mesh, applied to the deformed one); a point outside every mesh moves with
// the nearest vertex. Long segments get points in between first, so that a curve bends with the warp. The brush
// grows or shrinks where the triangle did (its area's square root), and the direction of travel is found again
// from the new path.
std::vector<doc::VectorStroke> warpedStrokes(const std::vector<doc::VectorStroke> &strokes,
                                             const puppet::PuppetRig &rig);

} // namespace mothion::canvas
