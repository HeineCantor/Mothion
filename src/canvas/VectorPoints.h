#pragma once

#include "core/document/VectorStroke.h"

#include <QPointF>
#include <QRectF>

#include <optional>
#include <utility>
#include <vector>

namespace mothion::canvas {

// The control points of a vector stroke (as Clip Studio's Correct line > Control point tool): a stroke keeps every point
// the pen gave (doc::VectorStroke::points, often hundreds), too close to take one by one; a few of them stand for the
// line, the anchors, and what is done to an anchor is done to the points round it.
//
// The anchors are indices into the stroke's points, in order: the first and the last point, every corner
// (brush::BrushInput::corner), and between them the fewest points that keep the line within `tolerance` of the path
// (Douglas-Peucker). They are not kept with the stroke: they are found when the stroke is chosen, and kept by the tool
// as it is edited (the functions below put them right as points come and go).
std::vector<int> controlPoints(const doc::VectorStroke &stroke, double tolerance);

// The weight of the anchor at the end of a run of points between two anchors, at `t` (0..1, the way along the run):
// from 0 at the start to 1 at the end. A smooth anchor (corner false) lets go of the points round it slowly (its
// slope is 0 there: what is near it goes along with it as a whole, so the line stays smooth through it); a corner lets
// go at once (slope 1: the line bends at it).
double anchorWeight(double t, bool startCorner, bool endCorner);

// `original` with each anchor moved by its offset (`offsets[k]` for `anchors[k]`, zero for those that stay): each point
// between two anchors moves by a mix of their offsets (anchorWeight(), the way along the run measured on the path), so
// the line keeps what the pen did, its wobbles and its pressure, and only bends. The direction of travel is found again.
doc::VectorStroke movedAnchors(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<QPointF> &offsets);

// `original` with the width at each anchor multiplied by its factor (`factors[k]`, 1 for those that stay), and the
// points between two anchors by a mix of theirs: brush::BrushInput::sizeScale, which multiplies the size whatever the
// brush does with the pressure.
doc::VectorStroke scaledWidths(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<double> &factors);

// The place on the stroke's path nearest `pos`: between points `segment` and `segment + 1`, at `t` (0..1) of the way.
struct PathPlace
{
    int segment = 0;
    double t = 0;
    double distance = 0;
    QPointF point;
};
std::optional<PathPlace> nearestPlace(const doc::VectorStroke &stroke, const QPointF &pos);

// An anchor made at `place`: the stroke's point there if one is within `snap` of it, else a point put in between
// (each value halfway as far as the place is). `anchors` is put right; returns the new anchor's position in it (or the
// one that was there already).
int addAnchor(doc::VectorStroke &stroke, std::vector<int> &anchors, const PathPlace &place, double snap);

// Anchor `k` taken away. Inside the line, the points between the anchors before and after it are laid again on a
// smooth curve through the anchors round them (straight beside a corner); at an end, the line is cut short to the next
// anchor. False (nothing done) when only two anchors are left.
bool removeAnchor(doc::VectorStroke &stroke, std::vector<int> &anchors, int k);

// Anchor `k` made a corner, or a smooth point again: made smooth, the points between the anchors before and after it
// are laid again on a smooth curve through them (the angle is rounded); made a corner, the line stays as it is and
// bends at it from then on.
void toggleCorner(doc::VectorStroke &stroke, const std::vector<int> &anchors, int k);

// The stroke cut in two at `place` (a point is put there if none is within `snap`): each piece has the stroke's brush,
// colors and seed, and they share the point of the cut. Nothing if the place is at an end.
std::optional<std::pair<doc::VectorStroke, doc::VectorStroke>> cutStroke(const doc::VectorStroke &stroke,
                                                                         const PathPlace &place, double snap);

// The area the stroke can paint (its points, and the widest its brush gets round them).
QRectF strokeReach(const doc::VectorStroke &stroke);
// The same for its points `from` to `to` (both included, clamped to the stroke).
QRectF pointsReach(const doc::VectorStroke &stroke, int from, int to);

} // namespace mothion::canvas
