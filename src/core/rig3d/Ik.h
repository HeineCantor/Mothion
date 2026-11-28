#pragma once

#include "core/rig3d/JointLimit.h"
#include "core/rig3d/Pose.h"

#include <vector>

// Posing a model by dragging its bones: inverse kinematics (CCD), aiming a bone, twisting one.
namespace mothion::rig3d {

// The model's bones with what the person set for them, ready to pose: their settings and limits, by node.
struct Rig
{
    std::vector<JointSettings> settings; // one for each node (the default for one that has none)
    std::vector<NodeLimit> limits;       // one for each node
    double scale = 1;                    // the model's size (the radius of its meshes at rest): for its tolerances

    static Rig make(const Model &model, const std::vector<JointSettings> &settings);
};

// The bones IK turns to bring `node` somewhere: its parent and up, bone after bone, to the root, stopping before the
// first one that is anchored or is no bone (a node of the file that only holds the skeleton). From the tip.
std::vector<int> ikChain(const Model &model, const Rig &rig, int node);

struct IkOptions
{
    int iterations = 24; // CCD sweeps of the whole chain at most
    // Stops once the node is this near the target (times the rig's scale).
    double tolerance = 1e-4;
};

// CCD (cyclic coordinate descent): over and over, each bone of the chain from the tip to the root is turned round its
// start so that the line from it to the node points at the target, the turn scaled by (1 − its stiffness), brought
// within its limits (soft, then hard), and taken back if what is left of it would move the node away from where it
// was asked to go (the CSP test: the turn's axis and the asked axis less than 0.1 alike). Changes the turns of the
// chain's bones in `pose`; true if the node ends within the tolerance of the target.
bool solveIk(const Model &model, const Rig &rig, Pose &pose, int node, const Vec3 &target, const IkOptions &options = {});

// A plain drag of a node's start: its parent is turned to point the node at the target (what lies along the line from
// the parent through the target), and the bones above it that `follow` (each while the one below does) share the turn:
// from the top down each takes 1/n of the angle still left (n: the bones still to turn), so the parent, last, points
// the node at the target. Over `rounds` rounds; within their limits.
void aimAt(const Model &model, const Rig &rig, Pose &pose, int node, const Vec3 &target, int rounds = 2);

// The node turned round its own bone (the x of its limit frame) by `radians`, within its limits.
void twistNode(const Model &model, const Rig &rig, Pose &pose, int node, double radians);

// The node's start in the model's space in the pose.
Vec3 nodePosition(const Model &model, const Pose &pose, int node);

} // namespace mothion::rig3d
