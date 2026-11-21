#pragma once

#include <vector>

namespace mothion::doc {

// What a deformation cel (see CelProperties::deformer) is made of; fixed when it is made.
enum class DeformerKind
{
    None,     // no deformer: a cel with drawings, or a trajectory
    Bones,    // a tree of rigid bones that bend the drawing at their joints
    Curve,    // a chain of curves the drawing bends along (to come)
    Envelope, // a closed curve round the drawing that shapes it (to come)
};

// A bone of a Bones deformer: where it starts, its angle and its length are numbers of the pose (see Deformer); what it
// holds of the drawing is fixed. It holds all the points within `radius` of it (at rest), and less and less of them up
// to `fade` further: what no bone holds stays as drawn.
struct Bone
{
    int parent = -1; // a bone before this one (-1: a root)
    double radius = 30;
    double fade = 30;

    bool operator==(const Bone &) const = default;
};

// The numbers each bone has in a pose, in this order: where it starts (a root: on the page; any other: from the end of
// its parent, along and across the parent), its angle in degrees (a root: clockwise from the x axis; any other: from
// its parent's), its length.
constexpr int kBoneParams = 4;
enum BoneParam
{
    BoneX,
    BoneY,
    BoneAngle,
    BoneLength
};

// A deformation cel's own: its bones and the pose they have at rest (the drawing as drawn). A pose is a list of numbers,
// kBoneParams for each bone in order; the keys of the cel's Deform channel are poses, and the drawings that follow the
// cel are bent by how far the pose on the frame is from the rest.
struct Deformer
{
    std::vector<Bone> bones;
    std::vector<double> rest;

    bool isEmpty() const { return bones.empty(); }
    bool operator==(const Deformer &) const = default;
};

} // namespace mothion::doc
