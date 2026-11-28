#include "canvas/VectorPoints.h"

#include "core/brush/DabGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

using brush::BrushInput;

QPointF at(const BrushInput &in) { return {in.x, in.y}; }

// The anchors between `first` and `last` (neither included) that cut the run in pieces each a cubic curve can follow within
// `tolerance` (see fitRun()); defined below, with the curves.
void splitByCurves(const std::vector<BrushInput> &points, int first, int last, double tolerance, std::vector<int> &out);
// How far the points from `first` to `last` lie from the cubic curve fitted to them, at most; and where.
double fitError(const std::vector<BrushInput> &points, int first, int last, int *where = nullptr);

// Where the line bends most: the points where it turns most over a stretch of it as long as the line's size asks (at least
// a few screen pixels' worth, `tolerance` being px of the document), as far as that is a real bend, each the sharpest of
// those round it. As a
// drawing's control points are put: on the top of each bulge, not where one bend goes into the next.
std::vector<int> bendPoints(const std::vector<BrushInput> &points, double tolerance)
{
    const int n = int(points.size());
    std::vector<double> length(std::size_t(std::max(n, 1)), 0.0);
    for (int i = 1; i < n; ++i)
        length[std::size_t(i)] = length[std::size_t(i - 1)]
                                 + std::hypot(points[std::size_t(i)].x - points[std::size_t(i - 1)].x,
                                              points[std::size_t(i)].y - points[std::size_t(i - 1)].y);
    // How much of the line each way the turn is measured over: a part of the line as long as its size asks (the wobbles of
    // the pen turn little over it, a bend much), never less than a few screen pixels' worth.
    const double reach = std::max(6.0 * tolerance, length.back() / 20.0);
    const double enough = 30.0 * std::numbers::pi / 180.0; // turning this much over the reach is a bend
    std::vector<double> turn(std::size_t(std::max(n, 1)), 0.0);
    int back = 0, on = 0;
    for (int i = 0; i < n; ++i) {
        if (length[std::size_t(i)] < reach || length.back() - length[std::size_t(i)] < reach)
            continue; // too near an end to tell
        while (back + 1 < i && length[std::size_t(i)] - length[std::size_t(back + 1)] >= reach)
            ++back;
        on = std::max(on, i);
        while (on < n - 1 && length[std::size_t(on)] - length[std::size_t(i)] < reach)
            ++on;
        const double ax = points[std::size_t(i)].x - points[std::size_t(back)].x, ay = points[std::size_t(i)].y - points[std::size_t(back)].y;
        const double bx = points[std::size_t(on)].x - points[std::size_t(i)].x, by = points[std::size_t(on)].y - points[std::size_t(i)].y;
        turn[std::size_t(i)] = std::abs(std::atan2(ax * by - ay * bx, ax * bx + ay * by));
    }
    // The sharpest of each stretch of the line where it turns enough, and none nearer each other than the reach.
    std::vector<int> out;
    for (int i = 0; i < n;) {
        if (turn[std::size_t(i)] < enough) {
            ++i;
            continue;
        }
        int best = i, j = i;
        while (j < n && turn[std::size_t(j)] >= enough) {
            if (turn[std::size_t(j)] > turn[std::size_t(best)])
                best = j;
            ++j;
        }
        if (out.empty() || length[std::size_t(best)] - length[std::size_t(out.back())] >= reach)
            out.push_back(best);
        else if (turn[std::size_t(best)] > turn[std::size_t(out.back())])
            out.back() = best;
        i = j;
    }
    return out;
}

// The way along the path from point `from` to each point up to `to`, 0..1 (by the points' order where the run has no
// length).
std::vector<double> runFractions(const std::vector<BrushInput> &points, int from, int to)
{
    std::vector<double> s(std::size_t(to - from + 1), 0.0);
    for (int i = from + 1; i <= to; ++i)
        s[std::size_t(i - from)] = s[std::size_t(i - from - 1)]
                                   + std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    const double total = s.back();
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = total > 1e-9 ? s[i] / total : (to > from ? double(i) / double(to - from) : 0.0);
    return s;
}

void refreshDirections(std::vector<BrushInput> &points)
{
    for (std::size_t i = 0; i < points.size(); ++i) {
        const BrushInput &before = points[i > 0 ? i - 1 : i];
        const BrushInput &after = points[i + 1 < points.size() ? i + 1 : i];
        if (before.x != after.x || before.y != after.y)
            points[i].direction = std::atan2(after.y - before.y, after.x - before.x) * 180.0 / std::numbers::pi;
    }
}

// Catmull-Rom (uniform) between p1 and p2 (p0 and p3 round them), at u.
QPointF catmullRom(const QPointF &p0, const QPointF &p1, const QPointF &p2, const QPointF &p3, double u)
{
    const double u2 = u * u, u3 = u2 * u;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2
                  + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3);
}

// The points between anchors k and k + 1 laid again on a smooth curve through the anchors round them (an anchor that
// is a corner, or an end, stands in for the one beyond it: the curve leaves it straight).
void relayRun(std::vector<BrushInput> &points, const std::vector<int> &anchors, std::size_t k, bool loop = false)
{
    const int from = anchors[k], to = anchors[k + 1];
    if (to - from < 2)
        return;
    const QPointF p1 = at(points[from]), p2 = at(points[to]);
    // (On a loop the line goes on past the joint: the node beyond it is on the other side of the list.)
    const std::size_t count = anchors.size();
    const QPointF p0 = points[from].corner ? p1 : k > 0 ? at(points[anchors[k - 1]]) : loop ? at(points[anchors[count - 2]]) : p1;
    const QPointF p3 = points[to].corner        ? p2
                       : k + 2 < count          ? at(points[anchors[k + 2]])
                       : loop && count > 2      ? at(points[anchors[1]])
                                                : p2;
    const std::vector<double> s = runFractions(points, from, to);
    for (int i = from + 1; i < to; ++i) {
        const QPointF p = catmullRom(p0, p1, p2, p3, s[std::size_t(i - from)]);
        points[i].x = p.x();
        points[i].y = p.y();
    }
}

