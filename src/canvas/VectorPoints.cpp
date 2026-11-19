#include "canvas/VectorPoints.h"

#include "core/brush/DabGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

using brush::BrushInput;

QPointF at(const BrushInput &in) { return {in.x, in.y}; }

double segmentDistance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    const QPointF d = p - (a + t * ab);
    return std::hypot(d.x(), d.y());
}

void simplify(const std::vector<BrushInput> &points, int first, int last, double tolerance, std::vector<int> &out)
{
    if (last - first < 2)
        return;
    double farthest = -1;
    int index = first;
    for (int i = first + 1; i < last; ++i) {
        const double d = segmentDistance(at(points[i]), at(points[first]), at(points[last]));
        if (d > farthest) {
            farthest = d;
            index = i;
        }
    }
    if (farthest <= tolerance)
        return;
    simplify(points, first, index, tolerance, out);
    out.push_back(index);
    simplify(points, index, last, tolerance, out);
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
void relayRun(std::vector<BrushInput> &points, const std::vector<int> &anchors, std::size_t k)
{
    const int from = anchors[k], to = anchors[k + 1];
    if (to - from < 2)
        return;
    const QPointF p1 = at(points[from]), p2 = at(points[to]);
    const QPointF p0 = points[from].corner || k == 0 ? p1 : at(points[anchors[k - 1]]);
    const QPointF p3 = points[to].corner || k + 2 >= anchors.size() ? p2 : at(points[anchors[k + 2]]);
    const std::vector<double> s = runFractions(points, from, to);
    for (int i = from + 1; i < to; ++i) {
        const QPointF p = catmullRom(p0, p1, p2, p3, s[std::size_t(i - from)]);
        points[i].x = p.x();
        points[i].y = p.y();
    }
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
    // The ends and the corners stay; between them, the fewest points that keep the line.
    std::vector<int> fixed{0};
    for (int i = 1; i + 1 < n; ++i)
        if (stroke.points[i].corner)
            fixed.push_back(i);
    fixed.push_back(n - 1);
    std::vector<int> out{0};
    for (std::size_t k = 0; k + 1 < fixed.size(); ++k) {
        simplify(stroke.points, fixed[k], fixed[k + 1], tolerance, out);
        out.push_back(fixed[k + 1]);
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

doc::VectorStroke movedAnchors(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<QPointF> &offsets)
{
    doc::VectorStroke out = original;
    if (anchors.empty() || offsets.size() != anchors.size()
        || std::all_of(offsets.begin(), offsets.end(), [](const QPointF &d) { return d.isNull(); }))
        return out;
    std::vector<BrushInput> &points = out.points;
    if (anchors.size() == 1) {
        for (BrushInput &in : points) {
            in.x += offsets[0].x();
            in.y += offsets[0].y();
        }
        return out;
    }
    for (std::size_t k = 0; k + 1 < anchors.size(); ++k) {
        const int from = anchors[k], to = anchors[k + 1];
        if (offsets[k].isNull() && offsets[k + 1].isNull())
            continue;
        const std::vector<double> s = runFractions(original.points, from, to);
        const bool startCorner = original.points[from].corner, endCorner = original.points[to].corner;
        // The last point of a run is the first of the next: it is moved there.
        for (int i = from; i < to; ++i) {
            const double w = anchorWeight(s[std::size_t(i - from)], startCorner, endCorner);
            const QPointF d = (1.0 - w) * offsets[k] + w * offsets[k + 1];
            points[i].x += d.x();
            points[i].y += d.y();
        }
    }
    points[anchors.back()].x += offsets.back().x();
    points[anchors.back()].y += offsets.back().y();
    refreshDirections(points);
    return out;
}

doc::VectorStroke scaledWidths(const doc::VectorStroke &original, const std::vector<int> &anchors,
                               const std::vector<double> &factors)
{
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
    BrushInput &in = stroke.points[anchors[std::size_t(k)]];
    in.corner = !in.corner;
    if (in.corner || k == 0 || k == int(anchors.size()) - 1)
        return;
    relayRun(stroke.points, anchors, std::size_t(k - 1));
    relayRun(stroke.points, anchors, std::size_t(k));
    refreshDirections(stroke.points);
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
