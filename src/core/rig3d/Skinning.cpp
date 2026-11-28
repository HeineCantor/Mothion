#include "core/rig3d/Skinning.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>

namespace mothion::rig3d {

namespace {

constexpr int kChunk = 2048; // vertices for one task: enough work to be worth a core

// The vertices of `prim` with its blend shapes added, in double.
void morphed(const Primitive &prim, const std::vector<double> &weights, std::vector<double> &pos, std::vector<double> &nrm)
{
    pos.assign(prim.positions.begin(), prim.positions.end());
    nrm.assign(prim.normals.begin(), prim.normals.end());
    for (std::size_t t = 0; t < prim.targets.size() && t < weights.size(); ++t) {
        const double w = weights[t];
        if (w == 0)
            continue;
        const MorphTarget &target = prim.targets[t];
        for (std::size_t i = 0; i < pos.size() && i < target.positions.size(); ++i)
            pos[i] += w * target.positions[i];
        for (std::size_t i = 0; i < nrm.size() && i < target.normals.size(); ++i)
            nrm[i] += w * target.normals[i];
    }
}

} // namespace

std::vector<PosedPrimitive> posedMeshes(const Model &model, const Pose &pose)
{
    return posedMeshes(model, pose, globalMatrices(model, pose));
}

std::vector<PosedPrimitive> posedMeshes(const Model &model, const Pose &pose, const std::vector<Mat4> &globals)
{
    std::vector<PosedPrimitive> out;
    std::vector<double> pos, nrm;
    for (int n : model.order) {
        const Node &node = model.nodes[std::size_t(n)];
        if (node.mesh < 0)
            continue;
        const Mesh &mesh = model.meshes[std::size_t(node.mesh)];
        // The weights of this mesh's blend shapes in the pose.
        std::vector<double> weights = mesh.targetWeights;
        for (std::size_t s = 0; s < model.morphs.size() && s < pose.morphs.size(); ++s)
            if (model.morphs[s].mesh == node.mesh && std::size_t(model.morphs[s].target) < weights.size())
                weights[std::size_t(model.morphs[s].target)] = pose.morphs[s];
        const Skin *skin = node.skin >= 0 ? &model.skins[std::size_t(node.skin)] : nullptr;

        // The matrices of the skin's bones (M_b, and N_b for normals), or the node's own for a mesh it carries whole.
        std::vector<Mat4> m;
        std::vector<Mat3> nm;
        if (skin) {
            for (std::size_t j = 0; j < skin->joints.size(); ++j)
                m.push_back(globals[std::size_t(skin->joints[j])] * skin->inverseBind[j]);
        } else {
            m.push_back(globals[std::size_t(n)]);
        }
        for (const Mat4 &b : m) {
            bool ok = false;
            const Mat3 inv = b.linear().inverse(&ok);
            nm.push_back(ok ? inv.transposed() : Mat3{}); // a bone squashed flat: its normals as they are
        }

        for (std::size_t p = 0; p < mesh.primitives.size(); ++p) {
            const Primitive &prim = mesh.primitives[p];
            morphed(prim, weights, pos, nrm);
            const std::size_t count = prim.vertexCount();
            PosedPrimitive posed{node.mesh, int(p), std::vector<float>(count * 3), std::vector<float>(count * 3)};
            const bool skinned = skin && prim.weights.offsets.size() == count + 1;
            const int chunks = int((count + kChunk - 1) / kChunk);
            parallelFor(chunks, [&](int chunk) {
                const std::size_t end = std::min(count, std::size_t(chunk + 1) * kChunk);
                for (std::size_t v = std::size_t(chunk) * kChunk; v < end; ++v) {
                    const Vec3 p0{pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]};
                    const Vec3 n0{nrm[v * 3], nrm[v * 3 + 1], nrm[v * 3 + 2]};
                    Vec3 pv, nv;
                    if (skinned && prim.weights.offsets[v] == prim.weights.offsets[v + 1]) {
                        pv = p0; // held by no bone: stays as it was bound
                        nv = n0;
                    } else if (skinned) {
                        for (std::uint32_t k = prim.weights.offsets[v]; k < prim.weights.offsets[v + 1]; ++k) {
                            const std::size_t b = prim.weights.joint[k];
                            if (b >= m.size())
                                continue;
                            const double w = prim.weights.weight[k];
                            pv += m[b].point(p0) * w; // the division by w is per bone, before the sum
                            nv += normalized(nm[b] * n0) * w;
                        }
                    } else {
                        pv = m[0].point(p0);
                        nv = nm[0] * n0;
                    }
                    nv = normalized(nv);
                    posed.positions[v * 3] = float(pv.x);
                    posed.positions[v * 3 + 1] = float(pv.y);
                    posed.positions[v * 3 + 2] = float(pv.z);
                    posed.normals[v * 3] = float(nv.x);
                    posed.normals[v * 3 + 1] = float(nv.y);
                    posed.normals[v * 3 + 2] = float(nv.z);
                }
            });
            out.push_back(std::move(posed));
        }
    }
    return out;
}

} // namespace mothion::rig3d