// A loop's points and anchors begun at node `by` (of its anchors.size() - 1 nodes) instead.
void rotateLoop(std::vector<BrushInput> &points, std::vector<int> &anchors, int by)
{
    const int nodes = int(anchors.size()) - 1;
    if (nodes < 1 || by % nodes == 0)
        return;
    const int distinct = int(points.size()) - 1; // the last is the first again
    const int cut = anchors[std::size_t(by)];
    std::vector<BrushInput> turned(points.begin() + cut, points.begin() + distinct);
    turned.insert(turned.end(), points.begin(), points.begin() + cut + 1);
    std::vector<int> moved;
    for (int i = 0; i < nodes; ++i)
        moved.push_back((anchors[std::size_t((by + i) % nodes)] - cut + distinct) % distinct);
    moved.push_back(distinct);
    points = std::move(turned);
    anchors = std::move(moved);
}

// A loop turned so that node `node` is well inside the list of its points, where what is done to an anchor inside a
// line can be done to it; turned back by back().
struct TurnedLoop
{
    doc::VectorStroke stroke;
    std::vector<int> anchors;
    int nodes = 0, by = 0, node = 0; // the node asked for, as it is numbered now

    TurnedLoop(const doc::VectorStroke &loop, const std::vector<int> &original, int k)
        : stroke(loop), anchors(original), nodes(int(original.size()) - 1)
    {
        const int asked = k % nodes;
        by = (asked + nodes / 2) % nodes;
        node = (asked - by + nodes) % nodes;
        rotateLoop(stroke.points, anchors, by);
        stroke.closed = false; // worked on as an open line
    }
    // A handle's place as numbered before and after the turn (the joint's in handle is the last anchor's).
    int turned(int k) const { return (k % nodes - by + nodes) % nodes; }
    int unturned(int j, bool out) const
    {
        const int k = (j + by) % nodes;
        return !out && k == 0 ? nodes : k;
    }
    int turnedHandle(int k, bool out) const
    {
        const int j = turned(k);
        return !out && j == 0 ? nodes : j;
    }
    void back(doc::VectorStroke &result, std::vector<int> &moved) const
    {
        rotateLoop(result.points, moved, (nodes - by) % nodes);
        result.closed = true;
        syncJoint(result);
    }
};

bool isLoop(const doc::VectorStroke &stroke, const std::vector<int> &anchors)
{
    return stroke.closed && anchors.size() >= 3;
}

// The point put at `place` (or the one there, within `snap`): its index.
int pointAt(std::vector<BrushInput> &points, const PathPlace &place, double snap)
{
    const int a = place.segment, b = std::min(place.segment + 1, int(points.size()) - 1);
    const double da = std::hypot(place.point.x() - points[a].x, place.point.y() - points[a].y);
    const double db = std::hypot(place.point.x() - points[b].x, place.point.y() - points[b].y);
    if (std::min(da, db) <= snap || a == b)
        return da <= db ? a : b;
    BrushInput in = brush::DabGenerator::interpolate(points[a], points[b], place.t);
    in.corner = false;
    in.heldHandles = 0;
    in.keptAnchor = false;
    points.insert(points.begin() + b, in);
    return b;
}

} // namespace

std::vector<int> controlPoints(const doc::VectorStroke &stroke, double tolerance)
{
    const int n = int(stroke.points.size());
    if (n == 0)
        return {};
    if (n == 1)
        return {0};
    // The ends, the corners, the anchors put by hand and where the line bends most stay; between them, as few points as
    // keep the line within the tolerance of a curve.
    std::vector<int> fixed{0};
    const std::vector<int> bends = bendPoints(stroke.points, tolerance);
    for (int i = 1; i + 1 < n; ++i)
        if (stroke.points[i].corner || stroke.points[i].keptAnchor || std::binary_search(bends.begin(), bends.end(), i))
            fixed.push_back(i);
    fixed.push_back(n - 1);
    std::vector<int> out{0};
    for (std::size_t k = 0; k + 1 < fixed.size(); ++k) {
        const std::size_t start = out.size() - 1;
        splitByCurves(stroke.points, fixed[k], fixed[k + 1], tolerance, out);
        out.push_back(fixed[k + 1]);
        // A cut where the points lay farthest is not always the one a curve needs: an anchor cut in whose runs one curve
        // can follow together goes.
        for (std::size_t i = start + 1; i + 1 < out.size();) {
            if (fitError(stroke.points, out[i - 1], out[i + 1]) <= tolerance)
                out.erase(out.begin() + std::ptrdiff_t(i));
            else
                ++i;
        }
    }
    return out;
}

double anchorWeight(double t, bool startCorner, bool endCorner)
{
    t = std::clamp(t, 0.0, 1.0);
    const double m0 = startCorner ? 1.0 : 0.0, m1 = endCorner ? 1.0 : 0.0;
    const double t2 = t * t, t3 = t2 * t;
    // Cubic Hermite from 0 to 1 with slopes m0 and m1 at the ends.
    return m0 * (t3 - 2.0 * t2 + t) + (-2.0 * t3 + 3.0 * t2) + m1 * (t3 - t2);
}

namespace {

// The cubic curve fitted to the run of points from `from` to `to` (its ends on them; the inner control points by least
// squares, the points taken at their fraction of the way), and those fractions.
struct RunCurve
{
    QPointF p0, p1, p2, p3;
    std::vector<double> t;

