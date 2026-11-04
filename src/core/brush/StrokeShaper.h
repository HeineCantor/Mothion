#pragma once

#include "core/brush/BrushStyle.h"

#include <optional>
#include <vector>

namespace mothion::brush {

// Stroke-level shaping: sharp corners among the incoming points, the
// start/end factor and start taper by distance along the stroke (applied
// dab by dab in DabGenerator), and the corrections that need the finished
// stroke. When one of those applies, the canvas redraws the stroke when the
// pen lifts, with the same seed and the stroke's length known: up to where
// the end zone starts, the redraw places exactly the same dabs.

// Sharp angles: true if the stroke turns at `b`, between `a` and `c`, enough
// to keep a sharp corner there. The turn needed grows with the length of the
// two sides (about 37 degrees below 1 unit, up to about 66 from 30 units, in
// the units of the points); a turn of at most 90 degrees whose middle point
// lies within `tolerance` of the line a-c is the hand wobbling, not a corner.
bool isCorner(double ax, double ay, double bx, double by, double cx, double cy, double tolerance);

// The point of a sharp corner, on its outer side: where the stroke's two
// edges, `radius` from its center line, meet (a miter join), from the corner
// point and the directions in and out of it. Only for turns of 30 degrees or
// more; at most 2 x radius long. Corner first, then the outer edge points
// and the tip, as a quad to fill.
struct CornerTip
{
    double x[4], y[4];
};
std::optional<CornerTip> cornerTip(double cx, double cy, double inX, double inY, double outX, double outY,
                                   double radius);
// The tip at points[i], its directions taken from the nearest points before
// and after it that are apart from it.
std::optional<CornerTip> cornerTipAt(const std::vector<BrushInput> &points, std::size_t i, double radius);

// Finds sharp corners in a stream of points, live. Each point is judged
// with isCorner() against the points `window` before and after it along the
// path, so the whole turn is seen even when the pen sends many close points;
// of a run of neighbours that all turn, the sharpest is the corner. A point
// is decided once the path has gone `window` past it (and past the run it
// is in): the caller holds the points back until then, so the line reaches
// a corner before it turns instead of cutting back to it. Units are the
// caller's (screen pixels for the canvas, so the thresholds follow what the
// eye sees at any zoom).
class CornerDetector
{
public:
    struct Point
    {
        double x = 0.0, y = 0.0;
    };
    struct Decision
    {
        bool corner = false;
        Point before, after; // corner: the points `window` before and after it
    };

    explicit CornerDetector(double window = 4.0, double tolerance = 1.0)
        : m_window(window), m_tolerance(tolerance)
    {
    }
    void reset();
    void add(double x, double y);
    // The decisions now known for the oldest points not yet taken, in order:
    // one per point added. `end`: the stroke is over, decide all of them.
    std::vector<Decision> take(bool end = false);

private:
    double m_window, m_tolerance;
    std::vector<Point> m_points; // since the stroke started
    std::vector<double> m_along; // path length at each point
    std::size_t m_taken = 0;     // points already handed out
    std::size_t m_tested = 0;    // points already judged
    std::vector<double> m_cosine; // per judged point: its turn, or 2 if not a corner
};

// Start/end factor at `distance` px along a stroke of `total` px; `total`
// < 0 while the end is not known yet (no end zone, no ratio zones).
double inOutFactor(const InOutStyle &style, double distance, double total);

// Pressure factor of the start taper: sin(90°·d/length), 1 past `length`.
double startTaperFactor(double length, double distance);

// True if some parameter follows the start/end factor.
bool usesInOut(const BrushStyle &style);

// True if the finished stroke must be redrawn: its look depends on where it
// ends (end zone, ratio zones) or it is simplified.
bool needsFinalPass(const BrushStyle &style);

// The finished stroke's points as they are drawn for good (simplified when
// the brush asks for it).
std::vector<BrushInput> finalPoints(const BrushStyle &style, std::vector<BrushInput> points);

double strokeLength(const std::vector<BrushInput> &points);

// Pressure × sin(90°·d/len) over the first `count` points, len = their
// length (at least 2 px): the start correction after the stroke.
void applyStartRamp(std::vector<BrushInput> &points, int count);

// Drops points while the stroke stays within a tolerance growing with
// `level`, then joins the rest with a smooth (Catmull-Rom) curve. Corner
// points (BrushInput::corner) stay, and the curve does not round them.
std::vector<BrushInput> simplify(const std::vector<BrushInput> &points, int level);

// The correction after the stroke with curves, f = level^2 / 100 px:
//  - nodes: from each node kept, the points closer than 2f are passed over;
//    the first one beyond sets the way the stroke goes, and the next node is
//    the first point that strays from that line by more than the tolerance:
//    f, or with `bySpeed` 0.16 f x the step from the point before (the pen
//    samples at a steady rate, so the step is its speed);
//  - corners: a node where isCorner() holds (tolerance 3f + 1), so a quick
//    zigzag, whose turns fall on single nodes, keeps them;
//  - curve: quadratic pieces from midpoint to midpoint with the nodes as
//    control points, about every 2 px; at a corner the curve goes through
//    the node itself and leaves it straight (BrushInput::corner set there).
std::vector<BrushInput> postCorrect(const std::vector<BrushInput> &points, int level, bool bySpeed);

} // namespace mothion::brush
