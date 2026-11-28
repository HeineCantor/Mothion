#pragma once

#include "core/rig3d/Math3d.h"
#include "core/rig3d/Model.h"

#include <vector>

namespace mothion::rig3d {

// What the person set for a bone of a model, for posing it (the file has none of it: it is the project's).
struct JointSettings
{
    // Not turned by IK, and the end of every chain through it: what hangs from it is solved, what it hangs from is not.
    bool anchor = false;
    // Turns along when a bone below it is aimed by a plain drag: the turn is shared out among it and the bones that
    // follow up to the first that does not (busto and neck going with the head).
    bool follow = false;
    // How much it holds back in IK: 0 follows fully, toward 1 hardly at all (each CCD step is (1 − stiffness) of the turn).
    double stiffness = 0;

    // Angle limits, in degrees, of its turn from its rest. The turn is split into a twist round the bone and a swing
    // that takes the bone's direction elsewhere; the swing is told by two bends, toward the limit frame's y and z (see
    // limitFrame()). Each of the three can be limited or free.
    bool limitTwist = false;
    double twistMin = -45, twistMax = 45;
    bool limitBend[2] = {false, false};
    double bendMin[2] = {-90, -90};
    double bendMax[2] = {90, 90};
    // Where the swing may go, seen on the sphere from behind the bone (the stereographic plane): a rectangle (the two
    // bends each in their range), or a stadium: the rectangle with its short sides rounded into half circles, which
    // leaves out its corners (a bone bent fully both ways at once). Two limited bends only.
    bool rectangle = false;
    // A soft limit: within this many degrees of a limit, an IK step that goes toward it is slowed, the more the
    // closer (0: none).
    double softZone = 0;

    bool limited() const { return limitTwist || limitBend[0] || limitBend[1]; }
    bool operator==(const JointSettings &o) const
    {
        return anchor == o.anchor && follow == o.follow && stiffness == o.stiffness && limitTwist == o.limitTwist
               && twistMin == o.twistMin && twistMax == o.twistMax && limitBend[0] == o.limitBend[0]
               && limitBend[1] == o.limitBend[1] && bendMin[0] == o.bendMin[0] && bendMin[1] == o.bendMin[1]
               && bendMax[0] == o.bendMax[0] && bendMax[1] == o.bendMax[1] && rectangle == o.rectangle
               && softZone == o.softZone;
    }
};

// The frame a bone's limits are told in, in the node's own space: x along the bone (toward its first child, or the
// node's +y for a bone with none, as Blender points its bones), y and z across it (y as near the node's x as can be).
Quat limitFrame(const Model &model, int node);

// A turn split into a swing of the x axis (where it goes: `direction`) and a twist round x before it (`twist`, radians):
// turn = swing(direction) · twist.
struct SwingTwist
{
    Vec3 direction{1, 0, 0};
    double twist = 0;

    static SwingTwist of(const Quat &q);
    Quat toQuat() const;
    // The two bends (radians) the direction makes, toward y and toward z: twice the angle of its stereographic
    // coordinates seen from −x.
    double bend(int axis) const;
};

// A circle on the unit sphere (where n·v = d), and the cap it bounds on one side (n·v ≥ d).
struct SphereCircle
{
    Vec3 n;
    double d = 0;
    bool contains(const Vec3 &v, double tolerance = 0) const { return dot(n, v) >= d - tolerance; }
    // The nearest point of the circle to `v` (on the sphere); false where there is no one nearest (v on its axis).
    bool nearest(const Vec3 &v, Vec3 &out) const;
};

// The limits of one bone ready to apply: the swing's region as circles on the sphere, its corners, the twist's range.
class JointLimit
{
public:
    JointLimit() = default;
    explicit JointLimit(const JointSettings &settings);

    bool isFree() const { return !m_twist && m_shape == Shape::None; }
    // The swing's region: the circles bounding it, the corners of its outline, and whether a point is in it.
    bool swingContains(const Vec3 &direction, double tolerance = 1e-12) const;
    const std::vector<Vec3> &corners() const { return m_corners; }
    // A turn (from the bone's rest, in its limit frame) brought within the limits: as it is when it is in them; else its
    // swing to the nearest point of the region (the nearest along the sphere: on one of its edges, or a corner) and its
    // twist to the nearest end of its range.
    Quat clamp(const Quat &turn) const;
    Vec3 clampSwing(const Vec3 &direction) const;
    double clampTwist(double twist) const;
    // How far (radians) the turn is within its nearest limit: a large number where nothing limits it, 0 on or past one.
    double margin(const Quat &turn) const;

private:
    enum class Shape { None, Rectangle, Stadium };
    Shape m_shape = Shape::None;
    std::vector<SphereCircle> m_planes; // all of them hold for a point in the rectangle (the stadium's middle)
    std::vector<SphereCircle> m_caps;   // the stadium's round ends: a point in either is in it too
    std::vector<Vec3> m_corners;
    bool m_twist = false;
    double m_twistMin = 0, m_twistMax = 0; // radians, min ≤ max ≤ min + 2π
    bool m_bend[2] = {false, false};
    double m_bendMin[2] = {0, 0}, m_bendMax[2] = {0, 0};
};

// The model's limits of a node: the turn of the node (its local rotation in a pose) brought within them, as JointLimit
// does with it taken from its rest into its limit frame and back.
struct NodeLimit
{
    Quat rest;
    Quat frame;
    JointLimit limit;

    Quat clamp(const Quat &local) const;
    double margin(const Quat &local) const;
    // The turn from rest in the limit frame, and back.
    Quat toLimit(const Quat &local) const { return frame.conjugate() * rest.conjugate() * local * frame; }
    Quat fromLimit(const Quat &turn) const { return rest * frame * turn * frame.conjugate(); }
};

} // namespace mothion::rig3d