    QPointF at(double u) const
    {
        const double v = 1.0 - u;
        return v * v * v * p0 + 3 * v * v * u * p1 + 3 * v * u * u * p2 + u * u * u * p3;
    }
};

// Which inner control points a fit leaves in their anchor (a handle dropped into a corner).
enum class Held { None, Start, End, Both };

// The way the line goes at point `i`, into the run on side `side` (+1: towards the points after it, -1: before): through the
// point at a smooth anchor (from a little before it to a little after it, so the two runs that meet there share it and the
// line stays smooth through it), out of it into the run at an end of the stroke or a corner.
QPointF tangentAt(const std::vector<BrushInput> &points, int i, int side)
{
    constexpr double kReach = 8.0; // px of the line looked at, each way
    const int n = int(points.size());
    auto reach = [&](int from, int step) {
        double gone = 0;
        int j = from;
        while (j + step >= 0 && j + step < n && gone < kReach) {
            gone += std::hypot(points[std::size_t(j + step)].x - points[std::size_t(j)].x,
                               points[std::size_t(j + step)].y - points[std::size_t(j)].y);
            j += step;
        }
        return j;
    };
    bool smooth = i > 0 && i + 1 < n && !points[std::size_t(i)].corner;
    if (smooth) { // a sharp turn there (not marked a corner) is one all the same: each side its own way
        const QPointF in = at(points[std::size_t(i)]) - at(points[std::size_t(reach(i, -1))]);
        const QPointF out = at(points[std::size_t(reach(i, +1))]) - at(points[std::size_t(i)]);
        const double turn = std::abs(std::atan2(in.x() * out.y() - in.y() * out.x(), in.x() * out.x() + in.y() * out.y()));
        smooth = turn < 50.0 * std::numbers::pi / 180.0;
    }
    const int back = smooth ? reach(i, -1) : i, on = smooth ? reach(i, +1) : reach(i, side);
    QPointF d = smooth ? at(points[std::size_t(on)]) - at(points[std::size_t(back)])
                       : at(points[std::size_t(on)]) - at(points[std::size_t(i)]);
    if (smooth && side < 0)
        d = -d;
    const double length = std::hypot(d.x(), d.y());
    return length < 1e-9 ? QPointF() : d / length;
}

// The handles that fit the run best for the fractions `c.t`: each along the way the line goes at its anchor (tangentAt()),
// as long as the least squares say (Schneider); those `held` in their anchor. A length that makes no sense (none, against
// the line, or longer than the run) gives both a third of the way from end to end, as a plain curve has.
void fitControls(const std::vector<BrushInput> &points, int from, int to, RunCurve &c, Held held)
{
    const QPointF t1 = tangentAt(points, from, +1), t2 = tangentAt(points, to, -1);
    const double chord = std::hypot(c.p3.x() - c.p0.x(), c.p3.y() - c.p0.y());
    double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
    const double dot = t1.x() * t2.x() + t1.y() * t2.y();
    for (int i = from + 1; i < to; ++i) {
        const double u = c.t[std::size_t(i - from)], v = 1.0 - u;
        const double b0 = v * v * v, b1 = 3 * v * v * u, b2 = 3 * v * u * u, b3 = u * u * u;
        const QPointF rest = at(points[std::size_t(i)]) - ((b0 + b1) * c.p0 + (b2 + b3) * c.p3);
        c00 += b1 * b1, c01 += b1 * b2 * dot, c11 += b2 * b2;
        x0 += b1 * (rest.x() * t1.x() + rest.y() * t1.y());
        x1 += b2 * (rest.x() * t2.x() + rest.y() * t2.y());
    }
    double a1 = chord / 3, a2 = chord / 3;
    if (held == Held::Both) {
        a1 = a2 = 0;
    } else if (held == Held::Start) {
        a1 = 0;
        a2 = c11 > 1e-12 ? x1 / c11 : chord / 3;
        if (!(a2 > 1e-6 * chord && a2 < chord))
            a2 = chord / 3;
    } else if (held == Held::End) {
        a2 = 0;
        a1 = c00 > 1e-12 ? x0 / c00 : chord / 3;
        if (!(a1 > 1e-6 * chord && a1 < chord))
            a1 = chord / 3;
    } else if (const double det = c00 * c11 - c01 * c01; std::abs(det) > 1e-12) {
        const double f1 = (c11 * x0 - c01 * x1) / det, f2 = (c00 * x1 - c01 * x0) / det;
        if (f1 > 1e-6 * chord && f2 > 1e-6 * chord && f1 < chord && f2 < chord)
            a1 = f1, a2 = f2;
    }
    if (t1.isNull())
        a1 = 0;
    if (t2.isNull())
        a2 = 0;
    c.p1 = c.p0 + a1 * t1;
    c.p2 = c.p3 + a2 * t2;
}

// Each point's fraction put where the curve passes nearest it: a Newton step on the distance.
void project(const std::vector<BrushInput> &points, int from, int to, RunCurve &c)
{
    for (int i = from + 1; i < to; ++i) {
        double &u = c.t[std::size_t(i - from)];
        const double v = 1.0 - u;
        const QPointF d = c.at(u) - at(points[std::size_t(i)]);
        const QPointF d1 = 3 * v * v * (c.p1 - c.p0) + 6 * v * u * (c.p2 - c.p1) + 3 * u * u * (c.p3 - c.p2);
        const QPointF d2 = 6 * v * (c.p2 - 2 * c.p1 + c.p0) + 6 * u * (c.p3 - 2 * c.p2 + c.p1);
        const double num = d.x() * d1.x() + d.y() * d1.y();
        const double den = d1.x() * d1.x() + d1.y() * d1.y() + d.x() * d2.x() + d.y() * d2.y();
        if (std::abs(den) > 1e-12)
            u = std::clamp(u - num / den, 0.0, 1.0);
    }
}

// The fit, with the inner control points `held` in their anchors.
RunCurve fitRunHeld(const std::vector<BrushInput> &points, int from, int to, Held held)
{
    RunCurve c;
    c.p0 = at(points[std::size_t(from)]);
    c.p3 = at(points[std::size_t(to)]);
    c.t = runFractions(points, from, to);
    c.p1 = c.p0 + (c.p3 - c.p0) / 3.0; // a straight line, to begin with
    c.p2 = c.p3 - (c.p3 - c.p0) / 3.0;
    if (held == Held::Start || held == Held::Both)
        c.p1 = c.p0;
    if (held == Held::End || held == Held::Both)
        c.p2 = c.p3;
    if (to - from < 3)
        return c;
    fitControls(points, from, to, c, held);
    // Each point's fraction put where the curve passes nearest it (a Newton step on the distance), and the fit made again:
    // a run that is a cubic is found about as that cubic (so a handle dragged stays about where it was let go).
    for (int round = 0; round < 20; ++round) {
        project(points, from, to, c);
        fitControls(points, from, to, c, held);
    }
    return c;
}

// The run's curve: with the handles that are in their anchors (BrushInput::heldHandles) held there.
RunCurve fitRun(const std::vector<BrushInput> &points, int from, int to)
{
    const BrushInput &first = points[std::size_t(from)], &last = points[std::size_t(to)];
    const bool start = first.corner && (first.heldHandles & 2), end = last.corner && (last.heldHandles & 1);
    return fitRunHeld(points, from, to, start && end ? Held::Both : start ? Held::Start : end ? Held::End : Held::None);
}

double fitError(const std::vector<BrushInput> &points, int first, int last, int *where)
{
    if (where)
        *where = first;
    if (last - first < 2)
        return 0;
    const RunCurve c = fitRun(points, first, last);
    double farthest = 0;
    for (int i = first + 1; i < last; ++i) {
        const QPointF d = c.at(c.t[std::size_t(i - first)]) - at(points[std::size_t(i)]);
        const double distance = std::hypot(d.x(), d.y());
        if (distance > farthest) {
            farthest = distance;
            if (where)
                *where = i;
        }
    }
    return farthest;
}

void splitByCurves(const std::vector<BrushInput> &points, int first, int last, double tolerance, std::vector<int> &out)
{
    if (last - first < 3)
        return;
    // Where the points lie farthest from the curve fitted to them: cut there when it is past the tolerance (Schneider).
    int index = first;
    const double farthest = fitError(points, first, last, &index);
    if (farthest <= tolerance)
        return;
    // Not hard by an end (an anchor a few points from another makes a run too short to bend): in its middle half at most.
    const int margin = std::max(1, (last - first) / 4);
    index = std::clamp(index, first + margin, last - margin);
    splitByCurves(points, first, index, tolerance, out);
    out.push_back(index);
    splitByCurves(points, index, last, tolerance, out);
}

// The curve's parameter at each of `fractions` of its length (0..1, rising), from a table of its length.
std::vector<double> parametersAt(const RunCurve &c, const std::vector<double> &fractions)
{
    constexpr int kSteps = 256;
    std::vector<double> length(kSteps + 1, 0.0);
    QPointF last = c.p0;
    for (int i = 1; i <= kSteps; ++i) {
        const QPointF p = c.at(double(i) / kSteps);
        length[std::size_t(i)] = length[std::size_t(i - 1)] + std::hypot(p.x() - last.x(), p.y() - last.y());
        last = p;
    }
    std::vector<double> out;
    out.reserve(fractions.size());
    const double total = length.back();
    std::size_t i = 0;
    for (const double f : fractions) {
        if (total < 1e-9) {
            out.push_back(std::clamp(f, 0.0, 1.0));
            continue;
        }
        const double want = std::clamp(f, 0.0, 1.0) * total;
        while (i + 1 < std::size_t(kSteps) && length[i + 1] < want)
            ++i;
        const double span = length[i + 1] - length[i];
        const double w = span > 1e-12 ? (want - length[i]) / span : 0.0;
        out.push_back((double(i) + std::clamp(w, 0.0, 1.0)) / kSteps);
    }
    return out;
}

// The run from anchor `run` to the next laid on the curve `after` (same ends), as Harmony draws a run: its points put
// again along the curve at even steps, as dense as they were (and never sparser than kMaxStep, so the line draws as a curve,
// not in straight pieces), each with the pressure and the rest the pen had as far along the run. What the pen wobbled off
// the curve goes, so no wobble, stretched by the bend, comes out as a zigzag. The anchors after the run move along by the
// points put in or taken out.
void reshapeRun(std::vector<BrushInput> &points, std::vector<int> &anchors, std::size_t run, const RunCurve &after)
{
    constexpr double kMaxStep = 2.0;
    const int from = anchors[run], to = anchors[run + 1];
    const int count = to - from;
    if (count < 1)
        return;
    const std::vector<BrushInput> original(points.begin() + from, points.begin() + to + 1);
    const std::vector<double> along = runFractions(points, from, to);
    double was = 0;
    for (int i = 1; i <= count; ++i)
        was += std::hypot(original[std::size_t(i)].x - original[std::size_t(i - 1)].x,
                          original[std::size_t(i)].y - original[std::size_t(i - 1)].y);
    const double spacing = std::min(was > 1e-9 ? was / count : kMaxStep, kMaxStep);
    // How long the curve is now.
    double length = 0;
    QPointF last = after.p0;
    for (int i = 1; i <= 256; ++i) {
        const QPointF p = after.at(double(i) / 256);
        length += std::hypot(p.x() - last.x(), p.y() - last.y());
        last = p;
    }
    const int steps = std::clamp(int(std::ceil(length / spacing - 1e-6)), 1, 20000);
    std::vector<double> fractions(std::size_t(steps) + 1);
    for (int j = 0; j <= steps; ++j)
        fractions[std::size_t(j)] = double(j) / steps;
    const std::vector<double> at = parametersAt(after, fractions);
    std::vector<BrushInput> laid;
    laid.reserve(std::size_t(steps) + 1);
    laid.push_back(original.front()); // the anchor itself
    std::size_t i = 0;
    for (int j = 1; j < steps; ++j) {
        const double f = fractions[std::size_t(j)];
        while (i + 1 < std::size_t(count) && along[i + 1] < f)
            ++i;
        const double span = along[i + 1] - along[i];
        const double w = span > 1e-12 ? std::clamp((f - along[i]) / span, 0.0, 1.0) : 0.0;
        BrushInput in = brush::DabGenerator::interpolate(original[i], original[i + 1], w);
        in.corner = false;
        in.heldHandles = 0;
        in.keptAnchor = false;
        const QPointF q = after.at(at[std::size_t(j)]);
        in.x = q.x();
        in.y = q.y();
        laid.push_back(in);
    }
    laid.push_back(original.back()); // the next anchor
    // In place of the run (its ends included), and the anchors after it along.
    points.erase(points.begin() + from, points.begin() + to + 1);
    points.insert(points.begin() + from, laid.begin(), laid.end());
    const int added = int(laid.size()) - (count + 1);
    for (std::size_t k = run + 1; k < anchors.size(); ++k)
        anchors[k] += added;
}

// The curve of the run from anchor `run` to the next: fitted to its points, with the handles `known` (set before) in place
// of the fitted ones, and the points' fractions found again on it.
RunCurve curveOf(const std::vector<BrushInput> &points, const std::vector<int> &anchors, std::size_t run,
                 const KnownHandle &known)
{
    const int from = anchors[run], to = anchors[run + 1];
    RunCurve c = fitRun(points, from, to);
    const std::optional<QPointF> first = known ? known(int(run), true) : std::nullopt;
    const std::optional<QPointF> last = known ? known(int(run + 1), false) : std::nullopt;
    if (!first && !last)
        return c;
    if (first)
        c.p1 = *first;
    if (last)
        c.p2 = *last;
    for (int round = 0; round < 20; ++round)
        project(points, from, to, c);
    return c;
}

} // namespace

