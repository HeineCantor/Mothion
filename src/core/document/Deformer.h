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
    // A node of a Curve that begins a curve of its own, not joined to the node before (the first node always does): one
    // Curve deformer can hold several curves apart, as a Bones one several skeletons.
    bool starts = false;

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

// The numbers each node of a Curve or Envelope deformer has in a pose, in this order. The first node (and one that begins a
// curve of its own, see Bone::starts): where it is on the page and its tangent's angle (degrees, clockwise from the x
// axis); any other: where it is from the node before, as an angle from that node's tangent and a distance, and its
// tangent's angle from that node's tangent (so a curve turned or
// bent at a node takes the rest of it along, as bones do). Then how long its handles are, back and on, along its tangent:
// the curve between two nodes is a cubic Bézier from one node, through their handles, to the other. An Envelope closes:
// its last node runs back to its first.
constexpr int kNodeParams = 5;
enum NodeParam
{
    NodeA,     // the first node: x; any other: the angle from the node before's tangent
    NodeB,     // the first node: y; any other: the distance from the node before
    NodeAngle, // its tangent's angle
    NodeIn,    // the handle back
    NodeOut    // the handle on
};

// How many numbers each part of a deformer of `kind` has in a pose.
constexpr int partParams(DeformerKind kind)
{
    return kind == DeformerKind::Bones ? kBoneParams : kNodeParams;
}

// A deformation cel's own: its bones and the pose they have at rest (the drawing as drawn). A pose is a list of numbers,
// kBoneParams for each bone in order; the keys of the cel's Deform channel are poses, and the drawings that follow the
// cel are bent by how far the pose on the frame is from the rest.
//
// A Curve or an Envelope has nodes in `bones` instead (`parent` unused): a Curve bends what lies along it, each node's
// radius and fade saying how far from the curve it reaches there; an Envelope bends all that lies inside it, and out round
// it, as its outline is bent.
struct Deformer
{
    std::vector<Bone> bones;
    std::vector<double> rest;

    bool isEmpty() const { return bones.empty(); }
    bool operator==(const Deformer &) const = default;
};

} // namespace mothion::doc
