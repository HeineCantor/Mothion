#pragma once

#include "core/document/Deformer.h"
#include "core/puppet/Geometry.h"

#include <memory>
#include <vector>

namespace mothion::deform {

using puppet::Vec2;

// What a deformation cel does to the page on a frame: each point of the drawings that follow it, as drawn, goes to where
// the deformer takes it. Each kind of deformer is one (BoneField, CurveField, EnvelopeField).
class Field
{
public:
    virtual ~Field() = default;
    // Whether the pose is the rest (nothing moves).
    virtual bool isIdentity() const = 0;
    virtual Vec2 map(Vec2 p) const = 0;

    // The linear part of the map round `p` (how a small step from it is turned and stretched): a step (x, y) goes to
    // (a x + b y, c x + d y). Measured across `h` px.
    struct Linear
    {
        double a = 1, b = 0, c = 0, d = 1;
        Vec2 apply(Vec2 v) const { return {a * v.x + b * v.y, c * v.x + d * v.y}; }
        double det() const { return a * d - b * c; }
    };
    Linear linearAt(Vec2 p, double h = 0.5) const;
};

// The field of a deformer of `kind` (bones and rest from `deformer`) in `pose`; nullptr for no kind.
std::unique_ptr<Field> makeField(doc::DeformerKind kind, const doc::Deformer &deformer, const std::vector<double> &pose);

} // namespace mothion::deform