std::optional<QPointF> anchorHandle(const doc::VectorStroke &stroke, const std::vector<int> &anchors, int k, bool out)
{
    const int n = int(anchors.size());
    if (isLoop(stroke, anchors) && k >= 0 && k < n) {
        const TurnedLoop loop(stroke, anchors, k);
        return anchorHandle(loop.stroke, loop.anchors, loop.node, out);
    }
    if (k < 0 || k >= n || (out ? k + 1 >= n : k == 0))
        return std::nullopt;
    const RunCurve c = out ? fitRun(stroke.points, anchors[std::size_t(k)], anchors[std::size_t(k + 1)])
                           : fitRun(stroke.points, anchors[std::size_t(k - 1)], anchors[std::size_t(k)]);
    return out ? c.p1 : c.p2;
}

doc::VectorStroke movedHandle(const doc::VectorStroke &original, const std::vector<int> &anchors, int k, bool out,
                              const QPointF &to, bool mirrored, const KnownHandle &known, HandleChange *change)
{
    if (isLoop(original, anchors) && k >= 0 && k < int(anchors.size())) {
        const TurnedLoop loop(original, anchors, k);
        const KnownHandle there = !known ? KnownHandle() : KnownHandle([&](int j, bool towardsNext) {
            return known(loop.unturned(j, towardsNext), towardsNext);
        });
        HandleChange done;
        doc::VectorStroke result = movedHandle(loop.stroke, loop.anchors, loop.node, out, to, mirrored, there, &done);
        loop.back(result, done.anchors);
        for (HandleChange::Handle &h : done.handles)
            h.k = loop.unturned(h.k, h.out);
        if (change)
            *change = std::move(done);
        return result;
    }
    doc::VectorStroke result = original;
    std::vector<int> moved = anchors;
    const int n = int(anchors.size());
    if (change)
        *change = {anchors, {}};
    if (k < 0 || k >= n || (out ? k + 1 >= n : k == 0))
        return result;
    std::vector<BrushInput> &points = result.points;
    auto lay = [&](std::size_t run, const RunCurve &, const RunCurve &after) {
        reshapeRun(points, moved, run, after);
        if (change) {
            change->handles.push_back({int(run), true, after.p1});
            change->handles.push_back({int(run + 1), false, after.p2});
        }
    };
    const std::size_t run = std::size_t(out ? k : k - 1);
    const RunCurve before = curveOf(original.points, anchors, run, known);
    const QPointF anchor = at(original.points[std::size_t(anchors[std::size_t(k)])]);
    const QPointF was = (out ? before.p1 : before.p2) - anchor, now = to - anchor;
    const bool other = out ? k > 0 : k + 1 < n;
    const bool turns = mirrored && other && !original.points[std::size_t(anchors[std::size_t(k)])].corner
                       && std::hypot(was.x(), was.y()) > 1e-9 && std::hypot(now.x(), now.y()) > 1e-9;
    // The other side of a smooth anchor: its handle turned as far as this one turned, as long as it was (turned, not set
    // opposite: the fitted handles are seldom quite opposite, and the line would jump at the first move). The run after the
    // anchor is laid again first, then the one before (laying one again moves the anchors after it).
    auto turnOther = [&] {
        const double turn = std::atan2(now.y(), now.x()) - std::atan2(was.y(), was.x());
        const double c = std::cos(turn), s = std::sin(turn);
        const std::size_t otherRun = std::size_t(out ? k - 1 : k);
        const RunCurve then = curveOf(points, moved, otherRun, known);
        RunCurve turned = then;
        QPointF &handle = out ? turned.p2 : turned.p1; // the anchor's handle on that run
        const QPointF h = handle - anchor;
        handle = anchor + QPointF(c * h.x() - s * h.y(), s * h.x() + c * h.y());
        lay(otherRun, then, turned);
    };
    if (turns && !out)
        turnOther(); // (the run after the anchor)
    {
        RunCurve after = before;
        (out ? after.p1 : after.p2) = to;
        lay(run, before, after); // (its points and indices as they were: the run turned is after it)
    }
    if (turns && out)
        turnOther(); // (the run before the anchor)
    refreshDirections(points);
    if (change)
        change->anchors = moved;
    return result;
}

