#pragma once

#include "core/rig3d/Pose.h"

#include <vector>

namespace mothion::rig3d {

// A primitive of the model as the pose puts it, in the model's space: positions and normals, x y z for each vertex.
struct PosedPrimitive
{
    int mesh = 0;
    int primitive = 0;
    std::vector<float> positions;
    std::vector<float> normals;
};

// Every primitive of the model's scene in the pose: its blend shapes added first (each target times its weight), then
// bent by its skin (linear blend skinning), or carried whole by its node for a mesh no skin bends.
//
// Skinning, done on the CPU in double: for a bone b, M_b = global(b) · inverseBind(b) and N_b the inverse transpose of
// its 3×3; a vertex p with weights w_k on bones b_k goes to Σ w_k · (M_{b_k} · p) / h_k (h_k: the homogeneous w of that
// product, 1 for an affine matrix), its normal to the unit vector along Σ w_k · unit(N_{b_k} · n) (each bone's normal
// made a unit long before the sum, the sum again after). The vertices are shared out among the cores.
std::vector<PosedPrimitive> posedMeshes(const Model &model, const Pose &pose);

// The same from global matrices already worked out (see globalMatrices()).
std::vector<PosedPrimitive> posedMeshes(const Model &model, const Pose &pose, const std::vector<Mat4> &globals);

} // namespace mothion::rig3d
