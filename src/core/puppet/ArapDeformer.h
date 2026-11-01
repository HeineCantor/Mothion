#pragma once

#include "core/puppet/Triangulation.h"

#include <memory>
#include <vector>

namespace mothion::puppet {

// As-rigid-as-possible deformation of a triangle mesh (Sorkine & Alexa,
// 2007) with some vertices held at given positions (the pins).
//
// Each solve alternates a local step (the best rotation of every vertex's
// neighbourhood, in closed form) and a global step (the positions that best
// fit those rotations, a sparse linear system factored once per set of
// handles). Starting from the previous positions, a few iterations per
// drag event follow the pen smoothly.
class ArapDeformer
{
public:
    explicit ArapDeformer(const TriMesh &rest);
    ~ArapDeformer();
    ArapDeformer(const ArapDeformer &) = delete;
    ArapDeformer &operator=(const ArapDeformer &) = delete;

    // The vertices held in place; refactors the system.
    void setHandles(const std::vector<int> &vertices);

    // `positions`: the current deformed vertices, updated in place.
    // `targets`: where each handle goes, in setHandles() order.
    void solve(std::vector<Vec2> &positions, const std::vector<Vec2> &targets, int iterations) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace mothion::puppet