doc::VectorStroke pulledHandles(const doc::VectorStroke &original, const std::vector<int> &anchors, int k,
                                const QPointF &outTo, const KnownHandle &known, HandleChange *change)
{
    if (isLoop(original, anchors) && k >= 0 && k < int(anchors.size())) {
        const TurnedLoop loop(original, anchors, k);
        const KnownHandle there = !known ? KnownHandle() : KnownHandle([&](int j, bool towardsNext) {
            return known(loop.unturned(j, towardsNext), towardsNext);
        });
        HandleChange done;
        doc::VectorStroke result = pulledHandles(loop.stroke, loop.anchors, loop.node, outTo, there, &done);
        loop.back(result, done.anchors);
        for (HandleChange::Handle &h : done.handles)
            h.k = loop.unturned(h.k, h.out);
        if (change)
            *change = std::move(done);
        return result;
    }
    doc::VectorStroke result = original;
    std::vector<int> moved = anchors;
    const int n = int(anchors.size());
    if (change)
        *change = {anchors, {}};
    if (k < 0 || k >= n || n < 2)
        return result;
    std::vector<BrushInput> &points = result.points;
    // Its handles are in it now: the curves round it start from there (whatever was known of them before).
    const KnownHandle inIt = [&](int j, bool towardsNext) -> std::optional<QPointF> {
        if (j == k)
            return std::nullopt;
        return known ? known(j, towardsNext) : std::nullopt;
    };
    auto lay = [&](std::size_t run, const RunCurve &, const RunCurve &after) {
        reshapeRun(points, moved, run, after);
        if (change) {
            change->handles.push_back({int(run), true, after.p1});
            change->handles.push_back({int(run + 1), false, after.p2});
        }
    };
    const QPointF anchor = at(original.points[std::size_t(anchors[std::size_t(k)])]);
    if (k + 1 < n) { // the run after: its first handle at `outTo`
        const RunCurve was = curveOf(points, moved, std::size_t(k), inIt);
        RunCurve now = was;
        now.p1 = outTo;
        lay(std::size_t(k), was, now);
    }
    if (k > 0) { // the run before: its last handle opposite (at an end with no run after, the in handle is `outTo` itself)
        const RunCurve was = curveOf(points, moved, std::size_t(k - 1), inIt);
        RunCurve now = was;
        now.p2 = k + 1 < n ? 2 * anchor - outTo : outTo;
        lay(std::size_t(k - 1), was, now);
    }
    points[std::size_t(moved[std::size_t(k)])].corner = false;
    points[std::size_t(moved[std::size_t(k)])].heldHandles = 0;
    refreshDirections(points);
    if (change)
        change->anchors = moved;
    return result;
}

