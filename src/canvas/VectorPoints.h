#pragma once

#include "core/document/VectorStroke.h"

#include <QPointF>
#include <QRectF>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace mothion::canvas {

// The control points of a vector stroke (as Clip Studio's Correct line > Control point tool): a stroke keeps every point
// the pen gave (doc::VectorStroke::points, often hundreds), too close to take one by one; a few of them stand for the
// line, the anchors, and what is done to an anchor is done to the points round it.
//
// The anchors are indices into the stroke's points, in order: the first and the last point, every corner
// (brush::BrushInput::corner), every anchor put by hand (keptAnchor), where the line bends most (the top of each bulge, as
// a drawing's control points are put), and between those as few points as cut the line in pieces each a cubic Bezier
// curve follows within `tolerance` (Schneider's fitting). A smooth line has few of them: a curve between two, not a
// polyline's many corners. They are not kept with the stroke: they are found when the stroke is chosen, and kept by the tool
// as it is edited (the functions below put them right as points come and go).
std::vector<int> controlPoints(const doc::VectorStroke &stroke, double tolerance);

// The weight of the anchor at the end of a run of points between two anchors, at `t` (0..1, the way along the run):
// from 0 at the start to 1 at the end. A smooth anchor (corner false) lets go of the points round it slowly (its
// slope is 0 there: what is near it goes along with it as a whole, so the line stays smooth through it); a corner lets
// go at once (slope 1: the line bends at it).
double anchorWeight(double t, bool startCorner, bool endCorner);

// `original` with each anchor moved by its offset (`offsets[k]` for `anchors[k]`, zero for those that stay), its handles
// along with it: each run beside an anchor moved bent to the curve its ends make now (see movedHandle()), so the line keeps
// what the pen did, its wobbles and its pressure, and only bends; where a run grows longer, points are put in (as dense as
// it was). `anchorsAfter`, if given, gets the anchors' indices in the result. The direction of travel is found again.
doc::VectorStroke movedAnchors(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<QPointF> &offsets, std::vector<int> *anchorsAfter = nullptr);

// `original` with the width at each anchor multiplied by its factor (`factors[k]`, 1 for those that stay), and the
// points between two anchors by a mix of theirs: brush::BrushInput::sizeScale, which multiplies the size whatever the
// brush does with the pressure.
doc::VectorStroke scaledWidths(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<double> &factors);

// The Bezier handles of the anchors: the run of points between two anchors read as a cubic curve from one to the other
// (fitted to the points, its ends on the anchors; each handle along the way the line goes at its anchor, the same both
// sides of a smooth one, only its length fitted; a handle that is in its corner, see brush::BrushInput::heldHandles, held
// there), whose inner control points are the handles: the "out" handle of the anchor that starts the run and the "in"
// handle of the one that ends it.
//
// Anchor `k`'s handle, `out` (towards the next anchor) or in (towards the one before); nothing at an end that has no run
// that way.
std::optional<QPointF> anchorHandle(const doc::VectorStroke &stroke, const std::vector<int> &anchors, int k, bool out);

// Handles known already (set by a drag before: the curve fitted to the points finds them only about where they were put):
// anchor `k`'s, towards the next (`out`) or the one before; nothing for one not known, which is fitted.
using KnownHandle = std::function<std::optional<QPointF>(int k, bool out)>;
// What a change of the handles did: the anchors' indices in the stroke made (a run laid again may have more points), and
// the handles of the runs laid again, where they are now.
struct HandleChange
{
    struct Handle
    {
        int k = -1;
        bool out = false;
        QPointF at;
    };
    std::vector<int> anchors;
    std::vector<Handle> handles;
};

// `original` with anchor `k`'s handle (`out` or in) at `to`: the run on that side laid again along the curve it makes now,
// each point as far along it (by length) and as far off it as it was (so the line keeps what the pen did, and only bends;
// its ends stay); where the curve grew longer, points are put in, so the line is as dense as it was.
// `mirrored` (a smooth anchor): the handle on the other side turns with it, as long as it was, so the line stays smooth
// through the anchor (a corner's does not, nor an end's).
doc::VectorStroke movedHandle(const doc::VectorStroke &original, const std::vector<int> &anchors, int k, bool out,
                              const QPointF &to, bool mirrored, const KnownHandle &known = {},
                              HandleChange *change = nullptr);
// `original` with anchor `k`'s handles pulled out of it: the out handle at `outTo`, the in handle opposite, as long (at an
// end, the one there is); the anchor smooth again (not a corner). What makes a corner (its handles in it) smooth.
doc::VectorStroke pulledHandles(const doc::VectorStroke &original, const std::vector<int> &anchors, int k,
                                const QPointF &outTo, const KnownHandle &known = {}, HandleChange *change = nullptr);

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

// Two strokes made one: `first` joined by one of its ends (the last point if `firstAtEnd`, else the first) to one of
// `second`'s (`secondAtEnd` likewise). The stroke made goes from first's other end to second's other end, with first's
// brush, size and colors; where the ends are apart, a smooth curve that leaves and meets each end along its line fills
// the gap (the pen's pressure and the rest going from one end to the other on it). The pen's pressure falling at the
// ends joined (as it does where a line was begun or let go) is lifted to the line's own there, so the stroke made
// does not thin where they meet.
doc::VectorStroke mergedStrokes(const doc::VectorStroke &first, bool firstAtEnd, const doc::VectorStroke &second,
                                bool secondAtEnd);

// The stroke cut in two at `place` (a point is put there if none is within `snap`): each piece has the stroke's brush,
// colors and seed, and they share the point of the cut. Nothing if the place is at an end.
std::optional<std::pair<doc::VectorStroke, doc::VectorStroke>> cutStroke(const doc::VectorStroke &stroke,
                                                                         const PathPlace &place, double snap);

// A loop (doc::VectorStroke::closed): its anchors are as an open line's, the last one on the first: both stand for the
// one node there. The functions above that take anchors treat that node as any other inside the line (what is done to
// either of the two is done to the node), and removeAnchor() refuses when only two nodes would be left.
//
// A line made a loop: its ends joined as mergedStrokes() joins two (a smooth curve over what is between them).
doc::VectorStroke closedStroke(const doc::VectorStroke &stroke);
// A loop cut open at `place` (a point is put there if none is within `snap`): one open line, from there round to there.
doc::VectorStroke openedStroke(const doc::VectorStroke &loop, const PathPlace &place, double snap);
// What the two points of a loop's joint are (a corner, with handles in it) made the same on both.
void syncJoint(doc::VectorStroke &stroke);

// The area the stroke can paint (its points, and the widest its brush gets round them).
QRectF strokeReach(const doc::VectorStroke &stroke);
// The same for its points `from` to `to` (both included, clamped to the stroke).
QRectF pointsReach(const doc::VectorStroke &stroke, int from, int to);

} // namespace mothion::canvas
