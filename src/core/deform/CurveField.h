#pragma once

#include "core/deform/Field.h"

#include <vector>

namespace mothion::deform {

// Where a node of a Curve or Envelope deformer is in a pose: its place, its tangent's angle (radians, clockwise from the x
// axis on a page whose y goes down) and its handles' lengths, back and on.
struct CurveNode
{
    Vec2 at;
    double angle = 0;
    double in = 0, out = 0;
    bool starts = false; // it begins a curve of its own (see doc::Bone::starts; the first node always does)

    Vec2 direction() const;
    Vec2 inHandle() const { return at - direction() * in; }
    Vec2 outHandle() const { return at + direction() * out; }
};

// The nodes of `deformer` where `pose` (kNodeParams numbers for each node, see doc::NodeParam) puts them on the page; numbers
// missing from the pose are the rest's.
std::vector<CurveNode> curveNodes(const doc::Deformer &deformer, const std::vector<double> &pose);
// The curves the nodes make: for each, the first node and the one past its last.
std::vector<std::pair<std::size_t, std::size_t>> curveRuns(const std::vector<CurveNode> &nodes);
// The pose that puts the nodes where `nodes` are (the inverse of curveNodes()); each angle the nearest to `near`'s, if
// given, so that a node turned on further does not jump back by a turn.
std::vector<double> poseFromNodes(const std::vector<CurveNode> &nodes, const std::vector<double> &near = {});
// The curve through the nodes as a line of points: `perSegment` from each node to the next (and from the last back to the
// first if `closed`), the last node (or, closed, nothing more) at the end.
std::vector<Vec2> sampleCurve(const std::vector<CurveNode> &nodes, bool closed, int perSegment);

// A Curve deformer: what lies along the curve at rest goes along the curve in the pose. A point is found on the rest
// curve (where it is nearest) at so far along it and so far across it; it goes as far along the posed curve (so a longer
// curve stretches what it holds) and as far across, turned as the curve turned there. Past the ends it goes on straight
// with them. Each node's radius and fade (along the curve, between two nodes, as far between them) say how far across the
// curve it holds all of a point, and up to where less and less: what it holds partly moves part of the way.
class CurveField : public Field
{
public:
    CurveField(const doc::Deformer &deformer, const std::vector<double> &pose);
    bool isIdentity() const override { return m_identity; }
    Vec2 map(Vec2 p) const override;

private:
    struct Sample
    {
        Vec2 rest, posed;
        Vec2 restNormal, posedNormal; // across the curve, averaged at each point (smooth from one piece to the next)
        double radius = 0, fade = 0;
    };
    // Each curve's points (a point takes the curve it is nearest).
    std::vector<std::vector<Sample>> m_curves;
    bool m_identity = true;
};

// An Envelope deformer: a closed curve round a drawing, all of it inside (and round it outside) bent as the curve is. A
// point is a blend of the points of the rest curve (its mean value coordinates: each point of the curve counts as near it
// and as much of the view from it as it takes up); it goes to the same blend of the posed curve's points. A point on the
// curve goes where that point of the curve goes; nothing tears or folds while the curve does not.
class EnvelopeField : public Field
{
public:
    EnvelopeField(const doc::Deformer &deformer, const std::vector<double> &pose);
    bool isIdentity() const override { return m_identity; }
    Vec2 map(Vec2 p) const override;

private:
    std::vector<Vec2> m_rest, m_posed;
    bool m_identity = true;
};

} // namespace mothion::deform