doc::VectorStroke movedAnchors(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<QPointF> &offsets, std::vector<int> *anchorsAfter)
{
    doc::VectorStroke out = original;
    if (anchorsAfter)
        *anchorsAfter = anchors;
    if (anchors.empty() || offsets.size() != anchors.size()
        || std::all_of(offsets.begin(), offsets.end(), [](const QPointF &d) { return d.isNull(); }))
        return out;
    if (isLoop(original, anchors)) { // the joint's two anchors go together
        std::vector<QPointF> both = offsets;
        const QPointF joint = both.front().isNull() ? both.back() : both.front();
        if (!joint.isNull() && (both.front() != joint || both.back() != joint)) {
            both.front() = both.back() = joint;
            return movedAnchors(original, anchors, both, anchorsAfter);
        }
    }
    std::vector<BrushInput> &points = out.points;
    if (anchors.size() == 1) {
        for (BrushInput &in : points) {
            in.x += offsets[0].x();
            in.y += offsets[0].y();
        }
        return out;
    }
    // Each run beside an anchor moved bent to the curve whose ends (and their handles, along with them) moved as far: the
    // points between go as smoothly, and more are put in where it grew longer. The last run first (laying one again moves
    // the anchors after it); the anchors themselves at the end.
    std::vector<int> moved = anchors;
    for (std::size_t k = anchors.size() - 1; k-- > 0;) {
        if (offsets[k].isNull() && offsets[k + 1].isNull())
            continue;
        const RunCurve before = fitRun(points, moved[k], moved[k + 1]);
        RunCurve after = before;
        after.p0 += offsets[k], after.p1 += offsets[k];
        after.p2 += offsets[k + 1], after.p3 += offsets[k + 1];
        reshapeRun(points, moved, k, after);
    }
    for (std::size_t k = 0; k < moved.size(); ++k) {
        points[std::size_t(moved[k])].x += offsets[k].x();
        points[std::size_t(moved[k])].y += offsets[k].y();
    }
    refreshDirections(points);
    if (anchorsAfter)
        *anchorsAfter = moved;
    return out;
}

doc::VectorStroke scaledWidths(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<double> &factors)
{
    if (isLoop(original, anchors) && factors.size() == anchors.size()) { // the joint's two anchors go together
        const double joint = factors.front() == 1.0 ? factors.back() : factors.front();
        if (factors.front() != joint || factors.back() != joint) {
            std::vector<double> both = factors;
            both.front() = both.back() = joint;
            return scaledWidths(original, anchors, both);
        }
    }
    doc::VectorStroke out = original;
    if (anchors.empty() || factors.size() != anchors.size())
        return out;
    auto scale = [](BrushInput &in, double f) { in.sizeScale = std::clamp(in.sizeScale * f, 0.02, 20.0); };
    if (anchors.size() == 1) {
        for (BrushInput &in : out.points)
            scale(in, factors[0]);
        return out;
    }
    for (std::size_t k = 0; k + 1 < anchors.size(); ++k) {
        const int from = anchors[k], to = anchors[k + 1];
        if (factors[k] == 1.0 && factors[k + 1] == 1.0)
            continue;
        const std::vector<double> s = runFractions(original.points, from, to);
        for (int i = from; i < to; ++i) {
            const double w = anchorWeight(s[std::size_t(i - from)], false, false);
            scale(out.points[i], (1.0 - w) * factors[k] + w * factors[k + 1]);
        }
    }
    scale(out.points[anchors.back()], factors.back());
    return out;
}

std::optional<PathPlace> nearestPlace(const doc::VectorStroke &stroke, const QPointF &pos)
{
    const auto &points = stroke.points;
    if (points.empty())
        return std::nullopt;
    PathPlace best;
    best.point = at(points[0]);
    best.distance = std::hypot(pos.x() - points[0].x, pos.y() - points[0].y);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const QPointF a = at(points[i]), ab = at(points[i + 1]) - a;
        const double len2 = QPointF::dotProduct(ab, ab);
        const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(pos - a, ab) / len2, 0.0, 1.0) : 0.0;
        const QPointF p = a + t * ab;
        const double d = std::hypot(pos.x() - p.x(), pos.y() - p.y());
        if (d < best.distance) {
            best = {int(i), t, d, p};
        }
    }
    return best;
}

int addAnchor(doc::VectorStroke &stroke, std::vector<int> &anchors, const PathPlace &place, double snap)
{
    const std::size_t before = stroke.points.size();
    const int index = pointAt(stroke.points, place, snap);
    stroke.points[std::size_t(index)].keptAnchor = true; // put by hand: an anchor from now on
    if (stroke.points.size() != before)
        for (int &a : anchors)
            if (a >= index)
                ++a;
    const auto position = std::lower_bound(anchors.begin(), anchors.end(), index) - anchors.begin();
    if (position < std::ptrdiff_t(anchors.size()) && anchors[std::size_t(position)] == index)
        return int(position);
    anchors.insert(anchors.begin() + position, index);
    return int(position);
}

