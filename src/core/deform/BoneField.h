#pragma once

#include "core/deform/Field.h"

#include <vector>

namespace mothion::deform {

// Where a bone is in a pose: its start, its direction (radians, clockwise from the x axis on a page whose y goes down)
// and its length.
struct BoneFrame
{
    Vec2 origin;
    double angle = 0;
    double length = 0;

    Vec2 direction() const;
    Vec2 end() const;
};

// The bones of `deformer` where `pose` (kBoneParams numbers for each bone, see doc::Deformer) puts them, on the page: a
// root where its numbers say, any other from its parent's end, its angle on top of its parent's. Numbers missing from
// the pose are the rest's.
std::vector<BoneFrame> boneFrames(const doc::Deformer &deformer, const std::vector<double> &pose);

// The pose that puts the bones of `deformer` where `frames` are (the inverse of boneFrames()): a root's start and angle as
// they are, any other's from its parent's end and angle (the angle the nearest to `near`'s, if given: a bone turned on
// further does not jump back by a turn).
std::vector<double> poseFromFrames(const doc::Deformer &deformer, const std::vector<BoneFrame> &frames,
                                   const std::vector<double> &near = {});

// What a Bones deformer does to the page on a frame: each point goes where the bones that hold it take it.
//
// A bone moves what it holds rigidly from where it is at rest to where it is in the pose, and stretches it along itself
// as much as its length changed (what lies past its end only moves on with it). How much of a point each bone holds
// comes from its zone (all of it within `radius` of the bone at rest, less and less up to `fade` further); among the bones
// whose zones reach it, the nearer ones count more, so that a joint bends smoothly between its two bones. What no zone
// holds all of stays partly where it was drawn. The bones' moves are blended as rotations and translations (2D dual
// numbers), not as points: a bent joint does not shrink.
class BoneField : public Field
{
public:
    BoneField(const doc::Deformer &deformer, const std::vector<double> &pose);

    bool isIdentity() const override { return m_identity; }
    Vec2 map(Vec2 p) const override;

private:
    struct Part
    {
        Vec2 origin, direction; // at rest
        double length = 0;
        double stretch = 1;     // the length in the pose over the one at rest
        double radius = 0, fade = 0;
        double rx = 1, ry = 0;  // the rotation's half-angle, as a unit complex number
        double sx = 0, sy = 0;  // the translation's dual part: ½ t r̄
    };
    std::vector<Part> m_parts;
    bool m_identity = true;
};

} // namespace mothion::deform