bool removeAnchor(doc::VectorStroke &stroke, std::vector<int> &anchors, int k)
{
    if (isLoop(stroke, anchors)) {
        if (anchors.size() <= 3 || k < 0 || k >= int(anchors.size()))
            return false;
        TurnedLoop loop(stroke, anchors, k);
        const std::size_t node = std::size_t(loop.node);
        auto &points = loop.stroke.points;
        BrushInput &gone = points[std::size_t(loop.anchors[node])];
        gone.corner = false;
        gone.keptAnchor = false;
        gone.heldHandles = 0;
        loop.anchors.erase(loop.anchors.begin() + std::ptrdiff_t(node));
        relayRun(points, loop.anchors, node - 1, true);
        refreshDirections(points);
        stroke = std::move(loop.stroke); // (it stays begun where it was turned to: where makes no difference)
        stroke.closed = true;
        syncJoint(stroke);
        anchors = std::move(loop.anchors);
        return true;
    }
    if (anchors.size() <= 2 || k < 0 || k >= int(anchors.size()))
        return false;
    auto &points = stroke.points;
    if (k == 0) {
        const int cut = anchors[1];
        points.erase(points.begin(), points.begin() + cut);
        anchors.erase(anchors.begin());
        for (int &a : anchors)
            a -= cut;
    } else if (k == int(anchors.size()) - 1) {
        points.erase(points.begin() + anchors[std::size_t(k - 1)] + 1, points.end());
        anchors.pop_back();
    } else {
        points[anchors[std::size_t(k)]].corner = false;
        points[anchors[std::size_t(k)]].keptAnchor = false;
        points[anchors[std::size_t(k)]].heldHandles = 0;
        anchors.erase(anchors.begin() + k);
        relayRun(points, anchors, std::size_t(k - 1));
    }
    refreshDirections(points);
    return true;
}

void toggleCorner(doc::VectorStroke &stroke, const std::vector<int> &anchors, int k)
{
    if (k < 0 || k >= int(anchors.size()))
        return;
    if (isLoop(stroke, anchors)) {
        TurnedLoop loop(stroke, anchors, k);
        toggleCorner(loop.stroke, loop.anchors, loop.node);
        loop.back(loop.stroke, loop.anchors);
        stroke = std::move(loop.stroke);
        return;
    }
    BrushInput &in = stroke.points[anchors[std::size_t(k)]];
    in.corner = !in.corner;
    in.heldHandles = 0; // smooth, or a corner with its handles out
    if (in.corner || k == 0 || k == int(anchors.size()) - 1)
        return;
    relayRun(stroke.points, anchors, std::size_t(k - 1));
    relayRun(stroke.points, anchors, std::size_t(k));
    refreshDirections(stroke.points);
}

namespace {

// The way a line of points goes at its last point: from a point a little back along it (unit length; null if the
// points do not go anywhere).
QPointF wayAtEnd(const std::vector<BrushInput> &points, double back)
{
    const QPointF end = at(points.back());
    double walked = 0;
    for (std::size_t i = points.size() - 1; i > 0; --i) {
        walked += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
        if (walked >= back || i == 1) {
            const QPointF d = end - at(points[i - 1]);
            const double length = std::hypot(d.x(), d.y());
            return length > 1e-9 ? d / length : QPointF();
        }
    }
    return {};
}

// The pressure the pen let fall over the last stretch of a line (up to `reach` long, and a third of the line) lifted
// back to the most it had on that stretch: wholly near the end, less and less away from it.
void liftEndPressure(std::vector<BrushInput> &points, double reach)
{
    double total = 0;
    for (std::size_t i = 1; i < points.size(); ++i)
        total += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    reach = std::min(reach, total / 3.0);
    if (points.size() < 2 || reach <= 1e-9)
        return;
    std::vector<double> back{0.0}; // from the end
    std::size_t first = points.size() - 1;
    double body = points.back().pressure;
    while (first > 0) {
        const double d = back.back() + std::hypot(points[first].x - points[first - 1].x,
                                                  points[first].y - points[first - 1].y);
        if (d > reach)
            break;
        --first;
        back.push_back(d);
        body = std::max(body, points[first].pressure);
    }
    for (std::size_t j = 0; j < back.size(); ++j) {
        BrushInput &in = points[points.size() - 1 - j];
        // All the way up over the half next to the end, then less and less to where the stretch begins.
        const double x = std::clamp(2.0 * back[j] / reach - 1.0, 0.0, 1.0);
        const double w = 1.0 - x * x * (3.0 - 2.0 * x);
        if (in.pressure < body)
            in.pressure += (body - in.pressure) * w;
    }
}

void reverse(std::vector<BrushInput> &points)
{
    std::reverse(points.begin(), points.end());
    for (BrushInput &in : points)
        in.direction = std::remainder(in.direction + 180.0, 360.0);
}

// `b` put after `line`, a smooth curve that leaves and meets each along its way filling the gap between them (the
// pen's pressure and the rest going from one to the other on it).
void joinAcross(std::vector<BrushInput> &line, std::vector<BrushInput> b)
{
    const BrushInput p = line.back(), q = b.front();
    const QPointF from = at(p), to = at(q);
    const double gap = std::hypot(to.x() - from.x(), to.y() - from.y());
    if (gap <= 1e-9) {
        line.insert(line.end(), b.begin() + 1, b.end()); // the ends on each other: one point
        return;
    }
    // Leaving the first along its line, meeting the second along its own (a cubic Hermite curve).
    const double back = std::clamp(gap / 2.0, 2.0, 12.0);
    const QPointF straight = (to - from) / gap;
    QPointF leave = wayAtEnd(line, back);
    std::vector<BrushInput> reversedB = b;
    std::reverse(reversedB.begin(), reversedB.end());
    QPointF meet = -wayAtEnd(reversedB, back);
    if (leave.isNull())
        leave = straight;
    if (meet.isNull())
        meet = straight;
    const QPointF m0 = leave * gap, m1 = meet * gap;
    // As close together as the lines' points are, about.
    double spacing = 0;
    int steps = 0;
    for (const std::vector<BrushInput> *points : {&std::as_const(line), &std::as_const(b)})
        for (std::size_t i = 1; i < points->size(); ++i, ++steps)
            spacing += std::hypot((*points)[i].x - (*points)[i - 1].x, (*points)[i].y - (*points)[i - 1].y);
    spacing = std::clamp(steps > 0 ? spacing / steps : 2.0, 0.5, 4.0);
    const int n = std::max(1, int(std::ceil(gap / spacing)));
    for (int i = 1; i < n; ++i) {
        const double t = double(i) / n, t2 = t * t, t3 = t2 * t;
        const QPointF point = (2 * t3 - 3 * t2 + 1) * from + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * to
                              + (t3 - t2) * m1;
        BrushInput in = brush::DabGenerator::interpolate(p, q, t);
        in.x = point.x();
        in.y = point.y();
        in.corner = false;
        line.push_back(in);
    }
    line.insert(line.end(), b.begin(), b.end());
}

} // namespace

doc::VectorStroke mergedStrokes(const doc::VectorStroke &first, bool firstAtEnd, const doc::VectorStroke &second,
                                bool secondAtEnd)
{
    doc::VectorStroke out = first;
    out.closed = false;
    std::vector<BrushInput> a = first.points, b = second.points;
    if (!firstAtEnd)
        reverse(a);
    if (secondAtEnd)
        reverse(b);
    if (a.empty() || b.empty()) {
        out.points = a.empty() ? b : a;
        return out;
    }
    a.back().corner = false;
    b.front().corner = false;
    // No thinning where they meet: the pressure let fall at the ends lifted back.
    liftEndPressure(a, std::max(4.0 * first.size, 24.0));
    std::reverse(b.begin(), b.end());
    liftEndPressure(b, std::max(4.0 * second.size, 24.0));
    std::reverse(b.begin(), b.end());
    out.points = std::move(a);
    joinAcross(out.points, std::move(b));
    refreshDirections(out.points);
    return out;
}

doc::VectorStroke closedStroke(const doc::VectorStroke &stroke)
{
    doc::VectorStroke out = stroke;
    if (stroke.closed || stroke.fill || stroke.points.size() < 3)
        return out;
    std::vector<BrushInput> points = stroke.points;
    points.front().corner = false;
    points.back().corner = false;
    // No thinning at the joint: the pressure let fall at both ends lifted back.
    const double reach = std::max(4.0 * stroke.size, 24.0);
    liftEndPressure(points, reach);
    std::reverse(points.begin(), points.end());
    liftEndPressure(points, reach);
    std::reverse(points.begin(), points.end());
    // Round to the start again: the start of the line goes after its end, the gap filled between, and what came after
    // the first of its points (on the line already) is cut off again.
    const std::size_t had = points.size();
    const std::vector<BrushInput> start(points.begin(), points.begin() + std::ptrdiff_t(std::min<std::size_t>(had, 64)));
    const bool touching = std::hypot(points.back().x - points.front().x, points.back().y - points.front().y) <= 1e-9;
    if (!touching) {
        joinAcross(points, start);
        points.resize(points.size() - (start.size() - 1));
    }
    points.back() = points.front();
    out.points = std::move(points);
    out.closed = true;
    refreshDirections(out.points);
    return out;
}

doc::VectorStroke openedStroke(const doc::VectorStroke &loop, const PathPlace &place, double snap)
{
    doc::VectorStroke out = loop;
    out.closed = false;
    if (!loop.closed || out.points.size() < 3)
        return out;
    int index = pointAt(out.points, place, snap);
    const int distinct = int(out.points.size()) - 1;
    if (index >= distinct)
        index = 0;
    std::vector<BrushInput> turned(out.points.begin() + index, out.points.begin() + distinct);
    turned.insert(turned.end(), out.points.begin(), out.points.begin() + index + 1);
    for (BrushInput *end : {&turned.front(), &turned.back()}) {
        end->corner = false;
        end->heldHandles = 0;
    }
    refreshDirections(turned);
    out.points = std::move(turned);
    return out;
}

void syncJoint(doc::VectorStroke &stroke)
{
    if (!stroke.closed || stroke.points.size() < 2)
        return;
    BrushInput &first = stroke.points.front(), &last = stroke.points.back();
    first.corner = last.corner = first.corner || last.corner;
    first.keptAnchor = last.keptAnchor = first.keptAnchor || last.keptAnchor;
    const auto held = decltype(first.heldHandles)(first.corner ? first.heldHandles | last.heldHandles : 0);
    first.heldHandles = last.heldHandles = held;
}

std::optional<std::pair<doc::VectorStroke, doc::VectorStroke>> cutStroke(const doc::VectorStroke &stroke,
                                                                         const PathPlace &place, double snap)
{
    doc::VectorStroke whole = stroke;
    const int index = pointAt(whole.points, place, snap);
    if (index <= 0 || index >= int(whole.points.size()) - 1)
        return std::nullopt;
    doc::VectorStroke first = whole, second = whole;
    first.points.assign(whole.points.begin(), whole.points.begin() + index + 1);
    second.points.assign(whole.points.begin() + index, whole.points.end());
    first.points.back().corner = false;
    second.points.front().corner = false;
    return std::pair{std::move(first), std::move(second)};
}

QRectF strokeReach(const doc::VectorStroke &stroke)
{
    return pointsReach(stroke, 0, int(stroke.points.size()) - 1);
}

QRectF pointsReach(const doc::VectorStroke &stroke, int from, int to)
{
    from = std::max(from, 0);
    to = std::min(to, int(stroke.points.size()) - 1);
    if (from > to)
        return {};
    double minX = stroke.points[from].x, maxX = minX, minY = stroke.points[from].y, maxY = minY, widest = 0;
    for (int i = from; i <= to; ++i) {
        const BrushInput &in = stroke.points[std::size_t(i)];
        minX = std::min(minX, in.x);
        maxX = std::max(maxX, in.x);
        minY = std::min(minY, in.y);
        maxY = std::max(maxY, in.y);
        widest = std::max(widest, in.sizeScale);
    }
    const double margin = stroke.size * std::max(1.0, widest) * 1.5 + 2.0; // a soft edge reaches past the radius
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-margin, -margin, margin, margin);
}

} // namespace mothion::canvas
